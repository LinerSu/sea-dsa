; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-partial-collapse-closure=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ONEPASS
;
; Fields of first-class aggregates are closure sites (hand-written IR):
;   struct T { int *f0; int *f1; };  struct X { long pad; T t1; T t2; };
;   (t1 at 8, t2 at 24; t2.f1 at 32)
; f: p = c ? &x->t1 : &x->t2 gets [8,24]; s = *p (a first-class load: s lives
;    on p's cell); r = s.f1 (extractvalue).  The local pass reads the link of
;    the field cell raw(p) + 8 = 16, which lies in [8,24] (b's link), once.
;    r == &a when p == &x->t2 (x->t2.f1 = &a at 32).  The closure checks the
;    field as the constant site p + 8: [16,32] is not in [8,24], widened to
;    [8,+oo], which takes a's link: r aliases @a (CLOSED); ONEPASS does not.
; h: s2 = insertvalue s, @g, 1 writes the link of the field cell raw(p) + 8
;    (SeaDSA models the aggregate on the cell it was loaded from).  The closure
;    makes that link cover [16,32], so the load v of x->t2.f1 (32) reads it.
; CLOSED: MayAlias:\s+i32\* %r, i32\* @a
; CLOSED: MayAlias:\s+i32\* %v, i32\* @g
; ONEPASS: NoAlias:\s+i32\* %r, i32\* @a
; ONEPASS: NoAlias:\s+i32\* %v, i32\* @g

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.T = type { i32*, i32* }
%struct.X = type { i64, %struct.T, %struct.T }

@a = global i32 0, align 4
@b = global i32 0, align 4
@g = global i32 0, align 4

declare i32 @input()

define void @f(%struct.X* %x) {
entry:
  %t2f1 = getelementptr inbounds %struct.X, %struct.X* %x, i64 0, i32 2, i32 1
  store i32* @a, i32** %t2f1, align 8
  %t1f1 = getelementptr inbounds %struct.X, %struct.X* %x, i64 0, i32 1, i32 1
  store i32* @b, i32** %t1f1, align 8
  %c = call i32 @input()
  %tobool = icmp ne i32 %c, 0
  %t1 = getelementptr inbounds %struct.X, %struct.X* %x, i64 0, i32 1
  %t2 = getelementptr inbounds %struct.X, %struct.X* %x, i64 0, i32 2
  %p = select i1 %tobool, %struct.T* %t1, %struct.T* %t2
  %s = load %struct.T, %struct.T* %p, align 8
  %r = extractvalue %struct.T %s, 1
  store i32 1, i32* %r, align 4
  ret void
}

define %struct.T @h(%struct.X* %x) {
entry:
  %c = call i32 @input()
  %tobool = icmp ne i32 %c, 0
  %t1 = getelementptr inbounds %struct.X, %struct.X* %x, i64 0, i32 1
  %t2 = getelementptr inbounds %struct.X, %struct.X* %x, i64 0, i32 2
  %p = select i1 %tobool, %struct.T* %t1, %struct.T* %t2
  %s = load %struct.T, %struct.T* %p, align 8
  %s2 = insertvalue %struct.T %s, i32* @g, 1
  %t2f1 = getelementptr inbounds %struct.X, %struct.X* %x, i64 0, i32 2, i32 1
  %v = load i32*, i32** %t2f1, align 8
  store i32 2, i32* %v, align 4
  ret %struct.T %s2
}

define i32 @main() {
entry:
  %x = alloca %struct.X, align 8
  %y = alloca %struct.X, align 8
  call void @f(%struct.X* %x)
  %r = call %struct.T @h(%struct.X* %y)
  %v = load i32, i32* @a, align 4
  ret i32 %v
}
