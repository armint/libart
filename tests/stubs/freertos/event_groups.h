#pragma once
#include "FreeRTOS.h"

inline EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *pxEventGroupBuffer) {
   if(pxEventGroupBuffer) {
      pxEventGroupBuffer->bits = 0;
   }
   return pxEventGroupBuffer;
}

inline void vEventGroupDelete(EventGroupHandle_t xEventGroup) {
   (void)xEventGroup;
}

inline EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet) {
   if(xEventGroup) {
      xEventGroup->bits |= uxBitsToSet;
      return xEventGroup->bits;
   }
   return 0;
}

inline EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear) {
   if(xEventGroup) {
      EventBits_t prev = xEventGroup->bits;
      xEventGroup->bits &= ~uxBitsToClear;
      return prev;
   }
   return 0;
}

inline EventBits_t xEventGroupGetBits(EventGroupHandle_t xEventGroup) {
   return xEventGroup ? xEventGroup->bits : 0;
}

inline BaseType_t xEventGroupSetBitsFromISR(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet, BaseType_t *pxHigherPriorityTaskWoken) {
   if(pxHigherPriorityTaskWoken)
      *pxHigherPriorityTaskWoken = pdFALSE;
   xEventGroupSetBits(xEventGroup, uxBitsToSet);
   return pdPASS;
}

inline BaseType_t xEventGroupClearBitsFromISR(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear) {
   xEventGroupClearBits(xEventGroup, uxBitsToClear);
   return pdPASS;
}

inline EventBits_t xEventGroupGetBitsFromISR(EventGroupHandle_t xEventGroup) {
   return xEventGroupGetBits(xEventGroup);
}

inline EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                       const EventBits_t uxBitsToWaitFor,
                                       const BaseType_t xClearOnExit,
                                       const BaseType_t xWaitForAllBits,
                                       TickType_t xTicksToWait) {
   (void)xTicksToWait;
   if(!xEventGroup)
      return 0;

   bool matched = false;
   if(xWaitForAllBits == pdTRUE) {
      matched = (xEventGroup->bits & uxBitsToWaitFor) == uxBitsToWaitFor;
   } else {
      matched = (xEventGroup->bits & uxBitsToWaitFor) != 0;
   }

   if(matched) {
      EventBits_t res = xEventGroup->bits & uxBitsToWaitFor;
      if(xClearOnExit == pdTRUE) {
         xEventGroup->bits &= ~uxBitsToWaitFor;
      }
      return res;
   }
   return 0;
}
