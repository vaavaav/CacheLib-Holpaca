#pragma once
#include <cachelib/holpaca/control-plane/algorithms/ControlAlgorithm.h>

namespace facebook {
namespace cachelib {
namespace holpaca {

class MarginalHits : public ControlAlgorithm {
  static constexpr uint32_t s_kPoolMinSizeSlabs{1 << 22};
  static constexpr uint32_t s_kPoolMaxSizeSlabs{2 * (1 << 22)};
  static constexpr double s_kMovingAverageParam{0.3};

  std::vector<PoolId> m_poolIds;
  std::unordered_set<PoolId> m_validVictims;
  std::unordered_set<PoolId> m_validReceivers;
  std::unordered_map<PoolId, std::unordered_map<ClassId, uint64_t>> m_tailHits;
  std::unordered_map<PoolId, std::unordered_map<ClassId, uint64_t>>
      m_accumTailHits;
  std::unordered_map<PoolId, uint32_t> m_score;
  std::unordered_map<PoolId, double> m_smoothedRanks;

  void loop(ProxyManager* const kProxyManager) override final;

 public:
  MarginalHits(ProxyManager* const kProxyManager,
               std::chrono::milliseconds const kPeriodicity);
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
