#pragma once
#include <cachelib/holpaca/control-plane/ProxyManager.h>
#include <cachelib/holpaca/control-plane/algorithms/ControlAlgorithm.h>
#include <cachelib/holpaca/control-plane/algorithms/Optimizable.h>
#include <cachelib/holpaca/control-plane/algorithms/Spline.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/* This algorithm has the following assumptions:
 * 1. This is the only running algorithm resizing the pools.
 */

namespace facebook {
namespace cachelib {
namespace holpaca {

class PerformanceMaximization : public ControlAlgorithm {
 public:
  enum class MetricType {
    kHitRatio,
    kThroughput,
  };

 private:
  struct PoolConfig {
    uint64_t m_optimalSize;
    uint64_t const m_kCurrentSize;
    tk::spline m_utilityCurve;
    std::unordered_map<std::string, uint64_t> m_externalSize{};
    uint64_t m_lowerBound{0};
    uint64_t m_upperBound{0};
    double getMetric() const { return 1.0 / std::pow(m_optimalSize + 1, 1.0); };
  };

  struct CacheConfig {
    uint64_t m_maxSize{0};
    uint64_t m_usedSize{0};
    std::unordered_map<PoolId, PoolConfig> m_poolConfigs{};
  };

  struct Context : public Optimizable<Context> {
    std::unordered_map<std::string, CacheConfig> m_cacheConfigs;
    void step() override final;
    double energy() const override final;
    double distance(Optimizable const* other) const override final;
    bool skip() const override final;
  };

  MetricType const m_kMetricType;
  double const m_kDelta;
  const uint32_t m_kMRCMinLength{3};
  std::unordered_map<std::string, double> const m_kQoS{};
  std::unordered_map<std::string, CacheConfig> m_previousIteration{};

  void loop(ProxyManager* const kProxyManager) override final;
  /*

  void handleCacheRemoved(
      const std::string& removedCacheId,
      std::unordered_map<std::string, CacheStatus>& allCacheStatus,
      Context& context) const;

  void handlePoolRemoved(
      const std::string& cacheId,
      const PoolId& removedPoolId,
      std::unordered_map<std::string, CacheStatus>& allCacheStatus,
      Context& context) const;

  void handlePoolAdded(
      const std::string& addedCacheId,
      const PoolId& addedPoolId,
      std::unordered_map<std::string, CacheStatus>& allCacheStatus,
      Context& context) const;
      */

 public:
  PerformanceMaximization(ProxyManager* const kProxyManager,
                          std::chrono::milliseconds const kPeriodicity,
                          MetricType const kMetricType,
                          double const kDelta,
                          const std::unordered_map<std::string, double>& kQoS);
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
