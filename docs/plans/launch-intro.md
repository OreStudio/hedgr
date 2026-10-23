# Launch intro

The intro replaces the splash. It plays once, at launch, over the main window.

## Timeline

The intro is two slow pulses of light, like a neon sign that rests dim, flares,
settles back to dim, flares again and settles. Nothing moves or grows: only the
light changes. All times are from the first frame the intro is drawn. The
constants live in `hedgr.ui/render/intro.hpp`; the look constants live in
`hedgr.ui/theme/theme.hpp`.

The sign starts, ends and dips between the pulses at a resting level,
`intro_rest_brightness` (0.32 of full): a dim, soft, slightly cool grey-white,
not black and not blue.

| Time | Brightness | Veil |
| --- | --- | --- |
| 0.0 s | 0.32 (rest) | opaque |
| 1.423 s (`intro_peak_ns - intro_rise_ns`) | 0.32, the first pulse ignites | opaque |
| 3.023 s (`intro_peak_ns`) | 1 (first peak) | opaque |
| 4.623 s | 0.32, the valley: the pulses touch rest | opaque |
| 6.223 s (`intro_peak_ns + intro_pulse_period_ns`) | 1 (second peak, same height) | opaque |
| 7.823 s (`intro_rest_ns`) | 0.32 (rest again) | opaque |
| 8.623 s (`intro_total_ns`) | 0.32 | 0, the main window is fully shown |

Each pulse is a smoothstep from the rest level up to the peak over
`intro_rise_ns` (1.6 s) and a smoothstep back down over `intro_fall_ns` (1.6 s).
The period (`intro_pulse_period_ns`, 3.2 s) is the fall plus the rise, so the
valley is one instant at exactly the rest level and the two read as separate
flashes. `intro_pulse_count` (2) pulses are laid down by `brightness`, which
takes the highest of them at any time, so it supports any count. The slope is
zero at the start, each peak and the end of each pulse, so there is no jump or
flicker. The steepest step between samples 10 ms apart is about 0.006, and
`intro_max_step` (0.01) bounds it in a test. The veil is opaque until the rest
point and then fades linearly, so the whole screen dissolves into the main
window over the last 0.8 s. `intro_timing` holds `brightness`, `lift` (how far
above rest, 0 to 1), `halo_gain`, `veil_alpha` and `finished` as pure functions
of elapsed time, and a `skipped` flag. Any key or click skips, so the length
(about 8.6 s) costs nobody anything.

## What is drawn

On the foreground draw list, so it covers every window: black, the desk
wallpaper tinted to `intro_wallpaper_grey` (38; the desk uses 97), a vignette,
then the ORE swirl and the word `HEDGR.` under it as one lockup. The swirl is
`intro_logo_fraction` (0.20) of the shorter viewport side. The word is set so
its width is `intro_word_width` (1.2) swirl widths, which puts its capitals at
about a fifth of the swirl's height. The lockup is centred horizontally and
`intro_lockup_lift` (0.04) of the viewport height above the vertical centre.

The reference is a soft bloom: a gaussian-like halo hugging a crisp icon, with
a bright tight core, fading smoothly into the dark. The first build stacked
scaled copies of a small sharp raster, which read as hard ghost outlines. The
glow is now genuinely blurred.

**Swirl.** `image::rasterize_svg_glow` rasterises the swirl at
`intro_glow_inner` (96) px, centres it on a transparent `intro_glow_raster`
(256) px canvas and blurs the alpha with a separable gaussian
(`image::blur_alpha`, kernel cut at three sigma). The intro asks the texture
source for three of them through `image_texture_source::svg_glow`, at the
sigmas in `intro_glow_sigmas` (3, 8, 20 px): tight, medium, wide. The cache
(`image_texture_cache`) builds each once and keeps it under its own key, so
a steady frame is three cache hits and nothing is computed. Each is drawn at
one fixed size, `raster/inner` times the swirl's, with the crisp swirl on top.

**Word.** The font atlas gives no CPU copy of a glyph at the size drawn, so
there is no mask to blur. The word's halo is the word drawn again in three rings
of copies at fixed tiny offsets (`intro_word_ring_copies`, 10, 12 and 14 copies
at 0.035, 0.08 and 0.15 of the font size) at very low alpha each, which sums to
a soft blur. It costs 37 text draws and no texture.

**Light.** Only the brightness varies, and nothing changes size, place or
radius at any time: no layer, ring offset or vignette depends on the time.

- The crisp swirl and word keep a fixed alpha (`intro_peak_intensity`, 0.8) and
  their colour runs from the rest grey-white `intro_rest_rgb` to the white-hot
  `intro_core_rgb` in step with the lift above rest. At rest nothing is blue.
- The halo's alpha is its peak (`intro_glow_alphas`) times the lift to the power
  `intro_halo_gamma` (2), so there is none at rest, it ignites late and fades
  early, as a neon tube does. The word's rings follow the same gain, and at rest
  neither the rings nor the halo textures are drawn.
- The halo's colour moves from the indigo `intro_halo_cold_rgb` as it first
  lights to its layer's peak colour at the peak: the core for the tight layer, shading
  toward the chart purple (`intro_glow_purple_mix`) for the wide one, so the top
  of the pulse has a faint wide purple haze.

The brightest channel of any vertex after its alpha is 0.8 at the peak, and a
test caps it at `max_peak_channel` (0.82). There is no shader and no additive
blend: the layers alpha-blend over a dark scene, which looks close to additive.
Steady frames allocate nothing.

**Checking it.** The hidden test `intro capture` renders frames through the
headless software rasteriser, with the real SVG rasterised and blurred, and
writes a PPM per sample time when `HEDGR_INTRO_PPM` names a directory:

    hedgr.ui.tests "intro capture"

The wallpaper is left out of the capture, so the scene is black under the
vignette.

## Sound

`assets/sounds/intro_swell.wav`, mono 44.1 kHz 16-bit, 8.623 s, raised as the
signal `application.intro` on the first intro frame. It is the engine swell
the operator chose, played twice. One swell is Kenney's `spaceEngineLow_000`
faded in over 2.8 s and out over the next 2.6 s, with
`lowFrequency_explosion_000` (both Sci-Fi Sounds, CC0) delayed to 2.8 s as the
impact, padded to 6 s with a 1.2 s fade out. The second copy is the same
samples at the same level, delayed by the pulse period (3.2 s) over the first
one's tail, then the sum is faded out over the last 1.2 s, ending with the veil,
and loudness-normalised. `tools/make_intro_sound.sh` is the ffmpeg recipe; it
matches the shape and level of the committed file, not byte for byte, since
loudnorm's output depends on the ffmpeg build.

`tools/measure_intro_peak.py` reports the loudest 50 ms window in each pulse's
search window: 3.023 s (-16.9 dBFS) and 6.223 s (-16.3 dBFS), exactly one period
apart. These fix `intro_peak_ns` and `intro_pulse_period_ns`. The second is
0.6 dB louder only because the first swell's tail adds to it; the copies
themselves are the same level. The visual rest point (7.823 s) and total
(8.623 s) follow from the second peak plus the fall and the veil, where the
audio has faded to silence. The first pulse's lead-in at rest is 1.423 s, set by
where the first swell's impact lands rather than chosen.

## Keeping the sound and the picture together

A report said the sound came about a second after the pulse. The old order
raised `application.intro` on the bus just before the frame loop and started the
visual clock on the same line; the bus was drained on the first frame, the
player queued the name, its worker called `ma_engine_play_sound`, which reads
and decodes the six second file on that call, and the first frames also built
the swirl texture, three blurs and the glyphs while the clock ran. The swell's
exponential fade-in makes its first second nearly silent too.

Now, on the first intro frame (`application::launch_intro`):

1. The player preloaded `intro_swell.wav` at startup (`player::preload`, a
   decoded `ma_sound`), so starting it is a seek and a start.
2. `warm_intro` builds the textures (the swirl and the three glows) before
   anything is timed.
3. `start_intro_clock` starts the sound on the calling thread
   (`player::start_now`), and only then reads the steady clock. The visual clock
   starts at that instant plus the output buffer's length (`output_latency_ns`)
   plus the offset: `intro_clock_start(audio_started_ns, offset_ns)`.

The intro's sound no longer travels on the bus: the name is still resolved by
`sound_for`, but delivery is synchronous, because a queue is the wrong tool for
a sound that has to line up with a picture.

**Tuning.** `intro_audio_offset_ms` (0) is the calibration and
`HEDGR_INTRO_AUDIO_OFFSET_MS` overrides it for one run, within 5000 ms either
way. Positive means the sound is heard later than the picture shows it, and the
picture starts that much later; negative brings the picture forward. If the
sound still seems late, raise it in steps of 100 and watch each pulse's peak
against its impact. The log shows where time goes. A run prints, at info:

    Intro audio: preloaded <path>/intro_swell.wav in X ms
    Intro warm-up: textures built in X ms
    Intro audio: ma_sound_start called at T1 ns, returned at T2 ns (X ms), result 0, engine time E ms
    Intro audio: output R Hz, period P frames x N periods, latency L ms
    Intro clock: sound requested at T0 ns, started at T2 ns (X ms), output latency L ms, offset O ms, visual clock starts at T3 ns
    Intro first frame: drawn at T4 ns, X ms after the visual clock started

The two ends to compare are the preload and warm-up costs, the time `ma_sound_start`
takes, the reported output latency and the gap between the clock start and the
first frame (a hitch there is the visual side running late).

## Ending and skipping

- Any key or mouse button ends it (`intro_skip_requested`). Wheel and gamepad do not.
- A store that failed to open ends it, so the notice is readable.
- An unlocking store does not: the unlock continues underneath.
- `HEDGR_NO_INTRO=1` disables it. A capture run skips it unless
  `HEDGR_CAPTURE_SHOW_INTRO` is set. When the intro is off the plain
  `application.started` sound plays instead.

## Removed

`assets/branding/ore-studio-splash.png`, `splash_timing`, `splash_image_path`,
`draw_splash`, the application's splash texture loader and the
`HEDGR_CAPTURE_SHOW_SPLASH` variable.
