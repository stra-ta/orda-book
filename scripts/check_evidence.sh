#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

current_commit=$(git rev-parse HEAD)
snapshot_commit=$(sed -n 's/.*historical evidence from commit `\([0-9a-fA-F]\{7,40\}\)`.*/\1/p' README.md | head -1)
if [[ -z "$snapshot_commit" ]]; then
  echo "evidence check: README has no explicit historical snapshot commit" >&2
  exit 1
fi
if ! git cat-file -e "${snapshot_commit}^{commit}" 2>/dev/null; then
  echo "evidence check: README snapshot commit ${snapshot_commit} is not in this repository" >&2
  exit 1
fi

if [[ "$current_commit" == "$snapshot_commit"* ]]; then
  echo "evidence check: snapshot is at HEAD ${current_commit}"
else
  echo "evidence check: snapshot ${snapshot_commit} is historical; HEAD is ${current_commit}"
fi

if ! rg -q 'It is not a claim about the current commit' README.md; then
  echo "evidence check: historical table is not marked as non-current" >&2
  exit 1
fi

echo "evidence check: README numbers are explicitly marked historical"
