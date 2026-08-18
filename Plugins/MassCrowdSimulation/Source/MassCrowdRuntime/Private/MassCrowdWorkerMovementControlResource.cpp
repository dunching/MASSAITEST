#include "MassCrowdWorkerMovementControlResource.h"

#include "MassCrowdWorkerRuntimeV2.h"

namespace CrowdWorkerMovementControlPrivate
{
  template<typename T>
  void MovementControlAppendUnsigned(TArray<uint8>& Bytes, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      Bytes.Add(static_cast<uint8>(Value >> (Byte * 8)));
  }

  void MovementControlAppendFloat(TArray<uint8>& Bytes, const float Value)
  {
    uint32 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    MovementControlAppendUnsigned(Bytes, Bits);
  }

  template<typename T>
  bool MovementControlReadUnsigned(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    T& OutValue)
  {
    static_assert(std::is_unsigned_v<T>);
    if (Offset < 0
      || Offset + static_cast<int32>(sizeof(T)) > Bytes.Num())
      return false;
    OutValue = 0;
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      OutValue |= static_cast<T>(Bytes[Offset + Byte])
        << (Byte * 8);
    Offset += sizeof(T);
    return true;
  }

  bool MovementControlReadFloat(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    float& OutValue)
  {
    uint32 Bits = 0;
    if (!MovementControlReadUnsigned(Bytes, Offset, Bits)) return false;
    FMemory::Memcpy(&OutValue, &Bits, sizeof(Bits));
    return FMath::IsFinite(OutValue);
  }
}

using namespace CrowdWorkerMovementControlPrivate;

bool FCrowdWorkerMovementControlEntry::IsValid() const
{
  return EntityRef.IsValid()
    && AgentId != INDEX_NONE
    && PreviousBlockedAgeSteps >= 0
    && FMath::IsFinite(MaximumSpeedCmps)
    && MaximumSpeedCmps >= 0.0f
    && FMath::IsFinite(ParticleEnvironmentHardClearanceCm)
    && ParticleEnvironmentHardClearanceCm >= 0.0f
    && FMath::IsFinite(ParticlePhysicalRadiusCm)
    && ParticlePhysicalRadiusCm >= 0.0f
    && FMath::IsFinite(ParticleHardSafetyGapCm)
    && ParticleHardSafetyGapCm >= 0.0f
    && FMath::IsFinite(ParticleSoftMarginCm)
    && ParticleSoftMarginCm >= 0.0f
    && FMath::IsFinite(ParticleMobility)
    && ParticleMobility >= 0.0f
    && !AutonomousPreferredVelocity.ContainsNaN()
    && !LocalVelocity.ContainsNaN()
    && !BoundaryLocation.ContainsNaN()
    && FMath::IsFinite(ProposedZ)
    && FMath::IsFinite(VerticalVelocityCmps);
}

bool FCrowdWorkerMovementProfileCodec::Encode(
  const FCrowdWorkerMovementControlEntry& Profile,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Profile.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  MovementControlAppendUnsigned(OutPayload.Bytes, Profile.EntityRef.ProviderId);
  MovementControlAppendUnsigned(
    OutPayload.Bytes, Profile.EntityRef.StableEntityId);
  MovementControlAppendUnsigned(
    OutPayload.Bytes, Profile.EntityRef.LifecycleSerial);
  MovementControlAppendUnsigned(
    OutPayload.Bytes, static_cast<uint32>(Profile.AgentId));
  MovementControlAppendUnsigned(OutPayload.Bytes, Profile.InteractionLayer);
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(Profile.PreviousBlockedAgeSteps));
  MovementControlAppendFloat(OutPayload.Bytes, Profile.MaximumSpeedCmps);
  MovementControlAppendFloat(
    OutPayload.Bytes,
    Profile.ParticleEnvironmentHardClearanceCm);
  MovementControlAppendFloat(
    OutPayload.Bytes, Profile.ParticlePhysicalRadiusCm);
  MovementControlAppendFloat(
    OutPayload.Bytes, Profile.ParticleHardSafetyGapCm);
  MovementControlAppendFloat(OutPayload.Bytes, Profile.ParticleSoftMarginCm);
  MovementControlAppendFloat(OutPayload.Bytes, Profile.ParticleMobility);
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint8>(
      (Profile.bFreezeAtBoundaryLocation ? 1u : 0u)
      | (Profile.bVerticalOverride ? 2u : 0u)
      | (Profile.bParticleActive ? 4u : 0u)
      | (Profile.bUseLocalVelocity ? 8u : 0u)
      | (Profile.bLocalVelocityValid ? 16u : 0u)
      | (Profile.bUseWorkerTargetGuidance ? 32u : 0u)
      | (Profile.bUseAuthoritativePreferredVelocity ? 64u : 0u)));
  const auto AppendVector = [&OutPayload](const FVector& Value)
  {
    MovementControlAppendFloat(
      OutPayload.Bytes, static_cast<float>(Value.X));
    MovementControlAppendFloat(
      OutPayload.Bytes, static_cast<float>(Value.Y));
    MovementControlAppendFloat(
      OutPayload.Bytes, static_cast<float>(Value.Z));
  };
  AppendVector(Profile.AutonomousPreferredVelocity);
  AppendVector(Profile.LocalVelocity);
  AppendVector(Profile.BoundaryLocation);
  MovementControlAppendFloat(OutPayload.Bytes, Profile.ProposedZ);
  MovementControlAppendFloat(OutPayload.Bytes, Profile.VerticalVelocityCmps);
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerMovementProfileCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerMovementControlEntry& OutProfile)
{
  OutProfile = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  uint32 AgentId = 0;
  uint32 PreviousBlockedAgeSteps = 0;
  uint8 Flags = 0;
  if (!MovementControlReadUnsigned(
      Payload.Bytes, Offset, OutProfile.EntityRef.ProviderId)
    || !MovementControlReadUnsigned(
      Payload.Bytes, Offset,
      OutProfile.EntityRef.StableEntityId)
    || !MovementControlReadUnsigned(
      Payload.Bytes, Offset,
      OutProfile.EntityRef.LifecycleSerial)
    || !MovementControlReadUnsigned(Payload.Bytes, Offset, AgentId)
    || !MovementControlReadUnsigned(
      Payload.Bytes, Offset, OutProfile.InteractionLayer)
    || !MovementControlReadUnsigned(
      Payload.Bytes, Offset, PreviousBlockedAgeSteps)
    || PreviousBlockedAgeSteps
      > static_cast<uint32>(MAX_int32)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset, OutProfile.MaximumSpeedCmps)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutProfile.ParticleEnvironmentHardClearanceCm)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutProfile.ParticlePhysicalRadiusCm)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutProfile.ParticleHardSafetyGapCm)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset, OutProfile.ParticleSoftMarginCm)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset, OutProfile.ParticleMobility)
    || !MovementControlReadUnsigned(Payload.Bytes, Offset, Flags)
    || (Flags & ~uint8{127}) != 0)
    return false;
  const auto ReadVector = [&Payload, &Offset](FVector& OutValue)
  {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    if (!MovementControlReadFloat(Payload.Bytes, Offset, X)
      || !MovementControlReadFloat(Payload.Bytes, Offset, Y)
      || !MovementControlReadFloat(Payload.Bytes, Offset, Z))
      return false;
    OutValue = FVector(X, Y, Z);
    return true;
  };
  if (!ReadVector(OutProfile.AutonomousPreferredVelocity)
    || !ReadVector(OutProfile.LocalVelocity)
    || !ReadVector(OutProfile.BoundaryLocation)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset, OutProfile.ProposedZ)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset, OutProfile.VerticalVelocityCmps))
    return false;
  OutProfile.AgentId = static_cast<int32>(AgentId);
  OutProfile.PreviousBlockedAgeSteps =
    static_cast<int32>(PreviousBlockedAgeSteps);
  OutProfile.bFreezeAtBoundaryLocation = (Flags & 1u) != 0;
  OutProfile.bVerticalOverride = (Flags & 2u) != 0;
  OutProfile.bParticleActive = (Flags & 4u) != 0;
  OutProfile.bUseLocalVelocity = (Flags & 8u) != 0;
  OutProfile.bLocalVelocityValid = (Flags & 16u) != 0;
  OutProfile.bUseWorkerTargetGuidance = (Flags & 32u) != 0;
  OutProfile.bUseAuthoritativePreferredVelocity =
    (Flags & 64u) != 0;
  return Offset == Payload.Bytes.Num() && OutProfile.IsValid();
}

bool CrowdWorkerResolveMovementProfiles(
  const FCrowdWorkerEntityStateStore& EntityStates,
  const uint64 LastAppliedInputSequence,
  const FCrowdWorkerMovementControlResource& FrozenControl,
  TArray<FCrowdWorkerMovementControlEntry>& OutProfiles)
{
  OutProfiles.Reset();
  TArray<FCrowdStableEntityRef> Entities;
  EntityStates.GetEntities(Entities);
  OutProfiles.Reserve(Entities.Num());
  for (const FCrowdStableEntityRef& EntityRef : Entities)
  {
    FCrowdWorkerMovementControlEntry Profile;
    const FCrowdWorkerDirtyStateRecord* ProfileRecord =
      EntityStates.Find(
        EntityRef, ECrowdWorkerField::MovementProfile);
    if (ProfileRecord)
    {
      if (ProfileRecord->SourceInputSequence
          > LastAppliedInputSequence
        || !FCrowdWorkerMovementProfileCodec::Decode(
          ProfileRecord->Payload, Profile)
        || Profile.EntityRef != EntityRef)
        return false;
    }
    else
    {
      const FCrowdWorkerMovementControlEntry* FrozenProfile =
        FrozenControl.Find(EntityRef);
      if (!FrozenProfile) return false;
      Profile = *FrozenProfile;
    }
    OutProfiles.Add(MoveTemp(Profile));
  }
  OutProfiles.Sort([](
    const FCrowdWorkerMovementControlEntry& A,
    const FCrowdWorkerMovementControlEntry& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  return true;
}

const FCrowdWorkerMovementControlEntry*
FCrowdWorkerMovementControlResource::Find(
  const FCrowdStableEntityRef& EntityRef) const
{
  return Entries.FindByPredicate(
    [&EntityRef](const FCrowdWorkerMovementControlEntry& Entry)
    {
      return Entry.EntityRef == EntityRef;
    });
}

bool FCrowdWorkerMovementControlResource::IsValid() const
{
  const FCrowdLocalPredictiveSettings& Settings =
    LocalPredictiveSettings;
  const FCrowdParticleConstraintSettings& Particle =
    ParticleSettings;
  const FCrowdSharedFlowFieldConfig& Flow = Environment;
  if (Revision == 0 || FixedStepIndex < 0 || PlanRevision < 0
    || Flow.Revision < 0
    || Flow.BoundsMin.ContainsNaN()
    || Flow.BoundsMax.ContainsNaN()
    || Flow.GoalLocation.ContainsNaN()
    || !FMath::IsFinite(Flow.CellSizeCm)
    || !FMath::IsFinite(Flow.AgentInflateCm)
    || Flow.CellSizeCm <= 0.0f
    || Flow.AgentInflateCm < 0.0f
    || !FMath::IsFinite(Settings.FixedStepSeconds)
    || !FMath::IsFinite(Settings.TimeHorizonSeconds)
    || !FMath::IsFinite(Settings.SpatialCellSizeCm)
    || !FMath::IsFinite(Settings.VelocityQuantumCmps)
    || !FMath::IsFinite(Settings.ConstraintEpsilonCmps)
    || !FMath::IsFinite(Settings.RequestedProgressThresholdCmps)
    || !FMath::IsFinite(Settings.BlockedProgressThresholdCmps)
    || !FMath::IsFinite(Settings.GrantedResponsibility)
    || Settings.FixedStepSeconds <= 0.0f
    || Settings.TimeHorizonSeconds <= 0.0f
    || Settings.SpatialCellSizeCm <= 0.0f
    || Settings.VelocityQuantumCmps <= 0.0f
    || Settings.GrantDurationSteps < 0
    || Settings.JointIterationCount <= 0)
    return false;
  if (!FMath::IsFinite(Particle.FixedStepSeconds)
    || !FMath::IsFinite(Particle.SoftResponsePerSecond)
    || !FMath::IsFinite(
      Particle.SoftMaxPairCorrectionPerIterationCm)
    || !FMath::IsFinite(
      Particle.SoftMaxEnvironmentCorrectionPerIterationCm)
    || !FMath::IsFinite(
      Particle.HardMaxPairCorrectionPerIterationCm)
    || !FMath::IsFinite(Particle.PositionQuantumCm)
    || !FMath::IsFinite(Particle.VelocityQuantumCmps)
    || Particle.FixedStepSeconds <= 0.0f
    || Particle.IterationCount <= 0
    || Particle.SafetyIterationCount <= 0
    || Particle.PositionQuantumCm <= 0.0f
    || Particle.VelocityQuantumCmps <= 0.0f)
    return false;
  int32 PreviousObstacleId = MIN_int32;
  for (const FCrowdSharedFlowObstacleSpec& Obstacle :
    Flow.ObstacleSpecs)
  {
    if (Obstacle.ObstacleId <= PreviousObstacleId
      || Obstacle.Center.ContainsNaN()
      || Obstacle.Extent.ContainsNaN())
      return false;
    PreviousObstacleId = Obstacle.ObstacleId;
  }
  uint32 PreviousComponentKey = 0;
  int32 PreviousGrantedAgentId = INDEX_NONE;
  bool bHasPreviousGrant = false;
  for (const FCrowdLocalPredictiveGrantState& Grant :
    PreviousGrantStates)
  {
    if (Grant.ComponentKey == 0
      || Grant.GrantedAgentId == INDEX_NONE
      || Grant.GrantEpoch < 0 || Grant.RemainingSteps < 0
      || (bHasPreviousGrant
        && (Grant.ComponentKey < PreviousComponentKey
          || (Grant.ComponentKey == PreviousComponentKey
            && Grant.GrantedAgentId
              <= PreviousGrantedAgentId))))
      return false;
    PreviousComponentKey = Grant.ComponentKey;
    PreviousGrantedAgentId = Grant.GrantedAgentId;
    bHasPreviousGrant = true;
  }
  int32 PreviousExternalAgentId = 0;
  bool bHasPreviousExternalAgent = false;
  for (const FCrowdParticleConstraintAgent& Agent :
    ExternalParticleAgents)
  {
    if (Agent.AgentId == INDEX_NONE
      || (bHasPreviousExternalAgent
        && Agent.AgentId <= PreviousExternalAgentId)
      || Agent.StartPosition.ContainsNaN()
      || Agent.PredictedPosition.ContainsNaN()
      || !FMath::IsFinite(Agent.PhysicalRadiusCm)
      || !FMath::IsFinite(Agent.HardSafetyGapCm)
      || !FMath::IsFinite(Agent.EnvironmentHardClearanceCm)
      || !FMath::IsFinite(Agent.SoftMarginCm)
      || !FMath::IsFinite(Agent.Mobility)
      || Agent.PhysicalRadiusCm <= 0.0f
      || Agent.HardSafetyGapCm < 0.0f
      || Agent.EnvironmentHardClearanceCm < 0.0f
      || Agent.SoftMarginCm < 0.0f
      || Agent.Mobility < 0.0f)
      return false;
    PreviousExternalAgentId = Agent.AgentId;
    bHasPreviousExternalAgent = true;
  }
  FCrowdStableEntityRef Previous;
  for (const FCrowdWorkerMovementControlEntry& Entry : Entries)
  {
    if (!Entry.IsValid()
      || (!Previous.IsUnset()
        && !(Previous < Entry.EntityRef)))
      return false;
    Previous = Entry.EntityRef;
  }
  return true;
}

bool FCrowdWorkerMovementControlResourceCodec::Encode(
  const FCrowdWorkerMovementControlResource& Resource,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Resource.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  MovementControlAppendUnsigned(OutPayload.Bytes, Resource.Revision);
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(Resource.FixedStepIndex));
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(Resource.PlanRevision));
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint8>(
      (Resource.bRunLocalPredictive ? 1u : 0u)
      | (Resource.bApplyEnvironmentMovementConstraint
        ? 2u : 0u)
      | (Resource.bRunParticleInteraction ? 4u : 0u)));
  const FCrowdSharedFlowFieldConfig& Flow =
    Resource.Environment;
  MovementControlAppendUnsigned(
    OutPayload.Bytes, static_cast<uint32>(Flow.Revision));
  MovementControlAppendFloat(OutPayload.Bytes, static_cast<float>(Flow.BoundsMin.X));
  MovementControlAppendFloat(OutPayload.Bytes, static_cast<float>(Flow.BoundsMin.Y));
  MovementControlAppendFloat(OutPayload.Bytes, static_cast<float>(Flow.BoundsMin.Z));
  MovementControlAppendFloat(OutPayload.Bytes, static_cast<float>(Flow.BoundsMax.X));
  MovementControlAppendFloat(OutPayload.Bytes, static_cast<float>(Flow.BoundsMax.Y));
  MovementControlAppendFloat(OutPayload.Bytes, static_cast<float>(Flow.BoundsMax.Z));
  MovementControlAppendFloat(OutPayload.Bytes, Flow.CellSizeCm);
  MovementControlAppendFloat(OutPayload.Bytes, Flow.AgentInflateCm);
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(Flow.ConnectivityContractVersion));
  MovementControlAppendFloat(
    OutPayload.Bytes, static_cast<float>(Flow.GoalLocation.X));
  MovementControlAppendFloat(
    OutPayload.Bytes, static_cast<float>(Flow.GoalLocation.Y));
  MovementControlAppendFloat(
    OutPayload.Bytes, static_cast<float>(Flow.GoalLocation.Z));
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(Flow.ObstacleSpecs.Num()));
  for (const FCrowdSharedFlowObstacleSpec& Obstacle :
    Flow.ObstacleSpecs)
  {
    MovementControlAppendUnsigned(
      OutPayload.Bytes,
      static_cast<uint32>(Obstacle.ObstacleId));
    MovementControlAppendFloat(
      OutPayload.Bytes, static_cast<float>(Obstacle.Center.X));
    MovementControlAppendFloat(
      OutPayload.Bytes, static_cast<float>(Obstacle.Center.Y));
    MovementControlAppendFloat(
      OutPayload.Bytes, static_cast<float>(Obstacle.Center.Z));
    MovementControlAppendFloat(
      OutPayload.Bytes, static_cast<float>(Obstacle.Extent.X));
    MovementControlAppendFloat(
      OutPayload.Bytes, static_cast<float>(Obstacle.Extent.Y));
    MovementControlAppendFloat(
      OutPayload.Bytes, static_cast<float>(Obstacle.Extent.Z));
  }
  const FCrowdLocalPredictiveSettings& Settings =
    Resource.LocalPredictiveSettings;
  MovementControlAppendFloat(OutPayload.Bytes, Settings.FixedStepSeconds);
  MovementControlAppendFloat(OutPayload.Bytes, Settings.TimeHorizonSeconds);
  MovementControlAppendFloat(OutPayload.Bytes, Settings.SpatialCellSizeCm);
  MovementControlAppendFloat(OutPayload.Bytes, Settings.VelocityQuantumCmps);
  MovementControlAppendFloat(OutPayload.Bytes, Settings.ConstraintEpsilonCmps);
  MovementControlAppendFloat(
    OutPayload.Bytes,
    Settings.RequestedProgressThresholdCmps);
  MovementControlAppendFloat(
    OutPayload.Bytes,
    Settings.BlockedProgressThresholdCmps);
  MovementControlAppendFloat(OutPayload.Bytes, Settings.GrantedResponsibility);
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(Settings.GrantDurationSteps));
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(Settings.JointIterationCount));
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(Resource.PreviousGrantStates.Num()));
  for (const FCrowdLocalPredictiveGrantState& Grant :
    Resource.PreviousGrantStates)
  {
    MovementControlAppendUnsigned(OutPayload.Bytes, Grant.ComponentKey);
    MovementControlAppendUnsigned(
      OutPayload.Bytes,
      static_cast<uint32>(Grant.GrantedAgentId));
    MovementControlAppendUnsigned(
      OutPayload.Bytes,
      static_cast<uint32>(Grant.GrantEpoch));
    MovementControlAppendUnsigned(
      OutPayload.Bytes,
      static_cast<uint32>(Grant.RemainingSteps));
  }
  const FCrowdParticleConstraintSettings& Particle =
    Resource.ParticleSettings;
  MovementControlAppendFloat(OutPayload.Bytes, Particle.FixedStepSeconds);
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(Particle.IterationCount));
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(Particle.SafetyIterationCount));
  MovementControlAppendFloat(OutPayload.Bytes, Particle.SoftResponsePerSecond);
  MovementControlAppendFloat(
    OutPayload.Bytes,
    Particle.SoftMaxPairCorrectionPerIterationCm);
  MovementControlAppendFloat(
    OutPayload.Bytes,
    Particle.SoftMaxEnvironmentCorrectionPerIterationCm);
  MovementControlAppendFloat(
    OutPayload.Bytes,
    Particle.HardMaxPairCorrectionPerIterationCm);
  MovementControlAppendFloat(OutPayload.Bytes, Particle.PositionQuantumCm);
  MovementControlAppendFloat(OutPayload.Bytes, Particle.VelocityQuantumCmps);
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint8>(
      (Resource.bParticleConstrainToFlowBounds ? 1u : 0u)
      | (Particle.bCaptureSafetyStageTrace ? 2u : 0u)
      | (Particle.bCaptureRouteDiagnostic ? 4u : 0u)));
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(
      Resource.ExternalParticleAgents.Num()));
  for (const FCrowdParticleConstraintAgent& Agent :
    Resource.ExternalParticleAgents)
  {
    MovementControlAppendUnsigned(
      OutPayload.Bytes, static_cast<uint32>(Agent.AgentId));
    MovementControlAppendUnsigned(OutPayload.Bytes, Agent.InteractionLayer);
    MovementControlAppendFloat(
      OutPayload.Bytes, static_cast<float>(Agent.StartPosition.X));
    MovementControlAppendFloat(
      OutPayload.Bytes, static_cast<float>(Agent.StartPosition.Y));
    MovementControlAppendFloat(
      OutPayload.Bytes, static_cast<float>(Agent.StartPosition.Z));
    MovementControlAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(Agent.PredictedPosition.X));
    MovementControlAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(Agent.PredictedPosition.Y));
    MovementControlAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(Agent.PredictedPosition.Z));
    MovementControlAppendFloat(OutPayload.Bytes, Agent.PhysicalRadiusCm);
    MovementControlAppendFloat(OutPayload.Bytes, Agent.HardSafetyGapCm);
    MovementControlAppendFloat(
      OutPayload.Bytes, Agent.EnvironmentHardClearanceCm);
    MovementControlAppendFloat(OutPayload.Bytes, Agent.SoftMarginCm);
    MovementControlAppendFloat(OutPayload.Bytes, Agent.Mobility);
  }
  MovementControlAppendUnsigned(
    OutPayload.Bytes,
    static_cast<uint32>(Resource.Entries.Num()));
  for (const FCrowdWorkerMovementControlEntry& Entry :
    Resource.Entries)
  {
    MovementControlAppendUnsigned(OutPayload.Bytes, Entry.EntityRef.ProviderId);
    MovementControlAppendUnsigned(
      OutPayload.Bytes, Entry.EntityRef.StableEntityId);
    MovementControlAppendUnsigned(
      OutPayload.Bytes, Entry.EntityRef.LifecycleSerial);
    MovementControlAppendUnsigned(
      OutPayload.Bytes, static_cast<uint32>(Entry.AgentId));
    MovementControlAppendUnsigned(OutPayload.Bytes, Entry.InteractionLayer);
    MovementControlAppendUnsigned(
      OutPayload.Bytes,
      static_cast<uint32>(Entry.PreviousBlockedAgeSteps));
    MovementControlAppendFloat(OutPayload.Bytes, Entry.MaximumSpeedCmps);
    MovementControlAppendFloat(
      OutPayload.Bytes,
      Entry.ParticleEnvironmentHardClearanceCm);
    MovementControlAppendFloat(
      OutPayload.Bytes, Entry.ParticlePhysicalRadiusCm);
    MovementControlAppendFloat(
      OutPayload.Bytes, Entry.ParticleHardSafetyGapCm);
    MovementControlAppendFloat(
      OutPayload.Bytes, Entry.ParticleSoftMarginCm);
    MovementControlAppendFloat(
      OutPayload.Bytes, Entry.ParticleMobility);
    MovementControlAppendUnsigned(
      OutPayload.Bytes,
      static_cast<uint8>(
        (Entry.bFreezeAtBoundaryLocation ? 1u : 0u)
        | (Entry.bVerticalOverride ? 2u : 0u)
        | (Entry.bParticleActive ? 4u : 0u)
        | (Entry.bUseLocalVelocity ? 8u : 0u)
        | (Entry.bLocalVelocityValid ? 16u : 0u)
        | (Entry.bUseWorkerTargetGuidance ? 32u : 0u)
        | (Entry.bUseAuthoritativePreferredVelocity ? 64u : 0u)));
    MovementControlAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(Entry.AutonomousPreferredVelocity.X));
    MovementControlAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(Entry.AutonomousPreferredVelocity.Y));
    MovementControlAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(Entry.AutonomousPreferredVelocity.Z));
    MovementControlAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(Entry.LocalVelocity.X));
    MovementControlAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(Entry.LocalVelocity.Y));
    MovementControlAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(Entry.LocalVelocity.Z));
    MovementControlAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(Entry.BoundaryLocation.X));
    MovementControlAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(Entry.BoundaryLocation.Y));
    MovementControlAppendFloat(
      OutPayload.Bytes,
      static_cast<float>(Entry.BoundaryLocation.Z));
    MovementControlAppendFloat(OutPayload.Bytes, Entry.ProposedZ);
    MovementControlAppendFloat(
      OutPayload.Bytes, Entry.VerticalVelocityCmps);
  }
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerMovementControlResourceCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerMovementControlResource& OutResource)
{
  OutResource = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  uint32 Count = 0;
  uint32 FixedStepIndex = 0;
  uint32 PlanRevision = 0;
  uint8 ResourceFlags = 0;
  uint32 FlowRevision = 0;
  uint32 ConnectivityVersion = 0;
  uint32 ObstacleCount = 0;
  uint32 GrantDurationSteps = 0;
  uint32 JointIterationCount = 0;
  uint32 GrantCount = 0;
  uint32 ParticleIterationCount = 0;
  uint32 ParticleSafetyIterationCount = 0;
  uint8 ParticleFlags = 0;
  uint32 ExternalParticleCount = 0;
  if (!MovementControlReadUnsigned(
      Payload.Bytes, Offset, OutResource.Revision)
    || !MovementControlReadUnsigned(Payload.Bytes, Offset, FixedStepIndex)
    || !MovementControlReadUnsigned(Payload.Bytes, Offset, PlanRevision)
    || !MovementControlReadUnsigned(Payload.Bytes, Offset, ResourceFlags)
    || (ResourceFlags & ~uint8{7}) != 0
    || !MovementControlReadUnsigned(Payload.Bytes, Offset, FlowRevision))
    return false;
  float FlowValues[8] = {};
  for (float& Value : FlowValues)
    if (!MovementControlReadFloat(Payload.Bytes, Offset, Value))
      return false;
  float GoalValues[3] = {};
  if (!MovementControlReadUnsigned(
      Payload.Bytes, Offset, ConnectivityVersion))
    return false;
  for (float& Value : GoalValues)
    if (!MovementControlReadFloat(Payload.Bytes, Offset, Value))
      return false;
  if (!MovementControlReadUnsigned(Payload.Bytes, Offset, ObstacleCount)
    || ObstacleCount > 100000)
    return false;
  OutResource.Environment.Revision =
    static_cast<int32>(FlowRevision);
  OutResource.Environment.BoundsMin =
    FVector(FlowValues[0], FlowValues[1], FlowValues[2]);
  OutResource.Environment.BoundsMax =
    FVector(FlowValues[3], FlowValues[4], FlowValues[5]);
  OutResource.Environment.CellSizeCm = FlowValues[6];
  OutResource.Environment.AgentInflateCm = FlowValues[7];
  OutResource.Environment.ConnectivityContractVersion =
    static_cast<int32>(ConnectivityVersion);
  OutResource.Environment.GoalLocation =
    FVector(GoalValues[0], GoalValues[1], GoalValues[2]);
  OutResource.Environment.ObstacleSpecs.Reserve(ObstacleCount);
  for (uint32 ObstacleIndex = 0;
    ObstacleIndex < ObstacleCount; ++ObstacleIndex)
  {
    FCrowdSharedFlowObstacleSpec& Obstacle =
      OutResource.Environment.ObstacleSpecs.AddDefaulted_GetRef();
    uint32 ObstacleId = 0;
    float Values[6] = {};
    if (!MovementControlReadUnsigned(Payload.Bytes, Offset, ObstacleId))
      return false;
    for (float& Value : Values)
      if (!MovementControlReadFloat(Payload.Bytes, Offset, Value))
        return false;
    Obstacle.ObstacleId = static_cast<int32>(ObstacleId);
    Obstacle.Center = FVector(Values[0], Values[1], Values[2]);
    Obstacle.Extent = FVector(Values[3], Values[4], Values[5]);
  }
  if (!MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.LocalPredictiveSettings.FixedStepSeconds)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.LocalPredictiveSettings.TimeHorizonSeconds)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.LocalPredictiveSettings.SpatialCellSizeCm)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.LocalPredictiveSettings.VelocityQuantumCmps)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.LocalPredictiveSettings.ConstraintEpsilonCmps)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.LocalPredictiveSettings.
        RequestedProgressThresholdCmps)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.LocalPredictiveSettings.
        BlockedProgressThresholdCmps)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.LocalPredictiveSettings.GrantedResponsibility)
    || !MovementControlReadUnsigned(
      Payload.Bytes, Offset, GrantDurationSteps)
    || !MovementControlReadUnsigned(
      Payload.Bytes, Offset, JointIterationCount)
    || !MovementControlReadUnsigned(Payload.Bytes, Offset, GrantCount)
    || GrantCount > 100000)
    return false;
  OutResource.FixedStepIndex =
    static_cast<int32>(FixedStepIndex);
  OutResource.PlanRevision =
    static_cast<int32>(PlanRevision);
  OutResource.bRunLocalPredictive =
    (ResourceFlags & 1u) != 0;
  OutResource.bApplyEnvironmentMovementConstraint =
    (ResourceFlags & 2u) != 0;
  OutResource.bRunParticleInteraction =
    (ResourceFlags & 4u) != 0;
  OutResource.LocalPredictiveSettings.GrantDurationSteps =
    static_cast<int32>(GrantDurationSteps);
  OutResource.LocalPredictiveSettings.JointIterationCount =
    static_cast<int32>(JointIterationCount);
  OutResource.PreviousGrantStates.Reserve(GrantCount);
  for (uint32 GrantIndex = 0;
    GrantIndex < GrantCount; ++GrantIndex)
  {
    FCrowdLocalPredictiveGrantState& Grant =
      OutResource.PreviousGrantStates.AddDefaulted_GetRef();
    uint32 GrantedAgentId = 0;
    uint32 GrantEpoch = 0;
    uint32 RemainingSteps = 0;
    if (!MovementControlReadUnsigned(
        Payload.Bytes, Offset, Grant.ComponentKey)
      || !MovementControlReadUnsigned(
        Payload.Bytes, Offset, GrantedAgentId)
      || !MovementControlReadUnsigned(
        Payload.Bytes, Offset, GrantEpoch)
      || !MovementControlReadUnsigned(
        Payload.Bytes, Offset, RemainingSteps))
      return false;
    Grant.GrantedAgentId =
      static_cast<int32>(GrantedAgentId);
    Grant.GrantEpoch = static_cast<int32>(GrantEpoch);
    Grant.RemainingSteps =
      static_cast<int32>(RemainingSteps);
  }
  if (!MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.ParticleSettings.FixedStepSeconds)
    || !MovementControlReadUnsigned(
      Payload.Bytes, Offset, ParticleIterationCount)
    || !MovementControlReadUnsigned(
      Payload.Bytes, Offset, ParticleSafetyIterationCount)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.ParticleSettings.SoftResponsePerSecond)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.ParticleSettings.
        SoftMaxPairCorrectionPerIterationCm)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.ParticleSettings.
        SoftMaxEnvironmentCorrectionPerIterationCm)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.ParticleSettings.
        HardMaxPairCorrectionPerIterationCm)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.ParticleSettings.PositionQuantumCm)
    || !MovementControlReadFloat(
      Payload.Bytes, Offset,
      OutResource.ParticleSettings.VelocityQuantumCmps)
    || !MovementControlReadUnsigned(Payload.Bytes, Offset, ParticleFlags)
    || (ParticleFlags & ~uint8{7}) != 0
    || !MovementControlReadUnsigned(
      Payload.Bytes, Offset, ExternalParticleCount)
    || ExternalParticleCount > 100000)
    return false;
  OutResource.ParticleSettings.IterationCount =
    static_cast<int32>(ParticleIterationCount);
  OutResource.ParticleSettings.SafetyIterationCount =
    static_cast<int32>(ParticleSafetyIterationCount);
  OutResource.bParticleConstrainToFlowBounds =
    (ParticleFlags & 1u) != 0;
  OutResource.ParticleSettings.bCaptureSafetyStageTrace =
    (ParticleFlags & 2u) != 0;
  OutResource.ParticleSettings.bCaptureRouteDiagnostic =
    (ParticleFlags & 4u) != 0;
  OutResource.ExternalParticleAgents.Reserve(
    ExternalParticleCount);
  for (uint32 ExternalIndex = 0;
    ExternalIndex < ExternalParticleCount; ++ExternalIndex)
  {
    FCrowdParticleConstraintAgent& Agent =
      OutResource.ExternalParticleAgents.AddDefaulted_GetRef();
    uint32 AgentId = 0;
    float Values[11] = {};
    if (!MovementControlReadUnsigned(Payload.Bytes, Offset, AgentId)
      || !MovementControlReadUnsigned(
        Payload.Bytes, Offset, Agent.InteractionLayer))
      return false;
    for (float& Value : Values)
      if (!MovementControlReadFloat(Payload.Bytes, Offset, Value))
        return false;
    Agent.AgentId = static_cast<int32>(AgentId);
    Agent.StartPosition =
      FVector(Values[0], Values[1], Values[2]);
    Agent.PredictedPosition =
      FVector(Values[3], Values[4], Values[5]);
    Agent.PhysicalRadiusCm = Values[6];
    Agent.HardSafetyGapCm = Values[7];
    Agent.EnvironmentHardClearanceCm = Values[8];
    Agent.SoftMarginCm = Values[9];
    Agent.Mobility = Values[10];
  }
  if (!MovementControlReadUnsigned(Payload.Bytes, Offset, Count)
    || Count > 100000)
    return false;
  OutResource.Entries.Reserve(Count);
  for (uint32 Index = 0; Index < Count; ++Index)
  {
    FCrowdWorkerMovementControlEntry& Entry =
      OutResource.Entries.AddDefaulted_GetRef();
    uint8 Flags = 0;
    uint32 AgentId = 0;
    uint32 PreviousBlockedAgeSteps = 0;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float PreferredX = 0.0f;
    float PreferredY = 0.0f;
    float PreferredZ = 0.0f;
    float LocalX = 0.0f;
    float LocalY = 0.0f;
    float LocalZ = 0.0f;
    if (!MovementControlReadUnsigned(
        Payload.Bytes, Offset, Entry.EntityRef.ProviderId)
      || !MovementControlReadUnsigned(
        Payload.Bytes, Offset, Entry.EntityRef.StableEntityId)
      || !MovementControlReadUnsigned(
        Payload.Bytes, Offset, Entry.EntityRef.LifecycleSerial)
      || !MovementControlReadUnsigned(Payload.Bytes, Offset, AgentId)
      || !MovementControlReadUnsigned(
        Payload.Bytes, Offset, Entry.InteractionLayer)
      || !MovementControlReadUnsigned(
        Payload.Bytes, Offset, PreviousBlockedAgeSteps)
      || !MovementControlReadFloat(
        Payload.Bytes, Offset, Entry.MaximumSpeedCmps)
      || !MovementControlReadFloat(
        Payload.Bytes, Offset,
        Entry.ParticleEnvironmentHardClearanceCm)
      || !MovementControlReadFloat(
        Payload.Bytes, Offset,
        Entry.ParticlePhysicalRadiusCm)
      || !MovementControlReadFloat(
        Payload.Bytes, Offset,
        Entry.ParticleHardSafetyGapCm)
      || !MovementControlReadFloat(
        Payload.Bytes, Offset,
        Entry.ParticleSoftMarginCm)
      || !MovementControlReadFloat(
        Payload.Bytes, Offset,
        Entry.ParticleMobility)
      || !MovementControlReadUnsigned(Payload.Bytes, Offset, Flags)
      || (Flags & ~uint8{127}) != 0
      || !MovementControlReadFloat(Payload.Bytes, Offset, PreferredX)
      || !MovementControlReadFloat(Payload.Bytes, Offset, PreferredY)
      || !MovementControlReadFloat(Payload.Bytes, Offset, PreferredZ)
      || !MovementControlReadFloat(Payload.Bytes, Offset, LocalX)
      || !MovementControlReadFloat(Payload.Bytes, Offset, LocalY)
      || !MovementControlReadFloat(Payload.Bytes, Offset, LocalZ)
      || !MovementControlReadFloat(Payload.Bytes, Offset, X)
      || !MovementControlReadFloat(Payload.Bytes, Offset, Y)
      || !MovementControlReadFloat(Payload.Bytes, Offset, Z)
      || !MovementControlReadFloat(Payload.Bytes, Offset, Entry.ProposedZ)
      || !MovementControlReadFloat(
        Payload.Bytes, Offset, Entry.VerticalVelocityCmps))
      return false;
    Entry.AgentId = static_cast<int32>(AgentId);
    Entry.PreviousBlockedAgeSteps =
      static_cast<int32>(PreviousBlockedAgeSteps);
    Entry.bFreezeAtBoundaryLocation = (Flags & 1u) != 0;
    Entry.bVerticalOverride = (Flags & 2u) != 0;
    Entry.bParticleActive = (Flags & 4u) != 0;
    Entry.bUseLocalVelocity = (Flags & 8u) != 0;
    Entry.bLocalVelocityValid = (Flags & 16u) != 0;
    Entry.bUseWorkerTargetGuidance = (Flags & 32u) != 0;
    Entry.bUseAuthoritativePreferredVelocity =
      (Flags & 64u) != 0;
    Entry.AutonomousPreferredVelocity =
      FVector(PreferredX, PreferredY, PreferredZ);
    Entry.LocalVelocity = FVector(LocalX, LocalY, LocalZ);
    Entry.BoundaryLocation = FVector(X, Y, Z);
  }
  return Offset == Payload.Bytes.Num()
    && OutResource.IsValid();
}
