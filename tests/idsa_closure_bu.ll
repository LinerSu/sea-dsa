; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-partial-collapse-closure=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ONEPASS
;
; I-DSA closure after cloning (see tests/c/idsa_closure_bu.c).  The local
; graphs of main and use have no interval; the interval [4,8] comes from
; pick() through the bottom-up step into main (butd-cs), from main through the
; top-down step into use (butd-cs), and through the argument unification of
; the global graph (ci).  In both functions q = p + 2 was put at 12 by the
; one-pass construction and must cover [12,16], i.e. alias the field e at 16:
; use: q = %i, s->e = %i2; main: q = %i7, r.e = %i8.
; CLOSED: Function: use
; CLOSED: MayAlias:\s+i32\* %i, i32\* %i2
; CLOSED: Function: main
; CLOSED: MayAlias:\s+i32\* %i7, i32\* %i8
; ONEPASS: Function: use
; ONEPASS: NoAlias:\s+i32\* %i, i32\* %i2
; ONEPASS: Function: main
; ONEPASS: NoAlias:\s+i32\* %i7, i32\* %i8

; ModuleID = 'idsa_closure_bu.ll'
source_filename = "c/idsa_closure_bu.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.R = type { i32, i32, i32, i32, i32, i32 }

; Function Attrs: nounwind uwtable
define dso_local void @pick(%struct.R* noundef %arg, i32** noundef %arg1) #0 {
bb:
  %i = call i32 @input()
  %i2 = icmp ne i32 %i, 0
  %i3 = getelementptr inbounds %struct.R, %struct.R* %arg, i32 0, i32 1
  %i4 = getelementptr inbounds %struct.R, %struct.R* %arg, i32 0, i32 2
  %i5 = select i1 %i2, i32* %i3, i32* %i4
  store i32* %i5, i32** %arg1, align 8, !tbaa !5
  ret void
}

declare i32 @input() #1

; Function Attrs: nounwind uwtable
define dso_local void @use(%struct.R* noundef %arg, i32* noundef %arg1) #0 {
bb:
  %i = getelementptr inbounds i32, i32* %arg1, i64 2
  store i32 1, i32* %i, align 4, !tbaa !9
  %i2 = getelementptr inbounds %struct.R, %struct.R* %arg, i32 0, i32 4
  store i32 2, i32* %i2, align 4, !tbaa !11
  ret void
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #2

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #2

; Function Attrs: nounwind uwtable
define dso_local i32 @main() #0 {
bb:
  %i = alloca %struct.R, align 4
  %i1 = alloca %struct.R, align 4
  %i2 = alloca i32*, align 8
  %i3 = bitcast %struct.R* %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 24, i8* %i3) #3
  %i4 = bitcast %struct.R* %i1 to i8*
  call void @llvm.lifetime.start.p0i8(i64 24, i8* %i4) #3
  %i5 = bitcast i32** %i2 to i8*
  call void @llvm.lifetime.start.p0i8(i64 8, i8* %i5) #3
  call void @pick(%struct.R* noundef %i, i32** noundef %i2)
  %i6 = load i32*, i32** %i2, align 8, !tbaa !5
  %i7 = getelementptr inbounds i32, i32* %i6, i64 2
  store i32 1, i32* %i7, align 4, !tbaa !9
  %i8 = getelementptr inbounds %struct.R, %struct.R* %i, i32 0, i32 4
  store i32 2, i32* %i8, align 4, !tbaa !11
  %i9 = call i32 @input()
  %i10 = icmp ne i32 %i9, 0
  %i11 = getelementptr inbounds %struct.R, %struct.R* %i1, i32 0, i32 1
  %i12 = getelementptr inbounds %struct.R, %struct.R* %i1, i32 0, i32 2
  %i13 = select i1 %i10, i32* %i11, i32* %i12
  call void @use(%struct.R* noundef %i1, i32* noundef %i13)
  %i14 = getelementptr inbounds %struct.R, %struct.R* %i, i32 0, i32 5
  %i15 = load i32, i32* %i14, align 4, !tbaa !13
  %i16 = getelementptr inbounds %struct.R, %struct.R* %i1, i32 0, i32 5
  %i17 = load i32, i32* %i16, align 4, !tbaa !13
  %i18 = add nsw i32 %i15, %i17
  %i19 = bitcast i32** %i2 to i8*
  call void @llvm.lifetime.end.p0i8(i64 8, i8* %i19) #3
  %i20 = bitcast %struct.R* %i1 to i8*
  call void @llvm.lifetime.end.p0i8(i64 24, i8* %i20) #3
  %i21 = bitcast %struct.R* %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 24, i8* %i21) #3
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
!11 = !{!12, !10, i64 16}
!12 = !{!"R", !10, i64 0, !10, i64 4, !10, i64 8, !10, i64 12, !10, i64 16, !10, i64 20}
!13 = !{!12, !10, i64 20}
