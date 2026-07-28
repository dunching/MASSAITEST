#include "MassCrowdRuntimeBehavior.h"

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

  bool NeedsMovementProfile(const ECrowdActiveBehavior Behavior)
  {
    return Behavior == ECrowdActiveBehavior::Wander
      || Behavior == ECrowdActiveBehavior::MoveTo
      || Behavior == ECrowdActiveBehavior::Pursue
      || Behavior == ECrowdActiveBehavior::HaulPickup
      || Behavior == ECrowdActiveBehavior::HaulDeliver
      || Behavior == ECrowdActiveBehavior::Guard
      || Behavior == ECrowdActiveBehavior::Flee;
  }
}

bool FCrowdBehaviorTarget::IsValid() const
{
  if (Kind >= ECrowdBehaviorTargetKind::Count) return false;
  if (!FMath::IsFinite(Location.X)
    || !FMath::IsFinite(Location.Y)
    || !FMath::IsFinite(Location.Z)) return false;
  if (Kind == ECrowdBehaviorTargetKind::Entity) return EntityRef.IsValid();
  return Kind != ECrowdBehaviorTargetKind::None || EntityRef.IsUnset();
}

bool FCrowdBehaviorObjective::IsValid() const
{
  return FMath::IsFinite(Location.X)
    && FMath::IsFinite(Location.Y)
    && FMath::IsFinite(Location.Z)
    && (ObjectiveKey != 0 || AcceptanceRadiusCm == 0);
}

bool FCrowdInteractionIntent::IsValid() const
{
  return Kind < ECrowdInteractionIntentKind::Count
    && (TargetRef.IsUnset() || TargetRef.IsValid());
}

bool FCrowdBusinessCommitRequest::IsValid() const
{
  if (Kind == ECrowdBusinessCommitKind::None)
  {
    return CommitId == 0 && Quantity == 0;
  }
  return Kind < ECrowdBusinessCommitKind::Count
    && CommitId != 0
    && FixedStepIndex >= 0
    && TransitionRevision != 0
    && AgentRef.IsValid()
    && (TaskRef.IsUnset() || TaskRef.IsValid())
    && (TargetRef.IsUnset() || TargetRef.IsValid())
    && Quantity > 0;
}

bool FCrowdRuntimeBehaviorOutput::IsValidFor(
  const FCrowdRuntimeBehaviorContext& Context) const
{
  return Behavior == Context.RequestedBehavior
    && Target.IsValid()
    && Objective.IsValid()
    && InteractionIntent.IsValid()
    && BusinessCommitRequest.IsValid()
    && (!NeedsMovementProfile(Behavior) || MovementProfileKey != 0);
}

bool FCrowdRuntimeBasicBehaviorProvider::Supports(
  const ECrowdActiveBehavior Behavior) const
{
  return Behavior == ECrowdActiveBehavior::Idle
    || Behavior == ECrowdActiveBehavior::Wander
    || Behavior == ECrowdActiveBehavior::MoveTo
    || Behavior == ECrowdActiveBehavior::Pursue
    || Behavior == ECrowdActiveBehavior::Guard
    || Behavior == ECrowdActiveBehavior::Flee
    || Behavior == ECrowdActiveBehavior::Dead;
}

bool FCrowdRuntimeBasicBehaviorProvider::Evaluate(
  const FCrowdRuntimeBehaviorContext& Context,
  FCrowdRuntimeBehaviorOutput& OutOutput) const
{
  if (!Supports(Context.RequestedBehavior)) return false;
  OutOutput = {};
  OutOutput.Behavior = Context.RequestedBehavior;
  OutOutput.MovementProfileKey = Context.MovementProfileKey;
  OutOutput.Objective = {
    Context.ObjectiveKey,
    Context.TargetLocation,
    Context.RequestedBehavior == ECrowdActiveBehavior::Guard ? 100u : 25u};

  switch (Context.RequestedBehavior)
  {
  case ECrowdActiveBehavior::Idle:
  case ECrowdActiveBehavior::Dead:
    OutOutput.Objective = {};
    return true;
  case ECrowdActiveBehavior::Wander:
  case ECrowdActiveBehavior::MoveTo:
    OutOutput.Target = {ECrowdBehaviorTargetKind::Location, {}, Context.TargetLocation};
    OutOutput.InteractionIntent.Kind = ECrowdInteractionIntentKind::Move;
    return true;
  case ECrowdActiveBehavior::Pursue:
    OutOutput.Target = {
      ECrowdBehaviorTargetKind::Entity, Context.TargetRef, Context.TargetLocation};
    OutOutput.InteractionIntent = {
      ECrowdInteractionIntentKind::Pursue, Context.TargetRef, 0};
    return true;
  case ECrowdActiveBehavior::Guard:
    OutOutput.Target = {ECrowdBehaviorTargetKind::Location, {}, Context.TargetLocation};
    OutOutput.InteractionIntent.Kind = ECrowdInteractionIntentKind::Guard;
    return true;
  case ECrowdActiveBehavior::Flee:
    OutOutput.Target = {
      ECrowdBehaviorTargetKind::Entity, Context.TargetRef, Context.TargetLocation};
    OutOutput.InteractionIntent = {
      ECrowdInteractionIntentKind::Flee, Context.TargetRef, 0};
    return true;
  default:
    return false;
  }
}

bool FCrowdRuntimeBehaviorTransition::Evaluate(
  const ICrowdRuntimeBehaviorProvider& Provider,
  const FCrowdRuntimeBehaviorContext& Context,
  FCrowdRuntimeBehaviorOutput& OutOutput)
{
  OutOutput = {};
  if (!Context.AgentFacts.IsWellFormed()
    || Context.RequestedBehavior >= ECrowdActiveBehavior::Count
    || !Context.AgentFacts.CapabilitySet.CanActivate(Context.RequestedBehavior)
    || Context.FixedStepIndex < 0
    || Context.TransitionRevision == 0
    || !Provider.Supports(Context.RequestedBehavior)
    || !Provider.Evaluate(Context, OutOutput)) return false;
  return OutOutput.IsValidFor(Context);
}

bool FCrowdRuntimeBehaviorTransition::Commit(
  const FCrowdRuntimeBehaviorOutput& Output,
  FCrowdAgentFacts& InOutAgentFacts)
{
  if (!InOutAgentFacts.StableEntityRef.IsValid()
    || !InOutAgentFacts.CapabilitySet.CanActivate(Output.Behavior)) return false;
  InOutAgentFacts.ActiveBehavior = Output.Behavior;
  InOutAgentFacts.TargetRef = Output.Target.Kind == ECrowdBehaviorTargetKind::Entity
    ? Output.Target.EntityRef : FCrowdStableEntityRef{};
  InOutAgentFacts.MovementProfileKey = Output.MovementProfileKey;
  if (Output.BusinessCommitRequest.TaskRef.IsValid())
  {
    InOutAgentFacts.BusinessTaskRef = Output.BusinessCommitRequest.TaskRef;
  }
  return InOutAgentFacts.IsWellFormed();
}

uint64 FCrowdRuntimeBehaviorTransition::MakeCommitId(
  const ECrowdBusinessCommitKind Kind,
  const FCrowdRuntimeBehaviorContext& Context)
{
  if (Context.ExternalCommitId != 0) return Context.ExternalCommitId;
  uint64 Hash = FnvOffset64;
  FoldUnsigned(Hash, static_cast<uint8>(Kind));
  FoldUnsigned(Hash, static_cast<uint64>(Context.FixedStepIndex));
  FoldUnsigned(Hash, Context.TransitionRevision);
  FoldRef(Hash, Context.AgentFacts.StableEntityRef);
  FoldRef(Hash, Context.TaskRef);
  FoldRef(Hash, Context.TargetRef);
  FoldUnsigned(Hash, Context.InteractionPayloadKey);
  FoldUnsigned(Hash, static_cast<uint32>(Context.InteractionQuantity));
  return Hash == 0 ? 1 : Hash;
}
