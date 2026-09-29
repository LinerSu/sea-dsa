// clang-14 idsa_lazy_memcpy_interval.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o idsa_lazy_memcpy_interval.ll
// opt-14 idsa_lazy_memcpy_interval.ll -S -passes=mem2reg,simplifycfg,instnamer -o idsa_lazy_memcpy_interval.ll
//
// Lazy memcpy (--sea-dsa-lazy-mem-transfer) from a source that lies inside an
// interval (I-DSA) or on a collapsed node (SeaDSA).  x->f1 = &a puts a link at
// 8; p = c ? &x->f2 : &x->f1 joins 8 and 16 (I-DSA: [8,16], link keyed at 8;
// SeaDSA: the node collapses, link keyed at 0), while p's raw offset stays 16.
// Copying the links keyed in [raw(p), raw(p) + 8) copies nothing, although
// c == 0 copies &a into y->f1.  Selecting links by their cells, the source
// window has no unique placement: the copy unifies source and destination.
#include <string.h>
struct X { long pad; int *f1; int *f2; int *f3; }; /* 0, 8, 16, 24 */
int a;
extern int input(void);
int *f(struct X *x, struct X *y) {
  x->f1 = &a;
  int **p = input() ? &x->f2 : &x->f1;
  memcpy(&y->f1, p, sizeof(int *));
  int *r = y->f1;
  *r = 1;
  return r;
}
int main(void) { struct X x, y; int *r = f(&x, &y); return *r; }
