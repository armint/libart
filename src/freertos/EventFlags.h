#pragma once

#include <FreeRTOS.h>
#include <cstdint>
#include <event_groups.h>

namespace art::rtos {

/**
 * @brief Static-allocation event flags group wrapping FreeRTOS EventGroup.
 *
 * @note The EventFlags object must outlive all tasks accessing it.
 */
class EventFlags {
   public:
   EventFlags() {
      handle_ = xEventGroupCreateStatic(&buf_);
   }

   ~EventFlags() {
      if(handle_ != nullptr) {
         vEventGroupDelete(handle_);
         handle_ = nullptr;
      }
   }

   EventFlags(const EventFlags &) = delete;
   EventFlags &operator=(const EventFlags &) = delete;
   EventFlags(EventFlags &&) = delete;
   EventFlags &operator=(EventFlags &&) = delete;

   EventBits_t wait_all(EventBits_t bits, bool clearOnExit = true, uint32_t timeoutMs = UINT32_MAX) {
      const TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
      return xEventGroupWaitBits(handle_, bits, clearOnExit ? pdTRUE : pdFALSE, pdTRUE, ticks);
   }

   EventBits_t wait_any(EventBits_t bits, bool clearOnExit = true, uint32_t timeoutMs = UINT32_MAX) {
      const TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
      return xEventGroupWaitBits(handle_, bits, clearOnExit ? pdTRUE : pdFALSE, pdFALSE, ticks);
   }

   /**
    * @brief Alias for wait_all to match os::rtos naming conventions.
    */
   EventBits_t wait(EventBits_t bits, bool clearOnExit = true, uint32_t timeoutMs = UINT32_MAX) {
      return wait_all(bits, clearOnExit, timeoutMs);
   }

   EventBits_t set(EventBits_t bits) {
      return xEventGroupSetBits(handle_, bits);
   }

   /**
    * @brief Alias for set to match os::rtos naming conventions.
    */
   EventBits_t raise(EventBits_t bits) {
      return set(bits);
   }

   EventBits_t clear(EventBits_t bits) {
      return xEventGroupClearBits(handle_, bits);
   }

   EventBits_t get() const {
      return xEventGroupGetBits(handle_);
   }

   /**
    * @brief Sets event bits from ISR context.
    *
    * @note Requires configUSE_TIMERS == 1 and INCLUDE_xTimerPendFunctionCall == 1 in FreeRTOSConfig.h.
    */
   BaseType_t setFromIsr(EventBits_t bits, BaseType_t *higherPriorityTaskWoken = nullptr) {
      if(higherPriorityTaskWoken != nullptr) {
         return xEventGroupSetBitsFromISR(handle_, bits, higherPriorityTaskWoken);
      }
      BaseType_t woken = pdFALSE;
      const BaseType_t res = xEventGroupSetBitsFromISR(handle_, bits, &woken);
      portYIELD_FROM_ISR(woken);
      return res;
   }

   BaseType_t clearFromIsr(EventBits_t bits) {
      return xEventGroupClearBitsFromISR(handle_, bits);
   }

   EventBits_t getFromIsr() const {
      return xEventGroupGetBitsFromISR(handle_);
   }

   EventGroupHandle_t handle() const {
      return handle_;
   }

   private:
   StaticEventGroup_t buf_{};
   EventGroupHandle_t handle_{ nullptr };
};

} // namespace art::rtos
