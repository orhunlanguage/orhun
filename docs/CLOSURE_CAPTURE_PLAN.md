# Closure and Capture Plan

Orhun parses nested functions and anonymous functions, and the interpreter
(`yorumla`), default runner, and `vm-kati` now keep captured local scopes alive,
including loop iteration-local capture cells. The promoted runtime fixture is:

- `tests/cases/closure_missing_feature.oh`

The current closure guard is:

- `tests/known_gap_smoke.py`
- `tests/interpreter_closure_smoke.py`
- `tests/closure_capture_analysis_smoke.py`
- `tests/lambda_capture_analysis_smoke.py`

Before this promotion, VM/default behavior failed at the first captured outer
local:

```text
Tanimsiz degisken: 'adet'.
```

`tests/known_gap_smoke.py` now checks that the former known-gap fixture keeps
passing in the default runner and `vm-kati`. `tests/interpreter_closure_smoke.py`
checks the same capture behavior through `yorumla`.

## Target Behavior

The intended behavior is:

```text
--- Test 1: Basit Closure ---
1
2
1
3
--- Test 2: Ic Ice Closure ve Golgeleme ---
global
dis
ic
parametre
--- Test 3: Closure ile Degisken Degistirme ---
150
130
140
--- Test 4: Dongu Icinde Closure ---
0
1
2
```

This target implies:

- Returned functions keep captured outer locals alive.
- Separate calls create separate captured environments.
- Multiple closures returned from the same outer call share mutable captured
  cells when they close over the same variable.
- Nested functions can read globals, outer locals, local variables, and
  parameters with normal shadowing rules.
- Loop-local copies such as `x olsun i` should produce independent values for
  returned closures.

## Existing Behavior To Migrate

`tests/cases/lambda_capture_shadow.oh` now documents the promoted capture
behavior: a returned lambda sees the outer function's captured local instead of
falling back to a global with the same name.

The migration is documented in `docs/MIGRATION_GUIDE.md`.

## Implementation Slices

1. Define capture semantics in `docs/SPEC.md`. Done as the target contract.
2. Implement interpreter capture cells first to clarify semantics. Done for
   named functions, anonymous functions, shared mutation, shadowing, and loop
   iteration-local captures.
3. Implement VM closure/upvalue storage with GC marking. Done for the promoted
   closure fixture and returned-lambda fixtures.
4. Promote the known-gap fixture once VM and interpreter behavior match. Done.
5. Add parity tests for nested functions, anonymous functions, mutation, and
   loop captures.

`tests/closure_capture_analysis_smoke.py` already locks the static capture
candidates for the promoted closure fixture, including counter mutation, nested
parameter capture, shared account balance capture, and loop-local copies.
`tests/lambda_capture_analysis_smoke.py` separately locks anonymous-function
capture candidates for shadowing, returned lambdas, and top-level lambda
composition.
Both smoke tests also track which captured names are written inside the nested
function, so mutable capture cells can be implemented against a fixed target.
They also lock the lexical capture depth for each name, currently depth `1`
for direct parent captures and depth `2` for the nested lambda fixture
`tests/ast_json/lambda_capture_depth.oh` and the nested named-function fixture
`tests/ast_json/closure_capture_depth.oh`.
