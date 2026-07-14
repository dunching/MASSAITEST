#include "CrowdDemoMassSubsystem.h"

#include "Mass/CrowdDemoMassReplication.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoClientVisualMassProcessor.h"
#include "Mass/CrowdDemoRoundSimProcessors.h"
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

namespace
{
  FMassEntityTemplateID GetCrowdDemoReplicationTemplateID()
  {
    return FMassEntityTemplateIDFactory::Make(FGuid(0x6d617373, 0x61696372, 0x6f776464, 0x656d6f31));
  }

  void AddCrowdDemoTemplateFragments(FMassEntityTemplateData& TemplateData, const FMassEntityTemplateID TemplateID)
  {
    TemplateData.SetTemplateName(TEXT("CrowdDemoMassReplicatedAgent"));
    TemplateData.AddTag<FCrowdDemoMassAgentTag>();
    TemplateData.AddFragment<FCrowdDemoMassIdentityFragment>();
    TemplateData.AddFragment<FCrowdDemoMassStatsFragment>();
    TemplateData.AddFragment<FCrowdDemoMassMovementFragment>();
    TemplateData.AddFragment<FCrowdDemoRoundSimStateFragment>();
    TemplateData.AddFragment<FCrowdDemoRoundFormationFragment>();
    TemplateData.AddFragment<FCrowdDemoRoundMoveIntentFragment>();
    TemplateData.AddFragment<FCrowdDemoRoundFlowSampleFragment>();
    TemplateData.AddFragment<FCrowdDemoRoundProposedMovementFragment>();
    TemplateData.AddFragment<FCrowdDemoParticlePropertiesFragment>();
    TemplateData.AddFragment<FCrowdDemoRoundParticleConstraintFragment>();
    TemplateData.AddFragment<FCrowdDemoRoundObstacleConstraintFragment>();
    TemplateData.AddFragment<FCrowdDemoRoundPbdCorrectionFragment>();
    TemplateData.AddFragment<FCrowdDemoRoundSeparationFragment>();
    TemplateData.AddFragment<FCrowdDemoPortalAdmissionFragment>();
    TemplateData.AddFragment<FCrowdDemoPassingBandFragment>();
    TemplateData.AddFragment<FCrowdDemoPositionAssignmentFragment>();
    TemplateData.AddFragment<FCrowdDemoPursuitSteeringStateFragment>();
    TemplateData.AddFragment<FCrowdDemoPursuitGuidanceFragment>();
    TemplateData.AddFragment<FCrowdDemoOrcaVelocityFragment>();
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
    ReplicationParams.LODMaxCount[EMassLOD::High] = 2000;
    ReplicationParams.LODMaxCount[EMassLOD::Medium] = 2000;
    ReplicationParams.LODMaxCount[EMassLOD::Low] = 2000;
    ReplicationParams.LODMaxCount[EMassLOD::Off] = 0;
    ReplicationParams.LODMaxCountPerViewer[EMassLOD::High] = 2000;
    ReplicationParams.LODMaxCountPerViewer[EMassLOD::Medium] = 2000;
    ReplicationParams.LODMaxCountPerViewer[EMassLOD::Low] = 2000;
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
  RegisterRoundSimProcessors();
}

void UCrowdDemoMassSubsystem::Deinitialize()
{
  UnregisterRoundSimProcessors();
  TargetActor.Reset();
  TrackedAgents.Reset();
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

  if (World->GetNetMode() != NM_DedicatedServer)
  {
    ClientVisualProcessor = NewObject<UCrowdDemoClientVisualMassProcessor>(this);
    ClientVisualProcessor->CallInitialize(this, EntityManager);
    SimulationSubsystem->RegisterDynamicProcessor(*ClientVisualProcessor);
  }

  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoMass: round_processors_registered pipeline=1 client_visual=%d net_mode=%d source=MassSubsystem"),
    ClientVisualProcessor ? 1 : 0,
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
  CurrentScenario = InScenario;
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
    if (!Identity || !Movement || !Transform || !Velocity)
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
  }

  OutStates.Sort([](const FCrowdDemoRoundAgentState& A, const FCrowdDemoRoundAgentState& B)
  {
    return A.AgentId < B.AgentId;
  });
  return OutStates.Num();
}

void UCrowdDemoMassSubsystem::DestroyTrackedAgents()
{
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!EntitySubsystem)
  {
    TrackedAgents.Reset();
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

  FCrowdDemoMassStatsFragment& Stats = EntityManager.GetFragmentDataChecked<FCrowdDemoMassStatsFragment>(Entity);
  Stats.Health = 100.0f;
  Stats.MaxHealth = 100.0f;
  Stats.LifecycleState = ECrowdDemoLifecycleState::Alive;
  Stats.bAlive = true;

  FCrowdDemoMassMovementFragment& Movement = EntityManager.GetFragmentDataChecked<FCrowdDemoMassMovementFragment>(Entity);
  Movement.ContactRadiusCm = 42.0f;
  Movement.SeparationRadiusCm = 78.0f;
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

  FCrowdDemoRoundFormationFragment& RoundFormation = EntityManager.GetFragmentDataChecked<FCrowdDemoRoundFormationFragment>(Entity);
  RoundFormation.FormationIndex = AgentIndex;
  RoundFormation.LocalOffset = FVector::ZeroVector;
  RoundFormation.RadiusCm = Movement.ContactRadiusCm;
  RoundFormation.bInitialized = false;

  FCrowdDemoRoundMoveIntentFragment& RoundMoveIntent = EntityManager.GetFragmentDataChecked<FCrowdDemoRoundMoveIntentFragment>(Entity);
  RoundMoveIntent = FCrowdDemoRoundMoveIntentFragment();

  EntityManager.GetFragmentDataChecked<FCrowdDemoRoundFlowSampleFragment>(Entity) = FCrowdDemoRoundFlowSampleFragment();
  EntityManager.GetFragmentDataChecked<FCrowdDemoRoundProposedMovementFragment>(Entity) = FCrowdDemoRoundProposedMovementFragment();
  FCrowdDemoParticlePropertiesFragment& ParticleProperties =
    EntityManager.GetFragmentDataChecked<FCrowdDemoParticlePropertiesFragment>(Entity);
  ParticleProperties.PhysicalRadiusCm = Movement.ContactRadiusCm;
  ParticleProperties.HardSafetyGapCm = 10.0f;
  ParticleProperties.SoftMarginCm = 17.0f;
  ParticleProperties.Mobility = 1.0f;
  EntityManager.GetFragmentDataChecked<FCrowdDemoRoundParticleConstraintFragment>(Entity) =
    FCrowdDemoRoundParticleConstraintFragment();
  EntityManager.GetFragmentDataChecked<FCrowdDemoRoundObstacleConstraintFragment>(Entity) = FCrowdDemoRoundObstacleConstraintFragment();
  EntityManager.GetFragmentDataChecked<FCrowdDemoRoundPbdCorrectionFragment>(Entity) = FCrowdDemoRoundPbdCorrectionFragment();

  FCrowdDemoRoundSeparationFragment& RoundSeparation = EntityManager.GetFragmentDataChecked<FCrowdDemoRoundSeparationFragment>(Entity);
  RoundSeparation = FCrowdDemoRoundSeparationFragment();

  FCrowdDemoMassVisualFragment& Visual = EntityManager.GetFragmentDataChecked<FCrowdDemoMassVisualFragment>(Entity);
  Visual.AnimState = ECrowdDemoAnimState::Idle;
  Visual.VatClipIndex = static_cast<uint8>(ECrowdDemoAnimState::Idle);
  Visual.VatPhaseByte = static_cast<uint8>((AgentIndex * 37) % 255);
  Visual.VatPlayRateByte = 128;

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
