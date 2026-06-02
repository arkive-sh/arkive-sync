#include "api/ArkiveApi.hpp"
#include "api/ArkiveHttpClient.hpp"
#include "app/App.hpp"
#include "db/Sqlite.hpp"
#include "helpers/Helpers.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/AuthService.hpp"
#include "service/QueueService.hpp"
#include "service/SyncService.hpp"
#include "service/UploadService.hpp"
#include "service/VaultService.hpp"
#include "fs/FileEncryptor.hpp"
#include <exception>
#include <spdlog/spdlog.h>
#include <string>

namespace {

static constexpr const char *kCookiePath =
    "/home/archnuman/.local/share/arkive-sync/cookies.txt";

} // namespace

int main(int argc, char *argv[]) {
  try {
    Database db;
    // Repos
    UserRepo userRepo(db.getDb());
    SyncRepo syncRepo(db.getDb());
    QueueRepo queueRepo(db.getDb());
    // End Repos

    const std::string baseUrl = requireBaseUrl(userRepo);
    ArkiveHttpClient client(baseUrl, kCookiePath);
    ArkiveApi api(client);
    AuthService authService(userRepo, api);
    RustCrypto crypto;
    VaultService vaultService(userRepo, crypto);
    FileEncryptor fileEncryptor(crypto, vaultService);
    UploadService uploadService(api, fileEncryptor);
    QueueService queueService(queueRepo, syncRepo, uploadService);
    SyncService syncService(syncRepo, queueRepo, crypto);

    App app(userRepo, syncRepo, queueRepo, queueService, syncService,
            authService, uploadService, vaultService);
    return app.run(argc, argv);
  } catch (const std::exception &ex) {
    spdlog::error("Fatal error: {}", ex.what());
    return 1;
  }
}
