cmake_minimum_required(VERSION 3.16)
project(FlashBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra -march=native -g")

include_directories(${PROJECT_SOURCE_DIR}/include)

add_executable(flashbook
    src/main.cpp
    src/udp_receiver.cpp
)

add_executable(order_sender
    tools/order_sender.cpp
)

install(TARGETS flashbook order_sender DESTINATION bin)