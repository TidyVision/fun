// ============================================================================
//  twinspeak v7.1.16 -- ASIO-STRICT bit-exact engine (no resampler anywhere)
//
//  MISSION: one 8ch shared capture of a VAC cable fans out (memcpy only) into
//  two independent per-leg pipelines -- leg1 = Realtek FL/FR + LFE fold (5.1),
//  leg2 = Adesso Xtream S5 (center-only) -- WASAPI/WDM-KS exclusive, every
//  sample bit-exact, no resampler or pitch path anywhere. delay1/2_ms are
//  whole-ms ring targets; drift corrections are frame-level and logged.
//
//  ARCHITECTURE (v6.0):
//    VAC cable (8ch shared capture, exclusive)
//      +--> leg1.raw ring --> pump 1 --> leg1.out ring (6ch) --> Realtek
//      |      mix: FL=L+gain_lfe*LFE, FR=R+gain_lfe*LFE, FC=driven, SL/SR=0
//      +--> leg2.raw ring --> pump 2 (FIRST + higher priority)
//             gates: ch3 zero -> nothing; ch3 == Windows stereo-upmix
//             fingerprint -> muted; mix: both outs = ch3 only, rest muted
//             + 40 Hz @ 0.1% keep-alive at the device layer (v7.1.12)
//    playback callbacks = the ASIO buffer switch: gated excess drop +
//    last-buffer replay. WASAPI exclusive is INTEGRATED (v6.6.8): both legs
//    open exclusive together (shared fallback reopens BOTH), capture pin
//    exclusive with logged fallback. Exclusive bypasses the volume slider --
//    hence volume_follow + pin_outputs. clock_sync, drift_guard, power_guard,
//    yield_default, center_gate, center_upmix_guard, background: always on.
//    Drift corrections: the warden (baseline at seconds 5..9, >=
//    drift_correct_ms held 3 s, drift_interval_ms spacing) plus a
//    FIRST-SECOND fast tier at 10x that cadence (v7.1.14).
//
//  CHANGELOG (condensed; full notes in CHANGELOG.txt):
//    v7.1.16 THE REGION THAT ATE THE SHADOW + THE STACK THAT FITS: the
//            field's "I still don't see a drop shadow" was our own log
//            line talking -- "a shaped window cannot carry the DWM
//            shadow". Attr 33 (corner preference) is Win11-only; on the
//            field's Win10 it failed and v7.1.0's fallback shaped the
//            window with a GDI region -- and Windows refuses the DWM
//            window shadow for ANY region-shaped window, so every fix
//            since (v7.1.11's restore, v7.1.15's extend-frame +
//            NCRENDERING ON pins) was revoked by our own region lines
//            later. The region fallback is GONE -- the window is never
//            shaped again; Win10 keeps square corners and gets the full
//            all-directions shadow back. And the page-4 stack now fits:
//            the SAVE TO SLOT note hung 28 px past the panel bottom,
//            across the action row (the arithmetic was unwinnable -- 45
//            px of button+note vs 44 px of panel); the sliders ride up
//            8 px, the button rises to 354, the note is one line ending
//            at the panel edge. GUI-only; DSP byte-identical to v7.1.4.
//    v7.1.15 GUI POLISH, THE FIELD'S FOUR: the center-EQ note carried
//            17 text rows in a 13-row box -- the STATIC clipped its
//            own tail; re-wrapped short, full height, nothing cuts
//            off. SAVE TO SLOT moved from column B to right below
//            the sliders with its own line ("makes sure this slot's
//            current modifications persist across sessions"). The
//            ON/OFF pills re-inked: ON = full accent face + the
//            dark on-accent ink (the old bright-on-accentDk label
//            measured ~3.3:1), OFF border steps cLine -> cDim so the
//            pill reads on every theme. The window shadow is
//            EXPLICIT: DwmExtendFrameIntoClientArea 1 px +
//            NCRENDERING_POLICY = ON -- the documented triggers a
//            corner preference alone does not guarantee. Plus the
//            settings page's keep-alive note says 40 Hz @ 0.1% now
//            (stale since v7.1.12). GUI-only; DSP byte-identical
//            to v7.1.4.
//    v7.1.14 THE FIRST SECOND AT 10X: v7.1.13's snap is REVERTED -- the
//            field still measured the start offset with it in. Instead,
//            the drift CORRECTION itself runs ten times faster for the
//            first 1000 ms of a run, then returns to its normal speed.
//            The warden's steady-state gates (baseline armed at seconds
//            5..9, |err| >= drift_correct_ms held 3 s, corrections 5 s
//            apart) are all wide open exactly when the tick test looks,
//            so the fast tier samples the ledger every 100 ms against
//            the ini's own delay model, decides in 3 fast windows
//            (300 ms instead of 3 s), and re-fires every 500 ms (instead
//            of 5 s) with the warden's own bit-exact skip/replay-pad
//            table -- the startup residual is re-pinned before the
//            baseline can absorb it. Same trigger distance as the
//            warden; hardcoded (no ini key, no UI). Transport-level
//            only; DSP byte-identical to v7.1.4.
//    v7.1.12 KEEP-ALIVE RETUNE: the out2 keep-alive tone is back at 40 Hz
//            and up to 0.001 (-60 dBFS) -- the field's call: 1 Hz @ 0.0001
//            (-80 dBFS) was too little for some amps' sleep timers. Still
//            BUILT IN (no ini key, no UI), still post-mix at the device
//            layer -- it can never enter the capture stream OBS records.
//            The cycle table shrinks to 1200 samples (48000/40); the sum
//            path is table-length agnostic and unchanged.
//    v7.1.11 THE RESTART IS CLEAN AND THE LEGS OPEN TOGETHER: guiStopEngine
//            raises the stop gap ITSELF (the engine's health loop slept a
//            full second before noticing -- APPLY+RESTART played program
//            audio through the stop, the silent gap never arrived; the
//            loop now polls g_run every 50 ms); the start-align's first-
//            switch stamps are PERIOD-CORRECTED (the first callback fires
//            one device period after the DAC began eating, and the two
//            legs' periods differ -- the raw gap carried a P1-P2 bias and
//            leg1 landed late until the warden reeled it in); the DWM's
//            all-directions window shadow is back (NCRENDERING_POLICY =
//            DISABLED was killing it, leaving the thin bottom-right
//            CS_DROPSHADOW); the tick test logs a per-leg census at the
//            natural end (the center-missing witness); tabs land on +7 px
//            (167). DSP byte-identical to v7.1.4.
//    v7.1.10 ONE ACCENT, +6 px TABS: the slot selection collapses to ONE
//            variable -- v7.1.9's armed target and exact-match highlight
//            were separate and could light TWO slot faces at once; now a
//            click selects the slot (accent) and that selection doubles
//            as the SAVE TO SLOT target, surviving slider tweaks. The
//            tab faces land on the field's sweet spot: +6 px (166).
//            DSP byte-identical to v7.1.4.
//    v7.1.9  SAVE BUTTON + TAB WIDTH TRIM: the EQ slots keep left-click =
//            apply, but the save path leaves the hidden right-click -- a
//            SAVE TO SLOT button sits on the CENTER EQ page: click a slot
//            to arm it (the face stays accented while you tweak), adjust
//            the sliders, press the button and the curve + the EQ pill
//            land in the armed slot, ini spliced at once; right-click
//            still saves for the fast path. The tab faces come back to
//            +4 px (160 -> 164) -- 168 overflowed the strip's right pad.
//            DSP byte-identical to v7.1.4.
//    v7.1.8  TITLEBAR FLICKER GONE + THE X IS BACK + MORE THEMES + EQ SLOTS:
//            the bar renders into a memory buffer and lands as ONE blit --
//            no intermediate state for DWM to sample, so the title text and
//            the close X stop flickering on hover/activate/theme clicks;
//            the Marlett 'r' glyph is retired for the modern thin caption X
//            (two AA strokes, the DWM close shape); DARK RED, DARK ORANGE
//            and LIGHT RED join the theme cycler (eleven total, appended so
//            saved gui_theme indices survive); the four built-in EQ presets
//            become FIVE USER SAVE SLOTS -- left-click applies, right-click
//            saves the sliders + pill into the slot and splices the ini
//            (eq_slotN_on/freq/gain/q), slot 1 ships reserved with 387 Hz /
//            +3.5 dB / Q 0.30; the tab faces widen (160 -> 168) to fill
//            more of the strip. DSP byte-identical to v7.1.4.
//    v7.1.7  UI RESTRUCTURE, the field's wordlist: FIVE tabs (DEVICES |
//            TIMING | TRANSPORT | SETTINGS | CENTER EQ) -- OTHER merged
//            away, its startup pill and its SYNC column (drift trio) now
//            live on SETTINGS; VOLUME renamed SETTINGS; headers renamed
//            (DEVICES / GAIN / DEVICE PERIODS (ms) / TIMING); the
//            backend1/backend2 combos are GONE (both legs are wasapi
//            exclusive by default; backend1/backend2 = ks in the ini opts
//            a leg into Kernel Streaming), the transport rows lost their
//            ks_ prefix (period_frames / periods) and every mention says
//            "wasapi exclusive"; chimes moved just below chime_gain, the
//            "CENTER CUES" header is gone, "VOLUME + LEVELS" is now
//            "CHIME/KEEPALIVE"; keepalive_hz/keepalive_gain are BUILT IN
//            (1 Hz @ 0.0001, keys and rows retired, old ini values load
//            clean and are ignored); the keep-alive tone and the chime
//            are POST-MIX by construction (summed in the out2 output
//            callbacks, never in the capture stream OBS records); the
//            taskbar slot is created the instant the panel opens
//            (ITaskbarList AddTab/ActivateTab on show, DeleteTab on
//            hide). DSP byte-identical to v7.1.4.
//    v7.1.6  TRAY-FIRST START: a --tray (startup) launch builds NO control
//            window at all -- an invisible tray-owner window (same class,
//            never shown) carries the tray icon and the second-instance
//            messages; the panel is built ON DEMAND at the first icon
//            click / menu Open / second-instance ask -- a normal window
//            with a taskbar button while open, nothing while hidden. A
//            boot pop is structurally impossible -- there is no window
//            to show -- and at rest the app lives only in the tray. The
//            engine auto-start runs straight from the ini (no controls
//            exist yet); the buffered boot log flushes into the pane
//            when the panel first opens. DSP byte-identical to v7.1.4
//            (and to every 7.1.5 build).
//    v7.1.5  field fixes: THE BOOT POP (a duplicate --tray launch signaled
//            WM_APP+4 and forced the hidden startup window on screen --
//            --tray duplicates now exit silently, manual ones still come
//            up); THE START LATENCY, in two halves: (1) the Sleep(30) gap
//            piled pre-playback backlog on the prefill -- it is trimmed to
//            the anchor BEFORE the legs start; (2) the two devices begin
//            EATING their prefills a few ms apart and a start stagger
//            shifts that leg's whole stream while the ledger reads a FLAT
//            drift -- the first buffer switch of each leg is QPC-stamped
//            and the early leg is replay-padded by exactly the gap, so
//            both streams open on the same instant and the ledger starts
//            ON the calibrated baseline (no settle correction, no warden
//            wait); STATUS + LOG FLICKER (the status line paints in the
//            frame buffer, the log EDIT renders through WM_PRINTCLIENT
//            into a buffer and appends are WM_SETREDRAW-batched);
//            THE BOOT POP, round two: Windows' app-restart and Startup-
//            folder shortcuts replay the exe WITHOUT --tray -- a plain
//            boot twin either signaled "come up" or won the mutex race
//            and showed its own window. A shell-age check (the logon
//            window) now identifies boot twins: they exit silently, a
//            plain winner inside the logon window runs as the startup
//            instance, and a --tray twin yields boot ownership (WM_APP+5)
//            instead of letting the window pop; MOUSE INPUT
//            SPACING (the
//            UI thread stops queueing behind the engine's helpers at
//            HIGHEST priority, the watermark measure is cached, and the
//            status paint pauses mid-drag); CTRL+TAB / CTRL+SHIFT+TAB
//            cycle pages; the keyboard map lives tiny in the bottom-left
//            strip (+shift = backwards). DSP byte-identical to v7.1.4.
//    v7.1.4  fixes + keyboard: the GDI+ bar's gray hairline edges are gone
//            (integer fills + 1px overscan, no shape AA on the bar -- the
//            AA boundary blend against the stale pixels was the gray);
//            the title text is THEME-AWARE: plain caption color on light
//            themes, a cyan-gray vertical gradient on dark ones; keyboard
//            + Tab work (IsDialogMessage, WS_TABSTOP sliders, arrows
//            adjust the focused slider, dashed focus rings on buttons and
//            sliders); CS_DROPSHADOW on the window. DSP byte-identical
//            to v7.1.3.
//    v7.1.3  the titlebar is GDI+: ONE Gdiplus::Graphics paints the whole
//            bar -- the face is a subtle top-lit vertical gradient of the
//            theme bg, the bottom edge + close hover face + close X are
//            GDI+ fills with grayscale AA, the glyph brightens to the
//            accent on hover, and "TwinSpeak" keeps its blue->teal
//            gradient but LIFTED (the very-dark-blue top stop vanished
//            against the dark themes). DSP byte-identical to v7.1.2.
//    v7.1.2  tab labels SMOOTH: the label layer + both strip passes render
//            grayscale-antialiased (AntiAliasGridFit; the DirectWrite
//            fallback went GRAYSCALE) -- soft like the buttons, still
//            cText-bright on the dark themes. STARTUP ENGINE: the startup
//            launch (--tray) now starts the engine by itself
//            (startup_engine, default on; off = wait for START). DSP
//            byte-identical to v7.1.1.
//    v7.1.1  tab labels are the BUTTONS' pixels: GDI+ single-bit grid-fit
//            (the exact WM_DRAWITEM renderer) into a transparent ARGB layer
//            uploaded as a D2D bitmap over the GPU geometry -- DirectWrite
//            has no grid-fit aliasing and never matched. Inactive labels
//            read cText (the resting button color) -- cDim washed out on
//            the dark themes; active ramps cText -> cAccent. DSP
//            byte-identical to v7.1.0.
//    v7.1.0  window chrome polish: the XP-era caption flash on the first
//            drag is suppressed (undocumented WM_NCUAHDRAWCAPTION/FRAME),
//            the window corners are rounded (DWM round preference -- true
//            antialiased; GDI region fallback on Win10), the tab labels
//            render aliased/crisp on both the GPU and GDI+ passes, the
//            active EQ preset's 2 px accent ring draws on an inset-2 path
//            (its outer half no longer shaves on the item edge), and the
//            custom bar carries "TwinSpeak" in a dark-blue -> teal
//            vertical gradient. DSP byte-identical to v7.0.9.
//    v7.0.9  custom self-painted titlebar (DWM caption retired: theme colors
//            land at click time, no caption text), buffered single-blit frame
//            paint (watermark flicker gone), pending renders flushed before
//            the atomic page switch, strip recolors inside the theme click.
//            DSP byte-identical to v7.0.8.
//    v7.0.8  glow-fade flicker root cause: fade anchor stamped AFTER the
//            atomic repaint; now stamped before it.
//    v7.0.7  GPU strip on the BITBLT swap chain (FLIP on a child HWND fought
//            the GDI surface); cached brush/labels/status text; theme recolor
//            + strip repaint made synchronous.
//    v7.0.6  true GPU strip (D3D11 + DXGI + D2D 1.1, GDI+ fallback); atomic
//            page switch (WM_SETREDRAW freeze + one RDW_UPDATENOW).
//    v7.0.5  strip renders through Direct2D on a persistent buffer;
//            timeBeginPeriod(1) for the life of the fade.
//    v7.0.4  the strip is ONE custom control owning tabs + halo; fade
//            repaints only the strip; gui_theme persists via ini.
//    v7.0.3  rounded tab window regions; 170 ms ease-out fade-in; tab face =
//            one memory-DC blit.
//    v7.0.2  halo outward-only via the frame clip; tick button untoggles at
//            natural end; tab renamed VOLUME.
//    v7.0.1  restart re-arms the frame-interval ledger (no stale ring fill
//            in the capture-sequence math); glow fully inside the tab.
//    v7.0.0  stop gap (silence before device close on restart); keepalive
//            gain 0.001; pin_outputs integrated; ring defaults 10/10/5;
//            EIGHT live themes; the 120 BPM tick test.
//    v6.6.9  ini splice leaks plugged (empty values too); START label tells
//            the truth; silence compression in the pumps.
//    v6.6.8  ten switches integrated (always on, keys ignored); dark sliders;
//            audio-object flags.
//    v6.6.7  ghost corners round 2: buttons erase in plain GDI from the live
//            surface; owner-draw buffed.
//    v6.6.6  log pane CLIENTEDGE replaced by a WM_PAINT frame; startup pill;
//            engine-stopped status; watermark v3.
//    v6.6.5  GDI+ antialiased UI end to end; condensed 880x620 layout;
//            watermark linear gradient.
//    v6.6.4  scroll-ghost fix via WM_CTLCOLORSTATIC; close-only title bar.
//    v6.6.3  TidyVision branding; OPAQUE background mode for scrolled log
//            rows.
//    v6.6.2  layout repairs (button regPage + per-tab visibility).
//    v6.6.1  tray life (close hides, engine stays up); WS_CLIPCHILDREN
//            stale-paint fix; TaskbarCreated re-add.
//    v6.6.0  the control window (no args = GUI); engine in-process on a
//            worker thread, restart-safe.
//    v6.5.18 EQUALIZER APO crackle closed (system APO on the shared
//            endpoint); center dialogue EQ (RBJ peaking biquad on ch3).
//    v6.5.17 leg1 crackle: diagnostic-first, thread-time instrumentation.
//    v6.5.16 default-render switch doubling/deadlock fixed; session census.
//    v6.5.15 DAC yield keeps leg endpoint volumes untouched.
//    v6.5.14 DAC yields the default device when a leg IS the default.
//    v6.5.13 KS WaveRT lap-loss mode fixed (lapse guard).
//    v6.5.12 scheduled deep resync (deep_resync_min) for long watches.
//    v6.5.11 interrupt-storm buzz recovery (stall resync).
//    v6.5.10 the whole warden chain on a five-second clock.
//    v6.5.9  measured per-leg device buffering on the health line (devbuf).
//    v6.5.8  two warden gaps closed (10 ms rhythm-game skew).
//    v6.5.7  watchdog_key withdrawn; audio-thread inspection shipped.
//    v6.5.4  CPU-affinity pin proven to be the glitch source -- withdrawn.
//    v6.5.3  CPU placement experiment (last-2-core pinning).
//    v6.5.2  sleep / Modern-Standby power guard.
//    v6.5.1  "~65 ms delay" was the user's own delay1_ms -- reading, not bug.
//    v6.5    pure optimization pass: no key/log/rule/sample changes.
//    v6.4    drift warden: frame-interval ledger + corrections.
//    v6.3.1  leftover Adesso delay closed.
//    v6.3    WDM-KS backend inside twinspeak (lowest latency, 256 samples).
//    v6.2    background start, chimes, more field directives.
//    v6.1    bit-exact per-leg ring delay (delay1/2_ms) + center gates.
//    v6.0    no pitch shift: 8ch shared capture + two bit-exact pipelines,
//            implemented as structure, not options.
//    v5.0    the ASIO model (buffer-switch semantics) -- the real crackle fix.
//    v4.0    clock servo rewrite (no drop/dup corrections).
//    v3.9    health-line crash fix; volume autodetect; tunable keep-alive.
//    v3.8    volume follow, output pinning, out2-first, self-healing legs.
//    v3.7    banks can no longer pin (162 ms pending fix).
//    v3.6    scaled drift-guard windows (10 ms crackle + wobble fixed).
//    v3.3    routing audit, made visible -- no behavior change.
//    v3.2    ring depth decoupled from the device period.
//    v3.1    lfe_fold defaults ON; the discrete 5.1 sub is the option.
//    v3.0    native 5.1 leg1 (no fake center from upmix) + refill-to-target
//            level controller (the v2 skip/dup crackle is structurally gone).
//
//  Config (twinspeak.ini next to the exe -- or pass a path):
//      out1ms = 5, out2ms = 5   ; WASAPI periods -- keep EQUAL for alignment
//      ring1_ms = ring2_ms = 10 ; out-ring depth (floored at 2*period+burst+4ms)
//      delay1_ms / delay2_ms    ; bit-exact ring delay per leg (the EARLY
//                               ; leg takes the delay; see the template)
//      cap_ms = 5               ; capture burst period
//      lfe_fold = on            ; LFE folded onto FL/FR at gain_lfe
//      input_device / output_device1 / output_device2
//      optional: gain_l, gain_r, gain_lfe, gain_center, volume_follow,
//                pin_outputs (retired key, ignored), out2_first,
//                volume_endpoint, drift_interval_ms, deep_resync_min
//                (v7.1.7: keepalive_hz/keepalive_gain retired -- BUILT IN,
//                1 Hz @ 0.0001; old keys load clean and are ignored)
//      Integrated (always on, old keys load clean and are ignored):
//      capture_exclusive, exclusive, align_modes, clock_sync, drift_guard,
//      power_guard, yield_default, center_gate, center_upmix_guard, background.
//
//  Input channel order is the VAC 8ch standard: 1L 2R 3C 4LFE 5SL 6SR 7RL 8RR.
//  leg1 output order is Windows 5.1: FL FR FC LFE BL/SL BR/SR (6ch).
//  Fixed 48 kHz (the cable's rate). Build: bash build.sh  (zig c++, WASAPI).
// ============================================================================


#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <windows.h>
#include <commctrl.h>       // v6.6.0: trackbars for the GUI
#include <shellapi.h>       // v6.6.1: Shell_NotifyIcon -- the tray icon
#include <objbase.h>        // v7.1.7: CoCreateInstance -- ITaskbarList (the tab slot)
#include <gdiplus.h>        // v6.6.5: antialiased owner-drawn UI (buttons, text, watermark)
#include <d2d1_1.h>         // v7.0.6: TRUE GPU strip (D3D11 + DXGI swap chain + D2D 1.1;
                            // v7.0.7: bitblt-model presentation, no flip on child windows)
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>         // v7.0.6: GPU glyphs for the strip labels
#include <mmsystem.h>
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
#include <objbase.h>
#include <propsys.h>
#include <propidl.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>    // v6.5.16: IAudioSessionManager2/Enumerator (yield re-grab guard)
#include <endpointvolume.h>
#include <xmmintrin.h>     // v6.5: FTZ (SSE flush-to-zero)
#include <pmmintrin.h>     // v6.5: DAZ (denormals-are-zero)

// --------------------------------------------------- v6.3 WDM-KS includes ---
// initguid first (user-mode GUID defs, no import lib), then the KS property
// sets + setupapi filter enumeration. Vista+ WaveRT values missing from some
// ksmedia.h copies are re-defined here under TS_ names, WDK-exact layouts.
#include <initguid.h>
#include <ks.h>
#include <ksmedia.h>
#include <setupapi.h>

#ifndef FILE_DEVICE_KS
#define FILE_DEVICE_KS 0x0000002f
#endif
// IOCTL_KS_PROPERTY -- METHOD_NEITHER: lpInBuffer = property(+payload),
// lpOutBuffer = returned data. (CTL_CODE(DeviceType, Function, Method, Access))
#ifndef IOCTL_KS_PROPERTY
#define IOCTL_KS_PROPERTY CTL_CODE(FILE_DEVICE_KS, 0x000, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif

// KSPROPERTY_RTAUDIO property IDs (Vista+; GETPOSITIONFUNCTION stays 0)
enum {
    TS_KSPR_RTAUDIO_BUFFER                   = 1, // allocates the WaveRT looped buffer
    TS_KSPR_RTAUDIO_HWLATENCY                = 2, // FifoSize/ChipsetDelay/CodecDelay
    TS_KSPR_RTAUDIO_POSITIONREGISTER         = 3, // maps the HW play-position register
    TS_KSPR_RTAUDIO_CLOCKREGISTER            = 4,
    TS_KSPR_RTAUDIO_BUFFER_WITH_NOTIFICATION = 5,
    TS_KSPR_RTAUDIO_REGISTER_NOTIFICATION_EVENT    = 6,
    TS_KSPR_RTAUDIO_UNREGISTER_NOTIFICATION_EVENT  = 7
};

// WDK ksmedia.h WaveRT structures (layouts as shipped by the Windows DDK)
typedef struct _TS_KSRTAUDIO_BUFFER_PROPERTY {
    KSPROPERTY Property;
    PVOID      BaseAddress;
    ULONG      RequestedBufferSize;
} TS_KSRTAUDIO_BUFFER_PROPERTY, *PTS_KSRTAUDIO_BUFFER_PROPERTY;

typedef struct _TS_KSRTAUDIO_BUFFER {
    PVOID BufferAddress;          // kernel looped buffer, mapped into our space
    ULONG ActualBufferSize;       // bytes the driver actually allocated
    BOOL  CallMemoryBarrier;      // driver asks for a barrier between writes
} TS_KSRTAUDIO_BUFFER, *PTS_KSRTAUDIO_BUFFER;

typedef struct _TS_KSRTAUDIO_HWREGISTER_PROPERTY {
    KSPROPERTY Property;
    PVOID      BaseAddress;
} TS_KSRTAUDIO_HWREGISTER_PROPERTY, *PTS_KSRTAUDIO_HWREGISTER_PROPERTY;

typedef struct _TS_KSRTAUDIO_HWREGISTER {
    PVOID     Register;           // mapped, user-readable play-position register
    ULONG     Width;
    ULONGLONG Numerator;
    ULONGLONG Denominator;
    ULONG     Accuracy;
} TS_KSRTAUDIO_HWREGISTER, *PTS_KSRTAUDIO_HWREGISTER;

typedef struct _TS_KSRTAUDIO_HWLATENCY {
    ULONG FifoSize;               // bytes held in the hardware FIFO
    ULONG ChipsetDelay;
    ULONG CodecDelay;
} TS_KSRTAUDIO_HWLATENCY, *PTS_KSRTAUDIO_HWLATENCY;

// KsCreatePin lives in ksuser.dll -- loaded dynamically, exactly like the
// reference WDM-KS backends do (no import-lib dependency at all).
typedef DWORD (WINAPI *TS_KsCreatePinFn)(HANDLE, PKSPIN_CONNECT, ACCESS_MASK, PHANDLE);

#ifndef KSAUDIO_SPEAKER_5POINT1
#define KSAUDIO_SPEAKER_5POINT1           0x0000003F  // FL FR FC LFE BL BR
#endif
#ifndef KSAUDIO_SPEAKER_5POINT1_SURROUND
#define KSAUDIO_SPEAKER_5POINT1_SURROUND  0x0000060F  // FL FR FC LFE SL SR
#endif

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// ------------------------------------------------------------------ config --
static constexpr int      kInCh          = 8;      // VAC order: L R C LFE SL SR RL RR
static constexpr int      kOutCh1        = 6;      // leg1: Windows 5.1 FL FR FC LFE XL XR
static constexpr int      kOutCh2        = 2;      // leg2: stereo center speaker
static constexpr int      kScratchFrames = 8192;
static constexpr unsigned kSampleRate    = 48000;

// output2 keep-alive tone: 40 Hz at 0.1% gain (0.001, ~-60 dBFS) summed onto
// BOTH channels at the DEVICE layer -- every frame, silence or not, underrun
// or not, phase-continuous. Rendered from a precomputed cycle table (1200
// samples = exactly one 40 Hz cycle @ 48 kHz) instead of calling sin() per
// frame. v7.1.7: BUILT IN -- the ini keys and UI rows are gone. v7.1.12:
// retuned from 1 Hz @ 0.0001 (~-80 dBFS) to the field's 40 Hz @ 0.001 --
// the old wobble was too little for some amps' sleep timers, and 40 Hz is
// still below what any small center driver reproduces at -60 dBFS.
// POST-MIX by construction: the tone is summed in the leg2 OUTPUT callbacks,
// after the mix, so it can never enter the capture stream an OBS
// display-capture records.
static constexpr double   kTwoPi             = 6.283185307179586;
static constexpr unsigned kKeepAliveTableMax = 48000;  // headroom (40 Hz needs 1200)
static constexpr float    kKeepAliveHz       = 40.0f;   // v7.1.12: was 1 Hz
static constexpr float    kKeepAliveGain     = 0.001f;  // v7.1.12: was 0.0001
static float              g_kaHz   = kKeepAliveHz;
static float              g_kaGain = kKeepAliveGain;
static unsigned           g_kaLen  = 48000;   // samples per cycle: round(48000 / g_kaHz)
static float              g_kaTable[kKeepAliveTableMax];
static unsigned           g_kaIdx  = 0;      // owned by leg2's device thread only

// v6.0 ASIO-STRICT clock discipline: bit-exact memcpy; clock drift is
// absorbed at the buffer switch, never by a rate change -- SHORTFALL replays
// the last buffer (ASIO convention), EXCESS (sawtooth top + margin) drops the
// oldest excess, at most once per kCorrMinIntervalMs per leg.
static size_t             g_capBurstFrames    = 240;    // real capture burst
static size_t             g_periodFrames[2]   = {480, 480}; // real device period per leg
static constexpr unsigned kCorrMinIntervalMs  = 5000;   // gated drop spacing
static constexpr unsigned kReplayMaxPeriods   = 4;      // replay buffer size (periods)
static constexpr size_t   kRingSpareFrames    = 2048;   // out ring headroom (passive evict line)
static constexpr size_t   kPeriodMarginFrames = 192;    // 4 ms above the FULL device buffer (WASAPI legs)
// v6.3.1: per-transport margin. A KS leg's pull is our own pacing thread --
// exactly one bite, never merged -- so 1 ms of pump jitter on top of
// (bite + burst) is enough. WASAPI keeps the 4 ms: miniaudio's exclusive
// event mode pops the WHOLE buffer as two back-to-back callbacks.
static constexpr size_t   kKsMarginFrames     = 48;     // 1 ms above (bite + burst) on a KS leg

// v6.4 drift warden pacing: a correction never fires more than once per
// drift_interval_ms (v6.5.8; was the hardcoded 30 s), only after the drift
// error held for 3 consecutive health windows, and a deliberate pad never
// exceeds 100 ms of in-place replay.
static constexpr unsigned kWardenStreakNeeded  = 3;
static constexpr size_t   kWardenMaxPadFrames  = 4800;   // 100 ms @ 48 kHz

// v7.1.14 FIRST-SECOND FAST TIER: the drift corrector at 10x the warden's
// cadence, for the first 1000 ms of a run only. Same trigger distance the
// warden uses (drift_correct_ms), same bit-exact skip/replay-pad table --
// ten times the speed: 100 ms ledger samples (the health tick is 1000 ms),
// a 3-window decision (300 ms instead of 3 s), 500 ms spacing (instead of
// drift_interval_ms). Hardcoded: no ini key, no UI.
static constexpr unsigned kFastTierMs        = 1000;  // the fast lane's whole life
static constexpr unsigned kFastTierPollMs    = 100;   // ledger sample cadence (10x the tick)
static constexpr unsigned kFastTierStreak    = 3;     // fast windows before a fire
static constexpr unsigned kFastTierSpacingMs = 500;   // drift_interval_ms / 10

struct Config {
    float       out1Ms         = 5.0f;
    float       out2Ms         = 5.0f;
    float       ring1Ms        = 10.0f;   // v7.0.0: default 10 ms (was -1 = follow outXms)
    float       ring2Ms        = 10.0f;   // v7.0.0: keep EQUAL to ring1 for alignment
    float       legacyBufferMs = 10.0f;   // only used if outXms absent from ini
    float       capMs          = 5.0f;    // v3.6: capture burst period (decoupled)
    bool        captureExclusive = true;  // v6.6.8: INTEGRATED -- always on
    bool        haveOut1       = false;
    bool        haveOut2       = false;
    bool        exclusive      = true;    // v6.6.8: INTEGRATED -- always on (v3 default was off)
    bool        alignModes     = true;    // v6.6.8: INTEGRATED -- always on
    bool        lfeFold        = true;    // v3.1: user-confirmed routing (LFE onto FL/FR)
    std::string inputDevice;
    std::string outputDevice1;
    std::string outputDevice2;
    std::string volumeEndpoint;      // v3.9: needle for the cable's PLAYBACK endpoint
                                     //       (volume follow); empty = use input_device
    float       gainL          = 1.0f;
    float       gainR          = 1.0f;
    float       gainLfe        = 0.4f;
    float       gainCenter     = 1.0f;
    // v6.5.18 CENTER DIALOGUE EQ: RBJ peaking biquad on ch3, out2 mix only,
    // default OFF (off = bit-exact single multiply). KS pins bypass every
    // APO, so the dialogue boost must live in the engine.
    bool        centerEq       = false;
    float       centerEqFreq   = 1800.0f; // peak frequency (Hz, clamped 20..20000)
    float       centerEqGain   = 4.0f;    // peak gain (dB, clamped +-12)
    float       centerEqQ      = 0.8f;    // peak Q (clamped 0.1..10)
    // v7.1.8 CENTER EQ SAVE SLOTS: the four built-in presets are retired --
    // five user slots persist in the ini (eq_slotN_on/freq/gain/q). Slot 1
    // ships reserved with the field's dialogue curve; freq <= 0 = empty slot.
    bool        eqSlotOn[5]    = { true, false, false, false, false };
    float       eqSlotF[5]     = { 387.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float       eqSlotG[5]     = { 3.5f,   0.0f, 0.0f, 0.0f, 0.0f };
    float       eqSlotQ[5]     = { 0.30f,  0.0f, 0.0f, 0.0f, 0.0f };
    bool        clockSync      = true;    // v6.0 device-master replay/drop discipline
                                          // (replaces v5.0's auto_resync / resampler)
                                          // v6.6.8: INTEGRATED -- always on
    float       delay1Ms       = 0.0f;    // v6.1: bit-exact ring delay per leg
    float       delay2Ms       = 0.0f;    //       (the EARLY leg takes the delay)
    bool        centerGate     = true;    // v6.1: ch3 digital-zero -> out2 keep-alive only
                                          //       v6.6.8: INTEGRATED -- always on
    bool        centerUpmixGuard = true;  // v6.1: ch3 == a*ch1+b*ch2 (Windows fills FC
                                          //       for stereo sources) -> block muted
                                          // v6.6.8: INTEGRATED -- always on
    bool        volumeFollow   = true;   // v3.8: Line 1 endpoint volume drives both legs
    bool        pinOutputs     = true;   // v3.8: out1/out2 endpoints pinned 100%/unmuted.
                                          // v7.0.0: INTEGRATED -- always on, no key, no pill
    bool        out2First      = true;   // v3.8: center leg opens and starts first
    // v7.1.7: keepalive_hz / keepalive_gain are BUILT IN (kKeepAliveHz /
    // kKeepAliveGain, 40 Hz @ 0.001 since v7.1.12) -- no struct fields, no
    // ini key, no UI.
    bool        background     = true;   // v6.2: detach the console after a
                                          //       successful start; log to file
                                          // v6.6.8: INTEGRATED -- always on
    bool        chimes         = true;   // v6.2: CenterFold signature, modernized
    float       chimeGain      = 0.4f;   // v6.2: chime blip peak (0..1)
    // v7.1.2 STARTUP ENGINE: a startup launch (--tray, the sign-in path) is
    // the boot path -- the user signs in and the splitter must simply be
    // running. Default ON: the engine starts by itself, exactly as if START
    // was pressed. off = the old behavior (START is yours to press).
    bool        startupEngine  = true;
    // v6.3 per-leg backend: "wasapi" = miniaudio exclusive WaveRT event
    // mode; "ks" = direct kernel-streaming render pin + looped WaveRT buffer
    // paced against the mapped play-position register (below the audio
    // engine, FlexASIO-grade). ks_period_frames = switch granularity.
    std::string backend1       = "wasapi";
    std::string backend2       = "wasapi";
    unsigned    ksPeriodFrames = 256;    // KS buffer-switch size (frames)
    unsigned    ksPeriods      = 3;      // WaveRT looped buffer = ksPeriods * period
    // v6.4 drift warden: the frame-interval ledger is ALWAYS on (it only
    // observes); drift_guard switches the automatic re-pin on top of it.
    bool        driftGuard     = true;   // auto re-pin at >= drift_correct_ms
                                          // v6.6.8: INTEGRATED -- always on
    float       driftCorrectMs = 5.0f;   // v7.0.0: default 5 ms (was 10) -- trigger distance from the baseline
    // v6.5.8: minimum spacing between warden corrections -- was the hardcoded
    // kWardenMinIntervalMs = 30000. Lower it for tighter pinning on
    // latency-critical playback; the price is each correction being an
    // audible skip/stutter of up to drift_correct_ms.
    unsigned    driftIntervalMs = 5000;   // v6.5.10: the five-second resync (was 30000)
    // v6.5.12: scheduled fresh reopen of the CENTER leg (minutes; 0 = off).
    // The S5's below-packetizer FIFO can deepen silently over hours -- no
    // host meter sees that layer; a scheduled reopen is the one reset that
    // re-primes it (brief gap, warden re-arms ~9 s).
    unsigned    deepResyncMin   = 0;
    // v6.5.14 DAC YIELD: if the Windows default render endpoint IS a leg
    // device, that leg's DAC is released so the default-playing app owns the
    // hardware; the leg reopens via the watchdog when the default moves.
    // v6.6.8: INTEGRATED -- always on.
    bool        yieldDefault    = true;   // v6.6.8: INTEGRATED -- always on (v6.5.17 default was off)
    // v6.5.2 power guard: suspend parks the pumps, resume rebuilds capture
    // + legs; a sentinel reopens any leg consuming beyond +-10% for 2 s (a
    // half-alive post-sleep stream ticks at a bogus rate with a FRESH
    // heartbeat the silence watchdog cannot see). v6.6.8: INTEGRATED.
    bool        powerGuard     = true;
};
static Config g_cfg;

// --------------------------------------------------- v6.2 chimes -----------
// The CenterFold signature, modernized: a rising fifth as two short triangle
// blips with fast decay (C7 42 ms -> G7 72 ms, 20 ms gap) instead of square
// horns. Rendered ONCE at startup; leg2's device thread sums it onto both
// out2 channels at the DEVICE layer (not volume-followed: a UI cue, not
// program audio). out1 never carries it; re-armed on watchdog reopen (3 s).
static std::vector<float> g_chimeBuf;                 // rendered once, read-only after
static std::atomic<int>      g_chimeSel{-1};          // -1 idle, 0 = playing
static std::atomic<unsigned> g_chimeIdx{0};           // leg2's device thread owns it
static unsigned long long    g_chimeLastArmMs = 0;    // reopen cooldown (main thread)

static std::vector<float> renderChime(double f1, double ms1, double f2, double ms2,
                                      double gapMs, float amp) {
    std::vector<float> v;
    v.reserve(8192);
    auto blip = [&](double f, double ms) {
        const size_t len  = (size_t)((double)kSampleRate * ms / 1000.0);
        const size_t atk  = (size_t)((double)kSampleRate * 0.0015);   // 1.5 ms attack
        const size_t tail = (size_t)((double)kSampleRate * 0.002);    // 2 ms fade-out
        const double dph  = kTwoPi * f / (double)kSampleRate;
        double ph = 0.0;
        for (size_t i = 0; i < len; ++i) {
            ph += dph;
            const double t = (double)i / (double)len;
            double g = pow(10.0, -2.2 * t);                    // ~-44 dB at note end
            if (i < atk)        g *= (double)i / (double)atk;
            if (i + tail > len) g *= (double)(len - i) / (double)tail;
            // triangle = arcsin(sin(x)) scaled: phase-continuous, starts at 0
            const double tri = (2.0 / 3.14159265358979) * asin(sin(ph));
            v.push_back((float)(amp * g * tri));
        }
    };
    blip(f1, ms1);
    for (size_t i = 0, n = (size_t)((double)kSampleRate * gapMs / 1000.0); i < n; ++i)
        v.push_back(0.0f);
    blip(f2, ms2);
    // normalize so the blip peak is EXACTLY `amp` (the decay curve pulls the
    // raw peak ~1 dB under it; chime_gain is documented as the true peak)
    float peak = 0.0f;
    for (const float s : v) { const float a = s < 0.0f ? -s : s; if (a > peak) peak = a; }
    if (peak > 0.0f && (peak < amp * 0.999f || peak > amp * 1.001f)) {
        const float k = amp / peak;
        for (float& s : v) s *= k;
    }
    return v;
}

static void armChime(bool force) {
    if (!g_cfg.chimes || g_chimeBuf.empty()) return;
    const unsigned long long now = GetTickCount64();
    if (!force && now - g_chimeLastArmMs < 3000) return;   // flapping-device guard
    g_chimeLastArmMs = now;
    g_chimeIdx.store(0, std::memory_order_relaxed);
    g_chimeSel.store(0, std::memory_order_release);
}

// ------------------------------------------------------------- SPSC ring ----
struct Ring {
    std::vector<float>     buf;
    size_t                 cap = 0, mask = 0, stride = 0;
    std::atomic<unsigned long long> head{0}, tail{0};
    // v6.4 frame-interval ledger: seqTail is this ring's stream position in
    // CAPTURE-sequence space (which capture frame the boundary sits at).
    // Advanced by every consumer of real frames (pops, drops, skips) with
    // the same monotonic CAS discipline as tail; NEVER by replays or pads.
    std::atomic<unsigned long long> seqTail{0};

    void init(size_t frames, size_t channels) {
        stride = channels;
        size_t c = 256; while (c < frames) c <<= 1;
        buf.assign(c * stride, 0.0f);
        cap = c; mask = c - 1;
        head.store(0); tail.store(0);
        // v7.0.1: the ledger re-arms with the ring on EVERY engine start -- a
        // stale seqTail carried the previous run's leftover and moved the 120
        // bpm tick's flam differently on every APPLY + RESTART.
        seqTail.store(0, std::memory_order_release);
    }
    size_t count() const {
        return (size_t)(head.load(std::memory_order_acquire) - tail.load(std::memory_order_acquire));
    }
    // producer: append n frames; if full, drop the oldest (amount returned).
    // v6.5: ONE wrap-aware bulk memcpy (two at the seam) instead of per-frame
    // copies -- same bytes, same head/tail evolution (harness-proven).
    size_t pushOverwrite(const float* src, size_t n) {
        const unsigned long long h = head.load(std::memory_order_relaxed);
        const unsigned long long t = tail.load(std::memory_order_acquire);
        const size_t used = (size_t)(h - t);
        if (n > cap) { src += (n - cap) * stride; n = cap; }
        if (used + n > cap) tailAdvance(h + n - cap);
        const size_t off = ((size_t)h & mask) * stride;
        const size_t cnt = n * stride;
        if (off + cnt <= cap * stride) {
            memcpy(&buf[off], src, cnt * sizeof(float));
        } else {
            const size_t a = cap * stride - off;
            memcpy(&buf[off], src, a * sizeof(float));
            memcpy(&buf[0], src + a, (cnt - a) * sizeof(float));
        }
        head.store(h + n, std::memory_order_release);
        return (used + n > cap) ? (used + n - cap) : 0;
    }
    // v6.0: forward-only tail advance. The consumer (pop/discardOldest) and
    // the producer's overwrite path BOTH advance tail; a plain store can
    // regress it when the two race (a regressed tail = phantom frames =
    // garbage audio). The CAS loop makes every advance strictly monotonic.
    void tailAdvance(unsigned long long want) {
        unsigned long long cur = tail.load(std::memory_order_relaxed);
        while (cur < want) {
            if (tail.compare_exchange_weak(cur, want, std::memory_order_release,
                                           std::memory_order_relaxed)) break;
        }
    }
    // v6.4: same monotonic discipline for the stream-position counter -- the
    // pump's pops and producer-side overwrites can race on it exactly like
    // on tail.
    void seqTailAdvance(unsigned long long want) {
        unsigned long long cur = seqTail.load(std::memory_order_relaxed);
        while (cur < want) {
            if (seqTail.compare_exchange_weak(cur, want, std::memory_order_release,
                                              std::memory_order_relaxed)) break;
        }
    }
    // consumer: pop up to n frames, returns how many. v6.5: bulk copy,
    // two memcpys at the wrap seam (see pushOverwrite).
    size_t pop(float* dst, size_t n) {
        const unsigned long long t = tail.load(std::memory_order_relaxed);
        const unsigned long long h = head.load(std::memory_order_acquire);
        size_t avail = (size_t)(h - t);
        if (n > avail) n = avail;
        if (n) {
            const size_t off = ((size_t)t & mask) * stride;
            const size_t cnt = n * stride;
            if (off + cnt <= cap * stride) {
                memcpy(dst, &buf[off], cnt * sizeof(float));
            } else {
                const size_t a = cap * stride - off;
                memcpy(dst, &buf[off], a * sizeof(float));
                memcpy(dst + a, &buf[0], (cnt - a) * sizeof(float));
            }
            tailAdvance(t + n);
        }
        return n;
    }
    // v6.0 consumer-side discard of the OLDEST n frames (gated excess drop).
    // Same thread discipline as pop() -- call from the playback callback only.
    size_t discardOldest(size_t n) {
        const unsigned long long t = tail.load(std::memory_order_relaxed);
        const unsigned long long h = head.load(std::memory_order_acquire);
        const size_t avail = (size_t)(h - t);
        if (n > avail) n = avail;
        if (n) {
            tailAdvance(t + n);
            seqTailAdvance(seqTail.load(std::memory_order_relaxed) + n);   // v6.4
        }
        return n;
    }
    // v6.5: bulk prefill in chunks -- a chunk must NEVER exceed cap:
    // pushOverwrite clamps n > cap by dropping from src, which would
    // silently lose the overshoot (the harness caught exactly that).
    void pushSilence(size_t n) {
        static const float z[kInCh * 512] = { 0 };
        const size_t chunkMax = cap < 512 ? cap : 512;
        while (n) {
            const size_t chunk = n < chunkMax ? n : chunkMax;
            pushOverwrite(z, chunk);
            n -= chunk;
        }
    }
};

// ------------------------------------------------- v6.3 WDM-KS output -------
// One KS output leg. KS replaces ONLY the device layer -- pumps, rings,
// gates and sync rules are shared. The WaveRT looped buffer is the driver's
// DMA memory mapped into this process; play position = mapped HW register
// (zero syscalls) or KSPROPERTY_AUDIO_POSITION.
struct KsOut {
    // resolved state (ksOpenLeg / ksCloseLeg, main thread)
    int           active      = 0;      // 1 = this leg runs on KS right now
    HANDLE        filter      = nullptr;// open wave filter (device interface)
    HANDLE        pin         = nullptr;// the created render pin
    int           pinId       = -1;     // which pin factory we instantiated
    unsigned char* loopBuf    = nullptr;// WaveRT looped buffer (driver-mapped)
    size_t        loopBytes   = 0;
    size_t        loopFrames  = 0;      // loopBytes / frameBytes
    unsigned      frameBytes  = 0;      // outCh * bytesPerSample
    int           outCh       = 2;
    int           fmt         = 0;      // 0=f32 1=pcm32 2=pcm24-in-32 3=pcm24 4=pcm16
    BOOL          barrier     = FALSE;  // driver asks for a barrier between writes
    volatile ULONG* posReg    = nullptr;// mapped HW play register (may be null)
    bool          posMapped   = false;
    unsigned      fifoFrames  = 0;      // HWLATENCY FifoSize, informational
    std::string   filterName;
    std::string   fmtName;
    // pacing state (ksThread-private once running)
    unsigned      periodFrames = 256; // write granularity == ks_period_frames
    unsigned      leadFrames   = 512; // keep the write cursor this far ahead
    unsigned long long playFrames = 0;   // monotonic frames consumed by the DAC
    unsigned long long writeFrames = 0;  // monotonic frames written
    unsigned      lastPosBytes = 0;
    bool          havePos     = false;
    unsigned long long posGlitch = 0;  // implausible register reads ignored
    unsigned long long wrote     = 0;  // frames handed to the DAC (diagnostics)
    // v6.5.13 lapse guard: wall-clock pacing bookkeeping (ksThread-private)
    // and the number of play-register lap losses re-pinned since open.
    LARGE_INTEGER   lastQpc  = {};
    bool            qpcArmed = false;
    std::atomic<unsigned long long> lapses{0};
    // v6.5.9: the LIVE write-minus-play lead (frames queued ahead of the DAC's
    // play cursor), stored by ksThread every pacing cycle -- the health loop
    // prints it in ms as the measured device buffering of a KS leg.
    std::atomic<unsigned long long> aheadFr{0};
    HANDLE        thread      = nullptr;
    std::vector<unsigned char> conv;   // format-conversion scratch (private)
    std::vector<float>         fill;   // one period of decoded audio (private)
};

// ------------------------------------------------------------------ legs ----
struct Leg {
    int                 index = 0;               // 0-based; printed as +1
    int                 outCh = kOutCh1;         // 6 for mains, 2 for center
    bool                zeroFast = false;        // v6.6.9: bit-zero blocks skip the mix (pump-frozen gate)
    Ring                raw;                     // 8ch: capture cb -> pump
    Ring                out;                     // outCh: pump -> playback cb
    size_t              targetFrames = 576;      // latency anchor (floor: FULL device buffer + burst + 4 ms)
    std::vector<float>  scratch;                 // pump-private 8ch work buffer
    std::vector<float>  chunk;                   // pump-private mix output block
    // v6.0 replay state (playback-callback owned): the last played buffer,
    // tiled into a shortfall -- the ASIO "replay the last buffer" convention.
    std::vector<float>  lastBlock;               // last played frames (outCh interleaved)
    size_t              lastCap = 0;             // frames lastBlock can hold
    size_t              lastN = 0;               // frames actually stored
    bool                haveLast = false;        // false until the first real buffer
    // v6.0 gated drop state (playback-callback owned)
    unsigned long long  lastCorrMs = 0;          // last excess-drop timestamp (0 = never)
    ma_device           dev = {};
    ma_device_id        id = {};                 // WASAPI endpoint id (volume pin)
    bool                isExclusive = false;
    std::string         name;

    std::atomic<unsigned long long> rawDrop{0}, und{0}, outDrop{0};
    // v6.0 clock telemetry: captured = frames the cable delivered (cable
    // clock); popped = frames the playback callback consumed (device clock).
    // The 1 s health loop turns the pair into the measured offset ("off").
    std::atomic<unsigned long long> captured{0}, popped{0};
    std::atomic<unsigned long long> corrDrops{0};       // gated excess-drop events
    // v6.1 alignment delay (part of targetFrames -- kept separately for the
    // banner) and center-presence gate state (pump-2 thread owned):
    size_t              delayFrames = 0;         // extra ring depth = delay_ms * 48
    float               delayMs     = 0.0f;      // as configured (banner)
    std::atomic<unsigned long long> cmUpmix{0}; // blocks muted by the upmix guard
    size_t              upmixHoldFrames = 0;     // keep muting small blocks after a hit
    unsigned long long  lastGateLogMs = 0;       // rate-limited gate/guard logs
    unsigned long long  lastUpmixLogMs = 0;
    bool                centerWasSilent = false; // for state-change logging
    // v6.3: which device layer this leg runs on. 0 = WASAPI (miniaudio),
    // 1 = WDM-KS (direct pin; L.ks holds the state). The ini asks for it
    // (backend1/backend2); a KS open failure leaves the WASAPI path in place.
    int                 backend = 0;
    KsOut               ks;
    // v6.5.14: this leg's DAC is released to the Windows default render
    // device (yield_default): device closed, pump parked; the watchdog,
    // collapse sentinel, stall resync, deep resync and warden all skip it.
    // Cleared + reopened through the watchdog path when the default moves.
    std::atomic<bool>   yielded{false};
    // ---- v6.4 DRIFT WARDEN: frame-interval ledger ---------------------------
    // The OUT ring's seqTail IS this leg's stream position (the capture
    // sequence of its play boundary). Pops/drops/skips advance it; replays
    // and pads do not. The two legs' difference = the measured inter-speaker
    // offset. The pump transfers raw-ring sequence space, the device thread
    // owns the play boundary, the health loop owns the warden decisions.
    size_t              prefillRemaining = 0;        // boot prefill: carries no sequence
    std::atomic<size_t> padFrames{0};                // deliberate replay pad (warden)
    std::atomic<unsigned long long> padTotal{0};     // frames padded (quiet-window check)
    std::atomic<unsigned long long> wardenDrops{0};  // warden skip events
    std::atomic<unsigned long long> wardenPads{0};   // warden pad events
    std::atomic<double> lagMs{0.0};                  // age of the newest played frame (ms)
    // v7.1.5 START ALIGN: the QPC microsecond stamp of this leg's FIRST buffer
    // switch of the run -- the moment THIS device began eating its ring. The
    // two legs' difference is the real start stagger; the start sequence pads
    // the early leg by exactly that gap so both streams open together.
    std::atomic<unsigned long long> firstSwitchUs{0};
    bool                streamSeeded = false;        // pump: out ring's seq space armed
    unsigned long long  lastReopenMs = 0;            // warden pauses 10 s after a reopen
};
static Leg g_leg[2];

static std::atomic<bool> g_run{true};
static std::atomic<bool> g_stopSilence{false};  // v7.0.0: the stop gap emits silence

// ------------------------------------------------------------ v3.8 volume --
// VOLUME FOLLOW: the Line 1 playback endpoint's Windows volume is the single
// master knob. A 25 ms watcher polls it; the pumps scale every captured
// frame before mixing, so both legs track identically and the out1/out2
// sliders stay pinned at 100%. The out2 keep-alive is summed at the device
// layer and is deliberately NOT scaled. GUIDs spelled out: zig c++ (GNU
// mode) has no reliable __uuidof.
static const CLSID kCLSID_MMDeviceEnumerator =
    { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const IID kIID_IMMDeviceEnumerator =
    { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const IID kIID_IAudioEndpointVolume =
    { 0x5CDF2C82, 0x841E, 0x4546, { 0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A } };
static const PROPERTYKEY kPKEY_Device_FriendlyName =
    { { 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } }, 14 };
// v6.5.16: yield re-grab guard -- active-session probe on a leg endpoint.
// (Only IAudioSessionManager2's IID is needed: dev->Activate() takes an IID;
// the enumerator/control objects come back from plain method calls.)
static const IID kIID_IAudioSessionManager2 =
    { 0x77AA99A0, 0x1BD6, 0x484F, { 0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F } };

static std::atomic<float>    g_vol{1.0f};             // Line 1 endpoint scalar (0..1)
static std::atomic<bool>     g_volMuted{false};
static std::atomic<bool>     g_volFollowLive{false};  // source endpoint resolved + reading
static IAudioEndpointVolume* g_srcVol = nullptr;      // Line 1 playback side
static std::wstring          g_srcVolName;            // for the banner

struct OutPin {
    std::wstring          id;                       // endpoint id the pin came from
    IAudioEndpointVolume* vol = nullptr;
};
static OutPin g_pin[2];                               // out1/out2 pins (watcher-owned)

// v3.8 watchdog heartbeats: each device callback refreshes its timestamp;
// the health loop reopens any thread that goes silent >5 s.
static std::atomic<unsigned long long> g_hbCap{0}, g_hbOut0{0}, g_hbOut1{0};
// v6.5.8: set by every successful device reopen -- the health loop invalidates
// and re-arms the warden's baseline, because a reopen can legitimately change
// host-side latency and a boot-time baseline would then "correct" toward a
// number that no longer describes the path.
static std::atomic<bool> g_driftRecalReq{false};
static unsigned long long g_bootMs = 0;
// v6.4: QPC anchors the lag display (age of the newest played frame in
// cable time, ms). The drift metric itself needs no clock at all: it is
// the difference of the two legs' stream positions, pure frame counts.
static LARGE_INTEGER g_qpcBoot = {};
static double        g_qpf     = 0.0;
static inline void hbOut(int i) {
    (i ? g_hbOut1 : g_hbOut0).store(GetTickCount64(), std::memory_order_relaxed);
}
static inline unsigned long long hbOutGet(int i) {
    return (i ? g_hbOut1 : g_hbOut0).load(std::memory_order_relaxed);
}

// capture-side restart state (filled by main before start; used by watchdog)
static ma_device g_capDev = {};
static std::string g_capNeedle;
static bool    g_capWantExcl = false;
static bool    g_capIsExcl   = false;
static float   g_capPeriodMsF = 5.0f;

// MMCSS boost for the pump threads (v3.8): at 10 ms targets a normal-
// priority thread that loses one scheduling quantum can miss a refill
// window. "Pro Audio" role + time-critical priority keeps the pumps ahead.
// avrt.dll is loaded dynamically -- no new import-lib dependency.
static void boostAudioThread() {
    // v6.5: FTZ/DAZ on every realtime thread -- the standard RT denormal
    // guard. Only touches f32 values below ~-370 dBFS; what it buys is that
    // a 100+ cycle denormal microcode assist can never stall the path when
    // volume_follow multiplies quiet content into the denormal range.
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    static HMODULE av = LoadLibraryW(L"avrt.dll");
    if (av) {
        typedef HANDLE (WINAPI *AvSetMmFn)(LPCWSTR, LPDWORD);
        AvSetMmFn set = (AvSetMmFn)GetProcAddress(av, "AvSetMmThreadCharacteristicsW");
        if (set) { DWORD idx = 0; set(L"Pro Audio", &idx); }
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
}

// v4.0: miniaudio's own device threads (capture + both playback callbacks)
// only get THREAD_PRIORITY from miniaudio -- no MMCSS registration. Register
// "Pro Audio" on each device thread once (thread_local guard: one
// registration per thread, zero cost in the hot path afterwards).
static void boostDeviceThread() {
    thread_local bool done = false;
    if (done) return;
    done = true;
    boostAudioThread();
}

// v5.0: ONE locked logger. v4.0 printed servo/recovery lines to stderr and
// the health line to stdout from six threads; the two streams interleaved
// mid-line in the console (the garbled paste). Everything funnels through
// here now: one critical section, one stream, ASCII only.
static CRITICAL_SECTION g_logCS;
static bool g_logToFile = false;   // v6.2: stdout is twinspeak.log (background = on)

// v6.6.0: GUI log tap. When the control window owns the process, every tlog
// line is handed to this sink INSTEAD of stdout (the console is detached and
// the pane is the log). Set once by guiMain before anything else logs.
static void (*g_logSink)(const char* line) = nullptr;

static void tlog(const char* fmt, ...) {
    va_list ap;
    EnterCriticalSection(&g_logCS);
    if (g_logSink) {
        char buf[8192];
        va_start(ap, fmt);
        const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n >= 0 && n < (int)sizeof(buf)) {
            g_logSink(buf);
        } else {
            // one very long line (the banner essays can exceed 8 KB)
            std::string big((size_t)(n > 0 ? n : 8191) + 1, '\0');
            va_start(ap, fmt);
            vsnprintf(&big[0], big.size(), fmt, ap);
            va_end(ap);
            g_logSink(big.c_str());
        }
    } else {
        va_start(ap, fmt);
        vfprintf(stdout, fmt, ap);
        va_end(ap);
        fflush(stdout);
    }
    LeaveCriticalSection(&g_logCS);
}

static std::wstring narrowToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::wstring wLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower((wint_t)c);
    return s;
}
static bool wContainsCI(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return true;
    return wLower(hay).find(wLower(needle)) != std::wstring::npos;
}

static IAudioEndpointVolume* endpointVolFromIdString(LPCWSTR idStr) {
    if (!idStr || !idStr[0]) return nullptr;
    IAudioEndpointVolume* out = nullptr;
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                kIID_IMMDeviceEnumerator, (void**)&en)) || !en)
        return nullptr;
    IMMDevice* dev = nullptr;
    if (SUCCEEDED(en->GetDevice(idStr, &dev)) && dev) {
        dev->Activate(kIID_IAudioEndpointVolume, CLSCTX_ALL, nullptr, (void**)&out);
        dev->Release();
    }
    en->Release();
    return out;
}

// the render (playback) side of the VAC cable, matched by name against
// input_device -- its IAudioEndpointVolume IS "VAC's volume".
static IAudioEndpointVolume* findRenderEndpointVolume(const std::string& nameNeedle,
                                                      std::wstring* outName) {
    IAudioEndpointVolume* result = nullptr;
    const std::wstring wNeedle = narrowToWide(nameNeedle);
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                kIID_IMMDeviceEnumerator, (void**)&en)) || !en)
        return nullptr;
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
        UINT n = 0;
        if (SUCCEEDED(col->GetCount(&n))) {
            for (UINT i = 0; i < n && !result; ++i) {
                IMMDevice* dev = nullptr;
                if (FAILED(col->Item(i, &dev)) || !dev) continue;
                IPropertyStore* ps = nullptr;
                if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps)) && ps) {
                    PROPVARIANT var; PropVariantInit(&var);
                    if (SUCCEEDED(ps->GetValue(kPKEY_Device_FriendlyName, &var)) &&
                        var.vt == VT_LPWSTR && var.pwszVal) {
                        if (wContainsCI(var.pwszVal, wNeedle)) {
                            const std::wstring nm = var.pwszVal;
                            if (SUCCEEDED(dev->Activate(kIID_IAudioEndpointVolume, CLSCTX_ALL,
                                                        nullptr, (void**)&result)) && result) {
                                if (outName) *outName = nm;
                            }
                        }
                    }
                    PropVariantClear(&var);
                }
                if (ps) ps->Release();
                dev->Release();
            }
        }
        col->Release();
    }
    en->Release();
    return result;
}

// v3.9: when the volume endpoint cannot be matched, show the user EVERY
// render endpoint Windows has, with its state, so the exact playback-side
// name of the cable is visible and can go into volume_endpoint.
static void logRenderEndpoints() {
    tlog( "[twinspeak] volume        : render endpoints on this system (any state):\n");
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                kIID_IMMDeviceEnumerator, (void**)&en)) || !en) {
        tlog( "[twinspeak] volume        :   (COM enumeration unavailable)\n");
        return;
    }
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATEMASK_ALL, &col)) && col) {
        UINT n = 0;
        if (SUCCEEDED(col->GetCount(&n))) {
            for (UINT i = 0; i < n; ++i) {
                IMMDevice* dev = nullptr;
                if (FAILED(col->Item(i, &dev)) || !dev) continue;
                DWORD st = 0;
                dev->GetState(&st);
                const char* stn = (st == DEVICE_STATE_ACTIVE)     ? "active"   :
                                  (st == DEVICE_STATE_DISABLED)   ? "DISABLED" :
                                  (st == DEVICE_STATE_NOTPRESENT) ? "notpres"  : "unplugged";
                char nmA[256] = "(unnamed)";
                IPropertyStore* ps = nullptr;
                if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps)) && ps) {
                    PROPVARIANT var; PropVariantInit(&var);
                    if (SUCCEEDED(ps->GetValue(kPKEY_Device_FriendlyName, &var)) &&
                        var.vt == VT_LPWSTR && var.pwszVal)
                        WideCharToMultiByte(CP_UTF8, 0, var.pwszVal, -1,
                                            nmA, (int)sizeof(nmA), nullptr, nullptr);
                    PropVariantClear(&var);
                    ps->Release();
                }
                tlog( "[twinspeak] volume        :   [%-8s] \"%s\"\n", stn, nmA);
                dev->Release();
            }
        }
        col->Release();
    } else {
        tlog( "[twinspeak] volume        :   (enumeration failed)\n");
    }
    en->Release();
    tlog( "[twinspeak] volume        : to use one of these, put its exact name into\n"
                    "            volume_endpoint = <name>   in the ini (the endpoint must be\n"
                    "            ACTIVE in Windows sound settings for the slider to move).\n");
}

// every 2 s from the watcher: out1/out2 endpoints must sit at 100% unmuted.
// Also re-resolves a pin whose endpoint id changed (watchdog reopen).
static void pinCheckTick() {
    if (!g_cfg.pinOutputs) return;
    // v3.9: this runs on the watcher thread and no longer touches Leg fields
    // -- tryRestartLeg (main thread) re-pins after a reopen, so the two
    // threads never race on the same Leg state.
    for (int i = 0; i < 2; ++i) {
        // v6.5.15: a yielded leg's device IS the Windows default render
        // endpoint -- its volume slider belongs to the user, not to us.
        // (The yield release already drops the handle; this skip is the
        // belt to that suspenders.)
        if (g_leg[i].yielded.load(std::memory_order_relaxed)) continue;
        IAudioEndpointVolume* v = g_pin[i].vol;
        if (!v) continue;
        float s = 0.0f; BOOL m = FALSE;
        if (SUCCEEDED(v->GetMasterVolumeLevelScalar(&s)) && SUCCEEDED(v->GetMute(&m))) {
            if (m != FALSE || s < 0.999f) {
                v->SetMasterVolumeLevelScalar(1.0f, nullptr);
                v->SetMute(FALSE, nullptr);
                static unsigned long long lastPinLog[2] = { 0, 0 };
                const unsigned long long now = GetTickCount64();
                if (now - lastPinLog[i] >= 5000) {
                    tlog( "[twinspeak] out%d: endpoint volume drifted to %.0f%%%s -- re-pinned to 100%%/unmuted\n",
                            i + 1, s * 100.0f, (m != FALSE) ? " MUTED" : "");
                    lastPinLog[i] = now;
                }
            }
        }
    }
}

static DWORD WINAPI volumeWatchThread(LPVOID) {
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    unsigned tick = 0;
    while (g_run.load()) {
        Sleep(25);
        if (!g_run.load()) break;
        if (g_srcVol) {
            float v = 1.0f; BOOL m = FALSE;
            if (SUCCEEDED(g_srcVol->GetMasterVolumeLevelScalar(&v)))
                g_vol.store(v, std::memory_order_relaxed);
            if (SUCCEEDED(g_srcVol->GetMute(&m)))
                g_volMuted.store(m != FALSE, std::memory_order_relaxed);
            g_volFollowLive.store(true, std::memory_order_relaxed);
        }
        if (++tick % 80 == 0) pinCheckTick();   // ~2 s
    }
    if (com) CoUninitialize();
    return 0;
}

// --------------------------------------------------- v6.5.2 POWER GUARD ----
// The adaptive-power layer. g_powerPark is read by the pumps and the
// health loop; the window thread only logs and flips flags -- every
// reopen stays on the health-loop thread that owns the devices.
static std::atomic<bool> g_powerPark{false};    // PBT_APMSUSPEND: pumps idle
static std::atomic<bool> g_powerWoken{false};   // resume: health loop rebuilds
static constexpr long kCollapsePpm = 100000;    // +-10%: 25x the worst healthy
                                                // wobble seen in the field log

static LRESULT CALLBACK powerWndProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    (void)w; (void)lp;
    if (m == WM_POWERBROADCAST) {
        if (wp == PBT_APMSUSPEND) {
            if (!g_powerPark.exchange(true))
                tlog("[twinspeak] power guard : system suspend -- parking the pumps (audio resumes on wake)\n");
        } else if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
            // RESUMESUSPEND trails RESUMEAUTOMATIC on user-initiated
            // wakes; the health loop consumes the flag once, so raising
            // it twice is harmless.
            g_powerPark.store(false, std::memory_order_relaxed);
            g_powerWoken.store(true, std::memory_order_relaxed);
        }
        return TRUE;
    }
    return DefWindowProcW(w, m, wp, lp);
}

static DWORD WINAPI powerThread(LPVOID) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = powerWndProc;
    wc.lpszClassName = L"twinspeak_power_guard";
    wc.hInstance     = GetModuleHandleW(nullptr);
    if (!RegisterClassExW(&wc)) return 0;
    // style 0 = never visible; a NORMAL top-level window so broadcast
    // WM_POWERBROADCAST reaches it (message-only windows get nothing).
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"", 0,
                             0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    if (!w) return 0;
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}

// ------------------------------------------------------------- callbacks ----
// capture callback: dumb fan-out. The less work happens here, the better --
// each pump thread does its own leg's mixing, on its own thread, in parallel.
static void capCallback(ma_device*, void*, const void* pInput, ma_uint32 frameCount) {
    boostDeviceThread();                                          // v4.0 MMCSS
    g_hbCap.store(GetTickCount64(), std::memory_order_relaxed);   // v3.8 watchdog
    if (!pInput || frameCount == 0) return;
    const float* in = (const float*)pInput;
    for (int i = 0; i < 2; ++i) {
        size_t dropped = g_leg[i].raw.pushOverwrite(in, frameCount);
        if (dropped) {
            g_leg[i].rawDrop.fetch_add(dropped);
            // v6.4: an overwrite silently consumed the oldest frames -- their
            // sequence numbers are skipped with them (the stream is now that
            // much fresher; the ledger must see the same skip).
            g_leg[i].raw.seqTailAdvance(g_leg[i].raw.seqTail.load(std::memory_order_relaxed)
                                        + dropped);
        }
        g_leg[i].captured.fetch_add(frameCount, std::memory_order_relaxed); // v6.0 off-meter (cable clock)
    }
}

// v6.0 playback core -- the ASIO buffer switch, bit-exact, ONE code path for
// both backends (WASAPI callback and KS pacing thread both land here):
//   1) EXCESS -> GATED DROP: level a full period past the sawtooth top
//      (target + burst) means the cable clock is faster -- discard the
//      OLDEST excess, at most once per kCorrMinIntervalMs, logged.
//   2) the switch: pop the ring; a shortfall REPLAYS the last buffer (ASIO
//      convention), never a silence hole, never a rate change.
//   3) remember the buffer just played; leg2 sums the keep-alive at the
//      device layer. frameCount = the REAL device bite (period or KS bite).
static void bufferSwitchFill(int idx, float* out, size_t frameCount) {
    Leg& L = g_leg[idx];
    // v7.0.0: the STOP GAP. APPLY + RESTART (and STOP) silences the legs
    // BEFORE the devices close: from the moment the engine begins its
    // shutdown the callbacks emit literal silence instead of the stream
    // tail, so a restart never spits the last program audio back out.
    if (g_stopSilence.load(std::memory_order_relaxed)) {
        if (frameCount) memset(out, 0, (size_t)frameCount * L.outCh * sizeof(float));
        return;
    }
    // v7.1.5 START ALIGN: stamp the FIRST switch of this run before the pop
    // counter moves (release store, then the relaxed fetch_add -- the start
    // sequence polls popped and can rely on seeing the stamp). WASAPI: the
    // first callback after Start; KS: the pacing thread's first period write,
    // right after its RUN flip. Either way: when THIS DAC began eating.
    if (L.popped.load(std::memory_order_relaxed) == 0 && g_qpf > 0.0) {
        LARGE_INTEGER fsw;
        QueryPerformanceCounter(&fsw);
        L.firstSwitchUs.store((unsigned long long)((double)fsw.QuadPart * 1e6 / g_qpf),
                              std::memory_order_release);
    }
    L.popped.fetch_add((unsigned long long)frameCount, std::memory_order_relaxed);    // device clock

    // v6.4: a deliberate warden pad repeats the last played buffer IN PLACE
    // (the ASIO replay convention) -- the stream slides later, no ring frame
    // is consumed, and "und" stays honest (counted separately).
    size_t padFront = 0;
    {
        const size_t p = L.padFrames.load(std::memory_order_relaxed);
        if (p && frameCount) {
            padFront = p < frameCount ? p : frameCount;
            L.padFrames.store(p - padFront, std::memory_order_relaxed);
            L.padTotal.fetch_add(padFront, std::memory_order_relaxed);
        }
    }
    // replay/silence filler shared by the warden pad (front) and the
    // shortfall path (tail): rewind-repeat the LAST played frames, or
    // silence before the first real buffer ever played.
    auto replayFill = [&](float* dst, size_t miss) {
        if (g_cfg.clockSync && L.haveLast && L.lastN) {
            if (miss <= L.lastN) {
                memcpy(dst, L.lastBlock.data() + (L.lastN - miss) * L.outCh,
                       miss * L.outCh * sizeof(float));
            } else {
                for (size_t i = 0; i < miss; ++i) {
                    const float* src = L.lastBlock.data() + (i % L.lastN) * L.outCh;
                    memcpy(dst + i * L.outCh, src, L.outCh * sizeof(float));
                }
            }
        } else {
            memset(dst, 0, miss * L.outCh * sizeof(float));
        }
    };

    const size_t periodFr = g_periodFrames[idx];
    if (g_cfg.clockSync && periodFr) {
        const long long level = (long long)L.out.count();
        const long long line  = (long long)(L.targetFrames + g_capBurstFrames + periodFr);
        if (level > line) {
            const unsigned long long now = GetTickCount64();
            if (now - L.lastCorrMs >= kCorrMinIntervalMs) {
                const size_t dropped = L.out.discardOldest((size_t)(level - (long long)L.targetFrames));
                L.lastCorrMs = now;
                L.corrDrops.fetch_add(1, std::memory_order_relaxed);
                // v6.4: the discarded frames' sequence numbers are consumed
                // with them (discardOldest advanced the ring's seqTail).
                tlog("[twinspeak] leg%d sync     : dropped %llu fr of excess%s -- cable clock faster than this device\n",
                     idx + 1, (unsigned long long)dropped,
                     (now - g_bootMs < 8000) ? " (startup settle: pre-playback backlog trim)" : "");
            }
        }
    }

    size_t n = 0;
    if (frameCount > padFront)
        n = L.out.pop(out + padFront * L.outCh, frameCount - padFront);
    // v6.4: advance the play boundary by the REAL frames just played. The
    // boot prefill (pushSilence) carries no sequence -- it is subtracted
    // here exactly once per frame, so the boundary lives in true capture-
    // sequence space and the lag/drift math stays absolute.
    {
        size_t real = n;
        if (L.prefillRemaining) {
            const size_t p = real < L.prefillRemaining ? real : L.prefillRemaining;
            L.prefillRemaining -= p;
            real -= p;
        }
        if (real)
            L.out.seqTailAdvance(L.out.seqTail.load(std::memory_order_relaxed) + real);
    }
    if (n < frameCount - padFront) {
        L.und.fetch_add(1);
        const size_t miss = (size_t)frameCount - padFront - n;
        replayFill(out + (padFront + n) * L.outCh, miss);
    }
    if (padFront)
        replayFill(out, padFront);

    {
        const size_t keep = (size_t)frameCount < L.lastCap ? (size_t)frameCount : L.lastCap;
        if (keep) {
            memcpy(L.lastBlock.data(), out + ((size_t)frameCount - keep) * L.outCh,
                   keep * L.outCh * sizeof(float));
            L.lastN = keep;
            L.haveLast = true;
        }
    }

    // v6.4 lag display: the age of the newest played frame, in cable time.
    // Pure display -- the drift metric is the seqTail difference and needs
    // no clock at all.
    if (g_qpf > 0.0) {
        const unsigned long long st = L.out.seqTail.load(std::memory_order_relaxed);
        if (st) {
            LARGE_INTEGER c;
            QueryPerformanceCounter(&c);
            const double nowUs = (double)(c.QuadPart - g_qpcBoot.QuadPart) * 1e6 / g_qpf;
            const double capUs = (double)st * 1e6 / (double)kSampleRate;
            L.lagMs.store((nowUs - capUs) / 1000.0, std::memory_order_relaxed);
        }
    }
}

// v6.3: leg2's device-layer extras -- keep-alive tone + chime -- as their own
// function so the KS pump can sum them exactly like playCallback2 does.
static void sumLeg2Extras(float* out, ma_uint32 frameCount) {
    // keep-alive tone (+ armed chime) summed on top of whatever was just
    // written, phase-continuous via the cycle table, device-layer rule (not
    // volume-followed). v6.5: with no chime armed the tone is summed in
    // wrap-free RUNS over the table -- same sample sequence and clamps,
    // bit-exact (harness-proven).
    int chSel = g_chimeSel.load(std::memory_order_acquire);
    if (chSel < 0) {
        const unsigned len = g_kaLen;
        unsigned idx = g_kaIdx;
        ma_uint32 done = 0;
        while (done < frameCount) {
            const ma_uint32 run = (frameCount - done) < (ma_uint32)(len - idx)
                                ? (frameCount - done) : (ma_uint32)(len - idx);
            for (ma_uint32 i = 0; i < run; ++i, ++idx) {
                const float s = g_kaTable[idx];
                float l = out[(done + i) * 2 + 0] + s;
                float r = out[(done + i) * 2 + 1] + s;
                if (l >  1.0f) l =  1.0f; else if (l < -1.0f) l = -1.0f;
                if (r >  1.0f) r =  1.0f; else if (r < -1.0f) r = -1.0f;
                out[(done + i) * 2 + 0] = l;
                out[(done + i) * 2 + 1] = r;
            }
            done += run;
            if (idx >= len) idx -= len;
        }
        g_kaIdx = idx;
        return;
    }
    for (ma_uint32 i = 0; i < frameCount; ++i) {
        const float s = g_kaTable[g_kaIdx];
        if (++g_kaIdx >= g_kaLen) g_kaIdx = 0;
        float l = out[i * 2 + 0] + s;
        float r = out[i * 2 + 1] + s;
        if (chSel >= 0) {
            const unsigned ci = g_chimeIdx.fetch_add(1, std::memory_order_relaxed);
            if (ci < g_chimeBuf.size()) {
                const float c = g_chimeBuf[ci];
                l += c;
                r += c;
            } else {
                g_chimeSel.store(-1, std::memory_order_release);
                chSel = -1;
            }
        }
        if (l >  1.0f) l =  1.0f; else if (l < -1.0f) l = -1.0f;
        if (r >  1.0f) r =  1.0f; else if (r < -1.0f) r = -1.0f;
        out[i * 2 + 0] = l;
        out[i * 2 + 1] = r;
    }
}

static void playCore(int idx, float* out, ma_uint32 frameCount) {
    boostDeviceThread();
    hbOut(idx);   // v3.8 watchdog heartbeat
    bufferSwitchFill(idx, out, (size_t)frameCount);
}

static void playCallback1(ma_device*, void* pOutput, const void*, ma_uint32 frameCount) {
    playCore(0, (float*)pOutput, frameCount);
}

static void playCallback2(ma_device*, void* pOutput, const void*, ma_uint32 frameCount) {
    playCore(1, (float*)pOutput, frameCount);
    if (!g_stopSilence.load(std::memory_order_relaxed))   // v7.0.0: the stop gap is silence
        sumLeg2Extras((float*)pOutput, frameCount);
}

// ------------------------------------------- v6.5.18 center dialogue EQ ----
// RBJ peaking biquad on the center channel, out2 only. One filter serves
// both out2 channels (the mixer writes the SAME center value to L and R, so
// filter once before the fan-out). Coefficients computed ONCE at startup;
// transposed DF2 -- no division in the hot loop. Touched only by the leg2
// pump thread (both pump loops live in pumpThread, no locking).
struct CenterEq {
    bool    on     = false;
    float   f0     = 0.0f;   // clamped values, for the banner
    float   gainDb = 0.0f;
    float   q      = 0.0f;
    float   b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float   z1 = 0.0f, z2 = 0.0f;          // transposed DF2 state
};
static CenterEq g_ceq;

static void centerEqInit() {
    g_ceq.on = g_cfg.centerEq;
    if (!g_ceq.on) return;
    // clamp to sane bands so a typo in the ini cannot nuke the coefficients
    const float f0  = g_cfg.centerEqFreq < 20.0f    ? 20.0f
                    : g_cfg.centerEqFreq > 20000.0f ? 20000.0f
                    : g_cfg.centerEqFreq;
    const float gdb = g_cfg.centerEqGain < -12.0f   ? -12.0f
                    : g_cfg.centerEqGain > 12.0f    ? 12.0f
                    : g_cfg.centerEqGain;
    const float qv  = g_cfg.centerEqQ < 0.1f        ? 0.1f
                    : g_cfg.centerEqQ > 10.0f       ? 10.0f
                    : g_cfg.centerEqQ;
    // RBJ audio-EQ-cookbook peaking section
    const double A     = pow(10.0, (double)gdb / 40.0);
    const double w0    = kTwoPi * (double)f0 / (double)kSampleRate;
    const double alpha = sin(w0) / (2.0 * (double)qv);
    const double b0 = 1.0 + alpha * A, b1 = -2.0 * cos(w0), b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A, a1 = -2.0 * cos(w0), a2 = 1.0 - alpha / A;
    g_ceq.f0 = f0; g_ceq.gainDb = gdb; g_ceq.q = qv;
    g_ceq.b0 = (float)(b0 / a0); g_ceq.b1 = (float)(b1 / a0);
    g_ceq.b2 = (float)(b2 / a0); g_ceq.a1 = (float)(a1 / a0);
    g_ceq.a2 = (float)(a2 / a0);
    g_ceq.z1 = g_ceq.z2 = 0.0f;
}

// ----------------------------------------------------------------- mixer ----
// v6.5: whole-BLOCK mix with the ini gains hoisted into locals. Every
// per-frame FP op is unchanged (multiply + add, never FMA -- -ffp-contract
// off) so the mix is bit-identical to v6.4 (harness-proven, including the
// IEEE -0 + +0 = +0 behavior of the discrete-LFE branch: the "+ 0.0f" stays).
static void mixBlock(int idx, const float* sc, float* chunk, size_t n) {
    if (idx == 0) {
        // leg1 = NATIVE 5.1 stream. FC is DRIVEN SILENCE: the endpoint sees a
        // full-layout 6ch stream, so neither miniaudio nor the Windows engine
        // may run a stereo->5.1 upmix -- the fake FC = 0.5L+0.5R smear is gone.
        const float gL = g_cfg.gainL, gR = g_cfg.gainR, gLfe = g_cfg.gainLfe;
        if (g_cfg.lfeFold) {
            for (size_t i = 0; i < n; ++i) {
                const float* f = sc + i * kInCh;
                float* o = chunk + i * kOutCh1;
                const float lfe = gLfe * f[3];
                o[0] = gL * f[0] + lfe;                     // FL
                o[1] = gR * f[1] + lfe;                     // FR
                o[2] = 0.0f;                                // FC -- silent on purpose (center lives on leg2)
                o[3] = 0.0f;                                // LFE -- folded into the fronts
                o[4] = 0.0f;                                // SL
                o[5] = 0.0f;                                // SR
            }
        } else {
            for (size_t i = 0; i < n; ++i) {
                const float* f = sc + i * kInCh;
                float* o = chunk + i * kOutCh1;
                o[0] = gL * f[0] + 0.0f;                    // FL (the +0.0f preserves v6.4's -0 -> +0)
                o[1] = gR * f[1] + 0.0f;                    // FR
                o[2] = 0.0f;                                // FC
                o[3] = gLfe * f[3];                         // LFE -- discrete sub channel
                o[4] = 0.0f;                                // SL
                o[5] = 0.0f;                                // SR
            }
        }
    } else {
        // v6.0 EXPLICIT CENTER ISOLATION: the out2 copy of the block READS
        // ONLY ch3 -- every other channel is muted by never being touched,
        // so out2 can never be a clone of the program audio.
        const float gC = g_cfg.gainCenter;
        if (g_ceq.on) {
            // v6.5.18: peaking EQ on the center sample, BEFORE the gain. A
            // bit-zero ch3 block RESETS the filter instead of feeding it (a
            // biquad fed zeros still rings; the gate contract says a gated
            // block emits NOTHING). Nonzero blocks run every sample through
            // the filter, internal zeros included.
            bool allZero = true;
            for (size_t i = 0; i < n; ++i)
                if (sc[i * kInCh + 2] != 0.0f) { allZero = false; break; }
            if (allZero) {
                g_ceq.z1 = 0.0f;
                g_ceq.z2 = 0.0f;
                for (size_t i = 0; i < n; ++i) {
                    chunk[i * 2 + 0] = 0.0f;
                    chunk[i * 2 + 1] = 0.0f;
                }
            } else {
                for (size_t i = 0; i < n; ++i) {
                    const float x = sc[i * kInCh + 2];
                    const float y = g_ceq.b0 * x + g_ceq.z1;
                    g_ceq.z1 = g_ceq.b1 * x - g_ceq.a1 * y + g_ceq.z2;
                    g_ceq.z2 = g_ceq.b2 * x - g_ceq.a2 * y;
                    float c = gC * y;
                    // the boost can push past full scale; clamp here so the
                    // EQ mode is self-contained (the device layer would clip
                    // the same peak a moment later, after the keep-alive sum)
                    if (c >  1.0f) c =  1.0f; else if (c < -1.0f) c = -1.0f;
                    chunk[i * 2 + 0] = c;
                    chunk[i * 2 + 1] = c;
                }
            }
        } else {
            for (size_t i = 0; i < n; ++i) {
                const float c = gC * sc[i * kInCh + 2];
                chunk[i * 2 + 0] = c;
                chunk[i * 2 + 1] = c;
            }
        }
    }
}

// ------------------------------------------------- v6.1 center gates -------
// CENTER PRESENCE on out2 -- two gates in the leg2 pump, both default ON:
//   1) center_gate: ch3 digital zero for the whole block -> out2's copy
//      emits NOTHING (the keep-alive is summed later, at the device layer).
//   2) center_upmix_guard: a stereo app cannot write center to the 8ch
//      cable -- the Windows engine UPmixES it first, FC = a*FL + b*R. The
//      fingerprint is exact: per block, least-squares fit ch3 ~ a*ch1 +
//      b*ch2; residual < -30 dB with the gains inside the upmix band and
//      the fronts active -> the block is engine upmix, not center -> muted.
//      Genuine 5.1 dialogue is an INDEPENDENT track and always passes. A
//      hit arms a 50 ms hold over the sub-analysis blocks; any full-test
//      failure disarms it immediately (a false hit costs one block).
static void centerGuard(Leg& L, float* sc, size_t n) {
    // ---- gate 1: ch3 digital zero for the whole block ----------------------
    bool allZero = true;
    for (size_t i = 0; i < n; ++i) {
        if (sc[i * kInCh + 2] != 0.0f) { allZero = false; break; }
    }
    if (allZero) {
        if (g_cfg.centerGate) {
            if (!L.centerWasSilent) {
                L.centerWasSilent = true;
                const unsigned long long now = GetTickCount64();
                if (now - L.lastGateLogMs >= 5000) {
                    L.lastGateLogMs = now;
                    tlog("[twinspeak] out2 center gate : ch3 is digital zero -- out2 = keep-alive only\n");
                }
            }
        }
        return;                      // nothing left to classify on a silent block
    }
    L.centerWasSilent = false;

    // ---- gate 2: engine upmix fingerprint ----------------------------------
    if (!g_cfg.centerUpmixGuard) return;
    if (L.upmixHoldFrames > 0 && n < 64) {
        // sub-analysis-size block inside an armed hold: stay muted
        const size_t m = n < L.upmixHoldFrames ? n : L.upmixHoldFrames;
        for (size_t i = 0; i < m; ++i) sc[i * kInCh + 2] = 0.0f;
        L.upmixHoldFrames -= m;
        L.cmUpmix.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (n < 64) return;              // too few frames for a meaningful fit

    double S11 = 0, S22 = 0, S12 = 0, S31 = 0, S32 = 0, S33 = 0;
    for (size_t i = 0; i < n; ++i) {
        const float* f = sc + i * kInCh;
        const double x = f[0], y = f[1], z = f[2];
        S11 += x * x; S22 += y * y; S12 += x * y;
        S31 += z * x; S32 += z * y; S33 += z * z;
    }
    const double nD = (double)n;
    const double ms1 = S11 / nD, ms2 = S22 / nD, ms3 = S33 / nD;
    if (ms3 < 1e-8) return;          // ch3 ~ -80 dBFS: effectively silent
    if (0.5 * (ms1 + ms2) < 4e-8) return; // fronts ~ -74 dBFS: nothing to attribute against

    double a = 0.0, b = 0.0, res2 = 0.0;
    const double det = S11 * S22 - S12 * S12;
    if (det > 1e-6 * (S11 * S22 + 1e-30)) {
        a = (S31 * S22 - S32 * S12) / det;
        b = (S32 * S11 - S31 * S12) / det;
        res2 = S33 - a * S31 - b * S32;
        if (a < 0.05 || a > 2.0 || b < 0.05 || b > 2.0) {
            L.upmixHoldFrames = 0;   // gains outside any upmix matrix: real center
            return;
        }
    } else {
        // collinear fronts (mono content): 1-D fit on the dominant front
        const bool useCh1 = (S11 >= S22);
        const double Suu = useCh1 ? S11 : S22;
        const double S3u = useCh1 ? S31 : S32;
        if (Suu <= 0.0) return;
        a = S3u / Suu;
        res2 = S33 - a * S3u;
        if (a < 0.05 || a > 2.0) {
            L.upmixHoldFrames = 0;
            return;
        }
    }
    if (res2 < 0.0) res2 = 0.0;      // float noise below the fit
    if (res2 / S33 < 1e-3) {         // < -30 dB residual: exact mix = engine upmix
        for (size_t i = 0; i < n; ++i) sc[i * kInCh + 2] = 0.0f;
        L.upmixHoldFrames = 2400;    // 50 ms of small-block cover
        L.cmUpmix.fetch_add(1, std::memory_order_relaxed);
        const unsigned long long now = GetTickCount64();
        if (now - L.lastUpmixLogMs >= 5000) {
            L.lastUpmixLogMs = now;
            tlog("[twinspeak] out2 upmix guard: ch3 matches %.2f*ch1 + %.2f*ch2 (residual < -30 dB) -- block muted\n"
                 "                            (stereo source: the Windows engine fills FC = a*FL + b*R at the cable)\n",
                 a, b);
        }
    } else {
        L.upmixHoldFrames = 0;       // independent center content: disarm
    }
}

// ---- v6.6.9: silence compression -------------------------------------------
// TRUE bit-zero test, two frames per word: every sample must be the LITERAL
// +0.0f bit pattern (a == 0.0f compare would also match -0.0f, whose mix
// output carries the sign). Scratch always holds whole ring frames, so the
// word count is exact; the loop bails at the first nonzero word.
static inline bool bitZero8(const float* p, size_t n) {
    unsigned long long w;
    const size_t nw = n * (size_t)(kInCh / 2);
    for (size_t i = 0; i < nw; ++i) {
        memcpy(&w, p + i * 2, sizeof(w));
        if (w) return false;
    }
    return true;
}

// ---- v7.0.0: the 120 BPM tempo tick (the sync test) ------------------------
// One sharp 7.5 ms burst every 500 ms, 8 beats = 4 s, rendered ONCE, RAW BY
// DESIGN: the pumps sum the SAME tick samples at the SAME capture-sequence
// position on BOTH legs -- no latency compensation -- so whatever each
// speaker's path adds is exactly what you hear: the flam IS the measurement.
// PLAY/STOP toggle; ends itself after 4 s. 1500 Hz, fast decay.
static std::atomic<bool>      g_tickOn{false};
static std::atomic<long long> g_tickSeq0{-1};      // capture-seq of tick beat zero
static std::atomic<unsigned long long> g_tickEndMs{0};   // wall time the 4 s run ends
                                                 // (0 = idle; the GUI timer flips
                                                 // the button back when it passes)
static std::vector<float>     g_tickWave;          // ONE pre-rendered tick (48 kHz mono)
static constexpr size_t kTickPeriod = 24000;       // 500 ms @ 48 kHz = 120 bpm
static constexpr size_t kTickCount  = 8;           // 8 beats = 4 s, then it stops
// v7.1.11 TICK CENSUS: chunks each leg's pump actually stamped with tick
// samples. The center-missing field report needs a witness: if leg2's
// count is 0 the dispatch never reached its pump (seq window); if it is
// ~8+ the samples left the pump and the loss is device-side.
static std::atomic<unsigned long long> g_tickWrites[2] = {};   // pump-thread only
static void tickCensusLog() {
    tlog("[twinspeak] tick test     : census -- leg1 stamped %llu chunks, leg2 %llu\n"
         "                            (8 beats in 500 us pump cycles -> a healthy leg reads\n"
         "                            dozens; a leg at 0 never saw the window, a leg with\n"
         "                            counts but no sound loses it past the pump)\n",
         (unsigned long long)g_tickWrites[0].load(std::memory_order_relaxed),
         (unsigned long long)g_tickWrites[1].load(std::memory_order_relaxed));
}

static void tickRender() {
    if (!g_tickWave.empty()) return;
    const size_t n = 360;                          // 7.5 ms burst
    g_tickWave.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = (double)i / (double)kSampleRate;
        g_tickWave[i] = (float)(0.3 * sin(kTwoPi * 1500.0 * t) * exp(-t / 0.002));
    }
}

// Sum the tick onto FL/FR (leg1) or both channels (leg2). seqBefore is the
// popped block's capture sequence -- the SAME clock on both legs.
static void tickSum(int idx, float* chunk, size_t n, unsigned long long seqBefore) {
    const long long t0 = g_tickSeq0.load(std::memory_order_acquire);
    if (t0 < 0) return;
    // v7.0.0 RAW dispatch: no latency compensation of any kind -- the tick
    // lands at the same capture-sequence position on both legs and rides
    // whatever path each leg actually has. The flam is the measurement.
    const long long pos = (long long)seqBefore - t0;
    if (pos < 0 || pos >= (long long)(kTickCount * kTickPeriod)) return;
    const size_t wn = g_tickWave.size();
    const int    ch = (idx == 0) ? kOutCh1 : 2;
    bool stamped = false;   // v7.1.11 census: did THIS chunk carry samples?
    for (size_t i = 0; i < n; ++i) {
        const long long t = pos + (long long)i;
        if (t >= (long long)(kTickCount * kTickPeriod)) break;
        const size_t ph = (size_t)t % kTickPeriod;
        if (ph >= wn) continue;
        stamped = true;
        const float s = g_tickWave[ph];
        float* o = chunk + i * (size_t)ch;
        // v7.0.0: the tick channels -- leg1: FL, FR, LFE (FC is DRIVEN
        // SILENCE on purpose and SL/SR are unused); leg2: the center pair.
        static const int kTickCh1[3] = { 0, 1, 3 };
        static const int kTickCh2[2] = { 0, 1 };
        const int   nc = (idx == 0) ? 3 : 2;
        const int*  tc = (idx == 0) ? kTickCh1 : kTickCh2;
        for (int k = 0; k < nc; ++k) {
            const int c = tc[k];
            float v = o[c] + s;
            if (v >  1.0f) v =  1.0f; else if (v < -1.0f) v = -1.0f;
            o[c] = v;
        }
    }
    if (stamped) g_tickWrites[idx].fetch_add(1, std::memory_order_relaxed);
}

// ----------------------------------------------------------------- pumps ----
// v6.0: the pump is a dumb, fast MOVER (device thread is the master, the
// producer never corrects anything): raw ring -> master volume -> per-leg
// mute/mix -> out ring (push all). No staging buffer, no resampler, no rate
// state anywhere; excess/shortfall are the callback's gated drop / replay.
// v6.5.7: the pump tick is 500 us -- ONE-SHOT high-resolution timer
// re-armed per cycle (a PERIODIC timer cannot go below 1 ms).
static constexpr long long kTick500us = -5000;   // 0.5 ms, negative = relative (100-ns units)

static DWORD WINAPI pumpThread(LPVOID p) {
    const int idx = (int)(INT_PTR)p;
    Leg& L = g_leg[idx];
    boostAudioThread();   // v3.8: MMCSS "Pro Audio" + time-critical priority
    // v6.1: with out2_first on, the CENTER pump outranks the mains pump by
    // one priority notch. Device threads stay equal: both are deadline-bound.
    if (idx == 0 && g_cfg.out2First)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    // SYNCHRONIZATION timer (no MANUAL_RESET flag!) -- a manual-reset timer
    // stays signaled and silently turns this loop into a 100%-core spin
    // (v1/v2.0's 30% CPU). A sync timer auto-resets per released wait.
    HANDLE hTimer = CreateWaitableTimerExW(nullptr, nullptr,
                        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                        TIMER_ALL_ACCESS);
    if (!hTimer) hTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    LARGE_INTEGER due; due.QuadPart = kTick500us;   // v6.5.7: one-shot 500 us, re-armed per cycle
    bool timerOK = false;
    if (hTimer) timerOK = SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE) != 0;
    if (!timerOK)
        tlog( "[twinspeak] leg%d: 500 us timer unavailable; poll degrades to Sleep(1)\n", idx + 1);

    // v6.5: outCh is no longer needed here -- mixBlock() derives the leg's
    // channel count from idx itself (the config is startup-frozen).
    float*       sc     = L.scratch.data();
    float*       chunk  = L.chunk.data();
    // v6.6.9 SILENCE COMPRESSION gate, frozen at pump start. The fast path
    // replaces mixBlock with a memset ONLY where a bit-zero block PROVABLY
    // mixes to a bit-zero chunk: leg1 needs strictly positive gains (both
    // lfeFold states); leg2 with EQ on resets the biquad (memset exact), EQ
    // off needs a non-negative gain. centerGuard still runs inside it.
    L.zeroFast = (idx == 0)
        ? (g_cfg.gainL > 0.0f && g_cfg.gainR > 0.0f && g_cfg.gainLfe > 0.0f)
        : (g_ceq.on ? true : g_cfg.gainCenter >= 0.0f);

    while (g_run.load()) {
        if (timerOK) {
            due.QuadPart = kTick500us;   // v6.5.7: re-arm one-shot every cycle
            SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(hTimer, 50);
        } else         Sleep(1);
        if (!g_run.load()) break;
        // v6.5.2: PBT_APMSUSPEND park -- the OS is taking the devices;
        // stop feeding the out rings. The raw ring overflows harmlessly
        // (capture-side evictions are counted in 'drop' and the sequence
        // ledger advances with them); a rebuild follows on resume.
        if (g_powerPark.load(std::memory_order_relaxed)) continue;
        // v6.5.14: DAC yielded to the Windows default device -- park the pump
        // exactly like the suspend park: nothing feeds a device we released.
        if (L.yielded.load(std::memory_order_relaxed)) continue;

        // drain raw -> master volume -> mute/mix -> out ring (push all); the
        // ring headroom absorbs the pop/burst sawtooth, so this push can only
        // passively evict under a pathological flood (counted in outDrop).
        for (;;) {
            const size_t avail = L.raw.count();
            if (avail == 0) break;
            const size_t n = avail < (size_t)kScratchFrames ? avail : (size_t)kScratchFrames;
            const unsigned long long seqBefore = L.raw.seqTail.load(std::memory_order_relaxed);
            L.raw.pop(sc, n);
            L.raw.seqTailAdvance(seqBefore + n);   // v6.4: consume the seqs with the frames
            // v3.8 VOLUME FOLLOW: one master gain -- the Line 1 endpoint
            // volume -- applied to the raw 8ch block BEFORE the per-leg mix.
            // Both legs track the same knob; the ini gains still stack on it.
            {
                const float vg = g_volMuted.load(std::memory_order_relaxed) ? 0.0f
                               : g_vol.load(std::memory_order_relaxed);
                if (vg != 1.0f) {
                    const size_t tot = n * (size_t)kInCh;
                    for (size_t s = 0; s < tot; ++s) sc[s] *= vg;
                }
            }
            if (idx == 1) centerGuard(L, sc, n);   // v6.1: center presence gates
            // v6.6.9: silence compression -- a bit-zero block needs no mix
            // (see the gate above): same bytes out, none of the FP work.
            if (L.zeroFast && bitZero8(sc, n)) {
                if (idx == 1 && g_ceq.on) { g_ceq.z1 = 0.0f; g_ceq.z2 = 0.0f; }
                memset(chunk, 0, (size_t)n * (idx == 0 ? (size_t)kOutCh1
                                                       : (size_t)kOutCh2) * sizeof(float));
            } else {
                mixBlock(idx, sc, chunk, n);       // v6.5: hoisted bulk mix
            }
            if (g_tickOn.load(std::memory_order_relaxed))
                tickSum(idx, chunk, n, seqBefore); // v7.0.0: the sync tick, raw, both legs
            const size_t dropped = L.out.pushOverwrite(chunk, n);
            if (dropped && L.streamSeeded)         // v6.4: passive evict = a seq skip
                L.out.seqTailAdvance(L.out.seqTail.load(std::memory_order_relaxed) + dropped);
            if (dropped) L.outDrop.fetch_add(dropped);
            // v6.4: arm the out ring's sequence space ONCE -- the seed is the
            // capture seq of the oldest real frame now in the ring. From here
            // on the DEVICE thread owns the boundary (pops/drops/skips); the
            // pump never advances it again.
            if (!L.streamSeeded) {
                L.out.seqTailAdvance(seqBefore);
                L.streamSeeded = true;
            }
        }
    }
    // final drain so the tail reaches the speaker: raw -> gain -> mix -> out.
    // (Same center gates -- the guarantee holds to the very last frame.)
    for (;;) {
        const size_t avail = L.raw.count();
        if (avail == 0) break;
        const size_t n = avail < (size_t)kScratchFrames ? avail : (size_t)kScratchFrames;
        const unsigned long long seqBefore = L.raw.seqTail.load(std::memory_order_relaxed);
        L.raw.pop(sc, n);
        L.raw.seqTailAdvance(seqBefore + n);           // v6.4
        {
            const float vg = g_volMuted.load(std::memory_order_relaxed) ? 0.0f
                           : g_vol.load(std::memory_order_relaxed);
            if (vg != 1.0f) {
                const size_t tot = n * (size_t)kInCh;
                for (size_t s = 0; s < tot; ++s) sc[s] *= vg;
            }
        }
        if (idx == 1) centerGuard(L, sc, n);   // v6.1: center presence gates
        if (L.zeroFast && bitZero8(sc, n)) {   // v6.6.9: silence compression
            if (idx == 1 && g_ceq.on) { g_ceq.z1 = 0.0f; g_ceq.z2 = 0.0f; }
            memset(chunk, 0, (size_t)n * (idx == 0 ? (size_t)kOutCh1
                                                   : (size_t)kOutCh2) * sizeof(float));
        } else {
            mixBlock(idx, sc, chunk, n);       // v6.5: hoisted bulk mix
        }
        if (g_tickOn.load(std::memory_order_relaxed))
            tickSum(idx, chunk, n, seqBefore); // v7.0.0: the sync tick, stream-aligned
        const size_t dropped = L.out.pushOverwrite(chunk, n);
        if (dropped && L.streamSeeded)
            L.out.seqTailAdvance(L.out.seqTail.load(std::memory_order_relaxed) + dropped);
        if (dropped) L.outDrop.fetch_add(dropped);
        if (!L.streamSeeded) {                         // v6.4: arm once (see main loop)
            L.out.seqTailAdvance(seqBefore);
            L.streamSeeded = true;
        }
    }
    if (hTimer) { CancelWaitableTimer(hTimer); CloseHandle(hTimer); }
    return 0;
}

// ============================================================ v6.3 WDM-KS ===
// A direct kernel-streaming output path -- the FlexASIO protocol, embedded
// and paced harder: filter = KSCATEGORY_RENDER wave filter opened as a file;
// pin = KsCreatePin render sink (KSPIN_CONNECT + WAVEFORMATEXTENSIBLE);
// buffer = the WaveRT looped buffer (driver DMA memory, mapped here); clock
// = the mapped HW play-position register or KSPROPERTY_AUDIO_POSITION;
// pacing = our own TIME-CRITICAL MMCSS thread writing exactly
// ks_period_frames when the write cursor nears the lead. No events, no
// engine, no scheduler. The audio crossing bufferSwitchFill() is
// bit-identical to WASAPI; only who paces the DMA changes. NOTE: a KS pin
// is a direct line to the DAC -- Windows endpoint volume/muting do not
// apply while held; volume_follow still governs the leg (applied in the
// pump, exactly as for WASAPI legs).

// (declared later, ini section -- used here for name matching)
static bool containsCI(const std::string& hay, const std::string& needle);

static TS_KsCreatePinFn ksCreatePinFn() {
    static TS_KsCreatePinFn fn = nullptr;
    static bool done = false;
    if (!done) {
        done = true;
        HMODULE h = LoadLibraryW(L"ksuser.dll");   // kept for the process lifetime
        if (h) fn = (TS_KsCreatePinFn)GetProcAddress(h, "KsCreatePin");
    }
    return fn;
}

static bool ksSync(HANDLE h, void* in, DWORD inSz, void* out, DWORD outSz, DWORD* retBytes) {
    DWORD rb = 0;
    if (!DeviceIoControl(h, IOCTL_KS_PROPERTY, in, inSz, out, outSz, &rb, nullptr))
        return false;
    if (retBytes) *retBytes = rb;
    return true;
}

static bool ksPropGet(HANDLE h, const GUID& set, ULONG id, void* out, DWORD outSz) {
    KSPROPERTY p;
    memset(&p, 0, sizeof(p));
    p.Set = set; p.Id = id; p.Flags = KSPROPERTY_TYPE_GET;
    return ksSync(h, &p, sizeof(p), out, outSz, nullptr);
}

static bool ksPinPropGet(HANDLE h, const GUID& set, ULONG id, ULONG pinId,
                         void* out, DWORD outSz) {
    KSP_PIN pp;
    memset(&pp, 0, sizeof(pp));
    pp.Property.Set = set; pp.Property.Id = id; pp.Property.Flags = KSPROPERTY_TYPE_GET;
    pp.PinId = pinId; pp.Reserved = 0;
    return ksSync(h, &pp, sizeof(pp), out, outSz, nullptr);
}

// KSSTATE transitions: STOP -> ACQUIRE -> PAUSE -> RUN (and back down).
// The state is carried as the "output" buffer of the SET, matching the
// reference WDM-KS backends' convention with METHOD_NEITHER properties.
static bool ksSetPinState(HANDLE pin, KSSTATE st) {
    KSPROPERTY p;
    memset(&p, 0, sizeof(p));
    p.Set = KSPROPSETID_Connection; p.Id = KSPROPERTY_CONNECTION_STATE;
    p.Flags = KSPROPERTY_TYPE_SET;
    return ksSync(pin, &p, sizeof(p), &st, sizeof(st), nullptr);
}

struct KsRange {
    int      maxCh;
    int      bitsMin, bitsMax;
    unsigned freqMin, freqMax;
};

// KSPROPERTY_PIN_DATARANGES -> KSMULTIPLE_ITEM + KSDATARANGE (AUDIO) array.
// Two-pass: a null call returns the required size via GetLastError
// (ERROR_MORE_DATA) and *retBytes.
static bool ksPinAudioRanges(HANDLE filter, ULONG pinId, std::vector<KsRange>& out) {
    out.clear();
    std::vector<unsigned char> buf(65536);
    if (!ksPinPropGet(filter, KSPROPSETID_Pin, KSPROPERTY_PIN_DATARANGES, pinId,
                      buf.data(), (DWORD)buf.size()))
        return false;
    const KSMULTIPLE_ITEM* mi = (const KSMULTIPLE_ITEM*)buf.data();
    if (mi->Size < sizeof(KSMULTIPLE_ITEM) || mi->Size > buf.size()) return false;
    const unsigned char* p   = buf.data() + sizeof(KSMULTIPLE_ITEM);
    const unsigned char* end = buf.data() + mi->Size;
    while (p + sizeof(KSDATARANGE) <= end) {
        const KSDATARANGE* r = (const KSDATARANGE*)p;
        if (r->FormatSize < sizeof(KSDATARANGE) || p + r->FormatSize > end) break;
        if (IsEqualGUID(r->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) &&
            IsEqualGUID(r->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) &&
            r->FormatSize >= sizeof(KSDATARANGE_AUDIO)) {
            const KSDATARANGE_AUDIO* a = (const KSDATARANGE_AUDIO*)p;
            KsRange cr;
            cr.maxCh   = (int)a->MaximumChannels;
            cr.bitsMin = (int)a->MinimumBitsPerSample;
            cr.bitsMax = (int)a->MaximumBitsPerSample;
            cr.freqMin = a->MinimumSampleFrequency;
            cr.freqMax = a->MaximumSampleFrequency;
            out.push_back(cr);
        }
        p += r->FormatSize;
    }
    return true;
}

struct KsFmt {
    const char* name;
    int         id;
    const GUID* sub;
    int         bits;        // valid bits per sample
    int         container;   // bytes per sample in the stream
};

// Sub-format GUIDs spelled locally (the bundled ksmedia.h guards the
// IEEE_FLOAT definition behind _INC_MMREG; these are the canonical
// KSDATAFORMAT_SUBTYPE values, WAVEFORMATEX-GUID form).
static const GUID TS_GUID_SUBTYPE_PCM =
    { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID TS_GUID_SUBTYPE_FLOAT =
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

// Proposal order: float32 is the bit-exact mirror of the engine's format
// (memcpy, like the WASAPI exclusive float path); then the integer depths.
// Every entry must also satisfy the pin's dataranges before it is tried.
static const KsFmt kKsFmts[] = {
    { "float32",      0, &TS_GUID_SUBTYPE_FLOAT, 32, 4 },
    { "int32",        1, &TS_GUID_SUBTYPE_PCM,   32, 4 },
    { "int24",        2, &TS_GUID_SUBTYPE_PCM,   24, 4 },  // 24 valid in 32-bit slots
    { "int24-packed", 3, &TS_GUID_SUBTYPE_PCM,   24, 3 },
    { "int16",        4, &TS_GUID_SUBTYPE_PCM,   16, 2 }
};
static const int kKsFmtCount = (int)(sizeof(kKsFmts) / sizeof(kKsFmts[0]));

static bool ksRangeAccepts(const std::vector<KsRange>& ranges, int outCh, const KsFmt& f) {
    for (const KsRange& r : ranges) {
        if (r.maxCh > 0 && outCh > r.maxCh) continue;
        if (f.bits < r.bitsMin || f.bits > r.bitsMax) continue;
        if (r.freqMin && kSampleRate < r.freqMin) continue;
        if (r.freqMax && kSampleRate > r.freqMax) continue;
        return true;
    }
    return false;
}

// One KsCreatePin attempt, exactly the reference-backend recipe:
//   KSPIN_CONNECT { Interface(looped|streaming), Medium(DEVIO), PinId,
//                   PinToHandle=NULL, Priority=NORMAL/1 }
// immediately followed by the data format as KSDATAFORMAT +
// WAVEFORMATEXTENSIBLE (FormatSize covers both; Specifier = WAVEFORMATEX).
// The driver reads the format at Connect + sizeof(KSPIN_CONNECT), so the
// whole blob must be one contiguous struct.
static HANDLE ksTryCreatePin(HANDLE filter, ULONG pinId, int outCh, const KsFmt& f,
                             bool looped, int* usedLooped) {
    struct Blob {
        KSPIN_CONNECT        connect;
        KSDATAFORMAT         df;
        WAVEFORMATEXTENSIBLE wfx;
    } blob;
    memset(&blob, 0, sizeof(blob));
    KSPIN_CONNECT* c = &blob.connect;
    c->Interface.Set = KSINTERFACESETID_Standard;
    c->Interface.Id  = looped ? (ULONG)KSINTERFACE_STANDARD_LOOPED_STREAMING
                              : (ULONG)KSINTERFACE_STANDARD_STREAMING;
    c->Interface.Flags = 0;
    c->Medium.Set    = KSMEDIUMSETID_Standard;
    c->Medium.Id     = KSMEDIUM_TYPE_ANYINSTANCE;   // == KSMEDIUM_STANDARD_DEVIO
    c->Medium.Flags  = 0;
    c->PinId         = pinId;
    c->PinToHandle   = nullptr;
    c->Priority.PriorityClass    = KSPRIORITY_NORMAL;
    c->Priority.PrioritySubClass = 1;

    const WORD blockAlign = (WORD)(outCh * f.container);
    blob.df.FormatSize  = (ULONG)sizeof(blob.df) + (ULONG)sizeof(blob.wfx);
    blob.df.Flags       = 0;
    blob.df.SampleSize  = blockAlign;
    blob.df.Reserved    = 0;
    blob.df.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    memcpy(&blob.df.SubFormat, f.sub, sizeof(GUID));
    blob.df.Specifier   = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;

    WAVEFORMATEXTENSIBLE* w = &blob.wfx;
    w->Format.wFormatTag            = WAVE_FORMAT_EXTENSIBLE;
    w->Format.nChannels             = (WORD)outCh;
    w->Format.nSamplesPerSec        = kSampleRate;
    w->Format.wBitsPerSample        = (WORD)(f.container * 8);
    w->Format.nBlockAlign           = blockAlign;
    w->Format.nAvgBytesPerSec       = kSampleRate * blockAlign;
    w->Format.cbSize                = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    w->Samples.wValidBitsPerSample  = (WORD)f.bits;
    w->dwChannelMask = (outCh == 2) ? 0x3u
                     : (outCh == 6) ? (DWORD)KSAUDIO_SPEAKER_5POINT1
                     : (outCh == 8) ? (DWORD)KSAUDIO_SPEAKER_7POINT1_SURROUND : 0u;

    TS_KsCreatePinFn kp = ksCreatePinFn();
    if (!kp) return nullptr;
    HANDLE pin = nullptr;
    const DWORD err = kp(filter, c, GENERIC_WRITE | GENERIC_READ, &pin);
    if (err != ERROR_SUCCESS) {
        if (pin) CloseHandle(pin);
        return nullptr;
    }
    *usedLooped = looped ? 1 : 0;
    return pin;
}

static unsigned long ksGcd(unsigned long a, unsigned long b) { return b ? ksGcd(b, a % b) : a; }

// KSPROPERTY_RTAUDIO_BUFFER: hand the driver the requested looped-buffer
// size, get back its own DMA memory mapped into our address space. The
// request is aligned to the LCM(128, blockAlign) boundary the reference
// backends use (WaveRT allocations land on 128-byte hardware boundaries).
static bool ksAllocRtBuffer(HANDLE pin, ULONG blockAlign, size_t wantBytes,
                            unsigned char** buf, size_t* got, BOOL* barrier) {
    unsigned long gcd = ksGcd(128UL, (unsigned long)blockAlign);
    unsigned long lcm = 128UL * (unsigned long)blockAlign / (gcd ? gcd : 1);
    const size_t aligned = ((wantBytes + lcm - 1) / lcm) * lcm;
    TS_KSRTAUDIO_BUFFER_PROPERTY prop;
    memset(&prop, 0, sizeof(prop));
    prop.Property.Set = KSPROPSETID_RtAudio;
    prop.Property.Id  = TS_KSPR_RTAUDIO_BUFFER;
    prop.Property.Flags = KSPROPERTY_TYPE_GET;
    prop.BaseAddress = nullptr;
    prop.RequestedBufferSize = (ULONG)aligned;
    TS_KSRTAUDIO_BUFFER out;
    memset(&out, 0, sizeof(out));
    if (!ksSync(pin, &prop, sizeof(prop), &out, sizeof(out), nullptr)) return false;
    if (!out.BufferAddress || !out.ActualBufferSize) return false;
    *buf     = (unsigned char*)out.BufferAddress;
    *got     = out.ActualBufferSize;
    *barrier = out.CallMemoryBarrier;
    return true;
}

// The hardware play-position register: mapped with a SET of
// KSPROPERTY_RTAUDIO_POSITIONREGISTER (a "register this address with me"
// call -- the return structure carries the mapped pointer).
static bool ksMapPosRegister(HANDLE pin, volatile ULONG** reg) {
    TS_KSRTAUDIO_HWREGISTER_PROPERTY prop;
    memset(&prop, 0, sizeof(prop));
    prop.Property.Set = KSPROPSETID_RtAudio;
    prop.Property.Id  = TS_KSPR_RTAUDIO_POSITIONREGISTER;
    prop.Property.Flags = KSPROPERTY_TYPE_SET;
    prop.BaseAddress = nullptr;
    TS_KSRTAUDIO_HWREGISTER out;
    memset(&out, 0, sizeof(out));
    if (!ksSync(pin, &prop, sizeof(prop), &out, sizeof(out), nullptr)) return false;
    if (!out.Register) return false;
    *reg = (volatile ULONG*)out.Register;
    return true;
}

// Fallback clock: KSPROPERTY_AUDIO_POSITION (PlayOffset) through the pin.
// Same byte-offset semantics, one syscall per poll.
static bool ksReadPosIoctl(HANDLE pin, ULONG* posBytes) {
    KSAUDIO_POSITION pos;
    memset(&pos, 0, sizeof(pos));
    if (!ksPropGet(pin, KSPROPSETID_Audio, KSPROPERTY_AUDIO_POSITION,
                   &pos, sizeof(pos))) return false;
    *posBytes = (ULONG)pos.PlayOffset;
    return true;
}

struct KsFilterInfo {
    std::wstring path;
    std::string  name;
};

static std::string wideToNarrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                      nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// Every present KSCATEGORY_RENDER wave filter, with the friendliest name
// we can dig up (the interface key's FriendlyName first -- that is the
// wave filter's own name -- then the device's).
static void ksEnumerateRenderFilters(std::vector<KsFilterInfo>& out) {
    out.clear();
    const HDEVINFO di = SetupDiGetClassDevsW(&KSCATEGORY_RENDER, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) return;
    SP_DEVICE_INTERFACE_DATA ifd;
    ifd.cbSize = sizeof(ifd);
    for (ULONG i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &KSCATEGORY_RENDER, i, &ifd); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &ifd, nullptr, 0, &need, nullptr);
        if (!need) continue;
        std::vector<unsigned char> ddb(need, 0);
        SP_DEVICE_INTERFACE_DETAIL_DATA_W* dd = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)ddb.data();
        dd->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA did;
        did.cbSize = sizeof(did);
        if (!SetupDiGetDeviceInterfaceDetailW(di, &ifd, dd, need, nullptr, &did)) continue;
        std::wstring nm;
        HKEY k = SetupDiOpenDeviceInterfaceRegKey(di, &ifd, 0, KEY_READ);
        if (k != INVALID_HANDLE_VALUE) {
            wchar_t wb[512]; DWORD sz = sizeof(wb), type = 0;
            if (RegQueryValueExW(k, L"FriendlyName", nullptr, &type, (LPBYTE)wb, &sz) == ERROR_SUCCESS &&
                type == REG_SZ && wb[0])
                nm = wb;
            RegCloseKey(k);
        }
        if (nm.empty()) {
            wchar_t wb[512]; DWORD sz = sizeof(wb), type = 0;
            if (SetupDiGetDeviceRegistryPropertyW(di, &did, SPDRP_FRIENDLYNAME, &type,
                                                  (PBYTE)wb, sz, nullptr) && type == REG_SZ && wb[0])
                nm = wb;
        }
        if (nm.empty()) nm = L"(unnamed)";
        KsFilterInfo fi;
        fi.path = dd->DevicePath;
        fi.name = wideToNarrow(nm);
        out.push_back(fi);
    }
    SetupDiDestroyDeviceInfoList(di);
}

static void ksListKs() {
    std::vector<KsFilterInfo> fl;
    ksEnumerateRenderFilters(fl);
    tlog("[twinspeak] WDM-KS render filters on this system (%zu):\n", fl.size());
    for (size_t i = 0; i < fl.size(); ++i) {
        HANDLE fh = CreateFileW(fl[i].path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
        tlog("  [%zu] \"%s\"%s\n", i, fl[i].name.c_str(),
             (fh == INVALID_HANDLE_VALUE) ? "  (cannot open -- busy or access denied)" : "");
        if (fh == INVALID_HANDLE_VALUE) continue;
        ULONG ctypes = 0;
        if (ksPinPropGet(fh, KSPROPSETID_Pin, KSPROPERTY_PIN_CTYPES, 0,
                         &ctypes, sizeof(ctypes))) {
            for (ULONG p = 0; p < ctypes; ++p) {
                KSPIN_DATAFLOW df = (KSPIN_DATAFLOW)0;
                if (!ksPinPropGet(fh, KSPROPSETID_Pin, KSPROPERTY_PIN_DATAFLOW, p,
                                  &df, sizeof(df))) continue;
                if (df != KSPIN_DATAFLOW_IN) continue;
                std::vector<KsRange> ranges;
                const bool have = ksPinAudioRanges(fh, p, ranges);
                if (!have || ranges.empty()) {
                    tlog("        pin %lu: render, dataranges not readable\n", (unsigned long)p);
                    continue;
                }
                for (const KsRange& r : ranges) {
                    tlog("        pin %lu: up to %dch, %d..%d bit, %u..%u Hz\n",
                         (unsigned long)p, r.maxCh, r.bitsMin, r.bitsMax, r.freqMin, r.freqMax);
                }
            }
        }
        CloseHandle(fh);
    }
    tlog("  use a substring of a filter name as output_deviceN, then set\n"
         "  backendN = ks to route that leg through WDM-KS.\n");
}

// Open one leg's output on WDM-KS. Returns true with K.active = 1 and the
// pin held in KSSTATE_PAUSE (the thread flips it to RUN). Every refusal is
// logged with its reason; false simply means "use the WASAPI path here".
static bool ksOpenLeg(int idx, const std::string& needle) {
    Leg& L = g_leg[idx];
    KsOut& K = L.ks;
    const int outCh = L.outCh;
    const unsigned periodFrames = g_cfg.ksPeriodFrames;
    const unsigned loopPeriods  = g_cfg.ksPeriods;

    if (!ksCreatePinFn()) {
        tlog("[twinspeak] leg%d KS        : ksuser.dll unavailable -- WDM-KS not usable on this install\n",
             idx + 1);
        return false;
    }

    std::vector<KsFilterInfo> fl;
    ksEnumerateRenderFilters(fl);
    if (fl.empty()) {
        tlog("[twinspeak] leg%d KS        : no KSCATEGORY_RENDER wave filters found\n", idx + 1);
        return false;
    }
    tlog("[twinspeak] leg%d KS        : %zu render filters present -- matching \"%s\":\n",
         idx + 1, fl.size(), needle.c_str());
    for (size_t i = 0; i < fl.size(); ++i)
        tlog("[twinspeak] leg%d KS        :   [%zu] \"%s\"%s\n",
             idx + 1, i, fl[i].name.c_str(),
             containsCI(fl[i].name, needle) ? "  <-- matches" : "");
    int chosen = -1;
    for (size_t i = 0; i < fl.size(); ++i) {
        if (fl[i].name.empty()) continue;
        if (containsCI(fl[i].name, needle)) { chosen = (int)i; break; }
    }
    if (chosen < 0) {
        tlog("[twinspeak] leg%d KS        : no filter name matches \"%s\" -- use twinspeak --list-ks\n",
             idx + 1, needle.c_str());
        return false;
    }

    HANDLE fh = CreateFileW(fl[chosen].path.c_str(), GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    if (fh == INVALID_HANDLE_VALUE) {
        tlog("[twinspeak] leg%d KS        : cannot open filter \"%s\" (error %lu)\n",
             idx + 1, fl[chosen].name.c_str(), (unsigned long)GetLastError());
        return false;
    }

    ULONG ctypes = 0;
    if (!ksPinPropGet(fh, KSPROPSETID_Pin, KSPROPERTY_PIN_CTYPES, 0,
                      &ctypes, sizeof(ctypes)) || !ctypes) {
        tlog("[twinspeak] leg%d KS        : \"%s\" exposes no pin factories\n",
             idx + 1, fl[chosen].name.c_str());
        CloseHandle(fh);
        return false;
    }

    // Walk the render sink pins; on each, try every format the dataranges
    // allow (float32 first -- bit-exact), looped-streaming interface first.
    HANDLE pin = nullptr;
    int usedLooped = 0, usedFmt = 0;
    ULONG usedPin = 0;
    unsigned char* rtBuf = nullptr; size_t rtGot = 0; BOOL rtBarrier = FALSE;
    std::vector<KsRange> ranges;
    for (ULONG p = 0; p < ctypes && !pin; ++p) {
        KSPIN_DATAFLOW df = (KSPIN_DATAFLOW)0;
        if (!ksPinPropGet(fh, KSPROPSETID_Pin, KSPROPERTY_PIN_DATAFLOW, p,
                          &df, sizeof(df))) continue;
        if (df != KSPIN_DATAFLOW_IN) continue;
        const bool haveRanges = ksPinAudioRanges(fh, p, ranges);
        for (int fi = 0; fi < kKsFmtCount && !pin; ++fi) {
            const KsFmt& f = kKsFmts[fi];
            if (haveRanges && !ksRangeAccepts(ranges, outCh, f)) continue;
            for (int lp = 1; lp >= 0 && !pin; --lp) {
                int ul = 0;
                HANDLE hp = ksTryCreatePin(fh, p, outCh, f, lp != 0, &ul);
                if (!hp) continue;
                unsigned char* buf = nullptr; size_t got = 0; BOOL barrier = FALSE;
                const size_t wantBytes = (size_t)outCh * (size_t)f.container *
                                         (size_t)periodFrames * (size_t)loopPeriods;
                if (!ksAllocRtBuffer(hp, (ULONG)(outCh * f.container), wantBytes,
                                     &buf, &got, &barrier)) {
                    tlog("[twinspeak] leg%d KS        : pin %lu accepted %s but has no WaveRT looped buffer\n",
                         idx + 1, (unsigned long)p, f.name);
                    CloseHandle(hp);
                    continue;
                }
                pin = hp; usedLooped = ul; usedFmt = fi; usedPin = p;
                rtBuf = buf; rtGot = got; rtBarrier = barrier;
            }
        }
    }
    if (!pin) {
        tlog("[twinspeak] leg%d KS        : \"%s\" refused every 48 kHz pin/format (%dch) -- WaveRT polling unavailable\n",
             idx + 1, fl[chosen].name.c_str(), outCh);
        CloseHandle(fh);
        return false;
    }

    const KsFmt& f = kKsFmts[usedFmt];
    K.frameBytes  = (unsigned)(outCh * f.container);
    K.fmt         = f.id;
    K.fmtName     = f.name;
    K.outCh       = outCh;
    K.pinId       = (int)usedPin;
    K.filterName  = fl[chosen].name;
    K.periodFrames = periodFrames;
    K.loopBuf     = rtBuf;
    K.loopBytes   = rtGot;
    K.barrier     = rtBarrier;

    K.loopFrames = K.loopBytes / K.frameBytes;
    if (K.loopFrames < 2 * (size_t)periodFrames) {
        tlog("[twinspeak] leg%d KS        : driver returned a too-small WaveRT buffer (%u fr < %u)\n",
             idx + 1, (unsigned)K.loopFrames, 2 * periodFrames);
        K.loopBuf = nullptr;
        CloseHandle(pin);
        CloseHandle(fh);
        return false;
    }

    // Clock: mapped position register first, IOCTL polling as the fallback.
    K.posMapped = false;
    K.posReg = nullptr;
    volatile ULONG* reg = nullptr;
    if (ksMapPosRegister(pin, &reg)) {
        K.posReg = reg;
        K.posMapped = true;
    }
    // Hardware FIFO depth (informational -- the codec adds it on top of the
    // DMA position; ignoring it is the CONSERVATIVE choice: the FIFO acts
    // as extra cushion between our write cursor and the converter).
    {
        TS_KSRTAUDIO_HWLATENCY hl;
        memset(&hl, 0, sizeof(hl));
        if (ksPropGet(pin, KSPROPSETID_RtAudio, TS_KSPR_RTAUDIO_HWLATENCY,
                      &hl, sizeof(hl)))
            K.fifoFrames = hl.FifoSize / (K.frameBytes ? K.frameBytes : 1);
    }

    // STOP -> ACQUIRE -> PAUSE, prefill the loop with silence, park the
    // write cursor one lead ahead of the play position. The thread sets RUN.
    ksSetPinState(pin, KSSTATE_STOP);
    ksSetPinState(pin, KSSTATE_ACQUIRE);
    ksSetPinState(pin, KSSTATE_PAUSE);
    memset(K.loopBuf, 0, K.loopBytes);

    ULONG posBytes = 0;
    if (K.posReg) posBytes = *K.posReg;
    else if (!ksReadPosIoctl(pin, &posBytes)) posBytes = 0;
    const size_t posFrames = (size_t)(posBytes % (ULONG)K.loopBytes) / K.frameBytes;

    K.leadFrames  = (loopPeriods - 1) * periodFrames;
    if (K.leadFrames + periodFrames > (unsigned)K.loopFrames)
        K.leadFrames = (unsigned)(K.loopFrames - periodFrames);   // driver gave us more room than asked
    K.playFrames  = posFrames;
    K.writeFrames = posFrames + K.leadFrames;   // prefill silence sits [pos, pos+lead)
    K.lastPosBytes = posFrames * K.frameBytes;
    K.havePos     = true;
    K.posGlitch   = 0;
    K.wrote       = 0;
    K.conv.assign((size_t)periodFrames * K.frameBytes + 64, 0);
    K.fill.assign((size_t)periodFrames * (size_t)outCh, 0.0f);

    L.backend    = 1;
    L.isExclusive = false;
    K.active     = 1;
    K.filter     = fh;
    K.pin        = pin;

    tlog("[twinspeak] leg%d KS        : \"%s\" pin %lu, %s %dch @ %u Hz, %s interface, WaveRT loop %u fr (%u x %u), lead %u fr | pos %s | fifo ~%u fr\n",
         idx + 1, K.filterName.c_str(), (unsigned long)usedPin, K.fmtName.c_str(), outCh,
         (unsigned)kSampleRate, usedLooped ? "looped-streaming" : "streaming",
         (unsigned)K.loopFrames, loopPeriods, periodFrames,
         K.leadFrames, K.posMapped ? "REGISTER (mapped)" : "IOCTL polled", K.fifoFrames);
    return true;
}

// Tear one leg's KS output down: stop the pacing thread first, then walk
// the pin down through PAUSE/STOP and close everything.
static void ksCloseLeg(int idx) {
    KsOut& K = g_leg[idx].ks;
    K.active = 0;                                   // ksThread exits on this
    if (K.thread) {
        WaitForSingleObject(K.thread, 3000);
        CloseHandle(K.thread);
        K.thread = nullptr;
    }
    if (K.pin) {
        // v7.0.0: the shutdown silence, KS edition -- the looped WaveRT buffer
        // keeps looping whatever was written last until the pin stops, so
        // zero it WHILE the pin still runs (a PAUSE would freeze the cursor
        // on the old content), let the DMA read one silent sweep, then stop.
        if (K.loopBuf && K.loopBytes) {
            memset(K.loopBuf, 0, K.loopBytes);
            Sleep(40);
        }
        ksSetPinState(K.pin, KSSTATE_PAUSE);
        ksSetPinState(K.pin, KSSTATE_STOP);
        CloseHandle(K.pin);
        K.pin = nullptr;
    }
    if (K.filter) { CloseHandle(K.filter); K.filter = nullptr; }
    K.loopBuf = nullptr;
    K.loopBytes = K.loopFrames = 0;
    K.posReg = nullptr;
    K.posMapped = false;
    K.pinId = -1;
    K.conv.clear();
    K.conv.shrink_to_fit();
    K.fill.clear();
    K.fill.shrink_to_fit();
}

// float -> negotiated KS format. float32 is a straight memcpy (the engine's
// own format -- bit-exact); the integer depths clamp to [-1,1] and round to
// nearest, the same conversion class miniaudio applies on the WASAPI path.
static void ksConvertTo(const KsOut& K, const float* src, unsigned char* dst, size_t frames) {
    const int ch = K.outCh;
    switch (K.fmt) {
    case 0: {   // float32
        memcpy(dst, src, frames * (size_t)ch * sizeof(float));
        break;
    }
    case 1: {   // int32
        int32_t* d = (int32_t*)dst;
        for (size_t i = 0; i < frames * (size_t)ch; ++i) {
            float s = src[i];
            if (s >  1.0f) s =  1.0f; else if (s < -1.0f) s = -1.0f;
            d[i] = (int32_t)lrintf(s * 2147483647.0f);
        }
        break;
    }
    case 2: {   // int24 valid bits in 32-bit slots (left-justified)
        int32_t* d = (int32_t*)dst;
        for (size_t i = 0; i < frames * (size_t)ch; ++i) {
            float s = src[i];
            if (s >  1.0f) s =  1.0f; else if (s < -1.0f) s = -1.0f;
            d[i] = (int32_t)lrintf(s * 8388607.0f) << 8;
        }
        break;
    }
    case 3: {   // int24 packed (3 bytes, little-endian)
        for (size_t i = 0; i < frames * (size_t)ch; ++i) {
            float s = src[i];
            if (s >  1.0f) s =  1.0f; else if (s < -1.0f) s = -1.0f;
            const int32_t v = (int32_t)lrintf(s * 8388607.0f) & 0x00FFFFFF;
            dst[i * 3 + 0] = (unsigned char)(v & 0xFF);
            dst[i * 3 + 1] = (unsigned char)((v >> 8) & 0xFF);
            dst[i * 3 + 2] = (unsigned char)((v >> 16) & 0xFF);
        }
        break;
    }
    default: {  // int16
        int16_t* d = (int16_t*)dst;
        for (size_t i = 0; i < frames * (size_t)ch; ++i) {
            float s = src[i];
            if (s >  1.0f) s =  1.0f; else if (s < -1.0f) s = -1.0f;
            d[i] = (int16_t)lrintf(s * 32767.0f);
        }
        break;
    }
    }
}

// Copy one period into the looped buffer (wrap-aware, two memcpys at the seam).
static void ksWriteLoop(KsOut& K, const unsigned char* data, size_t bytes) {
    const size_t off = (size_t)(K.writeFrames % K.loopFrames) * K.frameBytes;
    if (off + bytes <= K.loopBytes) {
        memcpy(K.loopBuf + off, data, bytes);
    } else {
        const size_t a = K.loopBytes - off;
        memcpy(K.loopBuf + off, data, a);
        memcpy(K.loopBuf, data + a, bytes - a);
    }
    if (K.barrier) MemoryBarrier();
}

// The KS pacing thread -- the WDM-KS twin of miniaudio's device thread.
// Same heartbeats, same bufferSwitchFill, same leg2 extras; the difference
// is the clock it obeys: the DAC's own position, read directly.
static DWORD WINAPI ksThread(LPVOID p) {
    const int idx = (int)(INT_PTR)p;
    Leg& L = g_leg[idx];
    KsOut& K = L.ks;
    boostAudioThread();   // MMCSS "Pro Audio" + TIME_CRITICAL, same as the pumps

    HANDLE hTimer = CreateWaitableTimerExW(nullptr, nullptr,
                        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!hTimer) hTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    LARGE_INTEGER due; due.QuadPart = kTick500us;   // v6.5.7: one-shot 500 us, re-armed per sleep
    const bool timerOK = hTimer && SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE);

    ksSetPinState(K.pin, KSSTATE_RUN);   // go live: the DAC starts eating the prefill

    float* fill = K.fill.data();
    unsigned char* conv = K.conv.data();
    const unsigned periodFr = K.periodFrames;
    const unsigned leadFr   = K.leadFrames;

    // v6.5.13 lapse guard: the DAC advances on WALL time; this thread
    // advances on its own poll cadence. QPC is the wall-clock reference.
    LARGE_INTEGER qpcFreq = {};
    QueryPerformanceFrequency(&qpcFreq);
    unsigned long long lastLapseLogMs = 0;   // log rate limit

    while (g_run.load() && K.active) {
        // ---- read + integrate the play position (bytes inside the loop) ----
        ULONG raw = 0;
        bool ok = false;
        if (K.posReg) { raw = *K.posReg; ok = true; }
        else          { ok = ksReadPosIoctl(K.pin, &raw); }
        if (ok) {
            // ---- v6.5.13 LAPSE GUARD ------------------------------------
            // The residue integration only sees advances MOD the loop: a poll
            // gap of one lap or more (deschedule, suspend) silently loses k
            // laps and the DAC circles re-reading stale audio (the sawing)
            // with every other meter clean. Detection: the DAC's true advance
            // over the gap is wall-exact; residue advance is mod-loop. A
            // lap-sized gap or disagreement beyond tolerance -> re-pin the
            // write cursor to play+lead and re-prime (a bit-exact skip).
            LARGE_INTEGER qn;
            QueryPerformanceCounter(&qn);
            double elapsed = 0.0;
            if (K.qpcArmed && qpcFreq.QuadPart)
                elapsed = (double)(qn.QuadPart - K.lastQpc.QuadPart)
                        / (double)qpcFreq.QuadPart;
            K.lastQpc  = qn;
            K.qpcArmed = true;
            if (K.loopFrames && qpcFreq.QuadPart) {
                const long long expectedFr = (long long)(elapsed * (double)kSampleRate);
                const ULONG     pbGuard    = raw - raw % (ULONG)K.frameBytes;
                long long obsFr = ((long long)pbGuard - (long long)K.lastPosBytes)
                                / (long long)K.frameBytes;
                if (obsFr < 0) obsFr += (long long)K.loopFrames;   // residue wrap
                const long long tolFr = (long long)2 * periodFr + 96;
                const bool lapLoss    = expectedFr >= (long long)K.loopFrames;
                const bool residueOdd = obsFr > expectedFr + tolFr
                                     || obsFr + tolFr < expectedFr;
                if (lapLoss || residueOdd) {
                    const unsigned long long laps = lapLoss
                        ? (unsigned long long)(expectedFr / (long long)K.loopFrames) : 1;
                    const size_t playOffFr   = (size_t)(pbGuard / K.frameBytes) % K.loopFrames;
                    const size_t curWriteOff = (size_t)(K.writeFrames % K.loopFrames);
                    const size_t offDelta    = (playOffFr >= curWriteOff)
                        ? playOffFr - curWriteOff
                        : K.loopFrames - (curWriteOff - playOffFr);
                    K.writeFrames += offDelta;   // write cursor -> the DAC's true position
                    // re-prime the lead region [play, play + lead): the ASIO
                    // replay convention (the last played block, tiled), or
                    // silence before the first real block ever played.
                    for (size_t f = 0; f < leadFr; ) {
                        const size_t chunk = (leadFr - f < periodFr) ? (leadFr - f) : periodFr;
                        if (L.haveLast && L.lastN) {
                            for (size_t j = 0; j < chunk; ++j) {
                                const float* src = L.lastBlock.data()
                                                 + (((f + j) % L.lastN) * L.outCh);
                                memcpy(fill + j * L.outCh, src, L.outCh * sizeof(float));
                            }
                        } else {
                            memset(fill, 0, (size_t)chunk * L.outCh * sizeof(float));
                        }
                        ksConvertTo(K, fill, conv, chunk);
                        ksWriteLoop(K, conv, (size_t)chunk * K.frameBytes);
                        K.writeFrames += chunk;
                        f += chunk;
                    }
                    K.playFrames   = K.writeFrames - leadFr;   // trusted lead restored
                    K.lastPosBytes = pbGuard;
                    K.lapses.fetch_add(1, std::memory_order_relaxed);
                    const unsigned long long lapseMs = GetTickCount64();
                    if (lapseMs - lastLapseLogMs >= 1000) {
                        lastLapseLogMs = lapseMs;
                        tlog("[twinspeak] leg%d KS lapse  : %llu loop lap(s) of play-position history lost in a\n"
                             "                            %.1f ms pacing stall -- the DAC was re-reading stale loop audio\n"
                             "                            (the reverberated sawing); write cursor re-pinned to play+lead,\n"
                             "                            lead region re-primed (%u fr, replay convention) -- bit-exact skip\n",
                             idx + 1, laps, elapsed * 1000.0, leadFr);
                    }
                }
            }
            const ULONG pb = raw % (ULONG)K.loopBytes;
            if (K.havePos) {
                ULONG d = pb;
                if (pb < K.lastPosBytes) d += (ULONG)K.loopBytes;
                d -= K.lastPosBytes;
                if (d <= (ULONG)K.loopBytes) {           // plausible advance
                    const ULONG dfr = d / K.frameBytes;  // frame-aligned portion
                    K.playFrames += dfr;
                    K.lastPosBytes = (K.lastPosBytes + dfr * K.frameBytes) % (ULONG)K.loopBytes;
                } else {
                    ++K.posGlitch;                       // wild register value: ignore
                }
            } else {
                K.lastPosBytes = pb;
                K.havePos = true;
            }
        }
        // ---- pacing: sleep while far ahead of the lead, spin at the deadline ----
        long long ahead = (long long)K.writeFrames - (long long)K.playFrames;
        K.aheadFr.store((unsigned long long)(ahead > 0 ? ahead : 0),
                        std::memory_order_relaxed);   // v6.5.9: live device pad
        if (ahead >= (long long)leadFr) {
            const long long slackFr = ahead - (long long)leadFr;
            if (slackFr > (long long)(kSampleRate / 2000)) {      // > 0.5 ms of slack
                if (timerOK) {
                    due.QuadPart = kTick500us;                    // v6.5.7: re-arm one-shot 500 us
                    SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE);
                    WaitForSingleObject(hTimer, 2);
                } else
                    Sleep(1);
            } else {
                YieldProcessor();   // final stretch: spin, no scheduler round-trip
            }
            continue;
        }
        // ---- the buffer switch (identical to the WASAPI device callback) ----
        hbOut(idx);
        bufferSwitchFill(idx, fill, periodFr);
        if (idx == 1) sumLeg2Extras(fill, periodFr);
        ksConvertTo(K, fill, conv, periodFr);
        ksWriteLoop(K, conv, (size_t)periodFr * K.frameBytes);
        K.writeFrames += periodFr;
        K.wrote       += periodFr;
    }

    ksSetPinState(K.pin, KSSTATE_PAUSE);
    if (hTimer) { CancelWaitableTimer(hTimer); CloseHandle(hTimer); }
    return 0;
}


static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::string lower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = (char)tolower((unsigned char)c);
    return r;
}
static bool containsCI(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    return lower(hay).find(lower(needle)) != std::string::npos;
}
static bool isOff(const std::string& v) {
    std::string l = lower(v);
    return l == "off" || l == "0" || l == "no" || l == "false";
}

static void writeTemplate(const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f,
        "; twinspeak v7.1.16 config -- BIT-EXACT ASIO-strict engine: out1 = L/R + LFE fold,\n"
        "; out2 = center ONLY (every other channel muted), no resampler anywhere.\n"
        "\n"
        "; out1ms / out2ms set each leg's requested WASAPI device period. Keep them\n"
        "; EQUAL -- equal periods keep the legs time-aligned. Exclusive honors small\n"
        "; values; shared mode clamps to ~10 ms. v6.2 ships 5 ms: path = ring (~19 ms)\n"
        "; + device buffer (10 ms) + capture burst (~5 ms) = ~34 ms. If a driver\n"
        "; refuses 5 ms the banner says so -- set that leg to 10 (path ~54 ms).\n"
        "out1ms = 5\n"
        "out2ms = 5\n"
        "\n"
        "; CAPTURE burst period -- decoupled from the out periods above. Smaller =\n"
        "; finer capture granularity = lower and more stable leg latency. 5 ms is\n"
        "; the sweet spot. In shared mode the Windows engine clamps this to ~10 ms\n"
        "; -- the banner prints the REAL negotiated burst. On a WaveCyclic Line 1\n"
        "; port that clamp doubles every window; the WaveRT port driver (VAC\n"
        "; Control Panel) unlocks the real 5 ms.\n"
        "cap_ms = 5\n"
        "\n"
        "; Out RING depth per leg, in ms. The ring target is FLOORED at the FULL\n"
        "; device buffer (2 periods) + a FULL capture burst + 4 ms regardless of\n"
        "; this -- the ring must survive the device's worst legitimate bite (a\n"
        "; late/merged pop asks for both periods at once). At 5/5/cap5 the floor\n"
        "; is ~19 ms; raise ring1_ms/ring2_ms only for extra glitch headroom\n"
        "; (each ms costs 1 ms latency, legs stay aligned if kept equal).\n"
        "; v6.3.1: a backend = ks leg floors LOWER -- one KS bite + one capture\n"
        "; burst + 1 ms -- because its only pull is twinspeak's own pacing thread\n"
        "; (exactly one bite, never merged). At ks_period_frames 96 + cap_ms 1\n"
        "; that floor is 5 ms and the leg's est path prints ~10 ms.\n"
        "ring1_ms = 10\n"
        "ring2_ms = 10\n"
        "\n"
        "; ALIGNMENT DELAY (v6.1): delay1_ms / delay2_ms push ONE leg's output\n"
        "; later by whole milliseconds -- a BIT-EXACT ring delay: the leg's ring\n"
        "; target grows, the samples leave it later, nothing is resampled or\n"
        "; regenerated. Put the delay on whichever leg arrives EARLY:\n"
        ";   * the center speaker's USB path (amp + DSP) lags the mains -> raise\n"
        ";     delay1_ms so out1 waits for it;\n"
        ";   * the center leg runs early instead -> raise delay2_ms.\n"
        "; The banner prints each leg's 'est path' -- start at the printed\n"
        "; difference between the two legs, then trim by ear in 1-2 ms steps.\n"
        "; (The S5's own amp latency is OUTSIDE the printed path -- only the ear\n"
        "; can see it.) Each ms costs 1 ms of latency on that leg only.\n"
        "delay1_ms = 0\n"
        "delay2_ms = 0\n"
        "\n"
        "; v6.6.8: exclusive (WASAPI, both legs), capture_exclusive and\n"
        "; align_modes are part of the engine now -- ALWAYS ON, no ini key.\n"
        "; Both legs open WASAPI-exclusive together (a driver refusal reopens\n"
        "; BOTH shared so device buffering stays identical) and the VAC capture\n"
        "; pin is taken exclusive with a logged shared fallback. Exclusive\n"
        "; bypasses the Windows volume slider -- that is what volume_follow\n"
        "; exists for. (pin_outputs is part of the engine now -- ALWAYS ON.)\n"
        "\n"
        "; Fold the cable's LFE onto FL/FR at gain_lfe -- the CONFIRMED routing\n"
        "; (\"LFE is mixed fully into the left and right channels\"). The sub is\n"
        "; not discrete; it lives inside the front channels. lfe_fold = off\n"
        "; instead drives the DISCRETE 5.1 LFE channel (only for a real sub).\n"
        "lfe_fold = on\n"
        "\n"
        "; Substring match against device names (see: twinspeak --list)\n"
        "input_device   = Line 1\n"
        "output_device1 = Realtek\n"
        "output_device2 = Xtream\n"
        "\n"
        "; optional mixes (gain_lfe = LFE fold gain onto FL/FR; set 1.0 for a\n"
        ";                    unity fold. With lfe_fold = off it scales the\n"
        ";                    discrete sub.)\n"
        "gain_l = 1.0\n"
        "gain_r = 1.0\n"
        "gain_lfe = 0.4\n"
        "gain_center = 1.0\n"
        "\n"
        "; CENTER DIALOGUE EQ (v6.5.18): an opt-in peaking boost on the center\n"
        "; channel, applied ONLY to out2 (the center speaker). Built for exactly\n"
        "; one job: lifting dialogue presence without touching the mains. It\n"
        "; CANNOT be done with Equalizer APO -- the legs are kernel-streaming\n"
        "; pins, and KS bypasses the Windows audio engine (and every APO) whole.\n"
        "; center_eq = off keeps out2 bit-exact (the default). When on:\n"
        ";   center_eq_freq -- the peak's center in Hz. Dialogue presence and\n"
        ";     intelligibility live around 1.5-3 kHz; 1800 Hz is the general pick.\n"
        ";   center_eq_gain -- boost in dB. +2..+4 subtle, +6 obvious; the path\n"
        ";     clamps at full scale and gain_center stacks on top, so loud\n"
        ";     center peaks may clip first -- lower gain_center if they do.\n"
        ";   center_eq_q    -- sharpness. 0.7-1.0 = broad musical bump; 2+ = a\n"
        ";     narrow spike (harsh faster).\n"
        "center_eq = off\n"
        "center_eq_freq = 1800\n"
        "center_eq_gain = 4.0\n"
        "center_eq_q = 0.8\n"
        "; v7.1.9 EQ SAVE SLOTS: five user slots replace the old built-in\n"
        "; presets. Left-click a slot applies it AND arms it; adjust the\n"
        "; sliders, press SAVE TO SLOT on the CENTER EQ page and the curve\n"
        "; + the EQ pill land in the armed slot (right-click a slot saves\n"
        "; straight into it) -- either way this file is spliced at once.\n"
        "; Slot 1 ships reserved with the dialogue curve below; a freq of 0\n"
        "; marks an empty slot.\n"
        "eq_slot1_on = on\n"
        "eq_slot1_freq = 387\n"
        "eq_slot1_gain = 3.5\n"
        "eq_slot1_q = 0.30\n"
        "eq_slot2_on = off\n"
        "eq_slot2_freq = 0\n"
        "eq_slot2_gain = 0\n"
        "eq_slot2_q = 0\n"
        "eq_slot3_on = off\n"
        "eq_slot3_freq = 0\n"
        "eq_slot3_gain = 0\n"
        "eq_slot3_q = 0\n"
        "eq_slot4_on = off\n"
        "eq_slot4_freq = 0\n"
        "eq_slot4_gain = 0\n"
        "eq_slot4_q = 0\n"
        "eq_slot5_on = off\n"
        "eq_slot5_freq = 0\n"
        "eq_slot5_gain = 0\n"
        "eq_slot5_q = 0\n"
        "\n"
        "; VOLUME: volume_follow = the Windows volume of the cable's PLAYBACK\n"
        "; side (the Line 1 slider in sndvol, or the volume hotkeys when Line 1\n"
        "; is the default device) is the single master knob -- twinspeak\n"
        "; multiplies every captured frame by it, so BOTH out1 and out2 track it\n"
        "; identically. The playback endpoint is matched by name: volume_endpoint\n"
        "; first (substring), then input_device, then any 'Virtual Audio Cable'\n"
        "; render endpoint. If nothing matches, the banner prints EVERY render\n"
        "; endpoint with its state -- copy the cable's playback name into\n"
        "; volume_endpoint and rerun.\n"
        "volume_follow = on\n"
        "volume_endpoint =\n"
        "; v7.0.0: pin_outputs is part of the engine now -- ALWAYS ON: both\n"
        "; endpoint volumes are pinned 100%% / unmuted while the engine runs.\n"
        "out2_first = on\n"
        "\n"
        "; KEEP-ALIVE (BUILT IN -- no keys, no UI): out2 sums a sine on\n"
        "; both channels every frame (silence or not) so the center device\n"
        "; never sees digital silence. 40 Hz @ 0.001 (-60 dBFS, v7.1.12 --\n"
        "; was 1 Hz @ 0.0001, too little for some amps' sleep timers), still\n"
        "; below hearing, and a wobble every amp's sleep timer treats as signal.\n"
        "; The watchdog reopens out2 automatically whenever it comes back.\n"
        "; POST-MIX: the tone and the chime are summed in the out2 OUTPUT\n"
        "; callbacks, after the mix -- they never enter the capture stream an\n"
        "; OBS capture records.\n"
        "\n"
        "; CHIMES (v6.2): the CenterFold startup signature is back, modernized.\n"
        "; The old jingle was a rising SQUARE fifth (G5 784 Hz -> D6 1174.66 Hz);\n"
        "; the modernized chime keeps the rising fifth but plays two short\n"
        "; TRIANGLE BLIPS in the low treble instead: C7 2093 Hz (42 ms) -> G7\n"
        "; 3136 Hz (72 ms), fast decay -- a blip, not a horn. It plays on the\n"
        "; CENTER speaker the moment the engine goes live, and again whenever\n"
        "; the watchdog brings out2 back (3 s cooldown). It is a UI cue, not\n"
        "; program audio: summed at the out2 device layer exactly like the\n"
        "; keep-alive, so volume_follow does NOT scale it, and out1 -- the\n"
        "; bit-exact mains path -- NEVER carries it. chime_gain is the blip\n"
        "; peak (0..1; 0.4 = clearly audible; lower it if the center speaker\n"
        "; runs hot). chimes = off restores the v6.1 silence.\n"
        "chimes = on\n"
        "chime_gain = 0.4\n"
        "\n"
        "; v6.6.8: background is part of the engine now -- ALWAYS ON. The\n"
        "; console detaches after a successful start and everything logs to\n"
        "; twinspeak.log next to this ini. Stop a background run with:\n"
        ";   taskkill /IM twinspeak.exe\n"
        "; Start it visible anyway with:   twinspeak --foreground\n"
        "\n"
        "; v6.6.8: yield_default is part of the engine now -- ALWAYS ON: when\n"
        "; the WINDOWS DEFAULT render endpoint is (or becomes) one of\n"
        "; twinspeak's leg devices, that leg's DAC is released so the app\n"
        "; playing to the default owns the hardware; the leg reopens by\n"
        "; itself once the default moves elsewhere AND the device is QUIET\n"
        "; (the log announces both directions -- 'legN YIELD').\n"
        "\n"
        "; WDM-KS OUTPUT BACKEND (v6.3) -- the FlexASIO path, inside twinspeak.\n"
        "; v7.1.7: NO UI CONTROL -- both legs run wasapi exclusive unless these\n"
        "; keys say ks. backend1 / backend2 route each leg through Kernel\n"
        "; Streaming instead of wasapi exclusive: a direct render pin on the\n"
        "; device's wave filter (KsCreatePin),\n"
        "; the WaveRT looped buffer as the device's own DMA memory, and the\n"
        "; hardware play-position register mapped read-only -- twinspeak's own\n"
        "; TIME-CRITICAL thread paces the stream against the DAC itself, with no\n"
        "; events, no engine and no scheduler intermediaries in between. The\n"
        "; audio crossing the buffer switch is bit-identical (same rings, same\n"
        "; replay/drop rules, same gates); only the transport changes.\n"
        ";   backendN = wasapi  wasapi exclusive -- the proven path (miniaudio,\n"
        ";                      exclusive WaveRT event mode) -- the default\n"
        ";   backendN = ks      WDM-KS, lowest latency, 256-sample switches\n"
        "; ks_period_frames = the buffer-switch size in SAMPLES (256 @ 48 kHz =\n"
        "; one bite every 5.33 ms; 96 = one bite every 2 ms -- smaller bites\n"
        "; shrink the KS ring floor too; v6.5.14 field note: 128 MINIMUM paces\n"
        "; much steadier -- the lapse guard's lap window shrinks with the loop\n"
        "; and sub-128 loops buzzed in the field). ks_periods = the looped buffer size\n"
        "; around it: 2 = the write lead is one period (lowest latency, the pump\n"
        "; must never miss a deadline), 3 = one extra period of grace\n"
        "; (recommended), up to 8 -- a 1 in the ini clamps to 2 with a log line\n"
        "; (a 1-period loop has no write lead at all). The DAC adds its hardware\n"
        "; FIFO on top (~1-2 ms, printed). v6.3.1: with both legs on ks the\n"
        "; banner's align line prints the est-path DIFFERENCE and which delay\n"
        "; knob to move -- the delay trim starts from that number. v6.5.9: the\n"
        "; health log's 'devbuf' line (every 10 s) prints the MEASURED version\n"
        "; of those paths off the live devices (device pad + ring depth).\n"
        "; KS bypasses the Windows endpoint: no endpoint volume there, so\n"
        "; output pinning (always on, v7.0.0) is a no-op on a KS leg --\n"
        "; volume_follow still governs it\n"
        "; (applied in the pump, exactly as before). If the driver refuses the\n"
        "; pin (no WaveRT support, format refused, pin busy) that leg logs the\n"
        "; reason and falls back to WASAPI exclusive automatically. Use\n"
        ";   twinspeak --list-ks\n"
        "; to see every WDM-KS render filter with its pins and formats -- the\n"
        "; SAME name needles as below match the KS filters.\n"
        "backend1 = wasapi\n"
        "backend2 = wasapi\n"
        "ks_period_frames = 256\n"
        "ks_periods = 3\n"
        "\n"
        "; CLOCK SYNC (v6.0, ASIO-strict): the audio path is BIT-EXACT -- there\n"
        "; is no resampler and no pitch control anywhere in twinspeak. Clock\n"
        "; drift between the cable's virtual clock and each device crystal is\n"
        "; absorbed by two device-master rules at the buffer switch:\n"
        ";   * a buffer switch the ring cannot fill REPLAYS the last played\n"
        ";     buffer (the ASIO underrun convention) instead of a silence hole;\n"
        ";   * ring excess above the natural sawtooth (cable faster than device)\n"
        ";     is dropped OLDEST-first, at most once per 5 s per leg, logged.\n"
        "; The health line shows the measured offset ('off +N ppm'). At crystal\n"
        "; level both events are minutes apart. If 'off' sits far from 0, the\n"
        "; one-shot hint prints the value -- enable Clock Correction for the\n"
        "; cable in the VAC Control Panel and trim it until 'off' reads ~0; a\n"
        "; bit-exact path + a corrected cable clock = a silent path forever.\n"
        "; v6.6.8: clock_sync is part of the engine now -- ALWAYS ON.\n"
        "\n"
        "; DRIFT WARDEN (v6.4) -- the timestamp system. Every capture burst is\n"
        "; stamped into a frame-interval ledger: each leg tracks the capture\n"
        "; sequence number of its play boundary, advanced by exactly the real\n"
        "; frames that cross it (pops, gated drops, warden skips; replays and\n"
        "; deliberate pads never advance it). The difference of the two\n"
        "; boundaries IS the measured delay between the two speakers, printed\n"
        "; every second as 'drift' on the health line -- pure frame arithmetic,\n"
        "; no clock involved, so it cannot lie. The drift column INCLUDES your\n"
        "; own delay1_ms/delay2_ms: at delay1_ms = 60 it reads about -65 (the\n"
        "; few-ms remainder is the legs' natural start asymmetry) -- that\n"
        "; offset IS the alignment your ear asked for, not a fault. The 'err'\n"
        "; column beside it is the distance from the calibrated baseline --\n"
        "; the number the warden acts on; err ~ +0.0 means the path is pinned.\n"
        "; For ~9 s after boot the warden\n"
        "; CALIBRATES: it averages the settled drift into a baseline (your\n"
        "; delay1_ms/delay2_ms calibration is inside that number). After that,\n"
        "; a drift that leaves the baseline by >= drift_correct_ms (default 10)\n"
        "; for 3 consecutive seconds is CORRECTED automatically (a RESYNC), at\n"
        "; most once per drift_interval_ms (default 5000): the late leg's ring\n"
        "; gives back its excess (a bit-exact\n"
        "; skip -- the same operation as the clock rule above) and the early\n"
        "; leg is held (its last buffer replays in place until the depth\n"
        "; returns). No sample value is ever touched. Two honest limits:\n"
        ";   * the ledger sees the HOST pipeline only -- anything below the USB\n"
        ";     packetizer (the S5's own receiver FIFO / amp silicon) is\n"
        ";     invisible. If the drift column stays flat while your ears hear\n"
        ";     drift, the offset is device silicon and no host trigger exists --\n"
        ";     the log then proves exactly that.\n"
        ";   * a correction is a skip or a stutter of drift_correct_ms at most\n"
        ";     (rate-limited): that is the price of re-pinning a bit-exact path.\n"
        "; v6.6.8: drift_guard is part of the engine now -- ALWAYS ON (the\n"
        "; ledger + the drift column were never optional anyway).\n"
        "drift_correct_ms = 5\n"
        "\n"
        "; drift_interval_ms (v6.5.8, default retuned in v6.5.10): minimum\n"
        "; spacing between warden corrections (the RESYNC interval). Default\n"
        "; 5000 = the five-second resync. Since v6.5.12 the clamp floor is\n"
        "; 2000 (range 2000..60000), so a long session can chase drift on a\n"
        "; 2 s clock. Between corrections the drift error can keep\n"
        "; ramping, so latency-critical playback (rhythm games) wants this as\n"
        "; tight as tolerable: each correction is a small audible skip of up\n"
        "; to drift_correct_ms, so tighter spacing = more frequent, smaller\n"
        "; corrections. Raise it only if the skips ever bother you. Also (in\n"
        "; v6.5.8): any device reopen (sleep/wake, USB re-enumeration) now\n"
        "; invalidates the drift baseline and re-arms it once the path settles\n"
        "; (~9 s: 5 s settle + a 4-sample average) -- a boot-time-only\n"
        "; baseline went stale against exactly those events.\n"
        "drift_interval_ms = 5000\n"
        "\n"
        "; v7.1.14: the FIRST SECOND of every run corrects at 10x this\n"
        "; cadence -- 100 ms ledger samples, a 300 ms decision, 500 ms\n"
        "; spacing, hardcoded -- against the ini's own delay model, so a\n"
        "; startup residual is re-pinned before the baseline can absorb it.\n"
        "\n"
        "; STALL RESYNC (v6.5.11, always on -- no ini key): if a leg's device\n"
        "; thread starves (an interrupt storm, a 100%% CPU hog), the shortfall\n"
        "; path masks it with the fragment-loop buzz and the leg falls behind.\n"
        "; The health loop recovers both ways: stale backlog above the leg's\n"
        "; anchor is discarded immediately (bit-exact skip, logged as 'stall\n"
        "; resync'), and starvation lasting 3 consecutive seconds reopens the\n"
        "; leg fresh -- which also resets whatever the device's own silicon\n"
        "; accumulated during the storm.\n"
        "\n"
        "; KS LAPSE GUARD (v6.5.13, always on): if the KS pacing thread is\n"
        "; ever descheduled past one full WaveRT loop lap, the play\n"
        "; register's residue silently loses laps and the DAC circles the\n"
        "; loop re-reading stale audio -- a reverberated sawing mixed into\n"
        "; the program, invisible to every other meter. The guard cross-\n"
        "; checks the DAC's wall-exact advance against the register residue\n"
        "; on every 500 us poll and re-pins the write cursor to play+lead on\n"
        "; any loss (bit-exact skip, logged as 'KS lapse', counted in the\n"
        "; devbuf line). KS legs also self-heal cleanly across suspend now.\n"
        "\n"
        "; DEEP RESYNC (v6.5.12, optional): the S5's below-packetizer layer\n"
        "; (endpoint FIFO + amp silicon) can deepen silently over a long\n"
        "; session -- the drift ledger stays pinned while the center audibly\n"
        "; lags again. No host meter can see that layer; a scheduled fresh\n"
        "; reopen of the center leg re-primes it. Each resync is a brief\n"
        "; center gap and the warden re-arms ~9 s later; your delay1_ms\n"
        "; calibration holds across it. 0 = off (default); try 30 for long\n"
        "; movie sessions.\n"
        "deep_resync_min = 0\n"
        "\n"
        "; POWER GUARD (v6.5.2) -- sleep / Modern-Standby survival. Field log\n"
        "; (Lenovo M720Q): after sleep the S5's stream consumed ~42%% of the\n"
        "; cable's rate with a FRESH heartbeat -- the silence watchdog is blind\n"
        "; to that, and the path glitched forever in a drop/re-pin limit cycle.\n"
        "; With the guard ON: suspend parks the pumps; resume rebuilds the\n"
        "; capture pin + both legs (and replays the startup chime); any leg\n"
        "; whose measured clock offset ('off') leaves +-10%% for 2 s is reopened\n"
        "; fresh -- that catches the degraded sleep-ENTRY window too, which\n"
        "; fires before the suspend broadcast. Every action is logged.\n"
        "; v6.6.8: power_guard is part of the engine now -- ALWAYS ON.\n"
        "\n"
        "; CENTER PRESENCE (v6.1) -- out2 plays the center channel ONLY while it\n"
        "; is genuinely present. Two gates, both default ON:\n"
        ";   center_gate -- a block whose ch3 is digital zero emits NOTHING but\n"
        ";     the keep-alive tone: if the center channel is ever zero, every\n"
        ";     channel of that sample stays zero. Never a substitute.\n"
        ";   center_upmix_guard -- when the SOURCE has no center of its own (a\n"
        ";     stereo app on the 8ch cable), the Windows engine fills FC with\n"
        ";     a*FL + b*R at the cable's playback side -- BEFORE twinspeak can\n"
        ";     capture it, so channel muting cannot remove it (this is the 'if\n"
        ";     the center channel is absent it mixes left and right instead'\n"
        ";     issue). The guard recognizes that exact fingerprint per block\n"
        ";     (least-squares ch3 ~ a*ch1 + b*ch2, residual < -30 dB, gains in\n"
        ";     the upmix band, fronts active) and mutes those blocks too -- the\n"
        ";     center speaker stays on keep-alive only. Genuine 5.1 center is an\n"
        ";     INDEPENDENT track and never matches; it always passes. The health\n"
        ";     line counts guarded blocks ('cm') -- with real 5.1 content it\n"
        ";     must read 0. v6.6.8: center_gate and center_upmix_guard are\n"
        ";     part of the engine now -- ALWAYS ON.\n"
        "\n"
        "; ROUTING GUARANTEE (zero duplication, by construction):\n"
        ";   out1 gets ONLY ch1 (FL) and ch2 (FR) plus gain_lfe * ch4 (LFE)\n"
        ";   folded onto both. The 5.1 stream's center, discrete sub and\n"
        ";   surround channels are DRIVEN DIGITAL SILENCE, so no layer (Windows\n"
        ";   engine, miniaudio converter, Speaker Fill) has any signal to\n"
        ";   upmix into a fake center. out2 gets ONLY ch3 (center): every other\n"
        ";   input channel is EXPLICITLY MUTED in the out2 path -- out2 is not\n"
        ";   a clone of the program audio. The startup banner prints the route\n"
        ";   map on every launch.\n"
        ";\n"
        "; NOTE: output2 always sums the keep-alive sine (BUILT IN, v7.1.7:\n"
        "; 1 Hz @ 0.01%%) onto both channels, every\n"
        "; frame, so the center device NEVER sees digital silence -- and when\n"
        "; the center gates mute a block, the keep-alive is ALL that block\n"
        "; carries. The out2 pump also runs FIRST and at the higher priority\n"
        "; (out2_first): the center leg gets the freshest data and the most\n"
        "; scheduling attention; delay1_ms/delay2_ms then trim the alignment.\n"
        "\n"
        "; v7.0.4: gui_theme = the control window's color theme (0..7), saved\n"
        "; automatically whenever the THEME button cycles it. 0 = DARK CYAN,\n"
        "; the classic. Delete the line to fall back to the default.\n"
        "gui_theme = 0\n"
        "\n"
        "; v7.1.2: startup_engine = when the startup launch (--tray, the\n"
        "; sign-in path) starts TRAY-ONLY (v7.1.6: no window exists until\n"
        "; you click the tray icon), the engine starts BY ITSELF -- no\n"
        "; START press needed. off = the old behavior (the engine waits\n"
        "; for START).\n"
        "startup_engine = on\n");
    fclose(f);
}

static float clampMs(float ms) {
    return ms < 2.0f ? 2.0f : (ms > 100.0f ? 100.0f : ms);
}

// v6.6.0: the post-parse clamps, shared by loadIni AND the GUI's
// apply-from-controls path (so hand-typed values get the same guardrails
// and the same log lines whichever way they arrive).
static void cfgSanitize() {
    // legacy fallback: buffer_size applies to any leg without an explicit outXms
    if (!g_cfg.haveOut1) g_cfg.out1Ms = g_cfg.legacyBufferMs;
    if (!g_cfg.haveOut2) g_cfg.out2Ms = g_cfg.legacyBufferMs;
    g_cfg.out1Ms = clampMs(g_cfg.out1Ms);
    g_cfg.out2Ms = clampMs(g_cfg.out2Ms);
    g_cfg.capMs  = clampMs(g_cfg.capMs);
    if (g_cfg.delay1Ms < 0.0f)   g_cfg.delay1Ms = 0.0f;
    if (g_cfg.delay1Ms > 1000.0f) g_cfg.delay1Ms = 1000.0f;
    if (g_cfg.delay2Ms < 0.0f)   g_cfg.delay2Ms = 0.0f;
    if (g_cfg.delay2Ms > 1000.0f) g_cfg.delay2Ms = 1000.0f;
    if (g_cfg.ring1Ms >= 0.0f) g_cfg.ring1Ms = clampMs(g_cfg.ring1Ms);
    if (g_cfg.ring2Ms >= 0.0f) g_cfg.ring2Ms = clampMs(g_cfg.ring2Ms);
    if (g_cfg.driftIntervalMs < 2000)  g_cfg.driftIntervalMs = 2000;   // v6.5.12: was 5000
    if (g_cfg.driftIntervalMs > 60000) g_cfg.driftIntervalMs = 60000;
    if (g_cfg.deepResyncMin > 0 && g_cfg.deepResyncMin < 5) g_cfg.deepResyncMin = 5;
    if (g_cfg.deepResyncMin > 1440) g_cfg.deepResyncMin = 1440;
    // v6.3 backend normalization: "ks" (aliases: wdm-ks / wdmks) selects the
    // kernel-streaming pin; anything else stays WASAPI.
    for (int i = 0; i < 2; ++i) {
        std::string& b = (i == 0) ? g_cfg.backend1 : g_cfg.backend2;
        if (b == "ks" || b == "wdm-ks" || b == "wdmks" || b == "wdm_kernel_streaming" ||
            b == "kernel-streaming" || b == "kernelstreaming") b = "ks";
        else b = "wasapi";
    }
    // v6.3.1: the clamps are now LOGGED -- a silently-ignored ini value looks
    // exactly like a broken feature from the driver's seat.
    if (g_cfg.ksPeriodFrames < 32 || g_cfg.ksPeriodFrames > 4096) {
        tlog("[twinspeak] config        : ks_period_frames %u -> %u (usable bite range is 32..4096)\n",
             g_cfg.ksPeriodFrames, g_cfg.ksPeriodFrames < 32 ? 32u : 4096u);
        g_cfg.ksPeriodFrames = g_cfg.ksPeriodFrames < 32 ? 32 : 4096;
    }
    if (g_cfg.ksPeriods < 2 || g_cfg.ksPeriods > 8) {
        tlog("[twinspeak] config        : ks_periods %u -> %u -- the WaveRT loop needs 2..8 periods; a\n"
             "                            1-period loop has NO write lead (the DAC eats whatever sits in\n"
             "                            the buffer the moment the pacer hiccups), above 8 just wastes RAM\n",
             g_cfg.ksPeriods, g_cfg.ksPeriods < 2 ? 2u : 8u);
        g_cfg.ksPeriods = g_cfg.ksPeriods < 2 ? 2 : 8;
    }
    if (g_cfg.ksPeriodFrames < 128) {   // v6.5.14: field note, NOT a clamp
        tlog("[twinspeak] config        : ks_period_frames %u -- field note: bites under 128 frames pace on\n"
             "                            the edge (the lapse-guard lap window shrinks with the loop and\n"
             "                            64-frame loops buzzed in the field); 128+ is the steady zone\n",
             g_cfg.ksPeriodFrames);
    }
    if (g_cfg.driftCorrectMs < 2.0f)   g_cfg.driftCorrectMs = 2.0f;
    if (g_cfg.driftCorrectMs > 500.0f) g_cfg.driftCorrectMs = 500.0f;
}

// v7.0.4: the GUI theme persists across sessions -- loadIni (which runs long
// before the GUI exists) parks the parsed gui_theme here; guiMakeGdi applies it.
static int g_themeSaved = 0;

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
        if      (key == "out1ms")       { g_cfg.out1Ms = (float)atof(val.c_str()); g_cfg.haveOut1 = true; }
        else if (key == "out2ms")       { g_cfg.out2Ms = (float)atof(val.c_str()); g_cfg.haveOut2 = true; }
        else if (key == "ring1_ms")     { g_cfg.ring1Ms = (float)atof(val.c_str()); }
        else if (key == "ring2_ms")     { g_cfg.ring2Ms = (float)atof(val.c_str()); }
        else if (key == "cap_ms")            { g_cfg.capMs = (float)atof(val.c_str()); }
        else if (key == "buffer_size")  { g_cfg.legacyBufferMs = (float)atof(val.c_str()); }
        // v6.6.8: ten switches are now part of the engine itself -- always on,
        // no ini override, no UI pill. Old inis that still carry the keys load
        // cleanly: the lines are read and deliberately ignored.
        else if (key == "capture_exclusive" || key == "exclusive" ||
                 key == "align_modes"       || key == "clock_sync" ||
                 key == "auto_resync"       || key == "drift_guard" ||
                 key == "power_guard"       || key == "yield_default" ||
                 key == "center_gate"       || key == "center_upmix_guard" ||
                 key == "background"        || key == "pin_outputs") { }
        else if (key == "lfe_fold")     { g_cfg.lfeFold = !isOff(val); }
        else if (key == "input_device")   g_cfg.inputDevice = val;
        else if (key == "output_device1") g_cfg.outputDevice1 = val;
        else if (key == "output_device2") g_cfg.outputDevice2 = val;
        else if (key == "gain_l")         g_cfg.gainL = (float)atof(val.c_str());
        else if (key == "gain_r")         g_cfg.gainR = (float)atof(val.c_str());
        else if (key == "gain_lfe")       g_cfg.gainLfe = (float)atof(val.c_str());
        else if (key == "gain_center")    g_cfg.gainCenter = (float)atof(val.c_str());
        else if (key == "center_eq")      g_cfg.centerEq     = !isOff(val);          // v6.5.18
        else if (key == "center_eq_freq") g_cfg.centerEqFreq = (float)atof(val.c_str());
        else if (key == "center_eq_gain") g_cfg.centerEqGain = (float)atof(val.c_str());
        else if (key == "center_eq_q")    g_cfg.centerEqQ    = (float)atof(val.c_str());
        // v7.1.8: the five EQ save slots (eq_slotN_on / _freq / _gain / _q)
        else if (key.rfind("eq_slot", 0) == 0 && key.size() > 8 &&
                 key[7] >= '1' && key[7] <= '5') {
            const int n = key[7] - '1';
            const std::string t = key.substr(8);
            if      (t == "_on")   g_cfg.eqSlotOn[n] = !isOff(val);
            else if (t == "_freq") g_cfg.eqSlotF[n]  = (float)atof(val.c_str());
            else if (t == "_gain") g_cfg.eqSlotG[n]  = (float)atof(val.c_str());
            else if (t == "_q")    g_cfg.eqSlotQ[n]  = (float)atof(val.c_str());
        }
        else if (key == "delay1_ms")      g_cfg.delay1Ms = (float)atof(val.c_str());
        else if (key == "delay2_ms")      g_cfg.delay2Ms = (float)atof(val.c_str());
        else if (key == "volume_follow")  g_cfg.volumeFollow = !isOff(val);
        else if (key == "out2_first")     g_cfg.out2First    = !isOff(val);
        else if (key == "volume_endpoint") g_cfg.volumeEndpoint = val;
        else if (key == "chimes")         g_cfg.chimes        = !isOff(val);   // v6.2
        else if (key == "chime_gain")     g_cfg.chimeGain     = (float)atof(val.c_str()); // v6.2
        else if (key == "backend1")       g_cfg.backend1      = lower(val);    // v6.3
        else if (key == "backend2")       g_cfg.backend2      = lower(val);    // v6.3
        else if (key == "ks_period_frames") g_cfg.ksPeriodFrames = (unsigned)atoi(val.c_str()); // v6.3
        else if (key == "ks_periods")      g_cfg.ksPeriods      = (unsigned)atoi(val.c_str()); // v6.3
        else if (key == "drift_correct_ms") g_cfg.driftCorrectMs = (float)atof(val.c_str()); // v6.4
        else if (key == "drift_interval_ms") g_cfg.driftIntervalMs = (unsigned)atoi(val.c_str()); // v6.5.8
        else if (key == "deep_resync_min")   g_cfg.deepResyncMin   = (unsigned)atoi(val.c_str()); // v6.5.12
        else if (key == "gui_theme") {
            g_themeSaved = atoi(val.c_str());      // v7.0.4: theme persistence
            if (g_themeSaved < 0 || g_themeSaved > 10) g_themeSaved = 0;   // v7.1.8: 11 themes
        }
        else if (key == "startup_engine") g_cfg.startupEngine = !isOff(val);   // v7.1.2
    }
    fclose(f);
    cfgSanitize();
    return true;
}

// --------------------------------------------------------- enumeration ------
static void listDevices() {
    ma_backend be[] = { ma_backend_wasapi };
    ma_context ctx;
    if (ma_context_init(be, 1, nullptr, &ctx) != MA_SUCCESS) {
        tlog( "[twinspeak] context init failed\n");
        return;
    }
    ma_device_info* pb = nullptr; ma_uint32 npb = 0;
    ma_device_info* cp = nullptr; ma_uint32 ncp = 0;
    if (ma_context_get_devices(&ctx, &pb, &npb, &cp, &ncp) != MA_SUCCESS) {
        tlog( "[twinspeak] enumeration failed\n");
        ma_context_uninit(&ctx);
        return;
    }
    tlog("[twinspeak] backend: %s\n\n", ma_get_backend_name(ctx.backend));
    tlog("PLAYBACK devices (%u):\n", npb);
    for (ma_uint32 i = 0; i < npb; ++i) {
        tlog("  [%u] %s%s\n", i, pb[i].name, pb[i].isDefault ? "   (default)" : "");
        for (ma_uint32 k = 0; k < pb[i].nativeDataFormatCount && k < 4; ++k)
            tlog("        native: %uch @ %u Hz, %s\n", pb[i].nativeDataFormats[k].channels,
                   pb[i].nativeDataFormats[k].sampleRate,
                   ma_get_format_name(pb[i].nativeDataFormats[k].format));
    }
    tlog("\nCAPTURE devices (%u):\n", ncp);
    for (ma_uint32 i = 0; i < ncp; ++i) {
        tlog("  [%u] %s%s\n", i, cp[i].name, cp[i].isDefault ? "   (default)" : "");
        for (ma_uint32 k = 0; k < cp[i].nativeDataFormatCount && k < 4; ++k)
            tlog("        native: %uch @ %u Hz, %s\n", cp[i].nativeDataFormats[k].channels,
                   cp[i].nativeDataFormats[k].sampleRate,
                   ma_get_format_name(cp[i].nativeDataFormats[k].format));
    }
    ma_context_uninit(&ctx);
}

static bool pickDevice(ma_context* ctx, ma_device_type type, const std::string& needle,
                       ma_device_id* outId, std::string* outName) {
    ma_device_info* pb = nullptr; ma_uint32 npb = 0;
    ma_device_info* cp = nullptr; ma_uint32 ncp = 0;
    if (ma_context_get_devices(ctx, &pb, &npb, &cp, &ncp) != MA_SUCCESS) return false;
    ma_device_info* arr = (type == ma_device_type_playback) ? pb : cp;
    ma_uint32      cnt  = (type == ma_device_type_playback) ? npb : ncp;
    int best = -1;
    for (ma_uint32 i = 0; i < cnt; ++i) {
        if (!containsCI(arr[i].name, needle)) continue;
        if (best < 0) best = (int)i;
        else if (arr[i].isDefault && !arr[best].isDefault) best = (int)i;
    }
    if (best < 0) return false;
    *outId = arr[best].id;
    *outName = arr[best].name;
    return true;
}

// v6.5.16: the CURRENT default render endpoint's friendly name, read
// directly (one endpoint, one property read, eConsole role). v6.5.14 walked
// EVERY render endpoint per health tick -- churn every enumerating process
// contended with. One name, one read.
static bool defaultRenderName(std::string* outName) {
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                kIID_IMMDeviceEnumerator, (void**)&en)) || !en)
        return false;
    IMMDevice* dev = nullptr;
    bool ok = false;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) && dev) {
        IPropertyStore* ps = nullptr;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps)) && ps) {
            PROPVARIANT var; PropVariantInit(&var);
            if (SUCCEEDED(ps->GetValue(kPKEY_Device_FriendlyName, &var)) &&
                var.vt == VT_LPWSTR && var.pwszVal) {
                *outName = wideToNarrow(var.pwszVal);
                ok = !outName->empty();
            }
            PropVariantClear(&var);
            ps->Release();
        }
        dev->Release();
    }
    en->Release();
    return ok;
}

// v6.5.16: how many FOREIGN audio sessions are ACTIVE on the render endpoint
// matching the needle? Consumed by the yield-release path: an app that
// played to this device while it WAS the default does not re-route the
// instant the default moves -- re-grabbing the DAC under a live stream
// produced the field-reported doubled audio and wedged miniaudio apps.
// The release path re-grabs only a QUIET device.
// Returns >= 0 foreign active sessions; -1 the probe failed (caller fails
// OPEN -- pre-v6.5.16 behavior, logged).
static int foreignActiveSessions(const std::string& nameNeedle) {
    const std::wstring wNeedle = narrowToWide(nameNeedle);
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                kIID_IMMDeviceEnumerator, (void**)&en)) || !en)
        return -1;
    int count = -1;
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
        count = 0;   // store walk works; from here zero means "no activity found"
        UINT n = 0;
        if (SUCCEEDED(col->GetCount(&n))) {
            for (UINT i = 0; i < n; ++i) {
                IMMDevice* dev = nullptr;
                if (FAILED(col->Item(i, &dev)) || !dev) continue;
                IPropertyStore* ps = nullptr;
                bool match = false;
                if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps)) && ps) {
                    PROPVARIANT var; PropVariantInit(&var);
                    if (SUCCEEDED(ps->GetValue(kPKEY_Device_FriendlyName, &var)) &&
                        var.vt == VT_LPWSTR && var.pwszVal)
                        match = wContainsCI(var.pwszVal, wNeedle);
                    PropVariantClear(&var);
                    ps->Release();
                }
                if (match) {
                    IAudioSessionManager2* sm2 = nullptr;
                    if (SUCCEEDED(dev->Activate(kIID_IAudioSessionManager2, CLSCTX_ALL,
                                                nullptr, (void**)&sm2)) && sm2) {
                        IAudioSessionEnumerator* se = nullptr;
                        if (SUCCEEDED(sm2->GetSessionEnumerator(&se)) && se) {
                            int sn = 0;
                            if (SUCCEEDED(se->GetCount(&sn))) {
                                for (int s = 0; s < sn; ++s) {
                                    IAudioSessionControl* sc = nullptr;
                                    if (SUCCEEDED(se->GetSession(s, &sc)) && sc) {
                                        AudioSessionState st = AudioSessionStateInactive;
                                        if (SUCCEEDED(sc->GetState(&st)) && st == AudioSessionStateActive)
                                            ++count;
                                        sc->Release();
                                    }
                                }
                            }
                            se->Release();
                        }
                        sm2->Release();
                    }
                    dev->Release();
                    break;   // first name match only -- the leg-opening rule
                }
                dev->Release();
            }
        }
        col->Release();
    }
    en->Release();
    return count;
}

// ---------------------------------------------------------- device init -----
static bool initLegDevice(int idx, ma_context* ctx, const ma_device_id* id,
                          ma_uint32 periodMs, bool wantExcl) {
    Leg& L = g_leg[idx];
    ma_device_config pc = ma_device_config_init(ma_device_type_playback);
    pc.sampleRate = kSampleRate;
    pc.periodSizeInMilliseconds = periodMs;
    pc.periods = 2;
    pc.playback.pDeviceID = id;
    pc.playback.format = ma_format_f32;
    pc.playback.channels = (idx == 0) ? kOutCh1 : kOutCh2;
    pc.playback.shareMode = wantExcl ? ma_share_mode_exclusive : ma_share_mode_shared;
    pc.dataCallback = (idx == 0) ? playCallback1 : playCallback2;
    pc.wasapi.usage = ma_wasapi_usage_pro_audio;
    ma_result r = ma_device_init(ctx, &pc, &L.dev);
    if (r != MA_SUCCESS) {
        tlog( "[twinspeak] leg%d: %s init failed: %s\n", idx + 1,
                wantExcl ? "EXCLUSIVE" : "shared", ma_result_description(r));
        return false;
    }
    L.isExclusive = wantExcl;
    L.id = *id;
    return true;
}

// ------------------------------------------------- v3.8 watchdog restarts --
// The "output shut off by itself" class: a device thread silently stops
// (endpoint invalidated, USB replug, driver reset). The health loop reopens
// the leg BY NAME (fresh endpoint id), previous share mode first, then
// shared; silent retries until the state changes or it succeeds.
static bool tryRestartLeg(int idx, ma_context* ctx, ma_uint32 periodMs,
                          const char* why = "device thread silent > 5 s") {
    Leg& L = g_leg[idx];
    static unsigned long long lastLog[2] = { 0, 0 };
    const unsigned long long now = GetTickCount64();
    L.lastReopenMs = now;   // v6.4: the drift warden pauses while a leg settles
    if (now - lastLog[idx] >= 10000) {
        tlog( "[twinspeak] leg%d WATCHDOG : %s -- reopening \"%s\"\n",
                idx + 1, why, L.name.c_str());
        lastLog[idx] = now;
    }
    const char* needle = (idx == 0) ? g_cfg.outputDevice1.c_str() : g_cfg.outputDevice2.c_str();

    // v6.3: a KS leg reopens on its own transport first -- close pin+thread,
    // open a fresh KS pin. Only if the driver now refuses does it step down
    // to the WASAPI path below (and stay there for subsequent restarts).
    if (L.backend == 1) {
        ksCloseLeg(idx);
        if (ksOpenLeg(idx, needle)) {
            L.name = L.ks.filterName;
            L.ks.thread = CreateThread(nullptr, 0, ksThread, (LPVOID)(INT_PTR)idx, 0, nullptr);
            if (L.ks.thread) {
                hbOut(idx);
                g_driftRecalReq.store(true, std::memory_order_relaxed);   // v6.5.8: baseline re-arm
                tlog( "[twinspeak] leg%d WATCHDOG : reopened \"%s\" (WDM-KS)\n",
                        idx + 1, L.ks.filterName.c_str());
                if (idx == 1) armChime(false);   // v6.2: signature blip -- the center leg is back
                return true;
            }
            ksCloseLeg(idx);
        }
        tlog( "[twinspeak] leg%d WATCHDOG : WDM-KS reopen failed -- falling back to WASAPI for this leg\n", idx + 1);
        L.backend = 0;
        L.isExclusive = g_cfg.exclusive;   // the WASAPI fallback starts exclusive again
    }

    if (L.dev.pContext) ma_device_uninit(&L.dev);
    memset(&L.dev, 0, sizeof(L.dev));
    ma_device_id id; std::string nm;
    if (!pickDevice(ctx, ma_device_type_playback, needle, &id, &nm)) return false;
    bool ok = initLegDevice(idx, ctx, &id, periodMs, L.isExclusive);
    if (!ok && L.isExclusive) ok = initLegDevice(idx, ctx, &id, periodMs, false);
    if (!ok) return false;
    if (ma_device_start(&L.dev) != MA_SUCCESS) return false;
    L.name = nm;
    L.backend = 0;
    hbOut(idx);
    g_driftRecalReq.store(true, std::memory_order_relaxed);   // v6.5.8: baseline re-arm
    // v4.0: a reopen can negotiate a different period (exclusive->shared
    // fallback); the ring floor and the v6.0 gated-drop line follow the
    // REAL period.
    g_periodFrames[idx] = L.dev.playback.internalPeriodSizeInFrames
                        ? (size_t)L.dev.playback.internalPeriodSizeInFrames
                        : g_periodFrames[idx];
    // v3.9: re-pin the endpoint volume right here, on the main thread that
    // owns the reopen (the watcher only enforces level/mute). The endpoint
    // id can change across a re-enumeration, so resolve it fresh.
    if (g_cfg.pinOutputs) {
        if (g_pin[idx].vol) { g_pin[idx].vol->Release(); g_pin[idx].vol = nullptr; }
        g_pin[idx].id = L.id.wasapi;
        if (L.id.wasapi[0]) {
            g_pin[idx].vol = endpointVolFromIdString(L.id.wasapi);
            if (g_pin[idx].vol) {
                g_pin[idx].vol->SetMasterVolumeLevelScalar(1.0f, nullptr);
                g_pin[idx].vol->SetMute(FALSE, nullptr);
                tlog( "[twinspeak] out%d: endpoint re-acquired after reopen -- pinned 100%%\n", idx + 1);
            }
        }
    }
    tlog( "[twinspeak] leg%d WATCHDOG : reopened \"%s\" (%s)\n",
            idx + 1, nm.c_str(), L.isExclusive ? "exclusive" : "shared");
    if (idx == 1) armChime(false);   // v6.2: signature blip -- the center leg is back
    return true;
}

static bool tryRestartCap(ma_context* ctx) {
    static unsigned long long lastLog = 0;
    const unsigned long long now = GetTickCount64();
    if (now - lastLog >= 10000) {
        tlog( "[twinspeak] capture WATCHDOG: device thread silent >5 s -- reopening \"%s\"\n",
                g_capNeedle.c_str());
        lastLog = now;
    }
    if (g_capDev.pContext) ma_device_uninit(&g_capDev);
    memset(&g_capDev, 0, sizeof(g_capDev));
    ma_device_id id; std::string nm;
    if (!pickDevice(ctx, ma_device_type_capture, g_capNeedle, &id, &nm)) return false;
    ma_device_config cc = ma_device_config_init(ma_device_type_capture);
    cc.sampleRate = kSampleRate;
    cc.periodSizeInMilliseconds = (ma_uint32)(g_capPeriodMsF + 0.5f);
    cc.periods = 2;
    cc.dataCallback = capCallback;
    cc.capture.pDeviceID = &id;
    cc.capture.format = ma_format_f32;
    cc.capture.channels = kInCh;
    cc.capture.shareMode = g_capWantExcl ? ma_share_mode_exclusive : ma_share_mode_shared;
    cc.wasapi.usage = ma_wasapi_usage_pro_audio;
    ma_result r = ma_device_init(ctx, &cc, &g_capDev);
    if (r != MA_SUCCESS && g_capWantExcl) {
        cc.capture.shareMode = ma_share_mode_shared;
        r = ma_device_init(ctx, &cc, &g_capDev);
    }
    if (r != MA_SUCCESS) return false;
    g_capIsExcl = (cc.capture.shareMode == ma_share_mode_exclusive);
    if (ma_device_start(&g_capDev) != MA_SUCCESS) return false;
    g_hbCap.store(GetTickCount64(), std::memory_order_relaxed);
    g_driftRecalReq.store(true, std::memory_order_relaxed);   // v6.5.8: baseline re-arm
    tlog( "[twinspeak] capture WATCHDOG: reopened \"%s\" (%s)\n",
            nm.c_str(), g_capIsExcl ? "exclusive" : "shared");
    return true;
}

// ------------------------------------------------------- diagnostics --------
static void printLegDevice(int idx) {
    Leg& L = g_leg[idx];
    const double burst = (double)g_capBurstFrames * 1000.0 / (double)kSampleRate;
    const double ring  = (double)L.targetFrames * 1000.0 / (double)kSampleRate;
    if (L.backend == 1) {
        // v6.3: WDM-KS leg -- the device bite is ks_period_frames and the
        // transport latency is the write lead (+ the DAC's hardware FIFO).
        const KsOut& K = L.ks;
        const double pms   = (double)K.periodFrames * 1000.0 / (double)kSampleRate;
        const double lead  = ((double)K.leadFrames + 0.5 * (double)K.periodFrames)
                           * 1000.0 / (double)kSampleRate;
        tlog("[twinspeak] leg%d device     : WDM-KS WaveRT polling, \"%s\", pin %d, %s %dch @ %u Hz | loop %u fr, bite %u fr (~%.2f ms), lead ~%.1f ms%s | ring %.1f ms%s | est path ~%.1f ms (ring + KS lead + capture)\n",
               idx + 1, K.filterName.c_str(), K.pinId, K.fmtName.c_str(), K.outCh,
               (unsigned)kSampleRate,
               (unsigned)K.loopFrames, (unsigned)K.periodFrames, pms, lead,
               K.posMapped ? ", pos REGISTER" : ", pos IOCTL",
               ring, L.delayFrames ? " (incl. delay)" : "",
               ring + lead + burst);
        if (!K.posMapped)
            tlog("[twinspeak] leg%d info       : driver has no mappable position register -- pacing polls\n"
                        "            KSPROPERTY_AUDIO_POSITION (one syscall per poll; still direct-to-DAC,\n"
                        "            still far below the WASAPI shared path).\n", idx + 1);
        return;
    }
    const ma_device* d = &L.dev;
    const ma_uint32  pf = d->playback.internalPeriodSizeInFrames;
    const ma_uint32  sr = d->playback.internalSampleRate;
    const double     pms = (sr != 0 && pf != 0) ? (pf * 1000.0 / sr) : 0.0;
    const double     devms = pms * (double)d->playback.internalPeriods;
    tlog("[twinspeak] leg%d device     : %s, %uch @ %u Hz, period %u fr x%u (~%.2f ms) | ring %.1f ms%s | est path ~%.1f ms (ring + device + capture)\n",
           idx + 1, ma_get_format_name(d->playback.internalFormat),
           (unsigned)d->playback.internalChannels, (unsigned)sr,
           (unsigned)pf, (unsigned)d->playback.internalPeriods, devms, ring,
           L.delayFrames ? " (incl. delay)" : "",
           ring + devms + burst);
    // v5.0: with the strict split this is INFORMATION, not a warning. The FC
    // and discrete-LFE channels are DRIVEN DIGITAL SILENCE, so a stereo (or
    // any downmixed) endpoint has literally nothing to upmix -- the center
    // physically cannot appear on out1 no matter how the endpoint is set up.
    if (idx == 0 && d->playback.internalChannels != kOutCh1)
        tlog( "[twinspeak] leg1 info       : endpoint mix is %uch, not 6ch -- fine with the strict split:\n"
                        "            out1 drives FC/LFE as digital silence, so no layer (engine,\n"
                        "            miniaudio converter, Speaker Fill) has anything to upmix.\n",
                (unsigned)d->playback.internalChannels);
    // v6.0: a non-48k endpoint means miniaudio inserts a RESAMPLER on this
    // leg -- a pitch change, which twinspeak itself never does. Say it loud.
    if (sr != kSampleRate)
        tlog( "[twinspeak] leg%d WARNING   : endpoint runs at %u Hz -- miniaudio resamples 48000->%u on\n"
                        "            this leg (a pitch change; v6.0's own path is bit-exact and never\n"
                        "            resamples). Fix: set this device's default format to 24-bit\n"
                        "            48000 Hz in Windows sound settings.\n",
                idx + 1, (unsigned)sr, (unsigned)sr);
}

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
static int engineMain(const std::string& iniPath, bool fgForce,
                      bool allowMulti, bool guiMode);

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
    IDC_LOG = 204, IDC_REFRESH = 205, IDC_THEME = 206, IDC_TICK = 207,
    // page: devices
    IDC_INDEV = 300, IDC_OUT1, IDC_OUT2, IDC_VOLEP,
    IDC_GAIN_L, IDC_GAIN_R, IDC_GAIN_LFE, IDC_GAIN_CTR,
    IDC_TGL_LFEFOLD,
    // page: timing
    IDC_OUT1MS, IDC_OUT2MS, IDC_CAPMS, IDC_RING1, IDC_RING2,
    IDC_DELAY1, IDC_DELAY2, IDC_TGL_OUT2FIRST,   // v7.0.0: IDC_TGL_PINOUT retired (integrated)
    // page: transport
    IDC_KSPF, IDC_KSPD,         // v7.1.7: IDC_BE1/IDC_BE2 retired -- the backend
                                // keys live in the ini only (wasapi exclusive | ks)
    // page: settings (v7.1.7: sync + startup + chime/keepalive, one page)
    IDC_TGL_STARTUP,            // v6.6.6: Run-key pill
    IDC_DCORR, IDC_DINTERVAL, IDC_DEEPRESYNC,
    IDC_TGL_VOLFOLLOW, IDC_TGL_CHIMES,
    IDC_CHIMGAIN,               // v7.1.7: IDC_KAHZ/IDC_KAGAIN retired (keepalive built in)
    // page: center eq
    IDC_TGL_EQ,
    IDC_SLOT1, IDC_SLOT2, IDC_SLOT3, IDC_SLOT4, IDC_SLOT5,   // v7.1.8: save slots
                                                             // (IDC_PRE_FLAT..IDC_PRE_WARM retired)
    IDC_EQSAVE,                 // v7.1.9: SAVE TO SLOT (writes the armed slot)
    IDC_SLD_FREQ, IDC_SLD_GAIN, IDC_SLD_Q,
    IDC_LBL_FREQ, IDC_LBL_GAIN, IDC_LBL_Q,
};

// ---- gui state -------------------------------------------------------------
static HWND      g_hwnd          = nullptr;
static HFONT     g_fUI = nullptr, g_fUIb = nullptr,
                 g_fSml = nullptr, g_fMono = nullptr,
                 g_fTitle = nullptr;   // v7.1.0: the bar's "TwinSpeak" title
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
static std::vector<HWND> g_pageHw[5];          // children per page (show/hide)
static HANDLE    g_engThread     = nullptr;
static std::atomic<int>  g_engState{0};        // 0 stopped, 1 starting, 2 running, 3 stopping
static std::atomic<int>  g_engRC{0};
static std::string g_logRing;                  // pending log lines (g_logCS-locked)
static std::string g_lastHealth;               // latest "out1 ..." health line
static std::atomic<bool> g_logDirty{false};
// v6.6.1: tray + startup
static NOTIFYICONDATAA g_nid = {};
static ULONG_PTR g_gdipToken = 0;             // v6.6.5: GDI+ session token
static bool g_trayVisible = false;             // icon currently in the tray
static bool g_trayHint    = false;             // one-time close balloon shown
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
static void trayAdd(bool hint);
static void trayRemove();
static bool guiCreateRealWindow();   // v7.1.6 tray-first: deferred build, on demand
// v6.6.1 tray context-menu commands
enum { IDM_TRAY_OPEN = 900, IDM_TRAY_START, IDM_TRAY_AUTORUN, IDM_TRAY_EXIT };

// v7.1.7: FIVE tabs. OTHER merged away: its startup pill AND its SYNC column
// (drift corrections) both live on SETTINGS now, next to the chime/keepalive
// block. DEVICES | TIMING | TRANSPORT | SETTINGS | CENTER EQ.
static const char* kTabNames[5] = {
    "DEVICES", "TIMING", "TRANSPORT", "SETTINGS", "CENTER EQ"
};

// v6.6.4: only a close button -- fixed-size layout, close already hides to the
// tray. v7.0.9: the caption itself is custom (see kCapH below); WS_CAPTION
// stays so the bar still answers HTCAPTION drag + the WS_SYSMENU right-click.
static const DWORD kGuiFrame = WS_CAPTION | WS_SYSMENU;

// v7.0.9 THE CUSTOM TITLEBAR: the DWM caption is gone entirely (its text, its
// close button, and the dark->light recolor that never took until a DWM
// round-trip). The top 31 px strip of the window is our own bar: theme colors
// painted directly, so a theme click recolors it the same instant as the
// client. WM_NCCALCSIZE insets the client top by kCapH -- every child
// coordinate keeps its old meaning. DWM's own frame drawing is left at the
// window-style default (v7.1.11: an explicit NCRENDERING DISABLED here killed
// the window shadow) -- the bar defends itself message-by-message instead
// (WM_NCPAINT, NCACTIVATE lParam -1, the 0x00AE/0x00AF swallow).
static const int  kCapH = 31;
static bool       g_capHot = false;   // mouse over the close glyph
static const wchar_t* kCapTitle = L"TwinSpeak";   // v7.1.0: the bar's product name
static RECT guiCapCloseRect(HWND w) {  // window coords, the X button slot
    RECT wr; GetWindowRect(w, &wr);
    const RECT r = { (wr.right - wr.left) - 46, 0, wr.right - wr.left, kCapH };
    return r;
}
// Paints the bar from a persistent memory buffer -- usable from any context
// (theme click, WM_NCPAINT, hover), never needs an invalidate.
// v7.1.3 THE WHOLE BAR IS GDI+: v7.0.9 built the bar from GDI FillRects and
// let one GDI+ guest draw the title; v7.1.3 hands every pixel to a SINGLE
// Gdiplus::Graphics -- the face is a subtle top-lit vertical gradient (the
// theme bg lifted toward white, melting back down to cBg by the bottom
// edge), the 1px bottom edge, the close hover face and the close glyph are
// GDI+ fills, all text renders AntiAliasGridFit (grayscale AA, same family
// as the buttons since v7.1.2), and the close glyph brightens to the theme
// accent while hovered.
// v7.1.4 THE GRAY HAIRLINES: shape antialiasing on integer-aligned fills
// let the face/edge rectangles pick up half-coverage boundary pixels -- a
// 1px gray line on some edges (the AA blend against whatever sat under
// the bar). The fills go through the INTEGER FillRectangle overloads with a
// 1px overscan; SmoothingMode stays OFF for them (the X is the only AA
// shape, drawn last, its AA blending against our own face).
// v7.1.4 THEME-AWARE TITLE: the hard-coded blue->teal gradient is retired.
// Light themes read "TwinSpeak" in the plain caption text color (the
// normal look); dark themes get a CYAN-GRAY vertical gradient (cyan-tinted
// gray, light at the top -> slate at the bottom), keyed to the theme by
// the bg's luminance instead of hard-coded stops.
// v7.1.8 THE BUFFERED BAR (the text/X flicker fix): every draw above went
// STRAIGHT to the window DC -- DWM could sample between the gradient pass,
// the hover face, the glyph and the title, so hover enter/leave, activation
// changes and theme clicks flashed the bar's text and X. The bar now renders
// into a persistent memory surface (same model as the frame/button buffers)
// and lands as ONE BitBlt -- no intermediate state ever exists on screen.
// v7.1.8 THE X IS BACK TO THE SYSTEM LOOK: the Marlett 'r' DrawString is
// retired. The close glyph is the modern thin caption X (two 10 px diagonal
// strokes, the DWM close button's shape since Vista) drawn with an
// antialiased GDI+ pen -- it still warms to the theme accent while hovered.
static HDC     g_barDc = nullptr;
static HBITMAP g_barBm = nullptr, g_barBmOld = nullptr;
static int     g_barW = 0, g_barH = 0;
static bool guiBarBufEnsure(int ww, int hh) {
    if (g_barDc && ww <= g_barW && hh <= g_barH) return true;
    if (g_barBmOld) {
        SelectObject(g_barDc, g_barBmOld);
        DeleteObject(g_barBm);
        g_barBm = nullptr; g_barBmOld = nullptr;
    }
    if (g_barDc) { DeleteDC(g_barDc); g_barDc = nullptr; }
    HDC sdc = GetDC(nullptr);
    g_barDc = CreateCompatibleDC(sdc);
    g_barBm = CreateCompatibleBitmap(sdc, ww, hh);
    ReleaseDC(nullptr, sdc);
    if (!g_barDc || !g_barBm) {
        if (g_barDc) { DeleteDC(g_barDc); g_barDc = nullptr; }
        if (g_barBm) { DeleteObject(g_barBm); g_barBm = nullptr; }
        return false;
    }
    g_barBmOld = (HBITMAP)SelectObject(g_barDc, g_barBm);
    g_barW = ww; g_barH = hh;
    return true;
}
// renders the whole bar into dc (memory buffer or screen -- identical code)
static void guiRenderBar(HDC dc, int ww) {
    Gdiplus::Graphics g(dc);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    // the face: the theme bg with a light touch at the top -- depth without
    // inventing a color the theme does not own (guiLerpRGB toward white).
    // Integer fill + overscan: every bar pixel is covered exactly once, no
    // boundary blend, no gray hairline.
    const COLORREF bgTop = guiLerpRGB(guic::cBg, RGB(255, 255, 255), 0.07);
    Gdiplus::LinearGradientBrush face(
        Gdiplus::Rect(0, 0, ww + 1, kCapH + 1),
        Gdiplus::Color(255, GetRValue(bgTop), GetGValue(bgTop), GetBValue(bgTop)),
        Gdiplus::Color(255, GetRValue(guic::cBg), GetGValue(guic::cBg), GetBValue(guic::cBg)),
        Gdiplus::LinearGradientModeVertical);
    g.FillRectangle(&face, -1, -1, ww + 2, kCapH + 2);
    // 1px bottom edge: chrome, not dead space (same cPanel2 as v7.0.9).
    Gdiplus::SolidBrush edge(Gdiplus::Color(255, GetRValue(guic::cPanel2),
                                             GetGValue(guic::cPanel2), GetBValue(guic::cPanel2)));
    g.FillRectangle(&edge, 0, kCapH - 1, ww, 1);
    // the close slot: hover face + the thin system-style X. Shape smoothing
    // turns ON for the X's diagonals ONLY -- the two strokes need AA; the
    // fills above stay integer-exact.
    const int cx = ww - 23, cy = kCapH / 2;   // slot center (46 x kCapH slot)
    if (g_capHot) {
        Gdiplus::SolidBrush hf(Gdiplus::Color(255, GetRValue(guic::cPanel2),
                                              GetGValue(guic::cPanel2), GetBValue(guic::cPanel2)));
        g.FillRectangle(&hf, ww - 46, 0, 46, kCapH);
    }
    const COLORREF xcol = g_capHot ? guic::cAccent : guic::cText;
    Gdiplus::Pen xp(Gdiplus::Color(255, GetRValue(xcol), GetGValue(xcol), GetBValue(xcol)), 1.25f);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.DrawLine(&xp, cx - 5, cy - 5, cx + 5, cy + 5);   // the modern thin X --
    g.DrawLine(&xp, cx + 5, cy - 5, cx - 5, cy + 5);   // the DWM close glyph's
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);    // own shape, drawn ours
    // the product name, theme-aware (see the v7.1.4 note above).
    if (g_fTitle) {
        const bool dark = ((GetRValue(guic::cBg) * 77 + GetGValue(guic::cBg) * 150 +
                            GetBValue(guic::cBg) * 29) >> 8) < 128;   // Rec.601-ish luma
        Gdiplus::Font tf(dc, g_fTitle);
        const Gdiplus::RectF tr(12.0f, 3.0f, 220.0f, (Gdiplus::REAL)(kCapH - 7));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentNear);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        if (dark) {
            Gdiplus::LinearGradientBrush gb(
                tr, Gdiplus::Color(255, 160, 200, 205),   // cyan gray, light -- top
                    Gdiplus::Color(255, 108, 134, 140),   // slate cyan gray -- bottom
                Gdiplus::LinearGradientModeVertical);
            g.DrawString(kCapTitle, -1, &tf, tr, &sf, &gb);
        } else {
            Gdiplus::SolidBrush nb(Gdiplus::Color(255, GetRValue(guic::cText),
                                                  GetGValue(guic::cText), GetBValue(guic::cText)));
            g.DrawString(kCapTitle, -1, &tf, tr, &sf, &nb);
        }
    }
}
static void guiPaintBar(HWND w) {
    if (!w || !IsWindow(w)) return;
    HDC wdc = GetWindowDC(w);
    RECT wr; GetWindowRect(w, &wr);
    const int ww = wr.right - wr.left;
    if (ww > 0 && guiBarBufEnsure(ww, kCapH)) {
        guiRenderBar(g_barDc, ww);
        BitBlt(wdc, 0, 0, ww, kCapH, g_barDc, 0, 0, SRCCOPY);   // ONE blit, no flicker
    } else {
        guiRenderBar(wdc, ww);   // no buffer: draw straight (old path)
    }
    ReleaseDC(w, wdc);
}

// ---- fonts / brushes -------------------------------------------------------
static void applyTheme(int i);   // v7.0.0: forward decl (defined right below guiMakeGdi)
static void guiMakeGdi() {
    // v7.1.8: the Marlett close-glyph font is RETIRED -- the bar's X is the
    // modern thin caption shape, two antialiased GDI+ pen strokes (see
    // guiRenderBar). (The Times New Roman header font is retired with the
    // in-window header text it served.)
    g_fUI   = CreateFontA(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_fUIb  = CreateFontA(-13, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_fSml  = CreateFontA(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_fMono = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          FIXED_PITCH | FF_MODERN, "Consolas");
    // v7.1.0: the titlebar's product name -- Segoe UI Semibold 14 px, drawn
    // in the dark-blue -> teal vertical gradient (see guiPaintBar).
    g_fTitle = CreateFontA(-14, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    applyTheme(g_themeSaved);   // v7.0.0: the theme owns the brushes now; v7.0.4: the ini's gui_theme wins (default 0 = DARK CYAN)
}

// (v7.0.6's DWM dark-mode caption helper is retired with the DWM caption
// itself -- v7.0.9 paints the bar from the live guic:: colors instead.)

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
    if (g_hwnd) {
        SetWindowTextA(GetDlgItem(g_hwnd, IDC_THEME), t.name);
        // v7.0.9: the titlebar is ours and painted straight from these colors
        // -- it recolors HERE, synchronously, before the atomic repaint.
        guiPaintBar(g_hwnd);
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
static const int KCLI_W = 880, KCLI_H = 620;   // designed client size
static const int KLOG_BOTTOM = 594;            // v6.6.6: log ends here; 26px watermark strip below
static const int KPANEL_T = 66, KPANEL_B = 402; // page panel band
static const int KHDR_Y = 94, KROW0 = 120;   // page header / first row / pitch

// ---- small drawing helpers (v6.6.5: GDI+ -- everything the eye sees is AA) --
static Gdiplus::Color GCol(COLORREF c, BYTE a = 255) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

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
    Gdiplus::FontFamily ff(L"Arial");
    Gdiplus::Font f(&ff, 13, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
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
    const Gdiplus::REAL x = (Gdiplus::REAL)(crc.right - 14) - mr.Width;
    const Gdiplus::REAL y = (Gdiplus::REAL)KLOG_BOTTOM + ((Gdiplus::REAL)(crc.bottom - KLOG_BOTTOM) - mr.Height) / 2.0f;
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

// ---- v7.1.5: the log pane paints through a memory buffer (THE PANE FLICKER)
// Every log append scrolled the read-only EDIT and its direct WM_PAINT
// erased-then-drew ON SCREEN -- visible flicker under steady engine logging
// (the field: "the console text ... still flickers"). The pane is
// subclassed: WM_ERASEBKGND -> nothing, WM_PAINT renders the control into a
// memory surface via WM_PRINTCLIENT and lands as ONE opaque blit. The
// scroll logic, the colors (WM_CTLCOLORSTATIC) and the font stay stock.
static HDC     g_logDc = nullptr;
static HBITMAP g_logBm = nullptr, g_logBmOld = nullptr;
static int     g_logW = 0, g_logH = 0;
static void guiLogBufRelease() {
    if (g_logBmOld) { SelectObject(g_logDc, g_logBmOld); g_logBmOld = nullptr; }
    if (g_logBm)    { DeleteObject(g_logBm);             g_logBm    = nullptr; }
    if (g_logDc)    { DeleteDC(g_logDc);                 g_logDc    = nullptr; }
    g_logW = 0; g_logH = 0;
}
static bool guiLogBufEnsure(int w, int h) {
    if (g_logDc && w == g_logW && h == g_logH) return true;
    guiLogBufRelease();
    HDC sdc = GetDC(nullptr);
    g_logDc = CreateCompatibleDC(sdc);
    g_logBm = CreateCompatibleBitmap(sdc, w, h);
    ReleaseDC(nullptr, sdc);
    if (!g_logDc || !g_logBm) { guiLogBufRelease(); return false; }
    g_logBmOld = (HBITMAP)SelectObject(g_logDc, g_logBm);
    g_logW = w; g_logH = h;
    return true;
}
static LRESULT CALLBACK logSubProc(HWND h, UINT m, WPARAM wp, LPARAM lp,
                                   UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_ERASEBKGND:
        return 1;                       // the blit below is fully opaque
    case WM_PAINT: {
        RECT rc; GetClientRect(h, &rc);
        if (!guiLogBufEnsure(rc.right, rc.bottom)) break;   // no buffer -> stock paint
        SendMessageA(h, WM_PRINTCLIENT, (WPARAM)g_logDc, PRF_CLIENT | PRF_ERASEBKGND);
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, g_logDc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefSubclassProc(h, m, wp, lp);
}

static HWND mkLabel(HWND w, int id, int x, int y, int cw, const char* txt, HFONT f, int ch = 20) {
    HWND h = CreateWindowExA(0, "STATIC", txt, WS_CHILD | WS_VISIBLE,
                             x, y, cw, ch, w, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)(f ? f : g_fUI), TRUE);
    regPage(h);
    return h;
}

static HWND mkEdit(HWND w, int id, int x, int y, int cw, int ch, const char* val) {
    HWND h = CreateWindowExA(0, "EDIT", val,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        x, y, cw, ch, w, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_fUI, TRUE);
    guiDarkCtl(h);
    regPage(h);
    return h;
}

static HWND mkBtn(HWND w, int id, int x, int y, int cw, int ch, const char* txt) {
    // NOTE: deliberately NOT regPage()d here -- the tab strip and the
    // START/APPLY/SAVE bar live on every page. Page-bound buttons (toggles,
    // presets, REFRESH) are registered by their builders (v6.6.2).
    HWND h = CreateWindowExA(0, "BUTTON", txt,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        x, y, cw, ch, w, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_fUIb, TRUE);
    return h;
}

static HWND mkCombo(HWND w, int id, int x, int y, bool listOnly) {
    HWND h = CreateWindowExA(0, "COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_AUTOHSCROLL |
        (listOnly ? CBS_DROPDOWNLIST : CBS_DROPDOWN),
        x, y, CTL_W, 220, w, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_fUI, TRUE);
    SendMessageA(h, CB_SETITEMHEIGHT, (WPARAM)-1, 16);
    guiDarkCtl(h);
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
                             x, y, cw, 28, w, (HMENU)(INT_PTR)id, nullptr, nullptr);
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
    mkLabel(w, 0, colX, y, 406, txt, g_fSml, lines * 15 + 4);
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
    if (g_cfg.eqSlotF[i] <= 0.0f) {   // empty slot -- arm it, keep the sliders
        char b[128];
        snprintf(b, sizeof(b),
                 "slot %d is empty -- set the curve, then press SAVE TO SLOT",
                 i + 1);
        guiSetStatus(w, b);
        guiRepaintSlots(w);
        return;
    }
    g_tog[IDC_TGL_EQ] = g_cfg.eqSlotOn[i];
    sldSetPos(GetDlgItem(w, IDC_SLD_FREQ), freqToPos(g_cfg.eqSlotF[i]), TRUE);
    sldSetPos(GetDlgItem(w, IDC_SLD_GAIN), (int)(g_cfg.eqSlotG[i] * 2.0f), TRUE);
    sldSetPos(GetDlgItem(w, IDC_SLD_Q),    qToPos(g_cfg.eqSlotQ[i]), TRUE);
    guiUpdateEqLabels(w);
    guiRepaint(w, IDC_TGL_EQ);   // v6.6.1: pill + slots repaint themselves
    guiRepaintSlots(w);
}

// right-click a slot (or the SAVE TO SLOT button): the sliders' current
// curve + the EQ pill land in the slot and the ini is spliced at once
// (v7.1.8; same model as the theme click). v7.1.9: the button saves the
// ARMED slot -- clicking a slot arms it and survives slider tweaks.
static void guiSaveSlot(HWND w, int i) {
    g_cfg.eqSlotOn[i] = g_tog[IDC_TGL_EQ];
    g_cfg.eqSlotF[i]  = posToFreq(sldGetPos(GetDlgItem(w, IDC_SLD_FREQ)));
    g_cfg.eqSlotG[i]  = sldGetPos(GetDlgItem(w, IDC_SLD_GAIN)) * 0.5f;
    g_cfg.eqSlotQ[i]  = posToQ(sldGetPos(GetDlgItem(w, IDC_SLD_Q)));
    // the live EQ mirrors what is on screen, so the saved ini stays coherent
    g_cfg.centerEq     = g_tog[IDC_TGL_EQ];
    g_cfg.centerEqFreq = g_cfg.eqSlotF[i];
    g_cfg.centerEqGain = g_cfg.eqSlotG[i];
    g_cfg.centerEqQ    = g_cfg.eqSlotQ[i];
    g_eqSel  = i;
    const bool ok = guiSaveIni();
    guiUpdateEqLabels(w);
    guiRepaint(w, IDC_TGL_EQ);
    guiRepaintSlots(w);
    char b[128];
    snprintf(b, sizeof(b),
             "%s to slot %d: %.0f Hz / %+.1f dB / Q %.2f",
             ok ? "saved" : "save FAILED", i + 1,
             g_cfg.eqSlotF[i], g_cfg.eqSlotG[i], g_cfg.eqSlotQ[i]);
    guiSetStatus(w, b);
}

// ---- live device enumeration for the pickers -------------------------------
static void guiEnumNames(std::vector<std::string>& pb, std::vector<std::string>& cp) {
    ma_backend be[] = { ma_backend_wasapi };
    ma_context ctx;
    if (ma_context_init(be, 1, nullptr, &ctx) != MA_SUCCESS) return;
    ma_device_info* p = nullptr; ma_uint32 np = 0;
    ma_device_info* c = nullptr; ma_uint32 nc = 0;
    if (ma_context_get_devices(&ctx, &p, &np, &c, &nc) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < np; ++i) pb.push_back(p[i].name);
        for (ma_uint32 i = 0; i < nc; ++i) cp.push_back(c[i].name);
    }
    ma_context_uninit(&ctx);
}

static void guiFillCombos(HWND w) {
    std::vector<std::string> pb, cp;
    guiEnumNames(pb, cp);
    const int ids[3] = { IDC_OUT1, IDC_OUT2, IDC_INDEV };   // 0,1 playback; 2 capture
    for (int k = 0; k < 3; ++k) {
        HWND h = GetDlgItem(w, ids[k]);
        char cur[512]; GetWindowTextA(h, cur, sizeof(cur));
        SendMessageA(h, CB_RESETCONTENT, 0, 0);
        const std::vector<std::string>& v = (k < 2) ? pb : cp;
        for (size_t i = 0; i < v.size(); ++i)
            SendMessageA(h, CB_ADDSTRING, 0, (LPARAM)v[i].c_str());
        if (cur[0]) SetWindowTextA(h, cur);
    }
}

// ---- ini values <-> gui ----------------------------------------------------
static std::string boolText(bool b) { return b ? "on" : "off"; }

static std::string cfgValueText(const std::string& key) {
    char b[512];
    if      (key == "out1ms")            { snprintf(b, 64, "%g", g_cfg.out1Ms);  return b; }
    else if (key == "out2ms")            { snprintf(b, 64, "%g", g_cfg.out2Ms);  return b; }
    else if (key == "cap_ms")            { snprintf(b, 64, "%g", g_cfg.capMs);   return b; }
    else if (key == "ring1_ms")          { snprintf(b, 64, "%g", g_cfg.ring1Ms); return b; }
    else if (key == "ring2_ms")          { snprintf(b, 64, "%g", g_cfg.ring2Ms); return b; }
    else if (key == "delay1_ms")         { snprintf(b, 64, "%g", g_cfg.delay1Ms); return b; }
    else if (key == "delay2_ms")         { snprintf(b, 64, "%g", g_cfg.delay2Ms); return b; }
    else if (key == "gain_l")            { snprintf(b, 64, "%g", g_cfg.gainL);     return b; }
    else if (key == "gain_r")            { snprintf(b, 64, "%g", g_cfg.gainR);     return b; }
    else if (key == "gain_lfe")          { snprintf(b, 64, "%g", g_cfg.gainLfe);   return b; }
    else if (key == "gain_center")       { snprintf(b, 64, "%g", g_cfg.gainCenter);return b; }
    else if (key == "chime_gain")        { snprintf(b, 64, "%g", g_cfg.chimeGain); return b; }
    else if (key == "center_eq_freq")    { snprintf(b, 64, "%g", g_cfg.centerEqFreq); return b; }
    else if (key == "center_eq_gain")    { snprintf(b, 64, "%g", g_cfg.centerEqGain); return b; }
    else if (key == "center_eq_q")       { snprintf(b, 64, "%g", g_cfg.centerEqQ); return b; }
    // v7.1.8: the five EQ save slots
    else if (key.rfind("eq_slot", 0) == 0 && key.size() > 8 &&
             key[7] >= '1' && key[7] <= '5') {
        const int n = key[7] - '1';
        const std::string t = key.substr(8);
        if      (t == "_on")   return boolText(g_cfg.eqSlotOn[n]);
        else if (t == "_freq") { snprintf(b, 64, "%g", g_cfg.eqSlotF[n]); return b; }
        else if (t == "_gain") { snprintf(b, 64, "%g", g_cfg.eqSlotG[n]); return b; }
        else if (t == "_q")    { snprintf(b, 64, "%g", g_cfg.eqSlotQ[n]); return b; }
    }
    else if (key == "drift_correct_ms")  { snprintf(b, 64, "%g", g_cfg.driftCorrectMs); return b; }
    else if (key == "ks_period_frames")  { snprintf(b, 64, "%u", g_cfg.ksPeriodFrames); return b; }
    else if (key == "ks_periods")        { snprintf(b, 64, "%u", g_cfg.ksPeriods); return b; }
    else if (key == "drift_interval_ms") { snprintf(b, 64, "%u", g_cfg.driftIntervalMs); return b; }
    else if (key == "deep_resync_min")   { snprintf(b, 64, "%u", g_cfg.deepResyncMin); return b; }
    else if (key == "buffer_size")       { snprintf(b, 64, "%g", g_cfg.legacyBufferMs); return b; }
    else if (key == "lfe_fold")          return boolText(g_cfg.lfeFold);
    else if (key == "center_eq")         return boolText(g_cfg.centerEq);
    else if (key == "volume_follow")     return boolText(g_cfg.volumeFollow);
    else if (key == "out2_first")        return boolText(g_cfg.out2First);
    else if (key == "chimes")            return boolText(g_cfg.chimes);
    else if (key == "input_device")      return g_cfg.inputDevice;
    else if (key == "output_device1")    return g_cfg.outputDevice1;
    else if (key == "output_device2")    return g_cfg.outputDevice2;
    else if (key == "volume_endpoint")   return g_cfg.volumeEndpoint;
    else if (key == "backend1")          return g_cfg.backend1;
    else if (key == "backend2")          return g_cfg.backend2;
    else if (key == "gui_theme")        { snprintf(b, 64, "%u", (unsigned)g_theme); return b; }
    else if (key == "startup_engine")   return boolText(g_cfg.startupEngine);   // v7.1.2
    return "";
}

// every key the window can write, in canonical order (for appending)
static const char* kIniKeys[] = {
    "out1ms", "out2ms", "cap_ms", "ring1_ms", "ring2_ms",
    "delay1_ms", "delay2_ms", "lfe_fold",
    "input_device", "output_device1", "output_device2",
    "gain_l", "gain_r", "gain_lfe", "gain_center",
    "center_eq", "center_eq_freq", "center_eq_gain", "center_eq_q",
    "eq_slot1_on", "eq_slot1_freq", "eq_slot1_gain", "eq_slot1_q",   // v7.1.8
    "eq_slot2_on", "eq_slot2_freq", "eq_slot2_gain", "eq_slot2_q",
    "eq_slot3_on", "eq_slot3_freq", "eq_slot3_gain", "eq_slot3_q",
    "eq_slot4_on", "eq_slot4_freq", "eq_slot4_gain", "eq_slot4_q",
    "eq_slot5_on", "eq_slot5_freq", "eq_slot5_gain", "eq_slot5_q",
    "volume_follow", "volume_endpoint", "out2_first",
    "chimes", "chime_gain",
    "backend1", "backend2", "ks_period_frames", "ks_periods",
    "drift_correct_ms", "drift_interval_ms",
    "deep_resync_min",
    "gui_theme",                // v7.0.4: the GUI theme persists
    "startup_engine",           // v7.1.2: the startup launch auto-starts the engine
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
    g_cfg.inputDevice    = editStr(w, IDC_INDEV);
    g_cfg.outputDevice1  = editStr(w, IDC_OUT1);
    g_cfg.outputDevice2  = editStr(w, IDC_OUT2);
    g_cfg.volumeEndpoint = editStr(w, IDC_VOLEP);
    g_cfg.gainL     = editFloat(w, IDC_GAIN_L,  1.0f);
    g_cfg.gainR     = editFloat(w, IDC_GAIN_R,  1.0f);
    g_cfg.gainLfe   = editFloat(w, IDC_GAIN_LFE, 0.4f);
    g_cfg.gainCenter= editFloat(w, IDC_GAIN_CTR, 1.0f);
    g_cfg.lfeFold          = g_tog[IDC_TGL_LFEFOLD];
    // v6.6.8: captureExclusive / exclusive / alignModes are integrated -- the
    // struct defaults (all on) are final; no control can change them.
    g_cfg.out1Ms   = editFloat(w, IDC_OUT1MS, 1.0f);
    g_cfg.out2Ms   = editFloat(w, IDC_OUT2MS, 1.0f);
    g_cfg.capMs    = editFloat(w, IDC_CAPMS,  1.0f);
    g_cfg.ring1Ms  = editFloat(w, IDC_RING1, -1.0f);
    g_cfg.ring2Ms  = editFloat(w, IDC_RING2, -1.0f);
    g_cfg.delay1Ms = editFloat(w, IDC_DELAY1, 0.0f);
    g_cfg.delay2Ms = editFloat(w, IDC_DELAY2, 0.0f);
    g_cfg.out2First  = g_tog[IDC_TGL_OUT2FIRST];
    // v7.0.0: pinOutputs is integrated -- always on; no control can change it.
    // v7.1.7: backend1/backend2 have NO control -- the ini keys stand as loaded.
    g_cfg.ksPeriodFrames = (unsigned)editFloat(w, IDC_KSPF, 256);
    g_cfg.ksPeriods      = (unsigned)editFloat(w, IDC_KSPD, 3);
    // v6.6.8: clockSync / driftGuard / powerGuard / yieldDefault integrated.
    g_cfg.driftCorrectMs  = editFloat(w, IDC_DCORR, 5.0f);
    g_cfg.driftIntervalMs = (unsigned)editFloat(w, IDC_DINTERVAL, 5000);
    g_cfg.deepResyncMin   = (unsigned)editFloat(w, IDC_DEEPRESYNC, 0);
    // v6.6.8: centerGate / centerUpmixGuard / background integrated.
    g_cfg.volumeFollow    = g_tog[IDC_TGL_VOLFOLLOW];
    g_cfg.chimes          = g_tog[IDC_TGL_CHIMES];
    g_cfg.chimeGain       = editFloat(w, IDC_CHIMGAIN, 0.4f);
    g_cfg.centerEq       = g_tog[IDC_TGL_EQ];
    g_cfg.centerEqFreq   = posToFreq(sldGetPos(GetDlgItem(w, IDC_SLD_FREQ)));
    g_cfg.centerEqGain   = sldGetPos(GetDlgItem(w, IDC_SLD_GAIN)) * 0.5f;
    g_cfg.centerEqQ      = posToQ(sldGetPos(GetDlgItem(w, IDC_SLD_Q)));
}

static void controlsFromCfg(HWND w) {
    char b[64];
    SetDlgItemTextA(w, IDC_INDEV, g_cfg.inputDevice.c_str());
    SetDlgItemTextA(w, IDC_OUT1,  g_cfg.outputDevice1.c_str());
    SetDlgItemTextA(w, IDC_OUT2,  g_cfg.outputDevice2.c_str());
    SetDlgItemTextA(w, IDC_VOLEP, g_cfg.volumeEndpoint.c_str());
    snprintf(b, 64, "%g", g_cfg.gainL);      SetDlgItemTextA(w, IDC_GAIN_L, b);
    snprintf(b, 64, "%g", g_cfg.gainR);      SetDlgItemTextA(w, IDC_GAIN_R, b);
    snprintf(b, 64, "%g", g_cfg.gainLfe);    SetDlgItemTextA(w, IDC_GAIN_LFE, b);
    snprintf(b, 64, "%g", g_cfg.gainCenter); SetDlgItemTextA(w, IDC_GAIN_CTR, b);
    g_tog[IDC_TGL_LFEFOLD]    = g_cfg.lfeFold;
    snprintf(b, 64, "%g", g_cfg.out1Ms);  SetDlgItemTextA(w, IDC_OUT1MS, b);
    snprintf(b, 64, "%g", g_cfg.out2Ms);  SetDlgItemTextA(w, IDC_OUT2MS, b);
    snprintf(b, 64, "%g", g_cfg.capMs);   SetDlgItemTextA(w, IDC_CAPMS, b);
    if (g_cfg.ring1Ms >= 0.0f) { snprintf(b, 64, "%g", g_cfg.ring1Ms); SetDlgItemTextA(w, IDC_RING1, b); }
    if (g_cfg.ring2Ms >= 0.0f) { snprintf(b, 64, "%g", g_cfg.ring2Ms); SetDlgItemTextA(w, IDC_RING2, b); }
    snprintf(b, 64, "%g", g_cfg.delay1Ms); SetDlgItemTextA(w, IDC_DELAY1, b);
    snprintf(b, 64, "%g", g_cfg.delay2Ms); SetDlgItemTextA(w, IDC_DELAY2, b);
    g_tog[IDC_TGL_OUT2FIRST] = g_cfg.out2First;
    // v7.1.7: backend1/backend2 have no control -- loaded from the ini only
    snprintf(b, 64, "%u", g_cfg.ksPeriodFrames); SetDlgItemTextA(w, IDC_KSPF, b);
    snprintf(b, 64, "%u", g_cfg.ksPeriods);      SetDlgItemTextA(w, IDC_KSPD, b);
    snprintf(b, 64, "%g", g_cfg.driftCorrectMs);   SetDlgItemTextA(w, IDC_DCORR, b);
    snprintf(b, 64, "%u", g_cfg.driftIntervalMs);  SetDlgItemTextA(w, IDC_DINTERVAL, b);
    snprintf(b, 64, "%u", g_cfg.deepResyncMin);    SetDlgItemTextA(w, IDC_DEEPRESYNC, b);
    g_tog[IDC_TGL_VOLFOLLOW]  = g_cfg.volumeFollow;
    g_tog[IDC_TGL_CHIMES]     = g_cfg.chimes;
    snprintf(b, 64, "%g", g_cfg.chimeGain);      SetDlgItemTextA(w, IDC_CHIMGAIN, b);
    g_tog[IDC_TGL_EQ] = g_cfg.centerEq;
    sldSetPos(GetDlgItem(w, IDC_SLD_FREQ), freqToPos(g_cfg.centerEqFreq), FALSE);
    sldSetPos(GetDlgItem(w, IDC_SLD_GAIN), (int)(g_cfg.centerEqGain * 2.0f), FALSE);
    sldSetPos(GetDlgItem(w, IDC_SLD_Q),    qToPos(g_cfg.centerEqQ), FALSE);
    g_eqSel = -1;    // custom unless the values match a saved slot exactly
    for (int i = 0; i < 5; ++i) {
        if (g_cfg.eqSlotF[i] > 0.0f &&
            g_cfg.centerEq     == g_cfg.eqSlotOn[i] &&
            fabsf(g_cfg.centerEqFreq - g_cfg.eqSlotF[i]) < 1.0f &&
            fabsf(g_cfg.centerEqGain - g_cfg.eqSlotG[i]) < 0.26f &&
            fabsf(g_cfg.centerEqQ    - g_cfg.eqSlotQ[i]) < 0.02f) { g_eqSel = i; break; }
    }
    guiUpdateEqLabels(w);
    InvalidateRect(w, nullptr, TRUE);
}

// ---- engine control ---------------------------------------------------------
static void guiLogSink(const char* s) {
    std::string line(s);
    if (line.compare(0, 5, "out1 ") == 0) {          // the live health line:
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        g_lastHealth = line;                          // status bar, not the pane
        g_logDirty.store(true);
        return;
    }
    for (size_t i = 0; i < line.size(); ++i)
        if (line[i] == '\r') line[i] = '\n';          // console \r-overwrite -> lines
    EnterCriticalSection(&g_logCS);
    if (g_logRing.size() > 65536) g_logRing.erase(0, 32768);
    g_logRing += line;
    LeaveCriticalSection(&g_logCS);
    g_logDirty.store(true);
}

static void guiAppendLog(const std::string& s) {
    HWND e = GetDlgItem(g_hwnd, IDC_LOG);
    if (!e) return;
    // v6.6.2: the multiline EDIT honors only \r\n; a bare \n makes rows
    // merge and the wrap layout COLLAPSE into overlapping lines. Normalize
    // every break (CR / LF / CRLF / a ring-trim cut mid-pair) to one CRLF.
    std::string norm;
    norm.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            norm += "\r\n";
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;   // swallow the pair's LF
        } else if (s[i] == '\n') {
            norm += "\r\n";
        } else {
            norm += s[i];
        }
    }
    int len = GetWindowTextLengthA(e);
    // v7.1.5: the append is WM_SETREDRAW-batched -- one repaint per chunk,
    // then the subclassed pane lands it as a single blit.
    SendMessageA(e, WM_SETREDRAW, FALSE, 0);
    if (len > 131072) {                               // keep the pane bounded
        SendMessageA(e, EM_SETSEL, 0, len / 2);
        SendMessageA(e, EM_REPLACESEL, FALSE, (LPARAM)"");
        len = GetWindowTextLengthA(e);
    }
    SendMessageA(e, EM_SETSEL, len, len);
    SendMessageA(e, EM_REPLACESEL, FALSE, (LPARAM)norm.c_str());
    SendMessageA(e, EM_SCROLLCARET, 0, 0);
    SendMessageA(e, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(e, nullptr, FALSE);
}

// v7.1.5 FLICKER-FREE STATUS: the engine/health line paints INSIDE the frame
// buffer (guiPaintFrame) -- the STATIC child is hidden and only STORES the
// string. Every writer goes through here so the frame's status band is
// invalidated in the same breath; the next WM_PAINT lands it as one blit.
static void guiSetStatus(HWND w, const char* s) {
    SetDlgItemTextA(g_hwnd ? g_hwnd : w, IDC_STATUS, s);
    const RECT sr = { 14, 458, 866, 476 };
    InvalidateRect(g_hwnd ? g_hwnd : w, &sr, FALSE);
}

// v7.1.6 TRAY-FIRST: the one-and-only ring drain, used by the 250 ms tick
// AND by guiCreateRealWindow. While the window does not exist (tray-only)
// NOBODY drains: the ring buffers the whole boot log (capped at 64 KB like
// always); the first drain after the window opens lands it in the pane at
// once -- nothing the engine said before the first click is lost.
static void guiFlushLogRing() {
    if (!g_logDirty.exchange(false)) return;
    EnterCriticalSection(&g_logCS);
    const std::string chunk = g_logRing;
    g_logRing.clear();
    LeaveCriticalSection(&g_logCS);
    if (!chunk.empty()) guiAppendLog(chunk);
}

static DWORD WINAPI guiEngineThread(LPVOID) {
    tlog("[twinspeak] gui           : ---- engine start (%s) ----\n", g_guiIniPath.c_str());
    const int rc = engineMain(g_guiIniPath, false, false, true);
    g_engRC.store(rc);
    g_engState.store(0);
    PostMessageA(g_hwnd, WM_APP + 2, 0, 0);           // refresh the START button
    return 0;
}

static void guiStartEngine(HWND w) {
    if (g_engState.load() != 0) return;
    cfgFromControls(w);
    cfgSanitize();
    if (g_cfg.inputDevice.empty() || g_cfg.outputDevice1.empty() || g_cfg.outputDevice2.empty()) {
        guiSetStatus(g_hwnd, "set input_device / output_device1 / output_device2 first");
        return;
    }
    if (!guiSaveIni())
        tlog("[twinspeak] gui           : could not write the ini -- running with the in-window values\n");
    g_engState.store(1);
    guiRepaint(g_hwnd, IDC_START);   // v6.6.1: the button owns its own pixels
    g_engThread = CreateThread(nullptr, 0, guiEngineThread, nullptr, 0, nullptr);
}

static void guiStopEngine() {
    const int st = g_engState.load();
    if (st == 0 || st == 3) return;
    g_engState.store(3);
    if (g_hwnd) guiRepaint(g_hwnd, IDC_START);
    // v7.1.11 THE STOP GAP STARTS HERE: the callbacks used to keep playing
    // the program until the ENGINE thread noticed g_run (its health loop
    // sleeps a full second) -- an APPLY+RESTART kept the audio running for
    // up to a second and the designed silent gap never arrived. Setting
    // the flag HERE silences the legs the instant the GUI decides to stop;
    // engineMain's own store still re-arms the gap for a console CTRL+C.
    g_stopSilence.store(true, std::memory_order_relaxed);
    g_run.store(false);
    if (g_engThread) {
        WaitForSingleObject(g_engThread, 10000);
        CloseHandle(g_engThread);
        g_engThread = nullptr;
    }
    g_engState.store(0);
    if (g_hwnd) guiRepaint(g_hwnd, IDC_START);
}

// ---- page building ----------------------------------------------------------
static bool startupEnabled();   // v6.6.6: forward decl -- the GUARDS pill reads it at build time
// v7.0.0: uniform tab width -- v7.1.7: five faces span the same strip
// (5 x 160 px + 4 x 4 px gaps = 816 px, the 818 px layout's twin).
// v7.1.8: the faces widened to 168; v7.1.9 trimmed to 164; v7.1.10 went to
// 166; v7.1.11 lands on the field's final call: +7 px (167). 5 x 167 +
// 4 x 4 gaps + 5 px left pad ends at 856 inside the 866 px strip.
static const int kTabW[5] = { 167, 167, 167, 167, 167 };

static void comboRow(HWND w, int colX, int y, const char* label, int id,
                     bool listOnly, const char* cur) {
    mkLabel(w, 0, colX, y + 2, LBL_W, label, g_fUI);
    HWND h = mkCombo(w, id, colX + LBL_W + 6, y, listOnly);
    if (cur && cur[0]) SetWindowTextA(h, cur);
}

static void guiBuildPages(HWND w) {
    // ============ page 0: DEVICES ============
    g_tab = 0;
    header(w, COLA_X, KHDR_Y, "DEVICES");
    comboRow(w, COLA_X, KROW0, "capture input", IDC_INDEV, false, g_cfg.inputDevice.c_str());
    comboRow(w, COLA_X, 154, "output leg1 (mains)", IDC_OUT1, false, g_cfg.outputDevice1.c_str());
    comboRow(w, COLA_X, 188, "output leg2 (center)", IDC_OUT2, false, g_cfg.outputDevice2.c_str());
    row(w, COLA_X, 222, "volume_endpoint", IDC_VOLEP, g_cfg.volumeEndpoint.c_str());
    HWND b = mkBtn(w, IDC_REFRESH, COLA_X, 256, 150, 26, "REFRESH DEVICES");
    regPage(b);                        // v6.6.2: page 0 only
    note(w, COLA_X, 292,
         "leg1 carries FL/FR (+ the LFE fold) -- the mains.\n"
         "leg2 carries ONLY the center channel.\n"
         "volume_endpoint overrides which playback-side slider is the master volume.", 4);
    header(w, COLB_X, KHDR_Y, "GAIN");
    row(w, COLB_X, KROW0, "gain_l", IDC_GAIN_L, []{ char b[32]; snprintf(b,32,"%g",g_cfg.gainL); return std::string(b); }().c_str());
    row(w, COLB_X, 154, "gain_r", IDC_GAIN_R, []{ char b[32]; snprintf(b,32,"%g",g_cfg.gainR); return std::string(b); }().c_str());
    row(w, COLB_X, 188, "gain_lfe", IDC_GAIN_LFE, []{ char b[32]; snprintf(b,32,"%g",g_cfg.gainLfe); return std::string(b); }().c_str());
    row(w, COLB_X, 222, "gain_center", IDC_GAIN_CTR, []{ char b[32]; snprintf(b,32,"%g",g_cfg.gainCenter); return std::string(b); }().c_str());
    tglRow(w, COLB_X, 260, "lfe_fold (LFE onto FL/FR)", IDC_TGL_LFEFOLD, g_cfg.lfeFold);
    // v6.6.8: capture_exclusive / exclusive / align_modes pills are gone --
    // the share-mode design is part of the engine now (always on).

    // ============ page 1: TIMING ============
    g_tab = 1;
    header(w, COLA_X, KHDR_Y, "DEVICE PERIODS (ms)");
    row(w, COLA_X, KROW0, "out1ms", IDC_OUT1MS, []{ char b[32]; snprintf(b,32,"%g",g_cfg.out1Ms); return std::string(b); }().c_str());
    row(w, COLA_X, 154, "out2ms", IDC_OUT2MS, []{ char b[32]; snprintf(b,32,"%g",g_cfg.out2Ms); return std::string(b); }().c_str());
    row(w, COLA_X, 188, "cap_ms (capture burst)", IDC_CAPMS, []{ char b[32]; snprintf(b,32,"%g",g_cfg.capMs); return std::string(b); }().c_str());
    row(w, COLA_X, 222, "ring1_ms", IDC_RING1, []{ char b[32]; snprintf(b,32,"%g",g_cfg.ring1Ms); return std::string(b); }().c_str());
    row(w, COLA_X, 256, "ring2_ms", IDC_RING2, []{ char b[32]; snprintf(b,32,"%g",g_cfg.ring2Ms); return std::string(b); }().c_str());
    header(w, COLB_X, KHDR_Y, "TIMING");
    row(w, COLB_X, KROW0, "delay1_ms", IDC_DELAY1, []{ char b[32]; snprintf(b,32,"%g",g_cfg.delay1Ms); return std::string(b); }().c_str());
    row(w, COLB_X, 154, "delay2_ms", IDC_DELAY2, []{ char b[32]; snprintf(b,32,"%g",g_cfg.delay2Ms); return std::string(b); }().c_str());
    tglRow(w, COLB_X, 188, "out2_first (center opens first)", IDC_TGL_OUT2FIRST, g_cfg.out2First);
    // v7.0.0: the pin_outputs pill is gone -- output pinning is part of the
    // engine now (always on): both endpoint volumes are pinned 100% / unmuted.
    note(w, COLB_X, 262,
         "delay1/delay2 push one leg later, bit for bit -- put the delay on the\n"
         "EARLY leg. The banner prints both est paths; the health line's 'drift'\n"
         "column already includes your delay (it IS the alignment, not a fault).", 4);

    // ============ page 2: TRANSPORT ============
    g_tab = 2;
    header(w, COLA_X, KHDR_Y, "OUTPUT TRANSPORT PER LEG");
    // v7.1.7: the backend combos are GONE -- both legs run wasapi exclusive
    // by default and KS is opted into with backend1/backend2 = ks in the ini
    // (the user enforces exclusive anyway; the keys stay for power users).
    row(w, COLA_X, KROW0, "period_frames", IDC_KSPF, []{ char b[32]; snprintf(b,32,"%u",g_cfg.ksPeriodFrames); return std::string(b); }().c_str());
    row(w, COLA_X, 154, "periods", IDC_KSPD, []{ char b[32]; snprintf(b,32,"%u",g_cfg.ksPeriods); return std::string(b); }().c_str());
    note(w, COLB_X, 96,
         "wasapi exclusive -- the proven path: miniaudio, exclusive WaveRT\n"
         "event mode. This is what both legs run unless you say otherwise.\n\n"
         "ks -- the FlexASIO-grade path: a direct kernel-streaming render pin,\n"
         "the WaveRT looped buffer as the device's own DMA memory, paced\n"
         "against the hardware play-position register. Below the Windows\n"
         "engine: no events, no scheduler intermediaries.\n\n"
         "period_frames = the buffer-switch size in SAMPLES (128 @ 48 kHz\n"
         "= one bite every 2.67 ms; 128+ paces steadiest in the field).\n"
         "periods = the looped buffer around it (3 recommended, 2..8).\n\n"
         "Route a leg through KS with backend1/backend2 = ks in the ini (no\n"
         "UI control -- exclusive is enforced here). A driver refusal falls\n"
         "back to wasapi exclusive automatically and the log says so.", 13);

    // ============ page 3: SETTINGS (v7.1.7: SYNC + startup + chime/keepalive) =
    g_tab = 3;
    header(w, COLA_X, KHDR_Y, "SYNC");
    row(w, COLA_X, KROW0, "drift_correct_ms", IDC_DCORR, []{ char b[32]; snprintf(b,32,"%g",g_cfg.driftCorrectMs); return std::string(b); }().c_str());
    row(w, COLA_X, 154, "drift_interval_ms", IDC_DINTERVAL, []{ char b[32]; snprintf(b,32,"%u",g_cfg.driftIntervalMs); return std::string(b); }().c_str());
    row(w, COLA_X, 188, "deep_resync_min", IDC_DEEPRESYNC, []{ char b[32]; snprintf(b,32,"%u",g_cfg.deepResyncMin); return std::string(b); }().c_str());
    note(w, COLA_X, 230,
         "drift_correct_ms: trigger distance from the calibrated baseline.\n"
         "drift_interval_ms: minimum spacing between corrections (2000..60000).\n"
         "deep_resync_min: scheduled center-leg reopen; 0 = off, else 5..1440.", 4);
    tglRow(w, COLA_X, 316, "launch at Windows startup", IDC_TGL_STARTUP, startupEnabled());
    header(w, COLB_X, KHDR_Y, "CHIME/KEEPALIVE");
    tglRow(w, COLB_X, KROW0, "volume_follow (Line 1 slider = master)", IDC_TGL_VOLFOLLOW, g_cfg.volumeFollow);
    row(w, COLB_X, 154, "chime_gain", IDC_CHIMGAIN, []{ char b[32]; snprintf(b,32,"%g",g_cfg.chimeGain); return std::string(b); }().c_str());
    tglRow(w, COLB_X, 188, "chimes (startup / reopen blip)", IDC_TGL_CHIMES, g_cfg.chimes);
    note(w, COLB_X, 226,
         "The keep-alive tone is BUILT IN (40 Hz @ 0.1%): out2 never sees\n"
         "digital silence, so the center speaker stays awake. POST-MIX: the\n"
         "tone and the chime are summed in the out2 OUTPUT callbacks, after\n"
         "the mix -- they never enter the stream an OBS capture records.\n"
         "The log lives in the pane below; background mode is built in.", 5);

    // ============ page 4: CENTER EQ ============
    g_tab = 4;
    tglRow(w, COLA_X, 114, "center EQ (out2 only)", IDC_TGL_EQ, g_cfg.centerEq);
    // v7.1.8: the four built-in presets are GONE -- five user save slots,
    // uniform like they were: 5 x 76 px faces + 4 x 5 px gaps span the
    // sliders' 400 px exactly. Left-click applies AND arms; the SAVE TO
    // SLOT button (below the sliders since v7.1.15) writes the armed slot
    // (v7.1.9).
    for (int i = 0; i < 5; ++i) {
        char nm[16];
        snprintf(nm, sizeof(nm), "SLOT %d", i + 1);
        regPage(mkBtn(w, IDC_SLOT1 + i, COLA_X + i * 81, 148, 76, 26, nm));
    }
    // v7.1.15: SAVE TO SLOT lives under the sliders now (was column B,
    // detached from what it saves) -- the button sits directly beneath
    // the curve it writes, with its own one-line description.
    // v7.1.16 THE OVERLAP: that note shipped as a 2-row box carrying ONE
    // line -- 396..430 on a page panel that ends at KPANEL_B = 402 -- so
    // it hung 28 px past the panel and lay across the STOP / APPLY +
    // RESTART / SAVE INI row at 414 (the STATIC is built after the bottom
    // bar, so it painted OVER the buttons' tops). And the arithmetic was
    // unwinnable as the column stood: the sliders end at 358, the panel
    // leaves 44 px, and button (26) + note (19) need 45. The whole stack
    // tightens: the three slider groups ride up 8 px (labels 196/248/300,
    // sliders 214/266/318, ending at 346), the button rises to 354..380,
    // and the description is ONE line (383..402) ending at the panel
    // edge. Nothing crosses 402; nothing touches the action row.
    HWND sv = mkBtn(w, IDC_EQSAVE, COLA_X, 354, 400, 26, "SAVE TO SLOT");
    regPage(sv);                       // v7.1.9: page 4 only
    note(w, COLA_X, 383,
         "makes sure this slot's current modifications persist across sessions.", 1);
    mkLabel(w, IDC_LBL_FREQ, COLA_X, 196, 400, "PEAK FREQUENCY", g_fUIb, 16);
    mkSlider(w, IDC_SLD_FREQ, COLA_X, 214, 400, 0, 1000);
    mkLabel(w, IDC_LBL_GAIN, COLA_X, 248, 400, "BOOST", g_fUIb, 16);
    mkSlider(w, IDC_SLD_GAIN, COLA_X, 266, 400, -24, 24);
    mkLabel(w, IDC_LBL_Q, COLA_X, 300, 400, "SHARPNESS (Q)", g_fUIb, 16);
    mkSlider(w, IDC_SLD_Q, COLA_X, 318, 400, 0, 1000);
    // v7.1.15 THE NOTE THAT ATE ITS OWN TAIL: the old note carried 17
    // text rows inside a 13-row box -- a STATIC renders every line it
    // is handed and simply stops at the control's bottom edge, so the
    // final paragraphs were cut off. The text is re-wrapped to lines a
    // 406 px column can never wrap (all <= 57 chars) and the control is
    // sized for the TRUE row count. Nothing clips.
    note(w, COLB_X, 66,
         "One peaking band on ch3, applied inside the engine's\n"
         "out2 mix path -- out2 ONLY. The mains (out1) are never\n"
         "touched.\n"
         "\n"
         "A KS/WaveRT leg bypasses the Windows audio engine, so\n"
         "system-wide EQs (Equalizer APO and friends) can never\n"
         "see this stream. The boost lives here instead.\n"
         "\n"
         "SLOTS: left-click applies a slot and ARMS it -- the\n"
         "face stays accented while you tweak. Adjust the\n"
         "sliders, press SAVE TO SLOT and the curve + the EQ pill\n"
         "land in the armed slot (ini updated at once). SAVE TO\n"
         "SLOT makes sure this slot's current modifications\n"
         "persist across sessions. Right-click a slot saves\n"
         "straight into it. Slot 1 ships with 387 Hz / +3.5 dB /\n"
         "Q 0.30. The pill above is the off switch -- EQ off =\n"
         "the bit-exact path.\n"
         "\n"
         "The boost stacks with gain_center; the mix clamps at\n"
         "full scale, so loud center peaks may clip first --\n"
         "lower gain_center if they do. Keep-alive and chime are\n"
         "NOT equalized (UI cues, not dialogue).", 22);
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
static const int KSTRIP_X = 9, KSTRIP_Y = 28, KSTRIP_W = 866, KSTRIP_H = 36;
static const int KSTRIP_PAD = 5, KSTRIP_FACE_Y = 6;

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
static std::wstring          g_tabWide[5];            // v7.0.7: labels widened once
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
            const float px = 13.0f * 96.0f / (float)(dpi > 0 ? dpi : 96);
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
        tlog("[twinspeak] gui           : tab strip on GPU -- Direct2D 1.1 / DXGI swap chain (bitblt model%s)\n",
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
    for (int i = 0, fx = KSTRIP_PAD; i < 5; fx += kTabW[i] + 4, ++i) {
        const bool act = (i == g_tab);
        const RECT trc = { fx, KSTRIP_FACE_Y - 1, fx + kTabW[i], KSTRIP_FACE_Y + 25 };
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
        for (int i = 0, fx = KSTRIP_PAD; i < 5; fx += kTabW[i] + 4, ++i)
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
                for (int i = 0; i < g_tab; ++i) fxs += kTabW[i] + 4;
                const float fx = (float)fxs, fy = (float)KSTRIP_FACE_Y;
                const float fw = (float)kTabW[g_tab], fh = 23.0f;
                const float mid[3] = { 1.35f, 2.90f, 4.40f };  // band centers
                const float wd [3] = { 1.70f, 1.40f, 1.60f };  // band widths
                const BYTE  al [3] = { 80, 44, 22 };           // band strengths
                for (int k = 0; k < 3; ++k) {
                    const float s = mid[k];
                    const D2D1_ROUNDED_RECT rr = {
                        D2D1_RECT_F{ fx - s, fy - s, fx + fw + s, fy + fh + s },
                        6.0f + s, 6.0f + s };
                    TSBRCOL(guic::cAccent, al[k] * fs);
                    g_dctx->DrawRoundedRectangle(rr, g_d2dBr, wd[k]);
                }
            }
            for (int i = 0, ix = KSTRIP_PAD; i < 5; ix += kTabW[i] + 4, ++i) {
                const bool act = (i == g_tab);
                // the face rect is the 1 px inset rounded rect (guiRoundPath
                // convention) -- the centered stroke stays fully inside.
                const D2D1_ROUNDED_RECT rr = {
                    D2D1_RECT_F{ (float)ix + 1.0f, (float)KSTRIP_FACE_Y + 1.0f,
                                 (float)ix + kTabW[i] - 1.0f,
                                 (float)KSTRIP_FACE_Y + 22.0f }, 6.0f, 6.0f };
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
                for (int i = 0, fx = KSTRIP_PAD; i < 5; fx += kTabW[i] + 4, ++i) {
                    const bool act = (i == g_tab);
                    if (g_tabWide[i].empty()) g_tabWide[i] = guiWide(kTabNames[i]);
                    TSBRCOL(act ? guiLerpRGB(guic::cText, guic::cAccent, fs)
                                : guic::cText, 255);
                    const D2D1_RECT_F tr = {
                        (float)fx, (float)KSTRIP_FACE_Y - 1.0f,
                        (float)(fx + kTabW[i]), (float)KSTRIP_FACE_Y + 25.0f };
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
            for (int i = 0; i < g_tab; ++i) fxs += kTabW[i] + 4;
            const Gdiplus::RectF fr((Gdiplus::REAL)fxs,
                                    (Gdiplus::REAL)KSTRIP_FACE_Y,
                                    (Gdiplus::REAL)kTabW[g_tab], 23.0f);
            const float mid[3] = { 1.35f, 2.90f, 4.40f };
            const float wd [3] = { 1.70f, 1.40f, 1.60f };
            const BYTE  al [3] = { 80, 44, 22 };
            for (int k = 0; k < 3; ++k) {
                const float s = mid[k];
                const Gdiplus::RectF e(fr.X - s, fr.Y - s,
                                       fr.Width + 2.0f * s, fr.Height + 2.0f * s);
                Gdiplus::GraphicsPath* bp = guiRoundPathRaw(e, 6.0f + s);
                Gdiplus::Pen gp(GCol(guic::cAccent, (BYTE)(al[k] * fs2)), wd[k]);
                g.DrawPath(&gp, bp);
                delete bp;
            }
            for (int i = 0, fx = KSTRIP_PAD; i < 5; fx += kTabW[i] + 4, ++i) {
                const bool act = (i == g_tab);
                const Gdiplus::Rect fr2(fx, KSTRIP_FACE_Y, kTabW[i], 23);
                Gdiplus::GraphicsPath* p = guiRoundPath(fr2, 6);
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
                const RECT trc = { fx, KSTRIP_FACE_Y - 1, fx + kTabW[i], KSTRIP_FACE_Y + 25 };
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
    CreateWindowExA(0, "TidyTabStrip4", nullptr, WS_CHILD | WS_VISIBLE,
                    KSTRIP_X, KSTRIP_Y, KSTRIP_W, KSTRIP_H, w,
                    (HMENU)(INT_PTR)IDC_TABSTRIP, nullptr, nullptr);
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
    // v7.1.5 FLICKER-FREE STATUS: the STATIC is HIDDEN -- it only stores the
    // string; guiPaintFrame draws the line inside the frame buffer (one blit,
    // no erase-then-draw flicker four times a second). Writers go through
    // guiSetStatus, which invalidates the status band of the frame.
    HWND st = CreateWindowExA(0, "STATIC", "engine stopped", WS_CHILD,
                              14, 458, 852, 16, w, (HMENU)(INT_PTR)IDC_STATUS, nullptr, nullptr);
    SendMessageA(st, WM_SETFONT, (WPARAM)g_fMono, TRUE);
    // v6.6.6: no WS_EX_CLIENTEDGE -- the etched 3D edge rendered in light
    // classic colors was the "ghost bordered box"; WM_PAINT draws the frame.
    HWND log = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        14, 480, 852, 114, w, (HMENU)(INT_PTR)IDC_LOG, nullptr, nullptr);
    SendMessageA(log, WM_SETFONT, (WPARAM)g_fMono, TRUE);
    guiDarkCtl(log);
    SetWindowSubclass(log, logSubProc, 1, 0);   // v7.1.5: the pane paints buffered
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
    for (int i = 0; i < 5; ++i)
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
    RedrawWindow(w, nullptr, nullptr,
                 RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
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
static void trayRemove() {
    if (!g_trayVisible) return;
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
    g_trayVisible = false;
}
static void trayAdd(bool hint) {
    if (!g_hwnd || g_trayVisible) return;
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP + 3;
    g_nid.hIcon = (HICON)LoadImageA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(1),
                                    IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    lstrcpynA(g_nid.szTip, "TidyVision TwinSpeak", (int)sizeof(g_nid.szTip));
    g_trayVisible = Shell_NotifyIconA(NIM_ADD, &g_nid) != FALSE;
    if (g_trayVisible && hint && !g_trayHint) {
        g_trayHint = true;
        g_nid.uFlags       = NIF_INFO;
        lstrcpynA(g_nid.szInfoTitle, "TidyVision TwinSpeak", (int)sizeof(g_nid.szInfoTitle));
        lstrcpynA(g_nid.szInfo,
                  "Still running -- the engine stays up.\n"
                  "Double-click this icon to reopen the window; "
                  "right-click for Start/Stop and Exit.",
                  (int)sizeof(g_nid.szInfo));
        g_nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconA(NIM_MODIFY, &g_nid);
    }
}
// v7.1.6 TRAY-FIRST: a balloon with caller-supplied text. The boot balloon
// says "running in the tray" ("still running" reads wrong when nothing was
// ever open); the close balloon stays trayAdd's own one-time hint.
static void trayBalloon(const char* text) {
    if (!g_trayVisible) return;
    g_nid.uFlags       = NIF_INFO;
    lstrcpynA(g_nid.szInfoTitle, "TidyVision TwinSpeak", (int)sizeof(g_nid.szInfoTitle));
    lstrcpynA(g_nid.szInfo, text, (int)sizeof(g_nid.szInfo));
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
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
    RECT r = { 0, 0, KCLI_W, KCLI_H };
    AdjustWindowRect(&r, kGuiFrame, FALSE);
    g_hwnd = CreateWindowExA(0, "twinspeak.control",
                             "TidyVision TwinSpeak: Control Panel",
                             kGuiFrame | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top,
                             nullptr, nullptr, hi, nullptr);
    if (!g_hwnd) return false;
    // v7.0.9 THE CUSTOM TITLEBAR / v7.1.16 THE UNSHAPED WINDOW (as always)
    {
        typedef HRESULT (WINAPI *TS_DWMSA)(HWND, DWORD, LPCVOID, DWORD);
        const TS_DWMSA dwmSet = (TS_DWMSA)GetProcAddress(
            LoadLibraryA("dwmapi.dll"), "DwmSetWindowAttribute");
        bool rounded = false;
        if (dwmSet) {
            // v7.1.11: the NCRENDERING_POLICY = DISABLED call is GONE -- it
            // killed the DWM's own window shadow (the all-four-directions
            // one a captioned window gets on Win10), leaving only the thin
            // menu-style CS_DROPSHADOW on the bottom-right. The policy now
            // stays ENABLED so DWM draws the full shadow again.
            const int round = 2; // DWMWCP_ROUND
            rounded = SUCCEEDED(dwmSet(g_hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                                       &round, sizeof(round)));
            // v7.1.15/16 THE SHADOW PINS: DWMWA_NCRENDERING_POLICY = ENABLED
            // and the frame extended into the client by 1 px -- the two
            // documented triggers, SET instead of hoped for. They were
            // never the problem and never the cure: the field's shadow was
            // being revoked by our own window REGION, see below.
            const int ncrOn = 2;   // DWMNCRP_ENABLED
            dwmSet(g_hwnd, 2 /* DWMWA_NCRENDERING_POLICY */, &ncrOn, sizeof(ncrOn));
            typedef HRESULT (WINAPI *TS_DWMEA)(HWND, LPCVOID);
            const TS_DWMEA dwmExt = (TS_DWMEA)GetProcAddress(
                LoadLibraryA("dwmapi.dll"), "DwmExtendFrameIntoClientArea");
            if (dwmExt) {
                struct TSMargins { int cxLeft, cxRight, cyTop, cyBottom; } m = { 1, 1, 1, 1 };
                dwmExt(g_hwnd, &m);
            }
        }
        // v7.1.16 THE REGION THAT ATE THE SHADOW -- the post-mortem. The
        // field's "I don't see a drop shadow anymore" was true since
        // v7.1.0, and every run our own log confessed: "a shaped window
        // cannot carry the DWM shadow". DWMWA_WINDOW_CORNER_PREFERENCE is
        // Win11-only; on the field's Win10 it fails, v7.1.0's fallback
        // shaped the window with a CreateRoundRectRgn GDI region -- and
        // Windows refuses to draw the DWM window shadow for ANY
        // region-shaped window, whatever the NCRENDERING policy says. The
        // v7.1.11 restore and the v7.1.15 pins above set the right state
        // and the region revoked it lines later, every version since
        // v7.1.0. The fallback is GONE: the window is NEVER shaped again.
        // Win10 keeps square corners and gets the full all-directions DWM
        // shadow back; Win11 still rounds, via the corner preference.
        tlog("[twinspeak] gui           : window shadow -- extend-frame 1 px + NCRENDERING ON%s\n",
             rounded ? " + corner preference ROUND"
                     : " (square corners: pre-Win11, window UNSHAPED so DWM keeps the shadow)");
    }
    // v7.0.9: re-measure the NCCALCSIZE-inset client and resize once -- the
    // design client is EXACTLY 880x620 again.
    {
        RECT rc; GetClientRect(g_hwnd, &rc);
        if (rc.right != KCLI_W || rc.bottom != KCLI_H)
            SetWindowPos(g_hwnd, nullptr, 0, 0,
                         (r.right - r.left) + (KCLI_W - rc.right),
                         (r.bottom - r.top) + (KCLI_H - rc.bottom),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
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
    // the whole boot log -- everything the engine said while tray-only --
    // lands in the pane in one flush, oldest first, nothing lost.
    guiFlushLogRing();
    tlog("[twinspeak] gui           : control window up -- ini at %s\n", g_guiIniPath.c_str());
    return true;
}

// ---- v7.1.7: the taskbar slot, managed explicitly ---------------------------
// The panel is shown with SW_SHOWNA (the v6.6.5 no-activate show), and a
// no-activate show can leave the taskbar button missing until the shell
// next re-enumerates tabs or the user clicks the window -- the field wanted
// the slot the moment the panel opens. ITaskbarList::AddTab creates the
// button NOW; ActivateTab marks it the active one; DeleteTab removes it
// when the panel hides back to the tray (the tray icon remains). The
// vtable is hand-rolled -- no __uuidof, no uuid pragma, every toolchain
// links identically (the same trick as the D2D IIDs above).
static const CLSID kTSCLSID_TaskbarList =
    { 0x56fdf344, 0xfd6d, 0x11d0, { 0x95, 0x8a, 0x00, 0x60, 0x97, 0xc9, 0xa0, 0x90 } };
static const IID   kTSIID_ITaskbarList =
    { 0x56fdf342, 0xfd6d, 0x11d0, { 0x95, 0x8a, 0x00, 0x60, 0x97, 0xc9, 0xa0, 0x90 } };
struct TS_ITaskbarListVtbl {
    HRESULT (__stdcall *QueryInterface)(void*, const IID&, void**);
    ULONG   (__stdcall *AddRef)(void*);
    ULONG   (__stdcall *Release)(void*);
    HRESULT (__stdcall *HrInit)(void*);
    HRESULT (__stdcall *AddTab)(void*, HWND);
    HRESULT (__stdcall *DeleteTab)(void*, HWND);
    HRESULT (__stdcall *ActivateTab)(void*, HWND);
    HRESULT (__stdcall *SetActiveAlt)(void*, HWND);
};
static void* g_tbList = nullptr;   // ITaskbarList* (lazy, GUI thread only)
static TS_ITaskbarListVtbl* tbVtbl() {
    if (g_tbList) return (TS_ITaskbarListVtbl*)*(void**)g_tbList;
    void* p = nullptr;
    if (FAILED(CoCreateInstance(kTSCLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                kTSIID_ITaskbarList, &p)))
        return nullptr;            // pre-Win7 or COM trouble: stock behavior
    g_tbList = p;
    ((TS_ITaskbarListVtbl*)*(void**)p)->HrInit(p);   // tell the shell we manage tabs
    return (TS_ITaskbarListVtbl*)*(void**)p;
}
static void tbShowTab(HWND w) {   // panel open: the slot exists THIS instant
    TS_ITaskbarListVtbl* v = tbVtbl();
    if (v && g_tbList && w) { v->AddTab(g_tbList, w); v->ActivateTab(g_tbList, w); }
}
static void tbHideTab(HWND w) {   // panel hidden: the slot goes with it
    TS_ITaskbarListVtbl* v = tbVtbl();
    if (v && g_tbList && w) v->DeleteTab(g_tbList, w);
}

static void guiRestoreWindow() {
    // v7.1.6 TRAY-FIRST: the first ask BUILDS the panel (tray double-click,
    // tray menu, second instance); every later ask just shows it.
    if (!guiCreateRealWindow()) return;
    g_userShown = true;   // v7.1.5: a user asked for the window -- boot yields stop
    ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_hwnd);
    tbShowTab(g_hwnd);    // v7.1.7: the taskbar slot exists THIS instant
}

// ---- v7.0.9: the frame render buffer (THE WATERMARK-FLICKER FIX)  -----------
// The frame face is rendered into this memory surface and lands on the screen
// as ONE blit -- no erase pass, no partial updates, nothing for DWM to sample
// mid-build. (Same technique as the button buffer below.)
static HDC     g_frmDc = nullptr;
static HBITMAP g_frmBm = nullptr, g_frmBmOld = nullptr;
static int     g_frmW = 0, g_frmH = 0;
static void guiFrameBufRelease() {
    if (g_frmBmOld) { SelectObject(g_frmDc, g_frmBmOld); g_frmBmOld = nullptr; }
    if (g_frmBm)    { DeleteObject(g_frmBm);             g_frmBm    = nullptr; }
    if (g_frmDc)    { DeleteDC(g_frmDc);                 g_frmDc    = nullptr; }
    g_frmW = 0; g_frmH = 0;
}
static bool guiFrameBufEnsure(int w, int h) {
    if (g_frmDc && w == g_frmW && h == g_frmH) return true;
    guiFrameBufRelease();
    HDC sdc = GetDC(nullptr);
    g_frmDc = CreateCompatibleDC(sdc);
    g_frmBm = CreateCompatibleBitmap(sdc, w, h);
    ReleaseDC(nullptr, sdc);
    if (!g_frmDc || !g_frmBm) { guiFrameBufRelease(); return false; }
    g_frmBmOld = (HBITMAP)SelectObject(g_frmDc, g_frmBm);
    g_frmW = w; g_frmH = h;
    return true;
}
// The complete frame face -- background, page panel, log frame, version line,
// watermark -- into ANY dc (the memory buffer normally, the screen directly
// as the no-buffer fallback). Header text is gone in v7.0.9: the custom
// titlebar replaced it.
static void guiPaintFrame(HDC dc, int cw, int ch) {
    const RECT full = { 0, 0, cw, ch };
    FillRect(dc, &full, g_brBg);          // v7.0.9: the erase used to do this
    const RECT panel = { 12, KPANEL_T, 866, KPANEL_B };
    FillRect(dc, &panel, g_brPanel);
    FrameRect(dc, &panel, g_brPanel2);
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::Pen fp(GCol(guic::cLine), 1.0f);   // the log pane's 1px frame (no CLIENTEDGE)
    g.DrawRectangle(&fp, Gdiplus::RectF(13.5f, 479.5f, 853.0f, 115.0f));
    char vbuf[640];
    snprintf(vbuf, sizeof(vbuf), "v7.1.16  |  %s", g_guiIniPath.c_str());
    const RECT ver = { 300, 8, 866, 30 };
    guiAAString(g, dc, ver, vbuf, guic::cDim, g_fSml, 2);
    // v7.1.5 FLICKER-FREE STATUS: the engine/health line is painted HERE,
    // inside the frame buffer -- the STATIC child (hidden) only stores the
    // string. One blit, no flicker while the health line updates.
    char stb[512] = "";
    if (g_hwnd) GetDlgItemTextA(g_hwnd, IDC_STATUS, stb, (int)sizeof(stb));
    const RECT stc = { 14, 458, 866, 476 };
    guiAAString(g, dc, stc, stb, guic::cAccent, g_fMono, 1);
    // v7.1.5: the keyboard map, tiny, in the free bottom-left half of the
    // watermark strip (the watermark owns the right half).
    const RECT kbr = { 14, KLOG_BOTTOM, 552, KCLI_H };
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
        case IDC_TGL_LFEFOLD:
        case IDC_TGL_OUT2FIRST:   // v7.0.0: IDC_TGL_PINOUT retired (integrated)
        case IDC_TGL_STARTUP:   // v6.6.6: the startup pill
        case IDC_TGL_VOLFOLLOW: case IDC_TGL_CHIMES:
        case IDC_TGL_EQ:
            return true;
    }
    return false;
}

static LRESULT CALLBACK guiWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    // v6.6.1: explorer restarted -- the tray icon vanished; re-add it. v7.1.6
    // TRAY-FIRST: re-add ALWAYS (trayRemove + trayAdd): the icon owner may be
    // the invisible tray window (no control window exists yet), and explorer
    // lost every icon anyway -- a fresh NIM_ADD is the only thing that works.
    if (g_taskbarCreated && msg == g_taskbarCreated) {
        trayRemove();
        trayAdd(false);
        return 0;
    }
    switch (msg) {
    case WM_NCCALCSIZE:
        // v7.0.9: shrink the client top by kCapH -- that strip becomes the
        // custom titlebar; every child coordinate keeps its old meaning.
        if (wp) { ((RECT*)lp)->top += kCapH; return 0; }
        break;
    case 0x00AE:   // WM_NCUAHDRAWCAPTION (undocumented)
    case 0x00AF: { // WM_NCUAHDRAWFRAME   (undocumented)
        // v7.1.0 THE FIRST-DRAG XP BAR: on the first drag (and some
        // activations) Windows paints its XP-era themed caption through
        // these two messages -- NOT through WM_NCPAINT -- so the retired
        // caption flashed back over our bar exactly once per session.
        // Both are answered with 0: the bar is ours, nobody else draws it.
        return 0; }
    case WM_NCPAINT:
        guiPaintBar(w);                 // the bar is ours; never the classic frame
        return 0;
    case WM_NCACTIVATE:
        // lParam -1: DefWindowProc must not repaint the (retired) classic
        // frame on activation changes; the bar itself is activation-blind.
        guiPaintBar(w);
        return DefWindowProcA(w, msg, wp, -1);
    case WM_NCHITTEST: {
        // The bar's drag area answers HTCAPTION through DefWindowProc
        // (WS_CAPTION does the work). The close glyph is answered HTCLIENT
        // so its clicks land as WM_LBUTTONDOWN and we own the hover state.
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        RECT wr; GetWindowRect(w, &wr);
        if (pt.y - wr.top >= 0 && pt.y - wr.top < kCapH &&
            pt.x - wr.left >= (wr.right - wr.left) - 46)
            return HTCLIENT;
        break; }
    case WM_NCMOUSEMOVE:
        if (g_capHot) { g_capHot = false; guiPaintBar(w); }   // left the X onto the drag area
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        // v7.0.9 THE WATERMARK-FLICKER FIX: the frame painted straight to the
        // screen behind an erase pass -- RDW_ERASE wiped the client blank
        // (watermark gone), GDI+ rebuilt it, and DWM sampled the blank strip
        // on every tab click / theme press. The full face is rendered into
        // the buffer and lands as ONE blit; no intermediate state exists.
        RECT crc; GetClientRect(w, &crc);
        const int cw = crc.right, ch = crc.bottom;
        if (guiFrameBufEnsure(cw, ch)) {
            guiPaintFrame(g_frmDc, cw, ch);
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
                   ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
                   g_frmDc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        } else {
            guiPaintFrame(dc, cw, ch);   // no buffer: draw straight (old path)
        }
        EndPaint(w, &ps);
        return 0; }
    case WM_ERASEBKGND:
        // The class brush went stale after the first applyTheme (deleted +
        // recreated) -- that is why erase is answered here, not by the class.
        // v7.0.9: it now does NOTHING: the buffered WM_PAINT covers every
        // pixel, and a fill here would only hand DWM a blank intermediate
        // frame (the watermark flicker).
        return 1;
    case WM_CTLCOLORBTN:
        // v6.6.7: DefWindowProc answers WM_CTLCOLORBTN with WHITE, and the
        // white pass under the corner AA was the ghost arcs. Answer DARK so
        // no pass can ever put white under the AA.
        return (LRESULT)g_brBg;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        const int id = GetDlgCtrlID((HWND)lp);
        // v6.6.4 THE REAL SCROLL-GHOST FIX: the log pane is ES_READONLY -- it
        // sends WM_CTLCOLORSTATIC, never WM_CTLCOLOREDIT, so the v6.6.3 fix
        // sat in a message the pane does not send. Its colors are answered
        // HERE instead.
        SetBkMode(dc, OPAQUE);
        if (id == IDC_LOG) {
            SetBkColor(dc, guic::cLogBg);
            SetTextColor(dc, guic::cLog);
            return (LRESULT)g_brLog;
        }
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
        if (GetDlgCtrlID((HWND)lp) == IDC_LOG) {   // the pane: soft cyan on near-black
            SetBkColor(dc, guic::cLogBg);
            SetTextColor(dc, guic::cLog);
            return (LRESULT)g_brLog;
        }
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
        const bool onPage = !((id >= IDC_TAB0 && id <= IDC_TAB0 + 4) ||
                              id == IDC_START  || id == IDC_APPLY ||
                              id == IDC_SAVE   || id == IDC_TICK  ||
                              id == IDC_THEME);
        // v6.6.7: erase FIRST in PLAIN GDI -- a GDI+ fill let the square
        // corners blend against the white WM_CTLCOLORBTN pass underneath,
        // leaving bright ghost arcs on every rounded corner.
        FillRect(dc, &rc, onPage ? g_brPanel : g_brBg);
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        const Gdiplus::Rect grc(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
        if (guiIsToggle(id)) {
            // v7.0.0 polish: OFF is a TRANSPARENT face -- border + text over
            // the erased surface. The ON fill stays: it IS the state.
            // v7.1.15 CONTRAST: the field could not read the states --
            // ON's bright accent text on the accentDk face measures
            // ~3.3:1 (DARK CYAN), the muddiest label on the page. ON is
            // now a FULL accent face with the theme's dark on-accent ink
            // (cOnText): ~11:1, and a filled pill reads as a switch, not
            // a label. OFF keeps its transparent face but the border
            // steps up from cLine (near-invisible on the dark themes) to
            // cDim so the pill itself is visible in every theme.
            const bool on = g_tog[id];
            Gdiplus::GraphicsPath* p = guiRoundPath(grc, 10);
            Gdiplus::SolidBrush fb(GCol(guic::cAccent));
            Gdiplus::Pen        pb(GCol(on ? guic::cAccent : guic::cDim), 1.0f);
            if (on) g.FillPath(&fb, p);
            g.DrawPath(&pb, p);
            delete p;
            // v7.0.0: OFF reads in full text color -- cDim washed out on
            // every theme; v7.1.15: ON reads in cOnText on the accent face.
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
    case WM_LBUTTONDOWN: {
        // v7.0.9: clicks in the bar arrive with NEGATIVE-y client coords
        // (the strip lies above the client origin). Inside the close slot
        // they close -- which hides to the tray, engine stays up.
        const int cx = (short)LOWORD(lp), cy = (short)HIWORD(lp);
        if (cy < 0) {
            RECT cl = guiCapCloseRect(w);
            if (cx >= cl.left && cx < cl.right) PostMessageA(w, WM_CLOSE, 0, 0);
            return 0;
        }
        break; }
    case WM_MOUSEMOVE: {
        // hover feedback for the bar's X; TrackMouseEvent arms the leave
        const int cx = (short)LOWORD(lp), cy = (short)HIWORD(lp);
        bool hot = false;
        if (cy < 0) {
            RECT cl = guiCapCloseRect(w);
            hot = (cx >= cl.left && cx < cl.right);
        }
        if (hot != g_capHot) {
            g_capHot = hot;
            if (hot) { TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, w, 0 }; TrackMouseEvent(&t); }
            guiPaintBar(w);
        }
        break; }
    case WM_MOUSELEAVE:
        if (g_capHot) { g_capHot = false; guiPaintBar(w); }
        break;
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
        if (id >= IDC_TAB0 && id <= IDC_TAB0 + 4) { guiShowPage(w, id - IDC_TAB0); return 0; }
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
                DestroyWindow(w);
                return 0;
            case IDC_START:
                if (g_engState.load() == 0) guiStartEngine(w); else guiStopEngine();
                return 0;
            case IDC_SAVE: {
                cfgFromControls(w); cfgSanitize();
                guiSetStatus(w, guiSaveIni() ? "ini saved" : "ini save FAILED");
                return 0; }
            case IDC_TICK: {                   // v7.0.0: the 120 bpm sync tick -- PLAY/STOP toggle
                if (g_engState.load() == 0) {
                    guiSetStatus(w, "tick test: start the engine first");
                    return 0;
                }
                if (g_tickOn.load(std::memory_order_acquire)) {
                    // v7.0.0: second press -- stop the test where it is
                    g_tickOn.store(false, std::memory_order_release);
                    g_tickSeq0.store(-1, std::memory_order_relaxed);
                    g_tickEndMs.store(0, std::memory_order_relaxed);
                    SetDlgItemTextA(w, IDC_TICK, "120 BPM TICK TEST");
                    guiSetStatus(w, "tempo tick: stopped");
                    return 0;
                }
                tickRender();
                // Raw dispatch: beat zero = one second of capture time ahead;
                // both pumps write the SAME samples at the SAME capture-seq
                // position, onto FL/FR/LFE and the center pair. No latency
                // compensation -- the flam between the speakers IS the data.
                g_tickSeq0.store((long long)g_leg[0].captured.load(std::memory_order_relaxed)
                                 + (long long)kSampleRate, std::memory_order_release);
                g_tickEndMs.store(GetTickCount64() + 1000ULL + 4000ULL,
                                  std::memory_order_relaxed);
                g_tickWrites[0].store(0); g_tickWrites[1].store(0);   // v7.1.11 census
                g_tickOn.store(true, std::memory_order_release);
                SetDlgItemTextA(w, IDC_TICK, "STOP TICK TEST");
                guiSetStatus(w,
                    "tempo tick: 120 bpm, 8 beats (4 s) -- raw, all four channels");
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
                    guiStopEngine();
                    guiStartEngine(w);
                    guiSetStatus(w,
                                    ok ? "saved -- engine restarted"
                                       : "save FAILED -- restarted with the previous ini");
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
        // v6.6.3 polish, v6.6.5 size: the layout is hand-placed at fixed
        // coordinates and there is no WM_SIZE reflow -- lock the frame to the
        // designed 880x620 client; min == max == no resizing at all.
        RECT r = { 0, 0, KCLI_W, KCLI_H };
        AdjustWindowRect(&r, kGuiFrame, FALSE);
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = mmi->ptMaxTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = mmi->ptMaxTrackSize.y = r.bottom - r.top;
        return 0; }
    case WM_TIMER: {
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
        // v7.0.2: the tick test ends itself AND the button untoggles with it
        // -- nothing on the engine side cleared g_tickOn at the natural end,
        // so the GUI timer owns it: flag, anchor, label. The else side keeps
        // the old job: an engine restart voids the run and the stale endMs
        // restores the label.
        if (g_tickOn.load(std::memory_order_relaxed)) {
            const unsigned long long end = g_tickEndMs.load(std::memory_order_relaxed);
            if (end != 0 && GetTickCount64() > end) {
                g_tickOn.store(false, std::memory_order_release);
                g_tickSeq0.store(-1, std::memory_order_relaxed);
                g_tickEndMs.store(0, std::memory_order_relaxed);
                SetDlgItemTextA(w, IDC_TICK, "120 BPM TICK TEST");
                guiSetStatus(w, "tempo tick: finished");
                tickCensusLog();                     // v7.1.11: the witness line
            }
        } else {
            const unsigned long long end = g_tickEndMs.load(std::memory_order_relaxed);
            if (end != 0 && GetTickCount64() > end) {
                g_tickEndMs.store(0, std::memory_order_relaxed);
                SetDlgItemTextA(w, IDC_TICK, "120 BPM TICK TEST");
            }
        }
        guiFlushLogRing();                               // v7.1.6: one shared drain
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
        if (!g_lastHealth.empty()) { s += "   |   "; s += g_lastHealth; }
        // v7.0.7: WM_SETTEXT repaints a STATIC even when the text is
        // identical -- 4 idle repaints a second for nothing. Set only on
        // change; the task-manager CPU line stays at the engine's floor.
        static std::string s_lastStatus;
        if (s != s_lastStatus && !anySliderDragging()) {   // v7.1.5: no paint storm mid-drag
            s_lastStatus = s;
            guiSetStatus(w, s.c_str());
        }
        return 0; }
    case WM_APP + 2:                                   // engine thread ended
        guiRepaint(w, IDC_START);                      // v6.6.1
        return 0;
    case WM_APP + 3: {                                 // v6.6.1: tray icon events
        if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) {
            guiRestoreWindow();
        } else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
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
            if (g_cfg.startupEngine && g_engState.load() == 0)
                guiStartEngine(w);
            trayAdd(false);
            ShowWindow(w, SW_HIDE);
        }
        return 0; }
    case WM_CLOSE:
        // v6.6.1: closing hides to the tray -- the engine keeps running.
        // Real exit lives in the tray menu (Exit), which stops the engine
        // first. A one-time balloon tells the user where the window went.
        // v7.1.7: the taskbar slot goes away WITH the panel.
        tbHideTab(w);
        trayAdd(true);
        ShowWindow(w, SW_HIDE);
        return 0;
    case WM_DESTROY:
        trayRemove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

// ---- entry ------------------------------------------------------------------
// v6.6.1: startTray -- --tray launches the window already hidden in the tray
// (the "Run at Windows startup" entry uses it).
static int guiMain(const std::string& iniPath, bool startTray) {
    g_guiIniPath = iniPath;
    g_startTray  = startTray;
    g_logSink = guiLogSink;                            // everything logs to the pane now
    FreeConsole();                                     // the window replaces the console
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
        MessageBoxA(nullptr, "twinspeak is already running.\n\nStop the running copy first:\n  taskkill /IM twinspeak.exe",
                    "TidyVision TwinSpeak", MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 1;
    }
    if (!loadIni(iniPath.c_str())) writeTemplate(iniPath.c_str());   // show defaults if absent
    // v7.1.5 boot pop, round two (winner side): a PLAIN launch that beats the
    // Run-key --tray instance to the mutex shows its window and lets the real
    // startup twin die silently -- the boot pop, from the other direction.
    // Inside the logon window, with startup_engine opted in, a plain launch
    // IS the startup path: run it exactly like --tray (tray-only, engine on,
    // no window built at all).
    if (!startTray && g_cfg.startupEngine && withinLogonWindow(kBootWindowMs)) {
        tlog("[twinspeak] gui           : plain launch inside the logon window -- running it as the\n");
        tlog("                            startup instance (tray-only, startup engine)\n");
        startTray    = true;
        g_startTray  = true;
    }
    guiMakeGdi();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // v7.1.7: ITaskbarList
    // v6.6.5: GDI+ session for the antialiased owner-drawn UI + watermark
    {
        Gdiplus::GdiplusStartupInput sin_;
        Gdiplus::GdiplusStartup(&g_gdipToken, &sin_, nullptr);
    }
    g_taskbarCreated = RegisterWindowMessageA("TaskbarCreated");   // v6.6.1
    WNDCLASSA wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;   // v7.1.4: the window gets its drop shadow back
    wc.lpfnWndProc = guiWndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hIcon = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(1));
    if (!wc.hIcon) wc.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // v7.0.0: the erase is ours (WM_ERASEBKGND above);
                                  // a class-held brush would go stale the first time
                                  // applyTheme() deletes + recreates g_brBg
    wc.lpszClassName = "twinspeak.control";
    RegisterClassA(&wc);
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
        g_trayWnd = CreateWindowExA(0, "twinspeak.control", "TidyVision TwinSpeak: Tray",
                                    WS_OVERLAPPED, 0, 0, 0, 0,
                                    nullptr, nullptr, wc.hInstance, nullptr);
        if (!g_trayWnd) { if (mutex) CloseHandle(mutex); return 1; }
        g_hwnd = g_trayWnd;
        trayAdd(false);
        trayBalloon("Running in the tray -- the engine is starting.\n"
                    "Double-click the icon to open the control panel; "
                    "right-click for Start/Stop and Exit.");
        tlog("[twinspeak] gui           : tray-only start -- no window exists; double-click the tray icon\n");
        tlog("                            to build and open the control panel (no taskbar button).\n");
        // v7.1.2 STARTUP ENGINE: the startup launch is the boot path -- the
        // user signs in and the splitter must simply be running. With
        // startup_engine on (the default) the engine starts here, straight
        // from the ini (no controls exist yet -- cfgFromControls keeps g_cfg).
        // (A missing key parses as ON; explicit off restores the old
        // wait-for-START behavior.)
        if (g_cfg.startupEngine) {
            tlog("[twinspeak] gui           : startup launch -- auto-starting the engine (startup_engine).\n");
            guiStartEngine(g_hwnd);
        }
    } else {
        // the double-click path: build the panel, show it (guiCreateRealWindow
        // renders the whole tree into the hidden redirection surface first --
        // the v6.6.5 white-flash trick -- so SW_SHOWNA lands one finished
        // frame, no activate/erase cycle).
        if (!guiCreateRealWindow()) { if (mutex) CloseHandle(mutex); return 1; }
        ShowWindow(g_hwnd, SW_SHOWNA);   // v6.6.5: NA -- no activate repaint
        tbShowTab(g_hwnd);               // v7.1.7: the taskbar slot exists THIS instant
        tlog("[twinspeak] gui           : START runs the engine in this window; the pane below is its log.\n");
        tlog("[twinspeak] gui           : closing the window hides it to the tray -- the engine stays up.\n\n");
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
                                ? (g_tab + 4) % 5 : (g_tab + 1) % 5);
            continue;
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
    if (g_engState.load() != 0) guiStopEngine();
    if (g_gdipToken) { Gdiplus::GdiplusShutdown(g_gdipToken); g_gdipToken = 0; }   // v6.6.5
    if (mutex) CloseHandle(mutex);
    return 0;
}

// ---------------------------------------------------------------- main ------
static BOOL WINAPI ctrlHandler(DWORD) { g_run.store(false); return TRUE; }

// v6.6.0: the engine body, split out of main() so the GUI runs it on a
// worker thread and can restart it in-process. The caller owns the single-
// instance mutex. guiMode: no background/FreeConsole detach (the log pane is
// the destination) and restart-safe (g_run re-armed, all engine globals
// re-initialized below).
static int engineMain(const std::string& iniPath, bool fgForce,
                      bool allowMulti, bool guiMode);

int main(int argc, char** argv) {
    InitializeCriticalSection(&g_logCS);   // v5.0 logger
    std::string iniPath;
    bool doList = false, doListKS = false, fgForce = false, allowMulti = false;
    bool wantGui = false, startTray = false;   // v6.6.1: --gui / --tray
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--list")) doList = true;
        else if (!strcmp(argv[i], "--list-ks")) doListKS = true;   // v6.3
        else if (!strcmp(argv[i], "--ini") && i + 1 < argc) iniPath = argv[++i];
        else if (!strcmp(argv[i], "--foreground")) fgForce = true;    // v6.2
        else if (!strcmp(argv[i], "--multi"))      allowMulti = true; // v6.2
        else if (!strcmp(argv[i], "--gui"))      wantGui = true;      // v6.6.0: explicit, same as default
        else if (!strcmp(argv[i], "--tray"))     { wantGui = true; startTray = true; }   // v6.6.1: window hidden in the tray
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            tlog("usage: twinspeak [--list] [--list-ks] [--ini path] [--foreground] [--multi]\n"
                 "  (no args)     the control window -- every ini option + START/STOP + live log\n"
                 "                (closing it hides to the TRAY; the engine stays up)\n"
                 "  --tray        straight to the system tray -- NO window exists until you\n"
                 "                click the tray icon (never a taskbar button; this is what\n"
                 "                the tray's 'Run at Windows startup' writes -- use it for\n"
                 "                Task Scheduler sign-in tasks too)\n"
                 "  --list        show every WASAPI endpoint (the usual device needles)\n"
                 "  --list-ks     show every WDM-KS render filter with its pins/formats (v6.3)\n"
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

    if (doList) { listDevices(); return 0; }
    if (doListKS) { ksListKs(); return 0; }   // v6.3

    // v6.6.0: NO ARGUMENTS = the control window. The console modes all keep
    // their exact behavior behind explicit flags. v6.6.1: --gui is the same
    // default made explicit; --tray starts hidden in the tray.
    if (argc <= 1 || wantGui) return guiMain(iniPath, startTray);

    // v6.2 single-instance lock (console runs; the GUI holds its own).
    HANDLE hInstance = nullptr;
    if (!allowMulti) {
        hInstance = CreateMutexA(nullptr, TRUE, "Local\\twinspeak.instance");
        if (hInstance && GetLastError() == ERROR_ALREADY_EXISTS) {
            tlog( "[twinspeak] already running -- twinspeak is single-instance.\n"
                            "            stop the running copy first:  taskkill /IM twinspeak.exe\n"
                            "            (a deliberate second copy: add --multi)\n");
            return 1;
        }
    }
    const int rc = engineMain(iniPath, fgForce, allowMulti, false);
    if (hInstance) CloseHandle(hInstance);
    return rc;
}

static int engineMain(const std::string& iniPath, bool fgForce,
                      bool allowMulti, bool guiMode) {
    (void)allowMulti;   // the lock is the caller's business
    g_run.store(true);  // v6.6.0: fresh run -- a GUI restart arrives with it false
    g_stopSilence.store(false, std::memory_order_relaxed);  // v7.0.0: restart-safety
    g_tickOn.store(false, std::memory_order_relaxed);       // v7.0.0: a restart voids a running tick test
    g_tickSeq0.store(-1, std::memory_order_relaxed);
    if (!loadIni(iniPath.c_str())) {
        writeTemplate(iniPath.c_str());
        tlog( "[twinspeak] no config at %s\n"
                        "            wrote a template there -- edit the three device names and rerun.\n"
                        "            (tip: twinspeak --list shows every device name)\n", iniPath.c_str());
        return 1;
    }
    if (g_cfg.inputDevice.empty() || g_cfg.outputDevice1.empty() || g_cfg.outputDevice2.empty()) {
        tlog( "[twinspeak] config must set input_device, output_device1, output_device2\n");
        return 1;
    }

    // v6.5.17: HIGH process priority class. MMCSS orders the RT threads
    // WITHIN the process; the class is the scheduler's cross-process weight
    // (at NORMAL a foreground boost can bury the helper threads). HIGH is
    // the weight a DAW host takes; it does not starve other streams.
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    timeBeginPeriod(1);
    SetConsoleCtrlHandler(ctrlHandler, TRUE);
    g_bootMs = GetTickCount64();           // v3.8 watchdog grace timer
    {                                      // v6.4: lag display clock anchors
        LARGE_INTEGER f, c;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&c);
        g_qpf = (double)f.QuadPart;
        g_qpcBoot = c;
    }

    // keep-alive cycle table (BUILT IN -- 40 Hz @ 0.001 since v7.1.12, no ini, no UI)
    g_kaHz = kKeepAliveHz;
    g_kaGain = kKeepAliveGain;
    g_kaLen = (unsigned)((double)kSampleRate / (double)g_kaHz + 0.5);
    if (g_kaLen < 8) g_kaLen = 8;
    if (g_kaLen > kKeepAliveTableMax) g_kaLen = kKeepAliveTableMax;
    for (unsigned i = 0; i < g_kaLen; ++i)
        g_kaTable[i] = (float)(sin(kTwoPi * (double)g_kaHz * (double)i / (double)kSampleRate)
                               * (double)g_kaGain);

    // v6.5.18: center dialogue EQ coefficients (ini: center_eq*)
    centerEqInit();

    // v6.2 chimes: render the modernized signature ONCE -- C7 42 ms -> G7
    // 72 ms triangle blips, 20 ms gap, exp decay, peak = chime_gain. Plays
    // on engine start and on every watchdog reopen of the center leg.
    {
        float cg = g_cfg.chimeGain;
        if (cg < 0.0f) cg = 0.0f;
        if (cg > 1.0f) cg = 1.0f;
        g_chimeBuf = renderChime(2093.00, 42.0, 3135.97, 72.0, 20.0, cg);
    }

    ma_backend be[] = { ma_backend_wasapi };
    ma_context ctx;
    if (ma_context_init(be, 1, nullptr, &ctx) != MA_SUCCESS) {
        tlog( "[twinspeak] WASAPI context init failed\n");
        return 1;
    }

    ma_device_id capId, id1, id2; std::string capName, n1, n2;
    if (!pickDevice(&ctx, ma_device_type_capture, g_cfg.inputDevice, &capId, &capName)) {
        tlog( "[twinspeak] capture device matching \"%s\" not found (--list to see names)\n",
                g_cfg.inputDevice.c_str());
        return 1;
    }
    if (!pickDevice(&ctx, ma_device_type_playback, g_cfg.outputDevice1, &id1, &n1)) {
        tlog( "[twinspeak] output_device1 matching \"%s\" not found (--list)\n",
                g_cfg.outputDevice1.c_str());
        return 1;
    }
    if (!pickDevice(&ctx, ma_device_type_playback, g_cfg.outputDevice2, &id2, &n2)) {
        tlog( "[twinspeak] output_device2 matching \"%s\" not found (--list)\n",
                g_cfg.outputDevice2.c_str());
        return 1;
    }

    // per-leg periods, targets, rings -- fully independent. v3.6: the
    // capture burst no longer inherits min(out1ms, out2ms) -- cap_ms owns it.
    const ma_uint32 periodMs1 = (ma_uint32)(g_cfg.out1Ms + 0.5f);
    const ma_uint32 periodMs2 = (ma_uint32)(g_cfg.out2Ms + 0.5f);
    const ma_uint32 capPeriodMs = (ma_uint32)(g_cfg.capMs + 0.5f);
    g_capPeriodMsF = g_cfg.capMs;          // v3.8: watchdog restart state
    g_capNeedle    = g_cfg.inputDevice;
    g_capWantExcl  = g_cfg.captureExclusive;

    // per-leg identity + work buffers (rings come later -- their size needs
    // the negotiated device period)
    for (int i = 0; i < 2; ++i) {
        Leg& L = g_leg[i];
        L.index = i;
        L.outCh = (i == 0) ? kOutCh1 : kOutCh2;
        L.name  = (i == 0) ? n1 : n2;
        L.scratch.assign((size_t)kInCh * kScratchFrames, 0.0f);
        L.chunk.assign((size_t)L.outCh * kScratchFrames, 0.0f);
    }

    // v3: legs go exclusive TOGETHER or shared TOGETHER. v2 let one leg win
    // exclusive while the other fell back -- mixed device buffering (2x5 vs
    // 2x10 ms) put the legs ~10 ms apart, which reads as "out2 is delayed".
    // v3.8 (out2_first): the CENTER leg is opened first so its stream (and
    // the 40 Hz keep-alive) establishes before the mains leg.
    // v6.3: a leg whose backend = ks tries the WDM-KS pin FIRST (out2_first
    // ordering respected); a refusal falls back to the proven WASAPI path
    // below, so a leg is never left without a device. The align_modes dance
    // then applies to the WASAPI legs only (KS pins live under the endpoint
    // layer and do not participate in shared-mode buffering).
    const bool wantKS1 = (g_cfg.backend1 == "ks");
    const bool wantKS2 = (g_cfg.backend2 == "ks");
    bool ok2 = false, ok1 = false;
    if (wantKS2) ok2 = ksOpenLeg(1, g_cfg.outputDevice2);
    if (wantKS1) ok1 = ksOpenLeg(0, g_cfg.outputDevice1);
    if (wantKS2 && !ok2)
        tlog("[twinspeak] leg2 KS        : refused -- falling back to the WASAPI exclusive path\n");
    if (wantKS1 && !ok1)
        tlog("[twinspeak] leg1 KS        : refused -- falling back to the WASAPI exclusive path\n");
    g_leg[1].backend = (ok2 && wantKS2) ? 1 : 0;
    g_leg[0].backend = (ok1 && wantKS1) ? 1 : 0;
    if (!ok2) ok2 = initLegDevice(1, &ctx, &id2, periodMs2, g_cfg.exclusive);
    // v3: legs go exclusive TOGETHER or shared TOGETHER (v2 let one leg win
    // and the buffering mismatch skewed them ~10 ms). A driver refusal on
    // either WASAPI leg reopens BOTH shared so device buffering stays
    // identical; KS legs are committed to their pin and stay as-is.
    if (!ok1) ok1 = initLegDevice(0, &ctx, &id1, periodMs1, g_cfg.exclusive);
    if ((!ok1 || !ok2) && g_cfg.exclusive) {
        if (g_cfg.alignModes) {
            // Only WASAPI legs participate in the dance; a KS leg is already
            // committed to its pin and stays as-is.
            const bool dance1 = (g_leg[0].backend == 0);
            const bool dance2 = (g_leg[1].backend == 0);
            tlog( "[twinspeak] align_modes: exclusive %s -- reopening the WASAPI legs shared so device buffering stays identical\n",
                    (!ok1 && !ok2) ? "refused by both drivers" : "refused on one leg");
            if (ok1 && dance1) {
                ma_device_uninit(&g_leg[0].dev);
                memset(&g_leg[0].dev, 0, sizeof(g_leg[0].dev));
                ok1 = false;
            }
            if (ok2 && dance2) {
                ma_device_uninit(&g_leg[1].dev);
                memset(&g_leg[1].dev, 0, sizeof(g_leg[1].dev));
                ok2 = false;
            }
            if (dance1 && !ok1) ok1 = initLegDevice(0, &ctx, &id1, periodMs1, false);
            if (dance2 && !ok2) ok2 = initLegDevice(1, &ctx, &id2, periodMs2, false);
        } else {
            if (!ok1 && g_leg[0].backend == 0) ok1 = initLegDevice(0, &ctx, &id1, periodMs1, false);
            if (!ok2 && g_leg[1].backend == 0) ok2 = initLegDevice(1, &ctx, &id2, periodMs2, false);
        }
    }
    if (!ok1 || !ok2) {
        tlog( "[twinspeak] giving up -- a playback device could not be opened in any mode\n");
        return 1;
    }

    // v3.6: the CAPTURE side gets its own period + share mode, opened BEFORE
    // the rings (the ring floor needs the REAL negotiated burst). Exclusive
    // on the VAC pin = the direct WaveRT cyclic read; it usually refuses on
    // WaveCyclic/busy and falls back to shared (banner reports the winner).
    ma_device_config cc = ma_device_config_init(ma_device_type_capture);
    cc.sampleRate = kSampleRate;
    cc.periodSizeInMilliseconds = capPeriodMs;
    cc.periods = 2;
    cc.dataCallback = capCallback;
    cc.capture.pDeviceID = &capId;
    cc.capture.format = ma_format_f32;
    cc.capture.channels = kInCh;
    cc.capture.shareMode = g_cfg.captureExclusive ? ma_share_mode_exclusive
                                                  : ma_share_mode_shared;
    cc.wasapi.usage = ma_wasapi_usage_pro_audio;
    memset(&g_capDev, 0, sizeof(g_capDev));   // v3.8: g_capDev is file-scope (watchdog)
    bool capExclusive = false;
    ma_result cr = ma_device_init(&ctx, &cc, &g_capDev);
    if (cr == MA_SUCCESS) {
        capExclusive = (cc.capture.shareMode == ma_share_mode_exclusive);
        g_capIsExcl  = capExclusive;
    } else if (g_cfg.captureExclusive) {
        tlog( "[twinspeak] capture EXCLUSIVE refused (%s) -- falling back to shared\n",
                ma_result_description(cr));
        cc.capture.shareMode = ma_share_mode_shared;
        cr = ma_device_init(&ctx, &cc, &g_capDev);
    }
    if (cr != MA_SUCCESS) {
        tlog( "[twinspeak] capture init failed: %s\n", ma_result_description(cr));
        return 1;
    }

    // v4.0: the REAL negotiated device period per leg feeds the ring floor
    // and the gated-drop line. v6.3: a KS leg's bite is exactly
    // ks_period_frames (our pacing thread never asks for more).
    for (int i = 0; i < 2; ++i) {
        if (g_leg[i].backend == 1) {
            g_periodFrames[i] = (size_t)g_cfg.ksPeriodFrames;
            continue;
        }
        g_periodFrames[i] = g_leg[i].dev.playback.internalPeriodSizeInFrames
                          ? (size_t)g_leg[i].dev.playback.internalPeriodSizeInFrames
                          : (size_t)(((i == 0) ? periodMs1 : periodMs2) * (kSampleRate / 1000));
    }
    g_capBurstFrames = g_capDev.capture.internalPeriodSizeInFrames
                     ? (size_t)g_capDev.capture.internalPeriodSizeInFrames
                     : (size_t)(capPeriodMs * (kSampleRate / 1000));
    if (g_capBurstFrames == 0) g_capBurstFrames = 240;

    // rings, now that each leg's real device period AND burst are known.
    // Target floor = FULL device buffer + burst + margin (in miniaudio's
    // exclusive event mode the pop is the WHOLE buffer as two back-to-back
    // callbacks); capacity = target + spare so the pop/burst sawtooth never
    // reaches the overwrite line. This target is the LATENCY ANCHOR.
    for (int i = 0; i < 2; ++i) {
        Leg& L = g_leg[i];
        const float ms = (i == 0) ? g_cfg.out1Ms : g_cfg.out2Ms;
        float ringMs = (i == 0) ? g_cfg.ring1Ms : g_cfg.ring2Ms;
        const float delayMs = (i == 0) ? g_cfg.delay1Ms : g_cfg.delay2Ms;
        if (ringMs < 0.0f) ringMs = ms;   // v3.2: ring1_ms/ring2_ms default to outXms
        const size_t cfgFrames    = (size_t)((double)kSampleRate * ringMs / 1000.0);
        // v6.3/v6.3.1: a KS leg's bite is one period exactly, and its pull is
        // our own paced thread -- so its margin is 1 ms over (bite + burst);
        // a WASAPI leg keeps the 4 ms (exclusive event mode pops the whole
        // buffer as two back-to-back callbacks).
        const size_t devPeriodFr  = (L.backend == 1)
                                  ? (size_t)g_cfg.ksPeriodFrames
                                  : (L.dev.playback.internalPeriodSizeInFrames
                                     ? L.dev.playback.internalPeriodSizeInFrames : 480);
        const size_t marginFr     = (L.backend == 1) ? kKsMarginFrames
                                                     : kPeriodMarginFrames;
        // v6.1: the alignment delay IS extra ring depth -- the samples leave
        // the ring later, bit for bit; the gated-drop line rides the SAME
        // target so the delay cannot cause false drops; prefill covers it.
        L.delayFrames = (size_t)((double)kSampleRate * delayMs / 1000.0);
        L.delayMs     = delayMs;
        L.targetFrames = std::max<size_t>(cfgFrames,
                            devPeriodFr + g_capBurstFrames + marginFr)
                         + L.delayFrames;
        L.raw.init(L.targetFrames * 4, kInCh);
        L.out.init(L.targetFrames + kRingSpareFrames, L.outCh);
        L.out.pushSilence(L.targetFrames);   // prefill so playbacks start smooth
        L.prefillRemaining = L.targetFrames; // v6.4: prefill carries no sequence
        // v6.0 replay buffer: the last played device buffer, kept for the
        // ASIO-convention replay on a shortfall.
        L.lastCap = kReplayMaxPeriods * devPeriodFr;
        L.lastBlock.assign(L.lastCap * (size_t)L.outCh, 0.0f);
        L.lastN = 0;
        L.haveLast = false;
        // v7.0.1: every clock the ledger reads is PER RUN. A restart used to
        // inherit captured/popped/streamSeeded, putting seqBefore and the
        // tick anchor in mismatched capture-sequence spaces (per leg!), and
        // the warden corrected against a stale out-ledger. Zeroed HERE, on
        // the main thread, before any device/pump thread exists.
        L.captured.store(0, std::memory_order_relaxed);
        L.popped.store(0, std::memory_order_relaxed);
        L.streamSeeded = false;
        L.rawDrop.store(0, std::memory_order_relaxed);
        L.outDrop.store(0, std::memory_order_relaxed);
        L.und.store(0, std::memory_order_relaxed);
        L.corrDrops.store(0, std::memory_order_relaxed);
        L.cmUpmix.store(0, std::memory_order_relaxed);
        L.padFrames.store(0, std::memory_order_relaxed);
        L.padTotal.store(0, std::memory_order_relaxed);
        L.wardenDrops.store(0, std::memory_order_relaxed);
        L.wardenPads.store(0, std::memory_order_relaxed);
        L.firstSwitchUs.store(0, std::memory_order_relaxed);   // v7.1.5 start align
        L.lastCorrMs = 0;
    }

    // ---------------- v3.8 COM volume layer (setup on the main thread) -----
    // Before any device starts: resolve the cable's playback endpoint, seed
    // the volume atomics, resolve + force 100%/unmuted the out pins. The
    // watcher takes over both jobs once the devices run.
    const bool comOK = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    if (g_cfg.volumeFollow) {
        // v3.9: needle cascade -- volume_endpoint (ini override) first, then
        // input_device, then a generic "virtual audio cable" match. If all
        // miss, logRenderEndpoints() prints every render endpoint Windows
        // has (with state) so the exact playback-side name is visible.
        std::string volNeedles[3];
        int nN = 0;
        if (!g_cfg.volumeEndpoint.empty()) volNeedles[nN++] = g_cfg.volumeEndpoint;
        volNeedles[nN++] = g_cfg.inputDevice;
        volNeedles[nN++] = "virtual audio cable";
        for (int k = 0; k < nN && !g_srcVol; ++k) {
            bool dup = false;
            for (int j = 0; j < k; ++j)
                if (lower(volNeedles[j]) == lower(volNeedles[k])) dup = true;
            if (dup) continue;
            g_srcVol = findRenderEndpointVolume(volNeedles[k], &g_srcVolName);
        }
        if (g_srcVol) {
            float v = 1.0f; BOOL m = FALSE;
            if (SUCCEEDED(g_srcVol->GetMasterVolumeLevelScalar(&v)))
                g_vol.store(v, std::memory_order_relaxed);
            if (SUCCEEDED(g_srcVol->GetMute(&m)))
                g_volMuted.store(m != FALSE, std::memory_order_relaxed);
            g_volFollowLive.store(true, std::memory_order_relaxed);
        } else {
            logRenderEndpoints();   // v3.9: every render endpoint + its state
        }
    }
    if (g_cfg.pinOutputs) {
        for (int i = 0; i < 2; ++i) {
            if (g_leg[i].backend == 1) {
                // v6.3: a KS pin is a direct line to the DAC -- there is no
                // endpoint volume to pin. volume_follow still governs this
                // leg from the pump side.
                tlog("[twinspeak] out%d: WDM-KS pin -- endpoint pinning not applicable (KS bypasses endpoint volume)\n", i + 1);
                continue;
            }
            g_pin[i].id = g_leg[i].id.wasapi;
            g_pin[i].vol = endpointVolFromIdString(g_leg[i].id.wasapi);
            if (g_pin[i].vol) {
                g_pin[i].vol->SetMasterVolumeLevelScalar(1.0f, nullptr);
                g_pin[i].vol->SetMute(FALSE, nullptr);
            }
        }
    }

    // v3 start order: capture FIRST, pumps next, playbacks last -- the rings
    // hold real audio before the devices open their mouths (no startup
    // underrun burst). v6.1: with out2_first the CENTER leg's pump is also
    // CREATED first (and runs at the higher priority -- see pumpThread).
    if (ma_device_start(&g_capDev) != MA_SUCCESS) {
        tlog( "[twinspeak] capture start failed\n");
        return 1;
    }
    HANDLE hPump1 = nullptr, hPump2 = nullptr;
    if (g_cfg.out2First) {
        hPump2 = CreateThread(nullptr, 0, pumpThread, (LPVOID)(INT_PTR)1, 0, nullptr);
        hPump1 = CreateThread(nullptr, 0, pumpThread, (LPVOID)(INT_PTR)0, 0, nullptr);
    } else {
        hPump1 = CreateThread(nullptr, 0, pumpThread, (LPVOID)(INT_PTR)0, 0, nullptr);
        hPump2 = CreateThread(nullptr, 0, pumpThread, (LPVOID)(INT_PTR)1, 0, nullptr);
    }
    Sleep(30);   // let capture + pumps fill both rings with real audio

    // v7.1.5 START AT THE ANCHOR (half ONE of two -- half two, the start
    // STAGGER align, follows the device starts below): the Sleep(30) gap +
    // the render devices' start latency let the capture side pile REAL
    // frames on top of the prefill -- playback then opened ~10 ms behind
    // and the per-callback settle trim spent its first corrections shaving
    // that backlog (the field: "a ~10ms delay every time I start/restart
    // that eventually gets readjusted"). The excess is trimmed HERE, before
    // either leg starts: the first buffer opens at the target latency and
    // the settle trim stays a safety net. Same discardOldest semantics as
    // the per-callback trim (front-out bit-exact skip; the discarded
    // frames' sequence numbers are consumed with them).
    for (int li = 0; li < 2; ++li) {
        Leg& SL = g_leg[li];
        if (!g_cfg.clockSync || !SL.out.cap) continue;   // sync off / ring not built
        const long long level = (long long)SL.out.count();
        if (level > (long long)SL.targetFrames) {
            const size_t dropped =
                SL.out.discardOldest((size_t)(level - (long long)SL.targetFrames));
            tlog("[twinspeak] leg%d sync     : start trim: dropped %llu fr of pre-playback backlog -- the first buffer opens at the target latency\n",
                 li + 1, (unsigned long long)dropped);
        }
    }

    armChime(true);   // v6.2: the first thing the center speaker plays is the signature

    ma_result sA = MA_SUCCESS, sB = MA_SUCCESS;
    // v6.3: a KS leg "starts" by creating its pacing thread (the thread
    // flips the pin to RUN itself); ma_device_start applies to WASAPI legs.
    // out2_first ordering is honored for both backends.
    if (g_cfg.out2First) {
        if (g_leg[1].backend == 1) {                       // out2 (center) FIRST
            g_leg[1].ks.thread = CreateThread(nullptr, 0, ksThread, (LPVOID)(INT_PTR)1, 0, nullptr);
            if (!g_leg[1].ks.thread) sA = MA_ERROR;
        } else {
            sA = ma_device_start(&g_leg[1].dev);
        }
        if (g_leg[0].backend == 1) {
            g_leg[0].ks.thread = CreateThread(nullptr, 0, ksThread, (LPVOID)(INT_PTR)0, 0, nullptr);
            if (!g_leg[0].ks.thread) sB = MA_ERROR;
        } else {
            sB = ma_device_start(&g_leg[0].dev);
        }
    } else {
        if (g_leg[0].backend == 1) {
            g_leg[0].ks.thread = CreateThread(nullptr, 0, ksThread, (LPVOID)(INT_PTR)0, 0, nullptr);
            if (!g_leg[0].ks.thread) sA = MA_ERROR;
        } else {
            sA = ma_device_start(&g_leg[0].dev);
        }
        if (g_leg[1].backend == 1) {
            g_leg[1].ks.thread = CreateThread(nullptr, 0, ksThread, (LPVOID)(INT_PTR)1, 0, nullptr);
            if (!g_leg[1].ks.thread) sB = MA_ERROR;
        } else {
            sB = ma_device_start(&g_leg[1].dev);
        }
    }
    if (sA != MA_SUCCESS || sB != MA_SUCCESS) {
        tlog( "[twinspeak] playback start failed\n");
        return 1;
    }

    // v7.1.5 START AT THE SAME INSTANT (half two of the start-latency fix) --
    // the start trim above fixed the RING backlog, but the two output devices
    // also begin EATING their prefills when THEIR start call lands, and the
    // calls land a few ms apart (ma_device_start vs the KS thread's RUN flip).
    // A start stagger shifts that leg's WHOLE stream: the early leg stays
    // early by the gap, the ledger reads a FLAT drift (baseline + gap) from
    // the first real frame, the 5..9 s baseline calibration absorbs the gap
    // and the warden never sees it -- the field heard it as "there is still
    // latency between the legs on every start/apply+restart". So: wait for
    // both legs' first buffer switch (the moment each DAC began eating --
    // stamped in bufferSwitchFill), then replay-pad the EARLY leg by exactly
    // the gap. Both streams then open on the same instant, the ledger starts
    // ON the calibrated baseline, and no settle correction ever fires. The
    // pad rides the ring level, so it is clamped under the per-callback trim
    // line (target + burst + period) -- a larger gap is padded as far as is
    // safe and the log says so. Everything lands inside the silence prefill
    // window (guarded), so the filler is literal silence, never a repeat.
    if (g_cfg.clockSync) {
        const unsigned long long alignT0 = GetTickCount64();
        for (;;) {
            const bool sw0 = g_leg[0].popped.load(std::memory_order_acquire) > 0;
            const bool sw1 = g_leg[1].popped.load(std::memory_order_acquire) > 0;
            if (sw0 && sw1) break;
            if (GetTickCount64() - alignT0 > 250) break;   // a dead leg: watchdog territory
            Sleep(1);
        }
        const unsigned long long au0raw = g_leg[0].firstSwitchUs.load(std::memory_order_acquire);
        const unsigned long long au1raw = g_leg[1].firstSwitchUs.load(std::memory_order_acquire);
        // v7.1.11 PERIOD BIAS: the first callback fires one device period
        // AFTER the DAC truly began eating -- miniaudio primes the device
        // buffer, the driver consumes that prime, THEN the first event
        // lands. The two legs' negotiated periods differ, so the raw gap
        // carries a (P1 - P2) bias: the aligner padded the wrong amount
        // (or the wrong leg) and the field heard leg1 arrive late on every
        // start/apply+restart until the warden reeled it in. Compare the
        // TRUE eat-starts: stamp minus this leg's own period. KS legs are
        // left alone -- their pacing thread writes the first period itself,
        // there is no prime for the DAC to eat first.
        long long au0 = (long long)au0raw, au1 = (long long)au1raw;
        if (g_leg[0].backend != 1 && g_periodFrames[0])
            au0 -= (long long)((double)g_periodFrames[0] * 1e6 / (double)kSampleRate);
        if (g_leg[1].backend != 1 && g_periodFrames[1])
            au1 -= (long long)((double)g_periodFrames[1] * 1e6 / (double)kSampleRate);
        if (!au0raw || !au1raw) {
            tlog("[twinspeak] start align  : a leg never handed in its first buffer switch --\n"
                 "                            alignment skipped (the watchdog owns a dead leg)\n");
        } else {
            const long long gapUs  = (long long)au0 - (long long)au1;   // > 0: out1 began eating first
            const long long gapAbs = gapUs < 0 ? -gapUs : gapUs;
            long long gapFr = (gapAbs * (long long)kSampleRate + 500000) / 1000000;
            const int  earlyIdx = (gapUs > 0) ? 0 : 1;
            if (gapFr <= 0) {
                tlog("[twinspeak] start align  : both legs began eating within one frame -- nothing to align\n");
            } else {
                Leg& E = g_leg[earlyIdx];
                // headroom under the per-callback trim line (target + burst + period):
                const long long head = (long long)g_capBurstFrames
                                     + (long long)g_periodFrames[earlyIdx] - 96;
                bool clipped = gapFr > head;
                if (clipped) gapFr = head;
                // the pad must land wholly inside the silence prefill, or the
                // replay filler would repeat real audio (an audible stutter).
                if ((long long)E.prefillRemaining < gapFr) {
                    gapFr = (long long)E.prefillRemaining;
                    clipped = true;
                }
                if (gapFr > 0) {
                    E.padFrames.fetch_add((size_t)gapFr, std::memory_order_relaxed);
                    tlog("[twinspeak] start align  : leg%d began eating %lld us before leg%d -- replay-padded\n"
                         "                            %lld fr (%.2f ms)%s so both legs open their stream on the\n"
                         "                            same instant; the ledger starts ON the calibrated baseline\n"
                         "                            (v7.1.11: stamps are period-corrected -- %zu / %zu fr)\n",
                         earlyIdx + 1, gapAbs, 2 - earlyIdx, gapFr,
                         (double)gapFr * 1000.0 / (double)kSampleRate,
                         clipped ? " (clipped to the safe pad)" : "",
                         g_periodFrames[0], g_periodFrames[1]);
                } else {
                    tlog("[twinspeak] start align  : stagger %lld us, but no silence prefill left to pad inside --\n"
                         "                            the early leg already plays; leaving it to the warden\n", gapAbs);
                }
            }
        }
    }

    // v7.1.14 FIRST-SECOND FAST TIER -- the drift correction at 10x speed,
    // for exactly one second, then back to normal. The warden is the only
    // steady-state drift corrector and every one of its gates is wide open
    // when the tick test looks: the baseline arms at seconds 5..9, the
    // trigger is |err| >= drift_correct_ms held 3 s, corrections space 5 s
    // apart. A start residual that survives the start align (the stamp
    // model's error: a driver priming its WaveRT buffer differently, a pad
    // clipped to the safe headroom, a burst the trim line ate) sits in
    // that unguarded window, the baseline then ABSORBS it, and the flam
    // is on the tick forever. So: for the first 1000 ms of a run
    // (hardcoded -- no ini key, no UI) the corrector runs the warden's own
    // bit-exact table at ten times the cadence -- sample every 100 ms
    // (the health tick is 1000 ms), decide in 3 fast windows (300 ms
    // instead of 3 s), re-fire every 500 ms (drift_interval_ms / 10
    // instead of 5 s) -- against the ini's own delay model,
    // -(delay1 - delay2) ms: the reading a truly synced pair settles at,
    // since a deeper ring holds its play boundary back. Trigger distance
    // is unchanged: the same |err| >= drift_correct_ms the warden uses --
    // same threshold, ten times the speed. After 1000 ms this loop exits,
    // the engine returns to the normal 1 s health tick, and the warden
    // calibrates its baseline at seconds 5..9 ON the corrected path.
    // Transport-level only: ring skips and in-place replay pads, never a
    // resampled sample.
    if (g_cfg.clockSync && g_cfg.driftGuard) {
        // both ledgers must carry real sequence first (bounded: a starved
        // leg is watchdog territory)
        const unsigned long long ftWait0 = GetTickCount64();
        for (;;) {
            if (g_leg[0].out.seqTail.load(std::memory_order_acquire) > 0
             && g_leg[1].out.seqTail.load(std::memory_order_acquire) > 0) break;
            if (!g_run.load(std::memory_order_relaxed)) break;
            if (GetTickCount64() - ftWait0 > 500) break;
            Sleep(2);
        }
        const double modelMs = -(double)(g_cfg.delay1Ms - g_cfg.delay2Ms);
        const unsigned long long ft0 = GetTickCount64();
        int streak = 0, fires = 0;
        unsigned long long lastFireMs = 0;
        while (g_run.load(std::memory_order_relaxed)
               && GetTickCount64() - ft0 < kFastTierMs) {
            const long long s0 = (long long)g_leg[0].out.seqTail.load(std::memory_order_acquire);
            const long long s1 = (long long)g_leg[1].out.seqTail.load(std::memory_order_acquire);
            double err = 0.0;
            if (s0 > 0 && s1 > 0) {
                err = (double)(s0 - s1) / 48.0 - modelMs;
                if (err >= g_cfg.driftCorrectMs || err <= -g_cfg.driftCorrectMs) ++streak;
                else streak = 0;
            } else streak = 0;
            const unsigned long long nowMs = GetTickCount64();
            if (streak >= (int)kFastTierStreak && nowMs - lastFireMs >= kFastTierSpacingMs) {
                const long long errFr = (long long)(err * 48.0);
                const long long dpt0 = (long long)g_leg[0].out.count() - (long long)g_leg[0].targetFrames;
                const long long dpt1 = (long long)g_leg[1].out.count() - (long long)g_leg[1].targetFrames;
                bool did = false;
                if (err > 0 && dpt1 > 0) {          // leg2 late: give the real excess back
                    Leg& W = g_leg[1];
                    long long x = errFr < dpt1 ? errFr : dpt1;
                    const long long lvl = (long long)W.out.count();
                    if (x > lvl) x = lvl;
                    if (x > 0) {
                        W.out.discardOldest((size_t)x);   // seqTail advances: a true skip
                        W.wardenDrops.fetch_add(1, std::memory_order_relaxed);
                        tlog("[twinspeak] fast tier    : leg2 re-pinned -- skipped %lld fr (%.1f ms) of the\n"
                             "                            startup residual; drift %+.1f ms -> toward the delay model %+.1f ms\n",
                             x, (double)x * 1000.0 / (double)kSampleRate, err + modelMs, modelMs);
                        did = true;
                    }
                } else if (err > 0 && dpt0 < 0) {   // leg1 early: hold it (in-place replay)
                    Leg& W = g_leg[0];
                    long long x = -errFr < -dpt0 ? -errFr : -dpt0;
                    const long long head = (long long)g_capBurstFrames
                                         + (long long)g_periodFrames[0] - 96;
                    if (x > head) x = head;
                    if (x > (long long)kWardenMaxPadFrames) x = (long long)kWardenMaxPadFrames;
                    if (x > 0) {
                        W.padFrames.fetch_add((size_t)x, std::memory_order_relaxed);
                        W.wardenPads.fetch_add(1, std::memory_order_relaxed);
                        tlog("[twinspeak] fast tier    : leg1 re-pinned -- replay-padded %lld fr (%.1f ms) of the\n"
                             "                            startup residual; drift %+.1f ms -> toward the delay model %+.1f ms\n",
                             x, (double)x * 1000.0 / (double)kSampleRate, err + modelMs, modelMs);
                        did = true;
                    }
                } else if (err < 0 && dpt0 > 0) {   // leg1 late: give the real excess back
                    Leg& W = g_leg[0];
                    long long x = -errFr < dpt0 ? -errFr : dpt0;
                    const long long lvl = (long long)W.out.count();
                    if (x > lvl) x = lvl;
                    if (x > 0) {
                        W.out.discardOldest((size_t)x);
                        W.wardenDrops.fetch_add(1, std::memory_order_relaxed);
                        tlog("[twinspeak] fast tier    : leg1 re-pinned -- skipped %lld fr (%.1f ms) of the\n"
                             "                            startup residual; drift %+.1f ms -> toward the delay model %+.1f ms\n",
                             x, (double)x * 1000.0 / (double)kSampleRate, err + modelMs, modelMs);
                        did = true;
                    }
                } else if (err < 0 && dpt1 < 0) {   // leg2 early: hold it
                    Leg& W = g_leg[1];
                    long long x = -errFr < -dpt1 ? -errFr : -dpt1;
                    const long long head = (long long)g_capBurstFrames
                                         + (long long)g_periodFrames[1] - 96;
                    if (x > head) x = head;
                    if (x > (long long)kWardenMaxPadFrames) x = (long long)kWardenMaxPadFrames;
                    if (x > 0) {
                        W.padFrames.fetch_add((size_t)x, std::memory_order_relaxed);
                        W.wardenPads.fetch_add(1, std::memory_order_relaxed);
                        tlog("[twinspeak] fast tier    : leg2 re-pinned -- replay-padded %lld fr (%.1f ms) of the\n"
                             "                            startup residual; drift %+.1f ms -> toward the delay model %+.1f ms\n",
                             x, (double)x * 1000.0 / (double)kSampleRate, err + modelMs, modelMs);
                        did = true;
                    }
                }
                if (did) {
                    ++fires;
                    lastFireMs = nowMs;
                    tlog("[twinspeak] fast tier    : fired at %llu ms -- 10x cadence (%u ms samples, %u ms\n"
                         "                            spacing); the warden takes over at the normal speed now\n",
                         nowMs - ft0, kFastTierPollMs, kFastTierSpacingMs);
                } else {
                    lastFireMs = nowMs;   // nothing safe to correct: back off one spacing
                }
                streak = 0;
            }
            Sleep(kFastTierPollMs);
        }
        if (fires)
            tlog("[twinspeak] fast tier    : first second done -- %d correction(s); returning to the\n"
                 "                            normal cadence (the baseline arms at seconds 5..9 ON this\n"
                 "                            corrected path)\n", fires);
        else
            tlog("[twinspeak] fast tier    : first second clean -- drift held within %.0f ms of the\n"
                 "                            delay model; nothing to correct, the normal warden cadence\n"
                 "                            resumes\n", g_cfg.driftCorrectMs);
    }

    // v3.8: volume/pin watcher -- 25 ms volume poll, 2 s output pin re-check.
    HANDLE hWatch = (g_cfg.volumeFollow || g_cfg.pinOutputs)
                  ? CreateThread(nullptr, 0, volumeWatchThread, nullptr, 0, nullptr)
                  : nullptr;
    // v6.5.2 power guard: the WM_POWERBROADCAST listener must exist before
    // the health loop starts -- a sleep landing right after start is still
    // caught. The thread only flips flags; every reopen stays on the
    // health-loop thread that owns the devices.
    HANDLE hPower = g_cfg.powerGuard
                  ? CreateThread(nullptr, 0, powerThread, nullptr, 0, nullptr)
                  : nullptr;
    (void)hPower;

    // v6.2 BACKGROUND: detach BEFORE the banner -- a failure above already
    // returned with the console intact; from here the run is good and the
    // banner/logs go to twinspeak.log next to the ini (everything funnels
    // through stdout/tlog, so one freopen is the whole mechanism).
    // Stop a background run: taskkill /IM twinspeak.exe
    if (!guiMode && g_cfg.background && !fgForce) {   // v6.6.0: never detach under the GUI
        size_t ds = iniPath.find_last_of("\\/");
        std::string logPath = (ds == std::string::npos)
                            ? std::string("twinspeak.log")
                            : iniPath.substr(0, ds + 1) + "twinspeak.log";
        tlog("[twinspeak] background    : on -- logging to %s; console detaches now.\n"
               "            stop with:  taskkill /IM twinspeak.exe   (--foreground keeps the console for one run)\n",
               logPath.c_str());
        fflush(stdout);
        FILE* lf = freopen(logPath.c_str(), "w", stdout);
        if (lf) {
            g_logToFile = true;                  // health line: \n not \r, every 10 s
            freopen(logPath.c_str(), "a", stderr);
            FreeConsole();
        } else {
            tlog("[twinspeak] background    : could not open the log file -- staying in the foreground\n");
        }
    } else if (!guiMode && g_cfg.background && fgForce) {
        tlog("[twinspeak] background    : on in the ini, overridden by --foreground -- keeping the console\n");
    }

    tlog("[twinspeak] v7.1.16       : BIT-EXACT ASIO-strict engine -- the audio path is memcpy; twinspeak\n");
    tlog("                            never resamples and never shifts pitch. Center isolation: out2 carries ONLY ch3.\n");
    tlog("[twinspeak] backend        : %s\n", ma_get_backend_name(ctx.backend));
    tlog("[twinspeak] capture        : \"%s\"  8ch @ %u Hz | %s | burst %u fr (~%.1f ms)\n",
           capName.c_str(), kSampleRate,
           capExclusive ? "EXCLUSIVE direct pin" : "shared",
           (unsigned)g_capBurstFrames, g_capBurstFrames * 1000.0 / kSampleRate);
    if (!capExclusive && g_cfg.captureExclusive)
        tlog( "[twinspeak] capture hint   : exclusive was refused -- the pin is held by the engine or the\n"
                        "            Line 1 port driver is WaveCyclic. The direct WaveRT capture path\n"
                        "            needs the WaveRT port driver selected in the VAC Control Panel.\n");
    tlog("[twinspeak] clock sync     : %s\n",
           g_cfg.clockSync ?
           "device-master ASIO rules -- shortfall replays the last buffer (ASIO underrun\n"
           "                            convention), excess above the sawtooth is dropped OLDEST-first,\n"
           "                            gated 1x/5 s, logged. There is no rate state: nothing can bend pitch." :
           "OFF (clock_sync = off) -- shortfall pads silence, excess evicts at the ring cap (A/B only)");
    tlog("[twinspeak] drift warden  : %s -- frame-interval ledger: every capture burst is stamped, every\n"
           "                            real frame that crosses a leg's play boundary advances that leg's stream\n"
           "                            position, and the health line's 'drift' column is the measured inter-speaker\n"
           "                            offset (no clock involved -- pure frame arithmetic; your delay1_ms/delay2_ms\n"
           "                            is inside it -- at delay1_ms = 60 it reads about -65, and that IS the\n"
           "                            alignment, not a fault). The baseline (your delay calibration included) is\n"
           "                            averaged ~5-9 s after boot; auto re-pin (resync) when\n"
           "                            |drift - baseline| >= %.0f ms for 3 s: the late leg's ring gives back its\n"
           "                            excess (bit-exact skip), the early leg replays in place. The health line's\n"
           "                            'err' column is the live distance from that baseline (~0.0 = pinned). The\n"
           "                            ledger cannot see buffering BELOW the USB packetizer (the S5's own receiver\n"
           "                            FIFO) -- a flat drift column with audible drift = device silicon, and this\n"
           "                            log proves it.\n",
           g_cfg.driftGuard ? "ON" : "observe-only (drift_guard = off)", g_cfg.driftCorrectMs);
    tlog("[twinspeak] power guard   : %s -- suspend parks the pumps, resume rebuilds capture + both\n"
           "                            legs, and any leg consuming beyond +-10%% for 2 s is reopened\n"
           "                            fresh. The v6.5.1 field log: an S5 came back from sleep at ~42%%\n"
           "                            consumption with a fresh heartbeat and glitched forever.\n",
           g_cfg.powerGuard ? "ON" : "OFF (power_guard = off -- v6.5.1 behavior)");
    tlog("[twinspeak] leg1 (mains)   : \"%s\" | %s | period %u ms x2\n",
           n1.c_str(),
           g_leg[0].backend == 1 ? "WDM-KS (kernel streaming)" :
           g_leg[0].isExclusive ? "EXCLUSIVE" : "shared", (unsigned)periodMs1);
    if (g_cfg.lfeFold)
        tlog("[twinspeak] route out1     : FL = %.2f*ch1 + %.2f*ch4(LFE) | FR = %.2f*ch2 + %.2f*ch4(LFE)\n"
               "[twinspeak] route out1     : FC + discrete LFE + SL + SR = digital silence (center never enters out1)\n",
               g_cfg.gainL, g_cfg.gainLfe, g_cfg.gainR, g_cfg.gainLfe);
    else
        tlog("[twinspeak] route out1     : FL = %.2f*ch1 | FR = %.2f*ch2 | discrete LFE = %.2f*ch4\n"
               "[twinspeak] route out1     : FC + SL + SR = digital silence (center never enters out1)\n",
               g_cfg.gainL, g_cfg.gainR, g_cfg.gainLfe);
    tlog("[twinspeak] leg2 (center)  : \"%s\" | %s | period %u ms x2\n",
           n2.c_str(),
           g_leg[1].backend == 1 ? "WDM-KS (kernel streaming)" :
           g_leg[1].isExclusive ? "EXCLUSIVE" : "shared", (unsigned)periodMs2);
    tlog("[twinspeak] route out2     : L = R = %.2f*ch3(center) ONLY -- ch1/ch2/ch4..ch8 EXPLICITLY MUTED in the out2 path\n"
           "[twinspeak] route out2     : (out2 is NOT a clone of the program audio) + %.0f Hz @ %.2f%% keep-alive\n",
           g_cfg.gainCenter, g_kaHz, g_kaGain * 100.0f);
    tlog("[twinspeak] route out2     : center gates %s -- ch3 digital-zero -> keep-alive only; ch3 == a*ch1+b*ch2\n"
           "                            (the Windows stereo->8ch upmix fingerprint, residual < -30 dB) -> muted.\n"
           "                            Genuine 5.1 center (an independent track) never matches and always plays.\n",
           (g_cfg.centerGate && g_cfg.centerUpmixGuard) ? "BOTH ON" :
           (g_cfg.centerUpmixGuard ? "upmix guard on, gate OFF" :
           (g_cfg.centerGate ? "gate on, upmix guard OFF" : "OFF (center_gate = off)")));
    if (g_ceq.on)
        tlog("[twinspeak] route out2     : center EQ ON -- peaking %.0f Hz %+.1f dB Q %.2f on ch3 (dialogue boost;\n"
               "                            a KS/WaveRT leg bypasses every system APO, so the boost lives here)\n",
               g_ceq.f0, g_ceq.gainDb, g_ceq.q);
    else
        tlog("[twinspeak] route out2     : center EQ off -- out2 is bit-exact\n");
    // v6.3.1: the align line does the arithmetic -- both est paths and their
    // difference, with the trim direction spelled out. Same formula as
    // printLegDevice (ring + device lead + capture burst, delay included),
    // so the numbers match the per-leg device lines below.
    auto estPathOf = [&](int i) -> double {
        const Leg& L = g_leg[i];
        const double burstMs = (double)g_capBurstFrames * 1000.0 / (double)kSampleRate;
        const double ringMs  = (double)L.targetFrames * 1000.0 / (double)kSampleRate;
        if (L.backend == 1)
            return ringMs + ((double)L.ks.leadFrames + 0.5 * (double)L.ks.periodFrames)
                            * 1000.0 / (double)kSampleRate + burstMs;
        const ma_device* d = &L.dev;
        const double devMs = d->playback.internalPeriodSizeInFrames
            ? (double)d->playback.internalPeriodSizeInFrames * (double)d->playback.internalPeriods
              * 1000.0 / (double)d->playback.internalSampleRate : 0.0;
        return ringMs + devMs + burstMs;
    };
    const double ep1 = estPathOf(0), ep2 = estPathOf(1);
    tlog("[twinspeak] align          : delay1 +%d ms, delay2 +%d ms -- bit-exact ring delay; put it on the EARLY leg\n"
           "                            est path: leg1 ~%.1f ms vs leg2 ~%.1f ms (difference %+.1f ms with the\n"
           "                            current delay included -> %s). The S5's own\n"
           "                            amp latency is OUTSIDE the print -- the ear trims the last 1-2 ms.\n"
           "                            v6.5.9: the health log's 'devbuf' line re-prints this from the LIVE\n"
           "                            devices every 10 s (padding / exclusive buffer / KS lead + ring).\n",
           (int)(g_cfg.delay1Ms + 0.5f), (int)(g_cfg.delay2Ms + 0.5f),
           ep1, ep2, ep1 - ep2,
           (ep1 - ep2) > 0.05 ? "leg1 arrives later -- raise delay2_ms" :
           (ep1 - ep2) < -0.05 ? "leg2 (center) arrives later -- raise delay1_ms" :
           "printed paths are EQUAL -- any offset left is device silicon");
    tlog("[twinspeak] route check    : center (ch3) plays ONLY on out2; out1 plays ONLY ch1/ch2 + the LFE fold\n");
    if (g_cfg.volumeFollow) {
        if (g_srcVol)
            tlog("[twinspeak] volume        : following \"%ls\" (the cable's playback side) -- now %.0f%%%s -- drives BOTH legs\n",
                   g_srcVolName.c_str(), g_vol.load() * 100.0f,
                   g_volMuted.load() ? ", MUTED" : "");
        else
            tlog( "[twinspeak] volume        : no render endpoint matching \"%s\" (or \"virtual audio cable\") -- volume follow is OFF\n"
                            "            fix: set volume_endpoint = <the cable's PLAYBACK-side name> in the ini\n"
                            "            (the full endpoint list with states was printed above this line)\n",
                    g_cfg.inputDevice.c_str());
    } else {
        tlog("[twinspeak] volume        : follow off (volume_follow = off)\n");
    }
    if (g_cfg.pinOutputs)
        tlog("[twinspeak] output pins   : out1/out2 endpoints forced 100%%/unmuted -- re-checked every 2 s\n");
    tlog("[twinspeak] start order   : %s\n",
           g_cfg.out2First ? "out2 (center) FIRST -- its pump is created first and runs at the higher priority; out1 follows"
                           : "out1 first, then out2 (out2_first = off)");
    tlog("[twinspeak] dac yield     : %s -- the leg whose device IS the Windows default render endpoint\n"
           "                            is released the same second (KS pin closed / WASAPI device freed,\n"
           "                            pump parked) so the main device plays untouched; it reopens the\n"
           "                            moment the default moves elsewhere (yield_default)\n",
           g_cfg.yieldDefault ? "ON" : "off");
    if (g_cfg.chimes) {
        float cgp = g_cfg.chimeGain; if (cgp < 0.0f) cgp = 0.0f; if (cgp > 1.0f) cgp = 1.0f;
        tlog("[twinspeak] chimes        : ON -- CenterFold signature, modernized: low-treble TRIANGLE blips\n"
               "                            (rising C7->G7) on out2 at start + after a leg2 reopen | gain %.0f%%\n",
               cgp * 100.0f);
    } else {
        tlog("[twinspeak] chimes        : off (chimes = off)\n");
    }
    printLegDevice(0);
    printLegDevice(1);
    // v6.0 latency hint: the user's field report put the old 10 ms-default
    // path at ~50 ms. If the ini still asks for >=10 ms periods, point at
    // the 5 ms preset (the template default now).
    {
        auto legPathMs = [&](int i) {
            const ma_device* d = &g_leg[i].dev;
            const double pms = d->playback.internalSampleRate && d->playback.internalPeriodSizeInFrames
                             ? d->playback.internalPeriodSizeInFrames * 1000.0
                               / d->playback.internalSampleRate : 0.0;
            return (double)g_leg[i].targetFrames * 1000.0 / (double)kSampleRate
                 + pms * (double)d->playback.internalPeriods
                 + (double)g_capBurstFrames * 1000.0 / (double)kSampleRate;
        };
        // v6.3: the hint speaks to WASAPI legs only -- a KS leg's real path
        // is printed on its own device line above.
        const bool hint1 = periodMs1 >= 10 && g_leg[0].backend == 0;
        const bool hint2 = periodMs2 >= 10 && g_leg[1].backend == 0;
        if (hint1 || hint2)
            tlog("[twinspeak] latency hint   : out periods are %u/%u ms (est path ~%.0f/~%.0f ms). v6.2 defaults are\n"
                   "            5/5 ms (est path ~34 ms): set out1ms = 5 and out2ms = 5 in the ini if the\n"
                   "            drivers accept it -- the banner prints the real negotiated periods.\n"
                   "            (v6.3: a KS leg ignores outXms -- set backendN = ks + ks_period_frames.)\n",
                   (unsigned)periodMs1, (unsigned)periodMs2, legPathMs(0), legPathMs(1));
    }
    tlog("[twinspeak] threads        : 1 capture + 2 pumps (movers, one per leg) + device threads\n");
    {
        const int ksLegs = (g_leg[0].backend == 1) + (g_leg[1].backend == 1);
        if (ksLegs == 2)
            tlog("[twinspeak] transport      : BOTH legs on WDM-KS -- the WASAPI engine is fully out of the output path\n");
        else if (ksLegs == 1)
            tlog("[twinspeak] transport      : leg%d on WDM-KS, the other leg on WASAPI\n",
                   g_leg[0].backend == 1 ? 1 : 2);
    }

    if (g_logToFile)
        tlog("[twinspeak] running in BACKGROUND -- health line every 10 s (live counters every second):\n\n");
    else
        tlog("[twinspeak] running -- Ctrl+C to stop. health line every second:\n\n");

    unsigned long long d00 = 0, d10 = 0, u00 = 0, u10 = 0;
    unsigned long long c00 = 0, c10 = 0;                 // cumulative corr counters
    unsigned long long a00 = 0, a10 = 0, p00 = 0, p10 = 0; // off-meter counters
    unsigned long long m00 = 0;                          // cumulative upmix-guard blocks
    unsigned long long q00 = 0, q10 = 0;                 // cumulative warden-pad counters (v6.4)
    long off0 = 0, off1 = 0;
    int colStreak0 = 0, colStreak1 = 0;                  // v6.5.2 collapse sentinel
    unsigned long long colLastMs[2] = { 0, 0 };          // per-leg reopen cooldown
    bool ccHintDone = false;
    // v6.4 drift warden state (health-loop owned)
    double driftBase = 0.0;                              // calibrated inter-speaker offset
    bool   driftBaseOK = false;
    double driftSum = 0.0;  int driftN = 0;              // calibration window
    bool   driftRecal = false;                           // v6.5.8: this calibration is a re-arm
    int    wardenStreak = 0;
    unsigned long long wardenLastMs = 0;
    // v6.2: the clock hint fires only on a SETTLED path -- >=10 s up, a
    // quiet window (no corrections/replays), same-sign exceedance held
    // across THREE windows at more than kHintPpm. The meter is quantized by
    // the ~2 ms device switch (1000 ppm sat below the noise floor) and the
    // startup transient used to print a false alarm.
    static constexpr long kHintPpm = 5000;   // above the one-period meter quantum
    unsigned sec = 0;
    int hintStreak = 0;
    long prevDom = 0;
    unsigned stallSec[2] = { 0, 0 };   // v6.5.11: consecutive starved seconds per leg
    unsigned long long deepLastMs = 0; // v6.5.12: scheduled deep-resync clock
    while (g_run.load()) {
        // v7.1.11: poll g_run every 50 ms inside the 1 s health tick -- the
        // old bare Sleep(1000) delayed the whole shutdown sequence by up to
        // a second (the silence the field never heard on APPLY+RESTART).
        for (int w = 0; w < 20 && g_run.load(); ++w) Sleep(50);
        if (!g_run.load()) break;
        ++sec;
        // v6.0 measured clock offset per leg: dCaptured/dPopped - 1 over the
        // last second (both counters integrate the real clocks; bursts and
        // pop phase cancel in the deltas). Positive = cable runs fast.
        {
            const unsigned long long A0 = g_leg[0].captured.load(std::memory_order_relaxed);
            const unsigned long long C0 = g_leg[0].popped.load(std::memory_order_relaxed);
            if (p00 != 0 && C0 > p00 && A0 > a00)
                off0 = (long)(((double)(A0 - a00) / (double)(C0 - p00) - 1.0) * 1000000.0);
            a00 = A0; p00 = C0;
            const unsigned long long A1 = g_leg[1].captured.load(std::memory_order_relaxed);
            const unsigned long long C1 = g_leg[1].popped.load(std::memory_order_relaxed);
            if (p10 != 0 && C1 > p10 && A1 > a10)
                off1 = (long)(((double)(A1 - a10) / (double)(C1 - p10) - 1.0) * 1000000.0);
            a10 = A1; p10 = C1;
        }
        // v6.0 fields: held = ring + raw backlog (the latency anchor is the
        // ring target); off = measured clock offset ppm; und = replay-padded
        // switches; drop = passive overwrite; corr = gated drops. v6.2:
        // computed BEFORE the hint so it can require a quiet window.
        unsigned long long d0 = g_leg[0].rawDrop.load() + g_leg[0].outDrop.load(),
                           d1 = g_leg[1].rawDrop.load() + g_leg[1].outDrop.load(),
                           u0 = g_leg[0].und.load(),        u1 = g_leg[1].und.load(),
                           k0 = g_leg[0].corrDrops.load(),  k1 = g_leg[1].corrDrops.load(),
                           m1 = g_leg[1].cmUpmix.load(),    // v6.1 upmix-guarded blocks
                           q0 = g_leg[0].padTotal.load(),   q1 = g_leg[1].padTotal.load(); // v6.4
        // v6.4 DRIFT LEDGER: the two legs' play boundaries in capture-sequence
        // space; their difference IS the inter-speaker offset (1 ms = 48 fr).
        // Replays/pads correctly do NOT advance it; pops/drops/skips DO.
        const long long st0 = (long long)g_leg[0].out.seqTail.load(std::memory_order_relaxed);
        const long long st1 = (long long)g_leg[1].out.seqTail.load(std::memory_order_relaxed);
        const bool driftOK = (st0 > 0 && st1 > 0);
        const double driftMs = driftOK ? (double)(st0 - st1) / 48.0 : 0.0;
        char driftStr[16];
        if (driftOK) snprintf(driftStr, sizeof(driftStr), "%+5.1f", driftMs);
        else         snprintf(driftStr, sizeof(driftStr), "%6s", "----");
        // v6.0 one-shot field hint, v6.2 SETTLED-PATH GATING: a steady offset
        // far beyond crystal level means the cable's virtual clock is
        // free-running (VAC Clock Correction off). v6.0 measures and reports
        // it -- never hides it behind a resampler -- but only once settled.
        if (!ccHintDone && sec >= 10) {
            const long a0 = off0 < 0 ? -off0 : off0;
            const long a1 = off1 < 0 ? -off1 : off1;
            const long dom = (a0 >= a1) ? off0 : off1;      // dominant leg
            const long ad  = dom < 0 ? -dom : dom;
            const bool quiet = (k0 - c00) == 0 && (k1 - c10) == 0
                            && (u0 - u00) == 0 && (u1 - u10) == 0
                            && (q0 - q00) == 0 && (q1 - q10) == 0;
            const bool over = ad > kHintPpm;
            const bool sameSign = (prevDom > 0 && dom > 0) || (prevDom < 0 && dom < 0);
            if (quiet && over && sameSign)
                ++hintStreak;
            else
                hintStreak = (quiet && over) ? 1 : 0;
            prevDom = dom;
            if (hintStreak >= 3) {
                ccHintDone = true;
                tlog("\n[twinspeak] hint          : steady clock offset leg1 %+ld ppm, leg2 %+ld ppm (cable vs device crystals).\n"
                     "            v6.2 absorbs this with rare gated corrections (bit-exact path). For a silent\n"
                     "            path forever, enable Clock Correction for this cable in the VAC Control Panel\n"
                     "            (Cable properties) and trim it until 'off' reads ~0 on the health line.\n\n",
                     off0, off1);
            }
        }
        // v3.8 WATCHDOG: a device thread whose callback has not ticked for
        // 5 s is dead (endpoint invalidated, USB re-enumeration, driver
        // reset). Reopen it by name; retry every tick until it returns.
        // Out2 is checked first (start-order symmetry).
        const unsigned long long nowMs = GetTickCount64();
        // v6.5.2: while parked (suspend in flight) the heartbeats go stale
        // by definition -- do NOT storm reopen attempts at a device the OS
        // is putting to sleep; the resume path rebuilds everything.
        if (nowMs - g_bootMs > 3000 && !g_powerPark.load(std::memory_order_relaxed)) {
            if (nowMs - hbOutGet(1) > 5000 && !g_leg[1].yielded.load(std::memory_order_relaxed))
                tryRestartLeg(1, &ctx, periodMs2);
            if (nowMs - hbOutGet(0) > 5000 && !g_leg[0].yielded.load(std::memory_order_relaxed))
                tryRestartLeg(0, &ctx, periodMs1);
            if (nowMs - g_hbCap.load(std::memory_order_relaxed) > 5000) tryRestartCap(&ctx);
        }
        // ---------------- v6.5.14 DEFAULT-DEVICE DAC YIELD ----------------
        // Field directive: "if the main audio device is either output_device1
        // or output_device2 that the DAC shuts off immediately to let the
        // main device play." The default endpoint is re-checked every tick;
        // a needle match releases THAT leg's DAC the same second (KS: pin
        // closed + pacer stopped; WASAPI: device freed) so the default-
        // playing app owns the hardware. When the default moves away the leg
        // reopens via the watchdog path. While yielded: pump parked (raw ring
        // overflows harmlessly, like the suspend park), watchdog/sentinel/
        // resyncs/warden all hold off, devbuf prints '----'. Reaction: one
        // health tick (<= 1 s).
        if (g_cfg.yieldDefault && !g_powerPark.load(std::memory_order_relaxed)) {
            std::string defName;
            if (defaultRenderName(&defName)) {
                for (int i = 0; i < 2; ++i) {
                    const char* yneedle = (i == 0) ? g_cfg.outputDevice1.c_str()
                                                   : g_cfg.outputDevice2.c_str();
                    if (!yneedle[0]) continue;
                    Leg& Y = g_leg[i];
                    const bool mine = containsCI(defName, yneedle);
                    if (mine && !Y.yielded.load(std::memory_order_relaxed)) {
                        Y.yielded.store(true, std::memory_order_relaxed);
                        stallSec[i] = 0;
                        tlog("[twinspeak] leg%d YIELD     : the Windows default render device (\"%s\") matches\n"
                             "                            this leg's needle (\"%s\") -- releasing the DAC now so the\n"
                             "                            main device plays; it reopens the moment the default\n"
                             "                            device moves elsewhere (yield_default = on);\n"
                             "                            endpoint-volume pin released -- device volume stays yours\n",
                             i + 1, defName.c_str(), yneedle);
                        if (Y.backend == 1) ksCloseLeg(i);   // stops the pacer + pin
                        if (Y.dev.pContext) {
                            ma_device_uninit(&Y.dev);
                            memset(&Y.dev, 0, sizeof(Y.dev));
                        }
                        // v6.5.15: drop the endpoint-volume pin WITH the DAC --
                        // while this device is the system's main output its
                        // volume belongs to the user; the reopen re-pins.
                        if (g_pin[i].vol) { g_pin[i].vol->Release(); g_pin[i].vol = nullptr; }
                        g_pin[i].id.clear();
                        Y.lastReopenMs = nowMs;   // warden settle gates stay coherent
                    } else if (!mine && Y.yielded.load(std::memory_order_relaxed)) {
                        // v6.5.16: the default moved away -- but the app that
                        // played here while it WAS the default does not
                        // re-route instantly; its stream stays attached.
                        // Re-grabbing under a live stream doubled the audio
                        // and wedged miniaudio apps. Wait for a QUIET device;
                        // a failed probe fails OPEN (logged).
                        const int busy = foreignActiveSessions(yneedle);
                        if (busy > 0) {
                            static unsigned long long lastBusyLog[2] = { 0, 0 };
                            if (nowMs - lastBusyLog[i] >= 5000) {
                                tlog("[twinspeak] leg%d YIELD     : the default moved away, but %d foreign audio\n"
                                     "                            session(s) are still ACTIVE on this device --\n"
                                     "                            holding the release until they finish (re-grabbing\n"
                                     "                            under them is the doubled-audio/deadlock bug)\n",
                                     i + 1, busy);
                                lastBusyLog[i] = nowMs;
                            }
                        } else {
                            if (busy < 0) {
                                static unsigned long long lastProbeLog[2] = { 0, 0 };
                                if (nowMs - lastProbeLog[i] >= 5000) {
                                    tlog("[twinspeak] leg%d YIELD     : session probe unavailable -- releasing fail-open\n", i + 1);
                                    lastProbeLog[i] = nowMs;
                                }
                            }
                            if (Y.yielded.exchange(false, std::memory_order_relaxed)) {
                                tlog("[twinspeak] leg%d YIELD     : the default device no longer matches this leg\n"
                                     "                            -- reopening (\"%s\")\n",
                                     i + 1, Y.name.c_str());
                                Y.lastReopenMs = nowMs;
                                tryRestartLeg(i, &ctx, i == 0 ? periodMs1 : periodMs2,
                                              "yield release (default device moved away)");
                            }
                        }
                    }
                }
            }
        }
        // ---- v6.5.2 power guard: resume rebuild + collapse sentinel ----
        if (g_cfg.powerGuard && g_powerWoken.exchange(false)) {
            g_powerPark.store(false, std::memory_order_relaxed);
            tlog("[twinspeak] power guard : system resume -- rebuilding capture + both legs\n"
                 "                            (a post-sleep stream is never trusted: the field log\n"
                 "                            shows an S5 consuming at ~42%% with a fresh heartbeat)\n");
            tryRestartCap(&ctx);
            // v6.5.14: a yielded leg STAYS released (its device is ours by
            // choice, not broken); the yield check above re-evaluates the
            // default every tick and reopens it when the default moves.
            if (!g_leg[1].yielded.load(std::memory_order_relaxed))
                tryRestartLeg(1, &ctx, periodMs2);   // center first -- out2_first symmetry
            if (!g_leg[0].yielded.load(std::memory_order_relaxed))
                tryRestartLeg(0, &ctx, periodMs1);
            wardenStreak = 0; hintStreak = 0; prevDom = 0;  // stale pre-sleep meters
            colStreak0 = 0; colStreak1 = 0;
        }
        // CONSUMPTION-COLLAPSE SENTINEL: |off| beyond +-10% for 2 consecutive
        // seconds is impossible for a healthy 48 kHz link. A half-alive
        // post-sleep stream ticks at a bogus rate with a FRESH heartbeat --
        // only this meter sees it (also catches degraded sleep ENTRY and
        // any driver reset leaving a bogus-rate thread running).
        if (g_cfg.powerGuard && !g_powerPark.load(std::memory_order_relaxed)
            && nowMs - g_bootMs > 30000) {
            // v6.5.14: a yielded leg's off meter reads garbage by design
            // (captured grows, popped is frozen) -- it must not fire this.
            colStreak0 = (!g_leg[0].yielded.load(std::memory_order_relaxed)
                          && (off0 > kCollapsePpm || off0 < -kCollapsePpm)) ? colStreak0 + 1 : 0;
            colStreak1 = (!g_leg[1].yielded.load(std::memory_order_relaxed)
                          && (off1 > kCollapsePpm || off1 < -kCollapsePpm)) ? colStreak1 + 1 : 0;
            if (colStreak0 >= 2 || colStreak1 >= 2) {
                const int li = (colStreak0 >= 2) ? 0 : 1;
                const long bad = (li == 0) ? off0 : off1;
                Leg& SL = g_leg[li];
                if (nowMs - SL.lastReopenMs > 5000 && nowMs - colLastMs[li] >= 10000) {
                    colLastMs[li] = nowMs;
                    colStreak0 = 0; colStreak1 = 0;
                    if (bad > 0) {
                        // positive = the DEVICE under-consumed (captured more
                        // than it popped) -- the field log's half-alive S5.
                        tlog("[twinspeak] power guard : leg%d consuming at %+ld ppm -- stream broken\n"
                             "                            (sleep resume, driver reset?) -- reopening fresh\n",
                             li + 1, bad);
                        tryRestartLeg(li, &ctx, li == 0 ? periodMs1 : periodMs2);
                    } else {
                        // negative = the leg asked for frames the CAPTURE side
                        // never delivered -- rebuilding the leg cannot fix a
                        // starved ring; the capture pin gets rebuilt instead.
                        tlog("[twinspeak] power guard : leg%d starved (%+ld ppm) -- the capture side\n"
                             "                            stopped feeding it -- rebuilding the capture pin\n",
                             li + 1, bad);
                        tryRestartCap(&ctx);
                    }
                }
            }
        }
        // ---------------- v6.5.11 STALL RESYNC ----------------
        // A starved device thread (interrupt storm, CPU hog) trips the
        // shortfall replay (the synthetic buzz) and slips BEHIND while the
        // pump kept feeding its ring. Recovery:
        //   FAST: und this second + backlog above the anchor -> discard the
        //     excess NOW (bit-exact skip, no streak wait).
        //   DEEP: 3 s of starvation -> reopen the leg fresh (resets the
        //     device-side FIFO state the storm disturbed).
        // Gated on the baseline being armed + the suspend park; capped at
        // 100 ms per discard; reopen respects the 10 s per-leg spacing.
        if (driftBaseOK && !g_powerPark.load(std::memory_order_relaxed)) {
            for (int i = 0; i < 2; ++i) {
                Leg& SLeg = g_leg[i];
                if (SLeg.yielded.load(std::memory_order_relaxed)) {   // v6.5.14: released by choice
                    stallSec[i] = 0;
                    continue;
                }
                const unsigned long long undPrev = (i == 0) ? u00 : u10;
                const unsigned long long undNow =
                    SLeg.und.load(std::memory_order_relaxed);
                const bool starved = undNow > undPrev;
                stallSec[i] = starved ? stallSec[i] + 1 : 0;
                if (!starved) continue;
                // FAST PATH: immediate backlog resync (>= 1 ms of excess)
                const long long dpt = (long long)SLeg.out.count()
                                    - (long long)SLeg.targetFrames;
                if (dpt >= 48) {
                    size_t x = (dpt > (long long)kWardenMaxPadFrames)
                             ? kWardenMaxPadFrames : (size_t)dpt;
                    SLeg.out.discardOldest(x);
                    tlog("[twinspeak] stall resync : leg%d starved (und +%llu this second) -- discarded\n"
                         "                            %llu fr (%.1f ms) of stale backlog above the anchor;\n"
                         "                            the leg resumes at the freshest data (no warden wait)\n",
                         i + 1, (unsigned long long)(undNow - undPrev),
                         (unsigned long long)x, (double)x * 1000.0 / (double)kSampleRate);
                }
                // DEEP PATH: sustained starvation -> fresh reopen
                if (stallSec[i] >= 3 && nowMs - SLeg.lastReopenMs > 10000) {
                    tlog("[twinspeak] stall resync : leg%d starved %u s running -- reopening fresh\n"
                         "                            (also resets the device-side FIFO the storm disturbed)\n",
                         i + 1, stallSec[i]);
                    stallSec[i] = 0;
                    tryRestartLeg(i, &ctx, i == 0 ? periodMs1 : periodMs2);
                }
            }
        }
        // ---------------- v6.5.12 DEEP RESYNC (optional, deep_resync_min) --
        // The S5's below-packetizer layer (endpoint FIFO + amp DSP) can
        // deepen silently over hours: the ledger stays pinned while the
        // center audibly lags again -- no host meter sees that layer. The
        // only reset that re-primes it is a scheduled fresh reopen (the
        // stall resync's deep path, on a schedule instead of evidence).
        // Gated on the baseline + suspend park; the reopen invalidates the
        // baseline, so the warden re-arms ~9 s later, delay intact.
        if (g_cfg.deepResyncMin && driftBaseOK
            && !g_powerPark.load(std::memory_order_relaxed)
            && !g_leg[1].yielded.load(std::memory_order_relaxed)) {
            if (deepLastMs == 0)
                deepLastMs = nowMs;
            else if (nowMs - deepLastMs >= (unsigned long long)g_cfg.deepResyncMin * 60000ULL) {
                deepLastMs = nowMs;
                if (nowMs - g_leg[1].lastReopenMs > 10000
                    && nowMs - g_leg[0].lastReopenMs > 10000) {
                    tlog("[twinspeak] deep resync  : scheduled center-leg reopen (%u min) -- re-priming the\n"
                         "                            device's below-packetizer FIFO; a brief center gap is\n"
                         "                            expected, the warden re-arms ~9 s later\n",
                         g_cfg.deepResyncMin);
                    tryRestartLeg(1, &ctx, periodMs2);
                }
            }
        }
        // ---------------- v6.4 DRIFT WARDEN ----------------
        // Baseline: the settled path's average drift (seconds 5..9, heartbeats
        // fresh, no reopen within 5 s -- includes the user's own delay
        // calibration). Correction: |drift - baseline| >= drift_correct_ms
        // held 3 windows, at most once per drift_interval_ms: the late leg's
        // ring discards its excess (bit-exact skip), the early leg replays
        // in place. Never fires on a dead/reopening leg.
        if (driftOK) {
            const bool settled = (nowMs - g_leg[0].lastReopenMs > 5000)
                              && (nowMs - g_leg[1].lastReopenMs > 5000)
                              && (nowMs - hbOutGet(0) < 5000)
                              && (nowMs - hbOutGet(1) < 5000);
            // v6.5.8: a reopen can legitimately move the path's real latency
            // -- the baseline is invalidated and re-armed once settled again
            // (~9 s: 5 s settle + a 4-sample average).
            if (g_driftRecalReq.exchange(false) && driftBaseOK) {
                driftBaseOK = false;
                driftSum = 0.0;  driftN = 0;
                driftRecal = true;
                tlog("\n[twinspeak] drift warden : device reopen -- baseline invalidated, re-arming once the path settles\n\n");
            }
            if (!driftBaseOK) {
                if (sec >= 5 && settled) {
                    driftSum += driftMs;
                    ++driftN;
                    if (sec >= 9 && driftN >= 4) {
                        driftBase = driftSum / (double)driftN;
                        driftBaseOK = true;
                        if (driftRecal)
                            tlog("\n[twinspeak] drift warden : baseline RE-ARMED at %+.1f ms after reopen (the boot baseline\n"
                                 "                            was stale against the reopened path). Corrections fire when the drift\n"
                                 "                            leaves it by >= %.0f ms for %u s; 'err' is that distance live. If the\n"
                                 "                            ears still hear drift on a FLAT drift column, the offset lives BELOW\n"
                                 "                            the USB packetizer (device silicon) -- no host trigger exists.\n\n",
                                 driftBase, g_cfg.driftCorrectMs, kWardenStreakNeeded);
                        else
                            tlog("\n[twinspeak] drift warden : baseline calibrated at %+.1f ms (inter-speaker offset,\n"
                                 "                            your delay calibration included). Corrections fire when the drift\n"
                                 "                            leaves it by >= %.0f ms for %u s; the health line's 'err' column is\n"
                                 "                            that distance live. A flat drift column with audible drift = the\n"
                                 "                            offset lives BELOW the USB packetizer (device silicon).\n\n",
                                 driftBase, g_cfg.driftCorrectMs, kWardenStreakNeeded);
                        driftRecal = false;
                    }
                }
            } else if (g_cfg.driftGuard && settled
                       && nowMs - wardenLastMs >= g_cfg.driftIntervalMs) {
                const double err = driftMs - driftBase;
                if (err >= g_cfg.driftCorrectMs || err <= -g_cfg.driftCorrectMs) {
                    ++wardenStreak;
                    if (wardenStreak >= (int)kWardenStreakNeeded) {
                        wardenStreak = 0;
                        wardenLastMs = nowMs;
                        const long long errFr = (long long)(err * 48.0);
                        const long long dpt0 = (long long)g_leg[0].out.count() - (long long)g_leg[0].targetFrames;
                        const long long dpt1 = (long long)g_leg[1].out.count() - (long long)g_leg[1].targetFrames;
                        bool did = false;
                        if (err > 0 && dpt1 > 0) {          // leg2 late: give the excess back
                            Leg& W = g_leg[1];
                            size_t x = (size_t)(errFr < dpt1 ? errFr : dpt1);
                            const size_t lvl = W.out.count();
                            if (x > lvl) x = lvl;
                            if (x) {
                                W.out.discardOldest(x);     // seqTail advances: a true skip
                                W.wardenDrops.fetch_add(1, std::memory_order_relaxed);
                                tlog("[twinspeak] drift warden : leg2 re-pinned -- skipped %llu fr (%.1f ms) of excess;\n"
                                     "                            drift %+.1f ms -> back toward baseline %+.1f ms\n",
                                     (unsigned long long)x, (double)x * 1000.0 / (double)kSampleRate,
                                     driftMs, driftBase);
                                did = true;
                            }
                        } else if (err > 0 && dpt0 < 0) {   // leg1 early: hold it (in-place replay)
                            Leg& W = g_leg[0];
                            size_t x = (size_t)(-errFr < -dpt0 ? (size_t)(-errFr) : (size_t)(-dpt0));
                            if (x > kWardenMaxPadFrames) x = kWardenMaxPadFrames;
                            if (x) {
                                W.padFrames.fetch_add(x, std::memory_order_relaxed);
                                W.wardenPads.fetch_add(1, std::memory_order_relaxed);
                                tlog("[twinspeak] drift warden : leg1 re-pinned -- replay-padded %llu fr (%.1f ms);\n"
                                     "                            drift %+.1f ms -> back toward baseline %+.1f ms\n",
                                     (unsigned long long)x, (double)x * 1000.0 / (double)kSampleRate,
                                     driftMs, driftBase);
                                did = true;
                            }
                        } else if (err < 0 && dpt0 > 0) {   // leg1 late: give the excess back
                            Leg& W = g_leg[0];
                            size_t x = (size_t)(-errFr < dpt0 ? (size_t)(-errFr) : (size_t)dpt0);
                            const size_t lvl = W.out.count();
                            if (x > lvl) x = lvl;
                            if (x) {
                                W.out.discardOldest(x);
                                W.wardenDrops.fetch_add(1, std::memory_order_relaxed);
                                tlog("[twinspeak] drift warden : leg1 re-pinned -- skipped %llu fr (%.1f ms) of excess;\n"
                                     "                            drift %+.1f ms -> back toward baseline %+.1f ms\n",
                                     (unsigned long long)x, (double)x * 1000.0 / (double)kSampleRate,
                                     driftMs, driftBase);
                                did = true;
                            }
                        } else if (err < 0 && dpt1 < 0) {   // leg2 early: hold it
                            Leg& W = g_leg[1];
                            size_t x = (size_t)(-errFr < -dpt1 ? (size_t)(-errFr) : (size_t)(-dpt1));
                            if (x > kWardenMaxPadFrames) x = kWardenMaxPadFrames;
                            if (x) {
                                W.padFrames.fetch_add(x, std::memory_order_relaxed);
                                W.wardenPads.fetch_add(1, std::memory_order_relaxed);
                                tlog("[twinspeak] drift warden : leg2 re-pinned -- replay-padded %llu fr (%.1f ms);\n"
                                     "                            drift %+.1f ms -> back toward baseline %+.1f ms\n",
                                     (unsigned long long)x, (double)x * 1000.0 / (double)kSampleRate,
                                     driftMs, driftBase);
                                did = true;
                            }
                        }
                        if (!did)
                            tlog("[twinspeak] drift warden : drift %+.1f ms vs baseline %+.1f ms, but neither ring holds\n"
                                 "                            the excess (depth d0 %+lld / d1 %+lld fr) -- nothing host-side to re-pin\n",
                                 driftMs, driftBase, dpt0, dpt1);
                    }
                } else {
                    wardenStreak = 0;
                }
            } else if (!settled) {
                wardenStreak = 0;
            }
        }
        // v6.5.1: err -- the warden's own number: the live distance from the
        // calibrated baseline (drift is the RAW offset, the delay included;
        // err is what the correction rule acts on). Computed AFTER the
        // warden step so the calibration lands in the same second.
        char errStr[16];
        if (driftOK && driftBaseOK)
            snprintf(errStr, sizeof(errStr), "%+5.1f", driftMs - driftBase);
        else
            snprintf(errStr, sizeof(errStr), "%6s", "----");
        char volStr[16];
        if (g_volFollowLive.load(std::memory_order_relaxed))
            snprintf(volStr, sizeof(volStr), "%3.0f%%%s",
                     g_vol.load(std::memory_order_relaxed) * 100.0f,
                     g_volMuted.load(std::memory_order_relaxed) ? " M" : "  ");
        else
            snprintf(volStr, sizeof(volStr), "%6s", "---");
        // ---- v6.5.9 MEASURED device buffering per leg (the 'devbuf' line) ----
        // The live reading, from the authoritative source per transport:
        // WASAPI shared = GetCurrentPadding; exclusive = the whole device
        // buffer (WaveRT drivers misreport padding as 0 and miniaudio pops
        // the FULL buffer per event); WDM-KS = the pacer's write-minus-play
        // lead off the DAC position register. Telemetry only: a failed call
        // prints ----.
        double      padMs[2]  = { 0.0, 0.0 };
        const char* padHow[2] = { "----", "----" };
        for (int i = 0; i < 2; ++i) {
            Leg& L = g_leg[i];
            if (L.backend == 1) {
                if (L.ks.active) {
                    padMs[i]  = (double)L.ks.aheadFr.load(std::memory_order_relaxed)
                              * 1000.0 / (double)kSampleRate;
                    padHow[i] = "ks lead ";
                }
            } else if (L.dev.pContext) {
                const unsigned bf = L.dev.wasapi.actualBufferSizeInFramesPlayback;
                if (L.isExclusive) {
                    if (bf) {
                        padMs[i]  = (double)bf * 1000.0 / (double)kSampleRate;
                        padHow[i] = "excl buf";
                    }
                } else {
                    ma_IAudioClient* ac =
                        (ma_IAudioClient*)(void*)L.dev.wasapi.pAudioClientPlayback;
                    UINT32 pad = 0;
                    if (ac && SUCCEEDED(ma_IAudioClient_GetCurrentPadding(ac, &pad))
                        && pad > 0 && (bf == 0 || pad <= bf * 4)) {  // misreport guard
                        padMs[i]  = (double)pad * 1000.0 / (double)kSampleRate;
                        padHow[i] = "padding ";
                    }
                }
            }
        }
        if ((sec % 10) == 0) {   // same cadence as the log-file health line
            const double burstMs = (double)g_capBurstFrames * 1000.0 / (double)kSampleRate;
            const double path1 = padMs[0] + (double)g_leg[0].out.count() * 1000.0 / kSampleRate + burstMs;
            const double path2 = padMs[1] + (double)g_leg[1].out.count() * 1000.0 / kSampleRate + burstMs;
            tlog("\n[twinspeak] devbuf      : leg1 %5.1f ms (%s) + ring %5.1f + burst %.1f = %5.1f ms\n"
                 "                            leg2 %5.1f ms (%s) + ring %5.1f + burst %.1f = %5.1f ms\n"
                 "                            measured path diff %+.1f ms -- BELOW-packetizer silicon (the\n"
                 "                            S5's own FIFO) is still outside these numbers\n",
                 padMs[0], padHow[0], g_leg[0].out.count() * 1000.0 / kSampleRate, burstMs, path1,
                 padMs[1], padHow[1], g_leg[1].out.count() * 1000.0 / kSampleRate, burstMs, path2,
                 path1 - path2);
            const unsigned long long laps0 = g_leg[0].ks.lapses.load(std::memory_order_relaxed);
            const unsigned long long laps1 = g_leg[1].ks.lapses.load(std::memory_order_relaxed);
            if (laps0 || laps1)
                tlog("                            KS lapse guard: %llu / %llu write-cursor re-pin(s) since boot\n",
                     laps0, laps1);
        }
        if (!g_logToFile || (sec % 10) == 0)      // v6.2: the log gets a line every 10 s
        tlog(g_logToFile
             ? "out1 %5.1f | out2 %5.1f | vol %s | held %5.1f/%5.1f | off %+6ld/%+6ld | und +%llu/%llu | drop +%llu/%llu | corr %llu/%llu | cm +%llu | drift %s | err %s\n"
             : "out1 %5.1f | out2 %5.1f | vol %s | held %5.1f/%5.1f | off %+6ld/%+6ld | und +%llu/%llu | drop +%llu/%llu | corr %llu/%llu | cm +%llu | drift %s | err %s   \r",
               g_leg[0].out.count() * 1000.0f / kSampleRate,
               g_leg[1].out.count() * 1000.0f / kSampleRate,
               volStr,   // v3.9 NOTE: this argument WAS missing in v3.8 (the crash);
                         // every conversion here is argument-checked since (16/16 -- v6.5.1 added err).
               (g_leg[0].out.count() + g_leg[0].raw.count()) * 1000.0 / kSampleRate,
               (g_leg[1].out.count() + g_leg[1].raw.count()) * 1000.0 / kSampleRate,
               off0, off1,
               u0 - u00, u1 - u10, d0 - d00, d1 - d10,
               k0 - c00, k1 - c10,
               m1 - m00,
               driftStr, errStr);
        fflush(stdout);
        d00 = d0; d10 = d1; u00 = u0; u10 = u1; c00 = k0; c10 = k1; m00 = m1;
        q00 = q0; q10 = q1;
    }

    tlog("\n[twinspeak] stopping...\n");
    g_stopSilence.store(true, std::memory_order_relaxed);   // v7.0.0: the stop gap is SILENCE
    g_run.store(false);
    WaitForSingleObject(hPump1, 2000);
    WaitForSingleObject(hPump2, 2000);
    CloseHandle(hPump1);
    CloseHandle(hPump2);
    Sleep(150); // let the device threads drain the rings' tails
    if (hWatch) { WaitForSingleObject(hWatch, 2000); CloseHandle(hWatch); }
    if (g_capDev.pContext)     ma_device_uninit(&g_capDev);
    for (int i = 0; i < 2; ++i) {
        if (g_leg[i].backend == 1) ksCloseLeg(i);          // v6.3: stop thread + close pin
        if (g_leg[i].dev.pContext) ma_device_uninit(&g_leg[i].dev);
    }
    if (g_srcVol) { g_srcVol->Release(); g_srcVol = nullptr; }
    for (int i = 0; i < 2; ++i)
        if (g_pin[i].vol) { g_pin[i].vol->Release(); g_pin[i].vol = nullptr; }
    if (comOK) CoUninitialize();
    ma_context_uninit(&ctx);
    timeEndPeriod(1);
    tlog("[twinspeak] stopped.\n");
    return 0;
}
