#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include "Mass/CrowdDemoBidirectionalSwapKernel.h"
#include "Mass/CrowdDemoMassCrowdRuntimeAdapter.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
#include "CrowdSharedFlowFieldKernel.h"
#include "MassCrowdRuntimeBridge.h"
#include "MassCrowdWorkerFlowBinding.h"
#include "MassCrowdWorkerFlowResource.h"
#include "MassCrowdWorkerMovementAuthority.h"
#include "MassCrowdWorkerMovementControlResource.h"
#include "MassCrowdWorkerMovementDomain.h"
#include "MassCrowdWorkerNavigationObjective.h"
#include "MassCrowdWorkerShadowSync.h"

namespace
{
  TArray<FCrowdDemoBidirectionalSwapLayoutInput> MakeInputs()
  {
    TArray<FCrowdDemoBidirectionalSwapLayoutInput> Inputs;
    for (int32 Index = 0; Index < FCrowdDemoBidirectionalSwapKernel::AgentCount; ++Index)
    {
      auto& Input = Inputs.AddDefaulted_GetRef();
      Input.AgentId = 100 + Index;
      Input.FormationIndex = Index;
    }
    return Inputs;
  }

  FCrowdWorkerPayload MakeWorkerState(
    const FCrowdDemoBidirectionalSwapLayoutAgent& Agent,
    const FCrowdStableEntityRef& EntityRef,
    const FVector& Position)
  {
    FCrowdMassBoundaryAgentRecord Record;
    Record.Identity.AgentId = Agent.AgentId;
    Record.Identity.SetStableEntityRef(EntityRef);
    Record.AgentFacts.StableEntityRef = EntityRef;
    Record.AgentFacts.CapabilitySet.Bits = 1;
    Record.State.Position = Position;
    Record.State.PlanRevision = 1;
    Record.State.bInitialized = true;
    Record.Properties.PhysicalRadiusCm = 42.0f;
    Record.Properties.HardSafetyGapCm = 10.0f;
    Record.Properties.MaximumSpeedCmps = 800.0f;
    FCrowdWorkerPayload Payload;
    FCrowdWorkerBoundaryStateCodec::EncodeState(Record, Payload);
    return Payload;
  }

  struct FT3GenericWorkerFixture
  {
    static constexpr uint64 Generation = 51;
    static constexpr uint64 InputSequence = 7;
    static constexpr uint64 Epoch = 11;
    FCrowdWorkerEntityStateStore States;
    FCrowdWorkerResourceStore Resources;
    FCrowdWorkerDomainContext Context;
    FCrowdStableEntityRef NorthEntity;
    FCrowdStableEntityRef SouthEntity;

    bool Initialize(const FCrowdDemoBidirectionalSwapLayout& Layout)
    {
      if (!Layout.bValid || !States.Reset(16, 1024 * 1024)
        || !Resources.Reset(32 * 1024 * 1024))
        return false;
      const FCrowdDemoBidirectionalSwapLayoutAgent* North =
        Layout.Agents.FindByPredicate([](const auto& Agent)
        {
          return Agent.CohortKey
            == FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey;
        });
      const FCrowdDemoBidirectionalSwapLayoutAgent* South =
        Layout.Agents.FindByPredicate([](const auto& Agent)
        {
          return Agent.CohortKey
            == FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey;
        });
      if (!North || !South) return false;
      NorthEntity = {1, static_cast<uint64>(North->AgentId) + 1, 1};
      SouthEntity = {1, static_cast<uint64>(South->AgentId) + 1, 1};
      if (States.Spawn(NorthEntity, Generation, InputSequence,
            MakeWorkerState(*North, NorthEntity, North->SpawnLocation))
          != ECrowdWorkerQueueResult::Added
        || States.Spawn(SouthEntity, Generation, InputSequence,
            MakeWorkerState(*South, SouthEntity, South->SpawnLocation))
          != ECrowdWorkerQueueResult::Added)
        return false;

      FCrowdWorkerMovementControlResource Control;
      Control.Revision = 1;
      Control.FixedStepIndex = 1;
      Control.PlanRevision = 1;
      for (int32 Index = 0; Index < 2; ++Index)
      {
        FCrowdWorkerMovementControlEntry& Entry =
          Control.Entries.AddDefaulted_GetRef();
        Entry.EntityRef = Index == 0 ? NorthEntity : SouthEntity;
        Entry.AgentId = Index == 0 ? North->AgentId : South->AgentId;
        Entry.MaximumSpeedCmps = 800.0f;
        Entry.AutonomousPreferredVelocity = FVector::ZeroVector;
        Entry.bUseAuthoritativePreferredVelocity = false;
      }
      FCrowdWorkerPayload ControlPayload;
      if (!FCrowdWorkerMovementControlResourceCodec::Encode(
          Control, ControlPayload)
        || Resources.StageBuilding({
            CrowdWorkerResourceIds::MovementControl,
            Control.Revision, MoveTemp(ControlPayload)})
          != ECrowdWorkerQueueResult::Added)
        return false;

      const uint32 CohortKeys[] = {
        FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey,
        FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey};
      for (const uint32 CohortKey : CohortKeys)
      {
        const FCrowdDemoSharedFlowFieldConfig DemoConfig =
          FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(CohortKey);
        FCrowdSharedFlowField Flow;
        if (!FCrowdSharedFlowFieldKernel::Build(
            FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(
              DemoConfig), Flow))
          return false;
        FCrowdWorkerPayload FlowPayload;
        if (!FCrowdWorkerFlowFieldResourceCodec::Encode(
            Flow, FlowPayload)
          || Resources.StageBuilding({
              FCrowdDemoBidirectionalSwapKernel::FlowResourceForCohort(
                CohortKey),
              static_cast<uint64>(Flow.Config.Revision),
              MoveTemp(FlowPayload)})
            != ECrowdWorkerQueueResult::Added)
          return false;
        FCrowdWorkerNavigationObjectiveResource Objective;
        Objective.GoalLocation = FVector(DemoConfig.GoalLocation);
        FCrowdWorkerPayload ObjectivePayload;
        if (!FCrowdWorkerNavigationObjectiveResourceCodec::Encode(
            Objective, ObjectivePayload)
          || Resources.StageBuilding({
              FCrowdDemoBidirectionalSwapKernel::ObjectiveForCohort(
                CohortKey).ResolveResourceId(),
              static_cast<uint64>(DemoConfig.Revision),
              MoveTemp(ObjectivePayload)})
            != ECrowdWorkerQueueResult::Added)
          return false;
      }
      TArray<FCrowdWorkerResourceRevisionEvent> Events;
      if (!Resources.CommitBuildingAtEpoch(Epoch, Events)) return false;

      const FCrowdDemoBidirectionalSwapLayoutAgent* Representatives[] = {
        North, South};
      const FCrowdStableEntityRef EntityRefs[] = {NorthEntity, SouthEntity};
      for (int32 Index = 0; Index < 2; ++Index)
      {
        FCrowdWorkerFlowBinding Binding;
        Binding.EntityRef = EntityRefs[Index];
        Binding.ObjectiveRef = Representatives[Index]->ObjectiveRef;
        Binding.CohortKey = Representatives[Index]->CohortKey;
        Binding.FlowResourceId = Representatives[Index]->FlowResourceId;
        FCrowdWorkerDirtyStateRecord Dirty;
        Dirty.EntityRef = EntityRefs[Index];
        Dirty.Field = ECrowdWorkerField::FlowBinding;
        Dirty.Generation = Generation;
        Dirty.WorkerEpoch = Epoch;
        Dirty.StateRevision = InputSequence;
        Dirty.SourceInputSequence = InputSequence;
        if (!FCrowdWorkerFlowBindingCodec::Encode(
            Binding, Dirty.Payload)
          || States.ApplyDirty(Dirty)
            != ECrowdWorkerQueueResult::Replaced)
          return false;
      }
      Context.Generation = Generation;
      Context.WorkerEpoch = Epoch;
      Context.AbsoluteSimulationTick = Epoch;
      Context.LastAppliedInputSequence = InputSequence;
      Context.FixedDeltaSeconds = 1.0 / 30.0;
      Context.SimulationTimeSeconds = 1.0 / 30.0;
      Context.RuntimeMode = ECrowdWorkerRuntimeV2Mode::Production;
      Context.EntityStates = &States;
      Context.Resources = &Resources;
      return true;
    }

    bool Plan(
      const FCrowdWorkerWorkItem& Work,
      TMap<FCrowdStableEntityRef, FVector>& OutVelocities,
      FCrowdWorkerMovementPlanningDomainExecutor::FExecutionStats* Stats = nullptr)
    {
      FCrowdWorkerMovementPlanningDomainExecutor Planning(Stats);
      FCrowdWorkerDomainOutput PlanningOutput;
      if (!Planning.Execute(Context, MakeArrayView(&Work, 1), PlanningOutput))
        return false;
      for (const FCrowdWorkerDirtyStateRecord& Dirty :
        PlanningOutput.DirtyStates)
        if (States.ApplyDirty(Dirty) != ECrowdWorkerQueueResult::Replaced)
          return false;
      FCrowdWorkerMovementDomainExecutor Movement;
      FCrowdWorkerDomainOutput MovementOutput;
      if (!Movement.Execute(
          Context, PlanningOutput.NextWork, MovementOutput))
        return false;
      OutVelocities.Reset();
      for (const FCrowdWorkerDirtyStateRecord& Dirty :
        MovementOutput.DirtyStates)
      {
        FCrowdWorkerMovementState State;
        if (!FCrowdWorkerMovementStateCodec::Decode(
            Dirty.Payload, State))
          return false;
        OutVelocities.Add(Dirty.EntityRef, State.Velocity);
      }
      return true;
    }

    bool SetNorthWorkerPosition(const FVector& Position)
    {
      ++Context.WorkerEpoch;
      ++Context.AbsoluteSimulationTick;
      ++Context.LastAppliedInputSequence;
      FCrowdWorkerMovementState State;
      State.StartPosition = Position;
      State.Position = Position;
      State.SimulationTimeSeconds = Context.SimulationTimeSeconds;
      FCrowdWorkerDirtyStateRecord Dirty;
      Dirty.EntityRef = NorthEntity;
      Dirty.Field = ECrowdWorkerField::Movement;
      Dirty.Generation = Generation;
      Dirty.WorkerEpoch = Context.WorkerEpoch;
      Dirty.StateRevision = Context.WorkerEpoch;
      Dirty.SourceInputSequence = Context.LastAppliedInputSequence;
      return FCrowdWorkerMovementStateCodec::Encode(State, Dirty.Payload)
        && States.ApplyDirty(Dirty) == ECrowdWorkerQueueResult::Replaced;
    }
  };

  FCrowdDemoBidirectionalSwapProgress RunProductionRollout(
    const FCrowdDemoBidirectionalSwapLayout& Layout,
    TArray<FCrowdDemoBidirectionalSwapStepAgent>& OutFinalAgents,
    const float GoalLateralOffsetCm = -1.0f)
  {
    FCrowdDemoSharedFlowField Fields[2];
    const uint32 CohortKeys[] = {
      FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey,
      FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey};
    for (int32 CohortIndex = 0; CohortIndex < 2; ++CohortIndex)
    {
      const uint32 CohortKey = CohortKeys[CohortIndex];
      FCrowdDemoSharedFlowFieldConfig Config =
        FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(CohortKey);
      if (GoalLateralOffsetCm >= 0.0f)
        Config.GoalLocation.X = CohortIndex == 0
          ? GoalLateralOffsetCm : -GoalLateralOffsetCm;
      FCrowdDemoSharedFlowFieldKernel::Build(Config, Fields[CohortIndex]);
    }
    TArray<FCrowdDemoBidirectionalSwapStepAgent> States;
    for (const auto& LayoutAgent : Layout.Agents)
    {
      auto& State = States.AddDefaulted_GetRef();
      State.AgentId = LayoutAgent.AgentId;
      State.FormationIndex = LayoutAgent.FormationIndex;
      State.CohortKey = LayoutAgent.CohortKey;
      State.Location = LayoutAgent.SpawnLocation;
      State.FlowStatus = ECrowdDemoFlowLocationStatus::Reachable;
    }
    FCrowdDemoParticleConstraintEnvironment Environment;
    Environment.FlowConfig = FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(
      FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey);
    Environment.FlowConfig.AgentInflateCm = 52.0f;
    FCrowdDemoParticleConstraintSettings Settings;
    FCrowdDemoBidirectionalSwapProgress Progress;
    for (int32 Step = 0; Step < 900; ++Step)
    {
      TArray<FCrowdDemoParticleConstraintAgent> ParticleAgents;
      for (auto& State : States)
      {
        const int32 CohortIndex = State.CohortKey
          == FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey ? 0 : 1;
        const FCrowdDemoSharedFlowSample Sample =
          FCrowdDemoSharedFlowFieldKernel::Sample(
            Fields[CohortIndex], State.Location);
        State.FlowStatus = Sample.Status;
        const FVector Goal = FVector(Fields[CohortIndex].Config.GoalLocation);
        const bool bReached = FVector::DistSquared2D(State.Location, Goal)
          <= FMath::Square(140.0f);
        float Speed = 800.0f;
        if (Sample.GuidanceDistanceCm > 0.0f)
          Speed = FMath::Min(Speed, Sample.GuidanceDistanceCm / Settings.FixedStepSeconds);
        const FVector Desired = bReached || Sample.bUnreachable
          ? FVector::ZeroVector : Sample.FlowDirection * Speed;
        auto& Particle = ParticleAgents.AddDefaulted_GetRef();
        Particle.AgentId = State.AgentId;
        Particle.StartPosition = State.Location;
        Particle.PredictedPosition = State.Location + Desired * Settings.FixedStepSeconds;
      }
      TArray<FCrowdDemoParticleConstraintPair> Pairs;
      TArray<FCrowdDemoParticleConstraintResult> Results;
      FCrowdDemoParticleConstraintSummary Summary;
      FCrowdDemoParticleConstraintKernel::Solve(
        ParticleAgents, Environment, Settings, Pairs, Results, Summary);
      if (!Summary.bValid) break;
      TMap<int32, const FCrowdDemoParticleConstraintResult*> ById;
      for (const auto& Result : Results) ById.Add(Result.AgentId, &Result);
      for (auto& State : States)
        if (const auto* const* Result = ById.Find(State.AgentId))
        {
          State.Location = (*Result)->CorrectedPosition;
          State.Velocity = (*Result)->CorrectedVelocity;
        }
      FCrowdDemoBidirectionalSwapKernel::UpdateProgress(States, Step, Progress);
    }
    OutFinalAgents = MoveTemp(States);
    return Progress;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBidirectionalSwapLayoutTest,
  "CrowdDemo.SF.T3.BidirectionalSwap.LayoutAndOpposingFlows",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBidirectionalSwapLayoutTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoBidirectionalSwapLayoutInput> Inputs = MakeInputs();
  const auto Layout = FCrowdDemoBidirectionalSwapKernel::BuildLayout(Inputs);
  Algo::Reverse(Inputs);
  const auto Reversed = FCrowdDemoBidirectionalSwapKernel::BuildLayout(Inputs);
  TestTrue(TEXT("T3 layout valid"), Layout.bValid);
  TestEqual(TEXT("T3 layout agent count"), Layout.Agents.Num(), 20);
  TestEqual(TEXT("T3 reversed layout hash"), Reversed.LayoutHash, Layout.LayoutHash);
  int32 CohortCounts[2] = { 0, 0 };
  for (const auto& Agent : Layout.Agents)
  {
    const int32 CohortIndex = Agent.CohortKey
      == FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey ? 0
      : Agent.CohortKey
          == FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey ? 1
      : INDEX_NONE;
    if (CohortIndex != INDEX_NONE) ++CohortCounts[CohortIndex];
  }
  TestEqual(TEXT("south cohort count"), CohortCounts[0], 10);
  TestEqual(TEXT("north cohort count"), CohortCounts[1], 10);
  for (int32 A = 0; A < Layout.Agents.Num(); ++A)
    for (int32 B = A + 1; B < Layout.Agents.Num(); ++B)
      TestTrue(TEXT("initial hard separation"),
        FVector::Dist2D(Layout.Agents[A].SpawnLocation, Layout.Agents[B].SpawnLocation)
          + KINDA_SMALL_NUMBER >= 94.0f);

  FCrowdDemoSharedFlowField Fields[2];
  const uint32 CohortKeys[] = {
    FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey,
    FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey};
  for (int32 CohortIndex = 0; CohortIndex < 2; ++CohortIndex)
  {
    const auto Config = FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(
      CohortKeys[CohortIndex]);
    TestTrue(TEXT("opposing flow builds"),
      FCrowdDemoSharedFlowFieldKernel::Build(Config, Fields[CohortIndex]));
    TestEqual(TEXT("open flow has no obstacles"), Config.ObstacleSpecs.Num(), 0);
  }
  TestTrue(TEXT("opposing goals"),
    FVector(Fields[0].Config.GoalLocation).Y > 0.0f
      && FVector(Fields[1].Config.GoalLocation).Y < 0.0f
      && FVector(Fields[0].Config.GoalLocation).X > 0.0f
      && FVector(Fields[1].Config.GoalLocation).X < 0.0f);
  for (const auto& Agent : Layout.Agents)
  {
    const int32 CohortIndex = Agent.CohortKey
      == FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey ? 0 : 1;
    const auto Sample = FCrowdDemoSharedFlowFieldKernel::Sample(
      Fields[CohortIndex], Agent.SpawnLocation);
    TestEqual(TEXT("spawn flow reachable"), Sample.Status,
      ECrowdDemoFlowLocationStatus::Reachable);
    TestTrue(FString::Printf(
      TEXT("flow points toward opposing side agent=%d cohort=%d direction=(%.3f,%.3f) status=%d"),
      Agent.AgentId, CohortIndex, Sample.FlowDirection.X,
      Sample.FlowDirection.Y, static_cast<int32>(Sample.Status)),
      Sample.FlowDirection.Y * (CohortIndex == 0 ? 1.0f : -1.0f) > 0.5f);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBidirectionalSwapProgressTest,
  "CrowdDemo.SF.T3.BidirectionalSwap.CompletionPlanesAndDeterminism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBidirectionalSwapProgressTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoBidirectionalSwapStepAgent> Samples;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    auto& Agent = Samples.AddDefaulted_GetRef();
    Agent.AgentId = 100 + Index;
    Agent.FormationIndex = Index;
    Agent.CohortKey = Index < 10
      ? FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey
      : FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey;
    const float Sign = Agent.CohortKey
      == FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey ? 1.0f : -1.0f;
    Agent.Location = FVector(0.0f, Sign * 2250.0f, 60.0f);
    Agent.Velocity = FVector(0.0f, Sign * 100.0f, 0.0f);
    Agent.FlowStatus = ECrowdDemoFlowLocationStatus::Reachable;
  }
  FCrowdDemoBidirectionalSwapProgress Progress;
  FCrowdDemoBidirectionalSwapKernel::UpdateProgress(Samples, 250, Progress);
  Algo::Reverse(Samples);
  FCrowdDemoBidirectionalSwapProgress Reversed;
  FCrowdDemoBidirectionalSwapKernel::UpdateProgress(Samples, 250, Reversed);
  TestTrue(TEXT("progress valid"), Progress.bValid);
  TestEqual(TEXT("all center crossed"), Progress.CenterCrossedAgentIds.Num(), 20);
  TestEqual(TEXT("all completion planes crossed"), Progress.CompletedAgentIds.Num(), 20);
  TestEqual(TEXT("no final deadlock"), Progress.FinalDeadlockAgentIds.Num(), 0);
  TestEqual(TEXT("input order invariant progress hash"),
    Reversed.ProgressHash, Progress.ProgressHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBidirectionalSwapDeadlockTest,
  "CrowdDemo.SF.T3.BidirectionalSwap.FinalDeadlockSemantics",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBidirectionalSwapDeadlockTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoBidirectionalSwapStepAgent> Samples;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    auto& Agent = Samples.AddDefaulted_GetRef();
    Agent.AgentId = 100 + Index;
    Agent.FormationIndex = Index;
    Agent.CohortKey = Index < 10
      ? FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey
      : FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey;
    Agent.Location = FVector(0.0f, Index < 10 ? -1000.0f : 1000.0f, 60.0f);
    Agent.FlowStatus = ECrowdDemoFlowLocationStatus::Reachable;
  }
  FCrowdDemoBidirectionalSwapProgress Progress;
  for (int32 Step = 0; Step < 90; ++Step)
    FCrowdDemoBidirectionalSwapKernel::UpdateProgress(Samples, Step, Progress);
  TestEqual(TEXT("90 low-forward steps are current deadlock"),
    Progress.FinalDeadlockAgentIds.Num(), 20);
  for (auto& Agent : Samples)
    Agent.Velocity.Y = Agent.CohortKey
      == FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey
      ? 100.0f : -100.0f;
  FCrowdDemoBidirectionalSwapKernel::UpdateProgress(Samples, 90, Progress);
  TestEqual(TEXT("recovered agents are not final deadlock"),
    Progress.FinalDeadlockAgentIds.Num(), 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBidirectionalSwapProductionRolloutTest,
  "CrowdDemo.SF.T3.BidirectionalSwap.ProductionRollout",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBidirectionalSwapProductionRolloutTest::RunTest(
  const FString& Parameters)
{
  const auto Layout = FCrowdDemoBidirectionalSwapKernel::BuildLayout(MakeInputs());
  TArray<FCrowdDemoBidirectionalSwapStepAgent> FinalAgents;
  const FCrowdDemoBidirectionalSwapProgress Progress =
    RunProductionRollout(Layout, FinalAgents);
  for (const auto& Agent : FinalAgents)
    if (!Progress.CompletedAgentIds.Contains(Agent.AgentId))
      AddInfo(FString::Printf(TEXT("unfinished agent=%d formation=%d location=(%.1f,%.1f) velocity=(%.1f,%.1f) low_steps=%d"),
        Agent.AgentId, Agent.FormationIndex, Agent.Location.X, Agent.Location.Y,
        Agent.Velocity.X, Agent.Velocity.Y,
        Progress.ConsecutiveLowForwardStepsByAgentId.FindRef(Agent.AgentId)));
  TestTrue(TEXT("production rollout progress valid"), Progress.bValid);
  TestEqual(TEXT("production rollout center crossed"),
    Progress.CenterCrossedAgentIds.Num(), 20);
  TestEqual(TEXT("production rollout completion planes"),
    Progress.CompletedAgentIds.Num(), 20);
  TestEqual(TEXT("production rollout final deadlock"),
    Progress.FinalDeadlockAgentIds.Num(), 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBidirectionalSwapGenericFlowBindingTest,
  "CrowdDemo.SF.T3.BidirectionalSwap.GenericFlowBindingContract",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBidirectionalSwapGenericFlowBindingTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  const FCrowdDemoBidirectionalSwapLayout Layout =
    FCrowdDemoBidirectionalSwapKernel::BuildLayout(MakeInputs());
  TestTrue(TEXT("generic binding layout valid"), Layout.bValid);
  int32 NorthCount = 0;
  int32 SouthCount = 0;
  TSet<uint32> CohortKeys;
  TSet<uint64> ObjectiveIds;
  TSet<uint64> FlowResourceIds;
  for (const FCrowdDemoBidirectionalSwapLayoutAgent& Agent : Layout.Agents)
  {
    const FCrowdStableEntityRef EntityRef{
      1, static_cast<uint64>(Agent.AgentId) + 1, 1};
    FCrowdWorkerFlowBinding Binding{
      EntityRef, Agent.ObjectiveRef,
      Agent.CohortKey, Agent.FlowResourceId};
    FCrowdWorkerPayload Payload;
    FCrowdWorkerFlowBinding Decoded;
    TestTrue(TEXT("fixture binding encodes"),
      FCrowdWorkerFlowBindingCodec::Encode(Binding, Payload));
    TestTrue(TEXT("fixture binding decodes"),
      FCrowdWorkerFlowBindingCodec::Decode(Payload, Decoded));
    TestTrue(TEXT("fixture binding round trips"), Decoded == Binding);
    CohortKeys.Add(Binding.CohortKey);
    ObjectiveIds.Add(Binding.ObjectiveRef.ObjectiveId);
    FlowResourceIds.Add(Binding.FlowResourceId);
    if (Binding.CohortKey
      == FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey)
      ++NorthCount;
    else if (Binding.CohortKey
      == FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey)
      ++SouthCount;
  }
  TestEqual(TEXT("twenty explicit bindings"), Layout.Agents.Num(), 20);
  TestEqual(TEXT("north explicit cohort"), NorthCount, 10);
  TestEqual(TEXT("south explicit cohort"), SouthCount, 10);
  TestEqual(TEXT("two explicit cohort keys"), CohortKeys.Num(), 2);
  TestEqual(TEXT("two explicit objective refs"), ObjectiveIds.Num(), 2);
  TestEqual(TEXT("two explicit flow resource ids"), FlowResourceIds.Num(), 2);

  const FCrowdDemoSharedFlowFieldConfig NorthConfig =
    FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(
      FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey);
  FCrowdWorkerNavigationObjectiveResource Objective;
  Objective.GoalLocation = FVector(NorthConfig.GoalLocation);
  FCrowdWorkerPayload ObjectivePayload;
  FCrowdWorkerNavigationObjectiveResource DecodedObjective;
  TestTrue(TEXT("generic objective encodes"),
    FCrowdWorkerNavigationObjectiveResourceCodec::Encode(
      Objective, ObjectivePayload));
  TestTrue(TEXT("generic objective decodes"),
    FCrowdWorkerNavigationObjectiveResourceCodec::Decode(
      ObjectivePayload, DecodedObjective));
  TestTrue(TEXT("generic objective round trips"),
    DecodedObjective == Objective);

  FT3GenericWorkerFixture Fixture;
  if (!TestTrue(TEXT("T3 generic Worker fixture initializes"),
      Fixture.Initialize(Layout)))
    return false;
  FCrowdWorkerWorkItem FullWork;
  FullWork.Key.Domain = ECrowdWorkerDomainId::MovementPlanning;
  FullWork.Key.Kind = ECrowdWorkerWorkKind::Resource;
  FullWork.Key.ScopeKey = CrowdWorkerResourceIds::MovementControl;
  FullWork.Priority = ECrowdWorkerWorkPriority::High;
  FullWork.ReasonMask = 1;
  TMap<FCrowdStableEntityRef, FVector> FirstVelocities;
  FCrowdWorkerMovementPlanningDomainExecutor::FExecutionStats Stats;
  if (!TestTrue(TEXT("common Worker planning resolves T3 bindings"),
      Fixture.Plan(FullWork, FirstVelocities, &Stats)))
    return false;
  TestEqual(TEXT("same executor plans both representatives"),
    FirstVelocities.Num(), 2);
  TestEqual(TEXT("two bound flows decoded"), Stats.FlowDecodeCount, 2);
  TestTrue(TEXT("north representative receives north guidance"),
    FirstVelocities.FindRef(Fixture.NorthEntity).Y > 0.0);
  TestTrue(TEXT("south representative receives south guidance"),
    FirstVelocities.FindRef(Fixture.SouthEntity).Y < 0.0);

  const FVector InitialNorthVelocity =
    FirstVelocities.FindRef(Fixture.NorthEntity);
  if (!TestTrue(TEXT("authoritative Worker position changes"),
      Fixture.SetNorthWorkerPosition(FVector(2200.0, 0.0, 60.0))))
    return false;
  FCrowdWorkerWorkItem NorthWork;
  NorthWork.Key.Domain = ECrowdWorkerDomainId::MovementPlanning;
  NorthWork.Key.Kind = ECrowdWorkerWorkKind::Entity;
  NorthWork.Key.PrimaryEntity = Fixture.NorthEntity;
  NorthWork.Priority = ECrowdWorkerWorkPriority::High;
  NorthWork.ReasonMask = 1;
  TMap<FCrowdStableEntityRef, FVector> ReplannedVelocities;
  if (!TestTrue(TEXT("current Worker position replans"),
      Fixture.Plan(NorthWork, ReplannedVelocities)))
    return false;
  TestTrue(TEXT("bound flow sample follows new Worker position"),
    !InitialNorthVelocity.Equals(
      ReplannedVelocities.FindRef(Fixture.NorthEntity), 0.0));
  return true;
}

#endif
