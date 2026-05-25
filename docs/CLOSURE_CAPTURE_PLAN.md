# Closure and Capture Plan

Orhun currently parses nested functions and anonymous functions, but full
closure capture semantics are not locked yet. The active known-gap fixture is:

- `tests/cases/closure_missing_feature.oh`

The current known-gap guard is:

- `tests/known_gap_smoke.py`

Current VM/interpreter behavior fails at the first captured outer local:

```text
Tanimsiz degisken: 'adet'.
```

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

## Implementation Slices

1. Define capture semantics in `docs/SPEC.md`.
2. Promote the known-gap fixture once VM and interpreter behavior match.
3. Implement interpreter capture cells first to clarify semantics.
4. Implement VM closure/upvalue storage with GC marking.
5. Add parity tests for nested functions, anonymous functions, mutation, and
   loop captures.
