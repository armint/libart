
#include <cctype>
#include <cstring>

#include "cstring.h"

namespace utils {
void trim(char *str) {
   int begin = 0;
   // Find the beginning of the trimmed string
   while(isspace(str[begin])) {
      begin++;
   }
   // Find the end of the trimmed string
   int end = strlen(str + begin) - 1;
   while(isspace(str[end + begin]) && end >= 0) {
      end--;
   }
   if(begin >= end) {
      // String contains only spaces
      str[0] = 0;
   } else {
      if(begin > 0) {
         // Copy the string if needed
         for(int i = begin; i < end; i++) {
            str[i - begin] = str[i];
         }
      }
      // Terminate the string
      str[end + 1] = 0;
   }
}

} // namespace utils