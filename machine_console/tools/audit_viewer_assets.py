#!/usr/bin/env python3
"""Audit viewer resource links against an official unpackdat tree.

This is an inventory tool, not a repair tool. It mirrors the viewer's safe
texture fallback policy: exact/trimmed path first, then a unique basename,
same-stem alternate image format, or byte-identical candidates. Ambiguous
candidates stay unresolved.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys
from collections import Counter
from pathlib import Path
from typing import Iterator


IMAGE_EXTS = {".dds", ".png", ".tga"}
PP_FILE_RE = re.compile(r"^PP_.*\.vra$", re.IGNORECASE)
NODE_RE = re.compile(r"\{\s*(Sprite|Particle)\b")
FILE_FIELD_RE = re.compile(r'\(file\s+string\[\d+\]\s+"([^"\r\n]*)"')
ANIM_FIELD_RE = re.compile(r"\(anim_type\s+[^\s]+\s+(-?\d+)\b")
MATERIAL_RE = re.compile(r"\{\s*material\b")
QUOTED_RE = re.compile(r'"([^"\r\n]*)"')
GEOMETRY_RE = re.compile(r'\(Geometry\s+string(?:\[\d+\])?\s+"([^"\r\n]+)"', re.IGNORECASE)
MESH_RE = re.compile(r"\{\s*mesh\b")
LOD0_RE = re.compile(r"\{\s*lod0\b", re.IGNORECASE)
NAME_RE = re.compile(r'\(name\s+string(?:\[\d+\])?\s+"([^"\r\n]*)"')
MATERIAL_ID_RE = re.compile(r"\(material_id\s+int32(?:\[\d+\])?\s+(-?\d+)")
ID_RE = re.compile(r"\(id\s+int32(?:\[\d+\])?\s+(-?\d+)")
UV_ADD_FIELD_RE = re.compile(
    r"\(uvadd_[uv]\s+float(?:\[\d+\])?\s+([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][-+]?\d+)?)",
    re.IGNORECASE,
)
MODEL_ATTR_PREFIXES = (
    ("_em_", "_ez_"), ("msm_", "msz_"), ("bsm_", "bsz_"),
    ("bam_", "baz_"), ("em_", "ez_"), ("en_", "ez_"), ("mam_", "maz_"),
    ("pm_", "pz_"), ("plm_", "plz_"), ("um_", "uz_"),
    ("om_", "oz_"), ("fm_", "fz_"), ("sm_", "sz_"),
    ("opm_", "opz_"), ("evm_", "evz_"), ("dm_", "dz_"),
)


def walk_files(root: Path) -> Iterator[Path]:
    for base, dirs, names in os.walk(root):
        dirs.sort()
        for name in names:
            yield Path(base, name)


def balanced_block(text: str, start: int) -> tuple[str, int] | None:
    depth = 0
    quoted = False
    escaped = False
    for pos in range(start, len(text)):
        ch = text[pos]
        if quoted:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                quoted = False
            continue
        if ch == '"':
            quoted = True
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[start : pos + 1], pos + 1
    return None


def read_vra_text(path: Path) -> str:
    raw = path.read_bytes()
    try:
        return raw.decode("utf-8-sig")
    except UnicodeDecodeError:
        # The Japanese client stores some path whitespace as CP932 0x81 0x40.
        try:
            return raw.decode("cp932")
        except UnicodeDecodeError:
            return raw.decode("mbcs", errors="replace")


def iter_nodes(text: str, pattern: re.Pattern[str]) -> Iterator[tuple[str, int, int]]:
    pos = 0
    while True:
        start = text.find("{", pos)
        if start < 0:
            return
        match = pattern.match(text, start)
        if not match:
            pos = start + 1
            continue
        found = balanced_block(text, start)
        if found is None:
            return
        block, end = found
        yield block, start, end
        pos = end


def texture_refs_in_vra(text: str) -> Iterator[tuple[str, str]]:
    for material, _, _ in iter_nodes(text, MATERIAL_RE):
        for slot in ("s_base", "s_mix", "s_gloss"):
            block_pattern = re.compile(r"\{\s*" + slot + r"\b")
            for block, _, _ in iter_nodes(material, block_pattern):
                for field in re.finditer(r"\(texture\s+string(?:\[\d+\])?\s+", block):
                    value = QUOTED_RE.search(block, field.end())
                    if value:
                        yield slot, value.group(1)
            # Some VRA files serialize a texture slot as a flat string pair.
            flat = re.compile(
                r'\(' + slot + r'\s+string\[2\]\s+"texture"\s*,\s*"([^"\r\n]*)"'
            )
            for value in flat.finditer(material):
                yield slot, value.group(1)


def normalize_path(value: str, trim_segments: bool) -> tuple[str, list[str]]:
    value = value.strip().replace("\\", "/")
    parts = [part for part in value.split("/") if part]
    if trim_segments:
        parts = [part.strip() for part in parts if part.strip()]
    return "/".join(parts), parts


def texture_filename_aliases(name: str) -> list[str]:
    """Return only deterministic typo repairs supported by packaged files."""
    aliases: list[str] = []
    trimmed_extension = re.sub(r"[ \t\r\n\u3000]+(?=\.[^.]+$)", "", name)
    if trimmed_extension != name:
        aliases.append(trimmed_extension)
    collapsed = re.sub(r"\.{2,}", ".", name)
    if collapsed != name:
        aliases.append(collapsed)
    lower_name = name.casefold()
    for extension in IMAGE_EXTS:
        at = lower_name.find(extension)
        if at >= 0 and at + len(extension) < len(name):
            aliases.append(name[: at + len(extension)])
            break
    match = re.search(r"_(\d)(\.[^.]+)$", name)
    if match:
        aliases.append(name[: match.start(1)] + "0" + match.group(1) + match.group(2))
    extension = Path(name).suffix.casefold()
    if extension in IMAGE_EXTS:
        aliases.extend(name[: -len(extension)] + ext for ext in sorted(IMAGE_EXTS) if ext != extension)
    return aliases


def model_attr_stem(stem: str) -> str:
    lower = stem.casefold()
    for model_prefix, attr_prefix in MODEL_ATTR_PREFIXES:
        if lower.startswith(model_prefix):
            return attr_prefix + stem[len(model_prefix) :]
    return stem


def audit_model_attr_pairs(root: Path) -> dict[str, object]:
    totals: Counter[str] = Counter()
    examples: dict[str, list[str]] = {}
    for base, _, names in os.walk(root):
        model_dir = Path(base)
        if model_dir.name.casefold() != "model":
            continue
        attr_dir = model_dir.parent / "attr"
        if not attr_dir.is_dir():
            continue
        group = next((part.casefold() for part in model_dir.relative_to(root).parts if part.casefold() in {"chara", "item", "world", "fx"}), "other")
        attr_names = {p.name.casefold() for p in attr_dir.iterdir() if p.is_file()}
        geometry_paths: dict[str, str] = {}
        for attr_path in attr_dir.iterdir():
            if not attr_path.is_file() or attr_path.suffix.casefold() != ".vra":
                continue
            try:
                match = GEOMETRY_RE.search(read_vra_text(attr_path))
            except OSError:
                continue
            if match:
                geometry = match.group(1).replace("\\", "/").lstrip("./").casefold()
                geometry_paths[geometry] = attr_path.name.casefold()
        for name in names:
            model = model_dir / name
            if model.suffix.casefold() != ".dxg":
                continue
            totals[f"{group}_models"] += 1
            original = model.stem.rstrip()
            expected = model_attr_stem(original) + ".vra"
            if expected.casefold() in attr_names:
                totals[f"{group}_paired"] += 1
                if expected.casefold() != (original + ".vra").casefold():
                    totals[f"{group}_prefix_mapped"] += 1
            elif (original + ".vra").casefold() in attr_names:
                totals[f"{group}_paired"] += 1
                totals[f"{group}_same_stem_fallback"] += 1
            elif str(model.relative_to(root)).replace("\\", "/").casefold() in geometry_paths:
                totals[f"{group}_paired"] += 1
                totals[f"{group}_geometry_match"] += 1
            elif len(examples.setdefault(group, [])) < 12:
                examples[group].append(str(model.relative_to(root)).replace("\\", "/") + " -> " + str((attr_dir / expected).relative_to(root)).replace("\\", "/"))
    return {"counts": dict(sorted(totals.items())), "unpaired_examples": examples}


def dxg_names(path: Path) -> list[str]:
    data = path.read_bytes()
    if len(data) < 24 or data[:4] != b"DXG ":
        return []
    name_bytes = struct.unpack_from("<I", data, 20)[0]
    if name_bytes > len(data) - 24:
        return []
    raw_names = data[24 : 24 + name_bytes].split(b"\0")
    return [name.decode("cp932", errors="replace") for name in raw_names if name]


def vra_material_bindings(text: str) -> tuple[dict[str, tuple[int | None, bool, bool]], dict[int, dict[str, str]]]:
    meshes: dict[str, tuple[int | None, bool, bool]] = {}
    for block, _, _ in iter_nodes(text, MESH_RE):
        name = NAME_RE.search(block)
        if not name:
            continue
        lod0_blocks = list(iter_nodes(block, LOD0_RE))
        material_match = MATERIAL_ID_RE.search(lod0_blocks[0][0]) if lod0_blocks else None
        material_id = int(material_match.group(1)) if material_match else None
        no_render = bool(re.search(r"\(no_render\s+int8(?:\[\d+\])?\s+1\b", block))
        # v15 sub_C19320 scans VRA mesh records in file order and stops at
        # the first exact name match. Keep that same ordering here; replacing
        # this with a dict assignment would silently make the audit use the
        # last duplicate record, disagreeing with both the client and viewer.
        meshes.setdefault(name.group(1), (material_id, no_render, bool(lod0_blocks)))

    materials: dict[int, dict[str, str]] = {}
    for block, _, _ in iter_nodes(text, MATERIAL_RE):
        ident = ID_RE.search(block)
        if not ident:
            continue
        slots: dict[str, str] = {}
        for slot, ref in texture_refs_in_vra(block):
            slots[slot] = ref
        materials[int(ident.group(1))] = slots
    return meshes, materials


def audit_mesh_material_bindings(root: Path, texture_index: "TextureIndex") -> dict[str, object]:
    """Check each DXG name against its paired VRA mesh/lod0/material records."""
    totals: Counter[str] = Counter()
    examples: dict[str, list[str]] = {}
    texture_problem_examples: dict[str, list[str]] = {}
    for base, _, names in os.walk(root):
        model_dir = Path(base)
        if model_dir.name.casefold() != "model":
            continue
        attr_dir = model_dir.parent / "attr"
        if not attr_dir.is_dir():
            continue
        group = next((part.casefold() for part in model_dir.relative_to(root).parts if part.casefold() in {"chara", "item", "world", "fx"}), "other")
        attr_files = [p for p in attr_dir.iterdir() if p.is_file() and p.suffix.casefold() == ".vra"]
        by_name = {p.name.casefold(): p for p in attr_files}
        geometry_to_attr: dict[str, Path] = {}
        decoded: dict[Path, tuple[str, dict[str, tuple[int | None, bool, bool]], dict[int, dict[str, str]]]] = {}
        for attr_path in attr_files:
            try:
                text = read_vra_text(attr_path)
            except OSError:
                continue
            geometry_match = GEOMETRY_RE.search(text)
            geometry = geometry_match.group(1).replace("\\", "/").lstrip("./").casefold() if geometry_match else ""
            if geometry:
                geometry_to_attr[geometry] = attr_path
            meshes, materials = vra_material_bindings(text)
            decoded[attr_path] = (geometry, meshes, materials)

        for filename in names:
            model_path = model_dir / filename
            if model_path.suffix.casefold() != ".dxg":
                continue
            totals[f"{group}_models"] += 1
            stem = model_path.stem.rstrip()
            candidates = [by_name.get((model_attr_stem(stem) + ".vra").casefold()), by_name.get((stem + ".vra").casefold())]
            attr_path = next((candidate for candidate in candidates if candidate is not None), None)
            relative_model = str(model_path.relative_to(root)).replace("\\", "/").casefold()
            if attr_path is None:
                attr_path = geometry_to_attr.get(relative_model)
            if attr_path is None or attr_path not in decoded:
                totals[f"{group}_without_vra"] += 1
                continue
            totals[f"{group}_with_vra"] += 1
            geometry, mesh_binds, materials = decoded[attr_path]
            if not mesh_binds:
                totals[f"{group}_vra_without_mesh_binds"] += 1
            try:
                names_in_dxg = dxg_names(model_path)
            except OSError:
                continue
            if not names_in_dxg:
                totals[f"{group}_dxg_without_names"] += 1
            for mesh_name in names_in_dxg:
                totals[f"{group}_dxg_mesh_names"] += 1
                binding = mesh_binds.get(mesh_name)
                if binding is None or not binding[2]:
                    totals[f"{group}_mesh_name_unmatched"] += 1
                    if len(examples.setdefault(f"{group}_mesh_name_unmatched", [])) < 10:
                        examples[f"{group}_mesh_name_unmatched"].append(
                            f"{relative_model} :: {mesh_name} -> {attr_path.relative_to(root).as_posix()}"
                        )
                    continue
                totals[f"{group}_mesh_name_matched"] += 1
                material_id, no_render, _has_lod0 = binding
                if no_render:
                    totals[f"{group}_matched_no_render"] += 1
                    continue
                if material_id is None or material_id not in materials:
                    totals[f"{group}_matched_without_material"] += 1
                    continue
                totals[f"{group}_matched_with_material"] += 1
                if "_gnd_01" in mesh_name.casefold() or "_lod1" in mesh_name.casefold():
                    totals[f"{group}_legacy_lod_name_but_vra_bound"] += 1
                    example_key = f"{group}_legacy_lod_name_but_vra_bound"
                    if len(examples.setdefault(example_key, [])) < 10:
                        examples[example_key].append(
                            f"{relative_model} :: {mesh_name} -> material {material_id}"
                        )
                slots = materials[material_id]
                for slot in ("s_base", "s_mix"):
                    ref = slots.get(slot, "")
                    if not ref:
                        totals[f"{group}_{slot}_absent"] += 1
                    else:
                        classification = texture_index.classify(ref, geometry)
                        totals[f"{group}_{slot}_{classification}"] += 1
                        if classification == "unresolved" or "ambiguous" in classification:
                            key = f"{group}_{slot}_{classification}"
                            samples = texture_problem_examples.setdefault(key, [])
                            if len(samples) < 10:
                                samples.append(
                                    f"{relative_model} :: {mesh_name} material {material_id} "
                                    f"{slot}={ref} -> {attr_path.relative_to(root).as_posix()}"
                                )
    return {
        "counts": dict(sorted(totals.items())),
        "unmatched_mesh_examples": examples,
        "texture_problem_examples": texture_problem_examples,
    }


class TextureIndex:
    def __init__(self, root: Path):
        self.root = root
        self.by_name: dict[str, list[Path]] = {}
        self.cache: dict[str, str] = {}
        self.image_count = 0
        for path in walk_files(root):
            if path.suffix.lower() not in IMAGE_EXTS:
                continue
            self.image_count += 1
            self.by_name.setdefault(path.name.casefold(), []).append(path)

    def classify(self, reference: str, geometry_context: str = "") -> str:
        cache_key = (reference.strip().replace("\\", "/") + "\0" + geometry_context.strip().replace("\\", "/")).casefold()
        if cache_key in self.cache:
            return self.cache[cache_key]
        _, raw_parts = normalize_path(reference, trim_segments=False)
        _, clean_parts = normalize_path(reference, trim_segments=True)
        drive_path = bool(raw_parts and re.fullmatch(r"[A-Za-z]:", raw_parts[0]))
        parent_path = ".." in raw_parts or ".." in clean_parts
        unsafe = drive_path or parent_path
        if not unsafe and raw_parts and self.root.joinpath(*raw_parts).is_file():
            result = "exact"
        elif not unsafe and clean_parts and clean_parts != raw_parts and self.root.joinpath(*clean_parts).is_file():
            result = "trimmed_path"
        elif not clean_parts:
            result = "unresolved"
        else:
            same_directory = self.root.joinpath(*clean_parts[:-1])
            aliases = set(texture_filename_aliases(clean_parts[-1]))
            cross_directory_fallback = None
            cross_directory_fallback_class = "map_texture_preview_fallback"
            directory_aliases: set[str] = set()
            vra_directory_aliases: set[str] = set()
            map_texture_aliases: set[str] = set()
            map_texture_fallback_aliases: set[str] = set()
            fx_image_aliases: set[str] = set()
            item_texture_aliases: set[str] = set()
            same_directory_format_aliases: set[str] = set()
            requested_path = Path(clean_parts[-1])
            # A few official map VRA files retain stale texture prefixes even
            # though the intended sibling is unambiguous from the VRA's map
            # directory and semantic suffix. Keep these repairs map-scoped.
            map_folder = tuple(part.casefold() for part in clean_parts[:-1])
            map_name = requested_path.name.casefold()
            if map_folder == ("fx", "image") and map_name == "mt_05_208_02.png":
                corrected = "PT_05_208_02.png"
                aliases.add(corrected)
                fx_image_aliases.add(corrected.casefold())
            elif map_folder == ("world", "field", "f14", "tex"):
                map_aliases = {
                    "ft_09_kn_light01.dds": "FT_F14_kn_light01.dds",
                    "ft_f14_kn_mix_ice01_guid.dds": "FT_F14_kn_mix_ice01.dds",
                }
                corrected = map_aliases.get(map_name)
                if corrected:
                    aliases.add(corrected)
                    map_texture_aliases.add(corrected.casefold())
            elif map_folder == ("world", "field", "u02", "tex") and map_name == "ot_09_se_colony04.dds":
                corrected = "UT_U02_se_colony04.dds"
                aliases.add(corrected)
                map_texture_aliases.add(corrected.casefold())
            elif map_folder == ("world", "field", "u09", "tex") and map_name == "ut_u09_wt_dross01.dds":
                corrected = "UT_U09_nk_dross01.dds"
                aliases.add(corrected)
                map_texture_aliases.add(corrected.casefold())
            elif map_folder == ("world", "field", "u17", "tex") and map_name == "fu_grdline01.dds":
                corrected = "fu_line01.dds"
                aliases.add(corrected)
                map_texture_fallback_aliases.add(corrected.casefold())
            elif map_folder == ("world", "object", "22", "tex") and map_name == "ut_u09_wt_dross01.dds":
                cross_directory_fallback = self.root / "world" / "field" / "U09" / "tex" / "UT_U09_nk_dross01.dds"
            elif map_folder == ("world", "object", "22", "tex") and map_name == "ot_22_er_conta01.dds":
                cross_directory_fallback = self.root / "world" / "object" / "08" / "tex" / "OT_08_er_conta01.dds"
            elif map_folder == ("world", "object", "22", "tex") and map_name in {
                "ft_f04_ht_wall_shadow.dds", "ft_f04_ht_wall06_ca.dds", "ft_f12_th_grd_d02.dds"
            }:
                cross_directory_fallback = self.root / "world" / "object" / "F12" / "tex" / requested_path.name
            elif map_folder == ("world", "object", "09", "tex") and map_name.startswith("ot_09_ys_"):
                suffix = requested_path.name[len("OT_09_ys_"):]
                cross_directory_fallback = self.root / "world" / "object" / "60" / "tex" / ("OT_60_ys_" + suffix)
            elif map_folder == ("world", "object", "09", "tex"):
                local_fallbacks = {
                    "ot_09_ko_castlemount01a.dds": "OT_F06_to_castle001.dds",
                    "ot_09_ko_road01a.dds": "OZ_09_as_9GXroad01A.dds",
                    "oz_09_framea.dds": "OT_09_kn_frame01.dds",
                    "ot_09_ns_tabuild04.dds": "OT_09_ns_tabuild03.dds",
                }
                corrected = local_fallbacks.get(map_name)
                if corrected:
                    aliases.add(corrected)
                    map_texture_fallback_aliases.add(corrected.casefold())
            elif map_folder == ("world", "field", "u24", "tex"):
                local_fallbacks = {
                    "t_cliff_high_01_b.tga": "U24_ka_rock_a.dds",
                    "t_cliff_high_02_b.tga": "U24_ka_rock_b.dds",
                }
                corrected = local_fallbacks.get(map_name)
                if corrected:
                    aliases.add(corrected)
                    map_texture_fallback_aliases.add(corrected.casefold())
            elif map_folder == ("world", "field", "f16", "tex"):
                map_fallbacks = {
                    "fz_f16_as_roada.dds": "f16_ha_gr_B.dds",
                    "fz_f16_as_grassa.dds": "f16_ha_gr_A.dds",
                    "fz_f16_as_grassb.dds": "f16_ha_gr_mask_A.dds",
                    "fz_f16_as_cliffb.dds": "f16_ha_wall_A.dds",
                    "fz_f16_as_tutia.dds": "f16_ha_underwarter_A.dds",
                }
                corrected = map_fallbacks.get(map_name)
                if corrected:
                    aliases.add(corrected)
                    map_texture_fallback_aliases.add(corrected.casefold())
            elif map_folder in (("world", "field", "u17", "tex"), ("world", "object", "u17", "tex")) and map_name == "ut_u17_to_wall01.psd":
                corrected = "UT_U17_wall01.dds"
                aliases.add(corrected)
                map_texture_fallback_aliases.add(corrected.casefold())
            if requested_path.suffix.casefold() == ".psd":
                for extension in IMAGE_EXTS:
                    corrected = requested_path.stem + extension
                    aliases.add(corrected)
                    same_directory_format_aliases.add(corrected.casefold())
            directory_id = clean_parts[-3] if len(clean_parts) >= 3 else ""
            if len(directory_id) == 4 and directory_id.isdigit():
                filename = clean_parts[-1]
                for match in re.finditer(r"\d{4}", filename):
                    if match.group(0) == directory_id:
                        continue
                    corrected = filename[: match.start()] + directory_id + filename[match.end() :]
                    aliases.add(corrected)
                    directory_aliases.add(corrected.casefold())
            # A subset of official chara VRA files use legacy compact BST keys
            # that encode the unit/variant suffix instead of the full path key.
            # Rebuild the canonical sibling filename only when all directory
            # IDs reproduce the legacy token, then require that exact sibling.
            filename = clean_parts[-1]
            if len(clean_parts) >= 6 and clean_parts[0].casefold() == "chara":
                category, unit, variant = clean_parts[-5], clean_parts[-4], clean_parts[-3]
                match = re.fullmatch(r"BST_(\d{2})_(\d{4})_(.+)", filename, re.IGNORECASE)
                if (match and category.isdigit() and len(category) == 2 and unit.isdigit() and len(unit) == 2
                        and variant.isdigit() and len(variant) == 4 and match.group(1) == category
                        and match.group(2)[:2] == unit and match.group(2)[2:] == variant[2:]):
                    corrected = f"BST_{category}{unit}_{variant}_{match.group(3)}"
                    aliases.add(corrected)
                    vra_directory_aliases.add(corrected.casefold())
                elif (match and category.isdigit() and len(category) == 2 and unit.isdigit() and len(unit) == 2
                        and variant.isdigit() and len(variant) == 4 and match.group(1) == category):
                    corrected = f"BST_{category}{unit}_{variant}_{match.group(3)}"
                    aliases.add(corrected)
                    vra_directory_aliases.add(corrected.casefold())
            if len(clean_parts) >= 5 and clean_parts[0].casefold() == "item":
                category, item_id = clean_parts[-4], clean_parts[-3]
                clean_name = re.sub(r"[ \t\r\n\u3000]+(?=\.[^.]+$)", "", clean_parts[-1])
                match = re.fullmatch(r"EM_(\d{2})_(\d{4})_(.+)", clean_name, re.IGNORECASE)
                if (match and category.isdigit() and len(category) == 2 and item_id.isdigit() and len(item_id) == 4
                        and match.group(1) == category and match.group(2) == item_id):
                    corrected = f"ET_{category}_{item_id}_{match.group(3)}"
                    aliases.add(corrected)
                    item_texture_aliases.add(corrected.casefold())
                reverse = re.fullmatch(r"ET_(\d{2})_(\d{4})_(.+)", clean_name, re.IGNORECASE)
                if (reverse and category.isdigit() and len(category) == 2 and item_id.isdigit()
                        and len(item_id) == 4 and reverse.group(1) == category and reverse.group(2) == item_id):
                    corrected_reverse = f"EM_{category}_{item_id}_{reverse.group(3)}"
                    aliases.add(corrected_reverse)
                    item_texture_aliases.add(corrected_reverse.casefold())
            item_variant_aliases: set[str] = set()
            item_geometry_aliases: set[str] = set()
            if geometry_context:
                _, geometry_parts = normalize_path(geometry_context, trim_segments=True)
                geometry_path = Path(*geometry_parts)
                geometry_stem = geometry_path.stem
                if (len(geometry_parts) >= 5 and geometry_parts[-4].casefold() == "04"
                        and geometry_parts[-3].casefold() == "8417" and geometry_parts[-2].casefold() == "model"
                        and tuple(part.casefold() for part in clean_parts) == ("item", "04", "8417", "tex", "saber_brade.tga")):
                    match = re.fullmatch(r"EM_04_8417_(01|02)", geometry_stem, re.IGNORECASE)
                    if match:
                        variant = match.group(1)
                        cross_directory_fallback = self.root / "item" / "04" / "8413" / "tex" / f"ET_04_8413_{variant}C.dds"
                        cross_directory_fallback_class = "item_sibling_texture_preview_fallback"
                if (len(geometry_parts) >= 5 and geometry_parts[0].casefold() == "item"
                        and len(geometry_parts[-4]) == 2 and geometry_parts[-4].isdigit()
                        and len(geometry_parts[-3]) == 4 and geometry_parts[-3].isdigit()
                        and geometry_parts[-2].casefold() == "model"):
                    category, item_id = geometry_parts[-4], geometry_parts[-3]
                    expected_stem = f"EM_{category}_{item_id}_"
                    variant = geometry_stem[len(expected_stem):] if geometry_stem.casefold().startswith(expected_stem.casefold()) else ""
                    expected_texture_dir = Path(*geometry_parts[:-2], "tex")
                    same_dir = tuple(part.casefold() for part in clean_parts[:-1]) == tuple(
                        part.casefold() for part in expected_texture_dir.parts
                    )
                    if (filename.casefold() == "saber_brade.tga" and variant.isdigit()
                            and 1 <= len(variant) <= 2 and same_dir):
                        corrected = f"ET_{category}_{item_id}_{variant}C.dds"
                        aliases.add(corrected)
                        item_variant_aliases.add(corrected.casefold())
                    requested = re.fullmatch(r"ET_12_0107_(0[4-9]|10)C\.dds", filename, re.IGNORECASE)
                    if (requested and category == "12" and item_id == "0107"
                            and variant in {"01", "02"} and same_dir):
                        corrected = f"ET_12_0107_{variant}C.dds"
                        aliases.add(corrected)
                        item_geometry_aliases.add(corrected.casefold())
            normalized = {
                path
                for alias in aliases
                for path in same_directory.iterdir()
                if path.is_file() and path.name.casefold() == alias.casefold()
            } if same_directory.is_dir() else set()
            if len(normalized) == 1:
                candidate_name = next(iter(normalized)).name.casefold()
                if candidate_name in vra_directory_aliases:
                    result = "vra_directory_key_filename_repair"
                elif candidate_name in map_texture_aliases:
                    result = "vra_map_texture_name_repair"
                elif candidate_name in map_texture_fallback_aliases:
                    result = "map_texture_preview_fallback"
                elif candidate_name in fx_image_aliases:
                    result = "fx_image_prefix_repair"
                elif candidate_name in item_texture_aliases:
                    result = "item_texture_prefix_repair"
                elif candidate_name in item_variant_aliases:
                    result = "item_variant_texture_repair"
                elif candidate_name in item_geometry_aliases:
                    result = "item_geometry_texture_fallback"
                elif candidate_name in same_directory_format_aliases:
                    result = "same_directory_alt_format_repair"
                elif candidate_name in directory_aliases:
                    result = "directory_id_filename_repair"
                else:
                    result = "normalized_filename"
                self.cache[cache_key] = result
                return result
            if len(normalized) > 1:
                result = "ambiguous_normalized_filename"
                self.cache[cache_key] = result
                return result
            if cross_directory_fallback is not None and cross_directory_fallback.is_file():
                result = cross_directory_fallback_class
                self.cache[cache_key] = result
                return result
            candidates = self.by_name.get(Path(clean_parts[-1]).name.casefold(), [])
            if not candidates:
                requested = Path(clean_parts[-1])
                if requested.suffix.casefold() not in IMAGE_EXTS:
                    result = "unresolved"
                else:
                    candidates = [
                        path
                        for ext in IMAGE_EXTS
                        for path in self.by_name.get((requested.stem + ext).casefold(), [])
                    ]
                    if not candidates:
                        result = "unresolved"
                    elif len(candidates) == 1:
                        result = "unique_stem_alt_format"
                    else:
                        expected = hashlib.sha256(candidates[0].read_bytes()).digest()
                        result = "identical_stem_alt_format"
                        for candidate in candidates[1:]:
                            if hashlib.sha256(candidate.read_bytes()).digest() != expected:
                                result = "ambiguous_stem_alt_format"
                                break
            elif len(candidates) == 1:
                result = "unique_basename"
            else:
                expected = hashlib.sha256(candidates[0].read_bytes()).digest()
                result = "identical_basename"
                for candidate in candidates[1:]:
                    if hashlib.sha256(candidate.read_bytes()).digest() != expected:
                        result = "ambiguous_basename"
                        break
        self.cache[cache_key] = result
        return result


def audit_dxg(path: Path) -> dict[str, int]:
    result = Counter()
    data = path.read_bytes()
    result["files"] += 1
    if len(data) < 24 or data[:4] != b"DXG ":
        result["bad_magic"] += 1
        return result
    name_bytes = struct.unpack_from("<I", data, 20)[0]
    offset = 24 + name_bytes
    if offset > len(data):
        result["bad_name_table"] += 1
        return result

    while offset + 8 <= len(data):
        chunk_type, chunk_size = struct.unpack_from("<II", data, offset)
        payload = offset + 8
        end = payload + chunk_size
        if end > len(data):
            result["bad_chunk_bounds"] += 1
            break
        if chunk_type == 1:
            result["type1_chunks"] += 1
            block = payload
            while block + 28 <= end:
                result["type1_records"] += 1
                shape_count = data[block + 4]
                block_size = struct.unpack_from("<I", data, block + 8)[0]
                remaining = end - block
                if shape_count == 0 or block_size == 0 or block_size > remaining - 28:
                    result["non_mesh_type1_records"] += 1
                    break
                block_end = block + 28 + block_size
                pos_count, nrm_count, uv0_count, uv1_count, extra_count = struct.unpack_from(
                    "<5H", data, block + 12
                )
                weight_count = struct.unpack_from("<I", data, block + 24)[0]
                shape = block + 28
                valid = True
                for _ in range(shape_count):
                    if shape + 16 > block_end:
                        valid = False
                        break
                    vert_count, face_count = struct.unpack_from("<HH", data, shape)
                    extra_bytes, shape_size = struct.unpack_from("<II", data, shape + 8)
                    if shape_size > block_end - shape - 16:
                        valid = False
                        break
                    if vert_count * 10 + face_count * 6 > shape_size or extra_bytes > shape_size:
                        valid = False
                        break
                    shape += 16 + shape_size
                arrays = (
                    pos_count * 12
                    + nrm_count * 12
                    + uv0_count * 8
                    + uv1_count * 8
                    + extra_count * 4
                    + weight_count * 4
                )
                if not valid or shape > block_end or arrays > block_end - shape:
                    result["invalid_mesh_layouts"] += 1
                    break
                result["valid_mesh_blocks"] += 1
                result["shape_records"] += shape_count
                block = block_end
            if block < end and block + 28 > end:
                result["type1_trailing_bytes"] += end - block
        offset = end
    return result


def audit_textures(
    root: Path, index: TextureIndex
) -> tuple[dict[str, dict[str, dict[str, int]]], dict[str, dict[str, int]], dict[str, dict[str, list[str]]], dict[str, int]]:
    sources = ("vra_s_base", "vra_s_mix", "vra_s_gloss", "pp_image")
    totals: dict[str, Counter[str]] = {source: Counter() for source in sources}
    unique_totals: dict[str, Counter[str]] = {source: Counter() for source in sources}
    seen_refs: dict[str, set[str]] = {source: set() for source in sources}
    unresolved_paths: dict[str, dict[str, set[str]]] = {
        source: {"unresolved": set(), "ambiguous_basename": set()}
        for source in sources if source != "pp_image"
    }
    unresolved_paths["pp_image"] = {"unresolved": set(), "ambiguous_basename": set()}
    fx_counts: Counter[str] = Counter()
    pp_counts: Counter[str] = Counter()
    files = list(walk_files(root))
    vra_count = 0
    pp_count = 0
    uvadd_vra_files = 0
    uvadd_values = 0
    for path in files:
        if path.suffix.lower() != ".vra":
            continue
        try:
            text = read_vra_text(path)
        except OSError:
            continue
        vra_count += 1
        nonzero_uvadds = [float(match.group(1)) for match in UV_ADD_FIELD_RE.finditer(text)]
        nonzero_uvadds = [value for value in nonzero_uvadds if value != 0.0]
        if nonzero_uvadds:
            uvadd_vra_files += 1
            uvadd_values += len(nonzero_uvadds)
        geometry_match = GEOMETRY_RE.search(text)
        geometry = geometry_match.group(1) if geometry_match else ""
        for slot, ref in texture_refs_in_vra(text):
            source = "vra_" + slot
            classification = index.classify(ref, geometry)
            totals[source][classification] += 1
            key, _ = normalize_path(ref, trim_segments=True)
            if classification in unresolved_paths[source]:
                unresolved_paths[source][classification].add(key)
            if key.casefold() not in seen_refs[source]:
                seen_refs[source].add(key.casefold())
                unique_totals[source][classification] += 1
        ancestors = [part.casefold() for part in path.relative_to(root).parts[:-1]]
        in_fx = "fx" in ancestors
        is_pp = in_fx and PP_FILE_RE.match(path.name)
        if in_fx:
            for block, _, _ in iter_nodes(text, NODE_RE):
                node = NODE_RE.match(block)
                if not node:
                    continue
                kind = node.group(1).casefold()
                fx_counts[kind] += 1
                anim = ANIM_FIELD_RE.search(block)
                fx_counts[f"anim_type_{anim.group(1) if anim else 'missing'}"] += 1
        if not is_pp:
            continue
        pp_count += 1
        for block, _, _ in iter_nodes(text, NODE_RE):
            node = NODE_RE.match(block)
            if not node:
                continue
            kind = node.group(1).casefold()
            pp_counts[kind] += 1
            anim = ANIM_FIELD_RE.search(block)
            pp_counts[f"anim_type_{anim.group(1) if anim else 'missing'}"] += 1
            match = FILE_FIELD_RE.search(block)
            if not match:
                pp_counts["nodes_without_file"] += 1
                continue
            pp_counts["nodes_with_file"] += 1
            ref = match.group(1)
            if not ref.strip():
                pp_counts["nodes_with_empty_file"] += 1
                continue
            classification = index.classify(ref)
            totals["pp_image"][classification] += 1
            key, _ = normalize_path(ref, trim_segments=True)
            if classification in unresolved_paths["pp_image"]:
                unresolved_paths["pp_image"][classification].add(key)
            if key.casefold() not in seen_refs["pp_image"]:
                seen_refs["pp_image"].add(key.casefold())
                unique_totals["pp_image"][classification] += 1
    pp_counts["vra_files"] = vra_count
    pp_counts["pp_files"] = pp_count
    return (
        {
            key: {"references": dict(totals[key]), "unique_paths": dict(unique_totals[key])}
            for key in totals
        },
        {"all_fx_vra": dict(fx_counts), "pp_files": dict(pp_counts)},
        {source: {kind: sorted(paths) for kind, paths in kinds.items()} for source, kinds in unresolved_paths.items()},
        {"vra_files_with_nonzero_uvadd": uvadd_vra_files, "nonzero_uvadd_fields": uvadd_values},
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("unpackdat", type=Path, help="official unpackdat directory")
    parser.add_argument("--output", type=Path, help="write the full JSON report to this file")
    args = parser.parse_args()
    root = args.unpackdat.resolve()
    if not root.is_dir():
        parser.error(f"not a directory: {root}")

    index = TextureIndex(root)
    dxg = Counter()
    dxg_anomaly_files: dict[str, list[str]] = {"bad_name_table": [], "bad_chunk_bounds": []}
    dxg_files = [path for path in walk_files(root) if path.suffix.lower() == ".dxg"]
    for path in dxg_files:
        metrics = audit_dxg(path)
        dxg.update(metrics)
        for anomaly in dxg_anomaly_files:
            if metrics.get(anomaly, 0):
                dxg_anomaly_files[anomaly].append(path.relative_to(root).as_posix())
    texture_results, pp_counts, texture_path_examples, material_features = audit_textures(root, index)
    output = {
        "root": str(root),
        "dxg": {"enumerated_files": len(dxg_files), "metrics": dict(dxg)},
        "dxg_anomaly_files": dxg_anomaly_files,
        "dxg_attr_pairs": audit_model_attr_pairs(root),
        "dxg_mesh_material_bindings": audit_mesh_material_bindings(root, index),
        "texture_index": {"image_files": index.image_count},
        "texture_references": texture_results,
        "texture_path_examples": texture_path_examples,
        "material_features": material_features,
        "fx_vra_and_pp_files": pp_counts,
    }
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    rendered = json.dumps(output, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
