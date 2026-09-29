; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-partial-collapse-closure=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ONEPASS
;
; I-DSA closure under pointer arithmetic, loop (see tests/c/idsa_closure_loop.c):
; p (%.01, %i8 = p + 1) walks a; q = &a[5] is %i4.  The one-pass construction
; joins the phi's offsets 0 and 4 into [0,4] and leaves q at 20 (ONEPASS:
; NoAlias although p reaches q).  Re-applying p + 1 to [0,4] needs [4,8];
; widening the interval [0,4] gives [0,+oo], i.e. the node collapses.
; CLOSED: MayAlias:\s+i32\* %\.01, i32\* %i4
; CLOSED: MayAlias:\s+i32\* %i4, i32\* %i8
; CLOSED: alias queries: 28 \(NoAlias 0,
; ONEPASS: NoAlias:\s+i32\* %\.01, i32\* %i4
; ONEPASS: NoAlias:\s+i32\* %i4, i32\* %i8

; ModuleID = 'idsa_closure_loop.ll'
source_filename = "c/idsa_closure_loop.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: nounwind uwtable
define dso_local i32 @main() #0 {
bb:
  %i = alloca [10 x i32], align 16
  %i2 = bitcast [10 x i32]* %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 40, i8* %i2) #3
  %i3 = getelementptr inbounds [10 x i32], [10 x i32]* %i, i64 0, i64 0
  %i4 = getelementptr inbounds [10 x i32], [10 x i32]* %i, i64 0, i64 5
  br label %bb5

bb5:                                              ; preds = %bb7, %bb
  %.01 = phi i32* [ %i3, %bb ], [ %i8, %bb7 ]
  %.0 = phi i32 [ 0, %bb ], [ %i9, %bb7 ]
  %i6 = icmp slt i32 %.0, 10
  br i1 %i6, label %bb7, label %bb10

bb7:                                              ; preds = %bb5
  store i32 %.0, i32* %.01, align 4, !tbaa !5
  %i8 = getelementptr inbounds i32, i32* %.01, i64 1
  %i9 = add nsw i32 %.0, 1
  br label %bb5, !llvm.loop !9

bb10:                                             ; preds = %bb5
  store i32 42, i32* %i4, align 4, !tbaa !5
  %i11 = getelementptr inbounds [10 x i32], [10 x i32]* %i, i64 0, i64 0
  %i12 = load i32, i32* %i11, align 16, !tbaa !5
  call void @sink(i32 noundef %i12)
  %i13 = bitcast [10 x i32]* %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 40, i8* %i13) #3
  ret i32 0
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #1

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #1

declare void @sink(i32 noundef) #2

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
!9 = distinct !{!9, !10, !11}
!10 = !{!"llvm.loop.mustprogress"}
!11 = !{!"llvm.loop.unroll.disable"}
