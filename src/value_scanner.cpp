#include "value_scanner.h"
#include <iostream>
#include <cmath>

std::vector<uintptr_t> ValueScanner::scanFloat(HANDLE process, uintptr_t start, size_t size, 
                                                float value, float tolerance) {
    std::vector<uintptr_t> results;
    std::vector<uint8_t> buffer(4096);
    
    for (uintptr_t addr = start; addr < start + size; addr += 4096 - sizeof(float)) {
        SIZE_T bytesRead;
        if (!ReadProcessMemory(process, (LPCVOID)addr, buffer.data(), buffer.size(), &bytesRead)) {
            continue;
        }
        
        for (size_t i = 0; i <= bytesRead - sizeof(float); i += sizeof(float)) {
            float val = *(float*)&buffer[i];
            if (std::abs(val - value) <= tolerance) {
                results.push_back(addr + i);
            }
        }
    }
    
    return results;
}

uintptr_t ValueScanner::findPlayLayerByPercent(HANDLE process, uintptr_t start, size_t size, 
                                                  float expectedPercent) {
    std::cout << "[ValueScanner] Scanning for PlayLayer by percent (expected: " << expectedPercent << ")..." << std::endl;
    
    auto candidates = scanFloat(process, start, size, expectedPercent, 1.0f);
    
    std::cout << "[ValueScanner] Found " << candidates.size() << " candidates with percent near " << expectedPercent << std::endl;
    
    // Try to validate each candidate
    for (uintptr_t addr : candidates) {
        // PlayLayer + 0x3C0 = percent
        // So PlayLayer = addr - 0x3C0
        uintptr_t playLayer = addr - 0x3C0;
        
        // Validate PlayLayer - check if it's aligned and in valid range
        if (playLayer > 0x10000 && playLayer < 0x7FFFFFFFFFFF && (playLayer & 0xFFF) == 0) {
            // Try to read other PlayLayer values to confirm
            float levelLength = 0;
            SIZE_T read;
            ReadProcessMemory(process, (LPCVOID)(playLayer + 0x3B4), &levelLength, sizeof(levelLength), &read);
            
            if (levelLength > 0 && levelLength < 100000) {  // Reasonable level length
                std::cout << "[ValueScanner] Found valid PlayLayer at: 0x" << std::hex << playLayer 
                          << std::dec << " (level length: " << levelLength << ")" << std::endl;
                return playLayer;
            }
        }
    }
    
    return 0;
}

uintptr_t ValueScanner::findPlayerByPosition(HANDLE process, uintptr_t start, size_t size, 
                                               float expectedX, float expectedY) {
    std::cout << "[ValueScanner] Scanning for Player by position (X: " << expectedX << ", Y: " << expectedY << ")..." << std::endl;
    
    // Scan with wider tolerance for X (player might have moved slightly)
    auto xCandidates = scanFloat(process, start, size, expectedX, 50.0f);
    
    std::cout << "[ValueScanner] Found " << xCandidates.size() << " X candidates" << std::endl;
    
    // For each X candidate, check if it has valid Player structure
    for (uintptr_t xAddr : xCandidates) {
        // Calculate potential Player base
        uintptr_t player = xAddr - 0x67C;
        
        if (player < 0x10000 || player > 0x7FFFFFFFFFFF) continue;
        
        // Read and validate multiple Player fields
        float x, y, speed;
        uint8_t onGround, isDead;
        SIZE_T read;
        
        // Read X
        if (!ReadProcessMemory(process, (LPCVOID)(player + 0x67C), &x, sizeof(x), &read)) continue;
        
        // Read Y (should be 4 bytes after X)
        if (!ReadProcessMemory(process, (LPCVOID)(player + 0x680), &y, sizeof(y), &read)) continue;
        
        // Read speed
        if (!ReadProcessMemory(process, (LPCVOID)(player + 0x648), &speed, sizeof(speed), &read)) continue;
        
        // Read onGround
        if (!ReadProcessMemory(process, (LPCVOID)(player + 0x73E), &onGround, sizeof(onGround), &read)) continue;
        
        // Read isDead
        if (!ReadProcessMemory(process, (LPCVOID)(player + 0x63F), &isDead, sizeof(isDead), &read)) continue;
        
        // Validate all fields
        bool validX = (x >= -1000.0f && x <= 50000.0f);
        bool validY = (y >= 0.0f && y <= 2000.0f);
        bool validSpeed = (speed >= -1000.0f && speed <= 1000.0f);
        bool validOnGround = (onGround == 0 || onGround == 1);
        bool validIsDead = (isDead == 0 || isDead == 1);
        
        // Check Y is close to expected (player at start of level)
        bool yMatches = std::abs(y - expectedY) < 50.0f;
        
        if (validX && validY && validSpeed && validOnGround && validIsDead && yMatches) {
            std::cout << "[ValueScanner] Found VALID Player at: 0x" << std::hex << player << std::dec << std::endl;
            std::cout << "  X=" << x << " Y=" << y << " Speed=" << speed 
                      << " OnGround=" << (int)onGround << " Dead=" << (int)isDead << std::endl;
            return player;
        }
    }
    
    std::cout << "[ValueScanner] No valid Player found with multi-field validation" << std::endl;
    return 0;
}

std::vector<uintptr_t> ValueScanner::scanPointerTo(HANDLE process, uintptr_t start, size_t size, 
                                                    uintptr_t target) {
    std::vector<uintptr_t> results;
    std::vector<uint8_t> buffer(4096);
    
    for (uintptr_t addr = start; addr < start + size; addr += 4096 - sizeof(uintptr_t)) {
        SIZE_T bytesRead;
        if (!ReadProcessMemory(process, (LPCVOID)addr, buffer.data(), buffer.size(), &bytesRead)) {
            continue;
        }
        
        for (size_t i = 0; i <= bytesRead - sizeof(uintptr_t); i += sizeof(uintptr_t)) {
            uintptr_t val = *(uintptr_t*)&buffer[i];
            if (val == target) {
                results.push_back(addr + i);
            }
        }
    }
    
    return results;
}
