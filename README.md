# Orhun

[![Orhun CI](https://github.com/orhunlanguage/orhun/actions/workflows/ci.yml/badge.svg)](https://github.com/orhunlanguage/orhun/actions/workflows/ci.yml)

Orhun is an actively developed, Turkish-first programming language runtime.
It is experimental, but already has a working lexer, parser, interpreter,
bytecode compiler, VM execution path, standard-library modules, package
verification flows, and editor tooling.

The long-term goal is clear: Orhun starts with a C++ bootstrap core, then moves
toward self-hosting so more of the compiler and tooling can be written in Orhun
itself.

Repository: https://github.com/orhunlanguage/orhun

## Status

Orhun is under active development.

- Current version: `0.8.0`
- Stability: experimental / pre-1.0
- Primary implementation language: C++17
- Source extension: `.oh`
- Default language identity: Turkish-first
- Long-term direction: self-hosting compiler and independent toolchain

Do not treat Orhun as production-stable yet. The project is currently focused on
language semantics, VM parity, reliability, developer experience, and the path
toward self-hosting.

For a more concrete progress snapshot and the current estimate toward `1.0` /
`2.1.0`, see [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md).

## What Orhun Provides

- Turkish-first syntax and diagnostics
- UTF-8 identifiers and Turkish keywords
- Interpreter and VM execution paths
- Bytecode compiler and strict VM mode
- Functions, default arguments, lambdas, lists, dictionaries, classes, and
  inheritance
- Error handling with `deneme` / `yakala`
- Safe access with `?.`
- Async/task primitives and `paralel yap`
- Standard modules for files, JSON, regex, date/time, simple database helpers,
  server helpers, FFI, and system policy controls
- Package install and lock verification flows
- Formatter, linter, LSP, and VS Code language tooling
- Cross-platform CI for Windows, Linux, and macOS

## Example

```orhun
yaz "Merhaba Orhun"

tip Selamlayici:
    işlev selam(ad olsun "dünya"):
        döndür "Merhaba, " + ad

s olsun yeni Selamlayici()
yazdır s.selam()
yazdır s.selam("Orhun")
```

Expected output:

```text
Merhaba Orhun
Merhaba, dünya
Merhaba, Orhun
```

## Build From Source

Requirements:

- C++17 compiler (`g++` or `clang++`)
- PowerShell on Windows for the default test script
- Python 3 for smoke, benchmark, and tooling tests

Windows:

```powershell
g++ -std=c++17 -Wall -Wextra main.cpp Lexer.cpp Parser.cpp Interpreter.cpp Chunk.cpp Compiler.cpp VM.cpp -o orhun.exe
```

Linux/macOS:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp Lexer.cpp Parser.cpp Interpreter.cpp Chunk.cpp Compiler.cpp VM.cpp -o orhun
```

## Quick Start

Run a source file:

```bash
orhun examples/merhaba.oh
```

Read one line of input:

```orhun
ad olsun oku("Adın? ")
yaz "Merhaba, " + ad
```

Create a simple range:

```orhun
yaz aralik(5)
yaz aralık(5, 1, -2)
yaz ilk([3, 4])
yaz son([3, 4])
```

Strict VM mode:

```bash
orhun vm-kati dosya.oh
```

Interpreter-only mode for runtime diagnostics:

```bash
orhun yorumla dosya.oh
```

Project health summary:

```bash
orhun doctor
orhun doctor --json
```

Useful commands:

```bash
orhun fmt dosya.oh
orhun lint dosya.oh
orhun lex dosya.oh --json
orhun parse dosya.oh --json
orhun hiz dosya.oh --json
orhun lsp --stdio
orhun paket dogrula
```

## Test

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1 -Compiler g++ -Output build/orhun_test.exe
```

Linux/macOS:

```bash
bash tests/run_tests.sh g++ build/orhun_test
```

The test runner applies per-case timeouts so one broken fixture cannot hang the
whole suite.

## Project Direction

Near-term priorities:

1. Keep the VM path reliable and strict-mode compatible.
2. Stabilize language semantics before adding large new syntax.
3. Improve documentation and first-run developer experience.
4. Move safe parts of the standard library into Orhun source.
5. Build toward an Orhun-written lexer, parser, and compiler.

See [docs/SELF_HOSTING_ROADMAP.md](docs/SELF_HOSTING_ROADMAP.md) for the
self-hosting plan.

## Repository Guide

- Core runtime and CLI: `main.cpp`, `VM.cpp`, `Interpreter.cpp`
- Lexer/parser/AST: `Lexer.cpp`, `Parser.cpp`, `AST.h`
- Bytecode compiler: `Compiler.cpp`
- Standard library helpers: `Yerlesik.h`, `StdLib/`, `StdLib/orhun/`
  (`temel.oh`, `sonuc.oh`, `koleksiyon.oh`, `metin.oh`, `paket.oh`,
  `lexer.oh`, `parser.oh`)
- Examples: `examples/`
- Tests and fixtures: `tests/`
- Lexer parity fixtures: `tests/lexer_parity/`
- Parser AST JSON fixtures: `tests/ast_json/`
- Roadmap smoke aggregate: `tests/roadmap_smoke.py`
- Parser prototype parity smoke: `tests/parser_prototype_smoke.py`
- Closure capture analysis smoke: `tests/closure_capture_analysis_smoke.py`
- Lambda capture analysis smoke: `tests/lambda_capture_analysis_smoke.py`
- Interpreter closure smoke: `tests/interpreter_closure_smoke.py`
- VM closure extra smoke: `tests/vm_closure_extra_smoke.py`
- LSP smoke: `tests/lsp_smoke.py`
- VS Code syntax smoke: `tests/vscode_syntax_smoke.py`
- VS Code tooling: `tools/vscode-orhun`
- Migration notes: `docs/MIGRATION_GUIDE.md`
- Language specification: `docs/SPEC.md`
- Closure/capture plan: `docs/CLOSURE_CAPTURE_PLAN.md`
- Versioning policy: `docs/VERSIONING.md`
- Release channels: `docs/RELEASE_CHANNELS.md`
- Security policy: `SECURITY.md`

## Contributing

Contributions are welcome, especially around tests, documentation, examples,
language semantics, VM parity, and tooling. Because Orhun is pre-1.0, please keep
changes small, tested, and aligned with the Turkish-first identity of the
language. See [CONTRIBUTING.md](CONTRIBUTING.md) and
[docs/OPEN_SOURCE_POLICY.md](docs/OPEN_SOURCE_POLICY.md) for what belongs in
the public repository.

Before opening a pull request:

```bash
bash tests/run_tests.sh g++ build/orhun_test
```

On Windows:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1 -Compiler g++ -Output build/orhun_test.exe
```

## Security

Please report security issues privately first:

```text
orhunlang@gmail.com
```

See [SECURITY.md](SECURITY.md) for details.

## License

Orhun is released under the MIT License. See [LICENSE](LICENSE).
