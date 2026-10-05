# SPDX-License-Identifier: GPL-3.0-only
"""Wait for the newest PkgWithPartsShouldBeMerged Actions run and report every
job (pass 7).

Local passes say nothing about whether the code builds on Linux, macOS or a
different architecture, so this is the only way to close pass 7 from AGENTS.md.

    python tools/check_all.py --watch <sha-prefix>   # wait for that commit
    python tools/check_all.py --watch latest        # wait for the newest run

Exits 0 when every job passed, 1 on any failure, 2 when no run matched,
3 on timeout.

The token is read from the credential Git Credential Manager already holds and
is never printed or written to disk.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
import urllib.request

OWNER = "SpaceJamp"
# Must match the repository name exactly; the API 404s on a wrong or
# case-mismatched name, which surfaces as "no workflow run found" rather than
# as an obvious error.
REPO = "PkgWithPartsShouldBeMerged"


def token() -> str:
    proc = subprocess.run(
        ["git", "credential", "fill"],
        input=b"protocol=https\nhost=github.com\n\n",
        capture_output=True,
        check=True,
    )
    for line in proc.stdout.decode().splitlines():
        if line.startswith("password="):
            return line[len("password=") :]
    raise SystemExit(
        "no GitHub credential available; run 'git push' once so Git Credential "
        "Manager stores one, or set GITHUB_TOKEN"
    )


TOKEN = token()


def get(path: str) -> dict:
    request = urllib.request.Request(
        f"https://api.github.com/repos/{OWNER}/{REPO}{path}",
        headers={"Authorization": f"Bearer {TOKEN}", "Accept": "application/vnd.github+json"},
    )
    with urllib.request.urlopen(request) as response:
        return json.loads(response.read().decode())


def pick_run(want: str) -> dict | None:
    runs = get("/actions/runs?per_page=10")["workflow_runs"]
    if want == "latest":
        return runs[0] if runs else None
    for run in runs:
        if run["head_sha"].startswith(want):
            return run
    return None


def report(run: dict) -> int:
    print(f"run #{run['run_number']}  {run['head_sha'][:7]}  {run['conclusion']}")
    print(run["html_url"])
    failures = 0
    for job in get(f"/actions/runs/{run['id']}/jobs?per_page=20")["jobs"]:
        ok = job["conclusion"] in ("success", "skipped", None)
        if not ok:
            failures += 1
        print(f"  [{'ok  ' if ok else 'FAIL'}] {job['name']}: {job['conclusion']}")
        for step in job.get("steps", []):
            if step.get("conclusion") not in ("success", "skipped", None):
                print(f"           step '{step['name']}' -> {step['conclusion']}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--watch", required=True, metavar="SHA|latest")
    parser.add_argument("--timeout", type=int, default=1500, help="seconds to wait")
    parser.add_argument("--interval", type=int, default=20, help="seconds between polls")
    args = parser.parse_args()

    deadline = time.time() + args.timeout
    run = None
    while time.time() < deadline:
        run = pick_run(args.watch)
        if run is not None and run["status"] == "completed":
            break
        time.sleep(args.interval)

    if run is None:
        print(f"no workflow run found for {args.watch!r}", file=sys.stderr)
        return 2
    if run["status"] != "completed":
        print(f"timed out waiting; run #{run['run_number']} is still {run['status']}")
        print(run["html_url"])
        return 3
    return 1 if report(run) else 0


if __name__ == "__main__":
    raise SystemExit(main())