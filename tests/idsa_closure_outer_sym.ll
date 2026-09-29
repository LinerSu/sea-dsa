; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-partial-collapse-closure=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ONEPASS
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=SEADSA
;
; Array extent of a symbolic GEP whose pointer-level index is symbolic too
; (hand-written: instcombine merges p[i].buf[j] into one GEP; clang at -O1
; without it emits three GEPs):
;   struct S { int hdr; int buf[8]; };                  sizeof(S) = 36
;   a = &p[i].buf[j]  gep %S, %S* %p, i64 %i, i32 1, i64 %j  (offset 4, stride 4)
;   b = &p[1].buf[0]  offset 40: a == b for i = 1, j = 0
;   c = &p[2].hdr     offset 72
; The extent of buf (32 bytes) bounds j only, not i.  The one-pass model takes
; it anyway and bounds a by the interval [4,32] (ONEPASS: a and b do not
; alias).  The closed model uses an array extent only when no symbolic index
; precedes that array, so a gets [4,+oo] (CLOSED).  SeaDSA collapses the node.
; CLOSED: MayAlias:\s+i32\* %a, i32\* %b
; CLOSED: MayAlias:\s+i32\* %a, i32\* %c
; ONEPASS: NoAlias:\s+i32\* %a, i32\* %b
; ONEPASS: NoAlias:\s+i32\* %a, i32\* %c
; SEADSA: MayAlias:\s+i32\* %a, i32\* %b
; SEADSA: MayAlias:\s+i32\* %a, i32\* %c

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.S = type { i32, [8 x i32] }

declare i64 @input()

define void @g(%struct.S* %p) {
entry:
  %i = call i64 @input()
  %j = call i64 @input()
  %a = getelementptr inbounds %struct.S, %struct.S* %p, i64 %i, i32 1, i64 %j
  %b = getelementptr inbounds %struct.S, %struct.S* %p, i64 1, i32 1, i64 0
  %c = getelementptr inbounds %struct.S, %struct.S* %p, i64 2, i32 0
  store i32 1, i32* %a, align 4
  store i32 2, i32* %b, align 4
  store i32 3, i32* %c, align 4
  ret void
}

define i32 @main() {
entry:
  %s = alloca [4 x %struct.S], align 16
  %p = getelementptr inbounds [4 x %struct.S], [4 x %struct.S]* %s, i64 0, i64 0
  call void @g(%struct.S* %p)
  %h = getelementptr inbounds [4 x %struct.S], [4 x %struct.S]* %s, i64 0, i64 0, i32 0
  %v = load i32, i32* %h, align 4
  ret i32 %v
}
