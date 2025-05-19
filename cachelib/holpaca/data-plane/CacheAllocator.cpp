#include <cachelib/holpaca/data-plane/CacheAllocator.h>
#include <grpcpp/create_channel.h>

namespace facebook {
namespace cachelib {
namespace holpaca {

template <typename CacheTrait>
grpc::Status CacheAllocator<CacheTrait>::Resize(
    grpc::ServerContext* context,
    const ::holpaca::ResizeRequest* request,
    ::holpaca::ResizeResponse* response) {
  // CacheLib provides a resize method based on relative (not absolute sizes)
  std::vector<std::pair<int32_t, int64_t>> sortedRelSizes; // relSizes may
                                                           // be negative
  for (const auto& [poolId, delta] : request->deltasizes()) {
    sortedRelSizes.push_back({poolId, delta});
  }

  // resizing must be done in order from the most to least downsized pool
  // else, when expanding a pool, we may not have enough memory to expand
  std::sort(sortedRelSizes.begin(), sortedRelSizes.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  for (auto [poolId, relSize] : sortedRelSizes) {
    if (relSize < 0) {
      Super::shrinkPool(poolId, -relSize);
    } else {
      Super::growPool(poolId, relSize);
    }
  }
  return grpc::Status::OK;
}

template <typename CacheTrait>
grpc::Status CacheAllocator<CacheTrait>::ResizePool(
    grpc::ServerContext* context,
    const ::holpaca::ResizePoolRequest* request,
    ::holpaca::ResizePoolResponse* response) {
  if (request->deltasize() < 0) {
    Super::shrinkPool(request->poolid(), -request->deltasize());
  } else {
    Super::growPool(request->poolid(), request->deltasize());
  }
  return grpc::Status::OK;
}

template <typename CacheTrait>
grpc::Status CacheAllocator<CacheTrait>::GetCacheStatus(
    grpc::ServerContext* context,
    const ::holpaca::GetCacheStatusRequest* request,
    ::holpaca::GetCacheStatusResponse* response) {
  auto cacheStatus = response->mutable_cachestatus();
  auto pools = cacheStatus->mutable_pools();
  cacheStatus->set_maxsize(Super::getCacheMemoryStats().ramCacheSize);

  for (const auto& poolId : Super::getPoolIds()) {
    Metrics metrics;
    metrics = m_metrics[poolId];
    const auto& pool = Super::getPool(poolId);
    ::holpaca::PoolStatus poolStatus;
    poolStatus.set_poolid(poolId);
    poolStatus.set_maxsize(pool.getPoolSize());
    poolStatus.set_usedsize(pool.getCurrentAllocSize());
    poolStatus.set_diskiops(metrics.m_diskIOPS);
    poolStatus.set_lookups(metrics.m_lookups);
    poolStatus.set_misses(metrics.m_misses);
    auto pstats = Super::getPoolStats(poolId);
    poolStatus.set_evictions(pstats.numEvictions());
    auto tailAccesses = poolStatus.mutable_tailaccesses();
    for (const auto& [classId, stats] : pstats.cacheStats) {
      (*tailAccesses)[classId] = stats.containerStat.numTailAccesses;
    }
    auto mrc = metrics.m_shards->mrc();
    poolStatus.mutable_mrc()->insert(mrc.begin(), mrc.end());
    (*pools)[poolId] = poolStatus;
  }

  return grpc::Status::OK;
}

// TODO: check if we need mutex
template <typename CacheTrait>
grpc::Status CacheAllocator<CacheTrait>::GetPoolStatus(
    grpc::ServerContext* context,
    const ::holpaca::GetPoolStatusRequest* request,
    ::holpaca::GetPoolStatusResponse* response) {
  auto poolId = request->poolid();
  const auto& pool = Super::getPool(poolId);
  auto poolStatus = response->mutable_poolstatus();
  Metrics metrics;
  metrics = m_metrics[poolId];
  poolStatus->set_poolid(poolId);
  poolStatus->set_maxsize(pool.getPoolSize());
  poolStatus->set_usedsize(pool.getCurrentAllocSize());
  poolStatus->set_diskiops(metrics.m_diskIOPS);
  poolStatus->set_lookups(metrics.m_lookups);
  poolStatus->set_misses(metrics.m_misses);
  auto pstats = Super::getPoolStats(poolId);
  poolStatus->set_evictions(pstats.numEvictions());
  auto tailAccesses = poolStatus->mutable_tailaccesses();
  for (const auto& [classId, stats] : pstats.cacheStats) {
    (*tailAccesses)[classId] = stats.containerStat.numTailAccesses;
  }
  auto mrc = metrics.m_shards->mrc();
  poolStatus->mutable_mrc()->insert(mrc.begin(), mrc.end());

  return grpc::Status::OK;
}

template <typename CacheTrait>
CacheAllocator<CacheTrait>::CacheAllocator(Config& config)
    : ::facebook::cachelib::CacheAllocator<CacheTrait>(config) // deliberate
                                                               // slicing
{
  if (!config.m_address.empty()) {
    m_server =
        grpc::ServerBuilder()
            .AddListeningPort(config.m_address,
                              grpc::InsecureServerCredentials())
            .RegisterService(dynamic_cast<::holpaca::Stage::Service*>(this))
            .BuildAndStart();
    m_serverThread = std::thread([this] { m_server->Wait(); });
  }
}

template <typename CacheTrait>
PoolId CacheAllocator<CacheTrait>::addPool(std::string name, size_t size) {
  auto poolId = Super::addPool(name, size);

  m_metrics[poolId].m_shards = std::unique_ptr<Shards>(
      Shards::fixedSize(0.0001, this->getCacheMemoryStats().ramCacheSize, 100));

  return poolId;
}

template <typename CacheTrait>
CacheAllocator<CacheTrait>::~CacheAllocator() {
  m_stop.exchange(true);
  if (m_server != nullptr) {
    m_server->Shutdown();
  }
  if (m_serverThread.joinable()) {
    m_serverThread.join();
  }
}

template <typename CacheTrait>
void CacheAllocator<CacheTrait>::registerAccess(PoolId id,
                                                const std::string& key,
                                                uint32_t& size,
                                                bool isLookup,
                                                bool isMiss,
                                                bool reset) {
  if (isLookup) {
    m_metrics[id].m_lookups++;
    if (isMiss) {
      m_metrics[id].m_misses++;
    }
  }
  if (reset) {
    m_metrics[id].m_shards->erase(key);
  }
  m_metrics[id].m_shards->feed(key, size);
}

template <typename CacheTrait>
void CacheAllocator<CacheTrait>::registerMetrics(PoolId id,
                                                 const uint32_t diskIOPS) {
  m_metrics[id].m_diskIOPS = diskIOPS;
}

template <typename CacheTrait>
void CacheAllocator<CacheTrait>::removePool(PoolId id) {
  m_metrics.erase(id);
  Super::removePool(id);
}

template class CacheAllocator<::facebook::cachelib::LruCacheTrait>;
template class CacheAllocator<
    ::facebook::cachelib::LruCacheWithSpinBucketsTrait>;
template class CacheAllocator<::facebook::cachelib::Lru2QCacheTrait>;
template class CacheAllocator<::facebook::cachelib::TinyLFUCacheTrait>;
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
