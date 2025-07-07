#include <cachelib/holpaca/data-plane/CacheAllocator.h>
#include <grpcpp/create_channel.h>

namespace facebook {
namespace cachelib {
namespace holpaca {

template <typename CacheTrait>
CacheAllocator<CacheTrait>::CacheAllocator(Config& config)
    : ::facebook::cachelib::CacheAllocator<CacheTrait>(config),
      m_kAddress(config.m_address) {
  if (!m_kAddress.empty() && !config.m_controllerAddress.empty()) {
    m_server =
        grpc::ServerBuilder()
            .AddListeningPort(m_kAddress, grpc::InsecureServerCredentials())
            .RegisterService(dynamic_cast<::holpaca::Stage::Service*>(this))
            .BuildAndStart();
    m_serverThread = std::thread([this] { m_server->Wait(); });
    m_controller =
        std::make_shared<::holpaca::Controller::Stub>(grpc::CreateChannel(
            config.m_controllerAddress, grpc::InsecureChannelCredentials()));
    ::grpc::ClientContext context;
    ::holpaca::ConnectRequest request;
    ::holpaca::ConnectResponse response;

    request.set_cacheaddress(m_kAddress);

    ::grpc::Status status;

    do {
      auto status = m_controller->Connect(&context, request, &response);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (!status.ok());
  }
}

template <typename CacheTrait>
grpc::Status CacheAllocator<CacheTrait>::Resize(
    grpc::ServerContext* context,
    const ::holpaca::ResizeRequest* request,
    ::holpaca::ResizeResponse* response) {
  // CacheLib provides a resize method based on relative (not absolute sizes)
  std::vector<std::pair<int32_t, int64_t>> sortedRelSizes; // relSizes may
                                                           // be negative
  for (const auto& [poolId, poolsize] : request->poolsizes()) {
    sortedRelSizes.push_back({poolId, poolsize.deltasize()});
    // update external size
    {
      std::unique_lock<std::shared_timed_mutex> lock(m_externalSizeMutex);
      for (const auto& [externalCache, extSize] :
           poolsize.externaldeltasizes()) {
        m_externalSize[poolId].insert({externalCache, 0});
        m_externalSize[poolId][externalCache] += extSize;
        if (m_externalSize[poolId][externalCache] == 0) {
          m_externalSize[poolId].erase(externalCache);
        }
      }
    }
  }

  // resizing must be done in order from the most to least downsized pool
  // else, when expanding a pool, we may not have enough memory to expand
  std::sort(sortedRelSizes.begin(), sortedRelSizes.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

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
CacheAllocator<CacheTrait>::~CacheAllocator() {
  if (m_controller) {
    ::grpc::ClientContext context;
    ::holpaca::DisconnectRequest request;
    ::holpaca::DisconnectResponse response;
    request.set_cacheaddress(m_kAddress);
    m_controller->Disconnect(&context, request, &response);
  }
  if (m_server) {
    m_server->Shutdown();
    m_serverThread.join();
  }
}

template <typename CacheTrait>
grpc::Status CacheAllocator<CacheTrait>::GetStatus(
    grpc::ServerContext* context,
    const ::holpaca::GetStatusRequest* request,
    ::holpaca::GetStatusResponse* response) {
  auto cacheStatus = response->mutable_cachestatus();
  auto pools = cacheStatus->mutable_pools();
  cacheStatus->set_maxsize(Super::getCacheMemoryStats().ramCacheSize);

  for (const auto& poolId : Super::getPoolIds()) {
    const auto& pool = Super::getPool(poolId);
    ::holpaca::PoolStatus poolStatus;
    bool const isActive = [&]() {
      std::shared_lock<std::shared_timed_mutex> lock(m_activePoolsMutex);
      return m_activePools.find(poolId) != m_activePools.end();
    }();
    // get ExternalSize
    {
      std::shared_lock<std::shared_timed_mutex> lock(m_externalSizeMutex);
      const auto& map = m_externalSize[poolId];
      *poolStatus.mutable_externalsize() = {map.begin(), map.end()};
    }
    // get MRC
    if (isActive) {
      {
        std::shared_lock<std::shared_timed_mutex> lock(m_shardsMutex);
        auto const& mrc = m_shards[poolId]->mrc();
        *poolStatus.mutable_mrc() = {mrc.begin(), mrc.end()};
      }
      {
        poolStatus.set_diskiops([this, poolId]() {
          std::shared_lock<std::shared_timed_mutex> lock(m_diskIOPSMutex);
          return m_diskIOPS[poolId];
        }());
      }
      {
        poolStatus.set_qos([this, poolId]() {
          std::shared_lock<std::shared_timed_mutex> lock(m_qosLevelsMutex);
          return m_qosLevels[poolId];
        }());
      }
      {
        poolStatus.set_proportion([this, poolId]() {
          std::shared_lock<std::shared_timed_mutex> lock(m_proportionsMutex);
          return m_proportions[poolId];
        }());
      }
    }
    // get active
    poolStatus.set_active(isActive);
    auto pstats = Super::getPoolStats(poolId);
    poolStatus.set_poolid(poolId);
    poolStatus.set_maxsize(pool.getPoolSize());
    poolStatus.set_usedsize(pool.getCurrentAllocSize());
    poolStatus.set_evictions(pstats.numEvictions());
    auto tailAccesses = poolStatus.mutable_tailaccesses();
    for (const auto& [classId, stats] : pstats.cacheStats) {
      (*tailAccesses)[classId] = stats.containerStat.numTailAccesses;
    }
    (*pools)[poolId] = poolStatus;
  }

  return grpc::Status::OK;
}

template <typename CacheTrait>
PoolId CacheAllocator<CacheTrait>::addPool(std::string name,
                                           size_t size,
                                           double qosLevel,
                                           double proportion) {
  PoolId poolId = Super::addPool(name, size); // blocks until there is enough
                                              // space for the pool
  {
    std::unique_lock<std::shared_timed_mutex> lock(m_qosLevelsMutex);
    m_qosLevels[poolId] = qosLevel;
  }
  {
    std::unique_lock<std::shared_timed_mutex> lock(m_externalSizeMutex);
    m_externalSize[poolId] = {};
  }
  {
    std::unique_lock<std::shared_timed_mutex> lock(m_shardsMutex);
    m_shards[poolId] = std::shared_ptr<Shards>(Shards::fixedSize(
        0.001, this->getCacheMemoryStats().ramCacheSize, 100));
  }
  {
    std::unique_lock<std::shared_timed_mutex> lock(m_diskIOPSMutex);
    m_diskIOPS[poolId] = 0;
  }
  {
    std::unique_lock<std::shared_timed_mutex> lock(m_proportionsMutex);
    m_proportions[poolId] = proportion;
  }
  {
    std::unique_lock<std::shared_timed_mutex> lock(m_activePoolsMutex);
    m_activePools.insert(poolId);
  }

  return poolId;
}

template <typename CacheTrait>
typename CacheAllocator<CacheTrait>::ReadHandle
CacheAllocator<CacheTrait>::find(typename CacheAllocator<CacheTrait>::Key key) {
  auto handle = Super::find(key);
  if (handle) {
    std::unique_lock<std::shared_timed_mutex> lock(m_shardsMutex);
    auto poolId =
        Super::getAllocInfo(static_cast<const void*>(handle->getMemory()))
            .poolId;
    std::string keyStr(key.data(), key.size());
    auto size = handle->getSize();
    m_shards[poolId]->feed(keyStr, size);
  }
  return handle;
}

template <typename CacheTrait>
bool CacheAllocator<CacheTrait>::insert(
    const typename CacheAllocator<CacheTrait>::WriteHandle& handle) {
  bool const success = Super::insert(handle);
  if (success) {
    PoolId pid =
        Super::getAllocInfo(static_cast<const void*>(handle->getMemory()))
            .poolId;
    auto key = handle->getKey();
    std::string keyStr(key.data(), key.size());
    auto size = handle->getSize();
    std::unique_lock<std::shared_timed_mutex> lock(m_shardsMutex);
    m_shards[pid]->erase(keyStr);
    m_shards[pid]->feed(keyStr, size);
  }
  return success;
}

template <typename CacheTrait>
typename CacheAllocator<CacheTrait>::WriteHandle
CacheAllocator<CacheTrait>::insertOrReplace(
    const typename CacheAllocator<CacheTrait>::WriteHandle& handle) {
  auto oldHandle = Super::insertOrReplace(handle);
  if (oldHandle) {
    PoolId pid =
        Super::getAllocInfo(static_cast<const void*>(handle->getMemory()))
            .poolId;
    auto key = handle->getKey();
    std::string keyStr(key.data(), key.size());
    auto size = handle->getSize();
    std::unique_lock<std::shared_timed_mutex> lock(m_shardsMutex);
    m_shards[pid]->erase(keyStr);
    m_shards[pid]->feed(keyStr, size);
  }
  return oldHandle;
}

template <typename CacheTrait>
void CacheAllocator<CacheTrait>::registerDiskIOPS(PoolId poolId,
                                                  uint32_t diskIOPS) {
  std::unique_lock<std::shared_timed_mutex> lock(m_diskIOPSMutex);
  m_diskIOPS[poolId] = diskIOPS;
}

template <typename CacheTrait>
void CacheAllocator<CacheTrait>::removePool(PoolId id) {
  {
    std::unique_lock<std::shared_timed_mutex> lock(m_activePoolsMutex);
    m_activePools.erase(id);
    Super::shrinkPool(id, Super::getPool(id).getPoolSize());
  }
  {
    std::unique_lock<std::shared_timed_mutex> lock(m_shardsMutex);
    m_shards.erase(id);
  }
  {
    std::unique_lock<std::shared_timed_mutex> lock(m_diskIOPSMutex);
    m_diskIOPS.erase(id);
  }
  {
    std::unique_lock<std::shared_timed_mutex> lock(m_qosLevelsMutex);
    m_qosLevels.erase(id);
  }
  {
    std::unique_lock<std::shared_timed_mutex> lock(m_externalSizeMutex);
    m_externalSize.erase(id);
  }
  {
    std::unique_lock<std::shared_timed_mutex> lock(m_proportionsMutex);
    m_proportions.erase(id);
  }
}

template class CacheAllocator<::facebook::cachelib::LruCacheTrait>;
template class CacheAllocator<
    ::facebook::cachelib::LruCacheWithSpinBucketsTrait>;
template class CacheAllocator<::facebook::cachelib::Lru2QCacheTrait>;
template class CacheAllocator<::facebook::cachelib::TinyLFUCacheTrait>;
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
