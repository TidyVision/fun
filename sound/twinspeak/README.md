Disclaimer: This tool was made for free, bypassing the requirement of paying for e.g. voicemeeter potato. Use this at your own risk.

A GUI should come with twinspeak.exe. And there should be a built-in E.Q. setting to use instead of utilizing Equalizer APO, as that software uses shared-mode, where as this software bypasses all of that by WASAPI Exclusive/KS.

## Overview & Metadata

PROPERTY OF TIDYVISION, GENERATED FROM GLM 5.3 FLASH

POST-POLISH DONE THROUGH OCT. 28 2026 to OCT. 29 2026 | PROJECT STARTED ON SEPT. 28 at around 1:00PM

FORMERLY CENTERFOLD

## Download

[TwinSpeak Program](https://github.com/TidyVision/fun/raw/main/sound/twinspeak/TidyVision%20TwinSpeak.zip)

# Twinspeak

**Bit-exact 5.1 → 2.1 + center splitter for Windows — WASAPI / WDM-KS, no resampler, no clones, no surprises, and no external lag.**

twinspeak captures the 8-channel / 48 kHz stream of a VB-Audio **Virtual Audio Cable** ("Line 1") and plays it to **two output devices at once** with a strict, verifiable split:

```
                        +--> out1  mains (e.g. Realtek)        = FL + FR + 0.4 x LFE (folded)
apps --> VAC Line 1 ----+         center / sub / surrounds      = driven DIGITAL SILENCE
      (8ch @ 48 kHz)    +--> out2  center speaker (e.g.         = L = R = ch3 ONLY
                                 Adesso Xtream S5)             + 40 Hz @ 1% keep-alive
```

It exists for the machine that cannot do analog multichannel — a Lenovo M720q with one combo jack — but has an onboard DAC for the mains and a USB speaker for the center. The 5.1 stream arrives digitally; twinspeak routes it with the guarantees below and never gets in the way of the samples.

## The guarantees

* **Bit-exact.** The audio path is memcpy. There is no resampler, no pitch control and no sample regeneration anywhere in the engine. Clock drift between the cable's virtual clock and each device crystal is absorbed by two device-master rules at the buffer switch (below) — never by a rate change. The v6.5 optimization pass was validated by a randomized bit-equivalence harness (v6.4 vs v6.5 code side by side on adversarial inputs), and the build pins `-ffp-contract=off` so no mul+add can ever fuse into an FMA and change a rounding.
* **Lowest latency.** WASAPI-exclusive WaveRT output, exclusive capture, decoupled capture burst, ring floors computed from each transport's worst legitimate bite, and an optional WDM-KS transport (below) paced by twinspeak's own time-critical thread. The banner prints each leg's real `est path`.
* **Never interrupts.** A watchdog reopens any device thread that goes silent > 5 s, by name. The center device never sees digital silence (40 Hz @ 1% keep-alive), so cheap USB amps do not sleep. Underruns replay the last buffer (the ASIO convention) instead of punching a silence hole.
* **Zero cloning.** out1 is driven from ch1/ch2 (+ folded LFE) only — center, discrete sub and surrounds are driven digital silence, so no layer (Windows engine, miniaudio converter, Speaker Fill) has any signal to upmix into a fake center. out2 carries ch3 only — every other channel is explicitly muted in its path. The startup banner prints the route map on every launch.

## Requirements

* Windows 10/11 x64, and a 2013-or-newer CPU (Intel Haswell+, AMD Zen+ — the exe targets x86-64-v3/AVX2; rebuild without `-march` for anything older).
* [VB-Audio Virtual Audio Cable](https://vb-audio.com/Cable/) installed as the 5.1 source target: **8 channels, 48000 Hz**, and the **WaveRT port driver** selected in the VAC Control Panel (a WaveCyclic port doubles the shared-mode capture clamp; WaveRT unlocks the real burst).
* Two output devices. The reference rig: onboard Realtek (mains) + Adesso Xtream S5 USB speakers (center). Any devices work — you match them by name substring.
* A 5.1-capable source app that can address the cable.

## Quick start

1. Put `twinspeak.exe` (from Releases) in its own folder.
2. Put `generate_ini.bat` in the same folder and run it (double-click works too). **Replace the current twinspeak.ini so it works — that is exactly what the script does:** it writes the tuned, known-good v6.5.1 configuration and backs up any previous ini to `twinspeak.ini.bak`. (Running `twinspeak.exe` with no ini at all writes a generic template and exits — the generated one is the tuned config instead.)

   The script is **interactive**: it asks for the twelve keys that actually get tuned on this rig — devices, KS transport, alignment delays, guards — and every prompt shows its tuned default in `[brackets]`. **Press Enter to keep the default; type a value first to override it.** Holding Enter from the top accepts every default and reproduces the tuned ini byte-for-byte. A refused answer (cmd metacharacters, non-number in a number field, `on`/`off` typo, empty value) is announced and falls back to the default **before anything is written** — a bad answer never touches your current ini. An advanced `key=value` prompt at the end of step 1 can override *any* ini key, spaces included (`volume_endpoint=Line 1 (VAC)`).

   The script narrates five numbered steps: **[1/5]** your settings (12 prompts + the advanced `key=value` prompt; each accepted override is echoed), **[2/5]** backup (it says whether one was made), **[3/5]** the write — a deliberately *quiet* step, because all ~260 lines go into the file, not onto the screen (it then reports lines + bytes + full path), **[4/5]** a key-by-key verification of the written file against the values just chosen (`ok: …` per key, `MISSING: …` if anything went wrong, with a PASSED/FAILED verdict), **[5/5]** the summary — every prompt you changed is listed with its tuned default beside it. It then waits for a keypress so the window stays open. Windows tip: clicking inside a console window freezes its output (text-select mode) until you press Enter — if a step ever looks stuck, press Enter once and the log resumes; nothing was lost.
3. Check the device needles against your hardware: `twinspeak --list` (WASAPI endpoints) and `twinspeak --list-ks` (kernel filters). Edit `input_device`, `output_device1`, `output_device2` if yours differ.
4. Run `twinspeak.exe`. Read the banner: route map, negotiated periods per leg, each leg's `est path`, and the align line.
5. Watch the health line: `off` near 0 ppm, `und +0/0`, `drop +0/0`, `corr 0/0`, `cm +0`.
6. Align by ear: the banner's align line prints the est-path **difference** and which delay knob moves in which direction; trim in 1–2 ms steps with `delay1_ms` / `delay2_ms`.

## Backends — `wasapi` is the recommended setting

`backend1` / `backend2` choose the transport per leg. **You have to specify the backend `wasapi` as recommended** — the generated ini ships `backend1 = wasapi` / `backend2 = wasapi`, and that is the proven, low-maintenance path:

| setting | transport | character |
|---|---|---|
| `backendN = wasapi` | miniaudio WASAPI exclusive (kernel WaveRT event mode) | **Recommended.** No engine, no enhancements, ~10 ms less device buffering than shared. Falls back to shared only if the driver refuses, and `align_modes = on` reopens BOTH legs in the same mode so device buffering stays identical. |
| `backendN = ks` | **WDM-KS, inspired by FlexASIO** | The low-latency option for pinned machines. A direct render pin on the device's wave filter (`KsCreatePin`), the WaveRT looped buffer as the device's own DMA memory, the hardware play-position register mapped read-only — twinspeak's own TIME-CRITICAL MMCSS "Pro Audio" thread paces the stream against the DAC itself: no events, no engine, no scheduler intermediaries. |

The WDM-KS path is an original implementation of the public KS/WaveRT property interface (`KSCATEGORY_RENDER`, `KsCreatePin`, `KSPROPERTY_RTAUDIO_BUFFER` / `POSITIONREGISTER`), following the same wire layout as the reference WDM-KS backends. The audio crossing the buffer switch is **bit-identical** to the WASAPI path — same rings, same replay/drop clock rules, same gates; only the transport changes.

KS specifics:

* `ks_period_frames` — the buffer-switch size in samples (32..4096, clamped with a log line). 256 @ 48 kHz = one bite every 5.33 ms; 96 = one bite every 2 ms. Smaller bites shrink the KS ring floor (one bite + one capture burst + 1 ms).
* `ks_periods` — the looped WaveRT buffer, in periods (2..8; **a 1 in the ini clamps to 2 with a log line** — a 1-period loop has no write lead at all). 2 = lowest latency, the pacer must never miss a deadline; 3 = one period of grace (recommended); 4 = two consecutive missed deadlines tolerated; each extra period costs exactly one bite-time of latency.
* A KS pin bypasses the Windows endpoint: endpoint volume/mute do not apply (`pin_outputs` is a no-op on a KS leg — logged once). `volume_follow` still governs it, applied in the pump exactly as before.
* If the driver refuses the pin (no WaveRT support, format refused, pin busy), that leg logs the reason and falls back to WASAPI exclusive automatically — a leg is never left without a device.
* The DAC's hardware FIFO (~1–2 ms) sits outside both transports and is printed in the banner.

## Latency and alignment

Each leg's path is `ring` + `device buffer` + `capture burst`, printed per leg in the banner as `est path`. The ring target is **floored** at the transport's worst legitimate bite — a WASAPI leg must survive the whole device buffer popped as two back-to-back period callbacks (full device buffer + full burst + 4 ms); a KS leg's only pull is twinspeak's own pacer, exactly one bite, never merged (one bite + one burst + 1 ms).

`delay1_ms` / `delay2_ms` push ONE leg's output later by whole milliseconds — a **bit-exact ring delay**: the leg's ring target grows, the samples leave it later, nothing is resampled or regenerated. Put the delay on whichever leg arrives EARLY. (The center speaker's own amp/DSP latency lives OUTSIDE the printed path — only the ear can see it, and the delay knobs are the only instrument for it. On the reference rig the S5's firmware buffer measured ~65 ms, hence the generated ini ships `delay1_ms = 65`; on any other rig, start at 0 and trim by ear.)

## Clock discipline (device-master ASIO rules, not a resampler)

* **Shortfall → replay.** A buffer switch the ring cannot fill replays the last played buffer (the ASIO underrun convention) instead of a silence hole. Counted as `und` on the health line; must stay `+0/0` in steady state.
* **Excess → gated drop.** If the cable clock runs faster than the device and the ring grows past its natural sawtooth by a full period, the oldest excess is discarded — at most once per 5 s per leg, each event logged. Counted as `corr`.
* The health line measures the real offset (`off +N ppm` per leg). If it sits far from 0, the one-shot hint prints the value: enable **Clock Correction** for the cable in the VAC Control Panel and trim until `off` reads ~0 — a bit-exact path plus a corrected cable clock is a silent path forever.
* `clock_sync = off` disables both rules (A/B testing only).

## Drift Warden (the timestamp system)

Every capture burst is stamped into a frame-interval ledger. Each leg tracks the capture sequence number of its play boundary, advanced by exactly the real frames that cross it (pops, gated drops, warden skips advance it; replays and deliberate pads never do). The difference of the two boundaries IS the measured delay between the two speakers, printed every second as `drift` — pure frame arithmetic, no clock involved, so it cannot lie.

* For ~15 s after boot the warden **calibrates**: it averages the settled drift into a baseline (your `delay1_ms`/`delay2_ms` calibration is inside that number).
* `drift` is the RAW inter-speaker offset — your delay calibration included; that offset IS the alignment your ear asked for, not a fault. `err` beside it is the distance from the calibrated baseline — the number the warden acts on. `err ~ +0.0` = the path is pinned.
* A drift that leaves the baseline by >= `drift_correct_ms` (default 10, clamped 2..500) for 3 consecutive seconds is corrected automatically, at most once per 30 s: the late leg's ring gives back its excess (a bit-exact skip) and the early leg is held (its last buffer replays in place). No sample value is ever touched.
* **Honest limits, written into the log:** the ledger sees the HOST pipeline only — anything below the USB packetizer (a USB speaker's own receiver FIFO / amp silicon) is invisible. If `drift` stays flat while your ears hear drift, the offset is device silicon and no host trigger exists — the log proves exactly that. A correction is a skip or a stutter of at most `drift_correct_ms` (rate-limited).
* `drift_guard = off` keeps the ledger and the columns but disables the automatic re-pin (observe-only).

## Power guard — sleep / Modern Standby survival (v6.5.2)

A Modern Standby machine (field log: Lenovo M720Q, i5-8400) degrades long before it sleeps, and the failure mode is invisible to a silence watchdog: in the captured 4-hour log, the S5's stream stopped consuming at the real rate while its callback thread **kept ticking** — first small excess drops (288/576 frames), then the break: leg2 consumed at ~42% of the cable's rate (`off +1,345,794 ppm`), the out2 ring stuffed to 85 ms, ~572 ms of audio passively evicted per second, and the gated drop + drift warden fighting a limit cycle they can never win. The heartbeat stayed fresh, so nothing ever reopened the leg — the path glitched until twinspeak was restarted.

`power_guard = on` (default) adds two layers, every action logged as `power guard :`:

* **Suspend / resume hooks** (WM_POWERBROADCAST on a hidden window): `PBT_APMSUSPEND` parks the pumps — no more data into a device the OS is taking away — and suppresses the watchdog (no reopen storms mid-suspend). On the resume pair the health loop rebuilds the capture pin **and both legs from scratch**, replays the startup chime, and resets the warden/hint state. A post-sleep stream is never trusted.
* **Consumption-collapse sentinel**: a healthy 48 kHz link never leaves ±10% (the same log's worst *healthy* wobble was ±4000 ppm). Two consecutive 1-s windows beyond that triggers a fix, branched by sign: a **positive** collapse (the device under-consumed — the field log's half-alive S5) reopens that leg fresh; a **negative** collapse (the leg asked for frames the capture never delivered) rebuilds the capture pin instead. This catches the degraded sleep-**entry** window — which fires *before* the suspend broadcast — plus any driver reset that leaves a thread ticking at a bogus rate.

Worst case across any sleep with the guard on: one fresh reopen on wake and a chime, instead of a permanently glitching path. `power_guard = off` restores exact v6.5.1 behavior.

## Center presence gates (out2 plays center ONLY while it is genuinely present)

* `center_gate` — a block whose ch3 is digital zero emits NOTHING but the keep-alive tone. Never a substitute.
* `center_upmix_guard` — when the source has no center of its own, the Windows engine fills FC = a*FL + b*R at the cable's playback side, BEFORE twinspeak can capture it, so channel muting cannot remove it. The guard recognizes that exact fingerprint per block (least-squares ch3 ~ a*ch1 + b*ch2, residual < −30 dB, gains in the upmix band, fronts active) and mutes those blocks too. Genuine 5.1 center is an INDEPENDENT track and never matches. The health line counts guarded blocks (`cm`) — with real 5.1 content it must read 0; a sustained +N while a STEREO source plays is the guard doing exactly its job. The log names the fitted gains on every hit.

## Keep-alive and chimes

* **Keep-alive:** out2 sums a sine (default 40 Hz @ 1% = −40 dBFS) onto both channels every frame, so the center device never sees digital silence and cheap USB amps never sleep. If the speaker still powers down, raise `keepalive_gain` (0.03..0.05 is still inaudible at 40 Hz) or move `keepalive_hz` (5..2000 accepted). The watchdog reopens out2 automatically whenever it comes back.
* **Chimes:** the startup signature — two short triangle blips, C7 2093 Hz (42 ms) → G7 3136 Hz (72 ms), fast decay — on the CENTER speaker when the engine goes live, and again whenever the watchdog brings out2 back (3 s cooldown). It is a UI cue, not program audio: summed at the out2 device layer exactly like the keep-alive, so `volume_follow` does NOT scale it, and out1 — the bit-exact mains path — NEVER carries it. `chimes = off` restores silence; `chime_gain` is the blip peak (0..1, default 0.4).

## Volume

`volume_follow = on` makes the Windows volume of the cable's PLAYBACK side (the Line 1 slider in sndvol, or the volume hotkeys when Line 1 is the default device) the single master knob — twinspeak multiplies every captured frame by it, so BOTH out1 and out2 track it identically. The playback endpoint is matched by name: `volume_endpoint` first (substring), then `input_device`, then any 'Virtual Audio Cable' render endpoint. If nothing matches, the banner prints EVERY render endpoint with its state — copy the cable's playback name into `volume_endpoint` and rerun. `pin_outputs = on` additionally pins the out1/out2 endpoints at 100% and unmuted (no-op on a KS leg, where there is no endpoint volume).

## Background mode

`background = on` runs twinspeak as a background process: after a SUCCESSFUL startup the console detaches and everything — banner, sync events, health line — goes to `twinspeak.log` next to the ini (health line every 10 s there). A failed start keeps the window so you can read the error. Stop a background run with `taskkill /IM twinspeak.exe`; start visible once with `twinspeak --foreground`. A second instance is refused (single-instance lock; `--multi` allows one deliberately — two engines would fight over the same exclusive pins).

## The health line (1 s; every 10 s in the log)

```
out1  19.2 | out2  19.2 | vol  36%  | held  20.1/ 20.0 | off   +12/  +12 | und +0/0 | drop +0/0 | corr 0/0 | cm +0 | drift -65.2 | err +0.0
```

| column | meaning |
|---|---|
| `out1` / `out2` | ring fill in ms per leg |
| `vol` | the Line 1 playback-side slider (master for both legs); `M` = muted, `---` = follow off / endpoint not found |
| `held` | ring + capture backlog per leg — the latency anchor is the ring target printed in the banner (delay included) |
| `off` | measured clock offset in ppm per leg (cable vs device) |
| `und` | buffer switches padded by the replay rule — must stay `+0/0` |
| `drop` | frames discarded by passive overwrite — must stay `+0/0` |
| `corr` | gated excess-drop events (~1 per 100 s per 100 ppm) |
| `cm` | blocks muted by the upmix guard this second — `+0` with real 5.1 content |
| `drift` | raw inter-speaker offset in ms, cable time (your delay calibration included) |
| `err` | distance from the calibrated baseline — the warden's trigger number; `----` during the ~15 s calibration |

## Command line

```
twinspeak [--list] [--list-ks] [--ini path] [--foreground] [--multi] [--help]
  --list        show every WASAPI endpoint (the usual device needles)
  --list-ks     show every WDM-KS render filter with its pins/formats
  --ini path    read a config from somewhere else
  --foreground  ignore background = on for this run (keep the console)
  --multi       allow a second instance (skip the single-instance lock)
```

## Configuration reference

Every key is documented in place in `twinspeak.ini` (generated by `generate_ini.bat`); defaults below are the generated file's values, with the engine's clamps noted.

| key | generated value | notes |
|---|---|---|
| `out1ms`, `out2ms` | 1 | requested WASAPI device period per leg; keep EQUAL. Shared mode clamps to ~10 ms. Engine default if absent: 5 (legacy `buffer_size` = 10) |
| `cap_ms` | 1 | capture burst period, decoupled from the out periods |
| `capture_exclusive` | on | WASAPI-exclusive capture pin on the VAC recording side; falls back to shared and says so |
| `ring1_ms`, `ring2_ms` | 1 | out-ring depth per leg; floored per transport (see Latency). <0 = follow outXms |
| `delay1_ms`, `delay2_ms` | 65 / 0 | bit-exact ring delay on the EARLY leg (0..1000). The 65 is the reference rig's measured S5 firmware-buffer trim — start at 0 on any other rig |
| `exclusive` | on | WASAPI exclusive output; refused drivers → both legs reopen shared (`align_modes`) |
| `align_modes` | on | force both legs into the same share mode |
| `lfe_fold` | on | fold LFE onto FL/FR at `gain_lfe`; off = drive the discrete 5.1 LFE |
| `input_device` | Line 1 | substring needle |
| `output_device1` | Realtek | substring needle (mains) |
| `output_device2` | Xtream | substring needle (center) |
| `gain_l`, `gain_r`, `gain_lfe`, `gain_center` | 1.0 / 1.0 / 0.4 / 1.0 | mix gains |
| `volume_follow` | on | cable playback-side volume is the single master knob |
| `volume_endpoint` | (empty) | needle for the cable's PLAYBACK endpoint; empty = try `input_device` |
| `pin_outputs` | on | pin out endpoints 100%/unmuted (no-op on KS legs) |
| `out2_first` | on | center leg pumped first, higher priority |
| `keepalive_hz`, `keepalive_gain` | 40 / 0.01 | out2 keep-alive tone (5..2000 Hz / 0..0.25) |
| `chimes`, `chime_gain` | on / 0.4 | startup + watchdog-recovery blips on the center speaker (0..1) |
| `background` | off | detach after successful start; log to `twinspeak.log` |
| `backend1`, `backend2` | wasapi | **`wasapi` recommended**; `ks` = FlexASIO-inspired WDM-KS, per-leg fallback to WASAPI |
| `ks_period_frames` | 96 | KS buffer-switch size in samples (32..4096, logged clamp) |
| `ks_periods` | 4 | KS looped buffer in periods (2..8; 1 clamps to 2, logged) |
| `clock_sync` | on | device-master replay/drop rules (legacy `auto_resync` accepted) |
| `drift_guard` | on | automatic drift re-pin on top of the always-on ledger |
| `drift_correct_ms` | 10 | trigger distance from baseline (clamped 2..500) |
| `center_gate` | on | ch3 digital zero → out2 keep-alive only |
| `center_upmix_guard` | on | mute Windows' stereo→8ch FC upmix fingerprint |
| `power_guard` | — | sleep survival: suspend parks, resume rebuilds, collapse sentinel reopens; default ON even when the key is absent (not in `generate_ini.bat`) |

## Building from source

```bash
bash build.sh
```

Single translation unit (`twinspeak.cpp` + vendored `miniaudio.h`), static x64 console exe. The script prefers llvm-mingw and falls back to `zig c++`. v6.5.1 keeps the optimization-pass flags: `-O3`, `-march=x86-64-v3` (SSE4.2/AVX2 auto-vectorization), `-ffp-contract=off` (no FMA fusion — the bit-exact guarantee), `-flto`. Remove `-march` for a pre-2013 CPU; the code is architecture-neutral C++17.

## Repository layout

```
twinspeak.exe        the engine (from Releases)
generate_ini.bat     writes twinspeak.ini (interactive prompts, Enter = tuned default; advanced key=value prompt for any key; self-verifies)
twinspeak.ini        reference copy of exactly what the bat generates
twinspeak.cpp        the whole engine (single translation unit)
miniaudio.h          WASAPI/output layer (vendored, https://miniaud.io)
build.sh             llvm-mingw / zig build
CHANGELOG.txt        full version history, v6.0 -> v6.5.1
LICENSE              MIT (see Third-party components for miniaudio)
.gitignore           build + runtime artifacts
```

## Honest limits

* The host can only see the HOST pipeline. A USB speaker's internal firmware buffer (the reference S5 measured ~65 ms) sits below the USB packetizer: invisible to the ledger, invisible to every transport setting, fixable only by the delay knobs or different hardware.
* Device-side latency is a firmware decision. USB audio itself adds about a millisecond; everything beyond that is the speaker's own FIFO.
* ~70 ms of absolute path latency after alignment is the price of a matched pair on the reference rig. If video lip-flap ever bothers you, that is the player's video-delay slider, not another ini knob.
* Two KS legs may not both win pins on the SAME filter (pin instances are limited); the per-leg WASAPI fallback covers it.

## Credits

This repository's code exists because these projects paved the way — thank you:

* **[miniaudio](https://miniaud.io) by David Reid** — the WASAPI output/capture layer. `miniaudio.h` is vendored here and does the heavy lifting of every exclusive WaveRT device open on both legs. Dual-licensed public domain / MIT-0; used here under the public-domain grant, so it adds no terms of its own.
* **[FlexASIO](https://github.com/etcmstar/FlexASIO) by Etienne Dechamps** — the proof that Windows Kernel Streaming can be driven at ASIO-grade latency. twinspeak's `backend = ks` is an original implementation of the public KS/WaveRT property interface, but the *idea*, the wire layout and the confidence all came from studying FlexASIO. No FlexASIO code is used.
* **[VB-Audio Virtual Audio Cable](https://vb-audio.com/Cable/)** — the 8ch capture backbone (enable Clock Correction for it in the VAC Control Panel to zero the `off` column).
* Lineage: CenterFold → twinspeak v3 (two-device relay) → v6.0 clock discipline → v6.1 alignment + center gates → v6.2 background + chimes → v6.3/6.3.1 WDM-KS → v6.4 Drift Warden → v6.5 bit-exact optimization pass → v6.5.1 the `err` column.

## License

MIT — see [LICENSE](LICENSE). The vendored `miniaudio.h` keeps its own public domain / MIT-0 statement inside the file; the LICENSE's Third-party components section covers it. Swap the `Copyright (c) 2026 twinspeak contributors` line for your name or GitHub handle when you publish.
