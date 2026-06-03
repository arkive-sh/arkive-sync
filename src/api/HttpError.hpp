#pragma once

#include <stdexcept>
#include <string>

class HttpError : public std::runtime_error {
public:
  HttpError(int statusCode, std::string body);

  int statusCode;
  std::string body;

  const std::string &apiError() const noexcept;

private:
  static std::string buildMessage(int statusCode, const std::string &body);

  std::string apiError_;
};
