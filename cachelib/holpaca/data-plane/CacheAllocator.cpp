#include <cachelib/holpaca/data-plane/CacheAllocator.h>
#include <grpcpp/create_channel.h>

namespace facebook {
namespace cachelib {
namespace holpaca {

template <typename CacheTrait>
CacheAllocator<CacheTrait>::CacheAllocator(Config& config)
    : ::facebook::cachelib::CacheAllocator<CacheTrait>(config),
      m_kAddress(config.m_address),
      m_kVirtualSize(config.m_hasVirtualSize ? config.m_virtualSize
                                             : config.size),
      m_kProportion(config.proportion) {
  // avoid reallocation during runtime
  // maximum tolerated number of pools by CacheLib
  m_shardMutexes.reserve(64);
  m_shards.reserve(64);
  m_metrics.reserve(64);
  m_qosLevels.reserve(64);
  m_proportions.reserve(64);
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
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    } while (!status.ok());
  }
}

template <typename CacheTrait>
grpc::Status CacheAllocator<CacheTrait>::Resize(
    grpc::ServerContext* context,
    const ::holpaca::ResizeRequest* request,
    ::holpaca::ResizeResponse* response) {
  // CacheLib provides a resize method based on relative (not absolute sizes)
  std::vector<std::pair<PoolId, int64_t>> sortedRelSizes{}; // relSizes may
                                                            // be negative
  for (const auto& [poolId, poolsize] : request->poolsizes()) {
    auto const originalSize = Super::getPool(poolId).getPoolSize();
    if (poolsize.size() != originalSize) {
      sortedRelSizes.push_back({static_cast<PoolId>(poolId),
                                static_cast<int64_t>(poolsize.size()) -
                                    static_cast<int64_t>(originalSize)});
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
  cacheStatus->set_maxsize(
      std::min(m_kVirtualSize, Super::getCacheMemoryStats().ramCacheSize));
  cacheStatus->set_proportion(m_kProportion);

  for (const auto& poolId : Super::getPoolIds()) {
    const auto& pool = Super::getPool(poolId);
    bool const isActive = m_activePools.find(poolId) != m_activePools.end();
    if (isActive) {
      ::holpaca::PoolStatus poolStatus;
      // get MRC
      {
        std::lock_guard<std::mutex> lg(m_shardMutexes[poolId]);
        auto const& mrc = m_shards[poolId]->mrc();
        *poolStatus.mutable_mrc() = {mrc.begin(), mrc.end()};
      }
      {
        poolStatus.set_diskiops(
            [this, poolId]() { return std::get<0>(m_metrics[poolId]); }());
      }
      {
        poolStatus.set_missratio(
            [this, poolId]() { return std::get<1>(m_metrics[poolId]); }());
      }
      {
        poolStatus.set_throughput(
            [this, poolId]() { return std::get<2>(m_metrics[poolId]); }());
      }
      {
        poolStatus.set_qos([this, poolId]() { return m_qosLevels[poolId]; }());
      }
      {
        poolStatus.set_proportion(
            [this, poolId]() { return m_proportions[poolId]; }());
      }
      auto const& pstats = Super::getPoolStats(poolId);
      poolStatus.set_poolid(poolId);
      poolStatus.set_maxsize(pool.getPoolSize());
      poolStatus.set_usedsize(pool.getCurrentAllocSize());
      (*pools)[poolId] = poolStatus;
    }
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
  m_shards[poolId] = std::shared_ptr<Shards>(
      Shards::fixedSize(0.001, this->getCacheMemoryStats().ramCacheSize, 100));
  m_qosLevels[poolId] = qosLevel;
  m_metrics[poolId] = {0, 1.0, 0}; // diskIOPS, missRatio, throughput
  m_proportions[poolId] = proportion;
  m_activePools.insert(poolId);

  return poolId;
}

template <typename CacheTrait>
typename CacheAllocator<CacheTrait>::ReadHandle
CacheAllocator<CacheTrait>::find(typename CacheAllocator<CacheTrait>::Key key) {
  auto handle = Super::find(key);

  if (handle) {
    auto const kPoolId =
        Super::getAllocInfo(static_cast<const void*>(handle->getMemory()))
            .poolId;
    std::string keyStr(key.data(), key.size());
    uint32_t size = handle->getSize();
    std::lock_guard<std::mutex> lg(m_shardMutexes[kPoolId]);
    m_shards[kPoolId]->feed(keyStr, size);
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
    uint32_t size = handle->getSize();
    std::lock_guard<std::mutex> lg(m_shardMutexes[pid]);
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
    uint32_t size = handle->getSize();
    std::lock_guard<std::mutex> lg(m_shardMutexes[pid]);
    m_shards[pid]->erase(keyStr);
    m_shards[pid]->feed(keyStr, size);
  }
  return oldHandle;
}

template <typename CacheTrait>
void CacheAllocator<CacheTrait>::registerMetrics(PoolId poolId,
                                                 uint32_t diskIOPS,
                                                 double missRatio,
                                                 uint32_t throughput) {
  m_metrics[poolId] = {diskIOPS, missRatio, throughput};
}

template <typename CacheTrait>
void CacheAllocator<CacheTrait>::removePool(PoolId id) {
  m_activePools.erase(id);
  Super::shrinkPool(id, Super::getPool(id).getPoolSize());
}

template class CacheAllocator<::facebook::cachelib::LruCacheTrait>;
template class CacheAllocator<
    ::facebook::cachelib::LruCacheWithSpinBucketsTrait>;
template class CacheAllocator<::facebook::cachelib::Lru2QCacheTrait>;
template class CacheAllocator<::facebook::cachelib::TinyLFUCacheTrait>;
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
