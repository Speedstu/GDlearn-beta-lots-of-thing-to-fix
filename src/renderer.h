#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include <functional>
#include "level_parser.h"
#include "config.h"

// Live game state for rendering
struct RenderState {
    float playerX = 0.0f;
    float playerY = 0.0f;
    float playerSpeed = 0.0f;
    float percent = 0.0f;
    bool isDead = false;
    bool isOnGround = false;
    bool isHolding = false;  // Bot is pressing jump
    int gameMode = 0;        // 0=cube,1=ship,2=ball,3=ufo,4=wave,5=robot,6=spider,7=swing
    float cameraX = 0.0f;    // Camera offset for scrolling
    bool gravityFlipped = false;
    float rotation = 0.0f;
    
    // Trajectory trail (last N positions)
    std::vector<std::pair<float, float>> trail;
    
    // Debug info
    float reward = 0.0f;
    float value = 0.0f;
    int episode = 0;
    int step = 0;
    std::string levelName;
    int levelsCompleted = 0;
    float bestProgress = 0.0f;
    float avgProgress = 0.0f;
    float entropy = 0.0f;
};

// ============================================================================
// GD-Faithful 2D Renderer
// Uses Win32 GDI with pre-cached brushes/pens to avoid GDI resource leaks
// ============================================================================
class Renderer {
public:
    Renderer(int width = 1280, int height = 720);
    ~Renderer();
    
    // Window management
    bool createWindow(const std::string& title);
    bool isOpen() const { return window_ != nullptr && !shouldClose_; }
    void close() { shouldClose_ = true; }
    bool shouldClose() const { return shouldClose_; }
    void pollEvents();
    
    // Rendering
    void clear();
    void renderLevel(const LevelData& level, const RenderState& state);
    void present();
    
    // State updates
    void updateState(const RenderState& state) { currentState_ = state; }
    void setLevel(const LevelData& level) { currentLevel_ = level; }
    
    // Rate limiting
    void setFPS(int fps) { targetFPS_ = fps; }
    void setSpeedMultiplier(float mult) { speedMultiplier_ = mult; }
    void syncFrame();
    
private:
    // Window
    HWND window_ = nullptr;
    HDC hdc_ = nullptr;
    HDC memDC_ = nullptr;
    HBITMAP memBitmap_ = nullptr;
    int width_ = 1280;
    int height_ = 720;
    bool shouldClose_ = false;
    int targetFPS_ = 60;
    float speedMultiplier_ = 4.0f;
    
    // State
    RenderState currentState_;
    LevelData currentLevel_;
    
    // Timing
    LARGE_INTEGER freq_;
    LARGE_INTEGER lastFrameTime_;
    
    // ---- Cached GDI resources (created once, never leaked) ----
    HBRUSH brBgTop_ = nullptr;       // Dark blue-purple gradient top
    HBRUSH brBgBot_ = nullptr;       // Darker gradient bottom
    HBRUSH brGround_ = nullptr;      // Ground fill
    HBRUSH brGroundDark_ = nullptr;  // Ground checkerboard dark
    HBRUSH brGroundLight_ = nullptr; // Ground checkerboard light
    HBRUSH brBlock_ = nullptr;       // Block body
    HBRUSH brBlockHighlight_ = nullptr;
    HBRUSH brBlockDark_ = nullptr;   // Block shadow/outline
    HBRUSH brSpike_ = nullptr;
    HBRUSH brOrb_ = nullptr;
    HBRUSH brOrbGlow_ = nullptr;
    HBRUSH brPad_ = nullptr;
    HBRUSH brPortal_ = nullptr;
    HBRUSH brPlayer_ = nullptr;
    HBRUSH brPlayerIcon_ = nullptr;
    HBRUSH brPlayerOutline_ = nullptr;
    HBRUSH brTrail_ = nullptr;
    HBRUSH brUiPanel_ = nullptr;
    HBRUSH brProgressBg_ = nullptr;
    HBRUSH brProgressFill_ = nullptr;
    HBRUSH brDead_ = nullptr;
    HBRUSH brClick_ = nullptr;
    HBRUSH brNoClick_ = nullptr;
    HBRUSH brBlack_ = nullptr;
    HBRUSH brWhite_ = nullptr;
    
    HPEN penGroundLine_ = nullptr;
    HPEN penGrid_ = nullptr;
    HPEN penBlockOutline_ = nullptr;
    HPEN penSpike_ = nullptr;
    HPEN penOrbRing_ = nullptr;
    HPEN penPortal_ = nullptr;
    HPEN penPlayer_ = nullptr;
    HPEN penTrail_ = nullptr;
    HPEN penNull_ = nullptr;         // NULL_PEN for no outline
    
    HFONT fontUI_ = nullptr;
    HFONT fontBig_ = nullptr;
    HFONT fontSmall_ = nullptr;
    
    void createGDIResources();
    void destroyGDIResources();
    
    // ---- Drawing helpers (use cached resources) ----
    void fillScreenRect(int x, int y, int w, int h, HBRUSH brush);
    void drawScreenTriangle(int x1, int y1, int x2, int y2, int x3, int y3, HBRUSH brush, HPEN pen);
    void drawScreenCircle(int cx, int cy, int r, HBRUSH brush, HPEN pen);
    void drawScreenText(int x, int y, const std::string& text, COLORREF color, HFONT font = nullptr, UINT flags = DT_LEFT);
    
    // ---- Scene elements ----
    void drawBackground();
    void drawGround();
    void drawCheckerboard();
    void drawGridLines();
    
    // ---- Object renderers ----
    void drawBlock(const LevelObject& obj);
    void drawSpike(const LevelObject& obj);
    void drawOrb(const LevelObject& obj);
    void drawPad(const LevelObject& obj);
    void drawPortal(const LevelObject& obj);
    
    // ---- Player ----
    void drawPlayer();
    void drawTrail();
    
    // ---- HUD ----
    void drawHUD();
    void drawProgressBar();
    void drawDeathOverlay();
    
    // ---- Coordinate transform ----
    float worldToScreenX(float x) const;
    float worldToScreenY(float y) const;
    float worldScale() const;
    
    // Static window procedure
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
