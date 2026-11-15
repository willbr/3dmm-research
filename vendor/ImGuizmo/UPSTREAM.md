# ImGuizmo

Source: https://github.com/CedricGuillemet/ImGuizmo
Commit: `105e4fde45e4e74aae8973d98750820ef7aca7e2`
Archive: https://codeload.github.com/CedricGuillemet/ImGuizmo/tar.gz/105e4fde45e4e74aae8973d98750820ef7aca7e2
SHA256: `d625a9f3207f15b7a5b7706269747676f17b7e4df00f898ced9480e5063d493a`

Vendored subset: `src/ImGuizmo.cpp`, `src/ImGuizmo.h`,
upstream `README.md` and `LICENSE`. Other independent widgets, examples,
and sample dependencies are omitted. License: MIT; see `LICENSE`.

Local patch: `SetTranslationAxisMinimumSize(pixels)` keeps the original
translation arrows pickable by enlarging short projections along their actual
projected direction. Exactly end-on axes use a circular end cap at the original
projected origin. These handles use captured screen-space translation, while
ordinary handles retain the upstream ray/plane solver. Both solvers assign the
same translation delta; neither branch shadows it. Zero keeps upstream behavior.
Marcus also disables axis flipping so arrow directions represent positive axes.
