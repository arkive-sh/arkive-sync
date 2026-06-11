#include "api/ArkiveApi.hpp"
#include "api/ArkiveHttpClient.hpp"
#include "app/App.hpp"
#include "db/Sqlite.hpp"
#include "helpers/Helpers.hpp"
#include "platform/AppDataPaths.hpp"
#include "repo/UserRepo.hpp"
#include "service/AuthService.hpp"
#include "service/VaultService.hpp"
#include <exception>
#include <spdlog/spdlog.h>
#include <string>

int main(int argc, char *argv[]) {
  try {
    Database db;
    UserRepo userRepo(db.getDb());
    RustCrypto crypto;
    VaultService vaultService(userRepo, crypto);

    const std::string baseUrl = requireBaseUrl(userRepo);
    ArkiveHttpClient client(baseUrl, cookieJarPath().string());
    ArkiveApi api(client);
    AuthService authService(userRepo, api);

    App app(userRepo, authService, vaultService);
    return app.run(argc, argv);
  } catch (const std::exception &ex) {
    spdlog::error("Fatal error: {}", ex.what());
    return 1;
  }
}
