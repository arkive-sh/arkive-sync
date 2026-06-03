#include "api/HttpError.hpp"
#include <nlohmann/json.hpp>
#include <utility>

namespace {

std::string extractApiErrorMessage(const std::string &responseBody) {
  if (responseBody.empty()) {
    return "";
  }

  try {
    const nlohmann::json payload = nlohmann::json::parse(responseBody);
    if (payload.contains("error")) {
      const auto &error = payload["error"];
      if (error.is_string()) {
        return error.get<std::string>();
      }
      if (error.is_object()) {
        if (error.contains("message") && error["message"].is_string()) {
          return error["message"].get<std::string>();
        }
        if (error.contains("code") && error["code"].is_string()) {
          return error["code"].get<std::string>();
        }
      }
    }
    if (payload.contains("message") && payload["message"].is_string()) {
      return payload["message"].get<std::string>();
    }
  } catch (...) {
  }

  return "";
}

} // namespace

HttpError::HttpError(int statusCode, std::string body)
    : std::runtime_error(buildMessage(statusCode, body)),
      statusCode(statusCode), body(std::move(body)),
      apiError_(extractApiErrorMessage(this->body)) {}

const std::string &HttpError::apiError() const noexcept { return apiError_; }

std::string HttpError::buildMessage(int statusCode, const std::string &body) {
  const std::string apiError = extractApiErrorMessage(body);
  if (!apiError.empty()) {
    return "HTTP " + std::to_string(statusCode) + ": error: " + apiError;
  }

  if (!body.empty()) {
    return "HTTP " + std::to_string(statusCode) + ": " + body;
  }

  return "HTTP " + std::to_string(statusCode);
}
