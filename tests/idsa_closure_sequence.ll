; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-partial-collapse-closure=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ONEPASS
;
; Sequence (array) nodes in the closed I-DSA model (see
; tests/c/idsa_closure_sequence.c).  arr[i] makes arr's node a sequence node
; (stride 16), whose offsets are positions modulo the stride.
; within: p = %i8 = &arr[i].a or &arr[i].b; q = %i9 = p + 2; r = %i11 =
;   &arr[j].d.  q == r when p == &arr[i].b and j == i.  The one-pass
;   construction puts [0,4] on the sequence node and leaves q at the point 8
;   (ONEPASS: NoAlias q, r).
; wrap: a = %i3 = &arr[i].d (12), b = %i4 = a + 2 (position 4), r = %. = c ? a
;   : b, s = %i8 = &arr[j].b.  Positions 12 and 4 wrap around the stride and
;   the one-pass construction drops their interval (ONEPASS: NoAlias r, b,
;   although r == b when c == 0).
; The closed model keeps sequence nodes free of intervals and collapses the
; node instead, as plain SeaDSA does for a misaligned unification (CLOSED).
; CLOSED: Function: within
; CLOSED: MayAlias:\s+i32\* %i11, i32\* %i9
; CLOSED: alias queries: 36 \(NoAlias 0,
; CLOSED: Function: wrap
; CLOSED: MayAlias:\s+i32\* %\., i32\* %i4
; CLOSED: MayAlias:\s+i32\* %\., i32\* %i8
; CLOSED: alias queries: 21 \(NoAlias 0,
; ONEPASS: Function: within
; ONEPASS: NoAlias:\s+i32\* %i11, i32\* %i9
; ONEPASS: Function: wrap
; ONEPASS: NoAlias:\s+i32\* %\., i32\* %i4
; ONEPASS: NoAlias:\s+i32\* %\., i32\* %i8

; ModuleID = 'idsa_closure_sequence.ll'
source_filename = "c/idsa_closure_sequence.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.S = type { i32, i32, i32, i32 }

; Function Attrs: nounwind uwtable
define dso_local void @within(%struct.S* noundef %arg, i64 noundef %arg1, i64 noundef %arg2) #0 {
bb:
  %i = call i32 @input()
  %i3 = icmp ne i32 %i, 0
  %i4 = getelementptr inbounds %struct.S, %struct.S* %arg, i64 %arg1
  %i5 = getelementptr inbounds %struct.S, %struct.S* %i4, i32 0, i32 0
  %i6 = getelementptr inbounds %struct.S, %struct.S* %arg, i64 %arg1
  %i7 = getelementptr inbounds %struct.S, %struct.S* %i6, i32 0, i32 1
  %i8 = select i1 %i3, i32* %i5, i32* %i7
  %i9 = getelementptr inbounds i32, i32* %i8, i64 2
  %i10 = getelementptr inbounds %struct.S, %struct.S* %arg, i64 %arg2
  %i11 = getelementptr inbounds %struct.S, %struct.S* %i10, i32 0, i32 3
  store i32 1, i32* %i9, align 4, !tbaa !5
  store i32 2, i32* %i11, align 4, !tbaa !5
  ret void
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #1

declare i32 @input() #2

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #1

; Function Attrs: nounwind uwtable
define dso_local void @wrap(%struct.S* noundef %arg, i64 noundef %arg1, i64 noundef %arg2) #0 {
bb:
  %i = getelementptr inbounds %struct.S, %struct.S* %arg, i64 %arg1
  %i3 = getelementptr inbounds %struct.S, %struct.S* %i, i32 0, i32 3
  %i4 = getelementptr inbounds i32, i32* %i3, i64 2
  %i5 = call i32 @input()
  %i6 = icmp ne i32 %i5, 0
  %. = select i1 %i6, i32* %i3, i32* %i4
  %i7 = getelementptr inbounds %struct.S, %struct.S* %arg, i64 %arg2
  %i8 = getelementptr inbounds %struct.S, %struct.S* %i7, i32 0, i32 1
  store i32 1, i32* %., align 4, !tbaa !5
  store i32 2, i32* %i8, align 4, !tbaa !5
  ret void
}

; Function Attrs: nounwind uwtable
define dso_local i32 @main() #0 {
bb:
  %i = alloca [8 x %struct.S], align 16
  %i1 = alloca [8 x %struct.S], align 16
  %i2 = bitcast [8 x %struct.S]* %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 128, i8* %i2) #3
  %i3 = bitcast [8 x %struct.S]* %i1 to i8*
  call void @llvm.lifetime.start.p0i8(i64 128, i8* %i3) #3
  %i4 = getelementptr inbounds [8 x %struct.S], [8 x %struct.S]* %i, i64 0, i64 0
  %i5 = call i32 @input()
  %i6 = sext i32 %i5 to i64
  %i7 = call i32 @input()
  %i8 = sext i32 %i7 to i64
  call void @within(%struct.S* noundef %i4, i64 noundef %i6, i64 noundef %i8)
  %i9 = getelementptr inbounds [8 x %struct.S], [8 x %struct.S]* %i1, i64 0, i64 0
  %i10 = call i32 @input()
  %i11 = sext i32 %i10 to i64
  %i12 = call i32 @input()
  %i13 = sext i32 %i12 to i64
  call void @wrap(%struct.S* noundef %i9, i64 noundef %i11, i64 noundef %i13)
  %i14 = getelementptr inbounds [8 x %struct.S], [8 x %struct.S]* %i, i64 0, i64 0
  %i15 = getelementptr inbounds %struct.S, %struct.S* %i14, i32 0, i32 0
  %i16 = load i32, i32* %i15, align 16, !tbaa !9
  %i17 = getelementptr inbounds [8 x %struct.S], [8 x %struct.S]* %i1, i64 0, i64 0
  %i18 = getelementptr inbounds %struct.S, %struct.S* %i17, i32 0, i32 0
  %i19 = load i32, i32* %i18, align 16, !tbaa !9
  %i20 = add nsw i32 %i16, %i19
  %i21 = bitcast [8 x %struct.S]* %i1 to i8*
  call void @llvm.lifetime.end.p0i8(i64 128, i8* %i21) #3
  %i22 = bitcast [8 x %struct.S]* %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 128, i8* %i22) #3
  ret i32 %i20
}

attributes #0 = { nounwind uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { argmemonly nofree nosync nounwind willreturn }
attributes #2 = { "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nounwind }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{!"Ubuntu clang version 14.0.0-1ubuntu1.1"}
!5 = !{!6, !6, i64 0}
!6 = !{!"int", !7, i64 0}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C/C++ TBAA"}
!9 = !{!10, !6, i64 0}
!10 = !{!"S", !6, i64 0, !6, i64 4, !6, i64 8, !6, i64 12}
