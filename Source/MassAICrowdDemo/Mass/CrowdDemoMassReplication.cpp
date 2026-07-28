#include "Mass/CrowdDemoMassReplication.h"

#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoRelevantSnapshotAdapter.h"
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
    TConstArrayView<FCrowdDemoMassStatsFragment> Stats,
    TConstArrayView<FCrowdDemoBusinessStateFragment> Businesses,
    TConstArrayView<FCrowdDemoRangedAttackFragment> Attacks,
    TConstArrayView<FCrowdDemoReactiveMotionFragment> Reactives,
    TConstArrayView<FCrowdDemoHitFlashFragment> HitFlashes,
    TConstArrayView<FCrowdDemoMassMovementFragment> Movements,
    TConstArrayView<FCrowdDemoMassVisualFragment> Visuals,
    TConstArrayView<FTransformFragment> Transforms,
    TConstArrayView<FMassVelocityFragment> Velocities)
  {
    OutAgent.SetNetID(NetworkIDs[EntityIndex].NetID);
    OutAgent.SetTemplateID(TemplateIDs[EntityIndex].ID);
    OutAgent.NetworkIdValue = NetworkIDs[EntityIndex].NetID.GetValue();

    const FCrowdDemoMassIdentityFragment& Identity = Identities[EntityIndex];
    const FCrowdDemoMassStatsFragment& StatsFragment = Stats[EntityIndex];
    const FCrowdDemoBusinessStateFragment& Business = Businesses[EntityIndex];
    const FCrowdDemoRangedAttackFragment& Attack = Attacks[EntityIndex];
    const FCrowdDemoReactiveMotionFragment& Reactive = Reactives[EntityIndex];
    const FCrowdDemoHitFlashFragment& HitFlash = HitFlashes[EntityIndex];
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
    FCrowdDemoCombatNetState& Combat = OutAgent.Combat;
    Combat.Health = StatsFragment.Health;
    Combat.MaxHealth = StatsFragment.MaxHealth;
    Combat.LifecycleState = StatsFragment.LifecycleState;
    Combat.bAlive = StatsFragment.bAlive ? 1 : 0;
    Combat.BusinessState = Business.State;
    Combat.BusinessStateRevision = Business.StateRevision;
    Combat.BusinessStateEnterFixedStep = Business.StateEnterFixedStep;
    Combat.TargetAgentId = Business.TargetAgentId;
    Combat.TargetLifecycleSerial = Business.TargetLifecycleSerial;
    Combat.AttackPhase = Attack.Phase;
    Combat.AttackPhaseEnterFixedStep = Attack.PhaseEnterFixedStep;
    Combat.CooldownEndFixedStep = Attack.CooldownEndFixedStep;
    Combat.LockedTargetAgentId = Attack.LockedTargetAgentId;
    Combat.LockedTargetLifecycleSerial = Attack.LockedTargetLifecycleSerial;
    Combat.LockedTargetLocation = Attack.LockedTargetLocation;
    Combat.FireSequence = Attack.FireSequence;
    Combat.bFireRequestIssued = Attack.bFireRequestIssued ? 1 : 0;
    Combat.ReactiveMode = Reactive.Mode;
    Combat.HorizontalReactiveVelocity = Reactive.HorizontalVelocity;
    Combat.VerticalReactiveVelocityCmps = Reactive.VerticalVelocityCmps;
    Combat.ReactiveStartFixedStep = Reactive.StartFixedStep;
    Combat.ReactiveEndFixedStep = Reactive.EndFixedStep;
    Combat.ReactiveRevision = Reactive.ReactiveRevision;
    Combat.RestoreBusinessState = Reactive.RestoreBusinessState;
    Combat.ApexCount = Reactive.ApexCount;
    Combat.LandingCount = Reactive.LandingCount;
    Combat.HitFlashRevision = HitFlash.FlashRevision;
    Combat.HitFlashStartServerTimeSeconds = HitFlash.StartServerTimeSeconds;
    Combat.HitFlashDurationSeconds = HitFlash.DurationSeconds;
    Combat.HitFlashProfileKey = HitFlash.ProfileKey;
    Combat.HitFlashPeakIntensity = HitFlash.PeakIntensity;
    Combat.LastConsumedHitEventId = Business.LastConsumedHitEventId;
    Combat.VisualState = Visual.VisualState;
    Combat.VisualRevision = Visual.VisualRevision;
    Combat.VisualStateStartServerTimeSeconds = Visual.StateStartServerTimeSeconds;
    Combat.VisualPhaseSeed = Visual.PhaseSeed;
    OutAgent.ServerSampleTimeSeconds = ServerSampleTimeSeconds;
  }

  bool SameCombatState(const FCrowdDemoCombatNetState& A, const FCrowdDemoCombatNetState& B)
  {
    return FMath::IsNearlyEqual(A.Health, B.Health, 0.01f)
      && FMath::IsNearlyEqual(A.MaxHealth, B.MaxHealth, 0.01f)
      && A.LifecycleState == B.LifecycleState && A.bAlive == B.bAlive
      && A.BusinessState == B.BusinessState
      && A.BusinessStateRevision == B.BusinessStateRevision
      && A.BusinessStateEnterFixedStep == B.BusinessStateEnterFixedStep
      && A.TargetAgentId == B.TargetAgentId
      && A.TargetLifecycleSerial == B.TargetLifecycleSerial
      && A.AttackPhase == B.AttackPhase
      && A.AttackPhaseEnterFixedStep == B.AttackPhaseEnterFixedStep
      && A.CooldownEndFixedStep == B.CooldownEndFixedStep
      && A.LockedTargetAgentId == B.LockedTargetAgentId
      && A.LockedTargetLifecycleSerial == B.LockedTargetLifecycleSerial
      && FVector(A.LockedTargetLocation).Equals(FVector(B.LockedTargetLocation), 1.0f)
      && A.FireSequence == B.FireSequence
      && A.bFireRequestIssued == B.bFireRequestIssued
      && A.ReactiveMode == B.ReactiveMode
      && FVector(A.HorizontalReactiveVelocity).Equals(FVector(B.HorizontalReactiveVelocity), 1.0f)
      && FMath::IsNearlyEqual(A.VerticalReactiveVelocityCmps, B.VerticalReactiveVelocityCmps, 1.0f)
      && A.ReactiveStartFixedStep == B.ReactiveStartFixedStep
      && A.ReactiveEndFixedStep == B.ReactiveEndFixedStep
      && A.ReactiveRevision == B.ReactiveRevision
      && A.RestoreBusinessState == B.RestoreBusinessState
      && A.ApexCount == B.ApexCount && A.LandingCount == B.LandingCount
      && A.HitFlashRevision == B.HitFlashRevision
      && FMath::IsNearlyEqual(A.HitFlashStartServerTimeSeconds, B.HitFlashStartServerTimeSeconds, 0.001f)
      && FMath::IsNearlyEqual(A.HitFlashDurationSeconds, B.HitFlashDurationSeconds, 0.001f)
      && A.HitFlashProfileKey == B.HitFlashProfileKey
      && FMath::IsNearlyEqual(A.HitFlashPeakIntensity, B.HitFlashPeakIntensity, 0.001f)
      && A.LastConsumedHitEventId == B.LastConsumedHitEventId
      && A.VisualState == B.VisualState
      && A.VisualRevision == B.VisualRevision
      && FMath::IsNearlyEqual(A.VisualStateStartServerTimeSeconds, B.VisualStateStartServerTimeSeconds, 0.001f)
      && A.VisualPhaseSeed == B.VisualPhaseSeed;
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
      || Existing.VatPlayRateByte != Next.VatPlayRateByte
      || !SameCombatState(Existing.Combat, Next.Combat);
  }
}

bool FReplicatedCrowdDemoAgent::NetSerialize(
  FArchive& Ar,
  UPackageMap* Map,
  bool& bOutSuccess)
{
  static_cast<void>(Map);
  constexpr uint32 ContractVersion = 2;
  constexpr uint32 PositionBits = 20;
  constexpr int32 PositionBias = 1 << (PositionBits - 1);
  constexpr uint32 YawBits = 12;
  constexpr uint32 TimeBits = 20;
  constexpr uint32 MaxCombatPayloadBytes = 4095;
  bOutSuccess = false;

  uint32 Version = ContractVersion;
  Ar.SerializeBits(&Version, 3);
  if (Ar.IsLoading() && Version != ContractVersion)
    return false;

  uint32 VisualValue = Ar.IsSaving()
    ? static_cast<uint32>(VisualId) : 0;
  if (Ar.IsSaving()
    && (VisualId < 0 || VisualValue > MAX_uint16))
    return false;
  Ar.SerializeBits(&VisualValue, 16);

  const uint32 DerivedNetworkId = VisualValue + 1u;
  uint32 NetworkValue = Ar.IsSaving()
    ? NetworkIdValue : DerivedNetworkId;
  uint32 bExplicitNetworkId = Ar.IsSaving()
    ? (NetworkValue != DerivedNetworkId ? 1u : 0u) : 0u;
  Ar.SerializeBits(&bExplicitNetworkId, 1);
  if (bExplicitNetworkId != 0)
    Ar.SerializeBits(&NetworkValue, 32);
  else
    NetworkValue = DerivedNetworkId;
  if (NetworkValue == 0)
    return false;

  uint32 LifecycleValue = Ar.IsSaving()
    ? static_cast<uint32>(LifecycleSerial) : 0;
  if (Ar.IsSaving()
    && (LifecycleSerial <= 0
      || LifecycleValue > MAX_uint16))
    return false;
  Ar.SerializeBits(&LifecycleValue, 16);

  FVector Position = PositionYaw.GetPosition();
  for (int32 Axis = 0; Axis < 3; ++Axis)
  {
    int32 Quantized = Ar.IsSaving()
      ? FMath::RoundToInt(Position[Axis]) : 0;
    if (Ar.IsSaving()
      && (Quantized < -PositionBias
        || Quantized >= PositionBias))
      return false;
    uint32 Packed = Ar.IsSaving()
      ? static_cast<uint32>(Quantized + PositionBias) : 0;
    Ar.SerializeBits(&Packed, PositionBits);
    if (Ar.IsLoading())
      Position[Axis] =
        static_cast<float>(
          static_cast<int32>(Packed) - PositionBias);
  }

  uint32 YawValue = Ar.IsSaving()
    ? static_cast<uint32>(FMath::RoundToInt(
        FMath::Fmod(
          FMath::RadiansToDegrees(PositionYaw.GetYaw())
            + 360.0f,
          360.0f)
        * static_cast<float>((1u << YawBits) - 1u)
        / 360.0f))
    : 0;
  Ar.SerializeBits(&YawValue, YawBits);

  uint32 TimeValue = Ar.IsSaving()
    ? static_cast<uint32>(FMath::Clamp(
        FMath::RoundToInt(ServerSampleTimeSeconds * 100.0f),
        0,
        static_cast<int32>((1u << TimeBits) - 1u)))
    : 0;
  Ar.SerializeBits(&TimeValue, TimeBits);

  const FVector VelocityValue = FVector(Velocity);
  uint32 bHasVelocity = Ar.IsSaving()
    ? (!VelocityValue.IsNearlyZero(0.5f) ? 1u : 0u) : 0u;
  Ar.SerializeBits(&bHasVelocity, 1);
  FVector DecodedVelocity = FVector::ZeroVector;
  if (bHasVelocity != 0)
  {
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
      int32 Quantized = Ar.IsSaving()
        ? FMath::RoundToInt(VelocityValue[Axis]) : 0;
      if (Ar.IsSaving()
        && (Quantized < MIN_int16
          || Quantized > MAX_int16))
        return false;
      uint32 Packed = Ar.IsSaving()
        ? static_cast<uint16>(static_cast<int16>(Quantized)) : 0;
      Ar.SerializeBits(&Packed, 16);
      if (Ar.IsLoading())
        DecodedVelocity[Axis] =
          static_cast<float>(
            static_cast<int16>(
              static_cast<uint16>(Packed)));
    }
  }

  uint32 bHasVisual = Ar.IsSaving()
    ? (AnimState != 0 || VatClipIndex != 0
      || VatPhaseByte != 0 || VatPlayRateByte != 128
      ? 1u : 0u)
    : 0u;
  Ar.SerializeBits(&bHasVisual, 1);
  uint32 VisualBytes = 0;
  if (Ar.IsSaving())
  {
    VisualBytes = static_cast<uint32>(AnimState)
      | (static_cast<uint32>(VatClipIndex) << 8)
      | (static_cast<uint32>(VatPhaseByte) << 16)
      | (static_cast<uint32>(VatPlayRateByte) << 24);
  }
  if (bHasVisual != 0)
    Ar.SerializeBits(&VisualBytes, 32);

  const FCrowdDemoCombatNetState DefaultCombat;
  uint32 bHasCombat = Ar.IsSaving()
    ? (!SameCombatState(Combat, DefaultCombat) ? 1u : 0u)
    : 0u;
  Ar.SerializeBits(&bHasCombat, 1);
  FCrowdDemoCombatNetState DecodedCombat;
  if (bHasCombat != 0)
  {
    TArray<uint8> CombatBytes;
    if (Ar.IsSaving())
    {
      FCrowdDemoRoundAgentState State;
      State.AgentId = static_cast<int32>(VisualValue);
      State.LifecycleSerial =
        static_cast<int32>(LifecycleValue);
      State.Location = Position;
      State.Velocity = VelocityValue;
      State.YawDegrees =
        FMath::RadiansToDegrees(PositionYaw.GetYaw());
      State.Combat = Combat;
      TArray<FCrowdRelevantSnapshotEntityPayload> Payloads;
      if (!FCrowdDemoRelevantSnapshotAdapter::EncodeAgents(
          MakeArrayView(&State, 1), Payloads)
        || Payloads.Num() != 1
        || Payloads[0].Bytes.Num()
          > static_cast<int32>(MaxCombatPayloadBytes))
        return false;
      CombatBytes = MoveTemp(Payloads[0].Bytes);
    }
    uint32 PayloadSize = Ar.IsSaving()
      ? static_cast<uint32>(CombatBytes.Num()) : 0;
    Ar.SerializeBits(&PayloadSize, 12);
    if (PayloadSize == 0
      || PayloadSize > MaxCombatPayloadBytes)
      return false;
    if (Ar.IsLoading())
      CombatBytes.SetNumUninitialized(
        static_cast<int32>(PayloadSize));
    Ar.Serialize(
      CombatBytes.GetData(),
      static_cast<int64>(PayloadSize));
    if (Ar.IsLoading())
    {
      FCrowdRelevantSnapshotEntityPayload Payload;
      Payload.Bytes = MoveTemp(CombatBytes);
      TArray<FCrowdDemoRoundAgentState> States;
      if (!FCrowdDemoRelevantSnapshotAdapter::DecodeAgents(
          MakeArrayView(&Payload, 1), States)
        || States.Num() != 1)
        return false;
      DecodedCombat = States[0].Combat;
    }
  }

  if (Ar.IsError())
    return false;
  if (Ar.IsLoading())
  {
    NetworkIdValue = NetworkValue;
    SetNetID(FMassNetworkID(NetworkIdValue));
    SetTemplateID(
      GetCrowdDemoReplicationTemplateIDForBubble());
    VisualId = static_cast<int32>(VisualValue);
    LifecycleSerial = static_cast<int32>(LifecycleValue);
    PositionYaw.SetPosition(Position);
    PositionYaw.SetYaw(FMath::DegreesToRadians(
      static_cast<float>(YawValue) * 360.0f
      / static_cast<float>((1u << YawBits) - 1u)));
    Velocity = FVector_NetQuantize10(DecodedVelocity);
    AnimState = static_cast<uint8>(VisualBytes & 0xffu);
    VatClipIndex =
      static_cast<uint8>((VisualBytes >> 8) & 0xffu);
    VatPhaseByte =
      static_cast<uint8>((VisualBytes >> 16) & 0xffu);
    VatPlayRateByte = bHasVisual != 0
      ? static_cast<uint8>((VisualBytes >> 24) & 0xffu)
      : 128;
    ServerSampleTimeSeconds =
      static_cast<float>(TimeValue) / 100.0f;
    Combat = bHasCombat != 0
      ? MoveTemp(DecodedCombat) : DefaultCombat;
  }
  bOutSuccess = true;
  return true;
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
    || Existing.VatPlayRateByte != Agent.VatPlayRateByte
    || !SameCombatState(Existing.Combat, Agent.Combat);
  if (!bChanged)
  {
    return false;
  }

  Existing.VisualId = Agent.VisualId;
  Existing.LifecycleSerial = Agent.LifecycleSerial;
  Existing.AnimState = Agent.AnimState;
  Existing.VatClipIndex = Agent.VatClipIndex;
  Existing.VatPlayRateByte = Agent.VatPlayRateByte;
  Existing.Combat = Agent.Combat;
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
    Query.AddRequirement<FCrowdDemoClientAuthorityFragment>(EMassFragmentAccess::ReadWrite);
    Query.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadWrite);
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
  FCrowdDemoClientAuthorityFragment& Authority = EntityView.GetFragmentData<FCrowdDemoClientAuthorityFragment>();
  FCrowdDemoRoundSimStateFragment& RoundSimState = EntityView.GetFragmentData<FCrowdDemoRoundSimStateFragment>();
  const UWorld* World = Serializer ? Serializer->GetEntityManagerChecked().GetWorld() : nullptr;

  Transform.GetMutableTransform().SetLocation(NewLocation);
  Transform.GetMutableTransform().SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(NewYawDegrees)));
  Velocity.Value = NewVelocity;

  Identity.Id = Agent.VisualId;
  Identity.VisualId = Agent.VisualId;
  Identity.LifecycleSerial = Agent.LifecycleSerial;

  // Replication is an authority sample and visual input, not a second writer
  // for deterministic client gameplay state. RoundPlan/correction boundaries
  // initialize or restore the local combat and movement fragments; overwriting
  // them here between fixed steps makes reactive motion depend on packet timing.

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
  Authority.Combat = Agent.Combat;
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
  EntityQuery.AddRequirement<FCrowdDemoMassStatsFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoBusinessStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRangedAttackFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoReactiveMotionFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoHitFlashFragment>(EMassFragmentAccess::ReadOnly);
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
  TConstArrayView<FCrowdDemoMassStatsFragment> Stats;
  TConstArrayView<FCrowdDemoBusinessStateFragment> Businesses;
  TConstArrayView<FCrowdDemoRangedAttackFragment> Attacks;
  TConstArrayView<FCrowdDemoReactiveMotionFragment> Reactives;
  TConstArrayView<FCrowdDemoHitFlashFragment> HitFlashes;
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
    Stats = InContext.GetFragmentView<FCrowdDemoMassStatsFragment>();
    Businesses = InContext.GetFragmentView<FCrowdDemoBusinessStateFragment>();
    Attacks = InContext.GetFragmentView<FCrowdDemoRangedAttackFragment>();
    Reactives = InContext.GetFragmentView<FCrowdDemoReactiveMotionFragment>();
    HitFlashes = InContext.GetFragmentView<FCrowdDemoHitFlashFragment>();
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
      Stats,
      Businesses,
      Attacks,
      Reactives,
      HitFlashes,
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
      Stats,
      Businesses,
      Attacks,
      Reactives,
      HitFlashes,
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
