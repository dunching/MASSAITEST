#include "Mass/CrowdDemoCombatStateKernel.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;

  uint32 Fold(uint32 Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>((Value >> (Byte * 8)) & 0xffu);
      Hash *= FnvPrime;
    }
    return Hash;
  }

  int32 Quantize(const float Value, const float Scale = 1.0f)
  {
    return FMath::RoundToInt(Value * Scale);
  }

  bool IsFiniteFact(const FCrowdDemoHitFact& Fact)
  {
    const auto IsFiniteVector = [](const FVector& Value)
    {
      return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
    };

    return IsFiniteVector(Fact.HitPosition) && IsFiniteVector(Fact.HitDirection)
      && FMath::IsFinite(Fact.Damage)
      && FMath::IsFinite(Fact.HorizontalImpulseCmps)
      && FMath::IsFinite(Fact.VerticalImpulseCmps)
      && Fact.Damage >= 0.0f && Fact.HorizontalImpulseCmps >= 0.0f;
  }

  uint32 HashHit(uint32 Hash, const FCrowdDemoHitFact& Fact)
  {
    Hash = Fold(Hash, Fact.HitEventId);
    Hash = Fold(Hash, static_cast<uint64>(Fact.ApplyFixedStep));
    Hash = Fold(Hash, static_cast<uint64>(Fact.SourceAgentId));
    Hash = Fold(Hash, static_cast<uint64>(Fact.SourceLifecycleSerial));
    Hash = Fold(Hash, static_cast<uint64>(Fact.TargetAgentId));
    Hash = Fold(Hash, static_cast<uint64>(Fact.TargetLifecycleSerial));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Fact.HitPosition.X)));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Fact.HitPosition.Y)));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Fact.HitPosition.Z)));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Fact.HitDirection.X, 32767.0f)));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Fact.HitDirection.Y, 32767.0f)));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Fact.HitDirection.Z, 32767.0f)));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Fact.Damage, 100.0f)));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Fact.HorizontalImpulseCmps)));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Fact.VerticalImpulseCmps)));
    return Fold(Hash, Fact.HitFlashProfileKey);
  }

  bool SameFact(const FCrowdDemoHitFact& A, const FCrowdDemoHitFact& B)
  {
    return HashHit(FnvOffset, A) == HashHit(FnvOffset, B);
  }

  FVector StableHorizontalDirection(const FCrowdDemoHitFact& Fact)
  {
    FVector Direction(Fact.HitDirection.X, Fact.HitDirection.Y, 0.0f);
    if (!Direction.Normalize())
    {
      const uint32 Axis = static_cast<uint32>(Fact.SourceAgentId)
        ^ static_cast<uint32>(Fact.TargetAgentId * 0x9e3779b9u);
      Direction = (Axis & 1u) == 0u ? FVector::ForwardVector : FVector::RightVector;
    }
    return Direction;
  }

  uint32 HashAgent(uint32 Hash, const FCrowdDemoCombatAgentState& Agent)
  {
    Hash = Fold(Hash, static_cast<uint64>(Agent.AgentId));
    Hash = Fold(Hash, static_cast<uint64>(Agent.LifecycleSerial));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Agent.Health, 100.0f)));
    Hash = Fold(Hash, static_cast<uint8>(Agent.LifecycleState));
    Hash = Fold(Hash, Agent.bAlive ? 1u : 0u);
    Hash = Fold(Hash, static_cast<uint8>(Agent.BusinessState));
    Hash = Fold(Hash, static_cast<uint64>(Agent.BusinessStateRevision));
    Hash = Fold(Hash, static_cast<uint8>(Agent.AttackPhase));
    Hash = Fold(Hash, static_cast<uint64>(Agent.FireSequence));
    Hash = Fold(Hash, static_cast<uint8>(Agent.ReactiveMode));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Agent.HorizontalReactiveVelocity.X)));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Agent.HorizontalReactiveVelocity.Y)));
    Hash = Fold(Hash, static_cast<uint64>(Quantize(Agent.VerticalReactiveVelocityCmps)));
    Hash = Fold(Hash, static_cast<uint64>(Agent.ReactiveRevision));
    Hash = Fold(Hash, static_cast<uint64>(Agent.ApexCount));
    Hash = Fold(Hash, static_cast<uint64>(Agent.LandingCount));
    Hash = Fold(Hash, static_cast<uint64>(Agent.HitFlashRevision));
    Hash = Fold(Hash, Agent.HitFlashProfileKey);
    Hash = Fold(Hash, Agent.LastConsumedHitEventId);
    Hash = Fold(Hash, static_cast<uint8>(Agent.VisualState));
    Hash = Fold(Hash, static_cast<uint64>(Agent.VisualRevision));
    return Hash;
  }
}

void FCrowdDemoCombatStateKernel::ResolveHitFacts(
  const int32 FixedStepIndex,
  const float ServerTimeSeconds,
  const TConstArrayView<FCrowdDemoHitFact> HitFacts,
  const FCrowdDemoHitResponseSettings& Settings,
  TArray<FCrowdDemoCombatAgentState>& InOutAgents,
  FCrowdDemoHitResponseSummary& OutSummary)
{
  OutSummary = {};
  OutSummary.InputHitCount = HitFacts.Num();
  if (FixedStepIndex < 0 || !FMath::IsFinite(ServerTimeSeconds)
    || !FMath::IsFinite(Settings.MaximumHorizontalImpulseCmps)
    || !FMath::IsFinite(Settings.MaximumVerticalImpulseCmps)
    || Settings.MaximumHorizontalImpulseCmps < 0.0f
    || Settings.MaximumVerticalImpulseCmps < 0.0f
    || Settings.ReactiveDurationFixedSteps < 0
    || Settings.LandingRecoveryFixedSteps < 0)
  {
    return;
  }

  InOutAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  TMap<int32, int32> AgentIndexById;
  for (int32 Index = 0; Index < InOutAgents.Num(); ++Index)
  {
    if (InOutAgents[Index].AgentId < 0 || AgentIndexById.Contains(InOutAgents[Index].AgentId))
      return;
    AgentIndexById.Add(InOutAgents[Index].AgentId, Index);
  }

  TArray<FCrowdDemoHitFact> Sorted(HitFacts);
  Sorted.Sort([](const auto& A, const auto& B)
  {
    if (A.ApplyFixedStep != B.ApplyFixedStep) return A.ApplyFixedStep < B.ApplyFixedStep;
    if (A.TargetAgentId != B.TargetAgentId) return A.TargetAgentId < B.TargetAgentId;
    return A.HitEventId < B.HitEventId;
  });

  TSet<uint64> SeenEvents;
  TMap<uint64, FCrowdDemoHitFact> FirstFactByEvent;
  TSet<int32> DamageAgents;
  TSet<int32> ReactiveAgents;
  uint32 Hash = Fold(FnvOffset, 1u);
  for (const FCrowdDemoHitFact& Fact : Sorted)
  {
    Hash = HashHit(Hash, Fact);
    if (!IsFiniteFact(Fact) || Fact.HitEventId == 0 || Fact.ApplyFixedStep < 0)
      return;
    if (const FCrowdDemoHitFact* First = FirstFactByEvent.Find(Fact.HitEventId))
    {
      if (!SameFact(*First, Fact))
        return;
      ++OutSummary.DuplicateHitCount;
      continue;
    }
    FirstFactByEvent.Add(Fact.HitEventId, Fact);
    SeenEvents.Add(Fact.HitEventId);
    if (Fact.ApplyFixedStep != FixedStepIndex)
      continue;
    const int32* AgentIndex = AgentIndexById.Find(Fact.TargetAgentId);
    if (!AgentIndex)
    {
      ++OutSummary.MissingTargetCount;
      continue;
    }
    FCrowdDemoCombatAgentState& Agent = InOutAgents[*AgentIndex];
    if (Agent.LifecycleSerial != Fact.TargetLifecycleSerial)
    {
      ++OutSummary.StaleLifecycleCount;
      continue;
    }
    if (!Agent.bAlive || Agent.LifecycleState == ECrowdDemoLifecycleState::Dead
      || Agent.Health <= 0.0f)
    {
      ++OutSummary.AlreadyDeadCount;
      continue;
    }
    if (Fact.HitEventId <= Agent.LastConsumedHitEventId)
    {
      ++OutSummary.DuplicateHitCount;
      continue;
    }

    Agent.LastConsumedHitEventId = Fact.HitEventId;
    Agent.Health = FMath::Clamp(Agent.Health - Fact.Damage, 0.0f, Agent.MaxHealth);
    ++OutSummary.AppliedHitCount;
    DamageAgents.Add(Agent.AgentId);
    Agent.HitFlashRevision++;
    Agent.HitFlashStartServerTimeSeconds = ServerTimeSeconds;
    Agent.HitFlashDurationSeconds = Settings.HitFlashDurationSeconds;
    Agent.HitFlashProfileKey = Fact.HitFlashProfileKey;
    Agent.HitFlashPeakIntensity = Settings.HitFlashPeakIntensity;

    if (Agent.Health <= 0.0f)
    {
      Agent.bAlive = false;
      Agent.LifecycleState = ECrowdDemoLifecycleState::Dead;
      Agent.BusinessState = ECrowdDemoBusinessState::Dead;
      Agent.BusinessStateRevision++;
      Agent.BusinessStateEnterFixedStep = FixedStepIndex;
      Agent.AttackPhase = ECrowdDemoAttackPhase::None;
      Agent.ReactiveMode = ECrowdDemoReactiveMotionMode::None;
      Agent.HorizontalReactiveVelocity = FVector::ZeroVector;
      Agent.VerticalReactiveVelocityCmps = 0.0f;
      ++OutSummary.DeathCount;
      continue;
    }

    if (Agent.ReactiveMode == ECrowdDemoReactiveMotionMode::None
      || Agent.ReactiveMode == ECrowdDemoReactiveMotionMode::LandingRecovery)
    {
      Agent.RestoreBusinessState = Agent.BusinessState;
    }
    const FVector HorizontalDelta = StableHorizontalDirection(Fact)
      * Fact.HorizontalImpulseCmps;
    Agent.HorizontalReactiveVelocity += HorizontalDelta;
    Agent.HorizontalReactiveVelocity.Z = 0.0f;
    Agent.HorizontalReactiveVelocity = Agent.HorizontalReactiveVelocity.GetClampedToMaxSize(
      Settings.MaximumHorizontalImpulseCmps);
    Agent.VerticalReactiveVelocityCmps = FMath::Clamp(
      Agent.VerticalReactiveVelocityCmps + Fact.VerticalImpulseCmps,
      -Settings.MaximumVerticalImpulseCmps,
      Settings.MaximumVerticalImpulseCmps);
    Agent.ReactiveMode = Fact.VerticalImpulseCmps > 0.0f
      ? ECrowdDemoReactiveMotionMode::KnockUp
      : ECrowdDemoReactiveMotionMode::Knockback;
    Agent.ReactiveStartFixedStep = FixedStepIndex;
    Agent.ReactiveEndFixedStep = FixedStepIndex + Settings.ReactiveDurationFixedSteps;
    Agent.ReactiveRevision++;
    Agent.BusinessState = ECrowdDemoBusinessState::HitReact;
    Agent.BusinessStateRevision++;
    Agent.BusinessStateEnterFixedStep = FixedStepIndex;
    ReactiveAgents.Add(Agent.AgentId);
  }

  OutSummary.DamageAppliedAgentCount = DamageAgents.Num();
  OutSummary.ReactiveAgentCount = ReactiveAgents.Num();
  OutSummary.StableHash = Fold(Hash, HashAgents(InOutAgents));
  OutSummary.bValid = true;
}

FCrowdDemoReactiveMotionStepResult FCrowdDemoCombatStateKernel::AdvanceReactiveMotion(
  const int32 FixedStepIndex,
  const float CurrentZ,
  const FCrowdDemoHitResponseSettings& Settings,
  FCrowdDemoCombatAgentState& InOutAgent)
{
  FCrowdDemoReactiveMotionStepResult Result;
  Result.NewZ = CurrentZ;
  Result.HorizontalVelocity = InOutAgent.HorizontalReactiveVelocity;
  Result.NewVerticalVelocityCmps = InOutAgent.VerticalReactiveVelocityCmps;
  if (FixedStepIndex < 0 || !FMath::IsFinite(CurrentZ)
    || Settings.FixedStepSeconds <= 0.0f || !FMath::IsFinite(Settings.FixedStepSeconds)
    || !FMath::IsFinite(Settings.GravityCmps2) || !FMath::IsFinite(Settings.GroundZ))
  {
    return Result;
  }

  if (InOutAgent.ReactiveMode == ECrowdDemoReactiveMotionMode::KnockUp)
  {
    const float PreviousVz = InOutAgent.VerticalReactiveVelocityCmps;
    const float NewVz = PreviousVz + Settings.GravityCmps2 * Settings.FixedStepSeconds;
    float NewZ = CurrentZ + NewVz * Settings.FixedStepSeconds;
    if (PreviousVz > 0.0f && NewVz <= 0.0f)
    {
      Result.bReachedApex = true;
      ++InOutAgent.ApexCount;
    }
    if (NewZ <= Settings.GroundZ && NewVz <= 0.0f)
    {
      NewZ = Settings.GroundZ;
      InOutAgent.VerticalReactiveVelocityCmps = 0.0f;
      InOutAgent.ReactiveMode = ECrowdDemoReactiveMotionMode::LandingRecovery;
      InOutAgent.ReactiveStartFixedStep = FixedStepIndex;
      InOutAgent.ReactiveEndFixedStep = FixedStepIndex + Settings.LandingRecoveryFixedSteps;
      Result.bLanded = true;
      ++InOutAgent.LandingCount;
    }
    else
    {
      InOutAgent.VerticalReactiveVelocityCmps = NewVz;
    }
    Result.NewZ = NewZ;
  }
  else if (InOutAgent.ReactiveMode == ECrowdDemoReactiveMotionMode::Knockback
    && FixedStepIndex >= InOutAgent.ReactiveEndFixedStep)
  {
    InOutAgent.ReactiveMode = ECrowdDemoReactiveMotionMode::None;
    InOutAgent.HorizontalReactiveVelocity = FVector::ZeroVector;
    InOutAgent.BusinessState = InOutAgent.RestoreBusinessState;
    InOutAgent.BusinessStateRevision++;
    InOutAgent.BusinessStateEnterFixedStep = FixedStepIndex;
    Result.bRecovered = true;
  }
  else if (InOutAgent.ReactiveMode == ECrowdDemoReactiveMotionMode::LandingRecovery
    && FixedStepIndex >= InOutAgent.ReactiveEndFixedStep)
  {
    InOutAgent.ReactiveMode = ECrowdDemoReactiveMotionMode::None;
    InOutAgent.HorizontalReactiveVelocity = FVector::ZeroVector;
    InOutAgent.BusinessState = InOutAgent.RestoreBusinessState;
    InOutAgent.BusinessStateRevision++;
    InOutAgent.BusinessStateEnterFixedStep = FixedStepIndex;
    Result.bRecovered = true;
  }

  Result.HorizontalVelocity = InOutAgent.HorizontalReactiveVelocity;
  Result.NewVerticalVelocityCmps = InOutAgent.VerticalReactiveVelocityCmps;
  uint32 Hash = Fold(FnvOffset, static_cast<uint64>(InOutAgent.AgentId));
  Hash = Fold(Hash, static_cast<uint8>(InOutAgent.ReactiveMode));
  Hash = Fold(Hash, static_cast<uint64>(Quantize(Result.NewZ)));
  Hash = Fold(Hash, static_cast<uint64>(Quantize(Result.NewVerticalVelocityCmps)));
  Hash = Fold(Hash, static_cast<uint64>(InOutAgent.ApexCount));
  Hash = Fold(Hash, static_cast<uint64>(InOutAgent.LandingCount));
  Result.StableHash = Hash;
  Result.bValid = true;
  return Result;
}

FCrowdDemoVatShowcaseMotionResult FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
  const int32 FormationIndex,
  const int32 FixedStepIndex,
  const FVector& CurrentLocation,
  const FVector& AnchorLocation,
  const FCrowdDemoVatShowcaseMotionSettings& Settings)
{
  FCrowdDemoVatShowcaseMotionResult Result;
  Result.DesiredLocation = CurrentLocation;
  const bool bFinite = FMath::IsFinite(CurrentLocation.X)
    && FMath::IsFinite(CurrentLocation.Y)
    && FMath::IsFinite(CurrentLocation.Z)
    && FMath::IsFinite(AnchorLocation.X)
    && FMath::IsFinite(AnchorLocation.Y)
    && FMath::IsFinite(AnchorLocation.Z)
    && FMath::IsFinite(Settings.MoveSpeedCmps)
    && FMath::IsFinite(Settings.MaximumAnchorOffsetCm);
  if (!bFinite || FormationIndex < 0 || FixedStepIndex < 0
    || Settings.FirstMovingFormationIndex < 0 || Settings.MovingAgentCount < 0
    || Settings.HalfCycleFixedSteps <= 0 || Settings.MoveSpeedCmps < 0.0f
    || Settings.MaximumAnchorOffsetCm < 0.0f)
  {
    return Result;
  }

  Result.bMovingGroup = FormationIndex >= Settings.FirstMovingFormationIndex
    && FormationIndex < Settings.FirstMovingFormationIndex + Settings.MovingAgentCount;
  if (Result.bMovingGroup && Settings.MoveSpeedCmps > 0.0f
    && Settings.MaximumAnchorOffsetCm > 0.0f)
  {
    const int32 HalfCycle = FixedStepIndex / Settings.HalfCycleFixedSteps;
    float Direction = (HalfCycle & 1) == 0 ? 1.0f : -1.0f;
    const float OffsetX = CurrentLocation.X - AnchorLocation.X;
    if (OffsetX >= Settings.MaximumAnchorOffsetCm && Direction > 0.0f)
    {
      Direction = -1.0f;
    }
    else if (OffsetX <= -Settings.MaximumAnchorOffsetCm && Direction < 0.0f)
    {
      Direction = 1.0f;
    }
    Result.DesiredVelocity = FVector(Direction * Settings.MoveSpeedCmps, 0.0f, 0.0f);
    Result.DesiredLocation = AnchorLocation
      + FVector(Direction * Settings.MaximumAnchorOffsetCm, 0.0f, 0.0f);
  }

  uint32 Hash = Fold(FnvOffset, 1u);
  Hash = Fold(Hash, static_cast<uint64>(FormationIndex));
  Hash = Fold(Hash, static_cast<uint64>(FixedStepIndex));
  Hash = Fold(Hash, static_cast<uint64>(Quantize(CurrentLocation.X)));
  Hash = Fold(Hash, static_cast<uint64>(Quantize(CurrentLocation.Y)));
  Hash = Fold(Hash, static_cast<uint64>(Quantize(AnchorLocation.X)));
  Hash = Fold(Hash, static_cast<uint64>(Quantize(AnchorLocation.Y)));
  Hash = Fold(Hash, Result.bMovingGroup ? 1u : 0u);
  Hash = Fold(Hash, static_cast<uint64>(Quantize(Result.DesiredVelocity.X)));
  Hash = Fold(Hash, static_cast<uint64>(Quantize(Result.DesiredVelocity.Y)));
  Result.StableHash = Hash;
  Result.bValid = true;
  return Result;
}

ECrowdDemoVisualState FCrowdDemoCombatStateKernel::ResolveVisualState(
  const FCrowdDemoCombatAgentState& Agent,
  const FVector& Velocity,
  const bool bUseExplicitBusinessLocomotionState)
{
  if (!Agent.bAlive || Agent.Health <= 0.0f
    || Agent.LifecycleState == ECrowdDemoLifecycleState::Dead
    || Agent.BusinessState == ECrowdDemoBusinessState::Dead)
  {
    return ECrowdDemoVisualState::Death;
  }
  if (Agent.ReactiveMode != ECrowdDemoReactiveMotionMode::None
    || Agent.BusinessState == ECrowdDemoBusinessState::HitReact)
  {
    return ECrowdDemoVisualState::HitReact;
  }
  if (Agent.AttackPhase == ECrowdDemoAttackPhase::Windup
    || Agent.AttackPhase == ECrowdDemoAttackPhase::Fire
    || Agent.AttackPhase == ECrowdDemoAttackPhase::Recovery
    || Agent.BusinessState == ECrowdDemoBusinessState::Attacking)
  {
    return ECrowdDemoVisualState::Attack;
  }
  if (bUseExplicitBusinessLocomotionState)
  {
    return Agent.BusinessState == ECrowdDemoBusinessState::Moving
      ? ECrowdDemoVisualState::Move
      : ECrowdDemoVisualState::Idle;
  }
  return FVector(Velocity.X, Velocity.Y, 0.0f).SizeSquared() >= 1.0f
    ? ECrowdDemoVisualState::Move
    : ECrowdDemoVisualState::Idle;
}

void FCrowdDemoCombatStateKernel::ResolveVisualStateBoundary(
  const int32 FixedStepIndex,
  const float ServerTimeSeconds,
  const FVector& Velocity,
  FCrowdDemoCombatAgentState& InOutAgent,
  const bool bUseExplicitBusinessLocomotionState)
{
  const ECrowdDemoVisualState NewState = ResolveVisualState(
    InOutAgent, Velocity, bUseExplicitBusinessLocomotionState);
  if (NewState != InOutAgent.VisualState)
  {
    InOutAgent.VisualState = NewState;
    InOutAgent.VisualRevision++;
    InOutAgent.VisualStateStartServerTimeSeconds = ServerTimeSeconds;
    InOutAgent.VisualPhaseSeed = static_cast<uint32>(InOutAgent.AgentId * 2654435761u)
      ^ static_cast<uint32>(FixedStepIndex);
  }
}

uint32 FCrowdDemoCombatStateKernel::HashAgents(
  const TConstArrayView<FCrowdDemoCombatAgentState> Agents)
{
  TArray<FCrowdDemoCombatAgentState> Sorted(Agents);
  Sorted.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  uint32 Hash = Fold(FnvOffset, 1u);
  for (const FCrowdDemoCombatAgentState& Agent : Sorted)
    Hash = HashAgent(Hash, Agent);
  return Hash;
}
