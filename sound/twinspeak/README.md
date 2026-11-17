Disclaimer: This tool was made for free, bypassing the requirement of paying a donation for e.g. voicemeeter potato, vb audio vac, or the eugene muzychenko equivalent.

Use this at your own risk.

## Download

[TwinSpeak Program](https://github.com/TidyVision/fun/raw/main/sound/twinspeak/TidyVision%20TwinSpeak.zip)

# Twinspeak

TwinSpeak is a powerful audio-routing program where you actually use Virual Audio Cable for something meaningful (or software that's equivalent to it).

In this software is a GUI that's ran with it, along with the feature to minimize the audio into system tray.

When you start the engine, ultra-responsive feedback is provided with it, using a [futex](https://devblogs.microsoft.com/oldnewthing/20170601-00/?p=96265/) for absolute max delivery, sent against miniaudio's already fast event-driven system.

Internally, a sample size of 192 sample against a period size of 96 is used.

And finally, this software's also impressive for also packing a major HRTF function used for surround channels and a 0.51ms comb filter used for rear channels in such a small app *with the help of A.I.*

There should be a built-in E.Q. setting to use instead of utilizing Equalizer APO, as that software uses shared-mode, where as this software bypasses all of that by WASAPI Exclusive.

## Requirements

* [Eugene Muzychenko Virtual Audio Cable](https://vac.muzychenko.net/) installed as the 5.1 source target: **8 channels, 48000 Hz**, and the **WaveRT port driver** selected in the VAC Control Panel (a WaveCyclic port doubles the shared-mode capture clamp; WaveRT unlocks the real burst).
* Two output devices. The reference rig: onboard Realtek (mains) + Adesso Xtream S5 USB speakers (center). Any devices work — you match them by name substring.
* A 5.1-capable source app that can address the cable.

## Credits

This repository's code exists because these projects paved the way — thank you:

* **[miniaudio](https://miniaud.io) by David Reid** — the WASAPI output/capture layer. `miniaudio.h` is vendored here and does the heavy lifting of every exclusive WaveRT device open on both legs. Dual-licensed public domain / MIT-0; used here under the public-domain grant, so it adds no terms of its own.
* **[FlexASIO](https://github.com/etcmstar/FlexASIO) by Etienne Dechamps** — the proof that Windows Kernel Streaming can be driven at ASIO-grade latency. twinspeak's `backend = ks` is an original implementation of the public KS/WaveRT property interface, but the *idea*, the wire layout and the confidence all came from studying FlexASIO. No FlexASIO code is used.
* **[Eugene Muzychenko Virtual Audio Cable](https://vac.muzychenko.net/)** — the 8ch capture backbone (enable Clock Correction for it in the VAC Control Panel to zero the `off` column).
* Lineage: CenterFold → twinspeak v3 (two-device relay) → v6.0 clock discipline → v6.1 alignment + center gates → v6.2 background + chimes → v6.3/6.3.1 WDM-KS → v6.4 Drift Warden → v6.5 bit-exact optimization pass → v8 remove wdm-ks entirely for simplification → v9.3 futex implementation.

## License

MIT — see [LICENSE](LICENSE). The vendored `miniaudio.h` keeps its own public domain / MIT-0 statement inside the file; the LICENSE's Third-party components section covers it. Swap the `Copyright (c) 2026 twinspeak contributors` line for your name or GitHub handle when you publish.

PROPERTY OF TIDYVISION, GENERATED FROM GLM 5.3 FLASH (and later regular GLM 5.3)

POST-POLISH DONE THROUGH OCT. 28 2026 to NOV. 17 5:14PM | PROJECT STARTED ON SEPT. 28 at around 3:50PM

FORMERLY CENTERFOLD