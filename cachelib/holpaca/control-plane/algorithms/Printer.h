#pragma once
#include <cachelib/holpaca/control-plane/algorithms/ControlAlgorithm.h>

namespace facebook {
namespace cachelib {
namespace holpaca {
class Printer : public ControlAlgorithm {
  void loop(CacheStatus&& cacheStatus) override final {
    std::cout << "Cache (" << cacheStatus.m_maxSize << ")\n";
    for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
      std::cout << "  |-- Pool '" << poolId << "'\n";
      std::cout << "       |-- Max Size: " << poolStatus.m_maxSize << "\n";
      std::cout << "       |-- Used Size: " << poolStatus.m_usedSize << "\n";
      std::cout << "       |-- Disk IOPS: " << poolStatus.m_diskIOPS << "\n";
      std::cout << "       |-- Lookups: " << poolStatus.m_lookups << "\n";
      std::cout << "       |-- Misses: " << poolStatus.m_misses << "\n";
      std::cout << "       |-- Evictions: " << poolStatus.m_evictions << "\n";
    }
  }

 public:
  Printer(std::shared_ptr<CacheProxy> const kCacheProxy,
          std::chrono::milliseconds const kPeriodicity)
      : ControlAlgorithm(kCacheProxy, kPeriodicity) {}
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
