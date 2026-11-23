// v9.6.0: the Windows link set, carried by the source so a bare cl.exe line
// needs no .vcxproj (field-directed compile command):
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "d2d1.lib")     // v9.6.0: the GPU tab strip (D2D1CreateFactory)
#pragma comment(lib, "d3d11.lib")    // D3D11CreateDevice -- the strip's device
#pragma comment(lib, "dwrite.lib")   // DWriteCreateFactory -- the strip's glyphs
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "comdlg32.lib")   // GetSaveFileNameA -- the diagnosis log's save dialog
#pragma comment(lib, "Avrt.lib")       // the engine's MMCSS boost (core)

// ============================================================================
//  twinspeak v9.6.0 -- THE SHELL (GUI / ini / main).
//
//  Hollowed out: everything audio lives in twinspeak_core.cpp behind
//  twinspeak_core.h. This file keeps exactly what a shell should:
//    - the control window (tabs, pickers, sliders, pills, tray, themes)
//    - the ini load/save/template and the GUI <-> config mapping
//    - main() / WinMain, the console attach/detach policy, --list/--exit
//    - the tick-test button (the core writes the WAV, the shell opens it)
//  The engine -- miniaudio capture, the four legs, the rings, the depth
//  servo, the futex mix crew, the keep-alive, the chime, the watchdog --
//  is twinspeak_core.cpp's business, reached through four doors:
//      twsCfg / twsRun             -- the shared config + stop flag
//      twsEngineRun                -- the engine lifecycle (console + GUI)
//      twsListDevices/EnumDevices  -- --list and the REFRESH button
//      twsTickWavPath/Write        -- the 120 bpm tick test (LOUD now)
//  The routing (the simple audio pipeline, field-directed):
//      capture = miniaudio loopback on the SPECIFIED OUTPUT device
//      leg1 (mains)  -> the audio device; FL/FR + the LFE fold
//      leg2 (center) -> a separate audio device, filling (ch3 on L/R)
//      leg3 (rears)  -> a separate audio device; EMPTY -> folds to the mains
//      leg4 (LFE)    -> rides whichever leg output_device4 names; no device
//  Every leg runs the same flow (one period request, one anchor, one depth
//  servo, one cross-leg sync to leg1) so device delay drift is ground out,
//  not accepted. See twinspeak_core.cpp's banner for the discipline.
//
//  COMPILE (field-directed):
//    cl.exe /std:c++17 /EHsc /O2 twinspeak.cpp twinspeak_core.cpp
//  (a /subsystem:windows build picks WinMainCRTStartup; the shim at the
//   bottom forwards to main() either way. miniaudio.h sits beside these
//   files; twinspeak_core.cpp is the only unit that defines it.)
// ============================================================================

#include "twinspeak_core.h"

#include <windows.h>
#include <commctrl.h>       // trackbars for the GUI
#include <shellapi.h>       // Shell_NotifyIcon -- the tray icon
#include <objbase.h>        // CoCreateInstance (the tray/taskbar helpers)
#include <gdiplus.h>        // antialiased owner-drawn UI (buttons, text, watermark)
#include <d2d1_1.h>         // the GPU tab strip (D3D11 + DXGI swap chain + D2D 1.1)
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>         // GPU glyphs for the strip labels
#include <commdlg.h>        // GetSaveFileNameA -- the diagnosis log's save dialog
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <algorithm>
#include <cwctype>
#include <cstdarg>

// v7.8.0: the one-file window header is RETIRED whole -- the window is a
// plain native WS_OVERLAPPEDWINDOW and the DPI child ledger lives in this
// file (tsTrackChild / tsRelayoutChildren, with the control factories
// below).

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// ------------------------------------------------------------- the shell ----
// The shell's own log ring (the GUI pane) + the log lock the engine's sink
// contract rides. twsLog lives in the core; this lock guards the strings
// the sink and the 250 ms GUI tick both touch.
static CRITICAL_SECTION g_logCS;
static bool     g_logInit   = false;
static unsigned g_logGen   = 0;    // one per print (the console status reads it)
static bool     g_logToFile = false;
#define twsLogGen g_logGen
#define twsLogToFile g_logToFile

// --------------------------------------------------------------- helpers ----
static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
static std::string lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}
static bool isOff(const std::string& v) {
    const std::string l = lower(trim(v));
    return l == "off" || l == "0" || l == "no" || l == "false";
}
static bool containsCI(const std::string& hay, const std::string& needle) {
    return lower(hay).find(lower(needle)) != std::string::npos;
}

static void cfgSanitize() {
    if (twsCfg.delay1Ms < 0.0f)   twsCfg.delay1Ms = 0.0f;
    if (twsCfg.delay1Ms > 1000.0f) twsCfg.delay1Ms = 1000.0f;
    if (twsCfg.delay2Ms < 0.0f)   twsCfg.delay2Ms = 0.0f;
    if (twsCfg.delay2Ms > 1000.0f) twsCfg.delay2Ms = 1000.0f;
    if (twsCfg.offset3Ms < 0.0f)   twsCfg.offset3Ms = 0.0f;
    if (twsCfg.offset3Ms > 1000.0f) twsCfg.offset3Ms = 1000.0f;
}

static int g_themeSaved = 0;
static int g_guiWinX = -99999, g_guiWinY = -99999;

static bool loadIni(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        size_t h = s.find_first_of("#;");
        if (h != std::string::npos) s = s.substr(0, h);
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower(trim(s.substr(0, eq)));
        std::string val = trim(s.substr(eq + 1));
        if (key == "out1ms" || key == "out2ms" || key == "buffer_size" ||
            key == "capture_exclusive" || key == "exclusive" ||
            key == "align_modes" || key == "clock_sync" ||
            key == "auto_resync" || key == "drift_guard" ||
            key == "power_guard" || key == "yield_default" ||
            key == "center_gate" || key == "center_upmix_guard" ||
            key == "drop_correct" ||
            key == "ring1_ms" || key == "ring2_ms" || key == "cap_ms" ||
            key == "volume_follow" || key == "drift_correct_ms" ||
            key == "drift_interval_ms" || key == "deep_resync_min" ||
            key == "ks_period_frames" || key == "ks_periods" ||
            key == "backend1" || key == "backend2" ||
            key == "background" || key == "pin_outputs" ||
            key == "lfe_fold" || key == "out2_first" ||
            key == "volume_endpoint" || key == "surround_bass" ||
            key == "surround_bass_freq" || key == "surround_bass_gain" ||
            key == "surround_bass_q" || key == "input_is_output") { }   // engine constants / retired / always-on
        else if (key == "input_device")   twsCfg.inputDevice = val;
        else if (key == "packet_mode")    twsCfg.packetMode = !isOff(val);
        else if (key == "output_device1") twsCfg.outputDevice1 = val;
        else if (key == "output_device2") twsCfg.outputDevice2 = val;
        else if (key == "output_device3") twsCfg.outputDevice3 = val;
        else if (key == "output_device4") twsCfg.outputDevice4 = val;
        else if (key == "gain_l")         twsCfg.gainL = (float)atof(val.c_str());
        else if (key == "gain_r")         twsCfg.gainR = (float)atof(val.c_str());
        else if (key == "gain_lfe")       twsCfg.gainLfe = (float)atof(val.c_str());
        else if (key == "gain_center")    twsCfg.gainCenter = (float)atof(val.c_str());
        else if (key == "gain_rl")        twsCfg.gainRl = (float)atof(val.c_str());
        else if (key == "gain_rr")        twsCfg.gainRr = (float)atof(val.c_str());
        else if (key == "gain_sl")        twsCfg.gainSl = (float)atof(val.c_str());
        else if (key == "gain_sr")        twsCfg.gainSr = (float)atof(val.c_str());
        else if (key == "delay1_ms")      twsCfg.delay1Ms = (float)atof(val.c_str());
        else if (key == "delay2_ms")      twsCfg.delay2Ms = (float)atof(val.c_str());
        else if (key == "offset3_ms")     twsCfg.offset3Ms = (float)atof(val.c_str());
        else if (key == "parallel_mix")   twsCfg.parallelMix = !isOff(val);
        else if (key == "chimes")         twsCfg.chimes = !isOff(val);
        else if (key == "chime_gain")     twsCfg.chimeGain = (float)atof(val.c_str());
        else if (key == "startup_engine") twsCfg.startupEngine = !isOff(val);
        else if (key == "gui_theme") {
            g_themeSaved = atoi(val.c_str());
            if (g_themeSaved < 0 || g_themeSaved > 10) g_themeSaved = 0;
        }
        else if (key == "gui_window_x") g_guiWinX = atoi(val.c_str());
        else if (key == "gui_window_y") g_guiWinY = atoi(val.c_str());
    }
    fclose(f);
    cfgSanitize();
    return true;
}

// the template: the simple four-leg routing, nothing else
static void writeTemplate(const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f,
        "; twinspeak v9.6.0 config -- the simple audio pipeline:\n"
        ";   input_device    the OUTPUT device to capture (render loopback)\n"
        ";   output_device1  leg1 (mains): FL/FR + the LFE fold\n"
        ";   output_device2  leg2 (center): ch3 on L/R; EMPTY = fold to leg1\n"
        ";   output_device3  leg3 (rears): ch7+ch5 L, ch8+ch6 R; EMPTY = the\n"
        ";                   rears fold to the MAINS\n"
        ";   output_device4  leg4 (LFE fold): names an EXISTING leg; that leg\n"
        ";                   plays gain_lfe*ch4. NEVER opens its own device.\n"
        "; Every leg runs the same flow (one period request, one anchor, one\n"
        "; depth servo, one cross-leg sync to leg1) so device delay drift is\n"
        "; ground out -- only delay_ms stays as the deliberate offset.\n"
        "\n"
        "input_device = CABLE Input (the source render endpoint)\n"
        "output_device1 = Speakers (the mains)\n"
        "output_device2 = (the center; EMPTY = fold onto the mains)\n"
        "output_device3 = (the rears; EMPTY = fold onto the mains)\n"
        "output_device4 = (the LFE fold target: an EXISTING leg's name; EMPTY = idle)\n"
        "\n"
        "; ALIGNMENT DELAY: delay1_ms / delay2_ms / offset3_ms push one leg's\n"
        "; output later, bit for bit. Put the delay on whichever leg arrives\n"
        "; EARLY (a USB center speaker lags the mains -> raise delay1_ms).\n"
        "delay1_ms = 0\n"
        "delay2_ms = 0\n"
        "offset3_ms = 0\n"
        "\n"
        "; the gains (1.0 = unity; 0 = silent lane)\n"
        "gain_l = 1.0\n"
        "gain_r = 1.0\n"
        "gain_lfe = 0.4\n"
        "gain_center = 1.0\n"
        "gain_rl = 1.0\n"
        "gain_rr = 1.0\n"
        "gain_sl = 1.0\n"
        "gain_sr = 1.0\n"
        "\n"
        "; the flow discipline (packet_mode bends rate sub-0.1%% on a clock\n"
        "; mismatch instead of drying the ring; parallel_mix = the futex crew)\n"
        "packet_mode = on\n"
        "parallel_mix = on\n"
        "\n"
        "; UI\n"
        "chimes = on\n"
        "chime_gain = 0.4\n"
        "startup_engine = on\n"
        "gui_theme = 0\n"
        "gui_window_x = -99999\n"
        "gui_window_y = -99999\n");
    fclose(f);
}

// ---------------------------------------------------------- list devices ----
// (the core owns the WASAPI walk; the shell just calls it)
static void listDevices() { twsListDevices(); }

// ============================================================ v6.6.0 GUI ====
// THE CONTROL WINDOW (no args; console modes unchanged behind flags).
// Hand-drawn black+cyan Win32 UI: SIX TABS covering every ini key; START/STOP
// runs the SAME engineMain in-process (the pane below is the live log);
// APPLY+RESTART saves + cycles in one click; SAVE splices values into the
// existing ini lines, preserving comments; CENTER EQ presets FLAT/DIALOGUE/
// BASS/WARMTH; device pickers from live enumeration (edit stays free-text).
// Edits/combos dark via DarkMode_Explorer, else WM_CTLCOLOR*; every
// button/toggle/tab is owner-drawn.
// ----------------------------------------------------------------------------
// ---- v7.0.0 THEMES: the palette is RUNTIME data -- eleven themes (v7.1.8
// adds DARK RED, DARK ORANGE, LIGHT RED), live-switched from the THEME
// button. applyTheme()
// copies the active record in + recreates the four brushes; painters read
// the globals fresh each frame, so a switch is one repaint, no restart.
struct GuiTheme {
    const char* name;
    COLORREF bg, panel, panel2, panelHi, line, text, dim;
    COLORREF accent, accentDk, onText, stop, log, logBg;
};
static const GuiTheme kThemes[11] = {
    //               bg                panel             panel2            panelHi           line              text              dim
    { "DARK CYAN",    RGB(  9, 10, 12), RGB( 16, 18, 21), RGB( 24, 27, 31), RGB( 31, 35, 40), RGB( 44, 49, 55), RGB(226,232,234), RGB(126,136,142),
      RGB(  0,229,238), RGB(  0,121,128), RGB(  4, 24, 26), RGB(255,105,115), RGB(150,222,214), RGB(  6,  7,  8) },
    { "DARK BLUE",    RGB(  8, 10, 17), RGB( 14, 17, 27), RGB( 21, 26, 40), RGB( 28, 34, 50), RGB( 41, 49, 74), RGB(222,230,244), RGB(122,133,158),
      RGB( 84,164,255), RGB( 32, 92,172), RGB(  4, 12, 26), RGB(255,105,115), RGB(150,192,236), RGB(  5,  6, 10) },
    { "DARK YELLOW",  RGB( 14, 13,  8), RGB( 21, 20, 13), RGB( 30, 28, 18), RGB( 38, 35, 23), RGB( 56, 52, 33), RGB(235,229,210), RGB(141,134,111),
      RGB(255,208, 64), RGB(158,124, 22), RGB( 27, 22,  4), RGB(255,105,115), RGB(226,200,122), RGB(  7,  6,  4) },
    { "DARK GREEN",   RGB(  8, 14, 10), RGB( 14, 22, 16), RGB( 21, 31, 23), RGB( 28, 39, 30), RGB( 41, 57, 45), RGB(223,237,227), RGB(123,141,129),
      RGB( 62,220,132), RGB( 26,132, 72), RGB(  4, 26, 14), RGB(255,105,115), RGB(151,221,182), RGB(  5,  9,  6) },
    { "LIGHT GREEN",  RGB(242,247,240), RGB(232,240,229), RGB(221,232,217), RGB(208,222,203), RGB(152,171,147), RGB( 27, 39, 29), RGB(101,118,101),
      RGB( 26,138, 58), RGB( 20,104, 44), RGB(244,251,243), RGB(199, 38, 52), RGB( 22, 92, 44), RGB(238,245,236) },
    { "LIGHT ORANGE", RGB(248,243,236), RGB(239,231,219), RGB(229,218,200), RGB(216,203,181), RGB(176,155,124), RGB( 46, 35, 20), RGB(132,116, 90),
      RGB(204,108, 18), RGB(158, 78,  8), RGB(252,247,240), RGB(190, 42, 38), RGB(142, 74, 10), RGB(244,238,228) },
    { "LIGHT GRAY",   RGB(243,244,246), RGB(233,236,238), RGB(222,226,229), RGB(209,213,217), RGB(150,158,165), RGB( 30, 34, 40), RGB(110,118,126),
      RGB( 66,104,158), RGB( 48, 78,120), RGB(249,250,251), RGB(195, 44, 55), RGB( 40, 68,105), RGB(238,240,242) },
    { "LIGHT CYAN",   RGB(237,245,247), RGB(225,238,242), RGB(213,229,235), RGB(198,219,227), RGB(130,164,174), RGB( 14, 39, 45), RGB( 84,114,121),
      RGB(  0,128,142), RGB(  0, 99,111), RGB(245,251,252), RGB(199, 44, 59), RGB(  0,104,118), RGB(231,241,244) },
    // v7.1.8: the three new palettes APPEND here -- saved gui_theme indices
    // 0..7 keep their themes, nothing shifts under an existing ini.
    { "DARK RED",     RGB( 14,  8,  8), RGB( 22, 13, 13), RGB( 31, 19, 19), RGB( 40, 25, 25), RGB( 60, 37, 37), RGB(235,225,225), RGB(145,124,124),
      RGB(255, 92, 92), RGB(164, 30, 30), RGB( 28,  6,  6), RGB(255,105,115), RGB(226,150,150), RGB(  8,  5,  5) },
    { "DARK ORANGE",  RGB( 14, 10,  6), RGB( 22, 16, 10), RGB( 31, 23, 15), RGB( 40, 30, 20), RGB( 60, 46, 30), RGB(236,229,216), RGB(146,132,110),
      RGB(255,150, 40), RGB(164, 86, 10), RGB( 28, 16,  4), RGB(255,105,115), RGB(230,190,140), RGB(  8,  6,  4) },
    { "LIGHT RED",    RGB(248,241,241), RGB(240,230,230), RGB(231,218,218), RGB(220,205,205), RGB(180,148,148), RGB( 48, 22, 22), RGB(136,104,104),
      RGB(202, 52, 52), RGB(156, 36, 36), RGB(250,244,244), RGB(199, 38, 52), RGB(140, 44, 48), RGB(244,238,238) },
};
static int g_theme = 0;   // active kThemes index

namespace guic {
    COLORREF cBg        = RGB(9, 10, 12);      // window base (seeded with DARK CYAN;
    COLORREF cPanel     = RGB(16, 18, 21);     // applyTheme() owns the values)
    COLORREF cPanel2    = RGB(24, 27, 31);
    COLORREF cPanelHi   = RGB(31, 35, 40);
    COLORREF cLine      = RGB(44, 49, 55);
    COLORREF cText      = RGB(226, 232, 234);
    COLORREF cDim       = RGB(126, 136, 142);
    COLORREF cAccent    = RGB(0, 229, 238);
    COLORREF cAccentDk  = RGB(0, 121, 128);
    COLORREF cOnText    = RGB(4, 24, 26);
    COLORREF cStop      = RGB(255, 105, 115);
    COLORREF cLog       = RGB(150, 222, 214);
    COLORREF cLogBg     = RGB(6, 7, 8);
}

// ---- control ids -----------------------------------------------------------
enum {
    IDC_TABSTRIP = 96,          // v7.0.4: the whole strip is ONE custom control
    IDC_TAB0 = 100,             // v7.0.4: retired (the strip owns the tabs) --
                                // the ids stay reserved; nothing creates them
    IDC_START = 200, IDC_APPLY = 201, IDC_SAVE = 202, IDC_STATUS = 203,
    IDC_LOG = 204,              // v9.1.5: RETIRED -- the pane is gone; the id stays reserved
    IDC_REFRESH = 205, IDC_THEME = 206, IDC_TICK = 207,
    IDC_SAVLOG = 208,           // v9.1.5: DOWNLOAD LOG FOR DIAGNOSIS (the save-file dialog)
    // page: configuration (v8.3.0: was devices -- the chime pills from the
    // late SETTINGS page live here; v8.4.2: the startup row is this page's
    // LAST row)
    IDC_INDEV = 300, IDC_OUT1, IDC_OUT2, IDC_VOLEP,   // v9.5.1: IDC_VOLEP retired
                                             // (volume = the cable input's own slider)
                                             // -- id reserved, nothing creates it
    IDC_GAIN_L, IDC_GAIN_R, IDC_GAIN_LFE, IDC_GAIN_CTR,
    IDC_TGL_LFEFOLD,            // v8.4.0: retired (the fold is built in) --
                                 // id reserved, nothing creates it
    // page: timing (v8.2.0: unified -- the transport rows live here too;
    // IDC_OUT1MS/IDC_OUT2MS retired like IDC_CAPMS/RING1/RING2 before them)
    IDC_OUT1MS, IDC_OUT2MS, IDC_CAPMS, IDC_RING1, IDC_RING2,
    IDC_DELAY1, IDC_DELAY2, IDC_TGL_OUT2FIRST,   // v7.0.0: IDC_TGL_PINOUT retired (integrated)
    IDC_KSPF, IDC_KSPD,         // v8.4.5: retired with the calm bite (BUILT
                                // IN) -- ids stay reserved, nothing creates
                                // them (v8.2.0-8.4.4: the rows lived on
                                // TIMING; v7.1.7: IDC_BE1/IDC_BE2 retired
                                // before them, the backend keys are ini-only)
    // v8.3.0: the SETTINGS page is GONE -- the chime pills live on page 0
    // (v8.4.2: the startup row is page 0's last row); the drift trio +
    // volume_follow ids are retired with their keys
    // (reserved here like IDC_OUT1MS before them, nothing creates them).
    IDC_TGL_STARTUP,            // v6.6.6: Run-key pill (v8.4.2: the last row on page 0)
    IDC_DCORR, IDC_DINTERVAL, IDC_DEEPRESYNC,   // v8.3.0: retired (hardcoded)
    IDC_TGL_VOLFOLLOW, IDC_TGL_CHIMES,          // v8.3.0: VOLFOLLOW retired; CHIMES on page 0
    IDC_CHIMGAIN,               // v8.3.0: lives on page 0 (v7.1.7: KAHZ/KAGAIN retired)
    // page: center eq
    IDC_TGL_EQ,
    IDC_SLOT1, IDC_SLOT2, IDC_SLOT3, IDC_SLOT4, IDC_SLOT5,   // v7.1.8: save slots
                                                             // (IDC_PRE_FLAT..IDC_PRE_WARM retired)
    IDC_EQSAVE,                 // v7.1.9: SAVE TO SLOT (writes the armed slot)
    IDC_SLD_FREQ, IDC_SLD_GAIN, IDC_SLD_Q,
    IDC_LBL_FREQ, IDC_LBL_GAIN, IDC_LBL_Q,
    // v8.4.0: the four-leg controls -- the rear device combos, the gain
    // quad's two new edits, the timing quad's two offset edits.
    IDC_OUT3, IDC_GAIN_RL, IDC_GAIN_RR, IDC_OFF3,   // v8.4.11: IDC_OUT4/IDC_OFF4 retired with leg4
    IDC_OUT4,                                       // v9.1.4: leg4 is BACK -- the LFE fold target combo
    IDC_GAIN_SL, IDC_GAIN_SR,                       // v8.5.0: the surround gains (the second quad)
};

// ---- gui state -------------------------------------------------------------
static HWND      g_hwnd          = nullptr;
static HFONT     g_fUI = nullptr, g_fUIb = nullptr,
                 g_fSml = nullptr, g_fMono = nullptr;
// v7.8.0: g_fTitle is RETIRED with the custom caption -- the native title
// bar draws its own title with the system caption font; no per-window
// caption font API exists on any Windows.
static HBRUSH    g_brBg = nullptr, g_brPanel = nullptr, g_brPanel2 = nullptr,
                 g_brLog = nullptr;
static std::string g_guiIniPath;
static int       g_tab           = 0;
// v7.0.3: the tab fade-in. guiShowPage anchors the clock and arms the 16 ms
// WM_TIMER id 2; the frame's halo and the selected tab's face/border/label
// read their strength from guiTabFade() (ease-out cubic). 0 = settled -- every
// paint then reads full strength, exactly the pre-animation look.
static ULONGLONG g_tabAnimStart  = 0;
static bool      g_fadePeriod    = false;   // v7.0.5: timeBeginPeriod(1) refcount-of-one
static const int KTAB_FADE_MS    = 170;
static double guiTabFade() {
    if (!g_tabAnimStart) return 1.0;
    const ULONGLONG dt = GetTickCount64() - g_tabAnimStart;
    if (dt >= (ULONGLONG)KTAB_FADE_MS) return 1.0;
    const double t = (double)dt / (double)KTAB_FADE_MS;
    return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
}
static COLORREF guiLerpRGB(COLORREF a, COLORREF b, double t) {
    return RGB(GetRValue(a) + (int)((GetRValue(b) - GetRValue(a)) * t),
               GetGValue(a) + (int)((GetGValue(b) - GetGValue(a)) * t),
               GetBValue(a) + (int)((GetBValue(b) - GetBValue(a)) * t));
}
static std::map<int, bool> g_tog;            // toggle button states
static int       g_eqSel         = -1;         // v7.1.10: THE selected slot -- apply, save
                                               // and the accent all read this ONE var
                                               // (v7.1.9's armed/match pair could light
                                               // TWO faces at once; one var, one accent)
static std::vector<HWND> g_pageHw[3];          // children per page (show/hide; v8.3.0: THREE pages)
static HANDLE    g_engThread     = nullptr;
static bool      g_applyRestart  = false;   // v7.1.20: APPLY+RESTART chains off WM_APP+2
static std::atomic<int>  g_engState{0};        // 0 stopped, 1 starting, 2 running, 3 stopping
static std::atomic<int>  g_engRC{0};
static std::string g_logRing;                  // pending log lines (g_logCS-locked)
// v9.1.5 THE PANE IS GONE: the log has no GUI surface -- the uncapped
// g_logRing above (v9.1.2's promise stands) is the WHOLE engine history,
// and the DOWNLOAD LOG FOR DIAGNOSIS button writes it wherever the user
// points. The health line keeps its own g_lastHealth slot (it feeds the
// status band, never the log).
static std::string g_lastHealth;               // latest "out1 ..." health line
// v6.6.1: tray + startup
static NOTIFYICONDATAA g_nid = {};
static ULONG_PTR g_gdipToken = 0;             // v6.6.5: GDI+ session token
static bool g_trayVisible = false;             // icon currently in the tray
static bool g_startTray   = false;             // --tray: begin hidden in the tray
static bool g_userShown   = false;             // v7.1.5: the USER opened the window
                                               // (tray dbl-click / menu / second
                                               // instance) -- startup shows and the
                                               // boot yield never set it
// v7.1.6 TRAY-FIRST: the invisible tray-owner window. A --tray (startup)
// launch builds NO control window at all -- this surrogate (same window
// class, never shown) carries the tray icon and the second-instance
// messages, so a boot pop is structurally impossible: there is no window
// to show. The real control panel is built on demand (guiCreateRealWindow)
// the first time the user clicks the icon, opens the tray menu, or a
// second instance asks for the window.
static HWND g_trayWnd = nullptr;
static UINT g_taskbarCreated = 0;              // explorer restart re-adds the icon

// v7.1.5 THE BOOT POP, round two: a plain (no --tray) duplicate. Windows'
// app-restart machinery and Startup-folder shortcuts replay the exe WITHOUT
// --tray, so the v7.1.5 "--tray duplicates exit silently" gate misses them:
// one wins the mutex race and shows its own window, or signals WM_APP+4 and
// pops the hidden primary. The tell: boot machinery fires right after the
// shell comes up. This asks the shell (tray window -> explorer process) how
// long ago IT started: within kBootWindowMs of shell start = the logon
// window. Not a perfect user/boot oracle, but the tray window itself is the
// thing "the taskbar completes initialization" waits for, so boot duplicates
// always land deep inside it.
static bool withinLogonWindow(unsigned long long msWindow) {
    HWND sh = FindWindowA("Shell_TrayWnd", nullptr);
    if (!sh) sh = FindWindowA("Progman", nullptr);
    if (!sh) return false;                     // shell not up yet: not boot context
    DWORD pid = 0;
    GetWindowThreadProcessId(sh, &pid);
    if (!pid) return false;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return false;
    FILETIME ct, ex, kt, ut, now;
    const BOOL ok = GetProcessTimes(p, &ct, &ex, &kt, &ut);
    CloseHandle(p);
    if (!ok) return false;
    GetSystemTimeAsFileTime(&now);
    const ULONGLONG shellStart = (ULONGLONG)ct.dwLowDateTime
                               | ((ULONGLONG)ct.dwHighDateTime << 32);
    const ULONGLONG nowFt      = (ULONGLONG)now.dwLowDateTime
                               | ((ULONGLONG)now.dwHighDateTime << 32);
    if (nowFt <= shellStart) return false;
    const ULONGLONG shellAgeMs = (nowFt - shellStart) / 10000ULL;   // 100 ns -> ms
    return shellAgeMs < msWindow;
}
static constexpr unsigned long long kBootWindowMs = 120000;   // 2 min after the shell came up

// v6.6.1 forward decls (helpers defined below the page builder)
static void guiRepaint(HWND w, int id);
static void guiRepaintSlots(HWND w);
static void trayAdd();               // v7.5.1: no hint param -- the hide confirmation
                                     // lives in WM_CLOSE alone, every close
static void trayRemove();
static void guiRestoreWindow();      // v7.1.6: fwd -- the tray come-up
static void guiHideToTray(HWND w);   // v7.1.6: fwd -- the close-to-tray gesture
                                     // header's void(*)(HWND) show signature
static void trayUpdateTip(bool running);   // v7.1.20: the tip tracks the engine
static void trayBalloon(const char* text); // v7.2.12: fwd -- the tray path's voice (start refusals)
static bool guiCreateRealWindow();   // v7.1.6 tray-first: deferred build, on demand
// v6.6.1 tray context-menu commands
enum { IDM_TRAY_OPEN = 900, IDM_TRAY_START, IDM_TRAY_AUTORUN, IDM_TRAY_EXIT };

// v8.3.0: THREE tabs. The SETTINGS page is GONE -- its chime controls live
// on page 0 (v8.4.2: the startup row is page 0's last row)
// and the drift trio + volume_follow went with their keys (hardcoded from
// the fielded ini: 5 / 5000 / 30 / on). With nothing left to configure
// under the old label, page 0 says what it now is:
// CONFIGURATION | TIMING | CENTER EQ.
static const char* kTabNames[3] = {
    "CONFIGURATION", "TIMING", "CENTER EQ"
};

// v6.6.4: only a close button -- fixed-size layout, close already hides to the
// tray. v7.0.9: the caption itself is custom. v7.1.17 THE FRAME THAT CANNOT
// FLASH: WM_NCCALCSIZE returns the WHOLE window as client -- no NC pixel
// anywhere for any frame painter to draw into; the bar is the client's top
// 31 px, ours forever, structurally.
// v7.1.18 THE UNFOCUS FRAME, RETIRED AT THE SOURCE: field report -- on
// losing focus the window still grew a full Luna caption (blue gradient,
// icon + title text, glossy red close button) over the bar, PERSISTENT.
// The window had no NC area, WM_NCACTIVATE answered DefWindowProc with
// lParam -1, WM_NCPAINT/0x00AE/0x00AF were all swallowed -- and the themed
// frame painter still ran on deactivation (machines with customized visual
// styles take that path regardless of the -1 convention). The caption
// existed to be painted because the window still carried WS_CAPTION: any
// themed frame engine may paint it whenever it likes. So the style goes:
// no WS_CAPTION, no caption FOR ANYONE to draw -- the artifact becomes
// structurally impossible, same argument as v7.1.17's NC removal. The DWM
// shadow survives on WS_THICKFRAME (the canonical borderless recipe:
// WS_POPUP/WS_SYSMENU + WS_THICKFRAME + WM_NCCALCSIZE 0 = the standard
// Win10 drop shadow). WS_SYSMENU stays for Alt+Space and the HTCAPTION
// right-click sysmenu; the window is fixed-size, so resize edges are
// meaningless -- WM_NCHITTEST answers HTCLIENT below the bar.
// ---- v7.1.20 DPI SCALE ------------------------------------------------------
// The manifest makes the process System-DPI-aware; these translate the
// 96-dpi design pixels into real pixels exactly once per launch. Every
// window-geometry constant goes through S() (integers) / Sf() (floats);
// the named constants below are mutable and re-derived in guiInitDpi()
// -- call sites keep their literal, design-space meaning.
static UINT g_dpi96 = 96;
static int  S(int v)          { return MulDiv(v, (int)g_dpi96, 96); }
static float Sf(float v)      { return v * (float)g_dpi96 / 96.0f; }
// v7.1.20: the scaled geometry lives HERE (single definitions -- the old
// const definitions farther down the file are gone).
static int KCLI_W     = 880, KCLI_H      = 542;        // designed client size (v9.1.5: the pane is gone -- the hint band sits under the status band)
static int KPANEL_T   = 66,  KPANEL_B    = 402;        // page panel band
static int KLOG_BOTTOM= 516;                            // v9.1.5: the hint band's top (was the pane's bottom; v9.0.2: 770)
static int kTabW[3]   = { 283, 283, 283 };   // v8.3.0: three faces fill the strip (3*283+2*4 = 857)
static int kTabGap    = 4, KSTRIP_PAD = 5, KSTRIP_FACE_Y = 6;
// v7.8.0: the in-client caption band is RETIRED with the custom chrome --
// the client is pure content space (880x620 design px, y = 0 the first row
// under the NATIVE caption). The tab strip's GPU buffer keeps its own
// g_stripW/g_stripH -- unrelated.
// guiInitDpi runs FIRST in guiMain -- before guiMakeGdi (fonts) and before
// any window exists. v7.5.3d: the manifest is PerMonitorV2 now and the
// WM_DPICHANGED hook re-runs the derivation at the new scale -- the
// "system-aware, fixed for the process lifetime" rule is retired.
static void guiApplyDpi() {
    KCLI_W     = S(880);
    KCLI_H     = S(542);
    KPANEL_T   = S(66);
    KPANEL_B   = S(402);
    KLOG_BOTTOM= S(516);
    kTabW[0] = kTabW[1] = kTabW[2] = S(283);
    kTabGap     = S(4);
    KSTRIP_PAD  = S(5);
    KSTRIP_FACE_Y = S(6);
}
static void guiInitDpi() {
    HDC sdc = GetDC(nullptr);
    if (sdc) {
        const int d = GetDeviceCaps(sdc, LOGPIXELSX);
        if (d > 0) g_dpi96 = (UINT)d;
        ReleaseDC(nullptr, sdc);
    }
    guiApplyDpi();
}

// v7.6.12 THE NATIVE RESTART -- the field: "I would just start over and not
// rely on a custom window (if at all), and try to draw something just below
// the titlebar buttons." The window is a PLAIN NATIVE WS_OVERLAPPEDWINDOW:
// the caption, the icon, the three caption buttons, their hover states, the
// red close, the snap layouts, the maximize, the resize edges, the shadow,
// the activation fades and the DPI story are all WINDOWS' OWN -- pixel-
// perfect by definition at every scale, with nothing left to chase. The
// v7.1.24 main.c recipe (WS_THICKFRAME | WS_SYSMENU without WS_CAPTION +
// the WM_NCCALCSIZE insets + the in-client bar) is retired whole.
// v7.8.0 finishes it: the one-file window header that carried the ledger
// is retired too -- tsTrackChild / tsRelayoutChildren (this file) are the
// one piece of plumbing kept.

// v7.6.12: the frame-inset calculator is retired with the custom NC
// geometry -- AdjustWindowRectEx answers the creation size for the full
// native frame, and no WM_WINDOWPOSCHANGING pin or GETMINMAXINFO clamp
// hangs off hand-computed insets anymore.

// v7.6.12: THE BAR MACHINERY IS RETIRED WHOLE. The memory bar buffer
// (guiBarBufEnsure), the glass-sheet state and its byte patch, the
// activation force latches, guiRenderBar and guiPaintBar all go: the
// native caption paints itself, and nothing in this file answers for it
// anymore. ("I would just start over and not rely on a custom window (if
// at all), and try to draw something just below the titlebar buttons.")

static Gdiplus::Color GCol(COLORREF c, BYTE a = 255);   // fwd (defined below)

// ---- fonts / brushes -------------------------------------------------------
static void applyTheme(int i);   // v7.0.0: forward decl (defined right below guiMakeGdi)
// v7.5.3d: re-runnable -- the WM_DPICHANGED hook re-creates the four fonts
// at the new scale (the old ones die first).
static void guiMakeFonts() {
    if (g_fUI)   { DeleteObject(g_fUI);   g_fUI   = nullptr; }
    if (g_fUIb)  { DeleteObject(g_fUIb);  g_fUIb  = nullptr; }
    if (g_fSml)  { DeleteObject(g_fSml);  g_fSml  = nullptr; }
    if (g_fMono) { DeleteObject(g_fMono); g_fMono = nullptr; }
    g_fUI   = CreateFontA(-S(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_fUIb  = CreateFontA(-S(13), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_fSml  = CreateFontA(-S(11), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_fMono = CreateFontA(-S(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          FIXED_PITCH | FF_MODERN, "Consolas");
    // v7.8.0: the NONCLIENTMETRICS caption font is retired with the custom
    // caption -- the native title bar owns its own title text.
}

static void guiMakeGdi() {
    // v7.1.8: the Marlett close-glyph font is RETIRED -- the bar's X is the
    // modern thin caption shape, two antialiased GDI+ pen strokes (see
    // guiRenderBar). (The Times New Roman header font is retired with the
    // in-window header text it served.)
    // v7.1.20: the pixel heights scale with the DPI (S of the design px).
    guiMakeFonts();
    applyTheme(g_themeSaved);   // v7.0.0: the theme owns the brushes now; v7.0.4: the ini's gui_theme wins (default 0 = DARK CYAN)
}

// v7.6.12: guiOnDpiChanged is retired -- the WM_DPICHANGED case in
// guiWndProc does the whole standard dance now (suggested rect, scale,
// fonts, ledger re-seat). The strip's GPU-state release is still needed
// there:
static void guiStripGpuRelease();   // v7.0.7 (defined with the tab strip)

// (v7.0.6's DWM dark-mode caption helper is retired with the DWM caption
// itself -- v7.0.9 paints the bar from the live guic:: colors instead.)
static void guiApplyDwmMode();   // v7.7.5: THE DIAL -- defined with the frame section

// v7.8.0: the native caption takes its dark/light look from the immersive
// dark-mode attribute (guiApplyDwmMode below re-dials it from the theme on
// every switch); DWMWA_CAPTION_COLOR/DWMWA_TEXT_COLOR stay untouched -- on
// Windows 10 they fail silently and no per-window caption font API exists
// on any Windows. The client face reads guic:: live, so a theme switch
// needs no re-seed call at all -- the RDW below repaints it like any other
// client pixel.

// v7.0.0: live theme switch -- commit colors + brushes, then ONE synchronous
// repaint of the whole tree, bar included, so nothing can show old colors.
static void applyTheme(int i) {
    g_theme = i % 11;   // v7.1.8: eleven themes
    const GuiTheme& t = kThemes[g_theme];
    guic::cBg = t.bg;           guic::cPanel   = t.panel;   guic::cPanel2 = t.panel2;
    guic::cPanelHi = t.panelHi; guic::cLine    = t.line;    guic::cText   = t.text;
    guic::cDim = t.dim;         guic::cAccent  = t.accent;  guic::cAccentDk = t.accentDk;
    guic::cOnText = t.onText;   guic::cStop    = t.stop;    guic::cLog    = t.log;
    guic::cLogBg = t.logBg;
    #define TSREBR(b, c) do { if (b) DeleteObject(b); (b) = CreateSolidBrush(c); } while (0)
    TSREBR(g_brBg,     guic::cBg);
    TSREBR(g_brPanel,  guic::cPanel);
    TSREBR(g_brPanel2, guic::cPanel2);
    TSREBR(g_brLog,    guic::cLogBg);
    #undef TSREBR
    // v7.7.5 THE DIAL (v7.8.0: still the law): the theme changed, and with
    // it the titlebar's mode -- the theme background's HSL lightness re-picks
    // the immersive dark-mode attribute right here, in the same transaction
    // as the colors and the brushes, so the NATIVE caption follows the theme
    // (dark themes -> dark titlebar + white ink, light themes -> the
    // reverse).
    guiApplyDwmMode();
    if (g_hwnd) {
        SetWindowTextA(GetDlgItem(g_hwnd, IDC_THEME), t.name);
        // v7.8.0: no bar to recolor synchronously -- the native caption
        // renders its own active/inactive states, and the RDW_FRAME flag
        // below repaints it against the freshly dialed mode.
        RedrawWindow(g_hwnd, nullptr, nullptr,
                     RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
        // v7.0.9: the tabbar's new colors must be ON SCREEN before the click
        // handler returns. The RDW_ALLCHILDREN pass above repaints the strip
        // in theory, but three field reports in a row say the tabbar lands
        // late on real hardware -- so the strip now gets an EXPLICIT
        // invalidate + UpdateWindow: a synchronous WM_PAINT that runs the
        // GPU pass and its Present right here, with the new guic:: colors
        // already copied. No dependency on paint ordering, RDW flags or DWM
        // catching up.
        if (HWND ts = GetDlgItem(g_hwnd, IDC_TABSTRIP)) {
            InvalidateRect(ts, nullptr, FALSE);
            UpdateWindow(ts);
        }
    }
}

// ---- v6.6.5 CONDENSED GEOMETRY: the whole frame is tighter -- client 880x620
// (was 1000x750), 34px row pitch (was 38), slimmer bars everywhere.
static const int COLA_X = 24, COLB_X = 460, LBL_W = 160, CTL_W = 200;
// (KCLI_W/KCLI_H/KLOG_BOTTOM/KPANEL_T/KPANEL_B are defined -- and scaled --
// in the v7.1.20 DPI block above.)
static const int KHDR_Y = 94, KROW0 = 120;   // page header / first row / pitch

// ---- small drawing helpers (v6.6.5: GDI+ -- everything the eye sees is AA) --
static Gdiplus::Color GCol(COLORREF c, BYTE a) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

// v7.8.0: GBand (the band's 254-alpha clamp) is retired with the caption
// band -- no extend sheet exists anymore, every client fill rides the
// normal opaque path.

// rounded-rect path out of four arcs -- the AA replacement for guiRoundRect
static Gdiplus::GraphicsPath* guiRoundPathInset(const Gdiplus::Rect& rin, int rad, int inset) {
    // v6.6.7: the path is inset 1px inside the item rect -- a stroke ON the
    // boundary loses its outer half to the control DC clip; inset, the full
    // stroke shows and its AA blends against our own dark erase.
    const Gdiplus::Rect r(rin.X + inset, rin.Y + inset,
                          rin.Width - 2 * inset, rin.Height - 2 * inset);
    Gdiplus::GraphicsPath* p = new Gdiplus::GraphicsPath();
    const int d = rad * 2;
    p->AddArc(r.X, r.Y, d, d, 180, 90);
    p->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p->CloseFigure();
    return p;
}
static Gdiplus::GraphicsPath* guiRoundPath(const Gdiplus::Rect& rin, int rad) {
    return guiRoundPathInset(rin, rad, 1);
}

static std::wstring guiWide(const char* s) {
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

// antialiased single-line text; align: 0 center, 1 left, 2 right
static void guiAAString(Gdiplus::Graphics& g, HDC dc, const RECT& rc, const char* txt,
                        COLORREF col, HFONT f, int align) {
    const std::wstring wt = guiWide(txt);
    if (wt.empty()) return;
    Gdiplus::Font gf(dc, f);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(align == 1 ? Gdiplus::StringAlignmentNear :
                    align == 2 ? Gdiplus::StringAlignmentFar : Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoFitBlackBox);
    sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::SolidBrush br(GCol(col));
    const Gdiplus::RectF rf((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                            (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));
    g.DrawString(wt.c_str(), (INT)wt.size(), &gf, rf, &sf, &br);
}

// ---- v6.6.5: the TidyVision watermark (GDI+) --------------------------------
// Bottom-right, Arial Bold, antialiased, filled with a TRUE vertical linear
// gradient brush (top orange -> bottom cyan). Drawn inside WM_PAINT, so it
// never flickers. Replaces the v6.6.3/4 clip-path + pattern-brush build.
static void guiWatermark(Gdiplus::Graphics& g) {
    static const char*  kWM = "Property of TidyVision - Copyright 2026-beyond";
    static const std::wstring wt = guiWide(kWM);
    if (wt.empty() || !g_hwnd) return;
    RECT crc = { 0, 0, 880, 620 };
    GetClientRect(g_hwnd, &crc);
    // v7.8.0: plain client space -- no caption band to subtract; the
    // watermark centers in the strip between the log pane and the client
    // bottom edge.
    Gdiplus::FontFamily ff(L"Arial");
    Gdiplus::Font f(&ff, (Gdiplus::REAL)S(13), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentNear);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap |
                      Gdiplus::StringFormatFlagsNoFitBlackBox);
    // v7.1.5 THE CACHED MEASURE: MeasureString ran on EVERY frame render --
    // every hover, every focus ring, every status tick paid a GDI+ layout
    // pass for text that never changes, and that cost landed between mouse
    // moves (the field: "mouse inputs too spaced out in time"). The window
    // is fixed-size; measure ONCE, reuse forever.
    static Gdiplus::RectF mrCached(0, 0, 0, 0);
    static bool measured = false;
    if (!measured) {
        const Gdiplus::RectF lay(0, 0, (Gdiplus::REAL)crc.right, (Gdiplus::REAL)crc.bottom);
        g.MeasureString(wt.c_str(), (INT)wt.size(), &f, lay, &sf, &mrCached);
        measured = true;
    }
    const Gdiplus::RectF& mr = mrCached;
    if (mr.Width <= 0) return;
    // centered in the strip below the log pane; right edge 14px off the client edge
    const Gdiplus::REAL x = (Gdiplus::REAL)(crc.right - S(14)) - mr.Width;
    const Gdiplus::REAL y = (Gdiplus::REAL)KLOG_BOTTOM
                          + ((Gdiplus::REAL)(crc.bottom - KLOG_BOTTOM) - mr.Height) / 2.0f;
    const Gdiplus::RectF tr(x, y, mr.Width, mr.Height);
    Gdiplus::LinearGradientBrush lb(tr, GCol(RGB(255, 138, 42)),
                                    GCol(RGB(0, 229, 238)), Gdiplus::LinearGradientModeVertical);
    g.DrawString(wt.c_str(), (INT)wt.size(), &f, tr, &sf, &lb);
}

// ---- control factories -----------------------------------------------------

static void regPage(HWND h) { g_pageHw[g_tab].push_back(h); }

// DarkMode_Explorer when the OS has it (1809+); controls without it fall
// back to the WM_CTLCOLOR* handlers below -- both land on the same palette.
static void guiDarkCtl(HWND h) {
    typedef HRESULT (WINAPI *SWTFN)(HWND, LPCWSTR, LPCWSTR);
    static SWTFN f = nullptr;
    if (!f) { HMODULE ux = LoadLibraryA("uxtheme.dll"); if (ux) f = (SWTFN)GetProcAddress(ux, "SetWindowTheme"); }
    if (f) f(h, L"DarkMode_Explorer", nullptr);
}

// ---- v9.1.5: the log pane's flicker buffer is RETIRED with the pane -------
// (v7.1.5's subclassed memory-blit painter lived and died with the EDIT
// control; the log is the ring now, saved on demand.)

// ---- v7.8.0: the DPI child ledger (local) ------------------------------------
// Children register here in DESIGN px; on WM_DPICHANGED tsRelayoutChildren()
// re-positions every tracked child at the new scale, re-seats its font (a
// pointer into the host's HFONT slot -- guiMakeFonts re-creates the font
// first) and runs its per-child fixup (combobox item heights). With the
// native caption owning the titlebar, the ledger's coordinates are plain
// client space -- y = 0 is the first content row below the native frame.
struct TsKid {
    HWND    h;
    int     x, y, w, ht;        // design px, client space
    HFONT*  font;               // the host's font slot, re-sent after a move
    int     comboItemH;         // > 0: CB_SETITEMHEIGHT(-1) after the move
};
static std::vector<TsKid> g_kids;
static void tsTrackChild(HWND h, int x, int y, int w, int ht,
                         HFONT* font = nullptr, int comboItemH = 0) {
    for (auto& k : g_kids)
        if (k.h == h) { k.x = x; k.y = y; k.w = w; k.ht = ht;
                        k.font = font; k.comboItemH = comboItemH; return; }
    g_kids.push_back({ h, x, y, w, ht, font, comboItemH });
}
static void tsRelayoutChildren() {
    for (auto& k : g_kids) {
        if (!k.h || !IsWindow(k.h)) continue;
        SetWindowPos(k.h, nullptr,
                     S(k.x), S(k.y), S(k.w), S(k.ht),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (k.font && *k.font) SendMessageW(k.h, WM_SETFONT, (WPARAM)*k.font, TRUE);
        if (k.comboItemH > 0) {
            SendMessageW(k.h, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)S(k.comboItemH));
            InvalidateRect(k.h, nullptr, FALSE);
        }
    }
}

static HWND mkLabel(HWND w, int id, int x, int y, int cw, const char* txt, HFONT f, int ch = 20) {
    // v7.1.20: design px in, real px out (DPI scale).
    // v7.8.0: plain client space -- the native caption owns the titlebar,
    // so y = 0 is the first content row (no strip lead).
    const int dx = x, dy = y, dw2 = cw, dh = ch;
    x = S(x); y = S(y); cw = S(cw); ch = S(ch);
    HWND h = CreateWindowExA(0, "STATIC", txt, WS_CHILD | WS_VISIBLE,
                             x, y, cw, ch, w, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)(f ? f : g_fUI), TRUE);
    tsTrackChild(h, dx, dy, dw2, dh, (f == g_fUIb) ? &g_fUIb : &g_fUI);
    regPage(h);
    return h;
}

static HWND mkEdit(HWND w, int id, int x, int y, int cw, int ch, const char* val) {
    const int dx = x, dy = y, dw2 = cw, dh = ch;
    x = S(x); y = S(y); cw = S(cw); ch = S(ch);   // v7.1.20: DPI scale
    HWND h = CreateWindowExA(0, "EDIT", val,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        x, y, cw, ch, w, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_fUI, TRUE);
    guiDarkCtl(h);
    tsTrackChild(h, dx, dy, dw2, dh, &g_fUI);
    regPage(h);
    return h;
}

static HWND mkBtn(HWND w, int id, int x, int y, int cw, int ch, const char* txt) {
    // NOTE: deliberately NOT regPage()d here -- the tab strip and the
    // START/APPLY/SAVE bar live on every page. Page-bound buttons (toggles,
    // presets, REFRESH) are registered by their builders (v6.6.2).
    const int dx = x, dy = y, dw2 = cw, dh = ch;
    x = S(x); y = S(y); cw = S(cw); ch = S(ch);   // v7.1.20: DPI scale
    HWND h = CreateWindowExA(0, "BUTTON", txt,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        x, y, cw, ch, w, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_fUIb, TRUE);
    tsTrackChild(h, dx, dy, dw2, dh, &g_fUIb);
    return h;
}

static HWND mkCombo(HWND w, int id, int x, int y, bool listOnly) {
    const int dx = x, dy = y;
    x = S(x); y = S(y);   // v7.1.20: DPI scale
    HWND h = CreateWindowExA(0, "COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_AUTOHSCROLL |
        (listOnly ? CBS_DROPDOWNLIST : CBS_DROPDOWN),
        x, y, S(CTL_W), S(220), w, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_fUI, TRUE);
    SendMessageA(h, CB_SETITEMHEIGHT, (WPARAM)-1, S(16));
    guiDarkCtl(h);
    tsTrackChild(h, dx, dy, CTL_W, 220, &g_fUI, 16);
    regPage(h);
    return h;
}

// ---- v6.6.8: the EQ sliders are owner-drawn ---------------------------------
// Stock TRACKBAR_CLASSA flashed white before the dark chrome landed even
// under DarkMode_Explorer. Ours never erases (WM_ERASEBKGND -> 1) and paints
// groove + thumb GDI+ opaque: no white frame can exist. Drag reports through
// the SAME WM_HSCROLL, so parent handling did not change.
struct Sld { int lo, hi, pos; bool drag; };
static std::map<HWND, Sld> g_sld;

static int sldGetPos(HWND h) {
    const auto it = g_sld.find(h);
    return it != g_sld.end() ? it->second.pos : 0;
}
static void sldSetPos(HWND h, int p, bool redraw) {
    const auto it = g_sld.find(h);
    if (it == g_sld.end()) return;
    if (p < it->second.lo) p = it->second.lo;
    if (p > it->second.hi) p = it->second.hi;
    it->second.pos = p;
    if (redraw) InvalidateRect(h, nullptr, FALSE);
}

static void sldPaint(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, g_brPanel);          // exact opaque pixels FIRST (v6.6.7 rule)
    const Sld& s = g_sld[h];
    const int x0 = 10, x1 = rc.right - 10;
    const int cy = (rc.top + rc.bottom) / 2;
    const float t  = (s.hi > s.lo) ? (float)(s.pos - s.lo) / (float)(s.hi - s.lo) : 0.0f;
    const int   tx = x0 + (int)(t * (x1 - x0) + 0.5f);
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    {   // groove: full travel, then the traveled part in accent
        Gdiplus::GraphicsPath* p = guiRoundPath(Gdiplus::Rect(x0, cy - 3, x1 - x0, 6), 3);
        Gdiplus::SolidBrush gb(GCol(guic::cLine));
        g.FillPath(&gb, p);
        delete p;
        if (tx > x0) {
            Gdiplus::GraphicsPath* f = guiRoundPath(Gdiplus::Rect(x0, cy - 3, tx - x0, 6), 3);
            Gdiplus::SolidBrush fb(GCol(guic::cAccentDk));
            g.FillPath(&fb, f);
            delete f;
        }
    }
    {   // thumb: rounded grip, accent border while dragging
        Gdiplus::GraphicsPath* p = guiRoundPath(Gdiplus::Rect(tx - 7, cy - 11, 14, 22), 6);
        Gdiplus::SolidBrush fb(GCol(s.drag ? guic::cPanelHi : guic::cPanel2));
        Gdiplus::Pen        pb(GCol(s.drag ? guic::cAccent : guic::cLine), 1.0f);
        g.FillPath(&fb, p);
        g.DrawPath(&pb, p);
        delete p;
    }
    if (GetFocus() == h) {   // v7.1.4: the keyboard focus cue -- dashed accent ring
        Gdiplus::Pen fp(GCol(guic::cAccent), 1.0f);
        fp.SetDashStyle(Gdiplus::DashStyleDash);
        g.DrawRectangle(&fp, Gdiplus::Rect(4, 3, rc.right - rc.left - 8, rc.bottom - rc.top - 6));
    }
    EndPaint(h, &ps);
}

static void sldUpdateFromX(HWND h, Sld& s, LPARAM lp) {
    RECT rc;
    GetClientRect(h, &rc);
    const int x0 = 10, x1 = rc.right - 10;
    int x = (short)LOWORD(lp);
    if (x < x0) x = x0;
    if (x > x1) x = x1;
    s.pos = s.lo + (int)((float)(x - x0) * (float)(s.hi - s.lo) / (float)(x1 - x0) + 0.5f);
    InvalidateRect(h, nullptr, FALSE);
}

static LRESULT CALLBACK sldProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Sld& s = g_sld[h];
    switch (m) {
    case WM_ERASEBKGND:
        return 1;                      // the white flash cannot exist here
    case WM_LBUTTONDOWN:
        SetCapture(h);
        SetFocus(h);                   // v7.1.4: a click gives the slider keyboard focus
        s.drag = true;
        sldUpdateFromX(h, s, lp);
        SendMessageA(GetParent(h), WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, s.pos), (LPARAM)h);
        return 0;
    case WM_MOUSEMOVE:
        if (s.drag && GetCapture() == h) {
            sldUpdateFromX(h, s, lp);
            SendMessageA(GetParent(h), WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, s.pos), (LPARAM)h);
        }
        return 0;
    case WM_CAPTURECHANGED:
        s.drag = false;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        if (s.drag) {
            s.drag = false;
            ReleaseCapture();
            InvalidateRect(h, nullptr, FALSE);
            SendMessageA(GetParent(h), WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, s.pos), (LPARAM)h);
        }
        return 0;
    case WM_PAINT:
        sldPaint(h);
        return 0;
    // v7.1.4 KEYBOARD: the slider is a first-class tab stop. Arrows adjust
    // the value (DLGC_WANTARROWS keeps them away from IsDialogMessage's
    // focus logic); focus paints the dashed ring in sldPaint.
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN: {
        int step = (s.hi > s.lo) ? (s.hi - s.lo) / 50 : 1;
        if (step < 1) step = 1;
        int np = s.pos;
        if      (wp == VK_LEFT || wp == VK_DOWN) np = s.pos - step;
        else if (wp == VK_RIGHT || wp == VK_UP)  np = s.pos + step;
        else break;
        if (np < s.lo) np = s.lo;
        if (np > s.hi) np = s.hi;
        if (np != s.pos) {
            s.pos = np;
            InvalidateRect(h, nullptr, FALSE);
            SendMessageA(GetParent(h), WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, s.pos), (LPARAM)h);
        }
        return 0; }
    }
    return DefWindowProcA(h, m, wp, lp);
}

static HWND mkSlider(HWND w, int id, int x, int y, int cw, int lo, int hi) {
    const int dx = x, dy = y, dw2 = cw;
    x = S(x); y = S(y); cw = S(cw);   // v7.1.20: DPI scale
    static bool sldClass = false;      // registered once
    if (!sldClass) {
        WNDCLASSA wc = {};
        wc.lpfnWndProc   = sldProc;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = "ts_slider";
        RegisterClassA(&wc);
        sldClass = true;
    }
    HWND h = CreateWindowExA(0, "ts_slider", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP,   // v7.1.4: a tab stop
                             x, y, cw, S(28), w, (HMENU)(INT_PTR)id, nullptr, nullptr);
    tsTrackChild(h, dx, dy, dw2, 28);   // the dpi ledger (no font)
    g_sld[h] = { lo, hi, lo, false };
    InvalidateRect(h, nullptr, FALSE);   // paint with the REAL range once set
    regPage(h);
    return h;
}

// one ini row: label + edit box
static void row(HWND w, int colX, int y, const char* label, int id, const char* val) {
    mkLabel(w, 0, colX, y + 2, LBL_W, label, g_fUI);
    mkEdit(w, id, colX + LBL_W + 6, y, CTL_W, 22, val);
}

// one ini row: label + ON/OFF pill
static void tglRow(HWND w, int colX, int y, const char* label, int id, bool on) {
    mkLabel(w, 0, colX, y + 2, LBL_W, label, g_fUI);
    HWND b = mkBtn(w, id, colX + LBL_W + 6, y, 60, 22, "");
    regPage(b);                        // v6.6.2: the pill lives on THIS page
    g_tog[id] = on;
}

static void header(HWND w, int colX, int y, const char* txt) {
    static int hdrId = 600;
    mkLabel(w, hdrId++, colX, y, 400, txt, g_fUIb);
}

static void note(HWND w, int colX, int y, const char* txt, int lines) {
    mkLabel(w, 0, colX, y, 406, txt, g_fSml, lines * 15 + 4);   // the helper scales
}

// ---- EQ slider mapping (freq and Q on log scales, gain linear) ------------
static int freqToPos(float f) {
    if (f < 20.0f) f = 20.0f; if (f > 20000.0f) f = 20000.0f;
    return (int)(1000.0 * log((double)f / 20.0) / log(1000.0) + 0.5);
}
static float posToFreq(int p) { return (float)(20.0 * pow(1000.0, (double)p / 1000.0)); }
static int qToPos(float q) {
    if (q < 0.1f) q = 0.1f; if (q > 10.0f) q = 10.0f;
    return (int)(1000.0 * log((double)q / 0.1) / log(100.0) + 0.5);
}
static float posToQ(int p) { return (float)(0.1 * pow(100.0, (double)p / 1000.0)); }

// ---- v7.1.8 the EQ save slots ----------------------------------------------
// The four built-in presets (FLAT/DIALOGUE/BASS/WARMTH) are retired: the five
// buttons are USER slots now. Left-click applies a slot (empty slots say so);
// right-click (WM_CONTEXTMENU) saves the current sliders + the EQ pill into
// it and splices the ini immediately -- same save model as the theme button.
// Slot 1 ships reserved with the field's dialogue curve: 387 Hz, +3.5 dB,
// Q 0.30. freq <= 0 marks an empty slot.
static bool guiSaveIni();                 // fwd: defined below the ini section
static void guiRepaintSlots(HWND w);      // fwd: defined with the repaint helpers
static void guiSetStatus(HWND w, const char* s);   // fwd: defined in the frame section

static void guiUpdateEqLabels(HWND w) {
    char b[96];
    const int pf = sldGetPos(GetDlgItem(w, IDC_SLD_FREQ));
    const int pg = sldGetPos(GetDlgItem(w, IDC_SLD_GAIN));
    const int pq = sldGetPos(GetDlgItem(w, IDC_SLD_Q));
    const float f = posToFreq(pf), g = pg * 0.5f, q = posToQ(pq);
    if (f >= 1000.0f) snprintf(b, sizeof(b), "PEAK FREQUENCY     %.2f kHz", f / 1000.0f);
    else              snprintf(b, sizeof(b), "PEAK FREQUENCY     %.0f Hz", f);
    SetWindowTextA(GetDlgItem(w, IDC_LBL_FREQ), b);
    snprintf(b, sizeof(b), "BOOST     %+.1f dB", g);
    SetWindowTextA(GetDlgItem(w, IDC_LBL_GAIN), b);
    snprintf(b, sizeof(b), "SHARPNESS (Q)     %.2f", q);
    SetWindowTextA(GetDlgItem(w, IDC_LBL_Q), b);
}

static void guiApplySlot(HWND w, int i) {
    g_eqSel = i;                      // v7.1.10: one var -- the click selects AND
                                      // arms the slot; the accent survives slider
                                      // tweaks so the SAVE target stays visible
    if (twsCfg.eqSlotF[i] <= 0.0f) {   // empty slot -- arm it, keep the sliders
        char b[128];
        snprintf(b, sizeof(b),
                 "slot %d is empty -- set the curve, then press SAVE TO SLOT",
                 i + 1);
        guiSetStatus(w, b);
        guiRepaintSlots(w);
        return;
    }
    g_tog[IDC_TGL_EQ] = twsCfg.eqSlotOn[i];
    sldSetPos(GetDlgItem(w, IDC_SLD_FREQ), freqToPos(twsCfg.eqSlotF[i]), TRUE);
    sldSetPos(GetDlgItem(w, IDC_SLD_GAIN), (int)(twsCfg.eqSlotG[i] * 2.0f), TRUE);
    sldSetPos(GetDlgItem(w, IDC_SLD_Q),    qToPos(twsCfg.eqSlotQ[i]), TRUE);
    guiUpdateEqLabels(w);
    guiRepaint(w, IDC_TGL_EQ);   // v6.6.1: pill + slots repaint themselves
    guiRepaintSlots(w);
}

// right-click a slot (or the SAVE TO SLOT button): the sliders' current
// curve + the EQ pill land in the slot and the ini is spliced at once
// (v7.1.8; same model as the theme click). v7.1.9: the button saves the
// ARMED slot -- clicking a slot arms it and survives slider tweaks.
static void guiSaveSlot(HWND w, int i) {
    twsCfg.eqSlotOn[i] = g_tog[IDC_TGL_EQ];
    twsCfg.eqSlotF[i]  = posToFreq(sldGetPos(GetDlgItem(w, IDC_SLD_FREQ)));
    twsCfg.eqSlotG[i]  = sldGetPos(GetDlgItem(w, IDC_SLD_GAIN)) * 0.5f;
    twsCfg.eqSlotQ[i]  = posToQ(sldGetPos(GetDlgItem(w, IDC_SLD_Q)));
    // the live EQ mirrors what is on screen, so the saved ini stays coherent
    twsCfg.centerEq     = g_tog[IDC_TGL_EQ];
    twsCfg.centerEqFreq = twsCfg.eqSlotF[i];
    twsCfg.centerEqGain = twsCfg.eqSlotG[i];
    twsCfg.centerEqQ    = twsCfg.eqSlotQ[i];
    g_eqSel  = i;
    const bool ok = guiSaveIni();
    guiUpdateEqLabels(w);
    guiRepaint(w, IDC_TGL_EQ);
    guiRepaintSlots(w);
    char b[128];
    snprintf(b, sizeof(b),
             "%s to slot %d: %.0f Hz / %+.1f dB / Q %.2f",
             ok ? "saved" : "save FAILED", i + 1,
             twsCfg.eqSlotF[i], twsCfg.eqSlotG[i], twsCfg.eqSlotQ[i]);
    guiSetStatus(w, b);
}

// ---- live device enumeration for the pickers -------------------------------
static void guiEnumNames(std::vector<std::string>& pb, std::vector<std::string>& cp) {
    twsEnumDevices(pb, cp);   // the core owns the WASAPI walk
}

static void guiFillCombos(HWND w) {
    std::vector<std::string> pb, cp;
    guiEnumNames(pb, cp);
    const int ids[5] = { IDC_OUT1, IDC_OUT2, IDC_OUT3, IDC_OUT4, IDC_INDEV };  // 0..3 playback (v9.1.4: leg4's fold target); 4 capture
    for (int k = 0; k < 5; ++k) {
        HWND h = GetDlgItem(w, ids[k]);
        char cur[512]; GetWindowTextA(h, cur, sizeof(cur));
        SendMessageA(h, CB_RESETCONTENT, 0, 0);
        const std::vector<std::string>& v = (k < 4) ? pb : cp;   // v9.1.4: four playback combos
        for (size_t i = 0; i < v.size(); ++i)
            SendMessageA(h, CB_ADDSTRING, 0, (LPARAM)v[i].c_str());
        if (cur[0]) SetWindowTextA(h, cur);
    }
}

// ---- ini values <-> gui ----------------------------------------------------
static std::string boolText(bool b) { return b ? "on" : "off"; }

static std::string cfgValueText(const std::string& key) {
    char b[512];
    if      (key == "delay1_ms")         { snprintf(b, 64, "%g", twsCfg.delay1Ms); return b; }
    else if (key == "delay2_ms")         { snprintf(b, 64, "%g", twsCfg.delay2Ms); return b; }
    else if (key == "offset3_ms")        { snprintf(b, 64, "%g", twsCfg.offset3Ms); return b; }   // v8.4.0
    else if (key == "gain_l")            { snprintf(b, 64, "%g", twsCfg.gainL);     return b; }
    else if (key == "gain_r")            { snprintf(b, 64, "%g", twsCfg.gainR);     return b; }
    else if (key == "gain_lfe")          { snprintf(b, 64, "%g", twsCfg.gainLfe);   return b; }
    else if (key == "gain_center")       { snprintf(b, 64, "%g", twsCfg.gainCenter);return b; }
    else if (key == "gain_rl")           { snprintf(b, 64, "%g", twsCfg.gainRl);   return b; }   // v8.4.0
    else if (key == "gain_rr")           { snprintf(b, 64, "%g", twsCfg.gainRr);   return b; }   // v8.4.0
    else if (key == "gain_sl")           { snprintf(b, 64, "%g", twsCfg.gainSl);   return b; }   // v8.5.0
    else if (key == "gain_sr")           { snprintf(b, 64, "%g", twsCfg.gainSr);   return b; }
    else if (key == "chime_gain")        { snprintf(b, 64, "%g", twsCfg.chimeGain); return b; }
    else if (key == "center_eq_freq")    { snprintf(b, 64, "%g", twsCfg.centerEqFreq); return b; }
    else if (key == "center_eq_gain")    { snprintf(b, 64, "%g", twsCfg.centerEqGain); return b; }
    else if (key == "center_eq_q")       { snprintf(b, 64, "%g", twsCfg.centerEqQ); return b; }
    else if (key.rfind("eq_slot", 0) == 0 && key.size() > 8 &&
             key[7] >= '1' && key[7] <= '5') {
        const int n = key[7] - '1';
        const std::string t = key.substr(8);
        if      (t == "_on")   return boolText(twsCfg.eqSlotOn[n]);
        else if (t == "_freq") { snprintf(b, 64, "%g", twsCfg.eqSlotF[n]); return b; }
        else if (t == "_gain") { snprintf(b, 64, "%g", twsCfg.eqSlotG[n]); return b; }
        else if (t == "_q")    { snprintf(b, 64, "%g", twsCfg.eqSlotQ[n]); return b; }
    }
    else if (key == "center_eq")         return boolText(twsCfg.centerEq);
    // v9.6.0 repair: packet_mode / parallel_mix / chimes / gui_theme are LIVE
    // keys kIniKeys still writes on save -- their cfgValueText lines were
    // lost in the v9.6.0 cleanup, so every GUI save wrote them EMPTY (an
    // empty value reloads as ON/theme-0), silently resetting a user's OFF
    // toggles and theme choice.
    else if (key == "packet_mode")       return boolText(twsCfg.packetMode);
    else if (key == "parallel_mix")      return boolText(twsCfg.parallelMix);
    else if (key == "chimes")            return boolText(twsCfg.chimes);
    else if (key == "gui_theme")        { snprintf(b, 64, "%d", g_theme); return b; }
    else if (key == "input_device")      return twsCfg.inputDevice;
    else if (key == "output_device1")    return twsCfg.outputDevice1;
    else if (key == "output_device2")    return twsCfg.outputDevice2;
    else if (key == "output_device3")    return twsCfg.outputDevice3;   // v8.4.0
    else if (key == "output_device4")    return twsCfg.outputDevice4;   // v9.1.4: leg4's fold target
    else if (key == "startup_engine")   return boolText(twsCfg.startupEngine);   // v7.1.2
    else if (key == "gui_window_x")     { snprintf(b, 64, "%d", g_guiWinX); return b; }   // v7.1.20
    else if (key == "gui_window_y")     { snprintf(b, 64, "%d", g_guiWinY); return b; }   // v7.1.20
    return "";
}

// every key the window can write, in canonical order (for appending)
// v8.2.0: out1ms/out2ms are GONE from the list (engine constants now) -- a
// stale line in an old ini is copied verbatim by the save and ignored at
// load, exactly like cap_ms / ring1_ms / ring2_ms before them.
// v8.3.0: volume_follow + the drift trio are GONE with them (hardcoded
// constants); a stale line rides the same verbatim-copy path. v8.4.0:
// lfe_fold is GONE too (the fold is built in, always on). v8.4.5:
// ks_period_frames / ks_periods join them -- THE CALM BITE is built in.
// v9.0.1: backend1/backend2 join them -- WDM-KS is removed; a stale line
// rides the same verbatim-copy path.
static const char* kIniKeys[] = {
    "delay1_ms", "delay2_ms", "offset3_ms",                    // v8.4.0 rears (v8.4.11: offset4_ms retired)
    "input_device", "packet_mode",                // v9.6.0: the source is always output-as-source
    "output_device1", "output_device2",
    "output_device3",                                          // v8.4.0 rears
    "output_device4",                                          // v9.1.4: leg4 -- the LFE fold to an existing leg
    "gain_l", "gain_r", "gain_rl", "gain_rr", "gain_sl", "gain_sr", "gain_lfe", "gain_center",
    "center_eq", "center_eq_freq", "center_eq_gain", "center_eq_q",
    "parallel_mix",                                                                // v9.3.0: the mix crew
    "eq_slot1_on", "eq_slot1_freq", "eq_slot1_gain", "eq_slot1_q",   // v7.1.8
    "eq_slot2_on", "eq_slot2_freq", "eq_slot2_gain", "eq_slot2_q",
    "eq_slot3_on", "eq_slot3_freq", "eq_slot3_gain", "eq_slot3_q",
    "eq_slot4_on", "eq_slot4_freq", "eq_slot4_gain", "eq_slot4_q",
    "eq_slot5_on", "eq_slot5_freq", "eq_slot5_gain", "eq_slot5_q",
    "chimes", "chime_gain",
    "gui_theme",                // v7.0.4: the GUI theme persists
    "startup_engine",           // v7.1.2: the startup launch auto-starts the engine
    "gui_window_x",             // v7.1.20: the panel's last position (-99999 = default)
    "gui_window_y",             // v7.1.20: captured on hide/shutdown, spliced on save
};

// v6.6.0: write twinspeak.ini WITHOUT destroying it. Every line that is not a
// known key (comments, blanks) is copied byte-for-byte; a known key keeps its
// line position and only the value after '=' is spliced. Keys missing from
// the file are appended at the end. A .bak of the previous file is kept.
static bool guiSaveIni() {
    std::vector<std::string> lines;
    if (FILE* f = fopen(g_guiIniPath.c_str(), "r")) {
        char l[2048];
        while (fgets(l, sizeof(l), f)) lines.push_back(l);
        fclose(f);
        CopyFileA(g_guiIniPath.c_str(), (g_guiIniPath + ".bak").c_str(), FALSE);
    }
    bool seen[sizeof(kIniKeys) / sizeof(kIniKeys[0])] = { false };
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& s = lines[i];
        std::string head = s;
        const size_t h = head.find_first_of("#;");
        if (h != std::string::npos) head = head.substr(0, h);
        const size_t eq = head.find('=');
        std::string key;
        bool isKey = false;
        size_t keyIdx = 0;
        if (eq != std::string::npos) {
            key = lower(trim(head.substr(0, eq)));
            for (size_t k = 0; k < sizeof(kIniKeys) / sizeof(kIniKeys[0]); ++k)
                if (key == kIniKeys[k]) { isKey = true; keyIdx = k; break; }
        }
        if (isKey) {
            // v6.6.9: splice EVERY known key line, empty value or not -- the
            // old "skip empty" rule leaked the stale value verbatim and
            // appended an empty duplicate. One key, one line, current.
            seen[keyIdx] = true;
            out += head.substr(0, eq + 1) + " " + cfgValueText(key) + "\r\n";
            continue;
        }
        out += s;                       // comments / blanks / unknowns: verbatim
    }
    bool appended = false;
    for (size_t k = 0; k < sizeof(kIniKeys) / sizeof(kIniKeys[0]); ++k) {
        if (seen[k]) continue;
        if (!appended) { out += "\r\n; added by the twinspeak control window\r\n"; appended = true; }
        out += std::string(kIniKeys[k]) + " = " + cfgValueText(kIniKeys[k]) + "\r\n";
    }
    FILE* f = fopen(g_guiIniPath.c_str(), "w");
    if (!f) return false;
    fputs(out.c_str(), f);
    fclose(f);
    return true;
}

// ---- controls -> g_cfg ------------------------------------------------------
static float editFloat(HWND w, int id, float def) {
    char b[64]; GetDlgItemTextA(w, id, b, sizeof(b));
    return b[0] ? (float)atof(b) : def;
}
static std::string editStr(HWND w, int id) {
    char b[512]; GetDlgItemTextA(w, id, b, sizeof(b));
    return b;
}

static void cfgFromControls(HWND w) {
    // v7.1.6 TRAY-FIRST: a tray-only start runs the engine straight from the
    // ini (g_cfg as loaded) -- no window, no controls exist yet. Every start
    // path that lands here without the control window simply keeps g_cfg;
    // when the window is built later, controlsFromCfg fills the controls
    // from the same g_cfg, so START in the window still edits what runs.
    if (!w || !GetDlgItem(w, IDC_INDEV)) return;
    // v9.5.1d PLAIN PICKER: the box's text IS the needle -- nothing to
    // strip, the key stays ON (the outputs-only workflow IS output-as-
    // source). A legacy " -- output" marker (a v9.5.0/1c ini or a
    // hand-pasted entry) is stripped and the key set ON so it keeps
    // resolving exactly as it did; the marker itself never comes back.
    {
        std::string in = editStr(w, IDC_INDEV);
        const std::string tag = " -- output";
        if (in.size() > tag.size() &&
            in.compare(in.size() - tag.size(), tag.size(), tag) == 0) {
            in.resize(in.size() - tag.size());
            while (!in.empty() && in.back() == ' ') in.pop_back();
        }
        twsCfg.inputDevice = in;
    }
    twsCfg.outputDevice1  = editStr(w, IDC_OUT1);
    twsCfg.outputDevice2  = editStr(w, IDC_OUT2);
    twsCfg.outputDevice3  = editStr(w, IDC_OUT3);   // v8.4.0: empty = the leg stays silent (v8.4.11: ONE rear device)
    twsCfg.outputDevice4  = editStr(w, IDC_OUT4);   // v9.1.4: leg4's LFE fold target (empty = idle)
    twsCfg.gainL     = editFloat(w, IDC_GAIN_L,  1.0f);
    twsCfg.gainR     = editFloat(w, IDC_GAIN_R,  1.0f);
    twsCfg.gainRl    = editFloat(w, IDC_GAIN_RL, 1.0f);   // v8.4.0 gain quad
    twsCfg.gainRr    = editFloat(w, IDC_GAIN_RR, 1.0f);
    twsCfg.gainSl    = editFloat(w, IDC_GAIN_SL, 1.0f);   // v8.5.0: the surround gains
    twsCfg.gainSr    = editFloat(w, IDC_GAIN_SR, 1.0f);
    twsCfg.gainLfe   = editFloat(w, IDC_GAIN_LFE, 0.4f);
    twsCfg.gainCenter= editFloat(w, IDC_GAIN_CTR, 1.0f);
    // v8.4.0: lfeFold is INTEGRATED (always on) -- no control reads it.
    // v6.6.8: captureExclusive / exclusive / alignModes are integrated -- the
    // struct defaults (all on) are final; no control can change them.
    // v8.2.0: out1Ms/out2Ms have no controls -- the constant frame request is
    // the engine's.
    twsCfg.delay1Ms = editFloat(w, IDC_DELAY1, 0.0f);
    twsCfg.delay2Ms = editFloat(w, IDC_DELAY2, 0.0f);
    twsCfg.offset3Ms = editFloat(w, IDC_OFF3, 0.0f);   // v8.4.0 timing quad (v8.4.11: the one rear leg)
    // v9.5.1: out2_first is integrated (always center-first) -- nothing to read.
    // v7.0.0: pinOutputs is integrated -- always on; no control can change it.
    // v9.0.1: backend1/backend2 are gone with WDM-KS -- read-and-ignored.
    // v6.6.8: clockSync / driftGuard / powerGuard / yieldDefault integrated.
    // v8.3.0: the drift trio + volume_follow are hardcoded constants now
    // (5 / 5000 / 30 / on) -- no control reads them, nothing can flip them.
    // v6.6.8: centerGate / centerUpmixGuard / background integrated.
    twsCfg.chimes          = g_tog[IDC_TGL_CHIMES];
    twsCfg.chimeGain       = editFloat(w, IDC_CHIMGAIN, 0.4f);
    twsCfg.centerEq       = g_tog[IDC_TGL_EQ];
    twsCfg.centerEqFreq   = posToFreq(sldGetPos(GetDlgItem(w, IDC_SLD_FREQ)));
    twsCfg.centerEqGain   = sldGetPos(GetDlgItem(w, IDC_SLD_GAIN)) * 0.5f;
    twsCfg.centerEqQ      = posToQ(sldGetPos(GetDlgItem(w, IDC_SLD_Q)));
}

static void controlsFromCfg(HWND w) {
    char b[64];
    // v9.5.1d: the box shows the PLAIN name -- no tag anywhere (the
    // picker offers plain render names; the mode is the default).
    SetDlgItemTextA(w, IDC_INDEV, twsCfg.inputDevice.c_str());
    SetDlgItemTextA(w, IDC_OUT1,  twsCfg.outputDevice1.c_str());
    SetDlgItemTextA(w, IDC_OUT2,  twsCfg.outputDevice2.c_str());
    SetDlgItemTextA(w, IDC_OUT3,  twsCfg.outputDevice3.c_str());   // v8.4.0
    SetDlgItemTextA(w, IDC_OUT4,  twsCfg.outputDevice4.c_str());   // v9.1.4
    // v9.5.1: no IDC_VOLEP box exists to sync (volume_endpoint retired).
    snprintf(b, 64, "%g", twsCfg.gainL);      SetDlgItemTextA(w, IDC_GAIN_L, b);
    snprintf(b, 64, "%g", twsCfg.gainR);      SetDlgItemTextA(w, IDC_GAIN_R, b);
    snprintf(b, 64, "%g", twsCfg.gainRl);     SetDlgItemTextA(w, IDC_GAIN_RL, b);
    snprintf(b, 64, "%g", twsCfg.gainRr);     SetDlgItemTextA(w, IDC_GAIN_RR, b);
    snprintf(b, 64, "%g", twsCfg.gainSl);     SetDlgItemTextA(w, IDC_GAIN_SL, b);
    snprintf(b, 64, "%g", twsCfg.gainSr);     SetDlgItemTextA(w, IDC_GAIN_SR, b);
    snprintf(b, 64, "%g", twsCfg.gainLfe);    SetDlgItemTextA(w, IDC_GAIN_LFE, b);
    snprintf(b, 64, "%g", twsCfg.gainCenter); SetDlgItemTextA(w, IDC_GAIN_CTR, b);
    // v8.4.0: no LFEFOLD pill to sync -- the fold is built in (always on).
    snprintf(b, 64, "%g", twsCfg.delay1Ms); SetDlgItemTextA(w, IDC_DELAY1, b);
    snprintf(b, 64, "%g", twsCfg.delay2Ms); SetDlgItemTextA(w, IDC_DELAY2, b);
    snprintf(b, 64, "%g", twsCfg.offset3Ms); SetDlgItemTextA(w, IDC_OFF3, b);   // v8.4.0
    // v9.5.1: no IDC_TGL_OUT2FIRST pill exists to sync (out2_first retired).
    // v9.0.1: backend1/backend2 are gone with WDM-KS -- nothing to sync.
    // v8.3.0: the drift trio + volume_follow have no controls to sync.
    g_tog[IDC_TGL_CHIMES]     = twsCfg.chimes;
    snprintf(b, 64, "%g", twsCfg.chimeGain);      SetDlgItemTextA(w, IDC_CHIMGAIN, b);
    g_tog[IDC_TGL_EQ] = twsCfg.centerEq;
    sldSetPos(GetDlgItem(w, IDC_SLD_FREQ), freqToPos(twsCfg.centerEqFreq), FALSE);
    sldSetPos(GetDlgItem(w, IDC_SLD_GAIN), (int)(twsCfg.centerEqGain * 2.0f), FALSE);
    sldSetPos(GetDlgItem(w, IDC_SLD_Q),    qToPos(twsCfg.centerEqQ), FALSE);
    g_eqSel = -1;    // custom unless the values match a saved slot exactly
    for (int i = 0; i < 5; ++i) {
        if (twsCfg.eqSlotF[i] > 0.0f &&
            twsCfg.centerEq     == twsCfg.eqSlotOn[i] &&
            fabsf(twsCfg.centerEqFreq - twsCfg.eqSlotF[i]) < 1.0f &&
            fabsf(twsCfg.centerEqGain - twsCfg.eqSlotG[i]) < 0.26f &&
            fabsf(twsCfg.centerEqQ    - twsCfg.eqSlotQ[i]) < 0.02f) { g_eqSel = i; break; }
    }
    guiUpdateEqLabels(w);
    InvalidateRect(w, nullptr, TRUE);
}

// ---- engine control ---------------------------------------------------------
static void guiLogSink(const char* s) {
    std::string line(s);
    if (line.compare(0, 5, "raw1 ") == 0) {          // the live health line:
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        // v7.2.12: g_lastHealth is written HERE (an engine thread) and read
        // by the 250 ms tick (the GUI thread) -- a plain std::string handed
        // across threads without the lock is a torn-read / heap-corruption
        // crash waiting for one unlucky interleaving. It rides g_logCS like
        // the log ring one block below always has.
        EnterCriticalSection(&g_logCS);
        g_lastHealth = line;                          // status bar, not the pane
        LeaveCriticalSection(&g_logCS);
        return;
    }
    for (size_t i = 0; i < line.size(); ++i)
        if (line[i] == '\r') line[i] = '\n';          // console \r-overwrite -> lines
    EnterCriticalSection(&g_logCS);
    g_logRing += line;                            // v9.1.2: the 64 KB trim is retired -- nothing is lost
    LeaveCriticalSection(&g_logCS);
}

// v7.1.5 FLICKER-FREE STATUS: the engine/health line paints INSIDE the frame
// buffer (guiPaintFrame) -- the STATIC child is hidden and only STORES the
// string. Every writer goes through here so the frame's status band is
// invalidated in the same breath; the next WM_PAINT lands it as one blit.
static void guiSetStatus(HWND w, const char* s) {
    SetDlgItemTextA(g_hwnd ? g_hwnd : w, IDC_STATUS, s);
    // v7.1.20: MUST mirror guiPaintFrame's scaled status band exactly.
    // v9.0.2: the band is 3 rows (54px) -- one '\n'-split string.
    const RECT sr = { S(14), S(458), S(866), S(512) };
    InvalidateRect(g_hwnd ? g_hwnd : w, &sr, FALSE);
}

static DWORD WINAPI guiEngineThread(LPVOID) {
    twsLog("[twinspeak] gui           : ---- engine start (%s) ----\n", g_guiIniPath.c_str());
    const int rc = twsEngineRun(g_guiIniPath.c_str(), false, true);
    g_engRC.store(rc);
    g_engState.store(0);
    PostMessageA(g_hwnd, WM_APP + 2, 0, 0);           // refresh the START button
    return 0;
}

static void guiStartEngine(HWND w) {
    if (g_engState.load() != 0) return;
    cfgFromControls(w);
    cfgSanitize();
    // v8.4.1: output_device2 is OPTIONAL now -- an EMPTY center leg never
    // opens and the center folds onto leg1's FL/FR (g_centerFold); the
    // rear legs were already so. Only the capture and the mains are hard.
    if (twsCfg.inputDevice.empty() || twsCfg.outputDevice1.empty()) {
        guiSetStatus(g_hwnd, "set input_device / output_device1 first (legs 2/3/4 may stay EMPTY)");
        // v7.2.12: tray-only start (no panel yet) has no status line -- the
        // refusal used to vanish into a hidden STATIC and "Start engine"
        // from the tray menu looked like a silent dead click. The balloon
        // is the tray path's only voice.
        if (g_trayVisible && (!g_hwnd || g_hwnd == g_trayWnd))
            trayBalloon("Cannot start: set input_device / output_device1 in\n"
                        "twinspeak.ini first (output_device2/3/4 may stay EMPTY).");
        return;
    }
    if (!guiSaveIni())
        twsLog("[twinspeak] gui           : could not write the ini -- running with the in-window values\n");
    g_engState.store(1);
    guiRepaint(g_hwnd, IDC_START);   // v6.6.1: the button owns its own pixels
    trayUpdateTip(true);             // v7.1.20: the tray tip tracks the engine
    g_engThread = CreateThread(nullptr, 0, guiEngineThread, nullptr, 0, nullptr);
}

// v7.1.20 THE FREEZE FIX: guiStopEngine used to sit in WaitForSingleObject
// (10 s timeout) ON THE UI THREAD -- the window went dead for the whole
// stop (no repaint, no move, ghost "not responding", and a duplicate's
// SendMessageTimeout aborted against the frozen queue). The stop is now
// ASYNCHRONOUS: flag + repaint here; the engine thread lands WM_APP+2 when
// it finishes, which reaps the handle and runs a chained APPLY+RESTART.
// The 250 ms timer reaps as a backup. Only the shutdown path joins
// synchronously (guiStopEngineJoin).
static void guiStopEngine() {
    const int st = g_engState.load();
    if (st == 0 || st == 3) return;
    g_engState.store(3);
    if (g_hwnd) guiRepaint(g_hwnd, IDC_START);
    twsRun = false;
    trayUpdateTip(false);
}
// shutdown-only (message loop already exited / process going away): a block
// here costs nothing the user can see.
static void guiStopEngineJoin() {
    if (g_engState.load() != 0) { g_engState.store(3); twsRun = false; }
    if (g_engThread) {
        WaitForSingleObject(g_engThread, 10000);
        CloseHandle(g_engThread);
        g_engThread = nullptr;
    }
    g_engState.store(0);
    if (g_hwnd) guiRepaint(g_hwnd, IDC_START);
}

// ---- page building ----------------------------------------------------------
static bool startupEnabled();   // v6.6.6: forward decl -- the startup pill reads it at build time
// v7.0.0: uniform tab width -- v7.1.7: five faces span the same strip
// (5 x 160 px + 4 x 4 px gaps = 816 px, the 818 px layout's twin).
// v7.1.8: the faces widened to 168; v7.1.9 trimmed to 164; v7.1.10 lands
// on the field's sweet spot: +6 px (166). 5 x 166 + 4 x 4 gaps + 5 px
// left pad ends at 851 inside the 866 px strip -- halo fits, no overflow.
// (kTabW / kTabGap are defined -- and scaled -- in the v7.1.20 DPI block.)

static void comboRow(HWND w, int colX, int y, const char* label, int id,
                     bool listOnly, const char* cur) {
    mkLabel(w, 0, colX, y + 2, LBL_W, label, g_fUI);
    HWND h = mkCombo(w, id, colX + LBL_W + 6, y, listOnly);
    if (cur && cur[0]) SetWindowTextA(h, cur);
}

// v8.4.0 THE QUAD: a compact group -- a short small-font label OVER a
// narrow 92 px edit (100 px pitch, across the column). The gain quad
// (gain_l/gain_r/gain_rl/gain_rr) and the timing row (delay1_ms/delay2_ms/
// offset3_ms -- v8.4.11: offset4_ms retired with leg4) use it; the wide
// single-key rows keep the classic label+edit layout (the field's ask:
// shorter-width values, grouped).
static void quadRow(HWND w, int colX, int y, int cnt, const char* const lbl[],
                    const int id[], const char* const val[]) {
    for (int k = 0; k < cnt; ++k) {
        const int x = colX + k * 100;
        mkLabel(w, 0, x, y, 92, lbl[k], g_fSml, 14);
        mkEdit(w, id[k], x, y + 16, 92, 22, val[k]);
    }
}

static void guiBuildPages(HWND w) {
    // ============ page 0: CONFIGURATION (v8.3.0: was DEVICES; the startup +
    // chime rows from the late SETTINGS page live here too) ============
    g_tab = 0;
    header(w, COLA_X, KHDR_Y, "CONFIGURATION");
    comboRow(w, COLA_X, KROW0, "capture input", IDC_INDEV, false, twsCfg.inputDevice.c_str());
    comboRow(w, COLA_X, 154, "output leg1 (mains)", IDC_OUT1, false, twsCfg.outputDevice1.c_str());
    comboRow(w, COLA_X, 188, "output leg2 (center)", IDC_OUT2, false, twsCfg.outputDevice2.c_str());
    comboRow(w, COLA_X, 222, "output leg3 (rears)", IDC_OUT3, false, twsCfg.outputDevice3.c_str());
    // v9.1.4 LEG4: the LFE fold target -- pick a device ALREADY playing on
    // another leg and that leg's outputs gain the LFE channel. The left
    // column reflows: leg4 256, volume_endpoint 290, REFRESH 318, note 352.
    comboRow(w, COLA_X, 256, "output leg4 (LFE fold)", IDC_OUT4, false, twsCfg.outputDevice4.c_str());
    // v9.5.1: the volume_endpoint row is RETIRED -- volume is the cable
    // input's own slider (input_device), no separate endpoint key. The
    // column pulls up: REFRESH 290, note 324.
    HWND b = mkBtn(w, IDC_REFRESH, COLA_X, 290, 150, 26, "REFRESH DEVICES");
    regPage(b);                        // v6.6.2: page 0 only
    note(w, COLA_X, 324,
         "We recommend you wait 10 seconds before doing the tick test to prepare the audio buffer preload for accurate latency compensation measurements between each audio device.", 3);
    header(w, COLB_X, KHDR_Y, "GAIN");
    // v8.4.0 THE GAIN QUAD: gain_l/gain_r/gain_rl/gain_rr as four short
    // narrow edits in one group (the field's ask), gain_lfe just below,
    // gain_center under it -- the wide rows that stay single-key.
    {
        char bL[32], bR[32], bRL[32], bRR[32];
        snprintf(bL,  32, "%g", twsCfg.gainL);
        snprintf(bR,  32, "%g", twsCfg.gainR);
        snprintf(bRL, 32, "%g", twsCfg.gainRl);
        snprintf(bRR, 32, "%g", twsCfg.gainRr);
        const char* lbl[4]  = { "gain_l", "gain_r", "gain_rl", "gain_rr" };
        const int   ids[4]  = { IDC_GAIN_L, IDC_GAIN_R, IDC_GAIN_RL, IDC_GAIN_RR };
        const char* vals[4] = { bL, bR, bRL, bRR };
        quadRow(w, COLB_X, 118, 4, lbl, ids, vals);
    }
    // v8.5.0 THE SECOND QUAD: the remaining gains compacted below the
    // first -- gain_lfe / gain_center / gain_sl / gain_sr (the surround
    // volumes, new keys).
    {
        char bLfe[32], bCtr[32], bSL[32], bSR[32];
        snprintf(bLfe, 32, "%g", twsCfg.gainLfe);
        snprintf(bCtr, 32, "%g", twsCfg.gainCenter);
        snprintf(bSL,  32, "%g", twsCfg.gainSl);
        snprintf(bSR,  32, "%g", twsCfg.gainSr);
        const char* lbl[4]  = { "gain_lfe", "gain_center", "gain_sl", "gain_sr" };
        const int   ids[4]  = { IDC_GAIN_LFE, IDC_GAIN_CTR, IDC_GAIN_SL, IDC_GAIN_SR };
        const char* vals[4] = { bLfe, bCtr, bSL, bSR };
        quadRow(w, COLB_X, 156, 4, lbl, ids, vals);
    }
    // v6.6.8: capture_exclusive / exclusive / align_modes pills are gone --
    // the share-mode design is part of the engine now (always on).
    // v8.3.0: THE SETTINGS PAGE IS GONE -- its three survivors moved HERE.
    // v8.4.0: the lfe_fold pill joined the retired (the fold is BUILT IN,
    // always on; the gain_lfe row above still scales it); the gain quad
    // took the top of the column. v8.4.2: "launch at Windows startup" is
    // the column's LAST row -- a plain tglRow (label left, the ON/OFF pill
    // at the right), exactly every other row's shape, only REORDERED to
    // the bottom (v8.4.1's separately styled bottom-bar pill is gone).
    // The column ends at 308, v8.4.0's proven last-slot geometry.
    tglRow(w, COLB_X, 240, "enable_chime", IDC_TGL_CHIMES, twsCfg.chimes);
    row(w, COLB_X, 274, "chime_gain", IDC_CHIMGAIN, []{ char b[32]; snprintf(b,32,"%g",twsCfg.chimeGain); return std::string(b); }().c_str());
    tglRow(w, COLB_X, 308, "launch at Windows startup", IDC_TGL_STARTUP, startupEnabled());
    // v9.1.2 THE PAGE-0 NOTE: right below the startup row, the field's own
    // text (summarized by request) -- what an empty leg2/leg3 does, and
    // what the mains must be for each simulation.
    note(w, COLB_X, 336,
         "empty leg2 = simulated center: mains must be a stereo pair (2.1 with the\n"
         "sub if you have one); the center plays via HRTF + XTC.\n"
         "empty leg3 = simulated surround + rears: the rears fold with a 0.51 ms comb,\n"
         "the surround with frequency-boosted L/R phase inversion, wider than L/R alone.", 4);

    // ============ page 1: TIMING ==============================================
    // v8.4.5: the transport rows are GONE -- the bite is BUILT IN (v9.0.1:
    // the constant 128 fr x 3 request; no knob, one timing truth for every
    // first-run user). The page keeps the delay quad + out2_first, and the
    // note tells the built-in story. (v8.2.0 had unified the old TRANSPORT
    // page's rows into this page's left column; v8.4.5 retires them
    // whole, the out1ms/out2ms pattern.)
    g_tab = 1;
    // v8.4.7: the page moved from the right column to the LEFT
    // (field-directed) -- the rows themselves are unchanged.
    header(w, COLA_X, KHDR_Y, "TIMING");
    // v8.4.0 THE TIMING QUAD; v8.4.11 THE TRIPLE: delay1_ms/delay2_ms/
    // offset3_ms as short narrow edits in one group (the gain quad's twin)
    // -- every per-leg delay knob in one row (offset4_ms retired with the
    // one-rear-leg merge).
    {
        char b1[32], b2[32], b3[32];
        snprintf(b1, 32, "%g", twsCfg.delay1Ms);
        snprintf(b2, 32, "%g", twsCfg.delay2Ms);
        snprintf(b3, 32, "%g", twsCfg.offset3Ms);
        const char* lbl[3]  = { "delay1_ms", "delay2_ms", "offset3_ms" };
        const int   ids[3]  = { IDC_DELAY1, IDC_DELAY2, IDC_OFF3 };
        const char* vals[3] = { b1, b2, b3 };
        quadRow(w, COLA_X, 118, 3, lbl, ids, vals);
    }
    // v9.5.1: the out2_first pill is RETIRED -- the center-first order is
    // integrated; the timing note pulls up to its slot.
    // v7.0.0: the pin_outputs pill is gone -- output pinning is part of the
    // engine now (always on): both endpoint volumes are pinned 100% / unmuted,
    // except a leg that IS the current default playback device (v7.6.3 --
    // its slider stays the user's; yield_default releases the DAC anyway).
    // v8.2.0: the transport description (shortened) rides below the timing
    // one; v8.4.0: the quad pushed it down and the offsets joined the text.
    // v8.4.8: the two paragraphs after the first are GONE (field-directed:
    // "remove the two whole paragraphs after the first one from the timing
    // tab, because it's overflowing") -- the KS-bite and slow-sync stories
    // stay told by the boot banner and the ini.
    note(w, COLA_X, 170,
         "delay1/delay2 push one leg later, bit for bit -- put the delay on the\n"
         "EARLY leg. offset3_ms is the same knob for the rear leg (leg3 = the\n"
         "rears on ONE device; an empty rear device = no rear output). The\n"
         "banner prints every est path; 'drift' already includes your delay\n"
         "(it IS the alignment, not a fault).\n", 5);

    // v8.3.0: THE SETTINGS PAGE IS GONE -- nothing on it survived as a
    // setting: the drift trio + volume_follow are hardcoded constants, and
    // the startup + chime controls live on page 0 (CONFIGURATION) now.

    // ============ page 2: CENTER EQ (v8.3.0: was page 3) ============
    // v7.1.15/16 THE TIGHTENED STACK: the sliders rise to a 52 px pitch
    // (labels 196/248/300, tracks 214/266/318), SAVE TO SLOT lands BELOW
    // them (354..380, the sliders' own 400 px width) with its one-line
    // note right under it -- the whole stack ends at the panel edge, the
    // action row at 414 keeps its clearance, nothing overlaps. The right
    // column's description moved up to the panel top and got a rect sized
    // for its wrapped line count, so it never cuts off mid-sentence.
    g_tab = 2;
    tglRow(w, COLA_X, 114, "center EQ (out2 only)", IDC_TGL_EQ, twsCfg.centerEq);
    // v7.1.8: the four built-in presets are GONE -- five user save slots,
    // uniform like they were: 5 x 76 px faces + 4 x 5 px gaps span the
    // sliders' 400 px exactly. Left-click applies AND arms; the SAVE TO
    // SLOT button below the sliders writes the armed slot (v7.1.9).
    for (int i = 0; i < 5; ++i) {
        char nm[16];
        snprintf(nm, sizeof(nm), "SLOT %d", i + 1);
        regPage(mkBtn(w, IDC_SLOT1 + i, COLA_X + i * 81, 148, 76, 26, nm));
    }
    mkLabel(w, IDC_LBL_FREQ, COLA_X, 196, 400, "PEAK FREQUENCY", g_fUIb, 16);
    mkSlider(w, IDC_SLD_FREQ, COLA_X, 214, 400, 0, 1000);
    mkLabel(w, IDC_LBL_GAIN, COLA_X, 248, 400, "BOOST", g_fUIb, 16);
    mkSlider(w, IDC_SLD_GAIN, COLA_X, 266, 400, -24, 24);
    mkLabel(w, IDC_LBL_Q, COLA_X, 300, 400, "SHARPNESS (Q)", g_fUIb, 16);
    mkSlider(w, IDC_SLD_Q, COLA_X, 318, 400, 0, 1000);
    HWND sv = mkBtn(w, IDC_EQSAVE, COLA_X, 354, 400, 26, "SAVE TO SLOT");
    regPage(sv);                       // page 2 only (v8.3.0)
    note(w, COLA_X, 383,
         "makes sure this slot's current modifications persist across sessions.", 1);
    note(w, COLB_X, 74,
         "One peaking band on ch3, applied inside the engine's out2 mix path --\n"
         "out2 ONLY. The mains (out1) are never touched.\n\n"
         "A KS/WaveRT leg bypasses the Windows audio engine, so system-wide\n"
         "EQs (Equalizer APO and friends) can never see this stream. The\n"
         "boost lives here instead.\n\n"
         "SLOTS: left-click applies a slot and ARMS it -- the face stays\n"
         "accented while you tweak. Adjust the sliders, press SAVE TO SLOT\n"
         "and the curve + the EQ pill land in the armed slot (ini updated\n"
         "at once). Right-click a slot saves straight into it. Slot 1\n"
         "ships with 387 Hz / +3.5 dB / Q 0.30. The pill above is the off\n"
         "switch -- EQ off = the bit-exact path.\n\n"
         "The boost stacks with gain_center; the mix clamps at full scale,\n"
         "so loud center peaks may clip first -- lower gain_center if they\n"
         "do. Keep-alive and chime are NOT equalized (UI cues, not dialogue).", 20);
}

// ---- v7.0.4: the tab strip is ONE custom control ----------------------------
// Six owner-drawn BUTTONs lost the glow to system clipping for three versions
// (child DC clips at the tab rect, frame's WS_CLIPCHILDREN clips the other
// way). One control owns the whole strip: the halo is drawn around the
// selected face IN the strip's own double-buffered surface -- unclippable by
// construction, hugging the border, burning outward. The fade repaints only
// this one small control; the frame is never invalidated for a tab change.
static void guiShowPage(HWND w, int p);     // fwd: defined below

// rounded-rect path with NO 1 px inset (guiRoundPath is the face variant);
// the halo bands ride on exact offset curves of the face rect.
static Gdiplus::GraphicsPath* guiRoundPathRaw(const Gdiplus::RectF& r, float rad) {
    Gdiplus::GraphicsPath* p = new Gdiplus::GraphicsPath();
    const float d = rad * 2.0f;
    p->AddArc(r.X, r.Y, d, d, 180, 90);
    p->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p->CloseFigure();
    return p;
}

// strip-local geometry: the strip sits at (9, 28), each face 5 px in from the
// left edge of its slot and 6 px down -- so a 5 px halo fits inside the
// control on every side. Faces keep the exact positions (and the 23 px height
// with its 3 px bottom notch) the old buttons had.
static const int KSTRIP_X = 9, KSTRIP_Y = 28, KSTRIP_W = 866, KSTRIP_H = 36;   // design px -- S() at the creation site
// (KSTRIP_PAD / KSTRIP_FACE_Y are defined -- and scaled -- in the DPI block.)

// ---- v7.0.7: TRUE GPU -- Direct3D 11 + DXGI swap chain + Direct2D 1.1 ------
// The strip renders for real: D3D11 device (hardware first, WARP for RDP/VM),
// D2D 1.1 device context for halo/faces/borders, DirectWrite glyphs ON the
// GPU, presented through a BITBLT-model swap chain -- v7.0.6's flip model on
// a CHILD window fought GDI for the same HWND's pixels (flicker, stale theme
// colors, CPU burn). Bitblt = defined GDI interop, one GPU blit at Present.
// The whole stack unavailable -> the v7.0.4 GDI+ pass verbatim. IIDs spelled
// out literally -- no __uuidof, every toolchain links identically.
static ID2D1Factory1*        g_d2dFactory = nullptr;  // lazily created, once
static ID3D11Device*         g_d3d11      = nullptr;
static IDXGISwapChain1*      g_swap       = nullptr;
static ID2D1Device*          g_d2dDev     = nullptr;
static ID2D1DeviceContext*   g_dctx       = nullptr;
static ID2D1Bitmap1*         g_target     = nullptr;
static IDWriteFactory*       g_dwf        = nullptr;
static IDWriteTextFormat*    g_dwfTab     = nullptr;
static ID2D1SolidColorBrush* g_d2dBr      = nullptr;  // v7.0.7: ONE cached brush
static HDC                   g_stripDc    = nullptr;  // GDI+ fallback buffer
static HBITMAP               g_stripBm    = nullptr;
static HBITMAP               g_stripBmOld = nullptr;
static int                   g_stripW = 0, g_stripH = 0;
static bool                  g_gpuDead = false, g_gpuLogged = false;
static bool                  g_gpuPainted = false;    // v7.0.7: first-present flag
static std::wstring          g_tabWide[3];            // v7.0.7: labels widened once (v8.3.0: three)
// v7.0.7: the swap chain is the BITBLT model (DXGI_SWAP_EFFECT_DISCARD), not
// flip: a flip chain on a CHILD window keeps a persistent front buffer at
// DWM while GDI draws into the same HWND's redirection surface -- the two
// outvote each other (stale colors + flicker) and Presents block on vsync.
// Bitblt has DEFINED GDI interop, no persistent front buffer, and still
// rasterizes 100% on the GPU.
static const IID kTSIID_ID2D1Factory1 =
    { 0xbb12d362, 0xdaee, 0x4b9a, { 0xaa, 0x1d, 0x14, 0xba, 0x40, 0x1c, 0xfa, 0x1f } };
static const IID kTSIID_IDXGIDevice =
    { 0x54ec77fa, 0x1377, 0x44e6, { 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c } };
static const IID kTSIID_IDXGIFactory2 =
    { 0x50c83a1c, 0xe072, 0x4c48, { 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0x46, 0xe2 } };
static const IID kTSIID_IDXGISurface =
    { 0xcaf1c7ad, 0xeb31, 0x4269, { 0x92, 0xc5, 0xe4, 0xf4, 0x5b, 0xad, 0x81, 0x55 } };
static const IID kTSIID_IDWriteFactory =
    { 0xac338524, 0x30fa, 0x4a5e, { 0x9a, 0x6c, 0x8c, 0xc0, 0x38, 0xf3, 0x58, 0x5b } };

static D2D1_COLOR_F D2Col(COLORREF c, BYTE a) {
    return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f,
                        GetBValue(c) / 255.0f, a / 255.0f);
}

// the GDI+ fallback buffer: one 32bpp top-down DIB, rebuilt only when the
// strip's client size actually changes (it is 830x36 for the app's lifetime).
static bool guiStripBufEnsure(HWND w, int nw, int nh) {
    if (g_stripDc && nw == g_stripW && nh == g_stripH) return true;
    if (g_stripBmOld) {
        SelectObject(g_stripDc, g_stripBmOld);
        DeleteObject(g_stripBm);
        g_stripBm = nullptr; g_stripBmOld = nullptr;
    }
    if (g_stripDc) { DeleteDC(g_stripDc); g_stripDc = nullptr; }
    BITMAPV5HEADER bi = {};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = nw;
    bi.bV5Height      = -nh;                 // top-down
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;        // BGRA -- premultiplied-friendly
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;
    void* bits = nullptr;
    HDC wdc = GetDC(w);
    g_stripBm = CreateDIBSection(wdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(w, wdc);
    if (!g_stripBm) return false;
    g_stripDc = CreateCompatibleDC(nullptr);
    if (!g_stripDc) { DeleteObject(g_stripBm); g_stripBm = nullptr; return false; }
    g_stripBmOld = (HBITMAP)SelectObject(g_stripDc, g_stripBm);
    g_stripW = nw; g_stripH = nh;
    return true;
}

// the GPU stack, built once: D3D11 -> DXGI device/adapter/factory -> D2D 1.1
// device context -> bitblt swap chain -> backbuffer target -> cached brush
// -> DirectWrite tab format. Any failure tears it down, marks g_gpuDead and
// hands the strip to the GDI+ pass for good.
static void guiStripGpuRelease() {
    if (g_target) { g_target->Release(); g_target = nullptr; }
    if (g_dctx)   { g_dctx->Release();   g_dctx   = nullptr; }
    if (g_d2dBr)  { g_d2dBr->Release();  g_d2dBr  = nullptr; }  // v7.0.7
    if (g_swap)   { g_swap->Release();   g_swap   = nullptr; }
    if (g_dwfTab) { g_dwfTab->Release(); g_dwfTab = nullptr; }
    if (g_d2dDev) { g_d2dDev->Release(); g_d2dDev = nullptr; }
    if (g_d3d11)  { g_d3d11->Release();  g_d3d11  = nullptr; }
}

static bool guiStripGpuEnsure(HWND w, int cw, int ch) {
    if (g_gpuDead) return false;
    if (g_dctx) return true;
    if (!g_d2dFactory) {
        D2D1_FACTORY_OPTIONS fo = {};
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                     kTSIID_ID2D1Factory1, &fo,
                                     (void**)&g_d2dFactory))) {
            g_d2dFactory = nullptr;
            g_gpuDead = true;
            return false;
        }
    }
    UINT dfl = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_1,
                                      D3D_FEATURE_LEVEL_11_0,
                                      D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL flGot = D3D_FEATURE_LEVEL_9_1;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, dfl,
                                   fls, 3, D3D11_SDK_VERSION,
                                   &g_d3d11, &flGot, nullptr);
    bool warp = false;
    if (FAILED(hr)) {                    // no hardware D3D (VM / RDP session)
        warp = true;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, dfl,
                               fls, 3, D3D11_SDK_VERSION,
                               &g_d3d11, &flGot, nullptr);
    }
    if (FAILED(hr)) { guiStripGpuRelease(); g_gpuDead = true; return false; }
    IDXGIDevice*  xd = nullptr;
    IDXGIAdapter* xa = nullptr;
    IDXGIFactory2* xf = nullptr;
    bool ok = SUCCEEDED(g_d3d11->QueryInterface(kTSIID_IDXGIDevice, (void**)&xd)) &&
              xd && SUCCEEDED(xd->GetAdapter(&xa)) && xa &&
              SUCCEEDED(xa->GetParent(kTSIID_IDXGIFactory2, (void**)&xf)) && xf &&
              SUCCEEDED(g_d2dFactory->CreateDevice(xd, &g_d2dDev)) &&
              SUCCEEDED(g_d2dDev->CreateDeviceContext(
                  D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_dctx));
    if (ok) {   // v7.0.7: the flip variable is gone -- bitblt model only
        g_dctx->SetDpi(96.0f, 96.0f);    // DIPs == pixels: the geometry is authored in px
        g_dctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);  // v7.0.7: explicit AA
        // v7.1.2: the fallback text renders grayscale (was ALIASED v7.1.0 /
        // CLEARTYPE v7.0.6) -- same smooth family as the label layer.
        g_dctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        // v7.0.7: bitblt model, ONE buffer, Present(0,0) -- see the g_tabWide
        // block: flip on a child window fought GDI for the HWND's pixels.
        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width      = (UINT)cw;
        sd.Height     = (UINT)ch;
        sd.Format     = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage    = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount    = 1;
        sd.SwapEffect     = DXGI_SWAP_EFFECT_DISCARD;
        hr = xf->CreateSwapChainForHwnd(g_d3d11, w, &sd, nullptr, nullptr, &g_swap);
        ok = SUCCEEDED(hr) && g_swap != nullptr;
    }
    if (ok && !g_d2dBr) {                // v7.0.7: ONE brush, SetColor per draw
        ok = SUCCEEDED(g_dctx->CreateSolidColorBrush(
                D2D1::ColorF(0, 0, 0, 0), &g_d2dBr)) && g_d2dBr != nullptr;
    }
    if (ok) {                            // backbuffer -> D2D target bitmap
        IDXGISurface* s = nullptr;
        if (SUCCEEDED(g_swap->GetBuffer(0, kTSIID_IDXGISurface, (void**)&s)) && s) {
            const D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                  D2D1_ALPHA_MODE_PREMULTIPLIED));
            ok = SUCCEEDED(g_dctx->CreateBitmapFromDxgiSurface(s, &bp, &g_target));
            s->Release();
            if (ok) g_dctx->SetTarget(g_target);
        } else ok = false;
    }
    if (ok && !g_dwfTab) {               // DirectWrite: Segoe UI Semibold in
        if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,   // GDI px semantics
                                          kTSIID_IDWriteFactory, (IUnknown**)&g_dwf))) {
            HDC sdc = GetDC(nullptr);
            const int dpi = GetDeviceCaps(sdc, LOGPIXELSY);
            ReleaseDC(nullptr, sdc);
            // v7.1.20: the render target draws DIPs == pixels (SetDpi 96
            // above), so the 13 design px must arrive as PHYSICAL px:
            // 13 * dpi/96. (The old 96/dpi form was a no-op under the
            // DPI-unaware build -- dpi was always 96 -- and would have
            // SHRUNK the labels under the manifest's awareness.)
            const float px = 13.0f * (float)(dpi > 0 ? dpi : 96) / 96.0f;
            ok = SUCCEEDED(g_dwf->CreateTextFormat(
                    L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    px, L"", &g_dwfTab));
            if (ok) {
                g_dwfTab->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                g_dwfTab->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                g_dwfTab->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                const DWRITE_TRIMMING tr = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                IDWriteInlineObject* sign = nullptr;
                if (SUCCEEDED(g_dwf->CreateEllipsisTrimmingSign(g_dwfTab, &sign)) && sign) {
                    g_dwfTab->SetTrimming(&tr, sign);
                    sign->Release();
                } else {
                    g_dwfTab->SetTrimming(&tr, nullptr);
                }
            }
        } else ok = false;
    }
    if (xa) xa->Release();
    if (xf) xf->Release();
    if (xd) xd->Release();
    if (!ok) { guiStripGpuRelease(); g_gpuDead = true; return false; }
    if (!g_gpuLogged) {
        g_gpuLogged = true;
        twsLog("[twinspeak] gui           : tab strip on GPU -- Direct2D 1.1 / DXGI swap chain (bitblt model%s)\n",
             warp ? ", WARP adapter" : "");
    }
    return true;
}

// ---- v7.1.2: the strip's LABEL LAYER -- smooth like the buttons ------------
// The six labels are rasterized by the same code path as WM_DRAWITEM's
// buttons (guiAAString on GDI+), with TextRenderingHintAntiAliasGridFit --
// smooth, hinted, grayscale-antialiased glyphs (the buttons render with the
// system default, which is the same smooth family). The layer is transparent
// ARGB: grayscale AA writes proper per-pixel alpha, so it composites cleanly
// over the GPU geometry -- ClearType could not (it needs an opaque
// background). The GPU pass uploads the layer as a premultiplied D2D bitmap
// (no GDI touches the swap-chain HWND); the GDI+ legacy pass draws the same
// strings with the same hint directly. Colors: inactive = cText (the resting
// button color -- cDim washed out on the dark themes), active ramps
// cText -> cAccent with the fade.
static Gdiplus::Bitmap*   g_lblBmp = nullptr;
static Gdiplus::Graphics* g_lblG   = nullptr;
static int                g_lblW = 0, g_lblH = 0;
static void guiStripLabelsRelease() {
    delete g_lblG;  g_lblG  = nullptr;
    delete g_lblBmp; g_lblBmp = nullptr;
    g_lblW = 0; g_lblH = 0;
}
static Gdiplus::Bitmap* guiStripLabelLayer(int cw, int ch, double fs, HDC dc) {
    if (!g_lblBmp || g_lblW != cw || g_lblH != ch) {
        guiStripLabelsRelease();
        g_lblBmp = new Gdiplus::Bitmap(cw, ch, PixelFormat32bppARGB);
        if (g_lblBmp) g_lblG = new Gdiplus::Graphics(g_lblBmp);
        if (!g_lblBmp || !g_lblG) { guiStripLabelsRelease(); return nullptr; }
        g_lblW = cw; g_lblH = ch;
    }
    g_lblG->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g_lblG->Clear(Gdiplus::Color(0, 0, 0, 0));
    for (int i = 0, fx = KSTRIP_PAD; i < 3; fx += kTabW[i] + kTabGap, ++i) {
        const bool act = (i == g_tab);
        const RECT trc = { fx, KSTRIP_FACE_Y - 1, fx + kTabW[i], KSTRIP_FACE_Y + S(25) };
        guiAAString(*g_lblG, dc, trc, kTabNames[i],
                    act ? guiLerpRGB(guic::cText, guic::cAccent, fs) : guic::cText,
                    g_fUIb, 0);
    }
    return g_lblBmp;
}

static LRESULT CALLBACK tabStripProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;                       // the buffer covers every pixel opaquely
    case WM_LBUTTONUP: {
        const int x = (int)(short)LOWORD(lp);
        for (int i = 0, fx = KSTRIP_PAD; i < 3; fx += kTabW[i] + kTabGap, ++i)
            if (x >= fx && x < fx + kTabW[i]) {
                if (i != g_tab) guiShowPage(GetParent(w), i);
                return 0;
            }
        return 0; }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT rc; GetClientRect(w, &rc);
        const int cw = rc.right, ch = rc.bottom;
        if (cw <= 0 || ch <= 0) { EndPaint(w, &ps); return 0; }
        const double fs = guiTabFade();
        // v7.0.7 GPU PASS: everything on the swap chain's backbuffer --
        // geometry + DirectWrite glyphs, presented by Present(), never copied
        // by GDI. Once the GPU stack owns the window, nothing else draws to
        // it (the flip-era GDI mixing was the flicker); one one-time fill
        // covers the pre-first-present frame only.
        bool gpu = guiStripGpuEnsure(w, cw, ch);
        if (gpu && !g_gpuPainted) {
            FillRect(dc, &rc, g_brBg);   // exactly once: cover the pre-present frame
            g_gpuPainted = true;
        }
        if (gpu) {
            #define TSBRCOL(col, a) do { \
                g_d2dBr->SetColor(D2Col((col), (BYTE)(a))); } while (0)
            g_dctx->BeginDraw();
            g_dctx->Clear(D2Col(guic::cBg, 255));
            {   // THE HALO: three nested bands burning OUTWARD from the
                // selected tab's face (identical geometry to the GDI+ pass).
                int fxs = KSTRIP_PAD;
                for (int i = 0; i < g_tab; ++i) fxs += kTabW[i] + kTabGap;
                const float fx = (float)fxs, fy = (float)KSTRIP_FACE_Y;
                const float fw = (float)kTabW[g_tab], fh = Sf(23.0f);
                const float mid[3] = { 1.35f, 2.90f, 4.40f };  // band centers
                const float wd [3] = { 1.70f, 1.40f, 1.60f };  // band widths
                const BYTE  al [3] = { 80, 44, 22 };           // band strengths
                for (int k = 0; k < 3; ++k) {
                    const float s = Sf(mid[k]);
                    const D2D1_ROUNDED_RECT rr = {
                        D2D1_RECT_F{ fx - s, fy - s, fx + fw + s, fy + fh + s },
                        Sf(6.0f) + s, Sf(6.0f) + s };
                    TSBRCOL(guic::cAccent, al[k] * fs);
                    g_dctx->DrawRoundedRectangle(rr, g_d2dBr, Sf(wd[k]));
                }
            }
            for (int i = 0, ix = KSTRIP_PAD; i < 3; ix += kTabW[i] + kTabGap, ++i) {
                const bool act = (i == g_tab);
                // the face rect is the 1 px inset rounded rect (guiRoundPath
                // convention) -- the centered stroke stays fully inside.
                const D2D1_ROUNDED_RECT rr = {
                    D2D1_RECT_F{ (float)ix + 1.0f, (float)KSTRIP_FACE_Y + 1.0f,
                                 (float)ix + kTabW[i] - 1.0f,
                                 (float)KSTRIP_FACE_Y + Sf(22.0f) }, Sf(6.0f), Sf(6.0f) };
                if (act) {
                    TSBRCOL(guic::cPanelHi, 255 * fs);
                    g_dctx->FillRoundedRectangle(rr, g_d2dBr);
                }
                TSBRCOL(guic::cLine, 255);
                g_dctx->DrawRoundedRectangle(rr, g_d2dBr, 1.0f);
                if (act) {
                    TSBRCOL(guic::cAccent, 255 * fs);
                    g_dctx->DrawRoundedRectangle(rr, g_d2dBr, 1.0f);
                }
            }
            // v7.1.2 THE LABELS ARE SMOOTH: rasterized by the WM_DRAWITEM
            // code path (guiAAString on GDI+, AntiAliasGridFit) into a
            // transparent ARGB layer, uploaded as a premultiplied D2D bitmap
            // and blended over the GPU geometry -- grayscale AA carries
            // per-pixel alpha, so the glyphs composite cleanly and read as
            // soft as the buttons. Colors: inactive labels read cText (the
            // resting button color -- cDim washed out on the dark themes);
            // the active label ramps cText -> cAccent with the fade.
            bool labelsDrawn = false;
            if (Gdiplus::Bitmap* lb = guiStripLabelLayer(cw, ch, fs, dc)) {
                Gdiplus::BitmapData bd;
                const Gdiplus::Rect lr(0, 0, cw, ch);
                if (lb->LockBits(&lr, Gdiplus::ImageLockModeRead,
                                 PixelFormat32bppPARGB, &bd) == Gdiplus::Ok) {
                    const D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                          D2D1_ALPHA_MODE_PREMULTIPLIED));
                    ID2D1Bitmap* bm = nullptr;
                    if (SUCCEEDED(g_dctx->CreateBitmap(
                            D2D1::SizeU((UINT32)cw, (UINT32)ch),
                            bd.Scan0, (UINT32)bd.Stride, &bp, &bm)) && bm) {
                        g_dctx->DrawBitmap(bm,
                            D2D1::RectF(0.0f, 0.0f, (float)cw, (float)ch));
                        bm->Release();
                        labelsDrawn = true;
                    }
                    lb->UnlockBits(&bd);
                }
            }
            if (!labelsDrawn) {   // layer/upload failed: native text fallback
                for (int i = 0, fx = KSTRIP_PAD; i < 3; fx += kTabW[i] + kTabGap, ++i) {
                    const bool act = (i == g_tab);
                    if (g_tabWide[i].empty()) g_tabWide[i] = guiWide(kTabNames[i]);
                    TSBRCOL(act ? guiLerpRGB(guic::cText, guic::cAccent, fs)
                                : guic::cText, 255);
                    const D2D1_RECT_F tr = {
                        (float)fx, (float)KSTRIP_FACE_Y - 1.0f,
                        (float)(fx + kTabW[i]), (float)KSTRIP_FACE_Y + Sf(25.0f) };
                    g_dctx->DrawText(g_tabWide[i].c_str(),
                                     (UINT32)g_tabWide[i].size(), g_dwfTab, tr, g_d2dBr);
                }
            }
            #undef TSBRCOL
            const HRESULT ehr = g_dctx->EndDraw(nullptr, nullptr);
            if (SUCCEEDED(ehr)) {
                // v7.0.7: Present(0, 0) -- no vsync wait; Present(1,0) stalled
                // every synchronous RedrawWindow for up to a frame inside the
                // message handler. DWM composes at its own rate regardless.
                g_swap->Present(0, 0);
            } else {
                // device lost: tear the stack down; this frame falls through
                // to the GDI+ pass so the strip never shows garbage, and the
                // next paint rebuilds the GPU stack.
                gpu = false;
                guiStripGpuRelease();
            }
        }
        if (!gpu) {   // v6.6.5/7.0.4 LEGACY PASS: identical pixels, software
            if (!guiStripBufEnsure(w, cw, ch)) { EndPaint(w, &ps); return 0; }
            HDC buf = g_stripDc;
            FillRect(buf, &rc, g_brBg);
            Gdiplus::Graphics g(buf);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            // v7.1.2: tab labels smooth here too -- the GPU pass's label
            // layer and this hint render the same antialiased glyphs.
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            const double fs2 = fs;
            int fxs = KSTRIP_PAD;
            for (int i = 0; i < g_tab; ++i) fxs += kTabW[i] + kTabGap;
            const Gdiplus::RectF fr((Gdiplus::REAL)fxs,
                                    (Gdiplus::REAL)KSTRIP_FACE_Y,
                                    (Gdiplus::REAL)kTabW[g_tab], Sf(23.0f));
            const float mid[3] = { 1.35f, 2.90f, 4.40f };
            const float wd [3] = { 1.70f, 1.40f, 1.60f };
            const BYTE  al [3] = { 80, 44, 22 };
            for (int k = 0; k < 3; ++k) {
                const float s = Sf(mid[k]);
                const Gdiplus::RectF e(fr.X - s, fr.Y - s,
                                       fr.Width + 2.0f * s, fr.Height + 2.0f * s);
                Gdiplus::GraphicsPath* bp = guiRoundPathRaw(e, Sf(6.0f) + s);
                Gdiplus::Pen gp(GCol(guic::cAccent, (BYTE)(al[k] * fs2)), Sf(wd[k]));
                g.DrawPath(&gp, bp);
                delete bp;
            }
            for (int i = 0, fx = KSTRIP_PAD; i < 3; fx += kTabW[i] + kTabGap, ++i) {
                const bool act = (i == g_tab);
                const Gdiplus::Rect fr2(fx, KSTRIP_FACE_Y, kTabW[i], S(23));
                Gdiplus::GraphicsPath* p = guiRoundPath(fr2, S(6));
                if (act) {
                    Gdiplus::SolidBrush fb(GCol(guic::cPanelHi, (BYTE)(255 * fs2)));
                    g.FillPath(&fb, p);
                }
                Gdiplus::Pen pb(GCol(guic::cLine), 1.0f);
                g.DrawPath(&pb, p);
                if (act) {
                    Gdiplus::Pen ab(GCol(guic::cAccent, (BYTE)(255 * fs2)), 1.0f);
                    g.DrawPath(&ab, p);
                }
                delete p;
                const RECT trc = { fx, KSTRIP_FACE_Y - 1, fx + kTabW[i], KSTRIP_FACE_Y + S(25) };
                // v7.1.2: cText base -- same color math as the label layer
                guiAAString(g, buf, trc, kTabNames[i],
                            act ? guiLerpRGB(guic::cText, guic::cAccent, fs) : guic::cText,
                            g_fUIb, 0);
            }
            BitBlt(dc, 0, 0, cw, ch, buf, 0, 0, SRCCOPY);
        }
        EndPaint(w, &ps);
        return 0; }
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static void guiMakeTabStrip(HWND w) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSA wc = {};
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = tabStripProc;
        wc.hInstance     = GetModuleHandleA(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = "TidyTabStrip4";
        RegisterClassA(&wc);
        reg = true;
    }
    // v7.1.20: the design-px strip constants scale at the creation site.
    // v7.8.0: plain client space -- the tab strip sits at its design y under
    // the native caption (no strip lead).
    HWND ts = CreateWindowExA(0, "TidyTabStrip4", nullptr, WS_CHILD | WS_VISIBLE,
                    S(KSTRIP_X), S(KSTRIP_Y), S(KSTRIP_W), S(KSTRIP_H), w,
                    (HMENU)(INT_PTR)IDC_TABSTRIP, nullptr, nullptr);
    tsTrackChild(ts, KSTRIP_X, KSTRIP_Y, KSTRIP_W, KSTRIP_H);   // the dpi ledger
}

static void guiBuildControls(HWND w) {
    // bottom bar -- always visible (v6.6.5: slimmer, tighter)
    mkBtn(w, IDC_START, 14, 414, 140, 36, "");
    mkBtn(w, IDC_APPLY, 162, 414, 150, 36, "APPLY + RESTART");
    mkBtn(w, IDC_SAVE, 320, 414, 120, 36, "SAVE INI");
    // v7.0.0: the two new bottom-bar buttons -- the 120 bpm sync tick and the
    // theme cycler (label = the ACTIVE theme, kThemes[g_theme].name).
    mkBtn(w, IDC_TICK, 448, 414, 150, 36, "120 BPM TICK TEST");
    mkBtn(w, IDC_THEME, 606, 414, 150, 36, kThemes[g_theme].name);
    // v9.1.5: the diagnosis log's save dialog -- the ring IS the log now,
    // and this button writes it wherever the user points (it sits in the
    // freed band under the status text, opposite the keyboard hints)
    mkBtn(w, IDC_SAVLOG, 560, 516, 306, 26, "DOWNLOAD LOG FOR DIAGNOSIS");
    // v8.4.2: the bottom-bar startup pill is GONE -- "launch at Windows
    // startup" is a plain tglRow again, the LAST row of page 0's right
    // column (same label + pill style as every other row; only its ORDER
    // changed). The bar keeps its five controls; nothing else moved.
    // v7.1.5 FLICKER-FREE STATUS: the STATIC is HIDDEN -- it only stores the
    // string; guiPaintFrame draws the band inside the frame buffer (one blit,
    // no erase-then-draw flicker four times a second). Writers go through
    // guiSetStatus, which invalidates the status band of the frame.
    // v7.1.20: the direct creations scale too (the factories do it for the
    // rest).
    // v9.0.2: THE THREE-LINE STATUS BAR. The band is three 18px rows (54px):
    // row 1 the engine state ("engine started"), rows 2-3 the health fields
    // split 4+4 -- the old single line appended all ten and clipped past
    // ~106 mono chars. A '\n' in the stored string starts the next row
    // (guiPaintFrame splits on it); the hidden STATIC never shows it.
    HWND st = CreateWindowExA(0, "STATIC", "engine stopped", WS_CHILD,
                              S(14), S(458), S(852), S(54),
                              w, (HMENU)(INT_PTR)IDC_STATUS, nullptr, nullptr);
    SendMessageA(st, WM_SETFONT, (WPARAM)g_fMono, TRUE);
    tsTrackChild(st, 14, 458, 852, 54, &g_fMono);   // the dpi ledger
    // v6.6.6: no WS_EX_CLIENTEDGE -- the etched 3D edge rendered in light
    // classic colors was the "ghost bordered box"; WM_PAINT draws the frame.
    // v9.1.5: THE PANE IS GONE -- the EDIT control, its font ledger, its
    // dark subclass and its flicker buffer are retired whole. The status
    // band above is the only text surface; the log is the ring, one button
    // away (IDC_SAVLOG in the hint band below).
    // tab strip -- v7.0.4: ONE custom control (see tabStripProc above). The
    // six BUTTONs, their regions and the frame-painted halo are all retired.
    guiMakeTabStrip(w);
    guiBuildPages(w);
}

static void guiShowPage(HWND w, int p) {
    // v7.0.9: FLUSH EVERY PENDING RENDER FIRST (field directive: "make sure
    // every render updates before the atomic repaint"). Any window still
    // holding an invalidated region paints NOW, under the OLD page -- nothing
    // stale can land during or after the switch.
    UpdateWindow(w);
    // v7.0.6: the page switch is ATOMIC. The old hide-then-show let exposed
    // parent rects hit the screen between the two passes -- one blank frame.
    // WM_SETREDRAW freezes the frame through both; ONE synchronous
    // RedrawWindow then shows the finished page immediately, no fade on the
    // content (the tabs keep theirs -- that is the wanted animation).
    SendMessageA(w, WM_SETREDRAW, FALSE, 0);   // v7.0.6: freeze (no MFC SetRedraw in Win32)
    for (int i = 0; i < 3; ++i)
        for (size_t k = 0; k < g_pageHw[i].size(); ++k)
            ShowWindow(g_pageHw[i][k], i == p ? SW_SHOW : SW_HIDE);
    SendMessageA(w, WM_SETREDRAW, TRUE, 0);
    g_tab = p;
    // v7.0.8: the fade anchor is stamped BEFORE the atomic repaint. The
    // reverse order made the synchronous pass read a STALE anchor (full
    // glow), the next tick fs ~ 0, then the ramp back: full -> off -> fade-in
    // on every click. Anchor first = the paint is already fs ~ 0 and the
    // glow climbs monotonically.
    g_tabAnimStart = GetTickCount64();
    // v7.7.8 THE TAB-SWITCH FLICKER (the field: "on tab menu change, the
    // uDWM window flickers on redraw, which is not cool"). The RDW_ERASE
    // here -- plus the erase WM_SETREDRAW(TRUE) queues -- ran WM_ERASEBKGND
    // over the WHOLE client before the buffered paint: one flat theme-color
    // intermediate frame the DWM sampled on every click (the v7.0.9 note's
    // "nothing to flicker" stopped holding when v7.6.13's never-black belt
    // brought the erase back). The frame never changes on a tab switch --
    // only the page children do -- so the erase is pure loss now: RDW_NOERASE
    // suppresses the pending erase flags, the synchronous pass re-renders
    // the buffered face and lands it as ONE blit, and the intermediate state
    // is the PREVIOUS frame -- no flat flash, no blank beat. The belt stays
    // armed on every other repaint path (theme changes, resizes, DPI).
    RedrawWindow(w, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_NOERASE);
    // v7.0.4/v7.0.5: only the strip animates. timeBeginPeriod(1) for the
    // life of the animation -- a bare 16 ms WM_TIMER lands on the ~15.6 ms
    // tick and the eased ramp quantizes into visible steps. One begin per
    // run, released exactly once (WM_TIMER id 2).
    if (!g_fadePeriod) { timeBeginPeriod(1); g_fadePeriod = true; }
    SetTimer(w, 2, 16, nullptr);
    if (HWND ts = GetDlgItem(w, IDC_TABSTRIP)) InvalidateRect(ts, nullptr, TRUE);
}

// ---- v6.6.1: repaint helpers -----------------------------------------------
// Owner-drawn children own their pixels: invalidating the frame never
// repaints them (the "only updates when I minimize" bug). Every visual state
// change invalidates the exact child it changed.
static void guiRepaint(HWND w, int id) {
    if (HWND h = GetDlgItem(w, id)) InvalidateRect(h, nullptr, TRUE);
}
static void guiRepaintSlots(HWND w) {
    for (int i = 0; i < 5; ++i) guiRepaint(w, IDC_SLOT1 + i);
}

// ---- v7.0.6: the button render buffer (THE 1PX CLIP FIX) --------------------
// One shared memory surface, grown to the largest button seen, with one
// spare column/row -- the gutter absorbs the border stroke's outer AA half a
// DC-sized surface clipped away -- and the finished face lands as ONE opaque
// blit cropped to the item rect.
static HDC     g_btnDc    = nullptr;
static HBITMAP g_btnBm    = nullptr, g_btnBmOld = nullptr;
static int     g_btnW = 0, g_btnH = 0;
static HDC guiBtnBufEnsure(int w, int h) {
    if (g_btnDc && w + 1 <= g_btnW && h + 1 <= g_btnH) return g_btnDc;
    if (g_btnBmOld) {
        SelectObject(g_btnDc, g_btnBmOld);
        DeleteObject(g_btnBm);
        g_btnBm = nullptr; g_btnBmOld = nullptr;
    }
    if (g_btnDc) { DeleteDC(g_btnDc); g_btnDc = nullptr; }
    HDC sdc = GetDC(nullptr);
    g_btnDc = CreateCompatibleDC(sdc);
    g_btnBm = CreateCompatibleBitmap(sdc, w + 1, h + 1);   // the +1: the AA gutter
    ReleaseDC(nullptr, sdc);
    if (!g_btnDc || !g_btnBm) {
        if (g_btnDc) { DeleteDC(g_btnDc); g_btnDc = nullptr; }
        if (g_btnBm) { DeleteObject(g_btnBm); g_btnBm = nullptr; }
        return nullptr;
    }
    g_btnBmOld = (HBITMAP)SelectObject(g_btnDc, g_btnBm);
    g_btnW = w + 1; g_btnH = h + 1;
    return g_btnDc;
}

// ---- v6.6.1: "Run at Windows startup" (HKCU Run key, no shell:startup hack)
static const char* kRunKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const char* kRunVal = "twinspeak";

static bool startupEnabled() {
    HKEY k;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return false;
    const LONG r = RegQueryValueExA(k, kRunVal, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}
static void startupSet(bool on) {
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (on) {
        char exe[MAX_PATH];
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string cmd = std::string("\"") + exe + "\" --tray";
        RegSetValueExA(k, kRunVal, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)cmd.size() + 1);
    } else {
        RegDeleteValueA(k, kRunVal);
    }
    RegCloseKey(k);
}

// ---- v6.6.1: the tray icon --------------------------------------------------
// v7.1.20: the tip reflects the engine state (updated on start/stop only --
// no polling), a failed NIM_ADD retries every 2 s on timer id 3 (the
// tray-only sign-in launch can beat the shell and must recover), and the
// icon handle is LR_SHARED so re-adds after an explorer restart leak
// nothing.
static void trayUpdateTip(bool running) {
    if (!g_trayVisible) return;
    g_nid.uFlags = NIF_TIP;
    lstrcpynA(g_nid.szTip, running ? "TidyVision TwinSpeak -- engine running"
                                   : "TidyVision TwinSpeak -- engine stopped",
              (int)sizeof(g_nid.szTip));
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}
static void trayRemove() {
    if (!g_trayVisible) return;
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
    g_trayVisible = false;
}
static void trayAdd() {
    if (!g_hwnd || g_trayVisible) return;
    // v7.2.12: a re-add (explorer restart) keeps the ORIGINAL owner window.
    // The old code re-pointed the icon at the current g_hwnd every time, so
    // a TaskbarCreated re-add after the panel was built silently moved the
    // callback from the tray surrogate to the panel -- harmless while both
    // live, but it breaks the owner-only TaskbarCreated guard below and it
    // is a surprise nobody asked for. First add: the current window.
    const HWND owner = g_nid.hWnd ? g_nid.hWnd : g_hwnd;
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = owner;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP + 3;
    // v7.2.12: the tray wants the SMALL icon. LR_DEFAULTSIZE loaded the
    // 32 px shell icon and the notification area down-scaled it to the
    // tray's small size on every draw -- the soft-looking icon next to
    // every other tray icon. SM_CXSMICON/SM_CYSMICON is the size the tray
    // actually renders (16 px @ 96 dpi, DPI-scaled), the same pair the
    // window class loads for hIconSm.
    g_nid.hIcon = (HICON)LoadImageA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(1),
                                    IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    lstrcpynA(g_nid.szTip, "TidyVision TwinSpeak", (int)sizeof(g_nid.szTip));
    g_trayVisible = Shell_NotifyIconA(NIM_ADD, &g_nid) != FALSE;
    if (!g_trayVisible) {
        // v7.1.20: the shell was not ready (a sign-in launch can beat the
        // taskbar). Retry on the owner window until it lands; TaskbarCreated
        // covers the explorer-restart case on top.
        SetTimer(g_hwnd, 3, 2000, nullptr);
        return;
    }
    // v7.2.12: kill the retry wherever it was armed -- the failed add may
    // have armed timer 3 on the SURROGATE while this success runs after the
    // panel took g_hwnd over (an orphan 2 s no-op timer otherwise).
    KillTimer(owner, 3);
    if (g_hwnd != owner) KillTimer(g_hwnd, 3);
    // v7.2.12: the tip must match the engine the moment the icon lands. A
    // sign-in add that needed the retry timer landed AFTER guiStartEngine's
    // trayUpdateTip -- which was swallowed (the icon did not exist yet) --
    // and the tip then stayed the generic one for the whole session.
    trayUpdateTip(g_engState.load() != 0);
    // v7.5.1: the v7.1.20 "explain the first 3 closes" balloon block that
    // lived here is GONE. It was dead code from the day it shipped (trayAdd
    // early-returns while the icon is already up -- always, in the normal
    // close-to-tray flow), and the one balloon twinspeak answers a close
    // with now lives in WM_CLOSE, text the field chose, every close.
}
// v7.1.6: a balloon with caller-supplied text. The start refusals speak
// through it, and since v7.5.1 so does the close-to-tray confirmation --
// "TwinSpeak is now in system tray.", fired AFTER the hide so the shell
// never sees a notification whose owner window is mid-vanish.
static void trayBalloon(const char* text) {
    if (!g_trayVisible) return;
    g_nid.uFlags       = NIF_INFO;
    lstrcpynA(g_nid.szInfoTitle, "TidyVision TwinSpeak", (int)sizeof(g_nid.szInfoTitle));
    lstrcpynA(g_nid.szInfo, text, (int)sizeof(g_nid.szInfo));
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

// ---- v7.1.20: window-level helpers ------------------------------------------
// UIPI: an elevated twinspeak must still receive explorer's TaskbarCreated
// broadcast and the second-instance messages, or the tray icon is lost on
// an explorer restart and duplicate launches fall to the MessageBox.
static void tsAllowUiMessages(HWND w) {
    if (!w) return;
    // local types: header-independent (old/mingw SDKs may not declare them)
    typedef struct TS_CFS { DWORD cbSize; DWORD ExtStatus; } TS_CFS;
    typedef BOOL (WINAPI *TS_CWMFE)(HWND, UINT, DWORD, TS_CFS*);
    #ifndef MSGFLT_ALLOW
    #define MSGFLT_ALLOW 1
    #endif
    const TS_CWMFE f = (TS_CWMFE)(void*)GetProcAddress(
        GetModuleHandleA("user32.dll"), "ChangeWindowMessageFilterEx");
    if (!f) return;                       // pre-Win7: no UIPI, nothing to do
    f(w, g_taskbarCreated, MSGFLT_ALLOW, (TS_CFS*)nullptr);
    f(w, WM_APP + 4,       MSGFLT_ALLOW, (TS_CFS*)nullptr);
    f(w, WM_APP + 5,       MSGFLT_ALLOW, (TS_CFS*)nullptr);
    f(w, WM_APP + 6,       MSGFLT_ALLOW, (TS_CFS*)nullptr);   // v7.2.1: --exit clean stop
}
// A real, keyboard-reachable quit: "Exit twinspeak" joins the system menu
// (Alt+Space, or right-click the bar) next to Close -- the only quit was
// buried in the tray menu.
static void tsAddSysExit(HWND w) {
    HMENU sm = GetSystemMenu(w, FALSE);
    if (!sm) return;
    AppendMenuA(sm, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(sm, MF_STRING, IDM_TRAY_EXIT, "Exit twinspeak (stops the engine)");
}
// Clamp an open window back into the nearest monitor's work area -- the bar
// stays reachable after a monitor unplug, a resolution drop, or a restored
// position from a since-disconnected display.
static void guiClampToWorkArea(HWND w) {
    if (!w || !IsWindow(w)) return;
    RECT wr; if (!GetWindowRect(w, &wr)) return;
    HMONITOR mon = MonitorFromRect(&wr, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(mon, &mi)) return;
    int x = wr.left, y = wr.top;
    const int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    if (y + wh > mi.rcWork.bottom) y = mi.rcWork.bottom - wh;
    if (y < mi.rcWork.top) y = mi.rcWork.top;      // taller than the work area: pin the bar
    if (x + ww > mi.rcWork.right) x = mi.rcWork.right - ww;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (x != wr.left || y != wr.top)
        SetWindowPos(w, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
// Window-position persistence: captured on every hide / shutdown, spliced
// into the ini by the normal save (gui_window_x / gui_window_y).
// (g_guiWinX / g_guiWinY are defined above loadIni -- v7.1.20.)
static void guiCaptureWindowPos(HWND w) {
    // v7.1.20: only the real panel -- never the hidden tray surrogate (a
    // taskkill WM_CLOSE broadcast would otherwise capture its dummy rect).
    if (!w || !IsWindow(w) || w == g_trayWnd || w != g_hwnd) return;
    RECT wr; if (!GetWindowRect(w, &wr)) return;
    g_guiWinX = wr.left;    // full int: virtual-screen coords can be negative
    g_guiWinY = wr.top;
}
// ---- v7.1.6 TRAY-FIRST: the deferred control window -------------------------
// A --tray (startup) launch builds NOTHING but the invisible tray owner; the
// real panel is constructed HERE, on demand: the first tray double-click,
// "Open twinspeak" in the tray menu, or a second instance's "come up".
// The same sequence the boot path used to run at sign-in runs exactly once,
// in the same order, so the finished UI still paints before it is ever
// shown (the v6.6.5 white-flash trick is preserved: create hidden, render
// the whole tree into the redirection surface, show after).
// The panel is a NORMAL top-level window: while tray-only nothing is shown
// so nothing takes a taskbar slot; the moment the panel opens it gets its
// taskbar button (and Alt-Tab entry) so it can be seen and reached like any
// window; closing hides it and the button goes away -- the tray icon remains.
static bool guiCreateRealWindow() {
    if (g_hwnd && g_hwnd != g_trayWnd) return true;   // already built
    HINSTANCE hi = GetModuleHandleA(nullptr);
    // v7.8.0 THE PLAIN NATIVE FRAME, FINISHED. The window is a stock
    // WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN: the caption, the icon, the
    // title, the three caption buttons (WS_SYSMENU gates the group -- it is
    // BACK on the style, so the native min/max/close return), the Alt+Space
    // system menu, Alt+F4, the snap layouts, the resize edges, the shadow
    // and the DPI story are all Windows' own, pixel-perfect by definition at
    // every scale. No WM_NCCALCSIZE insets, no WM_NCHITTEST bands, no DWM
    // extend, no painted band -- everything the v7.6.x/v7.7.x line grew to
    // fake the frame is retired. The outer size comes from AdjustWindowRectEx
    // around the designed 880x620 client, exactly what a plain app does.
    // v9.0.1 THE FIXED WINDOW (field-directed: "Also make the window
    // unresizable too."). WS_THICKFRAME and WS_MAXIMIZEBOX are OFF the style:
    // the frame is the classic fixed-size dialog -- thin border, caption,
    // minimize + close buttons, no resize edges, no maximize button, no
    // caption-double-click maximize, no snap (drag-to-edge and Win+arrows
    // resize, so they are gone with the edges). The rest of the v7.8.0 plain
    // native frame story stands: the system owns the caption, the icon, the
    // buttons, the shadow and the DPI story. The outer size still comes from
    // AdjustWindowRectEx around the designed 880x542 client (v9.1.5: the
    // status band's third row), and
    // WM_GETMINMAXINFO pins min==max track to that size at the CURRENT DPI
    // (see guiWndProc) -- WM_DPICHANGED re-derives the scale first and sizes
    // the window itself, so a monitor change still lands the right frame.
    // v7.1.26 THE WINDOW IS UNICODE, END TO END: CreateWindowExW +
    // DefWindowProcW -- the title is the full brand the taskbar, Alt-Tab
    // and Task Manager's app entry read.
    DWORD kstyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU
                   | WS_MINIMIZEBOX | WS_CLIPCHILDREN;   // v9.0.1: fixed size
    RECT krc = { 0, 0, KCLI_W, KCLI_H };
    AdjustWindowRectEx(&krc, kstyle, FALSE, 0);
    const int kww = krc.right - krc.left;
    const int kwh = krc.bottom - krc.top;
    g_hwnd = CreateWindowExW(0, L"twinspeak.control",
                             L"TidyVision TwinSpeak",     // v7.2.1: the brand the taskbar,
                                                          // Alt-Tab and Task Manager read
                             kstyle,
                             CW_USEDEFAULT, CW_USEDEFAULT, kww, kwh,
                             nullptr, nullptr, hi, nullptr);
    if (!g_hwnd) return false;
    {   // v7.8.0: the ONLY DWM touches left on the plain frame -- the square
        // corners (DWMWCP_DONOTROUND, the v7.1.23 standing order; a no-op on
        // Windows 10, where the frame is flat by default) and the immersive
        // dark-mode dial (guiApplyDwmMode re-reads the theme and programs
        // DWMWA_USE_IMMERSIVE_DARK_MODE with the 19 fallback; applyTheme
        // parked the mode pre-creation, this call lands it on the live
        // window). The NC rendering policy pin and the glass-sheet extend
        // ({0,0,32,0}) are retired with the custom chrome -- the default
        // frame owns its own pixels now. (dwmapi.h is not included -- the
        // house spelled-out typedefs.)
        typedef HRESULT (WINAPI *TS_DWMSA)(HWND, DWORD, LPCVOID, DWORD);
        const TS_DWMSA dwmSet = (TS_DWMSA)GetProcAddress(
            LoadLibraryA("dwmapi.dll"), "DwmSetWindowAttribute");
        if (dwmSet) {
            const int corner = 1;   // DWMWCP_DONOTROUND -- square corners
            dwmSet(g_hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                   &corner, sizeof(corner));
        }
        guiApplyDwmMode();
    }
    // v7.1.20: keyboard-reachable quit in the system menu + UIPI openings.
    tsAddSysExit(g_hwnd);
    tsAllowUiMessages(g_hwnd);
    // v7.1.20: restore the last captured position (spliced into the ini by
    // the normal save); guiClampToWorkArea at show time catches monitors
    // that have gone away since.
    if (g_guiWinX != -99999 && g_guiWinY != -99999)
        SetWindowPos(g_hwnd, nullptr, g_guiWinX, g_guiWinY, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    guiBuildControls(g_hwnd);
    controlsFromCfg(g_hwnd);
    guiShowPage(g_hwnd, 0);
    guiFillCombos(g_hwnd);
    // v6.6.5 WHITE-FLASH FIX: render the ENTIRE tree into the (still hidden)
    // redirection surface now, so the first frame DWM composites is the
    // finished UI; SW_SHOWNA then costs no extra activate/erase cycle.
    RedrawWindow(g_hwnd, nullptr, nullptr,
                 RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
    SetTimer(g_hwnd, 1, 250, nullptr);

    // v9.1.5: no drain -- the ring IS the log now, saved on demand
    // (IDC_SAVLOG); the health line feeds the status band through
    // g_lastHealth on its own.
    twsLog("[twinspeak] gui           : control window up -- ini at %s\n", g_guiIniPath.c_str());
    return true;
}

// ---- v7.1.7 was: the taskbar slot, managed explicitly. RETIRED v7.1.26. -----
// The panel used to register itself through ITaskbarList::AddTab on every
// show (a v7.1.7 workaround for a button that lagged behind a no-activate
// show) and DeleteTab on every hide. On Win11's labeled taskbar that
// registration is WORSE than redundant: the shell files the button as a
// TAB, and tabs render as minimal-width items -- icon + first letter of
// the title -- which is exactly the one-letter stub the field kept seeing
// through v7.1.23's process AUMID and v7.1.25's per-window AUMID. A normal
// visible top-level window gets its NATIVE button when shown (full size,
// full label, native active highlight) and loses it when hidden, with zero
// registration from us. The whole hand-rolled ITaskbarList vtable goes with
// it; the per-window System.AppUserModel.ID (v7.1.25) went with v7.4.9.

// ---- v7.1.25 was: the taskbar label, claimed at the WINDOW level. RETIRED
// ---- v7.4.9. --------------------------------------------------------------
// The panel carried a per-window System.AppUserModel.ID through
// SHGetPropertyStoreForWindow + IPropertyStore::SetValue (shell32 exports,
// resolved dynamically -- the house trick). v7.4.9 retires it whole, and the
// panel is a fully VANILLA window again. Why:
//   * It never fixed what it was added for. Its own birth note (v7.1.25)
//     says the one-letter stub "the field kept seeing" survived the
//     process AUMID (v7.1.23) AND this property; the stub actually died
//     with v7.1.26's end-to-end Unicode conversion (an ANSI title read of
//     a UTF-16 title stops at the first byte -- "T").
//   * It was the ONE thing that made this window not-a-normal-window. On
//     Win11's labeled taskbar an AUMID-carrying window's button belongs to
//     the AUMID group's lifecycle rather than the plain window path -- and
//     this app runs the exact setup the labeled taskbar is documented to
//     mishandle: TWO processes (the sign-in --tray twin and the plain
//     launch) sharing one AUMID. v7.4.7/v7.4.8 exercised the show, the
//     foreground and the style channels and the button still stayed
//     unborn; a vanilla window rides the default path instead -- visible
//     top-level window -> NATIVE button on show/activation, the path every
//     normal Win32 app uses and the path the user's own unfocus/refocus
//     was already successfully forcing.
//   * The process AUMID (v7.1.23, guiMain) stays: grouping, jumplist and
//     the balloon notifications hang off it, and the taskbar label reads
//     the window TITLE ("TidyVision TwinSpeak") like any normal window.

// ---- v7.4.9: THE NAME-CHANGE CHANNEL ---------------------------------------
// The fourth and last birth channel, and the one that cannot be filtered by
// the paths above: EVENT_OBJECT_NAMECHANGE. A title write on a visible
// top-level window makes the taskbar re-read the window and birth the
// NATIVE button if it is still missing -- the standard remedy for a button
// that never appeared. The flip is invisible: the trailing space renders
// identically, and the bar paints its own title bytes anyway.
static int g_tbNag = 0;   // v7.4.9: pending re-announces, one per 250 ms tick
static int g_tbCycle = 0; // v7.4.10: ticks until the second activation replays
                          // (the unfocus + refocus replay -- see
                          // tsReplayActivation below; armed at every first
                          // show, spent in WM_TIMER)
static bool g_tbForce = false; // v7.4.11: the armed replay TAKES the
                               // foreground first (set by the
                               // show-from-hidden arm -- the tray path,
                               // where the foreground belongs to the tray
                               // after the click and a guard-only replay
                               // would never run; cleared when spent)

static void tsAnnounceToTaskbar(HWND w) {
    if (!w || !IsWindow(w)) return;
    wchar_t title[128];
    const int n = GetWindowTextW(w, title, 126);
    if (n <= 0) return;
    wchar_t bumped[128];
    wcscpy(bumped, title);
    wcscat(bumped, L" ");
    SetWindowTextW(w, bumped);   // fires EVENT_OBJECT_NAMECHANGE
    SetWindowTextW(w, title);    // fires it again, restores the exact title
}

// ---- v7.4.8: THE BUTTON AUDIT ------------------------------------------------
// The taskbar button is the shell's own birth: a visible top-level window
// gets one. But the birth rides the shell's event channels, and the field
// machine has now missed it on BOTH: the show event (v7.1.7's "a button
// that lagged behind a no-activate show" -- v7.4.7 made the first show
// activating and the button STILL stayed unborn), and the foreground event
// (the launch activation can be suppressed by the foreground lock, so no
// foreground event fires for the window at all -- the user's own unfocus +
// refocus cycle is the first real foreground event the shell sees, which is
// exactly when the button finally appeared). The audit makes the birth
// independent of both channels, after every first show:
//   1. If the window did not actually win the foreground, take it with the
//      thread-input attach dance (the classic SetForegroundWindow unlock:
//      attach to the foreground thread's input queue, call, detach -- the
//      foreground lock denies a plain call from a process whose launch
//      grant has gone stale).
//   2. Assert WS_EX_APPWINDOW once. A button-worthiness style change is a
//      THIRD channel that depends on neither the show event nor the
//      foreground event -- win32k announces the style change, the taskbar
//      re-evaluates the window and births the NATIVE button (full size,
//      full label). This is a style bit, NOT ITaskbarList::AddTab -- AddTab
//      is what Win11 files as a TAB and renders as a one-letter stub (the
//      v7.1.26 trap). For a normal top-level window the bit is what the
//      default styles already produce, so it changes nothing visually; it
//      only guarantees the button can never stay unborn.
//   3. (v7.4.9) Re-announce the window title (the name-change channel) and
//      arm the 250 ms nag budget -- see tsAnnounceToTaskbar below.
static void tsEnsureTaskbarButton(HWND w) {
    if (!w || !IsWindow(w)) return;
    if (GetForegroundWindow() != w) {
        const DWORD fgTid = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const DWORD myTid = GetCurrentThreadId();
        const BOOL attached = (fgTid && fgTid != myTid)
                              ? AttachThreadInput(myTid, fgTid, TRUE) : FALSE;
        SetForegroundWindow(w);
        SetFocus(w);
        if (attached) AttachThreadInput(myTid, fgTid, FALSE);
    }
    const LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
    if (!(ex & WS_EX_APPWINDOW)) {
        SetWindowLongPtrW(w, GWL_EXSTYLE, ex | WS_EX_APPWINDOW);
        SetWindowPos(w, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // v7.4.9: the name-change channel -- the birth channel that depends on
    // neither the show, nor the foreground, nor the style event. Fired here
    // once immediately and then once per 250 ms tick for ~3 s (the nag
    // budget), which covers the sign-in race: a panel shown while the
    // taskbar is still starting misses EVERY birth event, and the
    // re-announce is the first channel it processes once it listens.
    // Announce-only: no foreground stealing, no style churn, invisible (the
    // bar paints its own title; the flip is one trailing space apart).
    tsAnnounceToTaskbar(w);
    g_tbNag = 12;
}

// ---- v7.4.10: THE SECOND ACTIVATION -----------------------------------------
// The one birth trigger the field machine has NEVER missed is the user's
// own unfocus + refocus cycle: the panel loses the foreground, the panel
// regains it -- a SECOND activation of an ALREADY-VISIBLE window. Every
// channel the audit exercised (show v7.4.7, foreground v7.4.8, style
// v7.4.8, name-change v7.4.9) fires at or right after the FIRST
// activation; this is the one re-evaluation that happens after, and it
// is the one that always works there. No version ever replayed it: the
// fresh start shows-and-activates in one transaction and tsEnsureTask
// BarButton skips its dance when the window IS foreground, so after the
// first activation nothing ever happens again. This replays the pair
// programmatically: a 1x1 off-screen proxy takes the foreground for one
// instant and hands it straight back. Proxy rules: WS_EX_TOOLWINDOW (no
// taskbar entry, no Alt-Tab entry), 1x1 at -32000,-32000 (off every
// monitor), the pre-registered "static" class (nothing to register).
// The guard is the point: run ONLY while the panel still owns the
// foreground. If the user already clicked away, either they came back
// (their refocus was the real replay) or they are elsewhere (we never
// steal focus -- their next refocus births the button as it always has).
// The blip itself is one activation round trip inside our own process:
// the bar repaints active -> inactive -> active through its buffered
// paths and the fade presenter re-presents -- nothing else moves.
// v7.4.11: the tray come-up arms the replay FORCED (g_tbForce). The click
// granted the foreground to the tray, this process has no grant of its
// own, and the restore's foreground calls can lose that race -- the
// plain replay would exit at the guard every time and the button would
// wait for a manual unfocus + refocus. Forced, the replay takes the
// foreground with the attach dance first (the user asked for this
// window), then runs the blip unchanged. The plain-start arm stays
// unforced -- v7.4.10 as shipped.
static void tsReplayActivation(HWND panel, bool force) {
    if (!panel || !IsWindow(panel) || !IsWindowVisible(panel)
        || IsIconic(panel) || g_hwnd != panel) return;
    if (GetForegroundWindow() != panel) {
        if (!force) return;
        // v7.4.11: the forced replay does not stand down here -- it TAKES
        // the foreground first, with the v7.4.8 attach dance. The click on
        // the tray icon granted the foreground to the tray (Explorer);
        // this process, running hidden since sign-in, has no grant of its
        // own, so the activating show, the plain SetForegroundWindow and
        // even the audit's dance in guiRestoreWindow can all lose that
        // race -- 500 ms later the panel is still not foreground, the
        // plain replay exits at this guard and the button waits for the
        // user's own unfocus + refocus. The user clicked the icon to OPEN
        // this window: taking the foreground is the request, not a theft.
        // Denied twice, stand down quietly (the next real refocus births
        // the button as it always has).
        const DWORD fgTid = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const DWORD myTid = GetCurrentThreadId();
        const BOOL attached = (fgTid && fgTid != myTid)
                              ? AttachThreadInput(myTid, fgTid, TRUE) : FALSE;
        SetForegroundWindow(panel);
        SetFocus(panel);
        if (attached) AttachThreadInput(myTid, fgTid, FALSE);
        if (GetForegroundWindow() != panel) return;
    }
    const HWND proxy = CreateWindowExW(WS_EX_TOOLWINDOW, L"static", L"",
                                       WS_POPUP, -32000, -32000, 1, 1,
                                       nullptr, nullptr,
                                       GetModuleHandleA(nullptr), nullptr);
    if (!proxy) return;
    ShowWindow(proxy, SW_SHOWNOACTIVATE);   // visible so it can take focus;
                                            // off-screen 1x1 toolwindow
    SetForegroundWindow(proxy);             // the unfocus half
    Sleep(40);                              // two DISTINCT foreground events
    SetForegroundWindow(panel);             // the refocus half -- the ride
    SetFocus(panel);
    DestroyWindow(proxy);
}

static void guiRestoreWindow() {
    // v7.1.6 TRAY-FIRST: the first ask BUILDS the panel (tray double-click,
    // tray menu, second instance); every later ask just shows it.
    if (!guiCreateRealWindow()) return;
    g_userShown = true;   // v7.1.5: a user asked for the window -- boot yields stop
    const bool wasHidden = !IsWindowVisible(g_hwnd);   // v7.4.10: show-from-hidden?
    ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_hwnd);
    tsEnsureTaskbarButton(g_hwnd);   // v7.4.8: the audit -- birth the button if the shell missed it
    // v7.4.10: the second activation, armed (~500 ms out) for every
    // show-from-hidden (tray click, tray menu, second-instance come-up).
    // A re-show on an already-visible window (the no-op focus refresh)
    // does not re-arm: the shell re-evaluated nothing.
    // v7.4.11: the arm is FORCED -- the tray owns the foreground after
    // the click and this process holds no grant; the plain replay would
    // exit at its guard every time (see tsReplayActivation).
    if (wasHidden) {
        g_tbCycle = 2; g_tbForce = true;
        // v7.6.13: the first-frame belt rides every show-from-hidden too
        // (same reasoning as the plain-launch show in guiMain).
        InvalidateRect(g_hwnd, nullptr, TRUE);
        UpdateWindow(g_hwnd);
    }
    // v7.1.26: no ITaskbarList::AddTab anymore -- the native button arrives
    // with the show, full-size and fully labeled (see the taskbar note above).
    guiClampToWorkArea(g_hwnd);              // v7.1.20: survive dead monitors
    // v7.4.4 THE SHADOW REBUILD. Close-to-tray hides with SW_HIDE, which
    // retires the window's DWM visual -- the drop shadow goes with it.
    // SW_SHOW resurrects the window from the frame metadata cached at the
    // last frame change WITHOUT recomputing the NC geometry, and the shadow
    // is rebuilt at the stale bounds: detached from the real rect (the
    // tray-reopen ghost the field screenshotted). The WM_CREATE nudge
    // (v7.1.24, RAD main.c) is exactly the missing recompute -- one
    // SWP_FRAMECHANGED makes win32k re-run WM_NCCALCSIZE and DWM rebuild
    // the frame + shadow against the live rect. NOACTIVATE: the
    // SetForegroundWindow above just ran; the nudge must not start a second
    // activation flip behind it. Geometry only -- the glass sheet, the
    // recipe rows and every painted byte are untouched.
    SetWindowPos(
      g_hwnd, NULL,
      0, 0, 0, 0,
      SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
      SWP_NOACTIVATE
    );
    // v7.6.12: the sheet re-commit (v7.4.5) is retired with the sheet --
    // the native frame owns the shadow, and the SWP_FRAMECHANGED nudge
    // above is all the shadow rebuild a plain window ever needs.
    SetTimer(g_hwnd, 1, 250, nullptr);       // v7.1.20: the tick slept while hidden
}

// v7.5.0: the close-to-tray gesture, extracted from WM_CLOSE so the
// tray paths share one function: position captured for the ini, the 250 ms
// tick put to sleep, the icon ensured, the window hidden. The engine keeps
// running -- real exit stays in the tray menu and the system menu's "Exit
// twinspeak" (v7.8.0: with WS_SYSMENU back on the style, that system menu
// is Alt+Space reachable again, the native way).
// v7.5.0 (field): the NOTIFICATION is NOT part of the gesture -- an
// API-driven hide is SILENT. v7.5.1: WM_CLOSE's confirmation answers the
// close AFTER this gesture returns (the balloon must not race the vanish --
// a NIF_INFO modify dispatched while the owner window is mid-SW_HIDE can be
// dropped by the shell), and the old trayAdd(true) hint is gone with the
// v7.1.20 first-three-closes cap: the icon ensure here is silent.
static void guiHideToTray(HWND w) {
    guiCaptureWindowPos(w);
    KillTimer(w, 1);
    // v7.1.26: no DeleteTab -- hiding a window removes its native
    // taskbar button by itself.
    trayAdd();       // ensure the icon: the window must stay reachable
                     // (and the rare icon-was-missing re-add) -- SILENT
    ShowWindow(w, SW_HIDE);
}

// ---- v7.0.9: the frame render buffer (THE WATERMARK-FLICKER FIX)  -----------
// The frame face is rendered into this memory surface and lands on the screen
// as ONE blit -- no erase pass, no partial updates, nothing for DWM to sample
// mid-build. (Same technique as the button buffer below.)
// v7.7.9 -> v7.8.0: the buffer stays a 32bpp TOP-DOWN DIB SECTION
// (alpha-capable, every byte addressable). The alpha storage was bought for
// the retired extend band; it stays because a DIB is also the safer surface
// class generally -- GDI+'s blend path keeps its writes and BitBlt lands
// them verbatim.
static HDC     g_frmDc = nullptr;
static HBITMAP g_frmBm = nullptr, g_frmBmOld = nullptr;
static int     g_frmW = 0, g_frmH = 0;
static void*   g_frmBits = nullptr;          // v7.7.9: the DIB's bytes
static void guiFrameBufRelease() {
    if (g_frmBmOld) { SelectObject(g_frmDc, g_frmBmOld); g_frmBmOld = nullptr; }
    if (g_frmBm)    { DeleteObject(g_frmBm);             g_frmBm    = nullptr; }
    if (g_frmDc)    { DeleteDC(g_frmDc);                 g_frmDc    = nullptr; }
    g_frmBits = nullptr;                        // v7.7.9
    g_frmW = 0; g_frmH = 0;
}
static bool guiFrameBufEnsure(int w, int h) {
    if (g_frmDc && w == g_frmW && h == g_frmH) return true;
    guiFrameBufRelease();
    HDC sdc = GetDC(nullptr);
    g_frmDc = CreateCompatibleDC(sdc);
    BITMAPV5HEADER bi = {};                     // v7.7.9: the g_stripBm recipe
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = w;
    bi.bV5Height      = -h;                     // top-down
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;           // BGRA -- alpha byte lands
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;
    g_frmBm = CreateDIBSection(sdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS,
                               &g_frmBits, nullptr, 0);
    ReleaseDC(nullptr, sdc);
    if (!g_frmDc || !g_frmBm) { guiFrameBufRelease(); return false; }
    g_frmBmOld = (HBITMAP)SelectObject(g_frmDc, g_frmBm);
    g_frmW = w; g_frmH = h;
    return true;
}
// v7.5.3a: long ini paths used to run off the version line and clip mid-glyph.
// Middle-truncate so BOTH ends stay visible (the drive and the file name).
static std::string tsMidTrunc(const std::string& s, size_t maxChars) {
    if (s.size() <= maxChars) return s;
    const size_t keep = maxChars > 3 ? (maxChars - 3) / 2 : 1;
    return s.substr(0, keep) + "..." + s.substr(s.size() - keep, keep);
}

// ---- v7.8.0 THE CUSTOM CAPTION BAND RETIRES, WHOLE -------------------------
// The field: "Remove any references of the one-file window header (leaving
// me with just the flat-style default windows 10 window)." Everything that faked a
// titlebar inside the client is gone: the band's face seat, the uDWM
// caption-button geometry (stripBtnRects/stripBtnAt), the hover/press glow
// state machine and its 16 ms timer, the hand-painted glyphs/title/icon,
// the row-0 border line, the opaque-band alpha pass, the band's NC mouse
// routing and the +kCapHDesign layout lead. The native caption draws it
// all, Windows-accurate at every scale, for free.
// ---- v7.8.0 THE DIAL -- the immersive mode rides the theme ------------------
// The field's standing law (v7.7.5): "make it so the whenever the titlebar
// color's hsl lightness is over 128 it switches to light mode and vice
// versa." The titlebar color is the live theme's background -- guic::cBg
// (the panel face and the titlebar read as one surface). The dial is the
// HSL lightness
//         L = (max(r,g,b) + min(r,g,b)) / 2     -- 0..255, NOT the luma
// and the switch is DWMWA_USE_IMMERSIVE_DARK_MODE (20, fallback 19), the
// attribute that dials the NATIVE frame's flat Windows 10 titlebar:
//     L > 128  -> light mode (0): the light titlebar, dark caption ink
//     L <= 128 -> dark mode (1): the dark titlebar, white caption ink
// applyTheme calls this on every theme click ("whenever" the color
// changes), creation lands the parked mode once the window exists, and the
// flip lands in the same breath: the SWP_FRAMECHANGED nudge (all Windows
// 11 needs) plus, since v7.8.1, the WM_NCACTIVATE caption ping -- the
// pair a minimize/restore really rides; Windows 10's DWM repaints its
// caption only on a render-state CHANGE, so the ping supplies one -- no
// theme click ever shows a stale titlebar.
static int  g_dwmDark = 1;             // the mode the native caption reads
static bool guiThemeWantsLight() {
    const COLORREF c = guic::cBg;
    const int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    return (mx + mn) / 2 > 128;        // the field's 128 dial, HSL L, 0..255
}
static void guiApplyDwmMode() {
    const int prev = g_dwmDark;
    g_dwmDark = guiThemeWantsLight() ? 0 : 1;
    if (!g_hwnd) return;               // pre-creation: parked; creation lands it
    typedef HRESULT (WINAPI *TS_DWMSA)(HWND, DWORD, LPCVOID, DWORD);
    const TS_DWMSA dwmSet = (TS_DWMSA)GetProcAddress(
        LoadLibraryA("dwmapi.dll"), "DwmSetWindowAttribute");
    if (dwmSet) {
        if (FAILED(dwmSet(g_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                          &g_dwmDark, sizeof(g_dwmDark)))) {
            dwmSet(g_hwnd, 19, &g_dwmDark, sizeof(g_dwmDark));   // 1809's id
        }
    }
    if (prev == g_dwmDark) return;     // no flip: the caption already reads right
    // pass 1: the frame-change nudge -- win32k re-runs the NC calc. On
    // Windows 11 this alone repaints the caption; on 10 it is necessary
    // but (as the field proved) not sufficient.
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
    // pass 2, v7.8.1 THE CAPTION PING. The field report: on Windows 10 the
    // theme click landed the attribute (a state change proved it) but the
    // native titlebar kept the old mode until minimize/restore. The Win10
    // DWM re-renders its caption only when the caption's RENDER STATE
    // changes -- an activation flip, a text change, a geometry change; a
    // new attribute value alone dirties nothing, and a frame change with
    // no geometry behind it does not reach the caption on 10. The state
    // change that always worked is really the WM_NCACTIVATE(FALSE) /
    // WM_NCACTIVATE(TRUE) pair crossing the caption, so ping that pair
    // directly: the opposite-state pass re-renders the caption against
    // the NEW attribute, the live-state pass lands back where the window
    // truly is. Both synchronous inside DefWindowProcW (this wndproc owns
    // no NC cases since v7.8.0); no focus, no z-order, no activation
    // state is touched, and the two paints compose as one frame -- no
    // flash. lParam 0 = draw normally (the -1 skip-paint form is exactly
    // what must NOT ride here). Hidden window (tray-mode theme flips):
    // the paints land in the redirection surface and the next show
    // composes the new mode -- still correct.
    const bool live = (GetActiveWindow() == g_hwnd ||
                       GetForegroundWindow() == g_hwnd);
    SendMessageW(g_hwnd, WM_NCACTIVATE, live ? FALSE : TRUE, 0);
    SendMessageW(g_hwnd, WM_NCACTIVATE, live ? TRUE : FALSE, 0);
}

// The content face -- background, page panel, log frame, version line,
// status, keyboard map, watermark -- into ANY dc (the memory buffer
// normally, the screen directly as the no-buffer fallback).
// v7.8.0: CONTENT ONLY. The native caption owns every pixel above the
// client; there is no band to seat, no glyph to ink, no sheet to fight.
// The paint fills the whole client in plain design space (y = 0 is the
// first row under the native titlebar) and hands the DC back unshifted --
// no viewport trick, no byte-direct pass, nothing to restore at the tail.
static void guiPaintFrame(HDC dc, int cw, int ch) {
    const RECT full = { 0, 0, cw, ch };
    FillRect(dc, &full, g_brBg);          // v7.0.9: the erase used to do this
    const RECT panel = { S(12), KPANEL_T, S(866), KPANEL_B };
    FillRect(dc, &panel, g_brPanel);
    FrameRect(dc, &panel, g_brPanel2);
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    // v9.1.5: the pane's 1px frame and the pane itself are RETIRED -- the
    // status band above is the only text surface; the hint band below keeps
    // the keyboard map, and the diagnosis log is one button away.

    char vbuf[640];
    // v8.4.3 THE HONEST WATERMARK: this corner still read "v7.8.4 | <ini>"
    // -- a hard literal the v7.9.0 banner fix missed. It prints the real
    // TWS_VERSION now, exactly like the banner and the ini header: it can
    // never go stale again.
    snprintf(vbuf, sizeof(vbuf), "v%s |  %s",
             TWS_VERSION, tsMidTrunc(g_guiIniPath, (size_t)S(89)).c_str());
    const RECT ver = { S(300), S(8), S(866), S(30) };
    guiAAString(g, dc, ver, vbuf, guic::cDim, g_fSml, 2);
    // v7.1.5 FLICKER-FREE STATUS: the engine/health line is painted HERE,
    // inside the frame buffer -- the STATIC child (hidden) only stores the
    // string. One blit, no flicker while the health line updates.
    // v9.0.2: THE THREE-LINE STATUS BAR. The stored string may carry '\n'
    // breaks -- each row paints into its own 18px slot of the 54px band
    // (row 1 the engine state, rows 2-3 the health fields), each row
    // ellipsizing at the band edge (NoWrap + ellipsis, as before). One-off
    // texts without breaks ("ini saved") simply paint row 1 and leave rows
    // 2-3 empty.
    char stb[512] = "";
    if (g_hwnd) GetDlgItemTextA(g_hwnd, IDC_STATUS, stb, (int)sizeof(stb));
    {
        const char* p = stb;
        for (int row = 0; row < 3; ++row) {
            const char* nl = strchr(p, '\n');
            std::string r(p, nl ? (size_t)(nl - p) : strlen(p));
            const RECT stc = { S(14), S(458 + 18 * row), S(866), S(458 + 18 * row + 18) };
            guiAAString(g, dc, stc, r.c_str(), guic::cAccent, g_fMono, 1);
            if (!nl) break;
            p = nl + 1;
        }
    }
    // v7.1.5: the keyboard map, tiny, in the free bottom-left half of the
    // watermark strip (the watermark owns the right half).
    const RECT kbr = { S(14), KLOG_BOTTOM, S(552), KCLI_H };
    guiAAString(g, dc, kbr,
                "tab = next ctl   ctrl+tab = page   +shift = backwards   space = on/off   arrows = sliders",
                guic::cDim, g_fSml, 1);
    guiWatermark(g);
}

// ---- the window procedure ---------------------------------------------------
// v7.1.5 MOUSE-INPUT SPACING: while a slider is mid-drag the per-tick status
// work is skipped -- the health line repaint lands on the NEXT tick instead
// of interleaving GDI+ frame renders between WM_MOUSEMOVE bursts.
static bool anySliderDragging() {
    for (std::map<HWND, Sld>::const_iterator it = g_sld.begin(); it != g_sld.end(); ++it)
        if (it->second.drag) return true;
    return false;
}
static bool guiIsToggle(int id) {
    switch (id) {
        // v8.4.0: IDC_TGL_LFEFOLD retired (the fold is built in, always on)
        // v9.5.1: IDC_TGL_OUT2FIRST retired with out2_first (integrated;
        // the id stays reserved like IDC_TGL_PINOUT before it)
        case IDC_TGL_STARTUP:   // v6.6.6: the startup pill (v8.4.2: the LAST row of page 0's right column)
        case IDC_TGL_CHIMES:    // v8.3.0: on page 0; IDC_TGL_VOLFOLLOW retired
        case IDC_TGL_EQ:
            return true;
    }
    return false;
}

static LRESULT CALLBACK guiWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    // v7.8.0: every message flows to this switch and DefWindowProcW. The
    // native caption answers its own NC messages inside the default proc:
    // drag, snap, the buttons, the sysmenu, the resize edges, Alt+F4 --
    // nothing here answers for the frame anymore.
    // v6.6.1: explorer restarted -- the tray icon vanished; re-add it. v7.1.6
    // TRAY-FIRST: re-add ALWAYS (trayRemove + trayAdd): the icon owner may be
    // the invisible tray window (no control window exists yet), and explorer
    // lost every icon anyway -- a fresh NIM_ADD is the only thing that works.
    if (g_taskbarCreated && msg == g_taskbarCreated) {
        // v7.2.12: the OWNER window re-adds, once. The broadcast reaches
        // every top-level window of the class (the tray surrogate AND the
        // panel both carry the UIPI allowance), and two windows answering
        // it deleted + re-added the icon twice per explorer restart -- a
        // needless remove/add flicker racing the retry timer. g_nid.hWnd
        // names the one true owner (preserved across re-adds now); a
        // window that never added an icon (plain launch, no tray yet)
        // correctly skips: g_nid.hWnd is still null.
        if (w == g_nid.hWnd) {
            trayRemove();
            trayAdd();
        }
        return 0;
    }
    switch (msg) {
    case WM_DPICHANGED: {
        // v7.6.12: the boring, standard path -- the native frame owns the
        // DPI story ("forget about the dpi issue entirely"). The DWM's
        // suggested rect is applied, the scale re-derives, the fonts remake
        // and the tracked children re-seat through the local ledger (its
        // S() reads the same g_dpi96 the scale just stored). No guards, no
        // deferrals, no zoomed-host special cases.
        // v9.0.1 ORDERING: the scale lands BEFORE the resize, not after --
        // the fixed window's WM_GETMINMAXINFO track reads KCLI_W/KCLI_H,
        // which guiApplyDpi() just re-derived; if the suggested rect were
        // applied first, the clamp would pin the OLD scale's pixels. The
        // DWM's suggested rect is computed for the NEW dpi, so the two
        // agree by construction and the resize is a no-op clamp.
        g_dpi96 = (UINT)HIWORD(wp);
        guiApplyDpi();
        const RECT* sug = (const RECT*)lp;
        SetWindowPos(w, nullptr, sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        guiMakeFonts();
        guiStripGpuRelease();
        tsRelayoutChildren();
        RedrawWindow(w, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0; }
    // ---- v7.8.0: NO NC OVERRIDES AT ALL ---------------------------------
    // The plain frame needs none of it: no WM_NCCALCSIZE insets (the
    // system computes the real frame), no WM_NCHITTEST bands (the caption,
    // the buttons and the corners answer DefWindowProc), no WM_NCACTIVATE
    // dance (the DWM re-renders its own active/inactive titlebar), no band
    // mouse routing (the buttons are the real native ones). Every handler
    // below the custom chrome served is retired.
    // v7.5.3c: WM_WINDOWPOSCHANGING (the size lock's second belt -- it
    // pinned every sizing pass to the designed client size + insets) stays
    // retired: v9.0.1's fixed style + the WM_GETMINMAXINFO min==max track
    // (the lock's third belt, back) refuse every user resize at the source,
    // and WM_DPICHANGED is the only size-changing path left -- it must NOT
    // be pinned (it carries the new scale's own frame).
    case WM_SYSCOMMAND: {
        // v7.6.12: the built-in SC_ values ride DefWindowProc now -- the
        // native caption buttons, Win+arrows and SC_CLOSE are the system's
        // own (SC_CLOSE lands in WM_CLOSE: hide to tray, engine keeps
        // running). IDM_TRAY_EXIT from the appended system-menu item is
        // the real quit; the custom menu id is compared against the FULL
        // wParam (the 0xFFF0 mask is only for the built-in SC_ values).
        if ((int)wp == IDM_TRAY_EXIT) {
            trayRemove();
            if (g_engState.load() != 0) guiStopEngine();
            guiCaptureWindowPos(w);
            DestroyWindow(w);
            return 0;
        }
        break; }
    case WM_DISPLAYCHANGE:
        // v7.1.20: a monitor went away / the resolution dropped -- pull the
        // window back into a work area so the bar stays reachable.
        guiClampToWorkArea(w);
        break;
    case WM_ENDSESSION:
        // v7.1.20: logoff/shutdown -- remember where the window was and give
        // the engine a chance to release its devices cleanly (the threads
        // watch g_run at every buffer switch; no blocking here, Windows
        // allows only seconds). v8.4.21: the tray icon goes WITH the
        // session -- logoff kills the process without WM_DESTROY, and
        // every such session left a stale icon behind (the field's tray
        // full of dead twinspeak icons).
        if (wp) {
            trayRemove();
            guiCaptureWindowPos(w);
            if (g_engState.load() != 0) { g_engState.store(3); twsRun = false; }
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        // v7.5.1 THE MINIMIZE FREEZE: while iconic the paint body is
        // SKIPPED -- BeginPaint/EndPaint still run so the update region
        // validates (no WM_PAINT storm when the animation ends), but
        // nothing blits.
        if (!IsIconic(w)) {
        // v7.0.9 THE WATERMARK-FLICKER FIX: the frame painted straight to the
        // screen behind an erase pass -- RDW_ERASE wiped the client blank
        // (watermark gone), GDI+ rebuilt it, and DWM sampled the blank strip
        // on every tab click / theme press. The full face is rendered into
        // the buffer and lands as ONE blit; no intermediate state exists.
        // v7.6.12: no bar re-land anymore -- the native caption owns its
        // own pixels, the client is all ours.
        RECT crc; GetClientRect(w, &crc);
        const int cw = crc.right, ch = crc.bottom;
        if (guiFrameBufEnsure(cw, ch)) {
            guiPaintFrame(g_frmDc, cw, ch);
            // v7.6.13 THE FIRST-FRAME RECEIPT: once, the log pane gets the
            // rendered buffer's own center pixel next to the theme color.
            // A future "black window" report reads straight from it: buffer
            // black + theme colored = the render failed; buffer colored +
            // screen black = the present (the DWM side) lagged -- and the
            // erase belt above makes even that state theme-colored.
            static bool s_firstFrameLogged = false;
            if (!s_firstFrameLogged) {
                s_firstFrameLogged = true;
                const COLORREF px = GetPixel(g_frmDc, cw / 2, ch / 2);
                twsLog("[twinspeak] gui           : first buffered frame -- center RGB %02x%02x%02x, theme bg RGB %02x%02x%02x\n",
                     GetRValue(px), GetGValue(px), GetBValue(px),
                     GetRValue(guic::cBg), GetGValue(guic::cBg), GetBValue(guic::cBg));
            }
            // v7.8.0: no viewport shift exists anymore (the paint fills
            // plain client space), so no origin reset is needed before the
            // blit -- the buffer's row y IS the client's row y.
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
                   ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
                   g_frmDc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        } else {
            guiPaintFrame(dc, cw, ch);   // no buffer: draw straight (old path)
        }
        }   // v7.5.1: the not-iconic paint body
        EndPaint(w, &ps);
        return 0; }
    case WM_ERASEBKGND: {
        // The class brush went stale after the first applyTheme (deleted +
        // recreated) -- that is why erase is answered here, not by the class.
        // v7.0.9: it did NOTHING: the buffered WM_PAINT covered every pixel,
        // and a fill here would only hand DWM a blank intermediate frame
        // (the watermark flicker).
        // v7.6.13 THE NEVER-BLACK BELT: the field's v7.6.12 report ("I just
        // see a black window") makes the no-erase contract too dangerous to
        // keep -- with the class brush nullptr, any repaint cycle whose
        // buffered blit fails or never runs leaves the client's bytes
        // UNDEFINED and the DWM composites the surface's birth color: black.
        // The erase now fills the update region with the THEME BACKGROUND
        // itself. An intermediate frame can only ever be the theme's own
        // flat color -- the exact first fill the buffered paint lays anyway
        // -- so v7.0.9's blank intermediate no longer exists (nothing to
        // flicker) and an unpainted client is structurally impossible.
        HDC edc = (HDC)wp;
        RECT er;
        if (edc && GetClipBox(edc, &er) != ERROR) {
            HBRUSH br = g_brBg;
            if (!br) br = CreateSolidBrush(guic::cBg);
            FillRect(edc, &er, br);
            if (br != g_brBg) DeleteObject(br);
        }
        return 1; }
    case WM_CTLCOLORBTN: {
        // v7.5.2 THE ERASE IS NOTHING (the tab-switch/deactivate flicker).
        // The field: "the 'start engine', 'apply+restart', 'save ini',
        // '120 bpm tick test', or the theme button flicker on either tab
        // menu select or deactivate." Both events sweep every button with
        // a full repaint -- guiShowPage's RDW_ALLCHILDREN | RDW_UPDATENOW,
        // WM_ACTIVATE(WA_INACTIVE)'s RDW_FRAME | RDW_INVALIDATE |
        // RDW_UPDATENOW -- and each button repaint was TWO screen writes:
        // the BUTTON CONTROL erased its whole rect with the WM_CTLCOLORBTN
        // brush, THEN WM_DRAWITEM blitted the buffered face. The DWM can
        // compose the first alone: one compose of bare background -- no
        // border, no label, no fill -- per button, per tab click, per
        // unfocus. v7.2.15 matched the erase brush to the face's own
        // background, which hid the seam for the TRANSPARENT faces but
        // never restored the border+text the erase lacks (and never
        // covered the filled START accent or the ON pills). THE CONTRACT
        // IS NOW ONE WRITE: the erase answers NULL_BRUSH -- the control's
        // erase pass paints NOTHING, the previous bytes stand untouched --
        // and the buffered face (its own background fill included, the
        // corners AA'd inside the buffer per v7.0.6's spare column) lands
        // as the ONLY write. No intermediate frame exists to compose. The
        // v6.6.7 white-ghost worry dies with the erase itself: nothing at
        // all can sit under the AA -- not white, not a mismatched dark;
        // all GDI+ happens in the buffer, never on the item DC.
        return (LRESULT)GetStockObject(NULL_BRUSH); }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        const int id = GetDlgCtrlID((HWND)lp);
        // v9.1.5: the pane's ES_READONLY branch is retired with the pane.
        SetBkMode(dc, TRANSPARENT);
        if (id == IDC_STATUS) { SetTextColor(dc, guic::cAccent); return (LRESULT)g_brBg; }
        if (id == IDC_LBL_FREQ || id == IDC_LBL_GAIN || id == IDC_LBL_Q) {
            SetTextColor(dc, guic::cAccent);
            return (LRESULT)g_brPanel;
        }
        if (id >= 600 && id <= 611) {           // section headers
            SetTextColor(dc, guic::cText);
            return (LRESULT)g_brPanel;
        }
        SetTextColor(dc, guic::cDim);
        return (LRESULT)g_brPanel; }
    case WM_CTLCOLOREDIT: {
        // v6.6.3 SCROLL-GHOST FIX: a multiline EDIT scrolls by blitting old
        // rows and repainting only the exposed line(s); with TRANSPARENT the
        // new glyphs draw over the blit's leftovers and old text bleeds
        // through. OPAQUE + a matching bk color erases beneath every row.
        HDC dc = (HDC)wp;
        SetBkMode(dc, OPAQUE);
        // v9.1.5: the pane's branch is retired with the pane.
        SetBkColor(dc, guic::cPanel2);
        SetTextColor(dc, guic::cText);
        return (LRESULT)g_brPanel2; }
    case WM_CTLCOLORLISTBOX: {
        // same reasoning as WM_CTLCOLOREDIT: dropdowns scroll too
        HDC dc = (HDC)wp;
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, guic::cPanel2);
        SetTextColor(dc, guic::cText);
        return (LRESULT)g_brPanel2; }
    case WM_DRAWITEM: {
        // v6.6.5: the whole button face is GDI+ AA; the item rect is erased
        // with the surface color first (the white-corner fix). v7.0.0:
        // resting faces are TRANSPARENT -- border + text only; fills remain
        // for pressed feedback and state.
        const DRAWITEMSTRUCT* d = (const DRAWITEMSTRUCT*)lp;
        const RECT rc = d->rcItem;
        const int id = (int)d->CtlID;
        // v7.0.6 THE 1PX CLIP FIX: the button paints into the shared buffer
        // (spare column/row, see guiBtnBufEnsure) and lands as ONE blit; no
        // buffer -> the item DC and the old clipped look.
        const int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
        HDC dc = guiBtnBufEnsure(bw, bh);
        if (!dc) dc = d->hDC;
        char txt[128];
        GetWindowTextA(d->hwndItem, txt, sizeof(txt));
        const bool pressed = (d->itemState & ODS_SELECTED) != 0;
        // v7.0.0: the bottom-bar set is EXPLICIT -- a range test once
        // swallowed IDC_REFRESH and erased it with the wrong surface.
        // v7.0.4: the tab BUTTONs are retired; their ids remain here only as
        // dead-code insurance.
        const bool onPage = !((id >= IDC_TAB0 && id <= IDC_TAB0 + 2) ||
                              id == IDC_START  || id == IDC_APPLY ||
                              id == IDC_SAVE   || id == IDC_TICK  ||
                              id == IDC_THEME  || id == IDC_SAVLOG);
        // v6.6.7: erase FIRST in PLAIN GDI -- a GDI+ fill let the square
        // corners blend against the white WM_CTLCOLORBTN pass underneath,
        // leaving bright ghost arcs on every rounded corner.
        FillRect(dc, &rc, onPage ? g_brPanel : g_brBg);
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        const Gdiplus::Rect grc(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
        if (guiIsToggle(id)) {
            // v7.1.15 THE PILL CONTRAST FIX: the ON face is the theme's
            // ACCENT with the on-text color (white on every theme) -- the
            // old dark cAccentDk fill + cAccent text read as one muddy
            // blob on the dark themes. OFF stays the transparent face:
            // border + full text color.
            const bool on = g_tog[id];
            Gdiplus::GraphicsPath* p = guiRoundPath(grc, 10);
            Gdiplus::Pen        pb(GCol(on ? guic::cAccent : guic::cLine), 1.0f);
            if (on) {
                Gdiplus::SolidBrush fb(GCol(pressed ? guic::cAccentDk : guic::cAccent));
                g.FillPath(&fb, p);
            }
            g.DrawPath(&pb, p);
            delete p;
            guiAAString(g, dc, rc, on ? "ON" : "OFF", on ? guic::cOnText : guic::cText,
                        g_fUIb, 0);
            if (d->itemState & ODS_FOCUS) {   // v7.1.4: the keyboard focus cue
                Gdiplus::Pen fp(GCol(guic::cAccent), 1.0f);
                fp.SetDashStyle(Gdiplus::DashStyleDash);
                g.DrawRectangle(&fp, Gdiplus::Rect(2, 2, bw - 4, bh - 4));
            }
            if (dc != d->hDC)
                BitBlt(d->hDC, rc.left, rc.top, bw, bh, dc, 0, 0, SRCCOPY);
            return TRUE;
        }
        if (id == IDC_START) {
            const int st = g_engState.load();
            Gdiplus::GraphicsPath* p = guiRoundPath(grc, 8);
            if (st == 0) {
                Gdiplus::SolidBrush fb(GCol(pressed ? guic::cAccentDk : guic::cAccent));
                Gdiplus::Pen        pb(GCol(guic::cAccent), 1.0f);
                g.FillPath(&fb, p);
                g.DrawPath(&pb, p);
                delete p;
                guiAAString(g, dc, rc, "START ENGINE", guic::cOnText, g_fUIb, 0);
            } else if (st == 2) {
                // v7.0.0 polish: transparent face -- the red border speaks.
                Gdiplus::Pen        pb(GCol(guic::cStop), 1.0f);
                g.DrawPath(&pb, p);
                delete p;
                guiAAString(g, dc, rc, "STOP ENGINE", guic::cStop, g_fUIb, 0);
            } else {
                Gdiplus::Pen        pb(GCol(guic::cLine), 1.0f);
                g.DrawPath(&pb, p);
                delete p;
                // v6.6.9: the transient says what the CLICK does -- starting
                // the engine is already stoppable, so the label reads STOP.
                guiAAString(g, dc, rc, st == 1 ? "STOP" : "STOPPING...", guic::cDim,
                            g_fUIb, 0);
            }
            if (d->itemState & ODS_FOCUS) {   // v7.1.4: the keyboard focus cue
                Gdiplus::Pen fp(GCol(guic::cAccent), 1.0f);
                fp.SetDashStyle(Gdiplus::DashStyleDash);
                g.DrawRectangle(&fp, Gdiplus::Rect(2, 2, bw - 4, bh - 4));
            }
            if (dc != d->hDC)
                BitBlt(d->hDC, rc.left, rc.top, bw, bh, dc, 0, 0, SRCCOPY);
            return TRUE;
        }
        bool accentBtn = false;
        for (int i = 0; i < 5; ++i)
            if (id == IDC_SLOT1 + i) accentBtn = (g_eqSel == i);
        // v7.0.0 polish: resting faces are TRANSPARENT -- border + text over
        // the erased surface. Fills survive only where they mean something:
        // the pressed feedback (cPanelHi) and the active EQ preset (cPanel2
        // under the 2 px accent ring).
        // v7.1.0 THE RING SHAVE: the active preset's 2 px ring rode the
        // inset-1 path, so its outer half ran past the item edge and the
        // control clipped it -- a flat shaved spot on all four edges (the
        // "selectors still clip" report; v7.0.6's fix covered 1 px strokes
        // only). The ring now draws on its own inset-2 path: the stroke
        // spans 1..3 px inside the face, its AA tail fully owned.
        Gdiplus::GraphicsPath* p = guiRoundPathInset(grc, accentBtn ? 11 : 6,
                                                     accentBtn ? 2 : 1);
        if (pressed || accentBtn) {
            Gdiplus::SolidBrush fb(GCol(pressed ? guic::cPanelHi : guic::cPanel2));
            g.FillPath(&fb, p);
        }
        Gdiplus::Pen        pb(GCol(accentBtn ? guic::cAccent :
                                    (pressed ? guic::cPanelHi : guic::cLine)),
                               accentBtn ? 2.0f : 1.0f);
        g.DrawPath(&pb, p);
        delete p;
        guiAAString(g, dc, rc, txt, accentBtn ? guic::cAccent : guic::cText, g_fUIb, 0);
        if (d->itemState & ODS_FOCUS) {   // v7.1.4: the keyboard focus cue
            Gdiplus::Pen fp(GCol(guic::cAccent), 1.0f);
            fp.SetDashStyle(Gdiplus::DashStyleDash);
            g.DrawRectangle(&fp, Gdiplus::Rect(2, 2, bw - 4, bh - 4));
        }
        if (dc != d->hDC)
            BitBlt(d->hDC, rc.left, rc.top, bw, bh, dc, 0, 0, SRCCOPY);
        return TRUE; }
    case WM_COMMAND: {
        const int id = LOWORD(wp), code = HIWORD(wp);
        if (code != BN_CLICKED) break;
        if (guiIsToggle(id)) {
            g_tog[id] = !g_tog[id];
            guiRepaint(w, id);          // v6.6.1: the pill repaints itself
            if (id == IDC_TGL_STARTUP) {         // v6.6.6: the pill IS the Run key
                startupSet(g_tog[id]);
                guiSetStatus(w, g_tog[id]
                    ? "startup: twinspeak will run at sign-in (hidden in the tray)"
                    : "startup entry removed");
            }
            return 0;
        }
        if (id >= IDC_TAB0 && id <= IDC_TAB0 + 2) { guiShowPage(w, id - IDC_TAB0); return 0; }
        switch (id) {
            case IDM_TRAY_OPEN:                 // v6.6.1 tray menu
                guiRestoreWindow();
                return 0;
            case IDM_TRAY_START:
                if (g_engState.load() == 0) guiStartEngine(w); else guiStopEngine();
                return 0;
            case IDM_TRAY_AUTORUN: {
                const bool on = !startupEnabled();
                startupSet(on);
                g_tog[IDC_TGL_STARTUP] = on;     // v6.6.6: keep the pill in sync
                guiRepaint(w, IDC_TGL_STARTUP);
                guiSetStatus(w, on
                    ? "startup: twinspeak will run at sign-in (hidden in the tray)"
                    : "startup entry removed");
                return 0; }
            case IDM_TRAY_EXIT:
                trayRemove();
                if (g_engState.load() != 0) guiStopEngine();
                guiCaptureWindowPos(w);          // v7.1.20: remember the last spot
                DestroyWindow(w);
                return 0;
            case IDC_START:
                if (g_engState.load() == 0) guiStartEngine(w); else guiStopEngine();
                return 0;
            case IDC_SAVE: {
                cfgFromControls(w); cfgSanitize();
                guiSetStatus(w, guiSaveIni() ? "ini saved" : "ini save FAILED");
                return 0; }
            case IDC_SAVLOG: {                 // v9.1.5: the diagnosis log -> a generic save-file dialog
                char path[MAX_PATH];
                {
                    const size_t ds = g_guiIniPath.find_last_of("\\/");
                    snprintf(path, MAX_PATH, "%s\\twinspeak-diagnosis.log",
                             ds == std::string::npos ? "." : g_guiIniPath.substr(0, ds).c_str());
                }
                OPENFILENAMEA of;
                memset(&of, 0, sizeof(of));
                of.lStructSize = sizeof(of);
                of.hwndOwner   = w;
                of.lpstrFilter = "Log files (*.log)\0*.log\0All files (*.*)\0*.*\0";
                of.lpstrFile   = path;
                of.nMaxFile    = MAX_PATH;
                of.lpstrDefExt = "log";
                of.lpstrTitle  = "Download log for diagnosis";
                of.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
                if (GetSaveFileNameA(&of)) {
                    // the ring IS the log -- snapshot it under its lock and
                    // write CRLF-normalized under a small header
                    EnterCriticalSection(&g_logCS);
                    const std::string snap = g_logRing;
                    std::string hl = g_lastHealth;
                    LeaveCriticalSection(&g_logCS);
                    for (size_t i = 0; i < hl.size(); ++i)
                        if (hl[i] == '\n') hl[i] = ' ';
                    SYSTEMTIME st;
                    GetLocalTime(&st);
                    bool ok = false;
                    FILE* f = fopen(path, "w");
                    if (f) {
                        fprintf(f, "; twinspeak %s diagnosis log -- %04u-%02u-%02u %02u:%02u:%02u\r\n",
                                TWS_VERSION, (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                                (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
                        fprintf(f, "; last health line: %s\r\n", hl.c_str());
                        for (size_t i = 0; i < snap.size(); ++i) {
                            if (snap[i] == '\r') continue;
                            if (snap[i] == '\n') fputs("\r\n", f);
                            else fputc(snap[i], f);
                        }
                        fclose(f);
                        ok = true;
                    }
                    guiSetStatus(w, ok ? "diagnosis log saved" : "diagnosis log save FAILED");
                }
                return 0; }
            case IDC_TICK: {                   // v9.5.9: the 120 bpm tick -- an EXTERNAL 8ch WAV.
                if (g_engState.load() == 0) {
                    guiSetStatus(w, "tick test: start the engine first");
                    return 0;
                }
                // The path sits beside the INI (the same dir the engine's
                // own files live in). If the WAV already exists it is
                // OPENED, not regenerated (field-directed: "When the tick
                // test has already been generated, just open it"); if it
                // does not, it is generated once and opened. ShellExecuteA
                // hands it to the associated player -- one graph, one
                // shared-mode mixer, the SAME latency class on every
                // device in it. No STOP state anymore: the player owns
                // playback (close it there); the button is a pure PLAY.
                char iniDir[MAX_PATH];
                {
                    const size_t ds = g_guiIniPath.find_last_of("\\/");
                    snprintf(iniDir, MAX_PATH, "%s",
                             ds == std::string::npos ? "." : g_guiIniPath.substr(0, ds).c_str());
                }
                const std::string wav = twsTickWavPath(iniDir);
                if (GetFileAttributesA(wav.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    if (!twsTickWavWrite(wav)) {
                        guiSetStatus(w, "tick test: cannot write twinspeak-tick.wav");
                        twsLog("[twinspeak] tick test  : CANNOT write \"%s\" -- not opened\n",
                             wav.c_str());
                        return 0;
                    }
                    twsLog("[twinspeak] tick test  : generated \"%s\" (48 kHz, 8ch, 16-bit PCM,\n"
                         "                            120 bpm, 8 beats, FL/FR/FC/LFE) -- opening\n",
                         wav.c_str());
                } else {
                    twsLog("[twinspeak] tick test  : \"%s\" already generated -- opening\n",
                         wav.c_str());
                }
                ShellExecuteA(w, "open", wav.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                guiSetStatus(w, "tick test: 8ch WAV opened -- one graph, consistent latency");
                return 0; }
            case IDC_THEME: {                  // v7.0.0: cycle the eleven themes
                applyTheme(g_theme + 1);
                // v7.0.4: the theme persists across sessions -- save it (and
                // everything currently on the controls, exactly like SAVE INI
                // would) the moment it changes; no restart, no leak.
                cfgFromControls(w); cfgSanitize();   // same save model as SAVE INI
                const bool thOk = guiSaveIni();
                guiSetStatus(w, thOk ?
                    (std::string("theme saved: ") + kThemes[g_theme].name).c_str()
                    : kThemes[g_theme].name);
                return 0; }
            case IDC_APPLY: {
                const bool wasRunning = g_engState.load() != 0;
                cfgFromControls(w); cfgSanitize();
                const bool ok = guiSaveIni();
                if (wasRunning) {
                    // v7.1.20: the restart CHAINS off WM_APP+2 (the engine
                    // thread's farewell) -- no frozen window between the two
                    // halves, and the new engine only starts on the old one's
                    // corpse.
                    g_applyRestart = true;
                    guiStopEngine();
                    guiSetStatus(w, ok
                        ? "saved -- restarting the engine..."
                        : "save FAILED -- restarting with the previous ini");
                } else {
                    guiSetStatus(w, ok ? "saved -- START to apply" : "ini save FAILED");
                }
                return 0; }
            case IDC_REFRESH:   guiFillCombos(w);     return 0;
            case IDC_EQSAVE: {                   // v7.1.9: save the ARMED slot
                if (g_eqSel >= 0 && g_eqSel < 5) guiSaveSlot(w, g_eqSel);
                else guiSetStatus(w,
                     "no slot armed -- click a slot first, then SAVE TO SLOT");
                return 0; }
            case IDC_SLOT1: case IDC_SLOT2: case IDC_SLOT3:
            case IDC_SLOT4: case IDC_SLOT5:           // v7.1.8: apply the slot
                guiApplySlot(w, id - IDC_SLOT1);      return 0;
        }
        break; }
    case WM_HSCROLL: {
        const int id = GetDlgCtrlID((HWND)lp);
        if (id == IDC_SLD_FREQ || id == IDC_SLD_GAIN || id == IDC_SLD_Q) {
            guiUpdateEqLabels(w);                  // v7.1.10: the selected slot STAYS
            guiRepaintSlots(w);                    // selected while you tweak -- it is
        }                                          // the SAVE TO SLOT target
        return 0; }
    case WM_CONTEXTMENU: {
        // v7.1.8: right-click an EQ save slot = SAVE the current sliders +
        // the EQ pill into it (left-click applies). wParam names the child
        // the click landed on; anything else falls through to DefWindowProc.
        const int id = GetDlgCtrlID((HWND)wp);
        if (id >= IDC_SLOT1 && id <= IDC_SLOT5) {
            guiSaveSlot(w, id - IDC_SLOT1);
            return 0;
        }
        break; }
    case WM_GETMINMAXINFO: {
        // v9.0.1: THE SIZE LOCK RETURNS (field-directed: "make the window
        // unresizable"). v7.5.3c stood the min==max track down for the resize
        // test; the test is over and the lock is back, pinned to the designed
        // client size at the CURRENT scale (g_dpi96 re-derives in
        // WM_DPICHANGED, which resizes the window itself before any tracking
        // can see the stale scale). With WS_THICKFRAME/WS_MAXIMIZEBOX off the
        // style already refuses every user resize; this is the belt under
        // that suspend: drag-the-caption tricks, Alt+Enter-ish shell paths,
        // anything else that would try to size the window to something else.
        // The hidden tray surrogate (WS_OVERLAPPED, no caption) is EXEMPT --
        // it must stay 0x0, and the create-time WM_GETMINMAXINFO it also
        // receives would otherwise clamp it up to the panel's full size.
        if (!(GetWindowLongPtrW(w, GWL_STYLE) & WS_CAPTION)) break;
        RECT rc = { 0, 0, KCLI_W, KCLI_H };
        AdjustWindowRectEx(&rc, (DWORD)GetWindowLongPtrW(w, GWL_STYLE), FALSE, 0);
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMaxTrackSize.x = mmi->ptMinTrackSize.x = rc.right - rc.left;
        mmi->ptMaxTrackSize.y = mmi->ptMinTrackSize.y = rc.bottom - rc.top;
        return 0; }
    case WM_TIMER: {
        // v7.4.10: the second activation -- the unfocus + refocus replay
        // (tsReplayActivation above), armed two ticks (~500 ms) after the
        // first show, spent here once. Only fires while the panel owns
        // the foreground; the guard inside skips everything otherwise.
        // v7.4.11: the forced arm (show-from-hidden) takes the foreground
        // first instead of skipping -- see tsReplayActivation.
        if (g_tbCycle > 0 && --g_tbCycle == 0) {
            tsReplayActivation(w, g_tbForce);
            g_tbForce = false;
        }
        // v7.4.9: the taskbar nag -- the name-change re-announce budget set
        // by tsEnsureTaskbarButton, spent one per tick while the panel is
        // visible (covers the sign-in race: a taskbar that was still
        // starting at the first show hears the re-announce the moment it
        // listens). Announce-only: no focus stealing, no style churn.
        if (g_tbNag > 0) {
            --g_tbNag;
            if (IsWindowVisible(w)) tsAnnounceToTaskbar(w);
        }
        // v7.2.15: the wp == 4 fade-tracking tick is RETIRED -- the train is
        // the presenter thread now (see the old sheet notes), because
        // WM_TIMER ticks starved behind queue traffic and froze the DWM's
        // presentation-time sheet blend mid-fade. Timer 4 no longer exists
        // (v7.7.5 re-enlisted it for the caption glow; v7.8.0 retired it
        // again with the custom buttons).
        // v7.1.20: timer 3 = the tray-icon retry (a failed NIM_ADD at sign-in
        // re-tries every 2 s until the shell accepts it).
        if (wp == 3) {
            if (!g_trayVisible) trayAdd();
            if (g_trayVisible) KillTimer(w, 3);
            return 0;
        }
        // v7.0.3: timer 2 drives the tab fade-in; ONLY the strip repaints
        // each tick. When the run completes the timer kills itself and lands
        // one final full-strength pass -- no fractional-alpha frame remains.
        if (wp == 2) {
            HWND ts = GetDlgItem(w, IDC_TABSTRIP);
            if (g_tabAnimStart &&
                GetTickCount64() - g_tabAnimStart < (ULONGLONG)KTAB_FADE_MS) {
                if (ts) InvalidateRect(ts, nullptr, FALSE);
            } else {
                KillTimer(w, 2);
                if (g_fadePeriod) { timeEndPeriod(1); g_fadePeriod = false; }  // v7.0.5
                g_tabAnimStart = 0;
                if (ts) InvalidateRect(ts, nullptr, TRUE);
            }
            return 0;
        }
        // v9.5.9: the tick auto-stop is RETIRED -- the external WAV's
        // player owns start AND end now; there is no in-engine run to
        // stop, no button label to restore, no stale endMs to clear.
        // v7.1.20: the async stop's backup reap -- if WM_APP+2's bounded wait
        // expired (hidden window, timer asleep), the next tick closes the
        // handle. The thread itself already set state 0.
        if (g_engThread && WaitForSingleObject(g_engThread, 0) == WAIT_OBJECT_0) {
            CloseHandle(g_engThread);
            g_engThread = nullptr;
            guiRepaint(w, IDC_START);
        }
        static const char* stName[4] = { "engine stopped", "engine started",
                                         "engine RUNNING", "engine stopping..." };
        // v7.0.0: "engine starting..." renamed "engine started" -- the label
        // says the start process has finished, not that it is grinding.
        std::string s = stName[g_engState.load()];
        if (g_engState.load() == 0 && g_engRC.load() != 0) {
            char eb[32];
            snprintf(eb, sizeof(eb), " (exit code %d)", g_engRC.load());
            s += eb;
        }
        // v7.2.12: read the health line under the same lock the engine
        // thread writes it with (guiLogSink) -- no torn std::string reads.
        EnterCriticalSection(&g_logCS);
        const std::string health = g_lastHealth;
        LeaveCriticalSection(&g_logCS);
        // v9.0.2: THE THREE-LINE STATUS BAR. The health line stays ONE line
        // in storage (the sink's "raw1 " contract is untouched); HERE it
        // splits on its own " | " separators into rows 2-3, four fields
        // each (buffers/clocks then faults) -- the old single row appended
        // all ten fields and clipped past ~106 mono chars.
        if (!health.empty()) {
            std::string r2, r3;
            size_t pos = 0;
            int fi = 0;
            while (pos <= health.size() && fi < 8) {
                size_t sep = health.find(" | ", pos);
                const size_t end = (sep == std::string::npos) ? health.size() : sep;
                const std::string fld = health.substr(pos, end - pos);
                if (fi < 4) { if (fi) r2 += " | "; r2 += fld; }
                else        { if (fi > 4) r3 += " | "; r3 += fld; }
                ++fi;
                if (sep == std::string::npos) break;
                pos = sep + 3;
            }
            s += "\n"; s += r2;
            s += "\n"; s += r3;
        }
        // v7.0.7: WM_SETTEXT repaints a STATIC even when the text is
        // identical -- 4 idle repaints a second for nothing. Set only on
        // change; the task-manager CPU line stays at the engine's floor.
        static std::string s_lastStatus;
        if (s != s_lastStatus && !anySliderDragging()) {   // v7.1.5: no paint storm mid-drag
            s_lastStatus = s;
            guiSetStatus(w, s.c_str());
        }
        return 0; }
    case WM_APP + 2: {                                 // engine thread ended
        // v7.1.20: reap the thread NOW -- the poster is microseconds from
        // exiting, so the bounded wait almost never actually waits (and a
        // leftover handle is closed by the timer's reap instead).
        if (g_engThread) {
            if (WaitForSingleObject(g_engThread, 250) == WAIT_OBJECT_0) {
                CloseHandle(g_engThread);
                g_engThread = nullptr;
            }
        }
        guiRepaint(w, IDC_START);                      // v6.6.1
        // v7.1.20: the chained APPLY+RESTART -- the restart fires only after
        // the old engine is truly gone, with no UI-thread freeze anywhere.
        if (g_applyRestart) {
            g_applyRestart = false;
            if (g_hwnd && IsWindow(g_hwnd)) guiStartEngine(g_hwnd);
        }
        return 0; }
    case WM_APP + 3: {                                 // v6.6.1: tray icon events
        // v7.1.20: version-0 notifications deliver WM_RBUTTONUP (never
        // WM_CONTEXTMENU -- that lParam only exists with
        // NOTIFYICON_VERSION_4), so the dead check is gone. A single
        // left-click opens (as the balloon now says).
        if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) {
            guiRestoreWindow();
        } else if (lp == WM_RBUTTONUP) {
            // SetForegroundWindow before TrackPopupMenu + the WM_NULL after:
            // without them the menu refuses to dismiss when the user clicks
            // elsewhere.
            SetForegroundWindow(g_hwnd);
            HMENU m = CreatePopupMenu();
            AppendMenuA(m, MF_STRING, IDM_TRAY_OPEN, "Open twinspeak");
            AppendMenuA(m, MF_STRING, IDM_TRAY_START,
                        g_engState.load() == 0 ? "Start engine" : "Stop engine");
            AppendMenuA(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(m, MF_STRING | (startupEnabled() ? MF_CHECKED : MF_UNCHECKED),
                        IDM_TRAY_AUTORUN, "Run at Windows startup");
            AppendMenuA(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(m, MF_STRING, IDM_TRAY_EXIT,
                        g_engState.load() != 0 ? "Exit (stops the engine)" : "Exit");
            POINT p; GetCursorPos(&p);
            TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, g_hwnd, nullptr);
            DestroyMenu(m);
            PostMessageA(g_hwnd, WM_NULL, 0, 0);
        }
        return 0; }
    case WM_APP + 4:                                   // v6.6.1: second instance
        // v7.1.5 boot-pop guard (belt to the duplicate-side braces): a hidden
        // startup instance inside the logon window ignores "come up" -- the
        // sender is boot machinery that slipped past every other gate. A user
        // double-click outside the logon window (or after ever opening the
        // window) still comes up, as always.
        if (g_startTray && !g_userShown && withinLogonWindow(kBootWindowMs))
            return 0;
        guiRestoreWindow();                            // says "come up"
        return 0;
    case WM_APP + 5: {                                 // v7.1.5: boot yield
        // A --tray duplicate lost the mutex race to a PLAIN launch (Windows'
        // app-restart / a Startup-folder shortcut won the race and showed its
        // window). The --tray launch is BY DEFINITION the startup path: hand
        // the plain twin its true role -- go hidden, own the tray icon, run
        // the startup engine. Never touches a window the user opened.
        if (!g_userShown) {
            if (twsCfg.startupEngine && g_engState.load() == 0)
                guiStartEngine(w);
            trayAdd();
            ShowWindow(w, SW_HIDE);
        }
        return 0; }
    case WM_APP + 6: {                                 // v7.2.1: --exit clean stop
        // `twinspeak.exe --exit` (and the duplicate box's advice) asks the
        // running instance to quit the way the tray Exit does: icon removed
        // (g_nid remembers the true owner -- panel or surrogate), engine
        // stopped, position captured, WM_DESTROY ends the loop. Reaching
        // here from an elevated caller works because tsAllowUiMessages
        // opened WM_APP+6 on every window of this class. DestroyWindow
        // targets the real panel (or the surrogate when no panel was ever
        // built); whichever window received the message dies with it.
        trayRemove();
        if (g_engState.load() != 0) guiStopEngine();
        guiCaptureWindowPos(g_hwnd);
        DestroyWindow(g_hwnd ? g_hwnd : w);
        return 0; }
    case WM_CLOSE:
        // v6.6.1: closing hides to the tray -- the engine keeps running.
        // Real exit lives in the tray menu (Exit) and the system menu's
        // "Exit twinspeak" (v7.1.20).
        // v7.1.20: capture the position for the ini, and let the 250 ms
        // tick sleep while hidden (it returns in guiRestoreWindow). The
        // hidden tray surrogate ignores WM_CLOSE entirely (taskkill
        // broadcasts reach it too).
        // v7.5.0: the hide itself is guiHideToTray -- one shared SILENT
        // gesture for every tray path.
        // v7.5.1 (field): the balloon NEVER SHOWED, two reasons, both gone.
        // First, the order: the v7.2.12 dispatch fired the NIF_INFO modify
        // and THEN hid the window inside the same message -- a notification
        // racing its owner window's vanish, which the shell is free to
        // drop. The confirmation now lands AFTER guiHideToTray returns:
        // icon up, window settled hidden, then one NIM_MODIFY against the
        // icon (the balloon belongs to the icon, not the window).
        // Second, the cap: "the first three closes per session" ate every
        // close after the third and read as broken. The one-liner the
        // field chose ("TwinSpeak is now in system tray.") says exactly
        // where the window went and nothing more -- it can fire on every
        // close without nagging. The old two-line "Still running -- the
        // engine stays up..." text and the g_closeHints counter are gone.
        // An API-driven hide stays silent: this answer belongs to a USER's
        // close, never a programmatic one.
        if (w == g_trayWnd) return 0;
        guiHideToTray(w);
        trayBalloon("TwinSpeak is now in system tray.");
        return 0;
    case WM_DESTROY:
        trayRemove();
        PostQuitMessage(0);
        return 0;
    }
    // v7.1.26: DefWindowProcW, not DefWindowProcA -- the class is Unicode,
    // and the A default proc put every shell title read through an A/W
    // round trip (an ANSI read of the UTF-16 title returns "T" and stops).
    return DefWindowProcW(w, msg, wp, lp);
}

// ---- entry ------------------------------------------------------------------
// v6.6.1: startTray -- --tray launches the window already hidden in the tray
// (the "Run at Windows startup" entry uses it).
static int guiMain(const std::string& iniPath, bool startTray) {
    g_guiIniPath = iniPath;
    g_startTray  = startTray;
    twsLogSink = guiLogSink;                            // everything logs to the pane now
    // v7.1.20: the exe is GUI-subsystem now (no loader console flash) -- if
    // we were started from a terminal, detach politely; there is nothing to
    // free otherwise.
    FreeConsole();
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    HANDLE mutex = CreateMutexA(nullptr, TRUE, "Local\\twinspeak.instance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        // v7.1.5 THE BOOT POP: a duplicate --tray launch is the STARTUP path
        // -- it must never force the window up. The field log: the Run key
        // starts twinspeak hidden at sign-in, then Windows' startup machinery
        // re-runs the exe once the taskbar is up; the duplicate landed HERE,
        // signaled WM_APP+4 and the hidden window popped open. A --tray
        // duplicate now exits silently -- but if the winner shows a window
        // nobody asked for (a plain boot twin beat us to the mutex), hand it
        // the boot yield first: WM_APP+5 hides it, owns the tray icon and
        // runs the startup engine. Then exit silently.
        if (startTray) {
            for (int i = 0; i < 20; ++i) {
                if (HWND other = FindWindowA("twinspeak.control", nullptr)) {
                    if (IsWindowVisible(other)) {
                        DWORD_PTR res = 0;
                        SendMessageTimeoutA(other, WM_APP + 5, 0, 0,
                                            SMTO_ABORTIFHUNG, 4000, &res);
                    }
                    break;
                }
                Sleep(50);
            }
            CloseHandle(mutex);
            return 0;
        }
        // v7.1.5 boot pop, round two: a PLAIN duplicate can be boot machinery
        // too (Windows' app-restart, a Startup-folder shortcut) -- it carries
        // no --tray, so the gate above misses it. The tell is the SHELL's age:
        // right after logon, a plain duplicate finding an already-running
        // HIDDEN instance is not a user asking for the window -- it exits
        // silently. Outside the logon window (a real user double-click) it
        // still says "come up", as always.
        for (int i = 0; i < 20; ++i) {
            if (HWND other = FindWindowA("twinspeak.control", nullptr)) {
                if (withinLogonWindow(kBootWindowMs) && !IsWindowVisible(other)) {
                    CloseHandle(mutex);
                    return 0;                       // boot twin: silent exit
                }
                // v7.4.9 THE FOREGROUND HANDOFF: this duplicate is the
                // process the user just launched -- the double-click handed
                // IT the right to take the foreground, and that right dies
                // with it when it exits below. The instance that actually
                // shows the window is the background --tray twin: its own
                // SetForegroundWindow (guiRestoreWindow) would be denied,
                // the panel would come up unactivated, and the shell would
                // defer its taskbar button to the first focus click (the
                // "no tab until I unfocus and refocus" report).
                // AllowSetForegroundWindow moves the grant across BEFORE
                // the ask, so the surviving instance consumes it inside
                // this same synchronous round trip. (Resolved dynamically
                // -- the house trick -- so no header surface moves.)
                {
                    typedef BOOL (WINAPI *TS_ASFW)(DWORD);
                    HMODULE u32 = GetModuleHandleA("user32.dll");
                    if (!u32) u32 = LoadLibraryA("user32.dll");
                    const TS_ASFW allowFg =
                        u32 ? (TS_ASFW)(void*)GetProcAddress(
                                  u32, "AllowSetForegroundWindow")
                            : nullptr;
                    DWORD otherPid = 0;
                    GetWindowThreadProcessId(other, &otherPid);
                    if (allowFg && otherPid) allowFg(otherPid);
                }
                DWORD_PTR res = 0;
                if (SendMessageTimeoutA(other, WM_APP + 4, 0, 0,
                                        SMTO_ABORTIFHUNG, 4000, &res)) {
                    CloseHandle(mutex);
                    return 0;
                }
                break;
            }
            Sleep(50);
        }
        // v7.2.1: the advice is a CLEAN stop now -- taskkill's /F form
        // never runs WM_DESTROY, so the tray icon orphans and sits in the
        // tray until hovered (the "duplicating icons" the field saw).
        // --exit (WM_APP+6, UIPI-allowed below) removes the icon properly.
        MessageBoxA(nullptr,
                    "TidyVision TwinSpeak is already running.\n\n"
                    "Bring it up:  click its tray icon (or launch it again).\n\n"
                    "Stop it cleanly (removes the tray icon):\n"
                    "  right-click the tray icon -> Exit\n"
                    "  twinspeak.exe --exit\n\n"
                    "Avoid taskkill /F -- it leaves a stale tray icon behind.",
                    "TidyVision TwinSpeak", MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 1;
    }
    // v7.1.23 was: THE TASKBAR LABEL -- the process AppUserModelID claim
    // ("the window had no AUMID, so Win11's labeled taskbar collapsed the
    // button to a one-letter stub"). RETIRED v7.4.10, the plain process.
    // v7.4.9 retired the per-window half of this and called the panel a
    // vanilla window -- but kept this process-wide claim, so every panel
    // window was STILL born into the AUMID group's lifecycle instead of
    // the plain window path, with the windowless sign-in --tray twin as
    // the group's claimant and the come-up panel joining late. That
    // group is the exact thing the field machine fails to birth a button
    // in. Every job the claim still held runs without it: grouping (one
    // window -- nothing to group), the jumplist (the exe default), the
    // balloon identity (the tray icon's own) and the label (the window
    // title, "TidyVision TwinSpeak", as v7.4.9 already rides). And the
    // claim's original target -- the one-letter stub -- died with
    // v7.1.26's end-to-end Unicode conversion, v7.4.9's own finding.
    // The process is as vanilla as the window now: no AUMID anywhere,
    // no group anywhere -- and "hidden from the taskbar" happens only
    // on --tray, where v7.1.6 builds no window at all.
    if (!loadIni(iniPath.c_str())) writeTemplate(iniPath.c_str());   // show defaults if absent
    guiInitDpi();          // v7.1.20: FIRST geometry act -- scale every constant + font
                           // v7.6.12: the old SECOND act (guiCalcFrameInsets,
                           // the WM_NCCALCSIZE inset contract) is retired --
                           // AdjustWindowRectEx answers the native frame's
                           // creation size now.
    // v7.1.5 boot pop, round two (winner side): a PLAIN launch that beats the
    // Run-key --tray instance to the mutex shows its window and lets the real
    // startup twin die silently -- the boot pop, from the other direction.
    // Inside the logon window, with startup_engine opted in, a plain launch
    // IS the startup path: run it exactly like --tray (tray-only, engine on,
    // no window built at all).
    if (!startTray && twsCfg.startupEngine && withinLogonWindow(kBootWindowMs)) {
        twsLog("[twinspeak] gui           : plain launch inside the logon window -- running it as the\n");
        twsLog("                            startup instance (tray-only, startup engine)\n");
        startTray    = true;
        g_startTray  = true;
    }
    guiMakeGdi();
    // v7.1.7: ITaskbarList (the result only matters if COM changed modes under
    // us -- the taskbar slot then just falls back to shell timing).
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        twsLog("[twinspeak] gui           : COM init failed -- the taskbar slot falls back to shell timing\n");
    // v6.6.5: GDI+ session for the antialiased owner-drawn UI + watermark
    {
        Gdiplus::GdiplusStartupInput sin_;
        Gdiplus::GdiplusStartup(&g_gdipToken, &sin_, nullptr);
    }
    g_taskbarCreated = RegisterWindowMessageA("TaskbarCreated");   // v6.6.1
    // v7.1.20: WNDCLASSEXW (a true small icon -- hIconSm -- for the taskbar /
    // Alt-Tab instead of a scaled 32 px). v7.1.23 had CS_DROPSHADOW ride
    // along as a composition-off fallback shadow. v7.4.6 RETIRES it for
    // good: the legacy shadow is win32k's own "SysShadow" sprite, drawn
    // OFFSET DOWN-RIGHT of the owner window, and its show/hide lifecycle is
    // exactly what the tray-reopen ghost was -- a hide-to-tray + reopen
    // cycle left the sprite stale at the window's bounds (the v7.4.4 frame
    // nudge re-anchored it, the v7.4.5 sheet re-commit could not touch it:
    // neither one owns the sprite). No CS_DROPSHADOW -> no SysShadow window
    // can exist -> no ghost can. The Windows-default drop shadow the field
    // asked for is untouched: it comes from WS_THICKFRAME + the default
    // border color + the glass sheet (v7.1.23/v7.1.27) under DWM
    // composition -- which is where the field machine lives (the sheet,
    // the border color and the DWM shadow all read live there). A
    // composition-off machine loses only the legacy soft edge; that is the
    // trade that takes the ghost's head.
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = guiWndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hIcon = (HICON)LoadImageA(wc.hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                 LR_DEFAULTSIZE);
    if (!wc.hIcon) wc.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    wc.hIconSm = (HICON)LoadImageA(wc.hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                   LR_DEFAULTSIZE | LR_SHARED);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // v7.0.0: the erase is ours (WM_ERASEBKGND above);
                                  // a class-held brush would go stale the first time
                                  // applyTheme() deletes + recreates g_brBg
    wc.lpszClassName = L"twinspeak.control";
    if (!RegisterClassExW(&wc)) return 1;   // v7.1.20: checked (was ignored)
    // v7.1.5 THE INPUT PRIORITY: the message-loop thread ran at NORMAL while
    // the engine's helpers (health loop, volume watcher) and the RT audio
    // threads share the process -- under load the UI thread got the scraps
    // and mouse feedback arrived in time-spaced bursts (the field: "mouse
    // inputs too spaced out in time"). HIGHEST outranks every normal thread
    // in the process; the audio RT threads still outrank US (MMCSS Pro Audio
    // sits above priority 15), so the DSP path keeps every cycle it owns.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    if (g_startTray) {
        // v7.1.6 TRAY-FIRST: the startup launch builds NO control window at
        // all. The old path created the full 880x620 panel HIDDEN and trusted
        // every guard to keep it hidden -- and boot machinery still found a
        // way to pop it (two rounds of gates, the field still saw the window).
        // Now there is nothing to pop: an invisible top-level window of the
        // same class owns the tray icon and the second-instance messages;
        // the real panel is built the first time the user clicks the icon
        // (guiCreateRealWindow, on demand). FindWindowA-based duplicate
        // detection still works -- the surrogate carries the class.
        g_trayWnd = CreateWindowExW(0, L"twinspeak.control",
                                    L"TwinSpeak Control Panel",   // v7.2.1: Task Manager's
                                                                  // window entry (the hidden
                                                                  // surrogate); v7.2.0:
                                                                  // "...Panel: Tray"
                                    WS_OVERLAPPED, 0, 0, 0, 0,
                                    nullptr, nullptr, wc.hInstance, nullptr);
        if (!g_trayWnd) { if (mutex) CloseHandle(mutex); return 1; }
        tsAllowUiMessages(g_trayWnd);   // v7.1.20: the surrogate must hear explorer + twins
        g_hwnd = g_trayWnd;
        trayAdd();
        trayBalloon("Running in the tray -- the engine is starting.\n"
                    "Click the icon to open the control panel; "
                    "right-click for Start/Stop and Exit.");
        twsLog("[twinspeak] gui           : tray-only start -- no window exists; double-click the tray icon\n");
        twsLog("                            to build and open the control panel (no taskbar button).\n");
        // v7.1.2 STARTUP ENGINE: the startup launch is the boot path -- the
        // user signs in and the splitter must simply be running. With
        // startup_engine on (the default) the engine starts here, straight
        // from the ini (no controls exist yet -- cfgFromControls keeps g_cfg).
        // (A missing key parses as ON; explicit off restores the old
        // wait-for-START behavior.)
        if (twsCfg.startupEngine) {
            twsLog("[twinspeak] gui           : startup launch -- auto-starting the engine (startup_engine).\n");
            guiStartEngine(g_hwnd);
        }
    } else {
        // the double-click path: build the panel, show it (guiCreateRealWindow
        // renders the whole tree into the hidden redirection surface first --
        // the v6.6.5 white-flash trick -- so SW_SHOWNA lands one finished
        // frame, no activate/erase cycle).
        if (!guiCreateRealWindow()) { if (mutex) CloseHandle(mutex); return 1; }
        // v7.4.7 THE BUTTON RIDE: the first show carries the activation.
        // v6.6.5 showed SW_SHOWNA (no-activate: one pre-rendered frame, no
        // activate/erase cycle) and v7.1.20 bolted SetForegroundWindow on
        // behind it for the keyboard -- two separate shell transactions.
        // The taskbar button is born in the SHOW transaction, and a
        // no-activate show is the exact case v7.1.7 had to patch with
        // ITaskbarList::AddTab ("a button that lagged behind a no-activate
        // show"; retired v7.1.26 -- Win11 files AddTab items as TABS and
        // renders them as one-letter stubs). On the field machine the
        // separate foreground call does not birth the button at all: the
        // window opens, "TidyVision TwinSpeak" is nowhere on the taskbar,
        // and only an unfocus + refocus cycle (a real activation change)
        // makes it appear. The fix is the order every normal app uses:
        // ShowWindow(SW_SHOW) -- show AND activate in ONE transaction, the
        // show the button birth actually rides. The v6.6.5 pre-render
        // already made activation safe (v7.1.20's own finding), so the
        // white-flash trick keeps its finished first frame and the
        // activation comes home inside the same transaction. The two
        // foreground calls stay as a belt for launch contexts that
        // suppress ShowWindow activation; on an already-active window
        // they are no-ops.
        ShowWindow(g_hwnd, SW_SHOW);   // v7.4.7: activating show -- the taskbar button rides it
        // v7.6.13 THE FIRST-FRAME BELT: the v6.6.5 pre-render validated the
        // update region while the window was HIDDEN. If that hidden render
        // ever fails, the shown window displays the failed surface with an
        // EMPTY update region -- nothing would ever repaint it (the field's
        // black window). One invalidate + synchronous repaint after the show
        // re-runs WM_PAINT on the VISIBLE window: the same buffered bytes
        // (a no-op visually when the pre-render worked, a corrected first
        // frame when it did not).
        InvalidateRect(g_hwnd, nullptr, TRUE);
        UpdateWindow(g_hwnd);
        // v7.1.20 THE FOCUS FIX: SW_SHOWNA alone left the first-open window
        // UNFOCUSED -- keyboard dead until the first click (the tray path
        // always foregrounded; the double-click path never did). The v6.6.5
        // pre-render already made activation safe.
        SetForegroundWindow(g_hwnd);
        SetActiveWindow(g_hwnd);
        // v7.4.8 THE BUTTON AUDIT: the activating show (v7.4.7) was still
        // not enough on the field machine -- the button stayed unborn until
        // an unfocus + refocus cycle. The audit takes the foreground with
        // the attach dance when the launch activation was suppressed, and
        // asserts WS_EX_APPWINDOW -- a button-worthiness style change on a
        // channel that depends on neither the show nor the foreground
        // event (see the helper above for the full mechanism).
        tsEnsureTaskbarButton(g_hwnd);
        g_tbCycle = 2;                   // v7.4.10: arm the second activation
                                         // (the unfocus + refocus replay,
                                         // ~500 ms out -- see WM_TIMER)
        g_tbForce = false;               // v7.4.11: plain start never forces
                                         // -- the launch grant keeps the
                                         // panel foreground (v7.4.10 path)
        // v7.1.26: no AddTab -- the native button arrives with the show.
        guiClampToWorkArea(g_hwnd);      // v7.1.20: never open half off-screen
        g_userShown = true;              // v7.1.20: a user double-clicked -- boot yields
        // v7.4.6: the tray icon lands at plain start too. Until now it was
        // added only on the first close-to-tray (trayAdd rode WM_CLOSE), so
        // a fresh launch had no tray presence at all -- the app the field
        // sees in the taskbar corner should be there from the first second,
        // exactly like a --tray start (v7.1.6). hint=false: no balloon --
        // the window IS open, "still running" would read wrong. Clicking
        // the icon while the window is visible just re-runs
        // guiRestoreWindow: ShowWindow(SW_SHOW) on a visible window is a
        // no-op and the foreground call keeps it focused.
        trayAdd();
        twsLog("[twinspeak] gui           : START runs the engine in this window; the pane below is its log.\n");
        twsLog("[twinspeak] gui           : closing the window hides it to the tray -- the engine stays up.\n\n");
    }
    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0) > 0) {
        // v7.1.5 CTRL+TAB cycles pages: Ctrl+Tab = next, Ctrl+Shift+Tab =
        // previous. Handled HERE -- IsDialogMessage hands WM_KEYDOWN to the
        // focused control, and the loop is the one place that sees the key
        // no matter which control holds focus. guiShowPage owns the atomic
        // switch, the fade and the strip repaint.
        if (m.message == WM_KEYDOWN && m.wParam == VK_TAB
            && (GetKeyState(VK_CONTROL) & 0x8000)) {
            guiShowPage(g_hwnd, (GetKeyState(VK_SHIFT) & 0x8000)
                                ? (g_tab + 2) % 3 : (g_tab + 1) % 3);
            continue;
        }
        // v7.1.20 KEYBOARD: Enter activates the focused button (IsDialogMessage
        // only drives Space -- there is no default-ID plumbing here); Esc hides
        // like the X. Esc yields to an open combo dropdown first (the list owns
        // the key while it is dropped).
        if (m.message == WM_KEYDOWN && (m.wParam == VK_RETURN || m.wParam == VK_ESCAPE)
            && g_hwnd && IsWindow(g_hwnd)) {
            HWND f = GetFocus();
            if (f) {
                char cn[16]; GetClassNameA(f, cn, sizeof(cn));
                if (m.wParam == VK_RETURN && !strcmp(cn, "Button")) {
                    SendMessageA(f, BM_CLICK, 0, 0);
                    continue;
                }
                if (m.wParam == VK_ESCAPE && strcmp(cn, "ComboBox") != 0) {
                    bool comboOpen = false;
                    for (HWND t = f; t && t != g_hwnd; t = GetParent(t)) {
                        char cn2[16]; GetClassNameA(t, cn2, sizeof(cn2));
                        if (!strcmp(cn2, "ComboLBox") ||
                            (!strcmp(cn2, "ComboBox") &&
                             SendMessageA(t, CB_GETDROPPEDSTATE, 0, 0))) {
                            comboOpen = true;
                            break;
                        }
                    }
                    if (!comboOpen) { PostMessageA(g_hwnd, WM_CLOSE, 0, 0); continue; }
                }
            }
        }
        // v7.1.4 KEYBOARD + TAB: IsDialogMessage drives the whole child tree --
        // Tab / Shift+Tab cycle the tab stops (buttons, pills, edits, combos
        // and now the sliders), Space activates the focused button, and the
        // keys a control claims via WM_GETDLGCODE (the sliders' arrows) are
        // delivered to it. Hidden pages are skipped (visibility-checked
        // navigation), so the tab ring follows the open page.
        if (g_hwnd && IsWindow(g_hwnd) && IsDialogMessageA(g_hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    KillTimer(g_hwnd, 1);
    // v7.1.20: join the engine ONCE here (the only blocking stop left -- the
    // UI is gone anyway). v7.1.26: the taskbar-list COM object is retired
    // with the AddTab machinery; nothing to release.
    guiStopEngineJoin();
    if (g_gdipToken) { Gdiplus::GdiplusShutdown(g_gdipToken); g_gdipToken = 0; }   // v6.6.5
    if (mutex) CloseHandle(mutex);
    return 0;
}

// ---------------------------------------------------------------- main ------
// v8.4.21: the console control handler also removes the tray icon on
// logoff/shutdown -- those events kill the process without WM_DESTROY,
// and every killed session left a stale icon behind. (The GUI path gets
// the same cleanup from WM_ENDSESSION; this covers the console-attached
// runs. Harmless no-op when no icon is ours.)
static BOOL WINAPI ctrlHandler(DWORD ev) {
    if (ev == CTRL_SHUTDOWN_EVENT || ev == CTRL_LOGOFF_EVENT) trayRemove();
    twsRun = false;
    return TRUE;
}

// v7.1.20 CONSOLE, GUI-SUBSYSTEM EDITION: the exe no longer allocates a
// console at load (that was the black window flashing on every double-click
// and at every sign-in -- the loader created it, then guiMain's FreeConsole
// destroyed it half a second later). Console modes re-attach themselves:
// attach to the parent terminal when one exists (no window is created for
// that), and -- only for the console modes, only if there is no parent --
// allocate one so double-clicked `--list` output is still visible.
static void tsEnsureConsole(bool allowAlloc) {
    const HANDLE so = GetStdHandle(STD_OUTPUT_HANDLE);
    if (so && so != INVALID_HANDLE_VALUE) return;   // piped / redirected / already attached
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        return;
    }
    if (allowAlloc && AllocConsole()) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}

// v6.6.0: the engine body, split out of main() so the GUI runs it on a
// worker thread and can restart it in-process. The caller owns the single-
// instance mutex. guiMode: no background/FreeConsole detach (the log pane is
// the destination) and restart-safe (g_run re-armed, all engine globals
// re-initialized below).

int main(int argc, char** argv) {
    InitializeCriticalSection(&g_logCS);   // v5.0 logger
    // v8.4.21: the tray icon is removed on EVERY normal exit, whatever path
    // takes us down -- the scheduled cleanup ("upon exit the system tray
    // icon just gets killed along with it"): logoff/shutdown kill the
    // process without WM_DESTROY, and every such session left a stale icon
    // behind. The immediate paths (Exit, WM_ENDSESSION, the console
    // shutdown event) remove it themselves; this is the belt.
    atexit(trayRemove);
    tsEnsureConsole(false);                // v7.1.20: ride the parent terminal, if any
    std::string iniPath;
    bool doList = false, fgForce = false, allowMulti = false;
    bool wantGui = false, startTray = false;   // v6.6.1: --gui / --tray
    bool doExit = false;                       // v7.2.1: --exit (clean remote stop)
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--list")) doList = true;
        // v9.0.1: --list-ks is REMOVED with the WDM-KS backend (the flag now
        // falls through to the ini-path branch, like any unknown argument).
        else if (!strcmp(argv[i], "--ini") && i + 1 < argc) iniPath = argv[++i];
        else if (!strcmp(argv[i], "--foreground")) fgForce = true;    // v6.2
        else if (!strcmp(argv[i], "--multi"))      allowMulti = true; // v6.2
        else if (!strcmp(argv[i], "--gui"))      wantGui = true;      // v6.6.0: explicit, same as default
        else if (!strcmp(argv[i], "--tray"))     { wantGui = true; startTray = true; }   // v6.6.1: window hidden in the tray
        else if (!strcmp(argv[i], "--exit"))     doExit = true;      // v7.2.1: clean remote stop
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            twsLog("usage: twinspeak [--list] [--ini path] [--foreground] [--multi]\n"
                 "  (no args)     the control window -- every ini option + START/STOP + live log\n"
                 "                (closing it hides to the TRAY; the engine stays up)\n"
                 "  --tray        straight to the system tray -- NO window exists until you\n"
                 "                click the tray icon (never a taskbar button; this is what\n"
                 "                the tray's 'Run at Windows startup' writes -- use it for\n"
                 "                Task Scheduler sign-in tasks too)\n"
                 "  --exit        ask an already-running instance to exit CLEANLY -- tray icon\n"
                 "                removed, engine stopped (use this instead of taskkill /F,\n"
                 "                which orphans the tray icon until it is hovered)\n"
                 "  --list        show every WASAPI endpoint (the usual device needles)\n"
                 "  --foreground  ignore background = on for this run (keep the console)\n"
                 "  --multi       allow a second instance (skip the single-instance lock)\n");
            return 0;
        } else iniPath = argv[i];
    }

    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string exeDir = exePath;
    size_t sl = exeDir.find_last_of("\\/");
    if (sl != std::string::npos) exeDir = exeDir.substr(0, sl); else exeDir = ".";
    if (iniPath.empty()) iniPath = exeDir + "\\twinspeak.ini";

    if (doList) { twsListDevices(); return 0; }
    // v7.2.1: --exit -- ask the running instance to stop CLEANLY (tray icon
    // removed, engine stopped, window position captured) instead of the old
    // advice, taskkill, whose /F form orphans the tray icon until hovered
    // (the field's "tray icons duplicating": every force-killed twin left
    // one behind). Both windows of the class hear WM_APP+6 -- the surrogate
    // included -- and tsAllowUiMessages opened it through UIPI, so an
    // elevated twin answers a non-elevated --exit too.
    if (doExit) {
        HWND other = nullptr;
        for (int i = 0; i < 6 && !other; ++i) {          // ~1.2 s: a booting twin
            other = FindWindowA("twinspeak.control", nullptr);
            if (!other) Sleep(200);
        }
        if (!other) {
            twsLog("[twinspeak] --exit      : no running instance found (nothing to stop).\n");
            return 1;
        }
        DWORD_PTR res = 0;
        const BOOL ok = SendMessageTimeoutA(other, WM_APP + 6, 0, 0,
                                            SMTO_ABORTIFHUNG, 4000, &res);
        twsLog(ok ? "[twinspeak] --exit      : asked the running instance to exit cleanly.\n"
                : "[twinspeak] --exit      : could not reach the running instance (UIPI or hung).\n");
        return ok ? 0 : 1;
    }

    // v6.6.0: NO ARGUMENTS = the control window. The console modes all keep
    // their exact behavior behind explicit flags. v6.6.1: --gui is the same
    // default made explicit; --tray starts hidden in the tray.
    if (argc <= 1 || wantGui) return guiMain(iniPath, startTray);

    // v7.1.20: a real console mode -- make sure the output has somewhere to
    // go even with no parent terminal (AllocConsole only here; the GUI path
    // never allocates).
    tsEnsureConsole(true);

    // v6.2 single-instance lock (console runs; the GUI holds its own).
    HANDLE hInstance = nullptr;
    if (!allowMulti) {
        hInstance = CreateMutexA(nullptr, TRUE, "Local\\twinspeak.instance");
        if (hInstance && GetLastError() == ERROR_ALREADY_EXISTS) {
            twsLog( "[twinspeak] already running -- twinspeak is single-instance.\n"
                            "            stop the running copy cleanly:  twinspeak.exe --exit\n"
                            "            (taskkill /F works but orphans the tray icon until hovered;\n"
                            "            a deliberate second copy: add --multi)\n");
            return 1;
        }
    }
    // v9.6.0: the ORIGINAL engineMain loaded the ini itself (it died with the
    // old engine); the core reads twsCfg only, so the console path loads here
    // -- exactly the guiMain call at the GUI seam. A fresh machine gets the
    // template written, then a refusal naming input_device/output_device1.
    if (!loadIni(iniPath.c_str())) writeTemplate(iniPath.c_str());
    const int rc = twsEngineRun(iniPath.c_str(), fgForce, false);
    if (hInstance) CloseHandle(hInstance);
    return rc;
}

// v7.1.20: the GUI-subsystem entry. The linker picks WinMainCRTStartup under
// --subsystem,windows; this shim forwards to main() with parsed args so the
// console modes keep their exact argv behavior. (Under a console-subsystem
// build the entry is mainCRTStartup and this shim simply never runs.)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int n = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &n);
    char** argv = nullptr;
    if (wargv && n > 0) {
        argv = (char**)HeapAlloc(GetProcessHeap(), 0, sizeof(char*) * (size_t)n);
        if (argv) {
            for (int i = 0; i < n; ++i) {
                const int need = WideCharToMultiByte(CP_ACP, 0, wargv[i], -1,
                                                     nullptr, 0, nullptr, nullptr);
                argv[i] = (char*)HeapAlloc(GetProcessHeap(), 0,
                                           (SIZE_T)(need > 0 ? need : 1));
                if (argv[i]) {
                    if (need > 0)
                        WideCharToMultiByte(CP_ACP, 0, wargv[i], -1, argv[i], need,
                                            nullptr, nullptr);
                    else
                        argv[i][0] = '\0';
                }
            }
        }
        LocalFree(wargv);
    }
    const int rc = main(n, argv);
    if (argv) {
        for (int i = 0; i < n; ++i)
            if (argv[i]) HeapFree(GetProcessHeap(), 0, argv[i]);
        HeapFree(GetProcessHeap(), 0, argv);
    }
    return rc;
}
