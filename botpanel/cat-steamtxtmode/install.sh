#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"

if [ "$(id -u)" -ne 0 ]; then
    echo "MUST RUN AS SUDO: sudo ./install.sh" >&2
    exit 1
fi

soname="libcatsteamtxtmode.so"
triplet64="$(gcc -print-multiarch 2>/dev/null || echo x86_64-linux-gnu)"
triplet32="$(gcc -m32 -print-multiarch 2>/dev/null || echo i386-linux-gnu)"
libdir64="/usr/lib/${triplet64}"
libdir32="/usr/lib/${triplet32}"

make -C "$script_dir" REPO_ROOT="$repo_root" clean

if ! make -C "$script_dir" REPO_ROOT="$repo_root" lib64 -j"$(nproc --all)"; then
    echo "[cat-steamtxtmode] 64-bit build failed." >&2
    exit 1
fi

install -d -m 0755 "$libdir64"
install -m 0755 "$script_dir/bin/lib64/$soname" "$libdir64/"

if make -C "$script_dir" REPO_ROOT="$repo_root" lib32 -j"$(nproc --all)"; then
    install -d -m 0755 "$libdir32"
    install -m 0755 "$script_dir/bin/lib/$soname" "$libdir32/"
else
    echo "[cat-steamtxtmode] 32-bit build failed; continuing with 64-bit only." >&2
fi

ldconfig
echo "[cat-steamtxtmode] installed:"
ldconfig -p | grep "$soname" || true
