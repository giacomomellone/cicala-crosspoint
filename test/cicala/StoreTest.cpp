#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "cicala/CicalaStore.h"
#include "trusted_key.h"

#define CHECK(value)                                                   \
  do {                                                                 \
    if (!(value)) {                                                    \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); \
      std::abort();                                                    \
    }                                                                  \
  } while (0)

void snapshots() {
  CicalaStore first;
  cicala::Snapshot snapshot{};
  CHECK(!first.loadSession(snapshot));
  snapshot.play.permissions = 1;
  CHECK(first.saveSession(snapshot.play, snapshot.bag));
  snapshot.play.permissions = 3;
  CHECK(first.saveSession(snapshot.play, snapshot.bag));
  CicalaStore reboot;
  CHECK(reboot.loadSession(snapshot));
  CHECK(snapshot.play.permissions == 3 && snapshot.generation == 2);
  // A torn newest record must leave the older committed session usable.
  std::filesystem::resize_file(Storage.resolve("/.crosspoint/cicala/session-1.bin"), 17);
  CicalaStore torn;
  CHECK(torn.loadSession(snapshot));
  CHECK(snapshot.play.permissions == 1 && snapshot.generation == 1);
  Storage.writeBudget = 9;
  snapshot.play.permissions = 7;
  CHECK(!torn.saveSession(snapshot.play, snapshot.bag));
  Storage.writeBudget = -1;
  CicalaStore failedWrite;
  CHECK(failedWrite.loadSession(snapshot));
  CHECK(snapshot.play.permissions == 1);
  CHECK(CicalaStore::markResume(true));
  CHECK(CicalaStore::consumeResume());
  CHECK(!CicalaStore::consumeResume());
  Storage.readOnly = true;
  CHECK(!failedWrite.saveSession(snapshot.play, snapshot.bag));
  CHECK(!CicalaStore::markResume(true));
}

int main(int argc, char** argv) {
  CHECK(argc >= 4);
  Storage.root = argv[1];
  std::ifstream key(argv[2], std::ios::binary);
  CHECK(key.read(reinterpret_cast<char*>(CICALA_TRUSTED_KEY), 32));
  if (!strcmp(argv[3], "snapshots")) {
    snapshots();
    return 0;
  }
  CHECK(argc == 7);
  CicalaStore store;
  cicala::Qdb corpus;
  CHECK(store.openCorpus(corpus));
  std::atomic<bool> cancel{!strcmp(argv[3], "cancel")};
  if (!strcmp(argv[3], "short-write")) Storage.writeBudget = std::strtoll(argv[6], nullptr, 10);
  const auto result = store.sync(cancel);
  CHECK(static_cast<int>(result) == std::atoi(argv[4]));
  Storage.writeBudget = -1;
  CHECK(store.openCorpus(corpus));
  CHECK(!strcmp(store.installedVersion(), argv[5]));
  // A fresh adapter performs signature checks again, including after an interrupted update.
  CicalaStore reboot;
  CHECK(reboot.openCorpus(corpus));
  CHECK(!strcmp(reboot.installedVersion(), argv[5]));
  return 0;
}
