#pragma once

#include "CoreMinimal.h"

enum class ECrowdDemoTargetApproachState : uint8
{
  Approach,
  SlotIngress,
  SlotOccupied,
  FreeSettle
};

enum class ECrowdDemoTargetSlotKind : uint8
{
  Functional,
  Fill
};

struct FCrowdDemoTargetFact
{
  int32 TargetId = INDEX_NONE;
  int32 TargetRevision = INDEX_NONE;
  int32 MotionStep = 0;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  float YawDegrees = 0.0f;
  float PhysicalRadiusCm = 0.0f;
};

struct FCrowdDemoTargetSlotSpec
{
  int32 SlotId = INDEX_NONE;
  ECrowdDemoTargetSlotKind Kind = ECrowdDemoTargetSlotKind::Fill;
  int32 BandId = INDEX_NONE;
  int32 AngularIndex = INDEX_NONE;
  FVector2f TargetRelativeOffset = FVector2f::ZeroVector;
  float CenterDistanceCm = 0.0f;
  uint32 RequiredCapabilityMask = 0;
  int32 StablePriority = 0;
};

struct FCrowdDemoTargetApproachAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  float PhysicalRadiusCm = 0.0f;
  float MaxSpeedCmps = 0.0f;
  uint32 CapabilityMask = 0;
  float MinimumFunctionalDistanceCm = 0.0f;
  float MaximumFunctionalDistanceCm = 1000000.0f;
  int32 StableBusinessPriority = 0;
  ECrowdDemoTargetApproachState ExistingState = ECrowdDemoTargetApproachState::Approach;
  int32 ExistingSlotId = INDEX_NONE;
  int32 ExistingTargetRevision = INDEX_NONE;
  int32 ExistingSlotLayoutRevision = INDEX_NONE;
  int32 RingEnterFixedStep = INDEX_NONE;
  int32 StateEnterFixedStep = 0;
};

struct FCrowdDemoTargetApproachSettings
{
  bool bEnabled = false;
  float TransitionRingRadiusCm = 600.0f;
  float RingEnterToleranceCm = 10.0f;
  float RingExitToleranceCm = 40.0f;
  float ApproachSlowdownDistanceCm = 200.0f;
  float SlotArrivalToleranceCm = 20.0f;
  float SlotArrivalSpeedToleranceCmps = 20.0f;
  float SlotExitToleranceCm = 40.0f;
  float SlotArriveGainPerSecond = 2.0f;
  float SlotOccupiedGainPerSecond = 0.5f;
  float FreeSettleAttractionGainPerSecond = 1.0f;
  float FreeSettleMaxSpeedCmps = 180.0f;
  float TargetPhysicalRadiusCm = 100.0f;
  float TargetHardSafetyGapCm = 10.0f;
  float TargetSoftMarginCm = 17.0f;
  float PositionQuantumCm = 1.0f;
  float VelocityQuantumCmps = 1.0f;
};

struct FCrowdDemoTargetApproachResult
{
  int32 AgentId = INDEX_NONE;
  ECrowdDemoTargetApproachState State = ECrowdDemoTargetApproachState::Approach;
  int32 AssignedSlotId = INDEX_NONE;
  int32 SlotLayoutRevision = INDEX_NONE;
  int32 RingEnterFixedStep = INDEX_NONE;
  int32 StateEnterFixedStep = 0;
  FVector2f DesiredLocation = FVector2f::ZeroVector;
  FVector2f DesiredVelocity = FVector2f::ZeroVector;
  bool bEnteredRing = false;
  bool bStateChanged = false;
  bool bSettled = false;
};

struct FCrowdDemoTargetApproachSummary
{
  bool bValid = false;
  int32 ApproachAgentCount = 0;
  int32 RingEnteredCount = 0;
  int32 RingWaitingCount = 0;
  int32 FunctionalSlotCapacity = 0;
  int32 FunctionalSlotOccupied = 0;
  int32 FillSlotCapacity = 0;
  int32 FillSlotOccupied = 0;
  int32 SlotIngressCount = 0;
  int32 SlotOccupiedCount = 0;
  int32 FreeSettleCount = 0;
  int32 FreeSettledCount = 0;
  int32 DuplicateSlotOwnerCount = 0;
  int32 InvalidSlotOwnerCount = 0;
  int32 StateTransitionCount = 0;
  int32 SlotOwnerReleaseCount = 0;
  int32 SlotOwnerReusedCount = 0;
  int32 SlotOwnerConflictCount = 0;
  int32 SlotLayoutRevisionMismatchCount = 0;
  uint32 TargetFactHash = 0;
  uint32 AgentInputHash = 0;
  uint32 AgentFineKinematicHash = 0;
  uint32 AgentConfigHash = 0;
  uint32 AgentTemporalHash = 0;
  uint32 SettingsHash = 0;
  uint32 SlotInputHash = 0;
  uint32 FullInputHash = 0;
  uint32 OwnerStateHash = 0;
  uint32 TransitionHash = 0;
  uint32 GuidanceHash = 0;
  uint32 GuidanceLocationHash = 0;
  uint32 GuidanceVelocityHash = 0;
  uint32 ApproachHash = 0;
  uint32 ScheduleHash = 2166136261u;
  uint32 CommitHash = 2166136261u;
};

struct FCrowdDemoTargetApproachCommitAgent
{
  int32 AgentId = INDEX_NONE;
  uint32 CapabilityMask = 0;
  float MinimumFunctionalDistanceCm = 0.0f;
  float MaximumFunctionalDistanceCm = 1000000.0f;
};

struct FCrowdDemoTargetApproachCommitValidation
{
  bool bValid = false;
  int32 OwnerConflictCount = 0;
  int32 RevisionMismatchCount = 0;
  uint32 CommitHash = 2166136261u;
};

class MASSAICROWDDEMO_API FCrowdDemoTargetApproachKernel
{
public:
  static FVector2f StableDirectionFromAgentId(int32 AgentId);

  static FVector2f FindNearestTransitionRingPoint(
    const FCrowdDemoTargetFact& Target,
    const FVector2f& AgentLocation,
    int32 AgentId,
    float TransitionRingRadiusCm);

  static FVector2f TransformTargetRelativePoint(
    const FCrowdDemoTargetFact& Target,
    const FVector2f& TargetRelativePoint);

  static FCrowdDemoTargetFact BuildLinearMotionFact(
    int32 TargetId,
    int32 TargetRevision,
    int32 MotionStep,
    const FVector2f& InitialLocation,
    const FVector2f& LinearVelocity,
    float InitialYawDegrees,
    float YawRateDegreesPerSecond,
    float PhysicalRadiusCm,
    float FixedStepSeconds,
    float PositionQuantumCm,
    float VelocityQuantumCmps);

  static FCrowdDemoTargetFact BuildReflectedLinearMotionFact(
    int32 TargetId,
    int32 TargetRevision,
    int32 MotionStep,
    const FVector2f& InitialLocation,
    const FVector2f& LinearVelocity,
    const FVector2f& MotionBoundsMin,
    const FVector2f& MotionBoundsMax,
    float InitialYawDegrees,
    float YawRateDegreesPerSecond,
    float PhysicalRadiusCm,
    float FixedStepSeconds,
    float PositionQuantumCm,
    float VelocityQuantumCmps);

  static void ValidateAtomicCommit(
    int32 SlotLayoutRevision,
    TConstArrayView<FCrowdDemoTargetSlotSpec> Slots,
    TConstArrayView<FCrowdDemoTargetApproachCommitAgent> Agents,
    TConstArrayView<FCrowdDemoTargetApproachResult> Decisions,
    FCrowdDemoTargetApproachCommitValidation& OutValidation);

  static void Solve(
    const FCrowdDemoTargetFact& Target,
    const FCrowdDemoTargetApproachSettings& Settings,
    TConstArrayView<FCrowdDemoTargetSlotSpec> Slots,
    TConstArrayView<FCrowdDemoTargetApproachAgent> Agents,
    int32 FixedStepIndex,
    TArray<FCrowdDemoTargetApproachResult>& OutResults,
    FCrowdDemoTargetApproachSummary& OutSummary,
    int32 SlotLayoutRevision = 1);
};
