#include <cstdint>
#include <doctest/doctest.h>

#include "utils/RingBuffer.h"

TEST_CASE("[RingBuffer] push/pop and wraparound") {
   RingBuffer<uint8_t, 4> rb; // capacity() == 3

   CHECK(rb.empty());
   CHECK(rb.capacity() == 3);

   CHECK(rb.try_push(1));
   CHECK(rb.try_push(2));
   CHECK(rb.try_push(3));
   CHECK(rb.full());
   CHECK_FALSE(rb.try_push(4));

   uint8_t out = 0;
   CHECK(rb.try_pop(out));
   CHECK(out == 1);
   CHECK(rb.size() == 2);

   CHECK(rb.try_push(4));
   CHECK(rb.try_pop(out));
   CHECK(out == 2);
   CHECK(rb.try_pop(out));
   CHECK(out == 3);
   CHECK(rb.try_pop(out));
   CHECK(out == 4);
   CHECK(rb.empty());
   CHECK_FALSE(rb.try_pop(out));
}

TEST_CASE("[RingBuffer] peek does not consume") {
   RingBuffer<uint8_t, 4> rb;
   uint8_t out = 0;
   CHECK_FALSE(rb.peek(out));

   CHECK(rb.try_push(42));
   CHECK(rb.peek(out));
   CHECK(out == 42);
   CHECK(rb.size() == 1); // still there

   CHECK(rb.try_pop(out));
   CHECK(out == 42);
}

TEST_CASE("[RingBuffer] clear resets to empty") {
   RingBuffer<uint8_t, 4> rb;
   CHECK(rb.try_push(1));
   CHECK(rb.try_push(2));
   rb.clear();
   CHECK(rb.empty());
   CHECK(rb.size() == 0);
}

TEST_CASE("[RingBuffer] Size == 2 boundary (capacity() == 1)") {
   RingBuffer<uint8_t, 2> rb;

   CHECK(rb.capacity() == 1);
   CHECK(rb.empty());
   CHECK_FALSE(rb.full());

   CHECK(rb.try_push(9));
   CHECK(rb.full());
   CHECK_FALSE(rb.empty());
   CHECK_FALSE(rb.try_push(10)); // no room for a second element

   uint8_t out = 0;
   CHECK(rb.try_pop(out));
   CHECK(out == 9);
   CHECK(rb.empty());
   CHECK_FALSE(rb.full());
   CHECK_FALSE(rb.try_pop(out));

   // Cycle through the single slot a few times to exercise wraparound at
   // the smallest possible size.
   for(uint8_t i = 0; i < 5; i++) {
      CHECK(rb.try_push(i));
      CHECK(rb.try_pop(out));
      CHECK(out == i);
   }
}

TEST_CASE("[RingBuffer] non-default IndexT wraps correctly through RingBufferBase") {
   // Size = 512 forces the default IndexT to uint16_t; explicitly request
   // uint32_t instead to check the base-interface forwarding still holds
   // when IndexT differs from what default_index<Size> would have picked.
   RingBuffer<uint8_t, 512, uint32_t> rb;
   RingBufferBase<uint8_t> &base = rb;

   CHECK(base.size() == 0);

   // Push/pop enough elements to wrap the index around past Size at least
   // twice, exclusively through the type-erased interface.
   uint8_t sent = 0;
   uint8_t received = 0;
   for(uint32_t i = 0; i < 1500; i++) {
      CHECK(base.try_push(sent));
      sent++;
      uint8_t out = 0;
      CHECK(base.try_pop(out));
      CHECK(out == received);
      received++;
   }
   CHECK(base.empty());
}

TEST_CASE("[RingBuffer] RingBufferBase forwards to the concrete instance") {
   RingBuffer<uint8_t, 4> rb;
   RingBufferBase<uint8_t> &base = rb;

   CHECK(base.empty());
   CHECK(base.try_push(7));
   CHECK_FALSE(base.empty());
   CHECK(base.size() == 1);

   uint8_t peeked = 0;
   CHECK(base.peek(peeked));
   CHECK(peeked == 7);

   uint8_t popped = 0;
   CHECK(base.try_pop(popped));
   CHECK(popped == 7);
   CHECK(base.empty());

   CHECK(base.try_push(1));
   CHECK(base.try_push(2));
   CHECK_FALSE(base.full()); // capacity 3, two pushed
   CHECK(base.try_push(3));
   CHECK(base.full());

   base.clear();
   CHECK(base.empty());
}

TEST_CASE("[RingBuffer] try_push_bulk across wraparound boundary") {
   RingBuffer<uint8_t, 8> rb; // capacity() == 7

   // Setup: push 5 items (head=5, tail=0, size=5)
   uint8_t data[5] = { 10, 20, 30, 40, 50 };
   CHECK(rb.try_push_bulk(data, 5) == 5);
   CHECK(rb.size() == 5);

   // Pop 3 to free space (head=5, tail=3, size=2)
   uint8_t out = 0;
   CHECK(rb.try_pop(out));
   CHECK(out == 10);
   CHECK(rb.try_pop(out));
   CHECK(out == 20);
   CHECK(rb.try_pop(out));
   CHECK(out == 30);
   CHECK(rb.size() == 2);

   // Now head=5, tail=3, size=2, available=5.
   // Push 5 more items. They span the wraparound:
   // - 3 go to indices 5, 6, 7 (to_end=3)
   // - 2 wrap to indices 0, 1
   uint8_t more_data[5] = { 100, 110, 120, 130, 140 };
   std::size_t pushed = rb.try_push_bulk(more_data, 5);
   CHECK(pushed == 5);
   CHECK(rb.full());
   CHECK(rb.size() == 7);

   // Verify: buffer now contains [40, 50, 100, 110, 120, 130, 140]
   // at indices [3, 4, 5, 6, 7, 0, 1] in logical order
   uint8_t expected[7] = { 40, 50, 100, 110, 120, 130, 140 };
   for(uint8_t i = 0; i < 7; i++) {
      CHECK(rb.try_pop(out));
      CHECK(out == expected[i]);
   }
   CHECK(rb.empty());
}

TEST_CASE("[RingBuffer] try_pop_bulk across wraparound boundary") {
   RingBuffer<uint8_t, 8> rb;

   // Push 7 items (head=7, tail=0, size=7)
   uint8_t data[7] = { 11, 22, 33, 44, 55, 66, 77 };
   CHECK(rb.try_push_bulk(data, 7) == 7);
   CHECK(rb.full());

   // Pop 4 to advance tail (head=7, tail=4, size=3)
   uint8_t out = 0;
   CHECK(rb.try_pop(out));
   CHECK(out == 11);
   CHECK(rb.try_pop(out));
   CHECK(out == 22);
   CHECK(rb.try_pop(out));
   CHECK(out == 33);
   CHECK(rb.try_pop(out));
   CHECK(out == 44);
   CHECK(rb.size() == 3);

   // Push 3 more to create wraparound scenario (head=2, tail=4, size=6)
   uint8_t more[3] = { 88, 99, 100 };
   CHECK(rb.try_push_bulk(more, 3) == 3);
   CHECK(rb.size() == 6);
   CHECK_FALSE(rb.full());

   // Pop bulk across wraparound: tail=4, head=2
   // Data at [4, 5, 6, 7, 0, 1] = [55, 66, 77, 88, 99, 100]
   // First contiguous run: indices [4, 5, 6, 7] (to_end = 4)
   // Remaining: indices [0, 1] (wrapped portion)
   uint8_t result[6] = { 0 };
   std::size_t popped = rb.try_pop_bulk(result, 6);
   CHECK(popped == 6);
   uint8_t expected[6] = { 55, 66, 77, 88, 99, 100 };
   for(std::size_t i = 0; i < 6; i++) {
      CHECK(result[i] == expected[i]);
   }
   CHECK(rb.empty());
}

TEST_CASE("[RingBuffer] try_push_bulk returns partial count when exceeds space") {
   RingBuffer<uint8_t, 8> rb;

   // Push 5 items (head=5, tail=0, size=5)
   uint8_t data[5] = { 1, 2, 3, 4, 5 };
   CHECK(rb.try_push_bulk(data, 5) == 5);
   CHECK(rb.size() == 5);

   // Try to push 5 more, but only 2 slots available (capacity - size = 7 - 5 = 2)
   uint8_t more[5] = { 10, 20, 30, 40, 50 };
   std::size_t pushed = rb.try_push_bulk(more, 5);
   CHECK(pushed == 2); // truncated to available space
   CHECK(rb.full());
   CHECK(rb.size() == 7);

   // Verify contents
   uint8_t out = 0;
   CHECK(rb.try_pop(out));
   CHECK(out == 1);
   CHECK(rb.try_pop(out));
   CHECK(out == 2);
   CHECK(rb.try_pop(out));
   CHECK(out == 3);
   CHECK(rb.try_pop(out));
   CHECK(out == 4);
   CHECK(rb.try_pop(out));
   CHECK(out == 5);
   CHECK(rb.try_pop(out));
   CHECK(out == 10);
   CHECK(rb.try_pop(out));
   CHECK(out == 20);
   CHECK(rb.empty());
}

TEST_CASE("[RingBuffer] try_pop_bulk returns partial count when data insufficient") {
   RingBuffer<uint8_t, 8> rb;

   // Push 3 items
   uint8_t data[3] = { 100, 200, 50 };
   CHECK(rb.try_push_bulk(data, 3) == 3);
   CHECK(rb.size() == 3);

   // Try to pop 5, but only 3 available
   uint8_t result[5] = { 0 };
   std::size_t popped = rb.try_pop_bulk(result, 5);
   CHECK(popped == 3); // only 3 available
   CHECK(result[0] == 100);
   CHECK(result[1] == 200);
   CHECK(result[2] == 50);
   CHECK(rb.empty());
}

TEST_CASE("[RingBuffer] write_ptr and commit_write basic") {
   RingBuffer<uint8_t, 8> rb;

   // Get write pointer on empty buffer
   std::size_t len = 0;
   uint8_t *ptr = rb.write_ptr(len);
   CHECK(ptr != nullptr);
   CHECK(len == 7);   // full capacity (Size - 1)
   CHECK(rb.empty()); // getting a pointer doesn't consume space

   // Write 3 items directly
   ptr[0] = 11;
   ptr[1] = 22;
   ptr[2] = 33;
   rb.commit_write(3);
   CHECK(rb.size() == 3);

   // Pop to verify
   uint8_t out = 0;
   CHECK(rb.try_pop(out));
   CHECK(out == 11);
   CHECK(rb.try_pop(out));
   CHECK(out == 22);
   CHECK(rb.try_pop(out));
   CHECK(out == 33);
   CHECK(rb.empty());
}

TEST_CASE("[RingBuffer] write_ptr and commit_write with wraparound and partial commits") {
   RingBuffer<uint8_t, 8> rb; // capacity = 7

   // Pre-fill to position head at index 5
   uint8_t data[5] = { 1, 2, 3, 4, 5 };
   CHECK(rb.try_push_bulk(data, 5) == 5);
   // head=5, tail=0, size=5

   // Pop 3 to advance tail to index 3
   uint8_t out = 0;
   CHECK(rb.try_pop(out));
   CHECK(out == 1);
   CHECK(rb.try_pop(out));
   CHECK(out == 2);
   CHECK(rb.try_pop(out));
   CHECK(out == 3);
   // head=5, tail=3, size=2

   // First write_ptr: available=5, to_end=3, len=3 (indices 5,6,7)
   std::size_t len = 0;
   uint8_t *ptr = rb.write_ptr(len);
   CHECK(len == 3); // can write 3 before buffer boundary
   REQUIRE(ptr != nullptr);

   // Write only 2 and commit (partial)
   ptr[0] = 10;
   ptr[1] = 20;
   rb.commit_write(2);
   CHECK(rb.size() == 4); // 2 existing + 2 written

   // Second write_ptr: available=3, to_end=1, len=1 (index 7)
   len = 0;
   ptr = rb.write_ptr(len);
   CHECK(len == 1); // only 1 slot before boundary
   REQUIRE(ptr != nullptr);

   // Write 1 and commit
   ptr[0] = 30;
   rb.commit_write(1);
   CHECK(rb.size() == 5);

   // Third write_ptr: available=2, to_end=8, len=2 (indices 0,1)
   len = 0;
   ptr = rb.write_ptr(len);
   CHECK(len == 2); // wrapped, 2 slots before tail at 3
   REQUIRE(ptr != nullptr);

   // Write remaining 2 and commit
   ptr[0] = 40;
   ptr[1] = 50;
   rb.commit_write(2);
   CHECK(rb.full());
   CHECK(rb.size() == 7);

   // Pop all and verify order: 4, 5, 10, 20, 30, 40, 50
   uint8_t expected[7] = { 4, 5, 10, 20, 30, 40, 50 };
   for(uint8_t i = 0; i < 7; i++) {
      CHECK(rb.try_pop(out));
      CHECK(out == expected[i]);
   }
   CHECK(rb.empty());
}

TEST_CASE("[RingBuffer] read_ptr and commit_read basic") {
   RingBuffer<uint8_t, 8> rb;

   // Push some data
   uint8_t data[4] = { 77, 88, 99, 110 };
   CHECK(rb.try_push_bulk(data, 4) == 4);
   CHECK(rb.size() == 4);

   // Get read pointer
   std::size_t len = 0;
   const uint8_t *ptr = rb.read_ptr(len);
   CHECK(ptr != nullptr);
   CHECK(len == 4); // 4 items available

   // Verify data
   CHECK(ptr[0] == 77);
   CHECK(ptr[1] == 88);
   CHECK(ptr[2] == 99);
   CHECK(ptr[3] == 110);

   // Commit reading all 4
   rb.commit_read(4);
   CHECK(rb.empty());
}

TEST_CASE("[RingBuffer] read_ptr and commit_read with partial commit and wraparound") {
   RingBuffer<uint8_t, 8> rb;

   // Pre-fill (head=6, tail=0, size=6)
   uint8_t data[6] = { 11, 22, 33, 44, 55, 66 };
   CHECK(rb.try_push_bulk(data, 6) == 6);

   // Pop 4 to advance tail (head=6, tail=4, size=2)
   uint8_t out = 0;
   CHECK(rb.try_pop(out));
   CHECK(out == 11);
   CHECK(rb.try_pop(out));
   CHECK(out == 22);
   CHECK(rb.try_pop(out));
   CHECK(out == 33);
   CHECK(rb.try_pop(out));
   CHECK(out == 44);
   CHECK(rb.size() == 2);

   // Push 4 more: 2 at indices 6,7, then 2 wrapped at 0,1 (head=2, tail=4, size=6)
   uint8_t more[4] = { 77, 88, 99, 110 };
   CHECK(rb.try_push_bulk(more, 4) == 4);
   CHECK(rb.size() == 6);

   // First read_ptr: tail=4, available=6, to_end=4, len=4
   // Returns pointer to indices [4, 5, 6, 7] = [55, 66, 77, 88]
   std::size_t len = 0;
   const uint8_t *ptr = rb.read_ptr(len);
   CHECK(len == 4); // 4 contiguous elements before end
   REQUIRE(ptr != nullptr);
   CHECK(ptr[0] == 55);
   CHECK(ptr[1] == 66);
   CHECK(ptr[2] == 77);
   CHECK(ptr[3] == 88);

   // Commit reading only 2 (partial)
   rb.commit_read(2);
   // tail=6, head=2, size=4
   CHECK(rb.size() == 4);

   // Second read_ptr: tail=6, available=4, to_end=2, len=2
   // Returns pointer to indices [6, 7] = [77, 88] (not the wrapped portion yet)
   len = 0;
   ptr = rb.read_ptr(len);
   CHECK(len == 2); // 2 contiguous before end
   REQUIRE(ptr != nullptr);
   CHECK(ptr[0] == 77);
   CHECK(ptr[1] == 88);

   // Commit these 2
   rb.commit_read(2);
   // tail=0, head=2, size=2
   CHECK(rb.size() == 2);

   // Third read_ptr: tail=0, available=2, to_end=8, len=2
   // Returns pointer to indices [0, 1] = [99, 110]
   len = 0;
   ptr = rb.read_ptr(len);
   CHECK(len == 2);
   REQUIRE(ptr != nullptr);
   CHECK(ptr[0] == 99);
   CHECK(ptr[1] == 110);

   // Commit all remaining
   rb.commit_read(2);
   CHECK(rb.empty());
}

TEST_CASE("[RingBuffer] write_ptr on empty buffer returns full capacity and space_available") {
   RingBuffer<uint8_t, 8> rb;

   std::size_t len = 0;
   uint8_t *ptr = rb.write_ptr(len);
   CHECK(ptr != nullptr);
   CHECK(len == 7); // full capacity (Size - 1)
   CHECK(len == rb.space_available());
   CHECK(rb.empty());
}

TEST_CASE("[RingBuffer] read_ptr on empty buffer returns zero length") {
   RingBuffer<uint8_t, 8> rb;

   std::size_t len = 0;
   const uint8_t *ptr = rb.read_ptr(len);
   CHECK(ptr != nullptr);
   CHECK(len == 0); // nothing to read
   CHECK(rb.empty());
}

TEST_CASE("[RingBuffer] read_ptr on full buffer returns available contiguous run") {
   RingBuffer<uint8_t, 8> rb;

   // Fill completely
   uint8_t data[7] = { 1, 2, 3, 4, 5, 6, 7 };
   CHECK(rb.try_push_bulk(data, 7) == 7);
   CHECK(rb.full());

   // Read pointer should return all 7
   std::size_t len = 0;
   const uint8_t *ptr = rb.read_ptr(len);
   CHECK(len == 7);
   for(uint8_t i = 0; i < 7; i++) {
      CHECK(ptr[i] == i + 1);
   }
}

TEST_CASE("[RingBuffer] write_ptr on full buffer returns zero length") {
   RingBuffer<uint8_t, 8> rb;

   // Fill completely
   uint8_t data[7] = { 1, 2, 3, 4, 5, 6, 7 };
   CHECK(rb.try_push_bulk(data, 7) == 7);
   CHECK(rb.full());

   // Write pointer should return zero length
   std::size_t len = 0;
   uint8_t *ptr = rb.write_ptr(len);
   CHECK(len == 0);
}

TEST_CASE("[RingBuffer] try_push_bulk on full buffer returns 0") {
   RingBuffer<uint8_t, 8> rb;

   // Fill completely (capacity = 7)
   uint8_t data[7] = { 1, 2, 3, 4, 5, 6, 7 };
   CHECK(rb.try_push_bulk(data, 7) == 7);
   CHECK(rb.full());
   CHECK(rb.size() == 7);

   // Try to push more on full buffer: should return 0 (early return when count > avail)
   uint8_t more[3] = { 10, 20, 30 };
   std::size_t pushed = rb.try_push_bulk(more, 3);
   CHECK(pushed == 0);

   // Verify buffer unchanged
   CHECK(rb.full());
   CHECK(rb.size() == 7);

   // Verify contents unchanged
   uint8_t out = 0;
   for(uint8_t i = 1; i <= 7; i++) {
      CHECK(rb.try_pop(out));
      CHECK(out == i);
   }
   CHECK(rb.empty());
}

TEST_CASE("[RingBuffer] try_pop_bulk on empty buffer returns 0") {
   RingBuffer<uint8_t, 8> rb;

   // Verify empty
   CHECK(rb.empty());
   CHECK(rb.size() == 0);

   // Try to pop from empty buffer: should return 0 (early return when count > avail)
   uint8_t result[3] = { 0 };
   std::size_t popped = rb.try_pop_bulk(result, 3);
   CHECK(popped == 0);

   // Verify buffer still empty
   CHECK(rb.empty());
   CHECK(rb.size() == 0);

   // Verify no data was written to result
   CHECK(result[0] == 0);
   CHECK(result[1] == 0);
   CHECK(result[2] == 0);
}
