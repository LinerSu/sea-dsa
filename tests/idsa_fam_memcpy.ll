; RUN: %seadsa %s %ci_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-dot --sea-dsa-dot-outdir=%T/fam-ci
; RUN: cat %T/fam-ci/main.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=CI
; RUN: %seadsa %s %butd_dsa --sea-dsa-type-aware --sea-dsa-partial-collapse --sea-dsa-dot --sea-dsa-dot-outdir=%T/fam-butd
; RUN: cat %T/fam-butd/main.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=MAIN
; RUN: cat %T/fam-butd/push.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=PUSH
; RUN: cat %T/fam-butd/namevalue.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=NV
;
; curl's Curl_headers_push shape (see tests/c/idsa_fam_memcpy.c): a calloc'd
; struct with a flexible array member `buffer` at 29, a memcpy of unknown
; length into it, and `name`/`value` (fields 8 and 16) set to pointers INTO
; the copied buffer by namevalue(), which also computes end = header+hlen-1
; and walks it backwards.  In namevalue's local graph the string node is
; {[0-1],[2-+oo)} with header at raw 2; it is embedded into the store node at
; raw offset 27, strictly inside the buffer interval.
;  - The embedding base must stay raw (Node::pointTo): the buffer interval
;    lands at [29-+oo) and the store keeps its fields; snapping the base to
;    the interval start merged [0,1] and [1,+oo) and collapsed the node.
;  - name/value must point at the buffer cell (29) in every view: the
;    caller's (ci, BU) and the callee's (TD).  In BU/TD the string node's
;    clone is embedded at 27 while resolving the first formal, so cells built
;    later from that clone (Cloner::cloneCell) must carry the +27 shift.
; The [27-28] cell is namevalue's `end`/`end-1` (a negative GEP off the
; symbolic header+hlen lands one/two bytes before the interval) and is not
; checked here.
;
; The phi operands of %.02 (the `end--` loop) were reordered by hand, loop
; back-edge first, as clang -O1 emits for curl's loop: the same-node
; unification (end-1, end) must precede the embedding of the header+hlen node.
;
; CI: ^.*shape=record(?=.*[{,]8:i8\*)(?=.*[{,]16:i8\*)(?=.*[{,]24:i32)(?=.*\[29-\+oo\]:raw)
; CI: ^\s*(Node0x[0-9a-f]+):s0 -> \1\[arrowtail=tee,label="29, omni_i8\*"
; CI: ^\s*(Node0x[0-9a-f]+):s1 -> \1\[arrowtail=tee,label="29, omni_i8\*"
; MAIN: ^.*shape=record(?=.*[{,]8:i8\*)(?=.*[{,]16:i8\*)(?=.*[{,]24:i32)(?=.*\[29-\+oo\]:raw)
; MAIN: ^\s*(Node0x[0-9a-f]+):s0 -> \1\[arrowtail=tee,label="29, omni_i8\*"
; MAIN: ^\s*(Node0x[0-9a-f]+):s1 -> \1\[arrowtail=tee,label="29, omni_i8\*"
; PUSH: ^.*shape=record(?=.*[{,]8:i8\*)(?=.*[{,]16:i8\*)(?=.*[{,]24:i32)(?=.*\[29-\+oo\]:raw)
; PUSH: ^\s*(Node0x[0-9a-f]+):s0 -> \1\[arrowtail=tee,label="29, omni_i8\*"
; PUSH: ^\s*(Node0x[0-9a-f]+):s1 -> \1\[arrowtail=tee,label="29, omni_i8\*"
; NV: ^.*shape=record(?=.*[{,]8:i8\*)(?=.*[{,]16:i8\*)(?=.*[{,]24:i32)(?=.*\[29-\+oo\]:raw)
; NV: ^\s*(Node0x[0-9a-f]+):s0 -> \1\[arrowtail=tee,label="29, omni_i8\*"
; NV: ^\s*(Node0x[0-9a-f]+):s1 -> \1\[arrowtail=tee,label="29, omni_i8\*"

; ModuleID = 'idsa_fam_memcpy.ll'
source_filename = "idsa_fam_memcpy.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.store = type { %struct.store*, i8*, i8*, i32, i8, [1 x i8] }

@__const.main.line = private unnamed_addr constant [16 x i8] c"k: v  \00\00\00\00\00\00\00\00\00\00", align 16

; Function Attrs: nounwind uwtable
define dso_local i32 @main() #0 {
bb:
  %i = alloca [16 x i8], align 16
  %i1 = bitcast [16 x i8]* %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 16, i8* %i1) #5
  %i2 = bitcast [16 x i8]* %i to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 16 %i2, i8* align 16 getelementptr inbounds ([16 x i8], [16 x i8]* @__const.main.line, i32 0, i32 0), i64 16, i1 false)
  %i3 = getelementptr inbounds [16 x i8], [16 x i8]* %i, i64 0, i64 0
  %i4 = call i32 @nd_int()
  %i5 = sext i32 %i4 to i64
  %i6 = call %struct.store* @push(i8* noundef %i3, i64 noundef %i5)
  %i7 = icmp ne %struct.store* %i6, null
  br i1 %i7, label %bb8, label %bb19

bb8:                                              ; preds = %bb
  %i9 = getelementptr inbounds %struct.store, %struct.store* %i6, i32 0, i32 1
  %i10 = load i8*, i8** %i9, align 8, !tbaa !5
  call void @sink(i8* noundef %i10)
  %i11 = getelementptr inbounds %struct.store, %struct.store* %i6, i32 0, i32 2
  %i12 = load i8*, i8** %i11, align 8, !tbaa !11
  call void @sink(i8* noundef %i12)
  %i13 = getelementptr inbounds %struct.store, %struct.store* %i6, i32 0, i32 3
  %i14 = load i32, i32* %i13, align 8, !tbaa !12
  %i15 = getelementptr inbounds %struct.store, %struct.store* %i6, i32 0, i32 4
  %i16 = load i8, i8* %i15, align 4, !tbaa !13
  %i17 = zext i8 %i16 to i32
  %i18 = add nsw i32 %i14, %i17
  br label %bb19

bb19:                                             ; preds = %bb, %bb8
  %.0 = phi i32 [ %i18, %bb8 ], [ 1, %bb ]
  %i20 = bitcast [16 x i8]* %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 16, i8* %i20) #5
  ret i32 %.0
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #1

; Function Attrs: argmemonly nofree nounwind willreturn
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* noalias nocapture writeonly, i8* noalias nocapture readonly, i64, i1 immarg) #2

; Function Attrs: nounwind uwtable
define internal %struct.store* @push(i8* noundef %arg, i64 noundef %arg1) #0 {
bb:
  %i = alloca i8*, align 8
  %i2 = alloca i8*, align 8
  %i3 = bitcast i8** %i to i8*
  call void @llvm.lifetime.start.p0i8(i64 8, i8* %i3) #5
  store i8* null, i8** %i, align 8, !tbaa !14
  %i4 = bitcast i8** %i2 to i8*
  call void @llvm.lifetime.start.p0i8(i64 8, i8* %i4) #5
  store i8* null, i8** %i2, align 8, !tbaa !14
  %i5 = add i64 32, %arg1
  %i6 = call noalias i8* @calloc(i64 noundef 1, i64 noundef %i5) #5
  %i7 = bitcast i8* %i6 to %struct.store*
  %i8 = getelementptr inbounds %struct.store, %struct.store* %i7, i32 0, i32 5
  %i9 = getelementptr inbounds [1 x i8], [1 x i8]* %i8, i64 0, i64 0
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 1 %i9, i8* align 1 %arg, i64 %arg1, i1 false)
  %i10 = getelementptr inbounds %struct.store, %struct.store* %i7, i32 0, i32 5
  %i11 = getelementptr inbounds [1 x i8], [1 x i8]* %i10, i64 0, i64 %arg1
  store i8 0, i8* %i11, align 1, !tbaa !15
  %i12 = getelementptr inbounds %struct.store, %struct.store* %i7, i32 0, i32 5
  %i13 = getelementptr inbounds [1 x i8], [1 x i8]* %i12, i64 0, i64 0
  %i14 = call i32 @namevalue(i8* noundef %i13, i64 noundef %arg1, i8** noundef %i, i8** noundef %i2)
  %i15 = icmp ne i32 %i14, 0
  br i1 %i15, label %bb16, label %bb18

bb16:                                             ; preds = %bb
  %i17 = bitcast %struct.store* %i7 to i8*
  call void @free(i8* noundef %i17) #5
  br label %bb26

bb18:                                             ; preds = %bb
  %i19 = load i8*, i8** %i, align 8, !tbaa !14
  %i20 = getelementptr inbounds %struct.store, %struct.store* %i7, i32 0, i32 1
  store i8* %i19, i8** %i20, align 8, !tbaa !5
  %i21 = load i8*, i8** %i2, align 8, !tbaa !14
  %i22 = getelementptr inbounds %struct.store, %struct.store* %i7, i32 0, i32 2
  store i8* %i21, i8** %i22, align 8, !tbaa !11
  %i23 = getelementptr inbounds %struct.store, %struct.store* %i7, i32 0, i32 4
  store i8 1, i8* %i23, align 4, !tbaa !13
  %i24 = trunc i64 %arg1 to i32
  %i25 = getelementptr inbounds %struct.store, %struct.store* %i7, i32 0, i32 3
  store i32 %i24, i32* %i25, align 8, !tbaa !12
  br label %bb26

bb26:                                             ; preds = %bb18, %bb16
  %.0 = phi %struct.store* [ null, %bb16 ], [ %i7, %bb18 ]
  %i27 = bitcast i8** %i2 to i8*
  call void @llvm.lifetime.end.p0i8(i64 8, i8* %i27) #5
  %i28 = bitcast i8** %i to i8*
  call void @llvm.lifetime.end.p0i8(i64 8, i8* %i28) #5
  ret %struct.store* %.0
}

declare i32 @nd_int() #3

declare void @sink(i8* noundef) #3

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #1

; Function Attrs: nounwind
declare noalias i8* @calloc(i64 noundef, i64 noundef) #4

; Function Attrs: nounwind uwtable
define internal i32 @namevalue(i8* noundef %arg, i64 noundef %arg4, i8** noundef %arg5, i8** noundef %arg6) #0 {
bb:
  %i = getelementptr inbounds i8, i8* %arg, i64 %arg4
  %i7 = getelementptr inbounds i8, i8* %i, i64 -1
  store i8* %arg, i8** %arg5, align 8, !tbaa !14
  br label %bb8

bb8:                                              ; preds = %bb16, %bb
  %.01 = phi i8* [ %arg, %bb ], [ %i17, %bb16 ]
  %i9 = load i8, i8* %.01, align 1, !tbaa !15
  %i10 = sext i8 %i9 to i32
  %i11 = icmp ne i32 %i10, 0
  br i1 %i11, label %bb12, label %.critedge

bb12:                                             ; preds = %bb8
  %i13 = load i8, i8* %.01, align 1, !tbaa !15
  %i14 = sext i8 %i13 to i32
  %i15 = icmp ne i32 %i14, 58
  br i1 %i15, label %bb16, label %.critedge

bb16:                                             ; preds = %bb12
  %i17 = getelementptr inbounds i8, i8* %.01, i32 1
  br label %bb8, !llvm.loop !16

.critedge:                                        ; preds = %bb8, %bb12
  %i18 = load i8, i8* %.01, align 1, !tbaa !15
  %i19 = icmp ne i8 %i18, 0
  br i1 %i19, label %bb20, label %.critedge3

bb20:                                             ; preds = %.critedge
  %i21 = getelementptr inbounds i8, i8* %.01, i32 1
  store i8 0, i8* %.01, align 1, !tbaa !15
  br label %bb22

bb22:                                             ; preds = %bb26, %bb20
  %.1 = phi i8* [ %i21, %bb20 ], [ %i27, %bb26 ]
  %i23 = load i8, i8* %.1, align 1, !tbaa !15
  %i24 = sext i8 %i23 to i32
  %i25 = icmp eq i32 %i24, 32
  br i1 %i25, label %bb26, label %bb28

bb26:                                             ; preds = %bb22
  %i27 = getelementptr inbounds i8, i8* %.1, i32 1
  br label %bb22, !llvm.loop !19

bb28:                                             ; preds = %bb22
  store i8* %.1, i8** %arg6, align 8, !tbaa !14
  br label %bb29

bb29:                                             ; preds = %bb35, %bb28
  %.02 = phi i8* [ %i36, %bb35 ], [ %i7, %bb28 ]
  %i30 = icmp ugt i8* %.02, %.1
  br i1 %i30, label %bb31, label %.critedge3

bb31:                                             ; preds = %bb29
  %i32 = load i8, i8* %.02, align 1, !tbaa !15
  %i33 = sext i8 %i32 to i32
  %i34 = icmp eq i32 %i33, 32
  br i1 %i34, label %bb35, label %.critedge3

bb35:                                             ; preds = %bb31
  %i36 = getelementptr inbounds i8, i8* %.02, i32 -1
  store i8 0, i8* %.02, align 1, !tbaa !15
  br label %bb29, !llvm.loop !20

.critedge3:                                       ; preds = %bb31, %bb29, %.critedge
  %.0 = phi i32 [ 1, %.critedge ], [ 0, %bb29 ], [ 0, %bb31 ]
  ret i32 %.0
}

; Function Attrs: nounwind
declare void @free(i8* noundef) #4

attributes #0 = { nounwind uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { argmemonly nofree nosync nounwind willreturn }
attributes #2 = { argmemonly nofree nounwind willreturn }
attributes #3 = { "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nounwind "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #5 = { nounwind }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{!"Ubuntu clang version 14.0.0-1ubuntu1.1"}
!5 = !{!6, !7, i64 8}
!6 = !{!"store", !7, i64 0, !7, i64 8, !7, i64 16, !10, i64 24, !8, i64 28, !8, i64 29}
!7 = !{!"any pointer", !8, i64 0}
!8 = !{!"omnipotent char", !9, i64 0}
!9 = !{!"Simple C/C++ TBAA"}
!10 = !{!"int", !8, i64 0}
!11 = !{!6, !7, i64 16}
!12 = !{!6, !10, i64 24}
!13 = !{!6, !8, i64 28}
!14 = !{!7, !7, i64 0}
!15 = !{!8, !8, i64 0}
!16 = distinct !{!16, !17, !18}
!17 = !{!"llvm.loop.mustprogress"}
!18 = !{!"llvm.loop.unroll.disable"}
!19 = distinct !{!19, !17, !18}
!20 = distinct !{!20, !17, !18}
