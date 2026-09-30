#include "NullWritable.h"

art::expected<size_t, Writable::Error> NullWritable::write(art::span<const uint8_t> data) {
   return data.size();
}
art::expected<void, Writable::Error> NullWritable::flush() {
   return {};
}