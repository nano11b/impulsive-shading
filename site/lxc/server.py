"""Small standalone host for the Impulsive Shading benchmark dashboard."""
import hmac
import hashlib
import ipaddress
import json
import os
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from uuid import uuid4

ROOT = Path(__file__).resolve().parent
INDEX = ROOT / "index.html"
DOWNLOAD_PAGE = ROOT / "download.html"
DOWNLOAD_ZIP = ROOT / "Impulsive-Shading-Benchmark-Win64.zip"
DOWNLOAD_HASH = ROOT / "download.sha256"
LINUX_DOWNLOAD = ROOT / "Impulsive-Shading-Benchmark-Linux-x86_64.tar.gz"
LINUX_HASH = ROOT / "linux-download.sha256"
DATA_DIR = Path(os.environ.get("MARKS_DATA_DIR", "/var/lib/impulsive-marks/reports"))
TOKEN_FILE = Path(os.environ.get("MARKS_TOKEN_FILE", "/etc/impulsive-marks/upload-token"))
RATE_FILE = DATA_DIR.parent / "community-rate-limits.json"
HOST = os.environ.get("MARKS_HOST", "0.0.0.0")
PORT = int(os.environ.get("MARKS_PORT", "8096"))
MAX_REPORT_BYTES = 65536
EXPECTED_SHADER_IDS = set("73yXR1 sXyXR1 7XyXR1 s3GSz1 73GSz1 sXGSz1 7XGSz1 s3ySz1 sXySz1 7XySz1 s3GXz1 73GXz1 sXGXz1 7XGXz1 s3yXz1 73yXz1 sXyXz1 N3KSzh fXKSzh NXKSzh N11B01".split())
RATE_LOCK = threading.Lock()


def positive(value):
    return type(value) in (int, float) and value > 0 and value < float("inf")


def valid_report(report):
    if not isinstance(report, dict) or report.get("schema_version") != 1 or report.get("app") != "Impulsive Shading":
        return False
    if not isinstance(report.get("gpu_renderer"), str) or len(report["gpu_renderer"]) >= 300:
        return False
    resolution = report.get("resolution")
    if not isinstance(resolution, dict):
        return False
    for dimension in ("width", "height"):
        value = resolution.get(dimension)
        if type(value) is not int or not 0 < value <= 16384:
            return False
    results = report.get("results")
    if not isinstance(results, list) or not 0 < len(results) <= 100:
        return False
    for result in results:
        if not isinstance(result, dict):
            return False
        if not isinstance(result.get("shader_id"), str) or len(result["shader_id"]) >= 100:
            return False
        if not isinstance(result.get("shader_name"), str) or len(result["shader_name"]) >= 500:
            return False
        metrics = result.get("metrics")
        if not isinstance(metrics, dict) or not all(positive(metrics.get(k)) for k in
                ("average_fps", "median_frame_ms", "p95_frame_ms")):
            return False
        if type(metrics.get("frames")) is not int or metrics["frames"] <= 0:
            return False
    return True


def valid_community_report(report):
    return (valid_report(report)
            and report.get("resolution") == {"width": 1280, "height": 720}
            and report.get("vsync") is False
            and positive(report.get("target_seconds"))
            and 2 <= report["target_seconds"] <= 300
            and positive(report.get("warmup_seconds"))
            and report["warmup_seconds"] >= 0.5
            and len(report["results"]) == len(EXPECTED_SHADER_IDS)
            and {r["shader_id"] for r in report["results"]} == EXPECTED_SHADER_IDS)


def client_ip(handler):
    peer = ipaddress.ip_address(handler.client_address[0])
    if peer.is_loopback:
        forwarded = handler.headers.get("CF-Connecting-IP")
        if forwarded:
            try:
                return str(ipaddress.ip_address(forwarded))
            except ValueError:
                pass
    return str(peer)


def load_limits():
    try:
        return json.loads(RATE_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_limits(limits):
    temporary = RATE_FILE.with_suffix(".tmp")
    temporary.write_text(json.dumps(limits, separators=(",", ":")), encoding="utf-8")
    temporary.replace(RATE_FILE)


class Handler(BaseHTTPRequestHandler):
    def send_bytes(self, status, body, content_type, extra_headers=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        for name, value in (extra_headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, status, payload):
        self.send_bytes(status, json.dumps(payload, separators=(",", ":")).encode(), "application/json; charset=utf-8")

    def do_GET(self):
        if self.path == "/":
            self.send_bytes(200, INDEX.read_bytes(), "text/html; charset=utf-8")
        elif self.path in ("/download", "/download/"):
            page = DOWNLOAD_PAGE.read_text(encoding="utf-8")
            archive_size = DOWNLOAD_ZIP.stat().st_size
            size_text = f"{archive_size / (1024 * 1024):.1f} MB"
            linux_size = f"{LINUX_DOWNLOAD.stat().st_size / (1024 * 1024):.1f} MB"
            page = (page.replace("{{ZIP_SIZE}}", size_text)
                    .replace("{{SHA256}}", DOWNLOAD_HASH.read_text(encoding="utf-8").strip())
                    .replace("{{LINUX_SIZE}}", linux_size)
                    .replace("{{LINUX_SHA256}}", LINUX_HASH.read_text(encoding="utf-8").strip()))
            self.send_bytes(200, page.encode("utf-8"), "text/html; charset=utf-8")
        elif self.path == "/downloads/Impulsive-Shading-Benchmark-Win64.zip":
            self.send_bytes(200, DOWNLOAD_ZIP.read_bytes(), "application/zip",
                            {"Content-Disposition": 'attachment; filename="Impulsive-Shading-Benchmark-Win64.zip"'})
        elif self.path == "/downloads/Impulsive-Shading-Benchmark-Linux-x86_64.tar.gz":
            self.send_bytes(200, LINUX_DOWNLOAD.read_bytes(), "application/gzip",
                            {"Content-Disposition": 'attachment; filename="Impulsive-Shading-Benchmark-Linux-x86_64.tar.gz"'})
        elif self.path == "/api/reports":
            files = sorted(DATA_DIR.glob("*.json"), reverse=True)[:100]
            reports = []
            for path in files:
                try:
                    reports.append(json.loads(path.read_text(encoding="utf-8")))
                except (OSError, ValueError):
                    self.log_error("Could not read report %s", path.name)
            self.send_json(200, {"reports": reports})
        elif self.path == "/health":
            self.send_json(200, {"status": "ok"})
        else:
            self.send_json(404, {"error": "Not found"})

    def do_POST(self):
        community = self.path == "/api/community-reports"
        if self.path != "/api/reports" and not community:
            self.send_json(404, {"error": "Not found"})
            return
        try:
            expected = TOKEN_FILE.read_text(encoding="utf-8").strip()
        except OSError:
            self.send_json(503, {"error": "Upload credentials unavailable"})
            return
        supplied = self.headers.get("Authorization", "")
        if not expected:
            self.send_json(503, {"error": "Upload credentials unavailable"})
            return
        if not community and not hmac.compare_digest(supplied, "Bearer " + expected):
            self.send_json(401, {"error": "Unauthorized"})
            return
        try:
            size = int(self.headers.get("Content-Length", ""))
        except ValueError:
            size = 0
        if not 0 < size <= MAX_REPORT_BYTES:
            self.send_json(413, {"error": "Report must be 1–65536 bytes"})
            return
        try:
            report = json.loads(self.rfile.read(size))
        except (ValueError, UnicodeDecodeError):
            self.send_json(400, {"error": "Invalid JSON"})
            return
        if not (valid_community_report(report) if community else valid_report(report)):
            self.send_json(400, {"error": "Invalid benchmark report"})
            return
        now = datetime.now(timezone.utc)
        report_id = str(uuid4())
        received_at = now.isoformat(timespec="milliseconds").replace("+00:00", "Z")
        stored = {**report, "id": report_id, "received_at": received_at,
                  "source": "community" if community else "owner"}
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        filename = now.strftime("%Y-%m-%dT%H-%M-%S.%fZ") + "-" + report_id + ".json"
        target = DATA_DIR / filename
        if community:
            fingerprint = hashlib.sha256((expected + client_ip(self)).encode()).hexdigest()
            with RATE_LOCK:
                cutoff = time.time() - 3600
                limits = {key: recent for key, stamps in load_limits().items()
                          if (recent := [stamp for stamp in stamps if isinstance(stamp, (int, float)) and stamp > cutoff])}
                times = limits.get(fingerprint, [])
                if len(times) >= 5:
                    self.send_json(429, {"error": "Submission limit reached. Try again in an hour."})
                    return
                with target.open("x", encoding="utf-8") as file:
                    json.dump(stored, file, separators=(",", ":"))
                limits[fingerprint] = times + [time.time()]
                save_limits(limits)
        else:
            with target.open("x", encoding="utf-8") as file:
                json.dump(stored, file, separators=(",", ":"))
        self.send_json(201, {"id": report_id, "received_at": received_at})


if __name__ == "__main__":
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
