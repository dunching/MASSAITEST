#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "MassCrowdAsyncSimulationRuntime.h"
#include "MassCrowdWorkerInteractionDomain.h"
#include "MassCrowdWorkerLifecycleBehaviorDomain.h"
#include "MassCrowdWorkerFlowResource.h"
#include "MassCrowdLocalPredictiveWork.h"
#include "MassCrowdWorkerMovementAuthority.h"
#include "MassCrowdWorkerMovementControlResource.h"
#include "MassCrowdWorkerMovementDomain.h"
#include "MassCrowdWorkerNavigationResource.h"
#include "MassCrowdWorkerNetworkState.h"
#include "MassCrowdWorkerExchange.h"
#include "MassCrowdWorkerRuntimeV2.h"
#include "MassCrowdWorkerShadowSync.h"
#include "MassCrowdWorkerTargetDomain.h"
#include "MassCrowdWorkerProjectileDomain.h"

namespace CrowdWorkerRuntimeV2Tests
{
  FCrowdWorkerPayload MakePayload(
    const uint32 Value,
    const uint32 SchemaId = 91001)
  {
    FCrowdWorkerPayload Payload;
    Payload.SchemaId = SchemaId;
    Payload.SchemaVersion = 1;
    Payload.Bytes.SetNumUninitialized(sizeof(Value));
    FMemory::Memcpy(
      Payload.Bytes.GetData(), &Value, sizeof(Value));
    Payload.RecalculateStableHash();
    return Payload;
  }

  FCrowdWorkerPayload MakeBoundaryStatePayload(
    const FCrowdStableEntityRef& EntityRef,
    const int32 AgentId,
    const FVector& Position)
  {
    FCrowdMassBoundaryAgentRecord Record;
    Record.Identity.AgentId = AgentId;
    Record.Identity.SetStableEntityRef(EntityRef);
    Record.AgentFacts.StableEntityRef = EntityRef;
    Record.AgentFacts.CapabilitySet.Bits = 1;
    Record.State.Position = Position;
    Record.State.PlanRevision = 1;
    Record.State.bInitialized = true;
    FCrowdWorkerPayload Payload;
    if (!FCrowdWorkerBoundaryStateCodec::EncodeState(
        Record, Payload))
      return {};
    return Payload;
  }

  FCrowdWorkerWorkItem MakeEntityWork(
    const ECrowdWorkerDomainId Domain,
    const uint64 EntityId,
    const uint64 ReasonMask = 1,
    const ECrowdWorkerWorkPriority Priority =
      ECrowdWorkerWorkPriority::Normal)
  {
    FCrowdWorkerWorkItem Item;
    Item.Key.Domain = Domain;
    Item.Key.Kind = ECrowdWorkerWorkKind::Entity;
    Item.Key.PrimaryEntity = {1, EntityId, 1};
    Item.Priority = Priority;
    Item.ReasonMask = ReasonMask;
    return Item;
  }

  class FRecordingDomain final :
    public ICrowdWorkerDomainExecutor
  {
  public:
    FRecordingDomain(
      const ECrowdWorkerDomainId InDomain,
      TArray<ECrowdWorkerDomainId> InDependencies,
      TArray<ECrowdWorkerDomainId>& InOrder)
      : Domain(InDomain)
      , Dependencies(MoveTemp(InDependencies))
      , Order(InOrder)
    {
    }

    virtual ECrowdWorkerDomainId GetDomainId() const override
    {
      return Domain;
    }

    virtual void GetDependencies(
      TArray<ECrowdWorkerDomainId>& OutDependencies)
      const override
    {
      OutDependencies = Dependencies;
    }

    virtual bool Execute(
      const FCrowdWorkerDomainContext&,
      const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
      FCrowdWorkerDomainOutput&) override
    {
      if (WorkItems.IsEmpty()) return false;
      Order.Add(Domain);
      return true;
    }

  private:
    ECrowdWorkerDomainId Domain;
    TArray<ECrowdWorkerDomainId> Dependencies;
    TArray<ECrowdWorkerDomainId>& Order;
  };

  class FNoopBehaviorEvaluator final :
    public ICrowdBehaviorSourceEvaluator
  {
  public:
    virtual bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdBusinessContribution Business;
      Business.AdapterId = 1;
      Business.CommitId =
        Context.Instance.Handle.SourceSequence;
      Business.InstigatorRef =
        Context.Instance.Handle.EntityRef;
      Business.PayloadTypeId = 1;
      Business.Quantity = 1;
      return Writer.AddBusiness(Business);
    }
  };

  class FSyntheticLifecycleDomain final :
    public ICrowdWorkerDomainExecutor
  {
  public:
    virtual ECrowdWorkerDomainId GetDomainId() const override
    {
      return ECrowdWorkerDomainId::LifecycleInput;
    }

    virtual void GetDependencies(
      TArray<ECrowdWorkerDomainId>& OutDependencies)
      const override
    {
      OutDependencies.Reset();
    }

    virtual bool Execute(
      const FCrowdWorkerDomainContext& Context,
      const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
      FCrowdWorkerDomainOutput& OutOutput) override
    {
      for (const FCrowdWorkerWorkItem& Work : WorkItems)
      {
        FCrowdWorkerDirtyStateRecord Dirty;
        Dirty.EntityRef = Work.Key.PrimaryEntity;
        Dirty.Field = ECrowdWorkerField::Lifecycle;
        Dirty.Generation = Context.Generation;
        Dirty.WorkerEpoch = Context.WorkerEpoch;
        Dirty.StateRevision = Context.WorkerEpoch;
        Dirty.CorrectionRevision = Context.CorrectionRevision;
        Dirty.Payload = MakePayload(
          static_cast<uint32>(
            Work.Key.PrimaryEntity.StableEntityId),
          91002);
        OutOutput.DirtyStates.Add(MoveTemp(Dirty));

        FCrowdWorkerDependencyDeclaration Declaration;
        Declaration.Source = {
          ECrowdWorkerDependencyKind::Resource, {}, 77};
        Declaration.Dependent = Work;
        OutOutput.DeclaredDependencies.Add(Declaration);
        FCrowdWorkerDependencyObservation Observation;
        Observation.Source = Declaration.Source;
        Observation.Dependent = Work.Key;
        OutOutput.ObservedDependencies.Add(Observation);
      }
      return true;
    }
  };

  class FCorrectionRevisionBehaviorDomain final :
    public ICrowdWorkerDomainExecutor
  {
  public:
    explicit FCorrectionRevisionBehaviorDomain(
      TSharedPtr<TAtomic<int32>, ESPMode::ThreadSafe>
        InCorrectionApplyCount = {})
      : CorrectionApplyCount(MoveTemp(InCorrectionApplyCount))
    {
    }

    virtual ECrowdWorkerDomainId GetDomainId() const override
    {
      return ECrowdWorkerDomainId::Behavior;
    }

    virtual void GetDependencies(
      TArray<ECrowdWorkerDomainId>& OutDependencies)
      const override
    {
      OutDependencies = {ECrowdWorkerDomainId::LifecycleInput};
    }

    virtual bool Execute(
      const FCrowdWorkerDomainContext& Context,
      const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
      FCrowdWorkerDomainOutput& OutOutput) override
    {
      for (const FCrowdWorkerWorkItem& Work : WorkItems)
      {
        if (!Work.Key.PrimaryEntity.IsValid())
          continue;
        FCrowdWorkerDirtyStateRecord& Dirty =
          OutOutput.DirtyStates.AddDefaulted_GetRef();
        Dirty.EntityRef = Work.Key.PrimaryEntity;
        Dirty.Field = ECrowdWorkerField::Behavior;
        Dirty.Generation = Context.Generation;
        Dirty.WorkerEpoch = Context.WorkerEpoch;
        Dirty.StateRevision = Context.WorkerEpoch;
        // Deliberately omit the correction fence, matching the production
        // Behavior domain that exposed T7. The Owner dispatch boundary must
        // stamp every shard-local record with Context.CorrectionRevision.
        Dirty.SourceInputSequence =
          Context.LastAppliedInputSequence;
        Dirty.Payload = MakePayload(
          static_cast<uint32>(
            Work.Key.PrimaryEntity.StableEntityId),
          91007);

        FCrowdWorkerDependencyDeclaration& Declaration =
          OutOutput.DeclaredDependencies.AddDefaulted_GetRef();
        Declaration.Source.Kind =
          ECrowdWorkerDependencyKind::Entity;
        Declaration.Source.EntityRef =
          Work.Key.PrimaryEntity;
        Declaration.Source.ScopeKey =
          CrowdWorkerRuntimeV2DependencyScopeForField(
            ECrowdWorkerField::Lifecycle);
        Declaration.Dependent = Work;
        FCrowdWorkerDependencyObservation& Observation =
          OutOutput.ObservedDependencies.AddDefaulted_GetRef();
        Observation.Source = Declaration.Source;
        Observation.Dependent = Work.Key;
      }
      return true;
    }

    virtual bool ApplyAuthorityCorrection(
      const FCrowdWorkerDomainContext& Context,
      const TConstArrayView<FCrowdWorkerDirtyStateRecord> Records) override
    {
      if (Context.Generation == 0 || Records.IsEmpty())
        return false;
      if (CorrectionApplyCount)
      {
        CorrectionApplyCount->Store(
          CorrectionApplyCount->Load() + 1);
      }
      return true;
    }

  private:
    TSharedPtr<TAtomic<int32>, ESPMode::ThreadSafe>
      CorrectionApplyCount;
  };

  class FLeakyLifecycleDomain final :
    public ICrowdWorkerDomainExecutor
  {
  public:
    virtual ECrowdWorkerDomainId GetDomainId() const override
    {
      return ECrowdWorkerDomainId::LifecycleInput;
    }

    virtual void GetDependencies(
      TArray<ECrowdWorkerDomainId>& OutDependencies)
      const override
    {
      OutDependencies.Reset();
    }

    virtual bool Execute(
      const FCrowdWorkerDomainContext&,
      const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
      FCrowdWorkerDomainOutput& OutOutput) override
    {
      for (const FCrowdWorkerWorkItem& Work : WorkItems)
      {
        FCrowdWorkerDependencyObservation Observation;
        Observation.Source = {
          ECrowdWorkerDependencyKind::Resource, {}, 77};
        Observation.Dependent = Work.Key;
        OutOutput.ObservedDependencies.Add(Observation);
      }
      return true;
    }
  };

  struct FStageBarrierProbe
  {
    TAtomic<bool> bLifecycleCompleted{false};
    TAtomic<bool> bFlowObservedLifecycle{false};
  };

  class FSlowLifecycleDomain final :
    public ICrowdWorkerDomainExecutor
  {
  public:
    explicit FSlowLifecycleDomain(
      TSharedPtr<FStageBarrierProbe, ESPMode::ThreadSafe>
        InProbe = {})
      : Probe(MoveTemp(InProbe))
    {
    }

    virtual ECrowdWorkerDomainId GetDomainId() const override
    {
      return ECrowdWorkerDomainId::LifecycleInput;
    }

    virtual void GetDependencies(
      TArray<ECrowdWorkerDomainId>& OutDependencies)
      const override
    {
      OutDependencies.Reset();
    }

    virtual bool Execute(
      const FCrowdWorkerDomainContext& Context,
      const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
      FCrowdWorkerDomainOutput& OutOutput) override
    {
      FPlatformProcess::SleepNoStats(0.025f);
      FSyntheticLifecycleDomain Delegate;
      const bool bSucceeded =
        Delegate.Execute(Context, WorkItems, OutOutput);
      if (Probe)
        Probe->bLifecycleCompleted.Store(true);
      return bSucceeded;
    }

  private:
    TSharedPtr<FStageBarrierProbe, ESPMode::ThreadSafe> Probe;
  };

  class FBarrierFlowDomain final :
    public ICrowdWorkerDomainExecutor
  {
  public:
    explicit FBarrierFlowDomain(
      TSharedPtr<FStageBarrierProbe, ESPMode::ThreadSafe>
        InProbe)
      : Probe(MoveTemp(InProbe))
    {
    }

    virtual ECrowdWorkerDomainId GetDomainId() const override
    {
      return ECrowdWorkerDomainId::FlowResource;
    }

    virtual void GetDependencies(
      TArray<ECrowdWorkerDomainId>& OutDependencies)
      const override
    {
      OutDependencies = {
        ECrowdWorkerDomainId::LifecycleInput};
    }

    virtual bool Execute(
      const FCrowdWorkerDomainContext&,
      const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
      FCrowdWorkerDomainOutput&) override
    {
      const bool bObserved = !WorkItems.IsEmpty()
        && Probe && Probe->bLifecycleCompleted.Load();
      if (Probe)
        Probe->bFlowObservedLifecycle.Store(bObserved);
      return bObserved;
    }

  private:
    TSharedPtr<FStageBarrierProbe, ESPMode::ThreadSafe> Probe;
  };

  class FPropagatingLifecycleDomain final :
    public ICrowdWorkerDomainExecutor
  {
  public:
    virtual ECrowdWorkerDomainId GetDomainId() const override
    {
      return ECrowdWorkerDomainId::LifecycleInput;
    }

    virtual void GetDependencies(
      TArray<ECrowdWorkerDomainId>& OutDependencies)
      const override
    {
      OutDependencies.Reset();
    }

    virtual bool Execute(
      const FCrowdWorkerDomainContext&,
      const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
      FCrowdWorkerDomainOutput& OutOutput) override
    {
      OutOutput.NextWork.Append(WorkItems);
      return !WorkItems.IsEmpty();
    }
  };

  FCrowdWorkerDomainShardResult MakeShardResult(
    const FCrowdWorkerDomainShard& Shard)
  {
    FCrowdWorkerDomainShardResult Result;
    Result.Domain = Shard.Domain;
    Result.ShardOrdinal = Shard.ShardOrdinal;
    Result.bSucceeded = true;
    for (const FCrowdWorkerWorkItem& Work : Shard.WorkItems)
    {
      const uint64 EntityId =
        Work.Key.PrimaryEntity.StableEntityId;
      FCrowdWorkerDirtyStateRecord Dirty;
      Dirty.EntityRef = Work.Key.PrimaryEntity;
      Dirty.Field = ECrowdWorkerField::Movement;
      Dirty.Generation = 7;
      Dirty.WorkerEpoch = 3;
      Dirty.StateRevision = EntityId;
      Dirty.CorrectionRevision = Work.CorrectionRevision;
      Dirty.Payload = MakePayload(
        static_cast<uint32>(EntityId), 91005);
      Result.Output.DirtyStates.Add(MoveTemp(Dirty));

      FCrowdWorkerGameplayEvent Event;
      Event.EntityRef = Work.Key.PrimaryEntity;
      Event.Generation = 7;
      Event.WorkerEpoch = 3;
      Event.EventSequence = EntityId;
      Event.EventId = 92000 + EntityId;
      Event.Payload = MakePayload(
        static_cast<uint32>(EntityId), 91006);
      Event.RecalculateStableHash();
      Result.Output.OrderedEvents.Add(MoveTemp(Event));

      FCrowdWorkerWorkItem Next = Work;
      Next.Key.Domain = ECrowdWorkerDomainId::Movement;
      Next.ReasonMask = 1;
      Result.Output.NextWork.Add(MoveTemp(Next));
    }
    return Result;
  }

  FCrowdAsyncSimulationRuntimeConfig MakeSyntheticConfig()
  {
    FCrowdAsyncSimulationRuntimeConfig Config;
    Config.ContractLimits.MaxPayloadBytes = 64;
    Config.ContractLimits.MaxInputRecordsPerBatch = 8;
    Config.ContractLimits.MaxStatePatchesPerSlot = 8;
    Config.ContractLimits.MaxPendingOrderedEvents = 8;
    Config.FixedSimulationQuantumSeconds = 1.0 / 30.0;
    Config.MaxQueuedInputBatches = 4;
    Config.MaxInputBatchesPerPump = 2;
    Config.MaxSimulationStepsPerPump = 2;
    Config.MaxPendingCommands = 8;
    Config.MaxInFlightShadowWorks = 4;
    Config.WorkerV2 =
      FCrowdWorkerRuntimeV2Config::MakeProduction10k();
    Config.WorkerV2.Mode = ECrowdWorkerRuntimeV2Mode::Shadow;
    Config.WorkerV2.MaxWorkItems = 8;
    Config.WorkerV2.MaxWakeups = 8;
    Config.WorkerV2.MaxDependencyEdges = 8;
    Config.WorkerV2.MaxDirtyEntities = 8;
    Config.WorkerV2.MaxOrderedEvents = 8;
    Config.WorkerV2.bEnableSyntheticShadow = true;
    return Config;
  }

  FCrowdWorkerIntentBatch MakeSyntheticSnapshot(
    const uint64 Generation)
  {
    FCrowdWorkerIntentBatch Snapshot;
    Snapshot.Generation = Generation;
    Snapshot.FirstInputSequence = 1;
    Snapshot.LastInputSequence = 3;
    Snapshot.TargetSimulationTimeSeconds = 1.0 / 30.0;
    FCrowdWorkerSpawnDelta Spawn;
    Spawn.InputSequence = 1;
    Spawn.EntityRef = {1, 42, 1};
    Spawn.InitialState = MakePayload(42, 91003);
    Snapshot.Spawns.Add(Spawn);
    FCrowdWorkerResourceDelta Resource;
    Resource.InputSequence = 2;
    Resource.ResourceId = 77;
    Resource.Revision = 1;
    Resource.Payload = MakePayload(100, 91004);
    Snapshot.ResourceDeltas.Add(Resource);
    Snapshot.Clock.InputSequence = 3;
    Snapshot.Clock.SimulationTick = 1;
    Snapshot.RecalculateStableHash();
    return Snapshot;
  }

  struct FObjectiveClockObservation
  {
    FCriticalSection Mutex;
    uint64 AbsoluteSimulationTick = 0;
    int32 EffectiveFixedStepIndex = INDEX_NONE;
    uint64 ObjectiveResourceRevision = 0;
    int32 TargetRevision = INDEX_NONE;
    double ObjectiveAgeSeconds = -1.0;
    FVector2f BaseLocation = FVector2f::ZeroVector;
    FVector2f EffectiveLocation = FVector2f::ZeroVector;
    int32 ExecuteCount = 0;
  };

  class FObjectiveClockRecordingTargetDomain final
    : public ICrowdWorkerDomainExecutor
  {
  public:
    explicit FObjectiveClockRecordingTargetDomain(
      FObjectiveClockObservation& InObservation)
      : Observation(InObservation)
    {
    }

    virtual ECrowdWorkerDomainId GetDomainId() const override
    {
      return ECrowdWorkerDomainId::Target;
    }

    virtual void GetDependencies(
      TArray<ECrowdWorkerDomainId>& OutDependencies) const override
    {
      OutDependencies.Reset();
    }

    virtual bool Execute(
      const FCrowdWorkerDomainContext& Context,
      const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
      FCrowdWorkerDomainOutput&) override
    {
      if (!Context.Resources || WorkItems.IsEmpty())
        return false;
      const FCrowdWorkerResourceRecord* Record =
        Context.Resources->FindCurrent(
          CrowdWorkerResourceIds::ObjectiveRevision(
            CrowdWorkerTargetObjectiveIds::PrimaryTarget));
      FCrowdWorkerTargetObjectiveRevision Objective;
      if (!Record
        || !FCrowdWorkerTargetObjectiveRevisionCodec::Decode(
          Record->Payload, Objective)
        || Objective.EffectiveFixedStepIndex
          > static_cast<int64>(Context.AbsoluteSimulationTick))
        return false;
      const double AgeSeconds =
        static_cast<double>(Context.AbsoluteSimulationTick
          - static_cast<uint64>(
            Objective.EffectiveFixedStepIndex))
        * Context.FixedDeltaSeconds;
      FScopeLock Lock(&Observation.Mutex);
      Observation.AbsoluteSimulationTick =
        Context.AbsoluteSimulationTick;
      Observation.EffectiveFixedStepIndex =
        Objective.EffectiveFixedStepIndex;
      Observation.ObjectiveResourceRevision = Record->Revision;
      Observation.TargetRevision = Objective.TargetRevision;
      Observation.ObjectiveAgeSeconds = AgeSeconds;
      Observation.BaseLocation = Objective.TargetLocation;
      Observation.EffectiveLocation = Objective.TargetLocation
        + Objective.TargetVelocity
          * static_cast<float>(AgeSeconds);
      ++Observation.ExecuteCount;
      return true;
    }

  private:
    FObjectiveClockObservation& Observation;
  };

  bool WaitForRuntimeIdle(
    FCrowdAsyncSimulationRuntime& Runtime,
    const double TimeoutSeconds = 5.0)
  {
    const double Deadline = FPlatformTime::Seconds()
      + TimeoutSeconds;
    while (FPlatformTime::Seconds() < Deadline)
    {
      const ECrowdAsyncSimulationPollResult Result = Runtime.Poll();
      const FCrowdAsyncSimulationRuntimeMetrics Metrics =
        Runtime.GetMetrics();
      if (Result == ECrowdAsyncSimulationPollResult::Failed)
        return false;
      if (Result == ECrowdAsyncSimulationPollResult::Idle
        && Metrics.InputQueueDepth == 0
        && Metrics.SimulationTimeSeconds
          + UE_DOUBLE_SMALL_NUMBER
          >= Metrics.TargetSimulationTimeSeconds)
        return true;
      FPlatformProcess::SleepNoStats(0.0f);
    }
    return false;
  }

  bool WaitForCorrectionCount(
    FCrowdAsyncSimulationRuntime& Runtime,
    const uint64 ExpectedCount,
    const double TimeoutSeconds = 5.0)
  {
    const double Deadline = FPlatformTime::Seconds()
      + TimeoutSeconds;
    while (FPlatformTime::Seconds() < Deadline)
    {
      if (Runtime.Poll() == ECrowdAsyncSimulationPollResult::Failed)
        return false;
      if (Runtime.GetMetrics().AuthorityCorrectionCount
          >= ExpectedCount)
        return true;
      FPlatformProcess::SleepNoStats(0.0f);
    }
    return false;
  }

  FCrowdWorkerAuthorityCorrectionBatch MakeBehaviorCorrection(
    const uint64 Generation,
    const uint64 CorrectionSequence,
    const FCrowdAsyncSimulationRuntimeMetrics& Metrics)
  {
    const FCrowdStableEntityRef EntityRef{1, 42, 1};
    FCrowdWorkerAuthorityCorrectionBatch Correction;
    Correction.Generation = Generation;
    Correction.CorrectionSequence = CorrectionSequence;
    Correction.ApplySimulationTick = FMath::Max<uint64>(
      1, Metrics.AbsoluteSimulationTick);
    Correction.ThroughInputSequence = FMath::Max<uint64>(
      1, Metrics.LastAppliedInputSequence);
    FCrowdWorkerAuthorityScopeKey Scope;
    Scope.Field = ECrowdWorkerField::Behavior;
    Correction.Scopes.Add(Scope);
    Correction.AuthoritativeMembers.Add(EntityRef);
    FCrowdWorkerDirtyStateRecord Record;
    Record.EntityRef = EntityRef;
    Record.Field = ECrowdWorkerField::Behavior;
    Record.Generation = Generation;
    Record.WorkerEpoch = FMath::Max<uint64>(1, Metrics.WorkerEpoch);
    Record.StateRevision = FMath::Max<uint64>(
      1, Metrics.WorkerEpoch + 1);
    Record.CorrectionRevision = CorrectionSequence;
    Record.SourceInputSequence = Correction.ThroughInputSequence;
    Record.Payload = MakePayload(42, 91007);
    Correction.Records.Add(MoveTemp(Record));
    Correction.RecalculateStableHash();
    return Correction;
  }

  bool StartCorrectionRuntime(
    FCrowdAsyncSimulationRuntime& Runtime,
    const uint64 Generation,
    const bool bSlowLifecycle = false,
    TSharedPtr<TAtomic<int32>, ESPMode::ThreadSafe>
      CorrectionApplyCount = {})
  {
    const bool bLifecycleRegistered = bSlowLifecycle
      ? Runtime.RegisterDomainExecutor(
          MakeUnique<FSlowLifecycleDomain>())
      : Runtime.RegisterDomainExecutor(
          MakeUnique<FSyntheticLifecycleDomain>());
    return bLifecycleRegistered
      && Runtime.RegisterDomainExecutor(
        MakeUnique<FCorrectionRevisionBehaviorDomain>(
          MoveTemp(CorrectionApplyCount)))
      && Runtime.Start(MakeSyntheticConfig(), Generation);
  }

  FCrowdWorkerIntentBatch MakeClockIntent(
    const uint64 Generation,
    const uint64 InputSequence,
    const uint64 SimulationTick)
  {
    FCrowdWorkerIntentBatch Clock;
    Clock.Generation = Generation;
    Clock.FirstInputSequence = InputSequence;
    Clock.LastInputSequence = InputSequence;
    Clock.TargetSimulationTimeSeconds =
      static_cast<double>(SimulationTick) / 30.0;
    Clock.Clock.InputSequence = InputSequence;
    Clock.Clock.SimulationTick = SimulationTick;
    Clock.RecalculateStableHash();
    return Clock;
  }
}

using namespace CrowdWorkerRuntimeV2Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerMovingObjectiveAbsoluteClockTest,
  "MassCrowd.RuntimeV2.Target.MovingObjectiveAbsoluteClock",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerMovingObjectiveAbsoluteClockTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 31;
  constexpr double FixedStepSeconds = 1.0 / 30.0;
  constexpr double PreRoundUptimeSeconds = 9.0;

  FCrowdAsyncSimulationRuntimeConfig Config = MakeSyntheticConfig();
  Config.ContractLimits.MaxPayloadBytes = 4 * 1024 * 1024;
  Config.ContractLimits.MaxInputRecordsPerBatch = 16;
  Config.MaxSimulationStepsPerPump = 512;
  Config.WorkerV2.Mode = ECrowdWorkerRuntimeV2Mode::Production;
  Config.WorkerV2.bEnableSyntheticShadow = false;

  FObjectiveClockObservation Observation;
  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(TEXT("recording target domain registers"),
    Runtime.RegisterDomainExecutor(
      MakeUnique<FObjectiveClockRecordingTargetDomain>(
        Observation)));
  TestTrue(TEXT("runtime starts before moving round"),
    Runtime.Start(Config, Generation));

  FCrowdWorkerIntentBatch UptimeSnapshot;
  UptimeSnapshot.Generation = Generation;
  UptimeSnapshot.FirstInputSequence = 1;
  UptimeSnapshot.LastInputSequence = 1;
  UptimeSnapshot.TargetSimulationTimeSeconds =
    PreRoundUptimeSeconds;
  UptimeSnapshot.Clock.InputSequence = 1;
  UptimeSnapshot.Clock.SimulationTick = 270;
  UptimeSnapshot.RecalculateStableHash();
  TestEqual(TEXT("non-zero uptime snapshot accepted"),
    Runtime.SubmitResnapshot(UptimeSnapshot),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("runtime advances through pre-round uptime"),
    WaitForRuntimeIdle(Runtime));
  TestEqual(TEXT("pre-round absolute tick is retained"),
    Runtime.GetMetrics().AbsoluteSimulationTick,
    uint64{270});

  FCrowdWorkerTargetControlResource Control;
  Control.Revision = 1;
  FCrowdWorkerTargetCohortInput& Cohort =
    Control.Cohorts.AddDefaulted_GetRef();
  Cohort.CohortKey = 0;
  Cohort.TopologyRevision = 1;
  Cohort.TargetRevision = 1;
  Cohort.FixedStepIndex = 0;
  Cohort.FlowConfig.Revision = 1;
  Cohort.FlowConfig.BoundsMin =
    FVector(-2000.0f, -2000.0f, 0.0f);
  Cohort.FlowConfig.BoundsMax =
    FVector(2000.0f, 2000.0f, 0.0f);
  Cohort.FlowConfig.CellSizeCm = 100.0f;
  FCrowdWorkerTargetAgentInput& Agent =
    Cohort.Agents.AddDefaulted_GetRef();
  Agent.EntityRef = {1, 1, 1};
  Agent.Agent.AgentId = 1;
  Agent.Agent.MaxSpeedCmps = 300.0f;

  FCrowdWorkerPayload ControlPayload;
  TestTrue(TEXT("moving target control encodes"),
    FCrowdWorkerTargetControlResourceCodec::Encode(
      Control, ControlPayload));

  const auto SubmitObjective = [&Runtime, &ControlPayload](
    const uint64 FirstSequence,
    const uint64 ObjectiveRevision,
    const uint64 RoundTick,
    const FVector2f BaseLocation)
  {
    const double TargetTime = PreRoundUptimeSeconds
      + static_cast<double>(RoundTick) * FixedStepSeconds;
    int32 EffectiveTick = INDEX_NONE;
    if (!FCrowdWorkerTargetObjectiveClock::
        ResolveEffectiveFixedStepIndex(
          TargetTime, FixedStepSeconds, EffectiveTick))
      return false;
    FCrowdWorkerTargetObjectiveRevision Objective;
    Objective.TargetRevision = 1;
    Objective.EffectiveFixedStepIndex = EffectiveTick;
    Objective.TargetLocation = BaseLocation;
    Objective.TargetVelocity = FVector2f(-90.0f, 0.0f);
    FCrowdWorkerPayload ObjectivePayload;
    if (!FCrowdWorkerTargetObjectiveRevisionCodec::Encode(
        Objective, ObjectivePayload))
      return false;

    FCrowdWorkerIntentBatch Batch;
    Batch.Generation = Generation;
    Batch.FirstInputSequence = FirstSequence;
    FCrowdWorkerObjectiveRevisionDelta& ObjectiveDelta =
      Batch.ObjectiveRevisions.AddDefaulted_GetRef();
    ObjectiveDelta.InputSequence = FirstSequence;
    ObjectiveDelta.ObjectiveId =
      CrowdWorkerTargetObjectiveIds::PrimaryTarget;
    ObjectiveDelta.Revision = ObjectiveRevision;
    ObjectiveDelta.Payload = MoveTemp(ObjectivePayload);
    if (ObjectiveRevision == 1)
    {
      FCrowdWorkerResourceDelta& ControlDelta =
        Batch.ResourceDeltas.AddDefaulted_GetRef();
      ControlDelta.InputSequence = FirstSequence + 1;
      ControlDelta.ResourceId = CrowdWorkerResourceIds::TargetControl;
      ControlDelta.Revision = 1;
      ControlDelta.Payload = ControlPayload;
      Batch.Clock.InputSequence = FirstSequence + 2;
    }
    else
    {
      Batch.Clock.InputSequence = FirstSequence + 1;
    }
    Batch.Clock.SimulationTick =
      static_cast<uint64>(EffectiveTick);
    Batch.LastInputSequence = Batch.Clock.InputSequence;
    Batch.TargetSimulationTimeSeconds = TargetTime;
    Batch.RecalculateStableHash();
    return Runtime.SubmitIntentBatch(Batch)
      == ECrowdAsyncSimulationSubmitResult::Accepted;
  };

  TestTrue(TEXT("first moving objective accepted"),
    SubmitObjective(2, 1, 1, FVector2f::ZeroVector));
  TestTrue(TEXT("first moving objective reaches idle"),
    WaitForRuntimeIdle(Runtime));
  {
    FScopeLock Lock(&Observation.Mutex);
    TestEqual(TEXT("objective effective tick matches Worker absolute tick"),
      Observation.EffectiveFixedStepIndex,
      static_cast<int32>(Observation.AbsoluteSimulationTick));
    TestEqual(TEXT("pre-round ticks do not enter objective age"),
      Observation.ObjectiveAgeSeconds, 0.0);
    TestEqual(TEXT("TargetRevision remains plan revision"),
      Observation.TargetRevision, 1);
    TestTrue(TEXT("zero age preserves objective base location"),
      Observation.EffectiveLocation.Equals(
        Observation.BaseLocation, 0.001f));
  }

  TestTrue(TEXT("second moving objective accepted"),
    SubmitObjective(5, 2, 2, FVector2f(-3.0f, 0.0f)));
  TestTrue(TEXT("second moving objective reaches idle"),
    WaitForRuntimeIdle(Runtime));
  {
    FScopeLock Lock(&Observation.Mutex);
    TestEqual(TEXT("objective resource revision advances"),
      Observation.ObjectiveResourceRevision, uint64{2});
    TestEqual(TEXT("updated objective remains in absolute clock"),
      Observation.EffectiveFixedStepIndex,
      static_cast<int32>(Observation.AbsoluteSimulationTick));
    TestEqual(TEXT("updated objective age excludes uptime"),
      Observation.ObjectiveAgeSeconds, 0.0);
    TestEqual(TEXT("moving objective keeps TargetRevision constant"),
      Observation.TargetRevision, 1);
  }

  TestTrue(TEXT("objective clock runtime drains"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerWorkRingEpochTest,
  "MassCrowd.RuntimeV2.WorkRingEpochDedupFairness",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerWorkRingEpochTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdWorkerWorkRing Ring;
  TestTrue(TEXT("reset"), Ring.Reset(4, 10));
  TestEqual(
    TEXT("add movement"),
    Ring.EnqueueCurrent(MakeEntityWork(
      ECrowdWorkerDomainId::Movement, 2, 1)),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("dedup merges reason"),
    Ring.EnqueueCurrent(MakeEntityWork(
      ECrowdWorkerDomainId::Movement, 2, 4,
      ECrowdWorkerWorkPriority::High)),
    ECrowdWorkerQueueResult::MergedDuplicate);
  TestEqual(
    TEXT("add lifecycle"),
    Ring.EnqueueCurrent(MakeEntityWork(
      ECrowdWorkerDomainId::LifecycleInput, 1, 2,
      ECrowdWorkerWorkPriority::High)),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("add next"),
    Ring.EnqueueNext(MakeEntityWork(
      ECrowdWorkerDomainId::Behavior, 3)),
    ECrowdWorkerQueueResult::Added);

  FCrowdWorkerWorkItem First;
  FCrowdWorkerWorkItem Second;
  TestTrue(TEXT("first pop"), Ring.PopCurrent(First));
  TestTrue(TEXT("second pop"), Ring.PopCurrent(Second));
  TestEqual(
    TEXT("fair high priority begins lifecycle"),
    First.Key.Domain,
    ECrowdWorkerDomainId::LifecycleInput);
  TestEqual(
    TEXT("merged priority retained"),
    Second.Priority,
    ECrowdWorkerWorkPriority::High);
  TestEqual(TEXT("merged reasons"), Second.ReasonMask, uint64{5});
  Ring.AdvanceEpoch();
  TestEqual(TEXT("epoch advanced"), Ring.GetEpoch(), uint64{11});
  FCrowdWorkerWorkItem Next;
  TestTrue(TEXT("next became current"), Ring.PopCurrent(Next));
  TestEqual(
    TEXT("next domain"),
    Next.Key.Domain,
    ECrowdWorkerDomainId::Behavior);
  FCrowdWorkerWorkRing CapacityRing;
  TestTrue(
    TEXT("capacity ring reset"),
    CapacityRing.Reset(1, 1));
  TestEqual(
    TEXT("capacity ring first item"),
    CapacityRing.EnqueueCurrent(MakeEntityWork(
      ECrowdWorkerDomainId::Movement, 90)),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("capacity ring rejects overflow"),
    CapacityRing.EnqueueCurrent(MakeEntityWork(
      ECrowdWorkerDomainId::Movement, 91)),
    ECrowdWorkerQueueResult::RejectedCapacity);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerTimeWheelTest,
  "MassCrowd.RuntimeV2.TimeWheelOrderCancelRevision",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerTimeWheelTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdWorkerTimeWheel Wheel;
  TestTrue(TEXT("reset"), Wheel.Reset(3));
  FCrowdWorkerWakeup Later;
  Later.Key = {
    ECrowdWorkerDomainId::Movement, {1, 2, 1}, 1};
  Later.AbsoluteSimulationTick = 9;
  Later.Revision = 1;
  FCrowdWorkerWakeup Earlier;
  Earlier.Key = {
    ECrowdWorkerDomainId::CombatReactive, {1, 1, 1}, 2};
  Earlier.AbsoluteSimulationTick = 4;
  Earlier.Revision = 1;
  TestEqual(
    TEXT("later scheduled"),
    Wheel.Schedule(Later),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("earlier scheduled"),
    Wheel.Schedule(Earlier),
    ECrowdWorkerQueueResult::Added);
  FCrowdWorkerWakeup Revised = Later;
  Revised.AbsoluteSimulationTick = 5;
  Revised.Revision = 2;
  TestEqual(
    TEXT("new revision reschedules"),
    Wheel.Schedule(Revised),
    ECrowdWorkerQueueResult::Replaced);
  TestEqual(TEXT("two scheduled"), Wheel.Num(), 2);
  TArray<FCrowdWorkerWakeup> Due;
  TestEqual(TEXT("drain due"), Wheel.DrainDue(5, Due), 2);
  TestEqual(
    TEXT("absolute tick ordering"),
    Due[0].AbsoluteSimulationTick,
    uint64{4});
  TestEqual(
    TEXT("revised tick"),
    Due[1].AbsoluteSimulationTick,
    uint64{5});
  TestEqual(TEXT("empty"), Wheel.Num(), 0);
  FCrowdWorkerWakeup Baseline = Later;
  Baseline.Key.EntityRef = {1, 3, 1};
  Baseline.AbsoluteSimulationTick = 6;
  Baseline.Revision = 0;
  TestEqual(
    TEXT("zero correction revision is a valid baseline"),
    Wheel.Schedule(Baseline),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("first correction invalidates baseline wakeup"),
    Wheel.InvalidateEntityRevision(
      Baseline.Key.EntityRef, 1),
    1);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerDependencyResourceDirtyTest,
  "MassCrowd.RuntimeV2.DependencyResourceDirty",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerDependencyResourceDirtyTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdWorkerDependencyIndex Dependencies;
  TestTrue(TEXT("dependency reset"), Dependencies.Reset(2));
  const FCrowdWorkerDependencyKey ResourceKey{
    ECrowdWorkerDependencyKind::Resource, {}, 77};
  TestEqual(
    TEXT("edge add"),
    Dependencies.AddDependency(
      ResourceKey,
      MakeEntityWork(ECrowdWorkerDomainId::Movement, 9, 2)),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("edge dedup"),
    Dependencies.AddDependency(
      ResourceKey,
      MakeEntityWork(ECrowdWorkerDomainId::Movement, 9, 4)),
    ECrowdWorkerQueueResult::MergedDuplicate);
  TArray<FCrowdWorkerWorkItem> Dependents;
  TestEqual(
    TEXT("collect one"),
    Dependencies.CollectDependents(ResourceKey, Dependents),
    1);
  TestEqual(
    TEXT("reason merged"),
    Dependents[0].ReasonMask,
    uint64{6});

  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("resource reset"), Resources.Reset(64));
  TestEqual(
    TEXT("stage revision"),
    Resources.StageBuilding({77, 1, MakePayload(100)}),
    ECrowdWorkerQueueResult::Added);
  TestNull(
    TEXT("building not visible"),
    Resources.FindCurrent(77));
  TArray<FCrowdWorkerResourceRevisionEvent> Events;
  TestTrue(
    TEXT("commit epoch"),
    Resources.CommitBuildingAtEpoch(3, Events));
  TestEqual(TEXT("one revision event"), Events.Num(), 1);
  TestEqual(
    TEXT("current revision"),
    Resources.FindCurrent(77)->Revision,
    uint64{1});
  TestEqual(
    TEXT("stale rejected"),
    Resources.StageBuilding({77, 0, MakePayload(99)}),
    ECrowdWorkerQueueResult::RejectedInvalid);

  FCrowdWorkerDirtyStateStore Dirty;
  TestTrue(TEXT("dirty reset"), Dirty.Reset(1, 64));
  FCrowdWorkerDirtyStateRecord Record;
  Record.EntityRef = {1, 9, 1};
  Record.Field = ECrowdWorkerField::Movement;
  Record.Generation = 1;
  Record.WorkerEpoch = 3;
  Record.StateRevision = 1;
  Record.Payload = MakePayload(10);
  TestEqual(
    TEXT("dirty add"),
    Dirty.MarkDirty(Record),
    ECrowdWorkerQueueResult::Added);
  Record.StateRevision = 2;
  Record.Payload = MakePayload(11);
  TestEqual(
    TEXT("dirty latest wins"),
    Dirty.MarkDirty(Record),
    ECrowdWorkerQueueResult::Replaced);
  FCrowdWorkerDirtyStateRecord Other = Record;
  Other.EntityRef = {1, 10, 1};
  TestEqual(
    TEXT("dirty entity capacity"),
    Dirty.MarkDirty(Other),
    ECrowdWorkerQueueResult::RejectedCapacity);
  TArray<FCrowdWorkerDirtyStateRecord> DirtyRecords;
  TestEqual(TEXT("drain one"), Dirty.Drain(DirtyRecords), 1);
  TestEqual(
    TEXT("latest revision drained"),
    DirtyRecords[0].StateRevision,
    uint64{2});

  FCrowdWorkerOrderedEventStore OrderedEvents;
  TestTrue(
    TEXT("ordered event store reset"),
    OrderedEvents.Reset(2, 64, 1));
  FCrowdWorkerGameplayEvent Event;
  Event.EntityRef = {1, 9, 1};
  Event.Generation = 1;
  Event.WorkerEpoch = 3;
  Event.EventSequence = 2;
  Event.EventId = 7002;
  Event.Payload = MakePayload(22);
  Event.RecalculateStableHash();
  TestEqual(
    TEXT("ordered event gap rejected"),
    OrderedEvents.Append(Event),
    ECrowdWorkerQueueResult::Conflict);
  Event.EventSequence = 1;
  Event.EventId = 7001;
  Event.RecalculateStableHash();
  TestEqual(
    TEXT("first ordered event"),
    OrderedEvents.Append(Event),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("ordered duplicate cannot overwrite"),
    OrderedEvents.Append(Event),
    ECrowdWorkerQueueResult::RejectedStale);
  Event.EventSequence = 2;
  Event.EventId = 7002;
  Event.RecalculateStableHash();
  TestEqual(
    TEXT("second ordered event"),
    OrderedEvents.Append(Event),
    ECrowdWorkerQueueResult::Added);
  Event.EventSequence = 3;
  Event.EventId = 7003;
  Event.RecalculateStableHash();
  TestEqual(
    TEXT("ordered capacity is fail closed"),
    OrderedEvents.Append(Event),
    ECrowdWorkerQueueResult::RejectedCapacity);
  TArray<FCrowdWorkerGameplayEvent> DrainedEvents;
  TestEqual(
    TEXT("ordered events drain without overwrite"),
    OrderedEvents.Drain(DrainedEvents),
    2);
  TestEqual(
    TEXT("event sequence remains monotonic after drain"),
    OrderedEvents.Append(Event),
    ECrowdWorkerQueueResult::Added);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerDomainRegistryCheckpointTest,
  "MassCrowd.RuntimeV2.DomainDagCheckpoint",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerDomainRegistryCheckpointTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  TArray<ECrowdWorkerDomainId> Order;
  FCrowdWorkerDomainRegistry Registry;
  TestTrue(
    TEXT("register movement first"),
    Registry.Register(MakeUnique<FRecordingDomain>(
      ECrowdWorkerDomainId::Movement,
      TArray<ECrowdWorkerDomainId>{
        ECrowdWorkerDomainId::Behavior},
      Order)));
  TestTrue(
    TEXT("register behavior second"),
    Registry.Register(MakeUnique<FRecordingDomain>(
      ECrowdWorkerDomainId::Behavior,
      TArray<ECrowdWorkerDomainId>{},
      Order)));
  TestTrue(TEXT("freeze sorts dag"), Registry.Freeze());
  TArray<FCrowdWorkerWorkItem> Work{
    MakeEntityWork(ECrowdWorkerDomainId::Movement, 2),
    MakeEntityWork(ECrowdWorkerDomainId::Behavior, 1)};
  FCrowdWorkerDomainContext Context;
  Context.Generation = 7;
  Context.WorkerEpoch = 3;
  Context.AbsoluteSimulationTick = 10;
  FCrowdWorkerDomainOutput Output;
  TestTrue(
    TEXT("execute frozen dag"),
    Registry.ExecuteEpoch(Context, Work, Output));
  TestEqual(TEXT("two domains"), Order.Num(), 2);
  TestEqual(
    TEXT("behavior first"),
    Order[0],
    ECrowdWorkerDomainId::Behavior);
  TestEqual(
    TEXT("movement second"),
    Order[1],
    ECrowdWorkerDomainId::Movement);

  FCrowdWorkerCheckpoint Checkpoint;
  Checkpoint.Generation = 7;
  Checkpoint.WorkerEpoch = 3;
  Checkpoint.AbsoluteSimulationTick = 10;
  Checkpoint.FixedSimulationQuantumSeconds = 1.0 / 30.0;
  Checkpoint.LastAppliedInputSequence = 20;
  Checkpoint.LastOrderedEventSequence = 8;
  Checkpoint.EntityStateHash = 100;
  Checkpoint.ResourceRevisionHash = 200;
  Checkpoint.RecalculateStableHash();
  TestTrue(TEXT("checkpoint valid"), Checkpoint.IsValid());
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerNetworkCheckpointIntentTest,
  "MassCrowd.RuntimeV2.NetworkCheckpointIntentLateJoin",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerNetworkCheckpointIntentTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 41;
  const FCrowdStableEntityRef EntityRef{1, 101, 1};
  FCrowdWorkerNetworkStateConfig Config;
  Config.MaxRetainedIntentBatches = 2;
  Config.MaxStateRecordsPerCheckpoint = 16;
  Config.MaxResourceRecordsPerCheckpoint = 4;

  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("network state store resets"),
    States.Reset(8, 1024));
  TestEqual(TEXT("network entity spawns"),
    States.Spawn(
      EntityRef, Generation, 1, MakePayload(1001)),
    ECrowdWorkerQueueResult::Added);
  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("network resource store resets"),
    Resources.Reset(1024));
  TestEqual(TEXT("network resource stages"),
    Resources.StageBuilding({77, 1, MakePayload(2001)}),
    ECrowdWorkerQueueResult::Added);
  TArray<FCrowdWorkerResourceRevisionEvent> ResourceEvents;
  TestTrue(TEXT("network resource commits"),
    Resources.CommitBuildingAtEpoch(1, ResourceEvents));

  TArray<FCrowdWorkerDirtyStateRecord> CompleteStates;
  States.GetStateRecords(CompleteStates);
  TArray<FCrowdWorkerResourceRecord> CompleteResources;
  Resources.GetCurrentRecords(CompleteResources);
  FCrowdWorkerGameplayEvent Event;
  Event.EntityRef = EntityRef;
  Event.Generation = Generation;
  Event.WorkerEpoch = 1;
  Event.SourceInputSequence = 1;
  Event.EventSequence = 1;
  Event.EventId = 9001;
  Event.Payload = MakePayload(3001);
  Event.RecalculateStableHash();
  TArray<FCrowdWorkerGameplayEvent> Events{Event};

  auto MakeCheckpoint = [&States, &Resources](
    const uint64 Epoch,
    const uint64 Tick,
    const uint64 LastEventSequence,
    const uint64 LastAppliedInputSequence = 1)
  {
    FCrowdWorkerCheckpoint Checkpoint;
    Checkpoint.Generation = Generation;
    Checkpoint.WorkerEpoch = Epoch;
    Checkpoint.AbsoluteSimulationTick = Tick;
    Checkpoint.FixedSimulationQuantumSeconds = 1.0 / 30.0;
    Checkpoint.LastAppliedInputSequence =
      LastAppliedInputSequence;
    Checkpoint.LastOrderedEventSequence = LastEventSequence;
    Checkpoint.EntityStateHash = States.CalculateStableHash();
    Checkpoint.ResourceRevisionHash =
      Resources.CalculateCurrentStableHash();
    Checkpoint.RecalculateStableHash();
    return Checkpoint;
  };
  auto MakeContinuation = [EntityRef](
    const uint64 CompletedEpoch,
    const bool bPopulate = false)
  {
    FCrowdWorkerNetworkContinuationState Continuation;
    Continuation.WorkRing.Epoch = CompletedEpoch + 1;
    Continuation.LifecycleWatermarks.Add({
      EntityRef.ProviderId,
      EntityRef.StableEntityId,
      EntityRef.LifecycleSerial});
    if (!bPopulate) return Continuation;
    FCrowdWorkerWorkItem Work = MakeEntityWork(
      ECrowdWorkerDomainId::LifecycleInput,
      EntityRef.StableEntityId,
      1);
    Work.Key.PrimaryEntity = EntityRef;
    Work.EnqueueEpoch = CompletedEpoch + 1;
    Continuation.WorkRing.CurrentItems.Add(Work);
    FCrowdWorkerWakeup Wakeup;
    Wakeup.Key.Domain = ECrowdWorkerDomainId::LifecycleInput;
    Wakeup.Key.EntityRef = EntityRef;
    Wakeup.Key.WakeupId = 7001;
    Wakeup.AbsoluteSimulationTick = CompletedEpoch + 2;
    Wakeup.Revision = 1;
    Wakeup.ReasonMask = 1;
    Continuation.Wakeups.Add(Wakeup);
    FCrowdWorkerDependencyRecord Dependency;
    Dependency.Source.Kind = ECrowdWorkerDependencyKind::Resource;
    Dependency.Source.ScopeKey = 77;
    Dependency.Dependent = Work;
    Continuation.Dependencies.Add(Dependency);
    FCrowdWorkerCommandRecord Command;
    Command.InputSequence = CompletedEpoch;
    Command.EntityRef = EntityRef;
    Command.CommandId = 7002;
    Command.EffectiveSimulationTimeSeconds = 100.0;
    Command.Payload = MakePayload(7003);
    Continuation.Commands.Add(Command);
    return Continuation;
  };

  FCrowdWorkerNetworkStatePublisher Publisher;
  TestTrue(TEXT("network publisher resets"),
    Publisher.Reset(Config, Generation));
  TestTrue(TEXT("first network epoch commits"),
    Publisher.CommitEpoch(
      MakeCheckpoint(1, 1, 1),
      CompleteStates,
      CompleteResources,
      MakeContinuation(1)));
  FCrowdWorkerNetworkCheckpoint Checkpoint;
  TestEqual(TEXT("late join checkpoint is ready"),
    Publisher.ReadCheckpoint(Generation, Checkpoint),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("checkpoint event baseline"),
    Checkpoint.EventBaselineSequence, uint64{1});
  TestEqual(TEXT("checkpoint input baseline"),
    Checkpoint.InputBaselineSequence, uint64{1});

  TestTrue(TEXT("second network epoch commits"),
    Publisher.CommitEpoch(
      MakeCheckpoint(2, 2, 1),
      CompleteStates, CompleteResources,
      MakeContinuation(2)));
  TestTrue(TEXT("third network epoch commits"),
    Publisher.CommitEpoch(
      MakeCheckpoint(3, 3, 1),
      CompleteStates, CompleteResources,
      MakeContinuation(3)));
  TestEqual(TEXT("latest checkpoint remains readable"),
    Publisher.ReadCheckpoint(Generation, Checkpoint),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("latest checkpoint input baseline advances"),
    Checkpoint.InputBaselineSequence, uint64{1});
  TestEqual(TEXT("wrong generation rejected"),
    Publisher.ReadCheckpoint(Generation + 1, Checkpoint),
    ECrowdWorkerNetworkReadResult::RejectedGeneration);

  FCrowdWorkerDirtyStateRecord Correction;
  Correction.EntityRef = EntityRef;
  Correction.Field = ECrowdWorkerField::Movement;
  Correction.Generation = Generation;
  Correction.WorkerEpoch = 4;
  Correction.StateRevision = 4;
  Correction.CorrectionRevision = 2;
  Correction.SourceInputSequence = 4;
  Correction.Payload = MakePayload(4001);
  TestEqual(TEXT("correction reaches worker state"),
    States.ApplyDirty(Correction),
    ECrowdWorkerQueueResult::Replaced);
  States.GetStateRecords(CompleteStates);
  TestTrue(TEXT("correction epoch commits"),
    Publisher.CommitEpoch(
      MakeCheckpoint(4, 4, 1, 4),
      CompleteStates,
      CompleteResources,
      MakeContinuation(4, true)));
  TestEqual(TEXT("corrected checkpoint is ready"),
    Publisher.ReadCheckpoint(Generation, Checkpoint),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("correction revision is frozen"),
    Checkpoint.MaxCorrectionRevision, uint64{2});

  FCrowdAsyncSimulationRuntime RestoredRuntime;
  TestTrue(TEXT("restored runtime domain registers"),
    RestoredRuntime.RegisterDomainExecutor(
      MakeUnique<FSyntheticLifecycleDomain>()));
  TestTrue(TEXT("restored runtime starts"),
    RestoredRuntime.Start(MakeSyntheticConfig(), Generation));
  TestEqual(TEXT("checkpoint restores into starting runtime"),
    RestoredRuntime.RestoreNetworkCheckpoint(Checkpoint),
    ECrowdAsyncSimulationRestoreResult::Restored);
  TestEqual(TEXT("restored runtime is running"),
    RestoredRuntime.GetState(),
    ECrowdAsyncSimulationRuntimeState::Running);

  FCrowdWorkerMirrorSnapshot RestoredMirror;
  TestTrue(TEXT("restored mirror is readable"),
    RestoredRuntime.ReadMirrorSnapshot(RestoredMirror));
  TestEqual(TEXT("restored mirror epoch"),
    RestoredMirror.WorkerEpoch, uint64{4});
  TestEqual(TEXT("restored mirror input sequence"),
    RestoredMirror.LastAppliedInputSequence, uint64{4});
  TestEqual(TEXT("restored mirror entity count"),
    RestoredMirror.EntityRefs.Num(), 1);
  TestEqual(TEXT("restored mirror correction revision"),
    RestoredMirror.CorrectionRevisions[0], uint64{2});

  FCrowdWorkerNetworkCheckpoint RestoredCheckpoint;
  TestEqual(TEXT("restored checkpoint is readable"),
    RestoredRuntime.ReadNetworkCheckpoint(
      Generation, RestoredCheckpoint),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("restored checkpoint hash is exact"),
    RestoredCheckpoint.StableHash, Checkpoint.StableHash);

  FCrowdWorkerIntentBatch NextInput;
  NextInput.Generation = Generation;
  NextInput.FirstInputSequence = 5;
  NextInput.LastInputSequence = 6;
  NextInput.TargetSimulationTimeSeconds = 5.0 / 30.0;
  FCrowdWorkerExternalGameplayInput NextState;
  NextState.InputSequence = 5;
  NextState.EntityRef = EntityRef;
  NextState.DirtyMask = 1;
  NextState.FullState = MakePayload(5001);
  NextInput.ExternalGameplayInputs.Add(NextState);
  NextInput.Clock.InputSequence = 6;
  NextInput.Clock.SimulationTick = 5;
  NextInput.RecalculateStableHash();
  TestEqual(TEXT("restored sequence gate accepts next input"),
    RestoredRuntime.SubmitIntentBatch(NextInput),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  const double RestoreDeadline =
    FPlatformTime::Seconds() + 5.0;
  bool bRestoredRuntimeIdle = false;
  while (FPlatformTime::Seconds() < RestoreDeadline)
  {
    const ECrowdAsyncSimulationPollResult Result =
      RestoredRuntime.Poll();
    if (Result == ECrowdAsyncSimulationPollResult::Failed)
      break;
    if (Result == ECrowdAsyncSimulationPollResult::Idle
      && RestoredRuntime.GetMetrics().LastAppliedInputSequence
        == 6)
    {
      bRestoredRuntimeIdle = true;
      break;
    }
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestTrue(TEXT("restored runtime applies next input"),
    bRestoredRuntimeIdle);
  TestEqual(TEXT("post-restore checkpoint is readable"),
    RestoredRuntime.ReadNetworkCheckpoint(
      Generation, RestoredCheckpoint),
    ECrowdWorkerNetworkReadResult::Ready);
  TArray<FCrowdWorkerIntentBatch> RuntimeIntents;
  TestEqual(TEXT("post-restore shared intent is readable"),
    RestoredRuntime.ReadNetworkIntents(
      Generation,
      Checkpoint.Header.LastAppliedInputSequence,
      RuntimeIntents),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("one post-restore shared intent retained"),
    RuntimeIntents.Num(), 1);
  if (RuntimeIntents.Num() == 1)
  {
    FCrowdAsyncSimulationRuntime ReplicaRuntime;
    TestTrue(TEXT("replica runtime domain registers"),
      ReplicaRuntime.RegisterDomainExecutor(
        MakeUnique<FSyntheticLifecycleDomain>()));
    TestTrue(TEXT("replica runtime starts"),
      ReplicaRuntime.Start(MakeSyntheticConfig(), Generation));
    TestEqual(TEXT("replica restores the same baseline"),
      ReplicaRuntime.RestoreNetworkCheckpoint(Checkpoint),
      ECrowdAsyncSimulationRestoreResult::Restored);
    TestEqual(TEXT("replica queues the same intent contract"),
      ReplicaRuntime.SubmitIntentBatch(RuntimeIntents[0]),
      ECrowdAsyncSimulationSubmitResult::Accepted);
    const double ReplicaDeadline =
      FPlatformTime::Seconds() + 5.0;
    bool bReplicaApplied = false;
    while (FPlatformTime::Seconds() < ReplicaDeadline)
    {
      const ECrowdAsyncSimulationPollResult Result =
        ReplicaRuntime.Poll();
      if (Result == ECrowdAsyncSimulationPollResult::Failed)
        break;
      if (ReplicaRuntime.GetMetrics().LastAppliedInputSequence
        == RuntimeIntents[0].LastInputSequence)
      {
        bReplicaApplied = true;
        break;
      }
      FPlatformProcess::SleepNoStats(0.0f);
    }
    TestTrue(TEXT("replica applies the shared intent"),
      bReplicaApplied);
    FCrowdWorkerNetworkCheckpoint ReplicaCheckpoint;
    TestEqual(TEXT("replica checkpoint is readable"),
      ReplicaRuntime.ReadNetworkCheckpoint(
        Generation, ReplicaCheckpoint),
      ECrowdWorkerNetworkReadResult::Ready);
    TestEqual(TEXT("replica entity state hash is exact"),
      ReplicaCheckpoint.Header.EntityStateHash,
      RestoredCheckpoint.Header.EntityStateHash);
    TestEqual(TEXT("replica checkpoint hash is exact"),
      ReplicaCheckpoint.StableHash,
      RestoredCheckpoint.StableHash);
    TestTrue(TEXT("replica runtime stops"),
      ReplicaRuntime.StopAndDrain(5.0));
  }
  TestEqual(TEXT("pending wakeup survives restore"),
    RestoredCheckpoint.Continuation.Wakeups.Num(), 1);
  TestEqual(TEXT("dependency edge survives restore"),
    RestoredCheckpoint.Continuation.Dependencies.Num(), 1);
  TestEqual(TEXT("pending command survives restore"),
    RestoredCheckpoint.Continuation.Commands.Num(), 1);
  TestTrue(TEXT("restored runtime stops"),
    RestoredRuntime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerNetworkPublishCadenceTest,
  "MassCrowd.RuntimeV2.NetworkPublishCadenceResourceCoalescing",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerNetworkPublishCadenceTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 42;
  FCrowdAsyncSimulationRuntimeConfig Config = MakeSyntheticConfig();
  Config.NetworkPublishIntervalEpochs = 3;
  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(TEXT("cadence domain registers"),
    Runtime.RegisterDomainExecutor(
      MakeUnique<FSyntheticLifecycleDomain>()));
  TestTrue(TEXT("cadence runtime starts"),
    Runtime.Start(Config, Generation));

  auto PollUntilInput = [&Runtime](const uint64 ExpectedSequence)
  {
    const double Deadline = FPlatformTime::Seconds() + 5.0;
    while (FPlatformTime::Seconds() < Deadline)
    {
      const ECrowdAsyncSimulationPollResult Result = Runtime.Poll();
      if (Result == ECrowdAsyncSimulationPollResult::Failed)
        return false;
      if (Result == ECrowdAsyncSimulationPollResult::Idle
        && Runtime.GetMetrics().LastAppliedInputSequence
          == ExpectedSequence)
        return true;
      FPlatformProcess::SleepNoStats(0.0f);
    }
    return false;
  };
  auto MakeResourceInput = [](const uint64 InputSequence,
    const uint64 Revision, const uint32 Value,
    const double TargetTime)
  {
    FCrowdWorkerIntentBatch Batch;
    Batch.Generation = Generation;
    Batch.FirstInputSequence = InputSequence;
    Batch.LastInputSequence = InputSequence + 1;
    Batch.TargetSimulationTimeSeconds = TargetTime;
    FCrowdWorkerResourceDelta Resource;
    Resource.InputSequence = InputSequence;
    Resource.ResourceId = 77;
    Resource.Revision = Revision;
    Resource.Payload = MakePayload(Value, 91004);
    Batch.ResourceDeltas.Add(MoveTemp(Resource));
    Batch.Clock.InputSequence = InputSequence + 1;
    Batch.Clock.SimulationTick = FMath::Max<uint64>(
      1,
      static_cast<uint64>(FMath::RoundToInt64(TargetTime * 30.0)));
    Batch.RecalculateStableHash();
    return Batch;
  };

  const FCrowdWorkerIntentBatch Snapshot =
    MakeSyntheticSnapshot(Generation);
  TestEqual(TEXT("cadence snapshot queues"),
    Runtime.SubmitResnapshot(Snapshot),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  const bool bFirstEpochComplete = PollUntilInput(3);
  TestTrue(TEXT("cadence first epoch completes"), bFirstEpochComplete);
  FCrowdWorkerNetworkCheckpoint FirstCheckpoint;
  const ECrowdWorkerNetworkReadResult FirstRead =
    Runtime.ReadNetworkCheckpoint(Generation, FirstCheckpoint);
  TestEqual(TEXT("cadence first checkpoint reads"), FirstRead,
    ECrowdWorkerNetworkReadResult::Ready);
  if (!bFirstEpochComplete
    || FirstRead != ECrowdWorkerNetworkReadResult::Ready
    || FirstCheckpoint.ResourceRecords.Num() != 1)
  {
    AddError(TEXT("cadence first checkpoint fixture is incomplete"));
    Runtime.StopAndDrain(5.0);
    return true;
  }
  TestEqual(TEXT("first resource revision"),
    FirstCheckpoint.ResourceRecords[0].Revision, uint64{1});

  const FCrowdWorkerIntentBatch RevisionTwo = MakeResourceInput(
    4, 2, 200, 2.0 / 30.0);
  TestEqual(TEXT("second resource queues"),
    Runtime.SubmitIntentBatch(RevisionTwo),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  const bool bSecondEpochComplete = PollUntilInput(5);
  TestTrue(TEXT("cadence second epoch completes"), bSecondEpochComplete);
  FCrowdWorkerNetworkCheckpoint DeferredCheckpoint;
  TestEqual(TEXT("deferred checkpoint remains readable"),
    Runtime.ReadNetworkCheckpoint(Generation, DeferredCheckpoint),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("second epoch does not publish"),
    DeferredCheckpoint.StableHash, FirstCheckpoint.StableHash);

  const FCrowdWorkerIntentBatch RevisionThree = MakeResourceInput(
    6, 3, 300, 3.0 / 30.0);
  TestEqual(TEXT("third resource queues"),
    Runtime.SubmitIntentBatch(RevisionThree),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  const bool bThirdEpochComplete = PollUntilInput(7);
  TestTrue(TEXT("cadence third epoch completes"), bThirdEpochComplete);
  FCrowdWorkerNetworkCheckpoint FinalCheckpoint;
  TestEqual(TEXT("cadence final checkpoint reads"),
    Runtime.ReadNetworkCheckpoint(Generation, FinalCheckpoint),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("final checkpoint has one resource"),
    FinalCheckpoint.ResourceRecords.Num(), 1);
  if (FinalCheckpoint.ResourceRecords.Num() == 1)
    TestEqual(TEXT("latest resource revision wins"),
      FinalCheckpoint.ResourceRecords[0].Revision, uint64{3});
  TArray<FCrowdWorkerIntentBatch> Intents;
  TestEqual(TEXT("resource revisions remain in intent journal"),
    Runtime.ReadNetworkIntents(
      Generation,
      FirstCheckpoint.Header.LastAppliedInputSequence,
      Intents),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("two resource intents retained"), Intents.Num(), 2);
  if (Intents.Num() == 2)
  {
    TestEqual(TEXT("latest intent carries resource revision"),
      Intents[1].ResourceDeltas.Num(), 1);
    if (Intents[1].ResourceDeltas.Num() == 1)
      TestEqual(TEXT("latest intent resource revision"),
        Intents[1].ResourceDeltas[0].Revision, uint64{3});
  }
  TestTrue(TEXT("cadence runtime stops"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerShardCompletionOrderTest,
  "MassCrowd.RuntimeV2.ShardCompletionOrderStableMerge",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerShardCompletionOrderTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  TArray<FCrowdWorkerWorkItem> Work;
  for (uint64 EntityId = 5; EntityId >= 1; --EntityId)
  {
    FCrowdWorkerWorkItem Item = MakeEntityWork(
      ECrowdWorkerDomainId::Movement, EntityId);
    Item.CorrectionRevision = EntityId & 1;
    Work.Add(MoveTemp(Item));
  }
  TArray<FCrowdWorkerDomainShard> Shards;
  TestTrue(
    TEXT("build stable shards"),
    FCrowdWorkerDeterministicShardPlanner::Build(
      Work, 2, Shards));
  TestEqual(TEXT("three shards"), Shards.Num(), 3);
  TestEqual(
    TEXT("first shard begins stable entity one"),
    Shards[0].WorkItems[0].Key.PrimaryEntity.StableEntityId,
    uint64{1});

  TArray<FCrowdWorkerDomainShardResult> Forward;
  for (const FCrowdWorkerDomainShard& Shard : Shards)
    Forward.Add(MakeShardResult(Shard));
  TArray<FCrowdWorkerDomainShardResult> Reverse = Forward;
  Algo::Reverse(Reverse);
  TArray<FCrowdWorkerDomainShardResult> Scrambled;
  Scrambled.Add(Forward[1]);
  Scrambled.Add(Forward[2]);
  Scrambled.Add(Forward[0]);

  FCrowdWorkerDomainOutput ForwardMerged;
  FCrowdWorkerDomainOutput ReverseMerged;
  FCrowdWorkerDomainOutput ScrambledMerged;
  TestTrue(
    TEXT("forward merge"),
    FCrowdWorkerDeterministicShardPlanner::Merge(
      Forward, 8, 64, 8, ForwardMerged));
  TestTrue(
    TEXT("reverse merge"),
    FCrowdWorkerDeterministicShardPlanner::Merge(
      Reverse, 8, 64, 8, ReverseMerged));
  TestTrue(
    TEXT("scrambled merge"),
    FCrowdWorkerDeterministicShardPlanner::Merge(
      Scrambled, 8, 64, 8, ScrambledMerged));
  const uint64 ForwardHash =
    FCrowdWorkerDeterministicShardPlanner::CalculateStableHash(
      ForwardMerged);
  TestEqual(
    TEXT("reverse completion hash"),
    FCrowdWorkerDeterministicShardPlanner::CalculateStableHash(
      ReverseMerged),
    ForwardHash);
  TestEqual(
    TEXT("scrambled completion hash"),
    FCrowdWorkerDeterministicShardPlanner::CalculateStableHash(
      ScrambledMerged),
    ForwardHash);
  TestEqual(
    TEXT("five ordered events"),
    ForwardMerged.OrderedEvents.Num(),
    5);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerCorrectionInvalidationAuditTest,
  "MassCrowd.RuntimeV2.CorrectionInvalidationDependencyAudit",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerCorrectionInvalidationAuditTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  const FCrowdStableEntityRef EntityRef{1, 20, 1};
  FCrowdWorkerWorkRing Ring;
  TestTrue(TEXT("work reset"), Ring.Reset(8, 3));
  FCrowdWorkerWorkItem Stale = MakeEntityWork(
    ECrowdWorkerDomainId::Movement, 20);
  Stale.CorrectionRevision = 1;
  FCrowdWorkerWorkItem Current = MakeEntityWork(
    ECrowdWorkerDomainId::Behavior, 20);
  Current.CorrectionRevision = 3;
  TestEqual(
    TEXT("stale work added"),
    Ring.EnqueueCurrent(Stale),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("current work added"),
    Ring.EnqueueNext(Current),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("old work invalidated"),
    Ring.InvalidateEntityRevision(EntityRef, 3),
    1);
  TestEqual(
    TEXT("current revision survives"),
    Ring.GetStats().NextDepth,
    1);

  FCrowdWorkerTimeWheel Wheel;
  TestTrue(TEXT("wheel reset"), Wheel.Reset(8));
  FCrowdWorkerWakeup OldWakeup;
  OldWakeup.Key = {
    ECrowdWorkerDomainId::Movement, EntityRef, 1};
  OldWakeup.AbsoluteSimulationTick = 10;
  OldWakeup.Revision = 1;
  FCrowdWorkerWakeup NewWakeup = OldWakeup;
  NewWakeup.Key.WakeupId = 2;
  NewWakeup.Revision = 3;
  TestEqual(
    TEXT("old wakeup"),
    Wheel.Schedule(OldWakeup),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("new wakeup"),
    Wheel.Schedule(NewWakeup),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("old wakeup invalidated"),
    Wheel.InvalidateEntityRevision(EntityRef, 3),
    1);
  TestEqual(TEXT("new wakeup survives"), Wheel.Num(), 1);

  FCrowdWorkerDirtyStateStore Dirty;
  TestTrue(TEXT("dirty reset"), Dirty.Reset(4, 64));
  FCrowdWorkerDirtyStateRecord DirtyRecord;
  DirtyRecord.EntityRef = EntityRef;
  DirtyRecord.Field = ECrowdWorkerField::Movement;
  DirtyRecord.Generation = 7;
  DirtyRecord.WorkerEpoch = 3;
  DirtyRecord.StateRevision = 4;
  DirtyRecord.CorrectionRevision = 1;
  DirtyRecord.Payload = MakePayload(20);
  TestEqual(
    TEXT("old dirty added"),
    Dirty.MarkDirty(DirtyRecord),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("old dirty invalidated"),
    Dirty.InvalidateEntityRevision(EntityRef, 3),
    1);
  TestEqual(TEXT("no dirty entity"), Dirty.NumEntities(), 0);

  FCrowdWorkerDependencyIndex Dependencies;
  TestTrue(TEXT("dependency reset"), Dependencies.Reset(4));
  const FCrowdWorkerDependencyKey Source{
    ECrowdWorkerDependencyKind::Resource, {}, 77};
  TestEqual(
    TEXT("dependency declared"),
    Dependencies.AddDependency(Source, Current),
    ECrowdWorkerQueueResult::Added);
  TestTrue(
    TEXT("declared read is covered"),
    Dependencies.ContainsDependency(Source, Current.Key));
  FCrowdWorkerWorkItem Undeclared = Current;
  Undeclared.Key.PrimaryEntity = {1, 21, 1};
  TestFalse(
    TEXT("undeclared read is detected"),
    Dependencies.ContainsDependency(Source, Undeclared.Key));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2SyntheticIntegrationTest,
  "MassCrowd.RuntimeV2.AsyncRuntimeSyntheticShadow",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2SyntheticIntegrationTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  const FCrowdAsyncSimulationRuntimeConfig Config =
    MakeSyntheticConfig();

  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(
    TEXT("domain registers before start"),
    Runtime.RegisterDomainExecutor(
      MakeUnique<FSyntheticLifecycleDomain>()));
  TestTrue(TEXT("runtime starts"), Runtime.Start(Config, 7));

  TestEqual(
    TEXT("snapshot accepted"),
    Runtime.SubmitResnapshot(MakeSyntheticSnapshot(7)),
    ECrowdAsyncSimulationSubmitResult::Accepted);

  const double Deadline = FPlatformTime::Seconds() + 5.0;
  bool bIdle = false;
  while (FPlatformTime::Seconds() < Deadline)
  {
    const ECrowdAsyncSimulationPollResult Result = Runtime.Poll();
    const FCrowdAsyncSimulationRuntimeMetrics Metrics =
      Runtime.GetMetrics();
    if (Result == ECrowdAsyncSimulationPollResult::Failed)
      break;
    if (Result == ECrowdAsyncSimulationPollResult::Idle
      && Metrics.InputQueueDepth == 0
      && Metrics.SimulationTimeSeconds
        + UE_DOUBLE_SMALL_NUMBER
        >= Metrics.TargetSimulationTimeSeconds)
    {
      bIdle = true;
      break;
    }
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestTrue(TEXT("synthetic runtime reaches idle"), bIdle);
  const FCrowdAsyncSimulationRuntimeMetrics Metrics =
    Runtime.GetMetrics();
  TestTrue(
    TEXT("work watermark records lifecycle and resource"),
    Metrics.WorkerV2.WorkHighWatermark >= 2);
  TestEqual(
    TEXT("one propagation round"),
    Metrics.WorkerV2.PropagationRoundCount,
    uint64{1});
  TestEqual(
    TEXT("two synthetic domain shards dispatched"),
    Metrics.WorkerV2.ShardDispatchCount,
    uint64{2});
  TestEqual(
    TEXT("two synthetic domain shards completed"),
    Metrics.WorkerV2.ShardCompletionCount,
    uint64{2});
  TestEqual(
    TEXT("one merge barrier per synthetic domain"),
    Metrics.WorkerV2.ShardMergeCount,
    uint64{2});
  TestEqual(
    TEXT("no synthetic shard remains in flight"),
    Metrics.WorkerV2.ShardInFlightCount,
    0);
  TestEqual(
    TEXT("domain barrier limits this fixture to one in-flight shard"),
    Metrics.WorkerV2.ShardInFlightHighWatermark,
    1);
  TestEqual(
    TEXT("dirty entity shadow audited"),
    Metrics.WorkerV2.DirtyHighWatermark,
    1);
  TestEqual(
    TEXT("no propagation limit"),
    Metrics.WorkerV2.PropagationLimitHitCount,
    uint64{0});
  TestEqual(
    TEXT("no ordered event loss"),
    Metrics.WorkerV2.OrderedEventLossCount,
    uint64{0});
  TestTrue(
    TEXT("resource revision hash published"),
    Metrics.WorkerV2.ResourceRevisionHash != 0);
  TestTrue(TEXT("runtime stops"), Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2InFlightLifecycleTest,
  "MassCrowd.RuntimeV2.InFlightInvalidateAndTeardown",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2InFlightLifecycleTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  const FCrowdAsyncSimulationRuntimeConfig Config =
    MakeSyntheticConfig();
  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(
    TEXT("slow domain registers"),
    Runtime.RegisterDomainExecutor(
      MakeUnique<FSlowLifecycleDomain>()));
  TestTrue(TEXT("runtime starts"), Runtime.Start(Config, 9));
  TestEqual(
    TEXT("snapshot accepted"),
    Runtime.SubmitResnapshot(MakeSyntheticSnapshot(9)),
    ECrowdAsyncSimulationSubmitResult::Accepted);

  const double DispatchDeadline =
    FPlatformTime::Seconds() + 5.0;
  bool bShardDispatched = false;
  while (FPlatformTime::Seconds() < DispatchDeadline)
  {
    Runtime.Poll();
    const FCrowdAsyncSimulationRuntimeMetrics Metrics =
      Runtime.GetMetrics();
    if (Metrics.WorkerV2.ShardInFlightHighWatermark > 0)
    {
      bShardDispatched = true;
      break;
    }
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestTrue(
    TEXT("short shard host dispatched without owner wait"),
    bShardDispatched);
  TestTrue(TEXT("in-flight generation invalidates"),
    Runtime.Invalidate(10));

  const double InvalidateDeadline =
    FPlatformTime::Seconds() + 5.0;
  bool bInvalidated = false;
  while (FPlatformTime::Seconds() < InvalidateDeadline)
  {
    const ECrowdAsyncSimulationPollResult Result =
      Runtime.Poll();
    if (Result == ECrowdAsyncSimulationPollResult::StateChanged
      && Runtime.GetGeneration() == 10
      && Runtime.RequiresResnapshot())
    {
      bInvalidated = true;
      break;
    }
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestTrue(
    TEXT("in-flight shards drain before generation reset"),
    bInvalidated);
  TestEqual(
    TEXT("new generation snapshot accepted"),
    Runtime.SubmitResnapshot(MakeSyntheticSnapshot(10)),
    ECrowdAsyncSimulationSubmitResult::Accepted);

  bool bSecondShardDispatched = false;
  const double SecondDispatchDeadline =
    FPlatformTime::Seconds() + 5.0;
  while (FPlatformTime::Seconds() < SecondDispatchDeadline)
  {
    Runtime.Poll();
    const FCrowdAsyncSimulationRuntimeMetrics Metrics =
      Runtime.GetMetrics();
    if (Metrics.WorkerV2.ShardInFlightHighWatermark > 0)
    {
      bSecondShardDispatched = true;
      break;
    }
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestTrue(
    TEXT("new generation shard dispatched"),
    bSecondShardDispatched);
  TestTrue(
    TEXT("teardown drains in-flight shard tasks"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2AsyncDomainBarrierTest,
  "MassCrowd.RuntimeV2.AsyncDomainBarrierOrder",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2AsyncDomainBarrierTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  const TSharedPtr<FStageBarrierProbe, ESPMode::ThreadSafe> Probe =
    MakeShared<FStageBarrierProbe, ESPMode::ThreadSafe>();
  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(
    TEXT("lifecycle stage registers"),
    Runtime.RegisterDomainExecutor(
      MakeUnique<FSlowLifecycleDomain>(Probe)));
  TestTrue(
    TEXT("flow stage registers after lifecycle"),
    Runtime.RegisterDomainExecutor(
      MakeUnique<FBarrierFlowDomain>(Probe)));
  TestTrue(
    TEXT("barrier runtime starts"),
    Runtime.Start(MakeSyntheticConfig(), 13));
  TestEqual(
    TEXT("barrier snapshot accepted"),
    Runtime.SubmitResnapshot(MakeSyntheticSnapshot(13)),
    ECrowdAsyncSimulationSubmitResult::Accepted);

  const double Deadline = FPlatformTime::Seconds() + 5.0;
  bool bIdle = false;
  while (FPlatformTime::Seconds() < Deadline)
  {
    const ECrowdAsyncSimulationPollResult Result =
      Runtime.Poll();
    if (Result == ECrowdAsyncSimulationPollResult::Failed)
      break;
    if (Result == ECrowdAsyncSimulationPollResult::Idle)
    {
      bIdle = true;
      break;
    }
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestTrue(TEXT("barrier runtime reaches idle"), bIdle);
  TestTrue(
    TEXT("flow executes only after lifecycle shard barrier"),
    Probe->bFlowObservedLifecycle.Load());
  const FCrowdAsyncSimulationRuntimeMetrics Metrics =
    Runtime.GetMetrics();
  TestEqual(
    TEXT("two domain stages merged independently"),
    Metrics.WorkerV2.ShardMergeCount,
    uint64{2});
  TestTrue(
    TEXT("barrier runtime stops"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2InputEpochFreezeTest,
  "MassCrowd.RuntimeV2.InputWaitsForEpochBarrier",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2InputEpochFreezeTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(
    TEXT("slow lifecycle domain registers"),
    Runtime.RegisterDomainExecutor(
      MakeUnique<FSlowLifecycleDomain>()));
  TestTrue(
    TEXT("input freeze runtime starts"),
    Runtime.Start(MakeSyntheticConfig(), 14));
  TestEqual(
    TEXT("input freeze snapshot accepted"),
    Runtime.SubmitResnapshot(MakeSyntheticSnapshot(14)),
    ECrowdAsyncSimulationSubmitResult::Accepted);

  const double DispatchDeadline =
    FPlatformTime::Seconds() + 5.0;
  bool bShardInFlight = false;
  while (FPlatformTime::Seconds() < DispatchDeadline)
  {
    Runtime.Poll();
    if (Runtime.GetMetrics().WorkerV2.ShardInFlightCount > 0)
    {
      bShardInFlight = true;
      break;
    }
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestTrue(TEXT("first epoch shard is in flight"), bShardInFlight);

  FCrowdWorkerIntentBatch Incremental;
  Incremental.Generation = 14;
  Incremental.FirstInputSequence = 4;
  Incremental.LastInputSequence = 5;
  Incremental.TargetSimulationTimeSeconds = 2.0 / 30.0;
  FCrowdWorkerExternalGameplayInput State;
  State.InputSequence = 4;
  State.EntityRef = {1, 42, 1};
  State.DirtyMask = 1;
  State.FullState = MakePayload(43, 91003);
  Incremental.ExternalGameplayInputs.Add(State);
  Incremental.Clock.InputSequence = 5;
  Incremental.Clock.SimulationTick = 2;
  Incremental.RecalculateStableHash();
  TestEqual(
    TEXT("input is admitted while the epoch is in flight"),
    Runtime.SubmitIntentBatch(Incremental),
    ECrowdAsyncSimulationSubmitResult::Accepted);

  const double IdleDeadline = FPlatformTime::Seconds() + 5.0;
  bool bIdle = false;
  bool bFailed = false;
  while (FPlatformTime::Seconds() < IdleDeadline)
  {
    const ECrowdAsyncSimulationPollResult Result =
      Runtime.Poll();
    bFailed |= Result == ECrowdAsyncSimulationPollResult::Failed;
    if (Result == ECrowdAsyncSimulationPollResult::Idle)
    {
      bIdle = true;
      break;
    }
    FPlatformProcess::SleepNoStats(0.0f);
  }
  const FCrowdAsyncSimulationRuntimeMetrics Metrics =
    Runtime.GetMetrics();
  TestFalse(
    TEXT("admitted input cannot mutate a frozen epoch"),
    bFailed);
  TestTrue(TEXT("runtime reaches idle"), bIdle);
  TestEqual(
    TEXT("queued input applies after the epoch barrier"),
    Metrics.LastAppliedInputSequence,
    uint64{5});
  TestEqual(
    TEXT("no Worker v2 failure is latched"),
    Metrics.WorkerV2.LastFailure,
    ECrowdWorkerRuntimeV2Failure::None);
  TestTrue(
    TEXT("input freeze runtime stops"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2SameBatchLifecycleReuseTest,
  "MassCrowd.RuntimeV2.SameBatchLifecycleReuse",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2SameBatchLifecycleReuseTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdAsyncSimulationRuntime Runtime;
  TArray<ECrowdWorkerDomainId> ExecutionOrder;
  TestTrue(TEXT("reuse lifecycle domain registers"),
    Runtime.RegisterDomainExecutor(
      MakeUnique<FCrowdWorkerLifecycleDomainExecutor>()));
  TestTrue(TEXT("reuse planning probe registers"),
    Runtime.RegisterDomainExecutor(
      MakeUnique<FRecordingDomain>(
        ECrowdWorkerDomainId::MovementPlanning,
        TArray<ECrowdWorkerDomainId>{}, ExecutionOrder)));
  FCrowdAsyncSimulationRuntimeConfig Config =
    MakeSyntheticConfig();
  Config.ContractLimits.MaxPayloadBytes = 512;
  TestTrue(TEXT("reuse runtime starts"),
    Runtime.Start(Config, 17));
  TestEqual(TEXT("reuse snapshot accepted"),
    Runtime.SubmitResnapshot(MakeSyntheticSnapshot(17)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  const double InitialDeadline = FPlatformTime::Seconds() + 5.0;
  while (FPlatformTime::Seconds() < InitialDeadline)
  {
    const ECrowdAsyncSimulationPollResult Result = Runtime.Poll();
    if (Result == ECrowdAsyncSimulationPollResult::Failed
      || (Result == ECrowdAsyncSimulationPollResult::Idle
        && Runtime.GetMetrics().InputQueueDepth == 0))
      break;
    FPlatformProcess::SleepNoStats(0.0f);
  }

  FCrowdWorkerIntentBatch Reuse;
  Reuse.Generation = 17;
  Reuse.FirstInputSequence = 4;
  Reuse.LastInputSequence = 7;
  Reuse.TargetSimulationTimeSeconds = 2.0 / 30.0;
  FCrowdWorkerDespawnDelta Despawn;
  Despawn.InputSequence = 4;
  Despawn.EntityRef = {1, 42, 1};
  Despawn.ReasonId = 1;
  Reuse.Despawns.Add(Despawn);
  FCrowdWorkerSpawnDelta Spawn;
  Spawn.InputSequence = 5;
  Spawn.EntityRef = {1, 42, 2};
  Spawn.InitialState = MakePayload(43, 91003);
  Reuse.Spawns.Add(Spawn);
  FCrowdWorkerMovementControlEntry Profile;
  Profile.EntityRef = Spawn.EntityRef;
  Profile.AgentId = 43;
  Profile.MaximumSpeedCmps = 300.0f;
  Profile.ParticlePhysicalRadiusCm = 40.0f;
  Profile.ParticleHardSafetyGapCm = 8.0f;
  Profile.ParticleEnvironmentHardClearanceCm = 8.0f;
  Profile.ParticleSoftMarginCm = 4.0f;
  Profile.ParticleMobility = 1.0f;
  FCrowdWorkerExternalGameplayInput ProfileRevision;
  ProfileRevision.InputSequence = 6;
  ProfileRevision.EntityRef = Spawn.EntityRef;
  ProfileRevision.InputTypeId = static_cast<uint16>(
    ECrowdWorkerExternalGameplayInputType::
      MovementProfileRevision);
  ProfileRevision.DirtyMask = 1;
  TestTrue(TEXT("reuse movement profile encodes"),
    FCrowdWorkerMovementProfileCodec::Encode(
      Profile, ProfileRevision.FullState));
  Reuse.ExternalGameplayInputs.Add(MoveTemp(ProfileRevision));
  Reuse.Clock.InputSequence = 7;
  Reuse.Clock.SimulationTick = 2;
  Reuse.RecalculateStableHash();
  TestEqual(TEXT("same-batch lifecycle reuse is admitted"),
    Runtime.SubmitIntentBatch(Reuse),
    ECrowdAsyncSimulationSubmitResult::Accepted);

  bool bIdle = false;
  bool bFailed = false;
  const double ReuseDeadline = FPlatformTime::Seconds() + 5.0;
  while (FPlatformTime::Seconds() < ReuseDeadline)
  {
    const ECrowdAsyncSimulationPollResult Result = Runtime.Poll();
    bFailed |= Result == ECrowdAsyncSimulationPollResult::Failed;
    if (Result == ECrowdAsyncSimulationPollResult::Idle
      && Runtime.GetMetrics().InputQueueDepth == 0)
    {
      bIdle = true;
      break;
    }
    FPlatformProcess::SleepNoStats(0.0f);
  }
  const FCrowdAsyncSimulationRuntimeMetrics Metrics =
    Runtime.GetMetrics();
  TestFalse(TEXT("reuse does not fail Worker v2"), bFailed);
  TestTrue(TEXT("reuse runtime reaches idle"), bIdle);
  TestEqual(TEXT("reuse applies through spawn sequence"),
    Metrics.LastAppliedInputSequence, uint64{7});
  TestEqual(TEXT("reuse keeps one logical entity"),
    Metrics.MirrorEntityCount, 1);
  TestEqual(TEXT("reuse latches no Worker failure"),
    Metrics.WorkerV2.LastFailure,
    ECrowdWorkerRuntimeV2Failure::None);
  TestTrue(TEXT("profile revision schedules planning once"),
    ExecutionOrder.Contains(
      ECrowdWorkerDomainId::MovementPlanning));
  TestTrue(TEXT("reuse runtime stops"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2AbsoluteResnapshotClockTest,
  "MassCrowd.RuntimeV2.AbsoluteResnapshotClockBaseline",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2AbsoluteResnapshotClockTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(
    TEXT("absolute clock lifecycle domain registers"),
    Runtime.RegisterDomainExecutor(
      MakeUnique<FSyntheticLifecycleDomain>()));
  TestTrue(
    TEXT("absolute clock runtime starts"),
    Runtime.Start(MakeSyntheticConfig(), 16));
  FCrowdWorkerIntentBatch Snapshot = MakeSyntheticSnapshot(16);
  Snapshot.Clock.SimulationTick = 270;
  Snapshot.TargetSimulationTimeSeconds = 9.0;
  Snapshot.RecalculateStableHash();
  TestEqual(
    TEXT("absolute clock snapshot accepted"),
    Runtime.SubmitResnapshot(Snapshot),
    ECrowdAsyncSimulationSubmitResult::Accepted);

  const double Deadline = FPlatformTime::Seconds() + 5.0;
  bool bIdle = false;
  while (FPlatformTime::Seconds() < Deadline)
  {
    const ECrowdAsyncSimulationPollResult Result =
      Runtime.Poll();
    if (Result == ECrowdAsyncSimulationPollResult::Failed)
      break;
    if (Result == ECrowdAsyncSimulationPollResult::Idle)
    {
      bIdle = true;
      break;
    }
    FPlatformProcess::SleepNoStats(0.0f);
  }
  const FCrowdAsyncSimulationRuntimeMetrics Metrics =
    Runtime.GetMetrics();
  TestTrue(TEXT("absolute clock runtime reaches idle"), bIdle);
  TestEqual(
    TEXT("resnapshot restores one epoch instead of replaying from zero"),
    Metrics.WorkerEpoch,
    uint64{1});
  TestTrue(
    TEXT("restored absolute simulation time reaches target"),
    FMath::IsNearlyEqual(
      Metrics.SimulationTimeSeconds, 9.0, 1.e-6));
  TestTrue(
    TEXT("absolute clock runtime stops"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2PropagationBudgetTest,
  "MassCrowd.RuntimeV2.PropagationBudgetDefers",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2PropagationBudgetTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdAsyncSimulationRuntimeConfig Config =
    MakeSyntheticConfig();
  Config.WorkerV2.MaxPropagationRoundsPerEpoch = 1;
  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(
    TEXT("propagating domain registers"),
    Runtime.RegisterDomainExecutor(
      MakeUnique<FPropagatingLifecycleDomain>()));
  TestTrue(
    TEXT("propagation runtime starts"),
    Runtime.Start(Config, 15));
  TestEqual(
    TEXT("propagation snapshot accepted"),
    Runtime.SubmitResnapshot(MakeSyntheticSnapshot(15)),
    ECrowdAsyncSimulationSubmitResult::Accepted);

  const double Deadline = FPlatformTime::Seconds() + 5.0;
  bool bIdle = false;
  while (FPlatformTime::Seconds() < Deadline)
  {
    const ECrowdAsyncSimulationPollResult Result =
      Runtime.Poll();
    if (Result == ECrowdAsyncSimulationPollResult::Failed)
      break;
    if (Result == ECrowdAsyncSimulationPollResult::Idle)
    {
      bIdle = true;
      break;
    }
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestTrue(
    TEXT("propagation budget does not recursively spin"),
    bIdle);
  const FCrowdAsyncSimulationRuntimeMetrics Metrics =
    Runtime.GetMetrics();
  TestEqual(
    TEXT("one propagation round executed"),
    Metrics.WorkerV2.PropagationRoundCount,
    uint64{1});
  TestEqual(
    TEXT("limit hit recorded"),
    Metrics.WorkerV2.PropagationLimitHitCount,
    uint64{1});
  TestEqual(
    TEXT("overflow work deferred to next epoch"),
    Metrics.WorkerV2.WorkCurrentDepth,
    1);
  TestTrue(
    TEXT("propagation runtime stops"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2AuditAndInvalidationTest,
  "MassCrowd.RuntimeV2.AuditFailClosedAndGenerationReset",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2AuditAndInvalidationTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  const FCrowdAsyncSimulationRuntimeConfig Config =
    MakeSyntheticConfig();

  FCrowdAsyncSimulationRuntime Leaky;
  TestTrue(
    TEXT("leaky domain registers"),
    Leaky.RegisterDomainExecutor(
      MakeUnique<FLeakyLifecycleDomain>()));
  TestTrue(TEXT("leaky runtime starts"), Leaky.Start(Config, 9));
  TestEqual(
    TEXT("leaky snapshot accepted"),
    Leaky.SubmitResnapshot(MakeSyntheticSnapshot(9)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  const double FailureDeadline =
    FPlatformTime::Seconds() + 5.0;
  while (FPlatformTime::Seconds() < FailureDeadline
    && Leaky.GetState()
      != ECrowdAsyncSimulationRuntimeState::Failed)
  {
    Leaky.Poll();
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestEqual(
    TEXT("undeclared dependency fails runtime"),
    Leaky.GetState(),
    ECrowdAsyncSimulationRuntimeState::Failed);
  const FCrowdAsyncSimulationRuntimeMetrics FailureMetrics =
    Leaky.GetMetrics();
  TestEqual(
    TEXT("coverage failure reason"),
    FailureMetrics.WorkerV2.LastFailure,
    ECrowdWorkerRuntimeV2Failure::CoverageAudit);
  TestEqual(
    TEXT("coverage failure counted"),
    FailureMetrics.WorkerV2.CoverageAuditFailureCount,
    uint64{1});
  TestTrue(TEXT("failed runtime drains"), Leaky.StopAndDrain(5.0));

  FCrowdAsyncSimulationRuntime ResetRuntime;
  TestTrue(
    TEXT("reset domain registers"),
    ResetRuntime.RegisterDomainExecutor(
      MakeUnique<FSyntheticLifecycleDomain>()));
  TestTrue(
    TEXT("reset runtime starts"),
    ResetRuntime.Start(Config, 11));
  TestEqual(
    TEXT("reset snapshot accepted"),
    ResetRuntime.SubmitResnapshot(MakeSyntheticSnapshot(11)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  const double IdleDeadline = FPlatformTime::Seconds() + 5.0;
  while (FPlatformTime::Seconds() < IdleDeadline)
  {
    const ECrowdAsyncSimulationPollResult Result =
      ResetRuntime.Poll();
    if (Result == ECrowdAsyncSimulationPollResult::Idle
      && ResetRuntime.GetMetrics().InputQueueDepth == 0)
      break;
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestTrue(
    TEXT("pre-invalidation work recorded"),
    ResetRuntime.GetMetrics().WorkerV2.WorkHighWatermark > 0);
  TestTrue(
    TEXT("generation invalidation begins"),
    ResetRuntime.Invalidate(12));
  const double ResetDeadline = FPlatformTime::Seconds() + 5.0;
  while (FPlatformTime::Seconds() < ResetDeadline
    && ResetRuntime.GetState()
      == ECrowdAsyncSimulationRuntimeState::Invalidating)
  {
    ResetRuntime.Poll();
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestEqual(
    TEXT("runtime awaits new generation snapshot"),
    ResetRuntime.GetState(),
    ECrowdAsyncSimulationRuntimeState::Starting);
  const FCrowdAsyncSimulationRuntimeMetrics ResetMetrics =
    ResetRuntime.GetMetrics();
  TestEqual(
    TEXT("work high watermark reset"),
    ResetMetrics.WorkerV2.WorkHighWatermark,
    0);
  TestEqual(
    TEXT("dependency high watermark reset"),
    ResetMetrics.WorkerV2.DependencyHighWatermark,
    0);
  TestEqual(
    TEXT("failure reason reset"),
    ResetMetrics.WorkerV2.LastFailure,
    ECrowdWorkerRuntimeV2Failure::None);
  TestTrue(
    TEXT("reset runtime drains"),
    ResetRuntime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2AutonomousMovementTest,
  "MassCrowd.RuntimeV2.AutonomousMovementTimeWheel",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2AutonomousMovementTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  const FCrowdStableEntityRef EntityRef{1, 77, 1};
  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("entity store resets"), States.Reset(4, 256));
  TestEqual(
    TEXT("entity spawns"),
    States.Spawn(EntityRef, 5, 1, MakePayload(77)),
    ECrowdWorkerQueueResult::Added);

  FCrowdWorkerMovementState Initial;
  Initial.Position = FVector(10.0, 20.0, 0.0);
  Initial.Velocity = FVector(30.0, 0.0, 0.0);
  Initial.YawDegrees = 0.0f;
  Initial.SimulationTimeSeconds = 0.0;
  FCrowdWorkerDirtyStateRecord InitialRecord;
  InitialRecord.EntityRef = EntityRef;
  InitialRecord.Field = ECrowdWorkerField::Movement;
  InitialRecord.Generation = 5;
  InitialRecord.WorkerEpoch = 1;
  InitialRecord.StateRevision = 1;
  InitialRecord.SourceInputSequence = 1;
  TestTrue(
    TEXT("movement state encodes"),
    FCrowdWorkerMovementStateCodec::Encode(
      Initial, InitialRecord.Payload));
  TestEqual(
    TEXT("movement state seeds"),
    States.ApplyDirty(InitialRecord),
    ECrowdWorkerQueueResult::Replaced);

  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("resource store resets"), Resources.Reset(256));
  FCrowdWorkerDomainContext Context;
  Context.Generation = 5;
  Context.WorkerEpoch = 2;
  Context.AbsoluteSimulationTick = 2;
  Context.LastAppliedInputSequence = 1;
  Context.FixedDeltaSeconds = 0.5;
  Context.EntityStates = &States;
  Context.Resources = &Resources;
  FCrowdWorkerMovementDomainExecutor Executor;
  FCrowdWorkerDomainOutput Output;
  const FCrowdWorkerWorkItem Work =
    MakeEntityWork(ECrowdWorkerDomainId::Movement, 77);
  TestTrue(
    TEXT("movement executor advances autonomously"),
    Executor.Execute(Context, MakeArrayView(&Work, 1), Output));
  TestEqual(
    TEXT("one movement patch"),
    Output.DirtyStates.Num(), 1);
  TestEqual(
    TEXT("one next tick wakeup"),
    Output.Wakeups.Num(), 1);
  TestEqual(
    TEXT("snapshot-only movement does not wake resource-driven particle"),
    Output.NextWork.Num(), 0);
  if (Output.DirtyStates.Num() == 1)
  {
    FCrowdWorkerMovementState Advanced;
    TestTrue(
      TEXT("advanced movement decodes"),
      FCrowdWorkerMovementStateCodec::Decode(
        Output.DirtyStates[0].Payload, Advanced));
    TestTrue(
      TEXT("position integrates fixed delta"),
      Advanced.Position.Equals(FVector(25.0, 20.0, 0.0)));
  }
  if (Output.Wakeups.Num() == 1)
  {
    TestEqual(
      TEXT("wakeup targets next absolute tick"),
      Output.Wakeups[0].AbsoluteSimulationTick,
      uint64{3});
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2FlowResourceTest,
  "MassCrowd.RuntimeV2.FlowResourceRoundTrip",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2FlowResourceTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdSharedFlowFieldConfig Config =
    FCrowdSharedFlowFieldKernel::MakeSf1Config(9);
  FCrowdSharedFlowField Field;
  TestTrue(
    TEXT("shared flow fixture builds"),
    FCrowdSharedFlowFieldKernel::Build(Config, Field));
  FCrowdWorkerPayload Payload;
  TestTrue(
    TEXT("flow resource encodes"),
    FCrowdWorkerFlowFieldResourceCodec::Encode(
      Field, Payload));
  FCrowdWorkerFlowFieldResource Decoded;
  TestTrue(
    TEXT("flow resource decodes"),
    FCrowdWorkerFlowFieldResourceCodec::Decode(
      Payload, Decoded));
  TestEqual(
    TEXT("flow revision survives round trip"),
    Decoded.Revision,
    uint64{9});
  TestEqual(
    TEXT("flow build hash survives round trip"),
    Decoded.BuildHash,
    Field.BuildHash);
  TestEqual(
    TEXT("full flow topology hash survives round trip"),
    Decoded.Field.TopologyHash,
    Field.TopologyHash);
  TestEqual(
    TEXT("full flow integration hash survives round trip"),
    Decoded.Field.IntegrationHash,
    Field.IntegrationHash);
  TestEqual(
    TEXT("navigation nodes survive round trip"),
    Decoded.Field.NavigationNodes.Num(),
    Field.NavigationNodes.Num());
  TestEqual(
    TEXT("navigation edges survive round trip"),
    Decoded.Field.NavigationEdges.Num(),
    Field.NavigationEdges.Num());
  TestEqual(
    TEXT("navigation next-node table survives round trip"),
    Decoded.Field.NavigationNextNodeIndex.Num(),
    Field.NavigationNextNodeIndex.Num());
  FVector Direction;
  bool bReachable = false;
  TestTrue(
    TEXT("goal-adjacent cell samples"),
    Decoded.Sample(
      Field.Config.GoalLocation, Direction, bReachable));
  TestEqual(
    TEXT("reachability agrees with canonical direction"),
    bReachable,
    !Direction.IsNearlyZero());
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2MultiFieldExchangeTest,
  "MassCrowd.RuntimeV2.MultiFieldExchange",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2MultiFieldExchangeTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdWorkerContractLimits Limits;
  Limits.MaxPayloadBytes = 64;
  Limits.MaxInputRecordsPerBatch = 8;
  Limits.MaxStatePatchesPerSlot = 8;
  Limits.MaxPendingOrderedEvents = 8;
  FCrowdWorkerPublishedExchange Exchange;
  TestTrue(TEXT("exchange resets"), Exchange.ResetQuiescent(7, Limits));
  const FCrowdStableEntityRef EntityRef{1, 42, 1};
  for (uint16 FieldId = 1; FieldId <= 2; ++FieldId)
  {
    FCrowdWorkerStatePatch Patch;
    Patch.EntityRef = EntityRef;
    Patch.StateFieldId = FieldId;
    Patch.Generation = 7;
    Patch.WorkerEpoch = 3;
    Patch.SourceInputSequence = 2;
    Patch.DirtyMask = 1ull << (16 + FieldId);
    Patch.State.StateRevision = 3;
    Patch.State.Payload = MakePayload(FieldId);
    Patch.RecalculateStableHash();
    TestEqual(
      TEXT("independent field patch appends"),
      Exchange.AppendStatePatch(Patch),
      ECrowdWorkerAppendResult::Appended);
  }
  FCrowdWorkerPublishMetadata Metadata;
  Metadata.Generation = 7;
  Metadata.PublishSequence = 1;
  Metadata.MinWorkerEpoch = 3;
  Metadata.MaxWorkerEpoch = 3;
  Metadata.LastAppliedInputSequence = 2;
  Metadata.PublishedSimulationTimeSeconds = 0.1;
  TestEqual(
    TEXT("multi-field batch publishes"),
    Exchange.TryPublishBuildingBatch(Metadata),
    ECrowdWorkerPublishResult::Published);
  const FCrowdWorkerPublishedBatch* Batch = nullptr;
  TestEqual(
    TEXT("multi-field batch exchanges"),
    Exchange.TryExchangePublishedBatch(7, 1, Batch),
    ECrowdWorkerExchangeResult::Exchanged);
  TestTrue(TEXT("batch is present"), Batch != nullptr);
  if (Batch)
  {
    TestEqual(
      TEXT("both fields survive"),
      Batch->StatePatches.Num(), 2);
    if (Batch->StatePatches.Num() == 2)
    {
      TestEqual(
        TEXT("first field sorted"),
        Batch->StatePatches[0].StateFieldId, uint16{1});
      TestEqual(
        TEXT("second field sorted"),
        Batch->StatePatches[1].StateFieldId, uint16{2});
    }
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2ParticleIslandTest,
  "MassCrowd.RuntimeV2.ParticleClosedIsland",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2ParticleIslandTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("particle states reset"), States.Reset(4, 256));
  const FCrowdStableEntityRef A{1, 1, 1};
  const FCrowdStableEntityRef B{1, 2, 1};
  TestEqual(
    TEXT("A spawns"),
    States.Spawn(
      A, 9, 2, MakeBoundaryStatePayload(
        A, 1, FVector(0.0, 0.0, 0.0))),
    ECrowdWorkerQueueResult::Added);
  TestEqual(
    TEXT("B spawns"),
    States.Spawn(
      B, 9, 2, MakeBoundaryStatePayload(
        B, 2, FVector(50.0, 0.0, 0.0))),
    ECrowdWorkerQueueResult::Added);
  const auto SeedMovement = [&States](
    const FCrowdStableEntityRef& EntityRef,
    const FVector& Position,
    const uint64 Revision)
  {
    FCrowdWorkerMovementState Movement;
    Movement.StartPosition = Position;
    Movement.Position = Position;
    Movement.SimulationTimeSeconds = 0.0;
    FCrowdWorkerDirtyStateRecord Record;
    Record.EntityRef = EntityRef;
    Record.Field = ECrowdWorkerField::Movement;
    Record.Generation = 9;
    Record.WorkerEpoch = 1;
    Record.StateRevision = Revision;
    Record.SourceInputSequence = 2;
    return FCrowdWorkerMovementStateCodec::Encode(
        Movement, Record.Payload)
      && States.ApplyDirty(Record)
        == ECrowdWorkerQueueResult::Replaced;
  };
  TestTrue(TEXT("A movement seeds"), SeedMovement(
    A, FVector(0.0, 0.0, 0.0), 1));
  TestTrue(TEXT("B movement seeds"), SeedMovement(
    B, FVector(50.0, 0.0, 0.0), 2));
  FCrowdWorkerSpatialIndex Spatial;
  TestTrue(TEXT("spatial resets"), Spatial.Reset(4, 100.0f));
  TestTrue(TEXT("spatial rebuilds"), Spatial.Rebuild(States));
  TArray<FCrowdWorkerSpatialEntry> Neighbors;
  TestTrue(
    TEXT("neighbor query succeeds"),
    Spatial.QueryNeighbors(A, 200.0f, Neighbors));
  TestEqual(TEXT("one unique neighbor"), Neighbors.Num(), 1);
  FCrowdWorkerInteractionPairKey Forward{A, B};
  FCrowdWorkerInteractionPairKey Reverse{B, A};
  TestTrue(TEXT("forward pair normalizes"), Forward.Normalize());
  TestTrue(TEXT("reverse pair normalizes"), Reverse.Normalize());
  TestTrue(TEXT("pair key is unique"), Forward == Reverse);

  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("particle resources reset"), Resources.Reset(65536));
  FCrowdWorkerMovementControlResource Control;
  Control.Revision = 1;
  Control.FixedStepIndex = 2;
  Control.PlanRevision = 1;
  Control.bRunParticleInteraction = true;
  Control.ParticleSettings.FixedStepSeconds = 1.0f / 30.0f;
  FCrowdWorkerMovementControlEntry& ControlA =
    Control.Entries.AddDefaulted_GetRef();
  ControlA.EntityRef = A;
  ControlA.AgentId = 1;
  ControlA.MaximumSpeedCmps = 300.0f;
  ControlA.ParticlePhysicalRadiusCm = 42.0f;
  ControlA.ParticleHardSafetyGapCm = 10.0f;
  ControlA.ParticleSoftMarginCm = 8.0f;
  ControlA.ParticleMobility = 1.0f;
  FCrowdWorkerMovementControlEntry& ControlB =
    Control.Entries.AddDefaulted_GetRef();
  ControlB.EntityRef = B;
  ControlB.AgentId = 2;
  ControlB.MaximumSpeedCmps = 300.0f;
  ControlB.ParticlePhysicalRadiusCm = 42.0f;
  ControlB.ParticleHardSafetyGapCm = 10.0f;
  ControlB.ParticleSoftMarginCm = 8.0f;
  ControlB.ParticleMobility = 1.0f;
  FCrowdWorkerPayload ControlPayload;
  TestTrue(
    TEXT("particle control encodes"),
    FCrowdWorkerMovementControlResourceCodec::Encode(
      Control, ControlPayload));
  TestEqual(
    TEXT("particle control stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::MovementControl,
      Control.Revision,
      MoveTemp(ControlPayload)}),
    ECrowdWorkerQueueResult::Added);
  TArray<FCrowdWorkerResourceRevisionEvent> ResourceEvents;
  TestTrue(
    TEXT("particle control commits"),
    Resources.CommitBuildingAtEpoch(2, ResourceEvents));
  FCrowdWorkerDomainContext Context;
  Context.Generation = 9;
  Context.WorkerEpoch = 2;
  Context.AbsoluteSimulationTick = 2;
  Context.LastAppliedInputSequence = 2;
  Context.FixedDeltaSeconds = 1.0 / 30.0;
  Context.RuntimeMode = ECrowdWorkerRuntimeV2Mode::Production;
  Context.EntityStates = &States;
  Context.Resources = &Resources;
  Context.SpatialIndex = &Spatial;
  FCrowdWorkerParticleInteractionDomainExecutor Executor;
  FCrowdWorkerWorkItem ParticleWork;
  ParticleWork.Key.Domain =
    ECrowdWorkerDomainId::ParticleInteraction;
  ParticleWork.Key.Kind = ECrowdWorkerWorkKind::Resource;
  ParticleWork.Key.ScopeKey =
    CrowdWorkerResourceIds::MovementControl;
  TArray<FCrowdWorkerWorkItem> Work{ParticleWork};
  FCrowdWorkerDomainOutput Output;
  TestTrue(
    TEXT("closed island resolves"),
    Executor.Execute(Context, Work, Output));
  TestEqual(
    TEXT("both island entities publish final state"),
    Output.DirtyStates.Num(), 2);
  if (Output.DirtyStates.Num() == 2)
  {
    FCrowdWorkerParticleState StateA;
    FCrowdWorkerParticleState StateB;
    TestTrue(TEXT("A particle decodes"),
      FCrowdWorkerParticleStateCodec::Decode(
        Output.DirtyStates[0].Payload, StateA));
    TestTrue(TEXT("B particle decodes"),
      FCrowdWorkerParticleStateCodec::Decode(
        Output.DirtyStates[1].Payload, StateB));
    TestTrue(
      TEXT("pair contributions are symmetric"),
      (StateA.PositionOffset
        + StateB.PositionOffset).IsNearlyZero());
  }
  for (const FCrowdWorkerDirtyStateRecord& Dirty :
    Output.DirtyStates)
  {
    TestTrue(
      TEXT("particle output applies"),
      States.ApplyDirty(Dirty)
        == ECrowdWorkerQueueResult::Replaced);
  }
  FCrowdWorkerFacingFinalizeDomainExecutor Facing;
  FCrowdWorkerDomainOutput FacingOutput;
  TestTrue(
    TEXT("particle island finalizes"),
    Facing.Execute(Context, Output.NextWork, FacingOutput));
  TestEqual(
    TEXT("both final kinematics publish"),
    FacingOutput.DirtyStates.Num(), 2);
  for (const FCrowdWorkerDirtyStateRecord& Dirty :
    FacingOutput.DirtyStates)
  {
    TestTrue(
      TEXT("final kinematic applies"),
      States.ApplyDirty(Dirty)
        == ECrowdWorkerQueueResult::Replaced);
  }
  const FCrowdWorkerDirtyStateRecord* PreviousFinal =
    States.Find(A, ECrowdWorkerField::Facing);
  FCrowdWorkerMovementState PreviousFinalState;
  TestTrue(
    TEXT("previous final kinematic decodes"),
    PreviousFinal
      && FCrowdWorkerMovementStateCodec::Decode(
        PreviousFinal->Payload, PreviousFinalState));
  Context.WorkerEpoch = 3;
  Context.AbsoluteSimulationTick = 3;
  Context.LastAppliedInputSequence = 3;
  Context.SimulationTimeSeconds = 1.0 / 15.0;
  FCrowdWorkerMovementDomainExecutor Movement;
  TArray<FCrowdWorkerWorkItem> NextMovementWork{
    MakeEntityWork(ECrowdWorkerDomainId::Movement, 1)};
  FCrowdWorkerDomainOutput NextMovementOutput;
  TestTrue(
    TEXT("next movement executes from final state"),
    Movement.Execute(
      Context, NextMovementWork, NextMovementOutput));
  TestEqual(
    TEXT("one next movement state"),
    NextMovementOutput.DirtyStates.Num(), 1);
  if (NextMovementOutput.DirtyStates.Num() == 1)
  {
    FCrowdWorkerMovementState NextMovement;
    TestTrue(
      TEXT("next movement decodes"),
      FCrowdWorkerMovementStateCodec::Decode(
        NextMovementOutput.DirtyStates[0].Payload,
        NextMovement));
    TestTrue(
      TEXT("particle-final state is next epoch baseline"),
      NextMovement.StartPosition.Equals(
        PreviousFinalState.Position, 1.e-6));
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2ParticleTargetObjectiveTest,
  "MassCrowd.RuntimeV2.ParticleUsesLiveTargetObjective",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2ParticleTargetObjectiveTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 10;
  constexpr uint64 InputSequence = 3;
  const FCrowdStableEntityRef EntityRef{1, 1, 1};
  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("target particle states reset"),
    States.Reset(2, 256));
  TestEqual(TEXT("target particle entity spawns"),
    States.Spawn(
      EntityRef, Generation, InputSequence,
      MakeBoundaryStatePayload(
        EntityRef, 1, FVector::ZeroVector)),
    ECrowdWorkerQueueResult::Added);
  FCrowdWorkerMovementState Movement;
  Movement.StartPosition = FVector::ZeroVector;
  Movement.Position = FVector::ZeroVector;
  FCrowdWorkerDirtyStateRecord MovementRecord;
  MovementRecord.EntityRef = EntityRef;
  MovementRecord.Field = ECrowdWorkerField::Movement;
  MovementRecord.Generation = Generation;
  MovementRecord.WorkerEpoch = 2;
  MovementRecord.StateRevision = 1;
  MovementRecord.SourceInputSequence = InputSequence;
  TestTrue(TEXT("target particle movement encodes"),
    FCrowdWorkerMovementStateCodec::Encode(
      Movement, MovementRecord.Payload));
  TestEqual(TEXT("target particle movement applies"),
    States.ApplyDirty(MovementRecord),
    ECrowdWorkerQueueResult::Replaced);

  FCrowdWorkerMovementControlResource Control;
  Control.Revision = 1;
  Control.FixedStepIndex = 1;
  Control.PlanRevision = 1;
  Control.bRunParticleInteraction = true;
  Control.bParticleConstrainToFlowBounds = false;
  Control.ParticleSettings.FixedStepSeconds = 1.0f / 30.0f;
  FCrowdWorkerMovementControlEntry& Profile =
    Control.Entries.AddDefaulted_GetRef();
  Profile.EntityRef = EntityRef;
  Profile.AgentId = 1;
  Profile.MaximumSpeedCmps = 300.0f;
  Profile.ParticlePhysicalRadiusCm = 42.0f;
  Profile.ParticleHardSafetyGapCm = 10.0f;
  Profile.ParticleSoftMarginCm = 8.0f;
  Profile.ParticleMobility = 1.0f;
  FCrowdParticleConstraintAgent& TargetTemplate =
    Control.ExternalParticleAgents.AddDefaulted_GetRef();
  TargetTemplate.AgentId =
    CrowdWorkerTargetConstants::PrimaryTargetParticleAgentId;
  TargetTemplate.StartPosition = FVector(10000.0, 0.0, 0.0);
  TargetTemplate.PredictedPosition =
    FVector(10000.0, 0.0, 0.0);
  TargetTemplate.PhysicalRadiusCm = 42.0f;
  TargetTemplate.HardSafetyGapCm = 10.0f;
  TargetTemplate.SoftMarginCm = 8.0f;
  TargetTemplate.Mobility = 0.0f;
  FCrowdWorkerPayload ControlPayload;
  TestTrue(TEXT("target particle control encodes"),
    FCrowdWorkerMovementControlResourceCodec::Encode(
      Control, ControlPayload));
  const FCrowdWorkerPayload MissingObjectiveControlPayload =
    ControlPayload;

  FCrowdWorkerTargetObjectiveRevision Objective;
  Objective.TargetRevision = 2;
  Objective.EffectiveFixedStepIndex = 3;
  Objective.TargetLocation = FVector2f(50.0f, 0.0f);
  Objective.TargetVelocity = FVector2f(30.0f, 0.0f);
  FCrowdWorkerPayload ObjectivePayload;
  TestTrue(TEXT("live target objective encodes"),
    FCrowdWorkerTargetObjectiveRevisionCodec::Encode(
      Objective, ObjectivePayload));

  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("target particle resources reset"),
    Resources.Reset(65536));
  TestEqual(TEXT("target particle control stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::MovementControl,
      Control.Revision, MoveTemp(ControlPayload)}),
    ECrowdWorkerQueueResult::Added);
  TestEqual(TEXT("live target objective stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::ObjectiveRevision(
        CrowdWorkerTargetObjectiveIds::PrimaryTarget),
      1, MoveTemp(ObjectivePayload)}),
    ECrowdWorkerQueueResult::Added);
  TArray<FCrowdWorkerResourceRevisionEvent> ResourceEvents;
  TestTrue(TEXT("target particle resources commit"),
    Resources.CommitBuildingAtEpoch(3, ResourceEvents));

  FCrowdWorkerSpatialIndex Spatial;
  TestTrue(TEXT("target particle spatial resets"),
    Spatial.Reset(2, 400.0f));
  TestTrue(TEXT("target particle spatial rebuilds"),
    Spatial.Rebuild(States));
  FCrowdWorkerDomainContext Context;
  Context.Generation = Generation;
  Context.WorkerEpoch = 3;
  Context.AbsoluteSimulationTick = 3;
  Context.LastAppliedInputSequence = InputSequence;
  Context.FixedDeltaSeconds = 1.0 / 30.0;
  Context.SimulationTimeSeconds = 3.0 / 30.0;
  Context.RuntimeMode = ECrowdWorkerRuntimeV2Mode::Production;
  Context.EntityStates = &States;
  Context.Resources = &Resources;
  Context.SpatialIndex = &Spatial;
  FCrowdWorkerWorkItem ParticleWork;
  ParticleWork.Key.Domain =
    ECrowdWorkerDomainId::ParticleInteraction;
  ParticleWork.Key.Kind = ECrowdWorkerWorkKind::Resource;
  ParticleWork.Key.ScopeKey =
    CrowdWorkerResourceIds::MovementControl;
  FCrowdWorkerParticleInteractionDomainExecutor Executor;
  FCrowdWorkerResourceStore MissingObjectiveResources;
  TestTrue(TEXT("missing objective resources reset"),
    MissingObjectiveResources.Reset(65536));
  TestEqual(TEXT("missing objective control stages"),
    MissingObjectiveResources.StageBuilding({
      CrowdWorkerResourceIds::MovementControl,
      Control.Revision, MissingObjectiveControlPayload}),
    ECrowdWorkerQueueResult::Added);
  TArray<FCrowdWorkerResourceRevisionEvent>
    MissingObjectiveResourceEvents;
  TestTrue(TEXT("missing objective control commits"),
    MissingObjectiveResources.CommitBuildingAtEpoch(
      3, MissingObjectiveResourceEvents));
  Context.Resources = &MissingObjectiveResources;
  FCrowdWorkerDomainOutput MissingObjectiveOutput;
  AddExpectedError(
    TEXT("CrowdWorkerParticleDomainRejected stage=objective"),
    EAutomationExpectedErrorFlags::Contains, 1);
  TestFalse(TEXT("target particle without objective fails closed"),
    Executor.Execute(Context, {ParticleWork}, MissingObjectiveOutput));
  Context.Resources = &Resources;
  FCrowdWorkerDomainOutput Output;
  TestTrue(TEXT("particle consumes live target objective"),
    Executor.Execute(Context, {ParticleWork}, Output));
  TestEqual(TEXT("one target particle patch publishes"),
    Output.DirtyStates.Num(), 1);
  if (Output.DirtyStates.Num() == 1)
  {
    FCrowdWorkerParticleState Particle;
    TestTrue(TEXT("live target particle patch decodes"),
      FCrowdWorkerParticleStateCodec::Decode(
        Output.DirtyStates[0].Payload, Particle));
    TestTrue(TEXT("live objective moves colliding entity"),
      !Particle.PositionOffset.IsNearlyZero());
  }
  const uint64 ObjectiveResourceId =
    CrowdWorkerResourceIds::ObjectiveRevision(
      CrowdWorkerTargetObjectiveIds::PrimaryTarget);
  TestTrue(TEXT("particle declares objective dependency"),
    Output.DeclaredDependencies.ContainsByPredicate(
      [ObjectiveResourceId](const auto& Dependency)
      {
        return Dependency.Source.Kind
            == ECrowdWorkerDependencyKind::Resource
          && Dependency.Source.ScopeKey == ObjectiveResourceId;
      }));
  TestTrue(TEXT("particle observes objective dependency"),
    Output.ObservedDependencies.ContainsByPredicate(
      [ObjectiveResourceId](const auto& Observation)
      {
        return Observation.Source.Kind
            == ECrowdWorkerDependencyKind::Resource
          && Observation.Source.ScopeKey == ObjectiveResourceId;
      }));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2HeterogeneousTransitParticleBootstrapTest,
  "MassCrowd.RuntimeV2.HeterogeneousTransitParticleBootstrap",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2HeterogeneousTransitParticleBootstrapTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 12;
  constexpr uint64 InputSequence = 64;
  constexpr uint64 Epoch = 1;
  constexpr int32 AgentCount = 20;
  static constexpr int32 ProfileByFormation[AgentCount] = {
    0, 0, 0,
    1, 1, 1,
    2, 2, 2,
    3, 3,
    4, 4, 4,
    5, 5, 5,
    6, 6, 6,
  };
  static constexpr float RadiusByProfile[7] = {
    30.0f, 30.0f, 42.0f, 42.0f, 42.0f, 60.0f, 60.0f};
  static constexpr float MobilityByProfile[7] = {
    2.0f, 2.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f};

  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("heterogeneous states reset"),
    States.Reset(32, 4096));
  FCrowdWorkerMovementControlResource Control;
  Control.Revision = 1;
  Control.FixedStepIndex = 271;
  Control.PlanRevision = 1;
  Control.bRunParticleInteraction = true;
  Control.bParticleConstrainToFlowBounds = false;
  Control.ParticleSettings.FixedStepSeconds = 1.0f / 30.0f;
  for (int32 FormationIndex = 0;
    FormationIndex < AgentCount; ++FormationIndex)
  {
    const FCrowdStableEntityRef EntityRef{
      1, static_cast<uint64>(FormationIndex + 1), 1};
    const FVector Position(
      static_cast<double>(FormationIndex) * 500.0, 0.0, 0.0);
    TestEqual(TEXT("heterogeneous entity spawns"),
      States.Spawn(
        EntityRef, Generation, InputSequence,
        MakeBoundaryStatePayload(
          EntityRef, FormationIndex + 1, Position)),
      ECrowdWorkerQueueResult::Added);
    FCrowdWorkerMovementState Movement;
    Movement.StartPosition = Position;
    Movement.Position = Position;
    FCrowdWorkerDirtyStateRecord MovementRecord;
    MovementRecord.EntityRef = EntityRef;
    MovementRecord.Field = ECrowdWorkerField::Movement;
    MovementRecord.Generation = Generation;
    MovementRecord.WorkerEpoch = Epoch;
    MovementRecord.StateRevision = FormationIndex + 1;
    MovementRecord.SourceInputSequence = InputSequence;
    TestTrue(TEXT("heterogeneous movement encodes"),
      FCrowdWorkerMovementStateCodec::Encode(
        Movement, MovementRecord.Payload));
    TestEqual(TEXT("heterogeneous movement applies"),
      States.ApplyDirty(MovementRecord),
      ECrowdWorkerQueueResult::Replaced);

    const int32 ProfileId = ProfileByFormation[FormationIndex];
    FCrowdWorkerMovementControlEntry& Entry =
      Control.Entries.AddDefaulted_GetRef();
    Entry.EntityRef = EntityRef;
    Entry.AgentId = FormationIndex + 1;
    Entry.MaximumSpeedCmps = 800.0f;
    Entry.ParticleEnvironmentHardClearanceCm = 70.0f;
    Entry.ParticlePhysicalRadiusCm = RadiusByProfile[ProfileId];
    Entry.ParticleHardSafetyGapCm = 10.0f;
    Entry.ParticleSoftMarginCm = 17.0f;
    Entry.ParticleMobility = MobilityByProfile[ProfileId];

    FCrowdWorkerPayload ProfilePayload;
    TestTrue(TEXT("heterogeneous movement profile encodes"),
      FCrowdWorkerMovementProfileCodec::Encode(
        Entry, ProfilePayload));
    FCrowdWorkerMovementControlEntry DecodedProfile;
    TestTrue(TEXT("heterogeneous movement profile decodes"),
      FCrowdWorkerMovementProfileCodec::Decode(
        ProfilePayload, DecodedProfile));
    TestEqual(TEXT("heterogeneous radius preserved"),
      DecodedProfile.ParticlePhysicalRadiusCm,
      RadiusByProfile[ProfileId]);
    TestEqual(TEXT("heterogeneous hard gap preserved"),
      DecodedProfile.ParticleHardSafetyGapCm, 10.0f);
    TestEqual(TEXT("heterogeneous soft margin preserved"),
      DecodedProfile.ParticleSoftMarginCm, 17.0f);
    TestEqual(TEXT("heterogeneous mobility preserved"),
      DecodedProfile.ParticleMobility,
      MobilityByProfile[ProfileId]);
    TestEqual(TEXT("heterogeneous environment clearance preserved"),
      DecodedProfile.ParticleEnvironmentHardClearanceCm, 70.0f);
  }
  FCrowdParticleConstraintAgent& TargetParticle =
    Control.ExternalParticleAgents.AddDefaulted_GetRef();
  TargetParticle.AgentId =
    CrowdWorkerTargetConstants::PrimaryTargetParticleAgentId;
  TargetParticle.StartPosition = FVector(10000.0, 10000.0, 0.0);
  TargetParticle.PredictedPosition = TargetParticle.StartPosition;
  TargetParticle.PhysicalRadiusCm = 100.0f;
  TargetParticle.HardSafetyGapCm = 10.0f;
  TargetParticle.SoftMarginCm = 17.0f;
  TargetParticle.Mobility = 0.0f;

  FCrowdWorkerMovementControlEntry InvalidProfile =
    Control.Entries[0];
  InvalidProfile.ParticleMobility = -0.5f;
  FCrowdWorkerPayload InvalidProfilePayload;
  TestFalse(TEXT("negative heterogeneous mobility fails closed"),
    FCrowdWorkerMovementProfileCodec::Encode(
      InvalidProfile, InvalidProfilePayload));

  FCrowdWorkerPayload ControlPayload;
  TestTrue(TEXT("heterogeneous movement control encodes"),
    FCrowdWorkerMovementControlResourceCodec::Encode(
      Control, ControlPayload));
  FCrowdWorkerMovementControlResource DecodedControl;
  TestTrue(TEXT("heterogeneous movement control decodes"),
    FCrowdWorkerMovementControlResourceCodec::Decode(
      ControlPayload, DecodedControl));
  TestEqual(TEXT("heterogeneous control entry count"),
    DecodedControl.Entries.Num(), AgentCount);
  TestEqual(TEXT("transit control preserves physical target particle"),
    DecodedControl.ExternalParticleAgents.Num(), 1);

  FCrowdWorkerTargetObjectiveRevision Objective;
  Objective.TargetRevision = 1;
  Objective.EffectiveFixedStepIndex = 271;
  Objective.TargetLocation = FVector2f(10000.0f, 10000.0f);
  FCrowdWorkerPayload ObjectivePayload;
  TestTrue(TEXT("heterogeneous target objective encodes"),
    FCrowdWorkerTargetObjectiveRevisionCodec::Encode(
      Objective, ObjectivePayload));

  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("heterogeneous resources reset"),
    Resources.Reset(65536));
  TestEqual(TEXT("heterogeneous control stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::MovementControl,
      Control.Revision, MoveTemp(ControlPayload)}),
    ECrowdWorkerQueueResult::Added);
  TestEqual(TEXT("heterogeneous target objective stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::ObjectiveRevision(
        CrowdWorkerTargetObjectiveIds::PrimaryTarget),
      1, MoveTemp(ObjectivePayload)}),
    ECrowdWorkerQueueResult::Added);
  TArray<FCrowdWorkerResourceRevisionEvent> ResourceEvents;
  TestTrue(TEXT("heterogeneous resources commit"),
    Resources.CommitBuildingAtEpoch(Epoch, ResourceEvents));

  FCrowdWorkerSpatialIndex Spatial;
  TestTrue(TEXT("heterogeneous spatial resets"),
    Spatial.Reset(32, 400.0f));
  TestTrue(TEXT("heterogeneous spatial rebuilds"),
    Spatial.Rebuild(States));
  TestEqual(TEXT("heterogeneous spatial entity count"),
    Spatial.Num(), AgentCount);

  FCrowdWorkerDomainContext Context;
  Context.Generation = Generation;
  Context.WorkerEpoch = Epoch;
  Context.AbsoluteSimulationTick = 271;
  Context.LastAppliedInputSequence = InputSequence;
  Context.FixedDeltaSeconds = 1.0 / 30.0;
  Context.SimulationTimeSeconds = 271.0 / 30.0;
  Context.RuntimeMode = ECrowdWorkerRuntimeV2Mode::Production;
  Context.EntityStates = &States;
  Context.Resources = &Resources;
  Context.SpatialIndex = &Spatial;
  FCrowdWorkerWorkItem ParticleWork;
  ParticleWork.Key.Domain =
    ECrowdWorkerDomainId::ParticleInteraction;
  ParticleWork.Key.Kind = ECrowdWorkerWorkKind::Resource;
  ParticleWork.Key.ScopeKey =
    CrowdWorkerResourceIds::MovementControl;
  FCrowdWorkerParticleInteractionDomainExecutor Executor;
  FCrowdWorkerDomainOutput FirstOutput;
  FCrowdWorkerDomainOutput SecondOutput;
  TestTrue(TEXT("heterogeneous transit particle bootstrap succeeds"),
    Executor.Execute(Context, {ParticleWork}, FirstOutput));
  TestTrue(TEXT("heterogeneous particle bootstrap repeats"),
    Executor.Execute(Context, {ParticleWork}, SecondOutput));
  TestEqual(TEXT("heterogeneous particle output count"),
    FirstOutput.DirtyStates.Num(), AgentCount);
  TestEqual(TEXT("heterogeneous repeat output count"),
    SecondOutput.DirtyStates.Num(), FirstOutput.DirtyStates.Num());
  if (SecondOutput.DirtyStates.Num() == FirstOutput.DirtyStates.Num())
  {
    for (int32 Index = 0; Index < FirstOutput.DirtyStates.Num(); ++Index)
    {
      TestTrue(TEXT("heterogeneous output entity deterministic"),
        FirstOutput.DirtyStates[Index].EntityRef
          == SecondOutput.DirtyStates[Index].EntityRef);
      TestEqual(TEXT("heterogeneous output payload deterministic"),
        FirstOutput.DirtyStates[Index].Payload.StableHash,
        SecondOutput.DirtyStates[Index].Payload.StableHash);
    }
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2MovementControlCodecTest,
  "MassCrowd.RuntimeV2.MovementControlRoundTrip",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2MovementControlCodecTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  TestEqual(
    TEXT("existing movement domain id stays stable"),
    static_cast<uint8>(ECrowdWorkerDomainId::Movement),
    uint8{5});
  TestEqual(
    TEXT("existing movement field id stays stable"),
    static_cast<uint8>(ECrowdWorkerField::Movement),
    uint8{6});
  TestEqual(
    TEXT("planning domain is append-only"),
    static_cast<uint8>(ECrowdWorkerDomainId::MovementPlanning),
    uint8{9});
  TestEqual(
    TEXT("plan field is append-only"),
    static_cast<uint8>(ECrowdWorkerField::MovementPlan),
    uint8{11});
  TestEqual(
    TEXT("movement profile field is append-only"),
    static_cast<uint8>(ECrowdWorkerField::MovementProfile),
    uint8{13});
  FCrowdWorkerMovementControlResource Source;
  Source.Revision = 17;
  Source.FixedStepIndex = 9;
  Source.PlanRevision = 3;
  Source.bRunLocalPredictive = true;
  Source.bApplyEnvironmentMovementConstraint = true;
  Source.bRunParticleInteraction = true;
  Source.bParticleConstrainToFlowBounds = true;
  Source.ParticleSettings.IterationCount = 6;
  Source.ParticleSettings.SafetyIterationCount = 9;
  Source.ParticleSettings.bCaptureRouteDiagnostic = true;
  Source.Environment.Revision = 4;
  Source.Environment.GoalLocation = FVector(600.0, 25.0, 0.0);
  FCrowdSharedFlowObstacleSpec& Obstacle =
    Source.Environment.ObstacleSpecs.AddDefaulted_GetRef();
  Obstacle.ObstacleId = 7;
  Obstacle.Center = FVector(50.0, 60.0, 0.0);
  Obstacle.Extent = FVector(10.0, 20.0, 30.0);
  Source.LocalPredictiveSettings.TimeHorizonSeconds = 1.75f;
  Source.LocalPredictiveSettings.JointIterationCount = 37;
  FCrowdLocalPredictiveGrantState& Grant =
    Source.PreviousGrantStates.AddDefaulted_GetRef();
  Grant.ComponentKey = 123;
  Grant.GrantedAgentId = 10;
  Grant.GrantEpoch = 2;
  Grant.RemainingSteps = 4;
  FCrowdParticleConstraintAgent& External =
    Source.ExternalParticleAgents.AddDefaulted_GetRef();
  External.AgentId = -10;
  External.InteractionLayer = 2;
  External.StartPosition = FVector(-100.0, 5.0, 0.0);
  External.PredictedPosition = FVector(-95.0, 5.0, 0.0);
  External.PhysicalRadiusCm = 40.0f;
  External.HardSafetyGapCm = 8.0f;
  External.EnvironmentHardClearanceCm = 12.0f;
  External.SoftMarginCm = 6.0f;
  External.Mobility = 0.0f;
  FCrowdWorkerMovementControlEntry& First =
    Source.Entries.AddDefaulted_GetRef();
  First.EntityRef = {1, 10, 2};
  First.AgentId = 10;
  First.MaximumSpeedCmps = 325.0f;
  First.ParticleEnvironmentHardClearanceCm = 48.0f;
  First.ParticlePhysicalRadiusCm = 52.0f;
  First.ParticleHardSafetyGapCm = 12.0f;
  First.ParticleSoftMarginCm = 7.0f;
  First.ParticleMobility = 0.75f;
  First.AutonomousPreferredVelocity =
    FVector(100.0, 20.0, 0.0);
  First.bUseLocalVelocity = true;
  First.LocalVelocity = FVector(80.0, 10.0, 0.0);
  First.bLocalVelocityValid = true;
  First.bUseWorkerTargetGuidance = true;
  First.bUseAuthoritativePreferredVelocity = true;
  First.bFreezeAtBoundaryLocation = true;
  First.BoundaryLocation = FVector(10.0, 20.0, 30.0);
  First.bParticleActive = false;
  FCrowdWorkerMovementControlEntry& Second =
    Source.Entries.AddDefaulted_GetRef();
  Second.EntityRef = {1, 11, 3};
  Second.AgentId = 11;
  Second.MaximumSpeedCmps = 450.0f;
  Second.bVerticalOverride = true;
  Second.ProposedZ = 75.0f;
  Second.VerticalVelocityCmps = 42.0f;

  FCrowdWorkerPayload Payload;
  TestTrue(
    TEXT("movement control encodes"),
    FCrowdWorkerMovementControlResourceCodec::Encode(
      Source, Payload));
  FCrowdWorkerMovementControlResource Decoded;
  TestTrue(
    TEXT("movement control decodes"),
    FCrowdWorkerMovementControlResourceCodec::Decode(
      Payload, Decoded));
  TestEqual(TEXT("revision round trips"),
    Decoded.Revision, Source.Revision);
  TestEqual(TEXT("entry count round trips"),
    Decoded.Entries.Num(), Source.Entries.Num());
  TestEqual(TEXT("environment revision round trips"),
    Decoded.Environment.Revision, 4);
  TestTrue(TEXT("environment constraint flag round trips"),
    Decoded.bApplyEnvironmentMovementConstraint);
  TestTrue(TEXT("particle execution flag round trips"),
    Decoded.bRunParticleInteraction);
  TestTrue(TEXT("particle bounds flag round trips"),
    Decoded.bParticleConstrainToFlowBounds);
  TestEqual(TEXT("particle iteration count round trips"),
    Decoded.ParticleSettings.IterationCount, 6);
  TestTrue(TEXT("particle trace flag round trips"),
    Decoded.ParticleSettings.bCaptureRouteDiagnostic);
  TestEqual(TEXT("external particle count round trips"),
    Decoded.ExternalParticleAgents.Num(), 1);
  TestEqual(TEXT("obstacle count round trips"),
    Decoded.Environment.ObstacleSpecs.Num(), 1);
  TestEqual(TEXT("grant count round trips"),
    Decoded.PreviousGrantStates.Num(), 1);
  TestEqual(TEXT("planning iteration count round trips"),
    Decoded.LocalPredictiveSettings.JointIterationCount, 37);
  if (Decoded.Entries.Num() == 2)
  {
    TestTrue(TEXT("freeze flag round trips"),
      Decoded.Entries[0].bFreezeAtBoundaryLocation);
    TestTrue(TEXT("particle flag round trips"),
      !Decoded.Entries[0].bParticleActive);
    TestEqual(TEXT("particle clearance round trips"),
      Decoded.Entries[0].ParticleEnvironmentHardClearanceCm,
      48.0f);
    TestEqual(TEXT("particle radius round trips"),
      Decoded.Entries[0].ParticlePhysicalRadiusCm, 52.0f);
    TestEqual(TEXT("particle mobility round trips"),
      Decoded.Entries[0].ParticleMobility, 0.75f);
    TestTrue(TEXT("local velocity mode round trips"),
      Decoded.Entries[0].bUseLocalVelocity);
    TestTrue(TEXT("worker target mode round trips"),
      Decoded.Entries[0].bUseWorkerTargetGuidance);
    TestTrue(TEXT("authoritative preferred velocity round trips"),
      Decoded.Entries[0].bUseAuthoritativePreferredVelocity);
    TestTrue(TEXT("local velocity validity round trips"),
      Decoded.Entries[0].bLocalVelocityValid);
    TestTrue(TEXT("preferred velocity round trips"),
      Decoded.Entries[0].AutonomousPreferredVelocity.Equals(
        FVector(100.0, 20.0, 0.0)));
    TestTrue(TEXT("local velocity round trips"),
      Decoded.Entries[0].LocalVelocity.Equals(
        FVector(80.0, 10.0, 0.0)));
    TestTrue(TEXT("vertical flag round trips"),
      Decoded.Entries[1].bVerticalOverride);
    TestEqual(TEXT("vertical velocity round trips"),
      Decoded.Entries[1].VerticalVelocityCmps, 42.0f);
  }

  FCrowdWorkerPayload ProfilePayload;
  TestTrue(TEXT("movement profile encodes independently"),
    FCrowdWorkerMovementProfileCodec::Encode(
      Source.Entries[0], ProfilePayload));
  FCrowdWorkerMovementControlEntry DecodedProfile;
  TestTrue(TEXT("movement profile decodes independently"),
    FCrowdWorkerMovementProfileCodec::Decode(
      ProfilePayload, DecodedProfile));
  TestTrue(TEXT("movement profile entity round trips"),
    DecodedProfile.EntityRef == Source.Entries[0].EntityRef);
  TestEqual(TEXT("movement profile speed round trips"),
    DecodedProfile.MaximumSpeedCmps,
    Source.Entries[0].MaximumSpeedCmps);
  TestTrue(TEXT("movement profile target flag round trips"),
    DecodedProfile.bUseWorkerTargetGuidance);
  TestTrue(TEXT("movement profile velocity owner round trips"),
    DecodedProfile.bUseAuthoritativePreferredVelocity);
  FCrowdWorkerPayload CorruptProfile = ProfilePayload;
  CorruptProfile.Bytes[0] ^= 1;
  TestFalse(TEXT("movement profile rejects corrupt hash"),
    FCrowdWorkerMovementProfileCodec::Decode(
      CorruptProfile, DecodedProfile));

  FCrowdWorkerMovementControlResource SparseControl = Source;
  SparseControl.Entries.Reset();
  FCrowdWorkerPayload SparsePayload;
  TestTrue(TEXT("sparse movement control encodes without entries"),
    FCrowdWorkerMovementControlResourceCodec::Encode(
      SparseControl, SparsePayload));
  FCrowdWorkerMovementControlResource DecodedSparseControl;
  TestTrue(TEXT("sparse movement control decodes without entries"),
    FCrowdWorkerMovementControlResourceCodec::Decode(
      SparsePayload, DecodedSparseControl));
  TestEqual(TEXT("sparse movement control keeps entries empty"),
    DecodedSparseControl.Entries.Num(), 0);

  Swap(Source.Entries[0], Source.Entries[1]);
  TestTrue(
    TEXT("non-canonical entry order is rejected"),
    !FCrowdWorkerMovementControlResourceCodec::Encode(
      Source, Payload));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2MovementPlanningParityTest,
  "MassCrowd.RuntimeV2.MovementPlanningParity",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2MovementPlanningParityTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 3;
  constexpr uint64 InputSequence = 7;
  constexpr uint64 Epoch = 11;
  constexpr double FixedDeltaSeconds = 1.0 / 30.0;
  const FCrowdStableEntityRef A{1, 10, 1};
  const FCrowdStableEntityRef B{1, 11, 1};

  auto MakeState = [](
    const FCrowdStableEntityRef& EntityRef,
    const int32 AgentId,
    const FVector& Position,
    const FVector& Velocity)
  {
    FCrowdMassBoundaryAgentRecord Record;
    Record.Identity.AgentId = AgentId;
    Record.Identity.SetStableEntityRef(EntityRef);
    Record.AgentFacts.StableEntityRef = EntityRef;
    Record.AgentFacts.CapabilitySet.Bits = 1;
    Record.State.Position = Position;
    Record.State.Velocity = Velocity;
    Record.State.PlanRevision = 3;
    Record.State.bInitialized = true;
    Record.Properties.PhysicalRadiusCm = 42.0f;
    Record.Properties.HardSafetyGapCm = 10.0f;
    Record.Properties.MaximumSpeedCmps = 300.0f;
    FCrowdWorkerPayload Payload;
    FCrowdWorkerBoundaryStateCodec::EncodeState(Record, Payload);
    return Payload;
  };

  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("entity state store resets"),
    States.Reset(16, 4096));
  TestEqual(TEXT("A spawns"),
    States.Spawn(A, Generation, InputSequence,
      MakeState(A, 10, FVector(0.0, 0.0, 0.0),
        FVector::ZeroVector)),
    ECrowdWorkerQueueResult::Added);
  TestEqual(TEXT("B spawns"),
    States.Spawn(B, Generation, InputSequence,
      MakeState(B, 11, FVector(100.0, 0.0, 0.0),
        FVector::ZeroVector)),
    ECrowdWorkerQueueResult::Added);

  const auto ApplyBehavior = [this, &States,
    Generation, InputSequence, Epoch](
    const FCrowdStableEntityRef& EntityRef,
    const FVector& DesiredVelocity,
    const float SpeedLimitCmps,
    const bool bMovementLocked,
    const bool bHasMovementGoal)
  {
    FCrowdWorkerBehaviorState Behavior;
    Behavior.LastFixedStep = 4;
    Behavior.LastAbsoluteSimulationTick = Epoch;
    Behavior.BusinessCommitLedgerHash = 14695981039346656037ull;
    Behavior.EvaluationContext.EntityRef = EntityRef;
    Behavior.EvaluationContext.FixedStepIndex = 4;
    Behavior.EvaluationContext.Facing = FVector::ForwardVector;
    Behavior.EvaluationContext.RecalculateStableHash();
    Behavior.SourceSet.EntityRef = EntityRef;
    Behavior.SourceSet.CapabilityBinding.ProfileKey.Value = 1;
    Behavior.SourceSet.Revision = 1;
    Behavior.SourceSet.RecalculateStableHash();
    FCrowdBehaviorContributions Contributions;
    FCrowdMovementContribution& Movement =
      Contributions.Movement.AddDefaulted_GetRef();
    Movement.Key = {1, {1}, {1}, 1};
    Movement.DesiredVelocity = DesiredVelocity;
    if (bHasMovementGoal)
    {
      Movement.Goal.Location = FVector(500.0, 0.0, 0.0);
      Movement.Goal.FactRevision = 1;
      Movement.Goal.bHasGoal = true;
    }
    FCrowdConstraintContribution& Constraint =
      Contributions.Constraints.AddDefaulted_GetRef();
    Constraint.Key = {1, {1}, {1}, 1};
    Constraint.SpeedLimitCmps = SpeedLimitCmps;
    Constraint.bLockMovement = bMovementLocked;
    if (!FCrowdBehaviorResolver::Resolve(
        Contributions, Behavior.ResolvedChannels))
      return false;
    FCrowdWorkerDirtyStateRecord Dirty;
    Dirty.EntityRef = EntityRef;
    Dirty.Field = ECrowdWorkerField::Behavior;
    Dirty.Generation = Generation;
    Dirty.WorkerEpoch = Epoch;
    Dirty.StateRevision = Epoch;
    Dirty.SourceInputSequence = InputSequence;
    if (!FCrowdWorkerBehaviorStateCodec::Encode(
        Behavior, Dirty.Payload))
      return false;
    return States.ApplyDirty(Dirty)
      == ECrowdWorkerQueueResult::Replaced;
  };
  TestTrue(TEXT("A worker behavior locks movement"),
    ApplyBehavior(
      A, FVector(250.0, 0.0, 0.0), 300.0f, true, false));
  TestTrue(TEXT("B worker behavior limits goal movement"),
    ApplyBehavior(
      B, FVector(-180.0, 0.0, 0.0), 50.0f, false, true));

  FCrowdWorkerCombatState IdleCombat;
  IdleCombat.SourceFixedStep = 4;
  IdleCombat.bAlive = true;
  FCrowdWorkerDirtyStateRecord IdleCombatDirty;
  IdleCombatDirty.EntityRef = A;
  IdleCombatDirty.Field = ECrowdWorkerField::Combat;
  IdleCombatDirty.Generation = Generation;
  IdleCombatDirty.WorkerEpoch = Epoch;
  IdleCombatDirty.StateRevision = Epoch;
  IdleCombatDirty.SourceInputSequence = InputSequence;
  TestTrue(TEXT("idle combat state encodes"),
    FCrowdWorkerCombatStateCodec::Encode(
      IdleCombat, IdleCombatDirty.Payload));
  TestEqual(TEXT("idle combat state applies"),
    States.ApplyDirty(IdleCombatDirty),
    ECrowdWorkerQueueResult::Replaced);

  FCrowdWorkerMovementControlResource Control;
  Control.Revision = 5;
  Control.FixedStepIndex = 4;
  Control.PlanRevision = 3;
  Control.bRunLocalPredictive = true;
  Control.LocalPredictiveSettings.FixedStepSeconds =
    FixedDeltaSeconds;
  FCrowdWorkerMovementControlEntry& EntryA =
    Control.Entries.AddDefaulted_GetRef();
  EntryA.EntityRef = A;
  EntryA.AgentId = 10;
  EntryA.MaximumSpeedCmps = 300.0f;
  EntryA.AutonomousPreferredVelocity =
    FVector(200.0, 0.0, 0.0);
  EntryA.bUseLocalVelocity = true;
  EntryA.bLocalVelocityValid = true;
  EntryA.LocalVelocity = FVector(-299.0, 0.0, 0.0);
  FCrowdWorkerMovementControlEntry& EntryB =
    Control.Entries.AddDefaulted_GetRef();
  EntryB.EntityRef = B;
  EntryB.AgentId = 11;
  EntryB.MaximumSpeedCmps = 300.0f;
  EntryB.AutonomousPreferredVelocity =
    FVector(-200.0, 0.0, 0.0);
  EntryB.bUseLocalVelocity = true;
  EntryB.bLocalVelocityValid = true;
  EntryB.LocalVelocity = FVector(299.0, 0.0, 0.0);

  FCrowdWorkerPayload ControlPayload;
  TestTrue(TEXT("control resource encodes"),
    FCrowdWorkerMovementControlResourceCodec::Encode(
      Control, ControlPayload));
  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("resource store resets"), Resources.Reset(65536));
  TestEqual(TEXT("control resource stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::MovementControl,
      Control.Revision,
      MoveTemp(ControlPayload)}),
    ECrowdWorkerQueueResult::Added);
  TArray<FCrowdWorkerResourceRevisionEvent> ResourceEvents;
  TestTrue(TEXT("control resource commits"),
    Resources.CommitBuildingAtEpoch(Epoch, ResourceEvents));

  FCrowdWorkerDomainContext Context;
  Context.Generation = Generation;
  Context.WorkerEpoch = Epoch;
  Context.AbsoluteSimulationTick = Epoch;
  Context.LastAppliedInputSequence = InputSequence;
  Context.FixedDeltaSeconds = FixedDeltaSeconds;
  Context.SimulationTimeSeconds = FixedDeltaSeconds;
  Context.RuntimeMode = ECrowdWorkerRuntimeV2Mode::Production;
  Context.EntityStates = &States;
  Context.Resources = &Resources;

  FCrowdWorkerWorkItem PlanningWork;
  PlanningWork.Key.Domain =
    ECrowdWorkerDomainId::MovementPlanning;
  PlanningWork.Key.Kind = ECrowdWorkerWorkKind::Resource;
  PlanningWork.Key.ScopeKey =
    CrowdWorkerResourceIds::MovementControl;
  FCrowdWorkerWorkItem SupersededEntityPlanningWork;
  SupersededEntityPlanningWork.Key.Domain =
    ECrowdWorkerDomainId::MovementPlanning;
  SupersededEntityPlanningWork.Key.Kind =
    ECrowdWorkerWorkKind::Resource;
  SupersededEntityPlanningWork.Key.PrimaryEntity = A;
  SupersededEntityPlanningWork.Key.ScopeKey =
    CrowdWorkerResourceIds::MovementControl;
  TArray<FCrowdWorkerWorkItem> PlanningWorkItems{
    PlanningWork, SupersededEntityPlanningWork};
  FCrowdWorkerMovementPlanningDomainExecutor Planning;
  FCrowdWorkerDomainOutput PlanningOutput;
  TestTrue(TEXT("resource replan supersedes anchored continuation"),
    Planning.Execute(
      Context, PlanningWorkItems, PlanningOutput));
  TestEqual(TEXT("one plan per entity"),
    PlanningOutput.DirtyStates.Num(), 2);
  TestEqual(TEXT("one movement wake per entity"),
    PlanningOutput.NextWork.Num(), 2);
  TestEqual(TEXT("planning owns one next-tick continuation"),
    PlanningOutput.Wakeups.Num(), 1);
  if (PlanningOutput.Wakeups.Num() == 1)
  {
    TestEqual(TEXT("planning continuation domain"),
      PlanningOutput.Wakeups[0].Key.Domain,
      ECrowdWorkerDomainId::MovementPlanning);
    TestEqual(TEXT("planning continuation tick"),
      PlanningOutput.Wakeups[0].AbsoluteSimulationTick,
      Epoch + 1);
  }
  for (const FCrowdWorkerDirtyStateRecord& Plan :
    PlanningOutput.DirtyStates)
  {
    TestEqual(TEXT("planning writes only plan field"),
      Plan.Field, ECrowdWorkerField::MovementPlan);
    TestTrue(TEXT("plan applies"),
      States.ApplyDirty(Plan)
        == ECrowdWorkerQueueResult::Replaced);
  }

  FCrowdWorkerMovementDomainExecutor Movement;
  FCrowdWorkerDomainOutput MovementOutput;
  TestTrue(TEXT("worker movement executes from plans"),
    Movement.Execute(
      Context, PlanningOutput.NextWork, MovementOutput));
  TestEqual(TEXT("one movement state per entity"),
    MovementOutput.DirtyStates.Num(), 2);
  for (const FCrowdWorkerDirtyStateRecord& Dirty :
    MovementOutput.DirtyStates)
  {
    FCrowdWorkerMovementState State;
    TestTrue(TEXT("movement state decodes"),
      FCrowdWorkerMovementStateCodec::Decode(
        Dirty.Payload, State));
    const FVector InitialPosition =
      Dirty.EntityRef == A
        ? FVector(0.0, 0.0, 0.0)
        : FVector(100.0, 0.0, 0.0);
    TestTrue(TEXT("worker position integrates planned velocity"),
      State.Position.Equals(
        InitialPosition
          + State.Velocity * FixedDeltaSeconds,
        0.0));
    TestTrue(TEXT("legacy velocity was not consumed"),
      !State.Velocity.Equals(
        Dirty.EntityRef == A
          ? FVector(-299.0, 0.0, 0.0)
          : FVector(299.0, 0.0, 0.0),
        0.0));
    if (Dirty.EntityRef == A)
      TestTrue(TEXT("idle combat preserves worker behavior lock"),
        State.Velocity.IsNearlyZero(0.0));
    else
    {
      TestTrue(TEXT("worker behavior speed limit reaches movement"),
        State.Velocity.Size2D() <= 50.0f);
      TestTrue(TEXT("goal movement falls back to behavior direction without flow or target"),
        State.Velocity.X < 0.0f);
    }
    TestTrue(TEXT("first-tick movement applies"),
      States.ApplyDirty(Dirty)
        == ECrowdWorkerQueueResult::Replaced);
  }

  // A normal intent advances only the ordered clock/input waterline. The
  // immutable MovementControl profile remains at revision 5; planning must
  // consume its checkpointable prior plan state and continue autonomously.
  Context.WorkerEpoch = Epoch + 1;
  Context.AbsoluteSimulationTick = Epoch + 1;
  Context.LastAppliedInputSequence = InputSequence + 1;
  Context.SimulationTimeSeconds = FixedDeltaSeconds * 2.0;
  Context.RuntimeMode = ECrowdWorkerRuntimeV2Mode::Shadow;
  FCrowdWorkerDomainOutput SecondPlanningOutput;
  TestTrue(TEXT("clock-only Shadow planning continues from Worker state"),
    Planning.Execute(
      Context, PlanningWorkItems, SecondPlanningOutput));
  TestEqual(TEXT("clock-only tick emits every plan"),
    SecondPlanningOutput.DirtyStates.Num(), 2);
  TestEqual(TEXT("clock-only tick emits every movement work item"),
    SecondPlanningOutput.NextWork.Num(), 2);
  TestEqual(TEXT("clock-only tick retains one planning continuation"),
    SecondPlanningOutput.Wakeups.Num(), 1);
  for (const FCrowdWorkerDirtyStateRecord& Plan :
    SecondPlanningOutput.DirtyStates)
  {
    TestEqual(TEXT("clock-only plan uses new input waterline"),
      Plan.SourceInputSequence, InputSequence + 1);
    TestTrue(TEXT("clock-only plan applies"),
      States.ApplyDirty(Plan)
        == ECrowdWorkerQueueResult::Replaced);
  }
  FCrowdWorkerDomainOutput SecondMovementOutput;
  TestTrue(TEXT("clock-only second movement tick executes"),
    Movement.Execute(
      Context, SecondPlanningOutput.NextWork,
      SecondMovementOutput));
  TestEqual(TEXT("clock-only tick emits every movement state"),
    SecondMovementOutput.DirtyStates.Num(), 2);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2TargetDomainTest,
  "MassCrowd.RuntimeV2.TargetControlRoundTripAndDomain",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2TargetDomainTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 21;
  constexpr uint64 InputSequence = 8;
  FCrowdWorkerTargetControlResource Control;
  Control.Revision = 3;
  FCrowdWorkerTargetCohortInput& Cohort =
    Control.Cohorts.AddDefaulted_GetRef();
  Cohort.CohortKey = 0;
  Cohort.TopologyRevision = 91;
  Cohort.TargetRevision = 91;
  Cohort.FixedStepIndex = 0;
  Cohort.FlowConfig.Revision = 91;
  Cohort.FlowConfig.BoundsMin =
    FVector(-2000.0, -2000.0, 0.0);
  Cohort.FlowConfig.BoundsMax =
    FVector(2000.0, 2000.0, 0.0);
  Cohort.FlowConfig.CellSizeCm = 100.0f;
  Cohort.FlowConfig.AgentInflateCm = 48.0f;
  Cohort.FlowConfig.GoalLocation = FVector::ZeroVector;
  Cohort.Settings.MinimumCenterDistanceCm = 152.0f;
  Cohort.Settings.MaximumCenterDistanceCm = 850.0f;
  Cohort.Settings.InfluenceBlendWidthCm = 300.0f;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    const FCrowdStableEntityRef EntityRef{
      1, static_cast<uint64>(Index + 1), 1};
    FCrowdWorkerTargetAgentInput& Input =
      Cohort.Agents.AddDefaulted_GetRef();
    Input.EntityRef = EntityRef;
    Input.Agent.AgentId = Index + 1;
    const float Angle =
      0.5f * 2.0f * PI / 16.0f;
    Input.Agent.Location =
      FVector2f(FMath::Cos(Angle), FMath::Sin(Angle))
      * (1000.0f + static_cast<float>(Index));
    Input.Agent.FarFlowPreferredVelocity =
      -Input.Agent.Location.GetSafeNormal() * 600.0f;
    Input.Agent.MaxSpeedCmps = 800.0f;
  }
  FCrowdWorkerPayload Payload;
  TestTrue(TEXT("target control encodes"),
    FCrowdWorkerTargetControlResourceCodec::Encode(
      Control, Payload));
  FCrowdWorkerTargetControlResource Decoded;
  TestTrue(TEXT("target control decodes"),
    FCrowdWorkerTargetControlResourceCodec::Decode(
      Payload, Decoded));
  TestEqual(TEXT("default cohort key survives"),
    Decoded.Cohorts[0].CohortKey, uint32{0});
  TestEqual(TEXT("target agents survive"),
    Decoded.Cohorts[0].Agents.Num(), 20);
  FCrowdWorkerTargetObjectiveRevision Objective;
  Objective.TargetRevision = Cohort.TargetRevision;
  Objective.EffectiveFixedStepIndex = 1;
  Objective.TargetLocation = Cohort.Settings.TargetLocation;
  Objective.TargetVelocity = Cohort.Settings.TargetVelocity;
  FCrowdWorkerPayload ObjectivePayload;
  TestTrue(TEXT("target objective encodes"),
    FCrowdWorkerTargetObjectiveRevisionCodec::Encode(
      Objective, ObjectivePayload));
  FCrowdWorkerTargetObjectiveRevision DecodedObjective;
  TestTrue(TEXT("target objective decodes"),
    FCrowdWorkerTargetObjectiveRevisionCodec::Decode(
      ObjectivePayload, DecodedObjective));
  TestEqual(TEXT("target objective revision survives"),
    DecodedObjective.TargetRevision, Objective.TargetRevision);
  TestEqual(TEXT("target objective tick survives"),
    DecodedObjective.EffectiveFixedStepIndex,
    Objective.EffectiveFixedStepIndex);

  FCrowdTargetPolarTopology AbsoluteTopology;
  AbsoluteTopology.bValid = true;
  AbsoluteTopology.TopologyHash = 101;
  AbsoluteTopology.FeasibleGraphHash = 202;
  FCrowdTargetPolarCell& AbsoluteCell =
    AbsoluteTopology.Cells.AddDefaulted_GetRef();
  AbsoluteCell.StableCellKey = 7;
  AbsoluteCell.WorldAnchorCm = FVector2f(10.25f, -20.5f);
  AbsoluteCell.bFeasible = true;
  const uint32 InitialTopologyRevision =
    FCrowdWorkerTargetControlResourceCodec::
      CalculateTopologyRevision(AbsoluteTopology);
  TestTrue(TEXT("absolute topology revision is valid"),
    InitialTopologyRevision != 0);
  TestEqual(TEXT("unchanged absolute topology is stable"),
    FCrowdWorkerTargetControlResourceCodec::
      CalculateTopologyRevision(AbsoluteTopology),
    InitialTopologyRevision);
  AbsoluteCell.WorldAnchorCm.X += 0.25f;
  TestNotEqual(TEXT("translated absolute topology changes revision"),
    FCrowdWorkerTargetControlResourceCodec::
      CalculateTopologyRevision(AbsoluteTopology),
    InitialTopologyRevision);

  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("target state store resets"),
    States.Reset(32, 4096));
  for (const FCrowdWorkerTargetAgentInput& Agent :
    Cohort.Agents)
  {
    TestEqual(TEXT("target entity spawns"),
      States.Spawn(
        Agent.EntityRef, Generation, InputSequence,
        MakeBoundaryStatePayload(
          Agent.EntityRef,
          Agent.Agent.AgentId,
          FVector(
            Agent.Agent.Location.X,
            Agent.Agent.Location.Y, 0.0))),
      ECrowdWorkerQueueResult::Added);
  }
  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("target resource store resets"),
    Resources.Reset(4 * 1024 * 1024));
  FCrowdSharedFlowField TargetFlow;
  TestTrue(TEXT("target flow builds"),
    FCrowdSharedFlowFieldKernel::Build(
      Cohort.FlowConfig, TargetFlow));
  FCrowdWorkerPayload TargetFlowPayload;
  TestTrue(TEXT("target flow encodes"),
    FCrowdWorkerFlowFieldResourceCodec::Encode(
      TargetFlow, TargetFlowPayload));
  TestEqual(TEXT("target flow resource stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::Environment,
      static_cast<uint64>(Cohort.FlowConfig.Revision),
      MoveTemp(TargetFlowPayload)}),
    ECrowdWorkerQueueResult::Added);
  TestEqual(TEXT("target resource stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::TargetControl,
      Control.Revision, MoveTemp(Payload)}),
    ECrowdWorkerQueueResult::Added);
  TestEqual(TEXT("target objective resource stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::ObjectiveRevision(
        CrowdWorkerTargetObjectiveIds::PrimaryTarget),
      1, MoveTemp(ObjectivePayload)}),
    ECrowdWorkerQueueResult::Added);
  TArray<FCrowdWorkerResourceRevisionEvent> Events;
  TestTrue(TEXT("target resource commits"),
    Resources.CommitBuildingAtEpoch(1, Events));

  FCrowdWorkerDomainContext Context;
  Context.Generation = Generation;
  Context.WorkerEpoch = 1;
  Context.AbsoluteSimulationTick = 1;
  Context.LastAppliedInputSequence = InputSequence;
  Context.FixedDeltaSeconds = 1.0 / 30.0;
  Context.SimulationTimeSeconds = 1.0 / 30.0;
  Context.RuntimeMode = ECrowdWorkerRuntimeV2Mode::Production;
  Context.EntityStates = &States;
  Context.Resources = &Resources;
  FCrowdWorkerWorkItem TargetWork;
  TargetWork.Key.Domain = ECrowdWorkerDomainId::Target;
  TargetWork.Key.Kind = ECrowdWorkerWorkKind::Resource;
  TargetWork.Key.ScopeKey =
    CrowdWorkerResourceIds::TargetControl;
  TArray<FCrowdWorkerWorkItem> Work{TargetWork};
  FCrowdWorkerTargetDomainExecutor Executor;
  FCrowdWorkerDomainOutput Output;
  TestTrue(TEXT("target domain executes"),
    Executor.Execute(Context, Work, Output));
  TestEqual(TEXT("member patches plus replicated cohort state"),
    Output.DirtyStates.Num(), 40);
  TestEqual(TEXT("target dependency declared"),
    Output.DeclaredDependencies.Num(), 23);
  TestEqual(TEXT("target observes each member facing dependency"),
    Output.ObservedDependencies.Num(), 23);
  for (const FCrowdWorkerDirtyStateRecord& Dirty :
    Output.DirtyStates)
  {
    if (Dirty.Field == ECrowdWorkerField::TargetCohort)
    {
      FCrowdWorkerTargetCohortState CohortState;
      TestTrue(TEXT("target cohort state decodes"),
        FCrowdWorkerTargetCohortStateCodec::Decode(
          Dirty.Payload, CohortState));
      TestEqual(TEXT("target cohort key"),
        CohortState.CohortKey, uint32{0});
      TestTrue(TEXT("target cohort state applies"),
        States.ApplyDirty(Dirty)
          == ECrowdWorkerQueueResult::Replaced);
      continue;
    }
    FCrowdWorkerTargetState State;
    TestTrue(TEXT("target state decodes"),
      FCrowdWorkerTargetStateCodec::Decode(
        Dirty.Payload, State));
    TestEqual(TEXT("target state cohort"),
      State.CohortKey, uint32{0});
    TestTrue(TEXT("target state applies"),
      States.ApplyDirty(Dirty)
        == ECrowdWorkerQueueResult::Replaced);
  }
  FCrowdWorkerDomainOutput DuplicateOutput;
  TestTrue(TEXT("same target work is deterministic"),
    Executor.Execute(Context, Work, DuplicateOutput));
  TestEqual(TEXT("unchanged target output is not republished"),
    DuplicateOutput.DirtyStates.Num(), 0);
  FCrowdWorkerTargetDomainExecutor RestoredExecutor;
  FCrowdWorkerDomainOutput RestoredOutput;
  TestTrue(TEXT("fresh target executor restores from worker store"),
    RestoredExecutor.Execute(Context, Work, RestoredOutput));
  TestEqual(TEXT("restored target executor does not republish"),
    RestoredOutput.DirtyStates.Num(), 0);
  const FCrowdWorkerTargetDomainMetrics Metrics =
    Executor.GetMetrics();
  TestEqual(TEXT("topology builds once"),
    Metrics.TopologyBuildCount, uint64{1});
  TestEqual(TEXT("initial plan builds once"),
    Metrics.PlanBuildCount, uint64{1});
  TestEqual(TEXT("unchanged plan uses cache"),
    Metrics.PlanCacheHitCount, uint64{1});
  TestEqual(TEXT("only changed target states publish"),
    Metrics.PublishedPatchCount, uint64{20});

  Objective.TargetRevision = 92;
  Objective.EffectiveFixedStepIndex = 3;
  Objective.TargetLocation = FVector2f(100.0f, 0.0f);
  Objective.TargetVelocity = FVector2f(30.0f, 0.0f);
  TestTrue(TEXT("revised target objective encodes"),
    FCrowdWorkerTargetObjectiveRevisionCodec::Encode(
      Objective, ObjectivePayload));
  TestEqual(TEXT("revised target objective stages without control"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::ObjectiveRevision(
        CrowdWorkerTargetObjectiveIds::PrimaryTarget),
      2, MoveTemp(ObjectivePayload)}),
    ECrowdWorkerQueueResult::Added);
  Events.Reset();
  TestTrue(TEXT("revised target objective commits"),
    Resources.CommitBuildingAtEpoch(2, Events));
  Context.WorkerEpoch = 2;
  Context.AbsoluteSimulationTick = 2;
  Context.SimulationTimeSeconds = 2.0 / 30.0;
  FCrowdWorkerDomainOutput DeferredObjectiveOutput;
  TestTrue(TEXT("future target objective defers without failure"),
    Executor.Execute(Context, Work, DeferredObjectiveOutput));
  TestEqual(TEXT("future target objective publishes no early patch"),
    DeferredObjectiveOutput.DirtyStates.Num(), 0);
  TestEqual(TEXT("future target objective schedules one cohort"),
    DeferredObjectiveOutput.Wakeups.Num(), 1);
  if (DeferredObjectiveOutput.Wakeups.Num() == 1)
  {
    TestEqual(TEXT("future target objective wakeup tick"),
      DeferredObjectiveOutput.Wakeups[0].AbsoluteSimulationTick,
      uint64{3});
    TestEqual(TEXT("future target objective wakeup domain"),
      DeferredObjectiveOutput.Wakeups[0].Key.Domain,
      ECrowdWorkerDomainId::Target);
    TestEqual(TEXT("future target objective wakeup cohort"),
      DeferredObjectiveOutput.Wakeups[0].Key.WakeupId,
      CrowdWorkerTargetWorkScopes::EncodeCohortKey(0));
  }
  Context.WorkerEpoch = 3;
  Context.AbsoluteSimulationTick = 3;
  Context.SimulationTimeSeconds = 3.0 / 30.0;
  FCrowdWorkerDomainOutput ObjectiveOutput;
  TestTrue(TEXT("objective-only target clock executes"),
    Executor.Execute(Context, Work, ObjectiveOutput));
  TestTrue(TEXT("objective-only target clock publishes patches"),
    !ObjectiveOutput.DirtyStates.IsEmpty());
  TestEqual(TEXT("objective revision rebuilds topology"),
    Executor.GetMetrics().TopologyBuildCount, uint64{2});
  for (const FCrowdWorkerDirtyStateRecord& Dirty :
    ObjectiveOutput.DirtyStates)
    TestTrue(TEXT("objective target patch applies"),
      States.ApplyDirty(Dirty)
        == ECrowdWorkerQueueResult::Replaced);

  const FCrowdStableEntityRef RemovedMember =
    Control.Cohorts[0].Agents[0].EntityRef;
  TestTrue(TEXT("former cohort anchor despawns"),
    States.Despawn(RemovedMember));
  FCrowdWorkerTargetControlResource ReducedControl = Control;
  ReducedControl.Revision = 4;
  ReducedControl.Cohorts[0].Agents.RemoveAt(0);
  FCrowdWorkerPayload ReducedPayload;
  TestTrue(TEXT("reduced target control encodes"),
    FCrowdWorkerTargetControlResourceCodec::Encode(
      ReducedControl, ReducedPayload));
  TestEqual(TEXT("reduced target resource stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::TargetControl,
      ReducedControl.Revision, MoveTemp(ReducedPayload)}),
    ECrowdWorkerQueueResult::Added);
  Events.Reset();
  TestTrue(TEXT("reduced target resource commits"),
    Resources.CommitBuildingAtEpoch(4, Events));
  Context.WorkerEpoch = 4;
  Context.AbsoluteSimulationTick = 4;
  Context.SimulationTimeSeconds = 4.0 / 30.0;
  FCrowdWorkerTargetDomainExecutor SurvivorExecutor;
  FCrowdWorkerDomainOutput SurvivorOutput;
  TestTrue(TEXT("surviving member restores cohort after anchor despawn"),
    SurvivorExecutor.Execute(Context, Work, SurvivorOutput));
  int32 SurvivorCohortPatchCount = 0;
  for (const FCrowdWorkerDirtyStateRecord& Dirty :
    SurvivorOutput.DirtyStates)
  {
    TestTrue(TEXT("despawned member receives no target output"),
      Dirty.EntityRef != RemovedMember);
    SurvivorCohortPatchCount +=
      Dirty.Field == ECrowdWorkerField::TargetCohort ? 1 : 0;
  }
  TestEqual(TEXT("cohort state replicates to every survivor"),
    SurvivorCohortPatchCount, 19);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2TargetAffectedCohort10kTest,
  "MassCrowd.RuntimeV2.Complexity.TargetAffectedCohort10k",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2TargetAffectedCohort10kTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 41;
  constexpr uint64 InputSequence = 1;
  constexpr int32 TotalAgentCount = 10000;
  constexpr int32 AgentsPerCohort = TotalAgentCount / 2;
  constexpr int32 GuidanceShardSize = 128;
  FCrowdWorkerTargetControlResource Control;
  Control.Revision = 1;
  for (uint32 CohortIndex = 0; CohortIndex < 2; ++CohortIndex)
  {
    FCrowdWorkerTargetCohortInput& Cohort =
      Control.Cohorts.AddDefaulted_GetRef();
    Cohort.CohortKey = CohortIndex;
    Cohort.TopologyRevision = 101 + CohortIndex;
    Cohort.TargetRevision = 1;
    Cohort.FixedStepIndex = 0;
    Cohort.FlowConfig.Revision = 101;
    Cohort.FlowConfig.BoundsMin =
      FVector(-2000.0, -2000.0, 0.0);
    Cohort.FlowConfig.BoundsMax =
      FVector(2000.0, 2000.0, 0.0);
    Cohort.FlowConfig.CellSizeCm = 100.0f;
    Cohort.FlowConfig.AgentInflateCm = 48.0f;
    Cohort.FlowConfig.GoalLocation = FVector::ZeroVector;
    Cohort.Settings.MinimumCenterDistanceCm = 152.0f;
    Cohort.Settings.MaximumCenterDistanceCm = 852.0f;
    Cohort.Settings.InfluenceBlendWidthCm = 300.0f;
    Cohort.Settings.RadialBandWidthCm = 2.0f;
    Cohort.Agents.Reserve(AgentsPerCohort);
    for (int32 LocalIndex = 0;
      LocalIndex < AgentsPerCohort; ++LocalIndex)
    {
      const int32 GlobalIndex =
        static_cast<int32>(CohortIndex) * AgentsPerCohort
        + LocalIndex;
      FCrowdWorkerTargetAgentInput& Input =
        Cohort.Agents.AddDefaulted_GetRef();
      Input.EntityRef = {
        1, static_cast<uint64>(GlobalIndex + 1), 1};
      Input.Agent.AgentId = GlobalIndex + 1;
      const float Angle =
        (static_cast<float>(LocalIndex % 64) + 0.5f)
        * 2.0f * PI / 64.0f;
      const float Radius =
        500.0f + static_cast<float>((LocalIndex / 64) % 20);
      Input.Agent.Location =
        FVector2f(FMath::Cos(Angle), FMath::Sin(Angle))
        * Radius;
      Input.Agent.FarFlowPreferredVelocity =
        -Input.Agent.Location.GetSafeNormal() * 600.0f;
      Input.Agent.MaxSpeedCmps = 800.0f;
    }
  }

  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("10k target state store resets"),
    States.Reset(TotalAgentCount, 128 * 1024 * 1024));
  for (const FCrowdWorkerTargetCohortInput& Cohort :
    Control.Cohorts)
  {
    for (const FCrowdWorkerTargetAgentInput& Agent :
      Cohort.Agents)
    {
      const FVector Position(
        Agent.Agent.Location.X, Agent.Agent.Location.Y, 0.0f);
      if (States.Spawn(
          Agent.EntityRef, Generation, InputSequence,
          MakeBoundaryStatePayload(
            Agent.EntityRef, Agent.Agent.AgentId, Position))
        != ECrowdWorkerQueueResult::Added)
      {
        AddError(TEXT("10k target entity spawn failed"));
        return false;
      }
    }
  }

  FCrowdSharedFlowField TargetFlow;
  TestTrue(TEXT("10k target flow builds"),
    FCrowdSharedFlowFieldKernel::Build(
      Control.Cohorts[0].FlowConfig, TargetFlow));
  FCrowdWorkerPayload FlowPayload;
  TestTrue(TEXT("10k target flow encodes"),
    FCrowdWorkerFlowFieldResourceCodec::Encode(
      TargetFlow, FlowPayload));
  FCrowdWorkerPayload ControlPayload;
  TestTrue(TEXT("10k target control encodes"),
    FCrowdWorkerTargetControlResourceCodec::Encode(
      Control, ControlPayload));
  FCrowdWorkerTargetObjectiveRevision Objective;
  Objective.TargetRevision = 1;
  Objective.EffectiveFixedStepIndex = 0;
  FCrowdWorkerPayload ObjectivePayload;
  TestTrue(TEXT("10k target objective encodes"),
    FCrowdWorkerTargetObjectiveRevisionCodec::Encode(
      Objective, ObjectivePayload));
  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("10k target resources reset"),
    Resources.Reset(64 * 1024 * 1024));
  TestEqual(TEXT("10k target flow stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::Environment,
      static_cast<uint64>(Control.Cohorts[0].FlowConfig.Revision),
      MoveTemp(FlowPayload)}),
    ECrowdWorkerQueueResult::Added);
  TestEqual(TEXT("10k target control stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::TargetControl,
      Control.Revision, MoveTemp(ControlPayload)}),
    ECrowdWorkerQueueResult::Added);
  TestEqual(TEXT("10k target objective stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::ObjectiveRevision(
        CrowdWorkerTargetObjectiveIds::PrimaryTarget),
      1, MoveTemp(ObjectivePayload)}),
    ECrowdWorkerQueueResult::Added);
  TArray<FCrowdWorkerResourceRevisionEvent> ResourceEvents;
  TestTrue(TEXT("10k target resources commit"),
    Resources.CommitBuildingAtEpoch(1, ResourceEvents));

  FCrowdWorkerDomainContext Context;
  Context.Generation = Generation;
  Context.WorkerEpoch = 1;
  Context.AbsoluteSimulationTick = 1;
  Context.LastAppliedInputSequence = InputSequence;
  Context.FixedDeltaSeconds = 1.0 / 30.0;
  Context.SimulationTimeSeconds = 1.0 / 30.0;
  Context.RuntimeMode = ECrowdWorkerRuntimeV2Mode::Production;
  Context.EntityStates = &States;
  Context.Resources = &Resources;
  FCrowdWorkerWorkItem FullWork;
  FullWork.Key.Domain = ECrowdWorkerDomainId::Target;
  FullWork.Key.Kind = ECrowdWorkerWorkKind::Resource;
  FullWork.Key.ScopeKey = CrowdWorkerResourceIds::TargetControl;
  FCrowdWorkerTargetDomainExecutor Executor;
  FCrowdWorkerDomainOutput Baseline;
  TestTrue(TEXT("10k target baseline executes"),
    Executor.Execute(Context, {FullWork}, Baseline));
  TestEqual(TEXT("10k target baseline dirty count"),
    Baseline.DirtyStates.Num(), TotalAgentCount * 2);
  for (const FCrowdWorkerDirtyStateRecord& Dirty :
    Baseline.DirtyStates)
  {
    if (States.ApplyDirty(Dirty)
      != ECrowdWorkerQueueResult::Replaced)
    {
      AddError(TEXT("10k target baseline patch failed"));
      return false;
    }
  }
  const FCrowdWorkerTargetDomainMetrics BaselineMetrics =
    Executor.GetMetrics();
  TestEqual(TEXT("10k target baseline shard count"),
    BaselineMetrics.GuidanceShardCount,
    static_cast<uint64>(2 * FMath::DivideAndRoundUp(
      AgentsPerCohort, GuidanceShardSize)));
  TestEqual(TEXT("10k target max shard is bounded"),
    BaselineMetrics.GuidanceMaxShardSize, GuidanceShardSize);

  const FCrowdStableEntityRef ChangedEntity =
    Control.Cohorts[1].Agents[0].EntityRef;
  FCrowdWorkerMovementState ChangedMovement;
  ChangedMovement.StartPosition = FVector(500.0, 0.0, 0.0);
  ChangedMovement.Position = FVector(540.0, 0.0, 0.0);
  FCrowdWorkerDirtyStateRecord MovementDirty;
  MovementDirty.EntityRef = ChangedEntity;
  MovementDirty.Field = ECrowdWorkerField::Movement;
  MovementDirty.Generation = Generation;
  MovementDirty.WorkerEpoch = 2;
  MovementDirty.StateRevision = 1;
  MovementDirty.SourceInputSequence = InputSequence + 1;
  TestTrue(TEXT("affected cohort movement encodes"),
    FCrowdWorkerMovementStateCodec::Encode(
      ChangedMovement, MovementDirty.Payload));
  TestEqual(TEXT("affected cohort movement applies"),
    States.ApplyDirty(MovementDirty),
    ECrowdWorkerQueueResult::Replaced);
  Context.WorkerEpoch = 2;
  Context.AbsoluteSimulationTick = 2;
  Context.LastAppliedInputSequence = InputSequence + 1;
  Context.SimulationTimeSeconds = 2.0 / 30.0;
  FCrowdWorkerWorkItem AffectedWork;
  AffectedWork.Key.Domain = ECrowdWorkerDomainId::Target;
  AffectedWork.Key.Kind = ECrowdWorkerWorkKind::Cohort;
  AffectedWork.Key.ScopeKey =
    CrowdWorkerTargetWorkScopes::EncodeCohortKey(1);
  FCrowdWorkerDomainOutput AffectedOutput;
  TestTrue(TEXT("only affected target cohort executes"),
    Executor.Execute(Context, {AffectedWork}, AffectedOutput));
  for (const FCrowdWorkerDirtyStateRecord& Dirty :
    AffectedOutput.DirtyStates)
  {
    TestTrue(TEXT("unaffected cohort emits no dirty patch"),
      Dirty.EntityRef.StableEntityId
        > static_cast<uint64>(AgentsPerCohort));
  }
  const FCrowdWorkerTargetDomainMetrics AffectedMetrics =
    Executor.GetMetrics();
  TestEqual(TEXT("affected cohort adds only its guidance shards"),
    AffectedMetrics.GuidanceShardCount
      - BaselineMetrics.GuidanceShardCount,
    static_cast<uint64>(FMath::DivideAndRoundUp(
      AgentsPerCohort, GuidanceShardSize)));
  TestEqual(TEXT("unaffected cohort topology is not rebuilt"),
    AffectedMetrics.TopologyBuildCount,
    BaselineMetrics.TopologyBuildCount);
  TestEqual(TEXT("affected dependency closure has one cohort"),
    AffectedOutput.DeclaredDependencies.Num(),
    AgentsPerCohort + 3);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2ProjectileDomainTest,
  "MassCrowd.RuntimeV2.ProjectileCombatDomain",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2ProjectileDomainTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 31;
  constexpr uint64 InputSequence = 9;
  const FCrowdStableEntityRef Shooter{1, 100, 1};
  const FCrowdStableEntityRef Target{1, 101, 1};

  FCrowdWorkerProjectileControlResource Control;
  Control.Revision = 1;
  Control.AnchorEntity = Shooter;
  Control.bReplaceState = true;
  Control.Input.FixedStepIndex = 7;
  Control.Input.ServerTimeSeconds = 7.0f / 30.0f;
  Control.Input.FixedStepSeconds = 1.0f / 30.0f;
  FCrowdProjectileProfile& Profile =
    Control.Input.Profiles.AddDefaulted_GetRef();
  Profile.ProfileId = 1;
  Profile.RadiusCm = 10.0f;
  Profile.LifetimeFixedSteps = 30;
  Profile.MaxActiveProjectiles = 32;
  Profile.RecalculateStableHash();
  FCrowdProjectileSpawnRequest& Spawn =
    Control.Input.SpawnRequests.AddDefaulted_GetRef();
  Spawn.ProjectileId = 7001;
  Spawn.FixedStepIndex = Control.Input.FixedStepIndex;
  Spawn.Instigator = Shooter;
  Spawn.Target = Target;
  Spawn.FireSequence = 1;
  Spawn.SourceFactionId = 1;
  Spawn.ProjectileProfileId = 1;
  Spawn.CollisionProfileId = 1;
  Spawn.EffectProfileId = 1;
  Spawn.Position = FVector::ZeroVector;
  Spawn.Velocity = FVector(3000.0, 0.0, 0.0);
  Spawn.RecalculateStableHash();
  FCrowdProjectileTargetSnapshot& TargetSnapshot =
    Control.Input.Targets.AddDefaulted_GetRef();
  TargetSnapshot.EntityRef = Target;
  TargetSnapshot.FactionId = 2;
  TargetSnapshot.PreviousPosition = FVector(100.0, 0.0, 0.0);
  TargetSnapshot.Position = FVector(100.0, 0.0, 0.0);
  TargetSnapshot.RadiusCm = 10.0f;
  TargetSnapshot.bAlive = true;
  TargetSnapshot.RecalculateStableHash();
  FCrowdEffectProfile& Effect =
    Control.EffectProfiles.AddDefaulted_GetRef();
  Effect.EffectProfileId = 1;
  Effect.PayloadTypeId = 77;
  const uint32 Damage = 20;
  TestTrue(TEXT("effect payload initializes"),
    Effect.Payload.Set(78, Damage));
  Effect.RecalculateStableHash();

  FCrowdWorkerPayload Payload;
  TestTrue(TEXT("projectile control encodes"),
    FCrowdWorkerProjectileControlResourceCodec::Encode(
      Control, Payload));
  FCrowdWorkerProjectileControlResource DecodedControl;
  TestTrue(TEXT("projectile control decodes"),
    FCrowdWorkerProjectileControlResourceCodec::Decode(
      Payload, DecodedControl));
  TestEqual(TEXT("spawn survives round trip"),
    DecodedControl.Input.SpawnRequests.Num(), 1);
  TestEqual(TEXT("target survives round trip"),
    DecodedControl.Input.Targets.Num(), 1);

  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("projectile resource store resets"),
    Resources.Reset(
      FCrowdWorkerProjectileControlResourceCodec::
        MaxEncodedBytes));
  TestEqual(TEXT("projectile resource stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::ProjectileControl,
      Control.Revision, MoveTemp(Payload)}),
    ECrowdWorkerQueueResult::Added);
  TArray<FCrowdWorkerResourceRevisionEvent> ResourceEvents;
  TestTrue(TEXT("projectile resource commits"),
    Resources.CommitBuildingAtEpoch(1, ResourceEvents));

  FCrowdWorkerDomainContext Context;
  Context.Generation = Generation;
  Context.WorkerEpoch = 1;
  Context.AbsoluteSimulationTick = 7;
  Context.LastAppliedInputSequence = InputSequence;
  Context.NextOrderedEventSequence = 1;
  Context.FixedDeltaSeconds = 1.0 / 30.0;
  Context.SimulationTimeSeconds = 7.0 / 30.0;
  Context.RuntimeMode = ECrowdWorkerRuntimeV2Mode::Production;
  Context.Resources = &Resources;
  FCrowdWorkerWorkItem CombatWork;
  CombatWork.Key.Domain =
    ECrowdWorkerDomainId::CombatReactive;
  CombatWork.Key.Kind = ECrowdWorkerWorkKind::Resource;
  CombatWork.Key.ScopeKey =
    CrowdWorkerResourceIds::ProjectileControl;
  TArray<FCrowdWorkerWorkItem> Work{CombatWork};
  FCrowdWorkerProjectileDomainExecutor Executor;
  FCrowdWorkerDomainOutput Output;
  const bool bExecuted =
    Executor.Execute(Context, Work, Output);
  TestTrue(TEXT("projectile combat domain executes"), bExecuted);
  if (!bExecuted) return false;
  TestEqual(TEXT("projectile batch publishes once"),
    Output.DirtyStates.Num(), 1);
  TestEqual(TEXT("projectile field is explicit"),
    Output.DirtyStates[0].Field,
    ECrowdWorkerField::Projectile);
  FCrowdWorkerProjectileState State;
  TestTrue(TEXT("projectile state decodes"),
    FCrowdWorkerProjectileStateCodec::Decode(
      Output.DirtyStates[0].Payload, State));
  TestEqual(TEXT("one impact resolves"),
    State.Prepared.Impacts.Num(), 1);
  TestEqual(TEXT("one hit resolves"),
    State.ResolvedHits.Hits.Num(), 1);
  TestEqual(TEXT("spawn and impact lifecycle plus hit publish"),
    Output.OrderedEvents.Num(), 3);
  for (int32 Index = 0; Index < Output.OrderedEvents.Num(); ++Index)
  {
    TestEqual(TEXT("event sequence is contiguous"),
      Output.OrderedEvents[Index].EventSequence,
      static_cast<uint64>(Index + 1));
  }
  TestEqual(TEXT("active projectile schedules wakeup"),
    Output.Wakeups.Num(), 0);
  TestEqual(TEXT("resource dependency declared"),
    Output.DeclaredDependencies.Num(), 1);

  FCrowdWorkerDomainOutput DuplicateOutput;
  TestTrue(TEXT("duplicate projectile step is idempotent"),
    Executor.Execute(Context, Work, DuplicateOutput));
  TestEqual(TEXT("duplicate step does not republish state"),
    DuplicateOutput.DirtyStates.Num(), 0);
  TestEqual(TEXT("duplicate step does not republish events"),
    DuplicateOutput.OrderedEvents.Num(), 0);

  FCrowdWorkerProjectileControlResource NextControl = Control;
  NextControl.Revision = 2;
  NextControl.bReplaceState = false;
  NextControl.Input.FixedStepIndex = 8;
  NextControl.Input.ServerTimeSeconds = 8.0f / 30.0f;
  NextControl.Input.SpawnRequests.Reset();
  NextControl.Input.CurrentStates.Reset();
  FCrowdWorkerPayload NextPayload;
  TestTrue(TEXT("post-impact control encodes"),
    FCrowdWorkerProjectileControlResourceCodec::Encode(
      NextControl, NextPayload));
  TestEqual(TEXT("post-impact resource stages"),
    Resources.StageBuilding({
      CrowdWorkerResourceIds::ProjectileControl,
      NextControl.Revision, MoveTemp(NextPayload)}),
    ECrowdWorkerQueueResult::Added);
  TestTrue(TEXT("post-impact resource commits"),
    Resources.CommitBuildingAtEpoch(2, ResourceEvents));
  Context.WorkerEpoch = 2;
  Context.AbsoluteSimulationTick = 8;
  Context.LastAppliedInputSequence = InputSequence + 1;
  Context.NextOrderedEventSequence = 4;
  Context.SimulationTimeSeconds = 8.0 / 30.0;
  FCrowdWorkerDomainOutput PostImpactOutput;
  TestTrue(TEXT("post-impact step executes"),
    Executor.Execute(Context, Work, PostImpactOutput));
  TestEqual(TEXT("post-impact state publishes once"),
    PostImpactOutput.DirtyStates.Num(), 1);
  FCrowdWorkerProjectileState PostImpactState;
  TestTrue(TEXT("post-impact state decodes"),
    FCrowdWorkerProjectileStateCodec::Decode(
      PostImpactOutput.DirtyStates[0].Payload,
      PostImpactState));
  TestEqual(TEXT("terminal projectile is not re-fed"),
    PostImpactState.Prepared.States.Num(), 0);
  TestEqual(TEXT("terminal lifecycle is not replayed"),
    PostImpactOutput.OrderedEvents.Num(), 0);
  const FCrowdWorkerProjectileDomainMetrics Metrics =
    Executor.GetMetrics();
  TestEqual(TEXT("two projectile steps executed"),
    Metrics.ExecutedStepCount, uint64{2});
  TestEqual(TEXT("duplicate step counted"),
    Metrics.DuplicateStepCount, uint64{1});
  TestEqual(TEXT("one hit event published"),
    Metrics.PublishedHitEventCount, uint64{1});

  FCrowdWorkerProjectileControlResource LiveControl = Control;
  LiveControl.Revision = 10;
  LiveControl.Input.FixedStepIndex = 20;
  LiveControl.Input.ServerTimeSeconds = 20.0f / 30.0f;
  LiveControl.Input.SpawnRequests[0].FixedStepIndex = 20;
  LiveControl.Input.SpawnRequests[0].ProjectileId = 20001;
  LiveControl.Input.SpawnRequests[0].Velocity =
    FVector(300.0, 0.0, 0.0);
  LiveControl.Input.SpawnRequests[0].RecalculateStableHash();
  LiveControl.Input.Targets[0].PreviousPosition =
    FVector(10000.0, 0.0, 0.0);
  LiveControl.Input.Targets[0].Position =
    FVector(10000.0, 0.0, 0.0);
  LiveControl.Input.Targets[0].RecalculateStableHash();
  FCrowdWorkerPayload LivePayload;
  TestTrue(TEXT("live checkpoint control encodes"),
    FCrowdWorkerProjectileControlResourceCodec::Encode(
      LiveControl, LivePayload));
  FCrowdWorkerResourceStore LiveResources;
  TestTrue(TEXT("live checkpoint resources reset"),
    LiveResources.Reset(
      FCrowdWorkerProjectileControlResourceCodec::
        MaxEncodedBytes));
  TestEqual(TEXT("live checkpoint resource stages"),
    LiveResources.StageBuilding({
      CrowdWorkerResourceIds::ProjectileControl,
      LiveControl.Revision, MoveTemp(LivePayload)}),
    ECrowdWorkerQueueResult::Added);
  TestTrue(TEXT("live checkpoint resource commits"),
    LiveResources.CommitBuildingAtEpoch(20, ResourceEvents));
  FCrowdWorkerDomainContext LiveContext = Context;
  LiveContext.WorkerEpoch = 20;
  LiveContext.AbsoluteSimulationTick = 20;
  LiveContext.LastAppliedInputSequence = 20;
  LiveContext.NextOrderedEventSequence = 1;
  LiveContext.SimulationTimeSeconds = 20.0 / 30.0;
  LiveContext.Resources = &LiveResources;
  FCrowdWorkerProjectileDomainExecutor LiveExecutor;
  FCrowdWorkerDomainOutput LiveOutput;
  TestTrue(TEXT("live checkpoint step executes"),
    LiveExecutor.Execute(LiveContext, Work, LiveOutput));
  TestEqual(TEXT("live checkpoint publishes spawn event"),
    LiveOutput.OrderedEvents.Num(), 1);
  FCrowdWorkerProjectileState LiveState;
  TestTrue(TEXT("live checkpoint state decodes"),
    FCrowdWorkerProjectileStateCodec::Decode(
      LiveOutput.DirtyStates[0].Payload, LiveState));
  TestEqual(TEXT("live checkpoint retains active projectile"),
    LiveState.Prepared.States.Num(), 1);

  FCrowdWorkerProjectileDomainExecutor AutonomousExecutor;
  FCrowdWorkerDomainOutput AutonomousBootstrapOutput;
  TestTrue(TEXT("autonomous projectile bootstrap executes"),
    AutonomousExecutor.Execute(
      LiveContext, Work, AutonomousBootstrapOutput));
  FCrowdWorkerWorkItem ClockWork = CombatWork;
  ClockWork.ReasonMask = CrowdWorkerReasonMasks::CombatClock;
  FCrowdWorkerDomainContext TimerContext = LiveContext;
  TimerContext.WorkerEpoch = 21;
  TimerContext.AbsoluteSimulationTick = 21;
  TimerContext.LastAppliedInputSequence = 21;
  TimerContext.NextOrderedEventSequence = 2;
  TimerContext.SimulationTimeSeconds = 21.0 / 30.0;
  FCrowdWorkerDomainOutput AutonomousClockOutput;
  TestTrue(TEXT("projectile clock advances without resource revision"),
    AutonomousExecutor.Execute(
      TimerContext, TArray<FCrowdWorkerWorkItem>{ClockWork},
      AutonomousClockOutput));
  FCrowdWorkerProjectileState AutonomousClockState;
  TestTrue(TEXT("autonomous clock state decodes"),
    AutonomousClockOutput.DirtyStates.Num() == 1
      && FCrowdWorkerProjectileStateCodec::Decode(
        AutonomousClockOutput.DirtyStates[0].Payload,
        AutonomousClockState));
  TestEqual(TEXT("autonomous clock owns next fixed step"),
    AutonomousClockState.Prepared.FixedStepIndex, int64{21});
  TestEqual(TEXT("autonomous clock keeps control revision"),
    AutonomousClockState.ControlRevision,
    LiveControl.Revision);

  FCrowdWorkerProjectileControlResource NextRoundControl =
    LiveControl;
  NextRoundControl.Revision = LiveControl.Revision + 1;
  NextRoundControl.bReplaceState = true;
  NextRoundControl.Input.FixedStepIndex = 0;
  NextRoundControl.Input.ServerTimeSeconds = 0.0f;
  NextRoundControl.Input.SpawnRequests.Reset();
  NextRoundControl.Input.CurrentStates.Reset();
  FCrowdWorkerPayload NextRoundPayload;
  TestTrue(TEXT("next-round control encodes"),
    FCrowdWorkerProjectileControlResourceCodec::Encode(
      NextRoundControl, NextRoundPayload));
  FCrowdWorkerResourceStore NextRoundResources;
  TestTrue(TEXT("next-round resources reset"),
    NextRoundResources.Reset(
      FCrowdWorkerProjectileControlResourceCodec::
        MaxEncodedBytes));
  TestEqual(TEXT("next-round resource stages"),
    NextRoundResources.StageBuilding({
      CrowdWorkerResourceIds::ProjectileControl,
      NextRoundControl.Revision, MoveTemp(NextRoundPayload)}),
    ECrowdWorkerQueueResult::Added);
  TestTrue(TEXT("next-round resource commits"),
    NextRoundResources.CommitBuildingAtEpoch(
      TimerContext.WorkerEpoch + 1, ResourceEvents));
  FCrowdWorkerDomainContext NextRoundContext = TimerContext;
  NextRoundContext.WorkerEpoch += 1;
  NextRoundContext.AbsoluteSimulationTick += 1;
  NextRoundContext.LastAppliedInputSequence += 1;
  NextRoundContext.SimulationTimeSeconds += 1.0 / 30.0;
  NextRoundContext.Resources = &NextRoundResources;
  FCrowdWorkerDomainOutput NextRoundOutput;
  TestTrue(TEXT("fresh revision restarts projectile round"),
    AutonomousExecutor.Execute(
      NextRoundContext, Work, NextRoundOutput));
  FCrowdWorkerProjectileState NextRoundState;
  TestTrue(TEXT("next-round state decodes"),
    NextRoundOutput.DirtyStates.Num() == 1
      && FCrowdWorkerProjectileStateCodec::Decode(
        NextRoundOutput.DirtyStates[0].Payload,
        NextRoundState));
  TestEqual(TEXT("fresh revision owns reset fixed step"),
    NextRoundState.Prepared.FixedStepIndex, int64{0});
  TestEqual(TEXT("fresh revision replaces prior projectiles"),
    NextRoundState.Prepared.States.Num(), 0);
  TestEqual(TEXT("fresh revision becomes active control"),
    NextRoundState.ControlRevision,
    NextRoundControl.Revision);

  FCrowdWorkerCheckpoint ReplayCheckpoint;
  ReplayCheckpoint.Generation = Generation;
  ReplayCheckpoint.WorkerEpoch = LiveContext.WorkerEpoch;
  ReplayCheckpoint.AbsoluteSimulationTick =
    LiveContext.AbsoluteSimulationTick;
  ReplayCheckpoint.FixedSimulationQuantumSeconds = 1.0 / 30.0;
  ReplayCheckpoint.LastAppliedInputSequence =
    LiveContext.LastAppliedInputSequence;
  ReplayCheckpoint.LastOrderedEventSequence =
    LiveOutput.OrderedEvents.Last().EventSequence;
  ReplayCheckpoint.EntityStateHash =
    LiveOutput.DirtyStates[0].Payload.StableHash;
  ReplayCheckpoint.ResourceRevisionHash =
    LiveControl.Revision;
  ReplayCheckpoint.RecalculateStableHash();
  TestTrue(TEXT("projectile replay checkpoint valid"),
    ReplayCheckpoint.IsValid());

  FCrowdWorkerProjectileControlResource ReplayControl =
    LiveControl;
  ReplayControl.Revision = 11;
  ReplayControl.bReplaceState = false;
  ReplayControl.Input.FixedStepIndex = 21;
  ReplayControl.Input.ServerTimeSeconds = 21.0f / 30.0f;
  ReplayControl.Input.SpawnRequests.Reset();
  ReplayControl.Input.CurrentStates.Reset();
  FCrowdWorkerPayload ReplayPayload;
  TestTrue(TEXT("continuation control encodes"),
    FCrowdWorkerProjectileControlResourceCodec::Encode(
      ReplayControl, ReplayPayload));
  TestEqual(TEXT("continuation resource stages"),
    LiveResources.StageBuilding({
      CrowdWorkerResourceIds::ProjectileControl,
      ReplayControl.Revision, MoveTemp(ReplayPayload)}),
    ECrowdWorkerQueueResult::Added);
  TestTrue(TEXT("continuation resource commits"),
    LiveResources.CommitBuildingAtEpoch(21, ResourceEvents));
  LiveContext.WorkerEpoch = 21;
  LiveContext.AbsoluteSimulationTick = 21;
  LiveContext.LastAppliedInputSequence = 21;
  LiveContext.NextOrderedEventSequence =
    ReplayCheckpoint.LastOrderedEventSequence + 1;
  LiveContext.SimulationTimeSeconds = 21.0 / 30.0;
  FCrowdWorkerDomainOutput ContinuedOutput;
  TestTrue(TEXT("original continuation executes"),
    LiveExecutor.Execute(LiveContext, Work, ContinuedOutput));

  FCrowdWorkerProjectileControlResource RestoredControl =
    ReplayControl;
  RestoredControl.bReplaceState = true;
  RestoredControl.Input.CurrentStates =
    LiveState.Prepared.States;
  FCrowdWorkerPayload RestoredPayload;
  TestTrue(TEXT("restored control encodes"),
    FCrowdWorkerProjectileControlResourceCodec::Encode(
      RestoredControl, RestoredPayload));
  FCrowdWorkerResourceStore RestoredResources;
  TestTrue(TEXT("restored resources reset"),
    RestoredResources.Reset(
      FCrowdWorkerProjectileControlResourceCodec::
        MaxEncodedBytes));
  TestEqual(TEXT("restored resource stages"),
    RestoredResources.StageBuilding({
      CrowdWorkerResourceIds::ProjectileControl,
      RestoredControl.Revision, MoveTemp(RestoredPayload)}),
    ECrowdWorkerQueueResult::Added);
  TestTrue(TEXT("restored resource commits"),
    RestoredResources.CommitBuildingAtEpoch(21, ResourceEvents));
  FCrowdWorkerDomainContext RestoredContext = LiveContext;
  RestoredContext.Resources = &RestoredResources;
  FCrowdWorkerProjectileDomainExecutor RestoredExecutor;
  FCrowdWorkerDomainOutput RestoredOutput;
  TestTrue(TEXT("restored continuation executes"),
    RestoredExecutor.Execute(
      RestoredContext, Work, RestoredOutput));
  TestEqual(TEXT("replay event count is conserved"),
    RestoredOutput.OrderedEvents.Num(),
    ContinuedOutput.OrderedEvents.Num());
  TestEqual(TEXT("replay state count is conserved"),
    RestoredOutput.DirtyStates.Num(),
    ContinuedOutput.DirtyStates.Num());
  TestTrue(TEXT("checkpoint replay state is byte identical"),
    RestoredOutput.DirtyStates.Num() == 1
      && ContinuedOutput.DirtyStates.Num() == 1
      && RestoredOutput.DirtyStates[0].Payload
        == ContinuedOutput.DirtyStates[0].Payload);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2LifecycleCommandOwnershipTest,
  "MassCrowd.RuntimeV2.LifecycleCommandOwnership",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2LifecycleCommandOwnershipTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  const FCrowdStableEntityRef FirstLifecycle{7, 42, 3};
  const FCrowdStableEntityRef StaleLifecycle{7, 42, 2};
  const FCrowdStableEntityRef SkippedLifecycle{7, 42, 5};
  const FCrowdStableEntityRef NextLifecycle{7, 42, 4};

  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("lifecycle state store resets"),
    States.Reset(8, 1024));
  TestEqual(TEXT("resnapshot accepts current lifecycle"),
    States.Spawn(
      FirstLifecycle, 1, 1, MakePayload(11)),
    ECrowdWorkerQueueResult::Added);
  TestEqual(TEXT("duplicate spawn is idempotent"),
    States.Spawn(
      FirstLifecycle, 1, 1, MakePayload(11)),
    ECrowdWorkerQueueResult::MergedDuplicate);
  TestEqual(TEXT("parallel lifecycle is rejected"),
    States.Spawn(
      NextLifecycle, 1, 2, MakePayload(12)),
    ECrowdWorkerQueueResult::Conflict);
  TestTrue(TEXT("active lifecycle despawns"),
    States.Despawn(FirstLifecycle));
  TestEqual(TEXT("stale lifecycle cannot return"),
    States.Spawn(
      StaleLifecycle, 1, 3, MakePayload(13)),
    ECrowdWorkerQueueResult::RejectedStale);
  TestEqual(TEXT("lifecycle serial cannot skip"),
    States.Spawn(
      SkippedLifecycle, 1, 3, MakePayload(14)),
    ECrowdWorkerQueueResult::Conflict);
  TestEqual(TEXT("exact next lifecycle reuses slot"),
    States.Spawn(
      NextLifecycle, 1, 3, MakePayload(15)),
    ECrowdWorkerQueueResult::Added);
  TestFalse(TEXT("old lifecycle no longer exists"),
    States.Contains(FirstLifecycle));
  TestTrue(TEXT("next lifecycle is authoritative"),
    States.Contains(NextLifecycle));

  FCrowdWorkerCommandStore Commands;
  TestTrue(TEXT("command store resets"),
    Commands.Reset(4, 1024));
  FCrowdWorkerCommandDelta Command;
  Command.InputSequence = 4;
  Command.EntityRef = NextLifecycle;
  Command.CommandId = 77;
  Command.EffectiveSimulationTimeSeconds = 2.0;
  Command.Payload = MakePayload(21, 91020);
  TestEqual(TEXT("command journal accepts command"),
    Commands.Enqueue(Command),
    ECrowdWorkerQueueResult::Added);
  TestEqual(TEXT("command retry is idempotent"),
    Commands.Enqueue(Command),
    ECrowdWorkerQueueResult::MergedDuplicate);
  FCrowdWorkerCommandDelta Conflict = Command;
  Conflict.Payload = MakePayload(22, 91020);
  TestEqual(TEXT("conflicting command retry fails closed"),
    Commands.Enqueue(Conflict),
    ECrowdWorkerQueueResult::Conflict);
  TArray<FCrowdWorkerCommandRecord> Due;
  TestEqual(TEXT("future command stays asleep"),
    Commands.CollectEntity(NextLifecycle, 1.0, Due), 0);
  TestEqual(TEXT("due command wakes once"),
    Commands.CollectEntity(NextLifecycle, 2.0, Due), 1);
  TestEqual(TEXT("due command preserves input sequence"),
    Due[0].InputSequence, uint64{4});
  TestTrue(TEXT("owner barrier acknowledges command"),
    Commands.Acknowledge(NextLifecycle, 4));
  TestEqual(TEXT("acknowledged command leaves journal"),
    Commands.Num(), 0);

  FCrowdWorkerCommandDelta OldLifecycleCommand = Command;
  OldLifecycleCommand.InputSequence = 5;
  OldLifecycleCommand.EntityRef = FirstLifecycle;
  TestEqual(TEXT("store itself accepts structurally valid old ref"),
    Commands.Enqueue(OldLifecycleCommand),
    ECrowdWorkerQueueResult::Added);
  TestEqual(TEXT("despawn invalidates old lifecycle journal"),
    Commands.RemoveEntity(FirstLifecycle), 1);
  TestEqual(TEXT("old lifecycle journal is empty"),
    Commands.Num(), 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRuntimeV2LifecycleBehaviorDomainTest,
  "MassCrowd.RuntimeV2.LifecycleBehaviorDomain",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRuntimeV2LifecycleBehaviorDomainTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  const FCrowdStableEntityRef EntityRef{2, 900, 1};
  constexpr uint32 ProfileKey = 501;
  constexpr uint32 CapabilityId = 601;
  constexpr uint32 SourceTypeId = 701;
  constexpr uint32 SourcePayloadSchema = 702;

  FCrowdCapabilityProfileRegistry Profiles;
  FCrowdCapabilityProfile Profile;
  Profile.Key.Value = ProfileKey;
  Profile.CapabilityIds.Add({CapabilityId});
  TestTrue(TEXT("worker profile registers"),
    Profiles.Register(MoveTemp(Profile)));
  FCrowdCapabilityProfile ReducedProfile;
  ReducedProfile.Key.Value = ProfileKey + 1;
  TestTrue(TEXT("reduced worker profile registers"),
    Profiles.Register(MoveTemp(ReducedProfile)));
  TestTrue(TEXT("worker profiles freeze"),
    Profiles.Freeze());

  FCrowdBehaviorSourceEvaluatorRegistry Evaluators;
  FCrowdBehaviorSourceSpec Spec;
  Spec.TypeId.Value = SourceTypeId;
  Spec.ChannelMask =
    CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Business);
  Spec.MaxLifetimeSteps = 3;
  Spec.PayloadSchemaId = SourcePayloadSchema;
  Spec.RequiredCapabilityCount = 1;
  Spec.RequiredCapabilities[0].Value = CapabilityId;
  TestTrue(TEXT("worker behavior spec registers"),
    Evaluators.Register(
      Spec,
      MakeShared<
        FNoopBehaviorEvaluator,
        ESPMode::ThreadSafe>()));
  TestTrue(TEXT("worker behavior specs freeze"),
    Evaluators.Freeze());

  FCrowdMassBoundaryAgentRecord Agent;
  Agent.Identity.AgentId = 9;
  Agent.Identity.SetStableEntityRef(EntityRef);
  Agent.AgentFacts.StableEntityRef = EntityRef;
  Agent.AgentFacts.CapabilitySet.Bits = 1;
  Agent.State.Position = FVector(10.0, 20.0, 0.0);
  Agent.State.PlanRevision = 1;
  Agent.State.bInitialized = true;
  Agent.Properties.PhysicalRadiusCm = 25.0f;
  Agent.Properties.MaximumSpeedCmps = 300.0f;
  Agent.Properties.CapabilityProfileKey = ProfileKey;
  FCrowdWorkerPayload InitialPayload;
  TestTrue(TEXT("full spawn payload encodes"),
    FCrowdWorkerBoundaryStateCodec::EncodeState(
      Agent, InitialPayload));

  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("domain state store resets"),
    States.Reset(8, 16384));
  TestEqual(TEXT("domain spawn installs full state"),
    States.Spawn(
      EntityRef, 1, 1, MoveTemp(InitialPayload)),
    ECrowdWorkerQueueResult::Added);

  FCrowdBehaviorSourceCommand SourceCommand;
  SourceCommand.EffectiveFixedStep = 1;
  SourceCommand.Handle.EntityRef = EntityRef;
  SourceCommand.Handle.ControllerId.Value = 801;
  SourceCommand.Handle.SourceSequence = 1;
  SourceCommand.CommandSequence = 1;
  SourceCommand.Kind = ECrowdBehaviorSourceCommandKind::Start;
  SourceCommand.SourceTypeId.Value = SourceTypeId;
  SourceCommand.LifetimeSteps = 3;
  const uint32 BusinessValue = 55;
  TestTrue(TEXT("source command payload initializes"),
    SourceCommand.Payload.Set(
      SourcePayloadSchema, BusinessValue));
  FCrowdWorkerPayload EncodedCommand;
  TestTrue(TEXT("source command encodes"),
    FCrowdWorkerBoundaryStateCodec::EncodeBehaviorCommand(
      SourceCommand, EncodedCommand));
  FCrowdBehaviorSourceCommand DecodedCommand;
  TestTrue(TEXT("source command decodes"),
    FCrowdWorkerBoundaryStateCodec::DecodeBehaviorCommand(
      EncodedCommand, DecodedCommand));
  TestEqual(TEXT("command codec preserves stable hash"),
    DecodedCommand.CalculateStableHash(),
    SourceCommand.CalculateStableHash());

  FCrowdWorkerCommandStore Commands;
  TestTrue(TEXT("domain command store resets"),
    Commands.Reset(8, 16384));
  FCrowdWorkerCommandDelta CommandDelta;
  CommandDelta.InputSequence = 2;
  CommandDelta.EntityRef = EntityRef;
  CommandDelta.CommandId =
    FCrowdWorkerBoundaryStateCodec::BehaviorCommandSchemaId;
  CommandDelta.EffectiveSimulationTimeSeconds = 1.0 / 30.0;
  CommandDelta.Payload = EncodedCommand;
  TestEqual(TEXT("domain command journals"),
    Commands.Enqueue(CommandDelta),
    ECrowdWorkerQueueResult::Added);

  FCrowdWorkerDomainContext Context;
  Context.Generation = 1;
  Context.WorkerEpoch = 1;
  Context.AbsoluteSimulationTick = 1;
  Context.LastAppliedInputSequence = 2;
  Context.FixedDeltaSeconds = 1.0 / 30.0;
  Context.SimulationTimeSeconds = 1.0 / 30.0;
  Context.RuntimeMode = ECrowdWorkerRuntimeV2Mode::Shadow;
  Context.EntityStates = &States;
  Context.Commands = &Commands;

  FCrowdWorkerWorkItem LifecycleWork =
    MakeEntityWork(ECrowdWorkerDomainId::LifecycleInput, 900);
  LifecycleWork.Key.PrimaryEntity = EntityRef;
  TArray<FCrowdWorkerWorkItem> LifecycleItems{LifecycleWork};
  FCrowdWorkerLifecycleDomainExecutor LifecycleExecutor;
  FCrowdWorkerDomainOutput LifecycleOutput;
  TestTrue(TEXT("lifecycle domain executes"),
    LifecycleExecutor.Execute(
      Context, LifecycleItems, LifecycleOutput));
  TestEqual(TEXT("lifecycle publishes one state"),
    LifecycleOutput.DirtyStates.Num(), 1);
  FCrowdWorkerLifecycleState LifecycleState;
  TestTrue(TEXT("lifecycle state decodes"),
    FCrowdWorkerLifecycleStateCodec::Decode(
      LifecycleOutput.DirtyStates[0].Payload,
      LifecycleState));
  TestEqual(TEXT("lifecycle owns exact ref"),
    LifecycleState.EntityRef, EntityRef);

  FCrowdWorkerWorkItem BehaviorWork =
    MakeEntityWork(ECrowdWorkerDomainId::Behavior, 900);
  BehaviorWork.Key.PrimaryEntity = EntityRef;
  TArray<FCrowdWorkerWorkItem> BehaviorItems{BehaviorWork};
  FCrowdWorkerBehaviorDomainExecutor BehaviorExecutor(
    Profiles, Evaluators);
  FCrowdWorkerDomainOutput BehaviorOutput;
  TestTrue(TEXT("behavior domain executes"),
    BehaviorExecutor.Execute(
      Context, BehaviorItems, BehaviorOutput));
  TestEqual(TEXT("behavior publishes one state"),
    BehaviorOutput.DirtyStates.Num(), 1);
  TestEqual(TEXT("behavior consumes command once"),
    BehaviorOutput.ConsumedCommandInputSequences.Num(), 1);
  TestEqual(TEXT("behavior publishes ordered start event"),
    BehaviorOutput.OrderedEvents.Num(), 2);
  FCrowdBehaviorSourceEvent StartedEvent;
  TestTrue(TEXT("ordered start event decodes"),
    BehaviorOutput.OrderedEvents.Num() == 2
      && FCrowdWorkerBehaviorEventCodec::Decode(
        BehaviorOutput.OrderedEvents[0].Payload, StartedEvent));
  TestEqual(TEXT("ordered start event kind"),
    StartedEvent.Kind,
    ECrowdBehaviorSourceEventKind::Started);
  FCrowdBusinessContribution BusinessCommit;
  TestTrue(TEXT("ordered business commit decodes"),
    FCrowdWorkerBusinessCommitEventCodec::Decode(
      BehaviorOutput.OrderedEvents[1].Payload,
      BusinessCommit));
  TestEqual(TEXT("business commit id is stable"),
    BusinessCommit.CommitId, uint64{1});
  FCrowdWorkerBehaviorState BehaviorState;
  TestTrue(TEXT("behavior state decodes"),
    FCrowdWorkerBehaviorStateCodec::Decode(
      BehaviorOutput.DirtyStates[0].Payload,
      BehaviorState));
  TestEqual(TEXT("worker owns source set"),
    BehaviorState.SourceSet.Instances.Num(), 1);
  TestEqual(TEXT("worker owns command cursor"),
    BehaviorState.SourceSet.ControllerCursors.Num(), 1);
  TestEqual(TEXT("worker owns command ledger"),
    BehaviorState.LastConsumedCommandInputSequence,
    uint64{2});
  TestEqual(TEXT("worker owns business commit ledger"),
    BehaviorState.AppliedBusinessCommitIds.Num(), 1);
  TestTrue(TEXT("behavior state applies"),
    States.ApplyDirty(BehaviorOutput.DirtyStates[0])
      == ECrowdWorkerQueueResult::Replaced);
  TestTrue(TEXT("owner acknowledges behavior command"),
    Commands.Acknowledge(2));

  Context.WorkerEpoch = 4;
  Context.AbsoluteSimulationTick = 4;
  Context.SimulationTimeSeconds = 4.0 / 30.0;
  FCrowdWorkerDomainOutput ExpireOutput;
  TestTrue(TEXT("behavior wakeup expires source"),
    BehaviorExecutor.Execute(
      Context, BehaviorItems, ExpireOutput));
  TestEqual(TEXT("expiration publishes once"),
    ExpireOutput.DirtyStates.Num(), 1);
  TestEqual(TEXT("expiration publishes ordered event"),
    ExpireOutput.OrderedEvents.Num(), 1);
  FCrowdBehaviorSourceEvent ExpiredEvent;
  TestTrue(TEXT("ordered expiration decodes"),
    ExpireOutput.OrderedEvents.Num() == 1
      && FCrowdWorkerBehaviorEventCodec::Decode(
        ExpireOutput.OrderedEvents[0].Payload, ExpiredEvent));
  TestEqual(TEXT("ordered expiration event kind"),
    ExpiredEvent.Kind,
    ECrowdBehaviorSourceEventKind::Expired);
  FCrowdWorkerBehaviorState ExpiredState;
  TestTrue(TEXT("expired behavior state decodes"),
    FCrowdWorkerBehaviorStateCodec::Decode(
      ExpireOutput.DirtyStates[0].Payload,
      ExpiredState));
  TestEqual(TEXT("expired source leaves worker set"),
    ExpiredState.SourceSet.Instances.Num(), 0);
  TestEqual(TEXT("command is not consumed twice"),
    ExpireOutput.ConsumedCommandInputSequences.Num(), 0);
  TestEqual(TEXT("expired state applies"),
    States.ApplyDirty(ExpireOutput.DirtyStates[0]),
    ECrowdWorkerQueueResult::Replaced);

  FCrowdBehaviorSourceCommand LateStop = SourceCommand;
  LateStop.EffectiveFixedStep = 4;
  LateStop.CommandSequence = 2;
  LateStop.Kind = ECrowdBehaviorSourceCommandKind::Stop;
  FCrowdWorkerPayload LateStopPayload;
  TestTrue(TEXT("late stop encodes"),
    FCrowdWorkerBoundaryStateCodec::EncodeBehaviorCommand(
      LateStop, LateStopPayload));
  FCrowdWorkerCommandDelta LateStopDelta;
  LateStopDelta.InputSequence = 3;
  LateStopDelta.EntityRef = EntityRef;
  LateStopDelta.CommandId =
    FCrowdWorkerBoundaryStateCodec::BehaviorCommandSchemaId;
  LateStopDelta.EffectiveSimulationTimeSeconds = 4.0 / 30.0;
  LateStopDelta.Payload = MoveTemp(LateStopPayload);
  TestEqual(TEXT("late stop journals"),
    Commands.Enqueue(LateStopDelta),
    ECrowdWorkerQueueResult::Added);
  Context.WorkerEpoch = 5;
  Context.AbsoluteSimulationTick = 5;
  Context.LastAppliedInputSequence = 3;
  Context.SimulationTimeSeconds = 5.0 / 30.0;
  FCrowdWorkerDomainOutput LateStopOutput;
  TestTrue(TEXT("expired-source stop is idempotent"),
    BehaviorExecutor.Execute(
      Context, BehaviorItems, LateStopOutput));
  TestEqual(TEXT("late stop consumes once"),
    LateStopOutput.ConsumedCommandInputSequences.Num(), 1);
  TestEqual(TEXT("late stop publishes cursor tombstone"),
    LateStopOutput.DirtyStates.Num(), 1);
  TestEqual(TEXT("late stop does not duplicate expiration event"),
    LateStopOutput.OrderedEvents.Num(), 0);
  FCrowdWorkerBehaviorState TombstonedState;
  TestTrue(TEXT("late stop tombstone decodes"),
    FCrowdWorkerBehaviorStateCodec::Decode(
      LateStopOutput.DirtyStates[0].Payload,
      TombstonedState));
  TestEqual(TEXT("expired source stays absent"),
    TombstonedState.SourceSet.Instances.Num(), 0);
  TestEqual(TEXT("cursor advances through late stop"),
    TombstonedState.SourceSet.ControllerCursors[0].
      LastCommandSequence,
    uint32{2});
  TestEqual(TEXT("late stop state applies"),
    States.ApplyDirty(LateStopOutput.DirtyStates[0]),
    ECrowdWorkerQueueResult::Replaced);
  TestTrue(TEXT("late stop command acknowledges"),
    Commands.Acknowledge(3));

  FCrowdBehaviorCapabilityBindingUpdate BindingUpdate;
  BindingUpdate.EffectiveFixedStep = 6;
  BindingUpdate.EntityRef = EntityRef;
  BindingUpdate.Binding.ProfileKey.Value = ProfileKey + 1;
  BindingUpdate.RecalculateStableHash();
  FCrowdWorkerPayload BindingPayload;
  TestTrue(TEXT("binding input encodes"),
    FCrowdWorkerBehaviorBindingInputCodec::Encode(
      BindingUpdate, BindingPayload));
  FCrowdBehaviorCapabilityBindingUpdate DecodedBinding;
  TestTrue(TEXT("binding input decodes"),
    FCrowdWorkerBehaviorBindingInputCodec::Decode(
      BindingPayload, DecodedBinding));
  TestEqual(TEXT("binding codec preserves stable hash"),
    DecodedBinding.StableHash, BindingUpdate.StableHash);
  FCrowdWorkerCommandDelta BindingDelta;
  BindingDelta.InputSequence = 4;
  BindingDelta.EntityRef = EntityRef;
  BindingDelta.CommandId =
    FCrowdWorkerBehaviorBindingInputCodec::SchemaId;
  BindingDelta.EffectiveSimulationTimeSeconds = 6.0 / 30.0;
  BindingDelta.Payload = MoveTemp(BindingPayload);
  TestEqual(TEXT("binding input journals"),
    Commands.Enqueue(BindingDelta),
    ECrowdWorkerQueueResult::Added);
  Context.WorkerEpoch = 6;
  Context.AbsoluteSimulationTick = 6;
  Context.LastAppliedInputSequence = 4;
  Context.SimulationTimeSeconds = 6.0 / 30.0;
  FCrowdWorkerDomainOutput BindingOutput;
  TestTrue(TEXT("worker applies capability binding"),
    BehaviorExecutor.Execute(
      Context, BehaviorItems, BindingOutput));
  TestEqual(TEXT("binding input consumes once"),
    BindingOutput.ConsumedCommandInputSequences.Num(), 1);
  FCrowdWorkerBehaviorState BoundState;
  TestTrue(TEXT("binding worker state decodes"),
    BindingOutput.DirtyStates.Num() == 1
      && FCrowdWorkerBehaviorStateCodec::Decode(
        BindingOutput.DirtyStates[0].Payload, BoundState));
  TestEqual(TEXT("worker owns updated profile"),
    BoundState.SourceSet.CapabilityBinding.ProfileKey.Value,
    ProfileKey + 1);

  const FCrowdStableEntityRef BootstrapEntityRef{2, 901, 1};
  FCrowdMassBoundaryAgentRecord BootstrapAgent = Agent;
  BootstrapAgent.Identity.AgentId = 10;
  BootstrapAgent.Identity.SetStableEntityRef(BootstrapEntityRef);
  BootstrapAgent.AgentFacts.StableEntityRef = BootstrapEntityRef;
  BootstrapAgent.Properties.CapabilityProfileKey = 0;
  FCrowdWorkerPayload BootstrapStatePayload;
  TestTrue(TEXT("zero-profile spawn payload encodes"),
    FCrowdWorkerBoundaryStateCodec::EncodeState(
      BootstrapAgent, BootstrapStatePayload));
  FCrowdWorkerEntityStateStore BootstrapStates;
  TestTrue(TEXT("bootstrap state store resets"),
    BootstrapStates.Reset(2, 4096));
  TestEqual(TEXT("bootstrap entity spawns"),
    BootstrapStates.Spawn(
      BootstrapEntityRef, 1, 1,
      MoveTemp(BootstrapStatePayload)),
    ECrowdWorkerQueueResult::Added);
  FCrowdBehaviorCapabilityBindingUpdate BootstrapUpdate;
  BootstrapUpdate.EffectiveFixedStep = 1;
  BootstrapUpdate.EntityRef = BootstrapEntityRef;
  BootstrapUpdate.Binding.ProfileKey.Value = ProfileKey;
  BootstrapUpdate.RecalculateStableHash();
  FCrowdWorkerPayload BootstrapBindingPayload;
  TestTrue(TEXT("bootstrap binding encodes"),
    FCrowdWorkerBehaviorBindingInputCodec::Encode(
      BootstrapUpdate, BootstrapBindingPayload));
  FCrowdWorkerCommandStore BootstrapCommands;
  TestTrue(TEXT("bootstrap command store resets"),
    BootstrapCommands.Reset(2, 4096));
  FCrowdWorkerCommandDelta BootstrapDelta;
  BootstrapDelta.InputSequence = 1;
  BootstrapDelta.EntityRef = BootstrapEntityRef;
  BootstrapDelta.CommandId =
    FCrowdWorkerBehaviorBindingInputCodec::SchemaId;
  BootstrapDelta.EffectiveSimulationTimeSeconds = 1.0 / 30.0;
  BootstrapDelta.Payload = MoveTemp(BootstrapBindingPayload);
  TestEqual(TEXT("bootstrap binding journals"),
    BootstrapCommands.Enqueue(BootstrapDelta),
    ECrowdWorkerQueueResult::Added);
  FCrowdWorkerDomainContext BootstrapContext = Context;
  BootstrapContext.WorkerEpoch = 1;
  BootstrapContext.AbsoluteSimulationTick = 1;
  BootstrapContext.LastAppliedInputSequence = 1;
  BootstrapContext.SimulationTimeSeconds = 1.0 / 30.0;
  BootstrapContext.EntityStates = &BootstrapStates;
  BootstrapContext.Commands = &BootstrapCommands;
  FCrowdWorkerWorkItem BootstrapWork =
    MakeEntityWork(ECrowdWorkerDomainId::Behavior, 901);
  BootstrapWork.Key.PrimaryEntity = BootstrapEntityRef;
  const TArray<FCrowdWorkerWorkItem> BootstrapItems{
    BootstrapWork};
  FCrowdWorkerDomainOutput BootstrapOutput;
  TestTrue(TEXT("same-batch binding bootstraps behavior"),
    BehaviorExecutor.Execute(
      BootstrapContext, BootstrapItems, BootstrapOutput));
  FCrowdWorkerBehaviorState BootstrapState;
  TestTrue(TEXT("bootstrapped behavior state decodes"),
    BootstrapOutput.DirtyStates.Num() == 1
      && FCrowdWorkerBehaviorStateCodec::Decode(
        BootstrapOutput.DirtyStates[0].Payload,
        BootstrapState));
  TestEqual(TEXT("bootstrap binding owns initial profile"),
    BootstrapState.SourceSet.CapabilityBinding.ProfileKey.Value,
    ProfileKey);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerNoCorrectionPredictionWindowTest,
  "MassCrowd.RuntimeV2.NoCorrection300TickPrediction",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerNoCorrectionPredictionWindowTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 101;
  FCrowdAsyncSimulationRuntime Runtime;
  FCrowdAsyncSimulationRuntimeConfig PredictionConfig =
    MakeSyntheticConfig();
  PredictionConfig.WorkerV2.Mode =
    ECrowdWorkerRuntimeV2Mode::Production;
  TestTrue(TEXT("prediction domain registers"),
    Runtime.RegisterDomainExecutor(
      MakeUnique<FSyntheticLifecycleDomain>()));
  TestTrue(TEXT("prediction runtime starts"),
    Runtime.Start(PredictionConfig, Generation));
  TestEqual(TEXT("prediction bootstrap queues"),
    Runtime.SubmitResnapshot(MakeSyntheticSnapshot(Generation)),
    ECrowdAsyncSimulationSubmitResult::Accepted);

  auto PollThrough = [&Runtime](const uint64 InputSequence)
  {
    const double Deadline = FPlatformTime::Seconds() + 5.0;
    while (FPlatformTime::Seconds() < Deadline)
    {
      const ECrowdAsyncSimulationPollResult Result = Runtime.Poll();
      if (Result == ECrowdAsyncSimulationPollResult::Failed)
        return false;
      if (Result == ECrowdAsyncSimulationPollResult::Idle
        && Runtime.GetMetrics().LastAppliedInputSequence
          >= InputSequence)
        return true;
      FPlatformProcess::SleepNoStats(0.0f);
    }
    return false;
  };
  TestTrue(TEXT("prediction bootstrap applies"), PollThrough(3));
  const uint64 InitialResnapshotCount =
    Runtime.GetMetrics().ResnapshotCount;
  const uint64 InitialFullMirrorSerializationCount =
    Runtime.GetMetrics().FullMirrorSerializationCount;

  uint64 InputSequence = 4;
  for (uint64 Tick = 2; Tick <= 301; ++Tick, ++InputSequence)
  {
    FCrowdWorkerIntentBatch ClockOnly;
    ClockOnly.Generation = Generation;
    ClockOnly.FirstInputSequence = InputSequence;
    ClockOnly.LastInputSequence = InputSequence;
    ClockOnly.TargetSimulationTimeSeconds =
      static_cast<double>(Tick) / 30.0;
    ClockOnly.Clock.InputSequence = InputSequence;
    ClockOnly.Clock.SimulationTick = Tick;
    ClockOnly.RecalculateStableHash();
    if (Runtime.SubmitIntentBatch(ClockOnly)
        != ECrowdAsyncSimulationSubmitResult::Accepted
      || !PollThrough(InputSequence))
    {
      AddError(FString::Printf(
        TEXT("clock-only prediction failed at tick %llu"), Tick));
      Runtime.StopAndDrain(5.0);
      return false;
    }
  }

  const FCrowdAsyncSimulationRuntimeMetrics Metrics =
    Runtime.GetMetrics();
  TestTrue(TEXT("client predicts at least 300 epochs"),
    Metrics.ConsecutivePredictionEpochsWithoutCorrection >= 300);
  TestEqual(TEXT("normal prediction applies no correction"),
    Metrics.AuthorityCorrectionCount, uint64{0});
  TestEqual(TEXT("prediction does not restart runtime"),
    Metrics.ResnapshotCount, InitialResnapshotCount);
  TestTrue(TEXT("ordinary prediction avoids full mirror serialization"),
    Metrics.FullMirrorSerializationCount
      <= InitialFullMirrorSerializationCount + 1);
  TestEqual(TEXT("all clock intents apply"),
    Metrics.LastAppliedInputSequence, uint64{303});
  FCrowdWorkerMirrorSnapshot Mirror;
  TestTrue(TEXT("predicted world remains readable"),
    Runtime.ReadMirrorSnapshot(Mirror));
  TestEqual(TEXT("predicted entity remains alive"),
    Mirror.EntityRefs.Num(), 1);
  TestTrue(TEXT("prediction runtime stops"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerSparseCorrectionRecoveryTest,
  "MassCrowd.RuntimeV2.SparseCorrectionWithoutWorldRebuild",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerSparseCorrectionRecoveryTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 102;
  FCrowdAsyncSimulationRuntime Authority;
  FCrowdAsyncSimulationRuntime Client;
  TestTrue(TEXT("authority domain registers"),
    Authority.RegisterDomainExecutor(
      MakeUnique<FSyntheticLifecycleDomain>()));
  TestTrue(TEXT("client domain registers"),
    Client.RegisterDomainExecutor(
      MakeUnique<FSyntheticLifecycleDomain>()));
  TestTrue(TEXT("authority correction revision probe registers"),
    Authority.RegisterDomainExecutor(
      MakeUnique<FCorrectionRevisionBehaviorDomain>()));
  TestTrue(TEXT("client correction revision probe registers"),
    Client.RegisterDomainExecutor(
      MakeUnique<FCorrectionRevisionBehaviorDomain>()));
  TestTrue(TEXT("authority starts"),
    Authority.Start(MakeSyntheticConfig(), Generation));
  TestTrue(TEXT("client starts"),
    Client.Start(MakeSyntheticConfig(), Generation));

  FCrowdWorkerIntentBatch AuthoritySnapshot =
    MakeSyntheticSnapshot(Generation);
  FCrowdWorkerIntentBatch ClientSnapshot = AuthoritySnapshot;
  ClientSnapshot.Spawns[0].InitialState = MakePayload(999, 91003);
  ClientSnapshot.RecalculateStableHash();
  TestEqual(TEXT("authority bootstrap queues"),
    Authority.SubmitResnapshot(AuthoritySnapshot),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestEqual(TEXT("corrupted client bootstrap queues"),
    Client.SubmitResnapshot(ClientSnapshot),
    ECrowdAsyncSimulationSubmitResult::Accepted);

  auto PollThrough = [](FCrowdAsyncSimulationRuntime& Runtime,
    const uint64 InputSequence)
  {
    const double Deadline = FPlatformTime::Seconds() + 5.0;
    while (FPlatformTime::Seconds() < Deadline)
    {
      const ECrowdAsyncSimulationPollResult Result = Runtime.Poll();
      if (Result == ECrowdAsyncSimulationPollResult::Failed)
        return false;
      if (Result == ECrowdAsyncSimulationPollResult::Idle
        && Runtime.GetMetrics().LastAppliedInputSequence
          >= InputSequence)
        return true;
      FPlatformProcess::SleepNoStats(0.0f);
    }
    return false;
  };
  TestTrue(TEXT("authority bootstrap applies"),
    PollThrough(Authority, 3));
  TestTrue(TEXT("client bootstrap applies"),
    PollThrough(Client, 3));

  uint64 InputSequence = 4;
  auto AdvanceBoth = [&](const uint64 FirstTick, const uint64 LastTick)
  {
    for (uint64 Tick = FirstTick; Tick <= LastTick;
      ++Tick, ++InputSequence)
    {
      FCrowdWorkerIntentBatch ClockOnly;
      ClockOnly.Generation = Generation;
      ClockOnly.FirstInputSequence = InputSequence;
      ClockOnly.LastInputSequence = InputSequence;
      ClockOnly.TargetSimulationTimeSeconds =
        static_cast<double>(Tick) / 30.0;
      ClockOnly.Clock.InputSequence = InputSequence;
      ClockOnly.Clock.SimulationTick = Tick;
      ClockOnly.RecalculateStableHash();
      if (Authority.SubmitIntentBatch(ClockOnly)
          != ECrowdAsyncSimulationSubmitResult::Accepted
        || Client.SubmitIntentBatch(ClockOnly)
          != ECrowdAsyncSimulationSubmitResult::Accepted
        || !PollThrough(Authority, InputSequence)
        || !PollThrough(Client, InputSequence))
        return false;
    }
    return true;
  };
  TestTrue(TEXT("both runtimes predict through first digest"),
    AdvanceBoth(2, 30));

  FCrowdWorkerAuthorityDigestBatch AuthorityDigest;
  TestEqual(TEXT("authority digest is ready"),
    Authority.ReadAuthorityDigest(Generation, AuthorityDigest),
    ECrowdWorkerNetworkReadResult::Ready);
  TArray<FCrowdWorkerAuthorityScopeKey> Mismatches;
  TestEqual(TEXT("client compares authority digest"),
    Client.CompareAuthorityDigest(AuthorityDigest, Mismatches),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("one corrupted scope is isolated"),
    Mismatches.Num(), 1);
  if (Mismatches.Num() != 1)
  {
    Authority.StopAndDrain(5.0);
    Client.StopAndDrain(5.0);
    return false;
  }

  TestTrue(TEXT("newer digest may publish before correction request"),
    AdvanceBoth(31, 60));

  FCrowdWorkerAuthorityCorrectionBatch Correction;
  TestEqual(TEXT("authority builds sparse correction"),
    Authority.BuildAuthorityCorrection(
      Generation,
      AuthorityDigest.DigestSequence,
      1,
      Mismatches,
      Correction),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("correction contains one scope"),
    Correction.Scopes.Num(), 1);
  TestEqual(TEXT("correction contains one entity"),
    Correction.AuthoritativeMembers.Num(), 1);
  const uint64 ResnapshotCountBefore =
    Client.GetMetrics().ResnapshotCount;
  TestEqual(TEXT("client queues sparse correction"),
    Client.SubmitAuthorityCorrection(Correction),
    ECrowdAsyncSimulationCorrectionResult::Accepted);
  const double CorrectionDeadline = FPlatformTime::Seconds() + 5.0;
  while (FPlatformTime::Seconds() < CorrectionDeadline
    && Client.GetMetrics().AuthorityCorrectionCount == 0)
  {
    if (Client.Poll() == ECrowdAsyncSimulationPollResult::Failed)
      break;
    FPlatformProcess::SleepNoStats(0.0f);
  }
  const FCrowdAsyncSimulationRuntimeMetrics CorrectedMetrics =
    Client.GetMetrics();
  TestEqual(TEXT("one sparse correction applies"),
    CorrectedMetrics.AuthorityCorrectionCount, uint64{1});
  TestEqual(TEXT("correction does not rebuild runtime"),
    CorrectedMetrics.ResnapshotCount, ResnapshotCountBefore);
  TestEqual(TEXT("correction telemetry records one entity"),
    CorrectedMetrics.LastCorrectionEntityCount, 1);

  TestTrue(TEXT("both runtimes continue predicting"),
    AdvanceBoth(61, 90));
  TestEqual(TEXT("authority publishes next digest"),
    Authority.ReadAuthorityDigest(Generation, AuthorityDigest),
    ECrowdWorkerNetworkReadResult::Ready);
  Mismatches.Reset();
  TestEqual(TEXT("post-correction digest compares"),
    Client.CompareAuthorityDigest(AuthorityDigest, Mismatches),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("post-correction digest fully matches"),
    Mismatches.Num(), 0);

  const FCrowdWorkerAuthorityDigestEntry* LifecycleEntry =
    AuthorityDigest.Entries.FindByPredicate([](
      const FCrowdWorkerAuthorityDigestEntry& Entry)
    {
      return Entry.Scope.Field == ECrowdWorkerField::Lifecycle;
    });
  TestNotNull(TEXT("authority digest exposes lifecycle scope"),
    LifecycleEntry);
  if (!LifecycleEntry)
  {
    Authority.StopAndDrain(5.0);
    Client.StopAndDrain(5.0);
    return false;
  }
  const TArray<FCrowdWorkerAuthorityScopeKey> LifecycleScopes{
    LifecycleEntry->Scope};
  FCrowdWorkerAuthorityCorrectionBatch LifecycleCorrection;
  TestEqual(TEXT("authority builds lifecycle correction"),
    Authority.BuildAuthorityCorrection(
      Generation,
      AuthorityDigest.DigestSequence,
      2,
      LifecycleScopes,
      LifecycleCorrection),
    ECrowdWorkerNetworkReadResult::Ready);
  TestTrue(TEXT("client opens ordered correction barrier"),
    Client.BeginAuthorityCorrectionBarrier(
      Generation,
      LifecycleCorrection.ApplySimulationTick,
      LifecycleCorrection.ThroughInputSequence));
  TestEqual(TEXT("client queues lifecycle correction"),
    Client.SubmitAuthorityCorrection(LifecycleCorrection),
    ECrowdAsyncSimulationCorrectionResult::Accepted);
  const double LifecycleCorrectionDeadline =
    FPlatformTime::Seconds() + 5.0;
  while (FPlatformTime::Seconds() < LifecycleCorrectionDeadline
    && Client.GetMetrics().AuthorityCorrectionCount < 2)
  {
    if (Client.Poll() == ECrowdAsyncSimulationPollResult::Failed)
      break;
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestEqual(TEXT("lifecycle correction applies"),
    Client.GetMetrics().AuthorityCorrectionCount, uint64{2});
  TestTrue(TEXT("corrected dependent domain continues predicting"),
    AdvanceBoth(91, 120));
  TestEqual(TEXT("client remains running after corrected output"),
    Client.GetState(), ECrowdAsyncSimulationRuntimeState::Running);
  TestEqual(TEXT("corrected output preserves runtime failure invariant"),
    Client.GetMetrics().WorkerV2.LastFailure,
    ECrowdWorkerRuntimeV2Failure::None);
  TestEqual(TEXT("authority publishes digest after corrected output"),
    Authority.ReadAuthorityDigest(Generation, AuthorityDigest),
    ECrowdWorkerNetworkReadResult::Ready);
  Mismatches.Reset();
  TestEqual(TEXT("client compares digest after corrected output"),
    Client.CompareAuthorityDigest(AuthorityDigest, Mismatches),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("corrected dependent output remains semantically equal"),
    Mismatches.Num(), 0);
  TestTrue(TEXT("authority stops"),
    Authority.StopAndDrain(5.0));
  TestTrue(TEXT("client stops"), Client.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerWorkRingScalingTest,
  "MassCrowd.RuntimeV2.Complexity.WorkRingScaling",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerWorkRingScalingTest::RunTest(
  const FString& Parameters)
{
  using namespace CrowdWorkerRuntimeV2Tests;
  const int32 Counts[] = {1000, 2000, 5000, 10000};
  const uint64 MaximumProbesPerPop =
    static_cast<uint64>(ECrowdWorkerWorkPriority::Count)
      * static_cast<uint64>(ECrowdWorkerDomainId::Count);
  for (const int32 Count : Counts)
  {
    FCrowdWorkerWorkRing Ring;
    TestTrue(*FString::Printf(TEXT("reset %d ring"), Count),
      Ring.Reset(Count + 16, 1));
    for (int32 Index = Count - 1; Index >= 0; --Index)
    {
      const ECrowdWorkerDomainId Domain =
        static_cast<ECrowdWorkerDomainId>(
          Index % static_cast<int32>(ECrowdWorkerDomainId::Count));
      const ECrowdWorkerQueueResult Result = Ring.EnqueueCurrent(
        MakeEntityWork(Domain, static_cast<uint64>(Index + 1)));
      if (Result != ECrowdWorkerQueueResult::Added)
      {
        AddError(FString::Printf(
          TEXT("enqueue rejected at count=%d index=%d"), Count, Index));
        return false;
      }
    }
    int32 Drained = 0;
    FCrowdWorkerWorkItem Item;
    while (Ring.PopCurrent(Item)) ++Drained;
    TestEqual(*FString::Printf(TEXT("drain %d work items"), Count),
      Drained, Count);
    const FCrowdWorkerWorkRingStats Stats = Ring.GetStats();
    TestTrue(*FString::Printf(TEXT("%d drain uses bounded bucket probes"),
      Count), Stats.PopBucketProbeCount
        <= static_cast<uint64>(Count) * MaximumProbesPerPop);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerSparseTimeWheelScalingTest,
  "MassCrowd.RuntimeV2.Complexity.SparseTimeWheel",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerSparseTimeWheelScalingTest::RunTest(
  const FString& Parameters)
{
  FCrowdWorkerTimeWheel Wheel;
  TestTrue(TEXT("time wheel reset"), Wheel.Reset(10016));
  for (uint64 Index = 0; Index < 10000; ++Index)
  {
    FCrowdWorkerWakeup Wakeup;
    Wakeup.Key.Domain = ECrowdWorkerDomainId::Behavior;
    Wakeup.Key.EntityRef = {1, Index + 1, 1};
    Wakeup.Key.WakeupId = Index + 1;
    Wakeup.AbsoluteSimulationTick = 1000 + Index;
    Wakeup.Revision = 1;
    Wakeup.ReasonMask = 1;
    if (Wheel.Schedule(MoveTemp(Wakeup))
      != ECrowdWorkerQueueResult::Added)
      return false;
  }
  TArray<FCrowdWorkerWakeup> Due;
  TestEqual(TEXT("no early wakeups"), Wheel.DrainDue(10, Due), 0);
  TestEqual(TEXT("no future buckets scanned"),
    Wheel.GetScannedBucketCount(), uint64{0});
  FCrowdWorkerWakeup Immediate;
  Immediate.Key.Domain = ECrowdWorkerDomainId::Behavior;
  Immediate.Key.EntityRef = {2, 1, 1};
  Immediate.Key.WakeupId = 10001;
  Immediate.AbsoluteSimulationTick = 5;
  Immediate.Revision = 1;
  Immediate.ReasonMask = 1;
  TestEqual(TEXT("out-of-order minimum tick schedules"),
    Wheel.Schedule(MoveTemp(Immediate)),
    ECrowdWorkerQueueResult::Added);
  TestEqual(TEXT("only immediate wakeup drains"),
    Wheel.DrainDue(5, Due), 1);
  TestEqual(TEXT("only due bucket scanned"),
    Wheel.GetScannedBucketCount(), uint64{1});
  TestEqual(TEXT("six sparse future buckets drain"),
    Wheel.DrainDue(1005, Due), 6);
  TestEqual(TEXT("scans equal actual due buckets"),
    Wheel.GetScannedBucketCount(), uint64{7});
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerSpatialIncrementalMigrationTest,
  "MassCrowd.RuntimeV2.Complexity.SpatialIncrementalMigration",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerSpatialIncrementalMigrationTest::RunTest(
  const FString& Parameters)
{
  using namespace CrowdWorkerRuntimeV2Tests;
  constexpr int32 EntityCount = 10000;
  constexpr int32 GridWidth = 100;
  const auto RunMigrationCase = [this](const int32 CrossingPercent)
  {
    FCrowdWorkerEntityStateStore States;
    FCrowdWorkerSpatialIndex Index;
    if (!States.Reset(EntityCount, 65536)
      || !Index.Reset(EntityCount, 400.0f))
    {
      AddError(FString::Printf(
        TEXT("10k spatial reset rejected crossing_percent=%d"),
        CrossingPercent));
      return false;
    }
    const int32 CrossingCount =
      EntityCount * CrossingPercent / 100;
    const auto InitialPosition = [](const int32 EntityIndex)
    {
      return FVector(
        static_cast<double>(EntityIndex % GridWidth) * 20.0,
        static_cast<double>(EntityIndex / GridWidth) * 20.0,
        0.0);
    };
    for (int32 EntityIndex = 0;
      EntityIndex < EntityCount; ++EntityIndex)
    {
      const FCrowdStableEntityRef EntityRef = {
        1, static_cast<uint64>(EntityIndex + 1), 1};
      const ECrowdWorkerQueueResult SpawnResult = States.Spawn(
        EntityRef, 1, EntityIndex + 1,
        MakeBoundaryStatePayload(
          EntityRef, EntityIndex, InitialPosition(EntityIndex)));
      if (SpawnResult != ECrowdWorkerQueueResult::Added
        || !Index.Spawn(States, EntityRef))
      {
        AddError(FString::Printf(
          TEXT("10k spatial spawn rejected percent=%d index=%d result=%d"),
          CrossingPercent, EntityIndex,
          static_cast<int32>(SpawnResult)));
        return false;
      }
    }
    for (int32 EntityIndex = 0;
      EntityIndex < EntityCount; ++EntityIndex)
    {
      const FCrowdStableEntityRef EntityRef = {
        1, static_cast<uint64>(EntityIndex + 1), 1};
      FCrowdWorkerMovementState Movement;
      Movement.Position = InitialPosition(EntityIndex)
        + FVector(EntityIndex < CrossingCount ? 400.0 : 1.0, 0.0, 0.0);
      FCrowdWorkerDirtyStateRecord Dirty;
      Dirty.EntityRef = EntityRef;
      Dirty.Field = ECrowdWorkerField::Movement;
      Dirty.Generation = 1;
      Dirty.WorkerEpoch = 1;
      Dirty.StateRevision = 1;
      Dirty.SourceInputSequence = EntityIndex + 1;
      if (!FCrowdWorkerMovementStateCodec::Encode(
          Movement, Dirty.Payload))
      {
        AddError(FString::Printf(
          TEXT("10k movement encode rejected percent=%d index=%d"),
          CrossingPercent, EntityIndex));
        return false;
      }
      const ECrowdWorkerQueueResult DirtyResult =
        States.ApplyDirty(Dirty);
      if (DirtyResult != ECrowdWorkerQueueResult::Replaced
        || !Index.UpdateEntity(States, EntityRef))
      {
        AddError(FString::Printf(
          TEXT("10k spatial update rejected percent=%d index=%d result=%d"),
          CrossingPercent, EntityIndex,
          static_cast<int32>(DirtyResult)));
        return false;
      }
    }
    TestEqual(*FString::Printf(
        TEXT("10k %d%% case retains every entity"), CrossingPercent),
      Index.Num(), EntityCount);
    TestEqual(*FString::Printf(
        TEXT("10k %d%% case performs no full rebuild"), CrossingPercent),
      Index.GetFullRebuildCount(), uint64{0});
    TestEqual(*FString::Printf(
        TEXT("10k %d%% case uses incremental spawn and update"),
        CrossingPercent),
      Index.GetIncrementalUpdateCount(),
      static_cast<uint64>(EntityCount * 2));
    TestEqual(*FString::Printf(
        TEXT("10k %d%% case migrates only cross-cell entities"),
        CrossingPercent),
      Index.GetCellMigrationCount(),
      static_cast<uint64>(CrossingCount));
    return true;
  };
  return RunMigrationCase(1) && RunMigrationCase(10);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerCleanBarrierCorrectionTest,
  "MassCrowd.RuntimeV2.CorrectionRecovery.CleanBarrierCorrection",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerCleanBarrierCorrectionTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 201;
  FCrowdAsyncSimulationRuntime Runtime;
  TSharedPtr<TAtomic<int32>, ESPMode::ThreadSafe>
    CorrectionApplyCount = MakeShared<
      TAtomic<int32>, ESPMode::ThreadSafe>(0);
  TestTrue(TEXT("runtime starts"),
    StartCorrectionRuntime(
      Runtime, Generation, false, CorrectionApplyCount));
  TestEqual(TEXT("bootstrap queues"),
    Runtime.SubmitResnapshot(MakeSyntheticSnapshot(Generation)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("bootstrap reaches clean barrier"),
    WaitForRuntimeIdle(Runtime));
  const FCrowdAsyncSimulationRuntimeMetrics Before =
    Runtime.GetMetrics();
  const FCrowdWorkerAuthorityCorrectionBatch Correction =
    MakeBehaviorCorrection(Generation, 1, Before);
  TestTrue(TEXT("correction barrier opens"),
    Runtime.BeginAuthorityCorrectionBarrier(
      Generation, Correction.ApplySimulationTick,
      Correction.ThroughInputSequence));
  TestEqual(TEXT("correction queues"),
    Runtime.SubmitAuthorityCorrection(Correction),
    ECrowdAsyncSimulationCorrectionResult::Accepted);
  TestTrue(TEXT("clean barrier correction applies"),
    WaitForCorrectionCount(Runtime, 1));
  const FCrowdAsyncSimulationRuntimeMetrics After =
    Runtime.GetMetrics();
  TestEqual(TEXT("runtime remains running"), Runtime.GetState(),
    ECrowdAsyncSimulationRuntimeState::Running);
  TestEqual(TEXT("correction has no worker failure"),
    After.WorkerV2.LastFailure,
    ECrowdWorkerRuntimeV2Failure::None);
  TestEqual(TEXT("correction avoids resnapshot"),
    After.ResnapshotCount, Before.ResnapshotCount);
  TestEqual(TEXT("stateful domains rebase at correction barrier"),
    CorrectionApplyCount->Load(), 1);
  TestTrue(TEXT("runtime stops"), Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerCorrectionBeforeNextInputTest,
  "MassCrowd.RuntimeV2.CorrectionRecovery.CorrectionBeforeNextInput",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerCorrectionBeforeNextInputTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 202;
  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(TEXT("runtime starts"),
    StartCorrectionRuntime(Runtime, Generation));
  TestEqual(TEXT("bootstrap queues"),
    Runtime.SubmitResnapshot(MakeSyntheticSnapshot(Generation)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("bootstrap applies"), WaitForRuntimeIdle(Runtime));
  const FCrowdWorkerAuthorityCorrectionBatch Correction =
    MakeBehaviorCorrection(Generation, 1, Runtime.GetMetrics());
  TestTrue(TEXT("barrier opens"), Runtime.BeginAuthorityCorrectionBarrier(
    Generation, Correction.ApplySimulationTick,
    Correction.ThroughInputSequence));
  TestEqual(TEXT("correction queues"),
    Runtime.SubmitAuthorityCorrection(Correction),
    ECrowdAsyncSimulationCorrectionResult::Accepted);
  TestTrue(TEXT("correction applies"),
    WaitForCorrectionCount(Runtime, 1));
  FCrowdWorkerIntentBatch Next = MakeClockIntent(Generation, 4, 2);
  TestEqual(TEXT("next input queues"), Runtime.SubmitIntentBatch(Next),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("next input applies after correction"),
    WaitForRuntimeIdle(Runtime));
  const FCrowdAsyncSimulationRuntimeMetrics Metrics =
    Runtime.GetMetrics();
  TestEqual(TEXT("next input watermark"),
    Metrics.LastAppliedInputSequence, uint64{4});
  TestEqual(TEXT("resume_before_input remains healthy"),
    Metrics.WorkerV2.LastFailure,
    ECrowdWorkerRuntimeV2Failure::None);
  TestTrue(TEXT("runtime stops"), Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerPendingWorkCorrectionTest,
  "MassCrowd.RuntimeV2.CorrectionRecovery.PendingWorkCorrection",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerPendingWorkCorrectionTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 203;
  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(TEXT("slow runtime starts"),
    StartCorrectionRuntime(Runtime, Generation, true));
  TestEqual(TEXT("bootstrap queues"),
    Runtime.SubmitResnapshot(MakeSyntheticSnapshot(Generation)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  const double InFlightDeadline = FPlatformTime::Seconds() + 5.0;
  FCrowdAsyncSimulationRuntimeMetrics InFlight;
  while (FPlatformTime::Seconds() < InFlightDeadline)
  {
    Runtime.Poll();
    InFlight = Runtime.GetMetrics();
    if (InFlight.WorkerV2.ShardInFlightCount > 0
      && InFlight.AbsoluteSimulationTick > 0)
      break;
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestTrue(TEXT("pending shard is observed"),
    InFlight.WorkerV2.ShardInFlightCount > 0);
  const FCrowdWorkerAuthorityCorrectionBatch Correction =
    MakeBehaviorCorrection(Generation, 1, InFlight);
  TestTrue(TEXT("barrier opens while shard is pending"),
    Runtime.BeginAuthorityCorrectionBarrier(
      Generation, Correction.ApplySimulationTick,
      Correction.ThroughInputSequence));
  TestEqual(TEXT("pending-work correction queues"),
    Runtime.SubmitAuthorityCorrection(Correction),
    ECrowdAsyncSimulationCorrectionResult::Accepted);
  TestTrue(TEXT("barrier waits and correction applies"),
    WaitForCorrectionCount(Runtime, 1));
  const FCrowdAsyncSimulationRuntimeMetrics After =
    Runtime.GetMetrics();
  TestEqual(TEXT("pending correction does not resnapshot"),
    After.ResnapshotCount, InFlight.ResnapshotCount);
  TestEqual(TEXT("pending correction keeps runtime running"),
    Runtime.GetState(), ECrowdAsyncSimulationRuntimeState::Running);
  TestEqual(TEXT("pending correction has no failure"),
    After.WorkerV2.LastFailure,
    ECrowdWorkerRuntimeV2Failure::None);
  TestTrue(TEXT("runtime stops"), Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerStaleDirtyAfterCorrectionTest,
  "MassCrowd.RuntimeV2.CorrectionRecovery.StaleDirtyAfterCorrection",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerStaleDirtyAfterCorrectionTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdWorkerEntityStateStore Store;
  TestTrue(TEXT("state store resets"), Store.Reset(4, 64));
  const FCrowdStableEntityRef EntityRef{1, 42, 1};
  TestEqual(TEXT("entity spawns"),
    Store.Spawn(EntityRef, 204, 1, MakePayload(42, 91003)),
    ECrowdWorkerQueueResult::Added);
  FCrowdWorkerDirtyStateRecord Authority;
  Authority.EntityRef = EntityRef;
  Authority.Field = ECrowdWorkerField::Behavior;
  Authority.Generation = 204;
  Authority.WorkerEpoch = 10;
  Authority.StateRevision = 10;
  Authority.CorrectionRevision = 3;
  Authority.SourceInputSequence = 10;
  Authority.Payload = MakePayload(42, 91007);
  TestTrue(TEXT("authoritative revision installs"),
    Store.ApplyAuthoritativeDirty(Authority));
  FCrowdWorkerDirtyStateRecord Stale = Authority;
  Stale.WorkerEpoch = 11;
  Stale.StateRevision = 11;
  Stale.CorrectionRevision = 2;
  Stale.Payload = MakePayload(43, 91007);
  TestEqual(TEXT("older correction fence is classified stale"),
    Store.ApplyDirty(Stale),
    ECrowdWorkerQueueResult::RejectedStale);
  FCrowdWorkerDirtyStateRecord Conflict = Authority;
  Conflict.Payload = MakePayload(44, 91007);
  TestEqual(TEXT("same revision conflicting payload fails closed"),
    Store.ApplyDirty(Conflict), ECrowdWorkerQueueResult::Conflict);
  FCrowdWorkerDirtyStateRecord FutureGeneration = Authority;
  FutureGeneration.Generation = 205;
  FutureGeneration.Payload = MakePayload(45, 91007);
  TestEqual(TEXT("future generation is not treated as legal stale"),
    Store.ApplyDirty(FutureGeneration),
    ECrowdWorkerQueueResult::Conflict);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerRepeatedCorrectionTest,
  "MassCrowd.RuntimeV2.CorrectionRecovery.RepeatedCorrection",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerRepeatedCorrectionTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 205;
  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(TEXT("runtime starts"),
    StartCorrectionRuntime(Runtime, Generation));
  TestEqual(TEXT("bootstrap queues"),
    Runtime.SubmitResnapshot(MakeSyntheticSnapshot(Generation)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("bootstrap applies"), WaitForRuntimeIdle(Runtime));
  uint64 InputSequence = 4;
  uint64 Tick = 2;
  for (uint64 Sequence = 1; Sequence <= 3; ++Sequence)
  {
    const FCrowdWorkerAuthorityCorrectionBatch Correction =
      MakeBehaviorCorrection(Generation, Sequence,
        Runtime.GetMetrics());
    TestTrue(*FString::Printf(TEXT("barrier %llu opens"), Sequence),
      Runtime.BeginAuthorityCorrectionBarrier(
        Generation, Correction.ApplySimulationTick,
        Correction.ThroughInputSequence));
    TestEqual(*FString::Printf(TEXT("correction %llu queues"), Sequence),
      Runtime.SubmitAuthorityCorrection(Correction),
      ECrowdAsyncSimulationCorrectionResult::Accepted);
    TestTrue(*FString::Printf(TEXT("correction %llu applies"), Sequence),
      WaitForCorrectionCount(Runtime, Sequence));
    FCrowdWorkerIntentBatch Next = MakeClockIntent(
      Generation, InputSequence++, Tick++);
    TestEqual(*FString::Printf(TEXT("prediction %llu queues"), Sequence),
      Runtime.SubmitIntentBatch(Next),
      ECrowdAsyncSimulationSubmitResult::Accepted);
    TestTrue(*FString::Printf(TEXT("prediction %llu applies"), Sequence),
      WaitForRuntimeIdle(Runtime));
  }
  const FCrowdAsyncSimulationRuntimeMetrics Metrics =
    Runtime.GetMetrics();
  TestEqual(TEXT("three corrections apply"),
    Metrics.AuthorityCorrectionCount, uint64{3});
  TestEqual(TEXT("runtime remains running"), Runtime.GetState(),
    ECrowdAsyncSimulationRuntimeState::Running);
  TestEqual(TEXT("repeated corrections have no failure"),
    Metrics.WorkerV2.LastFailure,
    ECrowdWorkerRuntimeV2Failure::None);
  TestTrue(TEXT("runtime stops"), Runtime.StopAndDrain(5.0));
  return true;
}

#endif
