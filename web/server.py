#!/usr/bin/env python3
"""Web demo for citeverify.

The C++ program (build/citeverify) still does ALL the checking. This small
server only:
  1. serves the web page (web/index.html),
  2. writes the references from the page into a temporary file,
  3. starts the C++ program with --progress,
  4. streams every line the program prints to the page WHILE it runs
     (one JSON object per line, "NDJSON"), so the page can show live progress
     and the program's real console output.

Run (from the project folder, inside Ubuntu/WSL):
    python3 web/server.py
Then open http://localhost:8000 in your Windows browser.

Only the Python standard library is used, so there is nothing to install.
"""
import argparse
import csv
import json
import random
import re
import shutil
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
    "sample":    ("Real papers (hand-picked sample)", ROOT / "data" / "papers_sample.tsv"),
    "synthetic": ("20,000 generated papers",              ROOT / "data" / "synth" / "papers.tsv"),
    "arxiv":     ("arXiv snapshot (converted)",           ROOT / "data" / "arxiv.tsv"),
}
SYNTH_DIR = ROOT / "data" / "synth"
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


def build_binary():
    """(Re)build the C++ program. CMake only recompiles files that changed, so this is fast."""
    if not (ROOT / "build" / "CMakeCache.txt").exists():
        print("[web] configuring the C++ build with CMake ...")
        subprocess.run(["cmake", "-B", "build", "-S", ".", "-DCMAKE_BUILD_TYPE=Release"], cwd=ROOT, check=True)
    print("[web] building the C++ program ...")
    subprocess.run(["cmake", "--build", "build", "-j"], cwd=ROOT, check=True)


def generate_synthetic(out_dir, seed):
    """Run the data generator; returns the command line shown on the page."""
    cmd = [sys.executable, "scripts/gen_synthetic.py", "--papers", "20000", "--refs", "2000",
           "--out", str(out_dir), "--seed", str(seed)]
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True)
    return "python3 " + " ".join(cmd[1:]), proc.stdout.strip()


def rel(path):
    """Show paths relative to the project folder when possible (nicer on screen)."""
    try:
        return str(Path(path).relative_to(ROOT))
    except ValueError:
        return str(path)


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
    # ---- helpers ---------------------------------------------------------
    def _send(self, code, body, content_type="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _emit(self, obj):
        """Send one event to the page immediately (one JSON object per line)."""
        self.wfile.write((json.dumps(obj) + "\n").encode("utf-8"))
        self.wfile.flush()

    # ---- routes ----------------------------------------------------------
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
        if self.path != "/api/run":
            return self._send(404, {"ok": False, "error": "not found"})
        tmp = Path(tempfile.mkdtemp(prefix="citeverify_"))
        try:
            length = int(self.headers.get("Content-Length", 0))
            if length > MAX_INPUT_BYTES:
                raise ValueError("input too large (max 1 MB)")
            body = json.loads(self.rfile.read(length) or b"{}")
            pre_log = []   # lines to show before the C++ program starts (data generation)

            if body.get("mode") == "batch":
                batch = body.get("batch")
                if batch not in ("calm", "busy"):
                    raise ValueError("batch must be 'calm' or 'busy'")
                if body.get("newData"):
                    seed = random.randint(1, 999_999)
                    data_dir = tmp / "synth"
                else:
                    seed, data_dir = 42, SYNTH_DIR
                if body.get("newData") or not (data_dir / "papers.tsv").exists():
                    cmd, out = generate_synthetic(data_dir, seed)
                    pre_log += [f"$ {cmd}", out]
                db_path, refs_path = data_dir / "papers.tsv", data_dir / f"refs_{batch}.txt"
                pre_log.append(f"[web] data seed = {seed}" + ("" if body.get("newData") else " (fixed, same data every run)"))
            else:
                db_id = body.get("db", "sample")
                if db_id not in DATABASES:
                    raise ValueError("unknown database")
                if db_id == "synthetic" and not DATABASES["synthetic"][1].exists():
                    cmd, out = generate_synthetic(SYNTH_DIR, 42)
                    pre_log += [f"$ {cmd}", out]
                db_path = DATABASES[db_id][1]
                if not db_path.exists():
                    raise ValueError(f"database file missing: {rel(db_path)}")
                text = str(body.get("refs", ""))
                if not text.strip():
                    raise ValueError("paste at least one reference")
                refs_path = tmp / "your_references.txt"
                refs_path.write_text(text, encoding="utf-8")
        except Exception as e:
            shutil.rmtree(tmp, ignore_errors=True)
            return self._send(400, {"ok": False, "error": str(e)})

        # From here on we stream: headers first, then one event per line.
        self.send_response(200)
        self.send_header("Content-Type", "application/x-ndjson")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        try:
            self._stream_run(db_path, refs_path, tmp / "report.tsv", pre_log)
        except (BrokenPipeError, ConnectionResetError):
            pass  # the page was closed or reloaded during the run
        except Exception as e:
            self._emit({"type": "error", "error": str(e)})
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def _stream_run(self, db_path, refs_path, report_path, pre_log):
        cmd = [str(BIN), "--db", str(db_path), "--refs", str(refs_path), "--out", str(report_path), "--progress"]
        shown = ["./build/citeverify", "--db", rel(db_path), "--refs", rel(refs_path), "--out", "report.tsv", "--progress"]
        for line in pre_log:
            for part in line.splitlines():
                self._emit({"type": "log", "line": part})
        self._emit({"type": "start", "command": "$ " + " ".join(shown)})

        stages, results, papers = [], [], None
        # stderr is merged into stdout so timer lines arrive in the right order.
        proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, bufsize=1)
        for raw in proc.stdout:   # yields each line as soon as the C++ program prints it
            line = raw.rstrip("\n")
            if line.startswith("@total\t"):
                self._emit({"type": "total", "total": int(line.split("\t")[1])})
            elif line.startswith("@result\t"):
                f = line.split("\t")
                r = {"index": int(f[1]), "verdict": f[2], "method": f[3], "score": float(f[4]),
                     "micros": float(f[5]), "comparisons": int(f[6]), "label": f[7],
                     "matched_title": f[8], "note": f[9] if len(f) > 9 else "",
                     "title": f[10] if len(f) > 10 else ""}
                results.append(r)
                self._emit({"type": "result", **r})
            else:
                if m := TIMER_RE.search(line):   # := "walrus": assign inside the condition (Python 3.8+)
                    stages.append({"name": m.group(1).strip(), "tag": m.group(3) or "", "ms": float(m.group(4))})
                if m := PAPERS_RE.search(line):
                    papers = int(m.group(1))
                self._emit({"type": "log", "line": line})
        if proc.wait() != 0:
            raise RuntimeError("citeverify exited with an error (see console)")

        # The report file has the original reference text; attach it to each result.
        with report_path.open(encoding="utf-8") as fh:
            refs_text = {int(row["index"]): row["reference"]
                         for row in csv.DictReader(fh, delimiter="\t", quoting=csv.QUOTE_NONE)}
        for r in results:
            r["reference"] = refs_text.get(r["index"], "")
        self._emit({"type": "done", "papers": papers, "stages": stages,
                    "summary": summarize(results, stages), "results": results})

    def log_message(self, fmt, *args):  # quieter console: one short line per request
        print(f"[web] {self.command} {self.path}")


def main():
    ap = argparse.ArgumentParser(description="citeverify web demo")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--host", default="0.0.0.0",
                    help="0.0.0.0 lets the Windows browser reach the server inside WSL")
    a = ap.parse_args()
    build_binary()
    server = ThreadingHTTPServer((a.host, a.port), Handler)
    print(f"[web] citeverify demo running: open http://localhost:{a.port} in your browser")
    print("[web] press Ctrl+C to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[web] stopped")


if __name__ == "__main__":
    main()
