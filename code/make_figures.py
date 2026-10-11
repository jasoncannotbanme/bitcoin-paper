"""Draws the three figures of the paper.

Usage:  python make_figures.py [output_directory]
Needs:  matplotlib

Figures 1 and 3 use the values printed in the paper (Table 3 and the cited
sources). Figure 2 reads scaling_output.csv, which holds five runs of
`latency_examples_cpp --scaling`, and plots the median over the runs.
"""
import csv
import os
import statistics
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "figures")
os.makedirs(OUT, exist_ok=True)

BLUE, ORANGE = "#2a78d6", "#eb6834"   # checked for color-blind separation
INK, MUTED, GRID = "#0b0b0b", "#52514e", "#dddcd8"
plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["Times New Roman", "Times", "DejaVu Serif"],
    "font.size": 8, "axes.edgecolor": MUTED, "axes.linewidth": 0.6,
    "axes.labelcolor": INK, "xtick.color": MUTED, "ytick.color": INK,
    "xtick.labelsize": 8, "ytick.labelsize": 8, "legend.frameon": False,
    "savefig.dpi": 300, "figure.dpi": 100,
})
WIDTH = 4.8  # inches, the width of the text column


def style(ax):
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    ax.grid(axis="x", color=GRID, linewidth=0.5)
    ax.set_axisbelow(True)


def fmt_ns(v):
    return f"{v:,.0f}" if v >= 10 else f"{v:g}"


# ------------------------------------------------------------------
# Figure 1: baseline versus optimized medians (Table 3)
# ------------------------------------------------------------------
rows = [  # label, baseline ns, optimized ns
    ("Order-book update", 91, 8),
    ("Parse one message", 1586, 464),
    ("Parse and update book", 2860, 496),
    ("Copy and scan one message (C)", 156, 104),
    ("Convert one price string (C)", 124, 11),
    ("Thread hand-off", 76, 30),
]
fig, ax = plt.subplots(figsize=(WIDTH, 2.2))
style(ax)
for i, (label, base, opt) in enumerate(rows):
    y = len(rows) - 1 - i
    ax.plot([opt, base], [y, y], color=MUTED, linewidth=1.0, zorder=1)
    ax.scatter([base], [y], s=34, color=BLUE, marker="o", zorder=2,
               label="Baseline" if i == 0 else None)
    ax.scatter([opt], [y], s=34, color=ORANGE, marker="s", zorder=2,
               label="Optimized" if i == 0 else None)
    ax.annotate(fmt_ns(base), (base, y), xytext=(6, 0),
                textcoords="offset points", va="center", color=INK)
    ax.annotate(fmt_ns(opt), (opt, y), xytext=(-6, 0),
                textcoords="offset points", va="center", ha="right", color=INK)
ax.set_xscale("log")
ax.set_xlim(2, 12000)
ax.set_ylim(-0.6, len(rows) - 0.4)
ax.set_yticks(range(len(rows)))
ax.set_yticklabels([r[0] for r in reversed(rows)])
ax.tick_params(axis="y", length=0)
ax.set_xlabel("Median time per operation (ns, logarithmic scale)")
ax.legend(loc="lower right", ncol=2, handletextpad=0.2, columnspacing=1.2,
          bbox_to_anchor=(1.0, 1.0), borderaxespad=0.2)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig1_baseline_vs_optimized.png"))
plt.close(fig)

# ------------------------------------------------------------------
# Figure 2: order-book update time versus book depth
# ------------------------------------------------------------------
runs = {}
with open(os.path.join(HERE, "scaling_output.csv"), encoding="utf8") as f:
    for row in csv.reader(l for l in f if not l.startswith("#")):
        if row[0] == "width":
            continue
        runs.setdefault(int(row[0]), []).append([float(v) for v in row[1:]])
table = []
for width in sorted(runs):
    med = [statistics.median(col) for col in zip(*runs[width])]
    table.append([width] + med)
levels = [t[1] for t in table]
map_p50, map_p99 = [t[3] for t in table], [t[5] for t in table]
lad_p50, lad_p99 = [t[6] for t in table], [t[8] for t in table]

with open(os.path.join(OUT, "fig2_data.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["width", "active_levels", "ladder_bytes", "map_p50", "map_p95",
                "map_p99", "ladder_p50", "ladder_p95", "ladder_p99"])
    w.writerows(table)

fig, ax = plt.subplots(figsize=(WIDTH, 2.3))
style(ax)
ax.grid(axis="y", color=GRID, linewidth=0.5)
ax.fill_between(levels, map_p50, map_p99, color=BLUE, alpha=0.12, linewidth=0)
ax.fill_between(levels, lad_p50, lad_p99, color=ORANGE, alpha=0.15, linewidth=0)
ax.plot(levels, map_p50, color=BLUE, linewidth=1.6, marker="o", markersize=4.5)
ax.plot(levels, lad_p50, color=ORANGE, linewidth=1.6, marker="s",
        markersize=4.5, linestyle="--")
ax.set_xscale("log")
ax.set_yscale("log")
ax.set_xlim(12, 3.0e6)
ax.set_ylim(4, 3000)
ax.set_xlabel("Active price levels in the book (logarithmic scale)")
ax.set_ylabel("Time per update (ns, logarithmic scale)")
ax.annotate("std::map (baseline)", (levels[-1], map_p50[-1]), xytext=(7, 0),
            textcoords="offset points", va="center", color=INK)
ax.annotate("Flat price ladder", (levels[-1], lad_p50[-1]), xytext=(7, 0),
            textcoords="offset points", va="center", color=INK)
for x, y in ((levels[0], map_p50[0]), (levels[-1], map_p50[-1])):
    ax.annotate(fmt_ns(y), (x, y), xytext=(0, 7), textcoords="offset points",
                ha="center", color=INK)
for x, y in ((levels[0], lad_p50[0]), (levels[-1], lad_p50[-1])):
    ax.annotate(fmt_ns(y), (x, y), xytext=(0, -11), textcoords="offset points",
                ha="center", color=INK)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig2_book_depth_scaling.png"))
plt.close(fig)

# ------------------------------------------------------------------
# Figure 3: time scales in the reviewed sources and in this paper
# ------------------------------------------------------------------
NS, US, MS = 1.0, 1e3, 1e6
scale = [  # label, low ns, high ns, measured in this paper?
    ("Ladder book update (this paper)", 8 * NS, 8 * NS, True),
    ("FPGA book update [10]", 253 * NS, 253 * NS, False),
    ("Parse and update book, optimized (this paper)", 496 * NS, 496 * NS, True),
    ("FPGA end-to-end [11]", 1 * US, 1 * US, False),
    ("Parse and update book, baseline (this paper)", 2.86 * US, 2.86 * US, True),
    ("Software platforms, tens of microseconds [10]", 10 * US, 100 * US, False),
    ("Send time from co-located server [1]", 2 * MS, 2 * MS, False),
    ("Order-book snapshot interval [1]", 20 * MS, 200 * MS, False),
    ("Depth stream update interval [15]", 100 * MS, 1000 * MS, False),
]
fig, ax = plt.subplots(figsize=(WIDTH, 2.5))
style(ax)
seen = set()
for i, (label, lo, hi, ours) in enumerate(scale):
    y = len(scale) - 1 - i
    color, marker = (ORANGE, "s") if ours else (BLUE, "o")
    name = "Measured in this paper" if ours else "Reported in the cited source"
    if hi > lo:
        ax.plot([lo, hi], [y, y], color=color, linewidth=4,
                solid_capstyle="round", zorder=2)
    ax.scatter([lo, hi] if hi > lo else [lo], [y] * (2 if hi > lo else 1),
               s=30, color=color, marker=marker, zorder=3,
               label=None if name in seen else name)
    seen.add(name)
ax.set_xscale("log")
ax.set_xlim(2, 5e9)
ax.set_xticks([1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9])
ax.set_xticklabels(["1 ns", "", "", "1 µs", "", "", "1 ms", "", "", "1 s"])
ax.set_ylim(-0.6, len(scale) - 0.4)
ax.set_yticks(range(len(scale)))
ax.set_yticklabels([s[0] for s in reversed(scale)])
ax.tick_params(axis="y", length=0)
ax.set_xlabel("Time (logarithmic scale)")
ax.legend(loc="lower right", bbox_to_anchor=(1.0, 1.0), ncol=2,
          handletextpad=0.2, columnspacing=1.2, borderaxespad=0.2)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig3_time_scales.png"))
plt.close(fig)

for t in table:
    print(t)
print("figures written to", os.path.abspath(OUT))
