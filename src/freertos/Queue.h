#pragma once

#include <FreeRTOS.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <queue.h>
#include <type_traits>

namespace art::rtos {

/**
 * @brief Static-allocation, fixed-capacity FIFO message queue wrapping FreeRTOS static queue.
 *
 * Items are copied by value into/out of the underlying FreeRTOS queue storage, so T must be
 * trivially copyable.
 *
 * @note The Queue object must outlive all tasks accessing it.
 */
template <typename T, size_t Capacity>
class Queue {
  static_assert(Capacity > 0, "Queue capacity must be greater than zero");
  static_assert(std::is_trivially_copyable_v<T>, "Queue<T> requires a trivially copyable T");
  static_assert(std::is_default_constructible_v<T>, "Queue<T> requires a default-constructible T");

public:
  Queue() {
    handle_ = xQueueCreateStatic(Capacity, sizeof(T), storage_, &buf_);
  }

  /**
   * @brief Destructs the queue and deletes the FreeRTOS handle.
   *
   * @note A Queue must not be deleted while tasks are waiting on it.
   */
  ~Queue() {
    if (handle_ != nullptr) {
      vQueueDelete(handle_);
      handle_ = nullptr;
    }
  }

  Queue(const Queue &) = delete;
  Queue &operator=(const Queue &) = delete;
  Queue(Queue &&) = delete;
  Queue &operator=(Queue &&) = delete;

  /**
   * @brief Enqueues a copy of msg at the back of the queue, blocking while full.
   *
   * @param timeoutMs Maximum time to wait for space, in milliseconds. UINT32_MAX blocks
   * indefinitely, 0 does not block.
   * @return true if the item was enqueued, false on timeout.
   */
  bool post(const T &msg, uint32_t timeoutMs = UINT32_MAX) {
    const TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    return xQueueSendToBack(handle_, &msg, ticks) == pdTRUE;
  }

  /**
   * @brief Dequeues an item from the front of the queue, blocking while empty.
   *
   * @param timeoutMs Maximum time to wait for an item, in milliseconds. UINT32_MAX blocks
   * indefinitely, 0 does not block.
   * @return The dequeued item, or std::nullopt on timeout.
   */
  std::optional<T> receive(uint32_t timeoutMs = UINT32_MAX) {
    const TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    T msg;
    if (xQueueReceive(handle_, &msg, ticks) == pdTRUE) {
      return msg;
    }
    return std::nullopt;
  }

  /**
   * @brief Enqueues a copy of msg at the back of the queue from ISR context.
   */
  bool postFromIsr(const T &msg, BaseType_t *higherPriorityTaskWoken = nullptr) {
    if (higherPriorityTaskWoken != nullptr) {
      return xQueueSendToBackFromISR(handle_, &msg, higherPriorityTaskWoken) == pdTRUE;
    }
    BaseType_t woken = pdFALSE;
    const BaseType_t res = xQueueSendToBackFromISR(handle_, &msg, &woken);
    portYIELD_FROM_ISR(woken);
    return res == pdTRUE;
  }

  /**
   * @brief Dequeues an item from the front of the queue from ISR context.
   */
  std::optional<T> receiveFromIsr(BaseType_t *higherPriorityTaskWoken = nullptr) {
    T msg;
    BaseType_t res;
    if (higherPriorityTaskWoken != nullptr) {
      res = xQueueReceiveFromISR(handle_, &msg, higherPriorityTaskWoken);
    } else {
      BaseType_t woken = pdFALSE;
      res = xQueueReceiveFromISR(handle_, &msg, &woken);
      portYIELD_FROM_ISR(woken);
    }
    if (res == pdTRUE) {
      return msg;
    }
    return std::nullopt;
  }

  /**
   * @brief Returns whether the queue currently has room for at least one more item.
   *
   * @note Advisory only: concurrent posts/receives from other tasks or ISRs can change the
   * result immediately after it is read. Do not use as a substitute for checking post()'s
   * return value.
   */
  bool hasCapacity() const {
    return uxQueueSpacesAvailable(handle_) > 0;
  }

  QueueHandle_t handle() const { return handle_; }

private:
  alignas(T) uint8_t storage_[Capacity * sizeof(T)]{};
  StaticQueue_t buf_{};
  QueueHandle_t handle_{nullptr};
};

} // namespace art::rtos
