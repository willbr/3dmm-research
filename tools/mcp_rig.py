"""Script-side groups, joints and keyframes baked into ordinary MCP prop edits.

No rig data enters the movie format. Hierarchies support uniform group scale;
per-axis dimensions belong to leaf props, avoiding ambiguous sheared transforms.
Rotation is world XYZ Euler degrees at the MCP boundary, quaternions internally.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from bisect import bisect_right
import math


def vector(value):
    result = tuple(float(v) for v in value)
    if len(result) != 3 or not all(math.isfinite(v) for v in result):
        raise ValueError("Expected three finite coordinates")
    return result


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def multiply(a, b):
    w, x, y, z = a
    v, i, j, k = b
    return (
        w * v - x * i - y * j - z * k,
        w * i + x * v + y * k - z * j,
        w * j - x * k + y * v + z * i,
        w * k + x * j - y * i + z * v,
    )


def inverse(q):
    return (q[0], -q[1], -q[2], -q[3])


def rotate(q, point):
    return multiply(multiply(q, (0, *point)), inverse(q))[1:]


def quaternion(degrees):
    x, y, z = (math.radians(v) / 2 for v in vector(degrees))
    return multiply(
        multiply((math.cos(z), 0, 0, math.sin(z)), (math.cos(y), 0, math.sin(y), 0)),
        (math.cos(x), math.sin(x), 0, 0),
    )


def euler(q):
    w, x, y, z = q
    pitch = math.asin(max(-1, min(1, 2 * (w * y - z * x))))
    if abs(math.cos(pitch)) < 1e-7:
        return (
            0.0,
            math.degrees(pitch),
            math.degrees(math.atan2(2 * (w * z - x * y), 1 - 2 * (x * x + z * z))),
        )
    return tuple(
        math.degrees(v)
        for v in (
            math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)),
            pitch,
            math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)),
        )
    )


def slerp(a, b, t):
    dot = sum(x * y for x, y in zip(a, b))
    if dot < 0:
        b, dot = tuple(-v for v in b), -dot
    if dot > 0.9995:
        q = tuple(x + (y - x) * t for x, y in zip(a, b))
        length = math.sqrt(sum(v * v for v in q))
        return tuple(v / length for v in q)
    angle = math.acos(max(-1, min(1, dot)))
    return tuple(
        (x * math.sin((1 - t) * angle) + y * math.sin(t * angle)) / math.sin(angle)
        for x, y in zip(a, b)
    )


@dataclass(frozen=True)
class Pose:
    position: tuple = (0.0, 0.0, 0.0)
    rotation: tuple = (1.0, 0.0, 0.0, 0.0)
    scale: float = 1.0

    @classmethod
    def xyz(cls, position=(0, 0, 0), rotation=(0, 0, 0), scale=1):
        if not math.isfinite(scale) or scale <= 0:
            raise ValueError("Group scale must be finite and positive")
        return cls(vector(position), quaternion(rotation), float(scale))

    def point(self, point):
        return add(
            self.position, rotate(self.rotation, tuple(v * self.scale for v in point))
        )

    def compose(self, child):
        return Pose(
            self.point(child.position),
            multiply(self.rotation, child.rotation),
            self.scale * child.scale,
        )

    def inverted(self):
        q = inverse(self.rotation)
        return Pose(
            rotate(q, tuple(-v / self.scale for v in self.position)), q, 1 / self.scale
        )


def sample(keys, time, default):
    if not keys:
        return default
    ordered = sorted(keys)
    index = bisect_right([key[0] for key in ordered], time)
    if index == 0:
        return ordered[0][1]
    if index == len(ordered):
        return ordered[-1][1]
    start, a, easing = ordered[index - 1]
    end, b, _ = ordered[index]
    t = (time - start) / (end - start)
    if easing == "hold":
        return a
    if easing == "smooth":
        t = t * t * (3 - 2 * t)
    if isinstance(a, Pose):
        return Pose(
            tuple(x + (y - x) * t for x, y in zip(a.position, b.position)),
            slerp(a.rotation, b.rotation, t),
            a.scale + (b.scale - a.scale) * t,
        )
    return tuple(x + (y - x) * t for x, y in zip(a, b))


@dataclass
class Node:
    links: list
    template: int | None = None
    size: tuple | None = None
    color: str | None = None
    keys: list = field(default_factory=list)
    size_keys: list = field(default_factory=list)


class Rig:
    def __init__(self):
        self.nodes = {}

    def group(self, name, parent=None, **pose):
        if name in self.nodes or (parent is not None and parent not in self.nodes):
            raise ValueError("Duplicate node or unknown parent: " + name)
        self.nodes[name] = Node([(float("-inf"), parent, Pose.xyz(**pose))])
        return name

    def prop(self, name, template, size, color, parent=None, **pose):
        size = vector(size)
        if min(size) <= 0:
            raise ValueError("Prop dimensions must be positive")
        self.group(name, parent, **pose)
        node = self.nodes[name]
        node.template, node.size, node.color = template, size, color
        return name

    def key(
        self,
        name,
        time,
        position=(0, 0, 0),
        rotation=(0, 0, 0),
        scale=1,
        easing="smooth",
        size=None,
    ):
        if not math.isfinite(time) or easing not in ("smooth", "linear", "hold"):
            raise ValueError("Invalid key time or easing")
        node = self.nodes[name]
        if size is not None:
            size = vector(size)
            if node.template is None or min(size) <= 0:
                raise ValueError("Only props have positive dimensions")
            node.size_keys = [k for k in node.size_keys if k[0] != time]
            node.size_keys.append((time, size, easing))
        else:
            node.keys = [k for k in node.keys if k[0] != time]
            node.keys.append((time, Pose.xyz(position, rotation, scale), easing))

    def evaluate(self, time):
        result, visiting = {}, set()

        def visit(name):
            if name in result:
                return result[name]
            if name in visiting:
                raise ValueError("Cyclic rig hierarchy at " + name)
            visiting.add(name)
            node = self.nodes[name]
            _, parent, bind = max(
                (link for link in node.links if link[0] <= time),
                key=lambda link: link[0],
            )
            local = bind.compose(sample(node.keys, time, Pose()))
            result[name] = visit(parent).compose(local) if parent is not None else local
            visiting.remove(name)
            return result[name]

        for name in self.nodes:
            visit(name)
        return result

    def reparent(self, name, parent, time, keep_world=True):
        if (
            name == parent
            or (parent is not None and parent not in self.nodes)
            or not math.isfinite(time)
        ):
            raise ValueError("Invalid attachment")
        poses = self.evaluate(time)
        parent_world = poses[parent] if parent is not None else Pose()
        node = self.nodes[name]
        bind = (
            parent_world.inverted()
            .compose(poses[name])
            .compose(sample(node.keys, time, Pose()).inverted())
            if keep_world
            else Pose()
        )
        previous = node.links[:]
        node.links = [link for link in node.links if link[0] != time] + [
            (time, parent, bind)
        ]
        try:
            self.evaluate(time)
        except ValueError:
            node.links = previous
            raise

    def animated(self, name, visited=None):
        visited = set() if visited is None else visited
        if name in visited:
            return False
        visited.add(name)
        node = self.nodes[name]
        return bool(
            node.keys
            or node.size_keys
            or len(node.links) > 1
            or any(
                parent is not None and self.animated(parent, visited)
                for _, parent, _ in node.links
            )
        )


def retail_dimensions(size, base):
    size, base = vector(size), vector(base)
    if min(size) <= 0 or min(base) <= 0:
        raise ValueError("Prop and model dimensions must be positive")
    ratios = [value / unit for value, unit in zip(size, base)]
    low, high = max(0.25, max(ratios) / 4), min(10, min(ratios) / 0.25)
    if low > high + 1e-8:
        raise ValueError("Dimensions exceed retail scale/stretch limits: " + str(size))
    scale = math.sqrt(low * high)
    return scale, [r / scale for r in ratios]


class Baker:
    """Thin adapter over a call(tool, **arguments) -> decoded-result function."""

    def __init__(self, rig, call):
        self.rig, self.call, self.actors, self.bases = rig, call, {}, {}

    def create(self, time=0, progress=None):
        for name, node in self.rig.nodes.items():
            if node.template is None:
                continue
            made = self.call("create_actor", template_cno=node.template, name=name)
            self.actors[name], self.bases[name] = made["actor_id"], made["model_size"]
            if progress:
                progress(name, len(self.actors))
        self.pose(time, initial=True)

    def pose(self, time, initial=False):
        world, edits = self.rig.evaluate(time), []
        for name, actor_id in self.actors.items():
            node = self.rig.nodes[name]
            if not initial and not self.rig.animated(name):
                continue
            pose = world[name]
            size = tuple(
                v * pose.scale for v in sample(node.size_keys, time, node.size)
            )
            scale, stretch = retail_dimensions(size, self.bases[name])
            edit = dict(
                actor_id=actor_id,
                position=list(pose.position),
                rotation=list(euler(pose.rotation)),
                scale=scale,
                stretch=stretch,
            )
            if initial:
                edit["color"] = node.color
            edits.append(edit)
        for start in range(0, len(edits), 256):
            self.call("transform_actors", edits=edits[start : start + 256])

    def bake(self, frames, duration, progress=None):
        if frames < 2 or not math.isfinite(duration) or duration <= 0:
            raise ValueError(
                "Baking requires at least two frames and positive duration"
            )
        for frame in range(2, frames + 1):
            self.call("edit_frame", frame=frame)
            self.pose((frame - 1) * duration / (frames - 1))
            if progress:
                progress(frame)
        self.call("edit_frame", frame=1)
