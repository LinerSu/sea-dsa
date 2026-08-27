// clang-14 aa_sizes.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o aa_sizes.ll
// opt-14 aa_sizes.ll -S -passes=mem2reg,simplifycfg,instnamer -o aa_sizes.ll
// (then the field/select pointers were renamed pid/psize/pbuf/p/q by hand)
//
// Alias-analysis regression: access-size- and interval-aware mayAlias, and
// queries involving globals.
#include <stdlib.h>
struct rb { int id; int size; int used; char buf[]; };
extern int nd_int(void);
extern char nd_char(void);
int a, b, c;
int main(void) {
  int n = nd_int();
  if (n <= 0) return 0;
  struct rb *rb = (struct rb *)malloc(sizeof(struct rb) + n);
  int *pid = &rb->id;
  int *psize = &rb->size;
  char *pbuf = &rb->buf[n - 1];
  *pid = 1; *psize = n; *pbuf = nd_char();
  int *p = nd_int() ? &a : &b;
  int *q = &c;
  *p = 1; *q = 2;
  return *pid + a + c;
}
