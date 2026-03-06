# Agent Instructions for libArT

## Purpose

`libArT` is a core utility library designed for embedded systems, providing abstractions for common tasks. Target
systems are cortex-m or risc-v microcontrollers running bare-metal or rtos-based (freertos or µOS++ IIIe).

## Core Components
- **Streams**: `Readable` and `Writable` interfaces for various data sources/sinks (Array, String, Digest, etc.).
- **Logging**: A flexible logging system (`LogManager`, `Logger`).
- **Modbus**: Implementations for Modbus Master and Slave.
- **Settings**: A framework for managing device settings.
- **Utils**: JSON reading/writing, CRC (Digest), string utilities, and more.

## Guidelines

- **Language**: C++20 (or newer). Use modern C++ features and best practices as long as they don't interfere with the embedded nature of the project (e.g., const correctness, move semantics, no c-style casts).
- **Style & Linting**: Follow the established stream patterns mechanically when adding new data handling utilities. Before completing tasks, format code using the provided CMake target: `cmake --build <build_dir> --target clangformat`.
- **Resource Management**: Use RAII for all resource management. Use `Writable` streams for formatted output in a memory-efficient way. Prefer `std::span` (or equivalent) for passing buffers to avoid size/pointer mismatches.
- **Memory**: Heap memory usage is strictly prohibited. Ensure that changes are compatible with the embedded nature of the project (low memory footprint).
- **Dependencies**: The `embedded template library` (ETL) is a dependency and should be considered before creating similar classes. Use etl classes for memory-efficient containers and algorithms.
- **Conventions**: Always use meaningful and descriptive function and variable names that reflect their purpose. Use fixed-size integer types (`uint8_t`, `uint32_t`, etc.) for performance, memory efficiency, and portability. Document any new functions or changes with clear comments. Use comments to explain complex logic or non-obvious decisions.
