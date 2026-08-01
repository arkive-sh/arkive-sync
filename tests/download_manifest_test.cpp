#include "download/DownloadManifest.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("parseDownloadManifest computes cipher and plain ranges") {
  const auto manifest = parseDownloadManifest(
      R"json({
        "name": "movie.txt",
        "mime": "text/plain",
        "size": 5,
        "chunk_size": 4,
        "chunks": [
          {"n": 1, "plain_size": 4, "cipher_size": 10, "hash": "h1"},
          {"n": 2, "plain_size": 1, "cipher_size": 7, "hash": "h2"}
        ]
      })json",
      4, 5);

  REQUIRE(manifest.name == "movie.txt");
  REQUIRE(manifest.size == 5);
  REQUIRE(manifest.chunks.size() == 2);
  REQUIRE(manifest.chunks[0].cipherStart == 0);
  REQUIRE(manifest.chunks[0].cipherEnd == 9);
  REQUIRE(manifest.chunks[0].plainStart == 0);
  REQUIRE(manifest.chunks[0].plainSize == 4);
  REQUIRE(manifest.chunks[1].cipherStart == 10);
  REQUIRE(manifest.chunks[1].cipherEnd == 16);
  REQUIRE(manifest.chunks[1].plainStart == 4);
  REQUIRE(manifest.chunks[1].plainSize == 1);
}
