cmake_minimum_required(VERSION 3.10)

project(SOFIA VERSION 0.1)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

set(SOURCES
        main.cpp
        ChordNode.hpp
        Protocol.h
)

add_executable(chord_node ${SOURCES})

find_package(Threads REQUIRED)
target_link_libraries(chord_node PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(chord_node PRIVATE ws2_32)

endif()
