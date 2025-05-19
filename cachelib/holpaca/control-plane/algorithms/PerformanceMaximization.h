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
    PoolId const m_kId;
    uint64_t m_optimalSize;
    uint64_t const m_kCurrentSize;
    tk::spline m_utilityCurve;
    uint64_t const m_kLowerBound{0};
    uint64_t const m_kUpperBound{0};
    double getMetric() const { return 1.0 / std::pow(m_optimalSize + 1, 1.0); };
  };

  struct CacheConfig {
    uint64_t m_maxSize{0};
    std::vector<PoolConfig> m_poolConfigs;
  };

  struct Context : public Optimizable<Context> {
    CacheConfig m_cacheConfig;
    void step() override final;
    double energy() const override final;
    double distance(Optimizable const* other) const override final;
    bool skip() const override final;
  };

  MetricType const m_kMetricType;
  double const m_kDelta;
  const uint32_t m_kMRCMinLength{3};
  std::unordered_map<std::string, double> const m_kQoS{};

  void loop(CacheStatus&& cacheStatus) override final;

 public:
  PerformanceMaximization(std::shared_ptr<CacheProxy> const kCacheProxy,
                          std::chrono::milliseconds const kPeriodicity,
                          MetricType const kMetricType,
                          double const kDelta,
                          const std::unordered_map<std::string, double>& kQoS);
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
