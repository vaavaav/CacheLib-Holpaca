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
  int64_t totalSize = 0;
  std::vector<ProxyManager::CacheResize> cacheResizes;

  auto caches = kProxyManager->getStatus();

  for (const auto& [cacheId, cacheStatus] : caches) {
    for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
      sum += poolStatus.m_proportion * cacheStatus.m_proportion;
    }
    totalSize += cacheStatus.m_maxSize;
  }

  for (const auto& [cacheId, cacheStatus] : caches) {
    std::vector<ProxyManager::PoolResize> poolResizes;
    for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
      poolResizes.emplace_back(ProxyManager::PoolResize{
          .m_kId = poolId,
          .m_kSize = static_cast<uint64_t>(totalSize * poolStatus.m_proportion *
                                           cacheStatus.m_proportion / sum),
      });
    }
    cacheResizes.emplace_back(ProxyManager::CacheResize{
        .m_kName = cacheId, .m_kPoolResizes = poolResizes});
  }
  kProxyManager->resize(cacheResizes);
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
