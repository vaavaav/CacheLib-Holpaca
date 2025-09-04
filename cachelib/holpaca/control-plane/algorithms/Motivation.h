#pragma once
#include <cachelib/holpaca/control-plane/algorithms/ControlAlgorithm.h>

#include <unordered_set>

namespace facebook {
namespace cachelib {
namespace holpaca {

class Motivation : public ControlAlgorithm {
  void loop(ProxyManager* const kProxyManager) override final;

 public:
  Motivation(ProxyManager* const kProxyManager,
             std::chrono::milliseconds const kPeriodicity);
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
