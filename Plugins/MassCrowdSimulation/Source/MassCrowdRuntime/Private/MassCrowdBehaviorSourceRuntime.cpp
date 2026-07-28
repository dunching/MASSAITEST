#include "MassCrowdBehaviorSourceRuntime.h"

namespace
{
  constexpr uint64 FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 FnvPrime64 = 1099511628211ull;

  template <typename T>
  void FoldUnsigned(uint64& Hash, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 ByteIndex = 0; ByteIndex < sizeof(T); ++ByteIndex)
    {
      Hash ^= static_cast<uint8>(Value >> (ByteIndex * 8));
      Hash *= FnvPrime64;
    }
  }

  void FoldRef(uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    FoldUnsigned(Hash, Ref.ProviderId);
    FoldUnsigned(Hash, Ref.StableEntityId);
    FoldUnsigned(Hash, Ref.LifecycleSerial);
  }

  FCrowdBehaviorSourceSpec MakeSpec(
    const FCrowdBehaviorSourceTypeId TypeId,
    const uint16 ChannelMask,
    const int16 Priority,
    const ECrowdBehaviorSourceReplicationPolicy Replication,
    const FCrowdCapabilityId Required = {},
    const int32 MaxLifetimeSteps = 0,
    const uint16 ExclusiveGroup = 0)
  {
    FCrowdBehaviorSourceSpec Spec;
    Spec.TypeId = TypeId;
    Spec.ChannelMask = ChannelMask;
    Spec.DefaultPriority = Priority;
    Spec.ReplicationPolicy = Replication;
    Spec.PayloadSchemaId = CrowdBuiltinBehaviorSchemas::Standard;
    Spec.MaxLifetimeSteps = MaxLifetimeSteps;
    Spec.ExclusiveGroup = ExclusiveGroup;
    if (Required.IsValid())
    {
      Spec.RequiredCapabilityCount = 1;
      Spec.RequiredCapabilities[0] = Required;
    }
    return Spec;
  }

  bool ReadPayload(
    const FCrowdBehaviorSourceEvaluationContext& Context,
    FCrowdBuiltinBehaviorSourcePayload& OutPayload)
  {
    return Context.Instance.Payload.Get(
      CrowdBuiltinBehaviorSchemas::Standard, OutPayload);
  }

  class FMovementEvaluator final : public ICrowdBehaviorSourceEvaluator
  {
  public:
    explicit FMovementEvaluator(
      const ECrowdBehaviorBlendMode InMode) : Mode(InMode) {}

    virtual bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdBuiltinBehaviorSourcePayload Payload;
      if (!ReadPayload(Context, Payload)) return false;
      FCrowdMovementContribution Value;
      Value.BlendMode = Mode;
      Value.WeightQ15 = Payload.PrimaryId > 0
        ? static_cast<uint16>(FMath::Min<uint32>(
          Payload.PrimaryId, CrowdBehavior::FullQ15Weight))
        : CrowdBehavior::FullQ15Weight;
      Value.DesiredVelocity = Payload.Vector;
      return Writer.AddMovement(Value);
    }

  private:
    ECrowdBehaviorBlendMode Mode;
  };

  class FFacingEvaluator final : public ICrowdBehaviorSourceEvaluator
  {
  public:
    virtual bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdBuiltinBehaviorSourcePayload Payload;
      if (!ReadPayload(Context, Payload)) return false;
      FCrowdFacingContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Override;
      Value.DesiredDirection = Payload.Vector;
      return Writer.AddFacing(Value);
    }
  };

  class FConstraintEvaluator final : public ICrowdBehaviorSourceEvaluator
  {
  public:
    explicit FConstraintEvaluator(const bool bInPermanent)
      : bPermanent(bInPermanent) {}

    virtual bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdBuiltinBehaviorSourcePayload Payload;
      if (!ReadPayload(Context, Payload)) return false;
      FCrowdConstraintContribution Value;
      Value.BlendMode = ECrowdBehaviorBlendMode::Override;
      Value.SpeedLimitCmps = Payload.Vector.X >= 0.0
        ? static_cast<float>(Payload.Vector.X) : 0.0f;
      Value.AllowedNavLayerMask = Payload.CommitId != 0
        ? Payload.CommitId : MAX_uint64;
      Value.bLockMovement = bPermanent || (Payload.Flags & 1u) != 0;
      return Writer.AddConstraint(Value);
    }

  private:
    bool bPermanent = false;
  };

  class FPresentationEvaluator final
    : public ICrowdBehaviorSourceEvaluator
  {
  public:
    virtual bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdBuiltinBehaviorSourcePayload Payload;
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
    virtual bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdBuiltinBehaviorSourcePayload Payload;
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

  class FAttackEvaluator final : public ICrowdBehaviorSourceEvaluator
  {
  public:
    virtual bool Evaluate(
      const FCrowdBehaviorSourceEvaluationContext& Context,
      FCrowdBehaviorContributionWriter& Writer) const override
    {
      FCrowdBuiltinBehaviorSourcePayload Payload;
      if (!ReadPayload(Context, Payload)
        || !Payload.TargetRef.IsValid())
        return false;
      FCrowdInteractionContribution Interaction;
      Interaction.IntentTypeId = Payload.PrimaryId != 0
        ? Payload.PrimaryId : 1;
      Interaction.TargetRef = Payload.TargetRef;
      Interaction.PayloadTypeId = Payload.SecondaryId;
      Interaction.PayloadKey = Payload.Flags;
      if (!Writer.AddInteraction(Interaction)) return false;
      if (Payload.CommitId == 0) return true;
      FCrowdBusinessContribution Business;
      Business.AdapterId = Payload.PrimaryId != 0
        ? Payload.PrimaryId : 1;
      Business.ExclusiveGroup = Context.Instance.ExclusiveGroup;
      Business.CommitId = Payload.CommitId;
      Business.InstigatorRef = Context.Instance.Handle.EntityRef;
      Business.TargetRef = Payload.TargetRef;
      Business.PayloadTypeId = Payload.SecondaryId != 0
        ? Payload.SecondaryId : 1;
      Business.Quantity = Payload.Quantity;
      return Writer.AddBusiness(Business);
    }
  };

  uint64 CalculateBindingUpdateHash(
    const int64 FixedStep,
    const FCrowdStableEntityRef& EntityRef,
    const FCrowdCapabilityBinding& Binding)
  {
    uint64 Hash = FnvOffset64;
    FoldUnsigned(Hash, static_cast<uint64>(FixedStep));
    FoldRef(Hash, EntityRef);
    FoldUnsigned(Hash, Binding.ProfileKey.Value);
    FoldUnsigned(Hash, Binding.ModifierRevision);
    FoldUnsigned(Hash, Binding.ModifierCount);
    for (uint8 Index = 0; Index < Binding.ModifierCount; ++Index)
    {
      FoldUnsigned(Hash, Binding.Modifiers[Index].CapabilityId.Value);
      FoldUnsigned(Hash, static_cast<uint8>(
        Binding.Modifiers[Index].Operation));
    }
    return Hash;
  }

  uint64 CalculatePreparedEntityHash(
    const FCrowdBehaviorPreparedEntity& Entity)
  {
    uint64 Hash = FnvOffset64;
    FoldRef(Hash, Entity.EntityRef);
    FoldUnsigned(Hash, Entity.BaseSourceSetHash);
    FoldUnsigned(Hash, Entity.StagedSourceSet.StableHash);
    FoldUnsigned(Hash, Entity.ResolvedChannels.StableHash);
    FoldUnsigned(Hash, Entity.CommandBatchHash);
    FoldUnsigned(Hash, static_cast<uint32>(Entity.Events.Num()));
    for (const FCrowdBehaviorSourceEvent& Event : Entity.Events)
    {
      FoldUnsigned(Hash, static_cast<uint8>(Event.Kind));
      FoldUnsigned(Hash, static_cast<uint64>(Event.FixedStepIndex));
      FoldUnsigned(Hash, Event.Handle.ControllerId.Value);
      FoldUnsigned(Hash, Event.Handle.SourceSequence);
      FoldUnsigned(Hash, Event.SourceTypeId.Value);
    }
    return Hash;
  }

  uint64 CalculatePreparedBoundaryHash(
    const FCrowdBehaviorPreparedBoundary& Prepared)
  {
    uint64 Hash = FnvOffset64;
    FoldUnsigned(Hash, static_cast<uint64>(Prepared.FixedStepIndex));
    FoldUnsigned(Hash, static_cast<uint32>(Prepared.Entities.Num()));
    FoldUnsigned(Hash, Prepared.SourceSetHash);
    FoldUnsigned(Hash, Prepared.CommandBatchHash);
    FoldUnsigned(Hash, Prepared.ResolvedChannelHash);
    for (const FCrowdBehaviorPreparedEntity& Entity : Prepared.Entities)
      FoldUnsigned(Hash, Entity.StableHash);
    return Hash;
  }
}

FCrowdBehaviorContributionWriter::FCrowdBehaviorContributionWriter(
  const FCrowdBehaviorSourceSpec& InSpec,
  const FCrowdBehaviorSourceInstance& Instance,
  FCrowdBehaviorContributions& OutContributions)
  : Spec(InSpec)
  , Key{
      Instance.Priority,
      Instance.SourceTypeId,
      Instance.Handle.ControllerId,
      Instance.Handle.SourceSequence}
  , Out(OutContributions)
{
  bSucceeded = Spec.IsValid()
    && Instance.IsValid()
    && Spec.TypeId == Instance.SourceTypeId
    && Spec.Version == Instance.SourceVersion;
}

bool FCrowdBehaviorContributionWriter::CanWrite(
  const ECrowdBehaviorChannel Channel,
  const int32 CurrentCount)
{
  bSucceeded = bSucceeded
    && (Spec.ChannelMask & CrowdBehaviorChannelBit(Channel)) != 0
    && CurrentCount < CrowdBehavior::MaxContributionsPerChannel;
  return bSucceeded;
}

bool FCrowdBehaviorContributionWriter::AddMovement(
  FCrowdMovementContribution Contribution)
{
  if (!CanWrite(ECrowdBehaviorChannel::Movement, Out.Movement.Num()))
    return false;
  Contribution.Key = Key;
  Out.Movement.Add(Contribution);
  return true;
}

bool FCrowdBehaviorContributionWriter::AddFacing(
  FCrowdFacingContribution Contribution)
{
  if (!CanWrite(ECrowdBehaviorChannel::Facing, Out.Facing.Num()))
    return false;
  Contribution.Key = Key;
  Out.Facing.Add(Contribution);
  return true;
}

bool FCrowdBehaviorContributionWriter::AddConstraint(
  FCrowdConstraintContribution Contribution)
{
  if (!CanWrite(ECrowdBehaviorChannel::Constraint,
      Out.Constraints.Num()))
    return false;
  Contribution.Key = Key;
  Out.Constraints.Add(Contribution);
  return true;
}

bool FCrowdBehaviorContributionWriter::AddInteraction(
  FCrowdInteractionContribution Contribution)
{
  if (!CanWrite(ECrowdBehaviorChannel::Interaction,
      Out.Interactions.Num()))
    return false;
  Contribution.Key = Key;
  Out.Interactions.Add(Contribution);
  return true;
}

bool FCrowdBehaviorContributionWriter::AddBusiness(
  FCrowdBusinessContribution Contribution)
{
  if (!CanWrite(ECrowdBehaviorChannel::Business, Out.Business.Num()))
    return false;
  Contribution.Key = Key;
  Out.Business.Add(Contribution);
  return true;
}

bool FCrowdBehaviorContributionWriter::AddPresentation(
  FCrowdPresentationContribution Contribution)
{
  if (!CanWrite(ECrowdBehaviorChannel::Presentation,
      Out.Presentation.Num()))
    return false;
  Contribution.Key = Key;
  Out.Presentation.Add(Contribution);
  return true;
}

bool FCrowdBehaviorSourceEvaluatorRegistry::Register(
  const FCrowdBehaviorSourceSpec& Spec,
  TSharedRef<const ICrowdBehaviorSourceEvaluator, ESPMode::ThreadSafe>
    Evaluator)
{
  if (bFrozen || Evaluators.Contains(Spec.TypeId)
    || !Specs.Register(Spec))
    return false;
  Evaluators.Add(Spec.TypeId, Evaluator);
  return true;
}

bool FCrowdBehaviorSourceEvaluatorRegistry::Freeze()
{
  if (bFrozen || Evaluators.IsEmpty() || !Specs.Freeze())
    return false;
  bFrozen = true;
  return true;
}

const FCrowdBehaviorSourceSpec*
FCrowdBehaviorSourceEvaluatorRegistry::FindSpec(
  const FCrowdBehaviorSourceTypeId TypeId) const
{
  return bFrozen ? Specs.Find(TypeId) : nullptr;
}

TSharedPtr<const ICrowdBehaviorSourceEvaluator, ESPMode::ThreadSafe>
FCrowdBehaviorSourceEvaluatorRegistry::FindEvaluator(
  const FCrowdBehaviorSourceTypeId TypeId) const
{
  if (!bFrozen) return nullptr;
  const auto* Found = Evaluators.Find(TypeId);
  return Found ? *Found : nullptr;
}

uint64 FCrowdBehaviorSourceEvaluatorRegistry::CalculateStableHash() const
{
  return bFrozen ? Specs.CalculateStableHash() : 0;
}

bool FCrowdBehaviorCapabilityBindingUpdate::IsValid() const
{
  return EffectiveFixedStep >= 0
    && EntityRef.IsValid()
    && Binding.IsValid()
    && StableHash == CalculateBindingUpdateHash(
      EffectiveFixedStep, EntityRef, Binding);
}

bool FCrowdBehaviorSourceRuntime::InitializeBuiltins()
{
  Reset();
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
  if (!CapabilityProfiles.Register(MoveTemp(Profile))
    || !CapabilityProfiles.Freeze())
    return false;

  const auto Register = [this](
    const FCrowdBehaviorSourceSpec& Spec,
    TSharedRef<const ICrowdBehaviorSourceEvaluator,
      ESPMode::ThreadSafe> Evaluator)
  {
    return Evaluators.Register(Spec, Evaluator);
  };
  if (!Register(MakeSpec(
      CrowdBuiltinSourceTypeIds::MoveToSink,
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Movement),
      30, ECrowdBehaviorSourceReplicationPolicy::Predictable,
      CrowdBuiltinCapabilityIds::MoveTo),
      MakeShared<FMovementEvaluator, ESPMode::ThreadSafe>(
        ECrowdBehaviorBlendMode::Override))
    || !Register(MakeSpec(
      CrowdBuiltinSourceTypeIds::SharedFlow,
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Movement),
      10, ECrowdBehaviorSourceReplicationPolicy::Predictable,
      CrowdBuiltinCapabilityIds::Move),
      MakeShared<FMovementEvaluator, ESPMode::ThreadSafe>(
        ECrowdBehaviorBlendMode::WeightedAdd))
    || !Register(MakeSpec(
      CrowdBuiltinSourceTypeIds::Formation,
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Movement),
      20, ECrowdBehaviorSourceReplicationPolicy::Predictable,
      CrowdBuiltinCapabilityIds::Formation),
      MakeShared<FMovementEvaluator, ESPMode::ThreadSafe>(
        ECrowdBehaviorBlendMode::Additive))
    || !Register(MakeSpec(
      CrowdBuiltinSourceTypeIds::FaceMovement,
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Facing),
      10, ECrowdBehaviorSourceReplicationPolicy::Predictable,
      CrowdBuiltinCapabilityIds::Face),
      MakeShared<FFacingEvaluator, ESPMode::ThreadSafe>())
    || !Register(MakeSpec(
      CrowdBuiltinSourceTypeIds::FaceTarget,
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Facing),
      20, ECrowdBehaviorSourceReplicationPolicy::Predictable,
      CrowdBuiltinCapabilityIds::Face),
      MakeShared<FFacingEvaluator, ESPMode::ThreadSafe>())
    || !Register(MakeSpec(
      CrowdBuiltinSourceTypeIds::CarryCargo,
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Presentation),
      10, ECrowdBehaviorSourceReplicationPolicy::ResolvedOnly,
      CrowdBuiltinCapabilityIds::CarryCargo),
      MakeShared<FPresentationEvaluator, ESPMode::ThreadSafe>())
    || !Register(MakeSpec(
      CrowdBuiltinSourceTypeIds::PickupInteraction,
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Interaction)
        | CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Business),
      40, ECrowdBehaviorSourceReplicationPolicy::ServerOnly,
      CrowdBuiltinCapabilityIds::Haul, 0, 1),
      MakeShared<FInteractionBusinessEvaluator, ESPMode::ThreadSafe>())
    || !Register(MakeSpec(
      CrowdBuiltinSourceTypeIds::DeliverInteraction,
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Interaction)
        | CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Business),
      40, ECrowdBehaviorSourceReplicationPolicy::ServerOnly,
      CrowdBuiltinCapabilityIds::Haul, 0, 1),
      MakeShared<FInteractionBusinessEvaluator, ESPMode::ThreadSafe>())
    || !Register(MakeSpec(
      CrowdBuiltinSourceTypeIds::AttackTarget,
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Interaction)
        | CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Business),
      50, ECrowdBehaviorSourceReplicationPolicy::ServerOnly,
      CrowdBuiltinCapabilityIds::Attack, 0, 2),
      MakeShared<FAttackEvaluator, ESPMode::ThreadSafe>())
    || !Register(MakeSpec(
      CrowdBuiltinSourceTypeIds::HitReaction,
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Constraint),
      100, ECrowdBehaviorSourceReplicationPolicy::ResolvedOnly,
      CrowdBuiltinCapabilityIds::React, 30),
      MakeShared<FConstraintEvaluator, ESPMode::ThreadSafe>(false))
    || !Register(MakeSpec(
      CrowdBuiltinSourceTypeIds::StunConstraint,
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Constraint),
      110, ECrowdBehaviorSourceReplicationPolicy::ResolvedOnly,
      {}, 300),
      MakeShared<FConstraintEvaluator, ESPMode::ThreadSafe>(false))
    || !Register(MakeSpec(
      CrowdBuiltinSourceTypeIds::DeathConstraint,
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Constraint),
      32760, ECrowdBehaviorSourceReplicationPolicy::ResolvedOnly),
      MakeShared<FConstraintEvaluator, ESPMode::ThreadSafe>(true))
    || !Evaluators.Freeze())
    return false;

  bInitialized = true;
  return true;
}

void FCrowdBehaviorSourceRuntime::Reset()
{
  CapabilityProfiles = {};
  Evaluators = {};
  SourceSets.Reset();
  LastResolvedChannels.Reset();
  PendingCommands.Reset();
  PendingBindingUpdates.Reset();
  LastCommittedEvents.Reset();
  bInitialized = false;
}

bool FCrowdBehaviorSourceRuntime::RegisterEntity(
  const FCrowdStableEntityRef EntityRef,
  const FCrowdCapabilityBinding& Binding)
{
  if (!bInitialized || !EntityRef.IsValid()
    || SourceSets.Contains(EntityRef))
    return false;
  FCrowdResolvedCapabilitySet Resolved;
  if (!CapabilityProfiles.Resolve(Binding, Resolved))
    return false;
  FCrowdBehaviorSourceSet Set;
  Set.EntityRef = EntityRef;
  Set.CapabilityBinding = Binding;
  Set.Revision = 1;
  Set.RecalculateStableHash();
  if (!Set.IsValid()) return false;
  SourceSets.Add(EntityRef, MoveTemp(Set));
  return true;
}

bool FCrowdBehaviorSourceRuntime::RemoveEntity(
  const FCrowdStableEntityRef EntityRef)
{
  if (!bInitialized || SourceSets.Remove(EntityRef) != 1)
    return false;
  LastResolvedChannels.Remove(EntityRef);
  PendingCommands.RemoveAll([&](const auto& Command)
  {
    return Command.Handle.EntityRef == EntityRef;
  });
  PendingBindingUpdates.RemoveAll([&](const auto& Update)
  {
    return Update.EntityRef == EntityRef;
  });
  return true;
}

bool FCrowdBehaviorSourceRuntime::QueueCommand(
  const FCrowdBehaviorSourceCommand& Command)
{
  if (!bInitialized || !Command.IsValid()
    || !SourceSets.Contains(Command.Handle.EntityRef)
    || PendingCommands.Num() >= 4096)
    return false;
  PendingCommands.Add(Command);
  return true;
}

bool FCrowdBehaviorSourceRuntime::QueueCapabilityBinding(
  const int64 EffectiveFixedStep,
  const FCrowdStableEntityRef EntityRef,
  const FCrowdCapabilityBinding& Binding)
{
  if (!bInitialized || EffectiveFixedStep < 0
    || !EntityRef.IsValid() || !Binding.IsValid()
    || !SourceSets.Contains(EntityRef)
    || PendingBindingUpdates.Num() >= 1024)
    return false;
  FCrowdBehaviorCapabilityBindingUpdate Update;
  Update.EffectiveFixedStep = EffectiveFixedStep;
  Update.EntityRef = EntityRef;
  Update.Binding = Binding;
  Update.StableHash = CalculateBindingUpdateHash(
    EffectiveFixedStep, EntityRef, Binding);
  PendingBindingUpdates.Add(Update);
  return true;
}

bool FCrowdBehaviorSourceRuntime::PrepareBoundary(
  const int64 FixedStepIndex,
  FCrowdBehaviorPreparedBoundary& OutPrepared) const
{
  OutPrepared = {};
  if (!bInitialized || FixedStepIndex < 0) return false;

  TArray<FCrowdStableEntityRef> EntityRefs;
  SourceSets.GetKeys(EntityRefs);
  EntityRefs.Sort();
  OutPrepared.FixedStepIndex = FixedStepIndex;
  OutPrepared.Entities.Reserve(EntityRefs.Num());
  uint64 SourceSetHash = FnvOffset64;
  uint64 CommandHash = FnvOffset64;
  uint64 ResolvedHash = FnvOffset64;
  TArray<FCrowdBehaviorCapabilityBindingUpdate> DueBindingUpdates;
  for (const FCrowdBehaviorCapabilityBindingUpdate& Update
    : PendingBindingUpdates)
  {
    if (Update.EffectiveFixedStep <= FixedStepIndex)
      DueBindingUpdates.Add(Update);
  }
  DueBindingUpdates.Sort([](
    const FCrowdBehaviorCapabilityBindingUpdate& A,
    const FCrowdBehaviorCapabilityBindingUpdate& B)
  {
    if (A.EffectiveFixedStep != B.EffectiveFixedStep)
      return A.EffectiveFixedStep < B.EffectiveFixedStep;
    if (!(A.EntityRef == B.EntityRef))
      return A.EntityRef < B.EntityRef;
    return A.StableHash < B.StableHash;
  });
  for (int32 Index = 1; Index < DueBindingUpdates.Num(); ++Index)
  {
    const auto& Previous = DueBindingUpdates[Index - 1];
    const auto& Current = DueBindingUpdates[Index];
    if (Previous.EffectiveFixedStep == Current.EffectiveFixedStep
      && Previous.EntityRef == Current.EntityRef
      && Previous.StableHash != Current.StableHash)
      return false;
  }

  for (const FCrowdStableEntityRef EntityRef : EntityRefs)
  {
    const FCrowdBehaviorSourceSet& Current = SourceSets[EntityRef];
    FCrowdBehaviorSourceSet BoundaryBase = Current;
    bool bBindingChanged = false;
    for (const FCrowdBehaviorCapabilityBindingUpdate& Update
      : DueBindingUpdates)
    {
      if (Update.EffectiveFixedStep <= FixedStepIndex
        && Update.EntityRef == EntityRef)
      {
        if (!Update.IsValid()) return false;
        BoundaryBase.CapabilityBinding = Update.Binding;
        bBindingChanged = true;
      }
    }
    if (bBindingChanged) BoundaryBase.RecalculateStableHash();

    FCrowdResolvedCapabilitySet Capabilities;
    if (!CapabilityProfiles.Resolve(
        BoundaryBase.CapabilityBinding, Capabilities))
      return false;
    TArray<FCrowdBehaviorSourceCommand> DueCommands;
    for (const FCrowdBehaviorSourceCommand& Command : PendingCommands)
      if (Command.EffectiveFixedStep <= FixedStepIndex
        && Command.Handle.EntityRef == EntityRef)
        DueCommands.Add(Command);

    FCrowdBehaviorPreparedEntity& Prepared =
      OutPrepared.Entities.AddDefaulted_GetRef();
    Prepared.EntityRef = EntityRef;
    Prepared.BaseSourceSetHash = Current.StableHash;
    if (!FCrowdBehaviorSourceStateMachine::Apply(
        BoundaryBase, DueCommands, FixedStepIndex,
        Evaluators.GetSpecs(), Capabilities,
        Prepared.StagedSourceSet, Prepared.Events,
        Prepared.CommandBatchHash))
      return false;
    if (bBindingChanged
      && Prepared.StagedSourceSet.Revision == Current.Revision)
    {
      ++Prepared.StagedSourceSet.Revision;
      if (Prepared.StagedSourceSet.Revision == 0)
        Prepared.StagedSourceSet.Revision = 1;
      Prepared.StagedSourceSet.RecalculateStableHash();
    }

    FCrowdBehaviorContributions Contributions;
    for (const FCrowdBehaviorSourceInstance& Instance
      : Prepared.StagedSourceSet.Instances)
    {
      const FCrowdBehaviorSourceSpec* Spec =
        Evaluators.FindSpec(Instance.SourceTypeId);
      const auto Evaluator =
        Evaluators.FindEvaluator(Instance.SourceTypeId);
      if (!Spec || !Evaluator.IsValid()) return false;
      FCrowdBehaviorSourceEvaluationContext Context;
      Context.FixedStepIndex = FixedStepIndex;
      Context.Capabilities = Capabilities;
      Context.Instance = Instance;
      FCrowdBehaviorContributionWriter Writer(
        *Spec, Instance, Contributions);
      if (!Evaluator->Evaluate(Context, Writer)
        || !Writer.Succeeded())
        return false;
    }
    if (!FCrowdBehaviorResolver::Resolve(
        Contributions, Prepared.ResolvedChannels))
      return false;
    Prepared.StableHash = CalculatePreparedEntityHash(Prepared);
    if (Prepared.StableHash == 0) return false;
    FoldUnsigned(SourceSetHash, Prepared.StagedSourceSet.StableHash);
    FoldUnsigned(CommandHash, Prepared.CommandBatchHash);
    FoldUnsigned(ResolvedHash, Prepared.ResolvedChannels.StableHash);
  }

  for (const FCrowdBehaviorSourceCommand& Command : PendingCommands)
    if (Command.EffectiveFixedStep <= FixedStepIndex
      && !SourceSets.Contains(Command.Handle.EntityRef))
      return false;
  for (const FCrowdBehaviorCapabilityBindingUpdate& Update
    : PendingBindingUpdates)
    if (Update.EffectiveFixedStep <= FixedStepIndex
      && !SourceSets.Contains(Update.EntityRef))
      return false;

  OutPrepared.SourceSetHash = SourceSetHash;
  OutPrepared.CommandBatchHash = CommandHash;
  OutPrepared.ResolvedChannelHash = ResolvedHash;
  OutPrepared.StableHash =
    CalculatePreparedBoundaryHash(OutPrepared);
  OutPrepared.bValid = OutPrepared.StableHash != 0;
  return OutPrepared.bValid;
}

bool FCrowdBehaviorSourceRuntime::CommitPrepared(
  const FCrowdBehaviorPreparedBoundary& Prepared)
{
  if (!ValidatePrepared(Prepared)) return false;
  LastCommittedEvents.Reset();
  for (const FCrowdBehaviorPreparedEntity& Entity : Prepared.Entities)
  {
    SourceSets[Entity.EntityRef] = Entity.StagedSourceSet;
    LastResolvedChannels.Add(
      Entity.EntityRef, Entity.ResolvedChannels);
    LastCommittedEvents.Append(Entity.Events);
  }
  PendingCommands.RemoveAll([&](const auto& Command)
  {
    return Command.EffectiveFixedStep <= Prepared.FixedStepIndex;
  });
  PendingBindingUpdates.RemoveAll([&](const auto& Update)
  {
    return Update.EffectiveFixedStep <= Prepared.FixedStepIndex;
  });
  return true;
}

bool FCrowdBehaviorSourceRuntime::ValidatePrepared(
  const FCrowdBehaviorPreparedBoundary& Prepared) const
{
  if (!bInitialized || !Prepared.bValid
    || Prepared.FixedStepIndex < 0
    || Prepared.Entities.Num() != SourceSets.Num()
    || Prepared.StableHash != CalculatePreparedBoundaryHash(Prepared))
    return false;
  uint64 SourceSetHash = FnvOffset64;
  uint64 CommandHash = FnvOffset64;
  uint64 ResolvedHash = FnvOffset64;
  FCrowdStableEntityRef PreviousRef;
  for (const FCrowdBehaviorPreparedEntity& Entity : Prepared.Entities)
  {
    if (!PreviousRef.IsUnset() && !(PreviousRef < Entity.EntityRef))
      return false;
    const FCrowdBehaviorSourceSet* Current =
      SourceSets.Find(Entity.EntityRef);
    if (!Current
      || Current->StableHash != Entity.BaseSourceSetHash
      || !Entity.StagedSourceSet.IsValid()
      || !Entity.ResolvedChannels.bValid
      || Entity.StableHash != CalculatePreparedEntityHash(Entity))
      return false;
    FoldUnsigned(SourceSetHash, Entity.StagedSourceSet.StableHash);
    FoldUnsigned(CommandHash, Entity.CommandBatchHash);
    FoldUnsigned(ResolvedHash, Entity.ResolvedChannels.StableHash);
    PreviousRef = Entity.EntityRef;
  }
  return Prepared.SourceSetHash == SourceSetHash
    && Prepared.CommandBatchHash == CommandHash
    && Prepared.ResolvedChannelHash == ResolvedHash;
}

void FCrowdBehaviorSourceRuntime::RollbackPendingCommandsTo(
  const int32 Count)
{
  check(Count >= 0 && Count <= PendingCommands.Num());
  if (Count < PendingCommands.Num())
    PendingCommands.SetNum(Count, EAllowShrinking::No);
}

const FCrowdBehaviorSourceSet*
FCrowdBehaviorSourceRuntime::FindSourceSet(
  const FCrowdStableEntityRef EntityRef) const
{
  return SourceSets.Find(EntityRef);
}

const FCrowdResolvedBehaviorChannels*
FCrowdBehaviorSourceRuntime::FindResolvedChannels(
  const FCrowdStableEntityRef EntityRef) const
{
  return LastResolvedChannels.Find(EntityRef);
}

bool FCrowdBehaviorSourceRuntime::IsSourceActive(
  const FCrowdBehaviorSourceHandle& Handle) const
{
  const FCrowdBehaviorSourceSet* Set =
    SourceSets.Find(Handle.EntityRef);
  return Set && Set->Instances.ContainsByPredicate(
    [&](const FCrowdBehaviorSourceInstance& Instance)
    {
      return Instance.Handle == Handle;
    });
}

bool FCrowdBehaviorSourceRuntime::HasCommittedEvent(
  const FCrowdBehaviorSourceHandle& Handle,
  const ECrowdBehaviorSourceEventKind Kind,
  const int64 MinimumFixedStep) const
{
  return LastCommittedEvents.ContainsByPredicate(
    [&](const FCrowdBehaviorSourceEvent& Event)
    {
      return Event.Handle == Handle
        && Event.Kind == Kind
        && Event.FixedStepIndex >= MinimumFixedStep;
    });
}

FCrowdBehaviorSourceTypeId
FCrowdLegacyBehaviorRecipe::GetPrimarySourceType(
  const ECrowdActiveBehavior Behavior)
{
  switch (Behavior)
  {
  case ECrowdActiveBehavior::Wander:
  case ECrowdActiveBehavior::MoveTo:
  case ECrowdActiveBehavior::Pursue:
  case ECrowdActiveBehavior::Guard:
  case ECrowdActiveBehavior::Flee:
    return CrowdBuiltinSourceTypeIds::MoveToSink;
  case ECrowdActiveBehavior::HaulPickup:
    return CrowdBuiltinSourceTypeIds::PickupInteraction;
  case ECrowdActiveBehavior::HaulDeliver:
    return CrowdBuiltinSourceTypeIds::DeliverInteraction;
  case ECrowdActiveBehavior::Attack:
    return CrowdBuiltinSourceTypeIds::AttackTarget;
  case ECrowdActiveBehavior::Dead:
    return CrowdBuiltinSourceTypeIds::DeathConstraint;
  default:
    return {};
  }
}

bool FCrowdLegacyBehaviorRecipe::BuildTransitionCommands(
  const FCrowdRuntimeBehaviorContext& Context,
  const FCrowdBehaviorSourceSet& CurrentSet,
  const FCrowdBehaviorControllerId ControllerId,
  uint32& InOutNextCommandSequence,
  uint32& InOutNextSourceSequence,
  TArray<FCrowdBehaviorSourceCommand>& OutCommands)
{
  OutCommands.Reset();
  if (!Context.AgentFacts.StableEntityRef.IsValid()
    || Context.AgentFacts.StableEntityRef != CurrentSet.EntityRef
    || !ControllerId.IsValid()
    || InOutNextCommandSequence == 0
    || InOutNextSourceSequence == 0
    || Context.FixedStepIndex < 0)
    return false;

  for (const FCrowdBehaviorSourceInstance& Instance
    : CurrentSet.Instances)
  {
    if (Instance.Handle.ControllerId != ControllerId) continue;
    FCrowdBehaviorSourceCommand Stop;
    Stop.EffectiveFixedStep = Context.FixedStepIndex;
    Stop.Handle = Instance.Handle;
    Stop.CommandSequence = InOutNextCommandSequence++;
    Stop.Kind = ECrowdBehaviorSourceCommandKind::Stop;
    Stop.SourceTypeId = Instance.SourceTypeId;
    Stop.Payload = Instance.Payload;
    OutCommands.Add(Stop);
  }

  if (Context.RequestedBehavior == ECrowdActiveBehavior::Idle)
    return true;

  FCrowdBuiltinBehaviorSourcePayload Payload;
  Payload.Vector = Context.TargetLocation;
  Payload.TargetRef = Context.TargetRef;
  Payload.CommitId = Context.ExternalCommitId;
  Payload.PrimaryId = static_cast<uint32>(
    Context.InteractionPayloadKey != 0
      ? Context.InteractionPayloadKey : 1);
  Payload.SecondaryId = Context.InteractionPayloadKey != 0
    ? Context.InteractionPayloadKey : 1;
  Payload.Quantity = FMath::Max(1, Context.InteractionQuantity);
  Payload.Flags =
    Context.RequestedBehavior == ECrowdActiveBehavior::Dead ? 1u : 0u;

  const auto AddStart = [&](
    const FCrowdBehaviorSourceTypeId TypeId,
    const FCrowdBuiltinBehaviorSourcePayload& CommandPayload,
    const int32 Lifetime = 0)
  {
    FCrowdBehaviorSourceCommand Start;
    Start.EffectiveFixedStep = Context.FixedStepIndex;
    Start.Handle = {
      CurrentSet.EntityRef, ControllerId, InOutNextSourceSequence++};
    Start.CommandSequence = InOutNextCommandSequence++;
    Start.Kind = ECrowdBehaviorSourceCommandKind::Start;
    Start.SourceTypeId = TypeId;
    Start.LifetimeSteps = Lifetime;
    if (!Start.Payload.Set(
        CrowdBuiltinBehaviorSchemas::Standard, CommandPayload))
      return false;
    OutCommands.Add(Start);
    return true;
  };

  if (Context.RequestedBehavior == ECrowdActiveBehavior::Dead)
  {
    Payload.Vector.X = 0.0;
    Payload.Flags |= 1u;
    return AddStart(
      CrowdBuiltinSourceTypeIds::DeathConstraint, Payload);
  }

  FCrowdBuiltinBehaviorSourcePayload MovementPayload = Payload;
  MovementPayload.CommitId = 0;
  MovementPayload.PrimaryId = CrowdBehavior::FullQ15Weight;
  MovementPayload.SecondaryId = 0;
  MovementPayload.Quantity = 0;
  MovementPayload.Flags = 0;
  if (!AddStart(
      CrowdBuiltinSourceTypeIds::MoveToSink, MovementPayload)
    || !AddStart(
      Context.TargetRef.IsValid()
        ? CrowdBuiltinSourceTypeIds::FaceTarget
        : CrowdBuiltinSourceTypeIds::FaceMovement,
      MovementPayload))
    return false;

  const FCrowdBehaviorSourceTypeId PrimaryType =
    GetPrimarySourceType(Context.RequestedBehavior);
  if (PrimaryType == CrowdBuiltinSourceTypeIds::PickupInteraction
    || PrimaryType == CrowdBuiltinSourceTypeIds::DeliverInteraction
    || PrimaryType == CrowdBuiltinSourceTypeIds::AttackTarget)
  {
    if (PrimaryType != CrowdBuiltinSourceTypeIds::AttackTarget)
      Payload.TargetRef = Context.TaskRef;
    if (!AddStart(PrimaryType, Payload)) return false;
  }
  return true;
}
