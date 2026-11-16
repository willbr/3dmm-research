# Ok color

Bjorn Ottosson's C++ reference conversions for Oklab, OkHSL and OkHSV.

Source: https://bottosson.github.io/misc/ok_color.h

Explanation: https://bottosson.github.io/posts/colorpicker/

Upstream SHA256: `c4177a8c7b9363e83c7cc1125c0d6979f78c9c666f28c0860487528d15637125`

License: MIT; the notice is retained in the header and in
`docs/licenses/ok-color-LICENSE.txt`. The upstream header uses a Latin-1 copyright
comment; its notice is preserved.

Marcus includes it only from `src/marcus/colour_picker.cpp`.

Local accuracy changes: conversion intermediates use double precision instead of
float, and `compute_max_saturation` uses three Halley iterations (as suggested
by the upstream comment). This avoids large round-trip errors at the blue cusp
caused by float precision and the one-iteration approximation. No algorithm or
colour-space definitions were changed.
