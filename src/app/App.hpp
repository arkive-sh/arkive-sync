#pragma once

class UserRepo;
class AuthService;
class VaultService;

class App {
public:
  App(UserRepo &userRepo, AuthService &authService, VaultService &vaultService);
  ~App();

  int run(int argc, char *argv[]);

private:
  UserRepo &userRepo_;
  AuthService &authService_;
  VaultService &vaultService_;
};
