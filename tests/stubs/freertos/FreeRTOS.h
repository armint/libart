#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>

using TickType_t = uint32_t;
using BaseType_t = int32_t;
using UBaseType_t = uint32_t;
using StackType_t = uint32_t;
using EventBits_t = uint32_t;

struct StaticTask_t {
  uint32_t priority{1};
  bool suspended{false};
  bool deleted{false};
};

struct StaticSemaphore_t {
  bool isMutex{false};
  uint32_t count{0};
  uint32_t maxCount{1};
  // Recursive-mutex bookkeeping (unused by plain/binary/counting semaphores): how many
  // unmatched xSemaphoreTakeRecursive() calls are currently outstanding. The underlying
  // count is only released once this drops back to 0, so nested takes stay held until
  // every one of them is given back.
  uint32_t recursionCount{0};
};

struct StaticEventGroup_t {
  EventBits_t bits{0};
};

struct StaticTimer_t {
  const char *name{nullptr};
  TickType_t period{1};
  bool autoReload{false};
  void *timerId{nullptr};
  void (*callback)(void *){nullptr};
  bool active{false};
};

using TaskHandle_t = StaticTask_t*;
using SemaphoreHandle_t = StaticSemaphore_t*;
using EventGroupHandle_t = StaticEventGroup_t*;
using TimerHandle_t = StaticTimer_t*;

using TaskFunction_t = void (*)(void*);
using TimerCallbackFunction_t = void (*)(TimerHandle_t);

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xFFFFFFFFUL
#define configTICK_RATE_HZ 1000U
#define portTICK_PERIOD_MS (1000U / configTICK_RATE_HZ)
#define tskIDLE_PRIORITY 0U

#define configASSERT(x) assert(x)
#define portYIELD_FROM_ISR(x) do { (void)(x); } while(0)

#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>((static_cast<uint64_t>(ms) * configTICK_RATE_HZ) / 1000ULL))
