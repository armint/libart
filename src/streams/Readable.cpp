#include <cctype>
#include <cstdio>

#include <utils/tfp_printf.h>

#include "Readable.h"


std::expected<size_t, Readable::Error> Readable::read(std::span<uint8_t> target) {
   for(auto i = target.begin(); i < target.end(); ++i) {
      auto res = read();
      if(!res) {
         return std::unexpected(res.error());
      }
      *i = res.value();
   }
   return target.size();
}

std::expected<size_t, Readable::Error>
Readable::readUntil(std::initializer_list<const char *> toFind, std::span<char> buffer, size_t *charsRead) {
   size_t _charsRead = 0;
   size_t result = toFind.size();
   // Create an array of indexes pointing to where we are in matching the strings
   size_t indexes[result];
   for(auto &index : indexes) {
      index = 0;
   }
   do {
      auto r = read();
      if(!r) {
         // Abort on error
         // Set the number of read characters if needed
         if(charsRead != nullptr) {
            *charsRead = _charsRead;
         }
         return r;
      }
      // Copy the data to the output buffer if possible
      if(_charsRead < buffer.size()) {
         buffer[_charsRead] = r.value();
      }
      // See if we have a matching character
      int i = 0;
      for(auto strs = toFind.begin(); strs < toFind.end(); strs++) {
         auto str = *strs;
         if(str[indexes[i]] == r.value()) {
            // See if we can advance the string match
            indexes[i]++;
         } else if(str[0] == r.value()) {
            // or if we are at the beginning of a match
            indexes[i] = 1;
         } else {
            indexes[i] = 0;
         }
         if(str[indexes[i]] == 0) {
            // We are at the end of the string, which means we have a match
            result = i;
            break;
         }
         i++;
      }

      _charsRead++;
   } while(result == toFind.size());
   // Set the number of read characters if needed
   if(charsRead != nullptr) {
      *charsRead = _charsRead;
   }
   return result;
}

std::expected<size_t, Readable::Error> Readable::scanf(const char *format, ...) {
   va_list args;
   va_start(args, format);
   auto ret = this->vscanf(format, args);
   va_end(args);
   return ret;
}

std::expected<size_t, Readable::Error> Readable::vscanf(const char *format, va_list args) {
   char formatChar;
   size_t matches = 0;
   std::optional<char> nextChar;
   while((formatChar = *(format++))) {
      bool keep = true;
      // Read a character if needed
      if(!nextChar) {
         auto r = read();
         if(!r) {
            return r;
         }
         nextChar = r.value();
      }
      if(*nextChar == 0) {
         // End of string
         return matches;
      }
      if(formatChar != '%') {
         // In case of a space in the format string, we can expect 0 or more spaces in the data
         if(formatChar == ' ') {
            while(isspace(nextChar.value())) {
               auto r = read();
               if(!r) {
                  return r;
               }
               nextChar = r.value();
            }
            continue;
         }
         if(formatChar != nextChar) {
            // Not a match, so we're finished
            return matches;
         }
      } else {
         // We encountered a '%' in the format string
         int maxChars = std::numeric_limits<int>::max(); // The maximum amount of characters to read as a number
         bool ll = false;
         formatChar = *(format++);
         if(formatChar == '*') {
            // We need to parse the value, but not keep it
            keep = false;
            formatChar = *(format++);
         }
         if(isdigit(formatChar)) {
            // read a number from the format string, store in maxChars, and read the next char
            formatChar = a2i(formatChar, &format, 10, &maxChars);
         }
         if(formatChar == 'l') {
            formatChar = *(format++);
            if(formatChar == 'l') {
               ll = true;
               formatChar = *(format++);
            }
         }
         switch(formatChar) {
         case 0:
            // End of string
            return matches;
         case 'u': {
            // Unsigned integer
            auto value = readUnsigned(nextChar, 10, maxChars);
            if(!value) {
               if(value.error() == NotFound) {
                  return matches;
               }
               return value;
            }
            if(keep) {
               matches++;
               if(ll) {
                  *(unsigned long long int *)va_arg(args, unsigned long long int *) = value.value();
               } else {
                  *(unsigned int *)va_arg(args, unsigned int *) = value.value();
               }
            }
            break;
         }
         case 'b': {
            // binary
            auto value = readUnsigned(nextChar, 2, maxChars);
            if(!value) {
               if(value.error() == NotFound) {
                  return matches;
               }
               return value;
            }
            if(keep) {
               matches++;
               if(ll) {
                  *(unsigned long long int *)va_arg(args, unsigned long long int *) = value.value();
               } else {
                  *(unsigned int *)va_arg(args, unsigned int *) = value.value();
               }
            }
            break;
         }
         case 'i':
            // Fall through
         case 'd': {
            // Signed integer
            auto value = readSigned(nextChar, 10, maxChars);
            if(!value) {
               if(value.error() == NotFound) {
                  return matches;
               }
               return value;
            }
            if(keep) {
               matches++;
               if(ll) {
                  *(long long int *)va_arg(args, long long int *) = value.value();
               } else {
                  *(int *)va_arg(args, int *) = value.value();
               }
            }
            break;
         }
         case 'p':
            // Pointer
            // Size of pointer determines what we need to use in va_arg
            ll = sizeof(void *) > 4;
            // fall through
         case 'x':
         case 'X': {
            // Hex number, we will ignore case and accept both upper and lower case for hex numbers
            auto value = readUnsigned(nextChar, 16, maxChars);
            if(!value) {
               if(value.error() == NotFound) {
                  return matches;
               }
               return value;
            }
            if(keep) {
               matches++;
               if(ll) {
                  *(unsigned long long int *)va_arg(args, unsigned long long int *) = value.value();
               } else {
                  *(unsigned int *)va_arg(args, unsigned int *) = value.value();
               }
            }
            break;
         }
         case 'c': {
            // Single character
            if(nextChar == 0) {
               return matches;
            }
            if(keep) {
               matches++;
               *(char *)va_arg(args, char *) = nextChar.value();
            }
            break;
         }
         case 's': {
            // String
            char *ptr = nullptr;
            if(keep) {
               ptr = va_arg(args, char *);
               // Store the first character
               *ptr++ = nextChar.value();
               matches++;
               maxChars--;
            }
            std::expected<char, Error> streamChar;
            do {
               if(maxChars-- <= 0) {
                  // We have read the maximum amount of characters
                  break;
               }
               streamChar = read();
               if(!streamChar) {
                  return streamChar;
               }
               nextChar = streamChar.value();
               if(keep) {
                  *ptr++ = streamChar.value();
               }
               if(streamChar.value() == 0) {
                  // We reached the end of the string in the stream
                  return matches;
               }
            } while(!isspace(streamChar.value()));
            if(keep) {
               if(maxChars > 0) {
                  // replace the space
                  --ptr;
               }
               // terminate the read string
               *ptr = 0;
            }
            if(maxChars > 0) {
               // We have read 1 more character than we consumed, so we will skip the nextChar.reset() at the end of the loop
               continue;
            }
            break;
         }
         case '%': {
            // Literal % character
            if(nextChar != '%') {
               return matches;
            }
         }
         default:
            break;
         }
      }
      nextChar.reset();
   }
   return matches;
}

std::expected<char, Readable::Error> Readable::scanfRead() {
   auto r = read();
   if(r && r.value() == 0) {
      return NotFound;
   }
   return r;
}

std::expected<int64_t, Readable::Error> Readable::readSigned(std::optional<char> firstChar, int base, size_t maxDigits) {
   bool negative = false;
   if(!firstChar.has_value()) {
      auto r = scanfRead();
      if(!r) {
         return r;
      }
      firstChar = r.value();
   }
   // Check for signs
   if(firstChar == '-') {
      negative = true;
      firstChar.reset();
      --maxDigits;
   } else if(firstChar.value() == '+') {
      firstChar.reset();
      --maxDigits;
   }
   auto number = readUnsigned(firstChar, base, maxDigits);
   if(!number) {
      return number;
   }
   return negative ? -number.value() : number.value();
}

std::expected<uint64_t, Readable::Error> Readable::readUnsigned(std::optional<char> firstChar, int base, size_t maxDigits) {
   uint64_t number = 0;
   bool haveNumber = false;
   // Read the first character if needed
   std::expected<char, Error> r = firstChar ? firstChar.value() : scanfRead();
   do {
      if(!r) {
         return r;
      }
      // Convert the character into a possible digit
      int digit = a2d(r.value());
      if(digit >= base || digit < 0) {
         // Not a digit (anymore)
         break;
      }
      // We have a digit
      haveNumber = true;
      number = number * base + digit;
      r = read();
   } while(r && --maxDigits > 0);
   if(!haveNumber) {
      return std::unexpected<Error>(NotFound);
   }
   return number;
}
