#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */
typedef int64_t timestamp_t;
typedef uint32_t duration_t;

/**
 * Returns the current system tick in milliseconds.
 *
 * This function retrieves the system tick count since the start of the system.
 *
 * @return The current system tick count in milliseconds.
 */
timestamp_t getSysTick();

/**
 * @brief Retrieves the current system time in microsecond ticks.
 *
 * This function returns the current value of the system timer
 * that measures time in microsecond ticks, providing high-resolution
 * timing information.
 *
 * @return The current system time in microsecond ticks.
 */
timestamp_t getMicroTicks();

void sysTickUpdateHook(timestamp_t sysTick);
#if defined(__cplusplus)
}
#endif /* __cplusplus */
