/*--------------------------------------------------------------------*/
/*--- Herbgrind: a valgrind tool for Herbie          semantic-op.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Herbgrind, a valgrind tool for diagnosing
   floating point accuracy problems in binary programs and extracting
   problematic expressions.

   Copyright (C) 2016-2017 Alex Sanchez-Stern

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

#include "semantic-op.h"

#include "pub_tool_libcassert.h"
#include "pub_tool_machine.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_hashtable.h"

#include "../helper/instrument-util.h"
#include "../helper/debug.h"
#include "../helper/ir-info.h"

#include "../runtime/op-shadowstate/shadowop-info.h"
#include "../runtime/shadowop/shadowop.h"
#include "../runtime/value-shadowstate/value-shadowstate.h"

#include "instrument-storage.h"
#include "ownership.h"

VgHashTable* opInfoTable = NULL;

typedef struct _opInfoEntry {
  struct _opInfoEntry* next;
  UWord addr;
  IROp op_code;
  ShadowOpInfo* info;
} OpInfoEntry;

long int cmpOpInfoEntry(const void* node1, const void* node2);
long int cmpOpInfoEntry(const void* node1, const void* node2){
  const OpInfoEntry* entry1 = (const OpInfoEntry*)node1;
  const OpInfoEntry* entry2 = (const OpInfoEntry*)node2;
  if (entry1->addr == entry2->addr &&
      entry1->op_code == entry2->op_code){
    return 0;
  } else {
    return 1;
  }
}

void instrumentSemanticOp(IRSB* sbOut, IROp op_code,
                          int nargs, IRExpr** argExprs,
                          Addr curAddr, Addr blockAddr,
                          IRTemp dest){
  if (print_semantic_ops){
    ppIROp(op_code);
    VG_(printf)(" on ");
    for(int i = 0; i < nargs; ++i){
      ppIRExpr(argExprs[i]);
      VG_(printf)(", ");
    }
    VG_(printf)("\n");
    addPrintOp(op_code);
    addPrint("\n");
  }
  IRExpr* shadowOutput = runShadowOp(sbOut, mkU1(True),
                                     op_code,
                                     curAddr, blockAddr,
                                     nargs, argExprs,
                                     IRExpr_RdTmp(dest));
  addStoreTemp(sbOut, shadowOutput, argPrecision(op_code), dest);
}

IRExpr* runShadowOp(IRSB* sbOut, IRExpr* guard,
                    IROp op_code,
                    Addr curAddr, Addr block_addr,
                    int nargs, IRExpr** argExprs,
                    IRExpr* result){
  if (opInfoTable == NULL){
    opInfoTable = VG_(HT_construct)("Operation Info Table");
  }
  OpInfoEntry key = {.addr = curAddr, .op_code = op_code};
  OpInfoEntry* existingEntry = VG_(HT_gen_lookup)(opInfoTable, &key, cmpOpInfoEntry);
  ShadowOpInfo* info;
  if (existingEntry == NULL){
    info = mkShadowOpInfo(op_code, curAddr,
                          block_addr, nargs);
    OpInfoEntry* newEntry = VG_(malloc)("op info entry", sizeof(OpInfoEntry));
    newEntry->addr = curAddr;
    newEntry->op_code = op_code;
    newEntry->info = info;
    VG_(HT_add_node)(opInfoTable, newEntry);
  } else {
    info = existingEntry->info;
  }
  for(int i = 0; i < nargs; ++i){
    addStoreC(sbOut, argExprs[i],
              (uintptr_t)
              (info->exinfo.argPrecision == Ft_Single ?
               ((void*)computedArgs.argValuesF[i]) :
               ((void*)computedArgs.argValues[i])));
    if (argExprs[i]->tag == Iex_RdTmp){
      info->argTemps[i] = argExprs[i]->Iex.RdTmp.tmp;
      cleanupAtEndOfBlock(sbOut, argExprs[i]->Iex.RdTmp.tmp);
    } else {
      info->argTemps[i] = -1;
    }
  }
  addStoreC(sbOut, result, &computedResult);
  if (result->tag == Iex_RdTmp){
    cleanupAtEndOfBlock(sbOut, result->Iex.RdTmp.tmp);
  }
  IRTemp dest = newIRTemp(sbOut->tyenv, Ity_I64);
  IRDirty* dirty =
    unsafeIRDirty_1_N(dest, 1, "executeShadowOp",
                      VG_(fnptr_to_fnentry)(executeShadowOp),
                      mkIRExprVec_1(mkU64((uintptr_t)info)));
  dirty->mFx = Ifx_Read;
  dirty->mAddr = mkU64((uintptr_t)&computedArgs);
  dirty->mSize =
    sizeof(computedArgs)
    + sizeof(computedResult)
    + sizeof(shadowTemps);
  dirty->guard = guard;
  addStmtToIRSB(sbOut, IRStmt_Dirty(dirty));
  return IRExpr_RdTmp(dest);
}

void instrumentPossibleNegate(IRSB* sbOut,
                              IRExpr** argExprs, IRTemp dest,
                              Addr curAddr, Addr blockAddr){
  IRExpr* isSingleNegate =
    runAnd(sbOut,
           runBinop(sbOut, Iop_CmpEQ64,
                    runUnop(sbOut, Iop_V128to64,
                            argExprs[0]),
                    mkU64(0x80000000)),
           runBinop(sbOut, Iop_CmpEQ64,
                    runUnop(sbOut, Iop_V128HIto64,
                            argExprs[0]),
                    mkU64(0)));
  IRExpr* shadowSingleNegateOutput =
    runShadowOp(sbOut, isSingleNegate,
                IEop_Neg32F0x4,
                curAddr, blockAddr,
                1, argExprs + 1,
                IRExpr_RdTmp(dest));
  addStoreTempG(sbOut, isSingleNegate, shadowSingleNegateOutput,
                Ft_Single, dest);
  IRExpr* isDoubleNegate =
    runAnd(sbOut,
           runBinop(sbOut, Iop_CmpEQ64,
                    runUnop(sbOut, Iop_V128to64,
                            argExprs[0]),
                    mkU64(0x8000000000000000)),
           runBinop(sbOut, Iop_CmpEQ64,
                    runUnop(sbOut, Iop_V128HIto64,
                            argExprs[0]),
                    mkU64(0)));
  IRExpr* shadowDoubleNegateOutput =
    runShadowOp(sbOut, isDoubleNegate,
                IEop_Neg64F0x2,
                curAddr, blockAddr,
                1, argExprs + 1,
                IRExpr_RdTmp(dest));
  addStoreTempG(sbOut, isDoubleNegate, shadowDoubleNegateOutput,
                Ft_Double, dest);
}

ShadowOpInfo* getSemanticOpInfo(Addr callAddr, Addr block_addr, IROp op_code, int nargs){
  SemOpInfoEntry key = {.call_addr = callAddr, .op_code = op_code};
  SemOpInfoEntry* entry =
    VG_(HT_gen_lookup)(semanticOpInfoMap, &key, cmp_sem_entry_by_code);
  if (entry == NULL){
    ShadowOpInfo* callInfo =
      mkShadowOpInfo(op_code, callAddr, block_addr, nargs);
    entry = VG_(perm_malloc)(sizeof(SemOpInfoEntry),
                             vg_alignof(SemOpInfoEntry));
    entry->call_addr = callAddr;
    entry->info = callInfo;
    entry->op_code = op_code;
    VG_(HT_add_node)(mathreplaceOpInfoMap, entry);
  }
  return entry->info;
}

Word cmp_sem_entry_by_code(const void* node1, const void* node2){
  const SemOpInfoEntry* entry1 = (const SemOpInfoEntry*)node1;
  const SemOpInfoEntry* entry2 = (const SemOpInfoEntry*)node2;
  return !(entry1->op_code == entry2->op_code);
}
