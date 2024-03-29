/* { dg-do compile } */
/* { dg-options "-O2 -mfix-r10000" } */
/* { dg-final { scan-assembler-times "\tbeql\t|\tldaddw\t|\tldaddd\t" 3 } } */

NOMIPS16 int
f1 (int *z)
{
  return __sync_add_and_fetch (z, 42);
}

NOMIPS16 short
f2 (short *z)
{
  return __sync_add_and_fetch (z, 42);
}

NOMIPS16 char
f3 (char *z)
{
  return __sync_add_and_fetch (z, 42);
}
