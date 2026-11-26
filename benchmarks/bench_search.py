import csv
import statistics
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import requests

URL = "http://localhost:8080"
KEYWORD_CLIENTS = [1, 2, 4, 8, 16, 32, 64]
EXPANDED_CLIENTS = [1, 2, 4, 8, 16, 32]
KEYWORD_DURATION_S = 15.0
EXPANDED_DURATION_S = 20.0
OUT_DIR = Path("benchmarks/results")

QUERY_POOL = [
    "computer", "astronomy telescope", "ocean voyage", "railroad steam",
    "space stars", "ancient history", "mountain expedition", "king empire",
    "philosophy truth", "ship captain storm", "music symphony", "forest river",
    "battle victory", "garden flowers", "winter snow", "gold treasure",
    "castle knight", "island adventure", "science discovery", "library books",
    "london street", "electricity telegraph", "medicine doctor", "painting art",
]


def percentile(sorted_vals: list[float], p: float) -> float:
    if not sorted_vals:
        return 0.0
    k = (len(sorted_vals) - 1) * (p / 100.0)
    lo = int(k)
    hi = min(lo + 1, len(sorted_vals) - 1)
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def ensure_ready():
    stats = requests.get(f"{URL}/api/stats", timeout=10).json()
    docs = stats.get("documents", 0)
    if docs == 0:
        print("Індекс порожній — будуємо індекс на 8 потоках...", flush=True)
        r = requests.post(f"{URL}/api/index", json={"threads": 8}, timeout=600)
        assert r.status_code == 200, f"Помилка індексації: HTTP {r.status_code}"
        docs = r.json()["documents_indexed"]

    print(f"Індекс готовий: {docs} docs. Прогрів keyword + Ollama...", flush=True)
    for mode in ("keyword", "keyword", "expanded"):
        r = requests.post(f"{URL}/api/search", json={"query": "ocean voyage", "mode": mode}, timeout=60)
        assert r.status_code == 200, f"Прогрів ({mode}) впав: HTTP {r.status_code}"
    print("Прогрів завершено.\n", flush=True)


def run_level(mode: str, clients: int, duration_s: float) -> dict:
    barrier = threading.Barrier(clients)
    deadline_holder = [0.0]
    req_timeout = 60 if mode == "expanded" else 30

    def worker(wid: int):
        latencies, server_times = [], []
        errors, fallbacks, step = 0, 0, 0
        with requests.Session() as s:
            if barrier.wait() == 0:
                deadline_holder[0] = time.perf_counter() + duration_s
            barrier.wait()

            deadline = deadline_holder[0]
            while time.perf_counter() < deadline:
                query = QUERY_POOL[(wid * 5 + step) % len(QUERY_POOL)]
                step += 1
                t0 = time.perf_counter()
                try:
                    r = s.post(
                        f"{URL}/api/search",
                        json={"query": query, "mode": mode},
                        timeout=req_timeout,
                    )
                    dt_ms = (time.perf_counter() - t0) * 1000.0
                    if r.status_code == 200:
                        data = r.json()
                        latencies.append(dt_ms)
                        server_times.append(data.get("time_ms", 0))
                        if mode == "expanded" and not data.get("expanded_terms"):
                            fallbacks += 1
                    else:
                        errors += 1
                except Exception:
                    errors += 1
        return latencies, server_times, errors, fallbacks

    start = time.perf_counter()
    with ThreadPoolExecutor(max_workers=clients) as pool:
        results = list(pool.map(worker, range(clients)))
    elapsed = time.perf_counter() - start

    latencies = sorted(x for r in results for x in r[0])
    server_times = [x for r in results for x in r[1]]
    errors = sum(r[2] for r in results)
    fallbacks = sum(r[3] for r in results)
    ok_count = len(latencies)

    return {
        "clients": clients,
        "requests": ok_count,
        "errors": errors,
        "duration_s": round(elapsed, 2),
        "rps": round(ok_count / elapsed, 2) if elapsed > 0 else 0.0,
        "p50_ms": round(percentile(latencies, 50), 1),
        "p95_ms": round(percentile(latencies, 95), 1),
        "p99_ms": round(percentile(latencies, 99), 1),
        "avg_server_ms": round(statistics.mean(server_times), 1) if server_times else 0.0,
        "fallbacks": fallbacks,
        "fallback_pct": round((fallbacks / ok_count) * 100.0, 1) if ok_count else 0.0,
    }


def benchmark_mode(mode: str, levels: list[int], duration_s: float, out_path: Path):
    print(f"=== Режим '{mode}' ({duration_s:.0f}s на рівень) ===", flush=True)
    header = f"{'Clients':>7} | {'Reqs':>6} | {'Err':>4} | {'RPS':>7} | {'p50 ms':>8} | {'p95 ms':>8} | {'p99 ms':>8}"
    if mode == "expanded":
        header += f" | {'Fallback':>8}"
    print(header)
    print("-" * len(header))

    rows = []
    for c in levels:
        row = run_level(mode, c, duration_s)
        rows.append(row)
        line = (
            f"{row['clients']:>7} | {row['requests']:>6} | {row['errors']:>4} | "
            f"{row['rps']:>7.2f} | {row['p50_ms']:>8.1f} | {row['p95_ms']:>8.1f} | {row['p99_ms']:>8.1f}"
        )
        if mode == "expanded":
            line += f" | {row['fallback_pct']:>7.1f}%"
        print(line, flush=True)
        time.sleep(2)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"Збережено у {out_path}\n", flush=True)


if __name__ == "__main__":
    ensure_ready()
    benchmark_mode("keyword", KEYWORD_CLIENTS, KEYWORD_DURATION_S, OUT_DIR / "search_keyword.csv")
    benchmark_mode("expanded", EXPANDED_CLIENTS, EXPANDED_DURATION_S, OUT_DIR / "search_expanded.csv")