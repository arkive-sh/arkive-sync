#include "helpers/PathCodec.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("PathCodec stores relative paths with portable separators") {
  const std::string dbPath =
      PathCodec::toDbRelative(std::filesystem::path("movies") / "Gran Torino" / "file.mp4");

  REQUIRE(dbPath == "movies/Gran Torino/file.mp4");
}

TEST_CASE("PathCodec reconstructs native filesystem paths from db form") {
  const std::filesystem::path native =
      PathCodec::fromDbRelative("movies/Gran Torino/file.mp4");

  REQUIRE(native == std::filesystem::path("movies") / "Gran Torino" / "file.mp4");
}

TEST_CASE("PathCodec joins sync root with stored db relative path") {
  const std::filesystem::path joined = PathCodec::joinRoot(
      std::filesystem::path("/tmp/root"), "movies/Gran Torino/file.mp4");

  REQUIRE(joined ==
          std::filesystem::path("/tmp/root") / "movies" / "Gran Torino" /
              "file.mp4");
}
