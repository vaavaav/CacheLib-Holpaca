#pragma once
#include <cachelib/holpaca/control-plane/algorithms/ControlAlgorithm.h>

namespace facebook {
namespace cachelib {
namespace holpaca {
class Printer : public ControlAlgorithm {
  void loop(ProxyManager* const kProxyManager) override final {
    auto cacheStatus = kProxyManager->getStatus();
    for (const auto& [peer, status] : cacheStatus) {
      std::cout << "Cache '" << peer << "'\n";
      std::cout << "  Max size: " << status.m_maxSize << "\n";
      for (const auto& [poolId, poolStatus] : status.m_pools) {
        std::cout << "  Pool '" << poolId << "'\n";
        std::cout << "    Max size: " << poolStatus.m_maxSize << "\n";
        std::cout << "    Used size: " << poolStatus.m_usedSize << "\n";
        std::cout << "    Disk IOPS: " << poolStatus.m_diskIOPS << "\n";
        std::cout << "    Evictions: " << poolStatus.m_evictions << "\n";
      }
    }
  }

 public:
  Printer(ProxyManager* const kProxyManager,
          std::chrono::milliseconds const kPeriodicity)
      : ControlAlgorithm(kProxyManager, kPeriodicity) {}
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
