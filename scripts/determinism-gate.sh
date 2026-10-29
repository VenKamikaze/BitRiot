#!/usr/bin/env bash
# Cross-platform determinism gate (brief 05b).
#
# Builds bitriot_headless (the simulation with no SDL: -DBITRIOT_BUILD_GAME=OFF) under
# a set of "variants" (compilers, optimisation flags, CPU architectures, libstdc++ vs
# libc++, WebAssembly) and replays records with each one. A variant passes when, for
# every record, `bitriot_headless replay --hashes` exits 0 (every checkpoint of the
# record agrees) and prints byte-for-byte the same stdout as the reference build (the
# state hash of every tick). A variant that diverges is reported with the first
# diverging tick.
#
# The records are tests/data/golden-round.brmr (one full round, a checkpoint on every
# tick) and a few more rounds the reference build plays at the start (different map
# sizes, player counts, teams, block densities), also with a checkpoint on every tick.
#
# Then a negative control: the same replay with `--perturb-at 100` (one unit added to
# one block's offset between ticks 99 and 100) is run through the same comparison on
# every variant, and the gate must report it as a failure at exactly tick 100. If it
# does not, the gate cannot be trusted and it fails.
#
#   scripts/determinism-gate.sh                  # every variant that is available
#   scripts/determinism-gate.sh --list           # the variants and whether they are available
#   scripts/determinism-gate.sh --only gcc-O2,wasm-O2
#   scripts/determinism-gate.sh --strict         # an unavailable variant is a failure
#
# Exit status: 0 every available variant matched and the negative control was caught;
# 1 a variant diverged or failed to build, or the negative control was not caught;
# 2 setup error (the reference variant cannot be built, bad option).
#
# Needs: cmake and a C++17 compiler (g++, the reference). Optional, per variant: clang++
# (clang-*), libc++ (clang-libc++), 32-bit multilib (gcc-m32), aarch64-linux-gnu-g++ +
# qemu-aarch64 (aarch64-*), valgrind, and a pinned Emscripten + Node (wasm-*; install
# with scripts/setup-emsdk.sh; this script never installs it). The wasm variants find
# emcc on PATH, or $EMSDK_DIR (default <repo>/.emsdk) and source its emsdk_env.sh.
#
# Environment: GATE_JOBS (build jobs, default 4), GATE_BUILD_ROOT (default
# <repo>/build-gate, git-ignored), GATE_NODE (the Node that runs the wasm builds;
# default: the first `node` on PATH before emsdk is sourced), EMSDK_DIR.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JOBS="${GATE_JOBS:-4}"
BUILD_ROOT="${GATE_BUILD_ROOT:-$root/build-gate}"
EMSDK_DIR="${EMSDK_DIR:-$root/.emsdk}"
PINNED_EMSCRIPTEN="6.0.13"
GOLDEN="$root/tests/data/golden-round.brmr"
PERTURB_TICK=100
ONLY=""
LIST=0
STRICT=0
EXTRA_ROUNDS=1
CLEAN=0

usage() {
    sed -n '2,/^set -uo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
    cat <<'EOF'

Options:
  --list            print the variants and whether each is available, then exit
  --only A,B        run only these variants (the reference, gcc-O2, always runs)
  --strict          treat an unavailable variant as a failure
  --no-extra        replay the golden record only (skip the extra rounds)
  --jobs N          build jobs (default $GATE_JOBS or 4)
  --build-root DIR  where the builds and results go (default build-gate)
  --clean           delete the build root first
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --list) LIST=1 ;;
        --only) shift; ONLY="${1:-}" ;;
        --only=*) ONLY="${1#--only=}" ;;
        --strict) STRICT=1 ;;
        --no-extra) EXTRA_ROUNDS=0 ;;
        --jobs) shift; JOBS="${1:-4}" ;;
        --build-root) shift; BUILD_ROOT="${1:-}" ;;
        --clean) CLEAN=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "determinism-gate: unknown option $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

# --------------------------------------------------------------------------------------
# The variants. Each has a toolchain kind, flags and a runner (what starts the binary).
# --------------------------------------------------------------------------------------
# kind:  native-gcc | native-clang | aarch64 | wasm | same-binary (another way to run
#        an existing build)
# flags: CMAKE_CXX_FLAGS. The project's own per-config -O flags are bypassed on
#        purpose (the build type is "Gate", which the CMakeLists has no flags for), so
#        what is listed here is exactly what the compiler gets.
VARIANTS=(gcc-O2 gcc-O0 gcc-O3-native-fastmath gcc-initpattern gcc-asan-ubsan gcc-m32
          clang-O2 clang-O0 clang-O3-native-fastmath clang-libc++
          gcc-O0-valgrind gcc-O2-malloc-perturb
          aarch64-O2 aarch64-O3-fastmath
          wasm-O2 wasm-O3-fastmath wasm-exnref-O2 wasm-O2-emsdk-node)
declare -A KIND FLAGS LDFLAGS DESC BASE ONLY_RECORDS
# Valgrind is ~300x slower than native: it replays the golden record and one small round.
ONLY_RECORDS[gcc-O0-valgrind]="golden 13x11-6p"
KIND[gcc-O2]=native-gcc;                 FLAGS[gcc-O2]="-O2 -DNDEBUG";                          DESC[gcc-O2]="GCC -O2 (the reference; what Release builds)"
KIND[gcc-O0]=native-gcc;                 FLAGS[gcc-O0]="-O0 -g";                                DESC[gcc-O0]="GCC -O0 -g (Debug: asserts on)"
KIND[gcc-O3-native-fastmath]=native-gcc; FLAGS[gcc-O3-native-fastmath]="-O3 -march=native -ffast-math -DNDEBUG"; DESC[gcc-O3-native-fastmath]="GCC -O3 -march=native -ffast-math"
KIND[gcc-initpattern]=native-gcc;        FLAGS[gcc-initpattern]="-O2 -DNDEBUG -ftrivial-auto-var-init=pattern"; DESC[gcc-initpattern]="GCC -O2, locals start as 0xFE.. (uninitialised-local check)"
KIND[gcc-asan-ubsan]=native-gcc;         FLAGS[gcc-asan-ubsan]="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined"
                                         LDFLAGS[gcc-asan-ubsan]="-fsanitize=address,undefined";  DESC[gcc-asan-ubsan]="GCC -O1 AddressSanitizer + UBSan (any report aborts)"
KIND[gcc-m32]=native-gcc;                FLAGS[gcc-m32]="-O2 -DNDEBUG -m32";                     LDFLAGS[gcc-m32]="-m32"
                                         DESC[gcc-m32]="GCC -m32 -O2 (ILP32, 32-bit long and size_t, like wasm32)"
KIND[clang-O2]=native-clang;             FLAGS[clang-O2]="-O2 -DNDEBUG";                         DESC[clang-O2]="Clang -O2, libstdc++"
KIND[clang-O0]=native-clang;             FLAGS[clang-O0]="-O0 -g";                               DESC[clang-O0]="Clang -O0 -g (asserts on)"
KIND[clang-O3-native-fastmath]=native-clang; FLAGS[clang-O3-native-fastmath]="-O3 -march=native -ffast-math -DNDEBUG"; DESC[clang-O3-native-fastmath]="Clang -O3 -march=native -ffast-math"
KIND[clang-libc++]=native-clang;         FLAGS[clang-libc++]="-O2 -DNDEBUG -stdlib=libc++";      LDFLAGS[clang-libc++]="-stdlib=libc++"
                                         DESC[clang-libc++]="Clang -O2 with LLVM libc++ (what Emscripten uses), natively"
KIND[gcc-O0-valgrind]=same-binary;       BASE[gcc-O0-valgrind]=gcc-O0;                           DESC[gcc-O0-valgrind]="the gcc-O0 build under Valgrind memcheck (uninitialised reads, bad accesses)"
KIND[gcc-O2-malloc-perturb]=same-binary; BASE[gcc-O2-malloc-perturb]=gcc-O2;                    DESC[gcc-O2-malloc-perturb]="the gcc-O2 build with MALLOC_PERTURB_=165 (fresh heap memory is not zero)"
KIND[aarch64-O2]=aarch64;                FLAGS[aarch64-O2]="-O2 -DNDEBUG";                       DESC[aarch64-O2]="aarch64 GCC -O2 under qemu-user"
KIND[aarch64-O3-fastmath]=aarch64;       FLAGS[aarch64-O3-fastmath]="-O3 -ffast-math -DNDEBUG";  DESC[aarch64-O3-fastmath]="aarch64 GCC -O3 -ffast-math under qemu-user"
KIND[wasm-O2]=wasm;                      FLAGS[wasm-O2]="-O2 -DNDEBUG -fwasm-exceptions";        DESC[wasm-O2]="Emscripten -O2 -fwasm-exceptions (legacy EH, the default), Node"
KIND[wasm-O3-fastmath]=wasm;             FLAGS[wasm-O3-fastmath]="-O3 -ffast-math -DNDEBUG -fwasm-exceptions"; DESC[wasm-O3-fastmath]="Emscripten -O3 -ffast-math -fwasm-exceptions, Node"
KIND[wasm-exnref-O2]=wasm;               FLAGS[wasm-exnref-O2]="-O2 -DNDEBUG -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0"; DESC[wasm-exnref-O2]="Emscripten -O2, standard (exnref) wasm EH, Node"
KIND[wasm-O2-emsdk-node]=same-binary;    BASE[wasm-O2-emsdk-node]=wasm-O2;                      DESC[wasm-O2-emsdk-node]="the wasm-O2 build on the Node that emsdk ships (a newer V8)"

# Where the wasm builds' link flags are set. NODERAWFS: the binary reads the record and
# data/ straight from the real file system. 1 MiB stack and growable memory, as a wasm
# game would need.
WASM_LINK="-sNODERAWFS=1 -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576"

# --------------------------------------------------------------------------------------
# Availability
# --------------------------------------------------------------------------------------
SYSTEM_NODE="${GATE_NODE:-$(command -v node || true)}"
EMSDK_NODE_PATH=""
EMSCRIPTEN_VERSION=""
declare -A AVAIL REASON

probe_compile() { # compiler flags... : can it compile and link a C++17 program using <vector> and <map>?
    local cxx="$1"; shift
    local d; d="$(mktemp -d)"
    printf '#include <vector>\n#include <map>\n#include <string>\nint main(){std::map<int,std::string> m; m[1]="x"; std::vector<int> v(3); return (int)(m.size()+v.size())-4;}\n' > "$d/t.cpp"
    "$cxx" -std=c++17 "$@" "$d/t.cpp" -o "$d/t" >/dev/null 2>&1 && "$d/t" >/dev/null 2>&1
    local rc=$?
    rm -rf "$d"
    return $rc
}

# The pinned emsdk ($EMSDK_DIR, default <repo>/.emsdk) wins over an emcc on PATH (a distro's
# emscripten package is not the pinned version); with no emsdk there, the emcc on PATH is used.
try_emsdk() {
    if [ -f "$EMSDK_DIR/emsdk_env.sh" ]; then
        # shellcheck disable=SC1091
        EMSDK_QUIET=1 source "$EMSDK_DIR/emsdk_env.sh" >/dev/null 2>&1
        command -v emcc >/dev/null 2>&1 && return 0
    fi
    command -v emcc >/dev/null 2>&1
}

check_availability() {
    local v
    for v in "${VARIANTS[@]}"; do AVAIL[$v]=1; REASON[$v]=""; done
    if ! command -v cmake >/dev/null 2>&1; then
        for v in "${VARIANTS[@]}"; do AVAIL[$v]=0; REASON[$v]="cmake is not installed"; done
        return
    fi
    if ! command -v g++ >/dev/null 2>&1; then
        for v in gcc-O2 gcc-O0 gcc-O3-native-fastmath gcc-initpattern gcc-asan-ubsan gcc-m32 gcc-O0-valgrind gcc-O2-malloc-perturb; do
            AVAIL[$v]=0; REASON[$v]="g++ is not installed"; done
    else
        probe_compile g++ -fsanitize=address,undefined >/dev/null 2>&1 || { AVAIL[gcc-asan-ubsan]=0; REASON[gcc-asan-ubsan]="g++ cannot link the sanitizers"; }
        probe_compile g++ -m32 >/dev/null 2>&1 || { AVAIL[gcc-m32]=0; REASON[gcc-m32]="no 32-bit multilib (apt: g++-multilib, which on Ubuntu conflicts with g++-aarch64-linux-gnu: install one or the other; pacman: lib32-gcc-libs lib32-glibc)"; }
        valgrind -q --error-exitcode=99 true >/dev/null 2>&1 || { AVAIL[gcc-O0-valgrind]=0; REASON[gcc-O0-valgrind]="valgrind is not installed or does not run (apt/pacman: valgrind)"; }
    fi
    if ! command -v clang++ >/dev/null 2>&1; then
        for v in clang-O2 clang-O0 clang-O3-native-fastmath clang-libc++; do AVAIL[$v]=0; REASON[$v]="clang++ is not installed"; done
    else
        probe_compile clang++ -stdlib=libc++ -fuse-ld=lld >/dev/null 2>&1 || probe_compile clang++ -stdlib=libc++ >/dev/null 2>&1 \
            || { AVAIL[clang-libc++]=0; REASON[clang-libc++]="libc++ is not installed (apt: libc++-dev libc++abi-dev; pacman: libc++)"; }
    fi
    if ! command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
        for v in aarch64-O2 aarch64-O3-fastmath; do AVAIL[$v]=0; REASON[$v]="aarch64-linux-gnu-g++ is not installed (apt: g++-aarch64-linux-gnu; pacman: aarch64-linux-gnu-gcc)"; done
    elif ! command -v qemu-aarch64 >/dev/null 2>&1; then
        for v in aarch64-O2 aarch64-O3-fastmath; do AVAIL[$v]=0; REASON[$v]="qemu-aarch64 is not installed (apt/pacman: qemu-user)"; done
    fi
    # The wasm variants: an existing emsdk only. Never installed from here.
    if [ -z "$SYSTEM_NODE" ]; then
        for v in wasm-O2 wasm-O3-fastmath wasm-exnref-O2; do AVAIL[$v]=0; REASON[$v]="node is not installed"; done
    elif ! try_emsdk; then
        for v in wasm-O2 wasm-O3-fastmath wasm-exnref-O2; do
            AVAIL[$v]=0; REASON[$v]="not available: no emsdk in $EMSDK_DIR and no emcc on PATH (run scripts/setup-emsdk.sh)"; done
    else
        EMSCRIPTEN_VERSION="$(emcc --version 2>/dev/null | head -1 | sed -n 's/.* \([0-9][0-9.]*\) (.*/\1/p')"
        EMSDK_NODE_PATH="${EMSDK_NODE:-}"
        if [ -z "$EMSDK_NODE_PATH" ] || [ ! -x "$EMSDK_NODE_PATH" ]; then
            AVAIL[wasm-O2-emsdk-node]=0; REASON[wasm-O2-emsdk-node]="emsdk's Node is not available"
        fi
    fi
    # A variant that runs another variant's binary is only as available as that variant.
    for v in "${VARIANTS[@]}"; do
        if [ "${KIND[$v]}" = same-binary ] && [ "${AVAIL[${BASE[$v]}]}" != 1 ] && [ "${AVAIL[$v]}" = 1 ]; then
            AVAIL[$v]=0; REASON[$v]="needs ${BASE[$v]}: ${REASON[${BASE[$v]}]}"
        fi
    done
}

# --------------------------------------------------------------------------------------
# Building
# --------------------------------------------------------------------------------------
declare -A BINPATH RUNNER

# GEN: the CMake generator for a build directory: the one it was configured with (changing
# it is an error), else Ninja if installed, else CMake's default.
GEN=()
pick_generator() { # dir
    GEN=()
    local cached=""
    [ -f "$1/CMakeCache.txt" ] && cached="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$1/CMakeCache.txt")"
    if [ -n "$cached" ]; then GEN=(-G "$cached")
    elif command -v ninja >/dev/null 2>&1; then GEN=(-G Ninja); fi
}

build_variant() { # variant ; sets BINPATH; returns 0 on success
    local v="$1" dir="$BUILD_ROOT/$1" log="$BUILD_ROOT/$1.build.log"
    local kind="${KIND[$v]}"
    [ "$kind" = same-binary ] && return 0
    pick_generator "$dir"
    local -a cfg=(-S "$root" -B "$dir" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Gate -DBITRIOT_BUILD_GAME=OFF -DBITRIOT_BUILD_TESTS=OFF
                  "-DCMAKE_CXX_FLAGS=${FLAGS[$v]}" "-DCMAKE_EXE_LINKER_FLAGS=${LDFLAGS[$v]:-}")
    local launcher=()
    case "$kind" in
        native-gcc)   cfg+=(-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++); BINPATH[$v]="$dir/bitriot_headless" ;;
        native-clang) cfg+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++); BINPATH[$v]="$dir/bitriot_headless" ;;
        aarch64)      cfg+=(-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64
                            -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++)
                      BINPATH[$v]="$dir/bitriot_headless" ;;
        wasm)         launcher=(emcmake)
                      cfg=(-S "$root" -B "$dir" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Gate -DBITRIOT_BUILD_GAME=OFF -DBITRIOT_BUILD_TESTS=OFF
                           "-DCMAKE_CXX_FLAGS=${FLAGS[$v]}" "-DCMAKE_EXE_LINKER_FLAGS=${FLAGS[$v]} $WASM_LINK")
                      BINPATH[$v]="$dir/bitriot_headless.js" ;;
    esac
    {
        echo "== configure $v: ${launcher[*]:-} cmake ${cfg[*]}"
        "${launcher[@]}" cmake "${cfg[@]}" && echo "== build $v" && cmake --build "$dir" --target bitriot_headless -j "$JOBS"
    } > "$log" 2>&1
    local rc=$?
    if [ $rc -ne 0 ] || [ ! -f "${BINPATH[$v]}" ]; then
        return 1
    fi
    return 0
}

# How to start a variant's binary: sets RUNNER[variant] (a command prefix).
set_runner() {
    local v="$1" base="$1"
    [ "${KIND[$v]}" = same-binary ] && base="${BASE[$v]}"
    case "${KIND[$v]}" in
        aarch64) RUNNER[$v]="qemu-aarch64 -L /usr/aarch64-linux-gnu ${BINPATH[$v]}" ;;
        wasm) RUNNER[$v]="$SYSTEM_NODE ${BINPATH[$v]}" ;;
        same-binary)
            case "$v" in
                gcc-O0-valgrind)         RUNNER[$v]="valgrind -q --error-exitcode=99 ${BINPATH[$base]}" ;;
                gcc-O2-malloc-perturb)   RUNNER[$v]="env MALLOC_PERTURB_=165 ${BINPATH[$base]}" ;;
                wasm-O2-emsdk-node)      RUNNER[$v]="$EMSDK_NODE_PATH ${BINPATH[$base]}" ;;
            esac ;;
        *) RUNNER[$v]="${BINPATH[$v]}" ;;
    esac
}

# --------------------------------------------------------------------------------------
# Comparing
# --------------------------------------------------------------------------------------
ms_now() { echo $(( $(date +%s%N) / 1000000 )); }

# first_diff REF GOT -> "line<TAB>reftext<TAB>gottext" for the first line that differs
# (a missing line is shown as <end of output>), nothing if the files are identical.
first_diff() {
    local res
    res="$(awk -F'\n' '
        NR == FNR { ref[FNR] = $0; n = FNR; next }
        { if (FNR > n) { print FNR "\t<end of reference>\t" $0; found = 1; exit }
          if ($0 != ref[FNR]) { print FNR "\t" ref[FNR] "\t" $0; found = 1; exit } }
        END { if (!found && FNR < n) print (FNR + 1) "\t" ref[FNR + 1] "\t<end of output>" }
    ' "$1" "$2")"
    # awk sees nothing to compare when the second file is empty.
    if [ -z "$res" ] && ! cmp -s "$1" "$2"; then
        res="$(printf '1\t%s\t<empty output>' "$(head -1 "$1")")"
    fi
    [ -n "$res" ] && printf '%s\n' "$res"
    return 0
}

# describe_diff "line<TAB>ref<TAB>got" -> "tick N (ref 0x.. got 0x..)" or the raw lines
describe_diff() {
    local line ref got
    IFS=$'\t' read -r line ref got <<< "$1"
    if [[ "$ref" =~ ^hash\ ([0-9]+)\ (0x[0-9a-f]+)$ ]]; then
        local tick="${BASH_REMATCH[1]}" rh="${BASH_REMATCH[2]}"
        if [[ "$got" =~ ^hash\ [0-9]+\ (0x[0-9a-f]+)$ ]]; then
            echo "tick $tick: reference $rh, got ${BASH_REMATCH[1]}"
        else
            echo "tick $tick: reference $rh, output stops or changes: ${got:0:80}"
        fi
    else
        echo "line $line: reference '${ref:0:70}', got '${got:0:70}'"
    fi
}

# diverging_tick "line<TAB>ref<TAB>got" -> the tick number, or "?"
diverging_tick() {
    local line ref got
    IFS=$'\t' read -r line ref got <<< "$1"
    if [[ "$ref" =~ ^hash\ ([0-9]+)\  ]]; then echo "${BASH_REMATCH[1]}"; else echo "?"; fi
}

# crash_note STDERRFILE -> one line: the first sanitizer/Valgrind/abort message, else the last lines
crash_note() {
    local m
    m="$(grep -m1 -E 'runtime error|ERROR:|^==[0-9]+==|Aborted|terminate called|Assertion|Segmentation' "$1" 2>/dev/null | cut -c1-200)"
    if [ -z "$m" ]; then m="$(grep -v '^DEBUG' "$1" 2>/dev/null | tail -3 | tr '\n' ' ' | cut -c1-200)"; fi
    echo "${m:-no message on stderr}"
}

# run_replay VARIANT RECORD OUTFILE [extra args...] : runs replay --hashes; sets RC and ELAPSED_MS
run_replay() {
    local v="$1" rec="$2" out="$3"; shift 3
    local t0; t0="$(ms_now)"
    ( cd "$root" && ${RUNNER[$v]} replay --hashes "$@" "$rec" > "$out" 2> "$out.err" )
    RC=$?
    ELAPSED_MS=$(( $(ms_now) - t0 ))
}

# --------------------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------------------
if [ -z "$BUILD_ROOT" ]; then echo "determinism-gate: --build-root needs a directory" >&2; exit 2; fi
BUILD_ROOT="$(realpath -m -- "$BUILD_ROOT")"
if [ "$CLEAN" = 1 ]; then
    # --clean deletes the build root: never the filesystem root, a home directory or the repository itself.
    for bad in / "$HOME" "$root" "$(dirname "$root")"; do
        if [ "$BUILD_ROOT" = "$(realpath -m -- "$bad")" ]; then
            echo "determinism-gate: refusing to --clean $BUILD_ROOT (use a directory made for the gate, e.g. $root/build-gate)" >&2
            exit 2
        fi
    done
    [ -d "$BUILD_ROOT" ] && rm -rf "$BUILD_ROOT"
fi
OUT="$BUILD_ROOT/_gate"
mkdir -p "$BUILD_ROOT" "$OUT"
check_availability

if [ "$LIST" = 1 ]; then
    printf '%-26s %-10s %s\n' VARIANT AVAILABLE DESCRIPTION
    for v in "${VARIANTS[@]}"; do
        if [ "${AVAIL[$v]}" = 1 ]; then printf '%-26s %-10s %s\n' "$v" yes "${DESC[$v]}"
        else printf '%-26s %-10s %s\n' "$v" no "${REASON[$v]}"; fi
    done
    exit 0
fi

# Which variants to run.
SELECTED=()
if [ -n "$ONLY" ]; then
    IFS=',' read -ra want <<< "$ONLY"
    for w in "${want[@]}"; do
        found=0
        for v in "${VARIANTS[@]}"; do [ "$v" = "$w" ] && found=1; done
        [ $found = 1 ] || { echo "determinism-gate: unknown variant '$w' (see --list)" >&2; exit 2; }
    done
    # the reference first, then the others in table order
    SELECTED=(gcc-O2)
    for v in "${VARIANTS[@]}"; do
        [ "$v" = gcc-O2 ] && continue
        for w in "${want[@]}"; do [ "$v" = "$w" ] && SELECTED+=("$v"); done
    done
else
    SELECTED=("${VARIANTS[@]}")
fi
# a same-binary variant needs its base built
for v in "${SELECTED[@]}"; do
    if [ "${KIND[$v]}" = same-binary ]; then
        b="${BASE[$v]}"; have=0
        for s in "${SELECTED[@]}"; do [ "$s" = "$b" ] && have=1; done
        [ $have = 1 ] || SELECTED=("$b" "${SELECTED[@]}")
    fi
done

if [ "${AVAIL[gcc-O2]}" != 1 ]; then
    echo "determinism-gate: the reference variant (gcc-O2) is not available: ${REASON[gcc-O2]}" >&2
    exit 2
fi

echo "determinism gate: ${#SELECTED[@]} variants, build root $BUILD_ROOT, $JOBS build jobs"
[ -n "$EMSCRIPTEN_VERSION" ] && echo "emscripten $EMSCRIPTEN_VERSION (pinned: $PINNED_EMSCRIPTEN), node ${SYSTEM_NODE} $("$SYSTEM_NODE" --version 2>/dev/null)${EMSDK_NODE_PATH:+, emsdk node $("$EMSDK_NODE_PATH" --version 2>/dev/null)}"
if [ -n "$EMSCRIPTEN_VERSION" ] && [ "$EMSCRIPTEN_VERSION" != "$PINNED_EMSCRIPTEN" ]; then
    echo "warning: Emscripten $EMSCRIPTEN_VERSION ($(command -v emcc)) is not the pinned $PINNED_EMSCRIPTEN (scripts/setup-emsdk.sh installs the pin)" >&2
    MISMATCH=1
fi

declare -A STATUS NOTE GOLDEN_MS START_MS NC_STATUS NC_NOTE
FAILED=0
if [ "${MISMATCH:-0}" = 1 ] && [ "$STRICT" = 1 ]; then
    echo "determinism-gate: --strict and the Emscripten version is not the pinned one: failing" >&2
    FAILED=1
fi
RAN=()

# Build and set up each selected, available variant.
for v in "${SELECTED[@]}"; do
    if [ "${AVAIL[$v]}" != 1 ]; then
        STATUS[$v]="n/a"; NOTE[$v]="${REASON[$v]}"
        [ "$STRICT" = 1 ] && FAILED=1
        continue
    fi
    t0="$(ms_now)"
    printf 'building %-26s ' "$v"
    if [ "${KIND[$v]}" = same-binary ] && [ -z "${BINPATH[${BASE[$v]}]:-}" ]; then
        echo "FAILED (its build, ${BASE[$v]}, is missing)"
        STATUS[$v]="FAIL"; NOTE[$v]="the ${BASE[$v]} build it runs failed"; FAILED=1
    elif build_variant "$v"; then
        echo "ok ($(( ($(ms_now) - t0) / 1000 )) s)"
        RAN+=("$v")
    else
        echo "FAILED (see $BUILD_ROOT/$v.build.log)"
        STATUS[$v]="FAIL"; NOTE[$v]="build failed: $(grep -m1 -E 'error|Error' "$BUILD_ROOT/$v.build.log" | cut -c1-110)"
        FAILED=1
        if [ "$v" = gcc-O2 ]; then
            echo "determinism-gate: the reference build failed; nothing to compare against" >&2
            exit 2
        fi
    fi
done
for v in "${RAN[@]}"; do set_runner "$v"; done

# The reference's own version, and the records.
ref=gcc-O2
mkdir -p "$OUT/records" "$OUT/ref"
RECORDS=("golden")
declare -A RECFILE
RECFILE[golden]="$GOLDEN"
if [ "$EXTRA_ROUNDS" = 1 ]; then
    # name|seed|config : rounds the reference plays itself (all AI), with a checkpoint
    # on every tick so a divergence is pinned to its exact tick.
    EXTRA=("17x13-2p|101|players=2"
           "13x11-6p|202|players=6,map=13x11"
           "33x25-4p-dense|303|players=4,map=33x25,blocks=70"
           "21x15-teams|404|players=4,map=21x15,teams=0:0:1:1"
           "15x11-3p-bare|505|players=3,map=15x11,blocks=0,spawner=off"
           "25x19-5p-solid|606|players=5,map=25x19,blocks=100")
    for e in "${EXTRA[@]}"; do
        IFS='|' read -r name seed cfg <<< "$e"
        file="$OUT/records/$name.brmr"
        if ( cd "$root" && ${RUNNER[$ref]} run --out "$file" --seed "$seed" --config "$cfg" --checkpoint-every 1 --max-ticks 6000 ) > "$OUT/records/$name.run.txt" 2>&1; then
            RECORDS+=("$name"); RECFILE[$name]="$file"
        else
            echo "GATE FAILED: the reference could not play the extra round $name (see $OUT/records/$name.run.txt)" >&2
            exit 1
        fi
    done
fi

# Reference outputs.
for r in "${RECORDS[@]}"; do
    run_replay "$ref" "${RECFILE[$r]}" "$OUT/ref/$r.out"
    if [ $RC -ne 0 ]; then
        # The reference built, started and still disagrees with a record: that is a
        # determinism failure (or a stale record), not a setup problem.
        echo >&2
        echo "GATE FAILED: the reference build (gcc-O2) does not replay the record '$r' (exit $RC), so there is nothing to compare the other variants with:" >&2
        tail -3 "$OUT/ref/$r.out" >&2
        [ "$r" = golden ] && echo "(if the simulation was changed on purpose, regenerate the golden record; see tests/README.md)" >&2
        exit 1
    fi
done
( cd "$root" && ${RUNNER[$ref]} version ) > "$OUT/ref/version.out" 2>/dev/null
# Exceptions probes: a thrown and caught C++ exception (a missing file, a bad option) must
# behave the same in every build; it is what -fwasm-exceptions has to get right.
# The third one is thrown inside the simulation (an event for a slot the round lacks), deep
# in Simulation::step, and caught in the replayer: a hand-made one-tick record, 2 players.
z8='\x00\x00\x00\x00\x00\x00\x00\x00'
printf "BRMR\x01\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00${z8}${z8}\x01\x00\x00\x00\x00\x00\x00\x00\x0d\x00\x0b\x00\x37\x00\x01\x02\x00\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x00\x00\x01\x05\x00" > "$OUT/probe-badslot.brmr"
PROBES=("replay /no/such/dir/missing.brmr" "run --seed 1 --out $OUT/probe.brmr --config players=9" "replay"
        "replay --ignore-data-hash $OUT/probe-badslot.brmr")
run_probes() { # variant outdir
    local v="$1" dir="$2" i=0 p
    for p in "${PROBES[@]}"; do
        # stderr is compared without the DEBUG log lines that builds without NDEBUG print.
        ( cd "$root" && ${RUNNER[$v]} $p ) > "$dir/probe$i.out" 2> "$dir/probe$i.err.full"
        echo $? > "$dir/probe$i.rc"
        grep -E '^(error|warning|note):' "$dir/probe$i.err.full" > "$dir/probe$i.err" || true
        i=$((i+1))
    done
}
run_probes "$ref" "$OUT/ref"
echo "reference gcc-O2: $(grep -m1 'build id' "$OUT/ref/version.out"), $(grep -m1 'data hash' "$OUT/ref/version.out"); records: ${RECORDS[*]}"
echo "golden record: $(grep -m1 '^ticks:' "$OUT/ref/golden.out")"

for v in "${RAN[@]}"; do
    mkdir -p "$OUT/$v"
    problems=()
    # Same source and data: the build id and the data hash must be identical.
    t0="$(ms_now)"
    ( cd "$root" && ${RUNNER[$v]} version ) > "$OUT/$v/version.out" 2> "$OUT/$v/version.err"
    vrc=$?
    START_MS[$v]=$(( $(ms_now) - t0 ))
    if [ $vrc -ne 0 ] || ! cmp -s "$OUT/ref/version.out" "$OUT/$v/version.out"; then
        problems+=("version differs (exit $vrc): not the same source/data as the reference: $(first_diff "$OUT/ref/version.out" "$OUT/$v/version.out" | tr '\t' ' ' | cut -c1-120)")
    fi
    firsttick=""
    myrecords=("${RECORDS[@]}")
    if [ -n "${ONLY_RECORDS[$v]:-}" ]; then
        myrecords=()
        for r in ${ONLY_RECORDS[$v]}; do
            [ -n "${RECFILE[$r]:-}" ] && myrecords+=("$r")
        done
    fi
    for r in "${myrecords[@]}"; do
        run_replay "$v" "${RECFILE[$r]}" "$OUT/$v/$r.out"
        rc=$RC
        [ "$r" = golden ] && GOLDEN_MS[$v]=$ELAPSED_MS
        d="$(first_diff "$OUT/ref/$r.out" "$OUT/$v/$r.out")"
        if { [ $rc -gt 1 ] && [ $rc -ne 3 ]; } || ! grep -q '^replay: ' "$OUT/$v/$r.out"; then
            # Not a mismatch: a crash, an abort, a sanitizer or Valgrind error (UBSan exits 1,
            # like a mismatch, but the output has no closing "replay:" line), a bad file.
            problems+=("$r: exit $rc, not a hash mismatch: $(crash_note "$OUT/$v/$r.out.err"); last hash printed: $(grep -c '^hash ' "$OUT/$v/$r.out" | awk '{print ($1>0)?$1-1:"none"}')")
            [ -z "$firsttick" ] && firsttick="?"
        elif [ -n "$d" ]; then
            problems+=("$r: $(describe_diff "$d") (exit $rc)")
            t="$(diverging_tick "$d")"
            [ -z "$firsttick" ] && firsttick="$t"
        elif [ $rc -ne 0 ]; then
            problems+=("$r: output identical but exit code $rc: $(crash_note "$OUT/$v/$r.out.err")")
        fi
    done
    run_probes "$v" "$OUT/$v"
    for i in "${!PROBES[@]}"; do
        if ! cmp -s "$OUT/ref/probe$i.rc" "$OUT/$v/probe$i.rc" || ! cmp -s "$OUT/ref/probe$i.err" "$OUT/$v/probe$i.err" \
           || ! cmp -s "$OUT/ref/probe$i.out" "$OUT/$v/probe$i.out"; then
            problems+=("exception probe '${PROBES[$i]:0:40}': exit $(cat "$OUT/$v/probe$i.rc") (reference $(cat "$OUT/ref/probe$i.rc")), stderr: $(head -c 120 "$OUT/$v/probe$i.err" | tr '\n' ' ')")
        fi
    done
    if [ ${#problems[@]} -eq 0 ]; then
        STATUS[$v]="PASS"; NOTE[$v]="${#myrecords[@]} records identical on every tick; exceptions behave the same"
    else
        STATUS[$v]="FAIL"; NOTE[$v]="${problems[0]}"
        FAILED=1
        printf '  %s:\n' "$v"; printf '    %s\n' "${problems[@]}"
    fi
done

# --------------------------------------------------------------------------------------
# Negative control: a one-unit perturbation after tick PERTURB_TICK-1 must be reported by
# the very same comparison, as a failure at exactly tick PERTURB_TICK, on every variant.
# --------------------------------------------------------------------------------------
NC_FAILED=0
for v in "${RAN[@]}"; do
    run_replay "$v" "$GOLDEN" "$OUT/$v/golden.perturbed.out" --perturb-at "$PERTURB_TICK"
    rc=$RC
    d="$(first_diff "$OUT/ref/golden.out" "$OUT/$v/golden.perturbed.out")"
    t="$(diverging_tick "$d")"
    if [ -n "$d" ] && [ "$rc" = 1 ] && [ "$t" = "$PERTURB_TICK" ]; then
        NC_STATUS[$v]="caught"; NC_NOTE[$v]="fails at tick $t (expected $PERTURB_TICK), exit $rc"
    else
        NC_STATUS[$v]="NOT CAUGHT"; NC_FAILED=1
        NC_NOTE[$v]="expected a failure at tick $PERTURB_TICK with exit 1; got: ${d:+$(describe_diff "$d"), }exit $rc"
    fi
done

# --------------------------------------------------------------------------------------
# Report
# --------------------------------------------------------------------------------------
{
    echo
    echo "| Variant | Result | Negative control | Startup ms | Golden replay ms | Notes |"
    echo "| --- | --- | --- | --- | --- | --- |"
    for v in "${SELECTED[@]}"; do
        s="${STATUS[$v]:-?}"
        if [ "$s" = "n/a" ]; then
            echo "| $v | not available | - | - | - | ${NOTE[$v]} |"
        else
            echo "| $v | $s | ${NC_STATUS[$v]:--} | ${START_MS[$v]:--} | ${GOLDEN_MS[$v]:--} | ${NOTE[$v]} |"
        fi
    done
} > "$OUT/results.md"

echo
printf '%-26s %-14s %-17s %9s %9s  %s\n' VARIANT RESULT "NEG. CONTROL" "START ms" "GOLDEN ms" NOTES
for v in "${SELECTED[@]}"; do
    s="${STATUS[$v]:-?}"
    if [ "$s" = "n/a" ]; then
        printf '%-26s %-14s %-17s %9s %9s  %s\n' "$v" "not available" "-" "-" "-" "${NOTE[$v]}"
    else
        printf '%-26s %-14s %-17s %9s %9s  %s\n' "$v" "$s" "${NC_STATUS[$v]:--}" "${START_MS[$v]:--}" "${GOLDEN_MS[$v]:--}" "${NOTE[$v]:0:100}"
    fi
done

npass=0; nfail=0; nna=0
for v in "${SELECTED[@]}"; do
    case "${STATUS[$v]:-?}" in PASS) npass=$((npass+1)) ;; n/a) nna=$((nna+1)) ;; *) nfail=$((nfail+1)) ;; esac
done
echo
echo "variants: $npass passed, $nfail failed, $nna not available; records per variant: ${#RECORDS[@]} (${RECORDS[*]})"
if [ $NC_FAILED = 1 ]; then
    echo "NEGATIVE CONTROL FAILED: the gate did not catch a one-unit perturbation at tick $PERTURB_TICK on every variant, so it cannot be trusted"
else
    echo "negative control: a one-unit perturbation at tick $PERTURB_TICK was reported at exactly tick $PERTURB_TICK on all ${#RAN[@]} variants that ran"
fi
echo "results: $OUT/results.md (outputs per variant under $OUT/)"
if [ $FAILED = 1 ] || [ $NC_FAILED = 1 ]; then
    echo "GATE FAILED"
    exit 1
fi
if [ $nna -gt 0 ]; then
    echo "GATE PASSED (every variant that ran matched; $nna not available, see above; --strict would fail on that)"
else
    echo "GATE PASSED"
fi
exit 0
