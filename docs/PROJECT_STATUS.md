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
| Full self-hosting / independent compiler path | ~22% | Orhun lexer/parser prototypes and the first bytecode compiler subset are tested against C++; broad compiler/runtime replacement remains. |

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
- Orhun-written lexer/parser prototypes and the first bytecode compiler subset
  with parity smoke tests.
- Beginner-friendly `yaz` print alias, `oku` input alias, global
  `aralik`/`aralık` range helper, and simple collection helpers without
  reserving those words as keywords.

## Main Remaining Work

- Stabilize the language specification and migration policy.
- Keep growing parser parity until the Orhun parser can replace more of the C++
  parser path.
- Grow Orhun-written bytecode compiler parity beyond its initial constants,
  globals, binary operations, and print subset.
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
