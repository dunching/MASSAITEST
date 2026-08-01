#include "MassCrowdWorkerMovementDomain.h"

#include "MassCrowdWorkerMovementAuthority.h"
#include "MassCrowdWorkerCombatState.h"
#include "MassCrowdWorkerInteractionDomain.h"
#include "MassCrowdWorkerLifecycleBehaviorDomain.h"
#include "MassCrowdWorkerFlowResource.h"
#include "MassCrowdWorkerMovementControlResource.h"
#include "MassCrowdWorkerNavigationResource.h"
#include "MassCrowdWorkerShadowSync.h"
#include "MassCrowdWorkerTargetDomain.h"
#include "MassCrowdLocalPredictiveWork.h"
#include "CrowdSharedFlowFieldKernel.h"

namespace CrowdWorkerMovementPlanPrivate
{
  constexpr uint32 SchemaId = 0x43574D50u;
  constexpr uint16 SchemaVersion = 3;

  struct FPlanState
  {
    FVector AutonomousPreferredVelocity = FVector::ZeroVector;
    FVector LocalVelocity = FVector::ZeroVector;
    bool bUseLocalVelocity = false;
    bool bLocalVelocityValid = false;
    bool bMovementLocked = false;
  };

  void AppendFloat(TArray<uint8>& Bytes, const float Value)
  {
    uint32 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    for (uint32 Byte = 0; Byte < sizeof(Bits); ++Byte)
      Bytes.Add(static_cast<uint8>(Bits >> (Byte * 8)));
  }

  bool ReadFloat(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    float& OutValue)
  {
    if (Offset < 0 || Offset + 4 > Bytes.Num())
      return false;
    uint32 Bits = 0;
    for (uint32 Byte = 0; Byte < 4; ++Byte)
      Bits |= static_cast<uint32>(Bytes[Offset + Byte])
        << (Byte * 8);
    Offset += 4;
    FMemory::Memcpy(&OutValue, &Bits, sizeof(Bits));
    return FMath::IsFinite(OutValue);
  }

  bool Encode(
    const FPlanState& State,
    FCrowdWorkerPayload& OutPayload)
  {
    OutPayload = {};
    if (State.AutonomousPreferredVelocity.ContainsNaN()
      || State.LocalVelocity.ContainsNaN())
      return false;
    OutPayload.SchemaId = SchemaId;
    OutPayload.SchemaVersion = SchemaVersion;
    OutPayload.Bytes.Add(static_cast<uint8>(
      (State.bUseLocalVelocity ? 1u : 0u)
      | (State.bLocalVelocityValid ? 2u : 0u)
      | (State.bMovementLocked ? 4u : 0u)));
    AppendFloat(
      OutPayload.Bytes,
      static_cast<float>(State.AutonomousPreferredVelocity.X));
    AppendFloat(
      OutPayload.Bytes,
      static_cast<float>(State.AutonomousPreferredVelocity.Y));
    AppendFloat(
      OutPayload.Bytes,
      static_cast<float>(State.AutonomousPreferredVelocity.Z));
    AppendFloat(
      OutPayload.Bytes,
      static_cast<float>(State.LocalVelocity.X));
    AppendFloat(
      OutPayload.Bytes,
      static_cast<float>(State.LocalVelocity.Y));
    AppendFloat(
      OutPayload.Bytes,
      static_cast<float>(State.LocalVelocity.Z));
    OutPayload.RecalculateStableHash();
    return true;
  }

  bool Decode(
    const FCrowdWorkerPayload& Payload,
    FPlanState& OutState)
  {
    OutState = {};
    if (Payload.SchemaId != SchemaId
      || Payload.SchemaVersion != SchemaVersion
      || Payload.Bytes.Num() != 25
      || Payload.StableHash != Payload.CalculateStableHash()
      || (Payload.Bytes[0] & ~uint8{7}) != 0)
      return false;
    int32 Offset = 1;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    if (!ReadFloat(Payload.Bytes, Offset, X)
      || !ReadFloat(Payload.Bytes, Offset, Y)
      || !ReadFloat(Payload.Bytes, Offset, Z))
      return false;
    OutState.AutonomousPreferredVelocity = FVector(X, Y, Z);
    if (!ReadFloat(Payload.Bytes, Offset, X)
      || !ReadFloat(Payload.Bytes, Offset, Y)
      || !ReadFloat(Payload.Bytes, Offset, Z))
      return false;
    OutState.LocalVelocity = FVector(X, Y, Z);
    OutState.bUseLocalVelocity =
      (Payload.Bytes[0] & 1u) != 0;
    OutState.bLocalVelocityValid =
      (Payload.Bytes[0] & 2u) != 0;
    OutState.bMovementLocked =
      (Payload.Bytes[0] & 4u) != 0;
    return true;
  }
}

using namespace CrowdWorkerMovementPlanPrivate;

void FCrowdWorkerFlowResourceDomainExecutor::GetDependencies(
  TArray<ECrowdWorkerDomainId>& OutDependencies) const
{
  OutDependencies.Reset();
}

bool FCrowdWorkerFlowResourceDomainExecutor::Execute(
  const FCrowdWorkerDomainContext& Context,
  const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
  FCrowdWorkerDomainOutput& OutOutput)
{
  (void)OutOutput;
  if (!Context.Resources) return false;
  for (const FCrowdWorkerWorkItem& Work : WorkItems)
  {
    if (Work.Key.Domain != ECrowdWorkerDomainId::FlowResource
      || Work.Key.Kind != ECrowdWorkerWorkKind::Resource
      || !Context.Resources->FindCurrent(Work.Key.ScopeKey))
      return false;
    if (Work.Key.ScopeKey == CrowdWorkerResourceIds::NavTopology)
    {
      const FCrowdWorkerResourceRecord* Resource =
        Context.Resources->FindCurrent(Work.Key.ScopeKey);
      uint32 TopologyRevision = 0;
      FCrowdNavSurfaceGraph Graph;
      if (!Resource
        || !FCrowdWorkerNavTopologyCodec::Decode(
          Resource->Payload, TopologyRevision, Graph)
        || TopologyRevision != Resource->Revision)
      {
        UE_LOG(LogTemp, Error,
          TEXT("CrowdWorkerV2FlowResourceRejected resource=%llu revision=%llu decoded_revision=%u schema=%u bytes=%d"),
          Work.Key.ScopeKey,
          Resource ? Resource->Revision : 0,
          TopologyRevision,
          Resource ? Resource->Payload.SchemaId : 0,
          Resource ? Resource->Payload.Bytes.Num() : 0);
        return false;
      }
    }
  }
  return true;
}

void FCrowdWorkerMovementDomainExecutor::GetDependencies(
  TArray<ECrowdWorkerDomainId>& OutDependencies) const
{
  OutDependencies = {
    ECrowdWorkerDomainId::FlowResource};
}

void FCrowdWorkerMovementPlanningDomainExecutor::GetDependencies(
  TArray<ECrowdWorkerDomainId>& OutDependencies) const
{
  OutDependencies = {
    ECrowdWorkerDomainId::Behavior,
    ECrowdWorkerDomainId::FlowResource,
    ECrowdWorkerDomainId::Target,
    ECrowdWorkerDomainId::CombatReactive};
}

bool FCrowdWorkerMovementPlanningDomainExecutor::Execute(
  const FCrowdWorkerDomainContext& Context,
  const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
  FCrowdWorkerDomainOutput& OutOutput)
{
  auto Reject = [&Context](
    const TCHAR* Stage,
    const FCrowdStableEntityRef& EntityRef = {},
    const uint64 ActualSequence = 0,
    const int64 ActualFixedStep = INDEX_NONE,
    const int64 ExpectedFixedStep = INDEX_NONE)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerMovementPlanningRejected stage=%s entity=%u:%llu:%u generation=%llu epoch=%llu input=%llu actual_input=%llu actual_fixed_step=%lld expected_fixed_step=%lld"),
      Stage,
      EntityRef.ProviderId,
      EntityRef.StableEntityId,
      EntityRef.LifecycleSerial,
      Context.Generation,
      Context.WorkerEpoch,
      Context.LastAppliedInputSequence,
      ActualSequence,
      ActualFixedStep,
      ExpectedFixedStep);
    return false;
  };
  if (!Context.EntityStates || !Context.Resources
    || WorkItems.Num() != 1
    || WorkItems[0].Key.Domain
      != ECrowdWorkerDomainId::MovementPlanning
    || WorkItems[0].Key.Kind
      != ECrowdWorkerWorkKind::Resource
    || WorkItems[0].Key.ScopeKey
      != CrowdWorkerResourceIds::MovementControl)
    return Reject(TEXT("context"));
  const FCrowdWorkerResourceRecord* Record =
    Context.Resources->FindCurrent(
      CrowdWorkerResourceIds::MovementControl);
  FCrowdWorkerMovementControlResource Control;
  if (!Record
    || !FCrowdWorkerMovementControlResourceCodec::Decode(
      Record->Payload, Control)
    || Control.Revision != Record->Revision)
    return Reject(TEXT("movement_control"));

  TMap<int32, const FCrowdLocalPredictiveResult*> ResultByAgentId;
  FCrowdMassLocalPredictiveWorkOutput LocalOutput;
  TMap<FCrowdStableEntityRef, FVector> TargetVelocityByEntity;
  TMap<FCrowdStableEntityRef, FCrowdWorkerBehaviorState>
    BehaviorByEntity;
  TMap<FCrowdStableEntityRef, FCrowdWorkerCombatState>
    CombatByEntity;
  for (const FCrowdWorkerMovementControlEntry& Entry :
    Control.Entries)
  {
    if (!Entry.bUseWorkerTargetGuidance)
      continue;
    const FCrowdWorkerDirtyStateRecord* TargetRecord =
      Context.EntityStates->Find(
        Entry.EntityRef, ECrowdWorkerField::Target);
    FCrowdWorkerTargetState TargetState;
    if (!TargetRecord
      || !FCrowdWorkerTargetStateCodec::Decode(
        TargetRecord->Payload, TargetState))
      return Reject(TEXT("target_state"), Entry.EntityRef,
        TargetRecord ? TargetRecord->SourceInputSequence : 0);
    TargetVelocityByEntity.Add(
      Entry.EntityRef, TargetState.DesiredVelocity);
  }
  for (const FCrowdWorkerMovementControlEntry& Entry :
    Control.Entries)
  {
    const FCrowdWorkerDirtyStateRecord* BehaviorRecord =
      Context.EntityStates->Find(
        Entry.EntityRef, ECrowdWorkerField::Behavior);
    if (!BehaviorRecord) continue;
    FCrowdWorkerBehaviorState Behavior;
    if (BehaviorRecord->SourceInputSequence
          > Context.LastAppliedInputSequence
      || !FCrowdWorkerBehaviorStateCodec::Decode(
        BehaviorRecord->Payload, Behavior)
      || Behavior.SourceSet.EntityRef != Entry.EntityRef
      || Behavior.LastFixedStep > Control.FixedStepIndex)
      return Reject(TEXT("behavior_state"), Entry.EntityRef,
        BehaviorRecord->SourceInputSequence,
        Behavior.LastFixedStep, Control.FixedStepIndex);
    BehaviorByEntity.Add(Entry.EntityRef, MoveTemp(Behavior));
  }
  for (const FCrowdWorkerMovementControlEntry& Entry :
    Control.Entries)
  {
    const FCrowdWorkerDirtyStateRecord* CombatRecord =
      Context.EntityStates->Find(
        Entry.EntityRef, ECrowdWorkerField::Combat);
    if (!CombatRecord) continue;
    FCrowdWorkerCombatState Combat;
    if (CombatRecord->SourceInputSequence
          > Context.LastAppliedInputSequence
      || !FCrowdWorkerCombatStateCodec::Decode(
        CombatRecord->Payload, Combat)
      || Combat.SourceFixedStep > Control.FixedStepIndex)
      return Reject(TEXT("combat_state"), Entry.EntityRef,
        CombatRecord->SourceInputSequence);
    CombatByEntity.Add(Entry.EntityRef, MoveTemp(Combat));
  }
  if (Control.bRunLocalPredictive)
  {
    FCrowdMassLocalPredictiveWorkInput Input;
    Input.FixedStepIndex = Control.FixedStepIndex;
    Input.PlanRevision = Control.PlanRevision;
    Input.Environment = Control.Environment;
    Input.Settings = Control.LocalPredictiveSettings;
    Input.PreviousGrantStates = Control.PreviousGrantStates;
    Input.Agents.Reserve(Control.Entries.Num());
    for (const FCrowdWorkerMovementControlEntry& Entry :
      Control.Entries)
    {
      const FCrowdWorkerDirtyStateRecord* Snapshot =
        Context.EntityStates->Find(
          Entry.EntityRef, ECrowdWorkerField::InputSnapshot);
      FCrowdWorkerBoundaryKinematicState Kinematic;
      if (!Snapshot
        || !FCrowdWorkerBoundaryStateCodec::DecodeKinematicState(
          Snapshot->Payload, Kinematic)
        || Kinematic.PlanRevision != Control.PlanRevision)
        return Reject(TEXT("kinematic_state"), Entry.EntityRef,
          Snapshot ? Snapshot->SourceInputSequence : 0,
          Kinematic.PlanRevision, Control.PlanRevision);
      if (Context.RuntimeMode
          == ECrowdWorkerRuntimeV2Mode::Production)
      {
        const FCrowdWorkerDirtyStateRecord* FinalRecord =
          Context.EntityStates->Find(
            Entry.EntityRef, ECrowdWorkerField::Facing);
        if (!FinalRecord)
        {
          FinalRecord = Context.EntityStates->Find(
            Entry.EntityRef, ECrowdWorkerField::Movement);
        }
        if (FinalRecord)
        {
          FCrowdWorkerMovementState FinalState;
          if (!FCrowdWorkerMovementStateCodec::Decode(
              FinalRecord->Payload, FinalState))
            return false;
          Kinematic.Position = FinalState.Position;
          Kinematic.Velocity = FinalState.Velocity;
          Kinematic.YawDegrees = FinalState.YawDegrees;
        }
      }
      FCrowdLocalPredictiveAgent& Agent =
        Input.Agents.AddDefaulted_GetRef();
      Agent.AgentId = Entry.AgentId;
      Agent.InteractionLayer = Entry.InteractionLayer;
      Agent.Position = FVector2f(
        Kinematic.Position.X, Kinematic.Position.Y);
      Agent.Velocity = FVector2f(
        Kinematic.Velocity.X, Kinematic.Velocity.Y);
      const FVector* TargetVelocity =
        TargetVelocityByEntity.Find(Entry.EntityRef);
      FVector PreferredVelocity =
        TargetVelocity
          ? *TargetVelocity
          : Entry.AutonomousPreferredVelocity;
      float EffectiveMaximumSpeedCmps =
        Entry.MaximumSpeedCmps;
      bool bMovementLocked = false;
      if (const FCrowdWorkerBehaviorState* Behavior =
        BehaviorByEntity.Find(Entry.EntityRef))
      {
        const FCrowdResolvedBehaviorChannels& Resolved =
          Behavior->ResolvedChannels;
        EffectiveMaximumSpeedCmps = FMath::Min(
          EffectiveMaximumSpeedCmps,
          FMath::Max(0.0f, Resolved.SpeedLimitCmps));
        if (!TargetVelocity && !Resolved.MovementGoal.bHasGoal)
          PreferredVelocity = Resolved.DesiredVelocity;
        bMovementLocked = Resolved.bMovementLocked;
        if (bMovementLocked
          || (Resolved.AllowedNavLayerMask
            & (uint64{1} << FMath::Min<uint32>(
              Entry.InteractionLayer, 63u))) == 0)
          PreferredVelocity = FVector::ZeroVector;
        else
          PreferredVelocity = PreferredVelocity.GetClampedToMaxSize(
            EffectiveMaximumSpeedCmps);
      }
      if (const FCrowdWorkerCombatState* Combat =
        CombatByEntity.Find(Entry.EntityRef))
      {
        if (!Combat->bAlive)
        {
          PreferredVelocity = FVector::ZeroVector;
          EffectiveMaximumSpeedCmps = 0.0f;
          bMovementLocked = true;
        }
        else if (Combat->bReactiveActive)
        {
          PreferredVelocity =
            Combat->HorizontalReactiveVelocity;
          bMovementLocked = false;
        }
      }
      if (bMovementLocked)
        EffectiveMaximumSpeedCmps = 0.0f;
      Agent.PreferredVelocity = FVector2f(
        PreferredVelocity.X, PreferredVelocity.Y);
      Agent.PhysicalRadiusCm = Kinematic.PhysicalRadiusCm;
      Agent.HardSafetyGapCm = Kinematic.HardSafetyGapCm;
      Agent.MaxSpeedCmps = EffectiveMaximumSpeedCmps;
      Agent.BlockedAgeSteps = Entry.PreviousBlockedAgeSteps;
    }
    LocalOutput = FCrowdMassLocalPredictiveWork::Solve(Input);
    if (!LocalOutput.bCompleted
      || LocalOutput.Results.Num() != Control.Entries.Num())
      return false;
    for (const FCrowdLocalPredictiveResult& Result :
      LocalOutput.Results)
    {
      if (ResultByAgentId.Contains(Result.AgentId))
        return false;
      ResultByAgentId.Add(Result.AgentId, &Result);
    }
  }

  for (const FCrowdWorkerMovementControlEntry& Entry :
    Control.Entries)
  {
    FPlanState Plan;
    if (const FVector* TargetVelocity =
      TargetVelocityByEntity.Find(Entry.EntityRef))
      Plan.AutonomousPreferredVelocity = *TargetVelocity;
    else
      Plan.AutonomousPreferredVelocity =
        Entry.AutonomousPreferredVelocity;
    if (const FCrowdWorkerBehaviorState* Behavior =
      BehaviorByEntity.Find(Entry.EntityRef))
    {
      const FCrowdResolvedBehaviorChannels& Resolved =
        Behavior->ResolvedChannels;
      const float EffectiveMaximumSpeedCmps = FMath::Min(
        Entry.MaximumSpeedCmps,
        FMath::Max(0.0f, Resolved.SpeedLimitCmps));
      if (!TargetVelocityByEntity.Contains(Entry.EntityRef)
        && !Resolved.MovementGoal.bHasGoal)
        Plan.AutonomousPreferredVelocity =
          Resolved.DesiredVelocity;
      Plan.bMovementLocked = Resolved.bMovementLocked;
      if (Plan.bMovementLocked
        || (Resolved.AllowedNavLayerMask
          & (uint64{1} << FMath::Min<uint32>(
            Entry.InteractionLayer, 63u))) == 0)
        Plan.AutonomousPreferredVelocity = FVector::ZeroVector;
      else
        Plan.AutonomousPreferredVelocity =
          Plan.AutonomousPreferredVelocity.GetClampedToMaxSize(
            EffectiveMaximumSpeedCmps);
    }
    if (const FCrowdWorkerCombatState* Combat =
      CombatByEntity.Find(Entry.EntityRef))
    {
      if (!Combat->bAlive)
      {
        Plan.AutonomousPreferredVelocity = FVector::ZeroVector;
        Plan.bMovementLocked = true;
      }
      else if (Combat->bReactiveActive)
      {
        Plan.AutonomousPreferredVelocity =
          Combat->HorizontalReactiveVelocity;
        Plan.bMovementLocked = false;
      }
    }
    Plan.bUseLocalVelocity = Control.bRunLocalPredictive;
    if (Control.bRunLocalPredictive)
    {
      const FCrowdLocalPredictiveResult* const* Result =
        ResultByAgentId.Find(Entry.AgentId);
      if (!Result)
        return false;
      Plan.LocalVelocity = FVector(
        (*Result)->Velocity.X, (*Result)->Velocity.Y, 0.0f);
      Plan.bLocalVelocityValid = (*Result)->bValid;
    }
    FCrowdWorkerDirtyStateRecord Dirty;
    Dirty.EntityRef = Entry.EntityRef;
    Dirty.Field = ECrowdWorkerField::MovementPlan;
    Dirty.Generation = Context.Generation;
    Dirty.WorkerEpoch = Context.WorkerEpoch;
    Dirty.StateRevision = Context.WorkerEpoch;
    Dirty.SourceInputSequence =
      Context.LastAppliedInputSequence;
    if (!Encode(Plan, Dirty.Payload))
      return false;
    OutOutput.DirtyStates.Add(MoveTemp(Dirty));

    FCrowdWorkerWorkItem Movement;
    Movement.Key.Domain = ECrowdWorkerDomainId::Movement;
    Movement.Key.Kind = ECrowdWorkerWorkKind::Entity;
    Movement.Key.PrimaryEntity = Entry.EntityRef;
    Movement.Priority = ECrowdWorkerWorkPriority::Normal;
    Movement.ReasonMask = 1ull << 9;
    OutOutput.NextWork.Add(MoveTemp(Movement));
  }
  return true;
}

bool FCrowdWorkerMovementDomainExecutor::Execute(
  const FCrowdWorkerDomainContext& Context,
  const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
  FCrowdWorkerDomainOutput& OutOutput)
{
  if (!Context.EntityStates || !Context.Resources
    || Context.Generation == 0 || Context.WorkerEpoch == 0
    || !FMath::IsFinite(Context.FixedDeltaSeconds)
    || Context.FixedDeltaSeconds <= 0.0)
    return false;

  FCrowdWorkerFlowFieldResource FlowField;
  bool bHasFlowField = false;
  if (const FCrowdWorkerResourceRecord* Environment =
    Context.Resources->FindCurrent(
      CrowdWorkerResourceIds::Environment))
  {
    if (!FCrowdWorkerFlowFieldResourceCodec::Decode(
        Environment->Payload, FlowField)
      || FlowField.Revision != Environment->Revision)
      return false;
    bHasFlowField = true;
  }
  FCrowdWorkerMovementControlResource MovementControl;
  bool bHasMovementControl = false;
  if (const FCrowdWorkerResourceRecord* Control =
    Context.Resources->FindCurrent(
      CrowdWorkerResourceIds::MovementControl))
  {
    if (!FCrowdWorkerMovementControlResourceCodec::Decode(
        Control->Payload, MovementControl)
      || MovementControl.Revision != Control->Revision)
      return false;
    bHasMovementControl = true;
  }

  for (const FCrowdWorkerWorkItem& Work : WorkItems)
  {
    if (Work.Key.Domain != ECrowdWorkerDomainId::Movement
      || !Context.EntityStates->Contains(
        Work.Key.PrimaryEntity))
      return false;

    FCrowdWorkerBoundaryKinematicState Kinematic;
    const FCrowdWorkerDirtyStateRecord* Input =
      Context.EntityStates->Find(
        Work.Key.PrimaryEntity,
        ECrowdWorkerField::InputSnapshot);
    const bool bHasKinematic =
      Input
      && FCrowdWorkerBoundaryStateCodec::DecodeKinematicState(
        Input->Payload, Kinematic);
    const FCrowdWorkerMovementControlEntry* Control =
      bHasMovementControl
        ? MovementControl.Find(Work.Key.PrimaryEntity)
        : nullptr;
    if (bHasMovementControl && !Control)
      return false;
    const float MaximumSpeedCmps =
      Control ? Control->MaximumSpeedCmps
        : (bHasKinematic ? Kinematic.MaximumSpeedCmps : 0.0f);
    FPlanState Plan;
    bool bHasPlan = false;
    if (const FCrowdWorkerDirtyStateRecord* PlanRecord =
      Context.EntityStates->Find(
        Work.Key.PrimaryEntity,
        ECrowdWorkerField::MovementPlan))
    {
      if (!Decode(PlanRecord->Payload, Plan)
        || PlanRecord->SourceInputSequence
          != Context.LastAppliedInputSequence)
        return false;
      bHasPlan = true;
    }
    FCrowdWorkerCombatState Combat;
    const FCrowdWorkerDirtyStateRecord* CombatRecord =
      Context.EntityStates->Find(
        Work.Key.PrimaryEntity, ECrowdWorkerField::Combat);
    const bool bHasCombat = CombatRecord != nullptr;
    if (bHasCombat
      && (CombatRecord->SourceInputSequence
            != Context.LastAppliedInputSequence
        || !FCrowdWorkerCombatStateCodec::Decode(
          CombatRecord->Payload, Combat)))
      return false;

    FCrowdWorkerMovementState Movement;
    uint64 PreviousStateRevision = 0;
    uint64 CorrectionRevision = Work.CorrectionRevision;
    const FCrowdWorkerDirtyStateRecord* Current = nullptr;
    if (Context.RuntimeMode == ECrowdWorkerRuntimeV2Mode::Production)
    {
      Current = Context.EntityStates->Find(
        Work.Key.PrimaryEntity, ECrowdWorkerField::Facing);
    }
    if (!Current)
    {
      Current = Context.EntityStates->Find(
        Work.Key.PrimaryEntity, ECrowdWorkerField::Movement);
    }
    if (Current)
    {
      if (!FCrowdWorkerMovementStateCodec::Decode(
          Current->Payload, Movement))
        return false;
      PreviousStateRevision = Current->StateRevision;
      CorrectionRevision = FMath::Max(
        CorrectionRevision, Current->CorrectionRevision);
    }
    else
    {
      if (!bHasKinematic) return false;
      Movement.Position = Kinematic.Position;
      Movement.Velocity = Kinematic.Velocity.GetClampedToMaxSize(
        MaximumSpeedCmps);
      Movement.YawDegrees = Kinematic.YawDegrees;
      Movement.SimulationTimeSeconds =
        FMath::Max(
          0.0,
          Context.SimulationTimeSeconds
            - Context.FixedDeltaSeconds);
    }

    const bool bUseShadowInputBaseline =
      Context.RuntimeMode == ECrowdWorkerRuntimeV2Mode::Shadow
      && bHasKinematic
      && (Work.ReasonMask
        & ((1ull << 3) | (1ull << 6))) != 0;
    if (bUseShadowInputBaseline)
    {
      Movement.Position = Kinematic.Position;
      Movement.Velocity = Kinematic.Velocity.GetClampedToMaxSize(
        MaximumSpeedCmps);
      Movement.YawDegrees = Kinematic.YawDegrees;
    }
    if (Control && Control->bFreezeAtBoundaryLocation)
    {
      Movement.Position = Control->BoundaryLocation;
      Movement.Velocity = FVector::ZeroVector;
    }
    else if (Control)
    {
      if (bHasMovementControl
        && MovementControl.bRunLocalPredictive
        && !bHasPlan)
      {
        // A local-predictive resource is a hard request for Worker planning.
        // Falling back to the Legacy-computed velocity would create a hidden
        // second authority and make Canary/Production evidence meaningless.
        return false;
      }
      const bool bUseLocalVelocity =
        bHasPlan ? Plan.bUseLocalVelocity
          : Control->bUseLocalVelocity;
      const bool bLocalVelocityValid =
        bHasPlan ? Plan.bLocalVelocityValid
          : Control->bLocalVelocityValid;
      const FVector LocalVelocity =
        bHasPlan ? Plan.LocalVelocity
          : Control->LocalVelocity;
      Movement.Velocity = bUseLocalVelocity
        ? (bLocalVelocityValid
          ? LocalVelocity.GetClampedToMaxSize2D(
            MaximumSpeedCmps)
          : FVector::ZeroVector)
        : (bHasPlan
          ? Plan.AutonomousPreferredVelocity
          : Control->AutonomousPreferredVelocity);
      if (bHasPlan && Plan.bMovementLocked)
        Movement.Velocity = FVector::ZeroVector;
      if (bHasCombat && !Combat.bAlive)
        Movement.Velocity = FVector::ZeroVector;
    }
    else if (bHasFlowField)
    {
      if (!bHasKinematic) return false;
      FVector FlowDirection;
      bool bReachable = false;
      if (!FlowField.Sample(
          Movement.Position, FlowDirection, bReachable))
        return false;
      if (bReachable)
        Movement.Velocity =
          FlowDirection * MaximumSpeedCmps;
    }
    const FVector MovementStartPosition = Movement.Position;
    Movement.StartPosition = MovementStartPosition;
    Movement.Position +=
      Movement.Velocity * Context.FixedDeltaSeconds;
    if (bHasCombat && Combat.bReactiveActive
      && (!Control || !Control->bFreezeAtBoundaryLocation))
    {
      Movement.Position.Z = Combat.ProposedZ;
      Movement.Velocity.Z = Combat.VerticalVelocityCmps;
    }
    else if (Control && Control->bVerticalOverride
      && !Control->bFreezeAtBoundaryLocation)
    {
      Movement.Position.Z = Control->ProposedZ;
      Movement.Velocity.Z = Control->VerticalVelocityCmps;
    }
    else
    {
      if (Control && Control->bFreezeAtBoundaryLocation)
        Movement.Position.Z = Control->BoundaryLocation.Z;
      if (!Control || !Control->bVerticalOverride)
        Movement.Velocity.Z = 0.0f;
    }
    if (bHasMovementControl
      && MovementControl.bApplyEnvironmentMovementConstraint)
    {
      const FCrowdSharedFlowConstraintResult Constraint =
        FCrowdSharedFlowFieldKernel::ConstrainMovement(
          MovementControl.Environment,
          MovementStartPosition,
          Movement.Position,
          static_cast<float>(Context.FixedDeltaSeconds),
          false);
      Movement.Position = Constraint.Location;
      Movement.Velocity = Constraint.Velocity;
    }
    if (!Movement.Velocity.IsNearlyZero())
      Movement.YawDegrees = Movement.Velocity.Rotation().Yaw;
    Movement.SimulationTimeSeconds =
      Context.SimulationTimeSeconds;
    Movement.CorrectionRevision = CorrectionRevision;

    FCrowdWorkerDirtyStateRecord Dirty;
    Dirty.EntityRef = Work.Key.PrimaryEntity;
    Dirty.Field = ECrowdWorkerField::Movement;
    Dirty.Generation = Context.Generation;
    Dirty.WorkerEpoch = Context.WorkerEpoch;
    Dirty.StateRevision = FMath::Max(
      PreviousStateRevision + 1, Context.WorkerEpoch);
    Dirty.CorrectionRevision = CorrectionRevision;
    Dirty.SourceInputSequence =
      Context.LastAppliedInputSequence;
    if (!FCrowdWorkerMovementStateCodec::Encode(
        Movement, Dirty.Payload))
      return false;
    OutOutput.DirtyStates.Add(MoveTemp(Dirty));

    FCrowdWorkerWakeup Wakeup;
    Wakeup.Key.Domain = ECrowdWorkerDomainId::Movement;
    Wakeup.Key.EntityRef = Work.Key.PrimaryEntity;
    Wakeup.Key.WakeupId = 1;
    Wakeup.AbsoluteSimulationTick =
      Context.AbsoluteSimulationTick + 1;
    Wakeup.Revision = CorrectionRevision;
    Wakeup.Priority = ECrowdWorkerWorkPriority::Normal;
    Wakeup.ReasonMask = 1ull << 7;
    OutOutput.Wakeups.Add(MoveTemp(Wakeup));

    // Particle is resource-driven. Snapshot-only autonomous Movement is a
    // valid Worker mode, but it must not wake a domain whose required
    // MovementControl revision does not exist.
    if (bHasMovementControl)
    {
      FCrowdWorkerWorkItem Interaction;
      Interaction.Key.Domain =
        ECrowdWorkerDomainId::ParticleInteraction;
      Interaction.Key.Kind = ECrowdWorkerWorkKind::Resource;
      Interaction.Key.ScopeKey =
        CrowdWorkerResourceIds::MovementControl;
      Interaction.Priority = ECrowdWorkerWorkPriority::Normal;
      Interaction.CorrectionRevision = CorrectionRevision;
      Interaction.ReasonMask = 1ull << 8;
      OutOutput.NextWork.Add(MoveTemp(Interaction));
    }
  }
  return true;
}
