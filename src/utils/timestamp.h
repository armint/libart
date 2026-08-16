#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

typedef int64_t timestamp_t;
typedef uint32_t duration_t;

/**
 * @brief Returns the current timestamp in milliseconds.
 *
 * This function must be implemented by the platform to provide
 * a monotonic millisecond timestamp.
 *
 * @return The current timestamp in milliseconds.
 */
timestamp_t timestamp_in_ms();

#if defined(__cplusplus)
}
#endif /* __cplusplus */
