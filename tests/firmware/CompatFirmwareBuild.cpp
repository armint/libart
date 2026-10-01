// Compiled, not run: tests/check-compat-firmware-build.sh builds this file with arm-none-eabi-g++, -fno-exceptions and
// -fno-rtti, the way firmware is built. It uses every compat header (src/utils/compat/) and instantiates every member
// of the own art::expected that libArT uses, so that a throw, a typeid or a dynamic_cast in them fails the build, and
// so that a dependency on the exception or RTTI runtime shows up as an undefined symbol in the object file.

#include "utils/compat/bit.h"
#include "utils/compat/expected.h"
#include "utils/compat/span.h"
#include "utils/compat/type_traits.h"

#include <cstdint>
#include <type_traits>
#include <utility>

// A named namespace (not an anonymous one), so that the functions below have external linkage.
namespace compat_firmware_build {

enum class FirmwareError : uint8_t { Failed, Timeout };

// A move-only value with a non-trivial destructor, like storage::File.
class MoveOnlyHandle {
   public:
   explicit MoveOnlyHandle(int32_t id)
      : id_(id) {
   }
   MoveOnlyHandle(MoveOnlyHandle &&other) noexcept
      : id_(std::exchange(other.id_, -1)) {
   }
   MoveOnlyHandle &operator=(MoveOnlyHandle &&other) noexcept {
      id_ = std::exchange(other.id_, -1);
      return *this;
   }
   MoveOnlyHandle(const MoveOnlyHandle &) = delete;
   MoveOnlyHandle &operator=(const MoveOnlyHandle &) = delete;
   ~MoveOnlyHandle() {
      id_ = -1;
   }
   int32_t id() const {
      return id_;
   }

   private:
   int32_t id_;
};

static_assert(std::is_same_v<art::remove_cvref_t<const volatile int &>, int>);
static_assert(art::bit_cast<uint32_t>(1.0f) == 0x3f800000U);

} // namespace compat_firmware_build

using compat_firmware_build::FirmwareError;
using compat_firmware_build::MoveOnlyHandle;

// Every non-template member of each specialisation (the own expected), or of std::expected.
template class art::expected<int32_t, FirmwareError>;
template class art::expected<void, FirmwareError>;
template class art::expected<MoveOnlyHandle, FirmwareError>;
template class art::unexpected<FirmwareError>;

// External linkage, so that the compiler keeps the code that uses the templates.
art::expected<int32_t, FirmwareError> firmwareParse(art::span<const uint8_t> data) {
   if(data.empty()) {
      return art::unexpected(FirmwareError::Failed);
   }
   return static_cast<int32_t>(data.first(1)[0]) + (art::is_little_endian ? 0 : 1);
}

art::expected<void, FirmwareError> firmwareCheck(art::span<const uint8_t> data) {
   const auto parsed = firmwareParse(data.subspan(0));
   if(!parsed) {
      return art::unexpected(parsed.error());
   }
   return {};
}

int32_t firmwareOpen(int32_t id) {
   art::expected<MoveOnlyHandle, FirmwareError> handle{ art::unexpect, FirmwareError::Timeout };
   if(id >= 0) {
      handle = MoveOnlyHandle(id);
   }
   art::expected<MoveOnlyHandle, FirmwareError> moved = std::move(handle);
   const int32_t value = moved.has_value() ? moved->id() : firmwareParse({}).value_or(-1);
   return moved == art::unexpected(FirmwareError::Timeout) ? -2 : value + moved.value().id();
}
