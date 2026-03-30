#include "pattern_scanner.h"
#include <TlHelp32.h>

bool PatternScanner::comparePattern(const uint8_t* data, 
                                     const std::vector<uint8_t>& pattern,
                                     const std::vector<uint8_t>& mask) {
    for (size_t i = 0; i < pattern.size(); i++) {
        if (mask[i] == 0xFF && data[i] != pattern[i]) {
            return false;
        }
    }
    return true;
}

uintptr_t PatternScanner::scanPattern(HANDLE process, uintptr_t start, size_t size,
                                        const std::vector<uint8_t>& pattern,
                                        const std::vector<uint8_t>& mask) {
    std::vector<uint8_t> buffer(4096);
    size_t patternLen = pattern.size();
    
    for (uintptr_t addr = start; addr < start + size; addr += 4096 - patternLen) {
        SIZE_T bytesRead;
        if (!ReadProcessMemory(process, (LPCVOID)addr, buffer.data(), buffer.size(), &bytesRead)) {
            continue;
        }
        
        for (size_t i = 0; i < bytesRead - patternLen; i++) {
            if (comparePattern(&buffer[i], pattern, mask)) {
                return addr + i;
            }
        }
    }
    return 0;
}

// Find GameManager by looking for PlayLayer pointer pattern
uintptr_t PatternScanner::findGameManager(HANDLE process, uintptr_t baseAddr, size_t moduleSize) {
    // Scan for "mov rax, [rip+0xXXXXXXX]" followed by "mov rax, [rax+0x164]" pattern
    // This is typically how GameManager::sharedState() accesses the singleton
    
    std::vector<uint8_t> buffer(8192);
    
    for (uintptr_t addr = baseAddr; addr < baseAddr + moduleSize; addr += 4096) {
        SIZE_T bytesRead;
        if (!ReadProcessMemory(process, (LPCVOID)addr, buffer.data(), buffer.size(), &bytesRead)) {
            continue;
        }
        
        // Look for pattern: 48 8B 05 ?? ?? ?? ?? (mov rax, [rip+offset])
        for (size_t i = 0; i < bytesRead - 16; i++) {
            // Check for mov rax, [rip+0xXXXXXXX]
            if (buffer[i] == 0x48 && buffer[i+1] == 0x8B && buffer[i+2] == 0x05) {
                // Get the RIP-relative offset
                int32_t offset = *(int32_t*)&buffer[i+3];
                uintptr_t nextInstr = addr + i + 7;
                uintptr_t gameManagerPtr = nextInstr + offset;
                
                // Verify it's a valid pointer
                uintptr_t gameManager = 0;
                SIZE_T read;
                if (ReadProcessMemory(process, (LPCVOID)gameManagerPtr, &gameManager, sizeof(gameManager), &read)) {
                    if (gameManager != 0 && gameManager > 0x10000) {
                        // Check if PlayLayer offset exists at +0x164
                        uintptr_t playLayer = 0;
                        if (ReadProcessMemory(process, (LPCVOID)(gameManager + 0x164), &playLayer, sizeof(playLayer), &read)) {
                            return gameManager;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

// Alternative: Find by scanning for player's X position value
uintptr_t ValueScanner::findPlayerX(HANDLE process, uintptr_t start, size_t size) {
    // When player is at start of level, X is approximately 0-100
    // We'll scan for this value and verify by checking if it increases
    
    std::vector<uintptr_t> candidates;
    std::vector<uint8_t> buffer(4096);
    
    // First scan: find all addresses with value between 0 and 200
    for (uintptr_t addr = start; addr < start + size; addr += 4096 - sizeof(float)) {
        SIZE_T bytesRead;
        if (!ReadProcessMemory(process, (LPCVOID)addr, buffer.data(), buffer.size(), &bytesRead)) {
            continue;
        }
        
        for (size_t i = 0; i < bytesRead - sizeof(float); i += 4) {
            float val = *(float*)&buffer[i];
            if (val >= 0.0f && val <= 200.0f && val == val) { // not NaN
                candidates.push_back(addr + i);
            }
        }
    }
    
    std::cout << "[Scanner] Found " << candidates.size() << " potential X candidates" << std::endl;
    
    // TODO: Filter by checking if value increases over time
    // For now, return first candidate
    return candidates.empty() ? 0 : candidates[0];
}
