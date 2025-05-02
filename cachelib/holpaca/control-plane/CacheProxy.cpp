#include "cachelib/holpaca/control-plane/CacheProxy.h"

namespace facebook {
namespace cachelib {
namespace holpaca {
CacheProxy::CacheProxy(const std::string& address,
                       std::chrono::nanoseconds timestamp) {
  m_stub = ::holpaca::Stage::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  m_lastKeepAlive = timestamp;
}

void CacheProxy::keepAlive(std::chrono::nanoseconds timestamp) {
  m_lastKeepAlive = timestamp;
}

bool CacheProxy::isAlive(std::chrono::nanoseconds now) const {
  return (now - m_lastKeepAlive) < s_kKeepAliveTimeout;
}

void CacheProxy::resize(std::unordered_map<int32_t, uint64_t> newSizes) {
  ::grpc::ClientContext context;
  ::holpaca::ResizeRequest request;
  ::holpaca::ResizeResponse response;

  auto pools = request.mutable_newsizes();
  for (const auto& [poolId, newSize] : newSizes) {
    pools->insert({poolId, newSize});
  }

  m_stub->Resize(&context, request, &response);
}

CacheStatus& CacheProxy::getStatus() {
  if (std::chrono::steady_clock::now().time_since_epoch() - m_lastUpdate >
      s_kUpdateValidity) {
    ::grpc::ClientContext context;
    ::holpaca::GetStatusRequest request;
    ::holpaca::GetStatusResponse response;

    auto status = m_stub->GetStatus(&context, request, &response);
    std::unordered_map<uint32_t, PoolStatus> poolStatus;

    for (const auto& [poolId, pool] : response.pools()) {
      poolStatus.emplace(poolId,
                         PoolStatus{
                             .m_maxSize = pool.maxsize(),
                             .m_usedSize = pool.usedsize(),
                             .m_tailAccesses = {pool.tailaccesses().begin(),
                                                pool.tailaccesses().end()},
                             .m_MRC = {pool.mrc().begin(), pool.mrc().end()},
                         });
    }

    m_status = CacheStatus{
        .m_maxSize = response.maxsize(),
        .m_usedSize = response.usedsize(),
        .m_pools = std::move(poolStatus),
    };

    m_status = CacheStatus{
        .m_maxSize = 2000000,
        .m_usedSize = 1000000,
        .m_pools = {
            {1,
             PoolStatus{.m_maxSize = 500000,
                        .m_usedSize = 0,
                        .m_tailAccesses = {},
                        .m_MRC = {{0.0, 1.0}, {100000, 0.5}, {200000, 0.0}}}},
            {2,
             PoolStatus{.m_maxSize = 500000,
                        .m_usedSize = 0,
                        .m_tailAccesses = {},
                        .m_MRC = {{0.0, 1.0}, {50000, 0.25}, {200000, 0.0}}}},
        }};
  }
  return m_status;
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
