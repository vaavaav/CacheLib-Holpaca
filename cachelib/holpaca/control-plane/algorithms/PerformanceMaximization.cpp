#include <cachelib/holpaca/control-plane/algorithms/PerformanceMaximization.h>

#include <numeric>

namespace facebook {
namespace cachelib {
namespace holpaca {

PerformanceMaximization::PerformanceMaximization(
    std::shared_ptr<CacheProxy> const kCacheProxy,
    std::chrono::milliseconds const kPeriodicity,
    MetricType const kMetricType,
    double const kDelta,
    const std::unordered_map<std::string, double>& kQoS)
    : ControlAlgorithm(kCacheProxy, kPeriodicity),
      m_kDelta(kDelta),
      m_kMetricType(kMetricType),
      m_kQoS(kQoS) {} // TODO: use QoS to change lower bounds

void PerformanceMaximization::loop(CacheStatus&& cacheStatus) {
  // collect
  Context context;
  std::vector<PoolConfig> poolConfigs;
  for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
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
          missRatio =
              (poolStatus.m_diskIOPS > 0) * (missRatio / poolStatus.m_diskIOPS);
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
    context.m_cacheConfig = CacheConfig{
        .m_maxSize = cacheStatus.m_maxSize,
        .m_poolConfigs = std::move(poolConfigs),
    };
  }

  // compute
  context.run(2000, 250, 90, 0.1, 1.003);

  // enforce
  std::unordered_map<PoolId, int64_t> deltas;
  for (const auto& poolConfig : context.m_cacheConfig.m_poolConfigs) {
    std::cout << "Pool " << static_cast<int32_t>(poolConfig.m_kId) << ": "
              << poolConfig.m_kCurrentSize << " -> " << poolConfig.m_optimalSize
              << std::endl;
    deltas[poolConfig.m_kId] =
        poolConfig.m_optimalSize - poolConfig.m_kCurrentSize;
  }
  m_kCacheProxy->resize(deltas);
}

bool PerformanceMaximization::Context::skip() const {
  return m_cacheConfig.m_poolConfigs.size() <= 1;
}

void PerformanceMaximization::Context::step() {
  // 1: giver, 2: receiver
  int const poolIdx1 = randomUniformInt(m_cacheConfig.m_poolConfigs.size());
  int const poolIdx2 =
      (poolIdx1 + 1 +
       randomUniformInt(m_cacheConfig.m_poolConfigs.size() - 1)) %
      m_cacheConfig.m_poolConfigs.size();

  // Get the two caches and pools
  auto& pool1 = m_cacheConfig.m_poolConfigs[poolIdx1];
  auto& pool2 = m_cacheConfig.m_poolConfigs[poolIdx2];

  // Trade a random amount of space (limited by the lower and upper bounds)
  int const kMaxDelta = std::min({pool1.m_optimalSize - pool1.m_kLowerBound,
                                  pool2.m_kUpperBound - pool2.m_optimalSize});

  if (kMaxDelta > 0) {
    int const kDelta = randomUniformInt(kMaxDelta);
    // Update the optimal size of the pools
    pool1.m_optimalSize -= kDelta;
    pool2.m_optimalSize += kDelta;
  }
}

double PerformanceMaximization::Context::energy() const {
  return std::accumulate(m_cacheConfig.m_poolConfigs.begin(),
                         m_cacheConfig.m_poolConfigs.end(), 0.0,
                         [](double acc, const PoolConfig& poolConfig) {
                           return acc + poolConfig.getMetric();
                         });
}

double PerformanceMaximization::Context::distance(
    Optimizable const* other) const {
  return fabs(energy() - other->energy());
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
