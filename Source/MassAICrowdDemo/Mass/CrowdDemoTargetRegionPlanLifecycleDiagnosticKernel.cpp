#include "Mass/CrowdDemoTargetRegionPlanLifecycleDiagnosticKernel.h"

namespace
{
constexpr uint32 FnvOffset = 2166136261u;
constexpr uint32 FnvPrime = 16777619u;

uint32 Fold(uint32 Hash, const int64 Value)
{
  Hash = (Hash ^ static_cast<uint32>(Value)) * FnvPrime;
  return (Hash ^ static_cast<uint32>(Value >> 32)) * FnvPrime;
}

int64 EdgeKey(const int32 From, const int32 To)
{
  return (static_cast<int64>(From) << 32) | static_cast<uint32>(To);
}

template <typename T>
void AddExecutionSummary(T& Target, const FCrowdDemoTargetRegionExecutionInvalidSummary& Source)
{
  Target.StateMismatchCount += Source.StateMismatchCount;
  Target.ClaimOffEdgeCount += Source.ClaimOffEdgeCount;
  Target.QuotaExceededCount += Source.QuotaExceededCount;
  Target.SupplyWithoutOutgoingQuotaCount += Source.SupplyWithoutOutgoingQuotaCount;
  Target.OtherInvalidCount += Source.OtherInvalidCount;
}

FCrowdDemoTargetRegionExecutionInvalidSummary DiagnoseExecution(
  const FCrowdDemoTargetRegionPlanLifecycleBoundaryInput& Input,
  const bool bExecutionCondition)
{
  FCrowdDemoTargetRegionExecutionInvalidSummary Result;
  const auto& State = Input.PreviousExecution;
  const auto& Plan = Input.PreviousPlan;
  if (!State.bValid || State.PlanEpoch != Plan.PlanEpoch
    || State.PlanTransportHash != Plan.TransportHash
    || State.Edges.Num() != Plan.EdgeFlows.Num())
  {
    ++Result.StateMismatchCount;
  }

  TMap<int64, const FCrowdDemoTargetRegionQuotaEdgeState*> Edges;
  for (const auto& Edge : State.Edges)
  {
    Edges.Add(EdgeKey(Edge.FromCellKey, Edge.ToCellKey), &Edge);
    if (Edge.ConsumedQuota < 0 || Edge.ConsumedQuota > Edge.InitialQuota)
      ++Result.QuotaExceededCount;
  }
  TMap<int32, const FCrowdDemoTargetRegionAgentDemandState*> DemandByAgent;
  for (const auto& Agent : Input.Demand.AgentStates) DemandByAgent.Add(Agent.AgentId, &Agent);
  TMap<int64, int32> Reserved;
  TSet<int32> ClaimedAtSource;
  for (const auto& Claim : State.ActiveClaims)
  {
    const auto* const* Agent = DemandByAgent.Find(Claim.AgentId);
    const bool bOnEdge = Agent && (*Agent)->CurrentCellKey != INDEX_NONE
      && ((*Agent)->CurrentCellKey == Claim.FromCellKey
        || (*Agent)->CurrentCellKey == Claim.ToCellKey);
    if (!Edges.Contains(EdgeKey(Claim.FromCellKey, Claim.ToCellKey)) || !bOnEdge)
      ++Result.ClaimOffEdgeCount;
    if (Agent && (*Agent)->CurrentCellKey == Claim.FromCellKey)
    {
      ++Reserved.FindOrAdd(EdgeKey(Claim.FromCellKey, Claim.ToCellKey));
      ClaimedAtSource.Add(Claim.AgentId);
    }
  }
  for (const auto& Pair : Edges)
    if (Pair.Value->ConsumedQuota + Reserved.FindRef(Pair.Key) > Pair.Value->InitialQuota)
      ++Result.QuotaExceededCount;
  for (const auto& Agent : Input.Demand.AgentStates)
  {
    if (!Agent.bSupply || Agent.bTerminalStay || ClaimedAtSource.Contains(Agent.AgentId)) continue;
    bool bHasQuota = false;
    for (const auto& Edge : State.Edges)
    {
      if (Edge.FromCellKey == Agent.CurrentCellKey
        && Edge.InitialQuota - Edge.ConsumedQuota
          - Reserved.FindRef(EdgeKey(Edge.FromCellKey, Edge.ToCellKey)) > 0)
      {
        bHasQuota = true;
        break;
      }
    }
    if (!bHasQuota) ++Result.SupplyWithoutOutgoingQuotaCount;
  }
  if (bExecutionCondition
    && Result.StateMismatchCount + Result.ClaimOffEdgeCount
      + Result.QuotaExceededCount + Result.SupplyWithoutOutgoingQuotaCount == 0)
    ++Result.OtherInvalidCount;
  return Result;
}

bool PlanRoutesSupplyToRegion(
  const FCrowdDemoTargetPolarTopology& Topology,
  const FCrowdDemoTargetRegionDemandResult& Demand,
  const FCrowdDemoTargetRegionFlowPlan& Plan,
  const int32 RegionKey)
{
  if (!Plan.bValid || RegionKey == INDEX_NONE) return false;
  TSet<int32> TerminalCells;
  for (const auto& Link : Topology.RegionLinks)
    if (Link.bTerminal && Link.RegionKey == RegionKey)
      TerminalCells.Add(Link.CellKey);
  if (TerminalCells.IsEmpty()) return false;
  TMap<int32, TArray<int32>> Adjacency;
  for (const auto& Edge : Plan.EdgeFlows)
    if (Edge.AgentQuota > 0)
      Adjacency.FindOrAdd(Edge.FromCellKey).Add(Edge.ToCellKey);
  for (auto& Pair : Adjacency) Pair.Value.Sort();
  TArray<int32> Starts;
  for (const auto& Agent : Demand.AgentStates)
    if (Agent.bSupply && Agent.CurrentCellKey != INDEX_NONE)
      Starts.AddUnique(Agent.CurrentCellKey);
  Starts.Sort();
  TSet<int32> Visited;
  TArray<int32> Queue = Starts;
  for (const int32 Cell : Starts) Visited.Add(Cell);
  for (int32 Head = 0; Head < Queue.Num(); ++Head)
  {
    const int32 Cell = Queue[Head];
    if (TerminalCells.Contains(Cell)) return true;
    if (const TArray<int32>* NextCells = Adjacency.Find(Cell))
      for (const int32 Next : *NextCells)
        if (!Visited.Contains(Next))
        {
          Visited.Add(Next);
          Queue.Add(Next);
        }
  }
  return false;
}

uint32 HashFixture(FCrowdDemoTargetRegionPlanLifecycleFixture& Fixture)
{
  uint32 Hash = Fold(FnvOffset, 1);
  Hash = Fold(Hash, Fixture.FixedStepIndex);
  Hash = Fold(Hash, Fixture.CapabilityProfileKey);
  Hash = Fold(Hash, Fixture.FinalMissingRegionKey);
  Hash = Fold(Hash, Fixture.ObservedDeficitRegionKey);
  Hash = Fold(Hash, static_cast<int32>(Fixture.SelectionKind));
  Hash = Fold(Hash, Fixture.bPreviousPlanTargetsObservedRegion ? 1 : 0);
  Hash = Fold(Hash, Fixture.bNewPlanTargetsObservedRegion ? 1 : 0);
  Hash = Fold(Hash, Fixture.SelectedReason);
  Hash = Fold(Hash, Fixture.ConditionMask);
  Hash = Fold(Hash, Fixture.TargetRevision);
  Hash = Fold(Hash, Fixture.TargetLocationCm.X);
  Hash = Fold(Hash, Fixture.TargetLocationCm.Y);
  Hash = Fold(Hash, Fixture.PreviousGraph.CellFeasibilityHash);
  Hash = Fold(Hash, Fixture.PreviousGraph.EdgeSetHash);
  Hash = Fold(Hash, Fixture.PreviousGraph.EdgeCostHash);
  Hash = Fold(Hash, Fixture.CurrentGraph.CellFeasibilityHash);
  Hash = Fold(Hash, Fixture.CurrentGraph.EdgeSetHash);
  Hash = Fold(Hash, Fixture.CurrentGraph.EdgeCostHash);
  Hash = Fold(Hash, Fixture.PreviousPlan.TransportHash);
  Hash = Fold(Hash, Fixture.NewPlan.TransportHash);
  Hash = Fold(Hash, Fixture.PreviousExecution.ExecutionHash);
  Hash = Fold(Hash, Fixture.NewExecution.ExecutionHash);
  Hash = Fold(Hash, Fixture.SupplyEligibleClaimCount);
  Hash = Fold(Hash, Fixture.MigratedClaimCount);
  Hash = Fold(Hash, Fixture.CompletedAtReplacementClaimCount);
  for (const auto& Agent : Fixture.Agents)
  {
    Hash = Fold(Hash, Agent.AgentId);
    Hash = Fold(Hash, FMath::RoundToInt(Agent.Location.X));
    Hash = Fold(Hash, FMath::RoundToInt(Agent.Location.Y));
  }
  for (const auto& Region : Fixture.Demand.Regions)
  {
    Hash = Fold(Hash, Region.StableRegionKey);
    Hash = Fold(Hash, Region.CurrentPopulation);
    Hash = Fold(Hash, Region.DesiredPopulation);
  }
  Fixture.StableHash = Hash;
  return Hash;
}

void CountReason(FCrowdDemoTargetRegionPlanLifecycleSummary& Summary, const int32 Reason)
{
  ++Summary.RebuildCount;
  switch (static_cast<ECrowdDemoTargetRegionPlanRebuildReason>(Reason))
  {
    case ECrowdDemoTargetRegionPlanRebuildReason::Lifetime: ++Summary.LifetimeRebuildCount; break;
    case ECrowdDemoTargetRegionPlanRebuildReason::TargetRevision: ++Summary.TargetRevisionRebuildCount; break;
    case ECrowdDemoTargetRegionPlanRebuildReason::FeasibleGraph: ++Summary.FeasibleGraphRebuildCount; break;
    case ECrowdDemoTargetRegionPlanRebuildReason::Membership: ++Summary.MembershipRebuildCount; break;
    case ECrowdDemoTargetRegionPlanRebuildReason::DemandSatisfied: ++Summary.DemandSatisfiedRebuildCount; break;
    case ECrowdDemoTargetRegionPlanRebuildReason::ExecutionInvalid: ++Summary.ExecutionInvalidRebuildCount; break;
    case ECrowdDemoTargetRegionPlanRebuildReason::InitialInvalid: ++Summary.InitialInvalidRebuildCount; break;
    default: break;
  }
}
}

FCrowdDemoTargetRegionPlanGraphHashes
FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeGraphHashes(
  const FCrowdDemoTargetPolarTopology& Topology)
{
  FCrowdDemoTargetRegionPlanGraphHashes Result;
  TArray<FCrowdDemoTargetPolarCell> Cells = Topology.Cells;
  Cells.Sort([](const auto& A, const auto& B) { return A.StableCellKey < B.StableCellKey; });
  for (const auto& Cell : Cells)
  {
    Result.CellFeasibilityHash = Fold(Result.CellFeasibilityHash, Cell.StableCellKey);
    Result.CellFeasibilityHash = Fold(Result.CellFeasibilityHash, Cell.bFeasible ? 1 : 0);
    Result.CellFeasibilityHash = Fold(Result.CellFeasibilityHash, Cell.bTerminal ? 1 : 0);
    Result.CellFeasibilityHash = Fold(Result.CellFeasibilityHash, Cell.PrimaryDemandRegionKey);
  }
  TArray<FCrowdDemoTargetPolarEdge> Edges = Topology.Edges;
  Edges.Sort([](const auto& A, const auto& B)
  {
    return A.FromCellKey != B.FromCellKey ? A.FromCellKey < B.FromCellKey
      : A.ToCellKey < B.ToCellKey;
  });
  for (const auto& Edge : Edges)
  {
    Result.EdgeSetHash = Fold(Result.EdgeSetHash, Edge.FromCellKey);
    Result.EdgeSetHash = Fold(Result.EdgeSetHash, Edge.ToCellKey);
    Result.EdgeCostHash = Fold(Result.EdgeCostHash, Edge.FromCellKey);
    Result.EdgeCostHash = Fold(Result.EdgeCostHash, Edge.ToCellKey);
    Result.EdgeCostHash = Fold(Result.EdgeCostHash, Edge.GeometryCostCm);
    Result.EdgeCostHash = Fold(Result.EdgeCostHash, Edge.SoftClearancePenaltyCm);
    Result.EdgeCostHash = Fold(Result.EdgeCostHash, Edge.RadialDeviationPenaltyCm);
    Result.EdgeCostHash = Fold(Result.EdgeCostHash, Edge.bCrossBand ? 1 : 0);
  }
  return Result;
}

uint32 FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeConditionMask(
  const FCrowdDemoTargetRegionPlanLifecycleBoundaryInput& Input)
{
  uint32 Mask = 0;
  if (!Input.PreviousPlan.bValid) Mask |= CrowdDemoPlanCondition_InitialInvalid;
  if (Input.PreviousPlan.bValid && Input.PreviousPlan.TargetRevision != Input.TargetRevision)
    Mask |= CrowdDemoPlanCondition_TargetRevision;
  if (Input.PreviousPlan.bValid
    && Input.PreviousPlan.FeasibleGraphHash != Input.Topology.FeasibleGraphHash)
    Mask |= CrowdDemoPlanCondition_FeasibleGraph;
  if (Input.PreviousPlan.bValid
    && Input.PreviousPlan.MembershipHash != Input.Demand.MembershipHash)
    Mask |= CrowdDemoPlanCondition_Membership;
  if (Input.PreviousPlan.bValid && Input.FixedStepIndex - Input.PreviousPlan.BuildFixedStepIndex
      >= Input.PlanLifetimeSteps)
    Mask |= CrowdDemoPlanCondition_Lifetime;
  if (Input.PreviousPlan.bValid && Input.Demand.TotalDeficit == 0
    && Input.PreviousPlan.RoutedAgentCount > 0)
    Mask |= CrowdDemoPlanCondition_DemandSatisfied;
  const bool bExecutionContractMismatch = !Input.PreviousExecution.bValid
    || Input.PreviousExecution.PlanEpoch != Input.PreviousPlan.PlanEpoch
    || Input.PreviousExecution.PlanTransportHash != Input.PreviousPlan.TransportHash
    || Input.PreviousExecution.Edges.Num() != Input.PreviousPlan.EdgeFlows.Num();
  const bool bExecutionValidationFailure =
    Input.PreviousValidation.InsufficientOutgoingQuotaCellCount > 0
    || Input.PreviousValidation.FlowConservationFailureCount > 0;
  if (Input.PreviousPlan.bValid
    && (bExecutionContractMismatch || bExecutionValidationFailure))
    Mask |= CrowdDemoPlanCondition_ExecutionInvalid;
  return Mask;
}

ECrowdDemoTargetRegionPlanRebuildReason
FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::SelectReason(const uint32 Mask)
{
  if (Mask & CrowdDemoPlanCondition_InitialInvalid) return ECrowdDemoTargetRegionPlanRebuildReason::InitialInvalid;
  if (Mask & CrowdDemoPlanCondition_TargetRevision) return ECrowdDemoTargetRegionPlanRebuildReason::TargetRevision;
  if (Mask & CrowdDemoPlanCondition_FeasibleGraph) return ECrowdDemoTargetRegionPlanRebuildReason::FeasibleGraph;
  if (Mask & CrowdDemoPlanCondition_Membership) return ECrowdDemoTargetRegionPlanRebuildReason::Membership;
  if (Mask & CrowdDemoPlanCondition_Lifetime) return ECrowdDemoTargetRegionPlanRebuildReason::Lifetime;
  if (Mask & CrowdDemoPlanCondition_DemandSatisfied) return ECrowdDemoTargetRegionPlanRebuildReason::DemandSatisfied;
  if (Mask & CrowdDemoPlanCondition_ExecutionInvalid) return ECrowdDemoTargetRegionPlanRebuildReason::ExecutionInvalid;
  return ECrowdDemoTargetRegionPlanRebuildReason::None;
}

void FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::RecordBoundary(
  const FCrowdDemoTargetRegionPlanLifecycleBoundaryInput& Source,
  FCrowdDemoTargetRegionPlanLifecycleRuntime& Runtime)
{
  FCrowdDemoTargetRegionPlanLifecycleBoundaryInput Input = Source;
  Input.Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  Input.Demand.AgentStates.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  Input.Demand.Regions.Sort([](const auto& A, const auto& B) { return A.StableRegionKey < B.StableRegionKey; });
  auto& Summary = Runtime.Summary;
  ++Summary.SampleBoundaryCount;
  const uint32 ConditionMask = ComputeConditionMask(Input);
  const int32 SelectedReason = Input.SelectedReason;
  const auto CurrentGraph = ComputeGraphHashes(Input.Topology);
  uint32 StepHash = Fold(FnvOffset, Input.FixedStepIndex);
  StepHash = Fold(StepHash, Input.CapabilityProfileKey);
  StepHash = Fold(StepHash, SelectedReason);
  StepHash = Fold(StepHash, ConditionMask);
  StepHash = Fold(StepHash, CurrentGraph.CellFeasibilityHash);
  StepHash = Fold(StepHash, CurrentGraph.EdgeSetHash);
  StepHash = Fold(StepHash, CurrentGraph.EdgeCostHash);
  StepHash = Fold(StepHash, Input.TargetRevision);
  StepHash = Fold(StepHash, FMath::RoundToInt(Input.TargetLocation.X));
  StepHash = Fold(StepHash, FMath::RoundToInt(Input.TargetLocation.Y));
  StepHash = Fold(StepHash, Input.Demand.DemandHash);
  StepHash = Fold(StepHash, Input.PreviousPlan.TransportHash);
  StepHash = Fold(StepHash, Input.NewPlan.TransportHash);
  StepHash = Fold(StepHash, Input.PreviousExecution.ExecutionHash);
  StepHash = Fold(StepHash, Input.NewExecution.ExecutionHash);

  if (SelectedReason != 0)
  {
    CountReason(Summary, SelectedReason);
    const int32 PlanAge = Input.PreviousPlan.bValid
      ? FMath::Max(0, Input.FixedStepIndex - Input.PreviousPlan.BuildFixedStepIndex) : 0;
    Runtime.RebuildPlanAges.Add(PlanAge);
    if (Input.PreviousPlan.bValid && PlanAge < Input.PlanLifetimeSteps)
      ++Summary.PrematureRebuildCount;
    if (Runtime.bHasPlanGraph)
    {
      const bool bCellChanged = Runtime.PlanGraph.CellFeasibilityHash
        != CurrentGraph.CellFeasibilityHash;
      const bool bEdgeSetChanged = Runtime.PlanGraph.EdgeSetHash != CurrentGraph.EdgeSetHash;
      const bool bCostChanged = Runtime.PlanGraph.EdgeCostHash != CurrentGraph.EdgeCostHash;
      Summary.CellFeasibilityChangeCount += bCellChanged ? 1 : 0;
      Summary.EdgeSetChangeCount += bEdgeSetChanged ? 1 : 0;
      Summary.CostOnlyGraphChangeCount += bCostChanged && !bCellChanged && !bEdgeSetChanged ? 1 : 0;
    }
    const auto Execution = DiagnoseExecution(Input,
      (ConditionMask & CrowdDemoPlanCondition_ExecutionInvalid) != 0);
    AddExecutionSummary(Summary.ExecutionInvalid, Execution);

    TSet<int64> TopologyEdges;
    for (const auto& Edge : Input.Topology.Edges)
      TopologyEdges.Add(EdgeKey(Edge.FromCellKey, Edge.ToCellKey));
    TSet<int64> NewPlanEdges;
    for (const auto& Edge : Input.NewPlan.EdgeFlows)
      if (Edge.AgentQuota > 0) NewPlanEdges.Add(EdgeKey(Edge.FromCellKey, Edge.ToCellKey));
    TMap<int32, int32> CellByAgent;
    TSet<int32> SupplyAgentIds;
    for (const auto& Agent : Input.Demand.AgentStates)
    {
      CellByAgent.Add(Agent.AgentId, Agent.CurrentCellKey);
      if (Agent.bSupply) SupplyAgentIds.Add(Agent.AgentId);
    }
    int32 Eligible = 0;
    int32 SupplyEligible = 0;
    int32 Retained = 0;
    int32 Migrated = 0;
    int32 CompletedAtReplacement = 0;
    for (const auto& Claim : Input.PreviousExecution.ActiveClaims)
    {
      const int64 Key = EdgeKey(Claim.FromCellKey, Claim.ToCellKey);
      const int32 Cell = CellByAgent.FindRef(Claim.AgentId);
      const bool bEligible = TopologyEdges.Contains(Key)
        && (Cell == Claim.FromCellKey || Cell == Claim.ToCellKey);
      const bool bSupplyEligible = TopologyEdges.Contains(Key)
        && Cell == Claim.FromCellKey && SupplyAgentIds.Contains(Claim.AgentId);
      Eligible += bEligible ? 1 : 0;
      SupplyEligible += bSupplyEligible ? 1 : 0;
      Retained += bSupplyEligible && NewPlanEdges.Contains(Key) ? 1 : 0;
      CompletedAtReplacement += bEligible && Cell == Claim.ToCellKey ? 1 : 0;
      Migrated += bEligible && Input.NewExecution.ActiveClaims.ContainsByPredicate(
        [&Claim](const auto& NewClaim)
        {
          return NewClaim.AgentId == Claim.AgentId
            && NewClaim.FromCellKey == Claim.FromCellKey
            && NewClaim.ToCellKey == Claim.ToCellKey;
        }) ? 1 : 0;
    }
    Summary.ActiveClaimCount += Input.PreviousExecution.ActiveClaims.Num();
    Summary.GeometryEligibleClaimCount += Eligible;
    Summary.SupplyEligibleClaimCount += SupplyEligible;
    Summary.NewPlanEligibleClaimCount += Retained;
    Summary.MigratedClaimCount += Migrated;
    Summary.CompletedAtReplacementClaimCount += CompletedAtReplacement;
    Summary.DroppedStillFeasibleClaimCount += FMath::Max(0, SupplyEligible - Migrated);

    FCrowdDemoTargetRegionPlanLifecycleFixture Fixture;
    Fixture.bValid = true;
    Fixture.FixedStepIndex = Input.FixedStepIndex;
    Fixture.CapabilityProfileKey = Input.CapabilityProfileKey;
    Fixture.SelectedReason = SelectedReason;
    Fixture.ConditionMask = ConditionMask;
    Fixture.PlanAgeSteps = PlanAge;
    Fixture.TargetRevision = Input.TargetRevision;
    Fixture.TargetLocationCm = FIntPoint(
      FMath::RoundToInt(Input.TargetLocation.X), FMath::RoundToInt(Input.TargetLocation.Y));
    Fixture.PreviousGraph = Runtime.PlanGraph;
    Fixture.CurrentGraph = CurrentGraph;
    Fixture.ExecutionInvalid = Execution;
    Fixture.ActiveClaimCount = Input.PreviousExecution.ActiveClaims.Num();
    Fixture.GeometryEligibleClaimCount = Eligible;
    Fixture.SupplyEligibleClaimCount = SupplyEligible;
    Fixture.NewPlanEligibleClaimCount = Retained;
    Fixture.MigratedClaimCount = Migrated;
    Fixture.CompletedAtReplacementClaimCount = CompletedAtReplacement;
    Fixture.DroppedStillFeasibleClaimCount = FMath::Max(0, SupplyEligible - Migrated);
    Fixture.PreviousPlan = Input.PreviousPlan;
    Fixture.NewPlan = Input.NewPlan;
    Fixture.PreviousExecution = Input.PreviousExecution;
    Fixture.NewExecution = Input.NewExecution;
    Fixture.Agents = Input.Agents;
    Fixture.Demand = Input.Demand;
    HashFixture(Fixture);
    if (Fixture.DroppedStillFeasibleClaimCount > 0
      && !Runtime.FirstClaimDropFixture.bValid)
    {
      Runtime.FirstClaimDropFixture = Fixture;
      Runtime.FirstClaimDropFixture.SelectionKind =
        ECrowdDemoTargetRegionLifecycleFixtureSelection::ClaimDropFallback;
      HashFixture(Runtime.FirstClaimDropFixture);
    }
    if (Input.PreviousPlan.bValid && PlanAge < Input.PlanLifetimeSteps
      && !Runtime.FirstPrematureFixture.bValid)
    {
      Runtime.FirstPrematureFixture = Fixture;
      Runtime.FirstPrematureFixture.SelectionKind =
        ECrowdDemoTargetRegionLifecycleFixtureSelection::PrematureRebuildFallback;
      HashFixture(Runtime.FirstPrematureFixture);
    }
    for (const auto& Region : Input.Demand.Regions)
    {
      if (!Region.bFeasible || Region.Deficit <= 0) continue;
      FCrowdDemoTargetRegionPlanLifecycleFixture RegionFixture = Fixture;
      RegionFixture.ObservedDeficitRegionKey = Region.StableRegionKey;
      RegionFixture.bPreviousPlanTargetsObservedRegion = PlanRoutesSupplyToRegion(
        Input.Topology, Input.Demand, Input.PreviousPlan, Region.StableRegionKey);
      RegionFixture.bNewPlanTargetsObservedRegion = PlanRoutesSupplyToRegion(
        Input.Topology, Input.Demand, Input.NewPlan, Region.StableRegionKey);
      if (Execution.SupplyWithoutOutgoingQuotaCount > 0)
      {
        RegionFixture.SelectionKind =
          ECrowdDemoTargetRegionLifecycleFixtureSelection::FinalRegionSupplyWithoutOutgoing;
        HashFixture(RegionFixture);
        Runtime.LastSupplyGapFixtureByDeficitRegion.Add(
          Region.StableRegionKey, RegionFixture);
      }
      if (RegionFixture.bPreviousPlanTargetsObservedRegion
        || RegionFixture.bNewPlanTargetsObservedRegion)
      {
        RegionFixture.SelectionKind =
          ECrowdDemoTargetRegionLifecycleFixtureSelection::FinalRegionRouteProgress;
        HashFixture(RegionFixture);
        Runtime.LastRouteProgressFixtureByDeficitRegion.Add(
          Region.StableRegionKey, RegionFixture);
      }
    }
    Runtime.PlanGraph = CurrentGraph;
    Runtime.bHasPlanGraph = true;
  }
  Summary.StableHash = Fold(Fold(Summary.StableHash, Input.FixedStepIndex), StepHash);
  Summary.bValid = Summary.RebuildCount == Summary.LifetimeRebuildCount
    + Summary.TargetRevisionRebuildCount + Summary.FeasibleGraphRebuildCount
    + Summary.MembershipRebuildCount + Summary.DemandSatisfiedRebuildCount
    + Summary.ExecutionInvalidRebuildCount + Summary.InitialInvalidRebuildCount;
}

FCrowdDemoTargetRegionPlanLifecycleSummary
FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::BuildAggregateSummary(
  const TConstArrayView<FCrowdDemoTargetRegionPlanLifecycleRuntime> Runtimes,
  const uint32 MissingCohortKey,
  const int32 MissingRegionKey,
  FCrowdDemoTargetRegionPlanLifecycleFixture& OutFixture)
{
  FCrowdDemoTargetRegionPlanLifecycleSummary Result;
  TArray<int32> Ages;
  uint32 Hash = FnvOffset;
  OutFixture = {};
  for (const auto& Runtime : Runtimes)
  {
    const auto& S = Runtime.Summary;
    Result.SampleBoundaryCount += S.SampleBoundaryCount;
    Result.RebuildCount += S.RebuildCount;
    Result.LifetimeRebuildCount += S.LifetimeRebuildCount;
    Result.TargetRevisionRebuildCount += S.TargetRevisionRebuildCount;
    Result.FeasibleGraphRebuildCount += S.FeasibleGraphRebuildCount;
    Result.MembershipRebuildCount += S.MembershipRebuildCount;
    Result.DemandSatisfiedRebuildCount += S.DemandSatisfiedRebuildCount;
    Result.ExecutionInvalidRebuildCount += S.ExecutionInvalidRebuildCount;
    Result.InitialInvalidRebuildCount += S.InitialInvalidRebuildCount;
    Result.CostOnlyGraphChangeCount += S.CostOnlyGraphChangeCount;
    Result.CellFeasibilityChangeCount += S.CellFeasibilityChangeCount;
    Result.EdgeSetChangeCount += S.EdgeSetChangeCount;
    Result.PrematureRebuildCount += S.PrematureRebuildCount;
    Result.ActiveClaimCount += S.ActiveClaimCount;
    Result.GeometryEligibleClaimCount += S.GeometryEligibleClaimCount;
    Result.SupplyEligibleClaimCount += S.SupplyEligibleClaimCount;
    Result.NewPlanEligibleClaimCount += S.NewPlanEligibleClaimCount;
    Result.MigratedClaimCount += S.MigratedClaimCount;
    Result.CompletedAtReplacementClaimCount += S.CompletedAtReplacementClaimCount;
    Result.DroppedStillFeasibleClaimCount += S.DroppedStillFeasibleClaimCount;
    AddExecutionSummary(Result.ExecutionInvalid, S.ExecutionInvalid);
    Ages.Append(Runtime.RebuildPlanAges);
    Hash = Fold(Hash, S.StableHash);
    const FCrowdDemoTargetRegionPlanLifecycleFixture* Candidate = nullptr;
    if (const auto* SupplyFixture =
      Runtime.LastSupplyGapFixtureByDeficitRegion.Find(MissingRegionKey);
      SupplyFixture && SupplyFixture->bValid
      && SupplyFixture->CapabilityProfileKey == MissingCohortKey)
      Candidate = SupplyFixture;
    else if (const auto* RouteFixture =
      Runtime.LastRouteProgressFixtureByDeficitRegion.Find(MissingRegionKey);
      RouteFixture && RouteFixture->bValid
      && RouteFixture->CapabilityProfileKey == MissingCohortKey)
      Candidate = RouteFixture;
    else if (Runtime.FirstClaimDropFixture.bValid
      && Runtime.FirstClaimDropFixture.CapabilityProfileKey == MissingCohortKey)
      Candidate = &Runtime.FirstClaimDropFixture;
    else if (Runtime.FirstPrematureFixture.bValid
      && Runtime.FirstPrematureFixture.CapabilityProfileKey == MissingCohortKey)
      Candidate = &Runtime.FirstPrematureFixture;
    if (Candidate)
    {
      const int32 CandidateKind = static_cast<int32>(Candidate->SelectionKind);
      const int32 CurrentKind = static_cast<int32>(OutFixture.SelectionKind);
      const bool bDirect = CandidateKind <= static_cast<int32>(
        ECrowdDemoTargetRegionLifecycleFixtureSelection::FinalRegionRouteProgress);
      const bool bCurrentDirect = OutFixture.bValid
        && CurrentKind <= static_cast<int32>(
          ECrowdDemoTargetRegionLifecycleFixtureSelection::FinalRegionRouteProgress);
      const bool bPrefer = !OutFixture.bValid
        || (bDirect && !bCurrentDirect)
        || (bDirect == bCurrentDirect && CandidateKind < CurrentKind)
        || (CandidateKind == CurrentKind
          && (bDirect ? Candidate->FixedStepIndex > OutFixture.FixedStepIndex
                      : Candidate->FixedStepIndex < OutFixture.FixedStepIndex));
      if (bPrefer) OutFixture = *Candidate;
    }
  }
  if (OutFixture.bValid)
  {
    OutFixture.FinalMissingRegionKey = MissingRegionKey;
    HashFixture(OutFixture);
  }
  Ages.Sort();
  if (!Ages.IsEmpty())
  {
    Result.PlanAgeStepsP50 = Ages[FMath::Clamp(FMath::CeilToInt(Ages.Num() * 0.50f) - 1, 0, Ages.Num() - 1)];
    Result.PlanAgeStepsP95 = Ages[FMath::Clamp(FMath::CeilToInt(Ages.Num() * 0.95f) - 1, 0, Ages.Num() - 1)];
    Result.PlanAgeStepsMax = Ages.Last();
  }
  Result.bFixtureValid = OutFixture.bValid;
  Result.FixtureStep = OutFixture.FixedStepIndex;
  Result.FixtureCohortKey = OutFixture.CapabilityProfileKey;
  Result.FixtureReason = OutFixture.SelectedReason;
  Result.FixtureSelectionKind = static_cast<int32>(OutFixture.SelectionKind);
  Result.FixtureRegionKey = OutFixture.ObservedDeficitRegionKey;
  Result.FixtureHash = OutFixture.StableHash;
  Result.StableHash = Hash;
  Result.bValid = Result.RebuildCount == Result.LifetimeRebuildCount
    + Result.TargetRevisionRebuildCount + Result.FeasibleGraphRebuildCount
    + Result.MembershipRebuildCount + Result.DemandSatisfiedRebuildCount
    + Result.ExecutionInvalidRebuildCount + Result.InitialInvalidRebuildCount;
  return Result;
}
