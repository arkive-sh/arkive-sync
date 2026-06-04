#pragma once

class UserRepo;
class SyncRepo;
class QueueRepo;
class AuthService;
class QueueService;
class SyncScheduler;
class SyncService;
class UploadService;
class VaultService;

class App {
public:
  App(UserRepo &userRepo, SyncRepo &syncRepo, QueueRepo &queueRepo,
      QueueService &queueService, SyncScheduler &syncScheduler,
      SyncService &syncService,
      AuthService &authService, UploadService &uploadService,
      VaultService &vaultService);
  ~App();

  int run(int argc, char *argv[]);

private:
  UserRepo &userRepo_;
  SyncRepo &syncRepo_;
  QueueRepo &queueRepo_;
  QueueService &queueService_;
  SyncScheduler &syncScheduler_;
  SyncService &syncService_;
  AuthService &authService_;
  UploadService &uploadService_;
  VaultService &vaultService_;
};
