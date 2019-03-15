cmake_minimum_required(VERSION 2.8)

set(PACKAGE_NAME      "grpc")
set(PACKAGE_VERSION   "0.1.0")
set(PACKAGE_STRING    "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME   "${PACKAGE_NAME}-${PACKAGE_VERSION}")
project(${PACKAGE_NAME} C CXX)

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

# Providers for third-party dependencies (gRPC_*_PROVIDER properties):
# "module": build the dependency using sources from git submodule (under third_party)
# "package": use cmake's find_package functionality to locate a pre-installed dependency

set(gRPC_ZLIB_PROVIDER "module" CACHE STRING "Provider of zlib library")
set_property(CACHE gRPC_ZLIB_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_CARES_PROVIDER "module" CACHE STRING "Provider of c-ares library")
set_property(CACHE gRPC_CARES_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_SSL_PROVIDER "module" CACHE STRING "Provider of ssl library")
set_property(CACHE gRPC_SSL_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_PROTOBUF_PROVIDER "module" CACHE STRING "Provider of protobuf library")
set_property(CACHE gRPC_PROTOBUF_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_PROTOBUF_PACKAGE_TYPE "" CACHE STRING "Algorithm for searching protobuf package")
set_property(CACHE gRPC_PROTOBUF_PACKAGE_TYPE PROPERTY STRINGS "CONFIG" "MODULE")

set(gRPC_GFLAGS_PROVIDER "module" CACHE STRING "Provider of gflags library")
set_property(CACHE gRPC_GFLAGS_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_BENCHMARK_PROVIDER "module" CACHE STRING "Provider of benchmark library")
set_property(CACHE gRPC_BENCHMARK_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_USE_PROTO_LITE OFF CACHE BOOL "Use the protobuf-lite library")

if(UNIX)
    if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        set(_gRPC_PLATFORM_LINUX ON)
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        set(_gRPC_PLATFORM_MAC ON)
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Android")
        set(_gRPC_PLATFORM_ANDROID ON)
    else()
        set(_gRPC_PLATFORM_POSIX ON)
    endif()
endif()
if(WIN32)
    set(_gRPC_PLATFORM_WINDOWS ON)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

add_definitions(-DPB_FIELD_32BIT)

if (MSVC)
    include(cmake/msvc_static_runtime.cmake)
    add_definitions(-D_WIN32_WINNT=0x600 -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
    # needed to compile protobuf
    add_definitions(/wd4065 /wd4506)
    # TODO(jtattermusch): revisit warnings that were silenced as part of upgrade to protobuf3.6.0
    add_definitions(/wd4200 /wd4291 /wd4244)
    # TODO(jtattermusch): revisit C4267 occurrences throughout the code
    add_definitions(/wd4267)
    # TODO(jtattermusch): needed to build boringssl with VS2017, revisit later
    add_definitions(/wd4987 /wd4774 /wd4819 /wd4996 /wd4619)
endif()

if (gRPC_USE_PROTO_LITE)
    set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf-lite")
    add_definitions("-DGRPC_USE_PROTO_LITE")
else()
    set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf")
endif()

#if(gRPC_BACKWARDS_COMPATIBILITY_MODE)
#    add_definitions(-DGPR_BACKWARDS_COMPATIBILITY_MODE)
#    if (_gRPC_PLATFORM_MAC)
#        # some C++11 constructs not supported before OS X 10.9
#        set(CMAKE_OSX_DEPLOYMENT_TARGET 10.9)
#    endif()
#endif()

if (_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC)
    # C core has C++ source code, but should not depend on libstc++ (for better portability).
    # We need to use a few tricks to convince cmake to do that.
    # https://stackoverflow.com/questions/15058403/how-to-stop-cmake-from-linking-against-libstdc
    set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES "")
    # Exceptions and RTTI must be off to avoid dependency on libstdc++
    set(_gRPC_CORE_NOSTDCXX_FLAGS -fno-exceptions -fno-rtti)
else()
    set(_gRPC_CORE_NOSTDCXX_FLAGS "")
endif()

include(cmake/protobuf.cmake)

if(NOT MSVC)
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -std=c99")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

if(_gRPC_PLATFORM_MAC)
    set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} m pthread)
elseif(_gRPC_PLATFORM_ANDROID)
    set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} m)
elseif(UNIX)
    set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} rt m pthread)
endif()


add_executable(grpc_contract_csharp_plugin
        src/aelf_options.pb.cc
        src/contract_csharp_generator.cc
        src/contract_csharp_plugin.cc
        )


target_include_directories(grpc_contract_csharp_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
        PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
        PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
        PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
        PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
        PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
        PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
        PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
        PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
        PRIVATE ${_gRPC_PROTO_GENS_DIR}
        )

target_link_libraries(grpc_contract_csharp_plugin
        ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
        ${_gRPC_PROTOBUF_LIBRARIES}
        ${_gRPC_ALLTARGETS_LIBRARIES}
        )
