#pragma once
#include "FreeRTOS.h"

inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *pxMutexBuffer) {
  if (pxMutexBuffer) {
    pxMutexBuffer->isMutex = true;
    pxMutexBuffer->count = 1;
    pxMutexBuffer->maxCount = 1;
  }
  return pxMutexBuffer;
}

inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *pxStaticSemaphore) {
  if (pxStaticSemaphore) {
    pxStaticSemaphore->isMutex = false;
    pxStaticSemaphore->count = 0;
    pxStaticSemaphore->maxCount = 1;
  }
  return pxStaticSemaphore;
}

inline SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount,
                                                       StaticSemaphore_t *pxStaticSemaphore) {
  if (pxStaticSemaphore) {
    pxStaticSemaphore->isMutex = false;
    pxStaticSemaphore->count = uxInitialCount;
    pxStaticSemaphore->maxCount = uxMaxCount;
  }
  return pxStaticSemaphore;
}

inline void vSemaphoreDelete(SemaphoreHandle_t xSemaphore) { (void)xSemaphore; }

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xBlockTime) {
  (void)xBlockTime;
  if (!xSemaphore || xSemaphore->count == 0) {
    return pdFALSE;
  }
  xSemaphore->count--;
  return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
  if (!xSemaphore || xSemaphore->count >= xSemaphore->maxCount) {
    return pdFALSE;
  }
  xSemaphore->count++;
  return pdTRUE;
}

inline BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t xSemaphore, BaseType_t *pxHigherPriorityTaskWoken) {
  if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
  return xSemaphoreTake(xSemaphore, 0);
}

inline BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore, BaseType_t *pxHigherPriorityTaskWoken) {
  if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
  return xSemaphoreGive(xSemaphore);
}

