#pragma once

#include <limits>
#include <utils/systick.h>

namespace utils {
/**
 * Timeout is a utility object used for operations that can timeout
 *
 * Almost all operations on an embedded device have time limits. A timeout is a time in the future by
 * which a (set of) operation(s) should be finished.
 *
 */
class Timeout {
   const timestamp_t _deadline;

   public:
   /**
    * Create a new Timeout from a duration
    *
    * Explicit to make sure not every number is implicitly converted to a timeout
    *
    * \param time The amount of time to wait before the timeout expires
    *
    * */
   explicit Timeout(duration_t time)
      : _deadline(getSysTick() + time) {
   }

   inline bool hasExpired() const {
      return _deadline < getSysTick();
   }

   /**
    * Get the time in milliseconds until this timeout expires
    *
    * @return the number pf milliseconds to the deadline, 0 if the timeout expired
    */
   inline duration_t getDurationLeft() const {
      timestamp_t now = getSysTick();
      if(_deadline <= now) {
         return 0;
      }
      return _deadline - now;
   }

   /**
    * Allow implicit conversion to a duration_t
    */

   inline operator duration_t() const {
      return getDurationLeft();
   }

   /**
    * Get the deadline for this Timeout
    */
   inline timestamp_t getDeadline() const {
      return _deadline;
   }
};

static_assert(sizeof(Timeout) == sizeof(timestamp_t), "A Timeout should only contain a timestamp_t");

} // namespace utils

/**
 * Create a timeout in milliseconds
 */
inline utils::Timeout operator""_ms(unsigned long long timeout) {
   return utils::Timeout(timeout);
}

/**
 * Create a timeout in seconds
 */
inline utils::Timeout operator""_s(unsigned long long timeout) {
   return utils::Timeout(timeout * 1000);
}
