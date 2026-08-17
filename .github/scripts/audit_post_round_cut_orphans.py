from pathlib import Path

ROOTS = [Path("Source/MassAICrowdDemo")]
REPORT = Path("post_round_cut_orphans.txt")
SYMBOLS = [
    "FCrowdDemoPreparedMovementBoundaryCommit",
    "PreparedMovementBoundaryCommit",
    "SetPreparedMovementBoundaryCommit",
    "GetPreparedMovementBoundaryCommit",
    "IsPreparedMovementBoundaryCommitCurrent",
    "MovementFinalizeAppliedFixedStepIndex",
    "MarkMovementFinalizeApplied",
    "IsMovementFinalizeAppliedCurrent",
    "FCrowdDemoPreparedTargetResourcePlan",
    "PreparedTargetResourceSlots",
    "PreparePendingTargetResourcePlan",
    "FinalValidatePreparedTargetResourcePlan",
    "ApplyPreparedTargetResourcePlanNoFail",
    "PreparedTargetResourcePlan",
    "FCrowdDemoPreparedCombatBoundaryCommit",
    "PreparedCombatBoundaryCommit",
    "SetPreparedCombatBoundaryCommit",
    "GetPreparedCombatBoundaryCommit",
    "IsPreparedCombatBoundaryCommitCurrent",
    "PreparedRuntimeSharedFlowOutputs",
    "PreparedRuntimeComposedGuidance",
    "PreparedRuntimePredictedMovements",
    "PreparedRuntimeParticleResults",
    "PreparedRuntimeFinalKinematics",
    "PreparedRuntimeFacingResults",
    "PreparedFacingRollbackFacts",
    "FCrowdDemoPreparedParticleDiagnosticCommit",
    "PreparedParticleDiagnosticCommit",
    "CommitPreparedParticleDiagnostics",
    "SetPreparedParticleDiagnosticCommit",
    "IsPreparedParticleDiagnosticCommitCurrent",
    "FCrowdDemoRoundPostFinalizeMetricsStage",
    "FCrowdDemoRoundAuthorityCommitStage",
    "FCrowdDemoRoundClientPredictionCommitStage",
    "FCrowdDemoRoundCheckpointPublisherStage",
    "FCrowdDemoRoundMovementWorkStage",
    "FCrowdDemoRoundFacingFinalizeStage",
    "TryPrepareRoundApply",
    "BeginBoundaryTransaction",
    "FCrowdDemoRoundWorkBatch",
]
files=[]
for root in ROOTS:
    files.extend(p for p in root.rglob("*") if p.is_file() and p.suffix.lower() in {".h", ".cpp", ".inl"})
out=["POST ROUND CUT ORPHAN AUDIT\n"]
for symbol in SYMBOLS:
    matches=[]
    for path in files:
        try: text=path.read_text(encoding="utf-8-sig")
        except UnicodeDecodeError: continue
        for lineno,line in enumerate(text.splitlines(),1):
            if symbol in line:
                matches.append(f"{path}:{lineno}:{line.strip()}")
    out.append(f"\n## {symbol} ({len(matches)})\n")
    out.extend(m+"\n" for m in matches)
REPORT.write_text("".join(out), encoding="utf-8")
print(f"wrote {REPORT}")
