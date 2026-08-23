#pragma once

#include <FreeRTOS.h>
#include <cstdint>
#include <semphr.h>

namespace art::rtos {

/**
 * @brief Static-allocation counting or binary semaphore wrapping FreeRTOS static semaphore.
 *
 * @note The Semaphore object must outlive all tasks accessing it.
 */
class Semaphore {
public:
  explicit Semaphore(UBaseType_t maxCount = 1, UBaseType_t initialCount = 0) {
    if (maxCount == 1) {
      handle_ = xSemaphoreCreateBinaryStatic(&buf_);
      if (initialCount > 0) {
        (void)xSemaphoreGive(handle_);
      }
    } else {
      handle_ = xSemaphoreCreateCountingStatic(maxCount, initialCount, &buf_);
    }
  }

  /**
   * @brief Destructs the semaphore and deletes the FreeRTOS handle.
   *
   * @note A Semaphore must not be deleted while tasks are waiting on it.
   */
  ~Semaphore() {
    if (handle_ != nullptr) {
      vSemaphoreDelete(handle_);
      handle_ = nullptr;
    }
  }

  Semaphore(const Semaphore &) = delete;
  Semaphore &operator=(const Semaphore &) = delete;
  Semaphore(Semaphore &&) = delete;
  Semaphore &operator=(Semaphore &&) = delete;

  bool acquire(uint32_t timeoutMs = UINT32_MAX) {
    const TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    return xSemaphoreTake(handle_, ticks) == pdTRUE;
  }

  bool acquire_ticks(TickType_t ticks = portMAX_DELAY) {
    return xSemaphoreTake(handle_, ticks) == pdTRUE;
  }

  bool release() {
    const BaseType_t res = xSemaphoreGive(handle_);
    configASSERT(res == pdTRUE);
    return res == pdTRUE;
  }

  bool acquireFromIsr(BaseType_t *higherPriorityTaskWoken = nullptr) {
    if (higherPriorityTaskWoken != nullptr) {
      return xSemaphoreTakeFromISR(handle_, higherPriorityTaskWoken) == pdTRUE;
    }
    BaseType_t woken = pdFALSE;
    const BaseType_t res = xSemaphoreTakeFromISR(handle_, &woken);
    portYIELD_FROM_ISR(woken);
    return res == pdTRUE;
  }

  bool releaseFromIsr(BaseType_t *higherPriorityTaskWoken = nullptr) {
    if (higherPriorityTaskWoken != nullptr) {
      return xSemaphoreGiveFromISR(handle_, higherPriorityTaskWoken) == pdTRUE;
    }
    BaseType_t woken = pdFALSE;
    const BaseType_t res = xSemaphoreGiveFromISR(handle_, &woken);
    portYIELD_FROM_ISR(woken);
    return res == pdTRUE;
  }

  SemaphoreHandle_t handle() const { return handle_; }

private:
  StaticSemaphore_t buf_{};
  SemaphoreHandle_t handle_{nullptr};
};

} // namespace art::rtos
