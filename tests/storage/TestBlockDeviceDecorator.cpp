#include "storage/BlockDeviceDecorator.h"
#include <array>
#include <doctest/doctest.h>
#include <string_view>

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

   TEST_CASE("Geometry defaults") {
      const Geometry geometry{};
      CHECK(geometry.readSize == 1u);
      CHECK(geometry.programSize == 1u);
      CHECK(geometry.eraseSize == 0u);
      CHECK(geometry.size == 0u);
      CHECK_FALSE(geometry.eraseBeforeProgram); // no erase unit, so erase-before-program cannot apply
   }

   TEST_CASE("toString covers every error code") {
      CHECK(std::string_view{ toString(Error::Io) } == "Io");
      CHECK(std::string_view{ toString(Error::Unsupported) } == "Unsupported");
      CHECK(std::string_view{ toString(Error::NotInitialised) } == "NotInitialised");
      CHECK(std::string_view{ toString(static_cast<Error>(200u)) } == "Unknown");
   }
}
