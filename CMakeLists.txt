cmake_minimum_required(VERSION 3.20)
project(lanman VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

include(FetchContent)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.1
  GIT_SHALLOW TRUE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE httplib::httplib)
if(WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 iphlpapi)
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION="${PROJECT_VERSION}")

# Binaires autonomes : pas de dépendance au runtime C++ sur la machine cible
if(MSVC)
  set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
elseif(WIN32)
  target_link_options(${PROJECT_NAME} PRIVATE -static)
elseif(UNIX AND NOT APPLE)
  option(LANMAN_FULLY_STATIC "Binaire 100% statique (a utiliser avec musl/Alpine)" OFF)
  if(LANMAN_FULLY_STATIC)
    target_link_options(${PROJECT_NAME} PRIVATE -static)
  else()
    target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++ -static-libgcc)
  endif()
endif()

if(NOT MSVC AND NOT APPLE)
  target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:MinSizeRel,Release>:-s>)
endif()

if(APPLE)
  target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:MinSizeRel,Release>:-Wl,-dead_strip,-x>)
endif()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
