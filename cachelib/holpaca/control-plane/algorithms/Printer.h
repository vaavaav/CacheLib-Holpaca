#pragma once
#include <cachelib/holpaca/control-plane/algorithms/ControlAlgorithm.h>

namespace facebook {
namespace cachelib {
namespace holpaca {
class Printer : public ControlAlgorithm {
  void loop(std::unordered_map<std::string, CacheStatus>& cacheStatus)
      override final {
    for (const auto& [address, status] : cacheStatus) {
      std::cout << "Cache '" << address << "' (" << status.m_maxSize << " / "
                << status.m_usedSize << ")\n";
      for (const auto& [poolId, poolStatus] : status.m_pools) {
        std::cout << "  |-- Pool '" << poolId << "'\n";
        std::cout << "       |-- Max Size: " << poolStatus.m_maxSize << "\n";
        std::cout << "       |-- Used Size: " << poolStatus.m_usedSize << "\n";
        std::cout << "       |-- Disk IOPS: " << poolStatus.m_diskIOPS << "\n";
        std::cout << "       |-- Lookups: " << poolStatus.m_lookups << "\n";
        std::cout << "       |-- Misses: " << poolStatus.m_misses << "\n";
        std::cout << "       |-- Evictions: " << poolStatus.m_evictions << "\n";
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
