// clang-14 idsa_closure_negative.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o idsa_closure_negative.ll
// opt-14 idsa_closure_negative.ll -S -passes=mem2reg,simplifycfg,instnamer -o idsa_closure_negative.ll
//
// I-DSA closure with a negative constant offset: e = s + n makes s's node an
// array node, e - 1 re-bases it (s at 1) and turns it into the interval
// [1,32], after e - 1 got the point 0.  Re-applying e - 1 to [1,32] needs
// [0,31], so e - 1 joins the interval: e - 1 aliases s (n == 1).
extern unsigned long input(void);
extern void sinkc(char);
void chomp(void) {
  char s[32];
  unsigned long n = input();
  char *e = s + n;
  if (e[-1] == '\n') e[-1] = 0;
  sinkc(s[0]);
}
int main(void) {
  chomp();
  return 0;
}
