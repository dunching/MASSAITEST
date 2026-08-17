#pragma once

#include "CoreMinimal.h"
#include "Mass/CrowdDemoTargetRegionTransportKernel.h"

enum class ECrowdDemoTargetRegionPlanRebuildReason : uint8
{
  None = 0,
  Lifetime = 1,
  TargetRevision = 2,
  FeasibleGraph = 3,
  Membership = 4,
  DemandSatisfied = 5,
  ExecutionInvalid = 6,
  InitialInvalid = 7
};

enum class ECrowdDemoTargetRegionLifecycleFixtureSelection : uint8
{
  None = 0,
  FinalRegionSupplyWithoutOutgoing = 1,
  FinalRegionRouteProgress = 2,
  ClaimDropFallback = 3,
  PrematureRebuildFallback = 4
};

enum ECrowdDemoTargetRegionPlanCondition : uint32
{
  CrowdDemoPlanCondition_None = 0,
  CrowdDemoPlanCondition_InitialInvalid = 1u << 0,
  CrowdDemoPlanCondition_TargetRevision = 1u << 1,
  CrowdDemoPlanCondition_FeasibleGraph = 1u << 2,
  CrowdDemoPlanCondition_Membership = 1u << 3,
  CrowdDemoPlanCondition_Lifetime = 1u << 4,
  CrowdDemoPlanCondition_DemandSatisfied = 1u << 5,
  CrowdDemoPlanCondition_ExecutionInvalid = 1u << 6
};

struct FCrowdDemoTargetRegionPlanGraphHashes
{
  uint32 CellFeasibilityHash = 2166136261u;
  uint32 EdgeSetHash = 2166136261u;
  uint32 EdgeCostHash = 2166136261u;
};

struct FCrowdDemoTargetRegionExecutionInvalidSummary
{
  int32 StateMismatchCount = 0;
  int32 ClaimOffEdgeCount = 0;
  int32 QuotaExceededCount = 0;
  int32 SupplyWithoutOutgoingQuotaCount = 0;
  int32 OtherInvalidCount = 0;
};

struct FCrowdDemoTargetRegionPlanLifecycleFixture
{
  bool bValid = false;
  int32 FixedStepIndex = INDEX_NONE;
  uint32 CapabilityProfileKey = 0;
  int32 FinalMissingRegionKey = INDEX_NONE;
  int32 ObservedDeficitRegionKey = INDEX_NONE;
  ECrowdDemoTargetRegionLifecycleFixtureSelection SelectionKind =
    ECrowdDemoTargetRegionLifecycleFixtureSelection::None;
  bool bPreviousPlanTargetsObservedRegion = false;
  bool bNewPlanTargetsObservedRegion = false;
  int32 SelectedReason = 0;
  uint32 ConditionMask = 0;
  int32 PlanAgeSteps = 0;
  int32 TargetRevision = INDEX_NONE;
  FIntPoint TargetLocationCm = FIntPoint::ZeroValue;
  FCrowdDemoTargetRegionPlanGraphHashes PreviousGraph;
  FCrowdDemoTargetRegionPlanGraphHashes CurrentGraph;
  FCrowdDemoTargetRegionExecutionInvalidSummary ExecutionInvalid;
  int32 ActiveClaimCount = 0;
  int32 GeometryEligibleClaimCount = 0;
  int32 SupplyEligibleClaimCount = 0;
  int32 NewPlanEligibleClaimCount = 0;
  int32 MigratedClaimCount = 0;
  int32 CompletedAtReplacementClaimCount = 0;
  int32 DroppedStillFeasibleClaimCount = 0;
  FCrowdDemoTargetRegionFlowPlan PreviousPlan;
  FCrowdDemoTargetRegionFlowPlan NewPlan;
  FCrowdDemoTargetRegionQuotaExecutionState PreviousExecution;
  FCrowdDemoTargetRegionQuotaExecutionState NewExecution;
  TArray<FCrowdDemoTargetRegionTransportAgent> Agents;
  FCrowdDemoTargetRegionDemandResult Demand;
  uint32 StableHash = 0;
};

struct FCrowdDemoTargetRegionPlanLifecycleSummary
{
  bool bValid = false;
  int32 SampleBoundaryCount = 0;
  int32 RebuildCount = 0;
  int32 LifetimeRebuildCount = 0;
  int32 TargetRevisionRebuildCount = 0;
  int32 FeasibleGraphRebuildCount = 0;
  int32 MembershipRebuildCount = 0;
  int32 DemandSatisfiedRebuildCount = 0;
  int32 ExecutionInvalidRebuildCount = 0;
  int32 InitialInvalidRebuildCount = 0;
  int32 CostOnlyGraphChangeCount = 0;
  int32 CellFeasibilityChangeCount = 0;
  int32 EdgeSetChangeCount = 0;
  int32 PrematureRebuildCount = 0;
  int32 ActiveClaimCount = 0;
  int32 GeometryEligibleClaimCount = 0;
  int32 SupplyEligibleClaimCount = 0;
  int32 NewPlanEligibleClaimCount = 0;
  int32 MigratedClaimCount = 0;
  int32 CompletedAtReplacementClaimCount = 0;
  int32 DroppedStillFeasibleClaimCount = 0;
  FCrowdDemoTargetRegionExecutionInvalidSummary ExecutionInvalid;
  int32 PlanAgeStepsP50 = 0;
  int32 PlanAgeStepsP95 = 0;
  int32 PlanAgeStepsMax = 0;
  bool bFixtureValid = false;
  int32 FixtureStep = INDEX_NONE;
  uint32 FixtureCohortKey = 0;
  int32 FixtureReason = 0;
  int32 FixtureSelectionKind = 0;
  int32 FixtureRegionKey = INDEX_NONE;
  uint32 FixtureHash = 0;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoTargetRegionPlanLifecycleRuntime
{
  FCrowdDemoTargetRegionPlanGraphHashes PlanGraph;
  bool bHasPlanGraph = false;
  FCrowdDemoTargetRegionPlanLifecycleSummary Summary;
  TArray<int32> RebuildPlanAges;
  FCrowdDemoTargetRegionPlanLifecycleFixture FirstPrematureFixture;
  FCrowdDemoTargetRegionPlanLifecycleFixture FirstClaimDropFixture;
  TMap<int32, FCrowdDemoTargetRegionPlanLifecycleFixture>
    LastSupplyGapFixtureByDeficitRegion;
  TMap<int32, FCrowdDemoTargetRegionPlanLifecycleFixture>
    LastRouteProgressFixtureByDeficitRegion;
};

struct FCrowdDemoTargetRegionPlanLifecycleBoundaryInput
{
  int32 FixedStepIndex = INDEX_NONE;
  uint32 CapabilityProfileKey = 0;
  int32 PlanLifetimeSteps = 15;
  int32 TargetRevision = INDEX_NONE;
  FVector2f TargetLocation = FVector2f::ZeroVector;
  int32 SelectedReason = 0;
  FCrowdDemoTargetPolarTopology Topology;
  FCrowdDemoTargetRegionDemandResult Demand;
  FCrowdDemoTargetRegionFlowPlan PreviousPlan;
  FCrowdDemoTargetRegionFlowPlan NewPlan;
  FCrowdDemoTargetRegionQuotaExecutionState PreviousExecution;
  FCrowdDemoTargetRegionQuotaExecutionState NewExecution;
  FCrowdDemoTargetRegionPlanValidationResult PreviousValidation;
  TArray<FCrowdDemoTargetRegionTransportAgent> Agents;
};

class FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel
{
public:
  static FCrowdDemoTargetRegionPlanGraphHashes ComputeGraphHashes(
    const FCrowdDemoTargetPolarTopology& Topology);
  static uint32 ComputeConditionMask(
    const FCrowdDemoTargetRegionPlanLifecycleBoundaryInput& Input);
  static ECrowdDemoTargetRegionPlanRebuildReason SelectReason(uint32 ConditionMask);
  static void RecordBoundary(
    const FCrowdDemoTargetRegionPlanLifecycleBoundaryInput& Input,
    FCrowdDemoTargetRegionPlanLifecycleRuntime& InOutRuntime);
  static FCrowdDemoTargetRegionPlanLifecycleSummary BuildAggregateSummary(
    TConstArrayView<FCrowdDemoTargetRegionPlanLifecycleRuntime> Runtimes,
    uint32 MissingCohortKey,
    int32 MissingRegionKey,
    FCrowdDemoTargetRegionPlanLifecycleFixture& OutFixture);
};
