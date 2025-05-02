#include "CacheAllocator.h"

#include <grpcpp/create_channel.h>

namespace facebook {
namespace cachelib {
namespace holpaca {

template <typename CacheTrait>
grpc::Status CacheAllocator<CacheTrait>::GetStatus(
    grpc::ServerContext* context,
    const ::holpaca::GetStatusRequest* request,
    ::holpaca::GetStatusResponse* response) {
  response->set_maxsize(this->getCacheMemoryStats().ramCacheSize);
  response->set_usedsize(this->getCacheMemoryStats().ramCacheSize -
                         this->getCacheMemoryStats().unReservedSize);
  auto pools = response->mutable_pools();
  for (const auto pid : this->getPoolIds()) {
    auto stats = this->getPoolStats(pid);
    ::holpaca::GetStatusResponse::PoolStatus s;
    s.set_maxsize(stats.poolSize);
    s.set_usedsize(stats.poolUsableSize + stats.poolAdvisedSize);
    for (auto const& [cid, cs] : stats.cacheStats) {
      s.mutable_tailaccesses()->insert(
          {static_cast<uint32_t>(cid),
           static_cast<uint32_t>(cs.containerStat.numTailAccesses)});
    }
    auto mrc = m_shards[pid]->mrc();
    s.mutable_mrc()->insert(mrc.begin(), mrc.end());
    pools->insert({pid, s});
  }

  return grpc::Status::OK;
}

template <typename CacheTrait>
grpc::Status CacheAllocator<CacheTrait>::Resize(
    grpc::ServerContext* context,
    const ::holpaca::ResizeRequest* request,
    ::holpaca::ResizeResponse* response) {
  // CacheLib provides a resize method based on relative (not absolute sizes)
  std::vector<std::pair<int32_t, int64_t>> sortedRelSizes; // relSizes may
                                                           // be negative
  for (const auto& [poolId, newSize] : request->newsizes()) {
    sortedRelSizes.push_back(
        {poolId, newSize - this->getPoolStats(poolId).poolSize});
  }

  // resizing must be done in order from the most to least downsized pool
  // else, when expanding a pool, we may not have enough memory to expand
  std::sort(sortedRelSizes.begin(), sortedRelSizes.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  for (auto [poolId, relSize] : sortedRelSizes) {
    if (relSize < 0) {
      // downsizing
      this->shrinkPool(static_cast<::facebook::cachelib::PoolId>(poolId),
                       -relSize);
    } else {
      // upsizing
      this->growPool(static_cast<::facebook::cachelib::PoolId>(poolId),
                     relSize);
    }
  }
  return grpc::Status::OK;
}

template <typename CacheTrait>
CacheAllocator<CacheTrait>::CacheAllocator(Config& config)
    : ::facebook::cachelib::CacheAllocator<CacheTrait>(config) // deliberate
                                                               // slicing
{
  m_server =
      grpc::ServerBuilder()
          .AddListeningPort(config.m_address, grpc::InsecureServerCredentials())
          .RegisterService(dynamic_cast<::holpaca::Stage::Service*>(this))
          .BuildAndStart();
  m_serverThread = std::thread([this] { m_server->Wait(); });
  m_keepAliveThread = std::thread([this, config] {
    auto controllerStub = ::holpaca::Controller::NewStub(grpc::CreateChannel(
        config.m_controllerAddress, grpc::InsecureChannelCredentials()));
    while (!m_stop) {
      grpc::ClientContext ctx;
      ::holpaca::KeepAliveRequest req;
      ::holpaca::KeepAliveResponse rep;
      req.set_address(config.m_address);
      controllerStub->KeepAlive(&ctx, req, &rep);
      std::this_thread::sleep_for(s_KeepAlivePeriodicity);
    }
  });
}

template <typename CacheTrait>
PoolId CacheAllocator<CacheTrait>::addPool(std::string name, size_t size) {
  auto poolId = Super::addPool(name, size);

  m_shards[poolId] = std::unique_ptr<Shards>(
      Shards::fixedSize(0.0001, this->getCacheMemoryStats().ramCacheSize, 100));
  return poolId;
}

template <typename CacheTrait>
CacheAllocator<CacheTrait>::~CacheAllocator() {
  m_stop.exchange(true);
  m_keepAliveThread.join();
  m_server->Shutdown();
  m_serverThread.join();
}

template <typename CacheTrait>
void CacheAllocator<CacheTrait>::registerAccess(PoolId id,
                                                const std::string& key,
                                                uint32_t& size,
                                                bool reset) {
  if (reset) {
    m_shards[id]->erase(key);
  }
  m_shards[id]->feed(key, size);
}

template class CacheAllocator<::facebook::cachelib::LruCacheTrait>;
template class CacheAllocator<
    ::facebook::cachelib::LruCacheWithSpinBucketsTrait>;
template class CacheAllocator<::facebook::cachelib::Lru2QCacheTrait>;
template class CacheAllocator<::facebook::cachelib::TinyLFUCacheTrait>;
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
