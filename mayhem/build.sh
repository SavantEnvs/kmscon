#!/usr/bin/env bash
# mayhem/build.sh — build kmscon's fuzz harnesses (sanitized) and its test suite (normal flags).
#
# kmscon is C (meson) + an in-tree Rust libtsm (src/tsm) built by meson's rust module. Toolchains
# come from the image: meson under /opt/toolchains/meson, rustc/cargo under /opt/toolchains/rust.
# The four crates libtsm depends on (vte, unicode-width, arrayvec, memchr) are meson wraps resolved
# from the pre-filled package cache $MESON_PACKAGE_CACHE_DIR (/opt/toolchains/deps/meson-packagecache),
# so this script runs offline and can be re-run on an already-built tree.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${MESON_PACKAGE_CACHE_DIR:=/opt/toolchains/deps/meson-packagecache}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS MESON_PACKAGE_CACHE_DIR

cd "$SRC"

# Features that need hardware/GL/system services are off; the font backends we fuzz (psf) stay on.
MESON_OPTS=(
  -Dvideo_drm3d=disabled -Dvideo_drm2d=disabled -Drenderer_gltex=disabled
  -Dfont_pango=disabled -Dfont_freetype=disabled -Ddbus=disabled -Dpam=disabled
  -Ddocs=disabled -Dlibseat=disabled -Dfont_psf=enabled -Dfont_unifont=enabled
)

meson_setup() { # dir, args... — configure, or reconfigure an existing build dir (re-runnable)
  local dir=$1; shift
  if [ -f "$dir/build.ninja" ]; then
    meson setup --reconfigure "$dir" "$@"
  else
    rm -rf "$dir"
    meson setup "$dir" "$@"
  fi
}

# 1) Test suite with upstream's NORMAL flags (test.sh runs `meson test` on it).
meson_setup mayhem/build-test "${MESON_OPTS[@]}" -Dtests=true \
  -Dc_args="$COVERAGE_FLAGS" -Dc_link_args="$COVERAGE_FLAGS"
ninja -C mayhem/build-test -j"$MAYHEM_JOBS"

# 2) Sanitized + coverage-instrumented project build. C gets ASan+UBSan (halting) and SanCov;
#    the Rust libtsm gets SanCov edge counters + cmp tracing (stable rustc passes), DWARF 3, and
#    overflow checks (a panic aborts across the C FFI, so it is a crash).
RUST_FUZZ_FLAGS="-C passes=sancov-module -C llvm-args=-sanitizer-coverage-level=3 \
-C llvm-args=-sanitizer-coverage-inline-8bit-counters -C llvm-args=-sanitizer-coverage-pc-table \
-C llvm-args=-sanitizer-coverage-trace-compares -C dwarf-version=3 -C force-frame-pointers=yes \
-C overflow-checks=yes"
# meson's rustc sanity check links a small executable; ASan's runtime provides weak
# __sanitizer_cov_* definitions so it links (the staticlib itself is linked by the harnesses).
RUST_FUZZ_FLAGS="$RUST_FUZZ_FLAGS -C link-arg=-fsanitize=address"
meson_setup mayhem/build-fuzz "${MESON_OPTS[@]}" -Dtests=false -Dwerror=false -Db_lundef=false \
  -Dc_args="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link" \
  -Dc_link_args="$SANITIZER_FLAGS" \
  -Drust_args="$RUST_FUZZ_FLAGS"
ninja -C mayhem/build-fuzz -j"$MAYHEM_JOBS"

B=mayhem/build-fuzz
CFLAGS_H=(-include "$B/config.h" -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -std=gnu99
          -I src -I src/tsm -I src/misc -I src/font -I "$B/src/shl" -I "$B" -I mayhem/harnesses)
TSM="$B/src/tsm/libtsm.a"
SHL=("$B/src/shl/libshl.a" "$B/src/shl/htable/libext-htable.a")
SYS=(-lpthread -ldl -lm -lz)

$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.c -o "$B/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/standalone_main.o"
# PSF backend: built into mod-psf.so upstream; compiled here with the same flags and linked statically.
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link "${CFLAGS_H[@]}" -fvisibility=default \
  -c src/font/font_psf.c -o "$B/font_psf.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link "${CFLAGS_H[@]}" \
  -c src/config.c -o "$B/config.o"

link() { # name, extra objects/libs...
  local name=$1; shift
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS "${CFLAGS_H[@]}" -c "mayhem/harnesses/$name.c" -o "$B/$name.o"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$B/$name.o" "$B/lsan_off.o" "$@" "${SYS[@]}" \
    -o "$SRC/$name"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS "$B/standalone_main.o" "$B/$name.o" "$B/lsan_off.o" "$@" "${SYS[@]}" \
    -o "$SRC/$name-standalone"
}

link fuzz_tsm_vte "$TSM"
link fuzz_font_psf "$B/font_psf.o" "$B/src/font/libinput.a" "${SHL[@]}" "$TSM"
U="$B/src/font/mod-unifont.so.p"
link fuzz_font_unifont "$U/font_unifont.c.o" "$U/meson-generated_font_unifont_data.bin.c.o" \
  "$B/src/font/libinput.a" "${SHL[@]}" "$TSM"
link fuzz_asciinema "$B/src/misc/libmisc.a" "$B/src/shl/libeloop.a" "${SHL[@]}" "$TSM" -lxkbcommon
link fuzz_issue "$B/src/misc/libmisc.a" "${SHL[@]}" "$TSM" -lxkbcommon
link fuzz_render "$B/src/render/librender.a" "$B/src/font/libinput.a" "${SHL[@]}" "$TSM"
link fuzz_conf "$B/config.o" "$B/src/misc/libmisc.a" "${SHL[@]}" "$TSM" -lxkbcommon

echo "built: $(ls "$SRC"/fuzz_* | tr '\n' ' ')"
