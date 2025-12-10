#pragma once
#include <cstdint>
#if defined(LIBART_ENABLE_64BIT)
using big_int_t = int64_t;
using big_uint_t = uint64_t;
#else
using big_int_t = int32_t;
using big_uint_t = uint32_t;
#endif