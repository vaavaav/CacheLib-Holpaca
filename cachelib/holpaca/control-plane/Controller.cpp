#include <cachelib/holpaca/control-plane/Controller.h>

namespace facebook {
namespace cachelib {
namespace holpaca {
Controller::Controller(std::string address) {
  m_server =
      ::grpc::ServerBuilder()
          .AddListeningPort(address, ::grpc::InsecureServerCredentials())
          .RegisterService(dynamic_cast<::holpaca::Controller::Service*>(this))
          .BuildAndStart();
  if (m_server == nullptr) {
    throw std::runtime_error("Failed to start the server");
  }
  m_serverThread = std::thread([this] { m_server->Wait(); });
  m_cleanerThread = std::thread([this] {
    while (!m_stop) {
      {
        std::unique_lock<std::shared_timed_mutex> lock(m_mutex);
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        for (auto it = m_proxies.begin(); it != m_proxies.end();) {
          if (it->second->isAlive(now)) {
            ++it;
          } else {
            std::cout << "Removing proxy " << it->first << std::endl; // DEBUG
            it = m_proxies.erase(it);
          }
        }
      }
      std::this_thread::sleep_for(s_kCleanerPeriodicity);
    }
  });
}

Controller::~Controller() {
  m_stop.exchange(true);
  for (auto& algorithm : m_controlAlgorithms) {
    algorithm.reset();
  }
  m_server->Shutdown();
  m_serverThread.join();
  m_cleanerThread.join();
}

grpc::Status Controller::KeepAlive(grpc::ServerContext* context,
                                   const ::holpaca::KeepAliveRequest* request,
                                   ::holpaca::KeepAliveResponse* response) {
  std::unique_lock<std::shared_timed_mutex> lock(m_mutex);
  std::string address = request->address();
  std::cout << "Received keep alive from " << address << std::endl; // DEBUG
  auto now = std::chrono::steady_clock::now().time_since_epoch();
  if (auto it = m_proxies.insert(
          {address, std::make_shared<CacheProxy>(address, now)});
      !it.second) {
    it.first->second->keepAlive(now);
  }
  return grpc::Status::OK;
}

std::unordered_map<std::string, std::shared_ptr<CacheProxy>>
Controller::getCaches() {
  std::shared_lock<std::shared_timed_mutex> lock(m_mutex);
  std::unordered_map<std::string, std::shared_ptr<CacheProxy>> caches;
  auto now = std::chrono::steady_clock::now().time_since_epoch();
  for (const auto& [address, proxy] : m_proxies) {
    if (proxy->isAlive(now)) {
      caches[address] = proxy;
    }
  }
  return caches;
}

// TODO: confirmar que isto é o comportamento correto
std::shared_ptr<CacheProxy> Controller::getCache(const std::string& address) {
  std::shared_lock<std::shared_timed_mutex> lock(m_mutex);
  auto now = std::chrono::steady_clock::now().time_since_epoch();
  if (auto it = m_proxies.find(address); it != m_proxies.end()) {
    if (it->second->isAlive(now)) {
      return it->second;
    }
  }
  return nullptr;
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
