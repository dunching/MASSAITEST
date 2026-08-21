#include "MassCrowdStandardSources.h"

namespace
{
  bool IsFinite(const FVector& Value)
  {
    return FMath::IsFinite(Value.X)
      && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }

  FVector ToVector(const FVector3f& Value)
  {
    return FVector(Value.X, Value.Y, Value.Z);
  }

  template <typename T>
  bool ReadPayload(
    const FCrowdBehaviorSourceEvaluationContext& Context,
    const FCrowdBehaviorSourceTypeId TypeId,
    T& OutPayload)
  {
    return Context.Instance.SourceTypeId == TypeId
      && Context.Instance.Payload.Get(
        CrowdStandardSources::PayloadSchema(TypeId), OutPayload);
  }

  template <typename T>
  bool ReadContext(
    const FCrowdBehaviorSourceEvaluationContext& Context,
    const FCrowdBehaviorContextTypeId TypeId,
    T& OutValue)
  {
    const FCrowdBehaviorContextRecord* Record =
      Context.FindContext(TypeId);
    return Record
      && Record->Get(
        TypeId,
        CrowdStandardSources::ContextSchemaVersion,
        OutValue);
  }

  bool ReadTarget(
    const FCrowdBehaviorSourceEvaluationContext& Context,
    const FCrowdStableEntityRef ExpectedRef,
    FCrowdTargetKinematicsV1& OutTarget)
  {
    if (!ExpectedRef.IsValid()
      || !ReadContext(
        Context,
        CrowdStandardSources::TargetKinematicsContextType,
        OutTarget)
      || OutTarget.TargetRef != ExpectedRef
      || !OutTarget.TargetRef.IsValid()
      || OutTarget.FactRevision == 0)
      return false;
    const FVector Position = ToVector(OutTarget.Position);
    const FVector Velocity = ToVector(OutTarget.Velocity);
    const FVector Facing = ToVector(OutTarget.Facing);
    return IsFinite(Position) && IsFinite(Velocity)
      && IsFinite(Facing) && !Facing.IsNearlyZero();
  }

  FVector Forward2D(const FVector3f& Facing)
  {
    FVector Forward = ToVector(Facing);
    Forward.Z = 0.0;
    return Forward.IsNearlyZero()
      ? FVector::ForwardVector
      : Forward.GetSafeNormal();
  }

  FCrowdResolvedMovementGoal MakeGoal(
    const FVector Location,
    const FCrowdStableEntityRef TargetRef,
    const uint64 Revision)
  {
    FCrowdResolvedMovementGoal Goal;
    Goal.Location = Location;
    Goal.TargetRef = TargetRef;
    Goal.FactRevision = FMath::Max<uint64>(1, Revision);
    Goal.bHasGoal = true;
    return Goal;
  }

  FCrowdBehaviorSourceSpec MakeSpec(
    const FCrowdBehaviorSourceTypeId TypeId,
    const ECrowdBehaviorChannel Channel,
    const int16 Priority,
    const ECrowdBehaviorBlendMode,
    const ECrowdBehaviorSourceReplicationPolicy Replication,
    const FCrowdCapabilityId Capability,
    const uint32 StateSchema = 0,
    const int32 MaxLifetimeSteps = 0)
  {
    FCrowdBehaviorSourceSpec Spec;
    Spec.TypeId = TypeId;
    Spec.Version = 1;
    Spec.ChannelMask = CrowdBehaviorChannelBit(Channel);
    Spec.DefaultPriority = Priority;
    Spec.MaxLifetimeSteps = MaxLifetimeSteps;
    Spec.PayloadSchemaId =
      CrowdStandardSources::PayloadSchema(TypeId);
    Spec.StateSchemaId = StateSchema;
    Spec.ReplicationPolicy = Replication;
    Spec.RequiredCapabilityCount = 1;
    Spec.RequiredCapabilities[0] = Capability;
    return Spec;
  }

  class FMoveToLocationEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdMoveToLocationPayload Payload;
      if (!ReadPayload(
          Context, CrowdStandardSources::MoveToLocation, Payload)
        || !FMath::IsFinite(Payload.MaximumSpeedCmps)
        || !FMath::IsFinite(Payload.AcceptanceRadiusCm)
        || Payload.MaximumSpeedCmps < 0.0f
        || Payload.AcceptanceRadiusCm < 0.0f)
        return false;
      const FVector Target = ToVector(Payload.TargetLocation);
      const FVector Delta = Target - Context.Position;
      if (!IsFinite(Target)) return false;
      FCrowdMovementContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Override;
      Value.DesiredVelocity =
        Delta.Size() <= Payload.AcceptanceRadiusCm
        ? FVector::ZeroVector
        : Delta.GetSafeNormal() * Payload.MaximumSpeedCmps;
      Value.Goal = MakeGoal(
        Target, {}, static_cast<uint64>(
          FMath::Max<int64>(0, Context.Instance.LastUpdateFixedStep) + 1));
      return Writer.AddMovement(Value);
    }
  };

  class FArriveAtLocationEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdArriveAtLocationPayload Payload;
      if (!ReadPayload(
          Context, CrowdStandardSources::ArriveAtLocation, Payload)
        || !FMath::IsFinite(Payload.MaximumSpeedCmps)
        || !FMath::IsFinite(Payload.AcceptanceRadiusCm)
        || !FMath::IsFinite(Payload.SlowdownRadiusCm)
        || Payload.MaximumSpeedCmps < 0.0f
        || Payload.AcceptanceRadiusCm < 0.0f
        || Payload.SlowdownRadiusCm <= Payload.AcceptanceRadiusCm)
        return false;
      const FVector Target = ToVector(Payload.TargetLocation);
      const FVector Delta = Target - Context.Position;
      if (!IsFinite(Target)) return false;
      const float Distance = static_cast<float>(Delta.Size());
      float Speed = 0.0f;
      if (Distance > Payload.AcceptanceRadiusCm)
      {
        const float Scale = FMath::Clamp(
          (Distance - Payload.AcceptanceRadiusCm)
            / (Payload.SlowdownRadiusCm
              - Payload.AcceptanceRadiusCm),
          0.0f, 1.0f);
        Speed = Payload.MaximumSpeedCmps * Scale;
      }
      FCrowdMovementContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Override;
      Value.DesiredVelocity =
        Delta.IsNearlyZero() ? FVector::ZeroVector
        : Delta.GetSafeNormal() * Speed;
      Value.Goal = MakeGoal(
        Target, {}, static_cast<uint64>(
          FMath::Max<int64>(0, Context.Instance.LastUpdateFixedStep) + 1));
      return Writer.AddMovement(Value);
    }
  };

  class FFollowEntityEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdFollowEntityPayload Payload;
      FCrowdTargetKinematicsV1 Target;
      if (!ReadPayload(
          Context, CrowdStandardSources::FollowEntity, Payload)
        || !ReadTarget(Context, Payload.TargetRef, Target)
        || !FMath::IsFinite(Payload.MaximumSpeedCmps)
        || !FMath::IsFinite(Payload.AcceptanceRadiusCm)
        || !FMath::IsFinite(Payload.PositionGain)
        || Payload.MaximumSpeedCmps < 0.0f
        || Payload.AcceptanceRadiusCm < 0.0f
        || Payload.PositionGain < 0.0f)
        return false;
      const FVector Forward = Forward2D(Target.Facing);
      const FVector Right = FVector(-Forward.Y, Forward.X, 0.0);
      const FVector Offset = ToVector(Payload.LocalOffset);
      const FVector Anchor = ToVector(Target.Position)
        + Forward * Offset.X + Right * Offset.Y
        + FVector::UpVector * Offset.Z;
      const FVector Delta = Anchor - Context.Position;
      FVector Velocity = ToVector(Target.Velocity);
      if (Delta.Size() > Payload.AcceptanceRadiusCm)
        Velocity += Delta * Payload.PositionGain;
      Velocity = Velocity.GetClampedToMaxSize(
        Payload.MaximumSpeedCmps);
      FCrowdMovementContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Override;
      Value.DesiredVelocity = Velocity;
      Value.Goal = MakeGoal(
        Anchor, Payload.TargetRef, Target.FactRevision);
      return Writer.AddMovement(Value);
    }
  };

  class FPursueEntityEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdPursueEntityPayload Payload;
      FCrowdTargetKinematicsV1 Target;
      if (!ReadPayload(
          Context, CrowdStandardSources::PursueEntity, Payload)
        || !ReadTarget(Context, Payload.TargetRef, Target)
        || !FMath::IsFinite(Payload.MaximumSpeedCmps)
        || !FMath::IsFinite(Payload.AcceptanceRadiusCm)
        || !FMath::IsFinite(Payload.MaximumPredictionSeconds)
        || Payload.MaximumSpeedCmps <= 0.0f
        || Payload.AcceptanceRadiusCm < 0.0f
        || Payload.MaximumPredictionSeconds < 0.0f)
        return false;
      const FVector Position = ToVector(Target.Position);
      const float PredictionSeconds = FMath::Clamp(
        static_cast<float>(
          FVector::Distance(Context.Position, Position)
            / Payload.MaximumSpeedCmps),
        0.0f, Payload.MaximumPredictionSeconds);
      const FVector Goal =
        Position + ToVector(Target.Velocity) * PredictionSeconds;
      const FVector Delta = Goal - Context.Position;
      FCrowdMovementContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Override;
      Value.DesiredVelocity =
        Delta.Size() <= Payload.AcceptanceRadiusCm
        ? FVector::ZeroVector
        : Delta.GetSafeNormal() * Payload.MaximumSpeedCmps;
      Value.Goal = MakeGoal(
        Goal, Payload.TargetRef, Target.FactRevision);
      return Writer.AddMovement(Value);
    }
  };

  class FFleeFromEntityEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdFleeFromEntityPayload Payload;
      FCrowdTargetKinematicsV1 Target;
      if (!ReadPayload(
          Context, CrowdStandardSources::FleeFromEntity, Payload)
        || !ReadTarget(Context, Payload.TargetRef, Target)
        || !FMath::IsFinite(Payload.MaximumSpeedCmps)
        || !FMath::IsFinite(Payload.SafeDistanceCm)
        || !FMath::IsFinite(Payload.MaximumPredictionSeconds)
        || Payload.MaximumSpeedCmps < 0.0f
        || Payload.SafeDistanceCm < 0.0f
        || Payload.MaximumPredictionSeconds < 0.0f)
        return false;
      const FVector Position = ToVector(Target.Position);
      const float PredictionSeconds =
        Payload.MaximumSpeedCmps > 0.0f
        ? FMath::Clamp(
          static_cast<float>(
            FVector::Distance(Context.Position, Position)
              / Payload.MaximumSpeedCmps),
          0.0f, Payload.MaximumPredictionSeconds)
        : 0.0f;
      const FVector Predicted =
        Position + ToVector(Target.Velocity) * PredictionSeconds;
      const FVector Away = Context.Position - Predicted;
      FCrowdMovementContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Override;
      Value.DesiredVelocity =
        Away.Size() >= Payload.SafeDistanceCm || Away.IsNearlyZero()
        ? FVector::ZeroVector
        : Away.GetSafeNormal() * Payload.MaximumSpeedCmps;
      return Writer.AddMovement(Value);
    }
  };

  class FMaintainDistanceEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdMaintainDistancePayload Payload;
      FCrowdTargetKinematicsV1 Target;
      if (!ReadPayload(
          Context, CrowdStandardSources::MaintainDistance, Payload)
        || !ReadTarget(Context, Payload.TargetRef, Target)
        || !FMath::IsFinite(Payload.MinimumDistanceCm)
        || !FMath::IsFinite(Payload.MaximumDistanceCm)
        || !FMath::IsFinite(Payload.HysteresisCm)
        || !FMath::IsFinite(Payload.MaximumCorrectionSpeedCmps)
        || Payload.MinimumDistanceCm < 0.0f
        || Payload.MaximumDistanceCm < Payload.MinimumDistanceCm
        || Payload.HysteresisCm < 0.0f
        || Payload.MaximumCorrectionSpeedCmps < 0.0f)
        return false;
      FCrowdMaintainDistanceState State{};
      if (Context.Instance.State.Size != 0
        && !Context.Instance.State.Get(
          CrowdStandardSources::MaintainDistanceStateSchema, State))
        return false;
      const FVector Delta =
        ToVector(Target.Position) - Context.Position;
      const float Distance = static_cast<float>(Delta.Size());
      if (State.Mode == ECrowdMaintainDistanceMode::Hold)
      {
        if (Distance
          > Payload.MaximumDistanceCm + Payload.HysteresisCm)
          State.Mode = ECrowdMaintainDistanceMode::Approach;
        else if (Distance
          < FMath::Max(
            0.0f,
            Payload.MinimumDistanceCm - Payload.HysteresisCm))
          State.Mode = ECrowdMaintainDistanceMode::Retreat;
      }
      else if (State.Mode == ECrowdMaintainDistanceMode::Approach
        && Distance <= Payload.MaximumDistanceCm)
        State.Mode = ECrowdMaintainDistanceMode::Hold;
      else if (State.Mode == ECrowdMaintainDistanceMode::Retreat
        && Distance >= Payload.MinimumDistanceCm)
        State.Mode = ECrowdMaintainDistanceMode::Hold;
      State.LastTargetRevision = Target.FactRevision;
      FCrowdBehaviorSourceState NextState;
      if (!NextState.Set(
          CrowdStandardSources::MaintainDistanceStateSchema, State)
        || !Writer.SetNextState(NextState))
        return false;
      FVector Direction = FVector::ZeroVector;
      if (!Delta.IsNearlyZero()
        && State.Mode == ECrowdMaintainDistanceMode::Approach)
        Direction = Delta.GetSafeNormal();
      else if (!Delta.IsNearlyZero()
        && State.Mode == ECrowdMaintainDistanceMode::Retreat)
        Direction = -Delta.GetSafeNormal();
      FCrowdMovementContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Additive;
      Value.DesiredVelocity =
        Direction * Payload.MaximumCorrectionSpeedCmps;
      return Writer.AddMovement(Value);
    }
  };

  class FFaceMovementEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdFaceMovementPayload Payload;
      if (!ReadPayload(
          Context, CrowdStandardSources::FaceMovement, Payload)
        || !FMath::IsFinite(Payload.MinimumSpeedCmps)
        || Payload.MinimumSpeedCmps < 0.0f)
        return false;
      FVector Direction =
        Context.Velocity.Size() >= Payload.MinimumSpeedCmps
        ? Context.Velocity : Context.Facing;
      if (!IsFinite(Direction) || Direction.IsNearlyZero())
        return false;
      FCrowdFacingContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Override;
      Value.DesiredDirection = Direction.GetSafeNormal();
      return Writer.AddFacing(Value);
    }
  };

  class FFaceEntityEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdFaceEntityPayload Payload;
      FCrowdTargetKinematicsV1 Target;
      if (!ReadPayload(
          Context, CrowdStandardSources::FaceEntity, Payload)
        || !ReadTarget(Context, Payload.TargetRef, Target))
        return false;
      const FVector Direction =
        ToVector(Target.Position) - Context.Position;
      if (Direction.IsNearlyZero()) return false;
      FCrowdFacingContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Override;
      Value.DesiredDirection = Direction.GetSafeNormal();
      return Writer.AddFacing(Value);
    }
  };

  class FMovementLockEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdMovementLockPayload Payload;
      if (!ReadPayload(
          Context, CrowdStandardSources::MovementLock, Payload)
        || Payload.bLockMovement > 1)
        return false;
      FCrowdConstraintContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Override;
      Value.SpeedLimitCmps = Payload.bLockMovement
        ? 0.0f : TNumericLimits<float>::Max();
      Value.bLockMovement = Payload.bLockMovement != 0;
      return Writer.AddConstraint(Value);
    }
  };

  class FSpeedLimitEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdSpeedLimitPayload Payload;
      if (!ReadPayload(
          Context, CrowdStandardSources::SpeedLimit, Payload)
        || !FMath::IsFinite(Payload.MaximumSpeedCmps)
        || Payload.MaximumSpeedCmps < 0.0f)
        return false;
      FCrowdConstraintContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::MinLimit;
      Value.SpeedLimitCmps = Payload.MaximumSpeedCmps;
      Value.AllowedNavLayerMask = Payload.AllowedNavLayerMask;
      return Writer.AddConstraint(Value);
    }
  };

  const FVector2f WanderDirections[] = {
    {1.0f, 0.0f}, {0.9238795f, 0.3826834f},
    {0.7071068f, 0.7071068f}, {0.3826834f, 0.9238795f},
    {0.0f, 1.0f}, {-0.3826834f, 0.9238795f},
    {-0.7071068f, 0.7071068f}, {-0.9238795f, 0.3826834f},
    {-1.0f, 0.0f}, {-0.9238795f, -0.3826834f},
    {-0.7071068f, -0.7071068f}, {-0.3826834f, -0.9238795f},
    {0.0f, -1.0f}, {0.3826834f, -0.9238795f},
    {0.7071068f, -0.7071068f}, {0.9238795f, -0.3826834f}};

  uint32 NextRandom(uint32& State)
  {
    if (State == 0) State = 0x9e3779b9u;
    State ^= State << 13;
    State ^= State >> 17;
    State ^= State << 5;
    return State;
  }

  class FWanderSteeringEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdWanderSteeringPayload Payload;
      if (!ReadPayload(
          Context, CrowdStandardSources::WanderSteering, Payload)
        || !FMath::IsFinite(Payload.SpeedCmps)
        || Payload.SpeedCmps < 0.0f
        || Payload.ReselectIntervalSteps == 0)
        return false;
      FCrowdWanderSteeringState State{};
      if (Context.Instance.State.Size == 0)
      {
        State.RandomState =
          Context.Instance.Handle.EntityRef.ProviderId
          ^ static_cast<uint32>(
            Context.Instance.Handle.EntityRef.StableEntityId)
          ^ Context.Instance.Handle.EntityRef.LifecycleSerial
          ^ Context.Instance.Handle.ControllerId.Value
          ^ Context.Instance.Handle.SourceSequence;
        State.NextReselectFixedStep = Context.FixedStepIndex;
      }
      else if (!Context.Instance.State.Get(
        CrowdStandardSources::WanderStateSchema, State))
        return false;
      if (Context.FixedStepIndex >= State.NextReselectFixedStep)
      {
        State.DirectionIndex = static_cast<uint8>(
          NextRandom(State.RandomState)
            % UE_ARRAY_COUNT(WanderDirections));
        State.NextReselectFixedStep =
          Context.FixedStepIndex + Payload.ReselectIntervalSteps;
      }
      if (State.DirectionIndex >= UE_ARRAY_COUNT(WanderDirections))
        return false;
      FCrowdBehaviorSourceState NextState;
      if (!NextState.Set(
          CrowdStandardSources::WanderStateSchema, State)
        || !Writer.SetNextState(NextState))
        return false;
      const FVector2f Direction =
        WanderDirections[State.DirectionIndex];
      FCrowdMovementContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Override;
      Value.DesiredVelocity = FVector(
        Direction.X, Direction.Y, 0.0f) * Payload.SpeedCmps;
      return Writer.AddMovement(Value);
    }
  };

  class FFormationOffsetEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdFormationOffsetPayload Payload;
      FCrowdFormationAnchorV1 Anchor;
      if (!ReadPayload(
          Context, CrowdStandardSources::FormationOffset, Payload)
        || !ReadContext(
          Context,
          CrowdStandardSources::FormationAnchorContextType,
          Anchor)
        || !Anchor.AnchorRef.IsValid()
        || Anchor.FactRevision == 0
        || !FMath::IsFinite(Payload.PositionGain)
        || !FMath::IsFinite(Payload.MaximumCorrectionSpeedCmps)
        || Payload.PositionGain < 0.0f
        || Payload.MaximumCorrectionSpeedCmps < 0.0f)
        return false;
      const FVector Forward = Forward2D(Anchor.Facing);
      const FVector Right(-Forward.Y, Forward.X, 0.0);
      const FVector Offset = ToVector(Anchor.LocalSlotOffset);
      const FVector Desired = ToVector(Anchor.Position)
        + Forward * Offset.X + Right * Offset.Y
        + FVector::UpVector * Offset.Z;
      if (!IsFinite(Desired)) return false;
      FCrowdMovementContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Additive;
      Value.DesiredVelocity =
        ((Desired - Context.Position) * Payload.PositionGain)
          .GetClampedToMaxSize(
            Payload.MaximumCorrectionSpeedCmps);
      return Writer.AddMovement(Value);
    }
  };

  class FTimedImpulseEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdTimedImpulsePayload Payload;
      if (!ReadPayload(
          Context, CrowdStandardSources::TimedImpulse, Payload)
        || Payload.DecayMode > ECrowdImpulseDecayMode::Linear
        || Context.Instance.StartFixedStep < 0
        || Context.Instance.ExpireFixedStep
          <= Context.Instance.StartFixedStep)
        return false;
      const FVector Initial = ToVector(Payload.InitialVelocity);
      if (!IsFinite(Initial)) return false;
      double Scale = 1.0;
      if (Payload.DecayMode == ECrowdImpulseDecayMode::Linear)
      {
        const int64 Lifetime =
          Context.Instance.ExpireFixedStep
          - Context.Instance.StartFixedStep;
        const int64 Remaining = FMath::Max<int64>(
          0,
          Context.Instance.ExpireFixedStep
          - Context.FixedStepIndex);
        Scale = static_cast<double>(Remaining)
          / static_cast<double>(Lifetime);
      }
      FCrowdMovementContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Override;
      Value.DesiredVelocity = Initial * Scale;
      return Writer.AddMovement(Value);
    }
  };

  class FSemanticStateEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter&) const override
    {
      FCrowdSemanticBehaviorStatePayload Payload;
      return ReadPayload(
          Context, CrowdStandardSources::SemanticState, Payload)
        && Payload.State < ECrowdSemanticBehaviorState::Count;
    }
  };

  class FStandardSourcesProvider final
    : public ICrowdBehaviorSourceProvider
  {
  public:
    FCrowdBehaviorProviderId GetProviderId() const override
    {
      return CrowdStandardSources::ProviderId;
    }

    bool Register(
      FCrowdBehaviorRegistryBuilder& Builder) const override
    {
      FCrowdBehaviorContextSchema TargetSchema;
      TargetSchema.TypeId =
        CrowdStandardSources::TargetKinematicsContextType;
      TargetSchema.Version =
        CrowdStandardSources::ContextSchemaVersion;
      TargetSchema.Size = sizeof(FCrowdTargetKinematicsV1);
      FCrowdBehaviorContextSchema FormationSchema;
      FormationSchema.TypeId =
        CrowdStandardSources::FormationAnchorContextType;
      FormationSchema.Version =
        CrowdStandardSources::ContextSchemaVersion;
      FormationSchema.Size = sizeof(FCrowdFormationAnchorV1);
      if (!Builder.RegisterContextSchema(TargetSchema)
        || !Builder.RegisterContextSchema(FormationSchema))
        return false;

      const auto Source = [&](
        const FCrowdBehaviorSourceSpec& Spec,
        TSharedRef<const ICrowdBehaviorSourceEvaluator,
          ESPMode::ThreadSafe> Evaluator)
      {
        return Builder.RegisterSource(Spec, Evaluator);
      };
      using Policy = ECrowdBehaviorSourceReplicationPolicy;
      return Source(
          MakeSpec(
            CrowdStandardSources::MoveToLocation,
            ECrowdBehaviorChannel::Movement, 100,
            ECrowdBehaviorBlendMode::Override,
            Policy::Predictable,
            CrowdStandardSources::MoveCapability),
          MakeShared<FMoveToLocationEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::ArriveAtLocation,
            ECrowdBehaviorChannel::Movement, 100,
            ECrowdBehaviorBlendMode::Override,
            Policy::Predictable,
            CrowdStandardSources::MoveCapability),
          MakeShared<FArriveAtLocationEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::FollowEntity,
            ECrowdBehaviorChannel::Movement, 100,
            ECrowdBehaviorBlendMode::Override,
            Policy::ResolvedOnly,
            CrowdStandardSources::MoveCapability),
          MakeShared<FFollowEntityEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::PursueEntity,
            ECrowdBehaviorChannel::Movement, 100,
            ECrowdBehaviorBlendMode::Override,
            Policy::ResolvedOnly,
            CrowdStandardSources::MoveCapability),
          MakeShared<FPursueEntityEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::FleeFromEntity,
            ECrowdBehaviorChannel::Movement, 200,
            ECrowdBehaviorBlendMode::Override,
            Policy::ResolvedOnly,
            CrowdStandardSources::MoveCapability),
          MakeShared<FFleeFromEntityEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::MaintainDistance,
            ECrowdBehaviorChannel::Movement, 25,
            ECrowdBehaviorBlendMode::Additive,
            Policy::ResolvedOnly,
            CrowdStandardSources::MoveCapability,
            CrowdStandardSources::MaintainDistanceStateSchema),
          MakeShared<FMaintainDistanceEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::FaceMovement,
            ECrowdBehaviorChannel::Facing, 100,
            ECrowdBehaviorBlendMode::Override,
            Policy::Predictable,
            CrowdStandardSources::FaceCapability),
          MakeShared<FFaceMovementEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::FaceEntity,
            ECrowdBehaviorChannel::Facing, 100,
            ECrowdBehaviorBlendMode::Override,
            Policy::ResolvedOnly,
            CrowdStandardSources::FaceCapability),
          MakeShared<FFaceEntityEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::MovementLock,
            ECrowdBehaviorChannel::Constraint, 1000,
            ECrowdBehaviorBlendMode::Override,
            Policy::Predictable,
            CrowdStandardSources::MoveCapability),
          MakeShared<FMovementLockEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::SpeedLimit,
            ECrowdBehaviorChannel::Constraint, 100,
            ECrowdBehaviorBlendMode::MinLimit,
            Policy::Predictable,
            CrowdStandardSources::MoveCapability),
          MakeShared<FSpeedLimitEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::WanderSteering,
            ECrowdBehaviorChannel::Movement, 100,
            ECrowdBehaviorBlendMode::Override,
            Policy::Predictable,
            CrowdStandardSources::MoveCapability,
            CrowdStandardSources::WanderStateSchema),
          MakeShared<FWanderSteeringEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::FormationOffset,
            ECrowdBehaviorChannel::Movement, 20,
            ECrowdBehaviorBlendMode::Additive,
            Policy::ResolvedOnly,
            CrowdStandardSources::FormationCapability),
          MakeShared<FFormationOffsetEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::TimedImpulse,
            ECrowdBehaviorChannel::Movement, 300,
            ECrowdBehaviorBlendMode::Override,
            Policy::Predictable,
            CrowdStandardSources::ImpulseCapability,
            0, 300),
          MakeShared<FTimedImpulseEvaluator, ESPMode::ThreadSafe>())
        && Source(
          MakeSpec(
            CrowdStandardSources::SemanticState,
            ECrowdBehaviorChannel::Presentation, 0,
            ECrowdBehaviorBlendMode::Override,
            Policy::Predictable,
            CrowdStandardSources::SemanticStateCapability),
          MakeShared<FSemanticStateEvaluator, ESPMode::ThreadSafe>());
    }
  };
}

TSharedRef<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>
CreateCrowdStandardSourcesProvider()
{
  return MakeShared<FStandardSourcesProvider, ESPMode::ThreadSafe>();
}
