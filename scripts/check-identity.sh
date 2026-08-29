#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"

allowed_upstream='https://github.com/omacom-io/omawrite'

for required in \
    'oma-own-note.pro:TARGET = oma-own-note' \
    'src/main.cpp:oma-own-note' \
    'src/main.cpp:JustNak'
do
    file="${required%%:*}"
    needle="${required#*:}"
    if ! git grep -qF "$needle" -- "$file"; then
        echo "missing $needle in $file" >&2
        exit 1
    fi
done

hits="$(mktemp)"
trap 'rm -f "$hits"' EXIT

set +e
git grep -nI -E 'omawrite|Omawrite|Omacom|omacom\.io' -- . \
    ':!.audit' \
    ':!scripts/check-identity.sh' \
    >"$hits"
status=$?
set -e

if [ "$status" -eq 1 ]; then
    echo "identity check passed"
    exit 0
fi
if [ "$status" -ne 0 ]; then
    echo "git grep failed ($status)" >&2
    exit "$status"
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
