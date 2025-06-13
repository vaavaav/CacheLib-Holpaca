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
    double getMetric() const { return m_utilityCurve(m_optimalSize); };
  };

  struct CacheConfig {
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
  std::unordered_map<std::string, std::unordered_set<PoolId>>
      m_previouslyActive{};

  /*
   * Maximum size that virtual internal cache can take.
   * This assumes the same value for all instances.
   */
  uint64_t const m_kMaxInternalCacheSize;

  void loop(ProxyManager* const kProxyManager) override final;

 public:
  PerformanceMaximization(ProxyManager* const kProxyManager,
                          std::chrono::milliseconds const kPeriodicity,
                          MetricType const kMetricType,
                          double const kDelta,
                          const std::unordered_map<std::string, double>& kQoS,
                          uint64_t maxInternalCacheSize);
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
