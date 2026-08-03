#include "./Helpers.hpp"

#include <chrono>
#include <format>
#include <iostream>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

void TerminalEchoRestore::operator()(TerminalState *state) const noexcept {
  if (state != nullptr) {
#if defined(_WIN32)
    if (state->input != INVALID_HANDLE_VALUE) {
      SetConsoleMode(state->input, state->originalMode);
    }
#else
    tcsetattr(STDIN_FILENO, TCSANOW, &state->original);
#endif
    delete state;
  }
}

TerminalEchoGuard makeTerminalEchoGuard() {
  auto *state = new TerminalState{};
#if defined(_WIN32)
  state->input = GetStdHandle(STD_INPUT_HANDLE);
  if (state->input == INVALID_HANDLE_VALUE ||
      !GetConsoleMode(state->input, &state->originalMode)) {
    delete state;
    throw std::runtime_error("GetConsoleMode failed");
  }

  DWORD hidden = state->originalMode & ~ENABLE_ECHO_INPUT;
  if (!SetConsoleMode(state->input, hidden)) {
    delete state;
    throw std::runtime_error("SetConsoleMode failed");
  }
#else
  if (tcgetattr(STDIN_FILENO, &state->original) != 0) {
    delete state;
    throw std::runtime_error("tcgetattr failed");
  }

  termios hidden = state->original;
  hidden.c_lflag &= ~ECHO;

  if (tcsetattr(STDIN_FILENO, TCSANOW, &hidden) != 0) {
    delete state;
    throw std::runtime_error("tcsetattr failed");
  }
#endif

  return TerminalEchoGuard(state);
}

std::string readPasswordFromTerminal(const std::string &prompt) {
  std::string password;
  std::cout << prompt << std::flush;
  {
    auto echoGuard = makeTerminalEchoGuard();
    std::getline(std::cin, password);
  } // extra scope so RAII can happen and terminal returns to normal on
    // exceptions
  std::cout << '\n';

  return password;
}

bool commandAllowsMissingBaseUrl(int argc, char *argv[]) {
  if (argc < 2) {
    return true;
  }

  const std::string command = argv[1];
  if (command == "status" && argc == 2) {
    return true;
  }

  if (command == "set-base-url" && argc == 3) {
    return true;
  }

  return false;
}

std::string requireBaseUrl(const UserRepo &userRepo) {
  const auto account = userRepo.getAccount();
  if (!account.has_value()) {
    throw std::runtime_error("Base URL account record is missing");
  }

  if (account->baseUrl.empty()) {
    throw std::runtime_error(
        "Base URL is not configured. Run: arkive-sync set-base-url <url>");
  }

  return account->baseUrl;
}

std::string getCurrentTimestamp() {
  auto now = std::chrono::system_clock::now();
  // Redirects to UTC and formats natively
  return std::format("{:%FT%TZ}", now);
}
