#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoPursuitPositioningKernel.h"

enum class ECrowdDemoOrcaRouteMode : uint8
{
  None,
  Yielding,
  Active
};

enum class ECrowdDemoOrcaLocalPriority : uint8
{
  Yielding = 0,
  Normal = 1,
  Committed = 2
};

struct FCrowdDemoOrcaPriorityKey
{
  uint8 PortalPriority = 0;
  ECrowdDemoOrcaLocalPriority LocalPriority = ECrowdDemoOrcaLocalPriority::Normal;
};

struct FCrowdDemoOrcaAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Position = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  FVector2f PreferredVelocity = FVector2f::ZeroVector;
  FVector2f FlowDirection = FVector2f::ZeroVector;
  FVector2f PortalDirection = FVector2f::ZeroVector;
  float RadiusCm = 42.0f;
  float MaxSpeedCmps = 800.0f;
  ECrowdDemoPortalAdmissionState AdmissionState = ECrowdDemoPortalAdmissionState::None;
  ECrowdDemoOrcaLocalPriority LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Normal;
  int16 BandId = INDEX_NONE;
  int32 IntegrationCost = MAX_int32;
  ECrowdDemoOrcaRouteMode Sf4RouteMode = ECrowdDemoOrcaRouteMode::None;
  float Sf4RouteSafetyGapCm = 0.0f;
  TArray<FVector2f> Sf4RoutePoints;
};

struct FCrowdDemoOrcaRoutePairPolicy
{
  bool bOverridesDefault = false;
  bool bIncludeConstraint = true;
  float Responsibility = 0.5f;
};

struct FCrowdDemoOrcaNeighbor
{
  int32 AgentId = INDEX_NONE;
  int32 DistanceBucket = 0;
  float DistanceSquared = 0.0f;
};

enum class ECrowdDemoOrcaConstraintKind : uint8
{
  None,
  CutoffCircle,
  LeftLeg,
  RightLeg,
  Penetration
};

enum class ECrowdDemoOrcaFeasibility : uint8
{
  NoConstraint,
  PreferredFeasible,
  LpFeasible,
  SingleConstraintOutsideSpeedCircle,
  MultiConstraintEmptyIntersection,
  QuantizationDestroyedFeasibility,
  FallbackFlowFeasible,
  FallbackPortalFeasible,
  StopFeasible,
  StopViolation,
  FormalLpFeasible,
  FormalLpQuantizedRecovered,
  FormalLpQuantizedGeometryRecovered,
  FormalLpMissedOracleRecovered,
  FormalLpMissedZeroRecovered,
  ContinuousFeasibleQuantizedEmpty,
  TrueNoFeasibleWitness
};

enum class ECrowdDemoOrcaQuantizationResult : uint8
{
  CenterFeasible,
  NeighborhoodRecovered,
  GeometryRecovered,
  NoSolution
};

struct FCrowdDemoOrcaConstraint
{
  int32 OtherAgentId = INDEX_NONE;
  FVector2f Point = FVector2f::ZeroVector;
  FVector2f Normal = FVector2f::ZeroVector;
  float Responsibility = 0.5f;
  float CombinedRadiusCm = 0.0f;
  float DistanceCm = 0.0f;
  float TimeHorizonSeconds = 0.0f;
  ECrowdDemoOrcaConstraintKind Kind = ECrowdDemoOrcaConstraintKind::None;
  int32 StableConstraintOrder = INDEX_NONE;
};

// Responsibility-neutral geometry for one oriented pair.  The coupled hard
// constraint is dot((VelocityA - VelocityB) - RelativeVelocityPoint, Normal) >= 0.
// Priority ORCA derives its per-agent point from the same Correction value.
struct FCrowdDemoOrcaCanonicalPairGeometry
{
  int32 AgentId = INDEX_NONE;
  int32 OtherAgentId = INDEX_NONE;
  FVector2f QuantizedAgentVelocity = FVector2f::ZeroVector;
  FVector2f QuantizedOtherVelocity = FVector2f::ZeroVector;
  FVector2f Correction = FVector2f::ZeroVector;
  FVector2f RelativeVelocityPoint = FVector2f::ZeroVector;
  FVector2f Normal = FVector2f::ZeroVector;
  float CombinedRadiusCm = 0.0f;
  float DistanceCm = 0.0f;
  float TimeHorizonSeconds = 0.0f;
  ECrowdDemoOrcaConstraintKind Kind = ECrowdDemoOrcaConstraintKind::None;
  bool bValid = false;
};

struct FCrowdDemoOrcaHalfPlane
{
  FVector2f Point = FVector2f::ZeroVector;
  FVector2f Normal = FVector2f::ZeroVector;
  int32 StableOrder = INDEX_NONE;
};

enum class ECrowdDemoOrcaSolveStatus : uint8
{
  PreferredFeasible,
  ExactFeasible,
  QuantizedRecovered,
  BestEffort,
  ProvenInfeasible,
  NumericalFailure
};

struct FCrowdDemoOrcaContinuousSolveInput
{
  FVector2f PreferredVelocity = FVector2f::ZeroVector;
  float MaxSpeedCmps = 0.0f;
  float BehaviorEpsilonCmps = 0.1f;
  TArray<FCrowdDemoOrcaHalfPlane> HalfPlanes;
};

struct FCrowdDemoOrcaContinuousSolveResult
{
  ECrowdDemoOrcaSolveStatus Status = ECrowdDemoOrcaSolveStatus::NumericalFailure;
  FVector2f Velocity = FVector2f::ZeroVector;
  int32 FailedConstraintIndex = INDEX_NONE;
  bool bSatisfiesAllHalfPlanes = false;
};

struct FCrowdDemoOrcaResult
{
  int32 AgentId = INDEX_NONE;
  FVector2f Velocity = FVector2f::ZeroVector;
  int32 NeighborCount = 0;
  int32 ConstraintCount = 0;
  uint8 FallbackStage = 0;
  ECrowdDemoOrcaFeasibility Feasibility = ECrowdDemoOrcaFeasibility::NoConstraint;
  ECrowdDemoOrcaFeasibility FailureReason = ECrowdDemoOrcaFeasibility::NoConstraint;
  bool bAdjusted = false;
  bool bInfeasible = false;
  bool bOutputSatisfiesConstraints = false;
  bool bStopSatisfiesConstraints = false;
  bool bNeighborhood3x3Recovered = false;
  bool bGeometryQuantizedRecovered = false;
  TArray<FCrowdDemoOrcaConstraint> Constraints;
};

struct FCrowdDemoOrcaSummary
{
  uint32 VelocityHash = 0;
  uint32 PriorityHash = 2166136261u;
  int32 PriorityEqualPairCount = 0;
  int32 PriorityAsymmetricPairCount = 0;
  int32 PriorityHighSide25Count = 0;
  int32 PriorityLowSide75Count = 0;
  int32 PriorityResponsibilitySumViolationCount = 0;
  int32 ProcessedAgentCount = 0;
  int32 AdjustedAgentCount = 0;
  int32 InfeasibleAgentCount = 0;
  int32 FallbackStopCount = 0;
  int32 StopSatisfiesConstraintCount = 0;
  int32 StopViolatesConstraintCount = 0;
  int32 WaitingInfeasibleCount = 0;
  int32 ApproachInfeasibleCount = 0;
  int32 ReservedInfeasibleCount = 0;
  int32 InsideInfeasibleCount = 0;
  int32 WaitingFallbackStopCount = 0;
  int32 ApproachFallbackStopCount = 0;
  int32 ReservedFallbackStopCount = 0;
  int32 InsideFallbackStopCount = 0;
  int32 NeighborCountMax = 0;
  int32 ConstraintCountMax = 0;
  int32 CutoffCircleConstraintCount = 0;
  int32 LeftLegConstraintCount = 0;
  int32 RightLegConstraintCount = 0;
  int32 PenetrationConstraintCount = 0;
  int32 NoConstraintCount = 0;
  int32 PreferredFeasibleCount = 0;
  int32 LpFeasibleCount = 0;
  int32 SingleConstraintOutsideSpeedCircleCount = 0;
  int32 MultiConstraintEmptyIntersectionCount = 0;
  int32 QuantizationDestroyedFeasibilityCount = 0;
  int32 FallbackFlowFeasibleCount = 0;
  int32 FallbackPortalFeasibleCount = 0;
  int32 StopFeasibleCount = 0;
  int32 StopViolationCount = 0;
  int32 FormalLpFeasibleCount = 0;
  int32 FormalLpQuantizedRecoveredCount = 0;
  int32 FormalLpQuantizedGeometryRecoveredCount = 0;
  int32 FormalLpMissedOracleRecoveredCount = 0;
  int32 FormalLpMissedZeroRecoveredCount = 0;
  int32 ContinuousFeasibleQuantizedEmptyCount = 0;
  int32 TrueNoFeasibleWitnessCount = 0;
  int32 OracleInvocationCount = 0;
  int32 OracleCalledAfterContinuousFailureCount = 0;
  int32 OracleCalledAfterQuantizationFailureCount = 0;
  int32 OracleQuantizedWitnessUsedCount = 0;
  int32 Neighborhood3x3RecoveredCount = 0;
  int32 OracleNoWitnessCount = 0;
  int32 TrueNoWitnessReachableFlowCount = 0;
  int32 TrueNoWitnessInvalidFlowCount = 0;
  int32 TrueNoWitnessGoalNearCount = 0;
  int32 TrueNoWitnessCorridorCount = 0;
  int32 ParallelBranchCount = 0;
  int32 NearParallelBranchCount = 0;
  int32 RedundantParallelCount = 0;
  int32 StricterParallelCount = 0;
  int32 TrueParallelContradictionCount = 0;
  int32 NumericalToleranceAcceptanceCount = 0;
  TStaticArray<int32, 6> ProcessedByAdmissionState = {};
  TStaticArray<int32, 6> FormalLpMissedByAdmissionState = {};
  TStaticArray<int32, 6> QuantizedEmptyByAdmissionState = {};
  TStaticArray<int32, 6> InfeasibleByAdmissionState = {};
  TArray<float> NeighborCounts;
  TArray<float> ConstraintCounts;
};

#if WITH_DEV_AUTOMATION_TESTS
enum class ECrowdDemoOrcaOracleCandidateKind : uint8
{
  Zero,
  Preferred,
  LineProjection,
  LineIntersection,
  LineCircleIntersection
};

struct FCrowdDemoOrcaFeasibilityOracleResult
{
  bool bFoundFeasibleWitness = false;
  bool bZeroVelocityFeasible = false;
  bool bFoundQuantizedWitness = false;
  int32 CandidateCount = 0;
  FVector2f WitnessVelocity = FVector2f::ZeroVector;
  FVector2f QuantizedWitnessVelocity = FVector2f::ZeroVector;
  ECrowdDemoOrcaOracleCandidateKind WitnessKind = ECrowdDemoOrcaOracleCandidateKind::Zero;
};
#endif

struct FCrowdDemoOrcaLineInterval
{
  bool bFeasible = false;
  float MinimumT = 0.0f;
  float MaximumT = 0.0f;
};

struct FCrowdDemoOrcaNumericalTolerances
{
  double ParallelAngularTolerance = 0.0;
  double ResidualToleranceCmps = 0.0;
  double ParameterTolerance = 0.0;
};

struct FCrowdDemoOrcaNumericalSummary
{
  int32 ParallelBranchCount = 0;
  int32 NearParallelBranchCount = 0;
  int32 RedundantParallelCount = 0;
  int32 StricterParallelCount = 0;
  int32 TrueParallelContradictionCount = 0;
  int32 NumericalToleranceAcceptanceCount = 0;
};

enum class ECrowdDemoSf4RouteConstraintSource : uint8
{
  Active,
  Waiting,
  Stable,
  Other
};

enum class ECrowdDemoSf4ReservationConstraintClass : uint8
{
  ActiveRouteConflict,
  ActiveRouteDisjointContained,
  ActiveRouteDisjointOutsideCorridor,
  Waiting,
  Stable,
  Other
};

struct FCrowdDemoSf4SourcedOrcaConstraint
{
  FCrowdDemoOrcaConstraint Constraint;
  ECrowdDemoSf4RouteConstraintSource Source = ECrowdDemoSf4RouteConstraintSource::Other;
};

struct FCrowdDemoSf4RouteForwardFeasibilityResult
{
  bool bContinuousFeasible = false;
  bool bQuantizedFeasible = false;
  bool bFeasibleWithoutActive = false;
  bool bFeasibleWithoutWaiting = false;
  bool bFeasibleWithoutStable = false;
  bool bFeasibleWithoutOther = false;
  FVector2f ContinuousVelocity = FVector2f::ZeroVector;
  FVector2f QuantizedVelocity = FVector2f::ZeroVector;
  int32 ConstraintCount = 0;
  int32 IrreducibleCoreConstraintCount = 0;
  int32 IrreducibleCoreOtherAgentCount = 0;
  bool bFixtureTooLarge = false;
  TArray<int32> IrreducibleCoreConstraintOrders;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoSf4ReservationOrcaFixtureAgent
{
  FCrowdDemoOrcaAgent Agent;
  ECrowdDemoPursuitPositionState PositionState = ECrowdDemoPursuitPositionState::Pursuit;
  ECrowdDemoFrontApproachPhase CurrentPhase = ECrowdDemoFrontApproachPhase::None;
  FVector2f BaselineVelocity = FVector2f::ZeroVector;
  TArray<FCrowdDemoSf4SourcedOrcaConstraint> Constraints;
};

struct FCrowdDemoSf4ClassifiedReservationConstraint
{
  int32 PrimaryAgentId = INDEX_NONE;
  int32 OtherAgentId = INDEX_NONE;
  int32 StableConstraintOrder = INDEX_NONE;
  ECrowdDemoSf4ReservationConstraintClass Classification =
    ECrowdDemoSf4ReservationConstraintClass::Other;
};

struct FCrowdDemoSf4ReservationOrcaDiagnosticSummary
{
  bool bValid = false;
  bool bFixtureTooLarge = false;
  bool bContinuousFeasible = false;
  bool bQuantizedFeasible = false;
  bool bOnlyDisjointContainedActiveRestoresFeasibility = false;
  bool bConflictActiveRestoresFeasibility = false;
  bool bOutsideCorridorActiveRestoresFeasibility = false;
  int32 PrimaryAgentId = INDEX_NONE;
  int32 FixtureAgentCount = 0;
  int32 CoreConstraintCount = 0;
  int32 ActiveRouteConflictCount = 0;
  int32 ActiveRouteDisjointContainedCount = 0;
  int32 ActiveRouteDisjointOutsideCorridorCount = 0;
  int32 WaitingCount = 0;
  int32 StableCount = 0;
  int32 OtherCount = 0;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoSf4ReservationOrcaDiagnosticFixture
{
  bool bValid = false;
  FCrowdDemoPursuitTargetFact Target;
  float SafetyGapCm = 10.0f;
  float FixedStepSeconds = 1.0f / 30.0f;
  float MinimumForwardSpeedCmps = 30.0f;
  TArray<FCrowdDemoSf4ReservationOrcaFixtureAgent> Agents;
  TArray<FCrowdDemoSf4ClassifiedReservationConstraint> CoreConstraints;
  FCrowdDemoSf4ReservationOrcaDiagnosticSummary Summary;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoSf4RouteForwardFeasibilitySummary
{
  int32 SampleCount = 0;
  int32 ContinuousFeasibleCount = 0;
  int32 QuantizedFeasibleCount = 0;
  int32 BlockedCount = 0;
  int32 FeasibleWithoutActiveCount = 0;
  int32 FeasibleWithoutWaitingCount = 0;
  int32 FeasibleWithoutStableCount = 0;
  int32 FeasibleWithoutOtherCount = 0;
  uint32 RoundHash = 2166136261u;
  uint32 MinimumFixtureHash = 0;
  int32 MinimumFixtureConstraintCount = 0;
};

struct FCrowdDemoSf4RouteForwardFeasibilityFixture
{
  bool bValid = false;
  int32 AgentId = INDEX_NONE;
  FVector2f PreferredVelocity = FVector2f::ZeroVector;
  float MaxSpeedCmps = 0.0f;
  float MinimumForwardSpeedCmps = 30.0f;
  TArray<FCrowdDemoSf4SourcedOrcaConstraint> Constraints;
  FCrowdDemoSf4RouteForwardFeasibilityResult Result;
  uint32 StableHash = 2166136261u;
};

class MASSAICROWDDEMO_API FCrowdDemoDeterministicOrcaKernel
{
public:
  static FCrowdDemoOrcaPriorityKey MakePriorityKey(const FCrowdDemoOrcaAgent& Agent);
  static int32 ComparePriorityKeys(
    const FCrowdDemoOrcaPriorityKey& A, const FCrowdDemoOrcaPriorityKey& B);
  static FCrowdDemoOrcaRoutePairPolicy EvaluateSf4RoutePairPolicy(
    const FCrowdDemoOrcaAgent& Agent,
    const FCrowdDemoOrcaAgent& Other);

  static FCrowdDemoOrcaContinuousSolveInput MakeContinuousSolveInput(
    FVector2f PreferredVelocity,
    float MaxSpeedCmps,
    TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
    float BehaviorEpsilonCmps);

  static FCrowdDemoOrcaContinuousSolveResult SolveContinuousExact(
    const FCrowdDemoOrcaContinuousSolveInput& Input,
    FCrowdDemoOrcaNumericalSummary* OutNumericalSummary = nullptr);

  static bool ValidateContinuousVelocity(
    const FCrowdDemoOrcaContinuousSolveInput& Input,
    FVector2f Velocity);
  static void BuildNeighbors(
    TConstArrayView<FCrowdDemoOrcaAgent> Agents,
    const FCrowdDemoOrcaSettings& Settings,
    TArray<TArray<FCrowdDemoOrcaNeighbor>>& OutNeighbors);

  static bool BuildPairConstraint(
    const FCrowdDemoOrcaAgent& Agent,
    const FCrowdDemoOrcaAgent& Other,
    const FCrowdDemoOrcaSettings& Settings,
    float FixedStepSeconds,
    int32 StableConstraintOrder,
    FCrowdDemoOrcaConstraint& OutConstraint);

  static bool BuildCanonicalPairGeometry(
    const FCrowdDemoOrcaAgent& Agent,
    const FCrowdDemoOrcaAgent& Other,
    const FCrowdDemoOrcaSettings& Settings,
    float FixedStepSeconds,
    FCrowdDemoOrcaCanonicalPairGeometry& OutGeometry);

  static void BuildAgentConstraints(
    const FCrowdDemoOrcaAgent& Agent,
    TConstArrayView<FCrowdDemoOrcaAgent> SortedAgents,
    TConstArrayView<FCrowdDemoOrcaNeighbor> Neighbors,
    const FCrowdDemoOrcaSettings& Settings,
    float FixedStepSeconds,
    TArray<FCrowdDemoOrcaConstraint>& OutConstraints);

  static bool SolveVelocityHalfPlanes(
    FVector2f PreferredVelocity,
    float MaxSpeedCmps,
    TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
    const FCrowdDemoOrcaSettings& Settings,
    FVector2f& OutContinuousVelocity,
    FCrowdDemoOrcaNumericalSummary* OutNumericalSummary = nullptr);

  static bool QuantizeAndValidateVelocity(
    FVector2f ContinuousVelocity,
    FVector2f PreferredVelocity,
    float MaxSpeedCmps,
    TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
    const FCrowdDemoOrcaSettings& Settings,
    FVector2f& OutVelocity);

  static ECrowdDemoOrcaQuantizationResult QuantizeAndValidateVelocityDetailed(
    FVector2f ContinuousVelocity,
    FVector2f PreferredVelocity,
    float MaxSpeedCmps,
    TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
    const FCrowdDemoOrcaSettings& Settings,
    FVector2f& OutVelocity);

  static uint8 SelectFallback(
    const FCrowdDemoOrcaAgent& Agent,
    TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
    const FCrowdDemoOrcaSettings& Settings,
    FVector2f& OutVelocity,
    ECrowdDemoOrcaFeasibility& OutFeasibility);

  static void Solve(
    TConstArrayView<FCrowdDemoOrcaAgent> Agents,
    const FCrowdDemoOrcaSettings& Settings,
    float FixedStepSeconds,
    TArray<FCrowdDemoOrcaResult>& OutResults,
    FCrowdDemoOrcaSummary& OutSummary);

  static bool SolveVelocityForConstraints(
    const FVector2f PreferredVelocity,
    float MaxSpeedCmps,
    TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
    const FCrowdDemoOrcaSettings& Settings,
    FVector2f& OutVelocity);

  static FCrowdDemoSf4RouteForwardFeasibilityResult AnalyzeSf4RouteForwardFeasibility(
    FVector2f PreferredVelocity,
    float MaxSpeedCmps,
    float MinimumForwardSpeedCmps,
    TConstArrayView<FCrowdDemoSf4SourcedOrcaConstraint> Constraints,
    const FCrowdDemoOrcaSettings& Settings);

  static bool IsSf4ReservationStepContained(
    const FCrowdDemoOrcaAgent& Agent,
    FVector2f CandidateVelocity,
    float FixedStepSeconds,
    float SafetyGapCm);

  static bool AreSf4ReservationStepsPairSafe(
    const FCrowdDemoOrcaAgent& A,
    FVector2f AVelocity,
    const FCrowdDemoOrcaAgent& B,
    FVector2f BVelocity,
    float FixedStepSeconds);

  static void AnalyzeSf4ReservationOrcaDiagnostic(
    const FCrowdDemoPursuitTargetFact& Target,
    float SafetyGapCm,
    float FixedStepSeconds,
    float MinimumForwardSpeedCmps,
    TConstArrayView<FCrowdDemoSf4ReservationOrcaFixtureAgent> Agents,
    int32 PrimaryAgentId,
    const FCrowdDemoOrcaSettings& Settings,
    FCrowdDemoSf4ReservationOrcaDiagnosticFixture& OutFixture);

#if WITH_DEV_AUTOMATION_TESTS
  static FCrowdDemoOrcaFeasibilityOracleResult FindFeasibleVelocityOracle(
    FVector2f PreferredVelocity,
    float MaxSpeedCmps,
    TConstArrayView<FCrowdDemoOrcaConstraint> Constraints,
    const FCrowdDemoOrcaSettings& Settings);
#endif

  static FCrowdDemoOrcaLineInterval ComputeLineCircleInterval(
    FVector2f LinePoint,
    FVector2f LineDirection,
    float MaxSpeedCmps,
    float EpsilonCmps);

  static FCrowdDemoOrcaNumericalTolerances ComputeNumericalTolerances(
    double RelevantVelocityScaleCmps);

  static bool ClipLineIntervalAgainstHalfPlane(
    FVector2f LinePoint,
    FVector2f LineDirection,
    const FCrowdDemoOrcaConstraint& Constraint,
    float EpsilonCmps,
    float& InOutMinimumT,
    float& InOutMaximumT,
    FCrowdDemoOrcaNumericalSummary* OutNumericalSummary = nullptr);
};
