#include <doctest/doctest.h>
#include <freertos/Rtos.h>

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

   TEST_CASE("RecursiveMutex and RecursiveMutexGuard work properly") {
      art::rtos::RecursiveMutex mutex;
      CHECK(mutex.handle() != nullptr);

      CHECK(mutex.try_lock() == true);
      CHECK(mutex.unlock() == true);

      CHECK(mutex.lock() == true);
      CHECK(mutex.unlock() == true);

      {
         art::rtos::RecursiveMutexGuard guard(mutex);
         CHECK(guard.owns_lock() == true);
         // A nested guard on the same task must not deadlock or fail, unlike Mutex.
         art::rtos::RecursiveMutexGuard nestedGuard(mutex);
         CHECK(nestedGuard.owns_lock() == true);
      }
      // Both guards unwound; the mutex is free again.
      CHECK(mutex.try_lock() == true);
      CHECK(mutex.unlock() == true);
   }

   TEST_CASE("RecursiveMutex requires a matching unlock for every nested lock") {
      art::rtos::RecursiveMutex mutex;

      // Two nested locks from the "same task" (this single-threaded harness has only one)...
      CHECK(mutex.lock() == true);
      CHECK(mutex.lock() == true);

      // ...are not released by a single unlock(). This is the property a naive stub could get
      // wrong by modelling recursive take/give as a plain binary semaphore, where the first
      // give would already free it -- so assert on the underlying FreeRTOS recursion-count
      // semantics directly rather than only re-checking with a same-task try_lock/lock, which
      // would pass either way and not actually distinguish correct recursive behaviour from a
      // mutex that (incorrectly) unlocked early.
      CHECK(mutex.unlock() == true);
      CHECK(xSemaphoreTake(mutex.handle(), 0) == pdFALSE);

      // The second, matching unlock() actually releases it.
      CHECK(mutex.unlock() == true);
      CHECK(xSemaphoreTake(mutex.handle(), 0) == pdTRUE);
      CHECK(xSemaphoreGive(mutex.handle()) == pdTRUE);
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

   TEST_CASE("Queue post/receive respects FIFO order and capacity") {
      art::rtos::Queue<uint32_t, 3> queue;
      CHECK(queue.handle() != nullptr);
      CHECK(queue.hasCapacity() == true);

      CHECK(queue.post(1) == true);
      CHECK(queue.post(2) == true);
      CHECK(queue.post(3) == true);
      // Queue is now full; hasCapacity() must reflect that. This stub's post()/receive() never
      // actually block on xTicksToWait (see the note at the top of the queue.h stub), so a
      // timed post on a full queue fails immediately rather than demonstrating a real timeout.
      CHECK(queue.hasCapacity() == false);
      CHECK(queue.post(4, 0) == false);

      const auto first = queue.receive(0);
      REQUIRE(first.has_value());
      CHECK(*first == 1);
      CHECK(queue.hasCapacity() == true);

      const auto second = queue.receive(0);
      REQUIRE(second.has_value());
      CHECK(*second == 2);

      const auto third = queue.receive(0);
      REQUIRE(third.has_value());
      CHECK(*third == 3);

      // Queue is now empty; same caveat as above applies to a timed receive.
      CHECK(queue.receive(0).has_value() == false);
   }

   TEST_CASE("Queue storage index wraps around the ring buffer correctly") {
      art::rtos::Queue<uint32_t, 2> queue;

      // Repeatedly fill and drain past the capacity boundary so head/tail must wrap modulo
      // Capacity more than once; an off-by-one in the ring-buffer index would eventually
      // corrupt ordering or overwrite an unread slot.
      for(uint32_t cycle = 0; cycle < 5; ++cycle) {
         CHECK(queue.post(cycle * 10 + 1) == true);
         CHECK(queue.post(cycle * 10 + 2) == true);
         CHECK(queue.hasCapacity() == false);

         const auto first = queue.receive(0);
         REQUIRE(first.has_value());
         CHECK(*first == cycle * 10 + 1);

         const auto second = queue.receive(0);
         REQUIRE(second.has_value());
         CHECK(*second == cycle * 10 + 2);

         CHECK(queue.receive(0).has_value() == false);
      }
   }

   TEST_CASE("Queue supports trivially copyable struct payloads") {
      struct SensorSample {
         uint32_t timestampMs;
         int16_t temperatureCentiC;
         uint8_t flags;
      };

      art::rtos::Queue<SensorSample, 2> queue;
      CHECK(queue.post(SensorSample{ 1000, 2150, 0x01 }) == true);
      CHECK(queue.post(SensorSample{ 2000, 2180, 0x02 }) == true);

      const auto first = queue.receive(0);
      REQUIRE(first.has_value());
      CHECK(first->timestampMs == 1000);
      CHECK(first->temperatureCentiC == 2150);
      CHECK(first->flags == 0x01);

      const auto second = queue.receive(0);
      REQUIRE(second.has_value());
      CHECK(second->timestampMs == 2000);
      CHECK(second->temperatureCentiC == 2180);
      CHECK(second->flags == 0x02);
   }

   TEST_CASE("Queue postFromIsr/receiveFromIsr operate correctly") {
      art::rtos::Queue<uint32_t, 2> queue;

      CHECK(queue.postFromIsr(7) == true);
      CHECK(queue.postFromIsr(8) == true);
      // Queue is now full.
      CHECK(queue.postFromIsr(9) == false);

      const auto first = queue.receiveFromIsr();
      REQUIRE(first.has_value());
      CHECK(*first == 7);

      const auto second = queue.receiveFromIsr();
      REQUIRE(second.has_value());
      CHECK(*second == 8);

      // Queue is now empty.
      CHECK(queue.receiveFromIsr().has_value() == false);

      // Explicit higherPriorityTaskWoken pointer variant.
      BaseType_t woken = pdFALSE;
      CHECK(queue.postFromIsr(42, &woken) == true);
      const auto third = queue.receiveFromIsr(&woken);
      REQUIRE(third.has_value());
      CHECK(*third == 42);
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
