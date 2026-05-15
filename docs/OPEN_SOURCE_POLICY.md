# Orhun Open Source Policy

Orhun is intended to be developed in the open, similar in spirit to projects
like CPython: the implementation, tests, standard library, examples,
documentation, and CI configuration should be public.

Open source does not mean publishing secrets or operational access. The public
repository should contain everything needed to inspect, build, test, and improve
Orhun, but not private credentials or sensitive infrastructure details.

## Public In Git

These belong in the repository:

- C++ runtime, compiler, VM, parser, and CLI source files
- Orhun source modules under `StdLib/orhun/`
- tests, fixtures, smoke tests, and benchmark source code
- examples and documentation
- GitHub Actions workflows
- issue templates, pull request templates, and contribution guidelines
- license, security policy, roadmap, and release notes

## Not Public In Git

These must stay out of the repository:

- passwords, API keys, tokens, cookies, and session files
- signing keys, release credentials, package registry credentials
- private vulnerability reports before coordinated disclosure
- local `.env` files and machine-specific editor settings
- generated binaries, build directories, coverage output, benchmark result logs
- personal notes that are not intended as project documentation

## Generated Artifacts

Generated outputs should be reproducible from source. If CI needs to keep an
artifact, upload it as a CI artifact instead of committing it to git.

Examples:

- commit `tests/benchmark.sh`
- do not commit local `build/benchmark_results.jsonl`
- commit `.github/workflows/ci.yml`
- do not commit local coverage folders

## Security Reports

Security issues should be reported privately to `orhunlang@gmail.com`. After a
fix is available, a public advisory or changelog entry can summarize the impact
without exposing unrelated private details.

## Practical Rule

Push the parts that help someone build, test, review, learn, or contribute to
Orhun. Do not push anything that grants access, identifies a private machine, or
cannot be regenerated from source.
