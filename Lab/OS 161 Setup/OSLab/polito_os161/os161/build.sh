#!/bin/bash
# build.sh — unified build script for OS/161 (Assignment 2).
#
# Modes:
#   ./build.sh all       Full clean build: kernel + userland (2-4 min)
#   ./build.sh kernel    Incremental kernel rebuild only (3-15s)
#   ./build.sh testbin   Rebuild + install file_rwc + file_link (5-15s)
#   ./build.sh clean     Wipe build/ and re-configure (1s, then run ./build.sh all)
#   ./build.sh menu      Show this help
#
# All output streams live — no truncation. Errors are reported with
# clear banners so the failing step is obvious.
#
# Run from Ubuntu WSL.

# Don't `set -e` — we want to see the failing command and its full
# error output, not silently exit on the first failure.

SRC=/tmp/os161-src
W="/mnt/d/Depression/OSLab/polito_os161/os161"
OSTREE=/home/os161user/os161
export PATH="$W/tools/bin:$PATH"

# --- helpers ---------------------------------------------------------------

# (Re)create the /tmp/os161-src -> $W symlink if it's missing/broken.
ensure_symlink() {
    if [ ! -e "$SRC/src/configure" ]; then
        echo "=== (Re)creating /tmp/os161-src symlink ==="
        rm -f "$SRC" 2>/dev/null
        ln -sfn "$W" "$SRC"
    fi
    if [ ! -e "$SRC/src/configure" ]; then
        echo "!!! Symlink broken: $SRC -> $W !!!"
        exit 1
    fi
}

# Sync the freshly-built kernel into the boot location ($W/root) and VERIFY.
#
# History: OS/161 boots from $W/root/kernel-DUMBVM, but `bmake install` puts
# the kernel at $OSTREE/kernel-DUMBVM (NOT $OSTREE/root/...). An earlier
# version of this script copied from the wrong (never-existing) path, so it
# silently kept booting a STALE kernel — code changes appeared to have no
# effect. To make that class of bug impossible, we ALWAYS copy straight from
# the just-compiled binary and then assert the boot copy is byte-identical.
sync_kernel() {
    local built="$W/src/kern/compile/DUMBVM/kernel"
    local boot="$W/root/kernel-DUMBVM"

    echo
    echo "=== Syncing kernel to workspace root (boot location) ==="

    if [ ! -f "$built" ]; then
        echo "!!! Built kernel missing: $built !!!"
        echo "    The build step did not produce a kernel — aborting."
        exit 1
    fi

    mkdir -p "$W/root"
    cp -f "$built" "$boot" || { echo "!!! Failed to copy kernel to $boot !!!"; exit 1; }

    # Assert the boot copy is byte-for-byte the one we just built. If this
    # ever fails, the kernel you run is NOT the kernel you compiled.
    if cmp -s "$built" "$boot"; then
        echo "  OK: $boot matches freshly-built kernel"
        ls -la "$boot"
    else
        echo "!!! MISMATCH: $boot differs from $built after copy !!!"
        echo "    You would be booting a stale kernel — aborting."
        exit 1
    fi
}

# Run a command, print a banner, capture exit code.
run_step() {
    local name="$1"
    shift
    echo
    echo "===== STEP: $name ====="
    "$@"
    local rc=$?
    echo "===== STEP '$name' exited with code $rc ====="
    return $rc
}

# Verify the toolchain is on PATH.
verify_toolchain() {
    echo "=== Verifying toolchain ==="
    which mips-harvard-os161-gcc >/dev/null 2>&1 || { echo "!!! toolchain not in PATH !!!"; exit 1; }
    which bmake                  >/dev/null 2>&1 || { echo "!!! bmake not in PATH !!!";       exit 1; }
    which sys161                 >/dev/null 2>&1 || { echo "!!! sys161 not in PATH !!!";      exit 1; }
    echo
}

# Install a single built test program.
# OS/161 boots from the "emufs" device, which exposes sys161's *current
# directory* to the kernel. So files must end up under $W/root/ to be
# visible at boot as /testbin/, /bin/, etc.
# We install to BOTH locations:
#   - $W/root/testbin/  — where OS/161 will actually look (emufs)
#   - $OSTREE/testbin/  — for completeness, in case anything else uses it
install_one() {
    local prog="$1"
    local src="$SRC/src/build/userland/testbin/$prog/$prog"

    if [ ! -f "$src" ]; then
        echo "!!! Built binary missing: $src !!!"
        return 1
    fi

    # Primary install: $W/root/testbin/  (what emufs/boot sees)
    mkdir -p "$W/root/testbin"
    rm -f "$W/root/testbin/$prog"
    cp "$src" "$W/root/testbin/$prog" 2>/dev/null || ln "$src" "$W/root/testbin/$prog"
    if [ ! -f "$W/root/testbin/$prog" ]; then
        echo "!!! Failed to install $prog to $W/root/testbin/ !!!"
        return 1
    fi
    echo "  Installed: $W/root/testbin/$prog  (visible to OS/161)"

    # Secondary install: $OSTREE/testbin/  (consistency)
    mkdir -p "$OSTREE/testbin"
    rm -f "$OSTREE/testbin/$prog"
    cp "$src" "$OSTREE/testbin/$prog" 2>/dev/null || ln "$src" "$OSTREE/testbin/$prog"
    echo "  Installed: $OSTREE/testbin/$prog"
}

# Install the sample.txt data file into the emufs-visible testbin dir.
# It is not a program, so the userland `bmake install` never touches it;
# we copy it by hand into both root/testbin (what OS/161 boots against)
# and the ostree, so `p /testbin/fileoperations cat /testbin/sample.txt` works.
install_sample() {
    local src="$W/src/userland/testbin/sample.txt"
    if [ ! -f "$src" ]; then
        echo "!!! sample.txt missing: $src !!!"
        return 1
    fi
    mkdir -p "$W/root/testbin" "$OSTREE/testbin"
    cp "$src" "$W/root/testbin/sample.txt"
    cp "$src" "$OSTREE/testbin/sample.txt"
    echo "  Installed: $W/root/testbin/sample.txt  (visible to OS/161)"
}

# --- mode: full clean build -------------------------------------------------
do_all() {
    echo "=========================================="
    echo "  Full build: kernel + userland (clean)"
    echo "=========================================="
    ensure_symlink
    verify_toolchain

    # ---- Kernel ----
    cd "$W/src/kern/conf"
    ./config DUMBVM
    cd ../compile/DUMBVM

    run_step "kernel clean"   bmake -j4 clean  || true   # ignore "nothing to clean"
    run_step "kernel depend"  bmake -j4 depend || { echo "!!! KERNEL DEPEND FAILED !!!"; exit 1; }
    run_step "kernel build"   bmake -j4        || { echo "!!! KERNEL BUILD FAILED !!!"; exit 1; }
    run_step "kernel install" bmake install    || { echo "!!! KERNEL INSTALL FAILED !!!"; exit 1; }

    # ---- Userland ----
    echo
    echo "=== Configuring userland ==="
    cd "$SRC/src"
    rm -rf build
    mkdir build
    ./configure --ostree="$OSTREE" || { echo "!!! ./configure FAILED !!!"; exit 1; }
    echo

    run_step "userland build"   bmake -j4     || { echo "!!! USERLAND BUILD FAILED !!!"; exit 1; }

    # Wipe the ostree so the tar-based install doesn't choke on
    # leftover symlinks/dirs from a previous install.
    rm -rf "$OSTREE"
    mkdir -p "$OSTREE"

    run_step "userland install" bmake install || { echo "!!! USERLAND INSTALL FAILED !!!"; exit 1; }

    # ---- Sync ostree -> workspace root (where emufs boots from) ----
    echo
    echo "=== Syncing ostree to workspace root ==="
    mkdir -p "$W/root"
    # Copy everything from the ostree into root/, preserving structure.
    # We use cp -a so symlinks/dirs land in $W/root/ the way emufs wants.
    cp -a "$OSTREE"/. "$W/root/" 2>/dev/null || true

    # The ostree copy above may not carry a fresh kernel (the ostree is wiped
    # and rebuilt for the userland install). Always (re)sync + verify the
    # kernel from its build dir so root/ boots exactly what we just compiled.
    sync_kernel

    install_sample
    verify_testbin
    do_done_message
}

# --- mode: incremental kernel ----------------------------------------------
do_kernel() {
    echo "=========================================="
    echo "  Incremental kernel rebuild"
    echo "=========================================="

    local kern_build="$W/src/kern/compile/DUMBVM"
    if [ ! -d "$kern_build" ]; then
        echo "!!! Kernel not configured. Run './build.sh all' first !!!"
        exit 1
    fi

    verify_toolchain
    cd "$kern_build"

    run_step "kernel build"   bmake -j4     || { echo "!!! KERNEL BUILD FAILED !!!"; exit 1; }
    run_step "kernel install" bmake install || { echo "!!! KERNEL INSTALL FAILED !!!"; exit 1; }

    sync_kernel

    echo
    echo "=== Kernel rebuilt ==="
}

# --- mode: incremental testbin (file_rwc + file_link) ----------------------
do_testbin() {
    echo "=========================================="
    echo "  Incremental testbin rebuild (file_rwc + file_link)"
    echo "=========================================="
    ensure_symlink

    if [ ! -f "$SRC/src/build/install/bin/sh" ]; then
        echo "!!! No existing build/ — run './build.sh all' first !!!"
        exit 1
    fi

    verify_toolchain
    cd "$SRC/src/userland" || { echo "!!! cd failed !!!"; exit 1; }
    echo "=== Working in: $(pwd) ==="
    echo

    run_step "build file_rwc"  bmake -j4 -C testbin/file_rwc  || { echo "!!! file_rwc BUILD FAILED !!!"; exit 1; }
    run_step "build file_link" bmake -j4 -C testbin/file_link || { echo "!!! file_link BUILD FAILED !!!"; exit 1; }
    run_step "build fileoperations" bmake -j4 -C testbin/fileoperations || { echo "!!! fileoperations BUILD FAILED !!!"; exit 1; }

    echo
    echo "=== Installing rebuilt test programs to disk image ==="
    install_one file_rwc
    install_one file_link
    install_one fileoperations
    install_sample

    do_done_message
}

# --- mode: clean -----------------------------------------------------------
do_clean() {
    echo "=========================================="
    echo "  Clean build artifacts"
    echo "=========================================="
    rm -rf /tmp/os161-src/src/build
    rm -rf /home/os161user/os161
    rm -f  /home/os161user/os161/root/kernel-DUMBVM
    echo "  Wiped: /tmp/os161-src/src/build"
    echo "  Wiped: /home/os161user/os161"
    echo
    echo "Now run:  ./build.sh all"
}

# --- helpers used by multiple modes ----------------------------------------
verify_testbin() {
    echo
    echo "=== Verifying test programs on disk image ==="
    for p in file_rwc file_link; do
        if [ -f "$W/root/testbin/$p" ]; then
            echo "  OK: testbin/$p"
        else
            echo "  MISSING: testbin/$p"
        fi
    done
}

do_done_message() {
    echo
    echo "=== Build complete! ==="
    echo
    echo "To boot interactively:"
    echo "  cd \"$W/root\""
    echo "  ../tools/bin/sys161 kernel-DUMBVM"
    echo
    echo "To run A2 tests automatically:"
    echo "  ./test-assignment2.sh"
    echo
    echo "Inside the OS/161 menu:"
    echo "  p /testbin/file_rwc   - run the read/write/copy/append test"
    echo "  p /testbin/file_link  - run the symlink test"
    echo "  s                     - enter the shell"
}

# --- entry point -----------------------------------------------------------
mode="${1:-menu}"

case "$mode" in
    all)     do_all ;;
    kernel)  do_kernel ;;
    testbin) do_testbin ;;
    clean)   do_clean ;;
    menu|--help|-h|"")
        echo "build.sh — unified OS/161 build script"
        echo
        echo "Usage:  ./build.sh <mode>"
        echo
        echo "Modes:"
        echo "  all       Full clean build: kernel + userland           (~2-4 min)"
        echo "  kernel    Incremental kernel rebuild only               (~3-15 sec)"
        echo "  testbin   Rebuild + install file_rwc + file_link        (~5-15 sec)"
        echo "  clean     Wipe build/ and ostree, ready for './build.sh all'"
        echo "  menu      Show this help (default)"
        echo
        echo "Typical edit-compile-run loop:"
        echo "  1. (once)  ./build.sh all"
        echo "  2. edit a file in src/kern/  or  src/userland/testbin/"
        echo "  3. ./build.sh kernel      # if you changed kernel code"
        echo "     ./build.sh testbin     # if you changed test programs"
        echo "  4. ./test-assignment2.sh  # or ./run-os161.sh"
        ;;
    *)
        echo "!!! Unknown mode: $mode !!!"
        echo "    Try:  ./build.sh menu"
        exit 1
        ;;
esac