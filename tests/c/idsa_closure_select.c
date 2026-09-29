// clang-14 idsa_closure_select.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o idsa_closure_select.ll
// opt-14 idsa_closure_select.ll -S -passes=mem2reg,simplifycfg,instnamer -o idsa_closure_select.ll
//
// I-DSA closure without a loop: p = &s->b or &s->c gives p the interval
// [4,8]; q = p + 2 then points to 12 or 16, but the one-pass construction
// shifts only p's raw offset (8) and puts q at 16.  The closure merges
// [12,16] (a point grows into an interval: no widening), so q aliases s->d
// (and s->d, s->e share a cell); p and q still do not alias.
extern int input(void);
struct R { int a; int b; int c; int d; int e; int f; };
void sel(struct R *s) {
  int *p = input() ? &s->b : &s->c;
  int *q = p + 2;
  *p = 0;
  *q = 1;
  s->d = 2;
  s->e = 3;
}
int main(void) {
  struct R r;
  sel(&r);
  return 0;
}
