# Launch intro

The intro replaces the splash. It plays once, at launch, over the main window.

## Timeline

All times are from the first frame the intro is drawn. The constants live in
`hedgr.ui/render/intro.hpp`; the look constants live in `hedgr.ui/theme/theme.hpp`.

| Time | Brightness | Veil |
| --- | --- | --- |
| 0.0 s | 0 (dark) | opaque |
| 1.4 s (`intro_peak_ns`) | 1 (brightest) | opaque |
| 2.4 s (`intro_dark_ns`) | 0 (dark again) | opaque |
| 3.2 s (`intro_total_ns`) | 0 | 0, the main window is fully shown |

Brightness is `x*x` on the way up and on the way down, so the glow gathers and
then lands, as the sound does. The veil is opaque until the dark point and then
fades linearly. `intro_timing` holds `brightness`, `veil_alpha` and `finished`
as pure functions of elapsed time, and a `skipped` flag.

## What is drawn

On the foreground draw list, so it covers every window: black, the desk
wallpaper tinted to `intro_wallpaper_grey` (the desk uses 97), a radial halo,
a vignette, then the ORE swirl and the word `HEDGR.` under it. The swirl is
`intro_logo_fraction` of the shorter viewport side. The glow is
`intro_glow_layers` larger copies of a 40 px raster of the swirl (the bilinear
stretch blurs it) with falling alpha and a tint that moves from cyan-white
toward the chart purple, then the crisp swirl on top. The word uses the same
layers as larger font sizes of `icon::bold_font()`. There is no shader and no
additive blend: the layers alpha-blend over a dark scene, which looks close to
additive. Steady frames allocate nothing.

## Sound

`assets/sounds/intro_swell.wav`, mono 44.1 kHz, 3.2 s, raised as the signal
`application.intro` on the first intro frame. Kenney's Sci-fi Sounds has no
swell, so `tools/make_intro_sound.py` synthesises a rising noise and sine sweep
and layers Kenney's `lowFrequency_explosion_000` (CC0) as the impact. The
impact starts at 1.2 s and its loudest sample is at 1.43 s (measured), which
fixes `intro_peak_ns`. Change one and rerun the script with the other.

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
