cmake_minimum_required(VERSION 3.13)
project(FuzzilliRemoteExecutor C)

set(CMAKE_C_STANDARD 17)

add_library(reprl STATIC
    Sources/libreprl/libreprl-posix.c
    Sources/libreprl/libreprl-windows.c
)
target_include_directories(reprl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources/libreprl/include)

add_library(socket STATIC
    Sources/libsocket/socket-posix.c
    Sources/libsocket/socket-win32.c
)
target_include_directories(socket PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources/libsocket/include)
target_compile_definitions(socket PRIVATE REMOTE_EXECUTOR)

add_executable(executor
    Sources/remote_executor/executor.c
)
target_include_directories(executor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources/remote_executor/include)
target_link_libraries(executor
    reprl
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
