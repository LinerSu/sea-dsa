// clang-14 idsa_embed_inside_interval.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o idsa_embed_inside_interval.ll
// opt-14 idsa_embed_inside_interval.ll -S -passes=mem2reg,simplifycfg,instnamer -o idsa_embed_inside_interval.ll
//
// Embedding a node at an offset strictly inside an interval cell of the
// target node.  `q` may address byte 0 or byte 7 of `o`, so the same-node
// cell unification gives o's node the interval [0,7].  f's node for `p`
// (struct Inner, field c at 8) is then unified with `&o.in`, i.e. embedded
// into o's node at raw offset 4, which lies strictly inside [0,7].  The
// embedding base must stay raw: in.c lands at 4+8 = 12 (canonical: outside
// every interval), not at interval.start+8 = 8.
extern int nd_int(void);
struct Inner { int a; int b; int c; };            // a@0 b@4 c@8
struct Outer { int x; struct Inner in; int y; };  // x@0 in@4 y@16
void f(struct Inner *p) { p->c = 1; }             // writes Outer byte 12
int main(void) {
  struct Outer o;
  o.y = 0;                              // grows o's node to 20 bytes first
  char *raw = (char *)&o;
  char *q = nd_int() ? raw : raw + 7;   // same-node unify (o,0)+(o,7)
  *q = 0;                               //   => interval [0,7]
  f(&o.in);
  return o.y;
}
