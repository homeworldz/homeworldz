#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build/linux-release"
version=
run_tests=false

while (($#)); do
  case "$1" in
    --build-dir)
      build=$2
      shift 2
      ;;
    --version)
      version=$2
      shift 2
      ;;
    --test)
      run_tests=true
      shift
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

for command in cmake ninja c++; do
  command -v "$command" >/dev/null || {
    echo "required build command is unavailable: $command" >&2
    exit 1
  }
done

configure=(
  -S "$root"
  -B "$build"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DHOMEWORLDZ_REQUIRE_JOLT=ON
)
if [[ -n "${VCPKG_ROOT:-}" ]]; then
  configure+=(
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    -DVCPKG_MANIFEST_MODE=OFF
  )
fi
# Always pass a version, defaulting to the VERSION file. CMakeLists only reads
# that file when HOMEWORLDZ_VERSION is undefined, and a -D from any earlier
# configure persists in CMakeCache.txt — so leaving this off makes VERSION inert
# in an existing build directory. That is how the OVH box kept reporting a
# version string that no longer existed anywhere: editing VERSION changed
# nothing and the rebuild reported "no work to do".
if [[ -z "$version" && -f "$root/VERSION" ]]; then
  version=$(tr -d '[:space:]' < "$root/VERSION")
fi
if [[ -n "$version" ]]; then
  configure+=(-DHOMEWORLDZ_VERSION="$version")
fi

cmake "${configure[@]}"
cmake --build "$build" --parallel "${HOMEWORLDZ_BUILD_JOBS:-$(nproc)}"
if $run_tests; then
  ctest --test-dir "$build" --output-on-failure
fi

echo "$build/region/homeworldz-region"
