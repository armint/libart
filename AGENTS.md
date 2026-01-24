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

- Follow the established stream patterns when adding new data handling utilities.
- Use `Writable` streams for formatted output in a memory-efficient way.
- Prefer `std::span` (or equivalent) for passing buffers to avoid size/pointer mismatches.
- Ensure that changes are compatible with the embedded nature of the project (low memory footprint).
- Heap memory usage is not allowed.
- Use etl classes for memory-efficient containers and algorithms.
- Use modern C++ features and best practices as long as they don't interfere with the embedded nature of the project (
  e.g., RAII, const correctness, move semantics, no c-style casts).
- Use meaningful function and variable names that reflect their purpose.
- Use comments to explain complex logic or non-obvious decisions. Explain the purpose and rationale behind the code.
- Use fixed-size integer types (`uint8_t`, `uint32_t`, etc.) for performance and memory efficiency and portability.
