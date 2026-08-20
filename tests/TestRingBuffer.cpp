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
