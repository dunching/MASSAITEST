#include "Mass/CrowdDemoRoundSimProcessors.h"
#include "Mass/CrowdDemoRoundInitialStateKernel.h"
#include "Mass/CrowdDemoWorkerInputSync.h"

#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoCapabilityProfileKernel.h"
#include "Mass/CrowdDemoFacingKernel.h"
#include "Mass/CrowdDemoGuidanceComposeKernel.h"
#include "Mass/CrowdDemoRoundWorkKernel.h"
#include "Mass/CrowdDemoMassCrowdRuntimeAdapter.h"
#include "MassCrowdFacingFinalizeWork.h"
#include "MassCrowdMovementPipelineWork.h"
#include "MassCrowdParticleWork.h"
#include "MassCrowdParticlePipelineWork.h"
#include "MassCrowdRuntimeFragments.h"
#include "MassCrowdRuntimeSubsystem.h"
#include "MassCrowdSharedFlowWork.h"
#include "MassCrowdTargetRegionWork.h"
#include "MassCrowdWorkerCombatState.h"
#include "MassCrowdWorkerMovementAuthority.h"
#include "MassCrowdWorkerProjectileDomain.h"
#include "MassCrowdWorkerTargetDomain.h"
#include "MassCrowdWorkerTargetObservability.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
#include "Mass/CrowdDemoLocalPredictiveInteractionKernel.h"
#include "Mass/CrowdDemoOpenSpawnRelaxationKernel.h"
#include "Mass/CrowdDemoOpenCohortMovementKernel.h"
#include "Mass/CrowdDemoBidirectionalSwapKernel.h"
#include "Mass/CrowdDemoValidCorridorTransitKernel.h"
#include "Mass/CrowdDemoCombatStateKernel.h"
#include "Mass/CrowdDemoProjectileAdapters.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Mass/CrowdDemoWorkerCombatExtension.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "GameFramework/GameStateBase.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassEntityUtils.h"
#include "MassMovementFragments.h"
#include "EngineGlobals.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Algo/Count.h"

namespace
{
  constexpr uint32 CrowdDemoStableProviderId = 1;

  // A large count-only cap creates a spiral of death in single-process PIE:
  // the listen server and client worlds both try to repay the entire wall-time
  // debt on the same Game Thread, making the next frame even later. Preserve
  // every deterministic fixed step, but amortize catch-up over responsive
  // render frames using both a hard count and a per-world CPU budget.
  constexpr int32 MaxFixedStepsPerFrame = 4;
  constexpr double MaxFixedStepCatchupCpuMilliseconds = 16.0;
  constexpr uint32 TargetFnvPrime = 16777619u;

  uint32 FoldTargetHash(uint32 Hash, uint32 Value)
  {
    Hash ^= Value;
    Hash *= TargetFnvPrime;
    return Hash;
  }

  bool ProjectHeterogeneousWorkerTargetMetrics(
    const UCrowdDemoRoundSimPipelineSubsystem& Pipeline,
    const FCrowdWorkerResultApplyProxy& Proxy,
    TConstArrayView<FCrowdDemoRoundAgentState> FinalStates,
    FCrowdDemoParticleMetrics& Metrics)
  {
    const auto& Runtimes = Pipeline.GetCapabilityCohorts();
    FCrowdWorkerTargetObservation Observation;
    if (!FCrowdWorkerTargetObserver::Build(
        Proxy, FinalStates.Num(), Observation)
      || Observation.Cohorts.Num() != Runtimes.Num())
      return false;

    TMap<int32, const FCrowdDemoRoundAgentState*> FinalStateByAgentId;
    FinalStateByAgentId.Reserve(FinalStates.Num());
    for (const FCrowdDemoRoundAgentState& State : FinalStates)
      FinalStateByAgentId.Add(State.AgentId, &State);

    Metrics.CapabilityProfiles.Reset(Runtimes.Num());
    Metrics.TargetTransportFeasibleCellCount = 0;
    Metrics.TargetTransportEdgeCount = 0;
    Metrics.TargetTransportFeasibleRegionCount = 0;
    Metrics.TargetTransportFeasibleRegionCoverageCount = 0;
    Metrics.TargetTransportInsideEffectiveBandCount = 0;
    Metrics.TargetTransportMaximumRegionPopulation = 0;
    Metrics.TargetTransportDesiredPopulation = 0;
    Metrics.TargetTransportRoutedAgentCount = 0;
    Metrics.TargetTransportUnroutedAgentCount = 0;
    Metrics.TargetTransportPlanEpoch = 0;
    Metrics.TargetTransportTopologyHash = 2166136261u;
    Metrics.TargetTransportDemandHash = 2166136261u;
    Metrics.TargetTransportPlanHash = 2166136261u;
    Metrics.TargetTransportGuidanceHash = 2166136261u;
    Metrics.TargetTransportValidationHash = 2166136261u;

    const FVector2f TargetLocation(
      Pipeline.GetTargetFact().Location.X,
      Pipeline.GetTargetFact().Location.Y);
    const FVector2f TargetVelocity(
      Pipeline.GetTargetFact().Velocity.X,
      Pipeline.GetTargetFact().Velocity.Y);
    bool bValid = Observation.bValid;
    for (const FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime :
      Runtimes)
    {
      const FCrowdWorkerTargetCohortObservation* Cohort =
        Observation.Cohorts.FindByPredicate(
          [&Runtime](const FCrowdWorkerTargetCohortObservation& Candidate)
          {
            return Candidate.CohortKey
              == Runtime.Cohort.CapabilityProfileKey;
          });
      if (!Cohort)
      {
        bValid = false;
        continue;
      }

      FCrowdDemoCapabilityProfileMetrics& Profile =
        Metrics.CapabilityProfiles.AddDefaulted_GetRef();
      Profile.CapabilityProfileKey = Runtime.Cohort.CapabilityProfileKey;
      Profile.DemandRegionPhaseOffset = Runtime.DemandRegionPhaseOffset;
      Profile.AgentCount = Runtime.Cohort.AgentIds.Num();
      Profile.FeasibleRegionCount = Cohort->FeasibleRegionCount;
      Profile.FeasibleRegionCoverageCount =
        Cohort->FeasibleRegionCoverageCount;
      Profile.InsideBandCount = Cohort->CurrentTerminalPopulation;
      Profile.RoutedAgentCount = Cohort->RoutedAgentCount;
      Profile.UnroutedAgentCount = FMath::Max(
        Cohort->PlanUnroutedAgentCount,
        Cohort->UnroutedTargetStateCount);
      Profile.MaximumRegionPopulation =
        Cohort->MaximumRegionPopulation;
      Profile.TopologyHash = Cohort->FeasibleGraphHash;
      Profile.DemandHash = FoldTargetHash(
        Cohort->MembershipHash, Cohort->ExternalPopulationHash);
      Profile.TransportHash = Cohort->TransportHash;
      Profile.GuidanceHash = Cohort->GuidanceHash;
      Profile.ValidationHash = Cohort->ExecutionHash;

      bool bHasOutsideProgress = false;
      int32 FinalProfileStateCount = 0;
      for (const int32 AgentId : Runtime.Cohort.AgentIds)
      {
        const FCrowdDemoRoundAgentState* const* FinalState =
          FinalStateByAgentId.Find(AgentId);
        if (!FinalState)
        {
          bValid = false;
          continue;
        }
        ++FinalProfileStateCount;
        const FVector2f Location(
          (*FinalState)->Location.X, (*FinalState)->Location.Y);
        const FVector2f Velocity(
          (*FinalState)->Velocity.X, (*FinalState)->Velocity.Y);
        const FVector2f Delta = Location - TargetLocation;
        const float Distance = Delta.Size();
        float ErrorCm = 0.0f;
        float ProgressCmps = 0.0f;
        if (Distance < Runtime.Cohort.Profile.NormalizedMinimumCenterDistanceCm)
        {
          ++Profile.BelowBandCount;
          ErrorCm = Runtime.Cohort.Profile.NormalizedMinimumCenterDistanceCm
            - Distance;
          ProgressCmps = Distance > UE_SMALL_NUMBER
            ? FVector2f::DotProduct(
                Velocity - TargetVelocity, Delta / Distance)
            : 0.0f;
        }
        else if (Distance >
          Runtime.Cohort.Profile.NormalizedMaximumCenterDistanceCm)
        {
          ++Profile.AboveBandCount;
          ErrorCm = Distance
            - Runtime.Cohort.Profile.NormalizedMaximumCenterDistanceCm;
          ProgressCmps = Distance > UE_SMALL_NUMBER
            ? -FVector2f::DotProduct(
                Velocity - TargetVelocity, Delta / Distance)
            : 0.0f;
        }
        else
        {
          ++Profile.DistanceBandInsideCount;
          continue;
        }
        Profile.OutsideBandErrorCmMax = FMath::Max(
          Profile.OutsideBandErrorCmMax, ErrorCm);
        if (!bHasOutsideProgress)
        {
          Profile.OutsideBandProgressCmpsMin = ProgressCmps;
          Profile.OutsideBandProgressCmpsMax = ProgressCmps;
          bHasOutsideProgress = true;
        }
        else
        {
          Profile.OutsideBandProgressCmpsMin = FMath::Min(
            Profile.OutsideBandProgressCmpsMin, ProgressCmps);
          Profile.OutsideBandProgressCmpsMax = FMath::Max(
            Profile.OutsideBandProgressCmpsMax, ProgressCmps);
        }
      }

      bValid = bValid && Cohort->bValid
        && Cohort->TargetStateCount == Profile.AgentCount
        && FinalProfileStateCount == Profile.AgentCount
        && Cohort->CurrentTerminalPopulation <= Profile.AgentCount
        && Cohort->FeasibleRegionCoverageCount
          <= Cohort->FeasibleRegionCount;
      if (Cohort->CurrentTerminalPopulation != Profile.AgentCount
        || Cohort->FeasibleRegionCoverageCount
          != FMath::Min(Profile.AgentCount, Cohort->FeasibleRegionCount))
      {
        for (const int32 AgentId : Runtime.Cohort.AgentIds)
        {
          const FCrowdMassBoundaryAgentRecord* Boundary =
            Pipeline.GetBoundarySnapshot().Agents.FindByPredicate(
              [AgentId](const FCrowdMassBoundaryAgentRecord& Candidate)
              {
                return Candidate.Identity.AgentId == AgentId;
              });
          const FCrowdDemoRoundAgentState* const* FinalState =
            FinalStateByAgentId.Find(AgentId);
          FCrowdWorkerTargetState TargetState;
          const FCrowdWorkerDomainProxyState* TargetDomain = Boundary
            ? Proxy.FindDomain(
                Boundary->AgentFacts.StableEntityRef,
                ECrowdWorkerField::Target)
            : nullptr;
          const bool bTargetStateValid = TargetDomain
            && FCrowdWorkerTargetStateCodec::Decode(
              TargetDomain->State.Payload, TargetState);
          const FVector Location = FinalState
            ? FVector((*FinalState)->Location) : FVector::ZeroVector;
          const FVector Velocity = FinalState
            ? FVector((*FinalState)->Velocity) : FVector::ZeroVector;
          const float Distance = FVector2f(
            Location.X - TargetLocation.X,
            Location.Y - TargetLocation.Y).Size();
          UE_LOG(LogTemp, Display,
            TEXT("CrowdDemoT6WorkerTargetAcceptanceWitness profile=%u agent=%d terminal=%d/%d coverage=%d/%d location=(%.1f,%.1f) velocity=(%.1f,%.1f) distance=%.1f band=(%.1f,%.1f) target_state_valid=%d mode=%d current_cell=%d next_cell=%d demand_region=%d desired=(%.1f,%.1f) source=WorkerResultApply"),
            Cohort->CohortKey, AgentId,
            Cohort->CurrentTerminalPopulation, Profile.AgentCount,
            Cohort->FeasibleRegionCoverageCount,
            FMath::Min(Profile.AgentCount, Cohort->FeasibleRegionCount),
            Location.X, Location.Y, Velocity.X, Velocity.Y, Distance,
            Runtime.Cohort.Profile.NormalizedMinimumCenterDistanceCm,
            Runtime.Cohort.Profile.NormalizedMaximumCenterDistanceCm,
            bTargetStateValid ? 1 : 0,
            bTargetStateValid ? static_cast<int32>(TargetState.Mode)
              : INDEX_NONE,
            bTargetStateValid ? TargetState.CurrentCellKey : INDEX_NONE,
            bTargetStateValid ? TargetState.NextCellKey : INDEX_NONE,
            bTargetStateValid ? TargetState.DemandRegionKey : INDEX_NONE,
            bTargetStateValid ? TargetState.DesiredVelocity.X : 0.0f,
            bTargetStateValid ? TargetState.DesiredVelocity.Y : 0.0f);
        }
      }
      Metrics.TargetTransportFeasibleCellCount += Cohort->FeasibleCellCount;
      Metrics.TargetTransportEdgeCount += Cohort->EdgeCount;
      Metrics.TargetTransportFeasibleRegionCount +=
        Cohort->FeasibleRegionCount;
      Metrics.TargetTransportFeasibleRegionCoverageCount +=
        Cohort->FeasibleRegionCoverageCount;
      Metrics.TargetTransportInsideEffectiveBandCount +=
        Cohort->CurrentTerminalPopulation;
      Metrics.TargetTransportMaximumRegionPopulation = FMath::Max(
        Metrics.TargetTransportMaximumRegionPopulation,
        Cohort->MaximumRegionPopulation);
      Metrics.TargetTransportDesiredPopulation +=
        Cohort->DesiredPopulationTotal;
      Metrics.TargetTransportRoutedAgentCount += Cohort->RoutedAgentCount;
      Metrics.TargetTransportUnroutedAgentCount +=
        Profile.UnroutedAgentCount;
      Metrics.TargetTransportPlanEpoch = FMath::Max(
        Metrics.TargetTransportPlanEpoch, Cohort->PlanEpoch);
      const uint32 Key = Cohort->CohortKey;
      Metrics.TargetTransportTopologyHash = FoldTargetHash(
        FoldTargetHash(Metrics.TargetTransportTopologyHash, Key),
        Profile.TopologyHash);
      Metrics.TargetTransportDemandHash = FoldTargetHash(
        FoldTargetHash(Metrics.TargetTransportDemandHash, Key),
        Profile.DemandHash);
      Metrics.TargetTransportPlanHash = FoldTargetHash(
        FoldTargetHash(Metrics.TargetTransportPlanHash, Key),
        Profile.TransportHash);
      Metrics.TargetTransportGuidanceHash = FoldTargetHash(
        FoldTargetHash(Metrics.TargetTransportGuidanceHash, Key),
        Profile.GuidanceHash);
      Metrics.TargetTransportValidationHash = FoldTargetHash(
        FoldTargetHash(Metrics.TargetTransportValidationHash, Key),
        Profile.ValidationHash);
    }
    Metrics.bTargetRegionTransportValid = bValid ? 1 : 0;
    return bValid;
  }

  uint32 BuildAndOptionallyWriteTargetRegionFailureFixture(
    UWorld& World,
    UCrowdDemoRoundSimPipelineSubsystem& Pipeline,
    const FCrowdDemoTargetRegionPlanValidationResult& Validation,
    const int32 FailureKind,
    FString* OutWrittenPath = nullptr)
  {
    if (!FParse::Param(FCommandLine::Get(), TEXT("CrowdDemoTargetRegionTransportDiagnostic")))
      return 0;
    TArray<FCrowdDemoTargetRegionTransportAgent> Agents = Pipeline.GetPreparedTargetRegionAgents();
    Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
    const auto& Target = Pipeline.GetTargetFact();
    const auto& Topology = Pipeline.GetPreparedTargetRegionTopology();
    const auto& Demand = Pipeline.GetPreparedTargetRegionDemand();
    const auto& Plan = Pipeline.GetPreparedTargetRegionPlan();
    const auto& Guidance = Pipeline.GetTargetRegionGuidanceSummary();
    uint32 Hash = 2166136261u;
    auto Fold = [&](const int64 Value)
    {
      Hash = FoldTargetHash(Hash, static_cast<uint32>(Value));
      Hash = FoldTargetHash(Hash, static_cast<uint32>(Value >> 32));
    };
    Fold(Pipeline.GetCurrentFixedStepIndex()); Fold(FailureKind);
    Fold(FMath::RoundToInt(Target.Location.X)); Fold(FMath::RoundToInt(Target.Location.Y));
    Fold(FMath::RoundToInt(Target.Velocity.X)); Fold(FMath::RoundToInt(Target.Velocity.Y));
    Fold(Target.TargetRevision); Fold(Topology.FeasibleGraphHash);
    FString Json;
    Json.Reserve(196608);
    Json += FString::Printf(TEXT("{\n\"version\":1,\"fixed_step\":%d,\"failure_kind\":%d,"),
      Pipeline.GetCurrentFixedStepIndex(), FailureKind);
    Json += FString::Printf(TEXT("\"target\":{\"x\":%d,\"y\":%d,\"vx\":%d,\"vy\":%d,\"revision\":%d},"),
      FMath::RoundToInt(Target.Location.X), FMath::RoundToInt(Target.Location.Y),
      FMath::RoundToInt(Target.Velocity.X), FMath::RoundToInt(Target.Velocity.Y), Target.TargetRevision);
    Json += FString::Printf(TEXT("\"feasible_graph_hash\":%u,\"topology_hash\":%u,\"demand_hash\":%u,\"plan_hash\":%u,\"guidance_hash\":%u,"),
      Topology.FeasibleGraphHash, Topology.TopologyHash, Demand.DemandHash,
      Plan.TransportHash, Guidance.GuidanceHash);
    Json += TEXT("\"cells\":[");
    for (int32 Index = 0; Index < Topology.Cells.Num(); ++Index)
    {
      const auto& Cell = Topology.Cells[Index];
      if (Index) Json += TEXT(",");
      Json += FString::Printf(TEXT("{\"key\":%d,\"band\":%d,\"sector\":%d,\"region\":%d,\"x\":%d,\"y\":%d,\"feasible\":%d,\"terminal\":%d}"),
        Cell.StableCellKey, Cell.RadialBand, Cell.AngularSector, Cell.PrimaryDemandRegionKey,
        FMath::RoundToInt(Cell.WorldAnchorCm.X), FMath::RoundToInt(Cell.WorldAnchorCm.Y),
        Cell.bFeasible ? 1 : 0, Cell.bTerminal ? 1 : 0);
      Fold(Cell.StableCellKey); Fold(Cell.PrimaryDemandRegionKey); Fold(Cell.bFeasible); Fold(Cell.bTerminal);
    }
    Json += TEXT("],\"edges\":[");
    for (int32 Index = 0; Index < Topology.Edges.Num(); ++Index)
    {
      const auto& Edge = Topology.Edges[Index];
      if (Index) Json += TEXT(",");
      Json += FString::Printf(TEXT("{\"from\":%d,\"to\":%d,\"geometry\":%d,\"soft\":%d,\"radial\":%d,\"cross\":%d}"),
        Edge.FromCellKey, Edge.ToCellKey, Edge.GeometryCostCm, Edge.SoftClearancePenaltyCm,
        Edge.RadialDeviationPenaltyCm, Edge.bCrossBand ? 1 : 0);
      Fold(Edge.FromCellKey); Fold(Edge.ToCellKey); Fold(Edge.GeometryCostCm);
      Fold(Edge.SoftClearancePenaltyCm); Fold(Edge.RadialDeviationPenaltyCm); Fold(Edge.bCrossBand);
    }
    Json += TEXT("],\"agents\":[");
    for (int32 Index = 0; Index < Agents.Num(); ++Index)
    {
      const auto& Agent = Agents[Index];
      const FCrowdDemoTargetRegionAgentDemandState* State = nullptr;
      for (const auto& Candidate : Demand.AgentStates)
        if (Candidate.AgentId == Agent.AgentId) { State = &Candidate; break; }
      if (Index) Json += TEXT(",");
      Json += FString::Printf(TEXT("{\"id\":%d,\"x\":%d,\"y\":%d,\"cell\":%d,\"region\":%d,\"terminal\":%d,\"stay\":%d,\"supply\":%d,\"attached\":%d}"),
        Agent.AgentId, FMath::RoundToInt(Agent.Location.X), FMath::RoundToInt(Agent.Location.Y),
        State ? State->CurrentCellKey : INDEX_NONE, State ? State->CurrentRegionKey : INDEX_NONE,
        State && State->bTerminal ? 1 : 0, State && State->bTerminalStay ? 1 : 0,
        State && State->bSupply ? 1 : 0, State && State->bSourceAttached ? 1 : 0);
      Fold(Agent.AgentId); Fold(FMath::RoundToInt(Agent.Location.X)); Fold(FMath::RoundToInt(Agent.Location.Y));
      Fold(State ? State->CurrentCellKey : INDEX_NONE); Fold(State ? State->CurrentRegionKey : INDEX_NONE);
      Fold(State && State->bTerminal); Fold(State && State->bTerminalStay); Fold(State && State->bSupply);
    }
    Json += TEXT("],\"regions\":[");
    for (int32 Index = 0; Index < Demand.Regions.Num(); ++Index)
    {
      const auto& Region = Demand.Regions[Index];
      if (Index) Json += TEXT(",");
      Json += FString::Printf(TEXT("{\"key\":%d,\"current\":%d,\"desired\":%d,\"deficit\":%d,\"surplus\":%d}"),
        Region.StableRegionKey, Region.CurrentPopulation, Region.DesiredPopulation,
        Region.Deficit, Region.Surplus);
      Fold(Region.StableRegionKey); Fold(Region.CurrentPopulation); Fold(Region.DesiredPopulation);
      Fold(Region.Deficit); Fold(Region.Surplus);
    }
    Json += FString::Printf(TEXT("],\"plan\":{\"epoch\":%d,\"build_step\":%d,\"flows\":["),
      Plan.PlanEpoch, Plan.BuildFixedStepIndex);
    Fold(Plan.PlanEpoch); Fold(Plan.BuildFixedStepIndex);
    for (int32 Index = 0; Index < Plan.EdgeFlows.Num(); ++Index)
    {
      const auto& Flow = Plan.EdgeFlows[Index];
      if (Index) Json += TEXT(",");
      Json += FString::Printf(TEXT("{\"from\":%d,\"to\":%d,\"quota\":%d,\"reused\":%d}"),
        Flow.FromCellKey, Flow.ToCellKey, Flow.AgentQuota, Flow.ReusedQuota);
      Fold(Flow.FromCellKey); Fold(Flow.ToCellKey); Fold(Flow.AgentQuota); Fold(Flow.ReusedQuota);
    }
    Json += FString::Printf(TEXT("]},\"validation\":{\"valid\":%d,\"missing_edge\":%d,\"infeasible_edge\":%d,\"invalid_cell\":%d,\"insufficient_quota\":%d,\"flow_conservation\":%d,\"unreachable_deficit\":%d,\"first_cell\":%d,\"first_agent\":%d,\"hash\":%u},"),
      Validation.bValid ? 1 : 0, Validation.MissingEdgeCount, Validation.InfeasibleEdgeCount,
      Validation.InvalidCellCount, Validation.InsufficientOutgoingQuotaCellCount,
      Validation.FlowConservationFailureCount, Validation.UnreachableDeficitCount,
      Validation.FirstFailureCellKey, Validation.FirstFailureAgentId, Validation.ValidationHash);
    Fold(Validation.ValidationHash);
    Json += FString::Printf(TEXT("\"guidance\":{\"unrouted\":%d,\"first_agent\":%d,\"first_cell\":%d,\"consumption\":["),
      Guidance.UnroutedAgentCount, Guidance.FirstUnroutedAgentId, Guidance.FirstUnroutedCellKey);
    Fold(Guidance.UnroutedAgentCount); Fold(Guidance.FirstUnroutedAgentId); Fold(Guidance.FirstUnroutedCellKey);
    for (int32 Index = 0; Index < Guidance.Consumption.Num(); ++Index)
    {
      const auto& Consumption = Guidance.Consumption[Index];
      if (Index) Json += TEXT(",");
      Json += FString::Printf(TEXT("{\"from\":%d,\"to\":%d,\"quota\":%d,\"consumed\":%d}"),
        Consumption.FromCellKey, Consumption.ToCellKey,
        Consumption.AgentQuota, Consumption.ConsumedQuota);
      Fold(Consumption.FromCellKey); Fold(Consumption.ToCellKey);
      Fold(Consumption.AgentQuota); Fold(Consumption.ConsumedQuota);
    }
    Json += FString::Printf(TEXT("]},\"fixture_hash\":%u}\n"), Hash);
    if (World.GetNetMode() != NM_Client)
    {
      FString OutputPath;
      if (FParse::Value(FCommandLine::Get(),
        TEXT("CrowdDemoTargetRegionTransportDiagnosticOutput="), OutputPath)
        && !OutputPath.IsEmpty())
      {
        const bool bWritten = FFileHelper::SaveStringToFile(Json, *OutputPath,
          FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        if (bWritten && OutWrittenPath) *OutWrittenPath = OutputPath;
        if (!bWritten)
          UE_LOG(LogTemp, Error, TEXT("VIOLATION CrowdDemoTargetRegionFixtureWriteFailed path=%s"), *OutputPath);
      }
    }
    return Hash;
  }

  FCrowdDemoTargetRegionTransportSettings MakeTargetRegionTransportSettings(
    const FCrowdDemoRoundRules& Rules,
    const FCrowdDemoTargetFact& Target,
    const FCrowdDemoCapabilityProfile* CapabilityProfile = nullptr,
    const int32 DemandRegionPhaseOffset = 0)
  {
    FCrowdDemoTargetRegionTransportSettings Settings;
    Settings.TargetLocation = Target.Location;
    Settings.TargetVelocity = Target.Velocity;
    Settings.TargetPhysicalRadiusCm = CapabilityProfile
      ? CapabilityProfile->TargetPhysicalRadiusCm
      : Rules.TargetDistanceBandSettings.TargetPhysicalRadiusCm;
    Settings.TargetHardSafetyGapCm = CapabilityProfile
      ? CapabilityProfile->TargetHardSafetyGapCm
      : Rules.TargetDistanceBandSettings.TargetHardSafetyGapCm;
    Settings.PhysicalRadiusCm = CapabilityProfile
      ? CapabilityProfile->Particle.PhysicalRadiusCm
      : Rules.ParticleProfile.PhysicalRadiusCm;
    Settings.HardSafetyGapCm = CapabilityProfile
      ? CapabilityProfile->Particle.HardSafetyGapCm
      : Rules.ParticleProfile.HardSafetyGapCm;
    Settings.SoftMarginCm = CapabilityProfile
      ? CapabilityProfile->Particle.SoftMarginCm
      : Rules.ParticleProfile.SoftMarginCm;
    Settings.MinimumCenterDistanceCm = CapabilityProfile
      ? CapabilityProfile->NormalizedMinimumCenterDistanceCm
      : FMath::Max(
        Rules.TargetDistanceBandSettings.DefaultMinimumCombatCenterDistanceCm,
        Settings.TargetPhysicalRadiusCm + Settings.PhysicalRadiusCm
          + FMath::Max(Settings.TargetHardSafetyGapCm, Settings.HardSafetyGapCm));
    Settings.MaximumCenterDistanceCm = CapabilityProfile
      ? CapabilityProfile->NormalizedMaximumCenterDistanceCm
      : Rules.TargetDistanceBandSettings.DefaultMaximumCombatCenterDistanceCm;
    Settings.InfluenceBlendWidthCm = Rules.TargetDistanceBandSettings.InfluenceBlendWidthCm;
    Settings.RadialBandWidthCm = Rules.TargetRegionTransportSettings.RadialBandWidthCm;
    Settings.TransportSpeedCmps = Rules.TargetRegionTransportSettings.TransportSpeedCmps;
    Settings.RadialGainPerSecond = Rules.TargetDistanceBandSettings.RadialGainPerSecond;
    Settings.DemandRegionCount = Rules.TargetRegionTransportSettings.DemandRegionCount;
    Settings.DemandRegionPhaseOffset = DemandRegionPhaseOffset;
    Settings.PlanLifetimeSteps = Rules.TargetRegionTransportSettings.PlanLifetimeSteps;
    Settings.PositionQuantumCm = Rules.TargetDistanceBandSettings.PositionQuantumCm;
    Settings.VelocityQuantumCmps = Rules.TargetDistanceBandSettings.VelocityQuantumCmps;
    Settings.DistanceResponsePolicy = CapabilityProfile
      ? CapabilityProfile->TargetDistanceResponsePolicy
      : ECrowdDemoTargetDistanceResponsePolicy::StrictBand;
    return Settings;
  }

  bool RunTargetRegionTopologyWork(
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
    FCrowdDemoTargetPolarTopology& OutTopology,
    FCrowdDemoTargetPolarTopologySummary& OutSummary)
  {
    FCrowdMassTargetRegionTopologyInput Input;
    Input.Settings =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionSettings(Settings);
    Input.FlowConfig =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(FlowConfig);
    FCrowdMassTargetRegionTopologyOutput Output =
      FCrowdMassTargetRegionWork::BuildTopology(Input);
    OutTopology = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionTopology(
      Output.Topology);
    OutSummary =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionTopologySummary(
        Output.Summary);
    return Output.bValid;
  }

  bool RunTargetRegionDemandWork(
    const TConstArrayView<FCrowdDemoTargetRegionTransportAgent> Agents,
    const TConstArrayView<FCrowdDemoTargetRegionTransportAgent> ExternalAgents,
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
    const FCrowdSharedFlowField* SharedFlowField,
    const FCrowdDemoTargetPolarTopology& Topology,
    FCrowdDemoTargetRegionDemandResult& InOutDemand,
    const bool bUpdateStaticPopulation,
    const bool bRefreshSourceAttachments,
    FCrowdMassTargetRegionDemandInput* OutBoundaryInput = nullptr,
    const bool bExecute = true)
  {
    FCrowdMassTargetRegionDemandInput Input;
    Input.Settings =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionSettings(Settings);
    Input.FlowConfig =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(FlowConfig);
    Input.SharedFlowField = SharedFlowField;
    Input.Topology =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionTopology(Topology);
    Input.PreviousDemand =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionDemand(InOutDemand);
    Input.bUpdateStaticPopulation = bUpdateStaticPopulation;
    Input.bRefreshSourceAttachments = bRefreshSourceAttachments;
    for (const auto& Agent : Agents)
      Input.Agents.Add(
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionAgent(Agent));
    for (const auto& Agent : ExternalAgents)
      Input.ExternalAgents.Add(
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionAgent(Agent));
    if (OutBoundaryInput) *OutBoundaryInput = Input;
    if (!bExecute) return true;
    FCrowdMassTargetRegionDemandOutput Output =
      FCrowdMassTargetRegionWork::BuildDemand(Input);
    InOutDemand = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionDemand(
      Output.Demand);
    return Output.bValid;
  }

  FCrowdMassTargetRegionPlanOutput RunTargetRegionPlanWork(
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan& PreviousPlan,
    const FCrowdDemoTargetRegionQuotaExecutionState& PreviousExecution,
    const int32 FixedStepIndex,
    const int32 TargetRevision,
    const int32 PlanLifetimeSteps,
    FCrowdMassTargetRegionPlanInput* OutBoundaryInput = nullptr,
    const bool bExecute = true)
  {
    FCrowdMassTargetRegionPlanInput Input;
    Input.Topology =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionTopology(Topology);
    Input.Demand =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionDemand(Demand);
    Input.PreviousPlan =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionPlan(PreviousPlan);
    Input.PreviousExecution =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionExecution(
        PreviousExecution);
    Input.FixedStepIndex = FixedStepIndex;
    Input.TargetRevision = TargetRevision;
    Input.PlanLifetimeSteps = PlanLifetimeSteps;
    if (OutBoundaryInput) *OutBoundaryInput = Input;
    if (!bExecute) return {};
    return FCrowdMassTargetRegionWork::SolvePlan(Input);
  }

  FCrowdDemoTargetRegionPlanValidationResult RunTargetRegionValidationWork(
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan& Plan,
    const FCrowdDemoTargetRegionQuotaExecutionState& Execution,
    const int32 TargetRevision)
  {
    FCrowdMassTargetRegionPlanInput Input;
    Input.Topology =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionTopology(Topology);
    Input.Demand =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionDemand(Demand);
    Input.PreviousPlan =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionPlan(Plan);
    Input.PreviousExecution =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionExecution(Execution);
    Input.TargetRevision = TargetRevision;
    return FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionValidation(
      FCrowdMassTargetRegionWork::ValidateExecution(Input));
  }

  FCrowdMassTargetRegionGuidanceOutput RunTargetRegionGuidanceWork(
    const TConstArrayView<FCrowdDemoTargetRegionTransportAgent> Agents,
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan& Plan,
    const FCrowdDemoTargetRegionQuotaExecutionState& Execution,
    FCrowdMassTargetRegionGuidanceInput* OutBoundaryInput = nullptr,
    const bool bExecute = true)
  {
    FCrowdMassTargetRegionGuidanceInput Input;
    Input.Settings =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionSettings(Settings);
    Input.Topology =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionTopology(Topology);
    Input.Demand =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionDemand(Demand);
    Input.Plan =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionPlan(Plan);
    Input.Execution =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionExecution(Execution);
    for (const auto& Agent : Agents)
      Input.Agents.Add(
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionAgent(Agent));
    if (OutBoundaryInput) *OutBoundaryInput = Input;
    if (!bExecute) return {};
    return FCrowdMassTargetRegionWork::BuildGuidance(Input);
  }

  bool IsRoundFlowScenario(const ECrowdDemoScenario Scenario)
  {
    return Scenario == ECrowdDemoScenario::SimRoundObstacle
      || Scenario == ECrowdDemoScenario::SimRoundSoftPressure;
  }

  FVector QuantizeSf2State(const FVector& Value)
  {
    return FVector(
      FMath::RoundToDouble(Value.X),
      FMath::RoundToDouble(Value.Y),
      FMath::RoundToDouble(Value.Z));
  }

  float GetRoundPipelineServerTime(UWorld& World)
  {
    const AGameStateBase* GameState = World.GetGameState();
    return World.GetNetMode() == NM_Client && GameState
      ? GameState->GetServerWorldTimeSeconds()
      : World.GetTimeSeconds();
  }
  void SortAgentStates(TArray<FCrowdDemoRoundAgentState>& States)
  {
    States.Sort([](const FCrowdDemoRoundAgentState& A, const FCrowdDemoRoundAgentState& B)
    {
      return A.AgentId < B.AgentId;
    });
  }

  FCrowdDemoCombatNetState MakeCombatNetState(
    const FCrowdDemoMassStatsFragment& Stats,
    const FCrowdDemoBusinessStateFragment& Business,
    const FCrowdDemoRangedAttackFragment& Attack,
    const FCrowdDemoReactiveMotionFragment& Reactive,
    const FCrowdDemoHitFlashFragment& HitFlash,
    const FCrowdDemoMassVisualFragment& Visual)
  {
    FCrowdDemoCombatNetState Result;
    Result.Health = Stats.Health;
    Result.MaxHealth = Stats.MaxHealth;
    Result.LifecycleState = Stats.LifecycleState;
    Result.bAlive = Stats.bAlive ? 1 : 0;
    Result.BusinessState = Business.State;
    Result.BusinessStateRevision = Business.StateRevision;
    Result.BusinessStateEnterFixedStep = Business.StateEnterFixedStep;
    Result.TargetAgentId = Business.TargetAgentId;
    Result.TargetLifecycleSerial = Business.TargetLifecycleSerial;
    Result.LastConsumedHitEventId = Business.LastConsumedHitEventId;
    Result.AttackPhase = Attack.Phase;
    Result.AttackPhaseEnterFixedStep = Attack.PhaseEnterFixedStep;
    Result.CooldownEndFixedStep = Attack.CooldownEndFixedStep;
    Result.LockedTargetAgentId = Attack.LockedTargetAgentId;
    Result.LockedTargetLifecycleSerial = Attack.LockedTargetLifecycleSerial;
    Result.LockedTargetLocation = FVector_NetQuantize10(Attack.LockedTargetLocation);
    Result.FireSequence = Attack.FireSequence;
    Result.bFireRequestIssued = Attack.bFireRequestIssued ? 1 : 0;
    Result.ReactiveMode = Reactive.Mode;
    Result.HorizontalReactiveVelocity = FVector_NetQuantize10(Reactive.HorizontalVelocity);
    Result.VerticalReactiveVelocityCmps = Reactive.VerticalVelocityCmps;
    Result.ReactiveStartFixedStep = Reactive.StartFixedStep;
    Result.ReactiveEndFixedStep = Reactive.EndFixedStep;
    Result.ReactiveRevision = Reactive.ReactiveRevision;
    Result.RestoreBusinessState = Reactive.RestoreBusinessState;
    Result.ApexCount = Reactive.ApexCount;
    Result.LandingCount = Reactive.LandingCount;
    Result.HitFlashRevision = HitFlash.FlashRevision;
    Result.HitFlashStartServerTimeSeconds = HitFlash.StartServerTimeSeconds;
    Result.HitFlashDurationSeconds = HitFlash.DurationSeconds;
    Result.HitFlashProfileKey = HitFlash.ProfileKey;
    Result.HitFlashPeakIntensity = HitFlash.PeakIntensity;
    Result.VisualState = Visual.VisualState;
    Result.VisualRevision = Visual.VisualRevision;
    Result.VisualStateStartServerTimeSeconds = Visual.StateStartServerTimeSeconds;
    Result.VisualPhaseSeed = Visual.PhaseSeed;
    return Result;
  }

  void ApplyCombatNetState(
    const FCrowdDemoCombatNetState& Source,
    FCrowdDemoMassStatsFragment& Stats,
    FCrowdDemoBusinessStateFragment& Business,
    FCrowdDemoRangedAttackFragment& Attack,
    FCrowdDemoReactiveMotionFragment& Reactive,
    FCrowdDemoHitFlashFragment& HitFlash,
    FCrowdDemoMassVisualFragment& Visual)
  {
    Stats.Health = Source.Health;
    Stats.MaxHealth = Source.MaxHealth;
    Stats.LifecycleState = Source.LifecycleState;
    Stats.bAlive = Source.bAlive != 0;
    Business.State = Source.BusinessState;
    Business.StateRevision = Source.BusinessStateRevision;
    Business.StateEnterFixedStep = Source.BusinessStateEnterFixedStep;
    Business.TargetAgentId = Source.TargetAgentId;
    Business.TargetLifecycleSerial = Source.TargetLifecycleSerial;
    Business.LastConsumedHitEventId = Source.LastConsumedHitEventId;
    Attack.Phase = Source.AttackPhase;
    Attack.PhaseEnterFixedStep = Source.AttackPhaseEnterFixedStep;
    Attack.CooldownEndFixedStep = Source.CooldownEndFixedStep;
    Attack.LockedTargetAgentId = Source.LockedTargetAgentId;
    Attack.LockedTargetLifecycleSerial = Source.LockedTargetLifecycleSerial;
    Attack.LockedTargetLocation = FVector(Source.LockedTargetLocation);
    Attack.FireSequence = Source.FireSequence;
    Attack.bFireRequestIssued = Source.bFireRequestIssued != 0;
    Reactive.Mode = Source.ReactiveMode;
    Reactive.HorizontalVelocity = FVector(Source.HorizontalReactiveVelocity);
    Reactive.VerticalVelocityCmps = Source.VerticalReactiveVelocityCmps;
    Reactive.StartFixedStep = Source.ReactiveStartFixedStep;
    Reactive.EndFixedStep = Source.ReactiveEndFixedStep;
    Reactive.ReactiveRevision = Source.ReactiveRevision;
    Reactive.RestoreBusinessState = Source.RestoreBusinessState;
    Reactive.ApexCount = Source.ApexCount;
    Reactive.LandingCount = Source.LandingCount;
    HitFlash.FlashRevision = Source.HitFlashRevision;
    HitFlash.StartServerTimeSeconds = Source.HitFlashStartServerTimeSeconds;
    HitFlash.DurationSeconds = Source.HitFlashDurationSeconds;
    HitFlash.ProfileKey = Source.HitFlashProfileKey;
    HitFlash.PeakIntensity = Source.HitFlashPeakIntensity;
    Visual.VisualState = Source.VisualState;
    Visual.VisualRevision = Source.VisualRevision;
    Visual.StateStartServerTimeSeconds = Source.VisualStateStartServerTimeSeconds;
    Visual.PhaseSeed = Source.VisualPhaseSeed;
  }

  FCrowdDemoCombatAgentState MakeCombatAgentState(
    const FCrowdDemoMassIdentityFragment& Identity,
    const FCrowdDemoMassStatsFragment& Stats,
    const FCrowdDemoBusinessStateFragment& Business,
    const FCrowdDemoRangedAttackFragment& Attack,
    const FCrowdDemoReactiveMotionFragment& Reactive,
    const FCrowdDemoHitFlashFragment& HitFlash,
    const FCrowdDemoMassVisualFragment& Visual)
  {
    FCrowdDemoCombatAgentState Result;
    Result.AgentId = Identity.Id;
    Result.LifecycleSerial = Identity.LifecycleSerial;
    const FCrowdDemoCombatNetState Net = MakeCombatNetState(
      Stats, Business, Attack, Reactive, HitFlash, Visual);
    Result.Health = Net.Health;
    Result.MaxHealth = Net.MaxHealth;
    Result.LifecycleState = Net.LifecycleState;
    Result.bAlive = Net.bAlive != 0;
    Result.BusinessState = Net.BusinessState;
    Result.BusinessStateRevision = Net.BusinessStateRevision;
    Result.BusinessStateEnterFixedStep = Net.BusinessStateEnterFixedStep;
    Result.TargetAgentId = Net.TargetAgentId;
    Result.TargetLifecycleSerial = Net.TargetLifecycleSerial;
    Result.AttackPhase = Net.AttackPhase;
    Result.AttackPhaseEnterFixedStep = Net.AttackPhaseEnterFixedStep;
    Result.CooldownEndFixedStep = Net.CooldownEndFixedStep;
    Result.LockedTargetAgentId = Net.LockedTargetAgentId;
    Result.LockedTargetLifecycleSerial = Net.LockedTargetLifecycleSerial;
    Result.LockedTargetLocation = FVector(Net.LockedTargetLocation);
    Result.FireSequence = Net.FireSequence;
    Result.bFireRequestIssued = Net.bFireRequestIssued != 0;
    Result.ReactiveMode = Net.ReactiveMode;
    Result.HorizontalReactiveVelocity = FVector(Net.HorizontalReactiveVelocity);
    Result.VerticalReactiveVelocityCmps = Net.VerticalReactiveVelocityCmps;
    Result.ReactiveStartFixedStep = Net.ReactiveStartFixedStep;
    Result.ReactiveEndFixedStep = Net.ReactiveEndFixedStep;
    Result.ReactiveRevision = Net.ReactiveRevision;
    Result.RestoreBusinessState = Net.RestoreBusinessState;
    Result.ApexCount = Net.ApexCount;
    Result.LandingCount = Net.LandingCount;
    Result.HitFlashRevision = Net.HitFlashRevision;
    Result.HitFlashStartServerTimeSeconds = Net.HitFlashStartServerTimeSeconds;
    Result.HitFlashDurationSeconds = Net.HitFlashDurationSeconds;
    Result.HitFlashProfileKey = Net.HitFlashProfileKey;
    Result.HitFlashPeakIntensity = Net.HitFlashPeakIntensity;
    Result.LastConsumedHitEventId = Net.LastConsumedHitEventId;
    Result.VisualState = Net.VisualState;
    Result.VisualRevision = Net.VisualRevision;
    Result.VisualStateStartServerTimeSeconds = Net.VisualStateStartServerTimeSeconds;
    Result.VisualPhaseSeed = Net.VisualPhaseSeed;
    return Result;
  }

  void ApplyCombatAgentState(
    const FCrowdDemoCombatAgentState& Source,
    FCrowdDemoMassStatsFragment& Stats,
    FCrowdDemoBusinessStateFragment& Business,
    FCrowdDemoRangedAttackFragment& Attack,
    FCrowdDemoReactiveMotionFragment& Reactive,
    FCrowdDemoHitFlashFragment& HitFlash,
    FCrowdDemoMassVisualFragment& Visual)
  {
    FCrowdDemoCombatNetState Net;
    Net.Health = Source.Health;
    Net.MaxHealth = Source.MaxHealth;
    Net.LifecycleState = Source.LifecycleState;
    Net.bAlive = Source.bAlive ? 1 : 0;
    Net.BusinessState = Source.BusinessState;
    Net.BusinessStateRevision = Source.BusinessStateRevision;
    Net.BusinessStateEnterFixedStep = Source.BusinessStateEnterFixedStep;
    Net.TargetAgentId = Source.TargetAgentId;
    Net.TargetLifecycleSerial = Source.TargetLifecycleSerial;
    Net.AttackPhase = Source.AttackPhase;
    Net.AttackPhaseEnterFixedStep = Source.AttackPhaseEnterFixedStep;
    Net.CooldownEndFixedStep = Source.CooldownEndFixedStep;
    Net.LockedTargetAgentId = Source.LockedTargetAgentId;
    Net.LockedTargetLifecycleSerial = Source.LockedTargetLifecycleSerial;
    Net.LockedTargetLocation = FVector_NetQuantize10(Source.LockedTargetLocation);
    Net.FireSequence = Source.FireSequence;
    Net.bFireRequestIssued = Source.bFireRequestIssued ? 1 : 0;
    Net.ReactiveMode = Source.ReactiveMode;
    Net.HorizontalReactiveVelocity = FVector_NetQuantize10(Source.HorizontalReactiveVelocity);
    Net.VerticalReactiveVelocityCmps = Source.VerticalReactiveVelocityCmps;
    Net.ReactiveStartFixedStep = Source.ReactiveStartFixedStep;
    Net.ReactiveEndFixedStep = Source.ReactiveEndFixedStep;
    Net.ReactiveRevision = Source.ReactiveRevision;
    Net.RestoreBusinessState = Source.RestoreBusinessState;
    Net.ApexCount = Source.ApexCount;
    Net.LandingCount = Source.LandingCount;
    Net.HitFlashRevision = Source.HitFlashRevision;
    Net.HitFlashStartServerTimeSeconds = Source.HitFlashStartServerTimeSeconds;
    Net.HitFlashDurationSeconds = Source.HitFlashDurationSeconds;
    Net.HitFlashProfileKey = Source.HitFlashProfileKey;
    Net.HitFlashPeakIntensity = Source.HitFlashPeakIntensity;
    Net.LastConsumedHitEventId = Source.LastConsumedHitEventId;
    Net.VisualState = Source.VisualState;
    Net.VisualRevision = Source.VisualRevision;
    Net.VisualStateStartServerTimeSeconds = Source.VisualStateStartServerTimeSeconds;
    Net.VisualPhaseSeed = Source.VisualPhaseSeed;
    ApplyCombatNetState(Net, Stats, Business, Attack, Reactive, HitFlash, Visual);
  }

  FCrowdDemoRoundBoundaryBusinessFact BuildFinalBusinessFactFromState(
    const UCrowdDemoRoundSimPipelineSubsystem& Pipeline,
    const FCrowdDemoRoundBoundaryBusinessFact& Base,
    const FCrowdMovementOutput& Movement,
    const FCrowdDemoCombatAgentState* CombatState)
  {
    FCrowdDemoRoundBoundaryBusinessFact Result = Base;
    if (CombatState)
    {
      ApplyCombatAgentState(
        *CombatState, Result.Stats, Result.Business, Result.Attack,
        Result.ReactiveMotion, Result.HitFlash, Result.Visual);
    }
    FCrowdDemoMassIdentityFragment Identity;
    Identity.Id = Base.AgentId;
    Identity.LifecycleSerial = static_cast<int32>(
      Base.EntityRef.LifecycleSerial);
    FCrowdDemoCombatAgentState FinalCombat = MakeCombatAgentState(
      Identity, Result.Stats, Result.Business, Result.Attack,
      Result.ReactiveMotion, Result.HitFlash, Result.Visual);
    const bool bUseShowcaseLocomotionState =
      Pipeline.GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && (Pipeline.GetRules().SoftPressureTestCase
          == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse
        || Pipeline.GetRules().SoftPressureTestCase
          == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat);
    FCrowdDemoCombatStateKernel::ResolveVisualStateBoundary(
      Pipeline.GetCurrentFixedStepIndex(),
      Pipeline.GetCurrentStepEndServerTimeSeconds(), Movement.Velocity,
      FinalCombat, bUseShowcaseLocomotionState);
    ApplyCombatAgentState(
      FinalCombat, Result.Stats, Result.Business, Result.Attack,
      Result.ReactiveMotion, Result.HitFlash, Result.Visual);
    switch (FinalCombat.VisualState)
    {
      case ECrowdDemoVisualState::Move:
        Result.Visual.AnimState = ECrowdDemoAnimState::Move; break;
      case ECrowdDemoVisualState::Attack:
        Result.Visual.AnimState = ECrowdDemoAnimState::Attack; break;
      case ECrowdDemoVisualState::HitReact:
        Result.Visual.AnimState = ECrowdDemoAnimState::HitReact; break;
      case ECrowdDemoVisualState::Death:
        Result.Visual.AnimState = ECrowdDemoAnimState::Death; break;
      default:
        Result.Visual.AnimState = ECrowdDemoAnimState::Idle; break;
    }
    return Result;
  }

  FCrowdDemoRoundBoundaryBusinessFact BuildFinalBusinessFact(
    const UCrowdDemoRoundSimPipelineSubsystem& Pipeline,
    const FCrowdDemoRoundBoundaryBusinessFact& Base,
    const FCrowdMovementOutput& Movement,
    const FCrowdDemoRangedCombatAgent* CombatAgent)
  {
    return BuildFinalBusinessFactFromState(
      Pipeline, Base, Movement,
      CombatAgent ? &CombatAgent->Combat : nullptr);
  }

  FCrowdDemoRoundAgentState MakeRoundAgentState(
    const FCrowdDemoMassIdentityFragment& Identity,
    const FCrowdDemoRoundFormationFragment& Formation,
    const FCrowdDemoRoundSimStateFragment& State,
    const FCrowdDemoCombatNetState* Combat = nullptr)
  {
    FCrowdDemoRoundAgentState Result;
    Result.AgentId = Identity.Id;
    Result.LifecycleSerial = Identity.LifecycleSerial;
    Result.Location = FVector_NetQuantize10(State.Location);
    Result.YawDegrees = State.YawDegrees;
    Result.Velocity = FVector_NetQuantize10(State.Velocity);
    Result.RadiusCm = Formation.RadiusCm;
    if (Combat != nullptr)
    {
      Result.Combat = *Combat;
    }
    return Result;
  }

  FVector MakeCenteredFormationOffset(
    const int32 FormationIndex,
    const int32 AgentCount,
    const FCrowdDemoRoundRules& Rules)
  {
    if (Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && Rules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat)
    {
      const int32 ShooterCount = FMath::Max(1, Rules.RangedCombatSettings.ShooterCount);
      const bool bShooter = FormationIndex < ShooterCount;
      const int32 LocalIndex = bShooter ? FormationIndex : FormationIndex - ShooterCount;
      return FVector(
        (static_cast<float>(LocalIndex) - static_cast<float>(ShooterCount - 1) * 0.5f)
          * Rules.FormationSpacingCm,
        bShooter ? -450.0f : 450.0f,
        0.0f);
    }
    const int32 Columns = Rules.FormationColumns > 0
      ? Rules.FormationColumns
      : FMath::CeilToInt(FMath::Sqrt(static_cast<float>(FMath::Max(1, AgentCount))));
    const int32 UsedRows = FMath::Max(1, FMath::CeilToInt(
      static_cast<float>(AgentCount) / static_cast<float>(Columns)));
    const int32 Column = FormationIndex % Columns;
    const int32 Row = FormationIndex / Columns;
    return FVector(
      (static_cast<float>(Column) - static_cast<float>(Columns - 1) * 0.5f) * Rules.FormationSpacingCm,
      (static_cast<float>(Row) - static_cast<float>(UsedRows - 1) * 0.5f) * Rules.FormationSpacingCm,
      0.0f);
  }

}

static void ConfigureRoundPlanApplyQuery(FMassEntityQuery& Query)
{
  Query.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  Query.AddRequirement<FCrowdDemoMassMovementFragment>(EMassFragmentAccess::ReadOnly);
  Query.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdDemoParticlePropertiesFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdDemoMassStatsFragment>(
    EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
  Query.AddRequirement<FCrowdDemoBusinessStateFragment>(
    EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
  Query.AddRequirement<FCrowdDemoRangedAttackFragment>(
    EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
  Query.AddRequirement<FCrowdDemoReactiveMotionFragment>(
    EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
  Query.AddRequirement<FCrowdDemoHitFlashFragment>(
    EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
  Query.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdMassAgentFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdMassBehaviorFragment>(EMassFragmentAccess::ReadOnly);
  Query.AddRequirement<FCrowdMassSimulationStateFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdMassPropertiesFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdMassFacingFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
  Query.AddTagRequirement<FCrowdMassAgentTag>(EMassFragmentPresence::All);
}

static void ExecuteRoundPlanApply(
  FMassEntityQuery& EntityQuery,
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!World || !Pipeline)
  {
    return;
  }
  const float BoundaryTime = Pipeline->IsActive()
    ? Pipeline->GetSimulatedServerTimeSeconds()
    : GetRoundPipelineServerTime(*World);
  if (!Pipeline->HasDueAuthorityInput(BoundaryTime))
  {
    return;
  }
  Pipeline->RecordAuthorityMassWrite();

  int32 EntityCount = 0;
  TArray<int32> AgentIds;
  TMap<int32, int32> LifecycleByAgentId;
  TMap<int32, float> RadiusByAgentId;
  EntityQuery.ForEachEntityChunk(Context, [&EntityCount, &AgentIds, &LifecycleByAgentId,
      &RadiusByAgentId](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoMassMovementFragment> Movements = ChunkContext.GetFragmentView<FCrowdDemoMassMovementFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      AgentIds.Add(Identities[It].Id);
      LifecycleByAgentId.Add(Identities[It].Id, Identities[It].LifecycleSerial);
      RadiusByAgentId.Add(Identities[It].Id, Movements[It].ContactRadiusCm);
      ++EntityCount;
    }
  });
  if (EntityCount == 0)
  {
    return;
  }
  if (!Pipeline->IsActive() && !Pipeline->HasDueRoundPlan(BoundaryTime))
  {
    return;
  }
  if (!Pipeline->TryClaimPlanApplyBoundary())
  {
    return;
  }
  Pipeline->EnsureFormationIndexCache(AgentIds);
  const TMap<int32, int32>& FormationIndexById = Pipeline->GetFormationIndexByAgentId();

  FCrowdDemoRoundBootstrapPacket Bootstrap;
  if (Pipeline->PeekBootstrap(Bootstrap))
  {
    TMap<int32, const FCrowdDemoRoundAgentState*> BootstrapById;
    for (const FCrowdDemoRoundAgentState& Agent : Bootstrap.Agents)
    {
      BootstrapById.Add(Agent.AgentId, &Agent);
    }
    EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
    {
      const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
      const TConstArrayView<FCrowdDemoMassMovementFragment> Movements = ChunkContext.GetFragmentView<FCrowdDemoMassMovementFragment>();
      const auto RuntimeIdentities =
        ChunkContext.GetMutableFragmentView<FCrowdMassAgentFragment>();
      const TArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSimStateFragment>();
      const auto RuntimeStates =
        ChunkContext.GetMutableFragmentView<FCrowdMassSimulationStateFragment>();
      const TArrayView<FCrowdDemoRoundFormationFragment> Formations = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFormationFragment>();
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        const FCrowdDemoRoundAgentState* const* BootstrapState = BootstrapById.Find(Identities[It].Id);
        RuntimeIdentities[It].AgentId = Identities[It].Id;
        RuntimeIdentities[It].SetStableEntityRef(FCrowdStableEntityRef{
          CrowdDemoStableProviderId,
          static_cast<uint64>(Identities[It].Id) + 1,
          static_cast<uint32>(Identities[It].LifecycleSerial)});
        const int32 FormationIndex = FormationIndexById.FindRef(Identities[It].Id);
        FCrowdDemoRoundFormationFragment& Formation = Formations[It];
        Formation.FormationIndex = FormationIndex;
        Formation.RadiusCm = BootstrapState ? (*BootstrapState)->RadiusCm : Movements[It].ContactRadiusCm;
        Formation.bInitialized = true;
        FCrowdDemoRoundSimStateFragment& State = States[It];
        if (BootstrapState)
        {
          State.Location = FVector((*BootstrapState)->Location);
          State.Velocity = FVector((*BootstrapState)->Velocity);
          State.YawDegrees = (*BootstrapState)->YawDegrees;
        }
        State.SimulatedServerTimeSeconds = Bootstrap.ServerTimeSeconds;
        RuntimeStates[It].Position = State.Location;
        RuntimeStates[It].Velocity = State.Velocity;
        RuntimeStates[It].YawDegrees = State.YawDegrees;
        RuntimeStates[It].PlanRevision = State.PlanRevision;
        RuntimeStates[It].bInitialized = State.bInitialized;
      }
    });
    Pipeline->MarkBootstrapApplied(EntityCount);
  }

  FCrowdDemoRoundPlanPacket Plan;
  bool bActivatedPlan = false;
  auto ApplyPlan = [&](const FCrowdDemoRoundPlanPacket& DuePlan)
  {
    const bool bLate = BoundaryTime > DuePlan.StartServerTimeSeconds + DuePlan.Rules.FixedStepSeconds;
    Pipeline->ActivatePlan(DuePlan, EntityCount, bLate);
    bActivatedPlan = true;
    TArray<FCrowdDemoCapabilityProfile> CapabilityProfiles;
    TArray<FCrowdDemoCapabilityAgentAssignment> P0Assignments;
    TMap<int32, int32> ProfileIdByFormationIndex;
    if (DuePlan.Rules.bEnableHeterogeneousProfiles != 0)
    {
      FCrowdDemoCapabilityProfileKernel::BuildP0Profiles(CapabilityProfiles);
      FCrowdDemoCapabilityProfileKernel::BuildP0Assignments(0, P0Assignments);
      for (const FCrowdDemoCapabilityAgentAssignment& Assignment : P0Assignments)
        ProfileIdByFormationIndex.Add(Assignment.FormationIndex, Assignment.ProfileId);
    }
    TArray<FCrowdDemoCapabilityAgentAssignment> RuntimeCapabilityAssignments;
    const bool bOpenSpawnRelaxation =
      DuePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && DuePlan.Rules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::OpenSpawnRelaxation;
    FCrowdDemoOpenSpawnRelaxationLayout OpenSpawnLayout;
    if (bOpenSpawnRelaxation)
    {
      TArray<FCrowdDemoOpenSpawnRelaxationLayoutInput> LayoutInputs;
      LayoutInputs.Reserve(AgentIds.Num());
      for (const int32 AgentId : AgentIds)
      {
        auto& Input = LayoutInputs.AddDefaulted_GetRef();
        Input.AgentId = AgentId;
        Input.FormationIndex = FormationIndexById.FindRef(AgentId);
      }
      OpenSpawnLayout = FCrowdDemoOpenSpawnRelaxationKernel::BuildLayout(
        LayoutInputs,
        DuePlan.Rules.ParticleProfile.PhysicalRadiusCm,
        DuePlan.Rules.ParticleProfile.HardSafetyGapCm,
        DuePlan.Rules.ParticleProfile.SoftMarginCm);
      Pipeline->InitializeOpenSpawnRelaxation(OpenSpawnLayout);
      if (!OpenSpawnLayout.bValid)
        UE_LOG(LogTemp, Error, TEXT("VIOLATION CrowdDemoT1InvalidLayout agents=%d"), AgentIds.Num());
    }
    const bool bOpenCohortMovement =
      DuePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && DuePlan.Rules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::OpenCohortMovement;
    FCrowdDemoOpenCohortMovementLayout OpenCohortLayout;
    if (bOpenCohortMovement)
    {
      TArray<FCrowdDemoOpenCohortMovementLayoutInput> LayoutInputs;
      LayoutInputs.Reserve(AgentIds.Num());
      for (const int32 AgentId : AgentIds)
      {
        auto& Input = LayoutInputs.AddDefaulted_GetRef();
        Input.AgentId = AgentId;
        Input.FormationIndex = FormationIndexById.FindRef(AgentId);
      }
      OpenCohortLayout = FCrowdDemoOpenCohortMovementKernel::BuildLayout(
        LayoutInputs,
        DuePlan.Rules.ParticleProfile.PhysicalRadiusCm,
        DuePlan.Rules.ParticleProfile.HardSafetyGapCm,
        DuePlan.Rules.FormationColumns,
        DuePlan.Rules.FormationSpacingCm,
        FVector(DuePlan.Rules.SpawnOrigin));
      Pipeline->InitializeOpenCohortMovement(OpenCohortLayout);
      if (!OpenCohortLayout.bValid)
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoT2InvalidLayout agents=%d"), AgentIds.Num());
    }
    const bool bBidirectionalSwap =
      DuePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && DuePlan.Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::BidirectionalSwap;
    FCrowdDemoBidirectionalSwapLayout BidirectionalSwapLayout;
    if (bBidirectionalSwap)
    {
      TArray<FCrowdDemoBidirectionalSwapLayoutInput> LayoutInputs;
      LayoutInputs.Reserve(AgentIds.Num());
      for (const int32 AgentId : AgentIds)
      {
        auto& Input = LayoutInputs.AddDefaulted_GetRef();
        Input.AgentId = AgentId;
        Input.FormationIndex = FormationIndexById.FindRef(AgentId);
      }
      BidirectionalSwapLayout = FCrowdDemoBidirectionalSwapKernel::BuildLayout(
        LayoutInputs,
        DuePlan.Rules.ParticleProfile.PhysicalRadiusCm,
        DuePlan.Rules.ParticleProfile.HardSafetyGapCm,
        DuePlan.Rules.FormationSpacingCm);
      Pipeline->InitializeBidirectionalSwap(BidirectionalSwapLayout);
      if (!BidirectionalSwapLayout.bValid)
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoT3InvalidLayout agents=%d"), AgentIds.Num());
    }
    const bool bValidCorridorTransit =
      DuePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && (DuePlan.Rules.SoftPressureTestCase
          == ECrowdDemoSoftPressureTestCase::ValidCorridorTransit
        || DuePlan.Rules.SoftPressureTestCase
          == ECrowdDemoSoftPressureTestCase::HeterogeneousTransit);
    FCrowdDemoValidCorridorTransitLayout ValidCorridorTransitLayout;
    if (bValidCorridorTransit)
    {
      TArray<FCrowdDemoValidCorridorTransitLayoutInput> LayoutInputs;
      LayoutInputs.Reserve(AgentIds.Num());
      for (const int32 AgentId : AgentIds)
      {
        auto& Input = LayoutInputs.AddDefaulted_GetRef();
        Input.AgentId = AgentId;
        Input.FormationIndex = FormationIndexById.FindRef(AgentId);
      }
      float LayoutRadiusCm = DuePlan.Rules.ParticleProfile.PhysicalRadiusCm;
      float LayoutHardGapCm = DuePlan.Rules.ParticleProfile.HardSafetyGapCm;
      for (const FCrowdDemoCapabilityProfile& Profile : CapabilityProfiles)
      {
        LayoutRadiusCm = FMath::Max(LayoutRadiusCm, Profile.Particle.PhysicalRadiusCm);
        LayoutHardGapCm = FMath::Max(LayoutHardGapCm, Profile.Particle.HardSafetyGapCm);
      }
      ValidCorridorTransitLayout = FCrowdDemoValidCorridorTransitKernel::BuildLayout(
        LayoutInputs,
        LayoutRadiusCm,
        LayoutHardGapCm,
        FVector(DuePlan.Rules.SpawnOrigin),
        DuePlan.Rules.FormationSpacingCm);
      Pipeline->InitializeValidCorridorTransit(ValidCorridorTransitLayout);
      if (!ValidCorridorTransitLayout.bValid)
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoT4InvalidLayout agents=%d"), AgentIds.Num());
    }
    TArray<FCrowdDemoRoundInitialStateAgent> InitialStateInputs;
    InitialStateInputs.Reserve(AgentIds.Num());
    for (const int32 AgentId : AgentIds)
    {
      FCrowdDemoRoundInitialStateAgent& Input = InitialStateInputs.AddDefaulted_GetRef();
      Input.AgentId = AgentId;
      Input.LifecycleSerial = LifecycleByAgentId.FindRef(AgentId);
      Input.FormationIndex = FormationIndexById.FindRef(AgentId);
      Input.RadiusCm = RadiusByAgentId.FindRef(AgentId);
      if (DuePlan.Rules.bEnableHeterogeneousProfiles != 0)
      {
        const int32* ProfileId = ProfileIdByFormationIndex.Find(Input.FormationIndex);
        if (ProfileId != nullptr && CapabilityProfiles.IsValidIndex(*ProfileId))
        {
          const FCrowdDemoCapabilityProfile& Profile = CapabilityProfiles[*ProfileId];
          Input.CapabilityProfileKey = Profile.CapabilityProfileKey;
          Input.RadiusCm = Profile.Particle.PhysicalRadiusCm;
        }
      }
    }
    TArray<FCrowdDemoRoundInitialStateResult> InitialStates;
    FCrowdDemoRoundInitialStateSummary InitialStateSummary;
    const bool bResetStableInitialState = DuePlan.Rules.RoundStartPolicy
      == ECrowdDemoRoundStartPolicy::ResetToStableInitialState;
    const bool bInitialStateValid = !bResetStableInitialState
      || FCrowdDemoRoundInitialStateKernel::BuildGeneric(
        InitialStateInputs, DuePlan.Rules, InitialStates, InitialStateSummary);
    TMap<int32, const FCrowdDemoRoundInitialStateResult*> InitialStateById;
    for (const FCrowdDemoRoundInitialStateResult& InitialState : InitialStates)
      InitialStateById.Add(InitialState.AgentId, &InitialState);
    if (!bInitialStateValid)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoRoundInitialStateInvalid round_id=%d revision=%d agents=%d"),
        DuePlan.RoundId, DuePlan.Revision, AgentIds.Num());
    }
    EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
    {
      const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
      const auto RuntimeIdentities =
        ChunkContext.GetMutableFragmentView<FCrowdMassAgentFragment>();
      const TArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSimStateFragment>();
      const auto RuntimeStates =
        ChunkContext.GetMutableFragmentView<FCrowdMassSimulationStateFragment>();
      const auto RuntimeProperties =
        ChunkContext.GetMutableFragmentView<FCrowdMassPropertiesFragment>();
      const TArrayView<FCrowdDemoRoundFormationFragment> Formations = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFormationFragment>();
      const auto RuntimeFacings =
        ChunkContext.GetMutableFragmentView<FCrowdMassFacingFragment>();
      const TArrayView<FCrowdDemoRoundFlowSampleFragment> FlowSamples = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFlowSampleFragment>();
      const TArrayView<FCrowdDemoParticlePropertiesFragment> ParticleProperties = ChunkContext.GetMutableFragmentView<FCrowdDemoParticlePropertiesFragment>();
      const auto Stats = ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
      const auto Businesses = ChunkContext.GetMutableFragmentView<FCrowdDemoBusinessStateFragment>();
      const auto Attacks = ChunkContext.GetMutableFragmentView<FCrowdDemoRangedAttackFragment>();
      const auto Reactives = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionFragment>();
      const auto HitFlashes = ChunkContext.GetMutableFragmentView<FCrowdDemoHitFlashFragment>();
      const auto Visuals = ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
      const bool bHasCombatBundle = !Stats.IsEmpty()
        && Stats.Num() == Businesses.Num()
        && Stats.Num() == Attacks.Num()
        && Stats.Num() == Reactives.Num()
        && Stats.Num() == HitFlashes.Num();
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        RuntimeIdentities[It].AgentId = Identities[It].Id;
        RuntimeIdentities[It].SetStableEntityRef(FCrowdStableEntityRef{
          CrowdDemoStableProviderId,
          static_cast<uint64>(Identities[It].Id) + 1,
          static_cast<uint32>(Identities[It].LifecycleSerial)});
        FCrowdDemoRoundFormationFragment& Formation = Formations[It];
        Formation.FormationIndex = FormationIndexById.FindRef(Identities[It].Id);
        Formation.LocalOffset = MakeCenteredFormationOffset(Formation.FormationIndex, EntityCount, DuePlan.Rules);
        Formation.bInitialized = true;
        FCrowdDemoRoundSimStateFragment& State = States[It];
        if (bResetStableInitialState
          && (DuePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundObstacle
            || DuePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure))
        {
          if (const FCrowdDemoRoundInitialStateResult* const* Initial =
            InitialStateById.Find(Identities[It].Id))
          {
            State.Location = (*Initial)->Location;
            State.Velocity = (*Initial)->Velocity;
            State.YawDegrees = (*Initial)->YawDegrees;
            Formation.LocalOffset = State.Location - FVector(DuePlan.Rules.SpawnOrigin);
          }
        }
        else if (!State.bInitialized)
        {
          State.Location = FVector(DuePlan.Rules.SpawnOrigin) + Formation.LocalOffset;
          State.Velocity = FVector::ZeroVector;
          State.YawDegrees = 90.0f;
        }
        State.SimulatedServerTimeSeconds = DuePlan.StartServerTimeSeconds;
        State.PlanRevision = DuePlan.Revision;
        State.bInitialized = true;
        // Visual phase is stable simulation state included in the applied-state hash.
        // Authority spawn initializes it from the formation/agent index, while a
        // predicted client entity can still hold the fragment default.  Normalize it
        // at every stable round reset so both ends start from the same contract.
        if (bResetStableInitialState)
        {
          Visuals[It].PhaseSeed =
            static_cast<uint32>(Identities[It].Id * 2654435761u);
        }
        if (bHasCombatBundle
          && (DuePlan.Rules.SoftPressureTestCase
            == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse
          || DuePlan.Rules.SoftPressureTestCase
            == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat))
        {
          Stats[It] = FCrowdDemoMassStatsFragment();
          Businesses[It] = FCrowdDemoBusinessStateFragment();
          Attacks[It] = FCrowdDemoRangedAttackFragment();
          Reactives[It] = FCrowdDemoReactiveMotionFragment();
          HitFlashes[It] = FCrowdDemoHitFlashFragment();
          Visuals[It].VisualState = ECrowdDemoVisualState::Idle;
          Visuals[It].VisualRevision = 0;
          Visuals[It].StateStartServerTimeSeconds = DuePlan.StartServerTimeSeconds;
          Visuals[It].PhaseSeed = static_cast<uint32>(Identities[It].Id);
        }
        if (DuePlan.Rules.SoftPressureTestCase
          == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat)
        {
          State.YawDegrees = Formation.FormationIndex
            < DuePlan.Rules.RangedCombatSettings.ShooterCount ? 90.0f : -90.0f;
        }
        if (bOpenSpawnRelaxation)
        {
          if (const auto* LayoutAgent = OpenSpawnLayout.Agents.FindByPredicate(
            [&](const auto& Agent) { return Agent.AgentId == Identities[It].Id; }))
          {
            State.Location = LayoutAgent->StagingLocation;
            State.Velocity = FVector::ZeroVector;
            State.YawDegrees = 0.0f;
          }
        }
        else if (bOpenCohortMovement)
        {
          if (const auto* LayoutAgent = OpenCohortLayout.Agents.FindByPredicate(
            [&](const auto& Agent) { return Agent.AgentId == Identities[It].Id; }))
          {
            State.Location = LayoutAgent->SpawnLocation;
            State.Velocity = FVector::ZeroVector;
            State.YawDegrees = 90.0f;
          }
        }
        else if (bBidirectionalSwap)
        {
          if (const auto* LayoutAgent = BidirectionalSwapLayout.Agents.FindByPredicate(
            [&](const auto& Agent) { return Agent.AgentId == Identities[It].Id; }))
          {
            State.Location = LayoutAgent->SpawnLocation;
            State.Velocity = FVector::ZeroVector;
            State.YawDegrees = LayoutAgent->CohortKey
              == FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey
              ? 90.0f : -90.0f;
            Formation.LocalOffset = State.Location - FVector(DuePlan.Rules.SpawnOrigin);
          }
        }
        else if (bValidCorridorTransit)
        {
          if (const auto* LayoutAgent = ValidCorridorTransitLayout.Agents.FindByPredicate(
            [&](const auto& Agent) { return Agent.AgentId == Identities[It].Id; }))
          {
            State.Location = LayoutAgent->SpawnLocation;
            State.Velocity = FVector::ZeroVector;
            State.YawDegrees = 90.0f;
            Formation.LocalOffset = State.Location - FVector(DuePlan.Rules.SpawnOrigin);
          }
        }
        RuntimeFacings[It] = FCrowdMassFacingFragment();
        RuntimeFacings[It].Value.AgentId = Identities[It].Id;
        RuntimeFacings[It].Value.ResolvedYawDegrees = State.YawDegrees;
        RuntimeFacings[It].PlanRevision = DuePlan.Revision;
        FlowSamples[It] = FCrowdDemoRoundFlowSampleFragment();
        ParticleProperties[It].PhysicalRadiusCm = DuePlan.Rules.ParticleProfile.PhysicalRadiusCm;
        ParticleProperties[It].HardSafetyGapCm = DuePlan.Rules.ParticleProfile.HardSafetyGapCm;
        ParticleProperties[It].SoftMarginCm = DuePlan.Rules.ParticleProfile.SoftMarginCm;
        ParticleProperties[It].Mobility = DuePlan.Rules.ParticleProfile.Mobility;
        ParticleProperties[It].ProfileId = INDEX_NONE;
        ParticleProperties[It].CapabilityProfileKey = 0;
        if (DuePlan.Rules.bEnableHeterogeneousProfiles != 0)
        {
          const int32* ProfileId = ProfileIdByFormationIndex.Find(Formation.FormationIndex);
          if (ProfileId != nullptr && CapabilityProfiles.IsValidIndex(*ProfileId))
          {
            const FCrowdDemoCapabilityProfile& Profile = CapabilityProfiles[*ProfileId];
            ParticleProperties[It].ProfileId = Profile.ProfileId;
            ParticleProperties[It].CapabilityProfileKey = Profile.CapabilityProfileKey;
            ParticleProperties[It].PhysicalRadiusCm = Profile.Particle.PhysicalRadiusCm;
            ParticleProperties[It].HardSafetyGapCm = Profile.Particle.HardSafetyGapCm;
            ParticleProperties[It].SoftMarginCm = Profile.Particle.SoftMarginCm;
            ParticleProperties[It].Mobility = Profile.Particle.Mobility;
            FCrowdDemoCapabilityAgentAssignment& RuntimeAssignment =
              RuntimeCapabilityAssignments.AddDefaulted_GetRef();
            RuntimeAssignment.AgentId = Identities[It].Id;
            RuntimeAssignment.FormationIndex = Formation.FormationIndex;
            RuntimeAssignment.ProfileId = Profile.ProfileId;
            RuntimeAssignment.CapabilityProfileKey = Profile.CapabilityProfileKey;
          }
        }
        RuntimeStates[It].Position = State.Location;
        RuntimeStates[It].Velocity = State.Velocity;
        RuntimeStates[It].YawDegrees = State.YawDegrees;
        RuntimeStates[It].PlanRevision = State.PlanRevision;
        RuntimeStates[It].bInitialized = State.bInitialized;
        RuntimeProperties[It].PhysicalRadiusCm = ParticleProperties[It].PhysicalRadiusCm;
        RuntimeProperties[It].HardSafetyGapCm = ParticleProperties[It].HardSafetyGapCm;
        RuntimeProperties[It].SoftMarginCm = ParticleProperties[It].SoftMarginCm;
        RuntimeProperties[It].Mobility = ParticleProperties[It].Mobility;
        RuntimeProperties[It].MaximumSpeedCmps = DuePlan.Rules.MaxSpeedCmPerSecond;
        RuntimeProperties[It].CapabilityProfileKey =
          ParticleProperties[It].CapabilityProfileKey;
      }
    });
    if (DuePlan.Rules.bEnableHeterogeneousProfiles != 0)
    {
      TArray<FCrowdDemoCapabilityCohort> CapabilityCohorts;
      FCrowdDemoCapabilityProfileSummary CapabilitySummary;
      FCrowdDemoCapabilityProfileKernel::BuildCohorts(
        CapabilityProfiles, RuntimeCapabilityAssignments,
        CapabilityCohorts, CapabilitySummary);
      Pipeline->SetCapabilityCohorts(
        MoveTemp(CapabilityCohorts), CapabilitySummary);
    }
    if (bResetStableInitialState && bInitialStateValid)
    {
      TArray<FCrowdDemoRoundInitialStateResult> ActualInitialStates;
      ActualInitialStates.Reserve(AgentIds.Num());
      EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
      {
        const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
        const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
        const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
        const auto Properties = ChunkContext.GetFragmentView<FCrowdDemoParticlePropertiesFragment>();
        for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
        {
          FCrowdDemoRoundInitialStateResult& Actual = ActualInitialStates.AddDefaulted_GetRef();
          Actual.AgentId = Identities[It].Id;
          Actual.LifecycleSerial = Identities[It].LifecycleSerial;
          Actual.FormationIndex = Formations[It].FormationIndex;
          Actual.CapabilityProfileKey = Properties[It].CapabilityProfileKey;
          Actual.Location = FVector(States[It].Location);
          Actual.Velocity = FVector(States[It].Velocity);
          Actual.YawDegrees = States[It].YawDegrees;
          Actual.RadiusCm = Properties[It].PhysicalRadiusCm;
        }
      });
      Pipeline->RecordRoundInitialState(
        InitialStateSummary.InputHash,
        FCrowdDemoRoundInitialStateKernel::HashStates(ActualInitialStates));
    }
  };
  if (Pipeline->PopDueRoundPlan(BoundaryTime, Plan))
  {
    ApplyPlan(Plan);
  }

  if (!Pipeline->IsActive())
  {
    return;
  }

  auto GatherStates = [&]()
  {
    TArray<FCrowdDemoRoundAgentState> StatesOut;
    StatesOut.Reserve(EntityCount);
    EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
    {
      const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
      const TConstArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
      const TConstArrayView<FCrowdDemoRoundFormationFragment> Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
      const auto Stats = ChunkContext.GetFragmentView<FCrowdDemoMassStatsFragment>();
      const auto Businesses = ChunkContext.GetFragmentView<FCrowdDemoBusinessStateFragment>();
      const auto Attacks = ChunkContext.GetFragmentView<FCrowdDemoRangedAttackFragment>();
      const auto Reactives = ChunkContext.GetFragmentView<FCrowdDemoReactiveMotionFragment>();
      const auto HitFlashes = ChunkContext.GetFragmentView<FCrowdDemoHitFlashFragment>();
      const auto Visuals = ChunkContext.GetFragmentView<FCrowdDemoMassVisualFragment>();
      const bool bHasCombatBundle = !Stats.IsEmpty()
        && Stats.Num() == Businesses.Num()
        && Stats.Num() == Attacks.Num()
        && Stats.Num() == Reactives.Num()
        && Stats.Num() == HitFlashes.Num();
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        if (States[It].bInitialized)
        {
          FCrowdDemoCombatNetState Combat;
          if (bHasCombatBundle)
          {
            Combat = MakeCombatNetState(
              Stats[It], Businesses[It], Attacks[It], Reactives[It],
              HitFlashes[It], Visuals[It]);
          }
          else
          {
            const FCrowdDemoMassStatsFragment DefaultStats;
            const FCrowdDemoBusinessStateFragment DefaultBusiness;
            const FCrowdDemoRangedAttackFragment DefaultAttack;
            const FCrowdDemoReactiveMotionFragment DefaultReactive;
            const FCrowdDemoHitFlashFragment DefaultHitFlash;
            Combat = MakeCombatNetState(
              DefaultStats, DefaultBusiness, DefaultAttack,
              DefaultReactive, DefaultHitFlash, Visuals[It]);
          }
          StatesOut.Add(MakeRoundAgentState(
            Identities[It], Formations[It], States[It], &Combat));
        }
      }
    });
    SortAgentStates(StatesOut);
    return StatesOut;
  };

  bool bAppliedAuthoritativeFrame = false;
  FCrowdDemoRoundResultPacket Result;
  if (World->GetNetMode() == NM_Client && Pipeline->PopRoundResultForBoundary(Result))
  {
    bAppliedAuthoritativeFrame = true;
    const TArray<FCrowdDemoRoundAgentState> BeforeResult = GatherStates();
    Pipeline->RecordRoundResultComparisonAndApplied(BeforeResult, Result);
    TMap<int32, const FCrowdDemoRoundAgentState*> ResultById;
    for (const FCrowdDemoRoundAgentState& Agent : Result.Agents)
    {
      ResultById.Add(Agent.AgentId, &Agent);
    }
    EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
    {
      const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
      const TArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSimStateFragment>();
      const auto Stats = ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
      const auto Businesses = ChunkContext.GetMutableFragmentView<FCrowdDemoBusinessStateFragment>();
      const auto Attacks = ChunkContext.GetMutableFragmentView<FCrowdDemoRangedAttackFragment>();
      const auto Reactives = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionFragment>();
      const auto HitFlashes = ChunkContext.GetMutableFragmentView<FCrowdDemoHitFlashFragment>();
      const auto Visuals = ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
      const bool bHasCombatBundle = !Stats.IsEmpty()
        && Stats.Num() == Businesses.Num()
        && Stats.Num() == Attacks.Num()
        && Stats.Num() == Reactives.Num()
        && Stats.Num() == HitFlashes.Num();
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        if (const FCrowdDemoRoundAgentState* const* Corrected = ResultById.Find(Identities[It].Id))
        {
          States[It].Location = FVector((*Corrected)->Location);
          States[It].Velocity = FVector((*Corrected)->Velocity);
          States[It].YawDegrees = (*Corrected)->YawDegrees;
          States[It].SimulatedServerTimeSeconds = Result.EndServerTimeSeconds;
          if (bHasCombatBundle)
          {
            ApplyCombatNetState(
              (*Corrected)->Combat, Stats[It], Businesses[It], Attacks[It],
              Reactives[It], HitFlashes[It], Visuals[It]);
          }
          else
          {
            FCrowdDemoMassStatsFragment DefaultStats;
            FCrowdDemoBusinessStateFragment DefaultBusiness;
            FCrowdDemoRangedAttackFragment DefaultAttack;
            FCrowdDemoReactiveMotionFragment DefaultReactive;
            FCrowdDemoHitFlashFragment DefaultHitFlash;
            ApplyCombatNetState(
              (*Corrected)->Combat, DefaultStats, DefaultBusiness,
              DefaultAttack, DefaultReactive, DefaultHitFlash, Visuals[It]);
          }
        }
      }
    });
    Pipeline->SetSimulatedServerTimeForCheckpoint(
      Result.EndServerTimeSeconds);
  }

  if (bAppliedAuthoritativeFrame)
  {
    Pipeline->InvalidateInFlightBoundaryForAuthoritativeState();
  }

  // On clients the next plan is intentionally held until the old round result
  // is consumed. Apply it in this same PlanApply execution at the shared boundary.
  if (!bActivatedPlan && Pipeline->PopDueRoundPlan(Pipeline->GetSimulatedServerTimeSeconds(), Plan))
  {
    ApplyPlan(Plan);
  }

  if (bActivatedPlan)
  {
    Pipeline->RecordRoundStart(GatherStates());
  }
  Pipeline->LogStageOnce(TEXT("01_round_plan_apply"), EntityCount);
}

static void ExecuteRoundSharedFlowFieldBuild(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!Pipeline || !Pipeline->IsActive()
    || !IsRoundFlowScenario(Pipeline->GetRules().Scenario))
  {
    return;
  }
  const FCrowdDemoRoundRules& Rules = Pipeline->GetRules();
  const bool bDynamicTargetFlow = Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && (Rules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::PursuitAndSettleMoving
      || Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetMoving);
  const bool bFlowValid = bDynamicTargetFlow
    ? Pipeline->EnsureDynamicSharedFlowField(
        Rules.FlowFieldConfig, FVector(Pipeline->GetTargetFact().Location.X,
          Pipeline->GetTargetFact().Location.Y,
          Rules.FlowFieldConfig.GoalLocation.Z))
    : Pipeline->EnsureSharedFlowField(Rules.FlowFieldConfig);
  if (!bFlowValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoSharedFlowBuildInvalid round_id=%d step=%d dynamic=%d"),
      Pipeline->GetCurrentRoundId(), Pipeline->GetCurrentFixedStepIndex(),
      bDynamicTargetFlow ? 1 : 0);
    return;
  }
  if (Pipeline->IsBidirectionalSwap())
  {
    if (!Pipeline->EnsureBidirectionalSwapFlowResources())
      UE_LOG(LogTemp, Error, TEXT("VIOLATION CrowdDemoT3FlowFieldBuildFailed"));
  }
  Pipeline->LogStageOnce(TEXT("02_shared_flow_field_build"), 0);
}

static void ExecuteRoundFlowPreferredVelocity(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem = World
    ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>()
    : nullptr;
  if (!Pipeline || !RuntimeSubsystem || !Pipeline->IsActive())
  {
    return;
  }
  const FCrowdDemoRoundRules& Rules = Pipeline->GetRules();
  FCrowdMassSharedFlowSampleInput WorkInput;
  WorkInput.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  WorkInput.PlanRevision = Pipeline->GetCurrentPlanRevision();
  WorkInput.FixedStepSeconds = Rules.FixedStepSeconds;
  WorkInput.Fields.Add(&RuntimeSubsystem->GetSharedFlowResource().Field);
  const bool bTransitExitHold = Pipeline->IsValidCorridorTransit()
    && FCrowdDemoValidCorridorTransitKernel::ShouldHoldCompletedGroup(
      Pipeline->GetValidCorridorTransitProgress());
  bool bGatherValid = true;
  if (!Pipeline->IsBoundarySnapshotCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoSharedFlowBoundarySnapshotMissing step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  for (const FCrowdMassBoundaryAgentRecord& Record
    : Pipeline->GetBoundarySnapshot().Agents)
  {
    FCrowdMassSharedFlowAgentInput Agent;
    Agent.AgentId = Record.Identity.AgentId;
    Agent.LifecycleSerial = Record.Identity.LifecycleSerial;
    Agent.Location = Record.State.Position;
    Agent.CurrentYawDegrees = Record.State.YawDegrees;
    Agent.MaximumSpeedCmps = Rules.MaxSpeedCmPerSecond;
    Agent.FieldIndex = 0;
    FVector Goal = FVector(Rules.FlowFieldConfig.GoalLocation);
    if (Agent.AgentId == INDEX_NONE || Agent.LifecycleSerial <= 0
      || Agent.FieldIndex < 0 || Agent.FieldIndex >= WorkInput.Fields.Num()
      || !WorkInput.Fields[Agent.FieldIndex])
    {
      bGatherValid = false;
      continue;
    }
    const bool bTargetTerminalEnabled = Rules.Scenario
        == ECrowdDemoScenario::SimRoundSoftPressure
      && Rules.TargetDistanceBandSettings.bEnabled != 0;
    const bool bReachedGoal = !bTargetTerminalEnabled
      && FVector::DistSquared2D(Record.State.Position, Goal)
        <= FMath::Square(140.0f);
    Agent.bShouldStop = bReachedGoal || bTransitExitHold;
    Agent.bBypassFlow = Pipeline->IsOpenSpawnRelaxation();
    Agent.GoalLocation = bTransitExitHold ? Record.State.Position : Goal;
    WorkInput.Agents.Add(Agent);
  }
  if (!bGatherValid || WorkInput.Agents.IsEmpty())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoSharedFlowRuntimeGatherInvalid step=%d agents=%d"),
      WorkInput.FixedStepIndex, WorkInput.Agents.Num());
    return;
  }
  if (!Pipeline->StageBoundarySharedFlowWork(WorkInput))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoSharedFlowWorkStageRejected step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
}

namespace
{
bool PublishBootstrapBoundarySnapshotFromMass(
  FMassEntityQuery& EntityQuery,
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive()) return true;
  if (!Pipeline->NeedsBootstrapBoundarySnapshot()) return true;
  if (Pipeline->IsStepInProgress()) return false;
  if (!Pipeline->TryRecordBootstrapMassRead())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBootstrapMassReadRejected step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return false;
  }
  TArray<FCrowdMassBoundaryAgentRecord> Records;
  TArray<FCrowdDemoRoundBoundaryFormationFact> FormationFacts;
  TArray<FCrowdDemoRoundBoundaryFacingFact> FacingFacts;
  TArray<FCrowdDemoRoundBoundaryBusinessFact> BusinessFacts;
  bool bGatherValid = true;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<
      FCrowdDemoMassIdentityFragment>();
    const auto RuntimeIdentities = ChunkContext.GetFragmentView<
      FCrowdMassAgentFragment>();
    const auto RuntimeBehaviors = ChunkContext.GetFragmentView<
      FCrowdMassBehaviorFragment>();
    const auto Formations = ChunkContext.GetFragmentView<
      FCrowdDemoRoundFormationFragment>();
    const auto States = ChunkContext.GetFragmentView<
      FCrowdDemoRoundSimStateFragment>();
    const auto Movements = ChunkContext.GetFragmentView<
      FCrowdDemoMassMovementFragment>();
    const auto Particles = ChunkContext.GetFragmentView<
      FCrowdDemoParticlePropertiesFragment>();
    const auto RuntimeFacings = ChunkContext.GetFragmentView<
      FCrowdMassFacingFragment>();
    const auto Stats = ChunkContext.GetFragmentView<
      FCrowdDemoMassStatsFragment>();
    const auto Businesses = ChunkContext.GetFragmentView<
      FCrowdDemoBusinessStateFragment>();
    const auto Attacks = ChunkContext.GetFragmentView<
      FCrowdDemoRangedAttackFragment>();
    const auto Reactives = ChunkContext.GetFragmentView<
      FCrowdDemoReactiveMotionFragment>();
    const auto HitFlashes = ChunkContext.GetFragmentView<
      FCrowdDemoHitFlashFragment>();
    const auto Visuals = ChunkContext.GetFragmentView<
      FCrowdDemoMassVisualFragment>();
    const bool bHasCombatBundle = !Stats.IsEmpty()
      && Stats.Num() == Businesses.Num()
      && Stats.Num() == Attacks.Num()
      && Stats.Num() == Reactives.Num()
      && Stats.Num() == HitFlashes.Num();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdMassBoundaryAgentRecord Record;
      if (!FCrowdDemoMassCrowdRuntimeAdapter::BuildBoundaryAgentRecord(
        Identities[It], RuntimeIdentities[It], RuntimeBehaviors[It],
        States[It], Movements[It], Particles[It], Record))
      {
        bGatherValid = false;
        continue;
      }
      Records.Add(MoveTemp(Record));
      FormationFacts.Add({
        Identities[It].Id,
        Formations[It].FormationIndex,
        Formations[It].RadiusCm});
      FacingFacts.Add({
        Identities[It].Id,
        RuntimeFacings[It].ConsecutiveFinalSettleSteps});
      FCrowdDemoRoundBoundaryBusinessFact& BusinessFact =
        BusinessFacts.AddDefaulted_GetRef();
      BusinessFact.EntityRef = RuntimeIdentities[It].GetStableEntityRef();
      BusinessFact.AgentId = Identities[It].Id;
      BusinessFact.FormationIndex = Formations[It].FormationIndex;
      BusinessFact.LocalOffset = Formations[It].LocalOffset;
      BusinessFact.RadiusCm = Formations[It].RadiusCm;
      BusinessFact.YawDegrees = States[It].YawDegrees;
      BusinessFact.bHasCombatCapability = bHasCombatBundle;
      if (bHasCombatBundle)
      {
        BusinessFact.Stats = Stats[It];
        BusinessFact.Business = Businesses[It];
        BusinessFact.Attack = Attacks[It];
        BusinessFact.ReactiveMotion = Reactives[It];
        BusinessFact.HitFlash = HitFlashes[It];
      }
      else
      {
        BusinessFact.Stats = FCrowdDemoMassStatsFragment();
        BusinessFact.Business = FCrowdDemoBusinessStateFragment();
        BusinessFact.Attack = FCrowdDemoRangedAttackFragment();
        BusinessFact.ReactiveMotion = FCrowdDemoReactiveMotionFragment();
        BusinessFact.HitFlash = FCrowdDemoHitFlashFragment();
      }
      BusinessFact.Visual = Visuals[It];
    }
  });
  FCrowdMassBoundarySnapshot Snapshot;
  if (bGatherValid)
    FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
      Pipeline->GetCurrentFixedStepIndex(),
      Pipeline->GetCurrentPlanRevision(), Records, Snapshot);
  if (!bGatherValid || !Pipeline->PublishBoundarySnapshot(
      MoveTemp(Snapshot), MoveTemp(FormationFacts), MoveTemp(FacingFacts),
      MoveTemp(BusinessFacts)))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBootstrapSnapshotInvalid step=%d records=%d"),
      Pipeline->GetCurrentFixedStepIndex(), Records.Num());
    return false;
  }
  return true;
}
}

static void ExecuteRoundOpenSpawnRelaxationPrepare(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsOpenSpawnRelaxation())
    return;
  if (!Pipeline->PrepareOpenSpawnRelaxationBoundary())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoT1PreparedBoundaryFactsInvalid step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
  }
}

static void ExecuteRoundTargetFactApply(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline) return;
  const FCrowdDemoRoundRules& Rules = Pipeline->GetRules();
  if (Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && Rules.TargetDistanceBandSettings.bEnabled != 0)
  {
    const int32 MotionStep = Pipeline->GetCurrentFixedStepIndex();
    const FVector2f InitialLocation(
      Rules.TargetMotion.InitialLocation.X, Rules.TargetMotion.InitialLocation.Y);
    const FVector2f LinearVelocity(
      Rules.TargetMotion.LinearVelocity.X, Rules.TargetMotion.LinearVelocity.Y);
    const float PhysicalRadiusCm = Rules.TargetDistanceBandSettings.TargetPhysicalRadiusCm;
    const float PositionQuantumCm = Rules.TargetDistanceBandSettings.PositionQuantumCm;
    const float VelocityQuantumCmps = Rules.TargetDistanceBandSettings.VelocityQuantumCmps;
    Pipeline->GetTargetFact() = Rules.TargetMotion.bReflectAtMotionBounds != 0
      ? FCrowdDemoTargetFactKernel::BuildReflectedLinearMotionFact(
          Rules.TargetMotion.TargetId, Rules.TargetMotion.TargetRevision, MotionStep,
          InitialLocation, LinearVelocity,
          FVector2f(Rules.TargetMotion.MotionBoundsMin.X, Rules.TargetMotion.MotionBoundsMin.Y),
          FVector2f(Rules.TargetMotion.MotionBoundsMax.X, Rules.TargetMotion.MotionBoundsMax.Y),
          Rules.TargetMotion.InitialYawDegrees,
          Rules.TargetMotion.YawRateDegreesPerSecond, PhysicalRadiusCm,
          Rules.FixedStepSeconds, PositionQuantumCm, VelocityQuantumCmps)
      : FCrowdDemoTargetFactKernel::BuildLinearMotionFact(
          Rules.TargetMotion.TargetId, Rules.TargetMotion.TargetRevision, MotionStep,
          InitialLocation, LinearVelocity, Rules.TargetMotion.InitialYawDegrees,
          Rules.TargetMotion.YawRateDegreesPerSecond, PhysicalRadiusCm,
          Rules.FixedStepSeconds, PositionQuantumCm, VelocityQuantumCmps);
    Pipeline->LogStageOnce(TEXT("02_target_fact_apply"), 1);
    return;
  }
}

static void ExecuteRoundTargetPolarTopologyBuild(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || Pipeline->GetRules().TargetRegionTransportSettings.bEnabled == 0) return;
  const bool bStaticTargetForRound = FVector(
    Pipeline->GetRules().TargetMotion.LinearVelocity).IsNearlyZero(0.01f);
  if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
  {
    int32 FeasibleCells = 0;
    bool bAllValid = Pipeline->GetCapabilityProfileSummary().bValid;
    for (FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime : Pipeline->GetCapabilityCohorts())
    {
      const auto Settings = MakeTargetRegionTransportSettings(
        Pipeline->GetRules(), Pipeline->GetTargetFact(), &Runtime.Cohort.Profile,
        Runtime.DemandRegionPhaseOffset);
      FCrowdMassTargetRegionTopologyInput BoundaryWorkInput;
      BoundaryWorkInput.Settings =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionSettings(
          Settings);
      BoundaryWorkInput.FlowConfig =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(
          Pipeline->GetRules().FlowFieldConfig);
      const bool bUseCachedTopology =
        bStaticTargetForRound && Runtime.Topology.bValid
        && Runtime.TopologySummary.bValid;
      if (!Pipeline->StageBoundaryTargetTopologyWork(
          Runtime.Cohort.CapabilityProfileKey, BoundaryWorkInput,
          bUseCachedTopology ? &Runtime.Topology : nullptr,
          bUseCachedTopology ? &Runtime.TopologySummary : nullptr))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoTargetTopologyStageRejected step=%d profile_key=%u"),
          Pipeline->GetCurrentFixedStepIndex(),
          Runtime.Cohort.CapabilityProfileKey);
        return;
      }
      Pipeline->RecordTargetTopologyPerformance(!bUseCachedTopology);
      if (Pipeline->IsBoundarySnapshotCurrent()) continue;
      const bool bBuildTopology = !bStaticTargetForRound || !Runtime.Topology.bValid;
      if (bBuildTopology)
      {
        RunTargetRegionTopologyWork(
          Settings, Pipeline->GetRules().FlowFieldConfig,
          Runtime.Topology, Runtime.TopologySummary);
      }
      Runtime.TopologyRoundHash = FoldTargetHash(
        FoldTargetHash(Runtime.TopologyRoundHash,
          static_cast<uint32>(Pipeline->GetCurrentFixedStepIndex())),
        Runtime.Topology.TopologyHash);
      FeasibleCells += Runtime.TopologySummary.FeasibleCellCount;
      bAllValid = bAllValid && Runtime.TopologySummary.bValid;
      if (!Runtime.TopologySummary.bValid)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoCapabilityTopologyInvalid step=%d profile_key=%u cells=%d edges=%d hash=%u"),
          Pipeline->GetCurrentFixedStepIndex(), Runtime.Cohort.CapabilityProfileKey,
          Runtime.TopologySummary.CellCount, Runtime.TopologySummary.EdgeCount,
          Runtime.TopologySummary.TopologyHash);
      }
    }
    if (!bAllValid)
      return;
    Pipeline->LogStageOnce(TEXT("04_target_polar_topology_build"), FeasibleCells);
    return;
  }
  const auto Settings = MakeTargetRegionTransportSettings(
    Pipeline->GetRules(), Pipeline->GetTargetFact());
  FCrowdMassTargetRegionTopologyInput BoundaryWorkInput;
  BoundaryWorkInput.Settings =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionSettings(Settings);
  BoundaryWorkInput.FlowConfig =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(
      Pipeline->GetRules().FlowFieldConfig);
  const bool bUseCachedTopology =
    bStaticTargetForRound
    && Pipeline->GetPreparedTargetRegionTopology().bValid
    && Pipeline->GetTargetRegionTopologySummary().bValid;
  if (!Pipeline->StageBoundaryTargetTopologyWork(
      0, BoundaryWorkInput,
      bUseCachedTopology
        ? &Pipeline->GetPreparedTargetRegionTopology() : nullptr,
      bUseCachedTopology
        ? &Pipeline->GetTargetRegionTopologySummary() : nullptr))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetTopologyStageRejected step=%d profile_key=0"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  Pipeline->RecordTargetTopologyPerformance(!bUseCachedTopology);
  if (Pipeline->IsBoundarySnapshotCurrent()) return;
  const bool bBuildTopology = !bStaticTargetForRound
    || !Pipeline->GetPreparedTargetRegionTopology().bValid;
  if (bBuildTopology)
  {
    RunTargetRegionTopologyWork(
      Settings, Pipeline->GetRules().FlowFieldConfig,
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetTargetRegionTopologySummary());
  }
  Pipeline->RecordTargetRegionTopologyStep();
  if (!Pipeline->GetTargetRegionTopologySummary().bValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetRegionTopologyInvalid step=%d cells=%d edges=%d hash=%u"),
      Pipeline->GetCurrentFixedStepIndex(),
      Pipeline->GetTargetRegionTopologySummary().CellCount,
      Pipeline->GetTargetRegionTopologySummary().EdgeCount,
      Pipeline->GetTargetRegionTopologySummary().TopologyHash);
  }
  Pipeline->LogStageOnce(TEXT("04_target_polar_topology_build"),
    Pipeline->GetTargetRegionTopologySummary().FeasibleCellCount);
}

static void ExecuteRoundTargetRegionPopulationBuild(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  const UMassCrowdRuntimeSubsystem* RuntimeSubsystem = World
    ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>()
    : nullptr;
  if (!Pipeline || !RuntimeSubsystem
    || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || Pipeline->GetRules().TargetRegionTransportSettings.bEnabled == 0) return;
  const FCrowdSharedFlowField& RuntimeSharedFlowField =
    RuntimeSubsystem->GetSharedFlowResource().Field;
  if (!Pipeline->IsBoundarySnapshotCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetDemandBoundaryInputInvalid step=%d snapshot=%d"),
      Pipeline->GetCurrentFixedStepIndex(),
      Pipeline->GetBoundarySnapshot().Agents.Num());
    return;
  }
  if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
  {
    TMap<uint32, TArray<FCrowdDemoTargetRegionTransportAgent>> AgentsByProfile;
    for (const FCrowdMassBoundaryAgentRecord& Record
      : Pipeline->GetBoundarySnapshot().Agents)
    {
      FCrowdDemoTargetRegionTransportAgent& Agent = AgentsByProfile.FindOrAdd(
        Record.Properties.CapabilityProfileKey).AddDefaulted_GetRef();
      Agent.AgentId = Record.Identity.AgentId;
      Agent.Location = FVector2f(
        Record.State.Position.X, Record.State.Position.Y);
      Agent.Velocity = FVector2f(
        Record.State.Velocity.X, Record.State.Velocity.Y);
      Agent.MaxSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
      Agent.FarFlowPreferredVelocity = FVector2f::ZeroVector;
      Agent.PhysicalRadiusCm = Record.Properties.PhysicalRadiusCm;
      Agent.HardSafetyGapCm = Record.Properties.HardSafetyGapCm;
      Agent.SoftMarginCm = Record.Properties.SoftMarginCm;
    }
    int32 AgentCount = 0;
    for (FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime :
      Pipeline->GetCapabilityCohorts())
    {
      Runtime.Agents = AgentsByProfile.FindRef(Runtime.Cohort.CapabilityProfileKey);
      Runtime.Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
      TSet<int32> CurrentMembers;
      for (FCrowdDemoTargetRegionTransportAgent& Agent : Runtime.Agents)
      {
        CurrentMembers.Add(Agent.AgentId);
        const float Distance = (Agent.Location - FVector2f(
          Pipeline->GetTargetFact().Location.X,
          Pipeline->GetTargetFact().Location.Y)).Size();
        const bool bWasEngaged = Runtime.TargetEngagedHoldAgentIds.Contains(Agent.AgentId);
        const FCrowdDemoTargetRegionAgentDemandState* PreviousDemandState =
          Runtime.Demand.AgentStates.FindByPredicate(
            [&Agent](const FCrowdDemoTargetRegionAgentDemandState& State)
            {
              return State.AgentId == Agent.AgentId;
            });
        const FCrowdTargetEngagementDecision Engagement =
          FCrowdTargetRegionTransportKernel::ResolveTargetEngagement(
            static_cast<ECrowdTargetDistanceResponsePolicy>(
              static_cast<uint8>(Runtime.Cohort.Profile.TargetDistanceResponsePolicy)),
            bWasEngaged,
            PreviousDemandState && PreviousDemandState->bTerminalStay,
            PreviousDemandState && PreviousDemandState->bSupply,
            Distance,
            Runtime.Cohort.Profile.NormalizedMinimumCenterDistanceCm,
            Runtime.Cohort.Profile.NormalizedMaximumCenterDistanceCm,
            100.0f);
        if (Engagement.bAcquired)
        {
          Runtime.TargetEngagedHoldAgentIds.Add(Agent.AgentId);
          ++Runtime.TargetEngagementAcquireCount;
        }
        else if (Engagement.bReleased)
        {
          Runtime.TargetEngagedHoldAgentIds.Remove(Agent.AgentId);
          ++Runtime.TargetEngagementReleaseCount;
        }
        Agent.bEngagedHold = Engagement.bEngagedHold;
        if (Engagement.bSuppressedRetreat)
          ++Runtime.TargetEngagementSuppressedRetreatCount;
      }
      TArray<int32> RemovedMembers;
      for (const int32 AgentId : Runtime.TargetEngagedHoldAgentIds)
        if (!CurrentMembers.Contains(AgentId)) RemovedMembers.Add(AgentId);
      RemovedMembers.Sort();
      for (const int32 AgentId : RemovedMembers)
      {
        Runtime.TargetEngagedHoldAgentIds.Remove(AgentId);
        ++Runtime.TargetEngagementReleaseCount;
      }
    }
    for (FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime : Pipeline->GetCapabilityCohorts())
    {
      const auto Settings = MakeTargetRegionTransportSettings(
        Pipeline->GetRules(), Pipeline->GetTargetFact(), &Runtime.Cohort.Profile,
        Runtime.DemandRegionPhaseOffset);
      TArray<FCrowdDemoTargetRegionTransportAgent> ExternalAgents;
      for (const FCrowdDemoTargetRegionCapabilityCohortRuntime& OtherRuntime :
        Pipeline->GetCapabilityCohorts())
      {
        if (OtherRuntime.Cohort.CapabilityProfileKey
            == Runtime.Cohort.CapabilityProfileKey
          || !FCrowdDemoCapabilityProfileKernel::ShareTargetDistanceBand(
            Runtime.Cohort.Profile, OtherRuntime.Cohort.Profile))
        {
          continue;
        }
        ExternalAgents.Append(OtherRuntime.Agents);
      }
      ExternalAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
      const bool bStaticTargetForRound = FVector(
        Pipeline->GetRules().TargetMotion.LinearVelocity).IsNearlyZero(0.01f);
      FCrowdMassTargetRegionDemandInput DemandBoundaryInput;
      if (bStaticTargetForRound && Runtime.Demand.bValid)
      {
        const bool bRefreshSourceAttachments = !Runtime.Plan.bValid
          || !Runtime.Validation.bValid
          || Pipeline->GetCurrentFixedStepIndex() - Runtime.Plan.BuildFixedStepIndex
            >= Pipeline->GetRules().TargetRegionTransportSettings.PlanLifetimeSteps;
        RunTargetRegionDemandWork(
          Runtime.Agents, ExternalAgents, Settings,
          Pipeline->GetRules().FlowFieldConfig,
          &RuntimeSharedFlowField, Runtime.Topology,
          Runtime.Demand, true, bRefreshSourceAttachments,
          &DemandBoundaryInput, false);
        Pipeline->RecordTargetDemandPerformance(false);
      }
      else
      {
        RunTargetRegionDemandWork(
          Runtime.Agents, ExternalAgents, Settings,
          Pipeline->GetRules().FlowFieldConfig,
          &RuntimeSharedFlowField, Runtime.Topology,
          Runtime.Demand, false, true, &DemandBoundaryInput, false);
        Pipeline->RecordTargetDemandPerformance(true);
      }
      if (!Pipeline->StageBoundaryTargetDemandWork(
          Runtime.Cohort.CapabilityProfileKey, DemandBoundaryInput))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoTargetDemandStageRejected step=%d profile_key=%u"),
          Pipeline->GetCurrentFixedStepIndex(),
          Runtime.Cohort.CapabilityProfileKey);
        return;
      }
      if (Pipeline->IsBoundarySnapshotCurrent()) continue;
      Runtime.DemandRoundHash = FoldTargetHash(
        FoldTargetHash(Runtime.DemandRoundHash,
          static_cast<uint32>(Pipeline->GetCurrentFixedStepIndex())),
        Runtime.Demand.DemandHash);
      AgentCount += Runtime.Agents.Num();
      if (!Runtime.Demand.bValid || Runtime.Agents.Num() != Runtime.Cohort.AgentIds.Num())
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoCapabilityDemandInvalid step=%d profile_key=%u agents=%d expected=%d feasible_regions=%d attachment_failures=%d hash=%u"),
          Pipeline->GetCurrentFixedStepIndex(), Runtime.Cohort.CapabilityProfileKey,
          Runtime.Agents.Num(), Runtime.Cohort.AgentIds.Num(), Runtime.Demand.FeasibleRegionCount,
          Runtime.Demand.SourceAttachmentFailureCount, Runtime.Demand.DemandHash);
      }
    }
    Pipeline->LogStageOnce(TEXT("05_target_region_population_build"), AgentCount);
    return;
  }
  auto& Agents = Pipeline->GetPreparedTargetRegionAgents();
  Agents.Reset();
  for (const FCrowdMassBoundaryAgentRecord& Record
    : Pipeline->GetBoundarySnapshot().Agents)
  {
    auto& Agent = Agents.AddDefaulted_GetRef();
    Agent.AgentId = Record.Identity.AgentId;
    Agent.Location = FVector2f(
      Record.State.Position.X, Record.State.Position.Y);
    Agent.Velocity = FVector2f(
      Record.State.Velocity.X, Record.State.Velocity.Y);
    Agent.MaxSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
    Agent.FarFlowPreferredVelocity = FVector2f::ZeroVector;
  }
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  const auto Settings = MakeTargetRegionTransportSettings(
    Pipeline->GetRules(), Pipeline->GetTargetFact());
  const bool bStaticTargetForRound = FVector(
    Pipeline->GetRules().TargetMotion.LinearVelocity).IsNearlyZero(0.01f);
  FCrowdMassTargetRegionDemandInput DemandBoundaryInput;
  if (bStaticTargetForRound && Pipeline->GetPreparedTargetRegionDemand().bValid)
  {
    const bool bRefreshSourceAttachments =
      !Pipeline->GetPreparedTargetRegionPlan().bValid
      || !Pipeline->GetTargetRegionPlanValidation().bValid
      || Pipeline->GetCurrentFixedStepIndex()
          - Pipeline->GetPreparedTargetRegionPlan().BuildFixedStepIndex
        >= Pipeline->GetRules().TargetRegionTransportSettings.PlanLifetimeSteps;
    RunTargetRegionDemandWork(
      Agents, {}, Settings, Pipeline->GetRules().FlowFieldConfig,
      &RuntimeSharedFlowField,
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetPreparedTargetRegionDemand(), true,
      bRefreshSourceAttachments, &DemandBoundaryInput, false);
    Pipeline->RecordTargetDemandPerformance(false);
  }
  else
  {
    RunTargetRegionDemandWork(
      Agents, {}, Settings, Pipeline->GetRules().FlowFieldConfig,
      &RuntimeSharedFlowField,
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetPreparedTargetRegionDemand(), false, true,
      &DemandBoundaryInput, false);
    Pipeline->RecordTargetDemandPerformance(true);
  }
  if (!Pipeline->StageBoundaryTargetDemandWork(0, DemandBoundaryInput))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetDemandStageRejected step=%d profile_key=0"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  if (Pipeline->IsBoundarySnapshotCurrent()) return;
  Pipeline->RecordTargetRegionDemandStep();
  if (!Pipeline->GetPreparedTargetRegionDemand().bValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetRegionDemandInvalid step=%d agents=%d feasible_regions=%d attachment_failures=%d hash=%u"),
      Pipeline->GetCurrentFixedStepIndex(), Agents.Num(),
      Pipeline->GetPreparedTargetRegionDemand().FeasibleRegionCount,
      Pipeline->GetPreparedTargetRegionDemand().SourceAttachmentFailureCount,
      Pipeline->GetPreparedTargetRegionDemand().DemandHash);
  }
  Pipeline->LogStageOnce(TEXT("05_target_region_population_build"), Agents.Num());
}

static void ExecuteRoundTargetRegionTransportSolve(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || Pipeline->GetRules().TargetRegionTransportSettings.bEnabled == 0) return;
  if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
  {
    const int32 Step = Pipeline->GetCurrentFixedStepIndex();
    const int32 TargetRevision = Pipeline->GetTargetFact().TargetRevision;
    const bool bLifecycleDiagnostic =
      Pipeline->IsTargetRegionPlanLifecycleDiagnosticEnabled();
    int32 RoutedAgentCount = 0;
    for (FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime : Pipeline->GetCapabilityCohorts())
    {
      const FCrowdDemoTargetRegionFlowPlan PreviousPlan = Runtime.Plan;
      const FCrowdDemoTargetRegionQuotaExecutionState PreviousExecution =
        Runtime.QuotaExecution;
      FCrowdMassTargetRegionPlanInput PlanBoundaryInput;
      FCrowdMassTargetRegionPlanOutput WorkOutput = RunTargetRegionPlanWork(
        Runtime.Topology, Runtime.Demand, PreviousPlan, PreviousExecution,
        Step, TargetRevision,
        Pipeline->GetRules().TargetRegionTransportSettings.PlanLifetimeSteps,
        &PlanBoundaryInput, false);
      if (!Pipeline->StageBoundaryTargetPlanWork(
          Runtime.Cohort.CapabilityProfileKey, PlanBoundaryInput))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoTargetPlanStageRejected step=%d profile_key=%u"),
          Step, Runtime.Cohort.CapabilityProfileKey);
        return;
      }
      if (Pipeline->IsBoundarySnapshotCurrent()) continue;
      const int32 RebuildReason = WorkOutput.RebuildReason;
      Runtime.Plan =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionPlan(
          WorkOutput.Plan);
      Runtime.QuotaExecution =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionExecution(
          WorkOutput.Execution);
      Runtime.LastPlanReplacement =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionReplacement(
          WorkOutput.Replacement);
      FCrowdDemoTargetRegionPlanValidationResult Validation =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionValidation(
          WorkOutput.Validation);
      if (RebuildReason != 0)
      {
        Runtime.SolverMillisecondsSamples.Add(
          static_cast<float>(WorkOutput.SolverMilliseconds));
        ++Runtime.PlanRebuildCount;
      }
      if (bLifecycleDiagnostic)
      {
        FCrowdDemoTargetRegionPlanLifecycleBoundaryInput DiagnosticInput;
        DiagnosticInput.FixedStepIndex = Step;
        DiagnosticInput.CapabilityProfileKey = Runtime.Cohort.CapabilityProfileKey;
        DiagnosticInput.PlanLifetimeSteps =
          Pipeline->GetRules().TargetRegionTransportSettings.PlanLifetimeSteps;
        DiagnosticInput.TargetRevision = TargetRevision;
        DiagnosticInput.TargetLocation = FVector2f(
          Pipeline->GetTargetFact().Location.X,
          Pipeline->GetTargetFact().Location.Y);
        DiagnosticInput.SelectedReason = RebuildReason;
        DiagnosticInput.Topology = Runtime.Topology;
        DiagnosticInput.Demand = Runtime.Demand;
        DiagnosticInput.PreviousPlan = PreviousPlan;
        DiagnosticInput.NewPlan = Runtime.Plan;
        DiagnosticInput.PreviousExecution = PreviousExecution;
        DiagnosticInput.NewExecution = Runtime.QuotaExecution;
        DiagnosticInput.PreviousValidation = Validation;
        if (RebuildReason != 0)
        {
          DiagnosticInput.PreviousValidation = RunTargetRegionValidationWork(
            DiagnosticInput.Topology, DiagnosticInput.Demand,
            DiagnosticInput.PreviousPlan, DiagnosticInput.PreviousExecution,
            TargetRevision);
        }
        DiagnosticInput.Agents = Runtime.Agents;
        const uint32 ConditionMask =
          FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeConditionMask(
            DiagnosticInput);
        const int32 SelectedByKernel = static_cast<int32>(
          FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::SelectReason(
            ConditionMask));
        if (SelectedByKernel != RebuildReason)
        {
          UE_LOG(LogTemp, Error,
            TEXT("VIOLATION CrowdDemoTargetRegionPlanLifecycleReason step=%d profile_key=%u processor=%d kernel=%d mask=%u"),
            Step, Runtime.Cohort.CapabilityProfileKey, RebuildReason,
            SelectedByKernel, ConditionMask);
        }
        FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::RecordBoundary(
          DiagnosticInput, Runtime.PlanLifecycle);
      }
      Runtime.Validation = Validation;
      Runtime.ValidationRoundHash = FoldTargetHash(
        FoldTargetHash(Runtime.ValidationRoundHash, static_cast<uint32>(Step)),
        Validation.ValidationHash);
      Runtime.TransportRoundHash = FoldTargetHash(
        FoldTargetHash(Runtime.TransportRoundHash, static_cast<uint32>(Step)),
        FoldTargetHash(Runtime.Plan.TransportHash,
          Runtime.QuotaExecution.ExecutionHash));
      RoutedAgentCount += Runtime.Plan.RoutedAgentCount;
      if (!Runtime.Plan.bValid || !Validation.bValid)
      {
        Runtime.bRoundValid = false;
        if (Runtime.LastInvalidStep != Step)
        {
          ++Runtime.InvalidStepCount;
          Runtime.LastInvalidStep = Step;
        }
        if (!Validation.bValid) ++Runtime.ValidationFailureCount;
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoCapabilityTransportInvalid step=%d profile_key=%u routed=%d unrouted=%d epoch=%d validation_hash=%u insufficient_quota=%d conservation=%d unreachable=%d hash=%u"),
          Step, Runtime.Cohort.CapabilityProfileKey, Runtime.Plan.RoutedAgentCount,
          Runtime.Plan.UnroutedAgentCount, Runtime.Plan.PlanEpoch,
          Validation.ValidationHash, Validation.InsufficientOutgoingQuotaCellCount,
          Validation.FlowConservationFailureCount, Validation.UnreachableDeficitCount,
          Runtime.Plan.TransportHash);
      }
    }
    Pipeline->LogStageOnce(TEXT("06_target_region_transport_solve"), RoutedAgentCount);
    return;
  }
  const auto& Topology = Pipeline->GetPreparedTargetRegionTopology();
  const auto& Demand = Pipeline->GetPreparedTargetRegionDemand();
  auto& Plan = Pipeline->GetPreparedTargetRegionPlan();
  const int32 Step = Pipeline->GetCurrentFixedStepIndex();
  const int32 TargetRevision = Pipeline->GetTargetFact().TargetRevision;
  FCrowdMassTargetRegionPlanInput PlanBoundaryInput;
  FCrowdMassTargetRegionPlanOutput WorkOutput = RunTargetRegionPlanWork(
    Topology, Demand, Plan, Pipeline->GetTargetRegionQuotaExecution(),
    Step, TargetRevision,
    Pipeline->GetRules().TargetRegionTransportSettings.PlanLifetimeSteps,
    &PlanBoundaryInput, false);
  if (!Pipeline->StageBoundaryTargetPlanWork(0, PlanBoundaryInput))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetPlanStageRejected step=%d profile_key=0"),
      Step);
    return;
  }
  if (Pipeline->IsBoundarySnapshotCurrent()) return;
  const int32 RebuildReason = WorkOutput.RebuildReason;
  const float SolverMs = static_cast<float>(WorkOutput.SolverMilliseconds);
  Plan = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionPlan(
    WorkOutput.Plan);
  Pipeline->GetTargetRegionQuotaExecution() =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionExecution(
      WorkOutput.Execution);
  FCrowdDemoTargetRegionPlanValidationResult Validation =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionValidation(
      WorkOutput.Validation);
  Pipeline->GetTargetRegionPlanValidation() = Validation;
  Pipeline->RecordTargetRegionValidationStep();
  Pipeline->RecordTargetRegionTransportStep(SolverMs, RebuildReason);
  if (!Plan.bValid || !Validation.bValid)
  {
    Plan.bValid = false;
    FString WrittenPath;
    const uint32 FixtureHash = Pipeline->HasTargetRegionFailureFixture() ? 0
      : BuildAndOptionallyWriteTargetRegionFailureFixture(
        *World, *Pipeline, Validation, 3, &WrittenPath);
    Pipeline->PinTargetRegionFailureFixture(3, Validation.FirstFailureAgentId,
      Validation.FirstFailureCellKey, FixtureHash);
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetRegionTransportInvalid step=%d routed=%d unrouted=%d epoch=%d validation_hash=%u missing_edge=%d infeasible_edge=%d invalid_cell=%d insufficient_quota=%d conservation=%d unreachable=%d fixture_hash=%u hash=%u"),
      Step, Plan.RoutedAgentCount, Plan.UnroutedAgentCount,
      Plan.PlanEpoch, Validation.ValidationHash, Validation.MissingEdgeCount,
      Validation.InfeasibleEdgeCount, Validation.InvalidCellCount,
      Validation.InsufficientOutgoingQuotaCellCount,
      Validation.FlowConservationFailureCount, Validation.UnreachableDeficitCount,
      FixtureHash, Plan.TransportHash);
  }
  Pipeline->LogStageOnce(TEXT("06_target_region_transport_solve"), Plan.RoutedAgentCount);
}

static void ExecuteRoundTargetRegionGuidance(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || Pipeline->GetRules().TargetRegionTransportSettings.bEnabled == 0) return;
  if (!Pipeline->IsBoundarySnapshotCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetRegionGuidanceBoundarySnapshotMissing step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
  {
    TMap<int32, const FCrowdDemoTargetRegionGuidanceResult*> ById;
    bool bAllValid = true;
    for (FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime : Pipeline->GetCapabilityCohorts())
    {
      const auto Settings = MakeTargetRegionTransportSettings(
        Pipeline->GetRules(), Pipeline->GetTargetFact(), &Runtime.Cohort.Profile,
        Runtime.DemandRegionPhaseOffset);
      FCrowdMassTargetRegionGuidanceInput GuidanceBoundaryInput;
      FCrowdMassTargetRegionGuidanceOutput WorkOutput =
        RunTargetRegionGuidanceWork(
          Runtime.Agents, Settings, Runtime.Topology, Runtime.Demand,
          Runtime.Plan, Runtime.QuotaExecution, &GuidanceBoundaryInput,
          false);
      if (!Pipeline->StageBoundaryTargetGuidanceWork(
          Runtime.Cohort.CapabilityProfileKey, GuidanceBoundaryInput))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoTargetGuidanceStageRejected step=%d profile_key=%u"),
          Pipeline->GetCurrentFixedStepIndex(),
          Runtime.Cohort.CapabilityProfileKey);
        return;
      }
      if (Pipeline->IsBoundarySnapshotCurrent()) continue;
      Runtime.QuotaExecution =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionExecution(
          WorkOutput.Execution);
      Runtime.Guidance.Reset(WorkOutput.Results.Num());
      for (const auto& Result : WorkOutput.Results)
        Runtime.Guidance.Add(
          FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionGuidance(
            Result));
      Runtime.GuidanceSummary =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionGuidanceSummary(
          WorkOutput.Summary);
      Runtime.GuidanceRoundHash = FoldTargetHash(
        FoldTargetHash(Runtime.GuidanceRoundHash,
          static_cast<uint32>(Pipeline->GetCurrentFixedStepIndex())),
        Runtime.GuidanceSummary.GuidanceHash);
      for (const auto& Result : Runtime.Guidance)
        ById.Add(Result.AgentId, &Result);
      if (!Runtime.GuidanceSummary.bValid)
      {
        bAllValid = false;
        Runtime.bRoundValid = false;
        ++Runtime.GuidanceUnroutedStepCount;
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoCapabilityGuidanceInvalid step=%d profile_key=%u unrouted=%d first_agent=%d first_cell=%d hash=%u"),
          Pipeline->GetCurrentFixedStepIndex(), Runtime.Cohort.CapabilityProfileKey,
          Runtime.GuidanceSummary.UnroutedAgentCount,
          Runtime.GuidanceSummary.FirstUnroutedAgentId,
          Runtime.GuidanceSummary.FirstUnroutedCellKey,
          Runtime.GuidanceSummary.GuidanceHash);
      }
    }
    if (Pipeline->IsBoundarySnapshotCurrent())
    {
      Pipeline->LogStageOnce(
        TEXT("07_target_region_guidance"),
        Pipeline->GetCapabilityProfileSummary().AgentCount);
      return;
    }
    int32 Applied = 0;
    TArray<FCrowdGuidanceCandidate> PreparedTargetCandidates;
    for (const FCrowdMassBoundaryAgentRecord& Record
      : Pipeline->GetBoundarySnapshot().Agents)
    {
      const auto* const* Result = ById.Find(Record.Identity.AgentId);
      if (!Result) continue;
      const FVector DesiredLocation(
        Pipeline->GetTargetFact().Location.X,
        Pipeline->GetTargetFact().Location.Y, Record.State.Position.Z);
      const FVector DesiredVelocity(
        (*Result)->DesiredVelocity.X, (*Result)->DesiredVelocity.Y, 0.0f);
      const float DesiredYawDegrees = DesiredVelocity.IsNearlyZero()
        ? Record.State.YawDegrees : DesiredVelocity.Rotation().Yaw;
      const FCrowdDemoGuidanceCandidate Candidate =
        FCrowdDemoGuidanceComposeKernel::BuildCandidate(
          Record.Identity.AgentId, ECrowdDemoGuidanceProvider::TargetRegion,
          Pipeline->GetCurrentPlanRevision(), DesiredVelocity, DesiredLocation,
          DesiredYawDegrees,
          (*Result)->Mode != ECrowdDemoTargetRegionGuidanceMode::Unrouted);
      if (Candidate.bValid)
        PreparedTargetCandidates.Add(
          FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(
            Candidate));
      ++Applied;
    }
    PreparedTargetCandidates.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
    Pipeline->SetPreparedTargetRegionGuidanceCandidates(
      MoveTemp(PreparedTargetCandidates));
    if (!bAllValid || Applied != Pipeline->GetCapabilityProfileSummary().AgentCount)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoCapabilityGuidanceApplyInvalid step=%d applied=%d expected=%d"),
        Pipeline->GetCurrentFixedStepIndex(), Applied,
        Pipeline->GetCapabilityProfileSummary().AgentCount);
    }
    Pipeline->LogStageOnce(TEXT("07_target_region_guidance"), Applied);
    return;
  }
  const auto Settings = MakeTargetRegionTransportSettings(
    Pipeline->GetRules(), Pipeline->GetTargetFact());
  FCrowdMassTargetRegionGuidanceInput GuidanceBoundaryInput;
  FCrowdMassTargetRegionGuidanceOutput WorkOutput =
    RunTargetRegionGuidanceWork(
      Pipeline->GetPreparedTargetRegionAgents(), Settings,
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetPreparedTargetRegionDemand(),
      Pipeline->GetPreparedTargetRegionPlan(),
      Pipeline->GetTargetRegionQuotaExecution(), &GuidanceBoundaryInput,
      false);
  if (!Pipeline->StageBoundaryTargetGuidanceWork(0, GuidanceBoundaryInput))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetGuidanceStageRejected step=%d profile_key=0"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  if (Pipeline->IsBoundarySnapshotCurrent()) return;
  Pipeline->GetTargetRegionQuotaExecution() =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionExecution(
      WorkOutput.Execution);
  Pipeline->GetPreparedTargetRegionGuidance().Reset(WorkOutput.Results.Num());
  for (const auto& Result : WorkOutput.Results)
    Pipeline->GetPreparedTargetRegionGuidance().Add(
      FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionGuidance(Result));
  Pipeline->GetTargetRegionGuidanceSummary() =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionGuidanceSummary(
      WorkOutput.Summary);
  Pipeline->RecordOpenCohortMovementGuidance(
    Pipeline->GetPreparedTargetRegionGuidance());
  TMap<int32, const FCrowdDemoTargetRegionGuidanceResult*> ById;
  for (const auto& Result : Pipeline->GetPreparedTargetRegionGuidance())
    ById.Add(Result.AgentId, &Result);
  int32 Applied = 0;
  TArray<FCrowdGuidanceCandidate> PreparedTargetCandidates;
  for (const FCrowdMassBoundaryAgentRecord& Record
    : Pipeline->GetBoundarySnapshot().Agents)
  {
    if (const auto* const* Result = ById.Find(Record.Identity.AgentId))
    {
      const FVector DesiredLocation(
        Pipeline->GetTargetFact().Location.X,
        Pipeline->GetTargetFact().Location.Y, Record.State.Position.Z);
      const FVector DesiredVelocity(
        (*Result)->DesiredVelocity.X, (*Result)->DesiredVelocity.Y, 0.0f);
      const float DesiredYawDegrees = DesiredVelocity.IsNearlyZero()
        ? Record.State.YawDegrees : DesiredVelocity.Rotation().Yaw;
      const FCrowdDemoGuidanceCandidate Candidate =
        FCrowdDemoGuidanceComposeKernel::BuildCandidate(
          Record.Identity.AgentId, ECrowdDemoGuidanceProvider::TargetRegion,
          Pipeline->GetCurrentPlanRevision(), DesiredVelocity, DesiredLocation,
          DesiredYawDegrees,
          (*Result)->Mode != ECrowdDemoTargetRegionGuidanceMode::Unrouted);
      if (Candidate.bValid)
        PreparedTargetCandidates.Add(
          FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(
            Candidate));
      ++Applied;
    }
  }
  PreparedTargetCandidates.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  Pipeline->SetPreparedTargetRegionGuidanceCandidates(
    MoveTemp(PreparedTargetCandidates));
  Pipeline->RecordTargetRegionGuidanceStep();
  if (!Pipeline->GetTargetRegionGuidanceSummary().bValid)
  {
    FCrowdDemoTargetRegionPlanValidationResult Validation;
    Validation = RunTargetRegionValidationWork(
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetPreparedTargetRegionDemand(),
      Pipeline->GetPreparedTargetRegionPlan(),
      Pipeline->GetTargetRegionQuotaExecution(),
      Pipeline->GetTargetFact().TargetRevision);
    uint32 FixtureHash = 0;
    if (!Pipeline->HasTargetRegionFailureFixture())
    {
      FString WrittenPath;
      FixtureHash = BuildAndOptionallyWriteTargetRegionFailureFixture(
        *World, *Pipeline, Validation, 4, &WrittenPath);
      if (FixtureHash != 0)
      {
        Pipeline->PinTargetRegionFailureFixture(4,
          Pipeline->GetTargetRegionGuidanceSummary().FirstUnroutedAgentId,
          Pipeline->GetTargetRegionGuidanceSummary().FirstUnroutedCellKey,
          FixtureHash);
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTargetRegionFailureFixture role=%s step=%d kind=guidance agent=%d cell=%d validation_valid=%d validation_hash=%u fixture_hash=%u written=%d path=%s"),
          World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
          Pipeline->GetCurrentFixedStepIndex(),
          Pipeline->GetTargetRegionGuidanceSummary().FirstUnroutedAgentId,
          Pipeline->GetTargetRegionGuidanceSummary().FirstUnroutedCellKey,
          Validation.bValid ? 1 : 0, Validation.ValidationHash, FixtureHash,
          WrittenPath.IsEmpty() ? 0 : 1, *WrittenPath);
      }
    }
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetRegionGuidanceInvalid step=%d applied=%d unrouted=%d first_agent=%d first_cell=%d validation_valid=%d validation_hash=%u fixture_hash=%u hash=%u"),
      Pipeline->GetCurrentFixedStepIndex(), Applied,
      Pipeline->GetTargetRegionGuidanceSummary().UnroutedAgentCount,
      Pipeline->GetTargetRegionGuidanceSummary().FirstUnroutedAgentId,
      Pipeline->GetTargetRegionGuidanceSummary().FirstUnroutedCellKey,
      Validation.bValid ? 1 : 0, Validation.ValidationHash, FixtureHash,
      Pipeline->GetTargetRegionGuidanceSummary().GuidanceHash);
  }
  Pipeline->LogStageOnce(TEXT("07_target_region_guidance"), Applied);
}


static void ExecuteRoundParticleConstraint(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!Pipeline || !Pipeline->IsActive()
    || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure)
    return;

  TArray<FCrowdParticleConstraintAgent> CoreAgents;
  TMap<int32, uint32> CapabilityProfileKeyByAgentId;
  if (!Pipeline->IsBoundarySnapshotCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoParticleBoundarySnapshotInvalid step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  if (Pipeline->IsOpenSpawnRelaxation()
    && !Pipeline->ArePreparedOpenSpawnBoundaryFactsCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoParticleT1PreparedFactsStale step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  bool bGatherValid = true;
  for (const FCrowdMassBoundaryAgentRecord& Base
    : Pipeline->GetBoundarySnapshot().Agents)
  {
    bool bParticleActive = true;
    const FVector StartPosition = Base.State.Position;
    const FVector PredictedPosition = Base.State.Position;
    if (Pipeline->IsOpenSpawnRelaxation())
    {
      const FCrowdDemoPreparedOpenSpawnBoundaryFact* OpenSpawnFact =
        Pipeline->FindPreparedOpenSpawnBoundaryFact(Base.Identity.AgentId);
      if (!OpenSpawnFact)
      {
        bGatherValid = false;
        continue;
      }
      bParticleActive = OpenSpawnFact->bParticleActive;
    }
    if (!bParticleActive) continue;
    FCrowdParticleConstraintAgent Agent;
    if (!FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleAgent(
      Base.Identity, Base.Properties, StartPosition, PredictedPosition,
      Pipeline->GetRules().bEnableHeterogeneousProfiles != 0
        ? Pipeline->GetRules().FlowFieldConfig.AgentInflateCm : 0.0f,
      Agent))
    {
      bGatherValid = false;
      continue;
    }
    CoreAgents.Add(Agent);
    CapabilityProfileKeyByAgentId.Add(
      Agent.AgentId, Base.Properties.CapabilityProfileKey);
  }
  if (!bGatherValid || CoreAgents.IsEmpty())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoParticleRuntimeGatherInvalid step=%d agents=%d"),
      Pipeline->GetCurrentFixedStepIndex(), CoreAgents.Num());
    return;
  }
  constexpr int32 TargetParticleId =
    CrowdWorkerTargetConstants::PrimaryTargetParticleAgentId;
  const bool bHasTargetParticle =
    Pipeline->GetRules().TargetDistanceBandSettings.bEnabled != 0;
  if (bHasTargetParticle)
  {
    const FCrowdDemoTargetFact& Target = Pipeline->GetTargetFact();
    FCrowdParticleConstraintAgent& TargetAgent = CoreAgents.AddDefaulted_GetRef();
    TargetAgent.AgentId = TargetParticleId;
    const FVector CurrentTarget(Target.Location.X, Target.Location.Y, 60.0f);
    const FVector TargetVelocity(Target.Velocity.X, Target.Velocity.Y, 0.0f);
    TargetAgent.StartPosition = CurrentTarget - TargetVelocity * Pipeline->GetCurrentFixedStepSeconds();
    TargetAgent.PredictedPosition = CurrentTarget;
    TargetAgent.PhysicalRadiusCm = Target.PhysicalRadiusCm;
    TargetAgent.HardSafetyGapCm =
      Pipeline->GetRules().TargetDistanceBandSettings.TargetHardSafetyGapCm;
    TargetAgent.SoftMarginCm =
      Pipeline->GetRules().TargetDistanceBandSettings.TargetSoftMarginCm;
    TargetAgent.Mobility = 0.0f;
  }

  FCrowdDemoParticleConstraintEnvironment Environment;
  Environment.FlowConfig = Pipeline->GetRules().FlowFieldConfig;
  Environment.bConstrainToFlowBounds = true;
  FCrowdDemoParticleConstraintSettings Settings;
  Settings.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
  Settings.IterationCount = Pipeline->GetRules().ParticleConstraintIterations;
  Settings.SafetyIterationCount = Pipeline->GetRules().ParticleSafetyIterations;
  Settings.SoftResponsePerSecond = Pipeline->GetRules().ParticleSoftResponsePerSecond;
  Settings.SoftMaxPairCorrectionPerIterationCm = Pipeline->GetRules().ParticleSoftMaxCorrectionCm;
  Settings.HardMaxPairCorrectionPerIterationCm = Pipeline->GetRules().ParticleHardMaxCorrectionCm;
  Settings.PositionQuantumCm = Pipeline->GetRules().ParticlePositionQuantumCm;
  Settings.VelocityQuantumCmps = Pipeline->GetRules().ParticleVelocityQuantumCmps;
  const bool bRouteDiagnostic = Pipeline->IsSoftPressureRouteDiagnosticEnabled();
  const bool bStabilityDiagnostic = Pipeline->IsTargetStabilityDiagnosticEnabled();
  Settings.bCaptureRouteDiagnostic = bRouteDiagnostic || bStabilityDiagnostic
    || Pipeline->IsOpenSpawnRelaxation();
  const bool bCaptureParticleTrace = Settings.bCaptureRouteDiagnostic;
  bool bParticleTraceCaptured = bCaptureParticleTrace;
  FCrowdMassParticlePipelineWorkInput ParticlePipelineInput;
  FCrowdMassParticleWorkInput& ParticleWorkInput =
    ParticlePipelineInput.Particle;
  ParticleWorkInput.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  ParticleWorkInput.PlanRevision = Pipeline->GetCurrentPlanRevision();
  ParticleWorkInput.Environment =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleEnvironment(Environment);
  ParticleWorkInput.Settings =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleSettings(Settings);
  ParticleWorkInput.Agents = CoreAgents;
  ParticleWorkInput.bCaptureTrace = bCaptureParticleTrace;
  ParticlePipelineInput.Snapshot = Pipeline->GetBoundarySnapshot();
  ParticlePipelineInput.ExpectedExternalAgentCount =
    bHasTargetParticle ? 1 : 0;
  if (!Pipeline->StageBoundaryParticleWork(MoveTemp(ParticlePipelineInput)))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoParticleBootstrapStageRejected step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
  }
}

static void ExecuteRoundObstacleConstraint(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!Pipeline || !Pipeline->IsActive())
  {
    return;
  }
  if (Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundObstacle
    || !Pipeline->IsBoundarySnapshotCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoObstaclePreparedInputInvalid step=%d scenario=%d snapshot=%d"),
      Pipeline->GetCurrentFixedStepIndex(),
      static_cast<int32>(Pipeline->GetRules().Scenario),
      Pipeline->IsBoundarySnapshotCurrent() ? 1 : 0);
    return;
  }
  if (!Pipeline->StageBoundaryObstacleWork(
      Pipeline->GetRules().FlowFieldConfig,
      Pipeline->GetCurrentFixedStepSeconds()))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoObstacleWorkStageRejected step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
  }
}

static void ConfigureWorkerResultApplyQuery(
  FMassEntityQuery& Query)
{
  Query.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  Query.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdMassAgentFragment>(EMassFragmentAccess::ReadOnly);
  Query.AddRequirement<FCrowdMassSimulationStateFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdMassMovementOutputFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdMassFacingFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdDemoMassMovementFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(
    EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdDemoMassStatsFragment>(
    EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
  Query.AddRequirement<FCrowdDemoBusinessStateFragment>(
    EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
  Query.AddRequirement<FCrowdDemoRangedAttackFragment>(
    EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
  Query.AddRequirement<FCrowdDemoReactiveMotionFragment>(
    EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
  Query.AddRequirement<FCrowdDemoHitFlashFragment>(
    EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
  Query.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
  Query.AddTagRequirement<FCrowdMassAgentTag>(EMassFragmentPresence::All);
}



static void ExecuteRoundFacingBootstrap(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive()) return;
  if (!Pipeline->IsBoundarySnapshotCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingBoundarySnapshotInvalid step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }

  TMap<int32, ECrowdDemoTargetRegionGuidanceMode> GuidanceModeByAgentId;
  if (Pipeline->IsTargetRegionExecutionActive())
  {
    if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
    {
      for (const auto& Runtime : Pipeline->GetCapabilityCohorts())
        for (const auto& Guidance : Runtime.Guidance)
          GuidanceModeByAgentId.Add(Guidance.AgentId, Guidance.Mode);
    }
    else
    {
      for (const auto& Guidance : Pipeline->GetPreparedTargetRegionGuidance())
        GuidanceModeByAgentId.Add(Guidance.AgentId, Guidance.Mode);
    }
  }

  const bool bUsesParticle = Pipeline->GetRules().Scenario
    == ECrowdDemoScenario::SimRoundSoftPressure;
  TMap<int32, int32> PreviousSettleStepsByAgentId;
  for (const FCrowdDemoRoundBoundaryFacingFact& Facing
    : Pipeline->GetBoundaryFacingFacts())
  {
    PreviousSettleStepsByAgentId.Add(
      Facing.AgentId, Facing.ConsecutiveFinalSettleSteps);
  }

  FCrowdMassFacingFinalizeWorkInput CombinedInput;
  FCrowdMassFacingWorkInput& WorkInput = CombinedInput.Facing;
  WorkInput.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  WorkInput.PlanRevision = Pipeline->GetCurrentPlanRevision();
  WorkInput.Settings.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
  CombinedInput.Snapshot = Pipeline->GetBoundarySnapshot();

  TMap<int32, int32> ConsecutiveSettleStepsByAgentId;
  TMap<int32, bool> FinalSettledByAgentId;
  TMap<int32, bool> TerminalOwnerByAgentId;
  bool bGatherValid = true;
  for (const FCrowdMassBoundaryAgentRecord& Base
    : Pipeline->GetBoundarySnapshot().Agents)
  {
    if (!PreviousSettleStepsByAgentId.Contains(Base.Identity.AgentId))
    {
      bGatherValid = false;
      continue;
    }
    const ECrowdDemoTargetRegionGuidanceMode* Mode =
      GuidanceModeByAgentId.Find(Base.Identity.AgentId);
    const bool bTerminalOwner = Mode
      && (*Mode == ECrowdDemoTargetRegionGuidanceMode::TerminalSettle
        || *Mode == ECrowdDemoTargetRegionGuidanceMode::EngagedHold);
    TerminalOwnerByAgentId.Add(Base.Identity.AgentId, bTerminalOwner);
    ConsecutiveSettleStepsByAgentId.Add(Base.Identity.AgentId, 0);
    FinalSettledByAgentId.Add(Base.Identity.AgentId, false);

    FCrowdFacingInput& Input = WorkInput.Agents.AddDefaulted_GetRef();
    Input.AgentId = Base.Identity.AgentId;
    Input.CurrentYawDegrees = Base.State.YawDegrees;
    Input.AutonomousPreferredVelocity = FVector2f::ZeroVector;
    Input.Location = FVector2f(Base.State.Position.X, Base.State.Position.Y);
    Input.TargetLocation = FVector2f(
      Pipeline->GetTargetFact().Location.X,
      Pipeline->GetTargetFact().Location.Y);
    Input.bHasTarget = Pipeline->IsTargetRegionExecutionActive();
    Input.bFinalPositionSettled = false;
  }
  WorkInput.Agents.Sort([](const FCrowdFacingInput& A,
    const FCrowdFacingInput& B)
  {
    return A.AgentId < B.AgentId;
  });
  if (!bGatherValid || WorkInput.Agents.IsEmpty())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingGatherInvalid step=%d agents=%d"),
      Pipeline->GetCurrentFixedStepIndex(), WorkInput.Agents.Num());
    return;
  }

  const bool bDispatched = bUsesParticle
    ? Pipeline->DispatchBoundarySoftPressureWorkGraph(
        MoveTemp(CombinedInput),
        MoveTemp(PreviousSettleStepsByAgentId),
        MoveTemp(TerminalOwnerByAgentId))
    : Pipeline->DispatchBoundaryFacingWork(
        MoveTemp(CombinedInput),
        MoveTemp(ConsecutiveSettleStepsByAgentId),
        MoveTemp(FinalSettledByAgentId));
  if (!bDispatched)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBoundaryWorkGraphDispatchRejected step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
  }
}

static void ExecuteRoundMovementWork(
  float& OutGuidanceWorkMilliseconds, 
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  OutGuidanceWorkMilliseconds = 0.0f;
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive()) return;
  if (!Pipeline->IsBoundarySnapshotCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementWorkBoundarySnapshotInvalid step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }

  FCrowdMassMovementPipelineWorkInput WorkInput;
  WorkInput.Guidance.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  WorkInput.Guidance.PlanRevision = Pipeline->GetCurrentPlanRevision();
  WorkInput.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
  TArray<FCrowdGuidanceCandidate> FlowCandidates;
  FlowCandidates.Reserve(Pipeline->GetBoundarySnapshot().Agents.Num());
  for (const FCrowdMassBoundaryAgentRecord& Record
    : Pipeline->GetBoundarySnapshot().Agents)
  {
    FlowCandidates.Add(
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(
        FCrowdDemoGuidanceComposeKernel::BuildCandidate(
          Record.Identity.AgentId,
          ECrowdDemoGuidanceProvider::SharedFlow,
          Pipeline->GetCurrentPlanRevision(),
          FVector::ZeroVector, Record.State.Position,
          Record.State.YawDegrees, true)));
  }
  const bool bGuidanceGatherValid =
    FCrowdMassRuntimeBridge::BuildGuidanceRecords(
      Pipeline->GetBoundarySnapshot(), FlowCandidates,
      Pipeline->GetPreparedTargetRegionGuidanceCandidates(),
      Pipeline->GetPreparedBusinessGuidanceCandidates(),
      WorkInput.Guidance.Records);
  if (!bGuidanceGatherValid || WorkInput.Guidance.Records.IsEmpty())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementWorkGuidanceGatherInvalid step=%d records=%d"),
      Pipeline->GetCurrentFixedStepIndex(), WorkInput.Guidance.Records.Num());
    return;
  }

  WorkInput.Environment =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(
      Pipeline->GetRules().FlowFieldConfig);
  const bool bUseLocal = Pipeline->GetRules().Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure
    && Pipeline->GetRules().LocalPredictiveSettings.bEnabled != 0;
  WorkInput.bRunLocalPredictive = bUseLocal;
  const bool bCaptureDiagnostic =
    bUseLocal && Pipeline->IsTargetStabilityDiagnosticEnabled();
  WorkInput.bCaptureLocalPredictiveDiagnostic = bCaptureDiagnostic;
  TMap<int32, int32> PreviousBlockedAgeByAgentId;
  for (const FCrowdDemoLocalPredictiveResult& Previous
    : Pipeline->GetPreparedLocalPredictiveResults())
    PreviousBlockedAgeByAgentId.Add(
      Previous.AgentId, Previous.NextBlockedAgeSteps);
  const TArray<FCrowdDemoLocalPredictiveGrantState> PreviousGrantStates =
    Pipeline->GetLocalPredictiveGrantStates();
  for (const FCrowdDemoLocalPredictiveGrantState& Grant : PreviousGrantStates)
    WorkInput.PreviousGrantStates.Add(
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreLocalPredictiveGrant(Grant));
  if (bUseLocal)
  {
    const FCrowdDemoLocalPredictiveRuleSettings& Rule =
      Pipeline->GetRules().LocalPredictiveSettings;
    FCrowdDemoLocalPredictiveSettings Settings;
    Settings.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
    Settings.TimeHorizonSeconds = Rule.TimeHorizonSeconds;
    Settings.SpatialCellSizeCm = Rule.SpatialCellSizeCm;
    Settings.VelocityQuantumCmps =
      Pipeline->GetRules().ParticleVelocityQuantumCmps;
    Settings.ConstraintEpsilonCmps = Rule.ConstraintEpsilonCmps;
    Settings.RequestedProgressThresholdCmps =
      Rule.RequestedProgressThresholdCmps;
    Settings.BlockedProgressThresholdCmps =
      Rule.BlockedProgressThresholdCmps;
    Settings.GrantedResponsibility = Rule.GrantedResponsibility;
    Settings.GrantDurationSteps = Rule.GrantDurationSteps;
    Settings.JointIterationCount = Rule.JointIterationCount;
    WorkInput.LocalPredictiveSettings =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreLocalPredictiveSettings(
        Settings);
  }

  const bool bOpenSpawnRelaxation = Pipeline->IsOpenSpawnRelaxation();
  if (bOpenSpawnRelaxation
    && !Pipeline->ArePreparedOpenSpawnBoundaryFactsCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementWorkT1PreparedFactsStale step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  TMap<int32, int32> BoundaryLifecycleByAgentId;
  BoundaryLifecycleByAgentId.Reserve(WorkInput.Guidance.Records.Num());
  for (const FCrowdMassGatherRecord& Record : WorkInput.Guidance.Records)
  {
    BoundaryLifecycleByAgentId.Add(
      Record.Identity.AgentId, Record.Identity.LifecycleSerial);
    const FCrowdDemoPreparedOpenSpawnBoundaryFact* OpenSpawnFact =
      bOpenSpawnRelaxation
      ? Pipeline->FindPreparedOpenSpawnBoundaryFact(Record.Identity.AgentId)
      : nullptr;
    if (bOpenSpawnRelaxation && !OpenSpawnFact)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMovementWorkT1AgentFactMissing step=%d agent=%d"),
        Pipeline->GetCurrentFixedStepIndex(), Record.Identity.AgentId);
      return;
    }
    FCrowdMassMovementPipelineAgentOverlay& Overlay =
      WorkInput.AgentOverlays.AddDefaulted_GetRef();
    Overlay.AgentId = Record.Identity.AgentId;
    Overlay.PreviousBlockedAgeSteps =
      PreviousBlockedAgeByAgentId.FindRef(Record.Identity.AgentId);
    Overlay.MaximumSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
    if (OpenSpawnFact)
    {
      Overlay.bFreezeAtBoundaryLocation = true;
      Overlay.BoundaryLocation = OpenSpawnFact->bPendingBoundaryReset
        ? OpenSpawnFact->BoundaryResetLocation : Record.State.Position;
      Overlay.bParticleActive = OpenSpawnFact->bParticleActive;
    }
  }
  const bool bExpectReactiveFacts = Pipeline->IsRangedProjectileCombat()
    || (Pipeline->GetRules().Scenario
        == ECrowdDemoScenario::SimRoundSoftPressure
      && Pipeline->GetRules().SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse);
  const TArray<FCrowdDemoPreparedReactiveMotionStep>& ReactiveSteps =
    Pipeline->GetPreparedReactiveMotionSteps();
  TMap<int32, const FCrowdDemoPreparedReactiveMotionStep*> ReactiveByAgentId;
  const bool bReactivePrepared = !ReactiveSteps.IsEmpty();
  bool bReactiveGatherValid = !bReactivePrepared
    || ReactiveSteps.Num() == WorkInput.Guidance.Records.Num();
  for (const FCrowdDemoPreparedReactiveMotionStep& Step : ReactiveSteps)
  {
    if (Step.AgentId == INDEX_NONE || ReactiveByAgentId.Contains(Step.AgentId))
      bReactiveGatherValid = false;
    else
      ReactiveByAgentId.Add(Step.AgentId, &Step);
  }
  for (FCrowdMassMovementPipelineAgentOverlay& Overlay : WorkInput.AgentOverlays)
  {
    const int32* LifecycleSerial =
      BoundaryLifecycleByAgentId.Find(Overlay.AgentId);
    const FCrowdDemoPreparedReactiveMotionStep* const* Step =
      ReactiveByAgentId.Find(Overlay.AgentId);
    if (bExpectReactiveFacts && bReactivePrepared
      && (!LifecycleSerial || !Step
        || (*Step)->LifecycleSerial != *LifecycleSerial))
    {
      bReactiveGatherValid = false;
      continue;
    }
    if (Step && (*Step)->bActive)
    {
      Overlay.bVerticalOverride = true;
      Overlay.ProposedZ = (*Step)->ProposedZ;
      Overlay.VerticalVelocityCmps = (*Step)->VerticalVelocityCmps;
    }
  }
  if (!bReactiveGatherValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementWorkReactiveGatherInvalid step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }

  if (!Pipeline->StageBoundaryMovementWork(MoveTemp(WorkInput)))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementBootstrapStageRejected step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
  }
}




static void ExecuteRoundCheckpointPublisher(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive())
  {
    return;
  }
  if (!Pipeline->ShouldBuildRoundResult())
  {
    return;
  }
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoCheckpointRetainedViewMissing step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  const FCrowdWorkerResultApplyProxy& Proxy =
    RuntimeSubsystem->GetWorkerResultApplyProxy();
  const FCrowdWorkerResultApplyMetrics& ProxyMetrics = Proxy.GetMetrics();
  const TConstArrayView<FCrowdStableEntityRef> StableEntities =
    Proxy.GetStableEntityView();
  TMap<FCrowdStableEntityRef,
    const FCrowdDemoRoundBoundaryBusinessFact*> BusinessByEntityRef;
  for (const FCrowdDemoRoundBoundaryBusinessFact& Value
    : Pipeline->GetBoundaryBusinessFacts())
    BusinessByEntityRef.Add(Value.EntityRef, &Value);
  TArray<FCrowdDemoRoundAgentState> States;
  States.Reserve(StableEntities.Num());
  bool bCheckpointValid = ProxyMetrics.Generation != 0
    && ProxyMetrics.LastConsumedPublishSequence != 0
    && !StableEntities.IsEmpty()
    && BusinessByEntityRef.Num() == StableEntities.Num();
  for (const FCrowdStableEntityRef& EntityRef : StableEntities)
  {
    const FCrowdDemoRoundBoundaryBusinessFact* const* Business =
      BusinessByEntityRef.Find(EntityRef);
    const FCrowdWorkerDomainProxyState* MovementDomain =
      Proxy.FindDomain(EntityRef, ECrowdWorkerField::Facing);
    FCrowdWorkerMovementState WorkerMovement;
    if (!Business || !MovementDomain
      || MovementDomain->PublishSequence
        > ProxyMetrics.LastConsumedPublishSequence
      || MovementDomain->SourceInputSequence
        > ProxyMetrics.LastAppliedInputSequence
      || !FCrowdWorkerMovementStateCodec::Decode(
        MovementDomain->State.Payload, WorkerMovement))
    {
      bCheckpointValid = false;
      continue;
    }
    const FCrowdDemoRoundBoundaryBusinessFact& BaseBusiness = **Business;
    FCrowdMovementOutput Movement;
    Movement.AgentId = BaseBusiness.AgentId;
    Movement.LifecycleSerial = EntityRef.LifecycleSerial;
    Movement.Position = WorkerMovement.Position;
    Movement.Velocity = WorkerMovement.Velocity;
    Movement.YawDegrees = WorkerMovement.YawDegrees;
    Movement.bValid = true;
    FCrowdDemoCombatAgentState WorkerCombat;
    const FCrowdDemoCombatAgentState* WorkerCombatState = nullptr;
    if (const FCrowdWorkerDomainProxyState* CombatDomain =
        Proxy.FindDomain(EntityRef, ECrowdWorkerField::Combat))
    {
      FCrowdWorkerCombatState EncodedCombat;
      if (CombatDomain->PublishSequence
          > ProxyMetrics.LastConsumedPublishSequence
        || CombatDomain->SourceInputSequence
          > ProxyMetrics.LastAppliedInputSequence
        || !FCrowdWorkerCombatStateCodec::Decode(
          CombatDomain->State.Payload, EncodedCombat)
        || !FCrowdDemoWorkerCombatStatePayloadCodec::Decode(
          EncodedCombat.HostState, WorkerCombat)
        || WorkerCombat.AgentId != BaseBusiness.AgentId
        || WorkerCombat.LifecycleSerial
          != static_cast<int32>(EntityRef.LifecycleSerial))
      {
        bCheckpointValid = false;
        continue;
      }
      WorkerCombatState = &WorkerCombat;
    }
    else if (BaseBusiness.bHasCombatCapability)
    {
      bCheckpointValid = false;
      continue;
    }
    const FCrowdDemoRoundBoundaryBusinessFact FinalBusiness =
      BuildFinalBusinessFactFromState(
        *Pipeline, BaseBusiness, Movement, WorkerCombatState);
    FCrowdDemoRoundAgentState& State = States.AddDefaulted_GetRef();
    State.AgentId = BaseBusiness.AgentId;
    State.LifecycleSerial = EntityRef.LifecycleSerial;
    State.Location = FVector_NetQuantize10(Movement.Position);
    State.Velocity = FVector_NetQuantize10(Movement.Velocity);
    State.YawDegrees = Movement.YawDegrees;
    State.RadiusCm = BaseBusiness.RadiusCm;
    State.Combat = MakeCombatNetState(
      FinalBusiness.Stats, FinalBusiness.Business, FinalBusiness.Attack,
      FinalBusiness.ReactiveMotion, FinalBusiness.HitFlash,
      FinalBusiness.Visual);
  }
  if (!bCheckpointValid || States.Num() != StableEntities.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoCheckpointRetainedStateMismatch step=%d states=%d retained=%d"),
      Pipeline->GetCurrentFixedStepIndex(), States.Num(),
      StableEntities.Num());
    return;
  }
  SortAgentStates(States);
  if (Pipeline->ShouldBuildRoundResult())
  {
    FCrowdDemoRoundResultPacket Result;
    // bValid denotes a transportable packet, not an algorithm pass/fail.
    // Particle capability failure is carried by ParticleInvalidStepCount and
    // the pinned failure fixture so the client can still assemble checkpoint.
    Result.bValid = 1;
    Result.RoundId = Pipeline->GetCurrentRoundId();
    Result.Revision = Pipeline->GetCurrentPlanRevision();
    Result.CheckpointRevision = Pipeline->AllocateCheckpointRevision();
    Result.StateFrameRevision =
      Pipeline->AllocateCheckpointStateFrameRevision();
    Result.EndServerTimeSeconds = Pipeline->GetCurrentStepEndServerTimeSeconds();
    Result.Agents = States;
    Result.InitialOverlapPairCount = Pipeline->GetRoundInitialOverlapPairCount();
    Result.InitialSevereOverlapPairCount = Pipeline->GetRoundInitialSevereOverlapPairCount();
    if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
    {
      const FCrowdDemoParticleConstraintSummary& Particle =
        Pipeline->GetLastParticleAppliedSummary();
      Result.ParticleMetrics.SoftPairCount = Particle.SoftPairCount;
      Result.ParticleMetrics.RoundInputHash = Pipeline->GetRoundInputHash();
      Result.ParticleMetrics.RoundInitialStateHash = Pipeline->GetRoundInitialStateHash();
      Result.ParticleMetrics.RoundResetCount = Pipeline->GetRoundResetCount();
      Result.ParticleMetrics.RoundTransitionOrderViolationCount =
        Pipeline->GetRoundTransitionOrderViolationCount();
      Result.ParticleMetrics.DynamicFlowTopologyHash =
        Pipeline->GetSharedFlowField().TopologyHash;
      Result.ParticleMetrics.DynamicFlowAnchorCellKey =
        Pipeline->GetDynamicFlowAnchorCellKey();
      Result.ParticleMetrics.DynamicFlowIntegrationHash =
        Pipeline->GetSharedFlowField().IntegrationHash;
      Result.ParticleMetrics.DynamicFlowIntegrationRebuildCount =
        Pipeline->GetDynamicFlowIntegrationRebuildCount();
      Result.ParticleMetrics.DynamicFlowRoundHash = Pipeline->GetDynamicFlowRoundHash();
      const FCrowdDemoLocalPredictiveSummary& LocalPredictive =
        Pipeline->GetLastLocalPredictiveSummary();
      Result.ParticleMetrics.GuidanceCandidateHash =
        Pipeline->GetGuidanceCandidateRoundHash();
      Result.ParticleMetrics.GuidanceComposeHash =
        Pipeline->GetGuidanceComposeRoundHash();
      Result.ParticleMetrics.GuidanceComposeSampleCount =
        Pipeline->GetGuidanceComposeSampleCount();
      Result.ParticleMetrics.bLocalPredictiveValid = LocalPredictive.bValid ? 1 : 0;
      Result.ParticleMetrics.LocalPredictiveHash = Pipeline->GetLocalPredictiveRoundHash();
      Result.ParticleMetrics.LocalPredictiveSampleCount =
        Pipeline->GetLocalPredictiveSampleCount();
      Result.ParticleMetrics.LocalPredictiveProcessedAgentCount =
        LocalPredictive.ProcessedAgentCount;
      Result.ParticleMetrics.LocalPredictiveCandidatePairCount =
        LocalPredictive.CandidatePairCount;
      Result.ParticleMetrics.LocalPredictiveConflictPairCount =
        LocalPredictive.ConflictPairCount;
      Result.ParticleMetrics.LocalPredictiveComponentCount = LocalPredictive.ComponentCount;
      Result.ParticleMetrics.LocalPredictiveMaxComponentSize =
        LocalPredictive.MaxComponentSize;
      Result.ParticleMetrics.LocalPredictiveAdjustedAgentCount =
        LocalPredictive.AdjustedAgentCount;
      Result.ParticleMetrics.LocalPredictiveGrantedAgentCount =
        LocalPredictive.GrantedAgentCount;
      Result.ParticleMetrics.LocalPredictiveYieldingAgentCount =
        LocalPredictive.YieldingAgentCount;
      Result.ParticleMetrics.LocalPredictiveInfeasibleAgentCount =
        LocalPredictive.InfeasibleAgentCount;
      Result.ParticleMetrics.LocalPredictiveQuantizationFailureCount =
        LocalPredictive.QuantizationFailureCount;
      Result.ParticleMetrics.LocalPredictiveJointValidationFailureCount =
        LocalPredictive.JointValidationFailureCount;
      Result.ParticleMetrics.LocalPredictiveJointComponentResolutionCount =
        LocalPredictive.JointComponentResolutionCount;
      Result.ParticleMetrics.LocalPredictiveCoherentTranslationComponentCount =
        LocalPredictive.CoherentTranslationComponentCount;
      Result.ParticleMetrics.LocalPredictiveCoherentTranslationAgentCount =
        LocalPredictive.CoherentTranslationAgentCount;
      Result.ParticleMetrics.LocalPredictiveCoherentTranslationMaxCmps =
        LocalPredictive.CoherentTranslationMaxCmps;
      Result.ParticleMetrics.LocalPredictiveJointPreferredRecoveryComponentCount =
        LocalPredictive.JointPreferredRecoveryComponentCount;
      Result.ParticleMetrics.LocalPredictiveJointPreferredRecoveryAgentCount =
        LocalPredictive.JointPreferredRecoveryAgentCount;
      Result.ParticleMetrics.LocalPredictiveJointPreferredRecoveryMaxGainCmps =
        LocalPredictive.JointPreferredRecoveryMaxGainCmps;
      Result.ParticleMetrics.LocalPredictiveEnvironmentConstraintCount =
        LocalPredictive.EnvironmentConstraintCount;
      Result.ParticleMetrics.LocalPredictiveGrantSwitchCount =
        LocalPredictive.GrantSwitchCount;
      Result.ParticleMetrics.LocalPredictiveBlockedAgeMax = LocalPredictive.BlockedAgeMax;
      Result.ParticleMetrics.LocalPredictiveInvalidStepCount =
        Pipeline->GetLocalPredictiveInvalidStepCount();
      Result.ParticleMetrics.SoftViolatingPairCount = Particle.SoftViolatingPairCount;
      Result.ParticleMetrics.SoftErrorCmP50 = Particle.SoftErrorCmP50;
      Result.ParticleMetrics.SoftErrorCmP95 = Particle.SoftErrorCmP95;
      Result.ParticleMetrics.SoftErrorCmMax = Particle.SoftErrorCmMax;
      Result.ParticleMetrics.HardPairViolationCount = Particle.HardPairViolationCount;
      Result.ParticleMetrics.SweptPairViolationCount = Particle.SweptPairViolationCount;
      Result.ParticleMetrics.CrossProfileHardViolationCount =
        Pipeline->GetCrossProfileHardViolationCount();
      Result.ParticleMetrics.CrossProfileSweptViolationCount =
        Pipeline->GetCrossProfileSweptViolationCount();
      if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
      {
        const FCrowdDemoCapabilityProfileSummary& Capability =
          Pipeline->GetCapabilityProfileSummary();
        Result.ParticleMetrics.bCapabilityProfilesValid = Capability.bValid ? 1 : 0;
        Result.ParticleMetrics.CapabilityProfileCount =
          Pipeline->GetCapabilityCohorts().Num();
        Result.ParticleMetrics.CapabilityMembershipHash = Capability.MembershipHash;
        Result.ParticleMetrics.CapabilityCohortRebuildCount =
          Pipeline->GetCapabilityCohortRebuildCount();
      }
      Result.ParticleMetrics.PressureInfluencedAgentCount = Particle.PressureInfluencedAgentCount;
      Result.ParticleMetrics.FirstInfluencedIterationMax = Particle.FirstInfluencedIterationMax;
      Result.ParticleMetrics.ParticleCorrectedAgentCount = Particle.CorrectedAgentCount;
      Result.ParticleMetrics.MaxAgentCorrectionCm = Particle.MaxAgentCorrectionCm;
      Result.ParticleMetrics.ObstaclePenetrationCount = Particle.ObstaclePenetrationCount;
      Result.ParticleMetrics.BoundsViolationCount = Particle.BoundsViolationCount;
      Result.ParticleMetrics.EnvironmentSoftContactCount = Particle.EnvironmentSoftContactCount;
      Result.ParticleMetrics.EnvironmentSoftAppliedAgentCount =
        Particle.EnvironmentSoftAppliedAgentCount;
      Result.ParticleMetrics.EnvironmentSoftErrorCmP50 = Particle.EnvironmentSoftErrorCmP50;
      Result.ParticleMetrics.EnvironmentSoftErrorCmP95 = Particle.EnvironmentSoftErrorCmP95;
      Result.ParticleMetrics.EnvironmentSoftErrorCmMax = Particle.EnvironmentSoftErrorCmMax;
      Result.ParticleMetrics.EnvironmentSoftRequestedCorrectionCmMax =
        Particle.EnvironmentSoftRequestedCorrectionCmMax;
      Result.ParticleMetrics.EnvironmentSoftRealizedCorrectionCmMax =
        Particle.EnvironmentSoftRealizedCorrectionCmMax;
      Result.ParticleMetrics.UnifiedHardConstraintCount = Particle.UnifiedHardConstraintCount;
      Result.ParticleMetrics.UnifiedHardResidualCmMax = Particle.UnifiedHardResidualCmMax;
      Result.ParticleMetrics.UnifiedHardInfeasibleCount = Particle.UnifiedHardInfeasibleCount;
      Result.ParticleMetrics.ParticleInvalidStepCount = Pipeline->GetParticleInvalidStepCount();
      Result.ParticleMetrics.ParticleGlobalFallbackStepCount = Pipeline->GetParticleGlobalFallbackStepCount();
      Result.ParticleMetrics.SettlingSteps = Pipeline->GetParticleSettlingSteps();
      Result.ParticleMetrics.ParticleSolverMsP95 = Pipeline->GetParticleSolverMsP95();
      Result.ParticleMetrics.Performance = Pipeline->BuildRoundPerformanceMetrics();
      Result.ParticleMetrics.ParticleCandidateHash = Pipeline->GetParticleCandidateStateHash();
      Result.ParticleMetrics.ParticleAppliedStateHash = Pipeline->GetParticleAppliedStateHash();
      if (Pipeline->IsTargetStabilityDiagnosticEnabled())
      {
        Pipeline->FinalizeTargetStabilityDiagnostic();
        const auto& Diagnostic = Pipeline->GetTargetStabilitySummary();
        auto& Metrics = Result.ParticleMetrics;
        Metrics.bTargetStabilityDiagnosticValid = Diagnostic.bValid ? 1 : 0;
        Metrics.TargetStabilityPrimaryCause = static_cast<int32>(Diagnostic.PrimaryCause);
        Metrics.TargetStabilityDiagnosticHash = Diagnostic.StableHash;
        Metrics.TargetStabilitySampleStepCount = Diagnostic.SampleStepCount;
        Metrics.TargetStabilityWindowStepCount = Diagnostic.WindowStepCount;
        Metrics.TargetStabilityAgentCount = Diagnostic.AgentCount;
        Metrics.TargetStabilityInsideBandMin = Diagnostic.InsideBandMin;
        Metrics.TargetStabilityCoverageMin = Diagnostic.CoverageMin;
        Metrics.TargetStabilityCoverageRequired = Diagnostic.CoverageRequired;
        Metrics.TargetStabilityContendedStepCount = Diagnostic.ContendedStepCount;
        Metrics.TargetStabilityContendedGroupCount = Diagnostic.ContendedGroupCount;
        Metrics.TargetStabilityMergeBlockedAgentCount = Diagnostic.MergeBlockedAgentCount;
        Metrics.TargetStabilityMergeBlockedMaxSteps = Diagnostic.MergeBlockedMaxConsecutiveSteps;
        Metrics.TargetStabilityTerminalChatterAgentCount = Diagnostic.TerminalChatterAgentCount;
        Metrics.TargetStabilityTerminalChatterCount = Diagnostic.TerminalChatterCount;
        Metrics.TargetStabilityAttractionRejectionCycleCount =
          Diagnostic.AttractionRejectionCycleCount;
        Metrics.TargetStabilityParticleSettledWindowCount =
          Diagnostic.ParticleSettledWindowCount;
        Metrics.TargetStabilityParticleSettledMaxSteps =
          Diagnostic.ParticleSettledMaxConsecutiveSteps;
        Metrics.TargetStabilityTargetRelativeSpeedCmpsP95 =
          Diagnostic.TargetRelativeSpeedCmpsP95;
        Metrics.TargetStabilityTargetRelativeSpeedCmpsMax =
          Diagnostic.TargetRelativeSpeedCmpsMax;
        Metrics.TargetStabilityPositionPeakToPeakCmP95 =
          Diagnostic.PositionPeakToPeakCmP95;
        Metrics.TargetStabilityPositionPeakToPeakCmMax =
          Diagnostic.PositionPeakToPeakCmMax;
        Metrics.TargetStabilityFirstWitnessStep = Diagnostic.FirstWitnessStep;
        Metrics.TargetStabilityFirstWitnessAgentId = Diagnostic.FirstWitnessAgentId;
        Metrics.TargetStabilityFirstWitnessNextCellKey = Diagnostic.FirstWitnessNextCellKey;
        Metrics.TargetStabilityFinalMissingRegionCount = Diagnostic.FinalMissingRegionCount;
        Metrics.TargetStabilityFirstMissingCohortKey = Diagnostic.FirstMissingCohortKey;
        Metrics.TargetStabilityFirstMissingRegionKey = Diagnostic.FirstMissingRegionKey;
        Metrics.TargetStabilityFirstMissingRegionStage =
          static_cast<int32>(Diagnostic.FirstMissingRegionStage);
        Metrics.TargetStabilityRegionDemandGapStepCount =
          Diagnostic.RegionDemandGapStepCount;
        Metrics.TargetStabilityRegionPlanQuotaGapStepCount =
          Diagnostic.RegionPlanQuotaGapStepCount;
        Metrics.TargetStabilityRegionGuidanceGapStepCount =
          Diagnostic.RegionGuidanceGapStepCount;
        Metrics.TargetStabilityRegionTerminalRetentionGapStepCount =
          Diagnostic.RegionTerminalRetentionGapStepCount;
        Metrics.TargetStabilityRegionTerminalEnterCount =
          Diagnostic.RegionTerminalEnterCount;
        Metrics.TargetStabilityRegionTerminalExitCount =
          Diagnostic.RegionTerminalExitCount;
        Metrics.TargetStabilityFinalSubQuantumSupplyAgentCount =
          Diagnostic.FinalSubQuantumSupplyAgentCount;
        Metrics.TargetStabilityFirstSubQuantumSupplyAgentId =
          Diagnostic.FirstSubQuantumSupplyAgentId;
        Metrics.TargetStabilityMinimumExecutableSpeedCmps =
          Diagnostic.MinimumExecutableSpeedCmps;
        FString RegionFacts;
        for (const auto& Region : Diagnostic.FinalRegions)
        {
          if (!RegionFacts.IsEmpty()) RegionFacts += TEXT(";");
          FString TerminalIds;
          for (const int32 AgentId : Region.TerminalAgentIds)
          {
            if (!TerminalIds.IsEmpty()) TerminalIds += TEXT(",");
            TerminalIds += FString::FromInt(AgentId);
          }
          FString SettleIds;
          for (const int32 AgentId : Region.TerminalSettleAgentIds)
          {
            if (!SettleIds.IsEmpty()) SettleIds += TEXT(",");
            SettleIds += FString::FromInt(AgentId);
          }
          FString SupplyIds;
          for (const int32 AgentId : Region.SupplyAgentIds)
          {
            if (!SupplyIds.IsEmpty()) SupplyIds += TEXT(",");
            SupplyIds += FString::FromInt(AgentId);
          }
          RegionFacts += FString::Printf(
            TEXT("%u/%d[f=%d cap=%d cur=%d des=%d def=%d sur=%d quota=%d consumed=%d guidance=%d terminal=%d settle=%d supply=%d terminal_ids=%s settle_ids=%s supply_ids=%s]"),
            Region.CohortKey, Region.RegionKey, Region.bFeasible ? 1 : 0,
            Region.AvailableCapacity, Region.CurrentPopulation,
            Region.DesiredPopulation, Region.Deficit, Region.Surplus,
            Region.PrimaryIncomingPlanQuota,
            Region.PrimaryIncomingConsumedQuota, Region.GuidanceTargetCount,
            Region.TerminalAgentIds.Num(), Region.TerminalSettleAgentIds.Num(),
            Region.SupplyAgentIds.Num(), *TerminalIds, *SettleIds, *SupplyIds);
        }
        TSet<int32> SupplyIds;
        for (const auto& Region : Diagnostic.FinalRegions)
          for (const int32 AgentId : Region.SupplyAgentIds) SupplyIds.Add(AgentId);
        TArray<int32> SupplyWitnessIds = SupplyIds.Array();
        SupplyWitnessIds.Sort();
        FCrowdDemoLocalPredictiveComponentFixture LocalFixture;
        if (!SupplyWitnessIds.IsEmpty()
          && Pipeline->BuildCurrentLocalPredictiveComponentFixture(
            SupplyWitnessIds, LocalFixture))
        {
          Metrics.bLocalPredictiveComponentFixtureValid = 1;
          Metrics.LocalPredictiveComponentFixtureHash = LocalFixture.StableHash;
          Metrics.LocalPredictiveComponentFixtureAgentCount = LocalFixture.Agents.Num();
          Metrics.LocalPredictiveComponentFixtureWitnessCount =
            LocalFixture.WitnessAgentIds.Num();
          for (const auto& Component : LocalFixture.Trace.Components)
            Metrics.LocalPredictiveFullJointSafeComponentCount +=
              Component.bFullJointVelocitySafe ? 1 : 0;
          UE_LOG(LogTemp, Display,
            TEXT("CrowdDemoLocalPredictiveComponentFixture role=server round_id=%d fixed_step=%d valid=1 agents=%d witnesses=%d components=%d full_joint_safe=%d hash=%u source=MassPipeline"),
            Result.RoundId, LocalFixture.FixedStepIndex, LocalFixture.Agents.Num(),
            LocalFixture.WitnessAgentIds.Num(), LocalFixture.Trace.Components.Num(),
            Metrics.LocalPredictiveFullJointSafeComponentCount,
            LocalFixture.StableHash);
          Pipeline->SetLocalPredictiveComponentFixture(MoveTemp(LocalFixture));
        }
        FString AgentFacts;
        for (const auto& Agent : Diagnostic.FinalAgents)
        {
          if (!SupplyIds.Contains(Agent.AgentId)) continue;
          if (!AgentFacts.IsEmpty()) AgentFacts += TEXT(";");
          AgentFacts += FString::Printf(
            TEXT("%d[cell=%d next=%d region=%d mode=%d desired=(%.0f,%.0f) local=(%.0f,%.0f) predict=(%.0f,%.0f) applied=(%.0f,%.0f) local_valid=%d granted=%d yielding=%d neighbors=%d constraints=%d blocked_age=%d]"),
            Agent.AgentId, Agent.CurrentCellKey, Agent.NextCellKey,
            Agent.CurrentRegionKey, static_cast<int32>(Agent.GuidanceMode),
            Agent.DesiredVelocity.X, Agent.DesiredVelocity.Y,
            Agent.LocalVelocity.X, Agent.LocalVelocity.Y,
            Agent.PredictedVelocity.X, Agent.PredictedVelocity.Y,
            Agent.AppliedVelocity.X, Agent.AppliedVelocity.Y,
            Agent.bLocalValid ? 1 : 0, Agent.bLocalGranted ? 1 : 0,
            Agent.bLocalYielding ? 1 : 0, Agent.LocalNeighborCount,
            Agent.LocalConstraintCount, Agent.LocalBlockedAgeSteps);
        }
        FString EdgeFacts;
        for (const auto& Edge : Diagnostic.FinalEdges)
        {
          if (Edge.AgentQuota <= 0) continue;
          if (!EdgeFacts.IsEmpty()) EdgeFacts += TEXT(";");
          EdgeFacts += FString::Printf(
            TEXT("%u:%d>%d(r%d>%d q=%d c=%d terminal=%d)"),
            Edge.CohortKey, Edge.FromCellKey, Edge.ToCellKey,
            Edge.FromRegionKey, Edge.ToRegionKey, Edge.AgentQuota,
            Edge.ConsumedQuota, Edge.bToTerminal ? 1 : 0);
        }
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTargetRegionCoverageDiagnostic role=server round_id=%d valid=%d samples=%d missing=%d first=%u/%d stage=%d demand_gap_steps=%d plan_gap_steps=%d guidance_gap_steps=%d retention_gap_steps=%d enters=%d exits=%d sub_quantum_supply=%d first_sub_quantum=%d minimum_executable_cmps=%.3f regions=%s supply_agents=%s edges=%s source=MassPipeline"),
          Result.RoundId, Diagnostic.bValid ? 1 : 0, Diagnostic.SampleStepCount,
          Diagnostic.FinalMissingRegionCount, Diagnostic.FirstMissingCohortKey,
          Diagnostic.FirstMissingRegionKey,
          static_cast<int32>(Diagnostic.FirstMissingRegionStage),
          Diagnostic.RegionDemandGapStepCount,
          Diagnostic.RegionPlanQuotaGapStepCount,
          Diagnostic.RegionGuidanceGapStepCount,
          Diagnostic.RegionTerminalRetentionGapStepCount,
          Diagnostic.RegionTerminalEnterCount, Diagnostic.RegionTerminalExitCount,
          Diagnostic.FinalSubQuantumSupplyAgentCount,
          Diagnostic.FirstSubQuantumSupplyAgentId,
          Diagnostic.MinimumExecutableSpeedCmps,
          *RegionFacts, *AgentFacts, *EdgeFacts);
        const auto& Counterfactual = Diagnostic.Counterfactual;
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoTargetRegionCounterfactual role=server round_id=%d valid=%d cohort=%u region=%d missing_steps=%d attachment_inflight_steps=%d attachment_recovered_guidance_steps=%d attachment_final_inflight=%d attachment_final_agent=%d attachment_remaining_edges=%d attachment_edges_min_max=%d,%d attachment_edge_transitions=%d,%d,%d attachment_final_relative_cmps=%.3f attachment_changes_final=%d terminal_hold_transitions=%d terminal_recovered_steps=%d terminal_final_held=%d terminal_cross_region_rejects=%d population_violations=%d terminal_restores_final=%d outcome=%d hash=%u source=MassPipeline"),
          Result.RoundId, Counterfactual.bValid ? 1 : 0,
          Counterfactual.CohortKey, Counterfactual.RegionKey,
          Counterfactual.BaselineMissingStepCount,
          Counterfactual.AttachmentObservedInFlightStepCount,
          Counterfactual.AttachmentRecoveredGuidanceStepCount,
          Counterfactual.AttachmentFinalInFlightAgentCount,
          Counterfactual.AttachmentFinalInFlightAgentId,
          Counterfactual.AttachmentFinalMinimumRemainingEdgeCount,
          Counterfactual.AttachmentRemainingEdgeCountMin,
          Counterfactual.AttachmentRemainingEdgeCountMax,
          Counterfactual.AttachmentRemainingEdgeDecreaseCount,
          Counterfactual.AttachmentRemainingEdgeIncreaseCount,
          Counterfactual.AttachmentRemainingEdgeUnchangedCount,
          Counterfactual.AttachmentFinalRelativeSpeedCmps,
          Counterfactual.bAttachmentChangesFinalGuidance ? 1 : 0,
          Counterfactual.TerminalEligibleHoldTransitionCount,
          Counterfactual.TerminalRecoveredCoverageStepCount,
          Counterfactual.TerminalFinalHeldAgentCount,
          Counterfactual.TerminalCrossRegionRejectCount,
          Counterfactual.PopulationConservationViolationCount,
          Counterfactual.bTerminalRestoresFinalObservedCoverage ? 1 : 0,
          static_cast<int32>(Counterfactual.Outcome),
          Counterfactual.StableHash);
      }
      if (Pipeline->GetRules().TargetRegionTransportSettings.bEnabled != 0)
      {
        Pipeline->FinalizeTargetRegionPlanLifecycleDiagnostic();
        auto& Metrics = Result.ParticleMetrics;
        if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
        {
          const auto& Cohorts = Pipeline->GetCapabilityCohorts();
          const FCrowdDemoCapabilityProfileSummary& CapabilitySummary =
            Pipeline->GetCapabilityProfileSummary();
          Metrics.bCapabilityProfilesValid = CapabilitySummary.bValid ? 1 : 0;
          Metrics.CapabilityProfileCount = Cohorts.Num();
          Metrics.CapabilityMembershipHash = CapabilitySummary.MembershipHash;
          Metrics.CapabilityCohortRebuildCount = Pipeline->GetCapabilityCohortRebuildCount();
          Metrics.CapabilityProfiles.Reset(Cohorts.Num());
          Metrics.TargetTransportTopologyHash = 2166136261u;
          Metrics.TargetTransportDemandHash = 2166136261u;
          Metrics.TargetTransportPlanHash = 2166136261u;
          Metrics.TargetTransportGuidanceHash = 2166136261u;
          Metrics.TargetTransportValidationHash = 2166136261u;
          TArray<float> SolverSamples;
          bool bAllValid = Cohorts.Num() > 0;
          for (const FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime : Cohorts)
          {
            FCrowdDemoCapabilityProfileMetrics& ProfileMetrics =
              Metrics.CapabilityProfiles.AddDefaulted_GetRef();
            ProfileMetrics.CapabilityProfileKey = Runtime.Cohort.CapabilityProfileKey;
            ProfileMetrics.DemandRegionPhaseOffset = Runtime.DemandRegionPhaseOffset;
            ProfileMetrics.AgentCount = Runtime.Cohort.AgentIds.Num();
            ProfileMetrics.FeasibleRegionCount = Runtime.Demand.FeasibleRegionCount;
            ProfileMetrics.InsideBandCount = Runtime.Demand.CurrentTerminalPopulation;
            const FVector2f TargetLocation(
              Pipeline->GetTargetFact().Location.X,
              Pipeline->GetTargetFact().Location.Y);
            const FVector2f TargetVelocity(
              Pipeline->GetTargetFact().Velocity.X,
              Pipeline->GetTargetFact().Velocity.Y);
            bool bHasOutsideProgress = false;
            for (const FCrowdDemoTargetRegionTransportAgent& Agent : Runtime.Agents)
            {
              const FVector2f Delta = Agent.Location - TargetLocation;
              const float Distance = Delta.Size();
              float ErrorCm = 0.0f;
              float ProgressCmps = 0.0f;
              if (Distance < Runtime.Cohort.Profile.NormalizedMinimumCenterDistanceCm)
              {
                ++ProfileMetrics.BelowBandCount;
                ErrorCm = Runtime.Cohort.Profile.NormalizedMinimumCenterDistanceCm - Distance;
                ProgressCmps = Distance > UE_SMALL_NUMBER
                  ? FVector2f::DotProduct(Agent.Velocity - TargetVelocity, Delta / Distance)
                  : 0.0f;
              }
              else if (Distance > Runtime.Cohort.Profile.NormalizedMaximumCenterDistanceCm)
              {
                ++ProfileMetrics.AboveBandCount;
                ErrorCm = Distance - Runtime.Cohort.Profile.NormalizedMaximumCenterDistanceCm;
                ProgressCmps = Distance > UE_SMALL_NUMBER
                  ? -FVector2f::DotProduct(Agent.Velocity - TargetVelocity, Delta / Distance)
                  : 0.0f;
              }
              else
              {
                ++ProfileMetrics.DistanceBandInsideCount;
                continue;
              }
              ProfileMetrics.OutsideBandErrorCmMax = FMath::Max(
                ProfileMetrics.OutsideBandErrorCmMax, ErrorCm);
              if (!bHasOutsideProgress)
              {
                ProfileMetrics.OutsideBandProgressCmpsMin = ProgressCmps;
                ProfileMetrics.OutsideBandProgressCmpsMax = ProgressCmps;
                bHasOutsideProgress = true;
              }
              else
              {
                ProfileMetrics.OutsideBandProgressCmpsMin = FMath::Min(
                  ProfileMetrics.OutsideBandProgressCmpsMin, ProgressCmps);
                ProfileMetrics.OutsideBandProgressCmpsMax = FMath::Max(
                  ProfileMetrics.OutsideBandProgressCmpsMax, ProgressCmps);
              }
            }
            for (const FCrowdDemoTargetRegionTransportAgent& Agent : Runtime.Agents)
            {
              const float Distance = (Agent.Location - TargetLocation).Size();
              const FCrowdDemoTargetRegionAgentDemandState* State =
                Runtime.Demand.AgentStates.FindByPredicate(
                  [&Agent](const FCrowdDemoTargetRegionAgentDemandState& Candidate)
                  {
                    return Candidate.AgentId == Agent.AgentId;
                  });
              const bool bInsideDistanceBand = Distance + 0.01f
                  >= Runtime.Cohort.Profile.NormalizedMinimumCenterDistanceCm
                && Distance <= Runtime.Cohort.Profile.NormalizedMaximumCenterDistanceCm + 0.01f;
              if (!State || bInsideDistanceBand)
              {
                continue;
              }
              const FCrowdDemoTargetRegionGuidanceResult* Guidance =
                Runtime.Guidance.FindByPredicate(
                  [&Agent](const FCrowdDemoTargetRegionGuidanceResult& Candidate)
                  {
                    return Candidate.AgentId == Agent.AgentId;
                  });
              const FCrowdDemoTargetPolarCell* Cell =
                Runtime.Topology.Cells.IsValidIndex(State->CurrentCellKey)
                  ? &Runtime.Topology.Cells[State->CurrentCellKey] : nullptr;
              const FCrowdDemoTargetPolarCell* NextCell = Guidance
                && Runtime.Topology.Cells.IsValidIndex(Guidance->NextCellKey)
                  ? &Runtime.Topology.Cells[Guidance->NextCellKey] : nullptr;
              const FCrowdDemoTargetDemandRegion* Region =
                Runtime.Demand.Regions.IsValidIndex(State->CurrentRegionKey)
                  ? &Runtime.Demand.Regions[State->CurrentRegionKey] : nullptr;
              UE_LOG(LogTemp, Display,
                TEXT("CrowdDemoT6TerminalWitness profile_key=%u phase=%d agent_id=%d target=(%.1f,%.1f) band=(%.1f,%.1f) location=(%.1f,%.1f) distance=%.1f velocity=(%.1f,%.1f) region=%d region_current=%d region_desired=%d terminal=%d supply=%d source_attached=%d cell=%d cell_band=%d cell_anchor=(%.1f,%.1f) cell_feasible=%d cell_terminal=%d guidance_mode=%d guidance_velocity=(%.1f,%.1f) next_cell=%d next_anchor=(%.1f,%.1f) source=MassPipeline"),
                Runtime.Cohort.CapabilityProfileKey, Runtime.DemandRegionPhaseOffset,
                Agent.AgentId, TargetLocation.X, TargetLocation.Y,
                Runtime.Cohort.Profile.NormalizedMinimumCenterDistanceCm,
                Runtime.Cohort.Profile.NormalizedMaximumCenterDistanceCm,
                Agent.Location.X, Agent.Location.Y, Distance,
                Agent.Velocity.X, Agent.Velocity.Y, State->CurrentRegionKey,
                Region ? Region->CurrentPopulation : INDEX_NONE,
                Region ? Region->DesiredPopulation : INDEX_NONE,
                State->bTerminal ? 1 : 0, State->bSupply ? 1 : 0,
                State->bSourceAttached ? 1 : 0,
                State->CurrentCellKey,
                Cell ? Cell->RadialBand : INDEX_NONE,
                Cell ? Cell->WorldAnchorCm.X : 0.0f,
                Cell ? Cell->WorldAnchorCm.Y : 0.0f,
                Cell && Cell->bFeasible ? 1 : 0,
                Cell && Cell->bTerminal ? 1 : 0,
                Guidance ? static_cast<int32>(Guidance->Mode) : INDEX_NONE,
                Guidance ? Guidance->DesiredVelocity.X : 0.0f,
                Guidance ? Guidance->DesiredVelocity.Y : 0.0f,
                Guidance ? Guidance->NextCellKey : INDEX_NONE,
                NextCell ? NextCell->WorldAnchorCm.X : 0.0f,
                NextCell ? NextCell->WorldAnchorCm.Y : 0.0f);
              FString CohortWitness;
              for (const FCrowdDemoTargetRegionTransportAgent& CohortAgent : Runtime.Agents)
              {
                const FCrowdDemoTargetRegionAgentDemandState* CohortState =
                  Runtime.Demand.AgentStates.FindByPredicate(
                    [&CohortAgent](const FCrowdDemoTargetRegionAgentDemandState& Candidate)
                    {
                      return Candidate.AgentId == CohortAgent.AgentId;
                    });
                const FCrowdDemoTargetRegionGuidanceResult* CohortGuidance =
                  Runtime.Guidance.FindByPredicate(
                    [&CohortAgent](const FCrowdDemoTargetRegionGuidanceResult& Candidate)
                    {
                      return Candidate.AgentId == CohortAgent.AgentId;
                    });
                if (!CohortWitness.IsEmpty()) CohortWitness += TEXT(";");
                CohortWitness += FString::Printf(
                  TEXT("%d:%.1f:%.1f:%.1f:%.1f:%d:%d:%d:%d:%.1f:%.1f:%d"),
                  CohortAgent.AgentId, CohortAgent.Location.X, CohortAgent.Location.Y,
                  CohortAgent.Velocity.X, CohortAgent.Velocity.Y,
                  CohortState ? CohortState->CurrentRegionKey : INDEX_NONE,
                  CohortState && CohortState->bTerminal ? 1 : 0,
                  CohortState && CohortState->bTerminalStay ? 1 : 0,
                  CohortState && CohortState->bSupply ? 1 : 0,
                  CohortGuidance ? CohortGuidance->DesiredVelocity.X : 0.0f,
                  CohortGuidance ? CohortGuidance->DesiredVelocity.Y : 0.0f,
                  CohortGuidance ? CohortGuidance->NextCellKey : INDEX_NONE);
              }
              FString DemandWitness;
              FString FeasibleRegionWitness;
              for (const FCrowdDemoTargetDemandRegion& DemandRegion : Runtime.Demand.Regions)
              {
                if (DemandRegion.bFeasible)
                {
                  if (!FeasibleRegionWitness.IsEmpty()) FeasibleRegionWitness += TEXT(",");
                  FeasibleRegionWitness += FString::FromInt(DemandRegion.StableRegionKey);
                }
                if (DemandRegion.CurrentPopulation <= 0
                  && DemandRegion.DesiredPopulation <= 0) continue;
                if (!DemandWitness.IsEmpty()) DemandWitness += TEXT(";");
                DemandWitness += FString::Printf(TEXT("%d:%d:%d"),
                  DemandRegion.StableRegionKey, DemandRegion.CurrentPopulation,
                  DemandRegion.DesiredPopulation);
              }
              UE_LOG(LogTemp, Display,
                TEXT("CrowdDemoT6CohortWitness profile_key=%u phase=%d feasible_regions=[%s] agents=[%s] regions=[%s] agent_fields=id,x,y,vx,vy,region,terminal,stay,supply,desired_x,desired_y,next_cell region_fields=region,current,desired source=MassPipeline"),
                Runtime.Cohort.CapabilityProfileKey, Runtime.DemandRegionPhaseOffset,
                *FeasibleRegionWitness, *CohortWitness, *DemandWitness);
              break;
            }
            ProfileMetrics.RoutedAgentCount = Runtime.Plan.RoutedAgentCount;
            ProfileMetrics.UnroutedAgentCount = Runtime.Plan.UnroutedAgentCount;
            for (const FCrowdDemoTargetDemandRegion& Region : Runtime.Demand.Regions)
            {
              ProfileMetrics.MaximumRegionPopulation = FMath::Max(
                ProfileMetrics.MaximumRegionPopulation, Region.CurrentPopulation);
              if (Region.bFeasible && Region.CurrentPopulation > 0)
              {
                ++ProfileMetrics.FeasibleRegionCoverageCount;
                ++Metrics.TargetTransportFeasibleRegionCoverageCount;
              }
            }
            ProfileMetrics.TopologyHash = Runtime.TopologyRoundHash;
            ProfileMetrics.DemandHash = Runtime.DemandRoundHash;
            ProfileMetrics.TransportHash = Runtime.TransportRoundHash;
            ProfileMetrics.GuidanceHash = Runtime.GuidanceRoundHash;
            ProfileMetrics.ValidationHash = Runtime.ValidationRoundHash;

            Metrics.TargetTransportFeasibleCellCount += Runtime.TopologySummary.FeasibleCellCount;
            Metrics.TargetTransportEdgeCount += Runtime.TopologySummary.EdgeCount;
            Metrics.TargetTransportFeasibleRegionCount += Runtime.Demand.FeasibleRegionCount;
            Metrics.TargetTransportInsideEffectiveBandCount += Runtime.Demand.CurrentTerminalPopulation;
            Metrics.TargetTransportDesiredPopulation += Runtime.Demand.DesiredPopulationTotal;
            Metrics.TargetTransportRoutedAgentCount += Runtime.Plan.RoutedAgentCount;
            Metrics.TargetTransportUnroutedAgentCount += Runtime.Plan.UnroutedAgentCount;
            Metrics.TargetTransportMaximumRegionPopulation = FMath::Max(
              Metrics.TargetTransportMaximumRegionPopulation,
              ProfileMetrics.MaximumRegionPopulation);
            Metrics.TargetTransportTotalPhysicalCost += Runtime.Plan.TotalPhysicalCost;
            Metrics.TargetTransportChangedQuotaUnitCount += Runtime.Plan.ChangedQuotaUnitCount;
            Metrics.TargetTransportPlanEpoch = FMath::Max(
              Metrics.TargetTransportPlanEpoch, Runtime.Plan.PlanEpoch);
            Metrics.TargetTransportPlanRebuildCount += Runtime.PlanRebuildCount;
            Metrics.TargetTransportInvalidStepCount += Runtime.InvalidStepCount;
            Metrics.TargetTransportValidationFailureCount += Runtime.ValidationFailureCount;
            Metrics.TargetGuidanceUnroutedStepCount += Runtime.GuidanceUnroutedStepCount;
            SolverSamples.Append(Runtime.SolverMillisecondsSamples);

            const uint32 Key = Runtime.Cohort.CapabilityProfileKey;
            Metrics.TargetTransportTopologyHash = FoldTargetHash(
              FoldTargetHash(Metrics.TargetTransportTopologyHash, Key),
              Runtime.TopologyRoundHash);
            Metrics.TargetTransportDemandHash = FoldTargetHash(
              FoldTargetHash(Metrics.TargetTransportDemandHash, Key),
              Runtime.DemandRoundHash);
            Metrics.TargetTransportPlanHash = FoldTargetHash(
              FoldTargetHash(Metrics.TargetTransportPlanHash, Key),
              Runtime.TransportRoundHash);
            Metrics.TargetTransportGuidanceHash = FoldTargetHash(
              FoldTargetHash(Metrics.TargetTransportGuidanceHash, Key),
              Runtime.GuidanceRoundHash);
            Metrics.TargetTransportValidationHash = FoldTargetHash(
              FoldTargetHash(Metrics.TargetTransportValidationHash, Key),
              Runtime.ValidationRoundHash);
            bAllValid = bAllValid && Runtime.bRoundValid
              && Runtime.TopologySummary.bValid && Runtime.Demand.bValid
              && Runtime.Plan.bValid && Runtime.GuidanceSummary.bValid;
          }
          if (Pipeline->IsTargetRegionPlanLifecycleDiagnosticEnabled())
          {
            const auto& Lifecycle = Pipeline->GetTargetRegionPlanLifecycleSummary();
            Metrics.bTargetPlanLifecycleDiagnosticValid = Lifecycle.bValid ? 1 : 0;
            Metrics.TargetPlanLifecycleSampleBoundaryCount = Lifecycle.SampleBoundaryCount;
            Metrics.TargetPlanLifecycleHash = Lifecycle.StableHash;
            Metrics.TargetTransportLifetimeRebuildCount = Lifecycle.LifetimeRebuildCount;
            Metrics.TargetTransportTargetRebuildCount = Lifecycle.TargetRevisionRebuildCount;
            Metrics.TargetTransportEnvironmentRebuildCount = Lifecycle.FeasibleGraphRebuildCount;
            Metrics.TargetTransportMembershipRebuildCount = Lifecycle.MembershipRebuildCount;
            Metrics.TargetTransportDemandSatisfiedRebuildCount = Lifecycle.DemandSatisfiedRebuildCount;
            Metrics.TargetTransportPathInvalidRebuildCount = Lifecycle.ExecutionInvalidRebuildCount;
            Metrics.TargetPlanLifecycleInitialInvalidRebuildCount = Lifecycle.InitialInvalidRebuildCount;
            Metrics.TargetPlanLifecycleCostOnlyGraphChangeCount = Lifecycle.CostOnlyGraphChangeCount;
            Metrics.TargetPlanLifecycleCellFeasibilityChangeCount = Lifecycle.CellFeasibilityChangeCount;
            Metrics.TargetPlanLifecycleEdgeSetChangeCount = Lifecycle.EdgeSetChangeCount;
            Metrics.TargetPlanLifecyclePrematureRebuildCount = Lifecycle.PrematureRebuildCount;
            Metrics.TargetPlanLifecycleActiveClaimCount = Lifecycle.ActiveClaimCount;
            Metrics.TargetPlanLifecycleGeometryEligibleClaimCount = Lifecycle.GeometryEligibleClaimCount;
            Metrics.TargetPlanLifecycleSupplyEligibleClaimCount =
              Lifecycle.SupplyEligibleClaimCount;
            Metrics.TargetPlanLifecycleNewPlanEligibleClaimCount = Lifecycle.NewPlanEligibleClaimCount;
            Metrics.TargetPlanLifecycleMigratedClaimCount = Lifecycle.MigratedClaimCount;
            Metrics.TargetPlanLifecycleCompletedAtReplacementClaimCount =
              Lifecycle.CompletedAtReplacementClaimCount;
            Metrics.TargetPlanLifecycleDroppedStillFeasibleClaimCount = Lifecycle.DroppedStillFeasibleClaimCount;
            Metrics.TargetPlanLifecycleStateMismatchCount = Lifecycle.ExecutionInvalid.StateMismatchCount;
            Metrics.TargetPlanLifecycleClaimOffEdgeCount = Lifecycle.ExecutionInvalid.ClaimOffEdgeCount;
            Metrics.TargetPlanLifecycleQuotaExceededCount = Lifecycle.ExecutionInvalid.QuotaExceededCount;
            Metrics.TargetPlanLifecycleSupplyWithoutOutgoingQuotaCount =
              Lifecycle.ExecutionInvalid.SupplyWithoutOutgoingQuotaCount;
            Metrics.TargetPlanLifecycleOtherInvalidCount = Lifecycle.ExecutionInvalid.OtherInvalidCount;
            Metrics.TargetPlanLifecycleAgeP50 = Lifecycle.PlanAgeStepsP50;
            Metrics.TargetPlanLifecycleAgeP95 = Lifecycle.PlanAgeStepsP95;
            Metrics.TargetPlanLifecycleAgeMax = Lifecycle.PlanAgeStepsMax;
            Metrics.bTargetPlanLifecycleFixtureValid = Lifecycle.bFixtureValid ? 1 : 0;
            Metrics.TargetPlanLifecycleFixtureStep = Lifecycle.FixtureStep;
            Metrics.TargetPlanLifecycleFixtureCohortKey = Lifecycle.FixtureCohortKey;
            Metrics.TargetPlanLifecycleFixtureReason = Lifecycle.FixtureReason;
            Metrics.TargetPlanLifecycleFixtureSelectionKind = Lifecycle.FixtureSelectionKind;
            Metrics.TargetPlanLifecycleFixtureRegionKey = Lifecycle.FixtureRegionKey;
            Metrics.TargetPlanLifecycleFixtureHash = Lifecycle.FixtureHash;
          }
          SolverSamples.Sort();
          if (SolverSamples.Num() > 0)
          {
            const int32 Index = FMath::Clamp(
              FMath::CeilToInt(static_cast<float>(SolverSamples.Num()) * 0.95f) - 1,
              0, SolverSamples.Num() - 1);
            Metrics.TargetTransportSolverMsP95 = SolverSamples[Index];
          }
          Metrics.bTargetRegionTransportValid = bAllValid
            && Metrics.bCapabilityProfilesValid != 0 ? 1 : 0;
          if (Pipeline->HasHeterogeneousTargetRegionCapabilities()
            && !ProjectHeterogeneousWorkerTargetMetrics(
              *Pipeline, Proxy, States, Metrics))
          {
            UE_LOG(LogTemp, Error,
              TEXT("VIOLATION CrowdDemoT6WorkerTargetProjectionInvalid step=%d agents=%d profiles=%d"),
              Pipeline->GetCurrentFixedStepIndex(), States.Num(),
              Pipeline->GetCapabilityCohorts().Num());
          }
        }
        else
        {
        const auto& Topology = Pipeline->GetTargetRegionTopologySummary();
        const auto& Demand = Pipeline->GetPreparedTargetRegionDemand();
        const auto& Plan = Pipeline->GetPreparedTargetRegionPlan();
        const auto& Guidance = Pipeline->GetTargetRegionGuidanceSummary();
        Metrics.bTargetRegionTransportValid = Pipeline->IsTargetRegionRoundValid()
          && Topology.bValid && Demand.bValid && Plan.bValid && Guidance.bValid ? 1 : 0;
        Metrics.TargetTransportFeasibleCellCount = Topology.FeasibleCellCount;
        Metrics.TargetTransportEdgeCount = Topology.EdgeCount;
        Metrics.TargetTransportFeasibleRegionCount = Demand.FeasibleRegionCount;
        Metrics.TargetTransportInsideEffectiveBandCount = Demand.CurrentTerminalPopulation;
        Metrics.TargetTransportDesiredPopulation = Demand.DesiredPopulationTotal;
        Metrics.TargetTransportRoutedAgentCount = Plan.RoutedAgentCount;
        Metrics.TargetTransportUnroutedAgentCount = Plan.UnroutedAgentCount;
        Metrics.TargetGuidanceUnroutedStepCount = Pipeline->GetTargetRegionGuidanceUnroutedStepCount();
        Metrics.TargetGuidanceUnroutedAgentSampleCount = Pipeline->GetTargetRegionGuidanceUnroutedAgentSampleCount();
        Metrics.TargetGuidanceUnroutedAgentMax = Pipeline->GetTargetRegionGuidanceUnroutedAgentMax();
        Metrics.TargetGuidanceFirstFailureStep = Pipeline->GetTargetRegionGuidanceFirstFailureStep();
        Metrics.TargetGuidanceFirstFailureAgentId = Pipeline->GetTargetRegionGuidanceFirstFailureAgentId();
        Metrics.TargetTransportInvalidStepCount = Pipeline->GetTargetRegionInvalidStepCount();
        Metrics.TargetTransportValidationFailureCount = Pipeline->GetTargetRegionValidationFailureCount();
        Metrics.TargetTransportValidationHash = Pipeline->GetTargetRegionValidationRoundHash();
        Metrics.bTargetTransportFailureFixtureValid = Pipeline->HasTargetRegionFailureFixture() ? 1 : 0;
        Metrics.TargetTransportFailureFixtureStep = Pipeline->GetTargetRegionFailureFixtureStep();
        Metrics.TargetTransportFailureFixtureKind = Pipeline->GetTargetRegionFailureFixtureKind();
        Metrics.TargetTransportFailureFixtureAgentId = Pipeline->GetTargetRegionFailureFixtureAgentId();
        Metrics.TargetTransportFailureFixtureCellKey = Pipeline->GetTargetRegionFailureFixtureCellKey();
        Metrics.TargetTransportFailureFixtureHash = Pipeline->GetTargetRegionFailureFixtureHash();
        Metrics.TargetTransportTotalPhysicalCost = Plan.TotalPhysicalCost;
        Metrics.TargetTransportChangedQuotaUnitCount = Plan.ChangedQuotaUnitCount;
        Metrics.TargetTransportPlanEpoch = Plan.PlanEpoch;
        TSet<int32> RawRegions;
        int32 FeasibleCoverage = 0;
        int32 MaximumPopulation = 0;
        const auto Settings = MakeTargetRegionTransportSettings(
          Pipeline->GetRules(), Pipeline->GetTargetFact());
        for (const auto& Agent : Pipeline->GetPreparedTargetRegionAgents())
        {
          const FVector2f Offset = Agent.Location - Settings.TargetLocation;
          const float Distance = Offset.Size();
          if (Distance + 0.01f < Settings.MinimumCenterDistanceCm
            || Distance > Settings.MaximumCenterDistanceCm + 0.01f) continue;
          float Angle = FMath::Atan2(Offset.Y, Offset.X);
          if (Angle < 0.0f) Angle += 2.0f * PI;
          RawRegions.Add(FMath::Clamp(FMath::FloorToInt(Angle / (2.0f * PI)
            * Settings.DemandRegionCount), 0, Settings.DemandRegionCount - 1));
        }
        for (const auto& Region : Demand.Regions)
        {
          FeasibleCoverage += Region.bFeasible && Region.CurrentPopulation > 0 ? 1 : 0;
          MaximumPopulation = FMath::Max(MaximumPopulation, Region.CurrentPopulation);
        }
        Metrics.TargetTransportRawRegionCoverageCount = RawRegions.Num();
        Metrics.TargetTransportFeasibleRegionCoverageCount = FeasibleCoverage;
        Metrics.TargetTransportMaximumRegionPopulation = MaximumPopulation;
        Metrics.TargetTransportPlanRebuildCount = Pipeline->GetTargetRegionPlanRebuildCount();
        Metrics.TargetTransportLifetimeRebuildCount = Pipeline->GetTargetRegionLifetimeRebuildCount();
        Metrics.TargetTransportTargetRebuildCount = Pipeline->GetTargetRegionTargetRebuildCount();
        Metrics.TargetTransportEnvironmentRebuildCount = Pipeline->GetTargetRegionEnvironmentRebuildCount();
        Metrics.TargetTransportMembershipRebuildCount = Pipeline->GetTargetRegionMembershipRebuildCount();
        Metrics.TargetTransportDemandSatisfiedRebuildCount = Pipeline->GetTargetRegionDemandSatisfiedRebuildCount();
        Metrics.TargetTransportPathInvalidRebuildCount = Pipeline->GetTargetRegionPathInvalidRebuildCount();
        Metrics.TargetTransportSolverMsP95 = Pipeline->GetTargetRegionSolverMsP95();
        Metrics.TargetTransportTopologyHash = Pipeline->GetTargetRegionTopologyRoundHash();
        Metrics.TargetTransportDemandHash = Pipeline->GetTargetRegionDemandRoundHash();
        Metrics.TargetTransportPlanHash = Pipeline->GetTargetRegionTransportRoundHash();
        Metrics.TargetTransportGuidanceHash = Pipeline->GetTargetRegionGuidanceRoundHash();
        }
      }
      const FCrowdDemoParticleFailureFixture& Fixture = Pipeline->GetParticleFailureFixture();
      Result.ParticleMetrics.ParticleFailureFixtureStep = Fixture.FixedStepIndex;
      Result.ParticleMetrics.ParticleFailureMinAgentId = Fixture.MinAgentId;
      Result.ParticleMetrics.ParticleFailureMaxAgentId = Fixture.MaxAgentId;
      Result.ParticleMetrics.ParticleFailureFixtureHash = Fixture.FixtureHash;
      Result.ParticleMetrics.RollbackSnapshotHitCount =
        Pipeline->GetSoftPressureRollbackSnapshotHitCount();
      Result.ParticleMetrics.RollbackSnapshotMissCount =
        Pipeline->GetSoftPressureRollbackSnapshotMissCount();
      Result.ParticleMetrics.RollbackAgentMismatchCount =
        Pipeline->GetSoftPressureRollbackAgentMismatchCount();
      Result.ParticleMetrics.RollbackReplayedStepCount =
        Pipeline->GetSoftPressureRollbackReplayedStepCount();
      if (Pipeline->IsOpenSpawnRelaxation())
      {
        const auto& T1 = Pipeline->GetOpenSpawnRelaxationRuntime();
        auto& Metrics = Result.ParticleMetrics;
        Metrics.bT1Valid = T1.bValid ? 1 : 0;
        Metrics.T1Phase = static_cast<uint8>(T1.Phase);
        Metrics.T1PhaseTransitionCount = T1.PhaseTransitionCount;
        Metrics.T1BatchActivationCount = T1.BatchActivationCount;
        Metrics.T1InsertedAgentId = T1.SourceAgentId;
        Metrics.T1RemovedAgentId = T1.RemovedAgentId;
        Metrics.T1PressurePropagationLayerMax = T1.PressurePropagationLayerMax;
        Metrics.T1LayerAgentCounts = T1.LayerAgentCounts;
        Metrics.T1ActiveCountTransitions = T1.ActiveCountTransitions;
        Metrics.T1InsertSettlingStep = T1.InsertSettlingStep;
        Metrics.T1PostRemovalSettlingStep = T1.PostRemovalSettlingStep;
        Metrics.T1OldLayoutReturnedAgentCount = T1.OldLayoutReturnedAgentCount;
        Metrics.T1NewEquilibriumDisplacedAgentCount = T1.NewEquilibriumDisplacedAgentCount;
        Metrics.T1ExternalPreferredNonzeroCount = T1.ExternalPreferredNonzeroCount;
        Metrics.T1ParticipationHash = T1.ParticipationHash;
        Metrics.T1PropagationHash = T1.PropagationHash;
        Metrics.T1PhaseHash = T1.PhaseHash;
        for (int32 Index = 0; Index < T1.Agents.Num(); ++Index)
        {
          Metrics.T1ActiveAgentCount += T1.Agents[Index].bParticleActive ? 1 : 0;
          Metrics.T1InfluencedAgentCount +=
            T1.PropagationLayersByAgent.IsValidIndex(Index)
            && T1.PropagationLayersByAgent[Index] > 0 ? 1 : 0;
        }
      }
      if (Pipeline->IsOpenCohortMovement())
      {
        const auto& Layout = Pipeline->GetOpenCohortMovementLayout();
        const auto& Progress = Pipeline->GetOpenCohortMovementProgress();
        const auto& Route = Pipeline->GetSoftPressureRouteDiagnosticSummary();
        auto& Metrics = Result.ParticleMetrics;
        Metrics.bT2Valid = Layout.bValid && Progress.bValid
          && Progress.CurrentUnroutedAgentIds.IsEmpty() ? 1 : 0;
        Metrics.T2LayoutHash = Layout.LayoutHash;
        Metrics.T2RouteDiagnosticHash = Route.StableHash;
        Metrics.T2ProgressHash = Progress.ProgressHash;
        Metrics.T2FlowContractViolationCount = Route.FlowContractViolationCount;
        Metrics.T2FinalDeadlockAgentCount = Route.CorridorFinalDeadlockAgentCount;
        Metrics.T2FlowApproachEnteredCount = Progress.FlowApproachEnteredAgentIds.Num();
        Metrics.T2TransportHandoffCount = Progress.TransportHandoffAgentIds.Num();
        Metrics.T2InsideEffectiveBandCount =
          Progress.InsideEffectiveBandAgentIds.Num();
        Metrics.T2FeasibleRegionCount = Metrics.TargetTransportFeasibleRegionCount;
        Metrics.T2FeasibleRegionCoverageCount =
          Metrics.TargetTransportFeasibleRegionCoverageCount;
        Metrics.T2PlanUnroutedCount =
          Progress.CurrentUnroutedAgentIds.Num();
        Metrics.T2GuidanceUnroutedCount =
          Progress.CurrentUnroutedAgentIds.Num();
        Metrics.T2TransportValidationFailureCount =
          Metrics.TargetTransportValidationFailureCount;
        Metrics.T2TerminalSettledCount = Progress.TerminalSettledAgentIds.Num();
        Metrics.T2TerminalSettledStep = Progress.TerminalSettledStep;
        TArray<float> ForwardP50;
        TArray<float> ForwardP95;
        for (const auto& Agent : Route.Agents)
        {
          ForwardP50.Add(Agent.AppliedForwardCmpsP50);
          ForwardP95.Add(Agent.AppliedForwardCmpsP95);
        }
        auto Percentile = [](TArray<float>& Values, const float Fraction)
        {
          if (Values.IsEmpty()) return 0.0f;
          Values.Sort();
          return Values[FMath::Clamp(
            FMath::CeilToInt(Fraction * static_cast<float>(Values.Num())) - 1,
            0, Values.Num() - 1)];
        };
        Metrics.T2AppliedForwardCmpsP50 = Percentile(ForwardP50, 0.5f);
        Metrics.T2AppliedForwardCmpsP95 = Percentile(ForwardP95, 0.95f);
      }
      if (Pipeline->IsBidirectionalSwap())
      {
        const auto& Layout = Pipeline->GetBidirectionalSwapLayout();
        const auto& Progress = Pipeline->GetBidirectionalSwapProgress();
        auto& Metrics = Result.ParticleMetrics;
        Metrics.T3LayoutHash = Layout.LayoutHash;
        Metrics.T3ProgressHash = Progress.ProgressHash;
        Metrics.T3FinalDeadlockAgentCount = Progress.FinalDeadlockAgentIds.Num();
        Metrics.T3UnreachableSampleCount = Progress.UnreachableSampleCount;
        Metrics.T3LastFixedStep = Progress.LastFixedStepIndex;
        for (const auto& Agent : Layout.Agents)
        {
          const bool bNorthbound = Agent.CohortKey
            == FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey;
          if (bNorthbound) ++Metrics.T3Cohort0AgentCount;
          else if (Agent.CohortKey
            == FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey)
            ++Metrics.T3Cohort1AgentCount;
          if (Progress.CenterCrossedAgentIds.Contains(Agent.AgentId))
          {
            if (bNorthbound) ++Metrics.T3Cohort0CenterCrossedCount;
            else ++Metrics.T3Cohort1CenterCrossedCount;
          }
          if (Progress.CompletedAgentIds.Contains(Agent.AgentId))
          {
            if (bNorthbound) ++Metrics.T3Cohort0CompletedCount;
            else ++Metrics.T3Cohort1CompletedCount;
          }
        }
        Metrics.T3CompletedCount = Metrics.T3Cohort0CompletedCount
          + Metrics.T3Cohort1CompletedCount;
        Metrics.T3ThroughputDifference = FMath::Abs(
          Metrics.T3Cohort0CompletedCount - Metrics.T3Cohort1CompletedCount);
        for (const TPair<int32, int32>& Pair : Progress.CompletionStepByAgentId)
          Metrics.T3CompletionStepMax = FMath::Max(
            Metrics.T3CompletionStepMax, Pair.Value);
        const FCrowdDemoSharedFlowField* Cohort0Field =
          Pipeline->FindBidirectionalSwapFlowField(
            FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey);
        const FCrowdDemoSharedFlowField* Cohort1Field =
          Pipeline->FindBidirectionalSwapFlowField(
            FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey);
        Metrics.T3Cohort0FlowHash = Cohort0Field ? Cohort0Field->BuildHash : 0;
        Metrics.T3Cohort1FlowHash = Cohort1Field ? Cohort1Field->BuildHash : 0;
        Metrics.bT3Valid = Layout.bValid && Progress.bValid
          && Cohort0Field && Cohort0Field->IsValid()
          && Cohort1Field && Cohort1Field->IsValid() ? 1 : 0;
      }
      if (Pipeline->IsValidCorridorTransit())
      {
        const auto& Layout = Pipeline->GetValidCorridorTransitLayout();
        const auto& Progress = Pipeline->GetValidCorridorTransitProgress();
        auto& Metrics = Result.ParticleMetrics;
        Metrics.T4LayoutHash = Layout.LayoutHash;
        Metrics.T4FlowHash = Pipeline->GetSharedFlowField().BuildHash;
        Metrics.T4ProgressHash = Progress.ProgressHash;
        Metrics.T4WallPassedCount = Progress.WallPassedAgentIds.Num();
        Metrics.T4CorridorExitedCount = Progress.CorridorExitedAgentIds.Num();
        Metrics.T4CompletedCount = Progress.CompletedAgentIds.Num();
        Metrics.T4FinalSettledCount = Progress.FinalSettledAgentIds.Num();
        Metrics.T4FinalDeadlockAgentCount = Progress.FinalDeadlockAgentIds.Num();
        Metrics.T4UnreachableSampleCount = Progress.UnreachableSampleCount;
        Metrics.T4LastFixedStep = Progress.LastFixedStepIndex;
        for (const TPair<int32, int32>& Pair : Progress.CompletionStepByAgentId)
          Metrics.T4CompletionStepMax = FMath::Max(
            Metrics.T4CompletionStepMax, Pair.Value);
        Metrics.T4GroupCompletionStep = Progress.GroupCompletionStep;
        Metrics.T4GroupSettledStep = Progress.GroupSettledStep;
        Metrics.bT4Valid = Layout.bValid && Progress.bValid
          && Pipeline->GetSharedFlowField().IsValid()
          && Progress.CompletedAgentIds.Num()
            == FCrowdDemoValidCorridorTransitKernel::AgentCount
          && Progress.FinalSettledAgentIds.Num()
            == FCrowdDemoValidCorridorTransitKernel::AgentCount
          && Progress.GroupSettledStep != INDEX_NONE ? 1 : 0;
      }
      if (Pipeline->IsHeterogeneousTransit())
      {
        const auto& Layout = Pipeline->GetValidCorridorTransitLayout();
        const auto& Progress = Pipeline->GetValidCorridorTransitProgress();
        auto& Metrics = Result.ParticleMetrics;
        Metrics.T6TransitLayoutHash = Layout.LayoutHash;
        Metrics.T6TransitFlowHash = Pipeline->GetSharedFlowField().BuildHash;
        Metrics.T6TransitProgressHash = Progress.ProgressHash;
        Metrics.T6TransitWallPassedCount = Progress.WallPassedAgentIds.Num();
        Metrics.T6TransitCorridorExitedCount = Progress.CorridorExitedAgentIds.Num();
        Metrics.T6TransitCompletedCount = Progress.CompletedAgentIds.Num();
        Metrics.T6TransitFinalSettledCount = Progress.FinalSettledAgentIds.Num();
        Metrics.T6TransitFinalDeadlockAgentCount = Progress.FinalDeadlockAgentIds.Num();
        Metrics.T6TransitUnreachableSampleCount = Progress.UnreachableSampleCount;
        Metrics.T6TransitLastFixedStep = Progress.LastFixedStepIndex;
        for (const TPair<int32, int32>& Pair : Progress.CompletionStepByAgentId)
          Metrics.T6TransitCompletionStepMax = FMath::Max(
            Metrics.T6TransitCompletionStepMax, Pair.Value);
        Metrics.T6TransitGroupCompletionStep = Progress.GroupCompletionStep;
        Metrics.T6TransitGroupSettledStep = Progress.GroupSettledStep;
        Metrics.T6TransitTargetHandoffStep = Progress.GroupCompletionStep;
        Metrics.T6TransitTargetInsideBandCount =
          Metrics.TargetTransportInsideEffectiveBandCount;
        Metrics.T6TransitTargetCoverageCount =
          Metrics.TargetTransportFeasibleRegionCoverageCount;
        bool bTargetCapabilitySatisfied = Metrics.bTargetRegionTransportValid != 0;
        for (int32 ProfileIndex = 0;
          ProfileIndex < Metrics.CapabilityProfiles.Num(); ++ProfileIndex)
        {
          const FCrowdDemoCapabilityProfileMetrics& Profile =
            Metrics.CapabilityProfiles[ProfileIndex];
          const int32 RequiredCoverage = FMath::Min(
            Profile.AgentCount, Profile.FeasibleRegionCount);
          Metrics.T6TransitTargetRequiredCoverageCount += RequiredCoverage;
          bTargetCapabilitySatisfied = bTargetCapabilitySatisfied
            && Profile.InsideBandCount == Profile.AgentCount
            && Profile.FeasibleRegionCoverageCount == RequiredCoverage
            && Profile.UnroutedAgentCount == 0;
          if (Pipeline->GetCapabilityCohorts().IsValidIndex(ProfileIndex))
            Metrics.T6TransitTargetEngagedHoldCount += Pipeline->GetCapabilityCohorts()[
              ProfileIndex].TargetEngagedHoldAgentIds.Num();
        }
        Metrics.bT6TransitValid = Layout.bValid && Progress.bValid
          && Pipeline->GetSharedFlowField().IsValid()
          && Pipeline->GetCapabilityProfileSummary().bValid
          && Progress.CompletedAgentIds.Num()
            == FCrowdDemoValidCorridorTransitKernel::AgentCount
          && Metrics.T6TransitTargetInsideBandCount
            == FCrowdDemoValidCorridorTransitKernel::AgentCount
          && bTargetCapabilitySatisfied ? 1 : 0;
      }
      if (Pipeline->IsSoftPressureRouteDiagnosticEnabled())
      {
        const auto& Route = Pipeline->GetSoftPressureRouteDiagnosticSummary();
        auto& Metrics = Result.ParticleMetrics.RouteMetrics;
        Metrics.bValid = Route.bValid ? 1 : 0;
        Metrics.DiagnosticHash = Route.StableHash;
        Metrics.SelectedBranch = static_cast<int32>(Route.SelectedBranch);
        Metrics.SelectedAgentCount = Route.SelectedAgentCount;
        Metrics.NeverReachedAgentCount = Route.NeverReachedAgentCount;
        Metrics.ReachedThenLeftAgentCount = Route.ReachedThenLeftAgentCount;
        Metrics.GoalBoundaryTransitionCount = Route.GoalBoundaryTransitionCount;
        Metrics.ZeroToMaxSpeedTransitionCount = Route.ZeroToMaxSpeedTransitionCount;
        Metrics.MaxToZeroSpeedTransitionCount = Route.MaxToZeroSpeedTransitionCount;
        Metrics.CorridorEverStalledAgentCount = Route.CorridorEverStalledAgentCount;
        Metrics.CorridorFinalDeadlockAgentCount = Route.CorridorFinalDeadlockAgentCount;
        Metrics.FlowContractViolationCount = Route.FlowContractViolationCount;
        Metrics.FailureOwnedFlowContractViolationCount =
          Route.FailureOwnedFlowContractViolationCount;
        Metrics.CorridorFailureAgentCount = Route.CorridorFailureAgentCount;
        Metrics.GoalFailureAgentCount = Route.GoalFailureAgentCount;
        Metrics.InsideGoalCountP50 = Route.InsideGoalCountP50;
        Metrics.InsideGoalCountP95 = Route.InsideGoalCountP95;
        Metrics.InsideGoalCountMax = Route.InsideGoalCountMax;
        Metrics.NeverReachedDistanceCmP50 = Route.NeverReachedDistanceCmP50;
        Metrics.NeverReachedDistanceCmP95 = Route.NeverReachedDistanceCmP95;
        Metrics.NeverReachedDistanceCmMax = Route.NeverReachedDistanceCmMax;
        Metrics.NeverReachedDesiredForwardCmpsP50 = Route.NeverReachedDesiredForwardCmpsP50;
        Metrics.NeverReachedDesiredForwardCmpsP95 = Route.NeverReachedDesiredForwardCmpsP95;
        Metrics.NeverReachedAppliedForwardCmpsP50 = Route.NeverReachedAppliedForwardCmpsP50;
        Metrics.NeverReachedAppliedForwardCmpsP95 = Route.NeverReachedAppliedForwardCmpsP95;
        Metrics.NeverReachedSoftOppositionCmpsP50 = Route.NeverReachedSoftOppositionCmpsP50;
        Metrics.NeverReachedSoftOppositionCmpsP95 = Route.NeverReachedSoftOppositionCmpsP95;
        Metrics.BaselineNeverReachedForwardCmps =
          Route.Counterfactual.BaselineNeverReachedForwardCmps;
        Metrics.StickyNeverReachedForwardCmps =
          Route.Counterfactual.StickyNeverReachedForwardCmps;
        Metrics.SoftDisabledNeverReachedForwardCmps =
          Route.Counterfactual.SoftDisabledNeverReachedForwardCmps;
        Metrics.bStickyCounterfactualValid = Route.Counterfactual.bStickyValid ? 1 : 0;
        Metrics.bSoftDisabledCounterfactualValid =
          Route.Counterfactual.bSoftDisabledValid ? 1 : 0;
        for (const auto& AgentResult : Route.Agents)
        {
          const auto& Agent = AgentResult.Agent;
          UE_LOG(LogTemp, Display,
            TEXT("CrowdDemoSoftPressureRouteAgent role=%s round_id=%d agent=%d final=(%.3f,%.3f) goal_final=%.3f goal_min=%.3f inside=%d ever_reached=%d reached_then_left=%d transitions=%d zero_to_max=%d max_to_zero=%d wall_step=%d corridor_step=%d turn_step=%d flow_cell=%d flow_key=%d flow_status=%d integration=%d desired=(%.3f,%.3f) predicted=(%.3f,%.3f) applied=(%.3f,%.3f) desired_forward=%.3f applied_forward=%.3f pair_soft_requested=(%.3f,%.3f) pair_soft_realized=(%.3f,%.3f) environment_soft_requested=(%.3f,%.3f) environment_soft_realized=(%.3f,%.3f) unified_hard=(%.3f,%.3f) total_correction=(%.3f,%.3f) component=%d reached_neighbors=%d non_reached_neighbors=%d low_speed_current=%d low_speed_max=%d ever_stalled=%d final_deadlock=%d flow_contract_violations=%d flow_violation_first_step=%d flow_violation_mask=%d flow_violation_predict_distance_cm=%.3f source=MassPipeline"),
            World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
            Result.RoundId, Agent.AgentId, Agent.FinalLocation.X, Agent.FinalLocation.Y,
            Agent.FinalGoalDistanceCm, Agent.MinimumGoalDistanceCm,
            Agent.bCurrentInsideGoal ? 1 : 0, Agent.bEverReachedGoal ? 1 : 0,
            Agent.ReachedThenLeftCount, Agent.GoalBoundaryTransitionCount,
            Agent.ZeroToMaxSpeedTransitionCount, Agent.MaxToZeroSpeedTransitionCount,
            Agent.FirstWallStep, Agent.FirstCorridorStep, Agent.FirstTurnStep,
            Agent.FinalFlowCellIndex, Agent.FinalFlowStableCellKey,
            static_cast<int32>(Agent.FinalFlowStatus), Agent.FinalIntegrationCost,
            Agent.FinalDesiredVelocity.X, Agent.FinalDesiredVelocity.Y,
            Agent.FinalPredictedVelocity.X, Agent.FinalPredictedVelocity.Y,
            Agent.FinalAppliedVelocity.X, Agent.FinalAppliedVelocity.Y,
            Agent.FinalDesiredForwardCmps, Agent.FinalAppliedForwardCmps,
            Agent.FinalPairSoftRequestedCorrection.X, Agent.FinalPairSoftRequestedCorrection.Y,
            Agent.FinalPairSoftRealizedCorrection.X, Agent.FinalPairSoftRealizedCorrection.Y,
            Agent.FinalEnvironmentSoftRequestedCorrection.X,
            Agent.FinalEnvironmentSoftRequestedCorrection.Y,
            Agent.FinalEnvironmentSoftRealizedCorrection.X,
            Agent.FinalEnvironmentSoftRealizedCorrection.Y,
            Agent.FinalUnifiedHardCorrection.X, Agent.FinalUnifiedHardCorrection.Y,
            Agent.FinalTotalParticleCorrection.X, Agent.FinalTotalParticleCorrection.Y,
            AgentResult.ConstraintComponentSize, AgentResult.ReachedNeighborCount,
            AgentResult.NonReachedNeighborCount, Agent.CurrentLowSpeedSteps,
            Agent.MaxLowSpeedSteps, Agent.bEverCorridorStalled ? 1 : 0,
            Agent.bFinalCorridorDeadlock ? 1 : 0, Agent.FlowContractViolationCount,
            Agent.FirstFlowContractViolationStep, Agent.FirstFlowContractViolationMask,
            Agent.FirstFlowContractViolationPredictDistanceCm);
        }
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoSoftPressureRouteSummary role=%s round_id=%d valid=%d hash=%u branch=%d selected=%d never_reached=%d reached_then_left=%d transitions=%d zero_to_max=%d max_to_zero=%d inside_goal_p50=%.3f inside_goal_p95=%.3f inside_goal_max=%.3f never_distance_p50=%.3f never_distance_p95=%.3f never_distance_max=%.3f never_desired_forward_p50=%.3f never_desired_forward_p95=%.3f never_applied_forward_p50=%.3f never_applied_forward_p95=%.3f never_soft_opposition_p50=%.3f never_soft_opposition_p95=%.3f ever_stalled=%d final_deadlock=%d flow_contract_violations=%d failure_owned_flow_violations=%d corridor_failures=%d goal_failures=%d baseline_forward=%.3f sticky_forward=%.3f sticky_valid=%d soft_disabled_forward=%.3f soft_disabled_valid=%d source=MassPipeline"),
          World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
          Result.RoundId, Metrics.bValid, Metrics.DiagnosticHash, Metrics.SelectedBranch,
          Metrics.SelectedAgentCount, Metrics.NeverReachedAgentCount,
          Metrics.ReachedThenLeftAgentCount, Metrics.GoalBoundaryTransitionCount,
          Metrics.ZeroToMaxSpeedTransitionCount, Metrics.MaxToZeroSpeedTransitionCount,
          Metrics.InsideGoalCountP50, Metrics.InsideGoalCountP95, Metrics.InsideGoalCountMax,
          Metrics.NeverReachedDistanceCmP50, Metrics.NeverReachedDistanceCmP95,
          Metrics.NeverReachedDistanceCmMax, Metrics.NeverReachedDesiredForwardCmpsP50,
          Metrics.NeverReachedDesiredForwardCmpsP95,
          Metrics.NeverReachedAppliedForwardCmpsP50,
          Metrics.NeverReachedAppliedForwardCmpsP95,
          Metrics.NeverReachedSoftOppositionCmpsP50,
          Metrics.NeverReachedSoftOppositionCmpsP95,
          Metrics.CorridorEverStalledAgentCount,
          Metrics.CorridorFinalDeadlockAgentCount, Metrics.FlowContractViolationCount,
          Metrics.FailureOwnedFlowContractViolationCount,
          Metrics.CorridorFailureAgentCount, Metrics.GoalFailureAgentCount,
          Metrics.BaselineNeverReachedForwardCmps,
          Metrics.StickyNeverReachedForwardCmps, Metrics.bStickyCounterfactualValid,
          Metrics.SoftDisabledNeverReachedForwardCmps,
          Metrics.bSoftDisabledCounterfactualValid);
      }
    }
    if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
    {
      Result.SharedFlowMetrics = Pipeline->BuildSharedFlowMetrics(States);
    }
    if (Pipeline->IsRangedProjectileCombat())
      Result.ProjectileMetrics = Pipeline->BuildProjectileMetrics();
    Pipeline->MarkRoundResultBuilt(Result.CheckpointRevision);
    Pipeline->EnqueueOutgoingRoundResult(MoveTemp(Result));
  }

  Pipeline->LogStageOnce(
    Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("11_checkpoint_publisher")
      : TEXT("08_checkpoint_publisher"),
    States.Num());
}


struct FCrowdDemoPreparedWorkerMassRecord
{
  FCrowdStableEntityRef EntityRef;
  FMassEntityHandle Entity;
  int32 StableSlot = INDEX_NONE;
  int32 AgentId = INDEX_NONE;
  FCrowdWorkerMovementState WorkerMovement;
  FCrowdDemoCombatAgentState WorkerCombat;
  FCrowdWorkerProjectileState WorkerProjectile;
  FCrowdMassCommitRecord MovementCommit;
  FCrowdFacingResult Facing;
  FCrowdDemoRoundBoundaryBusinessFact FinalBusiness;
  int32 ConsecutiveFinalSettleSteps = 0;
  bool bFinalPositionSettled = false;
  bool bHasMovement = false;
  bool bHasCombat = false;
  bool bHasProjectile = false;
};

struct FCrowdDemoPreparedWorkerMassApplyPlan
{
  uint64 PublishSequence = 0;
  TArray<FCrowdDemoPreparedWorkerMassRecord> Records;
  TArray<FMassArchetypeEntityCollection> Collections;
  TMap<FCrowdStableEntityRef, int32> RecordIndexByEntityRef;
  FCrowdDemoProjectileStepSummary WorkerProjectileSummary;
  FCrowdDemoHitResponseSummary WorkerProjectileHitSummary;
  TArray<FCrowdDemoProjectileVisualEvent> WorkerProjectileVisualEvents;
  int32 ProjectileRecordIndex = INDEX_NONE;
  uint32 BuildCount = 0;
  uint32 ApplyCount = 0;
  bool bEnriched = false;
  bool bValid = false;
};

namespace
{

bool BuildPreparedWorkerMassApplyPlan(
  const FCrowdWorkerPreparedResultApply& Prepared,
  FMassEntityQuery& EntityQuery,
  FMassEntityManager& EntityManager,
  FCrowdDemoPreparedWorkerMassApplyPlan& OutPlan)
{
  OutPlan = {};
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoMassSubsystem* MassSubsystem = World
    ? World->GetSubsystem<UCrowdDemoMassSubsystem>() : nullptr;
  if (!MassSubsystem || !Prepared.IsValid())
    return false;

  TSet<FCrowdWorkerDirtyStateKey> UniqueFields;
  TMap<FCrowdStableEntityRef, int32> RecordIndexByEntityRef;
  TSet<FMassEntityHandle> UniqueEntityHandles;
  TArray<FMassEntityHandle> EntityHandles;
  UniqueFields.Reserve(Prepared.Batch.StatePatches.Num());
  RecordIndexByEntityRef.Reserve(Prepared.Batch.StatePatches.Num());
  EntityHandles.Reserve(Prepared.Batch.StatePatches.Num());
  for (int32 PatchIndex = 0;
       PatchIndex < Prepared.Batch.StatePatches.Num(); ++PatchIndex)
  {
    if (Prepared.StatePatchStableSlots[PatchIndex] == INDEX_NONE)
      continue;
    const FCrowdWorkerStatePatch& Patch =
      Prepared.Batch.StatePatches[PatchIndex];
    if (Patch.StateFieldId == 0)
      continue;
    const ECrowdWorkerField Field =
      static_cast<ECrowdWorkerField>(Patch.StateFieldId - 1);
    if (Field != ECrowdWorkerField::Facing
      && Field != ECrowdWorkerField::Combat
      && Field != ECrowdWorkerField::Projectile)
      continue;
    const FCrowdWorkerDirtyStateKey Key{Patch.EntityRef, Field};
    if (UniqueFields.Contains(Key))
      return false;
    UniqueFields.Add(Key);

    int32* ExistingRecordIndex =
      RecordIndexByEntityRef.Find(Patch.EntityRef);
    FCrowdDemoPreparedWorkerMassRecord* Record = ExistingRecordIndex
      ? &OutPlan.Records[*ExistingRecordIndex] : nullptr;
    if (Record && Record->StableSlot
        != Prepared.StatePatchStableSlots[PatchIndex])
      return false;
    if (!Record)
    {
      FMassEntityHandle Entity;
      if (!MassSubsystem->ResolveTrackedAgentHandle(
          Patch.EntityRef, EntityManager, Entity))
        return false;
      if (UniqueEntityHandles.Contains(Entity))
        return false;
      UniqueEntityHandles.Add(Entity);
      const int32 NewRecordIndex = OutPlan.Records.AddDefaulted();
      RecordIndexByEntityRef.Add(Patch.EntityRef, NewRecordIndex);
      Record = &OutPlan.Records[NewRecordIndex];
      Record->EntityRef = Patch.EntityRef;
      Record->Entity = Entity;
      Record->StableSlot = Prepared.StatePatchStableSlots[PatchIndex];
      EntityHandles.Add(Entity);
    }
    const FMassEntityHandle Entity = Record->Entity;
    const FCrowdDemoMassIdentityFragment* Identity =
      EntityManager.GetFragmentDataPtr<
        FCrowdDemoMassIdentityFragment>(Entity);
    const FCrowdMassAgentFragment* RuntimeIdentity =
      EntityManager.GetFragmentDataPtr<FCrowdMassAgentFragment>(Entity);
    if (!Identity || !RuntimeIdentity
      || RuntimeIdentity->GetStableEntityRef() != Patch.EntityRef
      || Identity->Id != RuntimeIdentity->AgentId
      || Identity->LifecycleSerial
        != static_cast<int32>(Patch.EntityRef.LifecycleSerial))
      return false;
    Record->AgentId = Identity->Id;
    if (Field == ECrowdWorkerField::Facing)
    {
      if (!FCrowdWorkerMovementStateCodec::Decode(
          Patch.State.Payload, Record->WorkerMovement))
        return false;
      Record->bHasMovement = true;
    }
    else if (Field == ECrowdWorkerField::Combat)
    {
      FCrowdWorkerCombatState WorkerCombat;
      if (!FCrowdWorkerCombatStateCodec::Decode(
          Patch.State.Payload, WorkerCombat)
        || !FCrowdDemoWorkerCombatStatePayloadCodec::Decode(
          WorkerCombat.HostState, Record->WorkerCombat)
        || Record->WorkerCombat.AgentId != Identity->Id
        || Record->WorkerCombat.LifecycleSerial != Identity->LifecycleSerial
        || !EntityManager.GetFragmentDataPtr<
          FCrowdDemoMassStatsFragment>(Entity)
        || !EntityManager.GetFragmentDataPtr<
          FCrowdDemoBusinessStateFragment>(Entity)
        || !EntityManager.GetFragmentDataPtr<
          FCrowdDemoRangedAttackFragment>(Entity)
        || !EntityManager.GetFragmentDataPtr<
          FCrowdDemoReactiveMotionFragment>(Entity)
        || !EntityManager.GetFragmentDataPtr<
          FCrowdDemoHitFlashFragment>(Entity))
        return false;
      Record->bHasCombat = true;
    }
    else
    {
      if (!FCrowdWorkerProjectileStateCodec::Decode(
          Patch.State.Payload, Record->WorkerProjectile))
        return false;
      Record->bHasProjectile = true;
    }
  }
  OutPlan.Records.Sort([](const auto& A, const auto& B)
  {
    return A.StableSlot < B.StableSlot;
  });
  OutPlan.RecordIndexByEntityRef.Reserve(OutPlan.Records.Num());
  for (int32 Index = 0; Index < OutPlan.Records.Num(); ++Index)
  {
    if (OutPlan.RecordIndexByEntityRef.Contains(
        OutPlan.Records[Index].EntityRef))
      return false;
    OutPlan.RecordIndexByEntityRef.Add(
      OutPlan.Records[Index].EntityRef, Index);
  }
  if (!EntityHandles.IsEmpty())
  {
    UE::Mass::Utils::CreateEntityCollections(
      EntityManager, EntityHandles,
      FMassArchetypeEntityCollection::EDuplicatesHandling::NoDuplicates,
      OutPlan.Collections);
    if (EntityQuery.GetNumMatchingEntities(OutPlan.Collections)
        != EntityHandles.Num())
      return false;
  }
  OutPlan.PublishSequence = Prepared.Batch.PublishSequence;
  OutPlan.BuildCount = 1;
  OutPlan.bValid = OutPlan.PublishSequence != 0;
  return OutPlan.bValid;
}

bool EnrichPreparedWorkerMassApplyPlan(
  UCrowdDemoRoundSimPipelineSubsystem& Pipeline,
  FMassEntityManager& EntityManager,
  FCrowdDemoPreparedWorkerMassApplyPlan& Plan)
{
  if (!Plan.bValid || Plan.BuildCount != 1 || Plan.bEnriched)
    return false;
  for (FCrowdDemoPreparedWorkerMassRecord& Record : Plan.Records)
  {
    const FCrowdDemoMassIdentityFragment* Identity =
      EntityManager.GetFragmentDataPtr<FCrowdDemoMassIdentityFragment>(
        Record.Entity);
    const FCrowdMassAgentFragment* RuntimeIdentity =
      EntityManager.GetFragmentDataPtr<FCrowdMassAgentFragment>(Record.Entity);
    const FCrowdDemoRoundSimStateFragment* CurrentState =
      EntityManager.GetFragmentDataPtr<FCrowdDemoRoundSimStateFragment>(
        Record.Entity);
    const FCrowdMassFacingFragment* CurrentFacing =
      EntityManager.GetFragmentDataPtr<FCrowdMassFacingFragment>(Record.Entity);
    const FCrowdDemoMassVisualFragment* CurrentVisual =
      EntityManager.GetFragmentDataPtr<FCrowdDemoMassVisualFragment>(
        Record.Entity);
    if (!Identity || !RuntimeIdentity || !CurrentState || !CurrentFacing
      || !CurrentVisual
      || RuntimeIdentity->GetStableEntityRef() != Record.EntityRef
      || Identity->Id != Record.AgentId
      || Identity->LifecycleSerial
        != static_cast<int32>(Record.EntityRef.LifecycleSerial))
      return false;

    Record.MovementCommit.EntityRef = Record.EntityRef;
    Record.MovementCommit.PlanRevision = Pipeline.GetCurrentPlanRevision();
    Record.MovementCommit.Movement.AgentId = Record.AgentId;
    Record.MovementCommit.Movement.LifecycleSerial =
      Record.EntityRef.LifecycleSerial;
    Record.MovementCommit.Movement.Position = Record.bHasMovement
      ? Record.WorkerMovement.Position : CurrentState->Location;
    Record.MovementCommit.Movement.Velocity = Record.bHasMovement
      ? Record.WorkerMovement.Velocity : CurrentState->Velocity;
    Record.MovementCommit.Movement.YawDegrees = Record.bHasMovement
      ? Record.WorkerMovement.YawDegrees : CurrentState->YawDegrees;
    Record.MovementCommit.Movement.bValid = true;

    Record.Facing = CurrentFacing->Value;
    Record.Facing.AgentId = Record.AgentId;
    if (Record.bHasMovement)
    {
      Record.Facing.DesiredYawDegrees = Record.WorkerMovement.YawDegrees;
      Record.Facing.ResolvedYawDegrees = Record.WorkerMovement.YawDegrees;
      Record.Facing.AppliedYawDeltaDegrees = FMath::FindDeltaAngleDegrees(
        CurrentFacing->Value.ResolvedYawDegrees,
        Record.WorkerMovement.YawDegrees);
      Record.Facing.bHeldCurrentYaw = FMath::IsNearlyZero(
        Record.Facing.AppliedYawDeltaDegrees, 0.001f);
    }
    Record.ConsecutiveFinalSettleSteps =
      CurrentFacing->ConsecutiveFinalSettleSteps;
    Record.bFinalPositionSettled =
      CurrentFacing->bFinalPositionSettled;

    FCrowdDemoRoundBoundaryBusinessFact BaseBusiness;
    BaseBusiness.EntityRef = Record.EntityRef;
    BaseBusiness.AgentId = Record.AgentId;
    BaseBusiness.YawDegrees = Record.bHasMovement
      ? Record.WorkerMovement.YawDegrees : CurrentState->YawDegrees;
    BaseBusiness.Visual = *CurrentVisual;
    if (const FCrowdDemoRoundFormationFragment* Formation =
        EntityManager.GetFragmentDataPtr<FCrowdDemoRoundFormationFragment>(
          Record.Entity))
    {
      BaseBusiness.FormationIndex = Formation->FormationIndex;
      BaseBusiness.LocalOffset = Formation->LocalOffset;
      BaseBusiness.RadiusCm = Formation->RadiusCm;
    }
    const FCrowdDemoMassStatsFragment* CurrentStats =
      EntityManager.GetFragmentDataPtr<FCrowdDemoMassStatsFragment>(
        Record.Entity);
    const FCrowdDemoBusinessStateFragment* CurrentBusiness =
      EntityManager.GetFragmentDataPtr<FCrowdDemoBusinessStateFragment>(
        Record.Entity);
    const FCrowdDemoRangedAttackFragment* CurrentAttack =
      EntityManager.GetFragmentDataPtr<FCrowdDemoRangedAttackFragment>(
        Record.Entity);
    const FCrowdDemoReactiveMotionFragment* CurrentReactive =
      EntityManager.GetFragmentDataPtr<FCrowdDemoReactiveMotionFragment>(
        Record.Entity);
    const FCrowdDemoHitFlashFragment* CurrentHitFlash =
      EntityManager.GetFragmentDataPtr<FCrowdDemoHitFlashFragment>(
        Record.Entity);
    BaseBusiness.bHasCombatCapability = CurrentStats && CurrentBusiness
      && CurrentAttack && CurrentReactive && CurrentHitFlash;
    if (BaseBusiness.bHasCombatCapability)
    {
      BaseBusiness.Stats = *CurrentStats;
      BaseBusiness.Business = *CurrentBusiness;
      BaseBusiness.Attack = *CurrentAttack;
      BaseBusiness.ReactiveMotion = *CurrentReactive;
      BaseBusiness.HitFlash = *CurrentHitFlash;
    }
    Record.FinalBusiness = BuildFinalBusinessFactFromState(
      Pipeline, BaseBusiness, Record.MovementCommit.Movement,
      Record.bHasCombat ? &Record.WorkerCombat : nullptr);
  }
  const FCrowdWorkerProjectileState* WorkerProjectile = nullptr;
  for (int32 Index = 0; Index < Plan.Records.Num(); ++Index)
  {
    if (!Plan.Records[Index].bHasProjectile)
      continue;
    if (WorkerProjectile)
      return false;
    Plan.ProjectileRecordIndex = Index;
    WorkerProjectile = &Plan.Records[Index].WorkerProjectile;
  }
  if (Pipeline.IsRangedProjectileCombat() && !WorkerProjectile)
    return false;
  if (WorkerProjectile)
  {
    const TConstArrayView<FCrowdProjectileState> ProjectileStates =
      WorkerProjectile->Prepared.States;
    const int32 ActiveProjectileCount = Algo::CountIf(
      ProjectileStates,
      [](const FCrowdProjectileState& State) { return State.bActive; });
    UWorld* World = EntityManager.GetWorld();
    UCrowdDemoMassSubsystem* MassSubsystem = World
      ? World->GetSubsystem<UCrowdDemoMassSubsystem>() : nullptr;
    FCrowdDemoWorkerCombatHostResult HostCombatResult;
    if (WorkerProjectile->Prepared.FixedStepIndex
          != Pipeline.GetCurrentFixedStepIndex()
      || !MassSubsystem
      || !MassSubsystem->PrepareProjectileCapacity(ActiveProjectileCount)
      || !MassSubsystem->ValidateProjectileStates(ProjectileStates)
      || !FCrowdDemoWorkerCombatHostResultCodec::Decode(
        WorkerProjectile->HostCombatResult, HostCombatResult)
      || HostCombatResult.FixedStepIndex
        != Pipeline.GetCurrentFixedStepIndex())
      return false;
    Plan.WorkerProjectileSummary = HostCombatResult.AttackSummary;
    FCrowdDemoProjectileAdapters::MergeSummary(
      WorkerProjectile->Prepared.Summary,
      Plan.WorkerProjectileSummary);
    Plan.WorkerProjectileHitSummary = HostCombatResult.HitSummary;
    FCrowdDemoProjectileAdapters::AppendVisualEvents(
      WorkerProjectile->Prepared.Events,
      Plan.WorkerProjectileVisualEvents);
  }
  Plan.bEnriched = true;
  return true;
}

bool FinalValidatePreparedWorkerMassDirtyPlan(
  const FCrowdWorkerPreparedResultApply& Prepared,
  const FCrowdWorkerResultCommitToken& CommitToken,
  const int32 PreparedPlanRevision,
  const int32 PreparedFixedStepIndex,
  FCrowdDemoPreparedWorkerMassApplyPlan& Plan,
  UCrowdDemoRoundSimPipelineSubsystem& Pipeline,
  FMassEntityQuery& EntityQuery,
  FMassEntityManager& EntityManager)
{
  if (!Prepared.IsValid() || !CommitToken.Matches(Prepared)
    || !Plan.bValid || !Plan.bEnriched
    || Plan.BuildCount != 1 || Plan.ApplyCount != 0
    || Plan.PublishSequence != Prepared.Batch.PublishSequence
    || PreparedPlanRevision != Pipeline.GetCurrentPlanRevision()
    || PreparedFixedStepIndex != Pipeline.GetCurrentFixedStepIndex()
    || Pipeline.IsCurrentStepWorkerDirtyMassApplied())
    return false;
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoMassSubsystem* MassSubsystem = World
    ? World->GetSubsystem<UCrowdDemoMassSubsystem>() : nullptr;
  if (!MassSubsystem
    || EntityQuery.GetNumMatchingEntities(Plan.Collections)
      != Plan.Records.Num())
    return false;
  for (const FCrowdDemoPreparedWorkerMassRecord& Record : Plan.Records)
  {
    FMassEntityHandle CurrentEntity;
    if (!MassSubsystem->ResolveTrackedAgentHandle(
        Record.EntityRef, EntityManager, CurrentEntity)
      || CurrentEntity != Record.Entity
      || !EntityManager.IsEntityValid(Record.Entity))
      return false;
    const FCrowdDemoMassIdentityFragment* Identity =
      EntityManager.GetFragmentDataPtr<FCrowdDemoMassIdentityFragment>(
        Record.Entity);
    const FCrowdMassAgentFragment* RuntimeIdentity =
      EntityManager.GetFragmentDataPtr<FCrowdMassAgentFragment>(
        Record.Entity);
    if (!Identity || !RuntimeIdentity
      || RuntimeIdentity->GetStableEntityRef() != Record.EntityRef
      || Identity->Id != Record.AgentId
      || Identity->LifecycleSerial
        != static_cast<int32>(Record.EntityRef.LifecycleSerial))
      return false;
  }
  if (Plan.ProjectileRecordIndex != INDEX_NONE)
  {
    const FCrowdWorkerProjectileState& Projectile =
      Plan.Records[Plan.ProjectileRecordIndex].WorkerProjectile;
    const int32 ActiveProjectileCount = Algo::CountIf(
      Projectile.Prepared.States,
      [](const FCrowdProjectileState& State) { return State.bActive; });
    if (Projectile.Prepared.FixedStepIndex
          != Pipeline.GetCurrentFixedStepIndex()
      || !MassSubsystem->PrepareProjectileCapacity(ActiveProjectileCount)
      || !MassSubsystem->ValidateProjectileStates(
        Projectile.Prepared.States))
      return false;
  }
  return Pipeline.TryBeginAtomicCommitWrite();
}

void ApplyValidatedWorkerMassDirtyPlan(
  FCrowdDemoPreparedWorkerMassApplyPlan& Plan,
  UCrowdDemoRoundSimPipelineSubsystem& Pipeline,
  FMassEntityQuery& EntityQuery,
  FMassExecutionContext& Context)
{
  checkf(Plan.bValid && Plan.bEnriched
      && Plan.BuildCount == 1 && Plan.ApplyCount == 0,
    TEXT("Worker Mass plan was not validated exactly once"));
  int32 AppliedCount = 0;
  EntityQuery.ForEachEntityChunkInCollections(
    Plan.Collections, Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities =
      ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto RuntimeIdentities =
      ChunkContext.GetFragmentView<FCrowdMassAgentFragment>();
    const auto States =
      ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto RuntimeStates =
      ChunkContext.GetMutableFragmentView<FCrowdMassSimulationStateFragment>();
    const auto RuntimeMovements =
      ChunkContext.GetMutableFragmentView<FCrowdMassMovementOutputFragment>();
    const auto RuntimeFacings =
      ChunkContext.GetMutableFragmentView<FCrowdMassFacingFragment>();
    const auto Transforms =
      ChunkContext.GetMutableFragmentView<FTransformFragment>();
    const auto Velocities =
      ChunkContext.GetMutableFragmentView<FMassVelocityFragment>();
    const auto DemoMovements =
      ChunkContext.GetMutableFragmentView<FCrowdDemoMassMovementFragment>();
    const auto Stats =
      ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
    const auto Businesses = ChunkContext.GetMutableFragmentView<
      FCrowdDemoBusinessStateFragment>();
    const auto Attacks = ChunkContext.GetMutableFragmentView<
      FCrowdDemoRangedAttackFragment>();
    const auto Reactives = ChunkContext.GetMutableFragmentView<
      FCrowdDemoReactiveMotionFragment>();
    const auto HitFlashes = ChunkContext.GetMutableFragmentView<
      FCrowdDemoHitFlashFragment>();
    const auto Visuals =
      ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
    const bool bHasCombatBundle = !Stats.IsEmpty()
      && Stats.Num() == Businesses.Num()
      && Stats.Num() == Attacks.Num()
      && Stats.Num() == Reactives.Num()
      && Stats.Num() == HitFlashes.Num();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdStableEntityRef EntityRef =
        RuntimeIdentities[It].GetStableEntityRef();
      const int32* RecordIndex =
        Plan.RecordIndexByEntityRef.Find(EntityRef);
      checkf(RecordIndex != nullptr,
        TEXT("Prepared Dirty Mass collection escaped its validated plan"));
      const FCrowdDemoPreparedWorkerMassRecord& Record =
        Plan.Records[*RecordIndex];
      checkf(Identities[It].Id == Record.AgentId
        && Identities[It].LifecycleSerial
          == static_cast<int32>(EntityRef.LifecycleSerial),
        TEXT("Prepared Dirty Mass identity changed after validation"));
      if (Record.bHasMovement)
      {
        RuntimeFacings[It].Value = Record.Facing;
        RuntimeFacings[It].PlanRevision =
          Record.MovementCommit.PlanRevision;
        RuntimeFacings[It].ConsecutiveFinalSettleSteps =
          Record.ConsecutiveFinalSettleSteps;
        RuntimeFacings[It].bFinalPositionSettled =
          Record.bFinalPositionSettled;
        FCrowdMassCommitTarget Target;
        Target.EntityRef = EntityRef;
        Target.AgentId = Record.AgentId;
        Target.LifecycleSerial = EntityRef.LifecycleSerial;
        const bool bDemoApplied =
          FCrowdDemoMassCrowdRuntimeAdapter::ApplyCommitRecord(
            Record.MovementCommit, Identities[It], RuntimeIdentities[It],
            States[It]);
        const bool bRuntimeApplied =
          FCrowdMassRuntimeBridge::ApplyMovementToState(
            Record.MovementCommit, Target, RuntimeStates[It],
            RuntimeMovements[It]);
        checkf(bDemoApplied && bRuntimeApplied,
          TEXT("Prepared Dirty movement changed after validation"));
        const FCrowdMovementOutput& Movement =
          Record.MovementCommit.Movement;
        FTransform Transform = Transforms[It].GetTransform();
        Transform.SetLocation(Movement.Position);
        Transform.SetRotation(FRotator(
          0.0f, Movement.YawDegrees, 0.0f).Quaternion());
        Transforms[It].SetTransform(Transform);
        Velocities[It].Value = Movement.Velocity;
        DemoMovements[It].CurrentVelocity = Movement.Velocity;
        DemoMovements[It].DesiredVelocity = Movement.Velocity;
        DemoMovements[It].YawDegrees = Movement.YawDegrees;
        States[It].SimulatedServerTimeSeconds =
          Pipeline.GetCurrentStepEndServerTimeSeconds();
      }
      if (bHasCombatBundle)
      {
        Stats[It] = Record.FinalBusiness.Stats;
        Businesses[It] = Record.FinalBusiness.Business;
        Attacks[It] = Record.FinalBusiness.Attack;
        Reactives[It] = Record.FinalBusiness.ReactiveMotion;
        HitFlashes[It] = Record.FinalBusiness.HitFlash;
      }
      Visuals[It] = Record.FinalBusiness.Visual;
      ++AppliedCount;
    }
  });
  checkf(AppliedCount == Plan.Records.Num(),
    TEXT("Prepared Dirty Mass apply count changed after validation"));
  if (Plan.ProjectileRecordIndex != INDEX_NONE)
  {
    const FCrowdWorkerProjectileState& WorkerProjectile =
      Plan.Records[Plan.ProjectileRecordIndex].WorkerProjectile;
    Pipeline.ApplyProjectileFinalState(
      WorkerProjectile.Prepared.States);
  }
  ++Plan.ApplyCount;
}

void CommitValidatedWorkerMassSideEffects(
  const FCrowdDemoPreparedWorkerMassApplyPlan& Plan,
  UCrowdDemoRoundSimPipelineSubsystem& Pipeline)
{
  checkf(Plan.ApplyCount == 1,
    TEXT("Worker Mass side effects require exactly one Mass apply"));
  if (Plan.ProjectileRecordIndex != INDEX_NONE)
  {
    Pipeline.RecordProjectileStep(
      Plan.WorkerProjectileSummary,
      Plan.WorkerProjectileVisualEvents);
    Pipeline.RecordProjectileHitResponse(
      Plan.WorkerProjectileHitSummary);
  }
  Pipeline.RecordDirtyMassApply(Plan.Records.Num());
  checkf(Pipeline.MarkCurrentStepWorkerDirtyMassApplied(
      Plan.PublishSequence, Plan.Records.Num()),
    TEXT("Validated Worker Dirty Mass marker unexpectedly failed"));
}
}

static void ExecuteWorkerResultApply(
  FMassEntityQuery& EntityQuery,
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  if (!World) return;
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline =
    World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>();
  if (!Pipeline) return;
  if (Pipeline->PeekPreparedRoundCommitPlan())
    return;
  const uint64 ConsumerFrameSequence =
    Pipeline->AllocateWorkerResultConsumerFrameSequence();
  if (World->GetNetMode() == NM_Client)
  {
    if (!FCrowdDemoWorkerInputSync::ConsumePublishedResults(
        *World, ConsumerFrameSequence))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerResultApplyFailed consumer_frame=%llu"),
        ConsumerFrameSequence);
    }
    return;
  }
  FCrowdWorkerPreparedResultApply Prepared;
  TSharedPtr<FCrowdDemoPreparedWorkerMassApplyPlan> PreparedMassPlan;
  bool bHasBatch = false;
  const double ApplyStartSeconds = FPlatformTime::Seconds();
  if (!FCrowdDemoWorkerInputSync::PreparePublishedResults(
      *World, ConsumerFrameSequence, Prepared, bHasBatch))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoWorkerResultApplyFailed consumer_frame=%llu"),
      ConsumerFrameSequence);
    return;
  }
  if (bHasBatch)
  {
    PreparedMassPlan =
      MakeShared<FCrowdDemoPreparedWorkerMassApplyPlan>();
    if (!PreparedMassPlan.IsValid()
      || !BuildPreparedWorkerMassApplyPlan(
        Prepared, EntityQuery, EntityManager, *PreparedMassPlan)
      || !EnrichPreparedWorkerMassApplyPlan(
        *Pipeline, EntityManager, *PreparedMassPlan))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerResultMassPrepareFailed consumer_frame=%llu publish=%llu"),
        ConsumerFrameSequence, Prepared.Batch.PublishSequence);
      return;
    }
    FCrowdDemoPreparedRoundCommitPlan Pending;
    Pending.WorkerCommitToken =
      FCrowdWorkerResultCommitToken::FromPrepared(Prepared);
    Pending.PlanRevision = Pipeline->GetCurrentPlanRevision();
    Pending.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
    Pending.PreparedProxyResult = MoveTemp(Prepared);
    Pending.PreparedMassPlan = MoveTemp(PreparedMassPlan);
    Pending.ApplyStartSeconds = ApplyStartSeconds;
    if (!Pipeline->QueuePreparedRoundCommitPlan(MoveTemp(Pending)))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerResultPendingQueueFailed consumer_frame=%llu"),
        ConsumerFrameSequence);
    }
  }
}

namespace
{
enum class ECrowdDemoRoundFrameStageResult : uint8
{
  Empty = 0,
  Pending,
  Committed,
  Failed
};

bool AdvanceRoundWorkerFrame(
  FMassEntityQuery& ResultCommitQuery,
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!World || !Pipeline)
    return false;
  const auto Commit = [&]() -> ECrowdDemoRoundFrameStageResult
  {
    if (!Pipeline->IsStepInProgress())
      return ECrowdDemoRoundFrameStageResult::Empty;
    if (!Pipeline->IsCurrentStepFullWorkerProductionFastPath())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoServerStepMissingWorkerAuthority step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
      Pipeline->FailFixedStep();
      return ECrowdDemoRoundFrameStageResult::Failed;
    }

    FCrowdDemoPreparedRoundCommitPlan* PendingWorkerResult =
      Pipeline->PeekPreparedRoundCommitPlan();
    UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
      World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
    if (!RuntimeSubsystem)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerOwnerBarrierRuntimeMissing step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
      Pipeline->FailFixedStep();
      return ECrowdDemoRoundFrameStageResult::Failed;
    }
    if (!PendingWorkerResult)
    {
      Pipeline->RecordPipelineFramePerformance(
        0, GetRoundPipelineServerTime(*World), false, false);
      return ECrowdDemoRoundFrameStageResult::Pending;
    }
    if (!PendingWorkerResult->IsValid())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerOwnerBarrierPendingInvalid step=%d publish=%llu"),
        Pipeline->GetCurrentFixedStepIndex(),
        PendingWorkerResult->WorkerCommitToken.PublishSequence);
      Pipeline->FailFixedStep();
      return ECrowdDemoRoundFrameStageResult::Failed;
    }
    {
      const uint64 ExpectedInputSequence =
        Pipeline->GetCurrentStepFullWorkerInputSequence();
      const uint64 AppliedInputSequence =
        PendingWorkerResult->PreparedProxyResult.Batch.
          LastAppliedInputSequence;
      if (AppliedInputSequence < ExpectedInputSequence)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoFullWorkerProductionStaleResult step=%d expected=%llu actual=%llu"),
          Pipeline->GetCurrentFixedStepIndex(), ExpectedInputSequence,
          AppliedInputSequence);
        Pipeline->FailFixedStep();
        return ECrowdDemoRoundFrameStageResult::Failed;
      }
      if (ExpectedInputSequence == 0
        || AppliedInputSequence != ExpectedInputSequence)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoFullWorkerProductionResultSequence step=%d expected=%llu actual=%llu"),
          Pipeline->GetCurrentFixedStepIndex(), ExpectedInputSequence,
          AppliedInputSequence);
        Pipeline->FailFixedStep();
        return ECrowdDemoRoundFrameStageResult::Failed;
      }
    }

    const double CommitStart = FPlatformTime::Seconds();
    FCrowdDemoPreparedWorkerMassApplyPlan& MassPlan =
      *PendingWorkerResult->PreparedMassPlan;
    FCrowdWorkerResultApplyProxy& Proxy =
      RuntimeSubsystem->GetWorkerResultApplyProxy();
    FCrowdDemoPreparedWorkerResultSideEffects PreparedSideEffects;
    const ECrowdWorkerResultOwnerCommitResult BarrierResult =
      FCrowdWorkerResultOwnerCommitBarrier::Commit(
        Proxy, PendingWorkerResult->PreparedProxyResult,
        PendingWorkerResult->WorkerCommitToken,
        [&]()
        {
          return FCrowdDemoWorkerInputSync::
              PrepareCommittedResultSideEffects(
                *World, PendingWorkerResult->PreparedProxyResult,
                PreparedSideEffects)
            && FinalValidatePreparedWorkerMassDirtyPlan(
              PendingWorkerResult->PreparedProxyResult,
              PendingWorkerResult->WorkerCommitToken,
              PendingWorkerResult->PlanRevision,
              PendingWorkerResult->FixedStepIndex, MassPlan,
              *Pipeline, ResultCommitQuery, EntityManager);
        },
        [&]()
        {
          ApplyValidatedWorkerMassDirtyPlan(
            MassPlan, *Pipeline, ResultCommitQuery, Context);
        },
        [&]()
        {
          CommitValidatedWorkerMassSideEffects(MassPlan, *Pipeline);
          checkf(Pipeline->MarkFullWorkerProductionResultCommitted(
              (FPlatformTime::Seconds() - CommitStart) * 1000.0),
            TEXT("Validated Full Worker Production commit unexpectedly failed"));
          FCrowdDemoWorkerInputSync::
            CommitPreparedResultSideEffectsNoFail(
              *World, PendingWorkerResult->PreparedProxyResult,
              PreparedSideEffects,
              (FPlatformTime::Seconds()
                - PendingWorkerResult->ApplyStartSeconds)
                * 1000.0);
        });
    const bool bApplied = BarrierResult
        == ECrowdWorkerResultOwnerCommitResult::Committed
      && Pipeline->IsCurrentStepWorkerDirtyMassApplied();
    if (!bApplied)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerOwnerBarrierRejected step=%d publish=%llu result=%u"),
        Pipeline->GetCurrentFixedStepIndex(),
        PendingWorkerResult->WorkerCommitToken.PublishSequence,
        static_cast<uint32>(BarrierResult));
      Pipeline->FailFixedStep();
      return ECrowdDemoRoundFrameStageResult::Failed;
    }
    const int64 AbsoluteSimulationTick = static_cast<int64>(
      RuntimeSubsystem->GetAsyncSimulationRuntime().GetMetrics().
        AbsoluteSimulationTick);
    Pipeline->ObserveCommittedWorkerScenarioState(
      Proxy,
      PendingWorkerResult->PreparedProxyResult.Batch.Generation,
      PendingWorkerResult->PreparedProxyResult.Batch.PublishSequence,
      AbsoluteSimulationTick);
    const float CommitMs = static_cast<float>(
      (FPlatformTime::Seconds() - CommitStart) * 1000.0);
    Pipeline->RecordPerformanceStage(
      ECrowdDemoRoundPerformanceStage::Commit, CommitMs);
    return ECrowdDemoRoundFrameStageResult::Committed;
  };
  const ECrowdDemoRoundFrameStageResult CommitResult = Commit();
  if (CommitResult == ECrowdDemoRoundFrameStageResult::Failed
    || CommitResult == ECrowdDemoRoundFrameStageResult::Pending)
    return false;
  const bool bCommitted =
    CommitResult == ECrowdDemoRoundFrameStageResult::Committed;
  if (bCommitted)
  {
    if (World->GetNetMode() != NM_Client)
    {
      ExecuteRoundCheckpointPublisher(EntityManager, Context);
    }
    Pipeline->RecordFixedStepPerformance(
      Pipeline->GetCurrentBoundaryWallMilliseconds());
    Pipeline->FinishFixedStep();
  }
  const int32 ExecutedSteps = bCommitted ? 1 : 0;
  const auto Submit = [&]() -> bool
  {
    const float TargetServerTime = GetRoundPipelineServerTime(*World);
    if (Pipeline->IsStepInProgress())
      return false;
    if (Pipeline->HasDueRoundPlan(
        Pipeline->GetSimulatedServerTimeSeconds()))
    {
      Pipeline->RecordPipelineFramePerformance(
        ExecutedSteps, TargetServerTime,
        false, false);
      return false;
    }
    if (!Pipeline->TryBeginFixedStep(TargetServerTime))
    {
      Pipeline->RecordPipelineFramePerformance(
        ExecutedSteps, TargetServerTime,
        false, false);
      return false;
    }

    const double SnapshotApplyStartSeconds = FPlatformTime::Seconds();
    const bool bSnapshotReady = Pipeline->TryUseBootstrapBoundarySnapshot()
      || Pipeline->TryPublishWorkerProxyBoundarySnapshot();
    if (!bSnapshotReady || !Pipeline->IsBoundarySnapshotCurrent())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoDirtyBoundaryMissing step=%d revision=%d"),
        Pipeline->GetCurrentFixedStepIndex(),
        Pipeline->GetCurrentPlanRevision());
      Pipeline->FailFixedStep();
      return false;
    }
    const double SnapshotApplyMilliseconds =
      (FPlatformTime::Seconds() - SnapshotApplyStartSeconds) * 1000.0;
    if (Pipeline->CanUseFullWorkerProductionFastPath())
    {
      ExecuteRoundTargetFactApply(EntityManager, Context);
      if (!Pipeline->TrySubmitFullWorkerProductionIntent())
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoFullWorkerProductionSubmitFailed step=%d"),
          Pipeline->GetCurrentFixedStepIndex());
        Pipeline->FailFixedStep();
        return false;
      }
      Pipeline->RecordPerformanceStage(
        ECrowdDemoRoundPerformanceStage::BusinessPrepare,
        static_cast<float>(SnapshotApplyMilliseconds));
      Pipeline->RecordPipelineFramePerformance(
        ExecutedSteps, TargetServerTime, false, false);
      return true;
    }
    if (!Pipeline->BeginWorkerBootstrapPreparation(
        SnapshotApplyMilliseconds))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerBootstrapBeginFailed step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
      Pipeline->FailFixedStep();
      return false;
    }
    const auto MeasureStage = [Pipeline](
      const ECrowdDemoRoundPerformanceStage Stage,
      auto&& Work)
    {
      const double Start = FPlatformTime::Seconds();
      Work();
      const float Milliseconds = static_cast<float>(
        (FPlatformTime::Seconds() - Start) * 1000.0);
      Pipeline->RecordPerformanceStage(Stage, Milliseconds);
    };

    bool bStageValid = true;
    MeasureStage(
      ECrowdDemoRoundPerformanceStage::BusinessPrepare, [&]
    {
      bStageValid = Pipeline->StageBoundaryBusinessWork();
      if (!bStageValid) return;
      if (Pipeline->IsOpenSpawnRelaxation())
        ExecuteRoundOpenSpawnRelaxationPrepare(
          EntityManager, Context);
      if (Pipeline->GetRules().Scenario
          == ECrowdDemoScenario::SimRoundSoftPressure
        && Pipeline->GetRules().TargetDistanceBandSettings.bEnabled
          != 0)
      {
        ExecuteRoundTargetFactApply(
          EntityManager, Context);
      }
    });
    if (!bStageValid)
    {
      Pipeline->FailFixedStep();
      return false;
    }
    MeasureStage(ECrowdDemoRoundPerformanceStage::SharedFlow, [&]
    {
      ExecuteRoundSharedFlowFieldBuild(
        EntityManager, Context);
      ExecuteRoundFlowPreferredVelocity(
        EntityManager, Context);
    });
    if (Pipeline->GetRules().Scenario
        == ECrowdDemoScenario::SimRoundSoftPressure
      && Pipeline->IsTargetRegionExecutionActive())
    {
      MeasureStage(
        ECrowdDemoRoundPerformanceStage::TargetTopology, [&]
      {
        ExecuteRoundTargetPolarTopologyBuild(
          EntityManager, Context);
      });
      MeasureStage(
        ECrowdDemoRoundPerformanceStage::TargetDemand, [&]
      {
        ExecuteRoundTargetRegionPopulationBuild(
          EntityManager, Context);
      });
      MeasureStage(
        ECrowdDemoRoundPerformanceStage::TargetPlan, [&]
      {
        ExecuteRoundTargetRegionTransportSolve(
          EntityManager, Context);
      });
      MeasureStage(
        ECrowdDemoRoundPerformanceStage::TargetGuidance, [&]
      {
        ExecuteRoundTargetRegionGuidance(
          EntityManager, Context);
      });
    }

    const double MovementStart = FPlatformTime::Seconds();
    float GuidanceWorkMilliseconds = 0.0f;
    ExecuteRoundMovementWork(
      GuidanceWorkMilliseconds, EntityManager, Context);
    const float MovementMs = static_cast<float>(
      (FPlatformTime::Seconds() - MovementStart) * 1000.0);
    const float GuidanceMs = FMath::Clamp(
      GuidanceWorkMilliseconds, 0.0f, MovementMs);
    Pipeline->RecordPerformanceStage(
      ECrowdDemoRoundPerformanceStage::GuidanceCompose,
      GuidanceMs);
    const float LocalMs = MovementMs - GuidanceMs;
    Pipeline->RecordPerformanceStage(
      ECrowdDemoRoundPerformanceStage::LocalPredictive,
      LocalMs);
    MeasureStage(ECrowdDemoRoundPerformanceStage::Particle, [&]
    {
      if (Pipeline->GetRules().Scenario
        == ECrowdDemoScenario::SimRoundSoftPressure)
      {
        ExecuteRoundParticleConstraint(
          EntityManager, Context);
      }
      else
      {
        ExecuteRoundObstacleConstraint(
          EntityManager, Context);
      }
    });
    MeasureStage(
      ECrowdDemoRoundPerformanceStage::FacingFinalize, [&]
    {
      ExecuteRoundFacingBootstrap(EntityManager, Context);
    });
    if (!Pipeline->SubmitPreparedWorkerBootstrapInput())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerBootstrapSubmitFailed step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
      Pipeline->FailFixedStep();
      return false;
    }
    Pipeline->RecordPipelineFramePerformance(
      ExecutedSteps, TargetServerTime,
      false, false);
    return true;
  };
  (void)Submit();
  return bCommitted;
}

}

UCrowdDemoWorkerInputSyncProcessor::
UCrowdDemoWorkerInputSyncProcessor()
  : InputSyncQuery(*this)
{
  ExecutionFlags = static_cast<int32>(
    EProcessorExecutionFlags::Server
    | EProcessorExecutionFlags::Client
    | EProcessorExecutionFlags::Standalone);
  ProcessingPhase = EMassProcessingPhase::PrePhysics;
  bRequiresGameThreadExecution = true;
  bAutoRegisterWithProcessingPhases = false;
  QueryBasedPruning = EMassQueryBasedPruning::Never;
  ExecutionOrder.ExecuteInGroup =
    TEXT("CrowdDemo.Worker.InputSync");
  ExecutionOrder.ExecuteAfter.Add(TEXT("MassReplicationProcessor"));
  ExecutionOrder.ExecuteBefore.Add(
    TEXT("CrowdDemo.Worker.ResultApply"));
}

void UCrowdDemoWorkerInputSyncProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  ConfigureRoundPlanApplyQuery(InputSyncQuery);
}

void UCrowdDemoWorkerInputSyncProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!World || !Pipeline)
    return;
  ExecuteRoundPlanApply(InputSyncQuery, EntityManager, Context);
  if (!PublishBootstrapBoundarySnapshotFromMass(
      InputSyncQuery, EntityManager, Context))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBootstrapSnapshotPublishFailed"));
  }
}

UCrowdDemoWorkerResultApplyProcessor::
UCrowdDemoWorkerResultApplyProcessor()
  : ResultCommitQuery(*this)
{
  ExecutionFlags = static_cast<int32>(
    EProcessorExecutionFlags::Server
    | EProcessorExecutionFlags::Client
    | EProcessorExecutionFlags::Standalone);
  ProcessingPhase = EMassProcessingPhase::PrePhysics;
  bRequiresGameThreadExecution = true;
  bAutoRegisterWithProcessingPhases = false;
  QueryBasedPruning = EMassQueryBasedPruning::Never;
  ExecutionOrder.ExecuteInGroup =
    TEXT("CrowdDemo.Worker.ResultApply");
  ExecutionOrder.ExecuteAfter.Add(
    TEXT("CrowdDemo.Worker.InputSync"));
  ExecutionOrder.ExecuteBefore.Add(
    TEXT("CrowdDemoClientVisualMassProcessor"));
  ExecutionOrder.ExecuteBefore.Add(
    TEXT("CrowdDemoMassVisualStateProcessor"));
}

void UCrowdDemoWorkerResultApplyProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  ConfigureWorkerResultApplyQuery(ResultCommitQuery);
}

void UCrowdDemoWorkerResultApplyProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  ExecuteWorkerResultApply(ResultCommitQuery, EntityManager, Context);
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!World || !Pipeline || World->GetNetMode() == NM_Client)
    return;
  const bool bMassCommitted = AdvanceRoundWorkerFrame(
    ResultCommitQuery, EntityManager, Context);
  if (!bMassCommitted)
    return;
  if (Pipeline->PeekPreparedRoundCommitPlan())
  {
    Pipeline->ClearPreparedRoundCommitPlan();
  }
}
