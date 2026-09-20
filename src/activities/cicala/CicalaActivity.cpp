#ifdef CICALA_ENABLED
#include "CicalaActivity.h"

#include <HalPowerManager.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_random.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "activities/network/WifiSelectionActivity.h"
#include "cicala/CicalaLogo.h"
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
Rect asRect(CicalaLayout::Box box) { return Rect{box.x, box.y, box.width, box.height}; }
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
  options = about = confirmNew = false;
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
  options = about = confirmNew = false;
  notice = nullptr;
  renderer.clearScreen();
  drawBody(session.state());
  const auto footer = controlBounds(0, 1);
  renderer.fillRect(footer.x - 4, footer.y - 4, footer.width + 8, footer.height + 8, false);
  drawWrapped(footer, SMALL_FONT_ID, tr(STR_CICALA_ASLEEP), true);
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
  bool touchUpdate = false;
  if (tapped) {
    const auto& play = session.state();
    const int count = controlCount(play);
    for (int column = 0; column < count; ++column) {
      if (!contains(controlBounds(column, count), touchX, touchY)) continue;
      Button button = Button::Back;
      if (syncing.load() || about || (options && !confirmNew && !notice) || (notice && ready)) {
        button = Button::Back;
      } else if (!ready) {
        touchUpdate = column == 1;
      } else if (confirmNew) {
        confirmStart = column == 1;
        button = column == 1 ? Button::Confirm : Button::Back;
      } else if (play.menu) {
        button = column == 1 ? Button::Confirm : Button::Back;
      } else if (play.kind == static_cast<uint8_t>(cicala::ViewKind::Empty) && corpus.count() == 0) {
        button = Button::Confirm;
        touchUpdate = column == 1;
      } else {
        const Button controls[] = {Button::Confirm, Button::Left, Button::Right};
        button = controls[column];
      }
      if (!touchUpdate) accepted |= 1u << static_cast<unsigned>(button);
    }
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
  if (touchUpdate) {
    options = true;
    notice = nullptr;
    lock.unlock();
    connectForSync();
    return;
  }
  if (!ready && (notice || (!options && !about)) && released(Button::Back)) {
    lock.unlock();
    activityManager.goHome(HomeMenuItem::CICALA);
    return;
  }
  if (notice) {
    if (released(Button::Back) || released(Button::Confirm)) {
      notice = nullptr;
      if (!ready) options = true;
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
  if (confirmNew) {
    if (released(Button::Back)) {
      confirmNew = false;
      queuePaint();
    } else if (released(Button::NavNext) || released(Button::NavPrevious) || released(Button::Left) ||
               released(Button::Right)) {
      confirmStart = !confirmStart;
      queuePaint();
    } else if (released(Button::Confirm)) {
      if (confirmStart)
        action(cicala::Action::NewSession);
      else {
        confirmNew = false;
        queuePaint();
      }
    }
    return;
  }
  if (options) {
    bool activate = released(Button::Confirm);
    if (tapped) {
      for (int row = 0; row < 5; ++row) {
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
      option = (option + 1) % 5;
      queuePaint();
    } else if (released(Button::NavPrevious) || released(Button::Left)) {
      option = (option + 4) % 5;
      queuePaint();
    } else if (activate) {
      if (option == 0) {
        options = false;
        queuePaint();
      } else if (option == 1) {
        if (ready) {
          confirmNew = true;
          confirmStart = false;
          queuePaint();
        }
      } else if (option == 2) {
        lock.unlock();
        connectForSync();
      } else if (option == 3) {
        about = true;
        queuePaint();
      } else {
        lock.unlock();
        activityManager.goHome(HomeMenuItem::CICALA);
      }
    }
    return;
  }
  if (tapped && session.state().menu && ready) {
    for (int row = 0; row < 3; ++row) {
      if (contains(rowBounds(row, true), touchX, touchY) && session.prepareFilterRow(row)) {
        saveAfterPaint = false;
        queuePaint();
        return;
      }
    }
  }
  if (session.state().menu && ready) {
    if (released(Button::Back))
      action(cicala::Action::CancelFilters);
    else if (released(Button::Confirm)) {
      if (session.prepareFilterRow(3)) {
        saveAfterPaint = false;
        queuePaint();
      }
    } else if (released(Button::Right) || released(Button::PageForward))
      action(cicala::Action::Next);
    else if (released(Button::Left) || released(Button::PageBack))
      action(cicala::Action::Filters);
    return;
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
  if (length > TEXT_CAPACITY || width <= 0 || height <= 0) return false;
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
  int font = NOTOSANS_18_FONT_ID;
  if (!layoutText(font, bounds.width, bounds.height, text, length)) {
    font = NOTOSANS_14_FONT_ID;
    if (!layoutText(font, bounds.width, bounds.height, text, length)) {
      layoutFailed = true;
      drawWrapped(bounds, UI_12_FONT_ID, tr(STR_CICALA_TEXT_ERROR), true);
      return;
    }
  }
  const int lineHeight = renderer.getLineHeight(font);
  const int gap = lineCount > 1 && static_cast<int>(lineCount) * (lineHeight + 6) <= bounds.height ? 6 : 0;
  int y = bounds.y + (bounds.height - static_cast<int>(lineCount) * (lineHeight + gap)) / 2;
  for (size_t line = 0; line < lineCount; ++line) {
    const size_t length = lineEnds[line] - lineStarts[line];
    memcpy(textBuffer, text + lineStarts[line], length);
    textBuffer[length] = '\0';
    UITheme::drawCenteredText(renderer, bounds, font, y, textBuffer);
    y += lineHeight + gap;
  }
}

CicalaLayout CicalaActivity::layout() const {
  auto safe = UITheme::getInstance().getScreenSafeArea(renderer, !mappedInput.hasTouch(), false);
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int x = std::max(safe.x, left);
  const int y = std::max(safe.y, top);
  return {{x, y, std::min(safe.x + safe.width, renderer.getScreenWidth() - right) - x,
           std::min(safe.y + safe.height, renderer.getScreenHeight() - bottom) - y}};
}

int CicalaActivity::controlCount(const cicala::PlayState& play) const {
  if (syncing.load() || about) return 1;
  if (confirmNew) return 2;
  if (options && !notice) return 1;
  if (!ready) return 2;
  if (notice) return 1;
  if (play.menu || (play.kind == static_cast<uint8_t>(cicala::ViewKind::Empty) && corpus.count() == 0)) return 2;
  return 3;
}

Rect CicalaActivity::controlBounds(int column, int count) const { return asRect(layout().control(column, count)); }
Rect CicalaActivity::bodyBounds() const { return asRect(layout().body()); }
Rect CicalaActivity::rowBounds(int row, bool filters) const { return asRect(layout().row(row, filters)); }

int CicalaActivity::drawWrapped(Rect bounds, int font, const char* value, bool centered) {
  const size_t length = strlen(value);
  if (!layoutText(font, bounds.width, bounds.height, value, length)) {
    font = SMALL_FONT_ID;
    if (!layoutText(font, bounds.width, bounds.height, value, length)) return 0;
  }
  const int height = static_cast<int>(lineCount) * renderer.getLineHeight(font);
  int y = bounds.y + (centered ? (bounds.height - height) / 2 : 0);
  for (size_t line = 0; line < lineCount; ++line) {
    const size_t count = lineEnds[line] - lineStarts[line];
    memcpy(textBuffer, value + lineStarts[line], count);
    textBuffer[count] = '\0';
    if (centered)
      UITheme::drawCenteredText(renderer, bounds, font, y, textBuffer);
    else
      renderer.drawText(font, bounds.x, y, textBuffer);
    y += renderer.getLineHeight(font);
  }
  return height;
}

void CicalaActivity::drawHeader(const cicala::PlayState& play) {
  const auto bounds = asRect(layout().header());
  const bool question = !options && !about && !confirmNew && !notice && !play.menu;
  const bool menu = options && !about && !confirmNew && !notice;
  const int size = menu || play.menu ? 32 : 36;
  const int y = bounds.y + (bounds.height - size) / 2;
  cicala_logo::draw(renderer, bounds.x, y, size);
  if (!question) {
    const int x = bounds.x + size + 12;
    renderer.drawText(UI_12_FONT_ID, x, bounds.y + (bounds.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                      tr(STR_CICALA), true, EpdFontFamily::BOLD);
    const char* title = menu ? tr(STR_CICALA_MENU) : play.menu ? tr(STR_CICALA_FILTERS) : nullptr;
    if (title) {
      const int next = x + renderer.getTextWidth(UI_12_FONT_ID, tr(STR_CICALA), EpdFontFamily::BOLD) + 24;
      const int available = bounds.x + bounds.width - 110 - next;
      if (renderer.getTextWidth(SMALL_FONT_ID, title) <= available)
        renderer.drawText(SMALL_FONT_ID, next, bounds.y + (bounds.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2,
                          title);
    }
  }
  const auto percentage = powerManager.getBatteryPercentage();
  const int batteryX = bounds.x + bounds.width - 26;
  const int batteryY = bounds.y + 28;
  BaseTheme::drawBatteryOutline(renderer, batteryX, batteryY, 24, 12);
  GUI.fillBatteryIcon(renderer, Rect{batteryX, batteryY, 24, 12}, percentage);
  if (SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS) {
    char label[8];
    snprintf(label, sizeof(label), "%u%%", static_cast<unsigned>(percentage));
    renderer.drawText(SMALL_FONT_ID, batteryX - 12 - renderer.getTextWidth(SMALL_FONT_ID, label), batteryY - 6, label);
  }
  renderer.drawLine(bounds.x, bounds.y + bounds.height, bounds.x + bounds.width, bounds.y + bounds.height);
}

void CicalaActivity::drawButton(Rect bounds, const char* label, bool solid, bool selected) {
  if (solid)
    renderer.fillRect(bounds.x, bounds.y, bounds.width, bounds.height);
  else
    renderer.drawRect(bounds.x, bounds.y, bounds.width, bounds.height);
  if (selected) renderer.drawRect(bounds.x - 3, bounds.y - 3, bounds.width + 6, bounds.height + 6, 2, true);
  const bool next = label == tr(STR_CICALA_NEXT);
  const int font = next ? UI_12_FONT_ID : UI_10_FONT_ID;
  const auto style = next ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  if (renderer.getTextWidth(font, label, style) <= bounds.width - 16) {
    UITheme::drawCenteredText(renderer, bounds, font, bounds.y + (bounds.height - renderer.getLineHeight(font)) / 2,
                              label, !solid, style);
  } else if (layoutText(UI_10_FONT_ID, bounds.width - 16, bounds.height - 12, label, strlen(label))) {
    int y = bounds.y + (bounds.height - static_cast<int>(lineCount) * renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    for (size_t line = 0; line < lineCount; ++line) {
      const size_t length = lineEnds[line] - lineStarts[line];
      memcpy(textBuffer, label + lineStarts[line], length);
      textBuffer[length] = '\0';
      UITheme::drawCenteredText(renderer, bounds, UI_10_FONT_ID, y, textBuffer, !solid);
      y += renderer.getLineHeight(UI_10_FONT_ID);
    }
  }
}

void CicalaActivity::drawControls(const cicala::PlayState& play) {
  const char* labels[3] = {tr(STR_CICALA_MENU), tr(STR_CICALA_FILTERS), tr(STR_CICALA_NEXT)};
  const int count = controlCount(play);
  bool solid = true;
  if (syncing.load()) {
    labels[0] = tr(STR_CICALA_CANCEL_UPDATE);
    solid = false;
  } else if (about || (options && !confirmNew && !notice)) {
    labels[0] = tr(STR_BACK);
    solid = false;
  } else if (!ready) {
    labels[0] = tr(STR_CICALA_HOME);
    labels[1] = tr(STR_CICALA_UPDATE);
  } else if (notice) {
    labels[0] = tr(STR_BACK);
    solid = false;
  } else if (confirmNew) {
    labels[0] = tr(STR_CICALA_KEEP_SESSION);
    labels[1] = tr(STR_CICALA_START_NEW);
  } else if (play.menu) {
    labels[0] = tr(STR_CANCEL);
    labels[1] = tr(STR_CICALA_APPLY);
  } else if (count == 2)
    labels[1] = tr(STR_CICALA_UPDATE);
  if (mappedInput.hasTouch()) {
    for (int i = 0; i < count; ++i) {
      const bool selected = confirmNew ? (i == (confirmStart ? 1 : 0)) : play.menu && play.cursor == 3 && i == 1;
      drawButton(controlBounds(i, count), labels[i], solid && i == count - 1, selected);
    }
  } else {
    const bool navigating = options || confirmNew;
    const auto hints =
        mappedInput.mapLabels(play.menu ? tr(STR_CANCEL) : tr(STR_BACK),
                              play.menu    ? tr(STR_CICALA_APPLY)
                              : navigating ? tr(STR_SELECT)
                                           : tr(STR_CICALA_MENU),
                              navigating ? tr(STR_CICALA_PREVIOUS) : tr(STR_CICALA_FILTERS), tr(STR_CICALA_NEXT));
    GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
    GUI.drawSideButtonHints(renderer, navigating ? tr(STR_CICALA_PREVIOUS) : tr(STR_CICALA_FILTERS),
                            tr(STR_CICALA_NEXT));
  }
}

void CicalaActivity::drawRows(const char* const* labels, int selected) {
  for (int row = 0; row < 5; ++row) {
    const auto rect = rowBounds(row);
    renderer.drawLine(rect.x, rect.y + rect.height, rect.x + rect.width, rect.y + rect.height);
    if (row == selected) renderer.fillRect(rect.x, rect.y + 15, 3, 28);
    drawWrapped(Rect{rect.x + 10, rect.y + 18, rect.width - 36, rect.height - 20}, UI_10_FONT_ID, labels[row]);
    renderer.drawText(UI_10_FONT_ID, rect.x + rect.width - 20, rect.y + 18, ">", true);
  }
}

void CicalaActivity::drawBody(const cicala::PlayState& play) {
  auto bounds = bodyBounds();
  drawHeader(play);
  if (notice || (!ready && !options && !about)) {
    drawWrapped(bounds, UI_12_FONT_ID, notice ? notice : tr(STR_CICALA_CORPUS_ERROR));
  } else if (about) {
    char info[240];
    snprintf(info, sizeof(info), tr(STR_CICALA_VERSIONS), CICALA_FIRMWARE_VERSION, CICALA_UPSTREAM_VERSION,
             CICALA_UPSTREAM_REVISION, cicala::kCoreVersion, store.installedVersion());
    drawWrapped(bounds, UI_12_FONT_ID, info);
  } else if (confirmNew) {
    const int height = drawWrapped(bounds, NOTOSANS_18_FONT_ID, tr(STR_CICALA_NEW_SESSION_PROMPT));
    bounds.y += height + 24;
    bounds.height -= height + 24;
    drawWrapped(bounds, UI_10_FONT_ID, tr(STR_CICALA_NEW_SESSION_DETAIL));
  } else if (options) {
    const char* labels[] = {tr(STR_RESUME), tr(STR_CICALA_NEW_SESSION), tr(STR_CICALA_UPDATE), tr(STR_CICALA_ABOUT),
                            tr(STR_CICALA_HOME)};
    drawRows(labels, option);
  } else if (play.menu) {
    const char* names[] = {tr(STR_CICALA_DARK), tr(STR_CICALA_SEXUAL), tr(STR_CICALA_HEAVY)};
    const char* hints[] = {tr(STR_CICALA_DARK_HINT), tr(STR_CICALA_SEXUAL_HINT), tr(STR_CICALA_HEAVY_HINT)};
    for (int row = 0; row < 3; ++row) {
      const auto rect = rowBounds(row, true);
      renderer.drawLine(rect.x, rect.y + rect.height, rect.x + rect.width, rect.y + rect.height);
      if (row == play.cursor) renderer.fillRect(rect.x, rect.y + 4, 3, rect.height - 10);
      renderer.drawText(UI_10_FONT_ID, rect.x + 12, rect.y + 5, names[row]);
      if (layout().portrait())
        drawWrapped(Rect{rect.x + 12, rect.y + 40, rect.width - 62, rect.height - 44}, SMALL_FONT_ID, hints[row]);
      const int x = rect.x + rect.width - 45;
      renderer.drawRect(x, rect.y + 9, 28, 28);
      if (play.draft & (1u << row)) renderer.fillRect(x + 6, rect.y + 15, 16, 16);
    }
  } else if (play.kind == static_cast<uint8_t>(cicala::ViewKind::Empty)) {
    const bool empty = corpus.count() == 0;
    const int height =
        drawWrapped(bounds, NOTOSANS_18_FONT_ID, empty ? tr(STR_CICALA_EMPTY_COLLECTION) : tr(STR_CICALA_EMPTY_POOL));
    bounds.y += height + 24;
    bounds.height -= height + 24;
    drawWrapped(bounds, UI_10_FONT_ID, empty ? tr(STR_CICALA_EMPTY_COLLECTION_DETAIL) : tr(STR_CICALA_EMPTY));
  } else
    drawQuestion(asRect(layout().question()), play.text, play.len);
  drawControls(play);
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
