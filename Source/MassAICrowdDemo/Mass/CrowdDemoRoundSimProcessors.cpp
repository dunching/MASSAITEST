#include "Mass/CrowdDemoRoundSimProcessors.h"
#include "Mass/CrowdDemoRoundInitialStateKernel.h"

#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoCapabilityProfileKernel.h"
#include "Mass/CrowdDemoFacingKernel.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
#include "Mass/CrowdDemoLocalPredictiveInteractionKernel.h"
#include "Mass/CrowdDemoOpenSpawnRelaxationKernel.h"
#include "Mass/CrowdDemoOpenCohortMovementKernel.h"
#include "Mass/CrowdDemoBidirectionalSwapKernel.h"
#include "Mass/CrowdDemoValidCorridorTransitKernel.h"
#include "Mass/CrowdDemoCombatStateKernel.h"
#include "Mass/CrowdDemoProjectileKernel.h"
#include "Mass/CrowdDemoTargetInfluenceKernel.h"
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
    const auto& Target = Pipeline.GetTargetApproachFact();
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
      : Rules.TargetInfluenceSettings.TargetPhysicalRadiusCm;
    Settings.TargetHardSafetyGapCm = CapabilityProfile
      ? CapabilityProfile->TargetHardSafetyGapCm
      : Rules.TargetInfluenceSettings.TargetHardSafetyGapCm;
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
        Rules.TargetInfluenceSettings.DefaultMinimumCombatCenterDistanceCm,
        Settings.TargetPhysicalRadiusCm + Settings.PhysicalRadiusCm
          + FMath::Max(Settings.TargetHardSafetyGapCm, Settings.HardSafetyGapCm));
    Settings.MaximumCenterDistanceCm = CapabilityProfile
      ? CapabilityProfile->NormalizedMaximumCenterDistanceCm
      : Rules.TargetInfluenceSettings.DefaultMaximumCombatCenterDistanceCm;
    Settings.InfluenceBlendWidthCm = Rules.TargetInfluenceSettings.InfluenceBlendWidthCm;
    Settings.RadialBandWidthCm = Rules.TargetRegionTransportSettings.RadialBandWidthCm;
    Settings.TransportSpeedCmps = Rules.TargetRegionTransportSettings.TransportSpeedCmps;
    Settings.RadialGainPerSecond = Rules.TargetInfluenceSettings.RadialGainPerSecond;
    Settings.DemandRegionCount = Rules.TargetRegionTransportSettings.DemandRegionCount;
    Settings.DemandRegionPhaseOffset = DemandRegionPhaseOffset;
    Settings.PlanLifetimeSteps = Rules.TargetRegionTransportSettings.PlanLifetimeSteps;
    Settings.PositionQuantumCm = Rules.TargetInfluenceSettings.PositionQuantumCm;
    Settings.VelocityQuantumCmps = Rules.TargetInfluenceSettings.VelocityQuantumCmps;
    Settings.DistanceResponsePolicy = CapabilityProfile
      ? CapabilityProfile->TargetDistanceResponsePolicy
      : ECrowdDemoTargetDistanceResponsePolicy::StrictBand;
    return Settings;
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
    const FCrowdDemoTargetApproachFragment* TargetApproach = nullptr,
    const FCrowdDemoCombatNetState* Combat = nullptr)
  {
    FCrowdDemoRoundAgentState Result;
    Result.AgentId = Identity.Id;
    Result.LifecycleSerial = Identity.LifecycleSerial;
    Result.Location = FVector_NetQuantize10(State.Location);
    Result.YawDegrees = State.YawDegrees;
    Result.Velocity = FVector_NetQuantize10(State.Velocity);
    Result.RadiusCm = Formation.RadiusCm;
    if (TargetApproach != nullptr)
    {
      Result.TargetApproach.bValid = 1;
      Result.TargetApproach.State = static_cast<uint8>(TargetApproach->State);
      Result.TargetApproach.TargetId = TargetApproach->TargetId;
      Result.TargetApproach.TargetRevision = TargetApproach->TargetRevision;
      Result.TargetApproach.SlotLayoutRevision = TargetApproach->SlotLayoutRevision;
      Result.TargetApproach.AssignedSlotId = TargetApproach->AssignedSlotId;
      Result.TargetApproach.RingEnterFixedStep = TargetApproach->RingEnterFixedStep;
      Result.TargetApproach.StateEnterFixedStep = TargetApproach->StateEnterFixedStep;
      Result.TargetApproach.StableSettleSteps = TargetApproach->StableSettleSteps;
      Result.TargetApproach.StateRevision = TargetApproach->StateRevision;
    }
    if (Combat != nullptr)
    {
      Result.Combat = *Combat;
    }
    return Result;
  }

  FCrowdDemoTargetApproachNetState MakeTargetApproachNetState(
    const FCrowdDemoTargetApproachFragment& Source)
  {
    FCrowdDemoTargetApproachNetState Result;
    Result.bValid = 1;
    Result.State = static_cast<uint8>(Source.State);
    Result.TargetId = Source.TargetId;
    Result.TargetRevision = Source.TargetRevision;
    Result.SlotLayoutRevision = Source.SlotLayoutRevision;
    Result.AssignedSlotId = Source.AssignedSlotId;
    Result.RingEnterFixedStep = Source.RingEnterFixedStep;
    Result.StateEnterFixedStep = Source.StateEnterFixedStep;
    Result.StableSettleSteps = Source.StableSettleSteps;
    Result.StateRevision = Source.StateRevision;
    return Result;
  }

  bool MatchesCorrectionState(
    const FCrowdDemoSoftPressureRollbackAgentState& Local,
    const FCrowdDemoRoundAgentState& Authority,
    const bool bCompareTargetApproach)
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
    if (bCompareTargetApproach)
    {
      const FCrowdDemoTargetApproachNetState LocalTarget =
        MakeTargetApproachNetState(Local.TargetApproach);
      if (!FCrowdDemoTargetApproachNetState::StaticStruct()->CompareScriptStruct(
        &LocalTarget, &Authority.TargetApproach, 0))
      {
        return false;
      }
    }
    return true;
  }

  bool IsValidTargetApproachNetState(const FCrowdDemoTargetApproachNetState& State)
  {
    return State.bValid != 0
      && State.State <= static_cast<uint8>(ECrowdDemoTargetApproachState::FreeSettle)
      && State.TargetId != INDEX_NONE
      && State.TargetRevision != INDEX_NONE
      && State.SlotLayoutRevision >= INDEX_NONE
      && State.RingEnterFixedStep >= INDEX_NONE
      && State.StateEnterFixedStep >= 0
      && State.StableSettleSteps >= 0
      && State.StateRevision >= 0;
  }

  void ApplyTargetApproachNetState(
    const FCrowdDemoTargetApproachNetState& Source,
    FCrowdDemoTargetApproachFragment& Target)
  {
    Target.State = static_cast<ECrowdDemoTargetApproachState>(Source.State);
    Target.TargetId = Source.TargetId;
    Target.TargetRevision = Source.TargetRevision;
    Target.SlotLayoutRevision = Source.SlotLayoutRevision;
    Target.AssignedSlotId = Source.AssignedSlotId;
    Target.RingEnterFixedStep = Source.RingEnterFixedStep;
    Target.StateEnterFixedStep = Source.StateEnterFixedStep;
    Target.StableSettleSteps = Source.StableSettleSteps;
    Target.StateRevision = Source.StateRevision;
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
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundFacingFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoTargetApproachFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoTargetCapabilityFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundProposedMovementFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoOpenSpawnRelaxationFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoParticlePropertiesFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassStatsFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoBusinessStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRangedAttackFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionStepFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoHitFlashFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
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
      const TArrayView<FCrowdDemoRoundMoveIntentFragment> Intents = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundMoveIntentFragment>();
      const auto Facings = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFacingFragment>();
      const TArrayView<FCrowdDemoTargetApproachFragment> TargetApproaches = ChunkContext.GetMutableFragmentView<FCrowdDemoTargetApproachFragment>();
      const TArrayView<FCrowdDemoTargetCapabilityFragment> TargetCapabilities = ChunkContext.GetMutableFragmentView<FCrowdDemoTargetCapabilityFragment>();
      const TArrayView<FCrowdDemoRoundFlowSampleFragment> FlowSamples = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFlowSampleFragment>();
      const TArrayView<FCrowdDemoRoundProposedMovementFragment> ProposedMovements = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundProposedMovementFragment>();
      const TArrayView<FCrowdDemoRoundObstacleConstraintFragment> Constraints = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
      const TArrayView<FCrowdDemoParticlePropertiesFragment> ParticleProperties = ChunkContext.GetMutableFragmentView<FCrowdDemoParticlePropertiesFragment>();
      const auto OpenSpawnStates = ChunkContext.GetMutableFragmentView<FCrowdDemoOpenSpawnRelaxationFragment>();
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
        OpenSpawnStates[It] = FCrowdDemoOpenSpawnRelaxationFragment();
        OpenSpawnStates[It].FormationIndex = Formation.FormationIndex;
        if (bOpenSpawnRelaxation)
        {
          if (const auto* LayoutAgent = OpenSpawnLayout.Agents.FindByPredicate(
            [&](const auto& Agent) { return Agent.AgentId == Identities[It].Id; }))
          {
            State.Location = LayoutAgent->StagingLocation;
            State.Velocity = FVector::ZeroVector;
            State.YawDegrees = 0.0f;
            OpenSpawnStates[It].BoundaryResetLocation = LayoutAgent->StagingLocation;
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
        Intents[It] = FCrowdDemoRoundMoveIntentFragment();
        Facings[It] = FCrowdDemoRoundFacingFragment();
        Facings[It].ResolvedYawDegrees = State.YawDegrees;
        TargetApproaches[It] = FCrowdDemoTargetApproachFragment();
        if (DuePlan.Rules.TargetInfluenceSettings.bEnabled != 0)
        {
          TargetCapabilities[It].MinimumFunctionalDistanceCm =
            DuePlan.Rules.TargetInfluenceSettings.DefaultMinimumCombatCenterDistanceCm;
          TargetCapabilities[It].MaximumFunctionalDistanceCm =
            DuePlan.Rules.TargetInfluenceSettings.DefaultMaximumCombatCenterDistanceCm;
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
      const TConstArrayView<FCrowdDemoTargetApproachFragment> TargetApproaches = ChunkContext.GetFragmentView<FCrowdDemoTargetApproachFragment>();
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
            Identities[It], Formations[It], States[It], &TargetApproaches[It], &Combat));
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
    const bool bTargetApproachCorrection = bSoftPressureScenario
      && Pipeline->GetRules().TargetApproachSettings.bEnabled != 0;
    const FCrowdDemoSoftPressureRollbackSnapshot* SoftPressureRollbackSnapshot =
      bSoftPressureScenario
      ? Pipeline->FindSoftPressureRollbackSnapshot(DiagnosticCorrectionFixedStep)
      : nullptr;
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
          || !FMath::IsNearlyEqual((*Local)->RadiusCm, ServerAgent.RadiusCm, 0.01f)
          || (bTargetApproachCorrection
            && !IsValidTargetApproachNetState(ServerAgent.TargetApproach)))
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
        CompareState.TargetApproach = MakeTargetApproachNetState(Agent.TargetApproach);
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
        if (!Local || !MatchesCorrectionState(
          **Local, ServerAgent, bTargetApproachCorrection))
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
    // quantum, so a TargetApproach correction must establish one authoritative
    // physical and business-state boundary before replay.
    bool bNeedsAuthoritativeState = bTargetApproachCorrection
      || !bValidSoftPressureRollback;
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
        const auto Facings = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFacingFragment>();
        const auto TargetApproaches = ChunkContext.GetMutableFragmentView<FCrowdDemoTargetApproachFragment>();
        const auto OpenSpawnStates = ChunkContext.GetMutableFragmentView<FCrowdDemoOpenSpawnRelaxationFragment>();
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
            Facings[It] = (*Rollback)->Facing;
            TargetApproaches[It] = (*Rollback)->TargetApproach;
            OpenSpawnStates[It] = (*Rollback)->OpenSpawnRelaxation;
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
              Facings[It] = FCrowdDemoRoundFacingFragment();
              Facings[It].ResolvedYawDegrees = (*Corrected)->YawDegrees;
              ApplyCombatNetState(
                (*Corrected)->Combat, Stats[It], Businesses[It], Attacks[It],
                Reactives[It], HitFlashes[It], Visuals[It]);
            }
          }
          if (bTargetApproachCorrection)
          {
            if (const FCrowdDemoRoundAgentState* const* Corrected =
              CorrectionById.Find(Identities[It].Id))
            {
              if (IsValidTargetApproachNetState((*Corrected)->TargetApproach))
                ApplyTargetApproachNetState((*Corrected)->TargetApproach, TargetApproaches[It]);
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
      const TArrayView<FCrowdDemoTargetApproachFragment> TargetApproaches =
        ChunkContext.GetMutableFragmentView<FCrowdDemoTargetApproachFragment>();
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
          if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
            && Pipeline->GetRules().TargetApproachSettings.bEnabled != 0
            && IsValidTargetApproachNetState((*Corrected)->TargetApproach))
          {
            ApplyTargetApproachNetState((*Corrected)->TargetApproach, TargetApproaches[It]);
          }
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
        Rules.FlowFieldConfig, FVector(Pipeline->GetTargetApproachFact().Location.X,
          Pipeline->GetTargetApproachFact().Location.Y,
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
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
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
  int32 AgentCount = 0;
  int32 RecoveredAgentCount = 0;
  int32 DesiredSegmentViolationCount = 0;
  int32 SourceAttachmentSuccessCount = 0;
  int32 NavigationUnreachableSampleCount = 0;
  const bool bTransitExitHold = Pipeline->IsValidCorridorTransit()
    && FCrowdDemoValidCorridorTransitKernel::ShouldHoldCompletedGroup(
      Pipeline->GetValidCorridorTransitProgress());
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const TConstArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const TArrayView<FCrowdDemoRoundMoveIntentFragment> Intents = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const TArrayView<FCrowdDemoRoundFlowSampleFragment> FlowSamples = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdDemoSharedFlowField* Field = &Pipeline->GetSharedFlowField();
      FVector Goal = FVector(Rules.FlowFieldConfig.GoalLocation);
      if (Pipeline->IsBidirectionalSwap())
      {
        Field = Pipeline->FindBidirectionalSwapFlowField(Formations[It].FormationIndex);
        const int32 CohortId = FCrowdDemoBidirectionalSwapKernel::
          CohortIdForFormationIndex(Formations[It].FormationIndex);
        Goal = FVector(FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(
          CohortId).GoalLocation);
      }
      if (!Field)
      {
        continue;
      }
      if (Pipeline->IsOpenSpawnRelaxation())
      {
        FCrowdDemoRoundFlowSampleFragment& FlowSample = FlowSamples[It];
        FlowSample = FCrowdDemoRoundFlowSampleFragment();
        FlowSample.Status = ECrowdDemoFlowLocationStatus::Reachable;
        FlowSample.bUnreachable = false;
        FCrowdDemoRoundMoveIntentFragment& Intent = Intents[It];
        Intent = FCrowdDemoRoundMoveIntentFragment();
        Intent.DesiredLocation = States[It].Location;
        Intent.DesiredYawDegrees = States[It].YawDegrees;
        Intent.PlanRevision = Pipeline->GetCurrentPlanRevision();
        ++AgentCount;
        continue;
      }
      const FCrowdDemoSharedFlowSample Sample = FCrowdDemoSharedFlowFieldKernel::Sample(*Field, States[It].Location);
      FCrowdDemoRoundFlowSampleFragment& FlowSample = FlowSamples[It];
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
      FlowSample.bRecoveredFromRasterMismatch = Sample.bRecoveredFromRasterMismatch;
      FlowSample.bSourceAttached = Sample.bSourceAttached;

      FCrowdDemoRoundMoveIntentFragment& Intent = Intents[It];
      const bool bTargetApproachEnabled = Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
        && Rules.TargetApproachSettings.bEnabled != 0;
      const bool bReachedGoal = !bTargetApproachEnabled && FVector::DistSquared2D(
        States[It].Location,
        Goal) <= FMath::Square(140.0f);
      const bool bShouldStop = bReachedGoal || bTransitExitHold;
      Intent.PreferredDirection = bShouldStop
        ? FVector::ZeroVector : Sample.FlowDirection;
      Intent.DesiredLocation = bTransitExitHold ? States[It].Location : Goal;
      float DesiredSpeedCmps = Rules.MaxSpeedCmPerSecond;
      if (Field->Config.ConnectivityContractVersion > 0
        && Sample.GuidanceDistanceCm > 0.0f)
        DesiredSpeedCmps = FMath::Min(
          DesiredSpeedCmps,
          Sample.GuidanceDistanceCm / FMath::Max(Rules.FixedStepSeconds, SMALL_NUMBER));
      Intent.DesiredVelocity = bShouldStop || Sample.bUnreachable
        ? FVector::ZeroVector
        : Sample.FlowDirection * DesiredSpeedCmps;
      Intent.DesiredYawDegrees = Intent.DesiredVelocity.IsNearlyZero()
        ? States[It].YawDegrees
        : Intent.DesiredVelocity.Rotation().Yaw;
      Intent.PlanRevision = Pipeline->GetCurrentPlanRevision();
      if (Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
        && Rules.SoftPressureTestCase
          == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse)
      {
        const FVector Anchor = FVector(Rules.SpawnOrigin) + Formations[It].LocalOffset;
        const FCrowdDemoVatShowcaseMotionResult Showcase =
          FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
            Formations[It].FormationIndex,
            Pipeline->GetCurrentFixedStepIndex(),
            States[It].Location,
            Anchor);
        if (Showcase.bValid)
        {
          Intent.PreferredDirection = Showcase.DesiredVelocity.GetSafeNormal();
          Intent.DesiredVelocity = Showcase.DesiredVelocity;
          Intent.DesiredLocation = Showcase.DesiredLocation;
          Intent.DesiredYawDegrees = Showcase.DesiredVelocity.IsNearlyZero()
            ? States[It].YawDegrees
            : Showcase.DesiredVelocity.Rotation().Yaw;
        }
      }
      else if (Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
        && Rules.SoftPressureTestCase
          == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat)
      {
        Intent.PreferredDirection = FVector::ZeroVector;
        Intent.DesiredVelocity = FVector::ZeroVector;
        Intent.DesiredLocation = States[It].Location;
        Intent.DesiredYawDegrees = States[It].YawDegrees;
      }
      RecoveredAgentCount += Sample.bRecoveredFromRasterMismatch ? 1 : 0;
      SourceAttachmentSuccessCount += Sample.bSourceAttached ? 1 : 0;
      NavigationUnreachableSampleCount += Sample.bUnreachable ? 1 : 0;
      if (Field->Config.ConnectivityContractVersion > 0
        && !Intent.DesiredVelocity.IsNearlyZero()
        && !FCrowdDemoSharedFlowFieldKernel::CanTraverseWorldSegment(
          Field->Config,
          States[It].Location,
          States[It].Location + Intent.DesiredVelocity * Rules.FixedStepSeconds))
        ++DesiredSegmentViolationCount;
      ++AgentCount;
    }
  });
  Pipeline->RecordFlowConnectivityStep(
    RecoveredAgentCount, DesiredSegmentViolationCount,
    SourceAttachmentSuccessCount, NavigationUnreachableSampleCount);
  Pipeline->LogStageOnce(TEXT("03_flow_preferred_velocity"), AgentCount);
}

UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor::
UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoOpenSpawnRelaxationFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.RegisterWithProcessor(*this);
}

void UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsOpenSpawnRelaxation())
    return;
  Pipeline->PrepareOpenSpawnRelaxationBoundary();
  const auto& Runtime = Pipeline->GetOpenSpawnRelaxationRuntime();
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetMutableFragmentView<FCrowdDemoOpenSpawnRelaxationFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (const auto* RuntimeAgent = Runtime.Agents.FindByPredicate(
        [&](const auto& Agent) { return Agent.AgentId == Identities[It].Id; }))
      {
        States[It].FormationIndex = RuntimeAgent->FormationIndex;
        States[It].bParticleActive = RuntimeAgent->bParticleActive;
        States[It].bPendingBoundaryReset = RuntimeAgent->bPendingBoundaryReset;
        States[It].BoundaryResetLocation = RuntimeAgent->BoundaryResetLocation;
      }
    }
  });
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
    && (Rules.TargetApproachSettings.bEnabled != 0
      || Rules.TargetInfluenceSettings.bEnabled != 0))
  {
    const int32 MotionStep = Pipeline->GetCurrentFixedStepIndex();
    const FVector2f InitialLocation(
      Rules.TargetMotion.InitialLocation.X, Rules.TargetMotion.InitialLocation.Y);
    const FVector2f LinearVelocity(
      Rules.TargetMotion.LinearVelocity.X, Rules.TargetMotion.LinearVelocity.Y);
    const float PhysicalRadiusCm = Rules.TargetInfluenceSettings.bEnabled != 0
      ? Rules.TargetInfluenceSettings.TargetPhysicalRadiusCm
      : Rules.TargetApproachSettings.TargetPhysicalRadiusCm;
    const float PositionQuantumCm = Rules.TargetInfluenceSettings.bEnabled != 0
      ? Rules.TargetInfluenceSettings.PositionQuantumCm
      : Rules.TargetApproachSettings.PositionQuantumCm;
    const float VelocityQuantumCmps = Rules.TargetInfluenceSettings.bEnabled != 0
      ? Rules.TargetInfluenceSettings.VelocityQuantumCmps
      : Rules.TargetApproachSettings.VelocityQuantumCmps;
    Pipeline->GetTargetApproachFact() = Rules.TargetMotion.bReflectAtMotionBounds != 0
      ? FCrowdDemoTargetApproachKernel::BuildReflectedLinearMotionFact(
          Rules.TargetMotion.TargetId, Rules.TargetMotion.TargetRevision, MotionStep,
          InitialLocation, LinearVelocity,
          FVector2f(Rules.TargetMotion.MotionBoundsMin.X, Rules.TargetMotion.MotionBoundsMin.Y),
          FVector2f(Rules.TargetMotion.MotionBoundsMax.X, Rules.TargetMotion.MotionBoundsMax.Y),
          Rules.TargetMotion.InitialYawDegrees,
          Rules.TargetMotion.YawRateDegreesPerSecond, PhysicalRadiusCm,
          Rules.FixedStepSeconds, PositionQuantumCm, VelocityQuantumCmps)
      : FCrowdDemoTargetApproachKernel::BuildLinearMotionFact(
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
        Pipeline->GetRules(), Pipeline->GetTargetApproachFact(), &Runtime.Cohort.Profile,
        Runtime.DemandRegionPhaseOffset);
      const bool bBuildTopology = !bStaticTargetForRound || !Runtime.Topology.bValid;
      if (bBuildTopology)
      {
        FCrowdDemoTargetRegionTransportKernel::BuildTopology(
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
    Pipeline->GetRules(), Pipeline->GetTargetApproachFact());
  const bool bBuildTopology = !bStaticTargetForRound
    || !Pipeline->GetPreparedTargetRegionTopology().bValid;
  if (bBuildTopology)
  {
    FCrowdDemoTargetRegionTransportKernel::BuildTopology(
      Settings, Pipeline->GetRules().FlowFieldConfig,
      Pipeline->GetPreparedTargetRegionTopology(), Pipeline->GetTargetRegionTopologySummary());
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
}

void UCrowdDemoRoundTargetRegionPopulationBuildProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoParticlePropertiesFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundTargetRegionPopulationBuildProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || Pipeline->GetRules().TargetRegionTransportSettings.bEnabled == 0) return;
  if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
  {
    TMap<uint32, TArray<FCrowdDemoTargetRegionTransportAgent>> AgentsByProfile;
    EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
    {
      const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
      const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
      const auto Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
      const auto Properties = ChunkContext.GetFragmentView<FCrowdDemoParticlePropertiesFragment>();
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        FCrowdDemoTargetRegionTransportAgent& Agent =
          AgentsByProfile.FindOrAdd(Properties[It].CapabilityProfileKey).AddDefaulted_GetRef();
        Agent.AgentId = Identities[It].Id;
        Agent.Location = FVector2f(States[It].Location.X, States[It].Location.Y);
        Agent.Velocity = FVector2f(States[It].Velocity.X, States[It].Velocity.Y);
        Agent.MaxSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
        Agent.FarFlowPreferredVelocity =
          FCrowdDemoTargetRegionTransportKernel::ComposeTargetAdvectedFarFlowVelocity(
            FVector2f(Intents[It].DesiredVelocity.X, Intents[It].DesiredVelocity.Y),
            FVector2f(Pipeline->GetTargetApproachFact().Velocity.X,
              Pipeline->GetTargetApproachFact().Velocity.Y),
            Agent.MaxSpeedCmps);
        Agent.PhysicalRadiusCm = Properties[It].PhysicalRadiusCm;
        Agent.HardSafetyGapCm = Properties[It].HardSafetyGapCm;
        Agent.SoftMarginCm = Properties[It].SoftMarginCm;
      }
    });
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
          Pipeline->GetTargetApproachFact().Location.X,
          Pipeline->GetTargetApproachFact().Location.Y)).Size();
        const bool bWasEngaged = Runtime.TargetEngagedHoldAgentIds.Contains(Agent.AgentId);
        const FCrowdDemoTargetRegionAgentDemandState* PreviousDemandState =
          Runtime.Demand.AgentStates.FindByPredicate(
            [&Agent](const FCrowdDemoTargetRegionAgentDemandState& State)
            {
              return State.AgentId == Agent.AgentId;
            });
        const FCrowdDemoTargetEngagementDecision Engagement =
          FCrowdDemoTargetRegionTransportKernel::ResolveTargetEngagement(
            Runtime.Cohort.Profile.TargetDistanceResponsePolicy,
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
        Pipeline->GetRules(), Pipeline->GetTargetApproachFact(), &Runtime.Cohort.Profile,
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
        FCrowdDemoTargetRegionTransportKernel::UpdateStaticDemandPopulation(
          Runtime.Agents, Settings, Pipeline->GetRules().FlowFieldConfig,
          &Pipeline->GetSharedFlowField(), Runtime.Topology, Runtime.Demand,
          ExternalAgents, bRefreshSourceAttachments);
        Pipeline->RecordTargetDemandPerformance(false);
      }
      else
      {
        FCrowdDemoTargetRegionTransportKernel::BuildDemand(
          Runtime.Agents, Settings, Pipeline->GetRules().FlowFieldConfig,
          &Pipeline->GetSharedFlowField(), Runtime.Topology, Runtime.Demand, ExternalAgents);
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
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      auto& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.Location = FVector2f(States[It].Location.X, States[It].Location.Y);
      Agent.Velocity = FVector2f(States[It].Velocity.X, States[It].Velocity.Y);
      Agent.MaxSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
      Agent.FarFlowPreferredVelocity =
        FCrowdDemoTargetRegionTransportKernel::ComposeTargetAdvectedFarFlowVelocity(
          FVector2f(Intents[It].DesiredVelocity.X, Intents[It].DesiredVelocity.Y),
          FVector2f(Pipeline->GetTargetApproachFact().Velocity.X,
            Pipeline->GetTargetApproachFact().Velocity.Y),
          Agent.MaxSpeedCmps);
    }
  });
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  const auto Settings = MakeTargetRegionTransportSettings(
    Pipeline->GetRules(), Pipeline->GetTargetApproachFact());
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
    FCrowdDemoTargetRegionTransportKernel::UpdateStaticDemandPopulation(
      Agents, Settings, Pipeline->GetRules().FlowFieldConfig,
      &Pipeline->GetSharedFlowField(),
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetPreparedTargetRegionDemand(), {}, bRefreshSourceAttachments);
    Pipeline->RecordTargetDemandPerformance(false);
  }
  else
  {
    FCrowdDemoTargetRegionTransportKernel::BuildDemand(
      Agents, Settings, Pipeline->GetRules().FlowFieldConfig,
      &Pipeline->GetSharedFlowField(),
      Pipeline->GetPreparedTargetRegionTopology(), Pipeline->GetPreparedTargetRegionDemand());
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
    const int32 TargetRevision = Pipeline->GetTargetApproachFact().TargetRevision;
    const bool bLifecycleDiagnostic =
      Pipeline->IsTargetRegionPlanLifecycleDiagnosticEnabled();
    int32 RoutedAgentCount = 0;
    for (FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime : Pipeline->GetCapabilityCohorts())
    {
      const FCrowdDemoTargetRegionFlowPlan PreviousPlan = Runtime.Plan;
      const FCrowdDemoTargetRegionQuotaExecutionState PreviousExecution =
        Runtime.QuotaExecution;
      FCrowdDemoTargetRegionPlanValidationResult Validation;
      FCrowdDemoTargetRegionTransportKernel::ValidateQuotaExecutionState(
        Runtime.Topology, Runtime.Demand, Runtime.Plan, Runtime.QuotaExecution,
        TargetRevision, Validation);
      int32 RebuildReason = 0;
      if (!Runtime.Plan.bValid) RebuildReason = 7;
      else if (Runtime.Plan.TargetRevision != TargetRevision) RebuildReason = 2;
      else if (Runtime.Plan.FeasibleGraphHash != Runtime.Topology.FeasibleGraphHash) RebuildReason = 3;
      else if (Runtime.Plan.MembershipHash != Runtime.Demand.MembershipHash) RebuildReason = 4;
      else if (Step - Runtime.Plan.BuildFixedStepIndex >=
        Pipeline->GetRules().TargetRegionTransportSettings.PlanLifetimeSteps) RebuildReason = 1;
      else if (Runtime.Demand.TotalDeficit == 0 && Runtime.Plan.RoutedAgentCount > 0) RebuildReason = 5;
      else if (!Validation.bValid) RebuildReason = 6;
      if (RebuildReason != 0)
      {
        const double Start = FPlatformTime::Seconds();
        FCrowdDemoTargetRegionFlowPlan NewPlan;
        FCrowdDemoTargetRegionQuotaExecutionState NewExecution;
        FCrowdDemoTargetRegionPlanReplacementSummary Replacement;
        FCrowdDemoTargetRegionTransportKernel::ReplacePlanPreservingClaims(
          Runtime.Topology, Runtime.Demand, PreviousPlan, PreviousExecution,
          FMath::Max(1, PreviousPlan.PlanEpoch + 1), Step, TargetRevision,
          NewPlan, NewExecution, Replacement);
        Runtime.SolverMillisecondsSamples.Add(static_cast<float>(
          (FPlatformTime::Seconds() - Start) * 1000.0));
        Runtime.Plan = MoveTemp(NewPlan);
        Runtime.QuotaExecution = MoveTemp(NewExecution);
        Runtime.LastPlanReplacement = Replacement;
        ++Runtime.PlanRebuildCount;
        FCrowdDemoTargetRegionPlanValidationResult InitialValidation;
        FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
          Runtime.Topology, Runtime.Demand, Runtime.Plan, TargetRevision,
          InitialValidation);
        if (!InitialValidation.bValid) Runtime.Plan.bValid = false;
        FCrowdDemoTargetRegionTransportKernel::ValidateQuotaExecutionState(
          Runtime.Topology, Runtime.Demand, Runtime.Plan, Runtime.QuotaExecution,
          TargetRevision, Validation);
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
          Pipeline->GetTargetApproachFact().Location.X,
          Pipeline->GetTargetApproachFact().Location.Y);
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
          FCrowdDemoTargetRegionTransportKernel::ValidateQuotaExecutionState(
            DiagnosticInput.Topology, DiagnosticInput.Demand,
            DiagnosticInput.PreviousPlan, DiagnosticInput.PreviousExecution,
            TargetRevision, DiagnosticInput.PreviousValidation);
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
  const int32 TargetRevision = Pipeline->GetTargetApproachFact().TargetRevision;
  FCrowdDemoTargetRegionPlanValidationResult Validation;
  FCrowdDemoTargetRegionTransportKernel::ValidateQuotaExecutionState(
    Topology, Demand, Plan, Pipeline->GetTargetRegionQuotaExecution(),
    TargetRevision, Validation);
  int32 RebuildReason = 0;
  if (!Plan.bValid) RebuildReason = 7;
  else if (Plan.TargetRevision != TargetRevision) RebuildReason = 2;
  else if (Plan.FeasibleGraphHash != Topology.FeasibleGraphHash) RebuildReason = 3;
  else if (Plan.MembershipHash != Demand.MembershipHash) RebuildReason = 4;
  else if (Step - Plan.BuildFixedStepIndex >=
    Pipeline->GetRules().TargetRegionTransportSettings.PlanLifetimeSteps) RebuildReason = 1;
  else if (Demand.TotalDeficit == 0 && Plan.RoutedAgentCount > 0) RebuildReason = 5;
  else if (!Validation.bValid) RebuildReason = 6;
  float SolverMs = 0.0f;
  if (RebuildReason != 0)
  {
    const double Start = FPlatformTime::Seconds();
    const FCrowdDemoTargetRegionFlowPlan PreviousPlan = Plan;
    const FCrowdDemoTargetRegionQuotaExecutionState PreviousExecution =
      Pipeline->GetTargetRegionQuotaExecution();
    FCrowdDemoTargetRegionFlowPlan NewPlan;
    FCrowdDemoTargetRegionQuotaExecutionState NewExecution;
    FCrowdDemoTargetRegionPlanReplacementSummary Replacement;
    FCrowdDemoTargetRegionTransportKernel::ReplacePlanPreservingClaims(
      Topology, Demand, PreviousPlan, PreviousExecution,
      FMath::Max(1, PreviousPlan.PlanEpoch + 1), Step, TargetRevision,
      NewPlan, NewExecution, Replacement);
    SolverMs = static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0);
    Plan = MoveTemp(NewPlan);
    Pipeline->GetTargetRegionQuotaExecution() = MoveTemp(NewExecution);
    FCrowdDemoTargetRegionPlanValidationResult InitialValidation;
    FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
      Topology, Demand, Plan, TargetRevision, InitialValidation);
    if (!InitialValidation.bValid) Plan.bValid = false;
    FCrowdDemoTargetRegionTransportKernel::ValidateQuotaExecutionState(
      Topology, Demand, Plan, Pipeline->GetTargetRegionQuotaExecution(),
      TargetRevision, Validation);
  }
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
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadWrite);
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
        Pipeline->GetRules(), Pipeline->GetTargetApproachFact(), &Runtime.Cohort.Profile,
        Runtime.DemandRegionPhaseOffset);
      FCrowdDemoTargetRegionTransportKernel::BuildGuidanceWithExecution(
        Runtime.Agents, Settings, Runtime.Topology, Runtime.Demand, Runtime.Plan,
        Runtime.QuotaExecution, Runtime.Guidance, Runtime.GuidanceSummary);
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
    EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
    {
      const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
      const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
      const auto Intents = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundMoveIntentFragment>();
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        const auto* const* Result = ById.Find(Identities[It].Id);
        if (!Result) continue;
        Intents[It].DesiredLocation = FVector(
          Pipeline->GetTargetApproachFact().Location.X,
          Pipeline->GetTargetApproachFact().Location.Y, States[It].Location.Z);
        Intents[It].DesiredVelocity = FVector(
          (*Result)->DesiredVelocity.X, (*Result)->DesiredVelocity.Y, 0.0f);
        Intents[It].PreferredDirection = Intents[It].DesiredVelocity.GetSafeNormal2D();
        Intents[It].DesiredYawDegrees = Intents[It].DesiredVelocity.IsNearlyZero()
          ? States[It].YawDegrees : Intents[It].DesiredVelocity.Rotation().Yaw;
        ++Applied;
      }
    });
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
    Pipeline->GetRules(), Pipeline->GetTargetApproachFact());
  FCrowdDemoTargetRegionTransportKernel::BuildGuidanceWithExecution(
    Pipeline->GetPreparedTargetRegionAgents(), Settings,
    Pipeline->GetPreparedTargetRegionTopology(), Pipeline->GetPreparedTargetRegionDemand(),
    Pipeline->GetPreparedTargetRegionPlan(), Pipeline->GetTargetRegionQuotaExecution(),
    Pipeline->GetPreparedTargetRegionGuidance(),
    Pipeline->GetTargetRegionGuidanceSummary());
  Pipeline->RecordOpenCohortMovementGuidance(
    Pipeline->GetPreparedTargetRegionGuidance());
  TMap<int32, const FCrowdDemoTargetRegionGuidanceResult*> ById;
  for (const auto& Result : Pipeline->GetPreparedTargetRegionGuidance())
    ById.Add(Result.AgentId, &Result);
  int32 Applied = 0;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Intents = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      if (const auto* const* Result = ById.Find(Identities[It].Id))
      {
        Intents[It].DesiredLocation = FVector(
          Pipeline->GetTargetApproachFact().Location.X,
          Pipeline->GetTargetApproachFact().Location.Y, States[It].Location.Z);
        Intents[It].DesiredVelocity = FVector(
          (*Result)->DesiredVelocity.X, (*Result)->DesiredVelocity.Y, 0.0f);
        Intents[It].PreferredDirection = Intents[It].DesiredVelocity.GetSafeNormal2D();
        Intents[It].DesiredYawDegrees = Intents[It].DesiredVelocity.IsNearlyZero()
          ? States[It].YawDegrees : Intents[It].DesiredVelocity.Rotation().Yaw;
        ++Applied;
      }
  });
  Pipeline->RecordTargetRegionGuidanceStep();
  if (!Pipeline->GetTargetRegionGuidanceSummary().bValid)
  {
    FCrowdDemoTargetRegionPlanValidationResult Validation;
    FCrowdDemoTargetRegionTransportKernel::ValidateQuotaExecutionState(
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetPreparedTargetRegionDemand(),
      Pipeline->GetPreparedTargetRegionPlan(),
      Pipeline->GetTargetRegionQuotaExecution(),
      Pipeline->GetTargetApproachFact().TargetRevision,
      Validation);
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

UCrowdDemoRoundTargetSlotLayoutPrepareProcessor::UCrowdDemoRoundTargetSlotLayoutPrepareProcessor()
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundTargetSlotLayoutPrepareProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UCrowdDemoRoundTargetSlotLayoutPrepareProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || Pipeline->GetRules().TargetApproachSettings.bEnabled == 0)
    return;
  const FCrowdDemoRoundRules& Rules = Pipeline->GetRules();
  FCrowdDemoTargetSlotLayoutInput Input;
  Input.Target = Pipeline->GetTargetApproachFact();
  Input.ParticleProfile = Rules.ParticleProfile;
  Input.Settings = Rules.TargetSlotLayoutSettings;
  Input.FlowConfig = Rules.FlowFieldConfig;
  Input.FlowField = &Pipeline->GetSharedFlowField();
  Input.TransitionRingRadiusCm = Rules.TargetApproachSettings.TransitionRingRadiusCm;
  const FCrowdDemoTargetSlotLayout Previous = Pipeline->GetPreparedTargetSlotLayout();
  FCrowdDemoTargetSlotLayout Layout;
  FCrowdDemoTargetSlotLayoutSummary Summary;
  FCrowdDemoTargetSlotLayoutKernel::Build(Input,
    Previous.bValid ? &Previous : nullptr, Layout, Summary);
  Pipeline->GetPreparedTargetSlotLayout() = MoveTemp(Layout);
  Pipeline->GetTargetSlotLayoutSummary() = Summary;
  if (!Summary.bValid)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetSlotLayoutInvalid step=%d candidates=%d topology=%u world=%u input=%u"),
      Pipeline->GetCurrentFixedStepIndex(), Summary.GeneratedCandidateCount,
      Summary.TopologyHash, Summary.WorldValidationHash, Summary.FullInputHash);
    return;
  }
  Pipeline->LogStageOnce(TEXT("03_target_slot_layout_prepare"),
    Pipeline->GetPreparedTargetSlotLayout().Slots.Num());
}

UCrowdDemoRoundTargetApproachScheduleProcessor::UCrowdDemoRoundTargetApproachScheduleProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundTargetApproachScheduleProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassMovementFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoTargetCapabilityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoTargetApproachFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundTargetApproachScheduleProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || Pipeline->GetRules().TargetApproachSettings.bEnabled == 0)
    return;

  const FCrowdDemoRoundRules& Rules = Pipeline->GetRules();
  TArray<FCrowdDemoTargetApproachAgent> Agents;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Movements = ChunkContext.GetFragmentView<FCrowdDemoMassMovementFragment>();
    const auto Capabilities = ChunkContext.GetFragmentView<FCrowdDemoTargetCapabilityFragment>();
    const auto Approaches = ChunkContext.GetFragmentView<FCrowdDemoTargetApproachFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoTargetApproachAgent& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.Location = FVector2f(States[It].Location.X, States[It].Location.Y);
      Agent.Velocity = FVector2f(States[It].Velocity.X, States[It].Velocity.Y);
      Agent.PhysicalRadiusCm = Movements[It].ContactRadiusCm;
      Agent.MaxSpeedCmps = Rules.MaxSpeedCmPerSecond;
      Agent.CapabilityMask = Capabilities[It].CapabilityMask;
      Agent.MinimumFunctionalDistanceCm = Capabilities[It].MinimumFunctionalDistanceCm;
      Agent.MaximumFunctionalDistanceCm = Capabilities[It].MaximumFunctionalDistanceCm;
      Agent.StableBusinessPriority = Capabilities[It].StableBusinessPriority;
      Agent.ExistingState = Approaches[It].State;
      Agent.ExistingSlotId = Approaches[It].AssignedSlotId;
      Agent.ExistingTargetRevision = Approaches[It].TargetRevision;
      Agent.ExistingSlotLayoutRevision = Approaches[It].SlotLayoutRevision;
      Agent.RingEnterFixedStep = Approaches[It].RingEnterFixedStep;
      Agent.StateEnterFixedStep = Approaches[It].StateEnterFixedStep;
    }
  });

  FCrowdDemoTargetApproachSettings Settings;
  Settings.bEnabled = Rules.TargetApproachSettings.bEnabled != 0;
  Settings.TransitionRingRadiusCm = Rules.TargetApproachSettings.TransitionRingRadiusCm;
  Settings.RingEnterToleranceCm = Rules.TargetApproachSettings.RingEnterToleranceCm;
  Settings.RingExitToleranceCm = Rules.TargetApproachSettings.RingExitToleranceCm;
  Settings.ApproachSlowdownDistanceCm = Rules.TargetApproachSettings.ApproachSlowdownDistanceCm;
  Settings.SlotArrivalToleranceCm = Rules.TargetApproachSettings.SlotArrivalToleranceCm;
  Settings.SlotArrivalSpeedToleranceCmps = Rules.TargetApproachSettings.SlotArrivalSpeedToleranceCmps;
  Settings.SlotExitToleranceCm = Rules.TargetApproachSettings.SlotExitToleranceCm;
  Settings.SlotArriveGainPerSecond = Rules.TargetApproachSettings.SlotArriveGainPerSecond;
  Settings.SlotOccupiedGainPerSecond = Rules.TargetApproachSettings.SlotOccupiedGainPerSecond;
  Settings.FreeSettleAttractionGainPerSecond = Rules.TargetApproachSettings.FreeSettleAttractionGainPerSecond;
  Settings.FreeSettleMaxSpeedCmps = Rules.TargetApproachSettings.FreeSettleMaxSpeedCmps;
  Settings.TargetPhysicalRadiusCm = Rules.TargetApproachSettings.TargetPhysicalRadiusCm;
  Settings.TargetHardSafetyGapCm = Rules.TargetApproachSettings.TargetHardSafetyGapCm;
  Settings.TargetSoftMarginCm = Rules.TargetApproachSettings.TargetSoftMarginCm;
  Settings.PositionQuantumCm = Rules.TargetApproachSettings.PositionQuantumCm;
  Settings.VelocityQuantumCmps = Rules.TargetApproachSettings.VelocityQuantumCmps;

  TArray<FCrowdDemoTargetApproachResult>& Results =
    Pipeline->GetPreparedTargetApproachResults();
  FCrowdDemoTargetApproachSummary Summary;
  const FCrowdDemoTargetSlotLayout& Layout = Pipeline->GetPreparedTargetSlotLayout();
  FCrowdDemoTargetApproachKernel::Solve(
    Pipeline->GetTargetApproachFact(), Settings, Layout.Slots, Agents,
    Pipeline->GetCurrentFixedStepIndex(), Results, Summary, Layout.SlotLayoutRevision);
  Summary.ScheduleHash = FoldTargetHash(Summary.ApproachHash, Layout.FullInputHash);
  for (const FCrowdDemoTargetApproachAgent& Agent : Agents)
  {
    const FCrowdDemoTargetApproachResult* Result = Results.FindByPredicate(
      [&Agent](const auto& Item) { return Item.AgentId == Agent.AgentId; });
    if (Agent.ExistingSlotId != INDEX_NONE && Result != nullptr
      && Result->AssignedSlotId == Agent.ExistingSlotId)
      ++Summary.SlotOwnerReusedCount;
    else if (Agent.ExistingSlotId != INDEX_NONE)
      ++Summary.SlotOwnerReleaseCount;
  }
  Pipeline->SetTargetApproachSummary(Summary);
  if (!Layout.bValid || !Summary.bValid || Results.Num() != Agents.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetApproachInvalid step=%d valid=%d agents=%d results=%d duplicate_owner=%d"),
      Pipeline->GetCurrentFixedStepIndex(), Summary.bValid ? 1 : 0,
      Agents.Num(), Results.Num(), Summary.DuplicateSlotOwnerCount);
    return;
  }
  Pipeline->LogStageOnce(TEXT("04_target_approach_schedule"), Results.Num());
}

UCrowdDemoRoundTargetApproachCommitProcessor::UCrowdDemoRoundTargetApproachCommitProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundTargetApproachCommitProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoTargetCapabilityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoTargetApproachFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundTargetApproachCommitProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || Pipeline->GetRules().TargetApproachSettings.bEnabled == 0)
    return;
  const FCrowdDemoTargetSlotLayout& Layout = Pipeline->GetPreparedTargetSlotLayout();
  TArray<FCrowdDemoTargetApproachResult> Results = Pipeline->GetPreparedTargetApproachResults();
  Results.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  struct FAgentFact
  {
    int32 AgentId = INDEX_NONE;
    FCrowdDemoTargetCapabilityFragment Capability;
    FCrowdDemoTargetApproachFragment Existing;
  };
  TArray<FAgentFact> Facts;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Capabilities = ChunkContext.GetFragmentView<FCrowdDemoTargetCapabilityFragment>();
    const auto Approaches = ChunkContext.GetFragmentView<FCrowdDemoTargetApproachFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FAgentFact& Fact = Facts.AddDefaulted_GetRef();
      Fact.AgentId = Identities[It].Id;
      Fact.Capability = Capabilities[It];
      Fact.Existing = Approaches[It];
    }
  });
  Facts.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  TArray<FCrowdDemoTargetApproachCommitAgent> CommitAgents;
  for (const FAgentFact& Fact : Facts)
  {
    FCrowdDemoTargetApproachCommitAgent& Agent = CommitAgents.AddDefaulted_GetRef();
    Agent.AgentId = Fact.AgentId;
    Agent.CapabilityMask = Fact.Capability.CapabilityMask;
    Agent.MinimumFunctionalDistanceCm = Fact.Capability.MinimumFunctionalDistanceCm;
    Agent.MaximumFunctionalDistanceCm = Fact.Capability.MaximumFunctionalDistanceCm;
  }
  FCrowdDemoTargetApproachCommitValidation CommitValidation;
  FCrowdDemoTargetApproachKernel::ValidateAtomicCommit(
    Layout.SlotLayoutRevision, Layout.Slots, CommitAgents, Results, CommitValidation);
  bool bValid = Layout.bValid && Pipeline->GetTargetApproachSummary().bValid
    && Results.Num() == Facts.Num() && CommitValidation.bValid;
  TSet<int32> Owners;
  const uint32 CommitHash = CommitValidation.CommitHash;
  for (int32 Index = 0; Index < Results.Num() && Index < Facts.Num(); ++Index)
  {
    const FCrowdDemoTargetApproachResult& Result = Results[Index];
    const FAgentFact& Fact = Facts[Index];
    if (Result.AgentId != Fact.AgentId
      || Result.SlotLayoutRevision != Layout.SlotLayoutRevision)
      bValid = false;
    const FCrowdDemoTargetSlotSpec* Slot = Result.AssignedSlotId == INDEX_NONE ? nullptr
      : Layout.Slots.FindByPredicate([&Result](const auto& Candidate)
        { return Candidate.SlotId == Result.AssignedSlotId; });
    if (Result.AssignedSlotId != INDEX_NONE)
    {
      if (Slot == nullptr || Owners.Contains(Result.AssignedSlotId))
        bValid = false;
      else if (Slot->Kind == ECrowdDemoTargetSlotKind::Functional
        && ((Slot->RequiredCapabilityMask != 0
          && (Fact.Capability.CapabilityMask & Slot->RequiredCapabilityMask)
            != Slot->RequiredCapabilityMask)
          || Slot->CenterDistanceCm < Fact.Capability.MinimumFunctionalDistanceCm
          || Slot->CenterDistanceCm > Fact.Capability.MaximumFunctionalDistanceCm))
        bValid = false;
      Owners.Add(Result.AssignedSlotId);
    }
    const bool bSlotState = Result.State == ECrowdDemoTargetApproachState::SlotIngress
      || Result.State == ECrowdDemoTargetApproachState::SlotOccupied;
    if (bSlotState != (Result.AssignedSlotId != INDEX_NONE))
      bValid = false;
  }
  FCrowdDemoTargetApproachSummary Summary = Pipeline->GetTargetApproachSummary();
  if (!bValid)
  {
    Summary.SlotOwnerConflictCount += CommitValidation.OwnerConflictCount;
    Summary.SlotLayoutRevisionMismatchCount += CommitValidation.RevisionMismatchCount;
    Pipeline->SetTargetApproachSummary(Summary);
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoTargetApproachCommitInvalid step=%d layout_valid=%d schedule_valid=%d agents=%d results=%d layout_revision=%d"),
      Pipeline->GetCurrentFixedStepIndex(), Layout.bValid ? 1 : 0,
      Summary.bValid ? 1 : 0, Facts.Num(), Results.Num(), Layout.SlotLayoutRevision);
    return;
  }
  TMap<int32, const FCrowdDemoTargetApproachResult*> ByAgentId;
  for (const auto& Result : Results)
    ByAgentId.Add(Result.AgentId, &Result);
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Approaches = ChunkContext.GetMutableFragmentView<FCrowdDemoTargetApproachFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdDemoTargetApproachResult* const* Result = ByAgentId.Find(Identities[It].Id);
      check(Result != nullptr);
      FCrowdDemoTargetApproachFragment& Fragment = Approaches[It];
      const bool bChanged = Fragment.State != (*Result)->State
        || Fragment.AssignedSlotId != (*Result)->AssignedSlotId
        || Fragment.SlotLayoutRevision != Layout.SlotLayoutRevision;
      Fragment.State = (*Result)->State;
      Fragment.TargetId = Pipeline->GetTargetApproachFact().TargetId;
      Fragment.TargetRevision = Pipeline->GetTargetApproachFact().TargetRevision;
      Fragment.SlotLayoutRevision = Layout.SlotLayoutRevision;
      Fragment.AssignedSlotId = (*Result)->AssignedSlotId;
      Fragment.RingEnterFixedStep = (*Result)->RingEnterFixedStep;
      Fragment.StateEnterFixedStep = (*Result)->StateEnterFixedStep;
      Fragment.StableSettleSteps = (*Result)->bSettled ? Fragment.StableSettleSteps + 1 : 0;
      Fragment.StateRevision += bChanged ? 1 : 0;
    }
  });
  Summary.CommitHash = CommitHash;
  Pipeline->SetTargetApproachSummary(Summary);
  Pipeline->SetTargetApproachCommitHash(CommitHash);
  Pipeline->GetPreparedTargetApproachGuidance() = MoveTemp(Results);
  Pipeline->LogStageOnce(TEXT("05_target_approach_commit"), Facts.Num());
}

UCrowdDemoRoundTargetApproachGuidanceProcessor::UCrowdDemoRoundTargetApproachGuidanceProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundTargetApproachGuidanceProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoTargetApproachFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundTargetApproachGuidanceProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || Pipeline->GetRules().TargetApproachSettings.bEnabled == 0)
    return;
  TMap<int32, const FCrowdDemoTargetApproachResult*> ResultByAgentId;
  for (const FCrowdDemoTargetApproachResult& Result : Pipeline->GetPreparedTargetApproachGuidance())
    ResultByAgentId.Add(Result.AgentId, &Result);
  int32 AppliedCount = 0;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Approaches = ChunkContext.GetFragmentView<FCrowdDemoTargetApproachFragment>();
    const auto Intents = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdDemoTargetApproachResult* const* Result = ResultByAgentId.Find(Identities[It].Id);
      if (Result == nullptr)
        continue;
      if (Approaches[It].SlotLayoutRevision
        != Pipeline->GetPreparedTargetSlotLayout().SlotLayoutRevision
        || Approaches[It].State != (*Result)->State
        || Approaches[It].AssignedSlotId != (*Result)->AssignedSlotId)
        continue;
      if (Approaches[It].State == ECrowdDemoTargetApproachState::Approach
        && !FCrowdDemoSharedFlowFieldKernel::CanTraverseWorldSegment(
          Pipeline->GetRules().FlowFieldConfig,
          States[It].Location,
          FVector((*Result)->DesiredLocation.X, (*Result)->DesiredLocation.Y,
            States[It].Location.Z)))
        continue;
      FCrowdDemoRoundMoveIntentFragment& Intent = Intents[It];
      Intent.DesiredLocation = FVector((*Result)->DesiredLocation.X,
        (*Result)->DesiredLocation.Y, States[It].Location.Z);
      Intent.DesiredVelocity = FVector((*Result)->DesiredVelocity.X,
        (*Result)->DesiredVelocity.Y, 0.0f);
      Intent.PreferredDirection = Intent.DesiredVelocity.GetSafeNormal();
      Intent.DesiredYawDegrees = Intent.DesiredVelocity.IsNearlyZero()
        ? States[It].YawDegrees : Intent.DesiredVelocity.Rotation().Yaw;
      ++AppliedCount;
    }
  });
  Pipeline->LogStageOnce(TEXT("06_target_approach_guidance"), AppliedCount);
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
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadWrite);
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
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Intents = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const auto Stats = ChunkContext.GetMutableFragmentView<FCrowdDemoMassStatsFragment>();
    const auto Businesses = ChunkContext.GetMutableFragmentView<FCrowdDemoBusinessStateFragment>();
    const auto Attacks = ChunkContext.GetMutableFragmentView<FCrowdDemoRangedAttackFragment>();
    const auto Reactives = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionFragment>();
    const auto Steps = ChunkContext.GetMutableFragmentView<FCrowdDemoReactiveMotionStepFragment>();
    const auto HitFlashes = ChunkContext.GetMutableFragmentView<FCrowdDemoHitFlashFragment>();
    const auto Visuals = ChunkContext.GetMutableFragmentView<FCrowdDemoMassVisualFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      auto Agent = MakeCombatAgentState(
        Identities[It], Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
      const auto StepResult = FCrowdDemoCombatStateKernel::AdvanceReactiveMotion(
        Pipeline->GetCurrentFixedStepIndex(), States[It].Location.Z, Settings, Agent);
      Steps[It] = FCrowdDemoReactiveMotionStepFragment();
      if (!Agent.bAlive)
      {
        Intents[It].DesiredVelocity = FVector::ZeroVector;
      }
      else if (StepResult.bValid && Agent.ReactiveMode != ECrowdDemoReactiveMotionMode::None)
      {
        Intents[It].DesiredVelocity.X = StepResult.HorizontalVelocity.X;
        Intents[It].DesiredVelocity.Y = StepResult.HorizontalVelocity.Y;
        Intents[It].DesiredYawDegrees = StepResult.HorizontalVelocity.IsNearlyZero()
          ? States[It].YawDegrees : StepResult.HorizontalVelocity.Rotation().Yaw;
        Steps[It].bActive = true;
        Steps[It].ProposedZ = StepResult.NewZ;
        Steps[It].VerticalVelocityCmps = StepResult.NewVerticalVelocityCmps;
      }
      ApplyCombatAgentState(
        Agent, Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
    }
  });
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
  TArray<FCrowdDemoSoftPressureRollbackCombatState> RollbackCombatStates;
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
        FCrowdDemoSoftPressureRollbackCombatState& Rollback =
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
    Pipeline->CompleteSoftPressureRollbackCombatState(
      Pipeline->GetCurrentFixedStepIndex(), RollbackCombatStates);
    Pipeline->RecordParticleAppliedStateHash(
      FCrowdDemoParticleConstraintKernel::HashAppliedRoundSimState(
        Pipeline->GetCurrentRoundId(), Pipeline->GetCurrentPlanRevision(),
        Pipeline->GetCurrentFixedStepIndex(), Pipeline->GetCurrentStepEndServerTimeSeconds(),
        ParticleAppliedStates));
  }
}

UCrowdDemoRoundLocalPredictiveInteractionProcessor::UCrowdDemoRoundLocalPredictiveInteractionProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundLocalPredictiveInteractionProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoParticlePropertiesFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundLocalVelocityFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundLocalPredictiveInteractionProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive()
    || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || Pipeline->GetRules().LocalPredictiveSettings.bEnabled == 0)
    return;

  TMap<int32, int32> PreviousBlockedAgeByAgentId;
  for (const FCrowdDemoLocalPredictiveResult& Previous
    : Pipeline->GetPreparedLocalPredictiveResults())
  {
    PreviousBlockedAgeByAgentId.Add(Previous.AgentId, Previous.NextBlockedAgeSteps);
  }

  TArray<FCrowdDemoLocalPredictiveAgent> Agents;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const auto Properties = ChunkContext.GetFragmentView<FCrowdDemoParticlePropertiesFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoLocalPredictiveAgent& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.Position = FVector2f(States[It].Location.X, States[It].Location.Y);
      Agent.Velocity = FVector2f(States[It].Velocity.X, States[It].Velocity.Y);
      Agent.PreferredVelocity = FVector2f(
        Intents[It].DesiredVelocity.X, Intents[It].DesiredVelocity.Y);
      Agent.PhysicalRadiusCm = Properties[It].PhysicalRadiusCm;
      Agent.HardSafetyGapCm = Properties[It].HardSafetyGapCm;
      Agent.MaxSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
      Agent.BlockedAgeSteps = PreviousBlockedAgeByAgentId.FindRef(Agent.AgentId);
    }
  });
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });

  const FCrowdDemoLocalPredictiveRuleSettings& Rule =
    Pipeline->GetRules().LocalPredictiveSettings;
  FCrowdDemoLocalPredictiveSettings Settings;
  Settings.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
  Settings.TimeHorizonSeconds = Rule.TimeHorizonSeconds;
  Settings.SpatialCellSizeCm = Rule.SpatialCellSizeCm;
  Settings.VelocityQuantumCmps = Pipeline->GetRules().ParticleVelocityQuantumCmps;
  Settings.ConstraintEpsilonCmps = Rule.ConstraintEpsilonCmps;
  Settings.RequestedProgressThresholdCmps = Rule.RequestedProgressThresholdCmps;
  Settings.BlockedProgressThresholdCmps = Rule.BlockedProgressThresholdCmps;
  Settings.GrantedResponsibility = Rule.GrantedResponsibility;
  Settings.GrantDurationSteps = Rule.GrantDurationSteps;
  Settings.JointIterationCount = Rule.JointIterationCount;

  TArray<FCrowdDemoLocalPredictivePair> ConflictPairs;
  TArray<FCrowdDemoLocalPredictiveGrantState> GrantStates;
  TArray<FCrowdDemoLocalPredictiveResult> Results;
  FCrowdDemoLocalPredictiveSummary Summary;
  const TArray<FCrowdDemoLocalPredictiveGrantState> PreviousGrantStates =
    Pipeline->GetLocalPredictiveGrantStates();
  FCrowdDemoLocalPredictiveDiagnosticTrace DiagnosticTrace;
  const bool bCaptureDiagnostic = Pipeline->IsTargetStabilityDiagnosticEnabled();
  FCrowdDemoLocalPredictiveInteractionKernel::Solve(
    Agents, Pipeline->GetRules().FlowFieldConfig, Settings,
    PreviousGrantStates, ConflictPairs, GrantStates,
    Results, Summary, bCaptureDiagnostic ? &DiagnosticTrace : nullptr);

  if (bCaptureDiagnostic)
  {
    FCrowdDemoLocalPredictiveDiagnosticFrame Frame;
    Frame.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
    Frame.Settings = Settings;
    Frame.Agents = Agents;
    Frame.PreviousGrantStates = PreviousGrantStates;
    Frame.ConflictPairs = ConflictPairs;
    Frame.GrantStates = GrantStates;
    Frame.Results = Results;
    Frame.Summary = Summary;
    Frame.Trace = MoveTemp(DiagnosticTrace);
    Pipeline->RecordLocalPredictiveDiagnosticFrame(MoveTemp(Frame));
  }

  TMap<int32, const FCrowdDemoLocalPredictiveResult*> ResultByAgentId;
  for (const FCrowdDemoLocalPredictiveResult& Result : Results)
    ResultByAgentId.Add(Result.AgentId, &Result);

  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const auto LocalVelocities =
      ChunkContext.GetMutableFragmentView<FCrowdDemoRoundLocalVelocityFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoRoundLocalVelocityFragment& Fragment = LocalVelocities[It];
      Fragment = FCrowdDemoRoundLocalVelocityFragment();
      Fragment.PlanRevision = Intents[It].PlanRevision;
      if (const FCrowdDemoLocalPredictiveResult* const* Found =
        ResultByAgentId.Find(Identities[It].Id))
      {
        const FCrowdDemoLocalPredictiveResult& Result = **Found;
        Fragment.Velocity = FVector(Result.Velocity.X, Result.Velocity.Y, 0.0f);
        Fragment.NeighborCount = Result.NeighborCount;
        Fragment.ConstraintCount = Result.ConstraintCount;
        Fragment.BlockedAgeSteps = Result.NextBlockedAgeSteps;
        Fragment.ComponentKey = Result.ComponentKey;
        Fragment.GrantEpoch = Result.GrantEpoch;
        Fragment.bAdjusted = Result.bAdjusted;
        Fragment.bGranted = Result.bGranted;
        Fragment.bYielding = Result.bYielding;
        Fragment.bValid = Result.bValid;
      }
    }
  });

  Pipeline->RecordLocalPredictiveStep(MoveTemp(Results), MoveTemp(GrantStates), Summary);
  Pipeline->LogStageOnce(TEXT("05_local_predictive_interaction"), Agents.Num());
}

UCrowdDemoRoundMovementPredictProcessor::UCrowdDemoRoundMovementPredictProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundMovementPredictProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundLocalVelocityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionStepFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundProposedMovementFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoOpenSpawnRelaxationFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundMovementPredictProcessor::Execute(
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
  const float FixedStep = Pipeline->GetCurrentFixedStepSeconds();
  int32 AgentCount = 0;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const TConstArrayView<FCrowdDemoRoundMoveIntentFragment> Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const auto LocalVelocities = ChunkContext.GetFragmentView<FCrowdDemoRoundLocalVelocityFragment>();
    const auto ReactiveSteps = ChunkContext.GetFragmentView<FCrowdDemoReactiveMotionStepFragment>();
    const TArrayView<FCrowdDemoRoundProposedMovementFragment> Proposed = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundProposedMovementFragment>();
    const auto OpenSpawnStates = ChunkContext.GetMutableFragmentView<FCrowdDemoOpenSpawnRelaxationFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (Pipeline->IsOpenSpawnRelaxation())
      {
        auto& Participation = OpenSpawnStates[It];
        const FVector Start = Participation.bPendingBoundaryReset
          ? Participation.BoundaryResetLocation : States[It].Location;
        Proposed[It].StartLocation = Start;
        Proposed[It].ProposedLocation = Start;
        Proposed[It].ProposedVelocity = FVector::ZeroVector;
        if (Participation.bPendingBoundaryReset)
        {
          Participation.bPendingBoundaryReset = false;
          if (auto* RuntimeAgent = Pipeline->GetOpenSpawnRelaxationRuntime().Agents.FindByPredicate(
            [&](const auto& Agent) { return Agent.AgentId == Identities[It].Id; }))
            RuntimeAgent->bPendingBoundaryReset = false;
        }
        ++AgentCount;
        continue;
      }
      Proposed[It].StartLocation = States[It].Location;
      FVector ProposedVelocity = Intents[It].DesiredVelocity;
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
        && Pipeline->GetRules().LocalPredictiveSettings.bEnabled != 0)
      {
        ProposedVelocity = LocalVelocities[It].bValid
          ? LocalVelocities[It].Velocity.GetClampedToMaxSize2D(
              Pipeline->GetRules().MaxSpeedCmPerSecond)
          : FVector::ZeroVector;
      }
      Proposed[It].ProposedVelocity = ProposedVelocity;
      Proposed[It].ProposedLocation = States[It].Location + ProposedVelocity * FixedStep;
      if (ReactiveSteps[It].bActive)
      {
        Proposed[It].ProposedLocation.Z = ReactiveSteps[It].ProposedZ;
        Proposed[It].ProposedVelocity.Z = ReactiveSteps[It].VerticalVelocityCmps;
      }
      else
      {
        Proposed[It].ProposedLocation.Z = States[It].Location.Z;
      }
      ++AgentCount;
    }
  });
  Pipeline->LogStageOnce(
    Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("05_movement_predict")
      : TEXT("04_movement_predict"),
    AgentCount);
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
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundLocalVelocityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoParticlePropertiesFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoOpenSpawnRelaxationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundParticleConstraintFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
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

  TArray<FCrowdDemoParticleConstraintAgent> Agents;
  TMap<int32, uint32> CapabilityProfileKeyByAgentId;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Proposed = ChunkContext.GetFragmentView<FCrowdDemoRoundProposedMovementFragment>();
    const auto Properties = ChunkContext.GetFragmentView<FCrowdDemoParticlePropertiesFragment>();
    const auto OpenSpawnStates = ChunkContext.GetFragmentView<FCrowdDemoOpenSpawnRelaxationFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (Pipeline->IsOpenSpawnRelaxation() && !OpenSpawnStates[It].bParticleActive)
        continue;
      FCrowdDemoParticleConstraintAgent& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.StartPosition = Proposed[It].StartLocation;
      Agent.PredictedPosition = Proposed[It].ProposedLocation;
      Agent.PhysicalRadiusCm = Properties[It].PhysicalRadiusCm;
      Agent.HardSafetyGapCm = Properties[It].HardSafetyGapCm;
      Agent.EnvironmentHardClearanceCm = Pipeline->GetRules().bEnableHeterogeneousProfiles != 0
        ? Pipeline->GetRules().FlowFieldConfig.AgentInflateCm
        : 0.0f;
      Agent.SoftMarginCm = Properties[It].SoftMarginCm;
      Agent.Mobility = Properties[It].Mobility;
      CapabilityProfileKeyByAgentId.Add(
        Agent.AgentId, Properties[It].CapabilityProfileKey);
    }
  });
  constexpr int32 TargetParticleId = -1000000001;
  const bool bHasTargetParticle = Pipeline->GetRules().TargetApproachSettings.bEnabled != 0
    || Pipeline->GetRules().TargetInfluenceSettings.bEnabled != 0;
  if (bHasTargetParticle)
  {
    const FCrowdDemoTargetFact& Target = Pipeline->GetTargetApproachFact();
    FCrowdDemoParticleConstraintAgent& TargetAgent = Agents.AddDefaulted_GetRef();
    TargetAgent.AgentId = TargetParticleId;
    const FVector CurrentTarget(Target.Location.X, Target.Location.Y, 60.0f);
    const FVector TargetVelocity(Target.Velocity.X, Target.Velocity.Y, 0.0f);
    TargetAgent.StartPosition = CurrentTarget - TargetVelocity * Pipeline->GetCurrentFixedStepSeconds();
    TargetAgent.PredictedPosition = CurrentTarget;
    TargetAgent.PhysicalRadiusCm = Target.PhysicalRadiusCm;
    TargetAgent.HardSafetyGapCm = Pipeline->GetRules().TargetInfluenceSettings.bEnabled != 0
      ? Pipeline->GetRules().TargetInfluenceSettings.TargetHardSafetyGapCm
      : Pipeline->GetRules().TargetApproachSettings.TargetHardSafetyGapCm;
    TargetAgent.SoftMarginCm = Pipeline->GetRules().TargetInfluenceSettings.bEnabled != 0
      ? Pipeline->GetRules().TargetInfluenceSettings.TargetSoftMarginCm
      : Pipeline->GetRules().TargetApproachSettings.TargetSoftMarginCm;
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
  const bool bExecutionDiagnostic = Pipeline->IsTargetInfluenceExecutionDiagnosticEnabled();
  const bool bStabilityDiagnostic = Pipeline->IsTargetStabilityDiagnosticEnabled();
  Settings.bCaptureRouteDiagnostic = bRouteDiagnostic || bExecutionDiagnostic || bStabilityDiagnostic
    || Pipeline->IsOpenSpawnRelaxation();
  TArray<FCrowdDemoParticleConstraintPair> Pairs;
  TArray<FCrowdDemoParticleConstraintResult> Results;
  FCrowdDemoParticleConstraintSummary Summary;
  FCrowdDemoParticleConstraintTrace Trace;
  const bool bCaptureParticleTrace = Settings.bCaptureRouteDiagnostic;
  bool bParticleTraceCaptured = bCaptureParticleTrace;
  const double StartSeconds = FPlatformTime::Seconds();
  FCrowdDemoParticleConstraintKernel::Solve(
    Agents, Environment, Settings, Pairs, Results, Summary,
    bCaptureParticleTrace ? &Trace : nullptr);
  const float SolverMilliseconds = static_cast<float>(
    (FPlatformTime::Seconds() - StartSeconds) * 1000.0);

  TMap<int32, const FCrowdDemoParticleConstraintResult*> ResultsByAgentId;
  for (const auto& Result : Results) ResultsByAgentId.Add(Result.AgentId, &Result);
  TMap<int32, int32> TraceIndexByAgentId;
  if (Settings.bCaptureRouteDiagnostic)
    for (int32 TraceIndex = 0; TraceIndex < Trace.AgentIds.Num(); ++TraceIndex)
      TraceIndexByAgentId.Add(Trace.AgentIds[TraceIndex], TraceIndex);
  TArray<FCrowdDemoParticleAppliedState> AppliedStates;
  TArray<FCrowdDemoSoftPressureRouteStepSample> RouteSamples;
  TArray<FCrowdDemoTargetInfluenceExecutionSample> ExecutionSamples;
  FCrowdDemoTargetStabilityStepSample StabilityStep;
  TMap<int32, const FCrowdDemoTargetRegionGuidanceResult*> StabilityGuidanceByAgentId;
  TMap<int32, const FCrowdDemoTargetRegionAgentDemandState*> StabilityDemandByAgentId;
  TMap<int32, int32> StabilitySurplusByAgentId;
  if (bStabilityDiagnostic)
  {
    StabilityStep.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
    StabilityStep.TargetRevision = Pipeline->GetTargetApproachFact().TargetRevision;
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
  TMap<int32, const FCrowdDemoTargetInfluenceResult*> InfluenceByAgentId;
  if (bExecutionDiagnostic)
    for (const auto& Influence : Pipeline->GetPreparedTargetInfluenceResults())
      InfluenceByAgentId.Add(Influence.AgentId, &Influence);
  TMap<int32, FVector> FlowDirectionByAgentId;
  AppliedStates.Reserve(Agents.Num());
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Proposed = ChunkContext.GetFragmentView<FCrowdDemoRoundProposedMovementFragment>();
    const auto Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const auto LocalVelocities =
      ChunkContext.GetFragmentView<FCrowdDemoRoundLocalVelocityFragment>();
    const auto FlowSamples = ChunkContext.GetFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    const auto Properties = ChunkContext.GetFragmentView<FCrowdDemoParticlePropertiesFragment>();
    const auto OpenSpawnStates = ChunkContext.GetFragmentView<FCrowdDemoOpenSpawnRelaxationFragment>();
    const auto Outputs = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundParticleConstraintFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoRoundParticleConstraintFragment& Output = Outputs[It];
      if (Pipeline->IsOpenSpawnRelaxation() && !OpenSpawnStates[It].bParticleActive)
      {
        Output.CorrectedLocation = Proposed[It].ProposedLocation;
        Output.CorrectedVelocity = FVector::ZeroVector;
        Output.RealizedCorrection = FVector::ZeroVector;
        Output.FirstInfluencedIteration = INDEX_NONE;
        Output.bValid = true;
        continue;
      }
      const FCrowdDemoParticleConstraintResult* const* Found = ResultsByAgentId.Find(Identities[It].Id);
      if (!Summary.bValid || !Found)
      {
        Output.CorrectedLocation = Proposed[It].StartLocation;
        Output.CorrectedVelocity = FVector::ZeroVector;
        Output.RealizedCorrection = Output.CorrectedLocation - Proposed[It].ProposedLocation;
        Output.FirstInfluencedIteration = INDEX_NONE;
        Output.bValid = false;
        FCrowdDemoParticleAppliedState& Applied = AppliedStates.AddDefaulted_GetRef();
        Applied.AgentId = Identities[It].Id;
        Applied.Position = Output.CorrectedLocation;
        Applied.Velocity = Output.CorrectedVelocity;
        continue;
      }
      Output.CorrectedLocation = (*Found)->CorrectedPosition;
      Output.CorrectedVelocity = (*Found)->CorrectedVelocity;
      Output.RealizedCorrection = (*Found)->RealizedCorrection;
      Output.FirstInfluencedIteration = (*Found)->FirstInfluencedIteration;
      Output.bValid = true;
      FCrowdDemoParticleAppliedState& Applied = AppliedStates.AddDefaulted_GetRef();
      Applied.AgentId = Identities[It].Id;
      Applied.Position = Output.CorrectedLocation;
      Applied.Velocity = Output.CorrectedVelocity;
      if (bRouteDiagnostic)
      {
        FCrowdDemoSoftPressureRouteStepSample& Route = RouteSamples.AddDefaulted_GetRef();
        Route.AgentId = Identities[It].Id;
        Route.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
        Route.PredictStartLocation = Proposed[It].StartLocation;
        Route.Location = Output.CorrectedLocation;
        Route.Goal = FVector(Pipeline->GetRules().FlowFieldConfig.GoalLocation);
        Route.FlowCellIndex = FlowSamples[It].CellIndex;
        Route.FlowStableCellKey = FlowSamples[It].StableCellKey;
        Route.FlowStatus = FlowSamples[It].Status;
        Route.IntegrationCost = FlowSamples[It].IntegrationCost;
        Route.FlowDirection = FlowSamples[It].FlowDirection;
        Route.DesiredVelocity = Intents[It].DesiredVelocity;
        Route.PredictedVelocity = Proposed[It].ProposedVelocity;
        Route.AppliedVelocity = Output.CorrectedVelocity;
        Route.TotalParticleCorrection = Output.RealizedCorrection;
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
      if (bExecutionDiagnostic)
      {
        FCrowdDemoTargetInfluenceExecutionSample& Sample = ExecutionSamples.AddDefaulted_GetRef();
        Sample.AgentId = Identities[It].Id;
        Sample.TargetRevision = Pipeline->GetTargetApproachFact().TargetRevision;
        Sample.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
        Sample.Location = FVector2f(Output.CorrectedLocation.X, Output.CorrectedLocation.Y);
        Sample.TargetLocation = Pipeline->GetTargetApproachFact().Location;
        Sample.MovementPredictVelocity = FVector2f(
          Proposed[It].ProposedVelocity.X, Proposed[It].ProposedVelocity.Y);
        Sample.AppliedVelocity = FVector2f(
          Output.CorrectedVelocity.X, Output.CorrectedVelocity.Y);
        Sample.FixedStepSeconds = Settings.FixedStepSeconds;
        Sample.PhysicalRadiusCm = Properties[It].PhysicalRadiusCm;
        Sample.HardSafetyGapCm = Properties[It].HardSafetyGapCm;
        if (const FCrowdDemoTargetInfluenceResult* const* Influence =
          InfluenceByAgentId.Find(Sample.AgentId))
        {
          Sample.DensityRequestedVelocity = (*Influence)->DensityVelocity
            * (static_cast<float>((*Influence)->InfluenceWeightQ15) / 32767.0f);
          Sample.InfluenceDesiredVelocity = (*Influence)->DesiredVelocity;
          Sample.DensityDirectionSign = (*Influence)->DensityDirectionSign;
          Sample.DensityLeftWeight = (*Influence)->DensityLeftWeight;
          Sample.DensityCurrentWeight = (*Influence)->DensityCurrentWeight;
          Sample.DensityRightWeight = (*Influence)->DensityRightWeight;
        }
        const FVector2f Offset = Sample.Location - Sample.TargetLocation;
        const int32 SectorCount = FMath::Max(1,
          Pipeline->GetRules().TargetInfluenceSettings.AngularSectorCount);
        const float Angle = FMath::Atan2(Offset.Y, Offset.X);
        const float PositiveAngle = Angle < 0.0f ? Angle + 2.0f * PI : Angle;
        Sample.AngularSectorIndex = FMath::Clamp(FMath::FloorToInt(
          PositiveAngle / (2.0f * PI) * static_cast<float>(SectorCount)), 0, SectorCount - 1);
        Sample.RadialBandIndex = FMath::Max(0, FMath::FloorToInt(
          Offset.Size() / FMath::Max(1.0f,
            Pipeline->GetRules().TargetInfluenceSettings.RadialBandWidthCm)));
        if (const int32* TraceIndex = TraceIndexByAgentId.Find(Sample.AgentId))
        {
          auto To2D = [](const FVector& Value) { return FVector2f(Value.X, Value.Y); };
          if (Trace.PairSoftRealizedCorrections.IsValidIndex(*TraceIndex))
            Sample.PairSoftCorrection = To2D(Trace.PairSoftRealizedCorrections[*TraceIndex]);
          if (Trace.EnvironmentSoftRealizedCorrections.IsValidIndex(*TraceIndex))
            Sample.EnvironmentSoftCorrection =
              To2D(Trace.EnvironmentSoftRealizedCorrections[*TraceIndex]);
          if (Trace.UnifiedHardCorrections.IsValidIndex(*TraceIndex))
            Sample.UnifiedHardCorrection = To2D(Trace.UnifiedHardCorrections[*TraceIndex]);
        }
        const FVector Accounted(
          Sample.PairSoftCorrection.X + Sample.EnvironmentSoftCorrection.X
            + Sample.UnifiedHardCorrection.X,
          Sample.PairSoftCorrection.Y + Sample.EnvironmentSoftCorrection.Y
            + Sample.UnifiedHardCorrection.Y, 0.0f);
        const FVector Residual = Output.RealizedCorrection - Accounted;
        Sample.FinalSafetyCorrection = FVector2f(Residual.X, Residual.Y);
      }
      if (bStabilityDiagnostic)
      {
        FCrowdDemoTargetStabilityAgentSample& Sample =
          StabilityStep.Agents.AddDefaulted_GetRef();
        Sample.AgentId = Identities[It].Id;
        Sample.CohortKey = CapabilityProfileKeyByAgentId.FindRef(Sample.AgentId);
        Sample.Location = FVector2f(Output.CorrectedLocation.X, Output.CorrectedLocation.Y);
        Sample.Velocity = FVector2f(Output.CorrectedVelocity.X, Output.CorrectedVelocity.Y);
        Sample.AppliedVelocity = Sample.Velocity;
        Sample.TargetLocation = Pipeline->GetTargetApproachFact().Location;
        Sample.TargetVelocity = Pipeline->GetTargetApproachFact().Velocity;
        Sample.TotalParticleCorrection = FVector2f(
          Output.RealizedCorrection.X, Output.RealizedCorrection.Y);
        Sample.LocalVelocity = FVector2f(
          LocalVelocities[It].Velocity.X, LocalVelocities[It].Velocity.Y);
        Sample.PredictedVelocity = FVector2f(
          Proposed[It].ProposedVelocity.X, Proposed[It].ProposedVelocity.Y);
        Sample.LocalNeighborCount = LocalVelocities[It].NeighborCount;
        Sample.LocalConstraintCount = LocalVelocities[It].ConstraintCount;
        Sample.LocalBlockedAgeSteps = LocalVelocities[It].BlockedAgeSteps;
        Sample.bLocalValid = LocalVelocities[It].bValid;
        Sample.bLocalGranted = LocalVelocities[It].bGranted;
        Sample.bLocalYielding = LocalVelocities[It].bYielding;
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
          Sample.DesiredVelocity = FVector2f(
            Intents[It].DesiredVelocity.X, Intents[It].DesiredVelocity.Y);
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
  uint32 AppliedStateHash = 2166136261u;
  FCrowdDemoParticleConstraintKernel::EvaluateAppliedState(
    Agents, AppliedStates, Environment, AppliedSummary, AppliedStateHash);
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
  if (bExecutionDiagnostic)
  {
    TArray<int32> OccupiedCellKeys;
    const int32 SectorCount = FMath::Max(1,
      Pipeline->GetRules().TargetInfluenceSettings.AngularSectorCount);
    for (const auto& Sample : ExecutionSamples)
      OccupiedCellKeys.Add(Sample.RadialBandIndex * SectorCount + Sample.AngularSectorIndex);
    const float Width = FMath::Max(1.0f,
      Pipeline->GetRules().TargetInfluenceSettings.RadialBandWidthCm);
    const int32 BandCount = FMath::Max(1, FMath::CeilToInt(
      (Pipeline->GetRules().TargetInfluenceSettings.DefaultMaximumCombatCenterDistanceCm
        + Pipeline->GetRules().TargetInfluenceSettings.InfluenceBlendWidthCm) / Width));
    FCrowdDemoTargetPolarEnvironmentSummary EnvironmentSummary;
    FCrowdDemoTargetInfluenceExecutionDiagnosticKernel::BuildEnvironmentFeasibility(
      Pipeline->GetTargetApproachFact().Location, SectorCount, BandCount, Width,
      Pipeline->GetRules().GetParticleEnvironmentHardClearanceCm(),
      Pipeline->GetRules().FlowFieldConfig, OccupiedCellKeys, EnvironmentSummary);
    Pipeline->RecordTargetInfluenceExecutionStep(ExecutionSamples, EnvironmentSummary);
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

      TArray<FCrowdDemoParticleConstraintAgent> StickyAgents = Agents;
      for (auto& Agent : StickyAgents)
        if (EverReached.Contains(Agent.AgentId)) Agent.PredictedPosition = Agent.StartPosition;
      TArray<FCrowdDemoParticleConstraintPair> StickyPairs;
      TArray<FCrowdDemoParticleConstraintResult> StickyResults;
      FCrowdDemoParticleConstraintSummary StickySummary;
      FCrowdDemoParticleConstraintSettings CounterfactualSettings = Settings;
      CounterfactualSettings.bCaptureRouteDiagnostic = false;
      FCrowdDemoParticleConstraintKernel::Solve(StickyAgents, Environment,
        CounterfactualSettings, StickyPairs, StickyResults, StickySummary);
      Counterfactual.bStickyValid = StickySummary.bValid;
      Counterfactual.StickyNeverReachedForwardCmps = SumNeverReachedForward(StickyResults);

      TArray<FCrowdDemoParticleConstraintPair> SoftDisabledPairs;
      TArray<FCrowdDemoParticleConstraintResult> SoftDisabledResults;
      FCrowdDemoParticleConstraintSummary SoftDisabledSummary;
      CounterfactualSettings.SoftResponsePerSecond = 0.0f;
      FCrowdDemoParticleConstraintKernel::Solve(Agents, Environment,
        CounterfactualSettings, SoftDisabledPairs, SoftDisabledResults, SoftDisabledSummary);
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
      TArray<FCrowdDemoParticleConstraintPair> DiagnosticPairs;
      TArray<FCrowdDemoParticleConstraintResult> DiagnosticResults;
      FCrowdDemoParticleConstraintSummary DiagnosticSummary;
      FCrowdDemoParticleConstraintKernel::Solve(
        Agents, Environment, DiagnosticSettings, DiagnosticPairs,
        DiagnosticResults, DiagnosticSummary, &Trace);
      bParticleTraceCaptured = true;
      if (DiagnosticSummary.CandidateHash != Summary.CandidateHash
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
      MaxNavigationDomainReprojectDeltaCm = FMath::Max(
        MaxNavigationDomainReprojectDeltaCm, Result.FlowBoundsReprojectDeltaCm);
      ++AgentCount;
    }
  });
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
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundParticleConstraintFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFacingFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
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

  TArray<FCrowdDemoFacingInput> Inputs;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const auto Particles = ChunkContext.GetFragmentView<FCrowdDemoRoundParticleConstraintFragment>();
    const auto Facings = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFacingFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoRoundFacingFragment& Facing = Facings[It];
      const ECrowdDemoTargetRegionGuidanceMode* Mode =
        GuidanceModeByAgentId.Find(Identities[It].Id);
      const bool bTerminalOwner = Mode
        && (*Mode == ECrowdDemoTargetRegionGuidanceMode::TerminalSettle
          || *Mode == ECrowdDemoTargetRegionGuidanceMode::EngagedHold);
      const bool bSettledThisStep = bTerminalOwner
        && FVector2f(Particles[It].CorrectedVelocity.X,
          Particles[It].CorrectedVelocity.Y).Size() <= 20.0f
        && Particles[It].RealizedCorrection.Size2D() <= 1.0f;
      Facing.ConsecutiveFinalSettleSteps = bSettledThisStep
        ? Facing.ConsecutiveFinalSettleSteps + 1 : 0;
      Facing.bFinalPositionSettled = Facing.ConsecutiveFinalSettleSteps >= 15;

      FCrowdDemoFacingInput& Input = Inputs.AddDefaulted_GetRef();
      Input.AgentId = Identities[It].Id;
      Input.CurrentYawDegrees = States[It].YawDegrees;
      Input.AutonomousPreferredVelocity = FVector2f(
        Intents[It].DesiredVelocity.X, Intents[It].DesiredVelocity.Y);
      Input.Location = FVector2f(Particles[It].CorrectedLocation.X,
        Particles[It].CorrectedLocation.Y);
      Input.TargetLocation = FVector2f(Pipeline->GetTargetApproachFact().Location.X,
        Pipeline->GetTargetApproachFact().Location.Y);
      Input.bHasTarget = Pipeline->IsTargetRegionExecutionActive();
      Input.bFinalPositionSettled = Facing.bFinalPositionSettled;
    }
  });

  FCrowdDemoFacingSettings Settings;
  Settings.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
  FCrowdDemoFacingSummary Summary;
  FCrowdDemoFacingKernel::Resolve(Inputs, Settings, Summary);
  if (!Summary.bValid || Summary.Results.Num() != Inputs.Num())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingResolveInvalid step=%d inputs=%d results=%d"),
      Pipeline->GetCurrentFixedStepIndex(), Inputs.Num(), Summary.Results.Num());
    return;
  }
  TMap<int32, const FCrowdDemoFacingResult*> ById;
  for (const auto& Result : Summary.Results) ById.Add(Result.AgentId, &Result);
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Facings = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFacingFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      if (const FCrowdDemoFacingResult* const* Result = ById.Find(Identities[It].Id))
      {
        Facings[It].ResolvedYawDegrees = (*Result)->ResolvedYawDegrees;
        Facings[It].bFacingTarget = (*Result)->bFacingTarget;
      }
  });
}

UCrowdDemoRoundMovementFinalizeProcessor::UCrowdDemoRoundMovementFinalizeProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundMovementFinalizeProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFacingFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundParticleConstraintFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoParticlePropertiesFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoTargetApproachFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoOpenSpawnRelaxationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassStatsFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoBusinessStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRangedAttackFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoHitFlashFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
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
  TArray<FCrowdDemoRoundFlowAgentSample> MetricSamples;
  TArray<FCrowdDemoSoftPressureRollbackAgentState> SoftPressureRollbackAgents;
  TArray<int32> OpenSpawnAgentIds;
  TArray<FVector> OpenSpawnLocations;
  TArray<FCrowdDemoBidirectionalSwapStepAgent> BidirectionalSwapAgents;
  TArray<FCrowdDemoValidCorridorTransitStepAgent> ValidCorridorTransitAgents;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoRoundMoveIntentFragment> Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const auto Facings = ChunkContext.GetFragmentView<FCrowdDemoRoundFacingFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const TConstArrayView<FCrowdDemoRoundFlowSampleFragment> FlowSamples = ChunkContext.GetFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    const TConstArrayView<FCrowdDemoRoundObstacleConstraintFragment> Constraints = ChunkContext.GetFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
    const auto ParticleConstraints = ChunkContext.GetFragmentView<FCrowdDemoRoundParticleConstraintFragment>();
    const auto ParticleProperties = ChunkContext.GetFragmentView<FCrowdDemoParticlePropertiesFragment>();
    const TArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto TargetApproaches = ChunkContext.GetFragmentView<FCrowdDemoTargetApproachFragment>();
    const auto OpenSpawnStates = ChunkContext.GetFragmentView<FCrowdDemoOpenSpawnRelaxationFragment>();
    const auto Stats = ChunkContext.GetFragmentView<FCrowdDemoMassStatsFragment>();
    const auto Businesses = ChunkContext.GetFragmentView<FCrowdDemoBusinessStateFragment>();
    const auto Attacks = ChunkContext.GetFragmentView<FCrowdDemoRangedAttackFragment>();
    const auto Reactives = ChunkContext.GetFragmentView<FCrowdDemoReactiveMotionFragment>();
    const auto HitFlashes = ChunkContext.GetFragmentView<FCrowdDemoHitFlashFragment>();
    const auto Visuals = ChunkContext.GetFragmentView<FCrowdDemoMassVisualFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoRoundSimStateFragment& State = States[It];
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
      {
        State.Location = ParticleConstraints[It].CorrectedLocation;
        State.Velocity = ParticleConstraints[It].CorrectedVelocity;
      }
      else
      {
        State.Location = Constraints[It].ConstrainedLocation;
        State.Velocity = Constraints[It].ConstrainedVelocity;
      }
      State.YawDegrees = Facings[It].ResolvedYawDegrees;
      State.SimulatedServerTimeSeconds = Pipeline->GetCurrentStepEndServerTimeSeconds();
      State.PlanRevision = Pipeline->GetCurrentPlanRevision();
      State.bInitialized = true;
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
        FinalField = Pipeline->FindBidirectionalSwapFlowField(Formations[It].FormationIndex);
      if (!FinalField) FinalField = &Pipeline->GetSharedFlowField();
      const FCrowdDemoSharedFlowSample FinalFlowSample =
        FCrowdDemoSharedFlowFieldKernel::Sample(*FinalField, State.Location);
      Metric.bUnreachable = FinalFlowSample.Status != ECrowdDemoFlowLocationStatus::Reachable;
      FCrowdDemoSharedFlowFieldConfig PhysicalObstacleConfig = FinalField->Config;
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
        PhysicalObstacleConfig.AgentInflateCm = FMath::Max(
          ParticleProperties[It].PhysicalRadiusCm + ParticleProperties[It].HardSafetyGapCm,
          Pipeline->GetRules().bEnableHeterogeneousProfiles != 0
            ? Pipeline->GetRules().FlowFieldConfig.AgentInflateCm
            : 0.0f);
      Metric.bPenetrating = (Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
          && Constraints[It].bPenetrating)
        || FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(PhysicalObstacleConfig, State.Location);
      if (Pipeline->IsBidirectionalSwap())
      {
        auto& SwapAgent = BidirectionalSwapAgents.AddDefaulted_GetRef();
        SwapAgent.AgentId = Identities[It].Id;
        SwapAgent.FormationIndex = Formations[It].FormationIndex;
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
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
      {
        FCrowdDemoSoftPressureRollbackAgentState& Rollback =
          SoftPressureRollbackAgents.AddDefaulted_GetRef();
        Rollback.AgentId = Identities[It].Id;
        Rollback.LifecycleSerial = Identities[It].LifecycleSerial;
        Rollback.Location = State.Location;
        Rollback.Velocity = State.Velocity;
        Rollback.YawDegrees = State.YawDegrees;
        Rollback.RadiusCm = Formations[It].RadiusCm;
        Rollback.SimulatedServerTimeSeconds = State.SimulatedServerTimeSeconds;
        Rollback.PlanRevision = State.PlanRevision;
        Rollback.bInitialized = State.bInitialized;
        Rollback.FlowSample = FlowSamples[It];
        Rollback.Facing = Facings[It];
        Rollback.TargetApproach = TargetApproaches[It];
        Rollback.OpenSpawnRelaxation = OpenSpawnStates[It];
        Rollback.Combat = MakeCombatNetState(
          Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
      }
    }
  });
  if (Pipeline->IsOpenSpawnRelaxation())
    FCrowdDemoOpenSpawnRelaxationKernel::RecordFinalLocations(
      OpenSpawnAgentIds, OpenSpawnLocations, Pipeline->GetOpenSpawnRelaxationRuntime());
  if (Pipeline->IsBidirectionalSwap())
    Pipeline->RecordBidirectionalSwapStep(BidirectionalSwapAgents);
  if (Pipeline->IsCorridorTransitProgressScenario())
    Pipeline->RecordValidCorridorTransitStep(ValidCorridorTransitAgents);
  if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    Pipeline->RecordSoftPressureRollbackSnapshot(
      Pipeline->GetCurrentFixedStepIndex(), MoveTemp(SoftPressureRollbackAgents));
  }
  Pipeline->RecordFlowAgentSamples(MetricSamples, World->GetNetMode() == NM_Client);
  Pipeline->LogStageOnce(
    Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("09_movement_finalize")
      : TEXT("06_movement_finalize"),
    MetricSamples.Num());
}

static void ConfigureCommitQuery(FMassEntityQuery& Query)
{
  Query.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddRequirement<FCrowdDemoMassMovementFragment>(EMassFragmentAccess::ReadWrite);
  Query.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

static int32 CommitRoundState(FMassEntityQuery& Query, FMassExecutionContext& Context)
{
  int32 Count = 0;
  Query.ForEachEntityChunk(Context, [&Count](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoRoundSimStateFragment> States =
      ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const TArrayView<FTransformFragment> Transforms =
      ChunkContext.GetMutableFragmentView<FTransformFragment>();
    const TArrayView<FMassVelocityFragment> Velocities =
      ChunkContext.GetMutableFragmentView<FMassVelocityFragment>();
    const TArrayView<FCrowdDemoMassMovementFragment> Movements =
      ChunkContext.GetMutableFragmentView<FCrowdDemoMassMovementFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (!States[It].bInitialized)
      {
        continue;
      }
      FTransform Transform = Transforms[It].GetTransform();
      Transform.SetLocation(States[It].Location);
      Transform.SetRotation(FRotator(0.0f, States[It].YawDegrees, 0.0f).Quaternion());
      Transforms[It].SetTransform(Transform);
      Velocities[It].Value = States[It].Velocity;
      Movements[It].CurrentVelocity = States[It].Velocity;
      Movements[It].DesiredVelocity = States[It].Velocity;
      Movements[It].YawDegrees = States[It].YawDegrees;
      ++Count;
    }
  });
  return Count;
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
  const int32 Count = CommitRoundState(EntityQuery, Context);
  if (UCrowdDemoRoundSimPipelineSubsystem* Pipeline = EntityManager.GetWorld()->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>())
  {
    Pipeline->LogStageOnce(
      Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
        ? TEXT("10_authority_commit")
        : TEXT("07_authority_commit"),
      Count);
  }
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
  const int32 Count = CommitRoundState(EntityQuery, Context);
  if (UCrowdDemoRoundSimPipelineSubsystem* Pipeline = EntityManager.GetWorld()->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>())
  {
    Pipeline->LogStageOnce(
      Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
        ? TEXT("10_client_prediction_commit")
        : TEXT("07_client_prediction_commit"),
      Count);
  }
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
  EntityQuery.AddRequirement<FCrowdDemoTargetApproachFragment>(EMassFragmentAccess::ReadOnly);
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
  TArray<FCrowdDemoRoundAgentState> States;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoRoundSimStateFragment> SimStates = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const TConstArrayView<FCrowdDemoRoundFormationFragment> Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const TConstArrayView<FCrowdDemoTargetApproachFragment> TargetApproaches =
      ChunkContext.GetFragmentView<FCrowdDemoTargetApproachFragment>();
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
          Identities[It], Formations[It], SimStates[It], &TargetApproaches[It], &Combat));
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
    Pipeline->PinTargetInfluenceExecutionDiagnosticForRoundResult();
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
      if (Pipeline->GetRules().TargetRegionTransportSettings.bEnabled == 0)
      {
      const FCrowdDemoTargetInfluenceSummary& TargetInfluence =
        Pipeline->GetTargetInfluenceSummary();
      Result.ParticleMetrics.bTargetInfluenceValid = TargetInfluence.bValid ? 1 : 0;
      Result.ParticleMetrics.TargetInfluenceAgentCount = TargetInfluence.InfluenceAgentCount;
      Result.ParticleMetrics.TargetInsideEffectiveBandCount =
        TargetInfluence.InsideEffectiveBandCount;
      Result.ParticleMetrics.TargetOutsideMaxCount = TargetInfluence.OutsideMaximumCount;
      Result.ParticleMetrics.TargetInsideMinCount = TargetInfluence.InsideMinimumCount;
      Result.ParticleMetrics.TargetRadialErrorCmP50 = TargetInfluence.RadialErrorCmP50;
      Result.ParticleMetrics.TargetRadialErrorCmP95 = TargetInfluence.RadialErrorCmP95;
      Result.ParticleMetrics.TargetRadialErrorCmMax = TargetInfluence.RadialErrorCmMax;
      Result.ParticleMetrics.TargetRelativeSpeedCmpsP95 = TargetInfluence.RelativeSpeedCmpsP95;
      Result.ParticleMetrics.TargetFollowLagCmP95 = TargetInfluence.FollowLagCmP95;
      Result.ParticleMetrics.OccupiedAngularSectorCount =
        TargetInfluence.OccupiedAngularSectorCount;
      Result.ParticleMetrics.AngularCoverageQ15 = TargetInfluence.AngularCoverageQ15;
      Result.ParticleMetrics.MaxAngularSectorPopulation =
        TargetInfluence.MaxAngularSectorPopulation;
      Result.ParticleMetrics.OccupiedRadialBandCount =
        TargetInfluence.OccupiedRadialBandCount;
      Result.ParticleMetrics.TargetDensityFieldHash = TargetInfluence.Density.FieldHash;
      Result.ParticleMetrics.TargetDensityContributingAgentCount =
        TargetInfluence.Density.ContributingAgentCount;
      Result.ParticleMetrics.TargetDensityOccupiedCellCount =
        TargetInfluence.Density.OccupiedCellCount;
      Result.ParticleMetrics.TargetDensityMaxCellPopulation =
        TargetInfluence.Density.MaximumCellPopulation;
      Result.ParticleMetrics.TargetDensityGuidedAgentCount =
        TargetInfluence.Density.DensityGuidedAgentCount;
      Result.ParticleMetrics.TargetDensityClockwiseAgentCount =
        TargetInfluence.Density.ClockwiseAgentCount;
      Result.ParticleMetrics.TargetDensityCounterClockwiseAgentCount =
        TargetInfluence.Density.CounterClockwiseAgentCount;
      Result.ParticleMetrics.TargetDensityTangentialSpeedCmpsP95 =
        TargetInfluence.Density.TangentialSpeedCmpsP95;
      Result.ParticleMetrics.TargetDensityTangentialSpeedCmpsMax =
        TargetInfluence.Density.MaximumTangentialSpeedCmps;
      Result.ParticleMetrics.TargetLargestEmptySectorRun =
        TargetInfluence.Density.LargestEmptySectorRun;
      Result.ParticleMetrics.TargetInfluenceHash = Pipeline->GetTargetInfluenceRoundHash();
      if (Pipeline->IsTargetInfluenceExecutionDiagnosticEnabled())
      {
        const auto& Diagnostic = Pipeline->GetTargetInfluenceExecutionSummary();
        auto& Metrics = Result.ParticleMetrics;
        Metrics.bTargetInfluenceExecutionDiagnosticValid = Diagnostic.bValid ? 1 : 0;
        Metrics.TargetInfluenceExecutionValidSampleCount = Diagnostic.ValidSampleCount;
        Metrics.TargetInfluenceExecutionRequestedAgentCount = Diagnostic.RequestedAgentCount;
        Metrics.TargetInfluenceExecutionBelowThresholdSampleCount =
          Diagnostic.RequestedBelowThresholdSampleCount;
        Metrics.TargetDensityRequestedTangentialCmpsP50 = Diagnostic.RequestedTangentialCmpsP50;
        Metrics.TargetDensityRequestedTangentialCmpsP95 = Diagnostic.RequestedTangentialCmpsP95;
        Metrics.TargetDensityRequestedTangentialCmpsMax = Diagnostic.RequestedTangentialCmpsMax;
        Metrics.TargetDensityPredictTangentialCmpsP50 = Diagnostic.MovementPredictTangentialCmpsP50;
        Metrics.TargetDensityPredictTangentialCmpsP95 = Diagnostic.MovementPredictTangentialCmpsP95;
        Metrics.TargetDensityPredictTangentialCmpsMax = Diagnostic.MovementPredictTangentialCmpsMax;
        Metrics.TargetDensityAppliedTangentialCmpsP50 = Diagnostic.AppliedTangentialCmpsP50;
        Metrics.TargetDensityAppliedTangentialCmpsP95 = Diagnostic.AppliedTangentialCmpsP95;
        Metrics.TargetDensityAppliedTangentialCmpsMax = Diagnostic.AppliedTangentialCmpsMax;
        Metrics.TargetDensityRequestedToAppliedRatioP50 = Diagnostic.RequestedToAppliedRatioP50;
        Metrics.TargetDensityRequestedToAppliedRatioP95 = Diagnostic.RequestedToAppliedRatioP95;
        Metrics.TargetDensityLostTangentialCmpsP50 = Diagnostic.LostTangentialCmpsP50;
        Metrics.TargetDensityLostTangentialCmpsP95 = Diagnostic.LostTangentialCmpsP95;
        Metrics.TargetDensityLostTangentialCmpsMax = Diagnostic.LostTangentialCmpsMax;
        Metrics.TargetDensityDirectionFlipAgentCount = Diagnostic.DirectionFlipAgentCount;
        Metrics.TargetDensityDirectionFlipCount = Diagnostic.DirectionFlipCount;
        Metrics.TargetDensityAngularSectorTransitionCount = Diagnostic.AngularSectorTransitionCount;
        Metrics.TargetDensityRadialBandTransitionCount = Diagnostic.RadialBandTransitionCount;
        Metrics.TargetDensityEnvironmentOpposedAgentCount = Diagnostic.EnvironmentOpposedAgentCount;
        Metrics.TargetDensityParticleOpposedAgentCount = Diagnostic.ParticleOpposedAgentCount;
        Metrics.TargetFeasibleSectorCountByRadialBand =
          Diagnostic.Environment.FeasibleSectorCountByRadialBand;
        Metrics.TargetOccupiedFeasibleSectorCount = Diagnostic.Environment.OccupiedFeasibleSectorCount;
        Metrics.TargetOccupiedInfeasiblePolarCellCount =
          Diagnostic.Environment.OccupiedInfeasiblePolarCellCount;
        Metrics.TargetFeasibleButUnoccupiedSectorCount =
          Diagnostic.Environment.FeasibleButUnoccupiedSectorCount;
        Metrics.TargetLargestEmptyFeasibleSectorRun =
          Diagnostic.Environment.LargestEmptyFeasibleSectorRun;
        Metrics.TargetFlowBoundsInfeasibleCellCount =
          Diagnostic.Environment.FlowBoundsInfeasibleCellCount;
        Metrics.TargetObstacleInfeasibleCellCount =
          Diagnostic.Environment.ObstacleInfeasibleCellCount;
        Metrics.TargetInfluenceExecutionDiagnosticHash = Diagnostic.DiagnosticHash;
      }
      }
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
              Pipeline->GetTargetApproachFact().Location.X,
              Pipeline->GetTargetApproachFact().Location.Y);
            const FVector2f TargetVelocity(
              Pipeline->GetTargetApproachFact().Velocity.X,
              Pipeline->GetTargetApproachFact().Velocity.Y);
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
          Pipeline->GetRules(), Pipeline->GetTargetApproachFact());
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
      const FCrowdDemoTargetApproachSummary& TargetApproach =
        Pipeline->GetTargetApproachSummary();
      Result.ParticleMetrics.bTargetApproachValid = TargetApproach.bValid ? 1 : 0;
      Result.ParticleMetrics.TargetFactHash = TargetApproach.TargetFactHash;
      Result.ParticleMetrics.TargetApproachHash = TargetApproach.ApproachHash;
      Result.ParticleMetrics.TargetAgentInputHash = TargetApproach.AgentInputHash;
      Result.ParticleMetrics.TargetAgentFineKinematicHash = TargetApproach.AgentFineKinematicHash;
      Result.ParticleMetrics.TargetAgentConfigHash = TargetApproach.AgentConfigHash;
      Result.ParticleMetrics.TargetAgentTemporalHash = TargetApproach.AgentTemporalHash;
      Result.ParticleMetrics.TargetSettingsHash = TargetApproach.SettingsHash;
      Result.ParticleMetrics.TargetSlotInputHash = TargetApproach.SlotInputHash;
      Result.ParticleMetrics.TargetFullInputHash = TargetApproach.FullInputHash;
      Result.ParticleMetrics.TargetOwnerStateHash = TargetApproach.OwnerStateHash;
      Result.ParticleMetrics.TargetTransitionHash = TargetApproach.TransitionHash;
      Result.ParticleMetrics.TargetGuidanceHash = TargetApproach.GuidanceHash;
      Result.ParticleMetrics.TargetGuidanceLocationHash = TargetApproach.GuidanceLocationHash;
      Result.ParticleMetrics.TargetGuidanceVelocityHash = TargetApproach.GuidanceVelocityHash;
      const FCrowdDemoTargetSlotLayout& TargetSlotLayout =
        Pipeline->GetPreparedTargetSlotLayout();
      const FCrowdDemoTargetSlotLayoutSummary& TargetSlotLayoutSummary =
        Pipeline->GetTargetSlotLayoutSummary();
      Result.ParticleMetrics.TargetSlotLayoutRevision = TargetSlotLayout.SlotLayoutRevision;
      Result.ParticleMetrics.TargetSlotLayoutTopologyHash = TargetSlotLayout.TopologyHash;
      Result.ParticleMetrics.TargetSlotLayoutWorldHash = TargetSlotLayout.WorldValidationHash;
      Result.ParticleMetrics.TargetSlotLayoutFullInputHash = TargetSlotLayout.FullInputHash;
      Result.ParticleMetrics.SlotLayoutCandidateCount = TargetSlotLayoutSummary.GeneratedCandidateCount;
      Result.ParticleMetrics.SlotLayoutFunctionalCount = TargetSlotLayoutSummary.AcceptedFunctionalCount;
      Result.ParticleMetrics.SlotLayoutFillCount = TargetSlotLayoutSummary.AcceptedFillCount;
      Result.ParticleMetrics.SlotRejectedTargetClearanceCount =
        TargetSlotLayoutSummary.RejectedTargetClearanceCount;
      Result.ParticleMetrics.SlotRejectedPairSpacingCount =
        TargetSlotLayoutSummary.RejectedPairSpacingCount;
      Result.ParticleMetrics.SlotRejectedObstacleCount =
        TargetSlotLayoutSummary.RejectedObstacleCount;
      Result.ParticleMetrics.SlotRejectedBoundsCount =
        TargetSlotLayoutSummary.RejectedBoundsCount;
      Result.ParticleMetrics.SlotRejectedUnreachableCount =
        TargetSlotLayoutSummary.RejectedUnreachableCount;
      Result.ParticleMetrics.SlotRejectedIngressSegmentCount =
        TargetSlotLayoutSummary.RejectedIngressSegmentCount;
      Result.ParticleMetrics.TargetApproachScheduleHash = TargetApproach.ScheduleHash;
      Result.ParticleMetrics.TargetApproachCommitHash = TargetApproach.CommitHash;
      Result.ParticleMetrics.SlotOwnerReleaseCount = TargetApproach.SlotOwnerReleaseCount;
      Result.ParticleMetrics.SlotOwnerReusedCount = TargetApproach.SlotOwnerReusedCount;
      Result.ParticleMetrics.SlotOwnerConflictCount = TargetApproach.SlotOwnerConflictCount;
      Result.ParticleMetrics.SlotLayoutRevisionMismatchCount =
        TargetApproach.SlotLayoutRevisionMismatchCount;
      Result.ParticleMetrics.RingEnteredCount = TargetApproach.RingEnteredCount;
      Result.ParticleMetrics.RingWaitingCount = TargetApproach.RingWaitingCount;
      Result.ParticleMetrics.FunctionalSlotCapacity = TargetApproach.FunctionalSlotCapacity;
      Result.ParticleMetrics.FunctionalSlotOccupied = TargetApproach.FunctionalSlotOccupied;
      Result.ParticleMetrics.FillSlotCapacity = TargetApproach.FillSlotCapacity;
      Result.ParticleMetrics.FillSlotOccupied = TargetApproach.FillSlotOccupied;
      Result.ParticleMetrics.SlotIngressCount = TargetApproach.SlotIngressCount;
      Result.ParticleMetrics.SlotOccupiedCount = TargetApproach.SlotOccupiedCount;
      Result.ParticleMetrics.FreeSettleCount = TargetApproach.FreeSettleCount;
      Result.ParticleMetrics.FreeSettledCount = TargetApproach.FreeSettledCount;
      Result.ParticleMetrics.DuplicateSlotOwnerCount = TargetApproach.DuplicateSlotOwnerCount;
      Result.ParticleMetrics.InvalidSlotOwnerCount = TargetApproach.InvalidSlotOwnerCount;
      Result.ParticleMetrics.TargetApproachStateTransitionCount =
        TargetApproach.StateTransitionCount;
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
  RangedCombatProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundRangedCombatProcessor>(*this, Owner, EntityManager);
  HitResponseBoundaryApplyProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundHitResponseBoundaryApplyProcessor>(*this, Owner, EntityManager);
  ReactiveMotionIntentComposeProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundReactiveMotionIntentComposeProcessor>(*this, Owner, EntityManager);
  VisualStateResolveProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundVisualStateResolveProcessor>(*this, Owner, EntityManager);
  OpenSpawnRelaxationPhasePrepareProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor>(*this, Owner, EntityManager);
  TargetFactApplyProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetFactApplyProcessor>(*this, Owner, EntityManager);
  TargetPolarTopologyBuildProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetPolarTopologyBuildProcessor>(*this, Owner, EntityManager);
  TargetRegionPopulationBuildProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetRegionPopulationBuildProcessor>(*this, Owner, EntityManager);
  TargetRegionTransportSolveProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetRegionTransportSolveProcessor>(*this, Owner, EntityManager);
  TargetRegionGuidanceProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetRegionGuidanceProcessor>(*this, Owner, EntityManager);
  TargetSlotLayoutPrepareProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetSlotLayoutPrepareProcessor>(*this, Owner, EntityManager);
  TargetApproachScheduleProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetApproachScheduleProcessor>(*this, Owner, EntityManager);
  TargetApproachCommitProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetApproachCommitProcessor>(*this, Owner, EntityManager);
  TargetApproachGuidanceProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetApproachGuidanceProcessor>(*this, Owner, EntityManager);
  SharedFlowFieldBuildProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundSharedFlowFieldBuildProcessor>(*this, Owner, EntityManager);
  FlowPreferredVelocityProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundFlowPreferredVelocityProcessor>(*this, Owner, EntityManager);
  LocalPredictiveInteractionProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundLocalPredictiveInteractionProcessor>(*this, Owner, EntityManager);
  MovementPredictProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundMovementPredictProcessor>(*this, Owner, EntityManager);
  ParticleConstraintProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundParticleConstraintProcessor>(*this, Owner, EntityManager);
  ObstacleConstraintProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundObstacleConstraintProcessor>(*this, Owner, EntityManager);
  FacingResolveProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundFacingResolveProcessor>(*this, Owner, EntityManager);
  MovementFinalizeProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundMovementFinalizeProcessor>(*this, Owner, EntityManager);
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

    MeasureStage(ECrowdDemoRoundPerformanceStage::SharedFlow, [&]
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
        && (Pipeline->GetRules().TargetApproachSettings.bEnabled != 0
          || Pipeline->GetRules().TargetInfluenceSettings.bEnabled != 0))
      {
        TargetFactApplyProcessor->CallExecute(EntityManager, Context);
      }
      SharedFlowFieldBuildProcessor->CallExecute(EntityManager, Context);
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
        && Pipeline->GetRules().TargetApproachSettings.bEnabled != 0)
        TargetSlotLayoutPrepareProcessor->CallExecute(EntityManager, Context);
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
    MeasureStage(ECrowdDemoRoundPerformanceStage::LocalPredictive, [&]
    {
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
        && Pipeline->GetRules().TargetApproachSettings.bEnabled != 0)
      {
        TargetApproachScheduleProcessor->CallExecute(EntityManager, Context);
        TargetApproachCommitProcessor->CallExecute(EntityManager, Context);
        TargetApproachGuidanceProcessor->CallExecute(EntityManager, Context);
      }
      if (Pipeline->IsRangedProjectileCombat()
        || (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
          && Pipeline->GetRules().SoftPressureTestCase
            == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse))
      {
        ReactiveMotionIntentComposeProcessor->CallExecute(EntityManager, Context);
      }
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
        && Pipeline->GetRules().LocalPredictiveSettings.bEnabled != 0)
        LocalPredictiveInteractionProcessor->CallExecute(EntityManager, Context);
      MovementPredictProcessor->CallExecute(EntityManager, Context);
    });
    MeasureStage(ECrowdDemoRoundPerformanceStage::Particle, [&]
    {
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
        ParticleConstraintProcessor->CallExecute(EntityManager, Context);
      else
        ObstacleConstraintProcessor->CallExecute(EntityManager, Context);
    });
    MeasureStage(ECrowdDemoRoundPerformanceStage::FinalizeCommit, [&]
    {
      FacingResolveProcessor->CallExecute(EntityManager, Context);
      MovementFinalizeProcessor->CallExecute(EntityManager, Context);
      VisualStateResolveProcessor->CallExecute(EntityManager, Context);
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
        TEXT("CrowdDemoCatchupBudget role=%s steps=%d cpu_ms=%.3f backlog_ms=%.3f consecutive=%d stages_ms=[flow=%.3f topology=%.3f demand=%.3f plan=%.3f guidance=%.3f local=%.3f particle=%.3f finalize=%.3f] source=MassPipeline"),
        World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
        ExecutedSteps, PipelineFrameCpuMilliseconds, BacklogMilliseconds,
        ConsecutiveCatchupCpuBudgetHitFrames,
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
          ECrowdDemoRoundPerformanceStage::LocalPredictive)],
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::Particle)],
        PipelineFrameStageMilliseconds[static_cast<uint8>(
          ECrowdDemoRoundPerformanceStage::FinalizeCommit)]);
    }
  }
  else
  {
    ConsecutiveCatchupCpuBudgetHitFrames = 0;
  }
}

#undef ROUND_DYNAMIC_FLAGS
