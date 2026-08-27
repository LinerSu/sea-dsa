; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-aa-eval --disable-basic-aa -print-all-alias-modref-info 2>&1 | grep -E "Alias:" | env LC_ALL=C sort | OutputCheck %s -d --comment=";"
;
; Size-, interval- and global-aware SeaDsaAA answers (see tests/c/aa_sizes.c):
;  - (@a,@b) unified through `p = nd ? &a : &b` => May; (@a,@c) => No
;  - malloc result (i8*, 1 byte) vs &rb->id => May; %struct.rb* (12 bytes) vs &rb->size => May
;  - &rb->id vs &rb->size, vs &rb->buf[n-1] (interval cell [12-+oo]) => No
; CHECK: ^\s*MayAlias:\s+%struct\.rb\*\ %rb,\ i32\*\ %psize$
; CHECK: ^\s*MayAlias:\s+i32\*\ %p,\ i32\*\ @a$
; CHECK: ^\s*MayAlias:\s+i32\*\ %pid,\ i8\*\ %i5$
; CHECK: ^\s*MayAlias:\s+i32\*\ @a,\ i32\*\ @b$
; CHECK: ^\s*NoAlias:\s+%struct\.rb\*\ %rb,\ i8\*\ %pbuf$
; CHECK: ^\s*NoAlias:\s+i32\*\ %p,\ i32\*\ %pid$
; CHECK: ^\s*NoAlias:\s+i32\*\ %p,\ i32\*\ @c$
; CHECK: ^\s*NoAlias:\s+i32\*\ %pid,\ i32\*\ %psize$
; CHECK: ^\s*NoAlias:\s+i32\*\ %pid,\ i8\*\ %pbuf$
; CHECK: ^\s*NoAlias:\s+i32\*\ @a,\ i32\*\ @c$

; ModuleID = 'aa_sizes.ll'
source_filename = "c/aa_sizes.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.rb = type { i32, i32, i32, [0 x i8] }

@a = dso_local global i32 0, align 4
@b = dso_local global i32 0, align 4
@c = dso_local global i32 0, align 4

; Function Attrs: nounwind uwtable
define dso_local i32 @main() #0 {
bb:
  %i = call i32 @nd_int()
  %i1 = icmp sle i32 %i, 0
  br i1 %i1, label %bb23, label %bb2

bb2:                                              ; preds = %bb
  %i3 = sext i32 %i to i64
  %i4 = add i64 12, %i3
  %i5 = call noalias i8* @malloc(i64 noundef %i4) #4
  %rb = bitcast i8* %i5 to %struct.rb*
  %pid = getelementptr inbounds %struct.rb, %struct.rb* %rb, i32 0, i32 0
  %psize = getelementptr inbounds %struct.rb, %struct.rb* %rb, i32 0, i32 1
  %pbuf_arr = getelementptr inbounds %struct.rb, %struct.rb* %rb, i32 0, i32 3
  %i10 = sub nsw i32 %i, 1
  %i11 = sext i32 %i10 to i64
  %pbuf = getelementptr inbounds [0 x i8], [0 x i8]* %pbuf_arr, i64 0, i64 %i11
  store i32 1, i32* %pid, align 4, !tbaa !5
  store i32 %i, i32* %psize, align 4, !tbaa !5
  %i13 = call signext i8 @nd_char()
  store i8 %i13, i8* %pbuf, align 1, !tbaa !9
  %i14 = call i32 @nd_int()
  %i15 = icmp ne i32 %i14, 0
  %i16 = zext i1 %i15 to i64
  %p = select i1 %i15, i32* @a, i32* @b
  store i32 1, i32* %p, align 4, !tbaa !5
  store i32 2, i32* @c, align 4, !tbaa !5
  %i18 = load i32, i32* %pid, align 4, !tbaa !5
  %i19 = load i32, i32* @a, align 4, !tbaa !5
  %i20 = add nsw i32 %i18, %i19
  %i21 = load i32, i32* @c, align 4, !tbaa !5
  %i22 = add nsw i32 %i20, %i21
  br label %bb23

bb23:                                             ; preds = %bb, %bb2
  %.0 = phi i32 [ %i22, %bb2 ], [ 0, %bb ]
  ret i32 %.0
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #1

declare i32 @nd_int() #2

; Function Attrs: nounwind
declare noalias i8* @malloc(i64 noundef) #3

declare signext i8 @nd_char() #2

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #1

attributes #0 = { nounwind uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { argmemonly nofree nosync nounwind willreturn }
attributes #2 = { "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nounwind "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nounwind }

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
!9 = !{!7, !7, i64 0}
