// v8.4.11: the Windows link set, carried by the source so a bare cl.exe line
// needs no .vcxproj (field-directed compile command):
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "propsys.lib")

// ============================================================================
//  twinspeak v8.4.11 -- ASIO-STRICT bit-exact engine (no resampler anywhere)
//
//  MISSION: one 8ch shared capture of a VAC cable fans out (memcpy only) into
//  independent per-leg pipelines -- leg1 = Realtek FL/FR + LFE fold (5.1),
//  leg2 = Adesso Xtream S5 (center-only), leg3 = THE REARS ON ONE DEVICE
//  (stereo: L = ch7+ch5 at gain_rl, R = ch8+ch6 at gain_rr; optional: an
//  EMPTY device keeps the rears silent) -- WASAPI/WDM-KS exclusive, every
//  sample bit-exact, no resampler or pitch path anywhere. delay1/2_ms +
//  offset3_ms are whole-ms prefills on the raw ring; drift is measured,
//  never resampled (v8.1.0) -- v8.4.7 SLOW SYNC: every leg EXCEPT the first
//  holds its settled offset to leg1's play boundary (a 16-frame drop/pad at
//  most every 100 ms, v8.4.9; no key, no UI), and v8.4.11 adds the old
//  warden's 5 ms / 2 s BITE on top so slow creep is corrected, never
//  accepted; the cable clock itself stays measure-only.
//
//  ARCHITECTURE (v8.1.0 -- THE STRIPPED PATH):
//    VAC cable (8ch shared capture, exclusive)
//      +--> leg1.raw ring (the fan-out) --> the switch mixes INLINE --> Realtek
//      |      mix: FL=L+gain_lfe*LFE, FR=R+gain_lfe*LFE, FC/LFE/rear slots
//      |      = driven silence (the rears AND the sides live on leg3/leg4;
//      |      v8.4.6 folds the sides onto the rear legs)
//      +--> leg2.raw ring (the fan-out) --> the switch mixes INLINE (out2 first)
//      |      gates: ch3 zero -> mix muted; ch3 == Windows stereo-upmix
//      |      fingerprint -> muted; mix: both outs = ch3 only, rest muted
//      +--> leg3.raw (v8.4.11: THE REARS ON ONE DEVICE, OPTIONAL)
//             mix: out L = gain_rl*(ch7+ch5), out R = gain_rr*(ch8+ch6) --
//             the rear pair at plain gain + THE v8.4.6 SIDE FOLD (the
//             sides ch5/ch6 ride their same-side rear OUTPUT at their
//             gain; out1's side slots stay ZERO so the sides never play
//             anywhere but here), each lane gated BY ITSELF (ch7/ch8/
//             ch5/ch6 zero -> that lane's term muted; the mix skips only
//             when ALL FOUR lanes are zero); EMPTY device = leg silent
//      v8.4.5: EVERY leg sums the universal keep-alive at the device layer
//      -- 20 Hz @ 0.1% on the driven slots (leg1: FL/FR only), through
//      every gate and every gap: a gated leg is PROGRAM-silent, never
//      device-silent
//    The buffer switch pops the leg's raw ring DIRECTLY -- there are no out
//    rings, no pump threads, no ring-depth targets, no replay/drop-correct/
//    escalation machinery and no capture-burst accounting (v8.1.0). A
//    shortfall plays SILENCE and is counted; capture excess evicts oldest at
//    the raw cap. delay1/2_ms is a silence PREFILL on the raw ring. The DAC's
//    own position register (KS) or the WASAPI event paces every write; the
//    KS deadline poll waits on the high-resolution waitable timer (100 ns
//    due) -- no busy wait anywhere in the engine.
//
//  CHANGELOG (condensed; full notes in CHANGELOG.txt):
//    v8.4.15 THE DELAY-NEUTRAL METRIC + THE STALL RESYNC RETIRED + THE
//            REALTIME PRIVILEGE ENABLE (field-directed: "Make sure that
//            leg1, leg2, and leg3 all be subtracted by their individual
//            delays (delay1_ms, delay2_ms, and delay3_ms) ... so that
//            measurements are accurate ... I don't want a device being
//            silent to reopen (if at all) ... make setting the process
//            priority to real-time work"). (1) THE DELAY-NEUTRAL METRIC:
//            the user set delay1_ms = 68 and watched raw2 climb to
//            roughly 68 -- CONFIRMED in code: the servo's raw seqTail
//            difference included the MASTER's calibration delay, so it
//            read the center 68 ms early and padded raw2 up to the
//            mains' DELAYED boundary -- the calibration defeated by its
//            own servo. The error is now measured between the UNDELAYED
//            boundaries: each leg's seqTail is shifted forward by its
//            own delay (leg1 = delay1_ms, leg2 = delay2_ms, leg3 =
//            offset3_ms), i.e. err = (depth2 - delay2) - (depth1 -
//            delay1) -- the backlogs ABOVE the configured delays, the
//            prefills untouched. The depth guard and the reopen landing
//            already measured delay-subtracted; the servo was the last
//            delay-contaminated meter. (2) THE STALL RESYNC IS RETIRED:
//            a starved leg plays the keep-alive through the hole and a
//            silent stream loses nothing -- the 3 s starvation reopen
//            was pure churn (the ding, the restart window, the landing).
//            The heartbeat WATCHDOG still reopens a thread that stopped
//            SWITCHING entirely (a dead device never returns alone); a
//            leg that merely starves rides the next capture burst.
//            (3) THE REALTIME PRIVILEGE: SeIncreaseBasePriorityPrivilege
//            is now explicitly ENABLED before SetPriorityClass -- the
//            privilege is often held-but-disabled at launch, which made
//            REALTIME refuse even under an administrator. NO new ini
//            key, NO UI row.
//    v8.4.14 THE DEFERRED REOPEN LANDING (field-directed, with the log:
//            "Just heard a second ding which raised raw2 to 43. This is
//            getting me suspicious."). The ding is the reopen chime
//            (armChime fires on every leg2 reopen); the 43 was the
//            v8.4.12 landing being UNDONE: dropReopenBacklog trimmed the
//            ring to its anchor AT REOPEN TIME, but the new device takes
//            hundreds of ms to actually start consuming, and in that
//            window the capture kept pushing and refilled the ring back
//            toward its cap -- the field log's own sequence (stall resync
//            + watchdog reopen on the S5, then "sync out2: 2064 fr late"
//            = exactly 43 ms, mid-grind). The first pop saw ~160 ms of
//            backlog and the servo spent ~25 s grinding it. THE FIX: a
//            reopen only MARKS the leg (relandPending); the NEW device
//            thread lands its own ring at its FIRST buffer switch
//            (landReopenedRing, called from bufferSwitchFill) -- the
//            discard and the first pop are one instant apart, nothing can
//            refill in between, and the landing target is the servo's own
//            (r2 = max(r1, anchor) + own prefill), floored at the depth
//            guard's 10 ms anchor so the restart's first fill cannot
//            starve on a capture gap. NO new ini key, NO UI row.
//    v8.4.13 NO BASELINE (field-directed, eight idle minutes + one glance:
//            "I do see a slow creep in delay on both raw1 and raw2 ... raw1
//            is at 8 and raw2 is at 12-16 ... I just saw raw2 jump to like
//            70 after raw1 went to 11 and then 7. This is suspicious."). raw2
//            PARKED at raw1 + ~60 ms while the servo sat silent was the
//            settle latch -- the LAST acceptance path v8.4.12 missed: two
//            seconds after any reopen it captured whatever off happened to
//            be, and if a burst (or the tail of a grind -- 60 ms takes ~10 s
//            at the warden's 5.7 ms/s) was still in the rings, the bloat
//            became the servo's TRUTH: it then defended raw2 = raw1 + bloat
//            with pads and drops like any other calibration. The baseline
//            also random-walked up a few ms per event -- the raw2-4-8-ms-
//            over-raw1 creep. THE FIX: syncBase/kSyncUnlatched are RETIRED
//            whole -- the servo's error IS the measured offset now (err =
//            off); the settle gate is a pure startup hush; the reopen
//            landing is r2 = r1 exactly. A nonzero settled offset was
//            startup skew, and startup skew is drift to be corrected, not
//            held. NO new ini key, NO UI row.
//    v8.4.12 THE DEPTH GUARD + THE DELAY-CORRECTED LANDING + NO RE-LATCH
//            (field-directed, one hour of field data: "I left it sitting
//            there for an hour ... I still get raw2 drift. Somehow, I got
//            it to get to 62ms ... Maybe delay1_ms is being added to the
//            drift corrector (which it shouldn't have)? ... whenever I
//            open e.g. obs that raw2 jumps to 162ms"). THREE mechanisms,
//            all confirmed in code: (1) THE DELAY LEAK -- the user's
//            hypothesis, CONFIRMED, but in the REOPEN path, not the
//            servo: dropReopenBacklog landed a reopened slave at leg1's
//            FULL ring depth + held -- and leg1's depth includes the
//            delay1_ms prefill -- so with delay1 = 68 every reopen put
//            ~68 ms of phantom backlog on raw2 (the "62 ms"), which the
//            servo then spent ~11 s grinding back off; the landing is
//            now delay-corrected (leg1's REAL backlog + held + the
//            slave's own prefill). (2) THE DEPTH GUARD -- the servo's
//            off = r2 - r1 is the REAL-backlog DIFFERENCE, blind to
//            COMMON-MODE ring growth: a shared-mode capture burst
//            (audiodg starved under load -- exactly an OBS open)
//            permanently added its size to EVERY ring, ratchet by
//            ratchet, and a big spike pinned them at the power-of-two
//            caps (8192 fr = 170.7 ms at delay 0 -- the "162 ms").
//            NOTHING drained depth back down; now the master's real
//            backlog is pinned to a 10 ms anchor past a 5 ms dead-band,
//            dropping the oldest frames at the warden's exact pacing
//            (16 fr / 100 ms, 240 fr / 2 s), and the slaves drain with
//            it through the err loop -- the legs drain TOGETHER, the
//            calibration intact, logged as 'depth out1'. (3) NO
//            RE-LATCH -- the v8.4.11 step test retired: both it and the
//            old |err| test silently ACCEPTED bloat as a new baseline;
//            a jump is now ground back like any other error, and only a
//            reopen re-arms the baseline (the settle gate). NO new ini
//            key, NO UI row.
//    v8.4.11 THE INSTANT ZERO, THE WARDEN BITE, THE ONE REAR LEG + THE
//            384x3 BITE (field-directed: "I still hear this center channel
//            upmix ... I think the audio device drifting is from the fact
//            the sample size is still a tad too small ... I want it so
//            it's 384 samples with 3 buffers instead ... raw2 is increasing
//            in offset over time without me knowing it ... something
//            happens and raw2 increases up to like 36ms ... Apply the same
//            thing to rear L and rear R, but combine both to just be one
//            leg3 so you only have to handle one device of the rear at
//            once"). (1) THE INSTANT ZERO: the upmix guard's 2.5 ms fade
//            is RETIRED -- a fingerprint HIT zeroes the chunk that
//            produced it on the spot (the fade was the leak: every engage
//            let ~120 frames of a*FL+b*FR through at ramping gain, the
//            field audibly; deepseek's diagnosis, confirmed in code); a
//            MISS plays it on the spot. No fade, no ramp, no anti-click
//            compromise -- the v8.4.10 "play the moment the input frame is
//            detected" contract, byte for byte. (2) THE WARDEN BITE: the
//            old version's drift correction (the shipped ini:
//            drift_correct_ms = 5, drift_interval_ms = 2000) returns
//            INSIDE the sync servo -- past 5 ms of accumulated drift a leg
//            drops (or held-sample pads) up to 240 frames in one bite, at
//            most once per 2 s, on top of the 16 fr / 100 ms wide nudge.
//            THE 36 MS ABNORMALITY IS THE OLD RE-LATCH: kSyncRelatchFr
//            could not tell slow creep from a reopen-class jump, so every
//            ~40 ms of accumulated drift got silently accepted as the new
//            baseline ("increasing over time without me knowing it") -- a
//            jump now re-latches only when the offset moves ~20 ms inside
//            ONE servo window (an eviction batch, a stall, a reopen);
//            CREEP is corrected, never accepted, and the bites log.
//            (3) THE ONE REAR LEG: leg3 + leg4 merge -- ONE rear device,
//            out L = gain_rl*(ch7 + ch5), out R = gain_rr*(ch8 + ch6),
//            all four lanes gated BY THEMSELVES ('rear L / side L / rear R
//            / side R gate'), the mix skipping only when ALL FOUR are
//            zero; output_device4 and offset4_ms are read-and-ignored
//            (stale inis load clean), the gain quad and the delay quad's
//            first three knobs keep their keys, and the servo + bite ride
//            leg3 like every non-master leg. (4) THE 384x3 BITE: 384
//            frames x 3 periods -- 8.0 ms of service, ~125 writes/s, a
//            1152-fr / 24.0 ms WaveRT loop and a 768-fr / 16.0 ms write
//            lead (deeper than every prior default; the field's own
//            diagnosis for the drift stress). NO new ini key, NO UI row
//            beyond the retired leg4 controls; the est-path banner prints
//            the deeper lead live.
//    v8.4.0 FOUR LEGS + THE FOLD IS BUILT IN (field-directed):
//            leg3 (rear L) and leg4 (rear R) carry the cable's rear pair
//            (ch7 RL / ch8 RR) on their OWN devices: out3 = gain_rl * ch7
//            on both outputs, out4 = gain_rr * ch8, offset3_ms/offset4_ms
//            the per-leg bit-exact delays. An EMPTY rear device = that leg
//            simply never opens (silent, no error); out1's 5.1 rear slots
//            stay DRIVEN SILENCE either way. lfe_fold is RETIRED with its
//            pill -- the fold is the confirmed routing, hardcoded on
//            (gain_lfe still scales it); the key is read-and-ignored. The
//            GAIN column compacts to a four-element quad (gain_l/gain_r/
//            gain_rl/gain_rr, gain_lfe below); TIMING gets the same quad
//            for delay1/delay2/offset3/offset4.
//    v8.4.10 THE INSTANT CENTER GATE + THE REOPEN BACKLOG DISCARD
//            (field-directed: "I found that the chattering comes from the
//            center channel still playing the l+r mono mix, which makes me
//            think the absolute center gate is being delayed by exactly one
//            buffer frame, so I want that fixed and that's it ... I want
//            the center gate to just play the moment the input frame is
//            detected to have it, which means before you play the output
//            data to the selected device" + "I still get this eventual bug
//            sometimes that there's this loopback sounding sound that plays
//            within the current twinspeak stream that goes away on reset,
//            and that's what I want you to fix as well"). THE INSTANT GATE:
//            the upmix guard's v7.8.4 hysteresis (engage after 2 hits,
//            release after 8 misses) retired -- it left the verdict whole
//            buffers behind the evidence, so a genuine center onset stayed
//            muted 12-48 ms and an upmix leaked a block the other way (the
//            chattering at every content transition): a fingerprint HIT now
//            mutes the chunk it was measured on, a MISS plays it, and ch3
//            alive against fronts too quiet to fit (a*0 + b*0 = 0) is a
//            definitive real center, released on the spot; the 2.5 ms fade
//            stays as the anti-click at a flip, not a delay; cgHits/cgMiss
//            retire. THE BACKLOG DISCARD: while a leg is down (the 30-min
//            deep resync, the watchdog, a re-enumeration) the capture fills
//            its raw ring to cap and eviction pins the read position ~170 ms
//            behind the freshest capture -- the reopened leg played a
//            permanent delayed copy of the stream (the loopback-sounding
//            echo) and the sync servo re-latched the >2000-fr offset,
//            holding it until reset; every reopen now discards the backlog
//            in its dead-consumer window (the old device thread is joined;
//            tail advances are monotonic CAS) and resumes at the freshest
//            capture, the discard LANDING ON THE OFFSET THE SERVO WAS
//            HOLDING (syncBase: the user's delay calibration survives the
//            reopen; the fallback keeps the delay prefill + one loop of
//            margin). NO new ini key, NO UI row.
//    v8.4.9 THE WIDE NUDGE + THE POWER BITE (field-directed: "I would
//            also do a slow sample-level nudge every 100ms and have the
//            buffer be 288 samples and 2 buffers (since it's slower but
//            more power saving that way). Also, I still get L/R
//            stuttering, which means the 1-frame nudge is clearly causing
//            decoherence in the sample level stuff, and that's why I want
//            a 16 sample nudge for every 100ms"). THE WIDE NUDGE: the
//            servo's step is 16 frames every 100 ms (was 1 fr / 200 ms)
//            -- the drop discards 16 oldest frames at once, the pad holds
//            the last mixed frame across 16 switch frames (the DAC still
//            gets its full frameCount; a held sample, never a hole),
//            capacity 160 fr/s ~ 3333 ppm; the 12-fr / 250 us dead-band,
//            the 2 s settle and the 2000-fr re-latch unchanged; the
//            clamp keeps the pad under the block so it always repeats a
//            REAL mixed frame. THE POWER BITE: 288 frames x 2 periods --
//            6.0 ms of service, ~167 writes/s, a 576-fr / 12.0 ms loop
//            and a 288-fr / 6.0 ms lead: fewer, larger writes than
//            v8.4.8's 256 x 4 (power saving, field-directed); the double
//            cushion RETIRES with the field's own diagnosis -- the L/R
//            stutter rode the 1-frame nudge's decoherence, not the lead.
//            NO new ini key, NO UI row; the est-path banner prints the
//            shallower lead live.
//    v8.4.8 THE DOUBLE CUSHION + THE TIMING NOTE TRIMMED (field-directed:
//            "I want to have a 256 sample buffer and 4 samples instead,
//            and also prevent the left and right from stuttering. And that
//            should be it. Also, remove the two whole paragraphs after the
//            first one from the timing tab, because it's overflowing").
//            THE BITE: 256 frames x 4 periods (was 192 x 3) -- 5.33 ms of
//            service per write, ~188 writes/s, a 1024-fr / 21.3 ms WaveRT
//            loop and a 768-fr / 16.0 ms write lead. THE L/R STUTTER: the
//            mains' left/right stuttered when a loaded video decoder
//            descheduled the pacer straight through v8.4.5's 8.0 ms lead
//            into a starved DAC -- the DOUBLED lead keeps the DAC fed
//            through the same spike (same kernel loop, same register
//            pacing, no resampler anywhere; leadFrames is still
//            (periods-1)*period and the est-path banner prints the deeper
//            lead live). NO new ini key, NO UI row: the two constants
//            carry it all, stale ks_period_frames / ks_periods lines stay
//            read-and-ignored. THE TIMING NOTE: only its FIRST paragraph
//            remains (the KS-bite and slow-sync paragraphs are gone from
//            the tab, which overflowed) -- both stories stay told by the
//            boot banner and the ini.
//    v8.4.7 SLOW SYNC TO THE FIRST DEVICE + THE TIMING PAGE MOVES LEFT
//            (field-directed: "each audio device delays eachother over
//            time and I want you to just fix that ... I want a slow drift
//            between each audio device and the others which should all
//            sync together to exactly that first audio device ... just
//            minimally implemented and that's it. And no adding new ini
//            keys nor have them on the user interface either way").
//            Every crystal walks its own ppm, so over an hour the legs'
//            relative offset creeps by tens of ms until the raw cap evicts
//            or the stream starves and the offset JUMPS -- that is the
//            "delays eachother over time". THE FIX: out2/out3/out4 servo
//            to OUT1's play boundary (seqTail -- pure frame arithmetic, no
//            clock, no resampler, nothing bends pitch): each leg latches
//            its SETTLED offset to leg1 two seconds after its last
//            (re)open, then holds it -- a 1-frame nudge at most every
//            200 ms, only past a 12-frame (250 us) dead-band. A LATE leg
//            (its crystal runs slow, the ring grows) DROPS one oldest
//            frame; an EARLY leg PADs one frame (the switch pops one frame
//            less and REPEATS its last mixed frame -- a held sample: the
//            DAC always receives its full frameCount, the wobble rides
//            the repeat like any frame). A leg that jumps past 2000 fr (an
//            eviction batch, a stall, a reopen) RE-LATCHES its baseline
//            instead of grinding the distance back. Leg1 itself is NEVER
//            touched (the master); a yielded or heart-silent leg1 freezes
//            the sync whole (a parked leg's ring still evicts, and eviction
//            jitter is not a clock to follow). MINIMAL BY DECREE: no ini
//            key, no UI row, constants in the config block, nudges logged
//            rate-limited. THE TIMING PAGE: its rows move from the right
//            column to the LEFT (COLA_X), field-directed, unchanged
//            otherwise; the note gains the sync story.
//    v8.4.6 THE SIDE FOLD + EVERY LANE GATED BY ITSELF (field-directed:
//            "the rear channels get the gate alone. I would not do ch8
//            -> ch4. Just have ch7 and ch8 make the output ch7 and ch8
//            get gated by itself, along with the input's ch5 and ch6
//            being gated to the output's ch5 and ch6 the same way. I
//            would also like to have the output's ch5 and ch6 to be
//            folded onto ch7 and ch8 and then zeroing out the samples of
//            ch5 and ch6 altogether to prevent it from playing to begin
//            with"). NO ch8 -> ch4 anywhere -- nothing folds onto the
//            LFE slot or the fronts from the rears; the rears keep their
//            own legs. (1) THE SIDE FOLD: out3 = gain_rl*(ch7 + ch5) and
//            out4 = gain_rr*(ch8 + ch6), both outputs of each leg -- the
//            cable's SIDES (ch5 SL / ch6 SR, the slots a 5.1 movie's
//            surround lands on under the Windows mask order) fold onto
//            their SAME-SIDE rear leg, riding the LEG's gain: one
//            calibrated knob per speaker (the LFE/center folds ride
//            their own gain_* because those channels have knobs; the
//            sides have none, and the speaker's calibration is the
//            truth). A 5.1 movie (surround on ch5/ch6, ch7/ch8 zero)
//            now plays its surround on the rear speakers natively -- the
//            v8.4.0 "flip the kRear*Ch constants" advice RETIRES. A 7.1
//            movie SUMS sides and backs on each rear leg (a fold, not a
//            switch). FP: the side term APPENDS after the rear term
//            (gRear*ch7 + gRear*ch5) and g*0 = 0 / x + 0 = x, so a
//            silent side lane is a bit-exact no-op -- with ch5/ch6
//            digital zero the legs play the v8.4.5 bytes. out1's 5.1
//            side slots (the stream's out ch5/ch6) stay DRIVEN SILENCE,
//            hardcoded: the sides never play out1, they ride the rears
//            ONLY -- zeroed "to prevent it from playing to begin with".
//            (2) EVERY LANE GATED BY ITSELF: ch7 all-zero mutes ch7's
//            term on out3 ('rear L gate'), ch8 all-zero mutes ch8's term
//            on out4 ('rear R gate'), ch5 all-zero mutes the fold's term
//            on out3 ('side L gate'), ch6 all-zero mutes the fold's term
//            on out4 ('side R gate') -- the center's contract, verbatim
//            (fully-served blocks only, no fade, no hysteresis, no ini
//            switch; the universal keep-alive rides on). A rear leg's
//            mix now SKIPS only when BOTH its lanes are digital zero:
//            a live side plays through a silent rear, a live rear
//            through a silent side. The center's ch3 gate is untouched
//            -- same lane, same EQ flush, same log lines, byte for byte.
//    v8.4.5 THE UNIVERSAL KEEP-ALIVE + THE REAR GATES + THE CALM BITE
//            (field-directed, three findings in one release):
//            1) THE KEEP-ALIVE IS BACK, EVERYWHERE: 20 Hz @ 0.1% (0.001,
//               ~-60 dBFS -- the field retuned v7.1.24's 40 Hz @ 0.5%),
//               summed at the device layer onto EVERY output leg, every
//               block -- through the lane gates, through the stop gap,
//               starved or not, phase-continuous PER LEG (each device
//               thread owns its own table index; the table is shared
//               read-only). leg1 carries it on FL/FR only -- the 5.1
//               stream's FC/LFE/rear slots stay DRIVEN SILENCE, the
//               upmixer-exclusion contract. v8.4.4's "literal digital
//               silence" is walked back, field-directed: a gated block is
//               PROGRAM silence, never device silence -- that is what
//               keeps the amp fed. The chime stays out2's alone.
//            2) THE REAR GATES: the v8.4.4 absolute gate is no longer
//               center-only -- an all-zero ch7 block mutes the out3 mix,
//               an all-zero ch8 block mutes the out4 mix (same contract:
//               fully-served blocks only, bit-identical skip, no fade, no
//               hysteresis; the universal keep-alive rides on). State
//               changes log 'rear L gate' / 'rear R gate' like 'center
//               gate'.
//            3) THE CALM BITE (the VLC crackle, field-directed): v8.4.3's
//               sub-ms bite (32 fr = a 0.67 ms cadence, ~1500 wakes/s)
//               crackled erratically under a video decoder's load -- the
//               missed services lapped the DAC into stale audio. The bite
//               is BUILT IN now: 192 fr (4.0 ms of service, 250 wakes/s --
//               six times the deschedule cushion) x 3 periods (a 576-fr /
//               12.0 ms loop, an 8.0 ms write lead -- SMALLER than the
//               v8.4.3 floor's 10.7 ms, so the transport latency drops).
//               The 0.33 ms clamp floor, the sub-128 loop floor (768/512)
//               and the sub-ms banner retire with it; ks_period_frames /
//               ks_periods are read-and-ignored (stale lines load clean),
//               the GUI rows are gone (TIMING keeps the delay quad +
//               out2_first) -- one timing truth for every first-run user.
//    v8.4.4 THE ABSOLUTE CENTER GATE + THE GUARD'S SMALL-BITE REPAIR
//            (field-directed: "channel 3 is still mixing left and right
//            even when there's zeroes in the whole buffer block of
//            channel 3's input -- muting the center device whenever the
//            capture input's channel 3 samples are detected to have all
//            zeroes in the current buffer"). (1) THE ABSOLUTE CENTER GATE:
//            an all-zero ch3 block now mutes the CENTER DEVICE itself, not
//            just the channel. Every prior gate (v6.1's center_gate, the
//            v7.8.4 guard) silenced the MIX -- but the device layer went
//            on summing the 40 Hz keep-alive wobble through every one of
//            them ("out2 = keep-alive only" was the honest log line): a
//            zero-ch3 block still put a live signal on the center device,
//            and anything downstream that keys on input activity (an amp
//            DSP, an auto-on circuit) stayed engaged. The absolute gate is
//            total and unconditional: a FULLY served block whose every
//            ch3 sample is 0.0f (+0.0f and -0.0f both) skips the mix,
//            resets the center EQ and kills the keep-alive wobble --
//            literal digital silence out, no fade, no hysteresis, no ini
//            switch. A starved block (a capture hole) keeps the wobble:
//            gap-silence is the keep-alive's own job, not center content.
//            An armed chime still plays (it is the user's own test, not
//            ambient content). State changes are logged ("center gate:
//            ... MUTED / live again") -- if the center speaker still mixes
//            L/R while the log says MUTED, the leak is provably below the
//            engine (driver DSP or hardware summing), and the log is the
//            evidence. (2) THE GUARD'S SMALL-BITE REPAIR: the upmix
//            fingerprint test required n >= 64 frames -- v8.4.3's 32-frame
//            default bite silently DISABLED it (and a sub-64 WASAPI period
//            had never run it either), so a stereo source's Windows upmix
//            phantom (ch3 = a*ch1 + b*ch2) played through untouched: the
//            literal "channel 3 mixing left and right". The fit now runs
//            from 16 frames (the clamp floor) up; the 2-hit/8-miss
//            hysteresis and the 2.5 ms fade absorb the noisier short-window
//            fit. gate 1's old log/latch block is gone with its "out2 =
//            keep-alive only" line -- the absolute gate owns zero blocks
//            now, and centerGate/centerUpmixGuard stay hardcoded-on
//            constants as they have been since v6.6.8.
//    v8.4.3 THE SUB-MILLISECOND BITE + THE HONEST WATERMARK (field-
//            directed: "roll back to v8.4.2 and start over from it; update
//            the very old v7.8.4 visual text to the current version; the
//            one optimization I want is a polling rate faster than 1 ms --
//            polish it like an iron fist and keep it as a center channel
//            router"). The v8.5-v8.8 line (the merges, the surround legs,
//            the zero watches) is RETIRED whole -- the baseline is the
//            v8.4.2 tree verbatim, and the app stays what the field
//            validated: out1 = L/R + LFE fold, out2 = center, optional
//            rears. (1) THE HONEST WATERMARK: the top-right corner still
//            read "v7.8.4 | <ini path>" -- a hard literal the v7.9.0
//            banner fix missed; it prints the real TWS_VERSION now, so it
//            can never go stale again. (2) THE SUB-MILLISECOND BITE: the
//            KS service cadence (the buffer-switch bite) goes under 1 ms
//            and STAYS steady. ks_period_frames defaults to 32 now (0.67
//            ms of service per bite at 48 kHz) and the clamp floor drops
//            32 -> 16 (0.33 ms); the deadline-zone poll was already the
//            finest expressible wait (one-shot 100 ns high-resolution
//            timer). What buzzed in the v6.5.14 field at sub-128 bites
//            was never the cadence -- it was the WaveRT LOOP shrinking
//            with the bite (bite x ks_periods): the lap window and the
//            write lead collapsed to a couple of ms and every ordinary
//            deschedule lapped the DAC into stale audio plus a re-pin
//            seam. A sub-128 bite now FLOORS the loop at the shipped
//            default's own numbers -- 768 frames (16 ms lap window) and a
//            512-frame (~10.7 ms) write lead, the cushion the field
//            proved steady -- while the bite writes as fine as 333 us.
//            The lapse guard's disagreement tolerance scales with the
//            loop (the fixed 2*bite+96 was degenerate at both ends: 96
//            alone exceeded the whole loop at a 32-frame bite, and 8288
//            exceeded the 8192-frame loop at bite 4096 x 2 periods).
//            Bites of 128+ are byte-identical to v8.4.2 (request, lead,
//            pacing, tolerance); the WASAPI fallback keeps its 1 ms period
//            request (a fallback's job is to open, not to race).
//    v8.4.2 THE STARTUP ROW, CORRECTED (field-directed): "bottom right"
//            meant the ROW'S ORDER, not a new control style -- "launch at
//            Windows startup" is a PLAIN tglRow again (label left, the
//            ON/OFF pill at the right, exactly every other row's shape),
//            only REORDERED to be the LAST row of page 0's right column.
//            v8.4.1's separately styled bottom-bar pill (its own "STARTUP
//            ON/OFF" face, off the settings page) is GONE; the bar keeps
//            its five controls and the row lives under the tabs with the
//            rest. No engine change.
//    v8.4.1 THE CENTER FOLD + THE STARTUP PILL'S NEW HOME (field-directed):
//            an EMPTY output_device2 is legal -- leg2 never opens and ch3
//            (center) folds onto leg1's FL/FR at gain_center, the LFE
//            fold's pattern with the SAME upmix guard riding it (a stereo
//            source's phantom FC fades out of the fronts; genuine 5.1
//            center always plays; the center EQ stays out2-only). The
//            launch-at-startup pill moved ALL THE WAY DOWN to the bottom
//            bar's right end -- its own always-visible control, off page 0.
//    v8.3.0 THREE TABS (field-directed). The warden's last tunables and the
//            volume path are RETIRED, hardcoded from the fielded ini:
//            drift_correct_ms 5, drift_interval_ms 5000, deep_resync_min 30,
//            volume_follow on. No GUI row, no splice key, no clamp; stale
//            ini lines load clean and are ignored. The SETTINGS page is GONE
//            with them -- its three survivors (launch at Windows startup,
//            enable_chime, chime_gain) live on page 0, and with nothing left
//            under the old label, DEVICES is renamed: the window reads
//            CONFIGURATION | TIMING | CENTER EQ.
//    v8.2.0 THE UNIFIED TIMING PAGE (field-directed). Both legs live on the
//            kernel loop now; the only knobs that still move anything are
//            the alignment delays and the KS bite -- so the UI says so:
//            1) FOUR TABS (DEVICES | TIMING | SETTINGS | CENTER EQ): the
//               TRANSPORT page is GONE -- its rows (period_frames/periods)
//               moved to the TIMING page's left column, and its description
//               was shortened and folded into the TIMING note (one blank
//               line between). The merge also fixes the field bug: the old
//               TRANSPORT note declared 13 lines for a 17-line text -- the
//               last four lines were clipped off the panel.
//            2) THE DEVICE PERIODS COLUMN IS GONE: out1ms/out2ms (and the
//               legacy buffer_size alias) are read-and-ignored -- the WASAPI
//               device period is an ENGINE CONSTANT (kOutPeriodMs = 1 ms, a
//               fallback leg's request; a KS leg ignores it whole -- equal
//               by construction). The GUI rows, the splice keys, the legacy
//               haveOut fallback and the banner's set-outXms hint are gone
//               with them. The KS path (the default) is byte-identical to
//               v8.1.0; only a WASAPI-fallback leg's request changes.
//    v8.1.0 THE STRIPPED-PATH EXPERIMENT (field-directed). The field closed
//            the loop on the crackle: the capture side (the VAC cable, ~4 ms
//            timing event, packet-mode RT Audio) delivers in bursts, and
//            every layer built to ABSORB those bursts -- the out rings and
//            their depth targets, the 16-switch miss history, the
//            drop-correct restart, the replay tiles, the warden pads and
//            skips, the pump danger-zone spin, the whole capture-burst
//            accounting system -- could turn the absorption itself into
//            audible seams. v8.1.0 removes the machinery, not the symptoms:
//            1) NO BUSY WAIT: the KS pacer's final-stretch spin (_mm_pause)
//               is GONE. Deadline polling waits on the high-resolution
//               waitable timer with the finest expressible due time -- 100 ns,
//               one tick (the syscall pair costs ~1-3 us, so the poll still
//               runs thousands of times per KS bite, but the thread BLOCKS
//               between polls: no core burn, preemptible, timer-driven).
//            2) THE RING SYSTEM IS REMOVED ENTIRELY: no out rings, no
//               targetFrames anchor, no floors, no prefill-to-target, no
//               drop-correct hold-off, no miss history, no replayFill, no
//               warden pads/skips, no stall-resync backlog discards, no
//               per-callback excess drops. The PUMPS are gone too -- the
//               buffer switch pops the leg's raw fan-out ring directly and
//               mixes INLINE (one code path for the KS pacer and the WASAPI
//               callback). A shortfall plays silence and is counted
//               ('short'); nothing ever repeats a frame.
//            3) THE CAPTURE BURST SYSTEM IS REMOVED ENTIRELY: cap_ms and
//               ring1_ms/ring2_ms are read-and-ignored (old inis load
//               cleanly); the capture opens with a fixed 4 ms request and
//               nothing consumes burst math anymore. GUI rows and ini keys
//               are gone from the value visual.
//            4) delay1_ms/delay2_ms survive as a silence PREFILL on the raw
//               ring (bit-exact, sequence-less -- the alignment calibration
//               holds across the upgrade). The warden stays as pure
//               MEASUREMENT (drift/err columns + baseline re-arm); its
//               corrections are gone.
//    v8.0.0 THE KERNEL LOOP + REALTIME. The v7.9.0 field log closed the
//            case: with MMCSS "Pro Audio" on every thread, BOTH legs still
//            served every other device switch ~2 ms late -- gap 4.15 ms on
//            a 2 ms period, margin -0.15 ms, ~250 late switches/s per leg.
//            That is a PHASE LOCK, not scheduler jitter: miniaudio's
//            event-driven device thread misses one event, its service
//            window eats the next boundary, and from then on it wakes at a
//            2-period stride arriving just past the DAC's stale boundary
//            every time. No priority can fix it -- MMCSS decides who wins
//            the CPU once RUNNABLE; it cannot wake a thread before its
//            event fires. So the scheduler is OUT of the render path:
//            1) KS PACING BY DEFAULT: both legs open WDM-KS pins (the v6.3
//               kernel-level loop, now the default transport) -- twinspeak's
//               own thread writes each bite the instant the DAC's own
//               position register says the lead was consumed. There is no
//               deadline to miss, so scheduling lateness cannot reach the
//               DAC. A refused pin falls back to wasapi automatically.
//               One-time ini migration carries old templates over (their
//               backendN = wasapi was boilerplate, not a choice).
//            2) REALTIME PROCESS CLASS: the engine takes
//               REALTIME_PRIORITY_CLASS (falls back to HIGH without the
//               admin privilege, logged); the KS pacer registers MMCSS
//               "Pro Audio" at AvrtPriorityCritical -- above both pumps.
//            3) The pump spin-pacer's danger zone follows the transport
//               (KS: one bite + burst; WASAPI: two periods + burst) -- the
//               old 2-period zone sat above a KS ring floor and spun the
//               pump against its 1.5 ms bound every tick, for nothing.
//    v7.9.0 THE SPIN PACER. The v7.8.4 escalation cured the replay-tile
//            static, but a ring can only clean up AFTER a late refill: the
//            static that survives is the device buffer itself running dry
//            when a deadline thread does not get scheduled. Three fixes, no
//            new ini keys:
//            1) PRIORITY INVERSION FIX: the v6.1 line demoted the MAINS pump
//               (idx 0) from TIME_CRITICAL (31) to HIGHEST (10) so the center
//               pump would "outrank" it -- at 100% CPU the mains pump starved
//               (leg1's underruns in the field log). Both pumps stay time-
//               critical now; the ranking happens INSIDE MMCSS: center =
//               AvrtPriorityCritical, mains = AvrtPriorityNormal (both far
//               above every normal thread; without avrt.dll they are equal).
//            2) PUMP SPIN PACER: when a leg's out ring falls under 2 device
//               periods + 1 capture burst of headroom (the danger zone), the
//               pump stops sleeping and busy-waits with _mm_pause for a
//               bounded 1.5 ms, draining the instant capture lands -- a late
//               tick can no longer compound into a second late tick. KS
//               pacers spin through the last period + 0.5 ms of slack against
//               the DAC's own position register (at the default switch size
//               that is the whole inter-switch window).
//            3) DEVICE-SWITCH LATENESS METER: the playback callback now
//               measures its own inter-switch interval; beyond period + 1 ms
//               it logs "the DAC grazed the buffer edge" -- the ground truth
//               no ring logic can see, so a residual device-side crackle
//               finally has evidence. The banner also stops claiming v7.1.19:
//               it prints the real TWS_VERSION now.
//    v7.8.4 THE SHALLOW-STAIRCASE FIX + THE CENTER FADE. Field evidence: a
//            phone recording of the speaker at 100% CPU showed the remaining
//            "static-ey" crackle is NOT the deep-collapse replay (that died
//            in v7.8.3): it is SUSTAINED SHALLOW shortfalls -- the out ring
//            hovers just above empty, EVERY switch misses a few frames, and
//            the legacy path still tile-REPLAYED the last buffer for those
//            (a seam every switch, 187.5 Hz at 256-frame switches, bursty at
//            the ~20 ms scheduler quantum -- 50 Hz chop in the recording).
//            Fix: each switch shifts a bit into a 16-switch miss history;
//            7+ short switches out of 16 escalates to the SAME clean
//            restart a deep shortfall gets (silence hold-off, rebuild to
//            the anchor, resume at the freshest capture). An isolated
//            shallow miss still replays in place -- sample-continuous with
//            the buffer just played, inaudible; only SUSTAINED starvation
//            restarts. Also: the center upmix guard stops crackling --
//            engage after 2 consecutive fingerprint hits, release after 8
//            consecutive misses, and every engage/release fades ch3 over
//            2.5 ms instead of hard-zeroing at a block boundary (the old
//            hit/miss flicker chopped dialogue into 10-20 ms slices). And
//            drop_correct is INTEGRATED: always on, the ini key is read
//            and ignored, the GUI pill is gone.
//    v7.8.3 DROP CORRECTION -- the ring restarts instead of crackling.
//            Field: "I feel like the ring system isn't stable enough, I
//            still sometimes get crackling even on buffer size 256 with 4
//            buffers -- it's related to how high CPU usage is. I want a
//            drop correction system integrated into it that makes the ring
//            system just start over instead of doing this crackling
//            thing." Diagnosis: under CPU pressure the pump/capture side
//            loses scheduling windows, the out ring drains, and the legacy
//            shortfall path tile-replayed the last played buffer EVERY
//            switch -- the ASIO underrun convention read as a fragment-loop
//            crackle. New ini key drop_correct (default ON, GUI pill on the
//            SETTINGS page): a DEEP shortfall (>= half the switch is hole)
//            (1) silence-fills the hole -- no duplicated audio, ever; (2)
//            holds the leg on silence while the pump rebuilds the ring to
//            its full target anchor (delay_ms depth included, so a delayed
//            leg returns with its calibration intact); (3) discards the
//            silent gap oldest-first (a bit-exact skip -- discardOldest
//            advances the sequence ledger) and resumes at the FRESHEST
//            capture with the ORIGINAL latency: the drift ledger returns to
//            its pre-drop value by itself, no warden correction, no latency
//            creep. A shallow miss (< half a switch) stays a sub-ms silence
//            sliver with no restart. Restarts are logged ('drop corr') and
//            counted in the devbuf line; a hold-off lasting 3 s feeds the
//            stall resync and reopens the leg, exactly like the old storm
//            path -- the safety net keeps working while the buzz is gone.
//            drop_correct = off restores the legacy replay bit-for-bit.
//    v7.8.2 THE BOOT YIELD. Field: "when twinspeak starts while roblox is
//            playing, it literally corrupts roblox's output driver list and
//            it results in a NoSoundDriver glitch"; also "fl studio's own
//            asio driver stops working until you click on the configurations
//            which is what makes it work again". Root cause: the boot
//            sequence opened BOTH legs WASAPI-EXCLUSIVE first and let the
//            yield_default tick release the default-matching leg a second
//            later -- but the damage is done inside that window. An
//            exclusive IAudioClient::Initialize attempt on an endpoint the
//            Windows engine is actively mixing to comes back
//            AUDCLNT_E_DEVICE_IN_USE, and on Realtek the REFUSED ATTEMPT
//            itself cycles the endpoint/pin state: every live shared-mode
//            client on that endpoint (FMOD inside Roblox, FL Studio ASIO's
//            WASAPI wrapper) eats AUDCLNT_E_DEVICE_INVALIDATED. FMOD parks
//            in its NoSoundDriver state and neither it nor FL-ASIO
//            re-initializes on its own -- the field workaround (switch the
//            default playback device away and back) worked only because the
//            default change forces those stacks to re-open. Fix: a leg whose
//            device IS the current default render endpoint, or that has
//            ACTIVE foreign audio sessions on it, now boots STRAIGHT INTO
//            THE YIELDED STATE -- its DAC is never touched at boot. The
//            existing per-tick yield block re-grabs it under the v6.5.16
//            quiet rule (default moved away AND foreignActiveSessions ==
//            0), so no live stream is ever invalidated again, by boot or
//            re-grab. A boot-yielded leg re-grabs on the WASAPI path (the
//            quiet rule runs before any pin opens; the next restart takes
//            the leg's configured backend -- KS by default since v8.0.0).
//    v7.8.1 THE CAPTION PING. Field: "when I press the theme button, the
//            window's dark mode property doesn't update visually until
//            you change the window state." The attribute WAS landing (a
//            state change proved it) -- what never landed was the paint:
//            Windows 10's DWM re-renders its caption only when the
//            caption's RENDER STATE changes (activation flip, text,
//            geometry); a new DWMWA_USE_IMMERSIVE_DARK_MODE value alone
//            dirties nothing, and the SWP_FRAMECHANGED nudge (no
//            geometry change behind it) is not enough on 10 (it is on
//            11). The state change that always worked -- minimize /
//            restore -- is really the WM_NCACTIVATE(FALSE) /
//            WM_NCACTIVATE(TRUE) pair crossing the caption, so
//            guiApplyDwmMode now pings that pair directly after the
//            attribute set: the opposite-state pass re-renders the
//            caption against the NEW mode, the live-state pass lands
//            back on the true activation; both synchronous inside
//            DefWindowProcW, no focus/z-order touched, the two paints
//            compose as one frame (no flash). Version steps to 7.8.1.
//    v7.8.0 THE ONE-FILE WINDOW HEADER RETIRES, WHOLE. Field: "Remove any
//            references of the one-file window header (leaving me with
//            just the flat-style default windows 10 window) and make sure
//            any dark themes update the dark mode value of the window."
//            The include is gone and the stub file with it; the window is
//            a stock WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN (WS_SYSMENU is
//            BACK -- the native min/max/close, the icon, the title, the
//            Alt+Space menu and Alt+F4 return), sized by AdjustWindowRectEx
//            around the 880x620 client. Retired whole with it: the DWM
//            extend carrier, the WM_NCCALCSIZE insets, the WM_NCHITTEST
//            bands, the WM_NCACTIVATE dance, the band's NC mouse routing,
//            the in-client caption band and its painted glyphs/title/icon
//            and glow state machine (timer 4), the +kCapHDesign layout
//            lead (children sit in plain client space now), the uDWM
//            caption font, and the NC rendering policy pin -- the ONLY DWM
//            touches left are the square-corner preference and the
//            immersive dark-mode dial. THE DIAL (the second field item):
//            guiApplyDwmMode reads the live theme's background HSL
//            lightness (the v7.7.5 law) and programs
//            DWMWA_USE_IMMERSIVE_DARK_MODE (20, fallback 19) on every
//            applyTheme and at creation -- a dark theme flips the native
//            titlebar to dark (white ink), a light theme back to light,
//            and the SWP_FRAMECHANGED nudge lands the flip in the same
//            breath. The DPI child ledger -- the one piece of plumbing
//            that outlived the header -- lives in this file now
//            (tsTrackChild / tsRelayoutChildren). Version steps to 7.8.0.
//    v7.5.3 THE UNIFORM SHEET ROW + THE BAR MOVES INTO THE HEADER. Field,
//            on the active sheet row: "Actually, I think this color on
//            active Gdiplus::Color(128, 43, 43, 43) would do it" -- after
//            "What about RGB(43,43,43)? ... especially since there's a dwm
//            border color gotten from registry that's overriding that
//            color." Adopted verbatim: the ACTIVE row drops the alpha-0
//            glass key it had carried since v7.1.27 and joins the
//            INACTIVE byte -- half-alpha RGB(43,43,43), the recipe's own
//            source gray, the field's byte since v7.4.3. The structural
//            win: with NO alpha-0 row anywhere, the transparent-glass
//            flash the minimize animation rode (the v7.5.1/v7.5.2 saga)
//            is impossible BY CONSTRUCTION -- the press frame, the
//            shrink and the restore all carry the same composited gray,
//            and the v7.5.2 ordered present stays, its row commit now
//            byte-idempotent (the FACE is what it still orders). The
//            v7.4.0 receipt stands behind the shape of the fix: an
//            alpha-0 row cannot carry a color at all (the RGB under
//            alpha 0 is invisible, the DWM's own glass shows
//            regardless), so the recolor had to be alpha > 0 -- and
//            this one is the field's own byte. Second field item:
//            "you didn't put the fake top bar logic inside
//            the one-file window header." Moved whole: the two row constants (now
//            one value), the byte-direct patch (cwPatchSheetRow), the
//            row-first commit (cwCommitSheetRowFast -- self-contained on
//            its own private 1 px DIB, the host's buffer untouched), the
//            NC latch (cwSheetRowLatch), the presenter gate
//            (cwSheetRowGate), the kick (cwSheetRowKick) and the fade
//            presenter thread (cwSheetPresenterStart/Stop) all live in
//            the header now, documented with the engine; twinspeak
//            calls the API. The face's own row-0 GDI+ fill remains as
//            the straight-to-screen fallback only. Version steps to
//            7.5.3.
//    v7.5.2 THE MINIMIZE PRESENT + THE ONE-WRITE BUTTONS. Field, after
//            v7.5.1: "I actually see that fake top border bar turns
//            transparent glass the moment the minimize button is
//            pressed", with the earlier spec: "the frame the minimize
//            button is pressed that the fabricated top title bar is
//            changed to be something like 60,60,60. Right now, there's
//            no dwmflush on the frame the minimize button is pressed."
//            THE WHY: DefWindowProc(SC_MINIMIZE) flips the placement
//            iconic immediately and WM_NCACTIVATE(FALSE)'s present is
//            guarded !IsIconic -- when the flip wins the race, nothing
//            ever presents the inactive style, and the bar rides the
//            whole shrink with its ACTIVE bytes, whose sheet row IS the
//            alpha-0 glass key (the transparent glass over a light
//            backdrop). The press frame now gets the full ordered
//            present INSIDE WM_SYSCOMMAND, before the flip: DwmFlush,
//            the row-first INACTIVE commit, the INACTIVE face, GdiFlush
//            -- the same bytes and order an ordinary unfocus lands. The
//            row is the half-alpha RGB(43,43,43) (the field's gray, the
//            closest real byte to "60,60,60": the proposed alpha-0
//            recolor cannot deliver a color at all -- the RGB under an
//            alpha-0 row is invisible, the DWM's own glass shows
//            regardless; proved in v7.4.0, retired in v7.4.3). The
//            animation rides the inactive bar; the v7.5.1 iconic guards
//            hold every other painter off until restore. Second field
//            item: "the 'start engine', 'apply+restart', 'save ini',
//            '120 bpm tick test', or the theme button flicker on either
//            tab menu select or deactivate." Both events swept every
//            button through a full repaint of TWO screen writes -- the
//            control's erase fill, then the buffered face blit -- and
//            the DWM composed the erase-only frame (bare background: no
//            border, no label, no fill) on each button. WM_CTLCOLORBTN
//            now answers NULL_BRUSH: the erase paints nothing, the
//            buffered face (its own background included) lands as the
//            ONLY write, and the intermediate frame ceases to exist.
//            Version steps to 7.5.2.
//    v7.5.1 THE MINIMIZE FREEZE + THE HONEST BORDER. Field, after v7.5.0:
//            "a split second after pressing the minimize titlebar button,
//            the top bar turns transparent while the rest of the bars stay
//            accurate", then: "pressing the minimize button is when the
//            window border gets brighter, only showing for a split second
//            as the window is getting moved back into its taskbar, so have
//            the top bar color draw its inactive style just once in the
//            case of after pressing the minimize button." The taped
//            artifact is a RACE, not a style: ShowWindow(SW_MINIMIZE)
//            flips the placement iconic while the shrink animation is
//            still sampling the surface, and painters with no iconic
//            guard kept re-landing EITHER face into it -- WM_ACTIVATE's
//            unconditional guiPaintBar (the queue view says whatever the
//            transaction left) and a stale invalidation's WM_PAINT (whose
//            frame blit's row 0 is not sheet bytes) -- each one a compose
//            of the bright inactive row or the transparent active glass
//            key, racing the DWM's own. THE FREEZE: while IsIconic,
//            guiPaintBar draws NOTHING and WM_PAINT's body is skipped
//            (BeginPaint/EndPaint still validate -- no paint storm); the
//            minimize's deactivation lands its ONE draw through the
//            existing WM_NCACTIVATE(FALSE) pre-flip present when it beats
//            the placement flip, and otherwise the bar rides the
//            animation untouched -- exactly one style change, or none.
//            Restore un-freezes byte for byte: the first not-iconic paint
//            path lands the correct face exactly as v7.5.0 did. Second
//            field item: "Fix the mouse registering an unresizable window
//            as it wanting to resize." The one-file window header's WM_NCHITTEST:
//            with the resize gate closed every edge answer
//            (HTLEFT/HTRIGHT/HTTOP/HTBOTTOM and the four corners -- the
//            WS_THICKFRAME style's own advertisement) downgrades to
//            HTBORDER and the manual HTTOP band answers HTBORDER too: the
//            cursor stays an arrow over a fixed window, there is nothing
//            to grab, and the drag that used to end where it started now
//            never starts. The thick frame itself stays -- the DWM shadow
//            lives on it. Third field item: "fix the notification from
//            twinspeak.cpp not working. And also minimize the description
//            of said notification to just 'TwinSpeak is now in system
//            tray.'" The close-to-tray balloon had never once shown: the
//            v7.2.12 dispatch fired the NIF_INFO modify and THEN hid the
//            window inside the same message -- a notification racing its
//            owner window's vanish, which the shell drops -- and the
//            first-three-closes cap ate every later close besides. The
//            confirmation now lands AFTER guiHideToTray returns (icon up,
//            window settled hidden, one NIM_MODIFY against the icon), the
//            cap and the g_closeHints counter are gone, and the text is
//            the field's one-liner: "TwinSpeak is now in system tray."
//            the tray-hide API's own hide stays silent -- the answer belongs to
//            a USER's close, never an API-driven hide. Version steps to
//            7.5.1.
//    v7.5.0 THE ONE-FILE WINDOW. The one-file window header: every piece of Windows
//            window chrome that is not the audio engine moves out of this
//            file into a one-file API -- WM_NCCALCSIZE's frame insets,
//            WM_NCHITTEST, the caption buttons (now a configurable stack:
//            minimize + maximize + close, close always kept), the hover
//            state (GWLP_USERDATA retired -- the header owns it), the
//            caption right-click system menu, WM_SETCURSOR, the button
//            glyph/hover painting, the title text (now the WINDOW TITLE
//            through GetWindowText -- kCapTitle retired), the bar face
//            fill, the window-class icon (new, optional, off by default:
//            the same icon the taskbar shows), the built-in fallback tray
//            icon, and the tray-hide API (true/false) which delegates to
//            twinspeak's own hide-to-tray / guiRestoreWindow paths. The
//            API was an eleven-function chrome surface (enable / recolor /
//            retitle / refont / icon / button layout / resize gate /
//            tray-hide -- all retired v7.8.0). twinspeak stayed consumer
//            #1: applyTheme
//            re-seeds every chrome color from the live theme (byte-identical
//            bytes -- the active/inactive face lift, the dark-theme title
//            gradient, the light-theme solid, the panel-gray hover),
//            guiRenderBar becomes background(row0/edge sandwich), the
//            NC cases leave guiWndProc for the header's proc, WM_CLOSE's body
//            becomes guiHideToTray (shared with the tray-hide API). No new
//            behavior on the field machine -- every pixel and every
//            gesture v7.4.11 produced, now through the API. Version
//            steps to 7.5.0.
//    v7.4.11 THE FORCED REPLAY. Field, after v7.4.10: "Now it shows.
//            Now have this same fix logic be applied to whenever you
//            click on the tray icon and that's it." Plain start births
//            the button -- the tray click does not. The second
//            activation was already armed on every show-from-hidden
//            (v7.4.10: tray click, tray menu, second-instance come-up),
//            but the replay carried one guard the tray path cannot
//            pass: tsReplayActivation ran only while the panel OWNS
//            the foreground. At a plain start the process holds the
//            launch grant, the panel owns the foreground, the guard
//            passes, the replay runs. At a tray click the foreground
//            belongs to the tray (Explorer) -- this process, running
//            hidden since sign-in, holds no grant of its own, and
//            every foreground call behind the restore (the activating
//            show, SetForegroundWindow, even the v7.4.8 dance) can
//            lose that race. 500 ms later the guard finds a
//            non-foreground panel and exits: the replay never runs,
//            the button stays unborn until the user's manual unfocus +
//            refocus -- the exact symptom. THE FIX: tsReplayActivation
//            takes a force flag, set by the show-from-hidden arm in
//            guiRestoreWindow. Forced, a replay that finds the panel
//            without the foreground does not stand down -- it TAKES
//            the foreground with the v7.4.8 attach dance (the user
//            clicked the icon to open this window: taking focus is
//            the request, not a theft), then runs the blip unchanged;
//            a twice-denied foreground stands down quietly. The
//            plain-start arm stays unforced -- v7.4.10 byte for byte.
//            Version steps to 7.4.11.
//    v7.4.10 THE PLAIN PROCESS + THE SECOND ACTIVATION. Field, after
//            v7.4.9: "Even then, it still does not show up on taskbar
//            the moment the app starts without --tray." The window is
//            vanilla since v7.4.9 -- but the PROCESS was not: the
//            v7.1.23 process AppUserModelID stayed, so every panel is
//            still born into the AUMID group's lifecycle instead of the
//            plain window path, and the group's claimant is the
//            windowless sign-in --tray twin joined late by the come-up
//            panel -- the exact setup the labeled taskbar mishandles.
//            (1) THE PLAIN PROCESS: SetCurrentProcessExplicitAppUser
//            ModelID is RETIRED whole -- grouping (one window, nothing
//            to group), the jumplist (exe default), balloon identity
//            and the label (the window title, as v7.4.9 already rides)
//            all run on the default path; the one-letter stub the claim
//            once fought died with v7.1.26's Unicode conversion. Window
//            AND process are vanilla; hiding happens only on --tray (no
//            window is built there at all, v7.1.6). (2) THE SECOND
//            ACTIVATION: the one birth trigger the field machine has
//            never missed is the user's own unfocus + refocus -- a
//            SECOND activation of the VISIBLE panel; no version ever
//            replayed it (the fresh start shows-and-activates in one
//            transaction and the v7.4.8 dance skips itself when the
//            window IS foreground). tsReplayActivation now replays the
//            pair ~500 ms after every first show (plain start AND every
//            show-from-hidden come-up): a 1x1 off-screen toolwindow
//            proxy takes the foreground for 40 ms and hands it straight
//            back -- the exact pair the shell already answers there,
//            minus the mouse. Runs only while the panel still owns the
//            foreground: a user who already clicked away either gave
//            the shell its re-evaluation or is elsewhere -- focus is
//            never stolen. Version steps to 7.4.10.
//    v7.4.9 THE VANILLA WINDOW + THE NAME-CHANGE CHANNEL. Field, after
//            v7.4.8's audit: "I still don't see it. See what hides it from
//            taskbar (initially from --tray) and fix it so it only hides
//            on --tray." The audit's three channels (show, foreground,
//            style) were all exercised and the button still stayed unborn
//            -- so the audit moves to the window itself. The panel was the
//            only window in the app carrying a per-window
//            System.AppUserModel.ID property (SHGetPropertyStoreForWindow,
//            v7.1.25) -- the one thing that made it not-a-normal-window.
//            On Win11's labeled taskbar an AUMID-carrying window's button
//            belongs to the AUMID group's lifecycle, not the plain window
//            path -- and this app runs the exact setup the labeled taskbar
//            mishandles: two processes (the sign-in --tray twin and the
//            plain launch) sharing one AUMID. The property also never
//            fixed what it was added for: its own birth note says the
//            one-letter stub survived v7.1.23's process AUMID AND
//            v7.1.25's property -- the stub died with v7.1.26's Unicode
//            conversion. RETIRED: the panel is fully vanilla again --
//            visible top-level window -> native button on show/activation,
//            the default path every normal app rides; the process AUMID
//            stays for grouping/label/jumplist/balloons. And the birth
//            channel that cannot be filtered: EVENT_OBJECT_NAMECHANGE --
//            tsEnsureTaskbarButton re-announces the window title twice
//            (flip to title+space, back; invisible -- the bar paints its
//            own title) immediately after every first show, plus a nag
//            budget (12 x 250 ms) that keeps re-announcing while visible,
//            covering the sign-in race (a panel shown while the taskbar
//            is still starting misses every birth event; the re-announce
//            is the first channel it processes once it listens). Also in
//            v7.4.9: the double-clicked duplicate transfers its foreground
//            grant to the running instance before forwarding "come up"
//            (AllowSetForegroundWindow -- the documented single-instance
//            pattern), so the background twin's SetForegroundWindow is no
//            longer denied on the come-up path. --tray starts untouched:
//            no window, no button, by design. Version steps to 7.4.9.
//    v7.4.8 THE BUTTON AUDIT. Field, after v7.4.7's activating show: "I
//            still don't see it on the taskbar." The button's birth rides
//            the shell's event channels, and the field machine missed it
//            on both: the show event (v7.1.7's no-activate lag -- v7.4.7
//            fixed the show and the button still stayed unborn) and the
//            foreground event (the launch activation can be suppressed by
//            the foreground lock, so no foreground event ever fires for
//            the window -- the user's own unfocus + refocus cycle is the
//            first real foreground event the shell sees, which is exactly
//            when the button appeared). tsEnsureTaskbarButton now runs
//            after EVERY first show (plain start AND guiRestoreWindow):
//            the thread-input attach dance takes the foreground when the
//            launch activation was suppressed, and WS_EX_APPWINDOW is
//            asserted once -- a button-worthiness STYLE change, a third
//            channel that depends on neither of the others; the taskbar
//            re-evaluates the window and births the NATIVE button (this
//            is a style bit, not ITaskbarList::AddTab -- AddTab is the
//            v7.1.26 one-letter-stub trap).
//    v7.4.7 THE BUTTON RIDE. Field: "The 'TidyVision TwinSpeak' will not
//            show up anywhere on the taskbar until I unfocus and refocus
//            the window" -- a plain launch opened the panel but the
//            taskbar button stayed unborn until an activation change.
//            The first show was SW_SHOWNA (v6.6.5's no-activate,
//            no-flash show) with SetForegroundWindow bolted on behind
//            it (v7.1.20) -- two separate shell transactions. The
//            taskbar button is born in the SHOW transaction, and a
//            no-activate show is the exact case v7.1.7 had to patch
//            with ITaskbarList::AddTab ("a button that lagged behind a
//            no-activate show"; retired v7.1.26 -- Win11 files AddTab
//            items as TABS and renders them as one-letter stubs). The
//            fix is the natural order: ShowWindow(SW_SHOW) -- show AND
//            activate in ONE transaction, the show the button birth
//            actually rides; the pre-render keeps the finished first
//            frame and the foreground calls stay as a belt. Both show
//            paths now activate (guiRestoreWindow always did).
//    v7.4.6 THE LEGACY SHADOW, RETIRED. Field: "When I click on the tray
//            icon to open the app's window again, I see this ghost shadow
//            that extends to the bottom right." The two DWM-side rebuilds
//            (v7.4.4's frame nudge, v7.4.5's sheet re-commit) shrank the
//            ghost but could not kill it -- because it was never DWM's
//            projection. The class carried CS_DROPSHADOW next to the DWM
//            shadow: win32k's legacy shadow is a separate "SysShadow"
//            sprite drawn OFFSET DOWN-RIGHT of the owner window (that is
//            its whole geometry -- exactly the ghost's shape), and its
//            show/hide lifecycle around a hide-to-tray + reopen cycle is
//            where it goes stale. The v7.4.4 nudge recomputes the win32
//            side and re-anchored the sprite (the ghost shrank to
//            right/bottom); the v7.4.5 sheet re-commit lives entirely on
//            the DWM side and cannot touch it. CS_DROPSHADOW is gone from
//            the class style: no SysShadow window can exist for this
//            window, so no ghost can. The Windows-default drop shadow the
//            field asked for in v7.1.23 is untouched -- it comes from
//            WS_THICKFRAME + the default border color + the glass sheet
//            (v7.1.23/v7.1.27) under DWM composition, none of which move;
//            only a composition-off machine loses the legacy soft edge
//            (the field machine runs composition -- the sheet, the border
//            color and the DWM shadow all read live there). Also in
//            v7.4.6: a plain (no --tray) launch adds the tray icon at
//            startup -- until now the icon rode the first close-to-tray
//            (trayAdd lived in WM_CLOSE), so a fresh launch had no tray
//            presence at all; every start path owns the icon from the
//            first moment now (hint=false: no balloon -- the window IS
//            open). Version steps to 7.4.6.
//    v7.4.5 THE SHEET RE-COMMIT. Field, after v7.4.4: "At least only the
//            right and bottom ghost shadow appears now, but I want to get
//            rid of it. I would do option 2." The v7.4.4 frame-change
//            nudge recomputes the win32 side; DWM's own shadow projection
//            can still resurrect stale (the right/bottom ghost). The
//            sheet is the DWM-side half of the frame commitment, so
//            guiRestoreWindow re-commits it after the nudge, RAD-style
//            (the v7.3.1 WM_DWMCOMPOSITIONCHANGED move) -- twice, because
//            a re-extend of unchanged margins can be filtered: first
//            {0,0,0,0} to drop the stale frame visualization, then the
//            real {0,0,1,0} so frame + shadow rebuild against the live
//            rect. Settled state = the exact v7.1.27 sheet; the bar's
//            bytes and both recipe rows are untouched. Version steps to
//            7.4.5.
//    v7.4.4 THE SHADOW REBUILD. Field, on the tray-reopen screenshot: "I
//            forgot to mention this again, but this is what happens when I
//            close the window and then click on the tray icon to reopen said
//            window." SW_HIDE retires the window's DWM visual (shadow goes
//            with it); SW_SHOW resurrects the window from the frame metadata
//            cached at the last frame change WITHOUT recomputing the NC
//            geometry -- the drop shadow is rebuilt at the stale bounds,
//            detached from the real rect. guiRestoreWindow now fires the
//            WM_CREATE nudge (v7.1.24, RAD main.c) once per restore after
//            the work-area clamp: SWP_FRAMECHANGED|NOMOVE|NOSIZE|NOZORDER|
//            NOACTIVATE -- win32k re-runs WM_NCCALCSIZE, DWM rebuilds frame
//            + shadow against the live rect. Geometry only: the glass sheet,
//            the recipe rows and every painted byte are untouched. Version
//            steps to 7.4.4.
//    v7.4.3 THE INACTIVE ROW, THE FIELD'S GRAY. Field: "Now, I just need
//            the top bar's inactive glass key color to be 43, 43, 43 and
//            that's just it." One color moves: kSheetRowInactive
//            0x80000000 -> 0x802B2B2B -- BGRA(43,43,43,0x80), half-alpha
//            RGB(43,43,43), the recipe's own source gray (the vanilla
//            inactive-border byte v7.3.2 already carried, now one pinned
//            constant with nothing measured behind it); the guiRenderBar
//            brush moves with it (Gdiplus::Color(128, 43, 43, 43)).
//            Nothing else: the alpha stays the half, active stays the
//            glass key BGRA(0,0,0,0), the pin stays a pin. Version steps
//            to 7.4.3.
//    v7.4.2 THE INACTIVE ROW, HALF-ALPHA BLACK. Field, after living with
//            v7.4.1: "Now I see something clever. This means the #696969
//            needs to be #000000 for the inactive window top bar." The
//            observation closes the loop: the DWM's own inactive border
//            is not a gray painted ON TOP of the glass -- it is the
//            glass itself DIMMED 50%, which is half-alpha BLACK and
//            nothing else. A gray source byte double-dims (the gray
//            rides 50% on top of an already-dimmed glass). One byte
//            moves: kSheetRowInactive 0x80696969 -> 0x80000000
//            (BGRA(0,0,0,0x80)); the guiRenderBar brush moves with it
//            (Gdiplus::Color(128, 0, 0, 0)). Active stays the glass key
//            BGRA(0,0,0,0) -- the focused window is never touched.
//            Version steps to 7.4.2.
//    v7.4.1 THE INACTIVE ROW, PINNED. Field: "Now the opposite happens on
//            unfocus. Why is this? Btw, the focused window remains
//            accurate. I just want you to revert those changes." Then the
//            spec: "Just have the glass-effect top border do a gray color
//            for inactive and that's it" -- and the recipe: "have the
//            inactive top border be a half-transparent #696969 upon the
//            window being unfocused, since that's what's apparently being
//            done." It is: the DWM's own inactive border IS half-alpha
//            DimGray, and Discord's custom-frame build shows the same
//            unfocus hole -- the inactive sheet material is the DWM's
//            business, never ours to measure. v7.4.0 (the glass key in
//            BOTH states) went BLACK over a (252,252,252) backdrop on
//            unfocus; the v7.3.0/v7.3.2 tracked imitations drifted from
//            the strips. Both are REVERTED/RETIRED and the row is pinned:
//            BGRA(0,0,0,0) active (untouched -- the focused window stays
//            accurate), BGRA(0x69,0x69,0x69,0x80) inactive -- one constant
//            per state in all three writers, zero measurement, zero latch
//            (guiHoldPxForBackdrop and g_holdLive/g_holdPx are deleted).
//            The DWM's own compose blends it exactly like the strips.
//            Version steps to 7.4.1.
//    v7.3.2 THE HOLD THAT DOES NOT EXPIRE. Field: "I just see a
//            transparency issue on the top border itself ... I think I
//            might have experienced that 240ms timer that redrew the
//            window afterward. Find references of it and also fix the
//            issue how I like it. Basically remove the 240ms timer."
//            Found, every reference: the wall-clock stamp (kHoldMs =
//            240, g_holdUntil) written at WM_NCACTIVATE(FALSE), the
//            clock checks in guiBarPatchSheetRow, guiCommitSheetRowFast
//            and guiFadePresenter, and the presenter's HANDOVER present
//            -- the first tick past the stamp re-landed the recipe's
//            half-alpha row and retired the stamp. That handover was
//            the transparency the field caught: 240 ms after every
//            deactivation the solid, backdrop-tracked border row
//            snapped back to the see-through 50/50 glass blend. The
//            timer is gone. The deactivation hold is a LATCH now
//            (g_holdLive): stamped with the measured backdrop color at
//            WM_NCACTIVATE(FALSE), honored by every writer for the
//            WHOLE inactive period -- the top border stays the solid
//            tracked color until the window activates again, and the
//            glass key (the DWM's own border) returns with the
//            activation exactly as before. The half-alpha recipe row
//            remains only as the ride for a failed measurement (window
//            docked at a monitor edge), unchanged. Nothing else moves:
//            the row-first commit, the lParam -1 answer, the presenter
//            stand-down, DwmFlush -- all v7.3.1. Version steps to 7.3.2.
//    v7.3.1 THE ROW-FIRST COMMIT, THE RAD CONTRACT. Field: "I still
//            sometimes get the one-frame delay of the window border ... I
//            want the accurate window borders from window activation to
//            render every time here. I'm not joking." The residual frame
//            was the one window v7.3.0's DwmFlush() cannot close: the
//            flush retires the compose IN FLIGHT, but the NEXT compose --
//            the one that carries the flip -- starts on the DWM's own
//            cadence, and v7.3.0's commit was the FULL bar paint, a multi-
//            millisecond GDI+ pass (gradients, glyphs, one blit) that
//            lands its row bytes LAST. A flip compose starting inside
//            that window sampled the PRE-flip row -- the active glass key
//            on deactivation, the held opaque row on activation -- and
//            the border read one frame late, "sometimes", wherever the
//            click landed in the compose cadence. Two adoptions from the
//            RAD debugger's win32_window_manager.c (the same main.c
//            geometry, field-proven) close it. (1) THE ROW-FIRST COMMIT:
//            after the latch, the flush and the hold stamp, row 0 reaches
//            the redirection surface ALONE -- one uniform-constant row
//            write into the bar DIB, one 1 px blit, GdiFlush(),
//            microseconds (guiCommitSheetRowFast) -- and the face follows
//            in the same handler with the full paint; the race window
//            collapses from milliseconds to the blit itself. (2) THE
//            lParam -1 ANSWER: WM_NCACTIVATE now returns
//            DefWindowProcW(w, msg, wp, -1) -- the documented convention
//            ("does not repaint the nonclient area to reflect the state
//            change"; RAD: "without this result, stuff flickers") -- so
//            the legacy NC repaint machinery (the themed-frame blit, the
//            mid-transaction WM_NCPAINT it schedules) never runs for the
//            flip; the DWM's own left/right/bottom strips are composited
//            by the DWM regardless, the flip still completes, and the
//            v7.2.8/v7.2.9 WM_NCPAINT dance stays for the message's OTHER
//            sources. Plus two guards the race analysis exposed: the fade
//            presenter thread now STANDS DOWN while the GUI commit is in
//            flight (g_guiRowCommit) -- a tick could read the NEW latch
//            with the OLD hold state between the two writes and re-land
//            the recipe row over the flip -- and WM_DWMCOMPOSITIONCHANGED
//            re-commits the sheet + dark mode + a bar repaint, so a
//            composition restart (driver reset, RDP) can no longer leave
//            row 0 riding a dead extend (RAD re-extends for the same
//            reason). Version steps to 7.3.1.
//    v7.3.0 THE CHROME CONTRACT, TRACKED. Field: "sometimes for a split
//            second the window borders stop being accurate until the next
//            frame. Really, the {0,0,1,0} should be drawn constantly.
//            Google chrome doesn't have this issue." Two truths became
//            code. (1) The sheet row is never "drawn constantly": the
//            {0,0,1,0} extend is a ONE-TIME GEOMETRY COMMITMENT, and the
//            row's final look is a DWM-side blend of OUR last-written
//            bytes with the sheet's ANIMATING activation visuals, re-baked
//            only when the window presents or the fade ticks -- between
//            re-bakes the row is the LAST BAKE. Two windows of
//            inaccuracy remained in v7.2.15: a compose already in flight
//            at the click can carry the flip off a surface snapshot taken
//            before our bytes existed (the "sometimes, until the next
//            frame" frame), and the ~200 ms fade itself blends the
//            half-alpha row against animating visuals (the "split
//            second"). The first is closed by REINSTATING v7.2.2's
//            DwmFlush() inside WM_NCACTIVATE -- it retired with the mouse
//            hook in v7.2.14 -- before the pre-flip present, both
//            directions. The second is closed by adopting the CHROME
//            CONTRACT for the deactivation: Chromium repaints its frame
//            synchronously inside WM_NCACTIVATE, returns TRUE without
//            DefWindowProc, and delivers its inactive border as a SOLID
//            theme color in its own atomically-presented surface -- no
//            alpha-carrying row, no glass blend to re-bake, nothing to
//            fade or sample stale. So on WM_NCACTIVATE(FALSE) row 0 is
//            now OPAQUE at the settled recipe composite over the CURRENT
//            backdrop, computed live (guiHoldPxForBackdrop: one BitBlt of
//            the desktop strip above the window's top edge into a
//            persistent DIB, sparse scan of the rows farthest from our
//            own drop shadow, median by luma -- v7.2.13's estimator
//            verbatim, whose look the field approved; the MACHINE around
//            it is what v7.2.14 retired, and none of it returns: no
//            measured cache, no seeder, no mouse hook, no tail timer).
//            The hold is a wall-clock stamp (240 ms, ~the DWM's 200 ms
//            border fade + one compose): guiBarPatchSheetRow honors it on
//            EVERY painter, the fade presenter's own train hands it back
//            (the first tick past the stamp presents the recipe's
//            half-alpha row and retires the stamp -- no tail timer, the
//            train IS the handover), and the clock check makes it
//            self-expiring, so a hold can never outlive the fade window.
//            Over an unchanged backdrop the handover is invisible (the
//            held color IS the recipe's settled composite there); over a
//            changed one it is a one-snap re-convergence. Measurement
//            failure (window top at a monitor edge) rides the vanilla
//            recipe row exactly as v7.2.14 did. Version steps to 7.3.0.
//    v7.2.15 THE QUEUE-PROOF FADE, THE QUIET BUTTONS. Field: "now I
//            sometimes get a deactivate redraw delay" + "some buttons
//            still flicker". The delay WAS the fade-tracking burst's
//            ride on WM_TIMER: the lowest-priority message in the
//            queue, synthesized only when the queue runs empty -- and
//            while the engine runs the queue is regularly fed (the
//            250 ms log drains, the status band, the pane repaints),
//            so burst ticks landed late or in clumps, and each missed
//            tick left the DWM's presentation-time sheet blend frozen
//            at the previous compose (v7.1.30). The burst is now a
//            dedicated PRESENTER THREAD kicked by an event at every
//            activation change: it owns a private 1 px row DIB (the
//            row is one uniform constant in both states), re-presents
//            it twenty times ~20 ms apart with GetDC/BitBlt/GdiFlush
//            from OUTSIDE the message queue, and a kick landing
//            mid-run restarts the train (the last state wins). Timer 4
//            and g_bakeTicks are retired; g_ncActive is atomic (the
//            thread reads it). The flicker WAS the erase mismatch: the
//            BUTTON control erases its rect with the WM_CTLCOLORBTN
//            brush BEFORE sending WM_DRAWITEM, and the answer was
//            always g_brBg while the drawitem erase paints g_brPanel
//            under every on-page face -- so toggles, pills and slot
//            selectors flashed the app background between their own
//            pixels on every press/toggle/preset repaint. WM_CTLCOLORBTN
//            now answers the same per-button brush WM_DRAWITEM erases
//            with (g_brPanel on the panels, g_brBg on the bottom bar):
//            the erase pass is pixel-identical to the face's own
//            background and the intermediate frame ceases to exist.
//    v7.2.14 THE VANILLA DEACTIVATION -- the opaque instant-row machine is
//            RETIRED. Field: "I still see the top border as opaque. And I
//            still see a 300ms delay for after deactivation when the
//            window's borders would be redrawn." Both symptoms ARE the
//            machine: the row is opaque BY DESIGN (an opaque pixel on the
//            glass sheet shows as-is, covering the DWM's border fade) and
//            the ~300 ms IS the hold window (the 400 ms tail tick that
//            handed the row back to the recipe bytes). A constant laid
//            OVER the fade can never be the vanilla look -- wrong over an
//            unmeasured backdrop, or frozen past the fade. So the machine
//            goes: the early-present WH_MOUSE_LL hook, the tracked hold +
//            its backdrop strip read, the measured cache + its screen
//            readbacks, the seeder (timer 5) and the 400 ms tail. What
//            remains is the vanilla path: the v7.2.3 NC-flip present (the
//            whole bar -- inactive face + the recipe's half-alpha row --
//            painted and GdiFlush()ed INSIDE WM_NCACTIVATE, before
//            DefWindowProc flips the frame), the v7.2.9 symmetric
//            activation present, the v7.2.8 forced synchronous frame
//            paint at WM_ACTIVATE(WA_INACTIVE), and the v7.1.31
//            fade-tracking burst -- now a cheap 1 px strip re-present
//            (twenty 20 ms ticks, no GDI+ pass) so the presentation-time
//            sheet blend cannot freeze mid-fade. The deactivated top
//            border is the recipe's half-alpha gray composited by the
//            DWM's glass, fading natively: the exact transition every
//            vanilla Windows 10 window shows.
//    v7.2.13 THE HOLD TRACKS THE BACKDROP. Field: "Thanks, but I still
//            get a white top border bar." The screenshot settles it: the
//            window INACTIVE over a dark video (and half over a white
//            explorer) shows the top border row as ONE uniform opaque
//            RGB(170,170,170) line across BOTH backdrops -- while the
//            DWM's own left strip blends dark over the same video. 170
//            is exactly the settled composite over PURE white
//            (half of gray85 + half of 255): the row was replaying the
//            MEASURED CACHE -- the instant row's constant, captured at
//            the tail of an earlier fade over a white backdrop. The
//            instant row is opaque BY DESIGN (opaque never fades), but a
//            constant is only right over the backdrop it was measured
//            on: on the white test surface the stale replay blended in
//            invisibly, over the dark video the same replay IS the white
//            bar -- on the hook's first frame (the early present) and
//            for the whole 400 ms hold after every unfocus. THE FIX: the
//            hold color is no longer a memory, it is COMPUTED at the
//            moment of deactivation from the backdrop actually behind
//            the row (guiInstantRowForBackdrop): the settled recipe
//            composite is (43 + 127*backdrop/255) per channel, and the
//            backdrop is the visible desktop strip directly above the
//            window's top edge -- one BitBlt into a persistent DIB, a
//            sparse scan of the rows farthest from our own drop shadow,
//            median by luma (the measure's own statistic). The held row
//            then equals what the settled blend WOULD show over the
//            current backdrop, so it never reads white over dark
//            content and the handover to the recipe bytes at the tail
//            is invisible. The measured cache (g_instantInactive)
//            demotes to the fallback for a failed strip read; the hook's
//            early present computes the color live too (its stale-cache
//            front row was the first white frame of every click-away),
//            so the instant row works from the FIRST deactivation of a
//            session (no cache needed), and the 20 x 20 ms no-cache
//            fade-tracking train retires -- the hold engages on every
//            deactivation now, the train survives only when no color is
//            readable at all. Belt: the hold carries a wall-clock stamp
//            and any paint 600 ms past the latch releases it (a hold can
//            never outlive the fade window again, whatever a timer
//            path does), and the measure tick clears a lingering hold
//            before reading the screen (the cache must measure the
//            settled recipe row, not our own replay). Version steps to
//            7.2.13.
//    v7.2.12 THE HOOK THREAD GOES QUIET + THE TRAY AUDIT. Field: "while the
//            window runs it makes my mouse lag -- look for any symptoms".
//            The GUI thread answers the system-wide WH_MOUSE_LL hook (the
//            zero-frame row delivery), so every mouse event on the desktop
//            queues behind whatever that thread is doing, and four things
//            kept it busy: (1) the settle burst re-rendered the whole bar
//            twenty times at 50 Hz after EVERY activation change even when
//            the measured cache made all twenty renders identical pixels --
//            with a cache the hold now spans the fade with ZERO re-renders
//            and one tail tick at 400 ms (the 20 x 20 ms train survives only
//            for the no-cache first-run fade tracking); (2) the measure-retry
//            tail re-rendered the bar on every 250 ms retry tick forever --
//            the retry is a bare re-measure now; (3) guiMeasureInstantRow
//            did up to 36 synchronous GetPixel screen readbacks per scan --
//            ONE BitBlt into a persistent 1-row DIB + an in-memory scan
//            replaces them, same pixels, same guards; (4) g_lastHealth was
//            a plain std::string written by the engine thread and read by
//            the GUI thread with no lock -- it rides g_logCS now. TRAY:
//            the close-hint balloon was dead code (trayAdd early-returns
//            while the icon is already in the tray -- it shows on WM_CLOSE
//            now, still capped at three per session); a delayed sign-in add
//            left the generic tooltip while the engine ran (the tip re-syncs
//            the moment the icon lands); the tray icon loaded the 32 px
//            shell icon and the tray down-scaled it (small icon size now);
//            explorer restarts deleted + re-added the icon TWICE (both
//            windows answered TaskbarCreated -- the owner window answers
//            once now, and re-adds keep the original owner); a failed add
//            could orphan the 2 s retry timer on the surrogate; a tray-menu
//            Start with unset devices died silently (balloon now). Version
//            steps to 7.2.12.
//    v7.2.11 THE LATCH ANSWERS FOR EVERY PATH. The field's second tape
//            (60 fps, the top border parked on the white titlebar of OBS):
//            on EVERY unfocus the deactivation plays one correct inactive
//            frame, then TWO frames where the caption flips back to BLACK
//            and the top border VANISHES against the white surface, then
//            the settled inactive face -- the exact "redrawn as the input
//            has never been gotten yet until the second frame" report, on
//            the version that already carries the ever-seeded cache. The
//            tape kills the no-cache theory for this artifact: the flash
//            is not the row riding the glass fade, it is a STALE FACE --
//            a painter running between WM_NCACTIVATE(FALSE) and
//            WM_ACTIVATE (the queue's view of activation has not caught
//            up; GetActiveWindow still returns us) that read the plain
//            queue view in guiPaintBar and re-landed the ACTIVE bar: the
//            black caption AND the ACTIVE row bytes, alpha 0 = bare glass
//            = the bright row over the light backdrop. v7.2.9 taught
//            WM_NCPAINT to follow the NC latch (g_ncActive, the frame's
//            own activation state, latched inside the transaction); this
//            version HOISTS that latch into guiPaintBar itself -- the one
//            function every painter passes through (WM_PAINT, the bar
//            hover paths WM_NCMOUSEMOVE/WM_MOUSEMOVE, the settle-burst
//            timer ticks, applyTheme's synchronous repaint): when the
//            latch and the queue view disagree the transaction is still
//            open and the LATCHED state paints, so no path can re-land a
//            stale face in the transaction window anymore; after
//            WM_ACTIVATE the views agree and every path reads the plain
//            truth exactly as before. The force flags (v7.2.3 deactivation
//            pre-flip present, v7.2.9 symmetric activation present and
//            WM_NCPAINT mid-transaction branch) keep winning unchanged.
//            Version steps to 7.2.11.
//    v7.2.10 THE EVER-SEEDED CACHE. The field: "My only issue is that
//            the window titlebar's top border is flashing whenever I
//            deactivate the window... the window is redrawn as the
//            input has never been gotten yet until the second frame."
//            That is the NO-CACHE deactivation, still, on a machine
//            where v7.2.9's median scan never even gets to run: every
//            instant machine on the row (the v7.2.1 hook early
//            present, the v7.2.0 opaque hold at WM_NCACTIVATE and
//            WM_ACTIVATE) gates on g_instantInactive, and the cache's
//            ONLY writer was the burst-tail measure -- twenty 20 ms
//            ticks that must run UNINTERRUPTED while the window STAYS
//            inactive, plus one more tick. The field's own test loop
//            defeats it: click away, click back inside half a second
//            -- WM_ACTIVATE resets g_measurePending, the burst
//            re-arms, the measure tick lands while ACTIVE
//            (guiMeasureInstantRow bails), the retry wants the window
//            to STILL be inactive, the timer dies -- and the cache
//            stays empty for the whole session. Every deactivation
//            then rides the DWM's transition: row 0 is the recipe's
//            HALF-ALPHA bytes, alpha-carrying pixels on the extended
//            sheet are owned by the glass blend while the activation
//            transition runs, and the composes that carry the flip
//            show the PRE-INPUT (active) blend until the settled gray
//            lands -- the flash, every time. The fix is a SEEDER of
//            its own: timer 5 at 250 ms, armed the moment the sheet
//            extends, ALIVE while g_instantInactive is empty and
//            owned by NO other path -- WM_ACTIVATE cannot reset it,
//            the burst cannot kill it. Each tick: hidden/iconic =
//            nothing; within 450 ms of the last activation change =
//            nothing (the fade owns the row there; a mid-fade sample
//            would cache the bright glass); otherwise
//            guiMeasureInstantRow runs -- any quiet inactive stretch
//            (the watched deactivation itself, the gap of a quick
//            toggle, the first idle seconds after launch) seeds the
//            cache, and the first success kills the timer FOREVER
//            (the burst tail owns the refresh from there; the cache
//            is never cleared, so the seeder's job is once per
//            session). From the first seeded moment every
//            deactivation is the v7.2.3 ordering with the v7.2.0
//            opaque row: the flip compose reads OUR bytes -- no
//            glass, no fade, no second frame. The one deactivation
//            the seeder cannot help is the first-ever one (nothing
//            settled has ever existed to measure). Version steps to
//            7.2.10.
//    v7.2.9 THE ONE-FRAME ROW, TRACED AND KILLED. The field: "I still
//            see the top bar thing being delayed by one frame. Trace
//            the exact sequence for deactivation and fix it once you
//            identify the root cause." Traced, on the field's own 60 fps
//            tape and in this code: the deactivation sequence is (1)
//            the click, (2) the WH_MOUSE_LL early present -- SKIPPED,
//            it requires a measured cache; (3) WM_NCACTIVATE(FALSE)
//            paints the whole bar inactive inside the transaction: the
//            FACE is opaque client pixels and displays AS-IS at the
//            flip compose (the proven titlebar fix), but row 0 without
//            a cache is the recipe's HALF-ALPHA BGRA(43,43,43,128) --
//            and alpha-carrying pixels on the extended glass sheet are
//            owned by the DWM's glass blend while the activation
//            transition runs, so the row cannot show its settled
//            composite at the flip compose; (4) the flip compose flips
//            the DWM's own strips and our face; (5) WM_ACTIVATE -- one
//            queue hop later -- re-presents the row, which lands the
//            NEXT compose: ONE FRAME LATE, every time. The root cause
//            is upstream of all of it: guiMeasureInstantRow could never
//            seed the cache on this desktop -- it required ALL THREE
//            fixed points (left-40 / center / right-40) uncovered AND
//            agreeing within +/-2, and the field's tape shows twinspeak
//            sitting PARTIALLY BEHIND covering windows (only a sliver
//            of the top row escapes), so the guards fail, the cache
//            stays empty, and the hook and the opaque hold -- the two
//            machines that make the row instant -- never engage. Three
//            fixes: (a) THE MEASURE SCANS the row every 24 px, keeps
//            every guarded point, takes the MEDIAN -- any single
//            visible point seeds the cache, partial cover no longer
//            starves it (the +/-2 gate is retired with it: the blend
//            legitimately varies along the row with the backdrop);
//            (b) WM_NCPAINT now paints the NC-LATCHED state (latched
//            at WM_NCACTIVATE) whenever the queue has not caught up --
//            a WM_NCPAINT that arrives INSIDE the transaction can no
//            longer repaint the STALE face and revert the pre-flip
//            present; (c) THE SYMMETRIC ACTIVATION PRESENT:
//            WM_NCACTIVATE(TRUE) paints the ACTIVE bar (row 0 black =
//            the glass key) before DefWindowProc flips, so activation
//            gets the same zero-frame ordering deactivation has had
//            since v7.2.3. With the cache seeded the deactivation row
//            is the OPAQUE measured composite from the flip compose
//            on -- no glass, no fade, no frame late; the burst tail
//            still hands it back to the recipe's real translucent
//            border once the fade has settled. Version steps to 7.2.9.
//    v7.2.8 BACK TO THE 7.2.3 BASE + THE POST-FLIP NCPAINT. The field:
//            "Go back to here, because that was when I had almost
//            had it. Make v7.2.8 with it. And lastly, make it so the
//            ncpaint happens immediately after you deactivate the
//            window." The v7.2.5..v7.2.7 strip experiments are
//            RETIRED whole (the side-strip samplers, the staged
//            sheet, the computed covers): GetPixel reads the GDI
//            surface, the DWM's border lives only in the compositor,
//            and no owned opaque color imitates a blend -- every one
//            of those covers was a guess wearing the border's
//            clothes. The base is v7.2.3 VERBATIM: the NC-flip
//            present at WM_NCACTIVATE(FALSE) (the proven pre-flip
//            present, two tapes clean), the opaque hold ONLY with a
//            measured cache, and the settled row = the recipe's
//            half-alpha BGRA(43,43,43,128) -- the REAL translucent
//            border the DWM composites over the sheet, the same
//            material the left/right/bottom strips carry: "almost
//            had it." What the 7.2.3 tape still caught was the row
//            BETWEEN states in the first post-flip composes, and the
//            fix is the field's own last ask, verbatim: WM_NCPAINT is
//            now HANDLED -- the message win32k sends when it redraws
//            the frame right after the activation change -- and the
//            bar (face + row 0) is repainted INSIDE that pass, with
//            the frame's own redraw, not a queue hop behind it. The
//            plain truth paints itself there: post-deactivation the
//            transaction has closed, GetActiveWindow() has moved on,
//            and guiPaintBar's plain read picks the inactive face;
//            post-activation it picks the active one (row 0 black =
//            the glass = the DWM's own frame). Belt: WA_INACTIVE (the
//            first message AFTER the transaction) also FORCES the
//            frame paint synchronously -- RedrawWindow RDW_FRAME |
//            RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE dispatches
//            WM_NCPAINT inside the WM_ACTIVATE call stack, because
//            the DWM can draw the flip without ever asking us to
//            repaint -- so the NC paint happens IMMEDIATELY after
//            the deactivation, by construction, on every path a
//            window can see. Idempotent with the burst and every
//            other presenting path (same buffered bytes, GdiFlush()
//            at the end of guiPaintBar); DefWindowProc still draws
//            the real frame from the common tail. Version steps to
//            7.2.8.
//    v7.2.3 THE NC-FLIP PRESENT. The field: "the top bar still doesn't
//            want to cooperate. On google chrome, the top bar going to
//            the inactive state is instant. Look at the frames from the
//            video and you'll see what I mean." The frames, decoded at
//            60 fps: deactivating, the border row sits on its active
//            composite for one frame, then goes BARE GLASS -- brighter
//            than either endpoint -- for two to three frames, and only
//            then lands on the settled inactive gray; the bar face (the
//            title text) flips on that same last frame. Activating is
//            already instant. The reason: the DWM re-bakes the extended
//            sheet row at the compose that carries the activation flip,
//            and that compose happens INSIDE the deactivation
//            transaction -- WM_NCACTIVATE is sent within it, WM_ACTIVATE
//            is only POSTED after it -- so the flip compose always reads
//            the row the ACTIVE state last wrote: alpha 0, bare glass
//            (the bright flash), and the inactive bytes every later path
//            writes (the v7.2.2 flushed hook present, WM_ACTIVATE's blit,
//            the burst) land one to three composes late. The fix rides
//            the one message that arrives in time: WM_NCACTIVATE(FALSE).
//            BEFORE DefWindowProc flips the frame state, the handler
//            paints the WHOLE bar inactive -- face and row 0 -- into the
//            buffer and GdiFlush()es it into the redirection surface;
//            win32k cannot resume the transaction until the WndProc
//            returns, so the flip compose reads inactive bytes BY
//            ORDERING: no glass frame, no stale face, one composed frame
//            -- Chrome-instant -- with or without the measured cache
//            (with it, row 0 is the opaque settled color; without it, the
//            recipe bytes ride the fade exactly like a native frame's
//            own strips do). The v7.2.1 mouse hook stays as the belt for
//            the click paths; WM_ACTIVATE still arms the burst (its
//            blit now repaints an already-inactive face -- a visual
//            no-op that keeps the fade tracking). And the cache seed
//            learns to RETRY: the burst-tail measurement bails on
//            transient guards (a covered row, a compose in flight), and
//            one bail used to leave the cache empty until the next full
//            burst; while the window sits inactive with no cache the
//            measure now re-arms at 250 ms until it succeeds, so the
//            opaque instant row engages from the first steady
//            deactivation. Version steps to 7.2.3.
//    v7.2.2 THE FLUSHED EARLY PRESENT. The field: "I still see that when
//            the window goes unfocused that it still shows the stale
//            version which immediately goes to the official unfocused
//            window frame exactly one refresh frame later." The v7.2.1
//            hook fired and patched the row, but its bytes were not
//            first to the surface: gdi32 BATCHES drawing calls per
//            thread, so the hook's unflushed BitBlt sat in user memory
//            while win32k resumed the activation transaction, the DWM
//            composed the flip against the OLD row, and the batch landed
//            one composed frame later -- deterministically. And a
//            composition already in flight when the click landed sampled
//            the old row before the click existed, yet can still carry
//            the flip. The hook now orders three steps before it
//            returns: DwmFlush() waits out the in-flight compose (after
//            it, every compose that can carry the flip starts after our
//            write and must sample it), the present shrinks to its actual
//            work -- patch row 0 of the existing bar buffer and blit the
//            1 px strip (the face stays active until WM_ACTIVATE anyway;
//            v7.2.1's full GDI+ re-render bought ~2 ms of unguarded
//            time) -- and GdiFlush() puts the bytes in the win32k
//            redirection surface BEFORE the hook returns; win32k cannot
//            resume the transaction until then, so the flip compose
//            reads the row by ordering, not by luck. guiPaintBar ends
//            with GdiFlush() too, so the WM_ACTIVATE blit and the burst
//            ticks stop waiting for an arbitrary batch flush. Version
//            steps to 7.2.2.
//    v7.2.1 THE ZERO-FRAME ROW + THE NAME SPLIT + A CLEAN KILL. The
//            field: "I still see the inactive window painted one frame
//            late." It does: WM_ACTIVATE hops the message queue AFTER
//            win32k has already flipped the DWM's own border strips, so
//            the first presentation that could carry the row always
//            misses the activation frame by one hop. v7.2.1 buys the
//            hop back with a low-level mouse hook on the (idle,
//            pumping) GUI thread: a button-down OUTSIDE our window
//            arrives BEFORE the activation transaction starts, and the
//            hook presents the measured inactive row right there -- the
//            border is on screen in the same composed frame as the
//            DWM's own flip (zero-frame). A click that never deactivates
//            us (menus and other non-activating surfaces) self-heals
//            within one 20 ms burst tick; Alt-Tab and programmatic
//            switches keep the queue path (one frame -- there is no
//            earlier signal a window can see). Naming: taskbar,
//            Alt-Tab, the duplicate-instance box, the tray icon and the
//            bar's brand text become "TidyVision TwinSpeak"; the hidden
//            tray surrogate window and the PE FileDescription keep
//            "TwinSpeak Control Panel" (Task Manager's window/process
//            entries). Tray ghosts: a force-killed twin (taskkill /F)
//            never runs WM_DESTROY, so its icon sits in the tray until
//            hovered -- every extra launch then reads as "another
//            icon". New `twinspeak.exe --exit` asks the running
//            instance (WM_APP+6, UIPI-allowed) to stop cleanly and
//            remove its icon; the duplicate box now recommends it
//            instead of taskkill. Version steps to 7.2.1.
//    v7.2.0 THE INSTANT INACTIVE BORDER + THE NEW NAME. The field: "for
//            the real challenge, make it so the inactive-window borders
//            show instantly", the app title becomes "Twinspeak Control
//            Panel", version steps to 7.2.0. The burst (v7.1.31) still
//            converges through the DWM's ~200 ms glass fade -- the border
//            arrives WITH the fade, not before it. The way out: an OPAQUE
//            non-black pixel on the sheet displays AS-IS (no glass in the
//            composite, nothing to fade), and the right color is knowable
//            -- it is what the settled recipe composite ACTUALLY
//            produced. So v7.2.0 measures it and replays it: at the end
//            of every burst (settled, inactive, visible, uncovered --
//            WindowFromPoint guards every sample) three GetPixel points
//            along the window's top row are read off the screen DC;
//            agreeing within +/-2 per channel they become
//            g_instantInactive. The NEXT deactivation holds row 0 opaque
//            at that color through the whole fade window -- the border
//            simply is there on the first frame -- then hands it back to
//            the recipe's half-alpha form once the fade has settled (an
//            invisible swap over an unchanged backdrop; a one-snap
//            re-convergence over a changed one) and re-measures one tick
//            later. The first deactivation ever has no cache: v7.1.31's
//            tracked fade runs once and seeds it. Titles: window, bar,
//            tray tooltip/balloons and the PE FileDescription/ProductName
//            now say "Twinspeak Control Panel" (AppUserModelID, company,
//            copyright and exe name untouched).
//    v7.1.31 THE SETTLE BURST. The field: "make it so that when the
//            window goes unfocused that it immediately updates the
//            window." v7.1.30's single 400 ms bake worked but read as a
//            visible delay: the row sat on its mid-fade composite for
//            two fifths of a second, then snapped. Fix: WM_ACTIVATE now
//            arms a BURST -- timer 4 at 20 ms, twenty re-presents
//            (~400 ms) -- so the sheet row is presented continuously
//            across the DWM's ~200 ms activation fade and tracks it
//            frame by frame, reading inactive the moment the fade lands
//            (the same schedule the DWM's own NC strips follow; nothing
//            snaps). The counter re-arms with the timer on rapid focus
//            toggles, last state wins.
//    v7.1.30 THE SETTLE BAKE. The field (v7.1.29, bytes verified right):
//            "Maybe try RGB(0, 0, 0) regardless of active? Btw, when I
//            hover the close button, the inactive border color's top
//            piece actually cooperates. Why is this?" Answer: the row
//            bytes are IDENTICAL with or without the hover -- same bytes,
//            wrong color at deactivation, right color seconds later, so
//            the TIMING is the bug. The DWM evaluates the extended-sheet
//            blend WHEN THE WINDOW PRESENTS NEW PIXELS for the row, not
//            continuously while its activation state settles. WM_ACTIVATE
//            arrives BEFORE the DWM's ~200 ms activation fade has run, so
//            the deactivation blit baked the 50/50 blend against the
//            STILL-ACTIVE sheet and the wrong composite stuck (nothing
//            re-presented the row). The close hover re-presents the bar
//            seconds later -- DWM fully inactive by then -- and the same
//            bytes bake correctly. Fix: WM_ACTIVATE schedules a one-shot
//            400 ms re-blit (timer 4) that presents the row against the
//            SETTLED state (both directions; focus gain has the same race
//            in reverse). Opaque black regardless of active is NOT
//            adopted: black serves the ACTIVE sheet only (black = show
//            the glass = the accent frame); inactive, the bare sheet has
//            no border color at all (v7.1.29's screenshot measured
//            #FFFFFF on it), so the semi-transparent premultiplied gray
//            IS the delivery.
//    v7.1.29 THE ALPHA NEVER LEFT THE BUILDING. Field screenshot, window
//            unfocused: the top row read #FFFFFF (the bare light-mode
//            sheet) while the DWM's own left/right edges read #AAAAAA.
//            Root cause: the bar buffer was a CreateCompatibleBitmap DDB
//            -- DDBs cannot hold an alpha channel, so v7.1.28's half-
//            alpha row reached the redirection surface as alpha 0 =
//            fully transparent = the bare sheet. Fix: the buffer is a
//            32bpp top-down DIB section now, and after the GDI+ pass the
//            sheet row is written BYTE-DIRECT in the recipe's premulti-
//            plied form -- active BGRA(0,0,0,0), inactive BGRA(43,43,43,
//            128) (= gray85 at 50%: "multiply it by the alpha", 85*0.5 =
//            43). Over the field's white desktop the row now composites
//            to ~#AAAAAA, the same as the DWM's own edges.
//    v7.1.28 THE INACTIVE TOP BORDER. The field: "when the window turns
//            unfocused, I still see the top border not proper like the
//            left, right and bottom." v7.1.27 composited the inactive row
//            as an OPAQUE lerp toward RGB(85,85,85) -- a fixed color that
//            can never track what is behind the window, unlike the DWM's
//            own semi-transparent borders on the three NC strips. The
//            recipe is explicit: inactive, you draw a gray SEMITRANSPAR-
//            ENT border -- gray = roundf(85*0.5) = 43, alpha = 0.5, i.e.
//            RGBA(43,43,43,128). The pixel's ALPHA is the delivery: the
//            DWM blends the extended sheet row with the client pixel by
//            that alpha, so the row composites 50/50 with the sheet --
//            the same semi-transparent inactive border the strips carry.
//            To let GDI+ actually write that alpha (src-over onto opaque
//            pixels would flatten it back to 255) the bar DIB is now
//            cleared transparent every frame before the face repaints it
//            opaque. Also: the in-app version string still said v7.1.25
//            (missed two bumps) -- fixed.
//    v7.1.27 THE REAL TOP BORDER, VANILLA AT LAST. From the field: with
//            Windows' "show accent color on the following surfaces: title
//            bars and window borders" the LEFT/RIGHT/BOTTOM border pieces
//            follow the Windows colors -- they are real NC strips from the
//            WM_NCCALCSIZE insets, so the DWM draws its real border there
//            -- while the TOP row stayed our paint (the fake shadow's
//            opaque gray: never half-transparent, never accent). The top
//            cannot be inset like the sides (any top gap makes the DWM
//            paint a ghost caption OUTSIDE the window rect), so the recipe
//            the native surfaces use is adopted: DwmExtendFrameIntoClient-
//            Area {0,0,1,0} extends the DWM glass sheet 1px into the
//            client's top row, and while ACTIVE the bar paints that row
//            PURE BLACK -- black on the sheet IS the DWM frame, so row 0
//            shows the window's REAL top border: the accent color when the
//            Windows override is on, the default SEMI-TRANSPARENT gray
//            when it is not -- vanilla on all four sides. DWMWA_USE_
//            IMMERSIVE_DARK_MODE rides along, always on (attribute 20,
//            fallback 19): without it the DWM paints that sheet WHITE in
//            light mode. While INACTIVE the DWM draws no border on the
//            sheet, so the row is composited by hand: the face top blended
//            50% toward a neutral RGB(85,85,85). The fake-shadow recipe is
//            retired, kept only as the fallback if the extend call fails.
//    v7.1.26 THE GRADIENT LEAVES ROW 0 + THE WINDOW IS UNICODE, END TO END.
//            (a) THE TOP EDGE, from the field: the top thin border still
//            shows "color" instead of the half-transparent gray. The bar
//            face's LinearGradientBrush STARTED at row 0, so its tinted
//            top stop colored the shadow line's row on every dark theme.
//            The gradient fill is offset 1px DOWN and runs 1px SHORTER
//            (the brush rides along) -- row 0 belongs to the fake top
//            shadow alone -- and the shadow line goes back to the
//            example's own recipe VERBATIM: a NEUTRAL RGB(100,100,100)
//            (a soft shadow on light faces, a quiet gray edge on dark
//            ones, never a theme tint), blended 50/50 toward the face top
//            while inactive. (b) THE "T" LABEL: the field still saw the
//            one-letter taskbar stub AND Task Manager listing the window
//            as "T". Two roots, both ours. FIRST, the window was an A/W
//            hybrid: the class registered RegisterClassExW, the window
//            created CreateWindowExA, and guiWndProc's fall-through ran
//            DefWindowProcA -- so the shell's title reads crossed the A/W
//            boundary, and an ANSI read of a UTF-16 title returns exactly
//            the FIRST CHARACTER ("T") and stops at the NUL -- the
//            byte-level signature both shell surfaces showed. The window
//            is Unicode end to end now: CreateWindowExW + DefWindowProcW,
//            and the title is the full brand "TidyVision TwinSpeak" (the
//            bar keeps drawing its own text). SECOND, the redundant
//            ITaskbarList::AddTab/ActivateTab registration is RETIRED: a
//            visible top-level window gets its native taskbar button
//            anyway, and Win11's labeled taskbar renders AddTab items as
//            minimal-width TABS (icon + first letter) -- the stub's other
//            half. Hiding still removes the button natively. out1/out2
//            sample paths untouched.
//    v7.1.25 TWO FIELD BUGS, ONE DARK-THEME, ONE SHELL. (a) THE TOP EDGE:
//            "the top edge of the window being black", dark themes ONLY.
//            The v7.1.24 fake top shadow (main.c's 1 px row) went out as
//            cPanel2 pushed 45% toward black -- a plausible shadow on the
//            light themes, near-BLACK on every dark one (the field's DARK
//            BLUE measured RGB(12,15,22) against a RGB(25,27,33) face
//            top -- exactly the formula's output). The example itself
//            licenses the recolor ("you might need to tweak the colors
//            here based on the color scheme of your app"): the row now
//            paints PLAIN cPanel2 -- the theme edge color the bar's 1 px
//            bottom edge already uses -- so top and bottom chrome match,
//            nothing blends toward a color the theme does not own, and
//            the inactive blend (toward the face top) only softens it.
//            (b) THE TASKBAR LABEL: "the taskbar name being a single
//            letter" AGAIN, with the v7.1.23 process AUMID verbatim in
//            the binary. The process-wide claim is kept (hardened with a
//            LoadLibrary fallback, both outcomes logged), but the shell
//            reads a WINDOW's identity off the window itself now: the
//            panel claims System.AppUserModel.ID = "TidyVision.TwinSpeak"
//            through SHGetPropertyStoreForWindow + IPropertyStore::
//            SetValue -- the same property a Start-menu shortcut carries,
//            resolved dynamically (the house ITaskbarList trick), no new
//            headers, set before the first paint and before tbShowTab.
//            out1/out2 sample paths untouched.
//    v7.1.24 KEEP-ALIVE 40 Hz @ 0.005 + THE WINDOW IS main.c: two field
//            asks. (a) THE KEEP-ALIVE: the built-in out2 tone moves from
//            1 Hz @ 0.0001 (~-80 dBFS) to 40 Hz @ 0.005 (0.5%, ~-46 dBFS)
//            -- a wobble the cheap USB amp's front end can actually grip,
//            still far below hearing; the cycle table is generic in the
//            rate (48000/40 = a 1200-sample cycle), phase-continuous as
//            always, post-mix as always. Banner prints 40 Hz @ 0.50%.
//            (b) THE WINDOW: the hand-rolled v7.0.9 caption (client ==
//            window, HTCAPTION strip, the 0x00AE/0x00AF + WM_NCPAINT +
//            WM_NCACTIVATE self-heal circus, the v7.1.23 pressed face) is
//            REPLACED with the proven Win32 custom title bar example the
//            field supplied (main.c): WS_THICKFRAME|WS_SYSMENU|
//            WS_MAXIMIZEBOX|WS_MINIMIZEBOX; WM_NCCALCSIZE insets the frame
//            left/right/bottom so the DWM border + shadow own a real NC
//            strip and no frame painter can strike into client pixels;
//            WM_NCHITTEST defers to DefWindowProc for the resize edges,
//            re-answers HTTOP for the top band, HTCAPTION across the bar;
//            caption buttons MINIMIZE + CLOSE (no maximize -- the size
//            lock refuses SC_MAXIMIZE/SC_SIZE and a dead button is a lie)
//            with the hovered slot in GWLP_USERDATA fed by WM_NCMOUSEMOVE
//            and dispatched in WM_NCLBUTTONUP; WM_NCRBUTTONUP opens the
//            system menu with the item states the size lock honors (one
//            deviation: return 0 -- DefWindowProc would stack its own
//            SC_MOUSEMENU menu on top); WM_SETCURSOR answers the arrow;
//            WM_CREATE nudges the frame; the example's fake top shadow
//            survives in the theme's edge color; the bar's buffered theme
//            renderer keeps its gradient, title and dimming; the close
//            hover is the example's red with a white X (the pressed face
//            retires with the client-side click machinery -- the example
//            acts on NCLBUTTONUP while hovered); the creation size, the
//            WM_WINDOWPOSCHANGING pin and WM_GETMINMAXINFO grow by the
//            insets so the designed 880x620 CLIENT is untouched; SC_RESTORE
//            passes again (the taskbar's restore click serves the new
//            minimize). The theme-measured caption height, the theme's
//            DrawThemeTextEx title and the HTMAXBUTTON/SnapLayout hook are
//            deliberately NOT adopted: kCapH is the layout contract, the
//            bar keeps its own brand text, and there is no maximize button
//            to serve. out1/out2 sample paths unchanged except the tone.
//    v7.1.23 SQUARE + SHADOW + LABEL + FLICKER, ALL FOUR CLOSED: the field
//            asked for square corners and the Windows default drop shadow
//            back, and reported two regressions -- the taskbar label
//            collapsed to one letter ("T") and FLICKERS of the classic
//            edges. Four roots, four fixes. (a) THE LABEL: the window
//            carried no AppUserModelID, so Win11's labeled taskbar ran the
//            generic-window path and shrank the button to a one-letter stub
//            while AUMID apps (Chrome, GitHub Desktop) kept full labels --
//            SetCurrentProcessExplicitAppUserModelID("TidyVision.TwinSpeak")
//            claims the identity before any window exists, and the window
//            title compacts to "TwinSpeak" (the bar draws its own brand
//            text; the shell's label reads this one). (b) THE FLICKER: the
//            last classic painter was DefWindowProc(WM_NCACTIVATE) itself
//            -- on the customized-visual-style machine the themed frame
//            painter runs inside it even with lParam -1, and the v7.1.22
//            heal overwrote the strike a frame later: flicker. The default
//            proc is not called at all now (return TRUE; no NC area, so no
//            painter -- themed or classic -- is ever invoked). (c) SQUARE:
//            corner preference back to DWMWCP_DONOTROUND. (d) THE SHADOW:
//            v7.1.22's DWMWA_COLOR_NONE removed the border line and took
//            the DWM drop shadow with it on the field machine -- the call
//            is retired (default border + default shadow return, the pair
//            every normal window carries) and CS_DROPSHADOW rejoins the
//            class style (dormant under DWM composition, the legacy soft
//            shadow when the composition effects are off, and with square
//            corners there is no rounded notch left for it to fill -- the
//            reason v7.1.20 retired it is gone). PLUS the close X gets a
//            PRESSED face -- vertical gradient, top brighter, bottom
//            darker -- and the close fires on RELEASE inside the slot,
//            like every caption button. DSP byte-identical to v7.1.4.
//    v7.1.22 THE LAST EDGE PAINTER: the field still saw a Windows-2000-
//            style hard ring on the window edges. With no NC area, no
//            caption and no region, one persistent painter remained: the
//            DWM's own 1px window border (DWMWA_BORDER_COLOR) -- on a
//            machine with a customized visual style it renders in the
//            theme's hard classic gray. DWMWA_COLOR_NONE retires it (the
//            shadow and the rounded shape are separate attributes and
//            survive; pre-Win11 the call fails harmlessly) and
//            DWMWA_NCRENDERING_POLICY=DWMNCRP_ENABLED pins NC rendering
//            to the DWM for good. The v7.1.18 self-heal also widens:
//            WM_NCACTIVATE/WM_NCPAINT re-blit the WHOLE buffered face
//            (then the bar), so a themed strike into any client edge is
//            overwritten before the message returns. DSP byte-identical
//            to v7.1.4.
//    v7.1.21 THE MANIFEST KILLED THE EXE: v7.1.20's manifest carried "--"
//            inside two XML comments ("comctl32 v6 -- the activation
//            context", "real pixels -- no more bitmap stretch"). "--" is
//            illegal inside XML comments, Windows' SxS loader parses the
//            embedded manifest STRICTLY, refused to build the activation
//            context, and the exe died at startup ("side-by-side
//            configuration is incorrect") before WinMain ever ran. Both
//            comments reworded; build.sh strict-parses the manifest before
//            compiling resources so this class of failure cannot ship
//            again. No code change; DSP byte-identical to v7.1.4.
//    v7.1.20 THE WINDOW ITSELF: a full pass over the top-level window
//            (the interior was already polished). ROUNDING FIXED FOR
//            REAL: CS_DROPSHADOW is retired -- the legacy shadow is drawn
//            from the SQUARE window rect along the bottom edge, and its
//            silhouette filled the DWM-rounded bottom-corner notches,
//            which is exactly why the window read as rounded only on the
//            top half; with the class style gone the DWMWCP_ROUND clip
//            shows on all four corners and DWM's own (shape-following)
//            shadow remains. MANIFEST: the exe embeds one now (RT_MANIFEST)
//            -- System-DPI awareness (the >100%-scaling blur is gone;
//            layout constants, fonts and the strip scale once in
//            guiInitDpi()), comctl32 v6 (guiDarkCtl's DarkMode_Explorer
//            finally binds; v5 classes ignored it), asInvoker,
//            longPathAware. SUBSYSTEM: GUI, not console -- no more black
//            console flash on every double-click / sign-in launch; console
//            modes re-attach to the parent console themselves and a
//            WinMain shim forwards to main(). FOCUS: the first launch
//            activates the window (SW_SHOWNA kept, SetForegroundWindow
//            added). NO FREEZE: STOP/APPLY no longer block the UI thread
//            up to 10 s -- the stop is async (WM_APP+2 reaps the thread
//            and chains the APPLY+RESTART); the blocking join exists only
//            on the shutdown path. SIZE LOCK: WM_WINDOWPOSCHANGING pins
//            the window to the designed size (Aero snap / Win+arrow can
//            no longer stretch the fixed layout) and SC_MAXIMIZE/SC_SIZE
//            are refused. TRAY: failed NIM_ADD retries on a 2 s timer,
//            no more HICON leak (LR_SHARED), the balloon says CLICK (a
//            single left-click opens -- as it always has), the tip
//            reflects the engine state, and the close explainer re-arms
//            for the first three closes per session. EXIT IS FINDABLE:
//            the system menu (Alt+Space / caption right-click) carries
//            "Exit twinspeak"; Esc hides like the X; Enter activates the
//            focused button. POSITION: the window remembers where it was
//            (gui_window_x/y in the ini, spliced by the normal save),
//            every open is clamped into the nearest monitor's work area,
//            and WM_DISPLAYCHANGE re-clamps. PLUS: TaskbarCreated and the
//            second-instance messages are opened through UIPI
//            (ChangeWindowMessageFilterEx), the small icon is a true 16px
//            (WNDCLASSEXW + hIconSm), the bar dims when the window is
//            inactive, the stale X hover is cleared on hide, the 250 ms
//            timer sleeps while hidden, WM_ENDSESSION stops the engine
//            cleanly, the version line ellipsizes long ini paths, and the
//            ITaskbarList is Released at exit. DSP byte-identical to
//            v7.1.4.
//    v7.1.19 ROUND AGAIN, ALL FOUR: the corner preference flips back from
//            DWMWCP_DONOTROUND to DWMWCP_ROUND -- the user asked for the
//            window rounded top AND bottom. With the v7.1.17/18 frame
//            architecture (no region, no WS_CAPTION, client == window) the
//            rounding is pure DWM geometry: it clips the composited alpha
//            shape on Win11 and the DWM shadow wraps the rounded outline;
//            pre-Win11 ignores the call and stays square. Nothing else
//            moves. DSP byte-identical to v7.1.4.
//    v7.1.18 THE UNFOCUS FRAME, RETIRED: field report -- losing focus still
//            grew a full Luna caption (blue gradient, icon + title, glossy
//            red X) over the bar, persistent. The themed frame painter runs
//            on deactivation even with no NC area and WM_NCACTIVATE's -1,
//            so the caption that existed to be painted is removed at the
//            source: WS_CAPTION is gone from the style (WS_SYSMENU |
//            WS_THICKFRAME now) -- no caption, nothing any theme can draw.
//            The DWM drop shadow survives on WS_THICKFRAME (the canonical
//            borderless recipe); WM_NCHITTEST answers flat HTCLIENT below
//            the bar (THICKFRAME would otherwise offer resize edges to a
//            fixed-size window); WM_NCACTIVATE re-blits the bar AFTER
//            DefWindowProc as self-heal insurance. Drag stays HTCAPTION,
//            Alt+Space and the caption right-click stay WS_SYSMENU. DSP
//            byte-identical to v7.1.4.
//    v7.1.17 THE FRAME THAT CANNOT FLASH: the XP-caption artifact is
//            structural, so the fix is structural -- WM_NCCALCSIZE now
//            returns the whole window as client (the v7.0.9 top-inset kept
//            a real NC strip, and GDI could still classic-paint it on
//            drag/activate). There is no non-client area left: no message
//            path can ever paint a caption that is not ours. The bar is
//            the client's top 31 px (every child coordinate keeps its
//            meaning -- the helpers add kCapH at creation, the frame paint
//            shifts by kCapH); drag answers HTCAPTION explicitly; the DWM
//            owns the frame again -- its own Win10 drop shadow, nothing
//            fighting it, corners pinned SQUARE (DWMWCP_DONOTROUND); the
//            v7.1.0 rounding region is gone (a region-shaped window
//            cannot carry the DWM shadow). Plus the reconstructed
//            v7.1.14-16 work: the first-second FAST TIER (warden at 10x
//            cadence against the delay model), the CENTER EQ page stack
//            (SAVE TO SLOT below the sliders + note, description never
//            cuts off), and the ON/OFF pill contrast. DSP byte-identical
//            to v7.1.4.
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
//      delay1_ms / delay2_ms    ; bit-exact ring delay per leg (the EARLY
//                               ; leg takes the delay; see the template)
//      lfe_fold = on            ; (retired -- the fold is built in, ignored)
//      input_device / output_device1 / output_device2
//      output_device3          ; v8.4.0 rear legs -- EMPTY = silent (v8.4.11:
//                               ; ONE device carries both rears;
//                               ; output_device4/offset4_ms retired, ignored)
//      offset3_ms              ; v8.4.0: the rear leg's delay twin
//      optional: gain_l, gain_r, gain_rl, gain_rr, gain_lfe, gain_center,
//                out2_first, volume_endpoint, chimes, chime_gain,
//                pin_outputs (retired key, ignored)
//                (v7.1.7: keepalive_hz/keepalive_gain retired -- BUILT IN,
//                1 Hz @ 0.0001; old keys load clean and are ignored)
//                (v8.2.0: out1ms/out2ms retired -- the WASAPI period is a
//                1 ms engine constant; buffer_size went with them)
//                (v8.3.0: volume_follow, drift_correct_ms, drift_interval_ms,
//                deep_resync_min retired -- hardcoded from the fielded ini:
//                on / 5 / 5000 / 30)
//      Integrated (always on, old keys load clean and are ignored):
//      capture_exclusive, exclusive, align_modes, clock_sync, drift_guard,
//      power_guard, yield_default, center_gate, center_upmix_guard, background.
//
//  Input channel order is the VAC 8ch standard: 1L 2R 3C 4LFE 5SL 6SR 7RL 8RR.
//  leg1 output order is Windows 5.1: FL FR FC LFE BL/SL BR/SR (6ch).
//  Fixed 48 kHz (the cable's rate). Build: bash build.sh  (zig c++, WASAPI).
// ============================================================================

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "propsys.lib")

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

// v7.9.0: ONE version constant. The startup banner used to carry a stale
// hard-coded "v7.1.19" string no matter the real build -- every version-
// bearing string now quotes this (banner, ini template header).
#define TWS_VERSION "8.4.15"
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
// v7.8.0: the one-file window header is RETIRED whole -- no include, no
// external chrome API, no stub file. The window is a plain native
// WS_OVERLAPPEDWINDOW (the flat default Windows 10 frame) and the one
// piece of plumbing that survived, the DPI child ledger, lives in this
// file now (tsTrackChild / tsRelayoutChildren, with the control factories
// below).

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
static constexpr int      kRearLCh       = 6;      // 0-based: cable ch7 (RL) -- out3's LEFT source (v8.4.5: hoisted for the lane gates)
static constexpr int      kRearRCh       = 7;      // 0-based: cable ch8 (RR) -- out3's RIGHT source (v8.4.11: one device)
static constexpr int      kSideLCh       = 4;      // 0-based: cable ch5 (SL) -- out3's left side-fold source (v8.4.6)
static constexpr int      kSideRCh       = 5;      // 0-based: cable ch6 (SR) -- out3's right side-fold source (v8.4.6/8.4.11)
static constexpr int      kScratchFrames = 8192;
static constexpr unsigned kSampleRate    = 48000;

// v8.4.7 SLOW SYNC TO THE FIRST DEVICE (field-directed: "each audio
// device delays eachother over time ... all sync together to exactly that
// first audio device ... minimally ... no new ini keys nor ... the user
// interface"). Every output leg EXCEPT leg1 servos its play boundary to
// LEG1's (the first device is the master, never adjusted): each leg
// latches its settled offset once, then holds it with kSyncNudgeFr-frame
// nudges, rate-limited -- a drop (discardOldest(kSyncNudgeFr)) when it
// runs late, a pad (the switch's last kSyncNudgeFr frames hold the last
// mixed one) when it runs early. The DAC always receives its full
// frameCount; nothing resamples, nothing bends pitch, no key, no UI row.
// v8.4.9 THE WIDE NUDGE (field-directed: "the 1-frame nudge is clearly
// causing decoherence in the sample level stuff ... I want a 16 sample
// nudge for every 100ms"): the step is 16 frames and the window 100 ms
// -- fewer, wider, cleaner corrections instead of per-frame chatter.
static constexpr int      kSyncDeadBandFr = 12;     // 250 us at 48 kHz -- the ear places ~1 ms between speakers
static constexpr int      kSyncNudgeFr    = 16;     // v8.4.9: the nudge step -- 16 fr (333 us) drop or held-sample pad
static constexpr unsigned kSyncNudgeMs    = 100;    // v8.4.9: 16 fr per window: 160 fr/s capacity (~3333 ppm)
static constexpr unsigned kSyncSettleMs   = 2000;   // v8.4.13: a pure startup hush past the last (re)open (no baseline)
// v8.4.11 THE WARDEN BITE (the old version's drift correction, from the
// shipped ini: drift_correct_ms = 5, drift_interval_ms = 2000): past 5 ms
// of accumulated drift a leg takes up to a 240-frame bite (drop or
// held-sample pad) at most once per 2 s, on top of the wide nudge -- slow
// creep is CORRECTED, never accepted.
static constexpr int      kDriftCorrectFr  = 240;  // 5 ms @ 48 kHz (the shipped ini's drift_correct_ms)
static constexpr unsigned kDriftIntervalMs = 2000; // (the shipped ini's drift_interval_ms)
// v8.4.12 THE DEPTH GUARD: the servo holds the SLAVES to the master's play
// boundary, but off = r2 - r1 (the REAL-backlog difference, prefill
// excluded) is blind to COMMON-MODE ring growth -- a shared-mode capture
// burst (audiodg under load: OBS, a game) permanently added its size to
// EVERY ring, ratchet by ratchet (the field's raw creep to 62 ms over an
// hour, and the jump to the ring's power-of-two cap -- 8192 fr = 170.7 ms
// at delay 0 -- when OBS opens). Nothing ever drained depth back down.
// The master's real backlog is now PINNED to its anchor: past the
// dead-band the oldest frames are dropped, at the warden's pacing, and
// the err loop drags every slave to the same anchor. Re-latches are GONE
// (v8.4.12): a jump is ground back like any other error -- only a reopen
// re-arms the baseline (the settle gate).
static constexpr unsigned kDepthAnchorFr   = 480;  // 10 ms: capture delivery + device buffer + slack
static constexpr int      kDepthDeadbandFr = 240;  // 5 ms: healthy jitter is never touched
static constexpr unsigned kSyncRefDeadMs  = 1000;   // leg1's heartbeat must be this fresh to be a reference
// v8.4.13 NO BASELINE: the settled-offset latch (syncBase) is RETIRED -- it
// was the last acceptance path. Two seconds after any reopen it captured
// whatever off happened to be, and a burst still in the rings became the
// servo's truth FOREVER (the field's "raw2 jumped to like 70 after raw1
// went to 11 and then 7" -- the servo defending raw1 + bloat), and the
// baseline random-walked up a few ms per event (raw2 creeping 4-8 ms over
// raw1 in eight idle minutes). The user's calibration lives in the delay
// prefills, which off never sees; a nonzero settled offset is startup
// skew, and startup skew is drift to be corrected, not held. err = off.

// v8.4.5 THE UNIVERSAL KEEP-ALIVE tone: 20 Hz at 0.1% gain (0.001,
// ~-60 dBFS -- 14 dB under v7.1.24's 40 Hz @ 0.5%) summed onto EVERY
// output leg at the DEVICE layer -- every frame, silence or not, gated or
// not, stop gap or not, underrun or not, phase-continuous PER LEG. A
// gated block is PROGRAM silence, never device silence: the wobble is
// what keeps every amp's front end gripped across it (v8.4.4's
// kill-the-wobble walked back, field-directed). leg1 carries it on FL/FR
// only -- the 5.1 stream's FC/LFE/rear slots stay DRIVEN SILENCE (the
// upmixer-exclusion contract). Rendered from a precomputed cycle table
// (48000/20 = a 2400-sample cycle) instead of calling sin() per frame.
// v7.1.7: BUILT IN -- the ini keys and UI rows are gone. POST-MIX by
// construction: the tone is summed in the OUTPUT callbacks, after the
// mix, so it can never enter the capture stream an OBS display-capture
// records.
static constexpr double   kTwoPi             = 6.283185307179586;
static constexpr unsigned kKeepAliveTableMax = 48000;  // capacity: the 1 Hz worst case
static constexpr float    kKeepAliveHz       = 20.0f;   // v8.4.5: 20 Hz @ 0.001, every leg
static constexpr float    kKeepAliveGain     = 0.001f;  // (0.1%, ~-60 dBFS)
static float              g_kaHz   = kKeepAliveHz;
static float              g_kaGain = kKeepAliveGain;
static unsigned           g_kaLen  = 48000;   // samples per cycle: round(48000 / g_kaHz)
static float              g_kaTable[kKeepAliveTableMax];
// v8.4.5: per-leg phase -- each leg's device thread owns its own index
// into the shared read-only table (the v8.4.4 single-index rule
// generalized to four writers; no atomics needed, the cgGain pattern).
static unsigned           g_kaIdxLeg[4] = {0, 0, 0, 0};

// v8.1.0 THE STRIPPED PATH: clock discipline is OBSERVATION ONLY -- a
// shortfall plays silence and is counted, capture excess evicts oldest at
// the raw cap (ledger-true), nothing ever repeats or drops on a schedule.
static size_t             g_capPeriodFrames   = 192;    // real negotiated capture period (informational)
static size_t             g_periodFrames[4]   = {480, 480, 480, 480}; // real device period per leg (v8.4.0: four)
static constexpr unsigned kReplayMaxPeriods   = 4;      // lastBlock size (the KS lapse re-prime)
// v8.2.0: the WASAPI device period is an ENGINE CONSTANT -- out1ms/out2ms
// are read-and-ignored (like cap_ms). Both legs request 1 ms (the floor
// every modern exclusive driver negotiates around); a KS leg -- the
// default -- ignores the request whole. Equal by construction.
static constexpr unsigned kOutPeriodMs       = 1;      // WASAPI leg request (v8.2.0)

// v8.4.11 THE 384x3 BITE (field-directed: "I think the audio device drifting
// is from the fact the sample size is still a tad too small ... I want it so
// it's 384 samples with 3 buffers instead. At this point that's just enough
// for me"): the bite is 384 frames x 3 periods -- 8.0 ms of service per
// write, ~125 writes/s, a 1152-fr / 24.0 ms WaveRT loop and a 768-fr /
// 16.0 ms write lead (the deepest shipped lead yet: a late service has
// twice the v8.4.9 margin before the DAC grazes the buffer edge -- the
// field's own diagnosis for the drift stress). Same kernel loop, same
// register pacing, no resampler anywhere; the est-path banner prints the
// lead live. No ini key, no GUI row, no clamp; stale ks_period_frames /
// ks_periods lines load clean and are ignored.
static constexpr unsigned kKsPeriodFrames    = 384;  // the bite: 8.0 ms of service @ 48 kHz (v8.4.11)
static constexpr unsigned kKsPeriods         = 3;    // the WaveRT loop: 1152 fr (24.0 ms), lead 768 fr (16.0 ms)

// v6.4 drift warden pacing: the observe log never fires more than once per
// drift_interval_ms (v6.5.8; was the hardcoded 30 s), only after the drift
// error held for 3 consecutive health windows.
static constexpr unsigned kWardenStreakNeeded  = 3;

struct Config {
    // v8.2.0: out1Ms/out2Ms are RETIRED, with the legacy buffer_size alias
    // and its haveOut fallback -- the WASAPI device period is an engine
    // constant now (kOutPeriodMs = 1 ms; a KS leg ignores it whole). The
    // keys are read-and-ignored in loadIni, like cap_ms before them.
    bool        captureExclusive = true;  // v6.6.8: INTEGRATED -- always on
    bool        exclusive      = true;    // v6.6.8: INTEGRATED -- always on (v3 default was off)
    bool        alignModes     = true;    // v6.6.8: INTEGRATED -- always on
    bool        lfeFold        = true;    // v3.1 routing; v8.4.0: INTEGRATED -- always on
    std::string inputDevice;
    std::string outputDevice1;
    std::string outputDevice2;
    // v8.4.0 THE REAR LEGS; v8.4.11 THE ONE REAR LEG: output_device3 opens
    // ONE stereo device carrying BOTH rears (out L = ch7+ch5, out R =
    // ch8+ch6). output_device4 is RETIRED -- read-and-ignored (stale ini
    // lines load clean). An EMPTY output_device3 keeps the rears SILENT --
    // no device is opened, no ring is built, the needle never matches:
    // empty in = empty out, the truth.
    std::string outputDevice3;
    std::string volumeEndpoint;      // v3.9: needle for the cable's PLAYBACK endpoint
                                     //       (volume follow); empty = use input_device
    float       gainL          = 1.0f;
    float       gainR          = 1.0f;
    float       gainLfe        = 0.4f;
    float       gainCenter     = 1.0f;
    // v8.4.0: the rear gains; v8.4.11: leg3's ONE stereo device --
    // out L = gain_rl * (ch7 + ch5), out R = gain_rr * (ch8 + ch6).
    float       gainRl         = 1.0f;
    float       gainRr         = 1.0f;
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
    // v8.4.0: delay1/2's exact twin for the rear leg -- a whole-ms silence
    // prefill on leg3's raw ring (the user's naming: offset3_ms;
    // offset4_ms retired with the one-rear-leg merge, read-and-ignored).
    float       offset3Ms      = 0.0f;
    bool        centerGate     = true;    // v6.1: ch3 digital-zero -> the out2 mix mutes (v8.4.5: the universal wobble rides on)
                                          //       v6.6.8: INTEGRATED -- always on
    bool        centerUpmixGuard = true;  // v6.1: ch3 == a*ch1+b*ch2 (Windows fills FC
                                          //       for stereo sources) -> block muted
                                          // v6.6.8: INTEGRATED -- always on
    bool        volumeFollow   = true;   // v3.8: Line 1 endpoint volume drives both legs
                                          // v8.3.0: HARDCODED (was volume_follow) -- born
                                          // on, no key, no pill; nothing can flip it
    bool        pinOutputs     = true;   // v3.8: out1/out2 endpoints pinned 100%/unmuted.
                                          // v7.0.0: INTEGRATED -- always on, no key, no pill
    bool        out2First      = true;   // v3.8: center leg opens and starts first
    // v7.1.7: keepalive_hz / keepalive_gain are BUILT IN (kKeepAliveHz /
    // kKeepAliveGain; v7.1.24: 40 Hz @ 0.005; v8.4.5: 20 Hz @ 0.001, every
    // leg, through the gates) -- no struct fields, no ini
    // key, no UI.
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
    // v6.3 per-leg backend; v8.0.0: "ks" IS the default -- the kernel-level
    // loop. "ks" = direct kernel-streaming render pin + looped WaveRT buffer
    // paced against the mapped play-position register (below the audio
    // engine, FlexASIO-grade). "wasapi" = miniaudio exclusive WaveRT event
    // mode -- the automatic fallback when a pin is refused, and the opt-out.
    // v8.4.5-v8.4.9 THE CALM BITE: ks_period_frames / ks_periods are BUILT
    // IN (kKsPeriodFrames 288 / kKsPeriods 2) -- no struct fields, no ini
    // keys, no GUI rows; stale lines load clean and are ignored.
    std::string backend1       = "ks";
    std::string backend2       = "ks";
    // v6.4 drift warden: the frame-interval ledger is ALWAYS on (it only
    // observes); drift_guard switches the automatic re-pin on top of it.
    bool        driftGuard     = true;   // auto re-pin at >= drift_correct_ms
                                          // v6.6.8: INTEGRATED -- always on
    float       driftCorrectMs = 5.0f;   // v7.0.0: default 5 ms (was 10) -- trigger distance
                                          // v8.3.0: HARDCODED (was drift_correct_ms) --
                                          // 5 ms, the fielded ini's value; no key, no row
    // v6.5.8: minimum spacing between warden corrections -- was the hardcoded
    // kWardenMinIntervalMs = 30000. v8.3.0: HARDCODED (was drift_interval_ms)
    // -- 5000 ms, the fielded ini's value; no key, no row.
    unsigned    driftIntervalMs = 5000;
    // v6.5.12: scheduled fresh reopen of the CENTER leg (minutes; 0 = off).
    // The S5's below-packetizer FIFO can deepen silently over hours -- no
    // host meter sees that layer; a scheduled reopen is the one reset that
    // re-primes it (brief gap, warden re-arms ~9 s). v8.3.0: HARDCODED (was
    // deep_resync_min) -- 30 min, the field's long-movie setting.
    unsigned    deepResyncMin   = 30;
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
    // v7.8.3 DROP CORRECTION: a buffer switch the out ring cannot fill used
    // to tile-replay the last played buffer (the ASIO convention) -- under
    // CPU pressure that repeats every switch and reads as crackle. With
    // drop_correct the shortfall is silence-filled instead and the ring
    // RESTARTS: the leg holds off on silence until the ring rebuilds to its
    // target anchor, then skips the silent gap oldest-first and resumes at
    // the freshest capture with the original latency. Default ON; off = the
    // legacy replay path, bit-for-bit.
    // v7.8.4: INTEGRATED -- always on. No ini key (a stray drop_correct
    // line in an old ini is read and ignored), no GUI pill, no writer
    // anywhere: the field is born true and nothing can flip it.
    bool        dropCorrect    = true;
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
    unsigned      periodFrames = kKsPeriodFrames;  // write granularity == the built-in bite (v8.4.5)
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
    int                 outCh = kOutCh1;         // 6 for mains, 2 for center/rears
    // v8.4.0: an OPTIONAL leg (rear, output_device3/4 empty) -- set once on
    // the main thread at boot BEFORE any device thread exists; every loop
    // that walks the legs skips a disabled one (no ring, no device, no
    // watchdog, no yield -- nothing to do).
    bool                disabled = false;
    bool                zeroFast = false;        // v6.6.9: bit-zero blocks skip the mix (start-frozen gate)
    Ring                raw;                     // 8ch: capture cb -> the buffer switch (v8.1.0)
    std::vector<float>  scratch;                 // switch-private 8ch work buffer (v8.1.0)
    // v6.0 replay state -- v8.1.0: the replay itself is GONE; lastBlock
    // survives ONLY as the KS lapse guard's re-prime source (the last played
    // frames, tiled into the lead region after a lost loop lap).
    std::vector<float>  lastBlock;               // last played frames (outCh interleaved)
    size_t              lastCap = 0;             // frames lastBlock can hold
    size_t              lastN = 0;               // frames actually stored
    bool                haveLast = false;        // false until the first real buffer
    ma_device           dev = {};
    ma_device_id        id = {};                 // WASAPI endpoint id (volume pin)
    bool                isExclusive = false;
    std::string         name;

    std::atomic<unsigned long long> rawDrop{0}, und{0};   // v8.1.0: passive evictions / short switches
    // v6.0 clock telemetry: captured = frames the cable delivered (cable
    // clock); popped = frames the playback callback consumed (device clock).
    // The 1 s health loop turns the pair into the measured offset ("off").
    std::atomic<unsigned long long> captured{0}, popped{0};
    // v6.1 alignment delay (a silence PREFILL on the raw ring, v8.1.0 -- kept
    // separately for the banner) and center-presence gate state (leg2's
    // device thread owns the guard state):
    size_t              delayFrames = 0;         // extra ring depth = delay_ms * 48
    float               delayMs     = 0.0f;      // as configured (banner)
    std::atomic<unsigned long long> cmUpmix{0}; // blocks muted by the upmix guard
    // v8.4.10/8.4.11: the upmix guard is INSTANT and the zero is INSTANT --
    // each chunk's verdict is that chunk's own fingerprint evidence,
    // applied to that chunk BEFORE it is mixed (the v7.8.4 2-hit/8-miss
    // hysteresis retired: it left the gate whole buffers behind the
    // evidence; the v7.8.4 2.5 ms fade retired v8.4.11: it WAS the leak --
    // every engage let ~120 frames of a*FL+b*FR through at ramping gain).
    // Leg2's device thread owns the state (single writer, no atomics
    // needed).
    bool                cgMuted = false;         // guard engaged: ch3 ZEROED on the spot
    float               cgGain  = 1.0f;          // verdict mirror for the pane (1 = pass, 0 = zeroed)
    unsigned long long  lastGateLogMs = 0;       // rate-limited gate/guard logs
    unsigned long long  lastUpmixLogMs = 0;
    // v8.4.4/v8.4.5 THE ABSOLUTE LANE GATE: this block's verdict + the log
    // latch, PER LANE (center ch3 / rear L ch7 / rear R ch8; v8.4.6 adds
    // the side lanes ch5/ch6 on the rear legs). Written and read by that
    // leg's device thread only -- single writer, no atomics, the cgGain
    // pattern. v8.4.5: the universal keep-alive rides THROUGH the gate, so
    // the log state-change is the latch's only consumer. v8.4.6: every
    // lane latches BY ITSELF -- a rear leg carries two (its rear channel
    // on gateMuted, its side fold on gateMuted2), so each channel's
    // evidence stays independent.
    bool                gateMuted    = false;    // this block's verdict: the lane's mix is muted (the wobble still rides)
    bool                gateWasMuted = false;    // previous block's verdict (state-change logging)
    bool                gateMuted2   = false;    // v8.4.6: the side lane's own verdict (rear legs only)
    bool                gateWasMuted2 = false;   // v8.4.6: its state-change latch ('side L/R gate')
    unsigned long long  lastSideLogMs = 0;       // v8.4.6: the side gate's own rate limit
    // v8.4.11: the merged rear's RIGHT lanes, each on its own latch and its
    // own rate limit (rear R ch8 / side R ch6) -- the v8.4.6 contract
    // verbatim, one device carrying both rears.
    bool                gateMuted3   = false;    // v8.4.11: rear R's own verdict ('rear R gate')
    bool                gateWasMuted3 = false;
    bool                gateMuted4   = false;    // v8.4.11: side R's own verdict ('side R gate')
    bool                gateWasMuted4 = false;
    unsigned long long  lastSide2LogMs = 0;      // v8.4.11: rear R gate's own rate limit
    unsigned long long  lastSide3LogMs = 0;      // v8.4.11: side R gate's own rate limit
    // ---- v8.4.7 SLOW SYNC TO LEG1: the drift servo state (no key, no UI).
    // v8.4.13: NO BASELINE -- the servo's error IS the measured offset
    // (the settled-baseline latch was the last acceptance path: 2 s after
    // any reopen it captured whatever off happened to be, and a burst
    // still in the rings became the servo's truth forever -- raw2 parked
    // at raw1 + bloat, and the baseline random-walked up a few ms per
    // event). The settle gate in syncToFirst is a pure startup hush now.
    // Written by this leg's device thread ONLY (the syncToFirst call
    // inside its buffer switch); lastSyncMs rate-limits the servo look;
    // syncDrops/syncPads count the nudges (logged rate-limited, per-run
    // like the ledger clocks).
    unsigned long long     lastSyncMs    = 0;
    unsigned long long     syncDrops     = 0;
    unsigned long long     syncPads      = 0;
    unsigned long long     lastSyncLogMs = 0;
    // v8.4.11/8.4.12: the warden bite state (this leg's device thread only,
    // the same single-writer discipline). lastBiteMs rate-limits the
    // 240-frame bite to one per 2 s (the slave's err bites AND the master's
    // depth-guard bites -- a leg never runs both); wardenDrops/wardenPads
    // count the bites, depthDrops counts the v8.4.12 depth-guard drops.
    unsigned long long     lastBiteMs    = 0;
    unsigned long long     wardenDrops   = 0;
    unsigned long long     wardenPads    = 0;
    unsigned long long     depthDrops    = 0;
    // v8.4.14 THE DEFERRED REOPEN LANDING: set by tryRestartLeg (the health
    // thread, BEFORE the new device thread exists -- the write happens-
    // before the thread creation, and the old thread is already joined);
    // consumed and cleared by that new device thread at its FIRST buffer
    // switch, where landReopenedRing discards the refill-bloated backlog
    // with the first pop one instant away. Plain bool: one writer before
    // the thread, one reader after.
    bool                   relandPending = false;
    // ---- v7.9.0 SPIN PACER telemetry (device-thread owned) -----------------
    // The playback callback IS the deadline: the exclusive buffer holds two
    // periods and the event fires with one period left to play, so a switch
    // interval beyond period + 1 ms means the thread was served late and the
    // DAC is eating into its last buffer margin -- the device-side static no
    // ring logic can ever see. Metered, logged, and counted.
    LARGE_INTEGER       lastSwQpc = {};          // previous switch stamp (QPC)
    bool                haveSwQpc = false;       // armed after switch #2
    unsigned long long  lastLateLogMs = 0;       // rate limit: one line / 10 s / leg
    std::atomic<unsigned long long> lateSwitches{0}; // late device switches since boot
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
    // ---- v6.4 DRIFT WARDEN: frame-interval ledger (v8.1.0: MEASUREMENT ONLY) --
    // The RAW ring's seqTail IS this leg's stream position (the capture
    // sequence of its play boundary). Pops and passive evictions advance it;
    // silence prefills do not. The two legs' difference = the measured
    // inter-speaker offset. The buffer switch owns the boundary, the health
    // loop owns the baseline -- corrections are GONE (v8.1.0). v8.4.7: the
    // INTER-LEG sync returns as a slow servo on this same boundary (see
    // syncToFirst -- out2/3/4 hold their settled offset to leg1, 16 frames
    // at a time (v8.4.9), no key, no UI); the cable clock itself stays
    // measure-only.
    size_t              prefillRemaining = 0;        // delay prefill: carries no sequence
    std::atomic<double> lagMs{0.0};                  // age of the newest played frame (ms)
    // v7.1.5 START ALIGN: the QPC microsecond stamp of this leg's FIRST buffer
    // switch of the run -- the moment THIS device began eating its ring. The
    // two legs' difference is the real start stagger; the start sequence pads
    // the early leg by exactly that gap so both streams open together.
    std::atomic<unsigned long long> firstSwitchUs{0};
    unsigned long long  lastReopenMs = 0;            // warden pauses 10 s after a reopen
    // ---- v7.8.3/v7.8.4 DROP CORRECTION + SHALLOW ESCALATION: REMOVED in
    // v8.1.0 (the whole ring-depth machinery went with them). A short switch
    // plays silence and counts into 'und' -- nothing escalates, nothing
    // restarts, nothing repeats a frame.
};
// v8.4.0: FOUR slots -- 0 = mains (5.1), 1 = center, 2 = the rears (ONE
// stereo device, v8.4.11), 3 = RETIRED (force-disabled; the old leg4's
// slot). The rear leg is OPTIONAL: an empty output_device3 marks it
// disabled.
// v8.4.1: leg1 (center) is optional TOO -- an empty output_device2 marks it
// disabled and turns on THE CENTER FOLD below.
static Leg g_leg[4];

// v8.4.1 THE CENTER FOLD: an EMPTY output_device2 (no center device) means
// leg2 never opens AND ch3 (center) folds onto leg1's FL/FR at gain_center
// -- the LFE fold's pattern: FL = gain_l*L + gain_lfe*LFE + gain_center*C,
// FR the mirror. The SAME center gates ride the fold (the upmix guard fades
// a stereo source's phantom FC out of the fronts; genuine 5.1 center always
// plays). Plain bool on the g_leg[].disabled discipline: written once per
// engine start on the main thread before any device thread exists. With
// output_device2 set, leg2 plays ch3 on its own device exactly as before
// and the fold stays off.
static bool g_centerFold = false;

// v8.4.0: one place answers "which ini needle opens leg i" -- every open /
// reopen / yield path asks here, so a rear leg can never fall back to a
// wrong device's needle.
static const char* legNeedle(int i) {
    switch (i) {
        case 0:  return g_cfg.outputDevice1.c_str();
        case 1:  return g_cfg.outputDevice2.c_str();
        case 2:  return g_cfg.outputDevice3.c_str();
        default: return "";            // v8.4.11: leg4 is retired -- never opens, never yields
    }
}

static std::atomic<bool> g_run{true};
static std::atomic<bool> g_stopSilence{false};  // v7.0.0: the stop gap emits silence

// ------------------------------------------------------------ v3.8 volume --
// VOLUME FOLLOW: the Line 1 playback endpoint's Windows volume is the single
// master knob. A 25 ms watcher polls it; the pumps scale every captured
// frame before mixing, so both legs track identically and the out1/out2
// sliders stay pinned at 100%. The universal keep-alive (v8.4.5, every
// leg) is summed at the device layer and is deliberately NOT scaled.
// GUIDs spelled out: zig c++ (GNU mode) has no reliable __uuidof.
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
static OutPin g_pin[4];                               // v8.4.0: per-leg endpoint pins (watcher-owned)

// v3.8 watchdog heartbeats: each device callback refreshes its timestamp;
// the health loop reopens any thread that goes silent >5 s.
static std::atomic<unsigned long long> g_hbCap{0};
// v8.4.0: one heartbeat slot per leg (was the two scalars g_hbOut0/g_hbOut1).
static std::atomic<unsigned long long> g_hbOutTab[4];
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
    g_hbOutTab[i].store(GetTickCount64(), std::memory_order_relaxed);
}
static inline unsigned long long hbOutGet(int i) {
    return g_hbOutTab[i].load(std::memory_order_relaxed);
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
static void boostAudioThreadEx(int mmcssSubPrio) {
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
        if (set) {
            DWORD idx = 0;
            HANDLE h = set(L"Pro Audio", &idx);
            // v7.9.0: sub-priority INSIDE the "Pro Audio" task. Within one
            // task every registered thread shares the same base priority and
            // MMCSS round-robins them -- twinspeak's center pump and, say,
            // Chrome's own Pro Audio threads were equals at 100% CPU.
            // AvSetMmThreadPriority raises a thread above its task mates:
            // the center pump takes CRITICAL, the mains pump NORMAL. Values
            // are the AVRT_PRIORITY enum (NORMAL = 0, CRITICAL = 2).
            if (h && mmcssSubPrio >= 0) {
                typedef HRESULT (WINAPI *AvPrioFn)(HANDLE, int);
                AvPrioFn prio = (AvPrioFn)GetProcAddress(av, "AvSetMmThreadPriority");
                if (prio) prio(h, mmcssSubPrio);
            }
        }
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
}
static void boostAudioThread() { boostAudioThreadEx(-1); }   // no sub-priority hint

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

// v7.6.3: is this endpoint (by its id string) the CURRENT Windows default
// playback (render) device? The pin layer must never write a volume to the
// endpoint the user is LISTENING on: when a leg's device IS the default
// render endpoint (the yield_default case), its volume slider belongs to
// the user -- engine start and reopen must leave it exactly where it is.
// Exact id-string compare: friendly names can collide, endpoint ids cannot.
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

// every 2 s from the watcher: every WASAPI leg's endpoint must sit at 100%
// unmuted. Also re-resolves a pin whose endpoint id changed (watchdog reopen).
static void pinCheckTick() {
    if (!g_cfg.pinOutputs) return;
    // v3.9: this runs on the watcher thread and no longer touches Leg fields
    // -- tryRestartLeg (main thread) re-pins after a reopen, so the two
    // threads never race on the same Leg state.
    for (int i = 0; i < 4; ++i) {   // v8.4.0: all four legs
        if (g_leg[i].disabled) continue;   // nothing held, nothing to pin
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
    for (int i = 0; i < 4; ++i) {
        // v8.4.0: a disabled rear leg owns no ring (pushOverwrite on an
        // uninit ring would be UB) -- empty device = nothing to feed.
        if (g_leg[i].disabled) continue;
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

// ------------------------------------------------- v8.4.7 slow sync --------
// THE PROBLEM (field-directed): every output device carries its own
// crystal, and the crystals disagree by a few ppm -- over an hour the
// legs' relative offset walks by tens of milliseconds (one device slowly
// delays the others) until the raw cap evicts or the stream starves and
// the offset jumps. THE FIX: every leg EXCEPT the first servos to LEG1's
// play boundary -- the FIRST device is the master, never adjusted, and
// all the others sync together to exactly it. The metric is the two
// seqTails' difference with EACH LEG'S OWN DELAY SUBTRACTED (v8.4.15:
// the master's delay1_ms sat inside the raw difference and the servo
// padded the slaves up to its delayed boundary -- the field's "raw2
// climbs to roughly that delay"; the error is now the backlogs ABOVE
// each leg's configured delay): pure frame arithmetic, no clock, no
// resampler. v8.4.13: the servo's ERROR IS that difference -- no settled baseline
// (the latch was an acceptance path; see the constants block) -- corrected
// with kSyncNudgeFr-frame nudges, at most one
// per kSyncNudgeMs and only past kSyncDeadBandFr (v8.4.9: 16 fr /
// 100 ms). LATE (its crystal runs slow, its ring grows):
// discardOldest(kSyncNudgeFr) -- the boundary advances 16 frames, 333 us
// earlier. EARLY (crystal fast, ring shallow): the switch pops
// kSyncNudgeFr frames less and holds its last mixed frame across them --
// the DAC always receives its full frameCount, and the pad is a held
// sample, not a hole.
// v8.4.11 THE WARDEN BITE: the old version's drift correction (the
// shipped ini: drift_correct_ms = 5, drift_interval_ms = 2000) rides the
// same servo -- past 5 ms of accumulated drift a leg takes up to a
// 240-frame drop or held-sample pad, at most once per 2 s, on top of the
// wide nudge; slow creep is CORRECTED, never accepted. v8.4.12: NO
// RE-LATCH of any kind -- a jump is ground back like any other error
// (the old |err| test AND the v8.4.11 step test both silently ACCEPTED
// bloat as a new baseline; only a reopen re-arms, through the settle
// gate), and the master's DEPTH GUARD (masterDepthGuard, below) owns
// common-mode ring growth, which this difference metric cannot see.
// A yielded or heart-silent leg1 freezes
// the sync whole (a parked leg's ring still evicts, and eviction jitter
// is not a clock to follow). Returns the number of pad frames the switch
// must synthesize (0, or up to kDriftCorrectFr, clamped under the block).
// No ini key, no UI row: the constants are the engine's truth.
static size_t syncToFirst(int idx, Leg& L, size_t frameCount)
{
    const unsigned long long nowMs = GetTickCount64();
    if (nowMs - L.lastSyncMs < kSyncNudgeMs) return 0;   // slow: one look per window
    L.lastSyncMs = nowMs;
    // leg1 must be ALIVE as a reference: not yielded, its device callback
    // heartbeating. Otherwise freeze (keep the baseline, nudge nothing).
    if (g_leg[0].yielded.load(std::memory_order_relaxed)) return 0;
    if (nowMs - hbOutGet(0) > kSyncRefDeadMs) return 0;
    // v8.4.15 THE DELAY-NEUTRAL METRIC (field-directed: "make sure that
    // leg1, leg2, and leg3 all be subtracted by their individual delays
    // (delay1_ms, delay2_ms, and delay3_ms) ... so that measurements are
    // accurate"). Each leg's play boundary sits delay_i_ms behind the
    // capture BY CALIBRATION -- the v8.4.13 metric (the raw seqTail
    // difference) included the MASTER's delay, so with delay1_ms = 68 the
    // servo read the center 68 ms EARLY and padded raw2 up ~68 ms to
    // "sync" it to the mains' DELAYED boundary -- the calibration defeated
    // by its own servo. The error is now measured between the UNDELAYED
    // boundaries: each seqTail is shifted forward by its own delay, i.e.
    // err = (depth2 - delay2) - (depth1 - delay1) -- the backlogs ABOVE
    // each leg's configured delay, the user's prefill untouched.
    const long long off =
        (long long)(g_leg[0].raw.seqTail.load(std::memory_order_relaxed)
                    + (long long)g_leg[0].delayFrames)
        - (long long)(L.raw.seqTail.load(std::memory_order_relaxed)
                      + (long long)L.delayFrames);
    // settle: kSyncSettleMs past the last (re)open AND kSyncSettleMs of
    // this leg's own switches -- the prefills and the start-align pads are
    // through. v8.4.13: a pure STARTUP HUSH -- no baseline to arm or latch
    // (the settled latch captured mid-grind bloat as the servo's truth; see
    // the constants block).
    const unsigned long long fsUs = L.firstSwitchUs.load(std::memory_order_relaxed);
    if (fsUs == 0 || nowMs - L.lastReopenMs < kSyncSettleMs) return 0;
    LARGE_INTEGER fsw;
    QueryPerformanceCounter(&fsw);
    const unsigned long long nowUs =
        (unsigned long long)((double)fsw.QuadPart * 1e6 / g_qpf);
    if (nowUs - fsUs < (unsigned long long)kSyncSettleMs * 1000ull) return 0;
    // v8.4.13: NO BASELINE -- the error IS the offset. A nonzero settled
    // offset was startup skew, and startup skew is drift to be corrected,
    // not held.
    const long long err = off;   // + = this leg is LATE vs leg1
    // NO RE-LATCH anywhere below (v8.4.12/8.4.13). A jump (a stall, an
    // eviction batch, a hiccup) is GROUND BACK like any other error --
    // accepting it was the ratchet that parked raw2 at 36, then 62, then
    // the ring cap, then parked it at raw1 + bloat via the settle latch.
    // The master's depth guard owns common-mode ring growth; this loop
    // owns the difference; nothing is ever blessed.
    if (err >= (long long)kSyncDeadBandFr) {          // LATE: drop (nudge v8.4.9, warden bite v8.4.11)
        // THE WARDEN BITE: past 5 ms of drift, take a 240-frame bite in one
        // discard -- at most once per 2 s (the old version's drift
        // correction, from the shipped ini: drift_correct_ms = 5,
        // drift_interval_ms = 2000). The wide nudge keeps ticking between
        // bites, so the total capacity is ~272 fr/s and a 36 ms creep is
        // gone in seconds -- visibly, in the log.
        if (err >= (long long)kDriftCorrectFr && nowMs - L.lastBiteMs >= kDriftIntervalMs) {
            if (L.raw.discardOldest((size_t)kDriftCorrectFr)) {
                L.syncDrops++;
                L.wardenDrops++;
                L.lastBiteMs = nowMs;
                if (nowMs - L.lastSyncLogMs >= 10000) {
                    L.lastSyncLogMs = nowMs;
                    tlog("[twinspeak] sync out%d     : %lld fr late vs out1 -- the warden bit %d frames\n"
                         "                            (%llu bite(s), %llu total dropped; the old drift correction:\n"
                         "                            %d fr / %u ms, creep corrected, never accepted)\n",
                         idx + 1, err, kDriftCorrectFr,
                         (unsigned long long)L.wardenDrops, (unsigned long long)L.syncDrops,
                         kDriftCorrectFr, kDriftIntervalMs);
                }
            }
            return 0;
        }
        if (L.raw.discardOldest((size_t)kSyncNudgeFr)) {
            L.syncDrops++;
            if (nowMs - L.lastSyncLogMs >= 10000) {
                L.lastSyncLogMs = nowMs;
                tlog("[twinspeak] sync out%d     : %lld fr late vs out1 -- dropped %d frames (%llu total; slow sync,\n"
                     "                            %d fr / %u ms max, the 20 Hz @ 0.1%% keep-alive rides on)\n",
                     idx + 1, err, kSyncNudgeFr, (unsigned long long)L.syncDrops, kSyncNudgeFr, kSyncNudgeMs);
            }
        }
        return 0;
    }
    if (err <= -(long long)kSyncDeadBandFr) {         // EARLY: pad (nudge v8.4.9, warden bite v8.4.11)
        // clamp under the block: the pad region must repeat a REAL mixed
        // frame (holdSrc = the last popped frame), never read past it.
        const long long need = -err;
        if (need >= (long long)kDriftCorrectFr && nowMs - L.lastBiteMs >= kDriftIntervalMs) {
            const size_t pad = (frameCount > (size_t)kDriftCorrectFr)
                             ? (size_t)kDriftCorrectFr
                             : (frameCount >= 2) ? frameCount - 1 : 0;
            if (pad) {
                L.syncPads++;
                L.wardenPads++;
                L.lastBiteMs = nowMs;
                if (nowMs - L.lastSyncLogMs >= 10000) {
                    L.lastSyncLogMs = nowMs;
                    tlog("[twinspeak] sync out%d     : %lld fr early vs out1 -- the warden padded %d frames\n"
                         "                            (%llu bite(s), %llu total padded; a held sample, never a hole)\n",
                         idx + 1, err, kDriftCorrectFr,
                         (unsigned long long)L.wardenPads, (unsigned long long)L.syncPads);
                }
            }
            return pad;
        }
        if (frameCount > (size_t)kSyncNudgeFr) return (size_t)kSyncNudgeFr;
        return (frameCount >= 2) ? frameCount - 1 : 0;
    }
    return 0;                                         // inside the dead-band: hold
}

// v8.4.12 THE DEPTH GUARD -- the master's own drift correction (no key, no
// UI row). The servo measures the REAL-backlog difference (r = ring depth
// minus the leg's OWN delay prefill, v8.4.15: both legs delay-subtracted):
// it holds every slave to
// the master's boundary but is BY CONSTRUCTION blind to common-mode ring
// growth -- both seqTails move together, off does not move, and a
// shared-mode capture burst (audiodg starved under load: OBS, a game, a
// browser) permanently added its size to EVERY ring. Nothing in the
// stripped path ever drained depth: small bursts ratcheted the latency up
// burst by burst (the field's "raw2 increasing over time ... to 62 ms"),
// and a load spike big enough pinned the rings at their power-of-two caps
// (8192 fr = 170.7 ms at delay 0 -- the field's "jumps to 162 when I open
// OBS") and kept them there. THE FIX: the master's real backlog is pinned
// to its anchor -- kDepthAnchorFr (10 ms: the capture delivery plus the
// device buffer plus slack) -- past a kDepthDeadbandFr (5 ms) dead-band,
// dropping the OLDEST frames at the warden's exact pacing (the 16 fr /
// 100 ms wide nudge, the 240 fr / 2 s bite, the shipped ini's drift
// correction). The skip is the correction: the leg catches up to the
// freshest capture, the latency returns to the anchor, nothing is
// accepted. The SLAVES ride it for free through the servo: draining the
// master's r1 raises off, the err loop drops each slave by the same
// amount, and off itself never moves -- the legs drain TOGETHER, the
// user's calibration intact. A healthy ring (jitter inside the dead-band)
// is never touched. Master-only by design: the err loop already owns each
// slave's depth relative to the master, and one owner per quantity keeps
// the two corrections from fighting.
static void masterDepthGuard(Leg& L) {
    const unsigned long long nowMs = GetTickCount64();
    if (nowMs - L.lastSyncMs < kSyncNudgeMs) return;   // slow: one look per window (the servo's clock)
    L.lastSyncMs = nowMs;
    const long long over = (long long)L.raw.count()
                         - (long long)L.delayFrames   // real backlog, prefill excluded
                         - (long long)kDepthAnchorFr;
    if (over < (long long)kDepthDeadbandFr) return;    // healthy jitter: hold
    if (over >= (long long)kDriftCorrectFr && nowMs - L.lastBiteMs >= kDriftIntervalMs) {
        if (L.raw.discardOldest((size_t)kDriftCorrectFr)) {
            L.depthDrops += kDriftCorrectFr;
            L.lastBiteMs = nowMs;
            if (nowMs - L.lastSyncLogMs >= 10000) {
                L.lastSyncLogMs = nowMs;
                tlog("[twinspeak] depth out1    : ring %lld fr over its anchor -- the guard bit %d frames\n"
                     "                            (%llu total; a capture burst's bloat is drained, never kept;\n"
                     "                            the %d fr / %u ms warden pacing, the slaves follow through the\n"
                     "                            sync loop)\n",
                     over, kDriftCorrectFr, (unsigned long long)L.depthDrops,
                     kDriftCorrectFr, kDriftIntervalMs);
            }
        }
        return;
    }
    if (L.raw.discardOldest((size_t)kSyncNudgeFr)) {
        L.depthDrops += kSyncNudgeFr;
        if (nowMs - L.lastSyncLogMs >= 10000) {
            L.lastSyncLogMs = nowMs;
            tlog("[twinspeak] depth out1    : ring %lld fr over its anchor -- dropped %d frames (%llu total;\n"
                 "                            the 16 fr / %u ms wide nudge, the skip is the correction)\n",
                 over, kSyncNudgeFr, (unsigned long long)L.depthDrops, kSyncNudgeMs);
        }
    }
}

// v8.1.0 forward declarations: the stripped switch sits at the TOP of the
// playback section, above the mixer / tick / EQ definitions it calls (the
// pumps that used to separate them are gone). Definitions stay where they
// were; only their names are promised here.
static void mixBlock(int idx, const float* sc, float* chunk, size_t n);
static void tickSum(int idx, float* chunk, size_t n, unsigned long long seqBefore);
static void centerEqFlushZ();                    // defined next to g_ceq
static void centerGuard(Leg& L, float* sc, size_t n, bool fold);   // v6.1 gates (v8.4.1: fold = the leg1 center-fold variant)
static bool bitZero8(const float* p, size_t n);         // v6.6.9, defined below
static void landReopenedRing(int idx);           // v8.4.14: the deferred reopen landing (defined in the reopen section)
static std::atomic<bool> g_tickOn{false};        // moved up from the tick block (v8.1.0)

// v8.1.0 playback core -- THE STRIPPED SWITCH, bit-exact, ONE code path for
// both backends (WASAPI callback and KS pacing thread both land here):
// pop the leg's raw fan-out ring DIRECTLY (the pumps and the out rings are
// GONE), mix INLINE, and play SILENCE for any shortfall -- no replay tile,
// no miss history, no escalation, no hold-off, no gated drops. The DAC hears
// exactly what capture delivered, at the depth delay_ms set. A short switch
// counts into 'und'; capture excess evicts oldest at the raw cap
// (capCallback's pushOverwrite, ledger-true). frameCount = the REAL device
// bite (period or KS bite).
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
    // v8.4.14 THE DEFERRED REOPEN LANDING: a reopened leg's NEW device
    // thread trims its ring HERE, at its first switch -- the discard and
    // the first pop are one instant apart, so the capture cannot refill
    // the vacuum the way it did when the landing ran at reopen time (the
    // field's "second ding raised raw2 to 43"). See landReopenedRing.
    if (L.relandPending) {
        L.relandPending = false;
        landReopenedRing(idx);
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

    // ---- the switch: raw -> master volume -> gates -> mix -> out, INLINE ---
    // The hole default is silence; whatever raw holds lands on top of it.
    // The universal keep-alive sums AFTER this at the device layer (v8.4.5),
    // so even a starved leg keeps its wobble through a gap.
    if (frameCount)
        memset(out, 0, (size_t)frameCount * L.outCh * sizeof(float));
    float* sc = L.scratch.data();
    size_t done = 0;
    // v8.4.12 THE DEPTH GUARD: the master's real backlog is pinned to its
    // anchor (the slaves follow through the servo's err loop -- off IS the
    // real-backlog difference). Then v8.4.7 SLOW SYNC: out2/out3 servo to
    // leg1's play boundary. A drop happened inside one of the two guards
    // (the oldest frames are gone); a PAD is applied below -- this switch
    // deliberately pops padFr frames less and holds its last mixed frame
    // across the pad region (v8.4.9: up to kSyncNudgeFr frames). Leg1 is
    // never SERVOED (it is the master) but its DEPTH is guarded.
    if (idx == 0) masterDepthGuard(L);
    const size_t padFr = (idx != 0) ? syncToFirst(idx, L, frameCount) : 0;
    // v8.4.4/v8.4.5 THE ABSOLUTE LANE GATE: the whole-block lane verdict,
    // per leg -- the center reads ch3 (idx==1); v8.4.11: the ONE rear leg
    // (idx==2) reads FOUR lanes, each BY ITSELF -- rear L ch7 and side L
    // ch5 gate the LEFT output, rear R ch8 and side R ch6 gate the RIGHT
    // output; the mains (idx==0) carry no gate. Every popped chunk ANDs
    // its own verdicts in; a starved block (done < frameCount at the end)
    // never reaches the mute -- the wobble through a capture gap is the
    // keep-alive's own job, not lane content. (The old leg4's lanes
    // retired with the merge: one device carries both rears.)
    const int gateLane  = (idx == 1) ? 2 : (idx == 2) ? kRearLCh : -1;
    const int gateLane2 = (idx == 2) ? kSideLCh : -1;   // v8.4.6: the side fold's lane (left output)
    const int gateLane3 = (idx == 2) ? kRearRCh : -1;   // v8.4.11: rear R's own lane (right output)
    const int gateLane4 = (idx == 2) ? kSideRCh : -1;   // v8.4.11: the side fold's lane (right output)
    bool laneZeroBlock  = true;    // the leg's own lane verdict (ch3 / rear L ch7)
    bool lane2ZeroBlock = true;    // the side fold's lane verdict (side L ch5)
    bool lane3ZeroBlock = true;    // v8.4.11: rear R's own verdict (ch8)
    bool lane4ZeroBlock = true;    // v8.4.11: side R's own verdict (ch6)
    while (done + padFr < frameCount) {
        const size_t want = frameCount - padFr - done;
        const size_t take = (want < (size_t)kScratchFrames) ? want : (size_t)kScratchFrames;
        const size_t avail = L.raw.count();
        const size_t n = (avail < take) ? avail : take;
        if (n == 0) break;
        const unsigned long long seqBefore = L.raw.seqTail.load(std::memory_order_relaxed);
        L.raw.pop(sc, n);
        // v3.8 VOLUME FOLLOW: one master gain -- the Line 1 endpoint volume
        // -- applied to the raw 8ch block BEFORE the per-leg mix. Both legs
        // track the same knob; the ini gains still stack on it.
        {
            const float vg = g_volMuted.load(std::memory_order_relaxed) ? 0.0f
                           : g_vol.load(std::memory_order_relaxed);
            if (vg != 1.0f) {
                const size_t tot = n * (size_t)kInCh;
                for (size_t s = 0; s < tot; ++s) sc[s] *= vg;
            }
        }
        // v8.4.4/v8.4.5: the chunk's lane verdict feeds the absolute gate;
        // the guard itself is unchanged below (it classifies NON-zero
        // blocks -- a zero chunk has nothing to fingerprint). v8.4.6: the
        // side lane carries its OWN verdict -- one lane going zero never
        // touches the other's evidence.
        bool zLane = false, zLane2 = false, zLane3 = false, zLane4 = false;
        if (gateLane >= 0) {
            zLane = true;
            for (size_t i = 0; i < n; ++i)
                if (sc[i * kInCh + gateLane] != 0.0f) { zLane = false; break; }
            laneZeroBlock = laneZeroBlock && zLane;
            if (gateLane2 >= 0) {
                zLane2 = true;
                for (size_t i = 0; i < n; ++i)
                    if (sc[i * kInCh + gateLane2] != 0.0f) { zLane2 = false; break; }
                lane2ZeroBlock = lane2ZeroBlock && zLane2;
            }
            if (gateLane3 >= 0) {
                zLane3 = true;
                for (size_t i = 0; i < n; ++i)
                    if (sc[i * kInCh + gateLane3] != 0.0f) { zLane3 = false; break; }
                lane3ZeroBlock = lane3ZeroBlock && zLane3;
            }
            if (gateLane4 >= 0) {
                zLane4 = true;
                for (size_t i = 0; i < n; ++i)
                    if (sc[i * kInCh + gateLane4] != 0.0f) { zLane4 = false; break; }
                lane4ZeroBlock = lane4ZeroBlock && zLane4;
            }
        }
        if (idx == 1)
            centerGuard(L, sc, n, false);   // v6.1: center presence gates
        // v8.4.1 THE CENTER FOLD: leg2 empty -> the SAME gates protect the
        // fold. The guard fades sc's ch3 IN PLACE, so mixBlock's fold term
        // reads the guarded value -- a stereo upmix's phantom FC (a*FL+b*FR)
        // can never double onto the fronts. The state lives on g_leg[0],
        // fully separate from a live leg2's.
        else if (idx == 0 && g_centerFold) centerGuard(L, sc, n, true);
        // v6.6.9: silence compression -- a bit-zero block needs no mix
        // (see the start-frozen gate): same bytes out, none of the FP work.
        float* dst = out + done * L.outCh;
        // v8.4.4/v8.4.5: an all-zero lane chunk skips the mix outright
        // (bit-identical: the center mix is gain_center * ch3 ONLY) and the
        // center resets its EQ -- the gate's contract, in code. v8.4.6: a
        // lone zero lane's term is a bit-exact no-op in the sum. v8.4.11:
        // the merged rear chunk skips only when ALL FOUR of its lanes are
        // all-zero (a live side plays through a silent rear on the same
        // output; a live right plays through a silent left).
        if ((L.zeroFast && bitZero8(sc, n)) ||
            (gateLane >= 0 && zLane && (gateLane2 < 0 || zLane2) &&
             (gateLane3 < 0 || zLane3) && (gateLane4 < 0 || zLane4))) {
            if (idx == 1) centerEqFlushZ();
            // dst is already silence
        } else {
            mixBlock(idx, sc, dst, n);         // v6.5: the bulk mix, inline now
        }
        if (g_tickOn.load(std::memory_order_relaxed))
            tickSum(idx, dst, n, seqBefore);   // v7.0.0: the sync tick, both legs
        // v6.4: advance the play boundary by the REAL frames just played.
        // The delay prefill (pushSilence) carries no sequence -- it is
        // subtracted here exactly once per frame, so the boundary lives in
        // true capture-sequence space and the drift math stays absolute.
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
        done += n;
    }
    // v8.4.7 THE SYNC PAD, v8.4.9 THE WIDE NUDGE: this switch deliberately
    // popped padApplied frames less -- the pad region HOLDS the last mixed
    // frame (a held sample: the DAC receives its full frameCount, the
    // boundary stalls by padApplied frames, the wobble rides the hold like
    // any frame; at padApplied = 1 this is byte-for-byte the v8.4.7 pad).
    // A starved switch abandons its pad (the shortfall already stalled
    // the boundary).
    const size_t padApplied = (padFr && done + padFr >= frameCount) ? padFr : 0;
    if (padApplied) {
        const float* holdSrc = out + (frameCount - padApplied - 1) * L.outCh;
        for (size_t i = 0; i < padApplied; ++i)
            memcpy(out + (frameCount - padApplied + i) * L.outCh, holdSrc,
                   L.outCh * sizeof(float));
        L.syncPads++;
        const unsigned long long nowMs = GetTickCount64();
        if (nowMs - L.lastSyncLogMs >= 10000) {
            L.lastSyncLogMs = nowMs;
            tlog("[twinspeak] sync out%d     : running early vs out1 -- padded %d frames (%llu total; slow sync,\n"
                 "                            %d fr / %u ms max, a held sample, never a hole)\n",
                 idx + 1, kSyncNudgeFr, L.syncPads, kSyncNudgeFr, kSyncNudgeMs);
        }
    }
    // v8.1.0 SHORTFALL RULE: the hole plays silence, counted -- never a
    // restart, never an escalation. (v8.4.7: the sync pad's frame is not a
    // hole -- it repeated, above; only real holes count.)
    if (done + padApplied < frameCount)
        L.und.fetch_add(1);
    // v8.4.4/v8.4.5 THE ABSOLUTE LANE GATE, the device-layer verdict: a
    // FULLY served block whose every lane sample was digital zero MUTES
    // THAT LANE'S TERM (the universal keep-alive still rides on -- a gated
    // leg is program-silent, never device-silent, v8.4.5). State changes
    // are logged per lane: if a speaker still mixes while the log says
    // MUTED, the leak is below the engine and this line is the evidence.
    // v8.4.6: every lane latches and logs BY ITSELF -- the center keeps
    // its v8.4.5 lines verbatim; a rear leg's own channel says "term"
    // (the side fold may still carry the leg), and the side lanes get
    // their own 'side L gate' / 'side R gate' lines on their own latch
    // and their own rate limit.
    if (gateLane >= 0) {
        // v8.4.7: a padded switch is fully served too -- every output frame
        // carries gated content (the pad repeats an already-gated frame).
        const bool full = (done + padApplied >= frameCount);
        L.gateMuted = full && laneZeroBlock;
        if (L.gateMuted != L.gateWasMuted) {
            L.gateWasMuted = L.gateMuted;
            const unsigned long long nowMs = GetTickCount64();
            if (nowMs - L.lastGateLogMs >= 2000) {
                L.lastGateLogMs = nowMs;
                static const char* kGateName[4] = { "", "center gate", "rear L gate", "rear R gate" };
                static const char* kGateLane[4] = { "", "ch3", "ch7", "ch8" };
                static const char* kGateEq[4]   = { "", ", EQ reset", "", "" };
                if (L.gateMuted) {
                    if (idx == 1)
                        tlog("[twinspeak] %s : %s is digital zero -- THE MIX IS MUTED (mix skipped%s)\n"
                             "                            (v8.4.5: the 20 Hz @ 0.1%% keep-alive rides on -- program silence,\n"
                             "                            the amp stays fed; no fade, no hysteresis, no ini switch)\n",
                             kGateName[idx], kGateLane[idx], kGateEq[idx]);
                    else
                        tlog("[twinspeak] %s : %s is digital zero -- its term is muted on out%d's LEFT output (the\n"
                             "                            other lanes still play if they are live; the mix skips only when ALL\n"
                             "                            FOUR lanes are zero; the 20 Hz @ 0.1%% keep-alive rides on -- program\n"
                             "                            silence, v8.4.11: one rear device carries both rears)\n",
                             kGateName[idx], kGateLane[idx], idx + 1);
                } else {
                    if (idx == 1)
                        tlog("[twinspeak] %s : %s carries samples again -- the mix resumes (the wobble never left)\n",
                             kGateName[idx], kGateLane[idx]);
                    else
                        tlog("[twinspeak] %s : %s carries samples again -- its term resumes on out%d's LEFT output (the wobble never left)\n",
                             kGateName[idx], kGateLane[idx], idx + 1);
                }
            }
        }
        // v8.4.6 THE SIDE GATE: the fold's lane, on its own latch and its
        // own rate limit -- one lane's state change never swallows the
        // other lane's evidence. v8.4.11: the merged rear carries the same
        // four-lane contract on ONE device -- rear L / side L latch above
        // and here, rear R / side R below.
        if (gateLane2 >= 0) {
            L.gateMuted2 = full && lane2ZeroBlock;
            if (L.gateMuted2 != L.gateWasMuted2) {
                L.gateWasMuted2 = L.gateMuted2;
                const unsigned long long nowMs = GetTickCount64();
                if (nowMs - L.lastSideLogMs >= 2000) {
                    L.lastSideLogMs = nowMs;
                    static const char* kSideName[4] = { "", "", "side L gate", "" };
                    static const char* kSideLane[4] = { "", "", "ch5", "" };
                    if (L.gateMuted2)
                        tlog("[twinspeak] %s : %s is digital zero -- the side fold's term is muted on out%d's LEFT\n"
                             "                            output (the rear lane still plays if it is live; the mix skips only\n"
                             "                            when ALL FOUR lanes are zero; the 20 Hz @ 0.1%% keep-alive rides on\n"
                             "                            -- program silence)\n",
                             kSideName[idx], kSideLane[idx], idx + 1);
                    else
                        tlog("[twinspeak] %s : %s carries samples again -- the side fold resumes on out%d's LEFT output (the wobble never left)\n",
                             kSideName[idx], kSideLane[idx], idx + 1);
                }
            }
        }
        // v8.4.11 THE RIGHT LANES of the merged rear: rear R (ch8) and its
        // side fold (ch6), each on its own latch and its own rate limit --
        // the v8.4.6 contract, verbatim, for the right output.
        if (gateLane3 >= 0) {
            L.gateMuted3 = full && lane3ZeroBlock;
            if (L.gateMuted3 != L.gateWasMuted3) {
                L.gateWasMuted3 = L.gateMuted3;
                const unsigned long long nowMs = GetTickCount64();
                if (nowMs - L.lastSide2LogMs >= 2000) {
                    L.lastSide2LogMs = nowMs;
                    if (L.gateMuted3)
                        tlog("[twinspeak] rear R gate : ch8 is digital zero -- its term is muted on out%d's RIGHT output (the\n"
                             "                            other lanes still play if they are live; the mix skips only when ALL\n"
                             "                            FOUR lanes are zero; the 20 Hz @ 0.1%% keep-alive rides on -- program\n"
                             "                            silence)\n",
                             idx + 1);
                    else
                        tlog("[twinspeak] rear R gate : ch8 carries samples again -- its term resumes on out%d's RIGHT output (the wobble never left)\n",
                             idx + 1);
                }
            }
        }
        if (gateLane4 >= 0) {
            L.gateMuted4 = full && lane4ZeroBlock;
            if (L.gateMuted4 != L.gateWasMuted4) {
                L.gateWasMuted4 = L.gateMuted4;
                const unsigned long long nowMs = GetTickCount64();
                if (nowMs - L.lastSide3LogMs >= 2000) {
                    L.lastSide3LogMs = nowMs;
                    if (L.gateMuted4)
                        tlog("[twinspeak] side R gate : ch6 is digital zero -- the side fold's term is muted on out%d's RIGHT\n"
                             "                            output (the rear lane still plays if it is live; the mix skips only\n"
                             "                            when ALL FOUR lanes are zero; the 20 Hz @ 0.1%% keep-alive rides on\n"
                             "                            -- program silence)\n",
                             idx + 1);
                    else
                        tlog("[twinspeak] side R gate : ch6 carries samples again -- the side fold resumes on out%d's RIGHT output (the wobble never left)\n",
                             idx + 1);
                }
            }
        }
    }

    // the last played frames -- the KS lapse guard's re-prime source (v8.1.0
    // note: the replay path itself is gone; this is the only consumer left).
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

// v6.3/v8.4.5: the device-layer extras -- the UNIVERSAL keep-alive tone on
// every leg, plus leg2's chime -- as their own function so the KS pump can
// sum them exactly like the playCallbacks do.
static void sumLegExtras(int idx, float* out, ma_uint32 frameCount) {
    // v8.4.5 THE UNIVERSAL KEEP-ALIVE: 20 Hz @ 0.1% rides EVERY leg, every
    // block -- through the lane gates (a gated block is PROGRAM silence,
    // never device silence) and through the stop gap (bridging gaps is the
    // wobble's whole job). Phase is PER LEG: each device thread owns its
    // own index into the shared read-only table, no atomics. leg1 (the 6ch
    // stream) carries it on FL/FR only -- the FC/LFE/rear slots stay
    // driven silence, the upmixer-exclusion contract. The chime stays
    // out2's alone (the user's own test, not ambient content): only idx==1
    // loads g_chimeSel, so g_chimeIdx keeps its single owner.
    int chSel = (idx == 1) ? g_chimeSel.load(std::memory_order_acquire) : -1;
    const int och = (idx == 0) ? kOutCh1 : 2;   // leg1: the 5.1 stride; legs 2-4: stereo
    // v6.5: with no chime armed the tone is summed in wrap-free RUNS over
    // the table -- same sample sequence and clamps, bit-exact
    // (harness-proven).
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
        const float s = g_kaTable[g_kaIdxLeg[idx]];   // the wobble rides every leg, chime or not
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

static void playCore(int idx, float* out, ma_uint32 frameCount) {
    boostDeviceThread();
    // v7.9.0: DEVICE-SWITCH LATENESS METER -- the ground truth the ring
    // cannot see. The event fires with one period still to play, so a
    // service interval beyond period + 1 ms means this thread was scheduled
    // late and the DAC is into its buffer margin; beyond 2 periods it has
    // already crossed into stale audio (device-side static). Logged
    // rate-limited; counted for the record.
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
                         "                            -- %.2f ms of device-buffer margin left%s (%llu late switch(es) since boot)\n",
                         idx + 1, gapMs, periodMs, 2.0 * periodMs - gapMs,
                         gapMs > 2.0 * periodMs
                             ? " -- the DAC crossed into stale audio"
                             : "",
                         (unsigned long long)L.lateSwitches.load(std::memory_order_relaxed));
                }
            }
        }
        L.lastSwQpc = qn;
        L.haveSwQpc = true;
    }
    hbOut(idx);   // v3.8 watchdog heartbeat
    bufferSwitchFill(idx, out, (size_t)frameCount);
}

static void playCallback1(ma_device*, void* pOutput, const void*, ma_uint32 frameCount) {
    playCore(0, (float*)pOutput, frameCount);
    sumLegExtras(0, (float*)pOutput, frameCount);   // v8.4.5: the universal wobble, stop gap included
}

static void playCallback2(ma_device*, void* pOutput, const void*, ma_uint32 frameCount) {
    playCore(1, (float*)pOutput, frameCount);
    sumLegExtras(1, (float*)pOutput, frameCount);   // v8.4.5: the wobble + the chime, through the stop gap
}
// v8.4.0: the rear legs -- 2ch switches. v8.4.5: they sum the universal
// keep-alive like every leg (no chime -- that stays out2's own test).
static void playCallback3(ma_device*, void* pOutput, const void*, ma_uint32 frameCount) {
    playCore(2, (float*)pOutput, frameCount);
    sumLegExtras(2, (float*)pOutput, frameCount);
}
static void playCallback4(ma_device*, void* pOutput, const void*, ma_uint32 frameCount) {
    playCore(3, (float*)pOutput, frameCount);
    sumLegExtras(3, (float*)pOutput, frameCount);
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

// v8.1.0: the stripped switch calls this when a bit-zero block skips the
// mix -- the center EQ's filter state must not carry across silence.
static void centerEqFlushZ()
{
    if (g_ceq.on) { g_ceq.z1 = 0.0f; g_ceq.z2 = 0.0f; }
}

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
// off) so the mix is bit-identical to v6.4 (harness-proven). v8.4.0: the
// discrete-LFE branch is RETIRED with the lfe_fold toggle -- the fold (the
// confirmed routing) is the only path and its FP ops are untouched.
// v8.4.0 THE REAR LEGS + v8.4.6 THE SIDE FOLD; v8.4.11 THE ONE REAR LEG:
// out1's 5.1 side slots are DRIVEN SILENCE -- the cable's rear pair (ch7 RL
// / ch8 RR of the VAC 8ch map: 1L 2R 3C 4LFE 5SL 6SR 7RL 8RR) plays on ONE
// stereo leg: leg3's LEFT output = gain_rl * ch7, its RIGHT output =
// gain_rr * ch8, the leg2 pattern (one source per output). v8.4.6: the
// SIDES (ch5 SL / ch6 SR -- the slots a 5.1 movie's surround lands on)
// FOLD onto their same-side rear OUTPUT at that side's gain: out L =
// gain_rl*(ch7 + ch5), out R = gain_rr*(ch8 + ch6); out1's side slots stay
// hard 0.0f so the sides never play anywhere but the rears. A rear leg
// with an EMPTY device never runs -- empty in = empty out (stereo content
// plays with silent rears; that is the truth), and an empty leg3 takes its
// side fold down with it. A 5.1 movie (surround on ch5/ch6) plays its
// surround on the rear speakers natively now.
static void mixBlock(int idx, const float* sc, float* chunk, size_t n) {
    if (idx == 0) {
        // leg1 = NATIVE 5.1 stream. FC is DRIVEN SILENCE: the endpoint sees a
        // full-layout 6ch stream, so neither miniaudio nor the Windows engine
        // may run a stereo->5.1 upmix -- the fake FC = 0.5L+0.5R smear is gone.
        // v8.4.1 THE CENTER FOLD: when leg2 has NO device (output_device2
        // EMPTY) the center must not vanish -- ch3 folds onto FL/FR at
        // gain_center, the LFE fold's pattern. Two loops, not an in-loop
        // branch: the no-fold loop is the v6.5 code VERBATIM (identical FP
        // op sequence -- bit-identical output when the fold is off), and the
        // fold loop only APPENDS the center term after the existing sums:
        // ((gL*f[0]) + lfe) + cf -- the FL/FR ops themselves are unchanged.
        // The FC slot stays driven silence even folded: the center CONTENT
        // rides the fronts (the jack set is FL/FR + sub; a nonzero FC would
        // feed a jack nothing is wired to), and the full 6ch layout keeps
        // every upmixer out.
        const float gL = g_cfg.gainL, gR = g_cfg.gainR, gLfe = g_cfg.gainLfe;
        if (g_centerFold) {
            const float gC = g_cfg.gainCenter;
            for (size_t i = 0; i < n; ++i) {
                const float* f = sc + i * kInCh;
                float* o = chunk + i * kOutCh1;
                const float lfe = gLfe * f[3];
                const float cf  = gC * f[2];            // the center fold (leg2 empty)
                o[0] = gL * f[0] + lfe + cf;            // FL + the center fold
                o[1] = gR * f[1] + lfe + cf;            // FR + the center fold
                o[2] = 0.0f;                            // FC -- driven silence even folded (the content rides the fronts)
                o[3] = 0.0f;                            // LFE -- folded into the fronts
                o[4] = 0.0f;                            // out ch5 (SL) -- hard zero: the side folds onto leg3, never plays here (v8.4.6)
                o[5] = 0.0f;                            // out ch6 (SR) -- hard zero: the side folds onto leg4, never plays here (v8.4.6)
            }
        } else {
            for (size_t i = 0; i < n; ++i) {
                const float* f = sc + i * kInCh;
                float* o = chunk + i * kOutCh1;
                const float lfe = gLfe * f[3];
                o[0] = gL * f[0] + lfe;                     // FL
                o[1] = gR * f[1] + lfe;                     // FR
                o[2] = 0.0f;                                // FC -- silent on purpose (center lives on leg2)
                o[3] = 0.0f;                                // LFE -- folded into the fronts
                o[4] = 0.0f;                                // out ch5 (SL) -- hard zero: the side folds onto leg3, never plays here (v8.4.6)
                o[5] = 0.0f;                                // out ch6 (SR) -- hard zero: the side folds onto leg4, never plays here (v8.4.6)
            }
        }
    } else if (idx == 1) {
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
    } else {
        // v8.4.0 THE REAR LEGS; v8.4.11 THE ONE REAR LEG: ONE stereo device
        // carries both rears -- the leg2 pattern minus the fingerprint
        // guard and the chime (a rear has no upmix fingerprint to guard;
        // v8.4.5: the lane gates and the universal keep-alive ride it from
        // the switch/device layers). The gain multiplies are the ONLY ops:
        // g * 0 = 0, so a silent cable channel stays bit-zero silence, and
        // a gated lane contributes exactly zero to the sum. v8.4.6 THE
        // SIDE FOLD: each output's SAME-SIDE side channel appends AFTER
        // the rear term, riding that side's gain (one calibrated knob per
        // speaker): out L = gain_rl*ch7 + gain_rl*ch5, out R = gain_rr*ch8
        // + gain_rr*ch6. g*0 = 0 and x + 0 = x, so a silent side lane
        // leaves the bytes bit-identical; the switch layer skips the whole
        // mix only when ALL FOUR lanes are all-zero (each lane's gate is
        // its own, above).
        const float gRL = g_cfg.gainRl, gRR = g_cfg.gainRr;
        for (size_t i = 0; i < n; ++i) {
            const float* f = sc + i * kInCh;
            chunk[i * 2 + 0] = gRL * f[kRearLCh] + gRL * f[kSideLCh];   // rear L ch7 + side L ch5
            chunk[i * 2 + 1] = gRR * f[kRearRCh] + gRR * f[kSideRCh];   // rear R ch8 + side R ch6
        }
    }
}

// ------------------------------------------------- v6.1 center gates -------
// CENTER PRESENCE on out2 -- two gates, both default ON:
//   1) v8.4.4/v8.4.5 THE ABSOLUTE LANE GATE (bufferSwitchFill): the lane
//      digital zero for the whole block -> THAT LANE'S TERM IS MUTED --
//      mix skipped (the center resets its EQ too); v8.4.5: the universal
//      keep-alive wobble rides on (a gated leg is program-silent only).
//      v8.4.6: the rears carry two lanes each (rear channel + side fold),
//      gated each BY ITSELF; the center keeps its single ch3 lane and its
//      v8.4.5 behavior verbatim.
//   2) center_upmix_guard: a stereo app cannot write center to the 8ch
//      cable -- the Windows engine UPmixES it first, FC = a*FL + b*R. The
//      fingerprint is exact: per block, least-squares fit ch3 ~ a*ch1 +
//      b*ch2; residual < -30 dB with the gains inside the upmix band and
//      the fronts active -> the block is engine upmix, not center -> muted.
//      Genuine 5.1 dialogue is an INDEPENDENT track and always passes.
//      v8.4.10: the verdict is INSTANT (field-directed: "the center gate
//      to just play the moment the input frame is detected to have it,
//      which means before you play the output data") -- a HIT mutes the
//      chunk it was measured on, a MISS plays it, and ch3 alive against
//      fronts too quiet to fit (a*0 + b*0 = 0) is a definitive real
//      center, released on the spot. The v7.8.4 2-hit/8-miss hysteresis
//      retired with the field's diagnosis: it left the gate whole buffers
//      behind the evidence and the late verdict chopped the center at
//      every content transition (the chattering). v8.4.11: the 2.5 ms
//      fade is RETIRED TOO (field-directed, deepseek-confirmed: "the
//      center upmix is still leaking because it has a 2.5ms fade whenever
//      it should've just rendered the zeroing out immediately") -- the
//      fade WAS the leak: every engage let ~120 frames of a*FL+b*FR
//      through at ramping gain. A HIT's chunk is ZEROED immediately, a
//      MISS's chunk plays immediately; no ramp, no anti-click compromise.
// v8.4.1: fold = THE CENTER FOLD variant -- the same two gates run on
// LEG1's pump and guard the FL/FR fold instead of out2's mix. The state
// (hysteresis, fade, counters) lives on the caller's Leg (g_leg[0]), fully
// separate from a live leg2's, and the logs say 'out1 fold' so the two
// paths never blur in the pane.
static void centerGuard(Leg& L, float* sc, size_t n, bool fold) {
    // ---- gate 1: ch3 digital zero for the whole block ----------------------
    bool allZero = true;
    for (size_t i = 0; i < n; ++i) {
        if (sc[i * kInCh + 2] != 0.0f) { allZero = false; break; }
    }
    if (allZero) {
        // v8.4.4: a zero block is THE ABSOLUTE CENTER GATE's territory now
        // (bufferSwitchFill mutes the device, wobble included, and logs the
        // state change itself) -- here there is nothing left to classify.
        return;                      // nothing left to classify on a silent block
    }

    // ---- gate 2: engine upmix fingerprint (v8.4.10 INSTANT, v8.4.11 ZEROED
    // ON THE SPOT) ----------------------------------------------------------
    // The verdict is the CHUNK's own evidence, applied to that chunk before
    // it is mixed (centerGuard runs ahead of mixBlock in the switch): a HIT
    // mutes this chunk, a MISS plays it. The v7.8.4 2-hit/8-miss hysteresis
    // retired with the field's diagnosis ("the absolute center gate is
    // being delayed by exactly one buffer frame") -- it left the verdict
    // whole buffers behind the evidence and the late gate chopped the
    // center at every content transition. v8.4.11 retires the 2.5 ms fade
    // with them: the mute is a hard zero of this chunk, the pass is a hard
    // pass -- the ramp WAS the leak.
    if (!g_cfg.centerUpmixGuard) return;
    // v8.4.4: the fit runs at EVERY bite size. The old n >= 64 floor
    // silently disabled the guard the day v8.4.3 made the default bite 32
    // frames (and a sub-64 WASAPI period had never run it either) -- a
    // stereo source's upmix phantom played through untouched. 16 = the
    // clamp floor (v8.4.11: the fade that "absorbed" the noisier
    // short-window fit is gone with it).
    if (n >= 16) {
        // full test (the fit is verbatim v6.1)
        double S11 = 0, S22 = 0, S12 = 0, S31 = 0, S32 = 0, S33 = 0;
        for (size_t i = 0; i < n; ++i) {
            const float* f = sc + i * kInCh;
            const double x = f[0], y = f[1], z = f[2];
            S11 += x * x; S22 += y * y; S12 += x * y;
            S31 += z * x; S32 += z * y; S33 += z * z;
        }
        const double nD = (double)n;
        const double ms1 = S11 / nD, ms2 = S22 / nD, ms3 = S33 / nD;
        if (ms3 >= 1e-8 && 0.5 * (ms1 + ms2) >= 4e-8) {
            double a = 0.0, b = 0.0, res2 = 0.0;
            bool outside = false;                    // definitive real center
            const double det = S11 * S22 - S12 * S12;
            if (det > 1e-6 * (S11 * S22 + 1e-30)) {
                a = (S31 * S22 - S32 * S12) / det;
                b = (S32 * S11 - S31 * S12) / det;
                res2 = S33 - a * S31 - b * S32;
                if (a < 0.05 || a > 2.0 || b < 0.05 || b > 2.0) outside = true;
            } else {
                // collinear fronts (mono content): 1-D fit on the dominant front
                const bool useCh1 = (S11 >= S22);
                const double Suu = useCh1 ? S11 : S22;
                const double S3u = useCh1 ? S31 : S32;
                if (Suu > 0.0) {
                    a = S3u / Suu;
                    res2 = S33 - a * S3u;
                    if (a < 0.05 || a > 2.0) outside = true;
                } else outside = true;
            }
            if (res2 < 0.0) res2 = 0.0;  // float noise below the fit
            if (!outside && res2 / S33 < 1e-3) {         // HIT: exact upmix fingerprint
                if (!L.cgMuted) {
                    L.cgMuted = true;                    // v8.4.10: engage THIS chunk (v8.4.11: zeroed on the spot)
                    L.cmUpmix.fetch_add(1, std::memory_order_relaxed);
                    const unsigned long long now = GetTickCount64();
                    if (now - L.lastUpmixLogMs >= 5000) {
                        L.lastUpmixLogMs = now;
                        if (fold) tlog("[twinspeak] out1 fold guard: ch3 matches %.2f*ch1 + %.2f*ch2 -- guard ENGAGED\n"
                                       "                            (stereo source: the engine fills FC = a*FL + b*R; the phantom center\n"
                                       "                            is ZEROED immediately -- no fade, no leak)\n",
                                       a, b);
                        else     tlog("[twinspeak] out2 upmix guard: ch3 matches %.2f*ch1 + %.2f*ch2 -- guard ENGAGED\n"
                                      "                            (stereo source: the engine fills FC = a*FL + b*R; ch3 is ZEROED\n"
                                      "                            immediately -- no fade, no leak)\n",
                                      a, b);
                    }
                } else {
                    L.cmUpmix.fetch_add(1, std::memory_order_relaxed);   // stays engaged
                }
            } else {                                     // miss or definitive real center
                if (L.cgMuted) {
                    L.cgMuted = false;                   // v8.4.10: release THIS chunk (v8.4.11: plays on the spot)
                    const unsigned long long now = GetTickCount64();
                    if (now - L.lastUpmixLogMs >= 5000) {
                        L.lastUpmixLogMs = now;
                        if (fold) tlog("[twinspeak] out1 fold guard: independent center content -- guard released\n"
                                       "                            (the real center folds back into the FL/FR immediately)\n");
                        else     tlog("[twinspeak] out2 upmix guard: independent center content -- guard released\n"
                                      "                            (ch3 plays immediately -- the miss carries its own chunk)\n");
                    }
                }                                        // v8.4.10: the miss plays THIS chunk
            }
        } else if (ms3 >= 1e-8) {
            // v8.4.10: ch3 ALIVE against fronts too quiet to fit -- a*0 +
            // b*0 = 0, so this cannot be engine upmix: definitive real
            // center, released on the spot (the old code held whatever
            // verdict it carried -- a dialogue-over-silent-fronts onset
            // stayed muted behind the hysteresis).
            if (L.cgMuted) {
                L.cgMuted = false;
                const unsigned long long now = GetTickCount64();
                if (now - L.lastUpmixLogMs >= 5000) {
                    L.lastUpmixLogMs = now;
                    if (fold) tlog("[twinspeak] out1 fold guard: ch3 alive over silent fronts -- released\n");
                    else     tlog("[twinspeak] out2 upmix guard: ch3 alive over silent fronts -- released\n");
                }
            }
        }
    }
    // v8.4.11: apply the verdict to the chunk IMMEDIATELY -- a muted chunk's
    // ch3 is ZEROED on the spot (every sample, bit-exact zero), a passing
    // chunk is left untouched (no multiply, the bytes pass verbatim). No
    // fade, no ramp: the zeroing is rendered the moment the fingerprint
    // evidence exists, inside the very chunk that produced it.
    if (L.cgMuted) {
        for (size_t i = 0; i < n; ++i)
            sc[i * kInCh + 2] = 0.0f;
        L.cgGain = 0.0f;
    } else {
        L.cgGain = 1.0f;
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
static std::atomic<long long> g_tickSeq0{-1};      // capture-seq of tick beat zero
                                                 // (g_tickOn now lives above the
                                                 // playback core -- v8.1.0)
static std::atomic<unsigned long long> g_tickEndMs{0};   // wall time the 4 s run ends
                                                 // (0 = idle; the GUI timer flips
                                                 // the button back when it passes)
static std::vector<float>     g_tickWave;          // ONE pre-rendered tick (48 kHz mono)
static constexpr size_t kTickPeriod = 24000;       // 500 ms @ 48 kHz = 120 bpm
static constexpr size_t kTickCount  = 8;           // 8 beats = 4 s, then it stops

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
    for (size_t i = 0; i < n; ++i) {
        const long long t = pos + (long long)i;
        if (t >= (long long)(kTickCount * kTickPeriod)) break;
        const size_t ph = (size_t)t % kTickPeriod;
        if (ph >= wn) continue;
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
}

// ----------------------------------------------------------------- pacing --
// v6.5.7: the pacing tick is 500 us -- ONE-SHOT high-resolution timer
// re-armed per cycle (a PERIODIC timer cannot go below 1 ms). v8.1.0 adds
// kTick100ns -- the finest expressible due time, ONE 100 ns tick, as fast
// as a Windows wait can be armed: the KS deadline poll waits on it between
// register reads (polling ALONE -- the register still decides when to
// write). No busy wait anywhere in the engine. The PUMP THREADS are GONE
// in v8.1.0 (the buffer switch mixes inline, straight off the raw ring).
static constexpr long long kTick500us = -5000;   // 0.5 ms, negative = relative (100-ns units)
static constexpr long long kTick100ns = -1;      // one 100-ns tick -- the poll-only wait

// (v8.4.3's sub-128 loop floor -- kKsLoopFloorFrames 768 / lead 512 --
// retired in v8.4.5 with the sub-ms bites it protected: the bite is
// BUILT IN at 192 fr x 3; see kKsPeriodFrames in the config block.)

// ============================================================ v6.3 WDM-KS ===
// A direct kernel-streaming output path -- the FlexASIO protocol, embedded
// and paced harder: filter = KSCATEGORY_RENDER wave filter opened as a file;
// pin = KsCreatePin render sink (KSPIN_CONNECT + WAVEFORMATEXTENSIBLE);
// buffer = the WaveRT looped buffer (driver DMA memory, mapped here); clock
// = the mapped HW play-position register or KSPROPERTY_AUDIO_POSITION;
// pacing = our own TIME-CRITICAL MMCSS thread writing exactly the
// built-in 288-fr bite (v8.4.9) when the write cursor nears the lead. No events, no
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
    // v8.4.9 THE POWER BITE: 288 fr x 2, BUILT IN -- the loop request is
    // simply bite x periods (fewer, larger writes than v8.4.8's 256 x 4:
    // ~167 writes/s; the double cushion retired with the field's own
    // diagnosis -- the L/R stutter was the servo's 1-fr nudge, not the lead).
    const unsigned periodFrames  = kKsPeriodFrames;
    const unsigned loopPeriods   = kKsPeriods;
    const unsigned loopReqFrames = kKsPeriods * kKsPeriodFrames;

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
                                         (size_t)loopReqFrames;
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
    // v8.0.0: the pacer takes AvrtPriorityCritical INSIDE the "Pro Audio"
    // task -- the thread whose deadline IS the DAC's register outranks both
    // pumps (center pump ties it, mains pump loses). boostAudioThread()
    // left it task-default: an equal inside MMCSS round-robin.
    boostAudioThreadEx(2 /*AVRT_PRIORITY_CRITICAL*/);

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
                // v8.4.3: the tolerance scales with the loop -- the fixed
                // 2*bite+96 was degenerate at both ends (96 alone exceeded
                // the whole loop at a 32-frame bite; 8288 exceeded the
                // 8192-frame loop at bite 4096 x 2 periods). The default
                // config keeps its exact v8.4.2 value (608 > 768/4).
                const long long tolFr = std::max< long long >(
                    (long long)2 * periodFr + 96, (long long)K.loopFrames / 4);
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
            // v8.1.0 NO BUSY WAIT (field-directed). The v7.9.0 final stretch
            // busy-waited on the DAC's position register with _mm_pause -- a
            // spinner by design, one burning core per KS leg. It is GONE.
            // The danger zone stays the last full period + 0.5 ms of slack
            // above the write line; inside it the pacer now POLLS ON THE
            // HIGH-RESOLUTION WAITABLE TIMER with the finest expressible due
            // time -- one 100 ns tick, as fast as Windows can wait. The
            // thread BLOCKS between register reads (no core burn, no pause
            // wall, preemptible by DPCs/ISRs); the SetWaitableTimer +
            // WaitForSingleObject pair costs ~1-3 us, so the poll still
            // lands thousands of times inside the zone, and the register it
            // obeys still decides the write -- the timer is for polling
            // ALONE. (No high-res timer pre-1803: Sleep(0) -- yield, never
            // spin.)
            if (slackFr > (long long)periodFr + (long long)(kSampleRate / 2000)) {
                if (timerOK) {
                    due.QuadPart = kTick500us;                    // v6.5.7: re-arm one-shot 500 us
                    SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE);
                    WaitForSingleObject(hTimer, 2);
                } else
                    Sleep(1);
            } else if (timerOK) {
                due.QuadPart = kTick100ns;    // v8.1.0: the 100 ns poll-only wait
                SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(hTimer, 2);
            } else {
                Sleep(0);                     // no high-res timer: yield, never spin
            }
            continue;
        }
        // ---- the buffer switch (identical to the WASAPI device callback) ----
        hbOut(idx);
        bufferSwitchFill(idx, fill, periodFr);
        sumLegExtras(idx, fill, periodFr);   // v8.4.5: the universal wobble, every KS leg
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
        "; twinspeak v8.4.15 config -- BIT-EXACT ASIO-strict engine: out1 = L/R + LFE fold,\n"
        "; out2 = center ONLY, out3 = the rear pair ON ONE DEVICE + the v8.4.6 side\n"
        "; folds (ch5 onto the left output, ch6 onto the right; optional; an EMPTY\n"
        "; device keeps the rears silent), no resampler anywhere.\n"
        "; v8.1.0 THE STRIPPED PATH: no out rings, no pumps, no capture-burst\n"
        "; accounting -- the buffer switch pops the capture fan-out ring\n"
        "; directly and mixes inline; the DAC's own position register paces\n"
        "; every write (100 ns high-resolution timer poll, no busy wait).\n"
        "; cap_ms / ring1_ms / ring2_ms are read-and-ignored.\n"
        "\n"
        "; v8.2.0: out1ms / out2ms (and the legacy buffer_size alias) are\n"
        "; read-and-ignored too -- the WASAPI device period is an engine\n"
        "; constant now (1 ms, hardcoded; a KS leg ignores it whole).\n"
        "; v8.3.0: volume_follow, drift_correct_ms, drift_interval_ms and\n"
        "; deep_resync_min joined them -- hardcoded from the fielded ini\n"
        "; (on / 5 / 5000 / 30). The knobs that are left: the delays + the\n"
        "; rear offsets below, the KS bite, the gains, the EQ and the chime.\n"
        "; v8.4.0: lfe_fold joined them too -- the fold is the confirmed\n"
        "; routing, built in; gain_lfe below still scales it. v8.4.0 also\n"
        "; adds THE REAR LEGS: output_device3, offset3_ms and gain_rl/gain_rr\n"
        "; (an EMPTY rear device keeps the rears silent). v8.4.11: ONE rear\n"
        "; device carries BOTH rears (the old output_device4/offset4_ms are\n"
        "; read-and-ignored -- stale lines load clean).\n"
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
        "offset3_ms = 0\n"
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
        "; not discrete; it lives inside the front channels. v8.4.0: the fold\n"
        "; is BUILT IN -- always on, no key (a stale lfe_fold line in an old\n"
        "; ini loads clean and is ignored); gain_lfe below still scales it.\n"
        "\n"
        "; v8.4.1 THE CENTER FOLD: an EMPTY output_device2 (no center device)\n"
        "; is LEGAL -- leg2 never opens, and the center channel (ch3) FOLDS\n"
        "; onto leg1's FL/FR at gain_center, exactly the LFE fold's pattern:\n"
        "; FL = gain_l*L + gain_lfe*LFE + gain_center*C, FR the mirror. The\n"
        "; center upmix guard rides the fold too -- a stereo source's phantom\n"
        "; FC (a*FL + b*FR, filled in by the Windows engine at the cable's\n"
        "; playback side) is ZEROED out of the fronts immediately (v8.4.11:\n"
        "; no fade -- the zeroing renders the moment the fingerprint is\n"
        "; measured), so it can never double onto FL/FR; genuine 5.1 center\n"
        "; always plays. A\n"
        "; NON-empty output_device2 that matches no device is still a hard\n"
        "; error. The center EQ is out2-only: with the fold active it does\n"
        "; not apply (the fold is a plain gain_center sum).\n"
        "\n"
        "; v8.4.0 THE REAR LEGS + v8.4.6 THE SIDE FOLD; v8.4.11 THE ONE REAR\n"
        "; LEG: ONE stereo device carries the cable's rear pair -- ch7 (RL)\n"
        "; and ch8 (RR) -- with the SIDES folded onto their same-side output:\n"
        "; out L = gain_rl * (ch7 + ch5), out R = gain_rr * (ch8 + ch6) --\n"
        "; the side (ch5 SL / ch6 SR, the slots a 5.1 movie's surround lands\n"
        "; on) rides its same-side rear output at that side's gain, each\n"
        "; lane gated BY ITSELF (see CENTER PRESENCE below); a 7.1 movie\n"
        "; sums sides and backs on each rear output.\n"
        "; offset3_ms stays the leg's bit-exact delay (offset4_ms is retired\n"
        "; with the merge, read-and-ignored). An EMPTY device (as shipped\n"
        "; below) keeps the rears SILENT -- no error, no output: empty in =\n"
        "; empty out, the truth, and an empty leg3 takes its side fold down\n"
        "; with it. out1's 5.1 side slots stay DRIVEN SILENCE, hardcoded --\n"
        "; the sides never play out1, they ride the rears ONLY. A 5.1\n"
        "; movie's surround now plays the rear speakers natively.\n"
        "\n"
        "; Substring match against device names (see: twinspeak --list).\n"
        "; v8.4.0: the rear leg -- output_device3 EMPTY (as shipped) keeps the\n"
        "; rears silent; fill it when the rears get their own device (ONE\n"
        "; device carries both -- v8.4.11).\n"
        "; v8.4.1: output_device2 may stay EMPTY too -- the center then folds\n"
        "; onto leg1's FL/FR at gain_center (see THE CENTER FOLD above).\n"
        "input_device   = Line 1\n"
        "output_device1 = Realtek\n"
        "output_device2 = Xtream\n"
        "output_device3 =\n"
        "\n"
        "; optional mixes (gain_lfe = the LFE fold gain onto FL/FR; 1.0 = a\n"
        ";                    unity fold; gain_rl/gain_rr scale the rear legs\n"
        ";                    AND their v8.4.6 side folds; gain_center scales\n"
        ";                    ch3 -- out2's mix, and the leg1 fold when\n"
        ";                    output_device2 is EMPTY)\n"
        "gain_l = 1.0\n"
        "gain_r = 1.0\n"
        "gain_rl = 1.0\n"
        "gain_rr = 1.0\n"
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
        "; VOLUME (v8.3.0: volume_follow is BUILT IN, hardcoded on): the\n"
        "; Windows volume of the cable's PLAYBACK side (the Line 1 slider in\n"
        "; sndvol, or the volume hotkeys when Line 1 is the default device) is\n"
        "; the single master knob -- twinspeak multiplies every captured frame\n"
        "; by it, so EVERY leg tracks it identically (v8.4.0: all four). The\n"
        "; endpoint is matched by name: volume_endpoint first (substring),\n"
        "; then input_device, then any 'Virtual Audio Cable' render endpoint.\n"
        "; If nothing matches, the banner prints EVERY render endpoint with its\n"
        "; state -- copy the cable's playback name into volume_endpoint and\n"
        "; rerun.\n"
        "volume_endpoint =\n"
        "; v7.0.0: pin_outputs is part of the engine now -- ALWAYS ON: both\n"
        "; endpoint volumes are pinned 100%% / unmuted while the engine runs,\n"
        "; EXCEPT a leg whose device IS the current default playback device:\n"
        "; that slider belongs to the user and is never written (v7.6.3).\n"
        "out2_first = on\n"
        "\n"
        "; KEEP-ALIVE (v7.1.7: BUILT IN -- no keys, no UI; v8.4.5: UNIVERSAL):\n"
        "; EVERY output leg sums a sine on its driven slots every frame\n"
        "; (gated or not, silence or not) so no device ever sees digital\n"
        "; silence -- 20 Hz @ 0.1%% (0.001, -60 dBFS) since v8.4.5 (was 40 Hz\n"
        "; @ 0.5%% since v7.1.24): far below hearing, and a wobble every\n"
        "; amp's sleep timer treats as signal. A gated block (any lane:\n"
        "; ch3/ch7/ch8 all-zero, or the v8.4.6 side lanes ch5/ch6) is\n"
        "; PROGRAM silence -- the wobble rides on, the amp stays fed. The\n"
        "; watchdog reopens a failed leg automatically\n"
        "; whenever it comes back.\n"
        "; POST-MIX: the tone (and out2's chime) are summed in the OUTPUT\n"
        "; callbacks, after the mix -- they never enter the capture stream\n"
        "; an OBS capture records.\n"
        "\n"
        "; CHIMES (v6.2): the CenterFold startup signature is back, modernized.\n"
        "; The old jingle was a rising SQUARE fifth (G5 784 Hz -> D6 1174.66 Hz);\n"
        "; the modernized chime keeps the rising fifth but plays two short\n"
        "; TRIANGLE BLIPS in the low treble instead: C7 2093 Hz (42 ms) -> G7\n"
        "; 3136 Hz (72 ms), fast decay -- a blip, not a horn. It plays on the\n"
        "; CENTER speaker the moment the engine goes live, and again whenever\n"
        "; the watchdog brings out2 back (3 s cooldown). It is a UI cue, not\n"
        "; program audio: summed at the out2 device layer exactly like the\n"
        "; keep-alive, so volume_follow does NOT scale it -- and the chime is\n"
        "; out2-ONLY (out1 carries just the universal wobble, never the\n"
        "; chime). chime_gain is the blip\n"
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
        "; v8.0.0: BOTH LEGS RUN KS BY DEFAULT -- the kernel-level loop. The\n"
        "; v7.9.0 field log (the 'switch served' meter) closed the case: even\n"
        "; with MMCSS on every thread, miniaudio's event-driven device thread\n"
        "; phase-locks to a 2-period stride at 100%% CPU -- every service lands\n"
        "; just past the DAC's stale boundary, and no priority can wake a\n"
        "; thread before its event fires. KS pacing has no deadline to miss:\n"
        "; the pacer writes the instant the DAC's own position register says\n"
        "; the lead was consumed. v7.1.7: NO UI CONTROL -- backendN = wasapi\n"
        "; opts a leg back into miniaudio exclusive event mode. A KS leg is a\n"
        "; direct render pin on the device's wave filter (KsCreatePin),\n"
        "; the WaveRT looped buffer as the device's own DMA memory, and the\n"
        "; hardware play-position register mapped read-only -- twinspeak's own\n"
        "; TIME-CRITICAL thread paces the stream against the DAC itself, with no\n"
        "; events, no engine and no scheduler intermediaries in between. The\n"
        "; audio crossing the buffer switch is bit-identical (same rings, same\n"
        "; replay/drop rules, same gates); only the transport changes.\n"
        ";   backendN = ks      WDM-KS -- the DEFAULT (the kernel-level loop)\n"
        ";   backendN = wasapi  wasapi exclusive (miniaudio event mode) -- the\n"
        ";                      automatic fallback and the opt-out\n"
        "; v8.4.11 THE 384x3 BITE (field-directed: \"I want it so it's 384\n"
        "; samples with 3 buffers instead\"): ks_period_frames / ks_periods\n"
        "; are BUILT IN -- 384 frames (8.0 ms of service, ~125 writes/s) x\n"
        "; 3 periods (a 1152-fr / 24.0 ms WaveRT loop, a 16.0 ms write\n"
        "; lead -- the deepest shipped lead yet). Fewer, larger writes\n"
        "; than v8.4.9's 288 x 2, and a late service has twice the margin\n"
        "; before the DAC grazes the buffer edge. No keys, no rows; stale\n"
        "; ks_period_frames / ks_periods lines load clean and are ignored.\n"
        "; The DAC adds its hardware FIFO on top\n"
        "; (~1-2 ms, printed). v6.3.1: with both legs on ks the\n"
        "; banner's align line prints the est-path DIFFERENCE and which delay\n"
        "; knob to move -- the delay trim starts from that number. v6.5.9: the\n"
        "; health log's 'devbuf' line (every 10 s) prints the MEASURED version\n"
        "; of those paths off the live devices (device pad + ring depth).\n"
        "; KS bypasses the Windows endpoint: no endpoint volume there, so\n"
        "; output pinning (always on, v7.0.0) is a no-op on a KS leg --\n"
        "; volume_follow still governs it\n"
        "; (applied in the buffer switch, exactly as before). If the driver refuses the\n"
        "; pin (no WaveRT support, format refused, pin busy) that leg logs the\n"
        "; reason and falls back to WASAPI exclusive automatically. Use\n"
        ";   twinspeak --list-ks\n"
        "; to see every WDM-KS render filter with its pins and formats -- the\n"
        "; SAME name needles as below match the KS filters.\n"
        "backend1 = ks\n"
        "backend2 = ks\n"
        "\n"
        "; CLOCK SYNC (v6.0, ASIO-strict): the audio path is BIT-EXACT -- there\n"
        "; is no resampler and no pitch control anywhere in twinspeak. Clock\n"
        "; drift between the cable's virtual clock and each device crystal is\n"
        "; absorbed by two device-master rules at the buffer switch:\n"
        ";   * a buffer switch the ring cannot fill is an UNDERRUN: it is a\n"
        ";     clean dropout (v7.8.4, built in): the leg goes\n"
        ";     silent while the ring restarts from the freshest capture (see\n"
        ";     the UNDERRUN RULE block below) -- never a replay tile.\n"
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
        "; delay1_ms/delay2_ms calibration is inside that number). v8.1.0: the\n"
        "; warden is OBSERVE ONLY -- a drift that leaves the baseline by\n"
        "; >= 5 ms (v8.3.0: hardcoded, was drift_correct_ms) for 3 consecutive\n"
        "; seconds LOGS an OBSERVE line (at most once per 5 s -- hardcoded,\n"
        "; was drift_interval_ms) naming the drift and advising a delay trim\n"
        "; BY EAR. No skip, no pad, no hold exists; the stripped path plays\n"
        "; what capture delivers. Two honest limits:\n"
        ";   * the ledger sees the HOST pipeline only -- anything below the USB\n"
        ";     packetizer (the S5's own receiver FIFO / amp silicon) is\n"
        ";     invisible. If the drift column stays flat while your ears hear\n"
        ";     drift, the offset is device silicon and no host trigger exists --\n"
        ";     the log then proves exactly that.\n"

        "; v6.6.8: drift_guard is part of the engine now -- ALWAYS ON (the\n"
        "; ledger + the drift column were never optional anyway).\n"
        "; v8.3.0: drift_correct_ms is RETIRED with the key -- hardcoded\n"
        "; 5 ms (the OBSERVE band), from the fielded ini.\n"
        "\n"
        "; v8.3.0: drift_interval_ms is RETIRED with the key -- hardcoded\n"
        "; 5000 ms, the OBSERVE line's spacing, from the fielded ini. Also\n"
        "; (in v6.5.8): any device reopen (sleep/wake, USB re-enumeration) now\n"
        "; invalidates the drift baseline and re-arms it once the path settles\n"
        "; (~9 s: 5 s settle + a 4-sample average) -- a boot-time-only\n"
        "; baseline went stale against exactly those events.\n"
        "; v8.4.7 SLOW SYNC (v8.4.9 THE WIDE NUDGE; v8.4.11 THE WARDEN\n"
        "; BITE; v8.4.12 THE DEPTH GUARD; v8.4.13 NO BASELINE): the\n"
        "; INTER-LEG drift is synced away -- out2/out3 sync their boundary\n"
        "; to OUT1, the first device (the master, never servoed): a\n"
        "; 16-frame drop/pad at most every 100 ms, only past a 250 us\n"
        "; dead-band, NO baseline at all (the servo's error is the measured\n"
        "; offset -- the settled latch was an acceptance path and is\n"
        "; retired); past 5 ms of accumulated drift the old warden bite\n"
        "; takes over (up to 240 frames per 2 s); creep is CORRECTED, never\n"
        "; accepted, and no re-latch exists. v8.4.12: the MASTER's ring\n"
        "; depth is guarded too (a capture burst's bloat is drained to a\n"
        "; 10 ms anchor, the slaves follow through the sync loop), and\n"
        "; reopens land delay-corrected. No key, no UI row. The cable\n"
        "; clock itself stays observe-only.\n"
        "\n"
        "; UNDERRUN RULE (v8.1.0, built in -- no key, no switch): a buffer\n"
        "; switch the raw ring cannot fully fill plays SILENCE for the hole\n"
        "; and counts it in the health line's 'short' column -- never a\n"
        "; repeated frame, never a replay tile, never a restart (v8.4.7: the\n"
        "; slow sync's one-frame pad is not a hole-filler -- it is a held\n"
        "; sample, rate-limited, holding the leg to out1): seams are gone by\n"
        "; construction, and what remains is the honest hole. 3 s of\n"
        "; sustained starvation (short switches every second) reopens the\n"
        "; leg fresh -- which also resets the device's own silicon. Capture\n"
        "; excess beyond the raw cap evicts the OLDEST frames (counted in\n"
        "; 'evict'), so the path can only ever run late -- never torn.\n"
        "\n"
        "; STALL RESYNC (v6.5.11, always on -- no ini key): if a leg starves\n"
        "; (an interrupt storm, a 100%% CPU hog) the shortfall rule counts it\n"
        "; ('short' climbs every second). Starvation lasting 3 consecutive\n"
        "; seconds reopens the leg fresh -- which also resets whatever the\n"
        "; device's own silicon accumulated during the storm. v8.4.10: every\n"
        "; reopen first discards the raw ring's backlog -- the leg resumes at\n"
        "; the freshest capture (see DEEP RESYNC).\n"
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
        "; v7.9.0: the KS pacer busy-waits on the DAC's position register\n"
        "; through the last period + 0.5 ms of slack (at the default switch\n"
        "; size that is the whole inter-switch window) -- one core per KS leg\n"
        "; is the price of a deadline the scheduler cannot move.\n"
        "\n"
        "; DEEP RESYNC (v6.5.12, optional): the S5's below-packetizer layer\n"
        "; (endpoint FIFO + amp silicon) can deepen silently over a long\n"
        "; session -- the drift ledger stays pinned while the center audibly\n"
        "; lags again. No host meter can see that layer; a scheduled fresh\n"
        "; reopen of the center leg re-primes it. Each resync is a brief\n"
        "; center gap and the warden re-arms ~9 s later; your delay1_ms\n"
        "; calibration holds across it. v8.3.0: hardcoded -- 30 min, the\n"
        "; long-movie setting (was deep_resync_min). v8.4.10: every reopen\n"
        "; first discards the raw ring's backlog -- while the leg was down\n"
        "; the ring filled to cap and eviction pinned its read position\n"
        "; ~170 ms behind the freshest capture, and a leg resumed from\n"
        "; there played a permanent delayed copy of the stream (the\n"
        "; loopback-sounding echo only a reset cleared). The leg now\n"
        "; resumes at the freshest capture, its delay calibration intact.\n"
        "\n"
        "; POWER GUARD (v6.5.2) -- sleep / Modern-Standby survival. Field log\n"
        "; (Lenovo M720Q): after sleep the S5's stream consumed ~42%% of the\n"
        "; cable's rate with a FRESH heartbeat -- the silence watchdog is blind\n"
        "; to that, and the path glitched forever in a drop/re-pin limit cycle.\n"
        "; With the guard ON: suspend parks the legs; resume rebuilds the\n"
        "; capture pin + both legs (and replays the startup chime); any leg\n"
        "; whose measured clock offset ('off') leaves +-10%% for 2 s is reopened\n"
        "; fresh -- that catches the degraded sleep-ENTRY window too, which\n"
        "; fires before the suspend broadcast. Every action is logged.\n"
        "; v6.6.8: power_guard is part of the engine now -- ALWAYS ON.\n"
        "\n"
        "; CENTER PRESENCE (v6.1) -- out2 plays the center channel ONLY while it\n"
        "; is genuinely present. Two gates, both default ON:\n"
        ";   center_gate (v8.4.4/v8.4.5: THE ABSOLUTE LANE GATE) -- a block\n"
        ";     whose ch3 is digital zero MUTES THE CENTER MIX: mix skipped,\n"
        ";     EQ reset -- program silence, no fade, no hysteresis, no ini\n"
        ";     switch. The v8.4.5 universal keep-alive (20 Hz @ 0.1%%) rides\n"
        ";     the gated block: the leg is program-silent, never\n"
        ";     device-silent -- that is what keeps the amp fed. A starved\n"
        ";     block keeps it too; an armed chime still plays. v8.4.5: the\n"
        ";     SAME gate rides the rears; v8.4.6 every lane is gated BY\n"
        ";     ITSELF -- ch7 all-zero mutes ch7's term on out3's left\n"
        ";     output, ch8 all-zero mutes ch8's term on out3's right\n"
        ";     output, and the side folds gate the same way (ch5/ch6\n"
        ";     all-zero mutes the fold's term on that output); v8.4.11: the\n"
        ";     rear mix skips only when ALL FOUR its lanes are zero. State\n"
        ";     changes log 'center gate' / 'rear L gate' / 'rear R gate' /\n"
        ";     'side L gate' / 'side R gate'.\n"
        ";   center_upmix_guard -- when the SOURCE has no center of its own (a\n"
        ";     stereo app on the 8ch cable), the Windows engine fills FC with\n"
        ";     a*FL + b*R at the cable's playback side -- BEFORE twinspeak can\n"
        ";     capture it, so channel muting cannot remove it (this is the 'if\n"
        ";     the center channel is absent it mixes left and right instead'\n"
        ";     issue). The guard recognizes that exact fingerprint per block\n"
        ";     (least-squares ch3 ~ a*ch1 + b*ch2, residual < -30 dB, gains in\n"
        ";     the upmix band, fronts active) and mutes those blocks too -- the\n"
        ";     center speaker stays on keep-alive only. v8.4.4: the fit runs\n"
        ";     from 16 frames up (the old 64-frame floor silently skipped it on\n"
        ";     the 32-frame default bite). Genuine 5.1 center is an\n"
        ";     INDEPENDENT track and never matches; it always passes. The health\n"
        ";     line counts guarded blocks ('cm') -- with real 5.1 content it\n"
        ";     must read 0. v6.6.8: center_gate and center_upmix_guard are\n"
        ";     part of the engine now -- ALWAYS ON. v8.4.10: the verdict is\n"
        ";     INSTANT -- the v7.8.4 2-hit/8-miss hysteresis retired (it\n"
        ";     left the gate whole buffers behind the evidence and the late\n"
        ";     verdict chopped the center at every content transition -- the\n"
        ";     chattering); a hit mutes the chunk it was measured on, a\n"
        ";     miss plays it, and ch3 alive over silent fronts is released\n"
        ";     on the spot. v8.4.11: the 2.5 ms fade is retired too -- a hit's\n"
        ";     chunk is ZEROED immediately (the fade was the leak: every\n"
        ";     engage let ~120 frames of the upmix through at ramping gain),\n"
        ";     a miss's chunk plays immediately.\n"
        "; v8.4.1: the same two gates ride THE CENTER FOLD -- when\n"
        ";     output_device2 is EMPTY and ch3 folds onto leg1's FL/FR, the\n"
        ";     upmix fingerprint fades the phantom center out of the fronts;\n"
        ";     'cm' then counts those guarded blocks on leg1.\n"
        "\n"
        "; ROUTING GUARANTEE (zero duplication, by construction):\n"
        ";   out1 gets ch1 (FL) and ch2 (FR) plus gain_lfe * ch4 (LFE) folded\n"
        ";   onto both. FC, the discrete LFE slot AND the 5.1 side slots stay\n"
        ";   DRIVEN DIGITAL SILENCE (the sides ride the rears only, v8.4.6),\n"
        ";   so no layer (Windows engine, miniaudio\n"
        ";   converter, Speaker Fill) has any signal to upmix into a fake\n"
        ";   center -- the center belongs to out2 alone (v8.4.1: an EMPTY\n"
        ";   output_device2 folds it onto out1's FL/FR at gain_center\n"
        ";   instead), the rears AND the sides to out3 alone. out2 gets\n"
        ";   ONLY ch3 (center): every other input\n"
        ";   channel is EXPLICITLY MUTED in the out2 path -- out2 is not a\n"
        ";   clone of the program audio. out3's LEFT output gets ch7 (rear\n"
        ";   L) + ch5 (side L, the v8.4.6 fold) and its RIGHT output gets\n"
        ";   ch8 (rear R) + ch6 (side R), at gain_rl/gain_rr, every lane\n"
        ";   gated BY ITSELF (an all-zero lane mutes its own term; the mix\n"
        ";   skips only when all four of the leg's lanes are zero); an\n"
        ";   unconfigured rear leg is silent, never an error. The startup banner prints the route map on every launch.\n"
        ";\n"
        "; NOTE: EVERY output sums the keep-alive sine (BUILT IN, v8.4.5:\n"
        "; 20 Hz @ 0.1%%) on its driven slots, every frame, so no device EVER\n"
        "; sees digital silence -- and when a lane gate mutes a block, the\n"
        "; keep-alive is all that block carries. The out2 pump also runs\n"
        "; FIRST and at the higher priority (out2_first): the center leg gets\n"
        "; the freshest data and the most scheduling attention;\n"
        "; delay1_ms/delay2_ms then trim the alignment. v8.4.1: with out2\n"
        "; EMPTY the fold rides the always-driven mains -- which carry the\n"
        "; wobble themselves now (v8.4.5).\n"
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
        "startup_engine = on\n"
        "\n"
        "; v7.1.20: gui_window_x / gui_window_y = where the control panel\n"
        "; last sat, rewritten automatically each time it hides (the values\n"
        "; below land in the file on the next save). -99999 = let Windows\n"
        "; place it; the position is always clamped back into a live\n"
        "; monitor's work area on open.\n"
        "gui_window_x = -99999\n"
        "gui_window_y = -99999\n");
    fclose(f);
}

// v8.2.0: clampMs is GONE with out1ms/out2ms -- the WASAPI period is an
// engine constant; nothing is left to clamp here.

// v6.6.0: the post-parse clamps, shared by loadIni AND the GUI's
// apply-from-controls path (so hand-typed values get the same guardrails
// and the same log lines whichever way they arrive).
static void cfgSanitize() {
    // v8.2.0: the out1ms/out2ms legacy fallback + clamps are GONE with the
    // keys (kOutPeriodMs is the request now; nothing to sanitize).
    if (g_cfg.delay1Ms < 0.0f)   g_cfg.delay1Ms = 0.0f;
    if (g_cfg.delay1Ms > 1000.0f) g_cfg.delay1Ms = 1000.0f;
    if (g_cfg.delay2Ms < 0.0f)   g_cfg.delay2Ms = 0.0f;
    if (g_cfg.delay2Ms > 1000.0f) g_cfg.delay2Ms = 1000.0f;
    // v8.4.0: the rear offsets ride the same clamps as the delays.
    if (g_cfg.offset3Ms < 0.0f)   g_cfg.offset3Ms = 0.0f;
    if (g_cfg.offset3Ms > 1000.0f) g_cfg.offset3Ms = 1000.0f;
    // v8.3.0: the drift trio's clamps are GONE with the keys -- hardcoded
    // constants (5 ms / 5000 ms / 30 min); nothing left to sanitize.
    // v6.3 backend normalization: "ks" (aliases: wdm-ks / wdmks) selects the
    // kernel-streaming pin; anything else stays WASAPI.
    for (int i = 0; i < 2; ++i) {
        std::string& b = (i == 0) ? g_cfg.backend1 : g_cfg.backend2;
        if (b == "ks" || b == "wdm-ks" || b == "wdmks" || b == "wdm_kernel_streaming" ||
            b == "kernel-streaming" || b == "kernelstreaming") b = "ks";
        else b = "wasapi";
    }
    // v8.4.5: the ks_period_frames / ks_periods clamps are GONE with the
    // keys -- the calm bite (192 fr x 3) is a compile-time constant; there
    // is nothing left to clamp.
}

// v7.0.4: the GUI theme persists across sessions -- loadIni (which runs long
// before the GUI exists) parks the parsed gui_theme here; guiMakeGdi applies it.
static int g_themeSaved = 0;

// v7.1.20: window-position persistence (gui_window_x / gui_window_y in the
// ini). Captured on every hide and at shutdown; spliced back by the normal
// ini save. -99999 = "let Windows place it" (virtual-screen coords can be
// negative, so the sentinel must be far outside that range).
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
        // v6.6.8: ten switches are now part of the engine itself -- always on,
        // no ini override, no UI pill. Old inis that still carry the keys load
        // cleanly: the lines are read and deliberately ignored.
        // v8.2.0: out1ms/out2ms (and the legacy buffer_size alias) joined the
        // list -- the WASAPI device period is an engine constant (kOutPeriodMs
        // = 1 ms; a KS leg, the default, never reads it at all).
        if (key == "out1ms"            ||    // v8.2.0: engine constant (1 ms)
            key == "out2ms"            ||    // v8.2.0: engine constant (1 ms)
            key == "buffer_size"       ||    // v8.2.0: the legacy outXms alias
            key == "capture_exclusive" || key == "exclusive" ||
                 key == "align_modes"       || key == "clock_sync" ||
                 key == "auto_resync"       || key == "drift_guard" ||
                 key == "power_guard"       || key == "yield_default" ||
                 key == "center_gate"       || key == "center_upmix_guard" ||
                 key == "drop_correct"      ||    // v7.8.4: integrated, read-and-ignored
                 key == "ring1_ms"         ||    // v8.1.0: ring system removed
                 key == "ring2_ms"         ||    // v8.1.0: ring system removed
                 key == "cap_ms"           ||    // v8.1.0: capture burst system removed
                 key == "volume_follow"    ||    // v8.3.0: hardcoded on
                 key == "drift_correct_ms" ||    // v8.3.0: hardcoded 5
                 key == "drift_interval_ms"||    // v8.3.0: hardcoded 5000
                 key == "deep_resync_min"  ||    // v8.3.0: hardcoded 30
                 key == "ks_period_frames" ||    // v8.4.5: the calm bite is BUILT IN (192 fr)
                 key == "ks_periods"       ||    // v8.4.5: BUILT IN (3 periods)
                 key == "background"        || key == "pin_outputs" ||
                 key == "lfe_fold") { }         // v8.4.0: the fold is built in
        else if (key == "input_device")   g_cfg.inputDevice = val;
        else if (key == "output_device1") g_cfg.outputDevice1 = val;
        else if (key == "output_device2") g_cfg.outputDevice2 = val;
        else if (key == "output_device3") g_cfg.outputDevice3 = val;   // v8.4.0 rear legs (v8.4.11: ONE device, both rears)
        // v8.4.11: output_device4 is RETIRED -- the key is read-and-ignored
        // (an unknown key falls through the loader untouched).
        else if (key == "gain_l")         g_cfg.gainL = (float)atof(val.c_str());
        else if (key == "gain_r")         g_cfg.gainR = (float)atof(val.c_str());
        else if (key == "gain_lfe")       g_cfg.gainLfe = (float)atof(val.c_str());
        else if (key == "gain_center")    g_cfg.gainCenter = (float)atof(val.c_str());
        else if (key == "gain_rl")        g_cfg.gainRl     = (float)atof(val.c_str()); // v8.4.0
        else if (key == "gain_rr")        g_cfg.gainRr     = (float)atof(val.c_str()); // v8.4.0
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
        else if (key == "offset3_ms")     g_cfg.offset3Ms = (float)atof(val.c_str()); // v8.4.0 rears (v8.4.11: the one rear leg)
        // v8.4.11: offset4_ms is RETIRED -- read-and-ignored, like out1ms.
        else if (key == "out2_first")     g_cfg.out2First    = !isOff(val);
        else if (key == "volume_endpoint") g_cfg.volumeEndpoint = val;
        else if (key == "chimes")         g_cfg.chimes        = !isOff(val);   // v6.2
        else if (key == "chime_gain")     g_cfg.chimeGain     = (float)atof(val.c_str()); // v6.2
        else if (key == "backend1")       g_cfg.backend1      = lower(val);    // v6.3
        else if (key == "backend2")       g_cfg.backend2      = lower(val);    // v6.3
        else if (key == "gui_theme") {
            g_themeSaved = atoi(val.c_str());      // v7.0.4: theme persistence
            if (g_themeSaved < 0 || g_themeSaved > 10) g_themeSaved = 0;   // v7.1.8: 11 themes
        }
        else if (key == "startup_engine") g_cfg.startupEngine = !isOff(val);   // v7.1.2
        else if (key == "gui_window_x") g_guiWinX = atoi(val.c_str());   // v7.1.20
        else if (key == "gui_window_y") g_guiWinY = atoi(val.c_str());   // v7.1.20
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
    pc.playback.channels = (idx == 0) ? kOutCh1 : kOutCh2;   // legs 2..4 are all 2ch
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
    L.isExclusive = wantExcl;
    L.id = *id;
    return true;
}

// v8.4.13/v8.4.14 THE DEFERRED REOPEN LANDING (the v8.4.10 reopen backlog
// discard, moved to where it can actually stick). Field, v8.4.13: "Just
// heard a second ding which raised raw2 to 43." The ding is the reopen
// chime (armChime fires on every leg2 reopen); the 43 was the landing
// being UNDONE: the old dropReopenBacklog trimmed the ring to its anchor
// AT REOPEN TIME -- but the new device takes hundreds of ms to actually
// start consuming, and in that window the capture kept pushing and
// refilled the ring right back toward its cap. The first pop then saw
// ~160 ms of backlog: off jumped, and the servo spent the next ~25 s
// grinding it (the log's "2064 fr late" = the field's 43 ms, mid-grind).
// THE FIX: a reopen only MARKS the leg (relandPending, set by
// tryRestartLeg before the new device thread exists); the NEW device
// thread lands its own ring at its FIRST buffer switch -- the discard and
// the first pop are the same instant, nothing can refill in between, and
// the producer races are already covered by the tail's monotonic CAS (the
// capture callback's evictions share it).
// The landing target is the servo's own: its error is the raw offset
// (v8.4.13: no baseline), so the clean landing is r2 = r1 -- leg1's REAL
// BACKLOG (its depth minus its delay prefill -- v8.4.12: the original
// full-depth target leaked delay1_ms into the landing, the field's
// "raw2 sits at 62") plus this leg's own prefill, floored at the depth
// guard's anchor so the restart's first fill cannot starve on a capture
// gap. The user's calibration lives in the delay prefills and needs no
// baseline to survive a reopen. The master lands at its own anchor.
static void landReopenedRing(int idx) {
    Leg& L = g_leg[idx];
    const long long have     = (long long)L.raw.count();
    const long long rMaster  = (long long)g_leg[0].raw.count()
                             - (long long)g_leg[0].delayFrames;
    const long long backlog  = (idx != 0)
                             ? (rMaster > (long long)kDepthAnchorFr ? rMaster
                                                                    : (long long)kDepthAnchorFr)
                             : (long long)kDepthAnchorFr;
    const long long want     = (long long)L.delayFrames + backlog;
    if (have > want)
        L.raw.discardOldest((size_t)(have - want));   // advances tail AND seqTail, ledger-true
}

// ------------------------------------------------- v3.8 watchdog restarts --
// The "output shut off by itself" class: a device thread silently stops
// (endpoint invalidated, USB replug, driver reset). The health loop reopens
// the leg BY NAME (fresh endpoint id), previous share mode first, then
// shared; silent retries until the state changes or it succeeds.
static bool tryRestartLeg(int idx, ma_context* ctx, ma_uint32 periodMs,
                          const char* why = "device thread silent > 5 s") {
    Leg& L = g_leg[idx];
    if (L.disabled) return false;   // v8.4.0: an empty rear leg never reopens
    static unsigned long long lastLog[4] = { 0, 0, 0, 0 };
    const unsigned long long now = GetTickCount64();
    L.lastReopenMs = now;   // v6.4: the drift warden pauses while a leg settles
    if (now - lastLog[idx] >= 10000) {
        tlog( "[twinspeak] leg%d WATCHDOG : %s -- reopening \"%s\"\n",
                idx + 1, why, L.name.c_str());
        lastLog[idx] = now;
    }
    const char* needle = legNeedle(idx);   // v8.4.0: one table for all four legs

    // v6.3: a KS leg reopens on its own transport first -- close pin+thread,
    // open a fresh KS pin. Only if the driver now refuses does it step down
    // to the WASAPI path below (and stay there for subsequent restarts).
    if (L.backend == 1) {
        ksCloseLeg(idx);
        L.relandPending = true;   // v8.4.14: the NEW device thread lands the ring at its first switch
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
    L.relandPending = true;   // v8.4.14: the NEW device thread lands the ring at its first switch
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
            // v7.6.3: THE DEFAULT-DEVICE GUARD -- a reopen that lands on the
            // current default playback endpoint must not write 100% to it
            // (the slider belongs to the user; yield_default re-yields the
            // leg within a tick). The pin stays UNRESOLVED -- the watcher
            // enforces nothing on an endpoint we do not hold.
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
    if (L.disabled) return;   // v8.4.0: the caller gates the print; belt to that
    const double burst = (double)g_capPeriodFrames * 1000.0 / (double)kSampleRate;
    const double ring  = (double)L.delayFrames * 1000.0 / (double)kSampleRate;
    // v7.8.2: a released leg (boot yield / default-device yield) holds no
    // device -- print the release state instead of a zeroed device line.
    if (L.yielded.load(std::memory_order_relaxed)) {
        tlog("[twinspeak] leg%d device     : RELEASED (yield) -- \"%s\" is not held by twinspeak right now;\n"
             "            the quiet re-grab opens it (exclusive first) once the Windows default has\n"
             "            moved away from it and no foreign session is active\n",
             idx + 1, L.name.c_str());
        return;
    }
    if (L.backend == 1) {
        // v6.3: WDM-KS leg -- the device bite is the built-in 192 fr (v8.4.5)
        // and the transport latency is the write lead (+ the DAC's FIFO).
        const KsOut& K = L.ks;
        const double pms   = (double)K.periodFrames * 1000.0 / (double)kSampleRate;
        const double lead  = ((double)K.leadFrames + 0.5 * (double)K.periodFrames)
                           * 1000.0 / (double)kSampleRate;
        tlog("[twinspeak] leg%d device     : WDM-KS WaveRT polling, \"%s\", pin %d, %s %dch @ %u Hz | loop %u fr, bite %u fr (~%.2f ms), lead ~%.1f ms%s | ring %.1f ms%s | est path ~%.1f ms (ring + KS lead + capture)\n",
               idx + 1, K.filterName.c_str(), K.pinId, K.fmtName.c_str(), K.outCh,
               (unsigned)kSampleRate,
               (unsigned)K.loopFrames, (unsigned)K.periodFrames, pms, lead,
               K.posMapped ? ", pos REGISTER" : ", pos IOCTL",
               ring, L.delayFrames ? " (delay prefill)" : "",
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
           L.delayFrames ? " (delay prefill)" : "",
           ring + devms + burst);
    // v5.0: with the strict split this is INFORMATION, not a warning. The FC
    // and discrete-LFE channels are DRIVEN DIGITAL SILENCE, so a stereo (or
    // any downmixed) endpoint has literally nothing to upmix -- the center
    // physically cannot appear on out1 no matter how the endpoint is set up.
    if (idx == 0 && d->playback.internalChannels != kOutCh1)
        tlog( "[twinspeak] leg1 info       : endpoint mix is %uch, not 6ch -- fine with the strict split:\n"
                        "            out1 drives FC/LFE AND the 5.1 rear slots as digital silence (the\n"
                        "            rears live on leg3), so no layer (engine, miniaudio converter,\n"
                        "            Speaker Fill) has anything to upmix into a fake center.\n",
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
    // page: configuration (v8.3.0: was devices -- the chime pills from the
    // late SETTINGS page live here; v8.4.2: the startup row is this page's
    // LAST row)
    IDC_INDEV = 300, IDC_OUT1, IDC_OUT2, IDC_VOLEP,
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
static std::string g_lastHealth;               // latest "out1 ..." health line
static std::atomic<bool> g_logDirty{false};
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
static int KCLI_W     = 880, KCLI_H      = 620;        // designed client size
static int KPANEL_T   = 66,  KPANEL_B    = 402;        // page panel band
static int KLOG_BOTTOM= 594;                            // log pane bottom
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
    KCLI_H     = S(620);
    KPANEL_T   = S(66);
    KPANEL_B   = S(402);
    KLOG_BOTTOM= S(594);
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
    const int ids[4] = { IDC_OUT1, IDC_OUT2, IDC_OUT3, IDC_INDEV };  // 0..2 playback (v8.4.11: ONE rear combo); 3 capture
    for (int k = 0; k < 4; ++k) {
        HWND h = GetDlgItem(w, ids[k]);
        char cur[512]; GetWindowTextA(h, cur, sizeof(cur));
        SendMessageA(h, CB_RESETCONTENT, 0, 0);
        const std::vector<std::string>& v = (k < 3) ? pb : cp;   // v8.4.11: three playback combos
        for (size_t i = 0; i < v.size(); ++i)
            SendMessageA(h, CB_ADDSTRING, 0, (LPARAM)v[i].c_str());
        if (cur[0]) SetWindowTextA(h, cur);
    }
}

// ---- ini values <-> gui ----------------------------------------------------
static std::string boolText(bool b) { return b ? "on" : "off"; }

static std::string cfgValueText(const std::string& key) {
    char b[512];
    if      (key == "delay1_ms")         { snprintf(b, 64, "%g", g_cfg.delay1Ms); return b; }
    else if (key == "delay2_ms")         { snprintf(b, 64, "%g", g_cfg.delay2Ms); return b; }
    else if (key == "offset3_ms")        { snprintf(b, 64, "%g", g_cfg.offset3Ms); return b; }   // v8.4.0
    else if (key == "gain_l")            { snprintf(b, 64, "%g", g_cfg.gainL);     return b; }
    else if (key == "gain_r")            { snprintf(b, 64, "%g", g_cfg.gainR);     return b; }
    else if (key == "gain_lfe")          { snprintf(b, 64, "%g", g_cfg.gainLfe);   return b; }
    else if (key == "gain_center")       { snprintf(b, 64, "%g", g_cfg.gainCenter);return b; }
    else if (key == "gain_rl")           { snprintf(b, 64, "%g", g_cfg.gainRl);   return b; }   // v8.4.0
    else if (key == "gain_rr")           { snprintf(b, 64, "%g", g_cfg.gainRr);   return b; }   // v8.4.0
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
    else if (key == "center_eq")         return boolText(g_cfg.centerEq);
    else if (key == "out2_first")        return boolText(g_cfg.out2First);
    else if (key == "chimes")            return boolText(g_cfg.chimes);
    else if (key == "input_device")      return g_cfg.inputDevice;
    else if (key == "output_device1")    return g_cfg.outputDevice1;
    else if (key == "output_device2")    return g_cfg.outputDevice2;
    else if (key == "output_device3")    return g_cfg.outputDevice3;   // v8.4.0
    else if (key == "volume_endpoint")   return g_cfg.volumeEndpoint;
    else if (key == "backend1")          return g_cfg.backend1;
    else if (key == "backend2")          return g_cfg.backend2;
    else if (key == "gui_theme")        { snprintf(b, 64, "%u", (unsigned)g_theme); return b; }
    else if (key == "startup_engine")   return boolText(g_cfg.startupEngine);   // v7.1.2
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
static const char* kIniKeys[] = {
    "delay1_ms", "delay2_ms", "offset3_ms",                    // v8.4.0 rears (v8.4.11: offset4_ms retired)
    "input_device", "output_device1", "output_device2",
    "output_device3",                                          // v8.4.0 rears (v8.4.11: output_device4 retired)
    "gain_l", "gain_r", "gain_rl", "gain_rr", "gain_lfe", "gain_center",
    "center_eq", "center_eq_freq", "center_eq_gain", "center_eq_q",
    "eq_slot1_on", "eq_slot1_freq", "eq_slot1_gain", "eq_slot1_q",   // v7.1.8
    "eq_slot2_on", "eq_slot2_freq", "eq_slot2_gain", "eq_slot2_q",
    "eq_slot3_on", "eq_slot3_freq", "eq_slot3_gain", "eq_slot3_q",
    "eq_slot4_on", "eq_slot4_freq", "eq_slot4_gain", "eq_slot4_q",
    "eq_slot5_on", "eq_slot5_freq", "eq_slot5_gain", "eq_slot5_q",
    "volume_endpoint", "out2_first",
    "chimes", "chime_gain",
    "backend1", "backend2",
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
    g_cfg.inputDevice    = editStr(w, IDC_INDEV);
    g_cfg.outputDevice1  = editStr(w, IDC_OUT1);
    g_cfg.outputDevice2  = editStr(w, IDC_OUT2);
    g_cfg.outputDevice3  = editStr(w, IDC_OUT3);   // v8.4.0: empty = the leg stays silent (v8.4.11: ONE rear device)
    g_cfg.volumeEndpoint = editStr(w, IDC_VOLEP);
    g_cfg.gainL     = editFloat(w, IDC_GAIN_L,  1.0f);
    g_cfg.gainR     = editFloat(w, IDC_GAIN_R,  1.0f);
    g_cfg.gainRl    = editFloat(w, IDC_GAIN_RL, 1.0f);   // v8.4.0 gain quad
    g_cfg.gainRr    = editFloat(w, IDC_GAIN_RR, 1.0f);
    g_cfg.gainLfe   = editFloat(w, IDC_GAIN_LFE, 0.4f);
    g_cfg.gainCenter= editFloat(w, IDC_GAIN_CTR, 1.0f);
    // v8.4.0: lfeFold is INTEGRATED (always on) -- no control reads it.
    // v6.6.8: captureExclusive / exclusive / alignModes are integrated -- the
    // struct defaults (all on) are final; no control can change them.
    // v8.2.0: out1Ms/out2Ms have no controls -- kOutPeriodMs is the request.
    g_cfg.delay1Ms = editFloat(w, IDC_DELAY1, 0.0f);
    g_cfg.delay2Ms = editFloat(w, IDC_DELAY2, 0.0f);
    g_cfg.offset3Ms = editFloat(w, IDC_OFF3, 0.0f);   // v8.4.0 timing quad (v8.4.11: the one rear leg)
    g_cfg.out2First  = g_tog[IDC_TGL_OUT2FIRST];
    // v7.0.0: pinOutputs is integrated -- always on; no control can change it.
    // v7.1.7: backend1/backend2 have NO control -- the ini keys stand as loaded.
    // v6.6.8: clockSync / driftGuard / powerGuard / yieldDefault integrated.
    // v8.3.0: the drift trio + volume_follow are hardcoded constants now
    // (5 / 5000 / 30 / on) -- no control reads them, nothing can flip them.
    // v6.6.8: centerGate / centerUpmixGuard / background integrated.
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
    SetDlgItemTextA(w, IDC_OUT3,  g_cfg.outputDevice3.c_str());   // v8.4.0
    SetDlgItemTextA(w, IDC_VOLEP, g_cfg.volumeEndpoint.c_str());
    snprintf(b, 64, "%g", g_cfg.gainL);      SetDlgItemTextA(w, IDC_GAIN_L, b);
    snprintf(b, 64, "%g", g_cfg.gainR);      SetDlgItemTextA(w, IDC_GAIN_R, b);
    snprintf(b, 64, "%g", g_cfg.gainRl);     SetDlgItemTextA(w, IDC_GAIN_RL, b);
    snprintf(b, 64, "%g", g_cfg.gainRr);     SetDlgItemTextA(w, IDC_GAIN_RR, b);
    snprintf(b, 64, "%g", g_cfg.gainLfe);    SetDlgItemTextA(w, IDC_GAIN_LFE, b);
    snprintf(b, 64, "%g", g_cfg.gainCenter); SetDlgItemTextA(w, IDC_GAIN_CTR, b);
    // v8.4.0: no LFEFOLD pill to sync -- the fold is built in (always on).
    snprintf(b, 64, "%g", g_cfg.delay1Ms); SetDlgItemTextA(w, IDC_DELAY1, b);
    snprintf(b, 64, "%g", g_cfg.delay2Ms); SetDlgItemTextA(w, IDC_DELAY2, b);
    snprintf(b, 64, "%g", g_cfg.offset3Ms); SetDlgItemTextA(w, IDC_OFF3, b);   // v8.4.0
    g_tog[IDC_TGL_OUT2FIRST] = g_cfg.out2First;
    // v7.1.7: backend1/backend2 have no control -- loaded from the ini only
    // v8.3.0: the drift trio + volume_follow have no controls to sync.
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
    // v7.1.20: MUST mirror guiPaintFrame's scaled status band exactly.
    const RECT sr = { S(14), S(458), S(866), S(476) };
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
    // v8.4.1: output_device2 is OPTIONAL now -- an EMPTY center leg never
    // opens and the center folds onto leg1's FL/FR (g_centerFold); the
    // rear legs were already so. Only the capture and the mains are hard.
    if (g_cfg.inputDevice.empty() || g_cfg.outputDevice1.empty()) {
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
        tlog("[twinspeak] gui           : could not write the ini -- running with the in-window values\n");
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
    g_run.store(false);
    trayUpdateTip(false);
}
// shutdown-only (message loop already exited / process going away): a block
// here costs nothing the user can see.
static void guiStopEngineJoin() {
    if (g_engState.load() != 0) { g_engState.store(3); g_run.store(false); }
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
    comboRow(w, COLA_X, KROW0, "capture input", IDC_INDEV, false, g_cfg.inputDevice.c_str());
    comboRow(w, COLA_X, 154, "output leg1 (mains)", IDC_OUT1, false, g_cfg.outputDevice1.c_str());
    comboRow(w, COLA_X, 188, "output leg2 (center)", IDC_OUT2, false, g_cfg.outputDevice2.c_str());
    comboRow(w, COLA_X, 222, "output leg3 (rears)", IDC_OUT3, false, g_cfg.outputDevice3.c_str());
    row(w, COLA_X, 256, "volume_endpoint", IDC_VOLEP, g_cfg.volumeEndpoint.c_str());
    HWND b = mkBtn(w, IDC_REFRESH, COLA_X, 290, 150, 26, "REFRESH DEVICES");
    regPage(b);                        // v6.6.2: page 0 only
    note(w, COLA_X, 318,
         "leg1 = FL/FR + the LFE fold; an EMPTY leg2 folds the center onto\n"
         "FL/FR (gain_center). leg3 = the cable's rears on ONE stereo device\n"
         "(out L = ch7+ch5 at gain_rl, out R = ch8+ch6 at gain_rr, v8.4.11);\n"
         "an EMPTY device row = the rears stay silent. volume_endpoint = master.", 4);
    header(w, COLB_X, KHDR_Y, "GAIN");
    // v8.4.0 THE GAIN QUAD: gain_l/gain_r/gain_rl/gain_rr as four short
    // narrow edits in one group (the field's ask), gain_lfe just below,
    // gain_center under it -- the wide rows that stay single-key.
    {
        char bL[32], bR[32], bRL[32], bRR[32];
        snprintf(bL,  32, "%g", g_cfg.gainL);
        snprintf(bR,  32, "%g", g_cfg.gainR);
        snprintf(bRL, 32, "%g", g_cfg.gainRl);
        snprintf(bRR, 32, "%g", g_cfg.gainRr);
        const char* lbl[4]  = { "gain_l", "gain_r", "gain_rl", "gain_rr" };
        const int   ids[4]  = { IDC_GAIN_L, IDC_GAIN_R, IDC_GAIN_RL, IDC_GAIN_RR };
        const char* vals[4] = { bL, bR, bRL, bRR };
        quadRow(w, COLB_X, 118, 4, lbl, ids, vals);
    }
    row(w, COLB_X, 170, "gain_lfe", IDC_GAIN_LFE, []{ char b[32]; snprintf(b,32,"%g",g_cfg.gainLfe); return std::string(b); }().c_str());
    row(w, COLB_X, 204, "gain_center", IDC_GAIN_CTR, []{ char b[32]; snprintf(b,32,"%g",g_cfg.gainCenter); return std::string(b); }().c_str());
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
    tglRow(w, COLB_X, 240, "enable_chime", IDC_TGL_CHIMES, g_cfg.chimes);
    row(w, COLB_X, 274, "chime_gain", IDC_CHIMGAIN, []{ char b[32]; snprintf(b,32,"%g",g_cfg.chimeGain); return std::string(b); }().c_str());
    tglRow(w, COLB_X, 308, "launch at Windows startup", IDC_TGL_STARTUP, startupEnabled());

    // ============ page 1: TIMING ==============================================
    // v8.4.5: the transport rows are GONE -- the bite is BUILT IN (192 fr
    // x 3, THE CALM BITE; no knob, one timing truth for every first-run
    // user). The page keeps the delay quad + out2_first, and the note
    // tells the built-in story. (v8.2.0 had unified the old TRANSPORT
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
        snprintf(b1, 32, "%g", g_cfg.delay1Ms);
        snprintf(b2, 32, "%g", g_cfg.delay2Ms);
        snprintf(b3, 32, "%g", g_cfg.offset3Ms);
        const char* lbl[3]  = { "delay1_ms", "delay2_ms", "offset3_ms" };
        const int   ids[3]  = { IDC_DELAY1, IDC_DELAY2, IDC_OFF3 };
        const char* vals[3] = { b1, b2, b3 };
        quadRow(w, COLA_X, 118, 3, lbl, ids, vals);
    }
    tglRow(w, COLA_X, 170, "out2_first (center opens first)", IDC_TGL_OUT2FIRST, g_cfg.out2First);
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
    note(w, COLA_X, 204,
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
    tglRow(w, COLA_X, 114, "center EQ (out2 only)", IDC_TGL_EQ, g_cfg.centerEq);
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
    // v8.4.2: the bottom-bar startup pill is GONE -- "launch at Windows
    // startup" is a plain tglRow again, the LAST row of page 0's right
    // column (same label + pill style as every other row; only its ORDER
    // changed). The bar keeps its five controls; nothing else moved.
    // v7.1.5 FLICKER-FREE STATUS: the STATIC is HIDDEN -- it only stores the
    // string; guiPaintFrame draws the line inside the frame buffer (one blit,
    // no erase-then-draw flicker four times a second). Writers go through
    // guiSetStatus, which invalidates the status band of the frame.
    // v7.1.20: the direct creations scale too (the factories do it for the
    // rest).
    HWND st = CreateWindowExA(0, "STATIC", "engine stopped", WS_CHILD,
                              S(14), S(458), S(852), S(16),
                              w, (HMENU)(INT_PTR)IDC_STATUS, nullptr, nullptr);
    SendMessageA(st, WM_SETFONT, (WPARAM)g_fMono, TRUE);
    tsTrackChild(st, 14, 458, 852, 16, &g_fMono);   // the dpi ledger
    // v6.6.6: no WS_EX_CLIENTEDGE -- the etched 3D edge rendered in light
    // classic colors was the "ghost bordered box"; WM_PAINT draws the frame.
    HWND log = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        S(14), S(480), S(852), S(114), w, (HMENU)(INT_PTR)IDC_LOG, nullptr, nullptr);
    SendMessageA(log, WM_SETFONT, (WPARAM)g_fMono, TRUE);
    tsTrackChild(log, 14, 480, 852, 114, &g_fMono);   // the dpi ledger
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
    // v7.1.26 THE WINDOW IS UNICODE, END TO END: CreateWindowExW +
    // DefWindowProcW -- the title is the full brand the taskbar, Alt-Tab
    // and Task Manager's app entry read.
    DWORD kstyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
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

    // the whole boot log -- everything the engine said while tray-only --
    // lands in the pane in one flush, oldest first, nothing lost.
    guiFlushLogRing();
    tlog("[twinspeak] gui           : control window up -- ini at %s\n", g_guiIniPath.c_str());
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
    Gdiplus::Pen fp(GCol(guic::cLine), 1.0f);   // the log pane's 1px frame (no CLIENTEDGE)
    // v7.7.1 THE CONSOLE FRAME HUGS THE PANE: the rect is the pane's OWN
    // device rect expanded 1px on every side. The old independent
    // S(13)/S(479) derivation rounded the frame and the pane separately,
    // and at 150% the drift showed -- the console frame floated off the
    // pane at odd scales. The pane is the anchor now; the frame cannot
    // miss it. (v7.8.0: the pane sits at its plain client seat -- S(480),
    // no band arithmetic.)
    g.DrawRectangle(&fp, Gdiplus::RectF((Gdiplus::REAL)S(14) - 0.5f,
                                        (Gdiplus::REAL)S(480) - 0.5f,
                                        (Gdiplus::REAL)S(852) + 1.0f,
                                        (Gdiplus::REAL)S(114) + 1.0f));
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
    char stb[512] = "";
    if (g_hwnd) GetDlgItemTextA(g_hwnd, IDC_STATUS, stb, (int)sizeof(stb));
    const RECT stc = { S(14), S(458), S(866), S(476) };
    guiAAString(g, dc, stc, stb, guic::cAccent, g_fMono, 1);
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
        case IDC_TGL_OUT2FIRST:   // v7.0.0: IDC_TGL_PINOUT retired (integrated)
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
        const RECT* sug = (const RECT*)lp;
        SetWindowPos(w, nullptr, sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        g_dpi96 = (UINT)HIWORD(wp);
        guiApplyDpi();
        guiMakeFonts();
        guiStripGpuRelease();
        tsRelayoutChildren();
        RedrawWindow(w, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0; }
    // ---- v7.8.0: NO NC OVERRIDES AT ALL ---------------------------------
    // The plain frame needs none of it: no WM_NCCALCSIZE insets (the
    // system computes the real frame), no WM_NCHITTEST bands (the caption,
    // the buttons, the resize edges and the corners answer DefWindowProc),
    // no WM_NCACTIVATE dance (the DWM re-renders its own active/inactive
    // titlebar), no band mouse routing (the buttons are the real native
    // ones). Every handler below the custom chrome served is retired.
    // v7.5.3c: WM_WINDOWPOSCHANGING (the size lock's second belt -- it
    // pinned every sizing pass to the designed 880x620+insets) stands down
    // for the resize test. The shell may size the window freely now; the
    // system's own WM_NCCALCSIZE owns the insets and WM_PAINT regrows the
    // frame buffers at whatever client size results.
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
        // allows only seconds).
        if (wp) {
            guiCaptureWindowPos(w);
            if (g_engState.load() != 0) { g_engState.store(3); g_run.store(false); }
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
                tlog("[twinspeak] gui           : first buffered frame -- center RGB %02x%02x%02x, theme bg RGB %02x%02x%02x\n",
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
        const bool onPage = !((id >= IDC_TAB0 && id <= IDC_TAB0 + 2) ||
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
    case WM_GETMINMAXINFO:
        // v7.5.3c: the min==max track (v6.6.3..v7.1.24 -- the size lock's
        // third belt) stands down for the resize test; DefWindowProc's
        // defaults own the tracking now.
        break;
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
            }
        } else {
            const unsigned long long end = g_tickEndMs.load(std::memory_order_relaxed);
            if (end != 0 && GetTickCount64() > end) {
                g_tickEndMs.store(0, std::memory_order_relaxed);
                SetDlgItemTextA(w, IDC_TICK, "120 BPM TICK TEST");
            }
        }
        // v7.1.20: the async stop's backup reap -- if WM_APP+2's bounded wait
        // expired (hidden window, timer asleep), the next tick closes the
        // handle. The thread itself already set state 0.
        if (g_engThread && WaitForSingleObject(g_engThread, 0) == WAIT_OBJECT_0) {
            CloseHandle(g_engThread);
            g_engThread = nullptr;
            guiRepaint(w, IDC_START);
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
        // v7.2.12: read the health line under the same lock the engine
        // thread writes it with (guiLogSink) -- no torn std::string reads.
        EnterCriticalSection(&g_logCS);
        const std::string health = g_lastHealth;
        LeaveCriticalSection(&g_logCS);
        if (!health.empty()) { s += "   |   "; s += health; }
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
            if (g_cfg.startupEngine && g_engState.load() == 0)
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
    g_logSink = guiLogSink;                            // everything logs to the pane now
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
    if (!startTray && g_cfg.startupEngine && withinLogonWindow(kBootWindowMs)) {
        tlog("[twinspeak] gui           : plain launch inside the logon window -- running it as the\n");
        tlog("                            startup instance (tray-only, startup engine)\n");
        startTray    = true;
        g_startTray  = true;
    }
    guiMakeGdi();
    // v7.1.7: ITaskbarList (the result only matters if COM changed modes under
    // us -- the taskbar slot then just falls back to shell timing).
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        tlog("[twinspeak] gui           : COM init failed -- the taskbar slot falls back to shell timing\n");
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
static BOOL WINAPI ctrlHandler(DWORD) { g_run.store(false); return TRUE; }

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
static int engineMain(const std::string& iniPath, bool fgForce,
                      bool allowMulti, bool guiMode);

int main(int argc, char** argv) {
    InitializeCriticalSection(&g_logCS);   // v5.0 logger
    tsEnsureConsole(false);                // v7.1.20: ride the parent terminal, if any
    std::string iniPath;
    bool doList = false, doListKS = false, fgForce = false, allowMulti = false;
    bool wantGui = false, startTray = false;   // v6.6.1: --gui / --tray
    bool doExit = false;                       // v7.2.1: --exit (clean remote stop)
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--list")) doList = true;
        else if (!strcmp(argv[i], "--list-ks")) doListKS = true;   // v6.3
        else if (!strcmp(argv[i], "--ini") && i + 1 < argc) iniPath = argv[++i];
        else if (!strcmp(argv[i], "--foreground")) fgForce = true;    // v6.2
        else if (!strcmp(argv[i], "--multi"))      allowMulti = true; // v6.2
        else if (!strcmp(argv[i], "--gui"))      wantGui = true;      // v6.6.0: explicit, same as default
        else if (!strcmp(argv[i], "--tray"))     { wantGui = true; startTray = true; }   // v6.6.1: window hidden in the tray
        else if (!strcmp(argv[i], "--exit"))     doExit = true;      // v7.2.1: clean remote stop
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            tlog("usage: twinspeak [--list] [--list-ks] [--ini path] [--foreground] [--multi]\n"
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
            tlog("[twinspeak] --exit      : no running instance found (nothing to stop).\n");
            return 1;
        }
        DWORD_PTR res = 0;
        const BOOL ok = SendMessageTimeoutA(other, WM_APP + 6, 0, 0,
                                            SMTO_ABORTIFHUNG, 4000, &res);
        tlog(ok ? "[twinspeak] --exit      : asked the running instance to exit cleanly.\n"
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
            tlog( "[twinspeak] already running -- twinspeak is single-instance.\n"
                            "            stop the running copy cleanly:  twinspeak.exe --exit\n"
                            "            (taskkill /F works but orphans the tray icon until hovered;\n"
                            "            a deliberate second copy: add --multi)\n");
            return 1;
        }
    }
    const int rc = engineMain(iniPath, fgForce, allowMulti, false);
    if (hInstance) CloseHandle(hInstance);
    return rc;
}

// v8.1.0: wardenRepin is GONE -- the re-pin operated on the out rings
// (skip the late leg's excess / replay-pad the early leg), and the ring
// system went with them. The warden measures and logs; nothing corrects.
// ---------------------------------------------------------------- v8.0.0 ----
// The kernel-loop default needs an ini-shaped on-ramp: every template before
// v8.0.0 wrote "backend1/backend2 = wasapi" explicitly, so a default flip
// alone never reaches an existing ini. One-time migration: a pre-8.0.0 ini
// whose backendN still carries the old template value ("wasapi") is migrated
// to ks, and the ini's version marker is bumped so the choice sticks. A user
// who edits backendN = wasapi AFTER the bump opts that leg out for good.
static int iniMarkerVersion(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return 0;
    char head[1024] = {};
    const size_t n = fread(head, 1, sizeof(head) - 1, f);
    fclose(f);
    head[n] = 0;
    const char* p = strstr(head, "; twinspeak v");
    if (!p) return 0;
    int maj = 0, mnr = 0, pat = 0;
    if (sscanf(p, "; twinspeak v%d.%d.%d", &maj, &mnr, &pat) != 3) return 0;
    return maj * 10000 + mnr * 100 + pat;
}
static bool bumpIniMarker(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string txt;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) txt.append(buf, n);
    fclose(f);
    const size_t at = txt.find("; twinspeak v");
    if (at == std::string::npos) return false;
    const size_t eol = txt.find('\n', at);
    if (eol == std::string::npos) return false;
    const std::string line = txt.substr(at, eol - at);
    const size_t cfgAt = line.find("config");
    const std::string tail = (cfgAt == std::string::npos) ? "" : line.substr(cfgAt + 6);
    char rep[160];
    snprintf(rep, sizeof(rep), "; twinspeak v%s config", TWS_VERSION);
    txt.replace(at, eol - at, std::string(rep) + tail);
    f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(txt.data(), 1, txt.size(), f) == txt.size();
    fclose(f);
    return ok;
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
    // v8.4.1: output_device2 is OPTIONAL -- EMPTY = no center leg and the
    // center folds onto leg1's FL/FR at gain_center (the rears were already so).
    if (g_cfg.inputDevice.empty() || g_cfg.outputDevice1.empty()) {
        tlog( "[twinspeak] config must set input_device and output_device1\n"
              "            (output_device2/3/4 may stay EMPTY: an empty leg2 folds center onto FL/FR)\n");
        return 1;
    }

    // v8.0.0 ONE-TIME BACKEND MIGRATION (see iniMarkerVersion above): an ini
    // written by any pre-8.0.0 template carries backendN = wasapi as literal
    // boilerplate -- the kernel-loop default would never reach it. Migrate
    // the old template value to ks and bump the ini's version marker; from
    // the next run on, an explicit wasapi in the ini is respected as a real
    // opt-out. An ini with NO version marker (hand-written) is left alone.
    {
        const int marker = iniMarkerVersion(iniPath);
        const bool legacy1 = (marker > 0 && marker < 80000 && g_cfg.backend1 == "wasapi");
        const bool legacy2 = (marker > 0 && marker < 80000 && g_cfg.backend2 == "wasapi");
        if (legacy1 || legacy2) {
            if (legacy1) g_cfg.backend1 = "ks";
            if (legacy2) g_cfg.backend2 = "ks";
            bumpIniMarker(iniPath);
            tlog("[twinspeak] v8.0.0       : one-time backend migration -- this ini predates the\n"
                 "                            kernel-loop default and still carried the old template's\n"
                 "                            backendN = wasapi; the leg(s) now open the direct WDM-KS pin\n"
                 "                            paced against the DAC's own position register (the 100%%-CPU\n"
                 "                            static fix). The ini's version marker is bumped, so an\n"
                 "                            explicit backendN = wasapi from here on is a real opt-out.\n");
        }
    }

    // v8.0.0: REALTIME process priority class -- the field directive, and
    // the right weight for a kernel-level loop: at REALTIME the engine's
    // TIME_CRITICAL threads sit at base 31, above every non-realtime thread
    // the OS has. v8.4.15: SeIncreaseBasePriorityPrivilege is EXPLICITLY
    // ENABLED first -- the privilege is often held by the token but
    // disabled at launch, and an unenabled privilege makes
    // SetPriorityClass(REALTIME) refuse even under an administrator
    // (field-directed: "make setting the process priority to real-time
    // work"). Without the privilege at all (a standard user token) the
    // call still refuses -- fall back to HIGH and say so.
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
        tlog("[twinspeak] priority      : REALTIME process class -- the engine's threads sit at the\n"
             "                            scheduler's top band; the KS pacer owns its core against\n"
             "                            anything user-mode.\n");
    } else {
        const DWORD pclErr = GetLastError();
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        tlog("[twinspeak] priority      : REALTIME class refused (err %lu) -- HIGH engaged. The token\n"
             "                            does not hold SeIncreaseBasePriorityPrivilege: launch elevated\n"
             "                            once for the full realtime band; MMCSS still orders the audio\n"
             "                            threads inside it.\n", pclErr);
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

    // keep-alive cycle table (BUILT IN -- v8.4.5: 20 Hz @ 0.001 on every
    // leg, through the gates; no ini, no UI)
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

    ma_device_id capId, id1, id2, id3; std::string capName, n1, n2, n3;   // v8.4.11: id4/n4 retired with leg4
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
    // v8.4.1: leg2 (center) joins the rears as OPTIONAL -- an EMPTY
    // output_device2 marks the leg disabled and turns on THE CENTER FOLD
    // (ch3 onto leg1's FL/FR at gain_center, the same upmix guard riding
    // it). A NON-empty needle that matches nothing is still a hard error,
    // exactly like leg1.
    g_leg[1].disabled = g_cfg.outputDevice2.empty();
    g_centerFold      = g_leg[1].disabled;
    if (!g_leg[1].disabled &&
        !pickDevice(&ctx, ma_device_type_playback, g_cfg.outputDevice2, &id2, &n2)) {
        tlog( "[twinspeak] output_device2 matching \"%s\" not found (--list)\n",
                g_cfg.outputDevice2.c_str());
        return 1;
    }
    if (g_leg[1].disabled)
        tlog("[twinspeak] leg2 (center)  : output_device2 is EMPTY -- no center device, no error;\n"
             "                            the center FOLDS onto leg1's FL/FR at gain_center\n"
             "                            (upmix-guarded: a stereo source's phantom FC is zeroed immediately)\n");
    // v8.4.0 THE REAR LEGS; v8.4.11 THE ONE REAR LEG: an EMPTY device
    // string = the leg is DISABLED -- no device is picked, no ring is
    // built, no thread starts: the rears simply never play (empty in =
    // empty out). output_device4 is retired -- the old leg4's slot is
    // force-disabled and a stale output_device4 line is ignored. A
    // NON-empty needle that matches nothing is still a hard error, exactly
    // like legs 1/2.
    g_leg[2].disabled = g_cfg.outputDevice3.empty();
    g_leg[3].disabled = true;   // v8.4.11: leg4 retired -- ONE rear device carries both rears
    if (!g_leg[2].disabled &&
        !pickDevice(&ctx, ma_device_type_playback, g_cfg.outputDevice3, &id3, &n3)) {
        tlog( "[twinspeak] output_device3 matching \"%s\" not found (--list)\n",
                g_cfg.outputDevice3.c_str());
        return 1;
    }
    if (g_leg[2].disabled)
        tlog("[twinspeak] leg3 (rears)    : output_device3 is EMPTY -- the leg stays SILENT (no device, no error)\n");

    // per-leg periods -- one engine constant for both now. v8.1.0: the
    // capture burst system is GONE -- the capture opens with a fixed 4 ms
    // request (the field's engine period) and nothing consumes burst math
    // anymore. v8.2.0: out1ms/out2ms are retired -- kOutPeriodMs (1 ms) is
    // every WASAPI leg's request; a KS leg never asks at all.
    const ma_uint32 periodMs1 = kOutPeriodMs;
    const ma_uint32 periodMs2 = kOutPeriodMs;
    const ma_uint32 capPeriodMs = 4;       // v8.1.0: fixed request, no cap_ms
    g_capPeriodMsF = 4.0f;                 // v3.8: watchdog restart state
    g_capNeedle    = g_cfg.inputDevice;
    g_capWantExcl  = g_cfg.captureExclusive;

    // per-leg identity + work buffers (rings come later -- their size needs
    // the negotiated device period)
    for (int i = 0; i < 4; ++i) {
        Leg& L = g_leg[i];
        L.index = i;
        L.outCh = (i == 0) ? kOutCh1 : kOutCh2;   // legs 2..4 are all 2ch
        L.name  = (i == 0) ? n1 : (i == 1) ? n2 : (i == 2) ? n3 : std::string();
        if (L.disabled) continue;                 // v8.4.0: no ring, no scratch (v8.4.11: leg4 always disabled)
        L.scratch.assign((size_t)kInCh * kScratchFrames, 0.0f);
    }

    // v3: legs go exclusive TOGETHER or shared TOGETHER. v2 let one leg win
    // exclusive while the other fell back -- mixed device buffering (2x5 vs
    // 2x10 ms) put the legs ~10 ms apart, which reads as "out2 is delayed".
    // v3.8 (out2_first): the CENTER leg is opened first so its stream (and
    // the keep-alive wobble) establishes before the mains leg.
    // v6.3: a leg whose backend = ks tries the WDM-KS pin FIRST (out2_first
    // ordering respected); a refusal falls back to the proven WASAPI path
    // below, so a leg is never left without a device. The align_modes dance
    // then applies to the WASAPI legs only (KS pins live under the endpoint
    // layer and do not participate in shared-mode buffering).
    // ---- v7.8.2 BOOT YIELD -- the Roblox "NoSoundDriver" / FL-ASIO fix ----
    // The old boot order opened both legs exclusive FIRST and let the
    // yield_default tick release the default-matching leg a second later.
    // That open -- even a REFUSED exclusive attempt, which on Realtek still
    // cycles the endpoint pin out from under the Windows engine --
    // invalidated every live shared-mode client on the endpoint (FMOD in
    // Roblox fell into NoSoundDriver; FL Studio ASIO sat on a dead stream
    // until its config was clicked). A leg whose device IS the current
    // default render endpoint, or that has ACTIVE foreign sessions, now
    // boots straight into the yielded state: the DAC is never touched at
    // boot, and the per-tick yield block re-grabs it under the v6.5.16
    // quiet rule (default moved away AND foreignActiveSessions == 0).
    bool bootYield[4] = { false, false, false, false };
    {
        std::string defNm;
        const bool haveDef = defaultRenderName(&defNm);
        for (int i = 0; i < 4; ++i) {
            if (g_leg[i].disabled) continue;      // v8.4.0: an empty rear leg holds nothing
            const char* yn = legNeedle(i);
            if (!yn[0]) continue;
            if (haveDef && containsCI(defNm, yn)) {
                bootYield[i] = true;
                g_leg[i].yielded.store(true, std::memory_order_relaxed);
                g_leg[i].isExclusive = g_cfg.exclusive;   // the quiet re-grab opens exclusive first
                g_leg[i].backend = 0;
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
                g_leg[i].isExclusive = g_cfg.exclusive;
                g_leg[i].backend = 0;
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
    // v8.4.0: FOUR legs, one generic open pass. wantKS: legs 1/2 honor
    // backend1/backend2; the rear legs are KS-first with no ini opt-out (a
    // driver refusal falls back to WASAPI exclusive, same as legs 1/2).
    // out2_first still orders the opens (center, mains, then the rears).
    const bool wantKS[4] = { g_cfg.backend1 == "ks", g_cfg.backend2 == "ks", true, true };
    int openOrder[4];
    if (g_cfg.out2First) { openOrder[0] = 1; openOrder[1] = 0; openOrder[2] = 2; openOrder[3] = 3; }
    else                 { openOrder[0] = 0; openOrder[1] = 1; openOrder[2] = 2; openOrder[3] = 3; }
    auto legIdPtr = [&](int i) -> ma_device_id* {
        return (i == 0) ? &id1 : (i == 1) ? &id2 : &id3;   // v8.4.11: leg4 never reaches here (disabled)
    };
    bool okLeg[4] = { false, false, false, false };
    for (int oi = 0; oi < 4; ++oi) {
        const int i = openOrder[oi];
        if (g_leg[i].disabled) { okLeg[i] = true; continue; }   // empty rear: fine by design
        if (bootYield[i])      { okLeg[i] = true; continue; }   // released by choice, not broken
        if (wantKS[i]) {
            okLeg[i] = ksOpenLeg(i, legNeedle(i));
            if (!okLeg[i])
                tlog("[twinspeak] leg%d KS        : refused -- falling back to the WASAPI exclusive path\n", i + 1);
        }
        g_leg[i].backend = (okLeg[i] && wantKS[i]) ? 1 : 0;
    }
    for (int oi = 0; oi < 4; ++oi) {
        const int i = openOrder[oi];
        if (g_leg[i].disabled || bootYield[i] || okLeg[i]) continue;
        okLeg[i] = initLegDevice(i, &ctx, legIdPtr(i), kOutPeriodMs, g_cfg.exclusive);
    }
    // v3: legs go exclusive TOGETHER or shared TOGETHER (v2 let one leg win
    // and the buffering mismatch skewed them ~10 ms). A driver refusal on
    // any WASAPI leg reopens ALL the WASAPI legs shared so device buffering
    // stays identical; KS legs are committed to their pin and stay as-is.
    {
        bool anyFail = false;
        for (int i = 0; i < 4; ++i)
            if (!okLeg[i] && !bootYield[i] && !g_leg[i].disabled) anyFail = true;
        if (anyFail && g_cfg.exclusive) {
            if (g_cfg.alignModes) {
                // Only WASAPI legs participate in the dance; a KS leg is already
                // committed to its pin and stays as-is. v7.8.2: a boot-yielded
                // leg holds no device at all -- the dance must never uninit or
                // reopen it (ok stays true; it is released by choice).
                tlog( "[twinspeak] align_modes: exclusive refused on a leg -- reopening the WASAPI legs shared so device buffering stays identical\n");
                for (int i = 0; i < 4; ++i) {
                    if (g_leg[i].disabled || bootYield[i] || g_leg[i].backend == 1) continue;
                    if (okLeg[i]) {
                        ma_device_uninit(&g_leg[i].dev);
                        memset(&g_leg[i].dev, 0, sizeof(g_leg[i].dev));
                        okLeg[i] = false;
                    }
                    if (!okLeg[i]) okLeg[i] = initLegDevice(i, &ctx, legIdPtr(i), kOutPeriodMs, false);
                }
            } else {
                for (int i = 0; i < 4; ++i)
                    if (!okLeg[i] && !bootYield[i] && !g_leg[i].disabled && g_leg[i].backend == 0)
                        okLeg[i] = initLegDevice(i, &ctx, legIdPtr(i), kOutPeriodMs, false);
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (okLeg[i] || bootYield[i] || g_leg[i].disabled) continue;
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
    // and the gated-drop line. v6.3: a KS leg's bite is exactly the
    // built-in 288-fr bite (v8.4.9; our pacing thread never asks for more).
    for (int i = 0; i < 4; ++i) {
        if (g_leg[i].disabled) { g_periodFrames[i] = 0; continue; }   // v8.4.0: no device
        if (g_leg[i].backend == 1) {
            g_periodFrames[i] = (size_t)kKsPeriodFrames;
            continue;
        }
        g_periodFrames[i] = g_leg[i].dev.playback.internalPeriodSizeInFrames
                          ? (size_t)g_leg[i].dev.playback.internalPeriodSizeInFrames
                          : (size_t)(((i == 0) ? periodMs1 : periodMs2) * (kSampleRate / 1000));
    }
    g_capPeriodFrames = g_capDev.capture.internalPeriodSizeInFrames
                      ? (size_t)g_capDev.capture.internalPeriodSizeInFrames
                      : (size_t)(capPeriodMs * (kSampleRate / 1000));
    if (g_capPeriodFrames == 0) g_capPeriodFrames = 192;   // v8.1.0: informational only

    // v8.1.0: THE STRIPPED PATH -- there are no out rings and no targets.
    // Each leg's ONLY buffer is its raw fan-out ring: capacity = the delay +
    // generous capture-jitter headroom; the delay itself is a silence
    // PREFILL (bit-exact, sequence-less). Capture excess evicts oldest at
    // the cap (counted in rawDrop, ledger-true); a device shortfall plays
    // silence (counted in und). Nothing manages depth.
    for (int i = 0; i < 4; ++i) {
        Leg& L = g_leg[i];
        if (L.disabled) continue;   // v8.4.0: an empty rear leg owns no ring
        const float delayMs = (i == 0) ? g_cfg.delay1Ms
                          : (i == 1) ? g_cfg.delay2Ms
                          : g_cfg.offset3Ms;           // v8.4.0: the rears' own knob (v8.4.11: ONE rear leg)
        const size_t devPeriodFr  = (L.backend == 1)
                                  ? (size_t)kKsPeriodFrames
                                  : (L.dev.playback.internalPeriodSizeInFrames
                                     ? L.dev.playback.internalPeriodSizeInFrames : 480);
        L.delayFrames = (size_t)((double)kSampleRate * delayMs / 1000.0);
        L.delayMs     = delayMs;
        L.raw.init(L.delayFrames + (size_t)(kSampleRate / 1000 * 150), kInCh);
        L.raw.pushSilence(L.delayFrames);   // the delay: bit-exact, seq-less
        L.prefillRemaining = L.delayFrames; // v6.4 lineage: prefill carries no sequence
        // the KS lapse guard's re-prime source (the replay path is gone).
        L.lastCap = kReplayMaxPeriods * devPeriodFr;
        L.lastBlock.assign(L.lastCap * (size_t)L.outCh, 0.0f);
        L.lastN = 0;
        L.haveLast = false;
        // v7.0.1: every clock the ledger reads is PER RUN. A restart used to
        // inherit captured/popped, putting the tick anchor in mismatched
        // capture-sequence spaces (per leg!). Zeroed HERE, on the main
        // thread, before any device thread exists.
        L.captured.store(0, std::memory_order_relaxed);
        L.popped.store(0, std::memory_order_relaxed);
        L.rawDrop.store(0, std::memory_order_relaxed);
        L.und.store(0, std::memory_order_relaxed);
        L.cmUpmix.store(0, std::memory_order_relaxed);
        L.firstSwitchUs.store(0, std::memory_order_relaxed);   // v7.1.5 start align
        L.relandPending = false;                                       // v8.4.14: fresh run, nothing pending
        L.syncDrops = 0;                                               // (per run, like the clocks)
        L.syncPads  = 0;
        L.lastBiteMs   = 0;                                            // v8.4.11/8.4.12: the bite + depth-guard clocks
        L.wardenDrops  = 0;
        L.wardenPads   = 0;
        L.depthDrops   = 0;
        // v6.6.9: the bit-zero fast-path gate, frozen at start (the pumps
        // that froze it at pump-start are gone).
        L.zeroFast = (i == 0)
            ? (g_cfg.gainL > 0.0f && g_cfg.gainR > 0.0f && g_cfg.gainLfe > 0.0f
               && (!g_centerFold || g_cfg.gainCenter > 0.0f))   // v8.4.1: the fold term too
            : (i == 1)
            ? (g_ceq.on ? true : g_cfg.gainCenter >= 0.0f)
            : (g_cfg.gainRl >= 0.0f && g_cfg.gainRr >= 0.0f); // v8.4.11: the one rear leg, both sides (g * 0 == 0, always)
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
        for (int i = 0; i < 4; ++i) {
            if (g_leg[i].disabled) continue;   // v8.4.0: nothing to pin
            if (g_leg[i].backend == 1) {
                // v6.3: a KS pin is a direct line to the DAC -- there is no
                // endpoint volume to pin. volume_follow still governs this
                // leg from the pump side.
                tlog("[twinspeak] out%d: WDM-KS pin -- endpoint pinning not applicable (KS bypasses endpoint volume)\n", i + 1);
                continue;
            }
            // v7.8.2: a boot-yielded leg holds no endpoint (its id was never
            // resolved) -- nothing to pin; the quiet re-grab re-pins with
            // the default-device guard (tryRestartLeg).
            if (g_leg[i].yielded.load(std::memory_order_relaxed)) {
                tlog("[twinspeak] out%d: leg RELEASED (boot yield) -- endpoint pin not applicable until it reopens\n", i + 1);
                continue;
            }
            // v7.6.3 THE DEFAULT-DEVICE GUARD: when a leg's endpoint IS the
            // current default playback device, its volume slider belongs to
            // the user -- engine start must never slam it to 100%/unmuted
            // (the field: "if the default playback device is either the
            // first output leg or second output leg, their volume must not
            // just get set to 100 once the engine starts"). The pin stays
            // UNRESOLVED -- the watcher has nothing to enforce on it -- and
            // yield_default releases this leg's DAC within a health tick;
            // the yield-release reopen re-pins once the default has moved
            // away, and the pin then writes like any other.
            if (isDefaultRenderEndpointId(g_leg[i].id.wasapi)) {
                tlog("[twinspeak] out%d: endpoint IS the default playback device -- volume left alone (no 100%% pin)\n", i + 1);
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

    // v3 start order (v8.1.0): capture FIRST, playbacks last -- the raw
    // rings hold real audio before the devices open their mouths (no startup
    // underrun). out2_first still orders the DEVICE starts (center first).
    if (ma_device_start(&g_capDev) != MA_SUCCESS) {
        tlog( "[twinspeak] capture start failed\n");
        return 1;
    }
    // v8.1.0: no pumps to start -- the switches pull from the raw rings.
    Sleep(30);   // let capture fill both raw rings with real audio

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
    for (int li = 0; li < 4; ++li) {   // v8.4.0: the rears trim their backlog too
        Leg& SL = g_leg[li];
        if (!SL.raw.cap) continue;                     // ring not built
        const long long level = (long long)SL.raw.count();
        if (level > (long long)(SL.delayFrames + 96)) {
            const size_t dropped =
                SL.raw.discardOldest((size_t)(level - (long long)SL.delayFrames - 96));
            tlog("[twinspeak] leg%d sync     : start trim: dropped %llu fr of pre-playback backlog -- the first buffer opens at the delay anchor\n",
                 li + 1, (unsigned long long)dropped);
        }
    }

    // v8.4.1: no center device = no chime (the signature rides out2 only);
    // arming it with leg2 disabled would just never play.
    if (!g_leg[1].disabled)
        armChime(true);   // v6.2: the first thing the center speaker plays is the signature

    // v6.3: a KS leg "starts" by creating its pacing thread (the thread
    // flips the pin to RUN itself); ma_device_start applies to WASAPI legs.
    // out2_first ordering is honored for both backends.
    // v7.8.2: a boot-yielded leg has no device to start -- count it as
    // success; the quiet re-grab (tryRestartLeg) starts it when it reopens.
    // v8.4.0: one generic pass over the open order -- center first when
    // out2_first, then the mains, then the rears; a disabled leg has
    // nothing to start, success by design.
    {
        ma_result sAll = MA_SUCCESS;
        for (int oi = 0; oi < 4; ++oi) {
            const int i = openOrder[oi];
            Leg& L = g_leg[i];
            if (L.disabled || L.yielded.load(std::memory_order_relaxed)) continue;
            if (L.backend == 1) {
                L.ks.thread = CreateThread(nullptr, 0, ksThread, (LPVOID)(INT_PTR)i, 0, nullptr);
                if (!L.ks.thread) sAll = MA_ERROR;
            } else {
                if (ma_device_start(&L.dev) != MA_SUCCESS) sAll = MA_ERROR;
            }
        }
        if (sAll != MA_SUCCESS) {
            tlog( "[twinspeak] playback start failed\n");
            return 1;
        }
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
        // v8.4.0: the align generalizes to every LIVE leg (a disabled rear
        // is out of the game by design; a boot-yielded leg aligns itself on
        // its quiet reopen). Wait for every participant's first buffer
        // switch, then align them ALL to the earliest one.
        const unsigned long long alignT0 = GetTickCount64();
        for (;;) {
            bool all = true;
            for (int i = 0; i < 4; ++i) {
                if (g_leg[i].disabled) continue;
                if (g_leg[i].yielded.load(std::memory_order_relaxed)) continue;
                if (g_leg[i].popped.load(std::memory_order_acquire) == 0) { all = false; break; }
            }
            if (all) break;
            if (GetTickCount64() - alignT0 > 250) break;   // a dead leg: watchdog territory
            Sleep(1);
        }
        unsigned long long fs[4] = { 0, 0, 0, 0 };
        int nPart = 0;
        bool missing = false;
        for (int i = 0; i < 4; ++i) {
            if (g_leg[i].disabled) continue;
            if (g_leg[i].yielded.load(std::memory_order_relaxed)) continue;   // released legs hand in nothing
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
                if (gapUs <= 0) continue;               // within one frame of the reference
                long long gapFr = (gapUs * (long long)kSampleRate + 500000) / 1000000;
                // v8.1.0: the replay-pad is GONE. A leg that began eating
                // later holds ~gapFr frames more in its raw ring -- skipping
                // them oldest-first (bit-exact, ledger-true) puts every
                // stream at the same depth, so all legs open their stream on
                // the same instant. The delay/offset differences are left
                // exactly as configured: the skip is clipped to the excess
                // and only ever consumes PREFILL SILENCE.
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
                } else {
                    tlog("[twinspeak] start align  : leg%d stagger %lld us vs leg%d, but it holds no excess to\n"
                         "                            skip -- leaving it to the baseline calibration\n",
                         i + 1, gapUs, ref + 1);
                }
            }
        }
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

    tlog("[twinspeak] v%s         : BIT-EXACT ASIO-strict engine -- the audio path is memcpy; twinspeak\n",
           TWS_VERSION);   // v7.9.0: the banner carried a stale v7.1.19 literal
    tlog("                            never resamples and never shifts pitch. Center isolation: out2 carries ONLY ch3.\n");
    tlog("[twinspeak] backend        : %s\n", ma_get_backend_name(ctx.backend));
    tlog("[twinspeak] capture        : \"%s\"  8ch @ %u Hz | %s | delivery ~%.1f ms (fixed 4 ms request, v8.1.0)\n",
           capName.c_str(), kSampleRate,
           capExclusive ? "EXCLUSIVE direct pin" : "shared",
           g_capPeriodFrames * 1000.0 / kSampleRate);
    if (!capExclusive && g_cfg.captureExclusive)
        tlog( "[twinspeak] capture hint   : exclusive was refused -- the pin is held by the engine or the\n"
                        "            Line 1 port driver is WaveCyclic. The direct WaveRT capture path\n"
                        "            needs the WaveRT port driver selected in the VAC Control Panel.\n");
    tlog("[twinspeak] clock sync     : v8.1.0 THE STRIPPED PATH -- a shortfall plays SILENCE (counted,\n"
           "                            'short'), capture excess evicts OLDEST at the raw cap (counted,\n"
           "                            'evict'). No replay queue, no rate state, nothing bends pitch: the\n"
           "                            DAC's register paces it all. v8.4.7 SLOW SYNC on top: out2/out3 hold\n"
           "                            their settled offset to OUT1 (the first device, never adjusted) -- a\n"
           "                            16-frame drop/pad at most every 100 ms past a 250 us dead-band (v8.4.9).\n"
           "                            v8.4.11 THE WARDEN BITE: the old drift correction (5 ms per 2 s) rides\n"
           "                            the same servo -- past 5 ms of accumulated drift a leg drops/pads up to\n"
           "                            240 frames in one bite, and slow creep is corrected, NEVER accepted.\n"
           "                            v8.4.12 THE DEPTH GUARD: the master's ring depth is pinned to a 10 ms\n"
           "                            anchor (past a 5 ms dead-band, the same warden pacing) -- a capture\n"
           "                            burst's bloat is DRAINED, never kept, and the slaves follow through\n"
           "                            the sync loop; reopens land delay-corrected; no re-latch anywhere.\n"
           "                            v8.4.13 NO BASELINE: the servo's error IS the measured offset -- the\n"
           "                            settled latch (an acceptance path) is retired; the settle gate is a\n"
           "                            startup hush only; nothing is ever blessed as the new truth.\n"
           "                            v8.4.15 THE DELAY-NEUTRAL METRIC: each leg's own delay is subtracted\n"
           "                            from its boundary before the diff (err = (depth2-delay2) -\n"
           "                            (depth1-delay1)) -- a delay on one leg can never drag the others;\n"
           "                            and a STARVED leg is never reopened (only a thread that stopped\n"
           "                            switching entirely is).\n");
    tlog("[twinspeak] drift warden  : %s -- frame-interval ledger: every captured frame is stamped, every\n"
           "                            real frame that crosses a leg's play boundary advances that leg's stream\n"
           "                            position, and the health line's 'drift' column is the measured inter-speaker\n"
           "                            offset (no clock involved -- pure frame arithmetic; your delay1_ms/delay2_ms\n"
           "                            is inside it -- at delay1_ms = 60 it reads about -65, and that IS the\n"
           "                            alignment, not a fault). The baseline (your delay calibration included) is\n"
           "                            averaged ~5-9 s after boot and re-arms after any reopen. v8.1.0: MEASUREMENT\n"
           "                            ONLY -- no skip, no pad, no correction; it LOGS when the drift leaves the\n"
           "                            %+.0f ms band for 3 s and the ears decide. v8.4.7: the INTER-LEG drift is\n"
           "                            now slowly synced away (out2/out3 hold their offset to out1 -- see clock\n"
           "                            sync above; v8.4.11: the warden bite corrects creep, never accepts it);\n"
           "                            the cable clock itself stays measurement-only.\n",
           g_cfg.driftGuard ? "ON" : "observe-only (drift_guard = off)", g_cfg.driftCorrectMs);
    tlog("[twinspeak] power guard   : %s -- suspend parks the legs, resume rebuilds capture + both\n"
           "                            legs, and any leg consuming beyond +-10%% for 2 s is reopened\n"
           "                            fresh. The v6.5.1 field log: an S5 came back from sleep at ~42%%\n"
           "                            consumption with a fresh heartbeat and glitched forever.\n",
           g_cfg.powerGuard ? "ON" : "OFF (power_guard = off -- v6.5.1 behavior)");
    tlog("[twinspeak] leg1 (mains)   : \"%s\" | %s | period %u ms x2\n",
           n1.c_str(),
           g_leg[0].backend == 1 ? "WDM-KS (kernel streaming)" :
           g_leg[0].yielded.load(std::memory_order_relaxed) ? "RELEASED (boot yield)" :
           g_leg[0].isExclusive ? "EXCLUSIVE" : "shared", (unsigned)periodMs1);
    tlog("[twinspeak] route out1     : FL = %.2f*ch1 + %.2f*ch4(LFE) | FR = %.2f*ch2 + %.2f*ch4(LFE)\n"
               "[twinspeak] route out1     : FC + discrete LFE + the 5.1 side slots (out ch5/ch6) = driven digital\n"
               "[twinspeak] route out1     : silence, hardcoded -- the sides ride leg3 through the v8.4.6 side fold\n"
               "[twinspeak] route out1     : + the universal 20 Hz @ 0.1%% keep-alive on FL/FR (v8.4.5, every gap)\n",
               g_cfg.gainL, g_cfg.gainLfe, g_cfg.gainR, g_cfg.gainLfe);
    tlog("[twinspeak] leg2 (center)  : \"%s\" | %s | period %u ms x2\n",
           n2.c_str(),
           g_leg[1].backend == 1 ? "WDM-KS (kernel streaming)" :
           g_leg[1].yielded.load(std::memory_order_relaxed) ? "RELEASED (boot yield)" :
           g_leg[1].isExclusive ? "EXCLUSIVE" : "shared", (unsigned)periodMs2);
    // v8.4.11: the rear leg's banner -- present only when a device is set.
    if (!g_leg[2].disabled) {
        tlog("[twinspeak] leg3 (rears)   : \"%s\" | %s | %s\n",
               g_leg[2].name.c_str(),
               g_leg[2].backend == 1 ? "WDM-KS (kernel streaming)" :
               g_leg[2].yielded.load(std::memory_order_relaxed) ? "RELEASED (boot yield)" :
               g_leg[2].isExclusive ? "EXCLUSIVE" : "shared",
               g_leg[2].backend == 1 ? "same bite/lead as leg1 (BUILT IN)"
                                     : "period 1 ms x2 (WASAPI fallback)");
    }
    tlog("[twinspeak] route out3     : L = %.2f*ch7 (cable RL) + %.2f*ch5 (cable SL, the v8.4.6 side fold)\n"
               "[twinspeak] route out3     : R = %.2f*ch8 (cable RR) + %.2f*ch6 (cable SR, side fold) -- ONE rear\n"
               "[twinspeak] route rears    : device carries both rears (v8.4.11), each lane gated BY ITSELF (an\n"
               "[twinspeak] route rears    : all-zero lane mutes its own output's term; the mix skips only when ALL\n"
               "[twinspeak] route rears    : FOUR lanes are zero) + the universal 20 Hz @ 0.1%% keep-alive; an\n"
               "[twinspeak] route rears    : unconfigured rear leg = silent by design (empty device = empty out; the\n"
               "[twinspeak] route rears    : rears AND sides ride ONLY here)\n",
               g_cfg.gainRl, g_cfg.gainRl, g_cfg.gainRr, g_cfg.gainRr);
    // v8.4.1: the route banner tells the truth for BOTH shapes -- a live
    // out2 (ch3 on its own device) and the fold (leg2 EMPTY, ch3 onto FL/FR).
    if (g_centerFold) {
        tlog("[twinspeak] route out2     : EMPTY -- no center device; ch3 (center) FOLDS onto out1's FL/FR at\n"
             "                            gain_center %.2f, the LFE fold's pattern (the FC slot stays driven silence).\n",
             g_cfg.gainCenter);
        tlog("[twinspeak] route out2     : center gates %s -- THE SAME gates ride the fold on leg1: digital-zero ch3\n"
             "                            folds nothing; ch3 == a*ch1+b*ch2 (the Windows stereo->8ch upmix\n"
             "                            fingerprint, residual < -30 dB) fades the phantom center out of the\n"
             "                            fronts; genuine 5.1 center (an independent track) always plays.\n",
             (g_cfg.centerGate && g_cfg.centerUpmixGuard) ? "BOTH ON" :
             (g_cfg.centerUpmixGuard ? "upmix guard on, gate OFF" :
             (g_cfg.centerGate ? "gate on, upmix guard OFF" : "OFF (center_gate = off)")));
        tlog("[twinspeak] route out2     : center EQ idle -- it is out2-only and out2 is EMPTY; the fold is a plain\n"
             "                            gain_center sum, no filter (a fold must never color the mains)\n");
    } else {
        tlog("[twinspeak] route out2     : L = R = %.2f*ch3(center) ONLY -- ch1/ch2/ch4..ch8 EXPLICITLY MUTED in the out2 path\n"
               "[twinspeak] route out2     : (out2 is NOT a clone of the program audio) + %.0f Hz @ %.2f%% keep-alive\n",
               g_cfg.gainCenter, g_kaHz, g_kaGain * 100.0f);
        tlog("[twinspeak] route out2     : center gates %s -- ch3 digital-zero -> THE MIX IS MUTED (mix + EQ off; the\n"
               "                            20 Hz @ 0.1%% wobble rides on, v8.4.5); ch3 == a*ch1+b*ch2 (the Windows\n"
               "                            stereo->8ch upmix fingerprint, residual < -30 dB) -> muted. Genuine 5.1\n"
               "                            center (an independent track) never matches and always plays.\n",
               (g_cfg.centerGate && g_cfg.centerUpmixGuard) ? "BOTH ON" :
               (g_cfg.centerUpmixGuard ? "upmix guard on, gate OFF" :
               (g_cfg.centerGate ? "gate on, upmix guard OFF" : "OFF (center_gate = off)")));
        if (g_ceq.on)
            tlog("[twinspeak] route out2     : center EQ ON -- peaking %.0f Hz %+.1f dB Q %.2f on ch3 (dialogue boost;\n"
                   "                            a KS/WaveRT leg bypasses every system APO, so the boost lives here)\n",
                   g_ceq.f0, g_ceq.gainDb, g_ceq.q);
        else
            tlog("[twinspeak] route out2     : center EQ off -- out2 is bit-exact\n");
    }
    // v6.3.1: the align line does the arithmetic -- both est paths and their
    // difference, with the trim direction spelled out. Same formula as
    // printLegDevice (ring + device lead + capture burst, delay included),
    // so the numbers match the per-leg device lines below.
    auto estPathOf = [&](int i) -> double {
        const Leg& L = g_leg[i];
        const double delayMsv = (double)L.delayFrames * 1000.0 / (double)kSampleRate;
        if (L.backend == 1)
            return delayMsv + ((double)L.ks.leadFrames + 0.5 * (double)L.ks.periodFrames)
                            * 1000.0 / (double)kSampleRate;
        const ma_device* d = &L.dev;
        const double devMs = d->playback.internalPeriodSizeInFrames
            ? (double)d->playback.internalPeriodSizeInFrames * (double)d->playback.internalPeriods
              * 1000.0 / (double)d->playback.internalSampleRate : 0.0;
        return delayMsv + devMs;
    };
    const double ep1 = estPathOf(0), ep2 = g_centerFold ? 0.0 : estPathOf(1);   // v8.4.1: leg2 may be EMPTY
    if (g_centerFold)
        tlog("[twinspeak] align          : delay1 +%d ms -- out2 is EMPTY: no center leg to align (the center fold\n"
             "                            rides leg1's own path, bit-exact; delay2_ms is inert until leg2 returns)\n",
             (int)(g_cfg.delay1Ms + 0.5f));
    else {
        tlog("[twinspeak] align          : delay1 +%d ms, delay2 +%d ms -- bit-exact prefill delay; put it on the EARLY leg\n"
               "                            est path: leg1 ~%.1f ms vs leg2 ~%.1f ms (difference %+.1f ms with the\n"
               "                            current delay included -> %s). The S5's own\n"
               "                            amp latency is OUTSIDE the print -- the ear trims the last 1-2 ms.\n"
               "                            v6.5.9: the health log's 'devbuf' line re-prints this from the LIVE\n"
               "                            devices every 10 s (padding / exclusive buffer / KS lead + raw).\n",
               (int)(g_cfg.delay1Ms + 0.5f), (int)(g_cfg.delay2Ms + 0.5f),
               ep1, ep2, ep1 - ep2,
               (ep1 - ep2) > 0.05 ? "leg1 arrives later -- raise delay2_ms" :
               (ep1 - ep2) < -0.05 ? "leg2 (center) arrives later -- raise delay1_ms" :
               "printed paths are EQUAL -- any offset left is device silicon");
    }
    // v8.4.0: the rear legs' est paths against the mains -- trim
    // offset3_ms from this number (a rear that arrives EARLY takes the
    // offset).
    if (!g_leg[2].disabled) {
        const double ep3 = estPathOf(2);
        tlog("[twinspeak] align rears   : offset3 +%d ms -- the rear leg's bit-exact delay\n"
             "                            est path vs leg1: leg3 %+.1f ms (positive = the rear arrives later;\n"
             "                            trim the offset of a rear that arrives EARLY)\n",
             (int)(g_cfg.offset3Ms + 0.5f), ep3 - ep1);
    }
    if (g_centerFold)
        tlog("[twinspeak] route check    : out2 is EMPTY -- center (ch3) FOLDS onto out1's FL/FR at gain_center, the\n"
             "                            same upmix guard protecting it; out1 plays ch1/ch2 + the LFE fold + the\n"
             "                            center fold; the rears (ch7/ch8) and the sides (ch5/ch6, the v8.4.6 fold)\n"
             "                            play ONLY on out3, devices permitting\n");
    else
        tlog("[twinspeak] route check    : center (ch3) plays ONLY on out2; out1 plays ONLY ch1/ch2 + the LFE fold;\n"
               "                            the rears (ch7/ch8) and the sides (ch5/ch6, the v8.4.6 fold) play ONLY on\n"
               "                            out3, and ONLY when its device is set\n");
    if (g_cfg.volumeFollow) {
        if (g_srcVol)
            tlog("[twinspeak] volume        : following \"%ls\" (the cable's playback side) -- now %.0f%%%s -- drives every leg\n",
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
        tlog("[twinspeak] output pins   : every WASAPI leg's endpoint forced 100%%/unmuted -- re-checked every 2 s\n");
    tlog("[twinspeak] start order   : %s\n",
           g_leg[1].disabled ? "out2 is EMPTY -- leg1 opens first, the rears follow; the center rides the FL/FR fold" :
           g_cfg.out2First ? "out2 (center) FIRST -- its device opens first; out1 follows (v8.1.0: no pumps)"
                           : "out1 first, then out2 (out2_first = off)");
    tlog("[twinspeak] dac yield     : %s -- the leg whose device IS the Windows default render endpoint\n"
           "                            is released the same second (KS pin closed / WASAPI device freed,\n"
           "                            pump parked) so the main device plays untouched; it reopens the\n"
           "                            moment the default moves elsewhere (yield_default)\n",
           g_cfg.yieldDefault ? "ON" : "off");
    if (g_leg[1].disabled)
        tlog("[twinspeak] chimes        : %s -- but out2 is EMPTY (no center device): the signature rides the\n"
             "                            center speaker only, so it stays silent this run\n",
             g_cfg.chimes ? "on" : "off");
    else if (g_cfg.chimes) {
        float cgp = g_cfg.chimeGain; if (cgp < 0.0f) cgp = 0.0f; if (cgp > 1.0f) cgp = 1.0f;
        tlog("[twinspeak] chimes        : ON -- CenterFold signature, modernized: low-treble TRIANGLE blips\n"
               "                            (rising C7->G7) on out2 at start + after a leg2 reopen | gain %.0f%%\n",
               cgp * 100.0f);
    } else {
        tlog("[twinspeak] chimes        : off (chimes = off)\n");
    }
    printLegDevice(0);
    printLegDevice(1);
    printLegDevice(2);   // v8.4.0: the rear legs print only when configured
    printLegDevice(3);
    // v8.2.0: the v6.0 latency hint is GONE -- it pointed at out1ms/out2ms,
    // and those are engine constants now (1 ms); its >=10 ms trigger cannot
    // fire. printLegDevice above prints each leg's real negotiated path.
    tlog("[twinspeak] threads        : 1 capture + the device threads only (v8.1.0: the switches mix inline,\n"
           "                            the pumps are gone)\n");
    {
        int ksLegs = 0, nLive = 0, firstWa = -1;
        for (int i = 0; i < 4; ++i) {
            if (g_leg[i].disabled) continue;
            ++nLive;
            if (g_leg[i].backend == 1) ++ksLegs;
            else if (firstWa < 0) firstWa = i;
        }
        if (nLive > 0 && ksLegs == nLive)
            tlog("[twinspeak] transport      : every leg on WDM-KS -- the WASAPI engine is fully out of the output path\n");
        else if (ksLegs > 0 && firstWa >= 0)
            tlog("[twinspeak] transport      : %d of %d legs on WDM-KS, the rest on WASAPI (first WASAPI leg: leg%d)\n",
                   ksLegs, nLive, firstWa + 1);
    }

    if (g_logToFile)
        tlog("[twinspeak] running in BACKGROUND -- health line every 10 s (live counters every second):\n\n");
    else
        tlog("[twinspeak] running -- Ctrl+C to stop. health line every second:\n\n");

    // v8.4.0: per-leg state, four slots (a disabled rear's counters never
    // move and its streaks stay zero -- it can never fire anything).
    unsigned long long uB[4] = { 0, 0, 0, 0 };           // prev-second 'short' per leg
    unsigned long long dB[4] = { 0, 0, 0, 0 };           // prev-second 'evict' per leg
    unsigned long long aB[4] = { 0, 0, 0, 0 };           // off-meter captured baselines
    unsigned long long pB[4] = { 0, 0, 0, 0 };           // off-meter popped baselines
    unsigned long long m00 = 0;                          // cumulative upmix-guard blocks
    long offV[4] = { 0, 0, 0, 0 };                       // measured clock offset ppm per leg
    int colStreak[4] = { 0, 0, 0, 0 };                   // v6.5.2 collapse sentinel
    unsigned long long colLastMs[4] = { 0, 0, 0, 0 };    // per-leg reopen cooldown
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
    // v8.4.15: stallSec is RETIRED with the stall resync (starvation never
    // reopens; see the STALL RESYNC note below).
    unsigned long long deepLastMs = 0; // v6.5.12: scheduled deep-resync clock
    // v8.1.0: the first-second fast tier is GONE (it existed only to fire
    // the warden's re-pin early; the re-pin is gone with the ring system).
    while (g_run.load()) {
        Sleep(1000);
        ++sec;
        // v6.0 measured clock offset per leg: dCaptured/dPopped - 1 over the
        // last second (both counters integrate the real clocks; bursts and
        // pop phase cancel in the deltas). Positive = cable runs fast.
        for (int i = 0; i < 4; ++i) {
            const unsigned long long Ai = g_leg[i].captured.load(std::memory_order_relaxed);
            const unsigned long long Ci = g_leg[i].popped.load(std::memory_order_relaxed);
            if (pB[i] != 0 && Ci > pB[i] && Ai > aB[i])
                offV[i] = (long)(((double)(Ai - aB[i]) / (double)(Ci - pB[i]) - 1.0) * 1000000.0);
            aB[i] = Ai; pB[i] = Ci;   // v8.4.0: a disabled leg holds 0/0 -> off stays 0
        }
        // v8.1.0 fields: off = measured clock offset ppm; short ('und') =
        // switches the raw ring could not fill; evict ('drop') = passive
        // oldest-evictions at the raw cap. Computed BEFORE the hint so it
        // can require a quiet window.
        unsigned long long dropN[4], undN[4];
        for (int i = 0; i < 4; ++i) {
            dropN[i] = g_leg[i].rawDrop.load();
            undN[i]  = g_leg[i].und.load();
        }
        // v8.4.1: with the center fold active the guarded blocks count on
        // LEG1 (the fold's guard state lives there) -- the 'cm' column stays
        // the truth in both shapes.
        const unsigned long long m1 = g_centerFold ? g_leg[0].cmUpmix.load()
                                                   : g_leg[1].cmUpmix.load();   // v6.1 upmix-guarded blocks
        // v6.4 DRIFT LEDGER: the two legs' play boundaries in capture-sequence
        // space; their difference IS the inter-speaker offset (1 ms = 48 fr).
        // Silence prefills do NOT advance it; pops and evictions DO (v8.1.0:
        // the boundary lives on the raw ring the switch pops).
        const long long st0 = (long long)g_leg[0].raw.seqTail.load(std::memory_order_relaxed);
        const long long st1 = (long long)g_leg[1].raw.seqTail.load(std::memory_order_relaxed);
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
            const long a0 = offV[0] < 0 ? -offV[0] : offV[0];
            const long a1 = offV[1] < 0 ? -offV[1] : offV[1];
            const long dom = (a0 >= a1) ? offV[0] : offV[1];   // dominant leg (the calibrated pair)
            const long ad  = dom < 0 ? -dom : dom;
            const bool quiet = (dropN[0] - dB[0]) == 0 && (dropN[1] - dB[1]) == 0
                            && (undN[0] - uB[0]) == 0 && (undN[1] - uB[1]) == 0;
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
                     offV[0], offV[1]);
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
            for (int i = 0; i < 4; ++i) {   // v8.4.0: every live leg gets the 5 s check
                if (g_leg[i].disabled) continue;
                if (nowMs - hbOutGet(i) > 5000 && !g_leg[i].yielded.load(std::memory_order_relaxed))
                    tryRestartLeg(i, &ctx, kOutPeriodMs);
            }
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
                for (int i = 0; i < 4; ++i) {
                    if (g_leg[i].disabled) continue;   // v8.4.0: nothing held, nothing to yield
                    const char* yneedle = legNeedle(i);
                    if (!yneedle[0]) continue;
                    Leg& Y = g_leg[i];
                    const bool mine = containsCI(defName, yneedle);
                    if (mine && !Y.yielded.load(std::memory_order_relaxed)) {
                        Y.yielded.store(true, std::memory_order_relaxed);
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
                            static unsigned long long lastBusyLog[4] = { 0, 0, 0, 0 };
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
                                static unsigned long long lastProbeLog[4] = { 0, 0, 0, 0 };
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
            // v8.4.0: one pass over the legs, out2_first symmetry kept.
            for (int oi = 0; oi < 4; ++oi) {
                const int i = g_cfg.out2First ? (oi == 0 ? 1 : oi == 1 ? 0 : oi) : oi;
                if (g_leg[i].disabled) continue;
                if (!g_leg[i].yielded.load(std::memory_order_relaxed))
                    tryRestartLeg(i, &ctx, kOutPeriodMs);
            }
            wardenStreak = 0; hintStreak = 0; prevDom = 0;  // stale pre-sleep meters
            for (int k = 0; k < 4; ++k) colStreak[k] = 0;   // v8.4.0: all four legs
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
            // v8.4.0: a disabled leg's off reads a flat 0 (nothing moves)
            // -- it must not fire this either.
            int li = -1;
            for (int i = 0; i < 4; ++i) {
                const bool bad = !g_leg[i].disabled
                                 && !g_leg[i].yielded.load(std::memory_order_relaxed)
                                 && (offV[i] > kCollapsePpm || offV[i] < -kCollapsePpm);
                colStreak[i] = bad ? colStreak[i] + 1 : 0;
                if (colStreak[i] >= 2 && li < 0) li = i;
            }
            if (li >= 0) {
                const long bad = offV[li];
                Leg& SL = g_leg[li];
                if (nowMs - SL.lastReopenMs > 5000 && nowMs - colLastMs[li] >= 10000) {
                    colLastMs[li] = nowMs;
                    for (int k = 0; k < 4; ++k) colStreak[k] = 0;
                    if (bad > 0) {
                        // positive = the DEVICE under-consumed (captured more
                        // than it popped) -- the field log's half-alive S5.
                        tlog("[twinspeak] power guard : leg%d consuming at %+ld ppm -- stream broken\n"
                             "                            (sleep resume, driver reset?) -- reopening fresh\n",
                             li + 1, bad);
                        tryRestartLeg(li, &ctx, kOutPeriodMs);
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
        // ---------------- v6.5.11 STALL RESYNC -- RETIRED (v8.4.15) ----------
        // Field-directed: "I don't want a device being silent to reopen (if
        // at all). The device being silent doesn't 'put the usb to sleep'
        // and cause a drift." A starved leg trips the shortfall rule
        // (silence, counted in 'und') and plays the keep-alive through the
        // hole -- the amp stays fed and a SILENT stream loses nothing, so
        // the 3 s starvation reopen was pure churn (the ding, the restart
        // window, the landing). The heartbeat WATCHDOG above still reopens
        // a thread that stopped SWITCHING entirely (a dead device never
        // returns on its own); a leg that merely starves -- switches alive,
        // ring dry -- is left alone to ride the next capture burst.
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
                // v8.1.0: OBSERVE ONLY -- the re-pin (ring skip / replay pad)
                // is gone with the ring system. The warden still measures,
                // still calibrates the baseline, and still SAYS when the path
                // left it -- the ears decide, the log proves.
                const double err = driftMs - driftBase;
                if (err >= g_cfg.driftCorrectMs || err <= -g_cfg.driftCorrectMs) {
                    ++wardenStreak;
                    if (wardenStreak >= (int)kWardenStreakNeeded) {
                        wardenStreak = 0;
                        wardenLastMs = nowMs;
                        tlog("[twinspeak] drift warden : OBSERVE -- drift %+.1f ms left the %+.1f ms baseline by\n"
                             "                            %+.1f ms for %u s (no correction exists in v8.1.0 -- the\n"
                             "                            stripped path plays what capture delivers; trim\n"
                             "                            delay1_ms/delay2_ms by ear)\n",
                             driftMs, driftBase, err, kWardenStreakNeeded);
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
        double      padMs[4]  = { 0.0, 0.0, 0.0, 0.0 };
        const char* padHow[4] = { "----", "----", "----", "----" };
        for (int i = 0; i < 4; ++i) {
            Leg& L = g_leg[i];
            if (L.disabled) continue;   // v8.4.0: no device, no pad
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
            const double path1 = padMs[0] + (double)g_leg[0].raw.count() * 1000.0 / kSampleRate;
            const double path2 = padMs[1] + (double)g_leg[1].raw.count() * 1000.0 / kSampleRate;
            tlog("\n[twinspeak] devbuf      : leg1 %5.1f ms (%s) + raw %5.1f = %5.1f ms\n"
                 "                            leg2 %5.1f ms (%s) + raw %5.1f = %5.1f ms\n"
                 "                            measured path diff %+.1f ms -- BELOW-packetizer silicon (the\n"
                 "                            S5's own FIFO) is still outside these numbers\n",
                 padMs[0], padHow[0], g_leg[0].raw.count() * 1000.0 / kSampleRate, path1,
                 padMs[1], padHow[1], g_leg[1].raw.count() * 1000.0 / kSampleRate, path2,
                 path1 - path2);
            // v8.4.0: the rear legs ride the same 10 s cadence, against leg1.
            for (int i = 2; i < 4; ++i) {
                if (g_leg[i].disabled) continue;
                const double pathR = padMs[i] + (double)g_leg[i].raw.count() * 1000.0 / kSampleRate;
                tlog("                            leg%d %5.1f ms (%s) + raw %5.1f = %5.1f ms (vs leg1: %+.1f ms)\n",
                     i + 1, padMs[i], padHow[i],
                     g_leg[i].raw.count() * 1000.0 / kSampleRate, pathR, pathR - path1);
            }
            unsigned long long laps[4] = { 0, 0, 0, 0 };
            bool anyLapse = false;
            for (int i = 0; i < 4; ++i) {
                laps[i] = g_leg[i].ks.lapses.load(std::memory_order_relaxed);
                if (laps[i]) anyLapse = true;
            }
            if (anyLapse)
                tlog("                            KS lapse guard: %llu / %llu / %llu / %llu write-cursor re-pin(s) since boot\n",
                     laps[0], laps[1], laps[2], laps[3]);
        }
        if (!g_logToFile || (sec % 10) == 0)      // v6.2: the log gets a line every 10 s
        tlog(g_logToFile
             ? "raw1 %5.1f | raw2 %5.1f | vol %s | off %+6ld/%+6ld | short +%llu/%llu/%llu/%llu | evict +%llu/%llu/%llu/%llu | cm +%llu | drift %s | err %s\n"
             : "raw1 %5.1f | raw2 %5.1f | vol %s | off %+6ld/%+6ld | short +%llu/%llu/%llu/%llu | evict +%llu/%llu/%llu/%llu | cm +%llu | drift %s | err %s   \r",
               g_leg[0].raw.count() * 1000.0f / kSampleRate,
               g_leg[1].raw.count() * 1000.0f / kSampleRate,
               volStr,   // v3.9 NOTE: this argument WAS missing in v3.8 (the crash);
                         // every conversion here is argument-checked since (v6.5.1 added err).
               offV[0], offV[1],   // v8.4.0: the rear legs' offsets live in the devbuf line
               undN[0] - uB[0], undN[1] - uB[1], undN[2] - uB[2], undN[3] - uB[3],
               dropN[0] - dB[0], dropN[1] - dB[1], dropN[2] - dB[2], dropN[3] - dB[3],
               m1 - m00,
               driftStr, errStr);
        fflush(stdout);
        for (int k = 0; k < 4; ++k) { uB[k] = undN[k]; dB[k] = dropN[k]; }
        m00 = m1;
    }

    tlog("\n[twinspeak] stopping...\n");
    g_stopSilence.store(true, std::memory_order_relaxed);   // v7.0.0: the stop gap is SILENCE
    g_run.store(false);
    Sleep(150); // let the device threads drain their tails (v8.1.0: no pumps to join)
    if (hWatch) { WaitForSingleObject(hWatch, 2000); CloseHandle(hWatch); }
    if (g_capDev.pContext)     ma_device_uninit(&g_capDev);
    for (int i = 0; i < 4; ++i) {   // v8.4.0: the rear legs close the same way
        if (g_leg[i].backend == 1) ksCloseLeg(i);          // v6.3: stop thread + close pin
        if (g_leg[i].dev.pContext) ma_device_uninit(&g_leg[i].dev);
    }
    if (g_srcVol) { g_srcVol->Release(); g_srcVol = nullptr; }
    for (int i = 0; i < 4; ++i)
        if (g_pin[i].vol) { g_pin[i].vol->Release(); g_pin[i].vol = nullptr; }
    if (comOK) CoUninitialize();
    ma_context_uninit(&ctx);
    timeEndPeriod(1);
    tlog("[twinspeak] stopped.\n");
    return 0;
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
