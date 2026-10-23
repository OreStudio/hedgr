# Launch intro

The intro replaces the splash. It plays once, at launch, over the main window.

## Timeline

The intro is one slow pulse of light, not a flash. All times are from the first
frame the intro is drawn. The constants live in `hedgr.ui/render/intro.hpp`; the
look constants live in `hedgr.ui/theme/theme.hpp`.

| Time | Brightness and expansion | Veil |
| --- | --- | --- |
| 0.0 s | 0 (dark, glow contracted) | opaque |
| 2.795 s (`intro_peak_ns`) | 1 (brightest, glow fully spread) | opaque |
| 5.2 s (`intro_dark_ns`) | 0 (dark again) | opaque |
| 6.0 s (`intro_total_ns`) | 0 | 0, the main window is fully shown |

Brightness is a smoothstep up to the peak and a smoothstep down to the dark
point: the slope is zero at the start, the peak and the dark point, so there is
no jump, no flicker and one maximum. The steepest step between samples 10 ms
apart is about 0.006, and `intro_max_step` (0.01) bounds it in a test.
`expansion(elapsed)` follows the brightness exactly. The veil is opaque until
the dark point and then fades linearly. `intro_timing` holds `brightness`,
`expansion`, `glow_scale`, `veil_alpha` and `finished` as pure functions of
elapsed time, and a `skipped` flag. Any key or click skips, so the length costs
nobody anything.

## What is drawn

On the foreground draw list, so it covers every window: black, the desk
wallpaper tinted to `intro_wallpaper_grey` (38; the desk uses 97), a radial
halo, a vignette, then the ORE swirl and the word `HEDGR.` under it. The swirl
is `intro_logo_fraction` of the shorter viewport side.

The glow is `intro_glow_layers` larger copies of a 40 px raster of the swirl
(the bilinear stretch blurs it) with falling alpha and a tint that moves from
cyan-white toward the chart purple, then the crisp swirl on top. The word uses
the same layers as larger font sizes of `icon::bold_font()`, scaled about the
text's centre. The glow expands outwards with the pulse: a layer's size is
`1 + (intro_glow_scales[i] - 1) * spread`, where spread runs from
`intro_glow_rest` (0.15) when dark to 1 at the peak, and the halo's radius
spreads the same way. The layers thin by `intro_glow_expand_fade` as they
spread. The crisp swirl and word never change size.

The first version blew out: the crisp shapes and the layers stacked to a clipped
white. Now the crisp shapes draw at `intro_peak_intensity` (0.8, was 1.0), the
layer alphas at the peak are 0.30 to 0.035 (were 0.55 to 0.07) and the halo is
0.16 (was 0.5). The brightest channel of any vertex after its alpha is 0.8
at the peak (was 1.0), and a test caps it at `max_peak_channel` (0.82).

There is no shader and no additive blend: the layers alpha-blend over a dark
scene, which looks close to additive. Steady frames allocate nothing.

## Sound

`assets/sounds/intro_swell.wav`, mono 44.1 kHz 16-bit, 6.0 s, raised as the
signal `application.intro` on the first intro frame. Kenney's Sci-fi Sounds has
no swell, so `tools/make_intro_sound.py` synthesises a rising noise and sine
sweep that grows slowly over 2.57 s and layers Kenney's
`lowFrequency_explosion_000` (CC0) as the impact, then lets the swell die away
through the dimming. The impact starts at 2.568 s and the loudest sample of the
file is sample 123281, at 2.7955 s (measured), which fixes `intro_peak_ns`.
Change one and rerun the script with the other. The sample's position moves a
few milliseconds with the impact's start, so measure rather than add.

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
