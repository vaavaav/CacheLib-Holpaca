#include <cachelib/holpaca/control-plane/algorithms/MarginalHits.h>

#include <unordered_set>

namespace facebook {
namespace cachelib {
namespace holpaca {

MarginalHits::MarginalHits(ProxyManager* const kProxyManager,
                           std::chrono::milliseconds const kPeriodicity)
    : ControlAlgorithm(kProxyManager, kPeriodicity) {}

void MarginalHits::loop(ProxyManager* const kProxyManager) {
  std::vector<ProxyManager::CacheResize> cacheResizes;
  auto allCacheStatus = kProxyManager->getStatus();
  for (const auto& [cacheId, cacheStatus] : allCacheStatus) {
    for (const auto& [poolId, poolStatus] : cacheStatus.m_pools) {
      if (poolStatus.m_evictions > 0 &&
          poolStatus.m_usedSize > s_kPoolMinSizeSlabs) {
        m_validVictims.insert(poolId);
      }
      if ((poolStatus.m_maxSize - poolStatus.m_usedSize) <
          s_kPoolMaxSizeSlabs) {
        m_validReceivers.insert(poolId);
      }
      m_poolIds.push_back(poolId);
      for (auto const& [cid, tailAccesses] : poolStatus.m_tailAccesses) {
        m_accumTailHits[poolId].emplace(cid, tailAccesses);
        m_tailHits[poolId][cid] = tailAccesses - m_accumTailHits[poolId][cid];
      }
    }

    if (!m_validVictims.empty() && !m_validReceivers.empty()) {
      for (auto const& pid : m_poolIds) {
        m_smoothedRanks[pid] = 0;
        m_score[pid] =
            std::max_element(m_tailHits[pid].begin(), m_tailHits[pid].end(),
                             [](auto const& a, auto const& b) {
                               return a.second < b.second;
                             })
                ->second;
      }
      auto cmp = [&](auto x, auto y) { return m_score[x] < m_score[y]; };
      std::sort(m_poolIds.begin(), m_poolIds.end(), cmp);
      for (int i = 0; i < m_poolIds.size(); i++) {
        auto& avg = m_smoothedRanks[m_poolIds[i]];
        avg = avg * s_kMovingAverageParam + i * (1 - s_kMovingAverageParam);
      }
      uint32_t victim =
          *std::min_element(m_validVictims.begin(), m_validVictims.end(),
                            [&](auto const& a, auto const& b) {
                              return m_smoothedRanks[a] < m_smoothedRanks[b];
                            });
      uint32_t receiver =
          *std::max_element(m_validReceivers.begin(), m_validReceivers.end(),
                            [&](auto const& a, auto const& b) {
                              return m_smoothedRanks[a] < m_smoothedRanks[b];
                            });
      std::unordered_map<PoolId, int64_t> const deltas{
          {victim, -s_kPoolMinSizeSlabs}, {receiver, s_kPoolMinSizeSlabs}};

      cacheResizes.emplace_back(ProxyManager::CacheResize{
          .m_kName = cacheId,
          .m_kPoolResizes =
              {
                  ProxyManager::PoolResize{
                      .m_kId = victim,
                      .m_kDeltaSize = -s_kPoolMinSizeSlabs,
                  },
                  ProxyManager::PoolResize{.m_kId = receiver,
                                           .m_kDeltaSize = s_kPoolMinSizeSlabs},
              },
      });
    }
  }
  if (!cacheResizes.empty()) {
    kProxyManager->resize(cacheResizes);
  }
  // clear the data structures
  m_poolIds.clear();
  m_validVictims.clear();
  m_validReceivers.clear();
  m_tailHits.clear();
  m_accumTailHits.clear();
  m_score.clear();
  m_smoothedRanks.clear();
}
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
