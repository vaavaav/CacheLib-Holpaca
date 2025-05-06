#pragma once

#include <cachelib/allocator/CacheAllocatorConfig.h>

namespace facebook {
namespace cachelib {
namespace holpaca {

template <typename CacheT>
class CacheAllocatorConfig
    : public ::facebook::cachelib::CacheAllocatorConfig<
          ::facebook::cachelib::CacheAllocator<typename CacheT::Trait>> {
  std::string m_address;
  std::string m_controllerAddress;

 public:
  CacheAllocatorConfig& setAddress(std::string address) {
    m_address = address;
    return *this;
  }

  CacheAllocatorConfig& setControllerAddress(std::string controllerAddress) {
    m_controllerAddress = controllerAddress;
    return *this;
  }

  const CacheAllocatorConfig& validate() const {
    if (m_address.empty()) {
      throw std::invalid_argument("Address for this instance must be set");
    }
    if (m_controllerAddress.empty()) {
      throw std::invalid_argument("Controller address must be set");
    }

    ::facebook::cachelib::CacheAllocatorConfig<
        ::facebook::cachelib::CacheAllocator<typename CacheT::Trait>>::
        validate();

    return *this;
  }

  friend CacheT;
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
