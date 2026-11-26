---
name: qwen-offload
description: Offload file-level implementation work to local qwen3-coder:30b models via Ollama to save Claude tokens. Claude designs the plan, interfaces and skeletons and writes a precise per-file spec; local workers (HPE-C3V4J0KWRQ.local and localhost, run in parallel) write the code; Claude verifies. Use when a task breaks into several well-contracted files or functions, when the user asks to "offload", "use qwen" or "use the local model", or for bulky boilerplate such as CRUD, parsers, tests, adapters and data classes.
---

# Qwen offload: Claude designs, local qwen writes the code

You (Claude) are the **architect and reviewer**. The local qwen3-coder models
are **implementers**. They are good at filling in a well-specified file and poor
at design, ambiguity and work that spans files. Spend your tokens on the
thinking, and let them spend theirs on typing.

Script: `.claude/skills/qwen-offload/scripts/offload.py`
Hosts: `.claude/skills/qwen-offload/hosts.json` (HPE-C3V4J0KWRQ.local + localhost, 1 slot each)
Spec template: `.claude/skills/qwen-offload/templates/spec.md`

## When to offload and when to do it yourself

Offload a file when:
- its contract can be stated fully (signatures, types, behavior, edge cases)
- it depends only on interfaces that already exist on disk
- it is bulky to write: >~40 lines of straightforward logic, tests, boilerplate

Do it yourself when:
- the work is design, architecture, or tricky concurrency or security logic
- the edit is small (writing the spec would cost more than writing the code)
- the change cuts across many files or needs judgment about existing code

## Workflow

### 1. Plan (Claude)
Break the work into **isolated units, one file each**. For every unit decide:
the file path, its public API, what it imports, and how to verify it. Order
the units so that shared types and interfaces come first.

### 2. Write the contracts and skeletons (Claude)
- Write the **shared interface files yourself**: types, protocols/ABCs,
  dataclasses, constants, error classes. These are the contracts that every
  worker imports, so they must be right.
- For each offloaded file, write a **skeleton** at its real path: imports,
  class and function signatures with full type hints, and docstrings that
  state the behavior, with bodies left as `...` / `pass` /
  `throw new Error("unimplemented")`. The script sends the existing file to
  the worker as the thing to fill in.
- Write a **spec** at `.qwen-offload/specs/<name>.md` from the template. Be
  explicit: the worker cannot ask questions. Cover the edge cases, the errors
  and examples. List the allowed imports, and add "do not redefine X" for
  anything that lives in the context files.

### 3. Dispatch (qwen)
Run a single file:
```bash
python3 .claude/skills/qwen-offload/scripts/offload.py run \
  --spec .qwen-offload/specs/parser.md --out src/parser.py \
  --context src/types.py src/errors.py \
  --check "python3 -m py_compile {out}"
```

To run in parallel across both hosts, write `.qwen-offload/jobs.json`:
```json
[
  {"spec": ".qwen-offload/specs/parser.md", "out": "src/parser.py",
   "context": ["src/types.py"], "check": "python3 -m py_compile {out}"},
  {"spec": ".qwen-offload/specs/store.md", "out": "src/store.py",
   "context": ["src/types.py"], "check": "python3 -m py_compile {out}"}
]
```
```bash
python3 .claude/skills/qwen-offload/scripts/offload.py batch .qwen-offload/jobs.json
```
- Jobs run concurrently, one per host slot. Jobs in a batch must **not depend
  on each other's output**: run dependent layers as separate batches.
- Generation takes minutes. For a big batch use `run_in_background: true`;
  you are notified when it finishes. Meanwhile, write the next layer's specs.
- `python3 .claude/skills/qwen-offload/scripts/offload.py hosts` shows which
  hosts are up and have the model.

**Check commands** (`{out}` is replaced with the file path). The script
retries `--retries` times (default 2) and feeds the check output back to the
worker each time. Use the strongest check that is cheap:
- Python: `python3 -m py_compile {out}`, or `ruff check {out}`, or a unit test: `pytest -q tests/test_x.py`
- TypeScript: `npx tsc --noEmit -p .`, or `npx tsc --noEmit {out}`
- Go: `go build ./... && go vet ./pkg/x`
- Rust: `cargo check -q`
- A test file Claude wrote first (TDD) is the best contract: `pytest -q tests/test_parser.py`

### 4. Verify (Claude)
The script prints one line per job and **never prints the code**, which keeps
your context small. Then:
- Run the real tests and type checks for the project.
- Review economically: read the file only when a check fails or the logic
  carries risk. `git diff --stat` and grepping for the public names are
  usually enough for boilerplate.
- On `CHECK_FAILED`: make a small fix yourself, or tighten the spec and
  re-run. Re-running overwrites the file, and the worker sees its last
  attempt as the "current content".
- Full prompts, responses and token counts are in `.qwen-offload/logs/*.json`.
  Read them only when you are debugging a bad result.

## Spec-writing tips (for good qwen output)
- Paste the exact signatures, and say "must match exactly".
- Name the algorithm and data structures; don't leave choices open.
- Give 2-4 concrete input → output examples.
- State what is out of scope ("no CLI, no logging config, don't modify other files").
- Keep each spec to one file, ideally under ~400 output lines. Split bigger files.
- Context = only what the file imports or must conform to. Smaller is better.

## Config
- `hosts.json`: per host `url`, `model`, `slots`, and optional `temperature`,
  `num_ctx`, `timeout_sec`, `think` (set `"think": false` for qwen3.x
  thinking models). Defaults: temp 0.2, num_ctx 32768, timeout 900s.
- Raise `slots` above 1 only if that Ollama server runs with
  `OLLAMA_NUM_PARALLEL>1`.
- Env overrides: `QWEN_OFFLOAD_HOSTS` (config path), `QWEN_OFFLOAD_LOGS` (log dir).
- All HTTP goes through `curl -4` (IPv4 forced for the `.local` mDNS host).
- Add `.qwen-offload/` to `.gitignore`.
