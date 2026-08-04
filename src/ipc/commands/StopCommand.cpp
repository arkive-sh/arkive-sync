#include "ipc/commands/Commands.hpp"

namespace ipc::commands {

arkive::ipc::Response stop(std::function<void()> requestStop) {
  requestStop();
  arkive::ipc::Response response;
  response.set_ok(true);
  response.set_state("stopping");
  return response;
}

} // namespace ipc::commands
