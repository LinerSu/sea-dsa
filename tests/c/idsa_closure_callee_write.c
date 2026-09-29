// clang-14 idsa_closure_callee_write.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o idsa_closure_callee_write.ll
// opt-14 idsa_closure_callee_write.ll -S -passes=mem2reg,simplifycfg,instnamer -o idsa_closure_callee_write.ll
//
// Callee arithmetic on a caller's interval (butd-cs; first review's bu_callee.c).
// pick() puts p in [8,16] of r (&r.b or &r.c); store_it() stores x at p + 2,
// i.e. r.d or r.e.  main then loads r.e: z may be &x (pick chose &r.c).
// Bottom-up cloning places store_it's store at the point 24 of r; only the
// callee rule p + 2, carried into main, widens it to [24,32].
extern int input(void);
struct R { int *a; int *b; int *c; int *d; int *e; };
void pick(struct R *s, int ***out) { *out = input() ? &s->b : &s->c; }
void store_it(int **p, int *x) { *(p + 2) = x; }
int main(void) {
  struct R r;
  int **p;
  int x = 1, y = 2;
  r.e = &y;
  pick(&r, &p);
  store_it(p, &x);
  int *z = r.e;
  *z = 3;
  return x + y;
}
