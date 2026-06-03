#include "upload/UploadPlanner.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr uint64_t kMiB = 1024ull * 1024ull;

}

TEST_CASE("UploadPlanner plans a small file as one chunk and one part") {
  const UploadPlan plan = UploadPlanner::createPlan(1ull * kMiB);

  REQUIRE(plan.originalSize == 1ull * kMiB);
  REQUIRE(plan.fileChunkSize == 4ull * kMiB);
  REQUIRE(plan.totalChunks == 1);
  REQUIRE(plan.uploadPartSize == 4ull * kMiB);
  REQUIRE(plan.chunksPerUploadPart == 1);
  REQUIRE(plan.uploadPartCount == 1);
}

TEST_CASE("UploadPlanner keeps exact chunk boundary files in one upload part") {
  const UploadPlan plan = UploadPlanner::createPlan(4ull * kMiB);

  REQUIRE(plan.originalSize == 4ull * kMiB);
  REQUIRE(plan.fileChunkSize == 4ull * kMiB);
  REQUIRE(plan.totalChunks == 1);
  REQUIRE(plan.uploadPartSize == 4ull * kMiB);
  REQUIRE(plan.chunksPerUploadPart == 1);
  REQUIRE(plan.uploadPartCount == 1);
}

TEST_CASE("UploadPlanner uses multipart sizing for larger files") {
  const UploadPlan plan = UploadPlanner::createPlan(12ull * kMiB);

  REQUIRE(plan.originalSize == 12ull * kMiB);
  REQUIRE(plan.fileChunkSize == 4ull * kMiB);
  REQUIRE(plan.totalChunks == 3);
  REQUIRE(plan.uploadPartSize == 8ull * kMiB);
  REQUIRE(plan.chunksPerUploadPart == 2);
  REQUIRE(plan.uploadPartCount == 2);
}

TEST_CASE("UploadPlanner supports empty files as a single logical chunk") {
  const UploadPlan plan = UploadPlanner::createPlan(0);

  REQUIRE(plan.originalSize == 0);
  REQUIRE(plan.fileChunkSize == 4ull * kMiB);
  REQUIRE(plan.totalChunks == 1);
  REQUIRE(plan.uploadPartSize == 4ull * kMiB);
  REQUIRE(plan.chunksPerUploadPart == 1);
  REQUIRE(plan.uploadPartCount == 1);
}
