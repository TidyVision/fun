// twinspeak_core.h -- v9.7.0 the audio-routing core's contract.
//
// The engine is split in two files:
//   twinspeak_core.cpp : miniaudio in, the four legs out. NOTHING else.
//                        One shared capture, one raw ring per leg, one
//                        buffer switch per device, the packet-mode depth
//                        servo, the futex mix crew, the keep-alive, the
//                        chime, the tick WAV, the volume pin/watch, the
//                        watchdog restarts, the power guard, the
//                        default-source gate, the health line. Every symbol
//                        below is owned by the core; the shell only calls
//                        what is declared here.
//   twinspeak.cpp     : the GUI + the ini + main(). No audio code.
//
// v9.7.0 THE DEFAULT-SOURCE GATE: the engine exists ONLY while the Windows
//   default render endpoint IS the configured source (input_device).
//   Anywhere else: nothing is opened or held (no "device in use" for other
//   players); the health loop reopens the whole engine when the default
//   returns to the source. (This replaces v9.6.0's per-leg yield guard.)
//
// v9.6.0 ROUTING (field-directed, the simple four-leg flow):
//   capture = miniaudio loopback on the SPECIFIED OUTPUT DEVICE
//             (input_device is a render endpoint; input_is_output is
//             the one capture path, no capture-device branch remains);
//   leg1 (mains)  -> the audio device. FL/FR + LFE fold on L/R;
//   leg2 (center) -> a SEPARATE audio device. ch3 on L/R, filling;
//   leg3 (rears)  -> a separate audio device. ch7+ch5 L, ch8+ch6 R;
//                    EMPTY device -> the rears FOLD onto leg1 (mainS);
//   leg4 (LFE)    -> NO device. gain_lfe * ch4 onto whichever leg
//                    output_device4 names (leg1/leg2/leg3) -- the leg
//                    that was already routed from the mains.
//
// CONSISTENT FLOW (why this exists): devices drift apart in delay and
//   never align. Every leg therefore runs the SAME pipeline shape --
//   same period request, same anchor floor, same depth servo, same
//   cross-leg correction -- so the only difference left between legs
//   is the delay the user set. The masterDepthGuard holds leg1's ring
//   at its anchor; syncToFirst grinds every other leg's UNDELAYED
//   boundary to leg1's, 240 frames a bite, at most one bite per 2 s,
//   inside digital silence when it can. Packet mode stays the default:
//   a clock mismatch bends rate sub-0.1% instead of drying the ring.

#ifndef TWS_CORE_H
#define TWS_CORE_H

#include <string>
#include <vector>

// ---------------------------------------------------------------- config --
// The ONE config struct. The shell owns the ini load/save and the GUI
// mapping; the core reads it. Field names are the ini key names.
struct TwsConfig {
    std::string inputDevice;         // the render endpoint to capture
    std::string outputDevice1;       // leg1 (mains)
    std::string outputDevice2;       // leg2 (center); EMPTY = fold to leg1
    std::string outputDevice3;       // leg3 (rears);  EMPTY = fold to leg1
    std::string outputDevice4;       // leg4 (LFE fold target name; never opens)
    float       gainL       = 1.0f;   // leg1 FL/FR
    float       gainR       = 1.0f;
    float       gainLfe     = 0.4f;   // LFE fold gain (leg1 + leg4's fold)
    float       gainCenter  = 1.0f;   // leg2 / the empty-leg2 fold
    float       gainRl      = 1.0f;   // leg3 L (rear L + side L)
    float       gainRr      = 1.0f;   // leg3 R
    float       gainSl      = 1.0f;   // side fold onto leg3 L/R
    float       gainSr      = 1.0f;
    bool        centerEq    = false;  // RBJ peaking band on leg2's ch3
    float       centerEqFreq = 287.0f;
    float       centerEqGain = 4.0f;
    float       centerEqQ    = 0.8f;
    bool        parallelMix  = true;  // the futex mix crew on/off
    bool        packetMode   = true;  // per-packet depth servo + ASRC
    bool        clockSync    = true;  // masterDepthGuard + syncToFirst
    float       delay1Ms     = 0.0f;  // bit-exact ring delay per leg
    float       delay2Ms     = 0.0f;
    float       offset3Ms    = 0.0f;
    bool        volumeFollow = true; // input endpoint volume = master knob
    bool        pinOutputs   = true; // leg endpoints pinned 100%/unmuted
    bool        background   = true; // console detach (console mode only)
    bool        chimes       = true;
    float       chimeGain    = 0.4f;
    bool        startupEngine = true;
    // the GUI's EQ save slots (5 presets: on/freq/gain/Q) -- the center EQ
    // itself is the four centerEq fields above; these are GUI bookkeeping.
    bool        eqSlotOn[5]    = { true, false, false, false, false };
    float       eqSlotF[5]     = { 387.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float       eqSlotG[5]     = { 3.5f,   0.0f, 0.0f, 0.0f, 0.0f };
    float       eqSlotQ[5]     = { 0.30f,  0.0f, 0.0f, 0.0f, 0.0f };
};

// ---------------------------------------------------------------- logging --
// One logger, shared. The shell installs a sink so the GUI pane sees the
// core's lines; without a sink the core prints to stdout (console mode).
extern void (*twsLogSink)(const char* line);
void twsLog(const char* fmt, ...);
#define tlog twsLog

// Console status helpers (3-line in-place block). Console mode only;
// the GUI never calls these (it rides the sink).
void twsStatusBlock(const char* const* f1, int n1,
                     const char* const* f2, int n2,
                     const char* const* f3, int n3);

// ---------------------------------------------------------------- engine --
// The config the core reads. The shell fills it from the ini before
// twsEngineRun / twsEngineStart; the core never writes it.
extern TwsConfig twsCfg;

// Run the engine until twsStop() is called. Returns 0 on a clean stop,
// nonzero on a startup refusal. This is the CONSOLE entry (background
// detach, health line, Ctrl+C). The GUI calls twsEngineStart on its
// engine thread instead; the bodies are one function.
int  twsEngineRun(const char* iniPath, bool fgForce, bool guiMode);

// Lifecycle for the GUI thread.
bool twsEngineStart(const char* iniPath);   // false on refusal (log says why)
void twsEngineJoin();                       // wait for a started engine
void twsEngineStop();                       // signal + drain (silence gap)

// Shared stop/run flags (the shell's tray/menu/ctrl handler flips these).
extern volatile bool twsRun;
extern volatile bool twsStopSilence;   // true past the last stop signal: switches emit silence

// Routing state for the shell's banners (set once per run by the core).
extern bool twsCenterFold;             // leg2 EMPTY: ch3 folds onto leg1
extern bool twsRearFold;               // leg3 EMPTY: ch7/ch8 (+ch5/ch6) fold onto leg1

// ------------------------------------------------------------- devices ----
// --list / --refresh support. Returns the endpoint names for the GUI's
// pickers; also the console listing itself.
void twsListDevices();                                    // --list
void twsEnumDevices(std::vector<std::string>& playback,   // refresh
                    std::vector<std::string>& capture);

// ------------------------------------------------------------ tick test ----
// The 120 bpm tick WAV: 48 kHz, 8ch, 16-bit PCM, 1500 Hz burst, 8 beats,
// FL/FR/FC/LFE, LOUD (0.95 peak). Generated once beside the ini if it
// does not exist; the shell opens it (ShellExecute) -- one player graph,
// one latency class on every device it touches.
std::string twsTickWavPath(const char* iniDir);
bool        twsTickWavWrite(const std::string& path);

// ------------------------------------------------------------- version ----
extern const char* TWS_VERSION;

#endif // TWS_CORE_H
