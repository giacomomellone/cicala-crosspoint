#ifdef CICALA_ENABLED
#include "CicalaActivity.h"

#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_random.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
uint32_t random32(void*) { return esp_random(); }
constexpr uint32_t REFRESH_TIMEOUT_MS = 35000;
constexpr unsigned FULL_REFRESH_INTERVAL = 10;
constexpr uint32_t TOUCH_CONTACT = 1u << 31;
bool contains(Rect rect, int x, int y) {
  return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}
}  // namespace

CicalaActivity::CicalaActivity(GfxRenderer& renderer, MappedInputManager& input)
    : Activity("Cicala", renderer, input), session(snapshot.play, random32, nullptr) {}

void CicalaActivity::onEnter() {
  RenderLock lock;
  Activity::onEnter();
  CicalaStore::markResume(false);
  store.loadSession(snapshot);
  ready = reloadCorpus();
  if (!ready) notice = tr(STR_CICALA_CORPUS_ERROR);
  if (ready && !snapshot.play.len && !snapshot.play.menu &&
      snapshot.play.kind != static_cast<uint8_t>(cicala::ViewKind::Empty))
    action(cicala::Action::Next);
  else
    queuePaint(true);
  LOG_DBG("CIC", "Entered, free heap: %u", ESP.getFreeHeap());
}

bool CicalaActivity::reloadCorpus() {
  session.cancel();
  return store.openCorpus(corpus) && session.bindCorpus(corpus, snapshot.bag);
}

void CicalaActivity::queuePaint(bool full) {
  fullRefresh = fullRefresh || full;
  paintDone.store(false);
  painting.store(true);
  startedPaint = millis();
  requestUpdate();
}

void CicalaActivity::action(cicala::Action actionValue) {
  if (!ready || !session.prepare(actionValue)) return;
  saveAfterPaint = actionValue == cicala::Action::NewSession;
  options = about = false;
  notice = nullptr;
  queuePaint(actionValue == cicala::Action::NewSession);
}

void CicalaActivity::finishPaint() {
  if (!paintDone.exchange(false)) return;
  if (millis() - startedPaint > REFRESH_TIMEOUT_MS) paintResult = cicala::RenderResult::Timeout;
  const bool committed = session.complete(paintToken, paintResult);
  painting.store(false);
  if (paintResult != cicala::RenderResult::Complete) {
    fullRefresh = true;
    notice = tr(STR_CICALA_REFRESH_ERROR);
    LOG_ERR("CIC", "Refresh failed; pending state discarded");
  }
  if (committed && saveAfterPaint && !store.saveSession(snapshot.play, snapshot.bag)) {
    notice = tr(STR_CICALA_SAVE_ERROR);
    queuePaint();
  }
  saveAfterPaint = false;
}

void CicalaActivity::stopOwnedWifi() {
  if (!ownsWifi) return;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  ownsWifi = false;
}

void CicalaActivity::stopSync() {
  cancelled.store(true);
  if (worker) {
    // The worker never takes RenderLock, including during cancellation.
    while (!syncDone.load()) vTaskDelay(pdMS_TO_TICKS(10));
    vTaskDelete(worker);
    worker = nullptr;
  }
  syncing.store(false);
  stopOwnedWifi();
}

void CicalaActivity::onExit() {
  stopSync();
  finishPaint();
  session.cancel();
  if (!store.saveSession(snapshot.play, snapshot.bag)) LOG_ERR("CIC", "Could not persist session on exit");
  store.releaseCorpus();
  if (!sleepPrepared) CicalaStore::markResume(false);
  Activity::onExit();
}

void CicalaActivity::prepareForSleep() {
  sleepPrepared = true;
  stopSync();
  finishPaint();
  session.cancel();
  // ActivityManager holds RenderLock here. Preserve the committed conversation
  // view even when sleep interrupts an options page or a bundle download.
  options = about = false;
  notice = nullptr;
  renderer.clearScreen();
  drawBody(session.state());
  renderer.displayBufferChecked(HalDisplay::FULL_REFRESH);
  const bool saved = store.saveSession(snapshot.play, snapshot.bag);
  CicalaStore::markResume(saved);
}

bool CicalaActivity::handleForcedRefresh() {
  RenderLock lock;
  if (!painting.load() && !syncing.load()) queuePaint(true);
  return true;
}

void CicalaActivity::connectForSync() {
  RenderLock lock;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    notice = tr(STR_CICALA_UPDATE_ERROR);
    queuePaint();
    return;
  }
  ownsWifi = WiFi.getMode() == WIFI_MODE_NULL;
  awaitingWifi = true;
  store.saveSession(snapshot.play, snapshot.bag);
  store.releaseCorpus();
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    RenderLock lock;
    awaitingWifi = false;
    const auto* connected = std::get_if<WifiResult>(&result.data);
    if (!result.isCancelled && connected && connected->connected)
      startSync();
    else {
      stopOwnedWifi();
      ready = reloadCorpus();
      queuePaint(true);
    }
  });
}

void CicalaActivity::startSync() {
  cancelled.store(false);
  syncDone.store(false);
  syncing.store(true);
  notice = tr(STR_CICALA_UPDATING);
  // Signature verification and HTTP run off the UI stack; storage buffers are members.
  if (xTaskCreate(syncTask, "CicalaSync", 12288, this, 1, &worker) != pdPASS) {
    worker = nullptr;
    syncing.store(false);
    stopOwnedWifi();
    ready = reloadCorpus();
    notice = tr(STR_CICALA_UPDATE_ERROR);
  }
  queuePaint();
}

void CicalaActivity::syncTask(void* context) {
  auto* self = static_cast<CicalaActivity*>(context);
  self->syncResult = self->store.sync(self->cancelled);
  self->syncDone.store(true);
  // The owner deletes the task before destroying any referenced state.
  vTaskSuspend(nullptr);
}

void CicalaActivity::loop() {
  using Button = MappedInputManager::Button;
  uint32_t down = 0, edges = 0;
  for (unsigned i = 0; i <= static_cast<unsigned>(Button::ScreenDown); ++i) {
    if (mappedInput.isPressed(static_cast<Button>(i))) down |= 1u << i;
    if (mappedInput.wasReleased(static_cast<Button>(i))) edges |= 1u << i;
  }
  int touchX = 0, touchY = 0;
  if (mappedInput.isScreenTouchHeld(touchX, touchY)) down |= TOUCH_CONTACT;
  if (mappedInput.wasScreenTouchReleased()) edges |= TOUCH_CONTACT;
  auto accepted = inputGate.update(painting.load(), down, edges);
  if ((edges & TOUCH_CONTACT) && !(accepted & TOUCH_CONTACT) && mappedInput.wasBackGesture())
    accepted &= ~(1u << static_cast<unsigned>(Button::Back));
  const bool tapped = (accepted & TOUCH_CONTACT) && mappedInput.wasScreenTapped(touchX, touchY);
  const auto released = [&](Button button) { return accepted & (1u << static_cast<unsigned>(button)); };
  if (painting.load()) {
    if (paintDone.load()) {
      RenderLock lock;
      finishPaint();
    }
    return;
  }
  if (tapped) {
    const Button controls[] = {Button::Back, Button::Confirm, Button::Left, Button::Right};
    for (int column = 0; column < 4; ++column)
      if (contains(controlBounds(column), touchX, touchY)) accepted |= 1u << static_cast<unsigned>(controls[column]);
  }
  if (syncing.load()) {
    if (released(Button::Back)) cancelled.store(true);
    if (syncDone.load()) {
      stopSync();
      RenderLock lock;
      ready = reloadCorpus();
      notice = syncResult == CicalaStore::SyncResult::Updated     ? tr(STR_CICALA_UPDATED)
               : syncResult == CicalaStore::SyncResult::Current   ? tr(STR_CICALA_CURRENT)
               : syncResult == CicalaStore::SyncResult::Cancelled ? tr(STR_CICALA_CANCELLED)
                                                                  : tr(STR_CICALA_UPDATE_ERROR);
      queuePaint(true);
    }
    return;
  }
  RenderLock lock;
  if (notice) {
    if (released(Button::Back) || released(Button::Confirm)) {
      notice = nullptr;
      queuePaint();
    }
    return;
  }
  if (about) {
    if (released(Button::Back) || released(Button::Confirm)) {
      about = false;
      queuePaint();
    }
    return;
  }
  if (options) {
    bool activate = released(Button::Confirm);
    if (tapped) {
      for (int row = 0; row < 4; ++row) {
        if (contains(rowBounds(row), touchX, touchY)) {
          option = row;
          activate = true;
          break;
        }
      }
    }
    if (released(Button::Back)) {
      options = false;
      queuePaint();
    } else if (released(Button::NavNext) || released(Button::Right)) {
      option = (option + 1) % 4;
      queuePaint();
    } else if (released(Button::NavPrevious) || released(Button::Left)) {
      option = (option + 3) % 4;
      queuePaint();
    } else if (activate) {
      if (option == 0) {
        options = false;
        queuePaint();
      } else if (option == 1)
        action(cicala::Action::NewSession);
      else if (option == 2) {
        lock.unlock();
        connectForSync();
      } else {
        about = true;
        queuePaint();
      }
    }
    return;
  }
  if (tapped && session.state().menu && ready) {
    for (int row = 0; row < 4; ++row) {
      if (contains(rowBounds(row), touchX, touchY) && session.prepareFilterRow(row)) {
        saveAfterPaint = false;
        queuePaint();
        return;
      }
    }
  }
  if (released(Button::Back)) {
    lock.unlock();
    activityManager.goHome(HomeMenuItem::CICALA);
  } else if (released(Button::Confirm)) {
    options = true;
    option = 0;
    queuePaint();
  } else if (released(Button::Right) || released(Button::PageForward))
    action(cicala::Action::Next);
  else if (released(Button::Left) || released(Button::PageBack))
    action(cicala::Action::Filters);
}

bool CicalaActivity::layoutText(int font, int width, int height, const char* text, size_t length) {
  lineCount = 0;
  size_t start = 0;
  while (start < length) {
    while (start < length && text[start] == ' ') ++start;
    if (start == length) break;
    size_t end = start, fit = start, space = start;
    while (end < length && text[end] != '\n') {
      if (text[end] == ' ') space = end;
      ++end;
      while (end < length && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) ++end;
      memcpy(textBuffer, text + start, end - start);
      textBuffer[end - start] = '\0';
      if (renderer.getTextWidth(font, textBuffer) > width) break;
      fit = end;
    }
    if (fit == start && end != start) return false;
    if (end < length && text[end] != '\n' && space > start && space <= fit) fit = space;
    if (lineCount >= cicala::kMaxQuestionBytes) return false;
    lineStarts[lineCount] = start;
    lineEnds[lineCount++] = fit;
    start = fit;
    if (start < length && text[start] == '\n') ++start;
  }
  return lineCount * renderer.getLineHeight(font) <= static_cast<size_t>(height);
}

void CicalaActivity::drawQuestion(Rect bounds, const char* text, size_t length) {
  int font = SETTINGS.getReaderFontId();
  if (!layoutText(font, bounds.width, bounds.height, text, length)) {
    font = UI_12_FONT_ID;
    if (!layoutText(font, bounds.width, bounds.height, text, length)) {
      layoutFailed = true;
      GUI.drawHelpText(renderer, bounds, tr(STR_CICALA_TEXT_ERROR));
      return;
    }
  }
  int y = bounds.y + (bounds.height - static_cast<int>(lineCount) * renderer.getLineHeight(font)) / 2;
  for (size_t line = 0; line < lineCount; ++line) {
    const size_t length = lineEnds[line] - lineStarts[line];
    memcpy(textBuffer, text + lineStarts[line], length);
    textBuffer[length] = '\0';
    UITheme::drawCenteredText(renderer, bounds, font, y, textBuffer);
    y += renderer.getLineHeight(font);
  }
}

Rect CicalaActivity::controlBounds(int column) const {
  const int height = std::max(56, 2 * renderer.getLineHeight(UI_10_FONT_ID) + 8);
  const int step = (renderer.getScreenWidth() - 16) / 4;
  return Rect{8 + column * step, renderer.getScreenHeight() - height - 8, step - 4, height};
}

Rect CicalaActivity::bodyBounds() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  auto bounds = UITheme::getInstance().getScreenSafeArea(renderer, !mappedInput.hasTouch(), true);
  if (mappedInput.hasTouch()) bounds.height = controlBounds(0).y - metrics.verticalSpacing;
  bounds.y += metrics.headerHeight + metrics.verticalSpacing;
  bounds.height -= metrics.headerHeight + 2 * metrics.verticalSpacing;
  bounds.x += metrics.contentSidePadding;
  bounds.width -= 2 * metrics.contentSidePadding;
  return bounds;
}

Rect CicalaActivity::rowBounds(int row) const {
  const auto bounds = bodyBounds();
  const int gap = UITheme::getInstance().getMetrics().menuSpacing;
  const int height = std::min(std::max(56, GUI.getMenuRowHeight(renderer)), (bounds.height - 3 * gap) / 4);
  return Rect{bounds.x, bounds.y + row * (height + gap), bounds.width, height};
}

void CicalaActivity::drawRows(const char* const* labels, int selected) {
  for (int row = 0; row < 4; ++row) {
    const auto rect = rowBounds(row);
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 1, true);
    if (row == selected) renderer.fillRect(rect.x, rect.y, 4, rect.height);
    BaseTheme::drawHintLabel(renderer, UI_10_FONT_ID, labels[row], rect.x + 8, rect.width - 16, rect.y, rect.height,
                             (rect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2);
  }
}

void CicalaActivity::drawBody(const cicala::PlayState& play) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  auto bounds = bodyBounds();
  GUI.drawHeader(
      renderer,
      Rect{bounds.x, bounds.y - metrics.headerHeight - metrics.verticalSpacing, bounds.width, metrics.headerHeight},
      tr(STR_CICALA));
  if (notice)
    GUI.drawHelpText(renderer, bounds, notice);
  else if (about) {
    char info[240];
    snprintf(info, sizeof(info), tr(STR_CICALA_VERSIONS), CICALA_FIRMWARE_VERSION, CICALA_UPSTREAM_VERSION,
             CICALA_UPSTREAM_REVISION, cicala::kCoreVersion, store.installedVersion());
    GUI.drawHelpText(renderer, bounds, info);
  } else if (options) {
    const char* labels[] = {tr(STR_RESUME), tr(STR_CICALA_NEW_SESSION), tr(STR_CICALA_UPDATE), tr(STR_CICALA_ABOUT)};
    drawRows(labels, option);
  } else if (play.menu) {
    const char* names[] = {tr(STR_CICALA_DARK), tr(STR_CICALA_SEXUAL), tr(STR_CICALA_HEAVY)};
    // Reuse the member text buffer for one row at a time; no per-frame heap buffer.
    for (int row = 0; row < 4; ++row) {
      const auto rect = rowBounds(row);
      renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 1, true);
      if (row == play.cursor) renderer.fillRect(rect.x, rect.y, 4, rect.height);
      if (row < 3)
        snprintf(textBuffer, sizeof(textBuffer), "%s: %s", names[row],
                 (play.draft & (1u << row)) ? tr(STR_CICALA_INCLUDED) : tr(STR_CICALA_EXCLUDED));
      else
        snprintf(textBuffer, sizeof(textBuffer), "%s", tr(STR_DONE));
      BaseTheme::drawHintLabel(renderer, UI_10_FONT_ID, textBuffer, rect.x + 8, rect.width - 16, rect.y, rect.height,
                               (rect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2);
    }
  } else {
    char summary[192];
    const auto permission = [&](int bit) {
      return play.permissions & bit ? tr(STR_CICALA_INCLUDED) : tr(STR_CICALA_EXCLUDED);
    };
    snprintf(summary, sizeof(summary), "%s: %s  %s: %s  %s: %s", tr(STR_CICALA_DARK), permission(1),
             tr(STR_CICALA_SEXUAL), permission(2), tr(STR_CICALA_HEAVY), permission(4));
    const int footer = 3 * renderer.getLineHeight(UI_10_FONT_ID);
    GUI.drawHelpText(renderer, Rect{bounds.x, bounds.y + bounds.height - footer, bounds.width, footer}, summary);
    bounds.height -= footer + metrics.verticalSpacing;
    if (play.kind == static_cast<uint8_t>(cicala::ViewKind::Empty))
      GUI.drawHelpText(renderer, bounds, tr(STR_CICALA_EMPTY));
    else
      drawQuestion(bounds, play.text, play.len);
  }
  const bool navigating = options && !about && !notice;
  if (mappedInput.hasTouch()) {
    const char* labels[] = {tr(STR_BACK), navigating ? tr(STR_SELECT) : tr(STR_CICALA_OPTIONS),
                            navigating ? tr(STR_CICALA_PREVIOUS) : tr(STR_CICALA_FILTERS), tr(STR_CICALA_NEXT)};
    for (int column = 0; column < 4; ++column) {
      const auto rect = controlBounds(column);
      renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 1, true);
      BaseTheme::drawHintLabel(renderer, UI_10_FONT_ID, labels[column], rect.x + 4, rect.width - 8, rect.y, rect.height,
                               (rect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2);
    }
    return;
  }
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), navigating ? tr(STR_SELECT) : tr(STR_CICALA_OPTIONS),
                            navigating ? tr(STR_CICALA_PREVIOUS) : tr(STR_CICALA_FILTERS), tr(STR_CICALA_NEXT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_CICALA_FILTERS), tr(STR_CICALA_NEXT));
}

void CicalaActivity::render(RenderLock&& lock) {
  (void)lock;
  if (!painting.exchange(true)) startedPaint = millis();
  layoutFailed = false;
  paintToken = session.busy() ? session.pending().token : 0;
  renderer.clearScreen();
  drawBody(session.busy() ? session.pending().play : session.state());
  const bool full = fullRefresh || partialRefreshes >= FULL_REFRESH_INTERVAL;
  const auto result = renderer.displayBufferChecked(full ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  paintResult = result == HalDisplay::RefreshResult::Complete  ? cicala::RenderResult::Complete
                : result == HalDisplay::RefreshResult::Timeout ? cicala::RenderResult::Timeout
                                                               : cicala::RenderResult::Failed;
  if (layoutFailed) paintResult = cicala::RenderResult::Failed;
  fullRefresh = result != HalDisplay::RefreshResult::Complete;
  partialRefreshes = full ? 0 : partialRefreshes + 1;
  paintDone.store(true);
}
#endif
