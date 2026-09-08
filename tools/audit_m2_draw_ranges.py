#!/usr/bin/env python3
"""Read-only WotLK MD20/SKIN draw-range audit matching M2Loader remapping."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def array(data, offset, count, fmt):
    stride = struct.calcsize(fmt)
    if offset < 0 or count < 0 or offset + count * stride > len(data):
        raise ValueError("declared array extends beyond file")
    return list(struct.iter_unpack(fmt, data[offset:offset + count * stride]))


def audit(model_path, skin_path):
    model = model_path.read_bytes()
    skin = skin_path.read_bytes()
    if model[:4] != b"MD20" or struct.unpack_from("<I", model, 4)[0] != 264:
        raise ValueError("this bounded audit supports MD20 version264 only")
    if skin[:4] != b"SKIN":
        raise ValueError("expected SKIN header")
    vertices, vertex_offset = struct.unpack_from("<II", model, 60)
    array(model, vertex_offset, vertices, "<48s")
    lookup_n, lookup_offset, triangle_n, triangle_offset, _, _, section_n, section_offset, batch_n, batch_offset, _ = struct.unpack_from("<11I", skin, 4)
    lookup = [x[0] for x in array(skin, lookup_offset, lookup_n, "<H")]
    triangles = [x[0] for x in array(skin, triangle_offset, triangle_n, "<H")]
    sections = array(skin, section_offset, section_n, "<10H7f")
    batches = array(skin, batch_offset, batch_n, "<Bb11H")
    bad_lookup = sum(i >= len(lookup) for i in triangles)
    bad_vertex = sum(lookup[i] >= vertices for i in triangles if i < len(lookup))
    resolved = [lookup[i] if i < len(lookup) and lookup[i] < vertices else 0 for i in triangles]
    draws = []
    for index, batch in enumerate(batches):
        section_index = batch[3]
        fallback = section_index >= len(sections)
        if fallback:
            level, vertex_start, vertex_count, first, count = 0, 0, vertices, 0, len(resolved)
        else:
            section = sections[section_index]
            level, vertex_start, vertex_count, first, count = section[1:6]
        indices = resolved[first:first + count]
        draws.append({"batch": index, "section": section_index, "fallback": fallback,
                      "level": level, "vertex_start": vertex_start, "vertex_count": vertex_count,
                      "index_start": first, "index_count": count,
                      "end_within_index_buffer": first + count <= len(resolved),
                      "triangle_multiple": count % 3 == 0,
                      "min_resolved_vertex": min(indices, default=None),
                      "max_resolved_vertex": max(indices, default=None),
                      "all_resolved_vertices_in_buffer": all(i < vertices for i in indices)})
    return {"model": str(model_path), "skin": str(skin_path),
            "model_sha256": hashlib.sha256(model).hexdigest(),
            "skin_sha256": hashlib.sha256(skin).hexdigest(),
            "version": 264, "vertices": vertices, "disk_vertex_stride": 48,
            "lookup_count": len(lookup), "index_count": len(resolved),
            "index_buffer_bytes": len(resolved) * 2,
            "max_raw_triangle_lookup": max(triangles, default=None),
            "max_lookup_global_vertex": max(lookup, default=None),
            "max_resolved_global_vertex": max(resolved, default=None),
            "out_of_bounds_lookup_count": bad_lookup, "out_of_bounds_vertex_count": bad_vertex,
            "section_count": len(sections), "draw_batch_count": len(draws),
            "base_vertex": 0, "draws": draws,
            "passes": bad_lookup == 0 and bad_vertex == 0 and
                all(d["end_within_index_buffer"] and d["all_resolved_vertices_in_buffer"] and
                    not d["fallback"] for d in draws)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--pair", nargs=2, action="append", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if any(args.output.resolve() == source.resolve() for pair in args.pair for source in pair):
        p.error("output must not replace an input asset")
    result = {"schema": 1, "scope": "read-only model indices; no GPU execution",
              "pairs": [audit(*pair) for pair in args.pair]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for item in result["pairs"]:
        print(item["model"], "vertices", item["vertices"], "indices", item["index_count"],
              "batches", item["draw_batch_count"], "passes", item["passes"])
    return 0 if all(item["passes"] for item in result["pairs"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
