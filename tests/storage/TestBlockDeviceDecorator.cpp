#include "storage/BlockDeviceDecorator.h"
#include <array>
#include <doctest/doctest.h>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace art::storage;

namespace {

// Lower device that records which methods were called and with what arguments. If failWith is set, every
// method reports that error.
class RecordingDevice : public BlockDevice {
   public:
   art::expected<void, Error> init() override {
      ++initCalls;
      return result();
   }

   art::expected<void, Error> read(offset_t offset, art::span<uint8_t> buffer) override {
      lastReadOffset = offset;
      lastReadSize = buffer.size();
      for(uint8_t &byte : buffer) {
         byte = 0x5Au;
      }
      return result();
   }

   art::expected<void, Error> program(offset_t offset, art::span<const uint8_t> data) override {
      lastProgramOffset = offset;
      lastProgramData.assign(data.begin(), data.end());
      return result();
   }

   art::expected<void, Error> erase(offset_t offset, offset_t length) override {
      lastEraseOffset = offset;
      lastEraseLength = length;
      return result();
   }

   art::expected<void, Error> sync() override {
      ++syncCalls;
      return result();
   }

   Geometry geometry() const override {
      return Geometry{ .readSize = 1u, .programSize = 256u, .eraseSize = 4096u, .size = 0x1'0000'0000ull, .eraseBeforeProgram = true };
   }

   std::optional<Error> failWith;
   uint32_t initCalls{ 0u };
   uint32_t syncCalls{ 0u };
   offset_t lastReadOffset{ 0u };
   size_t lastReadSize{ 0u };
   offset_t lastProgramOffset{ 0u };
   std::vector<uint8_t> lastProgramData;
   offset_t lastEraseOffset{ 0u };
   offset_t lastEraseLength{ 0u };

   private:
   art::expected<void, Error> result() const {
      if(failWith) {
         return art::unexpected(*failWith);
      }
      return {};
   }
};

// Transform device that overrides only read, program and geometry, as the issue requires.
class MinimalTransform : public BlockDeviceDecorator {
   public:
   using BlockDeviceDecorator::BlockDeviceDecorator;

   art::expected<void, Error> read(offset_t offset, art::span<uint8_t> buffer) override {
      return lower().read(offset + 1u, buffer);
   }

   art::expected<void, Error> program(offset_t offset, art::span<const uint8_t> data) override {
      return lower().program(offset + 1u, data);
   }

   Geometry geometry() const override {
      Geometry inner = lower().geometry();
      inner.programSize = 512u;
      return inner;
   }
};

} // namespace

TEST_SUITE("art::storage::BlockDeviceDecorator") {

   TEST_CASE("decorator overriding only read/program/geometry forwards the rest") {
      RecordingDevice lowerDevice;
      MinimalTransform transform{ lowerDevice };
      BlockDevice &device = transform;

      CHECK(device.init().has_value());
      CHECK(lowerDevice.initCalls == 1u);

      CHECK(device.erase(4096u, 8192u).has_value());
      CHECK(lowerDevice.lastEraseOffset == 4096u);
      CHECK(lowerDevice.lastEraseLength == 8192u);

      CHECK(device.sync().has_value());
      CHECK(lowerDevice.syncCalls == 1u);
   }

   TEST_CASE("overridden methods are used, not forwarded blindly") {
      RecordingDevice lowerDevice;
      MinimalTransform transform{ lowerDevice };
      BlockDevice &device = transform;

      std::array<uint8_t, 4> buffer{};
      CHECK(device.read(100u, buffer).has_value());
      CHECK(lowerDevice.lastReadOffset == 101u);
      CHECK(buffer[0] == 0x5Au);

      const std::array<uint8_t, 3> data{ 1u, 2u, 3u };
      CHECK(device.program(200u, data).has_value());
      CHECK(lowerDevice.lastProgramOffset == 201u);
      CHECK(lowerDevice.lastProgramData == std::vector<uint8_t>{ 1u, 2u, 3u });

      const Geometry geometry = device.geometry();
      CHECK(geometry.programSize == 512u);
      CHECK(geometry.eraseSize == 4096u);
      CHECK(geometry.size == 0x1'0000'0000ull);
   }

   TEST_CASE("plain decorator forwards every method with unchanged arguments") {
      RecordingDevice lowerDevice;
      BlockDeviceDecorator passThrough{ lowerDevice };

      CHECK(passThrough.init().has_value());
      CHECK(lowerDevice.initCalls == 1u);

      std::array<uint8_t, 5> buffer{};
      CHECK(passThrough.read(1234u, buffer).has_value());
      CHECK(lowerDevice.lastReadOffset == 1234u);
      CHECK(lowerDevice.lastReadSize == 5u);
      CHECK(buffer[4] == 0x5Au); // the caller's buffer, not a copy, reaches the lower device

      const std::array<uint8_t, 3> data{ 7u, 8u, 9u };
      CHECK(passThrough.program(5678u, data).has_value());
      CHECK(lowerDevice.lastProgramOffset == 5678u);
      CHECK(lowerDevice.lastProgramData == std::vector<uint8_t>{ 7u, 8u, 9u });

      CHECK(passThrough.erase(4096u, 8192u).has_value());
      CHECK(lowerDevice.lastEraseOffset == 4096u);
      CHECK(lowerDevice.lastEraseLength == 8192u);

      CHECK(passThrough.sync().has_value());
      CHECK(lowerDevice.syncCalls == 1u);

      const Geometry geometry = passThrough.geometry();
      CHECK(geometry.readSize == 1u);
      CHECK(geometry.programSize == 256u);
      CHECK(geometry.eraseSize == 4096u);
      CHECK(geometry.size == 0x1'0000'0000ull);
      CHECK(geometry.eraseBeforeProgram);
   }

   TEST_CASE("plain decorator passes a lower-device failure through unchanged") {
      RecordingDevice lowerDevice;
      BlockDeviceDecorator passThrough{ lowerDevice };
      std::array<uint8_t, 2> buffer{};
      const std::array<uint8_t, 2> data{ 1u, 2u };

      for(const Error injected : { Error::Io, Error::Timeout, Error::WriteProtected }) {
         lowerDevice.failWith = injected;
         // parentheses: doctest cannot stringify enum class
         CHECK((passThrough.init().error() == injected));
         CHECK((passThrough.read(0u, buffer).error() == injected));
         CHECK((passThrough.program(0u, data).error() == injected));
         CHECK((passThrough.erase(0u, 4096u).error() == injected));
         CHECK((passThrough.sync().error() == injected));
      }
   }

   TEST_CASE("BlockDevice::sync defaults to success") {
      struct NoSyncDevice : BlockDevice {
         art::expected<void, Error> init() override {
            return {};
         }
         art::expected<void, Error> read(offset_t, art::span<uint8_t>) override {
            return {};
         }
         art::expected<void, Error> program(offset_t, art::span<const uint8_t>) override {
            return {};
         }
         art::expected<void, Error> erase(offset_t, offset_t) override {
            return {};
         }
         Geometry geometry() const override {
            return {};
         }
      } device;
      CHECK(device.sync().has_value());
   }

   TEST_CASE("BlockDevice and BlockDeviceDecorator cannot be copied or moved through the base") {
      // BlockDevice is abstract, so is_*_constructible would be false whatever the access; the concrete
      // BlockDeviceDecorator pins the constructors, and assignability is sensitive to access on both.
      static_assert(!std::is_copy_assignable_v<BlockDevice>);
      static_assert(!std::is_move_assignable_v<BlockDevice>);
      static_assert(!std::is_copy_constructible_v<BlockDeviceDecorator>);
      static_assert(!std::is_move_constructible_v<BlockDeviceDecorator>);
      static_assert(!std::is_copy_assignable_v<BlockDeviceDecorator>);
      static_assert(!std::is_move_assignable_v<BlockDeviceDecorator>);
      CHECK(true);
   }

   TEST_CASE("Geometry defaults") {
      const Geometry geometry{};
      CHECK(geometry.readSize == 1u);
      CHECK(geometry.programSize == 1u);
      CHECK(geometry.eraseSize == 0u);
      CHECK(geometry.size == 0u);
      CHECK_FALSE(geometry.eraseBeforeProgram); // no erase unit, so erase-before-program cannot apply
   }

   TEST_CASE("Geometry::isValid checks the invariants") {
      constexpr Geometry nor{ .readSize = 1u, .programSize = 256u, .eraseSize = 4096u, .size = 1u << 20, .eraseBeforeProgram = true };
      static_assert(nor.isValid());
      CHECK(nor.isValid());

      const Geometry ram{ .size = 1024u };
      CHECK(ram.isValid());
      CHECK_FALSE(Geometry{}.isValid()); // size 0: no useful device, and what failed capacity detection yields
      CHECK_FALSE(Geometry{ .readSize = 512u, .programSize = 512u, .eraseSize = 4096u, .eraseBeforeProgram = true }.isValid());

      Geometry broken = nor;
      broken.eraseSize = 0u; // erase-before-program without an erase unit
      CHECK_FALSE(broken.isValid());
      broken = nor;
      broken.readSize = 0u;
      CHECK_FALSE(broken.isValid());
      broken = nor;
      broken.programSize = 0u;
      CHECK_FALSE(broken.isValid());
      broken = nor;
      broken.eraseSize = 4000u; // not a multiple of programSize
      CHECK_FALSE(broken.isValid());
      broken = nor;
      broken.size += 1u; // not a multiple of eraseSize
      CHECK_FALSE(broken.isValid());
   }

   TEST_CASE("Geometry range checks enforce alignment and bounds") {
      constexpr Geometry nor{ .readSize = 4u, .programSize = 256u, .eraseSize = 4096u, .size = 8192u, .eraseBeforeProgram = true };

      CHECK(nor.isValidRead(0u, 8192u));
      CHECK(nor.isValidRead(4u, 4u));
      CHECK_FALSE(nor.isValidRead(2u, 4u));
      CHECK_FALSE(nor.isValidRead(0u, 6u));
      CHECK_FALSE(nor.isValidRead(0u, 8196u));
      CHECK_FALSE(nor.isValidRead(8192u, 4u));
      CHECK(nor.isValidRead(8192u, 0u)); // empty range at the end

      CHECK(nor.isValidProgram(256u, 512u));
      CHECK_FALSE(nor.isValidProgram(4u, 256u));
      CHECK_FALSE(nor.isValidProgram(0u, 100u));

      CHECK(nor.isValidErase(4096u, 4096u));
      CHECK_FALSE(nor.isValidErase(256u, 4096u));
      CHECK_FALSE(nor.isValidErase(4096u, 8192u));

      // offset + length would wrap around 64 bits
      CHECK_FALSE(nor.isValidRead(0xFFFF'FFFF'FFFF'FFFCull, 8u));
      CHECK_FALSE(nor.isValidProgram(4096u, 0xFFFF'FFFF'FFFF'FF00ull));
   }

   TEST_CASE("Geometry::isValidErase accepts any in-bounds range without an erase unit") {
      const Geometry ram{ .size = 1000u };
      CHECK(ram.isValidErase(3u, 7u));
      CHECK_FALSE(ram.isValidErase(999u, 2u));
   }

   TEST_CASE("toString covers every error code") {
      struct Entry {
         Error error;
         std::string_view name;
      };
      // One row per enumerator, in declaration order. The loop below fails if an enumerator is added to
      // Error and toString() but not to this table.
      constexpr std::array<Entry, 21> table{ {
      { Error::Io, "Io" },
      { Error::Corrupt, "Corrupt" },
      { Error::NoEntry, "NoEntry" },
      { Error::Exists, "Exists" },
      { Error::NotDirectory, "NotDirectory" },
      { Error::IsDirectory, "IsDirectory" },
      { Error::NotEmpty, "NotEmpty" },
      { Error::NameTooLong, "NameTooLong" },
      { Error::NoSpace, "NoSpace" },
      { Error::NoMemory, "NoMemory" },
      { Error::TooBig, "TooBig" },
      { Error::InvalidArgument, "InvalidArgument" },
      { Error::BadHandle, "BadHandle" },
      { Error::NotMounted, "NotMounted" },
      { Error::NotInitialised, "NotInitialised" },
      { Error::Unsupported, "Unsupported" },
      { Error::Timeout, "Timeout" },
      { Error::Busy, "Busy" },
      { Error::WriteProtected, "WriteProtected" },
      { Error::NotReady, "NotReady" },
      { Error::NeedsRemount, "NeedsRemount" },
      } };

      for(const Entry &entry : table) {
         CHECK(std::string_view{ toString(entry.error) } == entry.name);
      }

      size_t named = 0u;
      while(named < 256u && std::string_view{ toString(static_cast<Error>(named)) } != "Unknown") {
         ++named;
      }
      CHECK(named == table.size()); // also fails if toString() has no "Unknown" fallback

      CHECK(std::string_view{ toString(static_cast<Error>(200u)) } == "Unknown");
   }

   TEST_CASE("toString is usable in constant expressions") {
      static_assert(std::string_view{ toString(Error::Io) } == "Io");
      static_assert(std::string_view{ toString(Error::WriteProtected) } == "WriteProtected");
      CHECK(true);
   }
}
