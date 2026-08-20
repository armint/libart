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
