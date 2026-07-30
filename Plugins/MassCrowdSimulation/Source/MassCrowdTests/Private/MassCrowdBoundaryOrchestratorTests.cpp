#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "HAL/Event.h"
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
      Record.AgentFacts.DerivedBehaviorLabel =
        static_cast<uint32>(ECrowdActiveBehavior::Idle);
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
      OutPatch.AdapterId = {100};
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

  class FOrderedCommitAdapter final : public ICrowdBoundaryCommitAdapter
  {
  public:
    FOrderedCommitAdapter(
      const uint32 InPhase,
      const uint32 InAdapterId,
      const uint64 InPatchKey,
      const bool bInValidate,
      TArray<uint32>& InApplyOrder)
      : Phase(InPhase)
      , AdapterId(InAdapterId)
      , PatchKey(InPatchKey)
      , bValidate(bInValidate)
      , ApplyOrder(InApplyOrder)
    {
    }

    virtual bool Prepare(
      const FCrowdMassBoundarySnapshot& Snapshot,
      FCrowdBoundaryPreparedPatch& OutPatch) const override
    {
      OutPatch = {};
      OutPatch.ApplyPhase = {Phase};
      OutPatch.AdapterId = {AdapterId};
      OutPatch.PatchKey = {PatchKey};
      OutPatch.FixedStepIndex = Snapshot.FixedStepIndex;
      OutPatch.PlanRevision = Snapshot.PlanRevision;
      for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
        OutPatch.EntityRefs.Add(Agent.AgentFacts.StableEntityRef);
      OutPatch.StableHash =
        (static_cast<uint64>(Phase) << 48)
        ^ (static_cast<uint64>(AdapterId) << 16)
        ^ PatchKey;
      OutPatch.Payload =
        MakeShared<FTestPreparedPayload, ESPMode::ThreadSafe>(1);
      OutPatch.bValid = OutPatch.StableHash != 0;
      return OutPatch.bValid;
    }

    virtual bool ValidatePrepared(
      const FCrowdBoundaryPreparedPatch& Patch,
      TConstArrayView<FCrowdMassCommitTarget> Targets) const override
    {
      return bValidate && Patch.bValid
        && Patch.EntityRefs.Num() == Targets.Num();
    }

    virtual void ApplyPrepared(
      const FCrowdBoundaryPreparedPatch& Patch,
      FCrowdBoundaryApplyContext& Context) const override
    {
      check(Patch.AdapterId.Value == AdapterId);
      ApplyOrder.Add(AdapterId);
    }

  private:
    uint32 Phase = 0;
    uint32 AdapterId = 0;
    uint64 PatchKey = 0;
    bool bValidate = false;
    TArray<uint32>& ApplyOrder;
  };

  TArray<FCrowdMassCommitTarget> MakeTargets(
    const FCrowdMassBoundarySnapshot& Snapshot)
  {
    TArray<FCrowdMassCommitTarget> Targets;
    for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
    {
      FCrowdMassCommitTarget& Target = Targets.AddDefaulted_GetRef();
      Target.EntityRef = Agent.AgentFacts.StableEntityRef;
      Target.AgentId = Agent.Identity.AgentId;
      Target.LifecycleSerial = Target.EntityRef.LifecycleSerial;
    }
    return Targets;
  }

  template<typename TBoundary>
  ECrowdBoundaryPollResult PollUntilTerminal(TBoundary& Boundary)
  {
    const double Deadline = FPlatformTime::Seconds() + 5.0;
    ECrowdBoundaryPollResult Result =
      ECrowdBoundaryPollResult::Pending;
    while (Result == ECrowdBoundaryPollResult::Pending
      && FPlatformTime::Seconds() < Deadline)
    {
      Result = Boundary.PollAndDrain();
      if (Result == ECrowdBoundaryPollResult::Pending)
        FPlatformProcess::SleepNoStats(0.001f);
    }
    return Result;
  }
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
    {{1}, {101}, 0}, {},
    [] { return FCrowdBoundaryTaskResult::Success(11); }));
  TestTrue(TEXT("flow task queued"), Orchestrator.AddTask(
    {{2}, {201}, 1}, {},
    [] { return FCrowdBoundaryTaskResult::Success(12); }));
  const FCrowdBoundaryTaskKey DemandPrerequisites[] = {
    {{1}, {101}, 0},
    {{2}, {201}, 1}};
  TestTrue(TEXT("dependent demand queued"), Orchestrator.AddTask(
    {{2}, {203}, 1}, DemandPrerequisites,
    [] { return FCrowdBoundaryTaskResult::Success(13); }));
  const FCrowdBoundaryTaskKey MovementPrerequisites[] = {
    {{2}, {203}, 1}};
  TestTrue(TEXT("terminal movement queued"), Orchestrator.AddTask(
    {{3}, {301}, 0}, MovementPrerequisites,
    [] { return FCrowdBoundaryTaskResult::Success(14); }));
  TestTrue(TEXT("graph dispatches once"), Orchestrator.Dispatch());
  TestEqual(TEXT("all dependency fronts drain"),
    PollUntilTerminal(Orchestrator), ECrowdBoundaryPollResult::Ready);

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
    {{1}, {101}, 0}, {},
    [] { return FCrowdBoundaryTaskResult::Failure(); }));
  const FCrowdBoundaryTaskKey Prerequisites[] = {
    {{1}, {101}, 0}};
  TestTrue(TEXT("dependent task queued"), Orchestrator.AddTask(
    {{3}, {301}, 0}, Prerequisites,
    [] { return FCrowdBoundaryTaskResult::Success(99); }));
  TestTrue(TEXT("valid graph dispatches"), Orchestrator.Dispatch());
  TestEqual(TEXT("failed prerequisite rejects terminal transaction"),
    PollUntilTerminal(Orchestrator), ECrowdBoundaryPollResult::Failed);
  TestEqual(TEXT("state is fail-closed"), Orchestrator.GetState(),
    ECrowdBoundaryTransactionState::Failed);
  TestFalse(TEXT("failed transaction cannot seal plan"),
    Orchestrator.SealMergedPlan(MakeCommitPlan(Snapshot), 0.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdBoundaryGenericDescriptorTest,
  "MassCrowd.Runtime.BoundaryOrchestrator.GenericDescriptorContracts",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdBoundaryGenericDescriptorTest::RunTest(
  const FString& Parameters)
{
  const FCrowdMassBoundarySnapshot Snapshot = MakeBoundarySnapshot();
  FCrowdBoundaryTaskDescriptor Producer;
  Producer.Key = {{41}, {9001}, 77};
  Producer.Output = {{501}, 3, 7001, 16, 0xabc, true};
  Producer.TelemetryId = 9101;

  FCrowdBoundaryTaskDescriptor Consumer;
  Consumer.Key = {{99}, {12345}, 88};
  Consumer.Prerequisites.Add(Producer.Key);
  Consumer.Inputs.Add({{501}, 3, 7001, 0xabc});
  Consumer.Output = {{777}, 9, 8002, 8, 0xdef, false};
  Consumer.TelemetryId = 9102;

  FCrowdMassBoundaryOrchestrator Valid;
  TestTrue(TEXT("generic descriptor transaction begins"),
    Valid.Begin(Snapshot, 0.0));
  TestTrue(TEXT("arbitrary producer accepted"), Valid.AddTask(
    Producer,
    [] { return FCrowdBoundaryTaskResult::Success(0xabc); }));
  TestTrue(TEXT("arbitrary consumer accepted"), Valid.AddTask(
    Consumer,
    [] { return FCrowdBoundaryTaskResult::Success(0xdef); }));
  TestTrue(TEXT("schema-compatible DAG dispatches"), Valid.Dispatch());
  TestEqual(TEXT("schema-compatible DAG drains"),
    PollUntilTerminal(Valid), ECrowdBoundaryPollResult::Ready);
  const FCrowdBoundaryOrchestratorResult Result = Valid.BuildResult();
  TestEqual(TEXT("telemetry id follows stable task descriptor"),
    Result.Tasks.Last().TelemetryId, 9102u);

  FCrowdMassBoundaryOrchestrator Mismatch;
  TestTrue(TEXT("mismatch transaction begins"),
    Mismatch.Begin(Snapshot, 0.0));
  TestTrue(TEXT("mismatch producer accepted before graph seal"),
    Mismatch.AddTask(
      Producer,
      [] { return FCrowdBoundaryTaskResult::Success(0xabc); }));
  Consumer.Inputs[0].SchemaId += 1;
  TestTrue(TEXT("mismatch consumer is structurally valid"),
    Mismatch.AddTask(
      Consumer,
      [] { return FCrowdBoundaryTaskResult::Success(0xdef); }));
  TestFalse(TEXT("schema mismatch fails graph atomically"),
    Mismatch.Dispatch());
  TestEqual(TEXT("mismatch state is failed"),
    Mismatch.GetState(), ECrowdBoundaryTransactionState::Failed);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdBoundaryPatchTransactionTest,
  "MassCrowd.Runtime.BoundaryOrchestrator.PatchTransaction",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdBoundaryPatchTransactionTest::RunTest(
  const FString& Parameters)
{
  const FCrowdMassBoundarySnapshot Snapshot = MakeBoundarySnapshot();
  const TArray<FCrowdMassCommitTarget> Targets = MakeTargets(Snapshot);
  TArray<uint32> ApplyOrder;
  FOrderedCommitAdapter Late(3, 300, 1, true, ApplyOrder);
  FOrderedCommitAdapter Early(1, 200, 1, true, ApplyOrder);
  FOrderedCommitAdapter Invalid(2, 100, 1, false, ApplyOrder);

  FCrowdBoundaryPatchTransaction Rejected;
  TestTrue(TEXT("late adapter registered"), Rejected.AddAdapter(Late));
  TestTrue(TEXT("invalid adapter registered"), Rejected.AddAdapter(Invalid));
  TestTrue(TEXT("early adapter registered"), Rejected.AddAdapter(Early));
  TestTrue(TEXT("all adapters prepare without writes"),
    Rejected.PrepareAll(Snapshot));
  TestEqual(TEXT("prepare performs zero writes"), ApplyOrder.Num(), 0);
  TestFalse(TEXT("one adapter rejection rejects complete set"),
    Rejected.ValidateAll(Targets));
  TestEqual(TEXT("failed validation performs zero writes"),
    ApplyOrder.Num(), 0);

  FCrowdBoundaryPatchTransaction Accepted;
  TestTrue(TEXT("late valid adapter registered"), Accepted.AddAdapter(Late));
  TestTrue(TEXT("early valid adapter registered"), Accepted.AddAdapter(Early));
  TestTrue(TEXT("valid patch set prepared"), Accepted.PrepareAll(Snapshot));
  TestTrue(TEXT("valid patch set validated"), Accepted.ValidateAll(Targets));
  FCrowdBoundaryApplyContext Context;
  Context.FixedStepIndex = Snapshot.FixedStepIndex;
  Context.PlanRevision = Snapshot.PlanRevision;
  Accepted.ApplyAll(Context);
  TestEqual(TEXT("both patches applied"), ApplyOrder.Num(), 2);
  TestEqual(TEXT("apply order uses phase before registration order"),
    ApplyOrder[0], 200u);
  TestEqual(TEXT("later phase applies last"), ApplyOrder[1], 300u);
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
  PatchA.AdapterId = {200};
  PatchA.StableHash = 0x1111;
  PatchB.AdapterId = {100};
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
    FCrowdBoundaryAdapterId{100});

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

  FCrowdBehaviorBoundaryMetadata BehaviorMetadata;
  BehaviorMetadata.SourceSetRevision = 3;
  BehaviorMetadata.SourceSetHash = 11;
  BehaviorMetadata.CommandBatchHash = 12;
  BehaviorMetadata.ResolvedChannelHash = 13;
  FCrowdBoundaryCommitEnvelope BehaviorEnvelope;
  TestTrue(TEXT("v3 envelope includes behavior source hashes"),
    FCrowdMassBoundaryOrchestrator::BuildCommitEnvelope(
      Snapshot, MovementPlan, Forward,
      BehaviorEnvelope, &BehaviorMetadata));
  TestEqual(TEXT("commit envelope protocol is v3"),
    BehaviorEnvelope.Version, 3u);
  TestTrue(TEXT("behavior metadata changes authoritative hash"),
    BehaviorEnvelope.StableHash != ForwardEnvelope.StableHash);

  FCrowdMassBoundaryOrchestrator Orchestrator;
  TestTrue(TEXT("orchestrator begins"), Orchestrator.Begin(Snapshot, 0.0));
  TestTrue(TEXT("terminal task queued"), Orchestrator.AddTask(
    {{3}, {301}, 0}, {},
    [] { return FCrowdBoundaryTaskResult::Success(7); }));
  TestTrue(TEXT("orchestrator dispatches"), Orchestrator.Dispatch());
  TestEqual(TEXT("orchestrator drains once"),
    PollUntilTerminal(Orchestrator), ECrowdBoundaryPollResult::Ready);
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
    {{3}, {301}, 0}, {},
    [] { return FCrowdBoundaryTaskResult::Success(17); }));
  TestTrue(TEXT("runner dispatches exactly once"), Runner.Dispatch());
  TestFalse(TEXT("second dispatch rejected"), Runner.Dispatch());
  TestEqual(TEXT("runner drains exactly once"),
    PollUntilTerminal(Runner), ECrowdBoundaryPollResult::Ready);
  TestEqual(TEXT("second drain rejected"), Runner.PollAndDrain(),
    ECrowdBoundaryPollResult::Failed);

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
    {{3}, {301}, 0}, {},
    [] { return FCrowdBoundaryTaskResult::Success(18); }));
  TestTrue(TEXT("valid runner dispatches"), ValidRunner.Dispatch());
  TestEqual(TEXT("valid runner drains"),
    PollUntilTerminal(ValidRunner), ECrowdBoundaryPollResult::Ready);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdBoundaryNonBlockingPollTest,
  "MassCrowd.Runtime.BoundaryOrchestrator.NonBlockingPollAndIdentity",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdBoundaryNonBlockingPollTest::RunTest(
  const FString& Parameters)
{
  const FCrowdMassBoundarySnapshot Snapshot = MakeBoundarySnapshot();
  FEventRef Gate(EEventMode::ManualReset);
  FCrowdMassBoundaryRunner Runner;
  const FCrowdBoundaryTransactionId Transaction =
    FCrowdBoundaryTransactionId::FromSnapshot(Snapshot, 7);
  TestTrue(TEXT("explicit transaction begins"),
    Runner.Begin(Snapshot, 0.0, Transaction));
  FEvent* const GatePtr = Gate.Get();
  TestTrue(TEXT("gated work queues"), Runner.AddTask(
    {{3}, {301}, 0}, {},
    [GatePtr]
    {
      GatePtr->Wait();
      return FCrowdBoundaryTaskResult::Success(77);
    }));
  TestTrue(TEXT("gated work dispatches"), Runner.Dispatch());
  TestEqual(TEXT("unfinished work reports pending without waiting"),
    Runner.PollAndDrain(), ECrowdBoundaryPollResult::Pending);
  Gate->Trigger();
  TestEqual(TEXT("released work becomes ready"),
    PollUntilTerminal(Runner), ECrowdBoundaryPollResult::Ready);
  TestTrue(TEXT("runner retains exact transaction identity"),
    Runner.MatchesTransaction(Transaction));
  TestTrue(TEXT("worker timing is reported"),
    Runner.BuildResult().Tasks[0].Timings.ExecutionMilliseconds >= 0.0);

  FCrowdMassBoundaryRunner Mismatch;
  FCrowdBoundaryTransactionId Wrong = Transaction;
  ++Wrong.SnapshotHash;
  TestFalse(TEXT("snapshot identity mismatch is rejected"),
    Mismatch.Begin(Snapshot, 0.0, Wrong));
  return true;
}

#endif
