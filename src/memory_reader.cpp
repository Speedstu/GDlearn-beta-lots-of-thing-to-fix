#include "memory_reader.h"
#include <iostream>
#include <cstring>

// ============================================================================
// BotState struct must match exactly what the Geode mod sends
// ============================================================================
#pragma pack(push,1)
struct BotState {
    float    x, y, percent;
    uint8_t  isDead, isOnGround, gravityFlipped, isPlaying;
};
#pragma pack(pop)

static HANDLE s_pipe = INVALID_HANDLE_VALUE;

MemoryReader::MemoryReader() {}

MemoryReader::~MemoryReader() {
    detach();
}

bool MemoryReader::attach() {
    // Connect to the named pipe created by the Geode mod
    for (int attempts = 0; attempts < 30; attempts++) {
        s_pipe = CreateFileA(
            "\\\\.\\pipe\\GDMLBotPipe",
            GENERIC_READ,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );
        if (s_pipe != INVALID_HANDLE_VALUE) {
            attached_ = true;
            std::cout << "[PipeReader] Connected to GD pipe!" << std::endl;
            return true;
        }
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY) {
            WaitNamedPipeA("\\\\.\\pipe\\GDMLBotPipe", 2000);
        } else {
            Sleep(500);
        }
    }
    std::cerr << "[PipeReader] Failed to connect to pipe. Is GD running with the Geode mod?" << std::endl;
    return false;
}

void MemoryReader::detach() {
    if (s_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(s_pipe);
        s_pipe = INVALID_HANDLE_VALUE;
    }
    attached_ = false;
}

bool MemoryReader::isAttached() const {
    return attached_ && s_pipe != INVALID_HANDLE_VALUE;
}

bool MemoryReader::readGameState(GameState& state) {
    if (!isAttached()) return false;

    BotState bs{};
    DWORD bytesRead = 0;

    BOOL ok = ReadFile(s_pipe, &bs, sizeof(bs), &bytesRead, nullptr);
    if (!ok || bytesRead != sizeof(bs)) {
        // Pipe broken — GD closed or mod unloaded
        std::cerr << "[PipeReader] Pipe disconnected. Reconnecting..." << std::endl;
        detach();
        Sleep(1000);
        attach();
        return false;
    }

    state.isPlaying      = (bs.isPlaying != 0);
    state.playerX        = bs.x;
    state.playerY        = bs.y;
    state.percent        = bs.percent;
    state.isDead         = (bs.isDead != 0);
    state.isOnGround     = (bs.isOnGround != 0);
    state.gravityFlipped = (bs.gravityFlipped != 0);

    return true;
}

// Legacy stubs — not needed with pipe approach
DWORD MemoryReader::findProcessId(const std::string&) { return 0; }
uintptr_t MemoryReader::getModuleBase(DWORD, const std::string&) { return 0; }
uintptr_t MemoryReader::resolvePointerChain(uintptr_t, const std::vector<uintptr_t>&) { return 0; }

template<typename T>
T MemoryReader::readMem(uintptr_t) { return T{}; }

// Explicit template instantiations to avoid linker errors
template float    MemoryReader::readMem<float>(uintptr_t);
template bool     MemoryReader::readMem<bool>(uintptr_t);
template int      MemoryReader::readMem<int>(uintptr_t);
template uint8_t  MemoryReader::readMem<uint8_t>(uintptr_t);
template uint64_t MemoryReader::readMem<uint64_t>(uintptr_t);
template uintptr_t MemoryReader::readMem<uintptr_t>(uintptr_t);
