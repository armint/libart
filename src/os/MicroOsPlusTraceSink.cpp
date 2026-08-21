/*
 * Bridges µOS++'s os::trace:: channel (used throughout the RTOS for
 * startup, thread, and scheduler diagnostics) into logging::Logger /
 * LogManager, under its own "OS" topic.
 *
 * os::trace::write()/initialize()/flush() are weak in the RTOS (see
 * micro-os-plus-iii's src/diag/trace.cpp); the strong definitions below
 * override them. Only compiled when the consumer links µOS++ (see
 * CMakeLists.txt). Additionally inert (falls through to the RTOS's own
 * weak no-op defaults) if the consumer has also selected one of µOS++'s
 * own trace backends (ITM/SWO, semihosting, Segger RTT) in their
 * os-app-config.h: those backends provide their own strong definitions
 * of the same functions, and linking both would be a multiple-definition
 * error.
 *
 * write() is called with arbitrary byte chunks, not necessarily whole
 * lines, so bytes are buffered here and only forwarded to the logger
 * once a line terminator is seen.
 */

#if defined(OS_USE_OS_APP_CONFIG_H)
#include <cmsis-plus/os-app-config.h>
#endif

#if !defined(OS_USE_TRACE_ITM) && !defined(OS_USE_TRACE_SEGGER_RTT) && !defined(OS_USE_TRACE_SEMIHOSTING_DEBUG) && \
   !defined(OS_USE_TRACE_SEMIHOSTING_STDOUT)

#include <cmsis-plus/diag/trace.h>
#include <cmsis-plus/rtos/os.h>

#include <logging/Logger.h>

namespace {

logging::Logger osLogger("OS");

// Zero-initialized before any dynamic initialization runs (guaranteed for
// a trivial bool with a constant initializer), so checking it is safe even
// from calls that land before osLogger's own constructor has executed
// (µOS++'s startup.cpp issues a few trace_printf() calls before
// os_run_init_array()). Set once osLogger is actually usable.
bool loggerReady = false;

struct OsLoggerInit {
   OsLoggerInit() {
      osLogger.set_level(logging::Level::Trace);
      loggerReady = true;
   }
};
OsLoggerInit osLoggerInit;

constexpr std::size_t kLineBufferSize = 160;
char lineBuffer[kLineBufferSize];
std::size_t lineLength = 0;

void flushLine() {
   if(lineLength > 0 && loggerReady) {
      lineBuffer[lineLength] = '\0';
      // libArT's tfp_printf has no support for "%.*s" (star precision
      // reads the width from a va_arg, which this implementation just
      // silently ignores); rely on the NUL terminator above instead.
      osLogger.trace("%s", lineBuffer);
   }
   lineLength = 0;
}

} // namespace

namespace os::trace {

void initialize(void) {
   // Nothing to do here: this runs before static constructors, so
   // loggerReady is still false and write()/flush() are no-ops until
   // osLoggerInit's constructor runs.
}

ssize_t write(const void *buf, std::size_t nbyte) {
   os::rtos::interrupts::critical_section cs;
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
   os::rtos::interrupts::critical_section cs;
   flushLine();
}

} // namespace os::trace

#endif // no competing µOS++ trace backend selected
