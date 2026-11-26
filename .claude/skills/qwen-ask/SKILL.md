---
name: qwen-ask
description: Offload small, generic text-in/text-out busywork to a local qwen model to save Claude tokens — rewording, summarizing a pasted blob, drafting commit messages or boilerplate prose, reformatting lists/tables, extracting fields from text, generating sample data or name ideas. Use only when the task is self-contained, low stakes, tedious, and needs no file edits or code understanding; or when the user says "ask qwen" / "ask the local model". For implementing code files, use qwen-offload instead.
---

# Qwen ask: one-shot text busywork on a local model

Script: `.claude/skills/qwen-ask/scripts/ask.py`
Hosts: `.claude/skills/qwen-ask/hosts.json`, tried in order, falling back on failure:
`hpe` remote Ollama (HPE-C3V4J0KWRQ.local, qwen3.5:9b) → `llamacpp` (127.0.0.1:8080, Qwen3.5-4B) → `local` Ollama (qwen3.6:35b-a3b).

## Delegate only when ALL of these hold
- **Text in, text out**: no file edits, and no reasoning about code semantics
- **Self-contained**: the prompt is short and makes sense on its own (the model has no context)
- **Low stakes**: an answer that is slightly off is acceptable, or cheap to spot
- **Tedious busywork**: you would spend more tokens writing it than reviewing it

If any one fails, do it yourself. Never send secrets, credentials or private
data the user hasn't cleared. The hosts are local, but keep prompts minimal anyway.

## Usage
```bash
python3 .claude/skills/qwen-ask/scripts/ask.py "Rewrite as a terse commit message: ..."
python3 .claude/skills/qwen-ask/scripts/ask.py --system "Output only a markdown table." < notes.txt
python3 .claude/skills/qwen-ask/scripts/ask.py --file CHANGELOG.md "Summarize the 3 biggest changes in one line each."
```
Options: `--system`, `--file PATH` (repeatable, appended to the prompt), `--max-tokens` (2048),
`--temperature` (0.3), `--timeout` (180s), `--enable-thinking`, `--host NAME` (only that host),
`--url BASE [--backend openai|ollama] --model M` (one-off server), `QWEN_ASK_HOSTS` (config path).

Only the answer goes to stdout. Diagnostics go to stderr. Exit codes: 0 ok, 1 all hosts failed, 2 bad input.

## Prompting tips
- State the output format exactly ("only the rewritten text, no preamble").
- Give one example when the format matters.
- Batch similar items into one call (e.g. "rewrite each of these 20 lines") instead of 20 calls.
- Skim the result before using it. If it's wrong twice, stop and do it yourself.
