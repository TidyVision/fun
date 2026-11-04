// customWindow.h -- THE ONE-FILE WINDOW (v7.7.12)
// =====================================================================
// A single-header custom window-chrome API for Windows 10/11. v7.7.12 is
// the field's transition round, all inside this file: THE SINGLE-PRESENT
// FLIP (the maximize/restore transition lands ONE synchronous whole-frame
// RedrawWindow after the carrier and the children move -- the trailing
// second queued repaint of v7.7.9..v7.7.11 is retired, so no intermediate
// frame can reach the compositor mid-animation), THE DPI CARRIER RE-LAND
// (cwDpiChanged re-runs cwSheetSetup: the extend carrier is a geometry
// commitment measured in REAL px -- at the old scale's height after a DPI
// change it left the band's glass sheet wrong for the window's whole life
// at the new scale -- the top-bar corruption the field filmed), and THE
// ALWAYS-HALF INACTIVE BORDER (the inactive sheet line is BGRA(43,43,43,
// 128) in every registry weather -- the AccentColorInactive override is
// retired; the field's "always at a half-transparent 43,43,43"). The
// v7.7.9..v7.7.11 consolidation stands unchanged beneath: THE WHOLE
// uDWM-LAW CHROME LIVES HERE NOW -- the caption band, the caption-button
// rects (the 0x21bc0 maximized law), the hover/press glow WITH THE REAL
// uDWM TIMELINE, the glyphs, the uDWM title, the icon, the sheet line
// with the REGISTRY border colors, the hit tests, the NC mouse contract,
// the HSL dial and the extend carrier. The host (twinspeak) creates the
// window, calls cwAttach, forwards its messages through cwWndProc FIRST
// and paints the two chrome halves inside its own buffered frame. That is
// the reason this API exists.
//
// REFERENCE BINARIES: uDWM.dll decides, dwmcore.dll draws, and -- the
// field's standing order since this round -- uxtheme.dll is the reference
// for the IN-CLIENT UI: its GetThemeSysSize (0x50840 + the scale helper
// 0x4abfc + the DPI source 0x135e8) is the scaling law the host's S()
// already matches byte-for-byte: scaled = MulDiv(value, processDpi, 96),
// processDpi = IsProcessDPIAware() ? GetDeviceCaps(GetDC(nullptr),
// LOGPIXELSX) : 96. The WM_DPICHANGED handler re-derives the scale FIRST
// and moves the window SECOND, so every geometry decision inside the
// resize (the NCCALCSIZE insets, the erase, the child seats) answers at
// the NEW scale -- the uxtheme MulDiv applied at the moment the resize
// asks for it.
//
// THE LAWS THIS FILE IMPLEMENTS (evidence: uDWM.dll, PDB-backed symbols):
//
//  THE BUTTON RECTS -- CTopLevelWindow::UpdateNCAreaPositionsAndSizes
//  (0x21bc0) + UpdateNCAreaButton (0x222a8): glyph = round(SM_CYSIZE *
//  21/22), stride = round(glyph * 48/22), restored close = round(glyph *
//  49/22). RESTORED: the contiguous flush-right stack one px inside the
//  right edge and one px down (the offset dial's 1). MAXIMIZED: the SAME
//  48/22 stride for all three, each fill = stride - 0.5 (the half-px
//  tails are REAL pixels -- the tail column renders at half the patch
//  alpha), 0.5 px seams, the close's right edge 1.5 px inside, top 0,
//  height one px short (bh = SM_CYCAPTION + SM_CYFRAME + SM_CXPADDEDBORDER).
//
//  THE HOVER TIMELINE -- CButton::ActivateTimeline (0x77b4) feeds
//  CTimelineBase the double at .rdata CC920 = 0x3FC47AE140000000 =
//  0.16 SECONDS, and CButton::ComputeFadeValues (0x7b10) rides it:
//      incoming alpha = start + 2P(end-start), clamped at end   (2x ramp,
//          full at HALF the timeline -- 80 ms)
//      outgoing alpha = (end - P) * C                           (1x ramp,
//          gone at the full 160 ms)
//  RedrawVisual (0x22648) chains consecutive fades by swapping the two
//  crossfade images with no pop. This file runs the same law per button:
//  a 16 ms timer, a hover channel and a press channel, each with its own
//  (t0, from) snapshot at every state change. The v7.5.x tempo brackets,
//  the inactive lapse and the linear 100/150 ramps are RETIRED -- the
//  decompile out-answers them.
//
//  THE INACTIVE LAW -- UpdateButtonVisuals (0x25364) picks the button
//  visuals' target opacity with TreatAsActiveWindow: 1.0 active, 0.4
//  inactive (the float pair at .rdata CFA0C/CFA18, read byte-exact):
//  the ink dials 254/102. v7.7.11: the HOVER engages on an inactive
//  window too -- the same 160 ms timeline, the same 2x-in/1x-out
//  curves, riding to the 0.4 target (the field's "different rates" is
//  the 0.4 target on the shared clock: the strength-per-second is 40%
//  of the active ramp in, and the 1x ramp out takes the full 160 ms
//  from a smaller peak). An activation flip RETARGETS the running
//  channels (the from-snapshot re-taken -- the RedrawVisual crossfade
//  law) instead of killing them: hover crosses 1.0 -> 0.4 smoothly when
//  the window loses focus and 0.4 -> 1.0 when it regains it, exactly
//  the SetVisualStates re-run the native does on the flip.
//
//  THE ICON -- CTopLevelWindow::UpdateIcon (0x3cfd8) + UpdateWindowVisuals
//  (0x23ad0): the caption icon is a CImage visual at window+0x210, created
//  (0x2435e) and pinned by VisualCollection::InsertRelative; v7.7.12 THE
//  ICON FADE VERDICT, found and closed: uDWM.dll carries NO titlebar icon
//  fade on the inactive state. CImage::SetOpacity (0x8ac00) exists but has
//  ZERO call sites in the whole .text; CVisual::SetOpacity (0x20a90)'s 47
//  call sites are the layered-window alpha (CWindowList::AlphaChange
//  0x112f0 byte/255), show/hide (0x1a20c), iconic thumbnails and the touch
//  visuals -- none targets the icon (and CAtlasedImage::SetOpacity
//  0x225c8 is ridden by the button crossfade pair and the shadow at
//  every PDB-named site); the activation flip (CWindowList::
//  ActivationChange 0x288e0 -> sub_289e8) flags the window data and re-runs
//  UpdateWindowVisuals, and that function touches the icon ONLY for its
//  hit-test override (CImage+0xc0 = 0/3 at 0x241e5 -- CVisual's ctor dials
//  the same field to -2, DoHitTest compares it to -1; it is NOT a draw
//  state). The dimming the eye reads on an inactive caption rides the
//  FACE BENEATH the icon: the glass blend weight per state (.rdata CCD90
//  = 0.2 active / CCDC0 = 0.6 inactive) re-blends the whole band, the
//  TITLE re-inks through BlendColors at the CFA18 0.4 weight and the
//  BUTTON glyphs dial to the 0.4 target -- the icon bitmap itself renders
//  at FULL opacity in both states, and this file draws it the same way
//  (cwPaintCaptionIcon: no fade, no dim -- 100% accurate to uDWM.dll).
//  dwmcore.dll and uxtheme.dll were scanned the same round: the
//  compositor stages no chroma of its own, and uxtheme's DrawThemeIcon
//  (0x4f2c0) is a forwarder into the caller-driven image-list draw with
//  no inactive machinery.

//  THE BORDER, REGISTRY EDITION -- uDWM hardcodes no registry names; it
//  reads uxtheme's GetUserColorPreference/GetColorFromPreference, whose
//  store (the strings inside uxtheme.dll) is:
//      HKCU\Software\Microsoft\Windows\DWM : ColorPrevalence (DWORD),
//          AccentColor (DWORD, 0xAABBGGRR -- default blue 0xffd77800),
//          AccentColorInactive (DWORD, same layout, optional),
//          ColorizationColor (DWORD, 0xAARRGGBB, the fallback).
//  DWM itself paints the 1px left/right/bottom strips the NCCALCSIZE
//  recipe hands it (v7.7.8's uDWM edition, unchanged). The house's TOP
//  sheet line (row 0) now mirrors the same registry law: ColorPrevalence
//  on -> the accent at full strength; off -> the native idle line (white
//  at the sheet's 0.2 -- the native 62-over-0e0808 measure). The line is
//  GONE while maximized (the native maximized clip runs the border off).
//  v7.7.12 THE ALWAYS-HALF INACTIVE BORDER: on the inactive state the
//  sheet line is BGRA(43,43,43,128) in EVERY registry weather -- the
//  uDWM state table's inactive entry (.rdata CD7E4 = 0xFF2B2B2B, the
//  43,43,43 the field called) at the sheet's own half alpha, and the
//  v7.7.11 AccentColorInactive override is RETIRED (the field: "the
//  inactive window's border color is always at a half-transparent
//  43,43,43"). The flip lands on WM_NCACTIVATE/WM_ACTIVATE AND at the
//  minimize moment (SC_MINIMIZE + WM_SIZE SIZE_MINIMIZED): the frame
//  presented as the window goes iconic -- and the taskbar preview after
//  it -- carries the inactive line, the inactive face and the retargeted
//  channels. dwmcore.dll was re-scanned for the field (3,589,632 bytes,
//  every 2B 2B 2B byte run, every 0x80-alpha DWORD, the 43/255 float):
//  it hardcodes NO chroma at all -- it renders what uDWM stages; the
//  inactive color law lives in uDWM.dll's own tables.
//
//  THE TOP AND BOTTOM -- the extend carrier covers the WHOLE caption band
//  ({0,0,cwEffBarH(),0}): every band row composites through the same
//  alpha path, from row 0 (the sheet line) to the band's LAST row. The
//  face seat fills rows 0..cwEffBarH()-1 at alpha 254 (the 254 law --
//  GDI+'s opaque fast path writes alpha 0, which composites as NOTHING
//  inside the extend band). MAXIMIZED the caption is ONE PHYSICAL PX
//  SHORTER (the border row vanishes): cwEffBarH() = g_barH - 1, and every
//  tracked child rides the same delta in cwRelayoutChildren -- the host's
//  tab bar and content land exactly one row higher, the native way.
//
//  THE DIAL -- the titlebar color's HSL lightness (L = (max+min)/2, NOT
//  luma) > 128 -> light mode, <= 128 -> dark mode, programmed as
//  DWMWA_USE_IMMERSIVE_DARK_MODE (attribute 20, fallback 19). It re-runs
//  on every changeWinTitlebarColor -- the startup color-change case
//  included: the band, the ink, the attribute, the registry border and
//  the host's full-tree repaint all move in ONE transaction.
//
//  THE TITLE -- the system NONCLIENTMETRICS lfCaptionFont, scaled by the
//  window's DPI ratio (CWindowData::GetNonClientCaptionFont 0x3dff8;
//  there is no per-window font API in uDWM.dll), drawn vertically
//  centered right of the icon slot, ellipsis-clamped before the buttons,
//  ink = the dial's pair (white on dark, black on light), dimmed toward
//  the face while the window is inactive. The icon is the window class
//  icon (WM_GETICON / GCLP_HICONSM / GCLP_HICON), DrawIconEx AFTER the
//  GDI+ scope dies.
//
//  THE HIT CODES -- the buttons answer HTMINBUTTON / HTMAXBUTTON /
//  HTCLOSE (20) over the exact float rects (float contains(): the half
//  columns hit, no dead pixel), BEFORE every resize edge (the native
//  order). The band mouse arrives as NC traffic; the house runs the
//  native press-release-cancel contract itself (press on down, release
//  over the same rect fires, drag-off cancels, WM_CANCELMODE and the
//  off-window release sweep un-latch). The caption right-click system
//  menu is retired with WS_SYSMENU (v7.7.6 -- the button-group gate).
//
// THE ELEVEN PUBLIC FUNCTIONS (the API -- unchanged in shape):
//     enableCustomWin(bool)                     the on/off switch
//     changeWinTitlebarColor(col) / (col,grad)  the caption face (ARGB)
//     setWinInactiveColor(col) / (col,grad)     the inactive face (-1 mirrors)
//     setWinTitleTextColor(color, grad, inact, inactGrad)  the title ink
//     changeWinTextFont(font)                   the legacy seed (the uDWM
//                                               caption font owns the title
//                                               by default since v7.7.6)
//     enableWinIcon(bool)                       the class icon at the left
//     setWinTitlebarButtonLayout(min, max)      which buttons exist
//     setWinCloseBtnColor(col)                  the close hover red
//     setCanWinResize(bool)                     the honest border
//     putWindowinTray(bool)                     the tray gesture
//
// Host plumbing (a window that already runs its own machinery adopts the
// chrome without behavior change through these; a fresh consumer needs
// none of them):
//     cwFrameStyle()               the window style the chrome expects
//     cwIsMaximized(HWND)          the maximized test (WPL)
//     cwAttach(HWND, void(*)(HWND))   the chrome window + the repaint fn
//     cwSetBarHeight(int) / cwBarHeight() / cwBarHeightNow()
//                                  the caption contract (cwBarHeightNow
//                                  answers the maximized-short height)
//     cwTrackChild / cwRelayoutChildren / cwSetDpiHook
//                                  the WM_DPICHANGED story (and the
//                                  maximized caption delta, v7.7.9)
//     cwSeedTitleFont(face, px, weight)   the legacy seed behind
//                                  changeWinTextFont
//     cwSeedChrome(...)            per-theme seed of every chrome color
//     cwSetTrayHost(hideFn, showFn)   putWindowinTray delegates to these
//     cwPaintBarBackground(g, ww, active) / cwPaintBarForeground(g, [dc,]
//                                  w, ww, active) / cwPaintChrome(...)
//                                  the two chrome halves (HDC variant:
//                                  pass the host's own buffer DC)
//     cwPaintCaptionIcon(dc, ww)   the icon pass (GDI, after the GDI+ scope)
//     cwWndProc(w, msg, wp, lp, &result)  FORWARD FIRST in the host wndproc
//     cwDpi(HWND-less): cwDpi()/cwDpiChanged(dpi)  the host's DPI story
//
// Host contract:
//   * Create the window with cwFrameStyle() (or twinspeak's
//     WS_OVERLAPPED|WS_CAPTION|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|
//     WS_CLIPCHILDREN -- WS_SYSMENU stays OUT, the button-group gate).
//   * Call cwAttach after creation (it lands the extend carrier, the
//     dial's dark-mode attribute, the square-corner + NC-render pins,
//     the registry border read and the uDWM caption font).
//   * The wndproc calls cwWndProc FIRST; unhandled messages keep flowing
//     to the host's own cases and DefWindowProc.
//   * Paint the chrome halves inside the host's own frame render
//     (background, then foreground), run cwPaintCaptionIcon after the
//     GDI+ scope, and shift the content viewport by cwBarHeightNow().
//   * GDI+ started before the first paint; the class carries the icon.
//   * The repaint callback is invoked on every state change (hover,
//     theme, activation, layout); it should repaint the band
//     synchronously -- or at least invalidate it.
//
// Targets: Windows (the implementation). Every other target compiles the
// stub block at the bottom -- the functions exist and do nothing, so host
// code writes unchanged on both sides of #ifdef _WIN32.
// Libraries (Windows): user32 gdi32 shell32 gdiplus advapi32 (-lole32 if
// COM is used by the host itself).
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

// the tray hook in cwWndProc dispatches through the public API
inline void putWindowinTray(bool hide);

namespace cwd {

// ---- the chrome state (ARGB ints, 0xAARRGGBB) ------------------------------
inline bool g_enabled     = true;
inline int  g_faceCol     = 0xFF181B1F;   // titlebar, focused (solid or gradient top)
inline int  g_faceGrad    = 0xFF090A0C;   // titlebar gradient bottom
inline int  g_faceColIn   = -1;           // titlebar, unfocused (-1 = mirror focused)
inline int  g_faceGradIn  = -1;
inline int  g_textCol     = 0xFFA0C8CD;   // legacy title ink seed (the uDWM dial owns
inline int  g_textGrad    = 0xFF6C868C;   //  the live title since v7.7.6; the seed
inline int  g_textUn      = 0xFF7E888E;   //  stays for the changeWinTextFont consumers)
inline int  g_textUnGrad  = -1;
inline int  g_glyph       = -1;
inline DWORD g_closeHover = 0xFFE81123;   // the close hover red (setWinCloseBtnColor)
inline std::wstring g_fontFace = L"Segoe UI";
inline int  g_fontPx      = 14;
inline int  g_fontWeight  = FW_SEMIBOLD;
inline bool g_icon        = false;
inline bool g_btnMin      = true;
inline bool g_btnMax      = true;         // v7.7.9: the uDWM stack is three wide; the
                                          // layout knob still gates the middle button
inline bool g_resizable   = true;         // v7.7.9: the uDWM window resizes; the honest
                                          // border (setCanWinResize(false)) can still refuse
// the uDWM caption font's dirty latch (re-derived on cwDpiChanged)
inline bool g_titleFontDirty = true;
// the built-in fallback tray (only when no tray host is registered)
inline NOTIFYICONDATAW g_nid   = {};
inline bool g_trayUp           = false;
inline UINT g_trayMsg          = 0;          // WM_APP+n, registered on first use
inline UINT g_taskbarCreated   = 0;

// ---- the caption geometry ---------------------------------------------------
inline HWND   g_host      = nullptr;      // the chrome window
inline void (*g_repaint)(HWND) = nullptr; // the host's repaint callback
inline void (*g_trayHide)(HWND) = nullptr;
inline void (*g_trayShow)(HWND) = nullptr;
inline void (*g_dpiHook)() = nullptr;
// v7.5.3d: the caption height -- 33 design px (the house-measured DWM caption),
// scaled by the header's DPI and re-derived on cwDpiChanged.
inline constexpr int kBarHDesign = 33;
inline int    g_barH      = 33;           // REAL px
inline bool   g_wasMax    = false;        // the WM_SIZE maximize-flip latch
// v7.7.9 THE MAXIMIZED CAPTION -- one physical px shorter than restored: the
// border row vanishes with the visible top border (the native maximized clip),
// the buttons sit at top 0 and lose their one px, and EVERYTHING below -- the
// face seat, the sheet line, the extend carrier, the tracked children, the
// host's content viewport -- rides cwEffBarH() so the whole window moves the
// native one row. (The old 24-design-px maximized caption is retired; the
// decompile's maximized law and the field's own captures out-answer it.)
inline bool cwIsMaximized(HWND w);              // defined with the small helpers
inline int cwEffBarH() {
    return (g_host && IsWindow(g_host) && cwIsMaximized(g_host)) ? g_barH - 1 : g_barH;
}
inline int cwBarHeightNow() { return cwEffBarH(); }
inline void cwRepaint() {
    if (g_repaint && g_host) { g_repaint(g_host); return; }
    if (g_host) {
        RECT r; if (GetClientRect(g_host, &r)) {
            r.bottom = cwEffBarH(); InvalidateRect(g_host, &r, FALSE);
        }
    }
}

// ---- small helpers ----------------------------------------------------------
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
// whole geometry derives from this (dpi, bar height, the caption font), so one
// call re-derives everything; the host re-scales and then cwRelayoutChildren()
// moves the tracked children. Registered nowhere -- the HOST's WM_DPICHANGED
// case calls this (twinspeak does), then runs its own hook chain.
// v7.7.12 THE DPI CARRIER RE-LAND: the extend carrier ({0,0,cwEffBarH(),0})
// is a geometry commitment measured in REAL px -- re-derived g_barH with the
// old-scale carrier still landed leaves the band's glass sheet at the WRONG
// height for the window's whole life at the new scale (the top-bar corruption
// the field filmed on every DPI change). The re-derivation re-runs
// cwSheetSetup now: the carrier, the registry border read and the dial's
// attribute re-land in the same transaction, before the host's SetWindowPos
// can show a single frame at the new scale. Pre-attach calls (the boot seed)
// skip the carrier -- there is no window yet; cwAttach lands it.
inline bool cwSheetSetup(HWND w);                 // defined with the DWMAPI seam
inline void cwDpiChanged(UINT dpi) {
    if (dpi && dpi != g_dpi) {
        g_dpi = dpi;
        g_barH = MulDiv(kBarHDesign, (int)dpi, 96);
        g_titleFontDirty = true;          // the caption font rides the scale
        if (g_host && IsWindow(g_host)) {
            cwSheetSetup(g_host);         // the carrier rides the new bar height
            cwRepaint();
        }
    }
}
// the uDWM caption font state (declared here, derived in cwEnsureTitleFont)
inline HFONT    g_titleFont = nullptr;
inline LOGFONTW g_titleLf   = {};
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
inline int cwS(int v) { return MulDiv(v, (int)cwd::cwDpi(), 96); }        // design px -> real px
inline float cwSf(float v) { return v * (float)cwd::cwDpi() / 96.0f; }
inline DWORD cwArgbOf(COLORREF c) {
    return 0xFF000000u | ((DWORD)GetRValue(c) << 16)
                       | ((DWORD)GetGValue(c) << 8) | (DWORD)GetBValue(c);
}
inline int cwChannel(int argb, int shift) { return (argb >> shift) & 0xFF; }
// v7.5.5: HSL lightness of an ARGB color, 0..255 -- L = (max + min) / 2. The
// dial reads this (NOT luma): L > 128 -> light mode, <= 128 -> dark mode.
inline int cwHslLightness(int argb) {
    const int r = cwChannel(argb, 16), g2 = cwChannel(argb, 8), b = cwChannel(argb, 0);
    const int mx = r > g2 ? (r > b ? r : b) : (g2 > b ? g2 : b);
    const int mn = r < g2 ? (r < b ? r : b) : (g2 < b ? g2 : b);
    return (mx + mn) / 2;
}
inline COLORREF cwLerpRGB(COLORREF a, COLORREF b, double t) {
    return RGB(GetRValue(a) + (int)((GetRValue(b) - GetRValue(a)) * t),
               GetGValue(a) + (int)((GetGValue(b) - GetGValue(a)) * t),
               GetBValue(a) + (int)((GetBValue(b) - GetBValue(a)) * t));
}
inline bool cwIsMaximized(HWND w) {          // main.c verbatim
    WINDOWPLACEMENT placement = {0};
    placement.length = sizeof(WINDOWPLACEMENT);
    return GetWindowPlacement(w, &placement)
        && placement.showCmd == SW_SHOWMAXIMIZED;
}
inline bool cwFileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// v7.7.7 THE 254 LAW -- the band's ink must never ride the opaque fast path.
// Inside the extend band GDI+ routes FULL-ALPHA (255) solid fills through its
// GDI fast path, which writes alpha=0 into the DIB -- and the DWM composites
// the band BY ALPHA, so every 255-fill lands as NOTHING. GBand clamps
// 255 -> 254: a 0.4% sheet bleed, invisible, and every band fill goes through
// the blend path with its alpha intact.
inline Gdiplus::Color GBand(COLORREF c, BYTE a) {
    if (a == 255) a = 254;
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

inline std::wstring cwWide(const char* s) {
    std::wstring w;
    if (s && *s) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        if (n > 1) {
            w.resize(n - 1);
            MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
        }
    }
    return w;
}
// antialiased single-line text; align: 0 center, 1 left, 2 right (the twinspeak
// renderer, moved with the title that uses it)
inline void cwDrawStringA(Gdiplus::Graphics& g, HDC dc, const RECT& rc, const char* txt,
                          COLORREF col, HFONT f, int align) {
    const std::wstring wt = cwWide(txt);
    if (wt.empty() || !f || !dc) return;
    Gdiplus::Font gf(dc, f);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(align == 1 ? Gdiplus::StringAlignmentNear :
                    align == 2 ? Gdiplus::StringAlignmentFar : Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoFitBlackBox);
    sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    const Gdiplus::SolidBrush br(GBand(col, 254));
    const Gdiplus::RectF rf((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                            (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));
    g.DrawString(wt.c_str(), (INT)wt.size(), &gf, rf, &sf, &br);
}

// ---- v7.7.9 THE uDWM CAPTION FONT -------------------------------------------
// CWindowData::GetNonClientCaptionFont (0x3dff8) copies the desktop settings
// singleton's LOGFONTW -- the system NONCLIENTMETRICS lfCaptionFont -- scaled
// by windowDpi/systemDpi with double math and truncation. There is no
// per-window font API anywhere in uDWM.dll (decompile notes v2 section 6).
// The header scales lfHeight by its own S() ratio the same way and re-runs on
// cwDpiChanged. CText::SetFont (0x2c4bc) memcmp's the LOGFONTW and no-ops on
// equality -- the header answers the same memcmp.
inline void cwEnsureTitleFont() {
    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    LOGFONTW lf = {};
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)
            && ncm.lfCaptionFont.lfHeight != 0) {
        lf = ncm.lfCaptionFont;
    } else {   // the belt: the metrics can fail on broken profiles
        lf.lfHeight = -12; lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        lstrcpynW(lf.lfFaceName, L"Segoe UI", LF_FACESIZE);
    }
    lf.lfHeight = MulDiv(lf.lfHeight, (int)cwd::cwS(100), 100);
    if (!g_titleFontDirty && g_titleFont &&
        memcmp(&lf, &g_titleLf, sizeof(lf)) == 0) return;   // CText::SetFont's law
    g_titleLf = lf;
    if (g_titleFont) DeleteObject(g_titleFont);
    g_titleFont = CreateFontIndirectW(&lf);
    g_titleFontDirty = false;
}

// ---- v7.7.9 THE BUTTON RECTS (the 0x21bc0 / 0x222a8 law, whole) -------------
// glyph = round(SM_CYSIZE * 21/22); stride = round(glyph * 48/22) -- the SAME
// stride in BOTH states; the restored close = round(glyph * 49/22). bh =
// SM_CYCAPTION + SM_CYFRAME + SM_CXPADDEDBORDER (31 at 100%).
//   RESTORED: the contiguous flush-right stack, one px inside the right edge,
//   one px down (the offset dial's 1), clamped at the caption's floor.
//   MAXIMIZED: top 0, height one px short, every fill = stride - 0.5 (the
//   half-px tails are REAL pixels -- the tail column renders at HALF the patch
//   alpha), 0.5 px seams, the close's right edge 1.5 px inside. The rects are
//   FLOAT; the contains() is a float compare (x < right holds on the half
//   column, so no dead pixel exists between the buttons).
enum { cwBtnNone = 0, cwBtnMin = 1, cwBtnMax = 2, cwBtnClose = 3 };
struct CwStripRectF { float left, top, right, bottom; };
inline void cwStripRectsF(CwStripRectF rc[3], float clientRight) {
    const int cys = cwMetric(SM_CYSIZE);
    const int glyph = (cys * 21 + 11) / 22;                // round(cys*21/22)
    const int sW = (glyph * 48 + 11) / 22;                 // the stride 46 at 100%
    const int cW = (glyph * 49 + 11) / 22;                 // restored close 47
    const int bh = cwMetric(SM_CYCAPTION)
                 + cwMetric(SM_CYFRAME)
                 + cwMetric(SM_CXPADDEDBORDER);            // 31 at 100%
    if (g_host && IsWindow(g_host) && cwIsMaximized(g_host)) {
        const float fill = (float)sW - 0.5f;
        const float R    = clientRight - 1.5f;
        rc[2].left = R - fill;                 rc[2].right = R;                     // close
        rc[1].left = R - 2.0f * fill - 0.5f;   rc[1].right = R - fill - 0.5f;       // max
        rc[0].left = R - 3.0f * fill - 1.0f;   rc[0].right = R - 2.0f * fill - 1.0f; // min
        rc[0].top = rc[1].top = rc[2].top = 0.0f;
        rc[0].bottom = rc[1].bottom = rc[2].bottom = (float)bh - 1.0f;
    } else {
        const int top = 1;
        int bbot = top + bh;
        if (bbot > g_barH) bbot = g_barH;
        rc[2].left = clientRight - 1 - cW;  rc[2].right = clientRight - 1;   // close
        rc[1].left = rc[2].left - sW;       rc[1].right = rc[2].left;        // max
        rc[0].left = rc[1].left - sW;       rc[0].right = rc[1].left;        // min
        rc[0].top = rc[1].top = rc[2].top = (float)top;
        rc[0].bottom = rc[1].bottom = rc[2].bottom = (float)bbot;
    }
}
// the integer view -- the glyph anchors and the invalidator. The half column
// stays IN (right/bottom rounded up): the painter owns the half coverage.
inline void cwStripRects(RECT rc[3], int clientRight) {
    CwStripRectF f[3];
    cwStripRectsF(f, (float)clientRight);
    for (int i = 0; i < 3; ++i) {
        rc[i].left   = (int)f[i].left;
        rc[i].top    = (int)f[i].top;
        rc[i].right  = (int)(f[i].right + 0.999f);
        rc[i].bottom = (int)(f[i].bottom + 0.999f);
    }
}
inline int cwStripBtnAt(HWND w, int x, int y) {
    RECT crc; CwStripRectF rc[3];
    GetClientRect(w, &crc);
    cwStripRectsF(rc, (float)crc.right);
    for (int i = 0; i < 3; ++i)
        if ((float)x >= rc[i].left && (float)x < rc[i].right &&
            (float)y >= rc[i].top  && (float)y < rc[i].bottom)
            return i + 1;
    return cwBtnNone;
}
inline void cwStripInvalidate(HWND w) {
    RECT crc, rc[3];
    GetClientRect(w, &crc);
    cwStripRects(rc, crc.right);
    for (int i = 0; i < 3; ++i) InvalidateRect(w, &rc[i], FALSE);
}

// ---- v7.7.9 THE uDWM TIMELINE (the hover/press glow) ------------------------
// CButton::ActivateTimeline (0x77b4) constructs the fade with the double at
// .rdata CC920 = 0.16 s; CButton::ComputeFadeValues (0x7b10) rides it:
//     incoming = start + 2P(end-start), clamped at end   (full at 80 ms)
//     outgoing = (end - P) * C                           (gone at 160 ms)
// RedrawVisual (0x22648) chains fades by snapshotting the running values as
// the next fade's (start, C) -- the swap keeps the crossfade pop-free. Here:
// per button, a HOVER channel and a PRESS channel, each with (t0, from)
// re-snapshotted at every state change -- the same law, one overlay per
// channel. The v7.5.x linear 100/150 ramps, the tempo brackets and the
// inactive lapse are retired (the decompile out-answers them).
inline constexpr UINT kCwFadeTimerId = 0x43485631u;  // the strip's 16 ms clock
inline constexpr int  kCwHoverFadeMs = 160;          // the .rdata CC920 double
// the patch strengths (the atlas tint equivalents, v7.7.5's signed values)
inline constexpr int kBtnFadeHover = 46, kBtnFadePress = 44;    // neutral ink /255
inline constexpr int kBtnRedHover  = 185, kBtnRedPress = 45;    // close red /255
inline int   g_btnHover   = cwBtnNone;
inline int   g_btnPress   = cwBtnNone;
inline int   g_btnHoverA[4] = { 0, 0, 0, 0 };
inline int   g_btnPressA[4] = { 0, 0, 0, 0 };
inline DWORD g_btnHovT0[4]  = { 0, 0, 0, 0 };
inline int   g_btnHovFrom[4] = { 0, 0, 0, 0 };
inline DWORD g_btnPrsT0[4]  = { 0, 0, 0, 0 };
inline int   g_btnPrsFrom[4] = { 0, 0, 0, 0 };
inline bool  g_btnTrack  = false;       // TRACKMOUSEEVENT is armed
inline bool  g_btnAnim   = false;       // the 16 ms clock is running
// v7.7.11 THE INACTIVE TARGETS -- UpdateButtonVisuals (0x25364) picks the
// button visuals' target opacity with TreatAsActiveWindow: 1.0f active
// (.rdata CFA0C), 0.4f inactive (.rdata CFA18). 0.4 * 255 = 102 -- the ink's
// own inactive dial. The hover engages on an inactive window and rides to
// THIS target on the same 160 ms clock: the field's "different rates".
inline constexpr int kCwHoverTgtAct   = 255;   // the 1.0f at CFA0C
inline constexpr int kCwHoverTgtInact = 102;   // the 0.4f at CFA18
// the activation ledger: g_winActive is the last known state; g_forceInact
// pins the INACTIVE law across the minimize moment (SC_MINIMIZE dispatch and
// WM_SIZE SIZE_MINIMIZED) -- the window is still GetActiveWindow() while the
// frame is presented, and the frame must present the inactive top bar, the
// inactive face and the retargeted channels anyway. Cleared by WM_ACTIVATE /
// WM_NCACTIVATE active and by any non-iconic WM_SIZE.
inline bool  g_winActive  = true;
inline bool  g_forceInact = false;
// ONE activation read for the whole header -- the painters, the channel
// targets and the flip handlers all answer this (the host's `active` paint
// argument is a hint; this is the law).
inline bool cwWinActive() {
    if (g_forceInact) return false;                   // the minimize moment
    if (!g_host || !IsWindow(g_host)) return true;    // pre-creation: active
    return GetActiveWindow() == g_host;
}
// one eased read of a channel: the 0x7b10 law. (from -> target; P = elapsed /
// 160 ms. The arriving side rides the 2x ramp clamped at target -- full at
// 80 ms; a retarget between two NONZERO strengths rides the same 2x ramp down
// (the fade END synced to the new [0x194], the incoming curve of
// ComputeFadeValues: start + 2P(end-start)); a TRUE leave (target 0) rides
// the 1x ramp to zero at the full timeline.)
inline int cwFadeChannel(DWORD now, DWORD t0, int from, int target) {
    if (from == target) return target;
    const DWORD el = now - t0;
    if (el >= (DWORD)kCwHoverFadeMs) return target;
    const float P = (float)el / (float)kCwHoverFadeMs;
    if (target > from) {
        float a = (float)from + 2.0f * P * (float)(target - from);
        if (a > (float)target) a = (float)target;
        return (int)(a + 0.5f);
    }
    if (target > 0) {
        // v7.7.11: the activation retarget (1.0 <-> 0.4) -- the incoming
        // curve driven DOWN to the new end, arriving at half the timeline.
        float a = (float)from + 2.0f * P * (float)(target - from);
        if (a < (float)target) a = (float)target;
        return (int)(a + 0.5f);
    }
    return (int)((float)from * (1.0f - P) + 0.5f);
}
inline void cwStripRetargetHov(int i) {
    if (i < 1 || i > 3) return;
    g_btnHovFrom[i] = g_btnHoverA[i];
    g_btnHovT0[i]   = GetTickCount();
}
// v7.7.10: defined below the state block -- the fade tick and the leave
// handlers both re-derive the hover from the cursor's real position.
inline void cwStripHoverRetarget(HWND w, int cx, int cy);
inline void cwStripRetargetPrs(int i) {
    if (i < 1 || i > 3) return;
    g_btnPrsFrom[i] = g_btnPressA[i];
    g_btnPrsT0[i]   = GetTickCount();
}
inline void cwStripAnimArm() {
    if (g_btnAnim || !g_host || !IsWindow(g_host)) return;
    if (SetTimer(g_host, kCwFadeTimerId, 16, nullptr)) g_btnAnim = true;
}
inline void cwStripAnimTick() {
    if (!g_btnAnim) return;
    const HWND w = g_host;
    if (!w || !IsWindow(w) || IsIconic(w) || !IsWindowVisible(w)) {
        g_btnHoverA[1] = g_btnHoverA[2] = g_btnHoverA[3] = 0;
        g_btnPressA[1] = g_btnPressA[2] = g_btnPressA[3] = 0;
        g_btnPress = cwBtnNone;
        KillTimer(w, kCwFadeTimerId); g_btnAnim = false;
        return;
    }
    const DWORD now = GetTickCount();
    bool busy = false;
    // v7.7.11: the channel targets answer the activation ledger -- the hover
    // (and the press, should one latch before activation lands) rides to
    // 255 on an active window, 102 on an inactive one: UpdateButtonVisuals'
    // TreatAsActiveWindow pair, on the shared 160 ms clock.
    const bool winAct = cwWinActive();
    const int hTgt = winAct ? kCwHoverTgtAct : kCwHoverTgtInact;
    // v7.7.10: the sweep rides the cursor's REAL position too -- a leave the
    // queue never delivered (a menu loop ate it, tracking was consumed) can
    // no longer strand a lit glow: while the timeline runs, every tick
    // re-derives the hover and retargets if the cursor is somewhere else.
    // The system's modal loops (a caption drag, a sizing edge) capture the
    // mouse and the native buttons stay unlit there -- the re-derive stands
    // down while GetCapture() answers a window.
    if (!g_btnPress && !GetCapture()) {
        POINT pt;
        if (GetCursorPos(&pt)) {
            ScreenToClient(w, &pt);
            cwStripHoverRetarget(w, pt.x, pt.y);
        }
    }
    for (int i = 1; i <= 3; ++i) {
        const int hT = (g_btnHover == i) ? hTgt : 0;
        const int pT = (g_btnPress == i) ? hTgt : 0;
        g_btnHoverA[i] = cwFadeChannel(now, g_btnHovT0[i], g_btnHovFrom[i], hT);
        g_btnPressA[i] = cwFadeChannel(now, g_btnPrsT0[i], g_btnPrsFrom[i], pT);
        if (g_btnHoverA[i] != hT || g_btnPressA[i] != pT) busy = true;
    }
    // the native contract's sweep: a release off-window un-presses
    if (g_btnPress && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        cwStripRetargetPrs(g_btnPress);
        g_btnPress = cwBtnNone;
        busy = true;
    }
    cwStripInvalidate(w);               // the tick's frame rides the host's paint
    if (!busy && g_btnAnim) { KillTimer(w, kCwFadeTimerId); g_btnAnim = false; }
}
// the pure hover re-derive -- compare the cursor's button and retarget the
// channels. NO tracking arm: the WM_MOUSELEAVE/WM_NCMOUSELEAVE handlers call
// this, and re-arming TrackMouseEvent from inside a leave handler races the
// system's own one-shot tracking into an arm/leave loop.
inline void cwStripHoverRetarget(HWND w, int cx, int cy) {
    // v7.7.11: the hover lives on INACTIVE windows too -- UpdateButtonVisuals
    // (0x25364) re-runs SetVisualStates with TreatAsActiveWindow's target at
    // every state change (1.0 active / 0.4 inactive): the buttons track the
    // mouse on an unfocused window and the glow rides to the 0.4 target. The
    // hit test gains the WindowFromPoint guard: with the inactive law the
    // tick's re-derive runs while OTHER windows own the cursor -- a window
    // stacked above ours must not light our buttons through its own pixels.
    int hit = cwBtnNone;
    if (g_host && IsWindow(g_host)) {
        POINT spt;
        if (GetCursorPos(&spt) && WindowFromPoint(spt) == g_host)
            hit = cwStripBtnAt(w, cx, cy);
    }
    if (g_btnPress && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        cwStripRetargetPrs(g_btnPress);
        g_btnPress = cwBtnNone;
        cwStripAnimArm();
    }
    if (hit != g_btnHover) {
        if (g_btnHover) cwStripRetargetHov(g_btnHover);   // the leave: the 1x ramp out
        g_btnHover = hit;
        if (hit) cwStripRetargetHov(hit);                 // the enter: the 2x ramp in
        cwStripAnimArm();
    }
}
inline void cwStripHoverUpdate(HWND w, int cx, int cy) {
    cwStripHoverRetarget(w, cx, cy);
    if (!g_btnTrack) {
        // v7.7.10 THE HOVER THAT NEVER LIT: TME_LEAVE alone tracks the
        // CLIENT. Armed from NC traffic -- the only traffic the band gets --
        // the cursor was never in the client, so the system answered with
        // WM_MOUSELEAVE immediately and the leave handler faded the hover
        // back out the same instant it lit. Hover looked dead; only the
        // PRESS channel (which no leave touches) ever showed -- the field:
        // "when I press my mouse down on a titlebar button that it triggers
        // the animation, when just having the cursor over a titlebar button
        // should have triggered the animation". BOTH leaves are tracked now
        // (TME_NONCLIENT rides along) and every leave re-derives from the
        // cursor's REAL position (cwStripHoverRetarget in the handlers
        // below): a leave that is only the band's client/NC boundary keeps
        // the hover, a true departure re-derives hit = none and fades out.
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT),
                                TME_LEAVE | TME_NONCLIENT, w, 0 };
        if (TrackMouseEvent(&tme)) g_btnTrack = true;
    }
}
// v7.7.11 THE ACTIVATION FLIP -- the SetVisualStates re-run: UpdateButtonVisuals
// picks the target opacity with TreatAsActiveWindow on EVERY visual-state
// pass, so the native running fades re-target when the window's activation
// changes. The from-snapshot is re-taken from the CURRENT alpha (the
// RedrawVisual crossfade law) so a channel mid-fade crosses 1.0 <-> 0.4
// smoothly instead of popping or dying. Called from WM_NCACTIVATE /
// WM_ACTIVATE and from the minimize-moment transaction.
inline void cwStripActivationFlip() {
    bool moving = false;
    for (int i = 1; i <= 3; ++i) {
        if (g_btnHoverA[i] || g_btnHover == i) {
            if (g_btnHoverA[i]) { g_btnHovFrom[i] = g_btnHoverA[i]; g_btnHovT0[i] = GetTickCount(); }
            moving = true;
        }
        if (g_btnPressA[i] || g_btnPress == i) {
            if (g_btnPressA[i]) { g_btnPrsFrom[i] = g_btnPressA[i]; g_btnPrsT0[i] = GetTickCount(); }
            moving = true;
        }
    }
    if (moving) cwStripAnimArm();
}

// ---- v7.7.5 THE DIAL (moved from the host, v7.7.9) --------------------------
// The titlebar color's HSL lightness picks the immersive mode; the band's ink
// (the glyphs and the title) reads the same dial. The host seeds the face
// through changeWinTitlebarColor -- applyTheme's call lands here on every
// theme switch, the startup color-change case included, and the band re-inks
// in the same transaction.
// (the DWMAPI seam lives below; the dial needs the setter)
typedef HRESULT (WINAPI *CwDwmSetAttrFn)(HWND, DWORD, LPCVOID, DWORD);
inline CwDwmSetAttrFn cwDwmSetAttrPtr();
inline int g_dwmDark = 1;             // the mode the band's ink reads
inline bool cwWantsLight() {
    return cwHslLightness(g_faceCol) > 128;   // the field's 128 dial, HSL L
}
inline void cwApplyDwmMode() {
    const int prev = g_dwmDark;
    g_dwmDark = cwWantsLight() ? 0 : 1;
    if (prev != g_dwmDark) cwRepaint();       // the band re-inks on the flip
    if (!g_host || !IsWindow(g_host)) return; // pre-creation: parked; cwAttach lands it
    if (CwDwmSetAttrFn set = cwDwmSetAttrPtr()) {
        if (FAILED(set(g_host, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                       &g_dwmDark, sizeof(g_dwmDark))))
            set(g_host, 19, &g_dwmDark, sizeof(g_dwmDark));
    }
}

// ---- v7.7.9 THE REGISTRY BORDER ---------------------------------------------
// uDWM hardcodes no registry names: it reads uxtheme's
// GetUserColorPreference / GetColorFromPreference, whose store -- the strings
// inside uxtheme.dll -- is HKCU\Software\Microsoft\Windows\DWM:
//     ColorPrevalence (DWORD)   "show accent color on title bars and borders"
//     AccentColor (DWORD)       0xAABBGGRR (alpha, B, G, R -- the default blue
//                               accent #0078D7 stores as 0xffd77800)
//     AccentColorInactive       same layout, optional (1903+) -- in the
//                               STORE, but v7.7.12 retires the house's read
//                               of it: the inactive line is always the state
//                               table's half-transparent 43,43,43
//     ColorizationColor         0xAARRGGBB, the fallback when AccentColor is
//                               absent
// DWM paints the 1px left/right/bottom strips itself (the NCCALCSIZE recipe's
// own NC area); the house's TOP sheet line mirrors this same law.
inline bool     g_borderPrevalence  = false;
inline bool     g_borderHaveAccent  = false;
inline COLORREF g_borderAccent      = RGB(0, 0, 0);
// v7.7.12: the AccentColorInactive read is RETIRED with the override -- the
// inactive sheet line is the uDWM state table's own entry at the sheet's
// half alpha, in every registry weather (see the always-half law above).
inline void cwBorderReadColors() {
    g_borderPrevalence = false;
    g_borderHaveAccent = false;
    HKEY k;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\DWM",
                      0, KEY_READ, &k) != ERROR_SUCCESS) return;
    DWORD v = 0, type = 0, cb = sizeof(v);
    if (RegQueryValueExA(k, "ColorPrevalence", nullptr, &type,
                         (LPBYTE)&v, &cb) == ERROR_SUCCESS && type == REG_DWORD)
        g_borderPrevalence = (v != 0);
    // v7.7.10 THE INVERTED VALUE FIX: AccentColor is 0xAABBGGRR -- the RED
    // byte is the LOW byte. The old decode read the HIGH byte as red, which
    // shipped the line the accent's R and B swapped (the default blue
    // #0078D7 painted as #D77800 orange -- the field's "inverted color
    // value" on the top bar). COLORREF wants red in the low byte too, so
    // the two LOW bytes map straight across: RGB(v & 0xFF, (v>>8) & 0xFF,
    // (v>>16) & 0xFF).
    cb = sizeof(v);
    if (RegQueryValueExA(k, "AccentColor", nullptr, &type,
                         (LPBYTE)&v, &cb) == ERROR_SUCCESS && type == REG_DWORD) {
        g_borderAccent = RGB(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF); // 0xAABBGGRR
        g_borderHaveAccent = true;
    }
    if (!g_borderHaveAccent) {   // the ColorizationColor fallback (0xAARRGGBB)
        cb = sizeof(v);
        if (RegQueryValueExA(k, "ColorizationColor", nullptr, &type,
                             (LPBYTE)&v, &cb) == ERROR_SUCCESS && type == REG_DWORD) {
            g_borderAccent = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
            g_borderHaveAccent = true;
        }
    }
    RegCloseKey(k);
}

// ---- the header's own DWMAPI seam (no dwmapi.h, no link dependency) ---------
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
inline void cwDwmFlush() {
    if (CwDwmFlushFn fn = cwDwmFlushPtr()) fn();
}
// v7.7.9: the sheet setup -- the dial's dark mode, the square corners, the NC
// render pin, the registry border read and THE WHOLE-BAND EXTEND CARRIER.
// The extend's top = cwEffBarH() (33 restored / 32 maximized): every band row
// composites through the same alpha path, row 0 (the sheet line) through the
// band's last row -- the top AND the bottom of the strip are the same glass,
// which is the accuracy the field asked for. Re-run on the maximize flip and
// on WM_DWMCOMPOSITIONCHANGED (composition can restart under the window).
inline bool cwSheetSetup(HWND w) {
    if (!w || !IsWindow(w)) return false;
    cwBorderReadColors();
    if (CwDwmSetAttrFn set = cwDwmSetAttrPtr()) {
        const int corner = 1;   // DWMWCP_DONOTROUND -- square corners
        set(w, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
        const int ncrp = 2;     // DWMNCRP_ENABLED
        set(w, 2 /* DWMWA_NCRENDERING_POLICY */, &ncrp, sizeof(ncrp));
        if (FAILED(set(w, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                       &g_dwmDark, sizeof(g_dwmDark))))
            set(w, 19, &g_dwmDark, sizeof(g_dwmDark));
    }
    CwDwmExtendFn ext = cwDwmExtendPtr();
    if (!ext) return false;
    const CwMargins sheet = { 0, 0, cwEffBarH(), 0 };   // the whole band, both states
    return SUCCEEDED(ext(w, &sheet));
}

// ---- the painters ------------------------------------------------------------
// BACKGROUND: the face seat (the band IS the titlebar color, alpha 254 through
// the blend path) + the registry sheet line on row 0 (restored only -- the
// maximized clip runs the border off). v7.7.11: BOTH read the header's own
// activation ledger (cwWinActive -- the host's `active` argument is a hint;
// the minimize moment MUST present the inactive law while GetActiveWindow()
// still answers this window).
inline void cwPaintBarBackground(Gdiplus::Graphics& g, int ww, bool active) {
    if (!g_enabled) return;
    const bool act = cwWinActive();
    const int bh = cwEffBarH();
    const int face = act ? g_faceCol : (g_faceColIn < 0 ? g_faceCol : g_faceColIn);
    const int grad = act ? g_faceGrad : (g_faceGradIn < 0 ? g_faceGrad : g_faceGradIn);
    if (face == grad) {   // the solid collapse (a gradient between equals is exact)
        const Gdiplus::SolidBrush seat(GBand(
            RGB(cwChannel(face, 16), cwChannel(face, 8), cwChannel(face, 0)), 254));
        g.FillRectangle(&seat, 0, 0, ww, bh);
    } else {
        Gdiplus::LinearGradientBrush brush(
            Gdiplus::Rect(0, 0, ww + 1, bh + 1),
            Gdiplus::Color(254, cwChannel(face, 16), cwChannel(face, 8), cwChannel(face, 0)),
            Gdiplus::Color(254, cwChannel(grad, 16), cwChannel(grad, 8), cwChannel(grad, 0)),
            Gdiplus::LinearGradientModeVertical);
        g.FillRectangle(&brush, 0, 0, ww, bh);
    }
    // THE REGISTRY SHEET LINE (row 0): the accent when ColorPrevalence says
    // so, the native idle line (white @ 0.2) otherwise, gone when maximized.
    // v7.7.12 THE ALWAYS-HALF INACTIVE BORDER: on the inactive state the
    // line is BGRA(43,43,43,128) in EVERY registry weather -- the uDWM state
    // table's inactive entry (.rdata CD7E4 = 0xFF2B2B2B) at the sheet's own
    // 0.5 alpha, no registry override (the field: "the inactive window's
    // border color is always at a half-transparent 43,43,43"; the v7.7.11
    // AccentColorInactive branch is retired). dwmcore.dll was scanned for it
    // and stages no chroma at all; the law is uDWM's own.
    if (!(g_host && IsWindow(g_host) && cwIsMaximized(g_host))) {
        COLORREF bc = RGB(255, 255, 255);
        BYTE a = 51;                       // the sheet's 0.2 (the native measure)
        if (g_borderPrevalence && g_borderHaveAccent && act) {
            bc = g_borderAccent;
            a = 254;
        } else if (!act) {
            bc = RGB(43, 43, 43);          // the uDWM inactive base CD7E4
            a = 128;                       // at the sheet's 0.5 alpha -- ALWAYS
        }
        const Gdiplus::SolidBrush b(GBand(bc, a));
        g.FillRectangle(&b, 0, 0, ww, 1);
    }
}

// FOREGROUND: the glow (the uDWM timeline), the glyphs, the uDWM title. The
// dc rides in for the title's Gdiplus::Font (the host's own buffer DC keeps
// the exact v7.7.6 look; the wrapper below fetches the window's).
inline void cwPaintBandForeground(Gdiplus::Graphics& gs, HDC dc, HWND w, int ww,
                                  bool active) {
    if (!g_enabled) return;
    gs.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    gs.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    CwStripRectF rf[3];
    cwStripRectsF(rf, (float)ww);
    RECT rc[3];
    cwStripRects(rc, ww);
    const bool maxed = g_host && IsWindow(g_host) && cwIsMaximized(g_host);
    const bool dark = (g_dwmDark != 0);
    const bool winActive = cwWinActive();   // v7.7.11: the header's own ledger
    const int glyphA = winActive ? 254 : 102;   // the 0.4 law (254 * 0.4 = 101.6)
    // the glow's own backdrop -- the face the patch bakes under its ink. The
    // CURRENT face: active or the inactive mirror -- the inactive glow rides
    // at the 0.4 target over the SAME face the seat painted (the patch stays
    // opaque, matched to the seat underneath it).
    const int faceSeat = winActive ? g_faceCol
                                   : (g_faceColIn < 0 ? g_faceCol : g_faceColIn);
    const COLORREF faceC = RGB(cwChannel(faceSeat, 16), cwChannel(faceSeat, 8),
                               cwChannel(faceSeat, 0));
    // ---- the glow: the three rects, the animated ink, nothing else. The dark
    // dial lays white ink under the white glass-ink glyphs; the light dial
    // black under the dark ones; the close keeps the native red -- the
    // setWinCloseBtnColor knob's (default the real thing's RGB(232,17,35);
    // the hardcoded 196,43,28 that ignored the knob is retired).
    // v7.7.10 THE OPAQUE PATCH: the band composites BY ALPHA and each GDI+
    // fill REPLACES the pixel under it (the 254 law) -- a patch drawn as bare
    // ink at alpha 46 punched the opaque face seat OUT of the buffer, and the
    // glass behind the window showed through the hovered button (the field:
    // "make sure that the titlebar button hovers are opaque"). The patch now
    // carries its own backdrop: the color is the FACE blended toward the ink
    // at the signed strength, drawn at alpha 254. The fade animates the COLOR
    // over an opaque button, never the alpha -- no state of the timeline can
    // show glass. The half-px tails ride the same law at their real coverage
    // (the tail column = the face blended toward the PATCH, still opaque);
    // the maximized fills' 0.5 px tails stay real pixels.
    for (int i = 1; i <= 3; ++i) {
        if (i == cwBtnMin && !g_btnMin) continue;   // the dead slot is face
        if (i == cwBtnMax && !g_btnMax) continue;
        // v7.7.11: the channels are UNGATED -- on an inactive window they
        // carry the 0.4 target themselves (kCwHoverTgtInact), so the patch
        // strength lands at 40% of the active values, the UpdateButtonVisuals
        // law, with no painter-side gate to fight the timeline.
        const int ha = g_btnHoverA[i];
        const int pa = g_btnPressA[i];
        int a = (i == cwBtnClose)
              ? (ha * kBtnRedHover  + pa * kBtnRedPress)  / 255
              : (ha * kBtnFadeHover + pa * kBtnFadePress) / 255;
        if (a <= 0) continue;
        if (a > 255) a = 255;
        const CwStripRectF& r = rf[i - 1];
        const int il = (int)r.left, ir = (int)r.right;
        const int it = (int)r.top,  ib = (int)r.bottom;
        const float lf = r.left  - (float)il;   // the left tail's coverage
        const float rf2 = r.right - (float)ir;  // the right tail's coverage
        const COLORREF ink = (i == cwBtnClose)
                           ? RGB((int)((g_closeHover >> 16) & 0xFF),
                                 (int)((g_closeHover >> 8) & 0xFF),
                                 (int)(g_closeHover & 0xFF))
                           : (dark ? RGB(255, 255, 255) : RGB(0, 0, 0));
        const COLORREF patch = cwLerpRGB(faceC, ink, a / 255.0);
        const int cl = (lf > 0.001f) ? il + 1 : il;      // the full columns
        if (ir > cl) {
            const Gdiplus::SolidBrush br(GBand(patch, 254));
            gs.FillRectangle(&br, Gdiplus::Rect(cl, it, ir - cl, ib - it));
        }
        if (lf > 0.001f) {                               // the left half column
            const Gdiplus::SolidBrush br(GBand(cwLerpRGB(faceC, patch, lf), 254));
            gs.FillRectangle(&br, Gdiplus::Rect(il, it, 1, ib - it));
        }
        if (rf2 > 0.001f) {                              // the right half column
            const Gdiplus::SolidBrush br(GBand(cwLerpRGB(faceC, patch, rf2), 254));
            gs.FillRectangle(&br, Gdiplus::Rect(ir, it, 1, ib - it));
        }
    }
    // ---- the glyphs -- the house's own (uDWM's compose retired with
    // WS_SYSMENU, the button-group gate): dash S(10)x1, square S(9)+1,
    // restore pair S(8)/S(2) in an S(10) span, X span S(10) -- 1px strokes,
    // centered in the decompile-formula rects. The close's ink crossfades to
    // white with its red band (light mode; a no-op on a dark titlebar).
    const COLORREF gInk = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
    auto gfill = [&](int x, int y, int w2, int h2, COLORREF c) {
        const Gdiplus::SolidBrush b(GBand(c, (BYTE)glyphA));   // the 254 law
        gs.FillRectangle(&b, x, y, w2, h2);
    };
    if (g_btnMin) {   // minimize: the dash, centered on the floored float-rect center
        const CwStripRectF& r = rf[0];
        const int w2 = cwS(10);
        const int cx = (int)((r.left + r.right) * 0.5f);
        const int cy = (int)((r.top + r.bottom) * 0.5f);
        gfill(cx - w2 / 2, cy, w2, 1, gInk);
    }
    if (g_btnMax) {   // maximize: the square; the restore pair while maximized (the front
        // sheet occludes the back one -- the proven clip recipe)
        const CwStripRectF& r = rf[1];
        const int cx = (int)((r.left + r.right) * 0.5f);
        const int cy = (int)((r.top + r.bottom) * 0.5f);
        if (maxed) {
            const int span = cwS(10), sheet = cwS(8), diag = cwS(2);
            const int fl = cx - (span + 1) / 2, bt = cy - span / 2;
            const Gdiplus::Rect frontRect(fl, bt + diag, sheet, sheet);
            Gdiplus::Region frontRegion(frontRect);
            gs.SetClip(&frontRegion, Gdiplus::CombineModeExclude);
            {   // the back sheet, occluded
                const int bx = fl + diag, by = bt;
                gfill(bx, by, sheet, 1, gInk);
                gfill(bx, by + sheet - 1, sheet, 1, gInk);
                gfill(bx, by + 1, 1, sheet - 2, gInk);
                gfill(bx + sheet - 1, by + 1, 1, sheet - 2, gInk);
            }
            gs.ResetClip();
            {   // the front sheet, full
                const int fy2 = bt + diag;
                gfill(fl, fy2, sheet, 1, gInk);
                gfill(fl, fy2 + sheet - 1, sheet, 1, gInk);
                gfill(fl, fy2 + 1, 1, sheet - 2, gInk);
                gfill(fl + sheet - 1, fy2 + 1, 1, sheet - 2, gInk);
            }
        } else {
            const int outer = cwS(9) + 1;
            const int x0 = cx - outer / 2, y0 = cy - outer / 2;
            gfill(x0, y0, outer, 1, gInk);
            gfill(x0, y0 + outer - 1, outer, 1, gInk);
            gfill(x0, y0 + 1, 1, outer - 2, gInk);
            gfill(x0 + outer - 1, y0 + 1, 1, outer - 2, gInk);
        }
    }
    {   // close: the X, span S(10); its ink crossfades to white with the
        // hover alpha (light mode: black ink to white over the red band)
        const CwStripRectF& r = rf[2];
        const int cx = r.left + (r.right - r.left) / 2;
        const int cy = r.top + (r.bottom - r.top) / 2;
        const int span = cwS(10);
        const int x0 = cx - span / 2, y0 = cy - span / 2;
        COLORREF ci = gInk;
        if (!dark && g_btnHoverA[cwBtnClose] > 0)
            ci = cwLerpRGB(RGB(0, 0, 0), RGB(255, 255, 255),
                           g_btnHoverA[cwBtnClose] / 255.0);
        for (int i = 0; i < span; ++i) {
            gfill(x0 + i, y0 + i, 1, 1, ci);
            gfill(x0 + span - 1 - i, y0 + i, 1, 1, ci);
        }
    }
    // ---- the title -- the uDWM caption font, the dial's ink, dimmed toward
    // the face while the window is inactive, vertically centered right of the
    // icon slot, ellipsis-clamped before the buttons.
    if (w && g_titleFont && dc) {
        char title[192] = "";
        if (GetWindowTextA(w, title, (int)sizeof(title)) && title[0]) {
            const int iconW = cwMetric(SM_CXSMICON);
            const RECT tr = { cwS(8) + iconW + cwS(7), rc[0].top,
                              rc[0].left - cwS(8), rc[0].bottom };
            const COLORREF base = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
            const int face = winActive ? g_faceCol
                                       : (g_faceColIn < 0 ? g_faceCol : g_faceColIn);
            const COLORREF faceC = RGB(cwChannel(face, 16), cwChannel(face, 8),
                                       cwChannel(face, 0));
            const COLORREF tc = winActive ? base : cwLerpRGB(base, faceC, 0.5);
            cwDrawStringA(gs, dc, tr, title, tc, g_titleFont, 1);
        }
    }
}
// the public wrappers -- with the host's own DC (the exact buffered look) or
// with the window's (a fresh consumer paints without a buffer).
inline void cwPaintBarForeground(Gdiplus::Graphics& g, HDC dc, HWND w, int ww,
                                 bool active) {
    cwPaintBandForeground(g, dc, w, ww, active);
}
inline void cwPaintBarForeground(Gdiplus::Graphics& g, HWND w, int ww, bool active) {
    HDC dc = w ? GetDC(w) : nullptr;
    cwPaintBandForeground(g, dc, w, ww, active);
    if (dc) ReleaseDC(w, dc);
}
// v7.7.6 THE CAPTION ICON -- the small icon in its native slot at the
// caption's left, vertically centered in the caption block. GDI (DrawIconEx)
// runs AFTER the GDI+ scope dies -- the house never mixes them on one DC.
// v7.7.12 THE ICON FADE VERDICT (the field asked for the disassembly of
// "the titlebar icon fade on the inactive window state"): there is NONE
// in uDWM.dll -- CImage::SetOpacity (0x8ac00) has ZERO call sites in the
// whole .text, CVisual::SetOpacity's (0x20a90) 47 call sites all target
// the layered alpha / show-hide / thumbnails / touch visuals, and the
// activation flip's UpdateWindowVisuals touches the icon only for its
// hit-test override (CImage+0xc0 = 0/3 at 0x241e5). The inactive dim the
// eye reads is the FACE BENEATH the icon re-blending (the glass state
// weights CCD90 = 0.2 / CCDC0 = 0.6) plus the title's CFA18 0.4 color
// blend and the buttons' 0.4 glyph dial. The icon therefore draws at FULL
// opacity in both states here -- the 100%-accurate answer. Full receipts:
// the header's THE ICON block and the v7.7.12 changelog.
inline void cwPaintCaptionIcon(HDC dc, int cw) {
    if (!g_host || !dc) return;
    HICON hic = (HICON)SendMessageW(g_host, WM_GETICON, ICON_SMALL, 0);
    if (!hic) hic = (HICON)GetClassLongPtrW(g_host, GCLP_HICONSM);
    if (!hic) hic = (HICON)GetClassLongPtrW(g_host, GCLP_HICON);
    if (!hic) return;
    const int iw = cwMetric(SM_CXSMICON), ih = cwMetric(SM_CYSMICON);
    RECT rc[3];
    cwStripRects(rc, cw);
    const int iy = rc[0].top + (rc[0].bottom - rc[0].top - ih) / 2;
    DrawIconEx(dc, cwS(8), iy, hic, iw, ih, 0, nullptr, DI_NORMAL);
}
// one-call chrome for fresh consumers
inline void cwPaintChrome(Gdiplus::Graphics& g, HWND w, int ww, bool active) {
    cwPaintBarBackground(g, ww, active);
    cwPaintBarForeground(g, w, ww, active);
}

// ---- v7.5.3d: the DPI-responsive child ledger --------------------------------
// Children that ride below the caption (design px in, real px out) register
// here; on WM_DPICHANGED (and on the maximize flip, v7.7.9) the header
// re-positions every tracked child, re-seats its font (a pointer into the
// host's HFONT slot) and runs its per-child fixup (combobox item heights).
// v7.7.9 THE MAXIMIZED DELTA: the caption is one physical px shorter while
// maximized, so EVERY tracked child lands one row higher (cwS(y) + the
// delta); underBar children ride the effective height outright.
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
    const int barNow = cwEffBarH();
    const int capDelta = barNow - g_barH;   // 0 restored, -1 maximized
    for (auto& k : g_kids) {
        if (!k.h || !IsWindow(k.h)) continue;
        SetWindowPos(k.h, nullptr,
                     cwS(k.x), cwS(k.y) + (k.underBar ? barNow : capDelta),
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

// ---- the message hook (forward this FIRST in the host wndproc) --------------
inline bool cwWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp, LRESULT* out) {
    *out = 0;
    if (!g_enabled) return false;
    switch (msg) {
    case WM_NCACTIVATE: {
        // the answer is DefWindowProc with lParam -1 -- never a bare TRUE: the
        // -1 stops DefWindowProc from repainting the NC area (its border
        // visuals are uDWM's own at compose time) while the activation
        // contract stays DefWindowProc's. The band re-inks on BOTH edges of
        // the flip (the glyphs re-render at the inactive 0.4, the title
        // re-dims, the sheet line rides its registry law).
        // v7.7.11: the hover is NOT killed on the flip anymore -- the native
        // UpdateButtonVisuals re-runs SetVisualStates on the flip and the
        // running fades RETARGET (1.0 <-> 0.4, the from-snapshot re-taken);
        // the cursor still over a button keeps its (now 0.4-strength) glow
        // on the unfocused window, the field's inactive-hover law.
        const LRESULT r = DefWindowProcW(w, msg, wp, (LPARAM)-1);
        const bool act = (wp != 0);
        if (act) g_forceInact = false;
        if (act != g_winActive) { g_winActive = act; cwStripActivationFlip(); }
        cwRepaint();
        *out = r; return true; }
    case WM_ACTIVATE:
        // v7.7.11: the activation ledger's second edge (WM_NCACTIVATE's
        // sibling -- ShowWindow/SetForegroundOrder paths can deliver one
        // without the other). WA_INACTIVE == 0; any nonzero low word is
        // WA_ACTIVE / WA_CLICKACTIVE. The flip retargets the running
        // channels and re-inks the band, exactly like WM_NCACTIVATE.
        if (LOWORD(wp) != WA_INACTIVE) g_forceInact = false;
        if ((LOWORD(wp) != WA_INACTIVE) != g_winActive) {
            g_winActive = (LOWORD(wp) != WA_INACTIVE);
            cwStripActivationFlip();
            cwRepaint();
        }
        return false;
    case WM_SYSCOMMAND:
        // v7.7.11 THE MINIMIZE MOMENT: the frame presented as the window goes
        // iconic -- and the taskbar's live preview after it -- must carry the
        // INACTIVE top bar (the uDWM state table's inactive entry at the
        // sheet's 0.5 alpha), the inactive face and the retargeted channels.
        // The flip lands BEFORE the default flow takes the command: the
        // ledger pins the inactive law, the channels re-target, and ONE
        // synchronous band repaint + DwmFlush present the inactive frame so
        // the minimize animation samples it, not the stale active one.
        // SC_RESTORE releases the pin; the activation contract re-lands the
        // active law on the WM_NCACTIVATE(TRUE) that follows.
        if ((wp & 0xFFF0) == SC_MINIMIZE) {
            g_forceInact = true;
            g_winActive = false;
            cwStripActivationFlip();
            if (IsWindowVisible(w) && !IsIconic(w)) {
                cwRepaint();                     // the band into the update region
                // ONE synchronous present, no erase pass (the v7.0.9 law):
                // the host's buffered WM_PAINT lands the inactive frame and
                // DWM is told the frame is there before the animation runs.
                RedrawWindow(w, nullptr, nullptr, RDW_NOERASE | RDW_UPDATENOW);
                cwDwmFlush();
            }
        } else if ((wp & 0xFFF0) == SC_RESTORE) {
            g_forceInact = false;
        }
        return false;
    case WM_DWMCOMPOSITIONCHANGED:
        // composition can restart under the window (driver reset, an RDP
        // session) -- the extend is a geometry commitment to a DWM that no
        // longer exists. Re-run the whole setup (registry border, the dial's
        // dark mode, the band carrier) and re-land the band. The host's own
        // case, if any, still runs after -- idempotent.
        cwSheetSetup(w);
        if (IsWindowVisible(w) && !IsIconic(w)) cwRepaint();
        return false;
    case WM_SIZE: {
        // v7.7.11: the iconic edge pins the inactive law (ShowWindow(SW_MINIMIZE)
        // can reach WM_SIZE without riding WM_SYSCOMMAND); any real size
        // (SIZE_RESTORED / the restore path) releases it -- the activation
        // contract re-lands the active law the moment the window is focused.
        if (wp == SIZE_MINIMIZED) {
            g_forceInact = true;
            g_winActive = false;
            cwStripActivationFlip();
            return false;
        }
        g_forceInact = false;
        // THE MAXIMIZE FLIP -- the extend carrier, the caption height and the
        // children. cwEffBarH() answers one px short while maximized (the
        // border row vanishes); the flip re-runs the sheet setup (the carrier
        // follows the band) and re-seats the tracked children at the new
        // delta. v7.7.12 THE SINGLE-PRESENT FLIP: ONE synchronous whole-frame
        // RedrawWindow (the band rows INCLUDED -- the client rect covers
        // them) lands the flip; the trailing cwRepaint() the older rounds
        // queued after the synchronous pass is retired -- a second queued
        // paint behind RDW_UPDATENOW is exactly the kind of stray frame the
        // compositor can sample mid-animation (the field's "temporarily
        // corrupt" on maximize/restore). RDW_FRAME rides along so the NC
        // border strips re-dress in the same present. Iconic transitions are
        // skipped.
        if (!IsIconic(w)) {
            const bool maxed = cwIsMaximized(w);
            if (maxed != g_wasMax) {
                g_wasMax = maxed;
                cwSheetSetup(w);
                cwRelayoutChildren();
                if (IsWindowVisible(w)) {
                    RECT cr; GetClientRect(w, &cr);
                    RedrawWindow(w, &cr, nullptr,
                                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME
                                 | RDW_ALLCHILDREN | RDW_UPDATENOW);
                }
            }
        }
        return false; }
    case WM_NCCALCSIZE: {
        // THE uDWM RECIPE: the client hands the frame a 1px strip on
        // left/right/bottom -- PHYSICAL px, the native border art is 1px at
        // every DPI -- and uDWM draws its own border line in them (the active
        // and inactive border colors are uDWM's, by construction; the accent
        // tint when colorization is on). Maximized keeps the full frame
        // giveback (the maximized clip runs the native border off).
        if (!wp) return false;
        NCCALCSIZE_PARAMS* ncp = (NCCALCSIZE_PARAMS*)lp;
        if (cwIsMaximized(w)) {
            const int fx = cwMetric(SM_CXFRAME) + cwMetric(SM_CXPADDEDBORDER);
            const int fy = cwMetric(SM_CYFRAME) + cwMetric(SM_CXPADDEDBORDER);
            ncp->rgrc[0].left   += fx;   ncp->rgrc[0].top    += fy;
            ncp->rgrc[0].right  -= fx;   ncp->rgrc[0].bottom -= fy;
        } else {
            ncp->rgrc[0].left   += 1;    ncp->rgrc[0].right  -= 1;
            ncp->rgrc[0].bottom -= 1;
        }
        *out = 0; return true; }
    case WM_NCHITTEST: {
        // THE HOUSE'S OWN WALK: the three buttons answer HTMINBUTTON /
        // HTMAXBUTTON / HTCLOSE over the exact float rects (float contains():
        // the half columns hit, no dead pixel) -- the codes win32k's own
        // caption-button machinery eats -- with the buttons AHEAD of every
        // resize edge (the native order). The resize edges are hand-rolled
        // and DPI-correct, corners first, all four. The rest of the band
        // answers HTCAPTION (drag, Aero snap, double-click maximize and
        // Win+arrows stay the system's own); below it, HTCLIENT.
        const POINT spt = { GET_X_PARAM(lp), GET_Y_PARAM(lp) };
        RECT wrc; GetWindowRect(w, &wrc);
        const int ww = wrc.right - wrc.left, wh = wrc.bottom - wrc.top;
        const int x = spt.x - wrc.left, y = spt.y - wrc.top;
        POINT cpt = spt;
        ScreenToClient(w, &cpt);
        const int btn = cwStripBtnAt(w, cpt.x, cpt.y);
        // the layout knobs gate their buttons: a dead slot is face, like the
        // native stack missing a button
        if (btn == cwBtnMin && g_btnMin)   { *out = HTMINBUTTON; return true; }
        if (btn == cwBtnMax && g_btnMax)   { *out = HTMAXBUTTON; return true; }
        if (btn == cwBtnClose)             { *out = HTCLOSE; return true; }  // 20
        int hit = -1;
        if (!cwIsMaximized(w)) {
            const int fx = cwMetric(SM_CXFRAME) + cwMetric(SM_CXPADDEDBORDER);
            const int fy = cwMetric(SM_CYFRAME) + cwMetric(SM_CXPADDEDBORDER);
            const bool lf = x < fx, rg = x >= ww - fx;
            const bool tp = y < fy, bt = y >= wh - fy;
            if (tp && lf)      hit = HTTOPLEFT;
            else if (tp && rg) hit = HTTOPRIGHT;
            else if (bt && lf) hit = HTBOTTOMLEFT;
            else if (bt && rg) hit = HTBOTTOMRIGHT;
            else if (tp)       hit = HTTOP;
            else if (bt)       hit = HTBOTTOM;
            else if (lf)       hit = HTLEFT;
            else if (rg)       hit = HTRIGHT;
        }
        if (hit >= 0) {
            // v7.5.1 THE HONEST BORDER: a refused resize must not advertise
            // itself -- every sizing code downgrades to HTBORDER (the arrow
            // stays an arrow, the drag never starts)
            if (!g_resizable) hit = HTBORDER;
            *out = hit; return true;
        }
        *out = (cpt.y >= 0 && cpt.y < cwEffBarH()) ? HTCAPTION : HTCLIENT;
        return true; }
    case WM_NCMOUSEMOVE: {
        // the hover lives here; the host's repaint callback lands the pixels
        POINT cpt = { GET_X_PARAM(lp), GET_Y_PARAM(lp) };
        ScreenToClient(w, &cpt);
        cwStripHoverUpdate(w, cpt.x, cpt.y);
        return false; }
    case WM_NCMOUSELEAVE:
    case WM_MOUSELEAVE:
        // v7.7.10: the tracking is one-shot -- mark it dead so the next
        // mousemove re-arms it. The hover itself re-derives from the
        // cursor's REAL position, NOT from the leave's assumption: armed
        // from NC traffic, WM_MOUSELEAVE fires the instant it is armed (the
        // cursor sits over the band, outside the client), and the old blind
        // cancel faded the hover out the same moment it lit -- hover looked
        // dead while press was the only visible glow. A leave that is just
        // that boundary keeps the hover; a true departure re-derives
        // hit = none and rides the 1x ramp out. No re-arm HERE: re-arming
        // inside a leave handler would race the one-shot tracking into an
        // arm/leave loop.
        g_btnTrack = false;
        {   POINT pt;
            if (GetCursorPos(&pt)) {
                ScreenToClient(w, &pt);
                cwStripHoverRetarget(w, pt.x, pt.y);
            }
        }
        return false;
    case WM_NCLBUTTONDOWN: {
        POINT cpt = { GET_X_PARAM(lp), GET_Y_PARAM(lp) };
        ScreenToClient(w, &cpt);
        // no active gate here: the native first click on an INACTIVE window's
        // button still acts (win32k activates on the down -- WM_MOUSEACTIVATE
        // lands before the NC down, so the press latches on the now-active
        // window at the full strength; the HOVER's own inactive law is the
        // 0.4 target, v7.7.11).
        const int hit = cwStripBtnAt(w, cpt.x, cpt.y);
        if (hit) {
            cwStripRetargetPrs(hit);
            g_btnPress = hit;
            cwStripAnimArm();
            *out = 0; return true;            // the house's own press-and-release
        }
        return false; }
    case WM_NCLBUTTONUP: {
        POINT cpt = { GET_X_PARAM(lp), GET_Y_PARAM(lp) };
        ScreenToClient(w, &cpt);
        const int hit = cwStripBtnAt(w, cpt.x, cpt.y);
        if (g_btnPress) {
            if (hit == g_btnPress) {
                // the dispatch: close rides SC_CLOSE (the host's WM_CLOSE
                // decides hide-or-destroy), minimize minimizes, maximize
                // toggles (refused while setCanWinResize(false) -- a dead
                // button for a host that asked)
                if (hit == cwBtnMin)        PostMessageW(w, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                else if (hit == cwBtnMax)   PostMessageW(w, WM_SYSCOMMAND,
                                                          cwIsMaximized(w) ? SC_RESTORE
                                                                           : SC_MAXIMIZE, 0);
                else if (hit == cwBtnClose) PostMessageW(w, WM_SYSCOMMAND, SC_CLOSE, 0);
            }
            cwStripRetargetPrs(g_btnPress);
            g_btnPress = cwBtnNone;
            cwStripAnimArm();
            if (hit) { *out = 0; return true; }
        }
        return false; }
    case WM_LBUTTONUP:
        // a drag-off release can land on the CLIENT (the up re-routes);
        // the press un-latches either way (the tick sweeps too)
        if (g_btnPress) {
            cwStripRetargetPrs(g_btnPress);
            g_btnPress = cwBtnNone;
            cwStripAnimArm();
        }
        return false;
    case WM_CANCELMODE:
        if (g_btnPress) {
            cwStripRetargetPrs(g_btnPress);
            g_btnPress = cwBtnNone;
            cwStripAnimArm();
        }
        return false;
    case WM_MOUSEMOVE: {
        // the belt-and-suspenders client path (the band is NC traffic now;
        // this only fires from odd transitional states)
        POINT cpt = { GET_X_PARAM(lp), GET_Y_PARAM(lp) };
        cwStripHoverUpdate(w, cpt.x, cpt.y);
        return false; }
    case WM_TIMER:
        // the fade timeline's own tick; every other timer is the host's
        if (wp == kCwFadeTimerId && g_btnAnim) { cwStripAnimTick(); *out = 0; return true; }
        return false;
    case WM_SETTINGCHANGE:
        // the accent / theme broadcast: re-read the registry border, re-run
        // the dial, re-ink the band. The host keeps flowing (its own reaction
        // rides after).
        if (lp && lstrcmpiW((LPCWSTR)lp, L"ImmersiveColorSet") == 0) {
            cwBorderReadColors();
            cwApplyDwmMode();
            cwRepaint();
        }
        return false;
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        // the colorization flip (0x0320): the same one-transaction refresh --
        // the registry border, the dial, the band. This is the "changing the
        // titlebar color updates EVERY object" law, from the system side.
        cwBorderReadColors();
        cwApplyDwmMode();
        cwRepaint();
        return false;
    case WM_SETCURSOR:
        // the arrow instead of the busy cursor (main.c); the host's
        // DefWindowProc finishes the protocol
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
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

// ---- the legacy seed font (changeWinTextFont consumers) ---------------------
inline Gdiplus::Font* g_font = nullptr;
inline std::wstring g_fontKey;             // face|px|weight|dpi of the cached font
inline std::wstring g_fontFamily = L"Segoe UI";   // the family the LOGFONT uses
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
                           + L"|" + std::to_wstring(cwDpi());
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

} // namespace cwd

// =====================================================================
// THE PUBLIC API -- global, inline, one file.
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

// v7.7.5/v7.7.9: the titlebar color changed -- the dial re-runs INSIDE the
// setter (the HSL law: L > 128 light, <= 128 dark), the band re-inks, and the
// host's repaint callback lands the pixels. One transaction, every object.
inline void changeWinTitlebarColor(int col) {
    cwd::g_faceCol = col; cwd::g_faceGrad = col;
    cwd::g_faceColIn = -1; cwd::g_faceGradIn = -1;
    cwd::cwApplyDwmMode();
    cwd::cwRepaint();
}
inline void changeWinTitlebarColor(int col, int gradColor) {
    cwd::g_faceCol = col; cwd::g_faceGrad = gradColor;
    cwd::cwApplyDwmMode();
    cwd::cwRepaint();
}
// v7.5.5: the inactive face. One argument: solid. Two: the inactive gradient.
// -1 restores the default: mirror the focused color.
inline void setWinInactiveColor(int col)                         { cwd::g_faceColIn = col; cwd::g_faceGradIn = col; cwd::cwRepaint(); }
inline void setWinInactiveColor(int col, int gradColor)          { cwd::g_faceColIn = col; cwd::g_faceGradIn = gradColor; cwd::cwRepaint(); }
// v7.6.2: the close button's hover red is a knob. One ARGB int (0xAARRGGBB);
// the alpha channel is ignored (the timeline owns the fade, the color rides
// it). Default: the real thing's full red, RGB(232,17,35).
inline void setWinCloseBtnColor(int col)                         { cwd::g_closeHover = (DWORD)col; cwd::cwRepaint(); }

inline void setWinTitleTextColor(int color, int gradientCol,
                                 int inactiveColor, int gradientInactiveCol) {
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

// v7.7.9: the attach -- the host's one call. Lands the caption contract (the
// bar height at the window's DPI), the uDWM caption font, the square-corner +
// NC-render pins, the dial's dark-mode attribute, the registry border read
// and the whole-band extend carrier. A fresh consumer needs NOTHING else.
inline void cwAttach(HWND w, void (*repaint)(HWND)) {
    cwd::g_host = w; cwd::g_repaint = repaint;
    cwd::g_barH = MulDiv(cwd::kBarHDesign, (int)cwd::cwDpi(), 96);
    cwd::cwEnsureTitleFont();
    cwd::cwSheetSetup(w);   // the registry border + the dial + the band carrier
}
inline void cwSetBarHeight(int px)                  { cwd::g_barH = px; }
// v7.5.3d: the bar height is the header's own -- 33 design px at the header's
// dpi. A host adopts this as its caption-height layout contract.
inline int  cwBarHeight()                           { return cwd::g_barH; }
// v7.7.9: the EFFECTIVE caption height -- one physical px short while the
// window is maximized (the border row vanishes). The host's content viewport
// and every below-caption coordinate answer through this.
inline int  cwBarHeightNow()                        { return cwd::cwEffBarH(); }
inline UINT cwDpi()                                 { return cwd::cwDpi(); }
inline void cwDpiChanged(UINT dpi)                  { cwd::cwDpiChanged(dpi); }
inline void cwTrackChild(HWND h, int x, int y, int w, int ht,
                         bool underBar = true, HFONT* font = nullptr,
                         int comboItemH = 0) {
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
// per-theme seed of every chrome color. inactive face defaults to mirroring
// the active one. (twinspeak drives changeWinTitlebarColor directly now; the
// seed stays for the API's own consumers.)
inline void cwSeedChrome(int faceCol, int faceGrad,
                         int faceColIn, int faceGradIn,
                         int textCol, int textGrad, int textUn,
                         int glyphCol) {
    cwd::g_faceCol = faceCol;   cwd::g_faceGrad = faceGrad;
    cwd::g_faceColIn = faceColIn < 0 ? faceCol  : faceColIn;
    cwd::g_faceGradIn = faceGradIn < 0 ? faceGrad : faceGradIn;
    cwd::g_textCol = textCol;   cwd::g_textGrad = textGrad;
    cwd::g_textUn = textUn;     cwd::g_glyph = glyphCol;
    cwd::cwApplyDwmMode();
    cwd::cwRepaint();
}
// v7.7.9: WS_SYSMENU stays OUT (the button-group gate, the v7.6.6 decode) --
// the uDWM stack is the house's own glyphs, not the compose's.
inline DWORD cwFrameStyle() {
    return WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME
         | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
}
inline bool cwWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp, LRESULT* out) {
    return cwd::cwWndProc(w, msg, wp, lp, out);
}
inline void cwPaintBarBackground(Gdiplus::Graphics& g, int ww, bool active) {
    cwd::cwPaintBarBackground(g, ww, active);
}
inline void cwPaintBarForeground(Gdiplus::Graphics& g, HDC dc, HWND w, int ww,
                                 bool active) {
    cwd::cwPaintBarForeground(g, dc, w, ww, active);
}
inline void cwPaintBarForeground(Gdiplus::Graphics& g, HWND w, int ww, bool active) {
    cwd::cwPaintBarForeground(g, w, ww, active);
}
inline void cwPaintChrome(Gdiplus::Graphics& g, HWND w, int ww, bool active) {
    cwd::cwPaintChrome(g, w, ww, active);
}
inline void cwPaintCaptionIcon(HDC dc, int ww) {
    cwd::cwPaintCaptionIcon(dc, ww);
}
inline bool cwIsMaximized(HWND w) { return cwd::cwIsMaximized(w); }
inline DWORD cwArgbOf(COLORREF c) { return cwd::cwArgbOf(c); }   // COLORREF -> ARGB

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

#endif /* _WIN32 */

#endif /* CUSTOMWINDOW_H_INCLUDED */
