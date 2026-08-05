#!/usr/bin/env bash
# Rewrite the VERSION file as 0.<progress>.<yymmdd>-<channel>.
#
# During the 0.x cycle the middle field is the sum of the two overall progress
# bars in docs/ROADMAP.md — legacy Firestorm-compatible services plus the modern
# client back end. It is not a percentage of anything and does not need to be:
# it only has to move with real progress. The date says how old a build is at a
# glance, which a bare 0.1.0 never could.
#
# The middle field never decreases. Adding roadmap entries lowers a percentage
# without undoing any work, and a version that walks backwards is worse than one
# that is slightly generous, so a smaller sum is kept rather than applied.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
roadmap="$root/docs/ROADMAP.md"
file="$root/VERSION"
channel=
date_stamp=$(date -u +%y%m%d)

while (($#)); do
  case "$1" in
    --channel) channel=$2; shift 2 ;;
    --date)    date_stamp=$2; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

[[ -f "$roadmap" ]] || { echo "no roadmap at $roadmap" >&2; exit 1; }

# The two overall bars carry class="roadmap-overall-progress"; the per-phase
# rows carry roadmap-phase-progress and must not be counted.
progress=$(awk '
  /roadmap-overall-progress/ { want = 1; next }
  want && match($0, /value="[0-9]+"/) {
    value = substr($0, RSTART + 7, RLENGTH - 8)
    total += value
    found++
    want = 0
  }
  END { if (found < 2) exit 3; print total }
' "$roadmap") || {
  echo "could not read both overall progress bars from $roadmap" >&2
  exit 1
}

current_channel=
if [[ -f "$file" ]]; then
  current=$(tr -d '[:space:]' < "$file")
  # 0.<progress>.<date>[-channel]
  current_progress=$(printf '%s' "$current" | cut -d. -f2)
  current_channel=$(printf '%s' "$current" | cut -s -d- -f2-)
  if [[ "$current_progress" =~ ^[0-9]+$ ]] && ((current_progress > progress)); then
    echo "roadmap sums to $progress, keeping $current_progress: the version does not go backwards"
    progress=$current_progress
  fi
fi

[[ -n "$channel" ]] || channel=$current_channel
version="0.$progress.$date_stamp"
[[ -n "$channel" ]] && version="$version-$channel"

printf '%s\n' "$version" > "$file"
echo "$version"
