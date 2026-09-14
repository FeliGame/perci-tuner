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
    from matplotlib.collections import LineCollection
    from matplotlib.patches import Circle, Patch, Wedge
    import numpy as np
except ImportError as exc:
    raise SystemExit("visualize.py requires matplotlib and numpy in the runtime environment") from exc


COLORS = {
    "fp16": "#39a852",
    "float": "#3274c7",
    "double": "#e5b62f",
    "long double": "#d94b45",
    "unknown": "#b8b8b8",
}
ERROR_RESULTS = {"TIMEOUT", "SKIP", "NAN", "ERROR"}
MATH_FUNCTIONS = set(FUNCTION_PRECISIONS)

# Numeric precision levels used as dimensionality-reduction features.
PRECISION_RANK = {"fp16": 0, "float": 1, "double": 2, "long double": 3, "unknown": 1}
# How strongly each downgrade ratio darkens its RGB channel (R: double, G:
# float, B: fp16); 0 keeps the white baseline, 1 reaches the pure channel.
DOWNGRADE_SCALE = 0.85
# Marker area (points^2) grows with the acceleration ratio (percentage
# points): bigger dots mean higher speedup / better effect. Points whose
# relative error exceeds the threshold are drawn gray instead.
POINT_SIZE_BASE = 14
POINT_SIZE_SCALE = 0.7
POINT_SIZE_MAX = 120
# Extra ring diameter in points around the final-config marker.
RING_PADDING = 6


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


def node_states(row, elements):
    """Return (original precision, final precision) pairs for every element."""
    changes = ast.literal_eval(row.get("changes") or "{}")
    if not isinstance(changes, dict):
        changes = {}
    values = dict(elements)
    for key, transition in changes.items():
        if key in values and "->" in str(transition):
            values[key] = str(transition).split("->", 1)[1]
    return [(precision_for_type(original), precision_for_type(values[key]))
            for key, original in elements]


def changes_for_node(row, elements):
    return [final for _, final in node_states(row, elements)]


def downgrade_rgb(states):
    """White baseline; R/G/B darken with the ratio of elements downgraded to
    double / float / fp16 respectively, so darker points are more downgraded."""
    counts = {"double": 0.0, "float": 0.0, "fp16": 0.0}
    total = max(1, len(states))
    for original, final in states:
        if PRECISION_RANK[final] < PRECISION_RANK[original] and final in counts:
            counts[final] += 1.0
    ratio = {key: value / total for key, value in counts.items()}
    return (1.0 - DOWNGRADE_SCALE * ratio["double"],
            1.0 - DOWNGRADE_SCALE * ratio["float"],
            1.0 - DOWNGRADE_SCALE * ratio["fp16"])


def point_size(row):
    """Marker area in points^2; grows with positive acceleration ratio."""
    acceleration = parse_number(row.get("acc_rate", ""))
    if acceleration is None or not math.isfinite(acceleration) or acceleration <= 0:
        return float(POINT_SIZE_BASE)
    return float(min(POINT_SIZE_MAX, POINT_SIZE_BASE + POINT_SIZE_SCALE * acceleration))


def is_over_error(row, threshold):
    """True when the run failed or its relative error exceeds the threshold."""
    result = str(row.get("result", "")).strip().upper()
    rel_err = parse_number(row.get("rel_err", ""))
    return (result in ERROR_RESULTS or rel_err is None
            or not math.isfinite(rel_err) or rel_err > threshold)


def final_run(rows, threshold):
    """Run id of the fastest valid config, matching the search's best config."""
    candidates = [row for row in rows if is_valid(row, threshold)
                  and parse_number(row.get("acc_rate", "")) > 0]
    if candidates:
        return int(max(candidates, key=lambda row: parse_number(row["acc_rate"]))["run_id"])
    return 1


def reduce_dimensions(method, features):
    """Return (2D coordinates, note); note explains any fallback used."""
    sample_count = len(features)
    if method == "tsne":
        try:
            from sklearn.manifold import TSNE
        except ImportError as exc:
            raise SystemExit(
                "t-SNE visualization requires scikit-learn: pip install scikit-learn"
            ) from exc
        params = {"n_components": 2, "init": "pca", "random_state": 42, "max_iter": 1000}
        # Perplexity must be strictly smaller than the number of samples.
        if sample_count <= 31:
            params["perplexity"] = max(1.0, float(sample_count - 1))
        return TSNE(**params).fit_transform(features), None

    if method != "umap":
        raise ValueError(f"unknown projection method: {method}")
    try:
        import umap
    except ImportError as exc:
        raise SystemExit(
            "UMAP visualization requires umap-learn: pip install umap-learn"
        ) from exc
    n_neighbors = min(15, max(2, sample_count - 1))
    try:
        return umap.UMAP(n_neighbors=n_neighbors, min_dist=0.1, n_components=2,
                         random_state=42).fit_transform(features), None
    except Exception as exc:
        # UMAP cannot build a neighbor graph for tiny/degenerate histories;
        # fall back to PCA so a plot is still produced.
        from sklearn.decomposition import PCA
        note = f"UMAP failed to embed {sample_count} configs ({exc}); PCA fallback used"
        print("warning:", note)
        return PCA(n_components=2, random_state=42).fit_transform(features), note


def render_projection(method, config_path, history_path, output_path):
    with open(config_path) as stream:
        config = json.load(stream)
    threshold = parse_number(config.get("result_error", "")) or 1e-6
    rows = sorted((row for row in load_rows(history_path) if int(row["run_id"]) > 0),
                  key=lambda row: int(row["run_id"]))
    elements = supported_elements(config)

    states = [node_states(row, elements) for row in rows]
    features = np.array([[PRECISION_RANK[final] for _, final in state] for state in states],
                        dtype=float)
    if len(rows) >= 2:
        coords, _ = reduce_dimensions(method, features)
    else:
        coords, _ = np.zeros((1, 2)), None

    fig, ax = plt.subplots(figsize=(9, 7), dpi=160)
    point_colors = [downgrade_rgb(state) for state in states]
    sizes = [point_size(row) for row in rows]
    # Faint path following the configs in search order (run_id); segments
    # fade from invisible to gray to indicate the increasing config index.
    if len(rows) >= 2:
        segments = np.stack([coords[:-1], coords[1:]], axis=1)
        line_colors = [(0.8, 0.8, 0.8, alpha) for alpha in np.linspace(0.0, 0.7, len(segments))]
        ax.add_collection(LineCollection(segments, colors=line_colors,
                                         linewidths=0.9, zorder=2))
    over_flags = [is_over_error(row, threshold) for row in rows]
    normal = [i for i, over in enumerate(over_flags) if not over]
    failed = [i for i, over in enumerate(over_flags) if over]
    if normal:
        ax.scatter(coords[normal, 0], coords[normal, 1],
                   s=[sizes[i] for i in normal],
                   c=[point_colors[i] for i in normal],
                   edgecolors="none", zorder=3)
    if failed:
        ax.scatter(coords[failed, 0], coords[failed, 1],
                   s=[sizes[i] for i in failed],
                   c="#999999", edgecolors="none", zorder=3)

    final_id = final_run(rows, threshold)
    final_index = next(index for index, row in enumerate(rows)
                       if int(row["run_id"]) == final_id)
    # Draw rings as hollow dashed markers in display space so they hug their
    # points instead of scaling with the data range.
    ring_size = (math.sqrt(sizes[final_index]) + RING_PADDING) ** 2
    ax.scatter([coords[final_index, 0]], [coords[final_index, 1]], s=ring_size,
               facecolors="none", edgecolors="#d62728", linewidths=1.5,
               linestyles="--", zorder=6)
    start_index = next(index for index, row in enumerate(rows)
                       if int(row["run_id"]) == 1)
    start_ring_size = (math.sqrt(sizes[start_index]) + RING_PADDING) ** 2
    ax.scatter([coords[start_index, 0]], [coords[start_index, 1]], s=start_ring_size,
               facecolors="none", edgecolors="#1f77b4", linewidths=1.5,
               linestyles="--", zorder=6)

    dark = DOWNGRADE_SCALE
    legend_handles = [
        Patch(facecolor=(1 - dark, 1, 1), edgecolor="#444444",
              label="double ratio"),
        Patch(facecolor=(1, 1 - dark, 1), edgecolor="#444444",
              label="float ratio"),
        Patch(facecolor=(1, 1, 1 - dark), edgecolor="#444444",
              label="fp16 ratio"),
        Patch(facecolor="none", edgecolor="#1f77b4", linestyle="--", label="start config"),
        Patch(facecolor="none", edgecolor="#d62728", linestyle="--", label="final config"),
    ]
    ax.legend(handles=legend_handles, loc="best", fontsize=8, framealpha=0.9)
    ax.set_xticks([])
    ax.set_yticks([])
    ax.margins(0.18)
    ax.set_aspect("equal")
    fig.savefig(output_path, bbox_inches="tight", pad_inches=0.2)
    plt.close(fig)


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

    config_path = os.path.join(root, "config.json")
    history_path = os.path.join(root, "run_history.csv")
    for method in ("tsne", "umap"):
        print(f"computing {method.upper()} projection ...", flush=True)
        output_path = os.path.join(root, f"run_history_{method}.png")
        render_projection(method, config_path, history_path, output_path)
        print("wrote", output_path, flush=True)
    print("rendering generation view (slow for large histories) ...", flush=True)
    output_path = os.path.join(root, "run_history.png")
    render(config_path, history_path, output_path)
    print("wrote", output_path, flush=True)


if __name__ == "__main__":
    main()
