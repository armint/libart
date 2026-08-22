#include <doctest/doctest.h>
#include <rtos/Rtos.h>

TEST_SUITE("art::rtos") {

TEST_CASE("CriticalSection compiles and operates") {
  {
    art::rtos::interrupts::critical_section cs;
    // Task-level critical section
  }
  {
    art::rtos::interrupts::isr_critical_section isrCs;
    // ISR-level critical section
  }
  {
    art::rtos::scheduler::critical_section cs;
    // Scheduler suspension
  }
}

TEST_CASE("SysClock returns time and sleeps") {
  g_mockTickCount = 1000;
  CHECK(art::rtos::sysclock::now() == 1000);
  CHECK(art::rtos::sysclock::now_ticks() == 1000);

  art::rtos::sysclock::sleep_for(10);
  CHECK(art::rtos::sysclock::now_ticks() == 1010);

  art::rtos::sysclock::sleep_for_ticks(5);
  CHECK(art::rtos::sysclock::now_ticks() == 1015);

  TickType_t prevTicks = 1015;
  art::rtos::sysclock::delay_until(prevTicks, 50);
  CHECK(prevTicks == 1065);
  CHECK(art::rtos::sysclock::now_ticks() == 1065);

  art::rtos::sysclock::sleep_until(1100);
  CHECK(art::rtos::sysclock::now_ticks() == 1100);
}

TEST_CASE("Mutex and MutexGuard work properly") {
  art::rtos::Mutex mutex;
  CHECK(mutex.handle() != nullptr);

  CHECK(mutex.try_lock() == true);
  // Mutex is now held; subsequent try_lock must return false
  CHECK(mutex.try_lock() == false);
  CHECK(mutex.unlock() == true);

  // Now lock again
  CHECK(mutex.lock() == true);
  CHECK(mutex.unlock() == true);

  {
    art::rtos::MutexGuard guard(mutex);
    CHECK(guard.owns_lock() == true);
    // Locked in scope; second try_lock should fail
    CHECK(mutex.try_lock() == false);
  }
  // Unlocked upon exiting scope
  CHECK(mutex.try_lock() == true);
  CHECK(mutex.unlock() == true);
}

TEST_CASE("Semaphore creates and acquires/releases") {
  art::rtos::Semaphore binSem;
  CHECK(binSem.handle() != nullptr);
  // Initial count is 0, acquire should fail
  CHECK(binSem.acquire(0) == false);
  CHECK(binSem.release() == true);
  CHECK(binSem.acquire(10) == true);
  CHECK(binSem.acquire(0) == false);

  // Test FromIsr methods
  CHECK(binSem.releaseFromIsr() == true);
  CHECK(binSem.acquireFromIsr() == true);

  art::rtos::Semaphore countSem(5, 2);
  CHECK(countSem.handle() != nullptr);
  CHECK(countSem.acquire() == true);
  CHECK(countSem.acquire() == true);
  CHECK(countSem.acquire(0) == false);
  CHECK(countSem.release() == true);
  CHECK(countSem.acquire() == true);
}

TEST_CASE("EventFlags set, get, clear, wait") {
  art::rtos::EventFlags flags;
  CHECK(flags.handle() != nullptr);
  CHECK(flags.get() == 0);

  flags.set(0x05);
  CHECK(flags.get() == 0x05);

  CHECK(flags.wait_any(0x01, false) == 0x01);
  CHECK(flags.get() == 0x05);

  CHECK(flags.wait_all(0x05, true) == 0x05);
  CHECK(flags.get() == 0x00);

  // Test aliases
  flags.raise(0x04);
  CHECK(flags.get() == 0x04);
  CHECK(flags.wait(0x04, true) == 0x04);
  CHECK(flags.get() == 0x00);

  flags.setFromIsr(0x02);
  CHECK(flags.getFromIsr() == 0x02);
  flags.clearFromIsr(0x02);
  CHECK(flags.get() == 0x00);
}

TEST_CASE("Timer lifecycle and operations") {
  auto cb = [](TimerHandle_t) {};
  art::rtos::Timer timer("testTimer", 100, true, nullptr, cb);
  CHECK(timer.handle() != nullptr);
  CHECK(timer.is_active() == false);

  CHECK(timer.start() == true);
  CHECK(timer.is_active() == true);

  CHECK(timer.changePeriod(200) == true);
  CHECK(timer.reset() == true);

  CHECK(timer.stop() == true);
  CHECK(timer.is_active() == false);

  CHECK(timer.startFromIsr() == true);
  CHECK(timer.is_active() == true);
  CHECK(timer.stopFromIsr() == true);
  CHECK(timer.is_active() == false);

  int dummy = 42;
  timer.setTimerId(&dummy);
  CHECK(timer.getTimerId() == &dummy);
}

TEST_CASE("Thread static allocation and start") {
  auto taskFn = [](void *) {};
  art::rtos::Thread<256> thread("testTask", taskFn, nullptr, 1);
  CHECK(thread.handle() != nullptr);
  CHECK(thread.priority() == 1);
  thread.setPriority(2);
  CHECK(thread.priority() == 2);
  thread.suspend();
  thread.resume();
}

}
