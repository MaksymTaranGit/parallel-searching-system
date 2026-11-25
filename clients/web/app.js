(() => {
  const statusOllamaEl = document.getElementById("status-ollama");
  const statusWsEl = document.getElementById("status-ws");

  const indexBadgeEl = document.getElementById("index-badge");
  const metricDocsEl = document.getElementById("metric-docs");
  const metricTimeEl = document.getElementById("metric-time");
  const metricThreadsEl = document.getElementById("metric-threads");
  const indexMetaEl = document.getElementById("index-meta");

  const indexForm = document.getElementById("index-form");
  const threadsInput = document.getElementById("threads-input");
  const presetButtons = Array.from(document.querySelectorAll(".preset-btn"));
  const indexBtn = document.getElementById("index-btn");
  const indexBtnLabel = document.getElementById("index-btn-label");

  const searchForm = document.getElementById("search-form");
  const queryInput = document.getElementById("query-input");
  const searchBtn = document.getElementById("search-btn");
  const errorBanner = document.getElementById("error-banner");

  const pipelineStateEl = document.getElementById("pipeline-state");
  const stageQeEl = document.getElementById("stage-query_expansion");
  const stageKsEl = document.getElementById("stage-keyword_search");
  const stageDoneEl = document.getElementById("stage-done");

  const stageQeTime = document.getElementById("stage-qe-time");
  const stageKsTime = document.getElementById("stage-ks-time");
  const stageDoneTime = document.getElementById("stage-done-time");

  const stageQeDetail = document.getElementById("stage-qe-detail");
  const stageKsDetail = document.getElementById("stage-ks-detail");
  const stageDoneDetail = document.getElementById("stage-done-detail");
  const expandedTermsEl = document.getElementById("expanded-terms");

  const resultsListEl = document.getElementById("results-list");
  const resultsSummaryEl = document.getElementById("results-summary");

  let ws = null;
  let searchInFlight = false;
  let searchStartedAt = 0;
  let activeHighlightTerms = [];

  function setPill(el, text, kind) {
    el.className = `pill pill-${kind}`;
    const label = el.querySelector(".pill-label");
    if (label) {
      label.textContent = text;
    }
  }

  function formatNumber(n) {
    return Number(n || 0).toLocaleString("en-US");
  }

  function formatDuration(ms) {
    if (ms === null || ms === undefined) {
      return "\u2014";
    }
    return ms >= 1000 ? `${(ms / 1000).toFixed(2)} s` : `${ms} ms`;
  }

  function showError(message) {
    errorBanner.textContent = message;
    errorBanner.classList.remove("hidden");
  }

  function clearError() {
    errorBanner.textContent = "";
    errorBanner.classList.add("hidden");
  }

  function setStageState(el, state) {
    el.className = `step step-${state}`;
  }

  function syncPresetButtons(val) {
    const strVal = String(val);
    presetButtons.forEach((btn) => {
      btn.classList.toggle("is-active", btn.dataset.threads === strVal);
    });
  }

  function resetPipelineForQuery() {
    pipelineStateEl.textContent = "Streaming...";
    setStageState(stageQeEl, "active");
    setStageState(stageKsEl, "idle");
    setStageState(stageDoneEl, "idle");

    stageQeTime.textContent = "...";
    stageKsTime.textContent = "\u2014";
    stageDoneTime.textContent = "\u2014";

    stageQeDetail.textContent = "Generating synonyms via Ollama...";
    stageKsDetail.textContent = "Waiting for expanded terms";
    stageDoneDetail.textContent = "In progress";
    expandedTermsEl.innerHTML = "";
    resultsSummaryEl.textContent = "";
    resultsListEl.innerHTML =
      '<div class="empty-state"><p class="empty-title">Running search pipeline...</p><p class="empty-sub">Streaming query expansion and inverted index lookup via WebSocket.</p></div>';
  }

  function escapeHtml(str) {
    return str
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function escapeRegex(str) {
    return str.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  }

  function highlightSnippet(snippet, terms) {
    const safe = escapeHtml(snippet || "");
    const tokens = Array.from(
      new Set(
        terms
          .join(" ")
          .toLowerCase()
          .split(/[^a-z0-9]+/)
          .filter((t) => t.length >= 2)
      )
    );

    if (tokens.length === 0) {
      return safe;
    }

    const pattern = new RegExp(`\\b(${tokens.map(escapeRegex).join("|")})\\b`, "gi");
    return safe.replace(pattern, "<mark>$1</mark>");
  }

  function renderExpandedTerms(terms) {
    expandedTermsEl.innerHTML = "";
    if (!Array.isArray(terms) || terms.length === 0) {
      return;
    }
    for (const term of terms) {
      const chip = document.createElement("span");
      chip.className = "term-chip";
      chip.textContent = term;
      expandedTermsEl.appendChild(chip);
    }
  }

  function renderResults(results, timeMs) {
    resultsListEl.innerHTML = "";
    resultsSummaryEl.textContent = `${results.length} hit(s) \u00b7 ${timeMs} ms`;

    if (results.length === 0) {
      resultsListEl.innerHTML =
        '<div class="empty-state"><p class="empty-title">No matching documents found</p><p class="empty-sub">Try another query or verify that the dataset has been indexed.</p></div>';
      return;
    }

    const maxScore = Math.max(...results.map((r) => Number(r.score) || 0), 0.0001);

    results.forEach((item, idx) => {
      const score = Number(item.score) || 0;
      const barPct = Math.max(8, Math.min(100, Math.round((score / maxScore) * 100)));

      const card = document.createElement("article");
      card.className = "result-item";

      const top = document.createElement("div");
      top.className = "result-top";

      const docWrap = document.createElement("div");
      docWrap.className = "result-doc-wrap";

      const rankEl = document.createElement("span");
      rankEl.className = "result-rank";
      rankEl.textContent = String(idx + 1);

      const docEl = document.createElement("span");
      docEl.className = "result-doc";
      docEl.textContent = item.doc_id;

      docWrap.appendChild(rankEl);
      docWrap.appendChild(docEl);

      const scoreBox = document.createElement("div");
      scoreBox.className = "result-score-box";

      const track = document.createElement("div");
      track.className = "score-bar-track";
      const fill = document.createElement("div");
      fill.className = "score-bar-fill";
      fill.style.width = `${barPct}%`;
      track.appendChild(fill);

      const scoreEl = document.createElement("span");
      scoreEl.className = "result-score";
      scoreEl.textContent = score.toFixed(4);

      scoreBox.appendChild(track);
      scoreBox.appendChild(scoreEl);

      top.appendChild(docWrap);
      top.appendChild(scoreBox);
      card.appendChild(top);

      if (item.snippet) {
        const snippetEl = document.createElement("p");
        snippetEl.className = "result-snippet";
        snippetEl.innerHTML = highlightSnippet(item.snippet, activeHighlightTerms);
        card.appendChild(snippetEl);
      }

      resultsListEl.appendChild(card);
    });
  }

  async function refreshStatus() {
    try {
      const [healthRes, statsRes] = await Promise.all([
        fetch("/api/health"),
        fetch("/api/stats"),
      ]);

      if (healthRes.ok) {
        const health = await healthRes.json();
        setPill(
          statusOllamaEl,
          health.ollama ? "Ollama: online" : "Ollama: offline",
          health.ollama ? "ok" : "warn"
        );
      }

      if (statsRes.ok) {
        const stats = await statsRes.json();
        metricDocsEl.textContent = formatNumber(stats.documents);

        if (stats.documents > 0) {
          metricTimeEl.textContent = formatDuration(stats.index_build_time_ms);
          metricThreadsEl.textContent = String(stats.threads_used);
          indexBadgeEl.textContent = "Ready";
          indexBadgeEl.className = "mini-badge mini-badge-ready";
          indexMetaEl.textContent = `Last build: ${formatNumber(stats.documents)} docs in ${formatDuration(stats.index_build_time_ms)}`;
        } else {
          metricTimeEl.textContent = "\u2014";
          metricThreadsEl.textContent = "\u2014";
          indexBadgeEl.textContent = "Empty";
          indexBadgeEl.className = "mini-badge mini-badge-muted";
          indexMetaEl.textContent = "Index is empty \u2014 click Build to scan dataset";
        }
      }
    } catch {
      setPill(statusOllamaEl, "Server: unreachable", "err");
    }
  }

  function connectWebSocket() {
    return new Promise((resolve, reject) => {
      if (ws && ws.readyState === WebSocket.OPEN) {
        resolve(ws);
        return;
      }

      const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
      const wsUrl = `${protocol}//${window.location.host}/ws/search`;
      setPill(statusWsEl, "WS: connecting", "muted");

      const socket = new WebSocket(wsUrl);
      ws = socket;

      socket.onopen = () => {
        setPill(statusWsEl, "WS: connected", "ok");
        resolve(socket);
      };

      socket.onerror = () => {
        setPill(statusWsEl, "WS: error", "err");
        reject(new Error("WebSocket connection failed"));
      };

      socket.onclose = () => {
        setPill(statusWsEl, "WS: disconnected", "warn");
        if (searchInFlight) {
          searchInFlight = false;
          searchBtn.disabled = false;
          pipelineStateEl.textContent = "Disconnected";
          showError("WebSocket connection closed before search completed.");
        }
      };

      socket.onmessage = (event) => {
        let msg;
        try {
          msg = JSON.parse(event.data);
        } catch {
          return;
        }

        const stage = msg.stage;
        const data = msg.data || {};

        if (stage === "query_expansion") {
          const qeMs = Math.round(performance.now() - searchStartedAt);
          const terms = Array.isArray(data.expanded_terms) ? data.expanded_terms : [];
          activeHighlightTerms = [data.original || "", ...terms];

          setStageState(stageQeEl, "done");
          stageQeTime.textContent = `${qeMs} ms`;
          stageQeDetail.textContent =
            terms.length > 0
              ? `Generated ${terms.length} synonym(s)`
              : "Direct query (no synonyms)";
          renderExpandedTerms(terms);

          setStageState(stageKsEl, "active");
          stageKsTime.textContent = "...";
          stageKsDetail.textContent = "Querying sharded index...";
        } else if (stage === "keyword_search") {
          const results = Array.isArray(data.results) ? data.results : [];
          const timeMs = data.time_ms ?? 0;

          setStageState(stageKsEl, "done");
          stageKsTime.textContent = `${timeMs} ms`;
          stageKsDetail.textContent = `Matched ${results.length} doc(s)`;
          renderResults(results, timeMs);

          setStageState(stageDoneEl, "active");
        } else if (stage === "done") {
          const totalMs = Math.round(performance.now() - searchStartedAt);
          setStageState(stageDoneEl, "done");
          stageDoneTime.textContent = `${totalMs} ms`;
          stageDoneDetail.textContent = "Total round-trip";
          pipelineStateEl.textContent = `Done (${totalMs} ms)`;

          searchInFlight = false;
          searchBtn.disabled = false;
        } else if (stage === "error") {
          setStageState(stageQeEl, "error");
          stageQeTime.textContent = "err";
          pipelineStateEl.textContent = "Error";
          showError(data.message || "Search failed on server");
          searchInFlight = false;
          searchBtn.disabled = false;
        }
      };
    });
  }

  presetButtons.forEach((btn) => {
    btn.addEventListener("click", () => {
      threadsInput.value = btn.dataset.threads;
      syncPresetButtons(btn.dataset.threads);
    });
  });

  threadsInput.addEventListener("input", () => {
    syncPresetButtons(threadsInput.value);
  });

  document.addEventListener("keydown", (event) => {
    if (
      event.key === "/" &&
      document.activeElement !== queryInput &&
      document.activeElement !== threadsInput
    ) {
      event.preventDefault();
      queryInput.focus();
    }
  });

  searchForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    const query = queryInput.value.trim();
    if (!query || searchInFlight) {
      return;
    }

    clearError();
    searchInFlight = true;
    searchBtn.disabled = true;
    searchStartedAt = performance.now();
    activeHighlightTerms = [query];
    resetPipelineForQuery();

    try {
      const socket = await connectWebSocket();
      socket.send(JSON.stringify({ query }));
    } catch (err) {
      setStageState(stageQeEl, "error");
      stageQeTime.textContent = "err";
      stageQeDetail.textContent = "Connection error";
      pipelineStateEl.textContent = "Failed";
      showError(err.message);
      searchInFlight = false;
      searchBtn.disabled = false;
    }
  });

  indexForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    const threads = Math.max(1, parseInt(threadsInput.value, 10) || 4);

    clearError();
    indexBtn.disabled = true;
    indexBtnLabel.textContent = "Building Index...";
    indexBadgeEl.textContent = "Indexing";
    indexBadgeEl.className = "mini-badge mini-badge-busy";
    indexMetaEl.textContent = `Running ThreadPool with ${threads} thread(s)...`;

    try {
      const res = await fetch("/api/index", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ threads }),
      });

      const data = await res.json();
      if (!res.ok) {
        throw new Error(data.error || `HTTP ${res.status}`);
      }

      indexMetaEl.textContent = `Indexed ${formatNumber(data.documents_indexed)} docs in ${formatDuration(data.time_ms)}`;
      await refreshStatus();
    } catch (err) {
      indexBadgeEl.textContent = "Error";
      indexBadgeEl.className = "mini-badge mini-badge-muted";
      showError(`Indexing failed: ${err.message}`);
    } finally {
      indexBtn.disabled = false;
      indexBtnLabel.textContent = "Build Inverted Index";
    }
  });

  refreshStatus();
  connectWebSocket().catch(() => {});
})();