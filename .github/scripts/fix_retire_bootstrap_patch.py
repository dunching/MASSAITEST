from pathlib import Path

path = Path(".github/scripts/apply_retire_bootstrap_second_pass.py")
text = path.read_text(encoding="utf-8")

# Replace the entire historical Facing transformation with a deterministic
# one-shot bootstrap function. It prepares only immutable facing input and then
# dispatches the synchronous bootstrap graph; there is no second consume pass.
facing_start = text.find(
    'fs, fe = brace_span(p, "static void ExecuteRoundFacingBootstrap(")')
facing_end_marker = 'p = p[:fs] + facing + p[fe:]\n'
facing_end = text.find(facing_end_marker, facing_start)
if facing_start < 0 or facing_end < 0:
    raise RuntimeError("legacy Facing transformation block missing")
facing_end += len(facing_end_marker)
facing_patch = r'''fs, fe = brace_span(p, "static void ExecuteRoundFacingBootstrap(")
facing = r''' + "'''" + r'''static void ExecuteRoundFacingBootstrap(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive()) return;
  if (!Pipeline->IsBoundarySnapshotCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingBoundarySnapshotInvalid step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }

  TMap<int32, ECrowdDemoTargetRegionGuidanceMode> GuidanceModeByAgentId;
  if (Pipeline->IsTargetRegionExecutionActive())
  {
    if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
    {
      for (const auto& Runtime : Pipeline->GetCapabilityCohorts())
        for (const auto& Guidance : Runtime.Guidance)
          GuidanceModeByAgentId.Add(Guidance.AgentId, Guidance.Mode);
    }
    else
    {
      for (const auto& Guidance : Pipeline->GetPreparedTargetRegionGuidance())
        GuidanceModeByAgentId.Add(Guidance.AgentId, Guidance.Mode);
    }
  }

  const bool bUsesParticle = Pipeline->GetRules().Scenario
    == ECrowdDemoScenario::SimRoundSoftPressure;
  TMap<int32, int32> PreviousSettleStepsByAgentId;
  for (const FCrowdDemoRoundBoundaryFacingFact& Facing
    : Pipeline->GetBoundaryFacingFacts())
  {
    PreviousSettleStepsByAgentId.Add(
      Facing.AgentId, Facing.ConsecutiveFinalSettleSteps);
  }

  FCrowdMassFacingFinalizeWorkInput CombinedInput;
  FCrowdMassFacingWorkInput& WorkInput = CombinedInput.Facing;
  WorkInput.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  WorkInput.PlanRevision = Pipeline->GetCurrentPlanRevision();
  WorkInput.Settings.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
  CombinedInput.Snapshot = Pipeline->GetBoundarySnapshot();

  TMap<int32, int32> ConsecutiveSettleStepsByAgentId;
  TMap<int32, bool> FinalSettledByAgentId;
  TMap<int32, bool> TerminalOwnerByAgentId;
  bool bGatherValid = true;
  for (const FCrowdMassBoundaryAgentRecord& Base
    : Pipeline->GetBoundarySnapshot().Agents)
  {
    if (!PreviousSettleStepsByAgentId.Contains(Base.Identity.AgentId))
    {
      bGatherValid = false;
      continue;
    }
    const ECrowdDemoTargetRegionGuidanceMode* Mode =
      GuidanceModeByAgentId.Find(Base.Identity.AgentId);
    const bool bTerminalOwner = Mode
      && (*Mode == ECrowdDemoTargetRegionGuidanceMode::TerminalSettle
        || *Mode == ECrowdDemoTargetRegionGuidanceMode::EngagedHold);
    TerminalOwnerByAgentId.Add(Base.Identity.AgentId, bTerminalOwner);
    ConsecutiveSettleStepsByAgentId.Add(Base.Identity.AgentId, 0);
    FinalSettledByAgentId.Add(Base.Identity.AgentId, false);

    FCrowdFacingInput& Input = WorkInput.Agents.AddDefaulted_GetRef();
    Input.AgentId = Base.Identity.AgentId;
    Input.CurrentYawDegrees = Base.State.YawDegrees;
    Input.AutonomousPreferredVelocity = FVector2f::ZeroVector;
    Input.Location = FVector2f(Base.State.Position.X, Base.State.Position.Y);
    Input.TargetLocation = FVector2f(
      Pipeline->GetTargetFact().Location.X,
      Pipeline->GetTargetFact().Location.Y);
    Input.bHasTarget = Pipeline->IsTargetRegionExecutionActive();
    Input.bFinalPositionSettled = false;
  }
  WorkInput.Agents.Sort([](const FCrowdFacingInput& A,
    const FCrowdFacingInput& B)
  {
    return A.AgentId < B.AgentId;
  });
  if (!bGatherValid || WorkInput.Agents.IsEmpty())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingGatherInvalid step=%d agents=%d"),
      Pipeline->GetCurrentFixedStepIndex(), WorkInput.Agents.Num());
    return;
  }

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
''' + "'''" + r'''
p = p[:fs] + facing + p[fe:]
'''
text = text[:facing_start] + facing_patch + text[facing_end:]

# Remove the obsolete Target/Resource prepared hash helper too.
needle = 'c = read(CPP)\nfor prefix in [\n'
if needle not in text:
    raise RuntimeError("pipeline cleanup insertion marker missing")
text = text.replace(
    needle,
    'c = read(CPP)\n'
    'c = remove_block(c, "  uint64 CalculatePreparedTargetResourceHash(")\n'
    'for prefix in [\n',
    1)
gate = '    "FCrowdDemoPreparedTargetResourcePlan",\n'
if gate not in text:
    raise RuntimeError("retired gate insertion marker missing")
text = text.replace(
    gate,
    gate + '    "CalculatePreparedTargetResourceHash",\n',
    1)

# Retire only the Pipeline Combat member/API. The one-shot bootstrap
# BusinessOutput still legitimately uses the value type.
wide_gate = '    "PreparedCombatBoundaryCommit",\n'
if wide_gate not in text:
    raise RuntimeError("wide prepared combat gate missing")
text = text.replace(wide_gate, "", 1)
member_gate_anchor = 'if h.count("PreparedTargetResourcePlan") != 0:\n    raise RuntimeError("PreparedRoundCommitPlan still carries target resource plan")\n'
if member_gate_anchor not in text:
    raise RuntimeError("combat member gate anchor missing")
text = text.replace(
    member_gate_anchor,
    member_gate_anchor
    + 'if "FCrowdDemoPreparedCombatBoundaryCommit PreparedCombatBoundaryCommit;" in h:\n'
      '    raise RuntimeError("prepared combat pipeline member remains")\n',
    1)

# The legacy PostFinalizeMinimalQuery test is the terminal automation test.
old = '''e = t.find("IMPLEMENT_SIMPLE_AUTOMATION_TEST(", s + len(marker))
if e < 0: raise RuntimeError("next test after old structure test missing")
'''
new_test_end = '''e = t.find("\\n#endif", s + len(marker))
if e < 0: raise RuntimeError("terminal #endif after old structure test missing")
'''
if old not in text:
    raise RuntimeError("old architecture test end finder missing")
text = text.replace(old, new_test_end, 1)

# Particle assertions were inside that same terminal test and vanish with it.
section_start = text.find("# Update the final particle architecture assertions")
section_end = text.find("write(TESTS, t)\n", section_start)
if section_start < 0 or section_end < 0:
    raise RuntimeError("legacy particle assertion patch section missing")
section_end += len("write(TESTS, t)\n")
text = text[:section_start] + "write(TESTS, t)\n" + text[section_end:]

path.write_text(text, encoding="utf-8")
print("retirement patch fixer applied")
