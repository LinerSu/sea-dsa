// clang-14 idsa_closure_loop.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o idsa_closure_loop.ll
// opt-14 idsa_closure_loop.ll -S -passes=mem2reg,simplifycfg,instnamer -o idsa_closure_loop.ll
//
// I-DSA closure under pointer arithmetic: p = p + 1 in a loop.  The one-pass
// construction joins the phi's offsets 0 and 4 into [0,4] and leaves
// q = &a[5] at 20, so p and q do not alias although p reaches q.  Re-applying
// p = p + 1 to [0,4] needs [4,8]; widening the interval [0,4] gives [0,+oo],
// i.e. the node is collapsed.
extern void sink(int);
int main(void) {
  int a[10];
  int *p = a;
  int *q = &a[5];
  for (int i = 0; i < 10; i++) {
    *p = i;
    p = p + 1;
  }
  *q = 42;
  sink(a[0]);
  return 0;
}
