#include "HitRatioMaximization.h"

namespace facebook {
namespace cachelib {
namespace holpaca {

HitRatioMaximization::HitRatioMaximization(
    ProxyManager* const kProxyManager,
    std::chrono::milliseconds const kPeriodicity,
    double const kDelta,
    const std::unordered_map<std::string, double>& kHitRatioQoS)
    : Optimizable({.m_kMaxTries = 2000,
                   .m_kIterationsPerTemperature = 250,
                   .m_kInitialTemperature = 90,
                   .m_kMinTemperature = 0.1,
                   .m_kCoolingRate = 1.003}),
      m_kProxyManager(kProxyManager),
      m_kPeriodicity(kPeriodicity),
      m_kDelta(kDelta),
      m_kHitRatioQoS(kHitRatioQoS) {
  m_thread = std::thread([this] {
    while (!m_stop) {
      std::cout << "Collecting..." << std::endl; // DEBUG
      collect();
      std::cout << "Checking if we can skip..." << std::endl; // DEBUG
      if (!skip()) {
        std::cout << "Running optimization..." << std::endl; // DEBUG
        run();
        std::cout << "Enforcing..." << std::endl; // DEBUG
        enforce();
      }
      std::cout << "Sleeping..." << std::endl; // DEBUG
      std::this_thread::sleep_for(m_kPeriodicity);
    }
  });
}

HitRatioMaximization::~HitRatioMaximization() {
  m_stop = true;
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

void HitRatioMaximization::collect() {
  m_cacheConfigs.clear();
  for (const auto& [address, proxy] : m_kProxyManager->getCaches()) {
    std::cout << "Collecting from " << address << std::endl; // DEBUG
    auto status = proxy->getStatus();
    std::vector<PoolConfig> poolConfigs;
    for (const auto& [poolId, poolStatus] : status.m_pools) {
      if (poolStatus.m_MRC.size() >= m_kMRCMinLength) {
        std::vector<double> cacheSizes;
        std::vector<double> missRatios;
        for (const auto& [size, missRatio] : poolStatus.m_MRC) {
          cacheSizes.emplace_back(size);
          missRatios.emplace_back(missRatio);
        }
        poolConfigs.emplace_back(PoolConfig{
            .m_kId = poolId,
            .m_optimalSize = poolStatus.m_maxSize,
            .m_kCurrentSize = poolStatus.m_maxSize,
            .m_MRC =
                tk::spline(cacheSizes, missRatios, tk::spline::cspline, true),
            .m_kLowerBound =
                static_cast<uint64_t>((1 - m_kDelta) * poolStatus.m_maxSize),
            .m_kUpperBound =
                static_cast<uint64_t>((1 + m_kDelta) * poolStatus.m_maxSize),
        });
      }
    }
    if (!poolConfigs.empty()) {
      m_cacheConfigs.emplace_back(CacheConfig{
          .m_kName = address,
          .m_kCurrentSize = status.m_maxSize,
          .m_kMaxSize = status.m_usedSize,
          .m_optimalSize = status.m_maxSize,
          .m_poolConfigs = std::move(poolConfigs),
      });
    }
  }
}

void HitRatioMaximization::enforce() {
  // print
  for (const auto& cacheConfig : m_cacheConfigs) {
    std::unordered_map<int32_t, uint64_t> newSizes;
    for (const auto& poolConfig : cacheConfig.m_poolConfigs) {
      std::cout << "Pool " << poolConfig.m_kId << ": "
                << poolConfig.m_kCurrentSize << " -> "
                << poolConfig.m_optimalSize << std::endl;
    }
  }
}

bool HitRatioMaximization::skip() {
  int active = 0;
  for (const auto& cacheConfig : m_cacheConfigs) {
    active += cacheConfig.m_poolConfigs.size();
  }
  return active < 2;
}

void HitRatioMaximization::step() {
  // 1: giver, 2: receiver
  // Get two caches (may be the same)
  int const cacheIdx1 = getRandomUniformInt(m_cacheConfigs.size());
  int const cacheIdx2 = getRandomUniformInt(m_cacheConfigs.size());
  // Get a pool from each cache (can't be the same)
  int const poolIdx1 =
      getRandomUniformInt(m_cacheConfigs[cacheIdx1].m_poolConfigs.size());
  int const poolIdx2 =
      cacheIdx1 == cacheIdx2
          ? (poolIdx1 + 1 +
             getRandomUniformInt(
                 m_cacheConfigs[cacheIdx1].m_poolConfigs.size() - 1)) %
                m_cacheConfigs[cacheIdx1].m_poolConfigs.size()
          : getRandomUniformInt(m_cacheConfigs[cacheIdx2].m_poolConfigs.size());
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

  int const kDelta = getRandomUniformInt(kMaxDelta);
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

double HitRatioMaximization::energy() const {
  auto result = std::accumulate(
      m_cacheConfigs.begin(), m_cacheConfigs.end(), 0.0,
      [](double acc, const CacheConfig& cacheConfig) {
        return acc +
               std::accumulate(cacheConfig.m_poolConfigs.begin(),
                               cacheConfig.m_poolConfigs.end(), 0.0,
                               [](double acc, const PoolConfig& poolConfig) {
                                 return acc + poolConfig.getMissRatio();
                               });
      });
  return result;
}

double HitRatioMaximization::distance(Optimizable const* other) const {
  return fabs(energy() - other->energy());
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
