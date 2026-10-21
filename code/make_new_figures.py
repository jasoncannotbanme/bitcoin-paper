#!/usr/bin/env python3
"""Generate figures for the revised paper using measured benchmark data."""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
import os

OUT = os.path.join(os.path.dirname(__file__), '..', 'figures')
os.makedirs(OUT, exist_ok=True)

# ── Consistent style ──
plt.rcParams.update({
    'font.family': 'serif',
    'font.size': 10,
    'axes.titlesize': 11,
    'axes.labelsize': 10,
    'xtick.labelsize': 9,
    'ytick.labelsize': 9,
    'legend.fontsize': 9,
    'figure.dpi': 300,
    'savefig.dpi': 300,
    'savefig.bbox': 'tight',
    'axes.spines.top': False,
    'axes.spines.right': False,
})

BLUE = '#2166ac'
RED  = '#b2182b'
GRAY = '#666666'
GREEN = '#1b7837'


# ═══════════════════════════════════════════════════════════════
#  Fig 1: Component-level speedups (grouped bar chart)
# ═══════════════════════════════════════════════════════════════

def fig1_component_speedups():
    components = [
        'Order Book\nUpdate',
        'Depth Message\nParsing',
        'Thread\nHand-off',
        'Number\nConversion',
        'Buffer\nHandling',
    ]
    baseline = [91, 1590, 75.5, 124, 156]
    optimized = [8, 466, 28.7, 11, 104]
    speedups = [b/o for b, o in zip(baseline, optimized)]

    x = np.arange(len(components))
    w = 0.35

    fig, ax1 = plt.subplots(figsize=(8, 4.5))

    bars_b = ax1.bar(x - w/2, baseline, w, label='Baseline', color=RED, alpha=0.85, edgecolor='white', linewidth=0.5)
    bars_o = ax1.bar(x + w/2, optimized, w, label='Optimised', color=BLUE, alpha=0.85, edgecolor='white', linewidth=0.5)

    ax1.set_ylabel('Median latency (ns)')
    ax1.set_xticks(x)
    ax1.set_xticklabels(components)
    ax1.set_yscale('log')
    ax1.yaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: f'{v:,.0f}'))
    ax1.legend(loc='upper right')

    # Add speedup annotations
    for i, (b, o, s) in enumerate(zip(baseline, optimized, speedups)):
        ax1.annotate(f'{s:.1f}×',
                     xy=(x[i] + w/2, o),
                     xytext=(0, -14), textcoords='offset points',
                     ha='center', va='top', fontsize=8, fontweight='bold',
                     color=BLUE)

    ax1.set_title('Measured Speedup per Pipeline Component (p50, GCC 16.1 -O2)')
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'fig1_component_speedups.png'))
    plt.close(fig)
    print('  fig1_component_speedups.png')


# ═══════════════════════════════════════════════════════════════
#  Fig 2: Order book scaling (tree vs ladder across depths)
# ═══════════════════════════════════════════════════════════════

def fig2_book_scaling():
    # Data from scaling_output.csv (median of 5 runs)
    widths     = [16,    64,    256,   1024,  4096,  16384, 65536, 262144]
    active     = [22,    90,    358,   1434,  5734,  22936, 91750, 330000]
    map_p50    = [53,    62,    79,    86,    96,    117,   158,   617]
    map_p99    = [79,    102,   137,   172,   229,   310,   438,   1312]
    ladder_p50 = [9,     8,     8,     9,     9,     8,     8,     9]
    ladder_p99 = [12,    12,    12,    12,    12,    12,    12,    13]

    fig, ax = plt.subplots(figsize=(7, 4.5))

    ax.plot(active, map_p50, 'o-', color=RED, label='std::map (p50)', markersize=4, linewidth=1.5)
    ax.fill_between(active, map_p50, map_p99, color=RED, alpha=0.12, label='std::map (p50–p99)')
    ax.plot(active, ladder_p50, 's-', color=BLUE, label='Price ladder (p50)', markersize=4, linewidth=1.5)
    ax.fill_between(active, ladder_p50, ladder_p99, color=BLUE, alpha=0.12, label='Price ladder (p50–p99)')

    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.set_xlabel('Active price levels')
    ax.set_ylabel('Time per update (ns)')
    ax.set_title('Order-Book Update Latency vs. Book Depth')
    ax.legend(loc='upper left', framealpha=0.9)
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: f'{v:,.0f}'))
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: f'{v:,.0f}'))
    ax.set_ylim(5, 2000)

    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'fig2_book_depth_scaling.png'))
    plt.close(fig)
    print('  fig2_book_depth_scaling.png')


# ═══════════════════════════════════════════════════════════════
#  Fig 3: Full pipeline comparison
# ═══════════════════════════════════════════════════════════════

def fig3_pipeline():
    labels = ['Naive Pipeline\n(from_chars + std::map)', 'Optimised Pipeline\n(zero-copy + ladder)']
    p50 = [2868, 494]
    p95 = [3464, 568]
    p99 = [4748, 978]

    x = np.arange(len(labels))
    w = 0.22

    fig, ax = plt.subplots(figsize=(6, 4))
    ax.bar(x - w, p50, w, label='p50', color=BLUE, alpha=0.9, edgecolor='white')
    ax.bar(x,     p95, w, label='p95', color='#4393c3', alpha=0.85, edgecolor='white')
    ax.bar(x + w, p99, w, label='p99', color='#92c5de', alpha=0.8, edgecolor='white')

    # Speedup annotation
    ax.annotate('5.8× faster',
                xy=(1, p50[1]), xytext=(0.5, 1800),
                arrowprops=dict(arrowstyle='->', color=GREEN, lw=1.5),
                fontsize=10, fontweight='bold', color=GREEN, ha='center')

    ax.set_ylabel('Latency per message (ns)')
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.legend(loc='upper right')
    ax.set_title('Full Parse-to-Book Pipeline: Naive vs. Optimised')

    for i, v in enumerate(p50):
        ax.text(i - w, v + 80, f'{v:,}', ha='center', va='bottom', fontsize=8, fontweight='bold')

    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'fig3_pipeline_comparison.png'))
    plt.close(fig)
    print('  fig3_pipeline_comparison.png')


# ═══════════════════════════════════════════════════════════════
#  Fig 4: Burst drain timeline
# ═══════════════════════════════════════════════════════════════

def fig4_burst_drain():
    burst_size = 500
    naive_per_msg_ns = 2943
    opt_per_msg_ns = 490  # measured

    # Timeline: messages processed over time
    naive_times = np.arange(burst_size) * naive_per_msg_ns / 1e6  # ms
    opt_times = np.arange(burst_size) * opt_per_msg_ns / 1e6

    fig, ax = plt.subplots(figsize=(7, 4))

    ax.plot(naive_times, np.arange(burst_size), color=RED, linewidth=2, label='Naive pipeline (2,943 ns/msg)')
    ax.plot(opt_times, np.arange(burst_size), color=BLUE, linewidth=2, label='Optimised pipeline (490 ns/msg)')

    # Mark completion times
    naive_done = naive_times[-1]
    opt_done = opt_times[-1]

    ax.axvline(opt_done, color=BLUE, linestyle='--', alpha=0.5, linewidth=1)
    ax.axvline(naive_done, color=RED, linestyle='--', alpha=0.5, linewidth=1)

    # Shade the gap
    ax.axvspan(opt_done, naive_done, alpha=0.08, color=GREEN)
    mid = (opt_done + naive_done) / 2
    ax.annotate(f'1.22 ms advantage',
                xy=(mid, 250), ha='center', va='center',
                fontsize=10, fontweight='bold', color=GREEN,
                bbox=dict(boxstyle='round,pad=0.3', facecolor='white', edgecolor=GREEN, alpha=0.9))

    ax.set_xlabel('Time (ms)')
    ax.set_ylabel('Messages processed')
    ax.set_title(f'Queue Drain During a {burst_size}-Message Burst')
    ax.legend(loc='lower right')
    ax.set_xlim(0, naive_done * 1.1)

    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'fig4_burst_drain.png'))
    plt.close(fig)
    print('  fig4_burst_drain.png')


# ═══════════════════════════════════════════════════════════════
#  Fig 5: Time-scale comparison (log axis)
# ═══════════════════════════════════════════════════════════════

def fig5_time_scales():
    entries = [
        ('Ladder book update',        8e-9,    BLUE,   'Our benchmark'),
        ('Fixed-point parse (1 num)',  11e-9,   BLUE,   'Our benchmark'),
        ('SPSC ring push/pop',         29e-9,   BLUE,   'Our benchmark'),
        ('std::map book update',       91e-9,   RED,    'Our benchmark'),
        ('Optimised full pipeline',    528e-9,  BLUE,   'Our benchmark'),
        ('Baseline full pipeline',     2943e-9, RED,    'Our benchmark'),
        ('FPGA book update [10]',      253e-9,  GRAY,   'Dvořák & Kořenek'),
        ('FPGA end-to-end [11]',       1e-6,    GRAY,   'Lockwood et al.'),
        ('Software platform [10]',     10e-6,   GRAY,   'Dvořák & Kořenek'),
        ('Co-located network RTT [1]', 2e-3,    '#d6604d', 'Albers et al.'),
        ('Network σ (jitter) [1]',     1e-3,    '#d6604d', 'Albers et al.'),
        ('Depth snapshot interval [15]', 100e-3, '#d6604d', 'Binance docs'),
        ('BTC block propagation [8]',  10,      '#878787', 'Decker & Wattenhofer'),
        ('BTC confirmation (~1 blk)',   600,    '#878787', 'Consensus protocol'),
    ]

    fig, ax = plt.subplots(figsize=(9, 5.5))

    for i, (label, time_s, color, source) in enumerate(entries):
        ax.barh(i, time_s, color=color, alpha=0.8, height=0.6, edgecolor='white', linewidth=0.5)
        # Label on the right
        ax.text(time_s * 1.5, i, f'  {label}', va='center', ha='left', fontsize=8)

    ax.set_xscale('log')
    ax.set_xlabel('Time (seconds)')
    ax.set_xlim(1e-9, 1e4)
    ax.set_yticks(range(len(entries)))
    ax.set_yticklabels(['' for _ in entries])
    ax.invert_yaxis()
    ax.set_title('Latency Time Scales: Our Benchmarks vs. Published Figures')

    # Add vertical reference lines
    for val, label in [(1e-9, '1 ns'), (1e-6, '1 µs'), (1e-3, '1 ms'), (1, '1 s')]:
        ax.axvline(val, color='#cccccc', linestyle=':', linewidth=0.8, zorder=0)
        ax.text(val, -0.8, label, ha='center', va='bottom', fontsize=7, color='#999999')

    # Legend
    from matplotlib.patches import Patch
    legend_elements = [
        Patch(facecolor=BLUE, alpha=0.8, label='Our optimised'),
        Patch(facecolor=RED, alpha=0.8, label='Our baseline'),
        Patch(facecolor=GRAY, alpha=0.8, label='Published (equity/FPGA)'),
        Patch(facecolor='#d6604d', alpha=0.8, label='Published (crypto network)'),
        Patch(facecolor='#878787', alpha=0.8, label='Blockchain'),
    ]
    ax.legend(handles=legend_elements, loc='lower right', fontsize=8, framealpha=0.9)

    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'fig5_time_scales.png'))
    plt.close(fig)
    print('  fig5_time_scales.png')


# ═══════════════════════════════════════════════════════════════
#  Fig 6: Burst throughput sustainability
# ═══════════════════════════════════════════════════════════════

def fig6_burst_throughput():
    # From advanced_examples.cpp Example C output
    burst_sizes = [10, 50, 100, 500, 1000]
    per_msg_ns  = [480, 476, 483, 490, 487]
    throughput_M = [2.08, 2.10, 2.07, 2.04, 2.05]

    fig, ax1 = plt.subplots(figsize=(6.5, 4))

    color1 = BLUE
    ax1.plot(burst_sizes, per_msg_ns, 'o-', color=color1, linewidth=2, markersize=6, label='Per-message latency')
    ax1.set_xlabel('Burst size (messages)')
    ax1.set_ylabel('Per-message latency (ns)', color=color1)
    ax1.tick_params(axis='y', labelcolor=color1)
    ax1.set_ylim(400, 550)
    ax1.set_xscale('log')

    ax2 = ax1.twinx()
    color2 = GREEN
    ax2.plot(burst_sizes, throughput_M, 's--', color=color2, linewidth=1.5, markersize=5, alpha=0.8, label='Throughput')
    ax2.set_ylabel('Throughput (million msg/s)', color=color2)
    ax2.tick_params(axis='y', labelcolor=color2)
    ax2.set_ylim(1.5, 2.5)

    ax1.set_title('Optimised Pipeline: Sustained Performance Under Burst Load')

    # Combined legend
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc='lower left', framealpha=0.9)

    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'fig6_burst_throughput.png'))
    plt.close(fig)
    print('  fig6_burst_throughput.png')


if __name__ == '__main__':
    print('Generating figures...')
    fig1_component_speedups()
    fig2_book_scaling()
    fig3_pipeline()
    fig4_burst_drain()
    fig5_time_scales()
    fig6_burst_throughput()
    print('Done.')
