#pragma once

#include <FreeRTOS.h>
#include <cstdint>
#include <task.h>

namespace art::rtos {

/**
 * @brief System clock and task delay utilities.
 */
struct sysclock {
   /**
    * @brief Current monotonic time in milliseconds since scheduler start.
    */
   static uint64_t now() {
      return static_cast<uint64_t>(xTaskGetTickCount()) * 1000ULL / configTICK_RATE_HZ;
   }

   /**
    * @brief Current monotonic time in FreeRTOS ticks.
    */
   static TickType_t now_ticks() {
      return xTaskGetTickCount();
   }

   /**
    * @brief Delays the calling task for the specified duration in milliseconds.
    *
    * If ms > 0 and pdMS_TO_TICKS(ms) resolves to 0, delays for at least 1 tick.
    */
   static void sleep_for(uint32_t ms) {
      TickType_t ticks = pdMS_TO_TICKS(ms);
      if(ticks == 0 && ms > 0) {
         ticks = 1;
      }
      vTaskDelay(ticks);
   }

   /**
    * @brief Delays the calling task for the specified duration in ticks.
    */
   static void sleep_for_ticks(TickType_t ticks) {
      vTaskDelay(ticks);
   }

   /**
    * @brief Delays the calling task until a periodic target time without drift.
    *
    * @param previousWakeTimeTicks Reference to variable holding previous wake time in ticks.
    * @param periodTicks Time increment in ticks.
    * @return pdTRUE if delay occurred, pdFALSE if unblock time was in the past.
    */
   static BaseType_t delay_until(TickType_t &previousWakeTimeTicks, TickType_t periodTicks) {
      return xTaskDelayUntil(&previousWakeTimeTicks, periodTicks);
   }

   /**
    * @brief Delays the calling task until an absolute deadline in ticks.
    *
    * @param whenTicks Target timestamp in ticks.
    */
   static void sleep_until(TickType_t whenTicks) {
      const TickType_t current = xTaskGetTickCount();
      if(whenTicks > current) {
         vTaskDelay(whenTicks - current);
      }
   }
};

} // namespace art::rtos
