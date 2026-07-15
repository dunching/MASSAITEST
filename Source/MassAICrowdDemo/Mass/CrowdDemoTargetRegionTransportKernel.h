#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

struct FCrowdDemoSharedFlowField;

enum class ECrowdDemoTargetRegionGuidanceMode : uint8
{
  FarFlow,
  Transport,
  TerminalSettle,
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
  int32 PlanLifetimeSteps = 15;
  float PositionQuantumCm = 1.0f;
  float VelocityQuantumCmps = 1.0f;
};

struct FCrowdDemoTargetRegionTransportAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  FVector2f FarFlowPreferredVelocity = FVector2f::ZeroVector;
  float MaxSpeedCmps = 0.0f;
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
};

struct FCrowdDemoTargetRegionDemandResult
{
  TArray<FCrowdDemoTargetDemandRegion> Regions;
  TArray<FCrowdDemoTargetRegionAgentDemandState> AgentStates;
  int32 FeasibleRegionCount = 0;
  int32 DesiredPopulationTotal = 0;
  int32 CurrentTerminalPopulation = 0;
  int32 TotalDeficit = 0;
  int32 TotalSurplus = 0;
  int32 SupplyAgentCount = 0;
  int32 SourceAttachmentFailureCount = 0;
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
  TArray<FCrowdDemoTargetPolarEdgeFlow> EdgeFlows;
  int32 RoutedAgentCount = 0;
  int32 UnroutedAgentCount = 0;
  int64 TotalPhysicalCost = 0;
  int64 ChangedQuotaUnitCount = 0;
  uint32 TransportHash = 2166136261u;
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
  int32 UnroutedAgentCount = 0;
  int32 FirstUnroutedAgentId = INDEX_NONE;
  int32 FirstUnroutedCellKey = INDEX_NONE;
  TArray<FCrowdDemoTargetRegionGuidanceConsumption> Consumption;
  uint32 GuidanceHash = 2166136261u;
  bool bValid = false;
};

class FCrowdDemoTargetRegionTransportKernel
{
public:
  static int32 SectorCountForRadius(float RadiusCm);

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
    FCrowdDemoTargetRegionDemandResult& OutDemand);

  static void SolveTransport(
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan* PreviousPlan,
    int32 PlanEpoch,
    int32 FixedStepIndex,
    int32 TargetRevision,
    FCrowdDemoTargetRegionFlowPlan& OutPlan);

  static void ValidatePlanForDemand(
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan& Plan,
    int32 TargetRevision,
    FCrowdDemoTargetRegionPlanValidationResult& OutValidation);

  static void BuildGuidance(
    TConstArrayView<FCrowdDemoTargetRegionTransportAgent> Agents,
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan& Plan,
    TArray<FCrowdDemoTargetRegionGuidanceResult>& OutResults,
    FCrowdDemoTargetRegionGuidanceSummary& OutSummary);
};
