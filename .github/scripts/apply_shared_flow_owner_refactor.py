from pathlib import Path

HEADER = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.h")
CPP = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp")


def replace_exact(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)


header = HEADER.read_text(encoding="utf-8-sig")
header = replace_exact(
    header,
    "  const FCrowdDemoSharedFlowField& GetSharedFlowField() const { return SharedFlowField; }\n"
    "  const FCrowdSharedFlowField& GetRuntimeSharedFlowField() const\n"
    "  { return RuntimeSharedFlowResource.Field; }\n"
    "  int32 GetDynamicFlowAnchorCellKey() const { return DynamicFlowAnchorCellKey; }",
    "  const FCrowdDemoSharedFlowField& GetSharedFlowField() const { return SharedFlowField; }\n"
    "  int32 GetDynamicFlowAnchorCellKey() const { return DynamicFlowAnchorCellKey; }",
    1,
    "remove pipeline runtime flow getter",
)
header = replace_exact(
    header,
    "  FCrowdMassSharedFlowResource RuntimeSharedFlowResource;\n",
    "",
    1,
    "remove pipeline runtime flow member",
)
HEADER.write_text(header, encoding="utf-8")

cpp = CPP.read_text(encoding="utf-8-sig")
cpp = replace_exact(
    cpp,
    "  RuntimeSharedFlowResource.DynamicAnchorCellKey = INDEX_NONE;\n"
    "  RuntimeSharedFlowResource.IntegrationRebuildCount = 0;",
    "  UMassCrowdRuntimeSubsystem* SharedFlowRuntimeSubsystem =\n"
    "    GetWorld() ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()\n"
    "               : nullptr;\n"
    "  check(SharedFlowRuntimeSubsystem);\n"
    "  SharedFlowRuntimeSubsystem->ResetSharedFlowDynamicState();",
    2,
    "move primary flow dynamic reset to runtime subsystem",
)
cpp = replace_exact(
    cpp,
    "  const FCrowdMassSharedFlowBuildOutput Output =\n"
    "    FCrowdMassSharedFlowWork::EnsureResource(\n"
    "      Input, RuntimeSharedFlowResource);\n"
    "  if (!Output.bValid) return false;",
    "  UMassCrowdRuntimeSubsystem* SharedFlowRuntimeSubsystem =\n"
    "    GetWorld() ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()\n"
    "               : nullptr;\n"
    "  if (!SharedFlowRuntimeSubsystem) return false;\n"
    "  FCrowdMassSharedFlowBuildOutput Output;\n"
    "  if (!SharedFlowRuntimeSubsystem->EnsureSharedFlowResource(\n"
    "      Input, Output))\n"
    "    return false;\n"
    "  const FCrowdMassSharedFlowResource& RuntimeSharedFlowResource =\n"
    "    SharedFlowRuntimeSubsystem->GetSharedFlowResource();",
    2,
    "route primary flow builds through runtime subsystem",
)
cpp = replace_exact(
    cpp,
    "bool UCrowdDemoRoundSimPipelineSubsystem::PreparePendingTargetResourcePlan()\n"
    "{\n"
    "  check(IsInGameThread());\n"
    "    FCrowdDemoPreparedRoundCommitPlan* Pending =\n"
    "      PeekPreparedRoundCommitPlan();",
    "bool UCrowdDemoRoundSimPipelineSubsystem::PreparePendingTargetResourcePlan()\n"
    "{\n"
    "  check(IsInGameThread());\n"
    "  UMassCrowdRuntimeSubsystem* SharedFlowRuntimeSubsystem =\n"
    "    GetWorld() ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()\n"
    "               : nullptr;\n"
    "  if (!SharedFlowRuntimeSubsystem) return false;\n"
    "  const FCrowdMassSharedFlowResource& RuntimeSharedFlowResource =\n"
    "    SharedFlowRuntimeSubsystem->GetSharedFlowResource();\n"
    "    FCrowdDemoPreparedRoundCommitPlan* Pending =\n"
    "      PeekPreparedRoundCommitPlan();",
    1,
    "bind target prepare to runtime flow resource",
)
cpp = replace_exact(
    cpp,
    "bool UCrowdDemoRoundSimPipelineSubsystem::\n"
    "  FinalValidatePreparedTargetResourcePlan(\n"
    "    const FCrowdDemoPreparedTargetResourcePlan& Prepared) const\n"
    "{\n"
    "  if (!Prepared.bValid || Prepared.BuildCount != 1",
    "bool UCrowdDemoRoundSimPipelineSubsystem::\n"
    "  FinalValidatePreparedTargetResourcePlan(\n"
    "    const FCrowdDemoPreparedTargetResourcePlan& Prepared) const\n"
    "{\n"
    "  const UMassCrowdRuntimeSubsystem* SharedFlowRuntimeSubsystem =\n"
    "    GetWorld() ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()\n"
    "               : nullptr;\n"
    "  if (!SharedFlowRuntimeSubsystem) return false;\n"
    "  const FCrowdMassSharedFlowResource& RuntimeSharedFlowResource =\n"
    "    SharedFlowRuntimeSubsystem->GetSharedFlowResource();\n"
    "  if (!Prepared.bValid || Prepared.BuildCount != 1",
    1,
    "bind target final validation to runtime flow resource",
)
cpp = replace_exact(
    cpp,
    "  Prepared.CommitToken.ResourceId = reinterpret_cast<uint64>(\n"
    "    &RuntimeSharedFlowResource.Field);",
    "  Prepared.CommitToken.ResourceId =\n"
    "    CrowdWorkerResourceIds::Environment;",
    1,
    "use stable environment resource id in target commit token",
)
cpp = replace_exact(
    cpp,
    "    || Prepared.CommitToken.ResourceId != reinterpret_cast<uint64>(\n"
    "      &RuntimeSharedFlowResource.Field)",
    "    || Prepared.CommitToken.ResourceId\n"
    "      != CrowdWorkerResourceIds::Environment",
    1,
    "validate stable environment resource id",
)
cpp = replace_exact(
    cpp,
    "  DynamicFlowAnchorCellKey = Snapshot.DynamicFlowAnchorCellKey;\n"
    "  RuntimeSharedFlowResource.DynamicAnchorCellKey =\n"
    "    Snapshot.DynamicFlowAnchorCellKey;\n"
    "  DynamicFlowIntegrationRebuildCount = Snapshot.DynamicFlowIntegrationRebuildCount;\n"
    "  RuntimeSharedFlowResource.IntegrationRebuildCount =\n"
    "    Snapshot.DynamicFlowIntegrationRebuildCount;",
    "  DynamicFlowAnchorCellKey = Snapshot.DynamicFlowAnchorCellKey;\n"
    "  DynamicFlowIntegrationRebuildCount =\n"
    "    Snapshot.DynamicFlowIntegrationRebuildCount;\n"
    "  UMassCrowdRuntimeSubsystem* SharedFlowRuntimeSubsystem =\n"
    "    GetWorld() ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()\n"
    "               : nullptr;\n"
    "  check(SharedFlowRuntimeSubsystem);\n"
    "  check(SharedFlowRuntimeSubsystem->RestoreSharedFlowDynamicState(\n"
    "    Snapshot.DynamicFlowAnchorCellKey,\n"
    "    Snapshot.DynamicFlowIntegrationRebuildCount));",
    1,
    "restore primary flow dynamic state through runtime subsystem",
)

if "GetRuntimeSharedFlowField" in header:
    raise RuntimeError("legacy runtime flow getter remains in header")
if "FCrowdMassSharedFlowResource RuntimeSharedFlowResource;" in header:
    raise RuntimeError("legacy primary runtime flow member remains in header")
if "reinterpret_cast<uint64>(\n    &RuntimeSharedFlowResource.Field)" in cpp:
    raise RuntimeError("pointer-address target resource id remains")
if "reinterpret_cast<uint64>(\n      &RuntimeSharedFlowResource.Field)" in cpp:
    raise RuntimeError("pointer-address target resource validation remains")

CPP.write_text(cpp, encoding="utf-8")
print("shared-flow owner refactor patch applied successfully")
