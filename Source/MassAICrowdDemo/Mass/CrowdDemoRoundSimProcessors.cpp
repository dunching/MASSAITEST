#include "Mass/CrowdDemoRoundSimProcessors.h"
#include "Mass/CrowdDemoRoundInitialStateKernel.h"

#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoCapabilityProfileKernel.h"
#include "Mass/CrowdDemoFacingKernel.h"
#include "Mass/CrowdDemoGuidanceComposeKernel.h"
#include "Mass/CrowdDemoRoundWorkKernel.h"
#include "Mass/CrowdDemoMassCrowdRuntimeAdapter.h"
#include "MassCrowdFacingWork.h"
#include "MassCrowdMovementFinalizeWork.h"
#include "MassCrowdMovementPipelineWork.h"
#include "MassCrowdParticleWork.h"
#include "MassCrowdRuntimeFragments.h"
#include "MassCrowdSharedFlowWork.h"
#include "MassCrowdTargetRegionWork.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
#include "Mass/CrowdDemoLocalPredictiveInteractionKernel.h"
#include "Mass/CrowdDemoOpenSpawnRelaxationKernel.h"
#include "Mass/CrowdDemoOpenCohortMovementKernel.h"
#include "Mass/CrowdDemoBidirectionalSwapKernel.h"
#include "Mass/CrowdDemoValidCorridorTransitKernel.h"
#include "Mass/CrowdDemoCombatStateKernel.h"
#include "Mass/CrowdDemoProjectileKernel.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "GameFramework/GameStateBase.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "HAL/PlatformTime.h"
#include "Async/Async.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"

namespace
{
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
    TFuture<FCrowdMassTargetRegionTopologyOutput> Future = Async(
      EAsyncExecution::ThreadPool,
      [Input = MoveTemp(Input)]() mutable
      {
        return FCrowdMassTargetRegionWork::BuildTopology(Input);
      });
    FCrowdMassTargetRegionTopologyOutput Output = Future.Get();
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
    const bool bRefreshSourceAttachments)
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
    TFuture<FCrowdMassTargetRegionDemandOutput> Future = Async(
      EAsyncExecution::ThreadPool,
      [Input = MoveTemp(Input)]() mutable
      {
        return FCrowdMassTargetRegionWork::BuildDemand(Input);
      });
    FCrowdMassTargetRegionDemandOutput Output = Future.Get();
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
    const int32 PlanLifetimeSteps)
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
    TFuture<FCrowdMassTargetRegionPlanOutput> Future = Async(
      EAsyncExecution::ThreadPool,
      [Input = MoveTemp(Input)]() mutable
      {
        return FCrowdMassTargetRegionWork::SolvePlan(Input);
      });
    return Future.Get();
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
    TFuture<FCrowdTargetRegionPlanValidationResult> Future = Async(
      EAsyncExecution::ThreadPool,
      [Input = MoveTemp(Input)]() mutable
      {
        return FCrowdMassTargetRegionWork::ValidateExecution(Input);
      });
    return FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionValidation(
      Future.Get());
  }

  FCrowdMassTargetRegionGuidanceOutput RunTargetRegionGuidanceWork(
    const TConstArrayView<FCrowdDemoTargetRegionTransportAgent> Agents,
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan& Plan,
    const FCrowdDemoTargetRegionQuotaExecutionState& Execution)
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
    TFuture<FCrowdMassTargetRegionGuidanceOutput> Future = Async(
      EAsyncExecution::ThreadPool,
      [Input = MoveTemp(Input)]() mutable
      {
        return FCrowdMassTargetRegionWork::BuildGuidance(Input);
      });
    return Future.Get();
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

  template<typename TProcessor>
  TProcessor* MakeDynamicRoundProcessor(
    UObject& Outer,
    UObject& Owner,
    const TSharedRef<FMassEntityManager>& EntityManager)
  {
    TProcessor* Processor = NewObject<TProcessor>(&Outer);
    Processor->MarkAsDynamic();
    Processor->CallInitialize(&Owner, EntityManager);
    return Processor;
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

  bool MatchesCorrectionState(
    const FCrowdDemoSoftPressureRollbackAgentState& Local,
    const FCrowdDemoRoundAgentState& Authority)
  {
    if (Local.AgentId != Authority.AgentId
      || Local.LifecycleSerial != Authority.LifecycleSerial
      || !Local.Location.Equals(FVector(Authority.Location), 0.051f)
      || !Local.Velocity.Equals(FVector(Authority.Velocity), 0.051f)
      || FMath::Abs(FMath::FindDeltaAngleDegrees(Local.YawDegrees, Authority.YawDegrees)) > 0.01f
      || !FMath::IsNearlyEqual(Local.RadiusCm, Authority.RadiusCm, 0.01f)
      || !FCrowdDemoCombatNetState::StaticStruct()->CompareScriptStruct(
        &Local.Combat, &Authority.Combat, 0))
    {
      return false;
    }
    return true;
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

#define ROUND_DYNAMIC_FLAGS \
  ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone); \
  bAutoRegisterWithProcessingPhases = false; \
  bRequiresGameThreadExecution = true

UCrowdDemoRoundPlanApplyProcessor::UCrowdDemoRoundPlanApplyProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundPlanApplyProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassMovementFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoTargetCapabilityFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundProposedMovementFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoParticlePropertiesFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassStatsFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoBusinessStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRangedAttackFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionStepFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoHitFlashFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassFacingFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
  EntityQuery.AddTagRequirement<FCrowdMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundPlanApplyProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!World || !Pipeline)
  {
    return;
  }

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
  const float BoundaryTime = Pipeline->IsActive()
    ? Pipeline->GetSimulatedServerTimeSeconds()
    : GetRoundPipelineServerTime(*World);
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
      const TArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSimStateFragment>();
      const TArrayView<FCrowdDemoRoundFormationFragment> Formations = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFormationFragment>();
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        const FCrowdDemoRoundAgentState* const* BootstrapState = BootstrapById.Find(Identities[It].Id);
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
      const TArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSimStateFragment>();
      const TArrayView<FCrowdDemoRoundFormationFragment> Formations = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFormationFragment>();
      const auto RuntimeFacings =
        ChunkContext.GetMutableFragmentView<FCrowdMassFacingFragment>();
      const TArrayView<FCrowdDemoTargetCapabilityFragment> TargetCapabilities = ChunkContext.GetMutableFragmentView<FCrowdDemoTargetCapabilityFragment>();
      const TArrayView<FCrowdDemoRoundFlowSampleFragment> FlowSamples = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFlowSampleFragment>();
      const TArrayView<FCrowdDemoRoundProposedMovementFragment> ProposedMovements = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundProposedMovementFragment>();
      const TArrayView<FCrowdDemoRoundObstacleConstraintFragment> Constraints = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
      const TArrayView<FCrowdDemoParticlePropertiesFragment> ParticleProperties = ChunkContext.GetMutableFragmentView<FCrowdDemoParticlePropertiesFragment>();
      const auto Stats = ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
      const auto Businesses = ChunkContext.GetMutableFragmentView<FCrowdDemoBusinessStateFragment>();
      const auto Attacks = ChunkContext.GetMutableFragmentView<FCrowdDemoRangedAttackFragment>();
      const auto Reactives = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionFragment>();
      const auto ReactiveSteps = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionStepFragment>();
      const auto HitFlashes = ChunkContext.GetMutableFragmentView<FCrowdDemoHitFlashFragment>();
      const auto Visuals = ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
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
        if (DuePlan.Rules.SoftPressureTestCase
            == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse
          || DuePlan.Rules.SoftPressureTestCase
            == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat)
        {
          Stats[It] = FCrowdDemoMassStatsFragment();
          Businesses[It] = FCrowdDemoBusinessStateFragment();
          Attacks[It] = FCrowdDemoRangedAttackFragment();
          Reactives[It] = FCrowdDemoReactiveMotionFragment();
          ReactiveSteps[It] = FCrowdDemoReactiveMotionStepFragment();
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
            State.YawDegrees = LayoutAgent->CohortId == 0 ? 90.0f : -90.0f;
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
        if (DuePlan.Rules.TargetDistanceBandSettings.bEnabled != 0)
        {
          TargetCapabilities[It].MinimumFunctionalDistanceCm =
            DuePlan.Rules.TargetDistanceBandSettings.DefaultMinimumCombatCenterDistanceCm;
          TargetCapabilities[It].MaximumFunctionalDistanceCm =
            DuePlan.Rules.TargetDistanceBandSettings.DefaultMaximumCombatCenterDistanceCm;
        }
        FlowSamples[It] = FCrowdDemoRoundFlowSampleFragment();
        ProposedMovements[It] = FCrowdDemoRoundProposedMovementFragment();
        Constraints[It] = FCrowdDemoRoundObstacleConstraintFragment();
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
            TargetCapabilities[It].MinimumFunctionalDistanceCm =
              Profile.NormalizedMinimumCenterDistanceCm;
            TargetCapabilities[It].MaximumFunctionalDistanceCm =
              Profile.NormalizedMaximumCenterDistanceCm;
            FCrowdDemoCapabilityAgentAssignment& RuntimeAssignment =
              RuntimeCapabilityAssignments.AddDefaulted_GetRef();
            RuntimeAssignment.AgentId = Identities[It].Id;
            RuntimeAssignment.FormationIndex = Formation.FormationIndex;
            RuntimeAssignment.ProfileId = Profile.ProfileId;
            RuntimeAssignment.CapabilityProfileKey = Profile.CapabilityProfileKey;
          }
        }
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
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        if (States[It].bInitialized)
        {
          const FCrowdDemoCombatNetState Combat = MakeCombatNetState(
            Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
          StatesOut.Add(MakeRoundAgentState(
            Identities[It], Formations[It], States[It], &Combat));
        }
      }
    });
    SortAgentStates(StatesOut);
    return StatesOut;
  };

  FCrowdDemoCorrectionFrame Correction;
  float ReceiveServerTime = 0.0f;
  int32 DiagnosticCorrectionFixedStep = INDEX_NONE;
  if (World->GetNetMode() == NM_Client && Pipeline->PopCorrectionForBoundary(Correction, ReceiveServerTime))
  {
    const double CorrectionApplyStartSeconds = FPlatformTime::Seconds();
    DiagnosticCorrectionFixedStep = FMath::Max(0, FMath::RoundToInt(
      (Correction.ServerTimeSeconds - Pipeline->GetActivePlan().StartServerTimeSeconds)
      / Pipeline->GetCurrentFixedStepSeconds()) - 1);
    const bool bSoftPressureScenario =
      Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure;
    const FCrowdDemoSoftPressureRollbackSnapshot* SoftPressureRollbackSnapshot =
      bSoftPressureScenario
      ? Pipeline->FindSoftPressureRollbackSnapshot(DiagnosticCorrectionFixedStep)
      : nullptr;
    if (SoftPressureRollbackSnapshot
      && !Pipeline->IsSoftPressureRollbackSnapshotReadyForReplay(
        DiagnosticCorrectionFixedStep))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoRollbackSnapshotIncomplete step=%d movement=%d combat=%d ready=%d"),
        DiagnosticCorrectionFixedStep,
        SoftPressureRollbackSnapshot->bMovementFactsComplete ? 1 : 0,
        SoftPressureRollbackSnapshot->bCombatFactsComplete ? 1 : 0,
        SoftPressureRollbackSnapshot->bSnapshotReadyForReplay ? 1 : 0);
      SoftPressureRollbackSnapshot = nullptr;
    }
    TArray<FCrowdDemoRoundAgentState> BeforeCorrection;
    TMap<int32, const FCrowdDemoSoftPressureRollbackAgentState*> SoftPressureRollbackById;
    bool bSoftPressureAgentMismatch = false;
    if (SoftPressureRollbackSnapshot)
    {
      bSoftPressureAgentMismatch = SoftPressureRollbackSnapshot->Agents.Num() != EntityCount
        || SoftPressureRollbackSnapshot->Agents.Num() != Correction.AgentStates.Num();
      for (const auto& Agent : SoftPressureRollbackSnapshot->Agents)
      {
        if (SoftPressureRollbackById.Contains(Agent.AgentId))
          bSoftPressureAgentMismatch = true;
        SoftPressureRollbackById.Add(Agent.AgentId, &Agent);
      }
      for (const auto& ServerAgent : Correction.AgentStates)
      {
        const FCrowdDemoSoftPressureRollbackAgentState* const* Local =
          SoftPressureRollbackById.Find(ServerAgent.AgentId);
        if (!Local || (*Local)->LifecycleSerial != ServerAgent.LifecycleSerial
          || !FMath::IsNearlyEqual((*Local)->RadiusCm, ServerAgent.RadiusCm, 0.01f))
        {
          bSoftPressureAgentMismatch = true;
          break;
        }
      }
      if (bSoftPressureAgentMismatch)
        SoftPressureRollbackById.Reset();
    }
    const bool bValidSoftPressureRollback =
      SoftPressureRollbackSnapshot && !bSoftPressureAgentMismatch;
    if (bValidSoftPressureRollback)
    {
      for (const auto& Agent : SoftPressureRollbackSnapshot->Agents)
      {
        FCrowdDemoRoundAgentState& CompareState = BeforeCorrection.AddDefaulted_GetRef();
        CompareState.AgentId = Agent.AgentId;
        CompareState.LifecycleSerial = Agent.LifecycleSerial;
        CompareState.Location = Agent.Location;
        CompareState.Velocity = Agent.Velocity;
        CompareState.YawDegrees = Agent.YawDegrees;
        CompareState.RadiusCm = Agent.RadiusCm;
        CompareState.Combat = Agent.Combat;
      }
    }
    else
    {
      BeforeCorrection = GatherStates();
    }
    bool bZeroErrorCorrection = bValidSoftPressureRollback;
    if (bZeroErrorCorrection)
    {
      for (const FCrowdDemoRoundAgentState& ServerAgent : Correction.AgentStates)
      {
        const FCrowdDemoSoftPressureRollbackAgentState* const* Local =
          SoftPressureRollbackById.Find(ServerAgent.AgentId);
        if (!Local || !MatchesCorrectionState(**Local, ServerAgent))
        {
          bZeroErrorCorrection = false;
          break;
        }
      }
    }
    int32 CorrectionReplayStepCount = bValidSoftPressureRollback && !bZeroErrorCorrection
      ? FMath::Max(0, FMath::RoundToInt(
          (BoundaryTime - Correction.ServerTimeSeconds)
          / Pipeline->GetCurrentFixedStepSeconds()))
      : 0;
    if (bSoftPressureScenario)
    {
      Pipeline->RecordSoftPressureRollbackOutcome(
        bValidSoftPressureRollback, bSoftPressureAgentMismatch, CorrectionReplayStepCount);
    }
    Pipeline->RecordCorrectionComparisonAndApplied(BeforeCorrection, Correction, BoundaryTime);
    // Target guidance is quantized after evaluating the exact world-space
    // position. Even a sub-tolerance local float delta can cross that guidance
    // quantum, so a non-replayable correction must establish one authoritative
    // physical and business-state boundary before replay.
    bool bNeedsAuthoritativeState = !bValidSoftPressureRollback;
    if (!bNeedsAuthoritativeState)
    {
      TMap<int32, const FCrowdDemoRoundAgentState*> BeforeById;
      for (const FCrowdDemoRoundAgentState& Agent : BeforeCorrection) BeforeById.Add(Agent.AgentId, &Agent);
      for (const FCrowdDemoRoundAgentState& ServerAgent : Correction.AgentStates)
      {
        const FCrowdDemoRoundAgentState* const* Local = BeforeById.Find(ServerAgent.AgentId);
        if (!Local
          || !FVector((*Local)->Location).Equals(FVector(ServerAgent.Location), 0.051f)
          || !FVector((*Local)->Velocity).Equals(FVector(ServerAgent.Velocity), 0.051f)
          || FMath::Abs(FMath::FindDeltaAngleDegrees((*Local)->YawDegrees, ServerAgent.YawDegrees)) > 0.01f)
        {
          bNeedsAuthoritativeState = true;
          break;
        }
      }
    }
    if (!bZeroErrorCorrection)
    {
      if (bValidSoftPressureRollback)
      {
        Pipeline->RestoreSoftPressureRuntime(*SoftPressureRollbackSnapshot);
      }
      TMap<int32, const FCrowdDemoRoundAgentState*> CorrectionById;
      for (const FCrowdDemoRoundAgentState& Agent : Correction.AgentStates)
      {
        CorrectionById.Add(Agent.AgentId, &Agent);
      }
      EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
      {
        const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
        const TArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSimStateFragment>();
        const auto FlowSamples = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFlowSampleFragment>();
        const auto RuntimeFacings =
          ChunkContext.GetMutableFragmentView<FCrowdMassFacingFragment>();
        const auto Stats = ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
        const auto Businesses = ChunkContext.GetMutableFragmentView<FCrowdDemoBusinessStateFragment>();
        const auto Attacks = ChunkContext.GetMutableFragmentView<FCrowdDemoRangedAttackFragment>();
        const auto Reactives = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionFragment>();
        const auto HitFlashes = ChunkContext.GetMutableFragmentView<FCrowdDemoHitFlashFragment>();
        const auto Visuals = ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
        for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
        {
          if (const FCrowdDemoSoftPressureRollbackAgentState* const* Rollback =
            SoftPressureRollbackById.Find(Identities[It].Id))
          {
            States[It].Location = (*Rollback)->Location;
            States[It].Velocity = (*Rollback)->Velocity;
            States[It].YawDegrees = (*Rollback)->YawDegrees;
            States[It].SimulatedServerTimeSeconds = (*Rollback)->SimulatedServerTimeSeconds;
            States[It].PlanRevision = (*Rollback)->PlanRevision;
            States[It].bInitialized = (*Rollback)->bInitialized;
            FlowSamples[It] = (*Rollback)->FlowSample;
            RuntimeFacings[It] = (*Rollback)->Facing;
            ApplyCombatNetState(
              (*Rollback)->Combat, Stats[It], Businesses[It], Attacks[It],
              Reactives[It], HitFlashes[It], Visuals[It]);
          }
          if (bNeedsAuthoritativeState)
          {
            if (const FCrowdDemoRoundAgentState* const* Corrected = CorrectionById.Find(Identities[It].Id))
            {
              States[It].Location = FVector((*Corrected)->Location);
              States[It].Velocity = FVector((*Corrected)->Velocity);
              States[It].YawDegrees = (*Corrected)->YawDegrees;
              RuntimeFacings[It] = FCrowdMassFacingFragment();
              RuntimeFacings[It].Value.AgentId = Identities[It].Id;
              RuntimeFacings[It].Value.ResolvedYawDegrees =
                (*Corrected)->YawDegrees;
              RuntimeFacings[It].PlanRevision =
                Pipeline->GetCurrentPlanRevision();
              ApplyCombatNetState(
                (*Corrected)->Combat, Stats[It], Businesses[It], Attacks[It],
                Reactives[It], HitFlashes[It], Visuals[It]);
            }
          }
          States[It].SimulatedServerTimeSeconds = Correction.ServerTimeSeconds;
        }
      });
      Pipeline->SetSimulatedServerTimeForCorrection(Correction.ServerTimeSeconds);
    }
    if (bSoftPressureScenario)
    {
      Pipeline->BeginRollbackReplayPerformance(
        CorrectionReplayStepCount,
        static_cast<float>((FPlatformTime::Seconds() - CorrectionApplyStartSeconds) * 1000.0),
        bZeroErrorCorrection);
    }
  }

  FCrowdDemoRoundResultPacket Result;
  if (World->GetNetMode() == NM_Client && Pipeline->PopRoundResultForBoundary(Result))
  {
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
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        if (const FCrowdDemoRoundAgentState* const* Corrected = ResultById.Find(Identities[It].Id))
        {
          States[It].Location = FVector((*Corrected)->Location);
          States[It].Velocity = FVector((*Corrected)->Velocity);
          States[It].YawDegrees = (*Corrected)->YawDegrees;
          States[It].SimulatedServerTimeSeconds = Result.EndServerTimeSeconds;
          ApplyCombatNetState(
            (*Corrected)->Combat, Stats[It], Businesses[It], Attacks[It],
            Reactives[It], HitFlashes[It], Visuals[It]);
        }
      }
    });
    Pipeline->SetSimulatedServerTimeForCorrection(Result.EndServerTimeSeconds);
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

UCrowdDemoRoundSharedFlowFieldBuildProcessor::UCrowdDemoRoundSharedFlowFieldBuildProcessor()
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundSharedFlowFieldBuildProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UCrowdDemoRoundSharedFlowFieldBuildProcessor::Execute(
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
    if (!Pipeline->EnsureBidirectionalSwapFlowFields())
      UE_LOG(LogTemp, Error, TEXT("VIOLATION CrowdDemoT3FlowFieldBuildFailed"));
  }
  Pipeline->LogStageOnce(TEXT("02_shared_flow_field_build"), 0);
}

UCrowdDemoRoundFlowPreferredVelocityProcessor::UCrowdDemoRoundFlowPreferredVelocityProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundFlowPreferredVelocityProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassGuidanceCandidatesFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
  EntityQuery.AddTagRequirement<FCrowdMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundFlowPreferredVelocityProcessor::Execute(
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
  const FCrowdDemoRoundRules& Rules = Pipeline->GetRules();
  FCrowdMassSharedFlowSampleInput WorkInput;
  WorkInput.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  WorkInput.PlanRevision = Pipeline->GetCurrentPlanRevision();
  WorkInput.FixedStepSeconds = Rules.FixedStepSeconds;
  WorkInput.Fields.Add(&Pipeline->GetRuntimeSharedFlowField());
  if (Pipeline->IsBidirectionalSwap())
  {
    WorkInput.Fields.Add(Pipeline->FindRuntimeBidirectionalSwapFlowField(0));
    WorkInput.Fields.Add(Pipeline->FindRuntimeBidirectionalSwapFlowField(10));
  }
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
    const auto* Formation = Pipeline->FindBoundaryFormationFact(
      Record.Identity.AgentId);
    if (!Formation)
    {
      bGatherValid = false;
      continue;
    }
    FCrowdMassSharedFlowAgentInput Agent;
    Agent.AgentId = Record.Identity.AgentId;
    Agent.LifecycleSerial = Record.Identity.LifecycleSerial;
    Agent.Location = Record.State.Position;
    Agent.CurrentYawDegrees = Record.State.YawDegrees;
    Agent.MaximumSpeedCmps = Rules.MaxSpeedCmPerSecond;
    Agent.FieldIndex = 0;
    FVector Goal = FVector(Rules.FlowFieldConfig.GoalLocation);
    if (Pipeline->IsBidirectionalSwap())
    {
      const int32 CohortId = FCrowdDemoBidirectionalSwapKernel::
        CohortIdForFormationIndex(Formation->FormationIndex);
      Agent.FieldIndex = CohortId + 1;
      Goal = FVector(FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(
        CohortId).GoalLocation);
    }
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
  TFuture<FCrowdMassSharedFlowSampleOutput> Future = Async(
    EAsyncExecution::ThreadPool,
    [Input = MoveTemp(WorkInput)]() mutable
    {
      return FCrowdMassSharedFlowWork::BuildPreferred(Input);
    });
  FCrowdMassSharedFlowSampleOutput WorkOutput = Future.Get();
  if (!WorkOutput.bValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoSharedFlowRuntimeWorkInvalid step=%d agents=%d"),
      Pipeline->GetCurrentFixedStepIndex(), WorkOutput.Agents.Num());
    return;
  }
  Pipeline->SetPreparedRuntimeSharedFlowOutputs(
    TArray<FCrowdMassSharedFlowAgentOutput>(WorkOutput.Agents));
  TMap<int32, const FCrowdMassSharedFlowAgentOutput*> ById;
  for (const FCrowdMassSharedFlowAgentOutput& Value : WorkOutput.Agents)
    ById.Add(Value.AgentId, &Value);
  bool bIdentityValid = true;
  int32 ValidatedCount = 0;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      bIdentityValid &= ById.Contains(Identities[It].Id);
      ++ValidatedCount;
    }
  });
  if (!bIdentityValid || ValidatedCount != WorkOutput.Agents.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoSharedFlowRuntimeIdentityInvalid step=%d validated=%d results=%d"),
      Pipeline->GetCurrentFixedStepIndex(), ValidatedCount, WorkOutput.Agents.Num());
    return;
  }
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto RuntimeCandidates =
      ChunkContext.GetMutableFragmentView<FCrowdMassGuidanceCandidatesFragment>();
    const auto FlowSamples =
      ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdMassSharedFlowAgentOutput& Value = **ById.Find(Identities[It].Id);
      RuntimeCandidates[It] = {};
      RuntimeCandidates[It].SharedFlow = Value.Candidate;
      const FCrowdDemoSharedFlowSample Sample =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowSample(Value.Sample);
      FCrowdDemoRoundFlowSampleFragment& FlowSample = FlowSamples[It];
      FlowSample = {};
      FlowSample.CellIndex = Sample.CellIndex;
      FlowSample.StableCellKey = Sample.StableCellKey;
      FlowSample.NavigationNodeKey = Sample.NavigationNodeKey;
      FlowSample.NextNavigationNodeKey = Sample.NextNavigationNodeKey;
      FlowSample.Status = Sample.Status;
      FlowSample.FlowDirection = Sample.FlowDirection;
      FlowSample.IntegrationCost = Sample.IntegrationCost;
      FlowSample.GuidanceDistanceCm = Sample.GuidanceDistanceCm;
      FlowSample.bBlocked = Sample.bBlocked;
      FlowSample.bUnreachable = Sample.bUnreachable;
      FlowSample.bRecoveredFromRasterMismatch =
        Sample.bRecoveredFromRasterMismatch;
      FlowSample.bSourceAttached = Sample.bSourceAttached;
    }
  });
  Pipeline->RecordFlowConnectivityStep(
    WorkOutput.RecoveredAgentCount, WorkOutput.DesiredSegmentViolationCount,
    WorkOutput.SourceAttachmentSuccessCount, WorkOutput.UnreachableSampleCount);
  Pipeline->LogStageOnce(
    TEXT("03_flow_preferred_velocity"), WorkOutput.Agents.Num());
}

UCrowdDemoRoundBoundaryGatherProcessor::
UCrowdDemoRoundBoundaryGatherProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundBoundaryGatherProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassMovementFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoParticlePropertiesFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(
    EMassFragmentPresence::All);
}

void UCrowdDemoRoundBoundaryGatherProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive()) return;
  TArray<FCrowdMassBoundaryAgentRecord> Records;
  TArray<FCrowdDemoRoundBoundaryFormationFact> FormationFacts;
  bool bGatherValid = true;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<
      FCrowdDemoMassIdentityFragment>();
    const auto Formations = ChunkContext.GetFragmentView<
      FCrowdDemoRoundFormationFragment>();
    const auto States = ChunkContext.GetFragmentView<
      FCrowdDemoRoundSimStateFragment>();
    const auto Movements = ChunkContext.GetFragmentView<
      FCrowdDemoMassMovementFragment>();
    const auto Particles = ChunkContext.GetFragmentView<
      FCrowdDemoParticlePropertiesFragment>();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdMassBoundaryAgentRecord Record;
      if (!FCrowdDemoMassCrowdRuntimeAdapter::BuildBoundaryAgentRecord(
        Identities[It], States[It], Movements[It], Particles[It], Record))
      {
        bGatherValid = false;
        continue;
      }
      Records.Add(MoveTemp(Record));
      FormationFacts.Add({
        Identities[It].Id,
        Formations[It].FormationIndex,
        Formations[It].RadiusCm});
    }
  });
  FCrowdMassBoundarySnapshot Snapshot;
  if (bGatherValid)
    FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
      Pipeline->GetCurrentFixedStepIndex(),
      Pipeline->GetCurrentPlanRevision(), Records, Snapshot);
  if (!bGatherValid || !Pipeline->PublishBoundarySnapshot(
      MoveTemp(Snapshot), MoveTemp(FormationFacts)))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBoundaryGatherInvalid step=%d records=%d"),
      Pipeline->GetCurrentFixedStepIndex(), Records.Num());
  }
}

UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor::
UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor()
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor::Execute(
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

UCrowdDemoRoundTargetFactApplyProcessor::UCrowdDemoRoundTargetFactApplyProcessor()
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundTargetFactApplyProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UCrowdDemoRoundTargetFactApplyProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
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

UCrowdDemoRoundTargetPolarTopologyBuildProcessor::
UCrowdDemoRoundTargetPolarTopologyBuildProcessor()
{
  ROUND_DYNAMIC_FLAGS;
  QueryBasedPruning = EMassQueryBasedPruning::Never;
}

void UCrowdDemoRoundTargetPolarTopologyBuildProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UCrowdDemoRoundTargetPolarTopologyBuildProcessor::Execute(
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
      const bool bBuildTopology = !bStaticTargetForRound || !Runtime.Topology.bValid;
      if (bBuildTopology)
      {
        RunTargetRegionTopologyWork(
          Settings, Pipeline->GetRules().FlowFieldConfig,
          Runtime.Topology, Runtime.TopologySummary);
      }
      Pipeline->RecordTargetTopologyPerformance(bBuildTopology);
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
  const bool bBuildTopology = !bStaticTargetForRound
    || !Pipeline->GetPreparedTargetRegionTopology().bValid;
  if (bBuildTopology)
  {
    RunTargetRegionTopologyWork(
      Settings, Pipeline->GetRules().FlowFieldConfig,
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetTargetRegionTopologySummary());
  }
  Pipeline->RecordTargetTopologyPerformance(bBuildTopology);
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

UCrowdDemoRoundTargetRegionPopulationBuildProcessor::
UCrowdDemoRoundTargetRegionPopulationBuildProcessor() : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
  QueryBasedPruning = EMassQueryBasedPruning::Never;
}

void UCrowdDemoRoundTargetRegionPopulationBuildProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UCrowdDemoRoundTargetRegionPopulationBuildProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || Pipeline->GetRules().TargetRegionTransportSettings.bEnabled == 0) return;
  if (!Pipeline->IsBoundarySnapshotCurrent()
    || Pipeline->GetPreparedRuntimeSharedFlowOutputs().Num()
      != Pipeline->GetBoundarySnapshot().Agents.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetDemandBoundaryInputInvalid step=%d snapshot=%d flow=%d"),
      Pipeline->GetCurrentFixedStepIndex(),
      Pipeline->GetBoundarySnapshot().Agents.Num(),
      Pipeline->GetPreparedRuntimeSharedFlowOutputs().Num());
    return;
  }
  TMap<int32, const FCrowdMassSharedFlowAgentOutput*> FlowByAgentId;
  for (const auto& Value : Pipeline->GetPreparedRuntimeSharedFlowOutputs())
    FlowByAgentId.Add(Value.AgentId, &Value);
  if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
  {
    TMap<uint32, TArray<FCrowdDemoTargetRegionTransportAgent>> AgentsByProfile;
    for (const FCrowdMassBoundaryAgentRecord& Record
      : Pipeline->GetBoundarySnapshot().Agents)
    {
      const auto* Flow = FlowByAgentId.FindRef(Record.Identity.AgentId);
      if (!Flow)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoTargetDemandFlowMissing step=%d agent_id=%d"),
          Pipeline->GetCurrentFixedStepIndex(), Record.Identity.AgentId);
        return;
      }
      FCrowdDemoTargetRegionTransportAgent& Agent = AgentsByProfile.FindOrAdd(
        Record.Properties.CapabilityProfileKey).AddDefaulted_GetRef();
      Agent.AgentId = Record.Identity.AgentId;
      Agent.Location = FVector2f(
        Record.State.Position.X, Record.State.Position.Y);
      Agent.Velocity = FVector2f(
        Record.State.Velocity.X, Record.State.Velocity.Y);
      Agent.MaxSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
      Agent.FarFlowPreferredVelocity =
        FCrowdTargetRegionTransportKernel::ComposeTargetAdvectedFarFlowVelocity(
          FVector2f(Flow->Candidate.PreferredVelocity.X,
            Flow->Candidate.PreferredVelocity.Y),
          FVector2f(Pipeline->GetTargetFact().Velocity.X,
            Pipeline->GetTargetFact().Velocity.Y),
          Agent.MaxSpeedCmps);
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
      if (bStaticTargetForRound && Runtime.Demand.bValid)
      {
        const bool bRefreshSourceAttachments = !Runtime.Plan.bValid
          || !Runtime.Validation.bValid
          || Pipeline->GetCurrentFixedStepIndex() - Runtime.Plan.BuildFixedStepIndex
            >= Pipeline->GetRules().TargetRegionTransportSettings.PlanLifetimeSteps;
        RunTargetRegionDemandWork(
          Runtime.Agents, ExternalAgents, Settings,
          Pipeline->GetRules().FlowFieldConfig,
          &Pipeline->GetRuntimeSharedFlowField(), Runtime.Topology,
          Runtime.Demand, true, bRefreshSourceAttachments);
        Pipeline->RecordTargetDemandPerformance(false);
      }
      else
      {
        RunTargetRegionDemandWork(
          Runtime.Agents, ExternalAgents, Settings,
          Pipeline->GetRules().FlowFieldConfig,
          &Pipeline->GetRuntimeSharedFlowField(), Runtime.Topology,
          Runtime.Demand, false, true);
        Pipeline->RecordTargetDemandPerformance(true);
      }
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
    const auto* Flow = FlowByAgentId.FindRef(Record.Identity.AgentId);
    if (!Flow)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoTargetDemandFlowMissing step=%d agent_id=%d"),
        Pipeline->GetCurrentFixedStepIndex(), Record.Identity.AgentId);
      return;
    }
    auto& Agent = Agents.AddDefaulted_GetRef();
    Agent.AgentId = Record.Identity.AgentId;
    Agent.Location = FVector2f(
      Record.State.Position.X, Record.State.Position.Y);
    Agent.Velocity = FVector2f(
      Record.State.Velocity.X, Record.State.Velocity.Y);
    Agent.MaxSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
    Agent.FarFlowPreferredVelocity =
      FCrowdTargetRegionTransportKernel::ComposeTargetAdvectedFarFlowVelocity(
        FVector2f(Flow->Candidate.PreferredVelocity.X,
          Flow->Candidate.PreferredVelocity.Y),
        FVector2f(Pipeline->GetTargetFact().Velocity.X,
          Pipeline->GetTargetFact().Velocity.Y),
        Agent.MaxSpeedCmps);
  }
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  const auto Settings = MakeTargetRegionTransportSettings(
    Pipeline->GetRules(), Pipeline->GetTargetFact());
  const bool bStaticTargetForRound = FVector(
    Pipeline->GetRules().TargetMotion.LinearVelocity).IsNearlyZero(0.01f);
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
      &Pipeline->GetRuntimeSharedFlowField(),
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetPreparedTargetRegionDemand(), true,
      bRefreshSourceAttachments);
    Pipeline->RecordTargetDemandPerformance(false);
  }
  else
  {
    RunTargetRegionDemandWork(
      Agents, {}, Settings, Pipeline->GetRules().FlowFieldConfig,
      &Pipeline->GetRuntimeSharedFlowField(),
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetPreparedTargetRegionDemand(), false, true);
    Pipeline->RecordTargetDemandPerformance(true);
  }
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

UCrowdDemoRoundTargetRegionTransportSolveProcessor::
UCrowdDemoRoundTargetRegionTransportSolveProcessor()
{
  ROUND_DYNAMIC_FLAGS;
  QueryBasedPruning = EMassQueryBasedPruning::Never;
}

void UCrowdDemoRoundTargetRegionTransportSolveProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UCrowdDemoRoundTargetRegionTransportSolveProcessor::Execute(
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
      FCrowdMassTargetRegionPlanOutput WorkOutput = RunTargetRegionPlanWork(
        Runtime.Topology, Runtime.Demand, PreviousPlan, PreviousExecution,
        Step, TargetRevision,
        Pipeline->GetRules().TargetRegionTransportSettings.PlanLifetimeSteps);
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
  FCrowdMassTargetRegionPlanOutput WorkOutput = RunTargetRegionPlanWork(
    Topology, Demand, Plan, Pipeline->GetTargetRegionQuotaExecution(),
    Step, TargetRevision,
    Pipeline->GetRules().TargetRegionTransportSettings.PlanLifetimeSteps);
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

UCrowdDemoRoundTargetRegionGuidanceProcessor::
UCrowdDemoRoundTargetRegionGuidanceProcessor() : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundTargetRegionGuidanceProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundTargetRegionGuidanceProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || Pipeline->GetRules().TargetRegionTransportSettings.bEnabled == 0) return;
  if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
  {
    TMap<int32, const FCrowdDemoTargetRegionGuidanceResult*> ById;
    bool bAllValid = true;
    for (FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime : Pipeline->GetCapabilityCohorts())
    {
      const auto Settings = MakeTargetRegionTransportSettings(
        Pipeline->GetRules(), Pipeline->GetTargetFact(), &Runtime.Cohort.Profile,
        Runtime.DemandRegionPhaseOffset);
      FCrowdMassTargetRegionGuidanceOutput WorkOutput =
        RunTargetRegionGuidanceWork(
          Runtime.Agents, Settings, Runtime.Topology, Runtime.Demand,
          Runtime.Plan, Runtime.QuotaExecution);
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
    int32 Applied = 0;
    TArray<FCrowdGuidanceCandidate> PreparedTargetCandidates;
    EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
    {
      const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
      const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        const auto* const* Result = ById.Find(Identities[It].Id);
        if (!Result) continue;
        const FVector DesiredLocation(
          Pipeline->GetTargetFact().Location.X,
          Pipeline->GetTargetFact().Location.Y, States[It].Location.Z);
        const FVector DesiredVelocity(
          (*Result)->DesiredVelocity.X, (*Result)->DesiredVelocity.Y, 0.0f);
        const float DesiredYawDegrees = DesiredVelocity.IsNearlyZero()
          ? States[It].YawDegrees : DesiredVelocity.Rotation().Yaw;
        const FCrowdDemoGuidanceCandidate Candidate =
          FCrowdDemoGuidanceComposeKernel::BuildCandidate(
            Identities[It].Id, ECrowdDemoGuidanceProvider::TargetRegion,
            Pipeline->GetCurrentPlanRevision(), DesiredVelocity, DesiredLocation,
            DesiredYawDegrees,
            (*Result)->Mode != ECrowdDemoTargetRegionGuidanceMode::Unrouted);
        if (Candidate.bValid)
          PreparedTargetCandidates.Add(
            FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(
              Candidate));
        ++Applied;
      }
    });
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
  FCrowdMassTargetRegionGuidanceOutput WorkOutput =
    RunTargetRegionGuidanceWork(
      Pipeline->GetPreparedTargetRegionAgents(), Settings,
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetPreparedTargetRegionDemand(),
      Pipeline->GetPreparedTargetRegionPlan(),
      Pipeline->GetTargetRegionQuotaExecution());
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
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      if (const auto* const* Result = ById.Find(Identities[It].Id))
      {
        const FVector DesiredLocation(
          Pipeline->GetTargetFact().Location.X,
          Pipeline->GetTargetFact().Location.Y, States[It].Location.Z);
        const FVector DesiredVelocity(
          (*Result)->DesiredVelocity.X, (*Result)->DesiredVelocity.Y, 0.0f);
        const float DesiredYawDegrees = DesiredVelocity.IsNearlyZero()
          ? States[It].YawDegrees : DesiredVelocity.Rotation().Yaw;
        const FCrowdDemoGuidanceCandidate Candidate =
          FCrowdDemoGuidanceComposeKernel::BuildCandidate(
            Identities[It].Id, ECrowdDemoGuidanceProvider::TargetRegion,
            Pipeline->GetCurrentPlanRevision(), DesiredVelocity, DesiredLocation,
            DesiredYawDegrees,
            (*Result)->Mode != ECrowdDemoTargetRegionGuidanceMode::Unrouted);
      if (Candidate.bValid)
          PreparedTargetCandidates.Add(
            FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(
              Candidate));
        ++Applied;
      }
  });
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


UCrowdDemoRoundRangedCombatProcessor::UCrowdDemoRoundRangedCombatProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundRangedCombatProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassStatsFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoBusinessStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRangedAttackFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoHitFlashFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundRangedCombatProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsRangedProjectileCombat())
    return;
  TArray<FCrowdDemoRangedCombatAgent> Agents;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Stats = ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
    const auto Businesses = ChunkContext.GetMutableFragmentView<FCrowdDemoBusinessStateFragment>();
    const auto Attacks = ChunkContext.GetMutableFragmentView<FCrowdDemoRangedAttackFragment>();
    const auto Reactives = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionFragment>();
    const auto HitFlashes = ChunkContext.GetMutableFragmentView<FCrowdDemoHitFlashFragment>();
    const auto Visuals = ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoRangedCombatAgent& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.LifecycleSerial = Identities[It].LifecycleSerial;
      Agent.FormationIndex = Formations[It].FormationIndex;
      Agent.Position = States[It].Location;
      Agent.RadiusCm = Formations[It].RadiusCm;
      Agent.bAlive = Stats[It].bAlive;
      Agent.Combat = MakeCombatAgentState(
        Identities[It], Stats[It], Businesses[It], Attacks[It],
        Reactives[It], HitFlashes[It], Visuals[It]);
    }
  });
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });

  FCrowdDemoProjectileStepSummary Summary;
  TArray<FCrowdDemoProjectileSpawnRequest> Requests;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  TArray<FCrowdDemoHitFact> HitFacts;
  const int32 FixedStep = Pipeline->GetCurrentFixedStepIndex();
  const FCrowdDemoRangedCombatSettings& Settings =
    Pipeline->GetRules().RangedCombatSettings;
  FCrowdDemoProjectileKernel::AdvanceAttackPhases(
    Pipeline->GetCurrentRoundId(), FixedStep, Settings, Agents, Requests, Summary);
  FCrowdDemoProjectileKernel::SpawnProjectiles(
    FixedStep, Pipeline->GetCurrentStepEndServerTimeSeconds(), Settings,
    Requests, Pipeline->GetPreparedProjectiles(), Events, Summary);
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
    FixedStep, Pipeline->GetCurrentStepEndServerTimeSeconds(),
    Pipeline->GetCurrentFixedStepSeconds(), Settings, Agents,
    Pipeline->GetPreparedProjectiles(), HitFacts, Events, Summary);
  Pipeline->SetPendingProjectileHitFacts(MoveTemp(HitFacts));
  Pipeline->RecordProjectileStep(Summary, Events);

  TMap<int32, const FCrowdDemoRangedCombatAgent*> AgentsById;
  for (const FCrowdDemoRangedCombatAgent& Agent : Agents)
    AgentsById.Add(Agent.AgentId, &Agent);
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Stats = ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
    const auto Businesses = ChunkContext.GetMutableFragmentView<FCrowdDemoBusinessStateFragment>();
    const auto Attacks = ChunkContext.GetMutableFragmentView<FCrowdDemoRangedAttackFragment>();
    const auto Reactives = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionFragment>();
    const auto HitFlashes = ChunkContext.GetMutableFragmentView<FCrowdDemoHitFlashFragment>();
    const auto Visuals = ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      if (const FCrowdDemoRangedCombatAgent* const* Agent = AgentsById.Find(Identities[It].Id))
        ApplyCombatAgentState(
          (*Agent)->Combat, Stats[It], Businesses[It], Attacks[It],
          Reactives[It], HitFlashes[It], Visuals[It]);
  });
  if (auto* MassSubsystem = World->GetSubsystem<UCrowdDemoMassSubsystem>())
    MassSubsystem->MirrorProjectileStates(Pipeline->GetPreparedProjectiles());
  Pipeline->LogStageOnce(TEXT("03_ranged_projectile_combat"), Agents.Num());
}

UCrowdDemoRoundHitResponseBoundaryApplyProcessor::UCrowdDemoRoundHitResponseBoundaryApplyProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundHitResponseBoundaryApplyProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassStatsFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoBusinessStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRangedAttackFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoHitFlashFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundHitResponseBoundaryApplyProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  const bool bShowcase = Pipeline && Pipeline->IsActive()
    && Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && Pipeline->GetRules().SoftPressureTestCase
      == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse;
  const bool bProjectileCombat = Pipeline && Pipeline->IsRangedProjectileCombat();
  if (!bShowcase && !bProjectileCombat)
    return;

  struct FBinding
  {
    int32 AgentId = INDEX_NONE;
    int32 FormationIndex = INDEX_NONE;
    FVector Location = FVector::ZeroVector;
  };
  TArray<FCrowdDemoCombatAgentState> Agents;
  TArray<FBinding> Bindings;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Stats = ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
    const auto Businesses = ChunkContext.GetMutableFragmentView<FCrowdDemoBusinessStateFragment>();
    const auto Attacks = ChunkContext.GetMutableFragmentView<FCrowdDemoRangedAttackFragment>();
    const auto Reactives = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionFragment>();
    const auto HitFlashes = ChunkContext.GetMutableFragmentView<FCrowdDemoHitFlashFragment>();
    const auto Visuals = ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      auto Agent = MakeCombatAgentState(
        Identities[It], Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
      if (bShowcase && Pipeline->GetCurrentFixedStepIndex() == 0
        && Agent.BusinessStateRevision == 0)
      {
        Agent.BusinessState = Formations[It].FormationIndex < 4
          ? ECrowdDemoBusinessState::Idle
          : Formations[It].FormationIndex < 8
            ? ECrowdDemoBusinessState::Moving
            : Formations[It].FormationIndex < 12
              ? ECrowdDemoBusinessState::Attacking
              : ECrowdDemoBusinessState::Idle;
        Agent.AttackPhase = Agent.BusinessState == ECrowdDemoBusinessState::Attacking
          ? ECrowdDemoAttackPhase::Windup : ECrowdDemoAttackPhase::None;
        Agent.BusinessStateRevision = 1;
        Agent.BusinessStateEnterFixedStep = 0;
      }
      Agents.Add(Agent);
      FBinding& Binding = Bindings.AddDefaulted_GetRef();
      Binding.AgentId = Identities[It].Id;
      Binding.FormationIndex = Formations[It].FormationIndex;
      Binding.Location = States[It].Location;
    }
  });
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  Bindings.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });

  TArray<FCrowdDemoHitFact> Facts = bProjectileCombat
    ? Pipeline->ConsumePendingProjectileHitFacts()
    : TArray<FCrowdDemoHitFact>();
  const int32 Step = Pipeline->GetCurrentFixedStepIndex();
  if (bShowcase)
  {
    for (const FBinding& Binding : Bindings)
    {
      const bool bKnockback = Step == 30 && Binding.FormationIndex >= 12 && Binding.FormationIndex < 14;
      const bool bKnockUp = Step == 60 && Binding.FormationIndex >= 14 && Binding.FormationIndex < 16;
      const bool bDeath = Step == 90 && Binding.FormationIndex >= 16;
      if (!bKnockback && !bKnockUp && !bDeath) continue;
      const FCrowdDemoCombatAgentState* Target = Agents.FindByPredicate(
        [&](const auto& Agent) { return Agent.AgentId == Binding.AgentId; });
      if (!Target) continue;
      FCrowdDemoHitFact& Fact = Facts.AddDefaulted_GetRef();
      Fact.HitEventId = (static_cast<uint64>(Pipeline->GetCurrentRoundId()) << 32)
        | (static_cast<uint64>(Step) << 16) | static_cast<uint32>(Binding.AgentId);
      Fact.ApplyFixedStep = Step;
      Fact.TargetAgentId = Binding.AgentId;
      Fact.TargetLifecycleSerial = Target->LifecycleSerial;
      Fact.HitPosition = Binding.Location;
      Fact.HitDirection = FVector::ForwardVector;
      Fact.Damage = bDeath ? 1000.0f : 10.0f;
      Fact.HorizontalImpulseCmps = bKnockback ? 500.0f : 0.0f;
      Fact.VerticalImpulseCmps = bKnockUp ? 650.0f : 0.0f;
      Fact.HitFlashProfileKey = 1;
    }
  }
  FCrowdDemoHitResponseSettings Settings;
  Settings.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
  FCrowdDemoHitResponseSummary Summary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(
    Step, Pipeline->GetCurrentStepEndServerTimeSeconds(), Facts, Settings, Agents, Summary);
  if (bProjectileCombat)
    Pipeline->RecordProjectileHitResponse(Summary);

  TMap<int32, const FCrowdDemoCombatAgentState*> ById;
  for (const auto& Agent : Agents) ById.Add(Agent.AgentId, &Agent);
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Stats = ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
    const auto Businesses = ChunkContext.GetMutableFragmentView<FCrowdDemoBusinessStateFragment>();
    const auto Attacks = ChunkContext.GetMutableFragmentView<FCrowdDemoRangedAttackFragment>();
    const auto Reactives = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionFragment>();
    const auto HitFlashes = ChunkContext.GetMutableFragmentView<FCrowdDemoHitFlashFragment>();
    const auto Visuals = ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      if (const auto* const* Agent = ById.Find(Identities[It].Id))
        ApplyCombatAgentState(**Agent, Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
  });
}

UCrowdDemoRoundReactiveMotionIntentComposeProcessor::UCrowdDemoRoundReactiveMotionIntentComposeProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundReactiveMotionIntentComposeProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassStatsFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoBusinessStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRangedAttackFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionStepFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoHitFlashFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundReactiveMotionIntentComposeProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive()) return;
  FCrowdDemoHitResponseSettings Settings;
  Settings.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
  TArray<FCrowdGuidanceCandidate> PreparedBusinessCandidates;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Stats = ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
    const auto Businesses = ChunkContext.GetMutableFragmentView<FCrowdDemoBusinessStateFragment>();
    const auto Attacks = ChunkContext.GetMutableFragmentView<FCrowdDemoRangedAttackFragment>();
    const auto Reactives = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionFragment>();
    const auto Steps = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionStepFragment>();
    const auto HitFlashes = ChunkContext.GetMutableFragmentView<FCrowdDemoHitFlashFragment>();
    const auto Visuals = ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoGuidanceCandidate BusinessCandidate;
      if (Pipeline->IsRangedProjectileCombat())
      {
        BusinessCandidate = FCrowdDemoGuidanceComposeKernel::BuildCandidate(
          Identities[It].Id, ECrowdDemoGuidanceProvider::BusinessOverride,
          Pipeline->GetCurrentPlanRevision(), FVector::ZeroVector,
          States[It].Location, States[It].YawDegrees, true);
      }
      else if (Pipeline->GetRules().SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse)
      {
        const FVector Anchor = FVector(Pipeline->GetRules().SpawnOrigin)
          + Formations[It].LocalOffset;
        const FCrowdDemoVatShowcaseMotionResult Showcase =
          FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
            Formations[It].FormationIndex, Pipeline->GetCurrentFixedStepIndex(),
            States[It].Location, Anchor);
        if (Showcase.bValid)
        {
          BusinessCandidate = FCrowdDemoGuidanceComposeKernel::BuildCandidate(
            Identities[It].Id, ECrowdDemoGuidanceProvider::BusinessOverride,
            Pipeline->GetCurrentPlanRevision(), Showcase.DesiredVelocity,
            Showcase.DesiredLocation,
            Showcase.DesiredVelocity.IsNearlyZero()
              ? States[It].YawDegrees : Showcase.DesiredVelocity.Rotation().Yaw,
            true);
        }
      }
      auto Agent = MakeCombatAgentState(
        Identities[It], Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
      const auto StepResult = FCrowdDemoCombatStateKernel::AdvanceReactiveMotion(
        Pipeline->GetCurrentFixedStepIndex(), States[It].Location.Z, Settings, Agent);
      Steps[It] = FCrowdDemoReactiveMotionStepFragment();
      if (!Agent.bAlive)
      {
        BusinessCandidate = FCrowdDemoGuidanceComposeKernel::BuildCandidate(
          Identities[It].Id, ECrowdDemoGuidanceProvider::BusinessOverride,
          Pipeline->GetCurrentPlanRevision(), FVector::ZeroVector,
          States[It].Location, States[It].YawDegrees, true);
      }
      else if (StepResult.bValid && Agent.ReactiveMode != ECrowdDemoReactiveMotionMode::None)
      {
        const FVector ReactiveVelocity(
          StepResult.HorizontalVelocity.X, StepResult.HorizontalVelocity.Y, 0.0f);
        const FVector DesiredLocation = BusinessCandidate.bValid
          ? BusinessCandidate.DesiredLocation : States[It].Location;
        BusinessCandidate = FCrowdDemoGuidanceComposeKernel::BuildCandidate(
          Identities[It].Id, ECrowdDemoGuidanceProvider::BusinessOverride,
          Pipeline->GetCurrentPlanRevision(), ReactiveVelocity, DesiredLocation,
          ReactiveVelocity.IsNearlyZero()
            ? States[It].YawDegrees : ReactiveVelocity.Rotation().Yaw,
          true);
        Steps[It].bActive = true;
        Steps[It].ProposedZ = StepResult.NewZ;
        Steps[It].VerticalVelocityCmps = StepResult.NewVerticalVelocityCmps;
      }
      if (BusinessCandidate.bValid)
      {
        PreparedBusinessCandidates.Add(
          FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(
            BusinessCandidate));
      }
      ApplyCombatAgentState(
        Agent, Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
    }
  });
  PreparedBusinessCandidates.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  Pipeline->SetPreparedBusinessGuidanceCandidates(
    MoveTemp(PreparedBusinessCandidates));
}

UCrowdDemoRoundVisualStateResolveProcessor::UCrowdDemoRoundVisualStateResolveProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundVisualStateResolveProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassStatsFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoBusinessStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRangedAttackFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoHitFlashFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundVisualStateResolveProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive()) return;
  TArray<FCrowdDemoPreparedCombatRollbackFact> RollbackCombatStates;
  TArray<FCrowdDemoParticleAppliedRoundSimState> ParticleAppliedStates;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Stats = ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
    const auto Businesses = ChunkContext.GetMutableFragmentView<FCrowdDemoBusinessStateFragment>();
    const auto Attacks = ChunkContext.GetMutableFragmentView<FCrowdDemoRangedAttackFragment>();
    const auto Reactives = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionFragment>();
    const auto HitFlashes = ChunkContext.GetMutableFragmentView<FCrowdDemoHitFlashFragment>();
    const auto Visuals = ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      auto Agent = MakeCombatAgentState(
        Identities[It], Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
      const bool bUseShowcaseLocomotionState =
        Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
        && (Pipeline->GetRules().SoftPressureTestCase
            == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse
          || Pipeline->GetRules().SoftPressureTestCase
            == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat);
      FCrowdDemoCombatStateKernel::ResolveVisualStateBoundary(
        Pipeline->GetCurrentFixedStepIndex(), Pipeline->GetCurrentStepEndServerTimeSeconds(),
        States[It].Velocity, Agent, bUseShowcaseLocomotionState);
      ApplyCombatAgentState(
        Agent, Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
      switch (Agent.VisualState)
      {
        case ECrowdDemoVisualState::Move: Visuals[It].AnimState = ECrowdDemoAnimState::Move; break;
        case ECrowdDemoVisualState::Attack: Visuals[It].AnimState = ECrowdDemoAnimState::Attack; break;
        case ECrowdDemoVisualState::HitReact: Visuals[It].AnimState = ECrowdDemoAnimState::HitReact; break;
        case ECrowdDemoVisualState::Death: Visuals[It].AnimState = ECrowdDemoAnimState::Death; break;
        default: Visuals[It].AnimState = ECrowdDemoAnimState::Idle; break;
      }
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
      {
        FCrowdDemoPreparedCombatRollbackFact& Rollback =
          RollbackCombatStates.AddDefaulted_GetRef();
        Rollback.AgentId = Identities[It].Id;
        Rollback.Combat = MakeCombatNetState(
          Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
        FCrowdDemoParticleAppliedRoundSimState& Applied =
          ParticleAppliedStates.AddDefaulted_GetRef();
        Applied.AgentId = Identities[It].Id;
        Applied.LifecycleSerial = Identities[It].LifecycleSerial;
        Applied.Position = States[It].Location;
        Applied.Velocity = States[It].Velocity;
        Applied.YawDegrees = States[It].YawDegrees;
        Applied.RadiusCm = Formations[It].RadiusCm;
        Applied.bInitialized = States[It].bInitialized;
        Applied.Combat = Rollback.Combat;
      }
    }
  });
  if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    if (!Pipeline->CompleteSoftPressureRollbackCombatState(
        Pipeline->GetCurrentFixedStepIndex(), RollbackCombatStates))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoRollbackCombatFactsIncomplete step=%d facts=%d"),
        Pipeline->GetCurrentFixedStepIndex(), RollbackCombatStates.Num());
      return;
    }
    Pipeline->RecordParticleAppliedStateHash(
      FCrowdDemoParticleConstraintKernel::HashAppliedRoundSimState(
        Pipeline->GetCurrentRoundId(), Pipeline->GetCurrentPlanRevision(),
        Pipeline->GetCurrentFixedStepIndex(), Pipeline->GetCurrentStepEndServerTimeSeconds(),
        ParticleAppliedStates));
  }
}

UCrowdDemoRoundParticleConstraintProcessor::UCrowdDemoRoundParticleConstraintProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundParticleConstraintProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundProposedMovementFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdMassParticleConstraintFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
  EntityQuery.AddTagRequirement<FCrowdMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundParticleConstraintProcessor::Execute(
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
  TMap<int32, const FCrowdMassPredictedMovement*> PredictedByAgentId;
  for (const FCrowdMassPredictedMovement& Value
    : Pipeline->GetPreparedRuntimePredictedMovements())
    PredictedByAgentId.Add(Value.AgentId, &Value);
  TMap<int32, const FCrowdComposedGuidance*> ComposedByAgentId;
  for (const FCrowdComposedGuidance& Value
    : Pipeline->GetPreparedRuntimeComposedGuidance())
    ComposedByAgentId.Add(Value.AgentId, &Value);
  TMap<int32, const FCrowdDemoLocalPredictiveResult*> LocalByAgentId;
  for (const FCrowdDemoLocalPredictiveResult& Value
    : Pipeline->GetPreparedLocalPredictiveResults())
    LocalByAgentId.Add(Value.AgentId, &Value);
  bool bGatherValid = true;
  for (const FCrowdMassBoundaryAgentRecord& Base
    : Pipeline->GetBoundarySnapshot().Agents)
  {
    const FCrowdMassPredictedMovement* const* Predicted =
      PredictedByAgentId.Find(Base.Identity.AgentId);
    if (!Predicted || !(*Predicted)->bValid)
    {
      bGatherValid = false;
      continue;
    }
    if (!(*Predicted)->bParticleActive) continue;
    FCrowdParticleConstraintAgent Agent;
    if (!FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleAgent(
      Base.Identity, Base.Properties, (*Predicted)->StartPosition,
      (*Predicted)->PredictedPosition,
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
  constexpr int32 TargetParticleId = -1000000001;
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
  FCrowdMassParticleWorkInput ParticleWorkInput;
  ParticleWorkInput.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  ParticleWorkInput.PlanRevision = Pipeline->GetCurrentPlanRevision();
  ParticleWorkInput.Environment =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleEnvironment(Environment);
  ParticleWorkInput.Settings =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleSettings(Settings);
  ParticleWorkInput.Agents = CoreAgents;
  ParticleWorkInput.bCaptureTrace = bCaptureParticleTrace;
  const double StartSeconds = FPlatformTime::Seconds();
  TFuture<FCrowdMassParticleWorkOutput> Future = Async(
    EAsyncExecution::ThreadPool,
    [Input = MoveTemp(ParticleWorkInput)]() mutable
    {
      return FCrowdMassParticleWork::Solve(Input);
    });
  FCrowdMassParticleWorkOutput WorkOutput = Future.Get();
  const float SolverMilliseconds = static_cast<float>(
    (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
  if (!WorkOutput.bCompleted)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoParticleRuntimeWorkIncomplete step=%d agents=%d"),
      Pipeline->GetCurrentFixedStepIndex(), CoreAgents.Num());
    return;
  }
  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  Agents.Reserve(CoreAgents.Num());
  for (const auto& Agent : CoreAgents)
    Agents.Add(FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleAgent(Agent));
  TArray<FCrowdDemoParticleConstraintPair> Pairs;
  Pairs.Reserve(WorkOutput.Pairs.Num());
  for (const auto& Pair : WorkOutput.Pairs)
    Pairs.Add(FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticlePair(Pair));
  TArray<FCrowdDemoParticleConstraintResult> Results;
  Results.Reserve(WorkOutput.Results.Num());
  for (const auto& Result : WorkOutput.Results)
    Results.Add(FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleResult(Result));
  FCrowdDemoParticleConstraintSummary Summary =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleSummary(
      WorkOutput.Summary);
  FCrowdDemoParticleConstraintTrace Trace =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleTrace(WorkOutput.Trace);

  TMap<int32, const FCrowdDemoParticleConstraintResult*> ResultsByAgentId;
  for (const auto& Result : Results) ResultsByAgentId.Add(Result.AgentId, &Result);
  TMap<int32, const FCrowdParticleConstraintResult*> CoreResultsByAgentId;
  for (const auto& Result : WorkOutput.Results)
    CoreResultsByAgentId.Add(Result.AgentId, &Result);
  TMap<int32, int32> TraceIndexByAgentId;
  if (Settings.bCaptureRouteDiagnostic)
    for (int32 TraceIndex = 0; TraceIndex < Trace.AgentIds.Num(); ++TraceIndex)
      TraceIndexByAgentId.Add(Trace.AgentIds[TraceIndex], TraceIndex);
  bool bResultIdentityValid = true;
  int32 ActiveEntityCount = 0;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities =
      ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdDemoPreparedOpenSpawnBoundaryFact* OpenSpawnFact =
        Pipeline->IsOpenSpawnRelaxation()
        ? Pipeline->FindPreparedOpenSpawnBoundaryFact(Identities[It].Id)
        : nullptr;
      if (Pipeline->IsOpenSpawnRelaxation() && !OpenSpawnFact)
      {
        bResultIdentityValid = false;
        continue;
      }
      if (OpenSpawnFact && !OpenSpawnFact->bParticleActive)
        continue;
      ++ActiveEntityCount;
      if (!ResultsByAgentId.Contains(Identities[It].Id)
        || !CoreResultsByAgentId.Contains(Identities[It].Id))
        bResultIdentityValid = false;
    }
  });
  if (!bResultIdentityValid
    || ActiveEntityCount + (bHasTargetParticle ? 1 : 0) != Results.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoParticleRuntimeIdentityInvalid step=%d active=%d results=%d target=%d"),
      Pipeline->GetCurrentFixedStepIndex(), ActiveEntityCount, Results.Num(),
      bHasTargetParticle ? 1 : 0);
    return;
  }
  TArray<FCrowdParticleConstraintResult> PreparedRuntimeParticleResults;
  if (Summary.bValid)
    PreparedRuntimeParticleResults = WorkOutput.Results;
  TArray<FCrowdDemoParticleAppliedState> AppliedStates;
  TArray<FCrowdDemoSoftPressureRouteStepSample> RouteSamples;
  FCrowdDemoTargetStabilityStepSample StabilityStep;
  TMap<int32, const FCrowdDemoTargetRegionGuidanceResult*> StabilityGuidanceByAgentId;
  TMap<int32, const FCrowdDemoTargetRegionAgentDemandState*> StabilityDemandByAgentId;
  TMap<int32, int32> StabilitySurplusByAgentId;
  if (bStabilityDiagnostic)
  {
    StabilityStep.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
    StabilityStep.TargetRevision = Pipeline->GetTargetFact().TargetRevision;
    StabilityStep.FixedStepSeconds = Settings.FixedStepSeconds;
    uint32 GraphHash = 2166136261u;
    auto FoldGraphHash = [&GraphHash](const uint32 Value)
    {
      GraphHash ^= Value;
      GraphHash *= 16777619u;
    };
    auto AddRuntime = [&](const uint32 CohortKey,
      const FCrowdDemoTargetPolarTopology& Topology,
      const FCrowdDemoTargetRegionDemandResult& Demand,
      const FCrowdDemoTargetRegionFlowPlan& Plan,
      const TConstArrayView<FCrowdDemoTargetRegionGuidanceResult> Guidance,
      const FCrowdDemoTargetRegionGuidanceSummary& GuidanceSummary)
    {
      FoldGraphHash(Topology.FeasibleGraphHash);
      StabilityStep.InsideBandCount += Demand.CurrentTerminalPopulation;
      StabilityStep.RequiredCoverageCount += FMath::Min(
        Demand.AgentStates.Num(), Demand.FeasibleRegionCount);
      TSet<int32> CoveredRegions;
      TMap<int32, int32> SurplusByRegion;
      TMap<int32, FCrowdDemoTargetStabilityRegionSample*> RegionSamples;
      const int32 FirstRegionSample = StabilityStep.Regions.Num();
      for (const auto& Region : Demand.Regions)
      {
        SurplusByRegion.Add(Region.StableRegionKey, Region.Surplus);
        FCrowdDemoTargetStabilityRegionSample& Sample =
          StabilityStep.Regions.AddDefaulted_GetRef();
        Sample.CohortKey = CohortKey;
        Sample.RegionKey = Region.StableRegionKey;
        Sample.AvailableCapacity = Region.AvailableCapacity;
        Sample.CurrentPopulation = Region.CurrentPopulation;
        Sample.DesiredPopulation = Region.DesiredPopulation;
        Sample.Deficit = Region.Deficit;
        Sample.Surplus = Region.Surplus;
        Sample.bFeasible = Region.bFeasible;
      }
      for (int32 Index = FirstRegionSample; Index < StabilityStep.Regions.Num(); ++Index)
        RegionSamples.Add(StabilityStep.Regions[Index].RegionKey,
          &StabilityStep.Regions[Index]);
      for (const auto& State : Demand.AgentStates)
      {
        StabilityDemandByAgentId.Add(State.AgentId, &State);
        StabilitySurplusByAgentId.Add(State.AgentId,
          SurplusByRegion.FindRef(State.CurrentRegionKey));
        if (State.bTerminal && State.CurrentRegionKey != INDEX_NONE)
        {
          CoveredRegions.Add(State.CurrentRegionKey);
          if (FCrowdDemoTargetStabilityRegionSample* const* Sample =
            RegionSamples.Find(State.CurrentRegionKey))
            (*Sample)->TerminalAgentIds.Add(State.AgentId);
        }
        if (State.bSupply && State.CurrentRegionKey != INDEX_NONE)
          if (FCrowdDemoTargetStabilityRegionSample* const* Sample =
            RegionSamples.Find(State.CurrentRegionKey))
            (*Sample)->SupplyAgentIds.Add(State.AgentId);
      }
      StabilityStep.CoverageCount += CoveredRegions.Num();
      for (const auto& Item : Guidance)
      {
        StabilityGuidanceByAgentId.Add(Item.AgentId, &Item);
        if (Item.Mode == ECrowdDemoTargetRegionGuidanceMode::TerminalSettle
          && Item.DemandRegionKey != INDEX_NONE)
          if (FCrowdDemoTargetStabilityRegionSample* const* Sample =
            RegionSamples.Find(Item.DemandRegionKey))
            (*Sample)->TerminalSettleAgentIds.Add(Item.AgentId);
        if (Item.Mode == ECrowdDemoTargetRegionGuidanceMode::Transport
          && Topology.Cells.IsValidIndex(Item.NextCellKey))
        {
          const auto& Cell = Topology.Cells[Item.NextCellKey];
          if (Cell.bTerminal)
            if (FCrowdDemoTargetStabilityRegionSample* const* Sample =
              RegionSamples.Find(Cell.PrimaryDemandRegionKey))
              ++(*Sample)->GuidanceTargetCount;
        }
      }
      TMap<int64, int32> ConsumedByEdge;
      for (const auto& Consumption : GuidanceSummary.Consumption)
      {
        const int64 EdgeKey = (static_cast<int64>(Consumption.FromCellKey) << 32)
          | static_cast<uint32>(Consumption.ToCellKey);
        ConsumedByEdge.Add(EdgeKey, Consumption.ConsumedQuota);
      }
      for (const auto& Flow : Plan.EdgeFlows)
      {
        if (!Topology.Cells.IsValidIndex(Flow.FromCellKey)
          || !Topology.Cells.IsValidIndex(Flow.ToCellKey)) continue;
        const auto& FromCell = Topology.Cells[Flow.FromCellKey];
        const auto& Cell = Topology.Cells[Flow.ToCellKey];
        const int64 EdgeKey = (static_cast<int64>(Flow.FromCellKey) << 32)
          | static_cast<uint32>(Flow.ToCellKey);
        FCrowdDemoTargetStabilityEdgeSample& Edge =
          StabilityStep.Edges.AddDefaulted_GetRef();
        Edge.CohortKey = CohortKey;
        Edge.FromCellKey = Flow.FromCellKey;
        Edge.ToCellKey = Flow.ToCellKey;
        Edge.FromRegionKey = FromCell.PrimaryDemandRegionKey;
        Edge.ToRegionKey = Cell.PrimaryDemandRegionKey;
        Edge.AgentQuota = Flow.AgentQuota;
        Edge.ConsumedQuota = ConsumedByEdge.FindRef(EdgeKey);
        Edge.bToTerminal = Cell.bTerminal;
        if (Cell.bTerminal)
          if (FCrowdDemoTargetStabilityRegionSample* const* Sample =
            RegionSamples.Find(Cell.PrimaryDemandRegionKey))
            (*Sample)->PrimaryIncomingPlanQuota += Flow.AgentQuota;
      }
      for (const auto& Consumption : GuidanceSummary.Consumption)
      {
        if (!Topology.Cells.IsValidIndex(Consumption.ToCellKey)) continue;
        const auto& Cell = Topology.Cells[Consumption.ToCellKey];
        if (!Cell.bTerminal) continue;
        if (FCrowdDemoTargetStabilityRegionSample* const* Sample =
          RegionSamples.Find(Cell.PrimaryDemandRegionKey))
          (*Sample)->PrimaryIncomingConsumedQuota += Consumption.ConsumedQuota;
      }
    };
    if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
    {
      TArray<const FCrowdDemoTargetRegionCapabilityCohortRuntime*> Cohorts;
      for (const auto& Runtime : Pipeline->GetCapabilityCohorts()) Cohorts.Add(&Runtime);
      Cohorts.Sort([](const auto& A, const auto& B)
      {
        return A.Cohort.CapabilityProfileKey < B.Cohort.CapabilityProfileKey;
      });
      for (const auto* Runtime : Cohorts)
        AddRuntime(Runtime->Cohort.CapabilityProfileKey, Runtime->Topology,
          Runtime->Demand, Runtime->Plan, Runtime->Guidance,
          Runtime->GuidanceSummary);
    }
    else
    {
      AddRuntime(0, Pipeline->GetPreparedTargetRegionTopology(),
        Pipeline->GetPreparedTargetRegionDemand(),
        Pipeline->GetPreparedTargetRegionPlan(),
        Pipeline->GetPreparedTargetRegionGuidance(),
        Pipeline->GetTargetRegionGuidanceSummary());
    }
    StabilityStep.FeasibleGraphHash = GraphHash;
  }
  TMap<int32, FVector> FlowDirectionByAgentId;
  AppliedStates.Reserve(Agents.Num());
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Proposed = ChunkContext.GetFragmentView<FCrowdDemoRoundProposedMovementFragment>();
    const auto FlowSamples = ChunkContext.GetFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    const auto RuntimeOutputs =
      ChunkContext.GetMutableFragmentView<FCrowdMassParticleConstraintFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdMassParticleConstraintFragment& RuntimeOutput = RuntimeOutputs[It];
      const FCrowdDemoPreparedOpenSpawnBoundaryFact* OpenSpawnFact =
        Pipeline->IsOpenSpawnRelaxation()
        ? Pipeline->FindPreparedOpenSpawnBoundaryFact(Identities[It].Id)
        : nullptr;
      if (Pipeline->IsOpenSpawnRelaxation() && !OpenSpawnFact)
      {
        bResultIdentityValid = false;
        continue;
      }
      if (OpenSpawnFact && !OpenSpawnFact->bParticleActive)
      {
        RuntimeOutput.Value.AgentId = Identities[It].Id;
        RuntimeOutput.Value.CorrectedPosition = Proposed[It].ProposedLocation;
        RuntimeOutput.Value.CorrectedVelocity = FVector::ZeroVector;
        RuntimeOutput.Value.RealizedCorrection = FVector::ZeroVector;
        RuntimeOutput.Value.FirstInfluencedIteration = INDEX_NONE;
        RuntimeOutput.Value.CorrectedPairCount = 0;
        RuntimeOutput.PlanRevision = Pipeline->GetCurrentPlanRevision();
        PreparedRuntimeParticleResults.Add(RuntimeOutput.Value);
        continue;
      }
      const FCrowdDemoParticleConstraintResult* const* Found = ResultsByAgentId.Find(Identities[It].Id);
      const FCrowdParticleConstraintResult* const* CoreFound =
        CoreResultsByAgentId.Find(Identities[It].Id);
      if (!Summary.bValid || !Found || !CoreFound)
      {
        RuntimeOutput.Value.AgentId = Identities[It].Id;
        RuntimeOutput.Value.CorrectedPosition = Proposed[It].StartLocation;
        RuntimeOutput.Value.CorrectedVelocity = FVector::ZeroVector;
        RuntimeOutput.Value.RealizedCorrection =
          Proposed[It].StartLocation - Proposed[It].ProposedLocation;
        RuntimeOutput.Value.FirstInfluencedIteration = INDEX_NONE;
        RuntimeOutput.Value.CorrectedPairCount = 0;
        RuntimeOutput.PlanRevision = Pipeline->GetCurrentPlanRevision();
        PreparedRuntimeParticleResults.Add(RuntimeOutput.Value);
        FCrowdDemoParticleAppliedState& Applied = AppliedStates.AddDefaulted_GetRef();
        Applied.AgentId = Identities[It].Id;
        Applied.Position = RuntimeOutput.Value.CorrectedPosition;
        Applied.Velocity = RuntimeOutput.Value.CorrectedVelocity;
        continue;
      }
      RuntimeOutput.Value = **CoreFound;
      RuntimeOutput.PlanRevision = Pipeline->GetCurrentPlanRevision();
      FCrowdDemoParticleAppliedState& Applied = AppliedStates.AddDefaulted_GetRef();
      Applied.AgentId = Identities[It].Id;
      Applied.Position = RuntimeOutput.Value.CorrectedPosition;
      Applied.Velocity = RuntimeOutput.Value.CorrectedVelocity;
      if (bRouteDiagnostic)
      {
        FCrowdDemoSoftPressureRouteStepSample& Route = RouteSamples.AddDefaulted_GetRef();
        Route.AgentId = Identities[It].Id;
        Route.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
        Route.PredictStartLocation = Proposed[It].StartLocation;
        Route.Location = RuntimeOutput.Value.CorrectedPosition;
        Route.Goal = FVector(Pipeline->GetRules().FlowFieldConfig.GoalLocation);
        Route.FlowCellIndex = FlowSamples[It].CellIndex;
        Route.FlowStableCellKey = FlowSamples[It].StableCellKey;
        Route.FlowStatus = FlowSamples[It].Status;
        Route.IntegrationCost = FlowSamples[It].IntegrationCost;
        Route.FlowDirection = FlowSamples[It].FlowDirection;
        if (const FCrowdComposedGuidance* const* Composed =
          ComposedByAgentId.Find(Route.AgentId))
          Route.DesiredVelocity = (*Composed)->AutonomousPreferredVelocity;
        Route.PredictedVelocity = Proposed[It].ProposedVelocity;
        Route.AppliedVelocity = RuntimeOutput.Value.CorrectedVelocity;
        Route.TotalParticleCorrection = RuntimeOutput.Value.RealizedCorrection;
        Route.FixedStepSeconds = Settings.FixedStepSeconds;
        Route.MaxSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
        // T2 samples are captured after TargetRegionGuidance and local velocity
        // composition have overwritten the move intent. They cannot validate the
        // earlier FlowPreferred stage and therefore must not claim Flow ownership.
        Route.bFlowGuidanceOwner = !Pipeline->IsOpenCohortMovement();
        if (const int32* TraceIndex = TraceIndexByAgentId.Find(Route.AgentId))
        {
          if (Trace.PairSoftRequestedCorrections.IsValidIndex(*TraceIndex))
            Route.PairSoftRequestedCorrection = Trace.PairSoftRequestedCorrections[*TraceIndex];
          if (Trace.PairSoftRealizedCorrections.IsValidIndex(*TraceIndex))
            Route.PairSoftRealizedCorrection = Trace.PairSoftRealizedCorrections[*TraceIndex];
          if (Trace.EnvironmentSoftRequestedCorrections.IsValidIndex(*TraceIndex))
            Route.EnvironmentSoftRequestedCorrection =
              Trace.EnvironmentSoftRequestedCorrections[*TraceIndex];
          if (Trace.EnvironmentSoftRealizedCorrections.IsValidIndex(*TraceIndex))
            Route.EnvironmentSoftRealizedCorrection =
              Trace.EnvironmentSoftRealizedCorrections[*TraceIndex];
          if (Trace.UnifiedHardCorrections.IsValidIndex(*TraceIndex))
            Route.UnifiedHardCorrection = Trace.UnifiedHardCorrections[*TraceIndex];
          if (Trace.ActiveNeighborAgentIds.IsValidIndex(*TraceIndex))
            Route.ActiveNeighborAgentIds = Trace.ActiveNeighborAgentIds[*TraceIndex];
        }
        FlowDirectionByAgentId.Add(Route.AgentId, Route.FlowDirection);
      }
      if (bStabilityDiagnostic)
      {
        FCrowdDemoTargetStabilityAgentSample& Sample =
          StabilityStep.Agents.AddDefaulted_GetRef();
        Sample.AgentId = Identities[It].Id;
        Sample.CohortKey = CapabilityProfileKeyByAgentId.FindRef(Sample.AgentId);
        Sample.Location = FVector2f(RuntimeOutput.Value.CorrectedPosition.X,
          RuntimeOutput.Value.CorrectedPosition.Y);
        Sample.Velocity = FVector2f(RuntimeOutput.Value.CorrectedVelocity.X,
          RuntimeOutput.Value.CorrectedVelocity.Y);
        Sample.AppliedVelocity = Sample.Velocity;
        Sample.TargetLocation = Pipeline->GetTargetFact().Location;
        Sample.TargetVelocity = Pipeline->GetTargetFact().Velocity;
        Sample.TotalParticleCorrection = FVector2f(
          RuntimeOutput.Value.RealizedCorrection.X,
          RuntimeOutput.Value.RealizedCorrection.Y);
        Sample.PredictedVelocity = FVector2f(
          Proposed[It].ProposedVelocity.X, Proposed[It].ProposedVelocity.Y);
        if (const FCrowdDemoLocalPredictiveResult* const* Local =
          LocalByAgentId.Find(Sample.AgentId))
        {
          Sample.LocalVelocity = (*Local)->Velocity;
          Sample.LocalNeighborCount = (*Local)->NeighborCount;
          Sample.LocalConstraintCount = (*Local)->ConstraintCount;
          Sample.LocalBlockedAgeSteps = (*Local)->NextBlockedAgeSteps;
          Sample.bLocalValid = (*Local)->bValid;
          Sample.bLocalGranted = (*Local)->bGranted;
          Sample.bLocalYielding = (*Local)->bYielding;
        }
        if (const auto* const* Guidance = StabilityGuidanceByAgentId.Find(Sample.AgentId))
        {
          Sample.CurrentCellKey = (*Guidance)->CurrentCellKey;
          Sample.NextCellKey = (*Guidance)->NextCellKey;
          Sample.CurrentRegionKey = (*Guidance)->DemandRegionKey;
          Sample.GuidanceMode = (*Guidance)->Mode;
          Sample.DesiredVelocity = (*Guidance)->DesiredVelocity;
        }
        else
        {
          if (const FCrowdComposedGuidance* const* Composed =
            ComposedByAgentId.Find(Sample.AgentId))
            Sample.DesiredVelocity = FVector2f(
              (*Composed)->AutonomousPreferredVelocity.X,
              (*Composed)->AutonomousPreferredVelocity.Y);
        }
        if (const auto* const* Demand = StabilityDemandByAgentId.Find(Sample.AgentId))
        {
          Sample.CurrentRegionKey = (*Demand)->CurrentRegionKey;
          Sample.bTerminal = (*Demand)->bTerminal;
          Sample.bTerminalStay = (*Demand)->bTerminalStay;
          Sample.bSupply = (*Demand)->bSupply;
          Sample.RegionSurplusCount = StabilitySurplusByAgentId.FindRef(Sample.AgentId);
        }
        if (const int32* TraceIndex = TraceIndexByAgentId.Find(Sample.AgentId))
          if (Trace.PairSoftRealizedCorrections.IsValidIndex(*TraceIndex))
            Sample.PairSoftCorrection = FVector2f(
              Trace.PairSoftRealizedCorrections[*TraceIndex].X,
              Trace.PairSoftRealizedCorrections[*TraceIndex].Y);
      }
    }
  });
  PreparedRuntimeParticleResults.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  TSet<int32> BoundaryAgentIds;
  for (const FCrowdMassBoundaryAgentRecord& Agent
    : Pipeline->GetBoundarySnapshot().Agents)
    BoundaryAgentIds.Add(Agent.Identity.AgentId);
  TArray<FCrowdMassFinalKinematicState> FinalKinematics;
  FinalKinematics.Reserve(BoundaryAgentIds.Num());
  for (const FCrowdParticleConstraintResult& Result
    : PreparedRuntimeParticleResults)
  {
    if (!BoundaryAgentIds.Contains(Result.AgentId)) continue;
    FCrowdMassFinalKinematicState& Kinematic =
      FinalKinematics.AddDefaulted_GetRef();
    Kinematic.AgentId = Result.AgentId;
    Kinematic.Position = Result.CorrectedPosition;
    Kinematic.Velocity = Result.CorrectedVelocity;
    Kinematic.bValid = true;
  }
  if (FinalKinematics.Num() != BoundaryAgentIds.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoParticleFinalKinematicSetInvalid step=%d prepared=%d expected=%d"),
      Pipeline->GetCurrentFixedStepIndex(), FinalKinematics.Num(),
      BoundaryAgentIds.Num());
    return;
  }
  Pipeline->SetPreparedRuntimeFinalKinematics(MoveTemp(FinalKinematics));
  Pipeline->SetPreparedRuntimeParticleResults(
    MoveTemp(PreparedRuntimeParticleResults));
  if (bHasTargetParticle)
  {
    FCrowdDemoParticleAppliedState& Applied = AppliedStates.AddDefaulted_GetRef();
    Applied.AgentId = TargetParticleId;
    if (Summary.bValid)
    {
      if (const FCrowdDemoParticleConstraintResult* const* TargetResult =
        ResultsByAgentId.Find(TargetParticleId))
      {
        Applied.Position = (*TargetResult)->CorrectedPosition;
        Applied.Velocity = (*TargetResult)->CorrectedVelocity;
      }
      else
      {
        Applied.Position = Agents.Last().StartPosition;
        Applied.Velocity = FVector::ZeroVector;
      }
    }
    else
    {
      Applied.Position = Agents.Last().StartPosition;
      Applied.Velocity = FVector::ZeroVector;
    }
  }
  FCrowdDemoParticleConstraintSummary AppliedSummary;
  const uint32 AppliedStateHash = WorkOutput.AppliedStateHash;
  AppliedSummary =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleSummary(
      WorkOutput.AppliedSummary);
  if (bStabilityDiagnostic)
  {
    StabilityStep.ParticleSoftErrorCmP95 = AppliedSummary.SoftErrorCmP95;
    StabilityStep.ParticleMaxActualCorrectionCm = AppliedSummary.MaxAgentCorrectionCm;
    Pipeline->RecordTargetStabilityStep(StabilityStep);
    if (Pipeline->ShouldBuildRoundResult())
      Pipeline->FinalizeTargetStabilityDiagnostic();
  }
  if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
  {
    TMap<int32, const FCrowdDemoParticleAppliedState*> AppliedByAgentId;
    for (const FCrowdDemoParticleAppliedState& Applied : AppliedStates)
      AppliedByAgentId.Add(Applied.AgentId, &Applied);
    int32 CrossProfileHardViolations = 0;
    int32 CrossProfileSweptViolations = 0;
    for (int32 AIndex = 0; AIndex < Agents.Num(); ++AIndex)
    {
      const FCrowdDemoParticleConstraintAgent& A = Agents[AIndex];
      const uint32* AKey = CapabilityProfileKeyByAgentId.Find(A.AgentId);
      const FCrowdDemoParticleAppliedState* const* AApplied =
        AppliedByAgentId.Find(A.AgentId);
      if (!AKey || !AApplied) continue;
      for (int32 BIndex = AIndex + 1; BIndex < Agents.Num(); ++BIndex)
      {
        const FCrowdDemoParticleConstraintAgent& B = Agents[BIndex];
        const uint32* BKey = CapabilityProfileKeyByAgentId.Find(B.AgentId);
        const FCrowdDemoParticleAppliedState* const* BApplied =
          AppliedByAgentId.Find(B.AgentId);
        if (!BKey || !BApplied || *AKey == *BKey) continue;
        const float HardDistance = A.PhysicalRadiusCm + B.PhysicalRadiusCm
          + FMath::Max(A.HardSafetyGapCm, B.HardSafetyGapCm);
        const FVector EndRelative = (*AApplied)->Position - (*BApplied)->Position;
        if (EndRelative.Size() + 0.01f < HardDistance)
          ++CrossProfileHardViolations;
        const FVector StartRelative = A.StartPosition - B.StartPosition;
        const FVector RelativeDelta = EndRelative - StartRelative;
        const float DeltaSizeSquared = RelativeDelta.SizeSquared();
        const float Time = DeltaSizeSquared > UE_SMALL_NUMBER
          ? FMath::Clamp(-FVector::DotProduct(StartRelative, RelativeDelta)
            / DeltaSizeSquared, 0.0f, 1.0f)
          : 0.0f;
        const float SweptDistance = (StartRelative + RelativeDelta * Time).Size();
        if (SweptDistance + 0.01f < HardDistance)
          ++CrossProfileSweptViolations;
      }
    }
    Pipeline->RecordCrossProfileParticleViolations(
      CrossProfileHardViolations, CrossProfileSweptViolations);
  }
  if (bRouteDiagnostic)
  {
    Pipeline->RecordSoftPressureRouteStep(RouteSamples);
    if (Pipeline->ShouldBuildRoundResult())
    {
      if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
      {
        TArray<int32> TerminalSupplyAgentIds;
        for (const FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime
          : Pipeline->GetCapabilityCohorts())
          for (const FCrowdDemoTargetRegionAgentDemandState& State : Runtime.Demand.AgentStates)
            if (!State.bTerminal || State.bSupply)
              TerminalSupplyAgentIds.AddUnique(State.AgentId);
        TerminalSupplyAgentIds.Sort();
        for (const int32 AgentId : TerminalSupplyAgentIds)
        {
          const int32* TraceIndex = TraceIndexByAgentId.Find(AgentId);
          if (!TraceIndex) continue;
          FString Neighbors;
          if (Trace.ActiveNeighborAgentIds.IsValidIndex(*TraceIndex))
            for (const int32 NeighborId : Trace.ActiveNeighborAgentIds[*TraceIndex])
            {
              if (!Neighbors.IsEmpty()) Neighbors += TEXT(",");
              Neighbors += FString::FromInt(NeighborId);
            }
          FString Influences;
          for (const FCrowdDemoParticleSoftPairInfluence& Influence : Trace.SoftPairInfluences)
          {
            if (Influence.MinAgentId != AgentId && Influence.MaxAgentId != AgentId)
              continue;
            if (!Influences.IsEmpty()) Influences += TEXT(";");
            const int32 OtherAgentId = Influence.MinAgentId == AgentId
              ? Influence.MaxAgentId : Influence.MinAgentId;
            const FVector Realized = Influence.MinAgentId == AgentId
              ? Influence.RealizedCorrectionA : Influence.RealizedCorrectionB;
            Influences += FString::Printf(TEXT("%d:%.2f:%.2f"),
              OtherAgentId, Realized.X, Realized.Y);
          }
          const FVector PairSoft = Trace.PairSoftRealizedCorrections.IsValidIndex(*TraceIndex)
            ? Trace.PairSoftRealizedCorrections[*TraceIndex] : FVector::ZeroVector;
          const FVector UnifiedHard = Trace.UnifiedHardCorrections.IsValidIndex(*TraceIndex)
            ? Trace.UnifiedHardCorrections[*TraceIndex] : FVector::ZeroVector;
          UE_LOG(LogTemp, Display,
            TEXT("CrowdDemoT6ParticleSupplyWitness agent_id=%d pair_soft=(%.2f,%.2f) unified_hard=(%.2f,%.2f) active_neighbors=[%s] soft_influences=[%s] influence_fields=other_agent,realized_x,realized_y source=MassPipeline"),
            AgentId, PairSoft.X, PairSoft.Y, UnifiedHard.X, UnifiedHard.Y,
            *Neighbors, *Influences);
        }
      }
      FCrowdDemoSoftPressureRouteCounterfactual Counterfactual;
      TSet<int32> EverReached;
      for (const auto& Agent : Pipeline->GetSoftPressureRouteDiagnosticRuntime().Agents)
        if (Agent.bEverReachedGoal) EverReached.Add(Agent.AgentId);
      auto SumNeverReachedForward = [&](const TConstArrayView<FCrowdDemoParticleConstraintResult> Values)
      {
        float Sum = 0.0f;
        for (const auto& Value : Values)
          if (!EverReached.Contains(Value.AgentId))
          {
            const FVector Direction = FlowDirectionByAgentId.FindRef(Value.AgentId).GetSafeNormal2D();
            Sum += FVector::DotProduct(Value.CorrectedVelocity, Direction);
          }
        return Sum;
      };
      Counterfactual.BaselineNeverReachedForwardCmps = SumNeverReachedForward(Results);

      TArray<FCrowdParticleConstraintAgent> StickyAgents = CoreAgents;
      for (auto& Agent : StickyAgents)
        if (EverReached.Contains(Agent.AgentId)) Agent.PredictedPosition = Agent.StartPosition;
      TArray<FCrowdDemoParticleConstraintResult> StickyResults;
      FCrowdDemoParticleConstraintSummary StickySummary;
      FCrowdDemoParticleConstraintSettings CounterfactualSettings = Settings;
      CounterfactualSettings.bCaptureRouteDiagnostic = false;
      FCrowdMassParticleWorkInput CounterfactualInput;
      CounterfactualInput.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
      CounterfactualInput.PlanRevision = Pipeline->GetCurrentPlanRevision();
      CounterfactualInput.Environment =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleEnvironment(Environment);
      CounterfactualInput.Settings =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleSettings(
          CounterfactualSettings);
      CounterfactualInput.Agents = StickyAgents;
      FCrowdMassParticleWorkOutput CounterfactualOutput =
        FCrowdMassParticleWork::Solve(CounterfactualInput);
      if (CounterfactualOutput.bCompleted)
      {
        StickySummary =
          FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleSummary(
            CounterfactualOutput.Summary);
        for (const auto& Result : CounterfactualOutput.Results)
          StickyResults.Add(
            FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleResult(Result));
      }
      Counterfactual.bStickyValid = StickySummary.bValid;
      Counterfactual.StickyNeverReachedForwardCmps = SumNeverReachedForward(StickyResults);

      TArray<FCrowdDemoParticleConstraintResult> SoftDisabledResults;
      FCrowdDemoParticleConstraintSummary SoftDisabledSummary;
      CounterfactualSettings.SoftResponsePerSecond = 0.0f;
      CounterfactualInput.Settings =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleSettings(
          CounterfactualSettings);
      CounterfactualInput.Agents = CoreAgents;
      CounterfactualOutput = FCrowdMassParticleWork::Solve(CounterfactualInput);
      if (CounterfactualOutput.bCompleted)
      {
        SoftDisabledSummary =
          FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleSummary(
            CounterfactualOutput.Summary);
        for (const auto& Result : CounterfactualOutput.Results)
          SoftDisabledResults.Add(
            FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleResult(Result));
      }
      Counterfactual.bSoftDisabledValid = SoftDisabledSummary.bValid;
      Counterfactual.SoftDisabledNeverReachedForwardCmps =
        SumNeverReachedForward(SoftDisabledResults);
      Pipeline->FinalizeSoftPressureRouteDiagnostic(Counterfactual);
    }
  }
  if (Summary.bValid)
  {
    AppliedSummary.PressureInfluencedAgentCount = Summary.PressureInfluencedAgentCount;
    AppliedSummary.FirstInfluencedIterationMax = Summary.FirstInfluencedIterationMax;
  }
  if (Pipeline->IsOpenSpawnRelaxation())
  {
    auto& Runtime = Pipeline->GetOpenSpawnRelaxationRuntime();
    Summary.CandidateHash = FoldTargetHash(Summary.CandidateHash, Runtime.ParticipationHash);
    Summary.CandidateHash = FoldTargetHash(Summary.CandidateHash, Runtime.PhaseHash);
    Pipeline->RecordOpenSpawnRelaxationParticleStep(
      Trace.SoftPairInfluences, AppliedSummary.MaxAgentCorrectionCm,
      AppliedSummary.SoftErrorCmP95);
  }
  else if (Pipeline->IsOpenCohortMovement())
  {
    Summary.CandidateHash = FoldTargetHash(
      Summary.CandidateHash,
      Pipeline->GetOpenCohortMovementLayout().LayoutHash);
  }
  else if (Pipeline->IsBidirectionalSwap())
  {
    Summary.CandidateHash = FoldTargetHash(
      Summary.CandidateHash,
      Pipeline->GetBidirectionalSwapLayout().LayoutHash);
    for (int32 CohortId = 0; CohortId < 2; ++CohortId)
      if (const FCrowdDemoSharedFlowField* Field =
        Pipeline->FindBidirectionalSwapFlowField(CohortId * 10))
        Summary.CandidateHash = FoldTargetHash(Summary.CandidateHash, Field->BuildHash);
  }
  else if (Pipeline->IsCorridorTransitProgressScenario())
  {
    Summary.CandidateHash = FoldTargetHash(
      Summary.CandidateHash,
      Pipeline->GetValidCorridorTransitLayout().LayoutHash);
    Summary.CandidateHash = FoldTargetHash(
      Summary.CandidateHash, Pipeline->GetSharedFlowField().BuildHash);
  }
  if (!Summary.bValid || !AppliedSummary.bValid)
  {
    // The full per-iteration trace is diagnostic data, not part of the normal
    // simulation contract. Re-run the pure deterministic kernel only on the
    // failure path so a valid fixed step does not allocate and populate every
    // contact/constraint/dual witness in production PIE.
    if (!bParticleTraceCaptured)
    {
      FCrowdDemoParticleConstraintSettings DiagnosticSettings = Settings;
      DiagnosticSettings.bCaptureRouteDiagnostic = true;
      FCrowdMassParticleWorkInput DiagnosticInput;
      DiagnosticInput.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
      DiagnosticInput.PlanRevision = Pipeline->GetCurrentPlanRevision();
      DiagnosticInput.Environment =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleEnvironment(Environment);
      DiagnosticInput.Settings =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreParticleSettings(
          DiagnosticSettings);
      DiagnosticInput.Agents = CoreAgents;
      DiagnosticInput.bCaptureTrace = true;
      const FCrowdMassParticleWorkOutput DiagnosticOutput =
        FCrowdMassParticleWork::Solve(DiagnosticInput);
      FCrowdDemoParticleConstraintSummary DiagnosticSummary;
      if (DiagnosticOutput.bCompleted)
      {
        DiagnosticSummary =
          FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleSummary(
            DiagnosticOutput.Summary);
        Trace = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoParticleTrace(
          DiagnosticOutput.Trace);
      }
      bParticleTraceCaptured = true;
      if (!DiagnosticOutput.bCompleted
        || DiagnosticSummary.CandidateHash != Summary.CandidateHash
        || DiagnosticSummary.bValid != Summary.bValid)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoParticleFailureTraceReplayMismatch step=%d candidate_hash=%u/%u valid=%d/%d"),
          Pipeline->GetCurrentFixedStepIndex(), Summary.CandidateHash,
          DiagnosticSummary.CandidateHash, Summary.bValid ? 1 : 0,
          DiagnosticSummary.bValid ? 1 : 0);
      }
    }
    FCrowdDemoParticleFailureFixture Fixture;
    FCrowdDemoParticleConstraintKernel::BuildFailureFixture(
      Agents, AppliedStates, Trace, Pipeline->GetCurrentFixedStepIndex(),
      Summary.CandidateHash, AppliedStateHash, Fixture);
    Pipeline->RecordParticleFailureFixture(Fixture);
  }
  Pipeline->RecordParticleConstraintSummary(
    Summary, AppliedSummary, AppliedStateHash, !Summary.bValid, SolverMilliseconds);
  Pipeline->LogStageOnce(TEXT("05_particle_constraint"), Agents.Num());
}

UCrowdDemoRoundObstacleConstraintProcessor::UCrowdDemoRoundObstacleConstraintProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundObstacleConstraintProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundProposedMovementFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundObstacleConstraintProcessor::Execute(
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
  const FCrowdDemoSharedFlowFieldConfig& Config = Pipeline->GetRules().FlowFieldConfig;
  const float FixedStep = Pipeline->GetCurrentFixedStepSeconds();
  int32 AgentCount = 0;
  float MaxNavigationDomainReprojectDeltaCm = 0.0f;
  TArray<FCrowdMassFinalKinematicState> FinalKinematics;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoRoundProposedMovementFragment> Proposed = ChunkContext.GetFragmentView<FCrowdDemoRoundProposedMovementFragment>();
    const TArrayView<FCrowdDemoRoundObstacleConstraintFragment> Constraints = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdDemoSharedFlowConstraintResult Result = FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
        Config,
        Proposed[It].StartLocation,
        Proposed[It].ProposedLocation,
        FixedStep,
        false);
      Constraints[It].ConstrainedLocation = Result.Location;
      Constraints[It].ConstrainedVelocity = Result.Velocity;
      Constraints[It].bHitObstacle = Result.bHitObstacle;
      Constraints[It].bPenetrating = Result.bPenetrating;
      Constraints[It].bHitFlowBounds = Result.bHitFlowBounds;
      Constraints[It].FlowBoundsReprojectDeltaCm = Result.FlowBoundsReprojectDeltaCm;
      FCrowdMassFinalKinematicState& Kinematic =
        FinalKinematics.AddDefaulted_GetRef();
      Kinematic.AgentId = Identities[It].Id;
      Kinematic.Position = Result.Location;
      Kinematic.Velocity = Result.Velocity;
      Kinematic.bValid = true;
      MaxNavigationDomainReprojectDeltaCm = FMath::Max(
        MaxNavigationDomainReprojectDeltaCm, Result.FlowBoundsReprojectDeltaCm);
      ++AgentCount;
    }
  });
  FinalKinematics.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  Pipeline->SetPreparedRuntimeFinalKinematics(MoveTemp(FinalKinematics));
  Pipeline->RecordNavigationDomainReprojectDelta(MaxNavigationDomainReprojectDeltaCm);
  Pipeline->LogStageOnce(
    Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("06_obstacle_constraint")
      : TEXT("05_obstacle_constraint"),
    AgentCount);
}

UCrowdDemoRoundFacingResolveProcessor::UCrowdDemoRoundFacingResolveProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundFacingResolveProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdMassFacingFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
  EntityQuery.AddTagRequirement<FCrowdMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundFacingResolveProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive()) return;

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

  FCrowdMassFacingWorkInput WorkInput;
  WorkInput.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  WorkInput.PlanRevision = Pipeline->GetCurrentPlanRevision();
  WorkInput.Settings.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
  TMap<int32, int32> ConsecutiveSettleStepsByAgentId;
  TMap<int32, bool> FinalSettledByAgentId;
  if (!Pipeline->IsBoundarySnapshotCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingBoundarySnapshotInvalid step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  TMap<int32, const FCrowdComposedGuidance*> ComposedByAgentId;
  for (const FCrowdComposedGuidance& Value
    : Pipeline->GetPreparedRuntimeComposedGuidance())
    ComposedByAgentId.Add(Value.AgentId, &Value);
  TMap<int32, const FCrowdParticleConstraintResult*> ParticleByAgentId;
  for (const FCrowdParticleConstraintResult& Value
    : Pipeline->GetPreparedRuntimeParticleResults())
    ParticleByAgentId.Add(Value.AgentId, &Value);
  TMap<int32, int32> PreviousSettleStepsByAgentId;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities =
      ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto RuntimeFacings =
      ChunkContext.GetMutableFragmentView<FCrowdMassFacingFragment>();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
      PreviousSettleStepsByAgentId.Add(
        Identities[It].Id,
        RuntimeFacings[It].ConsecutiveFinalSettleSteps);
  });
  bool bGatherValid = true;
  for (const FCrowdMassBoundaryAgentRecord& Base
    : Pipeline->GetBoundarySnapshot().Agents)
  {
    const FCrowdComposedGuidance* const* Composed =
      ComposedByAgentId.Find(Base.Identity.AgentId);
    const bool bUsesParticle = Pipeline->GetRules().Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure;
    const FCrowdParticleConstraintResult* const* Particle =
      ParticleByAgentId.Find(Base.Identity.AgentId);
    if (!Composed || (bUsesParticle && !Particle)
      || !PreviousSettleStepsByAgentId.Contains(Base.Identity.AgentId))
    {
      bGatherValid = false;
      continue;
    }
    const ECrowdDemoTargetRegionGuidanceMode* Mode =
      GuidanceModeByAgentId.Find(Base.Identity.AgentId);
    const bool bTerminalOwner = Mode
      && (*Mode == ECrowdDemoTargetRegionGuidanceMode::TerminalSettle
        || *Mode == ECrowdDemoTargetRegionGuidanceMode::EngagedHold);
    const bool bSettledThisStep = bTerminalOwner && bUsesParticle
      && FVector2f((*Particle)->CorrectedVelocity.X,
        (*Particle)->CorrectedVelocity.Y).Size() <= 20.0f
      && (*Particle)->RealizedCorrection.Size2D() <= 1.0f;
    const int32 ConsecutiveSettleSteps = bSettledThisStep
      ? PreviousSettleStepsByAgentId.FindRef(Base.Identity.AgentId) + 1 : 0;
    const bool bFinalPositionSettled = ConsecutiveSettleSteps >= 15;
    ConsecutiveSettleStepsByAgentId.Add(
      Base.Identity.AgentId, ConsecutiveSettleSteps);
    FinalSettledByAgentId.Add(
      Base.Identity.AgentId, bFinalPositionSettled);

    FCrowdFacingInput& Input = WorkInput.Agents.AddDefaulted_GetRef();
    Input.AgentId = Base.Identity.AgentId;
    Input.CurrentYawDegrees = Base.State.YawDegrees;
    Input.AutonomousPreferredVelocity = FVector2f(
      (*Composed)->AutonomousPreferredVelocity.X,
      (*Composed)->AutonomousPreferredVelocity.Y);
    const FVector FacingLocation = bUsesParticle
      ? (*Particle)->CorrectedPosition : Base.State.Position;
    Input.Location = FVector2f(FacingLocation.X, FacingLocation.Y);
    Input.TargetLocation = FVector2f(Pipeline->GetTargetFact().Location.X,
      Pipeline->GetTargetFact().Location.Y);
    Input.bHasTarget = Pipeline->IsTargetRegionExecutionActive();
    Input.bFinalPositionSettled = bFinalPositionSettled;
  }
  if (!bGatherValid || WorkInput.Agents.IsEmpty())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingGatherInvalid step=%d agents=%d"),
      Pipeline->GetCurrentFixedStepIndex(), WorkInput.Agents.Num());
    return;
  }
  TFuture<FCrowdMassFacingWorkOutput> Future = Async(
    EAsyncExecution::ThreadPool,
    [Input = MoveTemp(WorkInput)]() mutable
    {
      return FCrowdMassFacingWork::Resolve(Input);
    });
  FCrowdMassFacingWorkOutput WorkOutput = Future.Get();
  if (!WorkOutput.bCompleted)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingResolveInvalid step=%d inputs=%d results=%d"),
      Pipeline->GetCurrentFixedStepIndex(),
      ConsecutiveSettleStepsByAgentId.Num(), WorkOutput.Summary.Results.Num());
    return;
  }
  TMap<int32, const FCrowdFacingResult*> ById;
  for (const auto& Result : WorkOutput.Summary.Results)
    ById.Add(Result.AgentId, &Result);
  bool bResultSetValid =
    ById.Num() == ConsecutiveSettleStepsByAgentId.Num();
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities =
      ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
      bResultSetValid = bResultSetValid
        && ById.Contains(Identities[It].Id)
        && ConsecutiveSettleStepsByAgentId.Contains(Identities[It].Id)
        && FinalSettledByAgentId.Contains(Identities[It].Id);
  });
  if (!bResultSetValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingResultSetInvalid step=%d results=%d"),
      Pipeline->GetCurrentFixedStepIndex(), ById.Num());
    return;
  }
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto RuntimeFacings =
      ChunkContext.GetMutableFragmentView<FCrowdMassFacingFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdFacingResult& Result = **ById.Find(Identities[It].Id);
      const int32 ConsecutiveSteps =
        *ConsecutiveSettleStepsByAgentId.Find(Identities[It].Id);
      const bool bFinalSettled =
        *FinalSettledByAgentId.Find(Identities[It].Id);
      RuntimeFacings[It].Value = Result;
      RuntimeFacings[It].PlanRevision = WorkOutput.PlanRevision;
      RuntimeFacings[It].ConsecutiveFinalSettleSteps = ConsecutiveSteps;
      RuntimeFacings[It].bFinalPositionSettled = bFinalSettled;
    }
  });
  TArray<FCrowdFacingResult> PreparedFacingResults =
    WorkOutput.Summary.Results;
  PreparedFacingResults.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  TArray<FCrowdDemoPreparedFacingRollbackFact> PreparedFacingRollbackFacts;
  PreparedFacingRollbackFacts.Reserve(PreparedFacingResults.Num());
  for (const FCrowdFacingResult& Result : PreparedFacingResults)
  {
    FCrowdDemoPreparedFacingRollbackFact& Fact =
      PreparedFacingRollbackFacts.AddDefaulted_GetRef();
    Fact.AgentId = Result.AgentId;
    Fact.Facing.Value = Result;
    Fact.Facing.PlanRevision = WorkOutput.PlanRevision;
    Fact.Facing.ConsecutiveFinalSettleSteps =
      ConsecutiveSettleStepsByAgentId.FindRef(Result.AgentId);
    Fact.Facing.bFinalPositionSettled =
      FinalSettledByAgentId.FindRef(Result.AgentId);
  }
  Pipeline->SetPreparedRuntimeFacingResults(MoveTemp(PreparedFacingResults));
  Pipeline->SetPreparedFacingRollbackFacts(
    MoveTemp(PreparedFacingRollbackFacts));
}

UCrowdDemoRoundMovementWorkProcessor::UCrowdDemoRoundMovementWorkProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundMovementWorkProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionStepFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundProposedMovementFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassAgentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassSimulationStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassPropertiesFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassGuidanceCandidatesFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassComposedGuidanceFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassLocalVelocityFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
  EntityQuery.AddTagRequirement<FCrowdMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundMovementWorkProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  LastGuidanceWorkMilliseconds = 0.0f;
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
  FlowCandidates.Reserve(
    Pipeline->GetPreparedRuntimeSharedFlowOutputs().Num());
  for (const FCrowdMassSharedFlowAgentOutput& Value
    : Pipeline->GetPreparedRuntimeSharedFlowOutputs())
    FlowCandidates.Add(Value.Candidate);
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
    WorkInput.Environment =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(
        Pipeline->GetRules().FlowFieldConfig);
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
  for (const FCrowdMassGatherRecord& Record : WorkInput.Guidance.Records)
  {
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
  TMap<int32, FCrowdMassMovementPipelineAgentOverlay*> OverlayByAgentId;
  for (FCrowdMassMovementPipelineAgentOverlay& Overlay
    : WorkInput.AgentOverlays)
    OverlayByAgentId.Add(Overlay.AgentId, &Overlay);
  bool bReactiveGatherValid = true;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities =
      ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto ReactiveSteps =
      ChunkContext.GetFragmentView<FCrowdDemoReactiveMotionStepFragment>();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdMassMovementPipelineAgentOverlay* const* Found =
        OverlayByAgentId.Find(Identities[It].Id);
      if (!Found)
      {
        bReactiveGatherValid = false;
        continue;
      }
      if (ReactiveSteps[It].bActive)
      {
        (*Found)->bVerticalOverride = true;
        (*Found)->ProposedZ = ReactiveSteps[It].ProposedZ;
        (*Found)->VerticalVelocityCmps =
          ReactiveSteps[It].VerticalVelocityCmps;
      }
    }
  });
  if (!bReactiveGatherValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementWorkReactiveGatherInvalid step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }

  const TArray<FCrowdMassGatherRecord> GatherRecords =
    WorkInput.Guidance.Records;
  TFuture<FCrowdMassMovementPipelineWorkOutput> Future = Async(
    EAsyncExecution::ThreadPool,
    [Input = MoveTemp(WorkInput)]() mutable
    {
      return FCrowdMassMovementPipelineWork::Run(Input);
    });
  FCrowdMassMovementPipelineWorkOutput WorkOutput = Future.Get();
  LastGuidanceWorkMilliseconds = WorkOutput.GuidanceWorkMilliseconds;
  if (!WorkOutput.bCompleted)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementWorkIncomplete step=%d composed=%d local=%d predicted=%d"),
      Pipeline->GetCurrentFixedStepIndex(),
      WorkOutput.Guidance.ComposedGuidance.Num(),
      WorkOutput.LocalPredictive.Results.Num(),
      WorkOutput.MovementPredict.Results.Num());
    return;
  }

  TMap<int32, const FCrowdMassGatherRecord*> GatheredById;
  for (const FCrowdMassGatherRecord& Record : GatherRecords)
    GatheredById.Add(Record.Identity.AgentId, &Record);
  TMap<int32, const FCrowdComposedGuidance*> ComposedById;
  for (const FCrowdComposedGuidance& Value
    : WorkOutput.Guidance.ComposedGuidance)
    ComposedById.Add(Value.AgentId, &Value);
  TMap<int32, const FCrowdLocalPredictiveResult*> LocalById;
  for (const FCrowdLocalPredictiveResult& Value
    : WorkOutput.LocalPredictive.Results)
    LocalById.Add(Value.AgentId, &Value);
  TMap<int32, const FCrowdMassPredictedMovement*> PredictedById;
  for (const FCrowdMassPredictedMovement& Value
    : WorkOutput.MovementPredict.Results)
    PredictedById.Add(Value.AgentId, &Value);
  bool bResultSetValid = GatheredById.Num()
      == WorkOutput.Guidance.ComposedGuidance.Num()
    && PredictedById.Num() == GatheredById.Num()
    && (!bUseLocal || LocalById.Num() == GatheredById.Num());
  int32 ValidatedCount = 0;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities =
      ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const int32 AgentId = Identities[It].Id;
      if (!GatheredById.Contains(AgentId)
        || !ComposedById.Contains(AgentId)
        || !PredictedById.Contains(AgentId)
        || (bUseLocal && !LocalById.Contains(AgentId)))
      {
        bResultSetValid = false;
        continue;
      }
      ++ValidatedCount;
    }
  });
  if (!bResultSetValid || ValidatedCount != GatheredById.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementWorkResultSetInvalid step=%d validated=%d records=%d"),
      Pipeline->GetCurrentFixedStepIndex(), ValidatedCount,
      GatheredById.Num());
    return;
  }

  TArray<FCrowdDemoComposedGuidance> DemoComposed;
  DemoComposed.Reserve(WorkOutput.Guidance.ComposedGuidance.Num());
  for (const FCrowdComposedGuidance& Value
    : WorkOutput.Guidance.ComposedGuidance)
    DemoComposed.Add(
      FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoComposedGuidance(Value));
  TArray<FCrowdDemoLocalPredictivePair> ConflictPairs;
  TArray<FCrowdDemoLocalPredictiveGrantState> GrantStates;
  TArray<FCrowdDemoLocalPredictiveResult> DemoLocalResults;
  FCrowdDemoLocalPredictiveSummary DemoLocalSummary;
  if (bUseLocal)
  {
    for (const auto& Value : WorkOutput.LocalPredictive.ConflictPairs)
      ConflictPairs.Add(
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictivePair(Value));
    for (const auto& Value : WorkOutput.LocalPredictive.GrantStates)
      GrantStates.Add(
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictiveGrant(Value));
    for (const auto& Value : WorkOutput.LocalPredictive.Results)
      DemoLocalResults.Add(
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictiveResult(Value));
    DemoLocalSummary =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictiveSummary(
        WorkOutput.LocalPredictive.Summary);
    if (bCaptureDiagnostic)
    {
      FCrowdDemoLocalPredictiveSettings DiagnosticSettings;
      const FCrowdDemoLocalPredictiveRuleSettings& Rule =
        Pipeline->GetRules().LocalPredictiveSettings;
      DiagnosticSettings.FixedStepSeconds =
        Pipeline->GetCurrentFixedStepSeconds();
      DiagnosticSettings.TimeHorizonSeconds = Rule.TimeHorizonSeconds;
      DiagnosticSettings.SpatialCellSizeCm = Rule.SpatialCellSizeCm;
      DiagnosticSettings.VelocityQuantumCmps =
        Pipeline->GetRules().ParticleVelocityQuantumCmps;
      DiagnosticSettings.ConstraintEpsilonCmps =
        Rule.ConstraintEpsilonCmps;
      DiagnosticSettings.RequestedProgressThresholdCmps =
        Rule.RequestedProgressThresholdCmps;
      DiagnosticSettings.BlockedProgressThresholdCmps =
        Rule.BlockedProgressThresholdCmps;
      DiagnosticSettings.GrantedResponsibility = Rule.GrantedResponsibility;
      DiagnosticSettings.GrantDurationSteps = Rule.GrantDurationSteps;
      DiagnosticSettings.JointIterationCount = Rule.JointIterationCount;
      FCrowdDemoLocalPredictiveDiagnosticFrame Frame;
      Frame.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
      Frame.Settings = DiagnosticSettings;
      for (const FCrowdLocalPredictiveAgent& Agent
        : WorkOutput.LocalPredictiveAgents)
        Frame.Agents.Add(
          FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictiveAgent(Agent));
      Frame.PreviousGrantStates = PreviousGrantStates;
      Frame.ConflictPairs = ConflictPairs;
      Frame.GrantStates = GrantStates;
      Frame.Results = DemoLocalResults;
      Frame.Summary = DemoLocalSummary;
      Frame.Trace =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoLocalPredictiveTrace(
          WorkOutput.LocalPredictive.DiagnosticTrace);
      Pipeline->RecordLocalPredictiveDiagnosticFrame(MoveTemp(Frame));
    }
  }

  int32 AppliedCount = 0;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities =
      ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto RuntimeIdentities =
      ChunkContext.GetMutableFragmentView<FCrowdMassAgentFragment>();
    const auto RuntimeStates =
      ChunkContext.GetMutableFragmentView<FCrowdMassSimulationStateFragment>();
    const auto RuntimeProperties =
      ChunkContext.GetMutableFragmentView<FCrowdMassPropertiesFragment>();
    const auto RuntimeCandidates =
      ChunkContext.GetMutableFragmentView<FCrowdMassGuidanceCandidatesFragment>();
    const auto RuntimeComposed =
      ChunkContext.GetMutableFragmentView<FCrowdMassComposedGuidanceFragment>();
    const auto RuntimeLocal =
      ChunkContext.GetMutableFragmentView<FCrowdMassLocalVelocityFragment>();
    const auto Proposed =
      ChunkContext.GetMutableFragmentView<FCrowdDemoRoundProposedMovementFragment>();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const int32 AgentId = Identities[It].Id;
      const FCrowdMassGatherRecord& Record = **GatheredById.Find(AgentId);
      const FCrowdComposedGuidance& Composed = **ComposedById.Find(AgentId);
      const FCrowdMassPredictedMovement& Predicted = **PredictedById.Find(AgentId);
      RuntimeIdentities[It] = Record.Identity;
      RuntimeStates[It] = Record.State;
      RuntimeProperties[It] = Record.Properties;
      RuntimeCandidates[It] = Record.Guidance;
      RuntimeComposed[It].Value = Composed;
      if (bUseLocal)
      {
        RuntimeLocal[It].Value = **LocalById.Find(AgentId);
        RuntimeLocal[It].PlanRevision =
          WorkOutput.LocalPredictive.PlanRevision;
      }
      Proposed[It].StartLocation = Predicted.StartPosition;
      Proposed[It].ProposedLocation = Predicted.PredictedPosition;
      Proposed[It].ProposedVelocity = Predicted.Velocity;
      ++AppliedCount;
    }
  });
  if (AppliedCount != GatheredById.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementWorkApplyIncomplete step=%d applied=%d records=%d"),
      Pipeline->GetCurrentFixedStepIndex(), AppliedCount, GatheredById.Num());
    return;
  }

  Pipeline->SetPreparedRuntimeComposedGuidance(
    TArray<FCrowdComposedGuidance>(
      WorkOutput.Guidance.ComposedGuidance));
  Pipeline->RecordGuidanceComposeStep(MoveTemp(DemoComposed));
  if (bUseLocal)
  {
    Pipeline->RecordLocalPredictiveStep(
      MoveTemp(DemoLocalResults), MoveTemp(GrantStates), DemoLocalSummary);
    Pipeline->LogStageOnce(
      TEXT("05_local_predictive_interaction"), AppliedCount);
  }
  Pipeline->SetPreparedRuntimePredictedMovements(
    MoveTemp(WorkOutput.MovementPredict.Results));
  if (bOpenSpawnRelaxation)
  {
    TArray<int32> PendingResetAgentIds;
    for (const FCrowdDemoPreparedOpenSpawnBoundaryFact& Fact
      : Pipeline->GetPreparedOpenSpawnBoundaryFacts())
      if (Fact.bPendingBoundaryReset)
        PendingResetAgentIds.Add(Fact.AgentId);
    if (!PendingResetAgentIds.IsEmpty()
      && !Pipeline->ConsumeOpenSpawnBoundaryResets(PendingResetAgentIds))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMovementWorkT1ResetConsumeInvalid step=%d resets=%d"),
        Pipeline->GetCurrentFixedStepIndex(), PendingResetAgentIds.Num());
      return;
    }
  }
  Pipeline->LogStageOnce(TEXT("08_guidance_compose"), AppliedCount);
  Pipeline->LogStageOnce(
    Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("05_movement_predict") : TEXT("04_movement_predict"),
    AppliedCount);
}

UCrowdDemoRoundMovementFinalizeProcessor::UCrowdDemoRoundMovementFinalizeProcessor()
  : ValidationQuery(*this)
  , ApplyQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundMovementFinalizeProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  ValidationQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  ValidationQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadOnly);
  ValidationQuery.AddRequirement<FCrowdMassAgentFragment>(EMassFragmentAccess::ReadOnly);
  ValidationQuery.AddRequirement<FCrowdMassParticleConstraintFragment>(EMassFragmentAccess::ReadOnly);
  ValidationQuery.AddRequirement<FCrowdMassFacingFragment>(EMassFragmentAccess::ReadOnly);
  ValidationQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
  ValidationQuery.AddTagRequirement<FCrowdMassAgentTag>(EMassFragmentPresence::All);

  ApplyQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  ApplyQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadWrite);
  ApplyQuery.AddRequirement<FCrowdMassAgentFragment>(EMassFragmentAccess::ReadOnly);
  ApplyQuery.AddRequirement<FCrowdMassSimulationStateFragment>(EMassFragmentAccess::ReadWrite);
  ApplyQuery.AddRequirement<FCrowdMassMovementOutputFragment>(EMassFragmentAccess::ReadWrite);
  ApplyQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
  ApplyQuery.AddTagRequirement<FCrowdMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundMovementFinalizeProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!World || !Pipeline || !Pipeline->IsActive())
  {
    return;
  }
  FCrowdMassMovementFinalizeWorkInput FinalizeInput;
  TArray<FCrowdMassCommitTarget> CommitTargets;
  const bool bFinalizeGatherValid = Pipeline->IsBoundarySnapshotCurrent()
    && FCrowdMassMovementFinalizeWork::BuildInputFromPrepared(
      Pipeline->GetBoundarySnapshot(),
      Pipeline->GetPreparedRuntimeFinalKinematics(),
      Pipeline->GetPreparedRuntimeFacingResults(),
      FinalizeInput, CommitTargets);
  if (!bFinalizeGatherValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementFinalizePreparedInputInvalid step=%d snapshot=%d kinematics=%d facings=%d records=%d"),
      Pipeline->GetCurrentFixedStepIndex(),
      Pipeline->GetBoundarySnapshot().Agents.Num(),
      Pipeline->GetPreparedRuntimeFinalKinematics().Num(),
      Pipeline->GetPreparedRuntimeFacingResults().Num(),
      FinalizeInput.Records.Num());
    return;
  }
  TFuture<FCrowdMassMovementFinalizeWorkOutput> FinalizeFuture = Async(
    EAsyncExecution::ThreadPool,
    [Input = MoveTemp(FinalizeInput)]() mutable
    {
      return FCrowdMassMovementFinalizeWork::BuildCommitPlan(Input);
    });
  FCrowdMassMovementFinalizeWorkOutput FinalizeOutput = FinalizeFuture.Get();
  if (!FinalizeOutput.bCompleted
    || !FCrowdMassRuntimeBridge::ValidateCommitTargets(
      FinalizeOutput.CommitPlan, CommitTargets))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementFinalizeCommitPlanInvalid step=%d records=%d targets=%d"),
      Pipeline->GetCurrentFixedStepIndex(),
      FinalizeOutput.CommitPlan.Records.Num(), CommitTargets.Num());
    return;
  }
  TMap<int32, const FCrowdMassCommitRecord*> CommitByAgentId;
  for (const FCrowdMassCommitRecord& Record : FinalizeOutput.CommitPlan.Records)
    CommitByAgentId.Add(Record.Movement.AgentId, &Record);
  bool bAtomicApplySetValid = true;
  int32 AtomicApplyTargetCount = 0;
  const bool bSoftPressure = Pipeline->GetRules().Scenario
    == ECrowdDemoScenario::SimRoundSoftPressure;
  ValidationQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto DemoIdentities =
      ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto RuntimeIdentities =
      ChunkContext.GetFragmentView<FCrowdMassAgentFragment>();
    const auto RuntimeParticles =
      ChunkContext.GetFragmentView<FCrowdMassParticleConstraintFragment>();
    const auto Constraints =
      ChunkContext.GetFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
    const auto RuntimeFacings =
      ChunkContext.GetFragmentView<FCrowdMassFacingFragment>();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdMassCommitRecord* const* Record =
        CommitByAgentId.Find(DemoIdentities[It].Id);
      const FCrowdMovementOutput* Movement = Record
        ? &(*Record)->Movement : nullptr;
      const bool bFacingRuntimeValid = Movement
        && RuntimeFacings[It].PlanRevision == FinalizeOutput.CommitPlan.PlanRevision
        && RuntimeFacings[It].Value.AgentId == DemoIdentities[It].Id
        && FMath::IsNearlyEqual(Movement->YawDegrees,
          RuntimeFacings[It].Value.ResolvedYawDegrees, 0.01f);
      const bool bKinematicRuntimeValid = Movement && (bSoftPressure
        ? RuntimeParticles[It].PlanRevision
            == FinalizeOutput.CommitPlan.PlanRevision
          && RuntimeParticles[It].Value.AgentId == DemoIdentities[It].Id
          && Movement->Position.Equals(
            RuntimeParticles[It].Value.CorrectedPosition, 0.01f)
          && Movement->Velocity.Equals(
            RuntimeParticles[It].Value.CorrectedVelocity, 0.01f)
        : Movement->Position.Equals(
            Constraints[It].ConstrainedLocation, 0.01f)
          && Movement->Velocity.Equals(
            Constraints[It].ConstrainedVelocity, 0.01f));
      if (!Record || !(*Record)->Movement.bValid
        || (*Record)->Movement.LifecycleSerial
          != static_cast<uint32>(DemoIdentities[It].LifecycleSerial)
        || RuntimeIdentities[It].AgentId != DemoIdentities[It].Id
        || RuntimeIdentities[It].LifecycleSerial
          != DemoIdentities[It].LifecycleSerial
        || !bFacingRuntimeValid || !bKinematicRuntimeValid)
      {
        bAtomicApplySetValid = false;
        continue;
      }
      ++AtomicApplyTargetCount;
    }
  });
  if (!bAtomicApplySetValid
    || AtomicApplyTargetCount != FinalizeOutput.CommitPlan.Records.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementFinalizeAtomicSetInvalid step=%d validated=%d records=%d"),
      Pipeline->GetCurrentFixedStepIndex(), AtomicApplyTargetCount,
      FinalizeOutput.CommitPlan.Records.Num());
    return;
  }
  int32 AppliedCount = 0;
  ApplyQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto RuntimeIdentities =
      ChunkContext.GetFragmentView<FCrowdMassAgentFragment>();
    const auto RuntimeStates =
      ChunkContext.GetMutableFragmentView<FCrowdMassSimulationStateFragment>();
    const auto RuntimeMovements =
      ChunkContext.GetMutableFragmentView<FCrowdMassMovementOutputFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoRoundSimStateFragment& State = States[It];
      const FCrowdMassCommitRecord* const* RecordPtr =
        CommitByAgentId.Find(Identities[It].Id);
      const bool bDemoApplied =
        FCrowdDemoMassCrowdRuntimeAdapter::ApplyCommitRecord(
          **RecordPtr, Identities[It], State);
      const bool bRuntimeApplied =
        FCrowdMassRuntimeBridge::ApplyMovementToState(
          **RecordPtr,
          {RuntimeIdentities[It].AgentId,
            static_cast<uint32>(RuntimeIdentities[It].LifecycleSerial)},
          RuntimeStates[It], RuntimeMovements[It]);
      check(bDemoApplied && bRuntimeApplied);
      State.SimulatedServerTimeSeconds = Pipeline->GetCurrentStepEndServerTimeSeconds();
      ++AppliedCount;
    }
  });
  if (AppliedCount != FinalizeOutput.CommitPlan.Records.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementFinalizeApplyCountMismatch step=%d applied=%d records=%d"),
      Pipeline->GetCurrentFixedStepIndex(), AppliedCount,
      FinalizeOutput.CommitPlan.Records.Num());
    return;
  }
  Pipeline->MarkMovementFinalizeApplied();
  Pipeline->LogStageOnce(
    Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("09_movement_finalize")
      : TEXT("06_movement_finalize"),
    AppliedCount);
}

UCrowdDemoRoundPostFinalizeMetricsProcessor::
UCrowdDemoRoundPostFinalizeMetricsProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundPostFinalizeMetricsProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundPostFinalizeMetricsProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!World || !Pipeline || !Pipeline->IsActive()) return;
  if (!Pipeline->IsMovementFinalizeAppliedCurrent()) return;

  TMap<int32, const FCrowdMassBoundaryAgentRecord*> BoundaryByAgentId;
  for (const FCrowdMassBoundaryAgentRecord& Value
    : Pipeline->GetBoundarySnapshot().Agents)
  {
    BoundaryByAgentId.Add(Value.Identity.AgentId, &Value);
  }
  TMap<int32, const FCrowdDemoRoundBoundaryFormationFact*> FormationByAgentId;
  for (const FCrowdDemoRoundBoundaryFormationFact& Value
    : Pipeline->GetBoundaryFormationFacts())
  {
    FormationByAgentId.Add(Value.AgentId, &Value);
  }
  TMap<int32, const FCrowdMassSharedFlowAgentOutput*> FlowOutputByAgentId;
  for (const FCrowdMassSharedFlowAgentOutput& Value
    : Pipeline->GetPreparedRuntimeSharedFlowOutputs())
  {
    FlowOutputByAgentId.Add(Value.AgentId, &Value);
  }
  TMap<int32, const FCrowdDemoPreparedFacingRollbackFact*> FacingByAgentId;
  for (const FCrowdDemoPreparedFacingRollbackFact& Value
    : Pipeline->GetPreparedFacingRollbackFacts())
  {
    FacingByAgentId.Add(Value.AgentId, &Value);
  }
  bool bPreparedSetValid = BoundaryByAgentId.Num()
      == Pipeline->GetBoundarySnapshot().Agents.Num()
    && FormationByAgentId.Num() == Pipeline->GetBoundaryFormationFacts().Num()
    && FlowOutputByAgentId.Num()
      == Pipeline->GetPreparedRuntimeSharedFlowOutputs().Num()
    && FacingByAgentId.Num()
      == Pipeline->GetPreparedFacingRollbackFacts().Num()
    && BoundaryByAgentId.Num() == FormationByAgentId.Num()
    && BoundaryByAgentId.Num() == FlowOutputByAgentId.Num()
    && BoundaryByAgentId.Num() == FacingByAgentId.Num();

  TArray<FCrowdDemoRoundFlowAgentSample> MetricSamples;
  TArray<FCrowdDemoSoftPressureRollbackAgentState> SoftPressureRollbackAgents;
  TArray<int32> OpenSpawnAgentIds;
  TArray<FVector> OpenSpawnLocations;
  TArray<FCrowdDemoBidirectionalSwapStepAgent> BidirectionalSwapAgents;
  TArray<FCrowdDemoValidCorridorTransitStepAgent> ValidCorridorTransitAgents;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdDemoRoundSimStateFragment& State = States[It];
      const FCrowdMassBoundaryAgentRecord* const* Boundary =
        BoundaryByAgentId.Find(Identities[It].Id);
      const FCrowdDemoRoundBoundaryFormationFact* const* Formation =
        FormationByAgentId.Find(Identities[It].Id);
      const FCrowdMassSharedFlowAgentOutput* const* FlowOutput =
        FlowOutputByAgentId.Find(Identities[It].Id);
      const FCrowdDemoPreparedFacingRollbackFact* const* Facing =
        FacingByAgentId.Find(Identities[It].Id);
      if (!Boundary || !Formation || !FlowOutput || !Facing)
      {
        bPreparedSetValid = false;
        continue;
      }
      if (Pipeline->IsOpenSpawnRelaxation())
      {
        OpenSpawnAgentIds.Add(Identities[It].Id);
        OpenSpawnLocations.Add(State.Location);
      }

      FCrowdDemoRoundFlowAgentSample& Metric = MetricSamples.AddDefaulted_GetRef();
      Metric.AgentId = Identities[It].Id;
      Metric.Location = State.Location;
      Metric.Velocity = State.Velocity;
      const FCrowdDemoSharedFlowField* FinalField = &Pipeline->GetSharedFlowField();
      if (Pipeline->IsBidirectionalSwap())
        FinalField = Pipeline->FindBidirectionalSwapFlowField(
          (*Formation)->FormationIndex);
      if (!FinalField) FinalField = &Pipeline->GetSharedFlowField();
      const FCrowdDemoSharedFlowSample FinalFlowSample =
        FCrowdDemoSharedFlowFieldKernel::Sample(*FinalField, State.Location);
      Metric.bUnreachable = FinalFlowSample.Status
        != ECrowdDemoFlowLocationStatus::Reachable;
      FCrowdDemoSharedFlowFieldConfig PhysicalObstacleConfig = FinalField->Config;
      if (Pipeline->GetRules().Scenario
        == ECrowdDemoScenario::SimRoundSoftPressure)
      {
        PhysicalObstacleConfig.AgentInflateCm = FMath::Max(
          (*Boundary)->Properties.PhysicalRadiusCm
            + (*Boundary)->Properties.HardSafetyGapCm,
          Pipeline->GetRules().bEnableHeterogeneousProfiles != 0
            ? Pipeline->GetRules().FlowFieldConfig.AgentInflateCm
            : 0.0f);
      }
      const bool bStartPenetrating = Pipeline->GetRules().Scenario
          != ECrowdDemoScenario::SimRoundSoftPressure
        && FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
          PhysicalObstacleConfig, (*Boundary)->State.Position);
      Metric.bPenetrating = bStartPenetrating
        || FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
          PhysicalObstacleConfig, State.Location);
      if (Pipeline->IsBidirectionalSwap())
      {
        auto& SwapAgent = BidirectionalSwapAgents.AddDefaulted_GetRef();
        SwapAgent.AgentId = Identities[It].Id;
        SwapAgent.FormationIndex = (*Formation)->FormationIndex;
        SwapAgent.Location = State.Location;
        SwapAgent.Velocity = State.Velocity;
        SwapAgent.FlowStatus = FinalFlowSample.Status;
      }
      if (Pipeline->IsCorridorTransitProgressScenario())
      {
        auto& TransitAgent = ValidCorridorTransitAgents.AddDefaulted_GetRef();
        TransitAgent.AgentId = Identities[It].Id;
        TransitAgent.Location = State.Location;
        TransitAgent.Velocity = State.Velocity;
        TransitAgent.FlowStatus = FinalFlowSample.Status;
      }
      if (Pipeline->GetRules().Scenario
        == ECrowdDemoScenario::SimRoundSoftPressure)
      {
        FCrowdDemoSoftPressureRollbackAgentState& Rollback =
          SoftPressureRollbackAgents.AddDefaulted_GetRef();
        Rollback.AgentId = Identities[It].Id;
        Rollback.LifecycleSerial = Identities[It].LifecycleSerial;
        Rollback.Location = State.Location;
        Rollback.Velocity = State.Velocity;
        Rollback.YawDegrees = State.YawDegrees;
        // RadiusCm is part of the replicated RoundSim/checkpoint contract. It is
        // not interchangeable with the particle solver's physical radius: T6
        // capability profiles can intentionally keep different presentation /
        // formation and particle values. Preserve the exact boundary fact that
        // previously came from FCrowdDemoRoundFormationFragment.
        Rollback.RadiusCm = (*Formation)->RadiusCm;
        Rollback.SimulatedServerTimeSeconds = State.SimulatedServerTimeSeconds;
        Rollback.PlanRevision = State.PlanRevision;
        Rollback.bInitialized = State.bInitialized;
        const FCrowdDemoSharedFlowSample RollbackFlowSample =
          FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowSample(
            (*FlowOutput)->Sample);
        Rollback.FlowSample.CellIndex = RollbackFlowSample.CellIndex;
        Rollback.FlowSample.StableCellKey = RollbackFlowSample.StableCellKey;
        Rollback.FlowSample.NavigationNodeKey =
          RollbackFlowSample.NavigationNodeKey;
        Rollback.FlowSample.NextNavigationNodeKey =
          RollbackFlowSample.NextNavigationNodeKey;
        Rollback.FlowSample.Status = RollbackFlowSample.Status;
        Rollback.FlowSample.FlowDirection = RollbackFlowSample.FlowDirection;
        Rollback.FlowSample.IntegrationCost = RollbackFlowSample.IntegrationCost;
        Rollback.FlowSample.GuidanceDistanceCm =
          RollbackFlowSample.GuidanceDistanceCm;
        Rollback.FlowSample.bBlocked = RollbackFlowSample.bBlocked;
        Rollback.FlowSample.bUnreachable = RollbackFlowSample.bUnreachable;
        Rollback.FlowSample.bRecoveredFromRasterMismatch =
          RollbackFlowSample.bRecoveredFromRasterMismatch;
        Rollback.FlowSample.bSourceAttached =
          RollbackFlowSample.bSourceAttached;
        Rollback.Facing = (*Facing)->Facing;
      }
    }
  });
  if (!bPreparedSetValid
    || MetricSamples.Num() != Pipeline->GetBoundarySnapshot().Agents.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoPostFinalizePreparedSetInvalid step=%d snapshot=%d metrics=%d formations=%d flow=%d facing=%d"),
      Pipeline->GetCurrentFixedStepIndex(),
      Pipeline->GetBoundarySnapshot().Agents.Num(), MetricSamples.Num(),
      FormationByAgentId.Num(), FlowOutputByAgentId.Num(),
      FacingByAgentId.Num());
    return;
  }
  if (Pipeline->IsOpenSpawnRelaxation())
  {
    FCrowdDemoOpenSpawnRelaxationKernel::RecordFinalLocations(
      OpenSpawnAgentIds, OpenSpawnLocations,
      Pipeline->GetOpenSpawnRelaxationRuntime());
  }
  if (Pipeline->IsBidirectionalSwap())
    Pipeline->RecordBidirectionalSwapStep(BidirectionalSwapAgents);
  if (Pipeline->IsCorridorTransitProgressScenario())
    Pipeline->RecordValidCorridorTransitStep(ValidCorridorTransitAgents);
  if (Pipeline->GetRules().Scenario
    == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    Pipeline->RecordSoftPressureRollbackSnapshot(
      Pipeline->GetCurrentFixedStepIndex(),
      MoveTemp(SoftPressureRollbackAgents));
  }
  Pipeline->RecordFlowAgentSamples(
    MetricSamples, World->GetNetMode() == NM_Client);
}

static void ConfigureCommitQuery(FMassEntityQuery& Query)
{
  Query.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  Query.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  Query.AddRequirement<FCrowdMassAgentFragment>(EMassFragmentAccess::ReadOnly);
  Query.AddRequirement<FCrowdMassMovementOutputFragment>(EMassFragmentAccess::ReadOnly);
  Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdDemoMassMovementFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
  Query.AddTagRequirement<FCrowdMassAgentTag>(EMassFragmentPresence::All);
}

static int32 CommitRoundState(FMassEntityQuery& Query, FMassExecutionContext& Context)
{
  bool bCommitSetValid = true;
  int32 ValidatedCount = 0;
  Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto DemoIdentities =
      ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States =
      ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto RuntimeIdentities =
      ChunkContext.GetFragmentView<FCrowdMassAgentFragment>();
    const auto RuntimeMovements =
      ChunkContext.GetFragmentView<FCrowdMassMovementOutputFragment>();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdMovementOutput& Movement = RuntimeMovements[It].Value;
      if (!States[It].bInitialized || !Movement.bValid
        || DemoIdentities[It].Id != RuntimeIdentities[It].AgentId
        || DemoIdentities[It].LifecycleSerial
          != RuntimeIdentities[It].LifecycleSerial
        || Movement.AgentId != RuntimeIdentities[It].AgentId
        || Movement.LifecycleSerial != static_cast<uint32>(
          RuntimeIdentities[It].LifecycleSerial)
        || !Movement.Position.Equals(States[It].Location, 0.01f)
        || !Movement.Velocity.Equals(States[It].Velocity, 0.01f)
        || !FMath::IsNearlyEqual(
          Movement.YawDegrees, States[It].YawDegrees, 0.01f))
      {
        bCommitSetValid = false;
        continue;
      }
      ++ValidatedCount;
    }
  });
  if (!bCommitSetValid) return INDEX_NONE;

  int32 Count = 0;
  Query.ForEachEntityChunk(Context, [&Count](FMassExecutionContext& ChunkContext)
  {
    const auto RuntimeMovements =
      ChunkContext.GetFragmentView<FCrowdMassMovementOutputFragment>();
    const TArrayView<FTransformFragment> Transforms =
      ChunkContext.GetMutableFragmentView<FTransformFragment>();
    const TArrayView<FMassVelocityFragment> Velocities =
      ChunkContext.GetMutableFragmentView<FMassVelocityFragment>();
    const TArrayView<FCrowdDemoMassMovementFragment> Movements =
      ChunkContext.GetMutableFragmentView<FCrowdDemoMassMovementFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdMovementOutput& RuntimeMovement = RuntimeMovements[It].Value;
      FTransform Transform = Transforms[It].GetTransform();
      Transform.SetLocation(RuntimeMovement.Position);
      Transform.SetRotation(
        FRotator(0.0f, RuntimeMovement.YawDegrees, 0.0f).Quaternion());
      Transforms[It].SetTransform(Transform);
      Velocities[It].Value = RuntimeMovement.Velocity;
      Movements[It].CurrentVelocity = RuntimeMovement.Velocity;
      Movements[It].DesiredVelocity = RuntimeMovement.Velocity;
      Movements[It].YawDegrees = RuntimeMovement.YawDegrees;
      ++Count;
    }
  });
  return Count == ValidatedCount ? Count : INDEX_NONE;
}

UCrowdDemoRoundAuthorityCommitProcessor::UCrowdDemoRoundAuthorityCommitProcessor()
  : EntityQuery(*this)
{
  ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone);
  bAutoRegisterWithProcessingPhases = false;
}

void UCrowdDemoRoundAuthorityCommitProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
  ConfigureCommitQuery(EntityQuery);
}

void UCrowdDemoRoundAuthorityCommitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline =
    EntityManager.GetWorld()->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>();
  if (!Pipeline || !Pipeline->IsMovementFinalizeAppliedCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoAuthorityCommitBeforeMovementFinalize"));
    return;
  }
  const int32 Count = CommitRoundState(EntityQuery, Context);
  if (Count == INDEX_NONE)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoAuthorityCommitRuntimeOutputInvalid"));
    return;
  }
  Pipeline->LogStageOnce(
    Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("10_authority_commit")
      : TEXT("07_authority_commit"),
    Count);
}

UCrowdDemoRoundClientPredictionCommitProcessor::UCrowdDemoRoundClientPredictionCommitProcessor()
  : EntityQuery(*this)
{
  ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
  bAutoRegisterWithProcessingPhases = false;
}

void UCrowdDemoRoundClientPredictionCommitProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
  ConfigureCommitQuery(EntityQuery);
}

void UCrowdDemoRoundClientPredictionCommitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline =
    EntityManager.GetWorld()->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>();
  if (!Pipeline || !Pipeline->IsMovementFinalizeAppliedCurrent())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoClientCommitBeforeMovementFinalize"));
    return;
  }
  const int32 Count = CommitRoundState(EntityQuery, Context);
  if (Count == INDEX_NONE)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoClientPredictionCommitRuntimeOutputInvalid"));
    return;
  }
  Pipeline->LogStageOnce(
    Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("10_client_prediction_commit")
      : TEXT("07_client_prediction_commit"),
    Count);
}

UCrowdDemoRoundCheckpointPublisherProcessor::UCrowdDemoRoundCheckpointPublisherProcessor()
  : EntityQuery(*this)
{
  ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone);
  bAutoRegisterWithProcessingPhases = false;
}

void UCrowdDemoRoundCheckpointPublisherProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassStatsFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoBusinessStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRangedAttackFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoHitFlashFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundCheckpointPublisherProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive())
  {
    return;
  }
  if (!Pipeline->ShouldBuildCorrectionFrame() && !Pipeline->ShouldBuildRoundResult())
  {
    return;
  }
  if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && !Pipeline->IsSoftPressureRollbackSnapshotReadyForReplay(
      Pipeline->GetCurrentFixedStepIndex()))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoCheckpointSnapshotIncomplete step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  TArray<FCrowdDemoRoundAgentState> States;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoRoundSimStateFragment> SimStates = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const TConstArrayView<FCrowdDemoRoundFormationFragment> Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Stats = ChunkContext.GetFragmentView<FCrowdDemoMassStatsFragment>();
    const auto Businesses = ChunkContext.GetFragmentView<FCrowdDemoBusinessStateFragment>();
    const auto Attacks = ChunkContext.GetFragmentView<FCrowdDemoRangedAttackFragment>();
    const auto Reactives = ChunkContext.GetFragmentView<FCrowdDemoReactiveMotionFragment>();
    const auto HitFlashes = ChunkContext.GetFragmentView<FCrowdDemoHitFlashFragment>();
    const auto Visuals = ChunkContext.GetFragmentView<FCrowdDemoMassVisualFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (SimStates[It].bInitialized)
      {
        const FCrowdDemoCombatNetState Combat = MakeCombatNetState(
          Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
        States.Add(MakeRoundAgentState(
          Identities[It], Formations[It], SimStates[It], &Combat));
      }
    }
  });
  SortAgentStates(States);
  FVector Center = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  for (const FCrowdDemoRoundAgentState& State : States)
  {
    Center += FVector(State.Location);
    Velocity += FVector(State.Velocity);
  }
  if (!States.IsEmpty())
  {
    Center /= States.Num();
    Velocity /= States.Num();
  }

  const bool bBuildRoundResult = Pipeline->ShouldBuildRoundResult();
  const bool bBuildCorrection = Pipeline->ShouldBuildCorrectionFrame();
  int32 CheckpointRevision = 0;
  int32 StateFrameRevision = 0;
  if (bBuildRoundResult)
  {
    FCrowdDemoRoundResultPacket Result;
    // bValid denotes a transportable packet, not an algorithm pass/fail.
    // Particle capability failure is carried by ParticleInvalidStepCount and
    // the pinned failure fixture so the client can still assemble checkpoint.
    Result.bValid = 1;
    Result.RoundId = Pipeline->GetCurrentRoundId();
    Result.Revision = Pipeline->GetCurrentPlanRevision();
    Result.CheckpointRevision = Pipeline->AllocateCheckpointRevision();
    Result.StateFrameRevision = Pipeline->AllocateCorrectionRevision();
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
          && Metrics.bTargetRegionTransportValid != 0 ? 1 : 0;
        Metrics.T2LayoutHash = Layout.LayoutHash;
        Metrics.T2RouteDiagnosticHash = Route.StableHash;
        Metrics.T2ProgressHash = Progress.ProgressHash;
        Metrics.T2FlowContractViolationCount = Route.FlowContractViolationCount;
        Metrics.T2FinalDeadlockAgentCount = Route.CorridorFinalDeadlockAgentCount;
        Metrics.T2FlowApproachEnteredCount = Progress.FlowApproachEnteredAgentIds.Num();
        Metrics.T2TransportHandoffCount = Progress.TransportHandoffAgentIds.Num();
        Metrics.T2InsideEffectiveBandCount = Metrics.TargetTransportInsideEffectiveBandCount;
        Metrics.T2FeasibleRegionCount = Metrics.TargetTransportFeasibleRegionCount;
        Metrics.T2FeasibleRegionCoverageCount =
          Metrics.TargetTransportFeasibleRegionCoverageCount;
        Metrics.T2PlanUnroutedCount = Metrics.TargetTransportUnroutedAgentCount;
        Metrics.T2GuidanceUnroutedCount = Metrics.TargetGuidanceUnroutedAgentMax;
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
          if (Agent.CohortId == 0) ++Metrics.T3Cohort0AgentCount;
          else if (Agent.CohortId == 1) ++Metrics.T3Cohort1AgentCount;
          if (Progress.CenterCrossedAgentIds.Contains(Agent.AgentId))
          {
            if (Agent.CohortId == 0) ++Metrics.T3Cohort0CenterCrossedCount;
            else if (Agent.CohortId == 1) ++Metrics.T3Cohort1CenterCrossedCount;
          }
          if (Progress.CompletedAgentIds.Contains(Agent.AgentId))
          {
            if (Agent.CohortId == 0) ++Metrics.T3Cohort0CompletedCount;
            else if (Agent.CohortId == 1) ++Metrics.T3Cohort1CompletedCount;
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
          Pipeline->FindBidirectionalSwapFlowField(0);
        const FCrowdDemoSharedFlowField* Cohort1Field =
          Pipeline->FindBidirectionalSwapFlowField(10);
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
    CheckpointRevision = Result.CheckpointRevision;
    StateFrameRevision = Result.StateFrameRevision;
    Pipeline->MarkRoundResultBuilt(Result.CheckpointRevision);
    Pipeline->EnqueueOutgoingRoundResult(MoveTemp(Result));
  }

  if (bBuildCorrection || bBuildRoundResult)
  {
    FCrowdDemoCorrectionFrame Frame;
    Frame.bValid = 1;
    Frame.FrameKind = bBuildRoundResult
      ? ECrowdDemoRoundFrameKind::RoundResultCheckpoint
      : ECrowdDemoRoundFrameKind::Correction;
    Frame.CorrectionRevision = bBuildRoundResult
      ? StateFrameRevision
      : Pipeline->AllocateCorrectionRevision();
    Frame.RoundId = Pipeline->GetCurrentRoundId();
    Frame.RoundRevision = Pipeline->GetCurrentPlanRevision();
    Frame.SourceCheckpointRevision = CheckpointRevision;
    Frame.ServerTimeSeconds = Pipeline->GetCurrentStepEndServerTimeSeconds();
    Frame.AgentCount = States.Num();
    Frame.AgentStates = States;
    Frame.CrowdState.AgentCount = States.Num();
    Frame.CrowdState.CrowdCenter = FVector_NetQuantize10(Center);
    Frame.CrowdState.CrowdVelocity = FVector_NetQuantize10(Velocity);
    Frame.CrowdState.CrowdYawDegrees = States.IsEmpty() ? 0.0f : States[0].YawDegrees;
    const FCrowdDemoRoundPlanPacket& Plan = Pipeline->GetActivePlan();
    Frame.CrowdState.PlanPhase = FMath::Clamp(
      (Frame.ServerTimeSeconds - Plan.StartServerTimeSeconds) / FMath::Max(0.001f, Plan.DurationSeconds),
      0.0f,
      1.0f);
    Pipeline->EnqueueOutgoingCorrectionFrame(MoveTemp(Frame));
    Pipeline->MarkCorrectionFrameBuilt(Pipeline->GetCurrentStepEndServerTimeSeconds());
  }
  Pipeline->LogStageOnce(
    Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("11_checkpoint_publisher")
      : TEXT("08_checkpoint_publisher"),
    States.Num());
}

UCrowdDemoRoundSimFixedStepPipelineProcessor::UCrowdDemoRoundSimFixedStepPipelineProcessor()
{
  ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
  ProcessingPhase = EMassProcessingPhase::PrePhysics;
  bRequiresGameThreadExecution = true;
  bAutoRegisterWithProcessingPhases = false;
  QueryBasedPruning = EMassQueryBasedPruning::Never;
  ExecutionOrder.ExecuteAfter.Add(TEXT("MassReplicationProcessor"));
  ExecutionOrder.ExecuteBefore.Add(TEXT("CrowdDemoClientVisualMassProcessor"));
  ExecutionOrder.ExecuteBefore.Add(TEXT("CrowdDemoMassVisualStateProcessor"));
}

void UCrowdDemoRoundSimFixedStepPipelineProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UCrowdDemoRoundSimFixedStepPipelineProcessor::InitializeInternal(
  UObject& Owner,
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  Super::InitializeInternal(Owner, EntityManager);
  PlanApplyProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundPlanApplyProcessor>(*this, Owner, EntityManager);
  BoundaryGatherProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundBoundaryGatherProcessor>(*this, Owner, EntityManager);
  RangedCombatProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundRangedCombatProcessor>(*this, Owner, EntityManager);
  HitResponseBoundaryApplyProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundHitResponseBoundaryApplyProcessor>(*this, Owner, EntityManager);
  ReactiveMotionIntentComposeProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundReactiveMotionIntentComposeProcessor>(*this, Owner, EntityManager);
  MovementWorkProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundMovementWorkProcessor>(*this, Owner, EntityManager);
  VisualStateResolveProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundVisualStateResolveProcessor>(*this, Owner, EntityManager);
  OpenSpawnRelaxationPhasePrepareProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor>(*this, Owner, EntityManager);
  TargetFactApplyProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetFactApplyProcessor>(*this, Owner, EntityManager);
  TargetPolarTopologyBuildProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetPolarTopologyBuildProcessor>(*this, Owner, EntityManager);
  TargetRegionPopulationBuildProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetRegionPopulationBuildProcessor>(*this, Owner, EntityManager);
  TargetRegionTransportSolveProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetRegionTransportSolveProcessor>(*this, Owner, EntityManager);
  TargetRegionGuidanceProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetRegionGuidanceProcessor>(*this, Owner, EntityManager);
  SharedFlowFieldBuildProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundSharedFlowFieldBuildProcessor>(*this, Owner, EntityManager);
  FlowPreferredVelocityProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundFlowPreferredVelocityProcessor>(*this, Owner, EntityManager);
  ParticleConstraintProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundParticleConstraintProcessor>(*this, Owner, EntityManager);
  ObstacleConstraintProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundObstacleConstraintProcessor>(*this, Owner, EntityManager);
  FacingResolveProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundFacingResolveProcessor>(*this, Owner, EntityManager);
  MovementFinalizeProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundMovementFinalizeProcessor>(*this, Owner, EntityManager);
  PostFinalizeMetricsProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundPostFinalizeMetricsProcessor>(*this, Owner, EntityManager);
  AuthorityCommitProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundAuthorityCommitProcessor>(*this, Owner, EntityManager);
  ClientPredictionCommitProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundClientPredictionCommitProcessor>(*this, Owner, EntityManager);
  CheckpointPublisherProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundCheckpointPublisherProcessor>(*this, Owner, EntityManager);
}

void UCrowdDemoRoundSimFixedStepPipelineProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!World || !Pipeline)
  {
    return;
  }

  const float TargetServerTime = GetRoundPipelineServerTime(*World);
  const double PipelineFrameStartSeconds = FPlatformTime::Seconds();
  float PipelineFrameStageMilliseconds[
    static_cast<uint8>(ECrowdDemoRoundPerformanceStage::Count)] = {};
  int32 ExecutedSteps = 0;
  bool bHitCatchupCpuBudget = false;
  // A completed round has no next movement step, but its boundary must still be
  // allowed to activate the queued plan. Do not invoke the dynamic processor
  // twice at the same boundary: DebugGame Mass query setup is measurable even
  // when the subsystem gate correctly turns the second call into a no-op.
  bool bPlanApplyExecutedForBoundary = false;
  if (!Pipeline->IsActive())
  {
    PlanApplyProcessor->CallExecute(EntityManager, Context);
    bPlanApplyExecutedForBoundary = true;
  }
  while (ExecutedSteps < MaxFixedStepsPerFrame)
  {
    if (!bPlanApplyExecutedForBoundary)
      PlanApplyProcessor->CallExecute(EntityManager, Context);
    bPlanApplyExecutedForBoundary = false;
    if (!Pipeline->TryBeginFixedStep(TargetServerTime))
    {
      break;
    }
    BoundaryGatherProcessor->CallExecute(EntityManager, Context);
    if (!Pipeline->IsBoundarySnapshotCurrent())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoBoundaryGatherMissing step=%d revision=%d"),
        Pipeline->GetCurrentFixedStepIndex(),
        Pipeline->GetCurrentPlanRevision());
      break;
    }
    const double FixedStepStartSeconds = FPlatformTime::Seconds();
    const auto MeasureStage = [Pipeline, &PipelineFrameStageMilliseconds](
      const ECrowdDemoRoundPerformanceStage Stage, auto&& Work)
    {
      const double StartSeconds = FPlatformTime::Seconds();
      Work();
      const float Milliseconds = static_cast<float>(
        (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
      PipelineFrameStageMilliseconds[static_cast<uint8>(Stage)] += Milliseconds;
      Pipeline->RecordPerformanceStage(Stage, Milliseconds);
    };

    MeasureStage(ECrowdDemoRoundPerformanceStage::BusinessPrepare, [&]
    {
      const bool bRangedCombat = Pipeline->IsRangedProjectileCombat();
      const bool bCombatShowcase = Pipeline->GetRules().Scenario
          == ECrowdDemoScenario::SimRoundSoftPressure
        && Pipeline->GetRules().SoftPressureTestCase
          == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse;
      if (bRangedCombat)
        RangedCombatProcessor->CallExecute(EntityManager, Context);
      if (bRangedCombat || bCombatShowcase)
        HitResponseBoundaryApplyProcessor->CallExecute(EntityManager, Context);
      if (Pipeline->IsOpenSpawnRelaxation())
        OpenSpawnRelaxationPhasePrepareProcessor->CallExecute(EntityManager, Context);
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
        && Pipeline->GetRules().TargetDistanceBandSettings.bEnabled != 0)
      {
        TargetFactApplyProcessor->CallExecute(EntityManager, Context);
      }
    });
    MeasureStage(ECrowdDemoRoundPerformanceStage::SharedFlow, [&]
    {
      SharedFlowFieldBuildProcessor->CallExecute(EntityManager, Context);
      FlowPreferredVelocityProcessor->CallExecute(EntityManager, Context);
    });
    if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && Pipeline->IsTargetRegionExecutionActive())
    {
      MeasureStage(ECrowdDemoRoundPerformanceStage::TargetTopology, [&]
      {
        TargetPolarTopologyBuildProcessor->CallExecute(EntityManager, Context);
      });
      MeasureStage(ECrowdDemoRoundPerformanceStage::TargetDemand, [&]
      {
        TargetRegionPopulationBuildProcessor->CallExecute(EntityManager, Context);
      });
      MeasureStage(ECrowdDemoRoundPerformanceStage::TargetPlan, [&]
      {
        TargetRegionTransportSolveProcessor->CallExecute(EntityManager, Context);
      });
      MeasureStage(ECrowdDemoRoundPerformanceStage::TargetGuidance, [&]
      {
        TargetRegionGuidanceProcessor->CallExecute(EntityManager, Context);
      });
    }
    MeasureStage(ECrowdDemoRoundPerformanceStage::GuidanceCompose, [&]
    {
      if (Pipeline->IsRangedProjectileCombat()
        || (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
          && Pipeline->GetRules().SoftPressureTestCase
            == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse))
      {
        ReactiveMotionIntentComposeProcessor->CallExecute(EntityManager, Context);
      }
    });
    const double MovementWorkStartSeconds = FPlatformTime::Seconds();
    MovementWorkProcessor->CallExecute(EntityManager, Context);
    const float MovementWorkMilliseconds = static_cast<float>(
      (FPlatformTime::Seconds() - MovementWorkStartSeconds) * 1000.0);
    const float GuidanceWorkMilliseconds = FMath::Clamp(
      MovementWorkProcessor->GetLastGuidanceWorkMilliseconds(),
      0.0f, MovementWorkMilliseconds);
    if (GuidanceWorkMilliseconds > 0.0f)
    {
      PipelineFrameStageMilliseconds[static_cast<uint8>(
        ECrowdDemoRoundPerformanceStage::GuidanceCompose)] +=
          GuidanceWorkMilliseconds;
      Pipeline->RecordPerformanceStage(
        ECrowdDemoRoundPerformanceStage::GuidanceCompose,
        GuidanceWorkMilliseconds);
    }
    const float RemainingMovementWorkMilliseconds =
      MovementWorkMilliseconds - GuidanceWorkMilliseconds;
    PipelineFrameStageMilliseconds[static_cast<uint8>(
      ECrowdDemoRoundPerformanceStage::LocalPredictive)] +=
        RemainingMovementWorkMilliseconds;
    Pipeline->RecordPerformanceStage(
      ECrowdDemoRoundPerformanceStage::LocalPredictive,
      RemainingMovementWorkMilliseconds);
    MeasureStage(ECrowdDemoRoundPerformanceStage::Particle, [&]
    {
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
        ParticleConstraintProcessor->CallExecute(EntityManager, Context);
      else
        ObstacleConstraintProcessor->CallExecute(EntityManager, Context);
    });
    MeasureStage(ECrowdDemoRoundPerformanceStage::FacingFinalize, [&]
    {
      FacingResolveProcessor->CallExecute(EntityManager, Context);
      MovementFinalizeProcessor->CallExecute(EntityManager, Context);
      PostFinalizeMetricsProcessor->CallExecute(EntityManager, Context);
      VisualStateResolveProcessor->CallExecute(EntityManager, Context);
    });
    MeasureStage(ECrowdDemoRoundPerformanceStage::Commit, [&]
    {
      if (World->GetNetMode() == NM_Client)
      {
        ClientPredictionCommitProcessor->CallExecute(EntityManager, Context);
      }
      else
      {
        AuthorityCommitProcessor->CallExecute(EntityManager, Context);
        CheckpointPublisherProcessor->CallExecute(EntityManager, Context);
      }
      Pipeline->FinishFixedStep();
    });
    Pipeline->RecordFixedStepPerformance(static_cast<float>(
      (FPlatformTime::Seconds() - FixedStepStartSeconds) * 1000.0));
    ++ExecutedSteps;
    const bool bBacklogRemains = Pipeline->IsActive()
      && Pipeline->GetSimulatedServerTimeSeconds() + Pipeline->GetCurrentFixedStepSeconds()
        <= TargetServerTime + KINDA_SMALL_NUMBER;
    const double PipelineFrameCpuMilliseconds =
      (FPlatformTime::Seconds() - PipelineFrameStartSeconds) * 1000.0;
    if (bBacklogRemains
      && PipelineFrameCpuMilliseconds >= MaxFixedStepCatchupCpuMilliseconds)
    {
      bHitCatchupCpuBudget = true;
      break;
    }
  }
  const bool bHitFixedStepLimit = ExecutedSteps >= MaxFixedStepsPerFrame
    && Pipeline->IsActive()
    && Pipeline->GetSimulatedServerTimeSeconds() + Pipeline->GetCurrentFixedStepSeconds()
      <= TargetServerTime + KINDA_SMALL_NUMBER;
  Pipeline->RecordPipelineFramePerformance(
    ExecutedSteps, TargetServerTime, bHitFixedStepLimit, bHitCatchupCpuBudget);
  if (bHitCatchupCpuBudget)
  {
    ++ConsecutiveCatchupCpuBudgetHitFrames;
    if (ConsecutiveCatchupCpuBudgetHitFrames == 1
      || ConsecutiveCatchupCpuBudgetHitFrames % 30 == 0)
    {
      const double PipelineFrameCpuMilliseconds =
        (FPlatformTime::Seconds() - PipelineFrameStartSeconds) * 1000.0;
      const float BacklogMilliseconds = FMath::Max(
        0.0f, TargetServerTime - Pipeline->GetSimulatedServerTimeSeconds()) * 1000.0f;
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoCatchupBudget role=%s steps=%d cpu_ms=%.3f backlog_ms=%.3f consecutive=%d stages_ms=[business=%.3f flow=%.3f topology=%.3f demand=%.3f plan=%.3f guidance=%.3f compose=%.3f local=%.3f particle=%.3f facing_finalize=%.3f commit=%.3f] source=MassPipeline"),
        World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
        ExecutedSteps, PipelineFrameCpuMilliseconds, BacklogMilliseconds,
        ConsecutiveCatchupCpuBudgetHitFrames,
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::BusinessPrepare)],
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::SharedFlow)],
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::TargetTopology)],
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::TargetDemand)],
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::TargetPlan)],
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::TargetGuidance)],
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::GuidanceCompose)],
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::LocalPredictive)],
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::Particle)],
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::FacingFinalize)],
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::Commit)]);
    }
  }
  else
  {
    ConsecutiveCatchupCpuBudgetHitFrames = 0;
  }
}

#undef ROUND_DYNAMIC_FLAGS
