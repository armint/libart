#include "Writable.h"
#include "utils/tfp_printf.h"

void Writable::writeCharacter(void *ptr, char c) {
   Writable &writable = *(static_cast<TfpHelper *>(ptr)->writable);
   auto &result = static_cast<TfpHelper *>(ptr)->result;
   auto res = writable.write(c);
   if(res && result) {
      result.value() += res.value();
   } else if(!res) {
      result = std::unexpected(res.error());
   }
}

std::expected<size_t, Writable::Error> Writable::printf_va(const char *format, ...) {
   va_list ap;
   va_start(ap, format);
   auto ret = vprintf(format, ap);
   va_end(ap);
   return ret;
}
std::expected<size_t, Writable::Error> Writable::vprintf(const char *format, va_list va_l) {
   TfpHelper helper{ this, 0 };
   tfp_format(&helper, &writeCharacter, format, va_l);
   return helper.result;
}
