#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoCapabilityProfileKernel.h"

struct FCrowdDemoSharedFlowField;

enum class ECrowdDemoTargetRegionGuidanceMode : uint8
{
  FarFlow,
  Transport,
  TerminalSettle,
  EngagedHold,
  Unrouted
};

struct FCrowdDemoTargetRegionTransportSettings
{
  FVector2f TargetLocation = FVector2f::ZeroVector;
  FVector2f TargetVelocity = FVector2f::ZeroVector;
  float TargetPhysicalRadiusCm = 100.0f;
  float TargetHardSafetyGapCm = 10.0f;
  float PhysicalRadiusCm = 42.0f;
  float HardSafetyGapCm = 10.0f;
  float SoftMarginCm = 17.0f;
  float MinimumCenterDistanceCm = 152.0f;
  float MaximumCenterDistanceCm = 850.0f;
  float InfluenceBlendWidthCm = 300.0f;
  float RadialBandWidthCm = 100.0f;
  float TransportSpeedCmps = 300.0f;
  float RadialGainPerSecond = 2.0f;
  int32 DemandRegionCount = 16;
  int32 DemandRegionPhaseOffset = 0;
  int32 PlanLifetimeSteps = 15;
  float PositionQuantumCm = 1.0f;
  float VelocityQuantumCmps = 1.0f;
  ECrowdDemoTargetDistanceResponsePolicy DistanceResponsePolicy =
    ECrowdDemoTargetDistanceResponsePolicy::StrictBand;
  float AcquireThenHoldReleaseHysteresisCm = 100.0f;
};

struct FCrowdDemoTargetRegionTransportAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  FVector2f FarFlowPreferredVelocity = FVector2f::ZeroVector;
  float MaxSpeedCmps = 0.0f;
  float PhysicalRadiusCm = 42.0f;
  float HardSafetyGapCm = 10.0f;
  float SoftMarginCm = 17.0f;
  bool bEngagedHold = false;
};

struct FCrowdDemoTargetPolarCell
{
  int32 StableCellKey = INDEX_NONE;
  int32 RadialBand = INDEX_NONE;
  int32 AngularSector = INDEX_NONE;
  int32 SectorCount = 0;
  int32 PrimaryDemandRegionKey = INDEX_NONE;
  FVector2f RelativeAnchorCm = FVector2f::ZeroVector;
  FVector2f WorldAnchorCm = FVector2f::ZeroVector;
  bool bFeasible = false;
  bool bTerminal = false;
  bool bBoundsBlocked = false;
  bool bObstacleBlocked = false;
  bool bTargetBlocked = false;
};

struct FCrowdDemoTargetPolarCellRegionLink
{
  int32 CellKey = INDEX_NONE;
  int32 RegionKey = INDEX_NONE;
  int32 AngularOverlapQ15 = 0;
  bool bTerminal = false;
};

struct FCrowdDemoTargetPolarEdge
{
  int32 FromCellKey = INDEX_NONE;
  int32 ToCellKey = INDEX_NONE;
  int32 GeometryCostCm = 0;
  int32 SoftClearancePenaltyCm = 0;
  int32 RadialDeviationPenaltyCm = 0;
  bool bCrossBand = false;
};

struct FCrowdDemoTargetPolarTopology
{
  TArray<int32> BandCellOffsets;
  TArray<int32> BandSectorCounts;
  TArray<FCrowdDemoTargetPolarCell> Cells;
  TArray<FCrowdDemoTargetPolarCellRegionLink> RegionLinks;
  TArray<FCrowdDemoTargetPolarEdge> Edges;
  uint32 FeasibleGraphHash = 2166136261u;
  // Legacy alias kept in the replicated POD during the migration. It is always equal to
  // FeasibleGraphHash and must not be used as a weaker contract.
  uint32 EnvironmentHash = 2166136261u;
  uint32 TopologyHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoTargetPolarTopologySummary
{
  int32 CellCount = 0;
  int32 FeasibleCellCount = 0;
  int32 EdgeCount = 0;
  int32 CrossBandEdgeCount = 0;
  int32 BoundsBlockedCellCount = 0;
  int32 ObstacleBlockedCellCount = 0;
  int32 TargetBlockedCellCount = 0;
  uint32 FeasibleGraphHash = 0;
  uint32 EnvironmentHash = 0;
  uint32 TopologyHash = 0;
  bool bValid = false;
};

struct FCrowdDemoTargetDemandRegion
{
  int32 StableRegionKey = INDEX_NONE;
  int32 AvailableCapacity = 0;
  int32 CurrentPopulation = 0;
  int32 DesiredPopulation = 0;
  int32 Deficit = 0;
  int32 Surplus = 0;
  bool bFeasible = false;
};

struct FCrowdDemoTargetRegionAgentDemandState
{
  int32 AgentId = INDEX_NONE;
  int32 CurrentCellKey = INDEX_NONE;
  int32 CurrentRegionKey = INDEX_NONE;
  bool bTerminal = false;
  bool bTerminalStay = false;
  bool bSupply = false;
  bool bSourceAttached = false;
  bool bEngagedHold = false;
};

struct FCrowdDemoTargetRegionDemandResult
{
  TArray<FCrowdDemoTargetDemandRegion> Regions;
  TArray<FCrowdDemoTargetRegionAgentDemandState> AgentStates;
  TArray<int32> ExternalPopulationByCell;
  TArray<int32> ExternalCongestionCostByCellCm;
  int32 FeasibleRegionCount = 0;
  int32 DesiredPopulationTotal = 0;
  int32 CurrentTerminalPopulation = 0;
  int32 TotalDeficit = 0;
  int32 TotalSurplus = 0;
  int32 SupplyAgentCount = 0;
  int32 SourceAttachmentFailureCount = 0;
  int32 ExternalPopulationAgentCount = 0;
  int32 ExternalOccupiedCellCount = 0;
  uint32 ExternalPopulationHash = 2166136261u;
  uint32 MembershipHash = 2166136261u;
  uint32 DemandHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoTargetPolarEdgeFlow
{
  int32 FromCellKey = INDEX_NONE;
  int32 ToCellKey = INDEX_NONE;
  int32 AgentQuota = 0;
  int32 ReusedQuota = 0;
};

struct FCrowdDemoTargetRegionFlowPlan
{
  int32 PlanEpoch = 0;
  int32 BuildFixedStepIndex = INDEX_NONE;
  int32 TargetRevision = INDEX_NONE;
  uint32 FeasibleGraphHash = 0;
  uint32 EnvironmentHash = 0;
  uint32 MembershipHash = 0;
  uint32 ExternalPopulationHash = 0;
  TArray<FCrowdDemoTargetPolarEdgeFlow> EdgeFlows;
  int32 RoutedAgentCount = 0;
  int32 UnroutedAgentCount = 0;
  int64 TotalPhysicalCost = 0;
  int64 ChangedQuotaUnitCount = 0;
  uint32 TransportHash = 2166136261u;
  bool bValid = false;
};

// A plan is an immutable, short-lived routing fact. Execution state records only
// how that plan is being consumed; it is not a permanent region or slot owner.
struct FCrowdDemoTargetRegionQuotaEdgeState
{
  int32 FromCellKey = INDEX_NONE;
  int32 ToCellKey = INDEX_NONE;
  int32 InitialQuota = 0;
  int32 ConsumedQuota = 0;
};

struct FCrowdDemoTargetRegionQuotaAgentClaim
{
  int32 AgentId = INDEX_NONE;
  int32 FromCellKey = INDEX_NONE;
  int32 ToCellKey = INDEX_NONE;
};

struct FCrowdDemoTargetRegionQuotaExecutionState
{
  int32 PlanEpoch = 0;
  uint32 PlanTransportHash = 0;
  TArray<FCrowdDemoTargetRegionQuotaEdgeState> Edges;
  TArray<FCrowdDemoTargetRegionQuotaAgentClaim> ActiveClaims;
  int32 CompletedTransitionCount = 0;
  uint32 ExecutionHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoTargetRegionPlanReplacementSummary
{
  int32 PreviousClaimCount = 0;
  int32 GeometryEligibleClaimCount = 0;
  int32 MigratedClaimCount = 0;
  int32 ReleasedClaimCount = 0;
  int32 CompletedAtReplacementCount = 0;
  uint32 ReplacementHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoTargetRegionPlanValidationResult
{
  bool bValid = false;
  int32 MissingEdgeCount = 0;
  int32 InfeasibleEdgeCount = 0;
  int32 InvalidCellCount = 0;
  int32 InsufficientOutgoingQuotaCellCount = 0;
  int32 FlowConservationFailureCount = 0;
  int32 UnreachableDeficitCount = 0;
  int32 FirstFailureCellKey = INDEX_NONE;
  int32 FirstFailureAgentId = INDEX_NONE;
  uint32 ValidationHash = 2166136261u;
};

struct FCrowdDemoTargetRegionGuidanceConsumption
{
  int32 FromCellKey = INDEX_NONE;
  int32 ToCellKey = INDEX_NONE;
  int32 AgentQuota = 0;
  int32 ConsumedQuota = 0;
};

struct FCrowdDemoTargetRegionGuidanceResult
{
  int32 AgentId = INDEX_NONE;
  int32 CurrentCellKey = INDEX_NONE;
  int32 NextCellKey = INDEX_NONE;
  int32 DemandRegionKey = INDEX_NONE;
  ECrowdDemoTargetRegionGuidanceMode Mode = ECrowdDemoTargetRegionGuidanceMode::Unrouted;
  FVector2f DesiredVelocity = FVector2f::ZeroVector;
};

struct FCrowdDemoTargetRegionGuidanceSummary
{
  int32 FarFlowAgentCount = 0;
  int32 TransportAgentCount = 0;
  int32 TerminalSettleAgentCount = 0;
  int32 EngagedHoldAgentCount = 0;
  int32 UnroutedAgentCount = 0;
  int32 FirstUnroutedAgentId = INDEX_NONE;
  int32 FirstUnroutedCellKey = INDEX_NONE;
  TArray<FCrowdDemoTargetRegionGuidanceConsumption> Consumption;
  uint32 ExecutionHash = 2166136261u;
  uint32 GuidanceHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoTargetEngagementDecision
{
  bool bEngagedHold = false;
  bool bAcquired = false;
  bool bReleased = false;
  bool bSuppressedRetreat = false;
};

class FCrowdDemoTargetRegionTransportKernel
{
public:
  static int32 SectorCountForRadius(float RadiusCm);

  static FVector2f ComposeTargetAdvectedFarFlowVelocity(
    const FVector2f& SharedFlowPreferredVelocity,
    const FVector2f& TargetVelocity,
    float MaxSpeedCmps);

  // Acquire-then-hold is one-sided in target-relative space: an engaged agent
  // does not retreat when the target approaches it, but it still follows
  // tangential target motion and target motion that would otherwise increase
  // their separation.
  static FVector2f ComposeEngagedHoldVelocity(
    const FVector2f& AgentLocation,
    const FVector2f& TargetLocation,
    const FVector2f& TargetVelocity,
    float MaxSpeedCmps);

  static FCrowdDemoTargetEngagementDecision ResolveTargetEngagement(
    ECrowdDemoTargetDistanceResponsePolicy Policy,
    bool bWasEngaged,
    bool bPreviousTerminalStay,
    bool bPreviousSupply,
    float CurrentDistanceCm,
    float MinimumDistanceCm,
    float MaximumDistanceCm,
    float ReleaseHysteresisCm);

  static int32 ComputeEdgeSoftClearancePenaltyCm(
    const FVector2f& Start,
    const FVector2f& End,
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig);

  static uint32 ComputeFeasibleGraphHash(
    const FCrowdDemoTargetPolarTopology& Topology);

  static void BuildTopology(
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
    FCrowdDemoTargetPolarTopology& OutTopology,
    FCrowdDemoTargetPolarTopologySummary& OutSummary);

  static void BuildDemand(
    TConstArrayView<FCrowdDemoTargetRegionTransportAgent> Agents,
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
    const FCrowdDemoSharedFlowField* SharedFlowField,
    const FCrowdDemoTargetPolarTopology& Topology,
    FCrowdDemoTargetRegionDemandResult& OutDemand,
    TConstArrayView<FCrowdDemoTargetRegionTransportAgent> ExternalAgents = {});

  static void UpdateStaticDemandPopulation(
    TConstArrayView<FCrowdDemoTargetRegionTransportAgent> Agents,
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
    const FCrowdDemoSharedFlowField* SharedFlowField,
    const FCrowdDemoTargetPolarTopology& Topology,
    FCrowdDemoTargetRegionDemandResult& InOutDemand,
    TConstArrayView<FCrowdDemoTargetRegionTransportAgent> ExternalAgents = {},
    bool bRefreshSourceAttachments = true);

  static void SolveTransport(
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan* PreviousPlan,
    int32 PlanEpoch,
    int32 FixedStepIndex,
    int32 TargetRevision,
    FCrowdDemoTargetRegionFlowPlan& OutPlan,
    TConstArrayView<FCrowdDemoTargetRegionQuotaAgentClaim> ReservedClaims = {});

  static void ReplacePlanPreservingClaims(
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan& PreviousPlan,
    const FCrowdDemoTargetRegionQuotaExecutionState& PreviousExecution,
    int32 PlanEpoch,
    int32 FixedStepIndex,
    int32 TargetRevision,
    FCrowdDemoTargetRegionFlowPlan& OutPlan,
    FCrowdDemoTargetRegionQuotaExecutionState& OutExecution,
    FCrowdDemoTargetRegionPlanReplacementSummary& OutSummary);

  static void ValidatePlanForDemand(
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan& Plan,
    int32 TargetRevision,
    FCrowdDemoTargetRegionPlanValidationResult& OutValidation);

  static void InitializeQuotaExecutionState(
    const FCrowdDemoTargetRegionFlowPlan& Plan,
    FCrowdDemoTargetRegionQuotaExecutionState& OutState);

  static void ValidateQuotaExecutionState(
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan& Plan,
    const FCrowdDemoTargetRegionQuotaExecutionState& State,
    int32 TargetRevision,
    FCrowdDemoTargetRegionPlanValidationResult& OutValidation);

  static void BuildGuidanceWithExecution(
    TConstArrayView<FCrowdDemoTargetRegionTransportAgent> Agents,
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan& Plan,
    FCrowdDemoTargetRegionQuotaExecutionState& InOutExecutionState,
    TArray<FCrowdDemoTargetRegionGuidanceResult>& OutResults,
    FCrowdDemoTargetRegionGuidanceSummary& OutSummary);

  static void BuildGuidance(
    TConstArrayView<FCrowdDemoTargetRegionTransportAgent> Agents,
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan& Plan,
    TArray<FCrowdDemoTargetRegionGuidanceResult>& OutResults,
    FCrowdDemoTargetRegionGuidanceSummary& OutSummary);
};
