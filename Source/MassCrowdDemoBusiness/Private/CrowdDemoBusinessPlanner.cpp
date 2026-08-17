#include "CrowdDemoBusinessPlanner.h"

#include "Async/ParallelFor.h"

#include "CrowdDemoBusinessAdapters.h"

namespace CrowdDemoBusinessPlannerPrivate
{
  constexpr uint64 FnvOffset = 14695981039346656037ull;
  constexpr uint64 FnvPrime = 1099511628211ull;

  template <typename T>
  void FoldIntegral(uint64& Hash, const T Value)
  {
    static_assert(std::is_integral_v<T>);
    using UnsignedType = std::make_unsigned_t<T>;
    const UnsignedType Unsigned = static_cast<UnsignedType>(Value);
    for (uint32 Byte = 0; Byte < sizeof(UnsignedType); ++Byte)
    {
      Hash ^= static_cast<uint8>(Unsigned >> (Byte * 8));
      Hash *= FnvPrime;
    }
  }

  void FoldRef(uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    FoldIntegral(Hash, Ref.ProviderId);
    FoldIntegral(Hash, Ref.StableEntityId);
    FoldIntegral(Hash, Ref.LifecycleSerial);
  }

  void FoldVector(uint64& Hash, const FVector& Vector)
  {
    FoldIntegral(Hash, FMath::RoundToInt64(Vector.X * 10.0));
    FoldIntegral(Hash, FMath::RoundToInt64(Vector.Y * 10.0));
    FoldIntegral(Hash, FMath::RoundToInt64(Vector.Z * 10.0));
  }

  const FCrowdDemoObjectiveFact* FindObjective(
    const FCrowdDemoPlanningSnapshot& Snapshot,
    const FCrowdStableEntityRef& EntityRef,
    const FCrowdDemoObjectiveId ObjectiveId)
  {
    return Snapshot.Objectives.FindByPredicate(
      [&](const FCrowdDemoObjectiveFact& Fact)
      {
        return Fact.EntityRef == EntityRef
          && Fact.ObjectiveId == ObjectiveId;
      });
  }

  TArray<const FCrowdDemoPlannerAgentFact*> FindRoleAgents(
    const FCrowdDemoPlanningSnapshot& Snapshot,
    const uint32 CohortId,
    const FCrowdDemoBusinessPlannerId PlannerId)
  {
    TArray<const FCrowdDemoPlannerAgentFact*> Result;
    for (const FCrowdDemoPlannerAgentFact& Candidate : Snapshot.Agents)
    {
      if (Candidate.bActive
        && Candidate.Assignment.CohortId == CohortId
        && Candidate.Assignment.PlannerId == PlannerId)
        Result.Add(&Candidate);
    }
    Result.Sort([](const auto& A, const auto& B)
    {
      if (A.Assignment.Ordinal != B.Assignment.Ordinal)
        return A.Assignment.Ordinal < B.Assignment.Ordinal;
      return A.EntityRef < B.EntityRef;
    });
    return Result;
  }

  const FCrowdDemoPlannerAgentFact* SelectPeer(
    const FCrowdDemoPlanningSnapshot& Snapshot,
    const FCrowdDemoPlannerAgentFact& Agent,
    const FCrowdDemoBusinessPlannerId PlannerId)
  {
    const TArray<const FCrowdDemoPlannerAgentFact*> Candidates =
      FindRoleAgents(
        Snapshot, Agent.Assignment.CohortId, PlannerId);
    if (Candidates.IsEmpty()) return nullptr;
    const int32 Start =
      static_cast<int32>(Agent.Assignment.Ordinal)
      % Candidates.Num();
    for (int32 Offset = 0; Offset < Candidates.Num(); ++Offset)
    {
      const FCrowdDemoPlannerAgentFact* Candidate =
        Candidates[(Start + Offset) % Candidates.Num()];
      if (Candidate && Candidate->bActive)
        return Candidate;
    }
    return nullptr;
  }

  const FCrowdDemoPlannerAgentFact* SelectEnemy(
    const FCrowdDemoPlanningSnapshot& Snapshot,
    const FCrowdDemoPlannerAgentFact& Agent)
  {
    const FCrowdDemoPlannerAgentFact* Best = nullptr;
    int64 BestDistanceQ = MAX_int64;
    for (const FCrowdDemoPlannerAgentFact& Candidate
      : Snapshot.Agents)
    {
      if (!Candidate.bActive || Candidate.Health <= 0
        || Candidate.EntityRef == Agent.EntityRef
        || Candidate.FactionId == 0
        || Candidate.FactionId == Agent.FactionId)
        continue;
      const int64 DistanceQ = FMath::RoundToInt64(
        FVector::DistSquared(
          Agent.Position, Candidate.Position));
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

  bool AddFaceMovement(FCrowdDemoPlannerWriter& Writer)
  {
    FCrowdFaceMovementPayload Facing{};
    Facing.MinimumSpeedCmps = 1.0f;
    return Writer.AddStandard(
      CrowdDemoBehaviorControllerIds::Facing, 1,
      CrowdStandardSources::FaceMovement, Facing);
  }

  bool AddFaceEntity(
    FCrowdDemoPlannerWriter& Writer,
    const FCrowdStableEntityRef& TargetRef,
    const uint64 FactRevision)
  {
    FCrowdFaceEntityPayload Facing{};
    Facing.TargetRef = TargetRef;
    return Writer.AddStandard(
        CrowdDemoBehaviorControllerIds::Facing, 1,
        CrowdStandardSources::FaceEntity, Facing)
      && Writer.AddContext({
        ECrowdDemoContextRequestKind::TargetKinematics,
        TargetRef, FVector::ZeroVector, FactRevision});
  }

  bool AddSpeedLimit(
    FCrowdDemoPlannerWriter& Writer,
    const FCrowdDemoPlanningSnapshot& Snapshot)
  {
    FCrowdSpeedLimitPayload Limit{};
    Limit.MaximumSpeedCmps =
      Snapshot.Settings.PopulationLimit >= 500
      ? Snapshot.Settings.ScaleMaximumSpeedCmps
      : Snapshot.Settings.MaximumSpeedCmps;
    Limit.AllowedNavLayerMask = MAX_uint64;
    return Writer.AddStandard(
      CrowdDemoBehaviorControllerIds::Navigation, 3,
      CrowdStandardSources::SpeedLimit, Limit);
  }

  class FLogisticsPlanner final : public ICrowdDemoBusinessPlanner
  {
  public:
    FCrowdDemoBusinessPlannerId GetPlannerId() const override
    {
      return CrowdDemoBusinessPlanners::Logistics;
    }

    bool Evaluate(
      const FCrowdDemoPlanningSnapshot& Snapshot,
      const FCrowdDemoPlannerAgentFact& Agent,
      FCrowdDemoPlannerWriter& Writer) const override
    {
      const FCrowdDemoObjectiveId ObjectiveId = Agent.bCarrying
        ? CrowdDemoBusinessObjectives::LogisticsSink
        : CrowdDemoBusinessObjectives::LogisticsSource;
      const FCrowdDemoObjectiveFact* Objective =
        FindObjective(Snapshot, Agent.EntityRef, ObjectiveId);
      if (!Objective) return false;

      FCrowdArriveAtLocationPayload Move{};
      Move.TargetLocation = FVector3f(Objective->Location);
      Move.MaximumSpeedCmps = Snapshot.Settings.MaximumSpeedCmps;
      Move.AcceptanceRadiusCm = 80.0f;
      Move.SlowdownRadiusCm = 300.0f;
      if (!Writer.AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 1,
          CrowdStandardSources::ArriveAtLocation, Move)
        || !AddSpeedLimit(Writer, Snapshot)
        || !AddFaceMovement(Writer))
        return false;

      if (Agent.bCarrying)
      {
        FCrowdDemoBehaviorSourcePayload Carry{};
        Carry.PrimaryId = 1;
        Carry.SecondaryId = 1;
        if (!Writer.AddDemo(
            CrowdDemoBehaviorControllerIds::Presentation, 1,
            CrowdDemoSourceTypeIds::CarryCargo, Carry))
          return false;
      }

      const float InteractionRadius =
        Snapshot.Settings.PopulationLimit > 20
        ? Snapshot.Settings.ScaleInteractionRadiusCm
        : Snapshot.Settings.InteractionRadiusCm;
      if (FVector::Distance(Agent.Position, Objective->Location)
          <= InteractionRadius
        && Snapshot.FixedStepIndex - Agent.LastLogisticsFixedStep
          >= Snapshot.Settings.LogisticsCooldownSteps)
      {
        const ECrowdDemoBusinessCommitKind Kind = Agent.bCarrying
          ? ECrowdDemoBusinessCommitKind::CargoDeliver
          : ECrowdDemoBusinessCommitKind::CargoPickup;
        FCrowdDemoBehaviorSourcePayload Interaction{};
        Interaction.PrimaryId = Agent.bCarrying
          ? CrowdDemoBehaviorAdapterIds::CargoDeliver
          : CrowdDemoBehaviorAdapterIds::CargoPickup;
        Interaction.SecondaryId =
          static_cast<uint32>(Agent.bCarrying
            ? ECrowdActiveBehavior::HaulDeliver
            : ECrowdActiveBehavior::HaulPickup) + 1;
        Interaction.Quantity = 1;
        Interaction.TargetRef = Agent.TaskRef;
        Interaction.CommitId = FCrowdDemoBusinessCommitId::Make(
          Kind, Snapshot.FixedStepIndex, Agent.TransitionRevision,
          Agent.EntityRef, Agent.TaskRef, {}, Interaction.SecondaryId, 1);
        if (!Writer.AddDemo(
            CrowdDemoBehaviorControllerIds::Interaction, 1,
            Agent.bCarrying
              ? CrowdDemoSourceTypeIds::DeliverInteraction
              : CrowdDemoSourceTypeIds::PickupInteraction,
            Interaction, 1))
          return false;
      }
      return true;
    }
  };

  class FPursueAttackPlanner final : public ICrowdDemoBusinessPlanner
  {
  public:
    FCrowdDemoBusinessPlannerId GetPlannerId() const override
    {
      return CrowdDemoBusinessPlanners::PursueAttack;
    }

    bool Evaluate(
      const FCrowdDemoPlanningSnapshot& Snapshot,
      const FCrowdDemoPlannerAgentFact& Agent,
      FCrowdDemoPlannerWriter& Writer) const override
    {
      const FCrowdDemoPlannerAgentFact* Target = SelectPeer(
        Snapshot, Agent, CrowdDemoBusinessPlanners::GuardFlee);
      if (!Target) return AddSpeedLimit(Writer, Snapshot);
      FCrowdPursueEntityPayload Pursue{};
      Pursue.TargetRef = Target->EntityRef;
      Pursue.MaximumSpeedCmps = Snapshot.Settings.MaximumSpeedCmps;
      Pursue.AcceptanceRadiusCm = 140.0f;
      Pursue.MaximumPredictionSeconds = 0.25f;
      FCrowdMaintainDistancePayload DistanceBand{};
      DistanceBand.TargetRef = Target->EntityRef;
      DistanceBand.MinimumDistanceCm = 130.0f;
      DistanceBand.MaximumDistanceCm = 180.0f;
      DistanceBand.HysteresisCm = 10.0f;
      DistanceBand.MaximumCorrectionSpeedCmps = 150.0f;
      if (!Writer.AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 1,
          CrowdStandardSources::PursueEntity, Pursue)
        || !Writer.AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 2,
          CrowdStandardSources::MaintainDistance, DistanceBand)
        || !AddSpeedLimit(Writer, Snapshot)
        || !AddFaceEntity(
          Writer, Target->EntityRef, Snapshot.FactRevision))
        return false;

      const float AttackRadius =
        Snapshot.Settings.PopulationLimit > 20
        ? Snapshot.Settings.ScaleInteractionRadiusCm
        : Snapshot.Settings.InteractionRadiusCm;
      if (FVector::Distance(Agent.Position, Target->Position)
          <= AttackRadius
        && Snapshot.FixedStepIndex - Agent.LastAttackFixedStep
          >= Snapshot.Settings.AttackCooldownSteps)
      {
        FCrowdDemoHostIntent Attack{};
        Attack.ActionTypeId =
          CrowdDemoBusinessActions::Attack;
        Attack.PayloadTypeId =
          CrowdDemoAttackPayloadTypeIds::Melee;
        Attack.Quantity = 25;
        Attack.InstigatorRef = Agent.EntityRef;
        Attack.TargetRef = Target->EntityRef;
        Attack.ExpectedRevision = Agent.TransitionRevision;
        Attack.Vector = Target->Position;
        Attack.CommitId = FCrowdDemoBusinessCommitId::Make(
          ECrowdDemoBusinessCommitKind::CombatHit,
          Snapshot.FixedStepIndex, Agent.TransitionRevision,
          Agent.EntityRef, {}, Target->EntityRef,
          Attack.PayloadTypeId, Attack.Quantity);
        FCrowdMovementLockPayload Lock{};
        if (!Writer.AddHostIntent(Attack)
          || !Writer.AddStandard(
            CrowdDemoBehaviorControllerIds::Reaction, 1,
            CrowdStandardSources::MovementLock, Lock, 1))
          return false;
      }
      return true;
    }
  };

  class FGuardFleePlanner final : public ICrowdDemoBusinessPlanner
  {
  public:
    FCrowdDemoBusinessPlannerId GetPlannerId() const override
    {
      return CrowdDemoBusinessPlanners::GuardFlee;
    }

    bool Evaluate(
      const FCrowdDemoPlanningSnapshot& Snapshot,
      const FCrowdDemoPlannerAgentFact& Agent,
      FCrowdDemoPlannerWriter& Writer) const override
    {
      if (Agent.Health <= 50)
      {
        const FCrowdDemoPlannerAgentFact* Target = SelectPeer(
          Snapshot, Agent, CrowdDemoBusinessPlanners::PursueAttack);
        if (!Target) return AddSpeedLimit(Writer, Snapshot);
        FCrowdFleeFromEntityPayload Flee{};
        Flee.TargetRef = Target->EntityRef;
        Flee.MaximumSpeedCmps = Snapshot.Settings.MaximumSpeedCmps;
        Flee.SafeDistanceCm = 1000.0f;
        Flee.MaximumPredictionSeconds = 0.25f;
        return Writer.AddStandard(
            CrowdDemoBehaviorControllerIds::Navigation, 1,
            CrowdStandardSources::FleeFromEntity, Flee)
          && AddSpeedLimit(Writer, Snapshot)
          && AddFaceEntity(
            Writer, Target->EntityRef, Snapshot.FactRevision);
      }
      FCrowdMoveToLocationPayload Move{};
      Move.TargetLocation = FVector3f(Agent.Position);
      Move.MaximumSpeedCmps = 350.0f;
      Move.AcceptanceRadiusCm = 120.0f;
      return Writer.AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 1,
          CrowdStandardSources::MoveToLocation, Move, 0, 99)
        && AddSpeedLimit(Writer, Snapshot)
        && AddFaceMovement(Writer);
    }
  };

  class FRoamPlanner final : public ICrowdDemoBusinessPlanner
  {
  public:
    FCrowdDemoBusinessPlannerId GetPlannerId() const override
    {
      return CrowdDemoBusinessPlanners::Roam;
    }

    bool Evaluate(
      const FCrowdDemoPlanningSnapshot& Snapshot,
      const FCrowdDemoPlannerAgentFact& Agent,
      FCrowdDemoPlannerWriter& Writer) const override
    {
      const bool bUseMoveTo =
        ((Snapshot.FixedStepIndex
            / Snapshot.Settings.RoamSwitchIntervalSteps)
          + Agent.Assignment.Ordinal + 15) % 2 == 0;
      if (bUseMoveTo)
      {
        const FCrowdDemoObjectiveFact* Objective = FindObjective(
          Snapshot, Agent.EntityRef,
          CrowdDemoBusinessObjectives::RoamRoute);
        if (!Objective) return false;
        FCrowdMoveToLocationPayload Move{};
        Move.TargetLocation = FVector3f(Objective->Location);
        Move.MaximumSpeedCmps = 400.0f;
        Move.AcceptanceRadiusCm = 100.0f;
        if (!Writer.AddStandard(
            CrowdDemoBehaviorControllerIds::Navigation, 1,
            CrowdStandardSources::MoveToLocation, Move))
          return false;
      }
      else
      {
        FCrowdWanderSteeringPayload Wander{};
        Wander.SpeedCmps = 300.0f;
        Wander.ReselectIntervalSteps = 45;
        if (!Writer.AddStandard(
            CrowdDemoBehaviorControllerIds::Navigation, 1,
            CrowdStandardSources::WanderSteering, Wander))
          return false;
      }
      return AddSpeedLimit(Writer, Snapshot)
        && AddFaceMovement(Writer);
    }
  };

  class FEscortPlanner final : public ICrowdDemoBusinessPlanner
  {
  public:
    FCrowdDemoBusinessPlannerId GetPlannerId() const override
    {
      return CrowdDemoBusinessPlanners::Escort;
    }

    bool Evaluate(
      const FCrowdDemoPlanningSnapshot& Snapshot,
      const FCrowdDemoPlannerAgentFact& Agent,
      FCrowdDemoPlannerWriter& Writer) const override
    {
      const FCrowdDemoPlannerAgentFact* Anchor = SelectPeer(
        Snapshot, Agent, CrowdDemoBusinessPlanners::Roam);
      if (!Anchor) return AddSpeedLimit(Writer, Snapshot);
      const int32 EscortIndex =
        static_cast<int32>(Agent.Assignment.Ordinal);
      const FVector LocalOffset(
        -160.0 - 80.0 * (EscortIndex / 2),
        EscortIndex % 2 == 0 ? -100.0 : 100.0, 0.0);
      FCrowdFollowEntityPayload Follow{};
      Follow.TargetRef = Anchor->EntityRef;
      Follow.LocalOffset = FVector3f(LocalOffset);
      Follow.MaximumSpeedCmps = 450.0f;
      Follow.AcceptanceRadiusCm = 60.0f;
      Follow.PositionGain = 1.5f;
      FCrowdMaintainDistancePayload DistanceBand{};
      DistanceBand.TargetRef = Anchor->EntityRef;
      DistanceBand.MinimumDistanceCm = 120.0f;
      DistanceBand.MaximumDistanceCm = 300.0f;
      DistanceBand.HysteresisCm = 15.0f;
      DistanceBand.MaximumCorrectionSpeedCmps = 100.0f;
      FCrowdFormationOffsetPayload Formation{};
      Formation.PositionGain = 1.0f;
      Formation.MaximumCorrectionSpeedCmps = 120.0f;
      return Writer.AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 1,
          CrowdStandardSources::FollowEntity, Follow)
        && Writer.AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 2,
          CrowdStandardSources::MaintainDistance, DistanceBand)
        && AddSpeedLimit(Writer, Snapshot)
        && Writer.AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 4,
          CrowdStandardSources::FormationOffset, Formation)
        && AddFaceMovement(Writer)
        && Writer.AddContext({
          ECrowdDemoContextRequestKind::TargetKinematics,
          Anchor->EntityRef, FVector::ZeroVector,
          Snapshot.FactRevision})
        && Writer.AddContext({
          ECrowdDemoContextRequestKind::FormationAnchor,
          Anchor->EntityRef, LocalOffset,
          Snapshot.FactRevision});
    }
  };

  class FNoSourcePlanner final : public ICrowdDemoBusinessPlanner
  {
  public:
    explicit FNoSourcePlanner(const FCrowdDemoBusinessPlannerId InId)
      : Id(InId) {}
    FCrowdDemoBusinessPlannerId GetPlannerId() const override
    {
      return Id;
    }
    bool Evaluate(
      const FCrowdDemoPlanningSnapshot&,
      const FCrowdDemoPlannerAgentFact&,
      FCrowdDemoPlannerWriter&) const override
    {
      return true;
    }
  private:
    FCrowdDemoBusinessPlannerId Id;
  };

  class FMixedCombatPlanner final
    : public ICrowdDemoBusinessPlanner
  {
  public:
    FCrowdDemoBusinessPlannerId GetPlannerId() const override
    {
      return CrowdDemoBusinessPlanners::MixedCombat;
    }

    bool Evaluate(
      const FCrowdDemoPlanningSnapshot& Snapshot,
      const FCrowdDemoPlannerAgentFact& Agent,
      FCrowdDemoPlannerWriter& Writer) const override
    {
      const FCrowdDemoPlannerAgentFact* Target =
        SelectEnemy(Snapshot, Agent);
      if (!Target)
        return AddSpeedLimit(Writer, Snapshot)
          && AddFaceMovement(Writer);

      float MinimumDistance = 130.0f;
      float MaximumDistance = 260.0f;
      if (Agent.AttackProfileId
        == CrowdDemoAttackProfileIds::MidRange)
      {
        MinimumDistance = 400.0f;
        MaximumDistance = 600.0f;
      }
      else if (Agent.AttackProfileId
        == CrowdDemoAttackProfileIds::Ranged)
      {
        MinimumDistance = 700.0f;
        MaximumDistance = 850.0f;
      }
      else if (Agent.AttackProfileId
        != CrowdDemoAttackProfileIds::Melee)
      {
        return false;
      }

      FCrowdPursueEntityPayload Pursue{};
      Pursue.TargetRef = Target->EntityRef;
      Pursue.MaximumSpeedCmps = Snapshot.Settings.MaximumSpeedCmps;
      Pursue.AcceptanceRadiusCm = MinimumDistance;
      Pursue.MaximumPredictionSeconds = 0.25f;
      FCrowdMaintainDistancePayload Distance{};
      Distance.TargetRef = Target->EntityRef;
      Distance.MinimumDistanceCm = MinimumDistance;
      Distance.MaximumDistanceCm = MaximumDistance;
      Distance.HysteresisCm = 10.0f;
      Distance.MaximumCorrectionSpeedCmps = 180.0f;
      if (!Writer.AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 1,
          CrowdStandardSources::PursueEntity, Pursue)
        || !Writer.AddStandard(
          CrowdDemoBehaviorControllerIds::Navigation, 2,
          CrowdStandardSources::MaintainDistance, Distance)
        || !AddSpeedLimit(Writer, Snapshot)
        || !AddFaceEntity(
          Writer, Target->EntityRef, Snapshot.FactRevision))
        return false;
      if (Agent.AttackState.Phase
        == ECrowdDemoAttackPlannerPhase::Commit)
      {
        FCrowdMovementLockPayload Lock{};
        if (!Writer.AddStandard(
            CrowdDemoBehaviorControllerIds::Reaction, 1,
            CrowdStandardSources::MovementLock, Lock, 1))
          return false;
      }
      return true;
    }
  };

  uint32 DiagnosticLabelFor(
    const FCrowdDemoPlanningSnapshot& Snapshot,
    const FCrowdDemoPlannerAgentFact& Agent,
    const FCrowdDemoPlannerDecision& Decision)
  {
    if (Agent.Health <= 0)
      return static_cast<uint32>(ECrowdActiveBehavior::Dead);
    if (Agent.Assignment.PlannerId
      == CrowdDemoBusinessPlanners::Logistics)
      return static_cast<uint32>(Agent.bCarrying
        ? ECrowdActiveBehavior::HaulDeliver
        : ECrowdActiveBehavior::HaulPickup);
    if (Agent.Assignment.PlannerId
      == CrowdDemoBusinessPlanners::PursueAttack)
    {
      const bool bAttacking = Decision.HostIntents.ContainsByPredicate(
        [](const FCrowdDemoHostIntent& Intent)
        {
          return Intent.ActionTypeId
            == CrowdDemoBusinessActions::Attack;
        });
      return static_cast<uint32>(bAttacking
        ? ECrowdActiveBehavior::Attack
        : Decision.TargetRef.IsValid()
          ? ECrowdActiveBehavior::Pursue
          : ECrowdActiveBehavior::Idle);
    }
    if (Agent.Assignment.PlannerId
      == CrowdDemoBusinessPlanners::GuardFlee)
      return static_cast<uint32>(Agent.Health <= 50
        ? ECrowdActiveBehavior::Flee
        : ECrowdActiveBehavior::Guard);
    if (Agent.Assignment.PlannerId
      == CrowdDemoBusinessPlanners::Roam)
    {
      const bool bMove = Decision.DesiredSources.ContainsByPredicate(
        [](const FCrowdDemoDesiredSource& Source)
        {
          return Source.SourceTypeId
            == CrowdStandardSources::MoveToLocation;
        });
      return static_cast<uint32>(bMove
        ? ECrowdActiveBehavior::MoveTo
        : ECrowdActiveBehavior::Wander);
    }
    if (Agent.Assignment.PlannerId
      == CrowdDemoBusinessPlanners::Escort)
      return static_cast<uint32>(Decision.TargetRef.IsValid()
        ? ECrowdActiveBehavior::MoveTo
        : ECrowdActiveBehavior::Idle);
    if (Agent.Assignment.PlannerId
      == CrowdDemoBusinessPlanners::MixedCombat)
      return static_cast<uint32>(
        Agent.AttackState.Phase
            == ECrowdDemoAttackPlannerPhase::Windup
          || Agent.AttackState.Phase
            == ECrowdDemoAttackPlannerPhase::Commit
        ? ECrowdActiveBehavior::Attack
        : Decision.TargetRef.IsValid()
          ? ECrowdActiveBehavior::Pursue
          : ECrowdActiveBehavior::Idle);
    return static_cast<uint32>(ECrowdActiveBehavior::Idle);
  }
}

using namespace CrowdDemoBusinessPlannerPrivate;

bool FCrowdDemoPlannerAgentFact::IsValid() const
{
  return EntityRef.IsValid()
    && Assignment.IsValid()
    && Capabilities.IsValid()
    && !Position.ContainsNaN()
    && !Velocity.ContainsNaN()
    && !Facing.ContainsNaN()
    && (FactionId != 0 || AttackProfileId == 0)
    && (AttackProfileId == 0 || AttackState.IsValid())
    && (TaskRef.IsUnset() || TaskRef.IsValid())
    && Health >= 0
    && InteractionLayer < 64
    && TransitionRevision != 0;
}

bool FCrowdDemoObjectiveFact::IsValid() const
{
  return ObjectiveId.IsValid()
    && EntityRef.IsValid()
    && !Location.ContainsNaN()
    && Revision != 0;
}

bool FCrowdDemoPlanningSettings::IsValid() const
{
  return PopulationLimit > 0
    && FMath::IsFinite(MaximumSpeedCmps)
    && MaximumSpeedCmps >= 0.0f
    && FMath::IsFinite(ScaleMaximumSpeedCmps)
    && ScaleMaximumSpeedCmps >= 0.0f
    && InteractionRadiusCm >= 0.0f
    && ScaleInteractionRadiusCm >= 0.0f
    && LogisticsCooldownSteps > 0
    && AttackCooldownSteps > 0
    && RoamSwitchIntervalSteps > 0
    && HitReactionDurationSteps > 0;
}

bool FCrowdDemoPlanningSnapshot::Finalize()
{
  bValid = false;
  StableHash = 0;
  if (!ScenarioId.IsValid()
    || FixedStepIndex < 0
    || FactRevision == 0
    || !Settings.IsValid())
    return false;
  Agents.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  Objectives.Sort([](const auto& A, const auto& B)
  {
    if (A.EntityRef != B.EntityRef)
      return A.EntityRef < B.EntityRef;
    return A.ObjectiveId < B.ObjectiveId;
  });
  uint64 Hash = FnvOffset;
  FoldIntegral(Hash, ScenarioId.Value);
  FoldIntegral(Hash, static_cast<uint64>(FixedStepIndex));
  FoldIntegral(Hash, FactRevision);
  for (int32 Index = 0; Index < Agents.Num(); ++Index)
  {
    const FCrowdDemoPlannerAgentFact& Agent = Agents[Index];
    if (!Agent.IsValid()
      || (Index > 0
        && !(Agents[Index - 1].EntityRef < Agent.EntityRef)))
      return false;
    FoldRef(Hash, Agent.EntityRef);
    FoldIntegral(Hash, Agent.Assignment.PlannerId.Value);
    FoldIntegral(Hash, Agent.Assignment.CohortId);
    FoldIntegral(Hash, Agent.Assignment.Ordinal);
    FoldIntegral(Hash, Agent.FactionId);
    FoldIntegral(Hash, Agent.AttackProfileId);
    FoldVector(Hash, Agent.Position);
    FoldIntegral(Hash, static_cast<uint32>(Agent.Health));
    FoldIntegral(
      Hash, static_cast<uint8>(Agent.AttackState.Phase));
    FoldIntegral(
      Hash, static_cast<uint64>(
        Agent.AttackState.PhaseEnterFixedStep));
    FoldRef(Hash, Agent.AttackState.TargetRef);
    FoldIntegral(Hash, Agent.AttackState.FireSequence);
  }
  for (int32 Index = 0; Index < Objectives.Num(); ++Index)
  {
    const FCrowdDemoObjectiveFact& Objective = Objectives[Index];
    if (!Objective.IsValid()
      || (Index > 0
        && Objectives[Index - 1].EntityRef == Objective.EntityRef
        && Objectives[Index - 1].ObjectiveId
          == Objective.ObjectiveId))
      return false;
    FoldRef(Hash, Objective.EntityRef);
    FoldIntegral(Hash, Objective.ObjectiveId.Value);
    FoldVector(Hash, Objective.Location);
    FoldIntegral(Hash, Objective.Revision);
  }
  StableHash = Hash == 0 ? 1 : Hash;
  bValid = true;
  return true;
}

bool FCrowdDemoContextRequest::IsValid() const
{
  return (Kind == ECrowdDemoContextRequestKind::TargetKinematics
      || Kind == ECrowdDemoContextRequestKind::FormationAnchor)
    && SubjectRef.IsValid()
    && !LocalOffset.ContainsNaN()
    && FactRevision != 0;
}

bool FCrowdDemoHostIntent::IsValid() const
{
  return ActionTypeId != 0
    && CommitId != 0
    && InstigatorRef.IsValid()
    && (TargetRef.IsUnset() || TargetRef.IsValid())
    && PayloadTypeId != 0
    && ExpectedRevision != 0
    && Quantity >= 0
    && !Vector.ContainsNaN();
}

bool FCrowdDemoPlannerDecision::Finalize()
{
  bValid = false;
  StableHash = 0;
  if (!EntityRef.IsValid()
    || !PlannerId.IsValid()
    || DesiredSources.Num() > CrowdBehavior::MaxSourcesPerEntity
    || ContextRequests.Num()
      > CrowdBehavior::MaxContextRecordsPerEntity)
    return false;
  DesiredSources.Sort([](const auto& A, const auto& B)
  {
    if (A.ControllerId != B.ControllerId)
      return A.ControllerId < B.ControllerId;
    return A.SourceSequence < B.SourceSequence;
  });
  ContextRequests.Sort([](const auto& A, const auto& B)
  {
    if (A.Kind != B.Kind)
      return static_cast<uint8>(A.Kind)
        < static_cast<uint8>(B.Kind);
    return A.SubjectRef < B.SubjectRef;
  });
  HostIntents.Sort([](const auto& A, const auto& B)
  {
    if (A.ActionTypeId != B.ActionTypeId)
      return A.ActionTypeId < B.ActionTypeId;
    return A.CommitId < B.CommitId;
  });
  uint64 Hash = FnvOffset;
  FoldRef(Hash, EntityRef);
  FoldIntegral(Hash, PlannerId.Value);
  FoldIntegral(Hash, DiagnosticLabel);
  FoldRef(Hash, TargetRef);
  FoldRef(Hash, TaskRef);
  for (int32 Index = 0; Index < DesiredSources.Num(); ++Index)
  {
    const FCrowdDemoDesiredSource& Source = DesiredSources[Index];
    if (!Source.IsValid()
      || (Index > 0
        && DesiredSources[Index - 1].ControllerId
          == Source.ControllerId
        && DesiredSources[Index - 1].SourceSequence
          == Source.SourceSequence))
      return false;
    FoldIntegral(Hash, Source.ControllerId.Value);
    FoldIntegral(Hash, Source.SourceSequence);
    FoldIntegral(Hash, Source.SourceTypeId.Value);
    FoldIntegral(Hash, Source.Payload.CalculateStableHash());
  }
  for (int32 Index = 0; Index < ContextRequests.Num(); ++Index)
  {
    const FCrowdDemoContextRequest& Request =
      ContextRequests[Index];
    if (!Request.IsValid()
      || (Index > 0
        && ContextRequests[Index - 1].Kind == Request.Kind
        && ContextRequests[Index - 1].SubjectRef
          == Request.SubjectRef))
      return false;
    FoldIntegral(Hash, static_cast<uint8>(Request.Kind));
    FoldRef(Hash, Request.SubjectRef);
    FoldVector(Hash, Request.LocalOffset);
  }
  for (int32 Index = 0; Index < HostIntents.Num(); ++Index)
  {
    const FCrowdDemoHostIntent& Intent = HostIntents[Index];
    if (!Intent.IsValid()
      || (Index > 0
        && HostIntents[Index - 1].ActionTypeId
          == Intent.ActionTypeId
        && HostIntents[Index - 1].CommitId
          == Intent.CommitId))
      return false;
    FoldIntegral(Hash, Intent.ActionTypeId);
    FoldIntegral(Hash, Intent.CommitId);
  }
  StableHash = Hash == 0 ? 1 : Hash;
  bValid = true;
  return true;
}

bool FCrowdDemoPlannerWriter::AddDemo(
  const FCrowdBehaviorControllerId ControllerId,
  const uint32 SourceSequence,
  const FCrowdBehaviorSourceTypeId TypeId,
  const FCrowdDemoBehaviorSourcePayload& Payload,
  const int32 LifetimeSteps,
  const int16 Priority)
{
  if (Decision.DesiredSources.Num()
    >= CrowdBehavior::MaxSourcesPerEntity)
    return false;
  FCrowdDemoDesiredSource& Entry =
    Decision.DesiredSources.AddDefaulted_GetRef();
  Entry.ControllerId = ControllerId;
  Entry.SourceSequence = SourceSequence;
  Entry.SourceTypeId = TypeId;
  Entry.Priority = Priority;
  Entry.LifetimeSteps = LifetimeSteps;
  const FCrowdDemoBehaviorSourcePayload Canonical =
    CrowdDemoCanonicalPlannerPayload::Copy(Payload);
  return Entry.Payload.Set(
    CrowdDemoBehaviorSchemas::Standard, Canonical);
}

bool FCrowdDemoPlannerWriter::AddContext(
  const FCrowdDemoContextRequest& Request)
{
  if (!Request.IsValid()
    || Decision.ContextRequests.Num()
      >= CrowdBehavior::MaxContextRecordsPerEntity)
    return false;
  Decision.ContextRequests.Add(Request);
  return true;
}

bool FCrowdDemoPlannerWriter::AddHostIntent(
  const FCrowdDemoHostIntent& Intent)
{
  if (!Intent.IsValid()
    || Decision.HostIntents.Num() >= 4)
    return false;
  Decision.HostIntents.Add(Intent);
  return true;
}

bool FCrowdDemoBusinessPlannerRegistry::Register(
  TSharedRef<const ICrowdDemoBusinessPlanner, ESPMode::ThreadSafe>
    Planner)
{
  if (bFrozen || !Planner->GetPlannerId().IsValid())
    return false;
  if (Find(Planner->GetPlannerId()))
    return false;
  Planners.Add(MoveTemp(Planner));
  return true;
}

bool FCrowdDemoBusinessPlannerRegistry::Freeze()
{
  if (bFrozen || Planners.IsEmpty())
    return false;
  Planners.Sort([](const auto& A, const auto& B)
  {
    return A->GetPlannerId() < B->GetPlannerId();
  });
  uint64 Hash = FnvOffset;
  for (int32 Index = 0; Index < Planners.Num(); ++Index)
  {
    if (Index > 0
      && Planners[Index - 1]->GetPlannerId()
        == Planners[Index]->GetPlannerId())
      return false;
    FoldIntegral(Hash, Planners[Index]->GetPlannerId().Value);
  }
  StableHash = Hash == 0 ? 1 : Hash;
  bFrozen = true;
  return true;
}

const ICrowdDemoBusinessPlanner*
FCrowdDemoBusinessPlannerRegistry::Find(
  const FCrowdDemoBusinessPlannerId PlannerId) const
{
  const auto* Entry = Planners.FindByPredicate(
    [&](const auto& Planner)
    {
      return Planner->GetPlannerId() == PlannerId;
    });
  return Entry ? &(**Entry) : nullptr;
}

bool FCrowdDemoBusinessPlannerRunner::BuildDefaultRegistry(
  FCrowdDemoBusinessPlannerRegistry& OutRegistry)
{
  OutRegistry = {};
  return OutRegistry.Register(
      MakeShared<FLogisticsPlanner, ESPMode::ThreadSafe>())
    && OutRegistry.Register(
      MakeShared<FPursueAttackPlanner, ESPMode::ThreadSafe>())
    && OutRegistry.Register(
      MakeShared<FGuardFleePlanner, ESPMode::ThreadSafe>())
    && OutRegistry.Register(
      MakeShared<FRoamPlanner, ESPMode::ThreadSafe>())
    && OutRegistry.Register(
      MakeShared<FEscortPlanner, ESPMode::ThreadSafe>())
    && OutRegistry.Register(
      MakeShared<FNoSourcePlanner, ESPMode::ThreadSafe>(
        CrowdDemoBusinessPlanners::VatShowcase))
    && OutRegistry.Register(
      MakeShared<FNoSourcePlanner, ESPMode::ThreadSafe>(
        CrowdDemoBusinessPlanners::RangedAttack))
    && OutRegistry.Register(
      MakeShared<FMixedCombatPlanner, ESPMode::ThreadSafe>())
    && OutRegistry.Freeze();
}

namespace
{
  bool EvaluatePlannerAgent(
    const FCrowdDemoBusinessPlannerRegistry& Registry,
    const FCrowdDemoPlanningSnapshot& Snapshot,
    const FCrowdDemoPlannerAgentFact& Agent,
    FCrowdDemoPlannerDecision& OutDecision)
  {
    OutDecision = {};
    const ICrowdDemoBusinessPlanner* Planner =
      Registry.Find(Agent.Assignment.PlannerId);
    if (!Planner) return false;
    OutDecision.EntityRef = Agent.EntityRef;
    OutDecision.PlannerId = Agent.Assignment.PlannerId;
    OutDecision.TaskRef = Agent.TaskRef;
    if (Agent.Health <= 0)
    {
      FCrowdMovementLockPayload Lock{};
      FCrowdDemoPlannerWriter Writer(OutDecision);
      if (!Writer.AddStandard(
          CrowdDemoBehaviorControllerIds::Reaction, 2,
          CrowdStandardSources::MovementLock, Lock))
        return false;
    }
    else
    {
      FCrowdDemoPlannerWriter Writer(OutDecision);
      if (!Planner->Evaluate(Snapshot, Agent, Writer))
        return false;
      for (const FCrowdDemoContextRequest& Request
        : OutDecision.ContextRequests)
      {
        if (!OutDecision.TargetRef.IsValid()
          && Request.Kind
            == ECrowdDemoContextRequestKind::TargetKinematics)
          OutDecision.TargetRef = Request.SubjectRef;
      }
      if (Agent.HitReactionUntilFixedStep
        > Snapshot.FixedStepIndex)
      {
        FCrowdTimedImpulsePayload Impulse{};
        Impulse.InitialVelocity =
          FVector3f(Agent.HitReactionVelocity);
        Impulse.DecayMode = ECrowdImpulseDecayMode::Linear;
        if (!Writer.AddStandard(
            CrowdDemoBehaviorControllerIds::Reaction, 3,
            CrowdStandardSources::TimedImpulse, Impulse,
            static_cast<int32>(
              Agent.HitReactionUntilFixedStep
                - Snapshot.FixedStepIndex)))
          return false;
      }
    }
    OutDecision.DiagnosticLabel =
      DiagnosticLabelFor(Snapshot, Agent, OutDecision);
    return OutDecision.Finalize();
  }
}

bool FCrowdDemoBusinessPlannerRunner::Evaluate(
  const FCrowdDemoBusinessPlannerRegistry& Registry,
  const FCrowdDemoPlanningSnapshot& Snapshot,
  FCrowdDemoPlannerDecisionBatch& OutBatch)
{
  OutBatch = {};
  if (!Registry.IsFrozen()
    || Registry.GetStableHash() == 0
    || !Snapshot.bValid
    || Snapshot.StableHash == 0)
    return false;
  OutBatch.ScenarioId = Snapshot.ScenarioId;
  OutBatch.FixedStepIndex = Snapshot.FixedStepIndex;
  if (Snapshot.ScenarioId == CrowdDemoBusinessScenarios::NoBusiness)
  {
    uint64 Hash = FnvOffset;
    FoldIntegral(Hash, Snapshot.ScenarioId.Value);
    FoldIntegral(Hash, static_cast<uint64>(Snapshot.FixedStepIndex));
    OutBatch.StableHash = Hash == 0 ? 1 : Hash;
    OutBatch.bValid = true;
    return true;
  }

  uint64 BatchHash = FnvOffset;
  for (const FCrowdDemoPlannerAgentFact& Agent : Snapshot.Agents)
  {
    FCrowdDemoPlannerDecision Decision{};
    if (!EvaluatePlannerAgent(
        Registry, Snapshot, Agent, Decision))
      return false;
    FoldIntegral(BatchHash, Decision.StableHash);
    OutBatch.Decisions.Add(MoveTemp(Decision));
  }
  OutBatch.StableHash = BatchHash == 0 ? 1 : BatchHash;
  OutBatch.bValid = true;
  return true;
}

bool FCrowdDemoBusinessPlannerRunner::EvaluateSharded(
  const FCrowdDemoBusinessPlannerRegistry& Registry,
  const FCrowdDemoPlanningSnapshot& Snapshot,
  const int32 ShardSize,
  FCrowdDemoPlannerDecisionBatch& OutBatch,
  const bool bReverseDispatchOrder)
{
  OutBatch = {};
  if (ShardSize <= 0 || !Registry.IsFrozen()
    || Registry.GetStableHash() == 0
    || !Snapshot.bValid || Snapshot.StableHash == 0)
    return false;
  if (Snapshot.ScenarioId
      == CrowdDemoBusinessScenarios::NoBusiness)
    return Evaluate(Registry, Snapshot, OutBatch);
  const int32 ShardCount = FMath::DivideAndRoundUp(
    Snapshot.Agents.Num(), ShardSize);
  TArray<FCrowdDemoPlannerDecision> Decisions;
  TArray<uint8> Valid;
  Decisions.SetNum(Snapshot.Agents.Num());
  Valid.Init(0, Snapshot.Agents.Num());
  ParallelFor(ShardCount, [&](const int32 DispatchIndex)
  {
    const int32 ShardIndex = bReverseDispatchOrder
      ? ShardCount - DispatchIndex - 1
      : DispatchIndex;
    const int32 Begin = ShardIndex * ShardSize;
    const int32 End = FMath::Min(
      Begin + ShardSize, Snapshot.Agents.Num());
    for (int32 Index = Begin; Index < End; ++Index)
    {
      Valid[Index] = EvaluatePlannerAgent(
        Registry, Snapshot, Snapshot.Agents[Index],
        Decisions[Index]) ? 1 : 0;
    }
  });
  OutBatch.ScenarioId = Snapshot.ScenarioId;
  OutBatch.FixedStepIndex = Snapshot.FixedStepIndex;
  uint64 BatchHash = FnvOffset;
  for (int32 Index = 0; Index < Decisions.Num(); ++Index)
  {
    if (Valid[Index] == 0)
    {
      OutBatch = {};
      return false;
    }
    FoldIntegral(BatchHash, Decisions[Index].StableHash);
    OutBatch.Decisions.Add(MoveTemp(Decisions[Index]));
  }
  OutBatch.StableHash = BatchHash == 0 ? 1 : BatchHash;
  OutBatch.bValid = true;
  return true;
}

bool FCrowdDemoBusinessPlannerRunner::BuildMixedAssignment(
  const int32 SlotIndex,
  FCrowdDemoPlannerAssignment& OutAssignment)
{
  OutAssignment = {};
  if (SlotIndex <= 0) return false;
  const int32 Role = ((SlotIndex - 1) % 20) + 1;
  OutAssignment.CohortId =
    static_cast<uint32>((SlotIndex - 1) / 20 + 1);
  if (Role <= 6)
  {
    OutAssignment.PlannerId = CrowdDemoBusinessPlanners::Logistics;
    OutAssignment.Ordinal = static_cast<uint16>(Role - 1);
  }
  else if (Role <= 10)
  {
    OutAssignment.PlannerId = CrowdDemoBusinessPlanners::PursueAttack;
    OutAssignment.Ordinal = static_cast<uint16>(Role - 7);
  }
  else if (Role <= 14)
  {
    OutAssignment.PlannerId = CrowdDemoBusinessPlanners::GuardFlee;
    OutAssignment.Ordinal = static_cast<uint16>(Role - 11);
  }
  else if (Role <= 16)
  {
    OutAssignment.PlannerId = CrowdDemoBusinessPlanners::Roam;
    OutAssignment.Ordinal = static_cast<uint16>(Role - 15);
  }
  else
  {
    OutAssignment.PlannerId = CrowdDemoBusinessPlanners::Escort;
    OutAssignment.Ordinal = static_cast<uint16>(Role - 17);
  }
  return OutAssignment.IsValid();
}

bool FCrowdDemoBusinessPlannerRunner::GetObjectivePlacementOrdinal(
  const FCrowdDemoPlannerAssignment& Assignment,
  uint32& OutPlacementOrdinal)
{
  OutPlacementOrdinal = 0;
  if (!Assignment.IsValid()) return false;
  const uint32 CohortOffset = (Assignment.CohortId - 1) * 6;
  if (Assignment.PlannerId
    == CrowdDemoBusinessPlanners::Logistics)
  {
    OutPlacementOrdinal = CohortOffset + Assignment.Ordinal;
    return true;
  }
  if (Assignment.PlannerId == CrowdDemoBusinessPlanners::Roam)
  {
    OutPlacementOrdinal =
      CohortOffset + 14 + Assignment.Ordinal;
    return true;
  }
  OutPlacementOrdinal =
    static_cast<uint32>(Assignment.CohortId - 1) * 20
    + Assignment.Ordinal;
  return true;
}
