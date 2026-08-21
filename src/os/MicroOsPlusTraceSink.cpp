/*
 * Bridges µOS++'s os::trace:: channel (used throughout the RTOS for
 * startup, thread, and scheduler diagnostics) into logging::Logger /
 * LogManager, under its own "OS" topic.
 *
 * os::trace::write()/initialize()/flush() are weak in the RTOS (see
 * micro-os-plus-iii's src/diag/trace.cpp); the strong definitions below
 * override them. Compiled only when the consumer links µOS++ (see
 * CMakeLists.txt) — trace-itm.cpp and friends stay inert as long as no
 * OS_USE_TRACE_* backend is defined in the consumer's os-app-config.h,
 * since those backends would otherwise provide a second, conflicting
 * strong definition of os::trace::write().
 *
 * write() is called with arbitrary byte chunks, not necessarily whole
 * lines, so bytes are buffered here and only forwarded to the logger
 * once a line terminator is seen.
 */

#include <cmsis-plus/diag/trace.h>

#include <logging/Logger.h>

namespace {

logging::Logger osLogger("OS");

struct OsLoggerLevelInit {
   OsLoggerLevelInit() {
      osLogger.set_level(logging::Level::Trace);
   }
};
OsLoggerLevelInit osLoggerLevelInit;

constexpr std::size_t kLineBufferSize = 160;
char lineBuffer[kLineBufferSize];
std::size_t lineLength = 0;

void flushLine() {
   if(lineLength > 0) {
      osLogger.trace("%.*s", static_cast<int>(lineLength), lineBuffer);
      lineLength = 0;
   }
}

} // namespace

namespace os::trace {

void initialize(void) {
   // Nothing to do here: this typically runs before the consumer's own
   // static constructors, so the logger sink may not be wired up yet.
   // Trace output before that point is silently dropped by write() below.
}

ssize_t write(const void *buf, std::size_t nbyte) {
   const auto *cbuf = static_cast<const char *>(buf);
   for(std::size_t i = 0; i < nbyte; ++i) {
      const char c = cbuf[i];
      if(c == '\n') {
         flushLine();
      } else if(lineLength < kLineBufferSize - 1) {
         lineBuffer[lineLength++] = c;
      }
   }
   return static_cast<ssize_t>(nbyte);
}

void flush(void) {
   flushLine();
}

} // namespace os::trace
