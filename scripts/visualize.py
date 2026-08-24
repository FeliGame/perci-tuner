#!/usr/bin/env python3
"""Render run_history.csv as generation rows of mixed-precision configs."""

import ast
import csv
import json
import math
import os
import re

from config_utils import FUNCTION_PRECISIONS


try:
    import matplotlib.pyplot as plt
    from matplotlib.patches import Circle, Wedge
except ImportError as exc:
    raise SystemExit("visualize.py requires matplotlib in the runtime environment") from exc


COLORS = {
    "fp16": "#39a852",
    "float": "#3274c7",
    "double": "#e5b62f",
    "long double": "#d94b45",
    "unknown": "#b8b8b8",
}
ERROR_RESULTS = {"TIMEOUT", "SKIP", "NAN", "ERROR"}
MATH_FUNCTIONS = set(FUNCTION_PRECISIONS)


def parse_number(value):
    match = re.search(r"[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?", str(value))
    return float(match.group(0)) if match else None


def precision_for_type(type_name):
    value = str(type_name).lower().strip().replace("const ", "")
    if "fp16" in value or "__fp16" in value:
        return "fp16"
    if value in {"float", "float*"}:
        return "float"
    if value in {"long double", "long double*"}:
        return "long double"
    if value in {"double", "double*"}:
        return "double"
    if value in MATH_FUNCTIONS:
        return "float" if value.endswith("f") else "long double" if value.endswith("l") else "double"
    return "unknown"


def supported_elements(config):
    elements = []
    for index, item in enumerate(config.get("global") or []):
        if precision_for_type(item.get("type")) != "unknown":
            elements.append((f"global.{index}.type", item.get("type")))
    for function, items in (config.get("local") or {}).items():
        for index, item in enumerate(items or []):
            if precision_for_type(item.get("type")) != "unknown":
                elements.append((f"local.{function}.{index}.type", item.get("type")))
    for struct, items in (config.get("structs") or {}).items():
        for index, item in enumerate(items or []):
            if precision_for_type(item.get("type")) != "unknown":
                elements.append((f"structs.{struct}.{index}.type", item.get("type")))
    for caller, items in (config.get("calls") or {}).items():
        for index, item in enumerate(items or []):
            if precision_for_type(item.get("type")) != "unknown":
                elements.append((f"calls.{caller}.{index}.type", item.get("type")))
    return elements


def changes_for_node(row, elements):
    changes = ast.literal_eval(row.get("changes") or "{}")
    if not isinstance(changes, dict):
        changes = {}
    values = dict(elements)
    for key, transition in changes.items():
        if key in values and "->" in str(transition):
            values[key] = str(transition).split("->", 1)[1]
    return [precision_for_type(values[key]) for key, _ in elements]


def load_rows(path):
    with open(path, newline="") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError(f"empty history: {path}")
    required = {"run_id", "parent", "acc_rate", "rel_err", "result", "changes"}
    missing = required - set(rows[0])
    if missing:
        raise ValueError("run_history.csv is missing columns: " + ", ".join(sorted(missing)))
    return rows


def evaluation_color(row, threshold):
    if int(row["run_id"]) == 1:
        return "#ffffff"
    result = str(row.get("result", "" )).strip().upper()
    rel_err = parse_number(row.get("rel_err", ""))
    if result == "RUNTIME-ERROR":
        return "#000000"
    if (result in ERROR_RESULTS or rel_err is None or not math.isfinite(rel_err)
            or threshold is not None and rel_err > threshold):
        return "#d62728"
    acceleration = parse_number(row.get("acc_rate", ""))
    if acceleration is None or not math.isfinite(acceleration) or acceleration == 0:
        return "#ffffff"
    if acceleration < 0:
        amount = min(1.0, abs(acceleration) / 100.0)
        return (1.0, 1.0 - 0.45 * amount, 0.35 * (1.0 - amount))
    amount = min(1.0, acceleration / 100.0)
    return (1.0 - 0.55 * amount, 1.0, 1.0 - 0.55 * amount)


def draw_node(ax, x, y, row, sector_colors, radius, threshold, check=False):
    count = max(1, len(sector_colors))
    width = 360.0 / count
    for index, color in enumerate(sector_colors):
        ax.add_patch(Wedge((x, y), radius, 90 - (index + 1) * width, 90 - index * width,
                           facecolor=COLORS[color], edgecolor="white", linewidth=0.35))
    ax.add_patch(Circle((x, y), radius * 0.5, facecolor=evaluation_color(row, threshold),
                        edgecolor="#333333", linewidth=0.8, zorder=3))
    ax.text(x, y, str(row["run_id"]), ha="center", va="center", fontsize=9, zorder=4)
    if check:
        check_y = y - radius - 0.16
        ax.plot([x - 0.14, x - 0.03, x + 0.18],
                [check_y, check_y - 0.12, check_y + 0.14],
                color="#1b9e49", linewidth=1.8, solid_capstyle="round", zorder=5)


def is_valid(row, threshold):
    result = str(row.get("result", "")).strip().upper()
    rel_err = parse_number(row.get("rel_err", ""))
    acceleration = parse_number(row.get("acc_rate", ""))
    return (result not in ERROR_RESULTS and rel_err is not None
            and math.isfinite(rel_err) and rel_err <= threshold
            and acceleration is not None and math.isfinite(acceleration))


def split_generations(rows, threshold):
    ordered = sorted((row for row in rows if int(row["run_id"]) > 0),
                     key=lambda row: int(row["run_id"]))
    root = next((row for row in ordered if int(row["run_id"]) == 1), None)
    if root is None:
        raise ValueError("run_history.csv is missing the original run_id=1")

    generations = [(None, [root], None)]
    grouped = {}
    order = []
    for row in ordered:
        if int(row["run_id"]) == 1:
            continue
        parent = int(row.get("parent") or 0)
        if parent not in grouped:
            grouped[parent] = []
            order.append(parent)
        grouped[parent].append(row)
    for parent in order:
        group = grouped[parent]
        candidates = [row for row in group if is_valid(row, threshold)
                      and parse_number(row.get("acc_rate", "")) > 0]
        best = max(candidates, key=lambda row: parse_number(row["acc_rate"]), default=None)
        generations.append((parent, group, int(best["run_id"]) if best else None))
    return generations


def render(config_path, history_path, output_path):
    with open(config_path) as stream:
        config = json.load(stream)
    threshold = parse_number(config.get("result_error", "")) or 1e-6
    rows = load_rows(history_path)
    elements = supported_elements(config)
    generation_rows = split_generations(rows, threshold)

    radius = 0.58
    positions = {}
    spacing = 1.35
    row_gap = 2.05
    for row_index, (_, nodes, _) in enumerate(generation_rows):
        center = (len(nodes) - 1) * spacing / 2
        for index, row in enumerate(nodes):
            positions[int(row["run_id"])] = (index * spacing - center, -row_index * row_gap)
    width = max(8, max(len(nodes) for _, nodes, _ in generation_rows) * spacing + 3)
    height = max(4, len(generation_rows) * row_gap + 1.5)
    fig, ax = plt.subplots(figsize=(width, height), dpi=160)

    for row_index, (parent, nodes, best_id) in enumerate(generation_rows):
        label = "original" if parent is None else f"parent={parent}"
        label_y = -row_index * row_gap
        ax.text(-width / 2 + 0.1, label_y, label, ha="left", va="center", fontsize=15)
        for row in nodes:
            run_id = int(row["run_id"])
            draw_node(ax, *positions[run_id], row, changes_for_node(row, elements), radius,
                      threshold, check=run_id == best_id)
    all_y = [position[1] for position in positions.values()]
    ax.set_xlim(-width / 2, width / 2)
    ax.set_ylim(min(all_y) - 1, max(all_y) + 1)
    ax.set_aspect("equal")
    ax.axis("off")
    fig.savefig(output_path, bbox_inches="tight", pad_inches=0.2)
    plt.close(fig)


def main():
    root = os.environ.get("PERCI_TUNER")
    if not root:
        raise SystemExit("PERCI_TUNER is not set")
    render(os.path.join(root, "config.json"), os.path.join(root, "run_history.csv"),
           os.path.join(root, "run_history.png"))


if __name__ == "__main__":
    main()
