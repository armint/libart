#pragma once

#include <FreeRTOS.h>
#include <cstdint>
#include <timers.h>

namespace art::rtos {

/**
 * @brief Static-allocation software timer wrapping FreeRTOS StaticTimer.
 *
 * @note Timer owns its `StaticTimer_t` control block inline.
 * Because the FreeRTOS timer daemon task references this buffer directly, a Timer instance
 * must outlive any queued or active timer operations (typically by being given static or immortal storage duration).
 */
class Timer {
   public:
   Timer(const char *name, uint32_t periodMs, bool autoReload, void *timerId, TimerCallbackFunction_t callback) {
      TickType_t ticks = pdMS_TO_TICKS(periodMs);
      if(ticks == 0) {
         ticks = 1;
      }
      handle_ = xTimerCreateStatic(name, ticks, autoReload ? pdTRUE : pdFALSE, timerId, callback, &buf_);
   }

   ~Timer() {
      if(handle_ != nullptr) {
         (void)xTimerDelete(handle_, 0);
         handle_ = nullptr;
      }
   }

   Timer(const Timer &) = delete;
   Timer &operator=(const Timer &) = delete;
   Timer(Timer &&) = delete;
   Timer &operator=(Timer &&) = delete;

   bool start(uint32_t blockTimeMs = 0) {
      return xTimerStart(handle_, pdMS_TO_TICKS(blockTimeMs)) == pdPASS;
   }

   bool stop(uint32_t blockTimeMs = 0) {
      return xTimerStop(handle_, pdMS_TO_TICKS(blockTimeMs)) == pdPASS;
   }

   bool reset(uint32_t blockTimeMs = 0) {
      return xTimerReset(handle_, pdMS_TO_TICKS(blockTimeMs)) == pdPASS;
   }

   bool changePeriod(uint32_t newPeriodMs, uint32_t blockTimeMs = 0) {
      TickType_t ticks = pdMS_TO_TICKS(newPeriodMs);
      if(ticks == 0) {
         ticks = 1;
      }
      return xTimerChangePeriod(handle_, ticks, pdMS_TO_TICKS(blockTimeMs)) == pdPASS;
   }

   bool startFromIsr(BaseType_t *higherPriorityTaskWoken = nullptr) {
      if(higherPriorityTaskWoken != nullptr) {
         return xTimerStartFromISR(handle_, higherPriorityTaskWoken) == pdPASS;
      }
      BaseType_t woken = pdFALSE;
      const BaseType_t res = xTimerStartFromISR(handle_, &woken);
      portYIELD_FROM_ISR(woken);
      return res == pdPASS;
   }

   bool stopFromIsr(BaseType_t *higherPriorityTaskWoken = nullptr) {
      if(higherPriorityTaskWoken != nullptr) {
         return xTimerStopFromISR(handle_, higherPriorityTaskWoken) == pdPASS;
      }
      BaseType_t woken = pdFALSE;
      const BaseType_t res = xTimerStopFromISR(handle_, &woken);
      portYIELD_FROM_ISR(woken);
      return res == pdPASS;
   }

   bool resetFromIsr(BaseType_t *higherPriorityTaskWoken = nullptr) {
      if(higherPriorityTaskWoken != nullptr) {
         return xTimerResetFromISR(handle_, higherPriorityTaskWoken) == pdPASS;
      }
      BaseType_t woken = pdFALSE;
      const BaseType_t res = xTimerResetFromISR(handle_, &woken);
      portYIELD_FROM_ISR(woken);
      return res == pdPASS;
   }

   bool is_active() const {
      return xTimerIsTimerActive(handle_) != pdFALSE;
   }

   void *getTimerId() const {
      return pvTimerGetTimerID(handle_);
   }

   void setTimerId(void *newId) {
      vTimerSetTimerID(handle_, newId);
   }

   TimerHandle_t handle() const {
      return handle_;
   }

   private:
   StaticTimer_t buf_{};
   TimerHandle_t handle_{ nullptr };
};

} // namespace art::rtos
