#pragma once
#include <fcntl.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

class HostFile {
 public:
  explicit HostFile(int descriptor = -1) : fd(descriptor) {}
  HostFile(const HostFile&) = delete;
  HostFile(HostFile&& other) noexcept : fd(other.fd) { other.fd = -1; }
  ~HostFile() { close(); }
  explicit operator bool() const { return fd >= 0; }
  size_t size() const;
  int read(void* into, size_t size);
  size_t write(const void* data, size_t size);
  void flush();
  bool close();

 private:
  int fd;
};

struct HostStorage {
  std::filesystem::path root;
  int64_t writeBudget = -1;
  bool readOnly = false;
  HostFile open(const char* path, int flags = O_RDONLY);
  bool exists(const char* path);
  bool remove(const char* path);
  bool ensureDirectoryExists(const char* path);
  std::filesystem::path resolve(const char* path) const { return root / (path + 1); }
};
extern HostStorage Storage;
const char* testManifestUrl();
