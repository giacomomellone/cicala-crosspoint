#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

class HttpDownloader {
 public:
  using DataCallback = std::function<bool(const uint8_t*, size_t)>;
  static bool fetchUrl(const std::string& url, const DataCallback& callback);
};
