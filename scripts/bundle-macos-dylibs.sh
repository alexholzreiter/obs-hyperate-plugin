#!/usr/bin/env bash
set -euo pipefail

# Make the macOS plugin self-contained: copy every non-system dependency it links
# (Homebrew libwebsockets, OpenSSL and whatever those pull in) into the bundle's
# Frameworks directory and rewrite the install names to @loader_path/../Frameworks.
#
# This used to hand-pick libwebsockets/libssl/libcrypto, which broke whenever
# Homebrew changed a library's version or dependency set (e.g. libwebsockets 5.x no
# longer ships a dylib). dylibbundler walks the dependency graph recursively, so it
# stays correct across those changes.

bundle_path="${1:-}"
if [[ -z "${bundle_path}" ]]; then
  echo "Usage: $0 /path/to/obs-hyperate.plugin" >&2
  exit 2
fi

binary_path="${bundle_path}/Contents/MacOS/obs-hyperate"
frameworks_dir="${bundle_path}/Contents/Frameworks"

if [[ ! -f "${binary_path}" ]]; then
  echo "Plugin binary not found: ${binary_path}" >&2
  exit 1
fi

mkdir -p "${frameworks_dir}"

# stdin from /dev/null so dylibbundler never blocks on an interactive prompt in CI.
dylibbundler \
  --overwrite-files \
  --bundle-deps \
  --create-dir \
  --fix-file "${binary_path}" \
  --dest-dir "${frameworks_dir}" \
  --install-path "@loader_path/../Frameworks/" \
  --search-path /opt/homebrew/lib \
  --search-path /usr/local/lib \
  < /dev/null

shopt -s nullglob
bundled_libs=("${frameworks_dir}"/*.dylib)
shopt -u nullglob

if command -v codesign >/dev/null 2>&1; then
  codesign --force --sign - "${bundled_libs[@]}" "${binary_path}" >/dev/null 2>&1 || true
fi

# Safety net: nothing the plugin ships may still point at a Homebrew path, or it
# would fail to load on a machine without Homebrew.
if otool -L "${binary_path}" "${bundled_libs[@]}" 2>/dev/null \
    | grep -E '/opt/homebrew|/usr/local/opt|/usr/local/Cellar' >/dev/null; then
  echo "Bundled macOS plugin still references Homebrew libraries:" >&2
  otool -L "${binary_path}" "${bundled_libs[@]}" >&2
  exit 1
fi

echo "Bundled macOS runtime libraries in ${frameworks_dir}:"
if ((${#bundled_libs[@]})); then
  printf '  %s\n' "${bundled_libs[@]##*/}"
else
  echo "  (none — all dependencies statically linked)"
fi
