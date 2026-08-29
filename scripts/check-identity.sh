#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"

# Product names left over from the omawrite fork.
# Omarchy theme paths under ~/.local/state/omarchy stay. Those are runtime files.
# README.md may name the upstream repo URL once.

allowed_upstream='https://github.com/omacom-io/omawrite'

hits="$(mktemp)"
trap 'rm -f "$hits"' EXIT

if ! git grep -nI -E 'omawrite|Omawrite|Omacom|omacom\.io' -- . \
    ':!.audit' \
    ':!scripts/check-identity.sh' \
    >"$hits"
then
    echo "identity check passed"
    exit 0
fi

bad=0
while IFS= read -r line || [ -n "$line" ]; do
    [ -z "$line" ] && continue
    case "$line" in
        README.md:*"$allowed_upstream"*) continue ;;
    esac
    printf '%s\n' "$line"
    bad=1
done <"$hits"

if [ "$bad" -ne 0 ]; then
    echo "leftover omawrite or omacom product identity" >&2
    exit 1
fi

echo "identity check passed"
