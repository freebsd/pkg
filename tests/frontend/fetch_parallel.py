#!/usr/bin/env python3
"""Exercise the real download queue against an instrumented HTTP/1.1 server."""

import http.server
from email.utils import formatdate

import errno
import fcntl
import json
import os
import pty
import re
from pathlib import Path
import shutil
import signal
import select
import struct
import socketserver
import subprocess
import sys
import termios
import threading
import time


class Downloads:
    def __init__(self):
        self.lock = threading.Condition()
        self.active = 0
        self.reset()

    def reset(self, failure=None, first_wave=None, refill=False, other_repo=False):
        with self.lock:
            assert self.lock.wait_for(lambda: self.active == 0, timeout=10)
            self.maximum = 0

            self.by_repo = {}
            self.maximum_by_repo = {}
            self.requests = []
            self.by_host = {}
            self.maximum_by_host = {}
            self.mirrors = None
            self.reply = None
            self.reply_once = False
            self.redirect = None
            self.redirect_once = False
            self.redirected = set()
            self.failure = failure
            self.hold = False
            self.released = set()
            self.first_wave = first_wave
            self.refill = refill
            self.other_repo = other_repo


class Terminal:
    """Interpret the cursor operations used by the download display."""

    def __init__(self, columns=160, numbered=True):
        self.lines = [[]]
        self.row = self.column = 0
        self.columns = columns
        self.numbered = numbered
        self.pending = ""
        self.frames = []

    def feed(self, text):
        self.pending += text
        while self.pending:
            if self.pending.startswith("\033"):
                match = re.match(r"\033\[([0-9]*)([AJK])", self.pending)
                if not match:
                    assert len(self.pending) < 20, repr(self.pending[:20])
                    break
                number, command = match.groups()
                if command == "A":
                    self.row = max(0, self.row - int(number or 1))
                elif command == "J":
                    self.lines[self.row] = self.lines[self.row][:self.column]
                    del self.lines[self.row + 1:]
                else:
                    self.lines[self.row] = self.lines[self.row][:self.column]
                self.pending = self.pending[match.end():]
                continue
            char, self.pending = self.pending[0], self.pending[1:]
            if char == "\r":
                self.column = 0
            elif char == "\n":
                self.row += 1
                if self.row == len(self.lines):
                    self.lines.append([])
                self.frames.append(self.downloads())
            else:
                if self.column == self.columns:
                    self.row += 1
                    self.column = 0
                    if self.row == len(self.lines):
                        self.lines.append([])
                line = self.lines[self.row]
                line.extend(" " for _ in range(self.column + 1 - len(line)))
                line[self.column] = char
                self.column += 1

    def downloads(self):
        # Physical terminal rows can split a package name or its status fields.
        text = "".join("".join(line) for line in self.lines)
        prefix = r"\[(\d+)/7\] " if self.numbered else "()"
        return [(int(number) if number else i + 1, name.rstrip(), int(percent))
                for i, (number, name, percent) in enumerate(re.findall(
                    prefix + r"Fetching (parallel[^:]*):\s+(\d+)%", text))]


def main():
    pkg = str(Path(sys.argv[1]).resolve())
    root = Path.cwd()
    names = ["parallel0", "parallel1", "parallel2-" + "long-name-" * 12,
             "parallel3-medium", "parallel4", "parallel5-longer-name", "parallel6"]
    env = os.environ.copy()
    env.update(INSTALL_AS_USER="yes", PKG_ENABLE_PLUGINS="false", NO_TICK="yes")
    env.pop("FETCH_WORKERS", None)
    env.pop("FETCH_WORKERS_PER_HOST", None)
    env.pop("FETCH_HOSTS", None)
    for key in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY",
                "http_proxy", "https_proxy", "all_proxy"):
        env.pop(key, None)
    env["NO_PROXY"] = env["no_proxy"] = "127.0.0.1"
    state = Downloads()

    class Handler(http.server.SimpleHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(root), **kwargs)

        def log_message(self, *args):
            pass

        def handle(self):
            try:
                super().handle()
            except (BrokenPipeError, ConnectionResetError):
                pass

        def do_GET(self):
            if self.path == "/" and state.mirrors is not None:
                content = "".join(f"URL: {url}\n" for url in state.mirrors).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(content)))
                self.end_headers()
                self.wfile.write(content)
                return
            filename = self.path.rsplit("/", 1)[-1]
            if not (filename.startswith("parallel") and filename.endswith(".pkg")):
                try:
                    return super().do_GET()
                except (BrokenPipeError, ConnectionResetError):
                    return
            with state.lock:
                index = len(state.requests)
                request = {"path": self.path, "start": time.monotonic(),
                           "range": self.headers.get("Range"),
                           "agent": self.headers.get("User-Agent"),
                           "host": self.headers["Host"].split(":")[0]}
                state.requests.append(request)
                state.active += 1
                host = request["host"]
                state.by_host[host] = state.by_host.get(host, 0) + 1
                state.maximum_by_host[host] = max(
                    state.maximum_by_host.get(host, 0), state.by_host[host])
                reply = state.reply
                if reply is not None and state.reply_once:
                    state.reply = None
                redirect = state.redirect
                if state.redirect_once:
                    if self.path in state.redirected:
                        redirect = None
                    else:
                        state.redirected.add(self.path)

                repo = self.path.split("/")[1]
                state.by_repo[repo] = state.by_repo.get(repo, 0) + 1
                state.maximum_by_repo[repo] = max(
                    state.maximum_by_repo.get(repo, 0), state.by_repo[repo])
                state.maximum = max(state.maximum, state.active)
                failure = state.failure
                hold = state.hold
                first_wave = state.first_wave
                state.lock.notify_all()

            def finish_request():
                # Called with state.lock held. A client can start its next
                # request as soon as it receives the final response bytes.
                if "end" in request:
                    return
                request["end"] = time.monotonic()
                state.active -= 1
                state.by_host[host] -= 1
                state.by_repo[repo] -= 1
                state.lock.notify_all()

            try:
                if redirect is not None and host == "127.0.0.1":
                    with state.lock:
                        self.send_response(302)
                        self.send_header("Location", redirect(self.path) if callable(redirect)
                                         else redirect + self.path)
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                        finish_request()
                    return
                if reply is not None and host == "127.0.0.1":
                    status, delay = reply
                    with state.lock:
                        self.send_response(status)
                        if delay is not None:
                            self.send_header("Retry-After", delay)
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                        finish_request()
                    return
                if first_wave is not None:
                    with state.lock:
                        # Prove the first batch overlaps regardless of how long
                        # CI takes to schedule workers and accept connections.
                        assert state.lock.wait_for(
                            lambda: len(state.requests) >= first_wave, timeout=4), (
                                "first batch did not fill", first_wave, state.requests)
                        if index == 0 and state.refill:
                            assert state.lock.wait_for(
                                lambda: len(state.requests) > first_wave, timeout=4), (
                                    "download slot was not refilled", state.requests)
                        if index == 0 and state.other_repo:
                            assert state.lock.wait_for(
                                lambda: any(r["path"].split("/")[1] != repo
                                            for r in state.requests), timeout=4), (
                                                "other repository did not start",
                                                state.requests)
                elif not hold:
                    time.sleep(0.7 if index == 0 else 0.15)
                if failure == "missing":
                    with state.lock:
                        self.send_error(404)
                        finish_request()
                    return
                content = Path(self.translate_path(self.path)).read_bytes()
                if failure == "checksum":
                    content = bytes([content[0] ^ 1]) + content[1:]
                offset = 0
                if request["range"]:
                    offset = int(request["range"].split("=", 1)[1].split("-", 1)[0])
                    self.send_response(206)
                    self.send_header("Content-Range",
                                     f"bytes {offset}-{len(content)-1}/{len(content)}")
                else:
                    self.send_response(200)
                self.send_header("Content-Length", str(len(content) - offset))
                self.send_header("Last-Modified", self.date_time_string())
                self.end_headers()
                if hold:
                    # Publish a partial progress update, then let the terminal
                    # test choose which of the active downloads finishes first.
                    self.wfile.write(content[offset:offset + 8192])
                    self.wfile.flush()
                    offset += 8192
                    with state.lock:
                        assert state.lock.wait_for(
                            lambda: not state.hold or filename in state.released,
                            timeout=20)
                with state.lock:
                    self.wfile.write(content[offset:])
                    self.wfile.flush()
                    finish_request()
            except (BrokenPipeError, ConnectionResetError):
                pass
            finally:
                with state.lock:
                    finish_request()

    class Server(http.server.ThreadingHTTPServer):
        # Python versions with a backlog of five can delay the seventh
        # connection until after the first short download has finished.
        request_queue_size = len(names)

    server = Server(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f"http://127.0.0.1:{server.server_port}"

    def run(*args, ok=True):
        result = subprocess.run([pkg, "-C", str(root / "pkg.conf"), *args],
                                env=env, capture_output=True, text=True, timeout=30)
        if (result.returncode == 0) != ok:
            raise AssertionError(f"{args}: {result.returncode}\n"
                                 f"{result.stdout}\n{result.stderr}\n"
                                 f"requests: {state.requests}")
        return result

    def config(workers=None, host_workers=999, hosts=None, from_files=False):
        conf = {"PKG_DBDIR": str(root / "db"),
                "PKG_CACHEDIR": str(root / "cache"), "REPOS_DIR": [],
                "FETCH_RETRY": 1, "FETCH_TIMEOUT": 5, "WORKERS_COUNT": 1,
                "FETCH_HOSTS": hosts or {},
                "repositories": {
                    "one": {"url": base + "/one", "mirror_type": "none",
                            "env": {"HTTP_USER_AGENT": "parallel-one"}},
                    "two": {"url": base + "/two", "mirror_type": "none",
                            "env": {"HTTP_USER_AGENT": "parallel-two"}}}}
        if workers is not None:
            conf["FETCH_WORKERS"] = workers
        if host_workers is not None:
            conf["FETCH_WORKERS_PER_HOST"] = host_workers
        if from_files:
            directory = root / "reposconf"
            directory.mkdir(exist_ok=True)
            conf["REPOS_DIR"] = [str(directory)]
            for name, repo in conf.pop("repositories").items():
                (directory / f"{name}.conf").write_text(json.dumps({name: repo}))
        (root / "pkg.conf").write_text(json.dumps(conf))

    def clear_cache():
        shutil.rmtree(root / "cache", ignore_errors=True)

    def download_workers(pid):
        # BusyBox ps supports -o, but not the BSD -ax switches.
        args = ["/bin/ps", "-o", "pid,ppid"]
        if sys.platform != "linux":
            args.insert(1, "-ax")
        output = subprocess.check_output(args, text=True)
        parents = {int(row.split()[0]): int(row.split()[1])
                   for row in output.splitlines()[1:]}
        children = [child for child, parent in parents.items() if parent == pid]
        # The CLI has an outer supervisor for restarting after self-upgrades.
        if len(children) == 1:
            pid = children[0]
            children = [child for child, parent in parents.items() if parent == pid]
        assert len(children) == 3, children
        return pid, children

    def check_workers_reaped(children):
        for child in children:
            try:
                os.kill(child, 0)
            except ProcessLookupError:
                continue
            raise AssertionError(f"download worker {child} survived cancellation")

    def terminal_fetch(columns=160, reorder=False, *args, ok=True, cancel_after=None,
                       numbered=True):
        terminal = Terminal(columns, numbered)
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ,
                    struct.pack("HHHH", 24, columns, 0, 0))
        process = subprocess.Popen(
            [pkg, "-C", str(root / "pkg.conf"), "fetch", *args, "-U", "-y", "-a"],
            env=dict(env, TERM="xterm"), stdin=subprocess.DEVNULL,
            stdout=slave, stderr=slave)
        os.close(slave)
        output = ""
        first = None
        reordered = False
        before_cancel = None
        children = []
        held = None
        deadline = time.monotonic() + 30
        try:
            while True:
                assert time.monotonic() < deadline, output
                if not select.select([master], [], [], 0.1)[0]:
                    continue
                try:
                    data = os.read(master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                if not data:
                    break
                text = data.decode()
                output += text
                terminal.feed(text)
                for frame in terminal.frames:
                    assert sum(percent < 100 for _, _, percent in frame) <= 3, frame
                    if cancel_after is not None and before_cancel is None:
                        unfinished = [row for row in frame if 0 < row[2] < 100]
                        if cancel_after and held is None and len(unfinished) == 3:
                            # Keep one download pending while all others finish.
                            held = unfinished[0][1]
                            with state.lock:
                                state.released.update(
                                    f"{name}-1.pkg" for name in names
                                    if f"{name}-1" != held)
                                state.lock.notify_all()
                        if (sum(row[2] == 100 for row in frame) == cancel_after
                                and len(unfinished) == (1 if cancel_after else 3)):
                            before_cancel = frame
                            owner, children = download_workers(process.pid)
                            os.kill(owner, signal.SIGINT)
                    if reorder and first is None and len(frame) == 3 and all(
                            0 < percent < 100 for _, _, percent in frame):
                        assert [n for n, _, _ in frame] == [1, 2, 3], frame
                        first = [name for _, name, _ in frame]
                        with state.lock:
                            state.released.add(first[2] + ".pkg")
                            state.lock.notify_all()
                    if first and len(frame) >= 3 and frame[0][2] == 100:
                        assert frame[:3] == [
                            (1, first[2], 100), (2, first[0], frame[1][2]),
                            (3, first[1], frame[2][2])], frame
                        assert frame[1][2] < 100 and frame[2][2] < 100, frame
                        reordered = True
                        with state.lock:
                            state.hold = False
                            state.lock.notify_all()
                        first = None
                        reorder = False
                terminal.frames.clear()
            assert (process.wait(timeout=5) == 0) == ok, output
            if cancel_after is not None:
                assert before_cancel is not None, output
                assert "signal received, cleaning up" in output, output
                assert terminal.downloads() == before_cancel, output
                check_workers_reaped(children)
            if reorder:
                assert reordered, output
        finally:
            with state.lock:
                state.hold = False
                state.lock.notify_all()
            if process.poll() is None:
                process.kill()
                process.wait()
            os.close(master)
        (root / f"terminal-{columns}.log").write_text(output)
        return terminal, output, reordered

    def check_files(directory, mirror=False):
        for i in range(7):
            repo = "one" if i % 2 == 0 else "two"
            original = (root / repo / f"{names[i]}-1.pkg").read_bytes()
            files = list(directory.glob(f"{names[i]}-1*.pkg"))
            assert files, (i, list(directory.iterdir()))
            for file in files:
                assert file.read_bytes() == original, file
            if not mirror:
                assert (directory / f"{names[i]}-1.pkg").is_symlink()

    def check_columns(output, columns, numbered=True):
        output = re.sub(r"\033\[[0-9]*[AJK]", "", output)
        prefix = r"\[\d+/7\] " if numbered else ""
        short = long = False
        for line in output.splitlines():
            if not re.match(prefix + r"Fetching ", line):
                continue
            match = re.fullmatch(
                "(" + prefix + r"Fetching )([^:]+):( +)([ 0-9]{3})% (.{9}) (.{10}) (.{11})",
                line)
            assert match, repr(line)
            label, name, gap, percent, size, speed, duration = match.groups()
            assert name in {name + "-1" for name in names}, repr(line)
            if len(line) < columns:
                short = True
                assert len(line) == columns - 1, repr(line)
                assert match.start(4) == columns - 1 - 37, repr(line)
            else:
                long = True
                assert gap == " ", repr(line)
            # Values and units retain the existing representations.
            assert re.fullmatch(r"\s*\d+ (?:[KMGTPE]i)?B", size), size
            assert re.fullmatch(r"\s*\d+\.\d +(?:B|kB|MB|GB|TB)/s", speed), speed
        assert short and long, output

    try:
        (root / "db").mkdir()
        for name in ("one", "two"):
            (root / name).mkdir()
        config()
        for i in range(7):
            manifest = {"name": names[i], "origin": f"test/parallel{i}",
                        "version": "1", "maintainer": "test", "categories": ["test"],
                        "comment": "parallel fetch test", "www": "http://test",
                        "prefix": "/usr/local", "abi": "*",
                        # Keep archives large enough for intermediate read ticks.
                        "desc": os.urandom(16384).hex()}
            path = root / f"parallel{i}.json"
            path.write_text(json.dumps(manifest))
            run("create", "-o", "one" if i % 2 == 0 else "two", "-M", str(path))
        for name in ("one", "two"):
            run("repo", name)
        run("update")
        verbose = run("-vv").stdout
        assert "fetch_workers" not in verbose

        for workers, expected in ((None, 3), (1, 1), (2, 2), (5, 5), (999, 7)):
            clear_cache()
            config(workers)
            state.reset(first_wave=expected, refill=1 < expected < 7)
            result = run("fetch", "-U", "-y", "-a")
            assert "\033" not in result.stdout, result.stdout
            if expected >= 1:
                numbers = re.findall(r"\[(\d+)/7\] Fetching .*: 100%", result.stdout)
                assert numbers == [str(n) for n in range(1, 8)], result.stdout
            assert state.maximum == expected, (workers, state.maximum)
            assert len(state.requests) == 7, state.requests
            assert len({r["path"].split("/")[1] for r in state.requests}) == 2
            for request in state.requests:
                repo = request["path"].split("/")[1]
                assert request["agent"] == "parallel-" + repo, request
            if 1 < expected < 7:
                assert state.requests[expected]["start"] < state.requests[0]["end"]
            check_files(root / "cache")
            print(f"workers={workers}: observed {expected} concurrent downloads")

        clear_cache()
        config()
        state.reset()
        state.hold = True
        terminal, output, reordered = terminal_fetch(reorder=True)
        assert reordered, output
        completed = terminal.downloads()
        assert [n for n, _, _ in completed] == list(range(1, 8)), completed
        assert all(percent == 100 for _, _, percent in completed), completed
        assert len({name for _, name, _ in completed}) == 7, completed
        assert "Downloading 7 packages" not in output
        assert " ETA" in output and "B/s" in output, output
        check_columns(output, 160)
        check_files(root / "cache")
        print("terminal: three active rows; third completion renumbers earlier rows")

        clear_cache()
        state.reset()
        terminal, output, _ = terminal_fetch(columns=80)
        assert len(terminal.downloads()) == 7, output
        check_columns(output, 80)
        # Wrapped lines must not corrupt completed rows or leave stale copies.
        assert {name for _, name, _ in terminal.downloads()} == {
            name + "-1" for name in names}, output
        print("terminal: aligned columns and full wrapped names passed")

        config(1)
        for columns in (80, 160):
            clear_cache()
            state.reset()
            terminal, output, _ = terminal_fetch(columns=columns)
            completed = terminal.downloads()
            assert [n for n, _, _ in completed] == list(range(1, 8)), completed
            assert all(percent == 100 for _, _, percent in completed), completed
            assert {name for _, name, _ in completed} == {
                name + "-1" for name in names}, output
            check_columns(output, columns)
            check_files(root / "cache")
            assert state.maximum == 1, state.maximum
        clear_cache()
        state.reset()
        _, output, _ = terminal_fetch(160, False, "-q")
        assert not output, output
        print("terminal: serial rows share aligned columns and wrapped names")

        config()
        clear_cache()
        state.reset()
        _, output, _ = terminal_fetch(160, False, "-q")
        assert not output, output
        print("terminal: narrow display and quiet mode passed")

        clear_cache()
        state.reset("checksum")
        terminal, output, _ = terminal_fetch(ok=False)
        assert "checksum" in output.lower(), output
        assert not terminal.downloads(), terminal.downloads()
        print("terminal: failed packages leave no successful completion rows")

        for completed in (0, 6):
            clear_cache()
            state.reset()
            state.hold = True
            terminal_fetch(ok=False, cancel_after=completed)
        print("terminal: abort preserves active rows, including the last download")

        # Both repositories share one host budget, even with different URL paths.
        for workers, default, overrides, expected in (
                (3, None, {}, 1),
                (3, 1, {}, 1),
                (5, 2, {}, 2),
                (2, 5, {}, 2),
                (5, 1, {"127.0.0.1": {"fetch_workers": 3}}, 3),
                (3, 1, {"127.0.0.1.": {"fetch_workers": 2}}, 2)):
            clear_cache()
            config(workers, default, overrides)
            state.reset(first_wave=expected, refill=expected > 1)
            run("fetch", "-U", "-y", "-a")
            assert state.maximum == expected, (workers, default, overrides, state.maximum)
            assert len(state.requests) == 7, state.requests
            check_files(root / "cache")
            print(f"global={workers}, host default={default}, overrides={overrides}: {expected}")

        # Discover two FQDNs for each repository through an HTTP mirror list.
        clear_cache()
        config(3, 1)
        conf = json.loads((root / "pkg.conf").read_text())
        for repo in conf["repositories"].values():
            repo["mirror_type"] = "http"
        (root / "pkg.conf").write_text(json.dumps(conf))
        state.reset(first_wave=2)
        state.mirrors = [base + "/", f"http://localhost:{server.server_port}/"]
        run("fetch", "-U", "-y", "-a")
        assert state.maximum == 2, state.requests
        assert state.maximum_by_host == {"127.0.0.1": 1, "localhost": 1}
        check_files(root / "cache")
        print("hosts: HTTP mirrors run in parallel and share limits across repos")

        # A broken host is removed for every repository, leaving the other mirror.
        clear_cache()
        state.reset()
        state.mirrors = [base + "/", f"http://localhost:{server.server_port}/"]
        state.reply = (503, "10")
        run("fetch", "-U", "-y", "-a")
        assert sum(r["host"] == "127.0.0.1" for r in state.requests) == 1, state.requests
        check_files(root / "cache")
        print("hosts: failing mirrors are excluded across repositories")

        # All redirects converge on localhost: its shared limit must still be one.
        clear_cache()
        config(3, 1)
        state.reset()
        state.redirect = f"http://localhost:{server.server_port}"
        run("fetch", "-U", "-y", "-a")
        assert state.maximum_by_host["localhost"] == 1, state.requests
        assert sum(r["host"] == "localhost" for r in state.requests) == 7
        check_files(root / "cache")
        print("hosts: cross-host redirects obtain destination reservations")

        clear_cache()
        state.reset()
        state.redirect = lambda path: "./" + path.rsplit("/", 1)[-1]
        state.redirect_once = True
        run("fetch", "-U", "-y", "-a")
        assert len(state.requests) == 14, state.requests
        assert state.maximum == 1, state.requests
        check_files(root / "cache")
        print("hosts: relative redirects retain URL paths and host limits")

        # Retry-After must block both repositories, then permit one recovery probe.
        for status, date in ((429, False), (503, True)):
            clear_cache()
            config(3, 1)
            conf = json.loads((root / "pkg.conf").read_text())
            conf["FETCH_RETRY"] = 3
            (root / "pkg.conf").write_text(json.dumps(conf))
            state.reset()
            state.reply = (status, formatdate(time.time() + 3, usegmt=True) if date else "2")
            state.reply_once = True
            run("fetch", "-U", "-y", "-a")
            first, second = state.requests[:2]
            assert second["start"] - first["end"] >= (1.8 if not date else 1.5), state.requests
            assert state.maximum == 1, state.requests
            check_files(root / "cache")
        print("hosts: Retry-After seconds and HTTP dates coordinate recovery")

        # TCP/SSH-style streams also report transport failures to the host pool.
        connections = []

        class BrokenStream(socketserver.StreamRequestHandler):
            def handle(self):
                self.request.settimeout(5)
                self.wfile.write(b"ok: test\n")
                self.wfile.flush()
                while True:
                    command = self.rfile.readline().split()
                    if not command or command[0] == b"quit":
                        return
                    assert len(command) == 3 and command[0] == b"get", command
                    path = root / command[1].decode().lstrip("/")
                    if path.name.startswith("parallel") and path.suffix == ".pkg":
                        connections.append(1)
                        self.wfile.write(b"ok: 1\n")
                        self.wfile.flush()
                        # Close before delivering the promised package payload.
                        return
                    if path.is_file():
                        content = path.read_bytes()
                        self.wfile.write(f"ok: {len(content)}\n".encode())
                        self.wfile.write(content)
                    else:
                        self.wfile.write(b"ko: file not found\n")
                    self.wfile.flush()

        tcp = socketserver.ThreadingTCPServer(("127.0.0.1", 0), BrokenStream)
        threading.Thread(target=tcp.serve_forever, daemon=True).start()
        try:
            clear_cache()
            config(3, 1)
            conf = json.loads((root / "pkg.conf").read_text())
            for name, repo in conf["repositories"].items():
                repo["url"] = f"tcp://127.0.0.1:{tcp.server_address[1]}/{name}"
            (root / "pkg.conf").write_text(json.dumps(conf))
            # Build catalogs through pkg rather than depending on Python's
            # optional SQLite extension or the repository database layout.
            run("update", "-f")
            result = run("fetch", "-U", "-y", "-a", ok=False)
            assert len(connections) == 1, (connections, result.stdout, result.stderr)
        finally:
            tcp.shutdown()
            tcp.server_close()
            config()
            run("update", "-f")
        print("hosts: broken TCP streams suspend the shared host")

        for overrides in ({"http://127.0.0.1/": {"fetch_workers": 2}},
                          {"127.0.0.1:80": {"fetch_workers": 2}},
                          {"localhost": {}, "LOCALHOST.": {}},
                          {"127.0.0.1": {"unknown": 2}}):
            clear_cache()
            config(3, hosts=overrides)
            state.reset()
            result = run("fetch", "-U", "-y", "-a", ok=False)
            assert "FETCH_HOSTS" in result.stderr, result.stderr
            assert not state.requests

        for invalid in (0, -1, "two", True):
            clear_cache()
            config(3, hosts={"127.0.0.1": {"fetch_workers": invalid}})
            state.reset()
            result = run("fetch", "-U", "-y", "-a", ok=False)
            assert "FETCH_HOSTS" in result.stderr, result.stderr
            assert not state.requests
        config()
        run("fetch", "-U", "-y", "-a")

        state.reset()
        run("fetch", "-U", "-y", "-a")
        assert not state.requests, "valid cached packages were downloaded again"

        # Resume one partial archive while the other six remain cached.
        config()
        partial = next(p for p in (root / "cache").glob("parallel0-1*.pkg")
                       if not p.is_symlink())
        offset = partial.stat().st_size // 2
        with partial.open("r+b") as file:
            file.truncate(offset)
        state.reset()
        run("fetch", "-U", "-y", "-a")
        assert len(state.requests) == 1, state.requests
        assert state.requests[0]["range"] == f"bytes={offset}-", state.requests
        check_files(root / "cache")
        print("resume: only the missing bytes were requested")

        config()
        clear_cache()
        state.reset()
        run("install", "-n", "-U", "-y", "parallel0", "parallel1")
        assert not state.requests, "dry-run downloaded packages"
        state.reset(first_wave=3)
        run("fetch", "-U", "-y", "-a", "-o", str(root / "mirror"))
        assert state.maximum == 3
        check_files(root / "mirror", mirror=True)

        for failure in ("missing", "checksum"):
            clear_cache()
            state.reset(failure)
            result = run("fetch", "-U", "-y", "-a", ok=False)
            assert result.stderr, (failure, result.stdout)
            print(f"{failure}: download correctly failed")

        clear_cache()
        state.reset()
        config(0)
        result = run("fetch", "-U", "-y", "-a", ok=False)
        assert "FETCH_WORKERS must be greater than zero" in result.stderr
        assert not state.requests

        # Interrupt the parent alone; it must terminate and reap its workers.
        config()
        state.reset()
        state.hold = True
        process = subprocess.Popen([pkg, "-C", str(root / "pkg.conf"),
                                    "fetch", "-U", "-y", "-a"], env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            with state.lock:
                assert state.lock.wait_for(lambda: len(state.requests) >= 3, timeout=10)
            owner, children = download_workers(process.pid)
            os.kill(owner, signal.SIGINT)
            process.communicate(timeout=10)
            assert process.returncode != 0
            check_workers_reaped(children)
        finally:
            with state.lock:
                state.hold = False
                state.lock.notify_all()
            if process.poll() is None:
                process.kill()
                process.communicate()
        print("cancellation: all workers reaped")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
