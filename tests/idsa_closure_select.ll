; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-partial-collapse-closure=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ONEPASS
;
; I-DSA closure without a loop (see tests/c/idsa_closure_select.c):
; p = %i4 = &s->b (%i2) or &s->c (%i3) gets the interval [4,8]; q = %i5 = p + 2
; points to s->d (%i6, 12) or s->e (%i7, 16), but the one-pass construction
; shifts p's raw offset only (q at 12: ONEPASS misses q/s->e).  The closure
; merges [12,16], a point growing into an interval (no widening): q aliases
; s->e, and s->d, s->e share a cell; p, s->b, s->c still do not alias q or s->d.
; CLOSED: NoAlias:\s+i32\* %i4, i32\* %i5
; CLOSED: NoAlias:\s+i32\* %i2, i32\* %i6
; CLOSED: MayAlias:\s+i32\* %i5, i32\* %i6
; CLOSED: MayAlias:\s+i32\* %i5, i32\* %i7
; CLOSED: MayAlias:\s+i32\* %i6, i32\* %i7
; ONEPASS: NoAlias:\s+i32\* %i5, i32\* %i7
; ONEPASS: NoAlias:\s+i32\* %i6, i32\* %i7

; ModuleID = 'idsa_closure_select.ll'
source_filename = "c/idsa_closure_select.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.R = type { i32, i32, i32, i32, i32, i32 }

; Function Attrs: nounwind uwtable
define dso_local void @sel(%struct.R* noundef %arg) #0 {
bb:
  %i = call i32 @input()
  %i1 = icmp ne i32 %i, 0
  %i2 = getelementptr inbounds %struct.R, %struct.R* %arg, i32 0, i32 1
  %i3 = getelementptr inbounds %struct.R, %struct.R* %arg, i32 0, i32 2
  %i4 = select i1 %i1, i32* %i2, i32* %i3
  %i5 = getelementptr inbounds i32, i32* %i4, i64 2
  store i32 0, i32* %i4, align 4, !tbaa !5
  store i32 1, i32* %i5, align 4, !tbaa !5
  %i6 = getelementptr inbounds %struct.R, %struct.R* %arg, i32 0, i32 3
  store i32 2, i32* %i6, align 4, !tbaa !9
  %i7 = getelementptr inbounds %struct.R, %struct.R* %arg, i32 0, i32 4
  store i32 3, i32* %i7, align 4, !tbaa !11
  ret void
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #1

declare i32 @input() #2

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #1

; Function Attrs: nounwind uwtable
define dso_local i32 @main() #0 {
bb:
  %i = alloca %struct.R, align 4
  %i1 = bitcast %struct.R* %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 24, i8* %i1) #3
  call void @sel(%struct.R* noundef %i)
  %i2 = bitcast %struct.R* %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 24, i8* %i2) #3
  ret i32 0
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
!9 = !{!10, !6, i64 12}
!10 = !{!"R", !6, i64 0, !6, i64 4, !6, i64 8, !6, i64 12, !6, i64 16, !6, i64 20}
!11 = !{!10, !6, i64 16}
