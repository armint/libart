#include <chrono>
#include <utils/timestamp.h>

timestamp_t timestamp_in_ms() {
   using namespace std::chrono;
   return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
