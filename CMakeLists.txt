
cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(base
    DESCRIPTION "Viam C++ base Module"
    LANGUAGES CXX
)

# Everything needs threads, and prefer -pthread if available
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(viam-cpp-sdk CONFIG REQUIRED COMPONENTS viamsdk)

find_library(LGPIO_LIB NAMES lgpio REQUIRED)

add_executable(base
    src/main.cpp
    src/base.cpp
    src/cleaner.cpp
)

target_include_directories(base PUBLIC src)

target_link_libraries(base
    viam-cpp-sdk::viamsdk
    ${LGPIO_LIB}
)

# Parallel CRTP/template variant. Same Viam module interface, different impl.
add_executable(base_crtp
    src/main_crtp.cpp
    src/base_crtp.cpp
    src/cleaner.cpp
)

target_include_directories(base_crtp PUBLIC src)

target_link_libraries(base_crtp
    viam-cpp-sdk::viamsdk
    ${LGPIO_LIB}
)

install(
    FILES meta.json
    DESTINATION .
)

install(TARGETS base base_crtp)

set(CPACK_PACKAGE_NAME "base")
set(CPACK_PACKAGE_FILE_NAME "module")
set(CPACK_GENERATOR "TGZ")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)
include(CPack)

