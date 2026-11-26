import asyncio
import json
from pathlib import Path
import requests
import websockets

URL = "http://localhost:8080"
WS_URL = "ws://localhost:8080/ws/search"
OUT = Path("benchmarks/results/test_queries_table.md")

# (query, mode, expect_hits, expected_description)
QUERIES = [
    ("computer", "keyword", True, "Топ-10 документів зі словом 'computer'; TF-IDF за спаданням"),
    ("astronomy telescope", "keyword", True, "Документи про астрономію; TF-IDF за спаданням"),
    ("ocean voyage stars", "keyword", True, "Релевантні морські тексти; валідні сніпети"),
    ("railroad steam", "keyword", True, "Документи про залізницю / парові двигуни"),
    ("ocean voyage", "expanded", True, "Синоніми від Ollama + Топ-10 документів"),
    ("frustration", "expanded", True, "Англійські синоніми без сміття + Топ-10 документів"),
    ("space stars", "ws", True, "3 етапи WS: query_expansion -> keyword_search -> done"),
    ("the and of in", "keyword", False, "Порожній список [] (усі слова — стоп-слова)"),
    ("qwertyzxcv98765", "keyword", False, "Порожній список [] (неіснуючий термін)"),
]


async def ws_search(query: str):
    stages, terms, hits, time_ms = [], [], [], 0
    async with websockets.connect(WS_URL, open_timeout=10, close_timeout=10) as ws:
        await ws.send(json.dumps({"query": query}))
        while len(stages) < 5:
            msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=60))
            stage = msg.get("stage", "")
            assert stage != "error", f"WS error для '{query}': {msg}"
            stages.append(stage)
            if stage == "query_expansion":
                assert msg["data"].get("original") == query, "Переплутано original у WS!"
                terms = msg["data"]["expanded_terms"]
            elif stage == "keyword_search":
                hits, time_ms = msg["data"]["results"], msg["data"]["time_ms"]
            elif stage == "done":
                break
        else:
            raise AssertionError(f"WS не завершився етапом 'done': {stages}")

    assert stages == ["query_expansion", "keyword_search", "done"], f"Невірні етапи WS: {stages}"
    return stages, terms, hits, time_ms


def check_contracts(s: requests.Session):
    print("1. Контракти API та валідація 400 Bad Request...")
    h = s.get(f"{URL}/api/health", timeout=10)
    assert h.status_code == 200 and h.json().get("status") == "ok"
    assert isinstance(h.json().get("ollama"), bool)

    st = s.get(f"{URL}/api/stats", timeout=10)
    assert st.status_code == 200
    assert all(isinstance(st.json().get(k), int) for k in ("documents", "index_build_time_ms", "threads_used"))

    # Негативні кейси -> 400 Bad Request
    assert s.post(f"{URL}/api/search", data="{bad", headers={"Content-Type": "application/json"}, timeout=10).status_code == 400
    assert s.post(f"{URL}/api/search", json={"mode": "keyword"}, timeout=10).status_code == 400
    assert s.post(f"{URL}/api/search", json={"query": "x", "mode": "semantic"}, timeout=10).status_code == 400
    assert s.post(f"{URL}/api/index", json={"threads": 0}, timeout=10).status_code == 400

    # Строгий контракт mode=keyword (без поля expanded_terms)
    kw = s.post(f"{URL}/api/search", json={"query": "stars", "mode": "keyword"}, timeout=15)
    assert kw.status_code == 200 and "expanded_terms" not in kw.json()
    assert isinstance(kw.json().get("results"), list) and isinstance(kw.json().get("time_ms"), int)
    print(f"   OK (ollama={h.json()['ollama']})")


def check_determinism(s: requests.Session):
    print("2. Детермінованість паралельної індексації (1 vs 8 потоків)...")
    sample = ["computer", "ocean voyage", "space stars", "railroad steam"]

    r1 = s.post(f"{URL}/api/index", json={"threads": 1}, timeout=600)
    assert r1.status_code == 200
    d1 = r1.json()
    s1 = [s.post(f"{URL}/api/search", json={"query": q}, timeout=30).json()["results"] for q in sample]

    r8 = s.post(f"{URL}/api/index", json={"threads": 8}, timeout=600)
    assert r8.status_code == 200
    d8 = r8.json()
    s8 = [s.post(f"{URL}/api/search", json={"query": q}, timeout=30).json()["results"] for q in sample]

    assert d1["documents_indexed"] == d8["documents_indexed"] > 0, "Різна кількість документів!"
    for q, a, b in zip(sample, s1, s8):
        assert [x["score"] for x in a] == [x["score"] for x in b], f"Різні score для '{q}'!"
    print(f"   OK: {d8['documents_indexed']} docs (1t: {d1['time_ms']} ms, 8t: {d8['time_ms']} ms)")


async def check_concurrent_ws():
    ws_queries = ["ocean voyage", "space stars", "ancient history", "mountain expedition"]
    print(f"3. Конкурентний WebSocket ({len(ws_queries)} одночасні клієнти)...")
    results = await asyncio.gather(*(ws_search(q) for q in ws_queries))
    for q, (_, terms, hits, _) in zip(ws_queries, results):
        assert isinstance(hits, list) and isinstance(terms, list), f"Збій структури WS для '{q}'"
    print("   OK: усі клієнти отримали свої 3 етапи без перемішування")


def assert_valid_results(res: list, expect_hits: bool, label: str):
    scores = [x["score"] for x in res]
    assert scores == sorted(scores, reverse=True), f"Score не відсортовано для {label}!"
    if expect_hits:
        assert len(res) > 0, f"Очікувались результати для {label}, але повернуто []!"
        for item in res:
            assert item.get("doc_id") and isinstance(item.get("snippet"), str), f"Неповний DocMatch у {label}"
    else:
        assert len(res) == 0, f"Очікувався [] для {label}, але повернуто {len(res)} шт.!"


def run_queries(s: requests.Session):
    print("4. Тестові запити та генерація таблиці для звіту...")
    rows = [
        "| Запит | Режим | Очікуваний результат | Фактичний результат | Час (мс) | Статус |",
        "| :--- | :---: | :--- | :--- | :---: | :---: |",
    ]

    for query, mode, expect_hits, expected in QUERIES:
        if mode == "ws":
            stages, terms, res, t = asyncio.run(ws_search(query))
            assert_valid_results(res, expect_hits, f"ws:{query}")
            assert not any("terms" in w.split() for w in terms), f"Витік ключа 'terms': {terms}"
            top = f"{res[0]['doc_id']} ({res[0]['score']:.4f})"
            actual = f"{' -> '.join(stages)}; синоніми: {terms}; {len(res)} шт., Топ-1: {top}"
        else:
            r = s.post(f"{URL}/api/search", json={"query": query, "mode": mode}, timeout=60)
            assert r.status_code == 200, f"HTTP {r.status_code} для '{query}'"
            data = r.json()
            res, t = data["results"], data["time_ms"]
            assert_valid_results(res, expect_hits, f"{mode}:{query}")

            extra = ""
            if mode == "expanded":
                terms = data.get("expanded_terms", [])
                assert isinstance(terms, list) and not any("terms" in w.split() for w in terms), f"Погані синоніми: {terms}"
                extra = f"синоніми: {terms}; "

            top = f"{res[0]['doc_id']} ({res[0]['score']:.4f})" if res else "—"
            actual = f"{extra}знайдено {len(res)}, Топ-1: {top}" if res else "Повернуто []"

        print(f"   [PASS] [{mode}] '{query}' ({t} ms) -> {actual}")
        rows.append(f"| `{query}` | `{mode}` | {expected} | {actual} | {t} | **PASS** |")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"\nУсі перевірки пройдено. Таблицю збережено у {OUT}")


if __name__ == "__main__":
    with requests.Session() as session:
        check_contracts(session)
        check_determinism(session)
        asyncio.run(check_concurrent_ws())
        run_queries(session)