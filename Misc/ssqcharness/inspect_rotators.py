#!/usr/bin/env python3
"""Inventory rotate_object_continuously entities in Quake PAK/BSP files."""

import argparse
import hashlib
import re
import struct
from pathlib import Path


BSP_VERSION = 29
BSP2_VERSION = int.from_bytes(b"BSP2", "little")
LUMP_ENTITIES = 0
LUMP_VERTEXES = 3
LUMP_FACES = 7
LUMP_EDGES = 12
LUMP_SURFEDGES = 13
LUMP_MODELS = 14
ROTATOR_CLASS = "rotate_object_continuously"
SOLID_BSP_FLAG = 4
START_OFF_FLAG = 64


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def pak_files(path):
    data = path.read_bytes()
    magic, directory_offset, directory_length = struct.unpack_from("<4sii", data)
    if magic != b"PACK" or directory_length % 64:
        raise ValueError(f"{path}: invalid PAK header")
    for offset in range(directory_offset, directory_offset + directory_length, 64):
        raw_name, file_offset, file_length = struct.unpack_from("<56sii", data, offset)
        name = raw_name.split(b"\0", 1)[0].decode("latin1")
        yield name, data[file_offset:file_offset + file_length]


def parse_entities(data):
    text = data.split(b"\0", 1)[0].decode("latin1")
    for block in re.findall(r"\{(.*?)\}", text, re.S):
        yield dict(re.findall(r'"([^"\\]*)"\s+"([^"\\]*)"', block))


def parse_vector(value):
    try:
        values = [float(item) for item in value.split()]
    except (AttributeError, ValueError):
        values = []
    return tuple((values + [0.0, 0.0, 0.0])[:3])


def format_number(value):
    if value == int(value):
        return str(int(value))
    return f"{value:.1f}"


def format_vector(value):
    return " ".join(format_number(component) for component in value)


class Bsp:
    def __init__(self, name, data):
        self.name = name
        self.data = data
        self.version = struct.unpack_from("<i", data)[0]
        if self.version not in (BSP_VERSION, BSP2_VERSION):
            raise ValueError(f"{name}: unsupported BSP version {self.version}")
        self.lumps = [struct.unpack_from("<ii", data, 4 + index * 8) for index in range(15)]
        entity_offset, entity_length = self.lumps[LUMP_ENTITIES]
        self.entities = list(parse_entities(data[entity_offset:entity_offset + entity_length]))
        self.vertices = self._records(LUMP_VERTEXES, "<3f")
        self.surfedges = self._flat_ints(LUMP_SURFEDGES)
        if self.version == BSP2_VERSION:
            self.edges = self._records(LUMP_EDGES, "<II")
            self.faces = self._records(LUMP_FACES, "<5i4si")
        else:
            self.edges = self._records(LUMP_EDGES, "<HH")
            self.faces = self._records(LUMP_FACES, "<hhihh4si")
        self.models = self._records(LUMP_MODELS, "<9f7i")

    def _records(self, lump_index, record_format):
        offset, length = self.lumps[lump_index]
        size = struct.calcsize(record_format)
        if length % size:
            raise ValueError(f"{self.name}: lump {lump_index} has invalid size {length}")
        return [struct.unpack_from(record_format, self.data, item)
                for item in range(offset, offset + length, size)]

    def _flat_ints(self, lump_index):
        offset, length = self.lumps[lump_index]
        if length % 4:
            raise ValueError(f"{self.name}: lump {lump_index} has invalid size {length}")
        return struct.unpack_from(f"<{length // 4}i", self.data, offset)

    def model_details(self, model_index):
        model = self.models[model_index]
        model_mins = model[0:3]
        model_maxs = model[3:6]
        first_face, num_faces = model[-2:]
        used_vertices = []
        for face in self.faces[first_face:first_face + num_faces]:
            first_edge, num_edges = face[2:4]
            for surfedge in self.surfedges[first_edge:first_edge + num_edges]:
                edge = self.edges[abs(surfedge)]
                used_vertices.append(self.vertices[edge[0 if surfedge >= 0 else 1]])
        if not used_vertices:
            geometry_mins = geometry_maxs = (0.0, 0.0, 0.0)
        else:
            geometry_mins = tuple(min(vertex[axis] for vertex in used_vertices) for axis in range(3))
            geometry_maxs = tuple(max(vertex[axis] for vertex in used_vertices) for axis in range(3))
        dimensions = tuple(geometry_maxs[axis] - geometry_mins[axis] for axis in range(3))
        return model_mins, model_maxs, dimensions


def inventory(label, pak_path):
    rows = []
    for map_name, data in pak_files(pak_path):
        if not map_name.lower().endswith(".bsp") or map_name.lower().startswith("maps/bmodel/"):
            continue
        bsp = Bsp(map_name, data)
        for entity_index, entity in enumerate(bsp.entities):
            if entity.get("classname") != ROTATOR_CLASS:
                continue
            spawnflags = int(float(entity.get("spawnflags", "0")))
            solid = bool(spawnflags & SOLID_BSP_FLAG)
            start_off = bool(spawnflags & START_OFF_FLAG)
            angular_velocity = parse_vector(entity.get("avelocity", "0 30 0"))
            linear_velocity = parse_vector(entity.get("velocity", "0 0 0"))
            damage = float(entity.get("dmg", "0") or 0)
            model_name = entity.get("model", "")
            model_index = int(model_name[1:])
            model_mins, model_maxs, dimensions = bsp.model_details(model_index)
            rideable = (solid and abs(angular_velocity[2]) > 0.001 and
                        dimensions[0] >= 64 and dimensions[1] >= 64 and
                        dimensions[2] <= min(dimensions[0], dimensions[1]) * 0.35)
            mixed = any(abs(component) > 0.001 for component in linear_velocity)
            if solid and damage:
                category = "crushing/hazard rotator"
            elif rideable:
                category = "plausibly rideable platform"
            elif solid:
                category = "solid obstacle rotator"
            else:
                category = "non-solid decorative rotator"
            targetname = entity.get("targetname", "")
            incoming = []
            if targetname:
                for source_index, source in enumerate(bsp.entities):
                    if source.get("target") == targetname:
                        incoming.append(f"#{source_index} {source.get('classname', '?')}"
                                        f"[{source.get('targetname', '-')}]")
            rows.append({
                "pack": label,
                "map": map_name.removeprefix("maps/").removesuffix(".bsp"),
                "entity": entity_index,
                "origin": entity.get("origin", "0 0 0"),
                "runtime_origin": entity.get("pos2", entity.get("origin", "0 0 0")),
                "spawnflags": spawnflags,
                "solid": solid,
                "start_off": start_off,
                "avelocity": format_vector(angular_velocity),
                "delay": entity.get("delay", "0"),
                "damage": format_number(damage),
                "model": model_name,
                "model_bounds": f"{format_vector(model_mins)} .. {format_vector(model_maxs)}",
                "dimensions": format_vector(dimensions),
                "rideable": rideable,
                "mixed": mixed,
                "category": category,
                "targetname": targetname or "-",
                "target": entity.get("target", "-"),
                "incoming": ", ".join(incoming) or "-",
            })
    return rows


def markdown(specs):
    all_rows = []
    sources = []
    for label, path in specs:
        data = path.read_bytes()
        rows = inventory(label, path)
        all_rows.extend(rows)
        sources.append((label, path.name, len(data), sha256(data), len(rows)))

    lines = [
        "# Shipped rerelease rotating-brush inventory",
        "",
        "Generated by `inspect_rotators.py`. Model bounds are the `LUMP_MODELS` bounds; geometry dimensions are computed from the model's face vertices. `pos2` is shown as runtime origin because rerelease QC copies it to `origin` during spawn.",
        "",
        "## Sources",
        "",
        "| Pack | File | Bytes | SHA-256 | Rotators |",
        "|---|---|---:|---|---:|",
    ]
    for label, filename, length, digest, count in sources:
        lines.append(f"| {label} | {filename} | {length} | `{digest}` | {count} |")

    counts = {}
    for row in all_rows:
        counts[row["category"]] = counts.get(row["category"], 0) + 1
    lines.extend(["", "## Classification", ""])
    for category in (
            "non-solid decorative rotator",
            "solid decorative rotator",
            "solid obstacle rotator",
            "crushing/hazard rotator",
            "plausibly rideable platform"):
        lines.append(f"- {category}: {counts.get(category, 0)}")
    lines.extend([
        f"- mixed translating-and-rotating entities: {sum(row['mixed'] for row in all_rows)}",
        f"- start-off entities: {sum(row['start_off'] for row in all_rows)}",
        "",
        "The categories are deliberately collision-capability based. A solid rotator is counted as an obstacle rather than guessed to be decorative; the entity/model lumps alone cannot prove that KEX calls `blocked`, applies damage, or makes the brush reachable. One non-solid entity carries `dmg=1`, but cannot be a source-confirmed collision hazard while non-solid. The rideability heuristic requires a broad, thin horizontal model rotating about world Z; none match.",
        "",
        "## Entities",
        "",
        "| Pack/map | Ent | Origin → runtime origin | Flags | Solid/start | avelocity; delay; dmg | Model | Model bounds; geometry dimensions | Ride/mixed | Category | Targets |",
        "|---|---:|---|---:|---|---|---|---|---|---|---|",
    ])
    for row in all_rows:
        targets = (f"name={row['targetname']}; target={row['target']}; "
                   f"incoming={row['incoming']}").replace("|", "\\|")
        lines.append(
            f"| {row['pack']}/{row['map']} | {row['entity']} | `{row['origin']}` → `{row['runtime_origin']}` | "
            f"{row['spawnflags']} | {'yes' if row['solid'] else 'no'}/{'off' if row['start_off'] else 'on'} | "
            f"`{row['avelocity']}`; {row['delay']}; {row['damage']} | {row['model']} | `{row['model_bounds']}`; `{row['dimensions']}` | "
            f"{'yes' if row['rideable'] else 'no'}/{'yes' if row['mixed'] else 'no'} | {row['category']} | {targets} |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("packs", nargs="+", metavar="LABEL=PAK")
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()
    specs = []
    for spec in args.packs:
        if "=" not in spec:
            parser.error(f"expected LABEL=PAK, got {spec!r}")
        label, path = spec.split("=", 1)
        specs.append((label, Path(path)))
    report = markdown(specs)
    if args.output:
        args.output.write_text(report)
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
