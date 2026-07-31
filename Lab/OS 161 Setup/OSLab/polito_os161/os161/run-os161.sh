#!/bin/bash
# Boot OS/161 with the DUMBVM kernel and run Assignment 2 tests.
# Run this in Ubuntu WSL.

set -e

cd "/mnt/d/Depression/OSLab/polito_os161/os161/root"
SYS161="$(dirname "$(pwd)")/tools/bin/sys161"
KERNEL="$(pwd)/kernel-DUMBVM"

echo "=== OS/161 Assignment 2 — Run & Test ==="
echo "Kernel: $KERNEL"
echo "Sys161: $SYS161"
echo
echo "Available commands inside sys161:"
echo "  ls /testbin       - list test programs"
echo "  /testbin/file_rwc <in> <out>   - read/write/copy test"
echo "  /testbin/file_link <msg> <name> - symlink/readlink test"
echo "  q                 - quit"
echo
echo "=== Booting... ==="
"$SYS161" "$KERNEL"

echo
echo "=== Boot session ended ==="