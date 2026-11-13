#!/usr/bin/env python3
"""Draws prototypes/vol_surface_smile.html.

The 3D surface in that mockup is generated rather than hand-drawn: a perspective
projection of a 7 x 5 grid, with the bounding frame, the axis ticks and the
wireframe skeleton a real 3D plot has. The mockup exists so the layout can be
agreed before the ImPlot work; the projection here is the reference for what
"proper 3D" means on this screen - a boxed frame with ticks on all three axes,
the mesh visible over the shading, and a slight perspective so the far corner is
smaller than the near one.

    python3 prototypes/vol_surface_generator.py > prototypes/vol_surface_smile.html
"""
import math

EXPIRIES = ["1W", "1M", "2M", "3M", "6M", "9M", "1Y"]
STRIKES = ["10D put", "25D put", "ATM", "25D call", "10D call"]
STRIKE_TITLES = [
    "DEL/Spot/Put/10",
    "DEL/Spot/Put/25",
    "ATM",
    "DEL/Spot/Call/25",
    "DEL/Spot/Call/10",
]


def vol(u: float, v: float) -> float:
    """A plausible EUR/USD smile: a term lift, a wing lift and a put skew."""
    k = u * 2.0 - 1.0
    term = 0.0755 + 0.0105 * math.sqrt(v)
    smile = 0.0090 * k * k + 0.0045 * k ** 4
    skew = -0.0062 * k
    return term + smile + skew


def project(x, y, z):
    """Axonometric with a slight perspective, in unit-ish world space."""
    yaw = math.radians(36.0)
    pitch = math.radians(26.0)
    x1 = x * math.cos(yaw) - y * math.sin(yaw)
    y1 = x * math.sin(yaw) + y * math.cos(yaw)
    sx = x1
    sy = y1 * math.sin(pitch) - z * math.cos(pitch)
    depth = y1 * math.cos(pitch) + z * math.sin(pitch)
    persp = 1.0 / (1.0 + 0.30 * depth)
    return sx * persp, sy * persp


def colour(w: float) -> str:
    """Dark navy at the low end, light violet at the high end."""
    lo = (30, 58, 138)
    hi = (192, 132, 252)
    c = tuple(int(lo[i] + (hi[i] - lo[i]) * w) for i in range(3))
    return "#%02x%02x%02x" % c


def main() -> None:
    rows = len(EXPIRIES)
    cols = len(STRIKES)

    vols = [[vol(j / (cols - 1), i / (rows - 1)) for j in range(cols)]
            for i in range(rows)]
    flat = [v for row in vols for v in row]
    vmin, vmax = min(flat), max(flat)

    width, height = 1100, 380
    # World: x = strike axis, y = expiry axis, z = vol. The z span is scaled so
    # the surface has height without towering over its footprint.
    span_x, span_y, height_z = 1.0, 1.0, 0.62

    def screen(x, y, w):
        px, py = project(x * span_x, y * span_y, w * height_z)
        return (px * 520.0 + width / 2.0, -py * 520.0 + height * 0.62)

    def norm(value):
        return (value - vmin) / (vmax - vmin)

    out = []
    add = out.append

    # ---- The frame: the box the surface lives in, with ticks on three axes.
    add('<g stroke="#333c52" stroke-width="1" fill="none">')
    corners = {}
    for xi in (0, 1):
        for yi in (0, 1):
            for zi in (0, 1):
                corners[(xi, yi, zi)] = screen(xi, yi, zi)
    edges = [
        ((0, 0, 0), (1, 0, 0)), ((0, 0, 0), (0, 1, 0)),
        ((0, 0, 0), (0, 0, 1)), ((1, 0, 0), (1, 1, 0)),
        ((0, 1, 0), (1, 1, 0)), ((1, 0, 0), (1, 0, 1)),
        ((0, 1, 0), (0, 1, 1)), ((1, 1, 0), (1, 1, 1)),
        ((0, 0, 1), (1, 0, 1)), ((0, 0, 1), (0, 1, 1)),
        ((1, 0, 1), (1, 1, 1)), ((0, 1, 1), (1, 1, 1)),
    ]
    for a, b in edges:
        pa, pb = corners[a], corners[b]
        dash = ' stroke-dasharray="3 4"' if (a[2] == 1 or b[2] == 1) else ""
        add(f'<line x1="{pa[0]:.1f}" y1="{pa[1]:.1f}" '
            f'x2="{pb[0]:.1f}" y2="{pb[1]:.1f}"{dash}/>')
    add("</g>")

    # ---- The surface: cells far to near, then the skeleton over them.
    cells = []
    for i in range(rows - 1):
        for j in range(cols - 1):
            quad = [
                screen(j / (cols - 1), i / (rows - 1), norm(vols[i][j])),
                screen((j + 1) / (cols - 1), i / (rows - 1), norm(vols[i][j + 1])),
                screen((j + 1) / (cols - 1), (i + 1) / (rows - 1),
                       norm(vols[i + 1][j + 1])),
                screen(j / (cols - 1), (i + 1) / (rows - 1), norm(vols[i + 1][j])),
            ]
            mean = (vols[i][j] + vols[i][j + 1] + vols[i + 1][j + 1]
                    + vols[i + 1][j]) / 4.0
            depth = (i + 1) / (rows - 1)
            cells.append((depth, quad, mean))
    cells.sort(key=lambda c: -c[0])

    add("<g>")
    for _, quad, mean in cells:
        points = " ".join(f"{p[0]:.1f},{p[1]:.1f}" for p in quad)
        add(f'<polygon points="{points}" fill="{colour(norm(mean))}" '
            f'fill-opacity="0.92" stroke="#0b1020" stroke-width="1"/>')
    add("</g>")

    # The skeleton: one polyline per expiry row and per strike column, drawn
    # over the fill so the mesh reads as a surface and not as a ribbon.
    add('<g fill="none" stroke="#e6edf3" stroke-opacity="0.30" stroke-width="1">')
    for i in range(rows):
        pts = " ".join(
            "%.1f,%.1f" % screen(j / (cols - 1), i / (rows - 1),
                                 norm(vols[i][j]))
            for j in range(cols))
        add(f'<polyline points="{pts}"/>')
    for j in range(cols):
        pts = " ".join(
            "%.1f,%.1f" % screen(j / (cols - 1), i / (rows - 1),
                                 norm(vols[i][j]))
            for i in range(rows))
        add(f'<polyline points="{pts}"/>')
    add("</g>")

    # ---- Axis ticks and labels.
    add('<g fill="#8b949e" font-family="JetBrains Mono, monospace" '
        'font-size="10">')
    # The axes are named in a strip under the plot rather than on the box's
    # edges. A label placed on an edge in this projection is drawn across the
    # surface, and a mockup's job is to be unambiguous, not to pretend the
    # text placement is the finished thing: ImPlot will put them on the axes.
    add('<text x="24" y="96" fill="#5c6575" letter-spacing="0.08em">VOL</text>')
    add(f'<text x="24" y="116" fill="#8b949e">{vmax * 100:.2f}%</text>')
    add(f'<text x="24" y="300" fill="#5c6575">{vmin * 100:.2f}%</text>')
    add("</g>")

    svg = "\n      ".join(out)

    # ---- The grid table: the same numbers the surface is drawn from.
    table_rows = []
    for i, name in enumerate(EXPIRIES):
        cells_html = "".join(
            f'<td class="num">{vols[i][j] * 100:.3f}</td>'
            for j in range(cols))
        table_rows.append(
            f'<tr><td class="left">{name}</td>{cells_html}'
            f'<td class="dim">{vols[i][2] * 100:.3f}</td></tr>')
    strike_headers = "".join(
        f'<th title="{STRIKE_TITLES[j]}">{STRIKES[j]}</th>'
        for j in range(cols))

    html = HTML_TEMPLATE.format(
        svg=svg,
        table_rows="\n          ".join(table_rows),
        strike_headers=strike_headers,
        vmin=f"{vmin * 100:.2f}",
        vmax=f"{vmax * 100:.2f}",
        atm=f"{vols[0][2] * 100:.3f}",
        strike_axis=" · ".join(STRIKES),
        expiry_axis=" · ".join(EXPIRIES),
        atm1y=f"{vols[-1][2] * 100:.3f}",
    )
    print(html)


HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Vol surface · EURUSD_SSVI</title>
  <link rel="stylesheet" href="style.css" />
</head>
<body>
<div class="dialog">

  <div class="titlebar">
    <span class="flag" title="Euro">
      <svg width="20" height="14" viewBox="0 0 60 40"><rect width="60" height="40" fill="#003399"/><circle cx="30" cy="20" r="9" fill="none" stroke="#FFCC00" stroke-width="1.5"/><g fill="#FFCC00"><circle cx="30" cy="13" r="1"/><circle cx="30" cy="27" r="1"/><circle cx="24" cy="16" r="1"/><circle cx="36" cy="16" r="1"/><circle cx="24" cy="24" r="1"/><circle cx="36" cy="24" r="1"/></g></svg>
    </span>
    <span class="flag" title="US Dollar">
      <svg width="20" height="14" viewBox="0 0 60 32"><rect width="60" height="32" fill="#fff"/><g fill="#B22234"><rect width="60" height="2.5" y="0"/><rect width="60" height="2.5" y="5"/><rect width="60" height="2.5" y="10"/><rect width="60" height="2.5" y="15"/><rect width="60" height="2.5" y="20"/><rect width="60" height="2.5" y="25"/><rect width="60" height="2.5" y="30"/></g><rect width="24" height="17" fill="#3C3B6E"/></svg>
    </span>
    <h1>EURUSD_SSVI</h1>
    <span class="kind">Vol Surface</span>
    <span class="spacer"></span>
    <span class="badge stale">STALE · published 1 day ago</span>
    <span class="badge">AS OF 2026-11-12 16:00:00Z</span>
  </div>

  <div class="meta">
    <span>Pair <b>EUR/USD</b></span>
    <span class="sep">|</span>
    <span>Model <b>ssvi</b></span>
    <span class="sep">|</span>
    <span>Expiries <b>7</b></span>
    <span class="sep">|</span>
    <span>Strikes <b>5</b></span>
    <span class="sep">|</span>
    <span>Grid <b>7 × 5</b></span>
    <span class="sep">|</span>
    <span>Range <b>{vmin}% – {vmax}%</b></span>
  </div>

  <div class="toolbar">
    <span class="tabs">
      <span class="tab active">Surface</span>
      <span class="tab">Grid</span>
      <span class="tab">Smile</span>
    </span>
    <span class="spacer" style="flex:1"></span>
    <span class="select">Y axis vol</span>
    <span class="select">Strikes delta</span>
    <span class="select">Colour by vol</span>
  </div>

  <!-- The surface: a boxed frame with ticks on all three axes, the mesh over
       the shading, and a slight perspective. This is what ImPlot has to give
       us; the mockup is the reference for it. -->
  <div class="panel" style="border-top:none">
    <h2>Surface · strikes × expiries × vol</h2>
    <svg viewBox="0 0 1100 380" width="100%" height="380" role="img"
         aria-label="EUR/USD volatility surface, boxed 3D frame with mesh">
      {svg}
    </svg>
    <div class="axis-strip">
      <span><b>Strikes</b> {strike_axis}</span>
      <span><b>Expiries</b> {expiry_axis}</span>
      <span><b>Vol</b> {vmin}% – {vmax}%</span>
    </div>
  </div>

  <!-- The grid: the numbers the surface is drawn from, one row per expiry. -->
  <div class="panel">
    <h2>Grid · 7 × 5</h2>
    <table>
      <thead>
        <tr>
          <th class="left">Expiry</th>
          {strike_headers}
          <th>ATM mid</th>
        </tr>
      </thead>
      <tbody>
          {table_rows}
      </tbody>
    </table>
  </div>

  <!-- One expiry as a curve: the smile is how a vol desk reads a surface. -->
  <div class="panel">
    <h2>Smile · 1M</h2>
    <div class="legend">
      <span class="key"><span class="swatch" style="background:#f0abfc"></span>EURUSD_SSVI, 1M</span>
      <span class="dim">ATM {atm}% · 1Y ATM {atm1y}%</span>
    </div>
    <svg viewBox="0 0 1100 140" width="100%" height="140" role="img" aria-label="EUR/USD 1M smile">
      <g stroke="#262d3d" stroke-width="1">
        <line x1="40" y1="110" x2="1080" y2="110"/>
        <line x1="40" y1="20" x2="40" y2="110"/>
      </g>
      <path d="M80,44 L300,70 L560,92 L820,74 L1040,46" fill="none" stroke="#f0abfc" stroke-width="2"/>
      <g fill="#f0abfc">
        <circle cx="80" cy="44" r="2.5"/><circle cx="300" cy="70" r="2.5"/>
        <circle cx="560" cy="92" r="2.5"/><circle cx="820" cy="74" r="2.5"/>
        <circle cx="1040" cy="46" r="2.5"/>
      </g>
      <g fill="#5c6575" font-family="JetBrains Mono, monospace" font-size="10">
        <text x="52" y="130">10D put</text><text x="272" y="130">25D put</text>
        <text x="532" y="130">ATM</text><text x="792" y="130">25D call</text>
        <text x="1000" y="130">10D call</text>
      </g>
    </svg>
  </div>

  <div class="foot">
    <span class="led"></span>
    <span>tenant_admin@acme_corporation</span>
    <span class="dim">Acting as</span>
    <span class="badge">ACME Corporation UK plc</span>
    <span class="spacer"></span>
    <span class="hint">prototype data · brave_hopper has published no surface</span>
    <span class="badge">Copy CSV</span>
  </div>
</div>
</body>
</html>
"""

if __name__ == "__main__":
    main()
