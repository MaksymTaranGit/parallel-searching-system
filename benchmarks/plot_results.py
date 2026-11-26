import csv
from pathlib import Path
import matplotlib.pyplot as plt

RES_DIR = Path("benchmarks/results")
OUT_DIR = Path("benchmarks/charts")

plt.rcParams.update({
    "figure.facecolor": "white",
    "axes.facecolor": "#fafbfc",
    "axes.edgecolor": "#cbd5e1",
    "axes.grid": True,
    "grid.color": "#e2e8f0",
    "grid.linestyle": "--",
    "grid.alpha": 0.8,
    "font.size": 10,
    "axes.titlesize": 11.5,
    "axes.titleweight": "bold",
    "axes.labelsize": 10,
})


def read_csv(name: str) -> list[dict]:
    with (RES_DIR / name).open(encoding="utf-8") as f:
        return list(csv.DictReader(f))


def save_fig(fig: plt.Figure, filename: str):
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUT_DIR / filename
    fig.savefig(path, dpi=300, bbox_inches="tight")
    plt.close(fig)
    print(f"Збережено: {path}")


def plot_indexing_time_speedup(rows: list[dict]):
    threads = [int(r["threads"]) for r in rows]
    labels = [str(t) for t in threads]
    x = list(range(len(threads)))
    med_s = [float(r["median_ms"]) / 1000.0 for r in rows]
    std_s = [float(r["stdev_ms"]) / 1000.0 for r in rows]
    speedups = [float(r["speedup"]) for r in rows]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12.5, 4.8))

    # 1. Час побудови індексу
    bars = ax1.bar(x, med_s, yerr=std_s, capsize=4, color="#3b82f6", alpha=0.88, edgecolor="#1d4ed8")
    ax1.set_xticks(x, labels)
    ax1.set_xlabel("Кількість потоків (N)")
    ax1.set_ylabel("Медіанний час побудови, с")
    ax1.set_title("Час побудови індексу (11 933 документи)")
    ax1.set_ylim(0, max(med_s) * 1.15)
    for bar, val in zip(bars, med_s):
        ax1.text(
            bar.get_x() + bar.get_width() / 2, val + 1.4,
            f"{val:.1f}с", ha="center", va="bottom", fontsize=8.5, fontweight="bold",
        )

    # 2. Прискорення S(N) vs ідеальне лінійне
    ax2.plot([1, 2, 4, 8, 9.5], [1, 2, 4, 8, 9.5], "--", color="#94a3b8", linewidth=1.6, label="Ідеальне лінійне (S = N)")
    ax2.plot(threads, speedups, "-o", color="#10b981", linewidth=2.2, markersize=6, label="Реальне прискорення S(N)")
    ax2.axvline(16, color="#f59e0b", linestyle=":", linewidth=1.4, label="16 логічних ядер (nproc)")
    ax2.set_xticks([1, 4, 8, 16, 32, 64])
    ax2.set_xlim(0, 67)
    ax2.set_ylim(0, 9.2)
    ax2.set_xlabel("Кількість потоків (N)")
    ax2.set_ylabel("Прискорення S(N) = T(1) / T(N)")
    ax2.set_title("Масштабованість індексації: прискорення")
    ax2.legend(loc="lower right", frameon=True)

    offsets = {1: (6, -2), 2: (6, -3), 4: (6, -3), 8: (5, -13), 12: (-8, 8), 16: (4, -14), 32: (0, 8), 64: (-10, 8)}
    for t, s in zip(threads, speedups):
        dx, dy = offsets.get(t, (0, 8))
        ax2.annotate(f"{s:.2f}x", (t, s), textcoords="offset points", xytext=(dx, dy), fontsize=8.5)

    fig.tight_layout()
    save_fig(fig, "01_indexing_time_speedup.png")


def plot_indexing_efficiency_throughput(rows: list[dict]):
    labels = [r["threads"] for r in rows]
    x = list(range(len(rows)))
    eff = [float(r["efficiency_pct"]) for r in rows]
    docs_s = [float(r["docs_per_sec"]) for r in rows]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12.5, 4.6))

    # 1. Ефективність E(N)
    ax1.axhline(100, color="#94a3b8", linestyle="--", linewidth=1.2, label="100% (ідеал)")
    ax1.plot(x, eff, "-o", color="#6366f1", linewidth=2.2, markersize=6, label="Ефективність E(N)")
    ax1.fill_between(x, eff, color="#6366f1", alpha=0.1)
    ax1.set_xticks(x, labels)
    ax1.set_ylim(0, 118)
    ax1.set_xlabel("Кількість потоків (N)")
    ax1.set_ylabel("Ефективність E(N), %")
    ax1.set_title("Ефективність розпаралелювання індексації")
    ax1.legend(loc="upper right", frameon=True)
    for xi, val in zip(x, eff):
        ax1.annotate(f"{val:.1f}%", (xi, val), textcoords="offset points", xytext=(0, 7), ha="center", fontsize=8.5)

    # 2. Пропускна здатність індексації (док/с)
    bars = ax2.bar(x, docs_s, color="#0ea5e9", alpha=0.88, edgecolor="#0369a1")
    ax2.set_xticks(x, labels)
    ax2.set_ylim(0, max(docs_s) * 1.15)
    ax2.set_xlabel("Кількість потоків (N)")
    ax2.set_ylabel("Швидкість обробки, док/с")
    ax2.set_title("Пропускна здатність побудови індексу")
    for bar, val in zip(bars, docs_s):
        ax2.text(
            bar.get_x() + bar.get_width() / 2, val + 20,
            f"{val:.0f}", ha="center", va="bottom", fontsize=8.5, fontweight="bold",
        )

    fig.tight_layout()
    save_fig(fig, "02_indexing_efficiency_throughput.png")


def plot_search_keyword(rows: list[dict]):
    labels = [r["clients"] for r in rows]
    x = list(range(len(rows)))
    rps = [float(r["rps"]) for r in rows]
    p50 = [float(r["p50_ms"]) for r in rows]
    p95 = [float(r["p95_ms"]) for r in rows]
    p99 = [float(r["p99_ms"]) for r in rows]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12.5, 4.6))

    # 1. RPS
    ax1.plot(x, rps, "-o", color="#2563eb", linewidth=2.2, markersize=6, label="Throughput (RPS)")
    ax1.fill_between(x, rps, color="#2563eb", alpha=0.1)
    ax1.axvline(labels.index("16"), color="#f59e0b", linestyle=":", linewidth=1.4, label="Насичення (16 потоків Crow)")
    ax1.set_xticks(x, labels)
    ax1.set_ylim(0, max(rps) * 1.18)
    ax1.set_xlabel("Кількість одночасних клієнтів (C)")
    ax1.set_ylabel("Запитів за секунду (RPS)")
    ax1.set_title("Пошук 'keyword': пропускна здатність (RPS)")
    ax1.legend(loc="lower right", frameon=True)
    for xi, val in zip(x, rps):
        ax1.annotate(f"{val:.1f}", (xi, val), textcoords="offset points", xytext=(0, 8), ha="center", fontsize=8.5)

    # 2. Затримки p50 / p95 / p99
    ax2.plot(x, p50, "-o", color="#10b981", linewidth=2.0, markersize=5, label="p50 (медіана)")
    ax2.plot(x, p95, "-s", color="#f59e0b", linewidth=2.0, markersize=5, label="p95")
    ax2.plot(x, p99, "-^", color="#ef4444", linewidth=2.0, markersize=5, label="p99")
    ax2.set_xticks(x, labels)
    ax2.set_ylim(0, max(p99) * 1.12)
    ax2.set_xlabel("Кількість одночасних клієнтів (C)")
    ax2.set_ylabel("Затримка відповіді, мс")
    ax2.set_title("Пошук 'keyword': розподіл затримок")
    ax2.legend(loc="upper left", frameon=True)
    for xi, val in zip(x, p50):
        ax2.annotate(f"{val:.0f}", (xi, val), textcoords="offset points", xytext=(0, -13), ha="center", fontsize=8, color="#047857")

    fig.tight_layout()
    save_fig(fig, "03_search_keyword_scaling.png")


def plot_search_comparison(kw_rows: list[dict], exp_rows: list[dict]):
    exp_clients = [int(r["clients"]) for r in exp_rows]
    kw_by_c = {int(r["clients"]): r for r in kw_rows}

    labels = [str(c) for c in exp_clients]
    x = list(range(len(exp_clients)))
    kw_rps = [float(kw_by_c[c]["rps"]) for c in exp_clients]
    exp_rps = [float(r["rps"]) for r in exp_rows]
    exp_p50_s = [float(r["p50_ms"]) / 1000.0 for r in exp_rows]
    exp_p95_s = [float(r["p95_ms"]) / 1000.0 for r in exp_rows]
    exp_errs = [int(r["errors"]) for r in exp_rows]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12.5, 4.8))

    # 1. Порівняння RPS (лог-шкала через ~80x різницю)
    ax1.plot(x, kw_rps, "-o", color="#2563eb", linewidth=2.2, markersize=6, label="Режим 'keyword' (лише індекс)")
    ax1.plot(x, exp_rps, "-s", color="#e11d48", linewidth=2.2, markersize=6, label="Режим 'expanded' (Ollama LLM)")
    ax1.set_yscale("log")
    ax1.set_xticks(x, labels)
    ax1.set_ylim(1, 500)
    ax1.set_xlabel("Кількість одночасних клієнтів (C)")
    ax1.set_ylabel("Пропускна здатність, RPS (лог-шкала)")
    ax1.set_title("Вузьке місце LLM: 'keyword' vs 'expanded'")
    ax1.legend(loc="center right", frameon=True)
    for xi, (k, e) in enumerate(zip(kw_rps, exp_rps)):
        ax1.annotate(f"{k:.1f}", (xi, k), textcoords="offset points", xytext=(0, 7), ha="center", fontsize=8.5, color="#1d4ed8")
        ax1.annotate(f"{e:.2f}", (xi, e), textcoords="offset points", xytext=(0, -14), ha="center", fontsize=8.5, color="#be123c")

    # 2. Затримки та помилки у режимі expanded
    ax2_err = ax2.twinx()
    bars = ax2_err.bar(x, exp_errs, width=0.45, color="#fda4af", alpha=0.55, label="Помилки з'єднання (кількість)")
    ax2_err.set_ylabel("Кількість помилок з'єднання", color="#be123c")
    ax2_err.tick_params(axis="y", labelcolor="#be123c")
    ax2_err.set_ylim(0, max(max(exp_errs), 5) * 1.35)
    ax2_err.grid(False)

    # Виносимо лінії поверх стовпців помилок
    ax2.set_zorder(ax2_err.get_zorder() + 1)
    ax2.patch.set_visible(False)

    l1 = ax2.plot(x, exp_p50_s, "-o", color="#7c3aed", linewidth=2.2, markersize=6, label="p50 затримка, с")
    l2 = ax2.plot(x, exp_p95_s, "-s", color="#ea580c", linewidth=2.2, markersize=6, label="p95 затримка, с")
    l3 = ax2.axhline(5.0, color="#64748b", linestyle=":", linewidth=1.3, label="Поріг таймауту сокета (~5 с)")
    ax2.set_xticks(x, labels)
    ax2.set_ylim(0, max(exp_p95_s) * 1.15)
    ax2.set_xlabel("Кількість одночасних клієнтів (C)")
    ax2.set_ylabel("Затримка відповіді, с")
    ax2.set_title("Режим 'expanded': зростання черги та помилки")

    for xi, val in zip(x, exp_p50_s):
        ax2.annotate(f"{val:.1f}с", (xi, val), textcoords="offset points", xytext=(0, 8), ha="center", fontsize=8, color="#5b21b6")
    for bar, err in zip(bars, exp_errs):
        if err > 0:
            ax2_err.text(
                bar.get_x() + bar.get_width() / 2, err + 0.4,
                f"{err} err", ha="center", va="bottom", fontsize=8, color="#be123c", fontweight="bold",
            )

    handles = l1 + l2 + [l3, bars]
    ax2.legend(handles, [h.get_label() for h in handles], loc="upper left", fontsize=8.5, frameon=True)

    fig.tight_layout()
    save_fig(fig, "04_search_keyword_vs_expanded.png")


if __name__ == "__main__":
    plot_indexing_time_speedup(read_csv("indexing.csv"))
    plot_indexing_efficiency_throughput(read_csv("indexing.csv"))
    plot_search_keyword(read_csv("search_keyword.csv"))
    plot_search_comparison(read_csv("search_keyword.csv"), read_csv("search_expanded.csv"))