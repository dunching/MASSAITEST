#include "CrowdDemoMassSubsystem.h"

#include "Mass/CrowdDemoMassReplication.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoClientVisualMassProcessor.h"
#include "Mass/CrowdDemoRoundSimProcessors.h"
#include "Mass/CrowdDemoProjectileAdapters.h"
#include "MassCrowdRuntimeFragments.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplate.h"
#include "MassEntityTemplateRegistry.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassMovementFragments.h"
#include "MassReplicationFragments.h"
#include "MassReplicationSubsystem.h"
#include "MassSpawnerSubsystem.h"
#include "MassSpawnerTypes.h"
#include "MassSimulationSubsystem.h"
#include "Arena/CrowdDemoTargetActor.h"
#include "HAL/IConsoleManager.h"

namespace
{
  constexpr uint32 CrowdDemoStableProviderId = 1;
  TAutoConsoleVariable<int32> CVarCrowdDemoProjectileCapacity(
    TEXT("crowd.ProjectileMassCapacity"),
    1024,
    TEXT("Maximum Mass projectile entities available to the Demo host."),
    ECVF_Default);

  FMassEntityTemplateID GetCrowdDemoReplicationTemplateID()
  {
    return FMassEntityTemplateIDFactory::Make(FGuid(0x6d617373, 0x61696372, 0x6f776464, 0x656d6f31));
  }

  void AddCrowdDemoTemplateFragments(FMassEntityTemplateData& TemplateData, const FMassEntityTemplateID TemplateID)
  {
    TemplateData.SetTemplateName(TEXT("CrowdDemoMassReplicatedAgent"));
    TemplateData.AddTag<FCrowdDemoMassAgentTag>();
    TemplateData.AddTag<FCrowdMassAgentTag>();
    TemplateData.AddFragment<FCrowdMassAgentFragment>();
    TemplateData.AddFragment<FCrowdMassBehaviorFragment>();
    TemplateData.AddFragment<FCrowdMassSimulationStateFragment>();
    TemplateData.AddFragment<FCrowdMassPropertiesFragment>();
    TemplateData.AddFragment<FCrowdMassFacingFragment>();
    TemplateData.AddFragment<FCrowdMassMovementOutputFragment>();
    TemplateData.AddFragment<FCrowdDemoMassIdentityFragment>();
    TemplateData.AddFragment<FCrowdDemoMassStatsFragment>();
    TemplateData.AddFragment<FCrowdDemoBusinessStateFragment>();
    TemplateData.AddFragment<FCrowdDemoRangedAttackFragment>();
    TemplateData.AddFragment<FCrowdDemoReactiveMotionFragment>();
    TemplateData.AddFragment<FCrowdDemoHitFlashFragment>();
    TemplateData.AddFragment<FCrowdDemoMassMovementFragment>();
    TemplateData.AddFragment<FCrowdDemoRoundSimStateFragment>();
    TemplateData.AddFragment<FCrowdDemoRoundFormationFragment>();
    TemplateData.AddFragment<FCrowdDemoRoundFlowSampleFragment>();
    TemplateData.AddFragment<FCrowdDemoParticlePropertiesFragment>();
    TemplateData.AddFragment<FCrowdDemoMassVisualFragment>();
    TemplateData.AddFragment<FCrowdDemoClientAuthorityFragment>();
    TemplateData.AddFragment<FCrowdDemoClientVisualOffsetFragment>();
    TemplateData.AddFragment<FTransformFragment>();
    TemplateData.AddFragment<FMassVelocityFragment>();
    TemplateData.AddFragment<FMassDesiredMovementFragment>();
    TemplateData.AddFragment<FReplicationTemplateIDFragment>();
    TemplateData.AddFragment<FMassNetworkIDFragment>();
    TemplateData.AddFragment<FMassReplicatedAgentFragment>();
    TemplateData.AddFragment<FMassReplicationViewerInfoFragment>();
    TemplateData.AddFragment<FMassReplicationLODFragment>();
    TemplateData.AddFragment<FMassReplicationGridCellLocationFragment>();

    FReplicationTemplateIDFragment& TemplateIDFragment = TemplateData.AddFragment_GetRef<FReplicationTemplateIDFragment>();
    TemplateIDFragment.ID = TemplateID;
  }

  bool EnsureCrowdDemoReplicationTemplate(UWorld& World, FMassEntityManager& EntityManager)
  {
    UMassSpawnerSubsystem* SpawnerSubsystem = World.GetSubsystem<UMassSpawnerSubsystem>();
    UMassReplicationSubsystem* ReplicationSubsystem = World.GetSubsystem<UMassReplicationSubsystem>();
    if (!SpawnerSubsystem || !ReplicationSubsystem)
    {
      return false;
    }

    const FMassEntityTemplateID TemplateID = GetCrowdDemoReplicationTemplateID();
    if (SpawnerSubsystem->GetMassEntityTemplate(TemplateID))
    {
      return true;
    }

    TAlignedBytes<sizeof(FMassReplicationParameters), alignof(FMassReplicationParameters)> ReplicationParamsStorage;
    FMemory::Memzero(&ReplicationParamsStorage, sizeof(ReplicationParamsStorage));
    FMassReplicationParameters& ReplicationParams = *reinterpret_cast<FMassReplicationParameters*>(&ReplicationParamsStorage);
    ReplicationParams.BubbleInfoClass = ACrowdDemoMassClientBubbleInfo::StaticClass();
    ReplicationParams.ReplicatorClass = UCrowdDemoMassReplicator::StaticClass();
    ReplicationParams.BufferHysteresisOnDistancePercentage = 10.0f;
    ReplicationParams.LODDistance[EMassLOD::High] = 0.0f;
    ReplicationParams.LODDistance[EMassLOD::Medium] = 8000.0f;
    ReplicationParams.LODDistance[EMassLOD::Low] = 12000.0f;
    ReplicationParams.LODDistance[EMassLOD::Off] = 20000.0f;
    ReplicationParams.LODMaxCount[EMassLOD::High] = 10000;
    ReplicationParams.LODMaxCount[EMassLOD::Medium] = 10000;
    ReplicationParams.LODMaxCount[EMassLOD::Low] = 10000;
    ReplicationParams.LODMaxCount[EMassLOD::Off] = 0;
    ReplicationParams.LODMaxCountPerViewer[EMassLOD::High] = 10000;
    ReplicationParams.LODMaxCountPerViewer[EMassLOD::Medium] = 10000;
    ReplicationParams.LODMaxCountPerViewer[EMassLOD::Low] = 10000;
    ReplicationParams.LODMaxCountPerViewer[EMassLOD::Off] = 0;
    ReplicationParams.UpdateInterval[EMassLOD::High] = 0.05f;
    ReplicationParams.UpdateInterval[EMassLOD::Medium] = 0.08f;
    ReplicationParams.UpdateInterval[EMassLOD::Low] = 0.12f;
    ReplicationParams.UpdateInterval[EMassLOD::Off] = 0.5f;

    ReplicationSubsystem->RegisterBubbleInfoClass(ACrowdDemoMassClientBubbleInfo::StaticClass());

    FMassEntityTemplateData TemplateData;
    AddCrowdDemoTemplateFragments(TemplateData, TemplateID);
    TemplateData.AddConstSharedFragment(EntityManager.GetOrCreateConstSharedFragment(ReplicationParams));
    FMassReplicationSharedFragment ReplicationSharedFragment;
    ReplicationSharedFragment.LODCalculator.Initialize(
      ReplicationParams.LODDistance,
      ReplicationParams.BufferHysteresisOnDistancePercentage / 100.0f,
      ReplicationParams.LODMaxCountPerViewer);
    ReplicationSharedFragment.BubbleInfoClassHandle = ReplicationSubsystem->GetBubbleInfoClassHandle(ReplicationParams.BubbleInfoClass);
    ReplicationSharedFragment.CachedReplicator = ReplicationParams.ReplicatorClass.GetDefaultObject();
    TemplateData.AddSharedFragment(EntityManager.GetOrCreateSharedFragment(ReplicationSharedFragment));

    SpawnerSubsystem->GetMutableTemplateRegistryInstance().FindOrAddTemplate(TemplateID, MoveTemp(TemplateData));
    UE_LOG(LogTemp, Display, TEXT("CrowdDemoMass: replication_template_registered source=MassClientBubble"));
    return true;
  }
}

void UCrowdDemoMassSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
  Collection.InitializeDependency<UMassEntitySubsystem>();
  Collection.InitializeDependency<UMassSpawnerSubsystem>();
  Collection.InitializeDependency<UMassReplicationSubsystem>();
  Collection.InitializeDependency<UMassSimulationSubsystem>();
  Super::Initialize(Collection);

  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (World && EntitySubsystem)
  {
    EnsureCrowdDemoReplicationTemplate(*World, EntitySubsystem->GetMutableEntityManager());
  }
}

bool UCrowdDemoMassSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
  const UWorld* World = Outer ? Outer->GetWorld() : nullptr;
  return World != nullptr && World->IsGameWorld();
}

void UCrowdDemoMassSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
  Super::OnWorldBeginPlay(InWorld);
  if (InWorld.GetNetMode() == NM_Client)
  {
    UMassEntitySubsystem* EntitySubsystem =
      InWorld.GetSubsystem<UMassEntitySubsystem>();
    const int32 MaxProjectilePoolCapacity = FMath::Clamp(
      CVarCrowdDemoProjectileCapacity.GetValueOnGameThread(),
      1, 65536);
    checkf(EntitySubsystem
        && ProjectileStore.EnsureCapacity(
          EntitySubsystem->GetMutableEntityManager(),
          MaxProjectilePoolCapacity, MaxProjectilePoolCapacity),
      TEXT("Client projectile prediction Mass pool initialization failed"));
  }
  RegisterRoundSimProcessors();
}

void UCrowdDemoMassSubsystem::Deinitialize()
{
  UnregisterRoundSimProcessors();
  TargetActor.Reset();
  TrackedAgents.Reset();
  ProjectileStore.ResetTracking();
  Super::Deinitialize();
}

void UCrowdDemoMassSubsystem::RegisterRoundSimProcessors()
{
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  UMassSimulationSubsystem* SimulationSubsystem = World ? World->GetSubsystem<UMassSimulationSubsystem>() : nullptr;
  if (!EntitySubsystem || !SimulationSubsystem || RoundSimPipelineProcessor || ClientVisualProcessor)
  {
    return;
  }

  const TSharedRef<FMassEntityManager> EntityManager = EntitySubsystem->GetMutableEntityManager().AsShared();
  RoundSimPipelineProcessor = NewObject<UCrowdDemoRoundSimFixedStepPipelineProcessor>(this);
  RoundSimPipelineProcessor->CallInitialize(this, EntityManager);
  SimulationSubsystem->RegisterDynamicProcessor(*RoundSimPipelineProcessor);

  const bool bPublicPresentationOwnsClient =
    FParse::Param(FCommandLine::Get(),
      TEXT("CrowdDemoFriendlyLogisticsSmall"))
    || FParse::Param(FCommandLine::Get(),
      TEXT("CrowdDemoMixedSandbox"))
    || FParse::Param(FCommandLine::Get(),
      TEXT("CrowdDemoMixedCombatIntegration"))
    || FParse::Param(FCommandLine::Get(),
      TEXT("CrowdDemoContinuousLifecycle"));
  if (World->GetNetMode() != NM_DedicatedServer
    && !bPublicPresentationOwnsClient)
  {
    ClientVisualProcessor = NewObject<UCrowdDemoClientVisualMassProcessor>(this);
    ClientVisualProcessor->CallInitialize(this, EntityManager);
    SimulationSubsystem->RegisterDynamicProcessor(*ClientVisualProcessor);
  }

  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoMass: round_processors_registered pipeline=1 client_visual=%d public_presentation=%d net_mode=%d source=MassSubsystem"),
    ClientVisualProcessor ? 1 : 0,
    bPublicPresentationOwnsClient ? 1 : 0,
    static_cast<int32>(World->GetNetMode()));
}

void UCrowdDemoMassSubsystem::UnregisterRoundSimProcessors()
{
  UWorld* World = GetWorld();
  UMassSimulationSubsystem* SimulationSubsystem = World ? World->GetSubsystem<UMassSimulationSubsystem>() : nullptr;
  if (SimulationSubsystem)
  {
    if (ClientVisualProcessor)
    {
      SimulationSubsystem->UnregisterDynamicProcessor(*ClientVisualProcessor);
    }
    if (RoundSimPipelineProcessor)
    {
      SimulationSubsystem->UnregisterDynamicProcessor(*RoundSimPipelineProcessor);
    }
  }
  ClientVisualProcessor = nullptr;
  RoundSimPipelineProcessor = nullptr;
}

void UCrowdDemoMassSubsystem::SetTargetActor(AActor* InTargetActor)
{
  TargetActor = InTargetActor;
  if (ACrowdDemoTargetActor* CrowdTargetActor = Cast<ACrowdDemoTargetActor>(TargetActor.Get()))
  {
    CrowdTargetActor->ConfigureScenario(CurrentScenario);
  }
}

AActor* UCrowdDemoMassSubsystem::GetTargetActor() const
{
  return TargetActor.Get();
}

void UCrowdDemoMassSubsystem::SetScenario(const ECrowdDemoScenario InScenario)
{
  const int32 ScenarioValue = static_cast<int32>(InScenario);
  CurrentScenario = ScenarioValue == static_cast<int32>(ECrowdDemoScenario::SimRoundObstacle)
      || ScenarioValue == static_cast<int32>(ECrowdDemoScenario::SimRoundSoftPressure)
    ? InScenario
    : ECrowdDemoScenario::SimRoundObstacle;
  if (CurrentScenario != InScenario)
  {
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoScenario: unsupported=%d fallback=SimRoundObstacle source=MassSubsystem"),
      ScenarioValue);
  }
  if (ACrowdDemoTargetActor* CrowdTargetActor = Cast<ACrowdDemoTargetActor>(TargetActor.Get()))
  {
    CrowdTargetActor->ConfigureScenario(CurrentScenario);
  }

  UE_LOG(LogTemp, Display, TEXT("CrowdDemoScenario: scenario=%d source=MassSubsystem"), static_cast<int32>(CurrentScenario));
}

FCrowdDemoMassSpawnResult UCrowdDemoMassSubsystem::SpawnAgents(const int32 AgentCount)
{
  FCrowdDemoMassSpawnResult Result;
  Result.RequestedAgents = FMath::Max(0, AgentCount);

  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  UMassSpawnerSubsystem* SpawnerSubsystem = World ? World->GetSubsystem<UMassSpawnerSubsystem>() : nullptr;
  if (!World || !EntitySubsystem || !SpawnerSubsystem || World->GetNetMode() == NM_Client || Result.RequestedAgents <= 0)
  {
    UE_LOG(LogTemp, Warning, TEXT("CrowdDemoMass: spawn_skipped requested=%d has_world=%d has_mass_entity_subsystem=%d has_spawner_subsystem=%d net_mode=%d"),
      Result.RequestedAgents,
      World ? 1 : 0,
      EntitySubsystem ? 1 : 0,
      SpawnerSubsystem ? 1 : 0,
      World ? static_cast<int32>(World->GetNetMode()) : -1);
    return Result;
  }

  DestroyTrackedAgents();

  FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
  if (!EnsureCrowdDemoReplicationTemplate(*World, EntityManager))
  {
    UE_LOG(LogTemp, Warning, TEXT("CrowdDemoMass: spawn_skipped missing_replication_template"));
    return Result;
  }

  const FMassEntityTemplate* Template = SpawnerSubsystem->GetMassEntityTemplate(GetCrowdDemoReplicationTemplateID());
  if (!Template)
  {
    UE_LOG(LogTemp, Warning, TEXT("CrowdDemoMass: spawn_skipped template_lookup_failed"));
    return Result;
  }

  {
    const TSharedPtr<FMassEntityManager::FEntityCreationContext> CreationContext =
      SpawnerSubsystem->SpawnEntities(*Template, Result.RequestedAgents, TrackedAgents);
    for (int32 Index = 0; Index < TrackedAgents.Num(); ++Index)
    {
      InitializeAgentFragments(EntityManager, TrackedAgents[Index], Index, Result.RequestedAgents);
    }
  }
  const int32 MaxProjectilePoolCapacity = FMath::Clamp(
    CVarCrowdDemoProjectileCapacity.GetValueOnGameThread(),
    1, 65536);
  if (!ProjectileStore.EnsureCapacity(
      EntityManager, MaxProjectilePoolCapacity,
      MaxProjectilePoolCapacity))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoMass: spawn_failed projectile_pool_capacity=%d"),
      MaxProjectilePoolCapacity);
    DestroyTrackedAgents();
    return Result;
  }
  Result.SpawnedAgents = TrackedAgents.Num();
  Result.AliveAgents = GetAliveAgentCount();
  Result.bUsedMassEntitySubsystem = true;

  UE_LOG(LogTemp, Display, TEXT("CrowdDemoMass: START agents=%d alive=%d requested=%d source=UMassEntitySubsystem"),
    Result.SpawnedAgents,
    Result.AliveAgents,
    Result.RequestedAgents);

  return Result;
}

int32 UCrowdDemoMassSubsystem::GetTrackedAgentCount() const
{
  return TrackedAgents.Num();
}

int32 UCrowdDemoMassSubsystem::GetAliveAgentCount() const
{
  const UWorld* World = GetWorld();
  const UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!EntitySubsystem)
  {
    return 0;
  }

  const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
  int32 AliveCount = 0;
  for (const FMassEntityHandle Entity : TrackedAgents)
  {
    if (!EntityManager.IsEntityValid(Entity))
    {
      continue;
    }

    const FCrowdDemoMassStatsFragment* Stats = EntityManager.GetFragmentDataPtr<FCrowdDemoMassStatsFragment>(Entity);
    if (Stats && Stats->bAlive && Stats->Health > 0.0f)
    {
      ++AliveCount;
    }
  }
  return AliveCount;
}

int32 UCrowdDemoMassSubsystem::BuildVisualSnapshot(TArray<FCrowdDemoEntityState>& OutSnapshot, const float ServerTimeSeconds) const
{
  OutSnapshot.Reset(TrackedAgents.Num());

  const UWorld* World = GetWorld();
  const UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!EntitySubsystem)
  {
    return 0;
  }

  const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
  for (const FMassEntityHandle Entity : TrackedAgents)
  {
    if (!EntityManager.IsEntityValid(Entity))
    {
      continue;
    }

    const FCrowdDemoMassIdentityFragment* Identity = EntityManager.GetFragmentDataPtr<FCrowdDemoMassIdentityFragment>(Entity);
    const FCrowdDemoMassMovementFragment* Movement = EntityManager.GetFragmentDataPtr<FCrowdDemoMassMovementFragment>(Entity);
    const FCrowdDemoMassVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FCrowdDemoMassVisualFragment>(Entity);
    const FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity);
    if (!Identity || !Movement || !Visual || !Transform)
    {
      continue;
    }

    FCrowdDemoEntityState& State = OutSnapshot.AddDefaulted_GetRef();
    State.Id = Identity->VisualId;
    State.LifecycleSerial = Identity->LifecycleSerial;
    State.Location = Transform->GetTransform().GetLocation();
    State.YawCentidegrees = static_cast<int16>(FMath::RoundToInt(Movement->YawDegrees * 100.0f));
    State.AnimState = Visual->AnimState;
    State.VatClipIndex = Visual->VatClipIndex;
    State.VatPhaseByte = Visual->VatPhaseByte;
    State.VatPlayRateByte = Visual->VatPlayRateByte;
    State.ServerTimeSeconds = ServerTimeSeconds;
  }

  return OutSnapshot.Num();
}

int32 UCrowdDemoMassSubsystem::BuildRoundAgentStates(TArray<FCrowdDemoRoundAgentState>& OutStates) const
{
  OutStates.Reset(TrackedAgents.Num());

  const UWorld* World = GetWorld();
  const UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!EntitySubsystem)
  {
    return 0;
  }

  const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
  for (const FMassEntityHandle Entity : TrackedAgents)
  {
    if (!EntityManager.IsEntityValid(Entity))
    {
      continue;
    }

    const FCrowdDemoMassIdentityFragment* Identity = EntityManager.GetFragmentDataPtr<FCrowdDemoMassIdentityFragment>(Entity);
    const FCrowdDemoMassMovementFragment* Movement = EntityManager.GetFragmentDataPtr<FCrowdDemoMassMovementFragment>(Entity);
    const FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity);
    const FMassVelocityFragment* Velocity = EntityManager.GetFragmentDataPtr<FMassVelocityFragment>(Entity);
    const auto* Stats = EntityManager.GetFragmentDataPtr<FCrowdDemoMassStatsFragment>(Entity);
    const auto* Business = EntityManager.GetFragmentDataPtr<FCrowdDemoBusinessStateFragment>(Entity);
    const auto* Attack = EntityManager.GetFragmentDataPtr<FCrowdDemoRangedAttackFragment>(Entity);
    const auto* Reactive = EntityManager.GetFragmentDataPtr<FCrowdDemoReactiveMotionFragment>(Entity);
    const auto* HitFlash = EntityManager.GetFragmentDataPtr<FCrowdDemoHitFlashFragment>(Entity);
    const auto* Visual = EntityManager.GetFragmentDataPtr<FCrowdDemoMassVisualFragment>(Entity);
    if (!Identity || !Movement || !Transform || !Velocity || !Stats || !Business
      || !Attack || !Reactive || !HitFlash || !Visual)
    {
      continue;
    }

    FCrowdDemoRoundAgentState& State = OutStates.AddDefaulted_GetRef();
    State.AgentId = Identity->Id;
    State.LifecycleSerial = Identity->LifecycleSerial;
    State.Location = FVector_NetQuantize10(Transform->GetTransform().GetLocation());
    State.Velocity = FVector_NetQuantize10(Velocity->Value);
    State.YawDegrees = Movement->YawDegrees;
    State.RadiusCm = Movement->ContactRadiusCm;
    State.Combat.Health = Stats->Health;
    State.Combat.MaxHealth = Stats->MaxHealth;
    State.Combat.LifecycleState = Stats->LifecycleState;
    State.Combat.bAlive = Stats->bAlive ? 1 : 0;
    State.Combat.BusinessState = Business->State;
    State.Combat.BusinessStateRevision = Business->StateRevision;
    State.Combat.BusinessStateEnterFixedStep = Business->StateEnterFixedStep;
    State.Combat.TargetAgentId = Business->TargetAgentId;
    State.Combat.TargetLifecycleSerial = Business->TargetLifecycleSerial;
    State.Combat.LastConsumedHitEventId = Business->LastConsumedHitEventId;
    State.Combat.AttackPhase = Attack->Phase;
    State.Combat.AttackPhaseEnterFixedStep = Attack->PhaseEnterFixedStep;
    State.Combat.CooldownEndFixedStep = Attack->CooldownEndFixedStep;
    State.Combat.LockedTargetAgentId = Attack->LockedTargetAgentId;
    State.Combat.LockedTargetLifecycleSerial = Attack->LockedTargetLifecycleSerial;
    State.Combat.LockedTargetLocation = FVector_NetQuantize10(Attack->LockedTargetLocation);
    State.Combat.FireSequence = Attack->FireSequence;
    State.Combat.bFireRequestIssued = Attack->bFireRequestIssued ? 1 : 0;
    State.Combat.ReactiveMode = Reactive->Mode;
    State.Combat.HorizontalReactiveVelocity = FVector_NetQuantize10(Reactive->HorizontalVelocity);
    State.Combat.VerticalReactiveVelocityCmps = Reactive->VerticalVelocityCmps;
    State.Combat.ReactiveStartFixedStep = Reactive->StartFixedStep;
    State.Combat.ReactiveEndFixedStep = Reactive->EndFixedStep;
    State.Combat.ReactiveRevision = Reactive->ReactiveRevision;
    State.Combat.RestoreBusinessState = Reactive->RestoreBusinessState;
    State.Combat.ApexCount = Reactive->ApexCount;
    State.Combat.LandingCount = Reactive->LandingCount;
    State.Combat.HitFlashRevision = HitFlash->FlashRevision;
    State.Combat.HitFlashStartServerTimeSeconds = HitFlash->StartServerTimeSeconds;
    State.Combat.HitFlashDurationSeconds = HitFlash->DurationSeconds;
    State.Combat.HitFlashProfileKey = HitFlash->ProfileKey;
    State.Combat.HitFlashPeakIntensity = HitFlash->PeakIntensity;
    State.Combat.VisualState = Visual->VisualState;
    State.Combat.VisualRevision = Visual->VisualRevision;
    State.Combat.VisualStateStartServerTimeSeconds = Visual->StateStartServerTimeSeconds;
    State.Combat.VisualPhaseSeed = Visual->PhaseSeed;
  }

  OutStates.Sort([](const FCrowdDemoRoundAgentState& A, const FCrowdDemoRoundAgentState& B)
  {
    return A.AgentId < B.AgentId;
  });
  return OutStates.Num();
}

bool UCrowdDemoMassSubsystem::BuildProductBoundarySnapshot(
  const int32 FixedStepIndex,
  const int32 PlanRevision,
  FCrowdMassBoundarySnapshot& OutSnapshot,
  TArray<FCrowdMassCommitTarget>& OutTargets) const
{
  OutSnapshot = {};
  OutTargets.Reset();
  const UWorld* World = GetWorld();
  const UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!IsInGameThread() || !EntitySubsystem
    || FixedStepIndex < 0 || PlanRevision < 0)
    return false;

  const FMassEntityManager& EntityManager =
    EntitySubsystem->GetEntityManager();
  TArray<FCrowdMassBoundaryAgentRecord> Records;
  Records.Reserve(TrackedAgents.Num());
  OutTargets.Reserve(TrackedAgents.Num());
  for (const FMassEntityHandle Entity : TrackedAgents)
  {
    if (!EntityManager.IsEntityValid(Entity))
      return false;
    const auto* Identity =
      EntityManager.GetFragmentDataPtr<FCrowdMassAgentFragment>(Entity);
    const auto* Behavior =
      EntityManager.GetFragmentDataPtr<FCrowdMassBehaviorFragment>(Entity);
    const auto* State =
      EntityManager.GetFragmentDataPtr<FCrowdMassSimulationStateFragment>(
        Entity);
    const auto* Properties =
      EntityManager.GetFragmentDataPtr<FCrowdMassPropertiesFragment>(Entity);
    const auto* Transform =
      EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity);
    const auto* Velocity =
      EntityManager.GetFragmentDataPtr<FMassVelocityFragment>(Entity);
    if (!Identity || !Behavior || !State || !Properties
      || !Transform || !Velocity
      || !Identity->GetStableEntityRef().IsValid())
      return false;
    FCrowdMassBoundaryAgentRecord& Record =
      Records.AddDefaulted_GetRef();
    Record.Identity = *Identity;
    Record.AgentFacts = Behavior->GetAgentFacts(*Identity);
    Record.State = *State;
    Record.State.Position = Transform->GetTransform().GetLocation();
    Record.State.Velocity = Velocity->Value;
    Record.State.YawDegrees =
      Transform->GetTransform().Rotator().Yaw;
    Record.State.bInitialized = true;
    Record.Properties = *Properties;
    FCrowdMassCommitTarget& Target =
      OutTargets.AddDefaulted_GetRef();
    Target.EntityRef = Identity->GetStableEntityRef();
    Target.AgentId = Identity->AgentId;
    Target.LifecycleSerial = Identity->LifecycleSerial;
  }
  FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
    FixedStepIndex, PlanRevision, Records, OutSnapshot);
  OutTargets.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  return OutSnapshot.bValid
    && OutSnapshot.Agents.Num() == TrackedAgents.Num()
    && OutTargets.Num() == TrackedAgents.Num();
}

bool UCrowdDemoMassSubsystem::ApplyProductBoundaryCommit(
  const FCrowdMassCommitPlan& Plan,
  const TConstArrayView<FCrowdMassCommitTarget> Targets)
{
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!IsInGameThread() || !EntitySubsystem
    || !FCrowdMassRuntimeBridge::ValidateCommitTargets(Plan, Targets))
    return false;

  FMassEntityManager& EntityManager =
    EntitySubsystem->GetMutableEntityManager();
  TMap<FCrowdStableEntityRef, FMassEntityHandle> Resolved;
  for (const FMassEntityHandle Entity : TrackedAgents)
  {
    if (!EntityManager.IsEntityValid(Entity))
      return false;
    const auto* Identity =
      EntityManager.GetFragmentDataPtr<FCrowdMassAgentFragment>(Entity);
    if (!Identity || Resolved.Contains(Identity->GetStableEntityRef()))
      return false;
    Resolved.Add(Identity->GetStableEntityRef(), Entity);
  }
  if (Resolved.Num() != Targets.Num())
    return false;
  for (const FCrowdMassCommitTarget& Target : Targets)
  {
    const FMassEntityHandle* Entity = Resolved.Find(Target.EntityRef);
    if (!Entity)
      return false;
    const auto* Identity =
      EntityManager.GetFragmentDataPtr<FCrowdMassAgentFragment>(*Entity);
    if (!Identity || Identity->AgentId != Target.AgentId
      || Identity->LifecycleSerial != Target.LifecycleSerial)
      return false;
  }

  for (const FCrowdMassCommitRecord& Record : Plan.Records)
  {
    const FMassEntityHandle Entity =
      Resolved.FindChecked(Record.EntityRef);
    const FCrowdMassCommitTarget* Target = Targets.FindByPredicate(
      [&Record](const FCrowdMassCommitTarget& Value)
      {
        return Value.EntityRef == Record.EntityRef;
      });
    check(Target);
    auto& RuntimeState =
      EntityManager.GetFragmentDataChecked<
        FCrowdMassSimulationStateFragment>(Entity);
    auto& RuntimeMovement =
      EntityManager.GetFragmentDataChecked<
        FCrowdMassMovementOutputFragment>(Entity);
    checkf(FCrowdMassRuntimeBridge::ApplyMovementToState(
        Record, *Target, RuntimeState, RuntimeMovement),
      TEXT("Product boundary movement failed after target validation"));
    FTransform& Transform =
      EntityManager.GetFragmentDataChecked<FTransformFragment>(
        Entity).GetMutableTransform();
    Transform.SetLocation(Record.Movement.Position);
    Transform.SetRotation(FRotator(
      0.0f, Record.Movement.YawDegrees, 0.0f).Quaternion());
    EntityManager.GetFragmentDataChecked<FMassVelocityFragment>(
      Entity).Value = Record.Movement.Velocity;
    auto& DemoMovement =
      EntityManager.GetFragmentDataChecked<
        FCrowdDemoMassMovementFragment>(Entity);
    DemoMovement.CurrentVelocity = Record.Movement.Velocity;
    DemoMovement.DesiredVelocity = Record.Movement.Velocity;
    DemoMovement.YawDegrees = Record.Movement.YawDegrees;
    auto& DemoState =
      EntityManager.GetFragmentDataChecked<
        FCrowdDemoRoundSimStateFragment>(Entity);
    DemoState.Location = Record.Movement.Position;
    DemoState.Velocity = Record.Movement.Velocity;
    DemoState.YawDegrees = Record.Movement.YawDegrees;
    DemoState.PlanRevision = Record.PlanRevision;
    DemoState.bInitialized = true;
  }
  return true;
}

bool UCrowdDemoMassSubsystem::RecycleTrackedAgent(
  const FCrowdStableEntityRef& EntityRef,
  FCrowdStableEntityRef& OutReplacementRef)
{
  OutReplacementRef = {};
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  UMassSpawnerSubsystem* SpawnerSubsystem =
    World ? World->GetSubsystem<UMassSpawnerSubsystem>() : nullptr;
  if (!IsInGameThread() || !World || !EntitySubsystem
    || !SpawnerSubsystem || !EntityRef.IsValid())
    return false;
  FMassEntityManager& EntityManager =
    EntitySubsystem->GetMutableEntityManager();
  int32 TrackedIndex = INDEX_NONE;
  for (int32 Index = 0; Index < TrackedAgents.Num(); ++Index)
  {
    const FMassEntityHandle Entity = TrackedAgents[Index];
    const auto* Identity = EntityManager.IsEntityValid(Entity)
      ? EntityManager.GetFragmentDataPtr<FCrowdMassAgentFragment>(Entity)
      : nullptr;
    if (Identity && Identity->GetStableEntityRef() == EntityRef)
    {
      TrackedIndex = Index;
      break;
    }
  }
  const FMassEntityTemplate* Template =
    SpawnerSubsystem->GetMassEntityTemplate(
      GetCrowdDemoReplicationTemplateID());
  if (TrackedIndex == INDEX_NONE || !Template)
    return false;

  EntityManager.DestroyEntity(TrackedAgents[TrackedIndex]);
  TArray<FMassEntityHandle> Replacement;
  SpawnerSubsystem->SpawnEntities(*Template, 1, Replacement);
  if (Replacement.Num() != 1
    || !EntityManager.IsEntityValid(Replacement[0]))
    return false;
  InitializeAgentFragments(
    EntityManager, Replacement[0], TrackedIndex, TrackedAgents.Num());
  auto& DemoIdentity =
    EntityManager.GetFragmentDataChecked<
      FCrowdDemoMassIdentityFragment>(Replacement[0]);
  auto& RuntimeIdentity =
    EntityManager.GetFragmentDataChecked<
      FCrowdMassAgentFragment>(Replacement[0]);
  DemoIdentity.LifecycleSerial =
    static_cast<int32>(EntityRef.LifecycleSerial + 1);
  RuntimeIdentity.SetStableEntityRef({
    EntityRef.ProviderId, EntityRef.StableEntityId,
    EntityRef.LifecycleSerial + 1});
  auto& Behavior =
    EntityManager.GetFragmentDataChecked<
      FCrowdMassBehaviorFragment>(Replacement[0]);
  FCrowdAgentFacts Facts = Behavior.GetAgentFacts(RuntimeIdentity);
  Facts.StableEntityRef = RuntimeIdentity.GetStableEntityRef();
  Facts.BusinessTaskRef = {};
  Facts.DerivedBehaviorLabel =
    static_cast<uint32>(ECrowdActiveBehavior::Idle);
  Behavior.SetAgentFacts(Facts);
  TrackedAgents[TrackedIndex] = Replacement[0];
  OutReplacementRef = RuntimeIdentity.GetStableEntityRef();
  return true;
}

void UCrowdDemoMassSubsystem::DestroyTrackedAgents()
{
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!EntitySubsystem)
  {
    TrackedAgents.Reset();
    ProjectileStore.ResetTracking();
    return;
  }

  FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
  for (const FMassEntityHandle Entity : TrackedAgents)
  {
    if (EntityManager.IsEntityValid(Entity))
    {
      EntityManager.DestroyEntity(Entity);
    }
  }
  TrackedAgents.Reset();
  ProjectileStore.DestroyAll(EntityManager);
}

bool UCrowdDemoMassSubsystem::PrepareProjectileCapacity(
  const int32 RequiredCount)
{
  const int32 MaxProjectilePoolCapacity = FMath::Clamp(
    CVarCrowdDemoProjectileCapacity.GetValueOnGameThread(),
    1, 65536);
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem = World
    ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!EntitySubsystem || RequiredCount < 0
    || RequiredCount > MaxProjectilePoolCapacity)
    return false;
  return RequiredCount <= ProjectileStore.GetCapacity();
}

void UCrowdDemoMassSubsystem::ApplyProjectileStates(
  const TConstArrayView<FCrowdProjectileState> Projectiles)
{
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem = World
    ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  check(EntitySubsystem);
  FMassEntityManager& EntityManager =
    EntitySubsystem->GetMutableEntityManager();
  check(ProjectileStore.ValidatePreparedStates(Projectiles));
  ProjectileStore.ApplyValidated(EntityManager, Projectiles);
}

bool UCrowdDemoMassSubsystem::GatherProjectileStates(
  TArray<FCrowdProjectileState>& OutProjectiles) const
{
  OutProjectiles.Reset();
  const UWorld* World = GetWorld();
  const UMassEntitySubsystem* EntitySubsystem = World
    ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!EntitySubsystem)
    return false;
  return ProjectileStore.Gather(
    EntitySubsystem->GetEntityManager(), OutProjectiles);
}

void UCrowdDemoMassSubsystem::ResetProjectileStates()
{
  if (!PrepareProjectileCapacity(0))
    return;
  ApplyProjectileStates({});
}

void UCrowdDemoMassSubsystem::InitializeAgentFragments(
  FMassEntityManager& EntityManager,
  const FMassEntityHandle Entity,
  const int32 AgentIndex,
  const int32 AgentCount) const
{
  const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(FMath::Max(1, AgentCount))));
  const FVector SpawnLocation = MakeSpawnLocation(AgentIndex, AgentCount);

  FCrowdDemoMassIdentityFragment& Identity = EntityManager.GetFragmentDataChecked<FCrowdDemoMassIdentityFragment>(Entity);
  Identity.Id = AgentIndex;
  Identity.VisualId = AgentIndex;
  Identity.LifecycleSerial = 1;

  FCrowdMassAgentFragment& RuntimeIdentity =
    EntityManager.GetFragmentDataChecked<FCrowdMassAgentFragment>(Entity);
  RuntimeIdentity.AgentId = Identity.Id;
  RuntimeIdentity.SetStableEntityRef(FCrowdStableEntityRef{
    CrowdDemoStableProviderId,
    static_cast<uint64>(Identity.Id) + 1,
    static_cast<uint32>(Identity.LifecycleSerial)});

  FCrowdDemoMassStatsFragment& Stats = EntityManager.GetFragmentDataChecked<FCrowdDemoMassStatsFragment>(Entity);
  Stats.Health = 100.0f;
  Stats.MaxHealth = 100.0f;
  Stats.LifecycleState = ECrowdDemoLifecycleState::Alive;
  Stats.bAlive = true;

  FCrowdDemoBusinessStateFragment& Business =
    EntityManager.GetFragmentDataChecked<FCrowdDemoBusinessStateFragment>(Entity);
  Business = FCrowdDemoBusinessStateFragment();
  FCrowdDemoRangedAttackFragment& Attack =
    EntityManager.GetFragmentDataChecked<FCrowdDemoRangedAttackFragment>(Entity);
  Attack = FCrowdDemoRangedAttackFragment();
  FCrowdDemoReactiveMotionFragment& Reactive =
    EntityManager.GetFragmentDataChecked<FCrowdDemoReactiveMotionFragment>(Entity);
  Reactive = FCrowdDemoReactiveMotionFragment();
  FCrowdDemoHitFlashFragment& HitFlash =
    EntityManager.GetFragmentDataChecked<FCrowdDemoHitFlashFragment>(Entity);
  HitFlash = FCrowdDemoHitFlashFragment();

  FCrowdDemoMassMovementFragment& Movement = EntityManager.GetFragmentDataChecked<FCrowdDemoMassMovementFragment>(Entity);
  Movement.ContactRadiusCm = 42.0f;
  Movement.MaxSpeedCmPerSecond = 260.0f;
  Movement.YawDegrees = 0.0f;
  Movement.DesiredVelocity = FVector::ZeroVector;
  Movement.CurrentVelocity = FVector::ZeroVector;

  FCrowdDemoRoundSimStateFragment& RoundSimState = EntityManager.GetFragmentDataChecked<FCrowdDemoRoundSimStateFragment>(Entity);
  RoundSimState.Location = SpawnLocation;
  RoundSimState.Velocity = FVector::ZeroVector;
  RoundSimState.YawDegrees = 0.0f;
  RoundSimState.SimulatedServerTimeSeconds = 0.0f;
  RoundSimState.PlanRevision = 0;
  RoundSimState.bInitialized = false;

  FCrowdMassSimulationStateFragment& RuntimeState =
    EntityManager.GetFragmentDataChecked<FCrowdMassSimulationStateFragment>(Entity);
  RuntimeState.Position = RoundSimState.Location;
  RuntimeState.Velocity = RoundSimState.Velocity;
  RuntimeState.YawDegrees = RoundSimState.YawDegrees;
  RuntimeState.PlanRevision = RoundSimState.PlanRevision;
  RuntimeState.bInitialized = RoundSimState.bInitialized;

  FCrowdDemoRoundFormationFragment& RoundFormation = EntityManager.GetFragmentDataChecked<FCrowdDemoRoundFormationFragment>(Entity);
  RoundFormation.FormationIndex = AgentIndex;
  RoundFormation.LocalOffset = FVector::ZeroVector;
  RoundFormation.RadiusCm = Movement.ContactRadiusCm;
  RoundFormation.bInitialized = false;

  EntityManager.GetFragmentDataChecked<FCrowdDemoRoundFlowSampleFragment>(Entity) = FCrowdDemoRoundFlowSampleFragment();
  FCrowdDemoParticlePropertiesFragment& ParticleProperties =
    EntityManager.GetFragmentDataChecked<FCrowdDemoParticlePropertiesFragment>(Entity);
  const FCrowdDemoParticleProfile DefaultParticleProfile;
  ParticleProperties.PhysicalRadiusCm = DefaultParticleProfile.PhysicalRadiusCm;
  ParticleProperties.HardSafetyGapCm = DefaultParticleProfile.HardSafetyGapCm;
  ParticleProperties.SoftMarginCm = DefaultParticleProfile.SoftMarginCm;
  ParticleProperties.Mobility = DefaultParticleProfile.Mobility;
  FCrowdMassPropertiesFragment& RuntimeProperties =
    EntityManager.GetFragmentDataChecked<FCrowdMassPropertiesFragment>(Entity);
  RuntimeProperties.PhysicalRadiusCm = ParticleProperties.PhysicalRadiusCm;
  RuntimeProperties.HardSafetyGapCm = ParticleProperties.HardSafetyGapCm;
  RuntimeProperties.SoftMarginCm = ParticleProperties.SoftMarginCm;
  RuntimeProperties.Mobility = ParticleProperties.Mobility;
  RuntimeProperties.MaximumSpeedCmps = Movement.MaxSpeedCmPerSecond;
  RuntimeProperties.CapabilityProfileKey = ParticleProperties.CapabilityProfileKey;

  FCrowdAgentFacts RuntimeFacts;
  RuntimeFacts.StableEntityRef = RuntimeIdentity.GetStableEntityRef();
  RuntimeFacts.CapabilitySet.Add(ECrowdCapability::Move);
  RuntimeFacts.CapabilitySet.Add(ECrowdCapability::MoveTo);
  RuntimeFacts.DerivedBehaviorLabel =
    static_cast<uint32>(ECrowdActiveBehavior::Idle);
  RuntimeFacts.MovementProfileKey = RuntimeProperties.CapabilityProfileKey;
  FCrowdMassBehaviorFragment& RuntimeBehavior =
    EntityManager.GetFragmentDataChecked<FCrowdMassBehaviorFragment>(Entity);
  RuntimeBehavior.SetAgentFacts(RuntimeFacts);


  FCrowdDemoMassVisualFragment& Visual = EntityManager.GetFragmentDataChecked<FCrowdDemoMassVisualFragment>(Entity);
  Visual.AnimState = ECrowdDemoAnimState::Idle;
  Visual.VatClipIndex = static_cast<uint8>(ECrowdDemoAnimState::Idle);
  Visual.VatPhaseByte = static_cast<uint8>((AgentIndex * 37) % 255);
  Visual.VatPlayRateByte = 128;
  Visual.VisualState = ECrowdDemoVisualState::Idle;
  Visual.VisualRevision = 0;
  Visual.StateStartServerTimeSeconds = 0.0f;
  Visual.PhaseSeed = static_cast<uint32>(AgentIndex * 2654435761u);

  FTransformFragment& Transform = EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity);
  Transform.SetTransform(FTransform(FRotator::ZeroRotator, SpawnLocation, FVector::OneVector));

  FMassVelocityFragment& Velocity = EntityManager.GetFragmentDataChecked<FMassVelocityFragment>(Entity);
  Velocity.Value = FVector::ZeroVector;

  FMassDesiredMovementFragment& DesiredMovement = EntityManager.GetFragmentDataChecked<FMassDesiredMovementFragment>(Entity);
  DesiredMovement.DesiredVelocity = FVector::ZeroVector;
  DesiredMovement.DesiredFacing = FQuat::Identity;
  DesiredMovement.DesiredMaxSpeedOverride = Movement.MaxSpeedCmPerSecond;
}

FVector UCrowdDemoMassSubsystem::MakeSpawnLocation(const int32 AgentIndex, const int32 AgentCount) const
{
  const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(FMath::Max(1, AgentCount))));
  const int32 Row = AgentIndex / Columns;
  const int32 Column = AgentIndex % Columns;

  const FVector Origin(0.0f, -3050.0f, 60.0f);

  return Origin + FVector(
    (static_cast<float>(Column) - static_cast<float>(Columns) * 0.5f) * 95.0f,
    -static_cast<float>(Row) * 95.0f,
    0.0f);
}
