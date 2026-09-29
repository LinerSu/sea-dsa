// clang-14 idsa_lazy_memcpy_oneshot.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o idsa_lazy_memcpy_oneshot.ll
// opt-14 idsa_lazy_memcpy_oneshot.ll -S -passes=mem2reg,simplifycfg,instnamer -o idsa_lazy_memcpy_oneshot.ll
//
// Lazy memcpy of a whole struct of variable length.  p = k ? &src.h : &src.a
// joins 0 and 8 (I-DSA: [0,8]; SeaDSA: the node collapses) and *(p + 2) = &C
// writes src.b or src.c.  memcpy(&dst, &src, n) then copies src.b into dst.b:
// r = dst.b may be &B or &C.  SeaDSA keys all links of the collapsed source at
// 0 and the key rule copies them to dst + 0 only (r reads nothing); selecting
// links by their cells, the collapsed source unifies with the destination.
#include <string.h>
extern int input(void);
extern unsigned long len(void);
struct S { char *h, *a, *b, *c; };
char B, C;
char *f(void) {
  struct S src, dst;
  src.b = &B;
  src.c = &C;
  char **p = input() ? &src.h : &src.a;
  char **q = p + 2;
  *q = &C;
  memcpy(&dst, &src, len());
  char *r = dst.b;
  *r = 1;
  return r;
}
int main(void) { return *f(); }
