#include "CrowdDemoMassSubsystem.h"

#include "Mass/CrowdDemoMassReplication.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoClientVisualMassProcessor.h"
#include "Mass/CrowdDemoRoundSimProcessors.h"
#include "Mass/CrowdDemoProjectileAdapters.h"
#include "MassCrowdRuntimeFragments.h"
#include "MassCrowdRuntimeSubsystem.h"
#include "MassCrowdWorkerMovementControlResource.h"
#include "MassCrowdWorkerShadowSync.h"
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
  constexpr int32 MaxWorkerLifecycleProfileJournalRecords = 4096;
  TAutoConsoleVariable<int32> CVarCrowdDemoProjectileCapacity(
    TEXT("crowd.ProjectileMassCapacity"),
    1024,
    TEXT("Maximum Mass projectile entities available to the Demo host."),
    ECVF_Default);

  FMassEntityTemplateID GetCrowdDemoReplicationTemplateID()
  {
    return FMassEntityTemplateIDFactory::Make(FGuid(0x6d617373, 0x61696372, 0x6f776464, 0x656d6f31));
  }

  FMassEntityTemplateID GetCrowdDemoAuthorityTemplateID(
    const ECrowdDemoMassCapability Capabilities)
  {
    return FMassEntityTemplateIDFactory::Make(FGuid(
      0x6d617373, 0x61696372, 0x61757468,
      0x00010000u | static_cast<uint8>(Capabilities)));
  }

  ECrowdDemoMassCapability ResolveAuthorityCapabilityProfile(
    const ECrowdDemoScenario Scenario,
    const ECrowdDemoSoftPressureTestCase TestCase)
  {
    ECrowdDemoMassCapability Capabilities =
      ECrowdDemoMassCapability::Base;
    const bool bUsesCombat =
      Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && (TestCase
          == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse
        || TestCase
          == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat);
    if (bUsesCombat)
      Capabilities |= ECrowdDemoMassCapability::Combat;
    const bool bUsesTargetRegion =
      Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && (TestCase
          == ECrowdDemoSoftPressureTestCase::PursuitAndSettle
        || TestCase
          == ECrowdDemoSoftPressureTestCase::PursuitAndSettleMoving
        || TestCase
          == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetStatic
        || TestCase
          == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetMoving);
    if (bUsesTargetRegion)
      Capabilities |= ECrowdDemoMassCapability::Target;
    return Capabilities;
  }

  bool BuildWorkerLifecyclePayloads(
    const FMassEntityManager& EntityManager,
    const FMassEntityHandle Entity,
    FCrowdWorkerPayload& OutInitialState,
    FCrowdWorkerPayload& OutMovementProfile)
  {
    OutInitialState = {};
    OutMovementProfile = {};
    if (!EntityManager.IsEntityValid(Entity)) return false;
    const auto* Identity =
      EntityManager.GetFragmentDataPtr<FCrowdMassAgentFragment>(Entity);
    const auto* Behavior =
      EntityManager.GetFragmentDataPtr<FCrowdMassBehaviorFragment>(Entity);
    const auto* State = EntityManager.GetFragmentDataPtr<
      FCrowdMassSimulationStateFragment>(Entity);
    const auto* Properties = EntityManager.GetFragmentDataPtr<
      FCrowdMassPropertiesFragment>(Entity);
    const auto* Transform =
      EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity);
    const auto* Velocity =
      EntityManager.GetFragmentDataPtr<FMassVelocityFragment>(Entity);
    const auto* DemoMovement = EntityManager.GetFragmentDataPtr<
      FCrowdDemoMassMovementFragment>(Entity);
    const auto* Stats = EntityManager.GetFragmentDataPtr<
      FCrowdDemoMassStatsFragment>(Entity);
    if (!Identity || !Behavior || !State || !Properties
      || !Transform || !Velocity || !DemoMovement
      || !Identity->GetStableEntityRef().IsValid())
      return false;

    FCrowdMassBoundaryAgentRecord Record;
    Record.Identity = *Identity;
    Record.AgentFacts = Behavior->GetAgentFacts(*Identity);
    Record.State = *State;
    Record.State.Position = Transform->GetTransform().GetLocation();
    Record.State.Velocity = Velocity->Value;
    Record.State.YawDegrees = Transform->GetTransform().Rotator().Yaw;
    Record.State.bInitialized = true;
    Record.Properties = *Properties;
    if (!FCrowdWorkerBoundaryStateCodec::EncodeState(
        Record, OutInitialState))
      return false;

    FCrowdWorkerMovementControlEntry Profile;
    Profile.EntityRef = Identity->GetStableEntityRef();
    Profile.AgentId = Identity->AgentId;
    Profile.MaximumSpeedCmps = Properties->MaximumSpeedCmps;
    Profile.ParticleEnvironmentHardClearanceCm =
      Properties->HardSafetyGapCm;
    Profile.ParticlePhysicalRadiusCm =
      Properties->PhysicalRadiusCm;
    Profile.ParticleHardSafetyGapCm =
      Properties->HardSafetyGapCm;
    Profile.ParticleSoftMarginCm = Properties->SoftMarginCm;
    Profile.ParticleMobility = Properties->Mobility;
    Profile.AutonomousPreferredVelocity =
      DemoMovement->DesiredVelocity;
    Profile.bParticleActive = !Stats
      || (Stats->bAlive && Stats->Health > 0.0f);
    return FCrowdWorkerMovementProfileCodec::Encode(
      Profile, OutMovementProfile);
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

  void AddCrowdDemoAuthorityTemplateFragments(
    FMassEntityTemplateData& TemplateData,
    const FMassEntityTemplateID TemplateID,
    const ECrowdDemoMassCapability Capabilities)
  {
    check(EnumHasAnyFlags(
      Capabilities, ECrowdDemoMassCapability::Base));
    TemplateData.SetTemplateName(FString::Printf(
      TEXT("CrowdDemoAuthorityAgent_%u"),
      static_cast<uint8>(Capabilities)));
    TemplateData.AddTag<FCrowdDemoMassAgentTag>();
    TemplateData.AddTag<FCrowdMassAgentTag>();
    if (EnumHasAnyFlags(
        Capabilities, ECrowdDemoMassCapability::Target))
    {
      TemplateData.AddTag<FCrowdDemoTargetCapabilityTag>();
    }
    if (EnumHasAnyFlags(
        Capabilities, ECrowdDemoMassCapability::Combat))
    {
      TemplateData.AddTag<FCrowdDemoCombatCapabilityTag>();
      TemplateData.AddFragment<FCrowdDemoMassStatsFragment>();
      TemplateData.AddFragment<FCrowdDemoBusinessStateFragment>();
      TemplateData.AddFragment<FCrowdDemoRangedAttackFragment>();
      TemplateData.AddFragment<FCrowdDemoReactiveMotionFragment>();
      TemplateData.AddFragment<FCrowdDemoHitFlashFragment>();
    }
    TemplateData.AddFragment<FCrowdMassAgentFragment>();
    TemplateData.AddFragment<FCrowdMassBehaviorFragment>();
    TemplateData.AddFragment<FCrowdMassSimulationStateFragment>();
    TemplateData.AddFragment<FCrowdMassPropertiesFragment>();
    TemplateData.AddFragment<FCrowdMassFacingFragment>();
    TemplateData.AddFragment<FCrowdMassMovementOutputFragment>();
    TemplateData.AddFragment<FCrowdDemoMassIdentityFragment>();
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

    FReplicationTemplateIDFragment& TemplateIDFragment =
      TemplateData.AddFragment_GetRef<
        FReplicationTemplateIDFragment>();
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

    FMassReplicationSharedFragment ReplicationSharedFragment;
    ReplicationSharedFragment.LODCalculator.Initialize(
      ReplicationParams.LODDistance,
      ReplicationParams.BufferHysteresisOnDistancePercentage / 100.0f,
      ReplicationParams.LODMaxCountPerViewer);
    ReplicationSharedFragment.BubbleInfoClassHandle = ReplicationSubsystem->GetBubbleInfoClassHandle(ReplicationParams.BubbleInfoClass);
    ReplicationSharedFragment.CachedReplicator = ReplicationParams.ReplicatorClass.GetDefaultObject();
    const auto RegisterSharedFragments =
      [&](FMassEntityTemplateData& TemplateData)
    {
      TemplateData.AddConstSharedFragment(
        EntityManager.GetOrCreateConstSharedFragment(
          ReplicationParams));
      TemplateData.AddSharedFragment(
        EntityManager.GetOrCreateSharedFragment(
          ReplicationSharedFragment));
    };

    if (!SpawnerSubsystem->GetMassEntityTemplate(TemplateID))
    {
      FMassEntityTemplateData TemplateData;
      AddCrowdDemoTemplateFragments(TemplateData, TemplateID);
      RegisterSharedFragments(TemplateData);
      SpawnerSubsystem->GetMutableTemplateRegistryInstance().
        FindOrAddTemplate(TemplateID, MoveTemp(TemplateData));
    }

    const ECrowdDemoMassCapability AuthorityCapabilities[] = {
      ECrowdDemoMassCapability::Base,
      ECrowdDemoMassCapability::Base
        | ECrowdDemoMassCapability::Target,
      ECrowdDemoMassCapability::Base
        | ECrowdDemoMassCapability::Combat,
      ECrowdDemoMassCapability::Base
        | ECrowdDemoMassCapability::Target
        | ECrowdDemoMassCapability::Combat
    };
    for (const ECrowdDemoMassCapability Capabilities
      : AuthorityCapabilities)
    {
      const FMassEntityTemplateID AuthorityTemplateID =
        GetCrowdDemoAuthorityTemplateID(Capabilities);
      if (SpawnerSubsystem->GetMassEntityTemplate(
          AuthorityTemplateID))
      {
        continue;
      }
      FMassEntityTemplateData TemplateData;
      BuildCrowdDemoAuthorityTemplateData(
        TemplateData, AuthorityTemplateID, Capabilities);
      RegisterSharedFragments(TemplateData);
      SpawnerSubsystem->GetMutableTemplateRegistryInstance().
        FindOrAddTemplate(
          AuthorityTemplateID, MoveTemp(TemplateData));
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMass: replication_template_registered authority_capability_templates=4 source=MassClientBubble"));
    return true;
  }
}

void BuildCrowdDemoAuthorityTemplateData(
  FMassEntityTemplateData& TemplateData,
  const FMassEntityTemplateID& TemplateID,
  const ECrowdDemoMassCapability Capabilities)
{
  AddCrowdDemoAuthorityTemplateFragments(
    TemplateData, TemplateID, Capabilities);
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
  StableEntityHandles.Reset();
  PendingWorkerSpawns.Reset();
  PendingWorkerDespawns.Reset();
  PendingWorkerProfileRevisions.Reset();
  bWorkerLifecycleProfileJournalOverflowed = false;
  ProjectileStore.ResetTracking();
  Super::Deinitialize();
}

void UCrowdDemoMassSubsystem::RegisterRoundSimProcessors()
{
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  UMassSimulationSubsystem* SimulationSubsystem = World ? World->GetSubsystem<UMassSimulationSubsystem>() : nullptr;
  if (!EntitySubsystem || !SimulationSubsystem
    || WorkerInputSyncProcessor || WorkerResultApplyProcessor
    || ClientVisualProcessor)
  {
    return;
  }

  const TSharedRef<FMassEntityManager> EntityManager = EntitySubsystem->GetMutableEntityManager().AsShared();
  WorkerInputSyncProcessor =
    NewObject<UCrowdDemoWorkerInputSyncProcessor>(this);
  WorkerInputSyncProcessor->CallInitialize(this, EntityManager);
  SimulationSubsystem->RegisterDynamicProcessor(
    *WorkerInputSyncProcessor);

  WorkerResultApplyProcessor =
    NewObject<UCrowdDemoWorkerResultApplyProcessor>(this);
  WorkerResultApplyProcessor->CallInitialize(this, EntityManager);
  SimulationSubsystem->RegisterDynamicProcessor(
    *WorkerResultApplyProcessor);

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
    TEXT("CrowdDemoMass: worker_processors_registered input_sync=1 result_apply=1 legacy_round_processors=0 client_visual=%d public_presentation=%d net_mode=%d source=MassSubsystem"),
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
    if (WorkerResultApplyProcessor)
    {
      SimulationSubsystem->UnregisterDynamicProcessor(
        *WorkerResultApplyProcessor);
    }
    if (WorkerInputSyncProcessor)
    {
      SimulationSubsystem->UnregisterDynamicProcessor(
        *WorkerInputSyncProcessor);
    }
  }
  ClientVisualProcessor = nullptr;
  WorkerResultApplyProcessor = nullptr;
  WorkerInputSyncProcessor = nullptr;
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

  const ECrowdDemoMassCapability Capabilities =
    ResolveAuthorityCapabilityProfile(
      CurrentScenario, SoftPressureTestCase);
  const FMassEntityTemplate* Template =
    SpawnerSubsystem->GetMassEntityTemplate(
      GetCrowdDemoAuthorityTemplateID(Capabilities));
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
      const FCrowdMassAgentFragment& Identity =
        EntityManager.GetFragmentDataChecked<FCrowdMassAgentFragment>(
          TrackedAgents[Index]);
      checkf(!StableEntityHandles.Contains(Identity.GetStableEntityRef()),
        TEXT("Duplicate stable entity registered while spawning Mass agents"));
      StableEntityHandles.Add(
        Identity.GetStableEntityRef(), TrackedAgents[Index]);
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
    if (!Stats || (Stats->bAlive && Stats->Health > 0.0f))
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
    if (!Identity || !Movement || !Transform || !Velocity || !Visual)
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
    const bool bHasCombatBundle = Stats && Business && Attack
      && Reactive && HitFlash;
    if (bHasCombatBundle)
    {
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
    }
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
  if (StableEntityHandles.Num() != Targets.Num())
    return false;
  for (const FCrowdMassCommitTarget& Target : Targets)
  {
    FMassEntityHandle Entity;
    if (!ResolveTrackedAgentHandle(
        Target.EntityRef, EntityManager, Entity))
      return false;
    const auto* Identity =
      EntityManager.GetFragmentDataPtr<FCrowdMassAgentFragment>(Entity);
    if (!Identity || Identity->AgentId != Target.AgentId
      || Identity->LifecycleSerial != Target.LifecycleSerial)
      return false;
  }

  for (const FCrowdMassCommitRecord& Record : Plan.Records)
  {
    FMassEntityHandle Entity;
    check(ResolveTrackedAgentHandle(
      Record.EntityRef, EntityManager, Entity));
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

bool UCrowdDemoMassSubsystem::ResolveTrackedAgentHandle(
  const FCrowdStableEntityRef& EntityRef,
  FMassEntityManager& EntityManager,
  FMassEntityHandle& OutEntity) const
{
  OutEntity = {};
  const FMassEntityHandle* Found = StableEntityHandles.Find(EntityRef);
  if (!Found || !EntityManager.IsEntityValid(*Found))
    return false;
  const FCrowdMassAgentFragment* Identity =
    EntityManager.GetFragmentDataPtr<FCrowdMassAgentFragment>(*Found);
  if (!Identity || Identity->GetStableEntityRef() != EntityRef)
    return false;
  OutEntity = *Found;
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
  const ECrowdDemoMassCapability Capabilities =
    ResolveAuthorityCapabilityProfile(
      CurrentScenario, SoftPressureTestCase);
  const FMassEntityTemplate* Template =
    SpawnerSubsystem->GetMassEntityTemplate(
      GetCrowdDemoAuthorityTemplateID(Capabilities));
  if (TrackedIndex == INDEX_NONE || !Template)
    return false;

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

  FCrowdWorkerSpawnDelta WorkerSpawn;
  FCrowdWorkerDespawnDelta WorkerDespawn;
  FCrowdWorkerExternalGameplayInput WorkerProfile;
  bool bPublishWorkerLifecycle = false;
  if (const UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World->GetSubsystem<UMassCrowdRuntimeSubsystem>())
  {
    bPublishWorkerLifecycle =
      RuntimeSubsystem->GetWorkerShadowSync().IsStarted();
  }
  if (bPublishWorkerLifecycle)
  {
    const int32 PendingRecordCount = PendingWorkerSpawns.Num()
      + PendingWorkerDespawns.Num()
      + PendingWorkerProfileRevisions.Num();
    if (PendingRecordCount
        > MaxWorkerLifecycleProfileJournalRecords - 3
      || !BuildWorkerLifecyclePayloads(
        EntityManager, Replacement[0],
        WorkerSpawn.InitialState, WorkerProfile.FullState))
    {
      bWorkerLifecycleProfileJournalOverflowed = true;
      EntityManager.DestroyEntity(Replacement[0]);
      return false;
    }
    WorkerDespawn.EntityRef = EntityRef;
    WorkerDespawn.ReasonId = 1;
    WorkerSpawn.EntityRef = RuntimeIdentity.GetStableEntityRef();
    WorkerProfile.EntityRef = WorkerSpawn.EntityRef;
    WorkerProfile.InputTypeId = static_cast<uint16>(
      ECrowdWorkerExternalGameplayInputType::
        MovementProfileRevision);
    WorkerProfile.DirtyMask = 1;
  }

  EntityManager.DestroyEntity(TrackedAgents[TrackedIndex]);
  StableEntityHandles.Remove(EntityRef);
  TrackedAgents[TrackedIndex] = Replacement[0];
  OutReplacementRef = RuntimeIdentity.GetStableEntityRef();
  checkf(!StableEntityHandles.Contains(OutReplacementRef),
    TEXT("Duplicate stable entity registered while recycling Mass agent"));
  StableEntityHandles.Add(OutReplacementRef, Replacement[0]);
  if (bPublishWorkerLifecycle)
  {
    PendingWorkerDespawns.Add(MoveTemp(WorkerDespawn));
    PendingWorkerSpawns.Add(MoveTemp(WorkerSpawn));
    PendingWorkerProfileRevisions.Add(MoveTemp(WorkerProfile));
  }
  return true;
}

bool UCrowdDemoMassSubsystem::CopyPendingWorkerLifecycleProfileJournal(
  TArray<FCrowdWorkerSpawnDelta>& OutSpawns,
  TArray<FCrowdWorkerDespawnDelta>& OutDespawns,
  TArray<FCrowdWorkerExternalGameplayInput>& OutProfileRevisions) const
{
  check(IsInGameThread());
  OutSpawns = PendingWorkerSpawns;
  OutDespawns = PendingWorkerDespawns;
  OutProfileRevisions = PendingWorkerProfileRevisions;
  return !bWorkerLifecycleProfileJournalOverflowed;
}

bool UCrowdDemoMassSubsystem::AcknowledgeWorkerLifecycleProfileJournal(
  const int32 SpawnCount,
  const int32 DespawnCount,
  const int32 ProfileRevisionCount)
{
  check(IsInGameThread());
  if (SpawnCount < 0 || SpawnCount > PendingWorkerSpawns.Num()
    || DespawnCount < 0
    || DespawnCount > PendingWorkerDespawns.Num()
    || ProfileRevisionCount < 0
    || ProfileRevisionCount > PendingWorkerProfileRevisions.Num())
    return false;
  PendingWorkerSpawns.RemoveAt(
    0, SpawnCount, EAllowShrinking::No);
  PendingWorkerDespawns.RemoveAt(
    0, DespawnCount, EAllowShrinking::No);
  PendingWorkerProfileRevisions.RemoveAt(
    0, ProfileRevisionCount, EAllowShrinking::No);
  return true;
}

void UCrowdDemoMassSubsystem::DestroyTrackedAgents()
{
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!EntitySubsystem)
  {
    TrackedAgents.Reset();
    StableEntityHandles.Reset();
    PendingWorkerSpawns.Reset();
    PendingWorkerDespawns.Reset();
    PendingWorkerProfileRevisions.Reset();
    bWorkerLifecycleProfileJournalOverflowed = false;
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
  StableEntityHandles.Reset();
  PendingWorkerSpawns.Reset();
  PendingWorkerDespawns.Reset();
  PendingWorkerProfileRevisions.Reset();
  bWorkerLifecycleProfileJournalOverflowed = false;
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

bool UCrowdDemoMassSubsystem::ValidateProjectileStates(
  const TConstArrayView<FCrowdProjectileState> Projectiles) const
{
  return ProjectileStore.ValidatePreparedStates(Projectiles);
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

  if (FCrowdDemoMassStatsFragment* Stats =
      EntityManager.GetFragmentDataPtr<FCrowdDemoMassStatsFragment>(Entity))
  {
    *Stats = FCrowdDemoMassStatsFragment();
    *EntityManager.GetFragmentDataPtr<FCrowdDemoBusinessStateFragment>(Entity) =
      FCrowdDemoBusinessStateFragment();
    *EntityManager.GetFragmentDataPtr<FCrowdDemoRangedAttackFragment>(Entity) =
      FCrowdDemoRangedAttackFragment();
    *EntityManager.GetFragmentDataPtr<FCrowdDemoReactiveMotionFragment>(Entity) =
      FCrowdDemoReactiveMotionFragment();
    *EntityManager.GetFragmentDataPtr<FCrowdDemoHitFlashFragment>(Entity) =
      FCrowdDemoHitFlashFragment();
  }

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
