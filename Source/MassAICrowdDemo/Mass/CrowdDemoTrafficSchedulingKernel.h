#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

struct FCrowdDemoTrafficAgent
{
  int32 AgentId = INDEX_NONE;
  int32 CohortId = 0;
  FVector2f Position = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  FVector2f FlowDirection = FVector2f::ZeroVector;
  float RadiusCm = 42.0f;
  int32 PreviousPortalId = INDEX_NONE;
  int32 PreviousDirectionEpoch = INDEX_NONE;
  int16 PreviousBandId = INDEX_NONE;
  int32 WaitSteps = 0;
  int32 PreviousDirectionKey = 0;
  int32 TokenGrantedStep = INDEX_NONE;
  int32 EnteredPortalStep = INDEX_NONE;
  int32 LastTransitionStep = INDEX_NONE;
  ECrowdDemoPortalAdmissionState AdmissionState = ECrowdDemoPortalAdmissionState::None;
};

struct FCrowdDemoTrafficCell
{
  int32 StableCellKey = INDEX_NONE;
  int32 AgentCount = 0;
  int32 ReservedAgentCount = 0;
  FVector2f MeanVelocity = FVector2f::ZeroVector;
  FVector2f DominantDirection = FVector2f::ZeroVector;
};

struct FCrowdDemoTrafficPortal
{
  int32 PortalId = INDEX_NONE;
  int32 Axis = 0;
  int32 CrossSectionCoordinate = 0;
  int32 SpanMin = 0;
  int32 SpanMax = 0;
  int32 WidthCells = 0;
  int32 Capacity = 1;
  int32 StableCellKey = INDEX_NONE;
  int32 UpstreamWidthCells = 0;
  int32 DownstreamWidthCells = 0;
  int32 MergedCandidateCount = 0;
  FVector2f Center = FVector2f::ZeroVector;
};

struct FCrowdDemoPortalExtractionSummary
{
  int32 RawCrossSectionCandidateCount = 0;
  int32 UniqueCrossSectionCandidateCount = 0;
  int32 LocalMinimumCandidateCount = 0;
  int32 ExtractedPortalCount = 0;
  int32 DuplicateRejectedCount = 0;
  int32 PlateauRejectedCount = 0;
  uint32 GeometryHash = 2166136261u;
};

struct FCrowdDemoTrafficPortalRuntime
{
  FCrowdDemoTrafficPortal Portal;
  int32 ActiveDirectionKey = 0;
  int32 DirectionEpoch = 0;
  int32 GreenSteps = 0;
  int32 ClearanceStepsRemaining = 0;
  int32 OccupiedCount = 0;
  int32 ReservedCount = 0;
};

struct FCrowdDemoPortalCandidate
{
  int32 AgentId = INDEX_NONE;
  int32 CohortId = 0;
  int32 PortalId = INDEX_NONE;
  int32 DirectionKey = 0;
  int32 WaitEpoch = 0;
  int32 DistanceBucket = 0;
  int32 WaitSteps = 0;
  FVector2f FlowDirection = FVector2f::ZeroVector;
  FVector2f PortalDirection = FVector2f::ZeroVector;
  float SignedAxialDistanceCm = 0.0f;
  float LateralDistanceCm = 0.0f;
  bool bNewBinding = false;
  bool bRebinding = false;
};

struct FCrowdDemoPortalDecision
{
  int32 AgentId = INDEX_NONE;
  int32 PortalId = INDEX_NONE;
  int32 DirectionKey = 0;
  int32 DirectionEpoch = 0;
  int16 BandId = INDEX_NONE;
  ECrowdDemoPortalAdmissionState State = ECrowdDemoPortalAdmissionState::None;
  bool bGranted = false;
  bool bBandAssigned = false;
  bool bBandReassigned = false;
  bool bReservationTimedOut = false;
  bool bTransitTimedOut = false;
  bool bHasHoldingTarget = false;
  bool bBound = false;
  bool bRebound = false;
  FVector2f PortalDirection = FVector2f::ZeroVector;
  FVector2f HoldingTarget = FVector2f::ZeroVector;
  float BandLateralErrorCm = 0.0f;
  int32 TokenGrantedStep = INDEX_NONE;
  int32 EnteredPortalStep = INDEX_NONE;
  int32 LastTransitionStep = INDEX_NONE;
};

struct FCrowdDemoPortalCandidateBuildSummary
{
  int32 BindCount = 0;
  int32 RebindCount = 0;
  int32 ReleaseCount = 0;
  int32 InvalidSideCandidateCount = 0;
  int32 WrongSpanCandidateCount = 0;
};

struct FCrowdDemoTrafficStepSummary
{
  uint32 TrafficFieldHash = 0;
  uint32 PortalDecisionHash = 0;
  int32 PortalCount = 0;
  int32 QueuedCount = 0;
  int32 OccupiedCount = 0;
  int32 AdmissionGrantedCount = 0;
  int32 AdmissionDeniedCount = 0;
  int32 BandAssignmentCount = 0;
  int32 BandReassignmentCount = 0;
  int32 DirectionEpochChangeCount = 0;
  int32 ReservationTimeoutCount = 0;
  int32 TransitTimeoutCount = 0;
  int32 CapacityViolationCount = 0;
  int32 DensityAgentCountMax = 0;
  int32 PortalBindCount = 0;
  int32 PortalRebindCount = 0;
  int32 PortalReleaseCount = 0;
  int32 InvalidSideCandidateCount = 0;
  int32 WrongSpanCandidateCount = 0;
  int32 HoldingTargetCount = 0;
  int32 HoldingTargetAllocationFailureCount = 0;
  int32 HoldingTargetOverlapCount = 0;
  int32 ReservedToInsideCount = 0;
  int32 InsideToExitedCount = 0;
  int32 ReservedPositiveAxialVelocityCount = 0;
  int32 ReservedZeroVelocityCount = 0;
  TArray<float> BandLateralErrors;
};

class MASSAICROWDDEMO_API FCrowdDemoTrafficSchedulingKernel
{
public:
  static void BuildTrafficCells(
    TConstArrayView<FCrowdDemoTrafficAgent> Agents,
    const FCrowdDemoSharedFlowFieldConfig& Config,
    const FCrowdDemoTrafficSettings& Settings,
    TArray<FCrowdDemoTrafficCell>& OutCells,
    uint32& OutHash);

  static void ExtractPortals(
    const FCrowdDemoSharedFlowField& FlowField,
    const FCrowdDemoTrafficSettings& Settings,
    TArray<FCrowdDemoTrafficPortal>& OutPortals,
    FCrowdDemoPortalExtractionSummary* OutSummary = nullptr,
    int32 PreferredAxis = INDEX_NONE);

  static void BuildCandidates(
    TConstArrayView<FCrowdDemoTrafficAgent> Agents,
    TConstArrayView<FCrowdDemoTrafficPortalRuntime> Portals,
    const FCrowdDemoTrafficSettings& Settings,
    TArray<FCrowdDemoPortalCandidate>& OutCandidates,
    FCrowdDemoPortalCandidateBuildSummary* OutSummary = nullptr);

  static void StepPortalSchedule(
    TArray<FCrowdDemoTrafficPortalRuntime>& InOutPortals,
    TConstArrayView<FCrowdDemoTrafficAgent> Agents,
    TConstArrayView<FCrowdDemoPortalCandidate> Candidates,
    const FCrowdDemoTrafficSettings& Settings,
    int32 FixedStepIndex,
    TArray<FCrowdDemoPortalDecision>& OutDecisions,
    FCrowdDemoTrafficStepSummary& OutSummary);

  static void BuildHoldingTargets(
    TConstArrayView<FCrowdDemoTrafficAgent> Agents,
    TConstArrayView<FCrowdDemoTrafficPortalRuntime> Portals,
    const FCrowdDemoSharedFlowField& FlowField,
    const FCrowdDemoTrafficSettings& Settings,
    TArray<FCrowdDemoPortalDecision>& InOutDecisions,
    FCrowdDemoTrafficStepSummary& InOutSummary);

  static FVector2f ApplyDensityAndBandGuidance(
    const FCrowdDemoTrafficAgent& Agent,
    const FCrowdDemoTrafficCell* Cell,
    const FCrowdDemoTrafficPortalRuntime* Portal,
    FCrowdDemoPortalDecision* Decision,
    const FCrowdDemoTrafficSettings& Settings,
    float MaxSpeedCmps);

  static uint32 StableHash(int32 A, int32 B, int32 C);
};
