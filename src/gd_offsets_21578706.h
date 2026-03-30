// GD Build 21578706 - Manual Offset Configuration
// Use Cheat Engine to find these offsets and update them here

#pragma once
#include <cstdint>

namespace gd_build_21578706 {
    // GameManager - find with AOB scan for "48 8B 05 ?? ?? ?? ??" in game code
    // Then check which pointer leads to valid PlayLayer
    static const uintptr_t GAMEMANAGER_OFFSET = 0x4FF8C0;  // Try: 0x5230D0, 0x50E0D0, etc.
    
    // PlayLayer offset from GameManager
    // Look for pointer that is 0 when not in level, non-zero when in level
    static const uintptr_t PLAYLAYER_OFFSET_GM = 0x168;  // Try: 0x164, 0x170, 0x180
    
    // Player1 offset from PlayLayer
    // Look for pointer with X at +0x67C, Y at +0x680 that CHANGES when player moves
    static const uintptr_t PLAYER1_OFFSET_PL = 0x228;  // Try: 0x224, 0x22C, 0x230
    
    // Player data offsets from Player1 base
    static const uintptr_t OFF_X = 0x67C;
    static const uintptr_t OFF_Y = 0x680;
    static const uintptr_t OFF_SPEED = 0x648;
    static const uintptr_t OFF_IS_DEAD = 0x63F;
    static const uintptr_t OFF_ON_GROUND = 0x73E;
    
    // PlayLayer data offsets
    static const uintptr_t OFF_PERCENT = 0x3C8;  // Try: 0x3C0, 0x3B4, 0x3D0
}
