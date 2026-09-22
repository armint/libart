#pragma once
#include "FreeRTOS.h"
#include <cstring>

inline QueueHandle_t xQueueCreateStatic(UBaseType_t uxQueueLength, UBaseType_t uxItemSize,
                                         uint8_t *pucQueueStorage, StaticQueue_t *pxQueueBuffer) {
  if (pxQueueBuffer) {
    pxQueueBuffer->storage = pucQueueStorage;
    pxQueueBuffer->itemSize = uxItemSize;
    pxQueueBuffer->length = uxQueueLength;
    pxQueueBuffer->head = 0;
    pxQueueBuffer->count = 0;
  }
  return pxQueueBuffer;
}

inline void vQueueDelete(QueueHandle_t xQueue) { (void)xQueue; }

inline BaseType_t xQueueGenericSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait,
                                     BaseType_t xCopyPosition) {
  (void)xTicksToWait;
  (void)xCopyPosition;
  if (!xQueue || xQueue->count >= xQueue->length) {
    return pdFALSE;
  }
  const size_t tail = (xQueue->head + xQueue->count) % xQueue->length;
  std::memcpy(xQueue->storage + tail * xQueue->itemSize, pvItemToQueue, xQueue->itemSize);
  xQueue->count++;
  return pdTRUE;
}

#define queueSEND_TO_BACK 0
#define queueSEND_TO_FRONT 1

#define xQueueSendToBack(xQueue, pvItemToQueue, xTicksToWait) \
  xQueueGenericSend((xQueue), (pvItemToQueue), (xTicksToWait), queueSEND_TO_BACK)
#define xQueueSend(xQueue, pvItemToQueue, xTicksToWait) \
  xQueueGenericSend((xQueue), (pvItemToQueue), (xTicksToWait), queueSEND_TO_BACK)

inline BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait) {
  (void)xTicksToWait;
  if (!xQueue || xQueue->count == 0) {
    return pdFALSE;
  }
  std::memcpy(pvBuffer, xQueue->storage + xQueue->head * xQueue->itemSize, xQueue->itemSize);
  xQueue->head = (xQueue->head + 1) % xQueue->length;
  xQueue->count--;
  return pdTRUE;
}

inline UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue) {
  return xQueue ? static_cast<UBaseType_t>(xQueue->count) : 0;
}

inline UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue) {
  return xQueue ? static_cast<UBaseType_t>(xQueue->length - xQueue->count) : 0;
}
