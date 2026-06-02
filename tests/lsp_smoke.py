#!/usr/bin/env python3
import argparse
import json
import subprocess
from pathlib import Path


def encode_message(payload: dict) -> bytes:
    raw = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode(
        "utf-8"
    )
    return f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii") + raw


def parse_messages(stream: bytes) -> list[dict]:
    # Windows text-mode stdout can expand \n into \r\n even when payload already
    # contains \r\n, yielding \r\r\n sequences.
    stream = stream.replace(b"\r\r\n", b"\r\n")

    out: list[dict] = []
    i = 0
    while i < len(stream):
        header_end = stream.find(b"\r\n\r\n", i)
        if header_end < 0:
            break
        header = stream[i:header_end].decode("utf-8", errors="replace")
        i = header_end + 4

        content_length = None
        for line in header.split("\r\n"):
            if line.lower().startswith("content-length:"):
                content_length = int(line.split(":", 1)[1].strip())
                break
        if content_length is None:
            break
        if i + content_length > len(stream):
            break

        payload = stream[i : i + content_length]
        i += content_length
        out.append(json.loads(payload.decode("utf-8", errors="replace")))
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="Orhun LSP smoke test")
    parser.add_argument("binary", nargs="?", default="./orhun_test", help="Orhun executable")
    parser.add_argument("--timeout", type=float, default=15.0, help="Process timeout (seconds)")
    args = parser.parse_args()

    binary = Path(args.binary)
    if not binary.exists():
        raise SystemExit(f"Binary not found: {binary}")

    uri = "file:///tmp/lsp_smoke.oh"
    source = "\n".join(
        [
            "islev topla(a, b olsun 2):",
            "    dondur a + b",
            "x olsun topla(1, 2)",
            "z olsun x",
            "",
        ]
    )
    broken_line = "islev topla(a, b olsun ):\n"

    payload = b"".join(
        [
            encode_message(
                {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}
            ),
            encode_message(
                {
                    "jsonrpc": "2.0",
                    "method": "textDocument/didOpen",
                    "params": {
                        "textDocument": {
                            "uri": uri,
                            "languageId": "orhun",
                            "version": 1,
                            "text": source,
                        }
                    },
                }
            ),
            encode_message(
                {
                    "jsonrpc": "2.0",
                    "id": 2,
                    "method": "textDocument/definition",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {"line": 2, "character": 10},
                    },
                }
            ),
            encode_message(
                {
                    "jsonrpc": "2.0",
                    "id": 3,
                    "method": "textDocument/hover",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {"line": 2, "character": 10},
                    },
                }
            ),
            encode_message(
                {
                    "jsonrpc": "2.0",
                    "id": 4,
                    "method": "textDocument/signatureHelp",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {"line": 2, "character": 17},
                    },
                }
            ),
            encode_message(
                {
                    "jsonrpc": "2.0",
                    "id": 5,
                    "method": "textDocument/references",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {"line": 3, "character": 8},
                        "context": {"includeDeclaration": True},
                    },
                }
            ),
            encode_message(
                {
                    "jsonrpc": "2.0",
                    "id": 6,
                    "method": "textDocument/completion",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {"line": 4, "character": 0},
                    },
                }
            ),
            encode_message(
                {
                    "jsonrpc": "2.0",
                    "id": 7,
                    "method": "textDocument/rename",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {"line": 3, "character": 8},
                        "newName": "sonuc",
                    },
                }
            ),
            encode_message(
                {
                    "jsonrpc": "2.0",
                    "method": "textDocument/didChange",
                    "params": {
                        "textDocument": {"uri": uri, "version": 2},
                        "contentChanges": [
                            {
                                "range": {
                                    "start": {"line": 0, "character": 0},
                                    "end": {"line": 1, "character": 0},
                                },
                                "text": broken_line,
                            }
                        ],
                    },
                }
            ),
            encode_message(
                {
                    "jsonrpc": "2.0",
                    "id": 8,
                    "method": "textDocument/diagnostic",
                    "params": {"textDocument": {"uri": uri}},
                }
            ),
            encode_message({"jsonrpc": "2.0", "id": 9, "method": "shutdown", "params": {}}),
            encode_message({"jsonrpc": "2.0", "method": "exit", "params": {}}),
        ]
    )

    proc = subprocess.Popen(
        [str(binary), "lsp", "--stdio"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    stdout, stderr = proc.communicate(input=payload, timeout=args.timeout)

    messages = parse_messages(stdout)
    init = next((m for m in messages if m.get("id") == 1), None)
    if init is None or "result" not in init:
        raise SystemExit("LSP smoke failed: initialize response missing")
    caps = init["result"].get("capabilities", {})
    for cap in (
        "completionProvider",
        "hoverProvider",
        "signatureHelpProvider",
        "definitionProvider",
        "referencesProvider",
        "renameProvider",
    ):
        if cap not in caps:
            raise SystemExit(f"LSP smoke failed: capability missing ({cap})")

    def_resp = next((m for m in messages if m.get("id") == 2), None)
    if def_resp is None or "result" not in def_resp:
        raise SystemExit("LSP smoke failed: definition response missing")
    if not isinstance(def_resp["result"], list) or not def_resp["result"]:
        raise SystemExit("LSP smoke failed: definition result empty")

    hover_resp = next((m for m in messages if m.get("id") == 3), None)
    if hover_resp is None or "result" not in hover_resp:
        raise SystemExit("LSP smoke failed: hover response missing")
    if hover_resp["result"] in (None, "null") or "contents" not in hover_resp["result"]:
        raise SystemExit("LSP smoke failed: hover payload missing contents")

    sig_resp = next((m for m in messages if m.get("id") == 4), None)
    if sig_resp is None or "result" not in sig_resp:
        raise SystemExit("LSP smoke failed: signatureHelp response missing")
    signatures = sig_resp["result"].get("signatures", [])
    if not signatures:
        raise SystemExit("LSP smoke failed: signatureHelp signatures empty")

    refs_resp = next((m for m in messages if m.get("id") == 5), None)
    if refs_resp is None or "result" not in refs_resp:
        raise SystemExit("LSP smoke failed: references response missing")
    if not isinstance(refs_resp["result"], list) or not refs_resp["result"]:
        raise SystemExit("LSP smoke failed: references result empty")

    completion_resp = next((m for m in messages if m.get("id") == 6), None)
    if completion_resp is None or "result" not in completion_resp:
        raise SystemExit("LSP smoke failed: completion response missing")
    completion_items = completion_resp["result"].get("items", [])
    labels = {str(item.get("label", "")) for item in completion_items}
    for label in ("yaz", "oku", "aralik", "aralık", "ilk", "son", "dolu_mu"):
        if label not in labels:
            raise SystemExit(f"LSP smoke failed: completion missing {label}")

    rename_resp = next((m for m in messages if m.get("id") == 7), None)
    if rename_resp is None or "result" not in rename_resp:
        raise SystemExit("LSP smoke failed: rename response missing")
    changes = rename_resp["result"].get("changes", {})
    uri_changes = changes.get(uri, [])
    if not uri_changes:
        raise SystemExit("LSP smoke failed: rename changes missing for target uri")
    if uri_changes[0].get("newText") != "sonuc":
        raise SystemExit("LSP smoke failed: rename newText mismatch")

    diag_resp = next((m for m in messages if m.get("id") == 8), None)
    if diag_resp is None or "result" not in diag_resp:
        raise SystemExit("LSP smoke failed: diagnostic response missing")
    items = diag_resp["result"].get("items", [])
    if not items:
        raise SystemExit("LSP smoke failed: expected at least one diagnostic item")

    start = items[0].get("range", {}).get("start", {})
    if "line" not in start or "character" not in start:
        raise SystemExit("LSP smoke failed: diagnostic range is incomplete")

    if proc.returncode not in (0, None):
        err_text = stderr.decode("utf-8", errors="replace")
        raise SystemExit(f"LSP smoke failed: process returncode={proc.returncode}\n{err_text}")

    print("LSP smoke passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
