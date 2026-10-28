#!/usr/bin/env python3
# Tier B Python 策略的受限执行沙箱（子进程）。
#
# 协议（父进程 <-> 本进程，均为 JSON 行）：
#   父 -> 子：第一行是 ctx；之后每个动作一行响应
#   子 -> 父：{"action": "eval", ...} / {"action": "done", ...} 等
#
# 安全措施：
# - 以 `python -I -B` 启动（不使用用户 site-packages / 环境变量）
# - 禁止网络、子进程、ctypes
# - 禁止在临时目录之外写文件
# - RLIMIT_AS / RLIMIT_CPU / RLIMIT_NOFILE

import importlib.util
import inspect
import json
import os
import resource
import sys
import tempfile
import traceback

WORKDIR = os.path.realpath(os.getcwd())
TEMP_ROOT = os.path.realpath(tempfile.gettempdir())

ALLOWED_WRITE_DIRS = (WORKDIR, TEMP_ROOT)

DENY_EVENT_PREFIXES = (
    "socket.",
    "subprocess.",
    "ctypes.",
    "shutil.",
    "os.exec",
    "os.spawn",
    "os.fork",
    "os.posix_spawn",
)

DENY_EVENTS = {
    "os.system",
    "os.remove",
    "os.rename",
    "os.replace",
    "os.mkdir",
    "os.rmdir",
    "os.chmod",
    "os.chown",
    "os.link",
    "os.symlink",
    "os.truncate",
    "os.utime",
    "os.chdir",
    "os.putenv",
    "os.unsetenv",
    "webbrowser.open",
}

BLACKLIST_MODULES = {
    "ctypes",
    "socket",
    "subprocess",
    "multiprocessing",
    "asyncio",
    "http",
    "urllib",
    "ftplib",
    "smtplib",
    "telnetlib",
    "pty",
    "shutil",
}

WRITE_FLAGS = (
    os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC | os.O_APPEND
)


def _is_under(path, root):
    path = os.path.realpath(os.fspath(path))
    return path == root or path.startswith(root + os.sep)


def _audit_hook(event, args):
    if event == "import":
        module = args[0] if args else ""
        if module and module.split(".")[0] in BLACKLIST_MODULES:
            raise PermissionError(f"module import blocked in strategy sandbox: {module}")
        return

    if event == "open":
        path = args[0] if args else None
        mode = args[1] if len(args) > 1 else None
        flags = args[2] if len(args) > 2 else 0
        writing = False
        if isinstance(mode, str) and any(c in mode for c in "wax+"):
            writing = True
        if isinstance(flags, int) and (flags & WRITE_FLAGS):
            writing = True
        if writing and path is not None:
            if not any(_is_under(path, root) for root in ALLOWED_WRITE_DIRS):
                raise PermissionError(f"file write blocked in strategy sandbox: {path}")
        return

    if event in DENY_EVENTS:
        raise PermissionError(f"operation blocked in strategy sandbox: {event}")
    for prefix in DENY_EVENT_PREFIXES:
        if event.startswith(prefix):
            raise PermissionError(f"operation blocked in strategy sandbox: {event}")


def _install_limits():
    try:
        resource.setrlimit(resource.RLIMIT_AS, (1 << 30, 1 << 30))
    except (ValueError, OSError):
        pass
    try:
        resource.setrlimit(resource.RLIMIT_CPU, (300, 300))
    except (ValueError, OSError):
        pass
    try:
        resource.setrlimit(resource.RLIMIT_NOFILE, (64, 64))
    except (ValueError, OSError):
        pass
    sys.dont_write_bytecode = True
    sys.addaudithook(_audit_hook)


def _load_strategy(path):
    if not os.path.isfile(path):
        raise FileNotFoundError(path)
    spec = importlib.util.spec_from_file_location("perci_user_strategy", path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load strategy: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    run = getattr(module, "run", None)
    if not callable(run):
        raise AttributeError("strategy must define a callable run(ctx)")
    return run


def main():
    if len(sys.argv) < 2:
        print("usage: strategy_sandbox.py <strategy.py>", file=sys.stderr)
        return 2
    strategy_path = sys.argv[1]

    # 协议通道：先复制 fd，再把 stdout/stdin 与协议分离
    proto_in = os.fdopen(os.dup(0), "r", buffering=1)
    proto_out = os.fdopen(os.dup(1), "w", buffering=1)
    os.dup2(2, 1)
    sys.stdin = open(os.devnull, "r")
    sys.stdout = sys.stderr

    def send(payload):
        proto_out.write(json.dumps(payload) + "\n")
        proto_out.flush()

    ctx_line = proto_in.readline()
    if not ctx_line:
        send({"action": "error", "message": "missing context"})
        return 2
    try:
        ctx = json.loads(ctx_line)
    except json.JSONDecodeError as exc:
        send({"action": "error", "message": f"invalid context: {exc}"})
        return 2

    _install_limits()

    try:
        run = _load_strategy(strategy_path)
        result = run(ctx)
    except Exception as exc:
        traceback.print_exc()
        send({"action": "error", "message": f"{type(exc).__name__}: {exc}"})
        return 1

    if result is None:
        send({"action": "done", "reason": "completed"})
        return 0
    if not inspect.isgenerator(result):
        send({
            "action": "error",
            "message": "run(ctx) must be a generator (use yield for actions)",
        })
        return 1

    response = None
    try:
        while True:
            try:
                action = result.send(response)
            except StopIteration:
                send({"action": "done", "reason": "completed"})
                return 0
            except Exception as exc:
                traceback.print_exc()
                send({"action": "error", "message": f"{type(exc).__name__}: {exc}"})
                return 1

            if not isinstance(action, dict) or "action" not in action:
                send({
                    "action": "error",
                    "message": "each yield must be an object with an 'action' key",
                })
                return 1
            if action["action"] in ("done", "error"):
                send(action)
                return 0

            send(action)
            line = proto_in.readline()
            if not line:
                return 0
            try:
                response = json.loads(line)
            except json.JSONDecodeError as exc:
                send({"action": "error", "message": f"invalid response: {exc}"})
                return 1
            if isinstance(response, dict) and response.get("terminal"):
                return 0
    finally:
        try:
            result.close()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
