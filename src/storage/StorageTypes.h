#pragma once

#include <cstdint>

namespace art::storage {

/**
 * @brief Byte offset or byte count on a storage medium or in a file.
 *
 * 64 bits wide so that SD cards and files above 4 GB remain addressable. It lives in its own header so that
 * the file layer and the stream adapters can use it without depending on the block device interface.
 */
using offset_t = uint64_t;

} // namespace art::storage
