set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_REFLECTION gRPC::grpc++_reflection)
set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
set(_GRPC_GRPCPP gRPC::grpc++)

if(CMAKE_CROSSCOMPILING)
  find_program(_PROTOBUF_PROTOC protoc)
  find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
  set(_PROTOBUF_PROTOC protobuf::protoc)
  set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
endif()

# Proto + gRPC
get_filename_component(
  HolpacaProtoFile "${CMAKE_CURRENT_LIST_DIR}/protos/Holpaca.proto" ABSOLUTE)
get_filename_component(HolpacaProtoPath ${HolpacaProtoFile} PATH)

# Generated sources
set(HolpacaProtoSources "${HolpacaProtoPath}/Holpaca.pb.cc")
set(HolpacaProtoHeaders "${HolpacaProtoPath}/Holpaca.pb.h")
set(HolpacaGRPCSources "${HolpacaProtoPath}/Holpaca.grpc.pb.cc")
set(HolpacaGRPCHeaders "${HolpacaProtoPath}/Holpaca.grpc.pb.h")

add_custom_command(
  OUTPUT 
    ${HolpacaProtoHeaders} 
    ${HolpacaProtoSources} 
    ${HolpacaGRPCHeaders}
    ${HolpacaGRPCSources}
  COMMAND
    ${_PROTOBUF_PROTOC} 
    ARGS 
      --grpc_out "${HolpacaProtoPath}" 
      --cpp_out  "${HolpacaProtoPath}" 
      -I "${HolpacaProtoPath}"
      --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
    "${HolpacaProtoFile}"
  DEPENDS 
   "${HolpacaProtoFile}"
  COMMENT 
   "Generating gRPC proto sources")

 add_library(cachelib_holpaca_proto 
   ${HolpacaProtoHeaders}
   ${HolpacaProtoSources} 
   ${HolpacaGRPCHeaders} 
   ${HolpacaGRPCSources}
 )

target_link_libraries(cachelib_holpaca_proto 
  PUBLIC 
  ${_REFLECTION} 
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})

add_library(cachelib_holpaca_data
  ./data-plane/CacheAllocator.h
  ./data-plane/CacheAllocatorConfig.h
  ./data-plane/CacheAllocator.cpp
)

target_link_libraries(cachelib_holpaca_data PUBLIC
  cachelib_allocator
  cachelib_holpaca_proto
  Shards::Shards
)

add_executable(cachelib_holpaca_controller
  ./control-plane/Main.cpp
  ./control-plane/Controller.h
  ./control-plane/Controller.cpp
  ./control-plane/ProxyManager.h
  ./control-plane/algorithms/Optimizable.h
  ./control-plane/algorithms/ControlAlgorithm.h
  ./control-plane/algorithms/PerformanceMaximization.h
  ./control-plane/algorithms/PerformanceMaximization.cpp
  ./control-plane/algorithms/MarginalHits.h
  ./control-plane/algorithms/MarginalHits.cpp
  ./control-plane/algorithms/Motivation.h
  ./control-plane/algorithms/Motivation.cpp
)

find_package(GSL REQUIRED)

target_link_libraries(cachelib_holpaca_controller PUBLIC
  cachelib_holpaca_proto
  GSL::gsl
)

install(
  TARGETS cachelib_holpaca_controller
  EXPORT cachelib-exports
  DESTINATION ${BIN_INSTALL_DIR})

install(
  TARGETS cachelib_holpaca_proto cachelib_holpaca_data 
  EXPORT cachelib-exports
  DESTINATION ${LIB_INSTALL_DIR})
