#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path
from urllib.parse import unquote, urlsplit


def encode_message(payload: dict) -> bytes:
    raw = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii") + raw


def parse_messages(stream: bytes) -> list[dict]:
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
        if content_length is None or i + content_length > len(stream):
            break
        payload = stream[i : i + content_length]
        i += content_length
        out.append(json.loads(payload.decode("utf-8", errors="replace")))
    return out


def normalized_file_uri(uri: str) -> str:
    parsed = urlsplit(uri)
    if parsed.scheme.lower() != "file":
        return uri
    path = unquote(parsed.path)
    if os.name == "nt" and len(path) >= 3 and path[0] == "/" and path[2] == ":":
        path = path[1:]
    return os.path.normcase(os.path.normpath(path))


def main() -> int:
    parser = argparse.ArgumentParser(description="Orhun LSP workspace-root smoke")
    parser.add_argument("binary", nargs="?", default="./orhun_test", help="Orhun executable")
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()

    binary = Path(args.binary)
    if not binary.exists():
        raise SystemExit(f"Binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="orhun_lsp_ws_") as tmp:
        root = Path(tmp)
        def_file = root / "a.oh"
        use_file = root / "b.oh"

        def_file.write_text(
            "\n".join(
                [
                    "islev dis_fonk(a):",
                    "    dondur a + 1",
                    "",
                ]
            ),
            encoding="utf-8",
        )
        use_file.write_text("x olsun dis_fonk(2)\n", encoding="utf-8")

        def_uri = def_file.resolve().as_uri()
        use_uri = use_file.resolve().as_uri()
        source = use_file.read_text(encoding="utf-8")

        payload = b"".join(
            [
                encode_message({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}),
                encode_message(
                    {
                        "jsonrpc": "2.0",
                        "method": "textDocument/didOpen",
                        "params": {
                            "textDocument": {
                                "uri": use_uri,
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
                        "method": "workspace/symbol",
                        "params": {"query": "dis_fonk"},
                    }
                ),
                encode_message(
                    {
                        "jsonrpc": "2.0",
                        "id": 3,
                        "method": "textDocument/definition",
                        "params": {
                            "textDocument": {"uri": use_uri},
                            "position": {"line": 0, "character": 12},
                        },
                    }
                ),
                encode_message({"jsonrpc": "2.0", "id": 4, "method": "shutdown", "params": {}}),
                encode_message({"jsonrpc": "2.0", "method": "exit", "params": {}}),
            ]
        )

        proc = subprocess.Popen(
            [str(binary), "lsp", "--stdio", "--workspace-root", str(root)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        stdout, stderr = proc.communicate(input=payload, timeout=args.timeout)
        messages = parse_messages(stdout)

        init = next((m for m in messages if m.get("id") == 1), None)
        if init is None or "result" not in init:
            raise SystemExit("workspace smoke failed: initialize response missing")

        ws = next((m for m in messages if m.get("id") == 2), None)
        if ws is None or "result" not in ws:
            raise SystemExit("workspace smoke failed: workspace/symbol response missing")
        ws_items = ws["result"]
        if not isinstance(ws_items, list) or not ws_items:
            raise SystemExit("workspace smoke failed: workspace/symbol empty")
        names = [str(item.get("name", "")) for item in ws_items]
        if "dis_fonk" not in names:
            raise SystemExit("workspace smoke failed: symbol not indexed from workspace")

        definition = next((m for m in messages if m.get("id") == 3), None)
        if definition is None or "result" not in definition:
            raise SystemExit("workspace smoke failed: definition response missing")
        result = definition["result"]
        if not isinstance(result, list) or not result:
            raise SystemExit("workspace smoke failed: definition result empty")
        uris = [str(item.get("uri", "")) for item in result]
        expected_uri = normalized_file_uri(def_uri)
        if expected_uri not in {normalized_file_uri(uri) for uri in uris}:
            raise SystemExit(
                "workspace smoke failed: definition did not resolve to workspace file "
                f"(expected={def_uri}, actual={uris})"
            )

        if proc.returncode not in (0, None):
            err_text = stderr.decode("utf-8", errors="replace")
            raise SystemExit(f"workspace smoke failed: returncode={proc.returncode}\n{err_text}")

    print("LSP workspace smoke passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
