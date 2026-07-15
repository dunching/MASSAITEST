#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoTargetApproachKernel.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

struct FCrowdDemoTargetSlotLayoutInput
{
  FCrowdDemoTargetFact Target;
  FCrowdDemoParticleProfile ParticleProfile;
  FCrowdDemoTargetSlotLayoutRuleSettings Settings;
  FCrowdDemoSharedFlowFieldConfig FlowConfig;
  const FCrowdDemoSharedFlowField* FlowField = nullptr;
  float TransitionRingRadiusCm = 600.0f;
};

struct FCrowdDemoTargetSlotLayout
{
  int32 TargetId = INDEX_NONE;
  int32 TargetRevision = INDEX_NONE;
  int32 SlotLayoutRevision = INDEX_NONE;
  TArray<FCrowdDemoTargetSlotSpec> Slots;
  uint32 TopologyHash = 2166136261u;
  uint32 WorldValidationHash = 2166136261u;
  uint32 FullInputHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoTargetSlotLayoutSummary
{
  int32 GeneratedCandidateCount = 0;
  int32 AcceptedFunctionalCount = 0;
  int32 AcceptedFillCount = 0;
  int32 RejectedTargetClearanceCount = 0;
  int32 RejectedPairSpacingCount = 0;
  int32 RejectedObstacleCount = 0;
  int32 RejectedBoundsCount = 0;
  int32 RejectedUnreachableCount = 0;
  int32 RejectedIngressSegmentCount = 0;
  uint32 TopologyHash = 2166136261u;
  uint32 WorldValidationHash = 2166136261u;
  uint32 FullInputHash = 2166136261u;
  bool bValid = false;
};

class MASSAICROWDDEMO_API FCrowdDemoTargetSlotLayoutKernel
{
public:
  static void Build(
    const FCrowdDemoTargetSlotLayoutInput& Input,
    const FCrowdDemoTargetSlotLayout* PreviousLayout,
    FCrowdDemoTargetSlotLayout& OutLayout,
    FCrowdDemoTargetSlotLayoutSummary& OutSummary);
};
