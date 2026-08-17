#include "MassCrowdTestBehaviorProvider.h"

namespace
{
  class FTestEvaluator final : public ICrowdBehaviorSourceEvaluator
  {
  public:
    virtual bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdBuiltinBehaviorSourcePayload Payload;
      if (!Context.Instance.Payload.Get(
          CrowdBuiltinBehaviorSchemas::Standard, Payload))
        return false;
      const FCrowdBehaviorSourceTypeId Type =
        Context.Instance.SourceTypeId;
      if (Type == CrowdBuiltinSourceTypeIds::MoveToSink
        || Type == CrowdBuiltinSourceTypeIds::SharedFlow
        || Type == CrowdBuiltinSourceTypeIds::Formation)
      {
        FCrowdMovementContribution Value;
        Value.BlendMode =
          Type == CrowdBuiltinSourceTypeIds::MoveToSink
            ? ECrowdBehaviorBlendMode::Override
            : Type == CrowdBuiltinSourceTypeIds::SharedFlow
              ? ECrowdBehaviorBlendMode::WeightedAdd
              : ECrowdBehaviorBlendMode::Additive;
        Value.WeightQ15 = Payload.PrimaryId > 0
          ? static_cast<uint16>(FMath::Min<uint32>(
            Payload.PrimaryId, CrowdBehavior::FullQ15Weight))
          : CrowdBehavior::FullQ15Weight;
        Value.DesiredVelocity = Payload.Vector;
        return Writer.AddMovement(Value);
      }
      if (Type == CrowdBuiltinSourceTypeIds::FaceMovement
        || Type == CrowdBuiltinSourceTypeIds::FaceTarget)
      {
        FCrowdFacingContribution Value;
        Value.DesiredDirection = Payload.Vector.IsNearlyZero()
          ? FVector::ForwardVector : Payload.Vector;
        return Writer.AddFacing(Value);
      }
      if (Type == CrowdBuiltinSourceTypeIds::CarryCargo)
      {
        FCrowdPresentationContribution Value;
        Value.PropertyId = FMath::Max(1u, Payload.PrimaryId);
        Value.Value = Payload.SecondaryId;
        return Writer.AddPresentation(Value);
      }
      if (Type == CrowdBuiltinSourceTypeIds::HitReaction
        || Type == CrowdBuiltinSourceTypeIds::StunConstraint
        || Type == CrowdBuiltinSourceTypeIds::DeathConstraint)
      {
        FCrowdConstraintContribution Value;
        Value.BlendMode = ECrowdBehaviorBlendMode::Override;
        Value.SpeedLimitCmps = FMath::Max(
          0.0f, static_cast<float>(Payload.Vector.X));
        Value.bLockMovement =
          Type == CrowdBuiltinSourceTypeIds::DeathConstraint
          || (Payload.Flags & 1u) != 0;
        return Writer.AddConstraint(Value);
      }
      FCrowdInteractionContribution Interaction;
      Interaction.IntentTypeId = FMath::Max(1u, Payload.PrimaryId);
      Interaction.TargetRef = Payload.TargetRef;
      Interaction.PayloadTypeId = FMath::Max(1u, Payload.SecondaryId);
      Interaction.PayloadKey = Payload.Flags;
      if (!Writer.AddInteraction(Interaction)) return false;
      if (Payload.CommitId == 0) return true;
      FCrowdBusinessContribution Business;
      Business.AdapterId = Interaction.IntentTypeId;
      Business.ExclusiveGroup = Context.Instance.ExclusiveGroup;
      Business.CommitId = Payload.CommitId;
      Business.InstigatorRef = Context.Instance.Handle.EntityRef;
      Business.TargetRef = Payload.TargetRef;
      Business.PayloadTypeId = Interaction.PayloadTypeId;
      Business.Quantity = FMath::Max(1, Payload.Quantity);
      return Writer.AddBusiness(Business);
    }
  };

  class FTestProvider final : public ICrowdBehaviorSourceProvider
  {
  public:
    virtual FCrowdBehaviorProviderId GetProviderId() const override
    {
      return {90001};
    }

    virtual bool Register(
      FCrowdBehaviorRegistryBuilder& Builder) const override
    {
      FCrowdCapabilityProfile Profile;
      Profile.Key = CrowdBuiltinBehaviorSchemas::LegacyFullProfile;
      Profile.CapabilityIds = {
        CrowdBuiltinCapabilityIds::Move,
        CrowdBuiltinCapabilityIds::Wander,
        CrowdBuiltinCapabilityIds::MoveTo,
        CrowdBuiltinCapabilityIds::Pursue,
        CrowdBuiltinCapabilityIds::Haul,
        CrowdBuiltinCapabilityIds::Attack,
        CrowdBuiltinCapabilityIds::Guard,
        CrowdBuiltinCapabilityIds::Flee,
        CrowdBuiltinCapabilityIds::RangedAttack,
        CrowdBuiltinCapabilityIds::NavLayer,
        CrowdBuiltinCapabilityIds::Face,
        CrowdBuiltinCapabilityIds::Formation,
        CrowdBuiltinCapabilityIds::CarryCargo,
        CrowdBuiltinCapabilityIds::React};
      if (!Builder.RegisterProfile(MoveTemp(Profile)))
        return false;

      const auto Register = [&](const FCrowdBehaviorSourceTypeId TypeId,
        const uint16 Mask, const FCrowdCapabilityId Required,
        const int16 Priority = 10, const int32 Lifetime = 0,
        const uint16 ExclusiveGroup = 0)
      {
        FCrowdBehaviorSourceSpec Spec;
        Spec.TypeId = TypeId;
        Spec.ChannelMask = Mask;
        Spec.DefaultPriority = Priority;
        Spec.MaxLifetimeSteps = Lifetime;
        Spec.ExclusiveGroup = ExclusiveGroup;
        Spec.PayloadSchemaId = CrowdBuiltinBehaviorSchemas::Standard;
        Spec.ReplicationPolicy =
          ECrowdBehaviorSourceReplicationPolicy::Predictable;
        if (Required.IsValid())
        {
          Spec.RequiredCapabilityCount = 1;
          Spec.RequiredCapabilities[0] = Required;
        }
        return Builder.RegisterSource(
          Spec, MakeShared<FTestEvaluator, ESPMode::ThreadSafe>());
      };
      const uint16 Movement =
        CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Movement);
      const uint16 Facing =
        CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Facing);
      const uint16 Constraint =
        CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Constraint);
      const uint16 InteractionBusiness =
        CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Interaction)
        | CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Business);
      return Register(CrowdBuiltinSourceTypeIds::MoveToSink,
          Movement, CrowdBuiltinCapabilityIds::MoveTo, 30)
        && Register(CrowdBuiltinSourceTypeIds::SharedFlow,
          Movement, CrowdBuiltinCapabilityIds::Move, 10)
        && Register(CrowdBuiltinSourceTypeIds::Formation,
          Movement, CrowdBuiltinCapabilityIds::Formation, 20)
        && Register(CrowdBuiltinSourceTypeIds::FaceMovement,
          Facing, CrowdBuiltinCapabilityIds::Face)
        && Register(CrowdBuiltinSourceTypeIds::FaceTarget,
          Facing, CrowdBuiltinCapabilityIds::Face, 20)
        && Register(CrowdBuiltinSourceTypeIds::CarryCargo,
          CrowdBehaviorChannelBit(
            ECrowdBehaviorChannel::Presentation),
          CrowdBuiltinCapabilityIds::CarryCargo)
        && Register(CrowdBuiltinSourceTypeIds::PickupInteraction,
          InteractionBusiness, CrowdBuiltinCapabilityIds::Haul,
          40, 0, 1)
        && Register(CrowdBuiltinSourceTypeIds::DeliverInteraction,
          InteractionBusiness, CrowdBuiltinCapabilityIds::Haul,
          40, 0, 1)
        && Register(CrowdBuiltinSourceTypeIds::BusinessProbe,
          InteractionBusiness, CrowdBuiltinCapabilityIds::Attack,
          50, 0, 2)
        && Register(CrowdBuiltinSourceTypeIds::HitReaction,
          Constraint, CrowdBuiltinCapabilityIds::React, 100, 30)
        && Register(CrowdBuiltinSourceTypeIds::StunConstraint,
          Constraint, {}, 110, 300)
        && Register(CrowdBuiltinSourceTypeIds::DeathConstraint,
          Constraint, {}, 32760);
    }
  };
}

TSharedRef<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>
CreateMassCrowdTestBehaviorProvider()
{
  return MakeShared<FTestProvider, ESPMode::ThreadSafe>();
}
