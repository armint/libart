#pragma once

#include <limits>
#include <utils/systick.h>

namespace utils {
/**
 * @class Timeout
 * @brief Utility class for managing operation timeouts on an embedded device.
 *
 * The Timeout class is used to represent a time limit by which certain operations should be completed.
 * It helps in managing timed operations by providing mechanisms to check if the timeout has expired,
 * and to retrieve the remaining time.
 */
class Timeout {
   timestamp_t _deadline;

   public:
   /**
    * @brief Constructs a Timeout object from a duration.
    *
    * This constructor is explicit to prevent implicit conversion from integer
    * types to Timeout, ensuring that time values are given intentionally.
    *
    * @param time The duration of the timeout in system ticks.
    */
   explicit Timeout(duration_t time)
      : _deadline(getSysTick() + time) {}

   /**
    * @brief Checks if the timeout has expired.
    *
    * @return Returns true if the current system tick is past the deadline, false otherwise.
    */
   inline bool hasExpired() const {
      return _deadline <= getSysTick();
   }

   /**
    * @brief Retrieves the remaining duration until the timeout expires.
    *
    * @return The number of system ticks remaining until the deadline. Returns 0 if the timeout has expired.
    */
   inline duration_t getDurationLeft() const {
      timestamp_t now = getSysTick();
      if(_deadline <= now) {
         return 0;
      }
      return _deadline - now;
   }

   /**
    * @brief Allows implicit conversion to duration_t.
    *
    * This operator enables the Timeout object to be implicitly converted to a duration_t,
    * representing the remaining time until the timeout expires.
    *
    * @return The remaining duration until the timeout expires.
    */
   inline operator duration_t() const {
      return getDurationLeft();
   }

   /**
    * @brief Gets the deadline for this Timeout.
    *
    * @return The system tick value representing the deadline for the timeout.
    */
   inline timestamp_t getDeadline() const {
      return _deadline;
   }
};

/**
 * @brief Ensures that the Timeout class contains only a single timestamp_t.
 */
static_assert(sizeof(Timeout) == sizeof(timestamp_t), "A Timeout should only contain a timestamp_t");

} // namespace utils

/**
 * @brief User-defined literal operator for creating Timeout in milliseconds.
 *
 * This operator allows the creation of a Timeout object using the _ms suffix.
 * Example: 100_ms creates a Timeout of 100 milliseconds.
 *
 * @param timeout The duration in milliseconds.
 * @return A Timeout object representing the specified duration.
 */
inline utils::Timeout operator""_ms(unsigned long long timeout) {
   return utils::Timeout(timeout);
}

/**
 * @brief User-defined literal operator for creating Timeout in seconds.
 *
 * This operator allows the creation of a Timeout object using the _s suffix.
 * Example: 2_s creates a Timeout of 2 seconds.
 *
 * @param timeout The duration in seconds.
 * @return A Timeout object representing the specified duration.
 */
inline utils::Timeout operator""_s(unsigned long long timeout) {
   return utils::Timeout(timeout * 1000);
}