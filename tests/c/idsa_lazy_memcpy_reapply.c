// clang-14 idsa_lazy_memcpy_reapply.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o idsa_lazy_memcpy_reapply.ll
// opt-14 idsa_lazy_memcpy_reapply.ll -S -passes=mem2reg,simplifycfg,instnamer -o idsa_lazy_memcpy_reapply.ll
//
// The local pass copies the links of a lazy memcpy once.  The closed I-DSA
// model re-applies every lazy copy in the closure, so links and merges that
// reach the source later reach the destination.
// g: p = &x.a or &x.b ([8,16]); q = p + 1 is &x.b or &x.c, which the one pass
//    puts at 16 (x.b) and the closure widens to [16,24]; *q = &C.  The copy of
//    x.c into y.h happens when x.c (24) is still a point without a link, so
//    the one-shot copy moves nothing, although x.c == &C when p == &x.b.
// order: y = x; x = z in a loop (two copies, the first one sees x.f only in
//    the second iteration): y.f may be &v.
#include <string.h>
extern int input(void);
extern unsigned long len(void);
extern void sink(int);
struct S { char *h, *a, *b, *c, *d; };
char C;
char *g(void) {
  struct S x, y;
  char **p = input() ? &x.a : &x.b;
  char **q = p + 1;
  *q = &C;
  memcpy(&y.h, &x.c, sizeof(char *) * (len() ? 1 : 1));
  char *r = y.h;
  *r = 1;
  return r;
}
struct P { int *f; long pad; };
void order(void) {
  int v = 1;
  struct P x, y, z;
  z.f = &v;
  for (int i = 0; i < 2; i++) {
    y = x;
    x = z;
  }
  sink(*y.f);
}
int main(void) { order(); return *g(); }
