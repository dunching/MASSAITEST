from pathlib import Path

HEADER = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.h")
CPP = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.cpp")


def read(path): return path.read_text(encoding="utf-8-sig")
def write(path, text): path.write_text(text, encoding="utf-8")


def function_span(text, signature):
    pos = text.find(signature)
    if pos < 0:
        raise RuntimeError(f"missing function: {signature}")
    brace = text.find("{", pos)
    if brace < 0:
        raise RuntimeError(f"missing brace: {signature}")
    depth = 0; i = brace
    in_string = in_char = escape = line_comment = block_comment = False
    while i < len(text):
        c = text[i]; n = text[i+1] if i+1 < len(text) else ""
        if line_comment:
            if c == "\n": line_comment = False
            i += 1; continue
        if block_comment:
            if c == "*" and n == "/": block_comment = False; i += 2; continue
            i += 1; continue
        if in_string:
            if escape: escape = False
            elif c == "\\": escape = True
            elif c == '"': in_string = False
            i += 1; continue
        if in_char:
            if escape: escape = False
            elif c == "\\": escape = True
            elif c == "'": in_char = False
            i += 1; continue
        if c == "/" and n == "/": line_comment = True; i += 2; continue
        if c == "/" and n == "*": block_comment = True; i += 2; continue
        if c == '"': in_string = True; i += 1; continue
        if c == "'": in_char = True; i += 1; continue
        if c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                while end < len(text) and text[end] in " \t\r": end += 1
                if end < len(text) and text[end] == "\n": end += 1
                return pos, end
        i += 1
    raise RuntimeError(f"unbalanced function: {signature}")


def remove_function(text, signature):
    s, e = function_span(text, signature)
    return text[:s] + text[e:]


def transform_function(text, signature, new_signature, body_transform=None):
    s, e = function_span(text, signature)
    block = text[s:e]
    if signature not in block:
        raise RuntimeError(f"signature not inside span: {signature}")
    block = block.replace(signature, new_signature, 1)
    if body_transform:
        block = body_transform(block)
    return text[:s] + block + text[e:]

# ---------------------------------------------------------------------------
# Header becomes a true processor header: only the two UMassProcessor types.
# ---------------------------------------------------------------------------
h = read(HEADER)
start = h.find("class UCrowdDemoRoundSimPipelineSubsystem;\n")
end = h.find("UCLASS()\n", start)
if start < 0 or end < 0:
    raise RuntimeError("stage declaration range missing")
h = h[:start] + h[end:]
if "struct FCrowdDemoRound" in h or "struct FCrowdDemoWorkerResultApplyStage" in h:
    raise RuntimeError("stage declaration remains in processor header")
write(HEADER, h)

p = read(CPP)

# Query configurators.
def strip_query_member(block):
    block = block.replace("  EntityQuery = &Query;\n", "", 1)
    return block

p = transform_function(
    p,
    "void FCrowdDemoRoundPlanApplyStage::BindQuery(FMassEntityQuery& Query)",
    "static void ConfigureRoundPlanApplyQuery(FMassEntityQuery& Query)",
    strip_query_member)
p = transform_function(
    p,
    "void FCrowdDemoRoundFacingFinalizeStage::BindQuery(\n  FMassEntityQuery& Query)",
    "static void ConfigureWorkerResultApplyQuery(\n  FMassEntityQuery& Query)",
    strip_query_member)
# The old WorkerResult BindQuery only delegated to Facing.BindQuery.
p = remove_function(
    p,
    "void FCrowdDemoWorkerResultApplyStage::BindQuery(\n  FMassEntityQuery& Query)")

# Plan Apply takes its query explicitly instead of owning a Stage object.
def plan_execute_body(block):
    unbound = (
        "  if (!EntityQuery)\n"
        "  {\n"
        "    UE_LOG(LogTemp, Error,\n"
        "      TEXT(\"VIOLATION CrowdDemoAuthorityInputQueryUnbound\"));\n"
        "    return;\n"
        "  }\n")
    if unbound not in block:
        raise RuntimeError("PlanApply unbound guard missing")
    block = block.replace(unbound, "", 1)
    block = block.replace("EntityQuery->", "EntityQuery.")
    return block

p = transform_function(
    p,
    "void FCrowdDemoRoundPlanApplyStage::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)",
    "static void ExecuteRoundPlanApply(\n  FMassEntityQuery& EntityQuery,\n  FMassEntityManager& EntityManager,\n  FMassExecutionContext& Context)",
    plan_execute_body)

# Worker Result Apply also takes its query explicitly.
def result_execute_body(block):
    block = block.replace(
        "  if (!EntityQuery\n    || !FCrowdDemoWorkerInputSync::PreparePublishedResults(\n",
        "  if (!FCrowdDemoWorkerInputSync::PreparePublishedResults(\n",
        1)
    block = block.replace("*EntityQuery", "EntityQuery")
    return block

p = transform_function(
    p,
    "void FCrowdDemoWorkerResultApplyStage::Execute(\n  FMassEntityManager& EntityManager,\n  FMassExecutionContext& Context)",
    "static void ExecuteWorkerResultApply(\n  FMassEntityQuery& EntityQuery,\n  FMassEntityManager& EntityManager,\n  FMassExecutionContext& Context)",
    result_execute_body)

# Simple stage methods become file-local functions.
simple = {
    "FCrowdDemoRoundSharedFlowFieldBuildStage": "ExecuteRoundSharedFlowFieldBuild",
    "FCrowdDemoRoundOpenSpawnRelaxationPhasePrepareStage": "ExecuteRoundOpenSpawnRelaxationPrepare",
    "FCrowdDemoRoundFlowPreferredVelocityStage": "ExecuteRoundFlowPreferredVelocity",
    "FCrowdDemoRoundTargetFactApplyStage": "ExecuteRoundTargetFactApply",
    "FCrowdDemoRoundTargetPolarTopologyBuildStage": "ExecuteRoundTargetPolarTopologyBuild",
    "FCrowdDemoRoundTargetRegionPopulationBuildStage": "ExecuteRoundTargetRegionPopulationBuild",
    "FCrowdDemoRoundTargetRegionTransportSolveStage": "ExecuteRoundTargetRegionTransportSolve",
    "FCrowdDemoRoundTargetRegionGuidanceStage": "ExecuteRoundTargetRegionGuidance",
    "FCrowdDemoRoundParticleConstraintStage": "ExecuteRoundParticleConstraint",
    "FCrowdDemoRoundObstacleConstraintStage": "ExecuteRoundObstacleConstraint",
    "FCrowdDemoRoundFacingFinalizeStage": "ExecuteRoundFacingBootstrap",
    "FCrowdDemoRoundCheckpointPublisherStage": "ExecuteRoundCheckpointPublisher",
}
for stage, func in simple.items():
    signature = f"void {stage}::Execute(\n  FMassEntityManager& EntityManager,"
    if signature not in p:
        # some one-line signatures
        signature = f"void {stage}::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)"
        new = f"static void {func}(FMassEntityManager& EntityManager, FMassExecutionContext& Context)"
    else:
        new = f"static void {func}(\n  FMassEntityManager& EntityManager,"
    p = transform_function(p, signature, new)

# Movement needs one tiny piece of mutable stage state; make it an out value.
def movement_body(block):
    block = block.replace("LastGuidanceWorkMilliseconds", "OutGuidanceWorkMilliseconds")
    return block
p = transform_function(
    p,
    "void FCrowdDemoRoundMovementWorkStage::Execute(\n  FMassEntityManager& EntityManager, FMassExecutionContext& Context)",
    "static void ExecuteRoundMovementWork(\n  FMassEntityManager& EntityManager,\n  FMassExecutionContext& Context,\n  float& OutGuidanceWorkMilliseconds)",
    movement_body)

# No live Worker-owned server path calls these legacy adapters anymore.
for signature in [
    "void FCrowdDemoRoundPostFinalizeMetricsStage::Execute(\n  FMassEntityManager& EntityManager, FMassExecutionContext& Context)",
    "void FCrowdDemoRoundAuthorityCommitStage::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)",
    "void FCrowdDemoRoundClientPredictionCommitStage::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)",
]:
    p = remove_function(p, signature)

# Processor wiring now calls helpers directly.
p = p.replace(
    "  FCrowdDemoRoundPlanApplyStage PlanApply;\n"
    "  PlanApply.BindQuery(InputSyncQuery);\n",
    "  ConfigureRoundPlanApplyQuery(InputSyncQuery);\n",
    1)
p = p.replace(
    "  FCrowdDemoRoundPlanApplyStage PlanApply;\n"
    "  PlanApply.UseQuery(InputSyncQuery);\n"
    "  PlanApply.Execute(EntityManager, Context);\n",
    "  ExecuteRoundPlanApply(InputSyncQuery, EntityManager, Context);\n",
    1)
p = p.replace(
    "  FCrowdDemoWorkerResultApplyStage WorkerResultApply;\n"
    "  WorkerResultApply.BindQuery(ResultCommitQuery);\n",
    "  ConfigureWorkerResultApplyQuery(ResultCommitQuery);\n",
    1)
p = p.replace(
    "  FCrowdDemoWorkerResultApplyStage WorkerResultApply;\n"
    "  WorkerResultApply.UseQuery(ResultCommitQuery);\n"
    "  WorkerResultApply.Execute(EntityManager, Context);\n",
    "  ExecuteWorkerResultApply(ResultCommitQuery, EntityManager, Context);\n",
    1)

# AdvanceRoundWorkerFrame: replace temporary Stage objects with direct helpers.
old_decls = '''  FCrowdDemoRoundOpenSpawnRelaxationPhasePrepareStage
    OpenSpawnRelaxationPhasePrepare;
  FCrowdDemoRoundTargetFactApplyStage TargetFactApply;
  FCrowdDemoRoundTargetPolarTopologyBuildStage
    TargetPolarTopologyBuild;
  FCrowdDemoRoundTargetRegionPopulationBuildStage
    TargetRegionPopulationBuild;
  FCrowdDemoRoundTargetRegionTransportSolveStage
    TargetRegionTransportSolve;
  FCrowdDemoRoundTargetRegionGuidanceStage TargetRegionGuidance;
  FCrowdDemoRoundSharedFlowFieldBuildStage SharedFlowFieldBuild;
  FCrowdDemoRoundFlowPreferredVelocityStage
    FlowPreferredVelocity;
  FCrowdDemoRoundMovementWorkStage MovementWork;
  FCrowdDemoRoundParticleConstraintStage ParticleConstraint;
  FCrowdDemoRoundObstacleConstraintStage ObstacleConstraint;
  FCrowdDemoRoundFacingFinalizeStage FacingFinalize;
'''
if old_decls not in p:
    raise RuntimeError("Advance stage declarations missing")
p = p.replace(old_decls, "", 1)

calls = {
    "TargetFactApply.Execute(EntityManager, Context);": "ExecuteRoundTargetFactApply(EntityManager, Context);",
    "OpenSpawnRelaxationPhasePrepare.Execute(\n          EntityManager, Context);": "ExecuteRoundOpenSpawnRelaxationPrepare(\n          EntityManager, Context);",
    "TargetFactApply.Execute(\n          EntityManager, Context);": "ExecuteRoundTargetFactApply(\n          EntityManager, Context);",
    "SharedFlowFieldBuild.Execute(\n        EntityManager, Context);": "ExecuteRoundSharedFlowFieldBuild(\n        EntityManager, Context);",
    "FlowPreferredVelocity.Execute(\n        EntityManager, Context);": "ExecuteRoundFlowPreferredVelocity(\n        EntityManager, Context);",
    "TargetPolarTopologyBuild.Execute(\n          EntityManager, Context);": "ExecuteRoundTargetPolarTopologyBuild(\n          EntityManager, Context);",
    "TargetRegionPopulationBuild.Execute(\n          EntityManager, Context);": "ExecuteRoundTargetRegionPopulationBuild(\n          EntityManager, Context);",
    "TargetRegionTransportSolve.Execute(\n          EntityManager, Context);": "ExecuteRoundTargetRegionTransportSolve(\n          EntityManager, Context);",
    "TargetRegionGuidance.Execute(\n          EntityManager, Context);": "ExecuteRoundTargetRegionGuidance(\n          EntityManager, Context);",
    "ParticleConstraint.Execute(\n          EntityManager, Context);": "ExecuteRoundParticleConstraint(\n          EntityManager, Context);",
    "ObstacleConstraint.Execute(\n          EntityManager, Context);": "ExecuteRoundObstacleConstraint(\n          EntityManager, Context);",
    "FacingFinalize.Execute(EntityManager, Context);": "ExecuteRoundFacingBootstrap(EntityManager, Context);",
    "CheckpointPublisher.Execute(EntityManager, Context);": "ExecuteRoundCheckpointPublisher(EntityManager, Context);",
}
for old, new in calls.items():
    if old not in p:
        raise RuntimeError(f"call site missing: {old[:50]}")
    p = p.replace(old, new)

old_move = '''    const double MovementStart = FPlatformTime::Seconds();
    MovementWork.Execute(EntityManager, Context);
    const float MovementMs = static_cast<float>(
      (FPlatformTime::Seconds() - MovementStart) * 1000.0);
    const float GuidanceMs = FMath::Clamp(
      MovementWork.GetLastGuidanceWorkMilliseconds(),
      0.0f, MovementMs);'''
new_move = '''    const double MovementStart = FPlatformTime::Seconds();
    float GuidanceWorkMilliseconds = 0.0f;
    ExecuteRoundMovementWork(
      EntityManager, Context, GuidanceWorkMilliseconds);
    const float MovementMs = static_cast<float>(
      (FPlatformTime::Seconds() - MovementStart) * 1000.0);
    const float GuidanceMs = FMath::Clamp(
      GuidanceWorkMilliseconds, 0.0f, MovementMs);'''
if old_move not in p:
    raise RuntimeError("movement call block missing")
p = p.replace(old_move, new_move, 1)

# The checkpoint object declaration is now unnecessary.
p = p.replace(
    "      FCrowdDemoRoundCheckpointPublisherStage CheckpointPublisher;\n"
    "      ExecuteRoundCheckpointPublisher(EntityManager, Context);\n",
    "      ExecuteRoundCheckpointPublisher(EntityManager, Context);\n",
    1)

write(CPP, p)

# Structural gates.
h = read(HEADER); p = read(CPP)
retired_stages = [
    "FCrowdDemoWorkerResultApplyStage",
    "FCrowdDemoRoundPlanApplyStage",
    "FCrowdDemoRoundSharedFlowFieldBuildStage",
    "FCrowdDemoRoundOpenSpawnRelaxationPhasePrepareStage",
    "FCrowdDemoRoundFlowPreferredVelocityStage",
    "FCrowdDemoRoundTargetFactApplyStage",
    "FCrowdDemoRoundTargetPolarTopologyBuildStage",
    "FCrowdDemoRoundTargetRegionPopulationBuildStage",
    "FCrowdDemoRoundTargetRegionTransportSolveStage",
    "FCrowdDemoRoundTargetRegionGuidanceStage",
    "FCrowdDemoRoundMovementWorkStage",
    "FCrowdDemoRoundParticleConstraintStage",
    "FCrowdDemoRoundObstacleConstraintStage",
    "FCrowdDemoRoundFacingFinalizeStage",
    "FCrowdDemoRoundPostFinalizeMetricsStage",
    "FCrowdDemoRoundAuthorityCommitStage",
    "FCrowdDemoRoundClientPredictionCommitStage",
    "FCrowdDemoRoundCheckpointPublisherStage",
]
for retired in retired_stages:
    if retired in h or retired in p:
        raise RuntimeError(f"retired stage type remains: {retired}")
if h.count("public UMassProcessor") != 2:
    raise RuntimeError(f"processor header expected 2 UMassProcessor classes, found {h.count('public UMassProcessor')}")
for required in [
    "ConfigureRoundPlanApplyQuery(InputSyncQuery)",
    "ExecuteRoundPlanApply(InputSyncQuery, EntityManager, Context)",
    "ConfigureWorkerResultApplyQuery(ResultCommitQuery)",
    "ExecuteWorkerResultApply(ResultCommitQuery, EntityManager, Context)",
    "FCrowdWorkerResultOwnerCommitBarrier::Commit(",
    "ExecuteRoundCheckpointPublisher(EntityManager, Context)",
]:
    if required not in p:
        raise RuntimeError(f"required helper path missing: {required}")
print("round stage struct removal patch applied")
