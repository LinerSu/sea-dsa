// clang-14 idsa_closure_sequence.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o idsa_closure_sequence.ll
// opt-14 idsa_closure_sequence.ll -S -passes=mem2reg,simplifycfg,instnamer -o idsa_closure_sequence.ll
//
// Sequence (array) nodes in the closed I-DSA model.  arr[i] makes arr's node
// a sequence node (stride 16); offsets are positions modulo the stride.
//  - within(): p = &arr[i].a or &arr[i].b puts two positions (0, 4) into one
//    cell; q = p + 2 is &arr[i].c or &arr[i].d and r = &arr[j].d, so q == r
//    when p == &arr[i].b and j == i.  The one-pass construction keeps [0,4]
//    on the sequence node, which the pointer-arithmetic closure skipped.
//  - wrap(): a = &arr[i].d (position 12), b = a + 2 (position 4, in
//    arr[i+1]); r = c ? a : b unifies positions 12 and 4.  Their interval
//    wraps around the stride and the one-pass construction drops it, so r and
//    b (and s = &arr[j].b) do not alias although r == b when c == 0.
// The closed model keeps sequence nodes free of intervals and collapses the
// node, as plain SeaDSA does for a misaligned unification.
extern int input(void);
struct S { int a, b, c, d; };
void within(struct S *arr, long i, long j) {
  int *p = input() ? &arr[i].a : &arr[i].b;
  int *q = p + 2;
  int *r = &arr[j].d;
  *q = 1;
  *r = 2;
}
void wrap(struct S *arr, long i, long j) {
  int *a = &arr[i].d;
  int *b = a + 2;
  int *r = input() ? a : b;
  int *s = &arr[j].b;
  *r = 1;
  *s = 2;
}
int main(void) {
  struct S s1[8], s2[8];
  within(s1, input(), input());
  wrap(s2, input(), input());
  return s1[0].a + s2[0].a;
}
