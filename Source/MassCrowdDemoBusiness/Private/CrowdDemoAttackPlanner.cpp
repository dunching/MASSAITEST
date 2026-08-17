#include "CrowdDemoAttackPlanner.h"

namespace CrowdDemoAttackPlannerPrivate
{
  int32 QuantizeScalar(const float Value, const float Quantum)
  {
    return FMath::RoundToInt(
      Value / FMath::Max(Quantum, SMALL_NUMBER));
  }

  FVector QuantizeVector(
    const FVector& Value, const float Quantum)
  {
    return FVector(
      QuantizeScalar(Value.X, Quantum),
      QuantizeScalar(Value.Y, Quantum),
      QuantizeScalar(Value.Z, Quantum)) * Quantum;
  }

  void Fold(uint64& Hash, const uint64 Value)
  {
    constexpr uint64 Prime = 1099511628211ull;
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= Prime;
    }
  }

  uint64 MakeImpactId(
    const int32 RoundId,
    const FCrowdStableEntityRef& Instigator,
    const uint32 FireSequence,
    const ECrowdDemoAttackArchetype Archetype)
  {
    uint64 Hash = 14695981039346656037ull;
    Fold(Hash, static_cast<uint32>(RoundId));
    Fold(Hash, Instigator.ProviderId);
    Fold(Hash, Instigator.StableEntityId);
    Fold(Hash, Instigator.LifecycleSerial);
    Fold(Hash, FireSequence);
    Fold(Hash, static_cast<uint8>(Archetype));
    return Hash == 0 ? 1 : Hash;
  }

  const FCrowdDemoAttackProfileV1* FindProfile(
    const TMap<uint32, const FCrowdDemoAttackProfileV1*>& Profiles,
    const uint32 ProfileId)
  {
    const FCrowdDemoAttackProfileV1* const* Found =
      Profiles.Find(ProfileId);
    return Found ? *Found : nullptr;
  }

  const FCrowdDemoAttackAgent* FindAgent(
    const TArray<FCrowdDemoAttackAgent>& Agents,
    const FCrowdStableEntityRef& Ref)
  {
    if (!Ref.IsValid()) return nullptr;
    return Agents.FindByPredicate(
      [&Ref](const FCrowdDemoAttackAgent& Agent)
      {
        return Agent.EntityRef == Ref;
      });
  }

  const FCrowdDemoAttackAgent* SelectTarget(
    const TArray<FCrowdDemoAttackAgent>& Agents,
    const FCrowdDemoAttackAgent& Source)
  {
    const FCrowdDemoAttackAgent* Preferred =
      FindAgent(Agents, Source.PreferredTargetRef);
    if (Preferred && Preferred->bAlive && Preferred->Health > 0
      && (Source.FactionId == 0 || Preferred->FactionId == 0
        || Preferred->FactionId != Source.FactionId))
      return Preferred;
    if (Source.bRequirePreferredTarget)
      return nullptr;

    const FCrowdDemoAttackAgent* Best = nullptr;
    int64 BestDistanceQ = MAX_int64;
    for (const FCrowdDemoAttackAgent& Candidate : Agents)
    {
      if (!Candidate.bAlive || Candidate.Health <= 0
        || (Source.FactionId != 0 && Candidate.FactionId != 0
          && Candidate.FactionId == Source.FactionId)
        || Candidate.EntityRef == Source.EntityRef)
        continue;
      const int64 DistanceQ = FMath::RoundToInt64(
        FVector::DistSquared(Source.Position, Candidate.Position));
      if (!Best || DistanceQ < BestDistanceQ
        || (DistanceQ == BestDistanceQ
          && Candidate.EntityRef < Best->EntityRef))
      {
        Best = &Candidate;
        BestDistanceQ = DistanceQ;
      }
    }
    return Best;
  }

  void ResetToAcquire(
    FCrowdDemoAttackState& State,
    const int64 FixedStepIndex)
  {
    State.Phase = ECrowdDemoAttackPlannerPhase::AcquireTarget;
    State.PhaseEnterFixedStep = FixedStepIndex;
    State.TargetRef = {};
    State.LockedTargetRef = {};
    State.LockedTargetLocation = FVector::ZeroVector;
    State.bCommitIssued = false;
    ++State.Revision;
  }
}

using namespace CrowdDemoAttackPlannerPrivate;

bool FCrowdDemoAttackProfileV1::IsValid() const
{
  return ProfileId != 0 && PayloadTypeId != 0
    && EffectProfileId != 0
    && WindupFixedSteps > 0
    && RecoveryFixedSteps >= 0
    && CooldownFixedSteps >= 0
    && FMath::IsFinite(MinimumDistanceCm)
    && FMath::IsFinite(MaximumDistanceCm)
    && FMath::IsFinite(QueryRadiusCm)
    && FMath::IsFinite(MuzzleForwardOffsetCm)
    && FMath::IsFinite(ProjectileSpeedCmps)
    && MinimumDistanceCm >= 0.0f
    && MaximumDistanceCm >= MinimumDistanceCm
    && QueryRadiusCm > 0.0f
    && MuzzleForwardOffsetCm >= 0.0f
    && (Archetype != ECrowdDemoAttackArchetype::Ranged
      || ProjectileSpeedCmps > 0.0f)
    && PositionQuantumCm > 0.0f
    && VelocityQuantumCmps > 0.0f
    && Damage > 0;
}

bool FCrowdDemoAttackState::IsValid() const
{
  return PhaseEnterFixedStep >= 0
    && CooldownEndFixedStep >= 0
    && (TargetRef.IsUnset() || TargetRef.IsValid())
    && (LockedTargetRef.IsUnset() || LockedTargetRef.IsValid())
    && !LockedTargetLocation.ContainsNaN();
}

bool FCrowdDemoAttackAgent::IsValid() const
{
  return EntityRef.IsValid()
    && (PreferredTargetRef.IsUnset()
      || PreferredTargetRef.IsValid())
    && AttackProfileId != 0
    && !Position.ContainsNaN()
    && !Velocity.ContainsNaN()
    && !Facing.ContainsNaN()
    && Health >= 0
    && State.IsValid();
}

bool FCrowdDemoAttackIntent::IsValid() const
{
  return ImpactId != 0
    && FixedStepIndex >= 0
    && Instigator.IsValid()
    && Target.IsValid()
    && AttackProfileId != 0
    && PayloadTypeId != 0
    && EffectProfileId != 0
    && FireSequence > 0
    && !Position.ContainsNaN()
    && !Direction.ContainsNaN()
    && !Direction.IsNearlyZero()
    && !TargetStartPosition.ContainsNaN()
    && !TargetEndPosition.ContainsNaN()
    && RangeCm > 0.0f
    && QueryRadiusCm > 0.0f
    && (Archetype != ECrowdDemoAttackArchetype::Ranged
      || ProjectileSpeedCmps > 0.0f)
    && Damage > 0;
}

bool FCrowdDemoAttackPlanner::Advance(
  const int32 RoundId,
  const int64 FixedStepIndex,
  const TConstArrayView<FCrowdDemoAttackProfileV1> Profiles,
  TArray<FCrowdDemoAttackAgent>& InOutAgents,
  TArray<FCrowdDemoAttackIntent>& OutIntents,
  FCrowdDemoAttackPlanSummary& OutSummary)
{
  OutIntents.Reset();
  OutSummary = {};
  if (RoundId <= 0 || FixedStepIndex < 0)
    return false;

  TArray<FCrowdDemoAttackProfileV1> SortedProfiles(Profiles);
  SortedProfiles.Sort([](const auto& A, const auto& B)
  {
    return A.ProfileId < B.ProfileId;
  });
  TMap<uint32, const FCrowdDemoAttackProfileV1*> ProfilesById;
  for (const FCrowdDemoAttackProfileV1& Profile : SortedProfiles)
  {
    if (!Profile.IsValid()
      || ProfilesById.Contains(Profile.ProfileId))
      return false;
    ProfilesById.Add(Profile.ProfileId, &Profile);
  }

  InOutAgents.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  for (int32 Index = 0; Index < InOutAgents.Num(); ++Index)
  {
    if (!InOutAgents[Index].IsValid()
      || !ProfilesById.Contains(
        InOutAgents[Index].AttackProfileId)
      || (Index > 0
        && InOutAgents[Index - 1].EntityRef
          == InOutAgents[Index].EntityRef))
      return false;
  }

  for (FCrowdDemoAttackAgent& Source : InOutAgents)
  {
    if (!Source.bAlive || Source.Health <= 0
      || !Source.bCanAttack)
    {
      if (Source.State.TargetRef.IsValid()
        || Source.State.LockedTargetRef.IsValid())
        ++OutSummary.InvalidTargetLifecycleCount;
      ResetToAcquire(Source.State, FixedStepIndex);
      continue;
    }
    const FCrowdDemoAttackProfileV1* Profile =
      FindProfile(ProfilesById, Source.AttackProfileId);
    check(Profile);
    const FCrowdDemoAttackAgent* Target =
      SelectTarget(InOutAgents, Source);
    if (!Target)
    {
      if (Source.State.TargetRef.IsValid()
        || Source.State.LockedTargetRef.IsValid())
        ++OutSummary.InvalidTargetLifecycleCount;
      ResetToAcquire(Source.State, FixedStepIndex);
      continue;
    }

    if (Source.State.Phase == ECrowdDemoAttackPlannerPhase::None
      || Source.State.Phase
        == ECrowdDemoAttackPlannerPhase::AcquireTarget)
    {
      const float Distance =
        FVector::Distance(Source.Position, Target->Position);
      if (Distance < Profile->MinimumDistanceCm
        || Distance > Profile->MaximumDistanceCm)
      {
        ++OutSummary.OutOfRangeCount;
        Source.State.TargetRef = Target->EntityRef;
        Source.State.LockedTargetRef = {};
        continue;
      }
      Source.State.TargetRef = Target->EntityRef;
      Source.State.LockedTargetRef = Target->EntityRef;
      Source.State.LockedTargetLocation = Target->Position;
      Source.State.Phase = ECrowdDemoAttackPlannerPhase::Windup;
      Source.State.PhaseEnterFixedStep = FixedStepIndex;
      Source.State.bCommitIssued = false;
      ++Source.State.Revision;
      ++OutSummary.TargetAcquiredCount;
      continue;
    }

    const FCrowdDemoAttackAgent* LockedTarget =
      FindAgent(InOutAgents, Source.State.LockedTargetRef);
    if (!LockedTarget || !LockedTarget->bAlive
      || LockedTarget->Health <= 0
      || (Source.FactionId != 0 && LockedTarget->FactionId != 0
        && LockedTarget->FactionId == Source.FactionId))
    {
      ResetToAcquire(Source.State, FixedStepIndex);
      ++OutSummary.InvalidTargetLifecycleCount;
      continue;
    }

    switch (Source.State.Phase)
    {
    case ECrowdDemoAttackPlannerPhase::Windup:
      if (!Source.State.bCommitIssued
        && FixedStepIndex - Source.State.PhaseEnterFixedStep
          >= Profile->WindupFixedSteps)
      {
        const FVector Direction =
          (LockedTarget->Position - Source.Position)
          .GetSafeNormal();
        if (Direction.IsNearlyZero())
          return false;
        ++Source.State.FireSequence;
        Source.State.bCommitIssued = true;
        Source.State.Phase = ECrowdDemoAttackPlannerPhase::Commit;
        Source.State.PhaseEnterFixedStep = FixedStepIndex;
        ++Source.State.Revision;

        FCrowdDemoAttackIntent& Intent =
          OutIntents.AddDefaulted_GetRef();
        Intent.ImpactId = MakeImpactId(
          RoundId, Source.EntityRef,
          Source.State.FireSequence, Profile->Archetype);
        Intent.FixedStepIndex = FixedStepIndex;
        Intent.Instigator = Source.EntityRef;
        Intent.Target = LockedTarget->EntityRef;
        Intent.AttackProfileId = Profile->ProfileId;
        Intent.PayloadTypeId = Profile->PayloadTypeId;
        Intent.EffectProfileId = Profile->EffectProfileId;
        Intent.Archetype = Profile->Archetype;
        Intent.FireSequence = Source.State.FireSequence;
        Intent.SourceFactionId = Source.FactionId;
        Intent.NavLayer = Source.NavLayer;
        Intent.Position = QuantizeVector(
          Source.Position
            + Direction * Profile->MuzzleForwardOffsetCm,
          Profile->PositionQuantumCm);
        Intent.Direction = Direction;
        Intent.TargetStartPosition = QuantizeVector(
          LockedTarget->Position - LockedTarget->Velocity,
          Profile->PositionQuantumCm);
        Intent.TargetEndPosition = QuantizeVector(
          LockedTarget->Position,
          Profile->PositionQuantumCm);
        Intent.RangeCm = Profile->MaximumDistanceCm;
        Intent.QueryRadiusCm = Profile->QueryRadiusCm;
        Intent.ProjectileSpeedCmps =
          Profile->ProjectileSpeedCmps;
        Intent.Damage = Profile->Damage;
        if (!Intent.IsValid())
          return false;
        ++OutSummary.CompletedWindupCount;
      }
      break;
    case ECrowdDemoAttackPlannerPhase::Commit:
      Source.State.Phase = ECrowdDemoAttackPlannerPhase::Recovery;
      Source.State.PhaseEnterFixedStep = FixedStepIndex;
      ++Source.State.Revision;
      break;
    case ECrowdDemoAttackPlannerPhase::Recovery:
      if (FixedStepIndex - Source.State.PhaseEnterFixedStep
        >= Profile->RecoveryFixedSteps)
      {
        Source.State.Phase = ECrowdDemoAttackPlannerPhase::Cooldown;
        Source.State.PhaseEnterFixedStep = FixedStepIndex;
        Source.State.CooldownEndFixedStep =
          FixedStepIndex + Profile->CooldownFixedSteps;
        ++Source.State.Revision;
      }
      break;
    case ECrowdDemoAttackPlannerPhase::Cooldown:
      if (FixedStepIndex >= Source.State.CooldownEndFixedStep)
        ResetToAcquire(Source.State, FixedStepIndex);
      break;
    default:
      break;
    }
  }

  OutIntents.Sort([](const auto& A, const auto& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex)
      return A.FixedStepIndex < B.FixedStepIndex;
    if (A.Instigator != B.Instigator)
      return A.Instigator < B.Instigator;
    return A.FireSequence < B.FireSequence;
  });
  OutSummary.bValid = true;
  return true;
}
