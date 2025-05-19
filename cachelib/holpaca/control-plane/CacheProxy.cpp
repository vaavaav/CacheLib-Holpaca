#include <cachelib/holpaca/control-plane/CacheProxy.h>

namespace facebook {
namespace cachelib {
namespace holpaca {
CacheProxy::CacheProxy(const std::string& address, CommunicationType type)
    : m_kStub(::holpaca::Stage::NewStub(::grpc::CreateChannel(
          address, ::grpc::InsecureChannelCredentials()))),
      m_kType(type) {}

void CacheProxy::resize(
    const std::unordered_map<PoolId, int64_t>& kDeltaSizes) {
  if (m_kType == CommunicationType::kAll) {
    std::vector<PoolId> sortedByDelta;
    for (const auto& [poolId, delta] : kDeltaSizes) {
      sortedByDelta.push_back(poolId);
    }
    std::sort(sortedByDelta.begin(), sortedByDelta.end(),
              [&kDeltaSizes](const auto& a, const auto& b) {
                return kDeltaSizes.at(a) > kDeltaSizes.at(b);
              });

    // In this case, we send the entire resize to each pool
    for (const auto& poolId : sortedByDelta) {
      ::grpc::ClientContext context;
      ::holpaca::ResizePoolRequest request;
      ::holpaca::ResizePoolResponse response;

      request.set_poolid(poolId);
      request.set_deltasize(kDeltaSizes.at(poolId));

      m_kStub->ResizePool(&context, request, &response);
    }

  } else if (m_kType == CommunicationType::kSingle) {
    ::grpc::ClientContext context;
    ::holpaca::ResizeRequest request;
    ::holpaca::ResizeResponse response;
    auto deltaSizes = request.mutable_deltasizes();
    for (const auto& [poolId, delta] : kDeltaSizes) {
      deltaSizes->insert({poolId, delta});
    }

    m_kStub->Resize(&context, request, &response);
  } else {
    throw std::runtime_error("Unknown communication type");
  }
}

CacheStatus CacheProxy::getStatus() {
  CacheStatus cacheStatus;

  ::grpc::ClientContext context;
  ::holpaca::GetCacheStatusRequest request;
  ::holpaca::GetCacheStatusResponse response;
  m_kStub->GetCacheStatus(&context, request, &response);
  cacheStatus.m_maxSize = response.cachestatus().maxsize();
  if (m_kType == CommunicationType::kAll) {
    for (const auto& [poolId, _] : response.cachestatus().pools()) {
      ::grpc::ClientContext pcontext;
      ::holpaca::GetPoolStatusRequest prequest;
      ::holpaca::GetPoolStatusResponse presponse;
      prequest.set_poolid(poolId);
      m_kStub->GetPoolStatus(&pcontext, prequest, &presponse);
      cacheStatus.m_pools[poolId] = PoolStatus{
          .m_maxSize = presponse.poolstatus().maxsize(),
          .m_usedSize = presponse.poolstatus().usedsize(),
          .m_diskIOPS = presponse.poolstatus().diskiops(),
          .m_lookups = presponse.poolstatus().lookups(),
          .m_misses = presponse.poolstatus().misses(),
          .m_evictions = presponse.poolstatus().evictions(),
          .m_tailAccesses = {},
          .m_MRC = {},
      };
      for (const auto& [classId, accesses] :
           presponse.poolstatus().tailaccesses()) {
        cacheStatus.m_pools[poolId].m_tailAccesses[classId] = accesses;
      }
      for (const auto& [key, value] : presponse.poolstatus().mrc()) {
        cacheStatus.m_pools[poolId].m_MRC[key] = value;
      }
    }
  } else if (m_kType == CommunicationType::kSingle) {
    for (const auto& [poolId, ps] : response.cachestatus().pools()) {
      cacheStatus.m_pools[poolId] = PoolStatus{
          .m_maxSize = ps.maxsize(),
          .m_usedSize = ps.usedsize(),
          .m_diskIOPS = ps.diskiops(),
          .m_lookups = ps.lookups(),
          .m_misses = ps.misses(),
          .m_evictions = ps.evictions(),
          .m_tailAccesses = {},
          .m_MRC = {},
      };
      for (const auto& [classId, accesses] : ps.tailaccesses()) {
        cacheStatus.m_pools[poolId].m_tailAccesses[classId] = accesses;
      }
      for (const auto& [key, value] : ps.mrc()) {
        cacheStatus.m_pools[poolId].m_MRC[key] = value;
      }
    }
  } else {
    throw std::runtime_error("Unknown communication type");
  }

  return cacheStatus;
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
