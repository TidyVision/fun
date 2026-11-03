// customWindow.h -- THE ONE-FILE WINDOW (v7.5.5)
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
//     setWinInactiveColor(int col) / (int col, int gradColor)     v7.5.5
//         The caption bar's background while the window is INACTIVE. One
//         argument: solid. Two: a vertical top->bottom gradient. -1 restores
//         the default: mirror the focused color. The field's reason: the
//         real Windows 10 dark mode turns the bar GRAY on inactive and
//         keeps it black on focused -- a distinct inactive face is exactly
//         that. Colors are ARGB ints (0xAARRGGBB).
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
//         layout). Buttons are 45.5 design px wide each (v7.6.0: two
//         narrower than the sample's 47; v7.6.2: half a pixel BACK -- the
//         field's call, and yes, seriously). Default: minimize on,
//         maximize off.
//
//     setWinCloseBtnColor(int col)                                v7.6.2
//         The close button's hover red. One ARGB int (0xAARRGGBB); the
//         alpha channel is ignored -- the hover ramp owns the fade, the
//         color rides it. Default: RGB(232,17,35), the real thing's full
//         red (#E81123) -- the old 0xCC0000 was a step dark.
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
//     The button hover (v7.5.3d..v7.5.4): no longer a color knob. The minimize
//     and maximize buttons show a 10% black tint (measured off the real
//     thing) that fades in over 100 ms and out over 150 ms, LINEAR -- at
//     TWICE the speed while the window is unfocused (v7.5.4: 50/75 ms, the
//     real caption's inactive behavior; file explorer shows it) -- painted
//     over everything in the button, in a band of its own (v7.5.3f): ONE
//     real px of daylight at the top (row 0 -- the border row -- is never
//     covered; the v7.5.3e top pin is retired) and the whole band THREE real
//     px shorter than the bar (two px of daylight at the bottom), with crisp
//     whole-pixel seams between neighbouring buttons (the anti-aliased
//     sub-pixel seams smeared the edges; integer renders the hairline); the
//     glyphs ride the band, two real px higher to stay centered in it, and
//     while the window is UNFOCUSED the glyphs are half transparent -- a
//     hovered button's glyph rides its fade back to full (v7.5.4, the
//     task-manager behavior). The close button fades its vanilla red the
//     same way and the X whitens with it.
//     changeWinMinimizeHoverColor / changeWinMaximizeHoverColor are retired.
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
// v7.5.3d..v7.5.4: the hover is no longer a solid face color. The min/max
// hover is a black TINT -- 10% alpha (0x1A = 26, measured off a real
// Windows 10 caption button) -- that fades in over 100 ms and out over
// 150 ms, LINEAR (HALF those durations while the window is unfocused,
// v7.5.4: the real thing fades its caption buttons at twice the speed
// while inactive -- the field watches file explorer do it), painted OVER
// everything in the button (the glyph included), in a band of
// its own (v7.5.3f): ONE real px of daylight at the top -- row 0, the
// border row, is never covered (the v7.5.3e top pin lived one build) -- and
// THREE real px shorter than the bar, two px of daylight at the bottom. The
// seams between neighbouring buttons are WHOLE pixels (the real thing's 0.5
// design px hairline snapped to physical pixels -- the old anti-aliased
// sub-pixel insets smeared the edges into a two-column gradient, the
// field's "blurred"). The close button fades its vanilla red the same way;
// the X lerps to white with the fill.
inline constexpr int kHovTintAlpha = 26;    // 10% of 255 (0x1A), the measured value
// v7.5.5: the tint's CHANNEL follows the face being painted -- the field's
// rule: HSL lightness < 128 (kHovDarkFaceL) reads as a dark face, and the
// real caption's hover LIGHTENS a dark face: the UWP dark resources put the
// button hover a step lighter than the face (#343434 over #171717, which
// reads as a ~12.5% white overlay -- the field considered 12.5% for both
// modes and kept the measured 10%). A light face keeps the measured black
// step. The alpha never changes, only the channel; the close red is
// untouched -- the real close hover is red in both modes.
inline constexpr int kHovDarkFaceL = 128;   // the field's dark-face line
inline constexpr int kHovFadeInMs  = 100;   // pre-hover ramp, focused (v7.5.3e: was 150)
inline constexpr int kHovFadeOutMs = 150;   // post-hover ramp, focused (v7.5.3f: e had 100, d had 300)
// v7.5.4: the unfocused window's hover runs its own, faster ramps -- the
// highlight snaps on and off the way the real caption's does when the
// window does not have focus. g_hovInactive is latched in the WM_NCACTIVATE
// case, next to the sheet-row latch (the frame's own truth, hover's view).
// v7.6.1: the field's call -- 75 ms pre AND post (the resting values).
// v7.6.2: they went DYNAMIC -- the pointer's tempo over the caption
// steers them inside the field's bracket (a fast flick -- "have that
// flashing effect be slowed down based on how fast you've unhovered the
// thing"). The flash's fade-back rides the OUT value; the focused bar
// keeps its flat 100/150.
// v7.6.4: THE BRACKET ITSELF MOVES -- the field: "have the fade be 100ms
// to 300ms instead." The floor is 100 (a slow, deliberate pull-off burns
// it fast at 100, the flash barely lingers) and the ceiling 300 (a fast
// flick stretches the fade-back toward 300).
inline int kHovInactiveInMs  = 75;   // pre-hover ramp, unfocused (dynamic)
inline int kHovInactiveOutMs = 75;   // post-hover ramp, unfocused (dynamic)
inline constexpr int kHovInactiveMinMs = 100;   // the bracket's floor (v7.6.4: was 50)
inline constexpr int kHovInactiveMaxMs = 300;   // the bracket's ceiling (v7.6.4: was 200)
inline bool g_hovInactive = false;
// v7.5.4: the glyphs ride the band -- the band took its three px off the
// bottom of the bar, so a glyph centered on the FULL button rect hangs
// below the highlight's visual center. Every caption glyph (min line, max
// squares, close X) sits this many REAL px higher; unscaled, like the band
// itself. The window icon and the title text do not ride.
inline constexpr int kHovGlyphRide = 2;
// v7.5.4: the UNFOCUSED caption's glyphs are HALF there -- the field's
// task-manager observation: the real buttons dim to ~50% opacity while
// their window is inactive. A hovered button's glyph rides its own fade
// ramp (g_hovA[i]) back to full opacity; focused windows never dim.
inline constexpr int kGlyphInactiveAlpha = 128;   // ~50% of 255
inline constexpr UINT kHovTimerId  = 0x43485631u;  // the frame's own animation timer
inline int   g_hovA[3] = { 0, 0, 0 };       // min, max, close: current alpha 0..255
inline DWORD g_hovLast = 0;                 // the last tick's GetTickCount
inline bool  g_hovTimerOn = false;          // the 16 ms animation timer is running
// v7.6.1: the inactive lapse, on EVERY caption button -- when a button's
// hover ends on an INACTIVE window, that button's GLYPH is set to ZERO
// (kCwLapseAlpha = 0, fully transparent -- v7.6.3: the field's own
// beginning-of-the-project knowledge, "the unhover alpha transparency
// starts at 0 and fades back"; the old 32/12.5% floor was v7.6.0's
// misparse of the vanilla read) and fades back to the half-there look it
// rests at (kGlyphInactiveAlpha) over g_lapseMs[i], the leave's tempo
// (v7.6.2). v7.6.0's cut read the
// floor into the shared hover ramp instead (g_hovA = 32): the tint landed
// at 3/255 and the glyph at 144 vs the 128 rest -- both under the eye,
// which is exactly why the field saw nothing. The flash owns the glyph's
// alpha OUTRIGHT now (g_lapseA, read directly by glyphCol); the ramp keeps
// the tint. Inactive only -- the focused bar never lapses -- and every
// activation change and hide takes the live flashes with it.
inline constexpr int  kCwLapseAlpha = 0;    // the flash floor: FULLY transparent (v7.6.3, was 32)
// v7.6.5 THE SLOW-LEAVE CAP -- the field: "if the titlebar button unhover
// threshold is too slow (more than 250ms) then the fade thing is not done
// at all." The leave's tempo (cwHovTempoMs, captured into
// kHovInactiveOutMs at every leave site) answers for the threshold: past
// 250 ms the lapse is SKIPPED OUTRIGHT -- the glyph never drops to zero
// and simply rests at its half-there value. The fade-back only exists
// inside [100, 250] ms; a 2 px/ms flick (the 300 ms ceiling) gets no flash
// at all.
// v7.6.6 -- the field: "Have kLapseMaxMs be 100ms instead too." The cap
// drops onto the bracket's own floor: the lapse now lives ONLY at the
// 100 ms floor tempo (a still, deliberate pull-off); every faster leave
// -- anything past 100 ms, up to the 300 ms ceiling -- gets NO flash at
// all, the glyph simply rests at its half-there value.
inline constexpr int  kCwLapseMaxMs = 100;
inline bool  g_lapseOn[3] = { false, false, false };  // min, max, close: a flash is live
inline int   g_lapseA[3] = { 0, 0, 0 };               // the flash's glyph alpha, 0..128
inline DWORD g_lapseT0[3] = { 0, 0, 0 };              // the unhover tick each flash started
inline int   g_lapseMs[3] = { 100, 100, 100 };        // each flash's own fade-back life (v7.6.2: the leave's tempo)
// v7.6.2: the pointer's tempo over the caption, feeding the durations --
// an EMA of (px/ms)^2 sampled on every WM_NCMOUSEMOVE. Squared, so the
// mapping needs no sqrt; a stale sample reads as stillness (the floor).
inline DWORD g_hovMoveLast = 0;           // the last NCMOUSEMOVE tick
inline POINT g_hovMovePt  = { 0, 0 };     // its position
inline float g_hovSpeed2  = 0.0f;         // (px/ms)^2, EMA'd
inline constexpr float kCwHovSpeedRef2 = 4.0f;  // a 2 px/ms flick pins the ceiling
// The tempo -> a duration inside the bracket: the floor at rest, the
// ceiling at (and past) the 2 px/ms reference, linear between.
inline int cwHovTempoMs() {
    float ratio = (GetTickCount() - g_hovMoveLast > 200) ? 0.0f
                : g_hovSpeed2 / kCwHovSpeedRef2;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    return kHovInactiveMinMs
         + (int)((float)(kHovInactiveMaxMs - kHovInactiveMinMs) * ratio + 0.5f);
}
// v7.6.2: the close hover is a knob now -- setWinCloseBtnColor(int col) --
// and its default is the REAL full red, RGB(232,17,35) (the old 0xCC0000
// was a step dark; the field's measure is the thing's own #E81123).
inline DWORD g_closeHover = 0xFFE81123;   // ARGB; the alpha rides the hover ramp
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
// v7.6.3: THE MAXIMIZED CAPTION -- the real thing's maximized bar is
// SHORTER: 24 design px (the border row's daylight is gone with the
// sheet), the buttons one design px shorter than the bar, the close
// button two design px narrower. cwEffBarH() answers the effective
// height; every painter, rect and hit-test reads it.
inline constexpr int kBarHMaxDesign = 24;
inline bool   g_wasMax    = false;        // the WM_SIZE maximize-flip latch
inline int    g_barH      = 33;           // caption bar height, REAL px (the NORMAL baseline)
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

// v7.5.5: HSL lightness of an ARGB color, 0..255 -- L = (max + min) / 2 on
// the RGB channels. The hover's dark-face rule reads this (kHovDarkFaceL).
inline int cwHslLightness(int argb) {
    const int r = cwChannel(argb, 16), g2 = cwChannel(argb, 8), b = cwChannel(argb, 0);
    const int mx = r > g2 ? (r > b ? r : b) : (g2 > b ? g2 : b);
    const int mn = r < g2 ? (r < b ? r : b) : (g2 < b ? g2 : b);
    return (mx + mn) / 2;
}
inline bool cwIsMaximized(HWND w) {          // main.c verbatim
    WINDOWPLACEMENT placement = {0};
    placement.length = sizeof(WINDOWPLACEMENT);
    return GetWindowPlacement(w, &placement)
        && placement.showCmd == SW_SHOWMAXIMIZED;
}
// v7.6.3: the EFFECTIVE bar height -- 24 design px while the host window
// is maximized, the normal 33 otherwise. Every bar consumer (the painters,
// the button rects, the hit-test, the child relayout, cwRepaint) answers
// through this one getter, so the maximized style is one branch, not a
// second geometry.
inline int cwEffBarH() {
    return (g_host && cwIsMaximized(g_host)) ? cwS(kBarHMaxDesign) : g_barH;
}
inline void cwRepaint() {
    if (g_repaint && g_host) { g_repaint(g_host); return; }
    if (g_host) {
        RECT r; if (GetClientRect(g_host, &r)) { r.bottom = cwEffBarH(); InvalidateRect(g_host, &r, FALSE); }
    }
}

// ---- v7.5.3d: the hover fade train ------------------------------------------
// Linear ramps per button (min, max, close): focused 100/150 ms, unfocused
// 75/75 ms (v7.6.1; the v7.5.4 halving is retired) -- driven by a private
// 16 ms WM_TIMER on the host window; every tick repaints the bar through the
// host's synchronous painter, which reads g_hovA (and, v7.6.1, the lapse
// arrays). The timer lives only while a ramp or a flash is in flight, then
// it is gone again.
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
        g_lapseOn[0] = g_lapseOn[1] = g_lapseOn[2] = false;   // v7.6.1: no flash survives the hide
        KillTimer(w, kHovTimerId); g_hovTimerOn = false;
        return;
    }
    const DWORD now = GetTickCount();
    DWORD dt = now - g_hovLast;
    g_hovLast = now;
    if (dt > 1000) dt = 1000;                       // a stalled queue must not jump the ramp
    bool busy = false;
    // v7.5.4: the ramps' tempo follows the activation state; v7.6.1: the
    // unfocused tempo is its own 75 ms pre and post.
    const int inMs  = g_hovInactive ? kHovInactiveInMs  : kHovFadeInMs;
    const int outMs = g_hovInactive ? kHovInactiveOutMs : kHovFadeOutMs;
    for (int i = 0; i < 3; ++i) {
        const bool held = (g_hover == i + 1);
        int a = g_hovA[i];
        if (held) a += (int)(dt * 255 / inMs);
        else      a -= (int)(dt * 255 / outMs);
        if (a > 255) a = 255;
        if (a < 0) a = 0;
        if (a != g_hovA[i]) { g_hovA[i] = a; busy = true; }
        if ((held && a < 255) || (!held && a > 0)) busy = true;
        // v7.6.1: the lapse -- the flash's GLYPH alpha eases 0 -> 128
        // (v7.6.3: the floor is FULL transparency, not 32) across its own
        // life (g_lapseMs[i], the leave's tempo), timed off
        // the unhover tick. It dies the tick it lands or the moment its
        // button is re-hovered -- the normal ramp owns the glyph again from
        // there.
        if (g_lapseOn[i]) {
            if (held) { g_lapseOn[i] = false; }
            else {
                const DWORD el = now - g_lapseT0[i];
                // v7.6.2: the flash lives on the LEAVE's tempo --
                // g_lapseMs[i], captured at trigger, inside the bracket
                // (v7.6.4: [100, 300] -- "have the fade be 100ms to 300ms
                // instead"); a fast flick stretches it toward 300, a slow
                // pull-off burns it fast at 100.
                if (el >= (DWORD)g_lapseMs[i]) g_lapseOn[i] = false;
                else {
                    g_lapseA[i] = kCwLapseAlpha
                                + ((kGlyphInactiveAlpha - kCwLapseAlpha) * (int)el) / g_lapseMs[i];
                    busy = true;
                }
            }
        }
    }
    cwRepaint();                                    // the painter reads g_hovA
    if (!busy) { KillTimer(w, kHovTimerId); g_hovTimerOn = false; }
}

// ---- v7.6.1: the inactive lapse (every caption button) -----------------------
// The field, on the unmodified Windows 10 bar: "all titlebar buttons on
// their inactive state do the thing where it goes to 12.5% transparency
// right after unhovering and fading back to 50% transparency after 100ms"
// -- and v7.6.3's correction, the reading the field knew "from the
// beginning of trying to replicate windows 10's titlebar buttons": the
// unhover transparency STARTS AT 0 -- the glyph vanishes outright -- and
// fades back. The parse: on an INACTIVE window, when ANY button's hover
// ends, that button's GLYPH alpha is SET to 0 of 255 (fully transparent)
// -- right away, not on the next tick -- and eases back to its half-there
// rest (kGlyphInactiveAlpha,
// 128 = the "50% transparency") across g_lapseMs[i], the LEAVE's tempo
// (v7.6.2: inside the tempo bracket; v7.6.4: [100, 300] -- a fast flick
// stretches the flash toward 300, a slow pull-off burns it fast at 100).
// v7.6.0 read the old
// 12.5% floor into the shared hover ramp instead -- g_hovA = 32 lands the
// tint at 3/255 and the glyph at 144 vs the 128 rest, both under the eye,
// which is why the field saw nothing. Inactive only; every activation
// change (the WM_NCACTIVATE case) and every hide (the tick's rest branch)
// clears the flashes, and re-hovering a button hands its glyph back to
// the ramps.
inline void cwHovLapse(int i) {
    if (i < 0 || i > 2) return;
    // v7.6.5: the slow-leave cap -- kHovInactiveOutMs was JUST captured off
    // the leave's tempo at every leave site (WM_NCMOUSEMOVE's change,
    // WM_NCMOUSELEAVE, the WM_MOUSEMOVE walk into the client); past 250 ms
    // the flash does not exist. The glyph rests where the ramps put it.
    if (kHovInactiveOutMs > kCwLapseMaxMs) return;
    g_lapseOn[i] = true;                 // the flash is live
    g_lapseT0[i] = GetTickCount();       // its life starts now
    g_lapseA[i]  = kCwLapseAlpha;        // the transparency set to ZERO, this very frame
    g_lapseMs[i] = kHovInactiveOutMs;   // v7.6.2: the leave's tempo is the flash's life
    cwHovAnimEnsure();                   // the train owns the repaints, as always
}
inline void cwHovLapseClearAll() {       // activation changes and hides take every flash
    g_lapseOn[0] = g_lapseOn[1] = g_lapseOn[2] = false;
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
    const int barNow = cwEffBarH();   // v7.6.3: the children ride the EFFECTIVE bar height
    for (auto& k : g_kids) {
        if (!k.h || !IsWindow(k.h)) continue;
        SetWindowPos(k.h, nullptr,
                     cwS(k.x), cwS(k.y) + (k.underBar ? barNow : 0),
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
// Client coordinates over a bar 0..g_barH tall, buttons 45.75 design px
// wide with a 0.25 design px gap (v7.6.0: two narrower than the sample's
// 47; v7.6.2: 45.5 + a 0.5 gap -- half a pixel back, the field's call;
// v7.6.6: 45.75 + 0.25 -- the same 46 stride, the quarter pixel now in
// the GAP, the field's Windows-does-it guess), stacked from the right
// edge: close always; then maximize (if present); then minimize. With
// maximize off, minimize sits beside close -- the twinspeak geometry,
// minus the field's two pixels plus the half one.
struct CwButtonRects { RECT close, maximize, minimize; };
inline CwButtonRects cwButtonRects(int ww) {
    // v7.6.8 THE REFERENCE LAYOUT -- the field shipped the two maximized
    // titlebars side by side (the native one and ours) and the pixels
    // answered. Measured off the native shot at 1920 px wide: close
    // 1873..1917 (45), gap 1872, max 1827..1871 (45), gap 1826, min
    // 1781..1825 (45), inset 1918..1919 (2) -- buttons rows 0..20 on a
    // 23-row bar. The native maximized stack is 45/45/45 with REAL 1 px
    // gaps and the close ending 2 px inside the window edge.
    //   v7.6.6's reading ("+1 wide maximized, close -2") produced
    // 46/47/45 with no daylight -- every button one px wider than the
    // reference, the stack drifting leftward, the seams cut only while a
    // patch stood. The 45 + 1 gap split keeps the SAME 46 design px
    // stride the field's 45.75 + 0.25 metric rode (the stack total is
    // untouched) -- the extra pixel now lives in the GAP, where the
    // reference puts it, in BOTH states. Restored stays flush right
    // (close.right = ww); maximized keeps the v7.6.5 2 px inset.
    //   v7.6.8 THE RESTORED RAISE IS GONE -- the field: "on the normal
    // state (not maximized) I see that the titlebar buttons are raised
    // again, like I don't want (0px vs 1px and height reduced by 1px)."
    // The buttons run 0..g_barH in BOTH states -- no WIN32_FAKE_SHADOW
    // offset, no shortened band. The maximized band keeps its
    // v7.6.3 one-design-px floor (rows 0..bh-2; the hover's two px of
    // daylight land the band on the reference's 21 rows exactly).
    const bool  maxed  = g_host && cwIsMaximized(g_host);
    const float bwf      = cwSf(45.0f);
    const float gapf     = cwSf(1.0f);
    const int   bw       = (int)(bwf + 0.5f);                     // 45 at 96 dpi
    const int   gap      = gapf < 1.0f ? 1 : (int)(gapf + 0.5f);  // 1 at 96 dpi, never 0
    const int   closeR   = ww - (maxed ? cwS(2) : 0);             // v7.6.5: the maximized 2 px inset
    const int   bh       = maxed ? cwS(kBarHMaxDesign) : g_barH;
    RECT close = { 0, 0, closeR, bh };
    if (maxed) close.bottom -= cwS(1);    // v7.6.3: the maximized band ends one design px up
    close.left = close.right - bw;
    RECT maximize = close;
    maximize.right = close.left - gap;
    maximize.left  = maximize.right - bw;
    RECT minimize = maximize;
    minimize.right = maximize.left - gap;
    minimize.left  = minimize.right - bw;
    if (!g_btnMax) minimize = maximize;   // the empty slot collapses
    return { close, maximize, minimize };
}

// v7.5.3e: the seam between neighbouring hover patches, in REAL px -- the
// real DWM's half-a-design-px hairline snapped to whole physical pixels
// (never 0: a sub-pixel seam is what smeared the patches before). Taken off
// the LEFT neighbour's right edge, so the right-hand patch stays flush.
// v7.6.6: the hairline rides the NEW gap metric -- 0.25 design px (the
// button went 45.5 -> 45.75, the gap 0.5 -> 0.25). Still never 0: at every
// dpi here it clamps to the same 1 physical px as before.
inline int cwHovSeamPx() {
    const float s = cwSf(0.25f);
    return s < 1.0f ? 1 : (int)(s + 0.5f);
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
    // v7.6.3: the fill rides the EFFECTIVE bar height, and on a MAXIMIZED
    // window it starts at ROW 0 -- the border row does not exist there (the
    // sheet is withdrawn; cwSheetRowBytes lands the face byte on row 0 and
    // this fill paints the same face under it for the straight-to-screen
    // fallback).
    const bool maxed = g_host && cwIsMaximized(g_host);
    const int  bh    = maxed ? cwS(kBarHMaxDesign) : g_barH;
    const int  top   = maxed ? 0 : 1;
    const int face = active ? g_faceCol : (g_faceColIn < 0 ? g_faceCol : g_faceColIn);
    const int grad = active ? g_faceGrad : (g_faceGradIn < 0 ? g_faceGrad : g_faceGradIn);
    Gdiplus::LinearGradientBrush brush(
        Gdiplus::Rect(0, top, ww + 1, bh + 1 - top),
        Gdiplus::Color(cwChannel(face, 24), cwChannel(face, 16),
                       cwChannel(face, 8), cwChannel(face, 0)),
        Gdiplus::Color(cwChannel(grad, 24), cwChannel(grad, 16),
                       cwChannel(grad, 8), cwChannel(grad, 0)),
        Gdiplus::LinearGradientModeVertical);
    g.FillRectangle(&brush, -1, top, ww + 2, bh + 1 - top);
}

// FOREGROUND: icon, title text, caption buttons -- in that z-order (the
// buttons paint OVER the host's edge rows, exactly like the twinspeak
// render). Hover faces + glyphs; the close hover stays vanilla red.
inline void cwPaintBarForeground(Gdiplus::Graphics& g, HWND w, int ww, bool active) {
    if (!g_enabled) return;
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    const CwButtonRects br = cwButtonRects(ww);
    // v7.6.3: the icon and the title ride the EFFECTIVE bar height (the
    // button rects carry their own).
    const int bhEff = cwEffBarH();
    // v7.6.7: maxedW now drives the MAXIMIZED GLYPHS only (the 1px shrink
    // below) -- the hover band is state-independent since v7.6.7 (the band
    // owns row 0 in both states; the decompile split the two geometries).
    const bool maxedW = w && cwIsMaximized(w);
    const int item = active ? (g_glyph < 0 ? g_textCol : g_glyph) : g_textUn;
    // v7.5.4: the inactive glyphs are HALF there (task manager shows it) --
    // each button's glyph fades back to full opacity riding the same ramp
    // that drives its tint (pa = g_hovA[i]); focused windows pass through.
    // v7.6.1: a live lapse OWNS its glyph outright -- the flash floor (32)
    // easing back to the half-there rest, timed by the train's tick; the
    // ramp formula waits until the flash lands or the button re-hovers.
    auto glyphCol = [&](int base, int pa, int bi) -> int {
        int a;
        if (g_lapseOn[bi])    a = g_lapseA[bi];
        else if (!active)     a = kGlyphInactiveAlpha
                                  + ((255 - kGlyphInactiveAlpha) * pa + 127) / 255;
        else                  a = cwChannel(base, 24);
        return (a << 24) | (base & 0xFFFFFF);
    };
    auto fillARGB = [&g](int col, int x, int y, int w2, int h2) {
        Gdiplus::SolidBrush b(Gdiplus::Color(cwChannel(col, 24), cwChannel(col, 16),
                                             cwChannel(col, 8), cwChannel(col, 0)));
        g.FillRectangle(&b, x, y, w2, h2);
    };
    // v7.5.3e + f: the hover patch -- a translucent fill on WHOLE pixels. The
    // sub-pixel seams are gone: the 0.25 px anti-aliased insets smeared the
    // patch edges into a two-column gradient (the field: "blurred"). The
    // seam rides in whole physical pixels off the left neighbour's right
    // edge; the outer edges stay flush (close rides the window edge).
    // SmoothingMode stays None and every coordinate is an integer, so every
    // edge renders crisp.
    // v7.5.3f THE BAND: the patch ran ONE real px down from the top (the
    // button rects start on row 1, under the border row) and THREE real px
    // shorter than the bar: two px of daylight above the bar's bottom edge.
    // v7.6.5 THE HOVER AT 0PX -- the field: "I still don't see the titlebar
    // button hovers being at 0px." The real thing's highlight runs to the
    // VERY TOP of the frame -- the border row is covered the moment a
    // button lights. hoverBand pins the patch's top to ROW 0 (restored:
    // the sheet row is painted over; maximized: row 0 was already the
    // patch's first row) and keeps the two px of daylight at the bottom.
    // The byte-direct row writers composite the same bands over row 0
    // (cwSheetRowFill), so every path -- buffered blit, pre-flip commit,
    // fade train -- agrees with this one.
    // v7.6.6 asked the band to follow the field's "y = 1" restored button
    // request and dropped the RESTORED band to row 1 -- that read the two
    // geometries as one. The decompile splits them: the native BUTTONS sit
    // at y = margins.top + 1 (0x21bc0 -- the field's y = 1, kept in the
    // rects), while the native HOVER fills the whole button rect under the
    // group rect whose top is 0 (0x6a90) -- the highlight owns the border
    // row in BOTH states (the reference shot's red runs y 0..20).
    // v7.6.7 THE BAND OWNS ROW 0, BOTH STATES -- and the byte-direct
    // writers composite the same rule over the OPAQUE face row
    // (cwFaceRowBytes), never over the glass key, so the accent can never
    // blend into the patch. The glass key returns the moment no hover
    // alpha is standing (the idle border line, the native look).
    auto hoverBand = [](const RECT& r) {
        RECT h = r;
        h.top    = 0;                // v7.6.7: row 0 in both states
        h.bottom -= 2;
        return h;
    };
    auto fillPatch = [&](const RECT& r, int col, int insetR) {
        Gdiplus::SolidBrush b(Gdiplus::Color(cwChannel(col, 24), cwChannel(col, 16),
                                             cwChannel(col, 8), cwChannel(col, 0)));
        g.FillRectangle(&b, (INT)r.left, (INT)r.top,
                        (INT)((r.right - r.left) - insetR), (INT)(r.bottom - r.top));
    };
    // minimize
    if (g_btnMin) {
        // v7.6.8: the dash is the reference's own 10 design px in BOTH
        // states (the maximized shot's dash runs 1799..1808 -- v7.6.6's
        // "one px smaller" line drew 9 and sat a row high), and it centers
        // on the TRUE middle of its slot: (mw - lineW + 1) / 2 rounds the
        // half pixel INTO the button the way the reference does. The
        // v7.6.8 cy (the +1 rounding below) drops the maximized glyphs a
        // row to the reference's own rows (dash row 10, pair 5..14).
        const int lineW = cwS(10);
        const int mw    = br.minimize.right - br.minimize.left;
        const int dx    = br.minimize.left + (mw - lineW + 1) / 2;
        const int cy    = br.minimize.top + (br.minimize.bottom - br.minimize.top + 1) / 2
                        - kHovGlyphRide;   // v7.5.4: the glyph rides the band
        fillARGB(glyphCol(item, g_hovA[0], 0), dx, cy, lineW, 1);
    }
    // maximize (square outline; double square while maximized)
    if (g_btnMax) {
        // v7.6.8 cy: the +1 in the rounding centers on the band the way
        // the reference does -- the maximized glyphs land on the native
        // rows exactly (pair 5..14, dash row 10, X tips 5..14); the
        // restored glyphs keep the row they always sat on (the old rect
        // ran 1..g_barH, whose center this same rounding answers).
        const int cx = br.maximize.left
                     + (br.maximize.right - br.maximize.left) / 2;   // v7.6.2: its own width
        const int cy = br.maximize.top + (br.maximize.bottom - br.maximize.top + 1) / 2
                     - kHovGlyphRide;   // v7.5.4: the glyph rides the band
        const float penW = cwSf(1.25f) < 1.0f ? 1.0f : cwSf(1.25f);
        const int mi = glyphCol(item, g_hovA[1], 1);   // v7.5.4: dimmed while inactive
        Gdiplus::Pen p(Gdiplus::Color(cwChannel(mi, 24), cwChannel(mi, 16),
                                      cwChannel(mi, 8), cwChannel(mi, 0)), penW);
        if (w && cwIsMaximized(w)) {
            // v7.5.3d: the front sheet OCCLUDES the back one -- the real
            // restore glyph layers the squares, the back square's edges stop
            // at the front square instead of showing through its interior
            // (the maximized button used to read as see-through wireframe).
            // v7.6.5 ONE PX SMALLER -- the field: "doesn't the maximized
            // state's maximize button icon need to be 1px smaller?" Both
            // sheets run cwS(7) now (was cwS(8)), each keeping its top-left
            // anchor and the cwS(2) diagonal offset between them: the whole
            // icon draws 9x9 design inside the same slot (was 10x10), one
            // design px smaller on every side that counts. The restored
            // square keeps its 10 -- only the maximized icon shrinks.
            // v7.6.8 REVERTED TO cwS(8) -- the maximized reference shot
            // measures the restore pair at 8 px a sheet (back square top
            // edge row 5 x 1846..1853, front square top edge row 7 x
            // 1844..1851): the v7.6.5 shrink was the field's own guess, and
            // the reference out-answers it. With the v7.6.8 cy the pair
            // lands on the native rows EXACTLY (back 5..12, front 7..14).
            Gdiplus::Rect front(cx - cwS(5), cy - cwS(3), cwS(8), cwS(8));
            Gdiplus::Region frontRegion(front);
            g.SetClip(&frontRegion, Gdiplus::CombineModeExclude);
            g.DrawRectangle(&p, cx - cwS(3), cy - cwS(5), cwS(8), cwS(8));   // the back sheet, occluded
            g.ResetClip();
            g.DrawRectangle(&p, cx - cwS(5), cy - cwS(3), cwS(8), cwS(8));   // the front sheet, full
        } else {
            // v7.6.3 THE CRISP SQUARE: the pen path at half-pixel
            // coordinates grew a mitre-joint speck off the top-left corner
            // (GDI+ pen joins leak a sub-pixel wedge at a non-integer
            // corner -- the field's "a tiny corner off the top left"). The
            // iconic square is four INTEGER fills now -- a one-real-px
            // stroke, outer cwS(9)+1 -- the exact footprint the pen snapped
            // to at 96 dpi (outer 10, x from cx-5), no pen, no joint,
            // nothing hanging off any corner. Like the minimize line, the
            // stroke is the glyph color's own alpha (glyphCol owns it).
            const int outer = cwS(9) + 1;
            const int x0 = cx - outer / 2;
            const int y0 = cy - outer / 2;
            fillARGB(mi, x0, y0, outer, 1);                       // the top stroke
            fillARGB(mi, x0, y0 + outer - 1, outer, 1);           // the bottom stroke
            fillARGB(mi, x0, y0 + 1, 1, outer - 2);               // the left stroke
            fillARGB(mi, x0 + outer - 1, y0 + 1, 1, outer - 2);   // the right stroke
        }
    }
    // close (always; the close red -- g_closeHover since v7.6.2, the
    // setWinCloseBtnColor knob, default the real full red RGB(232,17,35) --
    // under the white X; both ride the same linear fades, 100 ms in / 150 ms
    // out focused, the inactive pair tempo-steered v7.6.2; the X stroke is
    // the real thing's 1 design px, the old 1.25 rendered half again too
    // thick)
    {
        const int pa = g_hovA[2];
        if (pa > 0) fillPatch(hoverBand(br.close), (pa << 24) | (int)(g_closeHover & 0xFFFFFF), 0);
        const int cx = br.close.left
                     + (br.close.right - br.close.left) / 2;   // v7.6.2: its own width
        const int cy = br.close.top + (br.close.bottom - br.close.top + 1) / 2
                     - kHovGlyphRide;   // v7.5.4: the glyph rides the band; v7.6.8: the +1 centers like the reference
        int xc = glyphCol(item, pa, 2);     // v7.5.4: half there while inactive, pa restores it
        if (pa > 0) {                       // the X whitens with the red (alpha rides glyphCol)
            const int a = cwChannel(xc, 24);
            const int r  = cwChannel(item, 16) + ((255 - cwChannel(item, 16)) * pa + 127) / 255;
            const int gr = cwChannel(item,  8) + ((255 - cwChannel(item,  8)) * pa + 127) / 255;
            const int b  = cwChannel(item,  0) + ((255 - cwChannel(item,  0)) * pa + 127) / 255;
            xc = (int)(((unsigned)a << 24) | (unsigned)(r << 16) | (unsigned)(gr << 8) | (unsigned)b);
        }
        Gdiplus::Pen xp(Gdiplus::Color(cwChannel(xc, 24), cwChannel(xc, 16),
                                       cwChannel(xc, 8), cwChannel(xc, 0)),
                        cwSf(1.0f) < 1.0f ? 1.0f : cwSf(1.0f));
        // v7.6.6: the maximized X runs one design px smaller -- half-px
        // arm reach (cwSf(4.5), was cwS(5)), the v7.5.x square's REAL
        // (float) overload trick; the restored X keeps its 10.
        const float xh = maxedW ? cwSf(4.5f) : (float)cwS(5);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.DrawLine(&xp, cx - xh, cy - xh, cx + xh, cy + xh);
        g.DrawLine(&xp, cx + xh, cy - xh, cx - xh, cy + xh);
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
                g.DrawImage(bm, Gdiplus::Rect(cwS(8), (bhEff - isz) / 2, isz, isz),
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
                                            (Gdiplus::REAL)(bhEff - cwS(7)));
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
    // v7.5.3d..f: the min/max hover tints land LAST -- they overlay
    // everything in the button (the glyph included), in the band: since
    // v7.6.5 the band starts at ROW 0 (the hover covers the border row,
    // like the real thing) and keeps the two real px of daylight at the
    // bottom. The seams against the right-hand neighbours are whole pixels
    // (the outer edges stay flush).
    {
        // v7.5.5: the tint follows the face BEING PAINTED -- the same face
        // expression cwPaintBarBackground answers, so the hover reads the
        // mode actually on screen: dark face -> the white overlay, light
        // face -> the measured black, kHovTintAlpha either way.
        const int faceNow = active ? g_faceCol
                                   : (g_faceColIn < 0 ? g_faceCol : g_faceColIn);
        const int tintCol = cwHslLightness(faceNow) < kHovDarkFaceL ? 0xFFFFFF : 0x000000;
        const int aMin = (g_hovA[0] * kHovTintAlpha + 127) / 255;
        const int aMax = (g_hovA[1] * kHovTintAlpha + 127) / 255;
        // v7.6.8: the seam insets are GONE -- the gaps are REAL now, they
        // live in the button rects themselves (cwButtonRects), so the
        // patches fill their rects flush and the 1 px of daylight between
        // neighbours shows the face exactly like the reference's own
        // button gaps. cwHovSeamPx() stays for hosts that run abutting
        // rects of their own.
        if (g_btnMin && aMin > 0) fillPatch(hoverBand(br.minimize), (aMin << 24) | tintCol, 0);
        if (g_btnMax && aMax > 0) fillPatch(hoverBand(br.maximize), (aMax << 24) | tintCol, 0);
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
    // v7.6.3: the MAXIMIZED window carries NO sheet -- the real thing's
    // maximized frame has no top border line at all. {0,0,0,0} while
    // maximized, {0,0,1,0} the moment it is not: the WM_SIZE maximize flip
    // re-runs this setup, so the sheet follows the window state for free
    // (and WM_DWMCOMPOSITIONCHANGED re-runs it maximize-aware too).
    const bool maxed = cwIsMaximized(w);
    const CwMargins sheet = maxed ? CwMargins{ 0, 0, 0, 0 }   // no glass row on a maximized window
                                  : CwMargins{ 0, 0, 1, 0 };  // row 0 becomes the DWM's own top border
    const bool on = SUCCEEDED(ext(w, &sheet));
    g_sheetHasGlass.store(on && !maxed, std::memory_order_release);
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

// v7.6.7: THE OPAQUE FACE ROW -- the maximized row-0 byte, factored out so
// every hover patch can base on it. Native receipt (uDWM.dll): the button
// hover is its own DirectComposition visual sitting ON TOP of the frame
// visuals -- when it lights, it REPLACES the border row, it never blends
// with the glass under it. So any pixel a hover patch owns must be
// delivered opaque, over the face, never over the alpha-0 glass key.
inline unsigned cwFaceRowBytes(bool active) {
    const int face = active ? g_faceCol
                            : (g_faceColIn < 0 ? g_faceCol : g_faceColIn);
    const int r = cwChannel(face, 16), g2 = cwChannel(face, 8), b = cwChannel(face, 0);
    // v7.6.8 THE BLUE LINE -- FOUND. This row byte landed as 0xAABBGGRR
    // (b<<16|r) while the 32bpp top-down DIB speaks 0xAARRGGBB (r<<16|b,
    // the same packing cwRGB above has always used). Every byte this path
    // wrote to row 0 had R and B SWAPPED. The gray (43,43,43) and the
    // glass key (0,0,0,0) are R/B-symmetric -- invisible. The near-white
    // face swap read (240,247,242) instead of (242,247,240) -- invisible.
    // The close hover red did not survive it: 232,17,35 delivered as
    // 35,17,232 -- the field's "blue line at the 0px row", captured live
    // in the field's own screenshot (row 0 = 35,17,232, rows 1..20 =
    // 232,17,35). v7.6.6's opaque-always and v7.6.7's face-base were both
    // real fixes of real problems -- over the WRONG bytes. Packed
    // 0xAARRGGBB now; the row agrees with the GDI+ rows under it.
    return 0xFF000000u | ((unsigned)r << 16)
         | ((unsigned)g2 << 8) | (unsigned)b;
}

inline unsigned cwSheetRowBytes(bool active) {
    // v7.6.3: MAXIMIZED -- the fake top border does not exist on the real
    // thing's maximized frame. The sheet is withdrawn ({0,0,0,0}, the
    // WM_SIZE flip re-runs cwSheetSetup) and row 0 is plain opaque FACE:
    // every writer answers through here, so no gray, no glass key and no
    // animation hold can ever paint a top border back onto a maximized
    // window. The byte is PREMULTIPLIED BGRA like its siblings (the face's
    // own alpha usually carries it at 255).
    if (g_host && cwIsMaximized(g_host)) {
        // v7.6.6: OPAQUE, ALWAYS -- the face's own alpha byte no longer
        // rides. A sub-255 face alpha made the row byte translucent, and
        // whatever the DWM keeps behind the client (the accent) blended
        // INTO the hover patch at row 0 -- the field's "the blue blend I
        // see at the 0px row that appears when I hover the close button."
        // The premultiply collapses with a = 255, so row 0 on a maximized
        // window is the plain opaque face byte and can never blend with
        // anything behind it again. (v7.6.7: the expression lives in
        // cwFaceRowBytes now -- the hover patch bases on it in BOTH
        // states, see cwSheetRowFill.)
        return cwFaceRowBytes(active);
    }
    // v7.5.3a: the gray shows when INACTIVE or while an animation holds the
    // row; focused & idle (and sheeted) the alpha-0 glass key lands and the
    // DWM's accent border shows through.
    if (active && !g_sheetRowAnim.load(std::memory_order_relaxed)
               && g_sheetHasGlass.load(std::memory_order_relaxed))
        return g_sheetRowFocusIdle;
    return active ? g_sheetRowActive : g_sheetRowInactive;
}

// v7.6.5: THE HOVER REACHES ROW 0 -- the byte-direct twin of the GDI+ patch.
// v7.5.3e composited the hover tint (and the close red) over row 0 so the
// highlight reached the window's top edge the way the real DWM's runs behind
// the border line; v7.5.3f pushed the highlight DOWN one real px (the field's
// "1px vertical") and the writers carried no patch state at all. The field,
// now: "I still don't see the titlebar button hovers being at 0px" -- the
// real thing's highlight covers the border row outright, so the compositing
// is BACK, rebuilt to match the v7.6.5 painter exactly: the same bands
// (hoverBand: top pinned to row 0, two real px of daylight at the bottom),
// the same colors (the white/black face tint for min/max, the red knob for
// close), the same alphas (kHovTintAlpha * the min/max ramps, the close's
// own ramp) and the same seams (cwHovSeamPx off the left neighbour's right
// edge). Every byte-direct writer goes through here -- the host's buffered
// blit (cwPatchSheetRow after the GDI+ pass), the pre-flip row commit and
// the fade train -- so row 0 agrees with rows 1+ on every path, hover or
// not. (The fade train reads the hover alphas from its own thread: plain
// int reads of GUI-thread state, the same shape as its g_host read -- a
// stale alpha costs one 20 ms tick of one row, never a crash.)
// Premultiplied-over of a patch (straight ARGB int) onto a row byte
// (premultiplied BGRA): out = patch*pa + base*(1-pa), alpha delivered the
// v7.1.29 way (the alpha IS the delivery -- a semi-transparent row over the
// focused glass key stays semi-transparent, exactly like the GDI+ fill).
inline unsigned cwRowPatchOver(unsigned base, int pa, int col) {
    if (pa <= 0) return base;
    if (pa > 255) pa = 255;
    const int ia = 255 - pa;
    const int pr = ((col >> 16) & 0xFF) * pa / 255;
    const int pg = ((col >>  8) & 0xFF) * pa / 255;
    const int pb = ( col        & 0xFF) * pa / 255;
    // v7.6.8: the base byte is 0xAARRGGBB (DIB order) -- R at bits 16..23,
    // B at bits 0..7. This compositor read it 0xAABBGGRR, so the patch
    // landed R/B-swapped on row 0 exactly when a hover was standing (the
    // red read blue -- the field's line). Same packing fix as
    // cwFaceRowBytes; premultiply unchanged.
    const int bA = (int)((base >> 24) & 0xFF), bR = (int)((base >> 16) & 0xFF);
    const int bG = (int)((base >>  8) & 0xFF), bB = (int)( base        & 0xFF);
    const int oA = pa + bA * ia / 255;
    const int oR = pr + bR * ia / 255;
    const int oG = pg + bG * ia / 255;
    const int oB = pb + bB * ia / 255;
    return ((unsigned)oA << 24) | ((unsigned)oR << 16)
         | ((unsigned)oG << 8)  | (unsigned)oB;
}
inline void cwSheetRowFill(unsigned* row, int cw2, bool active) {
    const unsigned px = cwSheetRowBytes(active);
    // fast path: no hover anywhere -- the plain sheet bytes, as always
    if (!g_hovA[0] && !g_hovA[1] && !g_hovA[2]) {
        for (int x = 0; x < cw2; ++x) row[x] = px;
        return;
    }
    // the bands, straight off the painter's geometry: the rects of THIS
    // width, the band top pinned to ROW 0 in BOTH states (v7.6.7 -- the
    // native group rect's top is 0 and the real thing's hover fills the
    // whole button rect: the reference shot's red runs y 0..20; v7.6.6's
    // restored row-1 band left the glass key shining above the red -- the
    // field's "when I hover the close button that blue line appears"),
    // the bottom two real px short.
    const CwButtonRects br = cwButtonRects(cw2);
    // v7.6.8: no seam cuts -- the gaps live in the rects (see cwButtonRects)
    const int aMin = (g_hovA[0] * kHovTintAlpha + 127) / 255;
    const int aMax = (g_hovA[1] * kHovTintAlpha + 127) / 255;
    const int aClose = g_hovA[2];
    const int faceNow = active ? g_faceCol
                               : (g_faceColIn < 0 ? g_faceCol : g_faceColIn);
    const int tintCol = cwHslLightness(faceNow) < kHovDarkFaceL ? 0xFFFFFF : 0x000000;
    const int closeCol = (int)(g_closeHover & 0xFFFFFF);
    // v7.6.7: BOTH STATES -- row 0 is inside the band whenever the band's
    // bottom is past it (it always is: the rects' own bottom runs to the
    // bar's floor).
    const bool rowInBand = g_host && (br.close.bottom - 2) > 0;
    // v7.6.7: THE PATCH BASE IS THE OPAQUE FACE ROW -- the native hover
    // visual replaces the border line, it does not blend with the glass
    // behind it. Over the alpha-0 glass key (restored, focused idle) a
    // patch delivered sub-255 alpha let the accent blend INTO the red --
    // the blue line. Over the opaque face the premultiply collapses
    // (oA = pa + 255*(255-pa)/255 = 255): the fade runs face -> red at a
    // constant opaque alpha, row 0 ends pure red at full hover, and the
    // glass key returns the moment the ramps rest at zero (the border
    // line comes back for the idle look, exactly like the native frame).
    const unsigned facePx = cwFaceRowBytes(active);
    for (int x = 0; x < cw2; ++x) {
        int pa = 0, col = 0;
        if (rowInBand) {
            if      (aClose > 0 && x >= br.close.left    && x < br.close.right)
                { pa = aClose; col = closeCol; }
            else if (aMax  > 0 && x >= br.maximize.left  && x < br.maximize.right)
                { pa = aMax;  col = tintCol;  }
            else if (aMin  > 0 && x >= br.minimize.left  && x < br.minimize.right)
                { pa = aMin;  col = tintCol;  }
        }
        row[x] = (pa > 0) ? cwRowPatchOver(facePx, pa, col) : px;
    }
}

// row 0 of a top-down 32bpp buffer (the host's bar DIB): the sheet byte,
// hover patches composited (v7.6.5: the hover reaches row 0)
inline void cwPatchSheetRow(void* bits32, int cw, bool active) {
    if (!bits32 || cw <= 0) return;
    cwSheetRowFill((unsigned*)bits32, cw, active);   // top-down: row 0 first
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
    unsigned* row = (unsigned*)g_rowBits;
    cwSheetRowFill(row, cr.right, active);         // sheet bytes + hover patches (v7.6.5: row 0 joins the hover)
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
        unsigned* row = (unsigned*)bits;
        cwSheetRowFill(row, cr.right, nc != 0);        // sheet bytes + hover patches (v7.6.5: row 0 joins the hover)
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
        cwHovLapseClearAll();         // v7.6.1: no flash crosses an activation change
        g_hovInactive = (wp == 0);    // v7.5.4: the inactive ramps (v7.6.1: 75 ms pre and post)
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
    case WM_SIZE: {
        // v7.6.3 THE MAXIMIZE FLIP -- the sheet AND the geometry. The real
        // thing's maximized frame has no top border line: the sheet is
        // withdrawn ({0,0,0,0} -- cwSheetSetup is maximize-aware now) and
        // row 0 renders plain face (cwSheetRowBytes answers the face for a
        // maximized host); on unmaximizing the {0,0,1,0} sheet comes back
        // and the top bar with it. The caption rides cwEffBarH() (24 design
        // px maximized, the buttons one design px shorter, the close two
        // narrower), so the flip re-seats the tracked children and re-lands
        // the bar. Iconic transitions are skipped -- a minimize from a
        // maximized window must not flip the latch (the placement reads
        // SW_SHOWMINIMIZED mid-flight); the restore returns to the same
        // state and nothing moves. The message itself keeps flowing to the
        // host and DefWindowProc.
        if (!IsIconic(w)) {
            const bool maxed = cwIsMaximized(w);
            if (maxed != g_wasMax) {
                g_wasMax = maxed;
                cwSheetSetup(w);          // {0,0,0,0} maximized / {0,0,1,0} restored
                cwRelayoutChildren();     // the children ride the bar height
                if (IsWindowVisible(w)) {
                    // v7.6.4 THE WHOLE-FRAME FLIP REPAINT -- the field's
                    // "have it so maximizing ... doesn't corrupt the window
                    // render for a split second." The flip used to re-land
                    // the BAR alone (cwRepaint), so every band of the panel
                    // below -- just moved by the 9 design px the bar gave
                    // up -- kept its PRE-flip pixels until the queue's async
                    // WM_PAINT wandered by: one to two composes of stale,
                    // misplaced content on every maximize/unmaximize. One
                    // synchronous full-window repaint (RDW_UPDATENOW) lands
                    // the buffered frame at the new geometry in a single
                    // write BEFORE the flip's compose can sample anything
                    // stale; the bar re-lands with it and the sheet train
                    // re-kicks after.
                    RECT cr; GetClientRect(w, &cr);
                    RedrawWindow(w, &cr, nullptr,
                                 RDW_INVALIDATE | RDW_ERASE
                                 | RDW_ALLCHILDREN | RDW_UPDATENOW);
                    cwRepaint();          // the bar re-lands with it (belt)
                    cwSheetRowKick();
                }
            }
        }
        return false; }
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
        if (cursor_point.y < cwEffBarH()) { *out = HTCAPTION; return true; }
        *out = HTCLIENT; return true; }
    case WM_NCMOUSEMOVE: {
        // which button the mouse is over -- hover state lives here; the
        // host's repaint callback lands the pixels synchronously
        POINT cursor_point;
        GetCursorPos(&cursor_point);
        ScreenToClient(w, &cursor_point);
        // v7.6.2: the tempo sample -- instantaneous (px/ms)^2, EMA 60/40.
        // A pause longer than 200 ms reads as stillness (the next flash
        // runs at the floor); a 2 px/ms flick pins the ceiling.
        const DWORD mvNow = GetTickCount();
        if (g_hovMoveLast && mvNow > g_hovMoveLast) {
            const DWORD mdt = mvNow - g_hovMoveLast;
            if (mdt >= 200) g_hovSpeed2 = 0.0f;
            else {
                const float dx = (float)(cursor_point.x - g_hovMovePt.x);
                const float dy = (float)(cursor_point.y - g_hovMovePt.y);
                const float inst2 = (dx * dx + dy * dy) / ((float)mdt * (float)mdt);
                g_hovSpeed2 = g_hovSpeed2 * 0.6f + inst2 * 0.4f;
            }
        }
        g_hovMoveLast = mvNow;
        g_hovMovePt   = cursor_point;
        RECT cr; GetClientRect(w, &cr);
        const CwButtonRects br = cwButtonRects(cr.right);
        int nh = cwHovNone;
        if (PtInRect(&br.close, cursor_point)) nh = cwHovClose;
        else if (g_btnMax && PtInRect(&br.maximize, cursor_point)) nh = cwHovMax;
        else if (g_btnMin && PtInRect(&br.minimize, cursor_point)) nh = cwHovMin;
        if (nh != g_hover) {
            const int was = g_hover;
            g_hover = nh;
            // v7.6.1: leaving ANY button on an INACTIVE window sets that
            // button's glyph to 12.5% for its fade-back -- moving onto
            // a neighbouring button unhovered the first one all the same.
            // v7.6.2: the leave's tempo steers the out ramp AND the flash
            // (cwHovTempoMs, inside the bracket -- v7.6.4: [100, 300]); the
            // arrival's tempo steers the IN ramp, and re-hover cancels
            // that button's flash.
            if (was != cwHovNone && g_hovInactive) {
                kHovInactiveOutMs = cwHovTempoMs();
                cwHovLapse(was - 1);
            }
            if (nh != cwHovNone) {
                g_lapseOn[nh - 1] = false;
                if (g_hovInactive) kHovInactiveInMs = cwHovTempoMs();
            }
            cwHovAnimEnsure();             // the fade owns the repaints
        }
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, w, 0 };
        TrackMouseEvent(&tme);          // the leave that starts the tempo-steered fade-out
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
        if (g_hover != cwHovNone) {
            const int was = g_hover;
            g_hover = cwHovNone;
            if (g_hovInactive) {
                kHovInactiveOutMs = cwHovTempoMs();      // v7.6.2: the leave's tempo
                cwHovLapse(was - 1);                      // v7.6.1: every button lapses
            }
            cwHovAnimEnsure();                        // the fade owns the repaints
        }
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
            if (IsWindowVisible(w) && !IsIconic(w)) {
                // v7.6.4 THE WHOLE-FRAME DPI REPAINT -- the field's "have it
                // so ... DPI reaction doesn't corrupt the window render for
                // a split second." The reaction used to re-land the BAR
                // alone (cwRepaint) and leave the host's own full repaint
                // ASYNC (RDW_INVALIDATE without RDW_UPDATENOW): between the
                // suggested-rect SetWindowPos and the queue's paint, the DWM
                // could compose the resized window with the OLD scale's
                // pixels -- the split-second garbage on every scale change.
                // One synchronous full-window repaint at the new scale,
                // AFTER the hook re-derived the constants and the children
                // are re-seated, lands the whole frame in a single write;
                // the bar re-lands with it and the sheet train re-kicks.
                RECT cr; GetClientRect(w, &cr);
                RedrawWindow(w, &cr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE
                             | RDW_ALLCHILDREN | RDW_UPDATENOW);
                cwRepaint();              // the bar re-lands with it (belt)
                cwSheetRowKick();
            }
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
            // v7.5.4 THE RELEASE PRESENT DROPS THE HOVER -- the strip's half
            // of the recipe. The field: "do the same type of thing as the
            // window border's top bar where the moment you release the
            // minimize button and have pressed it the titlebar's buttons go
            // half-transparent for that period." The release IS this
            // message: the WM_NCLBUTTONUP dispatch posts it (the caption
            // button since v7.5.3a), and the taskbar, the system menu and
            // the keyboard land here too. At this instant the cursor is
            // still ON the button -- the hover is armed and its ramp sits
            // at 255 -- so the untouched present would land that glyph FULL
            // (glyphCol lifts it, the tint band on it), and nothing could
            // ever correct the flight: the shrink shows whatever THIS
            // present lands (the iconic freeze), and the hover fade-out the
            // WM_NCACTIVATE clear starts dies on the same freeze. Zeroed
            // before the repaint, every glyph lands the inactive half
            // (glyphCol at pa = 0 = kGlyphInactiveAlpha), the band and the
            // red go with it, and the ride down shows the settled inactive
            // caption -- the exact frames the top bar's gray has ridden
            // since v7.5.3, now with the strip on it. Every OTHER
            // deactivation keeps its animated fade-out (WM_NCACTIVATE's
            // clear, the 75 ms inactive ramps); only the minimize handoff
            // drops instantly, because only this present is the last paint
            // the animation will ever sample. The restore's activation
            // lands the active face and the full strip as before.
            g_hover = cwHovNone;
            g_hovA[0] = g_hovA[1] = g_hovA[2] = 0;
            cwHovLapseClearAll();          // v7.6.1: the flight paints the settled caption, no flash
            cwSheetRowAnimHold(true);
            cwSheetRowGate(true);
            cwSheetRowLatch(0);
            cwDwmFlush();
            cwCommitSheetRowFast(w, false);
            cwRepaint();                   // the inactive face + the half strip, through the latch answer
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
        if (g_hover != cwHovNone) {
            const int was = g_hover;
            g_hover = cwHovNone;
            if (g_hovInactive) {
                kHovInactiveOutMs = cwHovTempoMs();      // v7.6.2: the leave's tempo
                cwHovLapse(was - 1);                      // v7.6.1: every button lapses
            }
            cwHovAnimEnsure();                        // the fade owns the repaints
        }
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
// v7.5.5: the inactive face, the field's call -- the real Windows 10 dark
// mode turns the bar GRAY on inactive and keeps it black on focused, and
// that needs a distinct inactive face. One argument: solid (a gradient
// between equal colors is exact). Two: the inactive gradient. -1 restores
// the default: mirror the focused color (cwPaintBarBackground's rule).
inline void setWinInactiveColor(int col)                         { cwd::g_faceColIn = col; cwd::g_faceGradIn = col; cwd::cwRepaint(); }
inline void setWinInactiveColor(int col, int gradColor)          { cwd::g_faceColIn = col; cwd::g_faceGradIn = gradColor; cwd::cwRepaint(); }
// v7.6.2: the close button's hover red is a knob -- the field's call. One
// ARGB int (0xAARRGGBB); the alpha channel is ignored (the hover ramp owns
// the fade, the color rides it). Default: the real thing's full red,
// RGB(232,17,35) -- the old fixed 0xCC0000 was a step dark.
inline void setWinCloseBtnColor(int col)                         { cwd::g_closeHover = (DWORD)col; cwd::cwRepaint(); }

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
// retired -- the hover is the fixed 10% tint now (the real thing's look is
// not a color knob); v7.5.5: the tint's channel follows the face's HSL
// lightness (white on a dark face, black on a light one) -- still no knob.

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
// v7.6.3: the EFFECTIVE caption height -- 24 design px while the window is
// maximized, cwBarHeight() otherwise. A host adopts this for its own bar
// strip (buffer height, blit rows, the bottom edge, the content offsets)
// so the maximized style lands whole.
inline int  cwBarHeightNow()                        { return cwd::cwEffBarH(); }
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
inline void setWinInactiveColor(int)                                   {}
inline void setWinInactiveColor(int, int)                              {}
inline void setWinCloseBtnColor(int)                                   {}
inline void setWinTitleTextColor(int, int, int, int)                   {}
inline void setWinTitleTextColor(int, int, int)                        {}
inline void setWinTitleTextColor(int, int)                             {}
inline void changeWinTextFont(const std::string& = "Segoe UI")         {}
inline void enableWinIcon(bool)                                        {}
inline void setWinTitlebarButtonLayout(bool, bool)                     {}
inline void setCanWinResize(bool)                                      {}
inline void putWindowinTray(bool)                                      {}
inline int  cwBarHeight()                                              { return 33; }
inline int  cwBarHeightNow()                                           { return 33; }
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
