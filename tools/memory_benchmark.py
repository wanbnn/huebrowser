#!/usr/bin/env python3
"""Repeatable UI smoke and process-memory benchmark for Hue Browser."""

from __future__ import annotations

import argparse
import http.server
import json
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import threading
import time
from collections import defaultdict
from urllib.parse import urlsplit


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP = ROOT / "build" / "hue-browser"
REAL_SITES = [
    {"name": "Hacker News", "host": "news.ycombinator.com", "url": "https://news.ycombinator.com/"},
    {"name": "Python.org", "host": "www.python.org", "url": "https://www.python.org/"},
    {"name": "MDN Web APIs", "host": "developer.mozilla.org", "url": "https://developer.mozilla.org/en-US/docs/Web/API"},
    {"name": "GitHub WebKit", "host": "github.com", "url": "https://github.com/WebKit/WebKit"},
]


class Pages(http.server.BaseHTTPRequestHandler):
    hits: dict[str, int] = defaultdict(int)
    hit_lock = threading.Lock()

    def do_GET(self):  # noqa: N802 - BaseHTTPRequestHandler API
        path = urlsplit(self.path).path
        with self.hit_lock:
            self.hits[path] += 1
        if path.startswith("/image/"):
            body = (b'<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200">'
                    b'<rect width="100%" height="100%" fill="#678"/>'
                    b'<text x="20" y="100" fill="white">Hue Browser memory run</text></svg>')
            kind = "image/svg+xml"
        elif path == "/light":
            body = self.page("Light page", "<h1>Light page ready</h1><p>Smoke check OK.</p>")
            kind = "text/html; charset=utf-8"
        elif path == "/dom":
            body = self.page("DOM page", """
                <h1>DOM page ready</h1><main id="items"></main>
                <script>
                  const root = document.querySelector('#items');
                  const fragment = document.createDocumentFragment();
                  for (let i = 0; i < 5000; i++) {
                    const row = document.createElement('p');
                    row.textContent = `DOM row ${i}: representative browser content`;
                    fragment.appendChild(row);
                  }
                  root.appendChild(fragment);
                  document.title = 'DOM ready: ' + root.children.length;
                  fetch('/js-complete', {cache: 'no-store'});
                </script>
            """)
            kind = "text/html; charset=utf-8"
        elif path == "/js-complete":
            self.send_response(204)
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            return
        elif path == "/images":
            images = "".join(f'<img width="160" height="100" src="/image/{i}.svg">' for i in range(36))
            body = self.page("Image page", f"<h1>Image page ready</h1><section>{images}</section>")
            kind = "text/html; charset=utf-8"
        else:
            body = self.page("Unknown route", "<h1>Unknown route</h1>")
            kind = "text/html; charset=utf-8"
            self.send_response(404)
            self.send_header("Content-Type", kind)
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    @staticmethod
    def page(title: str, content: str) -> bytes:
        return ("<!doctype html><html><head><meta charset='utf-8'>"
                f"<title>{title}</title><meta name='viewport' content='width=device-width'>"
                "<style>body{font:16px sans-serif;margin:2rem} img{margin:4px}</style>"
                f"</head><body>{content}</body></html>").encode()

    def log_message(self, *_args):
        pass


def process_tree(root_pid: int) -> list[int]:
    parents: dict[int, int] = {}
    for entry in pathlib.Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            status = (entry / "status").read_text()
            ppid = next(int(line.split()[1]) for line in status.splitlines() if line.startswith("PPid:"))
            parents[int(entry.name)] = ppid
        except (OSError, StopIteration, ValueError):
            continue
    result = {root_pid}
    changed = True
    while changed:
        before = len(result)
        result.update(pid for pid, ppid in parents.items() if ppid in result)
        changed = len(result) != before
    return sorted(result)


def process_memory(pid: int) -> tuple[int, int] | None:
    try:
        content = (pathlib.Path("/proc") / str(pid) / "smaps_rollup").read_text()
        values = {}
        for line in content.splitlines():
            if line.startswith(("Rss:", "Pss:")):
                key, number, _unit = line.split()
                values[key[:-1]] = int(number)
        return values["Rss"], values["Pss"]
    except (OSError, KeyError, ValueError):
        return None


class Measurement:
    def __init__(self, pid: int):
        self.pid = pid
        self.rows: list[dict] = []

    def snapshot(self) -> dict:
        pids = process_tree(self.pid)
        samples = [memory for pid in pids if (memory := process_memory(pid)) is not None]
        return {
            "rss_mib": round(sum(item[0] for item in samples) / 1024, 1),
            "pss_mib": round(sum(item[1] for item in samples) / 1024, 1),
            "processes": len(samples),
        }

    def sample_for(self, name: str, seconds: float):
        samples = []
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            samples.append(self.snapshot())
            time.sleep(min(1.0, max(0, end - time.monotonic())))
        if not samples:
            samples.append(self.snapshot())
        row = {"stage": name, "duration_seconds": seconds, "samples": len(samples)}
        for key in ("rss_mib", "pss_mib", "processes"):
            values = [sample[key] for sample in samples]
            row[f"{key}_average"] = round(sum(values) / len(values), 1)
            row[f"{key}_peak"] = max(values)
        self.rows.append(row)


def type_url(window: str, url: str, new_tab: bool = False):
    subprocess.run(["xdotool", "windowfocus", "--sync", window], check=True)
    if new_tab:
        subprocess.run(["xdotool", "key", "ctrl+t"], check=True)
        time.sleep(0.8)
    subprocess.run(["xdotool", "key", "ctrl+l"], check=True)
    subprocess.run(["xdotool", "type", "--clearmodifiers", "--delay", "1", url], check=True)
    subprocess.run(["xdotool", "key", "Return"], check=True)


def read_window_title(window: str) -> str:
    return subprocess.check_output(["xdotool", "getwindowname", window], text=True).strip()


def wait_for_site_title(window: str, expected_host: str, previous_title: str,
                        timeout: float = 30) -> tuple[bool, str]:
    end = time.monotonic() + timeout
    last_title = ""
    expected_prefix = f"Hue Browser — {expected_host} — "
    while time.monotonic() < end:
        last_title = read_window_title(window)
        if last_title.startswith(expected_prefix):
            page_title = last_title[len(expected_prefix):].strip()
            if page_title and page_title not in ("Carregando", "Nova guia") and page_title != previous_title:
                return True, last_title
        time.sleep(0.25)
    return False, last_title


def wait_for_hit(path: str, server: http.server.ThreadingHTTPServer, timeout: float = 20):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        with Pages.hit_lock:
            if Pages.hits[path]:
                return
        time.sleep(0.1)
    raise RuntimeError(f"A página de qualidade não foi requisitada: {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default="memory-results.json", help="arquivo JSON de resultados")
    parser.add_argument("--settle-seconds", type=float, default=8, help="tempo de amostragem por cenário")
    parser.add_argument("--site-seconds", type=float, default=12, help="tempo de amostragem por site real")
    args = parser.parse_args()

    if not APP.is_file():
        print(f"Binário não encontrado: {APP}. Compile o projeto primeiro.", file=sys.stderr)
        return 2
    if not os.environ.get("DISPLAY"):
        print("DISPLAY ausente. Execute em desktop ou use: xvfb-run -a python3 tools/memory_benchmark.py", file=sys.stderr)
        return 2
    for command in ("xdotool",):
        if not __import__("shutil").which(command):
            print(f"Dependência ausente: {command}", file=sys.stderr)
            return 2

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Pages)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_port}"
    profile_tmp = tempfile.TemporaryDirectory(prefix="hue-browser-benchmark-")
    test_env = os.environ.copy()
    test_env["XDG_DATA_HOME"] = str(pathlib.Path(profile_tmp.name) / "data")
    test_env["XDG_CACHE_HOME"] = str(pathlib.Path(profile_tmp.name) / "cache")
    browser = subprocess.Popen([str(APP)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=test_env)
    try:
        window = subprocess.check_output(
            ["xdotool", "search", "--sync", "--onlyvisible", "--name", "Hue Browser"], text=True, timeout=30
        ).strip().splitlines()[-1]
        measure = Measurement(browser.pid)
        measure.sample_for("inicialização e página inicial", args.settle_seconds)

        scenarios = [("página leve", "/light"), ("DOM 5000 nós", "/dom"), ("36 imagens", "/images")]
        for name, path in scenarios:
            type_url(window, base + path)
            wait_for_hit(path, server)
            measure.sample_for(name, args.settle_seconds)

        type_url(window, base + "/light", new_tab=True)
        wait_for_hit("/light", server)
        type_url(window, base + "/dom", new_tab=True)
        wait_for_hit("/dom", server)
        measure.sample_for("três abas carregadas", args.settle_seconds)

        # Close the two additional local-test tabs so each real-site sample is a
        # single active web page rather than cumulative content from other sites.
        for _ in range(2):
            subprocess.run(["xdotool", "windowfocus", "--sync", window], check=True)
            subprocess.run(["xdotool", "key", "ctrl+w"], check=True)
            time.sleep(0.5)

        real_site_results = []
        for site in REAL_SITES:
            previous_window_title = read_window_title(window)
            previous_page_title = previous_window_title.rsplit(" — ", 1)[-1]
            type_url(window, site["url"])
            loaded, title = wait_for_site_title(window, site["host"], previous_page_title)
            measure.sample_for(f"site real: {site['name']}", args.site_seconds)
            real_site_results.append({**site, "loaded": loaded,
                                      "previous_page_title": previous_page_title,
                                      "window_title": title})

        if browser.poll() is not None:
            raise RuntimeError(f"O navegador encerrou durante o cenário (código {browser.returncode})")
        with Pages.hit_lock:
            hits = dict(Pages.hits)
        required = ["/light", "/dom", "/images", "/js-complete"]
        missing = [path for path in required if not hits.get(path)]
        loaded_images = sum(count for path, count in hits.items() if path.startswith("/image/"))
        if loaded_images < 36:
            missing.append(f"36 imagens requisitadas (recebidas: {loaded_images})")
        if missing:
            raise RuntimeError("Rotas não requisitadas: " + ", ".join(missing))
        ram_total_kib = None
        try:
            for line in pathlib.Path("/proc/meminfo").read_text().splitlines():
                if line.startswith("MemTotal:"):
                    ram_total_kib = int(line.split()[1])
                    break
        except OSError:
            pass
        result = {
            "application": str(APP),
            "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "platform": {"system": os.uname().sysname, "release": os.uname().release,
                         "machine": os.uname().machine, "ram_total_kib": ram_total_kib},
            "method": "soma PSS/RSS dos processos descendentes; páginas locais determinísticas",
            "quality": {"browser_alive": True, "required_routes_requested": required, "http_hits": hits},
            "real_sites": {"all_loaded": all(site["loaded"] for site in real_site_results),
                           "results": real_site_results},
            "measurements": measure.rows,
        }
        output = pathlib.Path(args.output)
        output.write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
        return 0 if result["real_sites"]["all_loaded"] else 1
    except Exception as error:
        print(f"BENCHMARK FAILED: {error}", file=sys.stderr)
        return 1
    finally:
        if browser.poll() is None:
            browser.send_signal(signal.SIGTERM)
            try:
                browser.wait(timeout=5)
            except subprocess.TimeoutExpired:
                browser.kill()
        server.shutdown()
        profile_tmp.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
