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
#include <vector>

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
    uint32_t const m_kId;
    uint64_t m_optimalSize;
    uint64_t const m_kCurrentSize;
    tk::spline m_utilityCurve;
    uint64_t const m_kLowerBound{0};
    uint64_t const m_kUpperBound{0};
    double getMetric() const { return m_utilityCurve(m_optimalSize); }
  };

  struct CacheConfig {
    std::string const m_kName;
    uint64_t const m_kCurrentSize;
    uint64_t const m_kMaxSize;
    uint64_t m_optimalSize;
    std::vector<PoolConfig> m_poolConfigs;
  };

  struct Context : public Optimizable<Context> {
    std::vector<CacheConfig> m_cacheConfigs;
    void step() override final;
    double energy() const override final;
    double distance(Optimizable const* other) const override final;
    bool skip() const override final;
  };

  MetricType const m_kMetricType;
  double const m_kDelta;
  const uint32_t m_kMRCMinLength{3};
  std::unordered_map<std::string, double> const m_kQoS{};

  void loop(
      std::unordered_map<std::string, CacheStatus>& cacheStatus) override final;

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
