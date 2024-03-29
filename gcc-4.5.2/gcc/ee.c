/* Redundant extension elimination 
   Copyright (C) 2010 Free Software Foundation, Inc.
   Contributed by Tom de Vries (tom@codesourcery.com)

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/*

  MOTIVATING EXAMPLE

  The motivating example for this pass is:

    void f(unsigned char *p, short s, int c, int *z)
    {
      if (c)
        *z = 0;
      *p ^= (unsigned char)s;
    }

  For MIPS, compilation results in the following insns.

    (set (reg/v:SI 199)
         (sign_extend:SI (subreg:HI (reg:SI 200) 2)))

    ...

    (set (reg:QI 203)
         (subreg:QI (reg/v:SI 199) 3))

  These insns are the only def and the only use of reg 199, each located in a
  different bb.

  The sign-extension preserves the lower half of reg 200 and copies them to 
  reg 199, and the subreg use of reg 199 only reads the least significant byte.
  The sign extension is therefore redundant (the extension part, not the copy 
  part), and can safely be replaced with a regcopy from reg 200 to reg 199.


  OTHER SIGN/ZERO EXTENSION ELIMINATION PASSES

  There are other passes which eliminate sign/zero-extension: combine and
  implicit_zee. Both attempt to eliminate extensions by combining them with
  other instructions. The combine pass does this at bb level,
  implicit_zee works at inter-bb level.

  The combine pass combine an extension with either:
  - all uses of the extension, or 
  - all defs of the operand of the extension.
  The implicit_zee pass only implements the latter.

  For our motivating example, combine doesn't work since the def and the use of
  reg 199 are in a different bb.

  Implicit_zee does not work since it only combines an extension with the defs
  of its operand.


  INTENDED EFFECT

  This pass works by removing sign/zero-extensions, or replacing them with
  regcopies. The idea there is that the regcopy might be eliminated by a later
  pass. In case the regcopy cannot be eliminated, it might at least be cheaper
  than the extension.


  IMPLEMENTATION

  The pass scans twice over all instructions.

  The first scan registers all uses of a reg in the biggest_use array. After
  that first scan, the biggest_use array contains the size in bits of the
  biggest use of each reg.

  The second scan finds extensions, determines whether they are redundant based
  on the biggest use, and deletes or replaces them.

  In case that the src and dest reg of the replacement are not of the same size,
  we do not replace with a normal regcopy, but with a truncate or with the copy
  of a paradoxical subreg instead.


  LIMITATIONS

  The scope of the analysis is limited to an extension and its uses. The other
  type of analysis (related to the defs of the operand of an extension) is not
  done.

  Furthermore, we do the analysis of biggest use per reg. So when determining
  whether an extension is redundant, we take all uses of a the dest reg into
  account, also the ones that are not uses of the extension. This could be
  overcome by calculating the def-use chains and using those for analysis
  instead.

  Finally, during the analysis each insn is looked at in isolation. There is no
  propagation of information during the analysis.  To overcome this limitation,
  a backward iterative bit-level liveness analysis is needed.  */


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "flags.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "insn-config.h"
#include "function.h"
#include "expr.h"
#include "insn-attr.h"
#include "recog.h"
#include "toplev.h"
#include "target.h"
#include "timevar.h"
#include "optabs.h"
#include "insn-codes.h"
#include "rtlhooks-def.h"
#include "output.h"
#include "params.h"
#include "timevar.h"
#include "tree-pass.h"
#include "cgraph.h"

#define SKIP_REG (-1)

/* Array to register the biggest use of a reg, in bits.  */

static int *biggest_use;

/* Array to register the promoted subregs.  */

static VEC (rtx,heap) **promoted_subreg;

/* Forward declaration.  */

static void note_use (rtx *x, void *data);

/* The following two functions are borrowed from trunk/gcc/toplev.c. They can be
   removed for a check-in into gcc trunk.  */

/* Given X, an unsigned number, return the number of least significant bits
   that are zero.  When X == 0, the result is the word size.  */

static int
ctz_hwi (unsigned HOST_WIDE_INT x)
{
  return x ? floor_log2 (x & -x) : HOST_BITS_PER_WIDE_INT;
}

/* Similarly for most significant bits.  */

static int
clz_hwi (unsigned HOST_WIDE_INT x)
{
  return HOST_BITS_PER_WIDE_INT - 1 - floor_log2(x);
}

/* Check whether this is a paradoxical subreg.  */

static bool
paradoxical_subreg_p (rtx subreg)
{
  enum machine_mode subreg_mode, reg_mode;

  if (GET_CODE (subreg) != SUBREG)
    return false;

  subreg_mode = GET_MODE (subreg);
  reg_mode = GET_MODE (SUBREG_REG (subreg));

  if (GET_MODE_SIZE (subreg_mode) > GET_MODE_SIZE (reg_mode))
    return true;

  return false;
}

/* Check whether this is a promoted subreg.  */

static bool
promoted_subreg_p (rtx subreg)
{
  if (GET_CODE (subreg) != SUBREG)
    return false;

  if (SUBREG_PROMOTED_VAR_P (subreg))
    return true;

  return false;
}

/* Check whether this is a promoted subreg for which we cannot reset the
   promotion.  */

static bool
fixed_promoted_subreg_p (rtx subreg)
{
  if (!promoted_subreg_p (subreg))
    return false;

  if (targetm.mode_rep_extended (GET_MODE (subreg),
                                 GET_MODE (SUBREG_REG (subreg))) != UNKNOWN)
    return true;

  return false;
}

/* Get the size and reg number of a REG or SUBREG use.  */

static bool
reg_use_p (rtx use, int *size, unsigned int *regno)
{
  rtx reg;
      
  if (REG_P (use))
    {
      *regno = REGNO (use);
      *size = GET_MODE_BITSIZE (GET_MODE (use));
      return true;
    }
  else if (GET_CODE (use) == SUBREG)
    {
      reg = SUBREG_REG (use);

      if (!REG_P (reg))
        return false;

      *regno = REGNO (reg);

      if (paradoxical_subreg_p (use) || fixed_promoted_subreg_p (use))
        *size = GET_MODE_BITSIZE (GET_MODE (reg));
      else
        *size = subreg_lsb (use) + GET_MODE_BITSIZE (GET_MODE (use));

      return true;
    }

  return false;
}

/* Register the use of a reg.  */

static void
register_use (int size, unsigned int regno)
{
  int *current = &biggest_use[regno];

  if (*current == SKIP_REG)
    return;
  
  *current = MAX (*current, size);
}

/* Handle embedded uses.  */

static void
note_embedded_uses (rtx use, rtx pattern)
{
  const char *format_ptr;
  int i, j;

  format_ptr = GET_RTX_FORMAT (GET_CODE (use));
  for (i = 0; i < GET_RTX_LENGTH (GET_CODE (use)); i++)
    if (format_ptr[i] == 'e')
      note_use (&XEXP (use, i), pattern);
    else if (format_ptr[i] == 'E')
      for (j = 0; j < XVECLEN (use, i); j++)
        note_use (&XVECEXP (use, i, j), pattern);
}

/* Get the set that has use as its SRC operand.  */

static rtx
get_set (rtx use, rtx pattern)
{
  rtx sub;
  int i;

  if (GET_CODE (pattern) == SET && SET_SRC (pattern) == use)
    return pattern;

  if (GET_CODE (pattern) == PARALLEL)
    for (i = 0; i < XVECLEN (pattern, 0); ++i)
      {
        sub = XVECEXP (pattern, 0, i);
        if (GET_CODE (sub) == SET && SET_SRC (sub) == use)
          return sub;
      }
  
  return NULL_RTX;
}

/* Handle a restricted op use. In this context restricted means that a bit in an
   operand influences only the same bit or more significant bits in the result.
   The bitwise ops are a subclass, but PLUS is one as well.  */

static void
note_restricted_op_use (rtx use, unsigned int nr_operands, rtx pattern)
{
  unsigned int i, smallest;
  int operand_size[2];
  int used_size;
  unsigned int operand_regno[2];
  bool operand_reg[2];
  bool operand_ignore[2];
  rtx set;

  /* Init operand_reg, operand_size, operand_regno and operand_ignore.  */
  for (i = 0; i < nr_operands; ++i)
    {
      operand_reg[i] = reg_use_p (XEXP (use, i), &operand_size[i],
                                  &operand_regno[i]);
      operand_ignore[i] = false;
    }

  /* Handle case of reg and-masked with const.  */
  if (GET_CODE (use) == AND && CONST_INT_P (XEXP (use, 1)) && operand_reg[0])
    {
      used_size =
        HOST_BITS_PER_WIDE_INT - clz_hwi (UINTVAL (XEXP (use, 1)));
      operand_size[0] = MIN (operand_size[0], used_size);
    }

  /* Handle case of reg or-masked with const.  */
  if (GET_CODE (use) == IOR && CONST_INT_P (XEXP (use, 1)) && operand_reg[0])
    {
      used_size =
        HOST_BITS_PER_WIDE_INT - clz_hwi (~UINTVAL (XEXP (use, 1)));
      operand_size[0] = MIN (operand_size[0], used_size);
    }

  /* Ignore the use of a in 'a = a + b'.  */
  set = get_set (use, pattern);
  if (set != NULL_RTX && REG_P (SET_DEST (set)))
    for (i = 0; i < nr_operands; ++i)
      operand_ignore[i] = (operand_reg[i]
                           && (REGNO (SET_DEST (set)) == operand_regno[i]));

  /* Handle the case a reg is combined with don't care bits.  */
  if (nr_operands == 2 && operand_reg[0] && operand_reg[1]
      && operand_size[0] != operand_size[1])
    {
      smallest = operand_size[0] > operand_size[1];

      if (paradoxical_subreg_p (XEXP (use, smallest)))
        operand_size[1 - smallest] = operand_size[smallest];
    }

  /* Register the operand use, if necessary.  */
  for (i = 0; i < nr_operands; ++i)
    if (!operand_reg[i])
      note_use (&XEXP (use, i), pattern);
    else if (!operand_ignore[i])
      register_use (operand_size[i], operand_regno[i]);
}

/* Register promoted SUBREG in promoted_subreg.  */

static void
register_promoted_subreg (rtx subreg)
{
  int index = REGNO (SUBREG_REG (subreg));

  if (promoted_subreg[index] == NULL)
    promoted_subreg[index] = VEC_alloc (rtx, heap, 10);

  VEC_safe_push (rtx, heap, promoted_subreg[index], subreg);
}

/* Note promoted subregs in X.  */

static int
note_promoted_subreg (rtx *x, void *y ATTRIBUTE_UNUSED)
{
  rtx subreg = *x;

  if (promoted_subreg_p (subreg) && !fixed_promoted_subreg_p (subreg)
      && REG_P (SUBREG_REG (subreg)))
    register_promoted_subreg (subreg);

  return 0;
}

/* Handle all uses noted by note_uses.  */

static void
note_use (rtx *x, void *data)
{
  rtx use = *x;
  rtx pattern = (rtx)data;
  int use_size;
  unsigned int use_regno;

  for_each_rtx (x, note_promoted_subreg, NULL);

  switch (GET_CODE (use))
    {
    case REG:
    case SUBREG:
      if (!reg_use_p (use, &use_size, &use_regno))
        {
          note_embedded_uses (use, pattern);
          return;
        }
      register_use (use_size, use_regno);
      return;
    case IOR:
    case AND:
    case XOR:
    case PLUS:
    case MINUS:
      note_restricted_op_use (use, 2, pattern);
      return;
    case NOT:
    case NEG:
      note_restricted_op_use (use, 1, pattern);
      return;
    case ASHIFT:
      if (!reg_use_p (XEXP (use, 0), &use_size, &use_regno)
	  || !CONST_INT_P (XEXP (use, 1))
          || INTVAL (XEXP (use, 1)) <= 0
          || paradoxical_subreg_p (XEXP (use, 0)))
        {
          note_embedded_uses (use, pattern);
          return;
        }
      register_use (use_size - INTVAL (XEXP (use, 1)), use_regno);
      return;
    default:
      note_embedded_uses (use, pattern);
      return;
    }
}

/* Check whether reg is implicitly used.  */

static bool
implicit_use_p (int regno ATTRIBUTE_UNUSED)
{
#ifdef EPILOGUE_USES
  if (EPILOGUE_USES (regno))
    return true;
#endif

#ifdef EH_USES
  if (EH_USES (regno))
    return true;
#endif

  return false;
}

/* Note the uses of argument registers in a call.  */

static void
note_call_uses (rtx insn)
{
  rtx link, link_expr;

  if (!CALL_P (insn))
    return;

  for (link = CALL_INSN_FUNCTION_USAGE (insn); link; link = XEXP (link, 1))
    {
      link_expr = XEXP (link, 0);

      if (GET_CODE (link_expr) == USE)
        note_use (&XEXP (link_expr, 0), link);
    }
}

/* Calculate the biggest use mode for all regs. */

static void
calculate_biggest_use (void)
{
  int i;
  basic_block bb;
  rtx insn;

  /* Initialize biggest_use for all regs to 0. If a reg is used implicitly, we
     handle that reg conservatively and set it to SKIP_REG instead.  */
  for (i = 0; i < max_reg_num (); i++)
    biggest_use[i] = ((implicit_use_p (i) || HARD_REGISTER_NUM_P (i))
                      ? SKIP_REG : 0);

  /* For all insns, call note_use for each use in insn.  */
  FOR_EACH_BB (bb)
    FOR_BB_INSNS (bb, insn)
      {
        if (!NONDEBUG_INSN_P (insn))
          continue;

        note_uses (&PATTERN (insn), note_use, PATTERN (insn));

        if (CALL_P (insn))
          note_call_uses (insn);
      }

  /* Dump the biggest uses found.  */
  if (dump_file)
    for (i = 0; i < max_reg_num (); i++)
      if (biggest_use[i] > 0)
        fprintf (dump_file, "reg %d: size %d\n", i, biggest_use[i]);
}

/* Check whether this is a sign/zero extension.  */

static bool
extension_p (rtx insn, rtx *dest, rtx *inner, int *preserved_size)
{
  rtx src, op0;

  /* Detect set of reg.  */
  if (GET_CODE (PATTERN (insn)) != SET)
    return false;

  src = SET_SRC (PATTERN (insn));
  *dest = SET_DEST (PATTERN (insn));
          
  if (!REG_P (*dest))
    return false;

  /* Detect sign or zero extension.  */
  if (GET_CODE (src) == ZERO_EXTEND || GET_CODE (src) == SIGN_EXTEND
      || (GET_CODE (src) == AND && CONST_INT_P (XEXP (src, 1))))
    {
      op0 = XEXP (src, 0);

      /* Determine amount of least significant bits preserved by operation.  */
      if (GET_CODE (src) == AND)
        *preserved_size = ctz_hwi (~UINTVAL (XEXP (src, 1)));
      else
        *preserved_size = GET_MODE_BITSIZE (GET_MODE (op0));

      if (GET_CODE (op0) == SUBREG)
        {
          if (subreg_lsb (op0) != 0)
            return false;
      
          *inner = SUBREG_REG (op0);
          return true;
        }
      else if (REG_P (op0))
        {
          *inner = op0;
          return true;
        }
      else if (GET_CODE (op0) == TRUNCATE)
        {
          *inner = XEXP (op0, 0);
          return true;
        }
    }

  return false;
}

/* Check whether this is a redundant sign/zero extension.  */

static bool
redundant_extension_p (rtx insn, rtx *dest, rtx *inner)
{
  int biggest_dest_use;
  int preserved_size;

  if (!extension_p (insn, dest, inner, &preserved_size))
    return false;

  if (dump_file)
    fprintf (dump_file, "considering extension %u with preserved size %d\n", 
             INSN_UID (insn), preserved_size);

  biggest_dest_use = biggest_use[REGNO (*dest)];
      
  if (biggest_dest_use == SKIP_REG)
    return false;

  if (preserved_size < biggest_dest_use)
    return false;

  if (dump_file)
    fprintf (dump_file, "found superfluous extension %u\n", INSN_UID (insn));

  return true;
}

/* Reset promotion of subregs or REG.  */

static void
reset_promoted_subreg (rtx reg)
{
  int ix;
  rtx subreg;

  if (promoted_subreg[REGNO (reg)] == NULL)
    return;

  FOR_EACH_VEC_ELT (rtx, promoted_subreg[REGNO (reg)], ix, subreg)
    {
      SUBREG_PROMOTED_UNSIGNED_SET (subreg, 0);
      SUBREG_PROMOTED_VAR_P (subreg) = 0;
    }

  VEC_free (rtx, heap, promoted_subreg[REGNO (reg)]);
}

/* Try to remove or replace the redundant extension.  */

static void
try_remove_or_replace_extension (rtx insn, rtx dest, rtx inner)
{
  rtx cp_src, cp_dest, seq, one;

  if (GET_MODE_CLASS (GET_MODE (dest)) != GET_MODE_CLASS (GET_MODE (inner)))
    return;

  /* Check whether replacement is needed.  */
  if (dest != inner)
    {
      start_sequence ();

      /* Determine the proper replacement operation.  */
      if (GET_MODE (dest) == GET_MODE (inner))
        {
          cp_src = inner;
          cp_dest = dest;
        }
      else if (GET_MODE_SIZE (GET_MODE (dest))
               > GET_MODE_SIZE (GET_MODE (inner)))
        {
          emit_clobber (dest);
          cp_src = inner;
          cp_dest = gen_lowpart_SUBREG (GET_MODE (inner), dest);
        }
      else 
        {
          cp_src = gen_rtx_TRUNCATE (GET_MODE (dest), inner);
          cp_dest = dest;
        }

      emit_move_insn (cp_dest, cp_src);

      seq = get_insns ();
      end_sequence ();

      /* If the replacement is not supported, bail out.  */
      for (one = seq; one != NULL_RTX; one = NEXT_INSN (one))
        if (recog_memoized (one) < 0 && GET_CODE (PATTERN (one)) != CLOBBER)
          return;

      /* Insert the replacement.  */
      emit_insn_before (seq, insn);
    }

  /* Note replacement/removal in the dump.  */
  if (dump_file)
    {
      fprintf (dump_file, "superfluous extension %u ", INSN_UID (insn));
      if (dest != inner)
        fprintf (dump_file, "replaced by %u\n", INSN_UID (seq));
      else
        fprintf (dump_file, "removed\n");
    }

  /* Remove the extension.  */
  delete_insn (insn);  

  reset_promoted_subreg (dest);
}

/* Find redundant extensions and remove or replace them if possible.  */

static void
remove_redundant_extensions (void)
{
  basic_block bb;
  rtx insn, next, dest, inner;
  int i;

  biggest_use = XNEWVEC (int, max_reg_num ());
  promoted_subreg = XCNEWVEC (VEC (rtx,heap) *, max_reg_num ());
  calculate_biggest_use ();

  /* Remove redundant extensions.  */
  FOR_EACH_BB (bb)
    FOR_BB_INSNS_SAFE (bb, insn, next)
      {
        if (!NONDEBUG_INSN_P (insn))
          continue;

        if (!redundant_extension_p (insn, &dest, &inner))
          continue;

        try_remove_or_replace_extension (insn, dest, inner);
      }

  XDELETEVEC (biggest_use);
  for (i = 0; i < max_reg_num (); ++i)
    if (promoted_subreg[i] != NULL)
      VEC_free (rtx, heap, promoted_subreg[i]);
  XDELETEVEC (promoted_subreg);
}

/* Remove redundant extensions.  */

static unsigned int
rest_of_handle_ee (void)
{
  remove_redundant_extensions ();
  return 0;
}

/* Run ee pass when flag_ee is set at optimization level > 0.  */

static bool
gate_handle_ee (void)
{
  return (optimize > 0 && flag_ee);
}

struct rtl_opt_pass pass_ee =
{
 {
  RTL_PASS,
  "ee",                                 /* name */
  gate_handle_ee,                       /* gate */
  rest_of_handle_ee,                    /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_EE,                                /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_ggc_collect |
  TODO_dump_func |
  TODO_verify_rtl_sharing,              /* todo_flags_finish */
 }
};
