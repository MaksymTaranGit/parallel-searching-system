import csv
import statistics
import time
from pathlib import Path
import requests

URL = "http://localhost:8080"
THREADS = [1, 2, 4, 8, 12, 16, 32, 64]
RUNS = 3
OUT = Path("benchmarks/results/indexing.csv")


def run_index(session: requests.Session, threads: int) -> tuple[int, int]:
    r = session.post(f"{URL}/api/index", json={"threads": threads}, timeout=600)
    assert r.status_code == 200, f"HTTP {r.status_code} для threads={threads}: {r.text}"
    data = r.json()
    return data["documents_indexed"], data["time_ms"]


def main():
    rows = []
    with requests.Session() as s:
        print("Прогрів OS Page Cache (threads=8, у статистику не йде)...", flush=True)
        expected_docs, warm_ms = run_index(s, 8)
        assert expected_docs > 0, "Датасет порожній!"
        print(f"  OK: {expected_docs} docs за {warm_ms} ms\n", flush=True)
        time.sleep(1)

        for n in THREADS:
            times = []
            print(f"Тестування N={n} потоків ({RUNS} прогони):", end=" ", flush=True)
            for _ in range(RUNS):
                docs, t_ms = run_index(s, n)
                assert docs == expected_docs, f"Очікувалось {expected_docs} docs, отримано {docs}"
                times.append(t_ms)
                print(f"{t_ms}ms", end=" ", flush=True)
                time.sleep(1)

            mean_ms = statistics.mean(times)
            median_ms = statistics.median(times)
            stdev_ms = statistics.stdev(times) if len(times) > 1 else 0.0
            print(f"-> median={median_ms:.0f} ms (stdev={stdev_ms:.0f})", flush=True)

            rows.append({
                "threads": n,
                "documents": expected_docs,
                "runs": times,
                "mean_ms": mean_ms,
                "median_ms": median_ms,
                "stdev_ms": stdev_ms,
            })

    base_ms = rows[0]["median_ms"]
    OUT.parent.mkdir(parents=True, exist_ok=True)

    fieldnames = [
        "threads", "documents", "run1_ms", "run2_ms", "run3_ms",
        "mean_ms", "median_ms", "stdev_ms", "speedup", "efficiency_pct", "docs_per_sec",
    ]
    with OUT.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            speedup = base_ms / row["median_ms"]
            eff = (speedup / row["threads"]) * 100.0
            docs_per_sec = row["documents"] / (row["median_ms"] / 1000.0)
            row["speedup"], row["efficiency_pct"], row["docs_per_sec"] = speedup, eff, docs_per_sec
            writer.writerow({
                "threads": row["threads"],
                "documents": row["documents"],
                "run1_ms": row["runs"][0],
                "run2_ms": row["runs"][1],
                "run3_ms": row["runs"][2],
                "mean_ms": round(row["mean_ms"], 1),
                "median_ms": round(row["median_ms"], 1),
                "stdev_ms": round(row["stdev_ms"], 1),
                "speedup": round(speedup, 3),
                "efficiency_pct": round(eff, 2),
                "docs_per_sec": round(docs_per_sec, 1),
            })

    print("\n--- Підсумок індексації ---")
    print(f"{'Threads':>7} | {'Median (s)':>10} | {'Stdev (s)':>9} | {'Speedup':>7} | {'Eff (%)':>7} | {'Docs/s':>8}")
    print("-" * 62)
    for row in rows:
        print(
            f"{row['threads']:>7} | {row['median_ms']/1000:>10.2f} | "
            f"{row['stdev_ms']/1000:>9.2f} | {row['speedup']:>6.2f}x | "
            f"{row['efficiency_pct']:>6.1f}% | {row['docs_per_sec']:>8.1f}"
        )
    print(f"\nЗбережено у {OUT}")


if __name__ == "__main__":
    main()