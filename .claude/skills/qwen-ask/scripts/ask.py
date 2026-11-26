#!/usr/bin/env python3
"""Ask a local model a self-contained text question. Prints only the answer.

Backends are tried in order from hosts.json (or --url/--model for a one-off):
  - "openai": OpenAI-compatible /v1/chat/completions (llama.cpp server, etc.)
  - "ollama": native Ollama /api/chat (supports think=false reliably)
"""
import argparse, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_HOSTS = os.path.join(HERE, "..", "hosts.json")


def curl_json(url, payload, timeout):
    # curl -4: .local mDNS names often resolve IPv6 first and hang.
    cmd = ["curl", "-4", "-sS", "--fail-with-body", "-m", str(timeout),
           "-H", "Content-Type: application/json", "--data-binary", "@-", url]
    r = subprocess.run(cmd, input=json.dumps(payload).encode(), capture_output=True)
    if r.returncode != 0:
        raise RuntimeError(f"curl rc={r.returncode}: {(r.stderr or r.stdout).decode()[:300].strip()}")
    return json.loads(r.stdout.decode())


def ask(host, messages, a):
    kind = host.get("backend", "ollama")
    base = host["url"].rstrip("/")
    model = a.model or host["model"]
    if kind == "openai":
        data = curl_json(base + "/v1/chat/completions", {
            "model": model, "messages": messages, "stream": False,
            "max_tokens": a.max_tokens, "temperature": a.temperature,
            "chat_template_kwargs": {"enable_thinking": a.enable_thinking},
        }, a.timeout)
        msg = data["choices"][0]["message"]
        return (msg.get("content") or "").strip(), (msg.get("reasoning_content") or "").strip()
    data = curl_json(base + "/api/chat", {
        "model": model, "messages": messages, "stream": False,
        "think": a.enable_thinking,
        "options": {"temperature": a.temperature, "num_predict": a.max_tokens,
                    "num_ctx": host.get("num_ctx", 16384)},
    }, a.timeout)
    msg = data.get("message", {})
    return (msg.get("content") or "").strip(), (msg.get("thinking") or "").strip()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("prompt", nargs="?", help="prompt text; read from stdin if omitted")
    p.add_argument("--system")
    p.add_argument("--file", action="append", default=[],
                   help="append a file's text to the prompt (repeatable)")
    p.add_argument("--max-tokens", type=int, default=2048)
    p.add_argument("--temperature", type=float, default=0.3)
    p.add_argument("--timeout", type=int, default=180)
    p.add_argument("--enable-thinking", action="store_true")
    p.add_argument("--host", help="use only the host with this name from hosts.json")
    p.add_argument("--url", help="one-off server URL (base, e.g. http://127.0.0.1:8080)")
    p.add_argument("--backend", choices=["openai", "ollama"], default="openai",
                   help="backend for --url (default openai)")
    p.add_argument("--model", help="override the model name")
    a = p.parse_args()

    prompt = a.prompt if a.prompt is not None else sys.stdin.read()
    for f in a.file:
        with open(f, encoding="utf-8", errors="replace") as fh:
            prompt += f"\n\n--- {os.path.basename(f)} ---\n{fh.read()}"
    if not prompt.strip():
        print("ERROR: empty prompt", file=sys.stderr); return 2

    messages = ([{"role": "system", "content": a.system}] if a.system else []) + \
               [{"role": "user", "content": prompt}]

    if a.url:
        hosts = [{"name": "cli", "url": a.url, "backend": a.backend, "model": a.model or "default"}]
    else:
        with open(os.environ.get("QWEN_ASK_HOSTS", DEFAULT_HOSTS)) as fh:
            hosts = json.load(fh)["hosts"]
        if a.host:
            hosts = [h for h in hosts if h["name"] == a.host]
            if not hosts:
                print(f"ERROR: no host named {a.host}", file=sys.stderr); return 2

    errors = []
    for h in hosts:
        try:
            content, reasoning = ask(h, messages, a)
        except Exception as e:  # unreachable / model missing → try next host
            errors.append(f"{h['name']}: {e}")
            continue
        if content:
            print(content); return 0
        if reasoning:
            print(f"WARNING: only reasoning returned ({h['name']})", file=sys.stderr)
            print(reasoning); return 0
        errors.append(f"{h['name']}: empty response")
    print("ERROR: all hosts failed\n  " + "\n  ".join(errors), file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
