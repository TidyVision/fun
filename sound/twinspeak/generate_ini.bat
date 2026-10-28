@echo off
rem twinspeak v6.5.1 -- generate_ini.bat
rem Writes twinspeak.ini next to this script, backing up any previous
rem ini to twinspeak.ini.bak. INTERACTIVE: it asks for the keys that
rem get tuned on this rig -- every prompt shows its tuned default in
rem [brackets], and pressing Enter keeps it. An advanced key=value
rem prompt can override any ini key. A refused answer keeps the
rem default, and nothing is written until every prompt is done.
setlocal EnableExtensions
set "INI=%~dp0twinspeak.ini"
set "BAK=%~dp0twinspeak.ini.bak"

if "%~1"=="" goto :noargs
echo This version is interactive -- run it with no arguments and
echo answer the prompts. Nothing was changed.
goto :quit
:noargs

echo.
echo twinspeak v6.5.1 -- twinspeak.ini generator
echo ============================================
echo.
echo What this script does:
echo   [1/5] ask for your own values -- Enter keeps each tuned default
echo   [2/5] back up any existing twinspeak.ini to twinspeak.ini.bak
echo   [3/5] write twinspeak.ini with your values -- quiet on purpose,
echo         every line goes into the FILE, not the screen
echo   [4/5] verify the written file, key by key
echo   [5/5] print the summary
echo.
echo How the prompts work: each one shows its tuned default in
echo [brackets]. Press Enter to keep it, or type your own value
echo first -- holding Enter from here accepts every default. Keys
echo you are not asked about keep their tuned values; the advanced
echo key=value prompt at the end of step 1 can override any of them.
echo.
echo Tip: clicking inside a console window freezes its output until
echo you press Enter once. That Enter belongs to the selection,
echo not to this script -- nothing was lost.
echo.
set "v_out1ms=1"
set "v_out2ms=1"
set "v_cap_ms=1"
set "v_capture_exclusive=on"
set "v_ring1_ms=1"
set "v_ring2_ms=1"
set "v_delay1_ms=65"
set "v_delay2_ms=0"
set "v_exclusive=on"
set "v_align_modes=on"
set "v_lfe_fold=on"
set "v_input_device=Line 1"
set "v_output_device1=Realtek"
set "v_output_device2=Xtream"
set "v_gain_l=1.0"
set "v_gain_r=1.0"
set "v_gain_lfe=0.4"
set "v_gain_center=1.0"
set "v_volume_follow=on"
rem v_volume_endpoint: tuned default is empty -- stays undefined unless overridden
set "v_pin_outputs=on"
set "v_out2_first=on"
set "v_keepalive_hz=40"
set "v_keepalive_gain=0.01"
set "v_chimes=on"
set "v_chime_gain=0.4"
set "v_background=off"
set "v_backend1=wasapi"
set "v_backend2=wasapi"
set "v_ks_period_frames=96"
set "v_ks_periods=4"
set "v_clock_sync=on"
set "v_drift_guard=on"
set "v_drift_correct_ms=10"
set "v_center_gate=on"
set "v_center_upmix_guard=on"
echo [1/5] Your settings.
echo.
echo -- devices (substring match; see: twinspeak --list)
call :ask_text v_input_device "  input_device" "Line 1"
call :ask_text v_output_device1 "  output_device1" "Realtek"
call :ask_text v_output_device2 "  output_device2" "Xtream"
echo.
echo -- KS transport (backendN = wasapi is the recommended default)
call :ask_backend v_backend1 "  backend1" "wasapi"
call :ask_backend v_backend2 "  backend2" "wasapi"
call :ask_num v_ks_period_frames "  ks_period_frames" "96"
call :ask_num v_ks_periods "  ks_periods" "4"
echo.
echo -- speaker alignment (bit-exact ring delay per leg, in ms)
call :ask_num v_delay1_ms "  delay1_ms" "65"
call :ask_num v_delay2_ms "  delay2_ms" "0"
echo.
echo -- guards, keep-alive, background
call :ask_onoff v_drift_guard "  drift_guard" "on"
call :ask_num v_keepalive_gain "  keepalive_gain" "0.01"
call :ask_onoff v_background "  background" "off"
echo.
echo -- advanced: any other key, as key=value (a value may contain
echo    spaces; an empty value is refused; Enter finishes step 1)
:advloop
set "PAIR="
set /p "PAIR=  key=value, or Enter to finish: "
if not defined PAIR goto :advdone
if not "%PAIR%"=="%PAIR:&=_%" goto :advbad
if not "%PAIR%"=="%PAIR:<=_%" goto :advbad
if not "%PAIR%"=="%PAIR:>=_%" goto :advbad
if not "%PAIR%"=="%PAIR:|=_%" goto :advbad
if not "%PAIR%"=="%PAIR:^=_%" goto :advbad
if not "%PAIR%"=="%PAIR:"=_%" goto :advbad
echo(%PAIR%| findstr /c:"%%" >nul && goto :advbad
set "K="
set "VAL="
for /f "tokens=1,* delims==" %%a in ("%PAIR%") do (
    set "K=%%a"
    set "VAL=%%b"
)
if not defined K goto :advbad
if not defined VAL goto :advbad
if /i "%K%"=="out1ms" (
    set "v_out1ms=%VAL%"
    goto :advok
)
if /i "%K%"=="out2ms" (
    set "v_out2ms=%VAL%"
    goto :advok
)
if /i "%K%"=="cap_ms" (
    set "v_cap_ms=%VAL%"
    goto :advok
)
if /i "%K%"=="capture_exclusive" (
    set "v_capture_exclusive=%VAL%"
    goto :advok
)
if /i "%K%"=="ring1_ms" (
    set "v_ring1_ms=%VAL%"
    goto :advok
)
if /i "%K%"=="ring2_ms" (
    set "v_ring2_ms=%VAL%"
    goto :advok
)
if /i "%K%"=="delay1_ms" (
    set "v_delay1_ms=%VAL%"
    goto :advok
)
if /i "%K%"=="delay2_ms" (
    set "v_delay2_ms=%VAL%"
    goto :advok
)
if /i "%K%"=="exclusive" (
    set "v_exclusive=%VAL%"
    goto :advok
)
if /i "%K%"=="align_modes" (
    set "v_align_modes=%VAL%"
    goto :advok
)
if /i "%K%"=="lfe_fold" (
    set "v_lfe_fold=%VAL%"
    goto :advok
)
if /i "%K%"=="input_device" (
    set "v_input_device=%VAL%"
    goto :advok
)
if /i "%K%"=="output_device1" (
    set "v_output_device1=%VAL%"
    goto :advok
)
if /i "%K%"=="output_device2" (
    set "v_output_device2=%VAL%"
    goto :advok
)
if /i "%K%"=="gain_l" (
    set "v_gain_l=%VAL%"
    goto :advok
)
if /i "%K%"=="gain_r" (
    set "v_gain_r=%VAL%"
    goto :advok
)
if /i "%K%"=="gain_lfe" (
    set "v_gain_lfe=%VAL%"
    goto :advok
)
if /i "%K%"=="gain_center" (
    set "v_gain_center=%VAL%"
    goto :advok
)
if /i "%K%"=="volume_follow" (
    set "v_volume_follow=%VAL%"
    goto :advok
)
if /i "%K%"=="volume_endpoint" (
    set "v_volume_endpoint=%VAL%"
    goto :advok
)
if /i "%K%"=="pin_outputs" (
    set "v_pin_outputs=%VAL%"
    goto :advok
)
if /i "%K%"=="out2_first" (
    set "v_out2_first=%VAL%"
    goto :advok
)
if /i "%K%"=="keepalive_hz" (
    set "v_keepalive_hz=%VAL%"
    goto :advok
)
if /i "%K%"=="keepalive_gain" (
    set "v_keepalive_gain=%VAL%"
    goto :advok
)
if /i "%K%"=="chimes" (
    set "v_chimes=%VAL%"
    goto :advok
)
if /i "%K%"=="chime_gain" (
    set "v_chime_gain=%VAL%"
    goto :advok
)
if /i "%K%"=="background" (
    set "v_background=%VAL%"
    goto :advok
)
if /i "%K%"=="backend1" (
    set "v_backend1=%VAL%"
    goto :advok
)
if /i "%K%"=="backend2" (
    set "v_backend2=%VAL%"
    goto :advok
)
if /i "%K%"=="ks_period_frames" (
    set "v_ks_period_frames=%VAL%"
    goto :advok
)
if /i "%K%"=="ks_periods" (
    set "v_ks_periods=%VAL%"
    goto :advok
)
if /i "%K%"=="clock_sync" (
    set "v_clock_sync=%VAL%"
    goto :advok
)
if /i "%K%"=="drift_guard" (
    set "v_drift_guard=%VAL%"
    goto :advok
)
if /i "%K%"=="drift_correct_ms" (
    set "v_drift_correct_ms=%VAL%"
    goto :advok
)
if /i "%K%"=="center_gate" (
    set "v_center_gate=%VAL%"
    goto :advok
)
if /i "%K%"=="center_upmix_guard" (
    set "v_center_upmix_guard=%VAL%"
    goto :advok
)
goto :advunknown
:advok
set /a NOVERRIDES+=1
echo       set: %K% = %VAL%
goto :advloop
:advunknown
echo       unknown key "%K%" -- ignored. Enter on the empty prompt finishes.
goto :advloop
:advbad
echo       refused: plain key=value only -- no metacharacters, no empty value.
goto :advloop
:advdone
echo.
echo [2/5] Checking for an existing twinspeak.ini ...
if exist "%INI%" copy /y "%INI%" "%BAK%" >nul
if exist "%INI%" echo       found one -- backed up to twinspeak.ini.bak
if not exist "%INI%" echo       none found -- fresh install, nothing to back up.

echo [3/5] Writing twinspeak.ini -- this step looks quiet on purpose:
echo       every line goes into the file. It finishes in well under
echo       a second.
> "%INI%" echo ; twinspeak v6.5.1 config -- BIT-EXACT ASIO-strict engine: out1 = L/R + LFE fold,
>> "%INI%" echo ; out2 = center ONLY (every other channel muted), no resampler anywhere.
>> "%INI%" echo.
>> "%INI%" echo ; out1ms / out2ms set each leg's requested WASAPI device period. Keep them
>> "%INI%" echo ; EQUAL -- equal periods keep the legs time-aligned. Exclusive honors small
>> "%INI%" echo ; values; shared mode clamps to ~10 ms. v6.2 ships 5 ms: path = ring (~19 ms)
>> "%INI%" echo ; + device buffer (10 ms) + capture burst (~5 ms) = ~34 ms. If a driver
>> "%INI%" echo ; refuses 5 ms the banner says so -- set that leg to 10 (path ~54 ms).
>> "%INI%" echo out1ms = %v_out1ms%
>> "%INI%" echo out2ms = %v_out2ms%
>> "%INI%" echo.
>> "%INI%" echo ; CAPTURE burst period -- decoupled from the out periods above. Smaller =
>> "%INI%" echo ; finer capture granularity = lower and more stable leg latency. 5 ms is
>> "%INI%" echo ; the sweet spot. In shared mode the Windows engine clamps this to ~10 ms
>> "%INI%" echo ; -- the banner prints the REAL negotiated burst. On a WaveCyclic Line 1
>> "%INI%" echo ; port that clamp doubles every window; the WaveRT port driver (VAC
>> "%INI%" echo ; Control Panel) unlocks the real 5 ms.
>> "%INI%" echo cap_ms = %v_cap_ms%
>> "%INI%" echo.
>> "%INI%" echo ; CAPTURE pin mode: exclusive = take the VAC recording pin WASAPI-
>> "%INI%" echo ; exclusive (on a WaveRT port this is the direct cyclic-buffer path,
>> "%INI%" echo ; ~10 ms cable-^>app). If the pin refuses (engine holds it, WaveCyclic
>> "%INI%" echo ; port, format mismatch) twinspeak falls back to shared and says so.
>> "%INI%" echo capture_exclusive = %v_capture_exclusive%
>> "%INI%" echo.
>> "%INI%" echo ; Out RING depth per leg, in ms. The ring target is FLOORED at the FULL
>> "%INI%" echo ; device buffer (2 periods) + a FULL capture burst + 4 ms regardless of
>> "%INI%" echo ; this -- the ring must survive the device's worst legitimate bite (a
>> "%INI%" echo ; late/merged pop asks for both periods at once). At 5/5/cap5 the floor
>> "%INI%" echo ; is ~19 ms; raise ring1_ms/ring2_ms only for extra glitch headroom
>> "%INI%" echo ; (each ms costs 1 ms latency, legs stay aligned if kept equal).
>> "%INI%" echo ; v6.3.1: a backend = ks leg floors LOWER -- one KS bite + one capture
>> "%INI%" echo ; burst + 1 ms -- because its only pull is twinspeak's own pacing thread
>> "%INI%" echo ; (exactly one bite, never merged). At ks_period_frames 96 + cap_ms 1
>> "%INI%" echo ; that floor is 5 ms and the leg's est path prints ~10 ms.
>> "%INI%" echo ring1_ms = %v_ring1_ms%
>> "%INI%" echo ring2_ms = %v_ring2_ms%
>> "%INI%" echo.
>> "%INI%" echo ; ALIGNMENT DELAY (v6.1): delay1_ms / delay2_ms push ONE leg's output
>> "%INI%" echo ; later by whole milliseconds -- a BIT-EXACT ring delay: the leg's ring
>> "%INI%" echo ; target grows, the samples leave it later, nothing is resampled or
>> "%INI%" echo ; regenerated. Put the delay on whichever leg arrives EARLY:
>> "%INI%" echo ;   * the center speaker's USB path (amp + DSP) lags the mains -^> raise
>> "%INI%" echo ;     delay1_ms so out1 waits for it;
>> "%INI%" echo ;   * the center leg runs early instead -^> raise delay2_ms.
>> "%INI%" echo ; The banner prints each leg's 'est path' -- start at the printed
>> "%INI%" echo ; difference between the two legs, then trim by ear in 1-2 ms steps.
>> "%INI%" echo ; (The S5's own amp latency is OUTSIDE the printed path -- only the ear
>> "%INI%" echo ; can see it.) Each ms costs 1 ms of latency on that leg only.
>> "%INI%" echo delay1_ms = %v_delay1_ms%
>> "%INI%" echo delay2_ms = %v_delay2_ms%
>> "%INI%" echo.
>> "%INI%" echo ; WASAPI exclusive output (kernel WaveRT path: no engine, no
>> "%INI%" echo ; enhancements, ~10 ms less device buffering). Bypasses the Windows
>> "%INI%" echo ; volume slider. If either driver refuses, align_modes reopens BOTH
>> "%INI%" echo ; legs as shared automatically, so device buffering stays identical.
>> "%INI%" echo exclusive = %v_exclusive%
>> "%INI%" echo.
>> "%INI%" echo ; Force both legs into the same share mode (recommended -- mixed modes
>> "%INI%" echo ; are exactly what made out2 lag out1 in v2). off = per-leg fallback.
>> "%INI%" echo align_modes = %v_align_modes%
>> "%INI%" echo.
>> "%INI%" echo ; Fold the cable's LFE onto FL/FR at gain_lfe -- the CONFIRMED routing
>> "%INI%" echo ; ("LFE is mixed fully into the left and right channels"). The sub is
>> "%INI%" echo ; not discrete; it lives inside the front channels. lfe_fold = off
>> "%INI%" echo ; instead drives the DISCRETE 5.1 LFE channel (only for a real sub).
>> "%INI%" echo lfe_fold = %v_lfe_fold%
>> "%INI%" echo.
>> "%INI%" echo ; Substring match against device names (see: twinspeak --list)
>> "%INI%" echo input_device   = %v_input_device%
>> "%INI%" echo output_device1 = %v_output_device1%
>> "%INI%" echo output_device2 = %v_output_device2%
>> "%INI%" echo.
>> "%INI%" echo ; optional mixes (gain_lfe = LFE fold gain onto FL/FR; set 1.0 for a
>> "%INI%" echo ;                    unity fold. With lfe_fold = off it scales the
>> "%INI%" echo ;                    discrete sub.)
>> "%INI%" echo gain_l = %v_gain_l%
>> "%INI%" echo gain_r = %v_gain_r%
>> "%INI%" echo gain_lfe = %v_gain_lfe%
>> "%INI%" echo gain_center = %v_gain_center%
>> "%INI%" echo.
>> "%INI%" echo ; VOLUME: volume_follow = the Windows volume of the cable's PLAYBACK
>> "%INI%" echo ; side (the Line 1 slider in sndvol, or the volume hotkeys when Line 1
>> "%INI%" echo ; is the default device) is the single master knob -- twinspeak
>> "%INI%" echo ; multiplies every captured frame by it, so BOTH out1 and out2 track it
>> "%INI%" echo ; identically. The playback endpoint is matched by name: volume_endpoint
>> "%INI%" echo ; first (substring), then input_device, then any 'Virtual Audio Cable'
>> "%INI%" echo ; render endpoint. If nothing matches, the banner prints EVERY render
>> "%INI%" echo ; endpoint with its state -- copy the cable's playback name into
>> "%INI%" echo ; volume_endpoint and rerun.
>> "%INI%" echo volume_follow = %v_volume_follow%
if not defined v_volume_endpoint >> "%INI%" echo volume_endpoint =
if defined v_volume_endpoint >> "%INI%" echo volume_endpoint = %v_volume_endpoint%
>> "%INI%" echo pin_outputs = %v_pin_outputs%
>> "%INI%" echo out2_first = %v_out2_first%
>> "%INI%" echo.
>> "%INI%" echo ; KEEP-ALIVE: out2 sums a sine on both channels every frame (silence or
>> "%INI%" echo ; not) so the center device never sees digital silence. Frequency and
>> "%INI%" echo ; level are tunable. 40 Hz @ 1%% = -40 dBFS, far below hearing -- BUT
>> "%INI%" echo ; some USB amps (the Xtream S5 included) sleep on anything under their
>> "%INI%" echo ; detect threshold. If the S5 still powers down while twinspeak runs,
>> "%INI%" echo ; raise keepalive_gain (0.03..0.05 is still inaudible at 40 Hz) or move
>> "%INI%" echo ; keepalive_hz. The watchdog reopens out2 automatically whenever it
>> "%INI%" echo ; comes back.
>> "%INI%" echo keepalive_hz = %v_keepalive_hz%
>> "%INI%" echo keepalive_gain = %v_keepalive_gain%
>> "%INI%" echo.
>> "%INI%" echo ; CHIMES (v6.2): the CenterFold startup signature is back, modernized.
>> "%INI%" echo ; The old jingle was a rising SQUARE fifth (G5 784 Hz -^> D6 1174.66 Hz);
>> "%INI%" echo ; the modernized chime keeps the rising fifth but plays two short
>> "%INI%" echo ; TRIANGLE BLIPS in the low treble instead: C7 2093 Hz (42 ms) -^> G7
>> "%INI%" echo ; 3136 Hz (72 ms), fast decay -- a blip, not a horn. It plays on the
>> "%INI%" echo ; CENTER speaker the moment the engine goes live, and again whenever
>> "%INI%" echo ; the watchdog brings out2 back (3 s cooldown). It is a UI cue, not
>> "%INI%" echo ; program audio: summed at the out2 device layer exactly like the
>> "%INI%" echo ; keep-alive, so volume_follow does NOT scale it, and out1 -- the
>> "%INI%" echo ; bit-exact mains path -- NEVER carries it. chime_gain is the blip
>> "%INI%" echo ; peak (0..1; 0.4 = clearly audible; lower it if the center speaker
>> "%INI%" echo ; runs hot). chimes = off restores the v6.1 silence.
>> "%INI%" echo chimes = %v_chimes%
>> "%INI%" echo chime_gain = %v_chime_gain%
>> "%INI%" echo.
>> "%INI%" echo ; BACKGROUND (v6.2): background = on runs twinspeak as a background
>> "%INI%" echo ; process. The console detaches AFTER a successful startup (a failed
>> "%INI%" echo ; start keeps the window so you can read the error) and everything --
>> "%INI%" echo ; banner, sync lines, health line -- is logged to twinspeak.log next
>> "%INI%" echo ; to this ini (health line every 10 s in the file). Stop a background
>> "%INI%" echo ; run with:   taskkill /IM twinspeak.exe
>> "%INI%" echo ; Start it visible anyway with:   twinspeak --foreground
>> "%INI%" echo ; A second instance is refused (single-instance lock; add --multi to
>> "%INI%" echo ; allow one deliberately). off = classic console run, Ctrl+C to stop.
>> "%INI%" echo background = %v_background%
>> "%INI%" echo.
>> "%INI%" echo ; WDM-KS OUTPUT BACKEND (v6.3) -- the FlexASIO path, inside twinspeak.
>> "%INI%" echo ; backend1 / backend2 route each leg through Kernel Streaming instead of
>> "%INI%" echo ; WASAPI: a direct render pin on the device's wave filter (KsCreatePin),
>> "%INI%" echo ; the WaveRT looped buffer as the device's own DMA memory, and the
>> "%INI%" echo ; hardware play-position register mapped read-only -- twinspeak's own
>> "%INI%" echo ; TIME-CRITICAL thread paces the stream against the DAC itself, with no
>> "%INI%" echo ; events, no engine and no scheduler intermediaries in between. The
>> "%INI%" echo ; audio crossing the buffer switch is bit-identical (same rings, same
>> "%INI%" echo ; replay/drop rules, same gates); only the transport changes.
>> "%INI%" echo ;   backendN = wasapi  the proven v6.2 path (miniaudio, exclusive WaveRT)
>> "%INI%" echo ;   backendN = ks      WDM-KS, lowest latency, 256-sample switches
>> "%INI%" echo ; ks_period_frames = the buffer-switch size in SAMPLES (256 @ 48 kHz =
>> "%INI%" echo ; one bite every 5.33 ms; 96 = one bite every 2 ms -- smaller bites
>> "%INI%" echo ; shrink the KS ring floor too). ks_periods = the looped buffer size
>> "%INI%" echo ; around it: 2 = the write lead is one period (lowest latency, the pump
>> "%INI%" echo ; must never miss a deadline), 3 = one extra period of grace
>> "%INI%" echo ; (recommended), up to 8 -- a 1 in the ini clamps to 2 with a log line
>> "%INI%" echo ; (a 1-period loop has no write lead at all). The DAC adds its hardware
>> "%INI%" echo ; FIFO on top (~1-2 ms, printed). v6.3.1: with both legs on ks the
>> "%INI%" echo ; banner's align line prints the est-path DIFFERENCE and which delay
>> "%INI%" echo ; knob to move -- the delay trim starts from that number.
>> "%INI%" echo ; KS bypasses the Windows endpoint: no endpoint volume there, so
>> "%INI%" echo ; pin_outputs is a no-op on a KS leg -- volume_follow still governs it
>> "%INI%" echo ; (applied in the pump, exactly as before). If the driver refuses the
>> "%INI%" echo ; pin (no WaveRT support, format refused, pin busy) that leg logs the
>> "%INI%" echo ; reason and falls back to WASAPI exclusive automatically. Use
>> "%INI%" echo ;   twinspeak --list-ks
>> "%INI%" echo ; to see every WDM-KS render filter with its pins and formats -- the
>> "%INI%" echo ; SAME name needles as below match the KS filters.
>> "%INI%" echo ; NOTE: backendN = wasapi is the RECOMMENDED setting -- the proven,
>> "%INI%" echo ; low-maintenance path. backendN = ks is the FlexASIO-inspired direct
>> "%INI%" echo ; Kernel-Streaming transport for pinned machines that want the last
>> "%INI%" echo ; few ms; it needs a driver with WaveRT looped streaming and it gives
>> "%INI%" echo ; up cleanly to WASAPI when the pin refuses.
>> "%INI%" echo backend1 = %v_backend1%
>> "%INI%" echo backend2 = %v_backend2%
>> "%INI%" echo ks_period_frames = %v_ks_period_frames%
>> "%INI%" echo ks_periods = %v_ks_periods%
>> "%INI%" echo.
>> "%INI%" echo ; CLOCK SYNC (v6.0, ASIO-strict): the audio path is BIT-EXACT -- there
>> "%INI%" echo ; is no resampler and no pitch control anywhere in twinspeak. Clock
>> "%INI%" echo ; drift between the cable's virtual clock and each device crystal is
>> "%INI%" echo ; absorbed by two device-master rules at the buffer switch:
>> "%INI%" echo ;   * a buffer switch the ring cannot fill REPLAYS the last played
>> "%INI%" echo ;     buffer (the ASIO underrun convention) instead of a silence hole;
>> "%INI%" echo ;   * ring excess above the natural sawtooth (cable faster than device)
>> "%INI%" echo ;     is dropped OLDEST-first, at most once per 5 s per leg, logged.
>> "%INI%" echo ; The health line shows the measured offset ('off +N ppm'). At crystal
>> "%INI%" echo ; level both events are minutes apart. If 'off' sits far from 0, the
>> "%INI%" echo ; one-shot hint prints the value -- enable Clock Correction for the
>> "%INI%" echo ; cable in the VAC Control Panel and trim it until 'off' reads ~0; a
>> "%INI%" echo ; bit-exact path + a corrected cable clock = a silent path forever.
>> "%INI%" echo ; clock_sync = off disables BOTH rules (A/B only: shortfall pads
>> "%INI%" echo ; silence, excess evicts at the ring cap).
>> "%INI%" echo clock_sync = %v_clock_sync%
>> "%INI%" echo.
>> "%INI%" echo ; DRIFT WARDEN (v6.4) -- the timestamp system. Every capture burst is
>> "%INI%" echo ; stamped into a frame-interval ledger: each leg tracks the capture
>> "%INI%" echo ; sequence number of its play boundary, advanced by exactly the real
>> "%INI%" echo ; frames that cross it (pops, gated drops, warden skips; replays and
>> "%INI%" echo ; deliberate pads never advance it). The difference of the two
>> "%INI%" echo ; boundaries IS the measured delay between the two speakers, printed
>> "%INI%" echo ; every second as 'drift' on the health line -- pure frame arithmetic,
>> "%INI%" echo ; no clock involved, so it cannot lie. The drift column INCLUDES your
>> "%INI%" echo ; own delay1_ms/delay2_ms: at delay1_ms = 60 it reads about -65 (the
>> "%INI%" echo ; few-ms remainder is the legs' natural start asymmetry) -- that
>> "%INI%" echo ; offset IS the alignment your ear asked for, not a fault. The 'err'
>> "%INI%" echo ; column beside it is the distance from the calibrated baseline --
>> "%INI%" echo ; the number the warden acts on; err ~ +0.0 means the path is pinned.
>> "%INI%" echo ; For ~15 s after boot the warden
>> "%INI%" echo ; CALIBRATES: it averages the settled drift into a baseline (your
>> "%INI%" echo ; delay1_ms/delay2_ms calibration is inside that number). After that,
>> "%INI%" echo ; a drift that leaves the baseline by ^>= drift_correct_ms (default 10)
>> "%INI%" echo ; for 3 consecutive seconds is CORRECTED automatically, at most once
>> "%INI%" echo ; per 30 s: the late leg's ring gives back its excess (a bit-exact
>> "%INI%" echo ; skip -- the same operation as the clock rule above) and the early
>> "%INI%" echo ; leg is held (its last buffer replays in place until the depth
>> "%INI%" echo ; returns). No sample value is ever touched. Two honest limits:
>> "%INI%" echo ;   * the ledger sees the HOST pipeline only -- anything below the USB
>> "%INI%" echo ;     packetizer (the S5's own receiver FIFO / amp silicon) is
>> "%INI%" echo ;     invisible. If the drift column stays flat while your ears hear
>> "%INI%" echo ;     drift, the offset is device silicon and no host trigger exists --
>> "%INI%" echo ;     the log then proves exactly that.
>> "%INI%" echo ;   * a correction is a skip or a stutter of drift_correct_ms at most
>> "%INI%" echo ;     (rate-limited): that is the price of re-pinning a bit-exact path.
>> "%INI%" echo ; drift_guard = off keeps the ledger + the drift column but disables
>> "%INI%" echo ; the automatic re-pin (observe-only).
>> "%INI%" echo drift_guard = %v_drift_guard%
>> "%INI%" echo drift_correct_ms = %v_drift_correct_ms%
>> "%INI%" echo.
>> "%INI%" echo ; CENTER PRESENCE (v6.1) -- out2 plays the center channel ONLY while it
>> "%INI%" echo ; is genuinely present. Two gates, both default ON:
>> "%INI%" echo ;   center_gate -- a block whose ch3 is digital zero emits NOTHING but
>> "%INI%" echo ;     the keep-alive tone: if the center channel is ever zero, every
>> "%INI%" echo ;     channel of that sample stays zero. Never a substitute.
>> "%INI%" echo ;   center_upmix_guard -- when the SOURCE has no center of its own (a
>> "%INI%" echo ;     stereo app on the 8ch cable), the Windows engine fills FC with
>> "%INI%" echo ;     a*FL + b*R at the cable's playback side -- BEFORE twinspeak can
>> "%INI%" echo ;     capture it, so channel muting cannot remove it (this is the 'if
>> "%INI%" echo ;     the center channel is absent it mixes left and right instead'
>> "%INI%" echo ;     issue). The guard recognizes that exact fingerprint per block
>> "%INI%" echo ;     (least-squares ch3 ~ a*ch1 + b*ch2, residual ^< -30 dB, gains in
>> "%INI%" echo ;     the upmix band, fronts active) and mutes those blocks too -- the
>> "%INI%" echo ;     center speaker stays on keep-alive only. Genuine 5.1 center is an
>> "%INI%" echo ;     INDEPENDENT track and never matches; it always passes. The health
>> "%INI%" echo ;     line counts guarded blocks ('cm') -- with real 5.1 content it
>> "%INI%" echo ;     must read 0. Set center_upmix_guard = off only if it ever mutes
>> "%INI%" echo ;     genuine center dialogue (the log names the fitted gains).
>> "%INI%" echo center_gate = %v_center_gate%
>> "%INI%" echo center_upmix_guard = %v_center_upmix_guard%
>> "%INI%" echo.
>> "%INI%" echo ; ROUTING GUARANTEE (zero duplication, by construction):
>> "%INI%" echo ;   out1 gets ONLY ch1 (FL) and ch2 (FR) plus gain_lfe * ch4 (LFE)
>> "%INI%" echo ;   folded onto both. The 5.1 stream's center, discrete sub and
>> "%INI%" echo ;   surround channels are DRIVEN DIGITAL SILENCE, so no layer (Windows
>> "%INI%" echo ;   engine, miniaudio converter, Speaker Fill) has any signal to
>> "%INI%" echo ;   upmix into a fake center. out2 gets ONLY ch3 (center): every other
>> "%INI%" echo ;   input channel is EXPLICITLY MUTED in the out2 path -- out2 is not
>> "%INI%" echo ;   a clone of the program audio. The startup banner prints the route
>> "%INI%" echo ;   map on every launch.
>> "%INI%" echo ;
>> "%INI%" echo ; NOTE: output2 always sums the keep-alive sine (keepalive_hz @
>> "%INI%" echo ; keepalive_gain, default 40 Hz @ 1%%) onto both channels, every
>> "%INI%" echo ; frame, so the center device NEVER sees digital silence -- and when
>> "%INI%" echo ; the center gates mute a block, the keep-alive is ALL that block
>> "%INI%" echo ; carries. The out2 pump also runs FIRST and at the higher priority
>> "%INI%" echo ; (out2_first): the center leg gets the freshest data and the most
>> "%INI%" echo ; scheduling attention; delay1_ms/delay2_ms then trim the alignment.

if not exist "%INI%" echo       ERROR: twinspeak.ini was NOT written -- run this from a writable folder.
if exist "%INI%" for /f %%N in ('find /c /v "" ^< "%INI%"') do set "LINES=%%N"
if exist "%INI%" for %%A in ("%INI%") do set "BYTES=%%~zA"
if exist "%INI%" echo       wrote %LINES% lines, %BYTES% bytes.
if exist "%INI%" echo       Full path: %INI%

echo [4/5] Verifying the written file against the values just chosen ...
set "VERIFY=ok"
call :check "delay1_ms = %v_delay1_ms%"
call :check "ks_period_frames = %v_ks_period_frames%"
call :check "ks_periods = %v_ks_periods%"
call :check "backend1 = %v_backend1%"
call :check "drift_guard = %v_drift_guard%"
call :check "center_gate = %v_center_gate%"
call :check "keepalive_gain = %v_keepalive_gain%"
if "%VERIFY%"=="ok" echo       verification PASSED -- all sentinel keys present.
if not "%VERIFY%"=="ok" echo       verification FAILED -- do NOT run twinspeak with this file.

echo [5/5] Done.
if defined NOVERRIDES echo       advanced overrides applied: %NOVERRIDES%
set "ANYCHG="
if not "%v_input_device%"=="Line 1" (
    set "ANYCHG=1"
    echo       input_device = %v_input_device%  (tuned default Line 1)
)
if not "%v_output_device1%"=="Realtek" (
    set "ANYCHG=1"
    echo       output_device1 = %v_output_device1%  (tuned default Realtek)
)
if not "%v_output_device2%"=="Xtream" (
    set "ANYCHG=1"
    echo       output_device2 = %v_output_device2%  (tuned default Xtream)
)
if not "%v_backend1%"=="wasapi" (
    set "ANYCHG=1"
    echo       backend1 = %v_backend1%  (tuned default wasapi)
)
if not "%v_backend2%"=="wasapi" (
    set "ANYCHG=1"
    echo       backend2 = %v_backend2%  (tuned default wasapi)
)
if not "%v_ks_period_frames%"=="96" (
    set "ANYCHG=1"
    echo       ks_period_frames = %v_ks_period_frames%  (tuned default 96)
)
if not "%v_ks_periods%"=="4" (
    set "ANYCHG=1"
    echo       ks_periods = %v_ks_periods%  (tuned default 4)
)
if not "%v_delay1_ms%"=="65" (
    set "ANYCHG=1"
    echo       delay1_ms = %v_delay1_ms%  (tuned default 65)
)
if not "%v_delay2_ms%"=="0" (
    set "ANYCHG=1"
    echo       delay2_ms = %v_delay2_ms%  (tuned default 0)
)
if not "%v_drift_guard%"=="on" (
    set "ANYCHG=1"
    echo       drift_guard = %v_drift_guard%  (tuned default on)
)
if not "%v_keepalive_gain%"=="0.01" (
    set "ANYCHG=1"
    echo       keepalive_gain = %v_keepalive_gain%  (tuned default 0.01)
)
if not "%v_background%"=="off" (
    set "ANYCHG=1"
    echo       background = %v_background%  (tuned default off)
)
if defined ANYCHG goto :summary_done
echo       every prompted key kept its tuned default.
:summary_done
echo.
echo Replace the current twinspeak.ini so it works -- that is exactly what
echo this script did: the generated file has REPLACED any previous ini.
echo Keep twinspeak.exe in the same folder and run it.
echo.
echo Backend note: backend1/backend2 = wasapi is the RECOMMENDED setting.
echo The WDM-KS backend (backendN = ks), inspired by FlexASIO, is the
echo low-latency option for pinned machines; it falls back to WASAPI
echo automatically when a driver refuses the pin.
echo.
:quit
echo Press any key to close this window . . .
pause >nul
endlocal
exit /b

:check
findstr /c:%1 "%INI%" >nul 2>&1
if errorlevel 1 (
    set "VERIFY=FAILED"
    echo       MISSING: %~1
)
if not errorlevel 1 echo       ok: %~1
goto :eof

:ask_text
rem %1 = variable  %2 = label  %3 = default -- metachar guard only
set /p "%~1=  %~2 [%~3]: "
call :guard %~1 "%~3"
goto :eof

:ask_num
rem %1 = variable  %2 = label  %3 = default -- digits and dots only
set /p "%~1=  %~2 [%~3]: "
set "CHK="
call set "CHK=%%%~1%%"
echo(%CHK%| findstr /r /c:"^[0-9][0-9.]*$" >nul 2>&1
if not errorlevel 1 goto :eof
echo       not a number -- keeping the default (%~3).
set "%~1=%~3"
goto :eof

:ask_onoff
rem %1 = variable  %2 = label  %3 = default -- on or off
set /p "%~1=  %~2 [%~3]: "
call :guard %~1 "%~3"
set "CHK="
call set "CHK=%%%~1%%"
if /i "%CHK%"=="on" goto :eof
if /i "%CHK%"=="off" goto :eof
echo       please type on or off -- keeping the default (%~3).
set "%~1=%~3"
goto :eof

:ask_backend
rem %1 = variable  %2 = label  %3 = default -- wasapi or ks
set /p "%~1=  %~2 [%~3]: "
call :guard %~1 "%~3"
set "CHK="
call set "CHK=%%%~1%%"
if /i "%CHK%"=="wasapi" goto :eof
if /i "%CHK%"=="ks" goto :eof
echo       please type wasapi or ks -- keeping the default (%~3).
set "%~1=%~3"
goto :eof

:guard
rem %1 = variable  %2 = default to restore if the value is unsafe
rem replace-compare idiom, written WITH its closing percent sign
set "CHK="
call set "CHK=%%%~1%%"
if not defined CHK goto :eof
if not "%CHK%"=="%CHK:&=_%" goto :guard_bad
if not "%CHK%"=="%CHK:<=_%" goto :guard_bad
if not "%CHK%"=="%CHK:>=_%" goto :guard_bad
if not "%CHK%"=="%CHK:|=_%" goto :guard_bad
if not "%CHK%"=="%CHK:^=_%" goto :guard_bad
if not "%CHK%"=="%CHK:"=_%" goto :guard_bad
echo(%CHK%| findstr /c:"%%" >nul && goto :guard_bad
goto :eof
:guard_bad
echo       refused: cmd metacharacters are not safe here -- keeping the default.
set "%~1="
if not "%~2"=="" set "%~1=%~2"
goto :eof