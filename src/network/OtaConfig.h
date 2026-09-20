#pragma once

#ifndef OTA_REPOSITORY
#define OTA_REPOSITORY "crosspoint-reader/crosspoint-reader"
#endif
#ifndef OTA_VERSION
#define OTA_VERSION CROSSPOINT_VERSION
#endif
#ifndef OTA_ASSET_PREFIX
#define OTA_ASSET_PREFIX "crosspoint"
#endif

namespace ota_config {
inline constexpr char latestReleaseUrl[] = "https://api.github.com/repos/" OTA_REPOSITORY "/releases/latest";
inline constexpr char currentVersion[] = OTA_VERSION;
inline constexpr char assetPrefix[] = OTA_ASSET_PREFIX;
}  // namespace ota_config
