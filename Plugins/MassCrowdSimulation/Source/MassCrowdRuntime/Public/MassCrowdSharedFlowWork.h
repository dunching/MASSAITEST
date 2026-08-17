#pragma once

#include "CoreMinimal.h"
#include "CrowdGuidanceComposeKernel.h"
#include "CrowdSharedFlowFieldKernel.h"

struct FCrowdMassSharedFlowResource
{
  FCrowdSharedFlowField Field;
  int32 DynamicAnchorCellKey = INDEX_NONE;
  int32 FieldRebuildCount = 0;
  int32 IntegrationRebuildCount = 0;
};

struct FCrowdMassSharedFlowBuildInput
{
  FCrowdSharedFlowFieldConfig Config;
  FVector TargetLocation = FVector::ZeroVector;
  bool bDynamicTarget = false;
  bool bForceIntegrationRefresh = false;
};

struct FCrowdMassSharedFlowBuildOutput
{
  int32 DynamicAnchorCellKey = INDEX_NONE;
  bool bFieldRebuilt = false;
  bool bIntegrationRebuilt = false;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdMassSharedFlowAgentInput
{
  int32 AgentId = INDEX_NONE;
  int32 LifecycleSerial = 0;
  int32 FieldIndex = INDEX_NONE;
  FVector Location = FVector::ZeroVector;
  FVector GoalLocation = FVector::ZeroVector;
  float CurrentYawDegrees = 0.0f;
  float MaximumSpeedCmps = 0.0f;
  bool bShouldStop = false;
  bool bBypassFlow = false;
};

struct FCrowdMassSharedFlowAgentOutput
{
  int32 AgentId = INDEX_NONE;
  FCrowdSharedFlowSample Sample;
  FCrowdGuidanceCandidate Candidate;
  bool bDesiredSegmentViolation = false;
};

struct FCrowdMassSharedFlowSampleInput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  float FixedStepSeconds = 0.0f;
  TArray<const FCrowdSharedFlowField*> Fields;
  TArray<FCrowdMassSharedFlowAgentInput> Agents;
};

struct FCrowdMassSharedFlowSampleOutput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdMassSharedFlowAgentOutput> Agents;
  int32 RecoveredAgentCount = 0;
  int32 DesiredSegmentViolationCount = 0;
  int32 SourceAttachmentSuccessCount = 0;
  int32 UnreachableSampleCount = 0;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

class MASSCROWDRUNTIME_API FCrowdMassSharedFlowWork
{
public:
  static FCrowdMassSharedFlowBuildOutput EnsureResource(
    const FCrowdMassSharedFlowBuildInput& Input,
    FCrowdMassSharedFlowResource& InOutResource);

  static FCrowdMassSharedFlowSampleOutput BuildPreferred(
    const FCrowdMassSharedFlowSampleInput& Input);

  static FCrowdMassSharedFlowSampleOutput BuildPreferredSharded(
    const FCrowdMassSharedFlowSampleInput& Input,
    int32 ShardSize,
    bool bReverseDispatchOrder = false);
};
