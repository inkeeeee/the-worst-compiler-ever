#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
destination="$root/third_party/chibicc"
revision=90d1f7f199cc55b13c7fdb5839d1409806633fdb

if [[ ! -d "$destination/.git" ]]; then
    if [[ -e "$destination" ]]; then
        echo "Not a git checkout: $destination" >&2
        exit 1
    fi
    mkdir -p "$root/third_party"
    git clone --depth 1 https://github.com/rui314/chibicc.git "$destination"
fi
if [[ $(git -C "$destination" rev-parse HEAD) != "$revision" ]]; then
    if [[ -n $(git -C "$destination" status --porcelain) ]]; then
        echo "chibicc has local changes; refusing to switch its revision" >&2
        exit 1
    fi
    git -C "$destination" fetch --depth 1 origin "$revision"
    git -C "$destination" checkout --detach "$revision"
fi
make -C "$destination" -j "${JOBS:-2}"
echo "Built $destination/chibicc ($revision)"
