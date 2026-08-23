#pragma once

#include <FreeRTOS.h>
#include <cstdint>
#include <semphr.h>

namespace art::rtos {

/**
 * @brief Static-allocation RAII mutex wrapping FreeRTOS static mutex.
 *
 * @note The Mutex object must outlive all tasks accessing it.
 */
class Mutex {
public:
  Mutex() {
    handle_ = xSemaphoreCreateMutexStatic(&buf_);
  }

  /**
   * @brief Destructs the mutex and deletes the FreeRTOS handle.
   *
   * @note A Mutex must not be deleted while held by any task.
   */
  ~Mutex() {
    if (handle_ != nullptr) {
      vSemaphoreDelete(handle_);
      handle_ = nullptr;
    }
  }

  Mutex(const Mutex &) = delete;
  Mutex &operator=(const Mutex &) = delete;
  Mutex(Mutex &&) = delete;
  Mutex &operator=(Mutex &&) = delete;

  bool lock() {
    const BaseType_t res = xSemaphoreTake(handle_, portMAX_DELAY);
    configASSERT(res == pdTRUE);
    return res == pdTRUE;
  }

  bool try_lock() {
    return xSemaphoreTake(handle_, 0) == pdTRUE;
  }

  bool lock_for(uint32_t timeoutMs) {
    const TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    return xSemaphoreTake(handle_, ticks) == pdTRUE;
  }

  bool unlock() {
    const BaseType_t res = xSemaphoreGive(handle_);
    configASSERT(res == pdTRUE);
    return res == pdTRUE;
  }

  SemaphoreHandle_t handle() const { return handle_; }

private:
  StaticSemaphore_t buf_{};
  SemaphoreHandle_t handle_{nullptr};
};

/**
 * @brief RAII scoped lock guard for Mutex.
 */
class MutexGuard {
public:
  explicit MutexGuard(Mutex &mutex) : mutex_(mutex), ownsLock_(mutex_.lock()) {}

  ~MutexGuard() {
    if (ownsLock_) {
      mutex_.unlock();
    }
  }

  bool owns_lock() const { return ownsLock_; }

  MutexGuard(const MutexGuard &) = delete;
  MutexGuard &operator=(const MutexGuard &) = delete;
  MutexGuard(MutexGuard &&) = delete;
  MutexGuard &operator=(MutexGuard &&) = delete;

private:
  Mutex &mutex_;
  bool ownsLock_{false};
};

} // namespace art::rtos
