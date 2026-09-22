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
  std::optional<T> timedWait(uint32_t timeoutMs = UINT32_MAX) {
    const TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    T msg;
    if (xQueueReceive(handle_, &msg, ticks) == pdTRUE) {
      return msg;
    }
    return std::nullopt;
  }

  /**
   * @brief Returns whether the queue currently has room for at least one more item.
   */
  bool hasCapacity() const {
    return uxQueueSpacesAvailable(handle_) > 0;
  }

  QueueHandle_t handle() const { return handle_; }

private:
  uint8_t storage_[Capacity * sizeof(T)]{};
  StaticQueue_t buf_{};
  QueueHandle_t handle_{nullptr};
};

} // namespace art::rtos
