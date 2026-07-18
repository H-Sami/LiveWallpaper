import json
import os
import statistics
import subprocess
import time
import uuid
from pathlib import Path

import psutil

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "build" / "LiveWallpaper.Host.exe"
DATA = Path(os.environ["LOCALAPPDATA"]) / "LiveWallpaper"
SETTINGS = DATA / "settings.ini"
ARTIFACT = ROOT / "artifacts" / "performance.json"
CREATE_NO_WINDOW = 0x08000000


def stop_host():
    subprocess.run([str(HOST), "--exit"], cwd=ROOT, timeout=15,
                   creationflags=CREATE_NO_WINDOW, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline and find_host() is not None:
        time.sleep(0.1)
    process = find_host()
    if process is not None:
        process.kill()
        process.wait(5)


def find_host():
    expected = str(HOST.resolve()).lower()
    for process in psutil.process_iter(["exe", "name"]):
        try:
            if (process.info["exe"] or "").lower() == expected:
                return process
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass
    return None


def write_settings(media: Path, playing=True):
    DATA.mkdir(parents=True, exist_ok=True)
    SETTINGS.write_text(
        "version=1\n"
        f"media={media}\n"
        "muted=1\nvolume=50\nstart_ms=0\nend_ms=0\n"
        "close_to_tray=1\n"
        f"playing={1 if playing else 0}\n",
        encoding="utf-8",
    )


def apply(media: Path):
    write_settings(media)
    return legacy_command("--apply", "playing")


def legacy_command(action: str, expected_state: str):
    status = DATA / "runtime.status"
    status.unlink(missing_ok=True)
    started = time.perf_counter()
    if find_host() is None:
        subprocess.Popen([str(HOST), action], cwd=ROOT, creationflags=CREATE_NO_WINDOW,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        command = subprocess.run([str(HOST), action], cwd=ROOT, timeout=15,
                                 creationflags=CREATE_NO_WINDOW,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if command.returncode != 0:
            raise RuntimeError(f"Relay command failed with code {command.returncode}")
    deadline = time.monotonic() + 15
    payload = ""
    while time.monotonic() < deadline:
        if status.exists():
            payload = status.read_text(encoding="utf-8", errors="replace")
            values = dict(line.split("=", 1) for line in payload.splitlines() if "=" in line)
            if values.get("state") == expected_state:
                return time.perf_counter() - started
            if values.get("state") == "error":
                raise RuntimeError(payload)
        time.sleep(0.025)
    raise TimeoutError(f"Command timed out: {payload}")


def snapshot(process):
    info = process.memory_info()
    private = getattr(info, "private", info.rss)
    return {
        "private_bytes": private,
        "working_set_bytes": info.rss,
        "handles": process.num_handles(),
        "threads": process.num_threads(),
    }


def cpu_samples(process, count, interval=1.0):
    process.cpu_percent(None)
    return [process.cpu_percent(interval=interval) for _ in range(count)]


def gpu_percent(pid):
    command = (
        "$ErrorActionPreference='Stop'; "
        f"$p='pid_{pid}_'; "
        "$s=(Get-Counter '\\GPU Engine(*)\\Utilization Percentage' -SampleInterval 1 -MaxSamples 1).CounterSamples; "
        "[Math]::Round((($s | Where-Object {$_.Path -like ('*'+$p+'*')} | Measure-Object CookedValue -Sum).Sum),3)"
    )
    try:
        result = subprocess.run(["powershell.exe", "-NoProfile", "-Command", command],
                                capture_output=True, text=True, timeout=15,
                                creationflags=CREATE_NO_WINDOW)
        if result.returncode == 0 and result.stdout.strip():
            return float(result.stdout.strip())
    except Exception:
        pass
    return None


def summarize_cpu(values):
    return {"average_percent": round(statistics.mean(values), 3),
            "peak_percent": round(max(values), 3), "samples": len(values)}


def main():
    original = SETTINGS.read_bytes() if SETTINGS.exists() else None
    original_playing = original is not None and b"playing=1" in original
    results = {"logical_processors": psutil.cpu_count(), "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    try:
        stop_host()
        startup = apply(ROOT / "tests" / "fixtures" / "test-1080p.mp4")
        process = find_host()
        if process is None:
            raise RuntimeError("Host process not found after Apply")
        time.sleep(2)
        base = snapshot(process)
        playback_cpu = cpu_samples(process, 8)
        playback_gpu = [value for value in (gpu_percent(process.pid) for _ in range(3)) if value is not None]
        results["startup_seconds"] = round(startup, 3)
        results["playback_1080p"] = {**base, "cpu": summarize_cpu(playback_cpu),
                                     "gpu_average_percent": round(statistics.mean(playback_gpu), 3) if playback_gpu else None}

        legacy_command("--stop", "stopped")
        time.sleep(1)
        idle_cpu = cpu_samples(process, 5)
        results["stopped_idle"] = {**snapshot(process), "cpu": summarize_cpu(idle_cpu)}

        results["switch_to_4k_seconds"] = round(apply(ROOT / "tests" / "fixtures" / "test-4k.mp4"), 3)
        process = find_host()
        time.sleep(2)
        four_k_start = snapshot(process)
        four_k_cpu = cpu_samples(process, 8)
        four_k_gpu = [value for value in (gpu_percent(process.pid) for _ in range(3)) if value is not None]
        results["playback_4k"] = {**four_k_start, "cpu": summarize_cpu(four_k_cpu),
                                  "gpu_average_percent": round(statistics.mean(four_k_gpu), 3) if four_k_gpu else None}

        sustained = []
        sustained_started = time.monotonic()
        process.cpu_percent(None)
        while time.monotonic() - sustained_started < 60:
            time.sleep(5)
            sustained.append({"elapsed_seconds": round(time.monotonic() - sustained_started, 1),
                              "cpu_percent": process.cpu_percent(None), **snapshot(process)})
        private_growth = sustained[-1]["private_bytes"] - sustained[0]["private_bytes"]
        handle_growth = sustained[-1]["handles"] - sustained[0]["handles"]
        thread_growth = sustained[-1]["threads"] - sustained[0]["threads"]
        results["sustained_4k_60s"] = {
            "samples": sustained,
            "private_growth_bytes": private_growth,
            "handle_growth": handle_growth,
            "thread_growth": thread_growth,
            "passed_growth_limits": private_growth < 32 * 1024 * 1024 and handle_growth < 50 and thread_growth < 10,
        }
        if not results["sustained_4k_60s"]["passed_growth_limits"]:
            raise RuntimeError("Sustained-run resource growth exceeded limits")
        ARTIFACT.parent.mkdir(parents=True, exist_ok=True)
        ARTIFACT.write_text(json.dumps(results, indent=2), encoding="utf-8")
        print(json.dumps(results, indent=2))
    finally:
        stop_host()
        if original is None:
            SETTINGS.unlink(missing_ok=True)
        else:
            SETTINGS.write_bytes(original)
            if original_playing:
                request = "restore" + uuid.uuid4().hex
                subprocess.Popen([str(HOST), f"--apply|{request}"], cwd=ROOT,
                                 creationflags=CREATE_NO_WINDOW,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
