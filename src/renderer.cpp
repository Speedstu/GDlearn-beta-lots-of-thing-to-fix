#include "renderer.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

// Window instance for message handling
static Renderer* g_renderer = nullptr;

// ============================================================================
// GD Color Palette — faithful to the real game
// ============================================================================
// Background (Stereo Madness style dark blue)
static const COLORREF COL_BG_TOP       = RGB(10, 15, 40);
static const COLORREF COL_BG_MID       = RGB(15, 20, 55);
static const COLORREF COL_BG_BOT       = RGB(5, 8, 25);

// Ground
static const COLORREF COL_GROUND_TOP   = RGB(0, 100, 255);    // Blue ground line (GD style)
static const COLORREF COL_GROUND_DARK  = RGB(8, 12, 35);      // Dark checker
static const COLORREF COL_GROUND_LIGHT = RGB(15, 22, 50);     // Light checker
static const COLORREF COL_GROUND_LINE  = RGB(0, 150, 255);    // Bright top edge

// Objects
static const COLORREF COL_BLOCK        = RGB(50, 60, 90);     // Block fill
static const COLORREF COL_BLOCK_TOP    = RGB(80, 95, 140);    // Block highlight
static const COLORREF COL_BLOCK_EDGE   = RGB(30, 35, 55);     // Block outline
static const COLORREF COL_SPIKE        = RGB(50, 60, 90);     // Spike body (same hue as blocks)
static const COLORREF COL_SPIKE_EDGE   = RGB(30, 35, 55);     // Spike outline
static const COLORREF COL_ORB_YELLOW   = RGB(255, 220, 50);   // Yellow jump orb
static const COLORREF COL_ORB_BLUE     = RGB(50, 180, 255);   // Blue gravity orb
static const COLORREF COL_ORB_GLOW     = RGB(255, 255, 200);  // Orb glow
static const COLORREF COL_PAD_YELLOW   = RGB(255, 220, 50);   // Yellow pad
static const COLORREF COL_PAD_PINK     = RGB(255, 80, 200);   // Pink pad
static const COLORREF COL_PORTAL_BLUE  = RGB(0, 170, 255);    // Blue portal
static const COLORREF COL_PORTAL_ORANGE= RGB(255, 140, 0);    // Orange portal
static const COLORREF COL_PORTAL_GREEN = RGB(0, 255, 100);    // Green speed portal
static const COLORREF COL_PORTAL_PURPLE= RGB(170, 50, 255);   // Purple portal

// Player
static const COLORREF COL_PLAYER       = RGB(125, 255, 0);    // Classic GD green
static const COLORREF COL_PLAYER_2     = RGB(0, 255, 125);    // Secondary color
static const COLORREF COL_PLAYER_GLOW  = RGB(125, 255, 0);    // Glow
static const COLORREF COL_TRAIL        = RGB(125, 255, 0);    // Trail

// HUD
static const COLORREF COL_HUD_BG      = RGB(0, 0, 0);        // HUD background
static const COLORREF COL_HUD_TEXT     = RGB(255, 255, 255);  // White text
static const COLORREF COL_PROGRESS_BG  = RGB(30, 30, 30);     // Progress bar background
static const COLORREF COL_PROGRESS_FG  = RGB(0, 255, 100);    // Progress bar fill
static const COLORREF COL_DEAD         = RGB(255, 0, 0);      // Death indicator
static const COLORREF COL_CLICK_ON     = RGB(0, 255, 80);     // Click active
static const COLORREF COL_CLICK_OFF    = RGB(80, 80, 80);     // Click inactive

// ============================================================================
// Constructor / Destructor
// ============================================================================
Renderer::Renderer(int width, int height) : width_(width), height_(height) {
    QueryPerformanceFrequency(&freq_);
    QueryPerformanceCounter(&lastFrameTime_);
    currentState_.trail.reserve(200);
}

Renderer::~Renderer() {
    destroyGDIResources();
    if (memBitmap_) DeleteObject(memBitmap_);
    if (memDC_) DeleteDC(memDC_);
    if (hdc_) ReleaseDC(window_, hdc_);
    if (window_) DestroyWindow(window_);
}

// ============================================================================
// GDI Resource Management — created ONCE, destroyed ONCE
// ============================================================================
void Renderer::createGDIResources() {
    // Brushes
    brBgTop_          = CreateSolidBrush(COL_BG_TOP);
    brBgBot_          = CreateSolidBrush(COL_BG_BOT);
    brGround_         = CreateSolidBrush(COL_GROUND_TOP);
    brGroundDark_     = CreateSolidBrush(COL_GROUND_DARK);
    brGroundLight_    = CreateSolidBrush(COL_GROUND_LIGHT);
    brBlock_          = CreateSolidBrush(COL_BLOCK);
    brBlockHighlight_ = CreateSolidBrush(COL_BLOCK_TOP);
    brBlockDark_      = CreateSolidBrush(COL_BLOCK_EDGE);
    brSpike_          = CreateSolidBrush(COL_SPIKE);
    brOrb_            = CreateSolidBrush(COL_ORB_YELLOW);
    brOrbGlow_        = CreateSolidBrush(COL_ORB_GLOW);
    brPad_            = CreateSolidBrush(COL_PAD_YELLOW);
    brPortal_         = CreateSolidBrush(COL_PORTAL_BLUE);
    brPlayer_         = CreateSolidBrush(COL_PLAYER);
    brPlayerIcon_     = CreateSolidBrush(COL_PLAYER_2);
    brPlayerOutline_  = CreateSolidBrush(RGB(255, 255, 255));
    brTrail_          = CreateSolidBrush(COL_TRAIL);
    brUiPanel_        = CreateSolidBrush(RGB(0, 0, 0));
    brProgressBg_     = CreateSolidBrush(COL_PROGRESS_BG);
    brProgressFill_   = CreateSolidBrush(COL_PROGRESS_FG);
    brDead_           = CreateSolidBrush(COL_DEAD);
    brClick_          = CreateSolidBrush(COL_CLICK_ON);
    brNoClick_        = CreateSolidBrush(COL_CLICK_OFF);
    brBlack_          = CreateSolidBrush(RGB(0, 0, 0));
    brWhite_          = CreateSolidBrush(RGB(255, 255, 255));

    // Pens
    penGroundLine_    = CreatePen(PS_SOLID, 3, COL_GROUND_LINE);
    penGrid_          = CreatePen(PS_SOLID, 1, RGB(25, 35, 65));
    penBlockOutline_  = CreatePen(PS_SOLID, 1, COL_BLOCK_EDGE);
    penSpike_         = CreatePen(PS_SOLID, 2, COL_SPIKE_EDGE);
    penOrbRing_       = CreatePen(PS_SOLID, 2, COL_ORB_GLOW);
    penPortal_        = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    penPlayer_        = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    penTrail_         = CreatePen(PS_SOLID, 3, COL_TRAIL);
    penNull_          = (HPEN)GetStockObject(NULL_PEN);

    // Fonts
    fontUI_    = CreateFontA(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
    fontBig_   = CreateFontA(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
    fontSmall_ = CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
}

void Renderer::destroyGDIResources() {
    // Brushes
    if (brBgTop_)          DeleteObject(brBgTop_);
    if (brBgBot_)          DeleteObject(brBgBot_);
    if (brGround_)         DeleteObject(brGround_);
    if (brGroundDark_)     DeleteObject(brGroundDark_);
    if (brGroundLight_)    DeleteObject(brGroundLight_);
    if (brBlock_)          DeleteObject(brBlock_);
    if (brBlockHighlight_) DeleteObject(brBlockHighlight_);
    if (brBlockDark_)      DeleteObject(brBlockDark_);
    if (brSpike_)          DeleteObject(brSpike_);
    if (brOrb_)            DeleteObject(brOrb_);
    if (brOrbGlow_)        DeleteObject(brOrbGlow_);
    if (brPad_)            DeleteObject(brPad_);
    if (brPortal_)         DeleteObject(brPortal_);
    if (brPlayer_)         DeleteObject(brPlayer_);
    if (brPlayerIcon_)     DeleteObject(brPlayerIcon_);
    if (brPlayerOutline_)  DeleteObject(brPlayerOutline_);
    if (brTrail_)          DeleteObject(brTrail_);
    if (brUiPanel_)        DeleteObject(brUiPanel_);
    if (brProgressBg_)     DeleteObject(brProgressBg_);
    if (brProgressFill_)   DeleteObject(brProgressFill_);
    if (brDead_)           DeleteObject(brDead_);
    if (brClick_)          DeleteObject(brClick_);
    if (brNoClick_)        DeleteObject(brNoClick_);
    if (brBlack_)          DeleteObject(brBlack_);
    if (brWhite_)          DeleteObject(brWhite_);

    // Pens (don't delete stock objects)
    if (penGroundLine_)    DeleteObject(penGroundLine_);
    if (penGrid_)          DeleteObject(penGrid_);
    if (penBlockOutline_)  DeleteObject(penBlockOutline_);
    if (penSpike_)         DeleteObject(penSpike_);
    if (penOrbRing_)       DeleteObject(penOrbRing_);
    if (penPortal_)        DeleteObject(penPortal_);
    if (penPlayer_)        DeleteObject(penPlayer_);
    if (penTrail_)         DeleteObject(penTrail_);
    // penNull_ is stock object, don't delete

    // Fonts
    if (fontUI_)    DeleteObject(fontUI_);
    if (fontBig_)   DeleteObject(fontBig_);
    if (fontSmall_) DeleteObject(fontSmall_);
}

// ============================================================================
// Window Creation
// ============================================================================
bool Renderer::createWindow(const std::string& title) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "GDLearnCPP_Renderer";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

    RegisterClassA(&wc); // OK if already registered

    // Adjust window size to account for borders
    RECT rc = {0, 0, width_, height_};
    DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);

    window_ = CreateWindowA(
        "GDLearnCPP_Renderer", title.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );
    if (!window_) {
        std::cerr << "[Renderer] Failed to create window" << std::endl;
        return false;
    }

    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);

    hdc_ = GetDC(window_);
    memDC_ = CreateCompatibleDC(hdc_);
    memBitmap_ = CreateCompatibleBitmap(hdc_, width_, height_);
    SelectObject(memDC_, memBitmap_);

    // Create all cached GDI resources
    createGDIResources();

    g_renderer = this;
    std::cout << "[Renderer] Created " << width_ << "x" << height_ << " GD-style window" << std::endl;
    return true;
}

LRESULT CALLBACK Renderer::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            if (g_renderer) g_renderer->shouldClose_ = true;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE && g_renderer)
                g_renderer->shouldClose_ = true;
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void Renderer::pollEvents() {
    MSG msg;
    while (PeekMessage(&msg, window_, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

// ============================================================================
// Frame Management
// ============================================================================
void Renderer::clear() {
    // Background is drawn in drawBackground(), just need a base clear
    RECT r = {0, 0, width_, height_};
    FillRect(memDC_, &r, brBlack_);
}

void Renderer::present() {
    BitBlt(hdc_, 0, 0, width_, height_, memDC_, 0, 0, SRCCOPY);
}

void Renderer::syncFrame() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double frameTime = 1000.0 / (targetFPS_ * speedMultiplier_);
    double elapsed = (double)(now.QuadPart - lastFrameTime_.QuadPart) * 1000.0 / freq_.QuadPart;
    if (elapsed < frameTime) Sleep((DWORD)(frameTime - elapsed));
    QueryPerformanceCounter(&lastFrameTime_);
}

// ============================================================================
// Coordinate Transforms
// ============================================================================
float Renderer::worldScale() const {
    // Scale: 1 GD block = 30 world units. We want blocks to be ~24px on screen.
    return 0.8f;
}

float Renderer::worldToScreenX(float x) const {
    float scale = worldScale();
    float viewX = x - currentState_.cameraX;
    return viewX * scale + width_ * 0.25f;
}

float Renderer::worldToScreenY(float y) const {
    float scale = worldScale();
    // Ground (y=15) at 75% of screen height
    float groundScreenY = height_ * 0.75f;
    return groundScreenY - (y - 15.0f) * scale;
}

// ============================================================================
// Low-Level Drawing Helpers (use CACHED resources only)
// ============================================================================
void Renderer::fillScreenRect(int x, int y, int w, int h, HBRUSH brush) {
    RECT r = {x, y, x + w, y + h};
    FillRect(memDC_, &r, brush);
}

void Renderer::drawScreenTriangle(int x1, int y1, int x2, int y2, int x3, int y3, HBRUSH brush, HPEN pen) {
    POINT pts[3] = {{x1, y1}, {x2, y2}, {x3, y3}};
    HGDIOBJ oldBrush = SelectObject(memDC_, brush);
    HGDIOBJ oldPen = SelectObject(memDC_, pen);
    Polygon(memDC_, pts, 3);
    SelectObject(memDC_, oldBrush);
    SelectObject(memDC_, oldPen);
}

void Renderer::drawScreenCircle(int cx, int cy, int r, HBRUSH brush, HPEN pen) {
    HGDIOBJ oldBrush = SelectObject(memDC_, brush);
    HGDIOBJ oldPen = SelectObject(memDC_, pen);
    Ellipse(memDC_, cx - r, cy - r, cx + r, cy + r);
    SelectObject(memDC_, oldBrush);
    SelectObject(memDC_, oldPen);
}

void Renderer::drawScreenText(int x, int y, const std::string& text, COLORREF color, HFONT font, UINT flags) {
    if (font) SelectObject(memDC_, font);
    SetTextColor(memDC_, color);
    SetBkMode(memDC_, TRANSPARENT);
    RECT r = {x, y, x + 500, y + 30};
    DrawTextA(memDC_, text.c_str(), (int)text.length(), &r, flags);
}

// ============================================================================
// Background — dark blue gradient like real GD
// ============================================================================
void Renderer::drawBackground() {
    // Simulate a gradient: top is darker, bottom slightly lighter (before ground)
    int groundY = (int)(height_ * 0.75f);

    // Top section — dark
    int bandH = groundY / 4;
    for (int i = 0; i < 4; i++) {
        int r = 10 + i * 2;
        int g = 15 + i * 3;
        int b = 40 + i * 5;
        HBRUSH band = CreateSolidBrush(RGB(r, g, b));
        RECT rc = {0, i * bandH, width_, (i + 1) * bandH};
        FillRect(memDC_, &rc, band);
        DeleteObject(band);
    }
}

// ============================================================================
// Ground — checkerboard pattern like real GD
// ============================================================================
void Renderer::drawGround() {
    float scale = worldScale();
    int groundScreenY = (int)(height_ * 0.75f);

    // Ground body below the line (dark checkerboard)
    drawCheckerboard();

    // Bright blue line at ground level (iconic GD look)
    HGDIOBJ oldPen = SelectObject(memDC_, penGroundLine_);
    MoveToEx(memDC_, 0, groundScreenY, nullptr);
    LineTo(memDC_, width_, groundScreenY);
    SelectObject(memDC_, oldPen);
}

void Renderer::drawCheckerboard() {
    float scale = worldScale();
    int groundScreenY = (int)(height_ * 0.75f);
    int cellSize = (int)(30.0f * scale);  // 1 block = 30 units
    if (cellSize < 4) cellSize = 4;

    float camX = currentState_.cameraX;
    int startBlock = (int)(camX / 30.0f) - 1;
    int endBlock = startBlock + (width_ / cellSize) + 3;

    for (int bx = startBlock; bx < endBlock; bx++) {
        float worldX = bx * 30.0f;
        int sx = (int)worldToScreenX(worldX);

        for (int by = 0; by < 10; by++) {
            int sy = groundScreenY + by * cellSize;
            if (sy > height_) break;

            bool dark = ((bx + by) % 2 == 0);
            HBRUSH br = dark ? brGroundDark_ : brGroundLight_;
            RECT r = {sx, sy, sx + cellSize + 1, sy + cellSize + 1};
            FillRect(memDC_, &r, br);
        }
    }
}

void Renderer::drawGridLines() {
    // Subtle vertical grid lines in the background (like GD's BG grid)
    float scale = worldScale();
    float camX = currentState_.cameraX;
    int groundScreenY = (int)(height_ * 0.75f);

    HGDIOBJ oldPen = SelectObject(memDC_, penGrid_);

    // Vertical lines every 4 blocks (120 units)
    float gridSpacing = 120.0f;
    int startGrid = (int)(camX / gridSpacing);
    int endGrid = startGrid + (int)(width_ / (gridSpacing * scale)) + 2;

    for (int i = startGrid; i <= endGrid; i++) {
        float wx = i * gridSpacing;
        int sx = (int)worldToScreenX(wx);
        if (sx >= 0 && sx <= width_) {
            MoveToEx(memDC_, sx, 0, nullptr);
            LineTo(memDC_, sx, groundScreenY);
        }
    }

    // Horizontal lines every 4 blocks
    for (int i = 0; i < 8; i++) {
        float wy = 15.0f + i * 120.0f;
        int sy = (int)worldToScreenY(wy);
        if (sy >= 0 && sy <= groundScreenY) {
            MoveToEx(memDC_, 0, sy, nullptr);
            LineTo(memDC_, width_, sy);
        }
    }

    SelectObject(memDC_, oldPen);
}

// ============================================================================
// Object Renderers
// ============================================================================
void Renderer::drawBlock(const LevelObject& obj) {
    float scale = worldScale();
    int sx = (int)worldToScreenX(obj.x);
    // obj.y is center. Top of block = obj.y + h/2, Bottom = obj.y - h/2
    // worldToScreenY maps: higher world Y = higher on screen (lower screen Y)
    int screenTop    = (int)worldToScreenY(obj.y + obj.hitboxH * 0.5f);
    int screenBottom = (int)worldToScreenY(obj.y - obj.hitboxH * 0.5f);
    int w = (int)(obj.hitboxW * scale);
    int h = screenBottom - screenTop;
    if (h < 1) h = 1;

    // Main body
    RECT body = {sx - w/2, screenTop, sx + w/2, screenTop + h};
    FillRect(memDC_, &body, brBlock_);

    // Top highlight (3px bright strip)
    RECT top = {sx - w/2, screenTop, sx + w/2, screenTop + 3};
    FillRect(memDC_, &top, brBlockHighlight_);

    // Outline
    HGDIOBJ oldPen = SelectObject(memDC_, penBlockOutline_);
    HGDIOBJ oldBrush = SelectObject(memDC_, (HBRUSH)GetStockObject(NULL_BRUSH));
    Rectangle(memDC_, sx - w/2, screenTop, sx + w/2, screenTop + h);
    SelectObject(memDC_, oldPen);
    SelectObject(memDC_, oldBrush);
}

void Renderer::drawSpike(const LevelObject& obj) {
    float scale = worldScale();
    int sx = (int)worldToScreenX(obj.x);

    // Spike objects store a smaller gameplay hitbox near the tip, not the full
    // visual triangle. Anchor the rendered base from the hitbox top so ground
    // spikes sit flush on the block surface.
    float spikeTipY = obj.y + obj.hitboxH * 0.5f;
    float spikeBaseY = spikeTipY - 30.0f;
    int baseY = (int)worldToScreenY(spikeBaseY);
    int halfW = (int)(obj.hitboxW * scale * 0.5f);
    int tipH = (int)(30.0f * scale);

    // Triangle: base at bottom (touching block), tip at top
    int x1 = sx;         int y1 = baseY - tipH;
    int x2 = sx - halfW; int y2 = baseY;
    int x3 = sx + halfW; int y3 = baseY;

    drawScreenTriangle(x1, y1, x2, y2, x3, y3, brSpike_, penSpike_);
}

void Renderer::drawOrb(const LevelObject& obj) {
    float scale = worldScale();
    int sx = (int)worldToScreenX(obj.x);
    int sy = (int)worldToScreenY(obj.y);
    int r = (int)(obj.hitboxW * scale * 0.4f);

    // Outer glow ring
    drawScreenCircle(sx, sy, r + 4, brBlack_, penOrbRing_);
    // Inner orb
    drawScreenCircle(sx, sy, r, brOrb_, penNull_);
    // Bright center spot
    int sr = r / 3;
    drawScreenCircle(sx - sr/2, sy - sr/2, sr, brOrbGlow_, penNull_);
}

void Renderer::drawPad(const LevelObject& obj) {
    float scale = worldScale();
    int sx = (int)worldToScreenX(obj.x);
    int sy = (int)worldToScreenY(obj.y);
    int w = (int)(obj.hitboxW * scale * 1.0f);
    int h = (int)(8.0f * scale);

    // Pad body
    RECT body = {sx - w/2, sy - h/2, sx + w/2, sy + h/2};
    FillRect(memDC_, &body, brPad_);

    // Bright top edge
    RECT topEdge = {sx - w/2, sy - h/2, sx + w/2, sy - h/2 + 2};
    FillRect(memDC_, &topEdge, brOrbGlow_);
}

void Renderer::drawPortal(const LevelObject& obj) {
    float scale = worldScale();
    int sx = (int)worldToScreenX(obj.x);
    int sy = (int)worldToScreenY(obj.y);
    int rw = (int)(15.0f * scale);
    int rh = (int)(40.0f * scale);

    // Choose color based on portal type
    HBRUSH portalBrush = brPortal_;
    COLORREF portalColor = COL_PORTAL_BLUE;
    switch (obj.type) {
        case ObjectType::PORTAL_GRAVITY:
            portalBrush = brPortal_; // blue
            break;
        case ObjectType::PORTAL_SHIP:
        case ObjectType::PORTAL_UFO:
        case ObjectType::PORTAL_WAVE:
        case ObjectType::PORTAL_SWING:
            // Create a temp pen for non-blue portals — but use cached brush
            break;
        default:
            break;
    }

    // Oval portal shape
    HGDIOBJ oldBrush = SelectObject(memDC_, portalBrush);
    HGDIOBJ oldPen = SelectObject(memDC_, penPortal_);
    Ellipse(memDC_, sx - rw, sy - rh, sx + rw, sy + rh);
    SelectObject(memDC_, oldBrush);
    SelectObject(memDC_, oldPen);

    // Inner bright line
    int irw = rw - 4;
    int irh = rh - 4;
    if (irw > 2 && irh > 2) {
        HGDIOBJ oldBr2 = SelectObject(memDC_, (HBRUSH)GetStockObject(NULL_BRUSH));
        HGDIOBJ oldPn2 = SelectObject(memDC_, penPortal_);
        Ellipse(memDC_, sx - irw, sy - irh, sx + irw, sy + irh);
        SelectObject(memDC_, oldBr2);
        SelectObject(memDC_, oldPn2);
    }
}

// ============================================================================
// Player
// ============================================================================
void Renderer::drawPlayer() {
    float scale = worldScale();
    int sx = (int)worldToScreenX(currentState_.playerX);
    int sy = (int)worldToScreenY(currentState_.playerY);
    int size = (int)(28.0f * scale);
    int half = size / 2;

    int gameMode = currentState_.gameMode;

    if (gameMode == 0 || gameMode == 5 || gameMode == 6) {
        // Cube / Robot / Spider — square icon
        RECT body = {sx - half, sy - half, sx + half, sy + half};
        FillRect(memDC_, &body, brPlayer_);

        // White outline
        HGDIOBJ oldPen = SelectObject(memDC_, penPlayer_);
        HGDIOBJ oldBrush = SelectObject(memDC_, (HBRUSH)GetStockObject(NULL_BRUSH));
        Rectangle(memDC_, sx - half, sy - half, sx + half, sy + half);
        SelectObject(memDC_, oldPen);
        SelectObject(memDC_, oldBrush);

        // Eyes
        int eyeSize = (std::max)(2, size / 8);
        int eyeY = sy - size / 6;
        int eyeX1 = sx + size / 8;
        int eyeX2 = eyeX1 + eyeSize * 2;
        RECT e1 = {eyeX1, eyeY, eyeX1 + eyeSize, eyeY + eyeSize};
        RECT e2 = {eyeX2, eyeY, eyeX2 + eyeSize, eyeY + eyeSize};
        FillRect(memDC_, &e1, brBlack_);
        FillRect(memDC_, &e2, brBlack_);

    } else if (gameMode == 1) {
        // Ship — triangle/arrow shape
        POINT pts[3] = {
            {sx + half, sy},
            {sx - half, sy - half},
            {sx - half, sy + half}
        };
        HGDIOBJ oldBrush = SelectObject(memDC_, brPlayer_);
        HGDIOBJ oldPen = SelectObject(memDC_, penPlayer_);
        Polygon(memDC_, pts, 3);
        SelectObject(memDC_, oldBrush);
        SelectObject(memDC_, oldPen);

    } else if (gameMode == 2) {
        // Ball — circle
        drawScreenCircle(sx, sy, half, brPlayer_, penPlayer_);

    } else if (gameMode == 3) {
        // UFO — dome shape (circle + flat bottom)
        drawScreenCircle(sx, sy - half/3, half, brPlayer_, penPlayer_);
        RECT bottom = {sx - half, sy + half/3, sx + half, sy + half/3 + 4};
        FillRect(memDC_, &bottom, brPlayerIcon_);

    } else if (gameMode == 4) {
        // Wave — diamond shape
        POINT pts[4] = {
            {sx, sy - half},
            {sx + half, sy},
            {sx, sy + half},
            {sx - half, sy}
        };
        HGDIOBJ oldBrush = SelectObject(memDC_, brPlayer_);
        HGDIOBJ oldPen = SelectObject(memDC_, penPlayer_);
        Polygon(memDC_, pts, 4);
        SelectObject(memDC_, oldBrush);
        SelectObject(memDC_, oldPen);

    } else if (gameMode == 7) {
        // Swing copter — inverted triangle
        POINT pts[3] = {
            {sx, sy + half},
            {sx - half, sy - half},
            {sx + half, sy - half}
        };
        HGDIOBJ oldBrush = SelectObject(memDC_, brPlayer_);
        HGDIOBJ oldPen = SelectObject(memDC_, penPlayer_);
        Polygon(memDC_, pts, 3);
        SelectObject(memDC_, oldBrush);
        SelectObject(memDC_, oldPen);
    }

    // Click indicator — green glow ring when holding
    if (currentState_.isHolding) {
        int glowR = half + 6;
        HGDIOBJ oldBrush = SelectObject(memDC_, (HBRUSH)GetStockObject(NULL_BRUSH));
        HPEN glowPen = CreatePen(PS_SOLID, 2, COL_CLICK_ON);
        HGDIOBJ oldPen = SelectObject(memDC_, glowPen);
        Ellipse(memDC_, sx - glowR, sy - glowR, sx + glowR, sy + glowR);
        SelectObject(memDC_, oldBrush);
        SelectObject(memDC_, oldPen);
        DeleteObject(glowPen);
    }
}

// ============================================================================
// Trail
// ============================================================================
void Renderer::drawTrail() {
    if (currentState_.trail.size() < 2) return;

    HGDIOBJ oldPen = SelectObject(memDC_, penTrail_);

    for (size_t i = 1; i < currentState_.trail.size(); i++) {
        int x1 = (int)worldToScreenX(currentState_.trail[i-1].first);
        int y1 = (int)worldToScreenY(currentState_.trail[i-1].second);
        int x2 = (int)worldToScreenX(currentState_.trail[i].first);
        int y2 = (int)worldToScreenY(currentState_.trail[i].second);
        MoveToEx(memDC_, x1, y1, nullptr);
        LineTo(memDC_, x2, y2);
    }

    SelectObject(memDC_, oldPen);
}

// ============================================================================
// HUD — GD-style progress bar + stats overlay
// ============================================================================
void Renderer::drawProgressBar() {
    // Progress bar at top center (like real GD)
    int barW = width_ / 2;
    int barH = 8;
    int barX = (width_ - barW) / 2;
    int barY = 12;

    // Background
    RECT bg = {barX, barY, barX + barW, barY + barH};
    FillRect(memDC_, &bg, brProgressBg_);

    // Fill
    int fillW = (int)(barW * currentState_.percent / 100.0f);
    if (fillW > 0) {
        RECT fill = {barX, barY, barX + fillW, barY + barH};
        FillRect(memDC_, &fill, brProgressFill_);
    }

    // Percent text centered above bar
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << currentState_.percent << "%";
    drawScreenText(barX + barW/2 - 20, barY + barH + 2, ss.str(), COL_HUD_TEXT, fontUI_);
}

void Renderer::drawHUD() {
    drawProgressBar();

    // Stats panel (bottom-left, semi-transparent black)
    int panelW = 300;
    int panelH = 120;
    int panelX = 8;
    int panelY = height_ - panelH - 8;

    // Semi-opaque panel
    fillScreenRect(panelX, panelY, panelW, panelH, brUiPanel_);

    int tx = panelX + 8;
    int ty = panelY + 6;
    int lineH = 18;

    std::stringstream ss;

    // Level name & episode
    ss << "Level: " << currentState_.levelName;
    drawScreenText(tx, ty, ss.str(), COL_HUD_TEXT, fontUI_); ty += lineH;

    ss.str(""); ss << "Episode: " << currentState_.episode << "  Step: " << currentState_.step;
    drawScreenText(tx, ty, ss.str(), COL_HUD_TEXT, fontSmall_); ty += lineH;

    ss.str(""); ss << "Best: " << std::fixed << std::setprecision(1) << currentState_.bestProgress
                    << "%  Avg: " << std::setprecision(1) << currentState_.avgProgress << "%";
    drawScreenText(tx, ty, ss.str(), COL_PROGRESS_FG, fontSmall_); ty += lineH;

    ss.str(""); ss << "Reward: " << std::fixed << std::setprecision(2) << currentState_.reward
                    << "  Value: " << std::setprecision(2) << currentState_.value;
    drawScreenText(tx, ty, ss.str(), RGB(200, 200, 255), fontSmall_); ty += lineH;

    // Game mode indicator
    const char* modeNames[] = {"CUBE", "SHIP", "BALL", "UFO", "WAVE", "ROBOT", "SPIDER", "SWING"};
    int gm = std::clamp(currentState_.gameMode, 0, 7);
    ss.str(""); ss << "Mode: " << modeNames[gm] << "  Speed: " << std::setprecision(0) << currentState_.playerSpeed;
    drawScreenText(tx, ty, ss.str(), RGB(180, 180, 200), fontSmall_); ty += lineH;

    ss.str(""); ss << "Levels Done: " << currentState_.levelsCompleted
                    << "  Entropy: " << std::setprecision(3) << currentState_.entropy;
    drawScreenText(tx, ty, ss.str(), RGB(150, 150, 180), fontSmall_);

    // Click indicator (top right)
    int ciW = 90, ciH = 30;
    int ciX = width_ - ciW - 10, ciY = 10;
    HBRUSH ciBrush = currentState_.isHolding ? brClick_ : brNoClick_;
    fillScreenRect(ciX, ciY, ciW, ciH, ciBrush);
    const char* ciText = currentState_.isHolding ? "CLICK" : "---";
    COLORREF ciColor = currentState_.isHolding ? RGB(0, 0, 0) : RGB(150, 150, 150);
    drawScreenText(ciX + 20, ciY + 7, ciText, ciColor, fontUI_);
}

void Renderer::drawDeathOverlay() {
    if (!currentState_.isDead) return;

    // Red tint flash across the screen (subtle)
    // Just draw a small "DEAD" indicator
    int w = 120, h = 36;
    int x = (width_ - w) / 2;
    int y = (int)(height_ * 0.4f);
    fillScreenRect(x, y, w, h, brDead_);
    drawScreenText(x + 30, y + 8, "DEAD", RGB(255, 255, 255), fontBig_);
}

// ============================================================================
// Main Render Function
// ============================================================================
void Renderer::renderLevel(const LevelData& level, const RenderState& state) {
    currentState_ = state;

    // Camera follows player (player at ~25% from left)
    currentState_.cameraX = state.playerX - 250.0f;
    if (currentState_.cameraX < 0) currentState_.cameraX = 0;

    // Trail management
    if (state.step % 2 == 0) {
        currentState_.trail.push_back({state.playerX, state.playerY});
        if (currentState_.trail.size() > 120) {
            currentState_.trail.erase(currentState_.trail.begin());
        }
    }

    // 1. Clear
    clear();

    // 2. Background gradient
    drawBackground();

    // 3. Background grid lines
    drawGridLines();

    // 4. Ground (checkerboard + top line)
    drawGround();

    // 5. Level objects (frustum culled)
    for (const auto& obj : level.objects) {
        float screenX = worldToScreenX(obj.x);
        if (screenX < -80 || screenX > width_ + 80) continue;

        switch (obj.type) {
            case ObjectType::BLOCK:       drawBlock(obj); break;
            case ObjectType::SPIKE:       drawSpike(obj); break;
            case ObjectType::ORB:         drawOrb(obj);   break;
            case ObjectType::PAD:         drawPad(obj);   break;
            case ObjectType::PORTAL_GRAVITY:
            case ObjectType::PORTAL_SHIP:
            case ObjectType::PORTAL_BALL:
            case ObjectType::PORTAL_UFO:
            case ObjectType::PORTAL_WAVE:
            case ObjectType::PORTAL_ROBOT:
            case ObjectType::PORTAL_SPIDER:
            case ObjectType::PORTAL_CUBE:
            case ObjectType::PORTAL_SWING:
            case ObjectType::PORTAL_SPEED: drawPortal(obj); break;
            default: break;
        }
    }

    // 6. Trail
    drawTrail();

    // 7. Player
    drawPlayer();

    // 8. HUD overlay
    drawHUD();

    // 9. Death overlay
    drawDeathOverlay();

    // 10. Present to screen
    present();
}
