#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "CrowdSharedFlowFieldKernel.h"
#include "MassCrowdWorkerFlowBinding.h"
#include "MassCrowdWorkerFlowResource.h"
#include "MassCrowdWorkerMovementAuthority.h"
#include "MassCrowdWorkerMovementControlResource.h"
#include "MassCrowdWorkerMovementDomain.h"
#include "MassCrowdWorkerNavigationResource.h"
#include "MassCrowdWorkerShadowSync.h"

namespace CrowdWorkerFlowBindingTests
{
  constexpr uint64 Generation = 41;
  constexpr uint64 InputSequence = 7;
  constexpr uint64 Epoch = 11;
  constexpr double FixedDeltaSeconds = 1.0 / 30.0;
  constexpr uint64 ObjectiveA = 101;
  constexpr uint64 ObjectiveB = 102;
  constexpr uint64 FlowA = CrowdWorkerResourceIds::FlowResource(1001);
  constexpr uint64 FlowB = CrowdWorkerResourceIds::FlowResource(1002);
  const FCrowdStableEntityRef EntityA{1, 1001, 1};
  const FCrowdStableEntityRef EntityB{1, 1002, 1};

  FCrowdWorkerPayload MakePayload(const uint32 Value)
  {
    FCrowdWorkerPayload Payload;
    Payload.SchemaId = 0x42544D50u;
    Payload.SchemaVersion = 1;
    Payload.Bytes.SetNumUninitialized(sizeof(Value));
    FMemory::Memcpy(Payload.Bytes.GetData(), &Value, sizeof(Value));
    Payload.RecalculateStableHash();
    return Payload;
  }

  FCrowdWorkerPayload MakeState(
    const FCrowdStableEntityRef& EntityRef,
    const int32 AgentId,
    const FVector& Position,
    const uint64 SourceSequence = InputSequence)
  {
    (void)SourceSequence;
    FCrowdMassBoundaryAgentRecord Record;
    Record.Identity.AgentId = AgentId;
    Record.Identity.SetStableEntityRef(EntityRef);
    Record.AgentFacts.StableEntityRef = EntityRef;
    Record.AgentFacts.CapabilitySet.Bits = 1;
    Record.State.Position = Position;
    Record.State.PlanRevision = 1;
    Record.State.bInitialized = true;
    Record.Properties.PhysicalRadiusCm = 25.0f;
    Record.Properties.HardSafetyGapCm = 5.0f;
    Record.Properties.MaximumSpeedCmps = 200.0f;
    FCrowdWorkerPayload Payload;
    FCrowdWorkerBoundaryStateCodec::EncodeState(Record, Payload);
    return Payload;
  }

  bool BuildFlow(
    const int32 Revision,
    const FVector& Goal,
    FCrowdSharedFlowField& OutFlow)
  {
    FCrowdSharedFlowFieldConfig Config;
    Config.Revision = Revision;
    Config.BoundsMin = FVector(-1000.0, -1000.0, 0.0);
    Config.BoundsMax = FVector(1000.0, 1000.0, 0.0);
    Config.CellSizeCm = 100.0f;
    Config.AgentInflateCm = 0.0f;
    Config.GoalLocation = Goal;
    return FCrowdSharedFlowFieldKernel::Build(Config, OutFlow);
  }

  bool StageFlow(
    FCrowdWorkerResourceStore& Resources,
    const uint64 ResourceId,
    const FCrowdSharedFlowField& Flow)
  {
    FCrowdWorkerPayload Payload;
    return FCrowdWorkerFlowFieldResourceCodec::Encode(Flow, Payload)
      && Resources.StageBuilding({
          ResourceId,
          static_cast<uint64>(Flow.Config.Revision),
          MoveTemp(Payload)}) == ECrowdWorkerQueueResult::Added;
  }

  bool ApplyBinding(
    FCrowdWorkerEntityStateStore& States,
    const FCrowdStableEntityRef& EntityRef,
    const uint64 ObjectiveId,
    const uint32 CohortKey,
    const uint64 FlowResourceId,
    const uint64 Sequence = InputSequence)
  {
    FCrowdWorkerFlowBinding Binding;
    Binding.EntityRef = EntityRef;
    Binding.ObjectiveRef.ObjectiveId = ObjectiveId;
    Binding.CohortKey = CohortKey;
    Binding.FlowResourceId = FlowResourceId;
    FCrowdWorkerDirtyStateRecord Dirty;
    Dirty.EntityRef = EntityRef;
    Dirty.Field = ECrowdWorkerField::FlowBinding;
    Dirty.Generation = Generation;
    Dirty.WorkerEpoch = Epoch;
    Dirty.StateRevision = Sequence;
    Dirty.SourceInputSequence = Sequence;
    if (!FCrowdWorkerFlowBindingCodec::Encode(Binding, Dirty.Payload))
      return false;
    const ECrowdWorkerQueueResult Result = States.ApplyDirty(Dirty);
    return Result == ECrowdWorkerQueueResult::Replaced
      || Result == ECrowdWorkerQueueResult::MergedDuplicate;
  }

  struct FFixture
  {
    FCrowdWorkerEntityStateStore States;
    FCrowdWorkerResourceStore Resources;
    FCrowdWorkerDomainContext Context;

    bool Initialize(
      const bool bReversePublication,
      const bool bExplicitBindings,
      const bool bRunLocalPredictive,
      const bool bPublishEnvironment,
      const FVector& PositionA = FVector(-400.0, 0.0, 0.0),
      const FVector& PositionB = FVector(400.0, 0.0, 0.0))
    {
      if (!States.Reset(8, 1024 * 1024)
        || !Resources.Reset(32 * 1024 * 1024))
        return false;
      const TArray<FCrowdStableEntityRef> SpawnOrder = bReversePublication
        ? TArray<FCrowdStableEntityRef>{EntityB, EntityA}
        : TArray<FCrowdStableEntityRef>{EntityA, EntityB};
      for (const FCrowdStableEntityRef& EntityRef : SpawnOrder)
      {
        const bool bIsA = EntityRef == EntityA;
        if (States.Spawn(
            EntityRef, Generation, InputSequence,
            MakeState(
              EntityRef, bIsA ? 1 : 2,
              bIsA ? PositionA : PositionB))
          != ECrowdWorkerQueueResult::Added)
          return false;
      }

      FCrowdSharedFlowField North;
      FCrowdSharedFlowField South;
      if (!BuildFlow(1, FVector(0.0, 900.0, 0.0), North)
        || !BuildFlow(1, FVector(0.0, -900.0, 0.0), South))
        return false;

      FCrowdWorkerMovementControlResource Control;
      Control.Revision = 1;
      Control.FixedStepIndex = 1;
      Control.PlanRevision = 1;
      Control.bRunLocalPredictive = bRunLocalPredictive;
      Control.LocalPredictiveSettings.FixedStepSeconds =
        FixedDeltaSeconds;
      for (int32 Index = 0; Index < 2; ++Index)
      {
        FCrowdWorkerMovementControlEntry& Entry =
          Control.Entries.AddDefaulted_GetRef();
        Entry.EntityRef = Index == 0 ? EntityA : EntityB;
        Entry.AgentId = Index + 1;
        Entry.MaximumSpeedCmps = 200.0f;
        Entry.AutonomousPreferredVelocity = FVector(25.0, 0.0, 0.0);
      }
      FCrowdWorkerPayload ControlPayload;
      if (!FCrowdWorkerMovementControlResourceCodec::Encode(
          Control, ControlPayload))
        return false;

      struct FStaged
      {
        uint64 Id = 0;
        uint64 Revision = 0;
        FCrowdWorkerPayload Payload;
      };
      TArray<FStaged> Staged;
      Staged.Add({
        CrowdWorkerResourceIds::MovementControl,
        Control.Revision, MoveTemp(ControlPayload)});
      Staged.Add({CrowdWorkerResourceIds::ObjectiveRevision(ObjectiveA),
        1, MakePayload(101)});
      Staged.Add({CrowdWorkerResourceIds::ObjectiveRevision(ObjectiveB),
        1, MakePayload(102)});
      FCrowdWorkerPayload NorthPayload;
      FCrowdWorkerPayload SouthPayload;
      if (!FCrowdWorkerFlowFieldResourceCodec::Encode(
          North, NorthPayload)
        || !FCrowdWorkerFlowFieldResourceCodec::Encode(
          South, SouthPayload))
        return false;
      Staged.Add({FlowA, 1, MoveTemp(NorthPayload)});
      Staged.Add({FlowB, 1, MoveTemp(SouthPayload)});
      if (bPublishEnvironment)
      {
        FCrowdWorkerPayload EnvironmentPayload;
        if (!FCrowdWorkerFlowFieldResourceCodec::Encode(
            North, EnvironmentPayload))
          return false;
        Staged.Add({CrowdWorkerResourceIds::Environment, 1,
          MoveTemp(EnvironmentPayload)});
      }
      if (bReversePublication) Algo::Reverse(Staged);
      for (FStaged& Resource : Staged)
      {
        if (Resources.StageBuilding({
            Resource.Id, Resource.Revision,
            MoveTemp(Resource.Payload)})
          != ECrowdWorkerQueueResult::Added)
          return false;
      }
      TArray<FCrowdWorkerResourceRevisionEvent> Events;
      if (!Resources.CommitBuildingAtEpoch(Epoch, Events))
        return false;

      if (bExplicitBindings)
      {
        const TArray<FCrowdStableEntityRef> BindingOrder =
          bReversePublication
          ? TArray<FCrowdStableEntityRef>{EntityB, EntityA}
          : TArray<FCrowdStableEntityRef>{EntityA, EntityB};
        for (const FCrowdStableEntityRef& EntityRef : BindingOrder)
        {
          if (!ApplyBinding(
              States, EntityRef,
              EntityRef == EntityA ? ObjectiveA : ObjectiveB,
              EntityRef == EntityA ? 11 : 22,
              EntityRef == EntityA ? FlowA : FlowB))
            return false;
        }
      }
      Context.Generation = Generation;
      Context.WorkerEpoch = Epoch;
      Context.AbsoluteSimulationTick = Epoch;
      Context.LastAppliedInputSequence = InputSequence;
      Context.FixedDeltaSeconds = FixedDeltaSeconds;
      Context.SimulationTimeSeconds = FixedDeltaSeconds;
      Context.RuntimeMode = ECrowdWorkerRuntimeV2Mode::Shadow;
      Context.EntityStates = &States;
      Context.Resources = &Resources;
      return true;
    }

    bool Plan(
      const TConstArrayView<FCrowdWorkerWorkItem> Work,
      FCrowdWorkerDomainOutput& OutPlanning,
      TMap<FCrowdStableEntityRef, FVector>* OutVelocities = nullptr)
    {
      FCrowdWorkerMovementPlanningDomainExecutor Planning;
      if (!Planning.Execute(Context, Work, OutPlanning)) return false;
      for (const FCrowdWorkerDirtyStateRecord& Dirty :
        OutPlanning.DirtyStates)
      {
        if (States.ApplyDirty(Dirty)
          != ECrowdWorkerQueueResult::Replaced)
          return false;
      }
      if (OutVelocities)
      {
        FCrowdWorkerMovementDomainExecutor Movement;
        FCrowdWorkerDomainOutput MovementOutput;
        if (!Movement.Execute(
            Context, OutPlanning.NextWork, MovementOutput))
          return false;
        for (const FCrowdWorkerDirtyStateRecord& Dirty :
          MovementOutput.DirtyStates)
        {
          FCrowdWorkerMovementState State;
          if (!FCrowdWorkerMovementStateCodec::Decode(
              Dirty.Payload, State))
            return false;
          OutVelocities->Add(Dirty.EntityRef, State.Velocity);
        }
      }
      return true;
    }
  };

  FCrowdWorkerWorkItem MakeResourcePlanningWork()
  {
    FCrowdWorkerWorkItem Work;
    Work.Key.Domain = ECrowdWorkerDomainId::MovementPlanning;
    Work.Key.Kind = ECrowdWorkerWorkKind::Resource;
    Work.Key.ScopeKey = CrowdWorkerResourceIds::MovementControl;
    Work.Priority = ECrowdWorkerWorkPriority::High;
    Work.ReasonMask = 1;
    return Work;
  }

  FCrowdWorkerWorkItem MakeEntityPlanningWork(
    const FCrowdStableEntityRef& EntityRef)
  {
    FCrowdWorkerWorkItem Work;
    Work.Key.Domain = ECrowdWorkerDomainId::MovementPlanning;
    Work.Key.Kind = ECrowdWorkerWorkKind::Entity;
    Work.Key.PrimaryEntity = EntityRef;
    Work.Priority = ECrowdWorkerWorkPriority::High;
    Work.ReasonMask = 1;
    return Work;
  }
}

using namespace CrowdWorkerFlowBindingTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerFlowBindingCodecTest,
  "MassCrowd.RuntimeV2.FlowBinding.B1CodecDeterminism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerFlowBindingCodecTest::RunTest(const FString& Parameters)
{
  (void)Parameters;
  FCrowdWorkerFlowBinding Binding{
    EntityA, {ObjectiveA}, 11, FlowA};
  FCrowdWorkerPayload First;
  FCrowdWorkerPayload Second;
  TestTrue(TEXT("binding encodes"),
    FCrowdWorkerFlowBindingCodec::Encode(Binding, First));
  TestTrue(TEXT("binding repeat encodes"),
    FCrowdWorkerFlowBindingCodec::Encode(Binding, Second));
  TestTrue(TEXT("binding encoding is byte deterministic"), First == Second);
  TestEqual(TEXT("binding stable hash repeats"),
    First.StableHash, Second.StableHash);
  FCrowdWorkerFlowBinding Decoded;
  TestTrue(TEXT("binding decodes"),
    FCrowdWorkerFlowBindingCodec::Decode(First, Decoded));
  TestTrue(TEXT("binding round trips"), Decoded == Binding);
  FCrowdWorkerPayload Corrupt = First;
  Corrupt.Bytes[0] ^= 1;
  TestFalse(TEXT("corrupt binding hash rejects"),
    FCrowdWorkerFlowBindingCodec::Decode(Corrupt, Decoded));
  Corrupt = First;
  ++Corrupt.SchemaVersion;
  Corrupt.RecalculateStableHash();
  TestFalse(TEXT("unknown binding schema rejects"),
    FCrowdWorkerFlowBindingCodec::Decode(Corrupt, Decoded));
  FCrowdWorkerFlowBinding Invalid = Binding;
  Invalid.ObjectiveRef = {};
  TestFalse(TEXT("invalid objective ref rejects"),
    FCrowdWorkerFlowBindingCodec::Encode(Invalid, Corrupt));
  Invalid = Binding;
  Invalid.FlowResourceId = CrowdWorkerResourceIds::Environment;
  TestFalse(TEXT("non-generic flow resource id rejects"),
    FCrowdWorkerFlowBindingCodec::Encode(Invalid, Corrupt));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerFlowBindingTwoFlowsTest,
  "MassCrowd.RuntimeV2.FlowBinding.B2TwoEntitiesTwoFlows",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerFlowBindingTwoFlowsTest::RunTest(const FString& Parameters)
{
  (void)Parameters;
  FFixture Fixture;
  TestTrue(TEXT("two-flow fixture initializes"),
    Fixture.Initialize(false, true, false, false));
  const FCrowdWorkerWorkItem Work = MakeResourcePlanningWork();
  FCrowdWorkerDomainOutput Output;
  TMap<FCrowdStableEntityRef, FVector> Velocities;
  TestTrue(TEXT("common planning executes two bindings"),
    Fixture.Plan(MakeArrayView(&Work, 1), Output, &Velocities));
  TestEqual(TEXT("two entities planned"), Velocities.Num(), 2);
  TestTrue(TEXT("A samples north flow"),
    Velocities.FindRef(EntityA).Y > 0.0);
  TestTrue(TEXT("B samples south flow"),
    Velocities.FindRef(EntityB).Y < 0.0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerFlowBindingCurrentPositionTest,
  "MassCrowd.RuntimeV2.FlowBinding.B3CurrentPositionSampling",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerFlowBindingCurrentPositionTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FFixture Fixture;
  TestTrue(TEXT("position fixture initializes"),
    Fixture.Initialize(false, true, false, false,
      FVector(-400.0, 0.0, 0.0), FVector(400.0, 0.0, 0.0)));
  const FCrowdWorkerWorkItem FullWork = MakeResourcePlanningWork();
  FCrowdWorkerDomainOutput FirstOutput;
  TMap<FCrowdStableEntityRef, FVector> FirstVelocity;
  TestTrue(TEXT("initial planning executes"),
    Fixture.Plan(
      MakeArrayView(&FullWork, 1), FirstOutput, &FirstVelocity));
  TestTrue(TEXT("current position update applies"),
    Fixture.States.ApplyInputState(
      EntityA, Generation, InputSequence + 1,
      MakeState(EntityA, 1, FVector(400.0, 0.0, 0.0),
        InputSequence + 1)) == ECrowdWorkerQueueResult::Replaced);
  Fixture.Context.WorkerEpoch = Epoch + 1;
  Fixture.Context.AbsoluteSimulationTick = Epoch + 1;
  Fixture.Context.LastAppliedInputSequence = InputSequence + 1;
  const FCrowdWorkerWorkItem EntityWork =
    MakeEntityPlanningWork(EntityA);
  FCrowdWorkerDomainOutput SecondOutput;
  TMap<FCrowdStableEntityRef, FVector> SecondVelocity;
  TestTrue(TEXT("entity replans at new current position"),
    Fixture.Plan(
      MakeArrayView(&EntityWork, 1), SecondOutput, &SecondVelocity));
  TestTrue(TEXT("sample changes after current position changes"),
    !FirstVelocity.FindRef(EntityA).Equals(
      SecondVelocity.FindRef(EntityA), 0.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerFlowBindingEnvironmentFallbackTest,
  "MassCrowd.RuntimeV2.FlowBinding.B4EnvironmentFallback",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerFlowBindingEnvironmentFallbackTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FFixture Fixture;
  TestTrue(TEXT("legacy fixture initializes"),
    Fixture.Initialize(false, false, true, true));
  const FCrowdWorkerWorkItem Work = MakeResourcePlanningWork();
  FCrowdWorkerDomainOutput Output;
  TMap<FCrowdStableEntityRef, FVector> Velocities;
  TestTrue(TEXT("legacy Environment planning executes"),
    Fixture.Plan(MakeArrayView(&Work, 1), Output, &Velocities));
  TestTrue(TEXT("legacy Environment supplies north guidance"),
    Velocities.FindRef(EntityA).Y > 0.0);
  TestTrue(TEXT("legacy dependency remains Environment"),
    Output.DeclaredDependencies.ContainsByPredicate([](
      const FCrowdWorkerDependencyDeclaration& Declaration)
    {
      return Declaration.Source.Kind
          == ECrowdWorkerDependencyKind::Resource
        && Declaration.Source.ScopeKey
          == CrowdWorkerResourceIds::Environment;
    }));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerFlowBindingScopedRevisionTest,
  "MassCrowd.RuntimeV2.FlowBinding.B5ScopedRevision",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerFlowBindingScopedRevisionTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FFixture Fixture;
  TestTrue(TEXT("scoped fixture initializes"),
    Fixture.Initialize(false, true, false, false));
  const FCrowdWorkerWorkItem FullWork = MakeResourcePlanningWork();
  FCrowdWorkerDomainOutput Initial;
  TestTrue(TEXT("initial dependencies publish"),
    Fixture.Plan(MakeArrayView(&FullWork, 1), Initial));
  FCrowdWorkerDependencyIndex Index;
  TestTrue(TEXT("dependency index resets"), Index.Reset(128));
  for (const FCrowdWorkerDependencyDeclaration& Declaration :
    Initial.DeclaredDependencies)
    TestTrue(TEXT("dependency registers"),
      Index.AddDependency(
        Declaration.Source, Declaration.Dependent)
        != ECrowdWorkerQueueResult::RejectedInvalid);
  FCrowdWorkerDependencyKey FlowASource;
  FlowASource.Kind = ECrowdWorkerDependencyKind::Resource;
  FlowASource.ScopeKey = FlowA;
  TArray<FCrowdWorkerWorkItem> Dependents;
  TestEqual(TEXT("Flow A has one scoped dependent"),
    Index.CollectDependents(FlowASource, Dependents), 1);
  if (Dependents.Num() == 1)
  {
    TestEqual(TEXT("Flow A dependent is entity work"),
      Dependents[0].Key.Kind, ECrowdWorkerWorkKind::Entity);
    TestTrue(TEXT("Flow A invalidates only A"),
      Dependents[0].Key.PrimaryEntity == EntityA);
    TestTrue(TEXT("rebind replacement removes old dependent edges"),
      Index.RemoveDependent(Dependents[0].Key) > 0);
    TArray<FCrowdWorkerWorkItem> StaleDependents;
    TestEqual(TEXT("old Flow A edge is gone after replacement"),
      Index.CollectDependents(FlowASource, StaleDependents), 0);
  }
  FCrowdWorkerDomainOutput Scoped;
  TestTrue(TEXT("Flow A dependent replans"),
    Fixture.Plan(Dependents, Scoped));
  TestEqual(TEXT("only A plan is dirtied"), Scoped.DirtyStates.Num(), 1);
  if (Scoped.DirtyStates.Num() == 1)
    TestTrue(TEXT("scoped dirty belongs to A"),
      Scoped.DirtyStates[0].EntityRef == EntityA);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerFlowBindingLifecycleTest,
  "MassCrowd.RuntimeV2.FlowBinding.B6LifecycleStaleRejection",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerFlowBindingLifecycleTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("lifecycle store resets"), States.Reset(4, 4096));
  const FCrowdStableEntityRef Old{1, 77, 1};
  const FCrowdStableEntityRef Replacement{1, 77, 2};
  TestEqual(TEXT("old entity spawns"),
    States.Spawn(Old, Generation, 1, MakeState(Old, 1, FVector::ZeroVector)),
    ECrowdWorkerQueueResult::Added);
  FCrowdWorkerFlowBinding OldBinding{Old, {ObjectiveA}, 5, FlowA};
  FCrowdWorkerPayload Payload;
  TestTrue(TEXT("old binding encodes"),
    FCrowdWorkerFlowBindingCodec::Encode(OldBinding, Payload));
  FCrowdWorkerDirtyStateRecord Dirty;
  Dirty.EntityRef = Old;
  Dirty.Field = ECrowdWorkerField::FlowBinding;
  Dirty.Generation = Generation;
  Dirty.WorkerEpoch = 1;
  Dirty.StateRevision = 2;
  Dirty.SourceInputSequence = 2;
  Dirty.Payload = Payload;
  TestEqual(TEXT("old binding applies"), States.ApplyDirty(Dirty),
    ECrowdWorkerQueueResult::Replaced);
  TestTrue(TEXT("old entity despawns"), States.Despawn(Old));
  TestEqual(TEXT("replacement entity spawns"),
    States.Spawn(
      Replacement, Generation, 3,
      MakeState(Replacement, 1, FVector::ZeroVector)),
    ECrowdWorkerQueueResult::Added);
  TestEqual(TEXT("stale lifecycle binding rejects"),
    States.ApplyDirty(Dirty), ECrowdWorkerQueueResult::RejectedInvalid);
  TestTrue(TEXT("replacement has no inherited binding"),
    States.Find(Replacement, ECrowdWorkerField::FlowBinding) == nullptr);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerFlowBindingOrderingTest,
  "MassCrowd.RuntimeV2.FlowBinding.B7DeterministicOrdering",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerFlowBindingOrderingTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FFixture Forward;
  FFixture Reverse;
  TestTrue(TEXT("forward fixture initializes"),
    Forward.Initialize(false, true, false, false));
  TestTrue(TEXT("reverse fixture initializes"),
    Reverse.Initialize(true, true, false, false));
  const FCrowdWorkerWorkItem Work = MakeResourcePlanningWork();
  FCrowdWorkerDomainOutput ForwardOutput;
  FCrowdWorkerDomainOutput ReverseOutput;
  TestTrue(TEXT("forward planning executes"),
    Forward.Plan(MakeArrayView(&Work, 1), ForwardOutput));
  TestTrue(TEXT("reverse planning executes"),
    Reverse.Plan(MakeArrayView(&Work, 1), ReverseOutput));
  TestEqual(TEXT("publication order keeps semantic output hash"),
    FCrowdWorkerDeterministicShardPlanner::CalculateStableHash(
      ForwardOutput),
    FCrowdWorkerDeterministicShardPlanner::CalculateStableHash(
      ReverseOutput));
  return true;
}

#endif
