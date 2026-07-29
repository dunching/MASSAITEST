#include "MassCrowdBehaviorFixture.h"

#include "Modules/ModuleManager.h"

namespace
{
  class FFixtureEvaluator final : public ICrowdBehaviorSourceEvaluator
  {
  public:
    virtual bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      CrowdBehaviorFixture::FPayload Payload;
      if (!Context.Instance.Payload.Get(
          CrowdBehaviorFixture::PayloadSchemaId, Payload))
        return false;

      CrowdBehaviorFixture::FContext Extra;
      const FCrowdBehaviorContextRecord* Record =
        Context.FindContext(CrowdBehaviorFixture::ContextTypeId);
      if (!Record || !Record->Get(
          CrowdBehaviorFixture::ContextTypeId, 1, Extra)
        || Extra.VelocityScaleQ15 < 0
        || Extra.VelocityScaleQ15 > CrowdBehavior::FullQ15Weight)
        return false;

      CrowdBehaviorFixture::FState State;
      if (Context.Instance.State.Size != 0
        && !Context.Instance.State.Get(
          CrowdBehaviorFixture::StateSchemaId, State))
        return false;
      ++State.EvaluationCount;
      FCrowdBehaviorSourceState NextState;
      if (!NextState.Set(CrowdBehaviorFixture::StateSchemaId, State)
        || !Writer.SetNextState(NextState))
        return false;

      FCrowdMovementContribution Movement;
      Movement.BlendMode = ECrowdBehaviorBlendMode::Override;
      Movement.DesiredVelocity = Payload.DesiredVelocity
        * (static_cast<double>(Extra.VelocityScaleQ15)
          / CrowdBehavior::FullQ15Weight);
      if (!Writer.AddMovement(Movement))
        return false;

      FCrowdFacingContribution Facing;
      Facing.BlendMode = ECrowdBehaviorBlendMode::Override;
      Facing.DesiredDirection = Movement.DesiredVelocity.IsNearlyZero()
        ? Context.Facing : Movement.DesiredVelocity.GetSafeNormal();
      if (!Writer.AddFacing(Facing))
        return false;

      FCrowdConstraintContribution Constraint;
      Constraint.BlendMode = ECrowdBehaviorBlendMode::MinLimit;
      Constraint.SpeedLimitCmps = 1000.0f;
      if (!Writer.AddConstraint(Constraint))
        return false;

      FCrowdInteractionContribution Interaction;
      Interaction.IntentTypeId = 70001u;
      Interaction.TargetRef = Payload.TargetRef;
      Interaction.PayloadTypeId = 70002u;
      Interaction.PayloadKey = State.EvaluationCount;
      if (!Writer.AddInteraction(Interaction))
        return false;

      FCrowdBusinessContribution Business;
      Business.AdapterId = 70001u;
      Business.CommitId = Payload.CommitId;
      Business.InstigatorRef = Context.Instance.Handle.EntityRef;
      Business.TargetRef = Payload.TargetRef;
      Business.PayloadTypeId = 70002u;
      Business.Quantity = 1;
      if (!Writer.AddBusiness(Business))
        return false;

      FCrowdPresentationContribution Presentation;
      Presentation.PropertyId = 70001u;
      Presentation.Value = State.EvaluationCount;
      return Writer.AddPresentation(Presentation);
    }
  };

  class FFixtureProvider final : public ICrowdBehaviorSourceProvider
  {
  public:
    virtual FCrowdBehaviorProviderId GetProviderId() const override
    {
      return CrowdBehaviorFixture::ProviderId;
    }

    virtual bool Register(
      FCrowdBehaviorRegistryBuilder& Builder) const override
    {
      FCrowdCapabilityProfile Profile;
      Profile.Key = CrowdBehaviorFixture::ProfileKey;
      Profile.CapabilityIds = {CrowdBehaviorFixture::CapabilityId};
      if (!Builder.RegisterProfile(MoveTemp(Profile)))
        return false;

      FCrowdBehaviorContextSchema ContextSchema;
      ContextSchema.TypeId = CrowdBehaviorFixture::ContextTypeId;
      ContextSchema.Version = 1;
      ContextSchema.Size = sizeof(CrowdBehaviorFixture::FContext);
      if (!Builder.RegisterContextSchema(ContextSchema))
        return false;

      const uint16 AllChannels =
        (uint16{1} << static_cast<uint8>(
          ECrowdBehaviorChannel::Movement))
        | (uint16{1} << static_cast<uint8>(
          ECrowdBehaviorChannel::Facing))
        | (uint16{1} << static_cast<uint8>(
          ECrowdBehaviorChannel::Constraint))
        | (uint16{1} << static_cast<uint8>(
          ECrowdBehaviorChannel::Interaction))
        | (uint16{1} << static_cast<uint8>(
          ECrowdBehaviorChannel::Business))
        | (uint16{1} << static_cast<uint8>(
          ECrowdBehaviorChannel::Presentation));
      const auto RegisterSource =
        [&](const FCrowdBehaviorSourceTypeId TypeId,
          const ECrowdBehaviorSourceReplicationPolicy Policy)
      {
        FCrowdBehaviorSourceSpec Spec;
        Spec.TypeId = TypeId;
        Spec.Version = 1;
        Spec.ChannelMask = AllChannels;
        Spec.DefaultPriority = 100;
        Spec.PayloadSchemaId =
          CrowdBehaviorFixture::PayloadSchemaId;
        Spec.StateSchemaId =
          CrowdBehaviorFixture::StateSchemaId;
        Spec.ReplicationPolicy = Policy;
        Spec.RequiredCapabilityCount = 1;
        Spec.RequiredCapabilities[0] =
          CrowdBehaviorFixture::CapabilityId;
        return Builder.RegisterSource(
          Spec,
          MakeShared<FFixtureEvaluator,
            ESPMode::ThreadSafe>());
      };
      return RegisterSource(
          CrowdBehaviorFixture::SourceTypeId,
          ECrowdBehaviorSourceReplicationPolicy::Predictable)
        && RegisterSource(
          CrowdBehaviorFixture::ResolvedOnlySourceTypeId,
          ECrowdBehaviorSourceReplicationPolicy::ResolvedOnly)
        && RegisterSource(
          CrowdBehaviorFixture::ServerOnlySourceTypeId,
          ECrowdBehaviorSourceReplicationPolicy::ServerOnly);
    }
  };
}

class FMassCrowdBehaviorFixtureModule final : public IModuleInterface
{
public:
  virtual void StartupModule() override
  {
    Provider = MakeShared<FFixtureProvider, ESPMode::ThreadSafe>();
    verify(RegisterCrowdBehaviorSourceProvider(Provider.ToSharedRef()));
  }

private:
  TSharedPtr<const FFixtureProvider, ESPMode::ThreadSafe> Provider;
};

IMPLEMENT_MODULE(
  FMassCrowdBehaviorFixtureModule,
  MassCrowdBehaviorFixture)
