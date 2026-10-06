#pragma once

// A library that uses span-compat only, as system-stm32 does.
#include <span-compat/span.h>

#include <cstddef>
#include <cstdint>

size_t halChecksum(span_compat::span<const uint8_t> data);
