#include <stdint.h>

#include "pub_tool_basics.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_tooliface.h"

#include "mpfr.h"

#include "hg_mathreplace.h"
#include "hg_evaluate.h"
#include "hg_runtime.h"

void performOp(OpType op, double* result, double* args){
  size_t nargs;
  switch(op){
  case OP_SQRT:
  case OP_EXP:
  case OP_LOG:
  case OP_COS:
  case OP_SIN:
  case OP_TAN:
  case OP_ASIN:
  case OP_ACOS:
  case OP_ATAN:
  case OP_SINH:
  case OP_COSH:
  case OP_TANH:
  case OP_ABS:
  case OP_EXPM1:
  case OP_LOG1P:
    nargs = 1;
    break;
  case OP_MOD:
  case OP_POW:
  case OP_ATAN2:
  case OP_HYPOT:
    nargs = 2;
    break;
  }
  // We'll need the argument and the result in 64-bit mpfr, and
  // also shadow locations for both. We do the normal calculation
  // in MPFR instead of natively because we can't call the math
  // library here, and where we can it's being wrapped so doing so
  // would result in an infinite loop.
  mpfr_t *args_m, res;
  ShadowLocation **arg_shadows, *res_shadow;
  const HChar* plain_opname;

  // Initialize our 64-bit mpfr arg and shadow, and get the result
  // shadow set up.
  args_m = VG_(malloc)("wrapped-args", nargs * sizeof(double));
  arg_shadows = VG_(malloc)("wrapped-shadow", nargs * sizeof(ShadowLocation*));
  for (size_t i = 0; i < nargs; ++i){
    mpfr_init2(args_m[i], 64);
    // Get the actual value from the pointer they gave us.
    mpfr_set_d(args_m[i], args[i], MPFR_RNDN);
    // Lookup the address in our shadow hash table to get the
    // shadow argument.
    arg_shadows[0] = getShadowLocMem((uintptr_t)&(args[i]), args[i]);
  }
  mpfr_init2(res,64);
  res_shadow = mkShadowLocation(Lt_Double);

  switch(op){
  case OP_SQRT:
  case OP_EXP:
  case OP_LOG:
  case OP_COS:
  case OP_SIN:
  case OP_TAN:
  case OP_ASIN:
  case OP_ACOS:
  case OP_ATAN:
  case OP_SINH:
  case OP_COSH:
  case OP_TANH:
  case OP_ABS:
  case OP_EXPM1:
  case OP_LOG1P:
    {
      int (*mpfr_func)(mpfr_t, mpfr_srcptr, mpfr_rnd_t);
      switch(op){
      case OP_SQRT:
        plain_opname = "square root";
        mpfr_func = mpfr_sqrt;
        break;
      case OP_EXP:
        plain_opname = "exponentiate";
        mpfr_func = mpfr_exp;
        break;
      case OP_LOG:
        plain_opname = "log";
        mpfr_func = mpfr_log;
        break;
      case OP_COS:
        plain_opname = "cosine";
        mpfr_func = mpfr_cos;
        break;
      case OP_SIN:
        plain_opname = "sine";
        mpfr_func = mpfr_sin;
        break;
      case OP_TAN:
        plain_opname = "tangent";
        mpfr_func = mpfr_tan;
        break;
      case OP_ASIN:
        plain_opname = "arcsine";
        mpfr_func = mpfr_asin;
        break;
      case OP_ACOS:
        plain_opname = "arccosine";
        mpfr_func = mpfr_acos;
        break;
      case OP_ATAN:
        plain_opname = "arctangent";
        mpfr_func = mpfr_atan;
        break;
      case OP_SINH:
        plain_opname = "hyperbolic sine";
        mpfr_func = mpfr_sinh;
        break;
      case OP_COSH:
        plain_opname = "hyperbolic cosine";
        mpfr_func = mpfr_cosh;
        break;
      case OP_TANH:
        plain_opname = "hyperbolic tangent";
        mpfr_func = mpfr_tanh;
        break;
      case OP_ABS:
        plain_opname = "absolute";
        mpfr_func = mpfr_abs;
        break;
      case OP_EXPM1:
        plain_opname = "exponentiate minus one";
        mpfr_func = mpfr_expm1;
        break;
      case OP_LOG1P:
        plain_opname = "plus 1 log";
        mpfr_func = mpfr_log1p;
        break;
      default:
        return;
      }
      // Perform the operation on both regular and shadow values.
      mpfr_func(res, args_m[0], MPFR_RNDN);
      mpfr_func(res_shadow->values[0].value, arg_shadows[0]->values[0].value, MPFR_RNDN);
    }
    break;
  case OP_MOD:
  case OP_POW:
  case OP_ATAN2:
  case OP_HYPOT:
    {
      int (*mpfr_func)(mpfr_t, mpfr_srcptr, mpfr_srcptr, mpfr_rnd_t);
      switch(op){
      case OP_MOD:
        plain_opname = "modulus";
        mpfr_func = mpfr_fmod;
        break;
      case OP_POW:
        plain_opname = "power";
        mpfr_func = mpfr_pow;
        break;
      case OP_ATAN2:
        plain_opname = "arctangent (two arguments)";
        mpfr_func = mpfr_atan2;
        break;
      case OP_HYPOT:
        plain_opname = "hypotenuse";
        mpfr_func = mpfr_hypot;
        break;
      default:
        return;
      }
      // Perform the operation on both regular and shadow values.
      mpfr_func(res, args_m[0], args_m[1], MPFR_RNDN);
      mpfr_func(res_shadow->values[0].value, arg_shadows[0]->values[0].value, arg_shadows[0]->values[0].value, MPFR_RNDN);
    }
    break;
  }

  // Set both the regular and shadow results using the pointer we
  // were given to the result memory.
  *result = mpfr_get_d(res, MPFR_RNDN);
  setMem((uintptr_t)result, res_shadow);

  // Now, let's get the debug information on the call to this wrapped
  // library call, so we can report error to the user.
  OpDebug_Info debuginfo;
  getOpDebug_Info(last_abi_addr, plain_opname, &debuginfo);
  // And finally, evaluate the error of the operation.
  evaluateOpError(&(res_shadow->values[0]), *result, &debuginfo);
}

ShadowLocation* getShadowLocMem(Addr addr, double float_arg){
  ShadowLocation* loc = getMem(addr);
  if (loc != NULL) return loc;

  loc = mkShadowLocation(Lt_Double);
  setMem(addr, loc);

  mpfr_set_d(loc->values[0].value, float_arg, MPFR_RNDN);
  return loc;
}
