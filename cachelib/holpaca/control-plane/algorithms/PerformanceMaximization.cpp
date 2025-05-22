#include <cachelib/holpaca/control-plane/algorithms/PerformanceMaximization.h>

#include <numeric>

template <typename Map>
auto getNth(Map& m, size_t n) -> typename Map::value_type& {
  if (n >= m.size()) {
    throw std::out_of_range("Index out of range");
  }
  auto it = m.begin();
  std::advance(it, n);
  return *it;
}

namespace facebook {
namespace cachelib {
namespace holpaca {

PerformanceMaximization::PerformanceMaximization(
    ProxyManager* const kProxyManager,
    std::chrono::milliseconds const kPeriodicity,
    MetricType const kMetricType,
    double const kDelta,
    const std::unordered_map<std::string, double>& kQoS)
    : ControlAlgorithm(kProxyManager, kPeriodicity),
      m_kDelta(kDelta),
      m_kMetricType(kMetricType),
      m_kQoS(kQoS) {} // TODO: use QoS to change lower bounds

void PerformanceMaximization::loop(ProxyManager* const kProxyManager) {
  auto allCacheStatus = kProxyManager->getStatus();

  Context context;

  // Build the new context
  for (const auto& [cacheId, cacheStatus] : allCacheStatus) {
    uint64_t usedSize = 0;
    std::unordered_map<PoolId, PoolConfig> activePools;

    for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
      if (poolStatus.m_isActive) {
        usedSize += poolStatus.m_usedSize;
      }
      if (poolStatus.m_isActive && poolStatus.m_MRC.size() >= m_kMRCMinLength) {
        std::vector<double> cacheSizes;
        std::vector<double> metrics;

        for (const auto& [size, missRatio] : poolStatus.m_MRC) {
          cacheSizes.push_back(size);
          double metric = missRatio;
          if (m_kMetricType == MetricType::kThroughput &&
              poolStatus.m_diskIOPS > 0) {
            metric = missRatio / static_cast<double>(poolStatus.m_diskIOPS);
          }
          metrics.push_back(metric);
        }

        activePools.emplace(
            poolId,
            PoolConfig{
                .m_optimalSize = poolStatus.m_maxSize,
                .m_kCurrentSize = poolStatus.m_maxSize,
                .m_utilityCurve =
                    tk::spline(cacheSizes, metrics, tk::spline::cspline, true),
                .m_externalSize = poolStatus.m_externalSize,
                .m_lowerBound = static_cast<uint64_t>((1.0 - m_kDelta) *
                                                      poolStatus.m_maxSize),
                .m_upperBound = static_cast<uint64_t>((1.0 + m_kDelta) *
                                                      poolStatus.m_maxSize),
            });
      }
    }

    if (!activePools.empty()) {
      context.m_cacheConfigs[cacheId] = CacheConfig({
          .m_maxSize = cacheStatus.m_maxSize,
          .m_usedSize = usedSize,
          .m_poolConfigs = std::move(activePools),
      });
    }
  }

  std::unordered_set<std::string> resetCaches;
  // Check for removed caches: remove external size of other pools
  for (auto& [cacheId, cacheConfig] : m_previousIteration) {
    if (allCacheStatus.find(cacheId) == allCacheStatus.end()) {
      for (auto& [poolId, poolConfig] : cacheConfig.m_poolConfigs) {
        auto excess = poolConfig.m_externalSize[cacheId];
        poolConfig.m_lowerBound -= excess;
        poolConfig.m_optimalSize -= excess;
        poolConfig.m_upperBound -= excess;
        poolConfig.m_externalSize.erase(cacheId);
      }
    }

    // Check for inactive pools
    // 1. reset the partitioning of internal pools (later)
    // 2. remove external size of the removed pools
    for (auto& [poolId, poolConfig] : cacheConfig.m_poolConfigs) {
      if (!allCacheStatus[cacheId].m_pools[poolId].m_isActive) {
        poolConfig.m_lowerBound = 0;
        poolConfig.m_optimalSize = 0;
        poolConfig.m_upperBound = 0;
        poolConfig.m_externalSize.clear();
        resetCaches.emplace(cacheId);
      }
    }
  }

  // Check for added pools
  for (const auto& [cacheId, cacheConfig] : allCacheStatus) {
    if (std::any_of(cacheConfig.m_pools.begin(), cacheConfig.m_pools.end(),
                    [&](const auto& poolStatus) {
                      auto it = m_previousIteration.find(cacheId);
                      return it != m_previousIteration.end() &&
                             it->second.m_poolConfigs.find(poolStatus.first) ==
                                 it->second.m_poolConfigs.end();
                    })) {
      resetCaches.emplace(cacheId);
    }
  }

  // Reset partitioning
  for (const auto& cacheId : resetCaches) {
    auto size = context.m_cacheConfigs[cacheId].m_maxSize /
                context.m_cacheConfigs[cacheId].m_poolConfigs.size();
    for (auto& [poolId, poolConfig] :
         context.m_cacheConfigs[cacheId].m_poolConfigs) {
      if (allCacheStatus[cacheId].m_pools[poolId].m_isActive) {
        poolConfig.m_lowerBound = size * (1 - m_kDelta);
        poolConfig.m_optimalSize = size;
        poolConfig.m_upperBound = size * (1 + m_kDelta);
        for (const auto& [externalCache, externalSize] :
             poolConfig.m_externalSize) {
          if (resetCaches.find(externalCache) != resetCaches.end()) {
            poolConfig.m_externalSize[externalCache] = 0;
          }
        }
      }
    }
  }

  // compute
  context.run(2000, 250, 90, 0.1, 1.003);

  // enforce
  std::vector<ProxyManager::CacheResize> cacheResizes;
  for (const auto& [cacheId, cacheConfig] : context.m_cacheConfigs) {
    ProxyManager::CacheResize cacheResize;
    cacheResize.m_kName = cacheId;
    for (const auto& [poolId, poolConfig] : cacheConfig.m_poolConfigs) {
      cacheResize.m_kPoolResizes.emplace_back(ProxyManager::PoolResize{
          .m_kId = poolId,
          .m_kDeltaSize =
              static_cast<int64_t>(poolConfig.m_optimalSize) -
              static_cast<int64_t>(
                  cacheConfig.m_poolConfigs.at(poolId).m_kCurrentSize),
          .m_kExternalDeltaSize = [allCacheStatus, poolId, poolConfig] {
            std::unordered_map<std::string, int64_t> externalDeltaSize;
            for (const auto& [externalCache, size] :
                 poolConfig.m_externalSize) {
              externalDeltaSize[externalCache] =
                  poolConfig.m_externalSize.at(externalCache) -
                  allCacheStatus.at(externalCache)
                      .m_pools.at(poolId)
                      .m_externalSize.at(externalCache);
            }
            return externalDeltaSize;
          }()});
    }
    cacheResizes.emplace_back(cacheResize);
  }

  kProxyManager->resize(cacheResizes);

  m_previousIteration = std::move(context.m_cacheConfigs);
}

bool PerformanceMaximization::Context::skip() const {
  return std::accumulate(m_cacheConfigs.begin(), m_cacheConfigs.end(), 0,
                         [](int acc, const auto& ccit) {
                           return acc + ccit.second.m_poolConfigs.size();
                         }) <= 1;
}

void PerformanceMaximization::Context::step() {
  // 1: giver, 2: receiver
  // First get cache index (can be the same)
  int const cacheIdx1 = randomUniformInt(m_cacheConfigs.size());
  int const cacheIdx2 = randomUniformInt(m_cacheConfigs.size());
  auto& [cache1Id, cache1] = getNth(m_cacheConfigs, cacheIdx1);
  auto& [cache2Id, cache2] = getNth(m_cacheConfigs, cacheIdx2);

  // Then get pool index
  int const poolIdx1 = randomUniformInt(cache1.m_poolConfigs.size());
  int const poolIdx2 =
      cacheIdx1 != cacheIdx2
          ? randomUniformInt(cache2.m_poolConfigs.size())
          : (poolIdx1 + 1 + randomUniformInt(cache2.m_poolConfigs.size() - 1)) %
                cache2.m_poolConfigs.size();

  // Get the two caches and pools
  auto& [pool1Id, pool1] = getNth(cache1.m_poolConfigs, poolIdx1);
  auto& [pool2Id, pool2] = getNth(cache2.m_poolConfigs, poolIdx2);

  // Trade a random amount of space (limited by the lower and upper bounds)
  int const kMaxDelta =
      cacheIdx1 != cacheIdx2
          ? std::min({pool1.m_optimalSize - pool1.m_lowerBound,
                      pool2.m_upperBound - pool2.m_optimalSize})
          : std::min({pool1.m_upperBound - pool1.m_optimalSize,
                      pool2.m_optimalSize - pool2.m_lowerBound,
                      cache2.m_maxSize - cache2.m_usedSize, cache1.m_usedSize});

  if (kMaxDelta > 0) {
    int const kDelta = randomUniformInt(kMaxDelta);
    // Update the optimal size of the pools
    pool1.m_optimalSize -= kDelta;
    pool2.m_optimalSize += kDelta;
    if (cacheIdx1 != cacheIdx2) {
      cache1.m_usedSize -= kDelta;
      cache2.m_usedSize += kDelta;
      pool2.m_externalSize[cache1Id] += kDelta;
    }
  }
}

double PerformanceMaximization::Context::energy() const {
  // cacheconfigs is a map and poolconfigs too
  return std::accumulate(
      m_cacheConfigs.begin(), m_cacheConfigs.end(), 0.0,
      [](double acc, const auto& ccit) {
        return acc + std::accumulate(ccit.second.m_poolConfigs.begin(),
                                     ccit.second.m_poolConfigs.end(), 0.0,
                                     [](double acc, const auto& pcit) {
                                       return acc + pcit.second.getMetric();
                                     });
      });
}

double PerformanceMaximization::Context::distance(
    Optimizable const* other) const {
  return fabs(energy() - other->energy());
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
