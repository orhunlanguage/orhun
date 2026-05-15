# Contributing To Orhun

Thank you for helping Orhun grow. Orhun is a Turkish-first programming language
runtime, and the project is still pre-1.0, so small, tested changes are the best
way to move quickly without losing reliability.

## What To Contribute

Good contribution areas:

- language semantics and VM reliability
- Orhun-source standard library modules under `StdLib/orhun/`
- Turkish-first diagnostics and documentation
- tests, fixtures, and CI hardening
- editor, formatter, linter, and LSP improvements
- examples that show real Orhun usage

Avoid large rewrites unless they are discussed first.

## Before Opening A Pull Request

Run the test suite:

```powershell
.\tests\run_tests.ps1 -TimeoutSeconds 15
.\tests\vm_parity.ps1 -Output build/orhun_test.exe -TimeoutSeconds 15
```

On Linux/macOS:

```bash
bash tests/run_tests.sh g++ build/orhun_test
bash tests/vm_parity.sh g++ build/orhun_test
```

For documentation-only changes, explain that tests were not run and why.

## Style

- Keep Turkish-first language identity intact.
- Prefer clear Turkish names in Orhun examples and standard modules.
- Add or update tests for behavior changes.
- Keep generated files, local build outputs, credentials, and reports out of git.
- Update `docs/SPEC.md` when a behavior becomes part of the language contract.

## Security

Do not open public issues for vulnerabilities. Report them privately first:

```text
orhunlang@gmail.com
```

See `SECURITY.md` for details.
