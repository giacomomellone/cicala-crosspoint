#pragma once
#ifdef CICALA_ENABLED

#include <HalStorage.h>

#include <atomic>
#include <bundle.hpp>
#include <memory>
#include <snapshot.hpp>

class CicalaStore {
 public:
  enum class SyncResult { Updated, Current, Failed, Cancelled };
  bool openCorpus(cicala::Qdb& corpus);
  void releaseCorpus() { buffer.reset(); }
  bool loadSession(cicala::Snapshot& snapshot);
  bool saveSession(const cicala::PlayState& play, const cicala::Bag::State& bag);
  SyncResult sync(std::atomic<bool>& cancelled);
  const char* installedVersion() const { return version; }
  static bool consumeResume();
  static bool markResume(bool resume);

 private:
  bool readSlot(int slot, cicala::Qdb& corpus);
  bool readManifest(int slot);
  bool ensureBuffer();
  static bool hash(const uint8_t* data, size_t size, uint8_t* digest);
  std::unique_ptr<uint8_t[]> buffer;
  // Reused for manifests and snapshots; neither operation runs concurrently.
  char scratch[cicala::kMaxManifestBytes]{};
  cicala::BundlePlan plan{};
  cicala::Snapshot snapshotScratch{};
  size_t manifestSize = 0;
  uint32_t generation = 0;
  int sessionSlot = -1;
  int activeSlot = -1;
  char version[cicala::kMaxVersionBytes] = "0.0.0";
};
#endif
