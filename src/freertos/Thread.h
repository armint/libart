#pragma once

#include <FreeRTOS.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <task.h>

namespace art::rtos {

/**
 * @brief Static-allocation thread/task wrapping FreeRTOS StaticTask.
 *
 * @note Thread owns its `StaticTask_t` control block and stack buffer inline.
 * Because the FreeRTOS scheduler references these buffers directly, a Thread instance
 * must outlive the executing task (typically by being given static or immortal storage duration).
 *
 * @note FreeRTOS task functions must implement an infinite loop and must never return.
 * If a task needs to terminate, it must delete itself using vTaskDelete(nullptr).
 *
 * @tparam StackWords Stack size in words (StackType_t units).
 */
template <size_t StackWords>
class Thread {
   public:
   static constexpr size_t kStackWords = StackWords;

   Thread() = default;

   Thread(const char *name, TaskFunction_t function, void *parameters = nullptr, UBaseType_t priority = tskIDLE_PRIORITY + 1) {
      start(name, function, parameters, priority);
   }

   ~Thread() {
      if(handle_ != nullptr) {
         vTaskDelete(handle_);
         handle_ = nullptr;
      }
   }

   Thread(const Thread &) = delete;
   Thread &operator=(const Thread &) = delete;
   Thread(Thread &&) = delete;
   Thread &operator=(Thread &&) = delete;

   bool start(const char *name, TaskFunction_t function, void *parameters = nullptr, UBaseType_t priority = tskIDLE_PRIORITY + 1) {
      if(handle_ != nullptr) {
         return false;
      }
      handle_ = xTaskCreateStatic(function, name, static_cast<uint32_t>(StackWords), parameters, priority, stack_.data(), &tcb_);
      return handle_ != nullptr;
   }

   void suspend() {
      if(handle_ != nullptr) {
         vTaskSuspend(handle_);
      }
   }

   void resume() {
      if(handle_ != nullptr) {
         vTaskResume(handle_);
      }
   }

   UBaseType_t priority() const {
      return (handle_ != nullptr) ? uxTaskPriorityGet(handle_) : 0;
   }

   void setPriority(UBaseType_t priority) {
      if(handle_ != nullptr) {
         vTaskPrioritySet(handle_, priority);
      }
   }

   TaskHandle_t handle() const {
      return handle_;
   }

   private:
   StaticTask_t tcb_{};
   std::array<StackType_t, StackWords> stack_{};
   TaskHandle_t handle_{ nullptr };
};

} // namespace art::rtos
