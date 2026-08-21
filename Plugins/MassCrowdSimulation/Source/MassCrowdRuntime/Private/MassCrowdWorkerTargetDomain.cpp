#include "MassCrowdWorkerTargetDomain.h"

#include "MassCrowdWorkerFlowResource.h"
#include "MassCrowdWorkerMovementAuthority.h"
#include "MassCrowdWorkerNavigationResource.h"
#include "Misc/ScopeLock.h"

namespace CrowdWorkerTargetPrivate
{
  constexpr uint32 MaxArrayCount = 100000;

  bool IsTopologyCompatible(
    const FCrowdSharedFlowFieldConfig& A,
    const FCrowdSharedFlowFieldConfig& B)
  {
    if (A.BoundsMin != B.BoundsMin
      || A.BoundsMax != B.BoundsMax
      || A.CellSizeCm != B.CellSizeCm
      || A.AgentInflateCm != B.AgentInflateCm
      || A.ConnectivityContractVersion
        != B.ConnectivityContractVersion
      || A.ObstacleSpecs.Num() != B.ObstacleSpecs.Num())
      return false;
    for (int32 Index = 0; Index < A.ObstacleSpecs.Num(); ++Index)
    {
      const FCrowdSharedFlowObstacleSpec& Left =
        A.ObstacleSpecs[Index];
      const FCrowdSharedFlowObstacleSpec& Right =
        B.ObstacleSpecs[Index];
      if (Left.ObstacleId != Right.ObstacleId
        || Left.Center != Right.Center
        || Left.Extent != Right.Extent)
        return false;
    }
    return true;
  }

  template<typename T>
  void AppendUnsigned(TArray<uint8>& Bytes, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      Bytes.Add(static_cast<uint8>(Value >> (Byte * 8)));
  }

  void AppendSigned(TArray<uint8>& Bytes, const int32 Value)
  {
    AppendUnsigned(Bytes, static_cast<uint32>(Value));
  }

  void AppendSigned64(TArray<uint8>& Bytes, const int64 Value)
  {
    AppendUnsigned(Bytes, static_cast<uint64>(Value));
  }

  void AppendFloat(TArray<uint8>& Bytes, const float Value)
  {
    uint32 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    AppendUnsigned(Bytes, Bits);
  }

  void AppendVector2(TArray<uint8>& Bytes, const FVector2f& Value)
  {
    AppendFloat(Bytes, Value.X);
    AppendFloat(Bytes, Value.Y);
  }

  void AppendVector(TArray<uint8>& Bytes, const FVector& Value)
  {
    AppendFloat(Bytes, static_cast<float>(Value.X));
    AppendFloat(Bytes, static_cast<float>(Value.Y));
    AppendFloat(Bytes, static_cast<float>(Value.Z));
  }

  void AppendRef(
    TArray<uint8>& Bytes,
    const FCrowdStableEntityRef& Ref)
  {
    AppendUnsigned(Bytes, Ref.ProviderId);
    AppendUnsigned(Bytes, Ref.StableEntityId);
    AppendUnsigned(Bytes, Ref.LifecycleSerial);
  }

  template<typename T>
  bool ReadUnsigned(
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

  bool ReadSigned(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    int32& OutValue)
  {
    uint32 Value = 0;
    if (!ReadUnsigned(Bytes, Offset, Value)) return false;
    OutValue = static_cast<int32>(Value);
    return true;
  }

  bool ReadSigned64(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    int64& OutValue)
  {
    uint64 Value = 0;
    if (!ReadUnsigned(Bytes, Offset, Value)) return false;
    OutValue = static_cast<int64>(Value);
    return true;
  }

  bool ReadFloat(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    float& OutValue)
  {
    uint32 Bits = 0;
    if (!ReadUnsigned(Bytes, Offset, Bits)) return false;
    FMemory::Memcpy(&OutValue, &Bits, sizeof(Bits));
    return FMath::IsFinite(OutValue);
  }

  bool ReadVector2(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FVector2f& OutValue)
  {
    return ReadFloat(Bytes, Offset, OutValue.X)
      && ReadFloat(Bytes, Offset, OutValue.Y);
  }

  bool ReadVector(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FVector& OutValue)
  {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    if (!ReadFloat(Bytes, Offset, X)
      || !ReadFloat(Bytes, Offset, Y)
      || !ReadFloat(Bytes, Offset, Z))
      return false;
    OutValue = FVector(X, Y, Z);
    return true;
  }

  bool ReadRef(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdStableEntityRef& OutRef)
  {
    return ReadUnsigned(Bytes, Offset, OutRef.ProviderId)
      && ReadUnsigned(Bytes, Offset, OutRef.StableEntityId)
      && ReadUnsigned(Bytes, Offset, OutRef.LifecycleSerial);
  }

  bool ReadCount(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    uint32& OutCount,
    const bool bAllowEmpty = true)
  {
    return ReadUnsigned(Bytes, Offset, OutCount)
      && OutCount <= MaxArrayCount
      && (bAllowEmpty || OutCount > 0);
  }

  void AppendSettings(
    TArray<uint8>& Bytes,
    const FCrowdTargetRegionTransportSettings& Value)
  {
    AppendVector2(Bytes, Value.TargetLocation);
    AppendVector2(Bytes, Value.TargetVelocity);
    AppendFloat(Bytes, Value.TargetPhysicalRadiusCm);
    AppendFloat(Bytes, Value.TargetHardSafetyGapCm);
    AppendFloat(Bytes, Value.PhysicalRadiusCm);
    AppendFloat(Bytes, Value.HardSafetyGapCm);
    AppendFloat(Bytes, Value.SoftMarginCm);
    AppendFloat(Bytes, Value.MinimumCenterDistanceCm);
    AppendFloat(Bytes, Value.MaximumCenterDistanceCm);
    AppendFloat(Bytes, Value.InfluenceBlendWidthCm);
    AppendFloat(Bytes, Value.RadialBandWidthCm);
    AppendFloat(Bytes, Value.TransportSpeedCmps);
    AppendFloat(Bytes, Value.RadialGainPerSecond);
    AppendSigned(Bytes, Value.DemandRegionCount);
    AppendSigned(Bytes, Value.DemandRegionPhaseOffset);
    AppendSigned(Bytes, Value.PlanLifetimeSteps);
    AppendFloat(Bytes, Value.PositionQuantumCm);
    AppendFloat(Bytes, Value.VelocityQuantumCmps);
    AppendUnsigned(
      Bytes, static_cast<uint8>(Value.DistanceResponsePolicy));
    AppendFloat(
      Bytes, Value.AcquireThenHoldReleaseHysteresisCm);
  }

  bool ReadSettings(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdTargetRegionTransportSettings& OutValue)
  {
    uint8 Policy = 0;
    if (!ReadVector2(Bytes, Offset, OutValue.TargetLocation)
      || !ReadVector2(Bytes, Offset, OutValue.TargetVelocity)
      || !ReadFloat(
        Bytes, Offset, OutValue.TargetPhysicalRadiusCm)
      || !ReadFloat(
        Bytes, Offset, OutValue.TargetHardSafetyGapCm)
      || !ReadFloat(Bytes, Offset, OutValue.PhysicalRadiusCm)
      || !ReadFloat(Bytes, Offset, OutValue.HardSafetyGapCm)
      || !ReadFloat(Bytes, Offset, OutValue.SoftMarginCm)
      || !ReadFloat(
        Bytes, Offset, OutValue.MinimumCenterDistanceCm)
      || !ReadFloat(
        Bytes, Offset, OutValue.MaximumCenterDistanceCm)
      || !ReadFloat(
        Bytes, Offset, OutValue.InfluenceBlendWidthCm)
      || !ReadFloat(Bytes, Offset, OutValue.RadialBandWidthCm)
      || !ReadFloat(Bytes, Offset, OutValue.TransportSpeedCmps)
      || !ReadFloat(Bytes, Offset, OutValue.RadialGainPerSecond)
      || !ReadSigned(Bytes, Offset, OutValue.DemandRegionCount)
      || !ReadSigned(
        Bytes, Offset, OutValue.DemandRegionPhaseOffset)
      || !ReadSigned(Bytes, Offset, OutValue.PlanLifetimeSteps)
      || !ReadFloat(Bytes, Offset, OutValue.PositionQuantumCm)
      || !ReadFloat(Bytes, Offset, OutValue.VelocityQuantumCmps)
      || !ReadUnsigned(Bytes, Offset, Policy)
      || Policy > static_cast<uint8>(
        ECrowdTargetDistanceResponsePolicy::AcquireThenHold)
      || !ReadFloat(
        Bytes, Offset,
        OutValue.AcquireThenHoldReleaseHysteresisCm))
      return false;
    OutValue.DistanceResponsePolicy =
      static_cast<ECrowdTargetDistanceResponsePolicy>(Policy);
    return true;
  }

  void AppendFlow(
    TArray<uint8>& Bytes,
    const FCrowdSharedFlowFieldConfig& Value)
  {
    AppendSigned(Bytes, Value.Revision);
    AppendVector(Bytes, Value.BoundsMin);
    AppendVector(Bytes, Value.BoundsMax);
    AppendFloat(Bytes, Value.CellSizeCm);
    AppendFloat(Bytes, Value.AgentInflateCm);
    AppendSigned(Bytes, Value.ConnectivityContractVersion);
    AppendVector(Bytes, Value.GoalLocation);
    AppendUnsigned(
      Bytes, static_cast<uint32>(Value.ObstacleSpecs.Num()));
    for (const FCrowdSharedFlowObstacleSpec& Obstacle :
      Value.ObstacleSpecs)
    {
      AppendSigned(Bytes, Obstacle.ObstacleId);
      AppendVector(Bytes, Obstacle.Center);
      AppendVector(Bytes, Obstacle.Extent);
    }
  }

  bool ReadFlow(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdSharedFlowFieldConfig& OutValue)
  {
    uint32 Count = 0;
    if (!ReadSigned(Bytes, Offset, OutValue.Revision)
      || !ReadVector(Bytes, Offset, OutValue.BoundsMin)
      || !ReadVector(Bytes, Offset, OutValue.BoundsMax)
      || !ReadFloat(Bytes, Offset, OutValue.CellSizeCm)
      || !ReadFloat(Bytes, Offset, OutValue.AgentInflateCm)
      || !ReadSigned(
        Bytes, Offset, OutValue.ConnectivityContractVersion)
      || !ReadVector(Bytes, Offset, OutValue.GoalLocation)
      || !ReadCount(Bytes, Offset, Count))
      return false;
    OutValue.ObstacleSpecs.SetNum(Count);
    for (FCrowdSharedFlowObstacleSpec& Obstacle :
      OutValue.ObstacleSpecs)
    {
      if (!ReadSigned(Bytes, Offset, Obstacle.ObstacleId)
        || !ReadVector(Bytes, Offset, Obstacle.Center)
        || !ReadVector(Bytes, Offset, Obstacle.Extent))
        return false;
    }
    return true;
  }

  void AppendAgent(
    TArray<uint8>& Bytes,
    const FCrowdTargetRegionTransportAgent& Value)
  {
    AppendSigned(Bytes, Value.AgentId);
    AppendVector2(Bytes, Value.Location);
    AppendVector2(Bytes, Value.Velocity);
    AppendVector2(Bytes, Value.FarFlowPreferredVelocity);
    AppendFloat(Bytes, Value.MaxSpeedCmps);
    AppendFloat(Bytes, Value.PhysicalRadiusCm);
    AppendFloat(Bytes, Value.HardSafetyGapCm);
    AppendFloat(Bytes, Value.SoftMarginCm);
    AppendUnsigned(Bytes, static_cast<uint8>(
      Value.bEngagedHold ? 1 : 0));
  }

  bool ReadAgent(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdTargetRegionTransportAgent& OutValue)
  {
    uint8 Engaged = 0;
    return ReadSigned(Bytes, Offset, OutValue.AgentId)
      && ReadVector2(Bytes, Offset, OutValue.Location)
      && ReadVector2(Bytes, Offset, OutValue.Velocity)
      && ReadVector2(
        Bytes, Offset, OutValue.FarFlowPreferredVelocity)
      && ReadFloat(Bytes, Offset, OutValue.MaxSpeedCmps)
      && ReadFloat(Bytes, Offset, OutValue.PhysicalRadiusCm)
      && ReadFloat(Bytes, Offset, OutValue.HardSafetyGapCm)
      && ReadFloat(Bytes, Offset, OutValue.SoftMarginCm)
      && ReadUnsigned(Bytes, Offset, Engaged)
      && Engaged <= 1
      && ((OutValue.bEngagedHold = Engaged != 0), true);
  }

  void AppendPlan(
    TArray<uint8>& Bytes,
    const FCrowdTargetRegionFlowPlan& Value)
  {
    AppendSigned(Bytes, Value.PlanEpoch);
    AppendSigned(Bytes, Value.BuildFixedStepIndex);
    AppendSigned(Bytes, Value.TargetRevision);
    AppendUnsigned(Bytes, Value.FeasibleGraphHash);
    AppendUnsigned(Bytes, Value.EnvironmentHash);
    AppendUnsigned(Bytes, Value.MembershipHash);
    AppendUnsigned(Bytes, Value.ExternalPopulationHash);
    AppendUnsigned(
      Bytes, static_cast<uint32>(Value.EdgeFlows.Num()));
    for (const FCrowdTargetPolarEdgeFlow& Edge : Value.EdgeFlows)
    {
      AppendSigned(Bytes, Edge.FromCellKey);
      AppendSigned(Bytes, Edge.ToCellKey);
      AppendSigned(Bytes, Edge.AgentQuota);
      AppendSigned(Bytes, Edge.ReusedQuota);
    }
    AppendSigned(Bytes, Value.RoutedAgentCount);
    AppendSigned(Bytes, Value.UnroutedAgentCount);
    AppendSigned(Bytes, Value.TotalFeasibleCapacity);
    AppendSigned(Bytes, Value.AssignablePopulation);
    AppendSigned(Bytes, Value.OverflowPopulation);
    AppendSigned64(Bytes, Value.TotalPhysicalCost);
    AppendSigned64(Bytes, Value.ChangedQuotaUnitCount);
    AppendUnsigned(Bytes, Value.TransportHash);
    AppendUnsigned(
      Bytes, static_cast<uint8>(Value.bValid ? 1 : 0));
  }

  bool ReadPlan(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdTargetRegionFlowPlan& OutValue)
  {
    uint32 Count = 0;
    uint8 Valid = 0;
    if (!ReadSigned(Bytes, Offset, OutValue.PlanEpoch)
      || !ReadSigned(
        Bytes, Offset, OutValue.BuildFixedStepIndex)
      || !ReadSigned(Bytes, Offset, OutValue.TargetRevision)
      || !ReadUnsigned(
        Bytes, Offset, OutValue.FeasibleGraphHash)
      || !ReadUnsigned(Bytes, Offset, OutValue.EnvironmentHash)
      || !ReadUnsigned(Bytes, Offset, OutValue.MembershipHash)
      || !ReadUnsigned(
        Bytes, Offset, OutValue.ExternalPopulationHash)
      || !ReadCount(Bytes, Offset, Count))
      return false;
    OutValue.EdgeFlows.SetNum(Count);
    for (FCrowdTargetPolarEdgeFlow& Edge : OutValue.EdgeFlows)
    {
      if (!ReadSigned(Bytes, Offset, Edge.FromCellKey)
        || !ReadSigned(Bytes, Offset, Edge.ToCellKey)
        || !ReadSigned(Bytes, Offset, Edge.AgentQuota)
        || !ReadSigned(Bytes, Offset, Edge.ReusedQuota))
        return false;
    }
    return ReadSigned(Bytes, Offset, OutValue.RoutedAgentCount)
      && ReadSigned(Bytes, Offset, OutValue.UnroutedAgentCount)
      && ReadSigned(Bytes, Offset, OutValue.TotalFeasibleCapacity)
      && ReadSigned(Bytes, Offset, OutValue.AssignablePopulation)
      && ReadSigned(Bytes, Offset, OutValue.OverflowPopulation)
      && ReadSigned64(Bytes, Offset, OutValue.TotalPhysicalCost)
      && ReadSigned64(
        Bytes, Offset, OutValue.ChangedQuotaUnitCount)
      && ReadUnsigned(Bytes, Offset, OutValue.TransportHash)
      && ReadUnsigned(Bytes, Offset, Valid)
      && Valid <= 1
      && ((OutValue.bValid = Valid != 0), true);
  }

  void AppendExecution(
    TArray<uint8>& Bytes,
    const FCrowdTargetRegionQuotaExecutionState& Value)
  {
    AppendSigned(Bytes, Value.PlanEpoch);
    AppendUnsigned(Bytes, Value.PlanTransportHash);
    AppendUnsigned(
      Bytes, static_cast<uint32>(Value.Edges.Num()));
    for (const FCrowdTargetRegionQuotaEdgeState& Edge :
      Value.Edges)
    {
      AppendSigned(Bytes, Edge.FromCellKey);
      AppendSigned(Bytes, Edge.ToCellKey);
      AppendSigned(Bytes, Edge.InitialQuota);
      AppendSigned(Bytes, Edge.ConsumedQuota);
    }
    AppendUnsigned(
      Bytes, static_cast<uint32>(Value.ActiveClaims.Num()));
    for (const FCrowdTargetRegionQuotaAgentClaim& Claim :
      Value.ActiveClaims)
    {
      AppendSigned(Bytes, Claim.AgentId);
      AppendSigned(Bytes, Claim.FromCellKey);
      AppendSigned(Bytes, Claim.ToCellKey);
    }
    AppendSigned(Bytes, Value.CompletedTransitionCount);
    AppendUnsigned(Bytes, Value.ExecutionHash);
    AppendUnsigned(
      Bytes, static_cast<uint8>(Value.bValid ? 1 : 0));
  }

  bool ReadExecution(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdTargetRegionQuotaExecutionState& OutValue)
  {
    uint32 EdgeCount = 0;
    uint32 ClaimCount = 0;
    uint8 Valid = 0;
    if (!ReadSigned(Bytes, Offset, OutValue.PlanEpoch)
      || !ReadUnsigned(
        Bytes, Offset, OutValue.PlanTransportHash)
      || !ReadCount(Bytes, Offset, EdgeCount))
      return false;
    OutValue.Edges.SetNum(EdgeCount);
    for (FCrowdTargetRegionQuotaEdgeState& Edge : OutValue.Edges)
    {
      if (!ReadSigned(Bytes, Offset, Edge.FromCellKey)
        || !ReadSigned(Bytes, Offset, Edge.ToCellKey)
        || !ReadSigned(Bytes, Offset, Edge.InitialQuota)
        || !ReadSigned(Bytes, Offset, Edge.ConsumedQuota))
        return false;
    }
    if (!ReadCount(Bytes, Offset, ClaimCount))
      return false;
    OutValue.ActiveClaims.SetNum(ClaimCount);
    for (FCrowdTargetRegionQuotaAgentClaim& Claim :
      OutValue.ActiveClaims)
    {
      if (!ReadSigned(Bytes, Offset, Claim.AgentId)
        || !ReadSigned(Bytes, Offset, Claim.FromCellKey)
        || !ReadSigned(Bytes, Offset, Claim.ToCellKey))
        return false;
    }
    return ReadSigned(
        Bytes, Offset, OutValue.CompletedTransitionCount)
      && ReadUnsigned(Bytes, Offset, OutValue.ExecutionHash)
      && ReadUnsigned(Bytes, Offset, Valid)
      && Valid <= 1
      && ((OutValue.bValid = Valid != 0), true);
  }
}

using namespace CrowdWorkerTargetPrivate;

bool FCrowdWorkerTargetObjectiveClock::ResolveEffectiveFixedStepIndex(
  const double EffectiveSimulationTimeSeconds,
  const double FixedSimulationQuantumSeconds,
  int32& OutEffectiveFixedStepIndex)
{
  OutEffectiveFixedStepIndex = INDEX_NONE;
  if (!FMath::IsFinite(EffectiveSimulationTimeSeconds)
    || EffectiveSimulationTimeSeconds < 0.0
    || !FMath::IsFinite(FixedSimulationQuantumSeconds)
    || FixedSimulationQuantumSeconds <= 0.0)
    return false;
  const int64 FixedStep = FMath::RoundToInt64(
    EffectiveSimulationTimeSeconds
      / FixedSimulationQuantumSeconds);
  if (FixedStep < 0
    || FixedStep > static_cast<int64>(MAX_int32))
    return false;
  OutEffectiveFixedStepIndex = static_cast<int32>(FixedStep);
  return true;
}

bool FCrowdWorkerTargetAgentInput::IsValid() const
{
  return EntityRef.IsValid()
    && Agent.AgentId != INDEX_NONE
    && !Agent.Location.ContainsNaN()
    && !Agent.Velocity.ContainsNaN()
    && !Agent.FarFlowPreferredVelocity.ContainsNaN()
    && FMath::IsFinite(Agent.MaxSpeedCmps)
    && FMath::IsFinite(Agent.PhysicalRadiusCm)
    && FMath::IsFinite(Agent.HardSafetyGapCm)
    && FMath::IsFinite(Agent.SoftMarginCm)
    && Agent.MaxSpeedCmps >= 0.0f
    && Agent.PhysicalRadiusCm > 0.0f
    && Agent.HardSafetyGapCm >= 0.0f
    && Agent.SoftMarginCm >= 0.0f;
}

bool FCrowdWorkerTargetCohortInput::IsValid() const
{
  if (TopologyRevision == 0
    || TargetRevision < 0 || FixedStepIndex < 0
    || Agents.IsEmpty()
    || Settings.PlanLifetimeSteps <= 0
    || FlowConfig.Revision < 0
    || !FMath::IsFinite(FlowConfig.CellSizeCm)
    || FlowConfig.CellSizeCm <= 0.0f)
    return false;
  FCrowdStableEntityRef PreviousRef;
  int32 PreviousAgentId = MIN_int32;
  TSet<int32> AgentIds;
  for (const FCrowdWorkerTargetAgentInput& Input : Agents)
  {
    if (!Input.IsValid()
      || (!PreviousRef.IsUnset()
        && !(PreviousRef < Input.EntityRef))
      || AgentIds.Contains(Input.Agent.AgentId))
      return false;
    PreviousRef = Input.EntityRef;
    AgentIds.Add(Input.Agent.AgentId);
  }
  PreviousAgentId = MIN_int32;
  bool bHasExternal = false;
  for (const FCrowdTargetRegionTransportAgent& Agent :
    ExternalAgents)
  {
    if (Agent.AgentId == INDEX_NONE
      || AgentIds.Contains(Agent.AgentId)
      || (bHasExternal && Agent.AgentId <= PreviousAgentId)
      || Agent.Location.ContainsNaN()
      || Agent.Velocity.ContainsNaN()
      || Agent.FarFlowPreferredVelocity.ContainsNaN())
      return false;
    PreviousAgentId = Agent.AgentId;
    bHasExternal = true;
  }
  int32 PreviousObstacleId = MIN_int32;
  for (const FCrowdSharedFlowObstacleSpec& Obstacle :
    FlowConfig.ObstacleSpecs)
  {
    if (Obstacle.ObstacleId <= PreviousObstacleId
      || Obstacle.Center.ContainsNaN()
      || Obstacle.Extent.ContainsNaN())
      return false;
    PreviousObstacleId = Obstacle.ObstacleId;
  }
  return true;
}

bool FCrowdWorkerTargetControlResource::IsValid() const
{
  if (Revision == 0 || Cohorts.IsEmpty()) return false;
  uint32 PreviousKey = 0;
  bool bHasPreviousKey = false;
  TSet<FCrowdStableEntityRef> MemberRefs;
  for (const FCrowdWorkerTargetCohortInput& Cohort : Cohorts)
  {
    if (!Cohort.IsValid()
      || (bHasPreviousKey && Cohort.CohortKey <= PreviousKey))
      return false;
    PreviousKey = Cohort.CohortKey;
    bHasPreviousKey = true;
    for (const FCrowdWorkerTargetAgentInput& Agent :
      Cohort.Agents)
    {
      if (MemberRefs.Contains(Agent.EntityRef))
        return false;
      MemberRefs.Add(Agent.EntityRef);
    }
  }
  return true;
}

bool FCrowdWorkerTargetObjectiveRevision::IsValid() const
{
  return TargetRevision >= 0
    && EffectiveFixedStepIndex >= 0
    && !TargetLocation.ContainsNaN()
    && !TargetVelocity.ContainsNaN();
}

bool FCrowdWorkerTargetObjectiveRevisionCodec::Encode(
  const FCrowdWorkerTargetObjectiveRevision& Revision,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Revision.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  AppendSigned(OutPayload.Bytes, Revision.TargetRevision);
  AppendSigned(
    OutPayload.Bytes, Revision.EffectiveFixedStepIndex);
  AppendVector2(OutPayload.Bytes, Revision.TargetLocation);
  AppendVector2(OutPayload.Bytes, Revision.TargetVelocity);
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerTargetObjectiveRevisionCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerTargetObjectiveRevision& OutRevision)
{
  OutRevision = {};
  int32 Offset = 0;
  return Payload.SchemaId == SchemaId
    && Payload.SchemaVersion == SchemaVersion
    && Payload.StableHash == Payload.CalculateStableHash()
    && ReadSigned(
      Payload.Bytes, Offset, OutRevision.TargetRevision)
    && ReadSigned(
      Payload.Bytes, Offset,
      OutRevision.EffectiveFixedStepIndex)
    && ReadVector2(
      Payload.Bytes, Offset, OutRevision.TargetLocation)
    && ReadVector2(
      Payload.Bytes, Offset, OutRevision.TargetVelocity)
    && Offset == Payload.Bytes.Num()
    && OutRevision.IsValid();
}

bool FCrowdWorkerTargetControlResourceCodec::Encode(
  const FCrowdWorkerTargetControlResource& Resource,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!Resource.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  TArray<uint8>& Bytes = OutPayload.Bytes;
  CrowdWorkerTargetPrivate::AppendUnsigned(Bytes, Resource.Revision);
  CrowdWorkerTargetPrivate::AppendUnsigned(
    Bytes, static_cast<uint32>(Resource.Cohorts.Num()));
  for (const FCrowdWorkerTargetCohortInput& Cohort :
    Resource.Cohorts)
  {
    CrowdWorkerTargetPrivate::AppendUnsigned(Bytes, Cohort.CohortKey);
    CrowdWorkerTargetPrivate::AppendUnsigned(Bytes, Cohort.TopologyRevision);
    AppendSigned(Bytes, Cohort.TargetRevision);
    AppendSigned(Bytes, Cohort.FixedStepIndex);
    AppendSettings(Bytes, Cohort.Settings);
    AppendFlow(Bytes, Cohort.FlowConfig);
    CrowdWorkerTargetPrivate::AppendUnsigned(
      Bytes, static_cast<uint32>(Cohort.Agents.Num()));
    for (const FCrowdWorkerTargetAgentInput& Input :
      Cohort.Agents)
    {
      CrowdWorkerTargetPrivate::AppendRef(Bytes, Input.EntityRef);
      AppendAgent(Bytes, Input.Agent);
    }
    CrowdWorkerTargetPrivate::AppendUnsigned(
      Bytes,
      static_cast<uint32>(Cohort.ExternalAgents.Num()));
    for (const FCrowdTargetRegionTransportAgent& Agent :
      Cohort.ExternalAgents)
      AppendAgent(Bytes, Agent);
    AppendPlan(Bytes, Cohort.BootstrapPlan);
    AppendExecution(Bytes, Cohort.BootstrapExecution);
  }
  OutPayload.RecalculateStableHash();
  return true;
}

uint32 FCrowdWorkerTargetControlResourceCodec::
CalculateTopologyRevision(
  const FCrowdTargetPolarTopology& Topology)
{
  if (!Topology.bValid || Topology.Cells.IsEmpty())
    return 0;
  constexpr uint32 Offset = 2166136261u;
  constexpr uint32 Prime = 16777619u;
  uint32 Hash = Offset;
  const auto Fold = [&Hash](const uint32 Value)
  {
    Hash ^= Value;
    Hash *= Prime;
  };
  Fold(Topology.TopologyHash);
  Fold(Topology.FeasibleGraphHash);
  Fold(static_cast<uint32>(Topology.Cells.Num()));
  for (const FCrowdTargetPolarCell& Cell : Topology.Cells)
  {
    uint32 WorldAnchorXBits = 0;
    uint32 WorldAnchorYBits = 0;
    static_assert(sizeof(WorldAnchorXBits)
      == sizeof(Cell.WorldAnchorCm.X));
    FMemory::Memcpy(
      &WorldAnchorXBits,
      &Cell.WorldAnchorCm.X,
      sizeof(WorldAnchorXBits));
    FMemory::Memcpy(
      &WorldAnchorYBits,
      &Cell.WorldAnchorCm.Y,
      sizeof(WorldAnchorYBits));
    Fold(static_cast<uint32>(Cell.StableCellKey));
    Fold(WorldAnchorXBits);
    Fold(WorldAnchorYBits);
    Fold(Cell.bFeasible ? 1u : 0u);
    Fold(Cell.bTerminal ? 1u : 0u);
    Fold(static_cast<uint32>(Cell.Capacity));
    Fold(Cell.bNavigationBlocked ? 1u : 0u);
  }
  return Hash != 0 ? Hash : 1u;
}

bool FCrowdWorkerTargetControlResourceCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerTargetControlResource& OutResource)
{
  OutResource = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  uint32 CohortCount = 0;
  if (!ReadUnsigned(Payload.Bytes, Offset, OutResource.Revision)
    || !ReadCount(
      Payload.Bytes, Offset, CohortCount, false))
    return false;
  OutResource.Cohorts.SetNum(CohortCount);
  for (FCrowdWorkerTargetCohortInput& Cohort :
    OutResource.Cohorts)
  {
    uint32 AgentCount = 0;
    uint32 ExternalCount = 0;
    if (!ReadUnsigned(
        Payload.Bytes, Offset, Cohort.CohortKey)
      || !ReadUnsigned(
        Payload.Bytes, Offset, Cohort.TopologyRevision)
      || !ReadSigned(
        Payload.Bytes, Offset, Cohort.TargetRevision)
      || !ReadSigned(
        Payload.Bytes, Offset, Cohort.FixedStepIndex)
      || !ReadSettings(Payload.Bytes, Offset, Cohort.Settings)
      || !ReadFlow(Payload.Bytes, Offset, Cohort.FlowConfig)
      || !ReadCount(
        Payload.Bytes, Offset, AgentCount, false))
      return false;
    Cohort.Agents.SetNum(AgentCount);
    for (FCrowdWorkerTargetAgentInput& Input : Cohort.Agents)
    {
      if (!ReadRef(Payload.Bytes, Offset, Input.EntityRef)
        || !ReadAgent(Payload.Bytes, Offset, Input.Agent))
        return false;
    }
    if (!ReadCount(
        Payload.Bytes, Offset, ExternalCount))
      return false;
    Cohort.ExternalAgents.SetNum(ExternalCount);
    for (FCrowdTargetRegionTransportAgent& Agent :
      Cohort.ExternalAgents)
    {
      if (!ReadAgent(Payload.Bytes, Offset, Agent))
        return false;
    }
    if (!ReadPlan(
        Payload.Bytes, Offset, Cohort.BootstrapPlan)
      || !ReadExecution(
        Payload.Bytes, Offset, Cohort.BootstrapExecution))
      return false;
  }
  return Offset == Payload.Bytes.Num()
    && OutResource.IsValid();
}

bool FCrowdWorkerTargetState::IsValid() const
{
  return TargetRevision >= 0
    && static_cast<uint8>(Mode)
      <= static_cast<uint8>(
        ECrowdTargetRegionGuidanceMode::CapacityHold)
    && !DesiredVelocity.ContainsNaN()
    && ExecutionHash != 0
    && GuidanceHash != 0;
}

bool FCrowdWorkerTargetStateCodec::Encode(
  const FCrowdWorkerTargetState& State,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!State.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  CrowdWorkerTargetPrivate::AppendUnsigned(
    OutPayload.Bytes, State.CohortKey);
  AppendSigned(OutPayload.Bytes, State.TargetRevision);
  AppendSigned(OutPayload.Bytes, State.CurrentCellKey);
  AppendSigned(OutPayload.Bytes, State.NextCellKey);
  AppendSigned(OutPayload.Bytes, State.DemandRegionKey);
  CrowdWorkerTargetPrivate::AppendUnsigned(
    OutPayload.Bytes, static_cast<uint8>(State.Mode));
  AppendVector(OutPayload.Bytes, State.DesiredVelocity);
  CrowdWorkerTargetPrivate::AppendUnsigned(
    OutPayload.Bytes, State.ExecutionHash);
  CrowdWorkerTargetPrivate::AppendUnsigned(
    OutPayload.Bytes, State.GuidanceHash);
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerTargetStateCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerTargetState& OutState)
{
  OutState = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  uint8 Mode = 0;
  if (!ReadUnsigned(Payload.Bytes, Offset, OutState.CohortKey)
    || !ReadSigned(Payload.Bytes, Offset, OutState.TargetRevision)
    || !ReadSigned(Payload.Bytes, Offset, OutState.CurrentCellKey)
    || !ReadSigned(Payload.Bytes, Offset, OutState.NextCellKey)
    || !ReadSigned(Payload.Bytes, Offset, OutState.DemandRegionKey)
    || !ReadUnsigned(Payload.Bytes, Offset, Mode)
    || Mode > static_cast<uint8>(
      ECrowdTargetRegionGuidanceMode::CapacityHold)
    || !ReadVector(
      Payload.Bytes, Offset, OutState.DesiredVelocity)
    || !ReadUnsigned(
      Payload.Bytes, Offset, OutState.ExecutionHash)
    || !ReadUnsigned(
      Payload.Bytes, Offset, OutState.GuidanceHash)
    || Offset != Payload.Bytes.Num())
    return false;
  OutState.Mode =
    static_cast<ECrowdTargetRegionGuidanceMode>(Mode);
  return OutState.IsValid();
}

bool FCrowdWorkerTargetCohortState::IsValid() const
{
  return TopologyRevision != 0
    && TargetRevision >= 0
    && FeasibleCellCount >= 0
    && EdgeCount >= 0
    && FeasibleRegionCount >= 0
    && FeasibleRegionCoverageCount >= 0
    && FeasibleRegionCoverageCount <= FeasibleRegionCount
    && CurrentTerminalPopulation >= 0
    && MaximumRegionPopulation >= 0
    && DesiredPopulationTotal >= 0
    && ReleasedClaimCount >= 0
    && OverbookedCellCount >= 0
    && Plan.bValid
    && Execution.bValid
    && Plan.TargetRevision == TargetRevision
    && Execution.PlanEpoch == Plan.PlanEpoch
    && Execution.PlanTransportHash == Plan.TransportHash;
}

bool FCrowdWorkerTargetCohortStateCodec::Encode(
  const FCrowdWorkerTargetCohortState& State,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!State.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  CrowdWorkerTargetPrivate::AppendUnsigned(
    OutPayload.Bytes, State.CohortKey);
  CrowdWorkerTargetPrivate::AppendUnsigned(
    OutPayload.Bytes, State.TopologyRevision);
  AppendSigned(OutPayload.Bytes, State.TargetRevision);
  CrowdWorkerTargetPrivate::AppendUnsigned(
    OutPayload.Bytes, State.ObjectiveResourceRevision);
  AppendSigned(OutPayload.Bytes, State.ObjectiveEffectiveFixedStep);
  AppendVector2(OutPayload.Bytes, State.EffectiveTargetLocation);
  AppendVector2(OutPayload.Bytes, State.EffectiveTargetVelocity);
  AppendSigned(OutPayload.Bytes, State.FeasibleCellCount);
  AppendSigned(OutPayload.Bytes, State.EdgeCount);
  AppendSigned(OutPayload.Bytes, State.FeasibleRegionCount);
  AppendSigned(OutPayload.Bytes, State.FeasibleRegionCoverageCount);
  AppendSigned(OutPayload.Bytes, State.CurrentTerminalPopulation);
  AppendSigned(OutPayload.Bytes, State.MaximumRegionPopulation);
  AppendSigned(OutPayload.Bytes, State.DesiredPopulationTotal);
  AppendSigned(OutPayload.Bytes, State.ReleasedClaimCount);
  AppendSigned(OutPayload.Bytes, State.OverbookedCellCount);
  AppendPlan(OutPayload.Bytes, State.Plan);
  AppendExecution(OutPayload.Bytes, State.Execution);
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerTargetCohortStateCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerTargetCohortState& OutState)
{
  OutState = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  return ReadUnsigned(Payload.Bytes, Offset, OutState.CohortKey)
    && ReadUnsigned(
      Payload.Bytes, Offset, OutState.TopologyRevision)
    && ReadSigned(Payload.Bytes, Offset, OutState.TargetRevision)
    && ReadUnsigned(
      Payload.Bytes, Offset, OutState.ObjectiveResourceRevision)
    && ReadSigned(
      Payload.Bytes, Offset, OutState.ObjectiveEffectiveFixedStep)
    && ReadVector2(
      Payload.Bytes, Offset, OutState.EffectiveTargetLocation)
    && ReadVector2(
      Payload.Bytes, Offset, OutState.EffectiveTargetVelocity)
    && ReadSigned(Payload.Bytes, Offset, OutState.FeasibleCellCount)
    && ReadSigned(Payload.Bytes, Offset, OutState.EdgeCount)
    && ReadSigned(Payload.Bytes, Offset, OutState.FeasibleRegionCount)
    && ReadSigned(
      Payload.Bytes, Offset, OutState.FeasibleRegionCoverageCount)
    && ReadSigned(
      Payload.Bytes, Offset, OutState.CurrentTerminalPopulation)
    && ReadSigned(
      Payload.Bytes, Offset, OutState.MaximumRegionPopulation)
    && ReadSigned(
      Payload.Bytes, Offset, OutState.DesiredPopulationTotal)
    && ReadSigned(
      Payload.Bytes, Offset, OutState.ReleasedClaimCount)
    && ReadSigned(
      Payload.Bytes, Offset, OutState.OverbookedCellCount)
    && ReadPlan(Payload.Bytes, Offset, OutState.Plan)
    && ReadExecution(Payload.Bytes, Offset, OutState.Execution)
    && Offset == Payload.Bytes.Num()
    && OutState.IsValid();
}

void FCrowdWorkerTargetDomainExecutor::GetDependencies(
  TArray<ECrowdWorkerDomainId>& OutDependencies) const
{
  OutDependencies = {ECrowdWorkerDomainId::FlowResource};
}

FCrowdWorkerTargetDomainMetrics
FCrowdWorkerTargetDomainExecutor::GetMetrics() const
{
  FScopeLock Lock(&StateMutex);
  return Metrics;
}

bool FCrowdWorkerTargetDomainExecutor::Execute(
  const FCrowdWorkerDomainContext& Context,
  const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
  FCrowdWorkerDomainOutput& OutOutput)
{
  bool bRejectTargetContextValid = false;
  int32 RejectTargetRevision = INDEX_NONE;
  FVector2f RejectTargetLocation = FVector2f::ZeroVector;
  FVector2f RejectTargetVelocity = FVector2f::ZeroVector;
  const auto Reject = [&Context, &WorkItems,
    &bRejectTargetContextValid, &RejectTargetRevision,
    &RejectTargetLocation, &RejectTargetVelocity](
    const TCHAR* Stage,
    const uint32 CohortKey = 0)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerTargetDomainRejected stage=%s fixed_step=%llu generation=%llu epoch=%llu input=%llu work_count=%d cohort=%u target_context_valid=%d target_revision=%d target_x=%.3f target_y=%.3f target_velocity_x=%.3f target_velocity_y=%.3f"),
      Stage,
      Context.AbsoluteSimulationTick,
      Context.Generation,
      Context.WorkerEpoch,
      Context.LastAppliedInputSequence,
      WorkItems.Num(),
      CohortKey,
      bRejectTargetContextValid ? 1 : 0,
      RejectTargetRevision,
      RejectTargetLocation.X,
      RejectTargetLocation.Y,
      RejectTargetVelocity.X,
      RejectTargetVelocity.Y);
    return false;
  };
  if (!Context.Resources || !Context.EntityStates
    || Context.Generation == 0 || WorkItems.IsEmpty())
    return Reject(TEXT("contract"));
  const bool bFullResourceWork = WorkItems.Num() == 1
    && WorkItems[0].Key.Domain == ECrowdWorkerDomainId::Target
    && WorkItems[0].Key.Kind == ECrowdWorkerWorkKind::Resource
    && WorkItems[0].Key.ScopeKey
      == CrowdWorkerResourceIds::TargetControl;
  TMap<uint32, const FCrowdWorkerWorkItem*> WorkByCohort;
  if (!bFullResourceWork)
  {
    for (const FCrowdWorkerWorkItem& Work : WorkItems)
    {
      uint32 CohortKey = 0;
      if (Work.Key.Domain != ECrowdWorkerDomainId::Target
        || Work.Key.Kind != ECrowdWorkerWorkKind::Cohort
        || !CrowdWorkerTargetWorkScopes::DecodeCohortKey(
          Work.Key.ScopeKey, CohortKey)
        || WorkByCohort.Contains(CohortKey))
        return Reject(TEXT("cohort_work"), CohortKey);
      WorkByCohort.Add(CohortKey, &Work);
    }
  }
  const FCrowdWorkerResourceRecord* Record =
    Context.Resources->FindCurrent(
      CrowdWorkerResourceIds::TargetControl);
  FCrowdWorkerTargetControlResource Control;
  if (!Record
    || !FCrowdWorkerTargetControlResourceCodec::Decode(
      Record->Payload, Control)
    || Control.Revision != Record->Revision)
    return Reject(TEXT("resource_decode"));
  const FCrowdWorkerResourceRecord* FlowRecord =
    Context.Resources->FindCurrent(
      CrowdWorkerResourceIds::Environment);
  FCrowdWorkerFlowFieldResource FlowField;
  if (!FlowRecord
    || !FCrowdWorkerFlowFieldResourceCodec::Decode(
      FlowRecord->Payload, FlowField)
    || FlowField.Revision != FlowRecord->Revision)
    return Reject(TEXT("flow_resource_decode"));
  const uint64 ObjectiveResourceId =
    CrowdWorkerResourceIds::ObjectiveRevision(
      CrowdWorkerTargetObjectiveIds::PrimaryTarget);
  const FCrowdWorkerResourceRecord* ObjectiveRecord =
    Context.Resources->FindCurrent(ObjectiveResourceId);
  FCrowdWorkerTargetObjectiveRevision Objective;
  if (!ObjectiveRecord
    || !FCrowdWorkerTargetObjectiveRevisionCodec::Decode(
      ObjectiveRecord->Payload, Objective)
    || ObjectiveRecord->Revision == 0)
    return Reject(TEXT("objective_decode"));
  if (Objective.EffectiveFixedStepIndex
    > static_cast<int64>(Context.AbsoluteSimulationTick))
  {
    const auto DeferCohort = [&Control, &OutOutput, &Objective,
      &ObjectiveRecord](const uint32 CohortKey)
    {
      const FCrowdWorkerTargetCohortInput* Cohort =
        Control.Cohorts.FindByPredicate([CohortKey](const auto& Input)
        {
          return Input.CohortKey == CohortKey;
        });
      if (!Cohort || Cohort->Agents.IsEmpty()
        || !Cohort->Agents[0].EntityRef.IsValid())
        return false;
      FCrowdWorkerWakeup Wakeup;
      Wakeup.Key.Domain = ECrowdWorkerDomainId::Target;
      Wakeup.Key.EntityRef = Cohort->Agents[0].EntityRef;
      Wakeup.Key.WakeupId =
        CrowdWorkerTargetWorkScopes::EncodeCohortKey(CohortKey);
      Wakeup.AbsoluteSimulationTick = static_cast<uint64>(
        Objective.EffectiveFixedStepIndex);
      Wakeup.Revision = ObjectiveRecord->Revision;
      Wakeup.Priority = ECrowdWorkerWorkPriority::High;
      Wakeup.ReasonMask = 1ull << 20;
      OutOutput.Wakeups.Add(MoveTemp(Wakeup));
      return true;
    };
    if (bFullResourceWork)
    {
      for (const FCrowdWorkerTargetCohortInput& Cohort : Control.Cohorts)
        if (!DeferCohort(Cohort.CohortKey))
          return Reject(TEXT("objective_defer"), Cohort.CohortKey);
    }
    else
    {
      TArray<uint32> CohortKeys;
      WorkByCohort.GetKeys(CohortKeys);
      CohortKeys.Sort();
      for (const uint32 CohortKey : CohortKeys)
        if (!DeferCohort(CohortKey))
          return Reject(TEXT("objective_defer"), CohortKey);
    }
    return true;
  }

  FScopeLock Lock(&StateMutex);
  if (StateGeneration != Context.Generation)
  {
    Cohorts.Reset();
    StateGeneration = Context.Generation;
    Metrics = {};
  }
  TMap<int32, FCrowdStableEntityRef> EntityRefByAgentId;
  for (const FCrowdWorkerTargetCohortInput& Cohort : Control.Cohorts)
  {
    for (const FCrowdWorkerTargetAgentInput& Agent : Cohort.Agents)
    {
      const FCrowdStableEntityRef* Existing =
        EntityRefByAgentId.Find(Agent.Agent.AgentId);
      if ((Existing && *Existing != Agent.EntityRef)
        || !Agent.EntityRef.IsValid())
        return Reject(TEXT("entity_identity"), Cohort.CohortKey);
      EntityRefByAgentId.Add(
        Agent.Agent.AgentId, Agent.EntityRef);
    }
  }
  const auto RefreshKinematic =
    [&Context, &EntityRefByAgentId, &FlowField](
      FCrowdTargetRegionTransportAgent& Agent)
  {
    const FCrowdStableEntityRef* EntityRef =
      EntityRefByAgentId.Find(Agent.AgentId);
    if (!EntityRef)
      return true;
    const FCrowdWorkerDirtyStateRecord* State =
      Context.EntityStates->Find(
        *EntityRef, ECrowdWorkerField::Facing);
    if (!State)
      State = Context.EntityStates->Find(
        *EntityRef, ECrowdWorkerField::Movement);
    if (!State)
      return true;
    FCrowdWorkerMovementState Movement;
    if (!FCrowdWorkerMovementStateCodec::Decode(
        State->Payload, Movement))
      return false;
    Agent.Location = FVector2f(
      Movement.Position.X, Movement.Position.Y);
    Agent.Velocity = FVector2f(
      Movement.Velocity.X, Movement.Velocity.Y);
    if (const FCrowdWorkerDirtyStateRecord* PreviousTarget =
      Context.EntityStates->Find(
        *EntityRef, ECrowdWorkerField::Target))
    {
      FCrowdWorkerTargetState TargetState;
      if (!FCrowdWorkerTargetStateCodec::Decode(
          PreviousTarget->Payload, TargetState))
        return false;
      Agent.bEngagedHold =
        TargetState.Mode
          == ECrowdTargetRegionGuidanceMode::EngagedHold;
    }
    FVector FlowDirection;
    bool bReachable = false;
    if (!FlowField.Sample(
        Movement.Position, FlowDirection, bReachable))
      return false;
    Agent.FarFlowPreferredVelocity = bReachable
      ? FCrowdTargetRegionTransportKernel::
          ComposeTargetAdvectedFarFlowVelocity(
            FVector2f(FlowDirection.X, FlowDirection.Y)
              * Agent.MaxSpeedCmps,
            FVector2f::ZeroVector,
            Agent.MaxSpeedCmps)
      : FVector2f::ZeroVector;
    return true;
  };
  TSet<uint32> CurrentCohorts;
  for (const FCrowdWorkerTargetCohortInput& Input :
    Control.Cohorts)
  {
    const FCrowdWorkerWorkItem* SourceWork = bFullResourceWork
      ? &WorkItems[0]
      : WorkByCohort.FindRef(Input.CohortKey);
    if (!SourceWork) continue;
    FCrowdWorkerWorkItem DependentWork = *SourceWork;
    DependentWork.Key.Kind = ECrowdWorkerWorkKind::Cohort;
    DependentWork.Key.PrimaryEntity = {};
    DependentWork.Key.SecondaryEntity = {};
    DependentWork.Key.ScopeKey =
      CrowdWorkerTargetWorkScopes::EncodeCohortKey(
        Input.CohortKey);
    FCrowdTargetRegionTransportSettings EffectiveSettings =
      Input.Settings;
    const double ObjectiveAgeSeconds =
      static_cast<double>(Context.AbsoluteSimulationTick
        - static_cast<uint64>(Objective.EffectiveFixedStepIndex))
      * Context.FixedDeltaSeconds;
    EffectiveSettings.TargetLocation =
      Objective.TargetLocation
      + Objective.TargetVelocity
        * static_cast<float>(ObjectiveAgeSeconds);
    EffectiveSettings.TargetVelocity = Objective.TargetVelocity;
    if (Input.CohortKey == 0
      && (ObjectiveRecord->Revision == 1
        || ObjectiveRecord->Revision % 300 == 0))
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdWorkerTargetClockCheckpoint absolute_tick=%llu effective_tick=%d age_seconds=%.6f objective_resource_revision=%llu target_revision=%d base_x=%.3f base_y=%.3f effective_x=%.3f effective_y=%.3f velocity_x=%.3f velocity_y=%.3f environment_revision=%llu flow_build_hash=%u"),
        Context.AbsoluteSimulationTick,
        Objective.EffectiveFixedStepIndex,
        ObjectiveAgeSeconds,
        ObjectiveRecord->Revision,
        Objective.TargetRevision,
        Objective.TargetLocation.X,
        Objective.TargetLocation.Y,
        EffectiveSettings.TargetLocation.X,
        EffectiveSettings.TargetLocation.Y,
        EffectiveSettings.TargetVelocity.X,
        EffectiveSettings.TargetVelocity.Y,
        FlowField.Revision,
        FlowField.BuildHash);
    }
    bRejectTargetContextValid = true;
    RejectTargetRevision = Objective.TargetRevision;
    RejectTargetLocation = EffectiveSettings.TargetLocation;
    RejectTargetVelocity = EffectiveSettings.TargetVelocity;
    CurrentCohorts.Add(Input.CohortKey);
    FCohortRuntime& Runtime = Cohorts.FindOrAdd(Input.CohortKey);
    if (!Runtime.Topology.bValid
      || Runtime.TopologyRevision != Input.TopologyRevision
      || Runtime.ObjectiveResourceRevision
        != ObjectiveRecord->Revision)
    {
      FCrowdMassTargetRegionTopologyInput TopologyInput;
      TopologyInput.Settings = EffectiveSettings;
      if (!IsTopologyCompatible(
          Input.FlowConfig, FlowField.Field.Config))
        return Reject(
          TEXT("flow_resource_topology"), Input.CohortKey);
      TopologyInput.FlowConfig = Input.FlowConfig;
      TopologyInput.SharedFlowField = &FlowField.Field;
      Runtime.Topology =
        FCrowdMassTargetRegionWork::BuildTopology(TopologyInput);
      Runtime.TopologyRevision = Input.TopologyRevision;
      Runtime.ObjectiveResourceRevision =
        ObjectiveRecord->Revision;
      ++Metrics.TopologyBuildCount;
      if (!Runtime.Topology.bValid)
        return Reject(TEXT("topology"), Input.CohortKey);
    }

    FCrowdMassTargetRegionDemandInput DemandInput;
    DemandInput.Settings = EffectiveSettings;
    if (!IsTopologyCompatible(
        Input.FlowConfig, FlowField.Field.Config))
      return Reject(
        TEXT("flow_resource_topology"), Input.CohortKey);
    DemandInput.FlowConfig = Input.FlowConfig;
    DemandInput.SharedFlowField = &FlowField.Field;
    DemandInput.Topology = Runtime.Topology.Topology;
    DemandInput.PreviousDemand = {};
    DemandInput.bUpdateStaticPopulation = false;
    DemandInput.bRefreshSourceAttachments = true;
    DemandInput.ExternalAgents = Input.ExternalAgents;
    DemandInput.Agents.Reserve(Input.Agents.Num());
    for (const FCrowdWorkerTargetAgentInput& Agent : Input.Agents)
    {
      FCrowdTargetRegionTransportAgent& Refreshed =
        DemandInput.Agents.Add_GetRef(Agent.Agent);
      if (!RefreshKinematic(Refreshed))
        return Reject(TEXT("agent_kinematic"), Input.CohortKey);
    }
    for (FCrowdTargetRegionTransportAgent& External :
      DemandInput.ExternalAgents)
      if (!RefreshKinematic(External))
        return Reject(TEXT("external_kinematic"), Input.CohortKey);
    for (FCrowdTargetRegionTransportAgent& Agent : DemandInput.Agents)
      Agent.FarFlowPreferredVelocity =
        FCrowdTargetRegionTransportKernel::
          ComposeTargetAdvectedFarFlowVelocity(
            Agent.FarFlowPreferredVelocity,
            EffectiveSettings.TargetVelocity,
            Agent.MaxSpeedCmps);
    for (FCrowdTargetRegionTransportAgent& Agent :
      DemandInput.ExternalAgents)
      Agent.FarFlowPreferredVelocity =
        FCrowdTargetRegionTransportKernel::
          ComposeTargetAdvectedFarFlowVelocity(
            Agent.FarFlowPreferredVelocity,
            EffectiveSettings.TargetVelocity,
            Agent.MaxSpeedCmps);
    const FCrowdMassTargetRegionDemandOutput Demand =
      FCrowdMassTargetRegionWork::BuildDemand(DemandInput);
    if (!Demand.bValid)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdWorkerTargetDemandRejected fixed_step=%llu generation=%llu epoch=%llu input=%llu cohort=%u target_revision=%d agents=%d external_agents=%d regions=%d feasible_regions=%d desired=%d source_attachment_failures=%d topology_cells=%d topology_edges=%d target_x=%.3f target_y=%.3f target_velocity_x=%.3f target_velocity_y=%.3f flow_revision=%llu flow_build_hash=%u"),
        Context.AbsoluteSimulationTick,
        Context.Generation,
        Context.WorkerEpoch,
        Context.LastAppliedInputSequence,
        Input.CohortKey,
        Objective.TargetRevision,
        Input.Agents.Num(),
        Input.ExternalAgents.Num(),
        Demand.Demand.Regions.Num(),
        Demand.Demand.FeasibleRegionCount,
        Demand.Demand.DesiredPopulationTotal,
        Demand.Demand.SourceAttachmentFailureCount,
        Runtime.Topology.Topology.Cells.Num(),
        Runtime.Topology.Topology.Edges.Num(),
        EffectiveSettings.TargetLocation.X,
        EffectiveSettings.TargetLocation.Y,
        EffectiveSettings.TargetVelocity.X,
        EffectiveSettings.TargetVelocity.Y,
        FlowField.Revision,
        FlowField.BuildHash);
      return Reject(TEXT("demand"), Input.CohortKey);
    }

    FCrowdWorkerTargetCohortState PreviousCohort;
    bool bHasPreviousCohort = false;
    FCrowdWorkerPayload PreviousCohortPayload;
    for (const FCrowdWorkerTargetAgentInput& Agent : Input.Agents)
    {
      const FCrowdWorkerDirtyStateRecord* PreviousRecord =
        Context.EntityStates->Find(
          Agent.EntityRef, ECrowdWorkerField::TargetCohort);
      if (!PreviousRecord) continue;
      FCrowdWorkerTargetCohortState Candidate;
      if (!FCrowdWorkerTargetCohortStateCodec::Decode(
          PreviousRecord->Payload, Candidate))
        return Reject(TEXT("cohort_state"), Input.CohortKey);
      if (Candidate.CohortKey != Input.CohortKey
        || Candidate.TopologyRevision != Input.TopologyRevision
        || Candidate.TargetRevision != Objective.TargetRevision)
        continue;
      if (bHasPreviousCohort
        && !(PreviousCohortPayload == PreviousRecord->Payload))
        return Reject(TEXT("cohort_state_diverged"), Input.CohortKey);
      PreviousCohort = MoveTemp(Candidate);
      PreviousCohortPayload = PreviousRecord->Payload;
      bHasPreviousCohort = true;
    }

    FCrowdMassTargetRegionPlanInput PlanInput;
    PlanInput.Topology = Runtime.Topology.Topology;
    PlanInput.Demand = Demand.Demand;
    PlanInput.PreviousPlan = bHasPreviousCohort
      ? PreviousCohort.Plan : Input.BootstrapPlan;
    PlanInput.PreviousExecution = bHasPreviousCohort
      ? PreviousCohort.Execution : Input.BootstrapExecution;
    PlanInput.FixedStepIndex = static_cast<int32>(
      Context.AbsoluteSimulationTick);
    PlanInput.TargetRevision = Objective.TargetRevision;
    PlanInput.PlanLifetimeSteps =
      EffectiveSettings.PlanLifetimeSteps;
    const FCrowdMassTargetRegionPlanOutput Plan =
      FCrowdMassTargetRegionWork::SolvePlan(PlanInput);
    if (!Plan.bValid)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdWorkerTargetPlanRejected fixed_step=%llu cohort=%u rebuild_reason=%d desired=%d capacity=%d assignable=%d overflow=%d deficit=%d supply=%d plan_valid=%d routed=%d unrouted=%d execution_valid=%d missing_edge=%d infeasible_edge=%d invalid_cell=%d insufficient_quota=%d conservation=%d unreachable=%d overbook=%d first_cell=%d first_agent=%d feasible_graph_hash=%u demand_hash=%u transport_hash=%u execution_hash=%u"),
        Context.AbsoluteSimulationTick, Input.CohortKey,
        Plan.RebuildReason, Demand.Demand.DesiredPopulationTotal,
        Demand.Demand.TotalFeasibleCapacity,
        Demand.Demand.AssignablePopulation,
        Demand.Demand.OverflowPopulation,
        Demand.Demand.TotalDeficit, Demand.Demand.SupplyAgentCount,
        Plan.Plan.bValid ? 1 : 0, Plan.Plan.RoutedAgentCount,
        Plan.Plan.UnroutedAgentCount, Plan.Execution.bValid ? 1 : 0,
        Plan.Validation.MissingEdgeCount,
        Plan.Validation.InfeasibleEdgeCount,
        Plan.Validation.InvalidCellCount,
        Plan.Validation.InsufficientOutgoingQuotaCellCount,
        Plan.Validation.FlowConservationFailureCount,
        Plan.Validation.UnreachableDeficitCount,
        Plan.Validation.OverbookedCellCount,
        Plan.Validation.FirstFailureCellKey,
        Plan.Validation.FirstFailureAgentId,
        Runtime.Topology.Topology.FeasibleGraphHash,
        Demand.Demand.DemandHash, Plan.Plan.TransportHash,
        Plan.Execution.ExecutionHash);
      for (const FCrowdTargetRegionAgentDemandState& State :
        Demand.Demand.AgentStates)
      {
        if (!State.bSupply) continue;
        TBitArray<> Visited(false, Runtime.Topology.Topology.Cells.Num());
        TArray<int32> Queue;
        int32 OutgoingEdgeCount = 0;
        int32 ReachableTerminalCellCount = 0;
        int32 ReachableAdmissionCapacity = 0;
        uint32 ReachableDeficitRegionMask = 0;
        if (Visited.IsValidIndex(State.CurrentCellKey))
        {
          Visited[State.CurrentCellKey] = true;
          Queue.Add(State.CurrentCellKey);
        }
        for (int32 Head = 0; Head < Queue.Num(); ++Head)
        {
          const int32 CellKey = Queue[Head];
          const FCrowdTargetPolarCell& Cell =
            Runtime.Topology.Topology.Cells[CellKey];
          if (Cell.bTerminal
            && Demand.Demand.Regions.IsValidIndex(
              Cell.PrimaryDemandRegionKey))
          {
            const int32 Available =
              Demand.Demand.AvailableCapacityByCell.IsValidIndex(CellKey)
              ? FMath::Max(0,
                  Demand.Demand.AvailableCapacityByCell[CellKey]
                    - Demand.Demand.AdmittedPopulationByCell[CellKey])
              : 0;
            const int32 Deficit = Demand.Demand.Regions[
              Cell.PrimaryDemandRegionKey].Deficit;
            if (Available > 0 && Deficit > 0)
            {
              ++ReachableTerminalCellCount;
              ReachableAdmissionCapacity += FMath::Min(Available, Deficit);
              if (Cell.PrimaryDemandRegionKey >= 0
                && Cell.PrimaryDemandRegionKey < 32)
                ReachableDeficitRegionMask |=
                  1u << Cell.PrimaryDemandRegionKey;
            }
          }
          for (const FCrowdTargetPolarEdge& Edge :
            Runtime.Topology.Topology.Edges)
          {
            if (Edge.FromCellKey != CellKey) continue;
            if (CellKey == State.CurrentCellKey) ++OutgoingEdgeCount;
            if (Visited.IsValidIndex(Edge.ToCellKey)
              && !Visited[Edge.ToCellKey])
            {
              Visited[Edge.ToCellKey] = true;
              Queue.Add(Edge.ToCellKey);
            }
          }
        }
        UE_LOG(LogTemp, Error,
          TEXT("CrowdWorkerTargetPlanSupplyDiagnostic fixed_step=%llu agent=%d source_cell=%d current_region=%d assigned_region=%d outgoing_edges=%d reachable_cells=%d reachable_terminal_cells=%d reachable_admission_capacity=%d reachable_deficit_region_mask=%u"),
          Context.AbsoluteSimulationTick, State.AgentId,
          State.CurrentCellKey, State.CurrentRegionKey,
          State.AssignedRegionKey, OutgoingEdgeCount, Queue.Num(),
          ReachableTerminalCellCount, ReachableAdmissionCapacity,
          ReachableDeficitRegionMask);
      }
      for (const FCrowdTargetDemandRegion& Region : Demand.Demand.Regions)
      {
        if (Region.Deficit <= 0) continue;
        UE_LOG(LogTemp, Error,
          TEXT("CrowdWorkerTargetPlanRegionDiagnostic fixed_step=%llu region=%d capacity=%d current=%d desired=%d deficit=%d"),
          Context.AbsoluteSimulationTick, Region.StableRegionKey,
          Region.AvailableCapacity, Region.CurrentPopulation,
          Region.DesiredPopulation, Region.Deficit);
      }
      return Reject(TEXT("plan"), Input.CohortKey);
    }

    FCrowdMassTargetRegionGuidanceInput GuidanceInput;
    GuidanceInput.Settings = EffectiveSettings;
    GuidanceInput.Topology = Runtime.Topology.Topology;
    GuidanceInput.Demand = Demand.Demand;
    GuidanceInput.Plan = Plan.Plan;
    GuidanceInput.Execution = Plan.Execution;
    GuidanceInput.Agents = DemandInput.Agents;
    const FCrowdMassTargetRegionGuidanceOutput Guidance =
      FCrowdMassTargetRegionWork::BuildGuidanceSharded(
        GuidanceInput, 128);
    if (!Guidance.bValid
      || Guidance.Results.Num() != Input.Agents.Num())
      return Reject(TEXT("guidance"), Input.CohortKey);
    Metrics.GuidanceShardCount += FMath::DivideAndRoundUp(
      GuidanceInput.Agents.Num(), 128);
    Metrics.GuidanceMaxShardSize = FMath::Max(
      Metrics.GuidanceMaxShardSize,
      FMath::Min(128, GuidanceInput.Agents.Num()));

    if (bHasPreviousCohort
      && PreviousCohort.Plan.MembershipHash
        != Demand.Demand.MembershipHash)
      ++Metrics.MembershipChangeCount;
    if (Plan.RebuildReason == 0)
      ++Metrics.PlanCacheHitCount;
    else
      ++Metrics.PlanBuildCount;
    FCrowdWorkerTargetCohortState CohortState;
    CohortState.CohortKey = Input.CohortKey;
    CohortState.TopologyRevision = Input.TopologyRevision;
    CohortState.TargetRevision = Objective.TargetRevision;
    CohortState.ObjectiveResourceRevision = ObjectiveRecord->Revision;
    CohortState.ObjectiveEffectiveFixedStep =
      Objective.EffectiveFixedStepIndex;
    CohortState.EffectiveTargetLocation = EffectiveSettings.TargetLocation;
    CohortState.EffectiveTargetVelocity = EffectiveSettings.TargetVelocity;
    CohortState.FeasibleCellCount =
      Runtime.Topology.Summary.FeasibleCellCount;
    CohortState.EdgeCount = Runtime.Topology.Summary.EdgeCount;
    CohortState.FeasibleRegionCount = Demand.Demand.FeasibleRegionCount;
    CohortState.CurrentTerminalPopulation =
      Demand.Demand.CurrentTerminalPopulation;
    CohortState.DesiredPopulationTotal =
      Demand.Demand.DesiredPopulationTotal;
    CohortState.ReleasedClaimCount =
      (bHasPreviousCohort ? PreviousCohort.ReleasedClaimCount : 0)
      + Plan.Replacement.ReleasedClaimCount;
    CohortState.OverbookedCellCount =
      Plan.Validation.OverbookedCellCount;
    for (const FCrowdTargetDemandRegion& Region : Demand.Demand.Regions)
    {
      CohortState.MaximumRegionPopulation = FMath::Max(
        CohortState.MaximumRegionPopulation, Region.CurrentPopulation);
      if (Region.bFeasible && Region.CurrentPopulation > 0)
        ++CohortState.FeasibleRegionCoverageCount;
    }
    CohortState.Plan = Plan.Plan;
    CohortState.Execution = Guidance.Execution;
    FCrowdWorkerPayload CohortPayload;
    if (!FCrowdWorkerTargetCohortStateCodec::Encode(
        CohortState, CohortPayload))
      return Reject(TEXT("cohort_state_encode"), Input.CohortKey);
    for (const FCrowdWorkerTargetAgentInput& Agent : Input.Agents)
    {
      const FCrowdWorkerDirtyStateRecord* ExistingCohort =
        Context.EntityStates->Find(
          Agent.EntityRef, ECrowdWorkerField::TargetCohort);
      if (ExistingCohort
        && ExistingCohort->Payload == CohortPayload)
        continue;
      FCrowdWorkerDirtyStateRecord CohortDirty;
      CohortDirty.EntityRef = Agent.EntityRef;
      CohortDirty.Field = ECrowdWorkerField::TargetCohort;
      CohortDirty.Generation = Context.Generation;
      CohortDirty.WorkerEpoch = Context.WorkerEpoch;
      CohortDirty.StateRevision = ExistingCohort
        ? FMath::Max3(
            Control.Revision,
            Context.WorkerEpoch,
            ExistingCohort->StateRevision + 1)
        : FMath::Max(Control.Revision, Context.WorkerEpoch);
      CohortDirty.CorrectionRevision =
        SourceWork->CorrectionRevision;
      CohortDirty.SourceInputSequence =
        Context.LastAppliedInputSequence;
      CohortDirty.Payload = CohortPayload;
      OutOutput.DirtyStates.Add(MoveTemp(CohortDirty));
    }
    TMap<int32, const FCrowdWorkerTargetAgentInput*> AgentById;
    for (const FCrowdWorkerTargetAgentInput& Agent : Input.Agents)
      AgentById.Add(Agent.Agent.AgentId, &Agent);
    for (const FCrowdTargetRegionGuidanceResult& Result :
      Guidance.Results)
    {
      const FCrowdWorkerTargetAgentInput* const* Agent =
        AgentById.Find(Result.AgentId);
      if (!Agent)
        return Reject(TEXT("agent_join"), Input.CohortKey);
      FCrowdWorkerTargetState State;
      State.CohortKey = Input.CohortKey;
      State.TargetRevision = Objective.TargetRevision;
      State.CurrentCellKey = Result.CurrentCellKey;
      State.NextCellKey = Result.NextCellKey;
      State.DemandRegionKey = Result.DemandRegionKey;
      State.Mode = Result.Mode;
      State.DesiredVelocity = FVector(
        Result.DesiredVelocity.X,
        Result.DesiredVelocity.Y, 0.0f);
      State.ExecutionHash = Guidance.Execution.ExecutionHash;
      State.GuidanceHash = Guidance.Summary.GuidanceHash;
      FCrowdWorkerDirtyStateRecord Dirty;
      Dirty.EntityRef = (*Agent)->EntityRef;
      Dirty.Field = ECrowdWorkerField::Target;
      Dirty.Generation = Context.Generation;
      Dirty.WorkerEpoch = Context.WorkerEpoch;
      Dirty.CorrectionRevision =
        SourceWork->CorrectionRevision;
      Dirty.SourceInputSequence =
        Context.LastAppliedInputSequence;
      if (!FCrowdWorkerTargetStateCodec::Encode(
          State, Dirty.Payload))
        return Reject(TEXT("state_encode"), Input.CohortKey);
      const FCrowdWorkerDirtyStateRecord* Existing =
        Context.EntityStates->Find(
          Dirty.EntityRef, ECrowdWorkerField::Target);
      Dirty.StateRevision = Existing
        ? FMath::Max3(
            Control.Revision,
            Context.WorkerEpoch,
            Existing->StateRevision + 1)
        : FMath::Max(Control.Revision, Context.WorkerEpoch);
      if (!Existing || !(Existing->Payload == Dirty.Payload))
      {
        OutOutput.DirtyStates.Add(MoveTemp(Dirty));
        ++Metrics.PublishedPatchCount;
      }
    }
    const auto AddResourceDependency =
      [&OutOutput, &DependentWork](const uint64 ResourceId)
    {
      FCrowdWorkerDependencyDeclaration Declaration;
      Declaration.Source.Kind =
        ECrowdWorkerDependencyKind::Resource;
      Declaration.Source.ScopeKey = ResourceId;
      Declaration.Dependent = DependentWork;
      OutOutput.DeclaredDependencies.Add(Declaration);
      FCrowdWorkerDependencyObservation Observation;
      Observation.Source = Declaration.Source;
      Observation.Dependent = DependentWork.Key;
      OutOutput.ObservedDependencies.Add(Observation);
    };
    AddResourceDependency(CrowdWorkerResourceIds::TargetControl);
    AddResourceDependency(CrowdWorkerResourceIds::Environment);
    AddResourceDependency(ObjectiveResourceId);
    TArray<FCrowdStableEntityRef> DependencyEntities;
    DependencyEntities.Reserve(
      Input.Agents.Num() + Input.ExternalAgents.Num());
    for (const FCrowdWorkerTargetAgentInput& Agent : Input.Agents)
      DependencyEntities.AddUnique(Agent.EntityRef);
    for (const FCrowdTargetRegionTransportAgent& External :
      Input.ExternalAgents)
    {
      if (const FCrowdStableEntityRef* EntityRef =
        EntityRefByAgentId.Find(External.AgentId))
        DependencyEntities.AddUnique(*EntityRef);
    }
    DependencyEntities.Sort();
    for (const FCrowdStableEntityRef& EntityRef :
      DependencyEntities)
    {
      FCrowdWorkerDependencyDeclaration Declaration;
      Declaration.Source.Kind =
        ECrowdWorkerDependencyKind::Entity;
      Declaration.Source.EntityRef = EntityRef;
      Declaration.Source.ScopeKey =
        CrowdWorkerRuntimeV2DependencyScopeForField(
          ECrowdWorkerField::Facing);
      Declaration.Dependent = DependentWork;
      OutOutput.DeclaredDependencies.Add(Declaration);
      FCrowdWorkerDependencyObservation Observation;
      Observation.Source = Declaration.Source;
      Observation.Dependent = DependentWork.Key;
      OutOutput.ObservedDependencies.Add(Observation);
    }
  }
  if (bFullResourceWork)
  {
    for (auto It = Cohorts.CreateIterator(); It; ++It)
      if (!CurrentCohorts.Contains(It.Key()))
        It.RemoveCurrent();
  }
  return true;
}
