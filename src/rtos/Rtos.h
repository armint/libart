#pragma once

#include <FreeRTOS.h>

#if defined(configSUPPORT_STATIC_ALLOCATION)
static_assert(configSUPPORT_STATIC_ALLOCATION == 1,
              "art::rtos requires configSUPPORT_STATIC_ALLOCATION == 1 in FreeRTOSConfig.h");
#endif

#include "CriticalSection.h"
#include "EventFlags.h"
#include "Mutex.h"
#include "Semaphore.h"
#include "SysClock.h"
#include "Thread.h"
#include "Timer.h"
