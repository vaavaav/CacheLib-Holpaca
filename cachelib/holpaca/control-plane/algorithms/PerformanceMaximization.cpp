#include <cachelib/holpaca/control-plane/algorithms/PerformanceMaximization.h>

#include <numeric>

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
      m_kQoS(kQoS) {}

void PerformanceMaximization::loop(
    std::unordered_map<std::string, CacheStatus>& cacheStatus) {
  // collect
  Context context;
  for (const auto& [address, status] : cacheStatus) {
    std::vector<PoolConfig> poolConfigs;
    for (const auto& [poolId, poolStatus] : status.m_pools) {
      if (poolStatus.m_MRC.size() >= m_kMRCMinLength) {
        std::vector<double> cacheSizes;
        std::vector<double> metrics;
        for (const auto& [size, missRatio] : poolStatus.m_MRC) {
          cacheSizes.emplace_back(size);
          metrics.emplace_back(missRatio);
        }
        auto incorrectUtilityCurve =
            tk::spline(cacheSizes, metrics, tk::spline::cspline, true);
        double const correction_factor =
            (static_cast<double>(poolStatus.m_misses) / poolStatus.m_lookups) -
            incorrectUtilityCurve(poolStatus.m_usedSize);
        for (auto& missRatio : metrics) {
          missRatio += correction_factor;
          if (m_kMetricType == MetricType::kThroughput) {
            missRatio = (poolStatus.m_diskIOPS > 0) *
                        (missRatio / poolStatus.m_diskIOPS);
          }
        }
        poolConfigs.emplace_back(PoolConfig{
            .m_kId = poolId,
            .m_optimalSize = poolStatus.m_maxSize,
            .m_kCurrentSize = poolStatus.m_maxSize,
            .m_utilityCurve =
                tk::spline(cacheSizes, metrics, tk::spline::cspline, true),
            .m_kLowerBound =
                static_cast<uint64_t>((1 - m_kDelta) * poolStatus.m_maxSize),
            .m_kUpperBound =
                static_cast<uint64_t>((1 + m_kDelta) * poolStatus.m_maxSize),
        });
      }
    }
    if (!poolConfigs.empty()) {
      context.m_cacheConfigs.emplace_back(CacheConfig{
          .m_kName = address,
          .m_kCurrentSize = status.m_maxSize,
          .m_kMaxSize = status.m_usedSize,
          .m_optimalSize = status.m_maxSize,
          .m_poolConfigs = std::move(poolConfigs),
      });
    }
  }

  // compute
  context.run(2000, 250, 90, 0.1, 1.003);

  // enforce
  for (const auto& cacheConfig : context.m_cacheConfigs) {
    std::cout << "Cache " << cacheConfig.m_kName << ": "
              << cacheConfig.m_kCurrentSize << " -> "
              << cacheConfig.m_optimalSize << std::endl;
    std::unordered_map<int32_t, uint64_t> newSizes;
    for (const auto& poolConfig : cacheConfig.m_poolConfigs) {
      std::cout << "  |-- Pool " << poolConfig.m_kId << ": "
                << poolConfig.m_kCurrentSize << " -> "
                << poolConfig.m_optimalSize << std::endl;
      newSizes[poolConfig.m_kId] = poolConfig.m_optimalSize;
    }
    m_kProxyManager->getCache(cacheConfig.m_kName)->resize(newSizes);
  }
}

bool PerformanceMaximization::Context::skip() const {
  return std::accumulate(m_cacheConfigs.begin(), m_cacheConfigs.end(), 0,
                         [](int acc, const CacheConfig& cacheConfig) {
                           return acc + cacheConfig.m_poolConfigs.size();
                         }) <= 1;
}

void PerformanceMaximization::Context::step() {
  // 1: giver, 2: receiver
  // Get two caches (may be the same)
  int const cacheIdx1 = randomUniformInt(m_cacheConfigs.size());
  int const cacheIdx2 = randomUniformInt(m_cacheConfigs.size());
  // Get a pool from each cache (can't be the same)
  int const poolIdx1 =
      randomUniformInt(m_cacheConfigs[cacheIdx1].m_poolConfigs.size());
  int const poolIdx2 =
      cacheIdx1 == cacheIdx2
          ? (poolIdx1 + 1 +
             randomUniformInt(m_cacheConfigs[cacheIdx1].m_poolConfigs.size() -
                              1)) %
                m_cacheConfigs[cacheIdx1].m_poolConfigs.size()
          : randomUniformInt(m_cacheConfigs[cacheIdx2].m_poolConfigs.size());
  //
  // Get the two caches and pools
  auto& cache1 = m_cacheConfigs[cacheIdx1];
  auto& cache2 = m_cacheConfigs[cacheIdx2];
  auto& pool1 = cache1.m_poolConfigs[poolIdx1];
  auto& pool2 = cache2.m_poolConfigs[poolIdx2];

  // Trade a random amount of space (limited by the lower and upper bounds)
  // Also, they may not overcommit the size of their respective caches
  // TODO: fix
  int const kMaxDelta = std::min({pool1.m_optimalSize - pool1.m_kLowerBound,
                                  pool2.m_kUpperBound - pool2.m_optimalSize,
                                  cacheIdx1 == cacheIdx2
                                      ? cache2.m_kMaxSize - cache2.m_optimalSize
                                      : cache1.m_optimalSize});

  int const kDelta = randomUniformInt(kMaxDelta);
  // Update the optimal size of the pools
  pool1.m_optimalSize -= kDelta;
  pool2.m_optimalSize += kDelta;
  // If the pools are in different caches, update the optimal size of the
  // caches
  if (cacheIdx1 != cacheIdx2) {
    cache1.m_optimalSize -= kDelta;
    cache2.m_optimalSize += kDelta;
  }
}

double PerformanceMaximization::Context::energy() const {
  auto result = std::accumulate(
      m_cacheConfigs.begin(), m_cacheConfigs.end(), 0.0,
      [](double acc, const CacheConfig& cacheConfig) {
        return acc +
               std::accumulate(cacheConfig.m_poolConfigs.begin(),
                               cacheConfig.m_poolConfigs.end(), 0.0,
                               [](double acc, const PoolConfig& poolConfig) {
                                 return acc + poolConfig.getMetric();
                               });
      });
  return result;
}

double PerformanceMaximization::Context::distance(
    Optimizable const* other) const {
  return fabs(energy() - other->energy());
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
