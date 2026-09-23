#include "storage/BlockDeviceDecorator.h"
#include <array>
#include <doctest/doctest.h>
#include <string_view>
#include <type_traits>

using namespace art::storage;

namespace {

// Lower device that records which methods were called and with what arguments.
class RecordingDevice : public BlockDevice {
   public:
   std::expected<void, Error> init() override {
      ++initCalls;
      return {};
   }

   std::expected<void, Error> read(offset_t offset, std::span<uint8_t> buffer) override {
      lastReadOffset = offset;
      for(uint8_t &byte : buffer) {
         byte = 0x5Au;
      }
      return {};
   }

   std::expected<void, Error> program(offset_t offset, std::span<const uint8_t> data) override {
      lastProgramOffset = offset;
      lastProgramSize = data.size();
      return {};
   }

   std::expected<void, Error> erase(offset_t offset, offset_t length) override {
      lastEraseOffset = offset;
      lastEraseLength = length;
      return {};
   }

   std::expected<void, Error> sync() override {
      ++syncCalls;
      return std::unexpected(Error::Io);
   }

   Geometry geometry() const override {
      return Geometry{ 1u, 256u, 4096u, 0x1'0000'0000ull, true };
   }

   uint32_t initCalls{ 0u };
   uint32_t syncCalls{ 0u };
   offset_t lastReadOffset{ 0u };
   offset_t lastProgramOffset{ 0u };
   size_t lastProgramSize{ 0u };
   offset_t lastEraseOffset{ 0u };
   offset_t lastEraseLength{ 0u };
};

// Transform device that overrides only read, program and geometry, as the issue requires.
class MinimalTransform : public BlockDeviceDecorator {
   public:
   using BlockDeviceDecorator::BlockDeviceDecorator;

   std::expected<void, Error> read(offset_t offset, std::span<uint8_t> buffer) override {
      return lower().read(offset + 1u, buffer);
   }

   std::expected<void, Error> program(offset_t offset, std::span<const uint8_t> data) override {
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

      const auto syncResult = device.sync();
      REQUIRE_FALSE(syncResult.has_value());
      CHECK((syncResult.error() == Error::Io)); // parentheses: doctest cannot stringify enum class
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
      CHECK(lowerDevice.lastProgramSize == 3u);

      const Geometry geometry = device.geometry();
      CHECK(geometry.programSize == 512u);
      CHECK(geometry.eraseSize == 4096u);
      CHECK(geometry.size == 0x1'0000'0000ull);
   }

   TEST_CASE("plain decorator forwards geometry unchanged") {
      RecordingDevice lowerDevice;
      BlockDeviceDecorator passThrough{ lowerDevice };
      const Geometry geometry = passThrough.geometry();
      CHECK(geometry.readSize == 1u);
      CHECK(geometry.programSize == 256u);
      CHECK(geometry.eraseBeforeProgram);
   }

   TEST_CASE("BlockDevice::sync defaults to success") {
      struct NoSyncDevice : BlockDevice {
         std::expected<void, Error> init() override {
            return {};
         }
         std::expected<void, Error> read(offset_t, std::span<uint8_t>) override {
            return {};
         }
         std::expected<void, Error> program(offset_t, std::span<const uint8_t>) override {
            return {};
         }
         std::expected<void, Error> erase(offset_t, offset_t) override {
            return {};
         }
         Geometry geometry() const override {
            return {};
         }
      } device;
      CHECK(device.sync().has_value());
   }

   TEST_CASE("BlockDevice and BlockDeviceDecorator cannot be copied or moved through the base") {
      static_assert(!std::is_copy_constructible_v<BlockDevice>);
      static_assert(!std::is_move_constructible_v<BlockDevice>);
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
      CHECK(Geometry{}.isValid()); // the default is an empty but consistent device

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
      CHECK(std::string_view{ toString(Error::Io) } == "Io");
      CHECK(std::string_view{ toString(Error::Unsupported) } == "Unsupported");
      CHECK(std::string_view{ toString(Error::NotInitialised) } == "NotInitialised");
      CHECK(std::string_view{ toString(static_cast<Error>(200u)) } == "Unknown");
   }
}
