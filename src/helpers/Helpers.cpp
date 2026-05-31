#include "./Helpers.hpp"

#include <stdexcept>
#include <termios.h>
#include <unistd.h>

void TerminalEchoRestore::operator()(TerminalState *state) const noexcept {
  if (state != nullptr) {
    tcsetattr(STDIN_FILENO, TCSANOW, &state->original);
    delete state;
  }
}

TerminalEchoGuard makeTerminalEchoGuard() {
  auto *state = new TerminalState{};
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

  return TerminalEchoGuard(state);
}
