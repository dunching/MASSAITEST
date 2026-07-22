#include "MassCrowdSharedFlowWork.h"

namespace
{
  uint32 Fold(uint32 Hash, const uint32 Value)
  {
    Hash ^= Value;
    return Hash * 16777619u;
  }

  uint32 QuantizeFloat(const float Value, const float Scale = 1.0f)
  {
    return static_cast<uint32>(FMath::RoundToInt(Value * Scale));
  }

  bool ObstaclesMatch(
    const TArray<FCrowdSharedFlowObstacleSpec>& A,
    const TArray<FCrowdSharedFlowObstacleSpec>& B)
  {
    if (A.Num() != B.Num()) return false;
    for (int32 Index = 0; Index < A.Num(); ++Index)
    {
      if (A[Index].ObstacleId != B[Index].ObstacleId
        || !A[Index].Center.Equals(B[Index].Center, 0.01f)
        || !A[Index].Extent.Equals(B[Index].Extent, 0.01f))
        return false;
    }
    return true;
  }

  bool TopologyMatches(
    const FCrowdSharedFlowField& Field,
    const FCrowdSharedFlowFieldConfig& Config,
    const bool bCompareObstacles)
  {
    return Field.IsValid()
      && Field.Config.Revision == Config.Revision
      && Field.Config.ConnectivityContractVersion
        == Config.ConnectivityContractVersion
      && FMath::IsNearlyEqual(Field.Config.CellSizeCm, Config.CellSizeCm, 0.001f)
      && FMath::IsNearlyEqual(Field.Config.AgentInflateCm, Config.AgentInflateCm, 0.001f)
      && Field.Config.BoundsMin.Equals(Config.BoundsMin, 0.01f)
      && Field.Config.BoundsMax.Equals(Config.BoundsMax, 0.01f)
      && (!bCompareObstacles
        || ObstaclesMatch(Field.Config.ObstacleSpecs, Config.ObstacleSpecs));
  }
}

FCrowdMassSharedFlowBuildOutput FCrowdMassSharedFlowWork::EnsureResource(
  const FCrowdMassSharedFlowBuildInput& Input,
  FCrowdMassSharedFlowResource& InOutResource)
{
  FCrowdMassSharedFlowBuildOutput Output;
  const bool bTopologyMatches = TopologyMatches(
    InOutResource.Field, Input.Config, Input.bDynamicTarget);
  const bool bStaticGoalMatches = !Input.bDynamicTarget
    && bTopologyMatches
    && InOutResource.Field.Config.GoalLocation.Equals(
      Input.Config.GoalLocation, 0.01f);
  if (!bTopologyMatches || (!Input.bDynamicTarget && !bStaticGoalMatches))
  {
    if (!FCrowdSharedFlowFieldKernel::Build(
      Input.Config, InOutResource.Field))
      return Output;
    ++InOutResource.FieldRebuildCount;
    InOutResource.DynamicAnchorCellKey = INDEX_NONE;
    Output.bFieldRebuilt = true;
  }

  if (Input.bDynamicTarget)
  {
    int32 AnchorCellKey = INDEX_NONE;
    FVector AnchorLocation = FVector::ZeroVector;
    if (!FCrowdSharedFlowFieldKernel::ResolveGoalAnchor(
      InOutResource.Field, Input.TargetLocation,
      AnchorCellKey, AnchorLocation))
      return Output;
    const bool bSemanticAnchorChange =
      AnchorCellKey != InOutResource.DynamicAnchorCellKey;
    if (bSemanticAnchorChange || Input.bForceIntegrationRefresh)
    {
      if (!FCrowdSharedFlowFieldKernel::BuildIntegrationForAnchor(
        AnchorCellKey, AnchorLocation, InOutResource.Field))
        return Output;
      InOutResource.DynamicAnchorCellKey = AnchorCellKey;
      if (bSemanticAnchorChange)
        ++InOutResource.IntegrationRebuildCount;
      Output.bIntegrationRebuilt = true;
    }
  }

  Output.DynamicAnchorCellKey = InOutResource.DynamicAnchorCellKey;
  Output.StableHash = Fold(2166136261u, InOutResource.Field.BuildHash);
  Output.StableHash = Fold(Output.StableHash, InOutResource.Field.TopologyHash);
  Output.StableHash = Fold(Output.StableHash, InOutResource.Field.IntegrationHash);
  Output.StableHash = Fold(
    Output.StableHash,
    static_cast<uint32>(InOutResource.DynamicAnchorCellKey));
  Output.bValid = InOutResource.Field.IsValid();
  return Output;
}

FCrowdMassSharedFlowSampleOutput FCrowdMassSharedFlowWork::BuildPreferred(
  const FCrowdMassSharedFlowSampleInput& Input)
{
  FCrowdMassSharedFlowSampleOutput Output;
  Output.FixedStepIndex = Input.FixedStepIndex;
  Output.PlanRevision = Input.PlanRevision;
  if (Input.FixedStepIndex < 0 || Input.PlanRevision < 0
    || Input.FixedStepSeconds <= SMALL_NUMBER || Input.Fields.IsEmpty()
    || Input.Agents.IsEmpty())
    return Output;

  for (const FCrowdSharedFlowField* Field : Input.Fields)
    if (!Field || !Field->IsValid()) return Output;

  TArray<FCrowdMassSharedFlowAgentInput> Agents = Input.Agents;
  Agents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  int32 PreviousAgentId = INDEX_NONE;
  uint32 Hash = Fold(2166136261u, static_cast<uint32>(Input.FixedStepIndex));
  Hash = Fold(Hash, static_cast<uint32>(Input.PlanRevision));
  for (const FCrowdMassSharedFlowAgentInput& Agent : Agents)
  {
    if (Agent.AgentId == INDEX_NONE || Agent.AgentId <= PreviousAgentId
      || Agent.LifecycleSerial <= 0 || Agent.FieldIndex < 0
      || Agent.FieldIndex >= Input.Fields.Num()
      || !FMath::IsFinite(Agent.Location.X)
      || !FMath::IsFinite(Agent.Location.Y)
      || !FMath::IsFinite(Agent.MaximumSpeedCmps)
      || Agent.MaximumSpeedCmps < 0.0f)
      return Output;
    PreviousAgentId = Agent.AgentId;
    const FCrowdSharedFlowField& Field = *Input.Fields[Agent.FieldIndex];
    FCrowdMassSharedFlowAgentOutput& Result =
      Output.Agents.AddDefaulted_GetRef();
    Result.AgentId = Agent.AgentId;
    if (Agent.bBypassFlow)
    {
      Result.Sample.Status = ECrowdFlowLocationStatus::Reachable;
      Result.Sample.bUnreachable = false;
    }
    else
    {
      Result.Sample = FCrowdSharedFlowFieldKernel::Sample(
        Field, Agent.Location);
    }

    float DesiredSpeedCmps = Agent.MaximumSpeedCmps;
    if (!Agent.bBypassFlow
      && Field.Config.ConnectivityContractVersion > 0
      && Result.Sample.GuidanceDistanceCm > 0.0f)
      DesiredSpeedCmps = FMath::Min(
        DesiredSpeedCmps,
        Result.Sample.GuidanceDistanceCm / Input.FixedStepSeconds);
    const FVector DesiredVelocity = Agent.bShouldStop
      || Result.Sample.bUnreachable || Agent.bBypassFlow
      ? FVector::ZeroVector
      : Result.Sample.FlowDirection * DesiredSpeedCmps;
    const float DesiredYaw = DesiredVelocity.IsNearlyZero()
      ? Agent.CurrentYawDegrees : DesiredVelocity.Rotation().Yaw;
    Result.Candidate = FCrowdGuidanceComposeKernel::BuildCandidate(
      Agent.AgentId, ECrowdGuidanceProvider::SharedFlow,
      Input.PlanRevision, DesiredVelocity, Agent.GoalLocation,
      DesiredYaw, true);
    if (!Result.Candidate.bValid) return Output;

    Result.bDesiredSegmentViolation =
      !Agent.bBypassFlow
      && Field.Config.ConnectivityContractVersion > 0
      && !DesiredVelocity.IsNearlyZero()
      && !FCrowdSharedFlowFieldKernel::CanTraverseWorldSegment(
        Field.Config, Agent.Location,
        Agent.Location + DesiredVelocity * Input.FixedStepSeconds);
    Output.RecoveredAgentCount +=
      Result.Sample.bRecoveredFromRasterMismatch ? 1 : 0;
    Output.DesiredSegmentViolationCount +=
      Result.bDesiredSegmentViolation ? 1 : 0;
    Output.SourceAttachmentSuccessCount +=
      Result.Sample.bSourceAttached ? 1 : 0;
    Output.UnreachableSampleCount += Result.Sample.bUnreachable ? 1 : 0;
    Hash = Fold(Hash, static_cast<uint32>(Result.AgentId));
    Hash = Fold(Hash, static_cast<uint32>(Result.Sample.StableCellKey));
    Hash = Fold(Hash, static_cast<uint32>(Result.Sample.IntegrationCost));
    Hash = Fold(Hash, QuantizeFloat(Result.Sample.FlowDirection.X, 32767.0f));
    Hash = Fold(Hash, QuantizeFloat(Result.Sample.FlowDirection.Y, 32767.0f));
    Hash = Fold(Hash, Result.Candidate.StableHash);
  }
  Output.StableHash = Hash;
  Output.bValid = Output.Agents.Num() == Agents.Num();
  return Output;
}
