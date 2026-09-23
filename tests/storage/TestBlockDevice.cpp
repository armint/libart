#include "BlockDeviceContract.h"
#include "MockCipherBlockDevice.h"
#include "RamBlockDevice.h"
#include "storage/BlockDeviceDecorator.h"
#include <array>
#include <doctest/doctest.h>

using namespace art::storage;
using namespace art::storage::test;

namespace {

constexpr uint64_t kDeviceSize = 1024u;

// clang-format off
// NOR-like: byte reads, 4 byte program unit, 256 byte erase unit.
constexpr Geometry kNorGeometry{
   .readSize = 1u, .programSize = 4u, .eraseSize = 256u, .size = kDeviceSize, .eraseBeforeProgram = true
};
// SD-like: 8 byte units, no erase, overwritable.
constexpr Geometry kSdGeometry{
   .readSize = 8u, .programSize = 8u, .eraseSize = 0u, .size = kDeviceSize, .eraseBeforeProgram = false
};
// NOR with page-sized program unit larger than the cipher sector.
constexpr Geometry kPageNorGeometry{
   .readSize = 4u, .programSize = 16u, .eraseSize = 512u, .size = 2u * kDeviceSize, .eraseBeforeProgram = true
};
// clang-format on

// Owns the backing array on the test host; the device itself does not allocate.
template <uint64_t Size>
struct RamFixture {
   explicit RamFixture(const Geometry &geometry)
      : device(geometry, backing) {}

   std::array<uint8_t, Size> backing{};
   RamBlockDevice device;
};

} // namespace

TEST_SUITE("art::storage::BlockDevice contract") {

   TEST_CASE("RamBlockDevice as NOR passes the contract") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      checkBlockDeviceContract(ram.device, kNorErasedByte);
   }

   TEST_CASE("RamBlockDevice with coarse page NOR geometry passes the contract") {
      RamFixture<2u * kDeviceSize> ram{ kPageNorGeometry };
      checkBlockDeviceContract(ram.device, kNorErasedByte);
   }

   TEST_CASE("RamBlockDevice with 64 KiB erase units passes the contract") {
      constexpr Geometry largeErase{
         .readSize = 1u, .programSize = 256u, .eraseSize = 64u * 1024u, .size = 256u * 1024u, .eraseBeforeProgram = true
      };
      RamFixture<256u * 1024u> ram{ largeErase };
      checkBlockDeviceContract(ram.device, kNorErasedByte);
   }

   TEST_CASE("RamBlockDevice as SD card passes the contract") {
      RamFixture<kDeviceSize> ram{ kSdGeometry };
      checkBlockDeviceContract(ram.device, RamBlockDevice::kErasedByte);
   }

   TEST_CASE("pass-through decorator passes the contract") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      BlockDeviceDecorator passThrough{ ram.device };
      checkBlockDeviceContract(passThrough, kNorErasedByte);
   }

   TEST_CASE("MockCipherBlockDevice on NOR passes the contract") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      MockCipherBlockDevice cipher{ ram.device };
      checkBlockDeviceContract(cipher, std::nullopt);
   }

   TEST_CASE("MockCipherBlockDevice on an SD-like device passes the contract") {
      RamFixture<kDeviceSize> ram{ kSdGeometry };
      MockCipherBlockDevice cipher{ ram.device };
      checkBlockDeviceContract(cipher, std::nullopt);
   }

   TEST_CASE("cipher stacked on a pass-through decorator passes the contract") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      BlockDeviceDecorator passThrough{ ram.device };
      MockCipherBlockDevice cipher{ passThrough };
      checkBlockDeviceContract(cipher, std::nullopt);
   }
}

TEST_SUITE("art::storage::RamBlockDevice") {

   TEST_CASE("fresh device is erased and reports the configured geometry") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      const Geometry geometry = ram.device.geometry();
      CHECK(geometry.readSize == 1u);
      CHECK(geometry.programSize == 4u);
      CHECK(geometry.eraseSize == 256u);
      CHECK(geometry.size == kDeviceSize);
      CHECK(geometry.eraseBeforeProgram);
      REQUIRE(ram.device.init().has_value());
      std::array<uint8_t, 8> buffer{};
      REQUIRE(ram.device.read(0u, buffer).has_value());
      for(const uint8_t byte : buffer) {
         CHECK(byte == 0xFFu);
      }
   }

   TEST_CASE("operations before init are rejected") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      std::array<uint8_t, 4> buffer{};
      CHECK(failedWith(ram.device.read(0u, buffer), Error::NotInitialised));
      CHECK(failedWith(ram.device.program(0u, buffer), Error::NotInitialised));
      CHECK(failedWith(ram.device.erase(0u, 256u), Error::NotInitialised));
      CHECK(ram.device.readCount() == 0u);
   }

   TEST_CASE("init rejects unusable configuration") {
      std::array<uint8_t, 16> tooSmall{};
      RamBlockDevice undersized{ kNorGeometry, tooSmall };
      CHECK(failedWith(undersized.init(), Error::InvalidArgument));

      std::array<uint8_t, kDeviceSize> backing{};
      Geometry invalid = kNorGeometry;
      invalid.readSize = 0u;
      RamBlockDevice zeroGranularity{ invalid, backing };
      CHECK(failedWith(zeroGranularity.init(), Error::InvalidArgument));

      // Geometry::isValid() also rejects an empty device and erase-before-program without erase unit.
      invalid = kNorGeometry;
      invalid.size = 0u;
      RamBlockDevice empty{ invalid, backing };
      CHECK(failedWith(empty.init(), Error::InvalidArgument));

      invalid = kSdGeometry;
      invalid.eraseBeforeProgram = true;
      RamBlockDevice noEraseUnit{ invalid, backing };
      CHECK(failedWith(noEraseUnit.init(), Error::InvalidArgument));
   }

   TEST_CASE("program over programmed cells yields the bitwise AND") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      REQUIRE(ram.device.init().has_value());
      const std::array<uint8_t, 4> first{ 0xF0u, 0xFFu, 0x3Cu, 0x00u };
      const std::array<uint8_t, 4> second{ 0x3Cu, 0x0Fu, 0xF0u, 0xFFu };
      REQUIRE(ram.device.program(0u, first).has_value());
      REQUIRE(ram.device.program(0u, second).has_value());
      std::array<uint8_t, 4> result{};
      REQUIRE(ram.device.read(0u, result).has_value());
      CHECK(result[0] == 0x30u);
      CHECK(result[1] == 0x0Fu);
      CHECK(result[2] == 0x30u);
      CHECK(result[3] == 0x00u);
   }

   TEST_CASE("erase restores 0xFF only inside the erased range") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      REQUIRE(ram.device.init().has_value());
      const std::array<uint8_t, 4> zeros{};
      REQUIRE(ram.device.program(252u, zeros).has_value());
      REQUIRE(ram.device.program(256u, zeros).has_value());
      REQUIRE(ram.device.erase(0u, 256u).has_value());
      std::array<uint8_t, 8> result{};
      REQUIRE(ram.device.read(252u, result).has_value());
      CHECK(result[0] == 0xFFu);
      CHECK(result[3] == 0xFFu);
      CHECK(result[4] == 0x00u);
      CHECK(result[7] == 0x00u);
   }

   TEST_CASE("device without erase overwrites on program") {
      RamFixture<kDeviceSize> ram{ kSdGeometry };
      REQUIRE(ram.device.init().has_value());
      const std::array<uint8_t, 8> zeros{};
      const std::array<uint8_t, 8> ones{ 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu };
      REQUIRE(ram.device.program(0u, zeros).has_value());
      REQUIRE(ram.device.program(0u, ones).has_value());
      std::array<uint8_t, 8> result{};
      REQUIRE(ram.device.read(0u, result).has_value());
      CHECK(result == ones);
      // Erase is a successful no-op and is not counted.
      CHECK(ram.device.erase(0u, 8u).has_value());
      CHECK(ram.device.eraseCount() == 0u);
      std::array<uint8_t, 8> after{};
      REQUIRE(ram.device.read(0u, after).has_value());
      CHECK(after == ones);
   }

   TEST_CASE("device without erase accepts any in-bounds erase and rejects out-of-range ones") {
      RamFixture<kDeviceSize> ram{ kSdGeometry };
      CHECK(failedWith(ram.device.erase(0u, 8u), Error::NotInitialised));
      REQUIRE(ram.device.init().has_value());
      CHECK(ram.device.erase(1u, 3u).has_value());
      CHECK(ram.device.erase(kDeviceSize, 0u).has_value());
      CHECK(failedWith(ram.device.erase(kDeviceSize, 1u), Error::InvalidArgument));
      CHECK(failedWith(ram.device.erase(kDeviceSize - 1u, 2u), Error::InvalidArgument));
      CHECK(failedWith(ram.device.erase(UINT64_MAX, 1u), Error::InvalidArgument));
      CHECK(failedWith(ram.device.erase(1u, UINT64_MAX), Error::InvalidArgument));
      CHECK(ram.device.eraseCount() == 0u);
   }

   TEST_CASE("init again keeps the stored data") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      REQUIRE(ram.device.init().has_value());
      const std::array<uint8_t, 4> data{ 1u, 2u, 3u, 4u };
      REQUIRE(ram.device.program(8u, data).has_value());
      REQUIRE(ram.device.init().has_value());
      std::array<uint8_t, 4> result{};
      REQUIRE(ram.device.read(8u, result).has_value());
      CHECK(result == data);
   }

   TEST_CASE("misaligned and out-of-range access is rejected and changes nothing") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      REQUIRE(ram.device.init().has_value());
      const std::array<uint8_t, 8> zeros{};
      // programSize is 4: misaligned offset, misaligned length.
      CHECK(failedWith(ram.device.program(2u, std::span<const uint8_t>{ zeros }.first(4u)), Error::InvalidArgument));
      CHECK(failedWith(ram.device.program(0u, std::span<const uint8_t>{ zeros }.first(6u)), Error::InvalidArgument));
      // eraseSize is 256.
      CHECK(failedWith(ram.device.erase(4u, 256u), Error::InvalidArgument));
      CHECK(failedWith(ram.device.erase(0u, 300u), Error::InvalidArgument));
      // Past the end, including 64 bit wrap-around.
      std::array<uint8_t, 4> buffer{};
      CHECK(failedWith(ram.device.read(kDeviceSize - 2u, buffer), Error::InvalidArgument));
      CHECK(failedWith(ram.device.read(UINT64_MAX - 1u, buffer), Error::InvalidArgument));
      CHECK(failedWith(ram.device.erase(kDeviceSize, 256u), Error::InvalidArgument));

      for(const uint8_t byte : ram.device.contents()) {
         CHECK(byte == 0xFFu);
      }
      CHECK(ram.device.programCount() == 0u);
      CHECK(ram.device.eraseCount() == 0u);
   }

   TEST_CASE("misaligned read is rejected on a device with coarse read size") {
      RamFixture<kDeviceSize> ram{ kSdGeometry };
      REQUIRE(ram.device.init().has_value());
      std::array<uint8_t, 8> buffer{};
      CHECK(failedWith(ram.device.read(4u, buffer), Error::InvalidArgument));
      CHECK(failedWith(ram.device.read(0u, std::span<uint8_t>{ buffer }.first(4u)), Error::InvalidArgument));
   }

   TEST_CASE("counters count operations that reached the medium") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      REQUIRE(ram.device.init().has_value());
      std::array<uint8_t, 4> buffer{};
      REQUIRE(ram.device.erase(0u, 256u).has_value());
      REQUIRE(ram.device.program(0u, buffer).has_value());
      REQUIRE(ram.device.program(4u, buffer).has_value());
      REQUIRE(ram.device.read(0u, buffer).has_value());
      CHECK(ram.device.eraseCount() == 1u);
      CHECK(ram.device.programCount() == 2u);
      CHECK(ram.device.readCount() == 1u);
      ram.device.resetCounters();
      CHECK(ram.device.eraseCount() == 0u);
      CHECK(ram.device.programCount() == 0u);
      CHECK(ram.device.readCount() == 0u);
   }
}

TEST_SUITE("art::storage fault injection") {

   TEST_CASE("nth operation fails with Io, once, and leaves the data untouched") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      REQUIRE(ram.device.init().has_value());
      const std::array<uint8_t, 4> data{ 1u, 2u, 3u, 4u };
      std::array<uint8_t, 4> buffer{};

      ram.device.failNthOperation(3u);
      CHECK(ram.device.erase(0u, 256u).has_value());              // 1
      CHECK(ram.device.read(0u, buffer).has_value());             // 2
      CHECK(failedWith(ram.device.program(0u, data), Error::Io)); // 3
      CHECK(ram.device.programCount() == 1u);
      REQUIRE(ram.device.read(0u, buffer).has_value());
      CHECK(buffer[0] == 0xFFu);
      CHECK(buffer[3] == 0xFFu);

      // One-shot: the same operation now succeeds.
      CHECK(ram.device.program(0u, data).has_value());
   }

   TEST_CASE("nth operation can fail a read or an erase") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      REQUIRE(ram.device.init().has_value());
      std::array<uint8_t, 4> buffer{};
      ram.device.failNthOperation(1u);
      CHECK(failedWith(ram.device.read(0u, buffer), Error::Io));
      ram.device.failNthOperation(1u);
      CHECK(failedWith(ram.device.erase(0u, 256u), Error::Io));
      ram.device.failNthOperation(2u);
      ram.device.clearFaults();
      CHECK(ram.device.read(0u, buffer).has_value());
      CHECK(ram.device.read(0u, buffer).has_value());
   }

   TEST_CASE("rejected calls do not consume the failure countdown") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      REQUIRE(ram.device.init().has_value());
      std::array<uint8_t, 4> buffer{};
      ram.device.failNthOperation(1u);
      CHECK(failedWith(ram.device.program(1u, buffer), Error::InvalidArgument));
      CHECK(failedWith(ram.device.program(0u, buffer), Error::Io));
   }

   TEST_CASE("truncated program leaves exactly the bytes written so far") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      REQUIRE(ram.device.init().has_value());
      std::array<uint8_t, 16> data{};
      for(size_t index = 0u; index < data.size(); ++index) {
         data[index] = static_cast<uint8_t>(0x10u + index);
      }

      ram.device.truncateNextProgram(6u);
      CHECK(failedWith(ram.device.program(0u, data), Error::Io));

      const auto content = ram.device.contents();
      for(size_t index = 0u; index < 6u; ++index) {
         CHECK(content[index] == data[index]);
      }
      for(size_t index = 6u; index < 16u; ++index) {
         CHECK(content[index] == 0xFFu);
      }

      // One-shot: the next program completes.
      REQUIRE(ram.device.erase(0u, 256u).has_value());
      CHECK(ram.device.program(0u, data).has_value());
      CHECK(ram.device.contents()[15] == data[15]);
   }

   TEST_CASE("truncated program ANDs into existing cells like a full one") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      REQUIRE(ram.device.init().has_value());
      const std::array<uint8_t, 4> old{ 0x0Fu, 0x0Fu, 0x0Fu, 0x0Fu };
      const std::array<uint8_t, 4> next{ 0x3Cu, 0x3Cu, 0x3Cu, 0x3Cu };
      REQUIRE(ram.device.program(0u, old).has_value());
      ram.device.truncateNextProgram(2u);
      CHECK(failedWith(ram.device.program(0u, next), Error::Io));
      CHECK(ram.device.contents()[1] == 0x0Cu);
      CHECK(ram.device.contents()[2] == 0x0Fu);
   }

   TEST_CASE("faults propagate through a pass-through decorator") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      BlockDeviceDecorator passThrough{ ram.device };
      REQUIRE(passThrough.init().has_value());
      const std::array<uint8_t, 4> data{ 1u, 2u, 3u, 4u };
      ram.device.failNthOperation(1u);
      CHECK(failedWith(passThrough.erase(0u, 256u), Error::Io));
      ram.device.truncateNextProgram(2u);
      CHECK(failedWith(passThrough.program(0u, data), Error::Io));
      CHECK(ram.device.contents()[1] == 0x02u);
      CHECK(ram.device.contents()[2] == 0xFFu);
   }
}

TEST_SUITE("art::storage::MockCipherBlockDevice") {

   TEST_CASE("reports sector-sized read and program units and passes the rest through") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      MockCipherBlockDevice cipher{ ram.device };
      const Geometry geometry = cipher.geometry();
      CHECK(geometry.readSize == MockCipherBlockDevice::kSectorSize);
      CHECK(geometry.programSize == MockCipherBlockDevice::kSectorSize);
      CHECK(geometry.eraseSize == 256u);
      CHECK(geometry.size == kDeviceSize);
      CHECK(geometry.eraseBeforeProgram);
   }

   TEST_CASE("stores ciphertext, returns plaintext") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      MockCipherBlockDevice cipher{ ram.device };
      REQUIRE(cipher.init().has_value());
      REQUIRE(cipher.erase(0u, 256u).has_value());

      std::array<uint8_t, 32> plain{};
      for(size_t index = 0u; index < plain.size(); ++index) {
         plain[index] = static_cast<uint8_t>(index + 1u);
      }
      REQUIRE(cipher.program(64u, plain).has_value());

      std::array<uint8_t, 32> stored{};
      REQUIRE(ram.device.read(64u, stored).has_value());
      CHECK(stored != plain);

      std::array<uint8_t, 32> result{};
      REQUIRE(cipher.read(64u, result).has_value());
      CHECK(result == plain);
   }

   TEST_CASE("the keystream depends on the offset") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      MockCipherBlockDevice cipher{ ram.device };
      REQUIRE(cipher.init().has_value());
      REQUIRE(cipher.erase(0u, 256u).has_value());
      const std::array<uint8_t, 16> same{};
      REQUIRE(cipher.program(0u, same).has_value());
      REQUIRE(cipher.program(16u, same).has_value());
      const auto content = ram.device.contents();
      CHECK_FALSE(std::equal(content.begin(), content.begin() + 16, content.begin() + 16));
   }

   TEST_CASE("rejects access that the device below would tolerate") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      MockCipherBlockDevice cipher{ ram.device };
      REQUIRE(cipher.init().has_value());
      std::array<uint8_t, 16> buffer{};
      // Byte reads and 4 byte programs are fine for the RAM device but not for the cipher.
      std::array<uint8_t, 4> small{};
      CHECK(ram.device.read(1u, small).has_value());
      CHECK(failedWith(cipher.read(1u, buffer), Error::InvalidArgument));
      CHECK(failedWith(cipher.read(0u, small), Error::InvalidArgument));
      CHECK(ram.device.program(4u, small).has_value());
      CHECK(failedWith(cipher.program(4u, buffer), Error::InvalidArgument));
      CHECK(failedWith(cipher.program(0u, small), Error::InvalidArgument));
   }

   TEST_CASE("a rejected out-of-range program writes nothing") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      MockCipherBlockDevice cipher{ ram.device };
      REQUIRE(cipher.init().has_value());
      // Starts in range but ends past the end: the first chunk must not reach the medium.
      const std::array<uint8_t, 128> data{};
      CHECK(failedWith(cipher.program(kDeviceSize - 96u, data), Error::InvalidArgument));
      CHECK(ram.device.programCount() == 0u);
   }

   TEST_CASE("long program is split into chunks and round-trips") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      MockCipherBlockDevice cipher{ ram.device };
      REQUIRE(cipher.init().has_value());
      REQUIRE(cipher.erase(0u, 512u).has_value());
      std::array<uint8_t, 400> plain{};
      for(size_t index = 0u; index < plain.size(); ++index) {
         plain[index] = static_cast<uint8_t>(index * 7u);
      }
      REQUIRE(cipher.program(96u, std::span<const uint8_t>{ plain }.first(384u)).has_value());
      std::array<uint8_t, 384> result{};
      REQUIRE(cipher.read(96u, result).has_value());
      CHECK(std::equal(result.begin(), result.end(), plain.begin()));
   }

   TEST_CASE("errors from below surface and a truncated program leaves ciphertext up to the cut") {
      RamFixture<kDeviceSize> ram{ kNorGeometry };
      MockCipherBlockDevice cipher{ ram.device };
      REQUIRE(cipher.init().has_value());
      std::array<uint8_t, 32> plain{};
      for(size_t index = 0u; index < plain.size(); ++index) {
         plain[index] = static_cast<uint8_t>(index);
      }

      ram.device.failNthOperation(1u);
      std::array<uint8_t, 16> buffer{};
      CHECK(failedWith(cipher.read(0u, buffer), Error::Io));

      ram.device.truncateNextProgram(20u);
      CHECK(failedWith(cipher.program(0u, plain), Error::Io));
      const auto content = ram.device.contents();
      for(size_t index = 20u; index < 32u; ++index) {
         CHECK(content[index] == 0xFFu);
      }
      // The first 20 bytes are ciphertext of the plaintext, not 0xFF.
      CHECK_FALSE(std::all_of(content.begin(), content.begin() + 20, [](uint8_t byte) { return byte == 0xFFu; }));
   }

   TEST_CASE("init rejects a device below whose size is not a multiple of the sector") {
      Geometry odd = kSdGeometry;
      odd.size = 1000u; // multiple of the 8 byte units, not of 16
      RamFixture<kDeviceSize> ram{ odd };
      MockCipherBlockDevice cipher{ ram.device };
      CHECK(failedWith(cipher.init(), Error::Unsupported));
   }

   TEST_CASE("init rejects a device below with incompatible granularity") {
      Geometry coarse = kNorGeometry;
      coarse.readSize = 32u;
      coarse.programSize = 32u;
      RamFixture<kDeviceSize> ram{ coarse };
      MockCipherBlockDevice cipher{ ram.device };
      CHECK(failedWith(cipher.init(), Error::Unsupported));
   }
}
