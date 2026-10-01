#pragma once

/**
 * @file
 * @brief art::expected, art::unexpected, art::unexpect: std::expected in a build with ART_USE_STD_EXPECTED_SPAN=1,
 * libArT's own minimal implementation otherwise.
 *
 * See utils/compat/config.h for how the choice is made. The own implementation exists because etl::expected keeps
 * its value in an etl::variant: sizeof(etl::expected<char, E>) is 24 bytes instead of 2, and a typical call chain
 * compiles to 69 % more code (libArT issue #26). The own implementation has the layout of std::expected (a union of
 * T and E, followed by a bool) and generates the same code.
 *
 * It implements the subset of std::expected that libArT and libArT-proto use:
 * - construction from a value (implicit), from art::unexpected (also through class template argument deduction,
 *   return art::unexpected(error)), with std::in_place or art::unexpect, and default construction;
 *   expected<void, E> is default constructed with return {}.
 * - copy and move, and assignment of a value, an unexpected or another expected, between both states.
 * - has_value(), operator bool, operator*, operator->, value(), error(), value_or().
 * - operator== (and != before C++20) with another expected, with a value and with an unexpected.
 *
 * Not implemented: emplace(), swap(), the monadic functions (and_then(), transform(), ...), error_or(), conversion
 * from an expected of another type, and bad_expected_access.
 *
 * expected<T, E> is trivially copyable when T and E are, so that returning it costs no more than returning the
 * union. Otherwise it has user-provided copy and move operations, which are deleted when T or E cannot be copied
 * or moved; a move-only T (for example storage::File) gives a move-only expected.
 *
 * Differences to std::expected, because libArT is built without exceptions:
 * - value() on an error calls __builtin_trap() instead of throwing bad_expected_access. The two build modes
 *   therefore fail differently: with ART_USE_STD_EXPECTED_SPAN=1 and -fno-exceptions, libstdc++'s std::expected
 *   calls abort() instead (and throws if exceptions are on). Do not rely on either; check has_value() first.
 * - Assignment between the two states is not exception safe. It destroys the old member before it constructs the
 *   new one.
 * As in std::expected, operator*, operator-> and error() do not check the state.
 */

#include "utils/compat/config.h"

#if ART_USE_STD_EXPECTED_SPAN

#include <expected>

namespace art {

using std::expected;
using std::unexpect;
using std::unexpect_t;
using std::unexpected;

} // namespace art

#else

#include "utils/compat/type_traits.h"
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace art {

/** @brief Tag type to construct an expected in the error state, like std::unexpect_t. */
struct unexpect_t {
   explicit unexpect_t() = default;
};

/** @brief Tag to construct an expected in the error state, like std::unexpect. */
inline constexpr unexpect_t unexpect{};

namespace detail {

/**
 * a == b for the comparisons of expected and unexpected. std::expected writes a == b as well; its -Wsign-compare
 * warnings (expected<size_t, E> == 0) never reach the user because libstdc++ is a system header. For two arithmetic
 * operands this converts both to their common type explicitly. For operands of different types, std::common_type_t is
 * the type that the usual arithmetic conversions of the built-in == give. For two operands of the same type it is
 * that type, where == first promotes a type smaller than int (uint8_t, bool) to int; the promotion keeps the value,
 * so the result is the same. -1 is still equal to 0xFFFFFFFFu, and not equal to uint8_t{ 255 }; only the implicit
 * conversion that the warning is about becomes explicit. Every other operand pair uses its own operator==.
 *
 * Known limit: enums are not arithmetic types, so an unscoped enum compared with an int or with another enum still goes
 * through a == b here, and can still warn (-Wsign-compare, -Wenum-compare) where std::expected is silent. The result
 * is always the one of std::expected. The explicit path is not extended to unscoped enums because it would bypass an
 * operator== that the user declares for the enum. To avoid the warning, compare with a value of the same enum type,
 * or cast explicitly.
 */
template <typename A, typename B>
constexpr bool expected_equal(const A &a, const B &b) {
   if constexpr(std::is_arithmetic_v<A> && std::is_arithmetic_v<B>) {
      using Common = std::common_type_t<A, B>;
      return static_cast<Common>(a) == static_cast<Common>(b);
   } else {
      return a == b;
   }
}

} // namespace detail

/**
 * @brief Holds the error that an expected is constructed or assigned from, like std::unexpected.
 */
template <typename E>
class unexpected {
   public:
   static_assert(std::is_object_v<E> && !std::is_array_v<E> && !std::is_const_v<E> && !std::is_volatile_v<E>,
                 "unexpected<E> needs a non-array, non-const, non-volatile object type");

   unexpected(const unexpected &) = default;
   unexpected(unexpected &&) = default;

   template <typename Err = E,
             std::enable_if_t<!std::is_same_v<remove_cvref_t<Err>, unexpected> && !std::is_same_v<remove_cvref_t<Err>, std::in_place_t> && std::is_constructible_v<E, Err>, int> = 0>
   constexpr explicit unexpected(Err &&error)
      : error_(std::forward<Err>(error)) {}

   template <typename... Args, std::enable_if_t<std::is_constructible_v<E, Args...>, int> = 0>
   constexpr explicit unexpected(std::in_place_t, Args &&...args)
      : error_(std::forward<Args>(args)...) {}

   unexpected &operator=(const unexpected &) = default;
   unexpected &operator=(unexpected &&) = default;

   constexpr const E &error() const & noexcept {
      return error_;
   }
   constexpr E &error() & noexcept {
      return error_;
   }
   constexpr const E &&error() const && noexcept {
      return std::move(error_);
   }
   constexpr E &&error() && noexcept {
      return std::move(error_);
   }

   template <typename E2>
   friend constexpr bool operator==(const unexpected &x, const unexpected<E2> &y) {
      return detail::expected_equal(x.error(), y.error());
   }
#if !defined(__cpp_impl_three_way_comparison)
   template <typename E2>
   friend constexpr bool operator!=(const unexpected &x, const unexpected<E2> &y) {
      return !detail::expected_equal(x.error(), y.error());
   }
#endif

   private:
   E error_;
};

template <typename E>
unexpected(E) -> unexpected<E>;

template <typename T, typename E>
class expected;

namespace detail {

/** Stands for the (absent) value of expected<void, E>, so that both share one storage implementation. */
struct expected_void_value {};

template <typename T>
struct is_unexpected : std::false_type {};
template <typename E>
struct is_unexpected<unexpected<E> > : std::true_type {};

template <typename T>
struct is_expected : std::false_type {};
template <typename T, typename E>
struct is_expected<expected<T, E> > : std::true_type {};

[[noreturn]] inline void expected_bad_access() noexcept {
   // value() on an expected that holds an error. std::expected throws, or calls abort() without exceptions.
   __builtin_trap();
}

/**
 * The value or the error, and which one it is. The primary template is used when T and E are trivially copyable:
 * the anonymous union and therefore this struct are trivially copyable, and every special member is trivial.
 */
template <typename T, typename E, bool = std::is_trivially_copyable_v<T> && std::is_trivially_copyable_v<E> >
struct expected_storage {
   union {
      T value_;
      E error_;
   };
   bool hasValue_;

   template <typename... Args>
   constexpr explicit expected_storage(std::in_place_t, Args &&...args)
      : value_(std::forward<Args>(args)...)
      , hasValue_(true) {}

   template <typename... Args>
   constexpr explicit expected_storage(unexpect_t, Args &&...args)
      : error_(std::forward<Args>(args)...)
      , hasValue_(false) {}
};

/**
 * T or E is not trivially copyable: the union members have to be constructed, assigned and destroyed by hand. A
 * copy or move operation of this struct fails to compile only when it is used; expected deletes the ones that T or
 * E does not support (expected_enable_construct, expected_enable_assign), so that type traits report them correctly.
 */
template <typename T, typename E>
struct expected_storage<T, E, false> {
   union {
      T value_;
      E error_;
   };
   bool hasValue_;

   template <typename... Args>
   explicit expected_storage(std::in_place_t, Args &&...args)
      : value_(std::forward<Args>(args)...)
      , hasValue_(true) {}

   template <typename... Args>
   explicit expected_storage(unexpect_t, Args &&...args)
      : error_(std::forward<Args>(args)...)
      , hasValue_(false) {}

   expected_storage(const expected_storage &other)
      : hasValue_(other.hasValue_) {
      if(hasValue_) {
         ::new(static_cast<void *>(std::addressof(value_))) T(other.value_);
      } else {
         ::new(static_cast<void *>(std::addressof(error_))) E(other.error_);
      }
   }

   expected_storage(expected_storage &&other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                                       std::is_nothrow_move_constructible_v<E>)
      : hasValue_(other.hasValue_) {
      if(hasValue_) {
         ::new(static_cast<void *>(std::addressof(value_))) T(std::move(other.value_));
      } else {
         ::new(static_cast<void *>(std::addressof(error_))) E(std::move(other.error_));
      }
   }

   expected_storage &operator=(const expected_storage &other) {
      if(other.hasValue_) {
         assignValue(*this, other.value_);
      } else {
         assignError(*this, other.error_);
      }
      return *this;
   }

   expected_storage &operator=(expected_storage &&other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                                                  std::is_nothrow_move_assignable_v<T> &&
                                                                  std::is_nothrow_move_constructible_v<E> &&
                                                                  std::is_nothrow_move_assignable_v<E>) {
      if(other.hasValue_) {
         assignValue(*this, std::move(other.value_));
      } else {
         assignError(*this, std::move(other.error_));
      }
      return *this;
   }

   ~expected_storage() {
      if(hasValue_) {
         value_.~T();
      } else {
         error_.~E();
      }
   }
};

/** Assign a value to @p storage: assign it if storage holds a value, otherwise replace the error with it. */
template <typename Storage, typename U>
void assignValue(Storage &storage, U &&value) {
   using T = decltype(storage.value_);
   using E = decltype(storage.error_);
   if(storage.hasValue_) {
      storage.value_ = std::forward<U>(value);
   } else {
      storage.error_.~E();
      ::new(static_cast<void *>(std::addressof(storage.value_))) T(std::forward<U>(value));
      storage.hasValue_ = true;
   }
}

/** Assign an error to @p storage: assign it if storage holds an error, otherwise replace the value with it. */
template <typename Storage, typename G>
void assignError(Storage &storage, G &&error) {
   using T = decltype(storage.value_);
   using E = decltype(storage.error_);
   if(storage.hasValue_) {
      storage.value_.~T();
      ::new(static_cast<void *>(std::addressof(storage.error_))) E(std::forward<G>(error));
      storage.hasValue_ = false;
   } else {
      storage.error_ = std::forward<G>(error);
   }
}

/**
 * Empty bases that delete the copy and move operations of expected that T or E do not support. expected defaults
 * its own, and a defaulted operation is deleted when the one of a base is. Constructors and assignments are
 * separate bases, as in std::expected: a T that can be copy constructed but not assigned (a struct with a const
 * member) keeps the copy and move constructors of expected and loses only its assignments.
 */
template <bool Copy, bool Move>
struct expected_enable_construct {};

template <>
struct expected_enable_construct<false, true> {
   expected_enable_construct() = default;
   expected_enable_construct(const expected_enable_construct &) = delete;
   expected_enable_construct(expected_enable_construct &&) = default;
   expected_enable_construct &operator=(const expected_enable_construct &) = default;
   expected_enable_construct &operator=(expected_enable_construct &&) = default;
};

template <>
struct expected_enable_construct<false, false> {
   expected_enable_construct() = default;
   expected_enable_construct(const expected_enable_construct &) = delete;
   expected_enable_construct(expected_enable_construct &&) = delete;
   expected_enable_construct &operator=(const expected_enable_construct &) = default;
   expected_enable_construct &operator=(expected_enable_construct &&) = default;
};

template <bool Copy, bool Move>
struct expected_enable_assign {};

template <>
struct expected_enable_assign<false, true> {
   expected_enable_assign() = default;
   expected_enable_assign(const expected_enable_assign &) = default;
   expected_enable_assign(expected_enable_assign &&) = default;
   expected_enable_assign &operator=(const expected_enable_assign &) = delete;
   expected_enable_assign &operator=(expected_enable_assign &&) = default;
};

template <>
struct expected_enable_assign<false, false> {
   expected_enable_assign() = default;
   expected_enable_assign(const expected_enable_assign &) = default;
   expected_enable_assign(expected_enable_assign &&) = default;
   expected_enable_assign &operator=(const expected_enable_assign &) = delete;
   expected_enable_assign &operator=(expected_enable_assign &&) = delete;
};

// Assignment between the two states constructs the new member, so it needs construction as well. Like
// std::expected, it also needs T or E to have a nothrow move constructor (std needs that for its exception
// guarantee). The own type has no such guarantee, but keeps the condition so that code which compiles here also
// compiles with std::expected. For expected<void, E> the condition always holds.
template <typename T>
inline constexpr bool expected_copy_assignable_v = std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>;
template <typename T>
inline constexpr bool expected_move_assignable_v = std::is_move_constructible_v<T> && std::is_move_assignable_v<T>;
template <typename T, typename E>
inline constexpr bool expected_nothrow_move_one_of_v =
std::is_nothrow_move_constructible_v<T> || std::is_nothrow_move_constructible_v<E>;

template <typename T, typename E>
using expected_enable_construct_for =
expected_enable_construct<std::is_copy_constructible_v<T> && std::is_copy_constructible_v<E>,
                          std::is_move_constructible_v<T> && std::is_move_constructible_v<E> >;

template <typename T, typename E>
using expected_enable_assign_for =
expected_enable_assign<expected_copy_assignable_v<T> && expected_copy_assignable_v<E> && expected_nothrow_move_one_of_v<T, E>,
                       expected_move_assignable_v<T> && expected_move_assignable_v<E> && expected_nothrow_move_one_of_v<T, E> >;

/** A value argument U of expected<T, E>: not a tag, not an expected and not an unexpected. */
template <typename T, typename U>
inline constexpr bool expected_value_argument_v =
!std::is_same_v<remove_cvref_t<U>, std::in_place_t> && !std::is_same_v<remove_cvref_t<U>, unexpect_t> &&
!is_expected<remove_cvref_t<U> >::value && !is_unexpected<remove_cvref_t<U> >::value && std::is_constructible_v<T, U>;

} // namespace detail

/**
 * @brief Holds either a value of type T or an error of type E, like std::expected. See the file comment for the
 * supported subset.
 */
template <typename T, typename E>
class expected : private detail::expected_storage<T, E>,
                 private detail::expected_enable_construct_for<T, E>,
                 private detail::expected_enable_assign_for<T, E> {
   using Storage = detail::expected_storage<T, E>;

   static_assert(std::is_object_v<T> && !std::is_array_v<T> && !std::is_reference_v<T>,
                 "expected<T, E> needs an object type T (or void, see the specialization)");
   static_assert(!std::is_same_v<remove_cvref_t<T>, std::in_place_t> &&
                 !std::is_same_v<remove_cvref_t<T>, unexpect_t> && !detail::is_unexpected<remove_cvref_t<T> >::value,
                 "expected<T, E>: T must not be a tag or an unexpected");

   public:
   using value_type = T;
   using error_type = E;
   using unexpected_type = unexpected<E>;

   template <typename U = T, std::enable_if_t<std::is_default_constructible_v<U>, int> = 0>
   constexpr expected()
      : Storage(std::in_place) {}

   expected(const expected &) = default;
   expected(expected &&) = default;
   expected &operator=(const expected &) = default;
   expected &operator=(expected &&) = default;

   template <typename U = T, std::enable_if_t<detail::expected_value_argument_v<T, U> && std::is_convertible_v<U, T>, int> = 0>
   constexpr expected(U &&value)
      : Storage(std::in_place, std::forward<U>(value)) {}

   template <typename U = T, std::enable_if_t<detail::expected_value_argument_v<T, U> && !std::is_convertible_v<U, T>, int> = 0>
   constexpr explicit expected(U &&value)
      : Storage(std::in_place, std::forward<U>(value)) {}

   template <typename G, std::enable_if_t<std::is_convertible_v<const G &, E>, int> = 0>
   constexpr expected(const unexpected<G> &error)
      : Storage(unexpect, error.error()) {}

   template <typename G, std::enable_if_t<std::is_constructible_v<E, const G &> && !std::is_convertible_v<const G &, E>, int> = 0>
   constexpr explicit expected(const unexpected<G> &error)
      : Storage(unexpect, error.error()) {}

   template <typename G, std::enable_if_t<std::is_convertible_v<G, E>, int> = 0>
   constexpr expected(unexpected<G> &&error)
      : Storage(unexpect, std::move(error).error()) {}

   template <typename G, std::enable_if_t<std::is_constructible_v<E, G> && !std::is_convertible_v<G, E>, int> = 0>
   constexpr explicit expected(unexpected<G> &&error)
      : Storage(unexpect, std::move(error).error()) {}

   template <typename... Args, std::enable_if_t<std::is_constructible_v<T, Args...>, int> = 0>
   constexpr explicit expected(std::in_place_t, Args &&...args)
      : Storage(std::in_place, std::forward<Args>(args)...) {}

   template <typename... Args, std::enable_if_t<std::is_constructible_v<E, Args...>, int> = 0>
   constexpr explicit expected(unexpect_t, Args &&...args)
      : Storage(unexpect, std::forward<Args>(args)...) {}

   template <typename U = T, std::enable_if_t<detail::expected_value_argument_v<T, U> && std::is_assignable_v<T &, U>, int> = 0>
   expected &operator=(U &&value) {
      detail::assignValue(static_cast<Storage &>(*this), std::forward<U>(value));
      return *this;
   }

   template <typename G, std::enable_if_t<std::is_constructible_v<E, const G &>, int> = 0>
   expected &operator=(const unexpected<G> &error) {
      detail::assignError(static_cast<Storage &>(*this), error.error());
      return *this;
   }

   template <typename G, std::enable_if_t<std::is_constructible_v<E, G>, int> = 0>
   expected &operator=(unexpected<G> &&error) {
      detail::assignError(static_cast<Storage &>(*this), std::move(error).error());
      return *this;
   }

   constexpr bool has_value() const noexcept {
      return this->hasValue_;
   }
   constexpr explicit operator bool() const noexcept {
      return this->hasValue_;
   }

   constexpr const T *operator->() const noexcept {
      return std::addressof(this->value_);
   }
   constexpr T *operator->() noexcept {
      return std::addressof(this->value_);
   }

   constexpr const T &operator*() const & noexcept {
      return this->value_;
   }
   constexpr T &operator*() & noexcept {
      return this->value_;
   }
   constexpr const T &&operator*() const && noexcept {
      return std::move(this->value_);
   }
   constexpr T &&operator*() && noexcept {
      return std::move(this->value_);
   }

   /** The value. Traps (__builtin_trap()) if this holds an error; see the file comment for std::expected. */
   constexpr const T &value() const & {
      if(!this->hasValue_) {
         detail::expected_bad_access();
      }
      return this->value_;
   }
   constexpr T &value() & {
      if(!this->hasValue_) {
         detail::expected_bad_access();
      }
      return this->value_;
   }
   constexpr const T &&value() const && {
      if(!this->hasValue_) {
         detail::expected_bad_access();
      }
      return std::move(this->value_);
   }
   constexpr T &&value() && {
      if(!this->hasValue_) {
         detail::expected_bad_access();
      }
      return std::move(this->value_);
   }

   constexpr const E &error() const & noexcept {
      return this->error_;
   }
   constexpr E &error() & noexcept {
      return this->error_;
   }
   constexpr const E &&error() const && noexcept {
      return std::move(this->error_);
   }
   constexpr E &&error() && noexcept {
      return std::move(this->error_);
   }

   template <typename U>
   constexpr T value_or(U &&defaultValue) const & {
      return this->hasValue_ ? this->value_ : static_cast<T>(std::forward<U>(defaultValue));
   }
   template <typename U>
   constexpr T value_or(U &&defaultValue) && {
      return this->hasValue_ ? std::move(this->value_) : static_cast<T>(std::forward<U>(defaultValue));
   }

   template <typename T2, typename E2, std::enable_if_t<!std::is_void_v<T2>, int> = 0>
   friend constexpr bool operator==(const expected &x, const expected<T2, E2> &y) {
      if(x.has_value() != y.has_value()) {
         return false;
      }
      return x.has_value() ? detail::expected_equal(*x, *y) : detail::expected_equal(x.error(), y.error());
   }

   template <typename T2, std::enable_if_t<!detail::is_expected<T2>::value && !detail::is_unexpected<T2>::value, int> = 0>
   friend constexpr bool operator==(const expected &x, const T2 &value) {
      return x.has_value() && detail::expected_equal(*x, value);
   }

   template <typename E2>
   friend constexpr bool operator==(const expected &x, const unexpected<E2> &error) {
      return !x.has_value() && detail::expected_equal(x.error(), error.error());
   }

#if !defined(__cpp_impl_three_way_comparison)
   // Before C++20 the compiler does not rewrite a != b and b == a in terms of a == b.
   template <typename T2, typename E2, std::enable_if_t<!std::is_void_v<T2>, int> = 0>
   friend constexpr bool operator!=(const expected &x, const expected<T2, E2> &y) {
      return !(x == y);
   }
   template <typename T2, std::enable_if_t<!detail::is_expected<T2>::value && !detail::is_unexpected<T2>::value, int> = 0>
   friend constexpr bool operator!=(const expected &x, const T2 &value) {
      return !(x == value);
   }
   template <typename T2, std::enable_if_t<!detail::is_expected<T2>::value && !detail::is_unexpected<T2>::value, int> = 0>
   friend constexpr bool operator==(const T2 &value, const expected &x) {
      return x == value;
   }
   template <typename T2, std::enable_if_t<!detail::is_expected<T2>::value && !detail::is_unexpected<T2>::value, int> = 0>
   friend constexpr bool operator!=(const T2 &value, const expected &x) {
      return !(x == value);
   }
   template <typename E2>
   friend constexpr bool operator!=(const expected &x, const unexpected<E2> &error) {
      return !(x == error);
   }
   template <typename E2>
   friend constexpr bool operator==(const unexpected<E2> &error, const expected &x) {
      return x == error;
   }
   template <typename E2>
   friend constexpr bool operator!=(const unexpected<E2> &error, const expected &x) {
      return !(x == error);
   }
#endif
};

/**
 * @brief expected<void, E>: success without a value, or an error of type E, like std::expected<void, E>.
 * return {} constructs it in the success state.
 */
template <typename E>
class expected<void, E> : private detail::expected_storage<detail::expected_void_value, E>,
                          private detail::expected_enable_construct_for<detail::expected_void_value, E>,
                          private detail::expected_enable_assign_for<detail::expected_void_value, E> {
   using Storage = detail::expected_storage<detail::expected_void_value, E>;

   public:
   using value_type = void;
   using error_type = E;
   using unexpected_type = unexpected<E>;

   constexpr expected() noexcept
      : Storage(std::in_place) {}

   expected(const expected &) = default;
   expected(expected &&) = default;
   expected &operator=(const expected &) = default;
   expected &operator=(expected &&) = default;

   template <typename G, std::enable_if_t<std::is_convertible_v<const G &, E>, int> = 0>
   constexpr expected(const unexpected<G> &error)
      : Storage(unexpect, error.error()) {}

   template <typename G, std::enable_if_t<std::is_constructible_v<E, const G &> && !std::is_convertible_v<const G &, E>, int> = 0>
   constexpr explicit expected(const unexpected<G> &error)
      : Storage(unexpect, error.error()) {}

   template <typename G, std::enable_if_t<std::is_convertible_v<G, E>, int> = 0>
   constexpr expected(unexpected<G> &&error)
      : Storage(unexpect, std::move(error).error()) {}

   template <typename G, std::enable_if_t<std::is_constructible_v<E, G> && !std::is_convertible_v<G, E>, int> = 0>
   constexpr explicit expected(unexpected<G> &&error)
      : Storage(unexpect, std::move(error).error()) {}

   constexpr explicit expected(std::in_place_t) noexcept
      : Storage(std::in_place) {}

   template <typename... Args, std::enable_if_t<std::is_constructible_v<E, Args...>, int> = 0>
   constexpr explicit expected(unexpect_t, Args &&...args)
      : Storage(unexpect, std::forward<Args>(args)...) {}

   template <typename G, std::enable_if_t<std::is_constructible_v<E, const G &>, int> = 0>
   expected &operator=(const unexpected<G> &error) {
      detail::assignError(static_cast<Storage &>(*this), error.error());
      return *this;
   }

   template <typename G, std::enable_if_t<std::is_constructible_v<E, G>, int> = 0>
   expected &operator=(unexpected<G> &&error) {
      detail::assignError(static_cast<Storage &>(*this), std::move(error).error());
      return *this;
   }

   constexpr bool has_value() const noexcept {
      return this->hasValue_;
   }
   constexpr explicit operator bool() const noexcept {
      return this->hasValue_;
   }

   /** Does nothing, like std::expected<void, E>::operator*(). */
   constexpr void operator*() const noexcept {}

   /** Traps (__builtin_trap()) if this holds an error; see the file comment for std::expected. */
   constexpr void value() const {
      if(!this->hasValue_) {
         detail::expected_bad_access();
      }
   }

   constexpr const E &error() const & noexcept {
      return this->error_;
   }
   constexpr E &error() & noexcept {
      return this->error_;
   }
   constexpr const E &&error() const && noexcept {
      return std::move(this->error_);
   }
   constexpr E &&error() && noexcept {
      return std::move(this->error_);
   }

   template <typename E2>
   friend constexpr bool operator==(const expected &x, const expected<void, E2> &y) {
      if(x.has_value() != y.has_value()) {
         return false;
      }
      return x.has_value() || detail::expected_equal(x.error(), y.error());
   }

   template <typename E2>
   friend constexpr bool operator==(const expected &x, const unexpected<E2> &error) {
      return !x.has_value() && detail::expected_equal(x.error(), error.error());
   }

#if !defined(__cpp_impl_three_way_comparison)
   template <typename E2>
   friend constexpr bool operator!=(const expected &x, const expected<void, E2> &y) {
      return !(x == y);
   }
   template <typename E2>
   friend constexpr bool operator!=(const expected &x, const unexpected<E2> &error) {
      return !(x == error);
   }
   template <typename E2>
   friend constexpr bool operator==(const unexpected<E2> &error, const expected &x) {
      return x == error;
   }
   template <typename E2>
   friend constexpr bool operator!=(const unexpected<E2> &error, const expected &x) {
      return !(x == error);
   }
#endif
};

} // namespace art

#endif
