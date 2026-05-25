# Orhun Versioning

Orhun follows release-oriented semantic versioning.

## Current Stage

- Current version: `0.8.0`
- Stability: experimental, pre-1.0
- Version source of truth: `VERSION`, `Interpreter.h` (`ORHUN_SURUM`), and the
  README current-version line must match.

Pre-1.0 versions may change language behavior, but every breaking change should
be documented in `docs/MIGRATION_GUIDE.md`.

## When The Version Increases

- Patch version, for example `0.8.1`: bug fixes, test hardening,
  documentation corrections, CI reliability, and narrow compatibility fixes.
- Minor version, for example `0.9.0`: meaningful language/runtime capability,
  parser or VM parity milestones, packaging improvements, or standard-library
  growth.
- Major version, for example `1.0.0`: stable language contract, documented
  compatibility policy, release binaries, and a tested upgrade path.

Version numbers should change in a release commit, not after every normal
development commit.

## 2.1.0 Product Bar

`2.1.0` should mean Orhun is no longer just a beta-style experiment. Before
calling a release `2.1.0`, the project should have:

- Stable specification and migration policy.
- Release binaries for Windows, Linux, and macOS.
- Green CI and nightly test matrix.
- Package manager flow with lockfile/security checks.
- Beginner documentation and a reference manual.
- Clear known-issues list and security policy.
- Performance gates for representative workloads.
- Backward-compatible standard-library policy.

The version number alone will not make Orhun production-ready; these gates are
what make the number credible.
