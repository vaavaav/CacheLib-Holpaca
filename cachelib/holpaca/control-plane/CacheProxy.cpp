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

std::unordered_map<int32_t, PoolStatus> CacheProxy::getStatus() {
  ::grpc::ClientContext context;
  ::holpaca::GetStatusRequest request;
  ::holpaca::GetStatusResponse response;

  auto status = m_stub->GetStatus(&context, request, &response);
  std::unordered_map<int32_t, PoolStatus> poolStatus;

  for (const auto& [poolId, pool] : response.pools()) {
    PoolStatus s;
    s.maxSize = pool.maxsize();
    s.usedSize = pool.usedsize();
    for (const auto& [cid, tailAccesses] : pool.tailaccesses()) {
      s.tailAccesses[cid] = tailAccesses;
    }
    for (const auto& [cid, mrc] : pool.mrc()) {
      s.mrc[cid] = mrc;
    }
    poolStatus[poolId] = std::move(s);
  }

  return poolStatus;
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
