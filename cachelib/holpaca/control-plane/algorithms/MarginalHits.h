#pragma once
#include <cachelib/holpaca/control-plane/algorithms/ControlAlgorithm.h>

namespace facebook {
namespace cachelib {
namespace holpaca {

class MarginalHits : public ControlAlgorithm {
  static constexpr uint32_t s_kPoolMinSizeSlabs{1 << 22};
  static constexpr uint32_t s_kPoolMaxSizeSlabs{2 * (1 << 22)};
  static constexpr double s_kMovingAverageParam{0.3};

  std::vector<uint32_t> m_poolIds;
  std::unordered_set<uint32_t> m_validVictims;
  std::unordered_set<uint32_t> m_validReceivers;
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint64_t>>
      m_tailHits;
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint64_t>>
      m_accumTailHits;
  std::unordered_map<uint32_t, uint32_t> m_score;
  std::unordered_map<uint32_t, double> m_smoothedRanks;

  void loop(
      std::unordered_map<std::string, CacheStatus>& cacheStatus) override final;

 public:
  MarginalHits(ProxyManager* const kProxyManager,
               std::chrono::milliseconds const kPeriodicity);
};

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
