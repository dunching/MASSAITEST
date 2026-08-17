#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

struct FCrowdDemoParticleConstraintAgent
{
  int32 AgentId = INDEX_NONE;
  FVector StartPosition = FVector::ZeroVector;
  FVector PredictedPosition = FVector::ZeroVector;
  float PhysicalRadiusCm = 42.0f;
  float HardSafetyGapCm = 10.0f;
  // Optional shared navigation-domain clearance for obstacle/bounds contacts.
  // Zero preserves the physical radius + hard gap contract.
  float EnvironmentHardClearanceCm = 0.0f;
  float SoftMarginCm = 17.0f;
  float Mobility = 1.0f;
};

struct FCrowdDemoParticleConstraintPair
{
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  int32 MinAgentIndex = INDEX_NONE;
  int32 MaxAgentIndex = INDEX_NONE;
};

struct FCrowdDemoParticleConstraintEnvironment
{
  FCrowdDemoSharedFlowFieldConfig FlowConfig;
  bool bConstrainToFlowBounds = true;
};

enum class ECrowdDemoParticleEnvironmentContactKind : uint8
{
  ObstacleEndpoint = 0,
  ObstacleSwept = 1,
  FlowBounds = 2,
};

enum class ECrowdDemoParticleEnvironmentFace : uint8
{
  MinX = 0,
  MaxX = 1,
  MinY = 2,
  MaxY = 3,
};

struct FCrowdDemoParticleEnvironmentContact
{
  int32 AgentId = INDEX_NONE;
  int32 AgentIndex = INDEX_NONE;
  int32 EnvironmentId = INDEX_NONE;
  ECrowdDemoParticleEnvironmentContactKind ContactKind =
    ECrowdDemoParticleEnvironmentContactKind::ObstacleEndpoint;
  ECrowdDemoParticleEnvironmentFace Face = ECrowdDemoParticleEnvironmentFace::MinX;
  FVector ClosestPoint = FVector::ZeroVector;
  FVector CorrectionNormal = FVector::ForwardVector;
  float HardDistanceCm = 0.0f;
  float SoftDistanceCm = 0.0f;
  float SoftErrorCm = 0.0f;
  float HardDeficitCm = 0.0f;
  float SweptTime = 1.0f;
  float ConstraintThreshold = 0.0f;
};

enum class ECrowdDemoParticleHardConstraintKind : uint8
{
  PairEndpoint = 0,
  PairSwept = 1,
  ObstacleEndpoint = 2,
  ObstacleSwept = 3,
  FlowBounds = 4,
};

struct FCrowdDemoParticleHardConstraint
{
  ECrowdDemoParticleHardConstraintKind Kind =
    ECrowdDemoParticleHardConstraintKind::PairEndpoint;
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  int32 MinAgentIndex = INDEX_NONE;
  int32 MaxAgentIndex = INDEX_NONE;
  int32 EnvironmentId = INDEX_NONE;
  ECrowdDemoParticleEnvironmentFace Face = ECrowdDemoParticleEnvironmentFace::MinX;
  FVector Normal = FVector::ForwardVector;
  float CoefficientScale = 1.0f;
  float Threshold = 0.0f;
  float InitialDeficitCm = 0.0f;
};

struct FCrowdDemoParticleHardDualState
{
  ECrowdDemoParticleHardConstraintKind Kind =
    ECrowdDemoParticleHardConstraintKind::PairEndpoint;
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  int32 EnvironmentId = INDEX_NONE;
  ECrowdDemoParticleEnvironmentFace Face = ECrowdDemoParticleEnvironmentFace::MinX;
  int32 NormalXQ15 = 0;
  int32 NormalYQ15 = 0;
  float Lambda = 0.0f;
};

struct FCrowdDemoParticleUnifiedHardSummary
{
  bool bValid = true;
  int32 ConstraintCount = 0;
  int32 InfeasibleConstraintCount = 0;
  float MaxResidualCm = 0.0f;
  float MaxAppliedCorrectionCm = 0.0f;
};

struct FCrowdDemoParticleConstraintSettings
{
  float FixedStepSeconds = 1.0f / 30.0f;
  int32 IterationCount = 8;
  int32 SafetyIterationCount = 8;
  float SoftResponsePerSecond = 8.0f;
  float SoftMaxPairCorrectionPerIterationCm = 8.0f;
  float SoftMaxEnvironmentCorrectionPerIterationCm = 8.0f;
  float HardMaxPairCorrectionPerIterationCm = 24.0f;
  float PositionQuantumCm = 1.0f;
  float VelocityQuantumCmps = 1.0f;
  bool bCaptureSafetyStageTrace = false;
  bool bCaptureRouteDiagnostic = false;
};

struct FCrowdDemoParticleConstraintResult
{
  int32 AgentId = INDEX_NONE;
  FVector CorrectedPosition = FVector::ZeroVector;
  FVector CorrectedVelocity = FVector::ZeroVector;
  FVector RealizedCorrection = FVector::ZeroVector;
  int32 FirstInfluencedIteration = INDEX_NONE;
  int32 CorrectedPairCount = 0;
};

struct FCrowdDemoParticleConstraintSummary
{
  bool bValid = false;
  int32 CandidatePairCount = 0;
  int32 SoftPairCount = 0;
  int32 SoftViolatingPairCount = 0;
  int32 HardPairViolationCount = 0;
  int32 SweptPairViolationCount = 0;
  int32 ObstaclePenetrationCount = 0;
  int32 BoundsViolationCount = 0;
  int32 EnvironmentSoftContactCount = 0;
  int32 EnvironmentSoftAppliedAgentCount = 0;
  int32 UnifiedHardConstraintCount = 0;
  int32 UnifiedHardInfeasibleCount = 0;
  int32 PressureInfluencedAgentCount = 0;
  int32 FirstInfluencedIterationMax = 0;
  int32 CorrectedAgentCount = 0;
  float SoftErrorCmP50 = 0.0f;
  float SoftErrorCmP95 = 0.0f;
  float SoftErrorCmMax = 0.0f;
  float EnvironmentSoftErrorCmP50 = 0.0f;
  float EnvironmentSoftErrorCmP95 = 0.0f;
  float EnvironmentSoftErrorCmMax = 0.0f;
  float EnvironmentSoftRequestedCorrectionCmMax = 0.0f;
  float EnvironmentSoftRealizedCorrectionCmMax = 0.0f;
  float UnifiedHardResidualCmMax = 0.0f;
  float MaxAgentCorrectionCm = 0.0f;
  uint32 CandidateHash = 2166136261u;
};

struct FCrowdDemoParticleSettlingTracker
{
  int32 StepCount = 0;
  int32 ConsecutiveSettledSampleCount = 0;
  int32 SettlingSteps = INDEX_NONE;
  float PreviousSoftErrorCmP95 = -1.0f;
};

struct FCrowdDemoParticleAppliedState
{
  int32 AgentId = INDEX_NONE;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
};

struct FCrowdDemoParticleAppliedRoundSimState
{
  int32 AgentId = INDEX_NONE;
  int32 LifecycleSerial = 0;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float YawDegrees = 0.0f;
  float RadiusCm = 42.0f;
  bool bInitialized = false;
  FCrowdDemoCombatNetState Combat;
};

enum class ECrowdDemoParticleSafetyStage : uint8
{
  Input = 0,
  UnifiedHard = 1,
  Quantized = 2,
};

struct FCrowdDemoParticleSafetyStageTrace
{
  int32 Iteration = INDEX_NONE;
  ECrowdDemoParticleSafetyStage Stage = ECrowdDemoParticleSafetyStage::Input;
  int32 HardPairViolationCount = 0;
  int32 SweptPairViolationCount = 0;
  int32 ObstacleViolationCount = 0;
  int32 BoundsViolationCount = 0;
  float MinimumEndpointMarginCm = TNumericLimits<float>::Max();
  float MinimumSweptMarginCm = TNumericLimits<float>::Max();
  float MaximumEnvironmentDeficitCm = 0.0f;
  uint32 PositionHash = 2166136261u;
};

struct FCrowdDemoParticleSoftPairInfluence
{
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  FVector RequestedCorrectionA = FVector::ZeroVector;
  FVector RequestedCorrectionB = FVector::ZeroVector;
  FVector RealizedCorrectionA = FVector::ZeroVector;
  FVector RealizedCorrectionB = FVector::ZeroVector;
};

struct FCrowdDemoParticleConstraintTrace
{
  TArray<int32> AgentIds;
  TArray<FVector> StartPositions;
  TArray<FVector> PredictPositions;
  TArray<FVector> SoftPositions;
  TArray<FVector> EnvironmentSoftPositions;
  TArray<FVector> UnifiedHardPositions;
  TArray<FVector> HardPositions;
  TArray<FVector> SweptPositions;
  TArray<FVector> ObstaclePositions;
  TArray<FVector> QuantizedPositions;
  TArray<FVector> FinalSafetyPositions;
  TArray<FCrowdDemoParticleEnvironmentContact> FinalEnvironmentContacts;
  TArray<FCrowdDemoParticleHardConstraint> FinalHardConstraints;
  TArray<FCrowdDemoParticleSafetyStageTrace> SafetyStages;

  // Optional diagnostic-only attribution. These arrays are populated only
  // when bCaptureRouteDiagnostic is enabled and never participate in Solve's
  // candidate hash or constraint decisions.
  TArray<FVector> PairSoftRequestedCorrections;
  TArray<FVector> PairSoftRealizedCorrections;
  TArray<FVector> EnvironmentSoftRequestedCorrections;
  TArray<FVector> EnvironmentSoftRealizedCorrections;
  TArray<FVector> UnifiedHardCorrections;
  TArray<TArray<int32>> ActiveNeighborAgentIds;
  TArray<FCrowdDemoParticleSoftPairInfluence> SoftPairInfluences;
};

struct FCrowdDemoParticleFailureFixtureAgent
{
  int32 AgentId = INDEX_NONE;
  float PhysicalRadiusCm = 0.0f;
  float HardSafetyGapCm = 0.0f;
  float SoftMarginCm = 0.0f;
  float Mobility = 0.0f;
  FVector Start = FVector::ZeroVector;
  FVector Predict = FVector::ZeroVector;
  FVector Soft = FVector::ZeroVector;
  FVector EnvironmentSoft = FVector::ZeroVector;
  FVector UnifiedHard = FVector::ZeroVector;
  FVector Hard = FVector::ZeroVector;
  FVector Swept = FVector::ZeroVector;
  FVector Obstacle = FVector::ZeroVector;
  FVector Quantized = FVector::ZeroVector;
  FVector FinalSafety = FVector::ZeroVector;
  FVector Applied = FVector::ZeroVector;
};

struct FCrowdDemoParticleFailureFixture
{
  bool bValid = false;
  int32 FixedStepIndex = INDEX_NONE;
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  bool bHardViolation = false;
  bool bSweptViolation = false;
  float RequiredHardDistanceCm = 0.0f;
  float FinalEndpointDistanceCm = 0.0f;
  float FinalSweptDistanceCm = 0.0f;
  uint32 CandidateHash = 0;
  uint32 AppliedStateHash = 0;
  uint32 FixtureHash = 0;
  int32 FirstFailureEnvironmentId = INDEX_NONE;
  int32 FirstFailureConstraintKind = INDEX_NONE;
  bool bHasFirstFailureContact = false;
  bool bHasFirstFailureConstraint = false;
  FCrowdDemoParticleEnvironmentContact FirstFailureContact;
  FCrowdDemoParticleHardConstraint FirstFailureConstraint;
  TArray<FCrowdDemoParticleConstraintAgent> SolveAgents;
  TArray<FCrowdDemoParticleFailureFixtureAgent> Agents;
};

class MASSAICROWDDEMO_API FCrowdDemoParticleConstraintKernel
{
public:
  static void BuildCandidatePairs(
    TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
    TConstArrayView<FVector> EndPositions,
    TArray<FCrowdDemoParticleConstraintPair>& OutPairs);

  static bool BuildEnvironmentContacts(
    TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
    TConstArrayView<FVector> Positions,
    const FCrowdDemoParticleConstraintEnvironment& Environment,
    TArray<FCrowdDemoParticleEnvironmentContact>& OutContacts);

  static void BuildUnifiedHardConstraints(
    TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
    TConstArrayView<FVector> Positions,
    TConstArrayView<FCrowdDemoParticleConstraintPair> Pairs,
    TConstArrayView<FCrowdDemoParticleEnvironmentContact> Contacts,
    TArray<FCrowdDemoParticleHardConstraint>& OutConstraints);

  static void SolveUnifiedHardClosure(
    TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
    const FCrowdDemoParticleConstraintSettings& Settings,
    TConstArrayView<FCrowdDemoParticleHardConstraint> Constraints,
    TArray<FVector>& InOutPositions,
    TArray<FCrowdDemoParticleHardDualState>& InOutDualStates,
    FCrowdDemoParticleUnifiedHardSummary& OutSummary,
    int32 StableSweepIndex = INDEX_NONE);

  static void Solve(
    TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
    const FCrowdDemoParticleConstraintEnvironment& Environment,
    const FCrowdDemoParticleConstraintSettings& Settings,
    TArray<FCrowdDemoParticleConstraintPair>& OutPairs,
    TArray<FCrowdDemoParticleConstraintResult>& OutResults,
    FCrowdDemoParticleConstraintSummary& OutSummary,
    FCrowdDemoParticleConstraintTrace* OutTrace = nullptr);

  static void BuildFailureFixture(
    TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
    TConstArrayView<FCrowdDemoParticleAppliedState> AppliedStates,
    const FCrowdDemoParticleConstraintTrace& Trace,
    int32 FixedStepIndex,
    uint32 CandidateHash,
    uint32 AppliedStateHash,
    FCrowdDemoParticleFailureFixture& OutFixture);

  static void EvaluateAppliedState(
    TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
    TConstArrayView<FCrowdDemoParticleAppliedState> AppliedStates,
    const FCrowdDemoParticleConstraintEnvironment& Environment,
    FCrowdDemoParticleConstraintSummary& OutSummary,
    uint32& OutAppliedStateHash);

  static uint32 HashAppliedRoundSimState(
    int32 RoundId,
    int32 PlanRevision,
    int32 FixedStepIndex,
    float BoundaryServerTimeSeconds,
    TConstArrayView<FCrowdDemoParticleAppliedRoundSimState> States);

  static void AdvanceSettlingTracker(
    FCrowdDemoParticleSettlingTracker& Tracker,
    float MaxActualCorrectionCm,
    float SoftErrorCmP95);
};
