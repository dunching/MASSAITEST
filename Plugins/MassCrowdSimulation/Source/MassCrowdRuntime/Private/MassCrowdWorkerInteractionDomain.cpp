#include "MassCrowdWorkerInteractionDomain.h"

#include "MassCrowdWorkerCombatState.h"
#include "MassCrowdWorkerMovementAuthority.h"
#include "MassCrowdWorkerMovementControlResource.h"
#include "MassCrowdWorkerShadowSync.h"
#include "MassCrowdWorkerTargetDomain.h"
#include "MassCrowdParticleWork.h"

namespace CrowdWorkerInteractionDomainPrivate
{
  constexpr uint64 InteractionFnvOffset64 =
    14695981039346656037ull;
  constexpr uint64 InteractionFnvPrime64 = 1099511628211ull;

  void InteractionFold(uint64& Hash, const uint64 Value)
  {
    for (uint32 Byte = 0; Byte < sizeof(Value); ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= InteractionFnvPrime64;
    }
  }

  void InteractionAppendDouble(TArray<uint8>& Bytes, const double Value)
  {
    uint64 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    for (uint32 Byte = 0; Byte < sizeof(Bits); ++Byte)
      Bytes.Add(static_cast<uint8>(Bits >> (Byte * 8)));
  }

  bool InteractionReadDouble(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    double& OutValue)
  {
    if (Offset < 0
      || Offset + static_cast<int32>(sizeof(uint64))
        > Bytes.Num())
      return false;
    uint64 Bits = 0;
    for (uint32 Byte = 0; Byte < sizeof(Bits); ++Byte)
      Bits |= static_cast<uint64>(Bytes[Offset + Byte])
        << (Byte * 8);
    Offset += sizeof(Bits);
    FMemory::Memcpy(&OutValue, &Bits, sizeof(Bits));
    return FMath::IsFinite(OutValue);
  }

  bool DecodeSpatialState(
    const FCrowdWorkerEntityStateStore& States,
    const FCrowdStableEntityRef& EntityRef,
    FCrowdWorkerSpatialEntry& OutEntry)
  {
    OutEntry = {};
    OutEntry.EntityRef = EntityRef;
    const FCrowdWorkerDirtyStateRecord* Input =
      States.Find(EntityRef, ECrowdWorkerField::InputSnapshot);
    FCrowdWorkerBoundaryKinematicState Kinematic;
    if (!Input
      || !FCrowdWorkerBoundaryStateCodec::DecodeKinematicState(
        Input->Payload, Kinematic))
      return false;
    OutEntry.PhysicalRadiusCm = Kinematic.PhysicalRadiusCm;
    OutEntry.HardSafetyGapCm = Kinematic.HardSafetyGapCm;
    OutEntry.SoftMarginCm = Kinematic.SoftMarginCm;
    OutEntry.Mobility = Kinematic.Mobility;
    if (const FCrowdWorkerDirtyStateRecord* Movement =
      States.Find(EntityRef, ECrowdWorkerField::Movement))
    {
      FCrowdWorkerMovementState State;
      if (!FCrowdWorkerMovementStateCodec::Decode(
          Movement->Payload, State))
        return false;
      OutEntry.Position = State.Position;
      return true;
    }
    OutEntry.Position = Kinematic.Position;
    return true;
  }
}

using namespace CrowdWorkerInteractionDomainPrivate;

bool FCrowdWorkerInteractionPairKey::Normalize()
{
  if (!A.IsValid() || !B.IsValid() || A == B)
    return false;
  if (B < A) Swap(A, B);
  return true;
}

bool FCrowdWorkerInteractionPairKey::IsValid() const
{
  return A.IsValid() && B.IsValid() && A < B;
}

bool FCrowdWorkerInteractionPairKey::operator<(
  const FCrowdWorkerInteractionPairKey& Other) const
{
  return A != Other.A ? A < Other.A : B < Other.B;
}

bool FCrowdWorkerSpatialIndex::Reset(
  const int32 InMaxEntities,
  const float InCellSizeCm)
{
  if (InMaxEntities <= 0
    || !FMath::IsFinite(InCellSizeCm)
    || InCellSizeCm <= 0.0f)
    return false;
  MaxEntities = InMaxEntities;
  CellSizeCm = InCellSizeCm;
  Entries.Reset();
  Cells.Reset();
  FullRebuildCount = 0;
  IncrementalUpdateCount = 0;
  CellMigrationCount = 0;
  return true;
}

FIntVector FCrowdWorkerSpatialIndex::CellFor(
  const FVector& Position) const
{
  return FIntVector(
    FMath::FloorToInt(Position.X / CellSizeCm),
    FMath::FloorToInt(Position.Y / CellSizeCm),
    FMath::FloorToInt(Position.Z / CellSizeCm));
}

bool FCrowdWorkerSpatialIndex::Rebuild(
  const FCrowdWorkerEntityStateStore& States)
{
  TArray<FCrowdStableEntityRef> EntityRefs;
  States.GetEntities(EntityRefs);
  if (EntityRefs.Num() > MaxEntities) return false;
  TMap<FCrowdStableEntityRef, FCrowdWorkerSpatialEntry>
    BuildingEntries;
  TMap<FIntVector, TArray<FCrowdStableEntityRef>>
    BuildingCells;
  for (const FCrowdStableEntityRef& EntityRef : EntityRefs)
  {
    FCrowdWorkerSpatialEntry Entry;
    if (!DecodeSpatialState(States, EntityRef, Entry))
      continue;
    BuildingCells.FindOrAdd(
      CellFor(Entry.Position)).Add(EntityRef);
    BuildingEntries.Add(EntityRef, Entry);
  }
  for (TPair<FIntVector, TArray<FCrowdStableEntityRef>>& Pair :
    BuildingCells)
    Pair.Value.Sort();
  Entries = MoveTemp(BuildingEntries);
  Cells = MoveTemp(BuildingCells);
  ++FullRebuildCount;
  return true;
}

bool FCrowdWorkerSpatialIndex::Spawn(
  const FCrowdWorkerEntityStateStore& States,
  const FCrowdStableEntityRef& EntityRef)
{
  if (Entries.Contains(EntityRef))
    return false;
  FCrowdWorkerSpatialEntry Entry;
  if (!DecodeSpatialState(States, EntityRef, Entry))
    return true;
  if (Entries.Num() >= MaxEntities)
    return false;
  TArray<FCrowdStableEntityRef>& Cell =
    Cells.FindOrAdd(CellFor(Entry.Position));
  int32 InsertIndex = 0;
  while (InsertIndex < Cell.Num() && Cell[InsertIndex] < EntityRef)
    ++InsertIndex;
  Cell.Insert(EntityRef, InsertIndex);
  Entries.Add(EntityRef, MoveTemp(Entry));
  ++IncrementalUpdateCount;
  return true;
}

bool FCrowdWorkerSpatialIndex::Despawn(
  const FCrowdStableEntityRef& EntityRef)
{
  const FCrowdWorkerSpatialEntry* Entry = Entries.Find(EntityRef);
  if (!Entry) return true;
  const FIntVector CellKey = CellFor(Entry->Position);
  TArray<FCrowdStableEntityRef>* Cell = Cells.Find(CellKey);
  if (!Cell || Cell->RemoveSingle(EntityRef) != 1)
    return false;
  if (Cell->IsEmpty()) Cells.Remove(CellKey);
  Entries.Remove(EntityRef);
  ++IncrementalUpdateCount;
  return true;
}

bool FCrowdWorkerSpatialIndex::UpdateEntity(
  const FCrowdWorkerEntityStateStore& States,
  const FCrowdStableEntityRef& EntityRef)
{
  FCrowdWorkerSpatialEntry* Existing = Entries.Find(EntityRef);
  FCrowdWorkerSpatialEntry Updated;
  if (!DecodeSpatialState(States, EntityRef, Updated))
    return Existing ? Despawn(EntityRef) : true;
  if (!Existing)
    return Spawn(States, EntityRef);
  const FIntVector PreviousCell = CellFor(Existing->Position);
  const FIntVector UpdatedCell = CellFor(Updated.Position);
  if (PreviousCell != UpdatedCell)
  {
    TArray<FCrowdStableEntityRef>* OldCell = Cells.Find(PreviousCell);
    if (!OldCell || OldCell->RemoveSingle(EntityRef) != 1)
      return false;
    if (OldCell->IsEmpty()) Cells.Remove(PreviousCell);
    TArray<FCrowdStableEntityRef>& NewCell =
      Cells.FindOrAdd(UpdatedCell);
    int32 InsertIndex = 0;
    while (InsertIndex < NewCell.Num()
      && NewCell[InsertIndex] < EntityRef)
      ++InsertIndex;
    NewCell.Insert(EntityRef, InsertIndex);
    ++CellMigrationCount;
  }
  *Existing = MoveTemp(Updated);
  ++IncrementalUpdateCount;
  return true;
}

bool FCrowdWorkerSpatialIndex::QueryNeighbors(
  const FCrowdStableEntityRef& EntityRef,
  const float RadiusCm,
  TArray<FCrowdWorkerSpatialEntry>& OutNeighbors) const
{
  OutNeighbors.Reset();
  const FCrowdWorkerSpatialEntry* Center = Entries.Find(EntityRef);
  if (!Center || !FMath::IsFinite(RadiusCm) || RadiusCm <= 0.0f)
    return false;
  const int32 CellRadius =
    FMath::Max(1, FMath::CeilToInt(RadiusCm / CellSizeCm));
  const FIntVector CenterCell = CellFor(Center->Position);
  const double RadiusSquared =
    static_cast<double>(RadiusCm) * RadiusCm;
  for (int32 Z = -CellRadius; Z <= CellRadius; ++Z)
  {
    for (int32 Y = -CellRadius; Y <= CellRadius; ++Y)
    {
      for (int32 X = -CellRadius; X <= CellRadius; ++X)
      {
        const TArray<FCrowdStableEntityRef>* Cell =
          Cells.Find(CenterCell + FIntVector(X, Y, Z));
        if (!Cell) continue;
        for (const FCrowdStableEntityRef& CandidateRef : *Cell)
        {
          if (CandidateRef == EntityRef) continue;
          const FCrowdWorkerSpatialEntry& Candidate =
            Entries.FindChecked(CandidateRef);
          if (FVector::DistSquared(
              Center->Position, Candidate.Position)
            <= RadiusSquared)
            OutNeighbors.Add(Candidate);
        }
      }
    }
  }
  OutNeighbors.Sort([](
    const FCrowdWorkerSpatialEntry& A,
    const FCrowdWorkerSpatialEntry& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  return true;
}

const FCrowdWorkerSpatialEntry* FCrowdWorkerSpatialIndex::Find(
  const FCrowdStableEntityRef& EntityRef) const
{
  return Entries.Find(EntityRef);
}

uint64 FCrowdWorkerSpatialIndex::CalculateStableHash() const
{
  TArray<FCrowdStableEntityRef> EntityRefs;
  Entries.GetKeys(EntityRefs);
  EntityRefs.Sort();
  uint64 Hash = InteractionFnvOffset64;
  InteractionFold(Hash, 1);
  for (const FCrowdStableEntityRef& EntityRef : EntityRefs)
  {
    const FCrowdWorkerSpatialEntry& Entry =
      Entries.FindChecked(EntityRef);
    InteractionFold(Hash, EntityRef.ProviderId);
    InteractionFold(Hash, EntityRef.StableEntityId);
    InteractionFold(Hash, EntityRef.LifecycleSerial);
    InteractionFold(Hash, GetTypeHash(Entry.Position));
    InteractionFold(Hash, GetTypeHash(Entry.PhysicalRadiusCm));
    InteractionFold(Hash, GetTypeHash(Entry.HardSafetyGapCm));
    InteractionFold(Hash, GetTypeHash(Entry.SoftMarginCm));
    InteractionFold(Hash, GetTypeHash(Entry.Mobility));
  }
  return Hash;
}

bool FCrowdWorkerParticleState::IsValid() const
{
  return !PositionOffset.ContainsNaN()
    && !VelocityDelta.ContainsNaN();
}

bool FCrowdWorkerParticleStateCodec::Encode(
  const FCrowdWorkerParticleState& State,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!State.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  InteractionAppendDouble(OutPayload.Bytes, State.PositionOffset.X);
  InteractionAppendDouble(OutPayload.Bytes, State.PositionOffset.Y);
  InteractionAppendDouble(OutPayload.Bytes, State.PositionOffset.Z);
  InteractionAppendDouble(OutPayload.Bytes, State.VelocityDelta.X);
  InteractionAppendDouble(OutPayload.Bytes, State.VelocityDelta.Y);
  InteractionAppendDouble(OutPayload.Bytes, State.VelocityDelta.Z);
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerParticleStateCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerParticleState& OutState)
{
  OutState = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.Num() != 6 * sizeof(double)
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  return InteractionReadDouble(
      Payload.Bytes, Offset, OutState.PositionOffset.X)
    && InteractionReadDouble(
      Payload.Bytes, Offset, OutState.PositionOffset.Y)
    && InteractionReadDouble(
      Payload.Bytes, Offset, OutState.PositionOffset.Z)
    && InteractionReadDouble(
      Payload.Bytes, Offset, OutState.VelocityDelta.X)
    && InteractionReadDouble(
      Payload.Bytes, Offset, OutState.VelocityDelta.Y)
    && InteractionReadDouble(
      Payload.Bytes, Offset, OutState.VelocityDelta.Z)
    && OutState.IsValid();
}

void FCrowdWorkerParticleInteractionDomainExecutor::GetDependencies(
  TArray<ECrowdWorkerDomainId>& OutDependencies) const
{
  OutDependencies = {ECrowdWorkerDomainId::Movement};
}

bool FCrowdWorkerParticleInteractionDomainExecutor::Execute(
  const FCrowdWorkerDomainContext& Context,
  const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
  FCrowdWorkerDomainOutput& OutOutput)
{
  if (!Context.EntityStates || !Context.Resources
    || !Context.SpatialIndex
    || WorkItems.Num() != 1
    || WorkItems[0].Key.Domain
      != ECrowdWorkerDomainId::ParticleInteraction
    || WorkItems[0].Key.Kind
      != ECrowdWorkerWorkKind::Resource
    || WorkItems[0].Key.ScopeKey
      != CrowdWorkerResourceIds::MovementControl)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerParticleDomainRejected stage=context work=%d domain=%u kind=%u scope=%llu spatial=%d"),
      WorkItems.Num(),
      WorkItems.IsEmpty()
        ? MAX_uint8
        : static_cast<uint32>(WorkItems[0].Key.Domain),
      WorkItems.IsEmpty()
        ? MAX_uint8
        : static_cast<uint32>(WorkItems[0].Key.Kind),
      WorkItems.IsEmpty() ? 0 : WorkItems[0].Key.ScopeKey,
      Context.SpatialIndex ? Context.SpatialIndex->Num() : -1);
    return false;
  }
  const FCrowdWorkerResourceRecord* Resource =
    Context.Resources->FindCurrent(
      CrowdWorkerResourceIds::MovementControl);
  FCrowdWorkerMovementControlResource Control;
  if (!Resource
    || !FCrowdWorkerMovementControlResourceCodec::Decode(
      Resource->Payload, Control)
    || Control.Revision != Resource->Revision)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerParticleDomainRejected stage=control resource=%d resource_revision=%llu control_revision=%llu spatial=%d entries=%d"),
      Resource ? 1 : 0,
      Resource ? Resource->Revision : 0,
      Control.Revision,
      Context.SpatialIndex->Num(),
      Control.Entries.Num());
    return false;
  }
  TArray<FCrowdWorkerMovementControlEntry> Profiles;
  if (!CrowdWorkerResolveMovementProfiles(
      *Context.EntityStates,
      Context.LastAppliedInputSequence,
      Control,
      Profiles)
    || Context.SpatialIndex->Num() != Profiles.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerParticleDomainRejected stage=profiles spatial=%d profiles=%d"),
      Context.SpatialIndex->Num(),
      Profiles.Num());
    return false;
  }

  TMap<int32, FCrowdStableEntityRef> EntityRefByAgentId;
  TMap<FCrowdStableEntityRef, FCrowdWorkerMovementState>
    MovementByEntityRef;
  TMap<FCrowdStableEntityRef, bool> ParticleActiveByEntityRef;
  FCrowdMassParticleWorkInput Input;
  if (Context.AbsoluteSimulationTick
      > static_cast<uint64>(MAX_int32))
    return false;
  Input.FixedStepIndex =
    static_cast<int32>(Context.AbsoluteSimulationTick);
  Input.PlanRevision = Control.PlanRevision;
  Input.Environment.FlowConfig = Control.Environment;
  Input.Environment.bConstrainToFlowBounds =
    Control.bParticleConstrainToFlowBounds;
  Input.Settings = Control.ParticleSettings;
  Input.bCaptureTrace =
    Control.ParticleSettings.bCaptureRouteDiagnostic;
  for (const FCrowdWorkerMovementControlEntry& Entry :
    Profiles)
  {
    if (EntityRefByAgentId.Contains(Entry.AgentId))
      return false;
    EntityRefByAgentId.Add(Entry.AgentId, Entry.EntityRef);
    const FCrowdWorkerDirtyStateRecord* Snapshot =
      Context.EntityStates->Find(
        Entry.EntityRef, ECrowdWorkerField::InputSnapshot);
    const FCrowdWorkerDirtyStateRecord* MovementRecord =
      Context.EntityStates->Find(
        Entry.EntityRef, ECrowdWorkerField::Movement);
    FCrowdWorkerBoundaryKinematicState Kinematic;
    FCrowdWorkerMovementState Movement;
    if (!Snapshot || !MovementRecord
      || !FCrowdWorkerBoundaryStateCodec::DecodeKinematicState(
        Snapshot->Payload, Kinematic)
      || !FCrowdWorkerMovementStateCodec::Decode(
        MovementRecord->Payload, Movement)
      || MovementRecord->SourceInputSequence
        > Context.LastAppliedInputSequence)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdWorkerParticleDomainRejected stage=entity stable_id=%llu snapshot=%d movement=%d movement_input=%llu expected_input=%llu"),
        Entry.EntityRef.StableEntityId,
        Snapshot ? 1 : 0,
        MovementRecord ? 1 : 0,
        MovementRecord ? MovementRecord->SourceInputSequence : 0,
        Context.LastAppliedInputSequence);
      return false;
    }
    MovementByEntityRef.Add(Entry.EntityRef, Movement);
    bool bParticleActive = Entry.bParticleActive;
    if (const FCrowdWorkerDirtyStateRecord* CombatRecord =
      Context.EntityStates->Find(
        Entry.EntityRef, ECrowdWorkerField::Combat))
    {
      FCrowdWorkerCombatState Combat;
      if (!FCrowdWorkerCombatStateCodec::Decode(
          CombatRecord->Payload, Combat)
        || CombatRecord->SourceInputSequence
          > Context.LastAppliedInputSequence)
        return false;
      bParticleActive = bParticleActive && Combat.bAlive;
    }
    ParticleActiveByEntityRef.Add(
      Entry.EntityRef, bParticleActive);
    if (!Control.bRunParticleInteraction
      || !bParticleActive)
      continue;
    FCrowdParticleConstraintAgent& Agent =
      Input.Agents.AddDefaulted_GetRef();
    Agent.AgentId = Entry.AgentId;
    Agent.InteractionLayer = Entry.InteractionLayer;
    Agent.StartPosition = Movement.StartPosition;
    Agent.PredictedPosition = Movement.Position;
    Agent.PhysicalRadiusCm = Entry.ParticlePhysicalRadiusCm;
    Agent.HardSafetyGapCm =
      Entry.ParticleHardSafetyGapCm;
    Agent.EnvironmentHardClearanceCm =
      Entry.ParticleEnvironmentHardClearanceCm;
    Agent.SoftMarginCm = Entry.ParticleSoftMarginCm;
    Agent.Mobility = Entry.ParticleMobility;
  }
  if (Control.bRunParticleInteraction)
    Input.Agents.Append(Control.ExternalParticleAgents);
  bool bUsesPrimaryTargetObjective = false;
  if (Control.bRunParticleInteraction)
  {
    FCrowdParticleConstraintAgent* TargetAgent =
      Input.Agents.FindByPredicate([](const auto& Agent)
      {
        return Agent.AgentId
          == CrowdWorkerTargetConstants::PrimaryTargetParticleAgentId;
      });
    if (TargetAgent)
    {
      const uint64 ObjectiveResourceId =
        CrowdWorkerResourceIds::ObjectiveRevision(
          CrowdWorkerTargetObjectiveIds::PrimaryTarget);
      const FCrowdWorkerResourceRecord* ObjectiveRecord =
        Context.Resources->FindCurrent(ObjectiveResourceId);
      FCrowdWorkerTargetObjectiveRevision Objective;
      if (!ObjectiveRecord || ObjectiveRecord->Revision == 0
        || !FCrowdWorkerTargetObjectiveRevisionCodec::Decode(
          ObjectiveRecord->Payload, Objective)
        || Objective.EffectiveFixedStepIndex > Input.FixedStepIndex)
        return false;
      TargetAgent->PredictedPosition = FVector(
        Objective.TargetLocation.X, Objective.TargetLocation.Y,
        TargetAgent->PredictedPosition.Z);
      TargetAgent->StartPosition = TargetAgent->PredictedPosition
        - FVector(Objective.TargetVelocity.X, Objective.TargetVelocity.Y, 0.0f)
          * Input.Settings.FixedStepSeconds;
      bUsesPrimaryTargetObjective = true;
    }
  }
  Input.Agents.Sort([](
    const FCrowdParticleConstraintAgent& A,
    const FCrowdParticleConstraintAgent& B)
  {
    return A.AgentId < B.AgentId;
  });

  FCrowdMassParticleWorkOutput Solver;
  TMap<int32, const FCrowdParticleConstraintResult*> ResultByAgentId;
  if (Control.bRunParticleInteraction)
  {
    if (Input.Agents.IsEmpty()) return false;
    Solver = FCrowdMassParticleWork::Solve(Input);
    if (!Solver.bCompleted)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdWorkerParticleDomainRejected stage=solver agents=%d"),
        Input.Agents.Num());
      return false;
    }
    TSet<uint64> UniquePairs;
    for (const FCrowdParticleConstraintPair& Pair : Solver.Pairs)
    {
      const uint64 PairKey =
        (static_cast<uint64>(
          static_cast<uint32>(Pair.MinAgentId)) << 32)
        | static_cast<uint32>(Pair.MaxAgentId);
      if (Pair.MinAgentId >= Pair.MaxAgentId
        || UniquePairs.Contains(PairKey))
        return false;
      UniquePairs.Add(PairKey);
    }
    for (const FCrowdParticleConstraintResult& Result :
      Solver.Results)
    {
      if (ResultByAgentId.Contains(Result.AgentId))
        return false;
      ResultByAgentId.Add(Result.AgentId, &Result);
    }
  }

  for (const FCrowdWorkerMovementControlEntry& Entry :
    Profiles)
  {
    FCrowdWorkerDependencyDeclaration Declaration;
    Declaration.Source.Kind =
      ECrowdWorkerDependencyKind::Entity;
    Declaration.Source.EntityRef = Entry.EntityRef;
    Declaration.Source.ScopeKey =
      CrowdWorkerRuntimeV2DependencyScopeForField(
        ECrowdWorkerField::Movement);
    Declaration.Dependent = WorkItems[0];
    Declaration.Dependent.ReasonMask |= 1ull << 10;
    OutOutput.DeclaredDependencies.Add(Declaration);
    FCrowdWorkerDependencyObservation Observation;
    Observation.Source = Declaration.Source;
    Observation.Dependent = Declaration.Dependent.Key;
    OutOutput.ObservedDependencies.Add(Observation);
  }
  if (bUsesPrimaryTargetObjective)
  {
    FCrowdWorkerDependencyDeclaration Declaration;
    Declaration.Source.Kind = ECrowdWorkerDependencyKind::Resource;
    Declaration.Source.ScopeKey =
      CrowdWorkerResourceIds::ObjectiveRevision(
        CrowdWorkerTargetObjectiveIds::PrimaryTarget);
    Declaration.Dependent = WorkItems[0];
    Declaration.Dependent.ReasonMask |= 1ull << 11;
    OutOutput.DeclaredDependencies.Add(Declaration);
    FCrowdWorkerDependencyObservation Observation;
    Observation.Source = Declaration.Source;
    Observation.Dependent = Declaration.Dependent.Key;
    OutOutput.ObservedDependencies.Add(Observation);
  }

  for (const FCrowdWorkerMovementControlEntry& Entry :
    Profiles)
  {
    const FCrowdWorkerMovementState* Movement =
      MovementByEntityRef.Find(Entry.EntityRef);
    if (!Movement) return false;
    FVector FinalPosition = Movement->Position;
    FVector FinalVelocity = Movement->Velocity;
    if (Control.bRunParticleInteraction)
    {
      if (!ParticleActiveByEntityRef.FindRef(Entry.EntityRef))
      {
        FinalVelocity = FVector::ZeroVector;
      }
      else if (Solver.Summary.bValid)
      {
        const FCrowdParticleConstraintResult* const* Result =
          ResultByAgentId.Find(Entry.AgentId);
        if (!Result) return false;
        FinalPosition = (*Result)->CorrectedPosition;
        FinalVelocity = (*Result)->CorrectedVelocity;
      }
      else
      {
        FinalPosition = Movement->StartPosition;
        FinalVelocity = FVector::ZeroVector;
      }
    }
    FCrowdWorkerParticleState Particle;
    Particle.PositionOffset =
      FinalPosition - Movement->Position;
    Particle.VelocityDelta =
      FinalVelocity - Movement->Velocity;
    FCrowdWorkerDirtyStateRecord Dirty;
    Dirty.EntityRef = Entry.EntityRef;
    Dirty.Field = ECrowdWorkerField::Particle;
    Dirty.Generation = Context.Generation;
    Dirty.WorkerEpoch = Context.WorkerEpoch;
    Dirty.SourceInputSequence =
      Context.LastAppliedInputSequence;
    if (!FCrowdWorkerParticleStateCodec::Encode(
        Particle, Dirty.Payload))
      return false;
    const FCrowdWorkerDirtyStateRecord* Existing =
      Context.EntityStates->Find(
        Entry.EntityRef, ECrowdWorkerField::Particle);
    Dirty.StateRevision = Existing
      ? FMath::Max(
          Context.WorkerEpoch, Existing->StateRevision + 1)
      : Context.WorkerEpoch;
    if (!Existing || !(Existing->Payload == Dirty.Payload))
      OutOutput.DirtyStates.Add(MoveTemp(Dirty));

    FCrowdWorkerWorkItem Finalize;
    Finalize.Key.Domain = ECrowdWorkerDomainId::FacingFinalize;
    Finalize.Key.Kind = ECrowdWorkerWorkKind::Entity;
    Finalize.Key.PrimaryEntity = Entry.EntityRef;
    Finalize.Priority = ECrowdWorkerWorkPriority::Normal;
    Finalize.CorrectionRevision =
      WorkItems[0].CorrectionRevision;
    Finalize.ReasonMask = 1ull << 9;
    OutOutput.NextWork.Add(MoveTemp(Finalize));
  }
  return true;
}

void FCrowdWorkerFacingFinalizeDomainExecutor::GetDependencies(
  TArray<ECrowdWorkerDomainId>& OutDependencies) const
{
  OutDependencies = {
    ECrowdWorkerDomainId::ParticleInteraction};
}

bool FCrowdWorkerFacingFinalizeDomainExecutor::Execute(
  const FCrowdWorkerDomainContext& Context,
  const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
  FCrowdWorkerDomainOutput& OutOutput)
{
  if (!Context.EntityStates
    || Context.Generation == 0
    || Context.WorkerEpoch == 0)
    return false;
  for (const FCrowdWorkerWorkItem& Work : WorkItems)
  {
    if (Work.Key.Domain != ECrowdWorkerDomainId::FacingFinalize
      || Work.Key.Kind != ECrowdWorkerWorkKind::Entity)
      return false;
    const FCrowdWorkerDirtyStateRecord* MovementRecord =
      Context.EntityStates->Find(
        Work.Key.PrimaryEntity, ECrowdWorkerField::Movement);
    const FCrowdWorkerDirtyStateRecord* ParticleRecord =
      Context.EntityStates->Find(
        Work.Key.PrimaryEntity, ECrowdWorkerField::Particle);
    FCrowdWorkerMovementState Movement;
    FCrowdWorkerParticleState Particle;
    if (!MovementRecord || !ParticleRecord
      || !FCrowdWorkerMovementStateCodec::Decode(
        MovementRecord->Payload, Movement)
      || !FCrowdWorkerParticleStateCodec::Decode(
        ParticleRecord->Payload, Particle))
      return false;
    Movement.Position += Particle.PositionOffset;
    Movement.Velocity += Particle.VelocityDelta;
    if (!Movement.Velocity.IsNearlyZero())
      Movement.YawDegrees = Movement.Velocity.Rotation().Yaw;

    FCrowdWorkerDirtyStateRecord Dirty;
    Dirty.EntityRef = Work.Key.PrimaryEntity;
    // Movement is the autonomous pre-constraint stage output. Preserve it
    // for WA2 parity/canary comparison; Facing owns the finalized kinematic
    // result after Particle has contributed its correction.
    Dirty.Field = ECrowdWorkerField::Facing;
    Dirty.Generation = Context.Generation;
    Dirty.WorkerEpoch = Context.WorkerEpoch;
    const FCrowdWorkerDirtyStateRecord* ExistingFacing =
      Context.EntityStates->Find(
        Work.Key.PrimaryEntity, ECrowdWorkerField::Facing);
    Dirty.StateRevision = FMath::Max3(
      Context.WorkerEpoch,
      MovementRecord->StateRevision + 1,
      ExistingFacing ? ExistingFacing->StateRevision + 1 : 1);
    Dirty.CorrectionRevision = FMath::Max(
      Work.CorrectionRevision,
      MovementRecord->CorrectionRevision);
    Dirty.SourceInputSequence =
      Context.LastAppliedInputSequence;
    if (!FCrowdWorkerMovementStateCodec::Encode(
        Movement, Dirty.Payload))
      return false;
    OutOutput.DirtyStates.Add(MoveTemp(Dirty));
  }
  return true;
}
