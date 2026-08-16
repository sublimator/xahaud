cmake_minimum_required (VERSION 3.16)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()

# Fix "unrecognized escape" issues when passing CMAKE_MODULE_PATH on Windows.
file(TO_CMAKE_PATH "${CMAKE_MODULE_PATH}" CMAKE_MODULE_PATH)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

project (xrpl)
set(Boost_NO_BOOST_CMAKE ON)

find_package(Git QUIET)

# make SOURCE_ROOT_PATH define available for logging
set(SOURCE_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/")
add_definitions(-DSOURCE_ROOT_PATH="${SOURCE_ROOT_PATH}")

# BEAST_ENHANCED_LOGGING - adds file:line numbers and formatting to logs
# Automatically enabled for Debug builds via generator expression
# Can be explicitly controlled with -DBEAST_ENHANCED_LOGGING=ON/OFF
option(BEAST_ENHANCED_LOGGING "Include file and line numbers in log messages (auto: Debug=ON, Release=OFF)" OFF)
message(STATUS "BEAST_ENHANCED_LOGGING option: ${BEAST_ENHANCED_LOGGING}")

if(thread_safety_analysis)
  add_compile_options(-Wthread-safety -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -DRIPPLE_ENABLE_THREAD_SAFETY_ANNOTATIONS)
  add_compile_options("-stdlib=libc++")
  add_link_options("-stdlib=libc++")
endif()

option(USE_CONAN "Use Conan package manager for dependencies" OFF)
# Then, auto-detect if conan_toolchain.cmake is being used
if(CMAKE_TOOLCHAIN_FILE)
  # Check if the toolchain file path contains "conan_toolchain"
  if(CMAKE_TOOLCHAIN_FILE MATCHES "conan_toolchain")
    set(USE_CONAN ON CACHE BOOL "Using Conan detected from toolchain file" FORCE)
    message(STATUS "Conan toolchain detected: ${CMAKE_TOOLCHAIN_FILE}")
    message(STATUS "Building with Conan dependencies")
  endif()
endif()

include (CheckCXXCompilerFlag)
include (FetchContent)
include (ExternalProject)
include (CMakeFuncs) # must come *after* ExternalProject b/c it overrides one function in EP
if (target)
  message (FATAL_ERROR "The target option has been removed - use native cmake options to control build")
endif ()

include(RippledSanity)
include(RippledVersion)
include(RippledSettings)

# this check has to remain in the top-level cmake
# because of the early return statement
if (packages_only)
  if (NOT TARGET rpm)
    message (FATAL_ERROR "packages_only requested, but targets were not created - is docker installed?")
  endif()
  return ()
endif ()
include(RippledCompiler)
include(RippledInterface)

include(deps/Boost)
find_package(OpenSSL 1.1.1 REQUIRED)
set_target_properties(OpenSSL::SSL PROPERTIES
  INTERFACE_COMPILE_DEFINITIONS OPENSSL_NO_SSL2
)
set(SECP256K1_INSTALL TRUE)
add_subdirectory(external/secp256k1)
add_library(secp256k1::secp256k1 ALIAS secp256k1)
add_subdirectory(external/ed25519-donna)
add_subdirectory(external/antithesis-sdk)
find_package(gRPC REQUIRED)
find_package(lz4 REQUIRED)
# Target names with :: are not allowed in a generator expression.
# We need to pull the include directories and imported location properties
# from separate targets.
find_package(LibArchive REQUIRED)
find_package(SOCI REQUIRED)
find_package(SQLite3 REQUIRED)

option(rocksdb "Enable RocksDB" ON)
if(rocksdb)
  find_package(RocksDB REQUIRED)
  set_target_properties(RocksDB::rocksdb PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS RIPPLE_ROCKSDB_AVAILABLE=1
  )
  target_link_libraries(ripple_libs INTERFACE RocksDB::rocksdb)
endif()

find_package(nudb REQUIRED)
find_package(date REQUIRED)
find_package(xxHash REQUIRED)
find_package(magic_enum REQUIRED)

include(deps/WasmEdge) 
  if(TARGET nudb::core)
  set(nudb nudb::core)
elseif(TARGET NuDB::nudb)
  set(nudb NuDB::nudb)
else()
  message(FATAL_ERROR "unknown nudb target")
endif()
target_link_libraries(ripple_libs INTERFACE ${nudb})

target_link_libraries(ripple_libs INTERFACE
  ed25519::ed25519
  LibArchive::LibArchive
  lz4::lz4
  OpenSSL::Crypto
  OpenSSL::SSL
  secp256k1::secp256k1
  soci::soci
  SQLite::SQLite3
)

if(coverage)
  include(RippledCov)
endif()

set(PROJECT_EXPORT_SET RippleExports)
include(RippledCore)

# Refresh Git provenance whenever a target backed by libxrpl is requested.
# The script writes only when values change, so an unchanged build remains a
# no-op while commits, branch switches, and dirty/clean transitions rebuild
# BuildInfo.cpp without requiring a CMake reconfigure. This also works when
# .git is a worktree indirection file rather than a directory.
set(XRPL_GIT_PROVENANCE_HEADER
  "${CMAKE_CURRENT_BINARY_DIR}/generated/xrpl/protocol/BuildInfoGenerated.h"
)
add_custom_target(xrpl_git_provenance
  COMMAND "${CMAKE_COMMAND}"
          "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
          "-DOUTPUT_FILE=${XRPL_GIT_PROVENANCE_HEADER}"
          "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateGitProvenance.cmake"
  BYPRODUCTS "${XRPL_GIT_PROVENANCE_HEADER}"
  COMMENT "Refreshing Git build provenance"
  VERBATIM
)
add_dependencies(xrpl.libxrpl.protocol xrpl_git_provenance)
set_property(SOURCE src/libxrpl/protocol/BuildInfo.cpp APPEND PROPERTY
  OBJECT_DEPENDS "${XRPL_GIT_PROVENANCE_HEADER}"
)
target_include_directories(xrpl.libxrpl.protocol PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

# Optional local differential-testing oracle for hooks-testing. This target is
# excluded from the normal build and provides a small, supported C ABI so
# Python can load real Xahau arithmetic with ctypes. The local dylib may still
# expose transitive libxrpl symbols; only hookz_xahaud_* is supported.
add_library(hookz_xahaud_oracle SHARED EXCLUDE_FROM_ALL
  tools/hookz-oracle/HookzXahaudOracle.cpp
)
target_link_libraries(hookz_xahaud_oracle PRIVATE xrpl.libxrpl)
target_include_directories(hookz_xahaud_oracle PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
set_target_properties(hookz_xahaud_oracle PROPERTIES
  OUTPUT_NAME hookz_xahaud_oracle
)

# Optional host codec CLI. EXCLUDE_FROM_ALL: not part of default rippled.
set(CODEC_SOURCE_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/GeneratedCodecSource.cpp")
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  set(XAHAU_CODEC_PYTHON "${Python3_EXECUTABLE}")
else()
  set(XAHAU_CODEC_PYTHON python3)
endif()
add_custom_target(xahau_codec_source
  COMMAND "${XAHAU_CODEC_PYTHON}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/xahau-codec/projected-source/extract_codec_source.py"
          --generate-cpp "${CODEC_SOURCE_GENERATED}"
  BYPRODUCTS "${CODEC_SOURCE_GENERATED}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/xahau-codec/projected-source/extract_codec_source.py"
  COMMENT "Refreshing projected codec source documentation"
  VERBATIM
)
set_source_files_properties("${CODEC_SOURCE_GENERATED}" PROPERTIES GENERATED TRUE)

add_executable(xahau-codec EXCLUDE_FROM_ALL
  tools/xahau-codec/main.cpp
  "${CODEC_SOURCE_GENERATED}"
)
add_dependencies(xahau-codec xahau_codec_source)
target_link_libraries(xahau-codec PRIVATE xrpl.libxrpl)
set_target_properties(xahau-codec PROPERTIES
  OUTPUT_NAME xahau-codec
)

add_custom_target(xahau-codec-test
  COMMAND "${XAHAU_CODEC_PYTHON}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/xahau-codec/test_cli.py"
          --binary "$<TARGET_FILE:xahau-codec>"
          --rippled "$<TARGET_FILE:rippled>"
          --repo-root "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS xahau-codec rippled
  USES_TERMINAL
  VERBATIM
)

add_custom_target(hookz_xahaud_oracle_test
  COMMAND "${XAHAU_CODEC_PYTHON}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/hookz-oracle/test_oracle.py"
          --library "$<TARGET_FILE:hookz_xahaud_oracle>"
          --repo-root "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS hookz_xahaud_oracle
  USES_TERMINAL
  VERBATIM
)

add_custom_target(hookz_float_vectors_test
  COMMAND "${XAHAU_CODEC_PYTHON}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/hookz-oracle/test_vectors.py"
          --binary "$<TARGET_FILE:rippled>"
          --repo-root "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS rippled
  USES_TERMINAL
  VERBATIM
)

include(RippledInstall)
include(RippledDocs)
include(RippledValidatorKeys)
