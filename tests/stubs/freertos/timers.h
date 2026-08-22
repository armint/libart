#pragma once
#include "FreeRTOS.h"

inline TimerHandle_t xTimerCreateStatic(const char * const pcTimerName, const TickType_t xTimerPeriodInTicks,
                                        const UBaseType_t uxAutoReload, void * const pvTimerID,
                                        TimerCallbackFunction_t pxCallbackFunction,
                                        StaticTimer_t *pxTimerBuffer) {
  if (pxTimerBuffer) {
    pxTimerBuffer->name = pcTimerName;
    pxTimerBuffer->period = xTimerPeriodInTicks;
    pxTimerBuffer->autoReload = (uxAutoReload == pdTRUE);
    pxTimerBuffer->timerId = pvTimerID;
    pxTimerBuffer->callback = reinterpret_cast<void (*)(void*)>(pxCallbackFunction);
    pxTimerBuffer->active = false;
  }
  return pxTimerBuffer;
}

inline BaseType_t xTimerDelete(TimerHandle_t xTimer, TickType_t xTicksToWait) {
  (void)xTicksToWait;
  if (xTimer) {
    xTimer->active = false;
  }
  return pdPASS;
}

inline BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait) {
  (void)xTicksToWait;
  if (xTimer) {
    xTimer->active = true;
  }
  return pdPASS;
}

inline BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait) {
  (void)xTicksToWait;
  if (xTimer) {
    xTimer->active = false;
  }
  return pdPASS;
}

inline BaseType_t xTimerReset(TimerHandle_t xTimer, TickType_t xTicksToWait) {
  (void)xTicksToWait;
  if (xTimer) {
    xTimer->active = true;
  }
  return pdPASS;
}

inline BaseType_t xTimerChangePeriod(TimerHandle_t xTimer, TickType_t xNewPeriod, TickType_t xTicksToWait) {
  (void)xTicksToWait;
  if (xTimer) {
    xTimer->period = xNewPeriod;
  }
  return pdPASS;
}

inline BaseType_t xTimerStartFromISR(TimerHandle_t xTimer, BaseType_t *pxHigherPriorityTaskWoken) {
  if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
  return xTimerStart(xTimer, 0);
}

inline BaseType_t xTimerStopFromISR(TimerHandle_t xTimer, BaseType_t *pxHigherPriorityTaskWoken) {
  if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
  return xTimerStop(xTimer, 0);
}

inline BaseType_t xTimerResetFromISR(TimerHandle_t xTimer, BaseType_t *pxHigherPriorityTaskWoken) {
  if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
  return xTimerReset(xTimer, 0);
}

inline BaseType_t xTimerIsTimerActive(TimerHandle_t xTimer) {
  return (xTimer && xTimer->active) ? pdTRUE : pdFALSE;
}

inline void* pvTimerGetTimerID(const TimerHandle_t xTimer) {
  return xTimer ? xTimer->timerId : nullptr;
}

inline void vTimerSetTimerID(TimerHandle_t xTimer, void *pvNewID) {
  if (xTimer) {
    xTimer->timerId = pvNewID;
  }
}
