# Reference perspective and movement tracing

[`tools/reference_trace.py`](../../tools/reference_trace.py) creates a local,
interactive frame tracer and measures camera and movement errors. It uses the
Python standard library and the script-side rig math. It never edits a movie or
introduces runtime requirements.

```powershell
python -B tools/reference_trace.py tmp/project.json --html tmp/trace.html
python -B tools/reference_trace.py tmp/project.json --fit tmp/camera-fit.json
python -B tools/reference_trace.py tmp/project.json --motion tmp/motion-errors.json
```

Use `--reference image.png --rendered saved-frame.png` to embed a single pair of
frames in the HTML. For a sequence, provide `reference_pattern` and
`rendered_pattern`, such as `frames/frame-{frame:05}.png`, relative to the HTML.
`first_frame`, `last_frame` and `fps` control stepping. The tracer can also open
individual images and import or export project JSON. Its annotations remain
local; export them to retain edits.

Architectural points and lines can include a `frame`. `fit_frame` selects the
frame for a camera fit (default: `first_frame`, or 1). Other frames are excluded.

Camera input:

```json
{
  "image_size": [544, 306],
  "focal": 404,
  "initial_camera": {"position": [0, 20, 20], "rotation": [0, 0, 0]},
  "landmarks": [
    {"name": "door bottom left", "world": [-10, 0, -40], "reference": [205, 285]},
    {"name": "door bottom right", "world": [10, 0, -40], "reference": [339, 285]},
    {"name": "door top left", "world": [-10, 30, -40], "reference": [205, 86]},
    {"name": "door top right", "world": [10, 30, -40], "reference": [339, 86]}
  ],
  "lines": [
    {"surface": "reference", "axis": "y", "points": [[205, 285], [205, 86]]}
  ]
}
```

Coordinates use original image pixels, Y-up world space, XYZ Euler degrees and
a camera looking along local negative Z. Focal length is in pixels. At least
four non-collinear, visible 3D-to-2D correspondences and a plausible initial
camera are required. Optional axis lines constrain the camera's orientation.
The output lists each projected landmark and its error, not just an aggregate.
`vanishing_point()` handles finite intersections and parallel lines at infinity.

Establish the camera from fixed architecture first. Then place furniture against
held-out landmarks. Fitting the camera to a misplaced chair or a character with
incorrect proportions can conceal the actual error. A single image does not
uniquely recover an unknown set's dimensions, focal length and camera pose;
record inferred dimensions separately from observed image points. The solver
keeps focal length fixed and performs a local pose fit. A low residual does not
prove correct geometry, viewpoint or occlusion.

For movement, add named `tracks`, optional `bones` pairs, and sampled keys:

```json
{
  "image_size": [544, 306], "fps": 6, "max_gap": 1,
  "first_frame": 1, "last_frame": 3,
  "bones": [["elbow", "wrist"]],
  "tracks": [
    {"name": "wrist", "keys": [
      {"frame": 1, "reference": [120, 90], "rendered": [130, 110]},
      {"frame": 2, "visible": false},
      {"frame": 3, "reference": [125, 85], "rendered": [140, 105]}
    ]}
  ]
}
```

Mark `contact: true` on planted foot samples. Hidden joints use `visible: false`;
the sampler never extrapolates or interpolates through occlusion. Interpolation
only spans visible samples within `max_gap` frames. A key can contain a `world`
point instead of `rendered`; provide `camera`, `focal` and `image_size` to project
it. Use `inspect_actor.position` for saved actor world coordinates, not the
route-relative `get_state.position`.

List cut-start frame numbers in `cuts`. Interpolation and velocity comparisons
never cross those boundaries, even when samples are adjacent in the file.

Movement reports include joint displacement, velocity errors and screen motion
during marked contact. Compare reference and output in the same camera; screen
motion during a camera move alone does not establish foot sliding. These traces
measure the output. They do not automatically build joints, infer hidden depth,
retarget a rig or solve grip and ground contacts.

Run `python -B tests/reference_trace_test.py` (CTest `reference-trace`) for the
synthetic pose recovery, perspective, occlusion, motion and HTML escaping checks.
Keep source footage, frame annotations and generated viewers under ignored
`tmp/`; commit only reusable tools, documentation and synthetic tests.
