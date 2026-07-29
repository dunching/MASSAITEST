#include "CrowdDemoBehaviorSourceProvider.h"

namespace
{
  FCrowdBehaviorSourceSpec MakeSpec(
    const FCrowdBehaviorSourceTypeId TypeId,
    const uint16 ChannelMask,
    const int16 Priority,
    const ECrowdBehaviorSourceReplicationPolicy Replication,
    const FCrowdCapabilityId Required,
    const uint16 ExclusiveGroup = 0)
  {
    FCrowdBehaviorSourceSpec Spec;
    Spec.TypeId = TypeId;
    Spec.Version = 1;
    Spec.ChannelMask = ChannelMask;
    Spec.DefaultPriority = Priority;
    Spec.ExclusiveGroup = ExclusiveGroup;
    Spec.PayloadSchemaId = CrowdDemoBehaviorSchemas::Standard;
    Spec.ReplicationPolicy = Replication;
    Spec.RequiredCapabilityCount = 1;
    Spec.RequiredCapabilities[0] = Required;
    return Spec;
  }

  bool ReadPayload(
    const FCrowdBehaviorSourceEvaluationContext& Context,
    FCrowdDemoBehaviorSourcePayload& OutPayload)
  {
    return Context.Instance.Payload.Get(
      CrowdDemoBehaviorSchemas::Standard, OutPayload);
  }

  class FSharedFlowEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdDemoBehaviorSourcePayload Payload;
      if (!ReadPayload(Context, Payload)
        || Payload.Vector.ContainsNaN())
        return false;
      FCrowdMovementContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::WeightedAdd;
      Value.WeightQ15 = Payload.PrimaryId > 0
        ? static_cast<uint16>(FMath::Min<uint32>(
          Payload.PrimaryId, CrowdBehavior::FullQ15Weight))
        : CrowdBehavior::FullQ15Weight;
      Value.DesiredVelocity = Payload.Vector;
      return Writer.AddMovement(Value);
    }
  };

  class FCarryCargoEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdDemoBehaviorSourcePayload Payload;
      if (!ReadPayload(Context, Payload)
        || Payload.PrimaryId == 0)
        return false;
      FCrowdPresentationContribution Value;
      Value.PropertyId = Payload.PrimaryId;
      Value.Value = Payload.SecondaryId;
      return Writer.AddPresentation(Value);
    }
  };

  class FInteractionBusinessEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdDemoBehaviorSourcePayload Payload;
      if (!ReadPayload(Context, Payload)
        || Payload.PrimaryId == 0)
        return false;
      FCrowdInteractionContribution Interaction;
      Interaction.IntentTypeId = Payload.PrimaryId;
      Interaction.TargetRef = Payload.TargetRef;
      Interaction.PayloadTypeId = Payload.SecondaryId;
      Interaction.PayloadKey = Payload.Flags;
      if (!Writer.AddInteraction(Interaction)) return false;
      if (Payload.CommitId == 0) return true;
      FCrowdBusinessContribution Business;
      Business.AdapterId = Payload.PrimaryId;
      Business.ExclusiveGroup = Context.Instance.ExclusiveGroup;
      Business.CommitId = Payload.CommitId;
      Business.InstigatorRef = Context.Instance.Handle.EntityRef;
      Business.TargetRef = Payload.TargetRef;
      Business.PayloadTypeId = Payload.SecondaryId;
      Business.Quantity = Payload.Quantity;
      return Writer.AddBusiness(Business);
    }
  };

  class FCrowdDemoBehaviorProvider final
    : public ICrowdBehaviorSourceProvider
  {
  public:
    FCrowdBehaviorProviderId GetProviderId() const override
    {
      return CrowdDemoBehaviorSchemas::Provider;
    }

    bool Register(
      FCrowdBehaviorRegistryBuilder& Builder) const override
    {
      FCrowdCapabilityProfile Profile;
      Profile.Key = CrowdDemoBehaviorSchemas::FullProfile;
      Profile.CapabilityIds = {
        CrowdDemoCapabilityIds::Haul,
        CrowdDemoCapabilityIds::Attack,
        CrowdDemoCapabilityIds::RangedAttack,
        CrowdDemoCapabilityIds::NavLayer,
        CrowdDemoCapabilityIds::CarryCargo,
        CrowdStandardSources::MoveCapability,
        CrowdStandardSources::FaceCapability,
        CrowdStandardSources::FormationCapability,
        CrowdStandardSources::ImpulseCapability};
      Profile.CapabilityIds.Sort();
      if (!Builder.RegisterProfile(MoveTemp(Profile)))
        return false;

      const auto Register = [&](
        const FCrowdBehaviorSourceSpec& Spec,
        TSharedRef<const ICrowdBehaviorSourceEvaluator,
          ESPMode::ThreadSafe> Evaluator)
      {
        return Builder.RegisterSource(Spec, Evaluator);
      };
      if (!Register(
          MakeSpec(
            CrowdDemoSourceTypeIds::SharedFlow,
            CrowdBehaviorChannelBit(
              ECrowdBehaviorChannel::Movement),
            50,
            ECrowdBehaviorSourceReplicationPolicy::Predictable,
            CrowdStandardSources::MoveCapability),
          MakeShared<FSharedFlowEvaluator, ESPMode::ThreadSafe>()))
        return false;
      if (!Register(
          MakeSpec(
            CrowdDemoSourceTypeIds::CarryCargo,
            CrowdBehaviorChannelBit(
              ECrowdBehaviorChannel::Presentation),
            100,
            ECrowdBehaviorSourceReplicationPolicy::ResolvedOnly,
            CrowdDemoCapabilityIds::CarryCargo),
          MakeShared<FCarryCargoEvaluator, ESPMode::ThreadSafe>()))
        return false;
      const uint16 InteractionBusinessMask =
        CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Interaction)
        | CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Business);
      return Register(
          MakeSpec(
            CrowdDemoSourceTypeIds::PickupInteraction,
            InteractionBusinessMask, 100,
            ECrowdBehaviorSourceReplicationPolicy::ServerOnly,
            CrowdDemoCapabilityIds::Haul, 1),
          MakeShared<FInteractionBusinessEvaluator,
            ESPMode::ThreadSafe>())
        && Register(
          MakeSpec(
            CrowdDemoSourceTypeIds::DeliverInteraction,
            InteractionBusinessMask, 100,
            ECrowdBehaviorSourceReplicationPolicy::ServerOnly,
            CrowdDemoCapabilityIds::Haul, 1),
          MakeShared<FInteractionBusinessEvaluator,
            ESPMode::ThreadSafe>())
        && Register(
          MakeSpec(
            CrowdDemoSourceTypeIds::AttackTarget,
            InteractionBusinessMask, 200,
            ECrowdBehaviorSourceReplicationPolicy::ServerOnly,
            CrowdDemoCapabilityIds::Attack, 2),
          MakeShared<FInteractionBusinessEvaluator,
            ESPMode::ThreadSafe>());
    }
  };

  const FCrowdBehaviorControllerCursor* FindCursor(
    const FCrowdBehaviorSourceSet& Set,
    const FCrowdBehaviorControllerId ControllerId)
  {
    return Set.ControllerCursors.FindByPredicate(
      [&](const FCrowdBehaviorControllerCursor& Cursor)
      {
        return Cursor.ControllerId == ControllerId;
      });
  }
}

TSharedRef<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>
CreateCrowdDemoBehaviorSourceProvider()
{
  return MakeShared<FCrowdDemoBehaviorProvider, ESPMode::ThreadSafe>();
}

bool FCrowdDemoSourceSetDiff::BuildDesiredSourceDiff(
  const int64 EffectiveFixedStep,
  const FCrowdBehaviorSourceSet& CurrentSet,
  const TConstArrayView<FCrowdDemoDesiredSource> DesiredSources,
  TArray<FCrowdBehaviorSourceCommand>& OutCommands)
{
  OutCommands.Reset();
  if (EffectiveFixedStep < 0
    || !CurrentSet.EntityRef.IsValid()
    || DesiredSources.Num() > CrowdBehavior::MaxSourcesPerEntity)
    return false;

  TArray<FCrowdDemoDesiredSource, TInlineAllocator<16>>
    Desired(DesiredSources);
  Desired.Sort([](
    const FCrowdDemoDesiredSource& A,
    const FCrowdDemoDesiredSource& B)
  {
    if (A.ControllerId != B.ControllerId)
      return A.ControllerId < B.ControllerId;
    return A.SourceSequence < B.SourceSequence;
  });
  for (int32 Index = 0; Index < Desired.Num(); ++Index)
  {
    if (!Desired[Index].IsValid()
      || (Index > 0
        && Desired[Index - 1].ControllerId
          == Desired[Index].ControllerId
        && Desired[Index - 1].SourceSequence
          == Desired[Index].SourceSequence))
      return false;
  }

  TMap<FCrowdBehaviorControllerId, uint32> NextSequences;
  auto NextCommandSequence =
    [&](const FCrowdBehaviorControllerId ControllerId) mutable
  {
    uint32& Next = NextSequences.FindOrAdd(ControllerId);
    if (Next == 0)
    {
      const FCrowdBehaviorControllerCursor* Cursor =
        FindCursor(CurrentSet, ControllerId);
      Next = Cursor ? Cursor->LastCommandSequence + 1 : 1;
    }
    return Next++;
  };
  const auto FindDesired =
    [&](const FCrowdBehaviorSourceHandle& Handle)
      -> const FCrowdDemoDesiredSource*
  {
    return Desired.FindByPredicate(
      [&](const FCrowdDemoDesiredSource& Entry)
      {
        return Entry.ControllerId == Handle.ControllerId
          && Entry.SourceSequence == Handle.SourceSequence;
      });
  };

  TArray<FCrowdBehaviorSourceInstance> Current =
    CurrentSet.Instances;
  Current.Sort([](
    const FCrowdBehaviorSourceInstance& A,
    const FCrowdBehaviorSourceInstance& B)
  {
    return A.Handle < B.Handle;
  });
  for (const FCrowdBehaviorSourceInstance& Instance : Current)
  {
    const FCrowdDemoDesiredSource* Entry =
      FindDesired(Instance.Handle);
    if (Entry
      && Entry->SourceTypeId == Instance.SourceTypeId)
      continue;
    FCrowdBehaviorSourceCommand Stop;
    Stop.EffectiveFixedStep = EffectiveFixedStep;
    Stop.Handle = Instance.Handle;
    Stop.CommandSequence =
      NextCommandSequence(Instance.Handle.ControllerId);
    Stop.Kind = ECrowdBehaviorSourceCommandKind::Stop;
    Stop.SourceTypeId = Instance.SourceTypeId;
    Stop.Payload = Instance.Payload;
    OutCommands.Add(Stop);
  }
  for (const FCrowdDemoDesiredSource& Entry : Desired)
  {
    const FCrowdBehaviorSourceHandle Handle = {
      CurrentSet.EntityRef,
      Entry.ControllerId,
      Entry.SourceSequence};
    const FCrowdBehaviorSourceInstance* Existing =
      CurrentSet.Instances.FindByPredicate(
        [&](const FCrowdBehaviorSourceInstance& Instance)
        {
          return Instance.Handle == Handle
            && Instance.SourceTypeId == Entry.SourceTypeId;
        });
    if (Existing
      && Existing->Priority
        == (Entry.Priority != 0
          ? Entry.Priority : Existing->Priority)
      && Existing->Payload == Entry.Payload
      && (Entry.LifetimeSteps == 0
        || Existing->ExpireFixedStep
          == EffectiveFixedStep + Entry.LifetimeSteps))
      continue;
    FCrowdBehaviorSourceCommand Command;
    Command.EffectiveFixedStep = EffectiveFixedStep;
    Command.Handle = Handle;
    Command.CommandSequence =
      NextCommandSequence(Entry.ControllerId);
    Command.Kind = Existing
      ? ECrowdBehaviorSourceCommandKind::Update
      : ECrowdBehaviorSourceCommandKind::Start;
    Command.SourceTypeId = Entry.SourceTypeId;
    Command.Priority = Entry.Priority;
    Command.LifetimeSteps = Entry.LifetimeSteps;
    Command.Payload = Entry.Payload;
    OutCommands.Add(Command);
  }
  return true;
}
