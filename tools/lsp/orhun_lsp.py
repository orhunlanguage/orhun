#!/usr/bin/env python3
import json
import re
import sys
from typing import Any, Dict, List, Optional


KEYWORDS = [
    "yazdır",
    "olsun",
    "eğer",
    "ise",
    "değilse",
    "sürece",
    "tekrarla",
    "kez",
    "işlev",
    "döndür",
    "tip",
    "yeni",
    "benim",
    "ust",
    "deneme",
    "yakala",
    "kır",
    "devam",
    "dahil_et",
    "için",
    "içinde",
    "ve",
    "veya",
    "değil",
    "doğru",
    "yanlış",
]


def read_message() -> Optional[Dict[str, Any]]:
    content_length = None
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line == b"\r\n":
            break
        text = line.decode("utf-8", errors="replace").strip()
        if text.lower().startswith("content-length:"):
            _, value = text.split(":", 1)
            content_length = int(value.strip())

    if content_length is None:
        return None
    payload = sys.stdin.buffer.read(content_length)
    if not payload:
        return None
    return json.loads(payload.decode("utf-8", errors="replace"))


def send_message(msg: Dict[str, Any]) -> None:
    raw = json.dumps(msg, ensure_ascii=False).encode("utf-8")
    sys.stdout.buffer.write(f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii"))
    sys.stdout.buffer.write(raw)
    sys.stdout.buffer.flush()


def publish_diagnostics(uri: str, text: str) -> Dict[str, Any]:
    diagnostics: List[Dict[str, Any]] = []
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if "\t" in line:
            diagnostics.append(
                {
                    "range": {
                        "start": {"line": i, "character": 0},
                        "end": {"line": i, "character": len(line)},
                    },
                    "severity": 2,
                    "source": "orhun-lsp",
                    "message": "Tab karakteri bulundu; 4 boşluk kullanın.",
                }
            )
        if re.search(r"[ \t]+$", line):
            diagnostics.append(
                {
                    "range": {
                        "start": {"line": i, "character": max(0, len(line) - 1)},
                        "end": {"line": i, "character": len(line)},
                    },
                    "severity": 3,
                    "source": "orhun-lsp",
                    "message": "Satır sonu gereksiz boşluk.",
                }
            )
        indent = len(line) - len(line.lstrip(" "))
        if line.strip() and (indent % 4) != 0:
            diagnostics.append(
                {
                    "range": {
                        "start": {"line": i, "character": 0},
                        "end": {"line": i, "character": indent},
                    },
                    "severity": 2,
                    "source": "orhun-lsp",
                    "message": "Girinti 4'ün katı olmalı.",
                }
            )

    return {
        "jsonrpc": "2.0",
        "method": "textDocument/publishDiagnostics",
        "params": {"uri": uri, "diagnostics": diagnostics},
    }


def completion_items() -> List[Dict[str, Any]]:
    return [
        {
            "label": kw,
            "kind": 14,  # Keyword
            "detail": "Orhun keyword",
            "insertText": kw,
        }
        for kw in KEYWORDS
    ]


def run_server() -> int:
    docs: Dict[str, str] = {}
    shutdown_requested = False

    while True:
        msg = read_message()
        if msg is None:
            break

        method = msg.get("method")
        msg_id = msg.get("id")

        if method == "initialize":
            send_message(
                {
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "result": {
                        "capabilities": {
                            "textDocumentSync": 1,
                            "completionProvider": {
                                "resolveProvider": False,
                                "triggerCharacters": [".", ":"],
                            },
                        },
                        "serverInfo": {"name": "orhun-lsp", "version": "0.1.0"},
                    },
                }
            )
            continue

        if method == "initialized":
            continue

        if method == "shutdown":
            shutdown_requested = True
            send_message({"jsonrpc": "2.0", "id": msg_id, "result": None})
            continue

        if method == "exit":
            return 0 if shutdown_requested else 1

        if method == "textDocument/completion":
            send_message(
                {
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "result": {"isIncomplete": False, "items": completion_items()},
                }
            )
            continue

        if method == "textDocument/didOpen":
            params = msg.get("params", {})
            td = params.get("textDocument", {})
            uri = td.get("uri", "")
            text = td.get("text", "")
            docs[uri] = text
            send_message(publish_diagnostics(uri, text))
            continue

        if method == "textDocument/didChange":
            params = msg.get("params", {})
            td = params.get("textDocument", {})
            uri = td.get("uri", "")
            changes = params.get("contentChanges", [])
            if changes:
                text = changes[-1].get("text", "")
                docs[uri] = text
                send_message(publish_diagnostics(uri, text))
            continue

        if msg_id is not None:
            send_message({"jsonrpc": "2.0", "id": msg_id, "result": None})

    return 0


if __name__ == "__main__":
    sys.exit(run_server())

