; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=SEADSA
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=SEADSA
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-lazy-mem-transfer-by-cell=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=SEADSA_KEY
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-partial-collapse --sea-dsa-partial-collapse-closure=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ONEPASS
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-partial-collapse --sea-dsa-partial-collapse-closure=false --sea-dsa-lazy-mem-transfer-by-cell=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ONEPASS_KEY
;
; Lazy memcpy from a source inside an interval (I-DSA) or on a collapsed node
; (SeaDSA), see tests/c/idsa_lazy_memcpy_interval.c: y->f1 (%i11) may be &a.
; Link keys are canonical (the interval start 8; 0 on the collapsed node), the
; source's raw offset is 16: the key rule (--sea-dsa-lazy-mem-transfer-by-cell=
; false, the one-pass behaviour, *_KEY) copies nothing.  By cell, the source
; window has no unique placement and the copy unifies source and destination.
; SEADSA: MayAlias:\s+i32\* %i11, i32\* @a
; SEADSA_KEY: NoAlias:\s+i32\* %i11, i32\* @a
; CLOSED: MayAlias:\s+i32\* %i11, i32\* @a
; ONEPASS: MayAlias:\s+i32\* %i11, i32\* @a
; ONEPASS_KEY: NoAlias:\s+i32\* %i11, i32\* @a

; ModuleID = '/tmp/claude-1000/-home-ljgy-Works-seatools-clam/eacef5b5-4b86-4eb3-8f92-0a02bad86b1d/scratchpad/v2work/idsa_lazy_memcpy_interval.raw.ll'
source_filename = "c/idsa_lazy_memcpy_interval.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.X = type { i64, i32*, i32*, i32* }

@a = dso_local global i32 0, align 4

; Function Attrs: nounwind uwtable
define dso_local i32* @f(%struct.X* noundef %arg, %struct.X* noundef %arg1) #0 {
bb:
  %i = getelementptr inbounds %struct.X, %struct.X* %arg, i32 0, i32 1
  store i32* @a, i32** %i, align 8, !tbaa !5
  %i2 = call i32 @input()
  %i3 = icmp ne i32 %i2, 0
  %i4 = getelementptr inbounds %struct.X, %struct.X* %arg, i32 0, i32 2
  %i5 = getelementptr inbounds %struct.X, %struct.X* %arg, i32 0, i32 1
  %i6 = select i1 %i3, i32** %i4, i32** %i5
  %i7 = getelementptr inbounds %struct.X, %struct.X* %arg1, i32 0, i32 1
  %i8 = bitcast i32** %i7 to i8*
  %i9 = bitcast i32** %i6 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 8 %i8, i8* align 8 %i9, i64 8, i1 false)
  %i10 = getelementptr inbounds %struct.X, %struct.X* %arg1, i32 0, i32 1
  %i11 = load i32*, i32** %i10, align 8, !tbaa !5
  store i32 1, i32* %i11, align 4, !tbaa !11
  ret i32* %i11
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #1

declare i32 @input() #2

; Function Attrs: argmemonly nofree nounwind willreturn
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* noalias nocapture writeonly, i8* noalias nocapture readonly, i64, i1 immarg) #3

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #1

; Function Attrs: nounwind uwtable
define dso_local i32 @main() #0 {
bb:
  %i = alloca %struct.X, align 8
  %i1 = alloca %struct.X, align 8
  %i2 = bitcast %struct.X* %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 32, i8* %i2) #4
  %i3 = bitcast %struct.X* %i1 to i8*
  call void @llvm.lifetime.start.p0i8(i64 32, i8* %i3) #4
  %i4 = call i32* @f(%struct.X* noundef %i, %struct.X* noundef %i1)
  %i5 = load i32, i32* %i4, align 4, !tbaa !11
  %i6 = bitcast %struct.X* %i1 to i8*
  call void @llvm.lifetime.end.p0i8(i64 32, i8* %i6) #4
  %i7 = bitcast %struct.X* %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 32, i8* %i7) #4
  ret i32 %i5
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
!5 = !{!6, !10, i64 8}
!6 = !{!"X", !7, i64 0, !10, i64 8, !10, i64 16, !10, i64 24}
!7 = !{!"long", !8, i64 0}
!8 = !{!"omnipotent char", !9, i64 0}
!9 = !{!"Simple C/C++ TBAA"}
!10 = !{!"any pointer", !8, i64 0}
!11 = !{!12, !12, i64 0}
!12 = !{!"int", !8, i64 0}
