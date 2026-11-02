// customWindow.h -- THE ONE-FILE WINDOW (v7.5.3)
// =====================================================================
// A single-header custom window-chrome API for Windows 10/11: a borderless
// window that keeps the REAL frame machinery (DWM shadow on WS_THICKFRAME,
// real NC strips via WM_NCCALCSIZE insets, HTCAPTION drag/snap/Alt+Space,
// caption buttons, the caption right-click system menu) while the app paints
// the caption itself. Extracted verbatim from the twinspeak panel -- the
// v7.1.24 adoption of Microsoft's Win32 custom title bar sample (main.c),
// tuned in the field from v7.1.17 through v7.4.11 (ghost shadows, the sheet
// row, the RAD activation contract, the taskbar button birth).
//
// The ELEVEN public functions are the whole API:
//
//     enableCustomWin(bool enable)
//         Turn the custom chrome on or off. On: the window must have been
//         created with cwFrameStyle() (or at least WITHOUT WS_CAPTION) and
//         the host wndproc must forward messages through cwWndProc() first.
//         Off (runtime): the header adds WS_CAPTION back and answers nothing
//         -- the vanilla frame returns. Default: on.
//
//     changeWinTitlebarColor(int col) / (int col, int gradColor)
//         The caption bar's background. One argument: solid. Two: a vertical
//         top->bottom gradient col -> gradColor. Colors are ARGB ints
//         (0xAARRGGBB). Default: dark slate (the twinspeak seed).
//
//     setWinTitleTextColor(int color, int gradientCol,
//                          int inactiveColor = color,
//                          int gradientInactiveCol = gradientCol)
//         The title text, BOTH states in one call. Focused: color ->
//         gradientCol (a vertical top->bottom gradient; pass the same color
//         twice for solid). Inactive: inactiveColor ->
//         gradientInactiveCol, which default to the focused pair -- the
//         text only dims with focus when told to. Colors are ARGB ints
//         (0xAARRGGBB). Default: the cyan-gray gradient.
//
//     changeWinTextFont(const std::string& font = "Segoe UI")
//         The title text face. Accepts a font FAMILY name ("Segoe UI") or a
//         font file from C:/Windows/Fonts ("segoeuib.ttf", or a full path);
//         files load privately (AddFontResourceEx FR_PRIVATE, this process
//         only) and render under their own embedded family name. The size
//         and weight come from the seed (cwSeedTitleFont, default 14 px
//         semibold). Default: Segoe UI.
//
//     enableWinIcon(bool enable)
//         Draw the app icon at the left of the caption bar. The icon IS the
//         window class icon -- the same one the vanilla titlebar, the
//         taskbar and Alt-Tab show (GCLP_HICONSM / GCLP_HICON). Default: off.
//
//     setWinTitlebarButtonLayout(bool minimize, bool maximize)
//         Which caption buttons exist besides CLOSE (close is always kept,
//         always rightmost; maximize sits between minimize and close; with
//         maximize off, minimize sits directly beside close -- the twinspeak
//         layout). Buttons are 47 design-px wide each, the sample's widths.
//         Default: minimize on, maximize off.
//
//     setCanWinResize(bool resize)
//         Whether the window accepts resize/maximize COMMANDS. With resize
//         on, WS_THICKFRAME (which the DWM shadow needs) resizes. With
//         resize off (v7.5.1 THE HONEST BORDER) the header refuses
//         SC_SIZE / SC_MAXIMIZE, the maximize button dispatches nowhere,
//         AND the hit-test stops advertising: every edge band answers
//         HTBORDER instead of the sizing codes, so the cursor stays an
//         arrow and the mouse registers a fixed window as fixed -- a drag
//         never starts, where it used to "end where it started". Default:
//         false.
//
//     The button hover (v7.5.3d): no longer a color knob. The minimize and
//     maximize buttons show a 10% black tint (measured off the real thing)
//     that fades in over 150 ms and out over 300 ms, LINEAR, painted over
//     everything in the button, one design px short of the bar (the border
//     row stays untinted) and with 0.5 px seams between neighbouring
//     buttons. The close button fades its vanilla red the same way and the
//     X whitens with it. changeWinMinimizeHoverColor /
//     changeWinMaximizeHoverColor are retired.
//
//     putWindowinTray(bool hide)
//         hide=true: hide the window to the tray NOW, SILENTLY -- the
//         gesture is the hide itself (position capture is the host's, the
//         icon must already exist or the fallback below adds one) and it
//         NEVER notifies: no balloon, no toast. Any "the window went to
//         the tray" message belongs to the host's own close gesture, in
//         host code (twinspeak's balloon lives in its WM_CLOSE, not here).
//         With a tray host registered (cwSetTrayHost) the host owns the
//         whole gesture; without one, the header's built-in fallback tray
//         icon (the class icon, tooltip = window title, left-click
//         restores) is added first so the window is always reachable
//         again. hide=false: bring the window back (host path, or show +
//         foreground).
//
// Host plumbing (a window that already runs its own machinery -- twinspeak --
// adopts the chrome without behavior change through these; a fresh consumer
// needs NONE of them):
//     cwFrameStyle()               the window style the chrome expects
//     cwIsMaximized(HWND)          the sample's maximized test (WPL)
//     cwAttach(HWND, void(*)(HWND))   the chrome window + the bar repaint fn
//     cwSetBarHeight(int)          the caption bar height in REAL pixels
//     cwBarHeight()                v7.5.3d: the header's own caption height
//                                  (33 design px, dpi-scaled) -- the number
//                                  a host adopts as its caption contract;
//                                  cwSetBarHeight stays for hosts that keep
//                                  their own
//     cwTrackChild / cwRelayoutChildren / cwSetDpiHook
//                                  v7.5.3d: the WM_DPICHANGED story. The
//                                  header re-derives itself (dpi, bar height,
//                                  title font), applies the suggested rect,
//                                  calls the host's hook (constants + fonts),
//                                  then re-seats the tracked children.
//     cwSeedTitleFont(face, px, weight)   size/weight behind changeWinTextFont
//     cwSeedChrome(...)            per-theme seed of every chrome color
//     cwSetTrayHost(hideFn, showFn)   putWindowinTray delegates to these
//     cwPaintBarBackground(g, ww, active) / cwPaintBarForeground(g, w, ww, active)
//     cwPaintChrome(g, w, ww, active)     both, one call (fresh consumers)
//     cwWndProc(w, msg, wp, lp, &result)  FORWARD FIRST in the host wndproc
//
// The fake top border bar (the sheet row -- the window's REAL top border,
// row 0 of the caption bar) is the header's too, since v7.5.3:
//     cwSeedSheetRow(active, inactive)      the row bytes, BGRA u32 (defaults
//                                           = the field's half-alpha gray)
//     cwPatchSheetRow(bits, cw, active)     row 0 of the host's 32bpp buffer
//     cwCommitSheetRowFast(w, active)       the row-first 1 px commit
//                                           (private DIB + GdiFlush)
//     cwSheetRowGate(bool)                  presenter stand-down gate
//     cwSheetRowKick()                      restart the fade train
//     cwSheetRowLatch(int) / cwSheetRowLatchValue()   the NC activation
//                                           latch (-1/0/1, the frame's truth)
//     cwSheetPresenterStart(w) / cwSheetPresenterStop()   the fade train
//
//     (v7.5.3b: the sheet itself is the header's too -- cwAttach runs
//      cwSheetSetup, the {0,0,1,0} extend + the immersive dark mode
//      border, and the WM_SYSCOMMAND(SC_MINIMIZE) ordered present lives
//      in cwWndProc. A fresh consumer needs NO host DWM code at all.)
//
// Host contract (twinspeak already meets all of it):
//   * GDI+ started (GdiplusStartup) before the first paint.
//   * The window class carries the icon (hIcon/hIconSm) for enableWinIcon
//     and the built-in tray.
//   * The wndproc calls cwWndProc FIRST; unhandled messages keep flowing to
//     the host's own cases and DefWindowProc.
//   * The chrome draws face + icon + title + buttons; the host may draw its
//     own border/edge rows around those calls (twinspeak sandwiches them:
//     background, its glass-sheet row fill, its 1px bottom edge, foreground).
//     The BYTE-DIRECT sheet row (the fake top border) is the header's now:
//     the host patches its buffer with cwPatchSheetRow inside the bar paint
//     and orders the flip frames with cwCommitSheetRowFast.
//   * The repaint callback is invoked on every state change (hover, theme,
//     layout); it should repaint the bar synchronously.
//
// Targets: Windows (the implementation). Every other target compiles the
// stub block at the bottom -- the eleven functions exist and do nothing, so
// host code writes unchanged on both sides of #ifdef _WIN32.
// Libraries (Windows): user32 gdi32 shell32 gdiplus (-lole32 if COM is used
// by the host itself).
// =====================================================================

#ifndef CUSTOMWINDOW_H_INCLUDED
#define CUSTOMWINDOW_H_INCLUDED

#include <string>

#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <gdiplus.h>
#include <atomic>
#include <vector>

#ifndef GET_X_PARAM
#define GET_X_PARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_PARAM
#define GET_Y_PARAM(lp) ((int)(short)HIWORD(lp))
#endif

// main.c verbatim: the 1px top row the sample paints its fake shadow into --
// in the sheet recipe it is the window's REAL top border (row 0). The button
// rects start one row down so the border row is never covered.
#ifndef WIN32_FAKE_SHADOW_HEIGHT
#define WIN32_FAKE_SHADOW_HEIGHT 1
#endif

// the tray hook in cwWndProc dispatches through the public API -- declared
// here so the namespace below can reach it before its definition lands
inline void putWindowinTray(bool hide);

namespace cwd {

// ---- the chrome state (ARGB ints, 0xAARRGGBB) ------------------------------
inline bool g_enabled     = true;
inline int  g_faceCol     = 0xFF181B1F;   // titlebar, focused (solid or gradient top)
inline int  g_faceGrad    = 0xFF090A0C;   // titlebar gradient bottom
inline int  g_faceColIn   = -1;           // titlebar, unfocused (-1 = mirror focused)
inline int  g_faceGradIn  = -1;
inline int  g_textCol     = 0xFFA0C8CD;   // title text, focused (gradient top)
inline int  g_textGrad    = 0xFF6C868C;   // title text gradient bottom
inline int  g_textUn      = 0xFF7E888E;   // title text, unfocused (solid...
inline int  g_textUnGrad  = -1;           // ...or gradient bottom when >= 0)
inline int  g_glyph       = -1;           // button glyphs, focused (-1 = g_textCol)
// v7.5.3d: the hover is no longer a solid face color. The min/max hover is
// a black TINT -- 10% alpha (0x1A = 26, measured off a real Windows 10
// caption button) -- that fades in over 150 ms and out over 300 ms, LINEAR,
// painted OVER everything in the button (the glyph included), one design px
// short of the bar's full height (the DWM border row stays untinted) and
// with 0.5 px seams between neighbouring buttons (0.25 px off each inner
// edge, anti-aliased so the seam renders as the hairline the real thing
// shows). The close button fades its vanilla red the same way; the X lerps
// to white with the fill.
inline constexpr int kHovTintAlpha = 26;    // 10% of 255 (0x1A), the measured value
inline constexpr int kHovFadeInMs  = 150;   // pre-hover ramp
inline constexpr int kHovFadeOutMs = 300;   // post-hover ramp
inline constexpr UINT kHovTimerId  = 0x43485631u;  // the frame's own animation timer
inline int   g_hovA[3] = { 0, 0, 0 };       // min, max, close: current alpha 0..255
inline DWORD g_hovLast = 0;                 // the last tick's GetTickCount
inline bool  g_hovTimerOn = false;          // the 16 ms animation timer is running
inline const DWORD kCloseHover = 0xFFCC0000;   // vanilla red, not configurable
inline std::wstring g_fontFace = L"Segoe UI";
inline int  g_fontPx      = 14;
inline int  g_fontWeight  = FW_SEMIBOLD;
inline bool g_icon        = false;
inline bool g_btnMin      = true;
inline bool g_btnMax      = false;
inline bool g_resizable   = false;

// ---- host plumbing state ---------------------------------------------------
inline HWND   g_host      = nullptr;      // the chrome window
// v7.5.3d: the bar height is the header's own truth now -- 33 design px
// (the real DWM caption height; the old 31 was two px short), scaled by the
// header's DPI and re-derived on WM_DPICHANGED. A host adopts it through
// cwBarHeight() (twinspeak) or keeps seeding cwSetBarHeight() (others).
inline constexpr int kBarHDesign = 33;
inline int    g_barH      = 33;           // caption bar height, REAL px
inline void (*g_repaint)(HWND) = nullptr;  // the host's synchronous bar repaint
inline void (*g_trayHide)(HWND) = nullptr; // putWindowinTray(true)  -> host path
inline void (*g_trayShow)(HWND) = nullptr; // putWindowinTray(false) -> host path
// v7.5.3d: the DPI hook -- the one thing the header cannot do for a host is
// re-derive ITS constants and fonts. On WM_DPICHANGED the header updates
// itself (dpi, bar height, title font) and calls this AFTER the suggested
// rect has been applied; the host re-scales and then cwRelayoutChildren()
// moves the tracked children. Registered once, next to cwAttach.
inline void (*g_dpiHook)() = nullptr;

// hover: 0 none, 1 minimize, 2 maximize, 3 close (replaces GWLP_USERDATA)
enum { cwHovNone = 0, cwHovMin = 1, cwHovMax = 2, cwHovClose = 3 };
inline int g_hover = cwHovNone;

// the built-in fallback tray (only when no tray host is registered)
inline NOTIFYICONDATAW g_nid   = {};
inline bool g_trayUp           = false;
inline UINT g_trayMsg          = 0;          // WM_APP+n, registered on first use
inline UINT g_taskbarCreated   = 0;

// ---- the title font (family name or C:\Windows\Fonts file) -----------------
inline Gdiplus::Font* g_font = nullptr;
inline std::wstring g_fontKey;             // face|px|weight|dpi of the cached font
inline std::wstring g_fontFamily = L"Segoe UI";   // the family the LOGFONT uses

// ---- small helpers ---------------------------------------------------------
inline UINT g_dpi = 0;                    // 0 = seed on the first cwDpi()
inline UINT cwDpi() {
    if (!g_dpi) {
        HDC dc = GetDC(nullptr);
        int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc) ReleaseDC(nullptr, dc);
        g_dpi = (d > 0) ? (UINT)d : 96;
    }
    return g_dpi;
}
// v7.5.3d: WM_DPICHANGED -- the scale moved under the window. The header's
// whole geometry derives from this (cwS/cwSf, the bar height, the title
// font's pixel height), so one store re-derives everything lazily; the
// title font's cache key carries the DPI and rebuilds on the next paint.
inline void cwDpiChanged(UINT dpi) {
    if (dpi && dpi != g_dpi) { g_dpi = dpi; g_fontKey.clear(); }
}
// system metrics at the window's DPI (per-monitor-v2 correct; the plain
// GetSystemMetrics answers the primary's sign-in scale). Loaded loose so
// pre-1607 Windows keeps working.
inline int cwMetric(int idx) {
    typedef int (WINAPI *CwMetricForDpiFn)(int, UINT);
    static CwMetricForDpiFn fn = []() -> CwMetricForDpiFn {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        return u ? (CwMetricForDpiFn)(void*)GetProcAddress(u, "GetSystemMetricsForDpi") : nullptr;
    }();
    if (fn) { const int v = fn(idx, cwDpi()); if (v) return v; }
    return GetSystemMetrics(idx);
}
inline int cwS(int v) { return MulDiv(v, (int)cwd::cwDpi(), 96); }        // design px -> real px (twinspeak's S())
inline float cwSf(float v) { return v * (float)cwd::cwDpi() / 96.0f; }
inline DWORD cwArgbOf(COLORREF c) {
    return 0xFF000000u | ((DWORD)GetRValue(c) << 16)
                       | ((DWORD)GetGValue(c) << 8) | (DWORD)GetBValue(c);
}
inline int cwChannel(int argb, int shift) { return (argb >> shift) & 0xFF; }
inline bool cwIsMaximized(HWND w) {          // main.c verbatim
    WINDOWPLACEMENT placement = {0};
    placement.length = sizeof(WINDOWPLACEMENT);
    return GetWindowPlacement(w, &placement)
        && placement.showCmd == SW_SHOWMAXIMIZED;
}
inline void cwRepaint() {
    if (g_repaint && g_host) { g_repaint(g_host); return; }
    if (g_host) {
        RECT r; if (GetClientRect(g_host, &r)) { r.bottom = g_barH; InvalidateRect(g_host, &r, FALSE); }
    }
}

// ---- v7.5.3d: the hover fade train ------------------------------------------
// Linear 150/300 ms ramps per button (min, max, close), driven by a private
// 16 ms WM_TIMER on the host window; every tick repaints the bar through the
// host's synchronous painter, which reads g_hovA. The timer lives only while
// a ramp is in flight, then it is gone again.
inline void cwHovAnimEnsure() {
    if (g_hovTimerOn || !g_host || !IsWindow(g_host)) return;
    g_hovLast = GetTickCount();
    if (SetTimer(g_host, kHovTimerId, 16, nullptr)) g_hovTimerOn = true;
}
inline void cwHovAnimTick() {
    if (!g_hovTimerOn) return;
    const HWND w = g_host;
    if (!w || !IsWindow(w) || IsIconic(w) || !IsWindowVisible(w)) {
        g_hovA[0] = g_hovA[1] = g_hovA[2] = 0;      // nothing visible: rest
        KillTimer(w, kHovTimerId); g_hovTimerOn = false;
        return;
    }
    const DWORD now = GetTickCount();
    DWORD dt = now - g_hovLast;
    g_hovLast = now;
    if (dt > 1000) dt = 1000;                       // a stalled queue must not jump the ramp
    bool busy = false;
    for (int i = 0; i < 3; ++i) {
        const bool held = (g_hover == i + 1);
        int a = g_hovA[i];
        if (held) a += (int)(dt * 255 / kHovFadeInMs);
        else      a -= (int)(dt * 255 / kHovFadeOutMs);
        if (a > 255) a = 255;
        if (a < 0) a = 0;
        if (a != g_hovA[i]) { g_hovA[i] = a; busy = true; }
        if ((held && a < 255) || (!held && a > 0)) busy = true;
    }
    cwRepaint();                                    // the painter reads g_hovA
    if (!busy) { KillTimer(w, kHovTimerId); g_hovTimerOn = false; }
}

// ---- v7.5.3d: the DPI-responsive child ledger --------------------------------
// Children that ride below the bar (design px in, real px out) register here;
// on WM_DPICHANGED the header re-positions every tracked child at the new
// scale, re-seats its font (a pointer into the host's HFONT slot -- the host
// re-creates the font in its dpi hook) and runs its per-child fixup
// (combobox item heights).
struct CwTrackedChild {
    HWND    h;
    int     x, y, w, ht;        // design px
    bool    underBar;           // the y rides the header's bar height
    HFONT*  font;               // the host's font slot, re-sent after a move
    int     comboItemH;         // > 0: CB_SETITEMHEIGHT(-1) after the move
};
inline std::vector<CwTrackedChild> g_kids;
inline void cwTrackChild(HWND h, int x, int y, int w, int ht,
                         bool underBar = true, HFONT* font = nullptr,
                         int comboItemH = 0) {
    for (auto& k : g_kids)
        if (k.h == h) { k.x = x; k.y = y; k.w = w; k.ht = ht;
                        k.underBar = underBar; k.font = font;
                        k.comboItemH = comboItemH; return; }
    g_kids.push_back({ h, x, y, w, ht, underBar, font, comboItemH });
}
inline void cwRelayoutChildren() {
    for (auto& k : g_kids) {
        if (!k.h || !IsWindow(k.h)) continue;
        SetWindowPos(k.h, nullptr,
                     cwS(k.x), cwS(k.y) + (k.underBar ? g_barH : 0),
                     cwS(k.w), cwS(k.ht),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (k.font && *k.font) SendMessageW(k.h, WM_SETFONT, (WPARAM)*k.font, TRUE);
        if (k.comboItemH > 0) {
            SendMessageW(k.h, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)cwS(k.comboItemH));
            InvalidateRect(k.h, nullptr, FALSE);
        }
    }
}
inline void cwSetDpiHook(void (*hook)()) { g_dpiHook = hook; }

// ---- the button rects (main.c's win32_get_title_bar_button_rects) ----------
// Client coordinates over a bar 0..g_barH tall, buttons 47 design-px wide,
// stacked from the right edge: close always; then maximize (if present);
// then minimize. With maximize off, minimize sits beside close -- the
// twinspeak geometry, byte for byte.
struct CwButtonRects { RECT close, maximize, minimize; };
inline CwButtonRects cwButtonRects(int ww) {
    const int bw = cwS(47);
    RECT close = { 0, 0, ww, g_barH };
    close.top += WIN32_FAKE_SHADOW_HEIGHT;
    close.left = close.right - bw;
    RECT maximize = close;
    maximize.left  -= bw;
    maximize.right -= bw;
    RECT minimize = maximize;
    minimize.left  -= bw;
    minimize.right -= bw;
    if (!g_btnMax) minimize = maximize;   // the empty slot collapses
    return { close, maximize, minimize };
}

inline bool cwFileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Accepts a family name or a font file name/path. Files load FR_PRIVATE and
// render under their embedded family (read back through a private
// collection). Returns false only when nothing could be resolved, in which
// case the previous family stays.
inline bool cwResolveFont(const std::wstring& in) {
    const bool isPath = in.find(L'\\') != std::wstring::npos
                     || in.find(L'/')  != std::wstring::npos
                     || in.find(L'.')  != std::wstring::npos;
    if (!isPath) { g_fontFamily = in; return true; }
    std::wstring path = in;
    if (path.find(L':') == std::wstring::npos && path[0] != L'\\')
        path = L"C:\\Windows\\Fonts\\" + path;
    if (!cwFileExists(path)) return false;
    if (!AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr)) return false;
    Gdiplus::PrivateFontCollection coll;
    if (coll.AddFontFile(path.c_str()) != Gdiplus::Ok) return false;
    INT found = 0;
    Gdiplus::FontFamily fam;
    if (coll.GetFamilies(1, &fam, &found) != Gdiplus::Ok || found < 1) return false;
    WCHAR name[LF_FACESIZE] = L"";
    if (fam.GetFamilyName(name, LF_FACESIZE) != Gdiplus::Ok) return false;
    g_fontFamily = name;
    return true;
}

inline Gdiplus::Font* cwFont() {
    const std::wstring key = g_fontFamily + L"|" + std::to_wstring(g_fontPx)
                           + L"|" + std::to_wstring(g_fontWeight)
                           + L"|" + std::to_wstring(cwDpi());   // v7.5.3d: the dpi rides the key
    if (g_font && key == g_fontKey) return g_font;
    delete g_font; g_font = nullptr;
    LOGFONTW lf = {0};
    lf.lfHeight         = -cwS(g_fontPx);
    lf.lfWeight         = g_fontWeight;
    lf.lfCharSet        = DEFAULT_CHARSET;
    lf.lfQuality        = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    lstrcpynW(lf.lfFaceName, g_fontFamily.c_str(), LF_FACESIZE);
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc) {
        g_font = new Gdiplus::Font(dc, &lf);
        DeleteDC(dc);
        if (!g_font || g_font->GetLastStatus() != Gdiplus::Ok) {
            delete g_font; g_font = nullptr;
        }
    }
    if (g_font) g_fontKey = key;
    return g_font;
}

// ---- the painters ----------------------------------------------------------
// BACKGROUND: the caption face. The fill and brush rects are the twinspeak
// render verbatim (offset 1px down -- row 0 is the border row; the brush
// rides the same offset) so a host that sandwiches its own border rows
// around these calls keeps its exact bytes. Equal col/grad collapses to a
// solid (a linear gradient between equal colors is exact).
inline void cwPaintBarBackground(Gdiplus::Graphics& g, int ww, bool active) {
    if (!g_enabled) return;
    const int face = active ? g_faceCol : (g_faceColIn < 0 ? g_faceCol : g_faceColIn);
    const int grad = active ? g_faceGrad : (g_faceGradIn < 0 ? g_faceGrad : g_faceGradIn);
    Gdiplus::LinearGradientBrush brush(
        Gdiplus::Rect(0, 1, ww + 1, g_barH),
        Gdiplus::Color(cwChannel(face, 24), cwChannel(face, 16),
                       cwChannel(face, 8), cwChannel(face, 0)),
        Gdiplus::Color(cwChannel(grad, 24), cwChannel(grad, 16),
                       cwChannel(grad, 8), cwChannel(grad, 0)),
        Gdiplus::LinearGradientModeVertical);
    g.FillRectangle(&brush, -1, 1, ww + 2, g_barH);
}

// FOREGROUND: icon, title text, caption buttons -- in that z-order (the
// buttons paint OVER the host's edge rows, exactly like the twinspeak
// render). Hover faces + glyphs; the close hover stays vanilla red.
inline void cwPaintBarForeground(Gdiplus::Graphics& g, HWND w, int ww, bool active) {
    if (!g_enabled) return;
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    const CwButtonRects br = cwButtonRects(ww);
    const int buttonW = br.close.right - br.close.left;
    const int item = active ? (g_glyph < 0 ? g_textCol : g_glyph) : g_textUn;
    auto fillARGB = [&g](int col, int x, int y, int w2, int h2) {
        Gdiplus::SolidBrush b(Gdiplus::Color(cwChannel(col, 24), cwChannel(col, 16),
                                             cwChannel(col, 8), cwChannel(col, 0)));
        g.FillRectangle(&b, x, y, w2, h2);
    };
    // v7.5.3d: the hover patch -- a translucent fill with sub-pixel control.
    // The 0.5 px seams between neighbouring buttons are 0.25 px insets on the
    // inner edges, anti-aliased so the seam renders as the hairline the real
    // DWM shows; the outer edges stay flush (close rides the window edge).
    auto fillPatch = [&](const RECT& r, int col, float insetL, float insetR) {
        Gdiplus::SolidBrush b(Gdiplus::Color(cwChannel(col, 24), cwChannel(col, 16),
                                             cwChannel(col, 8), cwChannel(col, 0)));
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.FillRectangle(&b, (Gdiplus::REAL)r.left + insetL, (Gdiplus::REAL)r.top,
                        (Gdiplus::REAL)(r.right - r.left) - insetL - insetR,
                        (Gdiplus::REAL)(r.bottom - r.top));
        g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    };
    // minimize
    if (g_btnMin) {
        const int lineW = cwS(10);
        const int cx = br.minimize.left + buttonW / 2;
        const int cy = br.minimize.top + (br.minimize.bottom - br.minimize.top) / 2;
        fillARGB(item, cx - lineW / 2, cy, lineW, 1);
    }
    // maximize (square outline; double square while maximized)
    if (g_btnMax) {
        const int cx = br.maximize.left + buttonW / 2;
        const int cy = br.maximize.top + (br.maximize.bottom - br.maximize.top) / 2;
        const float penW = cwSf(1.25f) < 1.0f ? 1.0f : cwSf(1.25f);
        Gdiplus::Pen p(Gdiplus::Color(cwChannel(item, 24), cwChannel(item, 16),
                                      cwChannel(item, 8), cwChannel(item, 0)), penW);
        if (w && cwIsMaximized(w)) {
            // v7.5.3d: the front sheet OCCLUDES the back one -- the real
            // restore glyph layers the squares, the back square's edges stop
            // at the front square instead of showing through its interior
            // (the maximized button used to read as see-through wireframe).
            Gdiplus::Rect front(cx - cwS(5), cy - cwS(3), cwS(8), cwS(8));
            Gdiplus::Region frontRegion(front);
            g.SetClip(&frontRegion, Gdiplus::CombineModeExclude);
            g.DrawRectangle(&p, cx - cwS(3), cy - cwS(5), cwS(8), cwS(8));   // the back sheet, occluded
            g.ResetClip();
            g.DrawRectangle(&p, cx - cwS(5), cy - cwS(3), cwS(8), cwS(8));   // the front sheet, full
        } else {
            g.DrawRectangle(&p, cx - cwS(4), cy - cwS(4), cwS(9), cwS(9));
        }
    }
    // close (always; vanilla red hover, white X -- both ride the same linear
    // 150/300 ms fade; the X stroke is the real thing's 1 design px, the old
    // 1.25 rendered half again too thick)
    {
        const int pa = g_hovA[2];
        if (pa > 0) fillPatch(br.close, (pa << 24) | (int)(kCloseHover & 0xFFFFFF), 0.25f, 0.0f);
        const int cx = br.close.left + buttonW / 2;
        const int cy = br.close.top + (br.close.bottom - br.close.top) / 2;
        int xc = item;
        if (pa > 0) {                       // the X whitens with the red
            const int r  = cwChannel(item, 16) + ((255 - cwChannel(item, 16)) * pa + 127) / 255;
            const int gr = cwChannel(item,  8) + ((255 - cwChannel(item,  8)) * pa + 127) / 255;
            const int b  = cwChannel(item,  0) + ((255 - cwChannel(item,  0)) * pa + 127) / 255;
            xc = (int)(0xFF000000u | (unsigned)(r << 16) | (unsigned)(gr << 8) | (unsigned)b);
        }
        Gdiplus::Pen xp(Gdiplus::Color(cwChannel(xc, 24), cwChannel(xc, 16),
                                       cwChannel(xc, 8), cwChannel(xc, 0)),
                        cwSf(1.0f) < 1.0f ? 1.0f : cwSf(1.0f));
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.DrawLine(&xp, cx - cwS(5), cy - cwS(5), cx + cwS(5), cy + cwS(5));
        g.DrawLine(&xp, cx + cwS(5), cy - cwS(5), cx - cwS(5), cy + cwS(5));
        g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    }
    // the icon: the window class icon -- the same one the taskbar shows
    if (g_icon && w) {
        HICON hi = (HICON)GetClassLongPtrW(w, GCLP_HICONSM);
        if (!hi) hi = (HICON)GetClassLongPtrW(w, GCLP_HICON);
        if (hi) {
            Gdiplus::Bitmap* bm = Gdiplus::Bitmap::FromHICON(hi);
            if (bm) {
                const int isz = cwS(16);
                g.DrawImage(bm, Gdiplus::Rect(cwS(8), (g_barH - isz) / 2, isz, isz),
                            0, 0, bm->GetWidth(), bm->GetHeight(), Gdiplus::UnitPixel);
                delete bm;
            }
        }
    }
    // the title text: the WINDOW TITLE (what the vanilla bar would draw)
    if (w) {
        WCHAR title[256] = L"";
        GetWindowTextW(w, title, 256);
        if (title[0]) {
            Gdiplus::Font* tf = cwFont();
            if (tf) {
                const int tx = g_icon ? cwS(8) + cwS(16) + cwS(6) : cwS(12);
                int bw2 = 1 + (g_btnMax ? 1 : 0) + (g_btnMin ? 1 : 0);
                const int tw = ww - tx - bw2 * (br.close.right - br.close.left) - cwS(8);
                if (tw > 0) {
                    const Gdiplus::RectF tr((Gdiplus::REAL)cwSf((float)tx), cwSf(3.0f),
                                            (Gdiplus::REAL)tw,
                                            (Gdiplus::REAL)(g_barH - cwS(7)));
                    Gdiplus::StringFormat sf;
                    sf.SetAlignment(Gdiplus::StringAlignmentNear);
                    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                    if (active) {
                        Gdiplus::LinearGradientBrush gb(
                            tr, Gdiplus::Color(cwChannel(g_textCol, 24), cwChannel(g_textCol, 16),
                                               cwChannel(g_textCol, 8), cwChannel(g_textCol, 0)),
                                Gdiplus::Color(cwChannel(g_textGrad, 24), cwChannel(g_textGrad, 16),
                                               cwChannel(g_textGrad, 8), cwChannel(g_textGrad, 0)),
                            Gdiplus::LinearGradientModeVertical);
                        g.DrawString(title, -1, tf, tr, &sf, &gb);
                    } else if (g_textUnGrad < 0) {
                        Gdiplus::SolidBrush nb(Gdiplus::Color(
                            cwChannel(g_textUn, 24), cwChannel(g_textUn, 16),
                            cwChannel(g_textUn, 8), cwChannel(g_textUn, 0)));
                        g.DrawString(title, -1, tf, tr, &sf, &nb);
                    } else {
                        Gdiplus::LinearGradientBrush gb(
                            tr, Gdiplus::Color(cwChannel(g_textUn, 24), cwChannel(g_textUn, 16),
                                               cwChannel(g_textUn, 8), cwChannel(g_textUn, 0)),
                                Gdiplus::Color(cwChannel(g_textUnGrad, 24), cwChannel(g_textUnGrad, 16),
                                               cwChannel(g_textUnGrad, 8), cwChannel(g_textUnGrad, 0)),
                            Gdiplus::LinearGradientModeVertical);
                        g.DrawString(title, -1, tf, tr, &sf, &gb);
                    }
                }
            }
        }
    }
    // v7.5.3d: the min/max hover tints land LAST -- they overlay everything
    // in the button (the glyph included), one design px short of the bar's
    // full height so the DWM border row stays untinted, with the 0.5 px
    // seams against their neighbours (the outer edges stay flush).
    {
        const int aMin = (g_hovA[0] * kHovTintAlpha + 127) / 255;
        const int aMax = (g_hovA[1] * kHovTintAlpha + 127) / 255;
        if (g_btnMin && aMin > 0) fillPatch(br.minimize, aMin << 24, 0.0f, 0.25f);
        if (g_btnMax && aMax > 0) fillPatch(br.maximize, aMax << 24, 0.25f, 0.25f);
    }
}

// one-call chrome for fresh consumers (twinspeak sandwiches its own border
// rows between the two halves, so it calls them separately)
inline void cwPaintChrome(Gdiplus::Graphics& g, HWND w, int ww, bool active) {
    cwPaintBarBackground(g, ww, active);
    cwPaintBarForeground(g, w, ww, active);
}

// ---- THE FAKE TOP BORDER BAR (the sheet row; the header's since v7.5.3) -----
// Row 0 of the caption bar is the window's REAL top border. The host extends
// the DWM glass sheet exactly 1 px into the client ({0,0,1,0}), and this row
// is written BYTE-DIRECT into a 32bpp top-down DIB -- GDI+ on an HDC-backed
// surface cannot be trusted with the alpha byte, and the sheet recipe lives
// or dies on that byte (the recipe: "the colors needed to be pre-multiplied").
// The DWM composites the row with the sheet BY THE ALPHA, in the same compose
// that blends the left/right/bottom NC strips, so the fake row tracks the
// real frame exactly.
//
// THE BYTES (BGRA little-endian u32; the high byte is the alpha):
//   v7.1.27..v7.5.2  active    BGRA(0,0,0,0)       the glass key: the DWM's
//                                                   own border showed through.
//   v7.4.3..         inactive  BGRA(43,43,43,0x80) half-alpha RGB(43,43,43):
//                                                   the recipe's own source
//                                                   gray (85 at 50%), the
//                                                   field's byte -- "43, 43,
//                                                   43 and that's just it."
//   v7.5.3           active    BGRA(43,43,43,0x80) THE FIELD'S PICK: the
//                                                   active row joins the
//                                                   inactive byte -- the
//                                                   field: "Gdiplus::Color
//                                                   (128, 43, 43, 43) would
//                                                   do it", since the DWM's
//                                                   border color (the registry
//                                                   colorization) owns the
//                                                   frame tint anyway and a
//                                                   bare glass key delivered
//                                                   nothing the registry
//                                                   border did not.
// With no alpha-0 row anywhere, the transparent-glass flash the field taped
// on minimize (v7.5.1/v7.5.2) is impossible BY CONSTRUCTION: every frame of
// every animation -- the press frame, the shrink, the restore -- carries the
// same composited gray. (The v7.4.0 receipt stands: an alpha-0 row cannot
// carry a color at all -- the RGB under alpha 0 is invisible and the DWM's
// own glass shows regardless -- so any recolor fix had to be alpha > 0;
// this one is the field's own byte.) Both rows being one constant, the
// activation flip no longer changes row 0 -- only the FACE (dim text, dim
// glyphs) still flips, which is why the ordered presents stay: their row
// commit is byte-idempotent now, their face present is not.
//
// THE THREE WRITERS (all land the same constant; none ever measures):
//   * cwPatchSheetRow      -- row 0 of the HOST's bar buffer, inside the
//                             host's bar paint (before its one blit).
//   * cwCommitSheetRowFast -- the ROW-FIRST commit (v7.3.1): one row write
//                             into a PRIVATE 1 px DIB (the host's buffer is
//                             not touched, so the header is self-contained),
//                             ONE 1 px blit, GdiFlush -- microseconds -- so
//                             a flip-carrying compose can no longer sample
//                             the pre-flip row while the multi-ms face
//                             render is still running.
//   * cwFadePresenter      -- the v7.2.15 fade train, a thread. WM_TIMER is
//                             the queue's lowest-priority message; with the
//                             engine running the queue is regularly fed and
//                             timer ticks landed late, freezing the DWM's
//                             presentation-time sheet blend mid-fade (the
//                             intermittent deactivation redraw delay). The
//                             thread re-presents the row from OUTSIDE the
//                             queue: idling on the kick event, twenty 20 ms
//                             ticks per kick, each tick a 1 px blit from its
//                             own private DIB. A kick landing mid-run
//                             restarts the train, so the LAST activation
//                             state wins on rapid toggles.
//
// Threading: the latch (cwSheetRowLatch, written by the host's WM_NCACTIVATE
// FIRST, before anything else in the handler) is the frame's own truth,
// readable INSIDE the activation transaction -- unlike GetActiveWindow,
// which only catches up when WM_ACTIVATE runs after the transaction closes.
// The gate (cwSheetRowGate) stands the presenter down while the GUI thread
// is inside an ordered present (v7.3.1: a tick racing the handler would
// otherwise re-land the row into the commit window; byte-idempotent now,
// the gate is the belt it always was). The kick (cwSheetRowKick) restarts
// the train; the SetEvent still orders the GUI writes before the thread's
// reads.
inline unsigned g_sheetRowActive   = 0x802B2B2Bu; // BGRA(43,43,43,0x80): Gdiplus::Color(128,43,43,43)
inline unsigned g_sheetRowInactive = 0x802B2B2Bu; // half-alpha RGB(43,43,43): the field's gray (v7.4.3)
inline std::atomic<int>  g_ncActive{-1};     // the NC latch: 1 active, 0 inactive, -1 never seen
inline std::atomic<bool> g_guiRowCommit{false}; // the GUI-commit gate (the presenter stands down)
// v7.5.3a THE ANIMATION HOLD -- the focused state's timing gate. The header
// arms it in its own WM_SYSCOMMAND(SC_MINIMIZE) ordered present (every
// minimize source rides that message -- the caption button posts it since
// v7.5.3a); the fade train releases it ~280 ms
// into a presenting run -- the restore animation is over by then. While it
// stands, cwSheetRowBytes lands the field's gray for BOTH states: every
// frame of the shrink and the restore carries the same composited gray (the
// v7.5.3 construction -- the transparent-glass flash is impossible), and
// the gray never shows on a focused window that has no animation playing.
inline std::atomic<bool> g_sheetRowAnim{false};
// THE FOCUSED & IDLE ROW: the alpha-0 glass key (the v7.1.27..v7.5.2 active
// byte) -- the DWM's own border, accent-colorized by the registry, shows
// through untouched. Note the byte is Gdiplus::Color(0,0,0,0), NOT the
// field's "Color(255,0,0,0)": a literal (255,0,0,0) is an OPAQUE black row
// that covers the accent exactly like the gray does -- an alpha-0 row is
// the only way the accent shows (the v7.4.0 receipt).
inline unsigned g_sheetRowFocusIdle = 0x00000000u;
// the sheet's own state now (v7.5.3b): the header extends the 1 px glass
// sheet itself (cwSheetSetup, run from cwAttach and on
// WM_DWMCOMPOSITIONCHANGED) and records whether row 0 has glass behind it
// -- the header's writers (the commit and the train) must never land the
// glass key over a bare row. Hosts that run their own sheet machinery can
// still mirror theirs through the public cwSheetGlass().
inline std::atomic<bool> g_sheetHasGlass{false};

// v7.5.3b THE HEADER'S OWN DWMAPI SEAM -- the house trick (no dwmapi.h, no
// link dependency): the three pointers are fetched once into function-local
// statics, exactly how the host fetched its own. The sheet setup and
// DwmFlush live in the header now, so a fresh consumer gets the exact
// Windows 10 top edge from this file alone: the 1 px glass sheet under row
// 0 (the DWM's colorized border shows through the focused alpha-0 key) and
// the immersive dark mode attribute (without it the extended sheet paints
// WHITE in light mode -- the documented light-mode border bug). Idempotent
// with a host that runs its own copies (twinspeak does).
typedef HRESULT (WINAPI *CwDwmFlushFn)();
typedef HRESULT (WINAPI *CwDwmExtendFn)(HWND, const void*);
typedef HRESULT (WINAPI *CwDwmSetAttrFn)(HWND, DWORD, LPCVOID, DWORD);
typedef struct { int cxLeftWidth, cxRightWidth,
                 cyTopHeight, cyBottomHeight; } CwMargins;
inline CwDwmFlushFn cwDwmFlushPtr() {
    static CwDwmFlushFn fn =
        (CwDwmFlushFn)GetProcAddress(LoadLibraryA("dwmapi.dll"), "DwmFlush");
    return fn;
}
inline CwDwmExtendFn cwDwmExtendPtr() {
    static CwDwmExtendFn fn =
        (CwDwmExtendFn)GetProcAddress(LoadLibraryA("dwmapi.dll"),
                                      "DwmExtendFrameIntoClientArea");
    return fn;
}
inline CwDwmSetAttrFn cwDwmSetAttrPtr() {
    static CwDwmSetAttrFn fn =
        (CwDwmSetAttrFn)GetProcAddress(LoadLibraryA("dwmapi.dll"),
                                       "DwmSetWindowAttribute");
    return fn;
}
inline void cwDwmFlush() {                     // retire the compose in flight (the v7.3.0 ordering)
    if (CwDwmFlushFn fn = cwDwmFlushPtr()) fn();
}
inline bool cwSheetSetup(HWND w) {             // the sheet + the dark-mode border, one call
    if (!w || !IsWindow(w)) return false;
    if (CwDwmSetAttrFn set = cwDwmSetAttrPtr()) {
        const int darkOn = 1;                  // attribute 20 since 1903, 19 on the early Insider builds
        if (FAILED(set(w, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                       &darkOn, sizeof(darkOn))))
            set(w, 19, &darkOn, sizeof(darkOn));
    }
    CwDwmExtendFn ext = cwDwmExtendPtr();
    if (!ext) return false;
    const CwMargins sheet = { 0, 0, 1, 0 };    // row 0 becomes the DWM's own top border
    const bool on = SUCCEEDED(ext(w, &sheet));
    g_sheetHasGlass.store(on, std::memory_order_release);
    return on;
}

inline void cwSheetRowAnimHold(bool on) {      // armed at the minimize present; the train releases it
    g_sheetRowAnim.store(on, std::memory_order_release);
}
inline void cwSheetGlass(bool on) {            // hosts that run their own sheet mirror it here
    g_sheetHasGlass.store(on, std::memory_order_release);
}
inline HANDLE g_fadeKick   = nullptr;        // auto-reset: one SetEvent = one burst
inline HANDLE g_fadeThread = nullptr;        // created with the window, joined at stop
inline std::atomic<bool> g_fadeQuit{false};
// the fast commit's PRIVATE 1 px row DIB (GUI-thread only; the presenter
// thread keeps its own -- no GDI object is ever shared)
inline HDC     g_rowDc   = nullptr;
inline HBITMAP g_rowBm   = nullptr;
inline void*   g_rowBits = nullptr;
inline int     g_rowW    = 0;

inline unsigned cwSheetRowBytes(bool active) {
    // v7.5.3a: the gray shows when INACTIVE or while an animation holds the
    // row; focused & idle (and sheeted) the alpha-0 glass key lands and the
    // DWM's accent border shows through.
    if (active && !g_sheetRowAnim.load(std::memory_order_relaxed)
               && g_sheetHasGlass.load(std::memory_order_relaxed))
        return g_sheetRowFocusIdle;
    return active ? g_sheetRowActive : g_sheetRowInactive;
}

// row 0 of a top-down 32bpp buffer (the host's bar DIB), one constant
inline void cwPatchSheetRow(void* bits32, int cw, bool active) {
    if (!bits32 || cw <= 0) return;
    const unsigned px = cwSheetRowBytes(active);
    unsigned* row = (unsigned*)bits32;          // top-down: row 0 first
    for (int x = 0; x < cw; ++x) row[x] = px;
}

// the ROW-FIRST COMMIT (v7.3.1): fill the private row DIB, blit 1 px,
// GdiFlush. Buffer missing (the first frames): grown here -- the private
// DIB makes the commit self-contained, the host's buffer is not required.
inline void cwCommitSheetRowFast(HWND w, bool active) {
    if (!w || !IsWindow(w)) return;
    RECT cr;
    if (!GetClientRect(w, &cr) || cr.right <= 0) return;
    if (cr.right > g_rowW) {                    // (re)grow the private row DIB
        if (g_rowBm) DeleteObject(g_rowBm);
        if (g_rowDc) { DeleteDC(g_rowDc); g_rowDc = nullptr; }
        g_rowBm = nullptr; g_rowBits = nullptr; g_rowW = 0;
        HDC sdc = GetDC(w);
        if (sdc) {
            BITMAPINFO bi;
            ZeroMemory(&bi, sizeof(bi));
            bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biPlanes      = 1;
            bi.bmiHeader.biBitCount    = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            bi.bmiHeader.biHeight      = -1;    // top-down, like the bar DIB
            bi.bmiHeader.biWidth       = cr.right;
            g_rowBm = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &g_rowBits, nullptr, 0);
            if (g_rowBm) {
                g_rowDc = CreateCompatibleDC(sdc);
                if (g_rowDc) SelectObject(g_rowDc, g_rowBm);
            }
            ReleaseDC(w, sdc);
        }
        if (!g_rowDc) {
            if (g_rowBm) { DeleteObject(g_rowBm); g_rowBm = nullptr; }
            g_rowBits = nullptr;
            return;
        }
        g_rowW = cr.right;
    }
    const unsigned px = cwSheetRowBytes(active);
    unsigned* row = (unsigned*)g_rowBits;
    for (int x = 0; x < cr.right; ++x) row[x] = px;
    HDC wdc = GetDC(w);
    if (wdc) {
        BitBlt(wdc, 0, 0, cr.right, 1, g_rowDc, 0, 0, SRCCOPY);
        ReleaseDC(w, wdc);
    }
    GdiFlush();                                 // the row is in the redirection surface
}

inline void cwSheetRowLatch(int state) {       // the frame's own truth (WM_NCACTIVATE writes FIRST)
    g_ncActive.store(state);                   // seq_cst: the presenter reads relaxed, the kick orders
}
inline int cwSheetRowLatchValue() {
    return g_ncActive.load(std::memory_order_relaxed);
}
inline void cwSheetRowGate(bool on) {          // TRUE while the GUI thread is inside an ordered present
    g_guiRowCommit.store(on, std::memory_order_release);
}
inline void cwSheetRowKick() {                 // non-blocking; safe from any message
    if (g_fadeKick) SetEvent(g_fadeKick);
}

// the fade train (v7.2.15, moved here v7.5.3): re-presents row 0 from
// OUTSIDE the message queue -- the DWM bakes the sheet blend at presentation
// time (v7.1.30), and a starved repaint freezes it mid-fade.
inline DWORD WINAPI cwFadePresenter(LPVOID) {
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bi.bmiHeader.biHeight      = -1;          // top-down, like the bar DIB
    HDC     dc   = nullptr;
    HBITMAP bm   = nullptr;
    void*   bits = nullptr;
    int     rowW = 0, ticks = 0;
    for (;;) {
        // v7.5.3b PERF: the idle wait owns the thread until the next kick
        // (the old 250 ms poll woke four times a second to present nothing;
        // the presenting cadence itself is untouched -- 20 ms per tick).
        const bool kicked =
            WaitForSingleObject(g_fadeKick, ticks ? 20 : INFINITE) == WAIT_OBJECT_0;
        if (kicked) ticks = 20;               // every kick restarts the train
        else if (ticks) --ticks;
        if (!ticks) continue;                 // idle: the event owns the wait
        if (g_fadeQuit.load(std::memory_order_acquire)) break;
        if (g_guiRowCommit.load(std::memory_order_acquire)) {
            --ticks; continue;                // the GUI commit owns the row this tick
        }
        const int nc = g_ncActive.load(std::memory_order_relaxed);
        if (nc < 0) continue;                 // no activation state latched yet
        HWND w = g_host;
        if (!w || !IsWindow(w) || !IsWindowVisible(w) || IsIconic(w)) continue;
        RECT cr;
        if (!GetClientRect(w, &cr) || cr.right <= 0) continue;
        if (cr.right > rowW) {                // (re)grow the private row DIB
            if (bm) DeleteObject(bm);
            if (dc) { DeleteDC(dc); dc = nullptr; }
            bm = nullptr; bits = nullptr; rowW = 0;
            HDC sdc = GetDC(w);
            if (sdc) {
                bi.bmiHeader.biWidth = cr.right;
                bm = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
                if (bm) {
                    dc = CreateCompatibleDC(sdc);
                    if (dc) SelectObject(dc, bm);
                }
                ReleaseDC(w, sdc);
            }
            if (!dc) { if (bm) { DeleteObject(bm); bm = nullptr; } bits = nullptr; continue; }
            rowW = cr.right;
        }
        const unsigned px = cwSheetRowBytes(nc != 0);   // one constant, both states, since v7.5.3
        unsigned* row = (unsigned*)bits;
        for (int x = 0; x < cr.right; ++x) row[x] = px;
        HDC wdc = GetDC(w);
        if (wdc) {
            BitBlt(wdc, 0, 0, cr.right, 1, dc, 0, 0, SRCCOPY);
            ReleaseDC(w, wdc);
        }
        GdiFlush();                           // the strip is presented before the next tick
        // v7.5.3a THE HOLD RELEASE: after ~280 ms of presenting ticks the
        // restore animation is over -- drop the hold and re-kick so a fresh
        // train bakes the focused glass key (the accent). The iconic guard
        // above skips the shrink's ticks without presenting, so a minimize
        // alone can never reach this line and release the hold early.
        if (ticks <= 7 && g_sheetRowAnim.exchange(false, std::memory_order_acq_rel))
            cwSheetRowKick();
        --ticks;
    }
    if (bm) DeleteObject(bm);
    if (dc) DeleteDC(dc);
    return 0;
}
inline void cwSheetPresenterStart(HWND w) {    // once, with the window (after cwAttach)
    if (g_fadeThread) return;
    g_fadeQuit.store(false, std::memory_order_release);
    (void)w;                                   // the train reads g_host (cwAttach) every tick
    g_fadeKick  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_fadeThread = g_fadeKick ? CreateThread(nullptr, 0, cwFadePresenter,
                                             nullptr, 0, nullptr) : nullptr;
}
inline void cwSheetPresenterStop() {           // signal, wake, join (bounded), dispose
    g_fadeQuit.store(true, std::memory_order_release);
    if (g_fadeKick) SetEvent(g_fadeKick);
    if (g_fadeThread) {
        WaitForSingleObject(g_fadeThread, 2000);
        CloseHandle(g_fadeThread);
        g_fadeThread = nullptr;
    }
    if (g_fadeKick) { CloseHandle(g_fadeKick); g_fadeKick = nullptr; }
    if (g_rowBm) DeleteObject(g_rowBm);
    if (g_rowDc) DeleteDC(g_rowDc);
    g_rowBm = nullptr; g_rowDc = nullptr; g_rowBits = nullptr; g_rowW = 0;
}

// ---- the built-in fallback tray --------------------------------------------
inline void cwTrayEnsure() {
    if (g_trayUp || !g_host) return;
    if (!g_trayMsg) {
        g_trayMsg = WM_APP + 0x575;
        g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    }
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_host;
    g_nid.uID              = 0x5755;
    g_nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = g_trayMsg;
    g_nid.hIcon            = (HICON)GetClassLongPtrW(g_host, GCLP_HICON);
    WCHAR title[128] = L"";
    GetWindowTextW(g_host, title, 127);
    lstrcpynW(g_nid.szTip, title, (int)sizeof(g_nid.szTip) / sizeof(WCHAR));
    g_trayUp = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}
inline void cwTrayRemove() {
    if (!g_trayUp) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_trayUp = false;
}

// ---- the message hook (forward this FIRST in the host wndproc) -------------
inline bool cwWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp, LRESULT* out) {
    *out = 0;
    if (!g_enabled) return false;
    switch (msg) {
    case WM_NCACTIVATE:
        // v7.5.3b: the NC latch is latched HERE now, at the very top of
        // the dispatch -- the frame's own truth (wParam carries the state
        // the frame is flipping to), readable by every painter inside the
        // activation transaction. The message itself keeps flowing to the
        // host's own case and DefWindowProc.
        if (!wp && g_hover != cwHovNone) { g_hover = cwHovNone; cwHovAnimEnsure(); }
        cwSheetRowLatch(wp ? 1 : 0);
        return false;
    case WM_DWMCOMPOSITIONCHANGED:
        // v7.5.3b: composition can restart under the window (driver reset,
        // an RDP session) -- the {0,0,1,0} sheet is a geometry commitment
        // to a DWM that no longer exists. The header re-runs its own sheet
        // setup (re-extend + the dark-mode border; the glass flag follows
        // the result), re-lands the bar and re-kicks the train. A host's
        // own case, if it has one, still runs after this -- idempotent.
        cwSheetSetup(w);
        if (IsWindowVisible(w) && !IsIconic(w)) { cwRepaint(); cwSheetRowKick(); }
        return false;
    case WM_NCCALCSIZE: {
        // main.c verbatim: the client is the whole window MINUS the frame
        // insets on left/right/bottom (top only when maximized). The DWM
        // border + drop shadow own a real NC strip; the top stays
        // client-covered on purpose (a top gap makes the DWM paint a ghost
        // caption OUTSIDE the window rect -- the real top border belongs to
        // the host's sheet/border row).
        if (!wp) return false;
        const int frame_x = cwMetric(SM_CXFRAME);       // v7.5.3d: per-dpi metrics
        const int frame_y = cwMetric(SM_CYFRAME);
        const int padding = cwMetric(SM_CXPADDEDBORDER);
        NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lp;
        RECT* requested_client_rect = params->rgrc;
        requested_client_rect->right  -= frame_x + padding;
        requested_client_rect->left   += frame_x + padding;
        requested_client_rect->bottom -= frame_y + padding;
        if (cwIsMaximized(w)) requested_client_rect->top += frame_y + padding;
        return true; }
    case WM_NCHITTEST: {
        // main.c: DefWindowProc owns the resize edges (with WS_THICKFRAME +
        // resize allowed they resize; with resize refused they end where
        // they started), then the manual HTTOP re-answer for the band the
        // inset confuses, then HTCAPTION across the bar, HTCLIENT below.
        LRESULT hit = DefWindowProcW(w, msg, wp, lp);
        // v7.5.1 THE HONEST BORDER. A refused resize must not advertise
        // itself: WS_THICKFRAME is the style (the DWM shadow lives on it),
        // so DefWindowProc answers the edge bands the sizing codes and the
        // mouse treated a fixed window as wanting to resize -- sizing
        // arrows on a window whose drags end where they started. With
        // setCanWinResize(false) in force every edge answer downgrades to
        // HTBORDER: the arrow stays an arrow, there is nothing to grab,
        // the drag never starts. The frame style itself is untouched --
        // only the intent goes.
        if (!g_resizable) {
            switch (hit) {
                case HTLEFT: case HTRIGHT:
                case HTTOPLEFT: case HTTOP: case HTTOPRIGHT:
                case HTBOTTOMLEFT: case HTBOTTOM: case HTBOTTOMRIGHT:
                    hit = HTBORDER; break;
            }
        }
        switch (hit) {
            case HTNOWHERE: case HTBORDER: case HTRIGHT: case HTLEFT:
            case HTTOPLEFT: case HTTOP: case HTTOPRIGHT:
            case HTBOTTOMRIGHT: case HTBOTTOM: case HTBOTTOMLEFT:
                *out = hit; return true;
        }
        POINT cursor_point = {0};
        cursor_point.x = GET_X_PARAM(lp);
        cursor_point.y = GET_Y_PARAM(lp);
        ScreenToClient(w, &cursor_point);
        if (!cwIsMaximized(w) && cursor_point.y > 0 &&
            cursor_point.y < cwMetric(SM_CYFRAME) + cwMetric(SM_CXPADDEDBORDER)) {
            *out = g_resizable ? HTTOP : HTBORDER;   // v7.5.1: the honest border
            return true;
        }
        if (cursor_point.y < g_barH) { *out = HTCAPTION; return true; }
        *out = HTCLIENT; return true; }
    case WM_NCMOUSEMOVE: {
        // which button the mouse is over -- hover state lives here; the
        // host's repaint callback lands the pixels synchronously
        POINT cursor_point;
        GetCursorPos(&cursor_point);
        ScreenToClient(w, &cursor_point);
        RECT cr; GetClientRect(w, &cr);
        const CwButtonRects br = cwButtonRects(cr.right);
        int nh = cwHovNone;
        if (PtInRect(&br.close, cursor_point)) nh = cwHovClose;
        else if (g_btnMax && PtInRect(&br.maximize, cursor_point)) nh = cwHovMax;
        else if (g_btnMin && PtInRect(&br.minimize, cursor_point)) nh = cwHovMin;
        if (nh != g_hover) { g_hover = nh; cwHovAnimEnsure(); }   // the fade owns the repaints
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, w, 0 };
        TrackMouseEvent(&tme);          // the leave that starts the 300 ms fade-out
        *out = DefWindowProcW(w, msg, wp, lp); return true; }
    case WM_NCLBUTTONDOWN:
        // the DOWN must not count as a drag start when a button is hovered
        if (g_hover != cwHovNone) { *out = 0; return true; }
        return false;
    case WM_NCLBUTTONUP:
        // the dispatch: close posts WM_CLOSE (the host decides hide or
        // destroy), minimize minimizes, maximize toggles (refused while
        // setCanWinResize(false) -- a dead button for a host that asked)
        if (g_hover == cwHovClose)      { PostMessageW(w, WM_CLOSE, 0, 0); *out = 0; return true; }
        // v7.5.3a: the click rides WM_SYSCOMMAND(SC_MINIMIZE) now, not a
        // raw ShowWindow -- the header's ordered present (the WM_SYSCOMMAND
        // case below: the hold, DwmFlush, the row-first gray commit, the
        // face, the kick) arms BEFORE DefWindowProc flips anything. A raw ShowWindow skipped
        // all of it: the surface kept the focused glass key and the shrink
        // flew transparent (the v7.5.4..v7.5.10 flash).
        if (g_hover == cwHovMin)        { PostMessageW(w, WM_SYSCOMMAND, SC_MINIMIZE, 0); *out = 0; return true; }
        if (g_hover == cwHovMax && g_btnMax) {
            if (g_resizable) ShowWindow(w, cwIsMaximized(w) ? SW_RESTORE : SW_MAXIMIZE);
            *out = 0; return true;
        }
        return false;
    case WM_NCRBUTTONUP:
        // the caption right-click system menu, with the item states the
        // resize policy actually honors (a dead item is the same lie as a
        // dead button). Return 0: DefWindowProc would stack its own
        // SC_MOUSEMENU on top. Appended items survive -- the menu object is
        // the host's (GetSystemMenu), only the states are set here.
        if (wp == HTCAPTION) {
            const BOOL isMax = cwIsMaximized(w) ? TRUE : FALSE;
            MENUITEMINFO mii;
            memset(&mii, 0, sizeof(mii));
            mii.cbSize = sizeof(mii);
            mii.fMask  = MIIM_STATE;
            HMENU sys = GetSystemMenu(w, FALSE);
            auto state = [&](UINT item, bool enabled) {
                mii.fState = enabled ? MF_ENABLED : MF_DISABLED;
                SetMenuItemInfo(sys, item, FALSE, &mii);
            };
            state(SC_RESTORE,  isMax != FALSE);
            state(SC_MOVE,     true);
            state(SC_SIZE,     g_resizable);
            state(SC_MINIMIZE, true);
            state(SC_MAXIMIZE, g_resizable);
            state(SC_CLOSE,    true);
            const BOOL result = TrackPopupMenu(sys, TPM_RETURNCMD,
                                               GET_X_PARAM(lp), GET_Y_PARAM(lp),
                                               0, w, nullptr);
            if (result != 0) PostMessageW(w, WM_SYSCOMMAND, (WPARAM)result, 0);
            *out = 0; return true;
        }
        return false;
    case WM_NCMOUSELEAVE:
        // the cursor left the window: the fade-out owns what is on screen now
        if (g_hover != cwHovNone) { g_hover = cwHovNone; cwHovAnimEnsure(); }
        return false;
    case WM_TIMER:
        // the hover fade train's own tick; every other timer is the host's
        if (wp == kHovTimerId && g_hovTimerOn) { cwHovAnimTick(); *out = 0; return true; }
        return false;
    case WM_DPICHANGED: {
        // v7.5.3d: the scale moved under the window (a system scale change, a
        // dock, a drag across monitors). The header re-derives its own
        // geometry (dpi, bar height, title font), applies the DWM's suggested
        // rect, hands the host its hook (constants + fonts), then re-seats
        // the tracked children at the new scale. Handled HERE -- the host's
        // switch never sees it.
        const UINT nd = HIWORD(wp);
        if (nd && nd != cwDpi()) {
            cwDpiChanged(nd);
            g_barH = cwS(kBarHDesign);
            const LPRECT r = (LPRECT)lp;
            if (r) SetWindowPos(w, nullptr, r->left, r->top,
                                r->right - r->left, r->bottom - r->top,
                                SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_dpiHook) g_dpiHook();
            cwRelayoutChildren();
            if (IsWindowVisible(w) && !IsIconic(w)) { cwRepaint(); cwSheetRowKick(); }
        }
        *out = 0; return true; }
    case WM_SYSCOMMAND: {
        const UINT sc = (UINT)(wp & 0xFFF0);
        if (!g_resizable && (sc == SC_MAXIMIZE || sc == SC_SIZE)) { *out = 0; return true; }
        // v7.5.3b THE ORDERED MINIMIZE PRESENT -- the header's now (the
        // host's copy retired with the move). Every minimize source -- the
        // caption button (posts this message since v7.5.3a), the taskbar,
        // the system menu, the keyboard -- presents through this one
        // recipe BEFORE DefWindowProc flips the placement iconic: the
        // animation hold arms (the gray may show only while an animation
        // plays), the gate stands the train down, the latch flips FIRST
        // (the frame's own truth -- the repaint below answers through it
        // and lands the INACTIVE face, the same bytes the
        // WM_NCACTIVATE(FALSE) present would), DwmFlush retires the
        // compose in flight, the row lands (the half-alpha gray,
        // byte-idempotent -- the FACE is what this present orders), then
        // the gate clears and the train kicks. The restore's own
        // activation re-kicks; ~280 ms into that presenting run the train
        // releases the hold and the focused glass key -- the DWM's accent
        // border -- bakes once no animation is playing.
        if (sc == SC_MINIMIZE && IsWindowVisible(w) && !IsIconic(w)) {
            cwSheetRowAnimHold(true);
            cwSheetRowGate(true);
            cwSheetRowLatch(0);
            cwDwmFlush();
            cwCommitSheetRowFast(w, false);
            cwRepaint();                   // the inactive face, through the latch answer
            cwSheetRowGate(false);
            cwSheetRowKick();
        }
        return false; }
    case WM_SETCURSOR:
        // the arrow instead of the busy cursor (main.c)
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return false;   // the host's DefWindowProc finishes the protocol
    case WM_MOUSEMOVE:
        // into the client = no caption button hovered
        if (g_hover != cwHovNone) { g_hover = cwHovNone; cwHovAnimEnsure(); }
        return false;
    default:
        if (g_trayUp && g_trayMsg && msg == g_trayMsg) {
            if (lp == WM_LBUTTONUP) { putWindowinTray(false); *out = 0; return true; }
            return false;
        }
        if (g_taskbarCreated && msg == g_taskbarCreated && g_trayUp) {
            // explorer restarted: re-add the fallback icon
            g_trayUp = false;
            cwTrayEnsure();
            *out = 0; return true;
        }
        return false;
    }
}

} // namespace cwd

// =====================================================================
// THE PUBLIC API (the eleven) -- global, inline, one file.
// =====================================================================

inline void enableCustomWin(bool enable) {
    if (cwd::g_enabled == enable) return;
    cwd::g_enabled = enable;
    // the vanilla <-> custom switch: WS_CAPTION is what a vanilla window
    // carries and a custom window must not
    if (cwd::g_host) {
        LONG_PTR st = GetWindowLongPtrW(cwd::g_host, GWL_STYLE);
        const bool hasCap = (st & WS_CAPTION) != 0;
        if (enable && hasCap)  SetWindowLongPtrW(cwd::g_host, GWL_STYLE, st & ~WS_CAPTION);
        if (!enable && !hasCap) SetWindowLongPtrW(cwd::g_host, GWL_STYLE, st | WS_CAPTION);
        SetWindowPos(cwd::g_host, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    cwd::cwRepaint();
}

inline void cwApplyFace(bool active, int col, int grad) {
    if (active) { cwd::g_faceCol = col; cwd::g_faceGrad = grad; }
    else        { cwd::g_faceColIn = col; cwd::g_faceGradIn = grad; }
}

inline void changeWinTitlebarColor(int col)                      { cwd::g_faceCol = col; cwd::g_faceGrad = col; cwd::g_faceColIn = -1; cwd::g_faceGradIn = -1; cwd::cwRepaint(); }
inline void changeWinTitlebarColor(int col, int gradColor)       { cwd::g_faceCol = col; cwd::g_faceGrad = gradColor; cwd::cwRepaint(); }

inline void setWinTitleTextColor(int color, int gradientCol,
                                 int inactiveColor, int gradientInactiveCol) {
    // v7.5.3c: ONE setter for both states (the changeWinTextColor /
    // changeWinTextUnfocusedColor pair retired). The requested defaults --
    // inactiveColor = color, gradientInactiveCol = gradientCol -- are
    // spelled as overloads below (C++ function default arguments may not
    // reference the parameters to their left; template defaults may, and
    // the first draft leaned on that -- clang threw it right back).
    // A gradientInactiveCol >= 0 paints the inactive gradient, < 0 paints
    // inactiveColor solid (the painter's rule -- pass -1 for solid).
    cwd::g_textCol = color;  cwd::g_textGrad = gradientCol;
    cwd::g_textUn  = inactiveColor;
    cwd::g_textUnGrad = gradientInactiveCol;
    cwd::cwRepaint();
}
inline void setWinTitleTextColor(int color, int gradientCol, int inactiveColor) {
    setWinTitleTextColor(color, gradientCol, inactiveColor, gradientCol);
}
inline void setWinTitleTextColor(int color, int gradientCol) {
    setWinTitleTextColor(color, gradientCol, color, gradientCol);
}

// v7.5.3d: changeWinMinimizeHoverColor / changeWinMaximizeHoverColor are
// retired -- the hover is the fixed 10% black tint now (the real thing's
// look is not a color knob).

inline void changeWinTextFont(const std::string& font = "Segoe UI") {
    std::wstring wide(font.begin(), font.end());
    cwd::cwResolveFont(wide);          // on failure the previous family stays
    cwd::g_fontFace = wide;
    cwd::g_fontKey.clear();            // force the rebuild
    cwd::cwRepaint();
}

inline void enableWinIcon(bool enable)              { cwd::g_icon = enable; cwd::cwRepaint(); }
inline void setWinTitlebarButtonLayout(bool minimize, bool maximize) {
    cwd::g_btnMin = minimize; cwd::g_btnMax = maximize; cwd::cwRepaint();
}
inline void setCanWinResize(bool resize)            { cwd::g_resizable = resize; cwd::cwRepaint(); }

inline void putWindowinTray(bool hide) {
    if (hide) {
        if (cwd::g_trayHide) { cwd::g_trayHide(cwd::g_host); return; }
        cwd::cwTrayEnsure();           // the window must stay reachable
        if (cwd::g_host) ShowWindow(cwd::g_host, SW_HIDE);
    } else {
        if (cwd::g_trayShow) { cwd::g_trayShow(cwd::g_host); return; }
        if (cwd::g_host) {
            ShowWindow(cwd::g_host, SW_SHOW);
            SetForegroundWindow(cwd::g_host);
            SetFocus(cwd::g_host);
        }
    }
}

// ---- host plumbing (twinspeak's adoption seams; fresh consumers skip) ------

inline void cwAttach(HWND w, void (*repaint)(HWND)) {
    cwd::g_host = w; cwd::g_repaint = repaint;
    cwd::cwSheetSetup(w);   // v7.5.3b: the sheet + the dark-mode border -- the header's own
}
inline void cwSetBarHeight(int px)                  { cwd::g_barH = px; }
// v7.5.3d: the bar height is the header's own -- 33 design px at the header's
// dpi. A host adopts this as its caption-height layout contract (the buffer
// height, the content y offset) instead of deriving its own S(31).
inline int  cwBarHeight()                           { return cwd::cwS(cwd::kBarHDesign); }
inline UINT cwDpi()                                 { return cwd::cwDpi(); }   // v7.5.3d
inline void cwTrackChild(HWND h, int x, int y, int w, int ht,
                         bool underBar = true, HFONT* font = nullptr,
                         int comboItemH = 0) {   // v7.5.3d
    cwd::cwTrackChild(h, x, y, w, ht, underBar, font, comboItemH);
}
inline void cwRelayoutChildren()                    { cwd::cwRelayoutChildren(); }
inline void cwSetDpiHook(void (*hook)())            { cwd::cwSetDpiHook(hook); }
inline void cwSetTrayHost(void (*hide)(HWND), void (*show)(HWND)) {
    cwd::g_trayHide = hide; cwd::g_trayShow = show;
}
inline void cwSeedTitleFont(const std::string& face, int px, int weight) {
    std::wstring wide(face.begin(), face.end());
    cwd::g_fontFace = wide; cwd::g_fontPx = px; cwd::g_fontWeight = weight;
    cwd::g_fontKey.clear();
}
// per-theme seed of every chrome color (twinspeak's applyTheme re-seeds on
// every theme switch). inactive face defaults to mirroring the active one.
// v7.5.3d: the hover-color pair is gone from the seed -- the tint owns it.
inline void cwSeedChrome(int faceCol, int faceGrad,
                         int faceColIn, int faceGradIn,
                         int textCol, int textGrad, int textUn,
                         int glyphCol) {
    cwd::g_faceCol = faceCol;   cwd::g_faceGrad = faceGrad;
    cwd::g_faceColIn = faceColIn < 0 ? faceCol  : faceColIn;
    cwd::g_faceGradIn = faceGradIn < 0 ? faceGrad : faceGradIn;
    cwd::g_textCol = textCol;   cwd::g_textGrad = textGrad;
    cwd::g_textUn = textUn;     cwd::g_glyph = glyphCol;
    cwd::cwRepaint();
}
inline DWORD cwFrameStyle() {
    return WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
}
inline bool cwWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp, LRESULT* out) {
    return cwd::cwWndProc(w, msg, wp, lp, out);
}
inline void cwPaintBarBackground(Gdiplus::Graphics& g, int ww, bool active) {
    cwd::cwPaintBarBackground(g, ww, active);
}
inline void cwPaintBarForeground(Gdiplus::Graphics& g, HWND w, int ww, bool active) {
    cwd::cwPaintBarForeground(g, w, ww, active);
}
inline void cwPaintChrome(Gdiplus::Graphics& g, HWND w, int ww, bool active) {
    cwd::cwPaintChrome(g, w, ww, active);
}
inline bool cwIsMaximized(HWND w) { return cwd::cwIsMaximized(w); }
inline DWORD cwArgbOf(COLORREF c) { return cwd::cwArgbOf(c); }   // COLORREF -> ARGB

// ---- the fake top border bar (the sheet row; see the essay above) ----------
inline void cwSeedSheetRow(unsigned activeBgra, unsigned inactiveBgra) {
    cwd::g_sheetRowActive   = activeBgra;      // BGRA u32, premultiplied
    cwd::g_sheetRowInactive = inactiveBgra;
}
inline void cwPatchSheetRow(void* bits32, int cw, bool active) {
    cwd::cwPatchSheetRow(bits32, cw, active);
}
inline void cwCommitSheetRowFast(HWND w, bool active) {
    cwd::cwCommitSheetRowFast(w, active);
}
inline void cwSheetRowGate(bool on)    { cwd::cwSheetRowGate(on); }
inline void cwSheetRowKick()           { cwd::cwSheetRowKick(); }
inline void cwSheetRowLatch(int state) { cwd::cwSheetRowLatch(state); }
inline int  cwSheetRowLatchValue()     { return cwd::cwSheetRowLatchValue(); }
inline void cwSheetPresenterStart(HWND w) { cwd::cwSheetPresenterStart(w); }
inline void cwSheetPresenterStop()        { cwd::cwSheetPresenterStop(); }
// v7.5.3a: the animation hold + the sheet mirror (see the essay above)
inline void cwSheetRowAnimHold(bool on)   { cwd::cwSheetRowAnimHold(on); }
inline void cwSheetGlass(bool on)         { cwd::cwSheetGlass(on); }

#else  /* !_WIN32 -- the stub target: the API exists, does nothing ---------- */

inline void enableCustomWin(bool)                                      {}
inline void changeWinTitlebarColor(int)                                {}
inline void changeWinTitlebarColor(int, int)                           {}
inline void setWinTitleTextColor(int, int, int, int)                   {}
inline void setWinTitleTextColor(int, int, int)                        {}
inline void setWinTitleTextColor(int, int)                             {}
inline void changeWinTextFont(const std::string& = "Segoe UI")         {}
inline void enableWinIcon(bool)                                        {}
inline void setWinTitlebarButtonLayout(bool, bool)                     {}
inline void setCanWinResize(bool)                                      {}
inline void putWindowinTray(bool)                                      {}
inline int  cwBarHeight()                                              { return 33; }
inline void cwTrackChild(void*, int, int, int, int, bool, void*, int)  {}
inline void cwRelayoutChildren()                                       {}
inline void cwSetDpiHook(void (*)())                                   {}
inline void cwSeedSheetRow(unsigned, unsigned)                         {}
inline void cwPatchSheetRow(void*, int, bool)                          {}
inline void cwCommitSheetRowFast(void*, bool)                          {}
inline void cwSheetRowGate(bool)                                       {}
inline void cwSheetRowKick()                                           {}
inline void cwSheetRowLatch(int)                                       {}
inline int  cwSheetRowLatchValue()                                     { return -1; }
inline void cwSheetPresenterStart(void*)                               {}
inline void cwSheetPresenterStop()                                     {}
inline void cwSheetRowAnimHold(bool)                                   {}
inline void cwSheetGlass(bool)                                         {}

#endif /* _WIN32 */

#endif /* CUSTOMWINDOW_H_INCLUDED */
