// clang-14 idsa_closure_bu.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o idsa_closure_bu.ll
// opt-14 idsa_closure_bu.ll -S -passes=mem2reg,simplifycfg,instnamer -o idsa_closure_bu.ll
//
// I-DSA closure after cloning.  pick() gives s's node the interval [4,8]
// (&s->b or &s->c); main's local graph has q = p + 2 on a node without
// intervals.  The bottom-up step (butd-cs) and the argument unification of
// the global graph (ci) put p into [4,8] (at its raw offset 4), so q, which
// the one-pass construction put at 12 (&r.d), must cover [12,16]: q also
// aliases r.e.  use() is the top-down counterpart: there the interval comes
// from main.
extern int input(void);
struct R { int a; int b; int c; int d; int e; int f; };
void pick(struct R *s, int **out) { *out = input() ? &s->b : &s->c; }
void use(struct R *s, int *p) {
  int *q = p + 2;
  *q = 1;
  s->e = 2;
}
int main(void) {
  struct R r, t;
  int *p;
  pick(&r, &p);
  int *q = p + 2;
  *q = 1;
  r.e = 2;
  int *u = input() ? &t.b : &t.c;
  use(&t, u);
  return r.f + t.f;
}
