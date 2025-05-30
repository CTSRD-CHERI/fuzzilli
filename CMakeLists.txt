cmake_minimum_required(VERSION 3.13)
project(FuzzilliRemoteExecutor C)

set(CMAKE_C_STANDARD 17)

add_library(reprl STATIC
    Sources/libreprl/libreprl-posix.c
    Sources/libreprl/libreprl-windows.c
)
target_include_directories(reprl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources/libreprl/include)

add_library(coverage STATIC
    Sources/libcoverage/coverage.c
)
target_include_directories(coverage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources/libcoverage/include)
target_compile_definitions(coverage PUBLIC REMOTE_EXECUTOR)

add_library(socket STATIC
    Sources/libsocket_remote/socket-posix.c
    Sources/libsocket_remote/socket-win32.c
)
target_include_directories(socket PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources/libsocket_remote/include)

add_executable(executor
    Sources/remote_executor/executor.c
    Sources/remote_executor/reprl_layer.c
    Sources/remote_executor/coverage_layer.c
)
target_include_directories(executor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources/remote_executor/include)
target_link_libraries(executor
    reprl
    coverage
    socket
    pthread
)

# Build a driver to test the remote executor functionality
add_executable(test-executor
    Sources/remote_executor/test-executor.c
)
target_include_directories(test-executor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources/remote_executor/include
)
target_link_libraries(test-executor socket)
