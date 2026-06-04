#include "api/ArkiveApi.hpp"
#include "api/ArkiveHttpClient.hpp"
#include "app/App.hpp"
#include "db/Sqlite.hpp"
#include "helpers/Helpers.hpp"
#include "helpers/LocalPathProtector.hpp"
#include "platform/AppDataPaths.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UploadResumeRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/AuthService.hpp"
#include "service/QueueService.hpp"
#include "service/SyncService.hpp"
#include "service/UploadJobRunner.hpp"
#include "service/UploadService.hpp"
#include "service/VaultService.hpp"
#include "fs/FileEncryptor.hpp"
#include <exception>
#include <spdlog/spdlog.h>
#include <string>

int main(int argc, char *argv[]) {
  try {
    Database db;
    // Repos
    UserRepo userRepo(db.getDb());
    RustCrypto crypto;
    VaultService vaultService(userRepo, crypto);
    LocalPathProtector localPathProtector(crypto, vaultService);
    SyncRepo syncRepo(db.getDb(), localPathProtector);
    QueueRepo queueRepo(db.getDb());
    UploadResumeRepo uploadResumeRepo(db.getDb());
    // End Repos

    const std::string baseUrl = requireBaseUrl(userRepo);
    ArkiveHttpClient client(baseUrl, cookieJarPath().string());
    ArkiveApi api(client);
    AuthService authService(userRepo, api);
    FileEncryptor fileEncryptor(crypto, vaultService);
    UploadService uploadService(api, fileEncryptor, uploadResumeRepo);
    UploadJobRunner uploadJobRunner(syncRepo, uploadService, crypto);
    QueueService queueService(queueRepo, syncRepo, uploadJobRunner, api);
    SyncService syncService(syncRepo, queueRepo, crypto);

    App app(userRepo, syncRepo, queueRepo, queueService, syncService,
            authService, uploadService, vaultService);
    return app.run(argc, argv);
  } catch (const std::exception &ex) {
    spdlog::error("Fatal error: {}", ex.what());
    return 1;
  }
}
