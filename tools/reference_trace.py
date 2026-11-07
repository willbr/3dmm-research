"""Reference-frame tracing, camera fitting and projected movement diagnostics.

Authoring-side only: no movie-format or runtime changes. Coordinates use image
pixels (origin top left), world Y up and a camera looking along local negative Z.
"""

from __future__ import annotations

import argparse
import base64
import copy
import json
import math
import pathlib

from mcp_rig import Pose, euler, inverse, quaternion, rotate


def _finite(values, length):
    result = tuple(float(v) for v in values)
    if len(result) != length or not all(math.isfinite(v) for v in result):
        raise ValueError(f"Expected {length} finite coordinates")
    return result


def _solve(matrix, rhs):
    """Small dense system, with scaled partial pivoting and singular rejection."""
    rows = [list(map(float, row)) + [float(value)] for row, value in zip(matrix, rhs)]
    n = len(rhs)
    scales = [max(map(abs, row[:n]), default=0) for row in rows]
    if any(not value for value in scales):
        raise ValueError("Degenerate geometry")
    for col in range(n):
        pivot = max(range(col, n), key=lambda j: abs(rows[j][col]) / scales[j])
        rows[col], rows[pivot] = rows[pivot], rows[col]
        scales[col], scales[pivot] = scales[pivot], scales[col]
        if abs(rows[col][col]) < scales[col] * 1e-12:
            raise ValueError("Degenerate geometry")
        value = rows[col][col]
        rows[col] = [v / value for v in rows[col]]
        for j in range(n):
            if j == col:
                continue
            value = rows[j][col]
            rows[j] = [a - value * b for a, b in zip(rows[j], rows[col])]
    return [row[-1] for row in rows]


def project(camera, point, focal, image_size):
    """Project a world point. Reject points on or behind the camera plane."""
    width, height = _finite(image_size, 2)
    focal = float(focal)
    if focal <= 0 or min(width, height) <= 0 or not math.isfinite(focal):
        raise ValueError("Positive finite focal length and image dimensions required")
    x, y, z = camera.inverted().point(_finite(point, 3))
    if z >= -1e-6:
        raise ValueError("Landmark is on or behind the camera plane")
    return (width / 2 + focal * x / -z, height / 2 - focal * y / -z)


def vanishing_point(segments):
    """Least-squares intersection, retaining a direction for parallel lines."""
    lines = []
    for segment in segments:
        (x, y), (u, v) = [_finite(p, 2) for p in segment]
        length = math.hypot(u - x, v - y)
        if length < 1e-8:
            raise ValueError("Perspective line has identical endpoints")
        lines.append(((y - v) / length, (u - x) / length, (x * v - u * y) / length))
    if len(lines) < 2:
        raise ValueError("At least two perspective lines required")
    a = sum(l[0] ** 2 for l in lines)
    b = sum(l[0] * l[1] for l in lines)
    c = sum(l[1] ** 2 for l in lines)
    if a * c - b * b < 1e-10 * (a + c) ** 2:
        return {"at_infinity": True, "direction": [-lines[0][1], lines[0][0]]}
    xy = _solve([[a, b], [b, c]], [-sum(l[0] * l[2] for l in lines), -sum(l[1] * l[2] for l in lines)])
    return {"at_infinity": False, "point": xy,
            "rms_px": math.sqrt(sum((l[0] * xy[0] + l[1] * xy[1] + l[2]) ** 2 for l in lines) / len(lines))}


def fit_camera(spec, max_iterations=150):
    """Fit six camera pose parameters from architectural points and axis lines.

    Known intrinsics and a plausible initial pose are required. Furniture and
    character tracks should be held out until the room camera is established.
    This is a local fit, not monocular reconstruction of an unknown 3D set.
    """
    focal = float(spec["focal"])
    width, height = _finite(spec["image_size"], 2)
    frame = spec.get("fit_frame", spec.get("first_frame", 1))
    marks = [m for m in spec["landmarks"] if m.get("visible", True) and m.get("world") is not None and m.get("reference") is not None and m.get("frame", frame) == frame]
    if len(marks) < 4:
        raise ValueError("At least four visible 3D-to-2D architectural landmarks required")
    points = [_finite(m["world"], 3) for m in marks]
    targets = [_finite(m["reference"], 2) for m in marks]
    weights = [float(m.get("weight", 1)) for m in marks]
    if any(not math.isfinite(w) or w <= 0 for w in weights):
        raise ValueError("Landmark weights must be positive and finite")
    vectors = [tuple(p[j] - points[0][j] for j in range(3)) for p in points[1:]]
    area = max(sum((a[(j + 1) % 3] * b[(j + 2) % 3] - a[(j + 2) % 3] * b[(j + 1) % 3]) ** 2 for j in range(3)) for a in vectors for b in vectors)
    if area < 1e-12:
        raise ValueError("Architectural landmarks are collinear")
    lines = []
    axes = {"x": (1, 0, 0), "y": (0, 1, 0), "z": (0, 0, 1)}
    for line in spec.get("lines", []):
        if line.get("surface", "reference") != "reference" or not line.get("enabled", True) or line.get("frame", frame) != frame:
            continue
        (x, y), (u, v) = [_finite(p, 2) for p in line["points"]]
        a, b, c = y - v, u - x, x * v - u * y
        normal = (a * focal, -b * focal, -a * width / 2 - b * height / 2 - c)
        length = math.sqrt(sum(v * v for v in normal))
        if length < 1e-8:
            raise ValueError("Perspective line has identical endpoints")
        weight = float(line.get("weight", 1))
        if weight <= 0 or not math.isfinite(weight):
            raise ValueError("Line weights must be positive and finite")
        lines.append((axes[line["axis"]], tuple(v / length for v in normal), weight))
    initial = spec["initial_camera"]
    values = list(_finite(initial["position"], 3)) + [math.radians(v) for v in _finite(initial["rotation"], 3)]

    def pose(v):
        return Pose.xyz(v[:3], [math.degrees(a) for a in v[3:]])

    def residual(v):
        camera = pose(v)
        result = []
        for point, target, weight in zip(points, targets, weights):
            actual = project(camera, point, focal, (width, height))
            result.extend((a - b) * math.sqrt(weight) for a, b in zip(actual, target))
        for axis, normal, weight in lines:
            direction = rotate(inverse(camera.rotation), axis)
            result.append(focal * sum(a * b for a, b in zip(direction, normal)) * math.sqrt(weight))
        return result

    errors = residual(values)
    initial_cost = cost = sum(v * v for v in errors)
    damping = .001
    iterations = 0
    for iterations in range(1, max_iterations + 1):
        columns = []
        for j in range(6):
            step = 1e-4 if j < 3 else 1e-6
            delta = values.copy()
            delta[j] += step
            changed = residual(delta)
            columns.append([(a - b) / step for a, b in zip(changed, errors)])
        normal = [[sum(a * b for a, b in zip(x, y)) for y in columns] for x in columns]
        gradient = [sum(a * b for a, b in zip(column, errors)) for column in columns]
        for j in range(6):
            normal[j][j] += damping * max(normal[j][j], 1e-8)
        step = _solve(normal, [-g for g in gradient])
        candidate = [v + d for v, d in zip(values, step)]
        try:
            changed = residual(candidate)
            candidate_cost = sum(v * v for v in changed)
        except ValueError:
            candidate_cost = math.inf
        if candidate_cost < cost:
            improvement = cost - candidate_cost
            values, errors, cost = candidate, changed, candidate_cost
            damping = max(1e-10, damping / 3)
            if improvement < 1e-10 * (1 + cost) or max(map(abs, step)) < 1e-8:
                break
        else:
            damping *= 8
            if damping > 1e12:
                break
    camera = pose(values)
    fitted = []
    for mark, point, target in zip(marks, points, targets):
        actual = project(camera, point, focal, (width, height))
        fitted.append(dict(name=mark["name"], reference=list(target), projected=list(actual), error_px=math.dist(actual, target)))
    return dict(position=list(camera.position), rotation=list(euler(camera.rotation)),
                focal=focal, image_size=[width, height], iterations=iterations,
                initial_cost=initial_cost, final_cost=cost,
                rms_px=math.sqrt(sum(m["error_px"] ** 2 for m in fitted) / len(fitted)),
                max_error_px=max(m["error_px"] for m in fitted), landmarks=fitted)


def sample_track(track, frame, max_gap=1, cuts=()):
    """Interpolate only between visible nearby samples; never fill occlusions."""
    keys = sorted(track["keys"], key=lambda k: k["frame"])
    for key in keys:
        if key["frame"] == frame:
            return copy.deepcopy(key) if key.get("visible", True) else None
    for a, b in zip(keys, keys[1:]):
        if a["frame"] < frame < b["frame"]:
            if b["frame"] - a["frame"] > max_gap or not (a.get("visible", True) and b.get("visible", True)) or any(a["frame"] < cut <= b["frame"] for cut in cuts):
                return None
            t = (frame - a["frame"]) / (b["frame"] - a["frame"])
            result = dict(frame=frame, visible=True, interpolated=True)
            for field in ("reference", "rendered", "world"):
                if a.get(field) is not None and b.get(field) is not None:
                    result[field] = [x + (y - x) * t for x, y in zip(a[field], b[field])]
            return result
    return None


def movement_report(spec):
    """Compare traced reference joints to native projections or traced output."""
    frames = sorted({k["frame"] for t in spec["tracks"] for k in t["keys"]})
    camera_spec = spec.get("camera")
    camera = Pose.xyz(camera_spec["position"], camera_spec["rotation"]) if camera_spec else None
    checks = []
    for track in spec["tracks"]:
        previous = None
        for frame in frames:
            key = sample_track(track, frame, spec.get("max_gap", 1), spec.get("cuts", ()))
            if key is None or key.get("reference") is None:
                previous = None
                continue
            expected = _finite(key["reference"], 2)
            actual = key.get("rendered")
            if key.get("world") is not None and camera:
                actual = project(camera, key["world"], spec["focal"], spec["image_size"])
            if actual is None:
                previous = None
                continue
            actual = _finite(actual, 2)
            item = dict(joint=track["name"], frame=frame, reference=expected, rendered=actual,
                        error_px=math.dist(expected, actual), contact=key.get("contact", False))
            if previous and frame - previous[0] <= spec.get("max_gap", 1) and not any(previous[0] < cut <= frame for cut in spec.get("cuts", ())):
                dt = (frame - previous[0]) / spec.get("fps", 6)
                item["velocity_error_px_per_second"] = math.dist(
                    [(a - b) / dt for a, b in zip(expected, previous[1])],
                    [(a - b) / dt for a, b in zip(actual, previous[2])])
                if key.get("contact") and previous[3]:
                    item["rendered_contact_motion_px"] = math.dist(actual, previous[2])
            previous = (frame, expected, actual, key.get("contact", False))
            checks.append(item)
    return dict(checks=checks, count=len(checks),
                rms_px=math.sqrt(sum(c["error_px"] ** 2 for c in checks) / len(checks)) if checks else None,
                max_error_px=max((c["error_px"] for c in checks), default=None))


def write_viewer(spec, destination, reference=None, rendered=None):
    data = copy.deepcopy(spec)
    for field, path in (("reference_image", reference), ("rendered_image", rendered)):
        if path:
            path = pathlib.Path(path)
            mime = "image/jpeg" if path.suffix.lower() in (".jpg", ".jpeg") else "image/png"
            data[field] = f"data:{mime};base64," + base64.b64encode(path.read_bytes()).decode("ascii")
    template = pathlib.Path(__file__).with_name("reference_trace.html").read_text(encoding="utf-8")
    encoded = json.dumps(data, allow_nan=False).replace("<", "\\u003c")
    pathlib.Path(destination).write_text(template.replace("/*PROJECT*/{}", encoded), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=pathlib.Path)
    parser.add_argument("--fit", type=pathlib.Path, help="Write camera fit JSON")
    parser.add_argument("--motion", type=pathlib.Path, help="Write projected joint-error JSON")
    parser.add_argument("--html", type=pathlib.Path, help="Write interactive landmark/line/joint tracer")
    parser.add_argument("--reference", type=pathlib.Path)
    parser.add_argument("--rendered", type=pathlib.Path)
    args = parser.parse_args()
    spec = json.loads(args.project.read_text(encoding="utf-8"))
    if args.fit:
        result = fit_camera(spec)
        args.fit.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f'Camera fit: {result["rms_px"]:.2f}px RMS, {result["max_error_px"]:.2f}px maximum')
        spec["fit"] = result
    if args.motion:
        result = movement_report(spec)
        args.motion.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f'Movement comparisons: {result["count"]}')
    if args.html:
        write_viewer(spec, args.html, args.reference, args.rendered)


if __name__ == "__main__":
    main()
