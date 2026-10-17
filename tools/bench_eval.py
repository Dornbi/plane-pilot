#!/usr/bin/env python3
"""
Deterministic cycle, RAM, and visual-hash evaluation harness for Plane Pilot.

Runs:
  1. Build + static verification checks (check_zeropage, check_irq_zp,
     check_mul_div, check_rom_window, check_boot_region, check_narrowing)
     and host + 6510 target unit tests (`make -C c64o all`).
  2. RAM analysis (`tools/analyze_ram.py`) across `ppilot.map`, `ppilota.map`,
     and `fpilot.map` (reporting free allocatable bytes, largest contiguous
     run, segment totals, and zero-page / boot / stack headroom).
  3. Headless VICE (`x64sc`) cycle + visual-hash benchmark across four
     canonical poses (`runway`, `approach`, `cruise`, `banked`) in a single
     emulator run:
       - `real_work`: VIC-II DMA + raster-IRQ frame cycles (comparable with
         docs/framerate.md).
       - `cpu_total` & stage breakdown (`MDL`, `UPD`, `SNP`, `BGR`, `CHR`,
         `DRW`, `GRD`, `PNL`, `COL`, and sub-stages `CLD`, `SPR`, `PLY`):
         measured with badline/sprite DMA and raster IRQs suppressed so every
         6510 instruction cycle change is exact to the single cycle with zero
         cross-stage raster phase noise.
       - `frame_hash`: 32-bit FNV-1a checksum over Screen RAM, Color RAM
         buffer, and Character RAM after each pose to catch visual regressions.

Usage:
  python3 tools/bench_eval.py                       # run and print report
  python3 tools/bench_eval.py --save-baseline       # save baseline JSON
  python3 tools/bench_eval.py --compare             # compare against baseline
  python3 tools/bench_eval.py --quick               # skip Python generator tests
  python3 tools/bench_eval.py --json                # print JSON output
"""

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import analyze_ram  # noqa: E402

DEFAULT_BASELINE = "/tmp/ppilot-bench-baseline.json"
BENCH_BUILD_DIR = os.environ.get("BENCH_BUILD_DIR", "/tmp/ppilot-bench")
OSCAR64_INCLUDE = os.environ.get(
    "OSCAR64_INCLUDE", os.path.abspath(os.path.join(REPO, "..", "..", "oscar64-main", "include"))
)

POSES = ("runway", "approach", "cruise", "banked")
POSE_FIELDS = (
    "cpu_total",
    "real_work",
    "mdl",
    "upd",
    "snp",
    "bgr",
    "chr",
    "drw",
    "grd",
    "pnl",
    "col",
    "cld",
    "spr",
    "ply",
    "frame_hash",
)

BENCH_C_CODE = r"""
#include "benchmark.h"

#include <stdint.h>
#include <string.h>

#include "chardefs.h"
#include "cia.h"
#include "flight.h"
#include "gfx.h"
#include "keys.h"
#include "mem.h"
#include "msg.h"
#include "sound.h"
#include "sprites.h"
#include "vec.h"
#include "vic.h"
#include "view.h"

extern bool mem_debug_enabled;

struct BenchPoseResult {
  uint32_t cpu_total;
  uint32_t real_work;
  uint32_t mdl;
  uint32_t upd;
  uint32_t snp;
  uint32_t bgr;
  uint32_t chr;
  uint32_t drw;
  uint32_t grd;
  uint32_t pnl;
  uint32_t col;
  uint32_t cld;
  uint32_t spr;
  uint32_t ply;
  uint32_t frame_hash;
};

volatile struct BenchPoseResult g_bench_results[4];

static uint32_t _bm_start_ts;
static uint32_t _bm_work_start_ts;
static uint32_t _bm_pnl_start_ts;
static uint32_t _bm_sub_start_ts;
static uint32_t _bm_sub_accum[BM_SUB_COUNT];

// Non-zero initializer places _bench_magic in data (not uninitialized -xz bss).
static uint8_t _bench_magic = 0x5A;
static uint8_t _bench_pose;
static uint8_t _bench_sub;

static inline uint32_t _cia2_now(void) {
  uint16_t tb1 = cia2.tb;
  uint16_t ta = cia2.ta;
  uint16_t tb2 = cia2.tb;
  if (tb1 != tb2) {
    ta = cia2.ta;
  }
  return (((uint32_t)tb2 << 16) | ta);
}

void bm_init(void) {
  cia2.cra = 0;
  cia2.crb = 0;
  cia2.ta = 0xffff;
  cia2.tb = 0xffff;
  cia2.crb = 0x41;
  cia2.cra = 0x11;
}

void bm_start(void) {
  uint32_t now = _cia2_now();
  _bm_start_ts = now;
  _bm_work_start_ts = now;
}

void bm_view_start(void) { _bm_start_ts = _cia2_now(); }

void bm_model_start(void) { _bm_start_ts = _cia2_now(); }

void bm_poly_start(void) { _bm_start_ts = _cia2_now(); }

static void _record_stage(uint16_t pos, uint32_t elapsed) {
  if (_bench_pose >= 4 || _bench_sub != 3) {
    return;
  }
  volatile struct BenchPoseResult *r = &g_bench_results[_bench_pose];
  switch (pos) {
    case 630:
      r->mdl = elapsed;
      break;
    case 779:
      r->upd = elapsed;
      break;
    case 670:
      r->snp = elapsed;
      break;
    case 710:
      r->bgr = elapsed;
      break;
    case 750:
      r->chr = elapsed;
      break;
    case 790:
      r->drw = elapsed;
      break;
    case 830:
      r->grd = elapsed;
      break;
    case 910:
      r->col = elapsed;
      r->cld = _bm_sub_accum[BM_SUB_CLOUDS];
      r->spr = _bm_sub_accum[BM_SUB_SPRITES];
      r->ply = _bm_sub_accum[BM_SUB_POLY];
      r->cpu_total = r->mdl + r->upd + r->snp + r->bgr + r->chr + r->drw +
                     r->grd + r->pnl + r->col;
      break;
  }
}

void bm_end(uint16_t pos, const char *label) {
  uint32_t now = _cia2_now();
  uint32_t elapsed = _bm_start_ts - now;
  _record_stage(pos, elapsed);
  if (pos == 830) {
    _bm_pnl_start_ts = _cia2_now();
  }
}

void bm_view_end(uint16_t pos, const char *label) { bm_end(pos, label); }

void bm_model_end(uint16_t pos, const char *label) { bm_end(pos, label); }

void bm_poly_end(uint16_t pos, const char *label) { bm_end(pos, label); }

void bm_total(uint16_t pos, const char *label) {
  uint32_t now = _cia2_now();
  if (_bench_pose < 4) {
    if (_bench_sub == 2) {
      g_bench_results[_bench_pose].real_work = _bm_work_start_ts - now;
    } else if (_bench_sub == 3) {
      g_bench_results[_bench_pose].pnl = _bm_pnl_start_ts - now;
    }
  }
}

void bm_sub_start(void) { _bm_sub_start_ts = _cia2_now(); }

void bm_sub_end(uint8_t slot) {
  uint32_t now = _cia2_now();
  _bm_sub_accum[slot] += _bm_sub_start_ts - now;
}

void bm_sub_show(uint8_t slot, uint16_t pos, const char *label,
                 uint8_t num_digits) {}

static const int16_t kBenchCos[65] = {
    256, 256, 256, 255, 255, 254, 253, 252, 251, 250, 248, 247, 245,
    243, 241, 239, 237, 234, 231, 229, 226, 223, 220, 216, 213, 209,
    206, 202, 198, 194, 190, 185, 181, 177, 172, 167, 162, 157, 152,
    147, 142, 137, 132, 126, 121, 115, 109, 104, 98,  92,  86,  80,
    74,  68,  62,  56,  50,  44,  38,  31,  25,  19,  13,  6,   0,
};

static int16_t _bench_cos(uint8_t a) {
  if (a < 64) return kBenchCos[a];
  if (a < 128) return -kBenchCos[128 - a];
  if (a < 192) return -kBenchCos[a - 128];
  return kBenchCos[256 - (uint16_t)a];
}

static int16_t _bench_sin(uint8_t a) { return _bench_cos((uint8_t)(a - 64)); }

static int16_t _bench_mul(int16_t a, int16_t b) {
  return (int16_t)(((int32_t)a * (int32_t)b) >> 8);
}

static void _setup_pose(uint8_t pose) {
  view_state = VIEW_CENTER;
  if (pose == 0) {
    // Pose 0: runway (Mission 01 start, paused)
    flight_init_from_mission(0);
    flight_paused = true;
  } else if (pose == 1) {
    // Pose 1: approach (Mission 02 start, 512m on final, paused)
    flight_init_from_mission(1);
    flight_paused = true;
  } else if (pose == 2) {
    // Pose 2: cruise (docs/framerate.md cruise pose, 1400m at cloud deck, paused)
    flight_init_from_mission(0);
    flight_eye_x = 0x8D5000;
    flight_eye_y = 0x2C0000;
    flight_eye_z = 0x057800;
    flight_speed = 0;
    flight_throttle = 0;
    flight_paused = true;
  } else {
    // Pose 3: banked turn over town with active flight_advance() physics
    // Uses radius-3 altitude (0x014000) and roll=40 (tilted horizon + roll_period > 1)
    flight_init_from_mission(3);
    flight_eye_x = (int32_t)0x6000 << 8;
    flight_eye_y = (int32_t)0x9800 << 8;
    flight_eye_z = (int32_t)0x0140 << 8;

    const int16_t cy = _bench_cos(0);
    const int16_t sy = _bench_sin(0);
    const int16_t cp = _bench_cos((uint8_t)-4);
    const int16_t sp = _bench_sin((uint8_t)-4);
    flight_cam.front.x = _bench_mul(cp, cy);
    flight_cam.front.y = _bench_mul(cp, sy);
    flight_cam.front.z = sp;
    flight_cam.left.x = -sy;
    flight_cam.left.y = cy;
    flight_cam.left.z = 0;
    flight_cam.up.x = -_bench_mul(sp, cy);
    flight_cam.up.y = -_bench_mul(sp, sy);
    flight_cam.up.z = cp;

    const int16_t cr = _bench_cos(40);
    const int16_t sr = _bench_sin(40);
    const vec3_t l = flight_cam.left;
    const vec3_t u = flight_cam.up;
    flight_cam.left.x = _bench_mul(l.x, cr) - _bench_mul(u.x, sr);
    flight_cam.left.y = _bench_mul(l.y, cr) - _bench_mul(u.y, sr);
    flight_cam.left.z = _bench_mul(l.z, cr) - _bench_mul(u.z, sr);
    flight_cam.up.x = _bench_mul(u.x, cr) + _bench_mul(l.x, sr);
    flight_cam.up.y = _bench_mul(u.y, cr) + _bench_mul(l.y, sr);
    flight_cam.up.z = _bench_mul(u.z, cr) + _bench_mul(l.z, sr);

    flight_speed = 0x0900;
    flight_throttle = 0x14;
    flight_gear = false;
    flight_flap = false;
    flight_paused = false;
  }
  msg_clear();
  sound_init();
}

static uint32_t _fnv1a_update(uint32_t h, const uint8_t *buf, uint16_t len) {
  for (uint16_t i = 0; i < len; ++i) {
    h ^= buf[i];
    h *= 16777619UL;
  }
  return h;
}

static uint32_t _compute_frame_hash(void) {
  // Note: mem_switch_buffer() has already toggled mem_screen_ram to the next
  // buffer, so the buffer that was just rendered is the *other* one.
  const uint8_t *scr =
      mem_using_alt_buffer ? kScreenRamMain : kScreenRamAlt;
  uint32_t h = 2166136261UL;
  for (uint8_t y = 0; y < kViewportHeight; ++y) {
    const uint8_t *row_scr = scr + y * kScreenWidth + kViewportStartX;
    const uint8_t *row_col = mem_color_buffer + y * kViewportWidth;
    for (uint8_t x = 0; x < kViewportWidth; ++x) {
      uint8_t ch = row_scr[x];
      h ^= ch;
      h *= 16777619UL;
      h ^= row_col[x];
      h *= 16777619UL;
      const uint8_t *glyph = kCharRam + ((uint16_t)ch << 3);
      for (uint8_t b = 0; b < 8; ++b) {
        h ^= glyph[b];
        h *= 16777619UL;
      }
    }
  }
  sprites_show_terrain_sprites();
  h = _fnv1a_update(h, kScreenRamMain + 0x3f8, 8);
  h = _fnv1a_update(h, (const uint8_t *)0xd000, 16);
  sprites_show_panel_top_sprites();
  sprites_show_panel_bottom_sprites();
  h = _fnv1a_update(h, kScreenRamMain + 0x3f8, 8);
  h = _fnv1a_update(h, (const uint8_t *)0xd000, 16);
  return h;
}

static void _bench_sync_top_border(void) {
  // Raster lines 1..47 are in the top border, completely clear of all four
  // raster IRQs (which live at 153, 161, 185, and 250..256) and ahead of line
  // 48 where DEN latches badlines for the frame.
  while (vic.raster != 20)
    ;
  while (vic.raster == 20)
    ;
}

void bench_poll(void) {
  keyb_poll();

  if (_bench_magic == 0x5A) {
    _bench_magic = 0;
    _bench_pose = 0;
    _bench_sub = 0;
    memset(kCharRam, 0, kGfxCharStart * 8);
  } else {
    if (_bench_sub == 3) {
      // Just finished Subframe 3 (Pure CPU measurement, with SEI active).
      // Hash the rendered buffers while interrupts are disabled so raster IRQs
      // cannot mutate sprite pointers mid-hash.
      g_bench_results[_bench_pose].frame_hash = _compute_frame_hash();
      // Restore normal display and interrupts before advancing to next pose.
      vic.ctrl1 = 0x1b;
      __asm {
        cli
      }
    }

    if (++_bench_sub >= 4) {
      _bench_sub = 0;
      if (++_bench_pose >= 4) {
        for (;;) {
        }
      }
    }
  }

  _setup_pose(_bench_pose);
  _bm_sub_accum[0] = 0;
  _bm_sub_accum[1] = 0;
  _bm_sub_accum[2] = 0;

  if (_bench_sub == 2) {
    // Synchronize to the exact start of raster line 21 for Real VIC measurement.
    _bench_sync_top_border();
  } else if (_bench_sub == 3) {
    // Synchronize to raster line 21, then blank display and disable interrupts
    // before raster line 48 so zero badlines, sprite DMA, or IRQs occur.
    _bench_sync_top_border();
    __asm {
      sei
    }
    vic.ctrl1 = 0x0b;
    vic.spr_enable = 0;
  }
}
"""


def find_vice():
    if shutil.which("x64sc"):
        return shutil.which("x64sc")
    for pattern in ("/Applications/vice-*/bin/x64sc",):
        import glob
        matches = sorted(glob.glob(pattern))
        if matches and os.access(matches[0], os.X_OK):
            os.environ["PATH"] = os.path.dirname(matches[0]) + os.pathsep + os.environ.get("PATH", "")
            return matches[0]
    return None


def run_cmd(cmd, cwd=REPO, quiet=False):
    res = subprocess.run(
        cmd,
        cwd=cwd,
        stdout=subprocess.PIPE if quiet else None,
        stderr=subprocess.STDOUT if quiet else None,
        text=True,
    )
    if res.returncode != 0:
        if quiet and res.stdout:
            sys.stderr.write(res.stdout)
        raise RuntimeError(f"Command failed (exit {res.returncode}): {' '.join(cmd)}")
    return res.stdout if quiet else ""


def get_zp_headroom(asm_path):
    out = subprocess.check_output(
        [sys.executable, os.path.join(HERE, "check_zeropage.py"), asm_path],
        cwd=REPO,
        text=True,
    )
    m = re.search(r"\((\d+) bytes headroom\)", out)
    return int(m.group(1)) if m else 0


def collect_ram_metrics():
    metrics = {}
    for prog in ("ppilot", "ppilota", "fpilot"):
        map_path = os.path.join(REPO, "c64o", f"{prog}.map")
        asm_path = os.path.join(REPO, "c64o", f"{prog}.asm")
        prg_path = os.path.join(REPO, "c64o", f"{prog}.prg")
        categories, walk = analyze_ram.parse_map(map_path)

        cols = ("Code", "Data", "BSS", "ZP", "VRAM")
        seg_totals = {c: sum(cat[c] for cat in categories.values()) for c in cols}
        FA = walk["FREE_ALLOC"]
        FS = walk["FREE_STACK"]
        FO = walk["FREE_ORPHAN"]
        FB = walk["FREE_BOOT"]
        U = walk["USED"]
        biggest = max((e - s, s) for s, e, k, _o in walk["runs"] if k == FA)

        metrics[prog] = {
            "prg_bytes": os.path.getsize(prg_path),
            "used_bytes": walk["totals"][U],
            "free_allocatable": walk["totals"][FA],
            "largest_free_run": biggest[0],
            "largest_free_addr": f"${biggest[1]:04X}",
            "stack_headroom": walk["totals"][FS],
            "boot_headroom": walk["totals"][FB],
            "orphan_free": walk["totals"][FO],
            "zp_runtime_headroom": get_zp_headroom(asm_path),
            "code_bytes": seg_totals["Code"],
            "data_bytes": seg_totals["Data"],
            "bss_bytes": seg_totals["BSS"],
            "zp_bytes": seg_totals["ZP"],
        }
    return metrics


def run_cycle_benchmark(flight_aoa=False):
    if not shutil.which("oscar64"):
        raise RuntimeError("oscar64 not found on PATH")
    if not find_vice():
        raise RuntimeError("x64sc (VICE) not found on PATH")
    if not os.path.isdir(OSCAR64_INCLUDE):
        raise RuntimeError(f"oscar64 include directory not found at {OSCAR64_INCLUDE}")

    src_dir = os.path.join(BENCH_BUILD_DIR, "src")
    inc_link = os.path.join(BENCH_BUILD_DIR, "include")
    if os.path.exists(BENCH_BUILD_DIR):
        shutil.rmtree(BENCH_BUILD_DIR)
    os.makedirs(src_dir, exist_ok=True)
    os.symlink(OSCAR64_INCLUDE, inc_link)

    c64o_dir = os.path.join(REPO, "c64o")
    for fname in os.listdir(c64o_dir):
        if fname.endswith((".cc", ".h", ".bin", ".koa")):
            shutil.copy2(os.path.join(c64o_dir, fname), os.path.join(src_dir, fname))

    # Write custom benchmark.cc and bench_driver.h
    with open(os.path.join(src_dir, "bench_driver.h"), "w") as f:
        f.write("#ifndef BENCH_DRIVER_H\n#define BENCH_DRIVER_H\nvoid bench_poll(void);\n#endif\n")
    with open(os.path.join(src_dir, "benchmark.cc"), "w") as f:
        f.write(BENCH_C_CODE)

    # Patch ppilot.cc to enter sim_run(0) directly without menu_run()
    ppilot_cc = os.path.join(src_dir, "ppilot.cc")
    with open(ppilot_cc, "r") as f:
        code = f.read()
    patched_ppilot = code.replace(
        "uint8_t selected_mission = menu_run();\n    sim_run(selected_mission);",
        "sim_run(0);",
    )
    if patched_ppilot == code:
        raise RuntimeError("Failed to patch menu_run() in ppilot.cc")
    with open(ppilot_cc, "w") as f:
        f.write(patched_ppilot)

    # Patch sim.cc to call bench_poll() at top of main simulation loop
    sim_cc = os.path.join(src_dir, "sim.cc")
    with open(sim_cc, "r") as f:
        sim_code = f.read()
    patched_sim = sim_code.replace(
        '#include "screen.h"',
        '#include "screen.h"\n#include "bench_driver.h"',
        1,
    ).replace(
        "  while (1) {\n    keyb_poll();",
        "  while (1) {\n    bench_poll();",
        1,
    )
    if "bench_poll();" not in patched_sim:
        raise RuntimeError("Failed to patch keyb_poll() in sim.cc")
    with open(sim_cc, "w") as f:
        f.write(patched_sim)

    cmd = [
        "oscar64",
        "-ii=../include",
        "-D__OSCAR64__",
        "-g",
        "-xz",
        "-O2",
        "-Op",
        "-Oa",
        "-Oi",
        "-Oz",
        "-Oo",
        "-D__ENABLE_SOUND__",
        "-D__ENABLE_DEBUG__",
        "-D__ENABLE_CLOUDS__",
        "-D__MAX_RAM__",
    ]
    if flight_aoa:
        cmd.append("-D__FLIGHT_AOA__")
    cmd.extend(["-o=ppilot_bench.prg", "ppilot.cc"])
    run_cmd(cmd, cwd=src_dir, quiet=True)

    prg_path = os.path.join(src_dir, "ppilot_bench.prg")
    dump_bin = os.path.join(src_dir, "bench_results.bin")
    nbytes = 4 * len(POSE_FIELDS) * len(POSES)  # 240 bytes
    vice_cmd = [
        os.path.join(HERE, "vice_dump.sh"),
        prg_path,
        "@spin:bench_poll",
        "g_bench_results",
        str(nbytes),
        dump_bin,
        "40000000",
    ]
    run_cmd(vice_cmd, cwd=REPO, quiet=True)

    raw = open(dump_bin, "rb").read()[2:]  # strip 2-byte PRG load header
    if len(raw) != nbytes:
        raise RuntimeError(f"Expected {nbytes} bytes from vice_dump, got {len(raw)}")

    unpacked = struct.unpack("<" + "I" * (len(POSE_FIELDS) * len(POSES)), raw)
    poses_out = {}
    for idx, pose_name in enumerate(POSES):
        chunk = unpacked[idx * len(POSE_FIELDS) : (idx + 1) * len(POSE_FIELDS)]
        d = dict(zip(POSE_FIELDS, chunk))
        d["frame_hash_hex"] = f"0x{d['frame_hash']:08X}"
        poses_out[pose_name] = d

    totals = {}
    for field in POSE_FIELDS:
        if field != "frame_hash":
            totals[field] = sum(poses_out[p][field] for p in POSES)

    return {"poses": poses_out, "sum_4_poses": totals}


def print_report(result):
    ram = result["ram"]
    cyc = result["cycles"]
    print("=" * 94)
    print("1. RAM & HEADROOM SUMMARY")
    print("=" * 94)
    print(
        f"{'Binary':<10} | {'Free Alloc':>10} | {'Largest Run':>13} | {'Code':>7} | "
        f"{'Data':>7} | {'BSS':>6} | {'ZP':>4} | {'ZP Hdr':>6} | {'Boot Hdr':>8} | {'Stk Hdr':>7}"
    )
    print("-" * 94)
    for prog in ("ppilot", "ppilota", "fpilot"):
        r = ram[prog]
        run_str = f"{r['largest_free_run']}@{r['largest_free_addr']}"
        print(
            f"{prog:<10} | {r['free_allocatable']:>8} B | {run_str:>13} | "
            f"{r['code_bytes']:>6}B | {r['data_bytes']:>6}B | {r['bss_bytes']:>5}B | "
            f"{r['zp_bytes']:>3}B | {r['zp_runtime_headroom']:>5}B | "
            f"{r['boot_headroom']:>7}B | {r['stack_headroom']:>6}B"
        )

    print("\n" + "=" * 94)
    print("2. CYCLE BENCHMARK (6510 Pure CPU per Stage + Real VIC-II Work + Frame Hash)")
    print("=" * 94)
    hdr = (
        f"{'Pose':<10} | {'CPU Tot':>8} | {'RealWrk':>8} | {'MDL':>5} | {'UPD':>6} | "
        f"{'SNP':>5} | {'BGR':>5} | {'CHR':>5} | {'DRW':>5} | {'GRD':>6} | "
        f"{'PNL':>5} | {'COL':>5} | {'Hash':>10}"
    )
    print(hdr)
    print("-" * len(hdr))
    for p in POSES:
        c = cyc["poses"][p]
        print(
            f"{p:<10} | {c['cpu_total']:>8,} | {c['real_work']:>8,} | {c['mdl']:>5} | "
            f"{c['upd']:>6} | {c['snp']:>5} | {c['bgr']:>5} | {c['chr']:>5} | "
            f"{c['drw']:>5} | {c['grd']:>6} | {c['pnl']:>5} | {c['col']:>5} | "
            f"{c['frame_hash_hex']:>10}"
        )
    s = cyc["sum_4_poses"]
    print("-" * len(hdr))
    print(
        f"{'SUM (4)':<10} | {s['cpu_total']:>8,} | {s['real_work']:>8,} | {s['mdl']:>5} | "
        f"{s['upd']:>6} | {s['snp']:>5} | {s['bgr']:>5} | {s['chr']:>5} | "
        f"{s['drw']:>5} | {s['grd']:>6} | {s['pnl']:>5} | {s['col']:>5} | {'-':>10}"
    )
    print(
        f"\nSub-stage 4-pose sums:  PLY (polygons) = {s['ply']:,}   "
        f"CLD (clouds) = {s['cld']:,}   SPR (sprites) = {s['spr']:,}"
    )
    for p in POSES:
        c = cyc["poses"][p]
        print(
            f"  {p:<10}: PLY={c['ply']:>6,}   CLD={c['cld']:>6,}   SPR={c['spr']:>5,}"
        )
    print("=" * 94)


def compare_with_baseline(current, baseline_data, max_cycle_regression_pct=2.0, allow_visual_change=False):
    # baseline_data can either be a single snapshot or {"anchor": ..., "latest": ...}
    if "anchor" in baseline_data and "latest" in baseline_data:
        anchor = baseline_data["anchor"]
        latest = baseline_data["latest"]
    else:
        anchor = baseline_data
        latest = baseline_data

    print("\n" + "=" * 94)
    print("3. EVALUATION AGAINST BASELINE (Cycles & Free RAM)")
    print("=" * 94)

    cur_ram = current["ram"]["ppilot"]["free_allocatable"]
    lat_ram = latest["ram"]["ppilot"]["free_allocatable"]
    anc_ram = anchor["ram"]["ppilot"]["free_allocatable"]
    d_ram_step = cur_ram - lat_ram
    d_ram_anc = cur_ram - anc_ram

    cur_fp_ram = current["ram"]["fpilot"]["free_allocatable"]
    lat_fp_ram = latest["ram"]["fpilot"]["free_allocatable"]
    anc_fp_ram = anchor["ram"]["fpilot"]["free_allocatable"]
    d_fp_step = cur_fp_ram - lat_fp_ram
    d_fp_anc = cur_fp_ram - anc_fp_ram

    cur_code = current["ram"]["ppilot"]["code_bytes"]
    lat_code = latest["ram"]["ppilot"]["code_bytes"]
    d_code_step = cur_code - lat_code

    cur_cpu = current["cycles"]["sum_4_poses"]["cpu_total"]
    lat_cpu = latest["cycles"]["sum_4_poses"]["cpu_total"]
    anc_cpu = anchor["cycles"]["sum_4_poses"]["cpu_total"]
    d_cpu_step = cur_cpu - lat_cpu
    d_cpu_anc = cur_cpu - anc_cpu
    pct_cpu_step = (d_cpu_step / lat_cpu) * 100.0 if lat_cpu else 0.0
    pct_cpu_anc = (d_cpu_anc / anc_cpu) * 100.0 if anc_cpu else 0.0

    cur_real = current["cycles"]["sum_4_poses"]["real_work"]
    lat_real = latest["cycles"]["sum_4_poses"]["real_work"]
    anc_real = anchor["cycles"]["sum_4_poses"]["real_work"]
    d_real_step = cur_real - lat_real
    d_real_anc = cur_real - anc_real
    pct_real_anc = (d_real_anc / anc_real) * 100.0 if anc_real else 0.0

    max_allowed_cpu = int(anc_cpu * (1.0 + max_cycle_regression_pct / 100.0))

    print(
        f"  4-Pose CPU Total Cycles     : {lat_cpu:>8,} -> {cur_cpu:>8,}  "
        f"(step: {d_cpu_step:+d} [{pct_cpu_step:+.2f}%] | vs initial: {d_cpu_anc:+d} [{pct_cpu_anc:+.2f}%], "
        f"limit <= {max_allowed_cpu:,} [+{max_cycle_regression_pct:.1f}%])"
    )
    print(
        f"  4-Pose Real VIC Work Cycles : {lat_real:>8,} -> {cur_real:>8,}  "
        f"(step: {d_real_step:+d} | vs initial: {d_real_anc:+d} [{pct_real_anc:+.2f}%])"
    )
    print(
        f"  ppilot Free Allocatable RAM : {lat_ram:>8,} B -> {cur_ram:>8,} B  "
        f"(step: {d_ram_step:+d} B, code {d_code_step:+d} B | vs initial: {d_ram_anc:+d} B)"
    )
    print(
        f"  fpilot Free Allocatable RAM : {lat_fp_ram:>8,} B -> {cur_fp_ram:>8,} B  "
        f"(step: {d_fp_step:+d} B | vs initial: {d_fp_anc:+d} B)"
    )

    stage_deltas = []
    for st in ("mdl", "upd", "snp", "bgr", "chr", "drw", "grd", "pnl", "col", "cld", "spr", "ply"):
        ds = current["cycles"]["sum_4_poses"][st] - latest["cycles"]["sum_4_poses"][st]
        if ds != 0:
            stage_deltas.append(f"{st.upper()}:{ds:+d}")
    if stage_deltas:
        print(f"  Stage Deltas (vs latest)    : {', '.join(stage_deltas)}")

    hash_mismatches = []
    for p in POSES:
        ch = current["cycles"]["poses"][p]["frame_hash_hex"]
        bh = anchor["cycles"]["poses"][p]["frame_hash_hex"]
        dp_step = current["cycles"]["poses"][p]["cpu_total"] - latest["cycles"]["poses"][p]["cpu_total"]
        dp_anc = current["cycles"]["poses"][p]["cpu_total"] - anchor["cycles"]["poses"][p]["cpu_total"]
        pct_p_anc = (dp_anc / anchor["cycles"]["poses"][p]["cpu_total"]) * 100.0
        status = "OK" if ch == bh else f"MISMATCH ({bh} != {ch})"
        if ch != bh:
            hash_mismatches.append(p)
        print(
            f"    Pose {p:<10}: step {dp_step:+6d} cyc | vs initial {dp_anc:+6d} cyc ({pct_p_anc:+.2f}%) | "
            f"Visual Hash: {status}"
        )

    if hash_mismatches and not allow_visual_change:
        print(f"\nVERDICT: REJECT (Visual frame hash mismatch on: {', '.join(hash_mismatches)})")
        return 1

    # Hard guard: total CPU cycles and each individual pose must stay within +max_cycle_regression_pct% of initial anchor
    if cur_cpu > max_allowed_cpu:
        print(
            f"\nVERDICT: REJECT (Total CPU cycles {cur_cpu:,} exceeds +{max_cycle_regression_pct:.1f}% "
            f"ceiling {max_allowed_cpu:,} from initial baseline {anc_cpu:,})"
        )
        return 1

    for p in POSES:
        p_cur = current["cycles"]["poses"][p]["cpu_total"]
        p_anc = anchor["cycles"]["poses"][p]["cpu_total"]
        p_lim = int(p_anc * (1.0 + max_cycle_regression_pct / 100.0))
        if p_cur > p_lim:
            print(
                f"\nVERDICT: REJECT (Pose '{p}' CPU cycles {p_cur:,} exceeds +{max_cycle_regression_pct:.1f}% "
                f"ceiling {p_lim:,} from initial baseline {p_anc:,})"
            )
            return 1

    # Check step improvement:
    # 1. Strict Pareto win (improves cycles and/or RAM without hurting the other):
    if d_ram_step >= 0 and d_fp_step >= 0 and d_cpu_step <= 0 and (d_ram_step > 0 or d_fp_step > 0 or d_cpu_step < 0):
        win_types = []
        if d_cpu_step < 0:
            win_types.append(f"cycles {d_cpu_step:+d}")
        if d_ram_step > 0:
            win_types.append(f"ppilot RAM {d_ram_step:+d} B")
        if d_fp_step > 0:
            win_types.append(f"fpilot RAM {d_fp_step:+d} B")
        print(f"\nVERDICT: PARETO_WIN ({', '.join(win_types)} with zero trade-off)")
        return 0

    # 2. Cycle improvement that spends RAM (allowed ONLY if net free RAM stays >= initial anchor)
    if d_cpu_step < 0 and cur_ram >= anc_ram and cur_fp_ram >= anc_fp_ram:
        print(
            f"\nVERDICT: CYCLE_WIN (Cycles improved by {d_cpu_step:+d} [{pct_cpu_step:+.2f}%], "
            f"step RAM {d_ram_step:+d} B, net RAM vs initial {d_ram_anc:+d} B)"
        )
        return 0

    # 3. RAM improvement that spends cycles (allowed ONLY within the +2% ceiling from initial anchor)
    if (d_ram_step > 0 or d_fp_step > 0) and d_ram_step >= 0 and d_fp_step >= 0 and cur_cpu <= max_allowed_cpu:
        print(
            f"\nVERDICT: RAM_WIN_WITHIN_CYCLE_BUDGET (RAM {d_ram_step:+d} B, step cycles {d_cpu_step:+d}, "
            f"net cycles vs initial {pct_cpu_anc:+.2f}% <= +{max_cycle_regression_pct:.1f}% limit)"
        )
        return 0

    if d_ram_step == 0 and d_fp_step == 0 and d_cpu_step == 0:
        print("\nVERDICT: NEUTRAL (No change in cycles or free allocatable RAM)")
        return 2

    reasons = []
    if d_cpu_step > 0:
        reasons.append(f"step cycles regressed by +{d_cpu_step}")
    if d_ram_step < 0:
        reasons.append(f"step ppilot free RAM regressed by {d_ram_step} B (net vs initial: {d_ram_anc:+d} B)")
    if d_fp_step < 0:
        reasons.append(f"step fpilot free RAM regressed by {d_fp_step} B (net vs initial: {d_fp_anc:+d} B)")
    print(f"\nVERDICT: REJECT ({'; '.join(reasons)})")
    return 1


def main():
    parser = argparse.ArgumentParser(description="Plane Pilot Cycle + RAM Evaluation Harness")
    parser.add_argument("--init-baseline", nargs="?", const=DEFAULT_BASELINE, metavar="PATH",
                        help="Initialize both anchor and latest baseline JSON to current state")
    parser.add_argument("--update-latest", nargs="?", const=DEFAULT_BASELINE, metavar="PATH",
                        help="Update latest accepted state in baseline JSON while preserving initial anchor")
    parser.add_argument("--compare", nargs="?", const=DEFAULT_BASELINE, metavar="PATH",
                        help="Compare current metrics against baseline JSON and return verdict exit code")
    parser.add_argument("--max-cycle-regression", type=float, default=2.0,
                        help="Max allowed cycle regression %% from initial anchor baseline (default: 2.0%%)")
    parser.add_argument("--quick", action="store_true",
                        help="Skip the Python generator test suite (still runs all C64 builds, checks & target tests)")
    parser.add_argument("--no-build", action="store_true",
                        help="Skip `make -C c64o all` if binaries are already up to date")
    parser.add_argument("--allow-visual-change", action="store_true",
                        help="Do not fail --compare on frame hash changes")
    parser.add_argument("--json", action="store_true",
                        help="Print JSON output only")
    args = parser.parse_args()

    t0 = time.time()
    if not args.no_build:
        run_cmd(["make", "-C", "c64o", "all"], cwd=REPO, quiet=args.json)
        run_cmd(["make", "-C", "c64o/test", "test-both"], cwd=REPO, quiet=args.json)
        if not args.quick:
            run_cmd(["make", "test"], cwd=REPO, quiet=args.json)

    ram_metrics = collect_ram_metrics()
    cycle_metrics = run_cycle_benchmark(flight_aoa=False)

    result = {
        "elapsed_sec": round(time.time() - t0, 2),
        "ram": ram_metrics,
        "cycles": cycle_metrics,
    }

    if args.init_baseline:
        payload = {"anchor": result, "latest": result}
        with open(args.init_baseline, "w") as f:
            json.dump(payload, f, indent=2)
        if not args.json:
            print(f"Initialized anchor + latest baseline at {args.init_baseline}")

    if args.update_latest:
        if os.path.exists(args.update_latest):
            with open(args.update_latest, "r") as f:
                existing = json.load(f)
            anchor = existing.get("anchor", existing)
        else:
            anchor = result
        payload = {"anchor": anchor, "latest": result}
        with open(args.update_latest, "w") as f:
            json.dump(payload, f, indent=2)
        if not args.json:
            print(f"Updated latest accepted baseline at {args.update_latest} (anchor preserved)")

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print_report(result)
        print(f"Completed in {result['elapsed_sec']:.2f}s")

    if args.compare:
        if not os.path.exists(args.compare):
            sys.stderr.write(f"Baseline file not found: {args.compare}\n")
            sys.exit(1)
        with open(args.compare, "r") as f:
            baseline_data = json.load(f)
        rc = compare_with_baseline(
            result,
            baseline_data,
            max_cycle_regression_pct=args.max_cycle_regression,
            allow_visual_change=args.allow_visual_change,
        )
        sys.exit(rc)


if __name__ == "__main__":
    main()
