#include "CrowdDemoMixedSandboxCoordinator.h"
#include "Mass/CrowdDemoWorkerCombatExtension.h"
#include "Mass/CrowdDemoWorkerInputSync.h"

#include "Camera/CameraActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "CrowdDemoBusinessSourceProvider.h"
#include "CrowdDemoPlanningRuntimeHost.h"
#include "CrowdDemoReplicator.h"
#include "CrowdDemoSourceStatePublisher.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HighResScreenshot.h"
#include "MassCommonFragments.h"
#include "Mass/CrowdDemoPresentationAdapter.h"
#include "Mass/CrowdDemoProjectileAdapters.h"
#include "MassCrowdPresentationSubsystem.h"
#include "MassCrowdBoundaryWorkGraph.h"
#include "MassCrowdFacingFinalizeWork.h"
#include "MassCrowdMovementFinalizeWork.h"
#include "MassCrowdMovementPipelineWork.h"
#include "MassCrowdParticlePipelineWork.h"
#include "MassCrowdReplicationActor.h"
#include "MassCrowdRuntimeFragments.h"
#include "MassCrowdRuntimeSubsystem.h"
#include "MassEntitySubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

namespace
{
  constexpr double MixedFixedStepSeconds = 1.0 / 30.0;
  constexpr double MixedMaximumFixedStepP95Ms = 66.667;
  constexpr int32 DefaultMixedPopulation = 20;
  constexpr int32 MaximumMixedPopulation = 500;
  constexpr int32 LifecycleIntervalSteps = 45;
  constexpr float MixedStartDelaySeconds = 5.0f;
  constexpr float MinimumSafeSeparationCm = 70.0f;
  constexpr float MixedInteractionRadiusCm = 900.0f;
  constexpr float MixedScaleInteractionRadiusCm = 2000.0f;
  constexpr int64 MixedProjectileFireFixedStep = 60;
  constexpr uint32 MixedProjectileProfileId = 9901;
  constexpr uint32 MixedProjectileCollisionProfileId = 9901;
  constexpr uint32 MixedProjectileEffectProfileId = 9901;
  constexpr uint32 MixedPresentationProfileKey = 1001;
  constexpr uint32 MixedProjectilePayloadTypeId = 9901;
  constexpr uint32 MixedProjectilePayloadSchemaId = 9901;
  constexpr uint32 MixedAgentPayloadVersion = 2;

  enum class EWorkerMixedCombatAuthorityMode : uint8
  {
    Shadow = 0,
    Canary,
    Production
  };

  bool ResolveWorkerMixedCombatAuthorityMode(
    EWorkerMixedCombatAuthorityMode& OutMode)
  {
    OutMode = EWorkerMixedCombatAuthorityMode::Shadow;
    FString Value;
    if (!FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdWorkerCombatMode="), Value))
      return true;
    if (Value.Equals(TEXT("Shadow"), ESearchCase::IgnoreCase))
      return true;
    if (Value.Equals(TEXT("Canary"), ESearchCase::IgnoreCase))
    {
      OutMode = EWorkerMixedCombatAuthorityMode::Canary;
      return true;
    }
    if (Value.Equals(TEXT("Production"), ESearchCase::IgnoreCase))
    {
      OutMode = EWorkerMixedCombatAuthorityMode::Production;
      return true;
    }
    return false;
  }

  struct FMixedProjectileDamagePayload
  {
    int32 Damage = 1;
  };

  static_assert(
    std::is_trivially_copyable_v<FMixedProjectileDamagePayload>);

  TArray<FCrowdDemoAttackProfileV1>
  BuildMixedCombatAttackProfiles()
  {
    TArray<FCrowdDemoAttackProfileV1> Profiles;
    FCrowdDemoAttackProfileV1& Melee =
      Profiles.AddDefaulted_GetRef();
    Melee.ProfileId = CrowdDemoAttackProfileIds::Melee;
    Melee.PayloadTypeId =
      CrowdDemoAttackPayloadTypeIds::Melee;
    Melee.EffectProfileId = 1;
    Melee.Archetype = ECrowdDemoAttackArchetype::Melee;
    Melee.WindupFixedSteps = 6;
    Melee.RecoveryFixedSteps = 8;
    Melee.CooldownFixedSteps = 18;
    Melee.MinimumDistanceCm = 0.0f;
    Melee.MaximumDistanceCm = 300.0f;
    Melee.QueryRadiusCm = 80.0f;
    Melee.MuzzleForwardOffsetCm = 42.0f;
    Melee.Damage = 20;

    FCrowdDemoAttackProfileV1& MidRange =
      Profiles.AddDefaulted_GetRef();
    MidRange.ProfileId =
      CrowdDemoAttackProfileIds::MidRange;
    MidRange.PayloadTypeId =
      CrowdDemoAttackPayloadTypeIds::MidRange;
    MidRange.EffectProfileId = 1;
    MidRange.Archetype =
      ECrowdDemoAttackArchetype::MidRange;
    MidRange.WindupFixedSteps = 10;
    MidRange.RecoveryFixedSteps = 10;
    MidRange.CooldownFixedSteps = 24;
    MidRange.MinimumDistanceCm = 300.0f;
    MidRange.MaximumDistanceCm = 650.0f;
    MidRange.QueryRadiusCm = 90.0f;
    MidRange.MuzzleForwardOffsetCm = 42.0f;
    MidRange.Damage = 20;

    FCrowdDemoAttackProfileV1& Ranged =
      Profiles.AddDefaulted_GetRef();
    Ranged.ProfileId = CrowdDemoAttackProfileIds::Ranged;
    Ranged.PayloadTypeId =
      CrowdDemoAttackPayloadTypeIds::Ranged;
    Ranged.EffectProfileId = 1;
    Ranged.Archetype = ECrowdDemoAttackArchetype::Ranged;
    Ranged.WindupFixedSteps = 15;
    Ranged.RecoveryFixedSteps = 12;
    Ranged.CooldownFixedSteps = 30;
    Ranged.MinimumDistanceCm = 600.0f;
    Ranged.MaximumDistanceCm = 1000.0f;
    Ranged.QueryRadiusCm = 12.0f;
    Ranged.MuzzleForwardOffsetCm = 70.0f;
    Ranged.ProjectileSpeedCmps = 1800.0f;
    Ranged.Damage = 20;
    return Profiles;
  }

  double DistanceSquaredToNavNode(
    const FVector& Location,
    const FCrowdNavSurfaceNode& Node)
  {
    const FVector Normal =
      Node.SurfaceNormal.GetSafeNormal();
    const double PlaneDistance =
      FVector::DotProduct(
        Location - Node.Vertices[0], Normal);
    const FVector Projected =
      Location - Normal * PlaneDistance;
    bool bInside = true;
    double WindingSign = 0.0;
    for (int32 Index = 0;
      Index < Node.Vertices.Num(); ++Index)
    {
      const FVector& A = Node.Vertices[Index];
      const FVector& B =
        Node.Vertices[
          (Index + 1) % Node.Vertices.Num()];
      const double Side = FVector::DotProduct(
        FVector::CrossProduct(B - A, Projected - A),
        Normal);
      if (FMath::Abs(Side)
        <= UE_DOUBLE_KINDA_SMALL_NUMBER)
        continue;
      if (WindingSign == 0.0)
        WindingSign = Side;
      else if ((Side > 0.0)
        != (WindingSign > 0.0))
      {
        bInside = false;
        break;
      }
    }
    if (bInside)
      return FMath::Square(PlaneDistance);

    double BestDistanceSquared =
      TNumericLimits<double>::Max();
    for (int32 Index = 0;
      Index < Node.Vertices.Num(); ++Index)
    {
      const FVector& A = Node.Vertices[Index];
      const FVector& B =
        Node.Vertices[
          (Index + 1) % Node.Vertices.Num()];
      BestDistanceSquared = FMath::Min(
        BestDistanceSquared,
        FVector::DistSquared(
          Location,
          FMath::ClosestPointOnSegment(
            Location, A, B)));
    }
    return BestDistanceSquared;
  }

  int32 ResolveMixedPopulation()
  {
    int32 Population = DefaultMixedPopulation;
    FParse::Value(
      FCommandLine::Get(),
      TEXT("CrowdDemoEntityCount="), Population);
    return FMath::Clamp(
      Population, 1, MaximumMixedPopulation);
  }

  ACrowdDemoReplicator* FindMixedVisualHost(UWorld& World)
  {
    for (TActorIterator<ACrowdDemoReplicator> It(&World); It; ++It)
    {
      if (*It && !It->IsLocalVisualHostOnly()) return *It;
    }
    return nullptr;
  }

  uint32 BehaviorBit(const ECrowdActiveBehavior Behavior)
  {
    const uint8 Index = static_cast<uint8>(Behavior);
    return Index < 32 ? uint32{1} << Index : 0;
  }

  uint64 FoldMixedHash(uint64 Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>(
        (Value >> (Byte * 8)) & 0xffull);
      Hash *= 1099511628211ull;
    }
    return Hash;
  }

  uint64 CalculateMixedHostFactHash(
    const FCrowdDemoMixedAgentState& State)
  {
    uint64 Hash = 14695981039346656037ull;
    Hash = FoldMixedHash(Hash, State.StableEntityId);
    Hash = FoldMixedHash(Hash, State.LifecycleSerial);
    Hash = FoldMixedHash(Hash, State.MembershipKey);
    Hash = FoldMixedHash(Hash, State.DerivedBehaviorLabel);
    Hash = FoldMixedHash(Hash, State.Health);
    Hash = FoldMixedHash(Hash, State.FactionId);
    Hash = FoldMixedHash(Hash, State.AttackProfileId);
    Hash = FoldMixedHash(Hash, State.AttackPhase);
    Hash = FoldMixedHash(
      Hash, static_cast<uint64>(
        State.AttackPhaseEnterFixedStep));
    Hash = FoldMixedHash(
      Hash, static_cast<uint64>(
        State.AttackCooldownEndFixedStep));
    Hash = FoldMixedHash(Hash, State.AttackFireSequence);
    Hash = FoldMixedHash(Hash, State.AttackTargetProviderId);
    Hash = FoldMixedHash(Hash, State.AttackTargetStableEntityId);
    Hash = FoldMixedHash(Hash, State.AttackTargetLifecycleSerial);
    Hash = FoldMixedHash(Hash, State.TargetProviderId);
    Hash = FoldMixedHash(Hash, State.TargetStableEntityId);
    Hash = FoldMixedHash(Hash, State.TargetLifecycleSerial);
    Hash = FoldMixedHash(Hash, State.TaskProviderId);
    Hash = FoldMixedHash(Hash, State.TaskStableEntityId);
    Hash = FoldMixedHash(Hash, State.TaskLifecycleSerial);
    Hash = FoldMixedHash(Hash, State.ProjectileExpectedCount);
    Hash = FoldMixedHash(Hash, State.ProjectileSpawnedCount);
    Hash = FoldMixedHash(Hash, State.ProjectileImpactCount);
    Hash = FoldMixedHash(Hash, State.ProjectileDamageCount);
    Hash = FoldMixedHash(Hash, State.ProjectileExpiredCount);
    Hash = FoldMixedHash(Hash, State.ProjectileActiveCount);
    Hash = FoldMixedHash(Hash, State.ProjectileDuplicateCount);
    Hash = FoldMixedHash(Hash, State.ProjectileTraceHash);
    Hash = FoldMixedHash(Hash, State.AttackIntentCount);
    Hash = FoldMixedHash(Hash, State.AttackImpactCount);
    Hash = FoldMixedHash(Hash, State.AttackDamageCount);
    Hash = FoldMixedHash(Hash, State.AttackDeathCount);
    Hash = FoldMixedHash(Hash, State.AttackTargetSwitchCount);
    Hash = FoldMixedHash(Hash, State.MeleeAttackIntentCount);
    Hash = FoldMixedHash(Hash, State.MidRangeAttackIntentCount);
    Hash = FoldMixedHash(Hash, State.RangedAttackIntentCount);
    return Hash;
  }

  void WriteU32(TArray<uint8>& Bytes, const uint32 Value)
  {
    for (int32 Byte = 0; Byte < 4; ++Byte)
      Bytes.Add(static_cast<uint8>((Value >> (Byte * 8)) & 0xffu));
  }

  void WriteU64(TArray<uint8>& Bytes, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
      Bytes.Add(static_cast<uint8>((Value >> (Byte * 8)) & 0xffull));
  }

  bool ReadU32(
    const TConstArrayView<uint8> Bytes, int32& Offset, uint32& Out)
  {
    if (Offset < 0 || Offset + 4 > Bytes.Num()) return false;
    Out = 0;
    for (int32 Byte = 0; Byte < 4; ++Byte)
      Out |= static_cast<uint32>(Bytes[Offset++]) << (Byte * 8);
    return true;
  }

  bool ReadU64(
    const TConstArrayView<uint8> Bytes, int32& Offset, uint64& Out)
  {
    if (Offset < 0 || Offset + 8 > Bytes.Num()) return false;
    Out = 0;
    for (int32 Byte = 0; Byte < 8; ++Byte)
      Out |= static_cast<uint64>(Bytes[Offset++]) << (Byte * 8);
    return true;
  }

  void EncodeMixedAgent(
    const FCrowdDemoMixedAgentState& State,
    const int64 FixedStepIndex,
    const uint64 LifecycleResumeSequence,
    const uint32 InRelevantSetRevision,
    TArray<uint8>& OutBytes)
  {
    OutBytes.Reset();
    WriteU32(OutBytes, MixedAgentPayloadVersion);
    WriteU64(OutBytes, static_cast<uint64>(FixedStepIndex));
    WriteU64(OutBytes, LifecycleResumeSequence);
    WriteU32(OutBytes, InRelevantSetRevision);
    WriteU64(OutBytes, State.StableEntityId);
    WriteU32(OutBytes, State.LifecycleSerial);
    WriteU32(OutBytes, State.MembershipKey);
    WriteU32(OutBytes, static_cast<uint32>(
      FMath::RoundToInt(State.Location.X * 10.0)));
    WriteU32(OutBytes, static_cast<uint32>(
      FMath::RoundToInt(State.Location.Y * 10.0)));
    WriteU32(OutBytes, static_cast<uint32>(
      FMath::RoundToInt(State.Location.Z * 10.0)));
    WriteU32(OutBytes, State.DerivedBehaviorLabel);
    OutBytes.Add(State.Health);
    WriteU32(OutBytes, State.FactionId);
    WriteU32(OutBytes, State.AttackProfileId);
    OutBytes.Add(State.AttackPhase);
    WriteU64(OutBytes, static_cast<uint64>(
      State.AttackPhaseEnterFixedStep));
    WriteU64(OutBytes, static_cast<uint64>(
      State.AttackCooldownEndFixedStep));
    WriteU32(OutBytes, State.AttackFireSequence);
    WriteU32(OutBytes, State.AttackTargetProviderId);
    WriteU64(OutBytes, State.AttackTargetStableEntityId);
    WriteU32(OutBytes, State.AttackTargetLifecycleSerial);
    WriteU32(OutBytes, State.TargetProviderId);
    WriteU64(OutBytes, State.TargetStableEntityId);
    WriteU32(OutBytes, State.TargetLifecycleSerial);
    WriteU32(OutBytes, State.TaskProviderId);
    WriteU64(OutBytes, State.TaskStableEntityId);
    WriteU32(OutBytes, State.TaskLifecycleSerial);
    WriteU32(OutBytes, static_cast<uint32>(
      State.ProjectileExpectedCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.ProjectileSpawnedCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.ProjectileImpactCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.ProjectileDamageCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.ProjectileExpiredCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.ProjectileActiveCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.ProjectileDuplicateCount));
    WriteU64(OutBytes, State.ProjectileTraceHash);
    WriteU32(OutBytes, static_cast<uint32>(
      State.AttackIntentCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.AttackImpactCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.AttackDamageCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.AttackDeathCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.AttackTargetSwitchCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.MeleeAttackIntentCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.MidRangeAttackIntentCount));
    WriteU32(OutBytes, static_cast<uint32>(
      State.RangedAttackIntentCount));
  }

  bool DecodeMixedAgent(
    const TConstArrayView<uint8> Bytes,
    FCrowdDemoMixedAgentState& OutState,
    int64& OutFixedStepIndex,
    uint64& OutLifecycleResumeSequence,
    uint32& OutRelevantSetRevision)
  {
    OutState = {};
    uint64 Step = 0;
    uint32 X = 0;
    uint32 Y = 0;
    uint32 Z = 0;
    uint32 ProjectileExpected = 0;
    uint32 ProjectileSpawned = 0;
    uint32 ProjectileImpacted = 0;
    uint32 ProjectileDamage = 0;
    uint32 ProjectileExpired = 0;
    uint32 ProjectileActive = 0;
    uint32 ProjectileDuplicates = 0;
    uint64 AttackPhaseEnterStep = 0;
    uint64 AttackCooldownEndStep = 0;
    uint32 AttackIntent = 0;
    uint32 AttackImpact = 0;
    uint32 AttackDamage = 0;
    uint32 AttackDeath = 0;
    uint32 AttackTargetSwitch = 0;
    uint32 MeleeAttackIntent = 0;
    uint32 MidRangeAttackIntent = 0;
    uint32 RangedAttackIntent = 0;
    uint32 PayloadVersion = 0;
    int32 Offset = 0;
    if (!ReadU32(Bytes, Offset, PayloadVersion)
      || PayloadVersion != MixedAgentPayloadVersion
      || !ReadU64(Bytes, Offset, Step)
      || !ReadU64(Bytes, Offset, OutLifecycleResumeSequence)
      || !ReadU32(Bytes, Offset, OutRelevantSetRevision)
      || !ReadU64(Bytes, Offset, OutState.StableEntityId)
      || !ReadU32(Bytes, Offset, OutState.LifecycleSerial)
      || !ReadU32(Bytes, Offset, OutState.MembershipKey)
      || !ReadU32(Bytes, Offset, X)
      || !ReadU32(Bytes, Offset, Y)
      || !ReadU32(Bytes, Offset, Z)
      || Offset + 5 > Bytes.Num())
      return false;
    if (!ReadU32(Bytes, Offset, OutState.DerivedBehaviorLabel))
      return false;
    OutState.Health = Bytes[Offset++];
    if (!ReadU32(Bytes, Offset, OutState.FactionId)
      || !ReadU32(Bytes, Offset, OutState.AttackProfileId)
      || Offset >= Bytes.Num())
      return false;
    OutState.AttackPhase = Bytes[Offset++];
    if (!ReadU64(Bytes, Offset, AttackPhaseEnterStep)
      || !ReadU64(Bytes, Offset, AttackCooldownEndStep)
      || !ReadU32(Bytes, Offset, OutState.AttackFireSequence)
      || !ReadU32(Bytes, Offset, OutState.AttackTargetProviderId)
      || !ReadU64(Bytes, Offset, OutState.AttackTargetStableEntityId)
      || !ReadU32(Bytes, Offset, OutState.AttackTargetLifecycleSerial)
      || !ReadU32(Bytes, Offset, OutState.TargetProviderId)
      || !ReadU64(Bytes, Offset, OutState.TargetStableEntityId)
      || !ReadU32(Bytes, Offset, OutState.TargetLifecycleSerial)
      || !ReadU32(Bytes, Offset, OutState.TaskProviderId)
      || !ReadU64(Bytes, Offset, OutState.TaskStableEntityId)
      || !ReadU32(Bytes, Offset, OutState.TaskLifecycleSerial)
      || !ReadU32(Bytes, Offset, ProjectileExpected)
      || !ReadU32(Bytes, Offset, ProjectileSpawned)
      || !ReadU32(Bytes, Offset, ProjectileImpacted)
      || !ReadU32(Bytes, Offset, ProjectileDamage)
      || !ReadU32(Bytes, Offset, ProjectileExpired)
      || !ReadU32(Bytes, Offset, ProjectileActive)
      || !ReadU32(Bytes, Offset, ProjectileDuplicates)
      || !ReadU64(Bytes, Offset, OutState.ProjectileTraceHash)
      || !ReadU32(Bytes, Offset, AttackIntent)
      || !ReadU32(Bytes, Offset, AttackImpact)
      || !ReadU32(Bytes, Offset, AttackDamage)
      || !ReadU32(Bytes, Offset, AttackDeath)
      || !ReadU32(Bytes, Offset, AttackTargetSwitch)
      || !ReadU32(Bytes, Offset, MeleeAttackIntent)
      || !ReadU32(Bytes, Offset, MidRangeAttackIntent)
      || !ReadU32(Bytes, Offset, RangedAttackIntent)
      || Offset != Bytes.Num())
      return false;
    OutFixedStepIndex = static_cast<int64>(Step);
    OutState.Location = FVector(
      static_cast<int32>(X) / 10.0,
      static_cast<int32>(Y) / 10.0,
      static_cast<int32>(Z) / 10.0);
    OutState.ProjectileExpectedCount =
      static_cast<int32>(ProjectileExpected);
    OutState.ProjectileSpawnedCount =
      static_cast<int32>(ProjectileSpawned);
    OutState.ProjectileImpactCount =
      static_cast<int32>(ProjectileImpacted);
    OutState.ProjectileDamageCount =
      static_cast<int32>(ProjectileDamage);
    OutState.ProjectileExpiredCount =
      static_cast<int32>(ProjectileExpired);
    OutState.ProjectileActiveCount =
      static_cast<int32>(ProjectileActive);
    OutState.ProjectileDuplicateCount =
      static_cast<int32>(ProjectileDuplicates);
    OutState.AttackPhaseEnterFixedStep =
      static_cast<int64>(AttackPhaseEnterStep);
    OutState.AttackCooldownEndFixedStep =
      static_cast<int64>(AttackCooldownEndStep);
    OutState.AttackIntentCount =
      static_cast<int32>(AttackIntent);
    OutState.AttackImpactCount =
      static_cast<int32>(AttackImpact);
    OutState.AttackDamageCount =
      static_cast<int32>(AttackDamage);
    OutState.AttackDeathCount =
      static_cast<int32>(AttackDeath);
    OutState.AttackTargetSwitchCount =
      static_cast<int32>(AttackTargetSwitch);
    OutState.MeleeAttackIntentCount =
      static_cast<int32>(MeleeAttackIntent);
    OutState.MidRangeAttackIntentCount =
      static_cast<int32>(MidRangeAttackIntent);
    OutState.RangedAttackIntentCount =
      static_cast<int32>(RangedAttackIntent);
    return true;
  }

  void EncodeLifecycleOperation(
    const FCrowdDemoContinuousLifecycleOperation& Operation,
    TArray<uint8>& OutBytes)
  {
    OutBytes.Reset();
    WriteU64(OutBytes, Operation.Sequence);
    WriteU64(OutBytes, static_cast<uint64>(Operation.FixedStepIndex));
    WriteU32(OutBytes, Operation.RelevantSetRevision);
    WriteU64(OutBytes, Operation.StableEntityId);
    WriteU32(OutBytes, Operation.LifecycleSerial);
    WriteU32(OutBytes, Operation.PreviousMembershipKey);
    WriteU32(OutBytes, Operation.NewMembershipKey);
    OutBytes.Add(static_cast<uint8>(Operation.Kind));
    OutBytes.Add(Operation.DespawnReason);
  }

  bool DecodeLifecycleOperation(
    const TConstArrayView<uint8> Bytes,
    FCrowdDemoContinuousLifecycleOperation& OutOperation)
  {
    OutOperation = {};
    uint64 Step = 0;
    int32 Offset = 0;
    if (!ReadU64(Bytes, Offset, OutOperation.Sequence)
      || !ReadU64(Bytes, Offset, Step)
      || !ReadU32(Bytes, Offset, OutOperation.RelevantSetRevision)
      || !ReadU64(Bytes, Offset, OutOperation.StableEntityId)
      || !ReadU32(Bytes, Offset, OutOperation.LifecycleSerial)
      || !ReadU32(Bytes, Offset, OutOperation.PreviousMembershipKey)
      || !ReadU32(Bytes, Offset, OutOperation.NewMembershipKey)
      || Offset + 2 != Bytes.Num())
      return false;
    OutOperation.Kind =
      static_cast<ECrowdDemoContinuousLifecycleOperationKind>(
        Bytes[Offset++]);
    OutOperation.DespawnReason = Bytes[Offset++];
    OutOperation.FixedStepIndex = static_cast<int64>(Step);
    return static_cast<uint8>(OutOperation.Kind)
      <= static_cast<uint8>(
        ECrowdDemoContinuousLifecycleOperationKind::Membership);
  }

}

ACrowdDemoMixedSandboxCoordinator::ACrowdDemoMixedSandboxCoordinator()
{
  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.TickGroup = TG_PostUpdateWork;
  PrimaryActorTick.EndTickGroup = TG_PostUpdateWork;
  bReplicates = true;
  bAlwaysRelevant = true;
  SetNetUpdateFrequency(30.0f);
  SetMinNetUpdateFrequency(15.0f);
}

void ACrowdDemoMixedSandboxCoordinator::BeginPlay()
{
  Super::BeginPlay();
  bMixedCombatIntegration = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoMixedCombatIntegration"));
  bCaptureRequested = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoCaptureMixedSandbox"));
  if (UWorld* World = GetWorld())
    if (UMassCrowdRuntimeSubsystem* Runtime =
      World->GetSubsystem<UMassCrowdRuntimeSubsystem>())
      BehaviorSourceRuntime =
        &Runtime->GetBehaviorSourceRuntime();
  if (HasAuthority()) TryInitializeServer();
}

void ACrowdDemoMixedSandboxCoordinator::EndPlay(
  const EEndPlayReason::Type EndPlayReason)
{
  if (PendingMixedMovement.IsValid()
    && PendingMixedMovement->Finalize)
  {
    TUniqueFunction<void(bool, int32, uint64)> Finalize =
      MoveTemp(PendingMixedMovement->Finalize);
    PendingMixedMovement.Reset();
    Finalize(false, 0, 0);
  }
  if (UWorld* World = GetWorld())
  {
    if (HasAuthority())
    {
      if (UMassEntitySubsystem* EntitySubsystem =
        World->GetSubsystem<UMassEntitySubsystem>())
      {
        ProjectileStore.DestroyAll(
          EntitySubsystem->GetMutableEntityManager());
      }
    }
    if (UMassCrowdRuntimeSubsystem* Runtime =
      World->GetSubsystem<UMassCrowdRuntimeSubsystem>())
    {
      for (const TPair<uint64, FCrowdNavFlowHandle>& Pair
        : FlowHandleByGoalNode)
        Runtime->ReleaseFlow(Pair.Value);
      if (BehaviorSourceRuntime)
      {
        for (const FSlotState& Slot : Slots)
        {
          if (Slot.Facts.StableEntityRef.IsValid())
            BehaviorSourceRuntime->RemoveEntity(
              Slot.Facts.StableEntityRef);
        }
      }
    }
  }
  BehaviorSourceRuntime = nullptr;
  FlowHandleByGoalNode.Reset();
  NavGraphHandle.Reset();
  Super::EndPlay(EndPlayReason);
}

void ACrowdDemoMixedSandboxCoordinator::Tick(const float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);
  UWorld* World = GetWorld();
  if (!World) return;

  if (!HasAuthority())
  {
    ClientFrameMilliseconds.Add(FMath::Max(0.0f, DeltaSeconds) * 1000.0);
    if (ClientFrameMilliseconds.Num() > 2048) ClientFrameMilliseconds.RemoveAt(0, 512);
  }

  if (HasAuthority() && !bWorldInitialized
    && World->GetTimeSeconds() >= NextInitializationAttemptSeconds)
  {
    NextInitializationAttemptSeconds = World->GetTimeSeconds() + 1.0;
    TryInitializeServer();
  }
  if (!bWorldInitialized) return;
  if (HasAuthority())
    RefreshReplicationChannels();
  else
    ConsumeProductReplication();

  if (!HasAuthority() && bCaptureRequested && !bCaptureCompleted
    && CaptureAtWorldSeconds > 0.0
    && World->GetTimeSeconds() >= CaptureAtWorldSeconds)
  {
    FScreenshotRequest::RequestScreenshot(
      FPaths::ProjectSavedDir() / TEXT("StageJ_MixedSandbox_Visual.png"), false, false);
    bCaptureCompleted = true;
  }

  if (HasAuthority() && World->GetTimeSeconds() >= MixedStartDelaySeconds)
  {
    FixedStepAccumulatorSeconds += FMath::Max(DeltaSeconds, 0.0f);
    if (PendingMixedMovement.IsValid()
      && !PollProductMovementBoundary())
    {
      ++StaleRejectCount;
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=boundary_poll fixed_step=%lld"),
        FixedStepIndex);
    }
    int32 Steps = 0;
    while (!PendingMixedMovement.IsValid()
      && FixedStepAccumulatorSeconds >= MixedFixedStepSeconds && Steps < 8)
    {
      FixedStepAccumulatorSeconds -= MixedFixedStepSeconds;
      AdvanceServerFixedStep();
      ++Steps;
    }
  }

  if (!HasAuthority() && bVisualSyncPending) SyncClientVisualsIncremental();
  if (World->GetTimeSeconds() - LastCheckpointWorldSeconds >= 5.0)
  {
    LastCheckpointWorldSeconds = World->GetTimeSeconds();
    LogCheckpoint();
  }
  TryLogPass();
}

void ACrowdDemoMixedSandboxCoordinator::GetLifetimeReplicatedProps(
  TArray<FLifetimeProperty>& OutLifetimeProps) const
{
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);
  DOREPLIFETIME(ACrowdDemoMixedSandboxCoordinator, Config);
}

void ACrowdDemoMixedSandboxCoordinator::OnRep_Config()
{
  if (!HasAuthority() && Config.bValid != 0 && !bWorldInitialized)
  {
    if (!InitializeLifecycleWorld())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMixedSandbox role=client stage=initialize"));
    }
  }
}

bool ACrowdDemoMixedSandboxCoordinator::TryInitializeServer()
{
  if (!HasAuthority() || bWorldInitialized) return bWorldInitialized;
  UWorld* World = GetWorld();
  if (!World) return false;

  UMassCrowdRuntimeSubsystem* Runtime =
    World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!Runtime) return false;
  FCrowdNavSurfaceGraphBuildConfig BuildConfig;
  BuildConfig.MinPortalWidthCm = 70;
  Runtime->SetGraphBuildConfig(BuildConfig);
  if (!Runtime->BuildOrRefreshNavGraph()) return false;
  const FCrowdNavGraphResource& Resource = Runtime->GetNavGraphResource();
  if (!Resource.IsReady()) return false;
  NavGraphHandle = Resource.Graph;

  MarkerLocations.Reset();
  for (TActorIterator<ATargetPoint> It(World); It; ++It)
  {
    for (const FName Tag : It->Tags)
    {
      if (Tag.ToString().StartsWith(TEXT("CrowdNav")))
        MarkerLocations.Add(Tag, It->GetActorLocation());
    }
  }
  const FName Required[] = {
    TEXT("CrowdNavLower"), TEXT("CrowdNavHigh"), TEXT("CrowdNavRouteA"),
    TEXT("CrowdNavRouteB"), TEXT("CrowdNavGoal")};
  for (const FName Tag : Required)
    if (!MarkerLocations.Contains(Tag)) return false;

  Config = {};
  Config.bValid = 1;
  Config.bMixedCombatIntegration =
    bMixedCombatIntegration ? 1 : 0;
  Config.PopulationLimit = bMixedCombatIntegration
    ? 20 : ResolveMixedPopulation();
  Config.SnapshotRevision = 1;
  Config.RelevantSetRevision = 1;
  Config.NavTopologyHash = Resource.TopologyHash;
  if (!InitializeLifecycleWorld())
  {
    Config.bValid = 0;
    return false;
  }
  ForceNetUpdate();
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoMixedSandbox role=server stage=initialized active=%d nodes=%d topology_hash=%llu source=LifecycleBehaviorSurfaceFlow"),
    LifecycleWorld.GetActiveEntityCount(),
    NavGraphHandle->Nodes.Num(), NavGraphHandle->TopologyHash);
  return true;
}

bool ACrowdDemoMixedSandboxCoordinator::InitializeLifecycleWorld()
{
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>() : nullptr;
  if (!EntitySubsystem || !RuntimeSubsystem || Config.bValid == 0
    || Config.PopulationLimit <= 0
    || Config.PopulationLimit > MaximumMixedPopulation)
    return false;
  bMixedCombatIntegration =
    Config.bMixedCombatIntegration != 0;
  BehaviorSourceRuntime =
    &RuntimeSubsystem->GetBehaviorSourceRuntime();
  if (!BusinessPlannerRegistry.IsFrozen()
    && !FCrowdDemoBusinessPlannerRunner::BuildDefaultRegistry(
      BusinessPlannerRegistry))
    return false;

  FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
  const TArray<const UScriptStruct*> Types = {
    FCrowdMassAgentFragment::StaticStruct(),
    FCrowdMassBehaviorFragment::StaticStruct(),
    FCrowdMassMembershipFragment::StaticStruct(),
    FTransformFragment::StaticStruct(),
    FCrowdMassAgentTag::StaticStruct()};
  LifecycleArchetype = EntityManager.CreateArchetype(Types);
  if (!LifecycleArchetype.IsValid()) return false;

  Slots.SetNum(Config.PopulationLimit + 1);
  TArray<FCrowdLifecycleSnapshotEntity> Snapshot;
  Snapshot.Reserve(Config.PopulationLimit);
  for (int32 SlotIndex = 1; SlotIndex <= Config.PopulationLimit; ++SlotIndex)
  {
    InitializeSlotState(SlotIndex, 1);
    FCrowdCapabilityBinding Binding;
    Binding.ProfileKey =
      CrowdDemoBehaviorSchemas::FullProfile;
    if (!BehaviorSourceRuntime->RegisterEntity(
        Slots[SlotIndex].Facts.StableEntityRef, Binding))
      return false;
    Snapshot.Add({Slots[SlotIndex].Facts, Slots[SlotIndex].MembershipKey});
  }
  FCrowdLifecycleBatchLimits Limits;
  Limits.MaxSnapshotEntities = Config.PopulationLimit;
  Limits.MaxEntriesPerBatch = Config.PopulationLimit;
  Limits.MaxTrackedSlots = Config.PopulationLimit;
  Limits.MaxSequenceHistory = 256;
  if (!LifecycleWorld.InitializeFromSnapshot(
    EntityManager, LifecycleArchetype, Config.SnapshotRevision, 0,
    Config.RelevantSetRevision, Snapshot, Limits)) return false;

  for (int32 SlotIndex = 1; SlotIndex <= Config.PopulationLimit; ++SlotIndex)
  {
    FMassEntityHandle Entity;
    if (!LifecycleWorld.TryGetEntityHandle(Slots[SlotIndex].Facts.StableEntityRef, Entity))
      return false;
    FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity);
    if (!Transform) return false;
    Transform->GetMutableTransform().SetLocation(Slots[SlotIndex].Location);
  }

  FixedStepIndex = 0;
  ProjectileExpectedCount =
    bMixedCombatIntegration
    ? 128 : FMath::Max(1, Config.PopulationLimit / 5);
  ProjectileSpawnedCount = 0;
  ProjectileImpactCount = 0;
  ProjectileDamageCount = 0;
  ProjectileExpiredCount = 0;
  ProjectileActiveCount = 0;
  ProjectileDuplicateCount = 0;
  ProjectileTraceHash = 14695981039346656037ull;
  bProjectileBatchSpawned = false;
  AttackIntentCount = 0;
  AttackImpactCount = 0;
  AttackDamageCount = 0;
  AttackDeathCount = 0;
  AttackTargetSwitchCount = 0;
  MeleeAttackIntentCount = 0;
  MidRangeAttackIntentCount = 0;
  RangedAttackIntentCount = 0;
  if (HasAuthority()
    && !ProjectileStore.EnsureCapacity(
      EntityManager, ProjectileExpectedCount,
      ProjectileExpectedCount))
    return false;
  NextLifecycleSequence = 1;
  RelevantSetRevision = Config.RelevantSetRevision;
  MaxObservedPopulation = Config.PopulationLimit;
  LastExpectedEntitySetHash = LifecycleWorld.CalculateEntitySetHash();
  LastExpectedMembershipHash = LifecycleWorld.CalculateMembershipHash();
  bWorldInitialized = true;
  bVisualSyncPending = !HasAuthority();
  return true;
}

void ACrowdDemoMixedSandboxCoordinator::InitializeSlotState(
  const int32 SlotIndex,
  const uint32 LifecycleSerial)
{
  FSlotState& Slot = Slots[SlotIndex];
  Slot = {};
  Slot.Facts = MakeAgentFacts(SlotIndex, LifecycleSerial);
  Slot.MembershipKey = MembershipForDiagnosticLabel(
    static_cast<ECrowdActiveBehavior>(Slot.Facts.DerivedBehaviorLabel));
  Slot.Health = 100;
  Slot.bActive = true;
  Slot.TransitionRevision = 1;
  if (bMixedCombatIntegration)
  {
    Slot.PlannerAssignment.PlannerId =
      CrowdDemoBusinessPlanners::MixedCombat;
    Slot.PlannerAssignment.CohortId =
      SlotIndex <= 10 ? 1 : 2;
    Slot.PlannerAssignment.Ordinal =
      static_cast<uint16>((SlotIndex - 1) % 10);
    Slot.FactionId = SlotIndex <= 10 ? 1 : 2;
    const int32 TeamOrdinal = (SlotIndex - 1) % 10;
    Slot.AttackProfileId = TeamOrdinal < 4
      ? CrowdDemoAttackProfileIds::Melee
      : TeamOrdinal < 6
        ? CrowdDemoAttackProfileIds::MidRange
        : CrowdDemoAttackProfileIds::Ranged;
    Slot.Facts.FactionKey = Slot.FactionId;
  }
  else if (!FCrowdDemoBusinessPlannerRunner::BuildMixedAssignment(
      SlotIndex, Slot.PlannerAssignment))
  {
    Slot.bActive = false;
    return;
  }

  if (!NavGraphHandle.IsValid() || NavGraphHandle->Nodes.IsEmpty())
  {
    Slot.Location = FVector((SlotIndex % 5) * 180.0f, (SlotIndex / 5) * 180.0f, 60.0f);
    return;
  }

  const FCrowdNavSurfaceGraph& Graph = *NavGraphHandle;
  const FCrowdNavSurfaceFlow* GoalFlow = nullptr;
  GetOrBuildFlow(Marker(TEXT("CrowdNavGoal"), Graph.Nodes[0].Center), GoalFlow);
  if (Config.PopulationLimit > DefaultMixedPopulation)
  {
    constexpr double ScaleSpawnSpacingCm = 120.0;
    FBox GroundBounds(EForceInit::ForceInit);
    for (const FCrowdNavSurfaceNode& Node : Graph.Nodes)
      if (Node.Center.Z < 200.0)
        GroundBounds += Node.Center;
    if (GroundBounds.IsValid)
    {
      const int32 ColumnCount = FMath::Max(
        1, FMath::FloorToInt(
          (GroundBounds.Max.X - GroundBounds.Min.X)
          / ScaleSpawnSpacingCm) + 1);
      const int32 RowCount = FMath::Max(
        1, FMath::FloorToInt(
          (GroundBounds.Max.Y - GroundBounds.Min.Y)
          / ScaleSpawnSpacingCm) + 1);
      const int32 CellCount = ColumnCount * RowCount;
      for (int32 Offset = 0; Offset < CellCount; ++Offset)
      {
        const int32 Cell =
          (SlotIndex - 1 + Offset) % CellCount;
        FVector Candidate(
          GroundBounds.Min.X
            + static_cast<double>(Cell % ColumnCount)
              * ScaleSpawnSpacingCm,
          GroundBounds.Min.Y
            + static_cast<double>(Cell / ColumnCount)
              * ScaleSpawnSpacingCm,
          0.0);
        uint64 NodeId = 0;
        uint32 NavLayer = 0;
        if (!FCrowdNavSurfaceGraphKernel::AttachClosest(
            Graph, Candidate, 350.0f, NodeId, NavLayer))
          continue;
        const int32 NodeIndex = Graph.FindNodeIndex(NodeId);
        if (!Graph.Nodes.IsValidIndex(NodeIndex)
          || (GoalFlow
            && GoalFlow->Nodes[NodeIndex].IntegrationCostQ
              == MAX_uint32))
          continue;
        Candidate.Z = Graph.Nodes[NodeIndex].Center.Z;
        bool bSeparated = true;
        for (int32 Other = 1; Other < Slots.Num(); ++Other)
        {
          if (Other != SlotIndex
            && Slots[Other].bActive
            && FVector::Distance(
              Candidate, Slots[Other].Location)
              < ScaleSpawnSpacingCm)
          {
            bSeparated = false;
            break;
          }
        }
        if (bSeparated)
        {
          Slot.Location = Candidate;
          Slot.AttachedNavNodeId = NodeId;
          Slot.InteractionLayer = NavLayer;
          return;
        }
      }
    }
  }
  const int32 Start = (SlotIndex * 17) % Graph.Nodes.Num();
  for (int32 Offset = 0; Offset < Graph.Nodes.Num(); ++Offset)
  {
    const int32 NodeIndex = (Start + Offset) % Graph.Nodes.Num();
    if (GoalFlow && GoalFlow->Nodes[NodeIndex].IntegrationCostQ == MAX_uint32) continue;
    for (int32 Ring = 0; Ring <= 3; ++Ring)
    {
      const int32 RingPoints = Ring == 0 ? 1 : Ring * 6;
      for (int32 RingPoint = 0;
        RingPoint < RingPoints; ++RingPoint)
      {
        FVector Candidate = Graph.Nodes[NodeIndex].Center;
        if (Ring > 0)
        {
          const double Angle =
            UE_TWO_PI * static_cast<double>(RingPoint)
            / static_cast<double>(RingPoints);
          Candidate += FVector(
            FMath::Cos(Angle), FMath::Sin(Angle), 0.0)
            * (80.0 * Ring);
        }
        bool bSeparated = true;
        for (int32 Other = 1;
          Other < Slots.Num(); ++Other)
        {
          if (Other != SlotIndex
            && Slots[Other].bActive
            && FVector::Distance(
              Candidate, Slots[Other].Location)
              < 160.0f)
          {
            bSeparated = false;
            break;
          }
        }
        if (bSeparated)
        {
          Slot.Location = Candidate;
          Slot.AttachedNavNodeId =
            Graph.Nodes[NodeIndex].StableNodeId;
          Slot.InteractionLayer =
            Graph.Nodes[NodeIndex].NavLayer;
          return;
        }
      }
    }
  }
  Slot.Location = Graph.Nodes[Start].Center
    + FVector(
      static_cast<double>(SlotIndex) * 80.0,
      0.0, 0.0);
  Slot.AttachedNavNodeId = Graph.Nodes[Start].StableNodeId;
  Slot.InteractionLayer = Graph.Nodes[Start].NavLayer;
}

FCrowdAgentFacts ACrowdDemoMixedSandboxCoordinator::MakeAgentFacts(
  const int32 SlotIndex,
  const uint32 LifecycleSerial) const
{
  FCrowdAgentFacts Facts;
  Facts.StableEntityRef = {1, static_cast<uint64>(SlotIndex), LifecycleSerial};
  Facts.FactionKey = static_cast<uint32>((SlotIndex % 3) + 1);
  Facts.CapabilitySet.Add(ECrowdCapability::Move);
  Facts.CapabilitySet.Add(ECrowdCapability::Wander);
  Facts.CapabilitySet.Add(ECrowdCapability::MoveTo);
  Facts.CapabilitySet.Add(ECrowdCapability::Pursue);
  Facts.CapabilitySet.Add(ECrowdCapability::Haul);
  Facts.CapabilitySet.Add(ECrowdCapability::Attack);
  Facts.CapabilitySet.Add(ECrowdCapability::Guard);
  Facts.CapabilitySet.Add(ECrowdCapability::Flee);
  Facts.CapabilitySet.Add(ECrowdCapability::UseNavLayer);
  Facts.DerivedBehaviorLabel =
    static_cast<uint32>(ECrowdActiveBehavior::Idle);
  Facts.MovementProfileKey = 1;
  Facts.PresentationProfileKey = 1;
  Facts.RuntimeState = 1;
  return Facts;
}

bool ACrowdDemoMixedSandboxCoordinator::PrepareProjectileBoundary(
  const TArray<FSlotState>& StagedSlots,
  FCrowdPreparedProjectileBoundary& OutPrepared,
  FCrowdPreparedHostHitCommit& OutHitCommit)
{
  OutPrepared = {};
  OutHitCommit = {};
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!HasAuthority() || !EntitySubsystem
    || ProjectileExpectedCount <= 0)
    return false;

  FCrowdProjectileBoundaryInput Input;
  Input.FixedStepIndex = FixedStepIndex;
  Input.ServerTimeSeconds =
    static_cast<float>(FixedStepIndex * MixedFixedStepSeconds);
  Input.FixedStepSeconds =
    static_cast<float>(MixedFixedStepSeconds);
  if (!ProjectileStore.Gather(
      EntitySubsystem->GetEntityManager(),
      Input.CurrentStates))
    return false;

  FCrowdProjectileProfile& Profile =
    Input.Profiles.AddDefaulted_GetRef();
  Profile.ProfileId = MixedProjectileProfileId;
  Profile.RadiusCm = 8.0f;
  Profile.LifetimeFixedSteps = 10;
  Profile.MaxActiveProjectiles = ProjectileExpectedCount;
  Profile.PositionQuantumCm = 1.0f;
  Profile.VelocityQuantumCmps = 1.0f;
  Profile.GridCellSizeCm = 128.0f;
  Profile.RecalculateStableHash();

  // An empty Mass store has no projectile work outside the single
  // deterministic fire Boundary. Still produce and validate a no-op prepared
  // transaction so the host retains one atomic Boundary contract without
  // rebuilding target snapshots on every fixed step.
  if (Input.CurrentStates.IsEmpty()
    && FixedStepIndex != MixedProjectileFireFixedStep)
  {
    OutPrepared.FixedStepIndex = FixedStepIndex;
    OutPrepared.BaseStateHash =
      FCrowdProjectileKernel::HashStates(
        Input.CurrentStates);
    OutPrepared.RecalculateStableHash();
    if (!FCrowdProjectileBoundaryPipeline::ValidatePrepared(
        Input, OutPrepared)
      || !ProjectileStore.ValidatePreparedStates(
        OutPrepared.States))
      return false;
    OutHitCommit.FixedStepIndex = FixedStepIndex;
    OutHitCommit.SourceResolveHash =
      OutPrepared.StableHash;
    OutHitCommit.RecalculateStableHash();
    return OutHitCommit.IsValid();
  }

  for (int32 ShooterOffset = 0;
    ShooterOffset < ProjectileExpectedCount; ++ShooterOffset)
  {
    const int32 TargetSlot =
      ProjectileExpectedCount + ShooterOffset + 1;
    if (!StagedSlots.IsValidIndex(TargetSlot)
      || !StagedSlots[TargetSlot].bActive)
      return false;
    const FSlotState& TargetSlotState =
      StagedSlots[TargetSlot];
    FCrowdProjectileTargetSnapshot& Target =
      Input.Targets.AddDefaulted_GetRef();
    Target.EntityRef =
      TargetSlotState.Facts.StableEntityRef;
    Target.FactionId =
      TargetSlotState.Facts.FactionKey;
    Target.NavLayer = TargetSlotState.InteractionLayer;
    Target.PreviousPosition =
      TargetSlotState.Location
      - TargetSlotState.Velocity * MixedFixedStepSeconds;
    Target.Position = TargetSlotState.Location;
    Target.RadiusCm = 35.0f;
    Target.bAlive = true;
    Target.RecalculateStableHash();

    if (!bProjectileBatchSpawned
      && FixedStepIndex == MixedProjectileFireFixedStep)
    {
      const int32 ShooterSlot = ShooterOffset + 1;
      if (!StagedSlots.IsValidIndex(ShooterSlot)
        || !StagedSlots[ShooterSlot].bActive)
        return false;
      FCrowdProjectileSpawnRequest& Request =
        Input.SpawnRequests.AddDefaulted_GetRef();
      Request.ProjectileId =
        0x504a000000000000ull
        | static_cast<uint64>(ShooterOffset + 1);
      Request.FixedStepIndex = FixedStepIndex;
      Request.Instigator =
        StagedSlots[ShooterSlot].Facts.StableEntityRef;
      Request.Target = Target.EntityRef;
      Request.FireSequence =
        static_cast<uint32>(ShooterOffset + 1);
      Request.NavLayer = Target.NavLayer;
      Request.ProjectileProfileId = MixedProjectileProfileId;
      Request.CollisionProfileId =
        MixedProjectileCollisionProfileId;
      Request.EffectProfileId =
        MixedProjectileEffectProfileId;
      Request.Position =
        Target.Position - FVector(0.0, 0.0, 180.0);
      Request.Velocity = FVector(0.0, 0.0, 3000.0);
      Request.RecalculateStableHash();
    }
  }

  if (!FCrowdProjectileBoundaryPipeline::Prepare(
      Input, OutPrepared)
    || !FCrowdProjectileBoundaryPipeline::ValidatePrepared(
      Input, OutPrepared)
    || !ProjectileStore.ValidatePreparedStates(
      OutPrepared.States))
    return false;

  TArray<FCrowdHitFact> Hits;
  uint64 SourceResolveHash = OutPrepared.StableHash;
  if (!OutPrepared.Impacts.IsEmpty())
  {
    FCrowdEffectProfile EffectProfile;
    EffectProfile.EffectProfileId =
      MixedProjectileEffectProfileId;
    EffectProfile.PayloadTypeId =
      MixedProjectilePayloadTypeId;
    const FMixedProjectileDamagePayload DamagePayload;
    if (!EffectProfile.Payload.Set(
        MixedProjectilePayloadSchemaId, DamagePayload))
      return false;
    EffectProfile.RecalculateStableHash();
    FCrowdHitResolveResult ResolveResult;
    if (!FCrowdCombatResolver::Resolve(
        OutPrepared.Impacts,
        MakeArrayView(&EffectProfile, 1),
        ResolveResult)
      || ResolveResult.FixedStepIndex != FixedStepIndex)
      return false;
    Hits = MoveTemp(ResolveResult.Hits);
    SourceResolveHash = ResolveResult.StableHash;
  }
  OutHitCommit.FixedStepIndex = FixedStepIndex;
  OutHitCommit.SourceResolveHash = SourceResolveHash;
  OutHitCommit.Hits = MoveTemp(Hits);
  OutHitCommit.RecalculateStableHash();
  return OutHitCommit.IsValid();
}

bool ACrowdDemoMixedSandboxCoordinator::BuildWorkerMixedCombatControl(
  const TArray<FSlotState>& InputSlots,
  FCrowdWorkerProjectileControlResource& OutControl)
{
  OutControl = {};
  if (!HasAuthority() || !bMixedCombatIntegration
    || NextWorkerMixedCombatControlRevision == 0
    || NextWorkerMixedCombatControlRevision
      == TNumericLimits<uint64>::Max())
    return false;
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!EntitySubsystem)
    return false;

  FCrowdDemoWorkerMixedCombatHostInput HostInput;
  HostInput.FixedStepIndex = FixedStepIndex;
  HostInput.FixedStepSeconds =
    static_cast<float>(MixedFixedStepSeconds);
  HostInput.Profiles = BuildMixedCombatAttackProfiles();
  for (int32 SlotIndex = 1;
    SlotIndex < InputSlots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = InputSlots[SlotIndex];
    if (!Slot.bActive)
      continue;
    FCrowdDemoWorkerMixedCombatAgent& Agent =
      HostInput.Agents.AddDefaulted_GetRef();
    Agent.EntityRef = Slot.Facts.StableEntityRef;
    Agent.FactionId = Slot.FactionId;
    Agent.NavLayer = Slot.InteractionLayer;
    Agent.AttackProfileId = Slot.AttackProfileId;
    Agent.Position = Slot.Location;
    Agent.Velocity = Slot.Velocity;
    Agent.Facing =
      FRotator(0.0f, Slot.YawDegrees, 0.0f).Vector();
    Agent.Health = Slot.Health;
    Agent.AttackState = Slot.AttackState;
  }
  HostInput.Agents.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  if (HostInput.Agents.IsEmpty())
    return false;

  OutControl.Revision =
    NextWorkerMixedCombatControlRevision;
  OutControl.AnchorEntity =
    HostInput.Agents[0].EntityRef;
  OutControl.bReplaceState =
    !bWorkerMixedCombatBootstrapped;
  OutControl.Input.FixedStepIndex = FixedStepIndex;
  OutControl.Input.ServerTimeSeconds =
    static_cast<float>(
      FixedStepIndex * MixedFixedStepSeconds);
  OutControl.Input.FixedStepSeconds =
    static_cast<float>(MixedFixedStepSeconds);
  FCrowdProjectileProfile& ProjectileProfile =
    OutControl.Input.Profiles.AddDefaulted_GetRef();
  ProjectileProfile.ProfileId =
    CrowdDemoProjectileSchemas::ProjectileProfileId;
  ProjectileProfile.RadiusCm = 12.0f;
  ProjectileProfile.LifetimeFixedSteps = 60;
  ProjectileProfile.MaxActiveProjectiles =
    ProjectileExpectedCount;
  ProjectileProfile.PositionQuantumCm = 1.0f;
  ProjectileProfile.VelocityQuantumCmps = 1.0f;
  ProjectileProfile.GridCellSizeCm = 256.0f;
  ProjectileProfile.RecalculateStableHash();
  if (OutControl.bReplaceState
    && !ProjectileStore.Gather(
      EntitySubsystem->GetEntityManager(),
      OutControl.Input.CurrentStates))
    return false;

  FCrowdDemoRangedCombatSettings DamageSettings;
  DamageSettings.bEnabled = 1;
  DamageSettings.Damage = 20.0f;
  OutControl.EffectProfiles.Add(
    FCrowdDemoProjectileAdapters::BuildEffectProfile(
      DamageSettings));
  if (!FCrowdDemoWorkerMixedCombatHostInputCodec::Encode(
      HostInput, OutControl.HostCombatInput))
    return false;
  return OutControl.IsValid();
}

bool ACrowdDemoMixedSandboxCoordinator::PrepareMixedCombatAttackPlan(
  TArray<FSlotState>& InOutSlots,
  TArray<FCrowdDemoAttackIntent>& OutIntents,
  FCrowdDemoAttackPlanSummary& OutSummary,
  int32& OutTargetSwitchCount)
{
  OutTargetSwitchCount = 0;
  TArray<FCrowdDemoAttackAgent> Agents;
  Agents.Reserve(InOutSlots.Num() - 1);
  for (int32 SlotIndex = 1;
    SlotIndex < InOutSlots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = InOutSlots[SlotIndex];
    if (!Slot.bActive) continue;
    FCrowdDemoAttackAgent& Agent =
      Agents.AddDefaulted_GetRef();
    Agent.EntityRef = Slot.Facts.StableEntityRef;
    Agent.FactionId = Slot.FactionId;
    Agent.NavLayer = Slot.InteractionLayer;
    Agent.AttackProfileId = Slot.AttackProfileId;
    Agent.Position = Slot.Location;
    Agent.Velocity = Slot.Velocity
      * static_cast<float>(MixedFixedStepSeconds);
    Agent.Facing =
      FRotator(0.0f, Slot.YawDegrees, 0.0f).Vector();
    Agent.Health = Slot.Health;
    Agent.bAlive = Slot.Health > 0;
    Agent.State = Slot.AttackState;
  }
  const TArray<FCrowdDemoAttackProfileV1> Profiles =
    BuildMixedCombatAttackProfiles();
  if (!FCrowdDemoAttackPlanner::Advance(
      9, FixedStepIndex, Profiles, Agents,
      OutIntents, OutSummary)
    || !OutSummary.bValid)
    return false;
  for (const FCrowdDemoAttackAgent& Agent : Agents)
  {
    const int32 SlotIndex =
      static_cast<int32>(Agent.EntityRef.StableEntityId);
    if (!InOutSlots.IsValidIndex(SlotIndex)
      || InOutSlots[SlotIndex].Facts.StableEntityRef
        != Agent.EntityRef)
      return false;
    FSlotState& Slot = InOutSlots[SlotIndex];
    if (Slot.AttackState.TargetRef.IsValid()
      && Slot.AttackState.TargetRef
        != Agent.State.TargetRef)
    {
      ++OutTargetSwitchCount;
      // The resolved target changed after a lifecycle invalidation. Drop the
      // cached goal so this Boundary rebuilds the TargetRegion/flow plan from
      // the new target rather than reusing the dead target's terminal region.
      Slot.CachedGoalNodeId = 0;
    }
    Slot.AttackState = Agent.State;
  }
  return true;
}

bool ACrowdDemoMixedSandboxCoordinator::PrepareMixedCombatBoundary(
  const TArray<FSlotState>& StagedSlots,
  const TConstArrayView<FCrowdDemoAttackIntent> Intents,
  FCrowdDemoPreparedAttackBoundary& OutAttack,
  FCrowdPreparedProjectileBoundary& OutProjectile,
  FCrowdDemoPreparedAttackHealthPatch& OutHealthPatch)
{
  OutAttack = {};
  OutProjectile = {};
  OutHealthPatch = {};
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!HasAuthority() || !EntitySubsystem)
    return false;

  TArray<FCrowdDemoAttackTargetSnapshot> AttackTargets;
  TArray<FCrowdProjectileTargetSnapshot> ProjectileTargets;
  TArray<FCrowdDemoAttackHealthState> HealthStates;
  for (int32 SlotIndex = 1;
    SlotIndex < StagedSlots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = StagedSlots[SlotIndex];
    if (!Slot.bActive) continue;
    const FVector PreviousPosition =
      Slot.Location - Slot.Velocity
        * static_cast<float>(MixedFixedStepSeconds);
    if (Slot.Health > 0)
    {
      FCrowdDemoAttackTargetSnapshot& AttackTarget =
        AttackTargets.AddDefaulted_GetRef();
      AttackTarget.Body.EntityRef = Slot.Facts.StableEntityRef;
      AttackTarget.Body.StartPosition = PreviousPosition;
      AttackTarget.Body.EndPosition = Slot.Location;
      AttackTarget.Body.RadiusCm = 42.0f;
      AttackTarget.Body.NavLayer = Slot.InteractionLayer;
      AttackTarget.Body.RecalculateStableHash();
      AttackTarget.FactionId = Slot.FactionId;
    }

    FCrowdProjectileTargetSnapshot& ProjectileTarget =
      ProjectileTargets.AddDefaulted_GetRef();
    ProjectileTarget.EntityRef = Slot.Facts.StableEntityRef;
    ProjectileTarget.FactionId = Slot.FactionId;
    ProjectileTarget.NavLayer = Slot.InteractionLayer;
    ProjectileTarget.PreviousPosition = PreviousPosition;
    ProjectileTarget.Position = Slot.Location;
    ProjectileTarget.RadiusCm = 42.0f;
    ProjectileTarget.bAlive = Slot.Health > 0;
    ProjectileTarget.RecalculateStableHash();

    HealthStates.Add({
      Slot.Facts.StableEntityRef,
      Slot.FactionId,
      Slot.Health,
      Slot.Health > 0});
  }
  const TArray<FCrowdSpatialEnvironmentBody> Environment;
  if (!FCrowdDemoAttackHostAdapter::Prepare(
      FixedStepIndex, Intents, AttackTargets,
      Environment, OutAttack))
    return false;

  FCrowdProjectileBoundaryInput ProjectileInput;
  ProjectileInput.FixedStepIndex = FixedStepIndex;
  ProjectileInput.ServerTimeSeconds =
    static_cast<float>(FixedStepIndex * MixedFixedStepSeconds);
  ProjectileInput.FixedStepSeconds =
    static_cast<float>(MixedFixedStepSeconds);
  ProjectileInput.SpawnRequests =
    OutAttack.ProjectileRequests;
  ProjectileInput.Targets = MoveTemp(ProjectileTargets);
  if (!ProjectileStore.Gather(
      EntitySubsystem->GetEntityManager(),
      ProjectileInput.CurrentStates))
    return false;
  FCrowdProjectileProfile& ProjectileProfile =
    ProjectileInput.Profiles.AddDefaulted_GetRef();
  ProjectileProfile.ProfileId =
    CrowdDemoProjectileSchemas::ProjectileProfileId;
  ProjectileProfile.RadiusCm = 12.0f;
  ProjectileProfile.LifetimeFixedSteps = 60;
  ProjectileProfile.MaxActiveProjectiles =
    ProjectileExpectedCount;
  ProjectileProfile.PositionQuantumCm = 1.0f;
  ProjectileProfile.VelocityQuantumCmps = 1.0f;
  ProjectileProfile.GridCellSizeCm = 256.0f;
  ProjectileProfile.RecalculateStableHash();
  if (!FCrowdProjectileBoundaryPipeline::Prepare(
      ProjectileInput, OutProjectile)
    || !FCrowdProjectileBoundaryPipeline::ValidatePrepared(
      ProjectileInput, OutProjectile)
    || !ProjectileStore.ValidatePreparedStates(
      OutProjectile.States))
    return false;

  TArray<FCrowdImpactFact> Impacts =
    OutAttack.ImmediateImpacts;
  Impacts.Append(OutProjectile.Impacts);
  FCrowdDemoRangedCombatSettings DamageSettings;
  DamageSettings.bEnabled = 1;
  DamageSettings.Damage = 20.0f;
  const FCrowdEffectProfile EffectProfile =
    FCrowdDemoProjectileAdapters::BuildEffectProfile(
      DamageSettings);
  FCrowdHitResolveResult ResolveResult;
  if (!FCrowdCombatResolver::Resolve(
      Impacts, MakeArrayView(&EffectProfile, 1),
      ResolveResult))
    return false;
  return FCrowdDemoAttackHostAdapter::PrepareHealthPatch(
    FixedStepIndex, ResolveResult.Hits,
    HealthStates, OutHealthPatch);
}

void ACrowdDemoMixedSandboxCoordinator::AdvanceServerFixedStep()
{
  const double StartSeconds = FPlatformTime::Seconds();
  double EvaluationEndSeconds = StartSeconds;
  double PrepareEndSeconds = StartSeconds;
  ++FixedStepIndex;
  if (!RebuildSpatialSafety())
  {
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=spatial_safety fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  TArray<FSlotState> StagedSlots = Slots;
  FCrowdWorkerProjectileControlResource
    WorkerMixedCombatControl;
  if (bMixedCombatIntegration
    && !BuildWorkerMixedCombatControl(
      StagedSlots, WorkerMixedCombatControl))
  {
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedCombat role=server stage=worker_control fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  FCrowdDemoBusinessCommitLedger StagedBusinessLedger =
    BusinessLedger;
  int32 StagedBehaviorTransitionCount =
    BehaviorTransitionCount;
  int32 StagedDuplicateCommitCount =
    DuplicateCommitCount;
  uint32 StagedSeenBehaviorBits = SeenBehaviorBits;
  int32 StagedPendingCombatDeathSlot =
    PendingCombatDeathSlot;
  const int32 OriginalPendingSourceCommandCount =
    BehaviorSourceRuntime->GetPendingCommandCount();
  TArray<FCrowdDemoAttackIntent> AttackIntents;
  FCrowdDemoAttackPlanSummary AttackPlanSummary;
  int32 StagedAttackTargetSwitches = 0;
  if (bMixedCombatIntegration
    && !PrepareMixedCombatAttackPlan(
      StagedSlots, AttackIntents, AttackPlanSummary,
      StagedAttackTargetSwitches))
  {
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedCombat role=server stage=attack_planner fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  FCrowdDemoPlannerDecisionBatch PlannerDecisionBatch;
  if (!PlanBusinessBoundary(
      StagedSlots, PlannerDecisionBatch))
  {
    BehaviorSourceRuntime->RollbackPendingCommandsTo(
      OriginalPendingSourceCommandCount);
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=planner fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  EvaluationEndSeconds = FPlatformTime::Seconds();
  FCrowdBehaviorPreparedBoundary PreparedBehavior;
  if (!BehaviorSourceRuntime->PrepareBoundary(
      FixedStepIndex, PreparedBehavior)
    || !BehaviorSourceRuntime->ValidatePrepared(PreparedBehavior))
  {
    BehaviorSourceRuntime->RollbackPendingCommandsTo(
      OriginalPendingSourceCommandCount);
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=source_prepare fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  for (const FCrowdBehaviorPreparedEntity& Entity
    : PreparedBehavior.Entities)
  {
    const int32 SlotIndex =
      static_cast<int32>(Entity.EntityRef.StableEntityId);
    if (!StagedSlots.IsValidIndex(SlotIndex)
      || !StagedSlots[SlotIndex].bActive
      || StagedSlots[SlotIndex].Facts.StableEntityRef
        != Entity.EntityRef)
    {
      BehaviorSourceRuntime->RollbackPendingCommandsTo(
        OriginalPendingSourceCommandCount);
      ++StaleRejectCount;
      return;
    }
    const FCrowdDemoPlannerDecision* PlannerDecision =
      PlannerDecisionBatch.Decisions.FindByPredicate(
        [&Entity](const FCrowdDemoPlannerDecision& Decision)
        {
          return Decision.EntityRef == Entity.EntityRef;
        });
    if (!PlannerDecision
      || PlannerDecision->DiagnosticLabel
        >= static_cast<uint32>(ECrowdActiveBehavior::Count))
    {
      BehaviorSourceRuntime->RollbackPendingCommandsTo(
        OriginalPendingSourceCommandCount);
      ++StaleRejectCount;
      return;
    }
    const ECrowdActiveBehavior Label =
      static_cast<ECrowdActiveBehavior>(
        PlannerDecision->DiagnosticLabel);
    if (StagedSlots[SlotIndex].Facts.DerivedBehaviorLabel
      != static_cast<uint32>(Label))
    {
      ++StagedBehaviorTransitionCount;
      ++StagedSlots[SlotIndex].TransitionRevision;
    }
    StagedSlots[SlotIndex].Facts.DerivedBehaviorLabel =
      static_cast<uint32>(Label);
    StagedSeenBehaviorBits |= BehaviorBit(Label);
  }
  TArray<FCrowdDemoBusinessAgentState> BusinessAgents;
  BusinessAgents.Reserve(PreparedBehavior.Entities.Num());
  for (int32 SlotIndex = 1;
    SlotIndex < StagedSlots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = StagedSlots[SlotIndex];
    if (!Slot.bActive) continue;
    FCrowdDemoBusinessAgentState& Agent =
      BusinessAgents.AddDefaulted_GetRef();
    Agent.EntityRef = Slot.Facts.StableEntityRef;
    Agent.TransitionRevision =
      Slots[SlotIndex].TransitionRevision;
    Agent.Health = Slot.Health;
    Agent.LastAttackFixedStep = Slot.LastAttackFixedStep;
    Agent.LastLogisticsFixedStep =
      Slot.LastLogisticsFixedStep;
    Agent.HitReactionUntilFixedStep =
      Slot.HitReactionUntilFixedStep;
    Agent.HitReactionVelocity = Slot.HitReactionVelocity;
    Agent.bActive = true;
  }
  FCrowdDemoPreparedBusinessPatch PreparedBusiness;
  TArray<FCrowdDemoHostIntent> HostIntents;
  for (const FCrowdDemoPlannerDecision& Decision
    : PlannerDecisionBatch.Decisions)
    for (const FCrowdDemoHostIntent& Intent
      : Decision.HostIntents)
      HostIntents.Add(Intent);
  if (!FCrowdDemoBusinessPatchAdapter::Prepare(
      PreparedBehavior, HostIntents, BusinessAgents,
      StagedBusinessLedger, PreparedBusiness))
  {
    BehaviorSourceRuntime->RollbackPendingCommandsTo(
      OriginalPendingSourceCommandCount);
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=source_prepare fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  for (const FCrowdDemoBusinessAgentState& Agent
    : PreparedBusiness.Agents)
  {
    const int32 SlotIndex =
      static_cast<int32>(Agent.EntityRef.StableEntityId);
    if (!StagedSlots.IsValidIndex(SlotIndex)
      || StagedSlots[SlotIndex].Facts.StableEntityRef
        != Agent.EntityRef)
    {
      BehaviorSourceRuntime->RollbackPendingCommandsTo(
        OriginalPendingSourceCommandCount);
      ++StaleRejectCount;
      return;
    }
    FSlotState& Slot = StagedSlots[SlotIndex];
    Slot.Health = Agent.Health;
    Slot.LastAttackFixedStep = Agent.LastAttackFixedStep;
    Slot.LastLogisticsFixedStep =
      Agent.LastLogisticsFixedStep;
    Slot.HitReactionUntilFixedStep =
      Agent.HitReactionUntilFixedStep;
    Slot.HitReactionVelocity =
      Agent.HitReactionVelocity;
  }
  StagedBusinessLedger = PreparedBusiness.Ledger;
  StagedDuplicateCommitCount +=
    PreparedBusiness.DuplicateCommitCount;
  if (PreparedBusiness.PendingDeathRef.IsValid())
    StagedPendingCombatDeathSlot = static_cast<int32>(
      PreparedBusiness.PendingDeathRef.StableEntityId);
  PrepareEndSeconds = FPlatformTime::Seconds();
  TArray<FCrowdAgentFacts> PreparedFacts;
  for (int32 SlotIndex = 1;
    SlotIndex < StagedSlots.Num(); ++SlotIndex)
    if (StagedSlots[SlotIndex].bActive)
      PreparedFacts.Add(StagedSlots[SlotIndex].Facts);
  PreparedFacts.Sort([](const auto& A, const auto& B)
  {
    return A.StableEntityRef < B.StableEntityRef;
  });
  if (!LifecycleWorld.ValidateAgentFactsCorrectionsAtBoundary(
      FixedStepIndex, PreparedFacts))
  {
    BehaviorSourceRuntime->RollbackPendingCommandsTo(
      OriginalPendingSourceCommandCount);
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=business_commit fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  FCrowdPreparedProjectileBoundary PreparedProjectile;
  FCrowdPreparedHostHitCommit PreparedProjectileHits;
  FCrowdDemoPreparedAttackBoundary PreparedAttack;
  FCrowdDemoPreparedAttackHealthPatch PreparedAttackHealth;
  const bool bProjectilePrepared = bMixedCombatIntegration
    ? PrepareMixedCombatBoundary(
      StagedSlots, AttackIntents, PreparedAttack,
      PreparedProjectile, PreparedAttackHealth)
    : PrepareProjectileBoundary(
      StagedSlots, PreparedProjectile,
      PreparedProjectileHits);
  if (!bProjectilePrepared)
  {
    BehaviorSourceRuntime->RollbackPendingCommandsTo(
      OriginalPendingSourceCommandCount);
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=projectile_prepare fixed_step=%lld"),
      FixedStepIndex);
    return;
  }
  for (const FCrowdHitFact& Hit : PreparedProjectileHits.Hits)
  {
    if (bMixedCombatIntegration) break;
    const uint64 ProjectileOffset =
      Hit.Impact.ImpactId & 0xffffull;
    const uint64 ExpectedTarget =
      static_cast<uint64>(ProjectileExpectedCount)
      + ProjectileOffset;
    if (!Hit.IsValid()
      || ProjectileOffset == 0
      || ProjectileOffset
        > static_cast<uint64>(ProjectileExpectedCount)
      || Hit.Impact.Target.StableEntityId != ExpectedTarget)
    {
      BehaviorSourceRuntime->RollbackPendingCommandsTo(
        OriginalPendingSourceCommandCount);
      ++StaleRejectCount;
      return;
    }
  }
  if (bMixedCombatIntegration)
  {
    for (const FCrowdDemoAttackHealthState& Health
      : PreparedAttackHealth.States)
    {
      const int32 SlotIndex = static_cast<int32>(
        Health.EntityRef.StableEntityId);
      if (!StagedSlots.IsValidIndex(SlotIndex)
        || StagedSlots[SlotIndex].Facts.StableEntityRef
          != Health.EntityRef)
      {
        BehaviorSourceRuntime->RollbackPendingCommandsTo(
          OriginalPendingSourceCommandCount);
        ++StaleRejectCount;
        return;
      }
      StagedSlots[SlotIndex].Health = Health.Health;
    }
  }
  const int32 StagedProjectileSpawnedCount =
    ProjectileSpawnedCount
    + PreparedProjectile.Summary.SpawnedCount;
  const int32 StagedProjectileImpactCount =
    ProjectileImpactCount
    + PreparedProjectile.Summary.ImpactedCount;
  const int32 StagedProjectileDamageCount =
    ProjectileDamageCount
    + (bMixedCombatIntegration
      ? PreparedAttackHealth.AppliedDamageCount
      : PreparedProjectileHits.Hits.Num());
  const int32 StagedProjectileExpiredCount =
    ProjectileExpiredCount
    + PreparedProjectile.Summary.ExpiredCount;
  const int32 StagedProjectileActiveCount =
    PreparedProjectile.Summary.ActiveCount;
  const int32 StagedProjectileDuplicateCount =
    ProjectileDuplicateCount
    + PreparedProjectile.Summary.DuplicateFireCount;
  uint64 StagedProjectileTraceHash = ProjectileTraceHash;
  if (!PreparedProjectile.States.IsEmpty()
    || !PreparedProjectile.Events.IsEmpty()
    || !PreparedProjectile.Impacts.IsEmpty())
  {
    StagedProjectileTraceHash = FoldMixedHash(
      StagedProjectileTraceHash,
      PreparedProjectile.StableHash);
  }
  const bool bStagedProjectileBatchSpawned =
    bProjectileBatchSpawned
    || PreparedProjectile.Summary.SpawnedCount > 0;
  const int32 StagedAttackIntentCount =
    AttackIntentCount + AttackIntents.Num();
  const int32 StagedAttackImpactCount =
    AttackImpactCount
    + (bMixedCombatIntegration
      ? PreparedAttack.ImmediateImpacts.Num()
        + PreparedProjectile.Impacts.Num()
      : 0);
  const int32 StagedAttackDamageCount =
    AttackDamageCount
    + (bMixedCombatIntegration
      ? PreparedAttackHealth.AppliedDamageCount
      : 0);
  const int32 StagedAttackDeathCount =
    AttackDeathCount
    + (bMixedCombatIntegration
      ? PreparedAttackHealth.DeathCount
      : 0);
  const int32 StagedMeleeAttackIntentCount =
    MeleeAttackIntentCount
    + PreparedAttack.MeleeIntentCount;
  const int32 StagedMidRangeAttackIntentCount =
    MidRangeAttackIntentCount
    + PreparedAttack.MidRangeIntentCount;
  const int32 StagedRangedAttackIntentCount =
    RangedAttackIntentCount
    + PreparedAttack.RangedIntentCount;
  FCrowdWorkerPayload ExpectedWorkerCombatHostResult;
  uint64 ExpectedWorkerProjectileStableHash = 0;
  const TSharedRef<FWorkerMixedCombatApplyState,
    ESPMode::ThreadSafe> WorkerCombatApply =
      MakeShared<FWorkerMixedCombatApplyState,
        ESPMode::ThreadSafe>();
  const TSharedRef<FWorkerBehaviorApplyState,
    ESPMode::ThreadSafe> WorkerBehaviorApply =
      MakeShared<FWorkerBehaviorApplyState,
        ESPMode::ThreadSafe>();
  if (UWorld* World = GetWorld())
  {
    if (UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
      World->GetSubsystem<UMassCrowdRuntimeSubsystem>())
    {
      WorkerBehaviorApply->bApplyProduction =
        RuntimeSubsystem->GetWorkerBehaviorAuthority().GetMode()
          == ECrowdWorkerBehaviorAuthorityMode::Production;
    }
  }
  if (bMixedCombatIntegration)
  {
    FCrowdDemoWorkerMixedCombatHostResult ExpectedResult;
    ExpectedResult.FixedStepIndex = FixedStepIndex;
    ExpectedResult.AttackPlanSummary = AttackPlanSummary;
    ExpectedResult.MeleeIntentCount =
      PreparedAttack.MeleeIntentCount;
    ExpectedResult.MidRangeIntentCount =
      PreparedAttack.MidRangeIntentCount;
    ExpectedResult.RangedIntentCount =
      PreparedAttack.RangedIntentCount;
    ExpectedResult.MissCount = PreparedAttack.MissCount;
    ExpectedResult.EnvironmentImpactCount =
      PreparedAttack.EnvironmentImpactCount;
    ExpectedResult.AppliedDamageCount =
      PreparedAttackHealth.AppliedDamageCount;
    ExpectedResult.DuplicateHitCount =
      PreparedAttackHealth.DuplicateHitCount;
    ExpectedResult.FriendlyFireCount =
      PreparedAttackHealth.FriendlyFireCount;
    ExpectedResult.DeathCount =
      PreparedAttackHealth.DeathCount;
    ExpectedResult.TargetSwitchCount =
      StagedAttackTargetSwitches;
    EWorkerMixedCombatAuthorityMode WorkerMode;
    if (!FCrowdDemoWorkerMixedCombatHostResultCodec::Encode(
          ExpectedResult, ExpectedWorkerCombatHostResult)
      || !ResolveWorkerMixedCombatAuthorityMode(WorkerMode))
    {
      BehaviorSourceRuntime->RollbackPendingCommandsTo(
        OriginalPendingSourceCommandCount);
      ++StaleRejectCount;
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMixedCombat role=server stage=worker_expectation fixed_step=%lld"),
        FixedStepIndex);
      return;
    }
    ExpectedWorkerProjectileStableHash =
      PreparedProjectile.StableHash;
    WorkerCombatApply->bApplyProduction =
      WorkerMode !=
        EWorkerMixedCombatAuthorityMode::Shadow;
  }
  const TSharedRef<TArray<FSlotState>, ESPMode::ThreadSafe>
    StagedSlotsState =
      MakeShared<TArray<FSlotState>, ESPMode::ThreadSafe>(
        MoveTemp(StagedSlots));
  TUniqueFunction<void(bool, int32, uint64)> FinalizeBoundary =
    [this,
      StagedSlotsState,
      StartSeconds,
      EvaluationEndSeconds,
      PrepareEndSeconds,
      OriginalPendingSourceCommandCount,
      PreparedBehavior,
      PreparedFacts = MoveTemp(PreparedFacts),
      PreparedProjectile = MoveTemp(PreparedProjectile),
      StagedBusinessLedger = MoveTemp(StagedBusinessLedger),
      StagedBehaviorTransitionCount,
      StagedDuplicateCommitCount,
      StagedSeenBehaviorBits,
      StagedPendingCombatDeathSlot,
      PlannerDecisionHash = PlannerDecisionBatch.StableHash,
      StagedProjectileSpawnedCount,
      StagedProjectileImpactCount,
      StagedProjectileDamageCount,
      StagedProjectileExpiredCount,
      StagedProjectileActiveCount,
      StagedProjectileDuplicateCount,
      StagedProjectileTraceHash,
      bStagedProjectileBatchSpawned,
      StagedAttackIntentCount,
      StagedAttackImpactCount,
      StagedAttackDamageCount,
      StagedAttackDeathCount,
      StagedAttackTargetSwitches,
      StagedMeleeAttackIntentCount,
      StagedMidRangeAttackIntentCount,
      StagedRangedAttackIntentCount,
      WorkerCombatApply,
      WorkerBehaviorApply](
        const bool bMovementSucceeded,
        const int32 StagedSafetyHolds,
        const uint64 StagedBoundaryCommitHash) mutable
  {
    if (!bMovementSucceeded)
    {
      BehaviorSourceRuntime->RollbackPendingCommandsTo(
        OriginalPendingSourceCommandCount);
      return;
    }
    TArray<FSlotState>& StagedSlots = *StagedSlotsState;
    const double MovementEndSeconds = FPlatformTime::Seconds();
  UMassEntitySubsystem* ProjectileEntitySubsystem =
    GetWorld()
      ? GetWorld()->GetSubsystem<UMassEntitySubsystem>()
      : nullptr;
  check(ProjectileEntitySubsystem);
  const TArray<FCrowdProjectileState>&
    EffectiveProjectileStates =
      WorkerCombatApply->bReady
        && WorkerCombatApply->bApplyProduction
      ? WorkerCombatApply->ProjectileState.Prepared.States
      : PreparedProjectile.States;
  if (!EffectiveProjectileStates.IsEmpty()
    || !PreparedProjectile.Events.IsEmpty())
  {
    ProjectileStore.ApplyValidated(
      ProjectileEntitySubsystem->GetMutableEntityManager(),
      EffectiveProjectileStates);
  }
  LifecycleWorld.ApplyValidatedAgentFactsCorrectionsAtBoundary(
    FixedStepIndex, PreparedFacts);
  const bool bBehaviorCommitted =
    WorkerBehaviorApply->bApplyProduction
      ? WorkerBehaviorApply->bReady
        && BehaviorSourceRuntime->CommitWorkerPrepared(
          PreparedBehavior, WorkerBehaviorApply->Entities,
          WorkerBehaviorApply->Events)
      : BehaviorSourceRuntime->CommitPrepared(PreparedBehavior);
  checkf(bBehaviorCommitted,
    TEXT("Validated behavior source transaction changed before final apply"));
  if (WorkerBehaviorApply->bApplyProduction)
  {
    UMassCrowdRuntimeSubsystem* RuntimeSubsystem = GetWorld()
      ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()
      : nullptr;
    checkf(RuntimeSubsystem
        && WorkerBehaviorApply->InputSequence != 0
        && RuntimeSubsystem->GetWorkerBehaviorAuthority().
          AcknowledgeMatchedEvents(
            WorkerBehaviorApply->InputSequence),
      TEXT("Validated Worker behavior event transaction changed before ACK"));
  }
  Slots = MoveTemp(StagedSlots);
  BusinessLedger = MoveTemp(StagedBusinessLedger);
  BehaviorTransitionCount = StagedBehaviorTransitionCount;
  DuplicateCommitCount = StagedDuplicateCommitCount;
  SeenBehaviorBits = StagedSeenBehaviorBits;
  PendingCombatDeathSlot = StagedPendingCombatDeathSlot;
  SafetyHoldCount += StagedSafetyHolds;
  LastBoundaryCommitHash = StagedBoundaryCommitHash;
  LastPlannerDecisionHash = PlannerDecisionHash;
  ProjectileSpawnedCount = StagedProjectileSpawnedCount;
  ProjectileImpactCount = StagedProjectileImpactCount;
  ProjectileDamageCount = StagedProjectileDamageCount;
  ProjectileExpiredCount = StagedProjectileExpiredCount;
  ProjectileActiveCount = StagedProjectileActiveCount;
  ProjectileDuplicateCount = StagedProjectileDuplicateCount;
  ProjectileTraceHash = StagedProjectileTraceHash;
  bProjectileBatchSpawned = bStagedProjectileBatchSpawned;
  AttackIntentCount = StagedAttackIntentCount;
  AttackImpactCount = StagedAttackImpactCount;
  AttackDamageCount = StagedAttackDamageCount;
  AttackDeathCount = StagedAttackDeathCount;
  AttackTargetSwitchCount += StagedAttackTargetSwitches;
  MeleeAttackIntentCount = StagedMeleeAttackIntentCount;
  MidRangeAttackIntentCount = StagedMidRangeAttackIntentCount;
  RangedAttackIntentCount = StagedRangedAttackIntentCount;

  // Final Apply already rejects every unsafe candidate against the updated
  // spatial index. Sample the exact minimum once per simulation second for
  // telemetry instead of turning an audit metric into per-step production
  // work; retain the minimum observed across the complete run.
  if (FixedStepIndex <= 5 || FixedStepIndex % 30 == 0)
    MinimumSeparationCm = FMath::Min(
      MinimumSeparationCm,
      SpatialSafety.CalculateMinimumSeparationCm());

  if (!bMixedCombatIntegration
    && FixedStepIndex % LifecycleIntervalSteps == 0)
  {
    FCrowdDemoContinuousLifecycleOperation Operation;
    if (BuildLifecycleOperation(Operation))
    {
      if (!ApplyLifecycleOperation(Operation))
      {
        ++StaleRejectCount;
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=lifecycle sequence=%llu"),
          Operation.Sequence);
        return;
      }
      FCrowdReliableStateRecord Record;
      Record.Sequence = NextStateSequence++;
      if (Operation.Kind
        == ECrowdDemoContinuousLifecycleOperationKind::Spawn)
        Record.Kind = ECrowdReliableStateKind::Spawn;
      else if (Operation.Kind
        == ECrowdDemoContinuousLifecycleOperationKind::Despawn)
        Record.Kind = ECrowdReliableStateKind::Despawn;
      else
        Record.Kind = ECrowdReliableStateKind::Membership;
      Record.EntityRef = {
        1, Operation.StableEntityId, Operation.LifecycleSerial};
      Record.Revision = Operation.RelevantSetRevision;
      EncodeLifecycleOperation(Operation, Record.Payload);
      Record.StableHash =
        FCrowdReplicationTransport::CalculateReliableRecordHash(Record);
      for (TPair<TWeakObjectPtr<APlayerController>,
        TWeakObjectPtr<AMassCrowdReplicationActor>>& Pair
        : ReplicationChannels)
      {
        if (AMassCrowdReplicationActor* Channel = Pair.Value.Get())
          Channel->PublishReliable(Record);
      }
    }
  }
  if (FixedStepIndex % 3 == 0) PublishProductStateFrame();
  const double EndSeconds = FPlatformTime::Seconds();
  const double TotalMilliseconds =
    (EndSeconds - StartSeconds) * 1000.0;
  ServerStepMilliseconds.Add(TotalMilliseconds);
  if (Config.PopulationLimit == MaximumMixedPopulation
    && TotalMilliseconds > 33.333)
  {
    // Keep telemetry from perturbing the 500-entity performance sample.
    // Periodic samples retain phase attribution without synchronous log I/O
    // on every over-budget fixed step.
    if (FixedStepIndex % 30 == 0
      || TotalMilliseconds > 50.0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoMixedSandboxSlowStep fixed_step=%lld evaluate_ms=%.3f prepare_ms=%.3f movement_ms=%.3f apply_publish_ms=%.3f total_ms=%.3f"),
        FixedStepIndex,
        (EvaluationEndSeconds - StartSeconds) * 1000.0,
        (PrepareEndSeconds - EvaluationEndSeconds) * 1000.0,
        (MovementEndSeconds - PrepareEndSeconds) * 1000.0,
        (EndSeconds - MovementEndSeconds) * 1000.0,
        TotalMilliseconds);
    }
  }
  if (Config.PopulationLimit == MaximumMixedPopulation
    && (FixedStepIndex <= 5 || FixedStepIndex % 150 == 0))
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxPhases fixed_step=%lld evaluate_ms=%.3f prepare_ms=%.3f movement_ms=%.3f apply_publish_ms=%.3f total_ms=%.3f"),
      FixedStepIndex,
      (EvaluationEndSeconds - StartSeconds) * 1000.0,
      (PrepareEndSeconds - EvaluationEndSeconds) * 1000.0,
      (MovementEndSeconds - PrepareEndSeconds) * 1000.0,
      (EndSeconds - MovementEndSeconds) * 1000.0,
      (EndSeconds - StartSeconds) * 1000.0);
  }
  if (ServerStepMilliseconds.Num() > 2048) ServerStepMilliseconds.RemoveAt(0, 512);
  };

  if (!BeginProductMovementBoundary(
      PreparedBehavior, StagedSlotsState,
      MoveTemp(WorkerMixedCombatControl),
      MoveTemp(ExpectedWorkerCombatHostResult),
      ExpectedWorkerProjectileStableHash,
      WorkerCombatApply,
      WorkerBehaviorApply,
      MoveTemp(FinalizeBoundary)))
  {
    BehaviorSourceRuntime->RollbackPendingCommandsTo(
      OriginalPendingSourceCommandCount);
    ++StaleRejectCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=server stage=product_boundary_submit fixed_step=%lld"),
      FixedStepIndex);
  }
}

bool ACrowdDemoMixedSandboxCoordinator::PlanBusinessBoundary(
  TArray<FSlotState>& InOutSlots,
  FCrowdDemoPlannerDecisionBatch& OutDecisionBatch)
{
  OutDecisionBatch = {};
  if (!BehaviorSourceRuntime
    || !BusinessPlannerRegistry.IsFrozen()
    || !NavGraphHandle.IsValid())
    return false;

  FCrowdDemoPlanningSnapshot Snapshot;
  Snapshot.ScenarioId = bMixedCombatIntegration
    ? CrowdDemoBusinessScenarios::MixedCombat
    : CrowdDemoBusinessScenarios::Mixed;
  Snapshot.FixedStepIndex = FixedStepIndex;
  Snapshot.FactRevision =
    static_cast<uint64>(FixedStepIndex) + 1;
  Snapshot.Settings.PopulationLimit = Config.PopulationLimit;
  Snapshot.Settings.MaximumSpeedCmps = 500.0f;
  Snapshot.Settings.ScaleMaximumSpeedCmps = 10.0f;
  Snapshot.Settings.InteractionRadiusCm = 180.0f;
  Snapshot.Settings.ScaleInteractionRadiusCm =
    MixedScaleInteractionRadiusCm;
  Snapshot.Settings.LogisticsCooldownSteps = 30;
  Snapshot.Settings.AttackCooldownSteps = 30;
  Snapshot.Settings.RoamSwitchIntervalSteps = 300;
  Snapshot.Settings.HitReactionDurationSteps = 6;

  const auto ResolveObjectiveLocation =
    [this](const FSlotState& Slot, const FName MarkerTag)
  {
    const FVector MarkerLocation =
      Marker(MarkerTag, FVector::ZeroVector);
    if (!NavGraphHandle.IsValid())
      return MarkerLocation;
    TArray<const FCrowdNavSurfaceNode*> Candidates;
    for (const FCrowdNavSurfaceNode& Node : NavGraphHandle->Nodes)
    {
      const bool bScaleCandidate =
        Config.PopulationLimit > DefaultMixedPopulation
        && Node.Center.Z < 800.0;
      if (bScaleCandidate
        || FVector::Distance(Node.Center, MarkerLocation) <= 400.0)
        Candidates.Add(&Node);
    }
    Candidates.Sort(
      [&MarkerLocation](
        const FCrowdNavSurfaceNode& A,
        const FCrowdNavSurfaceNode& B)
      {
        const double DistanceA =
          FVector::DistSquared(A.Center, MarkerLocation);
        const double DistanceB =
          FVector::DistSquared(B.Center, MarkerLocation);
        return DistanceA != DistanceB
          ? DistanceA < DistanceB
          : A.StableNodeId < B.StableNodeId;
      });
    if (Candidates.IsEmpty())
      return MarkerLocation;
    uint32 PlacementOrdinal = 0;
    if (!FCrowdDemoBusinessPlannerRunner::
        GetObjectivePlacementOrdinal(
          Slot.PlannerAssignment, PlacementOrdinal))
      return MarkerLocation;
    const int32 MarkerOffset =
      MarkerTag == TEXT("CrowdNavHigh")
      ? Candidates.Num() / 2
      : MarkerTag == TEXT("CrowdNavRouteB")
        ? Candidates.Num() / 3
        : 0;
    return Candidates[
      (MarkerOffset
        + static_cast<int32>(PlacementOrdinal) * 17)
        % Candidates.Num()]
      ->Center;
  };

  for (int32 SlotIndex = 1;
    SlotIndex < InOutSlots.Num(); ++SlotIndex)
  {
    FSlotState& Slot = InOutSlots[SlotIndex];
    if (!Slot.bActive) continue;
    FCrowdDemoPlannerAgentFact Agent;
    Agent.EntityRef = Slot.Facts.StableEntityRef;
    Agent.Assignment = Slot.PlannerAssignment;
    Agent.Capabilities = Slot.Facts.CapabilitySet;
    Agent.FactionId = Slot.FactionId;
    Agent.AttackProfileId = Slot.AttackProfileId;
    Agent.Position = Slot.Location;
    Agent.Velocity = Slot.Velocity;
    Agent.Facing =
      FRotator(0.0f, Slot.YawDegrees, 0.0f).Vector();
    Agent.Health = Slot.Health;
    Agent.LastAttackFixedStep = Slot.LastAttackFixedStep;
    Agent.LastLogisticsFixedStep =
      Slot.LastLogisticsFixedStep;
    Agent.HitReactionUntilFixedStep =
      Slot.HitReactionUntilFixedStep;
    Agent.HitReactionVelocity = Slot.HitReactionVelocity;
    Agent.InteractionLayer = Slot.InteractionLayer;
    Agent.TransitionRevision = Slot.TransitionRevision;
    Agent.AttackState = Slot.AttackState;
    Agent.bActive = Slot.bActive;
    if (Slot.PlannerAssignment.PlannerId
      == CrowdDemoBusinessPlanners::Logistics)
    {
      Agent.TaskRef = {
        2, static_cast<uint64>(SlotIndex), 1};
      Agent.bCarrying =
        BusinessLedger.GetCargoCarrier(
          Agent.TaskRef.StableEntityId)
        == Agent.EntityRef.StableEntityId;
    }
    Snapshot.Agents.Add(Agent);
    Snapshot.Objectives.Add({
      CrowdDemoBusinessObjectives::LogisticsSource,
      Agent.EntityRef,
      ResolveObjectiveLocation(Slot, TEXT("CrowdNavLower")),
      Snapshot.FactRevision});
    Snapshot.Objectives.Add({
      CrowdDemoBusinessObjectives::LogisticsSink,
      Agent.EntityRef,
      ResolveObjectiveLocation(Slot, TEXT("CrowdNavHigh")),
      Snapshot.FactRevision});
    Snapshot.Objectives.Add({
      CrowdDemoBusinessObjectives::RoamRoute,
      Agent.EntityRef,
      ResolveObjectiveLocation(Slot, TEXT("CrowdNavRouteB")),
      Snapshot.FactRevision});
  }
  if (!Snapshot.Finalize()
    || !FCrowdDemoBusinessPlannerRunner::Evaluate(
      BusinessPlannerRegistry, Snapshot, OutDecisionBatch)
    || !OutDecisionBatch.bValid
    || OutDecisionBatch.Decisions.Num()
      != Snapshot.Agents.Num())
    return false;
  TArray<FCrowdDemoPlanningRuntimeEntityFact> RuntimeFacts;
  RuntimeFacts.Reserve(Snapshot.Agents.Num());
  for (const FCrowdDemoPlannerAgentFact& Agent
    : Snapshot.Agents)
  {
    const int32 SlotIndex =
      static_cast<int32>(Agent.EntityRef.StableEntityId);
    if (!InOutSlots.IsValidIndex(SlotIndex)
      || !InOutSlots[SlotIndex].bActive
      || InOutSlots[SlotIndex].Facts.StableEntityRef
        != Agent.EntityRef)
      return false;
    const FSlotState& Slot = InOutSlots[SlotIndex];
    RuntimeFacts.Add({
      Agent.EntityRef,
      Slot.Location,
      Slot.Velocity,
      FRotator(0.0f, Slot.YawDegrees, 0.0f).Vector(),
      Slot.InteractionLayer});
  }
  if (!FCrowdDemoPlanningRuntimeHost::Stage(
      *BehaviorSourceRuntime,
      FixedStepIndex,
      RuntimeFacts,
      OutDecisionBatch.Decisions))
    return false;
  for (const FCrowdDemoPlannerDecision& Decision
    : OutDecisionBatch.Decisions)
  {
    const int32 SlotIndex =
      static_cast<int32>(Decision.EntityRef.StableEntityId);
    if (!InOutSlots.IsValidIndex(SlotIndex)
      || !InOutSlots[SlotIndex].bActive
      || InOutSlots[SlotIndex].Facts.StableEntityRef
        != Decision.EntityRef)
      return false;
    FSlotState& Slot = InOutSlots[SlotIndex];
    Slot.Facts.TargetRef = Decision.TargetRef;
    Slot.Facts.BusinessTaskRef = Decision.TaskRef;
    Slot.Facts.MovementProfileKey = 1;
  }
  return true;
}

bool ACrowdDemoMixedSandboxCoordinator::BeginProductMovementBoundary(
  const FCrowdBehaviorPreparedBoundary& PreparedBehavior,
  const TSharedRef<TArray<FSlotState>, ESPMode::ThreadSafe>&
    InOutSlotsState,
  FCrowdWorkerProjectileControlResource&& WorkerCombatControl,
  FCrowdWorkerPayload&& ExpectedWorkerCombatHostResult,
  const uint64 ExpectedWorkerProjectileStableHash,
  const TSharedRef<FWorkerMixedCombatApplyState,
    ESPMode::ThreadSafe>& WorkerCombatApply,
  const TSharedRef<FWorkerBehaviorApplyState,
    ESPMode::ThreadSafe>& WorkerBehaviorApply,
  TUniqueFunction<void(bool, int32, uint64)>&& Finalize)
{
  if (PendingMixedMovement.IsValid() || !Finalize)
    return false;
  TArray<FSlotState>& InOutSlots = *InOutSlotsState;
  const double ProductStartSeconds =
    FPlatformTime::Seconds();
  const auto RejectBoundary = [this](const TCHAR* Reason)
  {
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoMixedSandboxBoundaryReject reason=%s fixed_step=%lld"),
      Reason, FixedStepIndex);
    return false;
  };
  UWorld* World = GetWorld();
  UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  UMassCrowdRuntimeSubsystem* Runtime =
    World ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>() : nullptr;
  if (!EntitySubsystem || !Runtime || !NavGraphHandle.IsValid())
    return RejectBoundary(TEXT("missing_runtime_or_nav"));

  TArray<FCrowdMassBoundaryAgentRecord> Gathered;
  TArray<FVector> ResolvedVelocities;
  TArray<FVector> ResolvedFacings;
  TArray<float> ResolvedMaximumSpeeds;
  TArray<uint32> ResolvedInteractionLayers;
  TArray<uint64> ResolvedAttachedNodeIds;
  ResolvedVelocities.SetNumZeroed(InOutSlots.Num());
  ResolvedFacings.SetNumZeroed(InOutSlots.Num());
  ResolvedMaximumSpeeds.Init(500.0f, InOutSlots.Num());
  ResolvedInteractionLayers.SetNumZeroed(InOutSlots.Num());
  ResolvedAttachedNodeIds.SetNumZeroed(InOutSlots.Num());
  TArray<FCrowdMassCommitTarget> Targets;
  TArray<const FCrowdBehaviorPreparedEntity*>
    PreparedBehaviorBySlot;
  PreparedBehaviorBySlot.SetNumZeroed(InOutSlots.Num());
  for (const FCrowdBehaviorPreparedEntity& PreparedEntity
    : PreparedBehavior.Entities)
  {
    const int32 PreparedSlotIndex =
      static_cast<int32>(
        PreparedEntity.EntityRef.StableEntityId);
    if (!PreparedBehaviorBySlot.IsValidIndex(
          PreparedSlotIndex)
      || PreparedBehaviorBySlot[PreparedSlotIndex])
      return RejectBoundary(TEXT("prepared_entity_index"));
    PreparedBehaviorBySlot[PreparedSlotIndex] =
      &PreparedEntity;
  }
  for (int32 SlotIndex = 1;
    SlotIndex < InOutSlots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = InOutSlots[SlotIndex];
    if (!Slot.bActive) continue;
    FCrowdMassBoundaryAgentRecord& Record =
      Gathered.AddDefaulted_GetRef();
    Record.Identity.AgentId = SlotIndex;
    Record.Identity.SetStableEntityRef(
      Slot.Facts.StableEntityRef);
    Record.AgentFacts = Slot.Facts;
    Record.State.Position = Slot.Location;
    Record.State.Velocity = Slot.Velocity;
    Record.State.YawDegrees = Slot.YawDegrees;
    Record.State.PlanRevision =
      static_cast<int32>(FixedStepIndex);
    Record.State.bInitialized = true;
    Record.Properties.PhysicalRadiusCm =
      MinimumSafeSeparationCm * 0.5f;
    Record.Properties.HardSafetyGapCm = 0.0f;
    Record.Properties.SoftMarginCm = 0.0f;
    Record.Properties.Mobility = 1.0f;
    Record.Properties.MaximumSpeedCmps = 500.0f;
    Record.Properties.CapabilityProfileKey = 1;
    uint64 AttachedNodeId =
      Slot.AttachedNavNodeId;
    uint32 AttachedNavLayer =
      Slot.InteractionLayer;
    int32 AttachedNodeIndex =
      NavGraphHandle->FindNodeIndex(AttachedNodeId);
    if (!NavGraphHandle->Nodes.IsValidIndex(
          AttachedNodeIndex)
      || DistanceSquaredToNavNode(
          Slot.Location,
          NavGraphHandle->Nodes[AttachedNodeIndex])
        > FMath::Square(350.0))
    {
      AttachedNodeIndex = INDEX_NONE;
      if (FCrowdNavSurfaceGraphKernel::AttachClosest(
          *NavGraphHandle, Slot.Location, 350.0f,
          AttachedNodeId, AttachedNavLayer))
        AttachedNodeIndex =
          NavGraphHandle->FindNodeIndex(AttachedNodeId);
    }
    if (NavGraphHandle->Nodes.IsValidIndex(
        AttachedNodeIndex))
    {
      ResolvedAttachedNodeIds[SlotIndex] =
        AttachedNodeId;
    }
    else
      ResolvedAttachedNodeIds[SlotIndex] =
        Slot.AttachedNavNodeId;
    ResolvedInteractionLayers[SlotIndex] =
      AttachedNavLayer;
    Targets.Add({
      Slot.Facts.StableEntityRef,
      SlotIndex,
      Slot.Facts.StableEntityRef.LifecycleSerial});

    const FCrowdBehaviorPreparedEntity* PreparedPtr =
      PreparedBehaviorBySlot[SlotIndex];
    if (!PreparedPtr
      || PreparedPtr->EntityRef
        != Slot.Facts.StableEntityRef)
      return RejectBoundary(TEXT("missing_prepared_entity"));
    const FCrowdBehaviorPreparedEntity& PreparedEntity =
      *PreparedPtr;
    const FCrowdResolvedBehaviorChannels& Resolved =
      PreparedEntity.ResolvedChannels;
    if (!Resolved.DesiredFacing.IsNearlyZero())
      ResolvedFacings[SlotIndex] =
        Resolved.DesiredFacing;
    const float SpeedLimitCmps =
      FMath::IsFinite(Resolved.SpeedLimitCmps)
      ? FMath::Clamp(Resolved.SpeedLimitCmps, 0.0f, 500.0f)
      : 500.0f;
    FVector DesiredVelocity = Resolved.bMovementLocked
      ? FVector::ZeroVector
      : Resolved.DesiredVelocity.GetClampedToMaxSize(SpeedLimitCmps);
    if ((Resolved.AllowedNavLayerMask
        & (uint64{1} << FMath::Min<uint32>(
          AttachedNavLayer, 63u))) == 0)
      DesiredVelocity = FVector::ZeroVector;
    if (!DesiredVelocity.IsNearlyZero()
      && Resolved.MovementGoal.bHasGoal)
    {
      const FCrowdNavSurfaceFlow* Flow = nullptr;
      if (!GetOrBuildFlow(
          Resolved.MovementGoal.Location, Flow,
          Slot.CachedGoalNodeId,
          &InOutSlots[SlotIndex].CachedGoalNodeId)
        || !Flow)
        DesiredVelocity = FVector::ZeroVector;
      else
      {
        const int32 CurrentIndex =
          AttachedNodeIndex;
        if (CurrentIndex == INDEX_NONE
          || !Flow->Nodes.IsValidIndex(CurrentIndex))
          DesiredVelocity = FVector::ZeroVector;
        else
        {
          FVector FlowDirection =
            Flow->Nodes[CurrentIndex].Direction;
          if (!FlowDirection.IsNearlyZero())
            DesiredVelocity =
              FlowDirection.GetSafeNormal()
                * DesiredVelocity.Size();
        }
      }
    }
    ResolvedVelocities[SlotIndex] =
      DesiredVelocity;
    ResolvedMaximumSpeeds[SlotIndex] =
      Resolved.bMovementLocked ? 0.0f : SpeedLimitCmps;
  }
  FCrowdMassBoundarySnapshot Snapshot;
  FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
    static_cast<int32>(FixedStepIndex),
    static_cast<int32>(FixedStepIndex),
    Gathered,
    Snapshot);
  if (!Snapshot.bValid)
    return RejectBoundary(TEXT("snapshot"));
  TArray<FCrowdWorkerVersionedResourceInput>
    AdditionalWorkerResources;
  const bool bRequireWorkerCombat =
    WorkerCombatControl.IsValid();
  if (bMixedCombatIntegration && !bRequireWorkerCombat)
    return RejectBoundary(TEXT("worker_combat_control"));
  if (bRequireWorkerCombat)
  {
    FCrowdWorkerVersionedResourceInput& Resource =
      AdditionalWorkerResources.AddDefaulted_GetRef();
    Resource.ResourceId =
      CrowdWorkerResourceIds::ProjectileControl;
    Resource.Revision = WorkerCombatControl.Revision;
    if (!FCrowdWorkerProjectileControlResourceCodec::Encode(
        WorkerCombatControl, Resource.Payload))
      return RejectBoundary(TEXT("worker_combat_encode"));
  }
  const bool bWorkerSubmitted =
    World && FCrowdDemoWorkerInputSync::SubmitBoundarySnapshot(
      *World, Snapshot, MixedFixedStepSeconds,
      static_cast<double>(FixedStepIndex + 1)
        * MixedFixedStepSeconds,
      AdditionalWorkerResources,
      &PreparedBehavior);
  if (!bWorkerSubmitted)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedWorkerShadowInputSync step=%lld"),
      FixedStepIndex);
    if (bRequireWorkerCombat)
      return RejectBoundary(TEXT("worker_combat_submit"));
  }
  if (bWorkerSubmitted)
  {
    WorkerBehaviorApply->bApplyProduction =
      Runtime->GetWorkerBehaviorAuthority().GetMode()
        == ECrowdWorkerBehaviorAuthorityMode::Production;
  }
  const uint64 WorkerBehaviorInputSequence =
    bWorkerSubmitted
      ? Runtime->GetWorkerShadowSync().GetMetrics()
        .LastSubmittedInputSequence
      : 0;
  if (WorkerBehaviorApply->bApplyProduction
    && WorkerBehaviorInputSequence == 0)
    return RejectBoundary(TEXT("worker_behavior_sequence"));
  else if (bRequireWorkerCombat)
  {
    bWorkerMixedCombatBootstrapped = true;
    ++NextWorkerMixedCombatControlRevision;
    if (NextWorkerMixedCombatControlRevision == 0)
      return RejectBoundary(TEXT("worker_combat_revision"));
  }

  FCrowdMassBoundaryWorkGraphInput PipelineInput;
  PipelineInput.Movement.Guidance.FixedStepIndex =
    Snapshot.FixedStepIndex;
  PipelineInput.Movement.Guidance.PlanRevision =
    Snapshot.PlanRevision;
  PipelineInput.Movement.FixedStepSeconds =
    static_cast<float>(MixedFixedStepSeconds);
  PipelineInput.Movement.bRunLocalPredictive = true;
  PipelineInput.Movement.LocalPredictiveSettings.FixedStepSeconds =
    static_cast<float>(MixedFixedStepSeconds);
  PipelineInput.Movement.LocalPredictiveSettings.TimeHorizonSeconds =
    0.05f;
  PipelineInput.Movement.LocalPredictiveSettings.SpatialCellSizeCm =
    Config.PopulationLimit >= 500 ? 120.0f : 200.0f;
  PipelineInput.Movement.LocalPredictiveSettings.JointIterationCount =
    1;
  bool bSourceSetChanged = false;
  for (const FCrowdBehaviorPreparedEntity& Entity
    : PreparedBehavior.Entities)
  {
    bSourceSetChanged |= !Entity.Events.IsEmpty();
  }
  if (!bSourceSetChanged)
    PipelineInput.Movement.PreviousGrantStates =
      MixedLocalPredictiveGrantStates;

  FBox MovementBounds(EForceInit::ForceInit);
  for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
    MovementBounds += Agent.State.Position;
  for (const FCrowdNavSurfaceNode& Node : NavGraphHandle->Nodes)
    MovementBounds += Node.Center;
  if (!MovementBounds.IsValid)
    return RejectBoundary(TEXT("movement_bounds"));
  PipelineInput.Movement.Environment.Revision =
    Snapshot.PlanRevision;
  PipelineInput.Movement.Environment.BoundsMin = FVector(
    MovementBounds.Min.X - 2000.0,
    MovementBounds.Min.Y - 2000.0, 0.0);
  PipelineInput.Movement.Environment.BoundsMax = FVector(
    MovementBounds.Max.X + 2000.0,
    MovementBounds.Max.Y + 2000.0, 0.0);
  PipelineInput.Movement.Environment.CellSizeCm = 150.0f;
  PipelineInput.Movement.Environment.AgentInflateCm =
    MinimumSafeSeparationCm * 0.5f;

  PipelineInput.ParticleTemplate.Particle.FixedStepIndex =
    Snapshot.FixedStepIndex;
  PipelineInput.ParticleTemplate.Particle.PlanRevision =
    Snapshot.PlanRevision;
  PipelineInput.ParticleTemplate.Particle.Environment.FlowConfig =
    PipelineInput.Movement.Environment;
  PipelineInput.ParticleTemplate.Particle.Environment
    .bConstrainToFlowBounds = true;
  PipelineInput.ParticleTemplate.Particle.Settings.FixedStepSeconds =
    static_cast<float>(MixedFixedStepSeconds);
  PipelineInput.ParticleTemplate.Particle.Settings.IterationCount =
    Config.PopulationLimit >= 500 ? 1 : 4;
  PipelineInput.ParticleTemplate.Particle.Settings.SafetyIterationCount =
    Config.PopulationLimit >= 500 ? 1 : 4;
  PipelineInput.ParticleTemplate.Particle.Settings.PositionQuantumCm =
    0.1f;
  PipelineInput.ParticleTemplate.Particle.Settings.VelocityQuantumCmps =
    0.1f;
  PipelineInput.FacingSettings.FixedStepSeconds =
    static_cast<float>(MixedFixedStepSeconds);

  for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
  {
    const FCrowdStableEntityRef Ref =
      Agent.AgentFacts.StableEntityRef;
    const int32 SlotIndex =
      Agent.Identity.AgentId;
    if (!ResolvedVelocities.IsValidIndex(SlotIndex)
      || !ResolvedMaximumSpeeds.IsValidIndex(SlotIndex)
      || !ResolvedInteractionLayers.IsValidIndex(SlotIndex))
      return RejectBoundary(TEXT("resolved_index"));
    const FVector& DesiredVelocity =
      ResolvedVelocities[SlotIndex];
    const float MaximumSpeed =
      ResolvedMaximumSpeeds[SlotIndex];
    const uint32 InteractionLayer =
      ResolvedInteractionLayers[SlotIndex];
    if (DesiredVelocity.ContainsNaN()
      || !FMath::IsFinite(MaximumSpeed)
      || MaximumSpeed < 0.0f)
      return RejectBoundary(TEXT("resolved_movement"));

    FCrowdMassGatherRecord& GuidanceRecord =
      PipelineInput.Movement.Guidance.Records
        .AddDefaulted_GetRef();
    GuidanceRecord.Identity = Agent.Identity;
    GuidanceRecord.AgentFacts = Agent.AgentFacts;
    GuidanceRecord.State = Agent.State;
    GuidanceRecord.Properties = Agent.Properties;
    const FVector& Facing =
      ResolvedFacings[SlotIndex];
    const float DesiredYawDegrees =
      !Facing.IsNearlyZero()
      ? Facing.Rotation().Yaw
      : DesiredVelocity.IsNearlyZero()
        ? Agent.State.YawDegrees
        : DesiredVelocity.Rotation().Yaw;
    GuidanceRecord.Guidance.TargetRegion =
      FCrowdGuidanceComposeKernel::BuildCandidate(
        Agent.Identity.AgentId,
        ECrowdGuidanceProvider::TargetRegion,
        Snapshot.PlanRevision,
        DesiredVelocity,
        Agent.State.Position
          + DesiredVelocity
            * static_cast<float>(MixedFixedStepSeconds),
        DesiredYawDegrees,
        true);

    FCrowdMassMovementPipelineAgentOverlay& Overlay =
      PipelineInput.Movement.AgentOverlays
        .AddDefaulted_GetRef();
    Overlay.AgentId = Agent.Identity.AgentId;
    Overlay.InteractionLayer = InteractionLayer;
    Overlay.PreviousBlockedAgeSteps =
      InOutSlots[
        static_cast<int32>(Ref.StableEntityId)]
        .PreviousBlockedAgeSteps;
    Overlay.MaximumSpeedCmps = MaximumSpeed;
    Overlay.BoundaryLocation = Agent.State.Position;
    Overlay.bVerticalOverride =
      !FMath::IsNearlyZero(DesiredVelocity.Z);
    Overlay.ProposedZ =
      Agent.State.Position.Z
      + DesiredVelocity.Z
        * static_cast<float>(MixedFixedStepSeconds);
    Overlay.VerticalVelocityCmps =
      DesiredVelocity.Z;
    Overlay.bParticleActive = true;

    FCrowdParticleConstraintAgent& ParticleAgent =
      PipelineInput.ParticleTemplate.Particle.Agents
        .AddDefaulted_GetRef();
    ParticleAgent.AgentId = Agent.Identity.AgentId;
    ParticleAgent.InteractionLayer = InteractionLayer;
    ParticleAgent.StartPosition = Agent.State.Position;
    ParticleAgent.PredictedPosition = Agent.State.Position;
    ParticleAgent.PhysicalRadiusCm =
      Agent.Properties.PhysicalRadiusCm;
    ParticleAgent.HardSafetyGapCm =
      Agent.Properties.HardSafetyGapCm;
    ParticleAgent.SoftMarginCm =
      Agent.Properties.SoftMarginCm;
    ParticleAgent.Mobility = Agent.Properties.Mobility;

    FCrowdMassBoundaryFacingTemplate& FacingTemplate =
      PipelineInput.FacingTemplates.AddDefaulted_GetRef();
    FacingTemplate.Input.AgentId = Agent.Identity.AgentId;
    FacingTemplate.Input.CurrentYawDegrees =
      Agent.State.YawDegrees;
    FacingTemplate.Input.Location = FVector2f(
      Agent.State.Position.X, Agent.State.Position.Y);
    if (!Facing.IsNearlyZero())
    {
      const FVector Target =
        Agent.State.Position + Facing.GetSafeNormal() * 100.0;
      FacingTemplate.Input.TargetLocation =
        FVector2f(Target.X, Target.Y);
      FacingTemplate.Input.bHasTarget = true;
    }
  }

  const TSharedRef<FMixedMovementWork, ESPMode::ThreadSafe> Work =
    MakeShared<FMixedMovementWork, ESPMode::ThreadSafe>();
  const double GatherEndSeconds =
    FPlatformTime::Seconds();
  TUniquePtr<FPendingMixedMovement> Pending =
    MakeUnique<FPendingMixedMovement>();
  Pending->Runner = MakeUnique<FCrowdMassBoundaryRunner>();
  const FCrowdBoundaryTransactionId TransactionId =
    FCrowdBoundaryTransactionId::FromSnapshot(
      Snapshot, ProductBoundaryGeneration);
  if (!Pending->Runner->Begin(Snapshot, 0.0, TransactionId))
    return RejectBoundary(TEXT("runner_begin"));
  PipelineInput.ParticleTemplate.Snapshot =
    MoveTemp(Snapshot);
  if (!Pending->Runner->AddTask(
      {{3}, {301}, 0}, {},
      [PipelineInput = MoveTemp(PipelineInput),
       ResolvedVelocities = MoveTemp(ResolvedVelocities),
       Work]()
      {
        const double MovementStart = FPlatformTime::Seconds();
        const FCrowdMassMovementPipelineWorkOutput Movement =
          FCrowdMassMovementPipelineWork::Run(
            PipelineInput.Movement);
        const double MovementEnd = FPlatformTime::Seconds();
        Work->MovementMilliseconds =
          (MovementEnd - MovementStart) * 1000.0;
        if (!Movement.bCompleted)
        {
          Work->FailureCode = 10;
          return FCrowdBoundaryTaskResult::Failure();
        }

        FCrowdMassParticlePipelineWorkInput ParticleInput;
        if (!FCrowdMassBoundaryWorkGraph::BuildParticleInput(
            PipelineInput, Movement, ParticleInput))
        {
          Work->FailureCode = 11;
          return FCrowdBoundaryTaskResult::Failure();
        }

        const double ParticleStart = FPlatformTime::Seconds();
        const FCrowdMassParticlePipelineWorkOutput Particle =
          FCrowdMassParticlePipelineWork::Run(ParticleInput);
        const double ParticleEnd = FPlatformTime::Seconds();
        Work->ParticleMilliseconds =
          (ParticleEnd - ParticleStart) * 1000.0;
        if (!Particle.bCompleted)
        {
          Work->FailureCode = 12;
          return FCrowdBoundaryTaskResult::Failure();
        }

        FCrowdMassFacingFinalizeWorkInput FacingInput;
        if (!FCrowdMassBoundaryWorkGraph::BuildFacingInput(
            PipelineInput, Movement, Particle, FacingInput))
        {
          Work->FailureCode = 13;
          return FCrowdBoundaryTaskResult::Failure();
        }
        const double FacingStart = FPlatformTime::Seconds();
        const FCrowdMassFacingFinalizeWorkOutput FacingFinalize =
          FCrowdMassFacingFinalizeWork::Run(FacingInput);
        Work->FacingMilliseconds =
          (FPlatformTime::Seconds() - FacingStart) * 1000.0;
        if (!FacingFinalize.bCompleted)
        {
          Work->FailureCode = 14;
          return FCrowdBoundaryTaskResult::Failure();
        }

        Work->Plan = FacingFinalize.Finalize.CommitPlan;
        Work->GrantStates =
          Movement.LocalPredictive.GrantStates;
        for (const FCrowdLocalPredictiveResult& Result
          : Movement.LocalPredictive.Results)
          Work->BlockedAgeByAgentId.Add(
            Result.AgentId, Result.NextBlockedAgeSteps);
        for (const FCrowdMassCommitRecord& Record
          : Work->Plan.Records)
        {
          const int32 AgentId =
            Record.Movement.AgentId;
          if (ResolvedVelocities.IsValidIndex(AgentId)
            && !ResolvedVelocities[AgentId].IsNearlyZero()
            && Record.Movement.Velocity.IsNearlyZero())
            ++Work->SafetyHolds;
        }
        uint64 WorkHash =
          FoldMixedHash(14695981039346656037ull,
            Movement.StableHash);
        WorkHash = FoldMixedHash(
          WorkHash, Particle.StableHash);
        WorkHash = FoldMixedHash(
          WorkHash, FacingFinalize.StableHash);
        WorkHash = FoldMixedHash(
          WorkHash, Work->Plan.StableHash);
        Work->bCompleted = true;
        return FCrowdBoundaryTaskResult::Success(WorkHash);
      }))
    return RejectBoundary(TEXT("runner_add"));
  if (!Pending->Runner->Dispatch())
    return RejectBoundary(TEXT("runner_dispatch"));
  Pending->Work = Work;
  Pending->StagedSlots = InOutSlotsState;
  Pending->Snapshot = Snapshot;
  Pending->Targets = MoveTemp(Targets);
  Pending->PreparedBehavior = PreparedBehavior;
  Pending->ResolvedInteractionLayers =
    MoveTemp(ResolvedInteractionLayers);
  Pending->ResolvedAttachedNodeIds =
    MoveTemp(ResolvedAttachedNodeIds);
  Pending->WorkerCombatAnchor =
    WorkerCombatControl.AnchorEntity;
  Pending->ExpectedWorkerCombatHostResult =
    MoveTemp(ExpectedWorkerCombatHostResult);
  Pending->ExpectedWorkerCombatControlRevision =
    WorkerCombatControl.Revision;
  Pending->ExpectedWorkerProjectileStableHash =
    ExpectedWorkerProjectileStableHash;
  Pending->WorkerBehaviorInputSequence =
    WorkerBehaviorInputSequence;
  Pending->bRequireWorkerCombat =
    bRequireWorkerCombat;
  Pending->WorkerCombatApply = WorkerCombatApply;
  Pending->WorkerBehaviorApply = WorkerBehaviorApply;
  Pending->Finalize = MoveTemp(Finalize);
  Pending->ProductStartSeconds = ProductStartSeconds;
  Pending->GatherEndSeconds = GatherEndSeconds;
  PendingMixedMovement = MoveTemp(Pending);
  return true;
}

bool ACrowdDemoMixedSandboxCoordinator::PollProductMovementBoundary()
{
  if (!PendingMixedMovement.IsValid()
    || !PendingMixedMovement->Runner.IsValid()
    || !PendingMixedMovement->Work.IsValid()
    || !PendingMixedMovement->StagedSlots.IsValid()
    || !PendingMixedMovement->Finalize)
    return false;
  FPendingMixedMovement& Pending = *PendingMixedMovement;
  FCrowdMassBoundaryRunner& Runner = *Pending.Runner;
  const ECrowdBoundaryPollResult PollResult = Runner.PollAndDrain();
  if (PollResult == ECrowdBoundaryPollResult::Pending)
    return true;

  const auto Complete = [this](
    const bool bSucceeded,
    const int32 SafetyHolds,
    const uint64 CommitHash)
  {
    TUniqueFunction<void(bool, int32, uint64)> Finalize =
      MoveTemp(PendingMixedMovement->Finalize);
    PendingMixedMovement.Reset();
    ++ProductBoundaryGeneration;
    if (ProductBoundaryGeneration == 0)
      ProductBoundaryGeneration = 1;
    Finalize(bSucceeded, SafetyHolds, CommitHash);
  };
  const auto RejectBoundary = [this, &Complete](const TCHAR* Reason)
  {
    const int32 FailureCode = PendingMixedMovement.IsValid()
      && PendingMixedMovement->Work.IsValid()
      ? PendingMixedMovement->Work->FailureCode
      : INDEX_NONE;
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoMixedSandboxBoundaryReject reason=%s code=%d fixed_step=%lld"),
      Reason, FailureCode, FixedStepIndex);
    Complete(false, 0, 0);
    return false;
  };
  if (PollResult == ECrowdBoundaryPollResult::Failed
    || !Pending.Work->bCompleted)
    return RejectBoundary(TEXT("runner_work"));

  UWorld* World = GetWorld();
  UMassCrowdRuntimeSubsystem* WorkerRuntimeSubsystem =
    World
      ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>()
      : nullptr;
  if (Pending.WorkerBehaviorApply.IsValid()
    && Pending.WorkerBehaviorApply->bApplyProduction)
  {
    if (!WorkerRuntimeSubsystem
      || Pending.WorkerBehaviorInputSequence == 0)
      return RejectBoundary(TEXT("worker_behavior_context"));
    const FCrowdWorkerBehaviorAuthorityMetrics& AuthorityMetrics =
      WorkerRuntimeSubsystem->GetWorkerBehaviorAuthority()
        .GetMetrics();
    if (AuthorityMetrics.bViolation)
      return RejectBoundary(TEXT("worker_behavior_authority"));
    if (AuthorityMetrics.LastMatchedInputSequence
        < Pending.WorkerBehaviorInputSequence)
      return true;
    if (AuthorityMetrics.LastMatchedInputSequence
        != Pending.WorkerBehaviorInputSequence)
      return RejectBoundary(TEXT("worker_behavior_sequence"));

    const FCrowdWorkerResultApplyProxy& Proxy =
      WorkerRuntimeSubsystem->GetWorkerResultApplyProxy();
    TArray<FCrowdBehaviorWorkerCommitEntity> WorkerEntities;
    WorkerEntities.Reserve(Pending.PreparedBehavior.Entities.Num());
    for (const FCrowdBehaviorPreparedEntity& Expected :
      Pending.PreparedBehavior.Entities)
    {
      const FCrowdWorkerDomainProxyState* BehaviorProxy =
        Proxy.FindDomain(
          Expected.EntityRef, ECrowdWorkerField::Behavior);
      FCrowdWorkerBehaviorState WorkerState;
      if (!BehaviorProxy
        || BehaviorProxy->SourceInputSequence
          != Pending.WorkerBehaviorInputSequence
        || !FCrowdWorkerBehaviorStateCodec::Decode(
          BehaviorProxy->State.Payload, WorkerState)
        || WorkerState.LastFixedStep
          != Pending.PreparedBehavior.FixedStepIndex
        || WorkerState.EvaluationContext.FixedStepIndex
          != Pending.PreparedBehavior.FixedStepIndex
        || WorkerState.SourceSet.EntityRef != Expected.EntityRef
        || WorkerState.ResolvedChannels.StableHash
          != Expected.ResolvedChannels.StableHash
        || WorkerState.EvaluationContext.StableHash
          != Expected.EvaluationContextHash)
        return RejectBoundary(TEXT("worker_behavior_state"));
      FCrowdBehaviorWorkerCommitEntity& Entity =
        WorkerEntities.AddDefaulted_GetRef();
      Entity.EntityRef = Expected.EntityRef;
      Entity.SourceSet = MoveTemp(WorkerState.SourceSet);
      Entity.ResolvedChannels =
        MoveTemp(WorkerState.ResolvedChannels);
      Entity.EvaluationContextHash =
        WorkerState.EvaluationContext.StableHash;
    }
    Pending.WorkerBehaviorApply->Entities =
      MoveTemp(WorkerEntities);
    if (!WorkerRuntimeSubsystem->GetWorkerBehaviorAuthority().
      PeekMatchedEvents(
        Pending.WorkerBehaviorInputSequence,
        Pending.WorkerBehaviorApply->Events,
        Pending.WorkerBehaviorApply->BusinessCommits))
      return RejectBoundary(TEXT("worker_behavior_events"));
    Pending.WorkerBehaviorApply->InputSequence =
      Pending.WorkerBehaviorInputSequence;
    TArray<FCrowdBusinessContribution> ExpectedBusinessCommits;
    for (const FCrowdBehaviorPreparedEntity& Entity
      : Pending.PreparedBehavior.Entities)
      ExpectedBusinessCommits.Append(
        Entity.ResolvedChannels.Business);
    if (ExpectedBusinessCommits.Num()
        != Pending.WorkerBehaviorApply->BusinessCommits.Num())
      return RejectBoundary(TEXT("worker_business_commit_count"));
    for (int32 Index = 0;
      Index < ExpectedBusinessCommits.Num(); ++Index)
    {
      FCrowdWorkerPayload ExpectedPayload;
      FCrowdWorkerPayload WorkerPayload;
      if (!FCrowdWorkerBusinessCommitEventCodec::Encode(
          ExpectedBusinessCommits[Index], ExpectedPayload)
        || !FCrowdWorkerBusinessCommitEventCodec::Encode(
          Pending.WorkerBehaviorApply->BusinessCommits[Index],
          WorkerPayload)
        || ExpectedPayload != WorkerPayload)
        return RejectBoundary(TEXT("worker_business_commit"));
    }
    Pending.WorkerBehaviorApply->bReady = true;
  }
  if (Pending.bRequireWorkerCombat)
  {
    UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
      WorkerRuntimeSubsystem;
    if (!RuntimeSubsystem
      || !Pending.WorkerCombatApply.IsValid()
      || !Pending.WorkerCombatAnchor.IsValid()
      || Pending.ExpectedWorkerCombatControlRevision == 0)
      return RejectBoundary(TEXT("worker_combat_context"));
    const FCrowdWorkerResultApplyProxy& Proxy =
      RuntimeSubsystem->GetWorkerResultApplyProxy();
    const FCrowdWorkerDomainProxyState* ProjectileProxy =
      Proxy.FindDomain(
        Pending.WorkerCombatAnchor,
        ECrowdWorkerField::Projectile);
    if (!ProjectileProxy
      || ProjectileProxy->State.StateRevision
        < Pending.ExpectedWorkerCombatControlRevision)
      return true;
    if (ProjectileProxy->State.StateRevision
        != Pending.ExpectedWorkerCombatControlRevision)
      return RejectBoundary(TEXT("worker_projectile_revision"));

    FCrowdWorkerProjectileState WorkerProjectileState;
    if (!FCrowdWorkerProjectileStateCodec::Decode(
          ProjectileProxy->State.Payload,
          WorkerProjectileState)
      || WorkerProjectileState.ControlRevision
        != Pending.ExpectedWorkerCombatControlRevision
      || WorkerProjectileState.Prepared.FixedStepIndex
        != FixedStepIndex
      || WorkerProjectileState.Prepared.StableHash
        != Pending.ExpectedWorkerProjectileStableHash
      || WorkerProjectileState.HostCombatResult
        != Pending.ExpectedWorkerCombatHostResult)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMixedWorkerCombatMismatch stage=projectile step=%lld revision=%llu expected_hash=%llu actual_hash=%llu"),
        FixedStepIndex,
        Pending.ExpectedWorkerCombatControlRevision,
        Pending.ExpectedWorkerProjectileStableHash,
        WorkerProjectileState.Prepared.StableHash);
      return RejectBoundary(TEXT("worker_projectile_parity"));
    }

    TArray<FSlotState>& WorkerValidatedSlots =
      *Pending.StagedSlots;
    for (int32 SlotIndex = 1;
      SlotIndex < WorkerValidatedSlots.Num(); ++SlotIndex)
    {
      FSlotState& Slot = WorkerValidatedSlots[SlotIndex];
      if (!Slot.bActive)
        continue;
      const FCrowdWorkerDomainProxyState* CombatProxy =
        Proxy.FindDomain(
          Slot.Facts.StableEntityRef,
          ECrowdWorkerField::Combat);
      if (!CombatProxy
        || CombatProxy->State.StateRevision
          < Pending.ExpectedWorkerCombatControlRevision)
        return true;
      if (CombatProxy->State.StateRevision
          != Pending.ExpectedWorkerCombatControlRevision)
        return RejectBoundary(TEXT("worker_combat_revision"));
      FCrowdWorkerCombatState CombatState;
      FCrowdDemoWorkerMixedCombatState HostState;
      FCrowdWorkerPayload ExpectedHostState;
      FCrowdDemoWorkerMixedCombatState ExpectedState;
      ExpectedState.Health = Slot.Health;
      ExpectedState.bAlive = Slot.Health > 0;
      ExpectedState.AttackState = Slot.AttackState;
      if (!FCrowdWorkerCombatStateCodec::Decode(
            CombatProxy->State.Payload, CombatState)
        || !FCrowdDemoWorkerMixedCombatStateCodec::Decode(
            CombatState.HostState, HostState)
        || !FCrowdDemoWorkerMixedCombatStateCodec::Encode(
            ExpectedState, ExpectedHostState)
        || CombatState.SourceFixedStep != FixedStepIndex
        || CombatState.bAlive != ExpectedState.bAlive
        || CombatState.HostState != ExpectedHostState)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoMixedWorkerCombatMismatch stage=agent step=%lld stable_id=%llu lifecycle=%u expected_health=%d actual_health=%d"),
          FixedStepIndex,
          Slot.Facts.StableEntityRef.StableEntityId,
          Slot.Facts.StableEntityRef.LifecycleSerial,
          ExpectedState.Health,
          HostState.Health);
        return RejectBoundary(TEXT("worker_combat_parity"));
      }
      if (Pending.WorkerCombatApply->bApplyProduction)
      {
        Slot.Health = HostState.Health;
        Slot.AttackState = HostState.AttackState;
      }
    }
    Pending.WorkerCombatApply->ProjectileState =
      MoveTemp(WorkerProjectileState);
    Pending.WorkerCombatApply->bReady = true;
    if (FixedStepIndex <= 5 || FixedStepIndex % 150 == 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoMixedWorkerCombatCheckpoint step=%lld revision=%llu mode=%s projectile_hash=%llu"),
        FixedStepIndex,
        Pending.ExpectedWorkerCombatControlRevision,
        Pending.WorkerCombatApply->bApplyProduction
          ? TEXT("WorkerOwner")
          : TEXT("Shadow"),
        Pending.ExpectedWorkerProjectileStableHash);
    }
  }
  UMassEntitySubsystem* EntitySubsystem =
    World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
  if (!EntitySubsystem)
    return RejectBoundary(TEXT("missing_entity_subsystem"));
  TArray<FSlotState>& InOutSlots = *Pending.StagedSlots;
  const TArray<uint32>& ResolvedInteractionLayers =
    Pending.ResolvedInteractionLayers;
  const TArray<uint64>& ResolvedAttachedNodeIds =
    Pending.ResolvedAttachedNodeIds;
  const FCrowdBehaviorPreparedBoundary& PreparedBehavior =
    Pending.PreparedBehavior;
  const TArray<FCrowdMassCommitTarget>& Targets = Pending.Targets;
  const TSharedPtr<FMixedMovementWork, ESPMode::ThreadSafe>& Work =
    Pending.Work;
  const double ProductStartSeconds = Pending.ProductStartSeconds;
  const double GatherEndSeconds = Pending.GatherEndSeconds;
  const double WorkEndSeconds =
    FPlatformTime::Seconds();
  const double SafetyStartSeconds = FPlatformTime::Seconds();

  FCrowdMassMovementFinalizeWorkInput SafetyFinalizeInput;
  SafetyFinalizeInput.FixedStepIndex = Work->Plan.FixedStepIndex;
  SafetyFinalizeInput.PlanRevision = Work->Plan.PlanRevision;
  SafetyFinalizeInput.Records.Reserve(Work->Plan.Records.Num());
  for (const FCrowdMassCommitRecord& Record : Work->Plan.Records)
  {
    const int32 SlotIndex =
      static_cast<int32>(Record.EntityRef.StableEntityId);
    if (!InOutSlots.IsValidIndex(SlotIndex)
      || !InOutSlots[SlotIndex].bActive
      || InOutSlots[SlotIndex].Facts.StableEntityRef != Record.EntityRef)
      return RejectBoundary(TEXT("safety_target"));
    const FSlotState& Slot = InOutSlots[SlotIndex];
    const bool bCandidateSafe =
      SpatialSafety.IsCandidateSafe(
        Record.EntityRef, Record.Movement.Position,
        MinimumSafeSeparationCm * 0.5f,
        ResolvedInteractionLayers[SlotIndex]);
    const FVector SafePosition =
      bCandidateSafe ? Record.Movement.Position : Slot.Location;
    const FVector SafeVelocity =
      bCandidateSafe ? Record.Movement.Velocity : FVector::ZeroVector;
    if (!bCandidateSafe)
      ++Work->SafetyHolds;
    if (!ResolvedInteractionLayers.IsValidIndex(SlotIndex)
      || !SpatialSafety.Update(
        Record.EntityRef, SafePosition,
        bCandidateSafe
          ? ResolvedInteractionLayers[SlotIndex]
          : Slot.InteractionLayer))
      return RejectBoundary(TEXT("safety_update"));
    if (bCandidateSafe)
    {
      InOutSlots[SlotIndex].AttachedNavNodeId =
        ResolvedAttachedNodeIds[SlotIndex];
      InOutSlots[SlotIndex].InteractionLayer =
        ResolvedInteractionLayers[SlotIndex];
    }
    SafetyFinalizeInput.Records.Add({
      Record.EntityRef,
      Record.Movement.AgentId,
      Record.EntityRef.LifecycleSerial,
      Record.CapabilityProfileKey,
      SafePosition,
      SafeVelocity,
      Record.Movement.YawDegrees});
  }
  const FCrowdMassMovementFinalizeWorkOutput SafetyFinalize =
    FCrowdMassMovementFinalizeWork::BuildCommitPlan(
      SafetyFinalizeInput);
  if (!SafetyFinalize.bCompleted
    || !SafetyFinalize.CommitPlan.bValid)
    return RejectBoundary(TEXT("safety_finalize"));
  Work->Plan = SafetyFinalize.CommitPlan;
  const double SafetyEndSeconds = FPlatformTime::Seconds();

  FCrowdBehaviorBoundaryMetadata BehaviorMetadata;
  for (const FCrowdBehaviorPreparedEntity& Entity
    : PreparedBehavior.Entities)
    BehaviorMetadata.SourceSetRevision = FMath::Max(
      BehaviorMetadata.SourceSetRevision,
      Entity.StagedSourceSet.Revision);
  BehaviorMetadata.SourceSetHash =
    PreparedBehavior.SourceSetHash;
  BehaviorMetadata.CommandBatchHash =
    PreparedBehavior.CommandBatchHash;
  BehaviorMetadata.ResolvedChannelHash =
    PreparedBehavior.ResolvedChannelHash;
  if (!Runner.BuildAndSealCommit(
      Work->Plan, {}, Targets, 0.0, &BehaviorMetadata))
    return RejectBoundary(TEXT("commit_envelope"));

  struct FResolvedWrite
  {
    FSlotState* Slot = nullptr;
    FTransformFragment* Transform = nullptr;
    const FCrowdMassCommitRecord* Record = nullptr;
  };
  TArray<FResolvedWrite> Writes;
  Writes.Reserve(Work->Plan.Records.Num());
  for (const FCrowdMassCommitRecord& Record
    : Work->Plan.Records)
  {
    const int32 SlotIndex =
      static_cast<int32>(Record.EntityRef.StableEntityId);
    FMassEntityHandle Entity;
    if (!InOutSlots.IsValidIndex(SlotIndex)
      || !InOutSlots[SlotIndex].bActive
      || InOutSlots[SlotIndex].Facts.StableEntityRef
        != Record.EntityRef
      || !LifecycleWorld.TryGetEntityHandle(
        Record.EntityRef, Entity))
      return RejectBoundary(TEXT("commit_target"));
    FTransformFragment* Transform =
      EntitySubsystem->GetMutableEntityManager()
        .GetFragmentDataPtr<FTransformFragment>(Entity);
    if (!Transform)
      return RejectBoundary(TEXT("transform_fragment"));
    Writes.Add({&InOutSlots[SlotIndex], Transform, &Record});
  }
  if (!Runner.MarkValidated(0.0))
    return RejectBoundary(TEXT("mark_validated"));
  for (const FResolvedWrite& Write : Writes)
  {
    Write.Slot->Location =
      Write.Record->Movement.Position;
    Write.Slot->Velocity =
      Write.Record->Movement.Velocity;
    Write.Slot->YawDegrees =
      Write.Record->Movement.YawDegrees;
    if (const int32* BlockedAge =
      Work->BlockedAgeByAgentId.Find(
        Write.Record->Movement.AgentId))
      Write.Slot->PreviousBlockedAgeSteps =
        *BlockedAge;
    Write.Transform->GetMutableTransform().SetLocation(
      Write.Record->Movement.Position);
    Write.Transform->GetMutableTransform().SetRotation(
      FRotator(
        0.0f, Write.Record->Movement.YawDegrees, 0.0f)
        .Quaternion());
  }
  MixedLocalPredictiveGrantStates =
    MoveTemp(Work->GrantStates);
  const int32 SafetyHolds = Work->SafetyHolds;
  const uint64 CommitHash = Runner.GetCommitEnvelope().StableHash;
  checkf(Runner.MarkCommitted(0.0),
    TEXT("Validated Mixed boundary failed during final apply"));
  if (Config.PopulationLimit == MaximumMixedPopulation
    && (FixedStepIndex <= 5 || FixedStepIndex % 150 == 0))
  {
    const double ProductEndSeconds =
      FPlatformTime::Seconds();
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxMovementPhases fixed_step=%lld gather_ms=%.3f runner_ms=%.3f movement_ms=%.3f particle_ms=%.3f facing_ms=%.3f safety_finalize_ms=%.3f commit_ms=%.3f"),
      FixedStepIndex,
      (GatherEndSeconds - ProductStartSeconds) * 1000.0,
      (WorkEndSeconds - GatherEndSeconds) * 1000.0,
      Work->MovementMilliseconds,
      Work->ParticleMilliseconds,
      Work->FacingMilliseconds,
      (SafetyEndSeconds - SafetyStartSeconds) * 1000.0,
      (ProductEndSeconds - SafetyEndSeconds) * 1000.0);
  }
  Complete(true, SafetyHolds, CommitHash);
  return true;
}

bool ACrowdDemoMixedSandboxCoordinator::GetOrBuildFlow(
  const FVector& Objective,
  const FCrowdNavSurfaceFlow*& OutFlow,
  const uint64 PreferredGoalNodeId,
  uint64* const OutGoalNodeId)
{
  OutFlow = nullptr;
  UWorld* World = GetWorld();
  UMassCrowdRuntimeSubsystem* Runtime =
    World ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>() : nullptr;
  if (!Runtime || !NavGraphHandle.IsValid()) return false;
  const FCrowdNavGraphResource& Resource = Runtime->GetNavGraphResource();
  if (!Resource.IsReady() || Resource.Graph != NavGraphHandle) return false;
  uint64 GoalNodeId = PreferredGoalNodeId;
  uint32 GoalLayer = 0;
  const int32 PreferredIndex =
    NavGraphHandle->FindNodeIndex(
      PreferredGoalNodeId);
  if (NavGraphHandle->Nodes.IsValidIndex(
      PreferredIndex)
    && DistanceSquaredToNavNode(
      Objective,
      NavGraphHandle->Nodes[PreferredIndex])
      <= FMath::Square(350.0))
  {
    GoalLayer =
      NavGraphHandle->Nodes[PreferredIndex].NavLayer;
  }
  else
  {
    GoalNodeId = 0;
    if (!FCrowdNavSurfaceGraphKernel::AttachClosest(
      *NavGraphHandle, Objective, 350.0f,
      GoalNodeId, GoalLayer))
      return false;
  }
  if (OutGoalNodeId)
    *OutGoalNodeId = GoalNodeId;
  if (const FCrowdNavFlowHandle* Existing =
    FlowHandleByGoalNode.Find(GoalNodeId))
  {
    const TSharedPtr<const FCrowdNavSurfaceFlow, ESPMode::ThreadSafe> Flow =
      Runtime->ResolveFlow(*Existing);
    OutFlow = Flow.Get();
    return OutFlow != nullptr;
  }
  FCrowdNavFlowHandle Handle;
  const FCrowdNavFlowKey Key{
    Resource.TopologyRevision, GoalNodeId, 1, GoalLayer};
  if (!Runtime->AcquireFlow(Key, Handle))
    return false;
  FlowHandleByGoalNode.Add(GoalNodeId, Handle);
  const TSharedPtr<const FCrowdNavSurfaceFlow, ESPMode::ThreadSafe> Flow =
    Runtime->ResolveFlow(Handle);
  OutFlow = Flow.Get();
  return OutFlow != nullptr;
}

bool ACrowdDemoMixedSandboxCoordinator::RebuildSpatialSafety()
{
  TArray<FCrowdSpatialSafetyAgent> Agents;
  Agents.Reserve(Slots.Num());
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    Agents.Add({
      Slot.Facts.StableEntityRef,
      Slot.Location,
      MinimumSafeSeparationCm * 0.5f,
      Slot.InteractionLayer});
  }
  return SpatialSafety.Build(Agents, MinimumSafeSeparationCm, 150.0f);
}

bool ACrowdDemoMixedSandboxCoordinator::BuildLifecycleOperation(
  FCrowdDemoContinuousLifecycleOperation& OutOperation)
{
  OutOperation = {};
  OutOperation.Sequence = NextLifecycleSequence;
  OutOperation.RelevantSetRevision = RelevantSetRevision + 1;
  OutOperation.FixedStepIndex = FixedStepIndex;

  if (PendingRespawnSlot != INDEX_NONE)
  {
    const FSlotState& Slot = Slots[PendingRespawnSlot];
    OutOperation.Kind = ECrowdDemoContinuousLifecycleOperationKind::Spawn;
    OutOperation.StableEntityId = PendingRespawnSlot;
    OutOperation.LifecycleSerial = Slot.Facts.StableEntityRef.LifecycleSerial + 1;
    OutOperation.NewMembershipKey = MembershipForDiagnosticLabel(
      static_cast<ECrowdActiveBehavior>(MakeAgentFacts(
        PendingRespawnSlot,
        OutOperation.LifecycleSerial).DerivedBehaviorLabel));
    return true;
  }
  if (PendingCombatDeathSlot != INDEX_NONE && Slots[PendingCombatDeathSlot].bActive)
  {
    const FSlotState& Slot = Slots[PendingCombatDeathSlot];
    OutOperation.Kind = ECrowdDemoContinuousLifecycleOperationKind::Despawn;
    OutOperation.StableEntityId = PendingCombatDeathSlot;
    OutOperation.LifecycleSerial = Slot.Facts.StableEntityRef.LifecycleSerial;
    OutOperation.DespawnReason = static_cast<uint8>(ECrowdDespawnReason::Death);
    return true;
  }
  for (int32 Offset = 0; Offset < Config.PopulationLimit; ++Offset)
  {
    const int32 Candidate = 1 + ((MembershipCursor - 1 + Offset) % Config.PopulationLimit);
    const FSlotState& Slot = Slots[Candidate];
    const uint32 Desired = MembershipForDiagnosticLabel(
      static_cast<ECrowdActiveBehavior>(Slot.Facts.DerivedBehaviorLabel));
    if (Slot.bActive && Slot.MembershipKey != Desired)
    {
      OutOperation.Kind = ECrowdDemoContinuousLifecycleOperationKind::Membership;
      OutOperation.StableEntityId = Candidate;
      OutOperation.LifecycleSerial = Slot.Facts.StableEntityRef.LifecycleSerial;
      OutOperation.PreviousMembershipKey = Slot.MembershipKey;
      OutOperation.NewMembershipKey = Desired;
      MembershipCursor = 1 + (Candidate % Config.PopulationLimit);
      return true;
    }
  }

  for (int32 Offset = 0; Offset < 4; ++Offset)
  {
    const int32 Candidate = 17 + ((RecycleCursor - 17 + Offset) % 4);
    if (Slots[Candidate].bActive)
    {
      OutOperation.Kind = ECrowdDemoContinuousLifecycleOperationKind::Despawn;
      OutOperation.StableEntityId = Candidate;
      OutOperation.LifecycleSerial = Slots[Candidate].Facts.StableEntityRef.LifecycleSerial;
      OutOperation.DespawnReason = static_cast<uint8>(ECrowdDespawnReason::BusinessRecycle);
      RecycleCursor = 17 + ((Candidate - 16) % 4);
      return true;
    }
  }
  return false;
}

bool ACrowdDemoMixedSandboxCoordinator::ApplyLifecycleOperation(
  const FCrowdDemoContinuousLifecycleOperation& Operation)
{
  if (Operation.StableEntityId == 0
    || Operation.StableEntityId >= static_cast<uint64>(Slots.Num())) return false;
  const int32 SlotIndex = static_cast<int32>(Operation.StableEntityId);
  FSlotState& Slot = Slots[SlotIndex];
  const FCrowdStableEntityRef Ref{1, Operation.StableEntityId, Operation.LifecycleSerial};
  const FCrowdLifecycleBatchHeader Header = MakeBatchHeader(Operation);
  ECrowdLifecycleBatchAcceptResult Result = ECrowdLifecycleBatchAcceptResult::RejectedInvalid;

  if (Operation.Kind == ECrowdDemoContinuousLifecycleOperationKind::Spawn)
  {
    FCrowdSpawnBatch Batch;
    Batch.Header = Header;
    Batch.Entries.Add({MakeAgentFacts(SlotIndex, Operation.LifecycleSerial), Operation.NewMembershipKey});
    FCrowdLifecycleBatchTransport::Finalize(Batch);
    Result = LifecycleWorld.ApplyAtBoundary(Batch);
    if (Result == ECrowdLifecycleBatchAcceptResult::Accepted)
    {
      InitializeSlotState(SlotIndex, Operation.LifecycleSerial);
    FCrowdCapabilityBinding Binding;
    Binding.ProfileKey =
        CrowdDemoBehaviorSchemas::FullProfile;
      if (!BehaviorSourceRuntime->RegisterEntity(
          Slot.Facts.StableEntityRef, Binding))
        return false;
      Slot.MembershipKey = Operation.NewMembershipKey;
      PendingRespawnSlot = INDEX_NONE;
      ++SpawnCount;
    }
  }
  else if (Operation.Kind == ECrowdDemoContinuousLifecycleOperationKind::Despawn)
  {
    if (Operation.DespawnReason >= static_cast<uint8>(ECrowdDespawnReason::Count)) return false;
    FCrowdDespawnBatch Batch;
    Batch.Header = Header;
    Batch.Entries.Add({Ref, static_cast<ECrowdDespawnReason>(Operation.DespawnReason)});
    FCrowdLifecycleBatchTransport::Finalize(Batch);
    Result = LifecycleWorld.ApplyAtBoundary(Batch);
    if (Result == ECrowdLifecycleBatchAcceptResult::Accepted)
    {
      if (!BehaviorSourceRuntime->RemoveEntity(Ref))
        return false;
      Slot.bActive = false;
      PendingRespawnSlot = SlotIndex;
      if (PendingCombatDeathSlot == SlotIndex) PendingCombatDeathSlot = INDEX_NONE;
      ++DespawnCount;
    }
  }
  else if (Operation.Kind == ECrowdDemoContinuousLifecycleOperationKind::Membership)
  {
    FCrowdMembershipBatch Batch;
    Batch.Header = Header;
    Batch.Entries.Add({Ref, Operation.PreviousMembershipKey, Operation.NewMembershipKey});
    FCrowdLifecycleBatchTransport::Finalize(Batch);
    Result = LifecycleWorld.ApplyAtBoundary(Batch);
    if (Result == ECrowdLifecycleBatchAcceptResult::Accepted)
    {
      Slot.MembershipKey = Operation.NewMembershipKey;
      ++MembershipChangeCount;
    }
  }
  if (Result != ECrowdLifecycleBatchAcceptResult::Accepted) return false;

  if (Operation.Kind
    != ECrowdDemoContinuousLifecycleOperationKind::Membership)
    MixedLocalPredictiveGrantStates.Reset();
  NextLifecycleSequence = Operation.Sequence + 1;
  RelevantSetRevision = Operation.RelevantSetRevision;
  MaxObservedPopulation = FMath::Max(
    MaxObservedPopulation, LifecycleWorld.GetActiveEntityCount());
  if (LifecycleWorld.GetActiveEntityCount() > Config.PopulationLimit) return false;
  bVisualSyncPending = !HasAuthority();
  return true;
}

FCrowdLifecycleBatchHeader ACrowdDemoMixedSandboxCoordinator::MakeBatchHeader(
  const FCrowdDemoContinuousLifecycleOperation& Operation) const
{
  FCrowdLifecycleBatchHeader Header;
  Header.BaseSnapshotRevision = Config.SnapshotRevision;
  Header.FixedStepIndex = Operation.FixedStepIndex;
  Header.RelevantSetRevision = Operation.RelevantSetRevision;
  Header.Sequence = Operation.Sequence;
  return Header;
}

FCrowdDemoMixedAgentState
ACrowdDemoMixedSandboxCoordinator::BuildReplicatedAgentState(
  const FSlotState& Slot) const
{
  FCrowdDemoMixedAgentState State;
  State.StableEntityId =
    Slot.Facts.StableEntityRef.StableEntityId;
  State.LifecycleSerial =
    Slot.Facts.StableEntityRef.LifecycleSerial;
  State.MembershipKey = Slot.MembershipKey;
  State.Location = Slot.Location;
  State.DerivedBehaviorLabel =
    Slot.Facts.DerivedBehaviorLabel;
  State.Health = static_cast<uint8>(
    FMath::Clamp(Slot.Health, 0, 100));
  State.FactionId = Slot.FactionId;
  State.AttackProfileId = Slot.AttackProfileId;
  State.AttackPhase =
    static_cast<uint8>(Slot.AttackState.Phase);
  State.AttackPhaseEnterFixedStep =
    Slot.AttackState.PhaseEnterFixedStep;
  State.AttackCooldownEndFixedStep =
    Slot.AttackState.CooldownEndFixedStep;
  State.AttackFireSequence =
    Slot.AttackState.FireSequence;
  State.AttackTargetProviderId =
    Slot.AttackState.TargetRef.ProviderId;
  State.AttackTargetStableEntityId =
    Slot.AttackState.TargetRef.StableEntityId;
  State.AttackTargetLifecycleSerial =
    Slot.AttackState.TargetRef.LifecycleSerial;
  State.TargetProviderId = Slot.Facts.TargetRef.ProviderId;
  State.TargetStableEntityId =
    Slot.Facts.TargetRef.StableEntityId;
  State.TargetLifecycleSerial =
    Slot.Facts.TargetRef.LifecycleSerial;
  State.TaskProviderId =
    Slot.Facts.BusinessTaskRef.ProviderId;
  State.TaskStableEntityId =
    Slot.Facts.BusinessTaskRef.StableEntityId;
  State.TaskLifecycleSerial =
    Slot.Facts.BusinessTaskRef.LifecycleSerial;
  State.ProjectileExpectedCount = ProjectileExpectedCount;
  State.ProjectileSpawnedCount = ProjectileSpawnedCount;
  State.ProjectileImpactCount = ProjectileImpactCount;
  State.ProjectileDamageCount = ProjectileDamageCount;
  State.ProjectileExpiredCount = ProjectileExpiredCount;
  State.ProjectileActiveCount = ProjectileActiveCount;
  State.ProjectileDuplicateCount = ProjectileDuplicateCount;
  State.ProjectileTraceHash = ProjectileTraceHash;
  State.AttackIntentCount = AttackIntentCount;
  State.AttackImpactCount = AttackImpactCount;
  State.AttackDamageCount = AttackDamageCount;
  State.AttackDeathCount = AttackDeathCount;
  State.AttackTargetSwitchCount = AttackTargetSwitchCount;
  State.MeleeAttackIntentCount = MeleeAttackIntentCount;
  State.MidRangeAttackIntentCount = MidRangeAttackIntentCount;
  State.RangedAttackIntentCount = RangedAttackIntentCount;
  return State;
}

void ACrowdDemoMixedSandboxCoordinator::PublishProductStateFrame()
{
  TArray<FCrowdReliableStateRecord> Records;
  TArray<FCrowdMovementCorrectionRecord> Corrections;
  Records.Reserve(Config.PopulationLimit);
  Corrections.Reserve(Config.PopulationLimit);
  constexpr int32 MaximumSourceSetRecordsPerFrame = 32;
  int32 SourceSetRecordCount = 0;
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    const FCrowdDemoMixedAgentState State =
      BuildReplicatedAgentState(Slot);
    const FCrowdStableEntityRef ReplicatedEntityRef{
      1, State.StableEntityId, State.LifecycleSerial};
    const uint64 HostFactHash =
      CalculateMixedHostFactHash(State);
    const uint64* LastHostFactHash =
      LastPublishedHostFactHashes.Find(ReplicatedEntityRef);
    if (!LastHostFactHash || *LastHostFactHash != HostFactHash)
    {
      FCrowdReliableStateRecord& Record =
        Records.AddDefaulted_GetRef();
      Record.Sequence = NextStateSequence++;
      Record.Kind = ECrowdReliableStateKind::HostEvent;
      Record.EntityRef = ReplicatedEntityRef;
      Record.Revision = static_cast<uint32>(
        FMath::Max<int64>(1, FixedStepIndex));
      EncodeMixedAgent(
        State, FixedStepIndex, NextLifecycleSequence,
        RelevantSetRevision, Record.Payload);
      Record.StableHash =
        FCrowdReplicationTransport::CalculateReliableRecordHash(Record);
      LastPublishedHostFactHashes.Add(
        ReplicatedEntityRef, HostFactHash);
    }
    if (BehaviorSourceRuntime
      && SourceSetRecordCount
        < MaximumSourceSetRecordsPerFrame)
    {
      const int32 PreviousRecordCount = Records.Num();
      const FCrowdDemoSourceStateFact SourceFact{
        ReplicatedEntityRef, State.DerivedBehaviorLabel};
      if (!FCrowdDemoSourceStatePublisher::AppendChanged(
          *BehaviorSourceRuntime,
          MakeArrayView(&SourceFact, 1),
          MaximumSourceSetRecordsPerFrame
            - SourceSetRecordCount,
          LastPublishedSourceSetRevisions,
          NextStateSequence,
          Records))
      {
        ++StaleRejectCount;
        return;
      }
      SourceSetRecordCount +=
        Records.Num() - PreviousRecordCount;
    }
    FCrowdMovementCorrectionRecord& Correction =
      Corrections.AddDefaulted_GetRef();
    Correction.EntityRef = ReplicatedEntityRef;
    Correction.Sequence = static_cast<uint64>(
      FMath::Max<int64>(1, FixedStepIndex + 1));
    Correction.FixedStepIndex = FixedStepIndex;
    Correction.Position = State.Location;
    Correction.StableHash =
      FCrowdReplicationTransport::CalculateMovementCorrectionHash(Correction);
  }
  for (TPair<TWeakObjectPtr<APlayerController>,
    TWeakObjectPtr<AMassCrowdReplicationActor>>& Pair
    : ReplicationChannels)
  {
    if (AMassCrowdReplicationActor* Channel = Pair.Value.Get())
    {
      Channel->PublishReliables(Records);
      if (!Channel->IsServerAwaitingBaselineAck())
        Channel->PublishMovementCorrections(Corrections);
    }
  }
}

void ACrowdDemoMixedSandboxCoordinator::RefreshReplicationChannels()
{
  UWorld* World = GetWorld();
  if (!World) return;
  for (auto It = ReplicationChannelEligibleSeconds.CreateIterator();
    It; ++It)
    if (!It.Key().IsValid())
      It.RemoveCurrent();
  for (auto It = ReplicationChannels.CreateIterator(); It; ++It)
  {
    AMassCrowdReplicationActor* Channel = It.Value().Get();
    if (!It.Key().IsValid() || !Channel)
    {
      It.RemoveCurrent();
      continue;
    }
    if (Channel->RequiresNewBaseline())
    {
      LastPublishedSourceSetRevisions.Reset();
      Channel->Destroy();
      It.RemoveCurrent();
    }
  }
  for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator();
    It; ++It)
  {
    APlayerController* Controller = It->Get();
    if (!Controller || ReplicationChannels.Contains(Controller)) continue;
    double* EligibleSeconds =
      ReplicationChannelEligibleSeconds.Find(Controller);
    if (!EligibleSeconds)
    {
      ReplicationChannelEligibleSeconds.Add(
        Controller, World->GetTimeSeconds() + 1.0);
      ForceNetUpdate();
      continue;
    }
    if (World->GetTimeSeconds() < *EligibleSeconds)
      continue;
    AMassCrowdReplicationActor* Channel =
      AMassCrowdReplicationActor::SpawnForController(*Controller);
    if (!Channel || !PublishBaseline(*Channel))
    {
      if (Channel) Channel->Destroy();
      *EligibleSeconds = World->GetTimeSeconds() + 1.0;
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoMixedSandboxReplication stage=baseline_retry owner=%s"),
        *GetNameSafe(Controller));
      continue;
    }
    LastPublishedSourceSetRevisions.Reset();
    ReplicationChannels.Add(Controller, Channel);
    ReplicationChannelEligibleSeconds.Remove(Controller);
  }
}

bool ACrowdDemoMixedSandboxCoordinator::PublishBaseline(
  AMassCrowdReplicationActor& Channel)
{
  TArray<FCrowdRelevantSnapshotEntityPayload> Entities;
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    FCrowdDemoMixedAgentState State =
      BuildReplicatedAgentState(Slot);
    EncodeMixedAgent(
      State, FixedStepIndex, NextLifecycleSequence,
      RelevantSetRevision,
      Entities.AddDefaulted_GetRef().Bytes);
  }
  FCrowdRelevantSnapshotLimits Limits;
  Limits.MaxEntityCount = Config.PopulationLimit;
  Limits.MaxChunkCount = Config.PopulationLimit;
  Limits.MaxEntitiesPerChunk =
    FMath::Min(128, FMath::Max(1, Config.PopulationLimit));
  Limits.MaxChunkPayloadBytes = 64 * 1024;
  Limits.MaxTotalPayloadBytes = 16 * 1024 * 1024;
  Limits.AssemblyTimeoutSeconds = 10.0;
  FCrowdRelevantSnapshotHeader Header;
  TArray<FCrowdRelevantSnapshotChunk> Chunks;
  if (!FCrowdRelevantSnapshotTransport::Build(
    FMath::Max(1u, Config.SnapshotRevision),
    FixedStepIndex,
    RelevantSetRevision,
    Entities,
    Limits,
    Header,
    Chunks))
  {
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoMixedSandboxReplication stage=baseline_build_reject entities=%d population_limit=%d chunk_entities=%d"),
      Entities.Num(), Config.PopulationLimit, Limits.MaxEntitiesPerChunk);
    return false;
  }
  if (!Channel.PublishBaseline(
    Header, Chunks, FMath::Max<uint64>(1, NextStateSequence)))
  {
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoMixedSandboxReplication stage=baseline_publish_reject revision=%u entities=%d chunks=%d resume=%llu"),
      Header.SnapshotRevision, Header.EntityCount, Header.ChunkCount,
      FMath::Max<uint64>(1, NextStateSequence));
    return false;
  }
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    const FCrowdDemoMixedAgentState State =
      BuildReplicatedAgentState(Slot);
    LastPublishedHostFactHashes.Add(
      Slot.Facts.StableEntityRef,
      CalculateMixedHostFactHash(State));
  }
  return true;
}

void ACrowdDemoMixedSandboxCoordinator::ConsumeProductReplication()
{
  UWorld* World = GetWorld();
  if (!World) return;
  AMassCrowdReplicationActor* Channel = nullptr;
  for (TActorIterator<AMassCrowdReplicationActor> It(World); It; ++It)
  {
    if (*It && !It->HasAuthority())
    {
      Channel = *It;
      break;
    }
  }
  if (!Channel || !Channel->IsReady()) return;
  if (LastConsumedReplicationChannel.Get() != Channel
    || Channel->GetCompletedBaselineRevision()
      != LastConsumedBaselineRevision)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxBaselineApply stage=begin revision=%u entities=%d"),
      Channel->GetCompletedBaselineRevision(),
      Channel->GetCompletedBaselineEntities().Num());
    UMassEntitySubsystem* EntitySubsystem =
      World->GetSubsystem<UMassEntitySubsystem>();
    if (!EntitySubsystem || !LifecycleArchetype.IsValid())
    {
      ++StaleRejectCount;
      return;
    }
    TArray<FCrowdLifecycleSnapshotEntity> Snapshot;
    int64 BaselineStep = INDEX_NONE;
    uint64 LifecycleResume = 0;
    uint32 BaselineRelevantRevision = 0;
    if (!BehaviorSourceRuntime)
    {
      Channel->RequestResync();
      ++StaleRejectCount;
      return;
    }
    for (FSlotState& Slot : Slots)
    {
      if (Slot.bActive && Slot.Facts.StableEntityRef.IsValid()
        && BehaviorSourceRuntime->FindSourceSet(
          Slot.Facts.StableEntityRef))
        BehaviorSourceRuntime->RemoveEntity(
          Slot.Facts.StableEntityRef);
      Slot.bActive = false;
    }
    for (const FCrowdRelevantSnapshotEntityPayload& Entity
      : Channel->GetCompletedBaselineEntities())
    {
      FCrowdDemoMixedAgentState State;
      int64 Step = 0;
      uint64 EntityLifecycleResume = 0;
      uint32 EntityRelevantRevision = 0;
      if (!DecodeMixedAgent(
          Entity.Bytes, State, Step,
          EntityLifecycleResume, EntityRelevantRevision)
        || State.StableEntityId == 0
        || State.StableEntityId >= static_cast<uint64>(Slots.Num())
        || State.DerivedBehaviorLabel >= static_cast<uint32>(
          ECrowdActiveBehavior::Count)
        || (BaselineStep != INDEX_NONE
          && (BaselineStep != Step
            || LifecycleResume != EntityLifecycleResume
            || BaselineRelevantRevision != EntityRelevantRevision)))
      {
        ++StaleRejectCount;
        return;
      }
      if (BaselineStep == INDEX_NONE)
      {
        BaselineStep = Step;
        LifecycleResume = EntityLifecycleResume;
        BaselineRelevantRevision = EntityRelevantRevision;
      }
      const int32 SlotIndex = static_cast<int32>(State.StableEntityId);
      FSlotState& Slot = Slots[SlotIndex];
      Slot.Facts = MakeAgentFacts(SlotIndex, State.LifecycleSerial);
      Slot.Facts.DerivedBehaviorLabel = State.DerivedBehaviorLabel;
      Slot.Facts.TargetRef = {
        State.TargetProviderId,
        State.TargetStableEntityId,
        State.TargetLifecycleSerial};
      Slot.Facts.BusinessTaskRef = {
        State.TaskProviderId,
        State.TaskStableEntityId,
        State.TaskLifecycleSerial};
      Slot.Facts.FactionKey = State.FactionId;
      Slot.Location = State.Location;
      Slot.MembershipKey = State.MembershipKey;
      Slot.Health = State.Health;
      Slot.FactionId = State.FactionId;
      Slot.AttackProfileId = State.AttackProfileId;
      Slot.AttackState.Phase =
        static_cast<ECrowdDemoAttackPlannerPhase>(
          State.AttackPhase);
      Slot.AttackState.PhaseEnterFixedStep =
        State.AttackPhaseEnterFixedStep;
      Slot.AttackState.CooldownEndFixedStep =
        State.AttackCooldownEndFixedStep;
      Slot.AttackState.FireSequence =
        State.AttackFireSequence;
      Slot.AttackState.TargetRef = {
        State.AttackTargetProviderId,
        State.AttackTargetStableEntityId,
        State.AttackTargetLifecycleSerial};
      Slot.AttackState.LockedTargetRef =
        Slot.AttackState.TargetRef;
      Slot.AttackState.bCommitIssued =
        Slot.AttackState.Phase
          == ECrowdDemoAttackPlannerPhase::Commit
        || Slot.AttackState.Phase
          == ECrowdDemoAttackPlannerPhase::Recovery
        || Slot.AttackState.Phase
          == ECrowdDemoAttackPlannerPhase::Cooldown;
      Slot.bActive = true;
      ProjectileExpectedCount =
        State.ProjectileExpectedCount;
      ProjectileSpawnedCount =
        State.ProjectileSpawnedCount;
      ProjectileImpactCount =
        State.ProjectileImpactCount;
      ProjectileDamageCount =
        State.ProjectileDamageCount;
      ProjectileExpiredCount =
        State.ProjectileExpiredCount;
      ProjectileActiveCount =
        State.ProjectileActiveCount;
      ProjectileDuplicateCount =
        State.ProjectileDuplicateCount;
      ProjectileTraceHash =
        State.ProjectileTraceHash;
      AttackIntentCount = State.AttackIntentCount;
      AttackImpactCount = State.AttackImpactCount;
      AttackDamageCount = State.AttackDamageCount;
      AttackDeathCount = State.AttackDeathCount;
      AttackTargetSwitchCount =
        State.AttackTargetSwitchCount;
      MeleeAttackIntentCount =
        State.MeleeAttackIntentCount;
      MidRangeAttackIntentCount =
        State.MidRangeAttackIntentCount;
      RangedAttackIntentCount =
        State.RangedAttackIntentCount;
      Snapshot.Add({Slot.Facts, Slot.MembershipKey});
    }
    if (BaselineStep < 0 || LifecycleResume == 0
      || BaselineRelevantRevision == 0)
    {
      ++StaleRejectCount;
      return;
    }
    Snapshot.Sort([](const auto& A, const auto& B)
    {
      return A.AgentFacts.StableEntityRef < B.AgentFacts.StableEntityRef;
    });
    FCrowdLifecycleBatchLimits LifecycleLimits;
    LifecycleLimits.MaxSnapshotEntities = Config.PopulationLimit;
    LifecycleLimits.MaxEntriesPerBatch = Config.PopulationLimit;
    LifecycleLimits.MaxTrackedSlots = Config.PopulationLimit;
    LifecycleLimits.MaxSequenceHistory = 256;
    if (bPresentationProfileRegistered)
    {
      UMassCrowdPresentationSubsystem* Presentation =
        World->GetSubsystem<UMassCrowdPresentationSubsystem>();
      if (!Presentation || !Presentation->ResetProfile(
          MixedPresentationProfileKey))
      {
        ++StaleRejectCount;
        return;
      }
      PresentedEntitiesByStableId.Reset();
    }
    LifecycleWorld.Reset();
    if (!LifecycleWorld.InitializeFromSnapshot(
      EntitySubsystem->GetMutableEntityManager(),
      LifecycleArchetype,
      Channel->GetCompletedBaselineRevision(),
      BaselineStep,
      BaselineRelevantRevision,
      Snapshot,
      LifecycleLimits,
      LifecycleResume))
    {
      ++StaleRejectCount;
      return;
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxBaselineApply stage=lifecycle_ready revision=%u"),
      Channel->GetCompletedBaselineRevision());
    for (int32 SlotIndex = 1;
      SlotIndex < Slots.Num(); ++SlotIndex)
    {
      if (!Slots[SlotIndex].bActive) continue;
      FCrowdCapabilityBinding Binding;
      Binding.ProfileKey =
        CrowdDemoBehaviorSchemas::FullProfile;
      if (!BehaviorSourceRuntime->RegisterEntity(
          Slots[SlotIndex].Facts.StableEntityRef, Binding))
      {
        Channel->RequestResync();
        ++StaleRejectCount;
        return;
      }
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxBaselineApply stage=sources_ready revision=%u"),
      Channel->GetCompletedBaselineRevision());
    for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
    {
      if (!Slots[SlotIndex].bActive) continue;
      FMassEntityHandle EntityHandle;
      if (!LifecycleWorld.TryGetEntityHandle(
          Slots[SlotIndex].Facts.StableEntityRef, EntityHandle))
      {
        ++StaleRejectCount;
        return;
      }
      FTransformFragment* Transform =
        EntitySubsystem->GetMutableEntityManager()
          .GetFragmentDataPtr<FTransformFragment>(EntityHandle);
      if (!Transform)
      {
        ++StaleRejectCount;
        return;
      }
      Transform->GetMutableTransform().SetLocation(
        Slots[SlotIndex].Location);
    }
    LastConsumedBaselineRevision = Channel->GetCompletedBaselineRevision();
    LastConsumedReplicationChannel = Channel;
    LastReceivedFixedStep = BaselineStep;
    NextLifecycleSequence = LifecycleResume;
    RelevantSetRevision = BaselineRelevantRevision;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoMixedSandboxBaselineApply stage=complete revision=%u fixed_step=%lld"),
      LastConsumedBaselineRevision, LastReceivedFixedStep);
  }

  TArray<FCrowdReplicationApplyFrame> ApplyFrames;
  if (!Channel->DrainClientApplyFrames(ApplyFrames))
  {
    if (Channel->GetClientState().RequiresResync())
      ++StaleRejectCount;
    return;
  }
  for (const FCrowdReplicationApplyFrame& Frame : ApplyFrames)
  {
    if (Frame.Kind == ECrowdReplicationApplyFrameKind::ReliableState)
    {
      for (const FCrowdReliableStateRecord& Record : Frame.ReliableRecords)
      {
        if (Record.Kind == ECrowdReliableStateKind::HostEvent)
        {
          FCrowdDemoMixedAgentState State;
          int64 Step = 0;
          uint64 IgnoredLifecycleResume = 0;
          uint32 IgnoredRelevantRevision = 0;
          if (!DecodeMixedAgent(
              Record.Payload, State, Step,
              IgnoredLifecycleResume, IgnoredRelevantRevision)
            || !ApplyReplicatedAgentState(State, Step))
          {
            ++StaleRejectCount;
            return;
          }
        }
        else if (Record.Kind == ECrowdReliableStateKind::Spawn
          || Record.Kind == ECrowdReliableStateKind::Despawn
          || Record.Kind == ECrowdReliableStateKind::Membership)
        {
          FCrowdDemoContinuousLifecycleOperation Operation;
          if (!DecodeLifecycleOperation(Record.Payload, Operation)
            || !ApplyLifecycleOperation(Operation))
          {
            ++StaleRejectCount;
            return;
          }
        }
        else if (Record.Kind
          == ECrowdReliableStateKind::BehaviorSourceSet)
        {
          FCrowdBehaviorSourceSetReplicationRecord SourceRecord;
          if (!BehaviorSourceRuntime
            || !FCrowdReplicationCodec::DecodeBehaviorSourceSet(
              Record.Payload,
              BehaviorSourceRuntime->GetRegistryHash(),
              BehaviorSourceRuntime->GetContextSchemaHash(),
              SourceRecord)
            || SourceRecord.SourceSet.EntityRef
              != Record.EntityRef
            || !BehaviorSourceRuntime->ApplyReplicatedSourceSet(
              SourceRecord.SourceSet))
          {
            Channel->RequestResync();
            ++StaleRejectCount;
            return;
          }
        }
        else
        {
          ++StaleRejectCount;
          return;
        }
        LastReceivedStateSequence = Record.Sequence;
      }
    }
    else if (Frame.Kind
      == ECrowdReplicationApplyFrameKind::MovementCorrection)
    {
      for (const FCrowdMovementCorrectionRecord& Correction
        : Frame.Corrections)
      {
        const int32 SlotIndex = static_cast<int32>(
          Correction.EntityRef.StableEntityId);
        if (Slots.IsValidIndex(SlotIndex)
          && Slots[SlotIndex].Facts.StableEntityRef
            == Correction.EntityRef)
        {
          Slots[SlotIndex].Location = Correction.Position;
        }
      }
    }
  }
  LastExpectedEntitySetHash = LifecycleWorld.CalculateEntitySetHash();
  LastExpectedMembershipHash = LifecycleWorld.CalculateMembershipHash();
  bVisualSyncPending = true;
}

bool ACrowdDemoMixedSandboxCoordinator::ApplyReplicatedAgentState(
  const FCrowdDemoMixedAgentState& State,
  const int64 InFixedStepIndex)
{
  if (State.StableEntityId == 0
    || State.StableEntityId >= static_cast<uint64>(Slots.Num())
    || State.DerivedBehaviorLabel >=
      static_cast<uint32>(ECrowdActiveBehavior::Count)
    || State.AttackPhase > static_cast<uint8>(
      ECrowdDemoAttackPlannerPhase::Cooldown)
    || (bMixedCombatIntegration
      && (State.FactionId < 1 || State.FactionId > 2
        || (State.AttackProfileId
            != CrowdDemoAttackProfileIds::Melee
          && State.AttackProfileId
            != CrowdDemoAttackProfileIds::MidRange
          && State.AttackProfileId
            != CrowdDemoAttackProfileIds::Ranged))))
  {
    if (!bClientApplyFailureLogged)
    {
      bClientApplyFailureLogged = true;
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoMixedSandboxClientApplyReject reason=invalid_payload entity=%llu lifecycle=%u behavior=%u step=%lld slots=%d"),
        State.StableEntityId, State.LifecycleSerial,
        State.DerivedBehaviorLabel,
        InFixedStepIndex, Slots.Num());
    }
    return false;
  }
  FSlotState& Slot = Slots[static_cast<int32>(State.StableEntityId)];
  if (!Slot.bActive
    || Slot.Facts.StableEntityRef.LifecycleSerial != State.LifecycleSerial)
  {
    if (!bClientApplyFailureLogged)
    {
      bClientApplyFailureLogged = true;
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoMixedSandboxClientApplyReject reason=lifecycle entity=%llu incoming=%u local=%u active=%d step=%lld last_step=%lld"),
        State.StableEntityId, State.LifecycleSerial,
        Slot.Facts.StableEntityRef.LifecycleSerial,
        Slot.bActive ? 1 : 0, InFixedStepIndex, LastReceivedFixedStep);
    }
    return false;
  }
  Slot.Location = State.Location;
  Slot.MembershipKey = State.MembershipKey;
  Slot.Facts.DerivedBehaviorLabel = State.DerivedBehaviorLabel;
  Slot.Facts.TargetRef = {
    State.TargetProviderId,
    State.TargetStableEntityId,
    State.TargetLifecycleSerial};
  Slot.Facts.BusinessTaskRef = {
    State.TaskProviderId,
    State.TaskStableEntityId,
    State.TaskLifecycleSerial};
  Slot.Health = State.Health;
  Slot.FactionId = State.FactionId;
  Slot.Facts.FactionKey = State.FactionId;
  Slot.AttackProfileId = State.AttackProfileId;
  Slot.AttackState.Phase =
    static_cast<ECrowdDemoAttackPlannerPhase>(
      State.AttackPhase);
  Slot.AttackState.PhaseEnterFixedStep =
    State.AttackPhaseEnterFixedStep;
  Slot.AttackState.CooldownEndFixedStep =
    State.AttackCooldownEndFixedStep;
  Slot.AttackState.FireSequence =
    State.AttackFireSequence;
  Slot.AttackState.TargetRef = {
    State.AttackTargetProviderId,
    State.AttackTargetStableEntityId,
    State.AttackTargetLifecycleSerial};
  Slot.AttackState.LockedTargetRef =
    Slot.AttackState.TargetRef;
  Slot.AttackState.bCommitIssued =
    Slot.AttackState.Phase
      == ECrowdDemoAttackPlannerPhase::Commit
    || Slot.AttackState.Phase
      == ECrowdDemoAttackPlannerPhase::Recovery
    || Slot.AttackState.Phase
      == ECrowdDemoAttackPlannerPhase::Cooldown;
  ProjectileExpectedCount = State.ProjectileExpectedCount;
  ProjectileSpawnedCount = State.ProjectileSpawnedCount;
  ProjectileImpactCount = State.ProjectileImpactCount;
  ProjectileDamageCount = State.ProjectileDamageCount;
  ProjectileExpiredCount = State.ProjectileExpiredCount;
  ProjectileActiveCount = State.ProjectileActiveCount;
  ProjectileDuplicateCount = State.ProjectileDuplicateCount;
  ProjectileTraceHash = State.ProjectileTraceHash;
  AttackIntentCount = State.AttackIntentCount;
  AttackImpactCount = State.AttackImpactCount;
  AttackDamageCount = State.AttackDamageCount;
  AttackDeathCount = State.AttackDeathCount;
  AttackTargetSwitchCount = State.AttackTargetSwitchCount;
  MeleeAttackIntentCount = State.MeleeAttackIntentCount;
  MidRangeAttackIntentCount =
    State.MidRangeAttackIntentCount;
  RangedAttackIntentCount = State.RangedAttackIntentCount;
  SeenBehaviorBits |= BehaviorBit(
    static_cast<ECrowdActiveBehavior>(Slot.Facts.DerivedBehaviorLabel));
  LastReceivedFixedStep = FMath::Max(LastReceivedFixedStep, InFixedStepIndex);
  const bool bApplied = LifecycleWorld.ApplyAgentFactsCorrectionAtBoundary(
    FMath::Max(InFixedStepIndex, LifecycleWorld.GetLastAppliedFixedStep()),
    Slot.Facts);
  if (!bApplied && !bClientApplyFailureLogged)
  {
    bClientApplyFailureLogged = true;
    UE_LOG(LogTemp, Warning,
      TEXT("CrowdDemoMixedSandboxClientApplyReject reason=runtime_facts entity=%llu lifecycle=%u behavior=%u well_formed=%d step=%lld world_step=%lld"),
      State.StableEntityId, State.LifecycleSerial,
      State.DerivedBehaviorLabel,
      Slot.Facts.IsWellFormed() ? 1 : 0, InFixedStepIndex,
      LifecycleWorld.GetLastAppliedFixedStep());
  }
  return bApplied;
}

void ACrowdDemoMixedSandboxCoordinator::SyncClientVisualsIncremental()
{
  UWorld* World = GetWorld();
  ACrowdDemoReplicator* Replicator = World ? FindMixedVisualHost(*World) : nullptr;
  UMassCrowdPresentationSubsystem* Presentation = World
    ? World->GetSubsystem<UMassCrowdPresentationSubsystem>() : nullptr;
  UInstancedStaticMeshComponent* Instances = Replicator
    ? Replicator->GetCrowdInstancesForClientVisuals() : nullptr;
  if (!Replicator || !Presentation || !Instances) return;

  if (!bClientVisualsInitialized)
  {
    Replicator->ClearCrowdVisualInstances();
    const TSharedRef<FCrowdDemoIsmPresentationSink> Sink =
      MakeShared<FCrowdDemoIsmPresentationSink>(*Instances);
    if (!Presentation->RegisterProfile(
        MixedPresentationProfileKey, Sink))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMixedSandbox role=client stage=presentation_profile"));
      ++StaleRejectCount;
      return;
    }
    bPresentationProfileRegistered = true;
    bClientVisualsInitialized = true;
  }
  const uint64 Sequence = ++PresentationSequence;
  TArray<FCrowdPresentationOperation> Operations;
  TMap<uint64, FCrowdStableEntityRef> NextPresented;
  for (int32 SlotIndex = 1; SlotIndex < Slots.Num(); ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    const uint64 StableId =
      Slot.Facts.StableEntityRef.StableEntityId;
    if (const FCrowdStableEntityRef* Presented =
      PresentedEntitiesByStableId.Find(StableId);
      Presented
      && (!Slot.bActive
        || *Presented != Slot.Facts.StableEntityRef))
    {
      FCrowdPresentationOperation& Despawn =
        Operations.AddDefaulted_GetRef();
      Despawn.Kind =
        ECrowdPresentationOperationKind::Despawn;
      Despawn.EntityRef = *Presented;
      Despawn.ProfileKey = MixedPresentationProfileKey;
      Despawn.Sequence = Sequence;
    }
    if (Slot.bActive)
    {
      FCrowdPresentationState State;
      State.EntityRef = Slot.Facts.StableEntityRef;
      State.Transform = FTransform(
        FRotator::ZeroRotator,
        Slot.Location + FVector(0, 0, 45),
        FVector(34));
      State.ProfileKey = MixedPresentationProfileKey;
      State.VisualState =
        Slot.Facts.DerivedBehaviorLabel ==
          static_cast<uint32>(ECrowdActiveBehavior::Attack) ? 1 : 0;
      if (Slot.Facts.DerivedBehaviorLabel ==
        static_cast<uint32>(ECrowdActiveBehavior::HaulDeliver))
        State.CargoRef = Slot.Facts.BusinessTaskRef;
      State.Sequence = Sequence;
      State.SampleServerSeconds = FixedStepIndex * MixedFixedStepSeconds;
      FCrowdPresentationOperation& Operation =
        Operations.AddDefaulted_GetRef();
      Operation.Kind =
        PresentedEntitiesByStableId.Contains(StableId)
          && PresentedEntitiesByStableId.FindChecked(
            StableId) == State.EntityRef
          ? ECrowdPresentationOperationKind::Update
          : ECrowdPresentationOperationKind::Spawn;
      Operation.State = State;
      Operation.EntityRef = State.EntityRef;
      Operation.ProfileKey = MixedPresentationProfileKey;
      Operation.Sequence = Sequence;
      NextPresented.Add(StableId, State.EntityRef);
    }
  }
  FCrowdPreparedPresentationFrame PreparedFrame;
  uint64 SourceFrameHash =
    LifecycleWorld.CalculateEntitySetHash();
  SourceFrameHash = FoldMixedHash(
    SourceFrameHash, LastReceivedStateSequence);
  if (!Presentation->PrepareFrame(
      SourceFrameHash == 0 ? 1 : SourceFrameHash,
      Operations, PreparedFrame)
    || !Presentation->ApplyPreparedFrame(PreparedFrame))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=client stage=presentation_frame operations=%d"),
      Operations.Num());
    ++StaleRejectCount;
    return;
  }
  PresentedEntitiesByStableId = MoveTemp(NextPresented);
  if (Instances->GetInstanceCount() != LifecycleWorld.GetActiveEntityCount())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoMixedSandbox role=client stage=visual_count active=%d visual=%d"),
      LifecycleWorld.GetActiveEntityCount(), Instances->GetInstanceCount());
    ++StaleRejectCount;
    return;
  }
  bVisualSyncPending = false;
}

void ACrowdDemoMixedSandboxCoordinator::LogCheckpoint()
{
  UWorld* World = GetWorld();
  const ACrowdDemoReplicator* Replicator = World ? FindMixedVisualHost(*World) : nullptr;
  const int32 Visible = !HasAuthority() && Replicator
    ? Replicator->GetCrowdVisualInstanceCount() : 0;
  float MinimumHaulObjectiveDistanceCm =
    TNumericLimits<float>::Max();
  float ClosestHaulObjectiveDistance2DCm =
    TNumericLimits<float>::Max();
  float ClosestHaulObjectiveHeightDeltaCm =
    TNumericLimits<float>::Max();
  float MaximumHaulObjectiveDistanceCm = 0.0f;
  int32 MovingHaulAgentCount = 0;
  for (int32 SlotIndex = 1;
    SlotIndex <= 6 && Slots.IsValidIndex(SlotIndex);
    ++SlotIndex)
  {
    const FSlotState& Slot = Slots[SlotIndex];
    if (!Slot.bActive) continue;
    const bool bCarrying =
      BusinessLedger.GetCargoCarrier(
        static_cast<uint64>(SlotIndex))
      == static_cast<uint64>(SlotIndex);
    const FVector Objective = Marker(
      bCarrying ? TEXT("CrowdNavHigh") : TEXT("CrowdNavLower"),
      FVector::ZeroVector);
    const float Distance =
      FVector::Distance(Slot.Location, Objective);
    if (Distance < MinimumHaulObjectiveDistanceCm)
    {
      MinimumHaulObjectiveDistanceCm = Distance;
      ClosestHaulObjectiveDistance2DCm =
        FVector::Dist2D(Slot.Location, Objective);
      ClosestHaulObjectiveHeightDeltaCm =
        static_cast<float>(FMath::Abs(
          Slot.Location.Z - Objective.Z));
    }
    MaximumHaulObjectiveDistanceCm = FMath::Max(
      MaximumHaulObjectiveDistanceCm, Distance);
    if (!Slot.Velocity.IsNearlyZero())
      ++MovingHaulAgentCount;
  }
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoMixedSandboxCheckpoint role=%s fixed_step=%lld state_sequence=%llu active=%d visible=%d transitions=%d seen_behavior_bits=0x%08x pickups=%d deliveries=%d combat_quantity=%d commits=%d duplicate_commits=%d spawned=%d despawned=%d membership=%d max_population=%d safety_holds=%d min_separation_cm=%.2f haul_distance_cm=%.2f..%.2f haul_min_2d_cm=%.2f haul_min_z_cm=%.2f haul_moving=%d stale_reject=%d projectile_expected=%d projectile_spawned=%d projectile_impacted=%d projectile_damage=%d projectile_duplicate=%d projectile_hash=%llu entity_hash=%llu membership_hash=%llu commit_hash=%llu source=MassCrowdBoundaryRunner+MassCrowdNavRuntime+ApplyFrame"),
    HasAuthority() ? TEXT("server") : TEXT("client"),
    HasAuthority() ? FixedStepIndex : LastReceivedFixedStep,
    HasAuthority() ? NextStateSequence - 1 : LastReceivedStateSequence,
    LifecycleWorld.GetActiveEntityCount(), Visible, BehaviorTransitionCount,
    SeenBehaviorBits,
    BusinessLedger.GetPickupCount(), BusinessLedger.GetDeliveryCount(),
    BusinessLedger.GetCombatHitQuantity(13) + BusinessLedger.GetCombatHitQuantity(14)
      + BusinessLedger.GetCombatHitQuantity(15) + BusinessLedger.GetCombatHitQuantity(16),
    BusinessLedger.GetAppliedCommitCount(), DuplicateCommitCount,
    SpawnCount, DespawnCount, MembershipChangeCount, MaxObservedPopulation,
    SafetyHoldCount, MinimumSeparationCm,
    MinimumHaulObjectiveDistanceCm,
    MaximumHaulObjectiveDistanceCm,
    ClosestHaulObjectiveDistance2DCm,
    ClosestHaulObjectiveHeightDeltaCm,
    MovingHaulAgentCount, StaleRejectCount,
    ProjectileExpectedCount,
    ProjectileSpawnedCount,
    ProjectileImpactCount,
    ProjectileDamageCount,
    ProjectileDuplicateCount,
    ProjectileTraceHash,
    LifecycleWorld.CalculateEntitySetHash(),
    LifecycleWorld.CalculateMembershipHash(),
    LastBoundaryCommitHash);
}

void ACrowdDemoMixedSandboxCoordinator::TryLogPass()
{
  if (bMixedCombatIntegration)
  {
    if (HasAuthority() && !bServerPassLogged
      && FixedStepIndex >= 600)
    {
      int32 AliveCount = 0;
      int32 ReferencedDeadCount = 0;
      for (int32 SlotIndex = 1;
        SlotIndex < Slots.Num(); ++SlotIndex)
      {
        const FSlotState& Slot = Slots[SlotIndex];
        if (!Slot.bActive) continue;
        AliveCount += Slot.Health > 0 ? 1 : 0;
        if (Slot.Health <= 0) continue;
        if (Slot.AttackState.TargetRef.IsValid())
        {
          const int32 TargetIndex = static_cast<int32>(
            Slot.AttackState.TargetRef.StableEntityId);
          if (!Slots.IsValidIndex(TargetIndex)
            || Slots[TargetIndex].Health <= 0
            || Slots[TargetIndex].Facts.StableEntityRef
              != Slot.AttackState.TargetRef)
            ++ReferencedDeadCount;
        }
      }
      const bool bProjectileConserved =
        ProjectileSpawnedCount
          == ProjectileImpactCount
            + ProjectileExpiredCount
            + ProjectileActiveCount;
      const bool bPassed =
        Config.PopulationLimit == 20
        && LifecycleWorld.GetActiveEntityCount() == 20
        && AliveCount > 0 && AliveCount < 20
        && AttackIntentCount > 0
        && AttackImpactCount > 0
        && AttackDamageCount > 0
        && AttackDeathCount > 0
        && AttackTargetSwitchCount > 0
        && MeleeAttackIntentCount > 0
        && MidRangeAttackIntentCount > 0
        && RangedAttackIntentCount > 0
        && ProjectileSpawnedCount > 0
        && ProjectileImpactCount > 0
        && bProjectileConserved
        && ProjectileDuplicateCount == 0
        && ReferencedDeadCount == 0
        && MinimumSeparationCm
          >= MinimumSafeSeparationCm - 0.5f
        && Percentile95(ServerStepMilliseconds)
          <= MixedMaximumFixedStepP95Ms
        && StaleRejectCount == 0;
      if (bPassed)
      {
        bServerPassLogged = true;
        UE_LOG(LogTemp, Display,
          TEXT("PASS CrowdDemoMixedCombat role=server fixed_step=%lld population=20 alive=%d attack_intent=%d melee_intent=%d midrange_intent=%d ranged_intent=%d impact=%d damage=%d death=%d target_switch=%d target_region_rebuild=%d referenced_dead=%d projectile_spawned=%d projectile_impacted=%d projectile_expired=%d projectile_active=%d projectile_duplicate=%d projectile_conserved=1 safety_holds=%d min_separation_cm=%.2f fixed_step_ms_p95=%.3f entity_hash=%llu membership_hash=%llu commit_hash=%llu"),
          FixedStepIndex, AliveCount, AttackIntentCount,
          MeleeAttackIntentCount, MidRangeAttackIntentCount,
          RangedAttackIntentCount, AttackImpactCount,
          AttackDamageCount, AttackDeathCount,
          AttackTargetSwitchCount, AttackTargetSwitchCount,
          ReferencedDeadCount,
          ProjectileSpawnedCount, ProjectileImpactCount,
          ProjectileExpiredCount, ProjectileActiveCount,
          ProjectileDuplicateCount, SafetyHoldCount,
          MinimumSeparationCm,
          Percentile95(ServerStepMilliseconds),
          LifecycleWorld.CalculateEntitySetHash(),
          LifecycleWorld.CalculateMembershipHash(),
          LastBoundaryCommitHash);
      }
    }

    if (!HasAuthority() && !bClientPassLogged
      && LastReceivedFixedStep >= 600
      && bClientVisualsInitialized && !bVisualSyncPending)
    {
      UWorld* World = GetWorld();
      ACrowdDemoReplicator* Replicator =
        World ? FindMixedVisualHost(*World) : nullptr;
      const int32 Visible = Replicator
        ? Replicator->GetCrowdVisualInstanceCount() : 0;
      int32 AliveCount = 0;
      for (int32 SlotIndex = 1;
        SlotIndex < Slots.Num(); ++SlotIndex)
        AliveCount += Slots[SlotIndex].bActive
          && Slots[SlotIndex].Health > 0 ? 1 : 0;
      const bool bPassed =
        Visible == LifecycleWorld.GetActiveEntityCount()
        && LifecycleWorld.GetActiveEntityCount() == 20
        && AliveCount > 0 && AliveCount < 20
        && LifecycleWorld.CalculateEntitySetHash()
          == LastExpectedEntitySetHash
        && LifecycleWorld.CalculateMembershipHash()
          == LastExpectedMembershipHash
        && AttackIntentCount > 0
        && AttackImpactCount > 0
        && AttackDamageCount > 0
        && AttackDeathCount > 0
        && AttackTargetSwitchCount > 0
        && MeleeAttackIntentCount > 0
        && MidRangeAttackIntentCount > 0
        && RangedAttackIntentCount > 0
        && ProjectileSpawnedCount > 0
        && ProjectileImpactCount > 0
        && ProjectileSpawnedCount
          == ProjectileImpactCount
            + ProjectileExpiredCount
            + ProjectileActiveCount
        && ProjectileDuplicateCount == 0
        && StaleRejectCount == 0;
      if (bPassed)
      {
        bClientPassLogged = true;
        UE_LOG(LogTemp, Display,
          TEXT("PASS CrowdDemoMixedCombat role=client fixed_step=%lld population=20 alive=%d visible=%d state_sequence=%llu attack_intent=%d melee_intent=%d midrange_intent=%d ranged_intent=%d impact=%d damage=%d death=%d target_switch=%d target_region_rebuild=%d projectile_spawned=%d projectile_impacted=%d projectile_expired=%d projectile_active=%d projectile_duplicate=%d projectile_conserved=1 entity_hash=%llu membership_hash=%llu"),
          LastReceivedFixedStep, AliveCount, Visible,
          LastReceivedStateSequence, AttackIntentCount,
          MeleeAttackIntentCount, MidRangeAttackIntentCount,
          RangedAttackIntentCount, AttackImpactCount,
          AttackDamageCount, AttackDeathCount,
          AttackTargetSwitchCount, AttackTargetSwitchCount,
          ProjectileSpawnedCount,
          ProjectileImpactCount, ProjectileExpiredCount,
          ProjectileActiveCount, ProjectileDuplicateCount,
          LifecycleWorld.CalculateEntitySetHash(),
          LifecycleWorld.CalculateMembershipHash());
      }
    }
    return;
  }

  if (HasAuthority() && !bServerPassLogged && FixedStepIndex >= 600)
  {
    const uint32 RequiredBehaviors =
      BehaviorBit(ECrowdActiveBehavior::HaulPickup)
      | BehaviorBit(ECrowdActiveBehavior::HaulDeliver)
      | BehaviorBit(ECrowdActiveBehavior::Pursue)
      | BehaviorBit(ECrowdActiveBehavior::Attack)
      | BehaviorBit(ECrowdActiveBehavior::Guard)
      | BehaviorBit(ECrowdActiveBehavior::Flee)
      | BehaviorBit(ECrowdActiveBehavior::Wander)
      | BehaviorBit(ECrowdActiveBehavior::MoveTo);
    const int32 CombatQuantity = BusinessLedger.GetCombatHitQuantity(13)
      + BusinessLedger.GetCombatHitQuantity(14)
      + BusinessLedger.GetCombatHitQuantity(15)
      + BusinessLedger.GetCombatHitQuantity(16);
    const bool bPassed = LifecycleWorld.GetActiveEntityCount() == Config.PopulationLimit
      && MaxObservedPopulation == Config.PopulationLimit
      && SpawnCount > 0 && DespawnCount > 0 && MembershipChangeCount > 0
      && BehaviorTransitionCount > 0
      && BusinessLedger.GetPickupCount() > 0
      && BusinessLedger.GetDeliveryCount() > 0
      && CombatQuantity > 0
      && DuplicateCommitCount == BusinessLedger.GetAppliedCommitCount()
      && (SeenBehaviorBits & RequiredBehaviors) == RequiredBehaviors
      && MinimumSeparationCm >= MinimumSafeSeparationCm - 0.5f
      && ProjectileExpectedCount
        == Config.PopulationLimit / 5
      && ProjectileSpawnedCount == ProjectileExpectedCount
      && ProjectileImpactCount == ProjectileExpectedCount
      && ProjectileDamageCount == ProjectileExpectedCount
      && ProjectileDuplicateCount == 0
      && ProjectileTraceHash != 14695981039346656037ull
      && Percentile95(ServerStepMilliseconds)
        <= MixedMaximumFixedStepP95Ms
      && StaleRejectCount == 0;
    if (bPassed)
    {
      bServerPassLogged = true;
      UE_LOG(LogTemp, Display,
        TEXT("PASS CrowdDemoMixedSandbox role=server fixed_step=%lld active=%d transitions=%d pickups=%d deliveries=%d combat_quantity=%d commits=%d duplicate_commits=%d spawned=%d despawned=%d membership=%d max_population=%d safety_holds=%d min_separation_cm=%.2f fixed_step_ms_p95=%.3f projectile_expected=%d projectile_spawned=%d projectile_impacted=%d projectile_damage=%d projectile_duplicate=%d projectile_hash=%llu entity_hash=%llu membership_hash=%llu topology_hash=%llu commit_hash=%llu source=MassCrowdBoundaryRunner+MassCrowdNavRuntime+ApplyFrame"),
        FixedStepIndex, LifecycleWorld.GetActiveEntityCount(), BehaviorTransitionCount,
        BusinessLedger.GetPickupCount(), BusinessLedger.GetDeliveryCount(), CombatQuantity,
        BusinessLedger.GetAppliedCommitCount(), DuplicateCommitCount,
        SpawnCount, DespawnCount, MembershipChangeCount, MaxObservedPopulation,
        SafetyHoldCount, MinimumSeparationCm, Percentile95(ServerStepMilliseconds),
        ProjectileExpectedCount,
        ProjectileSpawnedCount,
        ProjectileImpactCount,
        ProjectileDamageCount,
        ProjectileDuplicateCount,
        ProjectileTraceHash,
        LifecycleWorld.CalculateEntitySetHash(), LifecycleWorld.CalculateMembershipHash(),
        Config.NavTopologyHash,
        LastBoundaryCommitHash);
    }
  }

  if (!HasAuthority() && !bClientPassLogged && LastReceivedFixedStep >= 600
    && bClientVisualsInitialized && !bVisualSyncPending)
  {
    UWorld* World = GetWorld();
    ACrowdDemoReplicator* Replicator = World ? FindMixedVisualHost(*World) : nullptr;
    const int32 Visible = Replicator ? Replicator->GetCrowdVisualInstanceCount() : 0;
    const bool bPassed = Visible == LifecycleWorld.GetActiveEntityCount()
      && LifecycleWorld.GetActiveEntityCount() == Config.PopulationLimit
      && LifecycleWorld.CalculateEntitySetHash() == LastExpectedEntitySetHash
      && LifecycleWorld.CalculateMembershipHash() == LastExpectedMembershipHash
      && ProjectileExpectedCount
        == Config.PopulationLimit / 5
      && ProjectileSpawnedCount == ProjectileExpectedCount
      && ProjectileImpactCount == ProjectileExpectedCount
      && ProjectileDamageCount == ProjectileExpectedCount
      && ProjectileDuplicateCount == 0
      && ProjectileTraceHash != 14695981039346656037ull
      && StaleRejectCount == 0;
    if (bPassed)
    {
      bClientPassLogged = true;
      UE_LOG(LogTemp, Display,
        TEXT("PASS CrowdDemoMixedSandbox role=client fixed_step=%lld active=%d visible=%d state_sequence=%llu client_frame_ms_p95=%.3f projectile_expected=%d projectile_spawned=%d projectile_impacted=%d projectile_damage=%d projectile_duplicate=%d projectile_hash=%llu entity_hash=%llu membership_hash=%llu topology_hash=%llu source=LifecycleBehaviorSurfaceFlow"),
        LastReceivedFixedStep, LifecycleWorld.GetActiveEntityCount(), Visible,
        LastReceivedStateSequence, Percentile95(ClientFrameMilliseconds),
        ProjectileExpectedCount,
        ProjectileSpawnedCount,
        ProjectileImpactCount,
        ProjectileDamageCount,
        ProjectileDuplicateCount,
        ProjectileTraceHash,
        LifecycleWorld.CalculateEntitySetHash(), LifecycleWorld.CalculateMembershipHash(),
        Config.NavTopologyHash);
      if (bCaptureRequested && !bCaptureCompleted && World)
      {
        for (TActorIterator<ACameraActor> It(World); It; ++It)
        {
          if (It->ActorHasTag(TEXT("CrowdNavAcceptanceCamera")))
          {
            if (APlayerController* Controller = World->GetFirstPlayerController())
              Controller->SetViewTarget(*It);
            break;
          }
        }
        CaptureAtWorldSeconds = World->GetTimeSeconds() + 1.0;
      }
    }
  }
}

FVector ACrowdDemoMixedSandboxCoordinator::Marker(
  const FName Tag,
  const FVector& Fallback) const
{
  const FVector* Found = MarkerLocations.Find(Tag);
  return Found ? *Found : Fallback;
}

uint32 ACrowdDemoMixedSandboxCoordinator::MembershipForDiagnosticLabel(
  const ECrowdActiveBehavior Behavior) const
{
  switch (Behavior)
  {
  case ECrowdActiveBehavior::HaulPickup: return 1;
  case ECrowdActiveBehavior::HaulDeliver: return 2;
  case ECrowdActiveBehavior::Pursue: return 3;
  case ECrowdActiveBehavior::Attack: return 4;
  case ECrowdActiveBehavior::Guard: return 5;
  case ECrowdActiveBehavior::Flee: return 6;
  case ECrowdActiveBehavior::Wander: return 7;
  case ECrowdActiveBehavior::MoveTo: return 8;
  default: return 9;
  }
}

double ACrowdDemoMixedSandboxCoordinator::Percentile95(TArray<double> Values)
{
  if (Values.IsEmpty()) return 0.0;
  Values.Sort();
  const int32 Index = FMath::Clamp(
    FMath::CeilToInt(Values.Num() * 0.95) - 1, 0, Values.Num() - 1);
  return Values[Index];
}
