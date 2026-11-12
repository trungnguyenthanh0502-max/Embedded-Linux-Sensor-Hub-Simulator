#!/usr/bin/env python3
"""Integration test for Sensor Hub. Usage: scripts/test.py [--build-dir build]"""
import argparse
import json
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

PORT = 5555
SHM = Path("/dev/shm/sensor_hub_shm")
MQ = Path("/dev/mqueue/sensor_hub_alarm")
SENSORS = {"temperature", "humidity", "pressure", "light"}
STATUSES = {"OK", "WARNING", "CRITICAL", "INVALID"}
KEYS = {"t", "raw", "avg", "status", "ts", "valid", "invalid"}
VALID_MAX = {"temperature": 125, "humidity": 100, "pressure": 1100, "light": 1100}

failures = []


def check(cond, msg):
    print(("[PASS] " if cond else "[FAIL] ") + msg)
    if not cond:
        failures.append(msg)
    return cond


def wait_for(pred, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if pred():
            return True
        time.sleep(0.1)
    return pred()


def connect(timeout=5.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            return socket.create_connection(("127.0.0.1", PORT), timeout=1)
        except OSError:
            time.sleep(0.2)
    return None


def collect(sock, seconds):
    """Đọc seconds giây, trả về danh sách dòng hoàn chỉnh (bỏ dòng cụt cuối)."""
    sock.settimeout(0.5)
    buf = b""
    end = time.time() + seconds
    while time.time() < end:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            continue
        except OSError:
            break
        if not data:
            break
        buf += data
    return [p.decode(errors="replace") for p in buf.split(b"\n")[:-1] if p]


def parse(lines):
    objs, bad = [], 0
    for line in lines:
        try:
            o = json.loads(line)
        except ValueError:
            bad += 1
            continue
        if isinstance(o, dict) and KEYS <= o.keys():
            objs.append(o)
        else:
            bad += 1
    return objs, bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--duration", type=float, default=8.0)
    args = ap.parse_args()

    hub = (Path(args.build_dir) / "sensor_hub").resolve()
    if not hub.exists():
        print(f"hub binary not found: {hub}")
        return 2

    try:
        socket.create_connection(("127.0.0.1", PORT), timeout=0.5).close()
        print(f"port {PORT} already in use - stop the running hub first")
        return 2
    except OSError:
        pass

    workdir = Path(tempfile.mkdtemp(prefix="hubtest_"))
    errpath = workdir / "stderr.txt"
    proc = subprocess.Popen([str(hub)], cwd=workdir,
                            stdout=subprocess.DEVNULL, stderr=open(errpath, "w"))
    try:
        # ---- 1. Khởi động ----
        s1 = connect(8)
        check(s1 is not None, f"hub accepts TCP connections on port {PORT}")
        if s1 is None:
            return 1
        check(proc.poll() is None, "hub process is running")
        check(wait_for(SHM.exists, 3), "shared memory /dev/shm/sensor_hub_shm exists")
        check(wait_for(MQ.exists, 3), "message queue /dev/mqueue/sensor_hub_alarm exists")
        s2 = connect(2)
        check(s2 is not None, "second client can connect (broadcast)")

        # ---- 2. Dữ liệu ----
        lines = collect(s1, args.duration)
        objs, bad = parse(lines)
        check(len(objs) >= 10, f"received enough JSON lines ({len(objs)})")
        check(bad == 0, f"all lines are valid JSON with required keys (bad={bad})")
        check({o["t"] for o in objs} == SENSORS, "all 4 sensors present")
        check(all(o["status"] in STATUSES for o in objs), "status values are valid")
        check(not [o for o in objs if o["raw"] >= 9999 and o["status"] != "INVALID"],
              "raw=9999 is always INVALID for every sensor")
        check(not [o for o in objs if o["status"] != "INVALID" and o["avg"] > VALID_MAX[o["t"]]],
              "filtered value never exceeds physical range")

        by = {}
        for o in objs:
            by.setdefault(o["t"], []).append(o["valid"])
        for name, v in sorted(by.items()):
            check(all(a <= b for a, b in zip(v, v[1:])), f"{name}: valid counter never decreases")
            check(v[-1] > v[0], f"{name}: valid counter increases ({v[0]} -> {v[-1]})")

        # ---- 3. Client thứ hai nhận được dữ liệu ----
        o2, _ = parse(collect(s2, 1.5))
        check(len(o2) > 0, "second client also receives data")

        # ---- 4. Client ngắt đột ngột (RST) không làm hub chết (SIGPIPE) ----
        s2.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        s2.close()
        time.sleep(2.5)
        check(proc.poll() is None, "hub survives an abruptly closed client")
        s3 = connect(2)
        o3, _ = parse(collect(s3, 2.5)) if s3 else ([], 0)
        check(len(o3) > 0, "new client still receives data afterwards")
        for s in (s1, s3):
            if s:
                s.close()

        # ---- 5. Tắt sạch ----
        proc.send_signal(signal.SIGTERM)
        try:
            rc = proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            proc.kill()
            rc = None
        check(rc == 0, f"hub exits cleanly on SIGTERM (exit code {rc})")
        time.sleep(0.3)
        check(not SHM.exists(), "shared memory removed on exit")
        check(not MQ.exists(), "message queue removed on exit")

        log = workdir / "sensor_hub.log"
        text = log.read_text() if log.exists() else ""
        check("sensor hub started" in text, "log contains 'sensor hub started'")
        check("sensor hub stopped" in text, "log contains 'sensor hub stopped'")
        err = errpath.read_text()
        check("Sanitizer" not in err, "no sanitizer reports in stderr")
        if "Sanitizer" in err:
            print(err)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()

    print(f"\n{'FAILED: ' + str(len(failures)) + ' check(s)' if failures else 'ALL CHECKS PASSED'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
