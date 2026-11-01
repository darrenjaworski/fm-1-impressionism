#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Dinghy's host tests (no hardware). Run from the repo root after building, the SMK-37 first, the FM-1 last
# (both boards write the same build/ intermediates; the tests expect the FM-1 binaries):
#   BOARD=smk37 sh build.sh; BOARD=smk37 MODEL=elite sh build.sh; BOARD=smk37 MODEL=mke sh build.sh; sh build.sh
#   sh tests/run_tests.sh
# The FM-1 app on the host (tests/run_host.sh), MIDI and the SMK-37 on the host (tests/scripts), the flash map, the
# update path (ota.c, the loader, tools/fm1_install.py; from the fm1-to-smk37 kit's reference layer,
# genkigenki/sloop-fm1 smk37) and the packages.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # (fm1pkg_make.py: the SDK files of a package)
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

[ -f build/impressionism.fwsc ] || { echo "run ./build.sh first"; exit 1; }

run "FM-1 app on the host: the default script plays, screens and sound" sh tests/run_host.sh
run "SMK-37 app on the host: the default script" env BOARD=smk37 sh tests/run_host.sh
# midi.txt / smk37.txt drive keys, MIDI and the pitch wheel -- all intentionally unused by the v1 generative
# engine (ROADMAP.md: no performance input in v1). They still pass (tests/host.c only fails on file I/O errors,
# not on what the inputs did), just don't exercise anything app-specific any more; kept for the platform-level
# USB/TRS/clock/wheel coverage underneath, not for impressionism.c's own behaviour.
run "FM-1 MIDI: USB, the TRS jack, a clock, the channel (platform-level only, v1 ignores all of it)" sh tests/run_host.sh tests/scripts/midi.txt
run "SMK-37: velocity, a pad, an outer key, the pitch wheel (platform-level only, v1 ignores all of it)" env BOARD=smk37 sh tests/run_host.sh tests/scripts/smk37.txt

# Audio checks (tests/wav_check.py, stdlib only): each engine script is rendered on the host, then its WAV is checked for
# clipping and silence, and for a window that must sit well below another (the pause). Then long.txt is rendered again
# and must come out byte-identical: the render is deterministic, so any difference is a real change.
audio() { what=$1; script=$2; shift 2; echo "== $what"; sh tests/run_host.sh "tests/scripts/$script.txt" && python3 tests/wav_check.py build/host/impressionism.wav "$@" || fail=1; }
audio "FM-1 long.txt, 3 minutes: no clipping, not silent" long
cp build/host/impressionism.wav "$OUT/long_a.wav" || fail=1
run "FM-1 long.txt rendered again: byte-identical WAV" sh -c "sh tests/run_host.sh tests/scripts/long.txt >/dev/null && cmp '$OUT/long_a.wav' build/host/impressionism.wav && echo identical"
audio "FM-1 pause.txt: no clipping, not silent, and the last second of the pause (24-25 s) 40 dB below the second before it (19-20 s)" pause --drop 24:25 19:20 40
audio "FM-1 scales.txt: no clipping, not silent, through all six scales" scales
audio "FM-1 droneoff.txt, 3 minutes with the drone off: no clipping, not silent" droneoff
audio "FM-1 worstcase.txt, 4 minutes at density 3, drift 3, colour 31 in all six scales: no clipping, not silent" worstcase

$CC -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/smk_flash_test" tests/smk_flash_test.c
run "SMK-37 flash map: settings, blobs, owner tag, slots in the store; never the head, BTIF/USRTRIM, USR, key_mac" "$OUT/smk_flash_test"
$CC -o "$OUT/smk_wheel_test" tests/smk_wheel_test.c
run "SMK-37 pitch wheel: the bend is back to 0 at rest, however slowly the wheel was moved" "$OUT/smk_wheel_test"
$CC -o "$OUT/synth_test" tests/synth_test.c
run "the synthesis helpers: sine, the phase-step table, note_to_inc, pitch_drift at the top step" "$OUT/synth_test"
$CC -o "$OUT/planing_test" tests/planing_test.c
run "the generative engine's pure logic: scales, chord templates, the planing picker, the scheduler, the register limit" "$OUT/planing_test"
$CC -o "$OUT/scene_test" tests/scene_test.c
run "the reactive lily-pond screen's pure logic: water colour, bloom palette, bloom density, pad wander, water shimmer, layout" "$OUT/scene_test"
$CC -o "$OUT/status_test" tests/status_test.c
run "the status-text overlay's pure logic: every scale, drone, density and knob line fits the 240 px row" "$OUT/status_test"
$CC -o "$OUT/reverb_test" tests/reverb_test.c
run "Schroeder reverb: deterministic, bounded, T60 by the T30 method, a sustained full-scale input" "$OUT/reverb_test"
$CC -o "$OUT/envelope_test" tests/envelope_test.c
run "voice envelope: attack, exponential release, quiet threshold" "$OUT/envelope_test"
$CC -o "$OUT/engine_test" tests/engine_test.c
run "the generative engine on a fake clock (3 h, 4 seeds, all scales/densities/drifts): notes in scale and register, pause, resume, wrap, note rate by density; and the saved settings" "$OUT/engine_test"

$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry (FM-1)" "$OUT/ota_test" build/impressionism.fwsc
head -c 60000 build/impressionism.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_screen_test" tests/ldr_screen_test.c
run "update loader's screen (from Skiff): UPDATE MODE, WRITING and its bar, UPDATE FAILED" "$OUT/ldr_screen_test" "$OUT"
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader (FM-1): other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/impressionism.fwsc

# SMK-37: the OTA staging and the loader windows on SMK-layout packages (made here from the FM-1 binaries: only the
# layout and the protocol are under test), then the real SMK packages
python3 tools/fm1pkg_make.py --board smk37 build/impressionism.bin build/loader/ota.bin "$OUT/smk_new.fwsc" >/dev/null
python3 tools/fm1pkg_make.py --board smk37 "$OUT/old_app.bin" build/loader/ota.bin "$OUT/smk_old.fwsc" >/dev/null
run "SMK-37 package layout (fm1pkg_make --board smk37): PRCT 0x9D000, stock region table" \
    python3 tests/smk_pkg_test.py "$OUT/smk_new.fwsc" "SMK-37 Pro_900"
$CC -DBOARD_SMK37=1 -o "$OUT/ota_test_smk" tests/ota_test.c
run "M-UPGRADE entry, SMK-37 Pro (staging 0xBB000, parm ota-SMK-37 Pro_016)" "$OUT/ota_test_smk" "$OUT/smk_new.fwsc"
for m in 1 2; do
    $CC -DBOARD_SMK37=1 -DT_MODEL=$m -o "$OUT/ota_test_smk_m$m" tests/ota_test.c
    run "M-UPGRADE entry, SMK-37 model $m (1 Elite, 2 MKE-P37): identity and parm under its name" "$OUT/ota_test_smk_m$m" "$OUT/smk_new.fwsc"
done
$CC -DBOARD_SMK37=1 -o "$OUT/ldr_test_smk" tests/ldr_test.c
run "update loader, SMK-37: app area 0x4000-0x9CFFF, protected ranges untouched" "$OUT/ldr_test_smk" "$OUT/smk_old.fwsc" "$OUT/smk_new.fwsc"
found=0
for m in ":SMK-37 Pro_95" "-elite:SMK-37 Elite_95" "-mke:MKE-P37_95"; do
    for f in build/impressionism*-smk37${m%%:*}.fwsc; do
        [ -f "$f" ] || continue
        found=1
        v=$(echo "$f" | sed -n 's/.*impressionism-[0-9]\.\([0-9]\).*/\1/p'); v=${v:-0}
        run "SMK-37 package $f: layout, identity ${m#*:}$v, the loader marker" python3 tests/smk_pkg_test.py "$f" "${m#*:}$v"
    done
done
[ "$found" = 1 ] || echo "(no SMK-37 packages in build/: BOARD=smk37 sh build.sh first)"

run "installer (tools/fm1_install.py): FM-1 and SMK-37, install and the update-mode resume check the model" python3 tests/install_test.py

[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit "$fail"
