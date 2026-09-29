; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-partial-collapse-callee-sites=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=NOCARRY
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-partial-collapse-closure=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ONEPASS
;
; Callee arithmetic on a caller's interval, read through an intermediate
; function (butd-cs), see tests/c/idsa_closure_callee_read.c: in main,
; z = mid(p) (%i13) may be &y (%i3).  leaf's rule p + 2 is carried into mid
; and from mid into main (a rule carried into a graph is carried on).
; CLOSED: MayAlias:\s+i32\* %i13, i32\* %i3
; NOCARRY: NoAlias:\s+i32\* %i13, i32\* %i3
; ONEPASS: NoAlias:\s+i32\* %i13, i32\* %i3

; ModuleID = '/tmp/claude-1000/-home-ljgy-Works-seatools-clam/eacef5b5-4b86-4eb3-8f92-0a02bad86b1d/scratchpad/v2work/idsa_closure_callee_read.raw.ll'
source_filename = "c/idsa_closure_callee_read.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.R = type { i32*, i32*, i32*, i32*, i32* }

; Function Attrs: nounwind uwtable
define dso_local void @pick(%struct.R* noundef %arg, i32*** noundef %arg1) #0 {
bb:
  %i = call i32 @input()
  %i2 = icmp ne i32 %i, 0
  %i3 = getelementptr inbounds %struct.R, %struct.R* %arg, i32 0, i32 1
  %i4 = getelementptr inbounds %struct.R, %struct.R* %arg, i32 0, i32 2
  %i5 = select i1 %i2, i32** %i3, i32** %i4
  store i32** %i5, i32*** %arg1, align 8, !tbaa !5
  ret void
}

declare i32 @input() #1

; Function Attrs: nounwind uwtable
define dso_local i32* @leaf(i32** noundef %arg) #0 {
bb:
  %i = getelementptr inbounds i32*, i32** %arg, i64 2
  %i1 = load i32*, i32** %i, align 8, !tbaa !5
  ret i32* %i1
}

; Function Attrs: nounwind uwtable
define dso_local i32* @mid(i32** noundef %arg, i32* noundef %arg1) #0 {
bb:
  %i = call i32* @leaf(i32** noundef %arg)
  store i32 5, i32* %arg1, align 4, !tbaa !9
  ret i32* %i
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #2

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #2

; Function Attrs: nounwind uwtable
define dso_local i32 @main() #0 {
bb:
  %i = alloca %struct.R, align 8
  %i1 = alloca i32**, align 8
  %i2 = alloca i32, align 4
  %i3 = alloca i32, align 4
  %i4 = alloca i32, align 4
  %i5 = bitcast %struct.R* %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 40, i8* %i5) #3
  %i6 = bitcast i32*** %i1 to i8*
  call void @llvm.lifetime.start.p0i8(i64 8, i8* %i6) #3
  %i7 = bitcast i32* %i2 to i8*
  call void @llvm.lifetime.start.p0i8(i64 4, i8* %i7) #3
  store i32 1, i32* %i2, align 4, !tbaa !9
  %i8 = bitcast i32* %i3 to i8*
  call void @llvm.lifetime.start.p0i8(i64 4, i8* %i8) #3
  store i32 2, i32* %i3, align 4, !tbaa !9
  %i9 = bitcast i32* %i4 to i8*
  call void @llvm.lifetime.start.p0i8(i64 4, i8* %i9) #3
  store i32 0, i32* %i4, align 4, !tbaa !9
  %i10 = getelementptr inbounds %struct.R, %struct.R* %i, i32 0, i32 3
  store i32* %i2, i32** %i10, align 8, !tbaa !11
  %i11 = getelementptr inbounds %struct.R, %struct.R* %i, i32 0, i32 4
  store i32* %i3, i32** %i11, align 8, !tbaa !13
  call void @pick(%struct.R* noundef %i, i32*** noundef %i1)
  %i12 = load i32**, i32*** %i1, align 8, !tbaa !5
  %i13 = call i32* @mid(i32** noundef %i12, i32* noundef %i4)
  store i32 3, i32* %i13, align 4, !tbaa !9
  %i14 = load i32, i32* %i2, align 4, !tbaa !9
  %i15 = load i32, i32* %i3, align 4, !tbaa !9
  %i16 = add nsw i32 %i14, %i15
  %i17 = load i32, i32* %i4, align 4, !tbaa !9
  %i18 = add nsw i32 %i16, %i17
  %i19 = bitcast i32* %i4 to i8*
  call void @llvm.lifetime.end.p0i8(i64 4, i8* %i19) #3
  %i20 = bitcast i32* %i3 to i8*
  call void @llvm.lifetime.end.p0i8(i64 4, i8* %i20) #3
  %i21 = bitcast i32* %i2 to i8*
  call void @llvm.lifetime.end.p0i8(i64 4, i8* %i21) #3
  %i22 = bitcast i32*** %i1 to i8*
  call void @llvm.lifetime.end.p0i8(i64 8, i8* %i22) #3
  %i23 = bitcast %struct.R* %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 40, i8* %i23) #3
  ret i32 %i18
}

attributes #0 = { nounwind uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { argmemonly nofree nosync nounwind willreturn }
attributes #3 = { nounwind }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{!"Ubuntu clang version 14.0.0-1ubuntu1.1"}
!5 = !{!6, !6, i64 0}
!6 = !{!"any pointer", !7, i64 0}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C/C++ TBAA"}
!9 = !{!10, !10, i64 0}
!10 = !{!"int", !7, i64 0}
!11 = !{!12, !6, i64 24}
!12 = !{!"R", !6, i64 0, !6, i64 8, !6, i64 16, !6, i64 24, !6, i64 32}
!13 = !{!12, !6, i64 32}
