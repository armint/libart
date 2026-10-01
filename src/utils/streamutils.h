
#pragma once
#include "utils/compat/expected.h"
#include <limits>
#include <streams/Readable.h>
#include <streams/Writable.h>

namespace utils {
/**
 * Copies a specified maximum number of bytes from a readable stream to a writable stream.
 *
 * @param from The readable stream to copy data from.
 * @param to The writable stream to copy data to.
 * @param maxCount The maximum number of bytes to copy.
 * @return The number of bytes successfully copied, or an error if the operation fails.
 */
art::expected<size_t, Readable::Error>
copyStream(Readable &from, Writable &to, size_t maxCount = std::numeric_limits<size_t>::max());
} // namespace utils