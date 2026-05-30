#include "app/App.hpp"
#include <exception>
#include <spdlog/spdlog.h>

int main(int argc, char *argv[]) {
  try {
    App app;
    return app.run(argc, argv);
  } catch (const std::exception &ex) {
    spdlog::error("Fatal error: {}", ex.what());
    return 1;
  }
}
