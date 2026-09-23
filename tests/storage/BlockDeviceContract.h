#pragma once

#include "storage/BlockDevice.h"
#include <cstdint>
#include <expected>
#include <optional>

namespace art::storage::test {

/// Erased cell value of NOR flash, and of RamBlockDevice.
inline constexpr uint8_t kNorErasedByte = 0xFFu;

/**
 * @brief Reusable contract suite for any BlockDevice: a real driver, a decorator or a transform device.
 *
 * Call it from a doctest test case. It initialises the device, overwrites its first part (the checked window,
 * a few hundred bytes to a few erase units) and checks:
 * - the geometry passes Geometry::isValid() and does not change when init() is called again;
 * - program/read round trip at every programSize-aligned offset and length up to three units, with the
 *   neighbouring bytes untouched;
 * - erase removes programmed data, is deterministic, is confined to the requested range, and (if
 *   `erasedByte` is set) leaves that byte pattern;
 * - overwriting behaves as Geometry::eraseBeforeProgram claims: without erase it must overwrite if the flag is
 *   false. If the flag is true the outcome is unspecified by BlockDevice, so only the erase-then-program cycle
 *   is checked;
 * - a repeated init() keeps the data, and sync() succeeds;
 * - out-of-range access (including offset + length overflow) and misaligned access are rejected with
 *   Error::InvalidArgument, and a rejected program or erase changes nothing.
 *
 * @param device Device under test. It is used exclusively and its content is destroyed.
 * @param erasedByte Value every cell must read back after erase(), or std::nullopt for a device whose erased
 * pattern is not observable (a transform device: erased flash decrypts to noise).
 */
void checkBlockDeviceContract(BlockDevice &device, std::optional<uint8_t> erasedByte = kNorErasedByte);

/// true if `result` holds exactly the given error. Avoids doctest having to stringify the enum class.
inline bool failedWith(const std::expected<void, Error> &result, Error error) {
   return !result.has_value() && result.error() == error;
}

} // namespace art::storage::test
