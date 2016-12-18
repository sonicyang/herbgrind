/*--------------------------------------------------------------------*/
/*--- HerbGrind: a valgrind tool for Herbie    value-shadowstate.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of HerbGrind, a valgrind tool for diagnosing
   floating point accuracy problems in binary programs and extracting
   problematic expressions.

   Copyright (C) 2016 Alex Sanchez-Stern

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 3 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

// Here are the data structures we set up to hold shadow values. They
// take three forms:
//
// * Values that are being worked on currently are held in temps by
//   VEX, so we have an array of shadow values to match each temp
//   register, up to a limit set in the .h file.
//
// * Values that persist between blocks (I think this is how it
//   works), are held in a per thread data structure by VEX, so we set
//   up another array for every thread to hold those, also up to a
//   limit set in the .h file.
//
// * Finally, values might be written to memory, and then read out
//   later at some arbitrary point. For these, we'll maintain a hash
//   table that maps addresses to shadow values, so we don't have to
//   maintain a vast array of shadow values for all of memory.

#ifndef _VALUE_SHADOWSTATE_H
#define _VALUE_SHADOWSTATE_H

#include "shadowval.h"
#include "exprs.h"
#include "pub_tool_tooliface.h"

#include "../../helper/stack.h"

#define MAX_TEMPS 1000
#define MAX_REGISTERS 1000
#define MAX_THREADS 16

extern ShadowTemp* shadowTemps[MAX_TEMPS];
extern ShadowValue* shadowThreadState[MAX_THREADS][MAX_REGISTERS];

#define MAX_TEMP_SHADOWS 4

extern Stack* freedTemps[MAX_TEMP_SHADOWS];
extern Stack* freedVals;

typedef struct _TempDebtEntry {
  IRTemp temp;
  int num_vals;
} TempDebtEntry;

void initValueShadowState(void);
VG_REGPARM(2) void dynamicCleanup(int nentries, TempDebtEntry* entries);
VG_REGPARM(1) void disownShadowTempNonNull(ShadowTemp* temp);
VG_REGPARM(1) void disownShadowTemp(ShadowTemp* temp);
VG_REGPARM(1) ShadowTemp* copyShadowTemp(ShadowTemp* temp);

ShadowTemp* mkShadowTemp(UWord num_vals);
void freeShadowTemp(ShadowTemp* temp);
void disownShadowValue(ShadowValue* val);
void ownShadowValue(ShadowValue* val);
void freeShadowValue(ShadowValue* val);
ShadowValue* mkShadowValue(FloatType type, double value);
void disownExpr(ConcExpr* expr);
void ownExpr(ConcExpr* expr);

VG_REGPARM(1) ShadowTemp* mkShadowTempOneDouble(double value);
VG_REGPARM(1) ShadowTemp* mkShadowTempTwoDoubles(double* values);
VG_REGPARM(1) ShadowTemp* mkShadowTempOneSingle(float value);
VG_REGPARM(1) ShadowTemp* mkShadowTempFourSingles(float* values);
VG_REGPARM(1) ShadowTemp* mkShadowTempFourSinglesG(UWord guard, float* values);
#endif