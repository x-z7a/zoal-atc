#!/usr/bin/env python3
"""Offload file implementation tasks to local Ollama (qwen3-coder) workers.

Claude writes a task spec (contract + interfaces); this script sends it, with
any context files, to an Ollama host, extracts the code, writes the target
file, optionally runs a check command and retries with the error output.

Output to stdout is deliberately terse (one summary line per job) so the
calling agent spends few tokens. Full prompts/responses go to the log dir.

Usage:
  offload.py run   --spec SPEC.md --out path/to/file.py [--context a.py b.py]
                   [--check "python -m py_compile {out}"] [--retries 2]
                   [--host NAME] [--model MODEL]
  offload.py batch JOBS.json [--retries 2]
  offload.py hosts               # show hosts + whether model is available

JOBS.json:
  [{"spec": "specs/foo.md", "out": "src/foo.py",
    "context": ["src/types.py"], "check": "python -m py_compile {out}"}, ...]
"""
import argparse
import concurrent.futures as cf
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

SKILL_DIR = Path(__file__).resolve().parent.parent
CONFIG_PATH = Path(os.environ.get("QWEN_OFFLOAD_HOSTS", SKILL_DIR / "hosts.json"))
LOG_DIR = Path(os.environ.get("QWEN_OFFLOAD_LOGS", ".qwen-offload/logs"))

SYSTEM_PROMPT = """You are a senior software engineer implementing ONE file \
inside a larger project. An architect has already designed the system and \
given you a precise contract.

Rules:
- Implement exactly the contract in the spec: same file path, same public \
names, signatures, types, and behavior. Do not rename or add public APIs.
- Context files are read-only references (interfaces, types, neighbors). \
Import from them as the spec says; never redefine what they define.
- If the spec includes a skeleton, keep its structure and fill in every \
TODO / `...` / `pass` / `throw new Error("unimplemented")` body.
- Write complete, working, production-quality code. No placeholders, no \
"rest of code here", no omitted sections.
- Handle the edge cases and errors listed in the spec.
- Output ONLY the complete file content in a single fenced code block. \
No explanation before or after."""


def load_config():
    cfg = json.loads(CONFIG_PATH.read_text())
    return cfg.get("defaults", {}), cfg["hosts"]


def curl_json(url, payload=None, timeout=30):
    """HTTP via curl -4 (forces IPv4; .local mDNS names often resolve v6 first)."""
    cmd = ["curl", "-4", "-sS", "--fail-with-body", "-m", str(timeout), url]
    inp = None
    if payload is not None:
        cmd += ["-H", "Content-Type: application/json", "--data-binary", "@-"]
        inp = json.dumps(payload).encode()
    r = subprocess.run(cmd, input=inp, capture_output=True)
    if r.returncode != 0:
        raise RuntimeError(f"curl rc={r.returncode}: {(r.stderr or r.stdout).decode()[:300]}")
    return json.loads(r.stdout)


def available_hosts(hosts, quiet=False):
    ok = []
    for h in hosts:
        try:
            tags = curl_json(h["url"].rstrip("/") + "/api/tags", timeout=5)
            names = {m["name"] for m in tags.get("models", [])}
            if h["model"] in names:
                ok.append(h)
            elif not quiet:
                print(f"[warn] {h['name']}: model {h['model']} not found "
                      f"(run `ollama pull {h['model']}` there); skipping", file=sys.stderr)
        except Exception as e:
            if not quiet:
                print(f"[warn] {h['name']} unreachable: {e}; skipping", file=sys.stderr)
    return ok


def read_context(paths):
    parts = []
    for p in paths or []:
        pth = Path(p)
        if not pth.exists():
            raise FileNotFoundError(f"context file not found: {p}")
        parts.append(f"### Context file: {p}\n```\n{pth.read_text()}\n```")
    return "\n\n".join(parts)


FENCE_RE = re.compile(r"```[^\n`]*\n(.*?)\n?```", re.S)


def extract_code(text):
    # strip any stray <think> blocks from reasoning models
    text = re.sub(r"<think>.*?</think>", "", text, flags=re.S).strip()
    blocks = FENCE_RE.findall(text)
    if blocks:
        return max(blocks, key=len).rstrip() + "\n"
    return text.rstrip() + "\n"


def chat(host, defaults, messages):
    payload = {
        "model": host["model"],
        "messages": messages,
        "stream": False,
        "options": {
            "temperature": host.get("temperature", defaults.get("temperature", 0.2)),
            "num_ctx": host.get("num_ctx", defaults.get("num_ctx", 32768)),
        },
    }
    if "think" in host:  # only send for models that support the flag
        payload["think"] = host["think"]
    timeout = host.get("timeout_sec", defaults.get("timeout_sec", 900))
    resp = curl_json(host["url"].rstrip("/") + "/api/chat", payload, timeout=timeout)
    return resp["message"]["content"], resp


def run_check(check, out):
    if not check:
        return True, ""
    cmd = check.replace("{out}", str(out))
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=300)
    output = (r.stdout + r.stderr).strip()
    return r.returncode == 0, output[-4000:]


def run_job(job, host, defaults, retries):
    t0 = time.time()
    spec_path = Path(job["spec"])
    out = Path(job["out"])
    spec = spec_path.read_text()
    ctx = read_context(job.get("context"))

    user = f"# Target file: {out}\n\n## Spec\n{spec}"
    if out.exists() and job.get("include_existing", True):
        user += f"\n\n## Current content of {out} (skeleton — fill it in)\n```\n{out.read_text()}\n```"
    if ctx:
        user += f"\n\n## Reference context (read-only)\n{ctx}"
    user += f"\n\nReturn the complete content of `{out}` in one fenced code block."

    messages = [{"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": user}]

    LOG_DIR.mkdir(parents=True, exist_ok=True)
    log_path = LOG_DIR / f"{out.as_posix().replace('/', '__')}.{int(t0)}.json"
    log = {"host": host["name"], "model": host["model"], "job": job, "attempts": []}

    ok, check_out, attempt = False, "", 0
    for attempt in range(retries + 1):
        content, raw = chat(host, defaults, messages)
        code = extract_code(content)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(code)
        ok, check_out = run_check(job.get("check"), out)
        log["attempts"].append({"response": content, "check_ok": ok, "check_output": check_out,
                                "eval_count": raw.get("eval_count"),
                                "prompt_eval_count": raw.get("prompt_eval_count")})
        if ok:
            break
        messages += [
            {"role": "assistant", "content": content},
            {"role": "user", "content": f"The check `{job['check']}` failed:\n```\n{check_out}\n```\n"
                                        f"Fix the problems and return the complete corrected `{out}` "
                                        f"in one fenced code block."},
        ]
    log["messages"] = messages
    log_path.write_text(json.dumps(log, indent=2))

    lines = code.count("\n")
    status = "OK" if ok else "CHECK_FAILED"
    summary = (f"[{status}] {out} ({lines} lines) host={host['name']} "
               f"attempts={attempt + 1} {time.time() - t0:.0f}s log={log_path}")
    if not ok:
        tail = "\n".join(check_out.splitlines()[-15:])
        summary += f"\n  last check output:\n    " + tail.replace("\n", "\n    ")
    return ok, summary


def pick_host(hosts, name):
    for h in hosts:
        if h["name"] == name:
            return h
    sys.exit(f"host {name!r} not available")


def cmd_run(args):
    defaults, hosts = load_config()
    hosts = available_hosts(hosts)
    if not hosts:
        sys.exit("no Ollama hosts available")
    host = dict(pick_host(hosts, args.host) if args.host else hosts[0])
    if args.model:
        host["model"] = args.model
    job = {"spec": args.spec, "out": args.out, "context": args.context, "check": args.check}
    ok, summary = run_job(job, host, defaults, args.retries)
    print(summary)
    sys.exit(0 if ok else 1)


def cmd_batch(args):
    defaults, hosts = load_config()
    hosts = available_hosts(hosts)
    if not hosts:
        sys.exit("no Ollama hosts available")
    jobs = json.loads(Path(args.jobs).read_text())

    # a pool of host slots: each worker borrows a slot, runs a job, returns it
    slots = queue.Queue()
    for h in hosts:
        for _ in range(int(h.get("slots", 1))):
            slots.put(h)
    n_workers = slots.qsize()
    lock = threading.Lock()

    def worker(job):
        host = slots.get()
        try:
            res = run_job(job, host, defaults, job.get("retries", args.retries))
        except Exception as e:
            res = (False, f"[ERROR] {job.get('out')} host={host['name']}: {e}")
        finally:
            slots.put(host)
        with lock:
            print(res[1], flush=True)
        return res[0]

    print(f"dispatching {len(jobs)} jobs over {n_workers} slot(s): "
          + ", ".join(f"{h['name']}({h['model']})" for h in hosts), flush=True)
    with cf.ThreadPoolExecutor(max_workers=n_workers) as ex:
        results = list(ex.map(worker, jobs))
    passed = sum(results)
    print(f"done: {passed}/{len(jobs)} passed")
    sys.exit(0 if passed == len(jobs) else 1)


def cmd_hosts(_args):
    _, hosts = load_config()
    ok = {h["name"] for h in available_hosts(hosts)}
    for h in hosts:
        print(f"{'UP  ' if h['name'] in ok else 'DOWN'} {h['name']:8} {h['url']}  "
              f"model={h['model']} slots={h.get('slots', 1)}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("run")
    r.add_argument("--spec", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--context", nargs="*", default=[])
    r.add_argument("--check")
    r.add_argument("--retries", type=int, default=2)
    r.add_argument("--host")
    r.add_argument("--model")
    r.set_defaults(fn=cmd_run)

    b = sub.add_parser("batch")
    b.add_argument("jobs")
    b.add_argument("--retries", type=int, default=2)
    b.set_defaults(fn=cmd_batch)

    h = sub.add_parser("hosts")
    h.set_defaults(fn=cmd_hosts)

    args = p.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
