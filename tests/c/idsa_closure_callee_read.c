// clang-14 idsa_closure_callee_read.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o idsa_closure_callee_read.ll
// opt-14 idsa_closure_callee_read.ll -S -passes=mem2reg,simplifycfg,instnamer -o idsa_closure_callee_read.ll
//
// Callee arithmetic on a caller's interval, read effect through an
// intermediate function (butd-cs; first review's bu_read.c).  pick() puts p
// in [8,16] (&r.b or &r.c); leaf() returns *(p + 2), i.e. r.d or r.e; mid()
// forwards it.  z may be &y (pick chose &r.c).  The rule p + 2 of leaf is
// carried into mid and, from there, into main.
extern int input(void);
struct R { int *a; int *b; int *c; int *d; int *e; };
void pick(struct R *s, int ***out) { *out = input() ? &s->b : &s->c; }
int *leaf(int **p) { return *(p + 2); }
int *mid(int **p, int *w) { int *t = leaf(p); *w = 5; return t; }
int main(void) {
  struct R r;
  int **p;
  int x = 1, y = 2, w = 0;
  r.d = &x;
  r.e = &y;
  pick(&r, &p);
  int *z = mid(p, &w);
  *z = 3;
  return x + y + w;
}
