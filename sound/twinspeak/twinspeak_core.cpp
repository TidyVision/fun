// twinspeak_core.cpp -- v9.6.0 THE AUDIO-ROUTING CORE.
//
// Everything audio lives here; twinspeak.cpp is the GUI/ini/main shell.
// The flow (field-directed, "a simple audio pipeline with nothing extra"):
//
//   miniaudio captures the SPECIFIED OUTPUT DEVICE (loopback)
//     -> capCallback fans the 8ch block out (memcpy) into 4 raw rings
//     -> leg1 (mains)   : FL/FR + gain_lfe*LFE -> the audio device
//     -> leg2 (center)  : ch3 -> a separate audio device (filling)
//     -> leg3 (rears)   : L=gain_rl*(ch7+ch5), R=gain_rr*(ch8+ch6)
//                         -> a separate audio device; EMPTY -> fold to leg1
//     -> leg4 (LFE fold): gain_lfe*ch4 onto whichever leg output_device4
//                         names -- a leg already routed from the mains;
//                         NO device of its own, ever.
//
//   Every leg runs the SAME pipeline (consistent flow): same 768-fr x1
//   exclusive request, same device-buffer anchor floor, same per-packet
//   depth servo, same cross-leg sync to leg1's UNDELAYED boundary. The
//   only inter-leg difference left is delay_ms -- which is the point:
//   devices drift apart in delay; the engine grinds the drift out.
//
//   The futex mix crew (WaitOnAddress / WakeByAddressAll on the seqlock
//   word) is preserved tightly: workers park on the doorbell, the RT mix
//   thread only stores and wakes, never enters a kernel lock.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "twinspeak_core.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <propsys.h>
#include <propidl.h>
#include <avrt.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <xmmintrin.h>
#include <pmmintrin.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <algorithm>

#define TWS_VERSION_C "9.6.0"
const char* TWS_VERSION = TWS_VERSION_C;

// ============================================================ constants ===
static constexpr int      kInCh          = 8;      // L R C LFE SL SR RL RR
static constexpr int      kOutCh         = 2;      // every leg is stereo L+R
static constexpr int      kScratchFrames = 8192;
static constexpr unsigned kSampleRate    = 48000;

// the depth servo's timing (one look per window, one bite per interval)
static constexpr unsigned kSyncNudgeMs    = 100;
static constexpr unsigned kSyncSettleMs   = 2000;
static constexpr int      kSyncRefDeadMs  = 1000;
static constexpr int      kDriftCorrectFr  = 240;   // 5 ms @ 48 kHz
static constexpr unsigned kDriftIntervalMs = 2000;

static constexpr double kTwoPi = 6.283185307179586;

// packet mode (the per-packet trim servo; constants are the engine's truth)
static constexpr unsigned kOutPeriodFrames = 768;   // 16 ms, one constant request
static constexpr unsigned kOutPeriods      = 1;
static constexpr long kPktPpmPerFrame = 50;
static constexpr long kPktFloorFr    = 384;         // 8 ms ring floor
static constexpr long kPktMaxPpm      = 4000;
static constexpr double kPktEmaAlpha  = 768.0 / 48000.0;
static constexpr long    kPktEngageFr = 8;
static constexpr long    kPktRelaxFr  = 2;
static constexpr double  kPktPiKp  = 2.0;
static constexpr double  kPktPiKi  = 5e-4;
static constexpr double  kPktAwKb  = 0.1;
static constexpr double  kPktSlewSlow = 0.2;
static constexpr double  kPktSlewFast = 5.0;
static constexpr double  kPktSlewThr  = 150.0;
static constexpr double kPktEmaSlowAlpha = 96.0 / 48000.0;

// keep-alive (every leg, through every gate and gap)
static constexpr unsigned kKeepAliveTableMax = 48000;
static constexpr float    kKeepAliveHz      = 20.0f;
static constexpr float    kKeepAliveGain    = 0.001f;
static float              g_kaHz   = kKeepAliveHz;
static float              g_kaGain = kKeepAliveGain;
static unsigned           g_kaLen  = 48000;
static float              g_kaTable[kKeepAliveTableMax];
static unsigned           g_kaIdxLeg[4] = {0, 0, 0, 0};

// ================================================================ config ===
TwsConfig twsCfg;
volatile bool twsRun = true;
volatile bool twsStopSilence = false;
bool twsCenterFold = false;
bool twsRearFold   = false;

static size_t g_capPeriodFrames = 192;
static size_t g_periodFrames[4] = {96, 96, 96, 96};

// ================================================================ logging ===
static CRITICAL_SECTION g_logCS;
static bool     g_logInit   = false;
static bool     g_logToFile = false;
static unsigned g_logGen   = 0;
static bool     g_stAnchorOK = false;
static COORD    g_stAnchor   = {0, 0};
static unsigned g_stGen      = 0;

void (*twsLogSink)(const char* line) = nullptr;

void twsLog(const char* fmt, ...)
{
    if (!g_logInit) { InitializeCriticalSection(&g_logCS); g_logInit = true; }
    va_list ap;
    EnterCriticalSection(&g_logCS);
    ++g_logGen;
    if (twsLogSink) {
        char buf[8192];
        va_start(ap, fmt);
        const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n >= 0 && n < (int)sizeof(buf)) {
            twsLogSink(buf);
        } else {
            std::string big((size_t)(n > 0 ? n : 8191) + 1, '\0');
            va_start(ap, fmt);
            vsnprintf(&big[0], big.size(), fmt, ap);
            va_end(ap);
            twsLogSink(big.c_str());
        }
    } else {
        va_start(ap, fmt);
        vfprintf(stdout, fmt, ap);
        va_end(ap);
        fflush(stdout);
    }
    LeaveCriticalSection(&g_logCS);
}

static int conWidth()
{
    CONSOLE_SCREEN_BUFFER_INFO ci;
    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &ci)) {
        const int w = ci.srWindow.Right - ci.srWindow.Left + 1;
        if (w >= 8) return w;
        if (ci.dwSize.X > 0) return ci.dwSize.X;
    }
    return 80;
}

static std::string cullLine(const char* const* f, int n, int budget)
{
    std::string s;
    if (budget <= 0) return s;
    for (int k = n; k >= 1; --k) {
        s.clear();
        for (int i = 0; i < k; ++i) { if (i) s += " | "; s += f[i]; }
        if ((int)s.size() <= budget) return s;
    }
    s.resize((size_t)budget);
    return s;
}

void twsStatusBlock(const char* const* f1, int n1,
                    const char* const* f2, int n2,
                    const char* const* f3, int n3)
{
    if (!g_logInit) return;
    const int w = conWidth();
    const int budget = w - 1;
    std::string ln[3];
    ln[0] = cullLine(f1, n1, budget);
    ln[1] = cullLine(f2, n2, budget);
    ln[2] = cullLine(f3, n3, budget);
    EnterCriticalSection(&g_logCS);
    fflush(stdout);
    const HANDLE hc = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO ci;
    const bool conOK = hc && hc != INVALID_HANDLE_VALUE
                       && GetConsoleScreenBufferInfo(hc, &ci);
    bool inPlace = conOK && g_stAnchorOK && g_stGen == g_logGen;
    if (inPlace && (g_stAnchor.Y < ci.srWindow.Top
                    || g_stAnchor.Y > ci.srWindow.Bottom - 2)) {
        inPlace = false;
        g_stAnchorOK = false;
    }
    if (inPlace) SetConsoleCursorPosition(hc, g_stAnchor);
    for (int i = 0; i < 3; ++i) {
        std::string s = ln[i];
        if ((int)s.size() > budget) s.resize((size_t)budget);
        else if (inPlace && (int)s.size() < budget)
            s.append((size_t)(budget - (int)s.size()), ' ');
        s += '\n';
        fwrite(s.data(), 1, s.size(), stdout);
    }
    fflush(stdout);
    if (!inPlace) {
        if (conOK) {
            g_stAnchor.Y  = ci.srWindow.Bottom - 2;   // park below the block
            g_stAnchor.X  = (ci.srWindow.Right - ci.srWindow.Left + 1 > 0)
                          ? 0 : 0;
            g_stAnchorOK  = true;
            g_stGen       = g_logGen;
        }
    }
    LeaveCriticalSection(&g_logCS);
}

// =========================================================== SPSC ring ====
struct Ring {
    std::vector<float>     buf;
    size_t                 cap = 0, mask = 0, stride = 0;
    std::atomic<unsigned long long> head{0}, tail{0};
    std::atomic<unsigned long long> seqTail{0};   // capture-sequence stream position

    void init(size_t frames, size_t channels) {
        stride = channels;
        size_t c = 256; while (c < frames) c <<= 1;
        buf.assign(c * stride, 0.0f);
        cap = c; mask = c - 1;
        head.store(0); tail.store(0);
        seqTail.store(0, std::memory_order_release);
    }
    size_t count() const {
        return (size_t)(head.load(std::memory_order_acquire) - tail.load(std::memory_order_acquire));
    }
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
    void tailAdvance(unsigned long long want) {
        unsigned long long cur = tail.load(std::memory_order_relaxed);
        while (cur < want) {
            if (tail.compare_exchange_weak(cur, want, std::memory_order_release,
                                           std::memory_order_relaxed)) break;
        }
    }
    void seqTailAdvance(unsigned long long want) {
        unsigned long long cur = seqTail.load(std::memory_order_relaxed);
        while (cur < want) {
            if (seqTail.compare_exchange_weak(cur, want, std::memory_order_release,
                                              std::memory_order_relaxed)) break;
        }
    }
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
    size_t discardOldest(size_t n) {
        const unsigned long long t = tail.load(std::memory_order_relaxed);
        const unsigned long long h = head.load(std::memory_order_acquire);
        const size_t avail = (size_t)(h - t);
        if (n > avail) n = avail;
        if (n) {
            tailAdvance(t + n);
            seqTailAdvance(seqTail.load(std::memory_order_relaxed) + n);
        }
        return n;
    }
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

// ================================================================= legs ====
struct Leg {
    int                 index = 0;
    int                 outCh = kOutCh;
    bool                disabled = false;
    Ring                raw;
    std::vector<float>  scratch;                 // switch-private 8ch buffer
    ma_device           dev = {};
    ma_device_id        id = {};
    bool                isExclusive = false;
    std::string         name;

    std::atomic<unsigned long long> rawDrop{0}, und{0};
    std::atomic<unsigned long long> captured{0}, popped{0};
    size_t              delayFrames = 0;
    float               delayMs     = 0.0f;
    size_t              anchorFrames = 0;
    // packet mode state
    unsigned            pktRate     = 48000;
    long                pktTrimPpm  = 0;
    unsigned long long  pktEngaged  = 0;
    unsigned long long  pktBypass  = 0;
    float               pktHist[4][8];
    bool                pktHaveHist = false;
    double              pktPhase    = 0.0;
    double              pktRatio    = 1.0;
    std::vector<float>  pktRs;
    unsigned long long  pktLastTrimLogMs = 0;
    double              pktDepthEma  = -1.0;
    double              pktDepthEma2 = -1.0;
    double              pktPiI       = 0.0;
    double              pktAppliedPpm = 0.0;
    bool                pktEngagedOn = false;
    // sync state (single writer: this leg's device thread)
    unsigned long long     lastSyncMs    = 0;
    unsigned long long     syncDrops     = 0;
    unsigned long long     syncPads      = 0;
    unsigned long long     lastSyncLogMs = 0;
    unsigned long long     lastBiteMs    = 0;
    unsigned long long     wardenDrops   = 0;
    unsigned long long     wardenPads    = 0;
    unsigned long long     depthDrops   = 0;
    size_t                 biteDue       = 0;
    bool                   biteForced    = false;
    size_t                 padBudget     = 0;
    bool                   relandPending = false;
    // pacing telemetry
    LARGE_INTEGER       lastSwQpc = {};
    bool                haveSwQpc = false;
    unsigned long long  lastLateLogMs = 0;
    std::atomic<unsigned long long> lateSwitches{0};
    std::atomic<bool>   yielded{false};
    size_t              prefillRemaining = 0;
    std::atomic<double> lagMs{0.0};
    std::atomic<unsigned long long> firstSwitchUs{0};
    unsigned long long  lastReopenMs = 0;
};
static Leg g_leg[4];

// leg4's LFE fold target (-1 = idle; 0 = leg1, 1 = leg2, 2 = leg3)
static int  g_lfeFoldLeg = -1;

static const char* legNeedle(int i) {
    switch (i) {
        case 0:  return twsCfg.outputDevice1.c_str();
        case 1:  return twsCfg.outputDevice2.c_str();
        case 2:  return twsCfg.outputDevice3.c_str();
        default: return "";
    }
}

// ============================================================ CPU + boost ===
struct CpuTopo {
    int groups = 1;
    int lpTotal = 1;
    int lpPinnable = 1;
    int lpByGroup[64] = { 1 };
    bool viaGroups = false;
    WORD       grp[512] = {};
    DWORD_PTR  mask[512] = {};
    bool       ok[512]  = {};
};
static CpuTopo g_cpu;
static void cpuTopoProbe()
{
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    int total = (int)si.dwNumberOfProcessors;
    if (total < 1) total = 1;
    g_cpu.groups = 1;
    g_cpu.viaGroups = false;
    g_cpu.lpByGroup[0] = total;
    {
        int g = (int)GetActiveProcessorGroupCount();
        if (g < 1) g = 1;
        if (g > 64) g = 64;
        int sum = 0;
        for (int i = 0; i < g; ++i) {
            const int c = (int)GetActiveProcessorCount((WORD)i);
            g_cpu.lpByGroup[i] = c;
            sum += c;
        }
        if (sum >= 1) { g_cpu.groups = g; g_cpu.lpTotal = sum; g_cpu.viaGroups = true; }
    }
    if (!g_cpu.viaGroups) { g_cpu.lpTotal = total; g_cpu.lpByGroup[0] = total; }
    int lp = 0;
    for (int gi = 0; gi < g_cpu.groups && lp < 512; ++gi) {
        const int n = g_cpu.lpByGroup[gi];
        if (n < 1) continue;
        for (int li = 0; li < n && lp < 512; ++li) {
            g_cpu.grp[lp]  = (WORD)gi;
            g_cpu.mask[lp] = ((DWORD_PTR)1) << li;
            g_cpu.ok[lp]   = true;
            ++lp;
        }
    }
    if (lp < 1) {
        g_cpu.ok[0] = false;
        g_cpu.lpTotal = 1;
        lp = 1;
    }
    g_cpu.lpPinnable = lp;
}

static void cpuPinThreadToLp(const int slotIdx)
{
    if (slotIdx < 0 || slotIdx >= 512 || !g_cpu.ok[slotIdx]) return;
    GROUP_AFFINITY ga;
    ga.Group = g_cpu.grp[slotIdx];
    ga.Mask  = g_cpu.mask[slotIdx];
    ga.Reserved[0] = ga.Reserved[1] = ga.Reserved[2] = 0;
    if (!SetThreadGroupAffinity(GetCurrentThread(), &ga, nullptr))
        g_cpu.ok[slotIdx] = false;
}

static int audioCoreTicket(const int coreIdx)
{
    if (g_cpu.lpTotal < 2) return -1;
    return (1 + coreIdx) % g_cpu.lpPinnable;
}
static void cpuPinAudioThread(const int coreIdx)
{
    cpuPinThreadToLp(audioCoreTicket(coreIdx));
}

static void boostAudioThreadEx(int mmcssSubPrio) {
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    static HMODULE av = LoadLibraryW(L"avrt.dll");
    if (av) {
        typedef HANDLE (WINAPI *AvSetMmFn)(LPCWSTR, LPDWORD);
        AvSetMmFn set = (AvSetMmFn)GetProcAddress(av, "AvSetMmThreadCharacteristicsW");
        if (set) {
            DWORD idx = 0;
            HANDLE h = set(L"Pro Audio", &idx);
            if (h && mmcssSubPrio >= 0) {
                typedef HRESULT (WINAPI *AvPrioFn)(HANDLE, int);
                AvPrioFn prio = (AvPrioFn)GetProcAddress(av, "AvSetMmThreadPriority");
                if (prio) prio(h, mmcssSubPrio);
            }
        }
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
}

static void boostDeviceThreadCore(const int coreIdx) {
    thread_local bool done = false;
    if (done) return;
    done = true;
    boostAudioThreadEx(-1);
    cpuPinAudioThread(coreIdx);
}

// ========================================================= COM GUIDs + vol ===
static const CLSID kCLSID_MMDeviceEnumerator =
    { 0xbcde0395, 0xe52f, 0x4d1b, {0xaa,0xe1,0x0a,0x59,0x86,0x97,0x09,0x63} };
static const IID kIID_IMMDeviceEnumerator =
    { 0xa95664d2, 0x9614, 0x4f35, {0xa7,0x46,0xde,0x8d,0xb6,0x36,0x17,0xe6} };
static const IID kIID_IAudioEndpointVolume =
    { 0x5cdf2c1d, 0x8f41, 0x44a9, {0x92,0x54,0x2c,0x3c,0x2f,0x3a,0x62,0x12} };
static const PROPERTYKEY kPKEY_Device_FriendlyName =
    { {0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0xa4,0x6d,0x4c,0x02}}, 14 };
static const IID kIID_IAudioSessionManager2 =
    { 0x77aa99a0, 0x1bd6, 0x484f, {0x8b,0x07,0xb9,0x9f,0x47,0x01,0x5f,0xd9} };
static const IID kIID_IAudioSessionControl2 =
    { 0xbfb7ff88, 0xf759, 0x4a4c, {0x9d,0xa7,0x5a,0x5d,0x2c,0x5a,0x3e,0x4e} };

static std::atomic<float> g_vol{1.0f};
static std::atomic<bool>  g_volMuted{false};
static std::atomic<bool>  g_volFollowLive{false};
static IAudioEndpointVolume* g_srcVol = nullptr;
static std::wstring          g_srcVolName;

struct OutPin {
    std::wstring           id;
    IAudioEndpointVolume*  vol = nullptr;
};
static OutPin g_pin[4];

static std::atomic<unsigned long long> g_hbCap{0};
static std::atomic<unsigned long long> g_hbOutTab[4];
static std::atomic<bool> g_driftRecalReq{false};
static unsigned long long g_bootMs = 0;
static LARGE_INTEGER g_qpcBoot = {};
static double        g_qpf     = 0.0;

static inline void hbOut(int i) {
    g_hbOutTab[i].store(GetTickCount64(), std::memory_order_relaxed);
}
static inline unsigned long long hbOutGet(int i) {
    return g_hbOutTab[i].load(std::memory_order_relaxed);
}

static std::wstring narrowToWide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static bool wContainsCI(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return false;
    std::wstring h = hay, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::towlower);
    std::transform(n.begin(), n.end(), n.begin(), ::towlower);
    return h.find(n) != std::wstring::npos;
}
static bool containsCI(const std::string& hay, const std::string& needle) {
    return wContainsCI(narrowToWide(hay), narrowToWide(needle));
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

static bool isDefaultRenderEndpointId(const wchar_t* idStr) {
    if (!idStr || !idStr[0]) return false;
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                kIID_IMMDeviceEnumerator, (void**)&en)) || !en)
        return false;
    bool mine = false;
    IMMDevice* def = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &def)) && def) {
        LPWSTR defId = nullptr;
        if (SUCCEEDED(def->GetId(&defId)) && defId) {
            mine = wcscmp(defId, idStr) == 0;
            CoTaskMemFree(defId);
        }
        def->Release();
    }
    en->Release();
    return mine;
}

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

// yield guard: foreign ACTIVE sessions on a leg's device
static int foreignActiveSessions(const std::string& nameNeedle) {
    const std::wstring wNeedle = narrowToWide(nameNeedle);
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                kIID_IMMDeviceEnumerator, (void**)&en)) || !en)
        return -1;
    int count = -1;
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
        count = 0;
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
                                    if (FAILED(se->GetSession(s, &sc)) || !sc) continue;
                                    AudioSessionState st = AudioSessionStateInactive;
                                    sc->GetState(&st);
                                    if (st == AudioSessionStateActive) {
                                        DWORD pid = 0;
                                        IAudioSessionControl2* sc2 = nullptr;
                                        if (SUCCEEDED(sc->QueryInterface(kIID_IAudioSessionControl2,
                                                                         (void**)&sc2)) && sc2) {
                                            sc2->GetProcessId(&pid);
                                            sc2->Release();
                                        }
                                        ++count;
                                        tlog("[twinspeak] yield check  : pid %lu ACTIVE on \"%s\"\n",
                                             (unsigned long)pid, nameNeedle.c_str());
                                    }
                                    sc->Release();
                                }
                            }
                            se->Release();
                        }
                        sm2->Release();
                    }
                    dev->Release();
                    break;
                }
                dev->Release();
            }
        }
        col->Release();
    }
    en->Release();
    return count;
}

static void pinCheckTick() {
    if (!twsCfg.pinOutputs) return;
    for (int i = 0; i < 4; ++i) {
        if (g_leg[i].disabled) continue;
        if (g_leg[i].yielded.load(std::memory_order_relaxed)) continue;
        IAudioEndpointVolume* v = g_pin[i].vol;
        if (!v) continue;
        float s = 0.0f; BOOL m = FALSE;
        if (SUCCEEDED(v->GetMasterVolumeLevelScalar(&s)) && SUCCEEDED(v->GetMute(&m))) {
            if (m != FALSE || s < 0.999f) {
                v->SetMasterVolumeLevelScalar(1.0f, nullptr);
                v->SetMute(FALSE, nullptr);
                static unsigned long long lastPinLog[4] = { 0, 0, 0, 0 };
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
    while (twsRun) {
        Sleep(25);
        if (!twsRun) break;
        if (g_srcVol) {
            float v = 1.0f; BOOL m = FALSE;
            if (SUCCEEDED(g_srcVol->GetMasterVolumeLevelScalar(&v)))
                g_vol.store(v, std::memory_order_relaxed);
            if (SUCCEEDED(g_srcVol->GetMute(&m)))
                g_volMuted.store(m != FALSE, std::memory_order_relaxed);
            g_volFollowLive.store(true, std::memory_order_relaxed);
        }
        if (++tick % 80 == 0) pinCheckTick();
    }
    if (com) CoUninitialize();
    return 0;
}

// ============================================================ power guard ===
static std::atomic<bool> g_powerPark{false};
static std::atomic<bool> g_powerWoken{false};

static LRESULT CALLBACK powerWndProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    (void)w; (void)lp;
    if (m == WM_POWERBROADCAST) {
        if (wp == PBT_APMSUSPEND) {
            if (!g_powerPark.exchange(true))
                tlog("[twinspeak] power guard : system suspend -- parking the pumps (audio resumes on wake)\n");
        } else if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
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
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"", 0,
                             0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    if (!w) return 0;
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}

// ========================================================= the mix crew ====
// leg1's mix lanes: 0 center-fold chain, 1..2 surround bass L/R (unused in
// the simple flow -- kept for crew structure), 3..4 rear comb L/R. The
// simple mix keeps lanes 0, 3, 4 (fold paths); dead lanes pass through.
struct MixJob {
    const float* sc = nullptr;
    size_t       n  = 0;
    bool centerFold = false, cLive = false;
    bool rlLive = false, rrLive = false, combAdv = false;
    float foldZ1[2] = {0.0f, 0.0f};      // the fold chain's one-pole states
    float rcBufL[32] = {}, rcBufR[32] = {};
    int   rcIdx = 0;
};
static MixJob         g_mixJob;
static std::atomic<long> g_mixSeq{0};        // even = stable, odd = mid-write
static constexpr int kMixLanes = 5;
static constexpr size_t kMixCrewMinFrames = 64;
static constexpr unsigned kMixCrewArms = 32;

struct alignas(64) MixLaneOut {
    float term[kScratchFrames] = {};
    float foldZ1[2] = {0.0f, 0.0f};
    float rcBufL[32] = {}, rcBufR[32] = {};
    int   rcIdx = 0;
    std::atomic<long> landed{0};
};
static MixLaneOut  g_mixW[kMixLanes];
static MixLaneOut  g_mixSelf[kMixLanes];
static std::atomic<bool>  g_mixCrewRun{false};
static std::atomic<int>   g_mixCrewLive{0};
static HANDLE      g_mixCrewThread[kMixLanes] = {};
static unsigned long long g_mixCrewRounds  = 0;
static unsigned long long g_mixCrewAssists = 0;
static unsigned long long g_mixCrewAdopted = 0;
static unsigned long long g_mixCrewSkips   = 0;
static unsigned g_mixCrewMissStreak = 0;
static unsigned g_mixCrewCool       = 0;

// THE FUTEX DOORBELL (preserved tightly): workers WaitOnAddress on the
// seq word; the RT thread only WakeByAddressAll's it. The RT thread
// NEVER enters a kernel lock -- a priority inversion here was the field's
// crackle once, and the doorbell is the fix.
static BOOL (WINAPI *g_pWaitOnAddress)(volatile VOID*, PVOID, SIZE_T, DWORD) = nullptr;
static void (WINAPI *g_pWakeByAddressAll)(PVOID) = nullptr;
static bool g_futexReady = false;
static void futexResolve()
{
    if (g_futexReady) return;
    g_futexReady = true;
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;
    g_pWaitOnAddress   = (BOOL (WINAPI*)(volatile VOID*, PVOID, SIZE_T, DWORD))
                         GetProcAddress(k32, "WaitOnAddress");
    g_pWakeByAddressAll= (void (WINAPI*)(PVOID)) GetProcAddress(k32, "WakeByAddressAll");
}
static void futexPark(const long expect)
{
    if (g_pWaitOnAddress && g_pWakeByAddressAll) {
        long e = expect;
        g_pWaitOnAddress((volatile VOID*)&g_mixSeq, &e, sizeof(e), 250);
    } else {
        for (int i = 0; i < 400; ++i) { _mm_pause(); }
        Sleep(1);
    }
}

// the fold chain's comb (rear fold path): a 24-frame + frac delay pair
static constexpr float kRearCombFrac = 0.48f;
static inline float rearCombRun(const float x, float* buf, const int idx)
{
    const float a = buf[idx];
    const float b = buf[(idx + 1) & 31];
    const float y = a + kRearCombFrac * (b - a);
    buf[idx] = x;
    return y;
}

// one lane's exact op sequence: state in (the job snapshot) / terms out.
// NOTHING global is touched -- the same function runs on a worker or on
// the mix thread's self-completion, so the math has ONE definition.
static void mixCrewLane(const MixJob& j, const int lane, MixLaneOut& out)
{
    out.foldZ1[0] = j.foldZ1[0]; out.foldZ1[1] = j.foldZ1[1];
    memcpy(out.rcBufL, j.rcBufL, sizeof(out.rcBufL));
    memcpy(out.rcBufR, j.rcBufR, sizeof(out.rcBufR));
    out.rcIdx = j.rcIdx;
    const size_t n = j.n;
    const float* sc = j.sc;
    if (lane == 0) {
        // the center fold (ch3 -> FL/FR at gain_center, one-pole HF lean)
        if (j.centerFold && j.cLive) {
            float z1 = out.foldZ1[0];
            for (size_t i = 0; i < n; ++i)
                out.term[i] = sc[i * kInCh + 2] - 0.25f * z1,
                z1 = sc[i * kInCh + 2];
            out.foldZ1[0] = z1;
        }
    } else if (lane == 3) {
        // the rear fold's comb, L (ch7)
        float buf[32];
        memcpy(buf, j.rcBufL, sizeof(buf));
        int idx = j.rcIdx;
        if (j.rlLive) {
            for (size_t i = 0; i < n; ++i) {
                out.term[i] = rearCombRun(sc[i * kInCh + 6], buf, idx);
                if (j.combAdv) { if (++idx >= 32) idx = 0; }
            }
        } else {
            for (size_t i = 0; i < n; ++i) out.term[i] = 0.0f;
            if (j.combAdv) for (size_t i = 0; i < n; ++i) { if (++idx >= 32) idx = 0; }
        }
        memcpy(out.rcBufL, buf, sizeof(out.rcBufL));
        out.rcIdx = idx;
    } else if (lane == 4) {
        // the rear fold's comb, R (ch8)
        float buf[32];
        memcpy(buf, j.rcBufR, sizeof(buf));
        int idx = j.rcIdx;
        if (j.rrLive) {
            for (size_t i = 0; i < n; ++i) {
                out.term[i] = rearCombRun(sc[i * kInCh + 7], buf, idx);
                if (j.combAdv) { if (++idx >= 32) idx = 0; }
            }
        } else {
            for (size_t i = 0; i < n; ++i) out.term[i] = 0.0f;
            if (j.combAdv) for (size_t i = 0; i < n; ++i) { if (++idx >= 32) idx = 0; }
        }
        memcpy(out.rcBufR, buf, sizeof(out.rcBufR));
        out.rcIdx = idx;
    } else {
        // lanes 1/2 idle in the simple flow: pass zeros (identity state)
        for (size_t i = 0; i < n; ++i) out.term[i] = 0.0f;
    }
}

static DWORD WINAPI mixCrewWorker(LPVOID p)
{
    const int lane = (int)(INT_PTR)p;
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    cpuPinThreadToLp(g_cpu.lpPinnable - 1 - lane);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    long lastTag = 0;
    while (g_mixCrewRun.load(std::memory_order_acquire)) {
        long cur = g_mixSeq.load(std::memory_order_acquire);
        while (g_mixCrewRun.load(std::memory_order_acquire) &&
               cur / 2 <= lastTag) {
            futexPark(cur);
            cur = g_mixSeq.load(std::memory_order_acquire);
        }
        if (!g_mixCrewRun.load(std::memory_order_acquire)) break;
        MixJob j;
        long tag = 0;
        bool fresh = false;
        int pauses = 0;
        for (;;) {
            const long s1 = g_mixSeq.load(std::memory_order_acquire);
            if (s1 & 1) {
                if (++pauses >= 1024) {
                    pauses = 0;
                    if (!g_mixCrewRun.load(std::memory_order_acquire)) return 0;
                    Sleep(0);
                }
                _mm_pause();
                continue;
            }
            j = g_mixJob;
            tag = s1 / 2;
            const long s2 = g_mixSeq.load(std::memory_order_acquire);
            if (s1 != s2) continue;
            if (tag > lastTag) { fresh = true; break; }
            break;
        }
        if (!fresh) continue;
        if (!g_mixCrewRun.load(std::memory_order_acquire)) break;
        mixCrewLane(j, lane, g_mixW[lane]);
        g_mixW[lane].landed.store(tag, std::memory_order_release);
        lastTag = tag;
    }
    return 0;
}

static void mixCrewInit()
{
    if (g_mixCrewRun.exchange(true)) return;
    futexResolve();
    g_mixCrewLive.store(0, std::memory_order_relaxed);
    g_mixSeq.store(0, std::memory_order_relaxed);
    g_mixCrewRounds = 0; g_mixCrewAssists = 0;
    g_mixCrewAdopted = 0; g_mixCrewSkips = 0;
    g_mixCrewMissStreak = 0; g_mixCrewCool = 0;
    int started = 0;
    for (int lane = 0; lane < kMixLanes; ++lane) {
        g_mixCrewThread[lane] = CreateThread(nullptr, 0, mixCrewWorker,
                                             (LPVOID)(INT_PTR)lane, 0, nullptr);
        if (g_mixCrewThread[lane]) ++started;
    }
    g_mixCrewLive.store(started, std::memory_order_release);
    tlog("[twinspeak] mix          : the crew is up (%d lane workers, %s, %s)\n",
         started, twsCfg.parallelMix ? "parallel_mix on" : "parallel_mix off -- standing by",
         (g_pWaitOnAddress && g_pWakeByAddressAll) ? "futex doorbell" : "polling fallback");
}

static void mixCrewShutdown()
{
    if (!g_mixCrewRun.exchange(false)) return;
    if (g_pWakeByAddressAll) g_pWakeByAddressAll((PVOID)&g_mixSeq);
    for (int lane = 0; lane < kMixLanes; ++lane) {
        if (g_mixCrewThread[lane]) {
            WaitForSingleObject(g_mixCrewThread[lane], 2000);
            CloseHandle(g_mixCrewThread[lane]);
            g_mixCrewThread[lane] = nullptr;
        }
    }
    g_mixCrewLive.store(0, std::memory_order_relaxed);
    if (g_mixCrewRounds)
        tlog("[twinspeak] mix          : the crew idles (%llu rounds, %llu lane adopts, %llu assists, %llu skips)\n",
             g_mixCrewRounds, g_mixCrewAdopted, g_mixCrewAssists, g_mixCrewSkips);
}

// the mix crew's leg1 entry: dispatch under the seqlock, wake the futex,
// self-complete any lane that has not landed. gRL/gRR are the REAR-FOLD
// gains (leg3 EMPTY); gC the center-fold gain; gLfe the LFE fold.
static void mixCrewLeg1(const float gL, const float gR, const float gLfe,
                        const float gRL, const float gRR, const float gC, bool cLive,
                        bool rlLive, bool rrLive, bool combAdv,
                        const float* sc, float* chunk, const size_t n)
{
    MixJob j;
    j.sc = sc;  j.n = n;
    j.centerFold = twsCenterFold;
    j.cLive = cLive;
    j.rlLive = rlLive; j.rrLive = rrLive; j.combAdv = combAdv;
    j.foldZ1[0] = g_mixSelf[0].foldZ1[0];
    j.foldZ1[1] = g_mixSelf[0].foldZ1[1];
    memcpy(j.rcBufL, g_mixSelf[3].rcBufL, sizeof(j.rcBufL));
    memcpy(j.rcBufR, g_mixSelf[4].rcBufR, sizeof(j.rcBufR));
    j.rcIdx = g_mixSelf[3].rcIdx;

    bool crew = twsCfg.parallelMix &&
                g_mixCrewLive.load(std::memory_order_acquire) == kMixLanes &&
                n >= kMixCrewMinFrames &&
                g_mixCrewCool == 0;
    if (g_mixCrewCool > 0) --g_mixCrewCool;
    long tag = 0;
    if (crew) {
        tag = g_mixSeq.load(std::memory_order_relaxed) / 2 + 1;
        g_mixSeq.store(tag * 2 + 1, std::memory_order_release);   // odd: mid-write
        g_mixJob = j;
        g_mixSeq.store(tag * 2, std::memory_order_release);       // even: published
        ++g_mixCrewRounds;
        if (g_pWakeByAddressAll) g_pWakeByAddressAll((PVOID)&g_mixSeq);
    }

    // the merge (never waits): build each output slot per frame, adopting a
    // lane's term buffer if landed == tag, else self-completing inline.
    long landed[kMixLanes] = { -1, -1, -1, -1, -1 };
    if (crew) {
        for (int l = 0; l < kMixLanes; ++l)
            landed[l] = g_mixW[l].landed.load(std::memory_order_acquire);
    }
    unsigned adopted = 0;
    for (size_t i = 0; i < n; ++i) {
        const float* f = sc + i * kInCh;
        float l = gL * f[0], r = gR * f[1];
        l += gLfe * f[3]; r += gLfe * f[3];
        if (twsCenterFold && cLive) {
            if (crew && landed[0] == tag) {
                l += gC * g_mixW[0].term[i]; r += gC * g_mixW[0].term[i];
            } else {
                float z1 = g_mixSelf[0].foldZ1[0];
                const float x = f[2];
                const float term = x - 0.25f * z1;
                g_mixSelf[0].foldZ1[0] = x;
                g_mixSelf[0].term[i] = term;
                l += gC * term; r += gC * term;
            }
        }
        if (twsRearFold) {
            if (crew && landed[3] == tag) {
                l += gRL * f[6] + g_mixW[3].term[i];
                r += gRR * f[7] + g_mixW[4].term[i];
            } else {
                float bufL[32]; memcpy(bufL, g_mixSelf[3].rcBufL, sizeof(bufL));
                float bufR[32]; memcpy(bufR, g_mixSelf[4].rcBufR, sizeof(bufR));
                int idx = g_mixSelf[3].rcIdx;
                const float tl = rearCombRun(f[6], bufL, idx);
                const float tr = rearCombRun(f[7], bufR, idx);
                if (combAdv) { if (++idx >= 32) idx = 0; }
                memcpy(g_mixSelf[3].rcBufL, bufL, sizeof(bufL));
                memcpy(g_mixSelf[4].rcBufR, bufR, sizeof(bufR));
                g_mixSelf[3].rcIdx = idx;
                g_mixSelf[3].term[i] = tl;
                g_mixSelf[4].term[i] = tr;
                l += gRL * f[6] + tl;
                r += gRR * f[7] + tr;
            }
        }
        if (l >  1.0f) l =  1.0f; else if (l < -1.0f) l = -1.0f;
        if (r >  1.0f) r =  1.0f; else if (r < -1.0f) r = -1.0f;
        chunk[i * 2 + 0] = l;
        chunk[i * 2 + 1] = r;
    }
    if (crew) {
        for (int l = 0; l < kMixLanes; ++l)
            if (landed[l] == tag) ++adopted;
        g_mixCrewAdopted += adopted;
        g_mixCrewAssists += (kMixLanes - adopted);
        // commit the adopted end states back into the single-owner slots
        if (landed[0] == tag) {
            g_mixSelf[0].foldZ1[0] = g_mixW[0].foldZ1[0];
            g_mixSelf[0].foldZ1[1] = g_mixW[0].foldZ1[1];
        }
        if (landed[3] == tag) {
            memcpy(g_mixSelf[3].rcBufL, g_mixW[3].rcBufL, sizeof(g_mixSelf[3].rcBufL));
            g_mixSelf[3].rcIdx = g_mixW[3].rcIdx;
        }
        if (landed[4] == tag) {
            memcpy(g_mixSelf[4].rcBufR, g_mixW[4].rcBufR, sizeof(g_mixSelf[4].rcBufR));
            g_mixSelf[4].rcIdx = g_mixW[4].rcIdx;
        }
        if (adopted < 2) {
            if (++g_mixCrewMissStreak >= kMixCrewArms) {
                g_mixCrewMissStreak = 0;
                g_mixCrewCool = kMixCrewArms;
                ++g_mixCrewSkips;
            }
        } else {
            g_mixCrewMissStreak = 0;
        }
    }
}

// ============================================================ the mixer ====
static void mixBlock(int idx, const float* sc, float* chunk, size_t n)
{
    if (idx == 0) {
        const float gL = twsCfg.gainL, gR = twsCfg.gainR, gLfe = twsCfg.gainLfe;
        const float gRL = twsRearFold ? twsCfg.gainRl : 0.0f;
        const float gRR = twsRearFold ? twsCfg.gainRr : 0.0f;
        const float gC  = twsCfg.gainCenter;
        bool rlLive = false, rrLive = false;
        if (twsRearFold) {
            for (size_t i = 0; i < n; ++i) {
                if (sc[i * kInCh + 6] != 0.0f) rlLive = true;
                if (sc[i * kInCh + 7] != 0.0f) rrLive = true;
                if (rlLive && rrLive) break;
            }
        }
        const bool combAdv = twsRearFold && (rlLive || rrLive);
        bool cLive = false;
        if (twsCenterFold)
            for (size_t i = 0; i < n; ++i)
                if (sc[i * kInCh + 2] != 0.0f) { cLive = true; break; }
        mixCrewLeg1(gL, gR, gLfe, gRL, gRR, gC, cLive,
                    rlLive, rrLive, combAdv, sc, chunk, n);
    } else if (idx == 1) {
        // leg2: the center channel, filling (ch3 on both slots)
        const float gC = twsCfg.gainCenter;
        const float lfe4g = (g_lfeFoldLeg == 1) ? twsCfg.gainLfe : 0.0f;
        for (size_t i = 0; i < n; ++i) {
            const float* f = sc + i * kInCh;
            float c = gC * f[2];
            if (lfe4g != 0.0f) c += lfe4g * f[3];
            if (c >  1.0f) c =  1.0f; else if (c < -1.0f) c = -1.0f;
            chunk[i * 2 + 0] = c;
            chunk[i * 2 + 1] = c;
        }
    } else {
        // leg3: L = gain_rl*(ch7 + gain_sl*ch5), R = gain_rr*(ch8 + gain_sr*ch6)
        const float gRL = twsCfg.gainRl, gRR = twsCfg.gainRr;
        const float gSL = twsCfg.gainSl, gSR = twsCfg.gainSr;
        const float gLfe4 = (g_lfeFoldLeg == 2) ? twsCfg.gainLfe : 0.0f;
        for (size_t i = 0; i < n; ++i) {
            const float* f = sc + i * kInCh;
            float l = gRL * (f[6] + gSL * f[4]);
            float r = gRR * (f[7] + gSR * f[5]);
            if (gLfe4 != 0.0f) { l += gLfe4 * f[3]; r += gLfe4 * f[3]; }
            if (l >  1.0f) l =  1.0f; else if (l < -1.0f) l = -1.0f;
            if (r >  1.0f) r =  1.0f; else if (r < -1.0f) r = -1.0f;
            chunk[i * 2 + 0] = l;
            chunk[i * 2 + 1] = r;
        }
    }
}

static inline bool bitZero8(const float* p, size_t n) {
    unsigned long long w;
    const size_t nw = n * (size_t)(kInCh / 2);
    for (size_t i = 0; i < nw; ++i) {
        memcpy(&w, p + i * 2, sizeof(w));
        if (w) return false;
    }
    return true;
}

// ========================================================== the chime =====
static std::vector<float> g_chimeBuf;
static std::atomic<int>      g_chimeSel{-1};
static std::atomic<unsigned> g_chimeIdx{0};
static constexpr unsigned kChimeBootFr = (unsigned)kSampleRate * 400u / 1000u;
static std::atomic<bool>  g_chimeBootWait{false};
static unsigned           g_chimeBootServed = 0;
static unsigned long long g_chimeLastArmMs = 0;

static std::vector<float> renderChime(double f1, double ms1, double f2, double ms2,
                                       double gapMs, float gain)
{
    const size_t n1 = (size_t)(kSampleRate * ms1 / 1000.0);
    const size_t nGap = (size_t)(kSampleRate * gapMs / 1000.0);
    const size_t n2 = (size_t)(kSampleRate * ms2 / 1000.0);
    std::vector<float> out(n1 + nGap + n2, 0.0f);
    for (size_t i = 0; i < n1; ++i) {
        const double t = (double)i / (double)kSampleRate;
        const double e = exp(-t / (ms1 / 1000.0 * 0.25));
        out[i] = (float)(gain * e * sin(kTwoPi * f1 * t));
    }
    for (size_t i = 0; i < n2; ++i) {
        const double t = (double)i / (double)kSampleRate;
        const double e = exp(-t / (ms2 / 1000.0 * 0.25));
        out[n1 + nGap + i] = (float)(gain * e * sin(kTwoPi * f2 * t));
    }
    return out;
}

static void armChime(bool force) {
    if (!twsCfg.chimes || g_chimeBuf.empty()) return;
    if (g_chimeSel.load(std::memory_order_acquire) >= 0 && !force) return;
    const unsigned long long now = GetTickCount64();
    if (!force && now - g_chimeLastArmMs < 20000) return;
    g_chimeLastArmMs = now;
    g_chimeIdx.store(0, std::memory_order_relaxed);
    g_chimeSel.store(0, std::memory_order_release);
}

// ====================================================== packet sampler ====
// Hermite 4-point, 8ch, stream-continuous across chunks. pktPhase is
// CHUNK-LOCAL; pktHist holds the knots before the chunk. At integer
// positions the basis is exact; the curve is continuous across seams.
static size_t pktResample(Leg& L, const float* sc, size_t n, float* dst8, size_t maxOut)
{
    if (!L.pktHaveHist) {
        if (n < 4) return 0;
        for (int c = 0; c < kInCh; ++c) {
            L.pktHist[0][c] = sc[0 * kInCh + c];
            L.pktHist[1][c] = sc[1 * kInCh + c];
            L.pktHist[2][c] = sc[2 * kInCh + c];
            L.pktHist[3][c] = sc[3 * kInCh + c];
        }
        L.pktPhase = 4.0;
        L.pktHaveHist = true;
    }
    const double ratio = L.pktRatio;
    double s = L.pktPhase;
    size_t j = 0;
    while (j < maxOut) {
        if (s < -3.0 || s + 2.0 >= (double)n) break;
        const double fs = floor(s);
        const long long i0 = (long long)fs;
        const double t = s - fs;
        const float* kn[4];
        for (int q = 0; q < 4; ++q) {
            const long long k = i0 - 1 + q;
            kn[q] = (k < 0) ? L.pktHist[4 + (int)k] : sc + (size_t)k * (size_t)kInCh;
        }
        const double t2 = t * t, t3 = t2 * t;
        const double h00 =  2.0*t3 - 3.0*t2 + 1.0;
        const double h10 =        t3 - 2.0*t2 + t;
        const double h01 = -2.0*t3 + 3.0*t2;
        const double h11 =        t3 -       t2;
        for (int c = 0; c < kInCh; ++c) {
            const double y0 = (double)kn[1][c];
            const double y1 = (double)kn[2][c];
            const double m0 = 0.5 * ((double)kn[2][c] - (double)kn[0][c]);
            const double m1 = 0.5 * ((double)kn[3][c] - (double)kn[1][c]);
            const double y  = h00 * y0 + h10 * m0 + h01 * y1 + h11 * m1;
            dst8[j * kInCh + c] = (float)y;
        }
        s += ratio;
        ++j;
    }
    // advance the history: the last 4 source frames become the knots
    {
        const long long last = (long long)n - 4;
        if (last >= 0) {
            for (int c = 0; c < kInCh; ++c) {
                L.pktHist[0][c] = sc[(size_t)(last + 0) * kInCh + c];
                L.pktHist[1][c] = sc[(size_t)(last + 1) * kInCh + c];
                L.pktHist[2][c] = sc[(size_t)(last + 2) * kInCh + c];
                L.pktHist[3][c] = sc[(size_t)(last + 3) * kInCh + c];
            }
        }
        L.pktPhase = s - (double)n;
    }
    return j;
}

static inline void pktRetrack(Leg& L, const float* sc, size_t k) {
    if (k < 4) { L.pktHaveHist = false; return; }
    for (int c = 0; c < kInCh; ++c) {
        L.pktHist[0][c] = sc[(k - 4) * kInCh + c];
        L.pktHist[1][c] = sc[(k - 3) * kInCh + c];
        L.pktHist[2][c] = sc[(k - 2) * kInCh + c];
        L.pktHist[3][c] = sc[(k - 1) * kInCh + c];
    }
    L.pktPhase = 4.0;
    L.pktHaveHist = true;
}

// (fwd: defined below, after the callbacks -- the switch calls them early)
static void landReopenedRingCore(int idx);
static bool pickDeviceCore(ma_context* ctx, ma_device_type type, const std::string& needle,
                           ma_device_id* outId, std::string* outName);

// ===================================================== the two halves ====
// masterDepthGuard: LEG1's own ring depth, pinned at delay + anchor
// (the leg's real device buffer floor). LATE arms the drop (executed
// inside a digital-silence window); EARLY arms the pad budget (held
// samples, never holes). At most one bite per kDriftIntervalMs.
static size_t masterDepthGuard(Leg& L, size_t frameCount)
{
    const unsigned long long nowMs = GetTickCount64();
    if (nowMs - L.lastSyncMs < kSyncNudgeMs) return 0;
    L.lastSyncMs = nowMs;
    const long long over = (long long)L.raw.count()
                         - (long long)L.delayFrames
                         - (long long)L.anchorFrames;
    if (over >= (long long)kDriftCorrectFr) {
        if (nowMs - L.lastBiteMs >= kDriftIntervalMs) {
            L.biteDue    = (size_t)kDriftCorrectFr;
            L.biteForced = (over >= (long long)(2 * kDriftCorrectFr));
            L.lastBiteMs = nowMs;
        }
        return 0;
    }
    if (over <= 0) {
        if (L.padBudget == 0 && nowMs - L.lastBiteMs >= kDriftIntervalMs) {
            L.padBudget = (size_t)kDriftCorrectFr;
            L.wardenPads++;
            if (nowMs - L.lastSyncLogMs >= 10000) {
                L.lastSyncLogMs = nowMs;
                tlog("[twinspeak] depth out1    : ring %lld fr under its anchor -- the guard armed a %d-frame pad\n"
                     "                            budget (%llu total; the stall is delivered across the next\n"
                     "                            switches -- a held sample, never a hole)\n",
                     over, kDriftCorrectFr, (unsigned long long)L.wardenPads);
            }
        }
        if (L.padBudget && frameCount >= 2) {
            const size_t pad = (frameCount - 1 < L.padBudget)
                             ? (frameCount - 1) : L.padBudget;
            return pad;
        }
        return 0;
    }
    return 0;
}

// syncToFirst: every slave's UNDELAYED boundary to leg1's. The metric is
// delay-neutral (each leg subtracts its own delay); the corrections are
// bite-only; no baseline, no re-latch. A yielded or heart-silent leg1
// freezes the sync whole.
static size_t syncToFirst(int idx, Leg& L, size_t frameCount)
{
    const unsigned long long nowMs = GetTickCount64();
    if (nowMs - L.lastSyncMs < kSyncNudgeMs) return 0;
    L.lastSyncMs = nowMs;
    if (g_leg[0].yielded.load(std::memory_order_relaxed)) return 0;
    if (nowMs - hbOutGet(0) > kSyncRefDeadMs) return 0;
    const long long off =
        (long long)(g_leg[0].raw.seqTail.load(std::memory_order_relaxed)
                    + (long long)g_leg[0].delayFrames)
        - (long long)(L.raw.seqTail.load(std::memory_order_relaxed)
                      + (long long)L.delayFrames);
    const unsigned long long fsUs = L.firstSwitchUs.load(std::memory_order_relaxed);
    if (fsUs == 0 || nowMs - L.lastReopenMs < kSyncSettleMs) return 0;
    LARGE_INTEGER fsw;
    QueryPerformanceCounter(&fsw);
    const unsigned long long nowUs =
        (unsigned long long)((double)fsw.QuadPart * 1e6 / g_qpf);
    if (nowUs - fsUs < (unsigned long long)kSyncSettleMs * 1000ull) return 0;
    const long long err = off;   // + = this leg is LATE vs leg1
    if (err >= (long long)kDriftCorrectFr) {
        if (nowMs - L.lastBiteMs >= kDriftIntervalMs) {
            L.biteDue    = (size_t)kDriftCorrectFr;
            L.biteForced = (err >= (long long)(2 * kDriftCorrectFr));
            L.lastBiteMs = nowMs;
        }
        return 0;
    }
    if (err <= -(long long)kDriftCorrectFr) {
        if (L.padBudget == 0 && nowMs - L.lastBiteMs >= kDriftIntervalMs) {
            L.padBudget = (size_t)kDriftCorrectFr;
            L.wardenPads++;
            if (nowMs - L.lastSyncLogMs >= 10000) {
                L.lastSyncLogMs = nowMs;
                tlog("[twinspeak] sync out%d     : %lld fr early vs out1 -- the guard armed a %d-frame pad\n"
                     "                            budget (%llu total; the stall is delivered across the next\n"
                     "                            switches -- a held sample, never a hole)\n",
                     idx + 1, err, kDriftCorrectFr, (unsigned long long)L.wardenPads);
            }
        }
        if (L.padBudget && frameCount >= 2) {
            const size_t pad = (frameCount - 1 < L.padBudget)
                             ? (frameCount - 1) : L.padBudget;
            return pad;
        }
        return 0;
    }
    return 0;
}

// ==================================================== the buffer switch ====
static void bufferSwitchFill(int idx, float* out, size_t frameCount) {
    Leg& L = g_leg[idx];
    if (twsStopSilence) {
        if (frameCount) memset(out, 0, (size_t)frameCount * L.outCh * sizeof(float));
        return;
    }
    if (L.relandPending) {
        L.relandPending = false;
        landReopenedRingCore(idx);
    }
    if (L.popped.load(std::memory_order_relaxed) == 0 && g_qpf > 0.0) {
        LARGE_INTEGER fsw;
        QueryPerformanceCounter(&fsw);
        L.firstSwitchUs.store((unsigned long long)((double)fsw.QuadPart * 1e6 / g_qpf),
                              std::memory_order_release);
    }
    L.popped.fetch_add((unsigned long long)frameCount, std::memory_order_relaxed);

    if (frameCount)
        memset(out, 0, (size_t)frameCount * L.outCh * sizeof(float));
    float* sc = L.scratch.data();
    size_t done = 0;
    const bool pktOn = twsCfg.packetMode;
    size_t padFr = pktOn ? (size_t)0 : masterDepthGuard(L, frameCount);
    if (!pktOn && twsCfg.clockSync && idx != 0) {
        const size_t servoPad = syncToFirst(idx, L, frameCount);
        if (servoPad > padFr) padFr = servoPad;
    }
    static const unsigned char kLegMask[3][kInCh] = {
        { 1, 1, 0, 1, 0, 0, 0, 0 },   // leg1: ch1 ch2 (+ ch4 LFE fold)
        { 0, 0, 1, 0, 0, 0, 0, 0 },   // leg2: ch3 (+ ch4 if lfeFoldLeg==1)
        { 0, 0, 0, 0, 1, 1, 1, 1 },   // leg3: ch5 ch6 ch7 ch8
    };
    float* rs = L.pktRs.data();
    while (done + padFr < frameCount) {
        const size_t want = frameCount - padFr - done;
        const size_t avail = L.raw.count();
        bool engage = false;
        size_t n;
        if (pktOn) {
            if (done == 0) {
                if (L.pktDepthEma < 0.0) L.pktDepthEma = (double)avail;
                L.pktDepthEma += kPktEmaAlpha * ((double)avail - L.pktDepthEma);
                if (L.pktDepthEma2 < 0.0) L.pktDepthEma2 = (double)avail;
                L.pktDepthEma2 += kPktEmaSlowAlpha * ((double)avail - L.pktDepthEma2);
            }
            const long long perr = (long long)(L.pktDepthEma2 + 0.5)
                                 - (long long)L.delayFrames
                                 - (long long)L.anchorFrames - (long long)kPktFloorFr;
            long ptrim = 0;
            if      (perr >  kPktEngageFr) L.pktEngagedOn = true;
            else if (perr < -kPktEngageFr) L.pktEngagedOn = true;
            else if (perr <  kPktRelaxFr && perr > -kPktRelaxFr
                     && L.pktPiI > -1.0 && L.pktPiI < 1.0
                     && L.pktAppliedPpm > -0.5 && L.pktAppliedPpm < 0.5) {
                L.pktEngagedOn = false;
                L.pktPiI = 0.0;
                L.pktAppliedPpm = 0.0;
            }
            if (L.pktEngagedOn) {
                const double tgt = kPktPiKp * (double)perr
                                 + kPktPiKi * L.pktPiI;
                const double d = tgt - L.pktAppliedPpm;
                const double R = (d > kPktSlewThr || d < -kPktSlewThr)
                               ? kPktSlewFast : kPktSlewSlow;
                if      (d >  R) L.pktAppliedPpm += R;
                else if (d < -R) L.pktAppliedPpm -= R;
                else             L.pktAppliedPpm = tgt;
                L.pktPiI += (double)perr
                          + kPktAwKb * (L.pktAppliedPpm - tgt);
                if (L.pktPiI >  8000000.0) L.pktPiI =  8000000.0;
                if (L.pktPiI < -8000000.0) L.pktPiI = -8000000.0;
                ptrim = (long)(L.pktAppliedPpm + (L.pktAppliedPpm >= 0.0 ? 0.5 : -0.5));
                if (ptrim > kPktMaxPpm)  ptrim = kPktMaxPpm;
                if (ptrim < -kPktMaxPpm) ptrim = -kPktMaxPpm;
            }
            L.pktTrimPpm = ptrim;
            L.pktRatio = ((double)kSampleRate / (double)L.pktRate)
                       * (1.0 + L.pktAppliedPpm * 1e-6);
            engage = (L.pktRate != (unsigned)kSampleRate) || (ptrim != 0);
            if (engage) {
                const double ph0 = L.pktHaveHist ? L.pktPhase : 4.0;
                double need = floor(ph0 + (double)(want - 1) * L.pktRatio) + 3.0;
                size_t take = (size_t)(need + 0.999999);
                if (take < 1) take = 1;
                if (take > (size_t)kScratchFrames) take = (size_t)kScratchFrames;
                if (avail < take) break;
                if (avail >= L.delayFrames && (size_t)(avail - L.delayFrames) < take) break;
                n = take;
            } else {
                const size_t floor = L.delayFrames;
                const size_t usable = (avail > floor) ? (avail - floor) : 0;
                const size_t take = (want < (size_t)kScratchFrames) ? want
                                 : (size_t)kScratchFrames;
                n = (usable < take) ? usable : take;
                L.pktBypass++;
            }
        } else {
            const size_t floor = L.delayFrames;
            const size_t usable = (avail > floor) ? (avail - floor) : 0;
            const size_t take = (want < (size_t)kScratchFrames) ? want
                             : (size_t)kScratchFrames;
            n = (usable < take) ? usable : take;
        }
        if (n == 0) break;
        L.raw.pop(sc, n);
        {
            const float vg = g_volMuted.load(std::memory_order_relaxed) ? 0.0f
                           : g_vol.load(std::memory_order_relaxed);
            if (vg != 1.0f) {
                const size_t tot = n * (size_t)kInCh;
                for (size_t s2 = 0; s2 < tot; ++s2) sc[s2] *= vg;
            }
        }
        {
            const unsigned char* mk = kLegMask[idx];
            for (size_t i = 0; i < n; ++i)
                for (int c = 0; c < kInCh; ++c) {
                    unsigned char keep = mk[c];
                    if (idx == 0 && c == 2 && twsCenterFold) keep = 1;
                    if (idx == 0 && twsRearFold && (c == 6 || c == 7)) keep = 1;
                    if (idx == 0 && twsRearFold && (c == 4 || c == 5)) keep = 1;
                    if (idx == g_lfeFoldLeg && c == 3) keep = 1;
                    if (!keep) sc[i * kInCh + c] = 0.0f;
                }
        }
        if (pktOn && !engage && n >= 1) pktRetrack(L, sc, n);
        float* dst = out + done * L.outCh;
        const float* mixSrc = sc;
        size_t produced = n;
        size_t rawK = 0;
        if (pktOn && engage && n >= 4) {
            const size_t cap = (want < (size_t)kScratchFrames) ? want : (size_t)kScratchFrames;
            const size_t j = pktResample(L, sc, n, rs, cap);
            if (j > 0) {
                produced = j;
                mixSrc = rs;
                L.pktEngaged++;
            } else {
                L.pktBypass++;
                rawK = (n < want) ? n : want;
            }
        } else if (pktOn && engage) {
            L.pktBypass++;
            rawK = (n < want) ? n : want;
        }
        if (rawK) {
            produced = rawK;
            pktRetrack(L, sc, rawK);
        }
        mixBlock(idx, mixSrc, dst, produced);
        {
            size_t real = n;
            if (L.prefillRemaining) {
                const size_t p = real < L.prefillRemaining ? real : L.prefillRemaining;
                L.prefillRemaining -= p;
                real -= p;
            }
            if (real)
                L.raw.seqTailAdvance(L.raw.seqTail.load(std::memory_order_relaxed) + real);
        }
        if (L.biteDue) {
            const bool chunkQuiet = bitZero8(sc, n);
            if (chunkQuiet || L.biteForced) {
                const size_t drop = L.biteDue;
                L.raw.discardOldest(drop);
                L.biteDue = 0;
                L.biteForced = false;
                L.syncDrops++;
                L.wardenDrops++;
                L.depthDrops += drop;
                const unsigned long long dbn = GetTickCount64();
                if (dbn - L.lastSyncLogMs >= 10000) {
                    L.lastSyncLogMs = dbn;
                    tlog("[twinspeak] drift out%d    : %llu fr of drift dropped inside a silent window\n"
                         "                            (%llu total; a skip inside digital silence cannot click)\n",
                         idx + 1, (unsigned long long)drop, (unsigned long long)L.syncDrops);
                }
            }
        }
        done += produced;
    }
    const size_t padApplied = (padFr && done + padFr >= frameCount) ? padFr : 0;
    if (padApplied) {
        const float* holdSrc = out + (frameCount - padApplied - 1) * L.outCh;
        for (size_t i = 0; i < padApplied; ++i)
            memcpy(out + (frameCount - padApplied + i) * L.outCh, holdSrc,
                   L.outCh * sizeof(float));
        L.syncPads++;
        if (L.padBudget >= padApplied) L.padBudget -= padApplied;
        else                           L.padBudget = 0;
        const unsigned long long nowMs = GetTickCount64();
        if (nowMs - L.lastSyncLogMs >= 10000) {
            L.lastSyncLogMs = nowMs;
            tlog("[twinspeak] sync out%d     : running early vs out1 -- padded %d frames (%llu total; the warden\n"
                 "                            bite, %d fr / %u ms max, a held sample, never a hole)\n",
                 idx + 1, kDriftCorrectFr, (unsigned long long)L.syncPads, kDriftCorrectFr, kDriftIntervalMs);
        }
    }
    if (done + padApplied < frameCount) {
        const unsigned long long hb = g_hbCap.load(std::memory_order_relaxed);
        const bool flowing = hb && (GetTickCount64() - hb) <= 250;
        if (flowing) L.und.fetch_add(1);
    }
    if (g_qpf > 0.0) {
        const unsigned long long st = L.raw.seqTail.load(std::memory_order_relaxed);
        if (st) {
            LARGE_INTEGER c;
            QueryPerformanceCounter(&c);
            const double nowUs = (double)(c.QuadPart - g_qpcBoot.QuadPart) * 1e6 / g_qpf;
            const double capUs = (double)st * 1e6 / (double)kSampleRate;
            L.lagMs.store((nowUs - capUs) / 1000.0, std::memory_order_relaxed);
        }
    }
}

// ======================================================== sumLegExtras ====
// the universal keep-alive (20 Hz @ 0.1%, every leg, through every gate
// and every gap) + the chime (leg1 only). Runs AFTER the mix, on the
// mixed output, through the stop gap.
static void sumLegExtras(int idx, float* out, ma_uint32 frameCount) {
    if (idx == 0 && g_chimeBootWait.load(std::memory_order_acquire)) {
        g_chimeBootServed += frameCount;
        if (g_chimeBootServed >= kChimeBootFr) {
            g_chimeBootWait.store(false, std::memory_order_release);
            armChime(true);
        }
    }
    int chSel = (idx == 0) ? g_chimeSel.load(std::memory_order_acquire) : -1;
    const int och = 2;
    if (chSel < 0) {
        const unsigned len = g_kaLen;
        unsigned ka = g_kaIdxLeg[idx];
        ma_uint32 done = 0;
        while (done < frameCount) {
            const ma_uint32 run = (frameCount - done) < (ma_uint32)(len - ka)
                                ? (frameCount - done) : (ma_uint32)(len - ka);
            for (ma_uint32 i = 0; i < run; ++i, ++ka) {
                const float s = g_kaTable[ka];
                float l = out[(done + i) * och + 0] + s;
                float r = out[(done + i) * och + 1] + s;
                if (l >  1.0f) l =  1.0f; else if (l < -1.0f) l = -1.0f;
                if (r >  1.0f) r =  1.0f; else if (r < -1.0f) r = -1.0f;
                out[(done + i) * och + 0] = l;
                out[(done + i) * och + 1] = r;
            }
            done += run;
            if (ka >= len) ka -= len;
        }
        g_kaIdxLeg[idx] = ka;
        return;
    }
    for (ma_uint32 i = 0; i < frameCount; ++i) {
        float l = out[i * och + 0];
        float r = out[i * och + 1];
        const float s = g_kaTable[g_kaIdxLeg[idx]];
        if (++g_kaIdxLeg[idx] >= g_kaLen) g_kaIdxLeg[idx] = 0;
        l += s;
        r += s;
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
        out[i * och + 0] = l;
        out[i * och + 1] = r;
    }
}

// ========================================================== callbacks ====
static void landReopenedRingCore(int idx);   // fwd (defined below)
static bool pickDeviceCore(ma_context* ctx, ma_device_type type, const std::string& needle,
                           ma_device_id* outId, std::string* outName);   // fwd

static void capCallback(ma_device*, void*, const void* pInput, ma_uint32 frameCount) {
    boostDeviceThreadCore(0);
    g_hbCap.store(GetTickCount64(), std::memory_order_relaxed);
    if (!pInput || frameCount == 0) return;
    const float* in = (const float*)pInput;
    for (int i = 0; i < 4; ++i) {
        if (g_leg[i].disabled) continue;
        size_t dropped = g_leg[i].raw.pushOverwrite(in, frameCount);
        if (dropped) {
            g_leg[i].rawDrop.fetch_add(dropped);
            g_leg[i].raw.seqTailAdvance(g_leg[i].raw.seqTail.load(std::memory_order_relaxed)
                                        + dropped);
        }
        g_leg[i].captured.fetch_add(frameCount, std::memory_order_relaxed);
    }
}

static void playCore(int idx, float* out, ma_uint32 frameCount) {
    boostDeviceThreadCore(1 + idx);
    {
        Leg& L = g_leg[idx];
        LARGE_INTEGER qn;
        QueryPerformanceCounter(&qn);
        if (g_qpf > 0.0 && L.haveSwQpc) {
            const double periodMs = (double)frameCount * 1000.0 / (double)kSampleRate;
            const double gapMs    = (double)(qn.QuadPart - L.lastSwQpc.QuadPart) * 1000.0 / g_qpf;
            if (gapMs > periodMs + 1.0) {
                L.lateSwitches.fetch_add(1, std::memory_order_relaxed);
                const unsigned long long nowMs = GetTickCount64();
                if (nowMs - L.lastLateLogMs >= 10000) {
                    L.lastLateLogMs = nowMs;
                    tlog("[twinspeak] leg%d device    : switch served %.2f ms after the previous one (%.2f ms period)\n"
                         "                            -- %.2f ms of device-buffer margin left (%llu late switch(es) since boot)\n",
                         idx + 1, gapMs, periodMs, 2.0 * periodMs - gapMs,
                         (unsigned long long)L.lateSwitches.load(std::memory_order_relaxed));
                }
            }
        }
        L.lastSwQpc = qn;
        L.haveSwQpc = true;
    }
    hbOut(idx);
    bufferSwitchFill(idx, out, (size_t)frameCount);
}

static void playCallback1(ma_device*, void* pOutput, const void*, ma_uint32 frameCount) {
    playCore(0, (float*)pOutput, frameCount);
    sumLegExtras(0, (float*)pOutput, frameCount);
}
static void playCallback2(ma_device*, void* pOutput, const void*, ma_uint32 frameCount) {
    playCore(1, (float*)pOutput, frameCount);
    sumLegExtras(1, (float*)pOutput, frameCount);
}
static void playCallback3(ma_device*, void* pOutput, const void*, ma_uint32 frameCount) {
    playCore(2, (float*)pOutput, frameCount);
    sumLegExtras(2, (float*)pOutput, frameCount);
}
static void playCallback4(ma_device*, void* pOutput, const void*, ma_uint32 frameCount) {
    playCore(3, (float*)pOutput, frameCount);
    sumLegExtras(3, (float*)pOutput, frameCount);
}

// ======================================================== device open =====
static bool initLegDevice(int idx, ma_context* ctx, const ma_device_id* id,
                          ma_uint32 periodFrames, bool wantExcl) {
    Leg& L = g_leg[idx];
    ma_device_config pc = ma_device_config_init(ma_device_type_playback);
    pc.sampleRate = kSampleRate;
    pc.periodSizeInFrames = periodFrames;
    pc.periods = kOutPeriods;
    pc.playback.pDeviceID = id;
    pc.playback.format = ma_format_f32;
    pc.playback.channels = kOutCh;
    pc.playback.shareMode = wantExcl ? ma_share_mode_exclusive : ma_share_mode_shared;
    pc.dataCallback = (idx == 0) ? playCallback1 : (idx == 1) ? playCallback2
                   : (idx == 2) ? playCallback3 : playCallback4;
    pc.wasapi.usage = ma_wasapi_usage_pro_audio;
    ma_result r = ma_device_init(ctx, &pc, &L.dev);
    if (r != MA_SUCCESS) {
        tlog( "[twinspeak] leg%d: %s init failed: %s\n", idx + 1,
                wantExcl ? "EXCLUSIVE" : "shared", ma_result_description(r));
        return false;
    }
    if (twsCfg.packetMode) {
        const ma_uint32 got = L.dev.playback.internalSampleRate
                            ? L.dev.playback.internalSampleRate : (ma_uint32)kSampleRate;
        if (got != (ma_uint32)kSampleRate) {
            ma_device_uninit(&L.dev);
            memset(&L.dev, 0, sizeof(L.dev));
            pc.sampleRate = got;
            r = ma_device_init(ctx, &pc, &L.dev);
            if (r != MA_SUCCESS) {
                pc.sampleRate = kSampleRate;
                r = ma_device_init(ctx, &pc, &L.dev);
                if (r != MA_SUCCESS) {
                    tlog( "[twinspeak] leg%d: native-rate (%u Hz) AND 48k re-init both failed: %s\n",
                            idx + 1, (unsigned)got, ma_result_description(r));
                    return false;
                }
                tlog( "[twinspeak] leg%d device    : native-rate open refused -- staying at 48k (miniaudio converts)\n",
                        idx + 1);
            }
        }
    }
    L.pktRate     = L.dev.playback.internalSampleRate
                  ? L.dev.playback.internalSampleRate : (ma_uint32)kSampleRate;
    L.pktRatio    = (double)kSampleRate / (double)L.pktRate;
    L.pktTrimPpm  = 0;
    L.pktHaveHist = false;
    L.pktPhase    = 0.0;
    L.pktEngaged  = 0;
    L.pktBypass   = 0;
    L.pktLastTrimLogMs = 0;
    L.pktDepthEma  = -1.0;
    L.pktDepthEma2 = -1.0;
    L.pktPiI       = 0.0;
    L.pktAppliedPpm = 0.0;
    L.isExclusive = wantExcl;
    L.id = *id;
    return true;
}

// the deferred reopen landing: the new device thread's first switch
// discards the refill-bloated backlog so the ring opens at the anchor.
static void landReopenedRingCore(int idx) {
    Leg& L = g_leg[idx];
    const long long have = (long long)L.raw.count();
    const long long rMaster = (long long)g_leg[0].raw.count()
                            - (long long)g_leg[0].delayFrames;
    long long want = ((idx != 0) ? (rMaster + (long long)L.delayFrames)
                                 : ((long long)L.delayFrames + (long long)L.anchorFrames));
    if (want < (long long)L.delayFrames + (long long)L.anchorFrames)
        want = (long long)L.delayFrames + (long long)L.anchorFrames;
    if (have > want)
        L.raw.discardOldest((size_t)(have - want));
}

static bool tryRestartLeg(int idx, ma_context* ctx, ma_uint32 periodFrames,
                          const char* why = "device thread silent > 5 s") {
    Leg& L = g_leg[idx];
    if (L.disabled) return false;
    static unsigned long long lastLog[4] = { 0, 0, 0, 0 };
    const unsigned long long now = GetTickCount64();
    L.lastReopenMs = now;
    if (now - lastLog[idx] >= 10000) {
        tlog( "[twinspeak] leg%d WATCHDOG : %s -- reopening \"%s\"\n",
                idx + 1, why, L.name.c_str());
        lastLog[idx] = now;
    }
    const char* needle = legNeedle(idx);
    if (L.dev.pContext) ma_device_uninit(&L.dev);
    memset(&L.dev, 0, sizeof(L.dev));
    L.relandPending = true;
    ma_device_id id; std::string nm;
    if (!pickDeviceCore(ctx, ma_device_type_playback, needle, &id, &nm)) return false;
    bool ok = initLegDevice(idx, ctx, &id, periodFrames, L.isExclusive);
    if (!ok && L.isExclusive) ok = initLegDevice(idx, ctx, &id, periodFrames, false);
    if (!ok) return false;
    if (ma_device_start(&L.dev) != MA_SUCCESS) return false;
    L.name = nm;
    hbOut(idx);
    g_driftRecalReq.store(true, std::memory_order_relaxed);
    g_periodFrames[idx] = L.dev.playback.internalPeriodSizeInFrames
                        ? (size_t)L.dev.playback.internalPeriodSizeInFrames
                        : g_periodFrames[idx];
    if (twsCfg.pinOutputs) {
        if (g_pin[idx].vol) { g_pin[idx].vol->Release(); g_pin[idx].vol = nullptr; }
        g_pin[idx].id = L.id.wasapi;
        if (L.id.wasapi[0]) {
            if (isDefaultRenderEndpointId(L.id.wasapi)) {
                tlog( "[twinspeak] out%d: endpoint re-acquired after reopen -- IS the default playback device, volume left alone\n", idx + 1);
            } else {
                g_pin[idx].vol = endpointVolFromIdString(L.id.wasapi);
                if (g_pin[idx].vol) {
                    g_pin[idx].vol->SetMasterVolumeLevelScalar(1.0f, nullptr);
                    g_pin[idx].vol->SetMute(FALSE, nullptr);
                    tlog( "[twinspeak] out%d: endpoint re-acquired after reopen -- pinned 100%%\n", idx + 1);
                }
            }
        }
    }
    tlog( "[twinspeak] leg%d WATCHDOG : reopened \"%s\" (%s)\n",
            idx + 1, nm.c_str(), L.isExclusive ? "exclusive" : "shared");
    if (idx == 1) armChime(false);
    return true;
}

static ma_device g_capDev = {};
static std::string g_capNeedle;
static bool    g_capLoopback = true;   // the ONE capture path: render loopback
static float   g_capPeriodMsF = 4.0f;

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
    if (!pickDeviceCore(ctx, ma_device_type_playback, g_capNeedle, &id, &nm)) return false;
    ma_device_config cc = ma_device_config_init(ma_device_type_loopback);
    cc.sampleRate = kSampleRate;
    cc.periodSizeInMilliseconds = (ma_uint32)(g_capPeriodMsF + 0.5f);
    cc.periods = 2;
    cc.dataCallback = capCallback;
    cc.capture.pDeviceID = &id;
    cc.capture.format = ma_format_f32;
    cc.capture.channels = kInCh;
    cc.capture.shareMode = ma_share_mode_shared;
    cc.wasapi.usage = ma_wasapi_usage_pro_audio;
    ma_result r = ma_device_init(ctx, &cc, &g_capDev);
    if (r != MA_SUCCESS) return false;
    if (ma_device_start(&g_capDev) != MA_SUCCESS) return false;
    return true;
}

// ======================================================== enumeration ====
void twsListDevices() {
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

void twsEnumDevices(std::vector<std::string>& playback, std::vector<std::string>& capture) {
    playback.clear(); capture.clear();
    ma_backend be[] = { ma_backend_wasapi };
    ma_context ctx;
    if (ma_context_init(be, 1, nullptr, &ctx) != MA_SUCCESS) return;
    ma_device_info* p = nullptr; ma_uint32 np = 0;
    ma_device_info* c = nullptr; ma_uint32 nc = 0;
    if (ma_context_get_devices(&ctx, &p, &np, &c, &nc) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < np; ++i) playback.push_back(p[i].name);
        for (ma_uint32 i = 0; i < nc; ++i) capture.push_back(c[i].name);
    }
    ma_context_uninit(&ctx);
}

static bool pickDeviceCore(ma_context* ctx, ma_device_type type, const std::string& needle,
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

static std::string wideToNarrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_ACP, 0, &w[0], (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_ACP, 0, &w[0], (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static bool defaultRenderName(std::string* outName) {
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                kIID_IMMDeviceEnumerator, (void**)&en)) || !en)
        return false;
    bool ok = false;
    IMMDevice* def = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &def)) && def) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(def->GetId(&id)) && id) {
            IPropertyStore* ps = nullptr;
            if (SUCCEEDED(def->OpenPropertyStore(STGM_READ, &ps)) && ps) {
                PROPVARIANT var; PropVariantInit(&var);
                if (SUCCEEDED(ps->GetValue(kPKEY_Device_FriendlyName, &var)) &&
                    var.vt == VT_LPWSTR && var.pwszVal)
                    *outName = wideToNarrow(var.pwszVal), ok = true;
                PropVariantClear(&var);
                ps->Release();
            }
            CoTaskMemFree(id);
        }
        def->Release();
    }
    en->Release();
    return ok;
}

// ============================================================ tick test ====
// 48 kHz, 8ch, 16-bit PCM, 1500 Hz burst, 7.5 ms, 120 bpm, 8 beats on
// ch1-4 (FL/FR/FC/LFE). LOUD: 0.95 peak (the old 0.3 was too quiet to
// judge alignment by ear across devices).
static constexpr size_t kTickPeriod = 24000;   // 500 ms @ 48 kHz = 120 bpm
static constexpr size_t kTickCount  = 8;       // 8 beats = 4 s, then it stops

std::string twsTickWavPath(const char* iniDir) {
    return std::string(iniDir && *iniDir ? iniDir : ".") + "\\twinspeak-tick.wav";
}

bool twsTickWavWrite(const std::string& path) {
    const size_t nBurst = 360;                     // 7.5 ms @ 48 kHz
    const size_t nTail  = 4800;                     // 100 ms of silence after the last beat
    const size_t nFrames = kTickCount * kTickPeriod + nTail;
    const size_t nCh     = 8;                       // the capture cable's own width
    const unsigned dataBytes = (unsigned)(nFrames * nCh * 2);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    unsigned char hdr[44];
    auto wr32 = [&](unsigned off, unsigned v) { for (int b = 0; b < 4; ++b) hdr[off + b] = (unsigned char)((v >> (8 * b)) & 0xFF); };
    auto wr16 = [&](unsigned off, unsigned v) { for (int b = 0; b < 2; ++b) hdr[off + b] = (unsigned char)((v >> (8 * b)) & 0xFF); };
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr +  0, "RIFF", 4);  wr32(4, 36 + dataBytes);
    memcpy(hdr +  8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);  wr32(16, 16);
    wr16(20, 1);                    // PCM
    wr16(22, (unsigned)nCh);        // 8 channels
    wr32(24, kSampleRate);          // 48000
    wr32(28, (unsigned)(kSampleRate * nCh * 2));  // byte rate
    wr16(32, (unsigned)(nCh * 2));  // block align
    wr16(34, 16);                   // bits per sample
    memcpy(hdr + 36, "data", 4);  wr32(40, dataBytes);
    fwrite(hdr, 1, sizeof(hdr), f);
    std::vector<short> row(nCh, 0);
    for (size_t fr = 0; fr < nFrames; ++fr) {
        const size_t ph = fr % kTickPeriod;
        const double s  = (ph < nBurst)
            ? 0.95 * sin(kTwoPi * 1500.0 * (double)ph / (double)kSampleRate)
                     * exp(-(double)ph / (double)kSampleRate / 0.002)
            : 0.0;
        const short pcm = (short)(s > 0.0 ? s * 32767.0 + 0.5 : s * 32767.0 - 0.5);
        for (int c = 0; c < 4; ++c) row[c] = pcm;
        for (int c = 4; c < (int)nCh; ++c) row[c] = 0;
        fwrite(row.data(), sizeof(short) * nCh, 1, f);
    }
    fclose(f);
    return true;
}

// ====================================================== the engine body ====
static int engineMainCore(const char* iniPath, bool fgForce, bool guiMode) {
    twsRun = true;
    twsStopSilence = false;
    if (!iniPath || !*iniPath) {
        tlog("[twinspeak] no config path\n");
        return 1;
    }

    {
        HANDLE tok = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
            TOKEN_PRIVILEGES tp;
            if (LookupPrivilegeValueW(nullptr, L"SeIncreaseBasePriorityPrivilege",
                                      &tp.Privileges[0].Luid)) {
                tp.PrivilegeCount = 1;
                tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr);
            }
            CloseHandle(tok);
        }
    }
    if (SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
        tlog("[twinspeak] priority      : REALTIME process class -- the engine's device threads sit at\n"
             "                            the scheduler's top band against anything user-mode.\n");
    } else {
        const DWORD pclErr = GetLastError();
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        tlog("[twinspeak] priority      : REALTIME class refused (err %lu) -- HIGH engaged. Launch elevated\n"
             "                            once for the full realtime band; MMCSS still orders the audio\n"
             "                            threads inside it.\n", pclErr);
    }

    timeBeginPeriod(1);
    g_bootMs = GetTickCount64();
    {
        LARGE_INTEGER f, c;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&c);
        g_qpf = (double)f.QuadPart;
        g_qpcBoot = c;
    }
    cpuTopoProbe();

    // the keep-alive table
    g_kaHz = kKeepAliveHz;
    g_kaGain = kKeepAliveGain;
    g_kaLen = (unsigned)((double)kSampleRate / (double)g_kaHz + 0.5);
    if (g_kaLen < 8) g_kaLen = 8;
    if (g_kaLen > kKeepAliveTableMax) g_kaLen = kKeepAliveTableMax;
    for (unsigned i = 0; i < g_kaLen; ++i)
        g_kaTable[i] = (float)(sin(kTwoPi * (double)g_kaHz * (double)i / (double)kSampleRate)
                               * (double)g_kaGain);
    for (unsigned i = 0; i < g_kaLen; ++i)
        if (g_kaTable[i] == 0.0f) g_kaTable[i] = 1.0f / 32768.0f;

    mixCrewInit();

    {
        float cg = twsCfg.chimeGain;
        if (cg < 0.0f) cg = 0.0f;
        if (cg > 1.0f) cg = 1.0f;
        g_chimeBuf = renderChime(2093.00, 42.0, 3135.97, 72.0, 20.0, cg);
    }

    if (twsCfg.inputDevice.empty() || twsCfg.outputDevice1.empty()) {
        tlog( "[twinspeak] config must set input_device and output_device1\n"
              "            (output_device2/3/4 may stay EMPTY: empty leg2 folds the center onto\n"
              "            the mains, empty leg3 folds the rears onto the mains)\n");
        return 1;
    }

    ma_backend be[] = { ma_backend_wasapi };
    ma_context ctx;
    if (ma_context_init(be, 1, nullptr, &ctx) != MA_SUCCESS) {
        tlog( "[twinspeak] WASAPI context init failed\n");
        return 1;
    }

    // THE SOURCE: the specified OUTPUT device (render loopback, always)
    ma_device_id capId, id1, id2, id3;
    std::string capName, n1, n2, n3;
    g_capLoopback = true;
    if (!pickDeviceCore(&ctx, ma_device_type_playback, twsCfg.inputDevice, &capId, &capName)) {
        tlog( "[twinspeak] the source device matching \"%s\" not found (--list to see names)\n",
                twsCfg.inputDevice.c_str());
        return 1;
    }
    tlog("[twinspeak] source        : OUTPUT-AS-SOURCE -- \"%s\" is a render endpoint; its data is\n"
         "                            read (one-way loopback; nothing is written back -- no echo) and\n"
         "                            routed to the legs by channel\n", capName.c_str());

    if (!pickDeviceCore(&ctx, ma_device_type_playback, twsCfg.outputDevice1, &id1, &n1)) {
        tlog( "[twinspeak] output_device1 matching \"%s\" not found (--list)\n",
                twsCfg.outputDevice1.c_str());
        return 1;
    }

    // leg2 (center): EMPTY -> the center folds onto leg1's FL/FR
    g_leg[1].disabled = twsCfg.outputDevice2.empty();
    twsCenterFold     = g_leg[1].disabled;
    if (!g_leg[1].disabled &&
        !pickDeviceCore(&ctx, ma_device_type_playback, twsCfg.outputDevice2, &id2, &n2)) {
        tlog( "[twinspeak] output_device2 matching \"%s\" not found (--list)\n",
                twsCfg.outputDevice2.c_str());
        return 1;
    }
    if (g_leg[1].disabled)
        tlog("[twinspeak] leg2 (center)  : output_device2 is EMPTY -- no center device, no error;\n"
             "                            the center (ch3) FOLDS onto leg1's FL/FR at gain_center\n");

    // leg3 (rears): EMPTY -> the rears fold onto leg1 (the MAINS)
    g_leg[2].disabled = twsCfg.outputDevice3.empty();
    g_leg[3].disabled = true;   // leg4 NEVER opens a device of its own
    twsRearFold       = g_leg[2].disabled;
    if (!g_leg[2].disabled &&
        !pickDeviceCore(&ctx, ma_device_type_playback, twsCfg.outputDevice3, &id3, &n3)) {
        tlog( "[twinspeak] output_device3 matching \"%s\" not found (--list)\n",
                twsCfg.outputDevice3.c_str());
        return 1;
    }
    if (g_leg[2].disabled)
        tlog("[twinspeak] leg3 (rears)   : output_device3 is EMPTY -- the rears FOLD onto the mains\n"
             "                            (leg1) at gain_rl/gain_rr; no rear device, no error\n");

    // echo-loop guard: the source must not be any leg's device
    {
        if (memcmp(&capId, &id1, sizeof(ma_device_id)) == 0) {
            tlog( "[twinspeak] FATAL       : the source endpoint IS leg1's device -- routing a leg\n"
                    "                            back into itself is an echo loop; refused. Point input_device\n"
                    "                    at the cable's endpoint and output_device1 at the speakers.\n");
            return 1;
        }
        if (!g_leg[1].disabled && memcmp(&capId, &id2, sizeof(ma_device_id)) == 0) {
            tlog( "[twinspeak] FATAL       : the source endpoint IS leg2's device -- echo loop; refused\n");
            return 1;
        }
        if (!g_leg[2].disabled && memcmp(&capId, &id3, sizeof(ma_device_id)) == 0) {
            tlog( "[twinspeak] FATAL       : the source endpoint IS leg3's device -- echo loop; refused\n");
            return 1;
        }
    }

    // leg4 (LFE fold): the needle must name an EXISTING leg; that leg
    // plays gain_lfe * ch4 (LFE) on its own device. NO new device opens.
    g_lfeFoldLeg = -1;
    if (!twsCfg.outputDevice4.empty()) {
        ma_device_id id4; std::string n4;
        if (!pickDeviceCore(&ctx, ma_device_type_playback, twsCfg.outputDevice4, &id4, &n4)) {
            tlog("[twinspeak] leg4 (LFE fold) : output_device4 matching \"%s\" not found -- leg4 stays idle\n"
                 "                            (a soft note, never an error; --list to see names)\n",
                 twsCfg.outputDevice4.c_str());
        } else if (memcmp(&id4, &id1, sizeof(ma_device_id)) == 0) {
            g_lfeFoldLeg = 0;
            tlog("[twinspeak] leg4 (LFE fold) : the device is leg1 (the mains) -- the LFE already folds there; idle\n");
        } else if (!g_leg[1].disabled && memcmp(&id4, &id2, sizeof(ma_device_id)) == 0) {
            g_lfeFoldLeg = 1;
            tlog("[twinspeak] leg4 (LFE fold) : folding the LFE (ch4) onto leg2's L/R at gain_lfe -- \"%s\"\n", n4.c_str());
        } else if (!g_leg[2].disabled && memcmp(&id4, &id3, sizeof(ma_device_id)) == 0) {
            g_lfeFoldLeg = 2;
            tlog("[twinspeak] leg4 (LFE fold) : folding the LFE (ch4) onto leg3's L/R at gain_lfe -- \"%s\"\n", n4.c_str());
        } else {
            tlog("[twinspeak] leg4 (LFE fold) : \"%s\" is not playing on any leg -- idle (the fold targets\n"
                 "                            EXISTING legs only; no second stream is opened for it)\n", n4.c_str());
        }
    }

    const ma_uint32 capPeriodMs = 4;
    g_capPeriodMsF = 4.0f;
    g_capNeedle     = twsCfg.inputDevice;

    for (int i = 0; i < 4; ++i) {
        Leg& L = g_leg[i];
        L.index = i;
        L.outCh = kOutCh;
        L.name  = (i == 0) ? n1 : (i == 1) ? n2 : (i == 2) ? n3 : std::string();
        if (L.disabled) continue;
        L.scratch.assign((size_t)kInCh * kScratchFrames, 0.0f);
        L.pktRs.assign((size_t)kInCh * kScratchFrames, 0.0f);
    }

    // boot yield: a leg on the Windows default render device (or with
    // foreign ACTIVE sessions) starts RELEASED; the health loop re-grabs.
    bool bootYield[4] = { false, false, false, false };
    {
        std::string defNm;
        const bool haveDef = defaultRenderName(&defNm);
        for (int i = 0; i < 4; ++i) {
            if (g_leg[i].disabled) continue;
            const char* yn = legNeedle(i);
            if (!yn[0]) continue;
            if (haveDef && containsCI(defNm, yn)) {
                bootYield[i] = true;
                g_leg[i].yielded.store(true, std::memory_order_relaxed);
                g_leg[i].isExclusive = true;
                g_leg[i].lastReopenMs = GetTickCount64();
                tlog("[twinspeak] leg%d BOOT-YIELD : the Windows default render device (\"%s\") matches\n"
                     "                            this leg's needle -- starting the leg RELEASED (the DAC is\n"
                     "                            never touched at boot); it reopens when the default moves\n"
                     "                            away and the device is quiet\n",
                     i + 1, defNm.c_str());
                continue;
            }
            const int busy = foreignActiveSessions(yn);
            if (busy > 0) {
                bootYield[i] = true;
                g_leg[i].yielded.store(true, std::memory_order_relaxed);
                g_leg[i].isExclusive = true;
                g_leg[i].lastReopenMs = GetTickCount64();
                tlog("[twinspeak] leg%d BOOT-YIELD : %d foreign audio session(s) ACTIVE on this leg's device\n"
                     "                            (not the default) -- starting the leg RELEASED so those live\n"
                     "                            streams are not invalidated; it reopens when they finish\n",
                     i + 1, busy);
            } else if (busy < 0) {
                tlog("[twinspeak] leg%d BOOT-YIELD : session probe unavailable -- opening normally (fail-open)\n", i + 1);
            }
        }
    }
    const int openOrder[4] = { 1, 0, 2, 3 };
    auto legIdPtr = [&](int i) -> ma_device_id* {
        return (i == 0) ? &id1 : (i == 1) ? &id2 : &id3;
    };
    bool okLeg[4] = { false, false, false, false };
    for (int oi = 0; oi < 4; ++oi) {
        const int i = openOrder[oi];
        if (g_leg[i].disabled) { okLeg[i] = true; continue; }
        if (bootYield[i])      { okLeg[i] = true; continue; }
        okLeg[i] = initLegDevice(i, &ctx, legIdPtr(i), kOutPeriodFrames, true);
    }
    {
        bool anyFail = false;
        for (int i = 0; i < 4; ++i)
            if (!okLeg[i] && !bootYield[i] && !g_leg[i].disabled) anyFail = true;
        if (anyFail) {
            tlog( "[twinspeak] align_modes: exclusive refused on a leg -- reopening the WASAPI legs shared\n");
            for (int i = 0; i < 4; ++i) {
                if (g_leg[i].disabled || bootYield[i]) continue;
                if (okLeg[i]) {
                    ma_device_uninit(&g_leg[i].dev);
                    memset(&g_leg[i].dev, 0, sizeof(g_leg[i].dev));
                    okLeg[i] = false;
                }
                if (!okLeg[i]) okLeg[i] = initLegDevice(i, &ctx, legIdPtr(i), kOutPeriodFrames, false);
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (okLeg[i] || bootYield[i] || g_leg[i].disabled) continue;
        tlog( "[twinspeak] giving up -- a playback device could not be opened in any mode\n");
        return 1;
    }

    // THE CAPTURE: loopback on the render endpoint, always
    ma_device_config cc = ma_device_config_init(ma_device_type_loopback);
    cc.sampleRate = kSampleRate;
    cc.periodSizeInMilliseconds = capPeriodMs;
    cc.periods = 2;
    cc.dataCallback = capCallback;
    cc.capture.pDeviceID = &capId;
    cc.capture.format = ma_format_f32;
    cc.capture.channels = kInCh;
    cc.capture.shareMode = ma_share_mode_shared;
    cc.wasapi.usage = ma_wasapi_usage_pro_audio;
    memset(&g_capDev, 0, sizeof(g_capDev));
    ma_result cr = ma_device_init(&ctx, &cc, &g_capDev);
    if (cr != MA_SUCCESS) {
        tlog( "[twinspeak] output-as-source open FAILED: %s -- the render endpoint refused the\n"
                "                            loopback read (busy format change, endpoint disabled?); check --list\n",
                ma_result_description(cr));
        return 1;
    }

    for (int i = 0; i < 4; ++i) {
        if (g_leg[i].disabled) { g_periodFrames[i] = 0; continue; }
        g_periodFrames[i] = g_leg[i].dev.playback.internalPeriodSizeInFrames
                          ? (size_t)g_leg[i].dev.playback.internalPeriodSizeInFrames
                          : (size_t)kOutPeriodFrames;
    }
    g_capPeriodFrames = g_capDev.capture.internalPeriodSizeInFrames
                      ? (size_t)g_capDev.capture.internalPeriodSizeInFrames
                      : (size_t)(capPeriodMs * (kSampleRate / 1000));
    if (g_capPeriodFrames == 0) g_capPeriodFrames = 192;

    // EVERY LEG, THE SAME WAY (the consistent flow): delay prefill,
    // device-buffer anchor, fresh counters.
    for (int i = 0; i < 4; ++i) {
        Leg& L = g_leg[i];
        if (L.disabled) continue;
        const float delayMs = (i == 0) ? twsCfg.delay1Ms
                          : (i == 1) ? twsCfg.delay2Ms
                          : twsCfg.offset3Ms;
        const size_t devPeriodFr  = (L.dev.playback.internalPeriodSizeInFrames
                                     ? L.dev.playback.internalPeriodSizeInFrames : kOutPeriodFrames);
        L.delayFrames = (size_t)((double)kSampleRate * delayMs / 1000.0);
        L.delayMs     = delayMs;
        L.anchorFrames = devPeriodFr + devPeriodFr / 2;
        L.raw.init(L.delayFrames + (size_t)(kSampleRate / 1000 * 200), kInCh);
        L.raw.pushSilence(L.delayFrames);
        L.prefillRemaining = L.delayFrames;
        L.captured.store(0, std::memory_order_relaxed);
        L.popped.store(0, std::memory_order_relaxed);
        L.rawDrop.store(0, std::memory_order_relaxed);
        L.und.store(0, std::memory_order_relaxed);
        L.firstSwitchUs.store(0, std::memory_order_relaxed);
        L.relandPending = false;
        L.syncDrops = 0;
        L.syncPads  = 0;
        L.lastBiteMs   = 0;
        L.wardenDrops  = 0;
        L.wardenPads   = 0;
        L.depthDrops   = 0;
        L.padBudget    = 0;
        L.biteDue      = 0;
        L.biteForced   = false;
        L.pktDepthEma  = -1.0;
        L.pktEngagedOn = false;
        L.pktDepthEma2 = -1.0;
        L.pktPiI       = 0.0;
        L.pktAppliedPpm = 0.0;
        L.lateSwitches.store(0, std::memory_order_relaxed);
    }

    const bool comOK = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    if (twsCfg.volumeFollow) {
        std::string volNeedles[2];
        int nN = 0;
        volNeedles[nN++] = twsCfg.inputDevice;
        volNeedles[nN++] = "virtual audio cable";
        for (int k = 0; k < nN && !g_srcVol; ++k) {
            bool dup = false;
            for (int j = 0; j < k; ++j)
                if (volNeedles[j] == volNeedles[k]) dup = true;
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
        }
    }
    if (twsCfg.pinOutputs) {
        for (int i = 0; i < 4; ++i) {
            if (g_leg[i].disabled) continue;
            if (g_leg[i].yielded.load(std::memory_order_relaxed)) {
                tlog("[twinspeak] out%d: leg RELEASED (boot yield) -- endpoint pin not applicable until it reopens\n", i + 1);
                continue;
            }
            if (isDefaultRenderEndpointId(g_leg[i].id.wasapi)) {
                tlog("[twinspeak] out%d: endpoint IS the default playback device -- volume left alone (no 100%% pin)\n", i + 1);
                continue;
            }
            g_pin[i].id = g_leg[i].id.wasapi;
            g_pin[i].vol = endpointVolFromIdString(g_leg[i].id.wasapi);
            if (g_pin[i].vol) {
                g_pin[i].vol->SetMasterVolumeLevelScalar(1.0f, nullptr);
                g_pin[i].vol->SetMute(FALSE, nullptr);
                tlog("[twinspeak] out%d: endpoint pinned 100%%/unmuted\n", i + 1);
            }
        }
    }

    // start trim: drain any pre-playback backlog past the anchor
    for (int li = 0; li < 4; ++li) {
        Leg& SL = g_leg[li];
        if (!SL.raw.cap) continue;
        const long long level = (long long)SL.raw.count();
        if (level > (long long)(SL.delayFrames + SL.anchorFrames)) {
            const size_t dropped =
                SL.raw.discardOldest((size_t)(level - (long long)SL.delayFrames - (long long)SL.anchorFrames));
            tlog("[twinspeak] leg%d sync     : start trim: dropped %llu fr of pre-playback backlog -- the first buffer opens at the delay anchor\n",
                 li + 1, (unsigned long long)dropped);
        }
    }

    if (!g_leg[1].disabled) {
        g_chimeBootWait.store(true, std::memory_order_release);
        g_chimeBootServed = 0;
    }

    {
        ma_result sAll = MA_SUCCESS;
        for (int oi = 0; oi < 4; ++oi) {
            const int i = openOrder[oi];
            Leg& L = g_leg[i];
            if (L.disabled || L.yielded.load(std::memory_order_relaxed)) continue;
            if (ma_device_start(&L.dev) != MA_SUCCESS) sAll = MA_ERROR;
        }
        if (sAll != MA_SUCCESS) {
            tlog( "[twinspeak] playback start failed\n");
            return 1;
        }
    }

    // start align: every leg opens its stream on the same instant
    if (twsCfg.clockSync) {
        const unsigned long long alignT0 = GetTickCount64();
        for (;;) {
            bool all = true;
            for (int i = 0; i < 4; ++i) {
                if (g_leg[i].disabled) continue;
                if (g_leg[i].yielded.load(std::memory_order_relaxed)) continue;
                if (g_leg[i].popped.load(std::memory_order_acquire) == 0) { all = false; break; }
            }
            if (all) break;
            if (GetTickCount64() - alignT0 > 250) break;
            Sleep(1);
        }
        unsigned long long fs[4] = { 0, 0, 0, 0 };
        int nPart = 0;
        bool missing = false;
        for (int i = 0; i < 4; ++i) {
            if (g_leg[i].disabled) continue;
            if (g_leg[i].yielded.load(std::memory_order_relaxed)) continue;
            fs[i] = g_leg[i].firstSwitchUs.load(std::memory_order_acquire);
            if (fs[i]) ++nPart; else missing = true;
        }
        if (missing || nPart < 2) {
            tlog("[twinspeak] start align  : a leg never handed in its first buffer switch (dead or released) --\n"
                 "                            alignment skipped (the watchdog owns a dead leg; a released leg\n"
                 "                            aligns itself on its quiet reopen)\n");
        } else {
            int ref = -1;
            for (int i = 0; i < 4; ++i)
                if (fs[i] && (ref < 0 || fs[i] < fs[ref])) ref = i;
            for (int i = 0; i < 4; ++i) {
                if (i == ref || !fs[i]) continue;
                const long long gapUs = (long long)fs[i] - (long long)fs[ref];
                if (gapUs <= 0) continue;
                long long gapFr = (gapUs * (long long)kSampleRate + 500000) / 1000000;
                Leg& T = g_leg[i];
                bool clipped = false;
                const long long excess = (long long)T.raw.count()
                                       - (long long)g_leg[ref].raw.count();
                if (gapFr > excess) { gapFr = excess; clipped = true; }
                if (gapFr > 0) {
                    T.raw.discardOldest((size_t)gapFr);
                    tlog("[twinspeak] start align  : leg%d began eating %lld us after leg%d -- skipped %lld fr\n"
                         "                            (%.2f ms)%s of its deeper backlog so every leg opens its\n"
                         "                            stream on the same instant (bit-exact, ledger-true)\n",
                         i + 1, gapUs, ref + 1, gapFr,
                         (double)gapFr * 1000.0 / (double)kSampleRate,
                         clipped ? " (clipped to the available excess)" : "");
                }
            }
        }
    }

    HANDLE hWatch = (twsCfg.volumeFollow || twsCfg.pinOutputs)
                  ? CreateThread(nullptr, 0, volumeWatchThread, nullptr, 0, nullptr)
                  : nullptr;
    HANDLE hPower = CreateThread(nullptr, 0, powerThread, nullptr, 0, nullptr);
    (void)hPower;

    if (!guiMode && twsCfg.background && !fgForce) {
        size_t ds = 0;
        const char* p = iniPath;
        for (const char* q = p; *q; ++q) if (*q == '\\' || *q == '/') ds = (size_t)(q - p);
        std::string logPath = (ds == 0)
                            ? std::string("twinspeak.log")
                            : std::string(p, ds + 1) + "twinspeak.log";
        tlog("[twinspeak] background    : on -- logging to %s; console detaches now.\n"
               "            stop with:  taskkill /IM twinspeak.exe   (--foreground keeps the console for one run)\n",
               logPath.c_str());
        fflush(stdout);
        FILE* lf = freopen(logPath.c_str(), "w", stdout);
        if (lf) {
            g_logToFile = true;
            freopen(logPath.c_str(), "a", stderr);
            FreeConsole();
        } else {
            tlog("[twinspeak] background    : could not open the log file -- staying in the foreground\n");
        }
    } else if (!guiMode && twsCfg.background && fgForce) {
        tlog("[twinspeak] background    : on in the ini, overridden by --foreground -- keeping the console\n");
    }

    // the banner: the simple four-leg flow, as it actually runs
    tlog("[twinspeak] v%s         : the simple audio pipeline -- capture from the specified output\n"
         "            device, fan out to the legs, every leg the same flow. 4 legs:\n", TWS_VERSION);
    tlog("[twinspeak] leg1 (mains)  : \"%s\" -- FL/FR + the LFE fold on L/R\n", n1.c_str());
    if (g_leg[1].disabled)
        tlog("[twinspeak] leg2 (center)  : EMPTY -- the center (ch3) folds onto leg1 at gain_center\n");
    else
        tlog("[twinspeak] leg2 (center)  : \"%s\" -- ch3 on L/R, filling\n", n2.c_str());
    if (g_leg[2].disabled)
        tlog("[twinspeak] leg3 (rears)   : EMPTY -- the rears fold onto the MAINS (leg1) at gain_rl/gain_rr\n");
    else
        tlog("[twinspeak] leg3 (rears)   : \"%s\" -- L = gain_rl*(ch7+gain_sl*ch5), R = gain_rr*(ch8+gain_sr*ch6)\n", n3.c_str());
    if (g_lfeFoldLeg >= 0)
        tlog("[twinspeak] leg4 (LFE fold) : gain_lfe*ch4 rides leg%d's device (whichever the mains routed)\n", g_lfeFoldLeg + 1);
    else
        tlog("[twinspeak] leg4 (LFE fold) : idle (the LFE rides leg1's own fold)\n");
    tlog("[twinspeak] capture      : \"%s\" (render loopback, one-way, 8ch @ %u Hz, ~%.1f ms delivery)\n",
           capName.c_str(), kSampleRate, g_capPeriodFrames * 1000.0 / kSampleRate);
    tlog("[twinspeak] clock sync   : every leg runs the SAME flow -- one %u fr x %u request, one device-buffer\n"
           "            anchor, one depth servo; the slaves grind their UNDELAYED boundary to leg1's\n"
           "            (240-fr bites, one per 2 s max, inside digital silence when it can). Packet mode\n"
           "            bends rate sub-0.1%% on a clock mismatch instead of drying the ring. Devices drift:\n"
           "            the engine grinds the drift out -- only delay_ms stays as the deliberate offset.\n",
           (unsigned)kOutPeriodFrames, (unsigned)kOutPeriods);
    tlog("[twinspeak] packet mode : %s -- per-packet depth servo: err = depth - delay - anchor - floor;\n"
           "            trim = PI on the slow EMA, slew-limited, +-%d ppm. trim = 0 and 48k = the bit-exact\n"
           "            path. A non-48k endpoint re-opens AT its native rate and the ENGINE resamples\n"
           "            (Hermite 4-point).\n",
           twsCfg.packetMode ? "ON (default)" : "OFF (the warden discipline)",
           (int)kPktMaxPpm);
    {
        auto estPathOf = [&](int i) -> double {
            const Leg& L = g_leg[i];
            const double delayMsv = (double)L.delayFrames * 1000.0 / (double)kSampleRate;
            const ma_device* d = &L.dev;
            const double devMs = d->playback.internalPeriodSizeInFrames
                ? (double)d->playback.internalPeriodSizeInFrames * (double)d->playback.internalPeriods
                  * 1000.0 / (double)d->playback.internalSampleRate : 0.0;
            return delayMsv + devMs;
        };
        const double ep1 = estPathOf(0), ep2 = twsCenterFold ? 0.0 : estPathOf(1);
        tlog("[twinspeak] align        : delay1 +%d ms, delay2 +%d ms, offset3 +%d ms -- bit-exact prefill\n"
             "            delay; put it on the EARLY leg. est path: leg1 ~%.1f ms%s\n",
             (int)(twsCfg.delay1Ms + 0.5f), (int)(twsCfg.delay2Ms + 0.5f),
             (int)(twsCfg.offset3Ms + 0.5f), ep1,
             twsCenterFold ? " (leg2 EMPTY -- the center rides leg1)" :
             (ep2 != 0.0 ? "" : ""));
        if (!twsCenterFold && ep2 != 0.0)
            tlog("                            leg2 ~%.1f ms (difference %+.1f ms with the delays included)\n",
                 ep2, ep2 - ep1);
    }
    if (twsCfg.volumeFollow && g_srcVol)
        tlog("[twinspeak] volume        : following \"%ls\" (the source's playback side) -- now %.0f%%%s -- drives every leg\n",
               g_srcVolName.c_str(), g_vol.load() * 100.0f,
               g_volMuted.load() ? ", MUTED" : "");
    else
        tlog("[twinspeak] volume        : %s\n", twsCfg.volumeFollow ?
             "no matching render endpoint found -- follow OFF (check input_device)" :
             "follow off (volume_follow = off)");

    if (g_logToFile)
        tlog("[twinspeak] running in BACKGROUND -- health line every 10 s\n\n");
    else
        tlog("[twinspeak] running -- Ctrl+C to stop. 3-line status every second\n\n");

    // the health loop
    unsigned long long uB[4] = { 0, 0, 0, 0 };
    unsigned long long dB[4] = { 0, 0, 0, 0 };
    unsigned long long aB[4] = { 0, 0, 0, 0 };
    unsigned long long pB[4] = { 0, 0, 0, 0 };
    long offV[4] = { 0, 0, 0, 0 };
    int  offWarm = 0;
    int  offPhase = 0;
    unsigned long long colLastMs[4] = { 0, 0, 0, 0 };
    unsigned sec = 0;
    while (twsRun) {
        Sleep(1000);
        ++sec;
        const unsigned long long nowMs0 = GetTickCount64();
        const unsigned long long hbCapNow = g_hbCap.load(std::memory_order_relaxed);
        const bool capFlowing = hbCapNow && (nowMs0 - hbCapNow) <= 1500;
        const bool offClose = capFlowing && offPhase == 4;
        if (capFlowing) {
            if (offWarm < 5) ++offWarm;
            if (++offPhase >= 5) offPhase = 0;
        } else {
            offWarm = 0;
            offPhase = 0;
        }
        for (int i = 0; i < 4; ++i) {
            const unsigned long long Ai = g_leg[i].captured.load(std::memory_order_relaxed);
            const unsigned long long Ci = g_leg[i].popped.load(std::memory_order_relaxed);
            if (offClose && pB[i] != 0 && Ci > pB[i] && Ai > aB[i])
                offV[i] = (long)(((double)(Ai - aB[i]) / (double)(Ci - pB[i]) - 1.0) * 1000000.0);
            else if (!capFlowing)
                offV[i] = 0;
            if (offClose || !capFlowing) {
                aB[i] = Ai; pB[i] = Ci;
            }
        }
        unsigned long long dropN[4], undN[4];
        for (int i = 0; i < 4; ++i) {
            dropN[i] = g_leg[i].rawDrop.load();
            undN[i]  = g_leg[i].und.load();
        }
        const long long st0 = (long long)g_leg[0].raw.seqTail.load(std::memory_order_relaxed);
        const long long st1 = (long long)g_leg[1].raw.seqTail.load(std::memory_order_relaxed);
        const bool driftOK = (st0 > 0 && st1 > 0);
        const double driftMs = driftOK ? (double)(st0 - st1) / 48.0 : 0.0;
        char driftStr[16];
        if (driftOK) snprintf(driftStr, sizeof(driftStr), "%+5.1f", driftMs);
        else         snprintf(driftStr, sizeof(driftStr), "%6s", "----");

        const unsigned long long nowMs = GetTickCount64();
        // the watchdog: a silent leg reopens
        if (!g_powerPark.load(std::memory_order_relaxed)
            && nowMs - g_bootMs > 5000) {
            for (int oi = 0; oi < 4; ++oi) {
                const int i = (oi == 0) ? 1 : (oi == 1) ? 0 : oi;
                Leg& W = g_leg[i];
                if (W.disabled || W.yielded.load(std::memory_order_relaxed)) continue;
                const unsigned long long hb = hbOutGet(i);
                const bool stale = (hb == 0) || (nowMs - hb) > 5000;
                if (stale && nowMs - colLastMs[i] >= 10000) {
                    colLastMs[i] = nowMs;
                    tryRestartLeg(i, &ctx, kOutPeriodFrames);
                }
            }
        }
        // the yield guard: release the leg on the default device; re-grab when free
        if (!g_powerPark.load(std::memory_order_relaxed)) {
            std::string defName;
            if (defaultRenderName(&defName)) {
                for (int i = 0; i < 4; ++i) {
                    if (g_leg[i].disabled) continue;
                    const char* yneedle = legNeedle(i);
                    if (!yneedle[0]) continue;
                    Leg& Y = g_leg[i];
                    const bool mine = containsCI(defName, yneedle);
                    if (mine && !Y.yielded.load(std::memory_order_relaxed)) {
                        Y.yielded.store(true, std::memory_order_relaxed);
                        tlog("[twinspeak] leg%d YIELD     : the Windows default render device (\"%s\") matches\n"
                             "                            this leg's needle (\"%s\") -- released (the stream is\n"
                             "                            NOT closed; the park clears when the default moves elsewhere)\n",
                             i + 1, defName.c_str(), yneedle);
                        Y.lastReopenMs = nowMs;
                    } else if (!mine && Y.yielded.load(std::memory_order_relaxed)) {
                        const int busy = foreignActiveSessions(yneedle);
                        if (busy > 0) {
                            static unsigned long long lastBusyLog[4] = { 0, 0, 0, 0 };
                            if (nowMs - lastBusyLog[i] >= 5000) {
                                tlog("[twinspeak] leg%d YIELD     : the default moved away, but %d foreign audio\n"
                                     "                            session(s) are still ACTIVE on this device --\n"
                                     "                            holding the release until they finish\n",
                                     i + 1, busy);
                                lastBusyLog[i] = nowMs;
                            }
                        } else {
                            if (Y.yielded.exchange(false, std::memory_order_relaxed)) {
                                tlog("[twinspeak] leg%d YIELD     : the default device no longer matches this leg\n"
                                     "                            -- park CLEARED, the pump resumes on the live ring\n",
                                     i + 1);
                                Y.lastReopenMs = nowMs;
                            }
                        }
                    }
                }
            }
        }
        // the power guard: resume rebuilds
        if (g_powerWoken.exchange(false)) {
            g_powerPark.store(false, std::memory_order_relaxed);
            tlog("[twinspeak] power guard : system resume -- rebuilding the capture and any DEAD leg\n");
            tryRestartCap(&ctx);
            for (int oi = 0; oi < 4; ++oi) {
                const int i = (oi == 0) ? 1 : (oi == 1) ? 0 : oi;
                if (g_leg[i].disabled) continue;
                if (g_leg[i].yielded.load(std::memory_order_relaxed)) continue;
                if (nowMs - hbOutGet(i) <= 5000) continue;
                tryRestartLeg(i, &ctx, kOutPeriodFrames);
            }
        }

        char volStr[16];
        if (g_volFollowLive.load(std::memory_order_relaxed))
            snprintf(volStr, sizeof(volStr), "%3.0f%%%s",
                     g_vol.load() * 100.0f, g_volMuted.load() ? "M" : "");
        else
            snprintf(volStr, sizeof(volStr), "%6s", "---");
        char errStr[16];
        snprintf(errStr, sizeof(errStr), "%6s", "----");
        if (!g_logToFile || (sec % 10) == 0) {
            for (int pi = 0; pi < 4; ++pi) {
                Leg& PL = g_leg[pi];
                const long pt = PL.pktTrimPpm;
                if (twsCfg.packetMode && (pt > 500 || pt < -500)) {
                    const unsigned long long ptn = GetTickCount64();
                    if (ptn - PL.pktLastTrimLogMs >= 60000) {
                        PL.pktLastTrimLogMs = ptn;
                        tlog("[twinspeak] pkt out%d     : clock mismatch held at %+ld ppm (ring %5.1f ms, never dry -- packet mode bends the rate instead of dropping)\n",
                             pi + 1, pt, PL.raw.count() * 1000.0f / kSampleRate);
                    }
                }
            }
            char b1[3][32], b2[3][32], b3[2][32];
            snprintf(b1[0], 32, "raw1 %5.1f", g_leg[0].raw.count() * 1000.0f / kSampleRate);
            snprintf(b1[1], 32, "raw2 %5.1f", g_leg[1].raw.count() * 1000.0f / kSampleRate);
            snprintf(b1[2], 32, "vol %s",     volStr);
            if (!capFlowing)
                snprintf(b2[0], 32, "off    idle/idle");
            else if (offWarm < 5)
                snprintf(b2[0], 32, "off    warm %d/5", offWarm);
            else
                snprintf(b2[0], 32, "off %+6ld/%+6ld", offV[0], offV[1]);
            snprintf(b2[1], 32, "drift %s",   driftStr);
            snprintf(b2[2], 32, "err %s",     errStr);
            snprintf(b3[0], 32, "short +%llu/%llu/%llu/%llu",
                     undN[0] - uB[0], undN[1] - uB[1], undN[2] - uB[2], undN[3] - uB[3]);
            snprintf(b3[1], 32, "evict +%llu/%llu/%llu/%llu",
                     dropN[0] - dB[0], dropN[1] - dB[1], dropN[2] - dB[2], dropN[3] - dB[3]);
            const char* l1[3] = { b1[0], b1[1], b1[2] };
            const char* l2[3] = { b2[0], b2[1], b2[2] };
            const char* l3[2] = { b3[0], b3[1] };
            if (twsLogSink || g_logToFile) {
                tlog("raw1 %5.1f | raw2 %5.1f | vol %s | off %+6ld/%+6ld | short +%llu/%llu/%llu/%llu | evict +%llu/%llu/%llu/%llu | drift %s | err %s\n",
                     g_leg[0].raw.count() * 1000.0f / kSampleRate,
                     g_leg[1].raw.count() * 1000.0f / kSampleRate,
                     volStr, offV[0], offV[1],
                     undN[0] - uB[0], undN[1] - uB[1], undN[2] - uB[2], undN[3] - uB[3],
                     dropN[0] - dB[0], dropN[1] - dB[1], dropN[2] - dB[2], dropN[3] - dB[3],
                     driftStr, errStr);
            } else {
                twsStatusBlock(l1, 3, l2, 3, l3, 2);
            }
        }
        fflush(stdout);
        for (int k = 0; k < 4; ++k) { uB[k] = undN[k]; dB[k] = dropN[k]; }
    }

    tlog("\n[twinspeak] stopping...\n");
    twsStopSilence = true;
    twsRun = false;
    Sleep(150);
    if (hWatch) { WaitForSingleObject(hWatch, 2000); CloseHandle(hWatch); }
    if (g_capDev.pContext)     ma_device_uninit(&g_capDev);
    for (int i = 0; i < 4; ++i) {
        if (g_leg[i].dev.pContext) ma_device_uninit(&g_leg[i].dev);
    }
    mixCrewShutdown();
    if (g_srcVol) { g_srcVol->Release(); g_srcVol = nullptr; }
    for (int i = 0; i < 4; ++i)
        if (g_pin[i].vol) { g_pin[i].vol->Release(); g_pin[i].vol = nullptr; }
    if (comOK) CoUninitialize();
    ma_context_uninit(&ctx);
    timeEndPeriod(1);
    tlog("[twinspeak] stopped.\n");
    return 0;
}

// ------------------------------------------------------------ lifecycle --
int twsEngineRun(const char* iniPath, bool fgForce, bool guiMode) {
    return engineMainCore(iniPath, fgForce, guiMode);
}

bool twsEngineStart(const char* iniPath) {
    return engineMainCore(iniPath, false, true) == 0;
}

void twsEngineJoin() {
    // the engine ran synchronously on the caller's thread; nothing to join
}

void twsEngineStop() {
    twsRun = false;
}
