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
      m_kQoS(kQoS) {} // TODO: use QoS to change lower bounds

void PerformanceMaximization::loop(ProxyManager* const kProxyManager) {
  // collect
  Context context;
  std::vector<CacheConfig> cacheConfigs;
  for (const auto& [cacheId, cacheStatus] : kProxyManager->getStatus()) {
    std::vector<PoolConfig> poolConfigs;
    for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
      if (poolStatus.m_MRC.size() >= m_kMRCMinLength) {
        std::vector<double> cacheSizes;
        std::vector<double> metrics;
        for (const auto& [size, missRatio] : poolStatus.m_MRC) {
          cacheSizes.emplace_back(size);
          auto metric = missRatio;
          if (m_kMetricType == MetricType::kThroughput) {
            metric = (poolStatus.m_diskIOPS > 0) *
                     (missRatio / poolStatus.m_diskIOPS);
          }
          metrics.emplace_back(metric);
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
          .m_maxSize = cacheStatus.m_maxSize,
          .m_poolConfigs = std::move(poolConfigs),
      });
    }
  }

  // compute
  context.run(2000, 250, 90, 0.1, 1.003);

  // enforce
  std::vector<ProxyManager::CacheResize> cacheResizes;
  for (const auto& cacheConfig : context.m_cacheConfigs) {
    ProxyManager::CacheResize cacheResize;
    cacheResize.m_kName = cacheConfig.m_id;
    for (const auto& poolConfig : cacheConfig.m_poolConfigs) {
      ProxyManager::PoolResize poolResize;
      poolResize.m_kId = poolConfig.m_kId;
      poolResize.m_kDeltaSize =
          poolConfig.m_optimalSize - poolConfig.m_kCurrentSize;
      poolResize.m_kExternalDeltaSize = {};
      cacheResize.m_kPoolResizes.emplace_back(poolResize);
    }
    cacheResizes.emplace_back(cacheResize);
  }

  if (!cacheResizes.empty()) {
    kProxyManager->resize(cacheResizes);
  }
}

bool PerformanceMaximization::Context::skip() const {
  // TODO: fix
  return m_cacheConfigs.size() <= 0;
}

void PerformanceMaximization::Context::step() {
  // TODO: fix
  // 1: giver, 2: receiver
  int const poolIdx1 = randomUniformInt(m_cacheConfigs[0].m_poolConfigs.size());
  int const poolIdx2 =
      (poolIdx1 + 1 +
       randomUniformInt(m_cacheConfigs[0].m_poolConfigs.size() - 1)) %
      m_cacheConfigs[0].m_poolConfigs.size();

  // Get the two caches and pools
  auto& pool1 = m_cacheConfigs[0].m_poolConfigs[poolIdx1];
  auto& pool2 = m_cacheConfigs[0].m_poolConfigs[poolIdx2];

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
  return std::accumulate(m_cacheConfigs[0].m_poolConfigs.begin(),
                         m_cacheConfigs[0].m_poolConfigs.end(), 0.0,
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
