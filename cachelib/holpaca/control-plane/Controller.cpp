#include <cachelib/holpaca/control-plane/Controller.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <numeric>

namespace facebook {
namespace cachelib {
namespace holpaca {

std::unordered_map<std::string, ProxyManager::CacheStatus>
Controller::getStatus() {
  std::unordered_map<std::string, ProxyManager::CacheStatus> cacheStatus;
  std::shared_lock<std::shared_timed_mutex> lock(m_proxiesMutex);
  for (const auto& [peer, proxy] : m_proxies) {
    ::grpc::ClientContext context;
    ::holpaca::GetStatusRequest request;
    ::holpaca::GetStatusResponse response;
    proxy->GetStatus(&context, request, &response);
    cacheStatus[peer] = CacheStatus{
        .m_maxSize = response.cachestatus().maxsize(),
        .m_pools = {},
    };
    for (const auto& [poolId, ps] : response.cachestatus().pools()) {
      cacheStatus[peer].m_pools[poolId] = PoolStatus{
          .m_isActive = ps.active(),
          .m_maxSize = ps.maxsize(),
          .m_usedSize = ps.usedsize(),
          .m_diskIOPS = ps.diskiops(),
          .m_evictions = ps.evictions(),
          .m_externalSize = {ps.externalsize().begin(),
                             ps.externalsize().end()},
          .m_tailAccesses = {ps.tailaccesses().begin(),
                             ps.tailaccesses().end()},
          .m_MRC = {ps.mrc().begin(), ps.mrc().end()},
      };
    }
  }

  /*
  std::cout << "Controller: Retrieved status from " << cacheStatus.size()
            << " proxies." << std::endl;
  for (const auto& [peer, status] : cacheStatus) {
    std::cout << peer << ":\n";
    std::cout << "  Max Size: " << status.m_maxSize << "\n";
    std::cout << "  Pools:\n";
    std::cout << "    Pool ID | Active | Max Size | Used Size | Disk IOPS | "
                 "Evictions\n";
    for (const auto& [poolId, poolStatus] : status.m_pools) {
      std::cout << "    " << static_cast<uint32_t>(poolId) << " | "
                << (poolStatus.m_isActive ? "Yes" : "No") << " | "
                << poolStatus.m_maxSize << " | " << poolStatus.m_usedSize
                << " | " << poolStatus.m_diskIOPS << " | "
                << poolStatus.m_evictions << "\n";
    }
  }
  */

  return cacheStatus;
}

void Controller::resize(
    const std::vector<ProxyManager::CacheResize>& cacheResize) {
  std::unique_lock<std::shared_timed_mutex> lock(m_proxiesMutex);

  if (cacheResize.size() != m_proxies.size()) {
    std::cerr << "Controller: Mismatch in number of proxies and resize "
                 "requests. Expected "
              << m_proxies.size() << " but got " << cacheResize.size()
              << ". Aborting resize." << std::endl;
    return;
  }

  for (const auto& resizeOp : cacheResize) {
    auto proxy = m_proxies[resizeOp.m_kName];
    ::grpc::ClientContext context;
    ::holpaca::ResizeRequest request;
    ::holpaca::ResizeResponse response;
    auto deltaSizes = request.mutable_poolsizes();
    for (const auto& poolResize : resizeOp.m_kPoolResizes) {
      ::holpaca::PoolSize poolSize;
      poolSize.set_deltasize(poolResize.m_kDeltaSize);
      *poolSize.mutable_externaldeltasize() = {
          poolResize.m_kExternalDeltaSize.begin(),
          poolResize.m_kExternalDeltaSize.end()};
      (*deltaSizes)[poolResize.m_kId] = poolSize;
    }
    proxy->Resize(&context, request, &response);
  }
}

grpc::Status Controller::Connect(grpc::ServerContext* context,
                                 const ::holpaca::ConnectRequest* request,
                                 ::holpaca::ConnectResponse* response) {
  std::unique_lock<std::shared_timed_mutex> lock(m_proxiesMutex);
  m_proxies[request->cacheaddress()] =
      ::holpaca::Stage::NewStub(grpc::CreateChannel(
          request->cacheaddress(), grpc::InsecureChannelCredentials()));
  std::cout << "Connected to " << request->cacheaddress() << std::endl;

  return grpc::Status::OK;
}

grpc::Status Controller::Disconnect(grpc::ServerContext* context,
                                    const ::holpaca::DisconnectRequest* request,
                                    ::holpaca::DisconnectResponse* response) {
  std::unique_lock<std::shared_timed_mutex> lock(m_proxiesMutex);
  m_proxies.erase(request->cacheaddress());
  return grpc::Status::OK;
}

Controller::Controller(const std::string& kControllerAddress)
    : m_kServer(grpc::ServerBuilder()
                    .AddListeningPort(kControllerAddress,
                                      grpc::InsecureServerCredentials())
                    .RegisterService(
                        static_cast<::holpaca::Controller::Service*>(this))
                    .BuildAndStart()),
      m_serverThread([this] { m_kServer->Wait(); }) {}

Controller::~Controller() {
  for (auto& algorithm : m_controlAlgorithms) {
    algorithm.reset();
  }
  m_stop.exchange(true);
  if (m_kServer != nullptr) {
    m_kServer->Shutdown();
  }
  if (m_serverThread.joinable()) {
    m_serverThread.join();
  }
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
