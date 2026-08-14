/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2025 tildearrow and contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

// 31-EDO fork: shared constants and helpers for the 31-equal-divisions-of-the-
// octave reinterpretation of the 180-slot note space.
//
// slot = 31*(octave-2) + step, step in [0,30]. octave digits run 2..7.
// slot 62 = middle C ("C-4"). slot 85 = A-4 = song.tuning Hz.
// freq(slot) = tuning * 2^((slot-85)/31).
//
// spelling is chain-of-fifths meantone: 7 naturals, 7 sharps, 7 flats,
// 5 double sharps (Cx Dx Fx Gx Ax) and 5 double flats (Dbb Ebb Gbb Abb Bbb).
// the octave digit follows the LETTER (scientific pitch), so Cb (step 29)
// takes the digit of the C above it; B# (step 30) keeps its own chunk's digit.

#ifndef _EDO31_H
#define _EDO31_H

#include <cmath>

#define DIV_EDO31_STEPS 31
#define DIV_EDO31_MIDDLE_C 62
#define DIV_EDO31_A4 85
#define DIV_EDO31_A_STEP (DIV_EDO31_A4%DIV_EDO31_STEPS)
// one octave in 8.7 fixed-point units (31*128)
#define DIV_EDO31_OCTAVE_LINEAR 3968

enum DivEDO31Accidental {
  DIV_EDO31_NATURAL=0,
  DIV_EDO31_SHARP,
  DIV_EDO31_FLAT,
  DIV_EDO31_DSHARP,
  DIV_EDO31_DFLAT
};

// canonical step names, index = step from C
static const char* const edo31Names[31]={
  "C",   "Dbb", "C#",  "Db",  "Cx",  "D",   "Ebb", "D#",
  "Eb",  "Dx",  "E",   "Fb",  "E#",  "F",   "Gbb", "F#",
  "Gb",  "Fx",  "G",   "Abb", "G#",  "Ab",  "Gx",  "A",
  "Bbb", "A#",  "Bb",  "Ax",  "B",   "Cb",  "B#"
};

static const unsigned char edo31Class[31]={
  DIV_EDO31_NATURAL, DIV_EDO31_DFLAT,   DIV_EDO31_SHARP,   DIV_EDO31_FLAT,
  DIV_EDO31_DSHARP,  DIV_EDO31_NATURAL, DIV_EDO31_DFLAT,   DIV_EDO31_SHARP,
  DIV_EDO31_FLAT,    DIV_EDO31_DSHARP,  DIV_EDO31_NATURAL, DIV_EDO31_FLAT,
  DIV_EDO31_SHARP,   DIV_EDO31_NATURAL, DIV_EDO31_DFLAT,   DIV_EDO31_SHARP,
  DIV_EDO31_FLAT,    DIV_EDO31_DSHARP,  DIV_EDO31_NATURAL, DIV_EDO31_DFLAT,
  DIV_EDO31_SHARP,   DIV_EDO31_FLAT,    DIV_EDO31_DSHARP,  DIV_EDO31_NATURAL,
  DIV_EDO31_DFLAT,   DIV_EDO31_SHARP,   DIV_EDO31_FLAT,    DIV_EDO31_DSHARP,
  DIV_EDO31_NATURAL, DIV_EDO31_FLAT,    DIV_EDO31_SHARP
};

// Conventional 12-EDO semitone for each spelling above middle C. B# reaches
// the C above rather than wrapping to zero, matching scientific pitch names.
static const unsigned char edo31TwelveEDOSemitones[31]={
  0, 0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 5, 6,
  6, 7, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 11, 12
};

// Frequency of a reference spelling in octave 4 when A-4 is the tuning.
static inline double edo31ReferenceFrequency(double tuning, int step) {
  return tuning*std::pow(2.0,(double)(step-DIV_EDO31_A_STEP)/(double)DIV_EDO31_STEPS);
}

// Conventional 12-EDO frequency of a reference spelling at A-4 = 440 Hz.
static inline double edo31StandardReferenceFrequency(int step) {
  return 440.0*std::pow(
    2.0,
    (double)((int)edo31TwelveEDOSemitones[step]-9)/12.0
  );
}

// Convert a spelled reference frequency back to Furnace's stored A-4 anchor.
static inline double edo31TuningFromReferenceFrequency(double frequency, int step) {
  return frequency*std::pow(2.0,(double)(DIV_EDO31_A_STEP-step)/(double)DIV_EDO31_STEPS);
}

// Choose another exact spelling while preserving the current concert-pitch
// ratio. At standard pitch this pins the new spelling to its 12-EDO frequency.
static inline double edo31RetuneForReference(double tuning, int oldStep, int newStep) {
  double ratio=edo31ReferenceFrequency(tuning,oldStep)/edo31StandardReferenceFrequency(oldStep);
  return edo31TuningFromReferenceFrequency(
    edo31StandardReferenceFrequency(newStep)*ratio,
    newStep
  );
}

// octave digit shown for a slot (2..7, or 8 for the top Cb edge case if it
// ever comes into range; slot 179 is Bbb-7 so it does not today)
static inline int edo31Octave(int slot) {
  int oct=(slot/31)+2;
  if ((slot%31)==29) oct++; // Cb belongs to the octave of the C above it
  return oct;
}

// writes the fixed 4-byte pattern-cell representation (plus NUL) into out[5]:
// naturals "C-4 ", two-char names "C#4 ", three-char names "Dbb4".
static inline void edo31FormatNote(int slot, char* out) {
  const char* n=edo31Names[slot%31];
  int oct=edo31Octave(slot);
  if (n[1]==0) { // natural
    out[0]=n[0]; out[1]='-'; out[2]='0'+oct; out[3]=' ';
  } else if (n[2]==0) { // single accidental or x
    out[0]=n[0]; out[1]=n[1]; out[2]='0'+oct; out[3]=' ';
  } else { // double flat
    out[0]=n[0]; out[1]=n[1]; out[2]=n[2]; out[3]='0'+oct;
  }
  out[4]=0;
}

// MIDI convention (LatticeKeys interop): MIDI note number is a 31-EDO step
// index with middle C at MIDI 60.
static inline int edo31MidiToSlot(int midiNote) {
  int slot=midiNote+(DIV_EDO31_MIDDLE_C-60);
  if (slot<0) slot=0;
  if (slot>179) slot=179;
  return slot;
}

static inline int edo31SlotToMidi(int slot) {
  int midiNote=slot-(DIV_EDO31_MIDDLE_C-60);
  if (midiNote<0) midiNote=0;
  if (midiNote>127) midiNote=127;
  return midiNote;
}

#endif
