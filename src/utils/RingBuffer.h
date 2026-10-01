#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// ===========================================================================
// Single-producer / single-consumer lock-free ring buffer.
//
// Intended use: one ISR on one side, one thread/main loop on the other.
// Exactly ONE producer and ONE consumer. Two ISRs pushing, or a thread and
// an ISR both pushing, is NOT supported and will corrupt the buffer.
//
// Configuration macros (define before including):
//   RB_NO_STD_ATOMIC        Force the volatile+fence path even if <atomic>
//                           is available.
//   RB_INDEX_ALIGNMENT      Alignment for the head/tail indices. Set to the
//                           cache line size (e.g. 64) on cached/multi-core
//                           targets to avoid false sharing. Default: pointer
//                           sized, i.e. no padding.
//
// NOTE on non-coherent multi-core (e.g. Cortex-A + Cortex-M on the same SoC):
// fences order accesses, they do NOT perform cache maintenance. Placing the
// buffer in a shared region requires either non-cacheable memory or explicit
// clean/invalidate around the payload. This header does not do that for you.
// ===========================================================================

// ---------------------------------------------------------------------------
// Backend selection
// ---------------------------------------------------------------------------
#if !defined(RB_USE_STD_ATOMIC)
#if defined(RB_NO_STD_ATOMIC)
#define RB_USE_STD_ATOMIC 0
#elif defined(__has_include)
#if __has_include(<atomic>)
#define RB_USE_STD_ATOMIC 1
#else
#define RB_USE_STD_ATOMIC 0
#endif
#else
#define RB_USE_STD_ATOMIC 0
#endif
#endif

#if RB_USE_STD_ATOMIC
#include <atomic>
#endif

#ifndef RB_INDEX_ALIGNMENT
#define RB_INDEX_ALIGNMENT alignof(std::size_t)
#endif

// ---------------------------------------------------------------------------
// Memory barriers for the volatile fallback path.
//
// These must be BOTH compiler barriers and (where the hardware needs it)
// hardware fences. A plain no-op is never correct: it lets the compiler sink
// the payload store past the index store even on a single-core in-order MCU.
// ---------------------------------------------------------------------------
#if !RB_USE_STD_ATOMIC

namespace rb_barrier {

#if defined(__GNUC__) || defined(__clang__)

inline void acquire() noexcept {
   __atomic_thread_fence(__ATOMIC_ACQUIRE);
}
inline void release() noexcept {
   __atomic_thread_fence(__ATOMIC_RELEASE);
}

#elif defined(_MSC_VER)
#include <intrin.h>
#if defined(_M_ARM) || defined(_M_ARM64)
inline void acquire() noexcept {
   __dmb(_ARM_BARRIER_ISH);
}
inline void release() noexcept {
   __dmb(_ARM_BARRIER_ISH);
}
#else
// x86/x64 gives acquire/release ordering in hardware; a compiler
// barrier is sufficient.
inline void acquire() noexcept {
   _ReadWriteBarrier();
}
inline void release() noexcept {
   _ReadWriteBarrier();
}
#endif

#elif defined(__ICCARM__)
#include <intrinsics.h>
inline void acquire() noexcept {
   __DMB();
   asm volatile("" ::: "memory");
}
inline void release() noexcept {
   asm volatile("" ::: "memory");
   __DMB();
}

#elif defined(__CC_ARM)
inline void acquire() noexcept {
   __dmb(0xF);
   __schedule_barrier();
}
inline void release() noexcept {
   __schedule_barrier();
   __dmb(0xF);
}

#else
#error "ring_buffer.hpp: unknown compiler — no memory barrier available. \
Provide rb_barrier::acquire()/release() for your toolchain, or build with \
<atomic> support. Silently using no-ops here would miscompile."
#endif

} // namespace rb_barrier

#endif // !RB_USE_STD_ATOMIC

namespace rb_detail {

// ---------------------------------------------------------------------------
// Smallest unsigned type that can hold an index in [0, Size).
//
// This matters on 8/16-bit targets: a 16-bit std::size_t on AVR is loaded and
// stored in two instructions, so an ISR landing in between yields a torn index
// and a corrupted buffer. Byte and half-word accesses are single-instruction
// on every target this header supports.
// ---------------------------------------------------------------------------
template <std::size_t Size>
struct default_index {
   using type =
   typename std::conditional<(Size <= 256u), std::uint8_t, typename std::conditional<(Size <= 65536u), std::uint16_t, std::uint32_t>::type>::type;
};

#if RB_USE_STD_ATOMIC

template <typename IndexT>
struct Index {
   std::atomic<IndexT> val;

   Index() noexcept
      : val(0) {
   }

   void store_relaxed(IndexT v) noexcept {
      val.store(v, std::memory_order_relaxed);
   }
   void store_release(IndexT v) noexcept {
      val.store(v, std::memory_order_release);
   }

   [[nodiscard]] IndexT load_acquire() const noexcept {
      return val.load(std::memory_order_acquire);
   }
   [[nodiscard]] IndexT load_relaxed() const noexcept {
      return val.load(std::memory_order_relaxed);
   }
};

#else

template <typename IndexT>
struct Index {
   volatile IndexT val;

   Index() noexcept
      : val(0) {
   }

   void store_relaxed(IndexT v) noexcept {
      val = v;
   }

   // Release: the fence goes BEFORE the index store, so every payload write
   // issued earlier is visible before the index that publishes it.
   void store_release(IndexT v) noexcept {
      rb_barrier::release();
      val = v;
   }

   // Acquire: the fence goes AFTER the index load, so no payload read can be
   // hoisted above the index that made it valid.
   [[nodiscard]] IndexT load_acquire() const noexcept {
      const IndexT v = val;
      rb_barrier::acquire();
      return v;
   }

   [[nodiscard]] IndexT load_relaxed() const noexcept {
      return val;
   }
};

#endif // RB_USE_STD_ATOMIC

} // namespace rb_detail

// ---------------------------------------------------------------------------
// Type-erased interface over a RingBuffer<T, Size, IndexT> of any Size.
//
// Lets a consumer (e.g. a driver whose class shouldn't itself be templated
// on a buffer size) hold a reference/pointer to "a ring buffer of T" while
// the concrete RingBuffer, with its compile-time capacity, is owned and
// constructed by the caller. Only the single-element producer/consumer
// operations are exposed here; bulk and zero-copy access still require the
// concrete type.
//
// Cost of inheriting this: every RingBuffer<T, Size, IndexT> instantiation
// gains a vtable pointer (sizeof grows accordingly) plus one indirect call
// per single-element operation when accessed through RingBufferBase&/*. Each
// distinct instantiation also emits its own vtable and out-of-line virtual
// destructor, which is flash cost beyond the usual per-instantiation template
// code bloat. Skip this base class (use the concrete RingBuffer directly) if
// neither the non-templated call site nor the vtable/flash cost is wanted.
// ---------------------------------------------------------------------------
template <typename T>
class RingBufferBase {
   public:
   virtual ~RingBufferBase() = default;

   [[nodiscard]] virtual bool try_push(const T &value) noexcept = 0;
   [[nodiscard]] virtual bool try_pop(T &out) noexcept = 0;

   /**
    * Read the front element without removing it.
    * @return true if an element was available.
    */
   [[nodiscard]] virtual bool peek(T &out) const noexcept = 0;

   [[nodiscard]] virtual std::size_t size() const noexcept = 0;
   [[nodiscard]] virtual bool empty() const noexcept = 0;
   [[nodiscard]] virtual bool full() const noexcept = 0;

   /**
    * Reset to empty. Only safe when neither producer nor consumer is active.
    */
   virtual void clear() noexcept = 0;
};

// ---------------------------------------------------------------------------
// SPSC Ring Buffer
// ---------------------------------------------------------------------------
template <typename T, std::size_t Size, typename IndexT = typename rb_detail::default_index<Size>::type>
class RingBuffer : public RingBufferBase<T> {
   static_assert((Size & (Size - 1)) == 0, "Size must be a power of two");
   static_assert(Size >= 2, "Size must be at least 2");
   static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
   static_assert(std::is_unsigned<IndexT>::value, "IndexT must be an unsigned integer type");
   static_assert(Size - 1 <= static_cast<std::size_t>(static_cast<IndexT>(-1)), "IndexT is too narrow to address Size elements");

#if RB_USE_STD_ATOMIC && defined(__cpp_lib_atomic_is_always_lock_free)
   static_assert(std::atomic<IndexT>::is_always_lock_free, "std::atomic<IndexT> is not lock-free on this target");
#endif

   public:
   using value_type = T;
   using index_type = IndexT;

   RingBuffer() noexcept
      : buffer_{}
      , head_()
      , tail_() {
   }

   RingBuffer(const RingBuffer &) = delete;
   RingBuffer &operator=(const RingBuffer &) = delete;
   RingBuffer(RingBuffer &&) = delete;
   RingBuffer &operator=(RingBuffer &&) = delete;

   // ==================== Producer API ====================
   // All of the below may only be called from the single producer.

   /**
    * Try to push one element. Non-blocking, ISR-safe.
    * @return true if pushed, false if full.
    */
   [[nodiscard]] bool try_push(const T &value) noexcept override {
      const std::size_t head = head_.load_relaxed();
      const std::size_t next = (head + 1u) & mask_;
      if(next == tail_.load_acquire())
         return false; // full
      buffer_[head] = value;
      head_.store_release(static_cast<IndexT>(next));
      return true;
   }

   /**
    * Push one element, spinning until space is available.
    * Do NOT call from an ISR — use try_push().
    */
   void push(const T &value) noexcept {
      while(!try_push(value)) {
      }
   }

   /**
    * Push up to `count` elements from `src`. Uses at most two block copies
    * and a single release fence, regardless of count.
    * @return number of elements actually pushed.
    */
   std::size_t try_push_bulk(const T *src, std::size_t count) noexcept {
      if(src == nullptr || count == 0)
         return 0;

      const std::size_t head = head_.load_relaxed();
      const std::size_t tail = tail_.load_acquire();
      const std::size_t avail = (tail - head - 1u) & mask_;

      if(count > avail)
         count = avail;
      if(count == 0)
         return 0;

      const std::size_t to_end = Size - head;
      const std::size_t first = (count < to_end) ? count : to_end;

      std::memcpy(&buffer_[head], src, first * sizeof(T));
      if(count > first) {
         std::memcpy(&buffer_[0], src + first, (count - first) * sizeof(T));
      }

      head_.store_release(static_cast<IndexT>((head + count) & mask_));
      return count;
   }

   /**
    * Zero-copy producer access, e.g. to hand a region straight to a DMA
    * controller. Returns a pointer to the largest contiguous writable run
    * and sets `len` to its length (possibly 0). Follow with commit_write().
    *
    * The buffer may have more free space that wraps to index 0; call again
    * after committing to get the second run.
    */
   [[nodiscard]] T *write_ptr(std::size_t &len) noexcept {
      const std::size_t head = head_.load_relaxed();
      const std::size_t tail = tail_.load_acquire();
      const std::size_t avail = (tail - head - 1u) & mask_;
      const std::size_t to_end = Size - head;
      len = (avail < to_end) ? avail : to_end;
      return &buffer_[head];
   }

   /**
    * Publish `n` elements written via write_ptr(). `n` must not exceed the
    * `len` returned by that call.
    */
   void commit_write(std::size_t n) noexcept {
      const std::size_t head = head_.load_relaxed();
      head_.store_release(static_cast<IndexT>((head + n) & mask_));
   }

   // ==================== Consumer API ====================
   // All of the below may only be called from the single consumer.

   /**
    * Try to pop one element. Non-blocking, ISR-safe.
    * @return true if popped, false if empty.
    */
   [[nodiscard]] bool try_pop(T &out) noexcept override {
      const std::size_t tail = tail_.load_relaxed();
      if(tail == head_.load_acquire())
         return false; // empty
      out = buffer_[tail];
      tail_.store_release(static_cast<IndexT>((tail + 1u) & mask_));
      return true;
   }

   /**
    * Read the front element without removing it.
    * @return true if an element was available.
    */
   [[nodiscard]] bool peek(T &out) const noexcept override {
      const std::size_t tail = tail_.load_relaxed();
      if(tail == head_.load_acquire())
         return false;
      out = buffer_[tail];
      return true;
   }

   /**
    * Pop one element, spinning until data is available.
    * Do NOT call from an ISR — use try_pop().
    */
   void pop(T &out) noexcept {
      while(!try_pop(out)) {
      }
   }

   /**
    * Pop up to `count` elements into `dst`. Uses at most two block copies
    * and a single release fence.
    * @return number of elements actually popped.
    */
   std::size_t try_pop_bulk(T *dst, std::size_t count) noexcept {
      if(dst == nullptr || count == 0)
         return 0;

      const std::size_t tail = tail_.load_relaxed();
      const std::size_t head = head_.load_acquire();
      const std::size_t avail = (head - tail) & mask_;

      if(count > avail)
         count = avail;
      if(count == 0)
         return 0;

      const std::size_t to_end = Size - tail;
      const std::size_t first = (count < to_end) ? count : to_end;

      std::memcpy(dst, &buffer_[tail], first * sizeof(T));
      if(count > first) {
         std::memcpy(dst + first, &buffer_[0], (count - first) * sizeof(T));
      }

      tail_.store_release(static_cast<IndexT>((tail + count) & mask_));
      return count;
   }

   /**
    * Zero-copy consumer access. Returns a pointer to the largest contiguous
    * readable run and sets `len` to its length (possibly 0). Follow with
    * commit_read().
    */
   [[nodiscard]] const T *read_ptr(std::size_t &len) const noexcept {
      const std::size_t tail = tail_.load_relaxed();
      const std::size_t head = head_.load_acquire();
      const std::size_t avail = (head - tail) & mask_;
      const std::size_t to_end = Size - tail;
      len = (avail < to_end) ? avail : to_end;
      return &buffer_[tail];
   }

   /**
    * Release `n` elements consumed via read_ptr(). `n` must not exceed the
    * `len` returned by that call.
    */
   void commit_read(std::size_t n) noexcept {
      const std::size_t tail = tail_.load_relaxed();
      tail_.store_release(static_cast<IndexT>((tail + n) & mask_));
   }

   // ==================== Inspection ====================
   //
   // These read both indices non-atomically. Each side only ever observes a
   // stale value of the other side's index, and staleness is conservative in
   // both directions: a producer never over-estimates free space, a consumer
   // never over-estimates available data. Safe to call from either side, but
   // treat the result as a lower bound on what you can do.

   [[nodiscard]] std::size_t size() const noexcept override {
      const std::size_t h = head_.load_acquire();
      const std::size_t t = tail_.load_acquire();
      return (h - t) & mask_;
   }

   [[nodiscard]] static constexpr std::size_t capacity() noexcept {
      return Size - 1; // one slot reserved to distinguish full from empty
   }

   [[nodiscard]] std::size_t space_available() const noexcept {
      return capacity() - size();
   }

   [[nodiscard]] bool empty() const noexcept override {
      return head_.load_acquire() == tail_.load_acquire();
   }

   [[nodiscard]] bool full() const noexcept override {
      const std::size_t h = head_.load_acquire();
      return ((h + 1u) & mask_) == tail_.load_acquire();
   }

   /**
    * Reset to empty. Only safe when neither producer nor consumer is active
    * — i.e. with the relevant interrupt disabled.
    */
   void clear() noexcept override {
      head_.store_relaxed(0);
      tail_.store_relaxed(0);
   }

   private:
   static constexpr std::size_t mask_ = Size - 1;

   T buffer_[Size];
   alignas(RB_INDEX_ALIGNMENT) rb_detail::Index<IndexT> head_;
   alignas(RB_INDEX_ALIGNMENT) rb_detail::Index<IndexT> tail_;
};

// Convenience aliases for common byte buffers
using ByteRing8 = RingBuffer<std::uint8_t, 8>;
using ByteRing16 = RingBuffer<std::uint8_t, 16>;
using ByteRing32 = RingBuffer<std::uint8_t, 32>;
using ByteRing64 = RingBuffer<std::uint8_t, 64>;
using ByteRing128 = RingBuffer<std::uint8_t, 128>;
using ByteRing256 = RingBuffer<std::uint8_t, 256>;
using ByteRing512 = RingBuffer<std::uint8_t, 512>;
using ByteRing1024 = RingBuffer<std::uint8_t, 1024>;
