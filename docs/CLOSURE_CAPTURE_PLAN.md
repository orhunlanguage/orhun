# Closure and Capture Plan

Orhun currently parses nested functions and anonymous functions. The
interpreter (`yorumla`) now keeps captured local scopes alive, including loop
iteration-local capture cells. The VM/default runner still tracks closure
capture as a known gap. The active fixture is:

- `tests/cases/closure_missing_feature.oh`

The current known-gap guard is:

- `tests/known_gap_smoke.py`
- `tests/closure_capture_analysis_smoke.py`
- `tests/lambda_capture_analysis_smoke.py`

Current VM/default behavior fails at the first captured outer local:

```text
Tanimsiz degisken: 'adet'.
```

`tests/known_gap_smoke.py` checks this remains a controlled runtime error in
the default runner and `vm-kati`, so closure work does not regress into a VM
crash while VM upvalues are still pending.

`tests/interpreter_closure_smoke.py` checks that `yorumla` already matches the
target behavior for returned named functions, returned anonymous functions,
shared mutable captures, shadowing, and loop-local captures.

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

`tests/cases/lambda_capture_shadow.oh` still documents the VM/default
pre-capture behavior where a returned lambda resolves `x` through the global
scope after the outer function has returned. The interpreter smoke documents
the migration target: the returned lambda sees the outer function's captured
local instead.

When closure capture lands, update that fixture and document the migration in
`docs/MIGRATION_GUIDE.md`.

## Implementation Slices

1. Define capture semantics in `docs/SPEC.md`. Done as the target contract;
   VM implementation is still pending.
2. Implement interpreter capture cells first to clarify semantics. Done for
   named functions, anonymous functions, shared mutation, shadowing, and loop
   iteration-local captures.
3. Implement VM closure/upvalue storage with GC marking.
4. Promote the known-gap fixture once VM and interpreter behavior match.
5. Add parity tests for nested functions, anonymous functions, mutation, and
   loop captures.

`tests/closure_capture_analysis_smoke.py` already locks the static capture
candidates for the known-gap fixture, including counter mutation, nested
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
