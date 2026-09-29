; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-partial-collapse-closure=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ONEPASS
;
; I-DSA closure with a negative constant offset (see
; tests/c/idsa_closure_negative.c): e = s + n (%i4) makes s's node an array
; node; e - 1 (%i5) re-bases it (s at 1) and turns it into the interval
; [1,+oo] after e - 1 got the point 0 (ONEPASS: e - 1 does not alias s = %i3).
; Re-applying e - 1 to [1,+oo] needs [0,+oo]: the node collapses.
; CLOSED: MayAlias:\s+i8\* %i3, i8\* %i5
; CLOSED: alias queries: 28 \(NoAlias 0,
; ONEPASS: NoAlias:\s+i8\* %i3, i8\* %i5

; ModuleID = 'idsa_closure_negative.ll'
source_filename = "c/idsa_closure_negative.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: nounwind uwtable
define dso_local void @chomp() #0 {
bb:
  %i = alloca [32 x i8], align 16
  %i1 = bitcast [32 x i8]* %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 32, i8* %i1) #3
  %i2 = call i64 @input()
  %i3 = getelementptr inbounds [32 x i8], [32 x i8]* %i, i64 0, i64 0
  %i4 = getelementptr inbounds i8, i8* %i3, i64 %i2
  %i5 = getelementptr inbounds i8, i8* %i4, i64 -1
  %i6 = load i8, i8* %i5, align 1, !tbaa !5
  %i7 = sext i8 %i6 to i32
  %i8 = icmp eq i32 %i7, 10
  br i1 %i8, label %bb9, label %bb11

bb9:                                              ; preds = %bb
  %i10 = getelementptr inbounds i8, i8* %i4, i64 -1
  store i8 0, i8* %i10, align 1, !tbaa !5
  br label %bb11

bb11:                                             ; preds = %bb9, %bb
  %i12 = getelementptr inbounds [32 x i8], [32 x i8]* %i, i64 0, i64 0
  %i13 = load i8, i8* %i12, align 16, !tbaa !5
  call void @sinkc(i8 noundef signext %i13)
  %i14 = bitcast [32 x i8]* %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 32, i8* %i14) #3
  ret void
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #1

declare i64 @input() #2

declare void @sinkc(i8 noundef signext) #2

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #1

; Function Attrs: nounwind uwtable
define dso_local i32 @main() #0 {
bb:
  call void @chomp()
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
!6 = !{!"omnipotent char", !7, i64 0}
!7 = !{!"Simple C/C++ TBAA"}
