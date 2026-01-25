#include <string>
#include <optional>
#include <chrono>
#include <cstdint>
#include <thread>
#include <utils/systick.h>

extern "C" {
void delaymS(duration_t msec) {
   std::this_thread::sleep_for(std::chrono::milliseconds(msec));
}
}
