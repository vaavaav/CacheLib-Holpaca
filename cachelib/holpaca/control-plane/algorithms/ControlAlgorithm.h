#pragma once

#include <string>

#include "cachelib/holpaca/control-plane/ProxyManager.h"

namespace facebook {
namespace cachelib {
namespace holpaca {
class ControlAlgorithm {
 protected:
  virtual void run() = 0;

 public:
  ControlAlgorithm() {}
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
