#include <cachelib/holpaca/control-plane/Controller.h>

namespace facebook {
namespace cachelib {
namespace holpaca {
Controller::Controller(const std::string& kCacheAddress,
                       CacheProxy::CommunicationType type)
    : m_kProxy(std::make_shared<CacheProxy>(kCacheAddress, type)) {}

Controller::~Controller() {
  for (auto& algorithm : m_controlAlgorithms) {
    algorithm.reset();
  }
}

} // namespace holpaca
} // namespace cachelib
} // namespace facebook
