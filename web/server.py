#!/usr/bin/env python3
"""Web demo for citeverify.

The C++ program (build/citeverify) still does ALL the checking. This small
server only:
  1. serves the web page (web/index.html),
  2. writes the references from the page into a temporary file,
  3. runs the C++ program on it,
  4. reads the report + stage timers back and sends them to the page as JSON.

Run (from the project folder, inside Ubuntu/WSL):
    python3 web/server.py
Then open http://localhost:8000 in your Windows browser.

Only the Python standard library is used, so there is nothing to install.
"""
import argparse
import csv
import json
import re
import subprocess
import sys
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent       # project folder
BIN = ROOT / "build" / "citeverify"                  # the C++ program
WEB = ROOT / "web"

# Databases the page may choose from (a whitelist: the page can never pass a path).
DATABASES = {
    "sample":    ("15 real papers (hand-picked sample)", ROOT / "data" / "papers_sample.tsv"),
    "synthetic": ("20,000 generated papers",              ROOT / "data" / "synth" / "papers.tsv"),
    "arxiv":     ("arXiv snapshot (converted)",           ROOT / "data" / "arxiv.tsv"),
}
BATCHES = {"calm": ROOT / "data" / "synth" / "refs_calm.txt",
           "busy": ROOT / "data" / "synth" / "refs_busy.txt"}

MAX_INPUT_BYTES = 1_000_000
TIMER_RE = re.compile(r"\[timer\] (.+?)\s*(\[(.+?)\])?: ([\d.]+) ms")
PAPERS_RE = re.compile(r"\[info\] papers loaded: (\d+)")
EXPECTED = {"real": "VERIFIED", "mutated": "FOUND_DIFFERS", "fake": "NOT_FOUND"}

_paper_count_cache = {}


def count_papers(path):
    """Number of data lines in a TSV database (cached by file modification time)."""
    key = (path, path.stat().st_mtime)
    if key not in _paper_count_cache:
        with path.open(encoding="utf-8", errors="replace") as f:
            _paper_count_cache[key] = sum(1 for line in f if line.strip() and not line.startswith("#"))
    return _paper_count_cache[key]


def ensure_binary():
    """Build the C++ program if it does not exist yet."""
    if BIN.exists():
        return
    print("[web] build/citeverify not found, building it once with CMake ...")
    subprocess.run(["cmake", "-B", "build", "-S", ".", "-DCMAKE_BUILD_TYPE=Release"], cwd=ROOT, check=True)
    subprocess.run(["cmake", "--build", "build", "-j"], cwd=ROOT, check=True)


def ensure_synthetic():
    """Generate the 20k-paper test data the first time it is needed."""
    if DATABASES["synthetic"][1].exists() and all(p.exists() for p in BATCHES.values()):
        return
    print("[web] generating synthetic data (20,000 papers, 2 x 2,000 references) ...")
    subprocess.run([sys.executable, "scripts/gen_synthetic.py", "--papers", "20000",
                    "--refs", "2000", "--out", "data/synth"], cwd=ROOT, check=True)


def run_citeverify(db_path, refs_path):
    """Run the C++ program and turn its output into a dictionary for the page."""
    ensure_binary()
    with tempfile.TemporaryDirectory() as tmp:
        report = Path(tmp) / "report.tsv"
        proc = subprocess.run([str(BIN), "--db", str(db_path), "--refs", str(refs_path),
                               "--out", str(report)],
                              capture_output=True, text=True, timeout=300)
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr.strip() or "citeverify failed")
        with report.open(encoding="utf-8") as f:
            # The C++ report is tab-separated with a header line.
            rows = list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))

    stages = []
    for m in TIMER_RE.finditer(proc.stderr):
        stages.append({"name": m.group(1).strip(), "tag": m.group(3) or "", "ms": float(m.group(4))})
    papers = PAPERS_RE.search(proc.stderr)

    results = [{
        "index": int(r["index"]), "verdict": r["verdict"], "method": r["method"],
        "score": float(r["score"]), "matched_id": r["matched_id"], "matched_title": r["matched_title"],
        "note": r["note"], "comparisons": int(r["comparisons"]), "micros": float(r["micros"]),
        "label": r["label"], "reference": r["reference"],
    } for r in rows]

    return {"ok": True, "papers": int(papers.group(1)) if papers else None,
            "stages": stages, "summary": summarize(results, stages), "results": results}


def summarize(results, stages):
    """Counts, accuracy, cost per ladder step, load imbalance and Amdahl estimate."""
    verdicts = {"VERIFIED": 0, "FOUND_DIFFERS": 0, "NOT_FOUND": 0}
    methods = {}
    labelled = correct = 0
    for r in results:
        verdicts[r["verdict"]] = verdicts.get(r["verdict"], 0) + 1
        m = methods.setdefault(r["method"], {"count": 0, "total_us": 0.0, "comparisons": 0})
        m["count"] += 1
        m["total_us"] += r["micros"]
        m["comparisons"] += r["comparisons"]
        if r["label"] in EXPECTED:
            labelled += 1
            correct += EXPECTED[r["label"]] == r["verdict"]
    for m in methods.values():
        m["avg_us"] = m["total_us"] / m["count"]
        m["avg_comparisons"] = m["comparisons"] / m["count"]

    # Load imbalance: what static equal chunks WOULD cost on p threads,
    # computed from the measured time of every reference (same as the CLI).
    times = [r["micros"] for r in results]
    total = sum(times)
    imbalance = []
    if total > 0:
        n = len(times)
        for p in (2, 4, 8):
            chunks = [sum(times[n * k // p:n * (k + 1) // p]) for k in range(p)]
            imbalance.append({"threads": p, "ideal_ms": total / p / 1000,
                              "static_ms": max(chunks) / 1000, "ratio": max(chunks) / (total / p)})
    top = sorted(times, reverse=True)[:max(1, len(times) // 10)]

    # Amdahl: index build + verification are the parts that will run in parallel.
    ms = {s["name"]: s["ms"] for s in stages}
    par = ms.get("build trigrams", 0) + ms.get("verify all", 0)
    seq = ms.get("load database", 0) + ms.get("parse references", 0) + ms.get("write report", 0)
    f = seq / (seq + par) if seq + par > 0 else 1.0
    amdahl = [{"threads": p, "max_speedup": 1 / (f + (1 - f) / p)} for p in (2, 4, 8)]

    return {"count": len(results), "verdicts": verdicts, "methods": methods,
            "accuracy": {"labelled": labelled, "correct": correct},
            "top10_share": (sum(top) / total) if total else 0,
            "imbalance": imbalance, "serial_fraction": f, "amdahl": amdahl}


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, content_type="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _read_json(self):
        length = int(self.headers.get("Content-Length", 0))
        if length > MAX_INPUT_BYTES:
            raise ValueError("input too large (max 1 MB)")
        return json.loads(self.rfile.read(length) or b"{}")

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            return self._send(200, (WEB / "index.html").read_bytes(), "text/html; charset=utf-8")
        if self.path == "/api/info":
            dbs = [{"id": k, "label": label, "papers": count_papers(p) if p.exists() else None,
                    "available": p.exists() or k == "synthetic"}
                   for k, (label, p) in DATABASES.items()]
            return self._send(200, {"databases": dbs})
        if self.path == "/api/sample":
            text = (ROOT / "data" / "refs_sample.txt").read_text(encoding="utf-8")
            return self._send(200, {"text": text})
        self._send(404, {"ok": False, "error": "not found"})

    def do_POST(self):
        try:
            body = self._read_json()
            if self.path == "/api/verify":
                db_id = body.get("db", "sample")
                if db_id not in DATABASES:
                    raise ValueError("unknown database")
                if db_id == "synthetic":
                    ensure_synthetic()
                db_path = DATABASES[db_id][1]
                if not db_path.exists():
                    raise ValueError(f"database file missing: {db_path.relative_to(ROOT)}")
                text = str(body.get("refs", ""))
                if not text.strip():
                    raise ValueError("paste at least one reference")
                with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="utf-8") as f:
                    f.write(text)
                    refs_path = Path(f.name)
                try:
                    return self._send(200, run_citeverify(db_path, refs_path))
                finally:
                    refs_path.unlink(missing_ok=True)
            if self.path == "/api/batch":
                batch = body.get("batch")
                if batch not in BATCHES:
                    raise ValueError("batch must be 'calm' or 'busy'")
                ensure_synthetic()
                return self._send(200, run_citeverify(DATABASES["synthetic"][1], BATCHES[batch]))
            self._send(404, {"ok": False, "error": "not found"})
        except Exception as e:  # report every problem to the page instead of crashing
            self._send(400, {"ok": False, "error": str(e)})

    def log_message(self, fmt, *args):  # quieter console: one short line per request
        print(f"[web] {self.command} {self.path} -> {args[1] if len(args) > 1 else ''}")


def main():
    ap = argparse.ArgumentParser(description="citeverify web demo")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--host", default="0.0.0.0",
                    help="0.0.0.0 lets the Windows browser reach the server inside WSL")
    a = ap.parse_args()
    ensure_binary()
    server = ThreadingHTTPServer((a.host, a.port), Handler)
    print(f"[web] citeverify demo running: open http://localhost:{a.port} in your browser")
    print("[web] press Ctrl+C to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[web] stopped")


if __name__ == "__main__":
    main()
