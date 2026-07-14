#include "Mass/CrowdDemoRoundSimProcessors.h"

#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoHardSeparationPbdKernel.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
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

#if WITH_DEV_AUTOMATION_TESTS
#include "ThirdParty/Reference/RVO2/CrowdDemoRvo2ReferenceSolver.h"
#endif

namespace
{
  constexpr int32 MaxFixedStepsPerFrame = 256;

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

  FCrowdDemoRoundAgentState MakeRoundAgentState(
    const FCrowdDemoMassIdentityFragment& Identity,
    const FCrowdDemoRoundFormationFragment& Formation,
    const FCrowdDemoRoundSimStateFragment& State)
  {
    FCrowdDemoRoundAgentState Result;
    Result.AgentId = Identity.Id;
    Result.LifecycleSerial = Identity.LifecycleSerial;
    Result.Location = FVector_NetQuantize10(State.Location);
    Result.YawDegrees = State.YawDegrees;
    Result.Velocity = FVector_NetQuantize10(State.Velocity);
    Result.RadiusCm = Formation.RadiusCm;
    return Result;
  }

  FVector MakeCenteredFormationOffset(
    const int32 FormationIndex,
    const int32 AgentCount,
    const FCrowdDemoRoundRules& Rules)
  {
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
  EntityQuery.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundProposedMovementFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundPbdCorrectionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoRoundSeparationFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPortalAdmissionFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPassingBandFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoOrcaVelocityFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPositionAssignmentFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPursuitSteeringStateFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddRequirement<FCrowdDemoPursuitGuidanceFragment>(EMassFragmentAccess::ReadWrite);
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
  EntityQuery.ForEachEntityChunk(Context, [&EntityCount, &AgentIds](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      AgentIds.Add(Identities[It].Id);
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
    EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
    {
      const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
      const TArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSimStateFragment>();
      const TArrayView<FCrowdDemoRoundFormationFragment> Formations = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFormationFragment>();
      const TArrayView<FCrowdDemoRoundMoveIntentFragment> Intents = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundMoveIntentFragment>();
      const TArrayView<FCrowdDemoRoundFlowSampleFragment> FlowSamples = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFlowSampleFragment>();
      const TArrayView<FCrowdDemoRoundProposedMovementFragment> ProposedMovements = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundProposedMovementFragment>();
      const TArrayView<FCrowdDemoRoundObstacleConstraintFragment> Constraints = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundObstacleConstraintFragment>();
      const TArrayView<FCrowdDemoRoundPbdCorrectionFragment> PbdCorrections = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundPbdCorrectionFragment>();
      const TArrayView<FCrowdDemoRoundSeparationFragment> Separations = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundSeparationFragment>();
      const TArrayView<FCrowdDemoPortalAdmissionFragment> Admissions = ChunkContext.GetMutableFragmentView<FCrowdDemoPortalAdmissionFragment>();
      const TArrayView<FCrowdDemoPassingBandFragment> Bands = ChunkContext.GetMutableFragmentView<FCrowdDemoPassingBandFragment>();
      const TArrayView<FCrowdDemoOrcaVelocityFragment> OrcaVelocities = ChunkContext.GetMutableFragmentView<FCrowdDemoOrcaVelocityFragment>();
      const auto PositionAssignments = ChunkContext.GetMutableFragmentView<FCrowdDemoPositionAssignmentFragment>();
      const auto PursuitSteering = ChunkContext.GetMutableFragmentView<FCrowdDemoPursuitSteeringStateFragment>();
      const auto PursuitGuidance = ChunkContext.GetMutableFragmentView<FCrowdDemoPursuitGuidanceFragment>();
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
        if (IsRoundFlowScenario(DuePlan.Rules.Scenario)
          && (DuePlan.Revision == 1 || IsTrafficScenario(DuePlan.Rules.Scenario)))
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
        Intents[It] = FCrowdDemoRoundMoveIntentFragment();
        FlowSamples[It] = FCrowdDemoRoundFlowSampleFragment();
        ProposedMovements[It] = FCrowdDemoRoundProposedMovementFragment();
        Constraints[It] = FCrowdDemoRoundObstacleConstraintFragment();
        PbdCorrections[It] = FCrowdDemoRoundPbdCorrectionFragment();
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
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        if (States[It].bInitialized)
        {
          StatesOut.Add(MakeRoundAgentState(Identities[It], Formations[It], States[It]));
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
          || !FMath::IsNearlyEqual((*Local)->RadiusCm, ServerAgent.RadiusCm, 0.01f))
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
      }
    }
    else
    {
      BeforeCorrection = GatherStates();
    }
    Pipeline->RecordCorrectionComparisonAndApplied(BeforeCorrection, Correction, BoundaryTime);
    bool bNeedsAuthoritativeState = RollbackSnapshot == nullptr
      && !bValidSoftPressureRollback;
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
        }
        if (bNeedsAuthoritativeState)
        {
          if (const FCrowdDemoRoundAgentState* const* Corrected = CorrectionById.Find(Identities[It].Id))
          {
            States[It].Location = FVector((*Corrected)->Location);
            States[It].Velocity = FVector((*Corrected)->Velocity);
            States[It].YawDegrees = (*Corrected)->YawDegrees;
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
      for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
      {
        if (const FCrowdDemoRoundAgentState* const* Corrected = ResultById.Find(Identities[It].Id))
        {
          States[It].Location = FVector((*Corrected)->Location);
          States[It].Velocity = FVector((*Corrected)->Velocity);
          States[It].YawDegrees = (*Corrected)->YawDegrees;
          States[It].SimulatedServerTimeSeconds = Result.EndServerTimeSeconds;
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
  Pipeline->EnsureSharedFlowField(Pipeline->GetRules().FlowFieldConfig);
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
  TArray<FSf3AgentHashRecord> HashRecords;
  TArray<FCrowdDemoFlowReachabilityStageSample> ReachabilitySamples;
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const TConstArrayView<FCrowdDemoRoundSimStateFragment> States = ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const TConstArrayView<FCrowdDemoPortalAdmissionFragment> Admissions = ChunkContext.GetFragmentView<FCrowdDemoPortalAdmissionFragment>();
    const TArrayView<FCrowdDemoRoundMoveIntentFragment> Intents = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundMoveIntentFragment>();
    const TArrayView<FCrowdDemoRoundFlowSampleFragment> FlowSamples = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundFlowSampleFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdDemoSharedFlowField* Field = &Pipeline->GetSharedFlowField();
      FVector Goal = FVector(Rules.FlowFieldConfig.GoalLocation);
      if (IsTrafficScenario(Rules.Scenario))
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
      const FCrowdDemoSharedFlowSample Sample = FCrowdDemoSharedFlowFieldKernel::Sample(*Field, States[It].Location);
      FCrowdDemoRoundFlowSampleFragment& FlowSample = FlowSamples[It];
      FlowSample.CellIndex = Sample.CellIndex;
      FlowSample.StableCellKey = Sample.StableCellKey;
      FlowSample.Status = Sample.Status;
      FlowSample.FlowDirection = Sample.FlowDirection;
      FlowSample.IntegrationCost = Sample.IntegrationCost;
      FlowSample.bBlocked = Sample.bBlocked;
      FlowSample.bUnreachable = Sample.bUnreachable;
      FlowSample.bRecoveredFromRasterMismatch = Sample.bRecoveredFromRasterMismatch;

      FCrowdDemoRoundMoveIntentFragment& Intent = Intents[It];
      const bool bReachedGoal = FVector::DistSquared2D(
        States[It].Location,
        Goal) <= FMath::Square(140.0f);
      Intent.PreferredDirection = bReachedGoal ? FVector::ZeroVector : Sample.FlowDirection;
      Intent.DesiredLocation = Goal;
      Intent.DesiredVelocity = bReachedGoal || Sample.bUnreachable
        ? FVector::ZeroVector
        : Sample.FlowDirection * Rules.MaxSpeedCmPerSecond;
      Intent.DesiredYawDegrees = Intent.DesiredVelocity.IsNearlyZero()
        ? States[It].YawDegrees
        : Intent.DesiredVelocity.Rotation().Yaw;
      Intent.PlanRevision = Pipeline->GetCurrentPlanRevision();
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
      }
      ++AgentCount;
    }
  });
  Pipeline->RecordSf3FlowReachabilityStage(
    ECrowdDemoFlowReachabilityStage::StepStart, ReachabilitySamples);
  if (Pipeline->IsSf3DeterminismDiagnosticEnabled())
  {
    TArray<int32> Keys;
    const uint32 Hash = HashSf3AgentRecords(Pipeline->GetCurrentFixedStepIndex(), HashRecords, Keys);
    Pipeline->RecordSf3StageHash(
      ECrowdDemoSf3DeterminismStage::FlowPreferredVelocity, Hash, HashRecords.Num(), Keys);
  }
  Pipeline->LogStageOnce(TEXT("03_flow_preferred_velocity"), AgentCount);
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
  if (!Pipeline || Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundPursuitPositioning) return;
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
  EntityQuery.AddRequirement<FCrowdDemoRoundProposedMovementFragment>(EMassFragmentAccess::ReadWrite);
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
    const TArrayView<FCrowdDemoRoundProposedMovementFragment> Proposed = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundProposedMovementFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      Proposed[It].StartLocation = States[It].Location;
      FVector ProposedVelocity = Intents[It].DesiredVelocity;
      if (IsTrafficScenario(Pipeline->GetRules().Scenario))
      {
        ProposedVelocity = OrcaVelocities[It].Velocity.GetClampedToMaxSize2D(
          Pipeline->GetRules().MaxSpeedCmPerSecond);
      }
      Proposed[It].ProposedVelocity = ProposedVelocity;
      Proposed[It].ProposedLocation = States[It].Location + ProposedVelocity * FixedStep;
      Proposed[It].ProposedLocation.Z = States[It].Location.Z;
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
  EntityQuery.AddRequirement<FCrowdDemoParticlePropertiesFragment>(EMassFragmentAccess::ReadOnly);
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
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Proposed = ChunkContext.GetFragmentView<FCrowdDemoRoundProposedMovementFragment>();
    const auto Properties = ChunkContext.GetFragmentView<FCrowdDemoParticlePropertiesFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoParticleConstraintAgent& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Identities[It].Id;
      Agent.StartPosition = Proposed[It].StartLocation;
      Agent.PredictedPosition = Proposed[It].ProposedLocation;
      Agent.PhysicalRadiusCm = Properties[It].PhysicalRadiusCm;
      Agent.HardSafetyGapCm = Properties[It].HardSafetyGapCm;
      Agent.SoftMarginCm = Properties[It].SoftMarginCm;
      Agent.Mobility = Properties[It].Mobility;
    }
  });

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
  TArray<FCrowdDemoParticleAppliedState> AppliedStates;
  AppliedStates.Reserve(Agents.Num());
  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const auto Identities = ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    const auto Proposed = ChunkContext.GetFragmentView<FCrowdDemoRoundProposedMovementFragment>();
    const auto Outputs = ChunkContext.GetMutableFragmentView<FCrowdDemoRoundParticleConstraintFragment>();
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      FCrowdDemoRoundParticleConstraintFragment& Output = Outputs[It];
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
    }
  });
  FCrowdDemoParticleConstraintSummary AppliedSummary;
  uint32 AppliedStateHash = 2166136261u;
  FCrowdDemoParticleConstraintKernel::EvaluateAppliedState(
    Agents, AppliedStates, Environment, AppliedSummary, AppliedStateHash);
  if (Summary.bValid)
  {
    AppliedSummary.PressureInfluencedAgentCount = Summary.PressureInfluencedAgentCount;
    AppliedSummary.FirstInfluencedIterationMax = Summary.FirstInfluencedIterationMax;
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

      FCrowdDemoRoundFlowAgentSample& Metric = MetricSamples.AddDefaulted_GetRef();
      Metric.AgentId = Identities[It].Id;
      Metric.Location = State.Location;
      Metric.Velocity = State.Velocity;
      const FCrowdDemoSharedFlowSample FinalFlowSample =
        FCrowdDemoSharedFlowFieldKernel::Sample(Pipeline->GetSharedFlowField(), State.Location);
      Metric.bUnreachable = FinalFlowSample.Status != ECrowdDemoFlowLocationStatus::Reachable;
      FCrowdDemoSharedFlowFieldConfig PhysicalObstacleConfig = Pipeline->GetRules().FlowFieldConfig;
      if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
        PhysicalObstacleConfig.AgentInflateCm = ParticleProperties[It].PhysicalRadiusCm
          + ParticleProperties[It].HardSafetyGapCm;
      Metric.bPenetrating = (Pipeline->GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
          && Constraints[It].bPenetrating)
        || FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(PhysicalObstacleConfig, State.Location);
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
        FCrowdDemoParticleAppliedRoundSimState& Applied =
          ParticleAppliedStates.AddDefaulted_GetRef();
        Applied.AgentId = Identities[It].Id;
        Applied.LifecycleSerial = Identities[It].LifecycleSerial;
        Applied.Position = State.Location;
        Applied.Velocity = State.Velocity;
        Applied.YawDegrees = State.YawDegrees;
        Applied.RadiusCm = Formations[It].RadiusCm;
        Applied.bInitialized = State.bInitialized;
      }
    }
  });
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
    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      if (SimStates[It].bInitialized)
      {
        States.Add(MakeRoundAgentState(Identities[It], Formations[It], SimStates[It]));
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
      Result.ParticleMetrics.SoftViolatingPairCount = Particle.SoftViolatingPairCount;
      Result.ParticleMetrics.SoftErrorCmP50 = Particle.SoftErrorCmP50;
      Result.ParticleMetrics.SoftErrorCmP95 = Particle.SoftErrorCmP95;
      Result.ParticleMetrics.SoftErrorCmMax = Particle.SoftErrorCmMax;
      Result.ParticleMetrics.HardPairViolationCount = Particle.HardPairViolationCount;
      Result.ParticleMetrics.SweptPairViolationCount = Particle.SweptPairViolationCount;
      Result.ParticleMetrics.PressureInfluencedAgentCount = Particle.PressureInfluencedAgentCount;
      Result.ParticleMetrics.FirstInfluencedIterationMax = Particle.FirstInfluencedIterationMax;
      Result.ParticleMetrics.ParticleCorrectedAgentCount = Particle.CorrectedAgentCount;
      Result.ParticleMetrics.MaxAgentCorrectionCm = Particle.MaxAgentCorrectionCm;
      Result.ParticleMetrics.ObstaclePenetrationCount = Particle.ObstaclePenetrationCount;
      Result.ParticleMetrics.BoundsViolationCount = Particle.BoundsViolationCount;
      Result.ParticleMetrics.ParticleInvalidStepCount = Pipeline->GetParticleInvalidStepCount();
      Result.ParticleMetrics.ParticleGlobalFallbackStepCount = Pipeline->GetParticleGlobalFallbackStepCount();
      Result.ParticleMetrics.SettlingSteps = Pipeline->GetParticleSettlingSteps();
      Result.ParticleMetrics.ParticleSolverMsP95 = Pipeline->GetParticleSolverMsP95();
      Result.ParticleMetrics.ParticleCandidateHash = Pipeline->GetParticleCandidateStateHash();
      Result.ParticleMetrics.ParticleAppliedStateHash = Pipeline->GetParticleAppliedStateHash();
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
    }
    if (IsTrafficScenario(Pipeline->GetRules().Scenario))
    {
      Result.TrafficMetrics = Pipeline->BuildTrafficMetrics(States);
      Pipeline->RecordSf3CompletedRoundHash(Result.TrafficMetrics.AgentStateHash);
    }
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
  TargetFactApplyProcessor = MakeDynamicRoundProcessor<UCrowdDemoRoundTargetFactApplyProcessor>(*this, Owner, EntityManager);
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
    if (Pipeline->GetRules().Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning)
    {
      TargetFactApplyProcessor->CallExecute(EntityManager, Context);
    }
    SharedFlowFieldBuildProcessor->CallExecute(EntityManager, Context);
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
