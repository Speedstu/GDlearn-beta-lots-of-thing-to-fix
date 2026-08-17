// SPDX-License-Identifier: MIT
// Frame-accurate macro player for Windows.
//
// Deliberate design choice: NO memory reading, NO Cheat-Engine offsets, NO
// DLL injection. The old project hardcoded pointers like BASE_OFFSET 0x3222D0
// for one exact GD build, so it broke on every game update and every
// anti-cheat change. This player only sends keyboard input, timed against a
// high-resolution clock, which is exactly what a human macro tool does.
//
// Build (Visual Studio developer prompt):
//   cl /O2 /EHsc live\win_macro_player.cpp /Fe:macro_player.exe
// Build (MinGW):
//   g++ -O2 -std=c++17 live/win_macro_player.cpp -o macro_player.exe
//
// Usage:
//   macro_player.exe run.macro [--delay 3] [--key space] [--fps 60]
//
// Workflow: solve the level offline (`gdlearn solve level.gdl`), alt-tab to
// Geometry Dash, start the level, and the player replays the exact input
// sequence the search proved wins.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

// Reads the same RLE .macro format the C++ core writes.
bool loadMacro(const char* path, std::vector<unsigned char>* holds, int* fps) {
  FILE* f = std::fopen(path, "rb");
  if (!f) return false;
  char line[512];
  while (std::fgets(line, sizeof(line), f)) {
    if (line[0] == '#') continue;
    if (std::strncmp(line, "fps ", 4) == 0) {
      *fps = std::atoi(line + 4);
      continue;
    }
    if (line[0] == 'r' && line[1] == ' ') {
      int state = 0, count = 0;
      if (std::sscanf(line + 2, "%d %d", &state, &count) == 2)
        for (int i = 0; i < count; ++i)
          holds->push_back(static_cast<unsigned char>(state ? 1 : 0));
      continue;
    }
  }
  std::fclose(f);
  return !holds->empty();
}

}  // namespace

#ifdef _WIN32
namespace {

WORD keyFromName(const std::string& name) {
  if (name == "space") return VK_SPACE;
  if (name == "up") return VK_UP;
  if (name == "w") return 'W';
  if (name == "lmb") return 0;  // 0 => use mouse instead of keyboard
  return VK_SPACE;
}

void sendKey(WORD vk, bool down) {
  INPUT in{};
  if (vk == 0) {
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
  } else {
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
  }
  SendInput(1, &in, sizeof(INPUT));
}

// Hybrid sleep: coarse sleep to get close, then spin. Plain Sleep(16) drifts
// by whole frames, which is fatal when a macro needs 1-frame precision.
void waitUntil(LARGE_INTEGER target, LARGE_INTEGER freq) {
  LARGE_INTEGER now;
  for (;;) {
    QueryPerformanceCounter(&now);
    const double remainMs =
        1000.0 * static_cast<double>(target.QuadPart - now.QuadPart) /
        static_cast<double>(freq.QuadPart);
    if (remainMs <= 0.0) return;
    if (remainMs > 2.0) Sleep(1);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: macro_player.exe <file.macro> [--delay 3] [--key space|lmb|up|w] [--fps 60]\n");
    return 2;
  }
  std::string keyName = "space";
  int delay = 3, fpsOverride = 0;
  for (int i = 2; i < argc - 1; ++i) {
    if (!std::strcmp(argv[i], "--key")) keyName = argv[i + 1];
    if (!std::strcmp(argv[i], "--delay")) delay = std::atoi(argv[i + 1]);
    if (!std::strcmp(argv[i], "--fps")) fpsOverride = std::atoi(argv[i + 1]);
  }

  std::vector<unsigned char> holds;
  int fps = 60;
  if (!loadMacro(argv[1], &holds, &fps)) {
    std::printf("could not read macro: %s\n", argv[1]);
    return 1;
  }
  if (fpsOverride > 0) fps = fpsOverride;
  const WORD vk = keyFromName(keyName);

  std::printf("macro: %zu frames @ %d fps (%.2fs), key=%s\n", holds.size(), fps,
              static_cast<double>(holds.size()) / fps, keyName.c_str());
  std::printf("focus Geometry Dash now, starting in %ds\n", delay);
  for (int i = delay; i > 0; --i) {
    std::printf("  %d...\n", i);
    Sleep(1000);
  }

  // Best-effort scheduling priority: input jitter is the only real enemy here.
  SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  timeBeginPeriod(1);

  LARGE_INTEGER freq, start, target;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&start);
  const double ticksPerFrame =
      static_cast<double>(freq.QuadPart) / static_cast<double>(fps);

  bool down = false;
  for (size_t f = 0; f < holds.size(); ++f) {
    target.QuadPart =
        start.QuadPart + static_cast<LONGLONG>(ticksPerFrame * (f + 1));
    const bool want = holds[f] != 0;
    if (want != down) {
      sendKey(vk, want);
      down = want;
    }
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
      std::printf("aborted at frame %zu\n", f);
      break;
    }
    waitUntil(target, freq);
  }
  if (down) sendKey(vk, false);

  timeEndPeriod(1);
  std::printf("done\n");
  return 0;
}

#else

int main(int argc, char** argv) {
  std::vector<unsigned char> holds;
  int fps = 60;
  if (argc > 1 && loadMacro(argv[1], &holds, &fps)) {
    std::printf("macro is valid: %zu frames @ %d fps (%.2fs)\n", holds.size(),
                fps, static_cast<double>(holds.size()) / fps);
    std::printf("live playback requires Windows; build this file there.\n");
    return 0;
  }
  std::printf("win_macro_player: Windows-only playback. On this platform it "
              "only validates a .macro file.\n");
  return 0;
}

#endif
