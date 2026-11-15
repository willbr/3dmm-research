# ImGuizmo

Source: https://github.com/CedricGuillemet/ImGuizmo
Commit: `105e4fde45e4e74aae8973d98750820ef7aca7e2`
Archive: https://codeload.github.com/CedricGuillemet/ImGuizmo/tar.gz/105e4fde45e4e74aae8973d98750820ef7aca7e2
SHA256: `d625a9f3207f15b7a5b7706269747676f17b7e4df00f898ced9480e5063d493a`

Vendored subset: `src/ImGuizmo.cpp`, `src/ImGuizmo.h`,
upstream `README.md` and `LICENSE`. Other independent widgets, examples,
and sample dependencies are omitted. License: MIT; see `LICENSE`.

Local patch: `SetKeepTranslationAxesVisible(true)` opts into attached depth arrows
for translation axes hidden by foreshortening. Drawing and picking use the same
arrow geometry. End-on drags use a captured screen direction and world scale,
so parallel ray/plane intersections cannot produce jumps or NaNs. Normal axes,
plane handles, snapping and the upstream default behavior remain unchanged.
