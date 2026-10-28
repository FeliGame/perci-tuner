#!/usr/bin/env python3
"""测试用的 mock opencode 服务（HTTP + SSE）。

模拟 `opencode serve` 的启动输出与最小 API 子集：
  POST /api/session                     创建会话
  GET  /api/event                       事件流（SSE）
  POST /api/session/{sid}/prompt        发送 prompt，随后流式回放回答
  GET  /api/info                        服务信息

回答内容来自 MOCK_OPENCODE_DIR 目录：
  - answers/1.txt, answers/2.txt, ...  依次消费（用 counter 记录次数）
  - 或者单个 answer.txt
回答按小片段通过 session.text.delta 流式发出，最后发 text.ended 与
execution.succeeded，用于验证客户端的实时输出与完成判定。

认证：Basic opencode:<password>，password 由启动时随机生成并打印。
"""

import base64
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PASSWORD = "mock-password"
STREAM_DELAY = float(os.environ.get("MOCK_OPENCODE_STREAM_DELAY", "0.02"))
CHUNK = int(os.environ.get("MOCK_OPENCODE_CHUNK", "12"))
DATA_DIR = os.environ.get(
    "MOCK_OPENCODE_DIR", os.path.dirname(os.path.abspath(__file__))
)

_clients = []
_clients_lock = threading.Lock()
_counter_lock = threading.Lock()


def _next_answer():
    answers = os.path.join(DATA_DIR, "answers")
    if os.path.isdir(answers):
        counter_path = os.path.join(DATA_DIR, "counter")
        with _counter_lock:
            n = 1
            if os.path.exists(counter_path):
                with open(counter_path) as fh:
                    n = int(fh.read().strip() or "1")
            with open(counter_path, "w") as fh:
                fh.write(str(n + 1))
        path = os.path.join(answers, f"{n}.txt")
        if not os.path.exists(path):
            path = os.path.join(answers, "1.txt")
        with open(path) as fh:
            return fh.read()
    with open(os.path.join(DATA_DIR, "answer.txt")) as fh:
        return fh.read()


def _emit(event):
    payload = "data: " + json.dumps(event, ensure_ascii=False) + "\n\n"
    data = payload.encode()
    with _clients_lock:
        dead = []
        for queue in _clients:
            try:
                queue.append(data)
            except Exception:
                dead.append(queue)
        for queue in dead:
            _clients.remove(queue)


def _stream_answer(session_id, has_prompt):
    text = _next_answer()
    if has_prompt:
        _emit({"type": "session.reasoning.delta",
               "data": {"sessionID": session_id, "delta": "思考中"}})
        time.sleep(STREAM_DELAY)
    for start in range(0, len(text), CHUNK):
        _emit({"type": "session.text.delta",
               "data": {"sessionID": session_id, "delta": text[start:start + CHUNK]}})
        time.sleep(STREAM_DELAY)
    _emit({"type": "session.text.ended",
           "data": {"sessionID": session_id, "text": text}})
    _emit({"type": "session.execution.succeeded",
           "data": {"sessionID": session_id}})


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def _authorized(self):
        header = self.headers.get("Authorization", "")
        expected = "Basic " + base64.b64encode(
            f"opencode:{PASSWORD}".encode()).decode()
        if header != expected:
            path = os.environ.get("MOCK_OPENCODE_LOG")
            if path:
                with _counter_lock:
                    with open(path, "a") as fh:
                        fh.write(f"#auth-fail got={header!r} want={expected!r}\n")
                        fh.write("#headers " + repr(dict(self.headers)) + "\n")
        return header == expected

    def _json(self, status, obj):
        body = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _log_request(self):
        path = os.environ.get("MOCK_OPENCODE_LOG")
        if not path:
            return
        with _counter_lock:
            with open(path, "a") as fh:
                fh.write(self.command + " " + self.path + "\n")

    def do_GET(self):
        self._log_request()
        if not self._authorized():
            return self._json(401, {"_tag": "UnauthorizedError"})
        if self.path == "/api/info":
            return self._json(200, {"version": "mock", "urls": []})
        if self.path == "/api/event":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            queue = []
            with _clients_lock:
                _clients.append(queue)
            try:
                self.wfile.write(
                    b"data: {\"type\": \"server.connected\", \"data\": {}}\n\n")
                self.wfile.flush()
                while True:
                    if queue:
                        self.wfile.write(queue.pop(0))
                        self.wfile.flush()
                    else:
                        time.sleep(0.01)
            except (BrokenPipeError, ConnectionResetError):
                pass
            finally:
                with _clients_lock:
                    if queue in _clients:
                        _clients.remove(queue)
            return None
        return self._json(404, {"_tag": "NotFound"})

    def do_POST(self):
        self._log_request()
        if not self._authorized():
            return self._json(401, {"_tag": "UnauthorizedError"})
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b"{}"
        try:
            body = json.loads(raw or b"{}")
        except json.JSONDecodeError:
            body = {}
        if self.path == "/api/session":
            return self._json(200, {"data": {"id": "ses_mock"}})
        if self.path.startswith("/api/session/") and self.path.endswith("/prompt"):
            session_id = self.path.split("/")[3]
            self._json(200, {"data": {"accepted": True}})
            threading.Thread(
                target=_stream_answer,
                args=(session_id, bool(body.get("text"))),
                daemon=True,
            ).start()
            return None
        return self._json(404, {"_tag": "NotFound"})


def main(argv):
    port = 0
    host = "127.0.0.1"
    args = list(argv[1:])
    while args:
        arg = args.pop(0)
        if arg == "--port" and args:
            port = int(args.pop(0))
        elif arg == "--hostname" and args:
            host = args.pop(0)
    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    actual = server.server_address[1]
    print(f"server listening on http://{host}:{actual}", flush=True)
    print(f"server password {PASSWORD}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main(sys.argv)
