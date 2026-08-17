#pragma once

#include "CoreMinimal.h"
#include "CrowdSharedFlowFieldKernel.h"

struct FCrowdParticleConstraintAgent
{
  int32 AgentId = INDEX_NONE;
  uint32 InteractionLayer = 0;
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

struct FCrowdParticleConstraintPair
{
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  int32 MinAgentIndex = INDEX_NONE;
  int32 MaxAgentIndex = INDEX_NONE;
};

struct FCrowdParticleConstraintEnvironment
{
  FCrowdSharedFlowFieldConfig FlowConfig;
  bool bConstrainToFlowBounds = true;
};

enum class ECrowdParticleEnvironmentContactKind : uint8
{
  ObstacleEndpoint = 0,
  ObstacleSwept = 1,
  FlowBounds = 2,
};

enum class ECrowdParticleEnvironmentFace : uint8
{
  MinX = 0,
  MaxX = 1,
  MinY = 2,
  MaxY = 3,
};

struct FCrowdParticleEnvironmentContact
{
  int32 AgentId = INDEX_NONE;
  int32 AgentIndex = INDEX_NONE;
  int32 EnvironmentId = INDEX_NONE;
  ECrowdParticleEnvironmentContactKind ContactKind =
    ECrowdParticleEnvironmentContactKind::ObstacleEndpoint;
  ECrowdParticleEnvironmentFace Face = ECrowdParticleEnvironmentFace::MinX;
  FVector ClosestPoint = FVector::ZeroVector;
  FVector CorrectionNormal = FVector::ForwardVector;
  float HardDistanceCm = 0.0f;
  float SoftDistanceCm = 0.0f;
  float SoftErrorCm = 0.0f;
  float HardDeficitCm = 0.0f;
  float SweptTime = 1.0f;
  float ConstraintThreshold = 0.0f;
};

enum class ECrowdParticleHardConstraintKind : uint8
{
  PairEndpoint = 0,
  PairSwept = 1,
  ObstacleEndpoint = 2,
  ObstacleSwept = 3,
  FlowBounds = 4,
};

struct FCrowdParticleHardConstraint
{
  ECrowdParticleHardConstraintKind Kind =
    ECrowdParticleHardConstraintKind::PairEndpoint;
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  int32 MinAgentIndex = INDEX_NONE;
  int32 MaxAgentIndex = INDEX_NONE;
  int32 EnvironmentId = INDEX_NONE;
  ECrowdParticleEnvironmentFace Face = ECrowdParticleEnvironmentFace::MinX;
  FVector Normal = FVector::ForwardVector;
  float CoefficientScale = 1.0f;
  float Threshold = 0.0f;
  float InitialDeficitCm = 0.0f;
};

struct FCrowdParticleHardDualState
{
  ECrowdParticleHardConstraintKind Kind =
    ECrowdParticleHardConstraintKind::PairEndpoint;
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  int32 EnvironmentId = INDEX_NONE;
  ECrowdParticleEnvironmentFace Face = ECrowdParticleEnvironmentFace::MinX;
  int32 NormalXQ15 = 0;
  int32 NormalYQ15 = 0;
  float Lambda = 0.0f;
};

struct FCrowdParticleUnifiedHardSummary
{
  bool bValid = true;
  int32 ConstraintCount = 0;
  int32 InfeasibleConstraintCount = 0;
  float MaxResidualCm = 0.0f;
  float MaxAppliedCorrectionCm = 0.0f;
};

struct FCrowdParticleConstraintSettings
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

struct FCrowdParticleConstraintResult
{
  int32 AgentId = INDEX_NONE;
  FVector CorrectedPosition = FVector::ZeroVector;
  FVector CorrectedVelocity = FVector::ZeroVector;
  FVector RealizedCorrection = FVector::ZeroVector;
  int32 FirstInfluencedIteration = INDEX_NONE;
  int32 CorrectedPairCount = 0;
};

struct FCrowdParticleConstraintSummary
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

struct FCrowdParticleSettlingTracker
{
  int32 StepCount = 0;
  int32 ConsecutiveSettledSampleCount = 0;
  int32 SettlingSteps = INDEX_NONE;
  float PreviousSoftErrorCmP95 = -1.0f;
};

struct FCrowdParticleAppliedState
{
  int32 AgentId = INDEX_NONE;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
};

enum class ECrowdParticleSafetyStage : uint8
{
  Input = 0,
  UnifiedHard = 1,
  Quantized = 2,
};

struct FCrowdParticleSafetyStageTrace
{
  int32 Iteration = INDEX_NONE;
  ECrowdParticleSafetyStage Stage = ECrowdParticleSafetyStage::Input;
  int32 HardPairViolationCount = 0;
  int32 SweptPairViolationCount = 0;
  int32 ObstacleViolationCount = 0;
  int32 BoundsViolationCount = 0;
  float MinimumEndpointMarginCm = TNumericLimits<float>::Max();
  float MinimumSweptMarginCm = TNumericLimits<float>::Max();
  float MaximumEnvironmentDeficitCm = 0.0f;
  uint32 PositionHash = 2166136261u;
};

struct FCrowdParticleSoftPairInfluence
{
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  FVector RequestedCorrectionA = FVector::ZeroVector;
  FVector RequestedCorrectionB = FVector::ZeroVector;
  FVector RealizedCorrectionA = FVector::ZeroVector;
  FVector RealizedCorrectionB = FVector::ZeroVector;
};

struct FCrowdParticleConstraintTrace
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
  TArray<FCrowdParticleEnvironmentContact> FinalEnvironmentContacts;
  TArray<FCrowdParticleHardConstraint> FinalHardConstraints;
  TArray<FCrowdParticleSafetyStageTrace> SafetyStages;

  // Optional diagnostic-only attribution. These arrays are populated only
  // when bCaptureRouteDiagnostic is enabled and never participate in Solve's
  // candidate hash or constraint decisions.
  TArray<FVector> PairSoftRequestedCorrections;
  TArray<FVector> PairSoftRealizedCorrections;
  TArray<FVector> EnvironmentSoftRequestedCorrections;
  TArray<FVector> EnvironmentSoftRealizedCorrections;
  TArray<FVector> UnifiedHardCorrections;
  TArray<TArray<int32>> ActiveNeighborAgentIds;
  TArray<FCrowdParticleSoftPairInfluence> SoftPairInfluences;
};

struct FCrowdParticleFailureFixtureAgent
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

struct FCrowdParticleFailureFixture
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
  FCrowdParticleEnvironmentContact FirstFailureContact;
  FCrowdParticleHardConstraint FirstFailureConstraint;
  TArray<FCrowdParticleConstraintAgent> SolveAgents;
  TArray<FCrowdParticleFailureFixtureAgent> Agents;
};

class MASSCROWDCORE_API FCrowdParticleConstraintKernel
{
public:
  static void BuildCandidatePairs(
    TConstArrayView<FCrowdParticleConstraintAgent> Agents,
    TConstArrayView<FVector> EndPositions,
    TArray<FCrowdParticleConstraintPair>& OutPairs);

  static bool BuildEnvironmentContacts(
    TConstArrayView<FCrowdParticleConstraintAgent> Agents,
    TConstArrayView<FVector> Positions,
    const FCrowdParticleConstraintEnvironment& Environment,
    TArray<FCrowdParticleEnvironmentContact>& OutContacts);

  static void BuildUnifiedHardConstraints(
    TConstArrayView<FCrowdParticleConstraintAgent> Agents,
    TConstArrayView<FVector> Positions,
    TConstArrayView<FCrowdParticleConstraintPair> Pairs,
    TConstArrayView<FCrowdParticleEnvironmentContact> Contacts,
    TArray<FCrowdParticleHardConstraint>& OutConstraints);

  static void SolveUnifiedHardClosure(
    TConstArrayView<FCrowdParticleConstraintAgent> Agents,
    const FCrowdParticleConstraintSettings& Settings,
    TConstArrayView<FCrowdParticleHardConstraint> Constraints,
    TArray<FVector>& InOutPositions,
    TArray<FCrowdParticleHardDualState>& InOutDualStates,
    FCrowdParticleUnifiedHardSummary& OutSummary,
    int32 StableSweepIndex = INDEX_NONE);

  static void Solve(
    TConstArrayView<FCrowdParticleConstraintAgent> Agents,
    const FCrowdParticleConstraintEnvironment& Environment,
    const FCrowdParticleConstraintSettings& Settings,
    TArray<FCrowdParticleConstraintPair>& OutPairs,
    TArray<FCrowdParticleConstraintResult>& OutResults,
    FCrowdParticleConstraintSummary& OutSummary,
    FCrowdParticleConstraintTrace* OutTrace = nullptr);

  static void BuildFailureFixture(
    TConstArrayView<FCrowdParticleConstraintAgent> Agents,
    TConstArrayView<FCrowdParticleAppliedState> AppliedStates,
    const FCrowdParticleConstraintTrace& Trace,
    int32 FixedStepIndex,
    uint32 CandidateHash,
    uint32 AppliedStateHash,
    FCrowdParticleFailureFixture& OutFixture);

  static void EvaluateAppliedState(
    TConstArrayView<FCrowdParticleConstraintAgent> Agents,
    TConstArrayView<FCrowdParticleAppliedState> AppliedStates,
    const FCrowdParticleConstraintEnvironment& Environment,
    FCrowdParticleConstraintSummary& OutSummary,
    uint32& OutAppliedStateHash);

  static void AdvanceSettlingTracker(
    FCrowdParticleSettlingTracker& Tracker,
    float MaxActualCorrectionCm,
    float SoftErrorCmP95);
};
