#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Perci-Tuner Agent Skill shell.

A single-file, dependency-free CLI that lets an OpenAI-compatible chat model
drive perci-tuner through natural language, with streaming output and
interactive confirmations before expensive runs.
"""

from __future__ import annotations

import argparse
import csv
import difflib
import hashlib
import json
import math
import os
import re
import shutil
import signal
import subprocess
import sys
import textwrap
import threading
import time
import unicodedata
import urllib.error
import urllib.request
from collections import deque
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path

VERSION = "0.2.0"
SCRIPT_DIR = Path(__file__).resolve().parent
# Resolve project root robustly — SCRIPT_DIR may be the root itself,
# or a subdir like skills/perci-tuner/. Walk up until we find scripts/run.sh.
def _find_project_root(start: Path) -> Path:
    for candidate in (start, *start.parents):
        if (candidate / "scripts" / "run.sh").exists():
            return candidate
    return start
PROJECT_ROOT = _find_project_root(SCRIPT_DIR)
# SKILL.md may live at PROJECT_ROOT/SKILL.md or PROJECT_ROOT/skills/perci-tuner/SKILL.md
_SKILL_CANDIDATES = [
    PROJECT_ROOT / "SKILL.md",
    PROJECT_ROOT / "skills" / "perci-tuner" / "SKILL.md",
]
SKILL_FILE = next((c for c in _SKILL_CANDIDATES if c.exists()), _SKILL_CANDIDATES[0])
LEGACY_CONFIG_FILE = PROJECT_ROOT / "skills" / "perci-tuner" / "api.json"
XDG_CONFIG_HOME = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
AGENT_CONFIG_DIR = XDG_CONFIG_HOME / "perci-agent"
AGENT_CONFIG_FILE = AGENT_CONFIG_DIR / "perci-agent.jsonc"

DEFAULT_CONFIG = {
    "base_url": "http://192.168.139.3:1234/v1",
    "api_key": "lm-studio",
    "model": "qwen3.6-35b-a3b-splash",
    "temperature": 0.2,
    "max_tokens": 4096,
    "request_timeout": 240,
    "show_reasoning": False,
    "max_tool_rounds": 16,
    "confirm_tools": True,
    "verbose_tool_output": True,
    "headers": {},
}

CONFIG_SCHEMA_URL = "https://perci-tuner.github.io/schemas/perci-agent.json"

DEFAULT_JSONC = """// perci-agent config — JSONC (comments allowed)
// 与 opencode 相同：可用 provider 块或直接 base_url + model + api_key

{
  "$schema": "%(schema)s",

  // provider 块（opencode 风格）
  "provider": {
    "lmstudio": {
      "name": "LM Studio",
      "options": {
        "baseURL": "http://192.168.139.3:1234/v1"
      },
      "models": {
        "qwen/qwen3.6-35b-a3b-splash": {
          "name": "Qwen3.6 35B A3B Splash"
        }
      }
    }
  },
  "model": "lmstudio/qwen/qwen3.6-35b-a3b-splash",
  // 或者直接用 flat 字段（provider 优先）
  // "base_url": "http://192.168.139.3:1234/v1",
  // "model": "qwen3.6-35b-a3b-splash",
  // "api_key": "lm-studio",

  // 运行选项
  "temperature": 0.2,
  "max_tokens": 4096,
  "request_timeout": 240,
  "max_tool_rounds": 16,
  "confirm_tools": true,
  "verbose_tool_output": true
}
""" % {"schema": CONFIG_SCHEMA_URL}

MODES = ("print-config", "search-optimize", "change-by-config", "check")
ALGORITHMS = ("dd", "pedd", "hidd", "exhaust")
TIME_MODES = ("program", "kernel")

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
_C_LINE_COMMENT = re.compile(r"//[^\n]*")
_C_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)


def strip_jsonc(text: str) -> str:
    """Remove // ... and /* ... */ comments from a JSONC string."""
    out_lines = []
    for raw_line in text.split("\n"):
        line = raw_line
        quote_index = None
        for match in re.finditer(r"\"", line):
            if quote_index is None:
                quote_index = match.start()
            else:
                quote_index = None
        # crude: strip line comments only outside quoted strings
        stripped = ""
        i = 0
        in_string = False
        while i < len(line):
            ch = line[i]
            if ch == "\\" and in_string and i + 1 < len(line):
                stripped += line[i : i + 2]
                i += 2
                continue
            if ch == '"':
                in_string = not in_string
                stripped += ch
                i += 1
                continue
            if not in_string and line[i : i + 2] == "//":
                break
            stripped += ch
            i += 1
        out_lines.append(stripped.rstrip())
    stripped_text = "\n".join(out_lines)
    # strip block comments (simplified, non-nested)
    stripped_text = _C_BLOCK_COMMENT.sub("", stripped_text)
    return stripped_text


# ---------------------------------------------------------------------------
# Path fuzzy resolution
# ---------------------------------------------------------------------------

class PathResolver:
    """Resolve user-given paths against a root, with fuzzy name matching."""

    def __init__(self, root: Path):
        self.root = root
        self._cache: dict[str, list[Path]] = {}

    def resolve(self, raw: str) -> list[Path]:
        """Return all Paths that match ``raw``; empty list on no match."""
        raw = str(raw).strip()
        if not raw:
            return []
        path = Path(raw).expanduser()
        if path.is_absolute():
            if path.exists():
                return [path]
            return self._fuzzy(path)
        candidate = Path.cwd() / path
        if candidate.exists():
            return [candidate]
        candidate = self.root / path
        if candidate.exists():
            return [candidate]
        return self._fuzzy(path)

    def _fuzzy(self, needle: Path) -> list[Path]:
        name = needle.name or ""
        if len(name) < 3:
            return []
        cache_key = name
        if cache_key not in self._cache:
            all_paths = list(self.root.rglob("*.c")) + list(self.root.rglob("*.cpp")) \
                + list(self.root.rglob("*.h")) + list(self.root.rglob("*.hpp")) \
                + list(self.root.rglob("*.bc")) + list(self.root.rglob("*.json")) \
                + [p for p in self.root.rglob("*") if p.is_dir()]
            self._cache[cache_key] = all_paths
        all_paths = self._cache[cache_key]
        scored: list[tuple[float, Path]] = []
        needle_lower = name.lower()
        for candidate in all_paths:
            cand_name = candidate.name
            cand_lower = cand_name.lower()
            if cand_lower == needle_lower:
                scored.append((2.0, candidate))
                continue
            if needle_lower in cand_lower or cand_lower in needle_lower:
                scored.append((1.0 + len(needle_lower) / max(1, len(cand_lower)), candidate))
                continue
            ratio = difflib.SequenceMatcher(None, needle_lower, cand_lower).ratio()
            if ratio >= 0.6:
                scored.append((ratio, candidate))
        scored.sort(key=lambda t: -t[0])
        # prefer source files, then dirs, then artifacts
        priority = (lambda p: 0 if p.suffix in (".c", ".cpp", ".h", ".hpp")
                    else 1 if p.is_dir() else 2)
        scored.sort(key=lambda t: priority(t[1]))
        return [p for _, p in scored[:8]]


# ---------------------------------------------------------------------------
# Session persistence
# ---------------------------------------------------------------------------

@dataclass
class SessionMeta:
    id: str
    title: str
    created_at: str
    updated_at: str
    model: str = ""


@dataclass
class PersistedSession:
    meta: SessionMeta
    messages: list[dict] = field(default_factory=list)
    executor_state: dict = field(default_factory=dict)
    usage: dict = field(default_factory=dict)  # prompt_tokens, completion_tokens, total_tokens, turns


class SessionManager:
    """Persist one JSON per session under ``root/.sessions``."""

    SESSIONS_DIRNAME = ".sessions"

    def __init__(self, root: Path):
        self.root = root
        self.dir = root / self.SESSIONS_DIRNAME
        self.dir.mkdir(parents=True, exist_ok=True)
        self.current_id: str | None = None

    # -- list / find -------------------------------------------------------

    def list_sessions(self) -> list[SessionMeta]:
        out: list[SessionMeta] = []
        for path in self.dir.glob("*.json"):
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                meta = data.get("meta") or {}
                out.append(SessionMeta(
                    id=meta.get("id") or path.stem,
                    title=meta.get("title") or path.stem,
                    created_at=meta.get("created_at", ""),
                    updated_at=meta.get("updated_at", ""),
                    model=meta.get("model", ""),
                ))
            except (OSError, json.JSONDecodeError):
                continue
        out.sort(key=lambda m: m.updated_at or "", reverse=True)
        return out

    def find(self, target: str) -> PersistedSession | None:
        """Find session by id (exact) or title (startswith/contains)."""
        target = target.strip()
        if not target:
            return None
        by_id = self.dir / f"{target}.json"
        if by_id.exists():
            return self._load(by_id)
        metas = self.list_sessions()
        for meta in metas:
            if meta.title == target or meta.id == target:
                return self._load(self.dir / f"{meta.id}.json")
        low = target.lower()
        for meta in metas:
            if low in meta.title.lower() or meta.title.lower().startswith(low):
                return self._load(self.dir / f"{meta.id}.json")
        return None

    def _load(self, path: Path) -> PersistedSession | None:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return None
        meta = data.get("meta") or {}
        return PersistedSession(
            meta=SessionMeta(
                id=meta.get("id") or path.stem,
                title=meta.get("title") or path.stem,
                created_at=meta.get("created_at", ""),
                updated_at=meta.get("updated_at", ""),
                model=meta.get("model", ""),
            ),
            messages=data.get("messages") or [],
            executor_state=data.get("executor_state") or {},
            usage=data.get("usage") or {},
        )

    # -- create / save / delete -------------------------------------------

    def create(self, title: str, model: str, system_prompt: str,
               executor_state: dict) -> PersistedSession:
        now = _now_iso()
        session_id = self._slug(title or "新会话", now)
        session = PersistedSession(
            meta=SessionMeta(
                id=session_id,
                title=title or f"新会话 {session_id[:8]}",
                created_at=now,
                updated_at=now,
                model=model,
            ),
            messages=[{"role": "system", "content": system_prompt}],
            executor_state=executor_state,
            usage={"prompt_tokens": 0, "completion_tokens": 0,
                   "total_tokens": 0, "turns": 0},
        )
        self._save(session)
        self.current_id = session_id
        return session

    def save(self, session: PersistedSession) -> None:
        session.meta.updated_at = _now_iso()
        self._save(session)

    def _save(self, session: PersistedSession) -> None:
        path = self.dir / f"{session.meta.id}.json"
        payload = {
            "meta": asdict(session.meta),
            "messages": session.messages,
            "executor_state": session.executor_state,
            "usage": session.usage,
        }
        tmp = path.with_suffix(".json.tmp")
        try:
            tmp.write_text(json.dumps(payload, ensure_ascii=False, indent=2),
                           encoding="utf-8")
            tmp.replace(path)
        except OSError:
            try:
                tmp.unlink()
            except OSError:
                pass
            raise

    def delete(self, session_id: str) -> bool:
        path = self.dir / f"{session_id}.json"
        if not path.exists():
            return False
        path.unlink()
        if self.current_id == session_id:
            self.current_id = None
        return True

    def rename(self, session_id: str, new_title: str) -> PersistedSession | None:
        session = self._load(self.dir / f"{session_id}.json")
        if session is None:
            return None
        session.meta.title = new_title.strip() or session.meta.title
        self.save(session)
        return session

    # -- helpers ---------------------------------------------------------

    @staticmethod
    def _slug(text: str, stamp: str) -> str:
        """Stable, readable session id."""
        # remove non-alnum (keep CJK & _ - .)
        cleaned = re.sub(r"[^\w\u4e00-\u9fff\-_.]+", "_", text).strip("_")
        cleaned = cleaned[:32] or "session"
        # 4-digit stamp + 4 hex of md5 on stamp
        h = hashlib.md5((stamp + cleaned).encode("utf-8")).hexdigest()[:4]
        ts = stamp.split("T")[0].replace("-", "")[2:]  # e.g. 261029
        return f"{ts}{h}-{cleaned[:16]}"


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


# ---------------------------------------------------------------------------
# Usage tracker
# ---------------------------------------------------------------------------

@dataclass
class UsageTracker:
    prompt_tokens: int = 0
    completion_tokens: int = 0
    total_tokens: int = 0
    turns: int = 0

    def add(self, usage: dict | None) -> None:
        if not isinstance(usage, dict):
            return
        self.prompt_tokens += int(usage.get("prompt_tokens") or 0)
        self.completion_tokens += int(usage.get("completion_tokens") or 0)
        self.total_tokens += int(usage.get("total_tokens") or
                                  (int(usage.get("prompt_tokens") or 0)
                                   + int(usage.get("completion_tokens") or 0)))
        self.turns += 1

    def as_dict(self) -> dict:
        return {
            "prompt_tokens": self.prompt_tokens,
            "completion_tokens": self.completion_tokens,
            "total_tokens": self.total_tokens,
            "turns": self.turns,
        }

    def delta_str(self, last_usage: dict | None) -> str:
        if not last_usage:
            return ""
        delta_in = int(last_usage.get("prompt_tokens") or 0)
        delta_out = int(last_usage.get("completion_tokens") or 0)
        delta_total = int(last_usage.get("total_tokens") or (delta_in + delta_out))
        return f"本轮 {delta_in}+{delta_out} ({delta_total})"



# ---------------------------------------------------------------------------
# Terminal helpers
# ---------------------------------------------------------------------------

def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def visible_width(text: str) -> int:
    width = 0
    for ch in strip_ansi(text):
        width += 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1
    return width


def wrap_text(text: str, width: int) -> list[str]:
    if not text:
        return [""]
    width = max(20, width)
    if not any(unicodedata.east_asian_width(ch) in ("W", "F") for ch in text):
        return textwrap.wrap(
            text,
            width=width,
            replace_whitespace=False,
            drop_whitespace=False,
            break_long_words=True,
            break_on_hyphens=False,
        ) or [""]
    lines, current, used = [], "", 0
    for ch in text:
        step = 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1
        if used + step > width and current:
            lines.append(current)
            current, used = ch, step
        else:
            current += ch
            used += step
    lines.append(current)
    return lines


class Palette:
    ACCENT = (108, 196, 255)
    ACCENT2 = (186, 146, 255)
    USER = (126, 222, 166)
    TOOL = (240, 190, 110)
    OK = (126, 222, 166)
    ERR = (255, 120, 120)
    DIM = (128, 138, 152)
    CODE = (150, 210, 210)


class UI:
    def __init__(self, color: bool = True):
        self.color = (color and os.environ.get("NO_COLOR") is None
                      and sys.stdout.isatty())
        try:
            self.width = max(56, min(shutil.get_terminal_size((100, 24)).columns, 108))
        except OSError:
            self.width = 96
        self.tty = sys.stdout.isatty()

    def paint(self, text: str, rgb=None, bold: bool = False, dim: bool = False) -> str:
        if not self.color or not text:
            return text
        codes = []
        if bold:
            codes.append("1")
        if dim:
            codes.append("2")
        if rgb:
            codes.append(f"38;2;{rgb[0]};{rgb[1]};{rgb[2]}")
        if not codes:
            return text
        return f"\x1b[{';'.join(codes)}m{text}\x1b[0m"

    def banner(self, model: str, root: Path) -> None:
        inside = min(self.width - 4, 80)
        lines = [
            self.paint("◆ perci-tuner", Palette.ACCENT, bold=True)
            + "  " + self.paint("混合精度自动调优 · Agent Skill", Palette.DIM),
            self.paint("model", Palette.DIM) + "  " + model,
            self.paint("root ", Palette.DIM) + "  " + str(root),
            self.paint("/help 查看命令 · /quit 退出", Palette.DIM),
        ]
        print()
        print("  " + self.paint("╭" + "─" * inside + "╮", Palette.DIM))
        for line in lines:
            pad = max(0, inside - 1 - visible_width(line))
            print("  " + self.paint("│", Palette.DIM) + " " + line + " " * pad
                  + self.paint("│", Palette.DIM))
        print("  " + self.paint("╰" + "─" * inside + "╯", Palette.DIM))
        print()

    def user_prompt(self) -> str:
        return "\n  " + self.paint("❯ ", Palette.ACCENT, bold=True)

    def error(self, message: str) -> None:
        print()
        print("  " + self.paint("✗ ", Palette.ERR, bold=True)
              + self.paint(message, Palette.ERR))

    def info(self, message: str) -> None:
        print("  " + self.paint(message, Palette.DIM))

    def tool_begin(self, name: str, args: dict) -> None:
        print()
        detail = format_tool_args(args)
        print("  " + self.paint("◆ ", Palette.TOOL, bold=True)
              + self.paint(name, bold=True)
              + ("  " + self.paint(detail, Palette.DIM) if detail else ""))

    def tool_log(self, line: str) -> None:
        print("    " + self.paint("│ ", Palette.DIM) + self.paint(line, Palette.DIM))

    def tool_end(self, ok: bool, seconds: float, note: str = "") -> None:
        icon = self.paint("✓", Palette.OK, bold=True) if ok else self.paint("✗", Palette.ERR, bold=True)
        text = f"完成 · {seconds:.1f}s" if ok else f"失败 · {seconds:.1f}s"
        tail = ("  " + self.paint(note, Palette.DIM)) if note else ""
        print("  " + icon + " " + self.paint(text, Palette.DIM) + tail)

    def panel(self, title: str, items: list[tuple[str, str]]) -> None:
        key_width = max([visible_width(k) for k, _ in items] + [8])
        value_width = max([visible_width(v) for _, v in items] + [10])
        inside = min(self.width - 4, key_width + value_width + 6)
        print("  " + self.paint("╭─ " + title + " ", Palette.ACCENT, bold=True)
              + self.paint("─" * max(0, inside - visible_width(title) - 4), Palette.DIM)
              + self.paint("╮", Palette.DIM))
        for key, value in items:
            pad = max(0, key_width - visible_width(key))
            print("  " + self.paint("│", Palette.DIM) + " "
                  + self.paint(key, Palette.DIM) + " " * (pad + 2) + value)
        print("  " + self.paint("╰" + "─" * inside + "╯", Palette.DIM))

    def confirm(self, question: str) -> bool:
        print()
        print("  " + self.paint("? ", Palette.TOOL, bold=True) + question)
        try:
            answer = input("  " + self.paint("[y/N] ", Palette.DIM)).strip().lower()
        except (EOFError, KeyboardInterrupt):
            print()
            return False
        return answer in ("y", "yes", "是", "确认")


def format_tool_args(args: dict) -> str:
    if not isinstance(args, dict):
        return ""
    parts = []
    for key in ("mode", "threshold", "algorithm"):
        value = args.get(key)
        if value not in (None, ""):
            parts.append(f"{key}={value}")
    sources = args.get("sources")
    if sources:
        names = [Path(str(p)).name for p in sources[:3]]
        more = f" +{len(sources) - 3}" if len(sources) > 3 else ""
        parts.append("sources=" + ",".join(names) + more)
    target = args.get("target_config")
    if target:
        parts.append(f"target={Path(str(target)).name}")
    return " · ".join(parts)


class Spinner:
    FRAMES = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏"

    def __init__(self, ui: UI, text: str = "思考中"):
        self.ui = ui
        self.text = text
        self._stop = False
        self._thread = None
        self._active = False

    def start(self) -> None:
        if not (self.ui.tty and self.ui.color):
            return
        self._active = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self) -> None:
        index = 0
        while not self._stop:
            frame = self.FRAMES[index % len(self.FRAMES)]
            sys.stdout.write("\r  " + self.ui.paint(frame, Palette.ACCENT) + " "
                             + self.ui.paint(self.text, Palette.DIM) + "   ")
            sys.stdout.flush()
            index += 1
            time.sleep(0.08)

    def stop(self) -> None:
        if not self._active:
            return
        self._stop = True
        if self._thread:
            self._thread.join(timeout=0.3)
        sys.stdout.write("\r" + " " * min(self.ui.width, 80) + "\r")
        sys.stdout.flush()
        self._active = False


class PlainStream:
    """Wrapped, single-color streaming text (used for reasoning)."""

    def __init__(self, ui: UI, prefix: str, rgb, dim: bool = True):
        self.ui = ui
        self.prefix = prefix
        self.rgb = rgb
        self.dim = dim
        self.buffer = ""
        self.first = True

    def feed(self, text: str) -> None:
        self.buffer += text
        while "\n" in self.buffer:
            line, self.buffer = self.buffer.split("\n", 1)
            self._line(line)

    def _line(self, line: str) -> None:
        for index, segment in enumerate(wrap_text(line, self.ui.width - 6)):
            prefix = self.prefix if self.first and index == 0 else "    "
            print(prefix + self.ui.paint(segment, self.rgb, dim=self.dim))
        self.first = False

    def close(self) -> None:
        if self.buffer:
            self._line(self.buffer)
            self.buffer = ""


class MarkdownStream:
    """Streaming renderer for assistant messages with light markdown styling."""

    TOKEN_RE = re.compile(r"(`[^`\n]+`)|(\*\*[^*\n]+\*\*)")

    def __init__(self, ui: UI):
        self.ui = ui
        self.buffer = ""
        self.first = True
        self.in_code = False

    def _inline(self, text: str) -> str:
        def repl(match: re.Match) -> str:
            if match.group(1):
                return self.ui.paint(match.group(1)[1:-1], Palette.CODE)
            return self.ui.paint(match.group(2)[2:-2], bold=True)

        text = self.TOKEN_RE.sub(repl, text)
        heading = re.match(r"^(#{1,6})\s+(.*)$", text)
        if heading:
            text = self.ui.paint(heading.group(2), Palette.ACCENT, bold=True)
        return text

    def feed(self, text: str) -> None:
        self.buffer += text
        while "\n" in self.buffer:
            line, self.buffer = self.buffer.split("\n", 1)
            self._line(line)

    def _line(self, line: str) -> None:
        if line.startswith("```"):
            self.in_code = not self.in_code
            if self.in_code:
                print("    " + self.ui.paint("┌" + "─" * 8, Palette.DIM))
            else:
                print("    " + self.ui.paint("└" + "─" * 8, Palette.DIM))
            self.first = False
            return
        if self.in_code:
            print("    " + self.ui.paint("│ ", Palette.DIM)
                  + self.ui.paint(line, Palette.CODE))
            self.first = False
            return
        segments = wrap_text(line, self.ui.width - 6)
        for index, segment in enumerate(segments):
            prefix = "  " + self.ui.paint("● ", Palette.ACCENT, bold=True) \
                if self.first and index == 0 else "    "
            print(prefix + self._inline(segment))
        self.first = False

    def close(self) -> None:
        if self.buffer:
            self._line(self.buffer)
            self.buffer = ""
        if not self.first:
            print()


# ---------------------------------------------------------------------------
# API client
# ---------------------------------------------------------------------------

class LLMError(RuntimeError):
    pass


def load_api_config(path: Path | None = None) -> tuple[dict, Path]:
    """Load config from jsonc (preferred) or legacy api.json. Returns (config, resolved_path)."""
    candidates: list[Path] = []
    if path is not None:
        candidates.append(path)
    candidates.append(AGENT_CONFIG_FILE)
    candidates.append(LEGACY_CONFIG_FILE)
    candidates.extend([Path(p).expanduser() for p in [
        "~/.config/perci-agent/perci-agent.jsonc",
        "~/.config/opencode/opencode.jsonc",
    ]])

    # Find first existing
    existing: Path | None = None
    for cand in candidates:
        if cand.exists():
            existing = cand
            break

    if existing is None:
        # If legacy exists but xdg doesn't, migrate
        if LEGACY_CONFIG_FILE.exists():
            return _migrate_legacy_and_load(), LEGACY_CONFIG_FILE
        # Create default jsonc
        AGENT_CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        AGENT_CONFIG_FILE.write_text(DEFAULT_JSONC, encoding="utf-8")
        existing = AGENT_CONFIG_FILE
        print(f"  已创建默认配置 {existing}", file=sys.stderr)

    try:
        text = existing.read_text(encoding="utf-8")
        raw = json.loads(strip_jsonc(text))
    except (OSError, json.JSONDecodeError) as exc:
        raise LLMError(f"无法读取 API 配置 {existing}: {exc}") from exc
    if not isinstance(raw, dict):
        raise LLMError(f"API 配置文件必须是 JSON 对象: {existing}")

    config = _flatten_opencode(raw)
    for key in ("temperature", "max_tokens", "request_timeout",
                "max_tool_rounds", "confirm_tools", "verbose_tool_output"):
        if key in raw and key not in config:
            config[key] = raw[key]

    if os.environ.get("PERCI_API_BASE"):
        config["base_url"] = os.environ["PERCI_API_BASE"]
    if os.environ.get("PERCI_API_KEY"):
        config["api_key"] = os.environ["PERCI_API_KEY"]
    if os.environ.get("PERCI_MODEL"):
        config["model"] = os.environ["PERCI_MODEL"]
    if not config.get("base_url"):
        raise LLMError("API 配置缺少 base_url（或 provider.<name>.options.baseURL）")
    if not config.get("model"):
        raise LLMError("API 配置缺少 model（或 provider.<name>.<model_name>）")
    if not config.get("api_key"):
        config["api_key"] = ""
    merged = dict(DEFAULT_CONFIG)
    merged.update(config)
    return merged, existing


def _flatten_opencode(raw: dict) -> dict:
    """Flatten opencode-style config into flat keys we consume."""
    flat: dict = {}
    providers = raw.get("provider") or {}
    model_spec = raw.get("model") or ""
    selected_provider = ""
    model_name = model_spec
    if "/" in model_spec:
        selected_provider, model_name = model_spec.split("/", 1)
    elif providers and len(providers) == 1:
        selected_provider = next(iter(providers))

    if selected_provider and selected_provider in providers:
        prov = providers[selected_provider] or {}
        if isinstance(prov, dict):
            options = prov.get("options") or {}
            if isinstance(options, dict):
                for key in ("baseURL", "base_url"):
                    if options.get(key):
                        flat["base_url"] = options[key]
                        break
            if prov.get("apiKey"):
                flat["api_key"] = prov["apiKey"]

    for key in ("base_url", "api_key", "model", "headers"):
        if raw.get(key) is not None and key not in flat:
            flat[key] = raw[key]

    if not flat.get("model") and model_name:
        flat["model"] = model_name
    return flat


def _migrate_legacy_and_load() -> dict:
    try:
        legacy = json.loads(LEGACY_CONFIG_FILE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise LLMError(f"无法读取旧版 api.json: {exc}") from exc
    AGENT_CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    new_text = DEFAULT_JSONC.replace(
        '"http://192.168.139.3:1234/v1"',
        json.dumps(legacy.get("base_url", "http://192.168.139.3:1234/v1")),
    )
    AGENT_CONFIG_FILE.write_text(new_text, encoding="utf-8")
    print(f"  已将旧 api.json 迁移到 {AGENT_CONFIG_FILE}", file=sys.stderr)
    merged = dict(DEFAULT_CONFIG)
    merged.update(legacy)
    return merged


class LLMClient:
    def __init__(self, config: dict):
        self.config = config
        self.url = str(config["base_url"]).rstrip("/") + "/chat/completions"
        self.timeout = float(config.get("request_timeout", 240))

    def _headers(self) -> dict:
        headers = {
            "Content-Type": "application/json",
            "Accept": "text/event-stream",
        }
        key = self.config.get("api_key")
        if key:
            headers["Authorization"] = f"Bearer {key}"
        extra = self.config.get("headers") or {}
        if isinstance(extra, dict):
            headers.update({str(k): str(v) for k, v in extra.items()})
        return headers

    def chat(self, messages: list, tools: list, ui: UI) -> dict:
        body = {
            "model": self.config["model"],
            "messages": messages,
            "stream": True,
            # Force usage in the final SSE chunk — some servers (LM Studio)
            # omit usage in streaming mode unless this is set.
            "stream_options": {"include_usage": True},
            "temperature": float(self.config.get("temperature", 0.2)),
            "max_tokens": int(self.config.get("max_tokens", 2048)),
        }
        if tools:
            body["tools"] = tools
            body["tool_choice"] = "auto"
        request = urllib.request.Request(
            self.url,
            data=json.dumps(body, ensure_ascii=False).encode("utf-8"),
            headers=self._headers(),
            method="POST",
        )

        stream = MarkdownStream(ui)
        reasoning = PlainStream(ui, "  " + ui.paint("✧ ", Palette.DIM), Palette.DIM)
        spinner = Spinner(ui)
        spinner.start()
        started = False
        content: list[str] = []
        reasoning_text: list[str] = []
        calls: dict[int, dict] = {}
        usage: dict | None = None

        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                ctype = response.headers.get("Content-Type", "")
                if "application/json" in ctype and "text/event-stream" not in ctype:
                    payload = json.loads(response.read().decode("utf-8", "replace"))
                    spinner.stop()
                    message = ((payload.get("choices") or [{}])[0] or {}).get("message") or {}
                    text = message.get("content") or ""
                    if text:
                        stream.feed(text)
                        stream.close()
                    reasoning_text = [message.get("reasoning_content") or ""]
                    usage = payload.get("usage")
                    result = self._assemble(message, text, reasoning_text, message.get("tool_calls"))
                    result["_usage"] = usage
                    return result

                for raw_line in response:
                    line = raw_line.decode("utf-8", "replace").strip()
                    if not line or line.startswith(":"):
                        continue
                    if not line.startswith("data:"):
                        continue
                    chunk = line[5:].strip()
                    if chunk == "[DONE]":
                        break
                    try:
                        obj = json.loads(chunk)
                    except json.JSONDecodeError:
                        continue
                    if isinstance(obj, dict) and obj.get("error"):
                        raise LLMError(f"API 返回错误: {obj['error']}")
                    if isinstance(obj, dict) and obj.get("usage"):
                        usage = obj["usage"]
                    choices = obj.get("choices") or []
                    if not choices:
                        continue
                    delta = choices[0].get("delta") or {}
                    piece = delta.get("content")
                    if piece:
                        if not started:
                            spinner.stop()
                            started = True
                        content.append(piece)
                        stream.feed(piece)
                    thought = delta.get("reasoning_content") or delta.get("reasoning")
                    if thought:
                        if not started:
                            spinner.stop()
                            started = True
                        reasoning_text.append(thought)
                        if self.config.get("show_reasoning"):
                            reasoning.feed(thought)
                    for tool_call in delta.get("tool_calls") or []:
                        self._merge_tool_call(calls, tool_call)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", "replace")
            raise LLMError(f"HTTP {exc.code} {self.url}\n{detail[:1200]}") from exc
        except urllib.error.URLError as exc:
            raise LLMError(f"无法连接 {self.url}: {exc.reason}") from exc
        except TimeoutError as exc:
            raise LLMError(f"请求超时（{self.timeout:.0f}s）: {self.url}") from exc
        finally:
            spinner.stop()
            if self.config.get("show_reasoning"):
                reasoning.close()

        if started:
            stream.close()
        merged_calls = self._finalize_tool_calls(calls)
        result = self._assemble({}, "".join(content), reasoning_text, merged_calls)
        result["_usage"] = usage
        return result

    @staticmethod
    def _merge_tool_call(calls: dict[int, dict], tool_call: dict) -> None:
        index = int(tool_call.get("index", 0))
        slot = calls.setdefault(index, {
            "id": "",
            "type": "function",
            "function": {"name": "", "arguments": ""},
        })
        if tool_call.get("id"):
            slot["id"] = tool_call["id"]
        function = tool_call.get("function") or {}
        name = function.get("name")
        if name and not slot["function"]["name"]:
            slot["function"]["name"] = name
        arguments = function.get("arguments")
        if arguments:
            slot["function"]["arguments"] += arguments

    @staticmethod
    def _finalize_tool_calls(calls: dict[int, dict]) -> list[dict]:
        result = []
        for index in sorted(calls):
            slot = calls[index]
            if not slot["id"]:
                slot["id"] = f"call_{index}"
            result.append(slot)
        return result

    @staticmethod
    def _assemble(message: dict, text: str, reasoning: list[str], tool_calls) -> dict:
        normalized_calls = []
        for call in tool_calls or []:
            if not isinstance(call, dict):
                continue
            function = call.get("function") or {}
            arguments = function.get("arguments")
            if isinstance(arguments, dict):
                arguments = json.dumps(arguments, ensure_ascii=False)
            normalized_calls.append({
                "id": call.get("id") or f"call_{len(normalized_calls)}",
                "type": "function",
                "function": {
                    "name": function.get("name") or "",
                    "arguments": arguments or "",
                },
            })
        assistant: dict = {"role": "assistant", "content": text or ""}
        if normalized_calls:
            assistant["tool_calls"] = normalized_calls
        return assistant


# ---------------------------------------------------------------------------
# Tool schemas
# ---------------------------------------------------------------------------

TOOL_SCHEMAS = [
    {
        "type": "function",
        "function": {
            "name": "inspect_source",
            "description": (
                "只读检查 C/C++ 源文件或目录：是否存在、是否含 main、"
                "#pragma precision error/region/range、__autofp、常用头文件和建议链接参数。"
                "不会编译，也不会修改文件。"
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "sources": {
                        "type": "array",
                        "items": {"type": "string"},
                        "description": "源文件或目录路径（相对项目根或绝对路径）",
                    }
                },
                "required": ["sources"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "run_perci",
            "description": (
                "调用项目根目录的 scripts/run.sh 执行 Perci-Tuner。"
                "search-optimize 会先刷新 config.json 再搜索；change-by-config 生成混合精度程序。"
                "执行前 CLI 会向用户请求确认。"
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "mode": {
                        "type": "string",
                        "enum": list(MODES),
                        "description": "执行模式",
                    },
                    "sources": {
                        "type": "array",
                        "items": {"type": "string"},
                        "description": "目标源文件/目录；缺省时沿用本会话上一次的源程序",
                    },
                    "threshold": {
                        "type": "string",
                        "description": "相对误差阈值，例如 1e-6。search-optimize 必须显式提供，或源码含 #pragma precision error",
                    },
                    "algorithm": {
                        "type": "string",
                        "enum": list(ALGORITHMS),
                        "description": "搜索算法，默认 dd",
                    },
                    "repeat": {"type": "integer", "description": "每个配置重复运行次数"},
                    "time_mode": {
                        "type": "string",
                        "enum": list(TIME_MODES),
                        "description": "program 计时整个程序，kernel 读取 core_time_sec",
                    },
                    "search_range": {
                        "type": "string",
                        "description": "可搜索精度位宽，逗号分隔，如 128,64,32,16（只能降低精度）",
                    },
                    "include_func": {"type": "string", "description": "只扫描这些函数，逗号分隔/通配"},
                    "exclude_func": {"type": "string", "description": "跳过这些函数"},
                    "include_file": {"type": "string", "description": "只扫描这些源文件"},
                    "exclude_file": {"type": "string", "description": "跳过这些源文件"},
                    "mxp": {"type": "boolean", "description": "只处理 pragma precision 与 __autofp 变量"},
                    "fp128todd": {"type": "boolean", "description": "把 fp128 当作 double-double"},
                    "check_result": {"type": "boolean", "description": "在可执行程序中打印相对误差"},
                    "shuffled": {"type": "boolean", "description": "打乱搜索顺序"},
                    "timeout": {"type": "number", "description": "运行/编译超时倍数，默认 1.5"},
                    "optimize_level": {"type": "string", "description": "编译优化级别，默认 -O2"},
                    "link_libs": {
                        "type": "array", "items": {"type": "string"},
                        "description": "链接库，如 -lm、-lgsl",
                    },
                    "link_dirs": {
                        "type": "array", "items": {"type": "string"},
                        "description": "库搜索目录，如 -L/usr/local/lib",
                    },
                    "include_dirs": {
                        "type": "array", "items": {"type": "string"},
                        "description": "头文件目录，如 -I/usr/include",
                    },
                    "run_args": {
                        "type": "array", "items": {"type": "string"},
                        "description": "传给目标程序运行参数",
                    },
                    "target_config": {
                        "type": "string",
                        "description": "change-by-config 的目标配置；缺省使用本会话最佳搜索配置",
                    },
                },
                "required": ["mode"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "summarize_results",
            "description": "读取 run_history.csv、最佳配置和产物路径，返回结构化性能摘要。",
            "parameters": {"type": "object", "properties": {}},
        },
    },
]


# ---------------------------------------------------------------------------
# Session state and tool execution
# ---------------------------------------------------------------------------

@dataclass
class SessionState:
    sources: list[str] = field(default_factory=list)
    threshold: str | None = None
    algorithm: str | None = None
    best_run: int | None = None
    last_search: dict = field(default_factory=dict)
    run_options: dict = field(default_factory=dict)
    search_approved: bool = False


PRAGMA_ERROR_RE = re.compile(
    r"#pragma\s+precision\s+error\s+(\w+)\s*(?:\(\s*([0-9.eE+-]+)\s*\))?"
)
PRAGMA_OTHER_RE = re.compile(r"#pragma\s+precision\s+(region|range)")
INCLUDE_RE = re.compile(r"#include\s+[<\"]([^>\"]+)[>\"]")
MAIN_RE = re.compile(r"\b(?:int|void)\s+main\s*\(")
COMPILE_ERROR_RE = re.compile(
    r"(fatal error:|: error:|error generated|undefined reference|undefined symbol|"
    r"collect2:|ld: |cannot find -l|No such file or directory)",
    re.IGNORECASE,
)
LIB_HINT = {
    "math.h": ["-lm"],
    "gsl/gsl_rng.h": ["-lgsl", "-lgslcblas"],
    "gsl/gsl_math.h": ["-lgsl", "-lgslcblas"],
    "fftw3.h": ["-lfftw3"],
    "cblas.h": ["-lcblas", "-lblas"],
    "lapacke.h": ["-llapacke", "-llapack", "-lblas"],
}

REUSE_KEYS = (
    "link_libs", "link_dirs", "include_dirs", "run_args",
    "mxp", "fp128todd", "check_result", "timeout", "optimize_level",
    "include_func", "exclude_func", "include_file", "exclude_file",
)


class ToolExecutor:
    def __init__(self, root: Path, ui: UI, config: dict, state: SessionState, auto_yes: bool):
        self.root = root
        self.ui = ui
        self.config = config
        self.state = state
        self.auto_yes = auto_yes
        self.run_script = root / "scripts" / "run.sh"
        self.path_resolver = PathResolver(root)

    # -- path helpers -------------------------------------------------------

    def _resolve_path(self, raw: str) -> Path | list[Path]:
        path = Path(str(raw)).expanduser()
        if path.is_absolute() and path.exists():
            return path
        # Try exact under root / cwd first
        for base in (Path.cwd(), self.root):
            cand = base / path
            if cand.exists():
                return cand
        # Fall back to fuzzy suggestions
        matches = self.path_resolver.resolve(str(raw))
        if len(matches) == 1:
            return matches[0]
        return matches  # list = ambiguous, caller decides

    def _resolve_sources(self, args: dict) -> tuple[list[str], list[dict]]:
        raw_sources = args.get("sources") or []
        if not isinstance(raw_sources, list):
            raw_sources = [raw_sources]
        if not raw_sources:
            raw_sources = list(self.state.sources)
        resolved, missing = [], []
        for raw in raw_sources:
            found = self._resolve_path(str(raw))
            if isinstance(found, list):
                if len(found) == 1:
                    found = found[0]
                elif not found:
                    missing.append({"raw": str(raw)})
                    continue
                else:
                    missing.append({
                        "raw": str(raw),
                        "suggestions": [str(p) for p in found[:5]],
                    })
                    continue
            if found.exists():
                resolved.append(str(found))
            else:
                missing.append({"raw": str(raw)})
        return resolved, missing

    # -- inspect_source -----------------------------------------------------

    def inspect_source(self, args: dict) -> dict:
        sources, missing = self._resolve_sources(args)
        if not sources and not missing:
            return {
                "ok": False,
                "kind": "missing_source",
                "message": "还没有提供目标源程序。",
                "hint": "请用户提供 .c/.cpp 文件或包含源程序的目录。",
            }
        if missing:
            return {
                "ok": False,
                "kind": "source_not_found",
                "missing": missing,
                "hint": "请向用户确认正确的源程序路径。",
            }

        info = []
        for source in sources:
            path = Path(source)
            entry: dict = {"path": source, "exists": True}
            if path.is_dir():
                files = sorted(path.rglob("*.c")) + sorted(path.rglob("*.cpp"))
                includes, errors, has_main = set(), [], False
                for candidate in files[:20]:
                    try:
                        text = candidate.read_text(encoding="utf-8", errors="replace")
                    except OSError:
                        continue
                    includes.update(INCLUDE_RE.findall(text))
                    has_main = has_main or bool(MAIN_RE.search(text))
                    errors.extend(
                        {"variable": m.group(1), "threshold": m.group(2)}
                        for m in PRAGMA_ERROR_RE.finditer(text)
                    )
                entry.update({
                    "kind": "directory",
                    "c_files": [str(p) for p in files[:20]],
                    "count": len(files),
                    "has_main": has_main,
                    "pragma_error": errors,
                    "includes": sorted(includes)[:15],
                    "suggested_flags": sorted({
                        flag for header in includes for flag in LIB_HINT.get(header, [])
                    }),
                })
                info.append(entry)
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError as exc:
                entry.update({"kind": "file", "error": str(exc)})
                info.append(entry)
                continue
            errors = [
                {"variable": m.group(1), "threshold": m.group(2)}
                for m in PRAGMA_ERROR_RE.finditer(text)
            ]
            includes = sorted(set(INCLUDE_RE.findall(text)))
            entry.update({
                "kind": "file",
                "lines": text.count("\n") + 1,
                "has_main": bool(MAIN_RE.search(text)),
                "pragma_error": errors,
                "pragma_region": bool(re.search(r"#pragma\s+precision\s+region", text)),
                "pragma_range": bool(re.search(r"#pragma\s+precision\s+range", text)),
                "uses_autofp": "__autofp" in text,
                "includes": includes[:15],
                "suggested_flags": sorted({
                    flag for header in includes for flag in LIB_HINT.get(header, [])
                }),
            })
            info.append(entry)

        return {
            "ok": True,
            "sources": info,
            "threshold_in_source": any(
                entry.get("threshold")
                for item in info for entry in (item.get("pragma_error") or [])
            ),
            "hint": (
                "若用户已在对话中给出阈值，直接使用，不要重复确认；"
                "仅当用户未提供且源码无 #pragma precision error 时才必须追问。"
            ),
        }

    # -- run_perci ----------------------------------------------------------

    def run_perci(self, args: dict) -> dict:
        mode = str(args.get("mode") or "").strip()
        if mode not in MODES:
            return self._error("invalid_option", f"不支持的 mode: {mode!r}",
                               f"可选: {', '.join(MODES)}")

        sources, missing = self._resolve_sources(args)
        if missing:
            missing_msgs = []
            ambiguous = False
            for item in missing:
                raw = item.get("raw", str(item))
                if item.get("suggestions"):
                    ambiguous = True
                    missing_msgs.append(
                        f"{raw!r} 有多个候选：{', '.join(item['suggestions'][:3])}"
                    )
                else:
                    missing_msgs.append(repr(raw))
            kind = "ambiguous_paths" if ambiguous else "source_not_found"
            return self._error(kind, "找不到或歧义: " + "; ".join(missing_msgs),
                               "请向用户确认源程序路径；若有歧义请让用户明确选择。")
        if mode in ("print-config", "search-optimize", "change-by-config") and not sources:
            return self._error(
                "missing_source",
                "该模式需要目标源程序。",
                "请用户提供 .c/.cpp 文件或包含源程序的目录；不要猜测路径。",
            )

        threshold = args.get("threshold") or self.state.threshold
        if threshold is not None and not self._valid_threshold(threshold):
            return self._error("invalid_threshold", f"阈值 {threshold!r} 不是有效的正数。",
                               "请用户给出类似 1e-6 的相对误差阈值。")

        threshold_from_source = False
        if mode == "search-optimize":
            if threshold is None:
                threshold = self._source_threshold(sources)
                threshold_from_source = True
                if threshold is None:
                    return self._error(
                        "missing_threshold",
                        "缺少相对误差阈值，且源码中没有可用的 #pragma precision error 阈值。",
                        "必须先向用户询问可接受的相对误差阈值（例如 1e-6），得到确认后再执行。",
                    )
                if not self._valid_threshold(threshold):
                    return self._error("invalid_threshold",
                                       f"源码中的阈值 {threshold!r} 无效。",
                                       "请用户给出类似 1e-6 的相对误差阈值。")

        algorithm = args.get("algorithm") or self.state.algorithm or "dd"
        if algorithm not in ALGORITHMS:
            return self._error("invalid_option", f"不支持的算法: {algorithm!r}",
                               f"可选: {', '.join(ALGORITHMS)}")
        time_mode = args.get("time_mode") or "program"
        if time_mode not in TIME_MODES:
            return self._error("invalid_option", f"不支持的 time_mode: {time_mode!r}",
                               f"可选: {', '.join(TIME_MODES)}")
        search_range = args.get("search_range")
        if search_range is not None:
            tokens = [t.strip() for t in str(search_range).split(",") if t.strip()]
            if not tokens or any(t not in ("16", "32", "64", "128") for t in tokens):
                return self._error("invalid_option",
                                   f"search_range 只能是 16/32/64/128 的逗号列表: {search_range!r}",
                                   "例如 128,64,32,16（只能降低精度，不能升级）。")

        target_config = None
        if mode == "change-by-config":
            target_config = args.get("target_config")
            if not target_config and self.state.best_run is not None:
                target_config = f"config/{self.state.best_run}.json"
            if not target_config:
                return self._error(
                    "missing_target_config",
                    "还没有可用的最佳配置。",
                    "请先执行 search-optimize，或为用户指定的配置路径。",
                )
            target_path = Path(str(target_config)).expanduser()
            if not target_path.is_absolute():
                target_path = self.root / target_path
            if not target_path.exists():
                return self._error("missing_target_config",
                                   f"目标配置不存在: {target_path}",
                                   "请确认配置路径，或重新执行搜索。")
            target_config = str(target_path)

        args = dict(args)
        for key in REUSE_KEYS:
            if key not in args and key in self.state.run_options:
                args[key] = self.state.run_options[key]
        if not args.get("link_libs"):
            args["link_libs"] = self._suggested_flags(sources)

        command = self._build_command(mode, sources, args, threshold, algorithm,
                                      time_mode, search_range, target_config)

        preview = self._preview_command(command)
        if not self._confirm_run(mode, preview):
            return {
                "ok": False,
                "kind": "declined",
                "message": "用户拒绝执行该命令。",
                "hint": "询问用户希望调整哪些参数或计划，不要重复执行。",
            }

        started = time.perf_counter()
        result = self._execute(command)
        seconds = time.perf_counter() - started
        text = result["text"]
        ok = result["code"] == 0

        parsed = self._parse_output(mode, text)
        if ok and mode == "search-optimize" and parsed.get("best_run") is not None:
            self.state.best_run = parsed["best_run"]
            self.state.last_search = parsed
            self.state.threshold = str(threshold) if threshold is not None else self.state.threshold
            self.state.algorithm = algorithm
        if ok:
            self.state.sources = sources
            if threshold is not None:
                self.state.threshold = str(threshold)
            if mode == "search-optimize":
                self.state.algorithm = algorithm
                self.state.search_approved = True
            self.state.run_options = {
                key: args.get(key) for key in REUSE_KEYS
                if args.get(key) not in (None, [], "")
            }
        artifacts = self._artifacts(mode)

        payload: dict = {
            "ok": ok,
            "mode": mode,
            "seconds": round(seconds, 2),
            "command": preview,
            "threshold": threshold,
            "threshold_source": (
                "pragma" if threshold_from_source else ("user" if threshold else None)
            ),
            "summary": parsed,
            "artifacts": artifacts,
            "log_tail": self._tail(text),
        }
        if not ok:
            payload["kind"] = result.get("kind") or (
                "compile_error" if COMPILE_ERROR_RE.search(text) else "run_failed"
            )
            if payload["kind"] == "compile_error":
                payload["message"] = "Perci-Tuner 构建或编译目标程序失败。"
                payload["hint"] = (
                    "向用户复述关键错误（缺头文件/库/路径或源码错误），"
                    "请用户确认修复方式；不要自行修改源码。"
                )
            else:
                payload["message"] = "Perci-Tuner 执行失败。"
                payload["hint"] = "向用户复述关键错误并请求确认。"
        elif mode == "search-optimize":
            best = parsed.get("best_run")
            if best is not None:
                payload["hint"] = (
                    f"搜索完成。下一步调用 run_perci(mode='change-by-config', "
                    f"target_config='config/{best}.json', "
                    f"sources={json.dumps(sources, ensure_ascii=False)}) "
                    f"生成混合精度程序，然后调用 summarize_results 给出性能结论。"
                )
        return payload

    @staticmethod
    def _error(kind: str, message: str, hint: str) -> dict:
        return {"ok": False, "kind": kind, "message": message, "hint": hint}

    @staticmethod
    def _valid_threshold(value) -> bool:
        try:
            number = float(str(value))
        except (TypeError, ValueError):
            return False
        return math.isfinite(number) and number > 0

    @staticmethod
    def _source_threshold(sources: list[str]) -> str | None:
        for source in sources:
            path = Path(source)
            candidates = [path] if path.is_file() else sorted(path.rglob("*.c"))[:50]
            for candidate in candidates:
                try:
                    text = candidate.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    continue
                match = PRAGMA_ERROR_RE.search(text)
                if match and match.group(2):
                    return match.group(2)
        return None

    @staticmethod
    def _suggested_flags(sources: list[str]) -> list[str]:
        flags = set()
        for source in sources:
            path = Path(source)
            candidates = [path] if path.is_file() else (
                sorted(path.rglob("*.c")) + sorted(path.rglob("*.cpp")))[:50]
            for candidate in candidates:
                try:
                    text = candidate.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    continue
                for header in INCLUDE_RE.findall(text):
                    flags.update(LIB_HINT.get(header, []))
        return sorted(flags)

    def _build_command(self, mode, sources, args, threshold, algorithm,
                       time_mode, search_range, target_config) -> list[str]:
        command = ["sh", str(self.run_script), mode]
        if mode == "search-optimize":
            command.append(f"-thres={threshold}")
            command.append(f"-algorithm={algorithm}")
            command.append(f"-time-mode={time_mode}")
            if args.get("repeat") is not None:
                command.append(f"-repeat={int(args['repeat'])}")
            if search_range:
                command.append(f"-search-range={search_range}")
            if args.get("shuffled"):
                command.append("-shuffled")
        if mode == "change-by-config" and target_config:
            command.append(f"-target={target_config}")
        if mode != "check":
            for name, flag in (("include_func", "-include-func"),
                               ("exclude_func", "-exclude-func"),
                               ("include_file", "-include-file"),
                               ("exclude_file", "-exclude-file")):
                value = args.get(name)
                if value:
                    command.append(f"{flag}={value}")
        if args.get("mxp"):
            command.append("-mxp")
        if args.get("fp128todd"):
            command.append("-fp128todd")
        if args.get("check_result") and not args.get("mxp"):
            command.append("-check-result")
        timeout = args.get("timeout")
        if timeout is not None:
            command.append(f"-timeout={timeout}")
        level = args.get("optimize_level")
        if level:
            command.append(str(level))
        if mode != "check":
            command.extend(sources)
        for lib in args.get("link_libs") or []:
            command.append(self._normalize_flag(str(lib), "-l"))
        for directory in args.get("link_dirs") or []:
            command.append(self._normalize_flag(str(directory), "-L"))
        for directory in args.get("include_dirs") or []:
            command.append(self._normalize_flag(str(directory), "-I"))
        for run_arg in args.get("run_args") or []:
            command.append(f"-arg={run_arg}")
        return command

    @staticmethod
    def _normalize_flag(value: str, prefix: str) -> str:
        if value.startswith(prefix) or value.startswith("-"):
            return value
        return prefix + value

    def _preview_command(self, command: list[str]) -> str:
        parts = []
        for part in command:
            if " " in part:
                parts.append(f"'{part}'")
            else:
                parts.append(part)
        text = " ".join(parts)
        if len(text) > self.ui.width * 2:
            text = text[: self.ui.width * 2 - 3] + "..."
        return text

    def _confirm_run(self, mode: str, preview: str) -> bool:
        if self.auto_yes or not self.config.get("confirm_tools", True):
            return True
        if mode == "change-by-config" and self.state.search_approved:
            self.ui.info("已确认过本次搜索，直接生成混合精度程序。")
            return True
        if not sys.stdin.isatty():
            return False
        self.ui.info("将执行: " + preview)
        return self.ui.confirm(f"允许执行 {mode} 吗？")

    def _execute(self, command: list[str]) -> dict:
        env = os.environ.copy()
        env["PERCI_TUNER"] = str(self.root)
        captured: deque[str] = deque(maxlen=600)
        verbose = bool(self.config.get("verbose_tool_output", True))
        try:
            process = subprocess.Popen(
                command,
                cwd=str(self.root),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                errors="replace",
                bufsize=1,
                env=env,
                start_new_session=True,
            )
        except OSError as exc:
            return {"code": 127, "text": str(exc), "kind": "run_failed"}

        interrupted = False
        try:
            assert process.stdout is not None
            for line in process.stdout:
                line = line.rstrip("\n")
                captured.append(line)
                if verbose:
                    self.ui.tool_log(line)
            process.wait()
        except KeyboardInterrupt:
            interrupted = True
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except (ProcessLookupError, PermissionError):
                pass
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except (ProcessLookupError, PermissionError):
                    pass
        text = "\n".join(captured)
        if interrupted:
            return {"code": 130, "text": text + "\n[用户中断]", "kind": "interrupted"}
        return {"code": process.returncode, "text": text}

    @staticmethod
    def _tail(text: str, limit: int = 9000) -> str:
        if len(text) <= limit:
            return text
        return "...\n" + text[-limit:]

    def _parse_output(self, mode: str, text: str) -> dict:
        parsed: dict = {}
        if mode == "search-optimize":
            match = re.search(r"Optimized config index\s*\[\s*(\d+)\s*\]", text)
            if match:
                parsed["best_run"] = int(match.group(1))
            match = re.search(r"Optimized cost:\s*([0-9.eE+-]+)", text)
            if match:
                parsed["optimized_seconds"] = self._to_float(match.group(1))
            match = re.search(r"Original time:\s*([0-9.eE+-]+|inf)", text)
            if match:
                parsed["original_seconds"] = self._to_float(match.group(1))
            match = re.search(r"Acc rate:\s*([0-9.eE+-]+)\s*%", text)
            if match:
                parsed["speedup_percent"] = self._to_float(match.group(1))
            match = re.search(r"Searched\s+(\d+)\s+configs including\s+(\d+)\s+failed", text)
            if match:
                parsed["searched_configs"] = int(match.group(1))
                parsed["failed_configs"] = int(match.group(2))
            match = re.search(r"Search space size:\s*(\d+)", text)
            if match:
                parsed["search_space"] = int(match.group(1))
        if mode == "change-by-config":
            parsed["mixed_program"] = "changed_output"
            parsed["mixed_ir"] = "changed_output.bc"
        if mode == "print-config":
            parsed["config_file"] = "config.json"
        return parsed

    @staticmethod
    def _to_float(value: str) -> float | None:
        try:
            return float(value)
        except (TypeError, ValueError):
            return None

    def _artifacts(self, mode: str) -> list[str]:
        names: list[str] = []
        if mode == "search-optimize" and self.state.best_run is not None:
            names = [f"config/{self.state.best_run}.json",
                     f"IR/{self.state.best_run}.bc",
                     f"exec/{self.state.best_run}"]
        elif mode == "change-by-config":
            names = ["changed_output.bc", "changed_output"]
        elif mode == "print-config":
            names = ["config.json", "output.bc", "output"]
        elif mode == "check":
            names = []
        present = [name for name in names if (self.root / name).exists()]
        if (self.root / "run_history.csv").exists():
            present.append("run_history.csv")
        return present

    # -- summarize_results --------------------------------------------------

    def summarize_results(self, args: dict) -> dict:
        history_path = self.root / "run_history.csv"
        rows = self._read_history(history_path)
        best_run = self.state.best_run
        threshold = self.state.threshold
        if threshold is None:
            threshold = self._config_threshold()

        best_row = None
        if best_run is not None:
            for row in rows:
                if str(row.get("run_id")) == str(best_run):
                    best_row = row
                    break

        valid_rows = [row for row in rows if self._valid_row(row, threshold)]
        if best_row is None and valid_rows:
            best_row = min(valid_rows, key=lambda row: self._row_seconds(row))
            try:
                best_run = int(best_row.get("run_id"))
            except (TypeError, ValueError):
                best_run = None

        baseline_row = next(
            (row for row in rows if str(row.get("run_id")) == "1"), None
        )
        original_seconds = self._row_seconds(baseline_row) if baseline_row else None
        best_seconds = self._row_seconds(best_row) if best_row else None

        result: dict = {
            "ok": True,
            "history": "run_history.csv" if history_path.exists() else None,
            "best_run": best_run,
            "config": f"config/{best_run}.json" if best_run is not None else None,
            "ir": f"IR/{best_run}.bc" if best_run is not None else None,
            "executable": f"exec/{best_run}" if best_run is not None else None,
            "mixed_program": "changed_output" if (self.root / "changed_output").exists() else None,
            "mixed_ir": "changed_output.bc" if (self.root / "changed_output.bc").exists() else None,
            "search": self.state.last_search or {},
            "top_configs": [],
        }
        if threshold is not None:
            result["threshold"] = threshold
        if best_row and original_seconds is not None and best_seconds is not None:
            result["performance"] = {
                "original_seconds": original_seconds,
                "optimized_seconds": best_seconds,
                "speedup_percent": self._speedup(original_seconds, best_seconds),
                "searched_configs": len(rows),
                "invalid_configs": sum(
                    1 for row in rows if not math.isfinite(self._row_seconds(row))
                ),
            }
        if best_row:
            rel_err = best_row.get("rel_err")
            baseline = str(rel_err).strip().lower() in ("", "inf", "infinity", "nan", "none")
            item = {
                "run_id": best_row.get("run_id"),
                "seconds": best_seconds,
                "changes": (best_row.get("changes") or "")[:300],
                "baseline": baseline,
            }
            if baseline:
                item["note"] = "原始配置，未做精度修改"
                item["speedup_percent"] = 0.0
                item["rel_err"] = None
                result["precision"] = {"unchanged": True, "note": "保持原始精度，未做修改"}
            else:
                item["rel_err"] = rel_err
                item["speedup_percent"] = (
                    self._speedup(original_seconds, best_seconds)
                    if original_seconds is not None else None
                )
                result["precision"] = {
                    "unchanged": False,
                    "relative_error": rel_err,
                    "threshold": threshold,
                }
            result["best"] = item
        top = sorted(
            (row for row in valid_rows if str(row.get("run_id")) != "1"),
            key=lambda row: self._row_seconds(row),
        )[:5]
        for row in top:
            seconds = self._row_seconds(row)
            result["top_configs"].append({
                "run_id": row.get("run_id"),
                "seconds": self._to_float(row.get("seconds")),
                "speedup_percent": (
                    self._speedup(original_seconds, seconds)
                    if original_seconds is not None and math.isfinite(seconds) else None
                ),
                "rel_err": row.get("rel_err"),
            })
        if not rows:
            result["hint"] = "还没有 run_history.csv，请先执行 search-optimize。"
        elif result.get("mixed_program") is None:
            result["hint"] = (
                "还没有混合精度程序。请调用 run_perci(mode='change-by-config', "
                f"target_config='config/{best_run}.json') 生成 changed_output。"
                if best_run is not None else "请先完成搜索。"
            )
        return result

    @staticmethod
    def _speedup(original: float, optimized: float) -> float | None:
        if original <= 0 or not math.isfinite(original):
            return None
        return round((original - optimized) / original * 100.0, 2)

    @staticmethod
    def _read_history(path: Path) -> list[dict]:
        if not path.exists():
            return []
        try:
            with path.open("r", newline="", encoding="utf-8") as handle:
                return list(csv.DictReader(handle))
        except (OSError, csv.Error):
            return []

    def _config_threshold(self) -> str | None:
        path = self.root / "config.json"
        if not path.exists():
            return None
        try:
            config = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return None
        value = config.get("result_error")
        return str(value) if value is not None else None

    @staticmethod
    def _row_seconds(row: dict) -> float:
        try:
            return float(row.get("seconds") or "inf")
        except (TypeError, ValueError):
            return math.inf

    def _valid_row(self, row: dict, threshold: str | None) -> bool:
        try:
            seconds = float(row.get("seconds") or "inf")
        except (TypeError, ValueError):
            return False
        if not math.isfinite(seconds):
            return False
        if str(row.get("run_id")) == "1":
            return True
        try:
            rel_err = float(row.get("rel_err") or "inf")
        except (TypeError, ValueError):
            return False
        if not math.isfinite(rel_err):
            return False
        if threshold is not None:
            try:
                return rel_err <= float(threshold)
            except (TypeError, ValueError):
                return True
        return True


# ---------------------------------------------------------------------------
# Agent loop
# ---------------------------------------------------------------------------

def read_skill_body() -> str:
    try:
        text = SKILL_FILE.read_text(encoding="utf-8")
    except OSError:
        return "你是 Perci-Tuner 的自然语言外壳。"
    if text.startswith("---"):
        parts = text.split("---", 2)
        if len(parts) == 3:
            return parts[2].strip()
    return text.strip()


def build_system_prompt(root: Path, config: dict, state: SessionState) -> str:
    skill = read_skill_body()
    existing = [
        name for name in ("config.json", "output.bc", "output",
                          "run_history.csv", "changed_output")
        if (root / name).exists()
    ]
    runtime = f"""

---

# 运行时上下文（CLI 注入，仅本次会话有效）

- 项目根目录：`{root}`
- 入口：`{root}/scripts/run.sh`
- 当前模型：`{config.get("model")}`
- 现有产物：{", ".join(f"`{name}`" for name in existing) if existing else "无"}
- 会话已记录：sources={state.sources or "未提供"}，threshold={state.threshold or "未确认"}，best_run={state.best_run or "无"}

## 必须遵守

1. 与 Perci-Tuner 无关的问题：直接回答，禁止调用任何工具。
2. 缺少源程序、或缺少误差阈值且源码没有 `#pragma precision error`：先追问用户，禁止猜测。用户已明确给出阈值时直接使用，不要重复确认。
3. 工具返回 `compile_error` 等失败：向用户复述关键错误并请求确认，禁止自行修改源码。
4. 用户拒绝执行工具时：询问要调整的计划，禁止立即重试同一命令。
5. 搜索成功后必须继续生成混合精度程序（`change-by-config`），最终给出配置路径、程序路径、性能与精度结论。
6. 执行许可由 CLI 在 `run_perci` 前弹出确认；不要在文本里重复询问“是否开始执行”，否则会打断流程。
7. 回答使用用户的语言；数字只引用工具返回值，不要编造。`precision.unchanged` 为 true 时写“保持原始精度，未做修改”，不要报告 null 或 inf；`invalid_configs` 是无效配置数，不是失败数；阈值未记录时说明“本次搜索阈值未记录”。
"""
    return skill + runtime


class Agent:
    def __init__(self, root: Path, config: dict, ui: UI, executor: ToolExecutor,
                 client: LLMClient, auto_yes: bool, sessions: SessionManager | None = None,
                 initial_session_id: str | None = None):
        self.root = root
        self.config = config
        self.ui = ui
        self.executor = executor
        self.client = client
        self.auto_yes = auto_yes
        self.sessions = sessions or SessionManager(root)
        self.messages: list[dict] = []
        self.usage_tracker = UsageTracker()
        self._last_usage: dict | None = None
        self.current_session: PersistedSession | None = None
        self._last_session_metas: list[SessionMeta] = []  # cached from last `list`

        if initial_session_id:
            self._load_or_create(initial_session_id)
        else:
            self._load_default_or_create()

    # -- session helpers ---------------------------------------------------

    def _load_default_or_create(self) -> None:
        """Resume the most recent session, or start a fresh one."""
        metas = self.sessions.list_sessions()
        if metas:
            latest = self.sessions.find(metas[0].id)
            if latest is not None:
                self._restore(latest)
                self.ui.info(f"已恢复会话 “{latest.meta.title}” ({latest.meta.id})")
                return
        self._start_new("新会话")

    def _load_or_create(self, target: str) -> None:
        session = self.sessions.find(target)
        if session is not None:
            self._restore(session)
            self.ui.info(f"已切换到会话 “{session.meta.title}” ({session.meta.id})")
        else:
            self._start_new(target)

    def _start_new(self, title: str) -> None:
        self.executor.state = SessionState()
        system_prompt = build_system_prompt(self.root, self.config, self.executor.state)
        session = self.sessions.create(
            title=title or "新会话",
            model=str(self.config.get("model")),
            system_prompt=system_prompt,
            executor_state=asdict(self.executor.state),
        )
        self.current_session = session
        self.messages = list(session.messages)
        self.usage_tracker = UsageTracker()
        self._last_usage = None

    def _restore(self, session: PersistedSession) -> None:
        self.current_session = session
        self.messages = list(session.messages) if session.messages else []
        self.usage_tracker = UsageTracker(**(session.usage or {}))
        self._last_usage = None
        # restore executor state
        state_data = session.executor_state or {}
        self.executor.state = SessionState(**{
            key: state_data.get(key, getattr(SessionState(), key))
            for key in ("sources", "threshold", "algorithm", "best_run",
                        "last_search", "run_options", "search_approved")
        })
        # ensure system prompt is present and up to date
        if not self.messages or self.messages[0].get("role") != "system":
            system_prompt = build_system_prompt(self.root, self.config, self.executor.state)
            self.messages.insert(0, {"role": "system", "content": system_prompt})
        else:
            # refresh system prompt with current runtime context
            self.messages[0] = {
                "role": "system",
                "content": build_system_prompt(self.root, self.config, self.executor.state),
            }

    def save_current(self) -> None:
        """Persist current messages + state + usage to disk."""
        if self.current_session is None:
            return
        self.current_session.messages = list(self.messages)
        self.current_session.executor_state = asdict(self.executor.state)
        self.current_session.usage = self.usage_tracker.as_dict()
        try:
            self.sessions.save(self.current_session)
        except OSError as exc:
            self.ui.error(f"会话保存失败: {exc}")

    def switch_session(self, target: str) -> bool:
        """Save current, then switch to target. Returns False if not found."""
        self.save_current()
        found = self.sessions.find(target)
        if found is None:
            return False
        self._restore(found)
        return True

    def new_session(self, title: str = "新会话") -> None:
        self.save_current()
        self._start_new(title)

    # -- agent loop --------------------------------------------------------

    def reset(self) -> None:
        """Reset current in-memory messages/state but stay in the same session."""
        self.executor.state = SessionState()
        self.messages = [{
            "role": "system",
            "content": build_system_prompt(self.root, self.config, self.executor.state),
        }]
        self.usage_tracker = UsageTracker()
        self._last_usage = None
        if self.current_session is not None:
            self.current_session.messages = list(self.messages)
            self.current_session.executor_state = asdict(self.executor.state)
            self.current_session.usage = {}
            self.sessions.save(self.current_session)

    def handle(self, user_text: str) -> None:
        self.messages.append({"role": "user", "content": user_text})
        max_rounds = int(self.config.get("max_tool_rounds", 16))
        retried_empty = False
        for round_index in range(max_rounds):
            assistant = self.client.chat(self.messages, TOOL_SCHEMAS, self.ui)
            # every assistant call (tool round or terminal) has its own usage
            usage = assistant.get("_usage")
            if usage:
                self._last_usage = usage
                self.usage_tracker.add(usage)
            self.messages.append(assistant)
            calls = assistant.get("tool_calls") or []
            if not calls:
                if assistant.get("content"):
                    self.save_current()
                    self._show_usage_panel()
                    return
                if not retried_empty:
                    retried_empty = True
                    self.ui.info("模型返回空内容，正在要求其补充回答…")
                    self.messages.append({
                        "role": "user",
                        "content": "请基于上面的工具结果，用自然语言简明回答用户的问题。",
                    })
                    continue
                self.ui.error("模型没有返回内容。请重试，或输入 /clear 后重新描述需求。")
                self.save_current()
                self._show_usage_panel()
                return
            for call in calls:
                name = call.get("function", {}).get("name") or ""
                raw = call.get("function", {}).get("arguments") or "{}"
                try:
                    arguments = json.loads(raw) if raw.strip() else {}
                except json.JSONDecodeError:
                    arguments = {}
                    result = {
                        "ok": False,
                        "kind": "bad_arguments",
                        "message": f"工具参数不是合法 JSON: {raw[:200]}",
                        "hint": "请重新调用工具并提供合法 JSON 参数。",
                    }
                else:
                    if not isinstance(arguments, dict):
                        arguments = {}
                        result = {
                            "ok": False,
                            "kind": "bad_arguments",
                            "message": "工具参数必须是 JSON 对象。",
                            "hint": "请重新调用工具。",
                        }
                    else:
                        self.ui.tool_begin(name, arguments)
                        started = time.perf_counter()
                        result = self._run_tool(name, arguments)
                        self.ui.tool_end(
                            bool(result.get("ok")),
                            time.perf_counter() - started,
                            self._result_note(result),
                        )
                        self._show_tool_result(name, arguments, result)

                self.messages.append({
                    "role": "tool",
                    "tool_call_id": call.get("id") or f"call_{round_index}",
                    "name": name,
                    "content": json.dumps(result, ensure_ascii=False),
                })
        self.ui.error("达到最大工具调用轮次，已停止本轮。请缩小需求后重试。")
        self.save_current()
        self._show_usage_panel()

    def _show_usage_panel(self) -> None:
        """Display a compact usage panel below the assistant's answer."""
        tracker = self.usage_tracker
        delta = self._last_usage or {}
        delta_in = int(delta.get("prompt_tokens") or 0)
        delta_out = int(delta.get("completion_tokens") or 0)
        delta_total = int(delta.get("total_tokens") or (delta_in + delta_out))
        items = [
            ("本轮", f"{delta_in}+{delta_out} = {delta_total}"),
            ("累计", f"{tracker.total_tokens} tokens · {tracker.turns} turns"),
        ]
        if delta_total == 0 and tracker.total_tokens == 0:
            items.insert(0, ("", "（usage 未由模型返回）"))
        if self.current_session:
            items.append(("会话", self.current_session.meta.id))
        self.ui.panel("context usage", items)

    @staticmethod
    def _result_note(result: dict) -> str:
        if result.get("ok"):
            artifacts = result.get("artifacts") or []
            return ", ".join(Path(name).name for name in artifacts[:3])
        return str(result.get("kind") or "error")

    def _run_tool(self, name: str, arguments: dict) -> dict:
        if name == "inspect_source":
            return self.executor.inspect_source(arguments)
        if name == "run_perci":
            return self.executor.run_perci(arguments)
        if name == "summarize_results":
            return self.executor.summarize_results(arguments)
        return {
            "ok": False,
            "kind": "unknown_tool",
            "message": f"未知工具: {name}",
            "hint": "只使用 inspect_source / run_perci / summarize_results。",
        }

    def _show_tool_result(self, name: str, arguments: dict, result: dict) -> None:
        if name != "run_perci" or not result.get("ok"):
            return
        if arguments.get("mode") == "search-optimize" and result.get("summary"):
            summary = result["summary"]
            items = []
            if summary.get("best_run") is not None:
                items.append(("best run", f"#{summary['best_run']}"))
            if summary.get("speedup_percent") is not None:
                items.append(("speedup", f"{summary['speedup_percent']:.2f}%"))
            if summary.get("searched_configs") is not None:
                items.append(("searched", f"{summary['searched_configs']} 个配置"))
            if summary.get("best_run") is not None:
                items.append(("config", f"config/{summary['best_run']}.json"))
            if items:
                self.ui.panel("search result", items)
        elif arguments.get("mode") == "change-by-config":
            self.ui.panel("mixed-precision program", [
                ("program", "changed_output"),
                ("IR", "changed_output.bc"),
            ])


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def find_root(override: str | None) -> Path:
    if override:
        root = Path(override).expanduser().resolve()
    elif os.environ.get("PERCI_TUNER"):
        root = Path(os.environ["PERCI_TUNER"]).expanduser().resolve()
    else:
        root = PROJECT_ROOT
    if not (root / "scripts" / "run.sh").exists():
        raise LLMError(f"项目根目录无效（找不到 scripts/run.sh）: {root}\n"
                       f"可通过 --root 或 PERCI_TUNER 指定。")
    return root


def setup_readline() -> None:
    try:
        import readline
    except ImportError:
        return
    history = Path.home() / ".perci_tuner_agent_history"
    try:
        readline.read_history_file(str(history))
    except OSError:
        pass
    import atexit
    atexit.register(lambda: _save_history(readline, history))


def _save_history(readline_module, history: Path) -> None:
    try:
        readline_module.write_history_file(str(history))
    except OSError:
        pass


HELP_TEXT = """
  /help                 显示本帮助
  /clear                清空当前会话的对话上下文（保留会话文件）
  /config               显示当前 API 配置
  /usage                显示当前会话累计 token 使用
  /path fuzzy <路径>    手动测试模糊路径匹配
  /session list         列出所有会话
  /session show         显示当前会话信息
  /session new [标题]   新建会话
  /session switch <序号|id|标题> 切换到指定会话
  /session rename <序号|id> <新标题> 重命名会话
  /session delete <序号|id>  删除会话
  /quit                 退出

  直接输入自然语言即可，例如：
    把 test/accumulation_drift/dotprod_naive/dotprod_naive.c 调到误差 1e-6 以内，尽量加速
    帮我看看上次搜索的最佳配置
"""



def _resolve_session_ref(agent: Agent, arg: str) -> SessionMeta | None:
    """Resolve a user-given session reference (序号 / id / title).

    Accepts:
      1) decimal index — 1-based, uses cached /list numbering
      2) exact session id  (session-261029ab...)
      3) title             (startswith → contains)
    """
    arg = arg.strip()
    if not arg:
        return None
    # 1) numeric index
    if arg.isdigit():
        idx = int(arg) - 1
        metas = agent._last_session_metas or agent.sessions.list_sessions()
        if 0 <= idx < len(metas):
            return metas[idx]
        if agent._last_session_metas:
            fresh = agent.sessions.list_sessions()
            agent._last_session_metas = fresh
            if 0 <= idx < len(fresh):
                return fresh[idx]
        return None
    # 2) exact id by filename
    by_id = agent.sessions.dir / f"{arg}.json"
    if by_id.exists():
        try:
            data = json.loads(by_id.read_text(encoding="utf-8"))
            meta = data.get("meta") or {}
            return SessionMeta(
                id=meta.get("id") or arg,
                title=meta.get("title") or arg,
                created_at=meta.get("created_at", ""),
                updated_at=meta.get("updated_at", ""),
                model=meta.get("model", ""),
            )
        except (OSError, json.JSONDecodeError):
            return None
    # 3) title match (exact → startswith → contains)
    metas = agent.sessions.list_sessions()
    for m in metas:
        if m.title == arg:
            return m
    low = arg.lower()
    for m in metas:
        if m.title.lower().startswith(low):
            return m
    for m in metas:
        if low in m.title.lower():
            return m
    return None


def _fmt_session_updated(iso: str) -> str:
    if not iso:
        return "-"
    return iso[:16].replace("T", " ")


def _idx_of(agent: Agent, sid: str) -> int:
    """1-based index of sid in cached (or fresh) list; 0 if missing."""
    metas = agent._last_session_metas or agent.sessions.list_sessions()
    agent._last_session_metas = metas
    for i, m in enumerate(metas, 1):
        if m.id == sid:
            return i
    return 0


def _print_current_session(agent: Agent, ui: UI) -> None:
    if agent.current_session is None:
        ui.info("当前未进入任何会话。")
        return
    s = agent.current_session.meta
    idx = _idx_of(agent, s.id)
    ui.panel("当前会话", [
        ("#", str(idx) if idx else "-"),
        ("id", s.id),
        ("title", s.title),
        ("updated", _fmt_session_updated(s.updated_at)),
        ("messages", str(len(agent.messages))),
    ])


def _handle_session_cmd(agent: Agent, ui: UI, line: str) -> None:
    """Parse and execute /session subcommands."""
    parts = line.split(None, 2)
    sub = parts[1] if len(parts) > 1 else "show"
    arg = parts[2] if len(parts) > 2 else ""
    sessions = agent.sessions

    if sub == "list" or sub == "ls":
        metas = sessions.list_sessions()
        agent._last_session_metas = metas
        if not metas:
            ui.info("还没有任何会话。用 /session new 新建。")
            return
        rows = []
        for idx, m in enumerate(metas, 1):
            mark = "●" if (agent.current_session and agent.current_session.meta.id == m.id) else " "
            rows.append(
                (f"{idx:>2}{mark}",
                 f"{m.title}  ({_fmt_session_updated(m.updated_at)})")
            )
        for j in range(0, len(rows), 8):
            ui.panel("sessions", rows[j:j + 8])
        return

    if sub == "show" or sub == "status":
        _print_current_session(agent, ui)
        return

    if sub == "new":
        title = arg or f"新会话 {datetime.now().strftime('%H:%M:%S')}"
        agent.new_session(title)
        ui.info(f"已创建新会话 “{agent.current_session.meta.title}” (#1)")
        return

    if sub == "switch" or sub == "sw":
        if not arg:
            ui.info("用法: /session switch <序号|id|标题>")
            ui.info("先用 /session list 查看序号。")
            return
        meta = _resolve_session_ref(agent, arg)
        if meta is None:
            ui.error(f"找不到会话 {arg!r}")
            ui.info("用 /session list 查看所有会话；或直接给会话序号。")
            return
        ok = agent.switch_session(meta.id)
        if ok:
            idx = _idx_of(agent, meta.id)
            ui.info(f"已切换到 #{idx} “{agent.current_session.meta.title}”")
        return

    if sub == "rename":
        rest = arg
        if not rest:
            ui.info("用法: /session rename <序号|id> <新标题>")
            return
        bits = rest.split(None, 1)
        ref = bits[0]
        new_title = bits[1] if len(bits) > 1 else ref
        meta = _resolve_session_ref(agent, ref)
        if meta is None:
            ui.error(f"找不到会话 {ref!r}")
            return
        renamed = sessions.rename(meta.id, new_title)
        if renamed is None:
            ui.error(f"重命名失败：会话 {meta.id} 已不存在")
            return
        if agent.current_session and agent.current_session.meta.id == meta.id:
            agent.current_session.meta = renamed.meta
        agent._last_session_metas = sessions.list_sessions()
        idx = _idx_of(agent, meta.id)
        ui.info(f"已重命名 #{idx} 为 “{renamed.meta.title}”")
        return

    if sub == "delete" or sub == "rm":
        if not arg:
            ui.info("用法: /session delete <序号|id>")
            ui.info("先用 /session list 查看序号。")
            return
        meta = _resolve_session_ref(agent, arg)
        if meta is None:
            ui.error(f"找不到会话 {arg!r}")
            return
        idx = _idx_of(agent, meta.id)
        label = f"#{idx} “{meta.title}”"
        if not agent.auto_yes:
            ok = ui.confirm(f"删除会话 {label} 吗？")
            if not ok:
                ui.info("已取消。")
                return
        was_current = agent.current_session and agent.current_session.meta.id == meta.id
        deleted = sessions.delete(meta.id)
        if deleted:
            ui.info(f"已删除 {label}")
            if was_current:
                agent.current_session = None
                metas = sessions.list_sessions()
                agent._last_session_metas = metas
                if metas:
                    agent.switch_session(metas[0].id)
                    new_idx = _idx_of(agent, metas[0].id)
                    ui.info(f"已切换到 #{new_idx} “{agent.current_session.meta.title}”")
                else:
                    agent.new_session("新会话")
                    ui.info("已创建新会话。")
        return

    ui.info(f"未知的 /session 子命令: {sub}")

def repl(agent: Agent, ui: UI, config_path: Path) -> int:
    ui.banner(agent.config.get("model", "?"), agent.root)
    _print_current_session(agent, ui)
    setup_readline()
    while True:
        try:
            line = input(ui.user_prompt()).strip()
        except EOFError:
            print()
            agent.save_current()
            return 0
        except KeyboardInterrupt:
            print()
            continue
        if not line:
            continue
        if line in ("/quit", "/exit", "/q"):
            agent.save_current()
            return 0
        if line == "/help":
            print(HELP_TEXT)
            continue
        if line == "/clear":
            agent.reset()
            ui.info("对话上下文已清空（会话保留）。")
            continue
        if line == "/config":
            ui.panel("api config", [
                ("file", str(config_path)),
                ("base_url", str(agent.config.get("base_url"))),
                ("model", str(agent.config.get("model"))),
                ("confirm", str(agent.config.get("confirm_tools", True))),
            ])
            continue
        if line == "/usage":
            t = agent.usage_tracker
            ui.panel("token usage", [
                ("累计 tokens", str(t.total_tokens)),
                ("prompt", str(t.prompt_tokens)),
                ("completion", str(t.completion_tokens)),
                ("turns", str(t.turns)),
            ])
            continue
        if line.startswith("/path fuzzy"):
            needle = line.split(None, 2)[2] if len(line.split(None, 2)) > 2 else ""
            if not needle:
                ui.info("用法: /path fuzzy <路径片段>")
                continue
            resolver = agent.executor.path_resolver
            matches = resolver.resolve(needle)
            if not matches:
                ui.info(f"没有匹配 {needle!r}")
            else:
                ui.panel("fuzzy matches", [(str(i + 1), str(p)) for i, p in enumerate(matches[:8])])
            continue
        if line.startswith("/session") or line.startswith("/sessions"):
            _handle_session_cmd(agent, ui, line)
            continue
        try:
            agent.handle(line)
        except LLMError as exc:
            ui.error(str(exc))
            ui.info("请检查 API 配置，或输入 /config 查看当前配置。")
        except KeyboardInterrupt:
            print()
            ui.info("已中断本轮。")

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Perci-Tuner natural-language agent shell",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("-p", "--prompt", help="单轮执行后退出")
    parser.add_argument("--config", default=None,
                        help=f"API 配置路径（默认 {AGENT_CONFIG_FILE}）")
    parser.add_argument("--session", dest="session_id", default=None,
                        help="指定要继续的会话 id 或标题（不存在则新建）")
    parser.add_argument("--root", help="Perci-Tuner 项目根目录")
    parser.add_argument("--yes", action="store_true", help="跳过工具执行确认")
    parser.add_argument("--no-color", action="store_true", help="禁用颜色")
    parser.add_argument("--show-reasoning", action="store_true", help="显示模型推理过程")
    parser.add_argument("--max-rounds", type=int, help="单轮最大工具调用次数")
    parser.add_argument("--version", action="version", version=f"perci-tuner agent {VERSION}")
    args = parser.parse_args(argv)

    ui = UI(color=not args.no_color)
    try:
        config_arg = Path(args.config).expanduser().resolve() if args.config else None
        config, config_path = load_api_config(config_arg)
        root = find_root(args.root)
    except LLMError as exc:
        ui.error(str(exc))
        return 2

    if args.show_reasoning:
        config["show_reasoning"] = True
    if args.max_rounds is not None:
        config["max_tool_rounds"] = args.max_rounds

    sessions = SessionManager(root)
    state = SessionState()
    executor = ToolExecutor(root, ui, config, state, args.yes)
    client = LLMClient(config)
    agent = Agent(root, config, ui, executor, client, args.yes,
                  sessions=sessions, initial_session_id=args.session_id)

    if args.prompt:
        try:
            agent.handle(args.prompt)
            agent.save_current()
            return 0
        except LLMError as exc:
            ui.error(str(exc))
            return 1
        except KeyboardInterrupt:
            print()
            agent.save_current()
            ui.error("已中断。")
            return 130

    return repl(agent, ui, config_path)


if __name__ == "__main__":
    sys.exit(main())
