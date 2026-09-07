#pragma once

#include <FreeRTOS.h>
#include <cstdint>
#include <semphr.h>

namespace art::rtos {

/**
 * @brief Static-allocation RAII recursive mutex wrapping FreeRTOS's static recursive mutex.
 *
 * Unlike Mutex, the task that already holds this may lock() it again without deadlocking
 * itself: each lock() call must be matched by exactly one unlock() before another task can
 * acquire it. This is the primitive a Writable sink needs before it can safely override
 * Writable::beginWriteSession()/endWriteSession() with a lock (see streams/Writable.h) --
 * sessions may nest, e.g. logging::Logger holding an outer session across a whole log line
 * while each printf() call within it opens its own inner one on the same task. A plain
 * Mutex self-deadlocks on that second, nested take.
 *
 * @note The RecursiveMutex object must outlive all tasks accessing it.
 */
class RecursiveMutex {
public:
  RecursiveMutex() {
    handle_ = xSemaphoreCreateRecursiveMutexStatic(&buf_);
  }

  /**
   * @brief Destructs the mutex and deletes the FreeRTOS handle.
   *
   * @note A RecursiveMutex must not be deleted while held by any task.
   */
  ~RecursiveMutex() {
    if (handle_ != nullptr) {
      vSemaphoreDelete(handle_);
      handle_ = nullptr;
    }
  }

  RecursiveMutex(const RecursiveMutex &) = delete;
  RecursiveMutex &operator=(const RecursiveMutex &) = delete;
  RecursiveMutex(RecursiveMutex &&) = delete;
  RecursiveMutex &operator=(RecursiveMutex &&) = delete;

  bool lock() {
    const BaseType_t res = xSemaphoreTakeRecursive(handle_, portMAX_DELAY);
    configASSERT(res == pdTRUE);
    return res == pdTRUE;
  }

  bool try_lock() {
    return xSemaphoreTakeRecursive(handle_, 0) == pdTRUE;
  }

  bool lock_for(uint32_t timeoutMs) {
    const TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    return xSemaphoreTakeRecursive(handle_, ticks) == pdTRUE;
  }

  bool unlock() {
    const BaseType_t res = xSemaphoreGiveRecursive(handle_);
    configASSERT(res == pdTRUE);
    return res == pdTRUE;
  }

  SemaphoreHandle_t handle() const { return handle_; }

private:
  StaticSemaphore_t buf_{};
  SemaphoreHandle_t handle_{nullptr};
};

/**
 * @brief RAII scoped lock guard for RecursiveMutex.
 */
class RecursiveMutexGuard {
public:
  explicit RecursiveMutexGuard(RecursiveMutex &mutex) : mutex_(mutex), ownsLock_(mutex_.lock()) {}

  ~RecursiveMutexGuard() {
    if (ownsLock_) {
      mutex_.unlock();
    }
  }

  bool owns_lock() const { return ownsLock_; }

  RecursiveMutexGuard(const RecursiveMutexGuard &) = delete;
  RecursiveMutexGuard &operator=(const RecursiveMutexGuard &) = delete;
  RecursiveMutexGuard(RecursiveMutexGuard &&) = delete;
  RecursiveMutexGuard &operator=(RecursiveMutexGuard &&) = delete;

private:
  RecursiveMutex &mutex_;
  bool ownsLock_{false};
};

} // namespace art::rtos
