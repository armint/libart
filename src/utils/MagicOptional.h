#pragma once
#include <cstdint>
#include <utility>

/**
 * @class MagicOptional
 * @brief A class designed to persist information across reboots by storing it in volatile memory that remains uninitialized.
 *
 * The MagicOptional class provides a mechanism for retaining data that needs to survive a system reboot.
 * This can be particularly useful for applications requiring persistent state without necessarily using
 * non-volatile storage mechanisms, assuming the data is placed in a memory region that remains intact and
 * uninitialized after a reboot.
 *
 * @tparam T The type of the data to be stored.
 *
 * Usage example:
 * @code
 * constexpr uint32_t MAGIC_NUMBER = 0x600DF00D;
 * [[gnu::section(".noinit.BSRAM.magicoptional.test")]] MagicOptional<int> persistentCounter(MAGIC_NUMBER);
 * if (!persistentCounter.has_value()) {
 *     persistentCounter = 0; // Initialize if not already set
 * } else {
 *     persistentCounter = persistentCounter.value() + 1; // Increment the counter
 * }
 * int currentValue = persistentCounter.value(); // Retrieve the current value
 * @endcode
 *
 * @note Ensure that the memory region used for this data is not overwritten or re-initialized during the system's
 * boot sequence to maintain data persistence.
 */
template <typename T>
class MagicOptional {
   public:
   static constexpr uint32_t MAGIC_RESET_VALUE = 0;

   private:
   std::aligned_storage<sizeof(uint32_t), alignof(uint32_t)> _magicNumberStorage;
   typename std::aligned_storage<sizeof(T), alignof(T)>::type _storage;
   const uint32_t _magicNumber;

   /**
    * Constructs a MagicOptional object with the specified magic number.
    *
    * @tparam T The type of the value to be stored in the MagicOptional.
    * @param magicNumber The magic number used to initialize the MagicOptional object.
    * This number must not be equal to MAGIC_RESET_VALUE.
    */
   public:
   constexpr MagicOptional(uint32_t magicNumber);

   /**
    * @brief Copy constructor for MagicOptional class.
    *
    * This constructor initializes the current MagicOptional object
    * by copying another MagicOptional object.
    *
    * @param other The MagicOptional object to copy from.
    */
   MagicOptional(MagicOptional &other);

   /**
    * @brief Destructor for MagicOptional class.
    *
    * This destructor ensures that any held value is properly destructed.
    */
   ~MagicOptional();

   /**
    * @brief Assignment operator for MagicOptional class.
    *
    * This operator assigns the provided value to the MagicOptional instance,
    * initializing it if it wasn't already initialized.
    *
    * @param value The value to be assigned to the MagicOptional instance.
    * @return A reference to the MagicOptional instance after the assignment.
    */
   MagicOptional &operator=(const T &value);

   /**
    * Constructs and stores a new value in place using the provided arguments.
    *
    * This method destructs any value currently held by the MagicOptional
    * instance and constructs a new value in place using the parameters provided.
    *
    * @tparam Args Parameter pack that represents the types of arguments used to
    * construct the new value.
    * @param args Arguments used to construct the new value.
    */
   template <typename... Args>
   void emplace(Args &&...args) {
      // Make sure we call the destructor of a possible previous value
      reset();
      // Construct the new value in place
      new(&_storage) T(std::forward<Args>(args)...);
      reinterpret_cast<uint32_t &>(_magicNumberStorage) = _magicNumber;
   }


   /**
    * Provides access to the stored value.
    *
    * This method returns a reference to the stored value.
    *
    * @return A reference to the stored value.
    */
   T &value();

   /**
    * Checks if the MagicOptional instance currently holds a value.
    *
    * @retval true if the instance contains a value.
    * @retval false otherwise.
    */
   bool has_value() const;

   /**
    * Resets the MagicOptional instance, destroying the stored value if present.
    *
    * This method ensures that the stored value is destructed and the internal state is reset.
    */
   void reset();

   /**
    * Provides access to the stored value via dereference operator.
    *
    * If the MagicOptional object contains a value, this operator returns
    * a reference to the stored value.
    *
    * @return A reference to the stored value.
    */
   constexpr T &operator*() {
      return value();
   }

   /**
    * Provides access to the stored value via constant dereference operator.
    *
    * If the MagicOptional object contains a value, this operator returns
    * a constant reference to the stored value.
    *
    * @return A constant reference to the stored value.
    */
   constexpr const T &operator*() const {
      return value();
   }

   /**
    * Provides access to the stored value via pointer dereference operator.
    *
    * If the MagicOptional object contains a value, this operator returns
    * a pointer to the stored value, enabling member access through
    * the arrow operator.
    *
    * @return Pointer to the stored value.
    */
   constexpr T *operator->() {
      return &value();
   }

   /**
    * Provides access to the stored value via constant pointer dereference operator.
    *
    * If the MagicOptional object contains a value, this operator returns
    * a constant pointer to the stored value, enabling const member access through
    * the arrow operator.
    *
    * @return Constant pointer to the stored value.
    */
   constexpr const T *operator->() const {
      return &value();
   }

   /**
    * Checks if the MagicOptional instance currently holds a value.
    *
    * This operator returns true if the instance contains a value and false otherwise.
    *
    * @retval true if the instance contains a value.
    * @retval false otherwise.
    */
   operator bool() const {
      return has_value();
   }
};

template <typename T>
constexpr MagicOptional<T>::MagicOptional(uint32_t magicNumber)
   : _magicNumber(magicNumber) {
   assert(magicNumber != MAGIC_RESET_VALUE);
}

template <typename T>
MagicOptional<T>::MagicOptional(MagicOptional &other)
   : _magicNumber(other._magicNumber) {
   reset();
   if(other.has_value()) {
      reinterpret_cast<uint32_t &>(_magicNumberStorage) = other._magicNumber;
      new(&_storage) T(other.value());
   }
}

template <typename T>
MagicOptional<T>::~MagicOptional() {
   reset();
}

template <typename T>
MagicOptional<T> &MagicOptional<T>::operator=(const T &value) {
   if(has_value()) {
      this->value() = value;
   } else {
      new(&_storage) T(value);
      reinterpret_cast<uint32_t &>(_magicNumberStorage) = _magicNumber;
   }
   return *this;
}

template <typename T>
T &MagicOptional<T>::value() {
   assert(has_value());
   return reinterpret_cast<T &>(_storage);
}

template <typename T>
void MagicOptional<T>::reset() {
   if(has_value()) {
      value().~T();
   }
   reinterpret_cast<uint32_t &>(_magicNumberStorage) = MAGIC_RESET_VALUE;
}

template <typename T>
bool MagicOptional<T>::has_value() const {
   return reinterpret_cast<const uint32_t &>(_magicNumberStorage) == _magicNumber;
}