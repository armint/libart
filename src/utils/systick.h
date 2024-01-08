#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */
typedef int64_t timestamp_t;
typedef uint32_t duration_t;

timestamp_t getSysTick();

void sysTickUpdateHook(timestamp_t sysTick);
#if defined(__cplusplus)
}
#endif /* __cplusplus */
