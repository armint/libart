#pragma once

#include <chrono>
#include <limits>
#include "systick.h"
#include <type_traits>

namespace utils {

/// A timeout for an operation
///
/// Almost all operations on an embedded device can time out. A timeout is a time in the future by
/// which all operations have to be done. Instead of defining a point of time in the future though
/// is it convenient for the user to define an amount of time within which the operation has to be
/// performed. All Timeout constructors take a duration from which a deadline is constructed using
/// the current time.
///
/// Usage when defining a relative period from now:
/// \code
/// void busyWait(utils::Timeout timeout) {
///    while(!timeout) {}
/// }
///
/// void a() {
///    // Wait a second
///    busyWait(utils::Timeout(1000));
///
///    // Wait another second, this time letting the compiler convert a duration into a Timeout
///    busyWait(1_s);
///    // Wait another second, this time letting the compiler convert a duration into a Timeout
///    busyWait(1000_ms);
///
///    using namespace std::chrono_literals;
///    // Wait another second, but use the suffix from std::chrono_literals
///    busyWait(1s);
///    // Wait 2 minutes using a std::chrono_literals suffix
///    busyWait(2min);
///    // The line below doesn't compile commented out because nanoseconds isn't convertible into
///    // milliseconds without losing precision, and milliseconds is the tick rate currently used
///    // busyWait(10ns);
/// }
/// \endcode
///
/// When an event is guaranteed to come or the thread is blocked until something happens:
/// \code
/// void a() {
///    busyWait(utils::Timeout::Infinity());
///    busyWait(utils::Timeout::infinity);
/// }
/// \endcode
///
/// Example of how to use in a driver with a semaphore:
/// \code
/// int Peripheral::read(utils::Timeout timeout) {
///    if(timeout.isInfinite()) {
///       sem.wait();
///    } else {
///       // Convert the timeout back to a duration_t
///       if(sem.timed_wait(timeout) != result::ok) {
///          return TIMEOUT_ERROR;
///       }
///    }
///    return receiveBuffer.get();
/// }
/// \endcode
///
/// The timeout can be re-used, that way can a timeout be set for a sequence of operations:
/// \code
/// int a(uint8_t *data, utils::Timeout timeout) {
///    Peripheral p;
///    // All 5 reads below have to happen within the passed timeout or the operation fails
///    for (size_t i=0; i<5; ++i) {
///       data[i] = p.read(timeout);
///       if (data[i] == TIMEOUT_ERROR) {
///          return TIMEOUT_ERROR;
///       }
///    }
///    return 0;
/// }
///
/// b() {
///    uint8_t data[5];
///    // This call to a returns 0 only when all reads happened within 1 second from the start of
///    // the call
///    if (a(data, 1_s) == 0) {
///       // Use the read bytes from data
///    } else {
///       // Deal with a failed read
///    }
/// }
/// \endcode
///
/// This object doesn't work if the timestamp_t value overflows. In practice this shouldn't happen
/// since timestamp_t is an int64_t
class Timeout {
   const timestamp_t _deadline;

   public:
   /// Create a new Timeout from a duration
   ///
   /// Making this explicit means that no every number can be directly converted into a timeout.
   ///
   /// \param time The amount of time to wait before the operation times out
   explicit Timeout(duration_t time)
      : _deadline(getSysTick() + time) {
   }

   /// Create a new Timeout from a std::chrono::duration
   ///
   /// This constructor allows using the std::chrono_literals suffixes to define timeouts. Since
   /// duration_cast isn't used to convert to std::chrono::duration<duration_t, std::milli> but the
   /// constructor (https://en.cppreference.com/w/cpp/chrono/duration/duration) should it be
   /// impossible to create a timeout from a duration that isn't convertible to milliseconds
   /// without losing precision. For example passing an instance of std::chrono::nanoseconds
   /// doesn't compile but using std::chrono::seconds does.
   ///
   /// The constructor call is forwarded to the explicit constructor that takes milliseconds.
   ///
   /// \tparam T The duration representation, should be derived automatically
   /// \tparam Ratio The type of the Period for the duration, should be derived automatically
   ///
   /// \param time The duration to convert to milliseconds
   template <typename T, typename Ratio>
   Timeout(std::chrono::duration<T, Ratio> time)
      : Timeout(std::chrono::duration<duration_t, std::milli>(time).count()) {
      static_assert(std::is_convertible<T, duration_t>::value, "The duration has to be convertible to duration_t");
   }

   /// Get if this timeout has expired
   inline bool hasExpired() const {
      return _deadline <= getSysTick();
   }

   /// Get the amount of duration left before this expires
   ///
   /// If the timeout has expired return 0
   inline duration_t getDurationLeft() const {
      timestamp_t now = getSysTick();
      if(_deadline <= now) {
         return 0;
      }
      return _deadline - now;
   }

   /// Allow converting this object into a duration implicitly
   inline operator duration_t() const {
      return getDurationLeft();
   }

   /// Get the deadline for this Timeout
   inline timestamp_t getDeadline() const {
      return _deadline;
   }

   /// A type that can be cast to a special infinity value
   struct Infinity {};
   /// An instance of the Infinity type so you don't have to type ()
   static constexpr Infinity infinity();
   /// Construct a Timeout that will always wait by having an unreachable deadline
   Timeout(Infinity)
      : _deadline(std::numeric_limits<timestamp_t>::max()) {
   }

   /// Get if this timeout was setup to wait infinitely
   inline bool isInfinite() const {
      return _deadline == std::numeric_limits<timestamp_t>::max();
   }
};

static_assert(sizeof(Timeout) == sizeof(timestamp_t), "A Timeout should just be a timestampt_t with some methods");

} // namespace utils

/// Create a timeout while making explicit the passed value is in milliseconds
inline utils::Timeout operator""_ms(unsigned long long timeout) {
   return utils::Timeout(timeout);
}

/// Create a timeout while making explicit the passed value is in seconds
inline utils::Timeout operator""_s(unsigned long long timeout) {
   return utils::Timeout(timeout * 1000);
}
