#include <gtest/gtest.h>

#include "src/network/OtaRelease.h"
TEST(OtaRelease, ForkVersionsAdvanceIndependently) {
  EXPECT_TRUE(ota_release::newerStable("0.1.0", "0.1.1"));
  EXPECT_FALSE(ota_release::newerStable("0.1.0", "0.1.0"));
  EXPECT_FALSE(ota_release::newerStable("0.2.0", "0.1.9"));
  EXPECT_TRUE(ota_release::newerStable("1.6.5-rc+ce2b4fc", "1.6.5"));
  EXPECT_FALSE(ota_release::newerStable("1.6.5-slim", "1.6.5"));
}

TEST(OtaRelease, RejectsMalformedAndPrereleaseTags) {
  for (const auto* tag : {"", "1", "1.2", "v1.2.3", "1.2.3.4", "-1.2.3", "1.2.3rc", "1.2.3-rc", "4294967296.0.0",
                          "1.2.99999999999999999999", "1.2.3/trick", "1.2.3\n"}) {
    EXPECT_FALSE(ota_release::newerStable("0.0.0", tag)) << tag;
  }
  EXPECT_FALSE(ota_release::newerStable("invalid", "9.0.0"));
}

TEST(OtaRelease, AssetNamesAreBoundedAndBoardSpecific) {
  char name[96];
  ASSERT_TRUE(ota_release::assetName(name, sizeof(name), "cicala-crosspoint", "0.1.1", "-x4"));
  EXPECT_STREQ(name, "cicala-crosspoint-0.1.1-x4.bin");
  ASSERT_TRUE(ota_release::assetName(name, sizeof(name), "cicala-crosspoint", "0.1.1", "-x4pro"));
  EXPECT_STREQ(name, "cicala-crosspoint-0.1.1-x4pro.bin");
  ASSERT_TRUE(ota_release::assetName(name, sizeof(name), "crosspoint", "1.6.5", "-x3-x4"));
  EXPECT_STREQ(name, "crosspoint-1.6.5-x3-x4.bin");
  EXPECT_FALSE(ota_release::assetName(name, 4, "cicala-crosspoint", "0.1.1", "-x4"));
  EXPECT_FALSE(ota_release::assetName(name, sizeof(name), "cicala-crosspoint", "../../other", "-x4"));
}
