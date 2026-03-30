#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <string>
#include <vector>
#include <cstdint>
#include <iostream>

// All game state data read from GD process memory
struct GameState {
    // Player data
    float playerX = 0.0f;
    float playerY = 0.0f;
    float playerSpeed = 0.0f;
    float playerYAccel = 0.0f;
    float playerRotation = 0.0f;
    float playerSize = 1.0f;
    bool isDead = false;
    bool isOnGround = true;
    bool gravityFlipped = false;
    bool isHolding = false;
    int gameMode = 0;   // 0=cube,1=ship,2=ball,3=ufo,4=wave,5=robot,6=spider,7=swing

    // Level data
    float levelLength = 0.0f;
    float percent = 0.0f;
    int attempt = 0;
    bool isPractice = false;
    bool isPlaying = false;

    // Simulator-only fields (not read from memory)
    float nearestHazardDist = 999.0f;  // distance to nearest spike ahead
    bool justJumped = false;           // did the player just leave ground this frame
    bool isAirborne = false;           // in the air (not on ground)

    // Derived
    float progressPercent() const {
        return (levelLength > 0.0f) ? (playerX / levelLength) * 100.0f : 0.0f;
    }
};

class MemoryReader {
public:
    MemoryReader();
    ~MemoryReader();

    bool attach();
    void detach();
    bool isAttached() const;

    bool readGameState(GameState& state);
    uintptr_t getBaseAddress() const { return baseAddress_; }

private:
    HANDLE processHandle_ = nullptr;
    DWORD processId_ = 0;
    uintptr_t baseAddress_ = 0;
    bool attached_ = false;

    DWORD findProcessId(const std::string& processName);
    uintptr_t getModuleBase(DWORD pid, const std::string& moduleName);

    template<typename T>
    T readMem(uintptr_t address);

    uintptr_t resolvePointerChain(uintptr_t base, const std::vector<uintptr_t>& offsets);
};
