; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-partial-collapse-closure=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ONEPASS
;
; I-DSA closure of a GEP with a negative constant part and a symbolic index:
; p = (char*)a - 32 + 4*k, i.e. &a[k-8] (in bounds for 8 <= k < 16).  The
; negative constant re-bases a's node (a at 32) and visitGep drops the
; symbolic part (ONEPASS: p is the point 0, not aliasing a or q = &a[2]; plain
; SeaDSA answers the same).  The closure re-applies [lo + c, +oo] = [0,+oo]:
; the node collapses.
; CLOSED: MayAlias:\s+\[8 x i32\]\* %a, i32\* %p
; CLOSED: MayAlias:\s+i32\* %p, i32\* %q
; ONEPASS: NoAlias:\s+\[8 x i32\]\* %a, i32\* %p
; ONEPASS: NoAlias:\s+i32\* %p, i32\* %q

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

declare i64 @input()

define void @mix() {
entry:
  %a = alloca [8 x i32], align 16
  %q = getelementptr inbounds [8 x i32], [8 x i32]* %a, i64 0, i64 2
  %k = call i64 @input()
  %p = getelementptr inbounds [8 x i32], [8 x i32]* %a, i64 -1, i64 %k
  store i32 1, i32* %q, align 4
  store i32 2, i32* %p, align 4
  ret void
}
