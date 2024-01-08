
#pragma once
#include <expected>
#include <limits>
#include <streams/Readable.h>
#include <streams/Writable.h>

namespace utils {
std::expected<size_t, Readable::Error>
copyStream(Readable &from, Writable &to, size_t maxCount = std::numeric_limits<size_t>::max());
}