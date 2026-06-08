# Orhun Project Status

This document gives a human-readable snapshot of where Orhun is today and what
still has to become true before it can be treated as a stable, production-ready
language.

## Current Version

- Public version: `0.8.0`
- Stability: experimental / pre-1.0
- README current-version line: `0.8.0`
- Version source of truth: `VERSION`

Normal development commits do not change the public version number. Version
bumps should happen in release commits after the relevant gates are met.

## Progress Estimate

These percentages are planning estimates, not promises.

| Target | Estimated Progress | Meaning |
| --- | ---: | --- |
| Working experimental language / MVP | 55-60% | Orhun already has a lexer, parser, interpreter, bytecode compiler, VM, stdlib surface, package/security flows, tests, and tooling. |
| 1.0 stable language | 35-40% | Needs a stable spec, compatibility policy, release binaries, cleaner docs, stronger package flow, and hardened performance/security gates. |
| 2.1.0 production-ready product bar | 20-25% | Needs 1.0 stability plus ecosystem confidence: installers, docs, examples, package policy, support process, performance gates, and broad CI/nightly coverage. |
| Full self-hosting / independent compiler path | ~42% | The Orhun-written compiler has exact bytecode parity across every current runtime case accepted by C++, can compile its own source into a byte-identical bootstrap artifact, and its compiler/parser/lexer module chain runs from source-free precompiled modules through a dedicated bootstrap compile CLI. A standalone compiler executable and runtime replacement remain. |

## What Is Already Real

- Turkish-first syntax and diagnostics.
- UTF-8 identifiers and Turkish keywords.
- Interpreter and strict VM execution paths.
- Bytecode compiler.
- Functions, lambdas, default arguments, classes, inheritance, collections,
  slicing, safe access, and error handling.
- File, JSON, regex, date/time, database helper, server, task, FFI, and system
  policy surfaces.
- Formatter, linter, LSP, VS Code tooling, package/lock verification, and CI.
- Orhun-written lexer/parser prototypes and a bytecode compiler subset covering
  expressions, collections, control flow, functions, closures, lambdas,
  list comprehensions, parallel task plans, external declarations, class
  fields/methods/inheritance, locals, and optimizations with parity smoke tests.
- Exact Orhun/C++ compiler bytecode parity across all current C++-compileable
  runtime cases, guarded by a full-case sweep.
- Strict decoded-bytecode execution bridge from the Orhun-written compiler to
  the C++ VM, guarded by end-to-end bootstrap tests.
- Experimental single-command `orhun-vm` path through the Orhun-written
  compiler and validated C++ VM bridge.
- Experimental `orhun-derle` artifact path with byte-identical `.obc` output
  against the C++ compiler for guarded bootstrap fixtures.
- Bootstrap self-source compile: `StdLib/orhun/derleyici.oh` compiles through
  `orhun-derle` into the same `.obc` bytes as the C++ compiler.
- Explicit `obc-only` module mode runs the precompiled Orhun
  compiler/parser/lexer chain without requiring `.oh` sources or silently
  falling back to C++ source compilation.
- `bootstrap-hazirla` produces that source-free three-module toolchain and a
  CRC-bearing machine-readable manifest in one command.
- `bootstrap-dogrula` validates the complete prepared-toolchain contract,
  payload integrity, and OBC structure before distribution or execution.
- `bootstrap-derle` consumes a prepared toolchain in strict `obc-only` mode
  without requiring environment-variable setup.
- `bootstrap-calistir` uses the same prepared toolchain contract for a
  source-free compiler-module run path.
- Beginner-friendly `yaz` print alias, `oku` input alias, global
  `aralik`/`aralık` range helper, and simple collection helpers without
  reserving those words as keywords.
- Consistent `sistem.argumanlar` program arguments across direct VM,
  interpreter, OBC, packaged executable, and bootstrap execution paths.

## Main Remaining Work

- Stabilize the language specification and migration policy.
- Keep growing parser parity until the Orhun parser can replace more of the C++
  parser path.
- Grow compiler parity beyond the current test corpus and connect the
  Orhun-written compiler output to an executable bootstrap pipeline.
- Make release binaries easy on Windows, Linux, and macOS.
- Strengthen package manager UX, security checks, lockfile behavior, and docs.
- Add beginner learning material and larger example projects.
- Add performance gates for representative workloads.
- Keep compatibility and deprecation rules credible before 1.0 and especially
  before 2.1.0.

## 2.1.0 Rule

Do not call Orhun `2.1.0` just because the number looks mature. `2.1.0` should
mean users can start real projects with reasonable confidence. The gates in
`docs/VERSIONING.md` must be met first.
