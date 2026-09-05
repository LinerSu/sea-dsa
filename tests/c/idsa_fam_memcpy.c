// clang-14 idsa_fam_memcpy.c -S -emit-llvm -O1 -Xclang -disable-llvm-optzns -o idsa_fam_memcpy.ll
// opt-14 idsa_fam_memcpy.ll -S -passes=mem2reg,simplifycfg,instnamer -o idsa_fam_memcpy.ll
//
// curl's Curl_headers_push/namevalue shape: a calloc'd struct with a
// flexible array member, a memcpy of unknown length into it, pointers into
// the copied buffer stored back into the struct, and reads through those
// pointers.  namevalue() computes end = header + hlen - 1 and walks it
// backwards, which embeds its string node into the store's node at an
// offset strictly inside the buffer interval (Node::pointTo).
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
extern int nd_int(void);
extern void sink(const char *);
struct store {
  struct store *next;  // 0
  char *name;          // 8   points into buffer
  char *value;         // 16  points into buffer
  int amount;          // 24
  unsigned char type;  // 28
  char buffer[1];      // 29  raw header blob
};                     // sizeof == 32
static int namevalue(char *header, size_t hlen, char **name, char **value) {
  char *end = header + hlen - 1; /* point to the last byte */
  *name = header;
  while (*header && (*header != ':'))
    ++header;
  if (*header)
    *header++ = 0;
  else
    return 1;
  while (*header == ' ')
    header++;
  *value = header;
  while ((end > header) && *end == ' ')
    *end-- = 0;
  return 0;
}
static struct store *push(const char *header, size_t hlen) {
  char *name = NULL, *value = NULL;
  struct store *hs = calloc(1, sizeof(*hs) + hlen);
  memcpy(hs->buffer, header, hlen);
  hs->buffer[hlen] = 0;
  if (namevalue(hs->buffer, hlen, &name, &value)) {
    free(hs);
    return NULL;
  }
  hs->name = name;
  hs->value = value;
  hs->type = 1;
  hs->amount = (int)hlen;
  return hs;
}
int main(void) {
  char line[16] = "k: v  ";
  struct store *hs = push(line, (size_t)nd_int());
  if (!hs)
    return 1;
  sink(hs->name);
  sink(hs->value);
  return hs->amount + hs->type;
}
