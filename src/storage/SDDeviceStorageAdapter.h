#pragma once

#include "StorageDevice.h"
#include <cstdint>

// Forward declaration - SDDevice is defined in the main project
// This header only provides the interface adaptation

class SDDevice;

/**
 * @brief Adapter that wraps SDDevice to implement the StorageDevice interface
 *
 * This adapter allows the libArT FatFileSystem to work with the legacy SDDevice class
 * by providing the StorageDevice interface that FatFileSystem expects.
 */
class SDDeviceStorageAdapter : public StorageDevice {
public:
    explicit SDDeviceStorageAdapter(SDDevice &device) : _device(device) {}

    bool initialize() override {
        return _device.getStatus() != STA_NOINIT;
    }

    uint8_t getStatus() override {
        return static_cast<uint8_t>(_device.getStatus());
    }

    size_t read(uint8_t *buffer, size_t sector, size_t count) override {
        // SDDevice uses bd_size_t for sector count
        auto result = _device.read(buffer, static_cast<uint32_t>(sector),
                                    static_cast<uint32_t>(count));
        return result >= 0 ? static_cast<size_t>(result) : 0;
    }

    size_t write(const uint8_t *buffer, size_t sector, size_t count) override {
        auto result = _device.write(buffer, static_cast<uint32_t>(sector),
                                     static_cast<uint32_t>(count));
        return result >= 0 ? static_cast<size_t>(result) : 0;
    }

    size_t getSectorCount() override {
        return _device.getSectorCount();
    }

    size_t getEraseSize() override {
        return _device.getEraseSize();
    }

    size_t erase(size_t start, size_t end) override {
        auto result = _device.trim(static_cast<uint32_t>(start),
                                    static_cast<uint32_t>(end - start + 1));
        return result >= 0 ? static_cast<size_t>(result) : 0;
    }

    size_t getBlockSize() const override {
        return _device.getBlockSize();
    }

private:
    SDDevice &_device;
};
