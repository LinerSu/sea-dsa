; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-partial-collapse --sea-dsa-partial-collapse-closure=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ONEPASS
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=SEADSA
;
; The closed I-DSA model re-applies the lazy copies in the closure (see
; tests/c/idsa_lazy_memcpy_reapply.c).
; g: the source x.c (24) is a point without a link when the local pass copies;
;    the closure then widens q = p + 1 to [16,24], which holds &C: y.h (%i21)
;    may be &C.  Re-applied, the copy's source lies in [16,24] (no unique
;    placement) and source and destination are unified.
; order: y = x; x = z in a loop: the first copy runs before x.f has a link,
;    so the one-shot copy misses y.f == &v (%i19, %i).
; The one-pass model (ONEPASS) and SeaDSA copy once.
; CLOSED: MayAlias:\s+i8\* %i21, i8\* @C
; CLOSED: MayAlias:\s+i32\* %i, i32\* %i19
; ONEPASS: NoAlias:\s+i8\* %i21, i8\* @C
; ONEPASS: NoAlias:\s+i32\* %i, i32\* %i19
; SEADSA: NoAlias:\s+i32\* %i, i32\* %i19

; ModuleID = '/tmp/claude-1000/-home-ljgy-Works-seatools-clam/eacef5b5-4b86-4eb3-8f92-0a02bad86b1d/scratchpad/v2work/idsa_lazy_memcpy_reapply.raw.ll'
source_filename = "c/idsa_lazy_memcpy_reapply.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.S = type { i8*, i8*, i8*, i8*, i8* }
%struct.P = type { i32*, i64 }

@C = dso_local global i8 0, align 1

; Function Attrs: nounwind uwtable
define dso_local i8* @g() #0 {
bb:
  %i = alloca %struct.S, align 8
  %i1 = alloca %struct.S, align 8
  %i2 = bitcast %struct.S* %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 40, i8* %i2) #4
  %i3 = bitcast %struct.S* %i1 to i8*
  call void @llvm.lifetime.start.p0i8(i64 40, i8* %i3) #4
  %i4 = call i32 @input()
  %i5 = icmp ne i32 %i4, 0
  %i6 = getelementptr inbounds %struct.S, %struct.S* %i, i32 0, i32 1
  %i7 = getelementptr inbounds %struct.S, %struct.S* %i, i32 0, i32 2
  %i8 = select i1 %i5, i8** %i6, i8** %i7
  %i9 = getelementptr inbounds i8*, i8** %i8, i64 1
  store i8* @C, i8** %i9, align 8, !tbaa !5
  %i10 = getelementptr inbounds %struct.S, %struct.S* %i1, i32 0, i32 0
  %i11 = bitcast i8** %i10 to i8*
  %i12 = getelementptr inbounds %struct.S, %struct.S* %i, i32 0, i32 3
  %i13 = bitcast i8** %i12 to i8*
  %i14 = call i64 @len()
  %i15 = icmp ne i64 %i14, 0
  %i16 = zext i1 %i15 to i64
  %i17 = select i1 %i15, i32 1, i32 1
  %i18 = sext i32 %i17 to i64
  %i19 = mul i64 8, %i18
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 8 %i11, i8* align 8 %i13, i64 %i19, i1 false)
  %i20 = getelementptr inbounds %struct.S, %struct.S* %i1, i32 0, i32 0
  %i21 = load i8*, i8** %i20, align 8, !tbaa !9
  store i8 1, i8* %i21, align 1, !tbaa !11
  %i22 = bitcast %struct.S* %i1 to i8*
  call void @llvm.lifetime.end.p0i8(i64 40, i8* %i22) #4
  %i23 = bitcast %struct.S* %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 40, i8* %i23) #4
  ret i8* %i21
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #1

declare i32 @input() #2

declare i64 @len() #2

; Function Attrs: argmemonly nofree nounwind willreturn
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* noalias nocapture writeonly, i8* noalias nocapture readonly, i64, i1 immarg) #3

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #1

; Function Attrs: nounwind uwtable
define dso_local void @order() #0 {
bb:
  %i = alloca i32, align 4
  %i1 = alloca %struct.P, align 8
  %i2 = alloca %struct.P, align 8
  %i3 = alloca %struct.P, align 8
  %i4 = bitcast i32* %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 4, i8* %i4) #4
  store i32 1, i32* %i, align 4, !tbaa !12
  %i5 = bitcast %struct.P* %i1 to i8*
  call void @llvm.lifetime.start.p0i8(i64 16, i8* %i5) #4
  %i6 = bitcast %struct.P* %i2 to i8*
  call void @llvm.lifetime.start.p0i8(i64 16, i8* %i6) #4
  %i7 = bitcast %struct.P* %i3 to i8*
  call void @llvm.lifetime.start.p0i8(i64 16, i8* %i7) #4
  %i8 = getelementptr inbounds %struct.P, %struct.P* %i3, i32 0, i32 0
  store i32* %i, i32** %i8, align 8, !tbaa !14
  br label %bb9

bb9:                                              ; preds = %bb11, %bb
  %.0 = phi i32 [ 0, %bb ], [ %i16, %bb11 ]
  %i10 = icmp slt i32 %.0, 2
  br i1 %i10, label %bb11, label %bb17

bb11:                                             ; preds = %bb9
  %i12 = bitcast %struct.P* %i2 to i8*
  %i13 = bitcast %struct.P* %i1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 8 %i12, i8* align 8 %i13, i64 16, i1 false), !tbaa.struct !17
  %i14 = bitcast %struct.P* %i1 to i8*
  %i15 = bitcast %struct.P* %i3 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 8 %i14, i8* align 8 %i15, i64 16, i1 false), !tbaa.struct !17
  %i16 = add nsw i32 %.0, 1
  br label %bb9, !llvm.loop !19

bb17:                                             ; preds = %bb9
  %i18 = getelementptr inbounds %struct.P, %struct.P* %i2, i32 0, i32 0
  %i19 = load i32*, i32** %i18, align 8, !tbaa !14
  %i20 = load i32, i32* %i19, align 4, !tbaa !12
  call void @sink(i32 noundef %i20)
  %i21 = bitcast %struct.P* %i3 to i8*
  call void @llvm.lifetime.end.p0i8(i64 16, i8* %i21) #4
  %i22 = bitcast %struct.P* %i2 to i8*
  call void @llvm.lifetime.end.p0i8(i64 16, i8* %i22) #4
  %i23 = bitcast %struct.P* %i1 to i8*
  call void @llvm.lifetime.end.p0i8(i64 16, i8* %i23) #4
  %i24 = bitcast i32* %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 4, i8* %i24) #4
  ret void
}

declare void @sink(i32 noundef) #2

; Function Attrs: nounwind uwtable
define dso_local i32 @main() #0 {
bb:
  call void @order()
  %i = call i8* @g()
  %i1 = load i8, i8* %i, align 1, !tbaa !11
  %i2 = sext i8 %i1 to i32
  ret i32 %i2
}

attributes #0 = { nounwind uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { argmemonly nofree nosync nounwind willreturn }
attributes #2 = { "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { argmemonly nofree nounwind willreturn }
attributes #4 = { nounwind }

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
!9 = !{!10, !6, i64 0}
!10 = !{!"S", !6, i64 0, !6, i64 8, !6, i64 16, !6, i64 24, !6, i64 32}
!11 = !{!7, !7, i64 0}
!12 = !{!13, !13, i64 0}
!13 = !{!"int", !7, i64 0}
!14 = !{!15, !6, i64 0}
!15 = !{!"P", !6, i64 0, !16, i64 8}
!16 = !{!"long", !7, i64 0}
!17 = !{i64 0, i64 8, !5, i64 8, i64 8, !18}
!18 = !{!16, !16, i64 0}
!19 = distinct !{!19, !20, !21}
!20 = !{!"llvm.loop.mustprogress"}
!21 = !{!"llvm.loop.unroll.disable"}
