#!/usr/bin/env python3
"""Build the collision-only BSP used by the rotating-pusher SSQC harness."""

import hashlib
import struct
from pathlib import Path


BSP_VERSION = 29
CONTENTS_EMPTY = -1
CONTENTS_SOLID = -2
EMPTY_LEAF = -2
SOLID_LEAF = -1
HULL_BOUNDS = (
    ((-16, -16, -24), (16, 16, 32)),
    ((-32, -32, -24), (32, 32, 64)),
)
BOXES = (
    ("thin_vertical", (-64, -8, -64), (64, 8, 64)),
    ("rideable", (-64, -64, -8), (64, 64, 8)),
    ("blocking_wall", (-8, -64, -64), (8, 64, 64)),
    ("mixed_platform", (-48, -48, -8), (48, 48, 8)),
    ("start_off", (-40, -40, -8), (40, 40, 8)),
    ("tweened", (-48, -8, -48), (48, 8, 48)),
)
ENTITIES = b'''{
"classname" "worldspawn"
"message" "QSS-M rotating-pusher selftest"
}
{
"classname" "info_player_start"
"origin" "0 0 96"
"angle" "0"
}
\0'''


def _plane(planes, normal, distance, axis):
    planes.append((*normal, float(distance), axis))
    return len(planes) - 1


def _box_plane_indices(planes, mins, maxs, hull_mins=(0, 0, 0),
                       hull_maxs=(0, 0, 0)):
    result = []
    for axis in range(3):
        normal = [0.0, 0.0, 0.0]
        normal[axis] = 1.0
        result.append(_plane(planes, normal, maxs[axis] - hull_mins[axis], axis))
        normal[axis] = -1.0
        result.append(_plane(planes, normal, -mins[axis] + hull_maxs[axis],
                             axis + 3))
    return result


def _add_node_box(nodes, planes, mins, maxs):
    first = len(nodes)
    indices = _box_plane_indices(planes, mins, maxs)
    bounds = tuple(int(value) for value in (*mins, *maxs))
    for index, plane_index in enumerate(indices):
        inside = first + index + 1 if index + 1 < len(indices) else SOLID_LEAF
        nodes.append((plane_index, EMPTY_LEAF, inside, *bounds, 0, 0))
    return first


def _add_clip_box(clipnodes, planes, mins, maxs, hull_mins, hull_maxs):
    first = len(clipnodes)
    indices = _box_plane_indices(planes, mins, maxs, hull_mins, hull_maxs)
    for index, plane_index in enumerate(indices):
        inside = first + index + 1 if index + 1 < len(indices) else CONTENTS_SOLID
        clipnodes.append((plane_index, CONTENTS_EMPTY, inside))
    return first


def build_bsp():
    planes = [(1.0, 0.0, 0.0, 8192.0, 0)]
    nodes = [(0, EMPTY_LEAF, EMPTY_LEAF, -4096, -4096, -4096,
              4096, 4096, 4096, 0, 0)]
    clipnodes = [(0, CONTENTS_EMPTY, CONTENTS_EMPTY)]
    models = [((-4096, -4096, -4096), (4096, 4096, 4096), (0, 0, 0),
               (0, 0, 0, 0), 1, 0, 0)]

    for _name, mins, maxs in BOXES:
        node_head = _add_node_box(nodes, planes, mins, maxs)
        hull_heads = [_add_clip_box(clipnodes, planes, mins, maxs, *bounds)
                      for bounds in HULL_BOUNDS]
        models.append((mins, maxs, (0, 0, 0),
                       (node_head, hull_heads[0], hull_heads[1], 0), 0, 0, 0))

    lumps = [b"" for _ in range(15)]
    lumps[0] = ENTITIES
    lumps[1] = b"".join(struct.pack("<4fi", *plane) for plane in planes)
    lumps[2] = struct.pack("<i", 0)
    lumps[3] = struct.pack("<3f", 0.0, 0.0, 0.0)
    lumps[5] = b"".join(struct.pack("<ihh6hHH", *node) for node in nodes)
    lumps[9] = b"".join(struct.pack("<ihh", *node) for node in clipnodes)
    lumps[10] = (
        struct.pack("<ii6hHH4B", CONTENTS_SOLID, -1, -4096, -4096, -4096,
                    4096, 4096, 4096, 0, 0, 0, 0, 0, 0) +
        struct.pack("<ii6hHH4B", CONTENTS_EMPTY, -1, -4096, -4096, -4096,
                    4096, 4096, 4096, 0, 0, 0, 0, 0, 0)
    )
    lumps[14] = b"".join(
        struct.pack("<9f7i", *mins, *maxs, *origin, *heads,
                    visleafs, firstface, numfaces)
        for mins, maxs, origin, heads, visleafs, firstface, numfaces in models
    )

    header_size = 4 + 15 * 8
    cursor = header_size
    payload = bytearray()
    directory = []
    for lump in lumps:
        padding = (-cursor) & 3
        payload.extend(b"\0" * padding)
        cursor += padding
        directory.append((cursor, len(lump)))
        payload.extend(lump)
        cursor += len(lump)
    header = bytearray(struct.pack("<i", BSP_VERSION))
    for offset, length in directory:
        header.extend(struct.pack("<ii", offset, length))
    return bytes(header + payload)


def main():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = build_bsp()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"wrote {args.output} ({len(data)} bytes, sha256={hashlib.sha256(data).hexdigest()})")


if __name__ == "__main__":
    main()
