#include <cachelib/holpaca/control-plane/algorithms/PerformanceMaximization.h>

#include <istream>
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

struct CacheChanges {
  bool reset{false};
  uint64_t m_defaultPoolSize;
  std::unordered_set<PoolId> m_removedPools;
  std::unordered_set<PoolId> m_activeNotValidPools;
  std::unordered_set<PoolId> m_validPools;
};

PerformanceMaximization::PerformanceMaximization(
    ProxyManager* const kProxyManager,
    std::chrono::milliseconds const kPeriodicity,
    MetricType const kMetricType,
    double const kDelta,
    uint64_t const kMaxInternalCacheSize,
    bool printLatencies,
    bool applyAdjustment)
    : ControlAlgorithm(kProxyManager, kPeriodicity),
      m_kDelta(kDelta),
      m_kMetricType(kMetricType),
      m_kMaxInternalCacheSize(kMaxInternalCacheSize),
      m_kPrintLatencies(printLatencies),
      m_kApplyAdjustment(applyAdjustment) {}

void PerformanceMaximization::loop(ProxyManager* const kProxyManager) {
  std::chrono::high_resolution_clock::time_point start;
  std::chrono::duration<double, std::milli> collect, compute, enforce;
  double aggregatedMetrics = 0.0;

  // COLLECT
  if (m_kPrintLatencies) {
    auto start = std::chrono::high_resolution_clock::now();
  }

  auto allCacheStatus = kProxyManager->getStatus();

  std::unordered_set<std::string> removedCaches;
  for (const auto& [cacheId, _] : m_previouslyActive) {
    if (allCacheStatus.find(cacheId) == allCacheStatus.end()) {
      removedCaches.insert(cacheId);
    }
  }

  std::unordered_map<std::string, CacheChanges> cacheChanges;

  for (const auto& [cacheId, cacheStatus] : allCacheStatus) {
    std::unordered_set<PoolId> activeNotValidPools;
    std::unordered_set<PoolId> validPools;
    std::unordered_set<PoolId> removedPools;
    bool reset = false;
    bool cachePreviouslyRegistered =
        m_previouslyActive.find(cacheId) != m_previouslyActive.end();
    uint32_t activePoolCount = 0;

    for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
      if (poolStatus.m_isActive) {
        if (poolStatus.m_MRC.size() >= m_kMRCMinLength) {
          validPools.insert(poolId);
        } else {
          activeNotValidPools.insert(poolId);
        }
        if (!cachePreviouslyRegistered ||
            !m_previouslyActive[cacheId].count(poolId)) {
          reset = true;
        }
        activePoolCount++;
      } else {
        if (cachePreviouslyRegistered &&
            m_previouslyActive[cacheId].count(poolId)) {
          reset = true;
          removedPools.insert(poolId);
        }
      }
    }

    cacheChanges[cacheId] = CacheChanges{
        .reset = reset,
        .m_defaultPoolSize = static_cast<uint64_t>(
            std::min(m_kMaxInternalCacheSize, cacheStatus.m_maxSize) /
            static_cast<double>(activePoolCount)),
        .m_removedPools = std::move(removedPools),
        .m_activeNotValidPools = std::move(activeNotValidPools),
        .m_validPools = std::move(validPools),
    };
  }

  if (m_kPrintLatencies) {
    collect = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
  }

  // COMPUTE

  if (m_kPrintLatencies) {
    start = std::chrono::high_resolution_clock::now();
  }

  Context context;
  std::unordered_map<std::string, ProxyManager::CacheResize> cacheResizes;

  for (const auto& [cacheId, cacheStatus] : allCacheStatus) {
    cacheResizes[cacheId] = ProxyManager::CacheResize{
        .m_kName = cacheId,
        .m_kPoolResizes = {},
    };

    // Valid pools for optimization
    std::unordered_map<PoolId, PoolConfig> validPools;
    for (const auto& poolId : cacheChanges[cacheId].m_validPools) {
      auto poolStatus = cacheStatus.m_pools.at(poolId);
      std::vector<double> cacheSizes;
      std::vector<double> metrics;

      if (m_kMetricType == MetricType::kHitRatio) {
        for (const auto& [size, missRatio] : poolStatus.m_MRC) {
          cacheSizes.push_back(size);
          metrics.push_back(missRatio);
        }
        if (m_kApplyAdjustment) {
          auto spline = tk::spline(cacheSizes, metrics,
                                   tk::spline::cspline_hermite, true);
          double adjustment =
              poolStatus.m_missRatio - spline(poolStatus.m_maxSize);

          for (auto& metric : metrics) {
            metric += adjustment;
          }
        }
      } else { // kThroughput
        for (const auto& [size, missRatio] : poolStatus.m_MRC) {
          cacheSizes.push_back(size);
          metrics.push_back(missRatio ? -poolStatus.m_diskIOPS / missRatio
                                      : -DBL_MAX);
        }
        if (m_kApplyAdjustment) {
          auto spline = tk::spline(cacheSizes, metrics,
                                   tk::spline::cspline_hermite, true);
          // ajust the spline with the difference between real and expected
          auto const kCurrentMetric =
              poolStatus.m_missRatio
                  ? -poolStatus.m_diskIOPS / poolStatus.m_missRatio
                  : -DBL_MAX;
          double adjustment = kCurrentMetric - spline(poolStatus.m_maxSize);

          for (auto& metric : metrics) {
            metric += adjustment;
          }
        }
      }

      // If the cache is reset, redistribute the internal cache size among the
      // active pools
      auto size = cacheChanges[cacheId].reset
                      ? cacheChanges[cacheId].m_defaultPoolSize
                      : poolStatus.m_maxSize;

      std::unordered_map<std::string, int64_t> externalSize;
      for (const auto& [externalCache, extSize] : poolStatus.m_externalSize) {
        if (removedCaches.count(externalCache) ||
            cacheChanges[externalCache].reset) {
          if (!cacheChanges[cacheId].reset) {
            size -= extSize;
          }
          externalSize[externalCache] = 0; // Reset the size for removed caches
        } else {
          externalSize[externalCache] = extSize;
        }
      }

      auto spline =
          tk::spline(cacheSizes, metrics, tk::spline::cspline_hermite, true);
      aggregatedMetrics += spline(size);

      uint64_t lowerBound =
          poolStatus.m_qosLevel > 0.0 &&
                  (spline(size) > (m_kMetricType == MetricType::kHitRatio
                                       ? 1 - poolStatus.m_qosLevel
                                       : 1 / poolStatus.m_qosLevel))
              ? size
              : static_cast<uint64_t>((1.0 - m_kDelta) * size);

      validPools.emplace(
          poolId,
          PoolConfig{
              .m_optimalSize = size,
              .m_kCurrentSize = poolStatus.m_maxSize,
              .m_utilityCurve = std::move(spline),
              .m_externalSize = std::move(externalSize),
              .m_lowerBound = static_cast<uint64_t>(lowerBound),
              .m_upperBound = static_cast<uint64_t>((1.0 + m_kDelta) * size),
          });
    }

    if (cacheChanges[cacheId].reset) {
      // Active pools but not valid for optimization
      for (const auto& poolId : cacheChanges[cacheId].m_activeNotValidPools) {
        auto poolStatus = cacheStatus.m_pools.at(poolId);
        std::unordered_map<std::string, int64_t> externalDeltaSizes = {};
        for (const auto& [externalCache, extSize] : poolStatus.m_externalSize) {
          if (removedCaches.count(externalCache) ||
              cacheChanges[externalCache].reset) {
            externalDeltaSizes[externalCache] = -extSize;
          }
        }

        cacheResizes[cacheId].m_kPoolResizes.emplace_back(
            ProxyManager::PoolResize{
                .m_kId = poolId,
                .m_kDeltaSize = static_cast<int64_t>(
                                    cacheChanges[cacheId].m_defaultPoolSize) -
                                static_cast<int64_t>(poolStatus.m_maxSize),
                .m_kExternalDeltaSizes = std::move(externalDeltaSizes),
            });
      }

      // Removed Pools
      for (const auto& poolId : cacheChanges[cacheId].m_removedPools) {
        auto poolStatus = cacheStatus.m_pools.at(poolId);
        cacheResizes[cacheId].m_kPoolResizes.emplace_back(
            ProxyManager::PoolResize{
                .m_kId = poolId,
                .m_kDeltaSize = -static_cast<int64_t>(poolStatus.m_maxSize),
                .m_kExternalDeltaSizes = {},
            });
      }
    }

    if (!validPools.empty()) {
      context.m_cacheConfigs[cacheId] = CacheConfig({
          .m_poolConfigs = std::move(validPools),
      });
    }
  }

  double const kAvgMetrics =
      context.m_cacheConfigs.empty()
          ? 0.0
          : aggregatedMetrics / context.m_cacheConfigs.size();

  context.run(2000, 250, kAvgMetrics, 90, 0.1, 1.003);

  if (m_kPrintLatencies) {
    compute = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
  }

  // ENFORCE

  if (m_kPrintLatencies) {
    start = std::chrono::high_resolution_clock::now();
  }

  for (const auto& [cacheId, cacheConfig] : context.m_cacheConfigs) {
    for (const auto& [poolId, poolConfig] : cacheConfig.m_poolConfigs) {
      cacheResizes[cacheId].m_kPoolResizes.emplace_back(
          ProxyManager::PoolResize{
              .m_kId = poolId,
              .m_kDeltaSize =
                  static_cast<int64_t>(poolConfig.m_optimalSize) -
                  static_cast<int64_t>(
                      cacheConfig.m_poolConfigs.at(poolId).m_kCurrentSize),
              .m_kExternalDeltaSizes =
                  [poolConfig, &poolId, &allCacheStatus, &cacheId]() {
                    std::unordered_map<std::string, int64_t> externalDeltaSizes;
                    for (const auto& [externalCache, extSize] :
                         poolConfig.m_externalSize) {
                      externalDeltaSizes[externalCache] =
                          extSize - allCacheStatus[cacheId]
                                        .m_pools[poolId]
                                        .m_externalSize[externalCache];
                    }
                    return externalDeltaSizes;
                  }(),
          });
    }
  }

  std::vector<ProxyManager::CacheResize> cacheResizesFinal;
  for (const auto& [cacheId, cacheResize] : cacheResizes) {
    cacheResizesFinal.emplace_back(cacheResize);
  }

  kProxyManager->resize(cacheResizesFinal);

  if (m_kPrintLatencies) {
    enforce = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
    std::cout << collect.count() << "," << compute.count() << ","
              << enforce.count() << std::endl;
  }

  m_previouslyActive.clear();
  for (const auto& [cacheId, cacheChange] : cacheChanges) {
    m_previouslyActive[cacheId] = std::unordered_set<PoolId>{};
    for (const auto& poolId : cacheChange.m_validPools) {
      m_previouslyActive[cacheId].insert(poolId);
    }
    for (const auto& poolId : cacheChange.m_activeNotValidPools) {
      m_previouslyActive[cacheId].insert(poolId);
    }
  }
} // namespace holpaca

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
  int const kMaxDelta = std::min({pool1.m_optimalSize - pool1.m_lowerBound,
                                  pool2.m_upperBound - pool2.m_optimalSize});

  if (kMaxDelta > 0) {
    int const kDelta = randomUniformInt(kMaxDelta);
    // Update the optimal size of the pools
    pool1.m_optimalSize -= kDelta;
    pool2.m_optimalSize += kDelta;
    if (cacheIdx1 != cacheIdx2) {
      pool1.m_externalSize[cache2Id] -= kDelta;
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
