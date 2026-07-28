#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "HAL/PlatformProcess.h"
#include "MassCrowdBoundaryOrchestrator.h"
#include "MassCrowdBoundaryRunner.h"
#include "Misc/AutomationTest.h"

namespace
{
  FCrowdMassBoundarySnapshot MakeBoundarySnapshot()
  {
    TArray<FCrowdMassBoundaryAgentRecord> Records;
    for (const int32 AgentId : {2, 1})
    {
      FCrowdMassBoundaryAgentRecord& Record =
        Records.AddDefaulted_GetRef();
      Record.Identity.AgentId = AgentId;
      Record.Identity.SetStableEntityRef({
        7u, static_cast<uint64>(AgentId) + 100u, 3u});
      Record.AgentFacts.StableEntityRef =
        Record.Identity.GetStableEntityRef();
      Record.AgentFacts.CapabilitySet.Add(ECrowdCapability::Move);
      Record.AgentFacts.ActiveBehavior = ECrowdActiveBehavior::Idle;
      Record.State.Position = FVector(
        static_cast<double>(AgentId) * 100.0, 0.0, 60.0);
      Record.State.bInitialized = true;
      Record.Properties.CapabilityProfileKey = 1;
    }
    FCrowdMassBoundarySnapshot Snapshot;
    FCrowdMassRuntimeBridge::BuildBoundarySnapshot(9, 4, Records, Snapshot);
    return Snapshot;
  }

  FCrowdMassCommitPlan MakeCommitPlan(
    const FCrowdMassBoundarySnapshot& Snapshot)
  {
    FCrowdMassCommitPlan Plan;
    Plan.FixedStepIndex = Snapshot.FixedStepIndex;
    Plan.PlanRevision = Snapshot.PlanRevision;
    for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
    {
      FCrowdMassCommitRecord& Record = Plan.Records.AddDefaulted_GetRef();
      Record.EntityRef = Agent.AgentFacts.StableEntityRef;
      Record.PlanRevision = Snapshot.PlanRevision;
      Record.Movement.AgentId = Agent.Identity.AgentId;
      Record.Movement.LifecycleSerial =
        Agent.AgentFacts.StableEntityRef.LifecycleSerial;
      Record.Movement.Position = Agent.State.Position + FVector(1.0, 0.0, 0.0);
      Record.Movement.bValid = true;
    }
    Plan.StableHash = 0x1020304050607080ull;
    Plan.bValid = true;
    return Plan;
  }

  class FTestPreparedPayload final
    : public ICrowdBoundaryPreparedPatchPayload
  {
  public:
    explicit FTestPreparedPayload(const int32 InValue) : Value(InValue) {}
    int32 Value = 0;
  };

  class FTestCommitAdapter final : public ICrowdBoundaryCommitAdapter
  {
  public:
    explicit FTestCommitAdapter(int32& InAppliedValue)
      : AppliedValue(InAppliedValue)
    {
    }

    virtual bool Prepare(
      const FCrowdMassBoundarySnapshot& Snapshot,
      FCrowdBoundaryPreparedPatch& OutPatch) const override
    {
      OutPatch = {};
      if (!Snapshot.bValid) return false;
      OutPatch.AdapterId = TEXT("Test");
      OutPatch.FixedStepIndex = Snapshot.FixedStepIndex;
      OutPatch.PlanRevision = Snapshot.PlanRevision;
      for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
        OutPatch.EntityRefs.Add(Agent.AgentFacts.StableEntityRef);
      OutPatch.StableHash = 0xabcdefu;
      OutPatch.Payload =
        MakeShared<FTestPreparedPayload, ESPMode::ThreadSafe>(17);
      OutPatch.bValid = true;
      return true;
    }

    virtual bool ValidatePrepared(
      const FCrowdBoundaryPreparedPatch& Patch,
      const TConstArrayView<FCrowdMassCommitTarget> Targets) const override
    {
      if (!Patch.bValid || Patch.EntityRefs.Num() != Targets.Num())
        return false;
      TArray<FCrowdStableEntityRef> TargetRefs;
      for (const FCrowdMassCommitTarget& Target : Targets)
        TargetRefs.Add(Target.EntityRef);
      TargetRefs.Sort();
      return TargetRefs == Patch.EntityRefs;
    }

    virtual void ApplyPrepared(
      const FCrowdBoundaryPreparedPatch& Patch,
      FCrowdBoundaryApplyContext& Context) const override
    {
      check(IsInGameThread());
      const TSharedPtr<const FTestPreparedPayload, ESPMode::ThreadSafe>
        Payload = StaticCastSharedPtr<const FTestPreparedPayload>(
          Patch.Payload);
      check(Payload.IsValid());
      AppliedValue += Payload->Value;
    }

  private:
    int32& AppliedValue;
  };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdBoundaryOrchestratorDependencyTest,
  "MassCrowd.Runtime.BoundaryOrchestrator.DependencyAndTransaction",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdBoundaryOrchestratorDependencyTest::RunTest(
  const FString& Parameters)
{
  const FCrowdMassBoundarySnapshot Snapshot = MakeBoundarySnapshot();
  TestTrue(TEXT("fixture snapshot valid"), Snapshot.bValid);

  FCrowdMassBoundaryOrchestrator Orchestrator;
  TestTrue(TEXT("begin accepts immutable snapshot"),
    Orchestrator.Begin(Snapshot, 0.25));
  TestTrue(TEXT("business task queued"), Orchestrator.AddTask(
    {ECrowdBoundaryTaskStage::BusinessPrepare, 0}, {},
    [] { return FCrowdBoundaryTaskResult::Success(11); }));
  TestTrue(TEXT("flow task queued"), Orchestrator.AddTask(
    {ECrowdBoundaryTaskStage::SharedFlow, 1}, {},
    [] { return FCrowdBoundaryTaskResult::Success(12); }));
  const FCrowdBoundaryTaskKey DemandPrerequisites[] = {
    {ECrowdBoundaryTaskStage::BusinessPrepare, 0},
    {ECrowdBoundaryTaskStage::SharedFlow, 1}};
  TestTrue(TEXT("dependent demand queued"), Orchestrator.AddTask(
    {ECrowdBoundaryTaskStage::TargetDemand, 1}, DemandPrerequisites,
    [] { return FCrowdBoundaryTaskResult::Success(13); }));
  const FCrowdBoundaryTaskKey MovementPrerequisites[] = {
    {ECrowdBoundaryTaskStage::TargetDemand, 1}};
  TestTrue(TEXT("terminal movement queued"), Orchestrator.AddTask(
    {ECrowdBoundaryTaskStage::Movement, 0}, MovementPrerequisites,
    [] { return FCrowdBoundaryTaskResult::Success(14); }));
  TestTrue(TEXT("graph dispatches once"), Orchestrator.Dispatch());
  FPlatformProcess::SleepNoStats(0.01f);
  TestTrue(TEXT("all dependency fronts drain"), Orchestrator.WaitAndDrain());

  const FCrowdMassCommitPlan Plan = MakeCommitPlan(Snapshot);
  TestTrue(TEXT("stable merged plan sealed"),
    Orchestrator.SealMergedPlan(Plan, 0.5));
  TestTrue(TEXT("complete set marked validated"),
    Orchestrator.MarkValidated(0.75));
  TestTrue(TEXT("unique writer marked committed"),
    Orchestrator.MarkCommitted(1.0));
  const FCrowdBoundaryOrchestratorResult Result =
    Orchestrator.BuildResult();
  TestTrue(TEXT("transaction succeeds"), Result.bSucceeded);
  TestEqual(TEXT("all tasks reported"), Result.Tasks.Num(), 4);
  TestNotEqual(TEXT("versioned commit hash reported"),
    Result.CommitPlanHash, static_cast<uint64>(0));
  for (const FCrowdBoundaryCompletedTask& Task : Result.Tasks)
    TestTrue(TEXT("WORK ran off GT"), Task.Result.bRanOffGameThread);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdBoundaryOrchestratorFailureTest,
  "MassCrowd.Runtime.BoundaryOrchestrator.FailClosed",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdBoundaryOrchestratorFailureTest::RunTest(
  const FString& Parameters)
{
  const FCrowdMassBoundarySnapshot Snapshot = MakeBoundarySnapshot();
  FCrowdMassBoundaryOrchestrator Orchestrator;
  TestTrue(TEXT("begin succeeds"), Orchestrator.Begin(Snapshot, 0.0));
  TestTrue(TEXT("failing task queued"), Orchestrator.AddTask(
    {ECrowdBoundaryTaskStage::BusinessPrepare, 0}, {},
    [] { return FCrowdBoundaryTaskResult::Failure(); }));
  const FCrowdBoundaryTaskKey Prerequisites[] = {
    {ECrowdBoundaryTaskStage::BusinessPrepare, 0}};
  TestTrue(TEXT("dependent task queued"), Orchestrator.AddTask(
    {ECrowdBoundaryTaskStage::Movement, 0}, Prerequisites,
    [] { return FCrowdBoundaryTaskResult::Success(99); }));
  TestTrue(TEXT("valid graph dispatches"), Orchestrator.Dispatch());
  TestFalse(TEXT("failed prerequisite rejects terminal transaction"),
    Orchestrator.WaitAndDrain());
  TestEqual(TEXT("state is fail-closed"), Orchestrator.GetState(),
    ECrowdBoundaryTransactionState::Failed);
  TestFalse(TEXT("failed transaction cannot seal plan"),
    Orchestrator.SealMergedPlan(MakeCommitPlan(Snapshot), 0.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdBoundaryPreparedPatchTest,
  "MassCrowd.Runtime.BoundaryOrchestrator.PreparedPatch",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdBoundaryPreparedPatchTest::RunTest(
  const FString& Parameters)
{
  const FCrowdMassBoundarySnapshot Snapshot = MakeBoundarySnapshot();
  int32 AppliedValue = 0;
  FTestCommitAdapter Adapter(AppliedValue);
  FCrowdBoundaryPreparedPatch Patch;
  TestTrue(TEXT("prepare builds immutable patch"),
    Adapter.Prepare(Snapshot, Patch));
  TestEqual(TEXT("prepare performs no external write"), AppliedValue, 0);

  TArray<FCrowdMassCommitTarget> Targets;
  for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
  {
    FCrowdMassCommitTarget& Target = Targets.AddDefaulted_GetRef();
    Target.EntityRef = Agent.AgentFacts.StableEntityRef;
    Target.AgentId = Agent.Identity.AgentId;
    Target.LifecycleSerial = Target.EntityRef.LifecycleSerial;
  }
  Algo::Reverse(Targets);
  TestTrue(TEXT("complete reversed target set validates"),
    Adapter.ValidatePrepared(Patch, Targets));
  Targets[0].EntityRef.LifecycleSerial += 1;
  TestFalse(TEXT("stale lifecycle rejects prepared patch"),
    Adapter.ValidatePrepared(Patch, Targets));
  TestEqual(TEXT("validation failure performs no write"), AppliedValue, 0);

  FCrowdBoundaryApplyContext Context;
  Adapter.ApplyPrepared(Patch, Context);
  TestEqual(TEXT("apply executes prepared payload exactly once"),
    AppliedValue, 17);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdBoundaryCommitEnvelopeTest,
  "MassCrowd.Runtime.BoundaryOrchestrator.CommitEnvelope",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdBoundaryCommitEnvelopeTest::RunTest(
  const FString& Parameters)
{
  const FCrowdMassBoundarySnapshot Snapshot = MakeBoundarySnapshot();
  const FCrowdMassCommitPlan MovementPlan = MakeCommitPlan(Snapshot);

  int32 AppliedValue = 0;
  FTestCommitAdapter Adapter(AppliedValue);
  FCrowdBoundaryPreparedPatch PatchA;
  FCrowdBoundaryPreparedPatch PatchB;
  TestTrue(TEXT("first patch prepared"), Adapter.Prepare(Snapshot, PatchA));
  TestTrue(TEXT("second patch prepared"), Adapter.Prepare(Snapshot, PatchB));
  PatchA.AdapterId = TEXT("Visual");
  PatchA.StableHash = 0x1111;
  PatchB.AdapterId = TEXT("Combat");
  PatchB.StableHash = 0x2222;

  const FCrowdBoundaryPreparedPatch Forward[] = {PatchA, PatchB};
  const FCrowdBoundaryPreparedPatch Reverse[] = {PatchB, PatchA};
  FCrowdBoundaryCommitEnvelope ForwardEnvelope;
  FCrowdBoundaryCommitEnvelope ReverseEnvelope;
  TestTrue(TEXT("forward envelope builds"),
    FCrowdMassBoundaryOrchestrator::BuildCommitEnvelope(
      Snapshot, MovementPlan, Forward, ForwardEnvelope));
  TestTrue(TEXT("reverse envelope builds"),
    FCrowdMassBoundaryOrchestrator::BuildCommitEnvelope(
      Snapshot, MovementPlan, Reverse, ReverseEnvelope));
  TestEqual(TEXT("patch registration order does not change plan hash"),
    ForwardEnvelope.StableHash, ReverseEnvelope.StableHash);
  TestEqual(TEXT("descriptors are sorted"), ForwardEnvelope.Patches[0].AdapterId,
    FName(TEXT("Combat")));

  PatchB.StableHash += 1;
  const FCrowdBoundaryPreparedPatch Changed[] = {PatchA, PatchB};
  FCrowdBoundaryCommitEnvelope ChangedEnvelope;
  TestTrue(TEXT("changed envelope builds"),
    FCrowdMassBoundaryOrchestrator::BuildCommitEnvelope(
      Snapshot, MovementPlan, Changed, ChangedEnvelope));
  TestNotEqual(TEXT("any patch change changes authoritative hash"),
    ForwardEnvelope.StableHash, ChangedEnvelope.StableHash);

  FCrowdBoundaryPreparedPatch Duplicate = PatchA;
  Duplicate.StableHash += 10;
  const FCrowdBoundaryPreparedPatch Duplicates[] = {PatchA, Duplicate};
  FCrowdBoundaryCommitEnvelope RejectedEnvelope;
  TestFalse(TEXT("duplicate adapter id fails closed"),
    FCrowdMassBoundaryOrchestrator::BuildCommitEnvelope(
      Snapshot, MovementPlan, Duplicates, RejectedEnvelope));
  TestFalse(TEXT("rejected envelope remains invalid"),
    RejectedEnvelope.bValid);

  FCrowdMassBoundaryOrchestrator Orchestrator;
  TestTrue(TEXT("orchestrator begins"), Orchestrator.Begin(Snapshot, 0.0));
  TestTrue(TEXT("terminal task queued"), Orchestrator.AddTask(
    {ECrowdBoundaryTaskStage::Movement, 0}, {},
    [] { return FCrowdBoundaryTaskResult::Success(7); }));
  TestTrue(TEXT("orchestrator dispatches"), Orchestrator.Dispatch());
  FPlatformProcess::SleepNoStats(0.01f);
  TestTrue(TEXT("orchestrator drains once"), Orchestrator.WaitAndDrain());
  TestTrue(TEXT("full envelope seals"),
    Orchestrator.SealMergedEnvelope(ForwardEnvelope, 0.0));
  TestEqual(TEXT("reported hash covers patches"),
    Orchestrator.BuildResult().CommitPlanHash, ForwardEnvelope.StableHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdBoundaryRunnerAtomicValidationTest,
  "MassCrowd.Runtime.BoundaryRunner.AtomicValidation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdBoundaryRunnerAtomicValidationTest::RunTest(
  const FString& Parameters)
{
  const FCrowdMassBoundarySnapshot Snapshot = MakeBoundarySnapshot();
  const FCrowdMassCommitPlan MovementPlan = MakeCommitPlan(Snapshot);
  int32 AppliedValue = 0;
  FTestCommitAdapter Adapter(AppliedValue);
  FCrowdBoundaryPreparedPatch Patch;
  TestTrue(TEXT("prepared patch created without side effects"),
    Adapter.Prepare(Snapshot, Patch));

  TArray<FCrowdMassCommitTarget> Targets;
  for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
  {
    FCrowdMassCommitTarget& Target = Targets.AddDefaulted_GetRef();
    Target.EntityRef = Agent.AgentFacts.StableEntityRef;
    Target.AgentId = Agent.Identity.AgentId;
    Target.LifecycleSerial = Target.EntityRef.LifecycleSerial;
  }

  FCrowdMassBoundaryRunner Runner;
  TestTrue(TEXT("runner begins"), Runner.Begin(Snapshot, 0.0));
  TestTrue(TEXT("runner queues a worker"), Runner.AddTask(
    {ECrowdBoundaryTaskStage::Movement, 0}, {},
    [] { return FCrowdBoundaryTaskResult::Success(17); }));
  TestTrue(TEXT("runner dispatches exactly once"), Runner.Dispatch());
  TestFalse(TEXT("second dispatch rejected"), Runner.Dispatch());
  TestTrue(TEXT("runner waits exactly once"), Runner.WaitAndDrain());
  TestFalse(TEXT("second wait rejected"), Runner.WaitAndDrain());

  TArray<FCrowdMassCommitTarget> StaleTargets = Targets;
  StaleTargets.Last().EntityRef.LifecycleSerial += 1;
  StaleTargets.Last().LifecycleSerial += 1;
  TestFalse(TEXT("last stale entity rejects complete transaction"),
    Runner.BuildAndSealCommit(
      MovementPlan, MakeArrayView(&Patch, 1), StaleTargets, 0.0));
  TestEqual(TEXT("failed validation performs zero writes"),
    AppliedValue, 0);

  FCrowdMassBoundaryRunner ValidRunner;
  TestTrue(TEXT("valid runner begins"), ValidRunner.Begin(Snapshot, 0.0));
  TestTrue(TEXT("valid runner queues worker"), ValidRunner.AddTask(
    {ECrowdBoundaryTaskStage::Movement, 0}, {},
    [] { return FCrowdBoundaryTaskResult::Success(18); }));
  TestTrue(TEXT("valid runner dispatches"), ValidRunner.Dispatch());
  TestTrue(TEXT("valid runner waits"), ValidRunner.WaitAndDrain());
  TestTrue(TEXT("complete target set seals"),
    ValidRunner.BuildAndSealCommit(
      MovementPlan, MakeArrayView(&Patch, 1), Targets, 0.0));
  TestTrue(TEXT("complete plan validates"),
    ValidRunner.MarkValidated(0.0));
  TestTrue(TEXT("complete plan commits"),
    ValidRunner.MarkCommitted(0.0));
  TestEqual(TEXT("runner itself never applies host patches"),
    AppliedValue, 0);
  return true;
}

#endif
