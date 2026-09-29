; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=SEADSA
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=SEADSA
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-lazy-mem-transfer-by-cell=false --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=SEADSA_KEY
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-lazy-mem-transfer --sea-dsa-partial-collapse --sea-dsa-aa-pairs --disable-basic-aa 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=CLOSED
;
; Lazy memcpy of a whole struct with a variable length, see
; tests/c/idsa_lazy_memcpy_oneshot.c: r = dst.b (%i16) may be &B or &C.
; SeaDSA collapses src (p = &src.h or &src.a): its links are keyed at 0 and the
; key rule copies them to dst + 0 only (SEADSA_KEY); by cell, the collapsed
; source unifies with the destination.  I-DSA keeps [0,8] and the links of
; src.b and src.c, which are copied.
; SEADSA: MayAlias:\s+i8\* %i16, i8\* @B
; SEADSA: MayAlias:\s+i8\* %i16, i8\* @C
; SEADSA_KEY: NoAlias:\s+i8\* %i16, i8\* @B
; SEADSA_KEY: NoAlias:\s+i8\* %i16, i8\* @C
; CLOSED: MayAlias:\s+i8\* %i16, i8\* @B
; CLOSED: MayAlias:\s+i8\* %i16, i8\* @C

; ModuleID = '/tmp/claude-1000/-home-ljgy-Works-seatools-clam/eacef5b5-4b86-4eb3-8f92-0a02bad86b1d/scratchpad/v2work/idsa_lazy_memcpy_oneshot.raw.ll'
source_filename = "c/idsa_lazy_memcpy_oneshot.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.S = type { i8*, i8*, i8*, i8* }

@B = dso_local global i8 0, align 1
@C = dso_local global i8 0, align 1

; Function Attrs: nounwind uwtable
define dso_local i8* @f() #0 {
bb:
  %i = alloca %struct.S, align 8
  %i1 = alloca %struct.S, align 8
  %i2 = bitcast %struct.S* %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 32, i8* %i2) #4
  %i3 = bitcast %struct.S* %i1 to i8*
  call void @llvm.lifetime.start.p0i8(i64 32, i8* %i3) #4
  %i4 = getelementptr inbounds %struct.S, %struct.S* %i, i32 0, i32 2
  store i8* @B, i8** %i4, align 8, !tbaa !5
  %i5 = getelementptr inbounds %struct.S, %struct.S* %i, i32 0, i32 3
  store i8* @C, i8** %i5, align 8, !tbaa !10
  %i6 = call i32 @input()
  %i7 = icmp ne i32 %i6, 0
  %i8 = getelementptr inbounds %struct.S, %struct.S* %i, i32 0, i32 0
  %i9 = getelementptr inbounds %struct.S, %struct.S* %i, i32 0, i32 1
  %i10 = select i1 %i7, i8** %i8, i8** %i9
  %i11 = getelementptr inbounds i8*, i8** %i10, i64 2
  store i8* @C, i8** %i11, align 8, !tbaa !11
  %i12 = bitcast %struct.S* %i1 to i8*
  %i13 = bitcast %struct.S* %i to i8*
  %i14 = call i64 @len()
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 8 %i12, i8* align 8 %i13, i64 %i14, i1 false)
  %i15 = getelementptr inbounds %struct.S, %struct.S* %i1, i32 0, i32 2
  %i16 = load i8*, i8** %i15, align 8, !tbaa !5
  store i8 1, i8* %i16, align 1, !tbaa !12
  %i17 = bitcast %struct.S* %i1 to i8*
  call void @llvm.lifetime.end.p0i8(i64 32, i8* %i17) #4
  %i18 = bitcast %struct.S* %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 32, i8* %i18) #4
  ret i8* %i16
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
define dso_local i32 @main() #0 {
bb:
  %i = call i8* @f()
  %i1 = load i8, i8* %i, align 1, !tbaa !12
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
!5 = !{!6, !7, i64 16}
!6 = !{!"S", !7, i64 0, !7, i64 8, !7, i64 16, !7, i64 24}
!7 = !{!"any pointer", !8, i64 0}
!8 = !{!"omnipotent char", !9, i64 0}
!9 = !{!"Simple C/C++ TBAA"}
!10 = !{!6, !7, i64 24}
!11 = !{!7, !7, i64 0}
!12 = !{!8, !8, i64 0}
