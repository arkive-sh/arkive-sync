#pragma once

#include "upload/UploadTypes.hpp"
#include <cstdint>

class UploadPlanner {
public:
  static UploadPlan createPlan(uint64_t plaintextSize);
};
