#pragma once

#include <FreeRTOS.h>
#include <task.h>

namespace art::rtos {

namespace interrupts {

/**
 * @brief RAII task-level critical section guard using taskENTER_CRITICAL / taskEXIT_CRITICAL.
 *
 * @note On Cortex-M, this masks interrupts up to `configMAX_SYSCALL_INTERRUPT_PRIORITY`
 * by setting BASEPRI. It does NOT mask all interrupts (does not set PRIMASK).
 * This guard is valid ONLY from task context; do not invoke from an ISR.
 */
class critical_section {
public:
  critical_section() { taskENTER_CRITICAL(); }
  ~critical_section() { taskEXIT_CRITICAL(); }

  critical_section(const critical_section &) = delete;
  critical_section &operator=(const critical_section &) = delete;
  critical_section(critical_section &&) = delete;
  critical_section &operator=(critical_section &&) = delete;
};

/**
 * @brief RAII ISR-level critical section guard using taskENTER_CRITICAL_FROM_ISR /
 * taskEXIT_CRITICAL_FROM_ISR.
 *
 * @note Valid ONLY from ISR context. Saves and restores the interrupt mask status.
 */
class isr_critical_section {
public:
  isr_critical_section() : savedStatus_(taskENTER_CRITICAL_FROM_ISR()) {}
  ~isr_critical_section() { taskEXIT_CRITICAL_FROM_ISR(savedStatus_); }

  isr_critical_section(const isr_critical_section &) = delete;
  isr_critical_section &operator=(const isr_critical_section &) = delete;
  isr_critical_section(isr_critical_section &&) = delete;
  isr_critical_section &operator=(isr_critical_section &&) = delete;

private:
  UBaseType_t savedStatus_{0};
};

} // namespace interrupts

namespace scheduler {

/**
 * @brief RAII scheduler suspension guard using vTaskSuspendAll / xTaskResumeAll.
 */
class critical_section {
public:
  critical_section() { vTaskSuspendAll(); }
  ~critical_section() { xTaskResumeAll(); }

  critical_section(const critical_section &) = delete;
  critical_section &operator=(const critical_section &) = delete;
  critical_section(critical_section &&) = delete;
  critical_section &operator=(critical_section &&) = delete;
};

} // namespace scheduler

} // namespace art::rtos
