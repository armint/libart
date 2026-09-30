// Tests of the compat headers (src/utils/compat/). This file is compiled three times (tests/CMakeLists.txt):
// - into libArT-tests (C++23, ART_USE_STD_EXPECTED_SPAN derived: the std aliases),
// - into libArT-compat-tests-cpp17 (C++17: the own expected and etl::span),
// - into libArT-compat-tests-cpp23-own (C++23 with ART_USE_STD_EXPECTED_SPAN=0: the own types next to the std ones,
//   so that their sizes can be compared).
// The cases follow the call patterns of the code base (see libArT issue #26, check 2).

#include <doctest/doctest.h>

#include "utils/compat/bit.h"
#include "utils/compat/expected.h"
#include "utils/compat/span.h"
#include "utils/compat/type_traits.h"

#if ART_COMPAT_HAS_STD_EXPECTED_SPAN && !ART_USE_STD_EXPECTED_SPAN
#include <expected>
#endif

#if !ART_USE_STD_EXPECTED_SPAN && defined(__unix__)
#include <csignal>
#include <sys/resource.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#include <sys/wait.h>
#include <unistd.h>
#define ART_TEST_EXPECTED_TRAP 1
#endif

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {

enum class Error : uint8_t { Io = 1, NoEntry = 2, Full = 3 };

// The own types must follow the build-wide choice; the std ones only in a C++23 build that selects them.
#if ART_USE_STD_EXPECTED_SPAN
static_assert(std::is_same_v<art::expected<int, Error>, std::expected<int, Error>>);
static_assert(std::is_same_v<art::unexpected<Error>, std::unexpected<Error>>);
static_assert(std::is_same_v<art::span<int>, std::span<int>>);
#else
static_assert(std::is_same_v<art::span<int>, etl::span<int>>);
#endif

// ---------- type_traits.h ----------

static_assert(std::is_same_v<art::remove_cvref_t<const int &>, int>);
static_assert(std::is_same_v<art::remove_cvref_t<volatile int &&>, int>);
static_assert(std::is_same_v<art::remove_cvref_t<const int *const>, const int *>);
static_assert(std::is_same_v<art::remove_cvref_t<int[3]>, int[3]>);
static_assert(std::is_same_v<art::remove_cvref<const long &>::type, long>);
#if defined(__cpp_lib_remove_cvref)
static_assert(std::is_same_v<art::remove_cvref_t<const volatile Error &>, std::remove_cvref_t<const volatile Error &>>);
#endif

// ---------- bit.h ----------

// art::bit_cast is constexpr at every standard (__builtin_bit_cast).
static_assert(art::bit_cast<uint32_t>(1.0f) == 0x3F800000u);
static_assert(art::bit_cast<float>(uint32_t{ 0x40000000u }) == 2.0f);
static_assert(art::bit_cast<int32_t>(uint32_t{ 0xFFFFFFFFu }) == -1);

TEST_CASE("[compat] bit_cast reinterprets the object representation") {
   CHECK(art::bit_cast<uint32_t>(1.0f) == 0x3F800000u);
   CHECK(art::bit_cast<float>(uint32_t{ 0xC0490FDBu }) == -3.14159274f);
   CHECK(art::bit_cast<uint64_t>(art::bit_cast<double>(uint64_t{ 0x0123456789ABCDEFu })) == 0x0123456789ABCDEFu);

   struct Pair {
      uint16_t low;
      uint16_t high;
   };
   const auto pair = art::bit_cast<Pair>(uint32_t{ 0x11223344u });
   uint32_t roundTrip = 0;
   std::memcpy(&roundTrip, &pair, sizeof(roundTrip));
   CHECK(roundTrip == 0x11223344u);
}

TEST_CASE("[compat] is_little_endian matches the byte order in memory") {
   const uint16_t value = 0x0102;
   uint8_t bytes[2] = {};
   std::memcpy(bytes, &value, sizeof(value));
   CHECK(art::is_little_endian == (bytes[0] == 0x02));
}

// ---------- span.h ----------

// Generated getters in libArT-proto deduce the span with CTAD from a pointer and a size.
art::span<const uint8_t> viewOf(const uint8_t *data, size_t size) {
   return art::span(data, size);
}

size_t sum(art::span<const uint8_t> data) {
   size_t total = 0;
   for(const uint8_t byte : data) {
      total += byte;
   }
   return total;
}

struct TableEntry {
   uint16_t id;
   uint8_t width;
};
constexpr std::array<TableEntry, 3> kTable{ { { 1, 2 }, { 2, 4 }, { 7, 1 } } };
constexpr art::span<const TableEntry> kTableView{ kTable };
static_assert(kTableView.size() == 3);
static_assert(kTableView[2].id == 7);

static_assert(sizeof(art::span<uint8_t>) == 2 * sizeof(void *));

TEST_CASE("[compat] span construction and conversion") {
   uint8_t raw[4] = { 1, 2, 3, 4 };
   std::array<uint8_t, 3> array{ 10, 20, 30 };
   std::vector<uint8_t> vector{ 5, 6 };

   art::span<uint8_t> fromArray(raw);
   CHECK(fromArray.size() == 4);
   CHECK(fromArray.data() == raw);

   // span<T> -> span<const T>, from C array, std::array and std::vector.
   CHECK(sum(fromArray) == 10);
   CHECK(sum(array) == 60);
   CHECK(sum(vector) == 11);
   CHECK(sum(viewOf(raw, 2)) == 3);

   art::span<uint8_t, 4> fixed(raw);
   art::span<uint8_t> dynamic = fixed;
   CHECK(dynamic.size() == 4);
   CHECK(art::dynamic_extent == static_cast<size_t>(-1));

   fromArray[1] = 42;
   CHECK(raw[1] == 42);
   CHECK(!fromArray.empty());
   CHECK(art::span<uint8_t>().empty());
}

TEST_CASE("[compat] span first, last and subspan") {
   const uint8_t raw[6] = { 0, 1, 2, 3, 4, 5 };
   art::span<const uint8_t> all(raw);

   CHECK(all.first(2).size() == 2);
   CHECK(all.first(2)[1] == 1);
   CHECK(all.last(2)[0] == 4);
   CHECK(all.subspan(1, 3).size() == 3);
   CHECK(all.subspan(1, 3)[0] == 1);
   CHECK(all.subspan(4).size() == 2);
   CHECK(all.front() == 0);
   CHECK(all.back() == 5);
   CHECK(all.size_bytes() == 6);
}

TEST_CASE("[compat] constexpr span over a constexpr table") {
   uint32_t widths = 0;
   for(const TableEntry &entry : kTableView) {
      widths += entry.width;
   }
   CHECK(widths == 7);
}

// ---------- expected.h ----------

static_assert(sizeof(art::expected<char, Error>) == 2);
static_assert(sizeof(art::expected<void, Error>) == 2);
static_assert(sizeof(art::expected<uint32_t, Error>) == 8);
static_assert(std::is_trivially_destructible_v<art::expected<size_t, Error>>);
#if !ART_USE_STD_EXPECTED_SPAN
// Guaranteed by the own implementation only; libstdc++'s std::expected has a user-provided copy assignment.
static_assert(std::is_trivially_copyable_v<art::expected<char, Error>>);
static_assert(std::is_trivially_copyable_v<art::expected<void, Error>>);
static_assert(std::is_trivially_copyable_v<art::expected<size_t, Error>>);
#endif

#if ART_COMPAT_HAS_STD_EXPECTED_SPAN && !ART_USE_STD_EXPECTED_SPAN
// The own implementation has the layout of std::expected.
static_assert(sizeof(art::expected<char, Error>) == sizeof(std::expected<char, Error>));
static_assert(sizeof(art::expected<void, Error>) == sizeof(std::expected<void, Error>));
static_assert(sizeof(art::expected<size_t, Error>) == sizeof(std::expected<size_t, Error>));
static_assert(sizeof(art::expected<uint64_t, Error>) == sizeof(std::expected<uint64_t, Error>));
static_assert(alignof(art::expected<uint64_t, Error>) == alignof(std::expected<uint64_t, Error>));
#endif

// Usable in constant expressions (construction, copy, observers, comparison), with std and with the own type.
struct Point {
   int x;
   int y;
};
constexpr art::expected<int, Error> kValue = 4;
constexpr art::expected<int, Error> kError = art::unexpected(Error::Io);
constexpr art::expected<Point, Error> kPoint(std::in_place, Point{ 1, 2 });
constexpr art::expected<void, Error> kVoid;
constexpr art::expected<void, Error> kVoidError(art::unexpect, Error::Full);

constexpr art::expected<char, Error> narrow(art::expected<int, Error> in) {
   if(!in) {
      return art::unexpected(in.error());
   }
   return static_cast<char>(*in);
}

static_assert(kValue.has_value() && static_cast<bool>(kValue));
static_assert(*kValue == 4 && kValue.value() == 4 && kValue.value_or(9) == 4);
static_assert(!kError.has_value() && kError.error() == Error::Io && kError.value_or(9) == 9);
static_assert(kPoint->y == 2 && (*kPoint).x == 1);
static_assert(kVoid.has_value() && !kVoidError.has_value() && kVoidError.error() == Error::Full);
static_assert(*narrow(kValue) == 4 && narrow(kError).error() == Error::Io);
static_assert(kValue == 4 && kError == art::unexpected(Error::Io) && kValue != kError);
static_assert(art::unexpected(Error::Io).error() == Error::Io);

// A move-only resource with a non-trivial destructor, like storage::File. It counts the live instances, so that the
// tests see that every state change destroys the old member.
class MoveOnly {
   public:
   static int live;

   explicit MoveOnly(int handle)
      : handle_(handle) {
      ++live;
   }
   MoveOnly(MoveOnly &&other) noexcept
      : handle_(std::exchange(other.handle_, 0)) {
      ++live;
   }
   MoveOnly &operator=(MoveOnly &&other) noexcept {
      handle_ = std::exchange(other.handle_, 0);
      return *this;
   }
   MoveOnly(const MoveOnly &) = delete;
   MoveOnly &operator=(const MoveOnly &) = delete;
   ~MoveOnly() {
      --live;
   }

   int handle() const {
      return handle_;
   }

   private:
   int handle_;
};
int MoveOnly::live = 0;

static_assert(!std::is_copy_constructible_v<art::expected<MoveOnly, Error>>);
static_assert(!std::is_copy_assignable_v<art::expected<MoveOnly, Error>>);
static_assert(std::is_move_constructible_v<art::expected<MoveOnly, Error>>);
static_assert(std::is_move_assignable_v<art::expected<MoveOnly, Error>>);
static_assert(!std::is_trivially_copyable_v<art::expected<MoveOnly, Error>>);

// A copyable, non-trivial type that counts its live instances.
struct Counted {
   static int live;
   int value;

   Counted(int v)
      : value(v) {
      ++live;
   }
   Counted(const Counted &other)
      : value(other.value) {
      ++live;
   }
   Counted &operator=(const Counted &) = default;
   ~Counted() {
      --live;
   }
};
int Counted::live = 0;

static_assert(std::is_copy_constructible_v<art::expected<Counted, Error>>);
static_assert(!std::is_trivially_copyable_v<art::expected<Counted, Error>>);

// Copy constructible but not assignable (a const member), trivially copyable and not. Like std::expected, expected
// keeps its copy and move constructors for such a T and loses only the assignments.
struct ConstMember {
   const int id;
};

struct ConstCounted {
   const int id;
   Counted counted;
};

static_assert(std::is_copy_constructible_v<art::expected<ConstMember, Error>>);
static_assert(std::is_move_constructible_v<art::expected<ConstMember, Error>>);
static_assert(!std::is_copy_assignable_v<art::expected<ConstMember, Error>>);
static_assert(!std::is_move_assignable_v<art::expected<ConstMember, Error>>);
static_assert(std::is_copy_constructible_v<art::expected<ConstCounted, Error>>);
static_assert(std::is_move_constructible_v<art::expected<ConstCounted, Error>>);
static_assert(!std::is_copy_assignable_v<art::expected<ConstCounted, Error>>);
static_assert(!std::is_move_assignable_v<art::expected<ConstCounted, Error>>);

// T and E with potentially-throwing move constructors: like std::expected, expected can be constructed but not
// assigned, so that code that compiles at C++17 also compiles at C++23.
struct ThrowingMove {
   ThrowingMove() = default;
   ThrowingMove(const ThrowingMove &) noexcept(false) {}
   ThrowingMove(ThrowingMove &&) noexcept(false) {}
   ThrowingMove &operator=(const ThrowingMove &) = default;
   ThrowingMove &operator=(ThrowingMove &&) = default;
};

static_assert(std::is_copy_constructible_v<art::expected<ThrowingMove, ThrowingMove>>);
static_assert(std::is_move_constructible_v<art::expected<ThrowingMove, ThrowingMove>>);
static_assert(!std::is_copy_assignable_v<art::expected<ThrowingMove, ThrowingMove>>);
static_assert(!std::is_move_assignable_v<art::expected<ThrowingMove, ThrowingMove>>);
// One nothrow move constructor (here the error enum) is enough.
static_assert(std::is_copy_assignable_v<art::expected<ThrowingMove, Error>>);
static_assert(std::is_move_assignable_v<art::expected<ThrowingMove, Error>>);
static_assert(std::is_copy_assignable_v<art::expected<void, ThrowingMove>>);
static_assert(std::is_move_assignable_v<art::expected<void, ThrowingMove>>);

// Readable::read(): a virtual function that returns expected<char, E>, with error propagation through a caller.
class Source {
   public:
   virtual ~Source() = default;
   virtual art::expected<char, Error> read() = 0;
   virtual art::expected<size_t, Error> read(art::span<uint8_t> buffer) = 0;
};

class ArraySource : public Source {
   public:
   explicit ArraySource(art::span<const uint8_t> data)
      : data_(data) {}

   art::expected<char, Error> read() override {
      if(position_ >= data_.size()) {
         return art::unexpected(Error::NoEntry);
      }
      return static_cast<char>(data_[position_++]);
   }

   art::expected<size_t, Error> read(art::span<uint8_t> buffer) override {
      size_t count = 0;
      while(count < buffer.size()) {
         auto byte = read();
         if(!byte) {
            if(count == 0) {
               return art::unexpected(byte.error());
            }
            break;
         }
         buffer[count++] = static_cast<uint8_t>(*byte);
      }
      return count;
   }

   private:
   art::span<const uint8_t> data_;
   size_t position_ = 0;
};

// Propagates the error of an expected<char, E> into an expected<uint16_t, E>.
art::expected<uint16_t, Error> readWord(Source &source) {
   auto low = source.read();
   if(!low.has_value()) {
      return art::unexpected(low.error());
   }
   auto high = source.read();
   if(!high) {
      return art::unexpected<Error>(high.error());
   }
   return static_cast<uint16_t>(static_cast<uint8_t>(*low) | (static_cast<uint8_t>(high.value()) << 8));
}

art::expected<void, Error> check(bool ok) {
   if(!ok) {
      return art::unexpected(Error::Io);
   }
   return {};
}

art::expected<MoveOnly, Error> open(int handle) {
   if(handle < 0) {
      return art::unexpected(Error::NoEntry);
   }
   return MoveOnly(handle);
}

template <typename T>
art::expected<T, Error> twice(const art::expected<T, Error> &in) {
   if(!in) {
      return art::unexpected(in.error());
   }
   return static_cast<T>(*in * 2);
}

TEST_CASE("[compat] expected: value, error and propagation through a virtual interface") {
   const uint8_t bytes[] = { 0x34, 0x12, 0x56 };
   ArraySource source{ art::span<const uint8_t>(bytes) };

   auto word = readWord(source);
   REQUIRE(word.has_value());
   CHECK(*word == 0x1234);
   CHECK(word.value() == 0x1234);

   auto second = readWord(source);
   CHECK_FALSE(second.has_value());
   CHECK_FALSE(static_cast<bool>(second));
   CHECK(second.error() == Error::NoEntry);
   CHECK(second.value_or(uint16_t{ 7 }) == 7);

   ArraySource again{ art::span<const uint8_t>(bytes) };
   uint8_t buffer[8] = {};
   auto count = again.read(art::span<uint8_t>(buffer));
   REQUIRE(count);
   CHECK(*count == 3);
   CHECK(buffer[2] == 0x56);
   auto empty = again.read(art::span<uint8_t>(buffer));
   REQUIRE_FALSE(empty);
   CHECK(empty.error() == Error::NoEntry);
}

TEST_CASE("[compat] expected<void, E>") {
   auto ok = check(true);
   CHECK(ok.has_value());
   CHECK(static_cast<bool>(ok));
   ok.value();
   *ok;

   auto failed = check(false);
   CHECK_FALSE(failed);
   CHECK(failed.error() == Error::Io);

   art::expected<void, Error> assigned;
   assigned = failed;
   CHECK(assigned.error() == Error::Io);
   assigned = ok;
   CHECK(assigned.has_value());
   assigned = art::unexpected(Error::Full);
   CHECK(assigned.error() == Error::Full);

   const art::expected<void, Error> byTag(art::unexpect, Error::NoEntry);
   CHECK(byTag.error() == Error::NoEntry);
   CHECK(art::expected<void, Error>(std::in_place).has_value());
}

TEST_CASE("[compat] expected: construction forms") {
   art::expected<int, Error> byDefault;
   CHECK(byDefault.has_value());
   CHECK(*byDefault == 0);

   art::expected<int, Error> implicitValue = 5;
   CHECK(*implicitValue == 5);

   art::expected<int, Error> inPlace(std::in_place, 9);
   CHECK(*inPlace == 9);

   art::expected<int, Error> byTag(art::unexpect, Error::Full);
   CHECK(byTag.error() == Error::Full);

   const art::unexpected<Error> error(Error::Io);
   art::expected<int, Error> fromLvalueUnexpected = error;
   CHECK(fromLvalueUnexpected.error() == Error::Io);
   CHECK(error.error() == Error::Io);

   // unexpected<G> with G convertible to E.
   art::expected<int, int> widened = art::unexpected<uint8_t>(uint8_t{ 200 });
   CHECK(widened.error() == 200);

   art::expected<Point, Error> point(std::in_place, Point{ 3, 4 });
   CHECK(point->x == 3);
   CHECK((*point).y == 4);
   point->x = 8;
   CHECK(point.value().x == 8);
}

TEST_CASE("[compat] expected: value_or") {
   const art::expected<int, Error> value = 3;
   const art::expected<int, Error> error = art::unexpected(Error::Io);
   CHECK(value.value_or(0) == 3);
   CHECK(error.value_or(0) == 0);
   CHECK(error.value_or(short{ 1 }) == 1);

   // Rvalue overload with a move-only value.
   CHECK(open(4).value_or(MoveOnly(0)).handle() == 4);
   CHECK(open(-1).value_or(MoveOnly(11)).handle() == 11);
   CHECK(MoveOnly::live == 0);
}

TEST_CASE("[compat] expected: move-only value like storage::File") {
   {
      auto file = open(3);
      REQUIRE(file);
      CHECK(file->handle() == 3);
      CHECK(MoveOnly::live == 1);

      art::expected<MoveOnly, Error> moved = std::move(file);
      CHECK(moved->handle() == 3);
      CHECK(file->handle() == 0);

      MoveOnly taken = std::move(moved).value();
      CHECK(taken.handle() == 3);
      MoveOnly takenByStar = *open(8);
      CHECK(takenByStar.handle() == 8);

      auto missing = open(-1);
      CHECK(missing.error() == Error::NoEntry);

      // Move assignment between the states: the value is destroyed, then the error constructed, and back.
      moved = std::move(missing);
      CHECK_FALSE(moved);
      CHECK(moved.error() == Error::NoEntry);
      CHECK(MoveOnly::live == 3); // file (moved from), taken, takenByStar

      moved = open(5);
      CHECK(moved->handle() == 5);
      CHECK(MoveOnly::live == 4);

      moved = art::unexpected(Error::Io);
      CHECK(moved.error() == Error::Io);
      CHECK(MoveOnly::live == 3);

      moved = MoveOnly(6);
      CHECK(moved->handle() == 6);
      moved = MoveOnly(7);
      CHECK(moved->handle() == 7);
      CHECK(MoveOnly::live == 4);
   }
   CHECK(MoveOnly::live == 0);
}

TEST_CASE("[compat] expected: copy assignment between value and error states") {
   {
      art::expected<Counted, Error> value = Counted(1);
      art::expected<Counted, Error> error = art::unexpected(Error::Io);
      CHECK(Counted::live == 1);

      art::expected<Counted, Error> target = value;
      CHECK(Counted::live == 2);
      target = error;
      CHECK_FALSE(target);
      CHECK(Counted::live == 1);
      target = value;
      CHECK(target->value == 1);
      CHECK(Counted::live == 2);
      target = Counted(4);
      CHECK(target->value == 4);
      CHECK(Counted::live == 2);
      const auto &self = target;
      target = self;
      CHECK(target->value == 4);
      CHECK(Counted::live == 2);

      // Trivially copyable: the same through the trivial copy.
      art::expected<int, Error> small = 1;
      const art::expected<int, Error> smallError = art::unexpected(Error::Full);
      small = smallError;
      CHECK(small.error() == Error::Full);
      small = 2;
      CHECK(*small == 2);
   }
   CHECK(Counted::live == 0);
}

TEST_CASE("[compat] expected of a copy constructible, non-assignable value") {
   {
      const art::expected<ConstMember, Error> original(std::in_place, ConstMember{ 5 });
      art::expected<ConstMember, Error> copy = original;
      CHECK(copy->id == 5);
      art::expected<ConstMember, Error> moved = std::move(copy);
      CHECK(moved->id == 5);

      const art::expected<ConstCounted, Error> counted(std::in_place, ConstCounted{ 7, Counted(1) });
      CHECK(Counted::live == 1);
      art::expected<ConstCounted, Error> countedCopy = counted;
      CHECK(countedCopy->id == 7);
      CHECK(Counted::live == 2);
      const art::expected<ConstCounted, Error> error = art::unexpected(Error::Io);
      art::expected<ConstCounted, Error> errorCopy = error;
      CHECK(errorCopy.error() == Error::Io);
      CHECK(Counted::live == 2);
   }
   CHECK(Counted::live == 0);
}

#if ART_TEST_EXPECTED_TRAP
// value() on an error must trap (__builtin_trap(): SIGILL on x86, SIGTRAP on Arm), never return and never throw. A
// child process calls it; the parent checks that the child was killed by SIGILL or SIGTRAP. Any other end fails the
// test: another fatal signal (abort(), a null-pointer write), a normal exit, or a hang (the alarm raises SIGALRM after
// 5 s). Only for the own type: std::expected throws here (the tests build with exceptions).
TEST_CASE("[compat] expected: value() on an error traps") {
   // The signal that ended the child, or 0 if it exited.
   const auto terminatingSignal = [](auto call) {
      const pid_t child = fork();
      REQUIRE(child >= 0);
      if(child == 0) {
         // doctest's crash handler would report the trap as a failed test in the child; die by the signal instead.
         std::signal(SIGILL, SIG_DFL);
         std::signal(SIGTRAP, SIG_DFL);
         // SIGILL and SIGTRAP dump core by default; the expected trap must leave no core file. With a core_pattern that
         // pipes to a handler (systemd-coredump), the kernel still starts the handler and leaves RLIMIT_CORE to it, so
         // on Linux the child is also made non-dumpable, which skips the core dump completely.
         const rlimit noCore{ 0, 0 };
         setrlimit(RLIMIT_CORE, &noCore);
#if defined(__linux__)
         prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif
         alarm(5);
         call();
         _exit(0);
      }
      int status = 0;
      REQUIRE(waitpid(child, &status, 0) == child);
      return WIFSIGNALED(status) ? WTERMSIG(status) : 0;
   };
   const auto isTrap = [](int signal) { return signal == SIGILL || signal == SIGTRAP; };

   const int valueSignal = terminatingSignal([] {
      art::expected<int, Error> error = art::unexpected(Error::Io);
      static_cast<void>(error.value());
   });
   CHECK(isTrap(valueSignal));
   const int voidSignal = terminatingSignal([] {
      const art::expected<void, Error> error = art::unexpected(Error::Io);
      error.value();
   });
   CHECK(isTrap(voidSignal));
   const int moveOnlySignal = terminatingSignal([] { static_cast<void>(open(-1).value()); });
   CHECK(isTrap(moveOnlySignal));
   // The check itself: value() on a value returns normally.
   CHECK(terminatingSignal([] { static_cast<void>(open(1).value()); }) == 0);
}
#endif

TEST_CASE("[compat] expected in a template") {
   CHECK(*twice(art::expected<int, Error>(21)) == 42);
   CHECK(twice(art::expected<uint8_t, Error>(art::unexpect, Error::Io)).error() == Error::Io);
}

TEST_CASE("[compat] expected and unexpected comparison") {
   const art::expected<int, Error> three = 3;
   const art::expected<long, Error> threeLong = 3L;
   const art::expected<int, Error> io = art::unexpected(Error::Io);

   CHECK(three == 3);
   CHECK(3 == three);
   CHECK(three != 4);
   CHECK_FALSE(io == 3);
   CHECK(three == threeLong);
   CHECK(three != io);
   CHECK(io == art::unexpected(Error::Io));
   CHECK(art::unexpected(Error::Io) == io);
   CHECK(io != art::unexpected(Error::Full));
   CHECK(three != art::unexpected(Error::Io));
   CHECK(art::unexpected(Error::Io) == art::unexpected(Error::Io));
   CHECK(art::unexpected(Error::Io) != art::unexpected(Error::Full));

   CHECK(check(true) == check(true));
   CHECK(check(false) == check(false));
   CHECK(check(true) != check(false));
   CHECK(check(false) == art::unexpected(Error::Io));
   CHECK(check(true) != art::unexpected(Error::Io));
}

} // namespace
