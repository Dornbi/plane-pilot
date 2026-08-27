// Host test for the sprite stack and the three raster bands that read it.
//
// docs/clouds.md §1 is what this covers: a distance-sorted allocator for the
// eight hardware sprites, plus the restructure of the band handlers that lets
// the terrain band own all eight of them. Both are worth a test for the same
// reason sound_test.cc exists - the failure modes are quiet. A sprite handed
// the wrong index is a depth-sorting bug you would have to catch by eye at
// 10 Hz; a colour the panel band forgets to restore is one white needle among
// seven; an index used twice is an object that silently never appears.
//
// The assertions are on the VIC registers rather than on the frame struct, so
// what is being checked is what the hardware would actually see. vic.h and
// mem.h point their register and screen-RAM addresses at the arrays below when
// __OSCAR64__ is not defined, by the same device sid.h already uses for the
// SID, so calling a raster handler here is an array store instead of a segfault
// at $D000.
//
// What this test cannot see is the thing that actually broke first: whether the
// handlers touch oscar64's runtime zero page. That is a property of the
// generated 6502, not of the logic, and tools/check_irq_zp.py is what guards
// it. See docs/clouds.md §1.4.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../color.h"
#include "../mem.h"
#include "../msg.h"
#include "../vic.h"
#include "../view.h"

// --- The hardware, and the state sprites.cc reaches for --------------------

// $D000..$D02E. One byte past spr_color[7] is enough for everything the sprite
// code touches.
static uint8_t vic_regs[0x30];
struct VIC *vic_host = (struct VIC *)vic_regs;

// The two screen buffers, for the sprite pointers at offset 1016.
static uint8_t screen_main[1024];
static uint8_t screen_alt[1024];
uint8_t *kScreenRamMain = screen_main;
uint8_t *kScreenRamAlt = screen_alt;
uint8_t *kColorRam = nullptr;

// Which panel layout the band handlers build. The simulation drives this from
// the 1/2/3 keys; here it is set directly.
view_state_t view_state = VIEW_CENTER;

// msg.cc is not linked. The only thing sprites.cc asks it is "is a message up,
// and how wide", so defining that here lets the test place the span exactly on
// and exactly off a sprite instead of composing a message that happens to land
// there.
uint16_t msg_span_x0 = 0;
uint16_t msg_span_x1 = 0;
static bool msg_is_active = false;
bool msg_active(void) { return msg_is_active; }

// Included rather than linked: the band handlers are `inline`, which on the
// C64 means oscar64 folds them into the raster IRQs, and on the host means
// there is no out-of-line copy to link against.
#include "../sprites.cc"

// --- Reading the registers back --------------------------------------------

static const int8_t kSunPivotX = 12; // kSpriteDefSun's, spelled out so the
static const int8_t kSunPivotY = 10; // expected values below are readable
static const uint8_t kSun = 95;      // its bitmap index
static const uint8_t NB = kSpriteNoBitmap;

static uint8_t spr_x(uint8_t i) { return vic_regs[i * 2]; }
static uint8_t spr_y(uint8_t i) { return vic_regs[i * 2 + 1]; }
static uint8_t spr_msbx(void) { return vic_regs[0x10]; }
static uint8_t spr_enable(void) { return vic_regs[0x15]; }
static uint8_t spr_expand(void) { return vic_regs[0x1d]; }
static uint8_t spr_color(uint8_t i) { return vic_regs[0x27 + i]; }
static uint8_t spr_ptr(uint8_t i) { return screen_main[1016 + i]; }

// Program the terrain band from the frame the last commit published.
static void show_terrain(void) {
  memset(vic_regs, 0xAA, sizeof(vic_regs));
  memset(screen_main + 1016, 0xAA, 8);
  memset(screen_alt + 1016, 0xAA, 8);
  sprites_show_terrain_sprites();
  // Both screen buffers are double buffered under the handler, so every sprite
  // pointer has to be written to both or the pointer goes stale on alternate
  // frames. Checking it once here rather than in every case below.
  assert(memcmp(screen_main + 1016, screen_alt + 1016, 8) == 0);
}

static bool enabled(uint8_t i) { return (spr_enable() >> i) & 1; }

// --- Cases -----------------------------------------------------------------

static void test_empty_frame(void) {
  printf("  empty frame\n");
  sprites_stack_reset();
  sprites_stack_commit();
  show_terrain();
  assert(spr_enable() == 0x00);
  assert(spr_expand() == 0x00);
  assert(spr_msbx() == 0x00);
  // Disabled rather than parked, but parked as well, so a stray enable cannot
  // put a sprite somewhere visible.
  for (uint8_t i = 0; i < 8; ++i) {
    assert(spr_x(i) == 0);
    assert(spr_y(i) == 0);
  }
}

static void test_single_object(void) {
  printf("  one object lands on index 0\n");
  sprites_stack_reset();
  assert(sprites_stack_add(0x7FFF, 160, 56, kSunPivotX, kSunPivotY, kSun, NB,
                           kColorSun, 0));
  sprites_stack_commit();
  show_terrain();

  // Centre (160, 56) in viewport screen pixels, less the pivot, plus the VIC's
  // own origin: x = 160 - 12 + 24, y = 56 - 10 + 50.
  assert(spr_x(0) == 172);
  assert(spr_y(0) == 96);
  assert(spr_ptr(0) == kSun);
  assert(spr_color(0) == kColorSun);
  assert(spr_enable() == 0x01);
  assert(spr_expand() == 0x00);
  assert(spr_msbx() == 0x00);
}

static void test_msbx(void) {
  printf("  the ninth X bit\n");
  sprites_stack_reset();
  // Centre at 300 puts the left edge past 255 once the VIC origin is added.
  sprites_stack_add(100, 300, 56, kSunPivotX, kSunPivotY, kSun, NB, kColorSun,
                    0);
  sprites_stack_commit();
  show_terrain();
  assert(spr_x(0) == (uint8_t)(300 - 12 + 24)); // 312 & 0xff
  assert(spr_msbx() == 0x01);
}

static void test_nearest_first(void) {
  printf("  nearest object gets the lowest index\n");
  sprites_stack_reset();
  // Offered out of order, and with the sun in the middle of the sequence, so
  // passing this cannot be an artefact of insertion order.
  sprites_stack_add(3000, 100, 50, kSunPivotX, kSunPivotY, 30, NB, 1, 0);
  sprites_stack_add(1000, 110, 50, kSunPivotX, kSunPivotY, 10, NB, 1, 0);
  sprites_stack_add(0x7FFF, 120, 50, kSunPivotX, kSunPivotY, kSun, NB, 1, 0);
  sprites_stack_add(2000, 130, 50, kSunPivotX, kSunPivotY, 20, NB, 1, 0);
  sprites_stack_commit();
  show_terrain();
  assert(spr_ptr(0) == 10);
  assert(spr_ptr(1) == 20);
  assert(spr_ptr(2) == 30);
  // INT16_MAX sorts behind everything, which is the whole of what makes the sun
  // an ordinary entry.
  assert(spr_ptr(3) == kSun);
  assert(spr_enable() == 0x0F);
}

static void test_two_sprite_stack(void) {
  printf("  a 1 x 2 stack takes consecutive indices\n");
  sprites_stack_reset();
  // A near cloud: two blocks, X-expanded, pivot (12, 20) as spritedef.cc
  // stores it for the stacked bitmaps.
  sprites_stack_add(500, 160, 56, 12, 20, 85, 86, kColorWhite,
                    kSpriteFlagExpandX);
  sprites_stack_add(0x7FFF, 60, 56, kSunPivotX, kSunPivotY, kSun, NB, kColorSun,
                    0);
  sprites_stack_commit();
  show_terrain();

  assert(spr_ptr(0) == 85);
  assert(spr_ptr(1) == 86);
  assert(spr_x(0) == spr_x(1));
  // Exactly one sprite height apart, or the two halves show a seam.
  assert((uint8_t)(spr_y(1) - spr_y(0)) == kSpriteHeightPixels);
  assert(spr_color(0) == spr_color(1));
  // Expansion is per sprite, so both halves need the bit; the sun must not.
  assert(spr_expand() == 0x03);
  assert(spr_ptr(2) == kSun);
  assert(spr_enable() == 0x07);
  // pivot_x doubles under expansion and pivot_y does not - an expanded sprite
  // pixel is two screen pixels wide and one raster line tall.
  assert(spr_x(0) == (uint8_t)(160 - 2 * 12 + 24));
  assert(spr_y(0) == (uint8_t)(56 - 20 + 50));
}

static void test_overflow(void) {
  printf("  overflow drops the farthest, never a nearer one\n");
  sprites_stack_reset();
  for (int i = 0; i < 9; ++i) {
    // Descending depth, so the last one offered is the nearest.
    sprites_stack_add((int16_t)(900 - 100 * i), 160, 56, kSunPivotX, kSunPivotY,
                      (uint8_t)(i + 1), NB, 1, 0);
  }
  sprites_stack_commit();
  show_terrain();
  // Seven, not eight: index 7 is the vertical-speed needle's and the terrain
  // band never takes it (sprites.h, docs/clouds.md §1.9).
  assert(spr_enable() == 0x7F);
  assert(!enabled(kSpriteIdxVSpeed));
  for (int i = 0; i < kSpriteTerrainSlots; ++i) {
    assert(spr_ptr(i) == (uint8_t)(9 - i));
  }
  // Bitmaps 1 and 2 were the farthest of the nine and are the ones that go.
  for (int i = 0; i < kSpriteTerrainSlots; ++i) {
    assert(spr_ptr(i) != 1 && spr_ptr(i) != 2);
  }
  // A full stack refuses anything farther than everything in it, rather than
  // displacing something nearer.
  assert(!sprites_stack_add(30000, 160, 56, kSunPivotX, kSunPivotY, 77, NB, 1,
                            0));

  printf("  a two-slot entry that does not fit is not half placed\n");
  sprites_stack_reset();
  for (int i = 0; i < 6; ++i) {
    sprites_stack_add((int16_t)(100 + i), 160, 56, kSunPivotX, kSunPivotY,
                      (uint8_t)(i + 1), NB, 1, 0);
  }
  // Nearer than none of them, and wants two slots with one left.
  sprites_stack_add(200, 160, 56, 12, 20, 85, 86, 1, 0);
  sprites_stack_commit();
  show_terrain();
  assert(spr_enable() == 0x3F);
  assert(!enabled(6));
  assert(spr_x(6) == 0 && spr_y(6) == 0);
}

static void test_no_index_used_twice(void) {
  printf("  no index is used twice, unused ones stay disabled\n");
  sprites_stack_reset();
  sprites_stack_add(300, 100, 40, 12, 20, 85, 86, 1, kSpriteFlagExpandX);
  sprites_stack_add(100, 200, 40, 12, 20, 87, 88, 1, kSpriteFlagExpandX);
  sprites_stack_add(0x7FFF, 60, 20, kSunPivotX, kSunPivotY, kSun, NB, kColorSun,
                    0);
  sprites_stack_commit();
  show_terrain();

  assert(spr_ptr(0) == 87 && spr_ptr(1) == 88); // nearer pair first
  assert(spr_ptr(2) == 85 && spr_ptr(3) == 86);
  assert(spr_ptr(4) == kSun);
  assert(spr_enable() == 0x1F);
  assert(spr_expand() == 0x0F); // the four cloud halves, not the sun

  bool seen[256];
  memset(seen, 0, sizeof(seen));
  for (uint8_t i = 0; i < 8; ++i) {
    if (!enabled(i)) {
      assert(spr_x(i) == 0);
      assert(((spr_expand() >> i) & 1) == 0);
      continue;
    }
    assert(!seen[spr_ptr(i)]);
    seen[spr_ptr(i)] = true;
  }
}

static void test_culls(void) {
  printf("  culls\n");
  sprites_stack_reset();
  // Fully off each edge. The sprite is 24 x 21 around a pivot of (12, 10), so
  // these are the first centre positions with nothing left on screen.
  assert(!sprites_stack_add(100, -13, 56, kSunPivotX, kSunPivotY, kSun, NB, 7, 0));
  assert(!sprites_stack_add(100, 333, 56, kSunPivotX, kSunPivotY, kSun, NB, 7, 0));
  assert(!sprites_stack_add(100, 160, -11, kSunPivotX, kSunPivotY, kSun, NB, 7, 0));
  assert(!sprites_stack_add(100, 160, 123, kSunPivotX, kSunPivotY, kSun, NB, 7, 0));
  // One pixel inside each of those survives. The sprite spans [c - 12, c + 12)
  // horizontally and [c - 10, c + 11) vertically, so these are the centres at
  // which exactly one pixel column or row is still inside the viewport.
  assert(sprites_stack_add(100, -11, 56, kSunPivotX, kSunPivotY, kSun, NB, 7, 0));
  assert(sprites_stack_add(100, 331, 56, kSunPivotX, kSunPivotY, kSun, NB, 7, 0));
  assert(sprites_stack_add(100, 160, -10, kSunPivotX, kSunPivotY, kSun, NB, 7, 0));

  printf("  a 1 x 2 stack whose lower half could not start is not admitted\n");
  // The lower half is a separate hardware sprite 21 lines further down, and
  // the VIC only starts a sprite on the line its Y matches. Below the cut it
  // would never begin, and the cloud would draw as a flat-topped half.
  sprites_stack_reset();
  {
    // Centre chosen so the *pair's* lower sprite starts exactly on the cut.
    const int16_t centre =
        (int16_t)kSpriteVisibleEndYPixels - kSpriteHeightPixels + 20;
    assert(!sprites_stack_add(100, 160, centre, 12, 20, 85, 86, 7, 0));
    assert(sprites_stack_add(100, 160, centre - 1, 12, 20, 85, 86, 7, 0));
    // A single sprite at the same place is fine - it has no second half.
    assert(sprites_stack_add(100, 160, centre, kSunPivotX, kSunPivotY, kSun, NB,
                             7, 0));
  }

  printf("  an object over the bottom edge is clipped, not hidden\n");
  // docs/clouds.md §1.8: the old sun path culled anything reaching past the
  // bottom of the viewport. It is admitted and clipped instead - sprite DMA is
  // switched off above the split, which is what keeps the raster timing
  // stable, and the cull plays no part in that.
  sprites_stack_reset();
  {
    const int16_t centre = (int16_t)kSpriteVisibleEndYPixels - 1 + kSunPivotY;
    assert(sprites_stack_add(100, 160, centre, kSunPivotX, kSunPivotY, kSun, NB,
                             7, 0));
    sprites_stack_commit();
    show_terrain();
    assert(enabled(0));
    assert(spr_y(0) == (uint8_t)(centre - kSunPivotY + 50));
  }

  printf("  the message strip\n");
  msg_is_active = true;
  msg_span_x0 = 100;
  msg_span_x1 = 220;
  sprites_stack_reset();
  // Row 0, inside the span: hidden.
  assert(!sprites_stack_add(100, 160, 4, kSunPivotX, kSunPivotY, kSun, NB, 7, 0));
  // Row 0, clear of the span to the left: drawn. The span test is a box
  // overlap, not a row test, which is the point of doing it this way.
  assert(sprites_stack_add(100, 40, 4, kSunPivotX, kSunPivotY, kSun, NB, 7, 0));
  // Same column, but below the message: drawn.
  assert(sprites_stack_add(100, 160, 60, kSunPivotX, kSunPivotY, kSun, NB, 7, 0));
  msg_is_active = false;
  // With no message up, the first one is fine.
  sprites_stack_reset();
  assert(sprites_stack_add(100, 160, 4, kSunPivotX, kSunPivotY, kSun, NB, 7, 0));
}

static void test_dither_lattice(void) {
  printf("  the dither lattice, swept over the viewport\n");
  // docs/clouds.md §4.2: every cloud sprite has to land on one global
  // checkerboard, or overlapping blobs fill each other's holes in and the
  // group reads as a solid lump. In VIC coordinates that is X even and
  // (X >> 1) + Y even.
  int placed = 0;
  for (int16_t x = -40; x < 340; ++x) {
    for (int16_t y = -40; y < 120; ++y) {
      sprites_stack_reset();
      if (!sprites_stack_add(100, x, y, 12, 10, 80, NB, kColorWhite,
                             kSpriteFlagExpandX | kSpriteFlagAlignDither)) {
        continue;
      }
      sprites_stack_commit();
      show_terrain();
      int16_t vx = spr_x(0) + (spr_msbx() & 1 ? 256 : 0);
      int16_t vy = spr_y(0);
      assert((vx & 1) == 0);
      assert((((vx >> 1) + vy) & 1) == 0);
      ++placed;
    }
  }
  // "The sweep actually swept something", expressed against the vertical cull
  // so it does not need retuning every time kSpritesOffLead moves. 300 is a
  // conservative count of the horizontally admissible x positions.
  assert(placed > 300 * ((int)kSpriteVisibleEndYPixels - 10));
  printf("    %d positions checked\n", placed);

  printf("  an unflagged object is not snapped\n");
  // The sun is solid, so snapping it would cost a pixel of accuracy for
  // nothing. Find a position the snap would have moved and check it did not.
  sprites_stack_reset();
  sprites_stack_add(100, 161, 56, kSunPivotX, kSunPivotY, kSun, NB, kColorSun, 0);
  sprites_stack_commit();
  show_terrain();
  assert(spr_x(0) == (uint8_t)(161 - 12 + 24)); // odd, and left alone
}

static void test_orientation(void) {
  printf("  the orientation indicator owns sprite 7\n");
  // Its art is the block the cloud ladder's rung 0 used to hold - the one
  // bitmap in spritedef.bin no cloud can reach, because a collapsed group draws
  // one rung *larger* than its own (docs/clouds.md §3.5). The position below is
  // worked out from a pivot spelled out in sprites.cc rather than read from the
  // meta, so a regenerated blob that moved the pivot has to be noticed here.
  assert(kSpriteDefOrient.pivot_x == (int8_t)kSpriteOrientPivotX);
  assert(kSpriteDefOrient.pivot_y == (int8_t)kSpriteOrientPivotY);
  // And it is not one of the cloud ladder's rungs any more.
  for (uint8_t i = 0; i < kSpriteDefCloudRungCount; ++i) {
    assert(kSpriteDefCloudRung[i].bitmap != kSpriteDefOrient.bitmap_idx);
    assert(kSpriteDefCloudRung[i].bitmap2 != kSpriteDefOrient.bitmap_idx);
  }

  sprites_stack_reset();
  sprites_set_orientation();
  sprites_stack_commit();
  show_terrain();
  assert(enabled(kSpriteIdxOrient));
  assert(spr_ptr(kSpriteIdxOrient) == kSpriteDefOrient.bitmap_idx);
  assert(spr_color(kSpriteIdxOrient) == kColorOrientation);
  // Not expanded, unlike the clouds: the bar is its own 24 pixels wide.
  assert(spr_expand() == 0x00);
  assert(spr_enable() == kSpriteOrientBit);
  // Dead centre of the viewport: x = 160 - 12 + 24, y = 56 - 10 + 50. Both fit
  // a byte, so there is no ninth X bit.
  assert(spr_x(kSpriteIdxOrient) == 172);
  assert(spr_y(kSpriteIdxOrient) == 96);
  assert(spr_msbx() == 0x00);

  printf("  and it does not move\n");
  // The whole point of the mark: the horizon moves against it. Commit it again
  // after a frame full of objects and it is in the same place.
  const uint8_t x = spr_x(kSpriteIdxOrient), y = spr_y(kSpriteIdxOrient);
  sprites_stack_reset();
  for (int i = 0; i < 7; ++i) {
    sprites_stack_add((int16_t)(100 + i), (int16_t)(20 + 40 * i), 30,
                      kSunPivotX, kSunPivotY, (uint8_t)(i + 1), NB, 1, 0);
  }
  sprites_set_orientation();
  sprites_stack_commit();
  show_terrain();
  assert(spr_x(kSpriteIdxOrient) == x && spr_y(kSpriteIdxOrient) == y);
  // Seven objects and the mark fill all eight indices, and the mark is still on
  // the one the stack cannot hand out.
  assert(spr_enable() == 0xFF);
  for (int i = 0; i < kSpriteTerrainSlots; ++i) {
    assert(spr_ptr(i) == (uint8_t)(i + 1));
  }
  assert(spr_ptr(kSpriteIdxOrient) == kSpriteDefOrient.bitmap_idx);

  printf("  a frame that sets no mark leaves sprite 7 alone\n");
  sprites_stack_reset();
  sprites_stack_commit();
  show_terrain();
  assert(!enabled(kSpriteIdxOrient));
  assert(spr_x(kSpriteIdxOrient) == 0 && spr_y(kSpriteIdxOrient) == 0);
  // Including the needle's colour, which the panel band would otherwise have to
  // give back for a mark that was never there.
  assert(spr_color(kSpriteIdxOrient) == kColorInstrument);

  printf("  the side views do not carry it\n");
  // A mark fixed to the middle of the screen is a reference for where the nose
  // is pointing, and out of a side window it is not pointing there.
  static const view_state_t kSideViews[2] = {VIEW_LEFT, VIEW_RIGHT};
  for (uint8_t i = 0; i < 2; ++i) {
    view_state = kSideViews[i];
    sprites_stack_reset();
    sprites_set_orientation();
    sprites_stack_commit();
    show_terrain();
    assert(!enabled(kSpriteIdxOrient));
    assert(spr_enable() == 0x00);
  }
  view_state = VIEW_CENTER;

  printf("  the panel band takes sprite 7 back for the needle\n");
  sprites_set_vspeed(0);
  sprites_stack_reset();
  sprites_set_orientation();
  sprites_stack_commit();
  show_terrain();
  // Read before the panel band runs: kSpriteIdxOrient and kSpriteIdxVSpeed are
  // the same index, which is the whole subject of this case.
  const uint8_t mark_y = spr_y(kSpriteIdxOrient);
  sprites_show_panel_top_sprites();
  assert(spr_color(kSpriteIdxVSpeed) == kColorInstrument);
  assert(spr_expand() == 0x00);
  assert(spr_x(kSpriteIdxVSpeed) == _sprites_instrument_xy[kSpriteIdxVSpeed].x);
  assert(spr_y(kSpriteIdxVSpeed) == _sprites_instrument_xy[kSpriteIdxVSpeed].y);
  assert(spr_ptr(kSpriteIdxVSpeed) == _sprites_instrument_idx[kSpriteIdxVSpeed]);
  // Sharing the index is safe because the VIC has finished fetching the mark
  // before the needle's line comes round: a viewport sprite may not begin below
  // 139, and this one begins at 96 and is done 21 lines later (clouds.md §1.9).
  assert(mark_y < 139);
  assert(mark_y + kSpriteHeightPixels < spr_y(kSpriteIdxVSpeed));
}

static void test_double_buffer(void) {
  printf("  the frame is published by the flip, not written in place\n");
  sprites_stack_reset();
  sprites_stack_add(100, 160, 56, kSunPivotX, kSunPivotY, 42, NB, 1, 0);
  sprites_stack_commit();
  show_terrain();
  assert(spr_ptr(0) == 42);

  // Build a different frame. Until commit, the band still programs the old one
  // - which is what stops a raster interrupt landing mid-write from putting one
  // object's X against another's Y.
  sprites_stack_reset();
  sprites_stack_add(100, 100, 56, kSunPivotX, kSunPivotY, 43, NB, 1, 0);
  show_terrain();
  assert(spr_ptr(0) == 42);
  sprites_stack_commit();
  show_terrain();
  assert(spr_ptr(0) == 43);

  // And the two buffers alternate rather than one being favoured.
  uint8_t first = _sprites_frame_shown;
  sprites_stack_reset();
  sprites_stack_commit();
  assert(_sprites_frame_shown == (first ^ 1));
  sprites_stack_commit();
  assert(_sprites_frame_shown == first);
}

static void test_panel_bands(void) {
  printf("  the panel bands undo what the terrain band set up\n");
  // Give the instruments defined positions and pointers first.
  sprites_set_vspeed(0);
  sprites_set_speed(0);
  sprites_set_fuel(0x21FFF);
  sprites_set_throttle(0);
  sprites_set_pitch(0);
  sprites_set_roll(0);
  sprites_set_alt(0);

  // A near, expanded, coloured object in every slot the terrain band may use,
  // which is the worst case for the panel to inherit. Three 1 x 2 entries fill
  // six of the seven; the seventh is left over and index 7 is never offered.
  sprites_stack_reset();
  for (int i = 0; i < 4; ++i) {
    sprites_stack_add((int16_t)(100 + i), 160, 56, 12, 20, 85, 86, kColorRed,
                      kSpriteFlagExpandX);
  }
  sprites_stack_commit();
  show_terrain();
  assert(spr_expand() == 0x3F);
  assert(spr_color(5) == kColorRed);
  // The needle's index is untouched by the terrain band, in every register
  // that could reach across the split.
  assert(!enabled(kSpriteIdxVSpeed));
  assert(spr_color(kSpriteIdxVSpeed) == kColorInstrument);
  assert((spr_expand() >> kSpriteIdxVSpeed) == 0);

  sprites_show_panel_top_sprites();
  // $D01D. Parking at x = 0 hides a 24 pixel sprite because the left border
  // ends at 24; an X-expanded one is 48 wide and would poke into the panel.
  assert(spr_expand() == 0x00);
  // Sprite 7 is the vertical speed needle, the one instrument drawn in this
  // band, so it is the one colour the terrain band has to give back here.
  assert(spr_color(7) == kColorInstrument);
  assert(spr_enable() == 0xFF);
  assert(spr_msbx() == 0x00);
  for (uint8_t i = 0; i < 7; ++i) {
    assert(spr_x(i) == 0);
  }
  assert(spr_x(kSpriteIdxVSpeed) == _sprites_instrument_xy[kSpriteIdxVSpeed].x);
  assert(spr_ptr(kSpriteIdxVSpeed) == _sprites_instrument_idx[kSpriteIdxVSpeed]);

  sprites_show_panel_bottom_sprites();
  // All eight, unconditionally. Doing it here is what frees the terrain band to
  // write every colour without a handshake with the panel code.
  for (uint8_t i = 0; i < 8; ++i) {
    assert(spr_color(i) == kColorInstrument);
  }
  for (uint8_t i = 0; i < 7; ++i) {
    assert(spr_x(i) == _sprites_instrument_xy[i].x);
    assert(spr_y(i) == _sprites_instrument_xy[i].y);
    assert(spr_ptr(i) == _sprites_instrument_idx[i]);
  }
  assert(spr_msbx() == (1 << kSpriteIdxThrottle));

  printf("  a side view restores the colours but only its own instrument\n");
  view_state = VIEW_LEFT;
  show_terrain();
  sprites_show_panel_top_sprites();
  sprites_show_panel_bottom_sprites();
  for (uint8_t i = 0; i < 8; ++i) {
    assert(spr_color(i) == kColorInstrument);
  }
  assert(spr_x(kSpriteIdxFuel) == _sprites_instrument_xy[kSpriteIdxFuel].x);
  assert(spr_msbx() == (1 << kSpriteIdxFuel));
  view_state = VIEW_CENTER;

  printf("  debug mode drops the sprites entirely\n");
  show_terrain();
  sprites_show_no_sprites();
  assert(spr_enable() == 0x00);
  assert(spr_expand() == 0x00);
  assert(spr_msbx() == 0x00);
}

static void test_init(void) {
  printf("  sprites_init leaves a defined frame and defined registers\n");
  memset(vic_regs, 0xAA, sizeof(vic_regs));
  sprites_init();
  // sprite_objects.md §0: hires only, never Y-expanded, always in front of the
  // terrain. Nothing else in the program writes these, so if init does not,
  // they are whatever the machine came up with.
  assert(vic_regs[0x17] == 0x00); // spr_expand_y
  assert(vic_regs[0x1b] == 0x00); // spr_priority
  assert(vic_regs[0x1c] == 0x00); // spr_multi
  assert(vic_regs[0x1d] == 0x00); // spr_expand_x
  assert(vic_regs[0x15] == 0xFF); // spr_enable

  // Both frames were committed empty, so a terrain interrupt arriving before
  // the first world_update_objects() programs nothing rather than bss.
  show_terrain();
  assert(spr_enable() == 0x00);
  sprites_stack_commit();
  show_terrain();
  assert(spr_enable() == 0x00);
}

int main() {
  printf("Running sprites_test...\n");
  test_init();
  test_empty_frame();
  test_single_object();
  test_msbx();
  test_nearest_first();
  test_two_sprite_stack();
  test_overflow();
  test_no_index_used_twice();
  test_culls();
  test_dither_lattice();
  test_orientation();
  test_double_buffer();
  test_panel_bands();
  printf("sprites_test PASSED\n");
  return 0;
}
