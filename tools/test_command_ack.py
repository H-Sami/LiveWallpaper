import ctypes
import subprocess
import time
import urllib.parse
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "build" / "LiveWallpaper.Host.exe"
DATA = Path.home() / "AppData" / "Local" / "LiveWallpaper"
SETTINGS = DATA / "settings.ini"
STATUS = DATA / "runtime.status"
FIXTURE_A = (ROOT / "tests" / "fixtures" / "test-1080p.mp4").resolve()
FIXTURE_B = (ROOT / "tests" / "fixtures" / "test-h264.mkv").resolve()


def request_id():
    return uuid.uuid4().hex


def request_path(value):
    return Path(f"{STATUS}.{value}")


def write_settings(path, playing=True):
    SETTINGS.write_text(
        "version=1\n"
        f"media={path}\n"
        "muted=1\nvolume=0\nstart_ms=0\nend_ms=0\n"
        f"close_to_tray=1\nplaying={1 if playing else 0}\n",
        encoding="utf-8",
    )


def parse_status(path):
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = urllib.parse.unquote(value)
    return values


def wait_status(value, terminal_states, timeout=10):
    path = request_path(value)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            status = parse_status(path)
        except (FileNotFoundError, PermissionError, UnicodeDecodeError):
            time.sleep(0.05)
            continue
        if status.get("request_id") != value:
            raise RuntimeError(f"wrong request ID in {path}: {status}")
        if status.get("state") in terminal_states:
            return status
        time.sleep(0.05)
    raise TimeoutError(f"request {value} never reached {terminal_states}")


def correlated(action, value, wait=True):
    process = subprocess.Popen([str(HOST), f"{action}|{value}"])
    if wait:
        return process.wait(timeout=12)
    return process


DATA.mkdir(parents=True, exist_ok=True)
original = SETTINGS.read_bytes() if SETTINGS.exists() else None
host_process = None
try:
    subprocess.run([str(HOST), "--exit"], timeout=8, check=False)

    blocked_id = request_id()
    blocked_path = request_path(blocked_id)
    blocked_path.mkdir(parents=False)
    try:
        assert correlated("--stop", blocked_id) != 0
    finally:
        blocked_path.rmdir()

    stop_id = request_id()
    assert correlated("--stop", stop_id) == 0
    stopped = wait_status(stop_id, {"stopped"})
    assert stopped["operation"] == "stop"
    assert "playing=0" in SETTINGS.read_text(encoding="utf-8")

    write_settings(FIXTURE_A)
    first_id = request_id()
    host_process = correlated("--apply", first_id, wait=False)
    first = wait_status(first_id, {"playing", "error", "pending-shell"})
    assert first["state"] == "playing", first
    assert first["operation"] == "apply"

    write_settings(FIXTURE_B)
    relay_id = request_id()
    assert correlated("--apply", relay_id) == 0
    relayed = wait_status(relay_id, {"playing", "error", "pending-shell"})
    assert relayed["state"] == "playing", relayed

    write_settings(ROOT / "tests" / "fixtures" / "missing-video.mp4")
    invalid_id = request_id()
    assert correlated("--apply", invalid_id) != 0
    invalid = wait_status(invalid_id, {"error"})
    assert invalid["error_code"] == "file-not-found", invalid

    write_settings(FIXTURE_A)
    STATUS.write_text(
        "version=1\nrequest_id=stale\noperation=apply\nstate=playing\nerror_code=\nmessage=\n",
        encoding="utf-8",
    )
    stale_safe_id = request_id()
    assert correlated("--apply", stale_safe_id) == 0
    stale_safe = wait_status(stale_safe_id, {"playing", "error", "pending-shell"})
    assert stale_safe["state"] == "playing", stale_safe

    first_concurrent_id = request_id()
    second_concurrent_id = request_id()
    first_command = correlated("--apply", first_concurrent_id, wait=False)
    second_command = correlated("--apply", second_concurrent_id, wait=False)
    assert first_command.wait(timeout=12) == 0
    assert second_command.wait(timeout=12) == 0
    first_concurrent = wait_status(first_concurrent_id, {"playing", "error", "superseded"})
    second_concurrent = wait_status(second_concurrent_id, {"playing", "error", "superseded"})
    assert second_concurrent["state"] == "playing", second_concurrent
    assert first_concurrent["state"] in {"playing", "superseded"}, first_concurrent

    process_handle = ctypes.windll.kernel32.OpenProcess(0x0800, False, host_process.pid)
    if not process_handle:
        raise ctypes.WinError()
    try:
        if ctypes.windll.ntdll.NtSuspendProcess(process_handle) != 0:
            raise RuntimeError("NtSuspendProcess failed")
        hung_id = request_id()
        started = time.monotonic()
        hung_command = subprocess.run([str(HOST), f"--apply|{hung_id}"], timeout=9)
        elapsed = time.monotonic() - started
        assert hung_command.returncode == 3, hung_command.returncode
        assert 4.5 <= elapsed < 9, elapsed
        assert not request_path(hung_id).exists()
    finally:
        ctypes.windll.ntdll.NtResumeProcess(process_handle)
        ctypes.windll.kernel32.CloseHandle(process_handle)

    print("Command acknowledgement integration tests passed")
finally:
    subprocess.run([str(HOST), "--exit"], timeout=8, check=False)
    if host_process is not None:
        try:
            host_process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            host_process.kill()
    if original is None:
        SETTINGS.unlink(missing_ok=True)
    else:
        SETTINGS.write_bytes(original)
    for path in DATA.glob("runtime.status.*"):
        if path.name != "runtime.status.tmp":
            path.unlink(missing_ok=True)
