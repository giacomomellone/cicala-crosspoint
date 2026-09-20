#pragma once
#ifdef CICALA_ENABLED

#include <atomic>
#include <session.hpp>

#include "activities/Activity.h"
#include "cicala/CicalaInputGate.h"
#include "cicala/CicalaLayout.h"
#include "cicala/CicalaStore.h"

struct Rect;

class CicalaActivity final : public Activity {
 public:
  CicalaActivity(GfxRenderer& renderer, MappedInputManager& input);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool preventAutoSleep() override { return painting.load() || syncing.load() || awaitingWifi; }
  bool preserveScreenOnSleep() const override { return true; }
  void prepareForSleep() override;
  bool handleForcedRefresh() override;

 private:
  static void syncTask(void* context);
  void stopSync();
  void connectForSync();
  void startSync();
  void queuePaint(bool full = false);
  void finishPaint();
  void action(cicala::Action action);
  bool reloadCorpus();
  CicalaLayout layout() const;
  Rect bodyBounds() const;
  int controlCount(const cicala::PlayState& play) const;
  Rect controlBounds(int column, int count) const;
  Rect rowBounds(int row, bool filters = false) const;
  void drawRows(const char* const* labels, int selected);
  void drawHeader(const cicala::PlayState& play);
  void drawControls(const cicala::PlayState& play);
  void drawButton(Rect bounds, const char* label, bool solid = false, bool selected = false);
  int drawWrapped(Rect bounds, int font, const char* text, bool centered = false);
  void drawBody(const cicala::PlayState& play);
  bool layoutText(int font, int width, int height, const char* text, size_t length);
  void drawQuestion(Rect bounds, const char* text, size_t length);
  void stopOwnedWifi();

  CicalaStore store;
  CicalaInputGate inputGate;
  cicala::Snapshot snapshot{};
  cicala::Qdb corpus;
  cicala::Session session;
  std::atomic<bool> painting{false};
  std::atomic<bool> paintDone{false};
  cicala::RenderResult paintResult = cicala::RenderResult::Failed;
  uint32_t paintToken = 0;
  uint32_t startedPaint = 0;
  bool fullRefresh = true;
  unsigned partialRefreshes = 0;
  bool saveAfterPaint = false;
  bool sleepPrepared = false;
  bool layoutFailed = false;
  bool options = false;
  int option = 0;
  bool about = false;
  bool confirmNew = false;
  bool confirmStart = false;
  bool ready = false;
  bool awaitingWifi = false;
  bool ownsWifi = false;
  std::atomic<bool> syncing{false};
  std::atomic<bool> syncDone{false};
  std::atomic<bool> cancelled{false};
  CicalaStore::SyncResult syncResult = CicalaStore::SyncResult::Failed;
  TaskHandle_t worker = nullptr;
  const char* notice = nullptr;
  static constexpr size_t TEXT_CAPACITY = cicala::kMaxQuestionBytes > 240 ? cicala::kMaxQuestionBytes : 240;
  char textBuffer[TEXT_CAPACITY + 1]{};
  uint16_t lineStarts[cicala::kMaxQuestionBytes]{};
  uint16_t lineEnds[cicala::kMaxQuestionBytes]{};
  size_t lineCount = 0;
};
#endif
