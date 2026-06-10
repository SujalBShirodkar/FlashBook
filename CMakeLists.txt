cmake_minimum_required(VERSION 3.16)
project(FlashBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------
# Compiler flags
#   -O2           : optimise (use -O3 later for benchmarks)
#   -Wall         : all common warnings
#   -Wextra       : extra warnings
#   -march=native : allow CPU-specific instructions (AVX etc.)
#   -g            : debug symbols (remove in final benchmark)
# ---------------------------------------------------------
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra -march=native -g")

# Shared include path for all targets
include_directories(${PROJECT_SOURCE_DIR}/include)

# ---------------------------------------------------------
# Main receiver binary
# ---------------------------------------------------------
add_executable(flashbook
    src/main.cpp
    src/udp_receiver.cpp
)

# Link pthreads — needed for std::thread on Linux
find_package(Threads REQUIRED)
target_link_libraries(flashbook PRIVATE Threads::Threads)

# ---------------------------------------------------------
# order_sender tool
# ---------------------------------------------------------
add_executable(order_sender
    tools/order_sender.cpp
)

# ---------------------------------------------------------
# test_book — interactive order book CLI harness
# ---------------------------------------------------------
add_executable(test_book
    tools/test_book.cpp
    src/order_book.cpp
)

# ---------------------------------------------------------
# test_pool — memory pool correctness + benchmark
# ---------------------------------------------------------
add_executable(test_pool
    tools/test_pool.cpp
)

# ---------------------------------------------------------
# Installation (optional — just run from build/ dir)
# ---------------------------------------------------------
install(TARGETS flashbook order_sender DESTINATION bin)