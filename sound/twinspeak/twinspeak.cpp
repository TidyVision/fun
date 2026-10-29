// ============================================================================
//  twinspeak v6.5.15 -- ASIO-STRICT bit-exact engine (no resampler anywhere)
//
//  v6.5.15 (field report: "when the DAC is yielded, the output devices
//          aren't supposed to be changed to 100% by the program". The
//          yield release dropped the DAC but kept the leg's endpoint-
//          volume pin (IAudioEndpointVolume) alive, and pinCheckTick --
//          the ~2 s watcher pass that enforces 100%/unmuted on out1/out2
//          -- had no yield awareness. A leg with a live pin handle (e.g.
//          after a WASAPI fallback earlier in the session) therefore had
//          its Windows volume stomped to 100% every 2 s WHILE it sat
//          released as the system's main device -- exactly the device
//          whose slider belongs to the user. Two changes:
//            the YIELD release now drops the endpoint-volume handle with
//            the DAC (nothing of ours holds -- or touches -- the released
//            device), and pinCheckTick skips a yielded leg outright.
//            Re-pinning on yield RELEASE is unchanged: by then the default
//            has moved away and twinspeak owns the device again.)
//
//  v6.5.14 (field directive: "make it so that if the main audio device is
//          either output_device1 or output_device2 that the DAC shuts off
//          immediately to let the main device play" + field finding
//          "setting the buffer size to a reasonable value like 128 minimum
//          helped a lot". Two changes:
//            DAC YIELD (yield_default, default ON) -- Windows' default
//            render endpoint is re-enumerated every health tick; the moment
//            it is (or becomes) one of the leg devices, THAT leg's DAC is
//            released the same second: the KS pin closes / the WASAPI device
//            uninitializes, the pump parks, and nothing of twinspeak's
//            holds the hardware -- the app playing to the default device
//            owns it outright. When the default moves elsewhere the leg
//            reopens through the watchdog path (chime + warden baseline
//            re-arm included). The watchdog, collapse sentinel, stall
//            resync, deep resync and warden corrections all skip a yielded
//            leg -- its stale heartbeat is a deliberate release, not a
//            fault. Reaction time: one health tick (<= 1 s).
//            KS BITE FIELD NOTE -- bites under 128 frames now print a
//            startup note: the lapse guard's lap window shrinks with the
//            loop, and sub-128 loops (64) buzzed in the field; 128+ is the
//            steady zone. A note, not a clamp -- the bite stays the user's
//            call.)
//
//  v6.5.13 (field report: "sometimes I eventually get this reverberated
//          sawing sound that mixes in with the audio" -- the KS looped
//          WaveRT buffer had an unrecoverable lap-loss mode. The pacing
//          thread integrates the play register by residue deltas gated at
//          one full loop (d <= loopBytes always accepts); a deschedule
//          longer than one lap silently loses k whole laps, the believed
//          play position falls k loops behind, and the pacing sleeps on a
//          lead that no longer exists -- the DAC circles the entire loop
//          re-reading stale audio (the sawing) while heartbeats, und, the
//          ledger and devbuf all stay clean. v6.5.13 LAPSE GUARD: the
//          DAC's advance over each poll gap is wall-exact (QPC elapsed x
//          rate); the residue advance is mod-loop. Any disagreement
//          beyond tolerance -- or any gap of one lap or more -- re-pins
//          the write cursor to play+lead and re-primes the lead region
//          with the last played block (the ASIO replay convention;
//          silence before the first block): a bit-exact skip of the lost
//          window, logged as 'KS lapse' and counted in the devbuf line.
//          A side effect: KS legs now also self-heal cleanly across
//          system suspend.)
//
//  v6.5.12 (field report: "when watching a movie, I noticed that the center
//          speaker and main speaker eventually start delaying again", with
//          drift_interval_ms = 2000 in the ini -- BELOW the clamp floor, so
//          it silently ran at 5000. Two changes:
//            TIGHTER RESYNC -- the drift_interval_ms clamp floor drops
//            5000 -> 2000 ms, so the ini's 2000 is now honored. The
//            warden's ledger is frame arithmetic on the play boundaries,
//            not the noisy per-second ppm meter, so a 2 s evaluation
//            spacing costs nothing in accuracy: worst case to the first
//            re-pin drops from ~15 s (3 streak windows x 5 s) to ~6 s.
//            DEEP RESYNC (deep_resync_min, default 0 = off) -- the S5's
//            below-packetizer layer (endpoint FIFO + amp silicon) can
//            deepen silently over a long session: the drift ledger stays
//            pinned while the center audibly lags again. No host meter
//            sees that layer; a scheduled fresh reopen of the center leg
//            is the one reset that re-primes it. Opt-in for long movie
//            sessions.)
//
//  v6.5.11 (field report: "another synthetic buzz (probably from an
//          interrupt at 100% cpu) that delays the center speakers ... add
//          functionality that has it recover from that." -- the buzz is the
//          known shortfall path: a starved device thread finds the ring
//          empty and replayFill loops the last fragment (the fragment-loop
//          buzz). What was MISSING is the AFTERMATH: while the device
//          thread starves, the pump keeps feeding its ring, so the stalled
//          leg accumulates stale backlog above its target anchor and its
//          stream position falls behind the other leg -- the center arrives
//          late long after the buzz stops. Two-stage recovery, always on:
//            FAST PATH -- a leg with a shortfall (und) in the last second
//            AND backlog above its anchor is resynced IMMEDIATELY: the
//            stale excess is discarded (a bit-exact skip -- the warden's
//            own operation, but per-leg, on first evidence, no 3 s streak,
//            no interval wait). The leg resumes at the freshest data the
//            moment the storm ends.
//            DEEP PATH -- starvation lasting 3 consecutive seconds reopens
//            the leg fresh (the watchdog's own mechanism): that also resets
//            whatever the device-side silicon accumulated during the storm
//            -- which matters on the S5, whose below-packetizer FIFO "is
//            running something we have yet to completely uncover".
//          Gated on the drift baseline being armed (boot/prefill and the
//          post-reopen transient never fire it) and the power-park state.
//          The fast path is capped at 100 ms per event; the deep path
//          respects the 10 s per-leg reopen spacing. Zero sample-path
//          deltas: the audio crossing the buffer switch is unchanged.)
//
//  v6.5.10 (field directive: "have it resync after five seconds to ensure
//          the best possible stability" -- the whole warden resync chain now
//          runs on a five-second clock. Correction spacing defaults to
//          5000 ms -- the default resync clock (the v6.5.12 clamp floor is
//          2000 ms), so a
//          ramping drift error can never sit uncorrected longer than the
//          3 s streak + one 5 s interval. The post-reopen settle gate drops
//          10 s -> 5 s, so the baseline re-arm completes ~9 s after a
//          reopen (5 s settle + a 4-sample average) instead of ~14 s, and
//          boot calibration lands at ~9 s instead of ~15 s. Zero sample-
//          path deltas; corrections remain bit-exact skips/in-place pads.)
//
//  v6.5.9 (field directive: "Read the actual per-leg device buffering at
//          runtime." -- the align line's est path is arithmetic on CONFIGURED
//          sizes; this build adds a MEASURED reading off the live devices,
//          printed every 10 s as the 'devbuf' line next to the health line.
//          For each leg it reports the frames queued AHEAD of the DAC's play
//          cursor, from the authoritative source per transport:
//            WASAPI shared   -> IAudioClient::GetCurrentPadding (the live pad)
//            WASAPI exclusive -> the whole device buffer (WaveRT exclusive
//                              drivers misreport padding as 0, and miniaudio
//                              pops the FULL buffer on each event anyway --
//                              so the buffer size IS the honest pad);
//            WDM-KS          -> the pacing thread's own write-minus-play
//                              lead, read off the DAC position register.
//          Plus the ring's live depth, so each line is: pad + ring = the
//          measured waterline per leg, and their difference is the measured
//          version of the align line's estimate. A DIAGNOSTIC ONLY: nothing
//          in the sample path, the rings, the rules or the gates changes.
//          What the line still cannot see: buffering BELOW the USB
//          packetizer -- the S5's own receiver FIFO / amp silicon (wired
//          USB does not change this: the FIFO is the S5 firmware's own
//          choice). If the measured paths read EQUAL while the ear still
//          hears the center late, that residual is the S5's FIFO, now
//          proven by elimination instead of estimated.)
//
//  v6.5.8 (field report: "the two speakers eventually start delaying each
//          other sometimes ... I can notice even 10ms on my rhythm game."
//          TWO warden gaps closed, zero audio-path deltas:
//    1. STALE BASELINE: the drift baseline was calibrated ONCE (boot,
//          seconds 8..15) and never revisited -- but a device reopen (S5
//          sleep/wake, USB re-enumeration, power-guard rebuild) can
//          legitimately move the path's real latency afterwards. Every
//          successful reopen now invalidates the baseline; it re-arms
//          automatically once the path settles (~14 s), with its own log
//          line. If the drift column is FLAT while the ears still hear
//          drift after a re-arm, the offset is device silicon (below the
//          USB packetizer) -- no host trigger exists.
//    2. CORRECTION SPACING: the 30 s rate limit was hardcoded. New ini
//          key drift_interval_ms (default 30000 = exact old behavior,
//          range 5000..60000): lower it (e.g. 10000) so the warden can
//          chase ramping drift in tighter circles -- the price is each
//          correction being an audible skip of up to drift_correct_ms.)
//
//  v6.5.7 (field report: "it's something that doesn't resemble a chime" --
//          the interim watchdog_key build is WITHDRAWN to a side archive;
//          this build ships the audio-thread inspection result instead.
//          ONE change, zero audio-path deltas: the pump tick drops from
//          1 ms to 500 us -- the waitable-timer minimum. A periodic timer
//          cannot go sub-ms (lPeriod is whole milliseconds), so both the
//          pump threads and the KS pacing thread now run the high-res
//          timer ONE-SHOT (due time -5000 in 100-ns units, lPeriod = 0),
//          re-armed at the top of every cycle. Half the tick = half the
//          refill latency the buffer switch can observe -- the shortfall
//          path (replayFill looping the last ~4-period fragment, which
//          synthesizes a buzzy fragment-loop "beep" out of the PROGRAM
//          audio, not the chime) fires later or never. The KS slack
//          threshold moves with it: sleep only when >0.5 ms of slack.
//          Cost: two pumps waking at 2 kHz -- trivial memcpy bursts,
//          well under the v6.5 optimization pass's pay grade.)
//
//  v6.5.5 was the last shipped baseline; see its notes below.
//  v6.5.4 (field verdict: "Turns out it was from the cpu affinity" -- the
//          v6.5.3 CPU PLACEMENT shipped, got field-tested, and the pin was
//          PROVEN to be the glitch source: with every thread on 2 cores, the
//          KS thread's deadline spin assumes a free neighbor core that no
//          longer exists, and an MMCSS-boosted audiodg.exe on the same pair
//          turned scheduling into a fight twinspeak could lose audibly. The
//          whole placement layer is PULLED: no cpu_affinity, no priority
//          class, no process-wide mask -- exact v6.5.2 scheduling. Lesson
//          recorded: a deadline-spin realtime engine wants the scheduler's
//          full core inventory, not a reserved pair. audiodg.exe placement
//          remains a Process Lasso concern, outside twinspeak entirely.
//          Unknown ini keys are silently ignored, so inis that still carry
//          the v6.5.3 keys keep working untouched.)
//
//  v6.5.3 (field directive: "how optimized is this, actually? ... I want to
//          also ensure it runs on the last two threads of my cpu, same going
//          with audiodg.exe which I have modified affinity and priority
//          properties of on process lasso." -- the realtime internals were
//          already RT-grade (MMCSS 'Pro Audio' + time-critical pumps and
//          device threads, FTZ/DAZ, high-res waitable timers, lock-free CAS
//          rings, -O3 -march=x86-64-v3 with FMA fusion disabled); what was
//          MISSING is CPU PLACEMENT: the scheduler was free to roam all
//          ~8 threads across all cores, colliding with desktop/DPC churn
//          and going cache-cold on every migration. Two new ini keys:
//
//    cpu_affinity  pin EVERY thread to a fixed CPU set (last:N / "4,5" /
//                  "4-5" / 0x30 masks), process-wide BEFORE any audio
//                  thread exists so everything inherits. Paired with a
//                  Process Lasso rule for audiodg.exe, the whole audio
//                  chain lives on the same dedicated cores.
//    priority      raise the PROCESS class (normal/above_normal/high/
//                  realtime). The audio threads were already time-
//                  critical; 'high' lifts the rest of the process (health
//                  loop, watchers, reopen logic) to match. One interaction
//                  handled: in the HIGH class HIGHEST == TIME_CRITICAL ==
//                  15, which would erase the out2_first notch -- there the
//                  mains pump takes ABOVE_NORMAL instead. Defaults: off
//                  (exact v6.5.2 behavior). Banner reports both.
//
//  v6.5.2 (field report: "It eventually starts acting up whenever I let my
//          computer sleep" + a 4-hour health log from the M720Q. The log
//          catches the whole failure: the moment the machine degrades into
//          sleep, the S5's stream stops consuming at the real rate -- first
//          small excess drops (288/576 fr), then the break: leg2's device
//          thread kept ticking but consumed at ~42% of the cable's rate
//          (off +1,345,794 ppm), the out2 ring stuffed to 85 ms, ~572 ms/s
//          passively evicted, gated drops + warden re-pins fighting a
//          limit cycle they can never win. The heartbeat watchdog is
//          STRUCTURALLY BLIND here: the callback thread never went silent,
//          so nothing reopened the leg. The engine is faultless while
//          devices behave; nothing in it handled a half-alive device.
//          The fix -- the POWER GUARD (power_guard = on, default):
//
//    1. WM_POWERBROADCAST on a hidden top-level window (a message-only
//       HWND would never receive broadcasts): PBT_APMSUSPEND parks the
//       pumps -- no more data into a device the OS is taking away -- and
//       suppresses the watchdog (no reopen storms mid-suspend). The
//       resume pair clears the park and the health loop rebuilds the
//       capture pin + BOTH legs from scratch, replays the chime, and
//       resets the warden/hint state. A post-sleep stream is never
//       trusted again.
//    2. A consumption-collapse sentinel: a healthy 48 kHz link never
//       leaves +-10% (the same log's worst HEALTHY wobble was +-4000
//       ppm). Two consecutive 1-s windows beyond that and the leg is
//       reopened fresh -- this catches the degraded sleep-ENTRY window,
//       which fires BEFORE the suspend broadcast, plus any driver reset
//       that leaves a thread ticking at a bogus rate. With both layers,
//       the worst case across any sleep is one fresh reopen on wake
//       instead of a permanently glitching path.
//
//  v6.5.1 (field report: "I found that my delay between speakers are ~65ms"
//          -- that number was read off the health line's 'drift' column while
//          delay1_ms = 60, and it is CORRECT: ~60 ms of it is the user's own
//          deliberate delay -- the bit-exact alignment that cancels the S5's
//          silicon lag -- plus the legs' few-ms natural start asymmetry, and
//          the warden was holding the path exactly there. The column merely
//          conflates the DELIBERATE alignment with DRIFT. v6.5.1 splits the
//          two on the health line. A DISPLAY change only: no audio-path,
//          ledger or rule change of any kind):
//
//    5. THE 'err' COLUMN: the health line now ends "| drift %s | err %s".
//       drift stays the RAW inter-speaker offset (the delay calibration
//       included) -- the alignment instrument it always was. err is the
//       distance from the calibrated baseline -- the exact number the
//       warden's correction rule acts on (|drift - baseline| >=
//       drift_correct_ms held 3 s). err ~ +0.0 means the path is pinned to
//       your calibration; err moving and holding is what a real drift event
//       looks like. Before the baseline exists (first ~15 s) err reads
//       ----. Documented in the banner, the baseline log line and the ini
//       template.
//
//  v6.5 (field directive: "optimize it max whilst also preserving everything
//          in the code that's meaningful" -- a PURE optimization pass: every
//          ini key, every log line, every rule and every sample value stays
//          bit-identical to v6.4; only the CPU cost of the realtime path
//          dropped. Proven by a randomized bit-equivalence harness against
//          the v6.4 code: ring push/pop/discard/silence across 15 ring
//          geometries, both mixers, both keep-alive paths, at -O0/-O2/-O3
//          and -O3+x86-64-v3 -- ALL BIT-EXACT):
//
//    1. BULK RING COPIES: pushOverwrite/pop copy each run as one (or two,
//       at the wrap seam) wrap-aware memcpys instead of one memcpy PER
//       FRAME. The v6.0 "the path is memcpy" promise now holds at the call
//       level too: a 1 ms capture burst lands in each leg's raw ring as a
//       single memcpy instead of 48 (96 on the out side). Same data, same
//       order, same head/tail/seqTail CAS discipline. pushSilence prefills
//       in cap-sized chunks -- a chunk LARGER than the ring cap would be
//       clamped by pushOverwrite and silently lose frames (the equivalence
//       harness caught exactly that before it could ship).
//
//    2. BULK MIXER: the per-frame mixFrame() became mixBlock() -- the ini
//       gains are hoisted into locals, and the leg2 loop still READS ONLY
//       ch3 (the explicit-mute guarantee stays structural). Per-frame FP
//       ops are identical (mul, then add -- the build pins
//       -ffp-contract=off so no mul+add can ever fuse into an FMA and
//       change a rounding). The v6.4 negative-zero behavior is preserved
//       literally: the discrete-LFE branch keeps the "+ 0.0f" add because
//       IEEE says -0 + +0 = +0 (the harness checks that bit).
//
//    3. KEEP-ALIVE FAST PATH: sumLeg2Extras sums the tone in wrap-free
//       RUNS over the cycle table (the wrap check fires once per table
//       cycle, not per frame) and drops the chime branch from the hot loop
//       when no chime is armed -- the 99.9% case. The chime-active loop is
//       unchanged. Same (idx + i) mod len sequence, same clamps.
//
//    4. DENORMAL GUARD: every realtime thread (capture, both pumps, both
//       device callbacks, both KS pacers) now runs with FTZ/DAZ set (SSE
//       flush-to-zero + denormals-are-zero -- the standard RT-audio
//       guard). Honest scope: it affects ONLY f32 denormals, i.e. samples
//       below ~-370 dBFS -- over 100 dB under any DAC's LSB and 330 dB
//       under the keep-alive's -40 dBFS. What it buys: no denormal
//       microcode assist (100+ cycles per op) can ever stall the audio
//       path -- a real dropout class when volume_follow sits near zero
//       and multiplies quiet content into the denormal range.
//
//    5. BUILD: -O3 + LTO + x86-64-v3 (SSE4.2/AVX2; the M720q is Coffee
//       Lake -- any 2013+ x86-64 CPU runs it) with -ffp-contract=off, so
//       vectorization accelerates the bulk loops without touching a bit.
//
//  v6.4 (field directive: "a timestamp system capturing every frame by its
//          intervals which are corrected when there's even a 10ms drift
//          between the two speakers" -- detector only, no live trim input):
//
//    DRIFT WARDEN: every capture burst is stamped into a monotonic
//    frame-interval ledger. Each leg's stream position (the capture
//    sequence number of its play boundary) is advanced by exactly the
//    real frames that cross it -- pops, gated drops and warden skips
//    advance it; replays and deliberate pads do not -- so the difference
//    of the two legs' positions IS the measured inter-speaker offset in
//    cable time, printed every second as 'drift' on the health line.
//    After a ~15 s calibration (the baseline includes the ini delay
//    offsets), a drift that leaves the baseline by >= drift_correct_ms
//    (default 10) for 3 consecutive seconds is re-pinned automatically:
//    the late leg's ring discards its excess (a bit-exact skip -- the
//    same operation as the v6.0 gated drop) and the early leg is held
//    (its last buffer replays in place until the depth returns). Both
//    corrections move the stream position without touching a single
//    sample's value. What the ledger cannot see: buffering BELOW the USB
//    packetizer -- the S5's own receiver FIFO. If 'drift' stays flat
//    while the ear hears drift, the offset is device silicon, no host
//    trigger exists, and the log itself proves where the offset lives.
//
//  v6.3.1 (field directive: "I want to fix the remaining issue which is the
//          fact that I have actual delay left on the adesso part"):
//
//    1. KS-LEG RING FLOOR: on a backend = ks leg the ring floor drops from
//       (whole device buffer + burst + 4 ms) to (one KS bite + one capture
//       burst + 1 ms). Justification: a WASAPI-exclusive leg must survive
//       miniaudio's worst legitimate pull -- the WHOLE buffer popped as two
//       back-to-back period callbacks -- but a KS leg's pull is twinspeak's
//       OWN pacing thread: exactly ks_period_frames frames per switch,
//       never merged, paced against the DAC's position register on an
//       MMCSS TIME_CRITICAL thread, refilled by a pump that drains every
//       1 ms. At the field config (ks_period_frames 96, cap_ms 1) that
//       takes the KS leg from ring 8 ms / est path ~14 ms to ring 5 ms /
//       est path ~10 ms. Both legs on ks = symmetric minimal paths; the
//       remaining inter-leg offset is the S5's own USB+amp latency (only
//       the delay knobs -- and the ear -- can see that part).
//
//    2. HONEST CLAMPS: ks_periods below 2 (and ks_period_frames outside
//       32..4096) were silently clamped; now the log names the clamp and
//       the reason (a 1-period loop has no write lead -- the DAC would eat
//       silence the moment the pacer hiccups).
//
//    3. THE ALIGN LINE NOW DOES THE ARITHMETIC: it prints both est paths
//       and their difference with the delay direction spelled out, so the
//       delay trim starts from a number instead of subtracting two banner
//       lines by eye.
//
//  v6.3 (field directive on the working v6.2 config -- "take stuff from
//        FlexASIO ... WDM-KS inside twinspeak, lowest latency, 256 samples"):
//
//    WDM-KS OUTPUT BACKEND (per leg, ini-selectable). FlexASIO gets its
//    low-latency path from Windows Kernel Streaming; twinspeak now speaks
//    the same protocol natively -- a direct KS render pin on the device's
//    wave filter, BELOW the WASAPI layer and below the Windows audio
//    engine entirely:
//
//      backend1 = wasapi | ks      (leg1, the 5.1 mains)
//      backend2 = wasapi | ks      (leg2, the center speaker)
//      ks_period_frames = 256      (the buffer-switch granularity, samples)
//      ks_periods       = 3        (looped WaveRT buffer, in periods)
//
//    Mode "ks" walks the exact chain the low-latency ASIO wrappers use:
//      1. enumerate KSCATEGORY_RENDER wave filters (SetupAPI), match the
//         SAME output_deviceN name needles as the WASAPI path;
//      2. find a render sink pin that accepts 48 kHz (6ch on leg1 /
//         2ch on leg2) -- KsCreatePin (ksuser.dll, loaded dynamically)
//         with KSPIN_CONNECT + WAVEFORMATEXTENSIBLE, looped-streaming
//         interface first;
//      3. allocate the WaveRT looped buffer (KSPROPERTY_RTAUDIO_BUFFER)
//         and map the hardware play-position register
//         (KSPROPERTY_RTAUDIO_POSITIONREGISTER; falls back to polling
//         KSPROPERTY_AUDIO_POSITION through the same pin);
//      4. STOP -> ACQUIRE -> PAUSE -> prefill -> RUN, then a TIME-CRITICAL
//         MMCSS "Pro Audio" thread writes exactly ks_period_frames frames
//         per bite, pacing itself against the mapped position register
//         (sleep until far, spin over the last stretch -- no events, no
//         driver wakeups, no engine).
//
//    Everything above the device layer is UNCHANGED and bit-exact: the
//    same rings, the same v6.0 replay/gated-drop device-master rules, the
//    same volume follow, the same center gates, keep-alive and chimes.
//    A KS leg's buffer switch IS playCore() -- the WASAPI path just calls
//    it from miniaudio's thread, the KS path from its own. If a driver
//    refuses the KS pin (no WaveRT support, pin busy, format refused)
//    that leg logs the reason and falls back to the proven v6.2 WASAPI
//    exclusive path -- never silence, never a half-open device.
//
//    KS bypasses the Windows endpoint layer: no endpoint volume (so
//    pin_outputs is a no-op on a KS leg -- volume_follow still governs
//    it, applied in the pump exactly as before), and the DAC feeds its
//    physical jacks directly. twinspeak --list-ks prints every KS render
//    filter with its pins and formats, for needle picking.
//
//  v6.2 (field directives on the working v6.1 config):
//
//    1. BACKGROUND = ON: the ini can start twinspeak as a background
//       process. After a SUCCESSFUL startup the console detaches
//       (FreeConsole) and every log line -- banner, sync events, health
//       line -- goes to twinspeak.log next to the ini (health line every
//       10 s there, so the file stays small). A FAILED start keeps the
//       console so the reason stays on screen. Stop a background run with:
//       taskkill /IM twinspeak.exe   (--foreground overrides the ini for
//       one run; a second instance is refused unless --multi).
//
//    2. CHIMES: the CenterFold startup signature is back, modernized --
//       the old square G5->D6 rising jingle became two short TRIANGLE
//       blips in the low treble (C7 2093 Hz -> G7 3136 Hz, the same
//       rising fifth two-plus octaves up, fast decay -- a blip, not a
//       horn). Summed at the out2 DEVICE layer exactly like the keep-
//       alive: at engine start and again whenever the watchdog brings
//       the center leg back. out1 -- the bit-exact mains path -- never
//       carries them. chimes / chime_gain in the ini.
//
//    3. HINT HONESTY: the one-shot "steady clock offset" hint now waits
//       for a SETTLED path: 10 s minimum, a quiet measurement window (no
//       corrections/replays inside it), and a same-sign offset beyond
//       +-5000 ppm held across THREE consecutive windows. The old 1000 ppm
//       threshold sat below the meter's own noise floor (the ~2 ms device
//       switch quantizes each 1 s window by +-2000 ppm), and the startup
//       transient printed a -10000 ppm false alarm seconds after launch.
//       Both are fixed. The one-time startup excess drop is also tagged
//       "(startup settle)" in the log, so it reads as what it is: the
//       strict rule trimming the capture backlog that accumulated while
//       the devices were still being opened.
//
//  v6.1 (field directives on the working v6.0 config -- alignment + center
//  presence, still bit-exact, still no resampler anywhere):
//
//    1. ALIGNMENT DELAY: delay1_ms / delay2_ms push ONE leg's output later
//       by whole milliseconds. The mechanism is the ASIO-honest one: the
//       leg's ring target simply grows -- the samples leave it later, bit
//       for bit; there is no resampler and no pitch path involved. Use the
//       knob on whichever leg arrives EARLY: if the center speaker's USB
//       path (amp + DSP) lags the mains, raise delay1_ms so out1 waits for
//       it; delay2_ms pushes the center leg later instead. The banner
//       prints each leg's est path so the starting point is the printed
//       difference; the ear trims the rest.
//
//    2. OUT2 IS PROCESSED FIRST ("it needs the most attention"): with
//       out2_first = on the center leg's pump thread is created first AND
//       runs at a higher priority than the mains pump (the center leg's
//       device thread already started first, and the watchdog already
//       checks it first). out1 is deliberately second in line -- it has
//       the deeper slack (its raw ring) to absorb the difference.
//
//    3. CENTER PRESENCE GATES on out2 -- "if the center channel is absent,
//       something mixes left and right instead; zero everything except the
//       keep-alive." Root cause found: that L+R substitute is Windows'
//       OWN stereo->8ch upmix, applied at the cable's PLAYBACK side before
//       twinspeak ever captures it -- a stereo app renders to the 8ch Line 1
//       endpoint and the engine fills FC with a*FL + b*R. It IS ch3 by the
//       time the capture sees it, so channel muting cannot remove it.
//       v6.1 adds two gates in the out2 pump, both default ON:
//         * center_gate         -- a block whose ch3 is digital zero emits
//           NOTHING but the keep-alive tone (the literal rule).
//         * center_upmix_guard  -- a least-squares fit per block: if ch3 is
//           an EXACT linear mix of ch1/ch2 (residual < -30 dB, gains in the
//           upmix-matrix band, fronts active) the block is classified as
//           engine upmix and MUTED -- out2 = keep-alive only. Genuine 5.1
//           dialogue does NOT fit that fingerprint (its center is an
//           independent track) and always passes. The health line counts
//           guarded blocks ("cm"); the log announces them with the fitted
//           gains so the fingerprint is visible.
//
//  v6.0 (the three field directives, implemented as structure, not options):
//
//    1. "DO NOT USE ANY MORE PITCH SHIFT." The fractional resampler and the
//       whole v5.0 servo (rho / rhoFll / trim / bias) are DELETED. The audio
//       path is now literally memcpy: capture -> raw ring -> gain+mute+mix
//       -> out ring -> device buffer. twinspeak cannot bend pitch because it
//       never regenerates a sample -- it only MOVES the samples the cable
//       delivered. (What the ear caught on v5.0 was the trim/lock glide.)
//
//    2. "CENTER ONLY ON OUT2 -- MUTE EVERY OTHER CHANNEL, NO CLONE." The
//       out2 mixer now EXPLICITLY zeroes ch1/ch2/ch4/ch5/ch6/ch7/ch8 in its
//       private copy of every frame before reading ch3. Out2 structurally
//       carries ONLY the center channel -- it is not a clone of the program
//       audio -- and the startup route map states it. Out1 is unchanged:
//       FL/FR + the LFE fold, FC driven digital silence, so the center
//       physically cannot enter out1 either.
//
//    3. "~50 MS IS TOO MUCH." Buffer geometry tightened: template defaults
//       move to out1ms = out2ms = 5. Ring target floor is unchanged (the
//       FULL device buffer + burst + 4 ms -- the device's worst legitimate
//       bite), but at 5 ms periods that floor is ~19 ms instead of ~29 ms:
//       est path ~19 + 10 (device) + ~5 (capture burst) = ~34 ms, down from
//       ~54 ms. The banner prints the real negotiated number; if the ini
//       still asks for 10 ms periods a hint line offers the 5 ms preset.
//
//    CLOCK DISCIPLINE (device-master: what an ASIO driver does at every
//    buffer switch, expressed in software). The path is bit-exact, so the
//    two clocks' drift is absorbed by exactly two RULES -- never by rate:
//      * SHORTFALL -> REPLAY: a buffer switch the ring cannot fill is
//        padded by REPLAYING the last played buffer (the ASIO underrun
//        convention) instead of punching a silence hole. Counted "und".
//      * EXCESS -> GATED DROP: if the cable clock runs faster than the
//        device and the ring grows past its natural sawtooth by a full
//        period, the OLDEST excess is discarded -- at most once per 5 s
//        per leg, each event logged with the measured offset. Counted
//        "corr" on the health line.
//    At crystal-level offsets both events are rare enough to be inaudible
//    (a 10 ms correction every few minutes). If the cable free-runs, the
//    one-shot hint prints the exact measured ppm to dial into Clock
//    Correction in the VAC Control Panel -- fixing it at the SOURCE is the
//    only way to make a bit-exact path silent forever, and the tool now
//    measures it instead of hiding it behind a resampler.
//
//    clock_sync = on/off (new key) turns the discipline off entirely
//    (off = never replay/drop: shortfall pads silence, excess evicts at
//    the ring cap -- pure A/B only). auto_resync (v3.7-v5.0) is retired;
//    an old ini's auto_resync=off still maps to clock_sync=off.
//
//  v5.0 (THE ASIO MODEL -- what actually fixes the field crackle):
//    Field log (v4.0, user config 10/10/5): both leg servos cycled
//      "EXCESS -- draining buffer (2408 fr held, target 816) -- rho now 1.0500"
//      "recovered -- clock servo resumed (1680 fr held, target 816) -- rho 1.0000"
//    several times per SECOND, on BOTH legs. Root cause: v4.0's FLL trusted
//    band was +-2000 ppm, but a VIRTUAL cable clock is not crystal-locked to
//    anything -- VAC synthesizes Line 1's clock from system timers, and
//    without its Clock Correction engaged it free-runs ~0.5-1 % (5000-10000
//    ppm) away from the real device crystals. The FLL clamped at 0.2 %, the
//    un-tracked ~0.8 % ballooned the buffer into the EXCESS zone, the zone
//    slammed rho to 1.05, drained past the exit line, slammed back to 1.0 --
//    a permanent limit cycle of 5 % pitch steps, several per second. THAT
//    was the "crackle". (v3.x's shed/dup had the same authority ceiling --
//    200 frames/s against a 1 % mismatch is ~0.4 % of what was needed --
//    which is why every geometry from 1 ms to 40 ms crackled.)
//
//    v5.0 replaces ALL correction machinery with what ASIO does, expressed
//    in software (a real ASIO driver gets this from the hardware: one master
//    clock, callback-driven buffers, and no host-side rate tricks):
//      * ONE CONTINUOUS clock sync per leg. rho = rhoFll + trim + bias, all
//        additive, all through a single slew limiter (0.2/s). No modes, no
//        zones, no steps: the worst possible event is a slow pitch glide.
//      * rhoFll = the MEASURED cable/device ratio with REAL authority
//        (+-5 %). Whatever the clocks actually do, the resampler follows.
//      * trim (<= +-0.3 %, on a 5 s EMA of held level) pins latency to the
//        ring target continuously -- without touching the instant sawtooth.
//      * bias (+-3 %) only after a REAL event (time-persisted: stall
//        cushion gone for 100 ms, flood for 500 ms), glided in and out.
//      * Ring target floor raised to FULL device buffer + burst + 4 ms
//        (v4.0 floored at ONE period -- less than the device's own 2-period
//        buffer, so a single double-size pop could dip the ring below its
//        own floor every time pop phase and burst phase aligned badly).
//      * ONE locked ASCII logger: v4.0 wrote servo lines to stderr and the
//        health line to stdout from six threads -- the garbled console.
//      * The stale "endpoint mix is 2ch, not 6ch" WARNING is gone: with the
//        strict split FC/LFE are DRIVEN SILENCE, so a stereo endpoint has
//        nothing to upmix -- it cannot resurrect a fake center.
//      * If the measured steady offset exceeds 1000 ppm, one hint line
//        suggests enabling Clock Correction for the cable in the VAC
//        Control Panel -- v5.0 tracks any offset; correction shrinks the
//        steady trim to crystal level.
//    auto_resync = off still pins rho at exactly 1.0 (bit-exact A/B test).
//
//  v4.0 (CLOCK SERVO -- the crackle-proof rewrite):
//    * The v3.x rate matcher corrected clock drift by DROPPING (shed) or
//      REPEATING (dup) whole runs of frames. Every such correction is a
//      hard sample discontinuity -- a click by construction. It also
//      assumed "a healthy pump never holds more than one capture burst
//      of pending", which is only true when the device period is close
//      to the burst. At outXms = 40 (user config) the pump legitimately
//      parks one FULL PERIOD (1920 fr = 40 ms) of mixed audio in pending
//      between device drains; the sawtooth (mean ~960 fr) sailed past
//      the burst-scaled shed gates (720/1680 fr) and the gentle shed
//      fired at its 200 frames/s pace limit -- one click every 5 ms --
//      with sporadic bank sheds (up to 20 ms of audio) on top. That is
//      the "crackles again even with this config" report.
//    * v4.0 removes shed/dup ENTIRELY. Each leg now runs a fractional
//      linear-interpolation resampler whose ratio is servoed to the
//      MEASURED clock ratio cable/device (cumulative capture frames /
//      cumulative device-popped frames, 1 s windows, slew-limited).
//      No frame is ever dropped or repeated in steady state, so clicks
//      are structurally impossible at ANY period/ring/burst geometry.
//      Steady-state pitch error = the leg's clock offset while trimming
//      (<= +-0.2 % worst case; real crystal pairs are +-100 ppm =
//      +-0.01 %) -- far below audibility. Latency stays pinned: the ring
//      fill converges to target and stays there.
//    * RECOVERY: after a genuine starvation (capture stall, console
//      sleep), buffer total below target - 2xperiod switches the leg to
//      rho = 0.95 (re-deepen at ~2400 fr/s) until the band is regained,
//      then hands back to the servo. Symmetric excess guard at +5 %.
//    * auto_resync = off pins rho at exactly 1.0 (bit-exact passthrough,
//      no correction at all) -- for A/B tests only.
//    * MMCSS "Pro Audio" is now ALSO registered on miniaudio's own
//      device threads (capture + both playback callbacks), not just the
//      pump threads (miniaudio only sets thread priority).
//    * Health line: stg (staged ms, what pend was) and ppm (live servo
//      ratio deviation per leg; a sustained +N = that leg's device runs
//      N/48 ppm SLOW relative to the cable) replace shd/dup.
//
//  v3.9 (crash fix + volume autodetect + tunable keep-alive):
//    * CRASH FIX: the v3.8 health line printf was missing its volStr
//      argument. Harmless while pending was 0 ("vol (null)"), FATAL the
//      first second a buffering transient made pending non-zero -- the
//      %s conversion then consumed raw float bits as a pointer and the
//      process died instantly (the "closes by itself right after a
//      glitch sound" report). Every field after vol was shifted by one
//      as well (the garbage 140732231882871 in ovw was a code pointer).
//    * VOLUME ENDPOINT AUTODETECT: the cable's playback side is now
//      matched via volume_endpoint (ini) -> input_device -> "virtual
//      audio cable"; if all miss, the banner lists EVERY render
//      endpoint Windows has (name + state) so the right string is
//      visible and can be pasted into volume_endpoint.
//    * WATCHDOG/PIN RACE: the 2 s pin checker no longer reads Leg.id
//      (which the reopen path can rewrite concurrently); the reopen
//      re-pins the endpoint volume itself, on its own thread.
//    * TUNABLE KEEP-ALIVE: keepalive_hz / keepalive_gain (ini). The
//      S5's amp can sleep through 40 Hz @ 1% (-40 dBFS); raise the
//      gain (0.03..0.05 is still inaudible) if it powers down anyway.
//
//  v3.8 (volume follow + output pinning + out2-first + self-healing legs):
//    * VOLUME FOLLOW (volume_follow = on): the Windows volume of the
//      cable's PLAYBACK side -- the "Line 1" slider in sndvol / the volume
//      hotkeys when Line 1 is the default device -- is the single master
//      knob for BOTH legs. A 25 ms watcher polls the endpoint volume and
//      mute; every captured frame is scaled BEFORE the per-leg mix, so
//      out1 and out2 always track it identically and stay level-matched.
//    * OUTPUT PINNING (pin_outputs = on): the out1/out2 endpoint volumes
//      are forced to 100% + unmuted at startup and re-checked every 2 s
//      (drift is logged and re-pinned) -- the Windows output sliders can
//      no longer double-attenuate; all volume lives on the Line 1 knob.
//    * OUT2-FIRST (out2_first = on): the center leg is opened and
//      started BEFORE the mains leg -- its 40 Hz keep-alive is up first.
//    * WATCHDOG: the 1 s health loop tracks a callback heartbeat per
//      device thread. A thread silent >5 s (endpoint invalidated, USB
//      re-enumeration, driver reset -- the "output shut off by itself
//      while twinspeak still runs" class of failure) is reopened BY
//      NAME automatically (fresh endpoint id, so a replug re-attaches),
//      first in its previous share mode then shared, then re-pinned by
//      the watcher. Retries every second until it succeeds.
//    * MMCSS: pump threads join the "Pro Audio" MMCSS role at
//      time-critical priority (10 ms targets are scheduling-sensitive).
//    * Health line adds "vol NN%" (M = Line 1 muted; --- = follow off).
//
//  v3.7 (the "162 ms pending" build -- banks can no longer pin):
//    User report on v3.6: "162 ms pending and it just crackles at 10 ms
//    ring1/2_ms. 20 ms works and even period sizes (out1/2ms) at 1 ms
//    work." Three mechanisms, all fixed:
//
//      1. MISSED-BURST POP UNDERRUN (the 10 ms-ring crackle). With the
//         Line 1 port on WaveCyclic, capture_exclusive falls back to
//         shared and the engine clamps the 5 ms burst to ~10 ms -- EQUAL
//         to the 10 ms out period. The pop-vs-burst phase drifts with
//         the two clocks, so a device pop can land just before its
//         burst: the ring then holds target - period = 816 - 480 = 336
//         frames against a 480-frame pop -> a 144-frame silence blip.
//         ring 20 ms survives because its target (960) is exactly two
//         periods; 1 ms periods survive because their pops are tiny.
//         Fix: the ring floor now carries a FULL burst of phase slack
//         (period + burst + 2 ms -- provably safe at any burst/period
//         ratio), and the honest latency fix is a smaller burst: the
//         WaveRT port driver on Line 1 (capture_exclusive then engages,
//         burst ~5 ms, floor drops to ~17 ms / 12 ms).
//      2. SHED COULD LOSE THE RACE (the 162 ms "pending"). v3.6 shed at
//         most 1 frame per 5 passes = 200 frames/s (~4200 ppm); any
//         stall/burst bank outgrew that and pinned the pending buffer
//         at its 8192-frame (~170.7 ms) hard cap, evicting the oldest
//         frames (more crackle) while the pend EMA read ~162 ms.
//         Fix: BANK SHED -- when the INSTANT pending level passes
//         shedAbove + 2 bursts, the excess is shed proportionally (up
//         to two bursts per pass, no EMA lag, no throttle). The bank is
//         structurally capped near ~35 ms worst case and clears in
//         milliseconds; the 170 ms pin is impossible by construction.
//      3. DUP TRICKLE (fast-clock legs). The same 200 frames/s cap on
//         dup could not track a genuinely fast device clock. Fix: once
//         the phase-aware confirm window passes, the top-up is
//         proportional (whole guard-band deficit, capped at burst/4
//         per pass).
//    Health line: pend is now the INSTANT level per leg, and the shed/
//    dup rates are printed -- a sustained shed of ~N frames/s means that
//    leg's clock is ~N/48 ppm slow. Instant diagnosis.
//
//  v3.6 (the 10 ms build -- every drift-guard window now SCALES):
//    Root causes of the 10 ms crackle + pitch wobble, all three fixed:
//
//      1. BURST-PHASE DUP HUNTING -- the dup trigger ("fill below target
//         minus guard while pending AND raw are empty") is also true for
//         a few passes inside every capture burst gap even with the
//         clocks in perfect sync. Whether it fired depended on where the
//         device pop phase sat relative to the capture bursts, so dup
//         runs swept in and out audibly (repeated frames = pitch dip /
//         wobble). v3.6 gates every dup run behind a PHASE-AWARE CONFIRM
//         WINDOW sized to one capture burst period + 2 ms: a burst-phase
//         dip always self-corrects at the next burst arrival (refill
//         reaches target again), genuine fast-clock drift never does.
//         Phase dips can no longer confirm; real drift confirms within
//         ~burst + 2 ms. (Replaces v3.5's fixed 12 ms confirm window,
//         which was itself audible at 10 ms buffers.)
//      2. FIXED SHED BANK -- surplus was shed only above a FIXED
//         1024-frame (~21 ms) pending threshold at ANY buffer size, so
//         the slow-clock leg banked ~21 ms before shedding a single
//         frame. v3.6 scales the shed threshold to the real capture
//         burst: burst + max(burst/2, 2 ms), floored at 6 ms total.
//      3. CAPTURE BURST == OUT PERIOD -- the capture device previously
//         inherited min(out1ms, out2ms), so shrinking the out legs also
//         shrank capture bursts and moved every guard target.
//         v3.6 adds cap_ms (default 5): the capture burst decouples
//         from the out periods. Small bursts = small dips = tight
//         windows = stable legs at 10 ms without changing guard
//         character. All windows key off the REAL negotiated capture
//         period (printed in the banner), never the request.
//
//    * capture_exclusive = on (default): the VAC capture pin is opened
//      WASAPI-EXCLUSIVE -- on a WaveRT port that is the direct cyclic-
//      buffer path (~10 ms cable->app). If the pin refuses (another
//      client, WaveCyclic port, format mismatch) it falls back to
//      shared automatically and the banner says so.
//    * Ring target floor now carries half a capture burst of phase
//      slack: period + max(burst/2, 2 ms) + 2 ms -- the device pop phase
//      can no longer starve the device at small ring sizes.
//    * Health line adds the pending EMA per leg ("pend x/y ms") so the
//      shed bank is visible while tuning.
//
//  v3.3 (routing made visible -- NO behavior change vs v3.2):
//    * The user confirmed the required split AGAIN and demanded zero
//      duplication. Audit of the v3.2 mixer confirms it already holds:
//        out1: FL = ch1 + gain_lfe*ch4(LFE), FR = ch2 + gain_lfe*ch4(LFE),
//              FC + discrete LFE + SL + SR are DRIVEN DIGITAL SILENCE --
//              center physically cannot appear on out1, and the bass is
//              folded XOR discrete (never both = never double bass).
//        out2: L = R = gain_center*ch3 -- leg2's mixer READS ONLY ch3;
//              ch1/ch2/ch4 are never touched. +40 Hz @ 1% keep-alive.
//    * v3.3 adds the ROUTE MAP to the startup banner (actual gains from
//      the ini) so the split is verifiable on every launch instead of
//      taken on faith.
//
//  v3.2 (crackle tuning):
//    * ring1_ms / ring2_ms: the OUT RING depth is now decoupled from the
//      device period. outXms still picks the WASAPI period (5 ms); the ring
//      can sit much deeper (shipped: 40 ms). Scheduler spikes and capture
//      hiccups up to ring-minus-period are absorbed silently -- same immunity
//      as a huge device buffer, but it costs only ring depth in latency,
//      NOT 2x ring in device buffering. Keep ring1_ms == ring2_ms so the
//      legs stay time-aligned.
//
//  v3.1 (routing confirmed by the user):
//    * lfe_fold now defaults ON -- the cable's LFE is mixed onto FL/FR at
//      gain_lfe ("LFE is mixed fully into the left and right channels"),
//      and the DISCRETE 5.1 sub channel is what is now optional (set
//      lfe_fold = off to drive it). No engine changes vs v3.
//    * Shipped ini opens exclusive = on (both drivers already accepted it
//      in v2.1; align_modes still falls back to shared for BOTH together).
//
//  What v3 fixes (both bugs reported on v2.1):
//
//  1) "Monstrous out1" -- v1/v2 fed the Realtek a STEREO stream on a 5.1
//     endpoint. miniaudio's rectangular 2->6 channel converter (and, one
//     layer up, the Windows audio engine for stereo streams on a 6ch mix
//     format) derives a FAKE center: FC = 0.5*L + 0.5*R. The Realtek's
//     physical center speaker has been playing a smeared L+R copy all along,
//     comb-filtering against the Adesso's true center -- and the ini folded
//     0.4*LFE onto both fronts on top of that. This is the "it pulls L and R
//     and smears it into the center" complaint, root cause found: the smear
//     lived on the REALTEK leg, not the Adesso, which is why the 40 Hz
//     keep-alive never cured it.
//     v3 opens leg1 as a NATIVE 6ch (5.1) f32 stream:
//         FL = L + lfe, FR = R + lfe, FC = driven silence, SL/SR = 0
//         (lfe = gain_lfe * ch4; with lfe_fold = off the fold is skipped and
//          ch4 goes to the discrete 5.1 sub channel instead).
//     Every layer sees a full-layout stream: no upmix matrix, no speaker
//     fill, no fake center, ever.
//
//  2) "Delayed out2" + the v2 crackle -- three stacked defects:
//       a. v2's skip/dup drift guard was bang-bang on the INSTANT ring fill:
//          capture delivers in 10 ms (480-frame) bursts, so the ring swings
//          +/- 10 ms inside every period while its MEAN stays put. With
//          thresholds at target/2 and 2*target, every burst boundary could
//          cross a threshold and SKIP REAL AUDIO -- that is the crackle, and
//          it also let each leg's mean float target/2..2*target (up to 7.5 ms
//          of leg-to-leg skew from drift direction alone).
//       b. exclusive mode could succeed on one leg and fail on the other;
//          exclusive halves device buffering (2x5 ms vs 2x10 ms), so legs
//          ended up with ~10 ms of different fixed device latency.
//       c. a 5 ms ring simply cannot cover one 10 ms shared-mode device
//          period: if the pop lands before the next capture burst, the
//          device underruns. v2's outXms=5 was below the shared floor.
//     v3 fixes all three with a REFILL-TO-TARGET level controller:
//       * the pump tops each out ring up to its target on every 1 ms pass
//         and never pushes past it -- burst-proof by construction, the out
//         ring can structurally neither overflow nor trigger corrections
//         from capture bursts (v2's crackle mechanism is gone);
//       * surplus audio (this leg's device clock slower than the cable)
//         parks in a small pending buffer and is shed at the drift rate;
//       * deficit (device clock faster) tops up by duplicating one frame
//         per pass while the fill sits below target - 1 ms -- self-limiting;
//       * the ring target is floored at one device period + margin, so a
//         single pop can never starve the device;
//     align_modes forces both legs into the SAME share mode (both
//     exclusive, or both fall back to shared together), so device
//     buffering is identical and the legs stay time-aligned.
//     (An out2-late-by-15-20 ms that persists in shared mode usually means
//     the Adesso's shared mix format is 44.1 kHz and miniaudio inserts a
//     resampler -- the startup diagnostics now print each leg's negotiated
//     rate so you see it immediately; fix = set that device's default
//     format to 24-bit 48000 Hz in Windows sound settings.)
//
//  Architecture (v6.0): one 8ch shared capture of the VAC cable, two fully
//  independent per-leg pipelines on their own threads, all bit-exact:
//
//    VAC cable (8ch shared capture)
//      |  capture callback = dumb fan-out, memcpy only
//      +--> leg1.raw ring ---> pump 1 (mover) --> leg1.out ring (6ch) --> Realtek
//      |    mix: FL=L+0.4*LFE, FR=R+0.4*LFE, FC=0 (driven), SL/SR=0
//      +--> leg2.raw ring ---> pump 2 (mover, FIRST + higher priority) -->
//           leg2.out ring (2ch) --> Adesso
//           pump 2 gates: ch3 zero -> nothing; ch3 == a*ch1+b*ch2 (the
//           Windows stereo-upmix fingerprint) -> muted; then
//           mix: ch1/ch2/ch4..ch8 EXPLICITLY MUTED, both outs = ch3 only
//                + 40 Hz @ 1% keep-alive at the device layer
//      playback callbacks = the ASIO buffer switch: gated excess drop +
//      last-buffer replay; NO rate state exists anywhere in the program.
//      delay1_ms/delay2_ms = the leg's ring target grown by whole ms -- the
//      samples exit later, bit for bit; no pitch path can exist.
//
//  WASAPI exclusive is ON by default (and on by default in the shipped ini
//  since v3.1's field confirmation): exclusive bypasses the Windows volume
//  slider, which is why volume_follow + pin_outputs exist. align_modes
//  still forces both legs to agree on the share mode.
//
//  Config (twinspeak.ini, next to the exe -- or pass a path):
//      out1ms = 5            ; WASAPI period, leg1 (v6.0 default)
//      out2ms = 5            ; same for leg2 -- keep EQUAL for alignment
//      ring1_ms = 10         ; out-ring depth (floored at 2*period+burst+4ms)
//      ring2_ms = 10         ; keep EQUAL for alignment
//      delay1_ms = 0         ; v6.1: bit-exact ring delay per leg (the EARLY
//      delay2_ms = 0         ;        leg takes the delay; see template)
//      cap_ms = 5            ; capture burst period
//      exclusive = on        ; both-or-neither WASAPI exclusive attempt
//      align_modes = on      ; force both legs into the same share mode
//      lfe_fold = on         ; LFE folded onto FL/FR at gain_lfe (confirmed)
//      clock_sync = on       ; v6.0 device-master replay/drop discipline
//      center_gate = on          ; v6.1: ch3 silent -> out2 = keep-alive only
//      center_upmix_guard = on   ; v6.1: ch3 == a*ch1+b*ch2 (stereo upmix)
//                                ;       -> muted (Windows fills FC for you)
//      input_device   = Line 1
//      output_device1 = Realtek
//      output_device2 = Xtream
//      ; optional: gain_l, gain_r, gain_lfe, gain_center
//      ; v3.8+: volume_follow, pin_outputs, out2_first
//      ; v3.9+: volume_endpoint, keepalive_hz, keepalive_gain
//
//  Input channel order is the VAC 8ch standard: 1L 2R 3C 4LFE 5SL 6SR 7RL 8RR.
//  leg1 output order is Windows 5.1: FL FR FC LFE BL/SL BR/SR (6ch).
//  Fixed 48 kHz (the cable's rate). Build: bash build.sh  (llvm-mingw, WASAPI).
// ============================================================================

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cwctype>
#include <cstdarg>
#include <objbase.h>
#include <propsys.h>
#include <propidl.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <xmmintrin.h>     // v6.5: FTZ (SSE flush-to-zero)
#include <pmmintrin.h>     // v6.5: DAZ (denormals-are-zero)

// --------------------------------------------------- v6.3 WDM-KS includes ---
// ks.h/ksmedia.h give the kernel-streaming property sets, pin/datarange
// structs and the KSPROPERTY_CONNECTION/PIN enums. initguid.h FIRST makes
// DEFINE_GUIDSTRUCT emit the actual GUID definitions in this TU (user-mode
// KS convention -- no import lib needed for GUIDs). setupapi.h enumerates
// the KSCATEGORY_RENDER wave filters. The Vista+ WaveRT property values and
// structures are NOT in every bundled ksmedia.h, so they are defined here
// under TS_ names with the exact layouts of the WDK ksmedia.h (verified
// against the reference WDM-KS implementation FlexASIO builds on).
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

// output2 keep-alive tone: 40 Hz at 1% gain (~-40 dBFS, inaudible) summed onto
// BOTH channels at the DEVICE layer -- every frame, silence or not, underrun or
// not, phase-continuous. v3 renders it from a precomputed 1200-sample cycle
// table (48000/40 = exactly one cycle) instead of calling sin() per frame.
static constexpr double   kTwoPi             = 6.283185307179586;
static constexpr unsigned kKeepAliveTableMax = 10240;  // holds down to ~4.7 Hz cycles
// v3.9: the keep-alive tone is ini-tunable (keepalive_hz / keepalive_gain).
// The S5's amp sleeps on silence; 40 Hz @ 1% may sit BELOW its detect
// threshold -- if the speaker still powers down, raise keepalive_gain
// (0.03..0.05 stays inaudible at 40 Hz) or move the frequency.
static float              g_kaHz   = 40.0f;  // actual tuned values (from ini)
static float              g_kaGain = 0.01f;
static unsigned           g_kaLen  = 1200;   // samples per cycle: round(48000 / g_kaHz)
static float              g_kaTable[kKeepAliveTableMax];
static unsigned           g_kaIdx  = 0;      // owned by leg2's device thread only

// v6.0 ASIO-STRICT clock discipline -- the audio path is bit-exact memcpy;
// drift between the cable's virtual clock and each device crystal is absorbed
// by TWO device-master rules at the buffer switch (never by a rate change):
//   * SHORTFALL -> REPLAY: a buffer switch the ring cannot fill replays the
//     last played buffer (ASIO underrun convention), not silence.
//   * EXCESS -> GATED DROP: ring level above the natural sawtooth top by a
//     full period discards the oldest excess, at most once per 5 s per leg.
// The natural sawtooth top is target + burst (one unprocessed capture
// burst); the drop line adds one more period of margin so pop/burst phase
// can NEVER false-trigger it. After a drop the level returns to target.
//   kCorrMinIntervalMs : minimum spacing between drop corrections per leg.
//   kReplayMaxPeriods  : last-played buffer kept for replay (in periods).
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

struct Config {
    float       out1Ms         = 5.0f;
    float       out2Ms         = 5.0f;
    float       ring1Ms        = -1.0f;   // out-ring target; <0 = follow outXms
    float       ring2Ms        = -1.0f;
    float       legacyBufferMs = 10.0f;   // only used if outXms absent from ini
    float       capMs          = 5.0f;    // v3.6: capture burst period (decoupled)
    bool        captureExclusive = true;  // v3.6: exclusive VAC capture pin attempt
    bool        haveOut1       = false;
    bool        haveOut2       = false;
    bool        exclusive      = false;   // v3: default OFF (volume + alignment)
    bool        alignModes     = true;    // both legs same share mode
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
    bool        clockSync      = true;    // v6.0: device-master replay/drop discipline
                                          // (replaces v5.0's auto_resync / resampler)
    float       delay1Ms       = 0.0f;    // v6.1: bit-exact ring delay per leg
    float       delay2Ms       = 0.0f;    //       (the EARLY leg takes the delay)
    bool        centerGate     = true;    // v6.1: ch3 digital-zero -> out2 keep-alive only
    bool        centerUpmixGuard = true;  // v6.1: ch3 == a*ch1+b*ch2 (Windows fills FC
                                          //       for stereo sources) -> block muted
    bool        volumeFollow   = true;   // v3.8: Line 1 endpoint volume drives both legs
    bool        pinOutputs     = true;   // v3.8: out1/out2 endpoints pinned 100%/unmuted
    bool        out2First      = true;   // v3.8: center leg opens and starts first
    float       keepaliveHz    = 40.0f;  // v3.9: out2 keep-alive tone (5..2000 Hz)
    float       keepaliveGain  = 0.01f;  // v3.9: out2 keep-alive amplitude (0..0.25)
    bool        background     = false;  // v6.2: detach the console after a
                                          //       successful start; log to file
    bool        chimes         = true;   // v6.2: CenterFold signature, modernized
    float       chimeGain      = 0.4f;   // v6.2: chime blip peak (0..1)
    // v6.3 WDM-KS output backend, per leg. "wasapi" = the v6.2 path
    // (miniaudio, exclusive WaveRT event mode). "ks" = a direct kernel-
    // streaming render pin with a WaveRT looped buffer paced against the
    // mapped play-position register -- the FlexASIO-grade path, engaged
    // below the Windows audio engine. ks_period_frames is the buffer-switch
    // granularity in samples (256 @ 48 kHz = one bite every 5.33 ms);
    // ks_periods sizes the looped WaveRT buffer around it.
    std::string backend1       = "wasapi";
    std::string backend2       = "wasapi";
    unsigned    ksPeriodFrames = 256;    // KS buffer-switch size (frames)
    unsigned    ksPeriods      = 3;      // WaveRT looped buffer = ksPeriods * period
    // v6.4 drift warden: the frame-interval ledger is ALWAYS on (it only
    // observes); drift_guard switches the automatic re-pin on top of it.
    bool        driftGuard     = true;   // auto re-pin at >= drift_correct_ms
    float       driftCorrectMs = 10.0f;  // trigger distance from the baseline (ms)
    // v6.5.8: minimum spacing between warden corrections -- was the hardcoded
    // kWardenMinIntervalMs = 30000. Lower it for tighter pinning on
    // latency-critical playback; the price is each correction being an
    // audible skip/stutter of up to drift_correct_ms.
    unsigned    driftIntervalMs = 5000;   // v6.5.10: the five-second resync (was 30000)
    // v6.5.12: scheduled fresh reopen of the CENTER leg (minutes; 0 = off).
    // The S5's below-packetizer FIFO can deepen silently over a long
    // session -- the drift ledger stays pinned while the center audibly
    // lags again. No host meter sees that layer; a scheduled reopen is
    // the one reset that re-primes it (brief gap, warden re-arms ~9 s).
    unsigned    deepResyncMin   = 0;
    // v6.5.14 DAC YIELD: when the Windows default render endpoint is one of
    // the leg devices, that leg's DAC is released immediately (KS pin
    // closed / WASAPI device freed, pump parked) so the app playing to the
    // default device owns the hardware; the leg reopens through the
    // watchdog path when the default moves elsewhere.
    bool        yieldDefault    = true;
    // v6.5.2 power guard: sleep / Modern-Standby survival. Suspend parks
    // the pumps; resume rebuilds capture + both legs; a sentinel reopens
    // any leg consuming beyond +-10% for 2 s (a half-alive post-sleep
    // stream ticks at a bogus rate with a FRESH heartbeat -- the silence
    // watchdog cannot see it). off = exact v6.5.1 behavior.
    bool        powerGuard     = true;
};
static Config g_cfg;

// --------------------------------------------------- v6.2 chimes -----------
// The CenterFold startup signature, modernized. CenterFold played a rising
// SQUARE fifth (G5 784 Hz -> D6 1174.66 Hz, 80+150 ms, amp 0.42); the
// modernized chime keeps the rising fifth but moves it two-plus octaves up
// into the LOW TREBLE and renders it as two short TRIANGLE blips with a fast
// exponential decay (C7 2093 Hz 42 ms -> G7 3136 Hz 72 ms, 20 ms gap) -- a
// blip, not a horn. The pair is rendered ONCE at startup into a static
// buffer; leg2's device thread sums it onto BOTH out2 channels exactly like
// the keep-alive (device layer, deliberately NOT scaled by volume_follow:
// it is a UI cue, not program audio). out1 never carries it. Re-armed by
// the watchdog when the center leg comes back (3 s cooldown).
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
    }
    size_t count() const {
        return (size_t)(head.load(std::memory_order_acquire) - tail.load(std::memory_order_acquire));
    }
    // producer: append n frames; if full, drop the oldest (drop amount
    // returned). v6.5: the frames are copied as ONE wrap-aware bulk memcpy
    // (two at the wrap seam) instead of one memcpy per frame -- same data,
    // same order, same head/tail evolution, a fraction of the call
    // overhead (bit-exactness proven by the randomized equivalence harness
    // against the v6.4 per-frame loop, 15 ring geometries).
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
    void pushSilence(size_t n) {
        // v6.5: bulk prefill in cap-sized chunks. The chunk must NEVER
        // exceed cap: pushOverwrite clamps n > cap (dropping from src),
        // which would silently lose the overshoot -- the v6.4 one-frame-
        // per-call loop never hit that clamp, and neither may these chunks
        // (the equivalence harness caught it; see the v6.5 header note).
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
// Everything one KS output leg owns. KS replaces ONLY the device layer:
// the pumps, rings, gates and sync rules above are identical for both
// backends. The WaveRT looped buffer is the driver's own DMA memory mapped
// into this process; the play position is either a mapped hardware
// register (zero syscalls per poll) or KSPROPERTY_AUDIO_POSITION.
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
    // The OUT ring's seqTail (above) IS this leg's stream position: the
    // capture sequence number of its play boundary. Pops, gated drops and
    // warden skips advance it; replays and deliberate pads do not. The
    // difference of the two legs' positions is the measured inter-speaker
    // offset (cable time). The pump transfers the raw ring's sequence space
    // into the out ring; the device thread owns the play boundary; the
    // health loop owns the warden decisions.
    size_t              prefillRemaining = 0;        // boot prefill: carries no sequence
    std::atomic<size_t> padFrames{0};                // deliberate replay pad (warden)
    std::atomic<unsigned long long> padTotal{0};     // frames padded (quiet-window check)
    std::atomic<unsigned long long> wardenDrops{0};  // warden skip events
    std::atomic<unsigned long long> wardenPads{0};   // warden pad events
    std::atomic<double> lagMs{0.0};                  // age of the newest played frame (ms)
    bool                streamSeeded = false;        // pump: out ring's seq space armed
    unsigned long long  lastReopenMs = 0;            // warden pauses 10 s after a reopen
};
static Leg g_leg[2];

static std::atomic<bool> g_run{true};

// ------------------------------------------------------------ v3.8 volume --
// VOLUME FOLLOW: the Line 1 playback endpoint's Windows volume (the slider
// in sndvol / the hotkeys when Line 1 is the default device) is the single
// master knob. A 25 ms watcher polls its IAudioEndpointVolume, the pumps
// multiply every captured frame by the scalar before mixing, so BOTH legs
// track it identically and the out1/out2 sliders stay pinned at 100%.
// The out2 keep-alive is summed at the device layer and is deliberately
// NOT scaled -- the center device stays awake even at volume 0.
// GUIDs are spelled out because zig c++ (GNU mode) has no reliable __uuidof.
static const CLSID kCLSID_MMDeviceEnumerator =
    { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const IID kIID_IMMDeviceEnumerator =
    { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const IID kIID_IAudioEndpointVolume =
    { 0x5CDF2C82, 0x841E, 0x4546, { 0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A } };
static const PROPERTYKEY kPKEY_Device_FriendlyName =
    { { 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } }, 14 };

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
// v6.5.8: set by every successful device reopen (watchdog leg/capture, the
// power-guard resume path). The health loop consumes it: the drift warden's
// baseline is invalidated and re-armed once the path settles, because a
// reopen can legitimately change the path's host-side latency (an
// exclusive->shared fallback renegotiates device buffering; a fresh device
// start refills its silicon FIFO differently). A boot-time-only baseline
// goes stale against exactly that -- and the warden then "corrects" the
// path toward a number that no longer describes it.
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
    // v6.5: FTZ/DAZ on every realtime thread -- the standard RT-audio
    // denormal guard. Honest scope: affects ONLY f32 denormals, i.e. values
    // below ~-370 dBFS (100+ dB under any DAC's LSB, 330 dB under the
    // keep-alive's -40 dBFS). What it buys: a denormal microcode assist
    // (100+ cycles per operation) can no longer stall the audio path -- a
    // real dropout class when volume_follow sits near zero and multiplies
    // quiet content into the denormal range.
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

static void tlog(const char* fmt, ...) {
    va_list ap;
    EnterCriticalSection(&g_logCS);
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
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
    // v3.9: this runs on the watcher thread. It no longer reads Leg.id and
    // no longer re-resolves endpoints -- tryRestartLeg (main thread) does
    // the re-pin after a reopen, so the two threads never touch the same
    // Leg fields (the old code wcslen'd Leg.id.wasapi while a reopen could
    // memcpy over it).
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

// v6.0 playback core -- the ASIO buffer switch, bit-exact:
//   1) EXCESS -> GATED DROP: if the ring grew a full period past its natural
//      sawtooth top (target + burst), the cable clock runs faster than this
//      device: discard the OLDEST excess (level back to target), at most
//      once per kCorrMinIntervalMs per leg, logged. The pop/burst sawtooth
//      cannot reach that line, so only real drift ever fires it.
//   2) the switch itself: pop the ring; a shortfall is padded by REPLAYING
//      the last played buffer (the ASIO underrun convention) instead of
//      punching a silence hole. Counted "und". No rate change, no
//      interpolation -- there is no code path that can bend pitch.
//   3) remember the buffer just played for (2). Leg2 then sums the
//      keep-alive tone at the device layer.
// v6.3: the buffer switch itself, backend-free. WASAPI (miniaudio's device
// thread) and WDM-KS (twinspeak's own pacing thread) both land HERE, so
// every guarantee -- gated excess drop, replay-on-shortfall, last-block
// bookkeeping, the popped counter that feeds the off-meter -- is one code
// path for both backends. frameCount is the REAL device bite: miniaudio's
// negotiated period on WASAPI, ks_period_frames on KS.
static void bufferSwitchFill(int idx, float* out, size_t frameCount) {
    Leg& L = g_leg[idx];
    L.popped.fetch_add((unsigned long long)frameCount, std::memory_order_relaxed);    // device clock

    // v6.4: a deliberate warden pad -- the drift warden restoring an EARLY
    // leg's depth. The last played buffer repeats IN PLACE (the exact ASIO
    // replay convention), the stream slides padFront frames later, and not
    // a single ring frame is consumed. Counted separately from real
    // shortfalls so "und" stays honest.
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
    // keep-alive tone on top of whatever we just wrote -- silence, replay
    // padding or real center audio. Phase-continuous via the cycle table.
    // v6.2: the chime is summed right next to it -- same device-layer rule
    // (NOT scaled by volume_follow; a UI cue, not program audio).
    // v6.5: with no chime armed (the 99.9% case) the tone is summed in
    // wrap-free RUNS over the cycle table -- the wrap check fires once per
    // table cycle instead of per frame, and the chime branch disappears
    // from the hot loop entirely. The (idx + i) mod len sample sequence
    // and every clamp are exactly the general loop's (bit-exact, harness-
    // proven, including multi-cycle buffers and the chime-exhausted edge).
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
    sumLeg2Extras((float*)pOutput, frameCount);
}

// ----------------------------------------------------------------- mixer ----
// v6.5: the mixer is applied to a WHOLE BLOCK with the ini gains hoisted
// into locals (the config is startup-frozen), replacing the per-frame
// mixFrame() call. Every per-frame FP operation is unchanged -- multiply,
// then add, never an FMA (the build pins -ffp-contract=off) -- so the mix
// is bit-identical to v6.4 (harness-proven, both legs, both lfeFold
// states, including the IEEE -0 + +0 = +0 negative-zero behavior of the
// discrete-LFE branch, whose literal "+ 0.0f" is therefore KEPT).
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
        // ONLY ch3 -- every other input channel is muted by never being
        // touched here, so out2 carries the center channel and NOTHING
        // else, and can never be a clone of the program audio. (The
        // guarantee is structural, exactly as in v6.4's mixFrame.)
        const float gC = g_cfg.gainCenter;
        for (size_t i = 0; i < n; ++i) {
            const float c = gC * sc[i * kInCh + 2];
            chunk[i * 2 + 0] = c;
            chunk[i * 2 + 1] = c;
        }
    }
}

// ------------------------------------------------- v6.1 center gates -------
// CENTER PRESENCE on out2. Two gates run in the leg2 pump on every block it
// is about to mix, both behind ini switches (center_gate /
// center_upmix_guard), both default ON:
//
//   1) center_gate -- the literal rule: if ch3 is digital zero for the whole
//      block, out2's copy of the block emits NOTHING (the keep-alive tone is
//      summed later, at the device layer, so the speaker still never sees
//      digital silence). The mix path already passes zero through as zero --
//      this gate makes the guarantee explicit and visible (state-change log).
//
//   2) center_upmix_guard -- the real-world "center absent but something
//      plays anyway" case. A stereo app cannot write center to the 8ch
//      cable: the Windows engine UPmixES it first, FC = a*FL + b*R with
//      fixed positive gains, at the cable's playback side -- BEFORE the
//      capture. It is ch3 by the time twinspeak sees it, so no amount of
//      channel muting can remove it. The fingerprint is exact, though: per
//      block, least-squares fit ch3 ~ a*ch1 + b*ch2. If the residual is
//      < -30 dB of ch3's energy with the gains inside the upmix-matrix band
//      and the fronts actually active, the block is engine upmix, not
//      center -> muted. Genuine 5.1 dialogue is an INDEPENDENT track and
//      does not fit (music/FX live in the fronts too); it always passes.
//      A hit also arms a short hold (50 ms) so the sub-burst-size blocks
//      between full analysis windows stay muted on a continuous upmix
//      stream; any block that FAILS the full test disarms it immediately,
//      so a false hit costs one block, never a scene of dialogue.
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

// ----------------------------------------------------------------- pumps ----
// v6.0: the pump is a dumb, fast MOVER (ASIO discipline -- the device thread
// is the master; the producer never corrects anything):
//     raw ring (8ch) -> master volume -> explicit per-leg muting + mix ->
//     out ring (push everything).
// There is NO staging buffer, NO resampler, NO rate state anywhere: what the
// cable delivered is what the device plays, sample for sample. Excess is
// handled by the playback callback's gated drop; shortfall by its replay --
// both at the buffer switch, exactly where an ASIO driver would act.
// v6.5.7: the pump tick is 500 us -- the waitable-timer minimum. A PERIODIC
// SetWaitableTimer cannot go below 1 ms (lPeriod is whole milliseconds), so
// the high-resolution timer runs ONE-SHOT (lPeriod = 0) with a relative due
// time in 100-ns units and is re-armed at the top of every cycle. The
// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION flag is what makes 0.5 ms real:
// without it the OS coalesces the expiry to the scheduler tick. Half the
// tick = half the refill latency the buffer switch can observe, so the
// shortfall-replay path (replayFill's fragment loop -- the audible
// glitch/buzz under load) fires that much later or never.
static constexpr long long kTick500us = -5000;   // 0.5 ms, negative = relative (100-ns units)

static DWORD WINAPI pumpThread(LPVOID p) {
    const int idx = (int)(INT_PTR)p;
    Leg& L = g_leg[idx];
    boostAudioThread();   // v3.8: MMCSS "Pro Audio" + time-critical priority
    // v6.1: with out2_first on, the CENTER leg's pump outranks the mains
    // pump by one priority notch ("out2 is processed first -- it needs the
    // most attention"). The movers are trivial memcpy work; leg1's deeper
    // raw ring absorbs any scheduling deference. Device threads stay equal:
    // both are deadline-bound and must never be ranked against each other.
    if (idx == 0 && g_cfg.out2First)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    // SYNCHRONIZATION timer (no MANUAL_RESET flag!). A manual-reset timer
    // stays signaled after its first expiry -- WaitForSingleObject does not
    // reset it -- which silently turns this loop into a 100%-of-a-core busy
    // spin (v1/v2.0's 30% CPU). A synchronization timer auto-resets each time
    // a wait is released, so every wait below blocks for exactly one period.
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

        // drain raw -> master volume -> mute/mix -> out ring (push all).
        // The ring headroom absorbs the normal pop/burst sawtooth; real
        // excess is dropped by the callback's gated rule, so this push can
        // only passively evict under a pathological flood (clock_sync off
        // with a fast cable) -- counted in outDrop.
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
            mixBlock(idx, sc, chunk, n);           // v6.5: hoisted bulk mix
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
        mixBlock(idx, sc, chunk, n);           // v6.5: hoisted bulk mix
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
// A direct kernel-streaming output path -- the protocol FlexASIO speaks
// through its WDM-KS backend, embedded in twinspeak and paced harder:
//
//   filter  : a KSCATEGORY_RENDER wave filter opened as a file
//             (\\?\hdaudio#... or the USB Audio device object)
//   pin     : a render sink pin instantiated with KsCreatePin
//             (ksuser.dll) under KSPIN_CONNECT + WAVEFORMATEXTENSIBLE
//   buffer  : the WaveRT looped buffer -- the driver's own DMA memory,
//             mapped into this process (KSPROPERTY_RTAUDIO_BUFFER)
//   clock   : the hardware play-position register mapped read-only
//             (KSPROPERTY_RTAUDIO_POSITIONREGISTER), or KSPROPERTY_AUDIO_
//             POSITION polled through the pin when no register exists
//   pacing  : our own TIME-CRITICAL MMCSS thread -- write exactly
//             ks_period_frames frames when the write cursor threatens to
//             slip below the lead; sleep while far, spin over the last
//             millisecond. No events, no engine, no scheduler intermediaries.
//
// The audio that crosses bufferSwitchFill() is bit-identical to the WASAPI
// path; only who paces the DMA changes. Position semantics follow the
// WaveRT specification and the reference WDM-KS backends: the register /
// PlayOffset is a BYTE offset inside the looped buffer.
//
// NOTE on pin instances: a KS pin is a direct line to the DAC. Windows
// endpoint volume, endpoint muting and the engine's own mixing do not
// apply while twinspeak holds it; volume_follow still governs the leg
// (applied in the pump, exactly as for WASAPI legs).

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
            // The residue integration below can only see advances MOD the
            // loop: a poll gap of one lap or more (a deschedule under an
            // interrupt storm, system suspend) silently loses k whole laps,
            // the believed play position falls k loops behind, and the
            // pacing sleeps on a lead that no longer exists -- the DAC
            // circles the loop re-reading stale audio (the reverberated
            // sawing) with heartbeats, und, the ledger and devbuf all
            // clean. Detection: the DAC's true advance over the gap is
            // wall-exact (elapsed x rate); the residue advance is mod-loop.
            // Any lap-sized gap, or any residue advance disagreeing with
            // the wall expectation beyond a tolerance, means laps were
            // lost: re-pin the write cursor to play+lead and re-prime the
            // lead region (replay convention) -- a bit-exact skip of the
            // lost window.
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
        "; twinspeak v6.5.15 config -- BIT-EXACT ASIO-strict engine: out1 = L/R + LFE fold,\n"
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
        "; CAPTURE pin mode: exclusive = take the VAC recording pin WASAPI-\n"
        "; exclusive (on a WaveRT port this is the direct cyclic-buffer path,\n"
        "; ~10 ms cable->app). If the pin refuses (engine holds it, WaveCyclic\n"
        "; port, format mismatch) twinspeak falls back to shared and says so.\n"
        "capture_exclusive = on\n"
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
        "; WASAPI exclusive output (kernel WaveRT path: no engine, no\n"
        "; enhancements, ~10 ms less device buffering). Bypasses the Windows\n"
        "; volume slider. If either driver refuses, align_modes reopens BOTH\n"
        "; legs as shared automatically, so device buffering stays identical.\n"
        "exclusive = on\n"
        "\n"
        "; Force both legs into the same share mode (recommended -- mixed modes\n"
        "; are exactly what made out2 lag out1 in v2). off = per-leg fallback.\n"
        "align_modes = on\n"
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
        "pin_outputs = on\n"
        "out2_first = on\n"
        "\n"
        "; KEEP-ALIVE: out2 sums a sine on both channels every frame (silence or\n"
        "; not) so the center device never sees digital silence. Frequency and\n"
        "; level are tunable. 40 Hz @ 1%% = -40 dBFS, far below hearing -- BUT\n"
        "; some USB amps (the Xtream S5 included) sleep on anything under their\n"
        "; detect threshold. If the S5 still powers down while twinspeak runs,\n"
        "; raise keepalive_gain (0.03..0.05 is still inaudible at 40 Hz) or move\n"
        "; keepalive_hz. The watchdog reopens out2 automatically whenever it\n"
        "; comes back.\n"
        "keepalive_hz = 40\n"
        "keepalive_gain = 0.01\n"
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
        "; BACKGROUND (v6.2): background = on runs twinspeak as a background\n"
        "; process. The console detaches AFTER a successful startup (a failed\n"
        "; start keeps the window so you can read the error) and everything --\n"
        "; banner, sync lines, health line -- is logged to twinspeak.log next\n"
        "; to this ini (health line every 10 s in the file). Stop a background\n"
        "; run with:   taskkill /IM twinspeak.exe\n"
        "; Start it visible anyway with:   twinspeak --foreground\n"
        "; A second instance is refused (single-instance lock; add --multi to\n"
        "; allow one deliberately). off = classic console run, Ctrl+C to stop.\n"
        "background = off\n"
        "\n"
        "; DAC YIELD (v6.5.14): when the WINDOWS DEFAULT render endpoint is (or\n"
        "; becomes) one of the leg devices below, that leg's DAC is released\n"
        "; immediately -- the KS pin closes / the WASAPI device frees, the pump\n"
        "; parks -- so the app playing to the default device owns the hardware\n"
        "; with nothing fighting it. The leg reopens by itself (chime + warden\n"
        "; re-arm included) the moment the default moves elsewhere. The log\n"
        "; announces both directions ('legN YIELD'); a yielded leg shows '----'\n"
        "; on the devbuf line and a stale heartbeat, both BY DESIGN. While a\n"
        "; leg is yielded the program touches NOTHING on that device -- its\n"
        "; Windows volume (and mute) stay exactly where the user set them;\n"
        "; the 100%% pin re-applies only when the leg reopens for twinspeak.\n"
        "yield_default = on\n"
        "\n"
        "; WDM-KS OUTPUT BACKEND (v6.3) -- the FlexASIO path, inside twinspeak.\n"
        "; backend1 / backend2 route each leg through Kernel Streaming instead of\n"
        "; WASAPI: a direct render pin on the device's wave filter (KsCreatePin),\n"
        "; the WaveRT looped buffer as the device's own DMA memory, and the\n"
        "; hardware play-position register mapped read-only -- twinspeak's own\n"
        "; TIME-CRITICAL thread paces the stream against the DAC itself, with no\n"
        "; events, no engine and no scheduler intermediaries in between. The\n"
        "; audio crossing the buffer switch is bit-identical (same rings, same\n"
        "; replay/drop rules, same gates); only the transport changes.\n"
        ";   backendN = wasapi  the proven v6.2 path (miniaudio, exclusive WaveRT)\n"
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
        "; pin_outputs is a no-op on a KS leg -- volume_follow still governs it\n"
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
        "; clock_sync = off disables BOTH rules (A/B only: shortfall pads\n"
        "; silence, excess evicts at the ring cap).\n"
        "clock_sync = on\n"
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
        "; drift_guard = off keeps the ledger + the drift column but disables\n"
        "; the automatic re-pin (observe-only).\n"
        "drift_guard = on\n"
        "drift_correct_ms = 10\n"
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
        "power_guard = on\n"
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
        ";     must read 0. Set center_upmix_guard = off only if it ever mutes\n"
        ";     genuine center dialogue (the log names the fitted gains).\n"
        "center_gate = on\n"
        "center_upmix_guard = on\n"
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
        "; NOTE: output2 always sums the keep-alive sine (keepalive_hz @\n"
        "; keepalive_gain, default 40 Hz @ 1%%) onto both channels, every\n"
        "; frame, so the center device NEVER sees digital silence -- and when\n"
        "; the center gates mute a block, the keep-alive is ALL that block\n"
        "; carries. The out2 pump also runs FIRST and at the higher priority\n"
        "; (out2_first): the center leg gets the freshest data and the most\n"
        "; scheduling attention; delay1_ms/delay2_ms then trim the alignment.\n");
    fclose(f);
}

static float clampMs(float ms) {
    return ms < 2.0f ? 2.0f : (ms > 100.0f ? 100.0f : ms);
}

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
        else if (key == "capture_exclusive") { g_cfg.captureExclusive = !isOff(val); }
        else if (key == "buffer_size")  { g_cfg.legacyBufferMs = (float)atof(val.c_str()); }
        else if (key == "exclusive")    { g_cfg.exclusive = !isOff(val); }
        else if (key == "align_modes")  { g_cfg.alignModes = !isOff(val); }
        else if (key == "lfe_fold")     { g_cfg.lfeFold = !isOff(val); }
        else if (key == "input_device")   g_cfg.inputDevice = val;
        else if (key == "output_device1") g_cfg.outputDevice1 = val;
        else if (key == "output_device2") g_cfg.outputDevice2 = val;
        else if (key == "gain_l")         g_cfg.gainL = (float)atof(val.c_str());
        else if (key == "gain_r")         g_cfg.gainR = (float)atof(val.c_str());
        else if (key == "gain_lfe")       g_cfg.gainLfe = (float)atof(val.c_str());
        else if (key == "gain_center")    g_cfg.gainCenter = (float)atof(val.c_str());
        else if (key == "clock_sync")     g_cfg.clockSync = !isOff(val);
        else if (key == "auto_resync")    g_cfg.clockSync = !isOff(val);  // v3.7-v5.0 key alias
        else if (key == "delay1_ms")      g_cfg.delay1Ms = (float)atof(val.c_str());
        else if (key == "delay2_ms")      g_cfg.delay2Ms = (float)atof(val.c_str());
        else if (key == "center_gate")        g_cfg.centerGate      = !isOff(val);
        else if (key == "center_upmix_guard") g_cfg.centerUpmixGuard = !isOff(val);
        else if (key == "volume_follow")  g_cfg.volumeFollow = !isOff(val);
        else if (key == "pin_outputs")    g_cfg.pinOutputs   = !isOff(val);
        else if (key == "out2_first")     g_cfg.out2First    = !isOff(val);
        else if (key == "volume_endpoint") g_cfg.volumeEndpoint = val;
        else if (key == "keepalive_hz")   g_cfg.keepaliveHz   = (float)atof(val.c_str());
        else if (key == "keepalive_gain") g_cfg.keepaliveGain = (float)atof(val.c_str());
        else if (key == "background")     g_cfg.background    = !isOff(val);   // v6.2
        else if (key == "chimes")         g_cfg.chimes        = !isOff(val);   // v6.2
        else if (key == "chime_gain")     g_cfg.chimeGain     = (float)atof(val.c_str()); // v6.2
        else if (key == "backend1")       g_cfg.backend1      = lower(val);    // v6.3
        else if (key == "backend2")       g_cfg.backend2      = lower(val);    // v6.3
        else if (key == "ks_period_frames") g_cfg.ksPeriodFrames = (unsigned)atoi(val.c_str()); // v6.3
        else if (key == "ks_periods")      g_cfg.ksPeriods      = (unsigned)atoi(val.c_str()); // v6.3
        else if (key == "drift_guard")      g_cfg.driftGuard     = !isOff(val);  // v6.4
        else if (key == "drift_correct_ms") g_cfg.driftCorrectMs = (float)atof(val.c_str()); // v6.4
        else if (key == "drift_interval_ms") g_cfg.driftIntervalMs = (unsigned)atoi(val.c_str()); // v6.5.8
        else if (key == "deep_resync_min")   g_cfg.deepResyncMin   = (unsigned)atoi(val.c_str()); // v6.5.12
        else if (key == "yield_default")     g_cfg.yieldDefault    = !isOff(val); // v6.5.14
        else if (key == "power_guard")      g_cfg.powerGuard     = !isOff(val);  // v6.5.2
    }
    fclose(f);
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

// v6.5.14: the friendly name of the CURRENT default render endpoint, via a
// LIVE re-enumeration (ma_context_enumerate_devices re-walks the endpoints
// on every call, so a default-device switch is visible immediately; the
// cached ma_context_get_devices list is not). Consumed by the health loop's
// DAC-yield check.
struct YieldEnum { std::string defName; };
static ma_bool32 yieldEnumCb(ma_context*, ma_device_type deviceType,
                             const ma_device_info* pInfo, void* pUserData) {
    if (deviceType != ma_device_type_playback) return MA_TRUE;
    YieldEnum* d = (YieldEnum*)pUserData;
    if (pInfo->isDefault && d->defName.empty()) d->defName = pInfo->name;
    return MA_TRUE;
}
static bool defaultRenderName(ma_context* ctx, std::string* outName) {
    YieldEnum d;
    if (ma_context_enumerate_devices(ctx, yieldEnumCb, &d) != MA_SUCCESS) return false;
    if (d.defName.empty()) return false;
    *outName = d.defName;
    return true;
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
// The "output shut off by itself while twinspeak still runs" class of
// failure: a device thread silently stops (endpoint invalidated, USB
// re-enumeration, driver reset). The health loop reopens the leg BY NAME
// (fresh endpoint id -- a replug re-attaches), first in its previous share
// mode, then shared. Called once per second while the heartbeat is stale;
// retries are silent until the state changes or the reopen succeeds.
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

// ---------------------------------------------------------------- main ------
static BOOL WINAPI ctrlHandler(DWORD) { g_run.store(false); return TRUE; }

int main(int argc, char** argv) {
    InitializeCriticalSection(&g_logCS);   // v5.0 logger
    std::string iniPath;
    bool doList = false, doListKS = false, fgForce = false, allowMulti = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--list")) doList = true;
        else if (!strcmp(argv[i], "--list-ks")) doListKS = true;   // v6.3
        else if (!strcmp(argv[i], "--ini") && i + 1 < argc) iniPath = argv[++i];
        else if (!strcmp(argv[i], "--foreground")) fgForce = true;    // v6.2
        else if (!strcmp(argv[i], "--multi"))      allowMulti = true; // v6.2
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            tlog("usage: twinspeak [--list] [--list-ks] [--ini path] [--foreground] [--multi]\n"
                 "  --list        show every WASAPI endpoint (the usual device needles)\n"
                 "  --list-ks     show every WDM-KS render filter with its pins/formats (v6.3)\n"
                 "  --foreground  ignore background = on for this run (keep the console)\n"
                 "  --multi       allow a second instance (skip the single-instance lock)\n");
            return 0;
        } else iniPath = argv[i];
    }

    if (doList) { listDevices(); return 0; }
    if (doListKS) { ksListKs(); return 0; }   // v6.3

    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string exeDir = exePath;
    size_t sl = exeDir.find_last_of("\\/");
    if (sl != std::string::npos) exeDir = exeDir.substr(0, sl); else exeDir = ".";
    if (iniPath.empty()) iniPath = exeDir + "\\twinspeak.ini";

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

    // v6.2 single-instance lock: two engines would fight over the same
    // exclusive pins. background = on makes this easy to hit (the running
    // copy may have no window at all), so the refusal names the exact cure.
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

    // keep-alive cycle table (v3.9: keepalive_hz / keepalive_gain from ini)
    g_kaHz = g_cfg.keepaliveHz;
    if (g_kaHz < 5.0f)    g_kaHz = 5.0f;
    if (g_kaHz > 2000.0f) g_kaHz = 2000.0f;
    g_kaGain = g_cfg.keepaliveGain;
    if (g_kaGain < 0.0f)  g_kaGain = 0.0f;
    if (g_kaGain > 0.25f) g_kaGain = 0.25f;
    g_kaLen = (unsigned)((double)kSampleRate / (double)g_kaHz + 0.5);
    if (g_kaLen < 8) g_kaLen = 8;
    if (g_kaLen > kKeepAliveTableMax) g_kaLen = kKeepAliveTableMax;
    for (unsigned i = 0; i < g_kaLen; ++i)
        g_kaTable[i] = (float)(sin(kTwoPi * (double)g_kaHz * (double)i / (double)kSampleRate)
                               * (double)g_kaGain);

    // v6.2 chimes: render the modernized CenterFold signature ONCE -- two
    // short triangle blips in the low treble, the old rising fifth (G5->D6
    // square) two-plus octaves up: C7 2093 Hz (42 ms) -> G7 3136 Hz (72 ms),
    // 20 ms gap, exponential decay. Peak = chime_gain clamped to 0..1. It
    // plays when the engine goes live (armed right before the legs start)
    // and again whenever the watchdog reopens the center leg.
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

    // v3.6: the CAPTURE side gets its own period and its own share mode,
    // opened BEFORE the rings (the ring floor needs the REAL negotiated
    // capture burst). Exclusive on the VAC pin = the direct WaveRT
    // cyclic-buffer read on a WaveRT port (~10 ms cable->app); on a
    // WaveCyclic port or when the pin is busy it usually refuses, and we
    // fall back to shared automatically (banner reports which one won).
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

    // v4.0: the REAL negotiated device period per leg (the shared engine
    // clamps; exclusive honors small values) feeds the ring floor and the
    // v6.0 gated-drop line. The real capture burst feeds the ring floor.
    // v6.3: a KS leg's device bite is exactly ks_period_frames (our own
    // pacing thread never asks for more), so that IS its device period.
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

    // rings, now that each leg's real device period AND the real capture
    // burst are known. Target floor: the FULL device buffer + a full burst
    // + 4 ms (the ring must cover the device's worst legitimate bite -- in
    // miniaudio's exclusive event mode the pop is the WHOLE device buffer,
    // pulled as two back-to-back period callbacks); capacity = target +
    // spare so the normal pop/burst sawtooth never reaches the overwrite
    // line. This target is the LATENCY ANCHOR of the bit-exact path.
    for (int i = 0; i < 2; ++i) {
        Leg& L = g_leg[i];
        const float ms = (i == 0) ? g_cfg.out1Ms : g_cfg.out2Ms;
        float ringMs = (i == 0) ? g_cfg.ring1Ms : g_cfg.ring2Ms;
        const float delayMs = (i == 0) ? g_cfg.delay1Ms : g_cfg.delay2Ms;
        if (ringMs < 0.0f) ringMs = ms;   // v3.2: ring1_ms/ring2_ms default to outXms
        const size_t cfgFrames    = (size_t)((double)kSampleRate * ringMs / 1000.0);
        // v6.3: a KS leg's device bite is one period exactly (our pacing
        // thread writes period frames and never merges); the floor is thus
        // ONE device period + burst + margin.
        // v6.3.1: the margin is PER-TRANSPORT. A KS leg's pull is twinspeak's
        // own pacing thread -- exactly one bite, paced against the DAC's
        // position register, refilled by a pump that drains every 1 ms --
        // so the floor needs only 1 ms of jitter on top of (bite + burst).
        // A WASAPI leg keeps the full 4 ms margin: miniaudio's exclusive
        // event mode pops the WHOLE buffer as two back-to-back callbacks,
        // and the floor must survive that bite.
        const size_t devPeriodFr  = (L.backend == 1)
                                  ? (size_t)g_cfg.ksPeriodFrames
                                  : (L.dev.playback.internalPeriodSizeInFrames
                                     ? L.dev.playback.internalPeriodSizeInFrames : 480);
        const size_t marginFr     = (L.backend == 1) ? kKsMarginFrames
                                                     : kPeriodMarginFrames;
        // v6.1: the alignment delay IS extra ring depth -- the ring target
        // grows by delay_ms and the samples leave it that much later, bit
        // for bit. The gated-drop line (target + burst + period) rides on
        // the SAME target, so the delay cannot cause false excess drops;
        // the prefill covers the delay, so playback starts pre-delayed.
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
    }

    // ---------------- v3.8 COM volume layer (setup on the main thread) -----
    // Before any device starts: resolve the cable's PLAYBACK-side endpoint
    // (name match against input_device), seed the volume atomics, resolve
    // the out1/out2 endpoint pins and force 100%/unmuted. The watcher
    // thread takes over both jobs once the devices are running.
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
    // returned with the console intact and the reason on screen; from here
    // on the run is good, so the banner, sync lines and health line all go
    // into twinspeak.log next to the ini (the console note is the last
    // thing the window ever shows). Everything twinspeak logs funnels
    // through stdout (tlog), so one freopen is the whole mechanism.
    // FreeConsole closes a console the process owns; a console shared with
    // the shell the user ran us from simply stays theirs. Stop a background
    // run: taskkill /IM twinspeak.exe
    if (g_cfg.background && !fgForce) {
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
    } else if (g_cfg.background && fgForce) {
        tlog("[twinspeak] background    : on in the ini, overridden by --foreground -- keeping the console\n");
    }

    tlog("[twinspeak] v6.5.15        : BIT-EXACT ASIO-strict engine -- the audio path is memcpy; twinspeak\n");
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
    // v6.2: the clock hint fires only on a SETTLED path -- at least 10 s
    // up, a quiet measurement window (no corrections, no replays inside
    // it), and the same-sign exceedance held across THREE consecutive
    // windows at more than kHintPpm. Two reasons: the meter itself is
    // quantized by the ~2 ms device switch (one period = 2000 ppm of
    // per-window jitter, +-4000 under load), so 1000 ppm -- the v6.0/v6.1
    // threshold -- sat BELOW the noise floor; and the startup transient
    // (initial replays reading as -10000..-13944 ppm for a second or two)
    // printed a false "steady clock offset" alarm seconds after launch.
    static constexpr long kHintPpm = 5000;   // above the one-period meter quantum
    unsigned sec = 0;
    int hintStreak = 0;
    long prevDom = 0;
    unsigned stallSec[2] = { 0, 0 };   // v6.5.11: consecutive starved seconds per leg
    unsigned long long deepLastMs = 0; // v6.5.12: scheduled deep-resync clock
    while (g_run.load()) {
        Sleep(1000);
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
        // v6.0 fields: held = ring + raw backlog per leg (the latency anchor
        // is the ring target printed in the banner); off = measured clock
        // offset ppm; und = buffer switches padded by the replay rule;
        // drop = frames discarded by passive overwrite (raw + ring cap);
        // corr = gated excess-drop events. v6.2: computed BEFORE the hint
        // so the hint can require a quiet window.
        unsigned long long d0 = g_leg[0].rawDrop.load() + g_leg[0].outDrop.load(),
                           d1 = g_leg[1].rawDrop.load() + g_leg[1].outDrop.load(),
                           u0 = g_leg[0].und.load(),        u1 = g_leg[1].und.load(),
                           k0 = g_leg[0].corrDrops.load(),  k1 = g_leg[1].corrDrops.load(),
                           m1 = g_leg[1].cmUpmix.load(),    // v6.1 upmix-guarded blocks
                           q0 = g_leg[0].padTotal.load(),   q1 = g_leg[1].padTotal.load(); // v6.4
        // v6.4 DRIFT LEDGER: the two legs' play boundaries in capture-sequence
        // space. Their difference IS the inter-speaker offset in cable time
        // (1 ms = 48 frames) -- replays/pads correctly do NOT advance it,
        // pops/drops/skips correctly DO.
        const long long st0 = (long long)g_leg[0].out.seqTail.load(std::memory_order_relaxed);
        const long long st1 = (long long)g_leg[1].out.seqTail.load(std::memory_order_relaxed);
        const bool driftOK = (st0 > 0 && st1 > 0);
        const double driftMs = driftOK ? (double)(st0 - st1) / 48.0 : 0.0;
        char driftStr[16];
        if (driftOK) snprintf(driftStr, sizeof(driftStr), "%+5.1f", driftMs);
        else         snprintf(driftStr, sizeof(driftStr), "%6s", "----");
        // v6.0 one-shot field hint, v6.2 SETTLED-PATH GATING: a steady
        // offset far beyond crystal level means the cable's virtual clock
        // is free-running (VAC Clock Correction off). v6.0 is bit-exact --
        // it never hides the offset behind a resampler; it measures it and
        // hands you the number -- but only after the path has settled.
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
        // main device play." The default render endpoint is re-enumerated
        // every tick; a needle match releases THAT leg's DAC the same
        // second (KS: pin closed + pacer stopped; WASAPI: device freed) --
        // nothing of ours holds the hardware, so the app playing to the
        // default device owns it outright. When the default moves elsewhere
        // the leg reopens through the watchdog path (chime + baseline
        // re-arm included). While yielded: the pump parks (the raw ring
        // overflows harmlessly, exactly like the suspend park), the
        // watchdog skips the leg (the stale heartbeat is a deliberate
        // release, not a fault), the collapse sentinel / stall resync /
        // deep resync / warden corrections all hold off, and the devbuf
        // line prints '----'. Reaction time: one health tick (<= 1 s).
        if (g_cfg.yieldDefault && !g_powerPark.load(std::memory_order_relaxed)) {
            std::string defName;
            if (defaultRenderName(&ctx, &defName)) {
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
                        // v6.5.15: drop the endpoint-volume pin WITH the DAC.
                        // While this device is the system's main output its
                        // volume is the user's; pinCheckTick must have nothing
                        // to stomp 100%/unmuted onto. The normal reopen path
                        // re-pins on yield release, when we own it again.
                        if (g_pin[i].vol) { g_pin[i].vol->Release(); g_pin[i].vol = nullptr; }
                        g_pin[i].id.clear();
                        Y.lastReopenMs = nowMs;   // warden settle gates stay coherent
                    } else if (!mine && Y.yielded.exchange(false, std::memory_order_relaxed)) {
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
        // 1-s windows is physically impossible for a healthy 48 kHz link. A
        // half-alive post-sleep stream ticks at a fraction of the real rate
        // with a FRESH heartbeat -- only this meter can see it. Also catches
        // the degraded sleep-ENTRY window (it fires before the broadcast)
        // and any driver reset that leaves a bogus-rate thread running.
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
        // A starved device thread (interrupt storm, a 100% CPU hog) trips
        // the shortfall path -- replayFill loops the last fragment (the
        // synthetic buzz) -- and the leg ends up BEHIND: while it starved,
        // the pump kept feeding its ring, so stale backlog piled up above
        // the target anchor and the stream position slipped. Recovery:
        //   FAST: und this second + backlog above the anchor -> discard the
        //     excess NOW (bit-exact skip; the warden's own operation, but
        //     per-leg, on first evidence, no streak/interval wait).
        //   DEEP: starvation 3 seconds running -> reopen the leg fresh
        //     (resets the device-side FIFO state the storm disturbed --
        //     exactly the S5's below-packetizer mystery layer).
        // Gated on the drift baseline being armed (no boot/prefill or
        // post-reopen transients) and the suspend park. Capped at 100 ms
        // per discard; the reopen path respects the 10 s per-leg spacing.
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
        // The S5's below-packetizer layer (endpoint FIFO + amp DSP silicon)
        // can deepen silently over a long session: the drift ledger stays
        // pinned (host paths stay equal) while the center audibly lags
        // again. No host meter sees that layer -- the devbuf line reads the
        // HOST side of the packetizer -- so the only reset that re-primes
        // it is a fresh reopen of the leg (the stall resync's deep path,
        // but on a schedule instead of on starvation evidence). Gated on
        // the baseline being armed and the suspend park; the reopen sets
        // the usual baseline-invalidation, so the warden re-arms ~9 s
        // later and the delay calibration carries across untouched.
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
        // Baseline: the settled path's average drift (seconds 5..9, both
        // heartbeats fresh, no reopen inside 5 s -- the baseline includes
        // the ini delay offsets, i.e. the user's own delay1_ms calibration).
        // Correction: |drift - baseline| >= drift_correct_ms held for 3
        // consecutive windows, at most once per drift_interval_ms (the
        // v6.5.10 five-second resync default): the late leg's ring
        // discards its excess (bit-exact skip), the early leg is held (its
        // last buffer replays in place). Never fires on a dead/reopening leg.
        if (driftOK) {
            const bool settled = (nowMs - g_leg[0].lastReopenMs > 5000)
                              && (nowMs - g_leg[1].lastReopenMs > 5000)
                              && (nowMs - hbOutGet(0) < 5000)
                              && (nowMs - hbOutGet(1) < 5000);
            // v6.5.8: a reopen (watchdog leg/capture, power-guard resume) can
            // legitimately move the path's real latency afterwards -- the boot
            // baseline goes stale, so it is invalidated and re-armed once the
            // path settles again (~9 s: 5 s settle + a 4-sample average).
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
        // v6.5.1: err -- the warden's own number, the live distance from the
        // calibrated baseline. drift is the RAW inter-speaker offset (the
        // delay calibration included); err is what the correction rule acts
        // on. Computed here, AFTER the warden step, so the calibration lands
        // in the same second it happens.
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
        // The align line's est path is arithmetic on CONFIGURED sizes. This
        // is the live reading, from the authoritative source per transport:
        //   WASAPI shared    -> IAudioClient::GetCurrentPadding (the live pad)
        //   WASAPI exclusive -> the whole device buffer (WaveRT exclusive
        //                       drivers misreport padding as 0, and miniaudio
        //                       pops the FULL buffer per event, so the buffer
        //                       size IS the honest pad);
        //   WDM-KS           -> the pacing thread's write-minus-play lead off
        //                       the DAC position register (the live sawtooth).
        // Telemetry only, best-effort: a failed call just prints ----.
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
