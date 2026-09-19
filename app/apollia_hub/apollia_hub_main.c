/****************************************************************************
 * contest2026_435_UdifyFun/app/apollia_hub/apollia_hub_main.c
 *
 * Vela contest 2026 team 435 (UdifyFun) - Apollia Hub for BEST1700 AOS
 * EVB (BES2800BP).
 *
 * Apollia - named after the sea siren: an instrument that enchants the
 * room with its song. The round-screen hub is the "siren's vortex":
 * it gathers the song of both hands (7 ring sensors each), swirls the
 * notes through its core and casts them out to the DAW as sonar-like
 * ripples over USB-MIDI. See docs/apollia_design.md.
 *
 *   left arc    - chords  (I/IV/V major, II/III/VI minor, VII dim)
 *   right arc   - melody  (C5..B5)
 *   core drag   - effect wetness (CC91, mimics IMU up/down)
 *   INST button - program change (mimics fist-twist)
 *   role avatars - multi-player mock (drummer on ch10 etc.)
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/boardctl.h>

#include <lvgl/lvgl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#undef NEED_BOARDINIT
#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#  define NEED_BOARDINIT 1
#endif

#define MIDI_DEV_PATH "/dev/ttyS0"

#define HUB_SENSOR_COUNT 14
#define HUB_PARTICLE_MAX 12
#define HUB_ROLE_COUNT   4
#define HUB_INST_COUNT   4

/* Auto-play sequencer: Pachelbel's Canon in D (public domain). */

#define SEQ_STEP_MS 280
#define SEQ_BARS     8
#define SEQ_STEPS    (SEQ_BARS * 8)

#define HUB_TRIGO 32768    /* matches LV_TRIGO_SIN_MAX */

/* Siren palette: deep-sea abyss with teal song. */

#define SIREN_TEAL   0x2dd4bf
#define SIREN_AQUA   0x38bdf8
#define SIREN_CORAL  0xff6b8a
#define SIREN_TEXT   0xbfe8f0

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct
{
  lv_obj_t * dot;
  lv_obj_t * halo;
  uint8_t    notes[3];
  uint8_t    ncount;
  uint8_t    ch;
  uint32_t   color;
  int16_t    angle;      /* polar position: 0=east, ccw, screen y down */
  bool       held;
} hub_sensor_t;

typedef struct
{
  lv_obj_t * obj;
  bool       busy;
} hub_particle_t;

typedef struct
{
  const char * name;
  uint8_t      pc;
} hub_inst_t;

typedef struct
{
  const char * name;
  uint32_t     color;
  uint8_t      ch;
  uint8_t      notes[3];
  uint8_t      ncount;
} hub_role_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int          g_midifd = -1;
static int          g_wet = 64;
static int          g_inst;
static uint32_t     g_tx;
static lv_point_t   g_drag_last;

static lv_obj_t   * g_core;
static lv_obj_t   * g_pulse;
static lv_obj_t   * g_ripple[2];
static lv_obj_t   * g_wet_label;
static lv_obj_t   * g_inst_label;
static lv_obj_t   * g_tx_label;

static hub_sensor_t   g_sensors[HUB_SENSOR_COUNT];
static hub_particle_t g_particles[HUB_PARTICLE_MAX];
static lv_obj_t     * g_role_dots[HUB_ROLE_COUNT];
static lv_obj_t     * g_role_labels[HUB_ROLE_COUNT];

static const hub_inst_t g_insts[HUB_INST_COUNT] =
{
  { "PIANO",   0  },
  { "GUITAR",  24 },
  { "STRINGS", 48 },
  { "SYNTH",   81 },
};

static const hub_role_t g_roles[HUB_ROLE_COUNT] =
{
  { "DRUM", 0xff9f2e, 9, { 36, 38, 0 }, 2 },    /* ch10 kick+snare */
  { "PAD",  0xbf5af2, 0, { 57, 60, 64 }, 3 },   /* Am triad */
  { "RHY",  0x30d158, 0, { 65, 0, 0 }, 1 },     /* F4 */
  { "LEAD", 0xff5470, 0, { 79, 0, 0 }, 1 },     /* G5 */
};

/* Left hand chords along the left arc (top to bottom):
 * idx-upper(I) idx-mid(II) mid-upper(IV) mid-mid(III)
 * ring-upper(V) ring-mid(VI) pinky(VII dim)
 */

static const uint8_t g_left_notes[7][3] =
{
  { 60, 64, 67 },  /* I   C  major */
  { 62, 65, 69 },  /* II  Dm       */
  { 65, 69, 72 },  /* IV  F  major */
  { 64, 67, 71 },  /* III Em       */
  { 67, 71, 74 },  /* V   G  major */
  { 57, 60, 64 },  /* VI  Am       */
  { 59, 62, 65 },  /* VII B  dim   */
};

static const uint32_t g_left_col[7] =
{
  0xffb020, 0x6c8cff, 0xffb020, 0x6c8cff, 0xffb020, 0x6c8cff, 0xff5470,
};

static const uint8_t g_right_notes[7] = { 72, 74, 76, 77, 79, 81, 83 };
static const int16_t g_left_angles[7]  = { 150, 160, 170, 180, 190, 200, 210 };
static const int16_t g_right_angles[7] = { 30, 20, 10, 0, -10, -20, -30 };

/* Canon in D melody: eighth-note pairs, the famous stepwise descent.
 * D major: F#5=78 E5=76 D5=74 C#5=73 B4=71 A4=69 G4=67 F#4=66.
 */

static const uint8_t g_seq[SEQ_STEPS] =
{
  78, 78, 76, 76, 74, 74, 73, 73,
  71, 71, 69, 69, 71, 71, 73, 73,
  74, 74, 73, 73, 71, 71, 69, 69,
  67, 67, 66, 66, 67, 67, 69, 69,
  71, 71, 73, 73, 74, 74, 76, 76,
  78, 78, 76, 76, 74, 74, 73, 73,
  71, 71, 69, 69, 71, 71, 73, 73,
  74, 74, 73, 73, 71, 71, 69, 69,
};

/* The ground: D A Bm F#m G D G A */

static const uint8_t g_seq_chords[SEQ_BARS][3] =
{
  { 62, 66, 69 }, { 57, 61, 64 }, { 59, 62, 66 }, { 54, 58, 61 },
  { 55, 59, 62 }, { 62, 66, 69 }, { 55, 59, 62 }, { 57, 61, 64 },
};

static bool       g_auto_on;
static int        g_auto_step;
static uint8_t    g_auto_note;         /* currently sounding melody note */
static uint8_t    g_auto_chord[3];
static int        g_auto_lit_l = -1;   /* sensor dots lit by the auto play */
static int        g_auto_lit_r = -1;
static lv_obj_t * g_auto_btn;
static lv_obj_t * g_auto_label;
static lv_timer_t * g_auto_timer;

/****************************************************************************
 * MIDI
 ****************************************************************************/

static void midi_send(uint8_t b0, uint8_t b1, uint8_t b2)
{
  uint8_t msg[3];

  if (g_midifd < 0)
    {
      return;
    }

  msg[0] = b0;
  msg[1] = b1 & 0x7f;
  msg[2] = b2 & 0x7f;
  write(g_midifd, msg, 3);
  g_tx++;
}

static void midi_notes(bool on, uint8_t ch, const uint8_t * n, uint8_t cnt)
{
  uint8_t st = (on ? 0x90 : 0x80) | (ch & 0x0f);
  int i;

  for (i = 0; i < cnt; i++)
    {
      midi_send(st, n[i], on ? 100 : 0);
    }
}

/****************************************************************************
 * Geometry
 ****************************************************************************/

static void place_polar(lv_obj_t * o, int32_t cx, int32_t cy,
                        int16_t deg, int32_t r, int32_t w, int32_t h)
{
  int32_t x = cx + (lv_trigo_cos(deg) * r) / HUB_TRIGO;
  int32_t y = cy - (lv_trigo_sin(deg) * r) / HUB_TRIGO;

  /* Size must be passed in: coords are not refreshed until the layout
   * runs, so lv_obj_get_width() would return a stale value right after
   * lv_obj_set_size() and skew every dot half a body to the right.
   */

  lv_obj_set_pos(o, x - w / 2, y - h / 2);
}

/****************************************************************************
 * The siren's song: sonar ripples and data-flow particles
 ****************************************************************************/

static void core_pulse(void);

static void particle_opa_cb(void * var, int32_t v)
{
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void particle_out_done_cb(lv_anim_t * a)
{
  int idx = (int)(intptr_t)lv_obj_get_user_data(a->var);

  lv_obj_add_flag(a->var, LV_OBJ_FLAG_HIDDEN);
  g_particles[idx].busy = false;
}

static void particle_in_done_cb(lv_anim_t * a)
{
  lv_obj_t * obj = a->var;
  lv_obj_t * scr = lv_obj_get_parent(obj);
  int32_t cx = lv_obj_get_width(scr) / 2 - 3;
  int32_t cy = lv_obj_get_height(scr) * 90 / 100 - 3;
  lv_anim_t na;

  /* Reached the core: pulse it, then swirl out to the DAW outlet. */

  core_pulse();

  lv_anim_init(&na);
  lv_anim_set_var(&na, obj);
  lv_anim_set_exec_cb(&na, (lv_anim_exec_xcb_t)lv_obj_set_x);
  lv_anim_set_values(&na, lv_obj_get_x(obj), cx);
  lv_anim_set_duration(&na, 380);
  lv_anim_start(&na);

  lv_anim_init(&na);
  lv_anim_set_var(&na, obj);
  lv_anim_set_exec_cb(&na, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_set_values(&na, lv_obj_get_y(obj), cy);
  lv_anim_set_duration(&na, 380);
  lv_anim_start(&na);

  lv_anim_init(&na);
  lv_anim_set_var(&na, obj);
  lv_anim_set_exec_cb(&na, particle_opa_cb);
  lv_anim_set_values(&na, LV_OPA_60, LV_OPA_TRANSP);
  lv_anim_set_duration(&na, 380);
  lv_anim_set_completed_cb(&na, particle_out_done_cb);
  lv_anim_start(&na);
}

static void particle_spawn(int32_t from_x, int32_t from_y, uint32_t color)
{
  lv_obj_t * scr = lv_display_get_screen_active(lv_display_get_default());
  int32_t cx = lv_obj_get_width(scr) / 2 - 3;
  int32_t cy = lv_obj_get_height(scr) / 2 - 3;
  lv_anim_t a;
  int i;

  for (i = 0; i < HUB_PARTICLE_MAX; i++)
    {
      if (!g_particles[i].busy)
        {
          break;
        }
    }

  if (i == HUB_PARTICLE_MAX)
    {
      return;    /* pool exhausted - drop the visual, MIDI already sent */
    }

  g_particles[i].busy = true;
  lv_obj_clear_flag(g_particles[i].obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_color(g_particles[i].obj, lv_color_hex(color), 0);
  lv_obj_set_pos(g_particles[i].obj, from_x - 3, from_y - 3);
  lv_obj_set_style_opa(g_particles[i].obj, LV_OPA_COVER, 0);

  lv_anim_init(&a);
  lv_anim_set_var(&a, g_particles[i].obj);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
  lv_anim_set_values(&a, from_x - 3, cx);
  lv_anim_set_duration(&a, 420);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_start(&a);

  lv_anim_init(&a);
  lv_anim_set_var(&a, g_particles[i].obj);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_set_values(&a, from_y - 3, cy);
  lv_anim_set_duration(&a, 420);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_start(&a);

  lv_anim_init(&a);
  lv_anim_set_var(&a, g_particles[i].obj);
  lv_anim_set_exec_cb(&a, particle_opa_cb);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_40);
  lv_anim_set_duration(&a, 420);
  lv_anim_set_completed_cb(&a, particle_in_done_cb);
  lv_anim_start(&a);
}

static void core_pulse_exec_cb(void * var, int32_t v)
{
  lv_arc_set_value((lv_obj_t *)var, v);
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)(255 - v * 2), 0);
}

static void core_pulse(void)
{
  lv_anim_t a;

  lv_anim_init(&a);
  lv_anim_set_var(&a, g_pulse);
  lv_anim_set_exec_cb(&a, core_pulse_exec_cb);
  lv_anim_set_values(&a, 0, 100);
  lv_anim_set_duration(&a, 500);
  lv_anim_start(&a);
}

/* Ambient siren song: sonar rings drift outward forever, staggered so
 * one is always blooming from the core.
 */

static void ripple_value_cb(void * var, int32_t v)
{
  lv_arc_set_value((lv_obj_t *)var, v);
}

static void ripple_ambient_start(lv_obj_t * ring, uint32_t color,
                                 uint32_t delay)
{
  lv_anim_t a;

  lv_obj_set_style_arc_color(ring, lv_color_hex(color), LV_PART_INDICATOR);

  lv_anim_init(&a);
  lv_anim_set_var(&a, ring);
  lv_anim_set_exec_cb(&a, ripple_value_cb);
  lv_anim_set_values(&a, 0, 100);
  lv_anim_set_duration(&a, 2600);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);

  lv_anim_init(&a);
  lv_anim_set_var(&a, ring);
  lv_anim_set_exec_cb(&a, particle_opa_cb);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_duration(&a, 2600);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

static lv_obj_t * ripple_create(lv_obj_t * parent, int32_t size)
{
  lv_obj_t * ring = lv_arc_create(parent);

  lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(ring, size, size);
  lv_obj_center(ring);
  lv_arc_set_range(ring, 0, 100);
  lv_arc_set_bg_angles(ring, 0, 360);
  lv_arc_set_rotation(ring, 270);
  lv_obj_set_style_arc_width(ring, 2, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring, 2, LV_PART_INDICATOR);
  lv_arc_set_value(ring, 0);
  return ring;
}

/****************************************************************************
 * Sensors
 ****************************************************************************/

static void sensor_paint(hub_sensor_t * s, bool pressed)
{
  lv_obj_set_style_bg_color(s->dot,
                            lv_color_hex(pressed ? 0xffffff : s->color), 0);
  lv_obj_set_style_shadow_opa(s->dot,
                              pressed ? LV_OPA_COVER : LV_OPA_40, 0);
}

static void sensor_event_cb(lv_event_t * e)
{
  lv_obj_t * obj = lv_event_get_target(e);
  lv_event_code_t code = lv_event_get_code(e);
  int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
  hub_sensor_t * s = &g_sensors[idx];

  if (code == LV_EVENT_PRESSED && !s->held)
    {
      s->held = true;
      midi_notes(true, s->ch, s->notes, s->ncount);
      sensor_paint(s, true);
      particle_spawn(lv_obj_get_x(s->dot) + lv_obj_get_width(s->dot) / 2,
                     lv_obj_get_y(s->dot) + lv_obj_get_height(s->dot) / 2,
                     s->color);
    }
  else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
           && s->held)
    {
      s->held = false;
      midi_notes(false, s->ch, s->notes, s->ncount);
      sensor_paint(s, false);
    }
}

static void halo_breath_cb(void * var, int32_t v)
{
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void sensor_create(lv_obj_t * parent, hub_sensor_t * s, int idx,
                          int16_t angle, uint32_t color,
                          const uint8_t * notes, uint8_t ncount,
                          int32_t cx, int32_t cy, int32_t r, int32_t dot_d)
{
  lv_anim_t a;

  memset(s, 0, sizeof(*s));
  s->color = color;
  s->angle = angle;
  s->ncount = ncount;
  s->ch = 0;
  memcpy(s->notes, notes, ncount);

  /* Halo = the hand-back breathing light. */

  s->halo = lv_obj_create(parent);
  lv_obj_remove_flag(s->halo,
                     LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s->halo, dot_d * 16 / 10, dot_d * 16 / 10);
  lv_obj_set_style_radius(s->halo, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(s->halo, lv_color_hex(color), 0);
  lv_obj_set_style_border_opa(s->halo, LV_OPA_TRANSP, 0);
  place_polar(s->halo, cx, cy, angle, r, dot_d * 16 / 10, dot_d * 16 / 10);

  lv_anim_init(&a);
  lv_anim_set_var(&a, s->halo);
  lv_anim_set_exec_cb(&a, halo_breath_cb);
  lv_anim_set_values(&a, LV_OPA_20, LV_OPA_50);
  lv_anim_set_duration(&a, 1500 + (idx % 5) * 180);
  lv_anim_set_playback_duration(&a, 1500 + (idx % 5) * 180);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);

  /* Dot = palm press target. */

  s->dot = lv_obj_create(parent);
  lv_obj_set_size(s->dot, dot_d, dot_d);
  lv_obj_set_style_radius(s->dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s->dot, 0, 0);
  lv_obj_set_style_bg_color(s->dot, lv_color_hex(color), 0);
  lv_obj_set_style_shadow_width(s->dot, 12, 0);
  lv_obj_set_style_shadow_opa(s->dot, LV_OPA_40, 0);
  lv_obj_set_style_shadow_color(s->dot, lv_color_hex(color), 0);
  lv_obj_add_flag(s->dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s->dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_user_data(s->dot, (void *)(intptr_t)idx);
  place_polar(s->dot, cx, cy, angle, r, dot_d, dot_d);
  lv_obj_add_event_cb(s->dot, sensor_event_cb, LV_EVENT_ALL, NULL);
}

/****************************************************************************
 * Core drag = effect wetness (mimics IMU up/down)
 ****************************************************************************/

static void core_event_cb(lv_event_t * e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  int dy;
  int w;

  lv_indev_get_point(lv_indev_active(), &p);

  if (code == LV_EVENT_PRESSED)
    {
      g_drag_last = p;
    }
  else if (code == LV_EVENT_PRESSING)
    {
      dy = p.y - g_drag_last.y;
      if (dy == 0)
        {
          return;
        }

      g_drag_last = p;
      w = g_wet - dy;                 /* drag up = wetter */
      if (w < 0)
        {
          w = 0;
        }

      if (w > 127)
        {
          w = 127;
        }

      if (w != g_wet)
        {
          g_wet = w;
          lv_label_set_text_fmt(g_wet_label, "WET %d", g_wet);
          midi_send(0xb0, 91, (uint8_t)g_wet);
        }
    }
}

/****************************************************************************
 * Instrument switch (mimics fist-twist)
 ****************************************************************************/

static void inst_event_cb(lv_event_t * e)
{
  LV_UNUSED(e);
  g_inst = (g_inst + 1) % HUB_INST_COUNT;
  midi_send(0xc0, g_insts[g_inst].pc, 0);
  lv_label_set_text(g_inst_label, g_insts[g_inst].name);
}

/****************************************************************************
 * Auto play - the siren sings by herself (mocks the glove input)
 ****************************************************************************/

static void auto_unlight(void)
{
  if (g_auto_lit_l >= 0)
    {
      if (!g_sensors[g_auto_lit_l].held)
        {
          sensor_paint(&g_sensors[g_auto_lit_l], false);
        }

      g_auto_lit_l = -1;
    }

  if (g_auto_lit_r >= 0)
    {
      if (!g_sensors[g_auto_lit_r].held)
        {
          sensor_paint(&g_sensors[g_auto_lit_r], false);
        }

      g_auto_lit_r = -1;
    }
}

static void auto_all_off(void)
{
  int i;

  if (g_auto_note)
    {
      midi_send(0x80, g_auto_note, 0);
      g_auto_note = 0;
    }

  for (i = 0; i < 3; i++)
    {
      if (g_auto_chord[i])
        {
          midi_send(0x80, g_auto_chord[i], 0);
          g_auto_chord[i] = 0;
        }
    }

  auto_unlight();
}

static void auto_light(int idx, hub_sensor_t * s)
{
  sensor_paint(s, true);
  particle_spawn(lv_obj_get_x(s->dot) + lv_obj_get_width(s->dot) / 2,
                 lv_obj_get_y(s->dot) + lv_obj_get_height(s->dot) / 2,
                 s->color);
}

static void auto_step_cb(lv_timer_t * t)
{
  uint8_t note = g_seq[g_auto_step];
  int bar = g_auto_step / 8;
  int ridx;
  int i;

  LV_UNUSED(t);

  /* melody: cut the previous note, strike the next one and light the
   * matching right-arc sensor (mock of the melody ring press).
   */

  if (g_auto_note)
    {
      midi_send(0x80, g_auto_note, 0);
      g_auto_note = 0;
    }

  auto_unlight();

  if (note)
    {
      ridx = (note - 66) / 2;
      if (ridx < 0)
        {
          ridx = 0;
        }

      if (ridx > 6)
        {
          ridx = 6;
        }

      midi_send(0x90, note, 96);
      g_auto_note = note;
      g_auto_lit_r = 7 + ridx;
      auto_light(g_auto_lit_r, &g_sensors[g_auto_lit_r]);
    }

  /* chord bed: retune at every bar line, light the left-arc partner */

  if ((g_auto_step % 8) == 0)
    {
      int lidx = bar % 7;

      for (i = 0; i < 3; i++)
        {
          if (g_auto_chord[i])
            {
              midi_send(0x80, g_auto_chord[i], 0);
            }

          g_auto_chord[i] = g_seq_chords[bar][i];
          midi_send(0x90, g_auto_chord[i], 55);
        }

      g_auto_lit_l = lidx;
      auto_light(lidx, &g_sensors[lidx]);
    }

  g_auto_step = (g_auto_step + 1) % SEQ_STEPS;
}

static void auto_event_cb(lv_event_t * e)
{
  LV_UNUSED(e);

  if (g_auto_on)
    {
      g_auto_on = false;
      lv_timer_pause(g_auto_timer);
      auto_all_off();
      lv_label_set_text(g_auto_label, "AUTO");
      lv_obj_set_style_bg_color(g_auto_btn, lv_color_hex(0x0e3a4a), 0);
    }
  else
    {
      g_auto_on = true;
      g_auto_step = 0;
      lv_label_set_text(g_auto_label, "STOP");
      lv_obj_set_style_bg_color(g_auto_btn, lv_color_hex(0x7a2438), 0);
      lv_timer_resume(g_auto_timer);
      auto_step_cb(g_auto_timer);        /* sing the first note now */
    }
}

/****************************************************************************
 * Multi-player roles - the siren choir
 ****************************************************************************/

static void role_event_cb(lv_event_t * e)
{
  lv_obj_t * obj = lv_event_get_target(e);
  lv_event_code_t code = lv_event_get_code(e);
  int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
  const hub_role_t * r = &g_roles[idx];

  if (code == LV_EVENT_PRESSED)
    {
      midi_notes(true, r->ch, r->notes, r->ncount);
      lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffff), 0);
      particle_spawn(lv_obj_get_x(obj) + lv_obj_get_width(obj) / 2,
                     lv_obj_get_y(obj) + lv_obj_get_height(obj) / 2,
                     r->color);
    }
  else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
      midi_notes(false, r->ch, r->notes, r->ncount);
      lv_obj_set_style_bg_color(obj, lv_color_hex(r->color), 0);
    }
}

/* Sirens "join the song" one by one at boot. */

static void role_join_timer_cb(lv_timer_t * t)
{
  int idx = (int)(intptr_t)lv_timer_get_user_data(t);

  lv_obj_set_style_bg_opa(g_role_dots[idx], LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_opa(g_role_dots[idx], LV_OPA_40, 0);
  lv_obj_set_style_text_opa(g_role_labels[idx], LV_OPA_COVER, 0);
  lv_timer_delete(t);
}

static void roles_create(lv_obj_t * scr, int32_t w, int32_t h)
{
  int32_t cx = w / 2;
  int32_t cy = h / 2;
  int32_t rho = LV_MIN(w, h) / 2 * 72 / 100;
  int i;

  for (i = 0; i < HUB_ROLE_COUNT; i++)
    {
      /* Top arc, symmetric about 90 deg - stays inside the round bezel
       * (a plain x-row at fixed y pushed the corner dots outside the
       * circle and clipped DRUM / LEAD).
       */

      int16_t deg = 120 - i * 20;
      int32_t px = cx + (lv_trigo_cos(deg) * rho) / HUB_TRIGO;
      int32_t py = cy - (lv_trigo_sin(deg) * rho) / HUB_TRIGO;
      int32_t d = w * 6 / 100;
      lv_timer_t * tmr;

      g_role_dots[i] = lv_obj_create(scr);
      lv_obj_set_size(g_role_dots[i], d, d);
      lv_obj_set_pos(g_role_dots[i], px - d / 2, py - d / 2);
      lv_obj_set_style_radius(g_role_dots[i], LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(g_role_dots[i], 0, 0);
      lv_obj_set_style_bg_color(g_role_dots[i],
                                lv_color_hex(g_roles[i].color), 0);
      lv_obj_set_style_bg_opa(g_role_dots[i], LV_OPA_TRANSP, 0);
      lv_obj_set_style_shadow_width(g_role_dots[i], 10, 0);
      lv_obj_set_style_shadow_opa(g_role_dots[i], LV_OPA_TRANSP, 0);
      lv_obj_set_style_shadow_color(g_role_dots[i],
                                    lv_color_hex(g_roles[i].color), 0);
      lv_obj_add_flag(g_role_dots[i], LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(g_role_dots[i], LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_user_data(g_role_dots[i], (void *)(intptr_t)i);
      lv_obj_add_event_cb(g_role_dots[i], role_event_cb, LV_EVENT_ALL, NULL);

      g_role_labels[i] = lv_label_create(scr);
      lv_label_set_text(g_role_labels[i], g_roles[i].name);
      lv_obj_set_style_text_font(g_role_labels[i], &lv_font_montserrat_20, 0);
      lv_obj_set_style_text_color(g_role_labels[i],
                                  lv_color_hex(g_roles[i].color), 0);
      lv_obj_set_style_text_opa(g_role_labels[i], LV_OPA_TRANSP, 0);
      lv_obj_align(g_role_labels[i], LV_ALIGN_TOP_MID, px - cx,
                   py + d / 2 + 2);

      tmr = lv_timer_create(role_join_timer_cb, 700 * (i + 1),
                            (void *)(intptr_t)i);
      LV_UNUSED(tmr);
    }
}

/****************************************************************************
 * UI build - the siren's vortex
 ****************************************************************************/

static void hub_ui_create(lv_display_t * disp)
{
  lv_obj_t * scr = lv_display_get_screen_active(disp);
  int32_t w = lv_obj_get_width(scr);
  int32_t h = lv_obj_get_height(scr);
  int32_t cx = w / 2;
  int32_t cy = h / 2;
  int32_t r = LV_MIN(w, h) / 2;
  int32_t dot_d = r * 15 / 100;
  lv_obj_t * lbl;
  lv_obj_t * btn;
  int i;

  /* Deep-sea abyss background. */

  lv_obj_set_style_bg_color(scr, lv_color_hex(0x020617), 0);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x0a2540), 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

  /* Branding: the siren's name. */

  lbl = lv_label_create(scr);
  lv_label_set_text(lbl, "APOLLIA");
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(SIREN_TEAL), 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, h * 1 / 100);

  lbl = lv_label_create(scr);
  lv_label_set_text(lbl, "~ SIREN HUB ~");
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x5f8aa3), 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, h * 7 / 100);

  /* Ambient sonar rings - the siren's song always drifting outward. */

  g_ripple[0] = ripple_create(scr, r * 90 / 100);
  g_ripple[1] = ripple_create(scr, r * 90 / 100);
  ripple_ambient_start(g_ripple[0], SIREN_TEAL, 0);
  ripple_ambient_start(g_ripple[1], SIREN_AQUA, 1300);

  /* Core pulse ring (flares on every message). */

  g_pulse = lv_arc_create(scr);
  lv_obj_remove_flag(g_pulse, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(g_pulse, r * 70 / 100, r * 70 / 100);
  lv_obj_center(g_pulse);
  lv_arc_set_range(g_pulse, 0, 100);
  lv_arc_set_bg_angles(g_pulse, 0, 360);
  lv_arc_set_rotation(g_pulse, 270);
  lv_obj_set_style_arc_width(g_pulse, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(g_pulse, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(g_pulse, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g_pulse, lv_color_hex(SIREN_CORAL),
                             LV_PART_INDICATOR);
  lv_arc_set_value(g_pulse, 0);
  lv_obj_set_style_opa(g_pulse, LV_OPA_TRANSP, 0);

  /* Core orb: drag vertically = effect wetness. */

  g_core = lv_obj_create(scr);
  lv_obj_set_size(g_core, r * 34 / 100, r * 34 / 100);
  lv_obj_center(g_core);
  lv_obj_set_style_radius(g_core, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(g_core, 2, 0);
  lv_obj_set_style_border_color(g_core, lv_color_hex(SIREN_AQUA), 0);
  lv_obj_set_style_bg_color(g_core, lv_color_hex(0x0b2a3f), 0);
  lv_obj_set_style_shadow_width(g_core, 24, 0);
  lv_obj_set_style_shadow_opa(g_core, LV_OPA_50, 0);
  lv_obj_set_style_shadow_color(g_core, lv_color_hex(SIREN_AQUA), 0);
  lv_obj_add_flag(g_core, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(g_core, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(g_core, core_event_cb, LV_EVENT_ALL, NULL);

  lbl = lv_label_create(g_core);
  lv_label_set_text(lbl, "SIREN");
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(SIREN_TEXT), 0);
  lv_obj_center(lbl);

  /* Particle pool - notes swirling into the vortex. */

  for (i = 0; i < HUB_PARTICLE_MAX; i++)
    {
      g_particles[i].obj = lv_obj_create(scr);
      lv_obj_remove_flag(g_particles[i].obj,
                         LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_size(g_particles[i].obj, 6, 6);
      lv_obj_set_style_radius(g_particles[i].obj, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_opa(g_particles[i].obj, LV_OPA_TRANSP, 0);
      lv_obj_add_flag(g_particles[i].obj, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_user_data(g_particles[i].obj, (void *)(intptr_t)i);
      g_particles[i].busy = false;
    }

  /* Both hands: 14 sensors on left/right arcs at 80% radius. */

  for (i = 0; i < 7; i++)
    {
      sensor_create(scr, &g_sensors[i], i, g_left_angles[i],
                    g_left_col[i], g_left_notes[i], 3,
                    cx, cy, r * 80 / 100, dot_d);
    }

  for (i = 0; i < 7; i++)
    {
      uint8_t note = g_right_notes[i];

      sensor_create(scr, &g_sensors[7 + i], 7 + i, g_right_angles[i],
                    0x41e0d0, &note, 1,
                    cx, cy, r * 80 / 100, dot_d);
    }

  /* The siren choir joins over the first seconds. */

  roles_create(scr, w, h);

  /* Bottom row - polar placement keeps everything inside the round
   * bezel (BOTTOM_LEFT / BOTTOM_RIGHT corners are outside the circle).
   * Left arc: INST button + instrument name. Right arc: AUTO button +
   * WET readout - the siren sings by herself.
   */

  {
    int32_t px;
    int32_t py;

    /* Instrument name above the INST button, lower-left arc. */

    px = cx + (lv_trigo_cos(-120) * r * 78 / 100) / HUB_TRIGO;
    py = cy - (lv_trigo_sin(-120) * r * 78 / 100) / HUB_TRIGO;

    g_inst_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_inst_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_inst_label, lv_color_hex(0xffb020), 0);
    lv_label_set_text(g_inst_label, g_insts[g_inst].name);
    lv_obj_align(g_inst_label, LV_ALIGN_TOP_MID, px - cx, py - 30);

    /* WET readout above the AUTO button, lower-right arc. */

    px = cx + (lv_trigo_cos(-60) * r * 78 / 100) / HUB_TRIGO;
    py = cy - (lv_trigo_sin(-60) * r * 78 / 100) / HUB_TRIGO;

    g_wet_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_wet_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_wet_label, lv_color_hex(0x41e0d0), 0);
    lv_label_set_text_fmt(g_wet_label, "WET %d", g_wet);
    lv_obj_align(g_wet_label, LV_ALIGN_TOP_MID, px - cx, py - 30);

    /* TX counter, bottom center (kept short so it fits the bezel). */

    g_tx_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_tx_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_tx_label, lv_color_hex(0x9ab8c8), 0);
    lv_label_set_text_fmt(g_tx_label, "USB-MIDI tx %lu", (unsigned long)g_tx);
    lv_obj_align(g_tx_label, LV_ALIGN_TOP_MID, 0, cy + r * 80 / 100);

    /* INST button, lower-left arc. */

    btn = lv_button_create(scr);
    lv_obj_set_size(btn, w * 16 / 100, h * 8 / 100);
    px = cx + (lv_trigo_cos(-120) * r * 78 / 100) / HUB_TRIGO;
    py = cy - (lv_trigo_sin(-120) * r * 78 / 100) / HUB_TRIGO;
    lv_obj_set_pos(btn, px - w * 8 / 100, py - h * 4 / 100);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0e3a4a), 0);
    lv_obj_add_event_cb(btn, inst_event_cb, LV_EVENT_CLICKED, NULL);

    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "INST");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(SIREN_TEXT), 0);
    lv_obj_center(lbl);

    /* AUTO button, lower-right arc (mirrors INST). */

    g_auto_btn = lv_button_create(scr);
    lv_obj_set_size(g_auto_btn, w * 16 / 100, h * 8 / 100);
    px = cx + (lv_trigo_cos(-60) * r * 78 / 100) / HUB_TRIGO;
    py = cy - (lv_trigo_sin(-60) * r * 78 / 100) / HUB_TRIGO;
    lv_obj_set_pos(g_auto_btn, px - w * 8 / 100, py - h * 4 / 100);
    lv_obj_set_style_bg_color(g_auto_btn, lv_color_hex(0x0e3a4a), 0);
    lv_obj_add_event_cb(g_auto_btn, auto_event_cb, LV_EVENT_CLICKED, NULL);

    g_auto_label = lv_label_create(g_auto_btn);
    lv_label_set_text(g_auto_label, "AUTO");
    lv_obj_set_style_text_font(g_auto_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_auto_label, lv_color_hex(SIREN_TEXT), 0);
    lv_obj_center(g_auto_label);

    /* Sequencer, paused until AUTO is tapped. */

    g_auto_timer = lv_timer_create(auto_step_cb, SEQ_STEP_MS, NULL);
    lv_timer_pause(g_auto_timer);
  }
}

static void tx_timer_cb(lv_timer_t * t)
{
  LV_UNUSED(t);
  lv_label_set_text_fmt(g_tx_label, "USB-MIDI tx %lu",
                        (unsigned long)g_tx);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;

#ifdef NEED_BOARDINIT
  boardctl(BOARDIOC_INIT, 0);
#endif

  lv_init();
  lv_nuttx_dsc_init(&info);

#ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#endif

#ifdef CONFIG_INPUT_TOUCHSCREEN
  info.input_path = "/dev/input0";
#endif

  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      printf("apollia_hub: display init failed\n");
      return 1;
    }

  g_midifd = open(MIDI_DEV_PATH, O_WRONLY | O_NONBLOCK);
  if (g_midifd < 0)
    {
      printf("apollia_hub: " MIDI_DEV_PATH " unavailable, UI-only mode\n");
    }

  hub_ui_create(result.disp);
  lv_timer_create(tx_timer_cb, 500, NULL);

  while (1)
    {
      uint32_t idle = lv_timer_handler();
      usleep((idle ? idle : 1) * 1000);
    }

  return 0;
}
