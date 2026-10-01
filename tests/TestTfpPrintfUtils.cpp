#include <art_types.h>
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <utils/tfp_printf.h>

TEST_CASE("int_to_string - Basic functionality [tfp_printf_utils]") {
   char bf[65];

   SUBCASE("Positive integer") {
      int_to_string(123, bf, sizeof(bf));
      CHECK(std::strcmp(bf, "123") == 0);
   }

   SUBCASE("Negative integer") {
      int_to_string(-456, bf, sizeof(bf));
      CHECK(std::strcmp(bf, "-456") == 0);
   }

   SUBCASE("Zero") {
      int_to_string(0, bf, sizeof(bf));
      CHECK(std::strcmp(bf, "0") == 0);
   }
}

TEST_CASE("int_to_string - Edge cases [tfp_printf_utils]") {
   char bf[65];

   SUBCASE("Minimum value") {
      big_int_t min_val = std::numeric_limits<big_int_t>::min();
      int_to_string(min_val, bf, sizeof(bf));
      if(sizeof(big_int_t) == 4) {
         CHECK(std::strcmp(bf, "-2147483648") == 0);
      } else {
         CHECK(std::strcmp(bf, "-9223372036854775808") == 0);
      }
   }

   SUBCASE("Buffer too small") {
      int res = int_to_string(12345, bf, 4);
      CHECK(res == -1);
   }
}

TEST_CASE("string_to_int - Basic functionality [tfp_printf_utils]") {
   int num;
   const char *src = "23abc";
   char next = string_to_int('1', &src, 10, &num);

   CHECK(num == 123);
   CHECK(next == 'a');
   CHECK(std::strcmp(src, "bc") == 0);
}

TEST_CASE("string_to_int - Overflow [tfp_printf_utils]") {
   int num;
   // 2147483647 is MAX_INT (32-bit)
   const char *src = "147483648";
   string_to_int('2', &src, 10, &num);

   CHECK(num == std::numeric_limits<int>::max());
}

TEST_CASE("unsigned_int_to_string - Basic functionality [tfp_printf_utils]") {
   char bf[65];

   SUBCASE("Base 10") {
      unsigned_int_to_string(123, 10, 0, bf, sizeof(bf));
      CHECK(std::strcmp(bf, "123") == 0);
   }

   SUBCASE("Base 16") {
      unsigned_int_to_string(0xABC, 16, 0, bf, sizeof(bf));
      CHECK(std::strcmp(bf, "abc") == 0);
      unsigned_int_to_string(0xABC, 16, 1, bf, sizeof(bf));
      CHECK(std::strcmp(bf, "ABC") == 0);
   }

   SUBCASE("Base 2") {
      unsigned_int_to_string(10, 2, 0, bf, sizeof(bf));
      CHECK(std::strcmp(bf, "1010") == 0);
   }
}

TEST_CASE("unsigned_int_to_string - Edge cases [tfp_printf_utils]") {
   char bf[65];

   SUBCASE("Maximum value") {
      big_uint_t max_val = std::numeric_limits<big_uint_t>::max();
      unsigned_int_to_string(max_val, 10, 0, bf, sizeof(bf));
      if(sizeof(big_uint_t) == 4) {
         CHECK(std::strcmp(bf, "4294967295") == 0);
      } else {
         CHECK(std::strcmp(bf, "18446744073709551615") == 0);
      }
   }

   SUBCASE("Buffer too small") {
      int res = unsigned_int_to_string(12345, 10, 0, bf, 4);
      CHECK(res == -1);
   }
}
