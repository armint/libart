# Agent Instructions for libArT

## Purpose
`libArT` is a core utility library designed for embedded systems, providing abstractions for common tasks. Target systems are cortex-m or risc-v microcontrollers running bare-metal or rtos-based (freertos or µOS++ IIIe).

## Core Components
- **Streams**: `Readable` and `Writable` interfaces for various data sources/sinks (Array, String, Digest, etc.).
- **Logging**: A flexible logging system (`LogManager`, `Logger`).
- **Modbus**: Implementations for Modbus Master and Slave.
- **Settings**: A framework for managing device settings.
- **Utils**: JSON reading/writing, CRC (Digest), string utilities, and more.

## Guidelines
- Follow the established stream patterns when adding new data handling utilities.
- Use `tfp_printf` for formatted output in a memory-efficient way.
- Prefer `std::span` (or equivalent) for passing buffers to avoid size/pointer mismatches.
- Ensure that changes are compatible with the embedded nature of the project (low memory footprint).
- Heap memory usage is not allowed.
