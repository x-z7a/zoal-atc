# Spec: <path/to/file>

## Purpose
<1-3 sentences: what this file does and where it sits in the system.>

## Language / runtime
<e.g. Python 3.11, stdlib only | TypeScript 5, Node 20, ESM>

## Allowed imports / dependencies
- <module> — <what to use from it>
- Do NOT import: <anything off-limits>

## Public contract (must match exactly)
```<lang>
<signatures, types, classes, constants — copy from the skeleton>
```

## Behavior
For each public item:
- `<name>`: <inputs → outputs, invariants, side effects>
  - Edge cases: <empty input, None, overflow, missing key, ...>
  - Errors: <raise/throw what, when, with what message>

## Algorithm / implementation notes
<Anything non-obvious the implementer must follow: data structure choice,
complexity bound, ordering, concurrency rules, formats. Be explicit — the
implementer cannot ask questions.>

## Examples
```<lang>
<input → expected output, or a few asserts>
```

## Out of scope
- <things NOT to do: no logging setup, no CLI, don't touch other files>
