#include "Mass/CrowdDemoMassReplication.h"

#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplate.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassReplicationFragments.h"
#include "MassSpawnerTypes.h"
#include "Net/UnrealNetwork.h"

namespace
{
  constexpr float CrowdDemoReplicatePositionToleranceCm = 1.0f;
  constexpr float CrowdDemoReplicateYawToleranceDegrees = 0.25f;
  constexpr float CrowdDemoReplicateVelocityToleranceCmPerSecond = 1.0f;
  FMassEntityTemplateID GetCrowdDemoReplicationTemplateIDForBubble()
  {
    return FMassEntityTemplateIDFactory::Make(FGuid(0x6d617373, 0x61696372, 0x6f776464, 0x656d6f31));
  }

  void RepairCrowdDemoAgentBaseData(FReplicatedCrowdDemoAgent& Agent)
  {
    if (!Agent.GetNetID().IsValid() && Agent.NetworkIdValue != 0)
    {
      Agent.SetNetID(FMassNetworkID(Agent.NetworkIdValue));
    }

    if (!Agent.GetTemplateID().IsValid())
    {
      Agent.SetTemplateID(GetCrowdDemoReplicationTemplateIDForBubble());
    }
  }

  void RepairCrowdDemoAgentsBaseData(TArray<FCrowdDemoMassFastArrayItem>* Agents, const TArrayView<int32> Indices)
  {
    if (!Agents)
    {
      return;
    }

    for (const int32 Index : Indices)
    {
      if (Agents->IsValidIndex(Index))
      {
        RepairCrowdDemoAgentBaseData((*Agents)[Index].Agent);
      }
    }
  }

  void FillCrowdDemoReplicatedAgent(
    FReplicatedCrowdDemoAgent& OutAgent,
    const int32 EntityIndex,
    const float ServerSampleTimeSeconds,
    TConstArrayView<FMassNetworkIDFragment> NetworkIDs,
    TConstArrayView<FReplicationTemplateIDFragment> TemplateIDs,
    TConstArrayView<FCrowdDemoMassIdentityFragment> Identities,
    TConstArrayView<FCrowdDemoMassMovementFragment> Movements,
    TConstArrayView<FCrowdDemoMassVisualFragment> Visuals,
    TConstArrayView<FTransformFragment> Transforms,
    TConstArrayView<FMassVelocityFragment> Velocities)
  {
    OutAgent.SetNetID(NetworkIDs[EntityIndex].NetID);
    OutAgent.SetTemplateID(TemplateIDs[EntityIndex].ID);
    OutAgent.NetworkIdValue = NetworkIDs[EntityIndex].NetID.GetValue();

    const FCrowdDemoMassIdentityFragment& Identity = Identities[EntityIndex];
    const FCrowdDemoMassMovementFragment& Movement = Movements[EntityIndex];
    const FCrowdDemoMassVisualFragment& Visual = Visuals[EntityIndex];
    const FTransformFragment& Transform = Transforms[EntityIndex];
    const FMassVelocityFragment& Velocity = Velocities[EntityIndex];

    FReplicatedAgentPositionYawData& PositionYaw = OutAgent.GetReplicatedPositionYawDataMutable();
    PositionYaw.SetPosition(Transform.GetTransform().GetLocation());
    PositionYaw.SetYaw(FMath::DegreesToRadians(Movement.YawDegrees));

    OutAgent.Velocity = FVector_NetQuantize10(Velocity.Value);
    OutAgent.VisualId = Identity.VisualId;
    OutAgent.LifecycleSerial = Identity.LifecycleSerial;
    OutAgent.AnimState = static_cast<uint8>(Visual.AnimState);
    OutAgent.VatClipIndex = Visual.VatClipIndex;
    OutAgent.VatPhaseByte = Visual.VatPhaseByte;
    OutAgent.VatPlayRateByte = Visual.VatPlayRateByte;
    OutAgent.ServerSampleTimeSeconds = ServerSampleTimeSeconds;
  }

  bool HasReplicatedAgentChanged(const FReplicatedCrowdDemoAgent& Existing, const FReplicatedCrowdDemoAgent& Next)
  {
    const FReplicatedAgentPositionYawData& ExistingPositionYaw = Existing.GetReplicatedPositionYawData();
    const FReplicatedAgentPositionYawData& NextPositionYaw = Next.GetReplicatedPositionYawData();
    if (!ExistingPositionYaw.GetPosition().Equals(NextPositionYaw.GetPosition(), CrowdDemoReplicatePositionToleranceCm))
    {
      return true;
    }

    const float ExistingYawDegrees = FMath::RadiansToDegrees(ExistingPositionYaw.GetYaw());
    const float NextYawDegrees = FMath::RadiansToDegrees(NextPositionYaw.GetYaw());
    if (FMath::Abs(FMath::FindDeltaAngleDegrees(ExistingYawDegrees, NextYawDegrees)) > CrowdDemoReplicateYawToleranceDegrees)
    {
      return true;
    }

    if (!FVector(Existing.Velocity).Equals(FVector(Next.Velocity), CrowdDemoReplicateVelocityToleranceCmPerSecond))
    {
      return true;
    }

    return Existing.VisualId != Next.VisualId
      || Existing.LifecycleSerial != Next.LifecycleSerial
      || Existing.AnimState != Next.AnimState
      || Existing.VatClipIndex != Next.VatClipIndex
      || Existing.VatPhaseByte != Next.VatPhaseByte
      || Existing.VatPlayRateByte != Next.VatPlayRateByte;
  }
}

bool FCrowdDemoMassClientBubbleHandler::UpdateAgent(const FMassReplicatedAgentHandle Handle, const FReplicatedCrowdDemoAgent& Agent)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE
  check(AgentHandleManager.IsValidHandle(Handle));
  const int32 AgentsIndex = AgentLookupArray[Handle.GetIndex()].AgentsIdx;
  FCrowdDemoMassFastArrayItem& Item = (*Agents)[AgentsIndex];
  if (!HasReplicatedAgentChanged(Item.Agent, Agent))
  {
    return false;
  }

  Item.Agent = Agent;
  Serializer->MarkItemDirty(Item);
  return true;
#else
  return false;
#endif
}

bool FCrowdDemoMassClientBubbleHandler::UpdateAgentMinimal(
  const FMassReplicatedAgentHandle Handle,
  const FReplicatedCrowdDemoAgent& Agent)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE
  check(AgentHandleManager.IsValidHandle(Handle));
  const int32 AgentsIndex = AgentLookupArray[Handle.GetIndex()].AgentsIdx;
  FCrowdDemoMassFastArrayItem& Item = (*Agents)[AgentsIndex];
  FReplicatedCrowdDemoAgent& Existing = Item.Agent;
  const bool bChanged = Existing.VisualId != Agent.VisualId
    || Existing.LifecycleSerial != Agent.LifecycleSerial
    || Existing.AnimState != Agent.AnimState
    || Existing.VatClipIndex != Agent.VatClipIndex
    || Existing.VatPlayRateByte != Agent.VatPlayRateByte;
  if (!bChanged)
  {
    return false;
  }

  Existing.VisualId = Agent.VisualId;
  Existing.LifecycleSerial = Agent.LifecycleSerial;
  Existing.AnimState = Agent.AnimState;
  Existing.VatClipIndex = Agent.VatClipIndex;
  Existing.VatPlayRateByte = Agent.VatPlayRateByte;
  Serializer->MarkItemDirty(Item);
  return true;
#else
  return false;
#endif
}

int32 FCrowdDemoMassClientBubbleHandler::GetAgentCount() const
{
  return Agents ? Agents->Num() : 0;
}

#if UE_REPLICATION_COMPILE_CLIENT_CODE
void FCrowdDemoMassClientBubbleHandler::PostReplicatedAdd(const TArrayView<int32> AddedIndices, const int32 FinalSize)
{
  RepairCrowdDemoAgentsBaseData(Agents, AddedIndices);

  auto AddRequirementsForSpawnQuery = [](FMassEntityQuery& Query)
  {
    Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoMassStatsFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoMassMovementFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoClientAuthorityFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoClientVisualOffsetFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoRoundFormationFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoRoundMoveIntentFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoRoundFlowSampleFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoRoundProposedMovementFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoRoundObstacleConstraintFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoRoundPbdCorrectionFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoRoundSeparationFragment>(EMassFragmentAccess::ReadWrite);
  };

  auto CacheFragmentViewsForSpawnQuery = [](FMassExecutionContext&)
  {
  };

  auto SetSpawnedEntityData = [this](const FMassEntityView& EntityView, const FReplicatedCrowdDemoAgent& Agent, const int32)
  {
    SetReplicatedEntityData(EntityView, Agent);
  };

  auto SetModifiedEntityData = [this](const FMassEntityView& EntityView, const FReplicatedCrowdDemoAgent& Agent)
  {
    SetReplicatedEntityData(EntityView, Agent);
  };

  PostReplicatedAddHelper(AddedIndices, AddRequirementsForSpawnQuery, CacheFragmentViewsForSpawnQuery, SetSpawnedEntityData, SetModifiedEntityData);
}

void FCrowdDemoMassClientBubbleHandler::PostReplicatedChange(const TArrayView<int32> ChangedIndices, const int32 FinalSize)
{
  RepairCrowdDemoAgentsBaseData(Agents, ChangedIndices);
  auto SetModifiedEntityData = [this](const FMassEntityView& EntityView, const FReplicatedCrowdDemoAgent& Agent)
  {
    SetReplicatedEntityData(EntityView, Agent);
  };

  PostReplicatedChangeHelper(ChangedIndices, SetModifiedEntityData);
}

void FCrowdDemoMassClientBubbleHandler::SetReplicatedEntityData(const FMassEntityView& EntityView, const FReplicatedCrowdDemoAgent& Agent) const
{
  const FReplicatedAgentPositionYawData& PositionYaw = Agent.GetReplicatedPositionYawData();
  const FVector NewLocation = PositionYaw.GetPosition();
  const float NewYawDegrees = FMath::RadiansToDegrees(PositionYaw.GetYaw());
  const FVector NewVelocity = FVector(Agent.Velocity);

  FTransformFragment& Transform = EntityView.GetFragmentData<FTransformFragment>();
  FMassVelocityFragment& Velocity = EntityView.GetFragmentData<FMassVelocityFragment>();
  FCrowdDemoMassIdentityFragment& Identity = EntityView.GetFragmentData<FCrowdDemoMassIdentityFragment>();
  FCrowdDemoMassStatsFragment& Stats = EntityView.GetFragmentData<FCrowdDemoMassStatsFragment>();
  FCrowdDemoMassMovementFragment& Movement = EntityView.GetFragmentData<FCrowdDemoMassMovementFragment>();
  FCrowdDemoMassVisualFragment& Visual = EntityView.GetFragmentData<FCrowdDemoMassVisualFragment>();
  FCrowdDemoClientAuthorityFragment& Authority = EntityView.GetFragmentData<FCrowdDemoClientAuthorityFragment>();
  FCrowdDemoClientVisualOffsetFragment& VisualOffset = EntityView.GetFragmentData<FCrowdDemoClientVisualOffsetFragment>();
  FCrowdDemoRoundSimStateFragment& RoundSimState = EntityView.GetFragmentData<FCrowdDemoRoundSimStateFragment>();
  const UWorld* World = Serializer ? Serializer->GetEntityManagerChecked().GetWorld() : nullptr;

  Transform.GetMutableTransform().SetLocation(NewLocation);
  Transform.GetMutableTransform().SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(NewYawDegrees)));
  Velocity.Value = NewVelocity;

  Identity.Id = Agent.VisualId;
  Identity.VisualId = Agent.VisualId;
  Identity.LifecycleSerial = Agent.LifecycleSerial;
  Stats.bAlive = true;
  Stats.LifecycleState = ECrowdDemoLifecycleState::Alive;
  Movement.CurrentVelocity = NewVelocity;
  Movement.DesiredVelocity = NewVelocity;
  Movement.YawDegrees = NewYawDegrees;
  Visual.AnimState = static_cast<ECrowdDemoAnimState>(Agent.AnimState);
  Visual.VatClipIndex = Agent.VatClipIndex;
  Visual.VatPhaseByte = Agent.VatPhaseByte;
  Visual.VatPlayRateByte = Agent.VatPlayRateByte;

  Authority.VisualId = Agent.VisualId;
  Authority.LifecycleSerial = Agent.LifecycleSerial;
  Authority.AuthoritativeLocation = NewLocation;
  Authority.AuthoritativeVelocity = NewVelocity;
  Authority.AuthoritativeYawDegrees = NewYawDegrees;
  Authority.ServerSampleTimeSeconds = Agent.ServerSampleTimeSeconds;
  Authority.LastReceiveWorldTimeSeconds = World
    ? World->GetTimeSeconds()
    : 0.0f;
  Authority.AnimState = static_cast<ECrowdDemoAnimState>(Agent.AnimState);
  Authority.VatClipIndex = Agent.VatClipIndex;
  Authority.VatPhaseByte = Agent.VatPhaseByte;
  Authority.VatPlayRateByte = Agent.VatPlayRateByte;
  Authority.bInitialized = true;

  if (!RoundSimState.bInitialized)
  {
    RoundSimState.Location = NewLocation;
    RoundSimState.Velocity = NewVelocity;
    RoundSimState.YawDegrees = NewYawDegrees;
    RoundSimState.SimulatedServerTimeSeconds = Agent.ServerSampleTimeSeconds;
    RoundSimState.PlanRevision = 0;
    RoundSimState.bInitialized = true;
  }

}
#endif

ACrowdDemoMassClientBubbleInfo::ACrowdDemoMassClientBubbleInfo(const FObjectInitializer& ObjectInitializer)
  : Super(ObjectInitializer)
{
  Serializers.Add(&CrowdDemoSerializer);
}

void ACrowdDemoMassClientBubbleInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  FDoRepLifetimeParams SharedParams;
  SharedParams.bIsPushBased = true;
  DOREPLIFETIME_WITH_PARAMS_FAST(ACrowdDemoMassClientBubbleInfo, CrowdDemoSerializer, SharedParams);
}

void UCrowdDemoMassReplicator::AddRequirements(FMassEntityQuery& EntityQuery)
{
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassMovementFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoMassVisualFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoMassReplicator::ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE
  TConstArrayView<FCrowdDemoMassIdentityFragment> Identities;
  TConstArrayView<FCrowdDemoMassMovementFragment> Movements;
  TConstArrayView<FCrowdDemoMassVisualFragment> Visuals;
  TConstArrayView<FTransformFragment> Transforms;
  TConstArrayView<FMassVelocityFragment> Velocities;
  TConstArrayView<FMassNetworkIDFragment> NetworkIDs;
  TConstArrayView<FReplicationTemplateIDFragment> TemplateIDs;
  FMassReplicationSharedFragment* RepSharedFragment = nullptr;
  const UCrowdDemoMassSubsystem* MassSubsystem = ReplicationContext.World.GetSubsystem<UCrowdDemoMassSubsystem>();
  const bool bUseRoundSimMinimalUpdates = MassSubsystem
    && IsCrowdDemoRoundSimScenario(MassSubsystem->GetScenario());

  auto CacheViewsCallback = [&] (FMassExecutionContext& InContext)
  {
    NetworkIDs = InContext.GetFragmentView<FMassNetworkIDFragment>();
    TemplateIDs = InContext.GetFragmentView<FReplicationTemplateIDFragment>();
    Identities = InContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
    Movements = InContext.GetFragmentView<FCrowdDemoMassMovementFragment>();
    Visuals = InContext.GetFragmentView<FCrowdDemoMassVisualFragment>();
    Transforms = InContext.GetFragmentView<FTransformFragment>();
    Velocities = InContext.GetFragmentView<FMassVelocityFragment>();
    RepSharedFragment = &InContext.GetMutableSharedFragment<FMassReplicationSharedFragment>();
  };

  auto AddEntityCallback = [&] (
    FMassExecutionContext& InContext,
    const int32 EntityIndex,
    FReplicatedCrowdDemoAgent& InReplicatedAgent,
    const FMassClientHandle ClientHandle) -> FMassReplicatedAgentHandle
  {
    check(RepSharedFragment);
    FillCrowdDemoReplicatedAgent(
      InReplicatedAgent,
      EntityIndex,
      static_cast<float>(ReplicationContext.World.GetTimeSeconds()),
      NetworkIDs,
      TemplateIDs,
      Identities,
      Movements,
      Visuals,
      Transforms,
      Velocities);

    ACrowdDemoMassClientBubbleInfo& BubbleInfo =
      RepSharedFragment->GetTypedClientBubbleInfoChecked<ACrowdDemoMassClientBubbleInfo>(ClientHandle);
    return BubbleInfo.GetCrowdDemoSerializer().Bubble.AddAgent(InContext.GetEntity(EntityIndex), InReplicatedAgent);
  };

  auto ModifyEntityCallback = [&] (
    FMassExecutionContext&,
    const int32 EntityIndex,
    const EMassLOD::Type,
    const double Time,
    const FMassReplicatedAgentHandle Handle,
    const FMassClientHandle ClientHandle)
  {
    check(RepSharedFragment);
    FReplicatedCrowdDemoAgent NextAgent;
    FillCrowdDemoReplicatedAgent(
      NextAgent,
      EntityIndex,
      static_cast<float>(ReplicationContext.World.GetTimeSeconds()),
      NetworkIDs,
      TemplateIDs,
      Identities,
      Movements,
      Visuals,
      Transforms,
      Velocities);

    ACrowdDemoMassClientBubbleInfo& BubbleInfo =
      RepSharedFragment->GetTypedClientBubbleInfoChecked<ACrowdDemoMassClientBubbleInfo>(ClientHandle);
    TArrayView<FMassReplicatedAgentFragment> ReplicatedAgents = Context.GetMutableFragmentView<FMassReplicatedAgentFragment>();
    const bool bUpdated = bUseRoundSimMinimalUpdates
      ? BubbleInfo.GetCrowdDemoSerializer().Bubble.UpdateAgentMinimal(Handle, NextAgent)
      : BubbleInfo.GetCrowdDemoSerializer().Bubble.UpdateAgent(Handle, NextAgent);
    if (bUpdated)
    {
      ReplicatedAgents[EntityIndex].AgentData.LastUpdateTime = Time;
    }
  };

  auto RemoveEntityCallback = [&] (FMassExecutionContext&, const FMassReplicatedAgentHandle Handle, const FMassClientHandle ClientHandle)
  {
    check(RepSharedFragment);
    ACrowdDemoMassClientBubbleInfo& BubbleInfo =
      RepSharedFragment->GetTypedClientBubbleInfoChecked<ACrowdDemoMassClientBubbleInfo>(ClientHandle);
    BubbleInfo.GetCrowdDemoSerializer().Bubble.RemoveAgentChecked(Handle);
  };

  CalculateClientReplication<FCrowdDemoMassFastArrayItem>(
    Context,
    ReplicationContext,
    CacheViewsCallback,
    AddEntityCallback,
    ModifyEntityCallback,
    RemoveEntityCallback);
#endif
}
