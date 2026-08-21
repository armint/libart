#pragma once
#include "storage/FatFile.h"

// RAII guard for FatFile::state: a failing REQUIRE/CHECK unwinds the test
// case, skipping any trailing "FatFile::state = nullptr;" cleanup and
// leaving mock mode active for every later test case in the binary —
// including the real-fatfs-backed TestFatFile.cpp/TestFatDir.cpp tests
// sharing this process.
struct FatFileMockGuard {
   FatFile::MockState state;

   FatFileMockGuard() {
      FatFile::state = &state;
   }

   ~FatFileMockGuard() {
      FatFile::state = nullptr;
   }
};
