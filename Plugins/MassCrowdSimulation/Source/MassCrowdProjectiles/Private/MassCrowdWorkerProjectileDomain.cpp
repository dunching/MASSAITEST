#include "MassCrowdWorkerProjectileDomain.h"

#include "Misc/ScopeLock.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace CrowdWorkerProjectilePrivate
{
  constexpr int32 MaxArrayCount = 100000;

  void SerializeRef(FArchive& Ar, FCrowdStableEntityRef& Ref)
  {
    Ar << Ref.ProviderId;
    Ar << Ref.StableEntityId;
    Ar << Ref.LifecycleSerial;
  }

  void SerializeBehaviorPayload(
    FArchive& Ar,
    FCrowdBehaviorSourcePayload& Payload)
  {
    Ar << Payload.SchemaId;
    Ar << Payload.Size;
    if (Ar.IsLoading()
      && Payload.Size > CrowdBehavior::MaxPayloadBytes)
    {
      Ar.SetError();
      return;
    }
    Ar.Serialize(Payload.Bytes, Payload.Size);
    if (Ar.IsLoading() && Payload.Size < CrowdBehavior::MaxPayloadBytes)
    {
      FMemory::Memzero(
        Payload.Bytes + Payload.Size,
        CrowdBehavior::MaxPayloadBytes - Payload.Size);
    }
  }

  void SerializeWorkerPayload(
    FArchive& Ar,
    FCrowdWorkerPayload& Payload)
  {
    Ar << Payload.SchemaId;
    Ar << Payload.SchemaVersion;
    Ar << Payload.Bytes;
    Ar << Payload.StableHash;
  }

  template<typename T, typename F>
  void SerializeArray(FArchive& Ar, TArray<T>& Values, F SerializeValue)
  {
    int32 Count = Values.Num();
    Ar << Count;
    if (Count < 0 || Count > MaxArrayCount)
    {
      Ar.SetError();
      return;
    }
    if (Ar.IsLoading()) Values.SetNum(Count);
    for (T& Value : Values)
      SerializeValue(Ar, Value);
  }

  void SerializeProfile(FArchive& Ar, FCrowdProjectileProfile& Value)
  {
    Ar << Value.ProfileId;
    Ar << Value.RadiusCm;
    Ar << Value.LifetimeFixedSteps;
    Ar << Value.PierceCount;
    Ar << Value.MaxActiveProjectiles;
    Ar << Value.PositionQuantumCm;
    Ar << Value.VelocityQuantumCmps;
    Ar << Value.GridCellSizeCm;
    Ar << Value.CollisionMask;
    Ar << Value.QueryMask;
    Ar << Value.StableHash;
  }

  void SerializeSpawn(
    FArchive& Ar,
    FCrowdProjectileSpawnRequest& Value)
  {
    Ar << Value.ProjectileId;
    Ar << Value.FixedStepIndex;
    SerializeRef(Ar, Value.Instigator);
    SerializeRef(Ar, Value.Target);
    Ar << Value.FireSequence;
    Ar << Value.SourceFactionId;
    Ar << Value.NavLayer;
    Ar << Value.ProjectileProfileId;
    Ar << Value.CollisionProfileId;
    Ar << Value.EffectProfileId;
    Ar << Value.Position;
    Ar << Value.Velocity;
    Ar << Value.StableHash;
  }

  void SerializeTarget(
    FArchive& Ar,
    FCrowdProjectileTargetSnapshot& Value)
  {
    SerializeRef(Ar, Value.EntityRef);
    Ar << Value.FactionId;
    Ar << Value.NavLayer;
    Ar << Value.PreviousPosition;
    Ar << Value.Position;
    Ar << Value.RadiusCm;
    Ar << Value.CollisionMask;
    Ar << Value.QueryMask;
    uint8 Alive = Value.bAlive ? 1 : 0;
    Ar << Alive;
    if (Ar.IsLoading()) Value.bAlive = Alive != 0;
    Ar << Value.StableHash;
  }

  void SerializeEnvironment(
    FArchive& Ar,
    FCrowdSpatialEnvironmentBody& Value)
  {
    Ar << Value.StableSurfaceId;
    Ar << Value.NavLayer;
    Ar << Value.BoundsMin;
    Ar << Value.BoundsMax;
    Ar << Value.CollisionMask;
    Ar << Value.QueryMask;
    Ar << Value.CollisionProfileId;
    Ar << Value.EffectProfileId;
    Ar << Value.StableHash;
  }

  void SerializeProjectileState(
    FArchive& Ar,
    FCrowdProjectileState& Value)
  {
    Ar << Value.ProjectileId;
    SerializeRef(Ar, Value.Instigator);
    SerializeRef(Ar, Value.Target);
    Ar << Value.FireSequence;
    Ar << Value.SpawnFixedStep;
    Ar << Value.AgeFixedSteps;
    Ar << Value.RemainingPierces;
    SerializeRef(Ar, Value.LastHitTarget);
    Ar << Value.SourceFactionId;
    Ar << Value.NavLayer;
    Ar << Value.ProjectileProfileId;
    Ar << Value.CollisionProfileId;
    Ar << Value.EffectProfileId;
    Ar << Value.PreviousPosition;
    Ar << Value.Position;
    Ar << Value.Velocity;
    Ar << Value.RadiusCm;
    uint8 Flags = (Value.bActive ? 1 : 0)
      | (Value.bImpacted ? 2 : 0)
      | (Value.bExpired ? 4 : 0);
    Ar << Flags;
    if (Ar.IsLoading())
    {
      Value.bActive = (Flags & 1) != 0;
      Value.bImpacted = (Flags & 2) != 0;
      Value.bExpired = (Flags & 4) != 0;
    }
  }

  void SerializeEffect(FArchive& Ar, FCrowdEffectProfile& Value)
  {
    Ar << Value.EffectProfileId;
    Ar << Value.PayloadTypeId;
    SerializeBehaviorPayload(Ar, Value.Payload);
    Ar << Value.StableHash;
  }

  void SerializeImpact(FArchive& Ar, FCrowdImpactFact& Value)
  {
    Ar << Value.ImpactId;
    Ar << Value.ImpactTypeId;
    Ar << Value.FixedStepIndex;
    SerializeRef(Ar, Value.Instigator);
    SerializeRef(Ar, Value.Target);
    Ar << Value.Position;
    Ar << Value.Normal;
    Ar << Value.CollisionProfileId;
    Ar << Value.EffectProfileId;
    Ar << Value.TimeOfImpactQ;
    Ar << Value.StableHash;
  }

  void SerializeHit(FArchive& Ar, FCrowdHitFact& Value)
  {
    SerializeImpact(Ar, Value.Impact);
    Ar << Value.PayloadTypeId;
    SerializeBehaviorPayload(Ar, Value.Payload);
    Ar << Value.StableHash;
  }

  void SerializeLifecycle(
    FArchive& Ar,
    FCrowdProjectileLifecycleEvent& Value)
  {
    uint8 Kind = static_cast<uint8>(Value.Kind);
    Ar << Kind;
    if (Ar.IsLoading())
    {
      if (Kind > static_cast<uint8>(
          ECrowdProjectileLifecycleEventKind::Expire))
      {
        Ar.SetError();
        return;
      }
      Value.Kind =
        static_cast<ECrowdProjectileLifecycleEventKind>(Kind);
    }
    Ar << Value.ProjectileId;
    Ar << Value.FixedStepIndex;
    Ar << Value.ServerTimeSeconds;
    Ar << Value.Position;
    Ar << Value.Velocity;
    Ar << Value.RadiusCm;
  }

  void SerializeSummary(
    FArchive& Ar,
    FCrowdProjectileStepSummary& Value)
  {
    uint8 Valid = Value.bValid ? 1 : 0;
    Ar << Valid;
    if (Ar.IsLoading()) Value.bValid = Valid != 0;
    Ar << Value.SpawnedCount;
    Ar << Value.ActiveCount;
    Ar << Value.ImpactedCount;
    Ar << Value.ExpiredCount;
    Ar << Value.DuplicateFireCount;
    Ar << Value.InvalidProjectileCount;
    Ar << Value.EnvironmentImpactCount;
    Ar << Value.BroadphaseCandidateCount;
    Ar << Value.SweepTestCount;
    Ar << Value.ProjectileStateHash;
    Ar << Value.EventHash;
  }

  void SerializeBoundaryInput(
    FArchive& Ar,
    FCrowdProjectileBoundaryInput& Value)
  {
    Ar << Value.FixedStepIndex;
    Ar << Value.ServerTimeSeconds;
    Ar << Value.FixedStepSeconds;
    SerializeArray(Ar, Value.Profiles, SerializeProfile);
    SerializeArray(Ar, Value.SpawnRequests, SerializeSpawn);
    SerializeArray(Ar, Value.Targets, SerializeTarget);
    SerializeArray(Ar, Value.EnvironmentBodies, SerializeEnvironment);
    SerializeArray(Ar, Value.CurrentStates, SerializeProjectileState);
  }

  void SerializePrepared(
    FArchive& Ar,
    FCrowdPreparedProjectileBoundary& Value)
  {
    Ar << Value.FixedStepIndex;
    Ar << Value.BaseStateHash;
    SerializeArray(Ar, Value.States, SerializeProjectileState);
    SerializeArray(Ar, Value.Impacts, SerializeImpact);
    SerializeArray(Ar, Value.Events, SerializeLifecycle);
    SerializeSummary(Ar, Value.Summary);
    Ar << Value.StableHash;
  }

  void SerializeResolved(
    FArchive& Ar,
    FCrowdHitResolveResult& Value)
  {
    Ar << Value.FixedStepIndex;
    SerializeArray(Ar, Value.Hits, SerializeHit);
    Ar << Value.EnvironmentImpactCount;
    Ar << Value.StableHash;
  }

  bool FinalizeEncodedPayload(
    const uint32 SchemaId,
    const uint16 SchemaVersion,
    TArray<uint8>&& Bytes,
    FCrowdWorkerPayload& OutPayload)
  {
    if (Bytes.IsEmpty()
      || Bytes.Num()
        > FCrowdWorkerProjectileControlResourceCodec::
          MaxEncodedBytes)
      return false;
    OutPayload = {};
    OutPayload.SchemaId = SchemaId;
    OutPayload.SchemaVersion = SchemaVersion;
    OutPayload.Bytes = MoveTemp(Bytes);
    OutPayload.RecalculateStableHash();
    return OutPayload.StableHash != 0;
  }

  bool ValidatePayloadHeader(
    const FCrowdWorkerPayload& Payload,
    const uint32 SchemaId,
    const uint16 SchemaVersion)
  {
    if (Payload.SchemaId != SchemaId
      || Payload.SchemaVersion != SchemaVersion
      || Payload.Bytes.IsEmpty()
      || Payload.Bytes.Num()
        > FCrowdWorkerProjectileControlResourceCodec::
          MaxEncodedBytes)
      return false;
    FCrowdWorkerPayload Copy = Payload;
    Copy.RecalculateStableHash();
    return Copy.StableHash == Payload.StableHash;
  }

  bool EncodeEventPayload(
    const uint32 SchemaId,
    TFunctionRef<void(FArchive&)> Serialize,
    FCrowdWorkerPayload& OutPayload)
  {
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes, true);
    Serialize(Writer);
    if (Writer.IsError()) return false;
    return FinalizeEncodedPayload(
      SchemaId, 1, MoveTemp(Bytes), OutPayload);
  }

  uint64 MakeLifecycleEventId(
    const FCrowdProjectileLifecycleEvent& Event)
  {
    uint64 Hash = 14695981039346656037ull;
    const auto Fold = [&Hash](const uint64 Value)
    {
      for (int32 Byte = 0; Byte < 8; ++Byte)
      {
        Hash ^= static_cast<uint8>(Value >> (Byte * 8));
        Hash *= 1099511628211ull;
      }
    };
    Fold(CrowdWorkerProjectileEventTypeIds::Lifecycle);
    Fold(static_cast<uint8>(Event.Kind));
    Fold(Event.ProjectileId);
    Fold(static_cast<uint64>(Event.FixedStepIndex));
    return Hash == 0 ? 1 : Hash;
  }
}

using namespace CrowdWorkerProjectilePrivate;

bool FCrowdWorkerProjectileControlResource::IsValid() const
{
  if (Revision == 0 || !AnchorEntity.IsValid()
    || Input.FixedStepIndex < 0
    || !FMath::IsFinite(Input.ServerTimeSeconds)
    || !FMath::IsFinite(Input.FixedStepSeconds)
    || Input.FixedStepSeconds <= 0.0f
    || Input.Profiles.IsEmpty())
    return false;
  for (const FCrowdProjectileProfile& Profile : Input.Profiles)
    if (!Profile.IsValid()) return false;
  for (const FCrowdProjectileSpawnRequest& Spawn :
    Input.SpawnRequests)
    if (!Spawn.IsValid()
      || Spawn.FixedStepIndex != Input.FixedStepIndex)
      return false;
  for (const FCrowdProjectileTargetSnapshot& Target : Input.Targets)
    if (!Target.IsValid()) return false;
  for (const FCrowdSpatialEnvironmentBody& Body :
    Input.EnvironmentBodies)
    if (!Body.IsValid()) return false;
  for (const FCrowdProjectileState& State : Input.CurrentStates)
    if (!State.IsValid()) return false;
  for (const FCrowdEffectProfile& Effect : EffectProfiles)
    if (!Effect.IsValid()) return false;
  if (!HostCombatInput.Bytes.IsEmpty()
    && (HostCombatInput.SchemaId == 0
      || HostCombatInput.SchemaVersion == 0
      || HostCombatInput.StableHash
        != HostCombatInput.CalculateStableHash()))
    return false;
  return bReplaceState || Input.CurrentStates.IsEmpty();
}

bool FCrowdWorkerProjectileControlResourceCodec::Encode(
  const FCrowdWorkerProjectileControlResource& Resource,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Resource.IsValid()) return false;
  TArray<uint8> Bytes;
  FMemoryWriter Writer(Bytes, true);
  uint64 Revision = Resource.Revision;
  FCrowdStableEntityRef Anchor = Resource.AnchorEntity;
  FCrowdProjectileBoundaryInput Input = Resource.Input;
  TArray<FCrowdEffectProfile> Effects = Resource.EffectProfiles;
  FCrowdWorkerPayload HostCombatInput = Resource.HostCombatInput;
  uint8 Replace = Resource.bReplaceState ? 1 : 0;
  Writer << Revision;
  SerializeRef(Writer, Anchor);
  SerializeBoundaryInput(Writer, Input);
  SerializeArray(Writer, Effects, SerializeEffect);
  SerializeWorkerPayload(Writer, HostCombatInput);
  Writer << Replace;
  if (Writer.IsError()) return false;
  return FinalizeEncodedPayload(
    SchemaId, SchemaVersion, MoveTemp(Bytes), OutPayload);
}

bool FCrowdWorkerProjectileControlResourceCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerProjectileControlResource& OutResource)
{
  OutResource = {};
  if (!ValidatePayloadHeader(Payload, SchemaId, SchemaVersion))
    return false;
  FMemoryReader Reader(Payload.Bytes, true);
  uint8 Replace = 0;
  Reader << OutResource.Revision;
  SerializeRef(Reader, OutResource.AnchorEntity);
  SerializeBoundaryInput(Reader, OutResource.Input);
  SerializeArray(Reader, OutResource.EffectProfiles, SerializeEffect);
  SerializeWorkerPayload(Reader, OutResource.HostCombatInput);
  Reader << Replace;
  OutResource.bReplaceState = Replace != 0;
  return !Reader.IsError()
    && Reader.AtEnd()
    && Replace <= 1
    && OutResource.IsValid();
}

bool FCrowdWorkerProjectileState::IsValid() const
{
  return ControlRevision != 0
    && Prepared.IsValid()
    && ResolvedHits.IsValid()
    && (HostCombatResult.Bytes.IsEmpty()
      || (HostCombatResult.SchemaId != 0
        && HostCombatResult.SchemaVersion != 0
        && HostCombatResult.StableHash
          == HostCombatResult.CalculateStableHash()));
}

bool FCrowdWorkerProjectileStateCodec::Encode(
  const FCrowdWorkerProjectileState& State,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!State.IsValid()) return false;
  TArray<uint8> Bytes;
  FMemoryWriter Writer(Bytes, true);
  uint64 Revision = State.ControlRevision;
  FCrowdPreparedProjectileBoundary Prepared = State.Prepared;
  FCrowdHitResolveResult Resolved = State.ResolvedHits;
  FCrowdWorkerPayload HostCombatResult = State.HostCombatResult;
  Writer << Revision;
  SerializePrepared(Writer, Prepared);
  SerializeResolved(Writer, Resolved);
  SerializeWorkerPayload(Writer, HostCombatResult);
  if (Writer.IsError()) return false;
  return FinalizeEncodedPayload(
    SchemaId, SchemaVersion, MoveTemp(Bytes), OutPayload);
}

bool FCrowdWorkerProjectileStateCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerProjectileState& OutState)
{
  OutState = {};
  if (!ValidatePayloadHeader(Payload, SchemaId, SchemaVersion))
    return false;
  FMemoryReader Reader(Payload.Bytes, true);
  Reader << OutState.ControlRevision;
  SerializePrepared(Reader, OutState.Prepared);
  SerializeResolved(Reader, OutState.ResolvedHits);
  SerializeWorkerPayload(Reader, OutState.HostCombatResult);
  return !Reader.IsError()
    && Reader.AtEnd()
    && OutState.IsValid();
}

FCrowdWorkerProjectileDomainExecutor::
  FCrowdWorkerProjectileDomainExecutor(
    TUniquePtr<ICrowdWorkerCombatExtension> InCombatExtension)
  : CombatExtension(MoveTemp(InCombatExtension))
{
}

void FCrowdWorkerProjectileDomainExecutor::GetDependencies(
  TArray<ECrowdWorkerDomainId>& OutDependencies) const
{
  // Lifecycle/Input and Behavior keep their earlier stable execution ranks,
  // but their production executors are introduced in WA6. Registry
  // dependencies must name executors that are actually registered, so the
  // WA5 adapter depends only on the live Target predecessor. WA6 restores the
  // explicit Lifecycle/Input and Behavior edges when those domains exist.
  OutDependencies = {ECrowdWorkerDomainId::Target};
}

bool FCrowdWorkerProjectileDomainExecutor::Execute(
  const FCrowdWorkerDomainContext& Context,
  const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
  FCrowdWorkerDomainOutput& OutOutput)
{
  OutOutput = {};
  if (Context.Generation == 0
    || Context.WorkerEpoch == 0
    || Context.LastAppliedInputSequence == 0
    || Context.NextOrderedEventSequence == 0
    || !Context.Resources
    || WorkItems.IsEmpty())
    return false;
  const FCrowdWorkerResourceRecord* Record =
    Context.Resources->FindCurrent(
      CrowdWorkerResourceIds::ProjectileControl);
  if (!Record)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerProjectileDomainRejected stage=resource_missing"));
    return false;
  }
  FCrowdWorkerProjectileControlResource Control;
  if (!FCrowdWorkerProjectileControlResourceCodec::Decode(
      Record->Payload, Control)
    || Record->Revision != Control.Revision)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerProjectileDomainRejected stage=resource_decode record_revision=%llu"),
      Record->Revision);
    return false;
  }
  for (const FCrowdWorkerWorkItem& Work : WorkItems)
  {
    if (Work.Key.Domain != ECrowdWorkerDomainId::CombatReactive
      || (Work.Key.Kind != ECrowdWorkerWorkKind::Resource
        && Work.Key.Kind != ECrowdWorkerWorkKind::Timer)
      || (Work.Key.Kind == ECrowdWorkerWorkKind::Resource
        && Work.Key.ScopeKey
          != CrowdWorkerResourceIds::ProjectileControl))
      return false;
  }

  const bool bHasTimerWork = WorkItems.ContainsByPredicate(
    [](const FCrowdWorkerWorkItem& Work)
    {
      return Work.Key.Kind == ECrowdWorkerWorkKind::Timer;
    });
  const bool bHasClockWork = WorkItems.ContainsByPredicate(
    [](const FCrowdWorkerWorkItem& Work)
    {
      return (Work.ReasonMask
        & CrowdWorkerReasonMasks::CombatClock) != 0;
    });
  const bool bHasContinuationWork =
    bHasTimerWork || bHasClockWork;

  FScopeLock Lock(&StateMutex);
  if (StateGeneration != Context.Generation)
  {
    StateGeneration = Context.Generation;
    LastControlRevision = 0;
    LastFixedStepIndex = INDEX_NONE;
    Projectiles.Reset();
    Metrics = {};
  }
  const bool bFreshControlRevision =
    Control.Revision > LastControlRevision;
  if (Control.Revision < LastControlRevision)
    return false;
  if (Control.Input.FixedStepIndex < LastFixedStepIndex
    && !(bFreshControlRevision && Control.bReplaceState))
  {
    // A versioned control resource is configuration, not the simulation
    // clock. Timer wakeups deliberately reuse its revision while the Worker
    // advances the absolute tick on its own timeline.
    if (!bHasContinuationWork
      || Control.Revision != LastControlRevision)
      return false;
  }
  if (Control.Input.FixedStepIndex == LastFixedStepIndex
    && !bFreshControlRevision)
  {
    if (!bHasContinuationWork)
    {
      ++Metrics.DuplicateStepCount;
      return true;
    }
  }
  const bool bAutonomousContinuation = bHasContinuationWork
    && !bFreshControlRevision
    && Control.Input.FixedStepIndex <= LastFixedStepIndex;
  if (Control.bReplaceState && !bAutonomousContinuation)
    Projectiles = Control.Input.CurrentStates;
  else if (LastFixedStepIndex == INDEX_NONE
    && !Control.Input.CurrentStates.IsEmpty())
    return false;

  FCrowdProjectileBoundaryInput Input = Control.Input;
  if (bAutonomousContinuation)
  {
    Input.FixedStepIndex = LastFixedStepIndex + 1;
    Input.ServerTimeSeconds = static_cast<float>(
      Context.SimulationTimeSeconds);
    Input.FixedStepSeconds = static_cast<float>(
      Context.FixedDeltaSeconds);
    // Spawn requests are edge-triggered commands. Reusing the immutable
    // resource on a timer must not replay the resource's original batch;
    // a host Combat extension may append newly planned spawns below.
    Input.SpawnRequests.Reset();
  }
  Input.CurrentStates = Projectiles;
  TArray<FCrowdImpactFact> ImmediateImpacts;
  if (CombatExtension
    && !CombatExtension->BeginStep(
      Context, Control.HostCombatInput,
      Control.bReplaceState && !bAutonomousContinuation,
      Input, ImmediateImpacts))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerProjectileDomainRejected stage=combat_begin step=%lld"),
      Input.FixedStepIndex);
    return false;
  }
  FCrowdPreparedProjectileBoundary Prepared;
  if (!FCrowdProjectileBoundaryPipeline::Prepare(Input, Prepared)
    || !FCrowdProjectileBoundaryPipeline::ValidatePrepared(
      Input, Prepared))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerProjectileDomainRejected stage=projectile_prepare step=%lld profiles=%d spawns=%d targets=%d states=%d"),
      Input.FixedStepIndex, Input.Profiles.Num(),
      Input.SpawnRequests.Num(), Input.Targets.Num(),
      Input.CurrentStates.Num());
    return false;
  }
  FCrowdHitResolveResult Hits;
  TArray<FCrowdImpactFact> CombinedImpacts =
    MoveTemp(ImmediateImpacts);
  CombinedImpacts.Append(Prepared.Impacts);
  CombinedImpacts.Sort([](
    const FCrowdImpactFact& A,
    const FCrowdImpactFact& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex)
      return A.FixedStepIndex < B.FixedStepIndex;
    return A.ImpactId < B.ImpactId;
  });
  if (!FCrowdCombatResolver::Resolve(
      CombinedImpacts, Control.EffectProfiles, Hits))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerProjectileDomainRejected stage=hit_resolve step=%lld impacts=%d effects=%d"),
      Input.FixedStepIndex, Prepared.Impacts.Num(),
      Control.EffectProfiles.Num());
    return false;
  }
  if (CombinedImpacts.IsEmpty())
  {
    Hits.FixedStepIndex = Prepared.FixedStepIndex;
    Hits.RecalculateStableHash();
  }
  if (!Hits.IsValid()) return false;

  TArray<FCrowdWorkerCombatExtensionPatch> CombatPatches;
  FCrowdWorkerPayload HostCombatResult;
  if (CombatExtension
    && !CombatExtension->FinishStep(
      Context, Hits.Hits, CombatPatches, HostCombatResult))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerProjectileDomainRejected stage=combat_finish step=%lld hits=%d"),
      Input.FixedStepIndex, Hits.Hits.Num());
    return false;
  }

  FCrowdWorkerProjectileState State;
  State.ControlRevision = Control.Revision;
  State.Prepared = Prepared;
  State.ResolvedHits = Hits;
  State.HostCombatResult = MoveTemp(HostCombatResult);
  FCrowdWorkerDirtyStateRecord& Dirty =
    OutOutput.DirtyStates.AddDefaulted_GetRef();
  Dirty.EntityRef = Control.AnchorEntity;
  Dirty.Field = ECrowdWorkerField::Projectile;
  Dirty.Generation = Context.Generation;
  Dirty.WorkerEpoch = Context.WorkerEpoch;
  Dirty.StateRevision = FMath::Max(
    Control.Revision, Context.WorkerEpoch);
  Dirty.CorrectionRevision = Context.CorrectionRevision;
  Dirty.SourceInputSequence = Context.LastAppliedInputSequence;
  if (!FCrowdWorkerProjectileStateCodec::Encode(
      State, Dirty.Payload))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerProjectileDomainRejected stage=state_encode step=%lld states=%d impacts=%d hits=%d"),
      Input.FixedStepIndex, Prepared.States.Num(),
      Prepared.Impacts.Num(), Hits.Hits.Num());
    return false;
  }
  CombatPatches.Sort(
    [](const FCrowdWorkerCombatExtensionPatch& A,
      const FCrowdWorkerCombatExtensionPatch& B)
    {
      return A.EntityRef < B.EntityRef;
    });
  for (int32 PatchIndex = 0;
    PatchIndex < CombatPatches.Num(); ++PatchIndex)
  {
    const FCrowdWorkerCombatExtensionPatch& Patch =
      CombatPatches[PatchIndex];
    if (!Patch.EntityRef.IsValid()
      || !Patch.State.IsValid()
      || (PatchIndex > 0
        && CombatPatches[PatchIndex - 1].EntityRef
          == Patch.EntityRef))
      return false;
    FCrowdWorkerDirtyStateRecord& CombatDirty =
      OutOutput.DirtyStates.AddDefaulted_GetRef();
    CombatDirty.EntityRef = Patch.EntityRef;
    CombatDirty.Field = ECrowdWorkerField::Combat;
    CombatDirty.Generation = Context.Generation;
    CombatDirty.WorkerEpoch = Context.WorkerEpoch;
    CombatDirty.StateRevision = FMath::Max(
      Control.Revision, Context.WorkerEpoch);
    CombatDirty.CorrectionRevision = Context.CorrectionRevision;
    CombatDirty.SourceInputSequence =
      Context.LastAppliedInputSequence;
    if (!FCrowdWorkerCombatStateCodec::Encode(
      Patch.State, CombatDirty.Payload))
      return false;
    ++Metrics.PublishedCombatStateCount;
  }

  uint64 NextSequence = Context.NextOrderedEventSequence;
  for (FCrowdProjectileLifecycleEvent Event : Prepared.Events)
  {
    FCrowdWorkerGameplayEvent& Ordered =
      OutOutput.OrderedEvents.AddDefaulted_GetRef();
    Ordered.EntityRef = Control.AnchorEntity;
    Ordered.Generation = Context.Generation;
    Ordered.WorkerEpoch = Context.WorkerEpoch;
    Ordered.SourceInputSequence =
      Context.LastAppliedInputSequence;
    Ordered.EventSequence = NextSequence++;
    Ordered.EventId = MakeLifecycleEventId(Event);
    if (NextSequence == 0
      || !EncodeEventPayload(
        CrowdWorkerProjectileEventTypeIds::Lifecycle,
        [&Event](FArchive& Ar)
        {
          SerializeLifecycle(Ar, Event);
        },
        Ordered.Payload))
      return false;
    Ordered.RecalculateStableHash();
    ++Metrics.PublishedLifecycleEventCount;
  }
  for (FCrowdHitFact Hit : Hits.Hits)
  {
    FCrowdWorkerGameplayEvent& Ordered =
      OutOutput.OrderedEvents.AddDefaulted_GetRef();
    Ordered.EntityRef = Hit.Impact.Target;
    Ordered.Generation = Context.Generation;
    Ordered.WorkerEpoch = Context.WorkerEpoch;
    Ordered.SourceInputSequence =
      Context.LastAppliedInputSequence;
    Ordered.EventSequence = NextSequence++;
    Ordered.EventId = Hit.Impact.ImpactId;
    if (NextSequence == 0
      || !EncodeEventPayload(
        CrowdWorkerProjectileEventTypeIds::Hit,
        [&Hit](FArchive& Ar)
        {
          SerializeHit(Ar, Hit);
        },
        Ordered.Payload))
      return false;
    Ordered.RecalculateStableHash();
    ++Metrics.PublishedHitEventCount;
  }

  if (Prepared.States.ContainsByPredicate(
      [](const FCrowdProjectileState& Projectile)
      {
        return Projectile.bActive;
      }))
  {
    FCrowdWorkerWakeup& Wakeup =
      OutOutput.Wakeups.AddDefaulted_GetRef();
    Wakeup.Key.Domain = ECrowdWorkerDomainId::CombatReactive;
    Wakeup.Key.EntityRef = Control.AnchorEntity;
    Wakeup.Key.WakeupId =
      CrowdWorkerResourceIds::ProjectileControl;
    Wakeup.AbsoluteSimulationTick =
      Context.AbsoluteSimulationTick + 1;
    Wakeup.Revision = Control.Revision;
    Wakeup.Priority = ECrowdWorkerWorkPriority::High;
    Wakeup.ReasonMask = 1ull << 11;
    ++Metrics.ScheduledWakeupCount;
  }
  FCrowdWorkerDependencyDeclaration& Dependency =
    OutOutput.DeclaredDependencies.AddDefaulted_GetRef();
  Dependency.Source.Kind = ECrowdWorkerDependencyKind::Resource;
  Dependency.Source.ScopeKey =
    CrowdWorkerResourceIds::ProjectileControl;
  Dependency.Dependent.Key.Domain =
    ECrowdWorkerDomainId::CombatReactive;
  Dependency.Dependent.Key.Kind =
    ECrowdWorkerWorkKind::Resource;
  Dependency.Dependent.Key.ScopeKey =
    CrowdWorkerResourceIds::ProjectileControl;
  Dependency.Dependent.Priority =
    ECrowdWorkerWorkPriority::High;
  Dependency.Dependent.ReasonMask = 1ull << 11;
  FCrowdWorkerDependencyObservation& Observation =
    OutOutput.ObservedDependencies.AddDefaulted_GetRef();
  Observation.Source = Dependency.Source;
  Observation.Dependent = Dependency.Dependent.Key;

  // Prepared state includes this step's terminal impact/expiry records so the
  // patch and ordered lifecycle events describe the complete atomic result.
  // Only active projectiles survive into the next simulation tick; retaining
  // terminal records here would re-feed state that the canonical Mass proxy
  // (and the projectile contract) has already retired.
  Projectiles = Prepared.States;
  Projectiles.RemoveAllSwap(
    [](const FCrowdProjectileState& Projectile)
    {
      return !Projectile.bActive;
    },
    EAllowShrinking::No);
  Projectiles.Sort(
    [](const FCrowdProjectileState& A,
      const FCrowdProjectileState& B)
    {
      return A.ProjectileId < B.ProjectileId;
    });
  LastControlRevision = Control.Revision;
  LastFixedStepIndex = Input.FixedStepIndex;
  ++Metrics.ExecutedStepCount;
  ++Metrics.PublishedStateCount;
  return true;
}

bool FCrowdWorkerProjectileDomainExecutor::ApplyAuthorityCorrection(
  const FCrowdWorkerDomainContext& Context,
  const TConstArrayView<FCrowdWorkerDirtyStateRecord> Records)
{
  FScopeLock Lock(&StateMutex);
  if (Context.Generation == 0)
    return false;
  if (StateGeneration != Context.Generation)
  {
    StateGeneration = Context.Generation;
    LastControlRevision = 0;
    LastFixedStepIndex = INDEX_NONE;
    Projectiles.Reset();
    Metrics = {};
  }
  for (const FCrowdWorkerDirtyStateRecord& Record : Records)
  {
    if (Record.Field != ECrowdWorkerField::Projectile)
      continue;
    FCrowdWorkerProjectileState State;
    if (!FCrowdWorkerProjectileStateCodec::Decode(
        Record.Payload, State))
      return false;
    Projectiles = State.Prepared.States;
    Projectiles.RemoveAllSwap(
      [](const FCrowdProjectileState& Projectile)
      {
        return !Projectile.bActive;
      },
      EAllowShrinking::No);
    Projectiles.Sort(
      [](const FCrowdProjectileState& A,
        const FCrowdProjectileState& B)
      {
        return A.ProjectileId < B.ProjectileId;
      });
    LastControlRevision = State.ControlRevision;
    LastFixedStepIndex = State.Prepared.FixedStepIndex;
  }
  return !CombatExtension
    || CombatExtension->ApplyAuthorityCorrection(Context, Records);
}

FCrowdWorkerProjectileDomainMetrics
FCrowdWorkerProjectileDomainExecutor::GetMetrics() const
{
  FScopeLock Lock(&StateMutex);
  return Metrics;
}
