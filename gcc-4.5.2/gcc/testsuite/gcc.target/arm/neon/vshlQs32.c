/* Test the `vshlQs32' ARM Neon intrinsic.  */
/* This file was autogenerated by neon-testgen.  */

/* { dg-do assemble } */
/* { dg-require-effective-target arm_neon_ok } */
/* { dg-options "-save-temps -O0" } */
/* { dg-add-options arm_neon } */

#include "arm_neon.h"

void test_vshlQs32 (void)
{
  int32x4_t out_int32x4_t;
  int32x4_t arg0_int32x4_t;
  int32x4_t arg1_int32x4_t;

  out_int32x4_t = vshlq_s32 (arg0_int32x4_t, arg1_int32x4_t);
}

/* { dg-final { scan-assembler "vshl\.s32\[ 	\]+\[qQ\]\[0-9\]+, \[qQ\]\[0-9\]+, \[qQ\]\[0-9\]+!?\(\[ 	\]+@.*\)?\n" } } */
/* { dg-final { cleanup-saved-temps } } */
