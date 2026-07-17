#include "Mass/CrowdDemoRoundSimProcessors.h"
#include "Mass/CrowdDemoRoundInitialStateKernel.h"

#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoHardSeparationPbdKernel.h"
#include "Mass/CrowdDemoCapabilityProfileKernel.h"
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
#include "Mass/CrowdDemoTrafficSchedulingKernel.h"
#include "Mass/CrowdDemoDeterministicOrcaKernel.h"
#include "Mass/CrowdDemoElasticCrowdKernel.h"
#include "Mass/CrowdDemoElasticShadowKernel.h"
#include "Mass/CrowdDemoSf3DeterminismHash.h"
#include "GameFramework/GameStateBase.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ThirdParty/Reference/RVO2/CrowdDemoRvo2ReferenceSolver.h"
#endif

namespace
{
  constexpr int32 MaxFixedStepsPerFrame = 256;
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
    return Settings;
  }

  bool IsRoundFlowScenario(const ECrowdDemoScenario Scenario)
  {
    return Scenario == ECrowdDemoScenario::SimRoundObstacle
      || Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      || Scenario == ECrowdDemoScenario::SimRoundCrowdTraffic
      || Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning;
  }

  bool IsTrafficScenario(const ECrowdDemoScenario Scenario)
  {
    return Scenario == ECrowdDemoScenario::SimRoundCrowdTraffic
      || Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning;
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

  FCrowdDemoFlowReachabilityStageSample MakeFlowReachabilityStageSample(
    const int32 AgentId,
    const FCrowdDemoSharedFlowField& Field,
    const FVector& Location,
    const FVector& Velocity,
    const bool bContinuousPenetrating)
  {
    const FCrowdDemoSharedFlowSample Flow = FCrowdDemoSharedFlowFieldKernel::Sample(Field, Location);
    FCrowdDemoFlowReachabilityStageSample Result;
    Result.AgentId = AgentId;
    Result.Location = Location;
    Result.Velocity = Velocity;
    Result.Status = Flow.Status;
    Result.CellIndex = Flow.CellIndex;
    Result.StableCellKey = Flow.StableCellKey;
    Result.bContinuousPenetrating = bContinuousPenetrating;
    return Result;
  }

  const FCrowdDemoTrafficCohortRule* FindTrafficCohort(
    const FCrowdDemoRoundRules& Rules,
    const int32 FormationIndex)
  {
    for (const FCrowdDemoTrafficCohortRule& Cohort : Rules.TrafficCohorts)
    {
      if (FormationIndex >= Cohort.FirstFormationIndex
        && FormationIndex < Cohort.FirstFormationIndex + Cohort.AgentCount)
      {
        return &Cohort;
      }
    }
    return nullptr;
  }

  struct FSf3AgentHashRecord
  {
    int32 AgentId = INDEX_NONE;
    FVector Position = FVector::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
    FVector Direction = FVector::ZeroVector;
    FVector Auxiliary = FVector::ZeroVector;
    int32 Values[8] = {};
  };

  uint32 HashSf3AgentRecords(
    const int32 FixedStepIndex,
    TArray<FSf3AgentHashRecord>& Records,
    TArray<int32>& OutKeys)
  {
    Records.Sort([](const FSf3AgentHashRecord& A, const FSf3AgentHashRecord& B)
    {
      return A.AgentId < B.AgentId;
    });
    FCrowdDemoSf3DeterminismHashBuilder Hash(FixedStepIndex, Records.Num());
    OutKeys.Reset();
    for (const FSf3AgentHashRecord& Record : Records)
    {
      Hash.AddInt(Record.AgentId);
      Hash.AddPosition(Record.Position);
      Hash.AddVelocity(Record.Velocity);
      Hash.AddDirection(Record.Direction);
      Hash.AddVelocity(Record.Auxiliary);
      for (const int32 Value : Record.Values)
      {
        Hash.AddInt(Value);
      }
      if (OutKeys.Num() < 8)
      {
        OutKeys.Add(Record.AgentId);
      }
    }
    return Hash.Finalize();
  }

  bool IsSf4ObstacleConstraintDiagnosticEnabled()
  {
#if WITH_DEV_AUTOMATION_TESTS
    static const bool bEnabled = FParse::Param(
      FCommandLine::Get(), TEXT("CrowdDemoSf4ObstacleConstraintDiagnostic"));
    return bEnabled;
#else
    return false;
#endif
  }
}

#define ROUND_DYNAMIC_FLAGS \
  ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone); \
  bAutoRegisterWithProcessingPhases = false; \
  bRequiresGameThreadExecution = true

bool CrowdDemoUsesSteeringFirstSf4Pipeline(const ECrowdDemoScenario Scenario)
{
  return Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning;
}

bool CrowdDemoUsesLegacySf4ReservationPipeline(const ECrowdDemoScenario Scenario)
{
  return false;
}

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
  EntityQuery.AddRequirement<FCrowdDemoTargetApproachFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoTargetCapabilityFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundProposedMovementFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoOpenSpawnRelaxationFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundPbdCorrectionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoParticlePropertiesFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundSeparationFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPortalAdmissionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPassingBandFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoOrcaVelocityFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPursuitGuidanceFragment>(EMassFragmentAccess::ReadWrite);
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
      const TArrayView<FCrowdDemoTargetApproachFragment> TargetApproaches = ChunkContext.GetMutableFragmentView<FCrowdDemoTargetApproachFragment>();
      const TArrayView<FCrowdDemoTargetCapabilityFragment> TargetCapabilities = ChunkContext.GetMutableFragmentView<FCrowdDemoTargetCapabilityFragment>();
      const TArrayView<FCrowdDemoRoundFlowSampleFragment> FlowSamples = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFlowSampleFragment>();
      const TArrayView<FCrowdDemoRoundProposedMovementFragment> ProposedMovements = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundProposedMovementFragment>();
      const TArrayView<FCrowdDemoRoundObstacleConstraintFragment> Constraints = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
      const TArrayView<FCrowdDemoRoundPbdCorrectionFragment> PbdCorrections = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundPbdCorrectionFragment>();
      const TArrayView<FCrowdDemoParticlePropertiesFragment> ParticleProperties = ChunkContext.GetMutableFragmentView<FCrowdDemoParticlePropertiesFragment>();
      const auto OpenSpawnStates = ChunkContext.GetMutableFragmentView<FCrowdDemoOpenSpawnRelaxationFragment>();
      const TArrayView<FCrowdDemoRoundSeparationFragment> Separations = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSeparationFragment>();
      const TArrayView<FCrowdDemoPortalAdmissionFragment> Admissions = ChunkContext.GetMutableFragmentView<FCrowdDemoPortalAdmissionFragment>();
      const TArrayView<FCrowdDemoPassingBandFragment> Bands = ChunkContext.GetMutableFragmentView<FCrowdDemoPassingBandFragment>();
      const TArrayView<FCrowdDemoOrcaVelocityFragment> OrcaVelocities = ChunkContext.GetMutableFragmentView<FCrowdDemoOrcaVelocityFragment>();
      const auto PositionAssignments = ChunkContext.GetMutableFragmentView<FCrowdDemoPositionAssignmentFragment>();
      const auto PursuitSteering = ChunkContext.GetMutableFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
      const auto PursuitGuidance = ChunkContext.GetMutableFragmentView<FCrowdDemoPursuitGuidanceFragment>();
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
        FVector SpawnOrigin = FVector(DuePlan.Rules.SpawnOrigin);
        if (IsTrafficScenario(DuePlan.Rules.Scenario))
        {
          if (const FCrowdDemoTrafficCohortRule* Cohort = FindTrafficCohort(DuePlan.Rules, Formation.FormationIndex))
          {
            const int32 LocalIndex = Formation.FormationIndex - Cohort->FirstFormationIndex;
            const int32 Columns = FMath::Max(1, Cohort->FormationColumns);
            const int32 Rows = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Cohort->AgentCount) / Columns));
            Formation.LocalOffset = FVector(
              (LocalIndex % Columns - 0.5f * (Columns - 1)) * DuePlan.Rules.FormationSpacingCm,
              (LocalIndex / Columns - 0.5f * (Rows - 1)) * DuePlan.Rules.FormationSpacingCm,
              0.0f);
            SpawnOrigin = FVector(Cohort->SpawnOrigin);
            Admissions[It].CohortId = Cohort->CohortId;
          }
        }
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
        else if (IsTrafficScenario(DuePlan.Rules.Scenario))
        {
          State.Location = SpawnOrigin + Formation.LocalOffset;
          State.Velocity = FVector::ZeroVector;
          State.YawDegrees = Admissions[It].CohortId == 1 ? -90.0f : 90.0f;
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
        PbdCorrections[It] = FCrowdDemoRoundPbdCorrectionFragment();
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
        Separations[It] = FCrowdDemoRoundSeparationFragment();
        const int32 CohortId = Admissions[It].CohortId;
        Admissions[It] = FCrowdDemoPortalAdmissionFragment();
        Admissions[It].CohortId = CohortId;
        Bands[It] = FCrowdDemoPassingBandFragment();
        OrcaVelocities[It] = FCrowdDemoOrcaVelocityFragment();
        PositionAssignments[It] = FCrowdDemoPositionAssignmentFragment();
        PursuitSteering[It] = FCrowdDemoPursuitSteeringStateFragment();
        PursuitGuidance[It] = FCrowdDemoPursuitGuidanceFragment();
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
  int32 DiagnosticCorrectionRevision = 0;
  int32 DiagnosticCorrectionFixedStep = INDEX_NONE;
  if (World->GetNetMode() == NM_Client && Pipeline->PopCorrectionForBoundary(Correction, ReceiveServerTime))
  {
    DiagnosticCorrectionRevision = Correction.CorrectionRevision;
    DiagnosticCorrectionFixedStep = FMath::Max(0, FMath::RoundToInt(
      (Correction.ServerTimeSeconds - Pipeline->GetActivePlan().StartServerTimeSeconds)
      / Pipeline->GetCurrentFixedStepSeconds()) - 1);
    Pipeline->LogSf3DiagnosticBoundary(
      Correction.CorrectionRevision, TEXT("client_pre_apply"), DiagnosticCorrectionFixedStep);
    const FCrowdDemoSf3RollbackSnapshot* RollbackSnapshot =
      IsTrafficScenario(Pipeline->GetRules().Scenario)
      ? Pipeline->FindSf3RollbackSnapshot(DiagnosticCorrectionFixedStep)
      : nullptr;
    const bool bSoftPressureScenario =
      Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure;
    const bool bTargetApproachCorrection = bSoftPressureScenario
      && Pipeline->GetRules().TargetApproachSettings.bEnabled != 0;
    const FCrowdDemoSoftPressureRollbackSnapshot* SoftPressureRollbackSnapshot =
      bSoftPressureScenario
      ? Pipeline->FindSoftPressureRollbackSnapshot(DiagnosticCorrectionFixedStep)
      : nullptr;
    TArray<FCrowdDemoRoundAgentState> BeforeCorrection;
    TMap<int32, const FCrowdDemoSf3RollbackAgentState*> RollbackById;
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
      else
        Pipeline->RestoreSoftPressureRuntime(*SoftPressureRollbackSnapshot);
    }
    const bool bValidSoftPressureRollback =
      SoftPressureRollbackSnapshot && !bSoftPressureAgentMismatch;
    if (bSoftPressureScenario)
    {
      const int32 ReplayedSteps = bValidSoftPressureRollback
        ? FMath::Max(0, FMath::RoundToInt(
            (BoundaryTime - Correction.ServerTimeSeconds)
            / Pipeline->GetCurrentFixedStepSeconds()))
        : 0;
      Pipeline->RecordSoftPressureRollbackOutcome(
        bValidSoftPressureRollback, bSoftPressureAgentMismatch, ReplayedSteps);
    }
    if (RollbackSnapshot)
    {
      Pipeline->RestoreSf3PortalRuntime(*RollbackSnapshot);
      for (const FCrowdDemoSf3RollbackAgentState& Agent : RollbackSnapshot->Agents)
      {
        FCrowdDemoRoundAgentState& CompareState = BeforeCorrection.AddDefaulted_GetRef();
        CompareState.AgentId = Agent.AgentId;
        CompareState.LifecycleSerial = Agent.LifecycleSerial;
        CompareState.Location = Agent.Location;
        CompareState.Velocity = Agent.Velocity;
        CompareState.YawDegrees = Agent.YawDegrees;
        CompareState.RadiusCm = Agent.RadiusCm;
        RollbackById.Add(Agent.AgentId, &Agent);
      }
    }
    else if (bValidSoftPressureRollback)
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
    Pipeline->RecordCorrectionComparisonAndApplied(BeforeCorrection, Correction, BoundaryTime);
    // Target guidance is quantized after evaluating the exact world-space
    // position. Even a sub-tolerance local float delta can cross that guidance
    // quantum, so a TargetApproach correction must establish one authoritative
    // physical and business-state boundary before replay.
    bool bNeedsAuthoritativeState = bTargetApproachCorrection
      || (RollbackSnapshot == nullptr && !bValidSoftPressureRollback);
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
    TMap<int32, const FCrowdDemoRoundAgentState*> CorrectionById;
    for (const FCrowdDemoRoundAgentState& Agent : Correction.AgentStates)
    {
      CorrectionById.Add(Agent.AgentId, &Agent);
    }
    EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
    {
      const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
      const TArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSimStateFragment>();
      const auto Admissions = ChunkContext.GetMutableFragmentView<FCrowdDemoPortalAdmissionFragment>();
      const auto Bands = ChunkContext.GetMutableFragmentView<FCrowdDemoPassingBandFragment>();
      const auto FlowSamples = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFlowSampleFragment>();
      const auto PositionAssignments = ChunkContext.GetMutableFragmentView<FCrowdDemoPositionAssignmentFragment>();
      const auto PursuitSteering = ChunkContext.GetMutableFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
      const auto PursuitGuidance = ChunkContext.GetMutableFragmentView<FCrowdDemoPursuitGuidanceFragment>();
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
        if (const FCrowdDemoSf3RollbackAgentState* const* Rollback = RollbackById.Find(Identities[It].Id))
        {
          Admissions[It] = (*Rollback)->Admission;
          Bands[It] = (*Rollback)->Band;
          FlowSamples[It] = (*Rollback)->FlowSample;
          PositionAssignments[It] = (*Rollback)->PositionAssignment;
          PursuitSteering[It] = (*Rollback)->PursuitSteering;
          PursuitGuidance[It] = (*Rollback)->PursuitGuidance;
          States[It].Location = (*Rollback)->Location;
          States[It].Velocity = (*Rollback)->Velocity;
          States[It].YawDegrees = (*Rollback)->YawDegrees;
        }
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
  if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
  {
    TArray<FSf3AgentHashRecord> Records;
    EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
    {
      const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
      const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
      const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
      const auto Admissions = ChunkContext.GetFragmentView<FCrowdDemoPortalAdmissionFragment>();
      const auto Bands = ChunkContext.GetFragmentView<FCrowdDemoPassingBandFragment>();
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        FSf3AgentHashRecord& Record = Records.AddDefaulted_GetRef();
        Record.AgentId = Identities[It].Id;
        Record.Position = States[It].Location;
        Record.Velocity = States[It].Velocity;
        Record.Values[0] = Formations[It].FormationIndex;
        Record.Values[1] = Admissions[It].CohortId;
        Record.Values[2] = Admissions[It].PortalId;
        Record.Values[3] = static_cast<int32>(Admissions[It].State);
        Record.Values[4] = Admissions[It].DirectionEpoch;
        Record.Values[5] = Admissions[It].WaitSteps;
        Record.Values[6] = Bands[It].BandId;
        Record.Values[7] = States[It].PlanRevision;
      }
    });
    TArray<int32> Keys;
    const uint32 Hash = HashSf3AgentRecords(Pipeline->GetCurrentFixedStepIndex(), Records, Keys);
    Pipeline->RecordSf3StageHash(ECrowdDemoSf3DeterminismStage::PlanApplyInput, Hash, Records.Num(), Keys);
  }
  if (DiagnosticCorrectionRevision > 0)
  {
    Pipeline->LogSf3DiagnosticBoundary(
      DiagnosticCorrectionRevision, TEXT("client_post_apply"), DiagnosticCorrectionFixedStep + 1);
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
  if (IsTrafficScenario(Pipeline->GetRules().Scenario))
  {
    Pipeline->EnsureTrafficFlowFields();
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
  EntityQuery.AddRequirement<FCrowdDemoPortalAdmissionFragment>(EMassFragmentAccess::ReadOnly);
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
  TArray<FSf3AgentHashRecord> HashRecords;
  TArray<FCrowdDemoFlowReachabilityStageSample> ReachabilitySamples;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const TConstArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const TConstArrayView<FCrowdDemoPortalAdmissionFragment> Admissions = ChunkContext.GetFragmentView<FCrowdDemoPortalAdmissionFragment>();
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
      else if (IsTrafficScenario(Rules.Scenario))
      {
        Field = Pipeline->FindTrafficFlowField(Admissions[It].CohortId);
        for (const FCrowdDemoTrafficCohortRule& Cohort : Rules.TrafficCohorts)
        {
          if (Cohort.CohortId == Admissions[It].CohortId)
          {
            Goal = FVector(Cohort.FlowFieldConfig.GoalLocation);
            break;
          }
        }
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
      Intent.PreferredDirection = bReachedGoal ? FVector::ZeroVector : Sample.FlowDirection;
      Intent.DesiredLocation = Goal;
      float DesiredSpeedCmps = Rules.MaxSpeedCmPerSecond;
      if (Field->Config.ConnectivityContractVersion > 0
        && Sample.GuidanceDistanceCm > 0.0f)
        DesiredSpeedCmps = FMath::Min(
          DesiredSpeedCmps,
          Sample.GuidanceDistanceCm / FMath::Max(Rules.FixedStepSeconds, SMALL_NUMBER));
      Intent.DesiredVelocity = bReachedGoal || Sample.bUnreachable
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
      if (Pipeline->IsSf3FlowReachabilityDiagnosticEnabled())
      {
        ReachabilitySamples.Add(MakeFlowReachabilityStageSample(
          Identities[It].Id, *Field, States[It].Location, Intent.DesiredVelocity,
          FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(Field->Config, States[It].Location)));
      }
      if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
      {
        FSf3AgentHashRecord& Record = HashRecords.AddDefaulted_GetRef();
        Record.AgentId = Identities[It].Id;
        Record.Position = States[It].Location;
        Record.Velocity = Intent.DesiredVelocity;
        Record.Direction = FlowSample.FlowDirection;
        Record.Values[0] = FlowSample.IntegrationCost;
        Record.Values[1] = FlowSample.bBlocked ? 1 : 0;
        Record.Values[2] = FlowSample.bUnreachable ? 1 : 0;
        Record.Values[3] = Admissions[It].CohortId;
        Record.Values[4] = FlowSample.bRecoveredFromRasterMismatch ? 1 : 0;
        Record.Values[5] = Field->Config.ConnectivityContractVersion;
        Record.Values[6] = FMath::RoundToInt(FlowSample.GuidanceDistanceCm);
        const uint32 NavigationKeyFold =
          static_cast<uint32>(FlowSample.NavigationNodeKey)
          ^ static_cast<uint32>(FlowSample.NavigationNodeKey >> 32)
          ^ static_cast<uint32>(FlowSample.NextNavigationNodeKey)
          ^ static_cast<uint32>(FlowSample.NextNavigationNodeKey >> 32);
        Record.Values[7] = static_cast<int32>(NavigationKeyFold);
      }
      ++AgentCount;
    }
  });
  Pipeline->RecordSf3FlowReachabilityStage(
    ECrowdDemoFlowReachabilityStage::StepStart, ReachabilitySamples);
  Pipeline->RecordFlowConnectivityStep(
    RecoveredAgentCount, DesiredSegmentViolationCount,
    SourceAttachmentSuccessCount, NavigationUnreachableSampleCount);
  if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
  {
    TArray<int32> Keys;
    const uint32 Hash = HashSf3AgentRecords(Pipeline->GetCurrentFixedStepIndex(), HashRecords, Keys);
    Pipeline->RecordSf3StageHash(
      ECrowdDemoSf3DeterminismStage::FlowPreferredVelocity, Hash, HashRecords.Num(), Keys);
  }
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
    Pipeline->GetTargetApproachFact() = FCrowdDemoTargetApproachKernel::BuildLinearMotionFact(
      Rules.TargetMotion.TargetId,
      Rules.TargetMotion.TargetRevision,
      MotionStep,
      FVector2f(Rules.TargetMotion.InitialLocation.X, Rules.TargetMotion.InitialLocation.Y),
      FVector2f(Rules.TargetMotion.LinearVelocity.X, Rules.TargetMotion.LinearVelocity.Y),
      Rules.TargetMotion.InitialYawDegrees,
      Rules.TargetMotion.YawRateDegreesPerSecond,
      Rules.TargetInfluenceSettings.bEnabled != 0
        ? Rules.TargetInfluenceSettings.TargetPhysicalRadiusCm
        : Rules.TargetApproachSettings.TargetPhysicalRadiusCm,
      Rules.FixedStepSeconds,
      Rules.TargetInfluenceSettings.bEnabled != 0
        ? Rules.TargetInfluenceSettings.PositionQuantumCm
        : Rules.TargetApproachSettings.PositionQuantumCm,
      Rules.TargetInfluenceSettings.bEnabled != 0
        ? Rules.TargetInfluenceSettings.VelocityQuantumCmps
        : Rules.TargetApproachSettings.VelocityQuantumCmps);
    Pipeline->LogStageOnce(TEXT("02_target_fact_apply"), 1);
    return;
  }
  if (Rules.Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  FCrowdDemoPursuitTargetFact& Target = Pipeline->GetPursuitTargetFact();
  if (Target.Revision == 0)
  {
    Target.TargetId = 1;
    Target.Location = FVector2f(Pipeline->GetRules().FlowFieldConfig.GoalLocation.X,
      Pipeline->GetRules().FlowFieldConfig.GoalLocation.Y);
    Target.Velocity = FVector2f::ZeroVector;
    Target.RadiusCm = 80.0f;
    Target.Revision = 1;
  }
  Pipeline->LogStageOnce(TEXT("02_target_fact_apply"), 1);
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
  if (Pipeline->GetRules().bEnableHeterogeneousProfiles != 0)
  {
    int32 FeasibleCells = 0;
    bool bAllValid = Pipeline->GetCapabilityProfileSummary().bValid;
    for (FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime : Pipeline->GetCapabilityCohorts())
    {
      const auto Settings = MakeTargetRegionTransportSettings(
        Pipeline->GetRules(), Pipeline->GetTargetApproachFact(), &Runtime.Cohort.Profile,
        Runtime.DemandRegionPhaseOffset);
      FCrowdDemoTargetRegionTransportKernel::BuildTopology(
        Settings, Pipeline->GetRules().FlowFieldConfig,
        Runtime.Topology, Runtime.TopologySummary);
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
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(
    Settings, Pipeline->GetRules().FlowFieldConfig,
    Pipeline->GetPreparedTargetRegionTopology(), Pipeline->GetTargetRegionTopologySummary());
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
      FCrowdDemoTargetRegionTransportKernel::BuildDemand(
        Runtime.Agents, Settings, Pipeline->GetRules().FlowFieldConfig,
        &Pipeline->GetSharedFlowField(), Runtime.Topology, Runtime.Demand, ExternalAgents);
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
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    Agents, Settings, Pipeline->GetRules().FlowFieldConfig,
    &Pipeline->GetSharedFlowField(),
    Pipeline->GetPreparedTargetRegionTopology(), Pipeline->GetPreparedTargetRegionDemand());
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
    int32 RoutedAgentCount = 0;
    for (FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime : Pipeline->GetCapabilityCohorts())
    {
      FCrowdDemoTargetRegionPlanValidationResult Validation;
      FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
        Runtime.Topology, Runtime.Demand, Runtime.Plan, TargetRevision, Validation);
      int32 RebuildReason = 0;
      if (!Runtime.Plan.bValid) RebuildReason = 7;
      else if (Runtime.Plan.TargetRevision != TargetRevision) RebuildReason = 2;
      else if (Runtime.Plan.FeasibleGraphHash != Runtime.Topology.FeasibleGraphHash) RebuildReason = 3;
      else if (Runtime.Plan.MembershipHash != Runtime.Demand.MembershipHash) RebuildReason = 4;
      else if (Runtime.Plan.ExternalPopulationHash
        != Runtime.Demand.ExternalPopulationHash) RebuildReason = 8;
      else if (Step - Runtime.Plan.BuildFixedStepIndex >=
        Pipeline->GetRules().TargetRegionTransportSettings.PlanLifetimeSteps) RebuildReason = 1;
      else if (Runtime.Demand.TotalDeficit == 0 && Runtime.Plan.RoutedAgentCount > 0) RebuildReason = 5;
      else if (!Validation.bValid) RebuildReason = 6;
      if (RebuildReason != 0)
      {
        const double Start = FPlatformTime::Seconds();
        FCrowdDemoTargetRegionFlowPlan NewPlan;
        FCrowdDemoTargetRegionTransportKernel::SolveTransport(
          Runtime.Topology, Runtime.Demand, Runtime.Plan.bValid ? &Runtime.Plan : nullptr,
          FMath::Max(1, Runtime.Plan.PlanEpoch + 1), Step, TargetRevision, NewPlan);
        Runtime.SolverMillisecondsSamples.Add(static_cast<float>(
          (FPlatformTime::Seconds() - Start) * 1000.0));
        Runtime.Plan = MoveTemp(NewPlan);
        ++Runtime.PlanRebuildCount;
        FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
          Runtime.Topology, Runtime.Demand, Runtime.Plan, TargetRevision, Validation);
      }
      Runtime.Validation = Validation;
      Runtime.ValidationRoundHash = FoldTargetHash(
        FoldTargetHash(Runtime.ValidationRoundHash, static_cast<uint32>(Step)),
        Validation.ValidationHash);
      Runtime.TransportRoundHash = FoldTargetHash(
        FoldTargetHash(Runtime.TransportRoundHash, static_cast<uint32>(Step)),
        Runtime.Plan.TransportHash);
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
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    Topology, Demand, Plan, TargetRevision, Validation);
  int32 RebuildReason = 0;
  if (!Plan.bValid) RebuildReason = 7;
  else if (Plan.TargetRevision != TargetRevision) RebuildReason = 2;
  else if (Plan.FeasibleGraphHash != Topology.FeasibleGraphHash) RebuildReason = 3;
  else if (Plan.MembershipHash != Demand.MembershipHash) RebuildReason = 4;
  else if (Plan.ExternalPopulationHash != Demand.ExternalPopulationHash) RebuildReason = 8;
  else if (Step - Plan.BuildFixedStepIndex >=
    Pipeline->GetRules().TargetRegionTransportSettings.PlanLifetimeSteps) RebuildReason = 1;
  else if (Demand.TotalDeficit == 0 && Plan.RoutedAgentCount > 0) RebuildReason = 5;
  else if (!Validation.bValid) RebuildReason = 6;
  float SolverMs = 0.0f;
  if (RebuildReason != 0)
  {
    const double Start = FPlatformTime::Seconds();
    FCrowdDemoTargetRegionFlowPlan NewPlan;
    FCrowdDemoTargetRegionTransportKernel::SolveTransport(
      Topology, Demand, Plan.bValid ? &Plan : nullptr,
      FMath::Max(1, Plan.PlanEpoch + 1), Step, TargetRevision, NewPlan);
    SolverMs = static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0);
    Plan = MoveTemp(NewPlan);
    FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
      Topology, Demand, Plan, TargetRevision, Validation);
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
      FCrowdDemoTargetRegionTransportKernel::BuildGuidance(
        Runtime.Agents, Settings, Runtime.Topology, Runtime.Demand, Runtime.Plan,
        Runtime.Guidance, Runtime.GuidanceSummary);
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
  FCrowdDemoTargetRegionTransportKernel::BuildGuidance(
    Pipeline->GetPreparedTargetRegionAgents(), Settings,
    Pipeline->GetPreparedTargetRegionTopology(), Pipeline->GetPreparedTargetRegionDemand(),
    Pipeline->GetPreparedTargetRegionPlan(), Pipeline->GetPreparedTargetRegionGuidance(),
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
    FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
      Pipeline->GetPreparedTargetRegionTopology(),
      Pipeline->GetPreparedTargetRegionDemand(),
      Pipeline->GetPreparedTargetRegionPlan(),
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

UCrowdDemoRoundPositionCandidateBuildProcessor::UCrowdDemoRoundPositionCandidateBuildProcessor()
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundPositionCandidateBuildProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UCrowdDemoRoundPositionCandidateBuildProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning
    || !Pipeline->IsPositionCandidateDirty()) return;
  FCrowdDemoPursuitPositioningKernel::BuildCandidates(Pipeline->GetPursuitTargetFact(), 42.0f,
    Pipeline->GetPursuitPositioningSettings(), Pipeline->GetSharedFlowField(),
    Pipeline->GetPreparedPositionCandidates(), Pipeline->GetPositioningSummary());
  Pipeline->GetPreparedPositionAssignments().Reset();
  Pipeline->GetPreparedHoldingCandidates().Reset();
  Pipeline->GetTransitCapacitySelection() = FCrowdDemoTransitCapacityResult();
  Pipeline->GetPreparedHoldingCompatibilities().Reset();
  Pipeline->SetHoldingCompatibilityInputHash(0);
  Pipeline->GetPreparedHoldingAssignments().Reset();
  Pipeline->GetPreparedCommitRequests().Reset();
  Pipeline->GetPreparedCommitGateResult() = FCrowdDemoCommitGateResult();
  Pipeline->MarkPositionCandidatesBuilt();
  Pipeline->LogStageOnce(TEXT("05_position_candidate_build"),
    Pipeline->GetPreparedPositionCandidates().Num());
}

UCrowdDemoRoundPositionAssignmentProcessor::UCrowdDemoRoundPositionAssignmentProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundPositionAssignmentProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundPositionAssignmentProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning
    || !Pipeline->GetPreparedPositionAssignments().IsEmpty()) return;
  const FCrowdDemoTransitCapacityResult& Capacity = Pipeline->GetTransitCapacitySelection();
  if (!Capacity.bValid || Capacity.PositionCapacityDeficit != 0
    || Capacity.HoldingCapacityDeficit != 0) return;
  TArray<FCrowdDemoPositioningAgent> Agents;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Existing = ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      auto& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.Location = FVector2f(States[It].Location.X, States[It].Location.Y);
      Agent.RadiusCm = Formations[It].RadiusCm;
      Agent.ExistingPositionId = Existing[It].PositionId;
      Agent.ExistingRole = Existing[It].Role;
      Agent.ExistingState = Existing[It].State;
    }
  });
  FCrowdDemoPursuitPositioningKernel::Assign(Agents, Pipeline->GetPreparedPositionCandidates(),
    Pipeline->GetPursuitTargetFact(), Pipeline->GetPursuitPositioningSettings(),
    Pipeline->GetPreparedPositionAssignments(), Pipeline->GetPositioningSummary());
  const int32 Revision = Pipeline->AllocatePositionAssignmentRevision();
  TMap<int32, const FCrowdDemoPositionAssignment*> ById;
  TArray<int32> PromotionTransitionAgentIds;
  for (const auto& Assignment : Pipeline->GetPreparedPositionAssignments())
    ById.Add(Assignment.AgentId, &Assignment);
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Fragments = ChunkContext.GetMutableFragmentView<FCrowdDemoPositionAssignmentFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (const FCrowdDemoPositionAssignment* const* Found = ById.Find(Identities[It].Id))
      {
        auto& Fragment = Fragments[It];
        if (Fragment.PositionId != INDEX_NONE
          && Fragment.Role == ECrowdDemoPositionRole::Reserve
          && (*Found)->Role == ECrowdDemoPositionRole::Front)
        {
          PromotionTransitionAgentIds.Add(Identities[It].Id);
        }
        Fragment.TargetId = Pipeline->GetPursuitTargetFact().TargetId;
        Fragment.PositionId = (*Found)->PositionId;
        Fragment.AssignmentRevision = Revision;
        Fragment.Role = (*Found)->Role;
        // Steering-first boundary apply is the sole SF4 movement-state writer.
        Fragment.State = ECrowdDemoPursuitPositionState::Pursuit;
        Fragment.DesiredLocation = FVector((*Found)->DesiredLocation.X, (*Found)->DesiredLocation.Y, 60.0f);
        Fragment.LocalOffset = Fragment.DesiredLocation
          - FVector(Pipeline->GetPursuitTargetFact().Location.X, Pipeline->GetPursuitTargetFact().Location.Y, 60.0f);
        Fragment.LastReassignmentStep = Pipeline->GetCurrentFixedStepIndex();
        Fragment.FrontCommitGrantedStep = INDEX_NONE;
        Fragment.FrontApproachPhase = ECrowdDemoFrontApproachPhase::None;
        Fragment.RequestedApproachPhase = ECrowdDemoFrontApproachPhase::None;
        Fragment.PhaseReservationDecision = ECrowdDemoFrontPhaseReservationDecision::None;
        Fragment.PhaseReservationRevision = INDEX_NONE;
        Fragment.PhaseReservationHeldSteps = 0;
        Fragment.PhaseReservationInvalidReason = ECrowdDemoFrontPhaseReservationReason::None;
        Fragment.FrontApproachRouteRevision = 0;
        Fragment.FrontApproachBestErrorBucket = MAX_int32;
        Fragment.FrontApproachLastProgressStep = INDEX_NONE;
        Fragment.FrontApproachNoProgressSteps = 0;
        Fragment.FrontApproachPreviousRadialErrorCm = -1.0f;
        Fragment.FrontApproachPreviousErrorBucket = MAX_int32;
        Fragment.FrontApproachComposeBoundarySwitchCount = 0;
        Fragment.bFrontApproachComposeStateInitialized = false;
        Fragment.bFrontApproachWasWithinComposeRange = false;
        Fragment.bFrontApproachRadialErrorImproved = false;
        Fragment.bFrontApproachQuantizedProgressStall = false;
      }
    }
  });
  PromotionTransitionAgentIds.Sort();
  Pipeline->RecordPositionPromotionTransitions(PromotionTransitionAgentIds);
  Pipeline->RecordPositioningMetrics(Pipeline->GetPositioningSummary(), 0, 0, 0, -1.0f);
  Pipeline->LogStageOnce(TEXT("06_position_assignment"), Pipeline->GetPositioningSummary().AssignedCount);
}

#define SF4_STEERING_CTOR(ClassName) \
ClassName::ClassName() : EntityQuery(*this) { ROUND_DYNAMIC_FLAGS; }

SF4_STEERING_CTOR(UCrowdDemoRoundHoldingCandidateBuildProcessor)
SF4_STEERING_CTOR(UCrowdDemoRoundHoldingCompatibilityBuildProcessor)
SF4_STEERING_CTOR(UCrowdDemoRoundHoldingAssignmentProcessor)
SF4_STEERING_CTOR(UCrowdDemoRoundCommitRequestBuildProcessor)
SF4_STEERING_CTOR(UCrowdDemoRoundCommitGateScheduleProcessor)
SF4_STEERING_CTOR(UCrowdDemoRoundSteeringStateBoundaryApplyProcessor)
SF4_STEERING_CTOR(UCrowdDemoRoundSteeringFirstPositionGuidanceProcessor)

#undef SF4_STEERING_CTOR

void UCrowdDemoRoundHoldingCandidateBuildProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager) {}

void UCrowdDemoRoundHoldingCandidateBuildProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning
    || !Pipeline->GetPreparedHoldingCandidates().IsEmpty()) return;
  FCrowdDemoPursuitPositioningKernel::BuildHoldingCandidates(
    Pipeline->GetPursuitTargetFact(), 42.0f, Pipeline->GetPursuitPositioningSettings(),
    Pipeline->GetSharedFlowField(), Pipeline->GetPreparedPositionCandidates(),
    Pipeline->GetPreparedHoldingCandidates(), Pipeline->GetHoldingSummary());
  const int32 RawPositionCount = Pipeline->GetPreparedPositionCandidates().Num();
  const int32 RawHoldingCount = Pipeline->GetPreparedHoldingCandidates().Num();
  TArray<FCrowdDemoTransitCapacityCandidate> PositionCapacityCandidates;
  PositionCapacityCandidates.Reserve(RawPositionCount);
  for (const FCrowdDemoPositionCandidate& Candidate : Pipeline->GetPreparedPositionCandidates())
    PositionCapacityCandidates.Add({Candidate.PositionId, Candidate.WorldLocation});
  TArray<FCrowdDemoTransitCapacityCandidate> HoldingCapacityCandidates;
  HoldingCapacityCandidates.Reserve(RawHoldingCount);
  for (const FCrowdDemoHoldingCandidate& Candidate : Pipeline->GetPreparedHoldingCandidates())
    HoldingCapacityCandidates.Add({Candidate.HoldingId, Candidate.WorldLocation});
  FCrowdDemoTransitCapacitySettings CapacitySettings;
  FCrowdDemoJointVelocityKernel::EvaluateTransitCapacity(CapacitySettings,
    PositionCapacityCandidates, HoldingCapacityCandidates,
    Pipeline->GetTransitCapacitySelection());
  const FCrowdDemoTransitCapacityResult& Capacity = Pipeline->GetTransitCapacitySelection();
  if (Capacity.bValid && Capacity.PositionCapacityDeficit == 0
    && Capacity.HoldingCapacityDeficit == 0)
  {
    const TSet<int32> SelectedPositionIds(Capacity.SelectedPositionIds);
    const TSet<int32> SelectedHoldingIds(Capacity.SelectedHoldingIds);
    Pipeline->GetPreparedPositionCandidates().RemoveAll(
      [&](const FCrowdDemoPositionCandidate& Candidate)
      { return !SelectedPositionIds.Contains(Candidate.PositionId); });
    Pipeline->GetPreparedHoldingCandidates().RemoveAll(
      [&](const FCrowdDemoHoldingCandidate& Candidate)
      { return !SelectedHoldingIds.Contains(Candidate.HoldingId); });

    FCrowdDemoPositioningSummary& PositionSummary = Pipeline->GetPositioningSummary();
    PositionSummary.CandidateCount = Pipeline->GetPreparedPositionCandidates().Num();
    PositionSummary.FrontCapacity = 0;
    PositionSummary.ReserveCapacity = 0;
    PositionSummary.CandidateUnreachableCount = 0;
    uint32 PositionHash = 2166136261u;
    for (const FCrowdDemoPositionCandidate& Candidate : Pipeline->GetPreparedPositionCandidates())
    {
      PositionSummary.FrontCapacity += Candidate.Role == ECrowdDemoPositionRole::Front ? 1 : 0;
      PositionSummary.ReserveCapacity += Candidate.Role == ECrowdDemoPositionRole::Reserve ? 1 : 0;
      PositionSummary.CandidateUnreachableCount +=
        !Candidate.bReachable || !Candidate.bClearanceValid ? 1 : 0;
      PositionHash = (PositionHash ^ static_cast<uint32>(Candidate.PositionId)) * 16777619u;
      PositionHash = (PositionHash ^ static_cast<uint32>(Candidate.StableCellKey)) * 16777619u;
      PositionHash = (PositionHash ^ static_cast<uint32>(Candidate.Role)) * 16777619u;
    }
    PositionSummary.CandidateHash = PositionHash;

    FCrowdDemoHoldingSummary& HoldingSummary = Pipeline->GetHoldingSummary();
    HoldingSummary.CandidateCount = Pipeline->GetPreparedHoldingCandidates().Num();
    uint32 HoldingHash = 2166136261u;
    for (const FCrowdDemoHoldingCandidate& Candidate : Pipeline->GetPreparedHoldingCandidates())
    {
      HoldingHash = (HoldingHash ^ static_cast<uint32>(Candidate.HoldingId)) * 16777619u;
      HoldingHash = (HoldingHash ^ static_cast<uint32>(Candidate.RadialBand)) * 16777619u;
      HoldingHash = (HoldingHash ^ static_cast<uint32>(Candidate.AngularSector)) * 16777619u;
      HoldingHash = (HoldingHash ^ static_cast<uint32>(Candidate.StableCellKey)) * 16777619u;
    }
    HoldingSummary.CandidateHash = HoldingHash;
  }
  Pipeline->RecordTransitCapacitySelection();
  Pipeline->LogStageOnce(TEXT("05b_transit_capacity_selection"),
    Capacity.PositionCapacity + Capacity.HoldingCapacity);
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoTransitCapacitySelection role=%s raw_position=%d raw_holding=%d selected_position=%d selected_holding=%d position_deficit=%d holding_deficit=%d applied=%d hash=%u source=MassPipeline"),
    World->GetNetMode() != NM_Client ? TEXT("server") : TEXT("client"),
    RawPositionCount, RawHoldingCount, Capacity.PositionCapacity, Capacity.HoldingCapacity,
    Capacity.PositionCapacityDeficit, Capacity.HoldingCapacityDeficit,
    Capacity.bValid && Capacity.PositionCapacityDeficit == 0
      && Capacity.HoldingCapacityDeficit == 0 ? 1 : 0,
    Capacity.CapacityHash);
}

void UCrowdDemoRoundHoldingCompatibilityBuildProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundHoldingCompatibilityBuildProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  TArray<FCrowdDemoPositionIngressBlocker> Blockers;
  struct FBlockerHashRecord { int32 AgentId; int32 State; int32 PositionId; int32 X10; int32 Y10; int32 Radius; };
  TArray<FBlockerHashRecord> BlockerHashRecords;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Ids = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Position = ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    const auto Steering = ChunkContext.GetFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (Steering[It].SteeringState != ECrowdDemoPursuitSteeringState::StableOccupied
        && Steering[It].SteeringState != ECrowdDemoPursuitSteeringState::ReserveHold) continue;
      auto& Blocker = Blockers.AddDefaulted_GetRef();
      Blocker.AgentId = Ids[It].Id;
      Blocker.PositionId = Position[It].PositionId;
      Blocker.TargetRevision = Steering[It].TargetRevision;
      Blocker.State = Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::StableOccupied
        ? ECrowdDemoPursuitPositionState::StableOccupied : ECrowdDemoPursuitPositionState::ReserveHold;
      Blocker.Location = FVector2f(States[It].Location.X, States[It].Location.Y);
      Blocker.RadiusCm = Formations[It].RadiusCm;
      BlockerHashRecords.Add({Ids[It].Id, static_cast<int32>(Steering[It].SteeringState),
        Position[It].PositionId,
        static_cast<int32>(FMath::RoundToInt(States[It].Location.X / 10.0f)),
        static_cast<int32>(FMath::RoundToInt(States[It].Location.Y / 10.0f)),
        static_cast<int32>(FMath::RoundToInt(Formations[It].RadiusCm))});
    }
  });
  Blockers.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  BlockerHashRecords.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  const auto FoldInput = [](const uint32 Hash, const uint32 Value)
  { return (Hash ^ Value) * 16777619u; };
  uint32 InputHash = 2166136261u;
  InputHash = FoldInput(InputHash, static_cast<uint32>(Pipeline->GetPursuitTargetFact().TargetId));
  InputHash = FoldInput(InputHash, static_cast<uint32>(Pipeline->GetPursuitTargetFact().Revision));
  InputHash = FoldInput(InputHash, Pipeline->GetSharedFlowField().BuildHash);
  InputHash = FoldInput(InputHash, Pipeline->GetPositioningSummary().CandidateHash);
  for (const FBlockerHashRecord& Record : BlockerHashRecords)
  {
    InputHash = FoldInput(InputHash, static_cast<uint32>(Record.AgentId));
    InputHash = FoldInput(InputHash, static_cast<uint32>(Record.State));
    InputHash = FoldInput(InputHash, static_cast<uint32>(Record.PositionId));
    InputHash = FoldInput(InputHash, static_cast<uint32>(Record.X10));
    InputHash = FoldInput(InputHash, static_cast<uint32>(Record.Y10));
    InputHash = FoldInput(InputHash, static_cast<uint32>(Record.Radius));
  }
  if (!Pipeline->GetPreparedHoldingCompatibilities().IsEmpty()
    && Pipeline->GetHoldingCompatibilityInputHash() == InputHash) return;
  auto& Edges = Pipeline->GetPreparedHoldingCompatibilities();
  Edges.Reset();
  for (const FCrowdDemoHoldingCandidate& Holding : Pipeline->GetPreparedHoldingCandidates())
    for (const FCrowdDemoPositionCandidate& Position : Pipeline->GetPreparedPositionCandidates())
      Edges.Add(FCrowdDemoPursuitPositioningKernel::EvaluateHoldingPositionCompatibility(
        Pipeline->GetPursuitTargetFact(), 42.0f, Pipeline->GetPursuitPositioningSettings(),
        Pipeline->GetSharedFlowField(), Holding, Position, Blockers));
  Edges.Sort([](const auto& A, const auto& B)
  {
    if (A.HoldingId != B.HoldingId) return A.HoldingId < B.HoldingId;
    return A.PositionId < B.PositionId;
  });
  auto& Summary = Pipeline->GetHoldingSummary();
  Summary.CompatibilityCount = Edges.Num();
  uint32 Hash = 2166136261u;
  for (const auto& Edge : Edges) Hash = (Hash ^ Edge.StableHash) * 16777619u;
  Summary.CompatibilityHash = Hash;
  Pipeline->SetHoldingCompatibilityInputHash(InputHash);
}

void UCrowdDemoRoundHoldingAssignmentProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundHoldingAssignmentProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  TArray<FCrowdDemoHoldingAgent> Agents;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Ids = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Positions = ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    const auto Steering = ChunkContext.GetFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      auto& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Ids[It].Id;
      Agent.Location = FVector2f(States[It].Location.X, States[It].Location.Y);
      Agent.RadiusCm = Formations[It].RadiusCm;
      Agent.WaitEpoch = Steering[It].WaitEpoch;
      Agent.PositionId = Positions[It].PositionId;
      Agent.AssignedPosition = FVector2f(Positions[It].DesiredLocation.X, Positions[It].DesiredLocation.Y);
      Agent.PositionRole = Positions[It].Role;
      Agent.PositionIngressCost = Positions[It].PositionId;
      Agent.ExistingHoldingId = Steering[It].HoldingId;
      Agent.ExistingTargetRevision = Steering[It].TargetRevision;
      Agent.ExistingState = Steering[It].SteeringState;
      Agent.bPositionValid = Positions[It].PositionId != INDEX_NONE
        && Positions[It].TargetId == Pipeline->GetPursuitTargetFact().TargetId;
    }
  });
  Agents.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  uint32 InputHash=(Pipeline->GetHoldingCompatibilityInputHash()^2166136261u)*16777619u;
  for(const auto& Agent:Agents){InputHash=(InputHash^static_cast<uint32>(Agent.AgentId))*16777619u;
    InputHash=(InputHash^static_cast<uint32>(Agent.PositionId))*16777619u;
    InputHash=(InputHash^static_cast<uint32>(Agent.ExistingHoldingId))*16777619u;
    const uint32 OwnerClass=Agent.ExistingState==ECrowdDemoPursuitSteeringState::StableOccupied
      ||Agent.ExistingState==ECrowdDemoPursuitSteeringState::ReserveHold
      ||Agent.ExistingState==ECrowdDemoPursuitSteeringState::Commit
      ?static_cast<uint32>(Agent.ExistingState):0u;
    InputHash=(InputHash^OwnerClass)*16777619u;
    InputHash=(InputHash^static_cast<uint32>(Agent.ExistingTargetRevision))*16777619u;}
  if(Pipeline->GetJointAssignmentInputHash()==InputHash
    &&Pipeline->GetPreparedHoldingAssignments().Num()==Agents.Num())return;
  TArray<FCrowdDemoJointPositioningAgent> JointAgents;
  TArray<FCrowdDemoJointAgentHoldingEdge> AgentHoldingEdges;
  for(auto& Agent:Agents)
  {
    const auto* ExistingEdge=Pipeline->GetPreparedHoldingCompatibilities().FindByPredicate(
      [&](const auto&E){return E.HoldingId==Agent.ExistingHoldingId&&E.PositionId==Agent.PositionId;});
    const bool bCompleted=Agent.ExistingState==ECrowdDemoPursuitSteeringState::StableOccupied
      ||Agent.ExistingState==ECrowdDemoPursuitSteeringState::ReserveHold;
    Agent.bExistingOwnerHardValid=Agent.bPositionValid&&Agent.ExistingHoldingId!=INDEX_NONE
      &&(bCompleted||(ExistingEdge&&ExistingEdge->bCompatible));
    auto& J=JointAgents.AddDefaulted_GetRef();J.AgentId=Agent.AgentId;J.Location=Agent.Location;
    J.RadiusCm=Agent.RadiusCm;J.WaitEpoch=Agent.WaitEpoch;J.ExistingHoldingId=Agent.ExistingHoldingId;
    J.ExistingPositionId=Agent.PositionId;J.TargetRevision=Agent.ExistingTargetRevision;
    J.State=Agent.ExistingState;J.bExistingHardOwnerValid=Agent.bExistingOwnerHardValid;
    const bool bCurrentReachable=FCrowdDemoSharedFlowFieldKernel::Sample(Pipeline->GetSharedFlowField(),
      FVector(J.Location.X,J.Location.Y,60.0f)).Status==ECrowdDemoFlowLocationStatus::Reachable;
    for(const auto& Holding:Pipeline->GetPreparedHoldingCandidates())
    {auto&E=AgentHoldingEdges.AddDefaulted_GetRef();E.AgentId=J.AgentId;E.HoldingId=Holding.HoldingId;
      E.QuantizedCurrentToHoldingCostCm=FMath::RoundToInt((Holding.WorldLocation-J.Location).Size());
      E.bLocallyReachable=bCurrentReachable&&Holding.bReachable&&Holding.bClearanceValid
        &&Holding.TargetRevision==Pipeline->GetPursuitTargetFact().Revision;}
  }
  FCrowdDemoJointPositioningResult Joint;
  FCrowdDemoPursuitPositioningKernel::PlanJointHoldingPositions(
    Pipeline->GetPursuitTargetFact().Revision,JointAgents,Pipeline->GetPreparedHoldingCandidates(),
    Pipeline->GetPreparedPositionCandidates(),AgentHoldingEdges,
    Pipeline->GetPreparedHoldingCompatibilities(),Joint);
  if(!Joint.bValid||Joint.MaximumCardinality!=Agents.Num())return;
  TArray<FCrowdDemoPositionAssignment> NewPositions;
  TArray<FCrowdDemoHoldingAssignment> NewHoldings;
  for(const auto& J:Joint.Assignments)
  {
    const auto* Position=Pipeline->GetPreparedPositionCandidates().FindByPredicate(
      [&](const auto&P){return P.PositionId==J.PositionId;});
    const auto* Holding=Pipeline->GetPreparedHoldingCandidates().FindByPredicate(
      [&](const auto&H){return H.HoldingId==J.HoldingId;});
    const auto* Edge=Pipeline->GetPreparedHoldingCompatibilities().FindByPredicate(
      [&](const auto&E){return E.HoldingId==J.HoldingId&&E.PositionId==J.PositionId;});
    const auto* Agent=Agents.FindByPredicate([&](const auto&A){return A.AgentId==J.AgentId;});
    if(!Position||!Holding||!Agent)return;
    auto& PA=NewPositions.AddDefaulted_GetRef();PA.AgentId=J.AgentId;PA.PositionId=J.PositionId;
    PA.Role=Position->Role;PA.DesiredLocation=Position->WorldLocation;PA.IntegerCost=Position->PositionId;
    PA.bReused=J.bReusedPosition;
    auto& HA=NewHoldings.AddDefaulted_GetRef();HA.AgentId=J.AgentId;HA.HoldingId=J.HoldingId;
    HA.PositionId=J.PositionId;HA.HoldingLocation=Holding->WorldLocation;
    HA.AssignedPosition=Position->WorldLocation;HA.State=J.bHardLocked?Agent->ExistingState:
      ECrowdDemoPursuitSteeringState::Holding;HA.IntegerCost=Edge?Edge->QuantizedRouteCostCm:0;
    HA.CompatibilityHash=Edge?Edge->StableHash:2166136261u;
    HA.bCompatibilityValid=J.bHardLocked||(Edge&&Edge->bCompatible);HA.bReused=J.bReusedHolding;
  }
  NewPositions.Sort([](const auto&A,const auto&B){return A.AgentId<B.AgentId;});
  NewHoldings.Sort([](const auto&A,const auto&B){return A.AgentId<B.AgentId;});
  const int32 Revision=Pipeline->AllocatePositionAssignmentRevision();
  TMap<int32,const FCrowdDemoPositionAssignment*> PositionByAgent;
  for(const auto& P:NewPositions)PositionByAgent.Add(P.AgentId,&P);
  Pipeline->GetPreparedPositionAssignments()=NewPositions;
  Pipeline->GetPreparedHoldingAssignments()=NewHoldings;
  Pipeline->GetJointPositioningResult()=Joint;
  Pipeline->RecordJointPositioningDiagnostic();
  Pipeline->SetJointAssignmentInputHash(InputHash);
  EntityQuery.ForEachEntityChunk(Context,[&](FMassExecutionContext& ChunkContext)
  {const auto Ids=ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Fragments=ChunkContext.GetMutableFragmentView<FCrowdDemoPositionAssignmentFragment>();
    for(FMassExecutionContext::FEntityIterator It=ChunkContext.CreateEntityIterator();It;++It)
      if(const auto* const* Found=PositionByAgent.Find(Ids[It].Id))
      {auto& F=Fragments[It];F.TargetId=Pipeline->GetPursuitTargetFact().TargetId;
        F.PositionId=(*Found)->PositionId;F.AssignmentRevision=Revision;F.Role=(*Found)->Role;
        F.DesiredLocation=FVector((*Found)->DesiredLocation.X,(*Found)->DesiredLocation.Y,60.0f);
        F.LocalOffset=F.DesiredLocation-FVector(Pipeline->GetPursuitTargetFact().Location.X,
          Pipeline->GetPursuitTargetFact().Location.Y,60.0f);F.LastReassignmentStep=Pipeline->GetCurrentFixedStepIndex();}}
  );
  auto& Summary=Pipeline->GetHoldingSummary();Summary.AssignedCount=NewHoldings.Num();
  Summary.UnassignedCount=Agents.Num()-NewHoldings.Num();Summary.AssignmentHash=Joint.StableHash;
}

void UCrowdDemoRoundCommitRequestBuildProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundCommitRequestBuildProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  TMap<int32, const FCrowdDemoHoldingAssignment*> HoldingByAgent;
  for (const auto& Assignment : Pipeline->GetPreparedHoldingAssignments()) HoldingByAgent.Add(Assignment.AgentId, &Assignment);
  auto& Requests = Pipeline->GetPreparedCommitRequests(); Requests.Reset();
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Ids = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Positions = ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    const auto Steering = ChunkContext.GetFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdDemoHoldingAssignment* const* Found = HoldingByAgent.Find(Ids[It].Id);
      if (!Found || (*Found)->HoldingId == INDEX_NONE) continue;
      if (Steering[It].SteeringState != ECrowdDemoPursuitSteeringState::Holding
        && Steering[It].SteeringState != ECrowdDemoPursuitSteeringState::Commit) continue;
      auto& Request = Requests.AddDefaulted_GetRef();
      Request.AgentId = Ids[It].Id; Request.HoldingId = (*Found)->HoldingId;
      Request.PositionId = Positions[It].PositionId;
      Request.TargetRevision = Pipeline->GetPursuitTargetFact().Revision;
      Request.WaitEpoch = Steering[It].WaitEpoch;
      Request.PositionFillCost = Positions[It].PositionId;
      Request.Location = FVector2f(States[It].Location.X, States[It].Location.Y);
      Request.HoldingLocation = (*Found)->HoldingLocation;
      Request.AssignedPosition = (*Found)->AssignedPosition;
      Request.Velocity = FVector2f(States[It].Velocity.X, States[It].Velocity.Y);
      Request.RadiusCm = Formations[It].RadiusCm;
      Request.QuantizedCommitCostCm = FMath::RoundToInt((Request.AssignedPosition - Request.Location).Size());
      Request.bPositionValid = Positions[It].PositionId != INDEX_NONE;
      // Consume the exact edge selected by AssignHoldingPositions for this
      // prepared revision. Re-keying the graph here can select a different
      // record when stable IDs alias and breaks the assignment proof.
      Request.bCompatibilityFound = (*Found)->CompatibilityHash != 0;
      Request.bCompatibilityValid = (*Found)->bCompatibilityValid;
      Request.bAlreadyCommit = Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::Commit;
    }
  });
  Requests.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
}

void UCrowdDemoRoundCommitGateScheduleProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundCommitGateScheduleProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  TArray<FCrowdDemoPositionIngressBlocker> Blockers;
  TArray<FCrowdDemoJointPositioningAgent> JointAgents;
  TMap<int32,const FCrowdDemoHoldingAssignment*> HoldingByAgent;
  for(const auto& H:Pipeline->GetPreparedHoldingAssignments())HoldingByAgent.Add(H.AgentId,&H);
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Ids = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Positions = ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    const auto Steering = ChunkContext.GetFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const auto* const* ExistingHolding=HoldingByAgent.Find(Ids[It].Id);
      auto& J=JointAgents.AddDefaulted_GetRef();J.AgentId=Ids[It].Id;
      J.Location=FVector2f(States[It].Location.X,States[It].Location.Y);
      J.RadiusCm=Formations[It].RadiusCm;J.WaitEpoch=Steering[It].WaitEpoch;
      J.ExistingHoldingId=ExistingHolding?(*ExistingHolding)->HoldingId:INDEX_NONE;
      J.ExistingPositionId=Positions[It].PositionId;J.TargetRevision=Steering[It].TargetRevision;
      J.State=Steering[It].SteeringState;J.bExistingHardOwnerValid=ExistingHolding
        &&(*ExistingHolding)->bCompatibilityValid;
      if (Steering[It].SteeringState != ECrowdDemoPursuitSteeringState::StableOccupied
        && Steering[It].SteeringState != ECrowdDemoPursuitSteeringState::ReserveHold) continue;
      auto& B = Blockers.AddDefaulted_GetRef(); B.AgentId = Ids[It].Id;
      B.PositionId=Positions[It].PositionId;B.TargetRevision=Steering[It].TargetRevision;
      B.State = Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::StableOccupied
        ? ECrowdDemoPursuitPositionState::StableOccupied : ECrowdDemoPursuitPositionState::ReserveHold;
      B.Location = FVector2f(States[It].Location.X, States[It].Location.Y); B.RadiusCm = Formations[It].RadiusCm;
    }
  });
  Blockers.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  FCrowdDemoPursuitPositioningKernel::ScheduleCommitGate(
    Pipeline->GetPursuitTargetFact(), Pipeline->GetPursuitPositioningSettings(),
    Pipeline->GetSharedFlowField(), Pipeline->GetPreparedCommitRequests(), Blockers,
    Pipeline->GetPreparedCommitGateResult());
  TArray<FCrowdDemoJointAgentHoldingEdge> AgentHoldingEdges;
  for(const auto& Agent:JointAgents)
  {const bool bReachable=FCrowdDemoSharedFlowFieldKernel::Sample(Pipeline->GetSharedFlowField(),
      FVector(Agent.Location.X,Agent.Location.Y,60.0f)).Status==ECrowdDemoFlowLocationStatus::Reachable;
    for(const auto& Holding:Pipeline->GetPreparedHoldingCandidates())
    {auto&E=AgentHoldingEdges.AddDefaulted_GetRef();E.AgentId=Agent.AgentId;E.HoldingId=Holding.HoldingId;
      E.QuantizedCurrentToHoldingCostCm=FMath::RoundToInt((Holding.WorldLocation-Agent.Location).Size());
      E.bLocallyReachable=bReachable&&Holding.bReachable&&Holding.bClearanceValid;}}
  FCrowdDemoPursuitPositioningKernel::ApplyJointResidualCommitGate(
    Pipeline->GetPursuitTargetFact().Revision,Pipeline->GetPursuitPositioningSettings(),
    JointAgents,Pipeline->GetPreparedHoldingCandidates(),Pipeline->GetPreparedPositionCandidates(),
    AgentHoldingEdges,Pipeline->GetPreparedHoldingCompatibilities(),
    Pipeline->GetJointPositioningResult(),Pipeline->GetPreparedCommitGateResult(),
    Pipeline->GetJointCommitResidualResult());
  Pipeline->RecordJointCommitResidualDiagnostic();
}

void UCrowdDemoRoundSteeringStateBoundaryApplyProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundSteeringStateBoundaryApplyProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  TMap<int32, const FCrowdDemoHoldingAssignment*> HoldingByAgent;
  for (const auto& A : Pipeline->GetPreparedHoldingAssignments()) HoldingByAgent.Add(A.AgentId, &A);
  TMap<int32, ECrowdDemoCommitDecision> DecisionByAgent;
  for (const auto& D : Pipeline->GetPreparedCommitGateResult().Decisions) DecisionByAgent.Add(D.AgentId, D.Decision);
  struct FSteeringHashRecord { int32 AgentId; ECrowdDemoPursuitSteeringState State; int32 HoldingId; int32 PositionId; int32 Revision; };
  TArray<FSteeringHashRecord> HashRecords;
  int32 Counts[6] = {}; int32 Arrived = 0, Releases = 0, CommitReleases = 0, NoProgress = 0;
  const int32 Step = Pipeline->GetCurrentFixedStepIndex();
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Ids = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Flows = ChunkContext.GetFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    const auto Positions = ChunkContext.GetMutableFragmentView<FCrowdDemoPositionAssignmentFragment>();
    const auto Steering = ChunkContext.GetMutableFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      auto& S = Steering[It]; const auto Previous = S.SteeringState;
      const FCrowdDemoHoldingAssignment* const* HA = HoldingByAgent.Find(Ids[It].Id);
      const bool bValid = HA && (*HA)->HoldingId != INDEX_NONE && Positions[It].PositionId != INDEX_NONE
        && (*HA)->PositionId == Positions[It].PositionId;
      if (S.TargetRevision != INDEX_NONE && S.TargetRevision != Pipeline->GetPursuitTargetFact().Revision)
      { S.SteeringState = ECrowdDemoPursuitSteeringState::Reacquire; S.InvalidReason = ECrowdDemoPursuitSteeringInvalidReason::TargetRevision; }
      else if (!bValid)
      { S.SteeringState = ECrowdDemoPursuitSteeringState::Reacquire; S.InvalidReason = ECrowdDemoPursuitSteeringInvalidReason::HoldingInvalid; }
      else
      {
        S.HoldingId = (*HA)->HoldingId; S.AssignedPositionId = (*HA)->PositionId;
        S.TargetRevision = Pipeline->GetPursuitTargetFact().Revision; S.InvalidReason = ECrowdDemoPursuitSteeringInvalidReason::None;
        if (S.SteeringState == ECrowdDemoPursuitSteeringState::Pursuit
          || S.SteeringState == ECrowdDemoPursuitSteeringState::Reacquire)
        {
          S.SteeringState = FCrowdDemoPursuitPositioningKernel::ShouldEnterHolding(
            FVector2f(States[It].Location.X, States[It].Location.Y), (*HA)->HoldingLocation,
            Flows[It].Status, Pipeline->GetPursuitPositioningSettings(),
            Pipeline->GetRules().FlowFieldConfig)
            ? ECrowdDemoPursuitSteeringState::Holding
            : ECrowdDemoPursuitSteeringState::Pursuit;
        }
        const ECrowdDemoCommitDecision Decision = DecisionByAgent.FindRef(Ids[It].Id);
        if (S.SteeringState == ECrowdDemoPursuitSteeringState::Holding)
        {
          const bool bHoldingPathStillValid =
            FCrowdDemoPursuitPositioningKernel::ShouldEnterHolding(
              FVector2f(States[It].Location.X, States[It].Location.Y),
              (*HA)->HoldingLocation, Flows[It].Status,
              Pipeline->GetPursuitPositioningSettings(),
              Pipeline->GetRules().FlowFieldConfig);
          if (!bHoldingPathStillValid)
          {
            S.SteeringState = ECrowdDemoPursuitSteeringState::Pursuit;
          }
          else if (Decision == ECrowdDemoCommitDecision::Granted
            && S.CommitDecisionRevision != Step)
          { S.SteeringState = ECrowdDemoPursuitSteeringState::Commit; S.CommitDecisionRevision = Step; }
          else if (Decision == ECrowdDemoCommitDecision::Reacquire)
          { S.SteeringState = ECrowdDemoPursuitSteeringState::Reacquire; S.InvalidReason = ECrowdDemoPursuitSteeringInvalidReason::CompatibilityInvalid; }
          else if (Decision == ECrowdDemoCommitDecision::Held && Step % 30 == 0) ++S.WaitEpoch;
        }
        if (S.SteeringState == ECrowdDemoPursuitSteeringState::Commit)
        {
          const float Error = FVector::Dist2D(States[It].Location, Positions[It].DesiredLocation);
          if (Error <= 30.0f && States[It].Velocity.Size2D() <= 30.0f) ++S.StableArrivalStepCount;
          else S.StableArrivalStepCount = 0;
          const int32 Bucket = FMath::RoundToInt(Error);
          if (Bucket < S.LastProgressBucket) { S.LastProgressBucket = Bucket; S.LastProgressFixedStep = Step; }
          if (S.StableArrivalStepCount >= 15)
          { S.SteeringState = Positions[It].Role == ECrowdDemoPositionRole::Front
              ? ECrowdDemoPursuitSteeringState::StableOccupied : ECrowdDemoPursuitSteeringState::ReserveHold; ++Arrived; }
        }
      }
      if (Previous != S.SteeringState)
      { ++S.StateRevision; S.StateEnterFixedStep = Step; Releases += S.SteeringState == ECrowdDemoPursuitSteeringState::Reacquire ? 1 : 0; CommitReleases += Previous == ECrowdDemoPursuitSteeringState::Commit && S.SteeringState == ECrowdDemoPursuitSteeringState::Reacquire ? 1 : 0; }
      if (S.SteeringState == ECrowdDemoPursuitSteeringState::Reacquire) { S.HoldingId = INDEX_NONE; S.CommitDecisionRevision = INDEX_NONE; }
      Positions[It].State = S.SteeringState == ECrowdDemoPursuitSteeringState::StableOccupied ? ECrowdDemoPursuitPositionState::StableOccupied
        : S.SteeringState == ECrowdDemoPursuitSteeringState::ReserveHold ? ECrowdDemoPursuitPositionState::ReserveHold
        : ECrowdDemoPursuitPositionState::Pursuit;
      ++Counts[static_cast<int32>(S.SteeringState)];
      HashRecords.Add({Ids[It].Id, S.SteeringState, S.HoldingId, S.AssignedPositionId, S.StateRevision});
    }
  });
  HashRecords.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  uint32 Hash = 2166136261u;
  for (const auto& R : HashRecords)
  {
    Hash = (Hash ^ static_cast<uint32>(R.AgentId)) * 16777619u;
    Hash = (Hash ^ static_cast<uint32>(R.State)) * 16777619u;
    Hash = (Hash ^ static_cast<uint32>(R.HoldingId)) * 16777619u;
    Hash = (Hash ^ static_cast<uint32>(R.PositionId)) * 16777619u;
    Hash = (Hash ^ static_cast<uint32>(R.Revision)) * 16777619u;
  }
  Pipeline->RecordSteeringFirstMetrics(Hash, Counts[0], Counts[1], Counts[2], Counts[3], Counts[4], Counts[5],
    Arrived, Releases, CommitReleases, NoProgress, 0);
}

void UCrowdDemoRoundSteeringFirstPositionGuidanceProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPursuitGuidanceFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundSteeringFirstPositionGuidanceProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld(); auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  TMap<int32, const FCrowdDemoHoldingAssignment*> HoldingByAgent;
  for (const auto& A : Pipeline->GetPreparedHoldingAssignments()) HoldingByAgent.Add(A.AgentId, &A);
  auto& Prepared = Pipeline->GetPreparedSteeringGuidance(); Prepared.Reset();
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Ids = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>(); const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>(); const auto Steering = ChunkContext.GetFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
    const auto Guidance = ChunkContext.GetMutableFragmentView<FCrowdDemoPursuitGuidanceFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      Guidance[It] = FCrowdDemoPursuitGuidanceFragment(); const auto* const* HA = HoldingByAgent.Find(Ids[It].Id);
      if (!HA) continue;
      const FVector2f Desired = FCrowdDemoPursuitPositioningKernel::BuildSteeringFirstPreferredVelocity(
        Steering[It].SteeringState, FVector2f(States[It].Location.X, States[It].Location.Y),
        FVector2f(Intents[It].DesiredVelocity.X, Intents[It].DesiredVelocity.Y), (*HA)->HoldingLocation,
        (*HA)->AssignedPosition, Pipeline->GetRules().MaxSpeedCmPerSecond, Pipeline->GetPursuitPositioningSettings());
      Guidance[It].DesiredVelocity = FVector(Desired.X, Desired.Y, 0.0f); Guidance[It].bPositioningActive = true;
      auto& Item = Prepared.AddDefaulted_GetRef(); Item.AgentId = Ids[It].Id; Item.DesiredVelocity = Desired;
    }
  });
  Prepared.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
}

UCrowdDemoRoundFrontAdmissionProcessor::UCrowdDemoRoundFrontAdmissionProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

UCrowdDemoRoundPositionApproachRouteProcessor::UCrowdDemoRoundPositionApproachRouteProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundPositionApproachRouteProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundPositionApproachRouteProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  struct FAgentSnapshot
  {
    int32 AgentId = INDEX_NONE;
    FVector2f Location = FVector2f::ZeroVector;
    float RadiusCm = 42.0f;
    FCrowdDemoPositionAssignmentFragment Assignment;
  };
  TArray<FAgentSnapshot> Snapshots;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Assignments = ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FAgentSnapshot& Snapshot = Snapshots.AddDefaulted_GetRef();
      Snapshot.AgentId = Identities[It].Id;
      Snapshot.Location = FVector2f(States[It].Location.X, States[It].Location.Y);
      Snapshot.RadiusCm = Formations[It].RadiusCm;
      Snapshot.Assignment = Assignments[It];
    }
  });
  Snapshots.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  TArray<FCrowdDemoPositionIngressBlocker> Blockers;
  for (const FAgentSnapshot& Snapshot : Snapshots)
  {
    if (Snapshot.Assignment.State != ECrowdDemoPursuitPositionState::StableOccupied
      && Snapshot.Assignment.State != ECrowdDemoPursuitPositionState::ReserveHold) continue;
    FCrowdDemoPositionIngressBlocker& Blocker = Blockers.AddDefaulted_GetRef();
    Blocker.AgentId = Snapshot.AgentId;
    Blocker.PositionId = Snapshot.Assignment.PositionId;
    Blocker.State = Snapshot.Assignment.State;
    Blocker.Location = Snapshot.Location;
    Blocker.RadiusCm = Snapshot.RadiusCm;
  }
  TMap<int32, const FCrowdDemoPositionCandidate*> CandidateById;
  for (const FCrowdDemoPositionCandidate& Candidate : Pipeline->GetPreparedPositionCandidates())
    CandidateById.Add(Candidate.PositionId, &Candidate);
  TArray<FCrowdDemoFrontApproachRoute>& Routes = Pipeline->GetPreparedPositionApproachRoutes();
  Routes.Reset();
  TArray<FCrowdDemoFrontPhaseReservationRequest>& ReservationRequests =
    Pipeline->GetPreparedFrontPhaseReservationRequests();
  ReservationRequests.Reset();
  for (const FAgentSnapshot& Snapshot : Snapshots)
  {
    if (Snapshot.Assignment.Role != ECrowdDemoPositionRole::Front
      || Snapshot.Assignment.State == ECrowdDemoPursuitPositionState::StableOccupied) continue;
    const FCrowdDemoPositionCandidate* const* Candidate = CandidateById.Find(Snapshot.Assignment.PositionId);
    if (!Candidate) continue;
    FCrowdDemoFrontApproachRoute& Route = Routes.Add_GetRef(
      FCrowdDemoPursuitPositioningKernel::BuildFrontApproachRoute(
      Pipeline->GetPursuitTargetFact(), Pipeline->GetPursuitPositioningSettings(),
      Pipeline->GetSharedFlowField(), Snapshot.AgentId, Snapshot.RadiusCm,
      Snapshot.Location, **Candidate, Blockers, Pipeline->GetRules().MaxSpeedCmPerSecond,
      Snapshot.Assignment.AssignmentRevision));
    FCrowdDemoFrontPhaseReservationRequest& Request = ReservationRequests.AddDefaulted_GetRef();
    Request.AgentId = Snapshot.AgentId;
    Request.CommitGrantedStep = Snapshot.Assignment.FrontCommitGrantedStep;
    Request.RadiusCm = Snapshot.RadiusCm;
    Request.CurrentPhase = Snapshot.Assignment.FrontApproachPhase;
    if (Request.CurrentPhase == ECrowdDemoFrontApproachPhase::None)
      Request.RequestedPhase = ECrowdDemoFrontApproachPhase::RadialStage;
    else if (Request.CurrentPhase == ECrowdDemoFrontApproachPhase::RadialStage)
      Request.RequestedPhase = ECrowdDemoFrontApproachPhase::AngularAlign;
    else if (Request.CurrentPhase == ECrowdDemoFrontApproachPhase::AngularAlign)
      Request.RequestedPhase = ECrowdDemoFrontApproachPhase::RadialCommit;
    Request.bHasRequest =
      (Request.CurrentPhase == ECrowdDemoFrontApproachPhase::RadialStage
        && Route.RadialErrorCm <= Pipeline->GetPursuitPositioningSettings().FrontApproachRadialToleranceCm
          + Pipeline->GetPursuitPositioningSettings().SafetyGapCm)
      || (Request.CurrentPhase == ECrowdDemoFrontApproachPhase::AngularAlign
        && Route.AngularErrorRadians
          <= Pipeline->GetPursuitPositioningSettings().FrontApproachAngularCommitToleranceRadians);
    Request.bRequestValid = Request.RequestedPhase
      != ECrowdDemoFrontApproachPhase::None && Route.bGateReachable;
    if (Request.RequestedPhase == ECrowdDemoFrontApproachPhase::AngularAlign)
      Request.bRequestValid &= Route.bArcReachable;
    else if (Request.RequestedPhase == ECrowdDemoFrontApproachPhase::RadialCommit)
      Request.bRequestValid &= Route.bArcReachable && Route.bRadialCommitClear;
    Request.bTargetExclusionClear = Route.bTargetExclusionClear;
    FCrowdDemoPursuitPositioningKernel::BuildFrontPhaseReservationPoints(
      Route, Request.CurrentPhase, Snapshot.Location, Request.CurrentReservationPoints);
    FCrowdDemoPursuitPositioningKernel::BuildFrontPhaseReservationPoints(
      Route, Request.RequestedPhase, Snapshot.Location, Request.RequestedReservationPoints);
  }
  Routes.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  ReservationRequests.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  Pipeline->LogStageOnce(TEXT("07_position_approach_route_request"), Routes.Num());
}

void UCrowdDemoRoundFrontAdmissionProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundFrontAdmissionProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  TArray<FCrowdDemoFrontAdmissionAgent> Agents;
  TMap<int32, FCrowdDemoFrontPhaseReservationRequest*> ReservationRequestByAgentId;
  for (FCrowdDemoFrontPhaseReservationRequest& Request :
    Pipeline->GetPreparedFrontPhaseReservationRequests())
  {
    ReservationRequestByAgentId.Add(Request.AgentId, &Request);
  }
  TMap<int32, const FCrowdDemoFrontApproachRoute*> RouteByAgentId;
  for (const FCrowdDemoFrontApproachRoute& Route : Pipeline->GetPreparedPositionApproachRoutes())
    RouteByAgentId.Add(Route.AgentId, &Route);
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Assignments = ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoFrontAdmissionAgent& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.Location = FVector2f(States[It].Location.X, States[It].Location.Y);
      Agent.RadiusCm = Formations[It].RadiusCm;
      Agent.PositionId = Assignments[It].PositionId;
      Agent.Role = Assignments[It].Role;
      Agent.State = Assignments[It].State;
      Agent.ApproachPhase = Assignments[It].FrontApproachPhase;
      Agent.CommitGrantedStep = Assignments[It].FrontCommitGrantedStep;
      Agent.NoProgressSteps = Assignments[It].FrontApproachNoProgressSteps;
      if (const FCrowdDemoFrontApproachRoute* const* Route = RouteByAgentId.Find(Agent.AgentId))
      {
        if (FCrowdDemoFrontPhaseReservationRequest* const* Request =
          ReservationRequestByAgentId.Find(Agent.AgentId))
        {
          Agent.bRouteValid = (*Request)->bRequestValid
            && (*Request)->bTargetExclusionClear;
          Agent.RoutePoints = Agent.State == ECrowdDemoPursuitPositionState::FrontAssignedWaiting
            ? (*Request)->RequestedReservationPoints : (*Request)->CurrentReservationPoints;
        }
      }
    }
  });
  FVector2f EntryAxis(0.0f, -1.0f);
  if (!Pipeline->GetRules().TrafficCohorts.IsEmpty())
  {
    const FVector Spawn = Pipeline->GetRules().TrafficCohorts[0].SpawnOrigin;
    EntryAxis = (FVector2f(Spawn.X, Spawn.Y) - Pipeline->GetPursuitTargetFact().Location).GetSafeNormal();
  }
  FCrowdDemoFrontAdmissionResult& Result = Pipeline->GetPreparedFrontAdmissionResult();
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontAdmission(
    EntryAxis, Pipeline->GetCurrentFixedStepIndex(), Pipeline->GetPursuitPositioningSettings(),
    Pipeline->GetPreparedPositionCandidates(), Agents, Result);
  TSet<int32> Granted, Requeued;
  for (const int32 AgentId : Result.GrantedAgentIds) Granted.Add(AgentId);
  for (const int32 AgentId : Result.RequeuedAgentIds) Requeued.Add(AgentId);
  for (FCrowdDemoFrontPhaseReservationRequest& Request :
    Pipeline->GetPreparedFrontPhaseReservationRequests())
  {
    if (Granted.Contains(Request.AgentId)
      && Request.CurrentPhase == ECrowdDemoFrontApproachPhase::None)
      Request.bHasRequest = true;
    if (Requeued.Contains(Request.AgentId)) Request.bHasRequest = false;
  }
  Pipeline->RecordFrontAdmission(Result);
  Pipeline->LogStageOnce(TEXT("08_front_admission_eligibility"), Result.GrantedAgentIds.Num());
}

UCrowdDemoRoundFrontPhaseReservationProcessor::UCrowdDemoRoundFrontPhaseReservationProcessor()
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundFrontPhaseReservationProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UCrowdDemoRoundFrontPhaseReservationProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario
    != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  TArray<FCrowdDemoFrontPhaseReservationRequest>& Requests =
    Pipeline->GetPreparedFrontPhaseReservationRequests();
  Requests.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  FCrowdDemoFrontPhaseReservationResult& Result =
    Pipeline->GetPreparedFrontPhaseReservationResult();
  FCrowdDemoPursuitPositioningKernel::ScheduleFrontPhaseReservations(
    Pipeline->GetPursuitPositioningSettings(), Requests, Result);
  TSet<int32> Granted(Result.GrantedAgentIds);
  TSet<int32> Held(Result.HeldAgentIds);
  TSet<int32> Invalid(Result.InvalidAgentIds);
  TSet<int32> Requeued(Pipeline->GetPreparedFrontAdmissionResult().RequeuedAgentIds);
  TArray<FCrowdDemoFrontPhaseReservationDecisionRecord>& Decisions =
    Pipeline->GetPreparedFrontPhaseReservationDecisions();
  Decisions.Reset();
  for (const FCrowdDemoFrontPhaseReservationRequest& Request : Requests)
  {
    if (!Request.bHasRequest && !Requeued.Contains(Request.AgentId)) continue;
    FCrowdDemoFrontPhaseReservationDecisionRecord& Decision = Decisions.AddDefaulted_GetRef();
    Decision.AgentId = Request.AgentId;
    Decision.CurrentPhase = Request.CurrentPhase;
    Decision.RequestedPhase = Request.RequestedPhase;
    if (Requeued.Contains(Request.AgentId))
    {
      Decision.Decision = ECrowdDemoFrontPhaseReservationDecision::Invalid;
      Decision.Reason = ECrowdDemoFrontPhaseReservationReason::AdmissionRequeue;
    }
    else if (Granted.Contains(Request.AgentId))
    {
      Decision.Decision = ECrowdDemoFrontPhaseReservationDecision::Granted;
    }
    else if (Held.Contains(Request.AgentId))
    {
      Decision.Decision = ECrowdDemoFrontPhaseReservationDecision::Held;
      Decision.Reason = ECrowdDemoFrontPhaseReservationReason::RouteConflict;
    }
    else if (Invalid.Contains(Request.AgentId))
    {
      Decision.Decision = ECrowdDemoFrontPhaseReservationDecision::Invalid;
      Decision.Reason = !Request.bTargetExclusionClear
        ? ECrowdDemoFrontPhaseReservationReason::TargetExclusion
        : ECrowdDemoFrontPhaseReservationReason::InvalidRoute;
    }
  }
  Decisions.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  const auto Fold = [](const uint32 Hash, const uint32 Value)
  {
    return (Hash ^ Value) * 16777619u;
  };
  for (const int32 AgentId : Pipeline->GetPreparedFrontAdmissionResult().RequeuedAgentIds)
  {
    Result.DecisionHash = Fold(Fold(Result.DecisionHash, 4u), static_cast<uint32>(AgentId));
  }
  Pipeline->RecordFrontPhaseReservationSchedule(Requests, Decisions, Result.DecisionHash);
  Pipeline->LogStageOnce(TEXT("09_front_phase_reservation_schedule"), Decisions.Num());
}

UCrowdDemoRoundFrontPhaseReservationApplyProcessor::UCrowdDemoRoundFrontPhaseReservationApplyProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundFrontPhaseReservationApplyProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundFrontPhaseReservationApplyProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario
    != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  TMap<int32, const FCrowdDemoFrontPhaseReservationDecisionRecord*> DecisionByAgentId;
  for (const FCrowdDemoFrontPhaseReservationDecisionRecord& Decision :
    Pipeline->GetPreparedFrontPhaseReservationDecisions())
  {
    DecisionByAgentId.Add(Decision.AgentId, &Decision);
  }
  TMap<int32, const FCrowdDemoFrontApproachRoute*> RouteByAgentId;
  for (const FCrowdDemoFrontApproachRoute& Route : Pipeline->GetPreparedPositionApproachRoutes())
    RouteByAgentId.Add(Route.AgentId, &Route);
  int32 TransitionCount = 0;
  TArray<int32> HeldSteps;
  const int32 Revision = Pipeline->GetCurrentFixedStepIndex();
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Assignments = ChunkContext.GetMutableFragmentView<FCrowdDemoPositionAssignmentFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoPositionAssignmentFragment& Assignment = Assignments[It];
      const FCrowdDemoFrontPhaseReservationDecisionRecord* const* Found =
        DecisionByAgentId.Find(Identities[It].Id);
      if (!Found)
      {
        Assignment.RequestedApproachPhase = ECrowdDemoFrontApproachPhase::None;
        Assignment.PhaseReservationDecision = ECrowdDemoFrontPhaseReservationDecision::None;
        Assignment.PhaseReservationInvalidReason = ECrowdDemoFrontPhaseReservationReason::None;
      }
      else
      {
        FCrowdDemoFrontPhaseReservationState State;
        State.CurrentPhase = Assignment.FrontApproachPhase;
        State.RequestedPhase = Assignment.RequestedApproachPhase;
        State.Decision = Assignment.PhaseReservationDecision;
        State.InvalidReason = Assignment.PhaseReservationInvalidReason;
        State.AppliedRevision = Assignment.PhaseReservationRevision;
        State.HeldSteps = Assignment.PhaseReservationHeldSteps;
        const bool bTransition = FCrowdDemoPursuitPositioningKernel::
          ApplyFrontPhaseReservationDecision(**Found, Revision, State);
        Assignment.FrontApproachPhase = State.CurrentPhase;
        Assignment.RequestedApproachPhase = State.RequestedPhase;
        Assignment.PhaseReservationDecision = State.Decision;
        Assignment.PhaseReservationInvalidReason = State.InvalidReason;
        Assignment.PhaseReservationRevision = State.AppliedRevision;
        Assignment.PhaseReservationHeldSteps = State.HeldSteps;
        if (State.Decision == ECrowdDemoFrontPhaseReservationDecision::Held)
          HeldSteps.Add(State.HeldSteps);
        if (State.Decision == ECrowdDemoFrontPhaseReservationDecision::Invalid
          && State.InvalidReason == ECrowdDemoFrontPhaseReservationReason::AdmissionRequeue)
        {
          Assignment.State = ECrowdDemoPursuitPositionState::FrontAssignedWaiting;
          Assignment.FrontApproachPhase = ECrowdDemoFrontApproachPhase::None;
          Assignment.FrontCommitGrantedStep = INDEX_NONE;
          Assignment.StableArrivalStepCount = 0;
          Assignment.FrontApproachBestErrorBucket = MAX_int32;
          Assignment.FrontApproachNoProgressSteps = 0;
        }
        if (bTransition)
        {
          ++TransitionCount;
          Assignment.FrontApproachBestErrorBucket = MAX_int32;
          Assignment.FrontApproachLastProgressStep = Revision;
          Assignment.FrontApproachNoProgressSteps = 0;
          Assignment.FrontApproachPreviousRadialErrorCm = -1.0f;
          Assignment.FrontApproachPreviousErrorBucket = MAX_int32;
          Assignment.StableArrivalStepCount = 0;
          if (Assignment.FrontCommitGrantedStep == INDEX_NONE)
            Assignment.FrontCommitGrantedStep = Revision;
          Assignment.State = Assignment.FrontApproachPhase
            == ECrowdDemoFrontApproachPhase::RadialCommit
            ? ECrowdDemoPursuitPositionState::SlotCommit
            : ECrowdDemoPursuitPositionState::FrontCommitGranted;
        }
      }
      const FCrowdDemoFrontApproachRoute* const* Route =
        RouteByAgentId.Find(Identities[It].Id);
      if (!Route || Assignment.FrontApproachPhase == ECrowdDemoFrontApproachPhase::None)
        continue;
      Assignment.FrontApproachRouteRevision = (*Route)->RouteRevision;
      Assignment.bFrontApproachRadialErrorImproved =
        Assignment.FrontApproachPreviousRadialErrorCm >= 0.0f
        && (*Route)->RadialErrorCm < Assignment.FrontApproachPreviousRadialErrorCm - 0.05f;
      Assignment.bFrontApproachQuantizedProgressStall =
        Assignment.bFrontApproachRadialErrorImproved
        && (*Route)->RouteErrorBucket == Assignment.FrontApproachPreviousErrorBucket;
      Assignment.FrontApproachPreviousRadialErrorCm = (*Route)->RadialErrorCm;
      Assignment.FrontApproachPreviousErrorBucket = (*Route)->RouteErrorBucket;
      if ((*Route)->RouteErrorBucket < Assignment.FrontApproachBestErrorBucket)
      {
        Assignment.FrontApproachBestErrorBucket = (*Route)->RouteErrorBucket;
        Assignment.FrontApproachLastProgressStep = Revision;
        Assignment.FrontApproachNoProgressSteps = 0;
      }
      else
      {
        ++Assignment.FrontApproachNoProgressSteps;
      }
    }
  });
  Pipeline->RecordFrontPhaseReservationTransitions(TransitionCount, HeldSteps);
  Pipeline->LogStageOnce(TEXT("10_front_phase_reservation_apply"), TransitionCount);
}

UCrowdDemoRoundPursuitPositionGuidanceProcessor::UCrowdDemoRoundPursuitPositionGuidanceProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundPursuitPositionGuidanceProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPortalAdmissionFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPursuitGuidanceFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoOrcaVelocityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundPbdCorrectionFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundPursuitPositionGuidanceProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  int32 Stable = 0, Reserve = 0;
  TArray<float> Errors;
  TArray<float> UnsettledSpeeds;
  TArray<float> UnsettledGuidanceSpeeds;
  TArray<float> UnsettledOrcaSpeeds;
  TArray<float> UnsettledObstacleSpeeds;
  TArray<float> UnsettledOrcaConstraints;
  FCrowdDemoPositioningRuntimeDiagnostic Diagnostic;
  bool bFrontIngressComplete = true;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Assignments = ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      bFrontIngressComplete &= Assignments[It].Role != ECrowdDemoPositionRole::Front
        || Assignments[It].State == ECrowdDemoPursuitPositionState::StableOccupied;
  });
  TMap<int32, const FCrowdDemoFrontApproachRoute*> RouteByPositionId;
  for (const FCrowdDemoFrontApproachRoute& Route : Pipeline->GetPreparedPositionApproachRoutes())
    RouteByPositionId.Add(Route.PositionId, &Route);
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Admissions = ChunkContext.GetFragmentView<FCrowdDemoPortalAdmissionFragment>();
    const auto Assignments = ChunkContext.GetMutableFragmentView<FCrowdDemoPositionAssignmentFragment>();
    const auto Guidance = ChunkContext.GetMutableFragmentView<FCrowdDemoPursuitGuidanceFragment>();
    const auto Orca = ChunkContext.GetFragmentView<FCrowdDemoOrcaVelocityFragment>();
    const auto Pbd = ChunkContext.GetFragmentView<FCrowdDemoRoundPbdCorrectionFragment>();
    const auto Obstacles = ChunkContext.GetFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      Guidance[It] = FCrowdDemoPursuitGuidanceFragment();
      auto& Assignment = Assignments[It];
      if (Assignment.PositionId == INDEX_NONE) continue;
      const FVector Error = Assignment.DesiredLocation - States[It].Location;
      const float Distance = Error.Size2D();
      Errors.Add(Distance);
      const bool bPortalOwns = Admissions[It].State == ECrowdDemoPortalAdmissionState::Approach
        || Admissions[It].State == ECrowdDemoPortalAdmissionState::Waiting
        || Admissions[It].State == ECrowdDemoPortalAdmissionState::Reserved
        || Admissions[It].State == ECrowdDemoPortalAdmissionState::Inside;
      const bool bHoldingBefore = Assignment.State == ECrowdDemoPursuitPositionState::StableOccupied
        || Assignment.State == ECrowdDemoPursuitPositionState::ReserveHold;
      if (!bHoldingBefore)
      {
        Diagnostic.PortalOwnedCount += bPortalOwns ? 1 : 0;
        Diagnostic.OutsideComposeRangeCount += !bPortalOwns && Distance > 1200.0f ? 1 : 0;
      }
      const bool bWithinComposeRange =
        FCrowdDemoPursuitPositioningKernel::ShouldComposePositionGuidance(
          Assignment.State, Assignment.FrontApproachPhase, bPortalOwns, Distance,
          Pipeline->GetPursuitPositioningSettings().FrontAdmissionHoldRangeCm);
      if (Assignment.bFrontApproachComposeStateInitialized
        && Assignment.bFrontApproachWasWithinComposeRange != bWithinComposeRange)
      {
        ++Assignment.FrontApproachComposeBoundarySwitchCount;
      }
      Assignment.bFrontApproachComposeStateInitialized = true;
      Assignment.bFrontApproachWasWithinComposeRange = bWithinComposeRange;
      if (Assignment.State == ECrowdDemoPursuitPositionState::FrontAssignedWaiting
        && Assignment.FrontApproachPhase == ECrowdDemoFrontApproachPhase::None)
        continue;
      if (!bWithinComposeRange) continue;
      Guidance[It].bPositioningActive = true;
      if (Assignment.State == ECrowdDemoPursuitPositionState::ReserveCommit
        && !bFrontIngressComplete)
      {
        Guidance[It].DesiredVelocity = FVector::ZeroVector;
        continue;
      }
      if (Assignment.State == ECrowdDemoPursuitPositionState::FrontCommitGranted)
      {
        const FCrowdDemoFrontApproachRoute* const* Route = RouteByPositionId.Find(Assignment.PositionId);
        const FVector2f Desired = Route
          ? FCrowdDemoPursuitPositioningKernel::BuildFrontPhaseDesiredVelocity(
            **Route, Assignment.FrontApproachPhase,
            FVector2f(States[It].Location.X, States[It].Location.Y),
            Pipeline->GetRules().MaxSpeedCmPerSecond)
          : FVector2f::ZeroVector;
        Guidance[It].DesiredVelocity = FVector(Desired.X, Desired.Y, 0.0f);
        continue;
      }
      if (Distance <= 30.0f && States[It].Velocity.Size2D() <= 30.0f)
        ++Assignment.StableArrivalStepCount;
      else
        Assignment.StableArrivalStepCount = 0;
      if ((Assignment.State == ECrowdDemoPursuitPositionState::SlotCommit
          || Assignment.State == ECrowdDemoPursuitPositionState::ReserveCommit)
        && Assignment.StableArrivalStepCount >= 15)
      {
        Assignment.State = Assignment.Role == ECrowdDemoPositionRole::Front
          ? ECrowdDemoPursuitPositionState::StableOccupied
          : ECrowdDemoPursuitPositionState::ReserveHold;
      }
      const bool bHolding = Assignment.State == ECrowdDemoPursuitPositionState::StableOccupied
        || Assignment.State == ECrowdDemoPursuitPositionState::ReserveHold;
      const float Gain = bHolding ? 0.5f : 2.0f;
      if (Assignment.State == ECrowdDemoPursuitPositionState::SlotCommit)
      {
        const FCrowdDemoFrontApproachRoute* const* Route = RouteByPositionId.Find(Assignment.PositionId);
        const FVector2f Desired = Route
          ? FCrowdDemoPursuitPositioningKernel::BuildFrontPhaseDesiredVelocity(
            **Route, Assignment.FrontApproachPhase,
            FVector2f(States[It].Location.X, States[It].Location.Y),
            Pipeline->GetRules().MaxSpeedCmPerSecond)
          : FVector2f::ZeroVector;
        Guidance[It].DesiredVelocity = FVector(Desired.X, Desired.Y, 0.0f);
      }
      else
      {
        Guidance[It].DesiredVelocity = Error.GetSafeNormal2D()
          * FMath::Min(Pipeline->GetRules().MaxSpeedCmPerSecond, Distance * Gain);
      }
      if (bHolding && Distance <= 30.0f) Guidance[It].DesiredVelocity = FVector::ZeroVector;
      Stable += Assignment.State == ECrowdDemoPursuitPositionState::StableOccupied ? 1 : 0;
      Reserve += Assignment.State == ECrowdDemoPursuitPositionState::ReserveHold ? 1 : 0;
      if (!bHolding)
      {
        Diagnostic.SlotCommitCount += Assignment.State == ECrowdDemoPursuitPositionState::SlotCommit ? 1 : 0;
        Diagnostic.ReserveCommitCount += Assignment.State == ECrowdDemoPursuitPositionState::ReserveCommit ? 1 : 0;
        ++Diagnostic.GuidanceActiveCount;
        Diagnostic.ArrivalSpeedRejectedCount += Distance <= 30.0f
          && States[It].Velocity.Size2D() > 30.0f ? 1 : 0;
        Diagnostic.ErrorLe30Count += Distance <= 30.0f ? 1 : 0;
        Diagnostic.Error31To100Count += Distance > 30.0f && Distance <= 100.0f ? 1 : 0;
        Diagnostic.Error101To300Count += Distance > 100.0f && Distance <= 300.0f ? 1 : 0;
        Diagnostic.ErrorOver300Count += Distance > 300.0f ? 1 : 0;
        Diagnostic.PreviousOrcaFallbackCount += Orca[It].FallbackStage > 0 ? 1 : 0;
        Diagnostic.PreviousOrcaInfeasibleCount += Orca[It].bInfeasible ? 1 : 0;
        Diagnostic.PreviousPbdCorrectedCount += Pbd[It].bCorrected ? 1 : 0;
        UnsettledSpeeds.Add(States[It].Velocity.Size2D());
        UnsettledGuidanceSpeeds.Add(Guidance[It].DesiredVelocity.Size2D());
        UnsettledOrcaSpeeds.Add(Orca[It].Velocity.Size2D());
        UnsettledObstacleSpeeds.Add(Obstacles[It].ConstrainedVelocity.Size2D());
        UnsettledOrcaConstraints.Add(static_cast<float>(Orca[It].ConstraintCount));
        Diagnostic.OrcaAdjustedCount += Orca[It].bAdjusted ? 1 : 0;
        Diagnostic.OrcaZeroCount += Orca[It].Velocity.Size2D() < 1.0f ? 1 : 0;
        Diagnostic.ObstacleHitCount += Obstacles[It].bHitObstacle ? 1 : 0;
      }
    }
  });
  Errors.Sort();
  const float P95 = Errors.IsEmpty() ? -1.0f : Errors[FMath::Clamp(
    FMath::CeilToInt(Errors.Num() * 0.95f) - 1, 0, Errors.Num() - 1)];
  Pipeline->RecordPositioningMetrics(Pipeline->GetPositioningSummary(), Stable, Reserve, 0, P95);
  UnsettledSpeeds.Sort();
  Diagnostic.SpeedP95 = UnsettledSpeeds.IsEmpty() ? 0.0f : UnsettledSpeeds[FMath::Clamp(
    FMath::CeilToInt(UnsettledSpeeds.Num() * 0.95f) - 1, 0, UnsettledSpeeds.Num() - 1)];
  const auto P95Of = [](TArray<float>& Values)
  {
    Values.Sort();
    return Values.IsEmpty() ? 0.0f : Values[FMath::Clamp(
      FMath::CeilToInt(Values.Num() * 0.95f) - 1, 0, Values.Num() - 1)];
  };
  Diagnostic.GuidanceSpeedP95 = P95Of(UnsettledGuidanceSpeeds);
  Diagnostic.OrcaSpeedP95 = P95Of(UnsettledOrcaSpeeds);
  Diagnostic.ObstacleSpeedP95 = P95Of(UnsettledObstacleSpeeds);
  Diagnostic.OrcaConstraintP95 = P95Of(UnsettledOrcaConstraints);
  Pipeline->RecordPositioningDiagnostic(Diagnostic);
  Pipeline->LogStageOnce(TEXT("09_pursuit_position_guidance"), Stable + Reserve);
}

UCrowdDemoRoundMovementIntentComposeProcessor::UCrowdDemoRoundMovementIntentComposeProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundMovementIntentComposeProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPursuitGuidanceFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoTargetApproachFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundMovementIntentComposeProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Guidance = ChunkContext.GetFragmentView<FCrowdDemoPursuitGuidanceFragment>();
    const auto Intents = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (Guidance[It].bPositioningActive)
      {
        Intents[It].DesiredVelocity = Guidance[It].DesiredVelocity;
        Intents[It].DesiredLocation += Guidance[It].DesiredVelocity * Pipeline->GetCurrentFixedStepSeconds();
      }
    }
  });
  Pipeline->LogStageOnce(TEXT("10_movement_intent_compose"), 0);
}

UCrowdDemoRoundElasticCrowdShadowProcessor::UCrowdDemoRoundElasticCrowdShadowProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundElasticCrowdShadowProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundElasticCrowdShadowProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsElasticCrowdShadowEnabled()) return;

  const double SolverStart = FPlatformTime::Seconds();
  TMap<int32, const FCrowdDemoHoldingAssignment*> HoldingByAgent;
  for (const FCrowdDemoHoldingAssignment& Assignment
    : Pipeline->GetPreparedHoldingAssignments())
    HoldingByAgent.Add(Assignment.AgentId, &Assignment);

  FCrowdDemoElasticShadowStepInput StepInput;
  StepInput.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  StepInput.FixedStepSeconds = Pipeline->GetCurrentFixedStepSeconds();
  StepInput.ElasticSettings = Pipeline->GetRules().ElasticCrowdSettings;
  StepInput.ElasticSettings.FixedStepSeconds = StepInput.FixedStepSeconds;
  StepInput.OrcaSettings = Pipeline->GetRules().OrcaSettings;
  StepInput.PbdSettings.IterationCount = Pipeline->GetRules().HardSeparationPbdIterations;
  StepInput.PbdSettings.MaxPairCorrectionPerIterationCm =
    Pipeline->GetRules().HardSeparationPbdMaxCorrectionCm;
  StepInput.Environment.FlowConfig = Pipeline->GetRules().FlowFieldConfig;
  StepInput.Environment.TargetLocation = Pipeline->GetPursuitTargetFact().Location;
  StepInput.Environment.TargetExclusionRadiusCm =
    Pipeline->GetPursuitTargetFact().RadiusCm
      + Pipeline->GetPursuitPositioningSettings().SafetyGapCm;
  StepInput.Environment.bValidateFlowAndObstacles = true;
  StepInput.Environment.bConstrainToFlowBounds = true;
  StepInput.Environment.bValidateTargetExclusion = true;

  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const auto Flows = ChunkContext.GetFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    const auto Steering = ChunkContext.GetFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (!States[It].bInitialized) continue;
      FCrowdDemoElasticShadowAgentInput& Item = StepInput.Agents.AddDefaulted_GetRef();
      Item.Agent.AgentId = Identities[It].Id;
      Item.Agent.Position = FVector2f(States[It].Location.X, States[It].Location.Y);
      Item.Agent.Velocity = FVector2f(States[It].Velocity.X, States[It].Velocity.Y);
      Item.Agent.BasePreferredVelocity = FVector2f(
        Intents[It].DesiredVelocity.X, Intents[It].DesiredVelocity.Y);
      Item.Agent.PhysicalRadiusCm = Formations[It].RadiusCm;
      Item.Agent.MaxSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
      Item.Agent.ContextScaleQ15 = 32767;
      Item.Agent.LocalPriority = 1;
      Item.SteeringState = Steering[It].SteeringState;
      Item.Agent.bTransitSource = Item.SteeringState
        == ECrowdDemoPursuitSteeringState::Commit;
      Item.Agent.TransitSourceRadiusCm = Formations[It].RadiusCm;
      Item.Agent.TransitDirection = Item.Agent.BasePreferredVelocity.GetSafeNormal();
      if (Item.Agent.TransitDirection.IsNearlyZero())
        Item.Agent.TransitDirection = Item.Agent.Velocity.GetSafeNormal();
      Item.FlowDirection = FVector2f(Flows[It].FlowDirection.X, Flows[It].FlowDirection.Y);
      Item.FlowStatus = Flows[It].Status;
      if (const FCrowdDemoHoldingAssignment* const* Holding =
        HoldingByAgent.Find(Item.Agent.AgentId))
      {
        Item.HoldingLocation = (*Holding)->HoldingLocation;
        Item.AssignedPosition = (*Holding)->AssignedPosition;
        Item.bHasAssignment = (*Holding)->bCompatibilityValid;
      }
    }
  });
  StepInput.Agents.Sort([](const auto& A, const auto& B)
    { return A.Agent.AgentId < B.Agent.AgentId; });
  if (StepInput.Agents.IsEmpty()) return;

  FCrowdDemoElasticShadowTwinResult Twin;
  if (!FCrowdDemoElasticShadowKernel::RunTwinStep(StepInput, Twin))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoElasticShadow twin_step_invalid=1 step=%d"),
      StepInput.FixedStepIndex);
    return;
  }
  TMap<int32, int32>& ZeroProgress = Pipeline->GetElasticZeroProgressSteps();
  TArray<int32> ZeroProgressAgentIds;
  int32 ZeroProgressStepMax = 0;
  const int32 FinalStage = static_cast<int32>(ECrowdDemoElasticShadowStage::Reproject);
  for (const FCrowdDemoElasticShadowAgentInput& Agent : StepInput.Agents)
  {
    if (!Agent.Agent.bTransitSource) continue;
    const FCrowdDemoElasticShadowAgentStage* Final =
      Twin.Elastic.Stages[FinalStage].FindByPredicate([&](const auto& Value)
        { return Value.AgentId == Agent.Agent.AgentId; });
    FVector2f Direction = Agent.Agent.TransitDirection.GetSafeNormal();
    if (Direction.IsNearlyZero()) Direction = Agent.Agent.BasePreferredVelocity.GetSafeNormal();
    const float Forward = Final ? FVector2f::DotProduct(Final->Velocity, Direction) : 0.0f;
    int32& Steps = ZeroProgress.FindOrAdd(Agent.Agent.AgentId);
    Steps = Forward < 1.0f ? Steps + 1 : 0;
    ZeroProgressStepMax = FMath::Max(ZeroProgressStepMax, Steps);
    if (Steps >= 15) ZeroProgressAgentIds.Add(Agent.Agent.AgentId);
  }
  ZeroProgressAgentIds.Sort();
  TArray<int32> StaleIds;
  for (const auto& Pair : ZeroProgress)
    if (!StepInput.Agents.ContainsByPredicate([&](const auto& Agent)
      { return Agent.Agent.AgentId == Pair.Key && Agent.Agent.bTransitSource; }))
      StaleIds.Add(Pair.Key);
  for (const int32 AgentId : StaleIds) ZeroProgress.Remove(AgentId);

  FCrowdDemoElasticShadowFailureFixture Fixture;
  if (FCrowdDemoElasticShadowKernel::BuildFirstFailureFixture(
    StepInput, Twin, ZeroProgressAgentIds, ZeroProgressStepMax, Fixture))
    Pipeline->RecordElasticCrowdFailureFixture(Fixture);

  FCrowdDemoElasticShadowParallelState& Parallel = Pipeline->GetElasticParallelState();
  if (!Parallel.bActive)
    FCrowdDemoElasticShadowKernel::InitializeParallelRollout(
      StepInput, Pipeline->GetSharedFlowField(),
      Pipeline->GetPursuitPositioningSettings(), Parallel);
  if (Parallel.bActive && !Parallel.bCompleted)
  {
    FCrowdDemoElasticShadowTwinResult ParallelStep;
    if (FCrowdDemoElasticShadowKernel::AdvanceParallelRollout(
      StepInput, Parallel, ParallelStep))
      Pipeline->RecordElasticParallelRollout(Parallel, ParallelStep);
  }

  const float SolverMs = static_cast<float>(
    (FPlatformTime::Seconds() - SolverStart) * 1000.0);
  Pipeline->RecordElasticCrowdShadow(Twin, ZeroProgressStepMax, SolverMs);

}

UCrowdDemoRoundCrowdTrafficFieldBuildProcessor::UCrowdDemoRoundCrowdTrafficFieldBuildProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundCrowdTrafficFieldBuildProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPortalAdmissionFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPassingBandFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundCrowdTrafficFieldBuildProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive() || !IsTrafficScenario(Pipeline->GetRules().Scenario))
  {
    return;
  }
  TArray<FCrowdDemoTrafficAgent>& Agents = Pipeline->GetPreparedTrafficAgents();
  Agents.Reset();
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Flows = ChunkContext.GetFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    const auto Admissions = ChunkContext.GetFragmentView<FCrowdDemoPortalAdmissionFragment>();
    const auto Bands = ChunkContext.GetFragmentView<FCrowdDemoPassingBandFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoTrafficAgent& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.CohortId = Admissions[It].CohortId;
      Agent.Position = FVector2f(States[It].Location.X, States[It].Location.Y);
      Agent.Velocity = FVector2f(States[It].Velocity.X, States[It].Velocity.Y);
      Agent.FlowDirection = FVector2f(Flows[It].FlowDirection.X, Flows[It].FlowDirection.Y);
      Agent.RadiusCm = Formations[It].RadiusCm;
      Agent.PreviousPortalId = Admissions[It].PortalId;
      Agent.PreviousDirectionEpoch = Admissions[It].DirectionEpoch;
      Agent.PreviousBandId = Bands[It].BandId;
      Agent.WaitSteps = Admissions[It].WaitSteps;
      Agent.PreviousDirectionKey = Admissions[It].DirectionKey;
      Agent.TokenGrantedStep = Admissions[It].TokenGrantedStep;
      Agent.EnteredPortalStep = Admissions[It].EnteredPortalStep;
      Agent.LastTransitionStep = Admissions[It].LastTransitionStep;
      Agent.AdmissionState = Admissions[It].State;
    }
  });
  uint32 FieldHash = 0;
  FCrowdDemoTrafficSchedulingKernel::BuildTrafficCells(
    Agents, Pipeline->GetRules().FlowFieldConfig, Pipeline->GetRules().TrafficSettings,
    Pipeline->GetPreparedTrafficCells(), FieldHash);
  if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
  {
    const TArray<FCrowdDemoTrafficCell>& Cells = Pipeline->GetPreparedTrafficCells();
    FCrowdDemoSf3DeterminismHashBuilder Hash(Pipeline->GetCurrentFixedStepIndex(), Cells.Num());
    TArray<int32> Keys;
    for (const FCrowdDemoTrafficCell& Cell : Cells)
    {
      Hash.AddInt(Cell.StableCellKey);
      Hash.AddInt(Cell.AgentCount);
      Hash.AddInt(Cell.ReservedAgentCount);
      Hash.AddVelocity(FVector(Cell.MeanVelocity.X, Cell.MeanVelocity.Y, 0.0f));
      Hash.AddDirection(Cell.DominantDirection);
      if (Keys.Num() < 8) Keys.Add(Cell.StableCellKey);
    }
    Pipeline->RecordSf3StageHash(
      ECrowdDemoSf3DeterminismStage::TrafficField, Hash.Finalize(), Cells.Num(), Keys);
  }
  FCrowdDemoTrafficStepSummary& Summary = Pipeline->GetPreparedTrafficSummary();
  Summary = FCrowdDemoTrafficStepSummary();
  Summary.TrafficFieldHash = FieldHash;
  TArray<FCrowdDemoTrafficPortalRuntime>& Portals = Pipeline->GetPreparedTrafficPortals();
  if (Portals.IsEmpty())
  {
    TArray<FCrowdDemoTrafficPortal> Extracted;
    int32 PreferredAxis = INDEX_NONE;
    if (!Pipeline->GetRules().TrafficCohorts.IsEmpty())
    {
      const FCrowdDemoTrafficCohortRule& Cohort = Pipeline->GetRules().TrafficCohorts[0];
      const FVector Delta = FVector(Cohort.FlowFieldConfig.GoalLocation) - FVector(Cohort.SpawnOrigin);
      PreferredAxis = FMath::Abs(Delta.X) > FMath::Abs(Delta.Y) ? 0 : 1;
    }
    FCrowdDemoTrafficSchedulingKernel::ExtractPortals(
      Pipeline->GetSharedFlowField(), Pipeline->GetRules().TrafficSettings, Extracted,
      &Pipeline->GetPortalExtractionSummary(), PreferredAxis);
    for (const FCrowdDemoTrafficPortal& Portal : Extracted)
    {
      FCrowdDemoTrafficPortalRuntime& Runtime = Portals.AddDefaulted_GetRef();
      Runtime.Portal = Portal;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("CrowdDemoSf3PortalDiagnostic")))
    {
      const FCrowdDemoPortalExtractionSummary& Extraction = Pipeline->GetPortalExtractionSummary();
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf3PortalGeometry raw=%d unique=%d local_minimum=%d portals=%d duplicate_rejected=%d plateau_rejected=%d geometry_hash=%u source=MassPipeline"),
        Extraction.RawCrossSectionCandidateCount, Extraction.UniqueCrossSectionCandidateCount,
        Extraction.LocalMinimumCandidateCount, Extraction.ExtractedPortalCount,
        Extraction.DuplicateRejectedCount, Extraction.PlateauRejectedCount, Extraction.GeometryHash);
      for (const FCrowdDemoTrafficPortal& Portal : Extracted)
      {
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoSf3PortalGeometry portal_id=%d axis=%d center_x=%.1f center_y=%.1f span_min=%d span_max=%d width=%d capacity=%d stable_cell_key=%d upstream_width=%d downstream_width=%d merged_candidates=%d source=MassPipeline"),
          Portal.PortalId, Portal.Axis, Portal.Center.X, Portal.Center.Y,
          Portal.SpanMin, Portal.SpanMax, Portal.WidthCells, Portal.Capacity,
          Portal.StableCellKey, Portal.UpstreamWidthCells, Portal.DownstreamWidthCells,
          Portal.MergedCandidateCount);
      }
    }
  }
  Pipeline->LogStageOnce(TEXT("03_crowd_traffic_field_build"), Agents.Num());
}

UCrowdDemoRoundPortalScheduleProcessor::UCrowdDemoRoundPortalScheduleProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundPortalScheduleProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPortalAdmissionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPassingBandFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundPortalScheduleProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive() || !IsTrafficScenario(Pipeline->GetRules().Scenario))
  {
    return;
  }
  auto& Portals = Pipeline->GetPreparedTrafficPortals();
  auto& Candidates = Pipeline->GetPreparedPortalCandidates();
  FCrowdDemoPortalCandidateBuildSummary CandidateSummary;
  FCrowdDemoTrafficSchedulingKernel::BuildCandidates(
    Pipeline->GetPreparedTrafficAgents(), Portals, Pipeline->GetRules().TrafficSettings,
    Candidates, &CandidateSummary);
  FCrowdDemoTrafficStepSummary StepSummary = Pipeline->GetPreparedTrafficSummary();
  TArray<FCrowdDemoPortalDecision>& Decisions = Pipeline->GetPreparedPortalDecisions();
  FCrowdDemoTrafficStepSummary ScheduleSummary;
  FCrowdDemoTrafficSchedulingKernel::StepPortalSchedule(
    Portals, Pipeline->GetPreparedTrafficAgents(), Candidates, Pipeline->GetRules().TrafficSettings,
    FMath::RoundToInt((Pipeline->GetCurrentStepStartServerTimeSeconds() - Pipeline->GetActivePlan().StartServerTimeSeconds)
      / Pipeline->GetCurrentFixedStepSeconds()), Decisions, ScheduleSummary);
  ScheduleSummary.PortalBindCount = CandidateSummary.BindCount;
  ScheduleSummary.PortalRebindCount = CandidateSummary.RebindCount;
  ScheduleSummary.PortalReleaseCount = CandidateSummary.ReleaseCount;
  ScheduleSummary.InvalidSideCandidateCount = CandidateSummary.InvalidSideCandidateCount;
  ScheduleSummary.WrongSpanCandidateCount = CandidateSummary.WrongSpanCandidateCount;
  FCrowdDemoTrafficSchedulingKernel::BuildHoldingTargets(
    Pipeline->GetPreparedTrafficAgents(), Portals, Pipeline->GetSharedFlowField(),
    Pipeline->GetRules().TrafficSettings, Decisions, ScheduleSummary);
  ScheduleSummary.TrafficFieldHash = StepSummary.TrafficFieldHash;
  Pipeline->GetPreparedTrafficSummary() = ScheduleSummary;
  TMap<int32, const FCrowdDemoPortalDecision*> ById;
  for (const FCrowdDemoPortalDecision& Decision : Decisions) ById.Add(Decision.AgentId, &Decision);
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Admissions = ChunkContext.GetMutableFragmentView<FCrowdDemoPortalAdmissionFragment>();
    const auto Bands = ChunkContext.GetMutableFragmentView<FCrowdDemoPassingBandFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (const FCrowdDemoPortalDecision* const* Found = ById.Find(Identities[It].Id))
      {
        const FCrowdDemoPortalDecision& Decision = **Found;
        Admissions[It].PortalId = Decision.PortalId;
        Admissions[It].DirectionKey = Decision.DirectionKey;
        Admissions[It].DirectionEpoch = Decision.DirectionEpoch;
        Admissions[It].State = Decision.State;
        Admissions[It].WaitSteps = Decision.bGranted
          ? 0
          : (Decision.State == ECrowdDemoPortalAdmissionState::Waiting
            ? Admissions[It].WaitSteps + 1
            : Admissions[It].WaitSteps);
        Admissions[It].WaitEpoch = Admissions[It].WaitSteps / FMath::Max(1, Pipeline->GetRules().TrafficSettings.WaitEpochSteps);
        Admissions[It].TokenGrantedStep = Decision.TokenGrantedStep;
        Admissions[It].EnteredPortalStep = Decision.EnteredPortalStep;
        Admissions[It].LastTransitionStep = Decision.LastTransitionStep;
        Bands[It].PortalId = Decision.PortalId;
        Bands[It].DirectionEpoch = Decision.DirectionEpoch;
        Bands[It].BandId = Decision.BandId;
        Bands[It].bValid = Decision.BandId != INDEX_NONE;
      }
      else
      {
        Admissions[It].State = ECrowdDemoPortalAdmissionState::None;
        Admissions[It].PortalId = INDEX_NONE;
        Admissions[It].WaitSteps = 0;
        Admissions[It].TokenGrantedStep = INDEX_NONE;
        Admissions[It].EnteredPortalStep = INDEX_NONE;
        Admissions[It].LastTransitionStep = FMath::Max(
          Admissions[It].LastTransitionStep,
          Pipeline->GetCurrentFixedStepIndex());
        Bands[It] = FCrowdDemoPassingBandFragment();
      }
    }
  });
  if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
  {
    TArray<FCrowdDemoPortalDecision> SortedDecisions = Decisions;
    SortedDecisions.Sort([](const FCrowdDemoPortalDecision& A, const FCrowdDemoPortalDecision& B)
    {
      return A.AgentId < B.AgentId;
    });
    TArray<FCrowdDemoTrafficPortalRuntime> SortedPortals = Portals;
    SortedPortals.Sort([](const FCrowdDemoTrafficPortalRuntime& A, const FCrowdDemoTrafficPortalRuntime& B)
    {
      return A.Portal.PortalId < B.Portal.PortalId;
    });
    FCrowdDemoSf3DeterminismHashBuilder Hash(
      Pipeline->GetCurrentFixedStepIndex(), SortedPortals.Num() + SortedDecisions.Num());
    TArray<int32> Keys;
    for (const FCrowdDemoTrafficPortalRuntime& Portal : SortedPortals)
    {
      Hash.AddInt(Portal.Portal.PortalId);
      Hash.AddInt(Portal.ActiveDirectionKey);
      Hash.AddInt(Portal.DirectionEpoch);
      Hash.AddInt(Portal.GreenSteps);
      Hash.AddInt(Portal.ClearanceStepsRemaining);
      Hash.AddInt(Portal.OccupiedCount);
      Hash.AddInt(Portal.ReservedCount);
      if (Keys.Num() < 8) Keys.Add(Portal.Portal.PortalId);
    }
    for (const FCrowdDemoPortalDecision& Decision : SortedDecisions)
    {
      Hash.AddInt(Decision.AgentId);
      Hash.AddInt(Decision.PortalId);
      Hash.AddInt(Decision.DirectionKey);
      Hash.AddInt(Decision.DirectionEpoch);
      Hash.AddInt(Decision.BandId);
      Hash.AddInt(static_cast<int32>(Decision.State));
      Hash.AddBool(Decision.bGranted);
    }
    Pipeline->RecordSf3StageHash(
      ECrowdDemoSf3DeterminismStage::PortalSchedule, Hash.Finalize(),
      SortedPortals.Num() + SortedDecisions.Num(), Keys);
  }
  Pipeline->LogStageOnce(TEXT("04_portal_schedule"), Decisions.Num());
}

UCrowdDemoRoundPassingBandGuidanceProcessor::UCrowdDemoRoundPassingBandGuidanceProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundPassingBandGuidanceProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundPassingBandGuidanceProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !IsTrafficScenario(Pipeline->GetRules().Scenario)) return;
  TMap<int32, const FCrowdDemoTrafficAgent*> Agents;
  for (const auto& Agent : Pipeline->GetPreparedTrafficAgents()) Agents.Add(Agent.AgentId, &Agent);
  TMap<int32, const FCrowdDemoTrafficCell*> Cells;
  for (const auto& Cell : Pipeline->GetPreparedTrafficCells()) Cells.Add(Cell.StableCellKey, &Cell);
  TMap<int32, FCrowdDemoPortalDecision*> Decisions;
  for (auto& Decision : Pipeline->GetPreparedPortalDecisions()) Decisions.Add(Decision.AgentId, &Decision);
  TMap<int32, const FCrowdDemoTrafficPortalRuntime*> Portals;
  for (const auto& Portal : Pipeline->GetPreparedTrafficPortals()) Portals.Add(Portal.Portal.PortalId, &Portal);
  TArray<FSf3AgentHashRecord> HashRecords;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Intents = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdDemoTrafficAgent* const* Agent = Agents.Find(Identities[It].Id);
      if (!Agent) continue;
      const int32 Key = Pipeline->GetSharedFlowField().LocationToCellIndex(
        FVector((*Agent)->Position.X, (*Agent)->Position.Y, 0));
      FCrowdDemoPortalDecision* const* Decision = Decisions.Find(Identities[It].Id);
      const FCrowdDemoTrafficPortalRuntime* const* Portal = Decision ? Portals.Find((*Decision)->PortalId) : nullptr;
      const FVector2f Preferred = FCrowdDemoTrafficSchedulingKernel::ApplyDensityAndBandGuidance(
        **Agent, Cells.FindRef(Key), Portal ? *Portal : nullptr, Decision ? *Decision : nullptr,
        Pipeline->GetRules().TrafficSettings, Pipeline->GetRules().MaxSpeedCmPerSecond);
      Intents[It].DesiredVelocity = FVector(Preferred.X, Preferred.Y, 0.0f);
      if (Decision && (*Decision)->BandId != INDEX_NONE)
      {
        Pipeline->GetPreparedTrafficSummary().BandLateralErrors.Add(
          FMath::Abs((*Decision)->BandLateralErrorCm));
      }
      if (Decision && (*Decision)->State == ECrowdDemoPortalAdmissionState::Reserved)
      {
        const float AxialVelocity = FVector2f::DotProduct(Preferred, (*Decision)->PortalDirection);
        if (AxialVelocity > 0.5f) ++Pipeline->GetPreparedTrafficSummary().ReservedPositiveAxialVelocityCount;
        else ++Pipeline->GetPreparedTrafficSummary().ReservedZeroVelocityCount;
      }
      if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
      {
        FSf3AgentHashRecord& Record = HashRecords.AddDefaulted_GetRef();
        Record.AgentId = Identities[It].Id;
        Record.Position = FVector((*Agent)->Position.X, (*Agent)->Position.Y, 0.0f);
        Record.Velocity = Intents[It].DesiredVelocity;
        Record.Direction = FVector((*Agent)->FlowDirection.X, (*Agent)->FlowDirection.Y, 0.0f);
        if (Decision)
        {
          Record.Values[0] = (*Decision)->PortalId;
          Record.Values[1] = (*Decision)->DirectionEpoch;
          Record.Values[2] = (*Decision)->BandId;
          Record.Values[3] = static_cast<int32>((*Decision)->State);
        }
      }
    }
  });
  if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
  {
    TArray<int32> Keys;
    const uint32 Hash = HashSf3AgentRecords(Pipeline->GetCurrentFixedStepIndex(), HashRecords, Keys);
    Pipeline->RecordSf3StageHash(
      ECrowdDemoSf3DeterminismStage::PassingBandGuidance, Hash, HashRecords.Num(), Keys);
  }
  Pipeline->LogStageOnce(TEXT("06_passing_band_guidance"), Agents.Num());
}

UCrowdDemoRoundDeterministicOrcaProcessor::UCrowdDemoRoundDeterministicOrcaProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundDeterministicOrcaProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPortalAdmissionFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPassingBandFragment>(EMassFragmentAccess::ReadOnly);
  // Retained for aggregate SF4 diagnostics only. Steering-first does not turn
  // FrontApproachPhase into route-aware ORCA ownership.
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoOrcaVelocityFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundDeterministicOrcaProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !IsTrafficScenario(Pipeline->GetRules().Scenario)) return;
  auto& Agents = Pipeline->GetPreparedOrcaAgents();
  Agents.Reset();
  TMap<int32, ECrowdDemoFlowLocationStatus> FlowStatusByAgentId;
  TMap<int32, FVector> LocationByAgentId;
  TMap<int32, ECrowdDemoPursuitSteeringState> SteeringStateByAgentId;
  TMap<int32, const FCrowdDemoPortalDecision*> PortalDecisions;
  for (const FCrowdDemoPortalDecision& Decision : Pipeline->GetPreparedPortalDecisions())
    PortalDecisions.Add(Decision.AgentId, &Decision);
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const auto Flows = ChunkContext.GetFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    const auto Admissions = ChunkContext.GetFragmentView<FCrowdDemoPortalAdmissionFragment>();
    const auto Bands = ChunkContext.GetFragmentView<FCrowdDemoPassingBandFragment>();
    const auto Steering = ChunkContext.GetFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      auto& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.Position = FVector2f(States[It].Location.X, States[It].Location.Y);
      Agent.Velocity = FVector2f(States[It].Velocity.X, States[It].Velocity.Y);
      Agent.PreferredVelocity = FVector2f(Intents[It].DesiredVelocity.X, Intents[It].DesiredVelocity.Y);
      Agent.FlowDirection = FVector2f(Flows[It].FlowDirection.X, Flows[It].FlowDirection.Y);
      if (const FCrowdDemoPortalDecision* const* Decision = PortalDecisions.Find(Agent.AgentId))
        Agent.PortalDirection = (*Decision)->PortalDirection;
      else
        Agent.PortalDirection = Agent.FlowDirection;
      Agent.RadiusCm = Formations[It].RadiusCm;
      Agent.MaxSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
      Agent.AdmissionState = Admissions[It].State;
      Agent.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Normal;
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning)
      {
        switch (Steering[It].SteeringState)
        {
        case ECrowdDemoPursuitSteeringState::Commit:
          Agent.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Committed;
          break;
        case ECrowdDemoPursuitSteeringState::StableOccupied:
        case ECrowdDemoPursuitSteeringState::ReserveHold:
          Agent.LocalAvoidancePriority = ECrowdDemoOrcaLocalPriority::Yielding;
          break;
        default:
          break;
        }
      }
      // SF4 steering-first uses only the committed preferred velocity here.
      // Historical FrontApproachPhase ownership is deliberately not translated
      // into route-aware ORCA constraints.
      Agent.BandId = Bands[It].BandId;
      Agent.IntegrationCost = Flows[It].IntegrationCost;
      FlowStatusByAgentId.Add(Agent.AgentId, Flows[It].Status);
      LocationByAgentId.Add(Agent.AgentId, States[It].Location);
      SteeringStateByAgentId.Add(Agent.AgentId, Steering[It].SteeringState);
    }
  });
  FCrowdDemoOrcaSummary Summary;
  const double Start = FPlatformTime::Seconds();
  FCrowdDemoDeterministicOrcaKernel::Solve(Agents, Pipeline->GetRules().OrcaSettings,
    Pipeline->GetCurrentFixedStepSeconds(), Pipeline->GetPreparedOrcaResults(), Summary);
#if WITH_DEV_AUTOMATION_TESTS
  if (Pipeline->IsSf3OrcaReferenceDiagnosticEnabled())
  {
    FCrowdDemoOrcaReferenceDifferentialSummary Differential;
    TMap<int32, const FCrowdDemoOrcaAgent*> AgentById;
    for (const FCrowdDemoOrcaAgent& Agent : Agents) AgentById.Add(Agent.AgentId, &Agent);
    const FCrowdDemoOrcaSettings& Settings = Pipeline->GetRules().OrcaSettings;
    const auto IsExact = [](const FCrowdDemoOrcaContinuousSolveResult& SolveResult)
    {
      return (SolveResult.Status == ECrowdDemoOrcaSolveStatus::PreferredFeasible
          || SolveResult.Status == ECrowdDemoOrcaSolveStatus::ExactFeasible)
        && SolveResult.bSatisfiesAllHalfPlanes;
    };
    const auto Fold = [](uint32 Hash, const uint32 Value)
    {
      Hash ^= Value;
      return Hash * 16777619u;
    };
    for (const FCrowdDemoOrcaResult& Result : Pipeline->GetPreparedOrcaResults())
    {
      if (Result.FailureReason != ECrowdDemoOrcaFeasibility::TrueNoFeasibleWitness
        && Result.FallbackStage == 0 && !Result.bInfeasible)
      {
        continue;
      }
      const FCrowdDemoOrcaAgent* const* AgentPtr = AgentById.Find(Result.AgentId);
      if (!AgentPtr) continue;
      const FCrowdDemoOrcaAgent& Agent = **AgentPtr;
      const FCrowdDemoOrcaContinuousSolveInput Input =
        FCrowdDemoDeterministicOrcaKernel::MakeContinuousSolveInput(
          Agent.PreferredVelocity, Agent.MaxSpeedCmps, Result.Constraints,
          Settings.ConstraintEpsilonCmps);
      const FCrowdDemoOrcaContinuousSolveResult Current =
        FCrowdDemoDeterministicOrcaKernel::SolveContinuousExact(Input);
      const FCrowdDemoOrcaContinuousSolveResult Reference =
        FCrowdDemoRvo2ReferenceSolver::Solve(Input);
      const bool bCurrentExact = IsExact(Current)
        && FCrowdDemoDeterministicOrcaKernel::ValidateContinuousVelocity(Input, Current.Velocity);
      const bool bReferenceExact = IsExact(Reference)
        && FCrowdDemoDeterministicOrcaKernel::ValidateContinuousVelocity(Input, Reference.Velocity);
      const FCrowdDemoOrcaFeasibilityOracleResult Oracle =
        FCrowdDemoDeterministicOrcaKernel::FindFeasibleVelocityOracle(
          Agent.PreferredVelocity, Agent.MaxSpeedCmps, Result.Constraints, Settings);
      ++Differential.SampleCount;
      Differential.CurrentExactCount += bCurrentExact ? 1 : 0;
      Differential.ReferenceExactCount += bReferenceExact ? 1 : 0;
      Differential.CurrentMissReferenceHitCount += !bCurrentExact && bReferenceExact ? 1 : 0;
      Differential.BothMissOracleHitCount += !bCurrentExact && !bReferenceExact
        && Oracle.bFoundFeasibleWitness ? 1 : 0;
      Differential.CurrentHitReferenceMissCount += bCurrentExact && !bReferenceExact ? 1 : 0;
      Differential.AllExactMissCount += !bCurrentExact && !bReferenceExact
        && !Oracle.bFoundFeasibleWitness ? 1 : 0;
      Differential.OracleWitnessAvailableCount += Oracle.bFoundFeasibleWitness ? 1 : 0;
      Differential.BestEffortUsedCount += Result.FallbackStage > 0 ? 1 : 0;
      if (bCurrentExact)
      {
        FVector2f Quantized;
        const ECrowdDemoOrcaQuantizationResult Quantization =
          FCrowdDemoDeterministicOrcaKernel::QuantizeAndValidateVelocityDetailed(
            Current.Velocity, Agent.PreferredVelocity, Agent.MaxSpeedCmps,
            Result.Constraints, Settings, Quantized);
        Differential.ContinuousHitQuantizedMissCount +=
          Quantization == ECrowdDemoOrcaQuantizationResult::NoSolution ? 1 : 0;
        Differential.ThreeByThreeRecoveredCount +=
          Quantization == ECrowdDemoOrcaQuantizationResult::NeighborhoodRecovered ? 1 : 0;
      }
      const bool bDisagreement = bCurrentExact != bReferenceExact
        || (!bCurrentExact && !bReferenceExact);
      if (bDisagreement)
      {
        uint32 FixtureHash = 2166136261u;
        FixtureHash = Fold(FixtureHash, static_cast<uint32>(Result.Constraints.Num()));
        FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Agent.PreferredVelocity.X)));
        FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Agent.PreferredVelocity.Y)));
        FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Agent.MaxSpeedCmps)));
        FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Settings.ConstraintEpsilonCmps * 1000.0f)));
        for (const FCrowdDemoOrcaConstraint& Constraint : Result.Constraints)
        {
          FixtureHash = Fold(FixtureHash, static_cast<uint32>(Constraint.StableConstraintOrder));
          FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Constraint.Point.X)));
          FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Constraint.Point.Y)));
          FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Constraint.Normal.X * 32767.0f)));
          FixtureHash = Fold(FixtureHash, static_cast<uint32>(FMath::RoundToInt(Constraint.Normal.Y * 32767.0f)));
        }
        if (Differential.MinimumFixtureHash == 0
          || Result.Constraints.Num() < Differential.MinimumFixtureConstraintCount
          || (Result.Constraints.Num() == Differential.MinimumFixtureConstraintCount
            && FixtureHash < Differential.MinimumFixtureHash))
        {
          Differential.MinimumFixtureHash = FixtureHash;
          Differential.MinimumFixtureConstraintCount = Result.Constraints.Num();
        }
      }
    }
    Pipeline->RecordSf3OrcaReferenceDifferential(Differential);
  }
#endif
  const FVector Goal = FVector(Pipeline->GetRules().FlowFieldConfig.GoalLocation);
  for (const FCrowdDemoOrcaResult& Result : Pipeline->GetPreparedOrcaResults())
  {
    if (Result.FailureReason != ECrowdDemoOrcaFeasibility::TrueNoFeasibleWitness) continue;
    const ECrowdDemoFlowLocationStatus Status = FlowStatusByAgentId.FindRef(Result.AgentId);
    if (Status == ECrowdDemoFlowLocationStatus::Reachable)
      ++Summary.TrueNoWitnessReachableFlowCount;
    else
      ++Summary.TrueNoWitnessInvalidFlowCount;
    if (const FVector* Location = LocationByAgentId.Find(Result.AgentId))
    {
      if (FVector::DistSquared2D(*Location, Goal) <= FMath::Square(400.0f))
        ++Summary.TrueNoWitnessGoalNearCount;
      if (Location->Y > -2050.0f && Location->Y < -650.0f)
        ++Summary.TrueNoWitnessCorridorCount;
    }
  }
  const float SolverMs = static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0);
  Pipeline->RecordSf3GoalOrcaStep(Agents, Pipeline->GetPreparedOrcaResults());
  TMap<int32, const FCrowdDemoOrcaResult*> ById;
  for (const auto& Result : Pipeline->GetPreparedOrcaResults()) ById.Add(Result.AgentId, &Result);
  TMap<int32, const FCrowdDemoOrcaAgent*> OrcaAgentById;
  for (const FCrowdDemoOrcaAgent& Agent : Agents) OrcaAgentById.Add(Agent.AgentId, &Agent);
  if (Pipeline->IsTransitCapacityShadowEnabled())
  {
    const double ShadowStart = FPlatformTime::Seconds();
    const FCrowdDemoAdaptiveSpacingSettings ShadowSettings =
      Pipeline->GetTransitCapacityShadowSettings();
    TMap<int32, const FCrowdDemoHoldingAssignment*> HoldingByAgentId;
    for (const FCrowdDemoHoldingAssignment& Assignment
      : Pipeline->GetPreparedHoldingAssignments())
      HoldingByAgentId.Add(Assignment.AgentId, &Assignment);
    TArray<FCrowdDemoJointVelocityAgent> ShadowAgents;
    for (const FCrowdDemoOrcaAgent& OrcaAgent : Agents)
    {
      const FCrowdDemoOrcaResult* const* Baseline = ById.Find(OrcaAgent.AgentId);
      if (!Baseline) continue;
      FCrowdDemoJointVelocityAgent& Joint = ShadowAgents.AddDefaulted_GetRef();
      Joint.AgentId = OrcaAgent.AgentId;
      Joint.Position = OrcaAgent.Position;
      Joint.Velocity = OrcaAgent.Velocity;
      Joint.PreferredVelocity = OrcaAgent.PreferredVelocity;
      Joint.BaselinePriorityOrcaVelocity = (*Baseline)->Velocity;
      Joint.PhysicalRadiusCm = OrcaAgent.RadiusCm;
      Joint.MaxSpeedCmps = OrcaAgent.MaxSpeedCmps;
      const ECrowdDemoPursuitSteeringState SteeringState =
        SteeringStateByAgentId.FindRef(OrcaAgent.AgentId);
      Joint.bTransitSeed = SteeringState == ECrowdDemoPursuitSteeringState::Commit;
      Joint.MotionWeightQ8 = Joint.bTransitSeed ? 2560
        : (SteeringState == ECrowdDemoPursuitSteeringState::StableOccupied
          || SteeringState == ECrowdDemoPursuitSteeringState::ReserveHold ? 256 : 512);
      if (const FCrowdDemoHoldingAssignment* const* Assignment =
        HoldingByAgentId.Find(OrcaAgent.AgentId))
      {
        Joint.bHasAssignedPosition = SteeringState != ECrowdDemoPursuitSteeringState::Pursuit
          && SteeringState != ECrowdDemoPursuitSteeringState::Reacquire;
        Joint.AssignedPosition = SteeringState == ECrowdDemoPursuitSteeringState::Commit
          || SteeringState == ECrowdDemoPursuitSteeringState::StableOccupied
          ? (*Assignment)->AssignedPosition : (*Assignment)->HoldingLocation;
        Joint.RecoveryWeightQ8 = Joint.bHasAssignedPosition ? 256 : 0;
      }
    }
    ShadowAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
    TArray<FCrowdDemoJointVelocityPair> ShadowPairs;
    const float NeighborDistance = Pipeline->GetRules().OrcaSettings.NeighborDistanceCm;
    for (int32 AIndex = 0; AIndex < ShadowAgents.Num(); ++AIndex)
      for (int32 BIndex = AIndex + 1; BIndex < ShadowAgents.Num(); ++BIndex)
      {
        if ((ShadowAgents[AIndex].Position - ShadowAgents[BIndex].Position).SizeSquared()
          > FMath::Square(NeighborDistance)) continue;
        FCrowdDemoJointVelocityPair Pair;
        if (FCrowdDemoJointVelocityKernel::BuildPair(ShadowAgents[AIndex],
          ShadowAgents[BIndex], ShadowSettings, Pipeline->GetRules().OrcaSettings, Pair))
          ShadowPairs.Add(Pair);
      }
    TArray<FCrowdDemoJointVelocityComponent> ShadowComponents;
    FCrowdDemoJointVelocitySummary JointSummary;
    const bool bComponentsValid = FCrowdDemoJointVelocityKernel::BuildLocalComponents(
      ShadowAgents, ShadowPairs, ShadowSettings, ShadowComponents, JointSummary);
    TArray<FCrowdDemoJointVelocityComponentResult> ShadowResults;
    if (bComponentsValid)
      FCrowdDemoJointVelocityKernel::Solve(ShadowAgents, ShadowPairs,
        ShadowComponents, ShadowSettings, ShadowResults, JointSummary);
    FCrowdDemoTransitCapacityShadowSummary ShadowSummary;
    ShadowSummary.ComponentCount = ShadowComponents.Num();
    ShadowSummary.MaximumComponentSize = JointSummary.MaximumComponentSize;
    ShadowSummary.OversizeCount = JointSummary.OversizeCount;
    ShadowSummary.SolvedCount = JointSummary.SolvedCount;
    ShadowSummary.HardInfeasibleCount = JointSummary.HardInfeasibleCount;
    ShadowSummary.IterationLimitCount = JointSummary.IterationLimitCount;
    ShadowSummary.ClearanceNotAchievedCount = JointSummary.ClearanceNotAchievedCount;
    ShadowSummary.NoForwardGainCount = JointSummary.NoForwardGainCount;
    ShadowSummary.InvalidInputCount = JointSummary.InvalidInputCount;
    ShadowSummary.InfeasibleCount = JointSummary.HardInfeasibleCount
      + JointSummary.IterationLimitCount + JointSummary.ClearanceNotAchievedCount
      + JointSummary.NoForwardGainCount + JointSummary.InvalidInputCount;
    ShadowSummary.NumericalFailureCount = JointSummary.NumericalFailureCount;
    ShadowSummary.QuantizedFailureCount = JointSummary.QuantizedValidationFailureCount;
    ShadowSummary.YieldingAgentCount = JointSummary.TransitYieldingAgentCount;
    ShadowSummary.TransitDirectRelevantAgentCount =
      JointSummary.TransitDirectRelevantAgentCount;
    ShadowSummary.HardSafetyClosureAgentCount =
      JointSummary.HardSafetyClosureAgentCount;
    ShadowSummary.HardPairViolationCount = JointSummary.HardPairDistanceViolationCount;
    ShadowSummary.JointCandidateHardPairViolationCount =
      JointSummary.JointCandidateHardPairViolationCount;
    ShadowSummary.BaselineFallbackHardPairViolationCount =
      JointSummary.BaselineFallbackHardPairViolationCount;
    ShadowSummary.PairDoubleOwnerCount = JointSummary.SpacingPairDoubleOwnerCount;
    ShadowSummary.TransitCapsuleClearanceDeficitCmMax =
      JointSummary.TransitCapsuleClearanceDeficitCmMax;
    ShadowSummary.JointCandidateClearanceDeficitCmMax =
      JointSummary.JointCandidateClearanceDeficitCmMax;
    ShadowSummary.BaselineFallbackClearanceDeficitCmMax =
      JointSummary.BaselineFallbackClearanceDeficitCmMax;
    ShadowSummary.MaximumYieldDisplacementCm = JointSummary.MaximumYieldDisplacementCm;
    uint32 ComponentHash = 2166136261u;
    const auto FoldShadow = [](uint32 Hash, const uint32 Value)
    { return (Hash ^ Value) * 16777619u; };
    for (const FCrowdDemoJointVelocityComponent& Component : ShadowComponents)
    {
      const int32 Size = Component.AgentIds.Num();
      ShadowSummary.Component2Count += Size <= 2 ? 1 : 0;
      ShadowSummary.Component5Count += Size > 2 && Size <= 5 ? 1 : 0;
      ShadowSummary.Component8Count += Size > 5 && Size <= 8 ? 1 : 0;
      ShadowSummary.Component12Count += Size > 8 && Size <= 12 ? 1 : 0;
      ShadowSummary.Component20Count += Size > 12 && Size <= 20 ? 1 : 0;
      ComponentHash = FoldShadow(ComponentHash, Component.StableHash);
    }
    ShadowSummary.ComponentHash = ComponentHash;
    ShadowSummary.JointHash = JointSummary.StableHash;
    FCrowdDemoTransitCapacitySettings CapacitySettings;
    for (const FCrowdDemoJointVelocityPair& Pair : ShadowPairs)
    {
      const FCrowdDemoJointVelocityAgent* A = ShadowAgents.FindByPredicate(
        [&](const auto& Agent) { return Agent.AgentId == Pair.AgentAId; });
      const FCrowdDemoJointVelocityAgent* B = ShadowAgents.FindByPredicate(
        [&](const auto& Agent) { return Agent.AgentId == Pair.AgentBId; });
      if (!A || !B) continue;
      CapacitySettings.PhysicalRadiusACm = A->PhysicalRadiusCm;
      CapacitySettings.PhysicalRadiusBCm = B->PhysicalRadiusCm;
      FCrowdDemoTransitApertureResult Aperture;
      FCrowdDemoJointVelocityKernel::EvaluateTransitAperture(CapacitySettings,
        (A->Position - B->Position).Size(), Aperture);
      ShadowSummary.ApertureDeficitCmMax = FMath::Max(
        ShadowSummary.ApertureDeficitCmMax,
        static_cast<float>(Aperture.ApertureDeficitCm));
    }
    FCrowdDemoJointVelocityEnvironment Environment;
    Environment.FlowConfig = Pipeline->GetRules().FlowFieldConfig;
    Environment.bValidateFlowAndObstacles = true;
    Environment.bValidateTargetExclusion = true;
    Environment.TargetLocation = Pipeline->GetPursuitTargetFact().Location;
    Environment.TargetExclusionRadiusCm = Pipeline->GetPursuitTargetFact().RadiusCm
      + 42.0f + Pipeline->GetPursuitPositioningSettings().SafetyGapCm;
    int32 BaselineForward = 0, JointForward = 0;
    for (FCrowdDemoJointVelocityComponentResult& Result : ShadowResults)
    {
      FCrowdDemoJointVelocityComponentResult Validated;
      FCrowdDemoJointVelocityKernel::ValidateComponentEnvironment(
        ShadowAgents, Result, ShadowSettings, Environment, Validated);
      ShadowSummary.ObstacleViolationCount += Validated.ObstacleViolationCount;
      ShadowSummary.FlowBoundsViolationCount += Validated.FlowBoundsViolationCount;
      ShadowSummary.TargetViolationCount += Validated.TargetViolationCount;
      ShadowSummary.JointCandidateFlowBoundsViolationCount +=
        Validated.JointCandidateFlowBoundsViolationCount;
      ShadowSummary.JointCandidateObstacleViolationCount +=
        Validated.JointCandidateObstacleViolationCount;
      ShadowSummary.JointCandidateTargetViolationCount +=
        Validated.JointCandidateTargetViolationCount;
      ShadowSummary.BaselineFallbackFlowBoundsViolationCount +=
        Validated.BaselineFallbackFlowBoundsViolationCount;
      ShadowSummary.BaselineFallbackObstacleViolationCount +=
        Validated.BaselineFallbackObstacleViolationCount;
      ShadowSummary.BaselineFallbackTargetViolationCount +=
        Validated.BaselineFallbackTargetViolationCount;
      ShadowSummary.PreferredSpacingDeficitCmMax = FMath::Max(
        ShadowSummary.PreferredSpacingDeficitCmMax,
        Result.PreferredSpacingDeficitCmMax);
      for (const FCrowdDemoJointVelocityAgentResult& JointResult : Result.Agents)
      {
        const FCrowdDemoJointVelocityAgent* Agent = ShadowAgents.FindByPredicate(
          [&](const auto& Candidate) { return Candidate.AgentId == JointResult.AgentId; });
        if (!Agent || !Agent->bTransitSeed) continue;
        const FVector2f Forward = Agent->PreferredVelocity.GetSafeNormal();
        BaselineForward += FMath::Max(0, FMath::RoundToInt(FVector2f::DotProduct(
          Agent->BaselinePriorityOrcaVelocity, Forward)));
        JointForward += FMath::Max(0, FMath::RoundToInt(FVector2f::DotProduct(
          JointResult.JointCandidateVelocity, Forward)));
      }
      Result = MoveTemp(Validated);
    }
    ShadowSummary.TransitForwardSpeedRatioQ15 = BaselineForward > 0
      ? FMath::Clamp(FMath::RoundToInt(static_cast<double>(JointForward) * 32767.0
        / BaselineForward), 0, 131068) : (JointForward > 0 ? 131068 : 0);
    ShadowSummary.SolverMs = static_cast<float>(
      (FPlatformTime::Seconds() - ShadowStart) * 1000.0);
    uint32 ShadowHash = FoldShadow(ComponentHash, JointSummary.StableHash);
    ShadowHash = FoldShadow(ShadowHash, static_cast<uint32>(ShadowSummary.SolvedCount));
    ShadowHash = FoldShadow(ShadowHash, static_cast<uint32>(ShadowSummary.InfeasibleCount));
    ShadowHash = FoldShadow(ShadowHash, static_cast<uint32>(ShadowSummary.HardInfeasibleCount));
    ShadowHash = FoldShadow(ShadowHash, static_cast<uint32>(ShadowSummary.IterationLimitCount));
    ShadowHash = FoldShadow(ShadowHash,
      static_cast<uint32>(ShadowSummary.ClearanceNotAchievedCount));
    ShadowHash = FoldShadow(ShadowHash,
      static_cast<uint32>(ShadowSummary.NoForwardGainCount));
    ShadowHash = FoldShadow(ShadowHash, static_cast<uint32>(ShadowSummary.InvalidInputCount));
    ShadowHash = FoldShadow(ShadowHash,
      static_cast<uint32>(ShadowSummary.TransitDirectRelevantAgentCount));
    ShadowHash = FoldShadow(ShadowHash,
      static_cast<uint32>(ShadowSummary.HardSafetyClosureAgentCount));
    ShadowHash = FoldShadow(ShadowHash, static_cast<uint32>(ShadowSummary.HardPairViolationCount));
    ShadowHash = FoldShadow(ShadowHash,
      static_cast<uint32>(ShadowSummary.JointCandidateHardPairViolationCount));
    ShadowHash = FoldShadow(ShadowHash,
      static_cast<uint32>(ShadowSummary.BaselineFallbackHardPairViolationCount));
    ShadowHash = FoldShadow(ShadowHash, static_cast<uint32>(ShadowSummary.ObstacleViolationCount));
    ShadowHash = FoldShadow(ShadowHash, static_cast<uint32>(ShadowSummary.TargetViolationCount));
    ShadowHash = FoldShadow(ShadowHash,
      static_cast<uint32>(ShadowSummary.JointCandidateObstacleViolationCount));
    ShadowHash = FoldShadow(ShadowHash,
      static_cast<uint32>(ShadowSummary.JointCandidateFlowBoundsViolationCount));
    ShadowHash = FoldShadow(ShadowHash,
      static_cast<uint32>(ShadowSummary.JointCandidateTargetViolationCount));
    ShadowHash = FoldShadow(ShadowHash,
      static_cast<uint32>(ShadowSummary.BaselineFallbackObstacleViolationCount));
    ShadowHash = FoldShadow(ShadowHash,
      static_cast<uint32>(ShadowSummary.BaselineFallbackFlowBoundsViolationCount));
    ShadowHash = FoldShadow(ShadowHash,
      static_cast<uint32>(ShadowSummary.BaselineFallbackTargetViolationCount));
    ShadowHash = FoldShadow(ShadowHash, static_cast<uint32>(FMath::RoundToInt(
      ShadowSummary.JointCandidateClearanceDeficitCmMax)));
    ShadowHash = FoldShadow(ShadowHash, static_cast<uint32>(FMath::RoundToInt(
      ShadowSummary.BaselineFallbackClearanceDeficitCmMax)));
    ShadowHash = FoldShadow(ShadowHash, static_cast<uint32>(ShadowSummary.TransitForwardSpeedRatioQ15));
    ShadowSummary.StableHash = ShadowHash;
    ShadowSummary.bValid = bComponentsValid
      && ShadowSummary.OversizeCount == 0
      && ShadowSummary.InfeasibleCount == 0
      && ShadowSummary.NumericalFailureCount == 0
      && ShadowSummary.QuantizedFailureCount == 0
      && ShadowSummary.JointCandidateHardPairViolationCount == 0
      && ShadowSummary.JointCandidateObstacleViolationCount == 0
      && ShadowSummary.JointCandidateFlowBoundsViolationCount == 0
      && ShadowSummary.JointCandidateTargetViolationCount == 0
      && ShadowSummary.PairDoubleOwnerCount == 0
      && ShadowSummary.JointCandidateClearanceDeficitCmMax
        <= ShadowSettings.PositionQuantumCm
      && ShadowSummary.TransitForwardSpeedRatioQ15 > 32767;
    if (Pipeline->ShouldBuildRoundResult())
    {
      FCrowdDemoTransitCapacityFailureFixture Fixture;
      FCrowdDemoJointVelocityKernel::BuildTransitCapacityFailureFixture(
        ShadowAgents, ShadowPairs, ShadowComponents, ShadowResults,
        ShadowSettings, Environment, Fixture);
      Pipeline->RecordTransitCapacityFailureFixture(Fixture);
    }
    Pipeline->RecordTransitCapacityShadow(ShadowAgents, ShadowPairs,
      ShadowComponents, ShadowResults, ShadowSummary);
  }
  if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
  {
    TArray<FSf3AgentHashRecord> HashRecords;
    for (const FCrowdDemoOrcaResult& Result : Pipeline->GetPreparedOrcaResults())
    {
      FSf3AgentHashRecord& Record = HashRecords.AddDefaulted_GetRef();
      Record.AgentId = Result.AgentId;
      Record.Velocity = FVector(Result.Velocity.X, Result.Velocity.Y, 0.0f);
      Record.Values[0] = Result.NeighborCount;
      Record.Values[1] = Result.ConstraintCount;
      Record.Values[2] = Result.FallbackStage;
      Record.Values[3] = Result.bAdjusted ? 1 : 0;
      Record.Values[4] = Result.bInfeasible ? 1 : 0;
      FCrowdDemoSf3DeterminismHashBuilder ConstraintHash(0, Result.Constraints.Num());
      for (const FCrowdDemoOrcaConstraint& Constraint : Result.Constraints)
      {
        ConstraintHash.AddInt(Constraint.OtherAgentId);
        ConstraintHash.AddVelocity(FVector(Constraint.Point.X, Constraint.Point.Y, 0.0f));
        ConstraintHash.AddDirection(Constraint.Normal);
      }
      Record.Values[5] = static_cast<int32>(ConstraintHash.Finalize());
      Record.Values[6] = Result.bOutputSatisfiesConstraints ? 1 : 0;
      Record.Values[7] = static_cast<int32>(Result.Feasibility);
    }
    TArray<int32> Keys;
    const uint32 Hash = HashSf3AgentRecords(Pipeline->GetCurrentFixedStepIndex(), HashRecords, Keys);
    Pipeline->RecordSf3StageHash(
      ECrowdDemoSf3DeterminismStage::DeterministicOrca, Hash, HashRecords.Num(), Keys);
  }
  struct FWaitTelemetry
  {
    int32 HeldSteps = 0;
    int32 NoProgressSteps = 0;
    int32 RouteForwardVelocityBucket = 0;
  };
  TMap<int32, FWaitTelemetry> WaitTelemetryByAgentId;
  TMap<int32, ECrowdDemoPursuitPositionState> PositionStateByAgentId;
  TMap<int32, ECrowdDemoFrontApproachPhase> PositionPhaseByAgentId;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Velocities = ChunkContext.GetMutableFragmentView<FCrowdDemoOrcaVelocityFragment>();
    const auto PositionAssignments =
      ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (const FCrowdDemoOrcaResult* const* Found = ById.Find(Identities[It].Id))
      {
        PositionStateByAgentId.Add(Identities[It].Id, PositionAssignments[It].State);
        PositionPhaseByAgentId.Add(
          Identities[It].Id, PositionAssignments[It].FrontApproachPhase);
        Velocities[It].Velocity = FVector((*Found)->Velocity.X, (*Found)->Velocity.Y, 0.0f);
        Velocities[It].NeighborCount = (*Found)->NeighborCount;
        Velocities[It].ConstraintCount = (*Found)->ConstraintCount;
        Velocities[It].FallbackStage = (*Found)->FallbackStage;
        Velocities[It].bAdjusted = (*Found)->bAdjusted;
        Velocities[It].bInfeasible = (*Found)->bInfeasible;
        FWaitTelemetry& Telemetry = WaitTelemetryByAgentId.Add(Identities[It].Id);
        Telemetry.HeldSteps = PositionAssignments[It].PhaseReservationHeldSteps;
        Telemetry.NoProgressSteps = PositionAssignments[It].FrontApproachNoProgressSteps;
        if (const FCrowdDemoOrcaAgent* const* OrcaAgent =
          OrcaAgentById.Find(Identities[It].Id))
        {
          Telemetry.RouteForwardVelocityBucket = FMath::RoundToInt(FVector2f::DotProduct(
            (*Found)->Velocity, (*OrcaAgent)->PreferredVelocity.GetSafeNormal()));
        }
      }
    }
  });
  if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning)
  {
    TMap<int32, const FCrowdDemoFrontPhaseReservationDecisionRecord*> DecisionByAgentId;
    for (const FCrowdDemoFrontPhaseReservationDecisionRecord& Decision :
      Pipeline->GetPreparedFrontPhaseReservationDecisions())
      DecisionByAgentId.Add(Decision.AgentId, &Decision);
    TArray<FCrowdDemoFrontReservationWaitAgent> WaitAgents;
    for (const FCrowdDemoFrontPhaseReservationRequest& Request :
      Pipeline->GetPreparedFrontPhaseReservationRequests())
    {
      FCrowdDemoFrontReservationWaitAgent& Agent = WaitAgents.AddDefaulted_GetRef();
      Agent.AgentId = Request.AgentId;
      Agent.RadiusCm = Request.RadiusCm;
      Agent.CurrentPhase = Request.CurrentPhase;
      Agent.RequestedPhase = Request.RequestedPhase;
      Agent.bHasRequest = Request.bHasRequest;
      Agent.bRequestValid = Request.bRequestValid;
      Agent.bTargetExclusionClear = Request.bTargetExclusionClear;
      Agent.bActiveMember = WaitTelemetryByAgentId.Contains(Request.AgentId);
      Agent.CurrentReservationPoints = Request.CurrentReservationPoints;
      Agent.RequestedReservationPoints = Request.RequestedReservationPoints;
      if (const FCrowdDemoFrontPhaseReservationDecisionRecord* const* Decision =
        DecisionByAgentId.Find(Request.AgentId)) Agent.Decision = (*Decision)->Decision;
      if (const FWaitTelemetry* Telemetry = WaitTelemetryByAgentId.Find(Request.AgentId))
      {
        Agent.HeldSteps = Telemetry->HeldSteps;
        Agent.NoProgressSteps = Telemetry->NoProgressSteps;
        Agent.RouteForwardVelocityBucket = Telemetry->RouteForwardVelocityBucket;
      }
    }
    FCrowdDemoFrontReservationWaitGraphSummary WaitSummary;
    FCrowdDemoFrontReservationWaitGraphFixture WaitFixture;
    FCrowdDemoPursuitPositioningKernel::AnalyzeFrontReservationWaitGraph(
      Pipeline->GetPursuitTargetFact(), Pipeline->GetPursuitPositioningSettings(), WaitAgents,
      Pipeline->GetPreparedFrontPhaseReservationResult().BlockingPairs,
      Pipeline->GetPreparedFrontReservationWaitEdges(), WaitSummary, WaitFixture);
    Pipeline->RecordFrontReservationWaitGraph(WaitSummary, WaitFixture);

    if (Pipeline->IsSf4ReservationOrcaDiagnosticEnabled()
      && Pipeline->ShouldBuildRoundResult()
      && !Pipeline->HasCapturedSf4ReservationOrcaDiagnostic())
    {
      TArray<FCrowdDemoSf4ReservationOrcaFixtureAgent> DiagnosticAgents;
      for (const FCrowdDemoOrcaAgent& OrcaAgent : Agents)
      {
        const FCrowdDemoOrcaResult* const* Result = ById.Find(OrcaAgent.AgentId);
        if (!Result) continue;
        FCrowdDemoSf4ReservationOrcaFixtureAgent& DiagnosticAgent =
          DiagnosticAgents.AddDefaulted_GetRef();
        DiagnosticAgent.Agent = OrcaAgent;
        DiagnosticAgent.PositionState = PositionStateByAgentId.FindRef(OrcaAgent.AgentId);
        DiagnosticAgent.CurrentPhase = PositionPhaseByAgentId.FindRef(OrcaAgent.AgentId);
        DiagnosticAgent.BaselineVelocity = (*Result)->Velocity;
        for (const FCrowdDemoOrcaConstraint& Constraint : (*Result)->Constraints)
        {
          FCrowdDemoSf4SourcedOrcaConstraint& Sourced =
            DiagnosticAgent.Constraints.AddDefaulted_GetRef();
          Sourced.Constraint = Constraint;
          const ECrowdDemoPursuitPositionState OtherState =
            PositionStateByAgentId.FindRef(Constraint.OtherAgentId);
          if (OtherState == ECrowdDemoPursuitPositionState::FrontCommitGranted
            || OtherState == ECrowdDemoPursuitPositionState::SlotCommit)
            Sourced.Source = ECrowdDemoSf4RouteConstraintSource::Active;
          else if (OtherState == ECrowdDemoPursuitPositionState::FrontAssignedWaiting)
            Sourced.Source = ECrowdDemoSf4RouteConstraintSource::Waiting;
          else if (OtherState == ECrowdDemoPursuitPositionState::StableOccupied
            || OtherState == ECrowdDemoPursuitPositionState::ReserveHold)
            Sourced.Source = ECrowdDemoSf4RouteConstraintSource::Stable;
          else
            Sourced.Source = ECrowdDemoSf4RouteConstraintSource::Other;
        }
      }
      DiagnosticAgents.Sort([](const auto& A, const auto& B)
      {
        return A.Agent.AgentId < B.Agent.AgentId;
      });
      FCrowdDemoSf4ReservationOrcaDiagnosticFixture SelectedFixture;
      int32 SelectedCoreCount = MAX_int32;
      for (const FCrowdDemoSf4ReservationOrcaFixtureAgent& Candidate : DiagnosticAgents)
      {
        if (Candidate.Agent.Sf4RouteMode != ECrowdDemoOrcaRouteMode::Active) continue;
        const FWaitTelemetry* Telemetry = WaitTelemetryByAgentId.Find(Candidate.Agent.AgentId);
        const float ForwardVelocity = FVector2f::DotProduct(
          Candidate.BaselineVelocity, Candidate.Agent.PreferredVelocity.GetSafeNormal());
        if (Telemetry && Telemetry->NoProgressSteps < 30 && ForwardVelocity > 10.0f) continue;
        FCrowdDemoSf4ReservationOrcaDiagnosticFixture Fixture;
        FCrowdDemoDeterministicOrcaKernel::AnalyzeSf4ReservationOrcaDiagnostic(
          Pipeline->GetPursuitTargetFact(),
          Pipeline->GetPursuitPositioningSettings().SafetyGapCm,
          Pipeline->GetCurrentFixedStepSeconds(), 30.0f,
          DiagnosticAgents, Candidate.Agent.AgentId,
          Pipeline->GetRules().OrcaSettings, Fixture);
        if (!Fixture.bValid) continue;
        const int32 CoreCount = Fixture.CoreConstraints.Num();
        if (CoreCount < SelectedCoreCount
          || (CoreCount == SelectedCoreCount
            && Fixture.Summary.PrimaryAgentId < SelectedFixture.Summary.PrimaryAgentId))
        {
          SelectedCoreCount = CoreCount;
          SelectedFixture = MoveTemp(Fixture);
        }
      }
      Pipeline->RecordSf4ReservationOrcaDiagnostic(SelectedFixture);
    }

  }
  Pipeline->RecordTrafficStep(Pipeline->GetPreparedTrafficSummary(), Summary, SolverMs);
  Pipeline->LogStageOnce(TEXT("07_deterministic_orca"), Agents.Num());
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
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
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
      }
    }
  });
  if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    Pipeline->CompleteSoftPressureRollbackCombatState(
      Pipeline->GetCurrentFixedStepIndex(), RollbackCombatStates);
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
  EntityQuery.AddRequirement<FCrowdDemoRoundSeparationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoOrcaVelocityFragment>(EMassFragmentAccess::ReadOnly);
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
  TArray<FSf3AgentHashRecord> HashRecords;
  TArray<FCrowdDemoFlowReachabilityStageSample> ReachabilitySamples;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const TConstArrayView<FCrowdDemoRoundMoveIntentFragment> Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const TConstArrayView<FCrowdDemoRoundSeparationFragment> Separations = ChunkContext.GetFragmentView<FCrowdDemoRoundSeparationFragment>();
    const TConstArrayView<FCrowdDemoOrcaVelocityFragment> OrcaVelocities = ChunkContext.GetFragmentView<FCrowdDemoOrcaVelocityFragment>();
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
      if (IsTrafficScenario(Pipeline->GetRules().Scenario))
      {
        ProposedVelocity = OrcaVelocities[It].Velocity.GetClampedToMaxSize2D(
          Pipeline->GetRules().MaxSpeedCmPerSecond);
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
      if (Pipeline->IsSf3FlowReachabilityDiagnosticEnabled())
        ReachabilitySamples.Add(MakeFlowReachabilityStageSample(
          Identities[It].Id, Pipeline->GetSharedFlowField(), Proposed[It].ProposedLocation,
          Proposed[It].ProposedVelocity,
          FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
            Pipeline->GetRules().FlowFieldConfig, Proposed[It].ProposedLocation)));
      if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
      {
        FSf3AgentHashRecord& Record = HashRecords.AddDefaulted_GetRef();
        Record.AgentId = Identities[It].Id;
        Record.Position = Proposed[It].ProposedLocation;
        Record.Velocity = Proposed[It].ProposedVelocity;
        Record.Auxiliary = Proposed[It].StartLocation;
      }
      ++AgentCount;
    }
  });
  Pipeline->RecordSf3FlowReachabilityStage(
    ECrowdDemoFlowReachabilityStage::MovementPredict, ReachabilitySamples);
  if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
  {
    TArray<int32> Keys;
    const uint32 Hash = HashSf3AgentRecords(Pipeline->GetCurrentFixedStepIndex(), HashRecords, Keys);
    Pipeline->RecordSf3StageHash(
      ECrowdDemoSf3DeterminismStage::MovementPredict, Hash, HashRecords.Num(), Keys);
  }
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
  const double StartSeconds = FPlatformTime::Seconds();
  FCrowdDemoParticleConstraintKernel::Solve(
    Agents, Environment, Settings, Pairs, Results, Summary, &Trace);
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
  TArray<FSf3AgentHashRecord> HashRecords;
  TArray<FCrowdDemoFlowReachabilityStageSample> ReachabilitySamples;
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
        IsTrafficScenario(Pipeline->GetRules().Scenario));
      Constraints[It].ConstrainedLocation = Result.Location;
      Constraints[It].ConstrainedVelocity = Result.Velocity;
      Constraints[It].bHitObstacle = Result.bHitObstacle;
      Constraints[It].bPenetrating = Result.bPenetrating;
      Constraints[It].bHitFlowBounds = Result.bHitFlowBounds;
      Constraints[It].FlowBoundsReprojectDeltaCm = Result.FlowBoundsReprojectDeltaCm;
      MaxNavigationDomainReprojectDeltaCm = FMath::Max(
        MaxNavigationDomainReprojectDeltaCm, Result.FlowBoundsReprojectDeltaCm);
      if (Pipeline->IsSf3FlowReachabilityDiagnosticEnabled())
        ReachabilitySamples.Add(MakeFlowReachabilityStageSample(
          Identities[It].Id, Pipeline->GetSharedFlowField(), Result.Location,
          Result.Velocity, Result.bPenetrating
            || FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(Config, Result.Location)));
      if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
      {
        FSf3AgentHashRecord& Record = HashRecords.AddDefaulted_GetRef();
        Record.AgentId = Identities[It].Id;
        Record.Position = Constraints[It].ConstrainedLocation;
        Record.Velocity = Constraints[It].ConstrainedVelocity;
        Record.Values[0] = Constraints[It].bHitObstacle ? 1 : 0;
        Record.Values[1] = Constraints[It].bPenetrating ? 1 : 0;
      }
      ++AgentCount;
    }
  });
  Pipeline->RecordSf3FlowReachabilityStage(
    ECrowdDemoFlowReachabilityStage::ObstacleConstraint, ReachabilitySamples);
  Pipeline->RecordNavigationDomainReprojectDelta(MaxNavigationDomainReprojectDeltaCm);
  if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
  {
    TArray<int32> Keys;
    const uint32 Hash = HashSf3AgentRecords(Pipeline->GetCurrentFixedStepIndex(), HashRecords, Keys);
    Pipeline->RecordSf3StageHash(
      ECrowdDemoSf3DeterminismStage::ObstacleConstraint, Hash, HashRecords.Num(), Keys);
  }
  Pipeline->LogStageOnce(
    Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("06_obstacle_constraint")
      : TEXT("05_obstacle_constraint"),
    AgentCount);
}

UCrowdDemoRoundHardSeparationPbdProcessor::UCrowdDemoRoundHardSeparationPbdProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundHardSeparationPbdProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundPbdCorrectionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundHardSeparationPbdProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!Pipeline || !Pipeline->IsActive()
    || Pipeline->GetRules().bEnableHardSeparationPbd == 0)
  {
    return;
  }

  TArray<FCrowdDemoHardSeparationPbdAgent>& Agents = Pipeline->GetPreparedPbdAgents();
  Agents.Reset();
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoRoundObstacleConstraintFragment> Constraints = ChunkContext.GetFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoHardSeparationPbdAgent& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.Location = Constraints[It].ConstrainedLocation;
      Agent.RadiusCm = Pipeline->GetRules().HardSeparationRadiusCm;
    }
  });

  FCrowdDemoHardSeparationPbdSettings Settings;
  Settings.IterationCount = Pipeline->GetRules().HardSeparationPbdIterations;
  Settings.MaxPairCorrectionPerIterationCm = Pipeline->GetRules().HardSeparationPbdMaxCorrectionCm;
  TArray<FCrowdDemoHardSeparationPbdPair>& Pairs = Pipeline->GetPreparedPbdPairs();
  TArray<FCrowdDemoHardSeparationPbdResult>& Results = Pipeline->GetPreparedPbdResults();
  FCrowdDemoHardSeparationPbdSummary Summary;
  const double SolveStartSeconds = FPlatformTime::Seconds();
  FCrowdDemoHardSeparationPbdKernel::Solve(Agents, Settings, Pairs, Results, Summary);
  const float SolverMilliseconds = static_cast<float>((FPlatformTime::Seconds() - SolveStartSeconds) * 1000.0);

  TMap<int32, int32>& ResultIndexById = Pipeline->GetPreparedPbdResultIndexByAgentId();
  ResultIndexById.Reset();
  for (int32 Index = 0; Index < Results.Num(); ++Index)
  {
    ResultIndexById.Add(Results[Index].AgentId, Index);
  }
  TArray<FCrowdDemoFlowReachabilityStageSample> ReachabilitySamples;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoRoundObstacleConstraintFragment> Constraints = ChunkContext.GetFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
    const TArrayView<FCrowdDemoRoundPbdCorrectionFragment> PbdCorrections = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundPbdCorrectionFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoRoundPbdCorrectionFragment& Correction = PbdCorrections[It];
      Correction = FCrowdDemoRoundPbdCorrectionFragment();
      Correction.PrePbdLocation = Constraints[It].ConstrainedLocation;
      Correction.CorrectedLocation = Constraints[It].ConstrainedLocation;
      const int32* ResultIndex = ResultIndexById.Find(Identities[It].Id);
      if (!ResultIndex)
      {
        continue;
      }
      const FCrowdDemoHardSeparationPbdResult& Result = Results[*ResultIndex];
      Correction.CorrectedLocation = Result.CorrectedLocation;
      Correction.Correction = Result.Correction;
      Correction.CorrectedPairCount = Result.CorrectedPairCount;
      Correction.bCorrected = !Result.Correction.IsNearlyZero(0.001f);
      if (Pipeline->IsSf3FlowReachabilityDiagnosticEnabled())
        ReachabilitySamples.Add(MakeFlowReachabilityStageSample(
          Identities[It].Id, Pipeline->GetSharedFlowField(), Correction.CorrectedLocation,
          FVector::ZeroVector,
          FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
            Pipeline->GetRules().FlowFieldConfig, Correction.CorrectedLocation)));
    }
  });
  Pipeline->RecordSf3FlowReachabilityStage(
    ECrowdDemoFlowReachabilityStage::HardPbd, ReachabilitySamples);
  if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
  {
    FCrowdDemoSf3DeterminismHashBuilder Hash(
      Pipeline->GetCurrentFixedStepIndex(), Pairs.Num() + Results.Num());
    TArray<int32> Keys;
    for (const FCrowdDemoHardSeparationPbdPair& Pair : Pairs)
    {
      Hash.AddInt(Pair.MinAgentId);
      Hash.AddInt(Pair.MaxAgentId);
      if (Keys.Num() < 8) Keys.Add(Pair.MinAgentId);
    }
    for (const FCrowdDemoHardSeparationPbdResult& Result : Results)
    {
      Hash.AddInt(Result.AgentId);
      Hash.AddPosition(Result.CorrectedLocation);
      Hash.AddVelocity(Result.Correction);
      Hash.AddInt(Result.CorrectedPairCount);
    }
    Pipeline->RecordSf3StageHash(
      ECrowdDemoSf3DeterminismStage::HardPbd, Hash.Finalize(), Pairs.Num() + Results.Num(), Keys);
  }
  Pipeline->RecordHardSeparationPbdSummary(Summary, SolverMilliseconds);
  Pipeline->LogStageOnce(TEXT("07_hard_separation_pbd"), Agents.Num());
}

UCrowdDemoRoundObstacleReprojectProcessor::UCrowdDemoRoundObstacleReprojectProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundObstacleReprojectProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundProposedMovementFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundPbdCorrectionFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundObstacleReprojectProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!Pipeline || !Pipeline->IsActive()
    || Pipeline->GetRules().bEnableHardSeparationPbd == 0)
  {
    return;
  }
  const FCrowdDemoSharedFlowFieldConfig& Config = Pipeline->GetRules().FlowFieldConfig;
  const float FixedStep = Pipeline->GetCurrentFixedStepSeconds();
  int32 AgentCount = 0;
  float MaxReprojectDeltaCm = 0.0f;
  float MaxNavigationDomainReprojectDeltaCm = 0.0f;
  TArray<FSf3AgentHashRecord> HashRecords;
  TArray<FCrowdDemoFlowReachabilityStageSample> ReachabilitySamples;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoRoundProposedMovementFragment> Proposed = ChunkContext.GetFragmentView<FCrowdDemoRoundProposedMovementFragment>();
    const TConstArrayView<FCrowdDemoRoundPbdCorrectionFragment> PbdCorrections = ChunkContext.GetFragmentView<FCrowdDemoRoundPbdCorrectionFragment>();
    const TArrayView<FCrowdDemoRoundObstacleConstraintFragment> Constraints = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const bool bPreviouslyHit = Constraints[It].bHitObstacle;
      const bool bPreviouslyPenetrating = Constraints[It].bPenetrating;
      const FVector ReprojectCandidate =
        Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning
        ? PbdCorrections[It].CorrectedLocation
        : QuantizeSf2State(PbdCorrections[It].CorrectedLocation);
      const FCrowdDemoSharedFlowConstraintResult Result = FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
        Config,
        Proposed[It].StartLocation,
        ReprojectCandidate,
        FixedStep,
        IsTrafficScenario(Pipeline->GetRules().Scenario));
      Constraints[It].ConstrainedLocation = Result.Location;
      MaxReprojectDeltaCm = FMath::Max(
        MaxReprojectDeltaCm,
        FVector::Dist2D(Result.Location, PbdCorrections[It].CorrectedLocation));
      Constraints[It].ConstrainedVelocity = Result.Velocity;
      Constraints[It].bHitObstacle = bPreviouslyHit || Result.bHitObstacle;
      Constraints[It].bPenetrating = bPreviouslyPenetrating || Result.bPenetrating
        || FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(Config, Result.Location);
      Constraints[It].bHitFlowBounds = Constraints[It].bHitFlowBounds || Result.bHitFlowBounds;
      Constraints[It].FlowBoundsReprojectDeltaCm = FMath::Max(
        Constraints[It].FlowBoundsReprojectDeltaCm, Result.FlowBoundsReprojectDeltaCm);
      MaxNavigationDomainReprojectDeltaCm = FMath::Max(
        MaxNavigationDomainReprojectDeltaCm, Result.FlowBoundsReprojectDeltaCm);
      if (Pipeline->IsSf3FlowReachabilityDiagnosticEnabled())
        ReachabilitySamples.Add(MakeFlowReachabilityStageSample(
          Identities[It].Id, Pipeline->GetSharedFlowField(), Result.Location,
          Result.Velocity, Constraints[It].bPenetrating));
      if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
      {
        FSf3AgentHashRecord& Record = HashRecords.AddDefaulted_GetRef();
        Record.AgentId = Identities[It].Id;
        Record.Position = Constraints[It].ConstrainedLocation;
        Record.Velocity = Constraints[It].ConstrainedVelocity;
        Record.Auxiliary = PbdCorrections[It].CorrectedLocation;
        Record.Values[0] = Constraints[It].bHitObstacle ? 1 : 0;
        Record.Values[1] = Constraints[It].bPenetrating ? 1 : 0;
      }
      ++AgentCount;
    }
  });
  Pipeline->RecordSf3FlowReachabilityStage(
    ECrowdDemoFlowReachabilityStage::ObstacleReproject, ReachabilitySamples);
  Pipeline->RecordNavigationDomainReprojectDelta(MaxNavigationDomainReprojectDeltaCm);
  if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
  {
    TArray<int32> Keys;
    const uint32 Hash = HashSf3AgentRecords(Pipeline->GetCurrentFixedStepIndex(), HashRecords, Keys);
    Pipeline->RecordSf3StageHash(
      ECrowdDemoSf3DeterminismStage::ObstacleReproject, Hash, HashRecords.Num(), Keys);
  }
  Pipeline->RecordPbdSafetyDeltas(MaxReprojectDeltaCm, 0.0f);
  Pipeline->LogStageOnce(TEXT("08_obstacle_reproject"), AgentCount);
}

UCrowdDemoRoundOverlapSampleProcessor::UCrowdDemoRoundOverlapSampleProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundOverlapSampleProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundOverlapSampleProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World
    ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()
    : nullptr;
  if (!Pipeline || !Pipeline->IsActive()
    || !IsTrafficScenario(Pipeline->GetRules().Scenario))
  {
    return;
  }
  TArray<FCrowdDemoHardSeparationPbdAgent> Agents;
  int32 ObstaclePenetrations = 0;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Constraints = ChunkContext.GetFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoHardSeparationPbdAgent& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.Location = Constraints[It].ConstrainedLocation;
      Agent.RadiusCm = Pipeline->GetRules().HardSeparationRadiusCm;
      ObstaclePenetrations += Constraints[It].bPenetrating ? 1 : 0;
    }
  });
  TArray<FCrowdDemoHardSeparationPbdPair> OverlapPairs;
  TArray<FCrowdDemoHardSeparationPbdPair> SeverePairs;
  TArray<FCrowdDemoHardSeparationPbdPair> ResidualPairs;
  FCrowdDemoHardSeparationPbdKernel::BuildOverlapPairs(
    Agents, Pipeline->GetRules().SeparationRadiusCm, OverlapPairs);
  FCrowdDemoHardSeparationPbdKernel::BuildOverlapPairs(
    Agents, Pipeline->GetRules().HardSeparationRadiusCm, SeverePairs);
  FCrowdDemoHardSeparationPbdKernel::BuildOverlapPairs(
    Agents, Pipeline->GetRules().HardSeparationRadiusCm * 2.0f, ResidualPairs);
  Pipeline->RecordSf3OverlapSample(
    OverlapPairs.Num(), SeverePairs.Num(), ResidualPairs.Num(), ObstaclePenetrations);
  Pipeline->LogStageOnce(TEXT("10_overlap_sample"), Agents.Num());
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
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundParticleConstraintFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoParticlePropertiesFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPortalAdmissionFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPassingBandFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPursuitGuidanceFragment>(EMassFragmentAccess::ReadOnly);
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
  TArray<FSf3AgentHashRecord> HashRecords;
  TArray<FCrowdDemoSf3RollbackAgentState> RollbackAgents;
  TArray<FCrowdDemoSoftPressureRollbackAgentState> SoftPressureRollbackAgents;
  TArray<FCrowdDemoParticleAppliedRoundSimState> ParticleAppliedStates;
  TArray<FCrowdDemoFlowReachabilityStageSample> ReachabilitySamples;
  TArray<int32> OpenSpawnAgentIds;
  TArray<FVector> OpenSpawnLocations;
  TArray<FCrowdDemoBidirectionalSwapStepAgent> BidirectionalSwapAgents;
  TArray<FCrowdDemoValidCorridorTransitStepAgent> ValidCorridorTransitAgents;
  float MaxFinalSafetyDeltaCm = 0.0f;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoRoundMoveIntentFragment> Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const TConstArrayView<FCrowdDemoRoundFlowSampleFragment> FlowSamples = ChunkContext.GetFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    const TConstArrayView<FCrowdDemoRoundObstacleConstraintFragment> Constraints = ChunkContext.GetFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
    const auto ParticleConstraints = ChunkContext.GetFragmentView<FCrowdDemoRoundParticleConstraintFragment>();
    const auto ParticleProperties = ChunkContext.GetFragmentView<FCrowdDemoParticlePropertiesFragment>();
    const TArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Admissions = ChunkContext.GetFragmentView<FCrowdDemoPortalAdmissionFragment>();
    const auto Bands = ChunkContext.GetFragmentView<FCrowdDemoPassingBandFragment>();
    const auto PositionAssignments = ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    const auto PursuitSteering = ChunkContext.GetFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
    const auto PursuitGuidance = ChunkContext.GetFragmentView<FCrowdDemoPursuitGuidanceFragment>();
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
        MaxFinalSafetyDeltaCm = FMath::Max(
          MaxFinalSafetyDeltaCm,
          ParticleConstraints[It].RealizedCorrection.Size2D());
      }
      else
      {
        State.Location = Constraints[It].ConstrainedLocation;
        State.Velocity = Constraints[It].ConstrainedVelocity;
      }
      if (!State.Velocity.IsNearlyZero())
      {
        State.YawDegrees = Intents[It].DesiredYawDegrees;
      }
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
      if (Pipeline->IsSf3FlowReachabilityDiagnosticEnabled())
        ReachabilitySamples.Add(MakeFlowReachabilityStageSample(
          Identities[It].Id, Pipeline->GetSharedFlowField(), State.Location,
          State.Velocity, Metric.bPenetrating));
      if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
      {
        FSf3AgentHashRecord& Record = HashRecords.AddDefaulted_GetRef();
        Record.AgentId = Identities[It].Id;
        Record.Position = State.Location;
        Record.Velocity = State.Velocity;
        Record.Values[0] = FMath::RoundToInt(State.YawDegrees);
        Record.Values[1] = State.PlanRevision;
        Record.Values[2] = Metric.bUnreachable ? 1 : 0;
        Record.Values[3] = Metric.bPenetrating ? 1 : 0;
      }
      if (IsTrafficScenario(Pipeline->GetRules().Scenario))
      {
        FCrowdDemoSf3RollbackAgentState& Rollback = RollbackAgents.AddDefaulted_GetRef();
        Rollback.AgentId = Identities[It].Id;
        Rollback.LifecycleSerial = Identities[It].LifecycleSerial;
        Rollback.Location = State.Location;
        Rollback.Velocity = State.Velocity;
        Rollback.YawDegrees = State.YawDegrees;
        Rollback.RadiusCm = Formations[It].RadiusCm;
        Rollback.Admission = Admissions[It];
        Rollback.Band = Bands[It];
        Rollback.FlowSample = FlowSamples[It];
        Rollback.PositionAssignment = PositionAssignments[It];
        Rollback.PursuitSteering = PursuitSteering[It];
        Rollback.PursuitGuidance = PursuitGuidance[It];
      }
      else if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
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
        Rollback.TargetApproach = TargetApproaches[It];
        Rollback.OpenSpawnRelaxation = OpenSpawnStates[It];
        Rollback.Combat = MakeCombatNetState(
          Stats[It], Businesses[It], Attacks[It], Reactives[It], HitFlashes[It], Visuals[It]);
        FCrowdDemoParticleAppliedRoundSimState& Applied =
          ParticleAppliedStates.AddDefaulted_GetRef();
        Applied.AgentId = Identities[It].Id;
        Applied.LifecycleSerial = Identities[It].LifecycleSerial;
        Applied.Position = State.Location;
        Applied.Velocity = State.Velocity;
        Applied.YawDegrees = State.YawDegrees;
        Applied.RadiusCm = Formations[It].RadiusCm;
        Applied.bInitialized = State.bInitialized;
        Applied.Combat = Rollback.Combat;
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
  Pipeline->RecordSf3FlowReachabilityStage(
    ECrowdDemoFlowReachabilityStage::MovementFinalize, ReachabilitySamples);
  if (IsTrafficScenario(Pipeline->GetRules().Scenario))
  {
    Pipeline->RecordSf3RollbackSnapshot(Pipeline->GetCurrentFixedStepIndex(), MoveTemp(RollbackAgents));
  }
  else if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    Pipeline->RecordParticleAppliedStateHash(
      FCrowdDemoParticleConstraintKernel::HashAppliedRoundSimState(
        Pipeline->GetCurrentRoundId(), Pipeline->GetCurrentPlanRevision(),
        Pipeline->GetCurrentFixedStepIndex(), Pipeline->GetCurrentStepEndServerTimeSeconds(),
        ParticleAppliedStates));
    Pipeline->RecordSoftPressureRollbackSnapshot(
      Pipeline->GetCurrentFixedStepIndex(), MoveTemp(SoftPressureRollbackAgents));
  }
  if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
  {
    TArray<int32> Keys;
    const uint32 Hash = HashSf3AgentRecords(Pipeline->GetCurrentFixedStepIndex(), HashRecords, Keys);
    Pipeline->RecordSf3StageHash(
      ECrowdDemoSf3DeterminismStage::FinalState, Hash, HashRecords.Num(), Keys);
  }
  Pipeline->RecordPbdSafetyDeltas(0.0f, MaxFinalSafetyDeltaCm);
  Pipeline->RecordFlowAgentSamples(MetricSamples, World->GetNetMode() == NM_Client);
  Pipeline->LogStageOnce(
    Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("09_movement_finalize")
      : TEXT("06_movement_finalize"),
    MetricSamples.Num());
}

UCrowdDemoRoundSteeringFirstDiagnosticProcessor::UCrowdDemoRoundSteeringFirstDiagnosticProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundSteeringFirstDiagnosticProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundProposedMovementFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoOrcaVelocityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundPbdCorrectionFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundSteeringFirstDiagnosticProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario
    != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
  constexpr int32 StateCount = 6;
  TArray<TArray<float>> Distances, PreferredForward, OrcaForward, FinalForward;
  Distances.SetNum(StateCount); PreferredForward.SetNum(StateCount);
  OrcaForward.SetNum(StateCount); FinalForward.SetNum(StateCount);
  FCrowdDemoSteeringFirstRuntimeDiagnostic Diagnostic;
  Diagnostic.StateCounts.Init(0, StateCount);
  Diagnostic.OrcaConstraintSourceMatrix.Init(0, StateCount * StateCount);
  Diagnostic.OrcaInfeasibleCounts.Init(0, StateCount);
  Diagnostic.OrcaFallbackStopCounts.Init(0, StateCount);
  Diagnostic.ReacquireReasonCounts.Init(0, 7);
  TArray<float> CommitArrivalErrors, CommitObstacleCorrections, CommitPbdCorrections;
  TArray<float> CommitRouteForwardSpeeds, StablePhysicalDisplacements,
    ReservePhysicalDisplacements;
  TMap<int32, int32> StateByAgentId;
  TArray<FCrowdDemoSf4UnfinishedAgentDiagnosticInput> UnfinishedInputs;
  TArray<FCrowdDemoSf4PhysicalUnsatisfiedAgentInput> PhysicalUnsatisfiedInputs;
  TMap<int32, int32> UnfinishedInputIndexByAgentId;
  const bool bCaptureTransitJoint = Pipeline->IsTransitJointDiagnosticEnabled()
    && Pipeline->ShouldBuildRoundResult()
    && !Pipeline->HasCapturedTransitJointDiagnostic();
  const bool bCaptureObstacleConstraint = IsSf4ObstacleConstraintDiagnosticEnabled()
    && Pipeline->ShouldBuildRoundResult();
  FCrowdDemoSharedFlowConstraintDiagnostic MovementConstraintDiagnostic;
  FCrowdDemoSharedFlowConstraintDiagnostic HandoffConstraintDiagnostic;
  ECrowdDemoPursuitSteeringState ObstacleDiagnosticState =
    ECrowdDemoPursuitSteeringState::Pursuit;
  ECrowdDemoFlowLocationStatus ObstacleDiagnosticFlowStatus =
    ECrowdDemoFlowLocationStatus::OutOfBounds;
  float ObstacleDiagnosticHandoffDistanceCm = -1.0f;
  bool bCapturedObstacleConstraint = false;
  TArray<FCrowdDemoTransitJointDiagnosticAgent> TransitDiagnosticAgents;
  TMap<int32, int32> TransitDiagnosticAgentIndexById;
  TMap<int32, const FCrowdDemoOrcaAgent*> PreparedOrcaAgentById;
  if (bCaptureTransitJoint)
    for (const FCrowdDemoOrcaAgent& Agent : Pipeline->GetPreparedOrcaAgents())
      PreparedOrcaAgentById.Add(Agent.AgentId, &Agent);
  TMap<int32, uint32> CommitRejectMaskByAgentId;
  TMap<int32, uint32> CommitYieldableMaskByAgentId;
  for (const FCrowdDemoCommitDecisionRecord& Decision
    : Pipeline->GetPreparedCommitGateResult().Decisions)
  {
    CommitRejectMaskByAgentId.Add(Decision.AgentId, Decision.RejectReasonMask);
    CommitYieldableMaskByAgentId.Add(Decision.AgentId, Decision.YieldableConflictMask);
  }
  TMap<int32, const FCrowdDemoHoldingAssignment*> HoldingByAgent;
  for (const FCrowdDemoHoldingAssignment& Assignment : Pipeline->GetPreparedHoldingAssignments())
    HoldingByAgent.Add(Assignment.AgentId, &Assignment);
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Ids = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Intents = ChunkContext.GetFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Proposed = ChunkContext.GetFragmentView<FCrowdDemoRoundProposedMovementFragment>();
    const auto Flows = ChunkContext.GetFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    const auto Steering = ChunkContext.GetFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
    const auto Orca = ChunkContext.GetFragmentView<FCrowdDemoOrcaVelocityFragment>();
    const auto Positions = ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    const auto Obstacles = ChunkContext.GetFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
    const auto Pbd = ChunkContext.GetFragmentView<FCrowdDemoRoundPbdCorrectionFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const int32 StateIndex = static_cast<int32>(Steering[It].SteeringState);
      if (StateIndex < 0 || StateIndex >= StateCount) continue;
      StateByAgentId.Add(Ids[It].Id, StateIndex);
      ++Diagnostic.StateCounts[StateIndex];
      if (Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::Reacquire)
      {
        const int32 Reason = static_cast<int32>(Steering[It].InvalidReason);
        if (Diagnostic.ReacquireReasonCounts.IsValidIndex(Reason))
          ++Diagnostic.ReacquireReasonCounts[Reason];
      }
      if (Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::Commit)
      {
        CommitArrivalErrors.Add(FVector::Dist2D(States[It].Location,
          Positions[It].DesiredLocation));
        Diagnostic.CommitNoProgressStepsMax = FMath::Max(
          Diagnostic.CommitNoProgressStepsMax,
          Steering[It].LastProgressFixedStep == INDEX_NONE ? 0
            : Pipeline->GetCurrentFixedStepIndex() - Steering[It].LastProgressFixedStep);
        CommitObstacleCorrections.Add(Obstacles[It].FlowBoundsReprojectDeltaCm);
        CommitPbdCorrections.Add(Pbd[It].Correction.Size2D());
      }
      const FVector2f Location(States[It].Location.X, States[It].Location.Y);
      FVector2f Destination = Location;
      FVector2f Forward(Flows[It].FlowDirection.X, Flows[It].FlowDirection.Y);
      if (const FCrowdDemoHoldingAssignment* const* Found = HoldingByAgent.Find(Ids[It].Id))
      {
        const bool bPositionDestination =
          Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::Commit
          || Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::StableOccupied;
        Destination = bPositionDestination ? (*Found)->AssignedPosition : (*Found)->HoldingLocation;
        if (Steering[It].SteeringState != ECrowdDemoPursuitSteeringState::Pursuit
          && Steering[It].SteeringState != ECrowdDemoPursuitSteeringState::Reacquire)
          Forward = (Destination - Location).GetSafeNormal();
        if (Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::Pursuit)
        {
          const bool bMayHandoff = FCrowdDemoPursuitPositioningKernel::ShouldEnterHolding(
            Location, (*Found)->HoldingLocation, Flows[It].Status,
            Pipeline->GetPursuitPositioningSettings(),
            Pipeline->GetRules().FlowFieldConfig);
          Diagnostic.PursuitOutsideHandoffCount += !bMayHandoff
            && Flows[It].Status == ECrowdDemoFlowLocationStatus::Reachable ? 1 : 0;
          Diagnostic.PursuitInvalidFlowCount += Flows[It].Status
            != ECrowdDemoFlowLocationStatus::Reachable ? 1 : 0;
        }
        if (bCaptureObstacleConstraint && Ids[It].Id == 6)
        {
          const FVector Start = Proposed[It].StartLocation;
          const FVector ProposedLocation = Proposed[It].ProposedLocation;
          const FVector HoldingLocation(
            (*Found)->HoldingLocation.X, (*Found)->HoldingLocation.Y, Start.Z);
          MovementConstraintDiagnostic =
            FCrowdDemoSharedFlowFieldKernel::DiagnoseMovementConstraint(
              Pipeline->GetRules().FlowFieldConfig, Start, ProposedLocation, true);
          HandoffConstraintDiagnostic =
            FCrowdDemoSharedFlowFieldKernel::DiagnoseMovementConstraint(
              Pipeline->GetRules().FlowFieldConfig, Start, HoldingLocation, true);
          ObstacleDiagnosticState = Steering[It].SteeringState;
          ObstacleDiagnosticFlowStatus = Flows[It].Status;
          ObstacleDiagnosticHandoffDistanceCm = FVector::Dist2D(Start, HoldingLocation);
          bCapturedObstacleConstraint = true;
        }
      }
      Distances[StateIndex].Add((Destination - Location).Size());
      const FVector2f Preferred(Intents[It].DesiredVelocity.X, Intents[It].DesiredVelocity.Y);
      const FVector2f OrcaVelocity(Orca[It].Velocity.X, Orca[It].Velocity.Y);
      const FVector2f FinalVelocity(States[It].Velocity.X, States[It].Velocity.Y);
      PreferredForward[StateIndex].Add(FVector2f::DotProduct(Preferred, Forward));
      OrcaForward[StateIndex].Add(FVector2f::DotProduct(OrcaVelocity, Forward));
      FinalForward[StateIndex].Add(FVector2f::DotProduct(FinalVelocity, Forward));
      const float DestinationDistance = (Destination - Location).Size();
      const bool bPhysicallySatisfied =
        (Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::StableOccupied
          || Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::ReserveHold)
        && DestinationDistance <= 30.0f;
      {
        FCrowdDemoSf4PhysicalUnsatisfiedAgentInput& Physical =
          PhysicalUnsatisfiedInputs.AddDefaulted_GetRef();
        Physical.AgentId = Ids[It].Id;
        Physical.State = Steering[It].SteeringState;
        Physical.PositionId = Positions[It].PositionId;
        Physical.HoldingId = Steering[It].HoldingId;
        if (const FCrowdDemoHoldingAssignment* const* Found = HoldingByAgent.Find(Ids[It].Id))
          Physical.HoldingId = (*Found)->HoldingId;
        Physical.InvalidReason = static_cast<int32>(Steering[It].InvalidReason);
        Physical.Location = Location;
        Physical.Destination = Destination;
        Physical.PreferredVelocity = Preferred;
        Physical.OrcaVelocity = OrcaVelocity;
        const float FixedDt = FMath::Max(0.001f, Pipeline->GetCurrentFixedStepSeconds());
        const FVector2f Start(Proposed[It].StartLocation.X, Proposed[It].StartLocation.Y);
        Physical.ObstacleVelocity = (FVector2f(Pbd[It].PrePbdLocation.X,
          Pbd[It].PrePbdLocation.Y) - Start) / FixedDt;
        Physical.PbdVelocity = (FVector2f(Pbd[It].CorrectedLocation.X,
          Pbd[It].CorrectedLocation.Y) - Start) / FixedDt;
        Physical.ReprojectVelocity = FVector2f(Obstacles[It].ConstrainedVelocity.X,
          Obstacles[It].ConstrainedVelocity.Y);
        Physical.FinalVelocity = FinalVelocity;
        Physical.CommitRejectReasonMask = CommitRejectMaskByAgentId.FindRef(Ids[It].Id);
        Physical.CommitYieldableConflictMask =
          CommitYieldableMaskByAgentId.FindRef(Ids[It].Id);
        Physical.bPhysicallySatisfied = bPhysicallySatisfied;
      }
      if (bCaptureTransitJoint)
      {
        FCrowdDemoTransitJointDiagnosticAgent& Transit =
          TransitDiagnosticAgents.AddDefaulted_GetRef();
        Transit.JointAgent.AgentId = Ids[It].Id;
        Transit.JointAgent.Position = FVector2f(
          Proposed[It].StartLocation.X, Proposed[It].StartLocation.Y);
        Transit.JointAgent.PreferredVelocity = Preferred;
        Transit.JointAgent.BaselinePriorityOrcaVelocity = OrcaVelocity;
        Transit.JointAgent.AssignedPosition = Destination;
        Transit.JointAgent.PhysicalRadiusCm = Formations[It].RadiusCm;
        Transit.JointAgent.MaxSpeedCmps = Pipeline->GetRules().MaxSpeedCmPerSecond;
        Transit.JointAgent.bHasAssignedPosition =
          Steering[It].SteeringState != ECrowdDemoPursuitSteeringState::Pursuit
          && Steering[It].SteeringState != ECrowdDemoPursuitSteeringState::Reacquire;
        Transit.JointAgent.RecoveryWeightQ8 =
          Transit.JointAgent.bHasAssignedPosition ? 256 : 0;
        Transit.JointAgent.MotionWeightQ8 =
          Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::Commit ? 1024
          : (Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::StableOccupied
            || Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::ReserveHold
            ? 256 : 512);
        if (const FCrowdDemoOrcaAgent* const* Prepared =
          PreparedOrcaAgentById.Find(Ids[It].Id))
        {
          Transit.JointAgent.Position = (*Prepared)->Position;
          Transit.JointAgent.Velocity = (*Prepared)->Velocity;
          Transit.JointAgent.PreferredVelocity = (*Prepared)->PreferredVelocity;
          Transit.JointAgent.PhysicalRadiusCm = (*Prepared)->RadiusCm;
          Transit.JointAgent.MaxSpeedCmps = (*Prepared)->MaxSpeedCmps;
        }
        Transit.SteeringState = StateIndex;
        Transit.StartLocation = FVector2f(
          Proposed[It].StartLocation.X, Proposed[It].StartLocation.Y);
        Transit.PredictedLocation = FVector2f(
          Proposed[It].ProposedLocation.X, Proposed[It].ProposedLocation.Y);
        Transit.ObstacleLocation = FVector2f(
          Pbd[It].PrePbdLocation.X, Pbd[It].PrePbdLocation.Y);
        Transit.PbdLocation = FVector2f(
          Pbd[It].CorrectedLocation.X, Pbd[It].CorrectedLocation.Y);
        Transit.ReprojectLocation = FVector2f(
          Obstacles[It].ConstrainedLocation.X, Obstacles[It].ConstrainedLocation.Y);
        Transit.FinalLocation = Location;
        Transit.PriorityOrcaVelocity = OrcaVelocity;
        Transit.PredictedVelocity = FVector2f(
          Proposed[It].ProposedVelocity.X, Proposed[It].ProposedVelocity.Y);
        const float FixedDt = FMath::Max(0.001f, Pipeline->GetCurrentFixedStepSeconds());
        Transit.ObstacleVelocity = (Transit.ObstacleLocation - Transit.StartLocation) / FixedDt;
        Transit.PbdVelocity = (Transit.PbdLocation - Transit.StartLocation) / FixedDt;
        Transit.ReprojectVelocity = FVector2f(
          Obstacles[It].ConstrainedVelocity.X, Obstacles[It].ConstrainedVelocity.Y);
        Transit.FinalVelocity = FinalVelocity;
        Transit.PbdCorrection = FVector2f(Pbd[It].Correction.X, Pbd[It].Correction.Y);
        Transit.ObstacleReprojectDelta = Transit.ReprojectLocation - Transit.PbdLocation;
        TransitDiagnosticAgentIndexById.Add(Transit.JointAgent.AgentId,
          TransitDiagnosticAgents.Num() - 1);
      }
      if (Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::Commit)
      {
        const float OrcaForwardSpeed = FVector2f::DotProduct(OrcaVelocity, Forward);
        CommitRouteForwardSpeeds.Add(OrcaForwardSpeed);
        Diagnostic.CommitPreferredNonzeroOrcaZeroCount += Preferred.Size() > 1.0f
          && OrcaVelocity.Size() <= 1.5f ? 1 : 0;
      }
      else if (Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::StableOccupied)
      {
        StablePhysicalDisplacements.Add(DestinationDistance);
        Diagnostic.StablePhysicalDisplacedCount += DestinationDistance > 30.0f ? 1 : 0;
        Diagnostic.PhysicallySatisfiedPositionCount += DestinationDistance <= 30.0f ? 1 : 0;
      }
      else if (Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::ReserveHold)
      {
        ReservePhysicalDisplacements.Add(DestinationDistance);
        Diagnostic.ReservePhysicalDisplacedCount += DestinationDistance > 30.0f ? 1 : 0;
        Diagnostic.PhysicallySatisfiedPositionCount += DestinationDistance <= 30.0f ? 1 : 0;
      }
      if (Steering[It].SteeringState != ECrowdDemoPursuitSteeringState::StableOccupied
        && Steering[It].SteeringState != ECrowdDemoPursuitSteeringState::ReserveHold)
      {
        auto& Input = UnfinishedInputs.AddDefaulted_GetRef();
        Input.AgentId = Ids[It].Id;
        Input.State = Steering[It].SteeringState;
        Input.Location = Location;
        Input.Destination = Destination;
        Input.PreferredVelocity = Preferred;
        Input.OrcaVelocity = OrcaVelocity;
        Input.FinalVelocity = FinalVelocity;
        Input.OrcaConstraintSourceCounts.Init(0, StateCount);
        Input.CommitRejectReasonMask = CommitRejectMaskByAgentId.FindRef(Ids[It].Id);
        Input.NoProgressSteps = Steering[It].LastProgressFixedStep == INDEX_NONE
          ? FMath::Max(0, Pipeline->GetCurrentFixedStepIndex() - Steering[It].StateEnterFixedStep)
          : FMath::Max(0, Pipeline->GetCurrentFixedStepIndex()
            - Steering[It].LastProgressFixedStep);
        UnfinishedInputIndexByAgentId.Add(Input.AgentId, UnfinishedInputs.Num() - 1);
      }
    }
  });
  for (const FCrowdDemoOrcaResult& Result : Pipeline->GetPreparedOrcaResults())
  {
    if (const int32* TransitIndex = TransitDiagnosticAgentIndexById.Find(Result.AgentId))
      TransitDiagnosticAgents[*TransitIndex].PriorityConstraints = Result.Constraints;
    const int32* SubjectState = StateByAgentId.Find(Result.AgentId);
    if (!SubjectState) continue;
    Diagnostic.OrcaInfeasibleCounts[*SubjectState] += Result.bInfeasible ? 1 : 0;
    Diagnostic.OrcaFallbackStopCounts[*SubjectState] += Result.FallbackStage == 4 ? 1 : 0;
    for (const FCrowdDemoOrcaConstraint& Constraint : Result.Constraints)
    {
      const int32* OtherState = StateByAgentId.Find(Constraint.OtherAgentId);
      if (OtherState)
      {
        ++Diagnostic.OrcaConstraintSourceMatrix[*SubjectState * StateCount + *OtherState];
        if (const int32* InputIndex = UnfinishedInputIndexByAgentId.Find(Result.AgentId))
          ++UnfinishedInputs[*InputIndex].OrcaConstraintSourceCounts[*OtherState];
      }
    }
  }
  const auto Pct = [](TArray<float> Values, const float Q)
  {
    if (Values.IsEmpty()) return 0.0f;
    Values.Sort();
    return Values[FMath::Clamp(FMath::CeilToInt(Values.Num() * Q) - 1, 0, Values.Num() - 1)];
  };
  Diagnostic.DistanceP50.SetNum(StateCount); Diagnostic.DistanceP95.SetNum(StateCount);
  Diagnostic.PreferredForwardP50.SetNum(StateCount); Diagnostic.PreferredForwardP95.SetNum(StateCount);
  Diagnostic.OrcaForwardP50.SetNum(StateCount); Diagnostic.OrcaForwardP95.SetNum(StateCount);
  Diagnostic.FinalForwardP50.SetNum(StateCount); Diagnostic.FinalForwardP95.SetNum(StateCount);
  for (int32 Index = 0; Index < StateCount; ++Index)
  {
    Diagnostic.DistanceP50[Index] = Pct(Distances[Index], 0.50f);
    Diagnostic.DistanceP95[Index] = Pct(Distances[Index], 0.95f);
    Diagnostic.PreferredForwardP50[Index] = Pct(PreferredForward[Index], 0.50f);
    Diagnostic.PreferredForwardP95[Index] = Pct(PreferredForward[Index], 0.95f);
    Diagnostic.OrcaForwardP50[Index] = Pct(OrcaForward[Index], 0.50f);
    Diagnostic.OrcaForwardP95[Index] = Pct(OrcaForward[Index], 0.95f);
    Diagnostic.FinalForwardP50[Index] = Pct(FinalForward[Index], 0.50f);
    Diagnostic.FinalForwardP95[Index] = Pct(FinalForward[Index], 0.95f);
  }
  Diagnostic.CommitArrivalErrorCmP95 = Pct(CommitArrivalErrors, 0.95f);
  Diagnostic.CommitObstacleCorrectionCmP95 = Pct(CommitObstacleCorrections, 0.95f);
  Diagnostic.CommitPbdCorrectionCmP95 = Pct(CommitPbdCorrections, 0.95f);
  Diagnostic.CommitRouteForwardSpeedCmpsP50 = Pct(CommitRouteForwardSpeeds, 0.50f);
  Diagnostic.CommitRouteForwardSpeedCmpsP95 = Pct(CommitRouteForwardSpeeds, 0.95f);
  Diagnostic.StablePhysicalDisplacementCmP95 = Pct(StablePhysicalDisplacements, 0.95f);
  for (const float Distance : StablePhysicalDisplacements)
    Diagnostic.StablePhysicalDisplacementCmMax = FMath::Max(
      Diagnostic.StablePhysicalDisplacementCmMax, Distance);
  Diagnostic.ReservePhysicalDisplacementCmP95 = Pct(ReservePhysicalDisplacements, 0.95f);
  for (const float Distance : ReservePhysicalDisplacements)
    Diagnostic.ReservePhysicalDisplacementCmMax = FMath::Max(
      Diagnostic.ReservePhysicalDisplacementCmMax, Distance);
  Pipeline->RecordSteeringFirstRuntimeDiagnostic(Diagnostic);
  if (Pipeline->ShouldBuildRoundResult())
  {
    if (bCaptureObstacleConstraint)
    {
      const TCHAR* Role = World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server");
      uint32 CombinedHash = 2166136261u;
      CombinedHash = (CombinedHash ^ MovementConstraintDiagnostic.StableHash) * 16777619u;
      CombinedHash = (CombinedHash ^ HandoffConstraintDiagnostic.StableHash) * 16777619u;
      CombinedHash = (CombinedHash ^ static_cast<uint32>(ObstacleDiagnosticState)) * 16777619u;
      CombinedHash = (CombinedHash ^ static_cast<uint32>(ObstacleDiagnosticFlowStatus)) * 16777619u;
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4ObstacleConstraintDiagnostic role=%s round_id=%d valid=%d agent=6 state=%d flow_status=%d start=%.3f,%.3f proposed=%.3f,%.3f domain=%.3f,%.3f obstacle_id=%d inflated_min=%.3f,%.3f inflated_max=%.3f,%.3f segment_entry_t=%.6f segment_exit_t=%.6f start_inside=%d end_inside=%d direct_clear=%d slide_x_clear=%d slide_y_clear=%d flow_bounds_delta_cm=%.3f handoff_distance_cm=%.3f handoff_obstacle_id=%d handoff_entry_t=%.6f handoff_exit_t=%.6f handoff_start_inside=%d handoff_end_inside=%d handoff_direct_clear=%d movement_hash=%u handoff_hash=%u fixture_hash=%u source=MassPipeline"),
        Role, Pipeline->GetCurrentRoundId(), bCapturedObstacleConstraint ? 1 : 0,
        static_cast<int32>(ObstacleDiagnosticState),
        static_cast<int32>(ObstacleDiagnosticFlowStatus),
        MovementConstraintDiagnostic.Start.X, MovementConstraintDiagnostic.Start.Y,
        MovementConstraintDiagnostic.Proposed.X, MovementConstraintDiagnostic.Proposed.Y,
        MovementConstraintDiagnostic.DomainProposed.X,
        MovementConstraintDiagnostic.DomainProposed.Y,
        MovementConstraintDiagnostic.SelectedObstacleId,
        MovementConstraintDiagnostic.SelectedInflatedMin.X,
        MovementConstraintDiagnostic.SelectedInflatedMin.Y,
        MovementConstraintDiagnostic.SelectedInflatedMax.X,
        MovementConstraintDiagnostic.SelectedInflatedMax.Y,
        MovementConstraintDiagnostic.SelectedSegmentEntryT,
        MovementConstraintDiagnostic.SelectedSegmentExitT,
        MovementConstraintDiagnostic.bStartInsideSelectedObstacle ? 1 : 0,
        MovementConstraintDiagnostic.bEndInsideSelectedObstacle ? 1 : 0,
        MovementConstraintDiagnostic.bDirectSegmentClear ? 1 : 0,
        MovementConstraintDiagnostic.bSlideXClear ? 1 : 0,
        MovementConstraintDiagnostic.bSlideYClear ? 1 : 0,
        MovementConstraintDiagnostic.FlowBoundsReprojectDeltaCm,
        ObstacleDiagnosticHandoffDistanceCm,
        HandoffConstraintDiagnostic.SelectedObstacleId,
        HandoffConstraintDiagnostic.SelectedSegmentEntryT,
        HandoffConstraintDiagnostic.SelectedSegmentExitT,
        HandoffConstraintDiagnostic.bStartInsideSelectedObstacle ? 1 : 0,
        HandoffConstraintDiagnostic.bEndInsideSelectedObstacle ? 1 : 0,
        HandoffConstraintDiagnostic.bDirectSegmentClear ? 1 : 0,
        MovementConstraintDiagnostic.StableHash,
        HandoffConstraintDiagnostic.StableHash,
        CombinedHash);
    }
    FCrowdDemoPursuitPositioningKernel::BuildUnfinishedBoundaryFixture(
      UnfinishedInputs, Pipeline->GetUnfinishedBoundaryFixture());
    Pipeline->RecordUnfinishedBoundaryDiagnostic();
    FCrowdDemoPursuitPositioningKernel::BuildPhysicalUnsatisfiedBoundaryFixture(
      PhysicalUnsatisfiedInputs, Pipeline->GetPhysicalUnsatisfiedBoundaryFixture());
    Pipeline->RecordPhysicalUnsatisfiedBoundaryDiagnostic();
    const TCHAR* Role = World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server");
    const auto& Fixture = Pipeline->GetUnfinishedBoundaryFixture();
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSf4UnfinishedBoundary role=%s agents=%d hash=%u valid=%d source=MassPipeline"),
      Role, Fixture.Agents.Num(), Fixture.StableHash, Fixture.bValid ? 1 : 0);
    for (const auto& Agent : Fixture.Agents)
    {
      const auto CountAt = [&](const int32 Index)
      { return Agent.OrcaConstraintSourceCounts.IsValidIndex(Index)
          ? Agent.OrcaConstraintSourceCounts[Index] : 0; };
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4UnfinishedAgent role=%s agent=%d state=%d distance_cm=%d preferred_cmps=%d,%d orca_cmps=%d,%d final_cmps=%d,%d constraints_from=%d,%d,%d,%d,%d,%d commit_reject_mask=%u no_progress_steps=%d source=MassPipeline"),
        Role, Agent.AgentId, static_cast<int32>(Agent.State), Agent.DistanceCm,
        Agent.PreferredVelocityCmps.X, Agent.PreferredVelocityCmps.Y,
        Agent.OrcaVelocityCmps.X, Agent.OrcaVelocityCmps.Y,
        Agent.FinalVelocityCmps.X, Agent.FinalVelocityCmps.Y,
        CountAt(0), CountAt(1), CountAt(2), CountAt(3), CountAt(4), CountAt(5),
        Agent.CommitRejectReasonMask, Agent.NoProgressSteps);
    }
    const auto& PhysicalFixture = Pipeline->GetPhysicalUnsatisfiedBoundaryFixture();
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSf4PhysicalUnsatisfiedBoundary role=%s agents=%d total=%d satisfied=%d expected_unsatisfied=%d count_closed=%d hash=%u valid=%d source=MassPipeline"),
      Role, PhysicalFixture.Agents.Num(), PhysicalFixture.TotalAgentCount,
      PhysicalFixture.PhysicallySatisfiedCount,
      PhysicalFixture.TotalAgentCount - PhysicalFixture.PhysicallySatisfiedCount,
      PhysicalFixture.bCountClosed ? 1 : 0, PhysicalFixture.StableHash,
      PhysicalFixture.bValid ? 1 : 0);
    for (const FCrowdDemoSf4PhysicalUnsatisfiedAgentDiagnostic& Agent
      : PhysicalFixture.Agents)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoSf4PhysicalUnsatisfiedAgent role=%s agent=%d state=%d position_id=%d holding_id=%d distance_cm=%d preferred_cmps=%d,%d orca_cmps=%d,%d obstacle_cmps=%d,%d pbd_cmps=%d,%d reproject_cmps=%d,%d final_cmps=%d,%d commit_reject_mask=%u commit_yieldable_mask=%u invalid_reason=%d source=MassPipeline"),
        Role, Agent.AgentId, static_cast<int32>(Agent.State), Agent.PositionId,
        Agent.HoldingId, Agent.DistanceCm,
        Agent.PreferredVelocityCmps.X, Agent.PreferredVelocityCmps.Y,
        Agent.OrcaVelocityCmps.X, Agent.OrcaVelocityCmps.Y,
        Agent.ObstacleVelocityCmps.X, Agent.ObstacleVelocityCmps.Y,
        Agent.PbdVelocityCmps.X, Agent.PbdVelocityCmps.Y,
        Agent.ReprojectVelocityCmps.X, Agent.ReprojectVelocityCmps.Y,
        Agent.FinalVelocityCmps.X, Agent.FinalVelocityCmps.Y,
        Agent.CommitRejectReasonMask, Agent.CommitYieldableConflictMask,
        Agent.InvalidReason);
    }
    if (bCaptureTransitJoint)
    {
      const FCrowdDemoAdaptiveSpacingSettings TransitSettings =
        Pipeline->GetTransitJointDiagnosticSettings();
      TMap<int32, const FCrowdDemoJointVelocityAgent*> JointAgentById;
      for (const FCrowdDemoTransitJointDiagnosticAgent& Agent : TransitDiagnosticAgents)
        JointAgentById.Add(Agent.JointAgent.AgentId, &Agent.JointAgent);
      TSet<uint64> PairKeys;
      TArray<FCrowdDemoJointVelocityPair> TransitPairs;
      for (const FCrowdDemoTransitJointDiagnosticAgent& Agent : TransitDiagnosticAgents)
      {
        for (const FCrowdDemoOrcaConstraint& Constraint : Agent.PriorityConstraints)
        {
          const int32 MinId = FMath::Min(Agent.JointAgent.AgentId, Constraint.OtherAgentId);
          const int32 MaxId = FMath::Max(Agent.JointAgent.AgentId, Constraint.OtherAgentId);
          const uint64 Key = (static_cast<uint64>(static_cast<uint32>(MinId)) << 32)
            | static_cast<uint32>(MaxId);
          if (PairKeys.Contains(Key)) continue;
          const FCrowdDemoJointVelocityAgent* const* Other =
            JointAgentById.Find(Constraint.OtherAgentId);
          if (!Other) continue;
          FCrowdDemoJointVelocityPair Pair;
          if (FCrowdDemoJointVelocityKernel::BuildPair(
            Agent.JointAgent, **Other, TransitSettings,
            Pipeline->GetRules().OrcaSettings, Pair))
          {
            PairKeys.Add(Key);
            TransitPairs.Add(Pair);
          }
        }
      }
      FCrowdDemoTransitJointDiagnosticFixture TransitFixture;
      FCrowdDemoJointVelocityKernel::BuildDiagnosticFixture(
        6, TransitDiagnosticAgents, TransitPairs, TransitSettings, TransitFixture);
      Pipeline->RecordTransitJointDiagnostic(TransitFixture);
    }
  }
}

UCrowdDemoRoundResidualPositioningDiagnosticProcessor::UCrowdDemoRoundResidualPositioningDiagnosticProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundResidualPositioningDiagnosticProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundResidualPositioningDiagnosticProcessor::Execute(
  FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning
    || !Pipeline->ShouldBuildRoundResult()) return;
  struct FAgentFact
  {
    FCrowdDemoResidualPositioningAgent Residual;
    FVector2f Location = FVector2f::ZeroVector;
    float RadiusCm = 0.0f;
    ECrowdDemoFlowLocationStatus FlowStatus = ECrowdDemoFlowLocationStatus::OutOfBounds;
  };
  TArray<FAgentFact> Unfinished;
  TArray<FCrowdDemoHoldingAgent> MatchingAgents;
  TArray<FCrowdDemoPositionIngressBlocker> Blockers;
  TSet<int32> OccupiedPositionIds;
  TSet<int32> OccupiedHoldingIds;
  TMap<int32, const FCrowdDemoHoldingAssignment*> HoldingByAgent;
  for (const FCrowdDemoHoldingAssignment& Assignment : Pipeline->GetPreparedHoldingAssignments())
    HoldingByAgent.Add(Assignment.AgentId, &Assignment);
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Ids = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Positions = ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    const auto Steering = ChunkContext.GetFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
    const auto Flows = ChunkContext.GetFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const bool bCompleted = Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::StableOccupied
        || Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::ReserveHold;
      const FCrowdDemoHoldingAssignment* const* Holding = HoldingByAgent.Find(Ids[It].Id);
      FCrowdDemoHoldingAgent& MatchingAgent = MatchingAgents.AddDefaulted_GetRef();
      MatchingAgent.AgentId = Ids[It].Id;
      MatchingAgent.Location = FVector2f(States[It].Location.X, States[It].Location.Y);
      MatchingAgent.RadiusCm = Formations[It].RadiusCm;
      MatchingAgent.WaitEpoch = Steering[It].WaitEpoch;
      MatchingAgent.PositionId = Positions[It].PositionId;
      MatchingAgent.AssignedPosition = FVector2f(Positions[It].DesiredLocation.X,
        Positions[It].DesiredLocation.Y);
      MatchingAgent.PositionRole = Positions[It].Role;
      MatchingAgent.PositionIngressCost = Positions[It].PositionId;
      MatchingAgent.ExistingHoldingId = Steering[It].HoldingId;
      MatchingAgent.ExistingTargetRevision = Steering[It].TargetRevision;
      MatchingAgent.ExistingState = Steering[It].SteeringState;
      MatchingAgent.bPositionValid = Positions[It].PositionId != INDEX_NONE
        && Positions[It].TargetId == Pipeline->GetPursuitTargetFact().TargetId;
      if (bCompleted)
      {
        OccupiedPositionIds.Add(Positions[It].PositionId);
        if (Holding) OccupiedHoldingIds.Add((*Holding)->HoldingId);
        FCrowdDemoPositionIngressBlocker& B = Blockers.AddDefaulted_GetRef();
        B.AgentId = Ids[It].Id;
        B.PositionId = Positions[It].PositionId;
        B.TargetRevision = Steering[It].TargetRevision;
        B.State = Steering[It].SteeringState == ECrowdDemoPursuitSteeringState::StableOccupied
          ? ECrowdDemoPursuitPositionState::StableOccupied : ECrowdDemoPursuitPositionState::ReserveHold;
        B.Location = FVector2f(FMath::RoundToFloat(States[It].Location.X),
          FMath::RoundToFloat(States[It].Location.Y));
        B.RadiusCm = FMath::RoundToFloat(Formations[It].RadiusCm);
        continue;
      }
      FAgentFact& F = Unfinished.AddDefaulted_GetRef();
      F.Residual.AgentId = Ids[It].Id;
      F.Residual.PositionId = Positions[It].PositionId;
      F.Residual.HoldingId = Holding ? (*Holding)->HoldingId : INDEX_NONE;
      F.Residual.TargetRevision = Steering[It].TargetRevision;
      F.Residual.State = Steering[It].SteeringState;
      F.Residual.bHasHolding = Holding && (*Holding)->HoldingId != INDEX_NONE;
      F.Location = FVector2f(FMath::RoundToFloat(States[It].Location.X),
        FMath::RoundToFloat(States[It].Location.Y));
      F.RadiusCm = FMath::RoundToFloat(Formations[It].RadiusCm);
      F.FlowStatus = Flows[It].Status;
    }
  });
  Unfinished.Sort([](const auto& A, const auto& B){ return A.Residual.AgentId < B.Residual.AgentId; });
  Blockers.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  int32 PositionValidCount = 0;
  for (FCrowdDemoHoldingAgent& Agent : MatchingAgents)
  {
    PositionValidCount += Agent.bPositionValid ? 1 : 0;
    const FCrowdDemoHoldingPositionCompatibility* Edge =
      Pipeline->GetPreparedHoldingCompatibilities().FindByPredicate([&](const auto& E)
      { return E.HoldingId == Agent.ExistingHoldingId && E.PositionId == Agent.PositionId; });
    Agent.bExistingOwnerHardValid = Edge && Edge->bFlowReachable
      && Edge->bTargetClear && Edge->bObstacleClear;
  }
  FCrowdDemoHoldingMatchingInput MatchingInput;
  MatchingInput.Agents = MatchingAgents;
  MatchingInput.Holdings = Pipeline->GetPreparedHoldingCandidates();
  MatchingInput.Compatibility = Pipeline->GetPreparedHoldingCompatibilities();
  MatchingInput.TargetRevision = Pipeline->GetPursuitTargetFact().Revision;
  FCrowdDemoPursuitPositioningKernel::MatchHoldingPositions(
    MatchingInput, Pipeline->GetHoldingMatchingResult());
  Pipeline->RecordHoldingMatchingDiagnostic(PositionValidCount,
    Pipeline->GetHoldingSummary().AssignedCount);
  if (Pipeline->GetCurrentRoundId() == 1)
  {
    TMap<int32, const FCrowdDemoPositionCandidate*> FixedPositionById;
    for (const FCrowdDemoPositionCandidate& Position : Pipeline->GetPreparedPositionCandidates())
      FixedPositionById.Add(Position.PositionId, &Position);
    TArray<FCrowdDemoHoldingHallEdge> HallEdges;
    for (const FCrowdDemoHoldingAgent& Agent : MatchingAgents)
    {
      const FCrowdDemoPositionCandidate* const* Position = FixedPositionById.Find(Agent.PositionId);
      for (const FCrowdDemoHoldingCandidate& Holding : Pipeline->GetPreparedHoldingCandidates())
      {
        FCrowdDemoHoldingHallEdge& HallEdge = HallEdges.AddDefaulted_GetRef();
        HallEdge.AgentId = Agent.AgentId;
        HallEdge.PositionId = Agent.PositionId;
        HallEdge.HoldingId = Holding.HoldingId;
        const FCrowdDemoHoldingPositionCompatibility* Base =
          Pipeline->GetPreparedHoldingCompatibilities().FindByPredicate([&](const auto& Edge)
          { return Edge.HoldingId == Holding.HoldingId && Edge.PositionId == Agent.PositionId; });
        HallEdge.bCompatibilityRecordPresent = Base != nullptr;
        HallEdge.bFlowClear = Base && Base->bFlowReachable;
        HallEdge.bTargetClear = Base && Base->bTargetClear;
        HallEdge.bObstacleClear = Base && Base->bObstacleClear;
        HallEdge.bRevisionValid = Agent.ExistingTargetRevision == MatchingInput.TargetRevision
          && Holding.TargetRevision == MatchingInput.TargetRevision;
        if (Position)
        {
          for (const FCrowdDemoPositionIngressBlocker& Blocker : Blockers)
          {
            if (Blocker.AgentId == Agent.AgentId
              || !FCrowdDemoPursuitPositioningKernel::PositioningSegmentConflictsWithBlocker(
                Holding.WorldLocation, (*Position)->WorldLocation, Agent.RadiusCm,
                Pipeline->GetPursuitPositioningSettings(), Blocker))
            {
              continue;
            }
            if (Blocker.State == ECrowdDemoPursuitPositionState::StableOccupied)
              HallEdge.StableBlockerAgentIds.Add(Blocker.AgentId);
            else HallEdge.ReserveBlockerAgentIds.Add(Blocker.AgentId);
          }
        }
        HallEdge.bCompatible = HallEdge.bCompatibilityRecordPresent
          && HallEdge.bFlowClear && HallEdge.bTargetClear && HallEdge.bObstacleClear
          && HallEdge.bRevisionValid && HallEdge.StableBlockerAgentIds.IsEmpty()
          && HallEdge.ReserveBlockerAgentIds.IsEmpty();
      }
    }
    FCrowdDemoPursuitPositioningKernel::AnalyzeHoldingHallDeficiency(
      MatchingInput, HallEdges, Pipeline->GetHoldingHallFixture());
    Pipeline->RecordHoldingHallDiagnostic();
    const FCrowdDemoHoldingHallFixture& Hall = Pipeline->GetHoldingHallFixture();
    if (!Hall.AgentIds.IsEmpty())
    {
      const int32 WitnessAgentId = Hall.AgentIds[0];
      const FCrowdDemoHoldingAgent* WitnessAgent = MatchingAgents.FindByPredicate(
        [&](const auto& Agent){ return Agent.AgentId == WitnessAgentId; });
      const FCrowdDemoPositionCandidate* const* WitnessPosition = WitnessAgent
        ? FixedPositionById.Find(WitnessAgent->PositionId) : nullptr;
      if (WitnessAgent && WitnessPosition)
      {
        FCrowdDemoPursuitPositioningKernel::AnalyzeHoldingHallGeometry(
          WitnessAgentId, **WitnessPosition, WitnessAgent->RadiusCm,
          Pipeline->GetPursuitTargetFact(), Pipeline->GetPursuitPositioningSettings(),
          Pipeline->GetPreparedHoldingCandidates(), Blockers,
          Pipeline->GetPreparedHoldingCompatibilities(), Pipeline->GetHallGeometryFixture());
        Pipeline->RecordHallGeometryDiagnostic();
      }
    }
    TArray<FCrowdDemoJointPositioningAgent> JointAgents;
    TArray<FCrowdDemoJointAgentHoldingEdge> AgentHoldingEdges;
    for (const FCrowdDemoHoldingAgent& Agent : MatchingAgents)
    {
      auto& JointAgent = JointAgents.AddDefaulted_GetRef();
      JointAgent.AgentId = Agent.AgentId;
      JointAgent.Location = FVector2f(FMath::RoundToFloat(Agent.Location.X),
        FMath::RoundToFloat(Agent.Location.Y));
      JointAgent.RadiusCm = FMath::RoundToFloat(Agent.RadiusCm);
      JointAgent.WaitEpoch = Agent.WaitEpoch;
      JointAgent.ExistingHoldingId = Agent.ExistingHoldingId;
      JointAgent.ExistingPositionId = Agent.PositionId;
      JointAgent.TargetRevision = Agent.ExistingTargetRevision;
      JointAgent.State = Agent.ExistingState;
      JointAgent.bExistingHardOwnerValid = Agent.bExistingOwnerHardValid;
      const bool bCurrentReachable = FCrowdDemoSharedFlowFieldKernel::Sample(
        Pipeline->GetSharedFlowField(), FVector(JointAgent.Location.X,
          JointAgent.Location.Y, 60.0f)).Status == ECrowdDemoFlowLocationStatus::Reachable;
      for (const FCrowdDemoHoldingCandidate& Holding : Pipeline->GetPreparedHoldingCandidates())
      {
        auto& Edge = AgentHoldingEdges.AddDefaulted_GetRef();
        Edge.AgentId = Agent.AgentId;
        Edge.HoldingId = Holding.HoldingId;
        Edge.QuantizedCurrentToHoldingCostCm = FMath::RoundToInt(
          (Holding.WorldLocation - JointAgent.Location).Size());
        Edge.bLocallyReachable = bCurrentReachable && Holding.bReachable
          && Holding.bClearanceValid && Holding.TargetRevision == MatchingInput.TargetRevision;
      }
    }
    FCrowdDemoPursuitPositioningKernel::PlanJointHoldingPositions(
      MatchingInput.TargetRevision, JointAgents, Pipeline->GetPreparedHoldingCandidates(),
      Pipeline->GetPreparedPositionCandidates(), AgentHoldingEdges,
      Pipeline->GetPreparedHoldingCompatibilities(), Pipeline->GetJointPositioningResult());
    Pipeline->RecordJointPositioningDiagnostic();
    if (Pipeline->GetJointPositioningResult().bValid
      && Pipeline->GetJointPositioningResult().MaximumCardinality == JointAgents.Num())
    {
      FCrowdDemoPursuitPositioningKernel::EvaluateJointCommitResidualProtection(
        MatchingInput.TargetRevision, Pipeline->GetPursuitPositioningSettings(),
        JointAgents, Pipeline->GetPreparedHoldingCandidates(),
        Pipeline->GetPreparedPositionCandidates(), AgentHoldingEdges,
        Pipeline->GetPreparedHoldingCompatibilities(), Pipeline->GetJointPositioningResult(),
        Pipeline->GetJointCommitResidualResult());
      Pipeline->RecordJointCommitResidualDiagnostic();
    }
  }
  TArray<int32> RemainingPositions;
  TMap<int32, const FCrowdDemoPositionCandidate*> PositionById;
  for (const FCrowdDemoPositionCandidate& Position : Pipeline->GetPreparedPositionCandidates())
  {
    PositionById.Add(Position.PositionId, &Position);
    if (!OccupiedPositionIds.Contains(Position.PositionId)) RemainingPositions.Add(Position.PositionId);
  }
  RemainingPositions.Sort();
  TArray<const FCrowdDemoHoldingCandidate*> AvailableHoldings;
  for (const FCrowdDemoHoldingCandidate& Holding : Pipeline->GetPreparedHoldingCandidates())
    if (!OccupiedHoldingIds.Contains(Holding.HoldingId)) AvailableHoldings.Add(&Holding);
  AvailableHoldings.Sort([](const auto& A, const auto& B){ return A.HoldingId < B.HoldingId; });
  TArray<FCrowdDemoResidualPositioningEdge> Edges;
  for (const FAgentFact& Agent : Unfinished)
  {
    for (const int32 PositionId : RemainingPositions)
    {
      const FCrowdDemoPositionCandidate* const* Position = PositionById.Find(PositionId);
      if (!Position) continue;
      for (const FCrowdDemoHoldingCandidate* Holding : AvailableHoldings)
      {
        const FCrowdDemoHoldingPositionCompatibility Base =
          FCrowdDemoPursuitPositioningKernel::EvaluateHoldingPositionCompatibility(
            Pipeline->GetPursuitTargetFact(), Agent.RadiusCm,
            Pipeline->GetPursuitPositioningSettings(), Pipeline->GetSharedFlowField(),
            *Holding, **Position, {});
        FCrowdDemoResidualPositioningEdge& Edge = Edges.AddDefaulted_GetRef();
        Edge.AgentId = Agent.Residual.AgentId; Edge.PositionId = PositionId;
        Edge.HoldingId = Holding->HoldingId;
        Edge.bCurrentToHoldingReachable = Agent.FlowStatus == ECrowdDemoFlowLocationStatus::Reachable
          && Holding->bReachable && Holding->bClearanceValid;
        Edge.bFlowClear = Base.bFlowReachable; Edge.bTargetClear = Base.bTargetClear;
        Edge.bObstacleClear = Base.bObstacleClear;
        Edge.bRevisionValid = Holding->TargetRevision == Pipeline->GetPursuitTargetFact().Revision
          && Agent.Residual.TargetRevision == Pipeline->GetPursuitTargetFact().Revision;
        for (const FCrowdDemoPositionIngressBlocker& Blocker : Blockers)
        {
          if (!FCrowdDemoPursuitPositioningKernel::PositioningSegmentConflictsWithBlocker(
            Holding->WorldLocation, (*Position)->WorldLocation, Agent.RadiusCm,
            Pipeline->GetPursuitPositioningSettings(), Blocker)) continue;
          if (Blocker.State == ECrowdDemoPursuitPositionState::StableOccupied)
            Edge.StableBlockerAgentIds.Add(Blocker.AgentId);
          else Edge.ReserveBlockerAgentIds.Add(Blocker.AgentId);
        }
      }
    }
  }
  TArray<FCrowdDemoResidualPositioningAgent> Agents;
  for (const FAgentFact& Fact : Unfinished) Agents.Add(Fact.Residual);
  FCrowdDemoResidualPositioningSummary Summary;
  FCrowdDemoPursuitPositioningKernel::AnalyzeResidualPositioning(
    Agents, RemainingPositions, Edges, Summary);
  Pipeline->RecordResidualPositioningSummary(Summary);
}

UCrowdDemoRoundPositionIngressDiagnosticProcessor::UCrowdDemoRoundPositionIngressDiagnosticProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundPositionIngressDiagnosticProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoPursuitGuidanceFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoOrcaVelocityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundPbdCorrectionFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundPositionIngressDiagnosticProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  auto* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsSf4IngressDiagnosticEnabled()) return;
  TMap<int32, const FCrowdDemoOrcaResult*> OrcaByAgentId;
  for (const FCrowdDemoOrcaResult& Result : Pipeline->GetPreparedOrcaResults())
    OrcaByAgentId.Add(Result.AgentId, &Result);
  TMap<int32, const FCrowdDemoFrontApproachRoute*> ApproachRouteByAgentId;
  for (const FCrowdDemoFrontApproachRoute& Route : Pipeline->GetPreparedPositionApproachRoutes())
    ApproachRouteByAgentId.Add(Route.AgentId, &Route);
  TArray<FCrowdDemoPositionIngressAgent> Agents;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const auto Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    const auto Assignments = ChunkContext.GetFragmentView<FCrowdDemoPositionAssignmentFragment>();
    const auto Guidance = ChunkContext.GetFragmentView<FCrowdDemoPursuitGuidanceFragment>();
    const auto Orca = ChunkContext.GetFragmentView<FCrowdDemoOrcaVelocityFragment>();
    const auto Pbd = ChunkContext.GetFragmentView<FCrowdDemoRoundPbdCorrectionFragment>();
    const auto Obstacles = ChunkContext.GetFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoPositionIngressAgent& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.Location = FVector2f(States[It].Location.X, States[It].Location.Y);
      Agent.FinalVelocity = FVector2f(States[It].Velocity.X, States[It].Velocity.Y);
      Agent.PreferredVelocity = FVector2f(Guidance[It].DesiredVelocity.X, Guidance[It].DesiredVelocity.Y);
      Agent.OrcaVelocity = FVector2f(Orca[It].Velocity.X, Orca[It].Velocity.Y);
      Agent.ObstacleVelocity = FVector2f(
        Obstacles[It].ConstrainedVelocity.X, Obstacles[It].ConstrainedVelocity.Y);
      Agent.PbdCorrection = FVector2f(Pbd[It].Correction.X, Pbd[It].Correction.Y);
      const FVector ObstacleCorrection = Obstacles[It].ConstrainedLocation - Pbd[It].CorrectedLocation;
      Agent.ObstacleCorrection = FVector2f(ObstacleCorrection.X, ObstacleCorrection.Y);
      Agent.RadiusCm = Formations[It].RadiusCm;
      Agent.PositionId = Assignments[It].PositionId;
      Agent.AssignedLocation = FVector2f(
        Assignments[It].DesiredLocation.X, Assignments[It].DesiredLocation.Y);
      Agent.Role = Assignments[It].Role;
      Agent.State = Assignments[It].State;
      Agent.ApproachPhase = Assignments[It].FrontApproachPhase;
      Agent.ComposeBoundarySwitchCount = Assignments[It].FrontApproachComposeBoundarySwitchCount;
      Agent.bRadialErrorImproved = Assignments[It].bFrontApproachRadialErrorImproved;
      Agent.bQuantizedProgressStall = Assignments[It].bFrontApproachQuantizedProgressStall;
      if (const FCrowdDemoFrontApproachRoute* const* Route = ApproachRouteByAgentId.Find(Agent.AgentId))
        Agent.RadialErrorCm = (*Route)->RadialErrorCm;
      Agent.PreviousLowSpeedSteps = Pipeline->GetPositionIngressLowSpeedSteps().FindRef(Agent.AgentId);
      if (const FCrowdDemoOrcaResult* const* Result = OrcaByAgentId.Find(Agent.AgentId))
      {
        for (const FCrowdDemoOrcaConstraint& Constraint : (*Result)->Constraints)
          Agent.OrcaConstraintOtherAgentIds.Add(Constraint.OtherAgentId);
        Agent.OrcaConstraintOtherAgentIds.Sort();
      }
    }
  });
  TArray<FCrowdDemoPositionIngressEvaluation> Evaluations;
  FCrowdDemoPositionIngressSummary Summary;
  FCrowdDemoPositionIngressFixture Fixture;
  FCrowdDemoPursuitPositioningKernel::EvaluateIngress(
    Pipeline->GetPursuitTargetFact(), Pipeline->GetPursuitPositioningSettings(),
    Pipeline->GetPreparedPositionCandidates(), Agents, Evaluations, Summary, Fixture);
  TArray<float> RadialPreferredSpeeds, RadialOrcaSpeeds, RadialFinalSpeeds, RadialErrors;
  TArray<float> RadialOrcaForwardSpeeds, RadialFinalForwardSpeeds;
  TArray<float> RadialOrcaConstraintCounts;
  TMap<int32, ECrowdDemoPursuitPositionState> PositionStateByAgentId;
  for (const FCrowdDemoPositionIngressAgent& Agent : Agents)
    PositionStateByAgentId.Add(Agent.AgentId, Agent.State);
  for (const FCrowdDemoPositionIngressAgent& Agent : Agents)
  {
    Summary.FrontAssignedWaitingCount += Agent.State
      == ECrowdDemoPursuitPositionState::FrontAssignedWaiting ? 1 : 0;
    Summary.RadialStageCount += Agent.ApproachPhase
      == ECrowdDemoFrontApproachPhase::RadialStage ? 1 : 0;
    Summary.AngularAlignCount += Agent.ApproachPhase
      == ECrowdDemoFrontApproachPhase::AngularAlign ? 1 : 0;
    Summary.RadialCommitCount += Agent.ApproachPhase
      == ECrowdDemoFrontApproachPhase::RadialCommit ? 1 : 0;
    Summary.ComposeBoundarySwitchCount += Agent.ComposeBoundarySwitchCount;
    if (Agent.ApproachPhase == ECrowdDemoFrontApproachPhase::RadialStage)
    {
      RadialPreferredSpeeds.Add(Agent.PreferredVelocity.Size());
      RadialOrcaSpeeds.Add(Agent.OrcaVelocity.Size());
      RadialFinalSpeeds.Add(Agent.FinalVelocity.Size());
      const FVector2f Forward = Agent.PreferredVelocity.GetSafeNormal();
      RadialOrcaForwardSpeeds.Add(FVector2f::DotProduct(Agent.OrcaVelocity, Forward));
      RadialFinalForwardSpeeds.Add(FVector2f::DotProduct(Agent.FinalVelocity, Forward));
      RadialOrcaConstraintCounts.Add(static_cast<float>(Agent.OrcaConstraintOtherAgentIds.Num()));
      for (const int32 OtherAgentId : Agent.OrcaConstraintOtherAgentIds)
      {
        const ECrowdDemoPursuitPositionState* OtherState = PositionStateByAgentId.Find(OtherAgentId);
        if (!OtherState) { ++Summary.RadialConstraintFromOtherCount; continue; }
        if (*OtherState == ECrowdDemoPursuitPositionState::FrontCommitGranted
          || *OtherState == ECrowdDemoPursuitPositionState::SlotCommit)
          ++Summary.RadialConstraintFromActiveCount;
        else if (*OtherState == ECrowdDemoPursuitPositionState::FrontAssignedWaiting)
          ++Summary.RadialConstraintFromWaitingCount;
        else if (*OtherState == ECrowdDemoPursuitPositionState::ReserveCommit)
          ++Summary.RadialConstraintFromReserveCommitCount;
        else if (*OtherState == ECrowdDemoPursuitPositionState::StableOccupied
          || *OtherState == ECrowdDemoPursuitPositionState::ReserveHold)
          ++Summary.RadialConstraintFromStableCount;
        else
          ++Summary.RadialConstraintFromOtherCount;
      }
      RadialErrors.Add(Agent.RadialErrorCm);
      Summary.RadialErrorImprovedCount += Agent.bRadialErrorImproved ? 1 : 0;
      Summary.RadialQuantizedProgressStallCount += Agent.bQuantizedProgressStall ? 1 : 0;
    }
  }
  const auto Pct = [](TArray<float> Values, const float Q)
  {
    if (Values.IsEmpty()) return 0.0f;
    Values.Sort();
    return Values[FMath::Clamp(FMath::CeilToInt(Values.Num() * Q) - 1, 0, Values.Num() - 1)];
  };
  Summary.RadialPreferredSpeedP95 = Pct(RadialPreferredSpeeds, 0.95f);
  Summary.RadialOrcaSpeedP95 = Pct(RadialOrcaSpeeds, 0.95f);
  Summary.RadialFinalSpeedP95 = Pct(RadialFinalSpeeds, 0.95f);
  Summary.RadialOrcaForwardSpeedP50 = Pct(RadialOrcaForwardSpeeds, 0.50f);
  Summary.RadialOrcaForwardSpeedMin = RadialOrcaForwardSpeeds.IsEmpty()
    ? 0.0f : FMath::Min(RadialOrcaForwardSpeeds);
  Summary.RadialFinalForwardSpeedP50 = Pct(RadialFinalForwardSpeeds, 0.50f);
  Summary.RadialFinalForwardSpeedMin = RadialFinalForwardSpeeds.IsEmpty()
    ? 0.0f : FMath::Min(RadialFinalForwardSpeeds);
  Summary.RadialOrcaConstraintP95 = Pct(RadialOrcaConstraintCounts, 0.95f);
  Summary.RadialErrorP50 = Pct(RadialErrors, 0.50f);
  Summary.RadialErrorP95 = Pct(RadialErrors, 0.95f);
  Summary.RadialErrorMax = RadialErrors.IsEmpty() ? 0.0f : FMath::Max(RadialErrors);
  TArray<FCrowdDemoFrontApproachRoute> SortedRoutes = Pipeline->GetPreparedPositionApproachRoutes();
  SortedRoutes.Sort([](const auto& A, const auto& B){ return A.AgentId < B.AgentId; });
  uint32 RouteHash = 2166136261u;
  const auto FoldRoute = [](uint32 Hash, const uint32 Value)
  {
    Hash ^= Value;
    return Hash * 16777619u;
  };
  for (const FCrowdDemoFrontApproachRoute& Route : SortedRoutes)
  {
    RouteHash = FoldRoute(RouteHash, static_cast<uint32>(Route.AgentId));
    RouteHash = FoldRoute(RouteHash, Route.RouteHash);
    Summary.GateInvalidCount += !Route.bGateReachable || !Route.bArcReachable ? 1 : 0;
    Summary.RadialCommitBlockedCount += !Route.bRadialCommitClear ? 1 : 0;
  }
  Summary.RouteHash = RouteHash;
  TMap<int32, int32> LowSpeedSteps;
  for (const FCrowdDemoPositionIngressEvaluation& Evaluation : Evaluations)
    LowSpeedSteps.Add(Evaluation.AgentId, Evaluation.LowSpeedSteps);
  Pipeline->RecordPositionIngressDiagnostic(Summary, Fixture, MoveTemp(LowSpeedSteps));
  Pipeline->LogStageOnce(TEXT("13_position_ingress_diagnostic"), Evaluations.Num());
}

UCrowdDemoRoundSeparationProcessor::UCrowdDemoRoundSeparationProcessor()
  : EntityQuery(*this)
{
  ROUND_DYNAMIC_FLAGS;
}

void UCrowdDemoRoundSeparationProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSeparationFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoRoundSeparationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline = World ? World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>() : nullptr;
  if (!Pipeline || !Pipeline->IsActive())
  {
    return;
  }
  TArray<FCrowdDemoSeparationKernelAgent>& KernelAgents = Pipeline->GetPreparedSeparationAgents();
  KernelAgents.Reset();
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const TConstArrayView<FCrowdDemoRoundFormationFragment> Formations = ChunkContext.GetFragmentView<FCrowdDemoRoundFormationFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (!States[It].bInitialized)
      {
        continue;
      }
      FCrowdDemoSeparationKernelAgent& Agent = KernelAgents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.Location = States[It].Location;
      Agent.Velocity = States[It].Velocity;
      Agent.ContactRadiusCm = Pipeline->GetRules().HardSeparationRadiusCm;
      Agent.SeparationRadiusCm = Pipeline->GetRules().SeparationRadiusCm;
    }
  });

  TArray<FCrowdDemoSeparationKernelResult>& Results = Pipeline->GetPreparedSeparationResults();
  FCrowdDemoSeparationKernelSummary Summary;
  if (Pipeline->GetRules().bEnableSeparation != 0)
  {
    FCrowdDemoSeparationKernelSettings Settings;
    Settings.CellSizeCm = Pipeline->GetRules().SeparationCellSizeCm;
    Settings.SoftPushSpeedCmPerSecond = Pipeline->GetRules().SeparationSpeedCmPerSecond;
    Settings.HardPushSpeedCmPerSecond = Pipeline->GetRules().HardSeparationSpeedCmPerSecond;
    FCrowdDemoSeparationKernel::Solve(KernelAgents, Settings, Results, Summary);
  }
  else
  {
    Results.Reset();
    Results.Reserve(KernelAgents.Num());
    for (const FCrowdDemoSeparationKernelAgent& Agent : KernelAgents)
    {
      FCrowdDemoSeparationKernelResult& Result = Results.AddDefaulted_GetRef();
      Result.AgentId = Agent.AgentId;
    }
  }
  TMap<int32, int32>& ResultIndexById = Pipeline->GetPreparedResultIndexByAgentId();
  ResultIndexById.Reset();
  for (int32 Index = 0; Index < Results.Num(); ++Index)
  {
    ResultIndexById.Add(Results[Index].AgentId, Index);
  }
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TArrayView<FCrowdDemoRoundSeparationFragment> Separations = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSeparationFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoRoundSeparationFragment& Separation = Separations[It];
      const int32* ResultIndex = ResultIndexById.Find(Identities[It].Id);
      if (!ResultIndex)
      {
        Separation = FCrowdDemoRoundSeparationFragment();
        continue;
      }
      const FCrowdDemoSeparationKernelResult& Result = Results[*ResultIndex];
      Separation.PushVelocity = Result.PushVelocity;
      Separation.NeighborCount = Result.NeighborCount;
      Separation.OverlapCount = Result.OverlapCount;
      Separation.SevereOverlapCount = Result.SevereOverlapCount;
      Separation.bHardSeparation = Result.bHardSeparation;
    }
  });
  Pipeline->SetLastSeparationSummary(
    Summary.GridCellCount,
    Summary.AppliedAgentCount,
    Summary.OverlapPairCount,
    Summary.SevereOverlapPairCount);
  Pipeline->LogStageOnce(
    Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      ? TEXT("04_soft_separation")
      : TEXT("04_emergency_separation"),
    KernelAgents.Num());
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
    const TConstArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const TArrayView<FTransformFragment> Transforms = ChunkContext.GetMutableFragmentView<FTransformFragment>();
    const TArrayView<FMassVelocityFragment> Velocities = ChunkContext.GetMutableFragmentView<FMassVelocityFragment>();
    const TArrayView<FCrowdDemoMassMovementFragment> Movements = ChunkContext.GetMutableFragmentView<FCrowdDemoMassMovementFragment>();
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
    Result.OverlapPairCount = Pipeline->GetLastSeparationOverlapPairCount();
    Result.SevereOverlapPairCount = Pipeline->GetLastSeparationSevereOverlapPairCount();
    Result.SeparationAppliedAgentCount = Pipeline->GetLastSeparationAppliedAgentCount();
    Result.SeparationGridCellCount = Pipeline->GetLastSeparationGridCellCount();
    Result.PbdCorrectedAgentCount = Pipeline->GetLastPbdCorrectedAgentCount();
    Result.PbdCorrectedPairCount = Pipeline->GetLastPbdCorrectedPairCount();
    Result.PbdMaxPairCorrectionCm = Pipeline->GetLastPbdMaxPairCorrectionCm();
    Result.PbdMaxAgentTotalCorrectionCm = Pipeline->GetLastPbdMaxAgentTotalCorrectionCm();
    Result.PbdMaxObstacleReprojectDeltaCm = Pipeline->GetLastPbdMaxObstacleReprojectDeltaCm();
    Result.PbdMaxFinalSafetyDeltaCm = Pipeline->GetLastPbdMaxFinalSafetyDeltaCm();
    Result.PbdSolverMsP95 = Pipeline->GetPbdSolverMsP95();
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
      }
      if (Pipeline->GetRules().TargetRegionTransportSettings.bEnabled != 0)
      {
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
        Metrics.bT2Valid = Layout.bValid && Progress.bValid && Route.bValid
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
        Metrics.T4FinalDeadlockAgentCount = Progress.FinalDeadlockAgentIds.Num();
        Metrics.T4UnreachableSampleCount = Progress.UnreachableSampleCount;
        Metrics.T4LastFixedStep = Progress.LastFixedStepIndex;
        for (const TPair<int32, int32>& Pair : Progress.CompletionStepByAgentId)
          Metrics.T4CompletionStepMax = FMath::Max(
            Metrics.T4CompletionStepMax, Pair.Value);
        Metrics.bT4Valid = Layout.bValid && Progress.bValid
          && Pipeline->GetSharedFlowField().IsValid() ? 1 : 0;
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
        Metrics.T6TransitFinalDeadlockAgentCount = Progress.FinalDeadlockAgentIds.Num();
        Metrics.T6TransitUnreachableSampleCount = Progress.UnreachableSampleCount;
        Metrics.T6TransitLastFixedStep = Progress.LastFixedStepIndex;
        for (const TPair<int32, int32>& Pair : Progress.CompletionStepByAgentId)
          Metrics.T6TransitCompletionStepMax = FMath::Max(
            Metrics.T6TransitCompletionStepMax, Pair.Value);
        Metrics.bT6TransitValid = Layout.bValid && Progress.bValid
          && Pipeline->GetSharedFlowField().IsValid()
          && Pipeline->GetCapabilityProfileSummary().bValid ? 1 : 0;
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
    if (IsTrafficScenario(Pipeline->GetRules().Scenario)
      || Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
    {
      Result.TrafficMetrics = Pipeline->BuildTrafficMetrics(States);
      if (IsTrafficScenario(Pipeline->GetRules().Scenario))
        Pipeline->RecordSf3CompletedRoundHash(Result.TrafficMetrics.AgentStateHash);
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
    Pipeline->LogSf3DiagnosticBoundary(Frame.CorrectionRevision, TEXT("server_publish"));
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
  CrowdTrafficFieldBuildProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundCrowdTrafficFieldBuildProcessor>(*this, Owner, EntityManager);
  PortalScheduleProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundPortalScheduleProcessor>(*this, Owner, EntityManager);
  PositionCandidateBuildProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundPositionCandidateBuildProcessor>(*this, Owner, EntityManager);
  PositionAssignmentProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundPositionAssignmentProcessor>(*this, Owner, EntityManager);
  HoldingCandidateBuildProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundHoldingCandidateBuildProcessor>(*this, Owner, EntityManager);
  HoldingCompatibilityBuildProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundHoldingCompatibilityBuildProcessor>(*this, Owner, EntityManager);
  HoldingAssignmentProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundHoldingAssignmentProcessor>(*this, Owner, EntityManager);
  CommitRequestBuildProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundCommitRequestBuildProcessor>(*this, Owner, EntityManager);
  CommitGateScheduleProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundCommitGateScheduleProcessor>(*this, Owner, EntityManager);
  SteeringStateBoundaryApplyProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundSteeringStateBoundaryApplyProcessor>(*this, Owner, EntityManager);
  SteeringFirstPositionGuidanceProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundSteeringFirstPositionGuidanceProcessor>(*this, Owner, EntityManager);
  FrontAdmissionProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundFrontAdmissionProcessor>(*this, Owner, EntityManager);
  PositionApproachRouteProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundPositionApproachRouteProcessor>(*this, Owner, EntityManager);
  FrontPhaseReservationProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundFrontPhaseReservationProcessor>(*this, Owner, EntityManager);
  FrontPhaseReservationApplyProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundFrontPhaseReservationApplyProcessor>(*this, Owner, EntityManager);
  FlowPreferredVelocityProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundFlowPreferredVelocityProcessor>(*this, Owner, EntityManager);
  PassingBandGuidanceProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundPassingBandGuidanceProcessor>(*this, Owner, EntityManager);
  PursuitPositionGuidanceProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundPursuitPositionGuidanceProcessor>(*this, Owner, EntityManager);
  MovementIntentComposeProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundMovementIntentComposeProcessor>(*this, Owner, EntityManager);
  ElasticCrowdShadowProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundElasticCrowdShadowProcessor>(*this, Owner, EntityManager);
  DeterministicOrcaProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundDeterministicOrcaProcessor>(*this, Owner, EntityManager);
  LocalPredictiveInteractionProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundLocalPredictiveInteractionProcessor>(*this, Owner, EntityManager);
  MovementPredictProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundMovementPredictProcessor>(*this, Owner, EntityManager);
  ParticleConstraintProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundParticleConstraintProcessor>(*this, Owner, EntityManager);
  ObstacleConstraintProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundObstacleConstraintProcessor>(*this, Owner, EntityManager);
  HardSeparationPbdProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundHardSeparationPbdProcessor>(*this, Owner, EntityManager);
  ObstacleReprojectProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundObstacleReprojectProcessor>(*this, Owner, EntityManager);
  OverlapSampleProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundOverlapSampleProcessor>(*this, Owner, EntityManager);
  MovementFinalizeProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundMovementFinalizeProcessor>(*this, Owner, EntityManager);
  PositionIngressDiagnosticProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundPositionIngressDiagnosticProcessor>(*this, Owner, EntityManager);
  SteeringFirstDiagnosticProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundSteeringFirstDiagnosticProcessor>(*this, Owner, EntityManager);
  ResidualPositioningDiagnosticProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundResidualPositioningDiagnosticProcessor>(*this, Owner, EntityManager);
  SeparationProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundSeparationProcessor>(*this, Owner, EntityManager);
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
  int32 ExecutedSteps = 0;
  // A completed round has no next movement step, but its boundary must still be
  // allowed to activate the queued plan. The subsystem gate guarantees this
  // pre-loop call and the first loop call cannot both apply at one boundary.
  PlanApplyProcessor->CallExecute(EntityManager, Context);
  while (ExecutedSteps < MaxFixedStepsPerFrame)
  {
    PlanApplyProcessor->CallExecute(EntityManager, Context);
    if (!Pipeline->TryBeginFixedStep(TargetServerTime))
    {
      break;
    }
    RangedCombatProcessor->CallExecute(EntityManager, Context);
    HitResponseBoundaryApplyProcessor->CallExecute(EntityManager, Context);
    if (Pipeline->IsOpenSpawnRelaxation())
      OpenSpawnRelaxationPhasePrepareProcessor->CallExecute(EntityManager, Context);
    if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning
      || (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
        && (Pipeline->GetRules().TargetApproachSettings.bEnabled != 0
          || Pipeline->GetRules().TargetInfluenceSettings.bEnabled != 0)))
    {
      TargetFactApplyProcessor->CallExecute(EntityManager, Context);
    }
    SharedFlowFieldBuildProcessor->CallExecute(EntityManager, Context);
    if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && Pipeline->GetRules().TargetApproachSettings.bEnabled != 0)
      TargetSlotLayoutPrepareProcessor->CallExecute(EntityManager, Context);
    if (IsTrafficScenario(Pipeline->GetRules().Scenario))
    {
      CrowdTrafficFieldBuildProcessor->CallExecute(EntityManager, Context);
      PortalScheduleProcessor->CallExecute(EntityManager, Context);
      if (CrowdDemoUsesSteeringFirstSf4Pipeline(Pipeline->GetRules().Scenario))
      {
        PositionCandidateBuildProcessor->CallExecute(EntityManager, Context);
        HoldingCandidateBuildProcessor->CallExecute(EntityManager, Context);
        PositionAssignmentProcessor->CallExecute(EntityManager, Context);
        HoldingCompatibilityBuildProcessor->CallExecute(EntityManager, Context);
        HoldingAssignmentProcessor->CallExecute(EntityManager, Context);
        CommitRequestBuildProcessor->CallExecute(EntityManager, Context);
        CommitGateScheduleProcessor->CallExecute(EntityManager, Context);
        SteeringStateBoundaryApplyProcessor->CallExecute(EntityManager, Context);
      }
    }
    FlowPreferredVelocityProcessor->CallExecute(EntityManager, Context);
    if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && Pipeline->GetRules().TargetRegionTransportSettings.bEnabled != 0)
    {
      TargetPolarTopologyBuildProcessor->CallExecute(EntityManager, Context);
      TargetRegionPopulationBuildProcessor->CallExecute(EntityManager, Context);
      TargetRegionTransportSolveProcessor->CallExecute(EntityManager, Context);
      TargetRegionGuidanceProcessor->CallExecute(EntityManager, Context);
    }
    if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && Pipeline->GetRules().TargetApproachSettings.bEnabled != 0)
    {
      TargetApproachScheduleProcessor->CallExecute(EntityManager, Context);
      TargetApproachCommitProcessor->CallExecute(EntityManager, Context);
      TargetApproachGuidanceProcessor->CallExecute(EntityManager, Context);
    }
    if (IsTrafficScenario(Pipeline->GetRules().Scenario))
    {
      PassingBandGuidanceProcessor->CallExecute(EntityManager, Context);
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning)
      {
        SteeringFirstPositionGuidanceProcessor->CallExecute(EntityManager, Context);
        MovementIntentComposeProcessor->CallExecute(EntityManager, Context);
        ElasticCrowdShadowProcessor->CallExecute(EntityManager, Context);
      }
      DeterministicOrcaProcessor->CallExecute(EntityManager, Context);
    }
    ReactiveMotionIntentComposeProcessor->CallExecute(EntityManager, Context);
    if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && Pipeline->GetRules().LocalPredictiveSettings.bEnabled != 0)
      LocalPredictiveInteractionProcessor->CallExecute(EntityManager, Context);
    MovementPredictProcessor->CallExecute(EntityManager, Context);
    if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
      ParticleConstraintProcessor->CallExecute(EntityManager, Context);
    else
      ObstacleConstraintProcessor->CallExecute(EntityManager, Context);
    if (IsTrafficScenario(Pipeline->GetRules().Scenario))
    {
      HardSeparationPbdProcessor->CallExecute(EntityManager, Context);
      ObstacleReprojectProcessor->CallExecute(EntityManager, Context);
      if (IsTrafficScenario(Pipeline->GetRules().Scenario))
      {
        OverlapSampleProcessor->CallExecute(EntityManager, Context);
      }
    }
    MovementFinalizeProcessor->CallExecute(EntityManager, Context);
    VisualStateResolveProcessor->CallExecute(EntityManager, Context);
    if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning)
    {
      SteeringFirstDiagnosticProcessor->CallExecute(EntityManager, Context);
      ResidualPositioningDiagnosticProcessor->CallExecute(EntityManager, Context);
      PositionIngressDiagnosticProcessor->CallExecute(EntityManager, Context);
    }
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
    ++ExecutedSteps;
  }
}

#undef ROUND_DYNAMIC_FLAGS
