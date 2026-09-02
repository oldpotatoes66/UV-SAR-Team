#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
release_dir="$repo_dir/release/latest"
k56_source="$repo_dir/compiled-firmware/f4hwn.sar-team.packed.bin"
k1_source="$repo_dir/k1-firmware/build/SarTeam/sar-team-k1.bin"

if [ ! -f "$k56_source" ] || [ ! -f "$k1_source" ]; then
    echo "Missing compiled firmware. Build K5/K6 and K1 first." >&2
    exit 1
fi

mkdir -p "$release_dir"
cp "$k56_source" "$release_dir/SAR-TEAM-K5-K6-DP32G030.packed.bin"
cp "$k1_source" "$release_dir/SAR-TEAM-K1-PY32F071.bin"

(
    cd "$release_dir"
    shasum -a 256 \
        SAR-TEAM-K5-K6-DP32G030.packed.bin \
        SAR-TEAM-K1-PY32F071.bin > SHA256SUMS
)

source_commit=$(git -C "$repo_dir" rev-parse HEAD)
build_time=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
printf 'Source-Commit: %s\nBuilt-At-UTC: %s\n' "$source_commit" "$build_time" > "$release_dir/BUILD-INFO.txt"

echo "Release package updated in $release_dir"
