#!/usr/bin/env python3
import json
import re
import sys
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple


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

IDENTIFIER_RE = re.compile(r"[A-Za-z_ÇĞİÖŞÜçğıöşü][\wÇĞİÖŞÜçğıöşü]*", re.UNICODE)


@dataclass
class Symbol:
    name: str
    kind: int
    line: int
    character: int
    uri: str


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


def build_symbols(uri: str, text: str) -> List[Symbol]:
    symbols: List[Symbol] = []
    lines = text.splitlines()

    assign_re = re.compile(
        r"^\s*([A-Za-z_ÇĞİÖŞÜçğıöşü][\wÇĞİÖŞÜçğıöşü]*)\s*(?:olsun|=)\b",
        re.UNICODE,
    )
    fn_re = re.compile(
        r"^\s*işlev\s+([A-Za-z_ÇĞİÖŞÜçğıöşü][\wÇĞİÖŞÜçğıöşü]*)", re.UNICODE
    )
    ext_fn_re = re.compile(
        r"^\s*(?:dış_işlev|dis_islev)\s+([A-Za-z_ÇĞİÖŞÜçğıöşü][\wÇĞİÖŞÜçğıöşü]*)",
        re.UNICODE,
    )
    class_re = re.compile(
        r"^\s*tip\s+([A-Za-z_ÇĞİÖŞÜçğıöşü][\wÇĞİÖŞÜçğıöşü]*)", re.UNICODE
    )

    for line_no, line in enumerate(lines):
        for regex, kind in ((class_re, 5), (fn_re, 12), (ext_fn_re, 12), (assign_re, 13)):
            m = regex.search(line)
            if not m:
                continue
            name = m.group(1)
            idx = line.find(name)
            symbols.append(
                Symbol(
                    name=name,
                    kind=kind,
                    line=line_no,
                    character=max(0, idx),
                    uri=uri,
                )
            )
            break

    return symbols


def symbol_information(uri: str, symbols: List[Symbol]) -> List[Dict[str, Any]]:
    out: List[Dict[str, Any]] = []
    for sym in symbols:
        out.append(
            {
                "name": sym.name,
                "kind": sym.kind,
                "location": {
                    "uri": uri,
                    "range": {
                        "start": {"line": sym.line, "character": sym.character},
                        "end": {
                            "line": sym.line,
                            "character": sym.character + len(sym.name),
                        },
                    },
                },
            }
        )
    return out


def word_at_position(text: str, line: int, character: int) -> Optional[Tuple[str, int]]:
    lines = text.splitlines()
    if line < 0 or line >= len(lines):
        return None
    current = lines[line]
    if character < 0:
        return None
    if character >= len(current):
        character = max(0, len(current) - 1)

    for m in IDENTIFIER_RE.finditer(current):
        if m.start() <= character < m.end():
            return m.group(0), m.start()
    return None


def find_definition(
    query: str,
    current_uri: str,
    current_line: int,
    symbols_by_uri: Dict[str, List[Symbol]],
) -> Optional[Symbol]:
    symbols = symbols_by_uri.get(current_uri, [])
    same_doc = [s for s in symbols if s.name == query]
    # Önce aynı belgede en yakın önceki tanımı hedefle.
    before = [s for s in same_doc if s.line <= current_line]
    if before:
        return sorted(before, key=lambda s: (s.line, s.character), reverse=True)[0]
    if same_doc:
        return same_doc[0]

    for uri, uri_symbols in symbols_by_uri.items():
        if uri == current_uri:
            continue
        for sym in uri_symbols:
            if sym.name == query:
                return sym
    return None


def run_server() -> int:
    docs: Dict[str, str] = {}
    symbols_by_uri: Dict[str, List[Symbol]] = {}
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
                            "definitionProvider": True,
                            "documentSymbolProvider": True,
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
            symbols_by_uri[uri] = build_symbols(uri, text)
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
                symbols_by_uri[uri] = build_symbols(uri, text)
                send_message(publish_diagnostics(uri, text))
            continue

        if method == "textDocument/didClose":
            params = msg.get("params", {})
            td = params.get("textDocument", {})
            uri = td.get("uri", "")
            docs.pop(uri, None)
            symbols_by_uri.pop(uri, None)
            send_message(
                {
                    "jsonrpc": "2.0",
                    "method": "textDocument/publishDiagnostics",
                    "params": {"uri": uri, "diagnostics": []},
                }
            )
            continue

        if method == "textDocument/documentSymbol":
            params = msg.get("params", {})
            td = params.get("textDocument", {})
            uri = td.get("uri", "")
            symbols = symbols_by_uri.get(uri, [])
            send_message(
                {
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "result": symbol_information(uri, symbols),
                }
            )
            continue

        if method == "textDocument/definition":
            params = msg.get("params", {})
            td = params.get("textDocument", {})
            uri = td.get("uri", "")
            pos = params.get("position", {})
            line = int(pos.get("line", 0))
            character = int(pos.get("character", 0))

            text = docs.get(uri, "")
            at = word_at_position(text, line, character)
            if at is None:
                send_message({"jsonrpc": "2.0", "id": msg_id, "result": None})
                continue

            query, _ = at
            found = find_definition(query, uri, line, symbols_by_uri)
            if found is None:
                send_message({"jsonrpc": "2.0", "id": msg_id, "result": None})
                continue

            send_message(
                {
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "result": {
                        "uri": found.uri,
                        "range": {
                            "start": {
                                "line": found.line,
                                "character": found.character,
                            },
                            "end": {
                                "line": found.line,
                                "character": found.character + len(found.name),
                            },
                        },
                    },
                }
            )
            continue

        if msg_id is not None:
            send_message({"jsonrpc": "2.0", "id": msg_id, "result": None})

    return 0


if __name__ == "__main__":
    sys.exit(run_server())
