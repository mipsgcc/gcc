/* Test the `vextQu64' ARM Neon intrinsic.  */
/* This file was autogenerated by neon-testgen.  */

/* { dg-do assemble } */
/* { dg-require-effective-target arm_neon_ok } */
/* { dg-options "-save-temps -O0" } */
/* { dg-add-options arm_neon } */

#include "arm_neon.h"

void test_vextQu64 (void)
{
  uint64x2_t out_uint64x2_t;
  uint64x2_t arg0_uint64x2_t;
  uint64x2_t arg1_uint64x2_t;

  out_uint64x2_t = vextq_u64 (arg0_uint64x2_t, arg1_uint64x2_t, 0);
}

/* { dg-final { scan-assembler "vext\.64\[ 	\]+\[qQ\]\[0-9\]+, \[qQ\]\[0-9\]+, \[qQ\]\[0-9\]+, #\[0-9\]+!?\(\[ 	\]+@.*\)?\n" } } */
/* { dg-final { cleanup-saved-temps } } */
