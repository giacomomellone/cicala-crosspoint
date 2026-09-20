#ifdef CICALA_ENABLED
#include "CicalaStore.h"

#include <Logging.h>
#include <Memory.h>
#include <mbedtls/sha256.h>
#include <trusted_key.h>

#include <cicala_assets.hpp>
#include <cstring>

#include "network/HttpDownloader.h"

#ifndef CICALA_MANIFEST_URL
#define CICALA_MANIFEST_URL "http://device.cicala.dev/device/manifest.json"
#endif

namespace {
constexpr const char* ROOT = "/.crosspoint/cicala";
constexpr const char* RESUME = "/.crosspoint/cicala/resume";
constexpr const char* SNAPSHOTS[] = {"/.crosspoint/cicala/session-0.bin", "/.crosspoint/cicala/session-1.bin"};
constexpr const char* BUNDLES[] = {"/.crosspoint/cicala/bundle-0.qdb", "/.crosspoint/cicala/bundle-1.qdb"};
constexpr const char* MANIFESTS[] = {"/.crosspoint/cicala/bundle-0.json", "/.crosspoint/cicala/bundle-1.json"};

bool readExact(const char* path, void* into, size_t size) {
  auto file = Storage.open(path);
  return file && file.size() == size && file.read(into, size) == static_cast<int>(size);
}

bool writeExact(const char* path, const void* data, size_t size) {
  auto file = Storage.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file || file.write(data, size) != size) return false;
  file.flush();
  // This handle must close before the verification read opens the same file.
  return file.close();
}
}  // namespace

bool CicalaStore::ensureBuffer() {
  // QDB borrows a contiguous image. Keep one bounded allocation while active.
  if (!buffer) buffer = makeUniqueNoThrow<uint8_t[]>(cicala::kMaxCorpusBytes);
  if (!buffer) LOG_ERR("CIC", "No memory for corpus");
  return buffer != nullptr;
}

bool CicalaStore::hash(const uint8_t* data, size_t size, uint8_t* digest) {
  return mbedtls_sha256(data, size, digest, 0) == 0;
}

bool CicalaStore::readManifest(int slot) {
  auto file = Storage.open(MANIFESTS[slot]);
  if (!file || !file.size() || file.size() > sizeof(scratch)) return false;
  manifestSize = file.size();
  return file.read(scratch, manifestSize) == static_cast<int>(manifestSize);
}

bool CicalaStore::readSlot(int slot, cicala::Qdb& corpus) {
  if (!readManifest(slot) ||
      cicala::plan_bundle(scratch, manifestSize, "en", "", plan) != cicala::BundleResult::Ready || !ensureBuffer() ||
      !readExact(BUNDLES[slot], buffer.get(), plan.entry.size))
    return false;
  uint8_t digest[32];
  return hash(buffer.get(), plan.entry.size, digest) &&
         cicala::verify_bundle(plan, buffer.get(), plan.entry.size, digest, CICALA_TRUSTED_KEY, corpus) ==
             cicala::BundleResult::Ready;
}

bool CicalaStore::openCorpus(cicala::Qdb& corpus) {
  activeSlot = -1;
  strcpy(version, "0.0.0");
  cicala::Qdb checked;
  for (int slot = 0; slot < 2; ++slot) {
    if (!readSlot(slot, checked)) continue;
    if (activeSlot < 0 || cicala::version_compare(plan.manifest.version, version) > 0) {
      activeSlot = slot;
      snprintf(version, sizeof(version), "%s", plan.manifest.version);
    }
  }
  if (activeSlot >= 0 && readSlot(activeSlot, corpus)) return true;
  activeSlot = -1;
  strcpy(version, "0.0.0");
  releaseCorpus();
  return corpus.open(cicala::assets::english, sizeof(cicala::assets::english)) && cicala::corpus_renderable(corpus);
}

bool CicalaStore::loadSession(cicala::Snapshot& out) {
  sessionSlot = -1;
  generation = 0;
  for (int slot = 0; slot < 2; ++slot) {
    if (!readExact(SNAPSHOTS[slot], scratch, cicala::kSnapshotBytes) ||
        !cicala::snapshot_decode(reinterpret_cast<uint8_t*>(scratch), cicala::kSnapshotBytes, snapshotScratch))
      continue;
    if (sessionSlot < 0 || cicala::generation_newer(snapshotScratch.generation, generation)) {
      out = snapshotScratch;
      sessionSlot = slot;
      generation = out.generation;
    }
  }
  return sessionSlot >= 0;
}

bool CicalaStore::saveSession(const cicala::PlayState& play, const cicala::Bag::State& bag) {
  if (!Storage.ensureDirectoryExists(ROOT)) return false;
  snapshotScratch = {generation + 1, play, bag};
  const int target = sessionSlot == 0 ? 1 : 0;
  auto* bytes = reinterpret_cast<uint8_t*>(scratch);
  if (!cicala::snapshot_encode(snapshotScratch, bytes, sizeof(scratch)) ||
      !writeExact(SNAPSHOTS[target], bytes, cicala::kSnapshotBytes) ||
      !readExact(SNAPSHOTS[target], bytes, cicala::kSnapshotBytes) ||
      !cicala::snapshot_decode(bytes, cicala::kSnapshotBytes, snapshotScratch) ||
      snapshotScratch.generation != generation + 1) {
    LOG_ERR("CIC", "Session save failed");
    return false;
  }
  generation = snapshotScratch.generation;
  sessionSlot = target;
  return true;
}

bool CicalaStore::consumeResume() {
  char marker[4];
  const bool resume = readExact(RESUME, marker, sizeof(marker)) && !memcmp(marker, "CSLP", 4);
  // Consume before boot routing so a crash cannot create an automatic resume loop.
  if (Storage.exists(RESUME) && !Storage.remove(RESUME)) return false;
  return resume;
}

bool CicalaStore::markResume(bool resume) {
  if (!resume) return !Storage.exists(RESUME) || Storage.remove(RESUME);
  return Storage.ensureDirectoryExists(ROOT) && writeExact(RESUME, "CSLP", 4);
}

CicalaStore::SyncResult CicalaStore::sync(std::atomic<bool>& cancelled) {
  releaseCorpus();
  if (!Storage.ensureDirectoryExists(ROOT)) return SyncResult::Failed;
  manifestSize = 0;
  bool ok = HttpDownloader::fetchUrl(CICALA_MANIFEST_URL, [&](const uint8_t* data, size_t len) {
    if (cancelled.load() || len > sizeof(scratch) - manifestSize) return false;
    memcpy(scratch + manifestSize, data, len);
    manifestSize += len;
    return true;
  });
  if (cancelled.load()) return SyncResult::Cancelled;
  if (!ok) return SyncResult::Failed;
  const auto planned = cicala::plan_bundle(scratch, manifestSize, "en", version, plan);
  if (planned == cicala::BundleResult::Current) return SyncResult::Current;
  if (planned != cicala::BundleResult::Ready) return SyncResult::Failed;
  const int target = activeSlot == 0 ? 1 : 0;
  // Remove only the inactive slot's commit record before changing its payload.
  if (Storage.exists(MANIFESTS[target]) && !Storage.remove(MANIFESTS[target])) return SyncResult::Failed;
  auto file = Storage.open(BUNDLES[target], O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return SyncResult::Failed;
  size_t received = 0;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  ok = mbedtls_sha256_starts(&sha, 0) == 0 &&
       HttpDownloader::fetchUrl(plan.entry.url, [&](const uint8_t* data, size_t len) {
         if (cancelled.load() || len > plan.entry.size - received || file.write(data, len) != len ||
             mbedtls_sha256_update(&sha, data, len) != 0)
           return false;
         received += len;
         return true;
       });
  uint8_t digest[32];
  ok = ok && received == plan.entry.size && mbedtls_sha256_finish(&sha, digest) == 0;
  mbedtls_sha256_free(&sha);
  file.flush();
  ok = file.close() && ok;
  if (cancelled.load()) return SyncResult::Cancelled;
  // The HTTP clients are gone before QDB needs the corpus allocation.
  cicala::Qdb checked;
  if (!ok || !ensureBuffer() || !readExact(BUNDLES[target], buffer.get(), received)) return SyncResult::Failed;
  uint8_t storedDigest[32];
  if (!hash(buffer.get(), received, storedDigest) || memcmp(digest, storedDigest, sizeof(digest)) ||
      cicala::verify_bundle(plan, buffer.get(), received, storedDigest, CICALA_TRUSTED_KEY, checked) !=
          cicala::BundleResult::Ready)
    return SyncResult::Failed;
  if (cancelled.load()) return SyncResult::Cancelled;
  if (!writeExact(MANIFESTS[target], scratch, manifestSize)) return SyncResult::Failed;
  // Reopening verifies the complete payload/manifest pair after the commit write.
  if (!readSlot(target, checked)) return SyncResult::Failed;
  return SyncResult::Updated;
}
#endif
