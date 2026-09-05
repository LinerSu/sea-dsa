; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-dot --sea-dsa-dot-outdir=%T/ei
; RUN: cat %T/ei/main.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=MAIN
; RUN: cat %T/ei/f.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=F
;
; Embedding a node at an offset strictly inside an interval cell of the
; target (see tests/c/idsa_embed_inside_interval.c):
;  - `q = nd ? raw : raw+7` unifies (o,0) and (o,7) on the same node, so
;    o's node gets the interval [0,7];
;  - f's node for `p` (Inner: c@8) is unified with &o.in, i.e. embedded into
;    o's node at raw offset 4, which lies strictly inside [0,7].
; The embedding base must stay raw (4): in.c lands at 4+8 = 12 and o.y stays
; at 16.  Snapping the base to the interval start (0) shifted every field of
; the embedded node left by 4, putting in.c at 8.  Both the caller's (BU) and
; the callee's (TD) view of the object must show [0-7], 12:i32, 16:i32 and no
; field at 8.
; MAIN: ^.*shape=record(?=.*\[0-7\]:raw)(?=.*[{,]12:i32)(?=.*[{,]16:i32)(?!.*[{,]8:i32)
; F: ^.*shape=record(?=.*\[0-7\]:raw)(?=.*[{,]12:i32)(?=.*[{,]16:i32)(?!.*[{,]8:i32)

; ModuleID = 'idsa_embed_inside_interval.ll'
source_filename = "c/idsa_embed_inside_interval.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.Inner = type { i32, i32, i32 }
%struct.Outer = type { i32, %struct.Inner, i32 }

; Function Attrs: nounwind uwtable
define dso_local void @f(%struct.Inner* noundef %arg) #0 {
bb:
  %i = getelementptr inbounds %struct.Inner, %struct.Inner* %arg, i32 0, i32 2
  store i32 1, i32* %i, align 4, !tbaa !5
  ret void
}

; Function Attrs: nounwind uwtable
define dso_local i32 @main() #0 {
bb:
  %i = alloca %struct.Outer, align 4
  %i1 = bitcast %struct.Outer* %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 20, i8* %i1) #3
  %i2 = getelementptr inbounds %struct.Outer, %struct.Outer* %i, i32 0, i32 2
  store i32 0, i32* %i2, align 4, !tbaa !10
  %i3 = bitcast %struct.Outer* %i to i8*
  %i4 = call i32 @nd_int()
  %i5 = icmp ne i32 %i4, 0
  %i6 = getelementptr inbounds i8, i8* %i3, i64 7
  %i7 = select i1 %i5, i8* %i3, i8* %i6
  store i8 0, i8* %i7, align 1, !tbaa !12
  %i8 = getelementptr inbounds %struct.Outer, %struct.Outer* %i, i32 0, i32 1
  call void @f(%struct.Inner* noundef %i8)
  %i9 = getelementptr inbounds %struct.Outer, %struct.Outer* %i, i32 0, i32 2
  %i10 = load i32, i32* %i9, align 4, !tbaa !10
  %i11 = bitcast %struct.Outer* %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 20, i8* %i11) #3
  ret i32 %i10
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #1

declare i32 @nd_int() #2

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #1

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
!5 = !{!6, !7, i64 8}
!6 = !{!"Inner", !7, i64 0, !7, i64 4, !7, i64 8}
!7 = !{!"int", !8, i64 0}
!8 = !{!"omnipotent char", !9, i64 0}
!9 = !{!"Simple C/C++ TBAA"}
!10 = !{!11, !7, i64 16}
!11 = !{!"Outer", !7, i64 0, !6, i64 4, !7, i64 16}
!12 = !{!8, !8, i64 0}
