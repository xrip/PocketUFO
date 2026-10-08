cmake_minimum_required(VERSION 3.25)

# PocketUFO Win64 recreation.
# Source of truth: pufobeta.exe.i64 (WinCE/ARM32). See docs/idb/MAP.md.
# Module layout mirrors the IDB subsystem map (docs/idb/inventory-grouped.tsv).

project(pocketufo C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "pocketufo targets Win64 only (original is WinCE/ARM32).")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "pocketufo requires a 64-bit toolchain (Win64).")
endif()

enable_testing()

# Parity rule: the original was compiled with warnings enabled. Treat new
# warnings as errors so recreation drift is caught at build time.
add_library(pufo_options INTERFACE)
target_compile_options(pufo_options INTERFACE
    $<$<C_COMPILER_ID:Clang,AppleClang,MSVC>:/W4;/WX>
)

add_subdirectory(src/pufo)
