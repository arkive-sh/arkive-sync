#pragma once

class UserRepo;
class AuthService;
class VaultService;
class SyncService;
class RootScanner;
class ScanRepo;

class App {
public:
  App(UserRepo &userRepo, AuthService *authService, VaultService &vaultService,
      SyncService &syncService, RootScanner &rootScanner, ScanRepo &scanRepo);
  ~App();

  int run(int argc, char *argv[]);

private:
  UserRepo &userRepo_;
  AuthService *authService_;
  VaultService &vaultService_;
  SyncService &syncService_;
  RootScanner &rootScanner_;
  ScanRepo &scanRepo_;
};
