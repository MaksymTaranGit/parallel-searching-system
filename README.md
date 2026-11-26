# Parallel Searching System (C++ / Crow / Ollama)

Високопродуктивна багатопотокова пошукова система на базі **C++17** з інвертованим індексом (64 шарди, власна хеш-таблиця з відкритою адресацією), **TF-IDF** ранжуванням та опціональним розширенням запитів через локальну LLM (**Ollama**, модель `qwen2.5:0.5b`).

Система надає три способи взаємодії:

* **Web UI** - інтерактивний веб-інтерфейс зі стрімінгом етапів пошуку через WebSocket (`/ws/search`) та панеллю керування індексацією;
* **CLI-клієнт** - консольна утиліта (`./search` / `search.cmd`) для пошуку, запуску індексації та перевірки метрик;
* **REST API & WebSocket** - відкритий HTTP/WS інтерфейс для зовнішніх клієнтів і бенчмарків.

---

## Архітектура та ключові особливості

* **Власні структури даних (`src/core/`):** шаблонна хеш-таблиця `HashTable<V>` з відкритою адресацією без залежності від `std::unordered_map` у критичному шляху.
* **Паралельна індексація (`ThreadPool`):** власний пул потоків розподіляє читання файлів, токенізацію та підрахунок частот (`TF`) між `N` робочими потоками.
* **64-шардовий індекс (`ThreadSafeInvertedIndex`):** простір термінів розбито на 64 незалежні шарди (`IndexShard` із вирівнюванням `alignas(64)` проти *false sharing*), із `std::shared_mutex` для паралельного читання без блокувань та батчевою вставкою `add_batch()` при записі.
* **Інтеграція LLM із Graceful Degradation (`OllamaClient`):** звернення до локальної моделі синхронізовані через `std::timed_mutex` (таймаут очікування 15 с). Якщо Ollama недоступна або перевантажена, пошук автоматично виконується за оригінальним запитом без збою.
* **Асинхронний WebSocket (`ServerController`):** обробка `/ws/search` винесена в окремий `ws_pool_`, щоб не блокувати потік подій Crow.

---

## Вимоги та швидкий запуск

### Необхідне ПЗ

* **Git**;
* **Docker** та **Docker Compose** (v2+);
* *(Опціонально для бенчмарків)* Python 3.10+.

### 1. Клонування репозиторію

```bash
git clone <url-репозиторію>
cd parallel-searching-system
```

### 2. Підготовка датасету

* **Шлях:** розмістіть текстові файли у папці `./data/` в корені репозиторію.
* **Формат:** звичайні текстові файли з розширенням **`.txt`** у кодуванні **UTF-8**.
* **Тестовий корпус:** система протестована на базі книг **PG-19** (**11 933 `.txt` файлів**, сумарний обсяг **~2.15 GB**). Самі файли датасету виключені з Git через `.gitignore`.

### 3. Конфігурація середовища (`.env`)

За замовчуванням усі параметри вже задані у `docker-compose.yml`, тому система запускається одразу після клонування. Якщо потрібно змінити порти, шляхи чи модель - просто скопіюйте вміст файлу `.env.example` в `.env` і відредагуйте потрібні значення:

```bash
# Linux / macOS / Git Bash
cp .env.example .env

# Windows (PowerShell)
Copy-Item .env.example .env
```

Основні змінні середовища:

| Змінна | Значення за замовчуванням | Опис |
| :--- | :--- | :--- |
| `SERVER_PORT` | `8080` | Порт HTTP/WebSocket сервера |
| `HOST_DATA_DIR` | `./data` | Шлях до папки з датасетом на хості |
| `DATASET_PATH` | `/app/data` | Точка монтування датасету всередині контейнера (Read-Only) |
| `WEB_DIR` | `/app/web` | Директорія статичних файлів Web UI |
| `OLLAMA_URL` | `http://ollama:11434` | Адреса сервісу Ollama у внутрішній мережі Docker |
| `OLLAMA_MODEL` | `qwen2.5:0.5b` | Локальна LLM-модель для розширення запитів |
| `SEARCH_SERVER_URL` | `http://localhost:8080` | Базовий URL сервера для CLI-клієнта за замовчуванням |

### 4. Запуск системи

Запустіть усі сервіси однією командою з кореня проєкту:

```bash
docker compose up
```

Під час першого запуску Docker Compose автоматично збере образ `search-server`, підніме сервіс `ollama` та завантажить модель `qwen2.5:0.5b` без додаткових ручних кроків.

Після старту будуть доступні:

* **Web UI:** [http://localhost:8080](http://localhost:8080)
* **REST API:** `http://localhost:8080/api/...`
* **WebSocket:** `ws://localhost:8080/ws/search`

---

## Використання Web UI

Веб-інтерфейс відкривається у браузері за адресою **[http://localhost:8080](http://localhost:8080)** (статичні файли з `clients/web/` роздаються безпосередньо C++ сервером).

Основний функціонал Web UI:

* **Інтерактивний пошук через WebSocket (`/ws/search`):** після введення запиту інтерфейс у реальному часі відображає прогрес етапів пайплайну (`query_expansion → keyword_search → done`), згенеровані Ollama синоніми, час пошуку та список Топ-10 документів із їхніми TF-IDF оцінками (`score`) і текстовими сніпетами.
* **Керування індексацією (`POST /api/index`):** окрема панель дозволяє задати кількість робочих потоків `N` і запустити побудову або перебудову інвертованого індексу прямо з браузера.
* **Моніторинг стану та статистики (`GET /api/stats`, `GET /api/health`):** відображення статусу доступності сервера й Ollama, загальної кількості проіндексованих документів, тривалості останньої індексації та кількості використаних потоків.

---

## Використання CLI-клієнта

У корені проєкту підготовлені кросплатформні обгортки, які проксують команди до скомпільованого C++ клієнта `/app/search` всередині контейнера:

* **Linux / macOS / Git Bash:** `./search`
* **Windows (PowerShell / CMD):** `.\search.cmd`

### Синтаксис та опції

```text
Usage:
  ./search "<query>" [--mode keyword|expanded] [--url <url>]
  ./search --index [threads] [--url <url>]
  ./search --stats [--url <url>]
  ./search --health [--url <url>]

Options:
  -m, --mode <mode>    Режим пошуку: keyword (за замовчуванням) або expanded
  -u, --url <url>      Базовий URL сервера (за замовчуванням: SEARCH_SERVER_URL або http://localhost:8080)
  --index [threads]    Запуск побудови індексу з опціональною кількістю потоків
  --stats              Показати статистику індексації
  --health             Перевірити доступність сервера та Ollama
  -h, --help           Показати довідку
```

### Приклади команд

```bash
# Перевірка стану сервера та доступності Ollama
./search --health

# Побудова інвертованого індексу (з явним числом потоків або за замовчуванням)
./search --index 8
./search --index

# Перегляд статистики індексу (документи, час побудови, потоки)
./search --stats

# Прямий пошук за ключовими словами (режим keyword)
./search "astronomy telescope"

# Розширений пошук із генерацією синонімів через Ollama
./search "ocean voyage" --mode expanded

# Запит до іншого URL сервера
./search "space stars" -m keyword -u http://localhost:8080
```

---

## Специфікація API

Усі REST-ендпоінти приймають і повертають дані у форматі `application/json`. При невалідному JSON або некоректних параметрах сервер повертає статус **`400 Bad Request`** із полем `error`.

### 1. `GET /api/health`

Перевіряє працездатність сервера та доступність сервісу Ollama (неблокуючий запит `GET /api/tags` без захоплення `llm_mutex_`).

**Відповідь (`200 OK`):**

```json
{
  "status": "ok",
  "ollama": true
}
```

| Поле | Тип | Опис |
| :--- | :--- | :--- |
| `status` | `string` | Статус сервера (`"ok"`) |
| `ollama` | `boolean` | Доступність сервісу Ollama (`true` / `false`) |

### 2. `POST /api/index`

Очищує попередній індекс та запускає паралельну індексацію всіх `.txt` файлів із `DATASET_PATH` у `threads` потоках.

**Запит:**

```json
{
  "threads": 8
}
```

| Поле | Тип | Обов'язкове | Опис |
| :--- | :--- | :---: | :--- |
| `threads` | `integer` | Так | Кількість робочих потоків (`> 0`). При `<= 0` повертається `400 Bad Request` |

**Відповідь (`200 OK`):**

```json
{
  "status": "ok",
  "documents_indexed": 11933,
  "time_ms": 10787
}
```

| Поле | Тип | Опис |
| :--- | :--- | :--- |
| `status` | `string` | Статус операції (`"ok"`) |
| `documents_indexed` | `integer` | Кількість успішно проіндексованих документів |
| `time_ms` | `integer` | Тривалість побудови індексу в мілісекундах (`steady_clock`) |

### 3. `POST /api/search`

Виконує пошук по інвертованому індексу з TF-IDF ранжуванням (Топ-10 результатів).

**Запит:**

```json
{
  "query": "ocean voyage",
  "mode": "expanded"
}
```

| Поле | Тип | Обов'язкове | Опис |
| :--- | :--- | :---: | :--- |
| `query` | `string` | Так | Пошуковий запит |
| `mode` | `string` | Ні | Режим пошуку: `"keyword"` (за замовчуванням) або `"expanded"`. Інші значення повертають `400 Bad Request` |

**Відповідь (`200 OK`, режим `"keyword"`):**

```json
{
  "results": [
    {
      "doc_id": "6317.txt",
      "score": 17.2823,
      "snippet": "...sailing alone around the world..."
    }
  ],
  "time_ms": 21
}
```

**Відповідь (`200 OK`, режим `"expanded"`):**

```json
{
  "results": [
    {
      "doc_id": "6317.txt",
      "score": 16.1763,
      "snippet": "...sailing alone around the world..."
    }
  ],
  "expanded_terms": ["sea trip", "sea journey", "sea voyage", "ocean trip", "ocean journey"],
  "time_ms": 474
}
```

| Поле | Тип | Опис |
| :--- | :--- | :--- |
| `results` | `array<DocMatch>` | Відсортований за спаданням `score` список Топ-10 збігів |
| `results[].doc_id` | `string` | Ім'я/ідентифікатор знайденого файлу |
| `results[].score` | `number` (`double`) | Підсумковий зважений TF-IDF бал релевантності |
| `results[].snippet` | `string` | Текстовий фрагмент документа навколо знайденого терміну |
| `expanded_terms` | `array<string>` | *(Лише для `mode: "expanded"`)* Синоніми від Ollama (порожній масив `[]` при fallback) |
| `time_ms` | `integer` | Загальний час обробки запиту на сервері (мс) |

### 4. `GET /api/stats`

Повертає поточну статистику індексу та останньої індексації.

**Відповідь (`200 OK`):**

```json
{
  "documents": 11933,
  "index_build_time_ms": 10787,
  "threads_used": 16
}
```

| Поле | Тип | Опис |
| :--- | :--- | :--- |
| `documents` | `integer` | Загальна кількість документів в індексі |
| `index_build_time_ms` | `integer` | Час останньої побудови індексу (мс) |
| `threads_used` | `integer` | Кількість потоків останньої індексації |

### 5. WebSocket `WS /ws/search`

Асинхронний інтерактивний пошук зі стрімінгом етапів пайплайну в реальному часі.

**Вхідне повідомлення від клієнта:**

```json
{
  "query": "space stars"
}
```

**Вихідні повідомлення від сервера (послідовно):**

1. **Етап 1 - `query_expansion`** (результат роботи Ollama):

```json
{
  "stage": "query_expansion",
  "data": {
    "original": "space stars",
    "expanded_terms": ["stars", "galaxies", "space", "orbit", "galactic"]
  }
}
```

2. **Етап 2 - `keyword_search`** (результати пошуку по індексу):

```json
{
  "stage": "keyword_search",
  "data": {
    "results": [
      {
        "doc_id": "8172.txt",
        "score": 13.346,
        "snippet": "..."
      }
    ],
    "time_ms": 17
  }
}
```

3. **Етап 3 - `done`** (сигнал завершення пайплайну):

```json
{
  "stage": "done",
  "data": {}
}
```

*(У разі невалідного JSON або порожнього запиту сервер надсилає подію з `"stage": "error"` та описом помилки у `data`.)*

---

## Тестування та бенчмарки

У директорії `benchmarks/` знаходяться автоматизовані Python-скрипти для перевірки коректності, детермінованості паралельної індексації та вимірювання масштабованості:

```bash
python -m venv benchmarks/.venv
# Windows: .\benchmarks\.venv\Scripts\Activate.ps1
# Linux/macOS: source benchmarks/.venv/bin/activate
pip install -r benchmarks/requirements.txt

# 1. Верифікація API-контрактів, детермінованості (1 vs 8 потоків), WS та таблиця запитів
python benchmarks/verify_and_queries.py

# 2. Бенчмарк масштабованості індексації (1..64 потоки) -> benchmarks/results/indexing.csv
python benchmarks/bench_indexing.py

# 3. Навантажувальний тест пошуку (keyword та expanded) -> benchmarks/results/search_*.csv
python benchmarks/bench_search.py

# 4. Побудова графіків -> benchmarks/charts/
python benchmarks/plot_results.py
```

---

## Структура проєкту

```text
parallel-searching-system/
├── src/
│   ├── api/            # ServerController (Crow REST, WebSocket, роздача Web UI)
│   ├── core/           # HashTable<V>, IndexShard, ThreadSafeInvertedIndex, Posting, ThreadPool
│   ├── engine/         # SearchEngine, TF-IDF ранжування, DTO-моделі (models.h)
│   ├── nlp/            # Tokenizer, OllamaClient
│   └── main.cpp        # Точка входу сервера та конфігурація з env
├── clients/
│   ├── cli/            # C++ CLI-клієнт (cpp-httplib)
│   └── web/            # Фронтенд Web UI (index.html, style.css, app.js)
├── benchmarks/
│   ├── charts/         # Графіки
│   ├── results/        # CSV-метрики та таблиця тестових запитів
│   ├── verify_and_queries.py
│   ├── bench_indexing.py
│   ├── bench_search.py
│   ├── plot_results.py
│   └── requirements.txt
├── diagrams/           # UML-діаграми (.drawio та .png): Class, Sequence, Deployment, Use Case
├── data/               # Директорія датасету (.txt файли; у Git лише .gitkeep)
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
├── .env.example
├── search              # CLI-обгортка (Linux / macOS)
└── search.cmd          # CLI-обгортка (Windows)
```