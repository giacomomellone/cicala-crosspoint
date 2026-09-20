#include <curl/curl.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>

#include "HalStorage.h"
#include "network/HttpDownloader.h"

HostStorage Storage;
uint8_t CICALA_TRUSTED_KEY[32]{};
const char* testManifestUrl() { return std::getenv("CICALA_TEST_URL"); }
size_t HostFile::size() const {
  struct stat info{};
  return fstat(fd, &info) == 0 ? info.st_size : 0;
}
int HostFile::read(void* into, size_t size) { return ::read(fd, into, size); }
size_t HostFile::write(const void* data, size_t size) {
  if (Storage.writeBudget >= 0) size = std::min(size, static_cast<size_t>(Storage.writeBudget));
  const auto written = ::write(fd, data, size);
  if (written < 0) return 0;
  if (Storage.writeBudget >= 0) Storage.writeBudget -= written;
  return written;
}
void HostFile::flush() { fsync(fd); }
bool HostFile::close() {
  if (fd < 0) return true;
  const int result = ::close(fd);
  fd = -1;
  return result == 0;
}
HostFile HostStorage::open(const char* path, int flags) {
  if (readOnly && flags != O_RDONLY) return HostFile();
  return HostFile(::open(resolve(path).c_str(), flags, 0600));
}
bool HostStorage::exists(const char* path) { return std::filesystem::exists(resolve(path)); }
bool HostStorage::remove(const char* path) { return !readOnly && std::filesystem::remove(resolve(path)); }
bool HostStorage::ensureDirectoryExists(const char* path) {
  if (readOnly) return exists(path);
  std::error_code error;
  std::filesystem::create_directories(resolve(path), error);
  return !error;
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& callback) {
  auto* curl = curl_easy_init();
  if (!curl) return false;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &callback);
  curl_easy_setopt(
      curl, CURLOPT_WRITEFUNCTION, +[](char* data, size_t size, size_t count, void* context) -> size_t {
        const auto& consume = *static_cast<const DataCallback*>(context);
        return consume(reinterpret_cast<const uint8_t*>(data), size * count) ? size * count : 0;
      });
  const bool ok = curl_easy_perform(curl) == CURLE_OK;
  curl_easy_cleanup(curl);
  return ok;
}
