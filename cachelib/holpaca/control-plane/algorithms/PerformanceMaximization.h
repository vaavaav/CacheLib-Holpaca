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
    uint64_t m_lowerBound{0};
    uint64_t m_upperBound{0};
    tk::spline m_utilityCurve;
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
  // The maximum change in size (as a fraction of current size) per iteration
  double const m_kDelta{0.05};
  // Minimum length of MRC to consider the pool for optimization
  const uint32_t m_kMRCMinLength{3};
  // Margin for QoS
  double const m_kQoSMargin{0.10};

  bool const m_kFakeEnforce{false};
  uint64_t m_printLatenciesOnEntries{0};

  std::vector<std::tuple<std::chrono::duration<double, std::milli>,
                         std::chrono::duration<double, std::milli>,
                         std::chrono::duration<double, std::milli>>>
      m_latencies;

  struct PoolAvgMetrics {
    double m_missRatio{1.0};
    uint32_t m_diskIOPS{0};
    uint32_t m_throughput{0};
  };

  std::unordered_map<std::string, std::unordered_map<PoolId, PoolAvgMetrics>>
      m_poolAvgMetricsHistory;

  const double m_kMovingAverageParam{0.3};

  void loop(ProxyManager* const kProxyManager) override final;

 public:
  PerformanceMaximization(ProxyManager* const kProxyManager,
                          std::chrono::milliseconds const kPeriodicity,
                          MetricType const kMetricType,
                          double const kDelta,
                          bool const kFakeEnforce,
                          uint64_t const kPrintLatenciesOnEntries);
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
