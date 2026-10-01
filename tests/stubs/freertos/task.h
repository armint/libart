#pragma once
#include "FreeRTOS.h"

inline TickType_t g_mockTickCount = 1000;

inline TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode,
                                      const char *const pcName,
                                      const uint32_t ulStackDepth,
                                      void *const pvParameters,
                                      UBaseType_t uxPriority,
                                      StackType_t *const puxStackBuffer,
                                      StaticTask_t *const pxTaskBuffer) {
   (void)pxTaskCode;
   (void)pcName;
   (void)ulStackDepth;
   (void)pvParameters;
   (void)puxStackBuffer;
   if(pxTaskBuffer) {
      pxTaskBuffer->priority = uxPriority;
      pxTaskBuffer->suspended = false;
      pxTaskBuffer->deleted = false;
   }
   return pxTaskBuffer;
}

inline void vTaskDelete(TaskHandle_t xTaskToDelete) {
   if(xTaskToDelete) {
      xTaskToDelete->deleted = true;
   }
}

inline void vTaskSuspend(TaskHandle_t xTaskToSuspend) {
   if(xTaskToSuspend) {
      xTaskToSuspend->suspended = true;
   }
}

inline void vTaskResume(TaskHandle_t xTaskToResume) {
   if(xTaskToResume) {
      xTaskToResume->suspended = false;
   }
}

inline UBaseType_t uxTaskPriorityGet(TaskHandle_t xTask) {
   return xTask ? xTask->priority : 0;
}

inline void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority) {
   if(xTask) {
      xTask->priority = uxNewPriority;
   }
}

inline void vTaskDelay(const TickType_t xTicksToDelay) {
   g_mockTickCount += xTicksToDelay;
}

inline BaseType_t xTaskDelayUntil(TickType_t *const pxPreviousWakeTime, const TickType_t xTimeIncrement) {
   if(pxPreviousWakeTime) {
      *pxPreviousWakeTime += xTimeIncrement;
      g_mockTickCount = *pxPreviousWakeTime;
   }
   return pdTRUE;
}

inline TickType_t xTaskGetTickCount() {
   return g_mockTickCount;
}

inline void taskENTER_CRITICAL() {
}
inline void taskEXIT_CRITICAL() {
}
inline UBaseType_t taskENTER_CRITICAL_FROM_ISR() {
   return 0;
}
inline void taskEXIT_CRITICAL_FROM_ISR(UBaseType_t uxSavedInterruptStatus) {
   (void)uxSavedInterruptStatus;
}
inline void vTaskSuspendAll() {
}
inline BaseType_t xTaskResumeAll() {
   return pdTRUE;
}
