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

PerformanceMaximization::PerformanceMaximization(
    ProxyManager* const kProxyManager,
    std::chrono::milliseconds const kPeriodicity,
    MetricType const kMetricType,
    double const kDelta,
    bool printLatencies)
    : ControlAlgorithm(kProxyManager, kPeriodicity),
      m_kDelta(kDelta),
      m_kMetricType(kMetricType),
      m_kPrintLatencies(printLatencies) {}

void PerformanceMaximization::loop(ProxyManager* const kProxyManager) {
  std::chrono::duration<double, std::milli> collect, compute, enforce;

  std::unordered_map<std::string, ProxyManager::CacheStatus> allCacheStatus;
  std::vector<ProxyManager::CacheResize> cacheResizes;

  // COLLECT
  {
    std::chrono::high_resolution_clock::time_point start =
        std::chrono::high_resolution_clock::now();
    allCacheStatus = kProxyManager->getStatus();
    collect = std::chrono::high_resolution_clock::now() - start;
  }

  // COMPUTE
  {
    std::chrono::high_resolution_clock::time_point start =
        std::chrono::high_resolution_clock::now();

    uint64_t totalSize = 0;
    int pools = 0;
    int newPools = 0;
    uint64_t usedSpace = 0;
    double adjustmentFactor = 1.0;
    std::unordered_map<std::string, std::unordered_map<PoolId, uint64_t>>
        newPoolSizePerCache;

    for (const auto& [cacheId, cacheStatus] : allCacheStatus) {
      newPoolSizePerCache[cacheId] = {};
      totalSize += cacheStatus.m_maxSize;
      for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
        if (poolStatus.m_MRC.size() < m_kMRCMinLength) {
          newPools++;
        }
        pools++;
        // update pool metrics history
        auto& poolAvgMetrics = m_poolAvgMetricsHistory[cacheId][poolId];
        poolAvgMetrics.m_diskIOPS =
            (poolAvgMetrics.m_diskIOPS * m_kMovingAverageParam +
             poolStatus.m_diskIOPS * (1 - m_kMovingAverageParam));
        poolAvgMetrics.m_missRatio =
            (poolAvgMetrics.m_missRatio * m_kMovingAverageParam +
             poolStatus.m_missRatio * (1 - m_kMovingAverageParam));
      }
    }

    for (const auto& [cacheId, cacheStatus] : allCacheStatus) {
      for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
        if (poolStatus.m_MRC.size() >= m_kMRCMinLength) {
          usedSpace += poolStatus.m_maxSize;
        } else {
          newPoolSizePerCache[cacheId][poolId] =
              static_cast<uint64_t>(static_cast<double>(totalSize) / pools);
        }
      }
    }

    uint64_t const kUsedSpaceWithNewPools = static_cast<uint64_t>(
        newPools * static_cast<double>(totalSize) / pools);

    double const kAdjustmentFactor =
        pools == 0 ? 1.0 : 1.0 - static_cast<double>(newPools) / pools;

    double const kAdjustmentDelta =
        static_cast<double>(totalSize - kUsedSpaceWithNewPools -
                            kAdjustmentFactor * usedSpace) /
        (pools - newPools);

    for (const auto& [cacheId, cacheStatus] : allCacheStatus) {
      for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
        if (poolStatus.m_MRC.size() >= m_kMRCMinLength) {
          newPoolSizePerCache[cacheId][poolId] = std::max(
              0.0, poolStatus.m_maxSize * kAdjustmentFactor + kAdjustmentDelta);
        }
      }
    }

    // Prepare context
    // Context only considers valid pools (with MRC of sufficient length)
    Context context;
    double aggregatedMetrics = 0.0;

    for (const auto& [cacheId, cacheStatus] : allCacheStatus) {
      std::unordered_map<PoolId, PoolConfig> poolConfigs;
      for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
        if (poolStatus.m_MRC.size() >= m_kMRCMinLength) {
          std::vector<double> cacheSizes;
          std::vector<double> metrics;

          if (m_kMetricType == MetricType::kHitRatio) {
            for (const auto& [size, mr] : poolStatus.m_MRC) {
              cacheSizes.push_back(size);
              metrics.push_back(mr);
            }
            auto spline = tk::spline(cacheSizes, metrics,
                                     tk::spline::cspline_hermite, true);
            double const kAdjustment =
                m_poolAvgMetricsHistory[cacheId][poolId].m_missRatio -
                spline(poolStatus.m_maxSize);

            for (auto& metric : metrics) {
              metric += kAdjustment;
            }
          } else if (m_kMetricType == MetricType::kThroughput) {
            for (const auto& [size, mr] : poolStatus.m_MRC) {
              if (mr > 0.0) {
                cacheSizes.push_back(size);
                metrics.push_back(-poolStatus.m_diskIOPS / mr);
              }
            }
            auto spline = tk::spline(cacheSizes, metrics,
                                     tk::spline::cspline_hermite, true);
            auto const kAvgMissRatio =
                m_poolAvgMetricsHistory[cacheId][poolId].m_missRatio;
            auto const kAvgDiskIOPS =
                m_poolAvgMetricsHistory[cacheId][poolId].m_diskIOPS;

            double const kAdjustment =
                (kAvgMissRatio ? -kAvgDiskIOPS / kAvgMissRatio : -DBL_MAX) -
                spline(poolStatus.m_maxSize);

            for (auto& metric : metrics) {
              metric += kAdjustment;
            }
          }

          auto const kSize = newPoolSizePerCache[cacheId][poolId];

          auto spline = tk::spline(cacheSizes, metrics,
                                   tk::spline::cspline_hermite, true);
          aggregatedMetrics += spline(kSize);

          auto poolConfig = PoolConfig{
              .m_optimalSize = kSize,
              .m_lowerBound =
                  poolStatus.m_qosLevel > 0.0 &&
                          (spline(kSize) >
                           (m_kMetricType == MetricType::kHitRatio
                                ? 1 - poolStatus.m_qosLevel
                                : poolStatus.m_qosLevel))
                      ? kSize
                      : static_cast<uint64_t>((1.0 - m_kDelta) * kSize),
              .m_upperBound = static_cast<uint64_t>(kSize * (1 + m_kDelta)),
              .m_utilityCurve = std::move(spline),
          };
          poolConfigs.emplace(poolId, poolConfig);
        }
      }
      if (!poolConfigs.empty()) {
        context.m_cacheConfigs.emplace(cacheId, CacheConfig{poolConfigs});
      }
    }

    double const kAvgMetrics =
        context.m_cacheConfigs.empty()
            ? 0.0
            : aggregatedMetrics / context.m_cacheConfigs.size();

    context.run(2000, 250, kAvgMetrics, 90, 0.1, 1.003);

    for (auto const& [cacheId, cacheConfig] : context.m_cacheConfigs) {
      for (auto const& [poolId, poolConfig] : cacheConfig.m_poolConfigs) {
        newPoolSizePerCache[cacheId][poolId] = poolConfig.m_optimalSize;
      }
    }

    std::stringstream ss;
    ss << "TotalSize: " << totalSize << std::endl;
    for (auto const& [cacheId, cacheStatus] : allCacheStatus) {
      ss << "C[" << cacheId << "]: " << cacheStatus.m_maxSize << std::endl;
      for (auto const& [poolId, poolStatus] : cacheStatus.m_pools) {
        if (poolStatus.m_MRC.size() < m_kMRCMinLength) {
          ss << " !NEW! ";
        }
        ss << "C[" << cacheId << "] P[" << static_cast<uint32_t>(poolId)
           << "]: " << newPoolSizePerCache[cacheId][poolId] << " -> "
           << newPoolSizePerCache[cacheId][poolId] << std::endl;
        ss << "\tMR: " << (poolStatus.m_missRatio) << std::endl;
        ss << "\tdisk IOPS: " << (poolStatus.m_diskIOPS) << std::endl;
        ss << "\t Avg MR: "
           << (m_poolAvgMetricsHistory[cacheId][poolId].m_missRatio)
           << std::endl;
        ss << "\t Avg disk IOPS: "
           << (m_poolAvgMetricsHistory[cacheId][poolId].m_diskIOPS)
           << std::endl;
      }
    }
    std::cout << ss.str();

    for (const auto& [cacheId, pools] : newPoolSizePerCache) {
      std::vector<ProxyManager::PoolResize> poolResizes;
      for (const auto& [poolId, size] : pools) {
        poolResizes.emplace_back(ProxyManager::PoolResize{
            .m_kId = poolId,
            .m_kSize = size,
        });
      }
      cacheResizes.emplace_back(ProxyManager::CacheResize{
          .m_kName = cacheId, .m_kPoolResizes = poolResizes});
    }

    compute = std::chrono::high_resolution_clock::now() - start;
  }

  {
    auto start = std::chrono::high_resolution_clock::now();

    kProxyManager->resize(cacheResizes);

    enforce = std::chrono::high_resolution_clock::now() - start;
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
