cmake_minimum_required(VERSION 3.10)

project(SOFIA VERSION 0.1)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Optional: Warnungen einschalten (wichtig für C++ auf SPS!)
if(MSVC)
    add_compile_options(/W4) # Windows Visual Studio Warnungen
else()
    add_compile_options(-Wall -Wextra -pedantic) # GCC/Clang Warnungen
endif()

# --- QUELLE ---
# Definiere deine Quellcode-Dateien
set(SOURCES
        main.cpp
        ChordNode.hpp
        Protocol.h
)

# Erstelle das ausführbare Programm
add_executable(chord_node ${SOURCES})

# --- ABHÄNGIGKEITEN ---
# 1. Threads (für std::this_thread::sleep_for)
# CMake findet automatisch die richtige Library (-lpthread auf Linux, nix auf Windows)
find_package(Threads REQUIRED)
target_link_libraries(chord_node PRIVATE Threads::Threads)

# 2. Sockets (Windows spezifisch)
# Auf Linux sind Sockets im Standard, auf Windows braucht man "ws2_32"
if(WIN32)
    target_link_libraries(chord_node PRIVATE ws2_32)
endif()