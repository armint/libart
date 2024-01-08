#include <chrono>
#include <utils/systick.h>

timestamp_t getSysTick() {
   using namespace std::chrono;
   return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
