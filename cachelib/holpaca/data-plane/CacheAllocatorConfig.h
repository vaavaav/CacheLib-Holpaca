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
  int64_t m_virtualSize;
  bool m_hasVirtualSize{false};
  double proportion{1.0};

 public:
  CacheAllocatorConfig& setAddress(std::string address) {
    m_address = address;
    return *this;
  }

  CacheAllocatorConfig& setControllerAddress(std::string address) {
    m_controllerAddress = address;
    return *this;
  }

  // Below are things for control algorithms

  CacheAllocatorConfig& setVirtualSize(uint64_t size) {
    m_hasVirtualSize = true;
    m_virtualSize = size;
    return *this;
  }

  CacheAllocatorConfig& setProportion(double proportion) {
    this->proportion = proportion;
    return *this;
  }

  friend CacheT;
};
} // namespace holpaca
} // namespace cachelib
} // namespace facebook
