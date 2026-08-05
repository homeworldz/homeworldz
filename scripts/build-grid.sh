#!/usr/bin/env bash
# Build the Go grid binaries with a version stamped from the repository's
# VERSION file, the way scripts/build-region.sh does for the C++ region.
#
# This exists because an ad-hoc `go build` produces a binary that reports
# "unstamped", and every hand-deployment before 2026-08-05 did exactly that:
# the cloud grid and API answered /version with "dev", which is not a version
# but the Go variable's default. grid/cmd/package-release already stamps
# -X main.version for a release archive; this is the same stamp for the
# deploy-from-a-working-tree path that releases do not cover.
#
# The stamp appends the short commit, so two builds of the same VERSION are
# distinguishable — which a release archive does not need and a rolling
# deployment very much does. It stays inside the character set
# package-release's safeVersion allows, since the version reaches archive
# filenames there.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output="$root/var/deploy"
goos=linux
goarch=amd64
version=

while (($#)); do
  case "$1" in
    --output)
      output=$2
      shift 2
      ;;
    --version)
      version=$2
      shift 2
      ;;
    --os)
      goos=$2
      shift 2
      ;;
    --arch)
      goarch=$2
      shift 2
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ -z "$version" ]]; then
  [[ -f "$root/VERSION" ]] || { echo "no VERSION file at $root" >&2; exit 1; }
  version=$(tr -d '[:space:]' < "$root/VERSION")
  # A dirty tree is worth saying out loud: the commit alone would name code that
  # is not what was built.
  revision=$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo unknown)
  if ! git -C "$root" diff --quiet HEAD 2>/dev/null; then
    revision="$revision-dirty"
  fi
  version="$version-$revision"
fi

mkdir -p "$output"
# cmd/grid ships as homeworldz-grid, so the command and the binary do not share
# a name; pair them explicitly rather than deriving one from the other.
for spec in "grid:homeworldz-grid" "homeworldz-api:homeworldz-api"; do
  command=${spec%%:*}
  binary=${spec##*:}
  GOOS="$goos" GOARCH="$goarch" CGO_ENABLED=0 \
    go build -C "$root/grid" -trimpath \
      -ldflags "-s -w -X main.version=$version" \
      -o "$output/$binary" "./cmd/$command"
  echo "  $binary"
done

echo "built $version for $goos/$goarch into $output"
