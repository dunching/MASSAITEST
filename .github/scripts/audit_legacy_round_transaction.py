from pathlib import Path

ROOTS = [Path("Source/MassAICrowdDemo"), Path("Plugins/MassCrowdSimulation/Source")]
REPORT = Path("legacy_round_transaction_audit.txt")
SYMBOLS = [
    "FCrowdDemoRoundWorkBatch",
    "BeginBoundaryTransaction",
    "TryPrepareRoundApply",
    "BoundaryOrchestrator",
    "GetRoundWorkState",
    "MarkRoundApplyCommitted",
    "ValidateRoundApplyPlan",
    "StageBoundaryBusinessWork",
    "StageBoundarySharedFlowWork",
    "StageBoundaryTargetTopologyWork",
    "StageBoundaryTargetDemandWork",
    "StageBoundaryTargetPlanWork",
    "StageBoundaryTargetGuidanceWork",
    "StageBoundaryMovementWork",
    "ConsumeBoundaryMovementWork",
    "StageBoundaryParticleWork",
    "ConsumeBoundaryParticleWork",
    "DispatchBoundarySoftPressureWorkGraph",
    "DispatchBoundaryFacingWork",
    "ConsumeBoundaryFacingWork",
    "FCrowdDemoRoundOpenSpawnRelaxationPhasePrepareStage",
    "FCrowdDemoRoundTargetFactApplyStage",
    "FCrowdDemoRoundTargetPolarTopologyBuildStage",
    "FCrowdDemoRoundTargetRegionPopulationBuildStage",
    "FCrowdDemoRoundTargetRegionTransportSolveStage",
    "FCrowdDemoRoundTargetRegionGuidanceStage",
    "FCrowdDemoRoundSharedFlowFieldBuildStage",
    "FCrowdDemoRoundFlowPreferredVelocityStage",
    "FCrowdDemoRoundMovementWorkStage",
    "FCrowdDemoRoundParticleConstraintStage",
    "FCrowdDemoRoundObstacleConstraintStage",
    "FCrowdDemoRoundFacingFinalizeStage",
    "FCrowdDemoRoundPostFinalizeMetricsStage",
    "FCrowdDemoRoundAuthorityCommitStage",
    "FCrowdDemoRoundClientPredictionCommitStage",
    "FCrowdDemoRoundCheckpointPublisherStage",
]
TEXT_SUFFIXES = {".h", ".hpp", ".cpp", ".inl", ".cs"}
files = []
for root in ROOTS:
    if root.exists():
        files.extend(p for p in root.rglob("*") if p.is_file() and p.suffix.lower() in TEXT_SUFFIXES)

out = ["LEGACY ROUND TRANSACTION AUDIT\n"]
for symbol in SYMBOLS:
    matches = []
    for path in files:
        try:
            text = path.read_text(encoding="utf-8-sig")
        except UnicodeDecodeError:
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            if symbol in line:
                matches.append(f"{path}:{lineno}:{line.strip()}")
    out.append(f"\n## {symbol} ({len(matches)})\n")
    out.extend(m + "\n" for m in matches)

contexts = [
    (Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.cpp"), "bool AdvanceRoundWorkerFrame", 520),
    (Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"), "class FCrowdDemoRoundWorkBatch", 300),
    (Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"), "UCrowdDemoRoundSimPipelineSubsystem::BeginBoundaryTransaction", 220),
    (Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"), "UCrowdDemoRoundSimPipelineSubsystem::TryPrepareRoundApply", 520),
    (Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"), "UCrowdDemoRoundSimPipelineSubsystem::ValidateRoundApplyPlan", 220),
    (Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"), "UCrowdDemoRoundSimPipelineSubsystem::MarkRoundApplyCommitted", 180),
]
for path, needle, span in contexts:
    out.append(f"\n\n===== CONTEXT {path} :: {needle} =====\n")
    src = path.read_text(encoding="utf-8-sig").splitlines() if path.exists() else []
    idx = next((i for i, line in enumerate(src) if needle in line), None)
    if idx is None:
        out.append("MISSING NEEDLE\n")
        continue
    start = max(0, idx - 12)
    end = min(len(src), idx + span)
    for i in range(start, end):
        out.append(f"{i+1:05d}: {src[i]}\n")

REPORT.write_text("".join(out), encoding="utf-8")
print(f"wrote {REPORT}")
