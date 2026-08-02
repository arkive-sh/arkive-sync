#include "api/ArkiveApi.hpp"
#include "api/ArkiveHttpClient.hpp"
#include "app/App.hpp"
#include "db/Sqlite.hpp"
#include "helpers/Helpers.hpp"
#include "platform/AppDataPaths.hpp"
#include "repo/DirtyPathRepo.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/LocalEntryRepo.hpp"
#include "repo/ScanRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/AuthService.hpp"
#include "service/SyncService.hpp"
#include "service/VaultService.hpp"
#include "sync/RootScanner.hpp"
#include <exception>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>

int main(int argc, char *argv[]) {
  try {
    Database db;
    UserRepo userRepo(db.getDb());
    RustCrypto crypto;
    VaultService vaultService(userRepo, crypto);
    SyncRepo syncRepo(db.getDb());
    ScanRepo scanRepo(db.getDb());
    DirtyPathRepo dirtyPathRepo(db.getDb());
    EntryRepo entryRepo(db.getDb());
    LocalEntryRepo localEntryRepo(db.getDb());
    SyncService syncService(syncRepo, crypto);
    RootScanner rootScanner(db.getDb(), crypto, syncService, scanRepo, dirtyPathRepo,
                            entryRepo, localEntryRepo);

    std::unique_ptr<ArkiveHttpClient> client;
    std::unique_ptr<ArkiveApi> api;
    std::unique_ptr<AuthService> authService;

    const auto account = userRepo.getAccount();
    if (account.has_value() && !account->baseUrl.empty()) {
      client = std::make_unique<ArkiveHttpClient>(account->baseUrl,
                                                  cookieJarPath().string());
      api = std::make_unique<ArkiveApi>(*client);
      authService = std::make_unique<AuthService>(userRepo, *api);
    }

    App app(userRepo, authService.get(), vaultService, syncService,
            rootScanner, scanRepo);
    return app.run(argc, argv);
  } catch (const std::exception &ex) {
    spdlog::error("Fatal error: {}", ex.what());
    return 1;
  }
}
