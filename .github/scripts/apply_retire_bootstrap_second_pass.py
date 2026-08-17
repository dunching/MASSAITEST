from pathlib import Path

HEADER = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.h")
CPP = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp")
PROC = Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.cpp")
TESTS = Path("Source/MassAICrowdDemo/CrowdDemoCombatStateTests.cpp")


def read(path):
    return path.read_text(encoding="utf-8-sig")


def write(path, text):
    path.write_text(text, encoding="utf-8")


def brace_span(text, prefix):
    pos = text.find(prefix)
    if pos < 0:
        raise RuntimeError(f"missing block: {prefix}")
    brace = text.find("{", pos)
    if brace < 0:
        raise RuntimeError(f"missing brace: {prefix}")
    depth = 0
    i = brace
    in_string = in_char = escape = line_comment = block_comment = False
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
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
    raise RuntimeError(f"unbalanced block: {prefix}")


def remove_block(text, prefix):
    s, e = brace_span(text, prefix)
    return text[:s] + text[e:]


def remove_between(text, start, end, label):
    s = text.find(start)
    if s < 0: raise RuntimeError(f"{label}: start missing")
    e = text.find(end, s)
    if e < 0: raise RuntimeError(f"{label}: end missing")
    return text[:s] + text[e:]


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected 1, found {count}")
    return text.replace(old, new, 1)


def remove_test(text, test_name):
    marker = f"IMPLEMENT_SIMPLE_AUTOMATION_TEST(\n  {test_name},"
    s = text.find(marker)
    if s < 0: raise RuntimeError(f"test marker missing: {test_name}")
    e = text.find("IMPLEMENT_SIMPLE_AUTOMATION_TEST(", s + len(marker))
    if e < 0: raise RuntimeError(f"next test marker missing after: {test_name}")
    return text[:s] + text[e:]

# ---------------------------------------------------------------------------
# 1) Retire the dead second pass from bootstrap Movement / Particle / Facing.
# ---------------------------------------------------------------------------
p = read(PROC)

# Movement: the live bootstrap call only prepares the template. The synchronous
# bootstrap graph runs the actual Movement work immediately afterward.
ms, me = brace_span(p, "static void ExecuteRoundMovementWork(")
move = p[ms:me]
flow_start = move.find("  TArray<FCrowdGuidanceCandidate> FlowCandidates;\n")
flow_end = move.find("  const bool bGuidanceGatherValid =\n", flow_start)
if flow_start < 0 or flow_end < 0:
    raise RuntimeError("movement flow template range missing")
flow_template = '''  TArray<FCrowdGuidanceCandidate> FlowCandidates;
  FlowCandidates.Reserve(Pipeline->GetBoundarySnapshot().Agents.Num());
  for (const FCrowdMassBoundaryAgentRecord& Record
    : Pipeline->GetBoundarySnapshot().Agents)
  {
    FlowCandidates.Add(
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(
        FCrowdDemoGuidanceComposeKernel::BuildCandidate(
          Record.Identity.AgentId,
          ECrowdDemoGuidanceProvider::SharedFlow,
          Pipeline->GetCurrentPlanRevision(),
          FVector::ZeroVector, Record.State.Position,
          Record.State.YawDegrees, true)));
  }
'''
move = move[:flow_start] + flow_template + move[flow_end:]
second_start = move.find("  const TArray<FCrowdMassGatherRecord> GatherRecords =\n")
if second_start < 0:
    raise RuntimeError("movement second-pass marker missing")
move = move[:second_start] + '''  if (!Pipeline->StageBoundaryMovementWork(MoveTemp(WorkInput)))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementBootstrapStageRejected step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
  }
}
'''
p = p[:ms] + move + p[me:]

# Particle: build only the immutable particle template. Movement predictions,
# solving and publication come from the synchronous bootstrap graph / Worker.
ps, pe = brace_span(p, "static void ExecuteRoundParticleConstraint(")
particle = p[ps:pe]
map_start = particle.find("  TMap<int32, const FCrowdMassPredictedMovement*> PredictedByAgentId;\n")
map_end = particle.find("  bool bGatherValid = true;\n", map_start)
if map_start < 0 or map_end < 0:
    raise RuntimeError("particle legacy prepared maps missing")
particle = particle[:map_start] + particle[map_end:]
old_agent_prefix = '''    const FCrowdMassPredictedMovement* const* Predicted =
      PredictedByAgentId.Find(Base.Identity.AgentId);
    if (!bBuildingWorkerTemplate && (!Predicted || !(*Predicted)->bValid))
    {
      bGatherValid = false;
      continue;
    }
    bool bParticleActive = true;
    FVector StartPosition = Base.State.Position;
    FVector PredictedPosition = Base.State.Position;
    if (Predicted)
    {
      bParticleActive = (*Predicted)->bParticleActive;
      StartPosition = (*Predicted)->StartPosition;
      PredictedPosition = (*Predicted)->PredictedPosition;
    }
    else if (Pipeline->IsOpenSpawnRelaxation())
    {
      const FCrowdDemoPreparedOpenSpawnBoundaryFact* OpenSpawnFact =
        Pipeline->FindPreparedOpenSpawnBoundaryFact(Base.Identity.AgentId);
      if (!OpenSpawnFact)
      {
        bGatherValid = false;
        continue;
      }
      bParticleActive = OpenSpawnFact->bParticleActive;
    }
'''
new_agent_prefix = '''    bool bParticleActive = true;
    const FVector StartPosition = Base.State.Position;
    const FVector PredictedPosition = Base.State.Position;
    if (Pipeline->IsOpenSpawnRelaxation())
    {
      const FCrowdDemoPreparedOpenSpawnBoundaryFact* OpenSpawnFact =
        Pipeline->FindPreparedOpenSpawnBoundaryFact(Base.Identity.AgentId);
      if (!OpenSpawnFact)
      {
        bGatherValid = false;
        continue;
      }
      bParticleActive = OpenSpawnFact->bParticleActive;
    }
'''
particle = replace_once(
    particle, old_agent_prefix, new_agent_prefix,
    "particle agent template simplification")
particle = particle.replace(
    "  ParticlePipelineInput.PredictedMovements =\n"
    "    Pipeline->GetPreparedRuntimePredictedMovements();\n",
    "",
    1)
particle_second = particle.find("  const double StartSeconds = FPlatformTime::Seconds();\n")
if particle_second < 0:
    raise RuntimeError("particle second-pass marker missing")
particle = particle[:particle_second] + '''  if (!Pipeline->StageBoundaryParticleWork(MoveTemp(ParticlePipelineInput)))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoParticleBootstrapStageRejected step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
  }
}
'''
p = p[:ps] + particle + p[pe:]

# Facing: always prepare the bootstrap-facing template and dispatch the local
# synchronous graph once. Do not Consume/reset it or build a legacy commit plan.
fs, fe = brace_span(p, "static void ExecuteRoundFacingBootstrap(")
facing = p[fs:fe]
facing = facing.replace(
    "  if (Pipeline->IsPreparedMovementBoundaryCommitCurrent())\n    return;\n\n",
    "",
    1)
consume_start = facing.find("  FCrowdMassFacingFinalizeWorkOutput CombinedOutput;\n")
consume_if = facing.find("  if (!Pipeline->ConsumeBoundaryFacingWork(\n", consume_start)
if consume_start < 0 or consume_if < 0:
    raise RuntimeError("facing consume preamble missing")
open_brace = facing.find("{", consume_if)
# Find the exact close for the consume if using local brace scanning.
def local_matching_brace(text, brace):
    depth = 0; i = brace
    ins = inc = esc = line = block = False
    while i < len(text):
        c=text[i]; n=text[i+1] if i+1 < len(text) else ""
        if line:
            if c=='\n': line=False
            i+=1; continue
        if block:
            if c=='*' and n=='/': block=False; i+=2; continue
            i+=1; continue
        if ins:
            if esc: esc=False
            elif c=='\\': esc=True
            elif c=='"': ins=False
            i+=1; continue
        if inc:
            if esc: esc=False
            elif c=='\\': esc=True
            elif c=="'": inc=False
            i+=1; continue
        if c=='/' and n=='/': line=True; i+=2; continue
        if c=='/' and n=='*': block=True; i+=2; continue
        if c=='"': ins=True; i+=1; continue
        if c=="'": inc=True; i+=1; continue
        if c=='{': depth+=1
        elif c=='}':
            depth-=1
            if depth==0: return i
        i+=1
    raise RuntimeError("local brace mismatch")
close_brace = local_matching_brace(facing, open_brace)
inner = facing[open_brace + 1:close_brace]
# The nested first-pass branch ends with `return;`; remove it and dedent once.
if not inner.rstrip().endswith("return;"):
    raise RuntimeError("facing first pass does not end in return")
inner = inner.rstrip()
inner = inner[:-len("return;")].rstrip() + "\n"
inner_lines=[]
for line in inner.splitlines(True):
    inner_lines.append(line[2:] if line.startswith("  ") else line)
inner = "".join(inner_lines)
facing = facing[:consume_start] + inner + "}\n"
# Since this is now always a bootstrap template, strip prepared-result maps and
# force the template branch.
facing = facing.replace(
    "  TMap<int32, const FCrowdComposedGuidance*> ComposedByAgentId;\n"
    "  for (const FCrowdComposedGuidance& Value\n"
    "    : Pipeline->GetPreparedRuntimeComposedGuidance())\n"
    "    ComposedByAgentId.Add(Value.AgentId, &Value);\n"
    "  TMap<int32, const FCrowdParticleConstraintResult*> ParticleByAgentId;\n"
    "  for (const FCrowdParticleConstraintResult& Value\n"
    "    : Pipeline->GetPreparedRuntimeParticleResults())\n"
    "    ParticleByAgentId.Add(Value.AgentId, &Value);\n",
    "",
    1)
facing = facing.replace(
    "  const bool bBuildingBoundaryGraph =\n"
    "    ComposedByAgentId.IsEmpty()\n"
    "    && (bUsesParticle\n"
    "      ? ParticleByAgentId.IsEmpty()\n"
    "      : Pipeline->GetPreparedRuntimeFinalKinematics().IsEmpty());\n",
    "",
    1)
facing = facing.replace(
    "    const FCrowdComposedGuidance* const* Composed =\n"
    "      ComposedByAgentId.Find(Base.Identity.AgentId);\n"
    "    const FCrowdParticleConstraintResult* const* Particle =\n"
    "      ParticleByAgentId.Find(Base.Identity.AgentId);\n"
    "    if ((!bBuildingBoundaryGraph && !Composed)\n"
    "      || (bUsesParticle && !bBuildingBoundaryGraph && !Particle)\n"
    "      || !PreviousSettleStepsByAgentId.Contains(Base.Identity.AgentId))\n",
    "    if (!PreviousSettleStepsByAgentId.Contains(Base.Identity.AgentId))\n",
    1)
facing = facing.replace(
    "    const bool bSettledThisStep = !bBuildingBoundaryGraph\n"
    "      && bTerminalOwner && bUsesParticle\n"
    "      && FVector2f((*Particle)->CorrectedVelocity.X,\n"
    "        (*Particle)->CorrectedVelocity.Y).Size() <= 20.0f\n"
    "      && (*Particle)->RealizedCorrection.Size2D() <= 1.0f;\n",
    "    const bool bSettledThisStep = false;\n",
    1)
facing = facing.replace(
    "    Input.AutonomousPreferredVelocity = bBuildingBoundaryGraph\n"
    "      ? FVector2f::ZeroVector\n"
    "      : FVector2f((*Composed)->AutonomousPreferredVelocity.X,\n"
    "          (*Composed)->AutonomousPreferredVelocity.Y);\n"
    "    const FVector FacingLocation = bUsesParticle\n"
    "      && !bBuildingBoundaryGraph\n"
    "      ? (*Particle)->CorrectedPosition : Base.State.Position;\n",
    "    Input.AutonomousPreferredVelocity = FVector2f::ZeroVector;\n"
    "    const FVector FacingLocation = Base.State.Position;\n",
    1)
old_dispatch = '''  if (bBuildingBoundaryGraph)
  {
    const bool bDispatched = bUsesParticle
      ? Pipeline->DispatchBoundarySoftPressureWorkGraph(
          MoveTemp(CombinedInput),
          MoveTemp(PreviousSettleStepsByAgentId),
          MoveTemp(TerminalOwnerByAgentId))
      : Pipeline->DispatchBoundaryFacingWork(
          MoveTemp(CombinedInput),
          MoveTemp(ConsecutiveSettleStepsByAgentId),
          MoveTemp(FinalSettledByAgentId));
    if (!bDispatched)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoBoundaryWorkGraphDispatchRejected step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
    }
  }
  else
  {
    CombinedInput.Kinematics = Pipeline->GetPreparedRuntimeFinalKinematics();
    if (!Pipeline->DispatchBoundaryFacingWork(
          MoveTemp(CombinedInput),
          MoveTemp(ConsecutiveSettleStepsByAgentId),
          MoveTemp(FinalSettledByAgentId)))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoFacingWorkDispatchRejected step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
    }
  }
'''
new_dispatch = '''  const bool bDispatched = bUsesParticle
    ? Pipeline->DispatchBoundarySoftPressureWorkGraph(
        MoveTemp(CombinedInput),
        MoveTemp(PreviousSettleStepsByAgentId),
        MoveTemp(TerminalOwnerByAgentId))
    : Pipeline->DispatchBoundaryFacingWork(
        MoveTemp(CombinedInput),
        MoveTemp(ConsecutiveSettleStepsByAgentId),
        MoveTemp(FinalSettledByAgentId));
  if (!bDispatched)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBoundaryWorkGraphDispatchRejected step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
  }
'''
facing = replace_once(facing, old_dispatch, new_dispatch, "facing dispatch collapse")
p = p[:fs] + facing + p[fe:]

# WorkerResult prepared plan no longer carries a legacy Target/Resource commit.
p = p.replace(
    "    Pending.PreparedTargetResourcePlan =\n"
    "      MakeShared<FCrowdDemoPreparedTargetResourcePlan>();\n",
    "",
    1)
write(PROC, p)

# ---------------------------------------------------------------------------
# 2) Delete dead transaction payloads/APIs/members from the Pipeline.
# ---------------------------------------------------------------------------
h = read(HEADER)
h = h.replace("struct FCrowdDemoPreparedTargetResourcePlan;\n", "", 1)
h = h.replace(
    "  TSharedPtr<FCrowdDemoPreparedTargetResourcePlan>\n"
    "    PreparedTargetResourcePlan;\n",
    "",
    1)
h = h.replace(
    "      && PreparedTargetResourcePlan.IsValid()\n",
    "",
    1)
h = remove_between(
    h,
    "struct FCrowdDemoPreparedParticleDiagnosticCommit\n{",
    "struct FCrowdDemoTargetRegionCapabilityCohortRuntime\n{",
    "remove particle diagnostic commit type")
h = remove_between(
    h,
    "struct FCrowdDemoTargetResourceCommitToken\n{",
    "struct FCrowdDemoPreparedSteeringGuidance\n{",
    "remove target resource transaction types")
h = remove_between(
    h,
    "struct FCrowdDemoPreparedMovementBoundaryCommit\n{",
    "struct FCrowdDemoWorkerMovementTailExecution\n{",
    "remove prepared movement commit type")
# Remove obsolete boundary consume + legacy commit APIs, while keeping bootstrap
# staging and worker submit APIs.
for snippet in [
    "  bool ConsumeBoundaryMovementWork(\n    FCrowdMassMovementPipelineWorkOutput& OutOutput);\n",
    "  bool ConsumeBoundaryParticleWork(\n    FCrowdMassParticlePipelineWorkOutput& OutOutput);\n",
    "  bool ConsumeBoundaryFacingWork(\n    FCrowdMassFacingFinalizeWorkOutput& OutOutput,\n    TMap<int32, int32>& OutConsecutiveSettleStepsByAgentId,\n    TMap<int32, bool>& OutFinalSettledByAgentId);\n",
]:
    if snippet not in h: raise RuntimeError(f"consume declaration missing: {snippet[:35]}")
    h = h.replace(snippet, "", 1)
# Prepared movement / Target resource / movement finalize channel.
start = h.find("  bool SetPreparedMovementBoundaryCommit(\n")
end = h.find("  void SetPreparedRuntimeSharedFlowOutputs(\n", start)
if start < 0 or end < 0: raise RuntimeError("prepared movement/target API range missing")
h = h[:start] + h[end:]
# Prepared combat member API is also dead; the type remains inside BusinessOutput.
start = h.find("  bool SetPreparedCombatBoundaryCommit(\n")
end = h.find("  void SetPreparedRuntimeComposedGuidance(\n", start)
if start < 0 or end < 0: raise RuntimeError("prepared combat API range missing")
h = h[:start] + h[end:]
# Runtime second-pass arrays are no longer part of the bootstrap contract.
start = h.find("  void SetPreparedRuntimeSharedFlowOutputs(\n")
end = h.find("  void SetPreparedTargetRegionGuidanceCandidates(\n", start)
if start < 0 or end < 0: raise RuntimeError("prepared shared-flow API range missing")
h = h[:start] + h[end:]
start = h.find("  void SetPreparedRuntimeComposedGuidance(\n")
end = h.find("  bool IsRangedProjectileCombat() const\n", start)
if start < 0 or end < 0: raise RuntimeError("prepared runtime tail API range missing")
h = h[:start] + h[end:]
# Members.
for line in [
    "  int32 MovementFinalizeAppliedFixedStepIndex = INDEX_NONE;\n",
    "  TArray<FCrowdDemoBoundaryFacingWorkState::FTargetTopologySlot>\n    PreparedTargetResourceSlots;\n",
    "  FCrowdDemoPreparedMovementBoundaryCommit PreparedMovementBoundaryCommit;\n",
    "  TArray<FCrowdMassSharedFlowAgentOutput> PreparedRuntimeSharedFlowOutputs;\n",
    "  FCrowdDemoPreparedCombatBoundaryCommit PreparedCombatBoundaryCommit;\n",
    "  TArray<FCrowdComposedGuidance> PreparedRuntimeComposedGuidance;\n",
    "  TArray<FCrowdMassPredictedMovement> PreparedRuntimePredictedMovements;\n",
    "  TArray<FCrowdParticleConstraintResult> PreparedRuntimeParticleResults;\n",
    "  TArray<FCrowdMassFinalKinematicState> PreparedRuntimeFinalKinematics;\n",
    "  bool bPreparedRuntimeFinalKinematicsWorkerOwned = false;\n",
    "  TArray<FCrowdFacingResult> PreparedRuntimeFacingResults;\n",
    "  TArray<FCrowdDemoPreparedFacingRollbackFact> PreparedFacingRollbackFacts;\n",
    "  FCrowdDemoPreparedParticleDiagnosticCommit PreparedParticleDiagnosticCommit;\n",
]:
    if line not in h: raise RuntimeError(f"member missing: {line.strip()}")
    h = h.replace(line, "", 1)
write(HEADER, h)

c = read(CPP)
for prefix in [
    "bool UCrowdDemoRoundSimPipelineSubsystem::ConsumeBoundaryMovementWork(",
    "bool UCrowdDemoRoundSimPipelineSubsystem::ConsumeBoundaryParticleWork(",
    "bool UCrowdDemoRoundSimPipelineSubsystem::ConsumeBoundaryFacingWork(",
    "bool UCrowdDemoRoundSimPipelineSubsystem::SetPreparedMovementBoundaryCommit(",
    "bool FCrowdDemoPreparedTargetResourcePlan::ValidatePrepareInput(",
    "bool UCrowdDemoRoundSimPipelineSubsystem::PreparePendingTargetResourcePlan(",
    "bool UCrowdDemoRoundSimPipelineSubsystem::\n  FinalValidatePreparedTargetResourcePlan(",
    "void UCrowdDemoRoundSimPipelineSubsystem::\n  ApplyPreparedTargetResourcePlanNoFail(",
    "bool UCrowdDemoRoundSimPipelineSubsystem::SetPreparedCombatBoundaryCommit(",
    "bool UCrowdDemoRoundSimPipelineSubsystem::SetPreparedParticleDiagnosticCommit(",
    "bool UCrowdDemoRoundSimPipelineSubsystem::CommitPreparedParticleDiagnostics(",
]:
    c = remove_block(c, prefix)
# Remove stale reset statements for deleted members.
for statement in [
    "  MovementFinalizeAppliedFixedStepIndex = INDEX_NONE;\n",
    "  PreparedTargetResourceSlots.Reset();\n",
    "  PreparedMovementBoundaryCommit = {};\n",
    "  PreparedRuntimeSharedFlowOutputs.Reset();\n",
    "  PreparedCombatBoundaryCommit = {};\n",
    "  PreparedRuntimeComposedGuidance.Reset();\n",
    "  PreparedRuntimePredictedMovements.Reset();\n",
    "  PreparedRuntimeParticleResults.Reset();\n",
    "  PreparedRuntimeFinalKinematics.Reset();\n",
    "  bPreparedRuntimeFinalKinematicsWorkerOwned = false;\n",
    "  PreparedRuntimeFacingResults.Reset();\n",
    "  PreparedFacingRollbackFacts.Reset();\n",
    "  PreparedParticleDiagnosticCommit = {};\n",
]:
    c = c.replace(statement, "")
write(CPP, c)

# ---------------------------------------------------------------------------
# 3) Update architecture tests: generic Owner Barrier stays; retired Demo
# transaction/second-pass contracts are asserted absent instead of exercised.
# ---------------------------------------------------------------------------
t = read(TESTS)
# Remove Target/Resource-specific owner-barrier fixture, leaving generic barrier
# atomicity tests intact.
target_start = t.find("  FCrowdDemoTargetResourcePrepareValidationInput TargetPrepareInput;\n")
target_end = t.find("  FCrowdWorkerResultApplyProxy SuccessProxy;\n", target_start)
if target_start < 0 or target_end < 0:
    raise RuntimeError("target resource barrier fixture range missing")
t = t[:target_start] + t[target_end:]
t = t.replace(
    "  const int32 PreparedTargetResourcePlanBuildCount = 1;\n"
    "  int32 PreparedTargetResourcePlanApplyCount = 0;\n",
    "",
    1)
t = t.replace(
    "         return PreparedMassPlanBuildCount == 1\n"
    "           && PreparedTargetResourcePlanBuildCount == 1;\n",
    "         return PreparedMassPlanBuildCount == 1;\n",
    1)
t = t.replace(
    "         ++PreparedTargetResourcePlanApplyCount;\n",
    "",
    1)
t = t.replace(
    "  TestEqual(TEXT(\"Prepared Target/Resource Plan is built once\"),\n"
    "    PreparedTargetResourcePlanBuildCount, 1);\n"
    "  TestEqual(TEXT(\"Prepared Target/Resource Plan is applied once\"),\n"
    "    PreparedTargetResourcePlanApplyCount, 1);\n",
    "",
    1)
# Replace the large stale source-symbol test with a compact current-architecture gate.
old_test = "FCrowdDemoPostFinalizeMinimalQueryStructureTest"
marker = f"IMPLEMENT_SIMPLE_AUTOMATION_TEST(\n  {old_test},"
s = t.find(marker)
if s < 0: raise RuntimeError("old architecture structure test missing")
e = t.find("IMPLEMENT_SIMPLE_AUTOMATION_TEST(", s + len(marker))
if e < 0: raise RuntimeError("next test after old structure test missing")
new_test = r'''IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoPersistentWorkerProductionStructureTest,
  "CrowdDemo.Architecture.PersistentWorkerProductionStructure",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPersistentWorkerProductionStructureTest::RunTest(
  const FString& Parameters)
{
  FString ProcessorSource;
  FString ProcessorHeader;
  FString PipelineSource;
  FString PipelineHeader;
  FString OwnerBarrierSource;
  TestTrue(TEXT("processor source is readable"),
    FFileHelper::LoadFileToString(ProcessorSource, *FPaths::Combine(
      FPaths::ProjectDir(), TEXT(
        "Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.cpp"))));
  TestTrue(TEXT("processor header is readable"),
    FFileHelper::LoadFileToString(ProcessorHeader, *FPaths::Combine(
      FPaths::ProjectDir(), TEXT(
        "Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.h"))));
  TestTrue(TEXT("pipeline source is readable"),
    FFileHelper::LoadFileToString(PipelineSource, *FPaths::Combine(
      FPaths::ProjectDir(), TEXT(
        "Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"))));
  TestTrue(TEXT("pipeline header is readable"),
    FFileHelper::LoadFileToString(PipelineHeader, *FPaths::Combine(
      FPaths::ProjectDir(), TEXT(
        "Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.h"))));
  TestTrue(TEXT("Runtime Owner Barrier source is readable"),
    FFileHelper::LoadFileToString(OwnerBarrierSource, *FPaths::Combine(
      FPaths::ProjectDir(), TEXT(
        "Plugins/MassCrowdSimulation/Source/MassCrowdRuntime/Private/MassCrowdWorkerResultApply.cpp"))));

  int32 ProcessorClassCount = 0;
  int32 SearchFrom = 0;
  const FString ProcessorMarker = TEXT("public UMassProcessor");
  while (true)
  {
    const int32 Found = ProcessorHeader.Find(
      ProcessorMarker, ESearchCase::CaseSensitive,
      ESearchDir::FromStart, SearchFrom);
    if (Found == INDEX_NONE) break;
    ++ProcessorClassCount;
    SearchFrom = Found + ProcessorMarker.Len();
  }
  TestEqual(TEXT("Demo runtime owns exactly InputSync and ResultApply processors"),
    ProcessorClassCount, 2);

  const FString ProductionSource = ProcessorSource + PipelineSource
    + ProcessorHeader + PipelineHeader;
  const TCHAR* RetiredSymbols[] = {
    TEXT("FCrowdDemoRoundWorkBatch"),
    TEXT("BeginBoundaryTransaction"),
    TEXT("TryPrepareRoundApply"),
    TEXT("BoundaryOrchestrator"),
    TEXT("ECrowdBoundaryTransactionState"),
    TEXT("FCrowdDemoPreparedMovementBoundaryCommit"),
    TEXT("FCrowdDemoPreparedTargetResourcePlan"),
    TEXT("FCrowdDemoPreparedParticleDiagnosticCommit"),
    TEXT("ConsumeBoundaryMovementWork"),
    TEXT("ConsumeBoundaryParticleWork"),
    TEXT("ConsumeBoundaryFacingWork"),
    TEXT("FCrowdDemoRoundPostFinalizeMetricsStage"),
    TEXT("FCrowdDemoRoundAuthorityCommitStage"),
    TEXT("FCrowdDemoRoundClientPredictionCommitStage")
  };
  for (const TCHAR* Symbol : RetiredSymbols)
  {
    TestFalse(FString::Printf(TEXT("retired production symbol %s is absent"),
      Symbol), ProductionSource.Contains(Symbol));
  }
  TestTrue(TEXT("bootstrap preparation is one-shot and synchronous"),
    PipelineSource.Contains(TEXT("FCrowdDemoBootstrapSynchronousGraph"))
      && PipelineSource.Contains(TEXT("BeginWorkerBootstrapPreparation("))
      && PipelineSource.Contains(TEXT("SubmitPreparedWorkerBootstrapInput()"))
      && !PipelineSource.Contains(TEXT("CrowdDemoRoundWork\"")));
  TestTrue(TEXT("server commit remains behind Runtime Owner Barrier"),
    ProcessorSource.Contains(TEXT(
      "FCrowdWorkerResultOwnerCommitBarrier::Commit("))
      && ProcessorSource.Contains(TEXT(
        "ApplyValidatedWorkerMassDirtyPlan("))
      && ProcessorSource.Contains(TEXT(
        "MarkCurrentStepWorkerDirtyMassApplied(")));
  const int32 OwnerCommit = ProcessorSource.Find(TEXT(
    "FCrowdWorkerResultOwnerCommitBarrier::Commit("));
  const int32 Checkpoint = ProcessorSource.Find(TEXT(
    "ExecuteRoundCheckpointPublisher(EntityManager, Context)"),
    ESearchCase::CaseSensitive, ESearchDir::FromStart, OwnerCommit);
  TestTrue(TEXT("checkpoint publication follows Worker owner commit"),
    OwnerCommit != INDEX_NONE && Checkpoint > OwnerCommit);
  TestTrue(TEXT("ordinary Production submits intent without rebuilding Round DAG"),
    PipelineSource.Contains(TEXT("TrySubmitFullWorkerProductionIntent()"))
      && PipelineSource.Contains(TEXT(
        "FCrowdDemoWorkerInputSync::SubmitIntentBatch(")));
  TestTrue(TEXT("Runtime barrier commits host write before Proxy and side effects"),
    OwnerBarrierSource.Contains(TEXT("HostApplyNoFail()"))
      && OwnerBarrierSource.Contains(TEXT("CommitPreparedValidated(")));
  return true;
}

'''
t = t[:s] + new_test + t[e:]
# Update the final particle architecture assertions to the one-pass bootstrap model.
old_particle_assert = '''  TestFalse(TEXT("particle processor has no Mass query seam"),
    ProcessorSource.Contains(
      TEXT("void FCrowdDemoRoundParticleConstraintStage::ConfigureQueries")));
  const FString ParticleExecuteMarker =
    TEXT("void FCrowdDemoRoundParticleConstraintStage::Execute");
  const FString ObstacleConstructorMarker =
    TEXT("FCrowdDemoRoundObstacleConstraintStage::");
  const int32 ParticleExecuteStart =
    ProcessorSource.Find(ParticleExecuteMarker);
  const int32 ObstacleConstructorStart = ProcessorSource.Find(
    ObstacleConstructorMarker, ESearchCase::CaseSensitive,
    ESearchDir::FromStart, ParticleExecuteStart + ParticleExecuteMarker.Len());
  TestTrue(TEXT("particle execute block found"),
    ParticleExecuteStart != INDEX_NONE
      && ObstacleConstructorStart > ParticleExecuteStart);
  if (ParticleExecuteStart != INDEX_NONE
    && ObstacleConstructorStart > ParticleExecuteStart)
  {
    const FString ParticleExecuteBlock = ProcessorSource.Mid(
      ParticleExecuteStart, ObstacleConstructorStart - ParticleExecuteStart);
    const TCHAR* DeferredParticleSideEffects[] = {
      TEXT("RecordParticleConstraintSummary("),
      TEXT("RecordParticleFailureFixture("),
      TEXT("RecordCrossProfileParticleViolations("),
      TEXT("RecordSoftPressureRouteStep("),
      TEXT("RecordTargetStabilityStep("),
      TEXT("RecordOpenSpawnRelaxationParticleStep(")
    };
    for (const TCHAR* SideEffect : DeferredParticleSideEffects)
      TestFalse(FString::Printf(TEXT("particle solve defers %s"), SideEffect),
        ParticleExecuteBlock.Contains(SideEffect));
    TestTrue(TEXT("particle solve prepares one diagnostic commit"),
      ParticleExecuteBlock.Contains(
        TEXT("SetPreparedParticleDiagnosticCommit(")));
  }
  TestTrue(TEXT("post-finalize commits prepared particle diagnostics"),
    ProcessorSource.Contains(
      TEXT("FCrowdDemoRoundPostFinalizeMetricsStage::Execute"))
      && ProcessorSource.Contains(
        TEXT("CommitPreparedParticleDiagnostics()")));
'''
new_particle_assert = '''  TestTrue(TEXT("particle bootstrap only stages immutable Worker input"),
    ProcessorSource.Contains(TEXT("static void ExecuteRoundParticleConstraint("))
      && ProcessorSource.Contains(TEXT("StageBoundaryParticleWork(")));
  TestFalse(TEXT("legacy particle second-pass diagnostic commit is deleted"),
    ProcessorSource.Contains(TEXT("SetPreparedParticleDiagnosticCommit("))
      || PipelineSource.Contains(TEXT("CommitPreparedParticleDiagnostics("))
      || PipelineSource.Contains(TEXT("PreparedParticleDiagnosticCommit")));
'''
if old_particle_assert not in t:
    raise RuntimeError("legacy particle architecture assertion block missing")
t = t.replace(old_particle_assert, new_particle_assert, 1)
write(TESTS, t)

# ---------------------------------------------------------------------------
# 4) Structural gates.
# ---------------------------------------------------------------------------
h = read(HEADER); c = read(CPP); p = read(PROC); t = read(TESTS)
production = h + c + p
retired = [
    "FCrowdDemoRoundWorkBatch",
    "BeginBoundaryTransaction",
    "TryPrepareRoundApply",
    "BoundaryOrchestrator",
    "ECrowdBoundaryTransactionState",
    "FCrowdDemoPreparedMovementBoundaryCommit",
    "FCrowdDemoPreparedTargetResourcePlan",
    "FCrowdDemoTargetResourceCommitToken",
    "FCrowdDemoPreparedParticleDiagnosticCommit",
    "ConsumeBoundaryMovementWork",
    "ConsumeBoundaryParticleWork",
    "ConsumeBoundaryFacingWork",
    "SetPreparedMovementBoundaryCommit",
    "PreparePendingTargetResourcePlan",
    "FinalValidatePreparedTargetResourcePlan",
    "ApplyPreparedTargetResourcePlanNoFail",
    "SetPreparedCombatBoundaryCommit",
    "PreparedCombatBoundaryCommit",
    "PreparedRuntimeSharedFlowOutputs",
    "PreparedRuntimeComposedGuidance",
    "PreparedRuntimePredictedMovements",
    "PreparedRuntimeParticleResults",
    "PreparedRuntimeFinalKinematics",
    "PreparedRuntimeFacingResults",
    "PreparedFacingRollbackFacts",
    "PreparedParticleDiagnosticCommit",
]
for symbol in retired:
    if symbol in production:
        lines = [x for x in production.splitlines() if symbol in x][:8]
        raise RuntimeError(f"retired symbol remains {symbol}: {lines}")
if h.count("PreparedTargetResourcePlan") != 0:
    raise RuntimeError("PreparedRoundCommitPlan still carries target resource plan")
if "FCrowdWorkerResultOwnerCommitBarrier::Commit(" not in p:
    raise RuntimeError("Runtime Owner Barrier missing")
if "SubmitPreparedWorkerBootstrapInput()" not in p:
    raise RuntimeError("bootstrap submit call missing")
if "FCrowdDemoBootstrapSynchronousGraph" not in c:
    raise RuntimeError("synchronous bootstrap graph missing")
if "StageBoundaryMovementWork(MoveTemp(WorkInput))" not in p:
    raise RuntimeError("movement bootstrap staging missing")
if "StageBoundaryParticleWork(MoveTemp(ParticlePipelineInput))" not in p:
    raise RuntimeError("particle bootstrap staging missing")
if "DispatchBoundarySoftPressureWorkGraph(" not in p:
    raise RuntimeError("facing bootstrap dispatch missing")
# Old source-string tests must be gone too, except the new explicit absence list.
for obsolete_compile_symbol in [
    "FCrowdDemoTargetResourcePrepareValidationInput TargetPrepareInput",
    "TargetRevisionToken.Matches(",
    "PreparedTargetResourcePlanBuildCount",
]:
    if obsolete_compile_symbol in t:
        raise RuntimeError(f"obsolete test fixture remains: {obsolete_compile_symbol}")
print("bootstrap second-pass retirement patch applied")
