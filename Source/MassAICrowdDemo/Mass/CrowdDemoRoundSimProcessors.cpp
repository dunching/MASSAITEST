#include "Mass/CrowdDemoRoundSimProcessors.h"
#include "Mass/CrowdDemoRoundInitialStateKernel.h"

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
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"

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
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoParticlePropertiesFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassStatsFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoBusinessStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRangedAttackFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoHitFlashFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassAgentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassSimulationStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassPropertiesFragment>(EMassFragmentAccess::ReadWrite);
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
        if (DuePlan.Rules.SoftPressureTestCase
            == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse
          || DuePlan.Rules.SoftPressureTestCase
            == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat)
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
    const FCrowdDemoSoftPressureRollbackSnapshot* SoftPressureRollbackSnapshot =
      Pipeline->FindSoftPressureRollbackSnapshot(DiagnosticCorrectionFixedStep);
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
    Pipeline->RecordSoftPressureRollbackOutcome(
      bValidSoftPressureRollback, bSoftPressureAgentMismatch,
      CorrectionReplayStepCount);
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
    Pipeline->BeginRollbackReplayPerformance(
      CorrectionReplayStepCount,
      static_cast<float>(
        (FPlatformTime::Seconds() - CorrectionApplyStartSeconds) * 1000.0),
      bZeroErrorCorrection);
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
  if (!Pipeline->StageBoundarySharedFlowWork(WorkInput))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoSharedFlowWorkStageRejected step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
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
  EntityQuery.AddRequirement<FCrowdMassAgentFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdMassBehaviorFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassMovementFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoParticlePropertiesFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdMassFacingFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassStatsFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoBusinessStateFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRangedAttackFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoHitFlashFragment>(
    EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassVisualFragment>(
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
  const double GatherStartSeconds = FPlatformTime::Seconds();
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
      BusinessFact.Stats = Stats[It];
      BusinessFact.Business = Businesses[It];
      BusinessFact.Attack = Attacks[It];
      BusinessFact.ReactiveMotion = Reactives[It];
      BusinessFact.HitFlash = HitFlashes[It];
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
      TEXT("VIOLATION CrowdDemoBoundaryGatherInvalid step=%d records=%d"),
      Pipeline->GetCurrentFixedStepIndex(), Records.Num());
    return;
  }
  const double GatherMilliseconds =
    (FPlatformTime::Seconds() - GatherStartSeconds) * 1000.0;
  if (!Pipeline->BeginBoundaryTransaction(GatherMilliseconds))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBoundaryOrchestratorBeginFailed step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
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
      FCrowdMassTargetRegionTopologyInput BoundaryWorkInput;
      BoundaryWorkInput.Settings =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionSettings(
          Settings);
      BoundaryWorkInput.FlowConfig =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(
          Pipeline->GetRules().FlowFieldConfig);
      if (!Pipeline->StageBoundaryTargetTopologyWork(
          Runtime.Cohort.CapabilityProfileKey, BoundaryWorkInput))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoTargetTopologyStageRejected step=%d profile_key=%u"),
          Pipeline->GetCurrentFixedStepIndex(),
          Runtime.Cohort.CapabilityProfileKey);
        return;
      }
      if (Pipeline->IsBoundarySnapshotCurrent()) continue;
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
  FCrowdMassTargetRegionTopologyInput BoundaryWorkInput;
  BoundaryWorkInput.Settings =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionSettings(Settings);
  BoundaryWorkInput.FlowConfig =
    FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(
      Pipeline->GetRules().FlowFieldConfig);
  if (!Pipeline->StageBoundaryTargetTopologyWork(0, BoundaryWorkInput))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetTopologyStageRejected step=%d profile_key=0"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
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
          &Pipeline->GetRuntimeSharedFlowField(), Runtime.Topology,
          Runtime.Demand, true, bRefreshSourceAttachments,
          &DemandBoundaryInput, false);
        Pipeline->RecordTargetDemandPerformance(false);
      }
      else
      {
        RunTargetRegionDemandWork(
          Runtime.Agents, ExternalAgents, Settings,
          Pipeline->GetRules().FlowFieldConfig,
          &Pipeline->GetRuntimeSharedFlowField(), Runtime.Topology,
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
      &Pipeline->GetRuntimeSharedFlowField(),
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetPreparedTargetRegionDemand(), true,
      bRefreshSourceAttachments, &DemandBoundaryInput, false);
    Pipeline->RecordTargetDemandPerformance(false);
  }
  else
  {
    RunTargetRegionDemandWork(
      Agents, {}, Settings, Pipeline->GetRules().FlowFieldConfig,
      &Pipeline->GetRuntimeSharedFlowField(),
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

UCrowdDemoRoundTargetRegionGuidanceProcessor::
UCrowdDemoRoundTargetRegionGuidanceProcessor()
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundTargetRegionGuidanceProcessor::Execute(
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


UCrowdDemoRoundCombatBoundaryProcessor::UCrowdDemoRoundCombatBoundaryProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundCombatBoundaryProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UCrowdDemoRoundCombatBoundaryProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive()) return;
  const bool bProjectileCombat = Pipeline->IsRangedProjectileCombat();
  const bool bShowcase = Pipeline->GetRules().Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure
    && Pipeline->GetRules().SoftPressureTestCase
      == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse;
  if (!bProjectileCombat && !bShowcase) return;

  TArray<FCrowdDemoRangedCombatAgent> Agents;
  TMap<int32, FVector> LocalOffsetByAgentId;
  TMap<int32, float> YawByAgentId;
  const FCrowdMassBoundarySnapshot& BoundarySnapshot =
    Pipeline->GetBoundarySnapshot();
  const TArray<FCrowdDemoRoundBoundaryBusinessFact>& BusinessFacts =
    Pipeline->GetBoundaryBusinessFacts();
  if (!Pipeline->IsBoundarySnapshotCurrent()
    || BusinessFacts.Num() != BoundarySnapshot.Agents.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoCombatBoundaryOverlayMissing step=%d facts=%d expected=%d"),
      Pipeline->GetCurrentFixedStepIndex(), BusinessFacts.Num(),
      BoundarySnapshot.Agents.Num());
    return;
  }
  Agents.Reserve(BusinessFacts.Num());
  for (int32 Index = 0; Index < BusinessFacts.Num(); ++Index)
  {
    const FCrowdDemoRoundBoundaryBusinessFact& Fact = BusinessFacts[Index];
    const FCrowdMassBoundaryAgentRecord& Base = BoundarySnapshot.Agents[Index];
    if (Fact.EntityRef != Base.AgentFacts.StableEntityRef
      || Fact.AgentId != Base.Identity.AgentId)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoCombatBoundaryOverlayMismatch step=%d index=%d"),
        Pipeline->GetCurrentFixedStepIndex(), Index);
      return;
    }
    FCrowdDemoMassIdentityFragment Identity;
    Identity.Id = Fact.AgentId;
    Identity.LifecycleSerial =
      static_cast<int32>(Fact.EntityRef.LifecycleSerial);
    FCrowdDemoRangedCombatAgent& Agent = Agents.AddDefaulted_GetRef();
    Agent.AgentId = Fact.AgentId;
    Agent.LifecycleSerial = Identity.LifecycleSerial;
    Agent.FormationIndex = Fact.FormationIndex;
    Agent.Position = Base.State.Position;
    Agent.Velocity = Base.State.Velocity;
    Agent.RadiusCm = Fact.RadiusCm;
    Agent.bAlive = Fact.Stats.bAlive;
    Agent.Combat = MakeCombatAgentState(
      Identity, Fact.Stats, Fact.Business, Fact.Attack,
      Fact.ReactiveMotion, Fact.HitFlash, Fact.Visual);
    LocalOffsetByAgentId.Add(Agent.AgentId, Fact.LocalOffset);
    YawByAgentId.Add(Agent.AgentId, Fact.YawDegrees);
  }
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  bool bAgentSetValid = Agents.Num() == Pipeline->GetBoundarySnapshot().Agents.Num();
  for (int32 Index = 1; Index < Agents.Num(); ++Index)
    bAgentSetValid &= Agents[Index - 1].AgentId != Agents[Index].AgentId;
  if (!bAgentSetValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoCombatBoundaryGatherInvalid step=%d agents=%d expected=%d"),
      Pipeline->GetCurrentFixedStepIndex(), Agents.Num(),
      Pipeline->GetBoundarySnapshot().Agents.Num());
    return;
  }

  const int32 FixedStep = Pipeline->GetCurrentFixedStepIndex();
  if (bShowcase && FixedStep == 0)
  {
    for (FCrowdDemoRangedCombatAgent& Agent : Agents)
    {
      if (Agent.Combat.BusinessStateRevision != 0) continue;
      Agent.Combat.BusinessState = Agent.FormationIndex < 4
        ? ECrowdDemoBusinessState::Idle
        : Agent.FormationIndex < 8
          ? ECrowdDemoBusinessState::Moving
          : Agent.FormationIndex < 12
            ? ECrowdDemoBusinessState::Attacking
            : ECrowdDemoBusinessState::Idle;
      Agent.Combat.AttackPhase =
        Agent.Combat.BusinessState == ECrowdDemoBusinessState::Attacking
          ? ECrowdDemoAttackPhase::Windup : ECrowdDemoAttackPhase::None;
      Agent.Combat.BusinessStateRevision = 1;
      Agent.Combat.BusinessStateEnterFixedStep = 0;
    }
  }

  TArray<FCrowdDemoHitFact> HitFacts;
  FCrowdDemoProjectileStepSummary ProjectileSummary;
  TArray<FCrowdDemoProjectileVisualEvent> ProjectileEvents;
  TArray<FCrowdDemoProjectileState> NextProjectiles;
  if (!Pipeline->BuildProjectileSnapshot(NextProjectiles))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMassProjectileGatherFailed step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  if (bProjectileCombat)
  {
    TArray<FCrowdDemoProjectileSpawnRequest> Requests;
    const FCrowdDemoRangedCombatSettings& ProjectileSettings =
      Pipeline->GetRules().RangedCombatSettings;
    FCrowdDemoProjectileKernel::AdvanceAttackPhases(
      Pipeline->GetCurrentRoundId(), FixedStep, ProjectileSettings,
      Agents, Requests, ProjectileSummary);
    FCrowdDemoProjectileKernel::SpawnProjectiles(
      FixedStep, Pipeline->GetCurrentStepEndServerTimeSeconds(),
      ProjectileSettings, Requests, NextProjectiles,
      ProjectileEvents, ProjectileSummary);
    TArray<FCrowdImpactFact> Impacts;
    TArray<FCrowdProjectileEnvironmentBody> EnvironmentBodies;
    const FCrowdDemoFlowObstacleCollisionSnapshotProvider
      EnvironmentProvider(
        Pipeline->GetRules().FlowFieldConfig);
    if (!EnvironmentProvider.Gather(
        FixedStep, EnvironmentBodies))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoProjectileEnvironmentGatherFailed step=%d"),
        FixedStep);
      return;
    }
    FCrowdDemoProjectileKernel::AdvanceProjectiles(
      FixedStep, Pipeline->GetCurrentStepEndServerTimeSeconds(),
      Pipeline->GetCurrentFixedStepSeconds(), ProjectileSettings, Agents,
      EnvironmentBodies, NextProjectiles, Impacts,
      ProjectileEvents, ProjectileSummary);
    TArray<FCrowdHitFact> ResolvedHits;
    const FCrowdDemoHostHitResolver HitResolver(ProjectileSettings);
    if (!HitResolver.Resolve(Impacts, ResolvedHits)
      || !FCrowdDemoHostHitResolver::BuildDemoHitFacts(
        ResolvedHits, HitFacts))
      ProjectileSummary.bValid = false;
  }

  if (bShowcase)
  {
    for (const FCrowdDemoRangedCombatAgent& Agent : Agents)
    {
      const bool bKnockback = FixedStep == 30
        && Agent.FormationIndex >= 12 && Agent.FormationIndex < 14;
      const bool bKnockUp = FixedStep == 60
        && Agent.FormationIndex >= 14 && Agent.FormationIndex < 16;
      const bool bDeath = FixedStep == 90 && Agent.FormationIndex >= 16;
      if (!bKnockback && !bKnockUp && !bDeath) continue;
      FCrowdDemoHitFact& Fact = HitFacts.AddDefaulted_GetRef();
      Fact.HitEventId = (static_cast<uint64>(Pipeline->GetCurrentRoundId()) << 32)
        | (static_cast<uint64>(FixedStep) << 16)
        | static_cast<uint32>(Agent.AgentId);
      Fact.ApplyFixedStep = FixedStep;
      Fact.TargetAgentId = Agent.AgentId;
      Fact.TargetLifecycleSerial = Agent.Combat.LifecycleSerial;
      Fact.HitPosition = Agent.Position;
      Fact.HitDirection = FVector::ForwardVector;
      Fact.Damage = bDeath ? 1000.0f : 10.0f;
      Fact.HorizontalImpulseCmps = bKnockback ? 500.0f : 0.0f;
      Fact.VerticalImpulseCmps = bKnockUp ? 650.0f : 0.0f;
      Fact.HitFlashProfileKey = 1;
    }
  }

  TArray<FCrowdDemoCombatAgentState> CombatStates;
  CombatStates.Reserve(Agents.Num());
  for (const FCrowdDemoRangedCombatAgent& Agent : Agents)
    CombatStates.Add(Agent.Combat);
  FCrowdDemoHitResponseSettings HitSettings;
  HitSettings.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
  FCrowdDemoHitResponseSummary HitSummary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(
    FixedStep, Pipeline->GetCurrentStepEndServerTimeSeconds(), HitFacts,
    HitSettings, CombatStates, HitSummary);
  TMap<int32, const FCrowdDemoCombatAgentState*> CombatByAgentId;
  for (const FCrowdDemoCombatAgentState& Combat : CombatStates)
    CombatByAgentId.Add(Combat.AgentId, &Combat);
  for (FCrowdDemoRangedCombatAgent& Agent : Agents)
    if (const FCrowdDemoCombatAgentState* const* Combat =
      CombatByAgentId.Find(Agent.AgentId))
      Agent.Combat = **Combat;

  TArray<FCrowdDemoPreparedReactiveMotionStep> PreparedReactiveSteps;
  PreparedReactiveSteps.Reserve(Agents.Num());
  TArray<FCrowdGuidanceCandidate> PreparedBusinessCandidates;
  for (FCrowdDemoRangedCombatAgent& Agent : Agents)
  {
    const float YawDegrees = YawByAgentId.FindRef(Agent.AgentId);
    FCrowdDemoGuidanceCandidate BusinessCandidate;
    if (bProjectileCombat)
    {
      BusinessCandidate = FCrowdDemoGuidanceComposeKernel::BuildCandidate(
        Agent.AgentId, ECrowdDemoGuidanceProvider::BusinessOverride,
        Pipeline->GetCurrentPlanRevision(), FVector::ZeroVector,
        Agent.Position, YawDegrees, true);
    }
    else
    {
      const FVector Anchor = FVector(Pipeline->GetRules().SpawnOrigin)
        + LocalOffsetByAgentId.FindRef(Agent.AgentId);
      const FCrowdDemoVatShowcaseMotionResult Showcase =
        FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
          Agent.FormationIndex, FixedStep, Agent.Position, Anchor);
      if (Showcase.bValid)
      {
        BusinessCandidate = FCrowdDemoGuidanceComposeKernel::BuildCandidate(
          Agent.AgentId, ECrowdDemoGuidanceProvider::BusinessOverride,
          Pipeline->GetCurrentPlanRevision(), Showcase.DesiredVelocity,
          Showcase.DesiredLocation,
          Showcase.DesiredVelocity.IsNearlyZero()
            ? YawDegrees : Showcase.DesiredVelocity.Rotation().Yaw,
          true);
      }
    }
    const FCrowdDemoReactiveMotionStepResult StepResult =
      FCrowdDemoCombatStateKernel::AdvanceReactiveMotion(
        FixedStep, Agent.Position.Z, HitSettings, Agent.Combat);
    FCrowdDemoPreparedReactiveMotionStep ReactiveStep;
    ReactiveStep.AgentId = Agent.AgentId;
    ReactiveStep.LifecycleSerial = Agent.LifecycleSerial;
    if (!Agent.Combat.bAlive)
    {
      BusinessCandidate = FCrowdDemoGuidanceComposeKernel::BuildCandidate(
        Agent.AgentId, ECrowdDemoGuidanceProvider::BusinessOverride,
        Pipeline->GetCurrentPlanRevision(), FVector::ZeroVector,
        Agent.Position, YawDegrees, true);
    }
    else if (StepResult.bValid
      && Agent.Combat.ReactiveMode != ECrowdDemoReactiveMotionMode::None)
    {
      const FVector ReactiveVelocity(
        StepResult.HorizontalVelocity.X, StepResult.HorizontalVelocity.Y, 0.0f);
      const FVector DesiredLocation = BusinessCandidate.bValid
        ? BusinessCandidate.DesiredLocation : Agent.Position;
      BusinessCandidate = FCrowdDemoGuidanceComposeKernel::BuildCandidate(
        Agent.AgentId, ECrowdDemoGuidanceProvider::BusinessOverride,
        Pipeline->GetCurrentPlanRevision(), ReactiveVelocity, DesiredLocation,
        ReactiveVelocity.IsNearlyZero()
          ? YawDegrees : ReactiveVelocity.Rotation().Yaw,
        true);
      ReactiveStep.bActive = true;
      ReactiveStep.ProposedZ = StepResult.NewZ;
      ReactiveStep.VerticalVelocityCmps = StepResult.NewVerticalVelocityCmps;
    }
    PreparedReactiveSteps.Add(ReactiveStep);
    if (BusinessCandidate.bValid)
      PreparedBusinessCandidates.Add(
        FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreGuidanceCandidate(
          BusinessCandidate));
  }

  PreparedBusinessCandidates.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  Pipeline->SetPreparedBusinessGuidanceCandidates(
    MoveTemp(PreparedBusinessCandidates));
  Pipeline->SetPreparedReactiveMotionSteps(MoveTemp(PreparedReactiveSteps));
  uint64 StableHash = 14695981039346656037ull;
  const auto FoldCombat = [&StableHash](const uint64 Value)
  {
    for (int32 Shift = 0; Shift < 64; Shift += 8)
    {
      StableHash ^= static_cast<uint8>(Value >> Shift);
      StableHash *= 1099511628211ull;
    }
  };
  FoldCombat(1);
  FoldCombat(static_cast<uint32>(FixedStep));
  FoldCombat(static_cast<uint32>(Pipeline->GetCurrentPlanRevision()));
  for (const FCrowdDemoRangedCombatAgent& Agent : Agents)
  {
    FoldCombat(static_cast<uint32>(Agent.AgentId));
    FoldCombat(static_cast<uint32>(Agent.LifecycleSerial));
    FoldCombat(static_cast<uint32>(Agent.Combat.BusinessStateRevision));
    FoldCombat(static_cast<uint32>(Agent.Combat.ReactiveRevision));
    FoldCombat(Agent.Combat.LastConsumedHitEventId);
  }
  FoldCombat(ProjectileSummary.AttackStateHash);
  FoldCombat(ProjectileSummary.ProjectileStateHash);
  FoldCombat(ProjectileSummary.EventHash);
  FoldCombat(static_cast<uint32>(HitSummary.AppliedHitCount));
  FoldCombat(static_cast<uint32>(HitSummary.DuplicateHitCount));

  FCrowdDemoPreparedCombatBoundaryCommit Commit;
  Commit.FixedStepIndex = FixedStep;
  Commit.PlanRevision = Pipeline->GetCurrentPlanRevision();
  Commit.Agents = MoveTemp(Agents);
  Commit.Projectiles = MoveTemp(NextProjectiles);
  Commit.ProjectileEvents = MoveTemp(ProjectileEvents);
  Commit.ProjectileSummary = ProjectileSummary;
  Commit.HitSummary = HitSummary;
  Commit.StableHash = StableHash;
  Commit.bProjectileCombat = bProjectileCombat;
  Commit.bValid = StableHash != 0;
  if (!Pipeline->SetPreparedCombatBoundaryCommit(MoveTemp(Commit)))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoCombatBoundaryPrepareRejected step=%d"),
      FixedStep);
  }
}

UCrowdDemoRoundParticleConstraintProcessor::UCrowdDemoRoundParticleConstraintProcessor()
{
  ROUND_DYNAMIC_FLAGS;
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
  const bool bBuildingWorkerTemplate = PredictedByAgentId.IsEmpty();
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
    if (!bBuildingWorkerTemplate && (!Predicted || !(*Predicted)->bValid))
    {
      bGatherValid = false;
      continue;
    }
    bool bParticleActive = true;
    FVector StartPosition = Base.State.Position;
    FVector PredictedPosition = Base.State.Position;
    if (Predicted)
    {
      bParticleActive = (*Predicted)->bParticleActive;
      StartPosition = (*Predicted)->StartPosition;
      PredictedPosition = (*Predicted)->PredictedPosition;
    }
    else if (Pipeline->IsOpenSpawnRelaxation())
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
  ParticlePipelineInput.PredictedMovements =
    Pipeline->GetPreparedRuntimePredictedMovements();
  ParticlePipelineInput.ExpectedExternalAgentCount =
    bHasTargetParticle ? 1 : 0;
  const double StartSeconds = FPlatformTime::Seconds();
  FCrowdMassParticlePipelineWorkOutput ParticlePipelineOutput;
  if (!Pipeline->ConsumeBoundaryParticleWork(ParticlePipelineOutput))
  {
    if (!Pipeline->StageBoundaryParticleWork(MoveTemp(ParticlePipelineInput)))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoParticleWorkStageRejected step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
    }
    return;
  }
  const FCrowdMassParticleWorkOutput& WorkOutput =
    ParticlePipelineOutput.Particle;
  const FCrowdMassParticlePublishPlan& PublishPlan =
    ParticlePipelineOutput.PublishPlan;
  const float SolverMilliseconds = static_cast<float>(
    (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
  if (!ParticlePipelineOutput.bCompleted || !WorkOutput.bCompleted
    || !PublishPlan.bValid)
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
  TMap<int32, const FCrowdMassParticlePublishRecord*> PublishByAgentId;
  int32 ActiveEntityCount = 0;
  for (const FCrowdMassParticlePublishRecord& Record : PublishPlan.Records)
  {
    PublishByAgentId.Add(Record.AgentId, &Record);
    if (Record.bParticleActive) ++ActiveEntityCount;
  }
  TMap<int32, int32> TraceIndexByAgentId;
  if (Settings.bCaptureRouteDiagnostic)
    for (int32 TraceIndex = 0; TraceIndex < Trace.AgentIds.Num(); ++TraceIndex)
      TraceIndexByAgentId.Add(Trace.AgentIds[TraceIndex], TraceIndex);
  bool bResultIdentityValid = PublishByAgentId.Num()
    == Pipeline->GetBoundarySnapshot().Agents.Num();
  if (!bResultIdentityValid
    || ActiveEntityCount + (bHasTargetParticle ? 1 : 0) != Results.Num()
    || PublishPlan.FinalKinematics.Num()
      != Pipeline->GetBoundarySnapshot().Agents.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoParticleRuntimeIdentityInvalid step=%d active=%d results=%d target=%d"),
      Pipeline->GetCurrentFixedStepIndex(), ActiveEntityCount, Results.Num(),
      bHasTargetParticle ? 1 : 0);
    return;
  }
  TArray<FCrowdParticleConstraintResult> PreparedRuntimeParticleResults =
    PublishPlan.PreparedResults;
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
  TMap<int32, const FCrowdMassSharedFlowAgentOutput*> FlowByAgentId;
  for (const FCrowdMassSharedFlowAgentOutput& Value
    : Pipeline->GetPreparedRuntimeSharedFlowOutputs())
    FlowByAgentId.Add(Value.AgentId, &Value);

  for (const FCrowdMassParticlePublishRecord& Publish : PublishPlan.Records)
  {
    const FCrowdMassPredictedMovement* const* Predicted =
      PredictedByAgentId.Find(Publish.AgentId);
    if (!Predicted)
    {
      bResultIdentityValid = false;
      continue;
    }
    if (Publish.bAppliedStateSample)
    {
      FCrowdDemoParticleAppliedState& Applied =
        AppliedStates.AddDefaulted_GetRef();
      Applied.AgentId = Publish.AgentId;
      Applied.Position = Publish.Result.CorrectedPosition;
      Applied.Velocity = Publish.Result.CorrectedVelocity;
    }
    if (!Publish.bUsedSolverResult)
      continue;

    if (bRouteDiagnostic)
    {
      const FCrowdMassSharedFlowAgentOutput* const* Flow =
        FlowByAgentId.Find(Publish.AgentId);
      if (!Flow)
      {
        bResultIdentityValid = false;
      }
      else
      {
        const FCrowdDemoSharedFlowSample FlowSample =
          FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowSample(
            (*Flow)->Sample);
        FCrowdDemoSoftPressureRouteStepSample& Route =
          RouteSamples.AddDefaulted_GetRef();
        Route.AgentId = Publish.AgentId;
        Route.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
        Route.PredictStartLocation = (*Predicted)->StartPosition;
        Route.Location = Publish.Result.CorrectedPosition;
        Route.Goal = FVector(Pipeline->GetRules().FlowFieldConfig.GoalLocation);
        Route.FlowCellIndex = FlowSample.CellIndex;
        Route.FlowStableCellKey = FlowSample.StableCellKey;
        Route.FlowStatus = FlowSample.Status;
        Route.IntegrationCost = FlowSample.IntegrationCost;
        Route.FlowDirection = FlowSample.FlowDirection;
        if (const FCrowdComposedGuidance* const* Composed =
          ComposedByAgentId.Find(Route.AgentId))
          Route.DesiredVelocity = (*Composed)->AutonomousPreferredVelocity;
        Route.PredictedVelocity = (*Predicted)->Velocity;
        Route.AppliedVelocity = Publish.Result.CorrectedVelocity;
        Route.TotalParticleCorrection = Publish.Result.RealizedCorrection;
        Route.FixedStepSeconds = Settings.FixedStepSeconds;
        Route.MaxSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
        Route.bFlowGuidanceOwner = !Pipeline->IsOpenCohortMovement();
        if (const int32* TraceIndex = TraceIndexByAgentId.Find(Route.AgentId))
        {
          if (Trace.PairSoftRequestedCorrections.IsValidIndex(*TraceIndex))
            Route.PairSoftRequestedCorrection =
              Trace.PairSoftRequestedCorrections[*TraceIndex];
          if (Trace.PairSoftRealizedCorrections.IsValidIndex(*TraceIndex))
            Route.PairSoftRealizedCorrection =
              Trace.PairSoftRealizedCorrections[*TraceIndex];
          if (Trace.EnvironmentSoftRequestedCorrections.IsValidIndex(*TraceIndex))
            Route.EnvironmentSoftRequestedCorrection =
              Trace.EnvironmentSoftRequestedCorrections[*TraceIndex];
          if (Trace.EnvironmentSoftRealizedCorrections.IsValidIndex(*TraceIndex))
            Route.EnvironmentSoftRealizedCorrection =
              Trace.EnvironmentSoftRealizedCorrections[*TraceIndex];
          if (Trace.UnifiedHardCorrections.IsValidIndex(*TraceIndex))
            Route.UnifiedHardCorrection =
              Trace.UnifiedHardCorrections[*TraceIndex];
          if (Trace.ActiveNeighborAgentIds.IsValidIndex(*TraceIndex))
            Route.ActiveNeighborAgentIds =
              Trace.ActiveNeighborAgentIds[*TraceIndex];
        }
        FlowDirectionByAgentId.Add(Route.AgentId, Route.FlowDirection);
      }
    }
    if (bStabilityDiagnostic)
    {
      FCrowdDemoTargetStabilityAgentSample& Sample =
        StabilityStep.Agents.AddDefaulted_GetRef();
      Sample.AgentId = Publish.AgentId;
      Sample.CohortKey = CapabilityProfileKeyByAgentId.FindRef(Sample.AgentId);
      Sample.Location = FVector2f(Publish.Result.CorrectedPosition.X,
        Publish.Result.CorrectedPosition.Y);
      Sample.Velocity = FVector2f(Publish.Result.CorrectedVelocity.X,
        Publish.Result.CorrectedVelocity.Y);
      Sample.AppliedVelocity = Sample.Velocity;
      Sample.TargetLocation = Pipeline->GetTargetFact().Location;
      Sample.TargetVelocity = Pipeline->GetTargetFact().Velocity;
      Sample.TotalParticleCorrection = FVector2f(
        Publish.Result.RealizedCorrection.X,
        Publish.Result.RealizedCorrection.Y);
      Sample.PredictedVelocity = FVector2f(
        (*Predicted)->Velocity.X, (*Predicted)->Velocity.Y);
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
      if (const auto* const* Guidance =
        StabilityGuidanceByAgentId.Find(Sample.AgentId))
      {
        Sample.CurrentCellKey = (*Guidance)->CurrentCellKey;
        Sample.NextCellKey = (*Guidance)->NextCellKey;
        Sample.CurrentRegionKey = (*Guidance)->DemandRegionKey;
        Sample.GuidanceMode = (*Guidance)->Mode;
        Sample.DesiredVelocity = (*Guidance)->DesiredVelocity;
      }
      else if (const FCrowdComposedGuidance* const* Composed =
        ComposedByAgentId.Find(Sample.AgentId))
      {
        Sample.DesiredVelocity = FVector2f(
          (*Composed)->AutonomousPreferredVelocity.X,
          (*Composed)->AutonomousPreferredVelocity.Y);
      }
      if (const auto* const* Demand =
        StabilityDemandByAgentId.Find(Sample.AgentId))
      {
        Sample.CurrentRegionKey = (*Demand)->CurrentRegionKey;
        Sample.bTerminal = (*Demand)->bTerminal;
        Sample.bTerminalStay = (*Demand)->bTerminalStay;
        Sample.bSupply = (*Demand)->bSupply;
        Sample.RegionSurplusCount =
          StabilitySurplusByAgentId.FindRef(Sample.AgentId);
      }
      if (const int32* TraceIndex =
        TraceIndexByAgentId.Find(Sample.AgentId))
        if (Trace.PairSoftRealizedCorrections.IsValidIndex(*TraceIndex))
          Sample.PairSoftCorrection = FVector2f(
            Trace.PairSoftRealizedCorrections[*TraceIndex].X,
            Trace.PairSoftRealizedCorrections[*TraceIndex].Y);
    }
  }
  if (!bResultIdentityValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoParticlePreparedDiagnosticInputInvalid step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }

  PreparedRuntimeParticleResults.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  Pipeline->SetPreparedRuntimeFinalKinematics(
    TArray<FCrowdMassFinalKinematicState>(PublishPlan.FinalKinematics));
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
  FCrowdDemoPreparedParticleDiagnosticCommit DiagnosticCommit;
  DiagnosticCommit.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  DiagnosticCommit.PlanRevision = Pipeline->GetCurrentPlanRevision();
  DiagnosticCommit.AppliedStateHash = AppliedStateHash;
  DiagnosticCommit.SolverMilliseconds = SolverMilliseconds;
  DiagnosticCommit.AgentCount = Pipeline->GetBoundarySnapshot().Agents.Num();
  if (bStabilityDiagnostic)
  {
    StabilityStep.ParticleSoftErrorCmP95 = AppliedSummary.SoftErrorCmP95;
    StabilityStep.ParticleMaxActualCorrectionCm = AppliedSummary.MaxAgentCorrectionCm;
    DiagnosticCommit.bRecordStabilityStep = true;
    DiagnosticCommit.bFinalizeStabilityDiagnostic =
      Pipeline->ShouldBuildRoundResult();
    DiagnosticCommit.StabilityStep = MoveTemp(StabilityStep);
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
    DiagnosticCommit.bRecordCrossProfileViolations = true;
    DiagnosticCommit.CrossProfileHardViolationCount =
      CrossProfileHardViolations;
    DiagnosticCommit.CrossProfileSweptViolationCount =
      CrossProfileSweptViolations;
  }
  if (bRouteDiagnostic)
  {
    DiagnosticCommit.bRecordRouteStep = true;
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
      FCrowdDemoSoftPressureRouteDiagnosticRuntime ProjectedRouteRuntime =
        Pipeline->GetSoftPressureRouteDiagnosticRuntime();
      FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep(
        RouteSamples, ProjectedRouteRuntime);
      for (const auto& Agent : ProjectedRouteRuntime.Agents)
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
      DiagnosticCommit.bFinalizeRouteDiagnostic = true;
      DiagnosticCommit.RouteCounterfactual = Counterfactual;
    }
    DiagnosticCommit.RouteSamples = MoveTemp(RouteSamples);
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
    DiagnosticCommit.bRecordOpenSpawnStep = true;
    DiagnosticCommit.OpenSpawnSoftPairInfluences =
      Trace.SoftPairInfluences;
    DiagnosticCommit.OpenSpawnMaxAgentCorrectionCm =
      AppliedSummary.MaxAgentCorrectionCm;
    DiagnosticCommit.OpenSpawnSoftErrorCmP95 =
      AppliedSummary.SoftErrorCmP95;
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
    DiagnosticCommit.bRecordFailureFixture = true;
    DiagnosticCommit.FailureFixture = MoveTemp(Fixture);
  }
  DiagnosticCommit.CandidateSummary = Summary;
  DiagnosticCommit.AppliedSummary = AppliedSummary;
  DiagnosticCommit.bValid = true;
  if (!Pipeline->SetPreparedParticleDiagnosticCommit(
    MoveTemp(DiagnosticCommit)))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoParticleDiagnosticCommitPrepareInvalid step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }
  Pipeline->LogStageOnce(TEXT("05_particle_constraint"), Agents.Num());
}

UCrowdDemoRoundObstacleConstraintProcessor::UCrowdDemoRoundObstacleConstraintProcessor()
{
  ROUND_DYNAMIC_FLAGS;
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

UCrowdDemoRoundFacingFinalizeProcessor::UCrowdDemoRoundFacingFinalizeProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundFacingFinalizeProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassAgentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdMassSimulationStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassMovementOutputFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdMassFacingFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassMovementFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(
    EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassStatsFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoBusinessStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRangedAttackFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoHitFlashFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
  EntityQuery.AddTagRequirement<FCrowdMassAgentTag>(EMassFragmentPresence::All);
}

bool UCrowdDemoRoundFacingFinalizeProcessor::ApplyPreparedCommit(
  UCrowdDemoRoundSimPipelineSubsystem& Pipeline,
  FMassExecutionContext& Context)
{
  if (!Pipeline.IsPreparedMovementBoundaryCommitCurrent()
    || !Pipeline.GetPreparedPostFinalizeAgentRecords().IsEmpty()
    || !Pipeline.GetPreparedCheckpointAgentStates().IsEmpty())
    return false;

  const FCrowdDemoPreparedMovementBoundaryCommit& Prepared =
    Pipeline.GetPreparedMovementBoundaryCommit();
  const FCrowdMassFacingWorkOutput& WorkOutput = Prepared.Facing;
  const FCrowdMassMovementFinalizeWorkOutput& FinalizeOutput =
    Prepared.Finalize;
  TMap<int32, const FCrowdFacingResult*> ById;
  for (const FCrowdFacingResult& Result : WorkOutput.Summary.Results)
    ById.Add(Result.AgentId, &Result);
  TMap<int32, const FCrowdMassCommitRecord*> CommitByAgentId;
  for (const FCrowdMassCommitRecord& Record
    : FinalizeOutput.CommitPlan.Records)
    CommitByAgentId.Add(Record.Movement.AgentId, &Record);
  TMap<int32, const FCrowdDemoRoundBoundaryFormationFact*> FormationByAgentId;
  for (const FCrowdDemoRoundBoundaryFormationFact& Formation
    : Pipeline.GetBoundaryFormationFacts())
    FormationByAgentId.Add(Formation.AgentId, &Formation);
  TMap<int32, const FCrowdMassSharedFlowAgentOutput*> SharedFlowByAgentId;
  for (const FCrowdMassSharedFlowAgentOutput& Value
    : Pipeline.GetPreparedRuntimeSharedFlowOutputs())
    SharedFlowByAgentId.Add(Value.AgentId, &Value);
  const bool bRequiresCombatCommit = Pipeline.IsRangedProjectileCombat()
    || (Pipeline.GetRules().Scenario
        == ECrowdDemoScenario::SimRoundSoftPressure
      && Pipeline.GetRules().SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse);
  const FCrowdDemoPreparedCombatBoundaryCommit* CombatCommit =
    bRequiresCombatCommit
      ? &Pipeline.GetPreparedCombatBoundaryCommit() : nullptr;
  TMap<int32, const FCrowdDemoRangedCombatAgent*> CombatByAgentId;
  if (CombatCommit)
  {
    if (!Pipeline.IsPreparedCombatBoundaryCommitCurrent()
      || !CombatCommit->bValid
      || CombatCommit->Agents.Num()
        != Pipeline.GetBoundarySnapshot().Agents.Num())
    {
      return false;
    }
    for (const FCrowdDemoRangedCombatAgent& Agent : CombatCommit->Agents)
    {
      if (CombatByAgentId.Contains(Agent.AgentId))
        return false;
      CombatByAgentId.Add(Agent.AgentId, &Agent);
    }
  }

  bool bTargetSetValid = WorkOutput.bCompleted
    && FinalizeOutput.bCompleted
    && ById.Num() == Pipeline.GetBoundarySnapshot().Agents.Num()
    && CommitByAgentId.Num() == ById.Num()
    && FormationByAgentId.Num() == ById.Num()
    && Prepared.ConsecutiveSettleStepsByAgentId.Num() == ById.Num()
    && Prepared.FinalSettledByAgentId.Num() == ById.Num();
  bTargetSetValid = bTargetSetValid
    && SharedFlowByAgentId.Num() == ById.Num();
  TArray<FCrowdMassCommitTarget> ResolvedTargets;
  ResolvedTargets.Reserve(Pipeline.GetBoundarySnapshot().Agents.Num());
  EntityQuery.ForEachEntityChunk(
    Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto DemoIdentities =
      ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto RuntimeIdentities =
      ChunkContext.GetFragmentView<FCrowdMassAgentFragment>();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const int32 AgentId = DemoIdentities[It].Id;
      const FCrowdFacingResult* const* Facing = ById.Find(AgentId);
      const FCrowdMassCommitRecord* const* Record =
        CommitByAgentId.Find(AgentId);
      const FCrowdMovementOutput* Movement = Record
        ? &(*Record)->Movement : nullptr;
      FCrowdMassCommitTarget& ResolvedTarget =
        ResolvedTargets.AddDefaulted_GetRef();
      ResolvedTarget.EntityRef =
        RuntimeIdentities[It].GetStableEntityRef();
      ResolvedTarget.AgentId = RuntimeIdentities[It].AgentId;
      ResolvedTarget.LifecycleSerial =
        RuntimeIdentities[It].LifecycleSerial;
      bTargetSetValid = bTargetSetValid && Facing && Movement
        && SharedFlowByAgentId.Contains(AgentId)
        && (!CombatCommit || CombatByAgentId.Contains(AgentId))
        && Movement->bValid
        && Movement->LifecycleSerial
          == static_cast<uint32>(DemoIdentities[It].LifecycleSerial)
        && RuntimeIdentities[It].AgentId == AgentId
        && RuntimeIdentities[It].LifecycleSerial
          == DemoIdentities[It].LifecycleSerial
        && (!CombatCommit
          || (*CombatByAgentId.Find(AgentId))->LifecycleSerial
            == DemoIdentities[It].LifecycleSerial)
        && Prepared.ConsecutiveSettleStepsByAgentId.Contains(AgentId)
        && Prepared.FinalSettledByAgentId.Contains(AgentId)
        && FormationByAgentId.Contains(AgentId)
        && FMath::IsNearlyEqual(
          Movement->YawDegrees, (*Facing)->ResolvedYawDegrees, 0.01f);
    }
  });
  if (!bTargetSetValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingFinalizePreparedTargetSetInvalid step=%d facing=%d commits=%d"),
      Pipeline.GetCurrentFixedStepIndex(), ById.Num(),
      CommitByAgentId.Num());
    return false;
  }
  const double ValidateStartSeconds = FPlatformTime::Seconds();
  if (!Pipeline.PrepareBoundaryCommit(ResolvedTargets))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBoundaryEnvelopeRejected step=%d targets=%d"),
      Pipeline.GetCurrentFixedStepIndex(), ResolvedTargets.Num());
    return false;
  }
  if (CombatCommit && CombatCommit->bProjectileCombat)
  {
    int32 RequiredActiveProjectiles = 0;
    for (const FCrowdDemoProjectileState& Projectile
      : CombatCommit->Projectiles)
      RequiredActiveProjectiles += Projectile.bActive ? 1 : 0;
    if (!Pipeline.PrepareProjectileFinalApply(
        RequiredActiveProjectiles))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMassProjectileCapacityRejected step=%d required=%d"),
        Pipeline.GetCurrentFixedStepIndex(),
        RequiredActiveProjectiles);
      return false;
    }
  }
  const double ValidateMilliseconds =
    (FPlatformTime::Seconds() - ValidateStartSeconds) * 1000.0;
  if (ValidateMilliseconds > 2.0)
  {
    UE_LOG(LogTemp, Verbose,
      TEXT("CrowdDemoBoundaryPrevalidation step=%d ms=%.3f"),
      Pipeline.GetCurrentFixedStepIndex(), ValidateMilliseconds);
  }

  int32 AppliedCount = 0;
  TArray<FCrowdDemoPreparedPostFinalizeAgentRecord>
    PreparedPostFinalizeAgentRecords;
  PreparedPostFinalizeAgentRecords.Reserve(
    FinalizeOutput.CommitPlan.Records.Num());
  TArray<FCrowdDemoRoundAgentState> PreparedCheckpointAgentStates;
  PreparedCheckpointAgentStates.Reserve(
    FinalizeOutput.CommitPlan.Records.Num());
  const bool bUseShowcaseLocomotionState =
    Pipeline.GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && (Pipeline.GetRules().SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse
      || Pipeline.GetRules().SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat);
  EntityQuery.ForEachEntityChunk(
    Context, [&](FMassExecutionContext& ChunkContext)
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
    const auto FlowSamples =
      ChunkContext.GetMutableFragmentView<
        FCrowdDemoRoundFlowSampleFragment>();
    const auto Stats =
      ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
    const auto Businesses =
      ChunkContext.GetMutableFragmentView<FCrowdDemoBusinessStateFragment>();
    const auto Attacks =
      ChunkContext.GetMutableFragmentView<FCrowdDemoRangedAttackFragment>();
    const auto Reactives =
      ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionFragment>();
    const auto HitFlashes =
      ChunkContext.GetMutableFragmentView<FCrowdDemoHitFlashFragment>();
      const auto Visuals =
      ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const int32 AgentId = Identities[It].Id;
      const FCrowdFacingResult& Result = **ById.Find(AgentId);
      const int32 ConsecutiveSteps =
        Prepared.ConsecutiveSettleStepsByAgentId.FindRef(AgentId);
      const bool bFinalSettled =
        Prepared.FinalSettledByAgentId.FindRef(AgentId);
      RuntimeFacings[It].Value = Result;
      RuntimeFacings[It].PlanRevision = WorkOutput.PlanRevision;
      RuntimeFacings[It].ConsecutiveFinalSettleSteps = ConsecutiveSteps;
      RuntimeFacings[It].bFinalPositionSettled = bFinalSettled;
      const FCrowdMassCommitRecord& Record =
        **CommitByAgentId.Find(AgentId);
      const bool bDemoApplied =
        FCrowdDemoMassCrowdRuntimeAdapter::ApplyCommitRecord(
          Record, Identities[It], RuntimeIdentities[It], States[It]);
      FCrowdMassCommitTarget RuntimeTarget;
      RuntimeTarget.EntityRef = RuntimeIdentities[It].GetStableEntityRef();
      RuntimeTarget.AgentId = RuntimeIdentities[It].AgentId;
      RuntimeTarget.LifecycleSerial =
        RuntimeIdentities[It].LifecycleSerial;
      const bool bRuntimeApplied =
        FCrowdMassRuntimeBridge::ApplyMovementToState(
          Record, RuntimeTarget, RuntimeStates[It], RuntimeMovements[It]);
      checkf(bDemoApplied && bRuntimeApplied,
        TEXT("Prepared movement commit failed after full validation"));
      const FCrowdMovementOutput& Movement = Record.Movement;
      FTransform Transform = Transforms[It].GetTransform();
      Transform.SetLocation(Movement.Position);
      Transform.SetRotation(
        FRotator(0.0f, Movement.YawDegrees, 0.0f).Quaternion());
      Transforms[It].SetTransform(Transform);
      Velocities[It].Value = Movement.Velocity;
      DemoMovements[It].CurrentVelocity = Movement.Velocity;
      DemoMovements[It].DesiredVelocity = Movement.Velocity;
      DemoMovements[It].YawDegrees = Movement.YawDegrees;
      const FCrowdMassSharedFlowAgentOutput& SharedFlow =
        **SharedFlowByAgentId.Find(AgentId);
      const FCrowdDemoSharedFlowSample FlowSample =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowSample(
          SharedFlow.Sample);
      FlowSamples[It] = {};
      FlowSamples[It].CellIndex = FlowSample.CellIndex;
      FlowSamples[It].StableCellKey = FlowSample.StableCellKey;
      FlowSamples[It].NavigationNodeKey =
        FlowSample.NavigationNodeKey;
      FlowSamples[It].NextNavigationNodeKey =
        FlowSample.NextNavigationNodeKey;
      FlowSamples[It].Status = FlowSample.Status;
      FlowSamples[It].FlowDirection = FlowSample.FlowDirection;
      FlowSamples[It].IntegrationCost = FlowSample.IntegrationCost;
      FlowSamples[It].GuidanceDistanceCm =
        FlowSample.GuidanceDistanceCm;
      FlowSamples[It].bBlocked = FlowSample.bBlocked;
      FlowSamples[It].bUnreachable = FlowSample.bUnreachable;
      FlowSamples[It].bRecoveredFromRasterMismatch =
        FlowSample.bRecoveredFromRasterMismatch;
      FlowSamples[It].bSourceAttached =
        FlowSample.bSourceAttached;
      States[It].SimulatedServerTimeSeconds =
        Pipeline.GetCurrentStepEndServerTimeSeconds();
      if (CombatCommit)
      {
        const FCrowdDemoRangedCombatAgent& CombatAgent =
          **CombatByAgentId.Find(AgentId);
        ApplyCombatAgentState(
          CombatAgent.Combat, Stats[It], Businesses[It], Attacks[It],
          Reactives[It], HitFlashes[It], Visuals[It]);
      }
      FCrowdDemoCombatAgentState CombatAgent = MakeCombatAgentState(
        Identities[It], Stats[It], Businesses[It], Attacks[It],
        Reactives[It], HitFlashes[It], Visuals[It]);
      FCrowdDemoCombatStateKernel::ResolveVisualStateBoundary(
        Pipeline.GetCurrentFixedStepIndex(),
        Pipeline.GetCurrentStepEndServerTimeSeconds(),
        Movement.Velocity, CombatAgent, bUseShowcaseLocomotionState);
      ApplyCombatAgentState(
        CombatAgent, Stats[It], Businesses[It], Attacks[It],
        Reactives[It], HitFlashes[It], Visuals[It]);
      switch (CombatAgent.VisualState)
      {
        case ECrowdDemoVisualState::Move:
          Visuals[It].AnimState = ECrowdDemoAnimState::Move; break;
        case ECrowdDemoVisualState::Attack:
          Visuals[It].AnimState = ECrowdDemoAnimState::Attack; break;
        case ECrowdDemoVisualState::HitReact:
          Visuals[It].AnimState = ECrowdDemoAnimState::HitReact; break;
        case ECrowdDemoVisualState::Death:
          Visuals[It].AnimState = ECrowdDemoAnimState::Death; break;
        default:
          Visuals[It].AnimState = ECrowdDemoAnimState::Idle; break;
      }
      const FCrowdDemoCombatNetState Combat = MakeCombatNetState(
        Stats[It], Businesses[It], Attacks[It], Reactives[It],
        HitFlashes[It], Visuals[It]);
      FCrowdDemoPreparedPostFinalizeAgentRecord& FinalState =
        PreparedPostFinalizeAgentRecords.AddDefaulted_GetRef();
      FinalState.AgentId = AgentId;
      FinalState.LifecycleSerial = Identities[It].LifecycleSerial;
      FinalState.State = States[It];
      const FCrowdDemoRoundBoundaryFormationFact& Formation =
        **FormationByAgentId.Find(AgentId);
      FCrowdDemoRoundAgentState& Checkpoint =
        PreparedCheckpointAgentStates.AddDefaulted_GetRef();
      Checkpoint.AgentId = AgentId;
      Checkpoint.LifecycleSerial = Identities[It].LifecycleSerial;
      Checkpoint.Location = FVector_NetQuantize10(States[It].Location);
      Checkpoint.Velocity = FVector_NetQuantize10(States[It].Velocity);
      Checkpoint.YawDegrees = States[It].YawDegrees;
      Checkpoint.RadiusCm = Formation.RadiusCm;
      Checkpoint.Combat = Combat;
      ++AppliedCount;
    }
  });
  checkf(AppliedCount == FinalizeOutput.CommitPlan.Records.Num(),
    TEXT("Prepared movement apply count changed after validation"));
  Pipeline.ApplyPreparedBoundaryResourcePatches();
  if (CombatCommit)
  {
    if (CombatCommit->bProjectileCombat)
    {
      Pipeline.ApplyProjectileFinalState(
        CombatCommit->Projectiles);
      Pipeline.RecordProjectileStep(
        CombatCommit->ProjectileSummary,
        CombatCommit->ProjectileEvents);
      Pipeline.RecordProjectileHitResponse(CombatCommit->HitSummary);
    }
    Pipeline.ResetPreparedCombatBoundaryCommit();
    Pipeline.LogStageOnce(
      TEXT("03_combat_boundary_transaction"), AppliedCount);
  }
  const bool bPostRecordsAccepted =
    Pipeline.SetPreparedPostFinalizeAgentRecords(
      MoveTemp(PreparedPostFinalizeAgentRecords));
  const bool bCheckpointAccepted =
    bPostRecordsAccepted
    && Pipeline.SetPreparedCheckpointAgentStates(
      MoveTemp(PreparedCheckpointAgentStates));
  checkf(bPostRecordsAccepted && bCheckpointAccepted,
    TEXT("Prepared movement publisher records rejected after validation"));

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
      Prepared.ConsecutiveSettleStepsByAgentId.FindRef(Result.AgentId);
    Fact.Facing.bFinalPositionSettled =
      Prepared.FinalSettledByAgentId.FindRef(Result.AgentId);
  }
  Pipeline.SetPreparedRuntimeFacingResults(MoveTemp(PreparedFacingResults));
  Pipeline.SetPreparedFacingRollbackFacts(
    MoveTemp(PreparedFacingRollbackFacts));
  Pipeline.ResetPreparedMovementBoundaryCommit();
  Pipeline.MarkMovementFinalizeApplied();
  Pipeline.LogStageOnce(
    Pipeline.GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("09_movement_finalize")
      : TEXT("06_movement_finalize"),
    AppliedCount);
  return true;
}

void UCrowdDemoRoundFacingFinalizeProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive()) return;
  if (Pipeline->IsPreparedMovementBoundaryCommitCurrent())
  {
    ApplyPreparedCommit(*Pipeline, Context);
    return;
  }

  FCrowdMassFacingFinalizeWorkOutput CombinedOutput;
  TMap<int32, int32> ConsecutiveSettleStepsByAgentId;
  TMap<int32, bool> FinalSettledByAgentId;
  if (!Pipeline->ConsumeBoundaryFacingWork(
      CombinedOutput, ConsecutiveSettleStepsByAgentId,
      FinalSettledByAgentId))
  {
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

  FCrowdMassFacingFinalizeWorkInput CombinedInput;
  FCrowdMassFacingWorkInput& WorkInput = CombinedInput.Facing;
  WorkInput.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  WorkInput.PlanRevision = Pipeline->GetCurrentPlanRevision();
  WorkInput.Settings.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
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
  const bool bUsesParticle = Pipeline->GetRules().Scenario
    == ECrowdDemoScenario::SimRoundSoftPressure;
  const bool bBuildingBoundaryGraph =
    ComposedByAgentId.IsEmpty()
    && (bUsesParticle
      ? ParticleByAgentId.IsEmpty()
      : Pipeline->GetPreparedRuntimeFinalKinematics().IsEmpty());
  TMap<int32, int32> PreviousSettleStepsByAgentId;
  for (const FCrowdDemoRoundBoundaryFacingFact& Facing
    : Pipeline->GetBoundaryFacingFacts())
    PreviousSettleStepsByAgentId.Add(
      Facing.AgentId, Facing.ConsecutiveFinalSettleSteps);
  TMap<int32, bool> TerminalOwnerByAgentId;
  bool bGatherValid = true;
  for (const FCrowdMassBoundaryAgentRecord& Base
    : Pipeline->GetBoundarySnapshot().Agents)
  {
    const FCrowdComposedGuidance* const* Composed =
      ComposedByAgentId.Find(Base.Identity.AgentId);
    const FCrowdParticleConstraintResult* const* Particle =
      ParticleByAgentId.Find(Base.Identity.AgentId);
    if ((!bBuildingBoundaryGraph && !Composed)
      || (bUsesParticle && !bBuildingBoundaryGraph && !Particle)
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
    TerminalOwnerByAgentId.Add(Base.Identity.AgentId, bTerminalOwner);
    const bool bSettledThisStep = !bBuildingBoundaryGraph
      && bTerminalOwner && bUsesParticle
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
    Input.AutonomousPreferredVelocity = bBuildingBoundaryGraph
      ? FVector2f::ZeroVector
      : FVector2f((*Composed)->AutonomousPreferredVelocity.X,
          (*Composed)->AutonomousPreferredVelocity.Y);
    const FVector FacingLocation = bUsesParticle
      && !bBuildingBoundaryGraph
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
  CombinedInput.Snapshot = Pipeline->GetBoundarySnapshot();
  if (bBuildingBoundaryGraph)
  {
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
  else
  {
    CombinedInput.Kinematics = Pipeline->GetPreparedRuntimeFinalKinematics();
    if (!Pipeline->DispatchBoundaryFacingWork(
          MoveTemp(CombinedInput),
          MoveTemp(ConsecutiveSettleStepsByAgentId),
          MoveTemp(FinalSettledByAgentId)))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoFacingWorkDispatchRejected step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
    }
  }
    return;
  }
  const FCrowdMassFacingWorkOutput& WorkOutput = CombinedOutput.Facing;
  const FCrowdMassMovementFinalizeWorkOutput& FinalizeOutput =
    CombinedOutput.Finalize;
  if (!CombinedOutput.bCompleted || !WorkOutput.bCompleted
    || !FinalizeOutput.bCompleted)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingFinalizeWorkInvalid step=%d inputs=%d results=%d commits=%d"),
      Pipeline->GetCurrentFixedStepIndex(),
      ConsecutiveSettleStepsByAgentId.Num(), WorkOutput.Summary.Results.Num(),
      FinalizeOutput.CommitPlan.Records.Num());
    return;
  }
  TMap<int32, const FCrowdFacingResult*> ById;
  for (const auto& Result : WorkOutput.Summary.Results)
    ById.Add(Result.AgentId, &Result);
  TMap<int32, const FCrowdMassCommitRecord*> CommitByAgentId;
  for (const FCrowdMassCommitRecord& Record : FinalizeOutput.CommitPlan.Records)
    CommitByAgentId.Add(Record.Movement.AgentId, &Record);
  TMap<int32, const FCrowdMassFinalKinematicState*> KinematicByAgentId;
  for (const FCrowdMassFinalKinematicState& Kinematic
    : Pipeline->GetPreparedRuntimeFinalKinematics())
    KinematicByAgentId.Add(Kinematic.AgentId, &Kinematic);
  TMap<int32, const FCrowdDemoRoundBoundaryFormationFact*> FormationByAgentId;
  for (const FCrowdDemoRoundBoundaryFormationFact& Formation
    : Pipeline->GetBoundaryFormationFacts())
    FormationByAgentId.Add(Formation.AgentId, &Formation);
  bool bResultSetValid =
    ById.Num() == ConsecutiveSettleStepsByAgentId.Num()
    && CommitByAgentId.Num() == ById.Num()
    && KinematicByAgentId.Num() == ById.Num()
    && FormationByAgentId.Num() == ById.Num();
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto DemoIdentities =
      ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto RuntimeIdentities =
      ChunkContext.GetFragmentView<FCrowdMassAgentFragment>();
    for (FMassExecutionContext::FEntityIterator It =
      ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const int32 AgentId = DemoIdentities[It].Id;
      const FCrowdFacingResult* const* Facing = ById.Find(AgentId);
      const FCrowdMassCommitRecord* const* Commit = CommitByAgentId.Find(AgentId);
      const FCrowdMassFinalKinematicState* const* Kinematic =
        KinematicByAgentId.Find(AgentId);
      const FCrowdDemoRoundBoundaryFormationFact* const* Formation =
        FormationByAgentId.Find(AgentId);
      const FCrowdMovementOutput* Movement = Commit
        ? &(*Commit)->Movement : nullptr;
      const bool bFacingValid = Facing && Movement
        && (*Facing)->AgentId == AgentId
        && FMath::IsNearlyEqual(
          Movement->YawDegrees, (*Facing)->ResolvedYawDegrees, 0.01f);
      const bool bKinematicValid = Movement && Kinematic
        && (*Kinematic)->bValid
        && (*Kinematic)->AgentId == AgentId
        && Movement->Position.Equals((*Kinematic)->Position, 0.01f)
        && Movement->Velocity.Equals((*Kinematic)->Velocity, 0.01f);
      bResultSetValid = bResultSetValid && Commit && Movement
        && Movement->bValid
        && Movement->LifecycleSerial
          == static_cast<uint32>(DemoIdentities[It].LifecycleSerial)
        && RuntimeIdentities[It].AgentId == AgentId
        && RuntimeIdentities[It].LifecycleSerial
          == DemoIdentities[It].LifecycleSerial
        && ConsecutiveSettleStepsByAgentId.Contains(AgentId)
        && FinalSettledByAgentId.Contains(AgentId)
        && Formation && bFacingValid && bKinematicValid;
    }
  });
  if (!bResultSetValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingFinalizeAtomicSetInvalid step=%d facing=%d commits=%d"),
      Pipeline->GetCurrentFixedStepIndex(), ById.Num(), CommitByAgentId.Num());
    return;
  }
  TArray<int32> StableAgentIds;
  ConsecutiveSettleStepsByAgentId.GetKeys(StableAgentIds);
  StableAgentIds.Sort();
  uint64 StableHash = 14695981039346656037ull;
  const auto Fold = [&StableHash](const uint64 Value)
  {
    for (int32 Shift = 0; Shift < 64; Shift += 8)
    {
      StableHash ^= static_cast<uint8>(Value >> Shift);
      StableHash *= 1099511628211ull;
    }
  };
  Fold(1);
  Fold(Pipeline->GetBoundarySnapshot().StableHash);
  Fold(WorkOutput.StableHash);
  Fold(FinalizeOutput.StableHash);
  Fold(FinalizeOutput.CommitPlan.StableHash);
  for (const int32 AgentId : StableAgentIds)
  {
    Fold(static_cast<uint32>(AgentId));
    Fold(static_cast<uint32>(
      ConsecutiveSettleStepsByAgentId.FindRef(AgentId)));
    Fold(FinalSettledByAgentId.FindRef(AgentId) ? 1u : 0u);
  }
  FCrowdDemoPreparedMovementBoundaryCommit PreparedMovementBoundaryCommit;
  PreparedMovementBoundaryCommit.FixedStepIndex =
    Pipeline->GetCurrentFixedStepIndex();
  PreparedMovementBoundaryCommit.PlanRevision =
    Pipeline->GetCurrentPlanRevision();
  PreparedMovementBoundaryCommit.Facing = WorkOutput;
  PreparedMovementBoundaryCommit.Finalize = FinalizeOutput;
  PreparedMovementBoundaryCommit.ConsecutiveSettleStepsByAgentId =
    MoveTemp(ConsecutiveSettleStepsByAgentId);
  PreparedMovementBoundaryCommit.FinalSettledByAgentId =
    MoveTemp(FinalSettledByAgentId);
  PreparedMovementBoundaryCommit.StableHash = StableHash;
  PreparedMovementBoundaryCommit.bValid = StableHash != 0;
  if (!Pipeline->SetPreparedMovementBoundaryCommit(
      MoveTemp(PreparedMovementBoundaryCommit)))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMovementPreparedCommitRejected step=%d records=%d"),
      Pipeline->GetCurrentFixedStepIndex(),
      FinalizeOutput.CommitPlan.Records.Num());
  }
}

UCrowdDemoRoundMovementWorkProcessor::UCrowdDemoRoundMovementWorkProcessor()
{
  ROUND_DYNAMIC_FLAGS;
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
  if (FlowCandidates.IsEmpty())
  {
    FlowCandidates.Reserve(
      Pipeline->GetBoundarySnapshot().Agents.Num());
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

  const TArray<FCrowdMassGatherRecord> GatherRecords =
    WorkInput.Guidance.Records;
  FCrowdMassMovementPipelineWorkOutput WorkOutput;
  if (!Pipeline->ConsumeBoundaryMovementWork(WorkOutput))
  {
    if (!Pipeline->StageBoundaryMovementWork(MoveTemp(WorkInput)))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMovementWorkStageRejected step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
    }
    return;
  }
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
  for (const FCrowdMassGatherRecord& Record : GatherRecords)
  {
    const int32 AgentId = Record.Identity.AgentId;
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

  const int32 AppliedCount = GatheredById.Num();

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

UCrowdDemoRoundPostFinalizeMetricsProcessor::
UCrowdDemoRoundPostFinalizeMetricsProcessor()
{
  ROUND_DYNAMIC_FLAGS;
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
  if (Pipeline->GetRules().Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure
    && !Pipeline->CommitPreparedParticleDiagnostics())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoParticleDiagnosticCommitMissing step=%d"),
      Pipeline->GetCurrentFixedStepIndex());
    return;
  }

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
    && BoundaryByAgentId.Num() == FacingByAgentId.Num()
    && BoundaryByAgentId.Num()
      == Pipeline->GetPreparedPostFinalizeAgentRecords().Num();

  TArray<FCrowdDemoRoundFlowAgentSample> MetricSamples;
  TArray<FCrowdDemoSoftPressureRollbackAgentState> SoftPressureRollbackAgents;
  TArray<int32> OpenSpawnAgentIds;
  TArray<FVector> OpenSpawnLocations;
  TArray<FCrowdDemoBidirectionalSwapStepAgent> BidirectionalSwapAgents;
  TArray<FCrowdDemoValidCorridorTransitStepAgent> ValidCorridorTransitAgents;
  for (const FCrowdDemoPreparedPostFinalizeAgentRecord& FinalRecord
    : Pipeline->GetPreparedPostFinalizeAgentRecords())
  {
      const int32 AgentId = FinalRecord.AgentId;
      const int32 LifecycleSerial = FinalRecord.LifecycleSerial;
      const FCrowdDemoRoundSimStateFragment& State = FinalRecord.State;
      const FCrowdMassBoundaryAgentRecord* const* Boundary =
        BoundaryByAgentId.Find(AgentId);
      const FCrowdDemoRoundBoundaryFormationFact* const* Formation =
        FormationByAgentId.Find(AgentId);
      const FCrowdMassSharedFlowAgentOutput* const* FlowOutput =
        FlowOutputByAgentId.Find(AgentId);
      const FCrowdDemoPreparedFacingRollbackFact* const* Facing =
        FacingByAgentId.Find(AgentId);
      if (!Boundary || !Formation || !FlowOutput || !Facing
        || static_cast<int32>((*Boundary)->Identity.LifecycleSerial)
          != LifecycleSerial)
      {
        bPreparedSetValid = false;
        continue;
      }
      if (Pipeline->IsOpenSpawnRelaxation())
      {
        OpenSpawnAgentIds.Add(AgentId);
        OpenSpawnLocations.Add(State.Location);
      }

      FCrowdDemoRoundFlowAgentSample& Metric = MetricSamples.AddDefaulted_GetRef();
      Metric.AgentId = AgentId;
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
        SwapAgent.AgentId = AgentId;
        SwapAgent.FormationIndex = (*Formation)->FormationIndex;
        SwapAgent.Location = State.Location;
        SwapAgent.Velocity = State.Velocity;
        SwapAgent.FlowStatus = FinalFlowSample.Status;
      }
      if (Pipeline->IsCorridorTransitProgressScenario())
      {
        auto& TransitAgent = ValidCorridorTransitAgents.AddDefaulted_GetRef();
        TransitAgent.AgentId = AgentId;
        TransitAgent.Location = State.Location;
        TransitAgent.Velocity = State.Velocity;
        TransitAgent.FlowStatus = FinalFlowSample.Status;
      }
      FCrowdDemoSoftPressureRollbackAgentState& Rollback =
        SoftPressureRollbackAgents.AddDefaulted_GetRef();
      Rollback.AgentId = AgentId;
      Rollback.LifecycleSerial = LifecycleSerial;
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
  Pipeline->RecordSoftPressureRollbackSnapshot(
    Pipeline->GetCurrentFixedStepIndex(),
    MoveTemp(SoftPressureRollbackAgents));
  Pipeline->RecordFlowAgentSamples(
    MetricSamples, World->GetNetMode() == NM_Client);
  const TArray<FCrowdDemoRoundAgentState>& CheckpointStates =
    Pipeline->GetPreparedCheckpointAgentStates();
  const TArray<FCrowdDemoPreparedPostFinalizeAgentRecord>& FinalRecords =
    Pipeline->GetPreparedPostFinalizeAgentRecords();
  const TArray<FCrowdDemoRoundBoundaryFormationFact>& FormationFacts =
    Pipeline->GetBoundaryFormationFacts();
  TArray<FCrowdDemoPreparedCombatRollbackFact> RollbackCombatStates;
  TArray<FCrowdDemoParticleAppliedRoundSimState> ParticleAppliedStates;
  RollbackCombatStates.Reserve(CheckpointStates.Num());
  if (Pipeline->GetRules().Scenario
    == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    ParticleAppliedStates.Reserve(CheckpointStates.Num());
  }
  bool bCombatFactsValid = CheckpointStates.Num() == FinalRecords.Num()
    && CheckpointStates.Num() == FormationFacts.Num();
  for (int32 Index = 0; Index < CheckpointStates.Num(); ++Index)
  {
    const FCrowdDemoRoundAgentState& Checkpoint = CheckpointStates[Index];
    const FCrowdDemoPreparedPostFinalizeAgentRecord& Final =
      FinalRecords[Index];
    const FCrowdDemoRoundBoundaryFormationFact& Formation =
      FormationFacts[Index];
    if (Checkpoint.AgentId != Final.AgentId
      || Checkpoint.AgentId != Formation.AgentId
      || Checkpoint.LifecycleSerial != Final.LifecycleSerial)
    {
      bCombatFactsValid = false;
      continue;
    }
    FCrowdDemoPreparedCombatRollbackFact& Rollback =
      RollbackCombatStates.AddDefaulted_GetRef();
    Rollback.AgentId = Checkpoint.AgentId;
    Rollback.Combat = Checkpoint.Combat;
    if (Pipeline->GetRules().Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure)
    {
      FCrowdDemoParticleAppliedRoundSimState& Applied =
        ParticleAppliedStates.AddDefaulted_GetRef();
      Applied.AgentId = Final.AgentId;
      Applied.LifecycleSerial = Final.LifecycleSerial;
      Applied.Position = Final.State.Location;
      Applied.Velocity = Final.State.Velocity;
      Applied.YawDegrees = Final.State.YawDegrees;
      Applied.RadiusCm = Formation.RadiusCm;
      Applied.bInitialized = Final.State.bInitialized;
      Applied.Combat = Checkpoint.Combat;
    }
  }
  if (!bCombatFactsValid
    || !Pipeline->CompleteSoftPressureRollbackCombatState(
      Pipeline->GetCurrentFixedStepIndex(), RollbackCombatStates))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoPostFinalizeCombatFactsIncomplete step=%d checkpoint=%d final=%d formation=%d"),
      Pipeline->GetCurrentFixedStepIndex(), CheckpointStates.Num(),
      FinalRecords.Num(), FormationFacts.Num());
    return;
  }
  if (Pipeline->GetRules().Scenario
    == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    Pipeline->RecordParticleAppliedStateHash(
      FCrowdDemoParticleConstraintKernel::HashAppliedRoundSimState(
        Pipeline->GetCurrentRoundId(), Pipeline->GetCurrentPlanRevision(),
        Pipeline->GetCurrentFixedStepIndex(),
        Pipeline->GetCurrentStepEndServerTimeSeconds(),
        ParticleAppliedStates));
  }
}

UCrowdDemoRoundAuthorityCommitProcessor::UCrowdDemoRoundAuthorityCommitProcessor()
{
  ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone);
  bAutoRegisterWithProcessingPhases = false;
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
  const int32 Count = Pipeline->GetPreparedPostFinalizeAgentRecords().Num();
  if (Count != Pipeline->GetBoundarySnapshot().Agents.Num())
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
{
  ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
  bAutoRegisterWithProcessingPhases = false;
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
  const int32 Count = Pipeline->GetPreparedPostFinalizeAgentRecords().Num();
  if (Count != Pipeline->GetBoundarySnapshot().Agents.Num())
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
{
  ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone);
  bAutoRegisterWithProcessingPhases = false;
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
  const TArray<FCrowdDemoRoundAgentState>& PreparedStates =
    Pipeline->GetPreparedCheckpointAgentStates();
  if (PreparedStates.Num()
    != Pipeline->GetPreparedPostFinalizeAgentRecords().Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoCheckpointPreparedStateMismatch step=%d states=%d final=%d"),
      Pipeline->GetCurrentFixedStepIndex(), PreparedStates.Num(),
      Pipeline->GetPreparedPostFinalizeAgentRecords().Num());
    return;
  }
  TArray<FCrowdDemoRoundAgentState> States = PreparedStates;
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
  CombatBoundaryProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundCombatBoundaryProcessor>(
    *this, Owner, EntityManager);
  MovementWorkProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundMovementWorkProcessor>(*this, Owner, EntityManager);
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
  FacingFinalizeProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundFacingFinalizeProcessor>(*this, Owner, EntityManager);
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
      if (!Pipeline->StageBoundaryBusinessWork())
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoBusinessWorkStageRejected step=%d"),
          Pipeline->GetCurrentFixedStepIndex());
        return;
      }
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
      FacingFinalizeProcessor->CallExecute(EntityManager, Context);
      if (Pipeline->WaitBoundaryWork())
      {
        if (Pipeline->GetRules().Scenario
          == ECrowdDemoScenario::SimRoundSoftPressure)
        {
          MovementWorkProcessor->CallExecute(EntityManager, Context);
          ParticleConstraintProcessor->CallExecute(EntityManager, Context);
        }
        FacingFinalizeProcessor->CallExecute(EntityManager, Context);
      }
    });
    const bool bRequiresCombatCommit = Pipeline->IsRangedProjectileCombat()
      || (Pipeline->GetRules().Scenario
          == ECrowdDemoScenario::SimRoundSoftPressure
        && Pipeline->GetRules().SoftPressureTestCase
          == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse);
    if (bRequiresCombatCommit
      && !Pipeline->IsPreparedCombatBoundaryCommitCurrent())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoCombatBoundaryCommitMissing step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
      break;
    }
    if (!Pipeline->IsPreparedMovementBoundaryCommitCurrent())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMovementBoundaryCommitMissing step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
      break;
    }
    bool bFinalCommitApplied = true;
    MeasureStage(ECrowdDemoRoundPerformanceStage::Commit, [&]
    {
      const double CommitStartSeconds = FPlatformTime::Seconds();
      FacingFinalizeProcessor->CallExecute(EntityManager, Context);
      bFinalCommitApplied =
        !Pipeline->IsPreparedMovementBoundaryCommitCurrent()
        && Pipeline->IsMovementFinalizeAppliedCurrent()
        && (!bRequiresCombatCommit
          || !Pipeline->IsPreparedCombatBoundaryCommitCurrent());
      if (!bFinalCommitApplied)
        return;
      if (!Pipeline->MarkBoundaryCommitted(
          (FPlatformTime::Seconds() - CommitStartSeconds) * 1000.0))
      {
        bFinalCommitApplied = false;
        return;
      }
      if (World->GetNetMode() == NM_Client)
      {
        ClientPredictionCommitProcessor->CallExecute(EntityManager, Context);
      }
      else
      {
        AuthorityCommitProcessor->CallExecute(EntityManager, Context);
      }
      PostFinalizeMetricsProcessor->CallExecute(EntityManager, Context);
      if (World->GetNetMode() != NM_Client)
        CheckpointPublisherProcessor->CallExecute(EntityManager, Context);
      Pipeline->FinishFixedStep();
    });
    if (!bFinalCommitApplied)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoBoundaryFinalCommitRejected step=%d"),
        Pipeline->GetCurrentFixedStepIndex());
      break;
    }
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
