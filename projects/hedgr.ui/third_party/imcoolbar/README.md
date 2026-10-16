# ImCoolBar, vendored

ImCoolBar is vendored from https://github.com/aiekick/ImCoolBar at commit
`20a82095aea5ba64f2f286ef27043d352c8264a3`. `ImCoolBar.h`, `ImCoolBar.cpp`
and `LICENSE-ImCoolBar.txt` are upstream's files as fetched from that commit,
with one local change to `ImCoolBar.cpp`. The licence file is untouched.

## The local change

`0001-hover-and-layout.patch` is the whole of it. Applying that patch to the
upstream file at the named commit reproduces our copy byte for byte.

Upstream decides whether the pointer is over the bar by testing the bar's
window, and the window grows to the size of the magnified entries. That is the
source of two defects.

- The hover test asked the window. A window that grows with the magnification
  holds the bar open for a pointer that never came near the resting bar, and
  the growing rectangle can move out from under a pointer that is standing
  still. The patch tests the resting bar instead, computed from the entry
  count, the normal entry size, the window padding and the item spacing.
- The pin that keeps the pixel under the cursor fixed modelled the resting bar
  with no space between its entries. ImGui does put space between them, so the
  pin's idea of where the bar sits was half the total gap away from where
  ImGui actually put it. Whenever the animation left or returned to zero the
  bar slid by that gap, which flipped the hover test and made the bar flicker
  between two positions. The patch adds the item spacing to the base layout
  wherever the pin depends on it.

Together these read as a two frame limit cycle in the strip at the bar's
leading edge. A headless ImGui driver over a grid of parked pointer positions
found 91 flickering positions before the change and none after it.

## Re-deriving the change

Fetch upstream at the named commit and apply the patch from this directory.

    curl -fsSL https://raw.githubusercontent.com/aiekick/ImCoolBar/20a82095aea5ba64f2f286ef27043d352c8264a3/ImCoolBar.cpp -o ImCoolBar.cpp
    patch -p1 < 0001-hover-and-layout.patch

Everything outside the patch is upstream's, including the header comment at the
top of `ImCoolBar.cpp` and `ImCoolBar.h`.
