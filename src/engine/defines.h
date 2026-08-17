/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2026 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _DEFINES_H
#define _DEFINES_H

// global
#define DIV_MAX_CHIPS 32
#define DIV_MAX_CHANS 128
#define DIV_MAX_PATTERNS 256
#define DIV_MAX_CHIP_DEFS 256

// in-pattern
#define DIV_MAX_ROWS 256
#define DIV_MAX_COLS 32
#define DIV_MAX_EFFECTS 8

// pattern fields
#define DIV_PAT_NOTE 0
#define DIV_PAT_INS 1
#define DIV_PAT_VOL 2
#define DIV_PAT_FX(_x) (3+((_x)<<1))
#define DIV_PAT_FXVAL(_x) (4+((_x)<<1))

#define DIV_PAT_NOTE_BUFFER 27
#define DIV_PAT_RAW0 28
#define DIV_PAT_RAW1 29
#define DIV_PAT_RAW2 30
#define DIV_PAT_RAW3 31

// column type checks
#define DIV_PAT_IS_EFFECT(_x) ((_x)>DIV_PAT_VOL && ((_x)&1))
#define DIV_PAT_IS_EFFECT_VAL(_x) ((_x)>DIV_PAT_VOL && (!((_x)&1)))

// these sentinels live above the 465-slot 31-EDO note domain (0..464)
#define DIV_NOTE_RAW 507
#define DIV_NOTE_NULL_PAT 508
#define DIV_NOTE_OFF 509
#define DIV_NOTE_REL 510
#define DIV_MACRO_REL 511

// sample related
#define DIV_MAX_SAMPLE_TYPE 4

// dispatch
#define DIV_MAX_OUTPUTS 16
#define DIV_NOTE_NULL 0x7fffffff
#define DIV_NOTE_RAW_FLAG 0x80000000

#endif
