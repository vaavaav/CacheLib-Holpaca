#include <cachelib/holpaca/control-plane/ProxyManager.h>
#include <cachelib/holpaca/control-plane/algorithms/Motivation.h>

namespace facebook {
namespace cachelib {
namespace holpaca {

Motivation::Motivation(ProxyManager* const kProxyManager,
                       std::chrono::milliseconds const kPeriodicity)
    : ControlAlgorithm(kProxyManager, kPeriodicity) {}

void Motivation::loop(ProxyManager* const kProxyManager) {
  double sum = 0.0d;
  uint64_t totalSize = 0;
  std::vector<ProxyManager::CacheResize> cacheResizes;

  for (const auto& [cacheId, cacheStatus] : kProxyManager->getStatus()) {
    for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
      if (poolStatus.m_isActive) {
        sum += poolStatus.m_proportion;
        m_originalSizes.insert(
            {poolId, poolStatus.m_maxSize}); // Store original sizes
        totalSize += m_originalSizes[poolId];
      }
    }
    ProxyManager::CacheResize cacheResize;
    std::vector<ProxyManager::PoolResize> poolResizes;
    for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
      if (poolStatus.m_isActive) {
        auto delta =
            static_cast<int64_t>(totalSize * poolStatus.m_proportion / sum) -
            static_cast<int64_t>(poolStatus.m_maxSize);
        if (delta != 0) {
          poolResizes.emplace_back(ProxyManager::PoolResize{
              .m_kId = poolId,
              .m_kDeltaSize = delta,
              .m_kExternalDeltaSizes = {},
          });
        }
      }
    }
    cacheResizes.emplace_back(ProxyManager::CacheResize{
        .m_kName = cacheId, .m_kPoolResizes = poolResizes});
  }
  kProxyManager->resize(cacheResizes);
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
