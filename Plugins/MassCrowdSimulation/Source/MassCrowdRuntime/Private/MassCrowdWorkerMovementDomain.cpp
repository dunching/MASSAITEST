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
  constexpr uint16 SchemaVersion = 4;

  struct FPlanState
  {
    FVector AutonomousPreferredVelocity = FVector::ZeroVector;
    FVector LocalVelocity = FVector::ZeroVector;
    bool bUseLocalVelocity = false;
    bool bLocalVelocityValid = false;
    bool bMovementLocked = false;
    int32 NextBlockedAgeSteps = 0;
    uint32 GrantComponentKey = 0;
    int32 GrantEpoch = 0;
    int32 GrantRemainingSteps = 0;
  };

  void MovementPlanAppendUnsigned(TArray<uint8>& Bytes, const uint32 Value)
  {
    for (uint32 Byte = 0; Byte < sizeof(Value); ++Byte)
      Bytes.Add(static_cast<uint8>(Value >> (Byte * 8)));
  }

  bool MovementPlanReadUnsigned(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    uint32& OutValue)
  {
    if (Offset < 0 || Offset + 4 > Bytes.Num())
      return false;
    OutValue = 0;
    for (uint32 Byte = 0; Byte < 4; ++Byte)
      OutValue |= static_cast<uint32>(Bytes[Offset + Byte])
        << (Byte * 8);
    Offset += 4;
    return true;
  }

  void MovementPlanAppendFloat(TArray<uint8>& Bytes, const float Value)
  {
    uint32 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    for (uint32 Byte = 0; Byte < sizeof(Bits); ++Byte)
      Bytes.Add(static_cast<uint8>(Bits >> (Byte * 8)));
  }

  bool MovementPlanReadFloat(
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
      || State.LocalVelocity.ContainsNaN()
      || State.NextBlockedAgeSteps < 0
      || State.GrantEpoch < 0
      || State.GrantRemainingSteps < 0)
      return false;
    OutPayload.SchemaId = SchemaId;
    OutPayload.SchemaVersion = SchemaVersion;
    OutPayload.Bytes.Add(static_cast<uint8>(
      (State.bUseLocalVelocity ? 1u : 0u)
      | (State.bLocalVelocityValid ? 2u : 0u)
      | (State.bMovementLocked ? 4u : 0u)));
    MovementPlanAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(State.AutonomousPreferredVelocity.X));
    MovementPlanAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(State.AutonomousPreferredVelocity.Y));
    MovementPlanAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(State.AutonomousPreferredVelocity.Z));
    MovementPlanAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(State.LocalVelocity.X));
    MovementPlanAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(State.LocalVelocity.Y));
    MovementPlanAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(State.LocalVelocity.Z));
    MovementPlanAppendUnsigned(
      OutPayload.Bytes,
      static_cast<uint32>(State.NextBlockedAgeSteps));
    MovementPlanAppendUnsigned(OutPayload.Bytes, State.GrantComponentKey);
    MovementPlanAppendUnsigned(
      OutPayload.Bytes, static_cast<uint32>(State.GrantEpoch));
    MovementPlanAppendUnsigned(
      OutPayload.Bytes,
      static_cast<uint32>(State.GrantRemainingSteps));
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
      || Payload.Bytes.Num() != 41
      || Payload.StableHash != Payload.CalculateStableHash()
      || (Payload.Bytes[0] & ~uint8{7}) != 0)
      return false;
    int32 Offset = 1;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    if (!MovementPlanReadFloat(Payload.Bytes, Offset, X)
      || !MovementPlanReadFloat(Payload.Bytes, Offset, Y)
      || !MovementPlanReadFloat(Payload.Bytes, Offset, Z))
      return false;
    OutState.AutonomousPreferredVelocity = FVector(X, Y, Z);
    if (!MovementPlanReadFloat(Payload.Bytes, Offset, X)
      || !MovementPlanReadFloat(Payload.Bytes, Offset, Y)
      || !MovementPlanReadFloat(Payload.Bytes, Offset, Z))
      return false;
    OutState.LocalVelocity = FVector(X, Y, Z);
    OutState.bUseLocalVelocity =
      (Payload.Bytes[0] & 1u) != 0;
    OutState.bLocalVelocityValid =
      (Payload.Bytes[0] & 2u) != 0;
    OutState.bMovementLocked =
      (Payload.Bytes[0] & 4u) != 0;
    uint32 NextBlockedAgeSteps = 0;
    uint32 GrantEpoch = 0;
    uint32 GrantRemainingSteps = 0;
    if (!MovementPlanReadUnsigned(
        Payload.Bytes, Offset, NextBlockedAgeSteps)
      || !MovementPlanReadUnsigned(
        Payload.Bytes, Offset, OutState.GrantComponentKey)
      || !MovementPlanReadUnsigned(Payload.Bytes, Offset, GrantEpoch)
      || !MovementPlanReadUnsigned(
        Payload.Bytes, Offset, GrantRemainingSteps)
      || NextBlockedAgeSteps > static_cast<uint32>(MAX_int32)
      || GrantEpoch > static_cast<uint32>(MAX_int32)
      || GrantRemainingSteps > static_cast<uint32>(MAX_int32))
      return false;
    OutState.NextBlockedAgeSteps =
      static_cast<int32>(NextBlockedAgeSteps);
    OutState.GrantEpoch = static_cast<int32>(GrantEpoch);
    OutState.GrantRemainingSteps =
      static_cast<int32>(GrantRemainingSteps);
    return Offset == Payload.Bytes.Num();
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
    || WorkItems.IsEmpty())
    return Reject(TEXT("context"));
  const FCrowdWorkerWorkItem* ResourceWork = nullptr;
  for (const FCrowdWorkerWorkItem& Work : WorkItems)
  {
    if (Work.Key.Domain
        != ECrowdWorkerDomainId::MovementPlanning)
      return Reject(TEXT("context"));
    if (Work.Key.Kind == ECrowdWorkerWorkKind::Resource)
    {
      if (Work.Key.ScopeKey
          != CrowdWorkerResourceIds::MovementControl)
        return Reject(TEXT("context"));
      if (!ResourceWork)
        ResourceWork = &Work;
    }
    else if (Work.Key.Kind != ECrowdWorkerWorkKind::Entity)
      return Reject(TEXT("context"));
  }
  if (!ResourceWork)
  {
    if (WorkItems.Num() != 1)
      return Reject(TEXT("context"));
    const FCrowdStableEntityRef EntityRef =
      WorkItems[0].Key.PrimaryEntity;
    const FCrowdWorkerDirtyStateRecord* ProfileRecord =
      Context.EntityStates->Find(
        EntityRef, ECrowdWorkerField::MovementProfile);
    FCrowdWorkerMovementControlEntry Profile;
    if (!ProfileRecord
      || ProfileRecord->SourceInputSequence
        > Context.LastAppliedInputSequence
      || !FCrowdWorkerMovementProfileCodec::Decode(
        ProfileRecord->Payload, Profile)
      || Profile.EntityRef != EntityRef)
      return Reject(TEXT("movement_profile"), EntityRef,
        ProfileRecord ? ProfileRecord->SourceInputSequence : 0);
    return true;
  }
  // A new complete MovementControl resource invalidates the whole closed
  // planning set. Per-entity invalidations and an anchored TimeWheel resource
  // continuation propagated in the same round are covered by this resource
  // replan and must not execute a second plan for the same entity.
  const FCrowdWorkerResourceRecord* Record =
    Context.Resources->FindCurrent(
      CrowdWorkerResourceIds::MovementControl);
  FCrowdWorkerMovementControlResource Control;
  if (!Record
    || !FCrowdWorkerMovementControlResourceCodec::Decode(
      Record->Payload, Control)
    || Control.Revision != Record->Revision)
    return Reject(TEXT("movement_control"));
  TArray<FCrowdWorkerMovementControlEntry> Profiles;
  if (!CrowdWorkerResolveMovementProfiles(
      *Context.EntityStates,
      Context.LastAppliedInputSequence,
      Control,
      Profiles))
    return Reject(TEXT("movement_profiles"));
  if (Context.AbsoluteSimulationTick
      > static_cast<uint64>(MAX_int32))
    return Reject(TEXT("simulation_tick"));
  const int32 CurrentFixedStepIndex =
    static_cast<int32>(Context.AbsoluteSimulationTick);

  FCrowdWorkerFlowFieldResource FlowField;
  bool bHasFlowField = false;
  if (const FCrowdWorkerResourceRecord* Environment =
    Context.Resources->FindCurrent(
      CrowdWorkerResourceIds::Environment))
  {
    if (!FCrowdWorkerFlowFieldResourceCodec::Decode(
        Environment->Payload, FlowField)
      || FlowField.Revision != Environment->Revision)
      return Reject(TEXT("flow_field"));
    bHasFlowField = true;
  }

  TMap<int32, const FCrowdLocalPredictiveResult*> ResultByAgentId;
  TMap<int32, const FCrowdLocalPredictiveGrantState*>
    GrantByAgentId;
  FCrowdMassLocalPredictiveWorkOutput LocalOutput;
  TMap<FCrowdStableEntityRef, FVector> TargetVelocityByEntity;
  TMap<FCrowdStableEntityRef, FVector> FlowVelocityByEntity;
  TMap<FCrowdStableEntityRef, FCrowdWorkerBehaviorState>
    BehaviorByEntity;
  TMap<FCrowdStableEntityRef, FCrowdWorkerCombatState>
    CombatByEntity;
  TMap<FCrowdStableEntityRef, FPlanState> PreviousPlanByEntity;
  bool bHeterogeneousParticleProfiles = false;
  if (!Profiles.IsEmpty())
  {
    const FCrowdWorkerMovementControlEntry& FirstProfile =
      Profiles[0];
    for (int32 Index = 1; Index < Profiles.Num(); ++Index)
    {
      const FCrowdWorkerMovementControlEntry& Profile = Profiles[Index];
      if (!FMath::IsNearlyEqual(
            Profile.ParticlePhysicalRadiusCm,
            FirstProfile.ParticlePhysicalRadiusCm)
        || !FMath::IsNearlyEqual(
            Profile.ParticleHardSafetyGapCm,
            FirstProfile.ParticleHardSafetyGapCm)
        || !FMath::IsNearlyEqual(
            Profile.ParticleSoftMarginCm,
            FirstProfile.ParticleSoftMarginCm)
        || !FMath::IsNearlyEqual(
            Profile.ParticleMobility,
            FirstProfile.ParticleMobility))
      {
        bHeterogeneousParticleProfiles = true;
        break;
      }
    }
  }
  for (const FCrowdWorkerMovementControlEntry& Entry :
    Profiles)
  {
    if (!Entry.bUseWorkerTargetGuidance)
      continue;
    const FCrowdWorkerDirtyStateRecord* TargetRecord =
      Context.EntityStates->Find(
        Entry.EntityRef, ECrowdWorkerField::Target);
    if (!TargetRecord) continue;
    FCrowdWorkerTargetState TargetState;
    if (!FCrowdWorkerTargetStateCodec::Decode(
        TargetRecord->Payload, TargetState))
      return Reject(TEXT("target_state"), Entry.EntityRef,
        TargetRecord->SourceInputSequence);
    TargetVelocityByEntity.Add(
      Entry.EntityRef, TargetState.DesiredVelocity);
  }
  for (const FCrowdWorkerMovementControlEntry& Entry :
    Profiles)
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
      || Behavior.LastFixedStep > CurrentFixedStepIndex)
      return Reject(TEXT("behavior_state"), Entry.EntityRef,
        BehaviorRecord->SourceInputSequence,
        Behavior.LastFixedStep, CurrentFixedStepIndex);
    BehaviorByEntity.Add(Entry.EntityRef, MoveTemp(Behavior));
  }
  for (const FCrowdWorkerMovementControlEntry& Entry :
    Profiles)
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
      || Combat.SourceFixedStep > CurrentFixedStepIndex)
      return Reject(TEXT("combat_state"), Entry.EntityRef,
        CombatRecord->SourceInputSequence);
    CombatByEntity.Add(Entry.EntityRef, MoveTemp(Combat));
  }
  for (const FCrowdWorkerMovementControlEntry& Entry :
    Profiles)
  {
    const FCrowdWorkerDirtyStateRecord* PlanRecord =
      Context.EntityStates->Find(
        Entry.EntityRef, ECrowdWorkerField::MovementPlan);
    if (!PlanRecord) continue;
    FPlanState PreviousPlan;
    if (PlanRecord->SourceInputSequence
          > Context.LastAppliedInputSequence
      || !Decode(PlanRecord->Payload, PreviousPlan))
      return Reject(TEXT("previous_plan"), Entry.EntityRef,
        PlanRecord->SourceInputSequence);
    PreviousPlanByEntity.Add(
      Entry.EntityRef, MoveTemp(PreviousPlan));
  }
  if (Control.bRunLocalPredictive)
  {
    FCrowdMassLocalPredictiveWorkInput Input;
    Input.FixedStepIndex = CurrentFixedStepIndex;
    Input.PlanRevision = Control.PlanRevision;
    Input.Environment = Control.Environment;
    Input.Settings = Control.LocalPredictiveSettings;
    if (PreviousPlanByEntity.IsEmpty())
    {
      Input.PreviousGrantStates = Control.PreviousGrantStates;
    }
    else
    {
      for (const TPair<FCrowdStableEntityRef, FPlanState>& Pair :
        PreviousPlanByEntity)
      {
        const FPlanState& PreviousPlan = Pair.Value;
        if (PreviousPlan.GrantComponentKey == 0
          || PreviousPlan.GrantRemainingSteps <= 0)
          continue;
        FCrowdLocalPredictiveGrantState& Grant =
          Input.PreviousGrantStates.AddDefaulted_GetRef();
        Grant.ComponentKey = PreviousPlan.GrantComponentKey;
        const FCrowdWorkerMovementControlEntry* Entry =
          Profiles.FindByPredicate([
            &Pair](const FCrowdWorkerMovementControlEntry& Candidate)
          {
            return Candidate.EntityRef == Pair.Key;
          });
        if (!Entry) return Reject(TEXT("previous_grant_entity"));
        Grant.GrantedAgentId = Entry->AgentId;
        Grant.GrantEpoch = PreviousPlan.GrantEpoch;
        Grant.RemainingSteps =
          PreviousPlan.GrantRemainingSteps;
      }
      Input.PreviousGrantStates.Sort([](
        const FCrowdLocalPredictiveGrantState& A,
        const FCrowdLocalPredictiveGrantState& B)
      {
        return A.ComponentKey < B.ComponentKey;
      });
      for (int32 Index = 1;
        Index < Input.PreviousGrantStates.Num(); ++Index)
      {
        if (Input.PreviousGrantStates[Index - 1].ComponentKey
          == Input.PreviousGrantStates[Index].ComponentKey)
          return Reject(TEXT("duplicate_previous_grant"));
      }
    }
    Input.Agents.Reserve(Profiles.Num());
    for (const FCrowdWorkerMovementControlEntry& Entry :
      Profiles)
    {
      const FCrowdWorkerDirtyStateRecord* Snapshot =
        Context.EntityStates->Find(
          Entry.EntityRef, ECrowdWorkerField::InputSnapshot);
      FCrowdWorkerBoundaryKinematicState Kinematic;
      if (!Snapshot
        || !FCrowdWorkerBoundaryStateCodec::DecodeKinematicState(
          Snapshot->Payload, Kinematic)
        || Kinematic.PlanRevision != Control.PlanRevision
        || Snapshot->SourceInputSequence
          > Context.LastAppliedInputSequence)
        return Reject(TEXT("kinematic_state"), Entry.EntityRef,
          Snapshot ? Snapshot->SourceInputSequence : 0,
          Kinematic.PlanRevision, Control.PlanRevision);
      const bool bSnapshotIsCurrent =
        Snapshot->SourceInputSequence
          == Context.LastAppliedInputSequence;
      if (Context.RuntimeMode
          == ECrowdWorkerRuntimeV2Mode::Production
        || !bSnapshotIsCurrent)
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
      FVector DiagnosticFlowDirection = FVector::ZeroVector;
      bool bDiagnosticFlowReachable = false;
      bool bDiagnosticFlowSampled = false;
      if (!Entry.bUseAuthoritativePreferredVelocity
        && !TargetVelocity && bHasFlowField)
      {
        FVector FlowDirection;
        bool bReachable = false;
        if (!FlowField.Sample(
            Kinematic.Position, FlowDirection, bReachable))
          return Reject(TEXT("flow_sample"), Entry.EntityRef);
        DiagnosticFlowDirection = FlowDirection;
        bDiagnosticFlowReachable = bReachable;
        bDiagnosticFlowSampled = true;
        if (bReachable)
        {
          PreferredVelocity =
            FlowDirection * Entry.MaximumSpeedCmps;
          FlowVelocityByEntity.Add(
            Entry.EntityRef, PreferredVelocity);
        }
      }
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
        if (!Entry.bUseAuthoritativePreferredVelocity
          && !TargetVelocity
          && !FlowVelocityByEntity.Contains(Entry.EntityRef))
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
        else if (Combat->bMovementLocked)
        {
          PreferredVelocity = FVector::ZeroVector;
          bMovementLocked = true;
        }
      }
      if (bMovementLocked)
        EffectiveMaximumSpeedCmps = 0.0f;
      if (bHeterogeneousParticleProfiles && Context.WorkerEpoch == 1)
      {
        const FCrowdWorkerBehaviorState* Behavior =
          BehaviorByEntity.Find(Entry.EntityRef);
        UE_LOG(LogTemp, Display,
          TEXT("CrowdWorkerHeterogeneousMovementBootstrap generation=%llu worker_epoch=%llu absolute_tick=%llu input_sequence=%llu control_revision=%llu plan_revision=%d agent=%d provider=%u stable_entity=%llu lifecycle=%u position=(%.3f,%.3f,%.3f) radius=%.3f hard_gap=%.3f soft_margin=%.3f mobility=%.3f environment_clearance=%.3f max_speed=%.3f authoritative=%d worker_target_guidance=%d target_velocity=%d flow_present=%d flow_sampled=%d flow_reachable=%d flow_direction=(%.3f,%.3f,%.3f) behavior_present=%d behavior_speed_limit=%.3f behavior_locked=%d behavior_nav_mask=%llu preferred=(%.3f,%.3f,%.3f) effective_max_speed=%.3f source=WorkerMovementPlanning"),
          Context.Generation, Context.WorkerEpoch,
          Context.AbsoluteSimulationTick,
          Context.LastAppliedInputSequence,
          Control.Revision, Control.PlanRevision,
          Entry.AgentId,
          Entry.EntityRef.ProviderId,
          Entry.EntityRef.StableEntityId,
          Entry.EntityRef.LifecycleSerial,
          Kinematic.Position.X, Kinematic.Position.Y,
          Kinematic.Position.Z,
          Entry.ParticlePhysicalRadiusCm,
          Entry.ParticleHardSafetyGapCm,
          Entry.ParticleSoftMarginCm,
          Entry.ParticleMobility,
          Entry.ParticleEnvironmentHardClearanceCm,
          Entry.MaximumSpeedCmps,
          Entry.bUseAuthoritativePreferredVelocity ? 1 : 0,
          Entry.bUseWorkerTargetGuidance ? 1 : 0,
          TargetVelocity ? 1 : 0,
          bHasFlowField ? 1 : 0,
          bDiagnosticFlowSampled ? 1 : 0,
          bDiagnosticFlowReachable ? 1 : 0,
          DiagnosticFlowDirection.X,
          DiagnosticFlowDirection.Y,
          DiagnosticFlowDirection.Z,
          Behavior ? 1 : 0,
          Behavior ? Behavior->ResolvedChannels.SpeedLimitCmps : -1.0f,
          Behavior && Behavior->ResolvedChannels.bMovementLocked ? 1 : 0,
          Behavior ? Behavior->ResolvedChannels.AllowedNavLayerMask : 0,
          PreferredVelocity.X, PreferredVelocity.Y,
          PreferredVelocity.Z, EffectiveMaximumSpeedCmps);
      }
      Agent.PreferredVelocity = FVector2f(
        PreferredVelocity.X, PreferredVelocity.Y);
      Agent.PhysicalRadiusCm = Kinematic.PhysicalRadiusCm;
      Agent.HardSafetyGapCm = Kinematic.HardSafetyGapCm;
      Agent.MaxSpeedCmps = EffectiveMaximumSpeedCmps;
      const FPlanState* PreviousPlan =
        PreviousPlanByEntity.Find(Entry.EntityRef);
      Agent.BlockedAgeSteps = PreviousPlan
        ? PreviousPlan->NextBlockedAgeSteps
        : Entry.PreviousBlockedAgeSteps;
    }
    LocalOutput = FCrowdMassLocalPredictiveWork::Solve(Input);
    if (!LocalOutput.bCompleted
      || LocalOutput.Results.Num() != Profiles.Num())
      return false;
    for (const FCrowdLocalPredictiveResult& Result :
      LocalOutput.Results)
    {
      if (ResultByAgentId.Contains(Result.AgentId))
        return false;
      ResultByAgentId.Add(Result.AgentId, &Result);
    }
    for (const FCrowdLocalPredictiveGrantState& Grant :
      LocalOutput.GrantStates)
    {
      if (GrantByAgentId.Contains(Grant.GrantedAgentId))
        return false;
      GrantByAgentId.Add(Grant.GrantedAgentId, &Grant);
    }
  }

  for (const FCrowdWorkerMovementControlEntry& Entry :
    Profiles)
  {
    FPlanState Plan;
    if (const FVector* TargetVelocity =
      TargetVelocityByEntity.Find(Entry.EntityRef))
      Plan.AutonomousPreferredVelocity = *TargetVelocity;
    else if (const FVector* FlowVelocity =
      FlowVelocityByEntity.Find(Entry.EntityRef))
      Plan.AutonomousPreferredVelocity = *FlowVelocity;
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
      if (!Entry.bUseAuthoritativePreferredVelocity
        && !TargetVelocityByEntity.Contains(Entry.EntityRef)
        && !FlowVelocityByEntity.Contains(Entry.EntityRef))
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
      else if (Combat->bMovementLocked)
      {
        Plan.AutonomousPreferredVelocity = FVector::ZeroVector;
        Plan.bMovementLocked = true;
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
      Plan.NextBlockedAgeSteps =
        (*Result)->NextBlockedAgeSteps;
      if (const FCrowdLocalPredictiveGrantState* const* Grant =
        GrantByAgentId.Find(Entry.AgentId))
      {
        Plan.GrantComponentKey = (*Grant)->ComponentKey;
        Plan.GrantEpoch = (*Grant)->GrantEpoch;
        Plan.GrantRemainingSteps = (*Grant)->RemainingSteps;
      }
    }
    FCrowdWorkerDirtyStateRecord Dirty;
    Dirty.EntityRef = Entry.EntityRef;
    Dirty.Field = ECrowdWorkerField::MovementPlan;
    Dirty.Generation = Context.Generation;
    Dirty.WorkerEpoch = Context.WorkerEpoch;
    const FCrowdWorkerDirtyStateRecord* ExistingPlan =
      Context.EntityStates->Find(
        Entry.EntityRef, ECrowdWorkerField::MovementPlan);
    Dirty.StateRevision = ExistingPlan
      ? FMath::Max(
          Context.WorkerEpoch, ExistingPlan->StateRevision + 1)
      : Context.WorkerEpoch;
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
  if (!Profiles.IsEmpty())
  {
    FCrowdWorkerWakeup Wakeup;
    Wakeup.Key.Domain = ECrowdWorkerDomainId::MovementPlanning;
    Wakeup.Key.EntityRef = Profiles[0].EntityRef;
    Wakeup.Key.WakeupId = CrowdWorkerResourceIds::MovementControl;
    Wakeup.AbsoluteSimulationTick =
      Context.AbsoluteSimulationTick + 1;
    Wakeup.Priority = ECrowdWorkerWorkPriority::Normal;
    Wakeup.ReasonMask = 1ull << 11;
    OutOutput.Wakeups.Add(MoveTemp(Wakeup));
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
    FCrowdWorkerMovementControlEntry RevisedProfile;
    const FCrowdWorkerDirtyStateRecord* ProfileRecord =
      Context.EntityStates->Find(
        Work.Key.PrimaryEntity,
        ECrowdWorkerField::MovementProfile);
    const FCrowdWorkerMovementControlEntry* Control = nullptr;
    if (ProfileRecord)
    {
      if (ProfileRecord->SourceInputSequence
          > Context.LastAppliedInputSequence
        || !FCrowdWorkerMovementProfileCodec::Decode(
          ProfileRecord->Payload, RevisedProfile)
        || RevisedProfile.EntityRef != Work.Key.PrimaryEntity)
        return false;
      Control = &RevisedProfile;
    }
    else if (bHasMovementControl)
    {
      Control = MovementControl.Find(Work.Key.PrimaryEntity);
    }
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
          > Context.LastAppliedInputSequence)
        return false;
      // Plans are revision inputs. A clock-only intent advances simulation
      // without republishing unchanged control, so an older valid plan is
      // the authoritative plan for this epoch.
      bHasPlan = true;
    }
    FCrowdWorkerCombatState Combat;
    const FCrowdWorkerDirtyStateRecord* CombatRecord =
      Context.EntityStates->Find(
        Work.Key.PrimaryEntity, ECrowdWorkerField::Combat);
    const bool bHasCombat = CombatRecord != nullptr;
    if (bHasCombat
      && (CombatRecord->SourceInputSequence
            > Context.LastAppliedInputSequence
        || !FCrowdWorkerCombatStateCodec::Decode(
          CombatRecord->Payload, Combat)))
      return false;

    FCrowdWorkerMovementState Movement;
    uint64 PreviousStateRevision = 0;
    uint64 CorrectionRevision = Work.CorrectionRevision;
    const FCrowdWorkerDirtyStateRecord* MovementRecord =
      Context.EntityStates->Find(
        Work.Key.PrimaryEntity, ECrowdWorkerField::Movement);
    const FCrowdWorkerDirtyStateRecord* Current = nullptr;
    if (Context.RuntimeMode == ECrowdWorkerRuntimeV2Mode::Production)
    {
      Current = Context.EntityStates->Find(
        Work.Key.PrimaryEntity, ECrowdWorkerField::Facing);
    }
    if (!Current)
      Current = MovementRecord;
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
    if (MovementRecord && MovementRecord != Current)
    {
      PreviousStateRevision = FMath::Max(
        PreviousStateRevision, MovementRecord->StateRevision);
      CorrectionRevision = FMath::Max(
        CorrectionRevision, MovementRecord->CorrectionRevision);
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
      if (bHasCombat
        && (!Combat.bAlive
          || (Combat.bMovementLocked
            && !Combat.bReactiveActive)))
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

    if (!bHasMovementControl)
    {
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
    }

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
