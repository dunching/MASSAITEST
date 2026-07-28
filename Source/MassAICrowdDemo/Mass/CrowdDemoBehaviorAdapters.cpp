#include "Mass/CrowdDemoBehaviorAdapters.h"

bool FCrowdDemoLogisticsBehaviorAdapter::Supports(
  const ECrowdActiveBehavior Behavior) const
{
  return Behavior == ECrowdActiveBehavior::HaulPickup
    || Behavior == ECrowdActiveBehavior::HaulDeliver;
}

bool FCrowdDemoLogisticsBehaviorAdapter::Evaluate(
  const FCrowdRuntimeBehaviorContext& Context,
  FCrowdRuntimeBehaviorOutput& OutOutput) const
{
  if (!Supports(Context.RequestedBehavior)
    || !Context.TaskRef.IsValid()
    || Context.MovementProfileKey == 0) return false;

  OutOutput = {};
  OutOutput.Behavior = Context.RequestedBehavior;
  OutOutput.MovementProfileKey = Context.MovementProfileKey;
  OutOutput.Objective = {
    Context.ObjectiveKey, Context.TargetLocation, 50};

  const bool bPickup = Context.RequestedBehavior == ECrowdActiveBehavior::HaulPickup;
  OutOutput.Target = bPickup || !Context.TargetRef.IsValid()
    ? FCrowdBehaviorTarget{
      bPickup ? ECrowdBehaviorTargetKind::Entity : ECrowdBehaviorTargetKind::Location,
      bPickup ? Context.TaskRef : FCrowdStableEntityRef{},
      Context.TargetLocation}
    : FCrowdBehaviorTarget{
      ECrowdBehaviorTargetKind::Entity, Context.TargetRef, Context.TargetLocation};
  OutOutput.InteractionIntent = {
    bPickup ? ECrowdInteractionIntentKind::Pickup
      : ECrowdInteractionIntentKind::Deliver,
    Context.TaskRef,
    Context.InteractionPayloadKey};

  if (Context.bInteractionReady)
  {
    OutOutput.BusinessCommitRequest.Kind = bPickup
      ? ECrowdBusinessCommitKind::CargoPickup
      : ECrowdBusinessCommitKind::CargoDeliver;
    OutOutput.BusinessCommitRequest.CommitId =
      FCrowdRuntimeBehaviorTransition::MakeCommitId(
        OutOutput.BusinessCommitRequest.Kind, Context);
    OutOutput.BusinessCommitRequest.FixedStepIndex = Context.FixedStepIndex;
    OutOutput.BusinessCommitRequest.TransitionRevision = Context.TransitionRevision;
    OutOutput.BusinessCommitRequest.AgentRef = Context.AgentFacts.StableEntityRef;
    OutOutput.BusinessCommitRequest.TaskRef = Context.TaskRef;
    OutOutput.BusinessCommitRequest.TargetRef = Context.TargetRef;
    OutOutput.BusinessCommitRequest.PayloadKey = Context.InteractionPayloadKey;
    OutOutput.BusinessCommitRequest.Quantity = FMath::Max(1, Context.InteractionQuantity);
  }
  return true;
}

bool FCrowdDemoCombatBehaviorAdapter::Supports(
  const ECrowdActiveBehavior Behavior) const
{
  return Behavior == ECrowdActiveBehavior::Attack;
}

bool FCrowdDemoCombatBehaviorAdapter::Evaluate(
  const FCrowdRuntimeBehaviorContext& Context,
  FCrowdRuntimeBehaviorOutput& OutOutput) const
{
  if (!Supports(Context.RequestedBehavior) || !Context.TargetRef.IsValid()) return false;
  OutOutput = {};
  OutOutput.Behavior = ECrowdActiveBehavior::Attack;
  OutOutput.Target = {
    ECrowdBehaviorTargetKind::Entity, Context.TargetRef, Context.TargetLocation};
  OutOutput.Objective = {
    Context.ObjectiveKey, Context.TargetLocation, 0};
  OutOutput.MovementProfileKey = Context.MovementProfileKey;
  OutOutput.InteractionIntent = {
    ECrowdInteractionIntentKind::Attack,
    Context.TargetRef,
    Context.InteractionPayloadKey};
  if (Context.bInteractionReady)
  {
    OutOutput.BusinessCommitRequest.Kind = ECrowdBusinessCommitKind::CombatHit;
    OutOutput.BusinessCommitRequest.CommitId =
      FCrowdRuntimeBehaviorTransition::MakeCommitId(
        ECrowdBusinessCommitKind::CombatHit, Context);
    OutOutput.BusinessCommitRequest.FixedStepIndex = Context.FixedStepIndex;
    OutOutput.BusinessCommitRequest.TransitionRevision = Context.TransitionRevision;
    OutOutput.BusinessCommitRequest.AgentRef = Context.AgentFacts.StableEntityRef;
    OutOutput.BusinessCommitRequest.TaskRef = Context.TaskRef;
    OutOutput.BusinessCommitRequest.TargetRef = Context.TargetRef;
    OutOutput.BusinessCommitRequest.PayloadKey = Context.InteractionPayloadKey;
    OutOutput.BusinessCommitRequest.Quantity = FMath::Max(1, Context.InteractionQuantity);
  }
  return true;
}

bool FCrowdDemoCombatBehaviorAdapter::BuildContextFromHitFact(
  const FCrowdDemoHitFact& HitFact,
  const FCrowdAgentFacts& SourceFacts,
  const uint32 TransitionRevision,
  FCrowdRuntimeBehaviorContext& OutContext) const
{
  OutContext = {};
  if (HitFact.HitEventId == 0
    || HitFact.ApplyFixedStep < 0
    || HitFact.SourceAgentId <= 0
    || HitFact.SourceLifecycleSerial <= 0
    || HitFact.TargetAgentId <= 0
    || HitFact.TargetLifecycleSerial <= 0
    || HitFact.Damage <= 0.0f
    || TransitionRevision == 0
    || !SourceFacts.StableEntityRef.IsValid()
    || SourceFacts.StableEntityRef.StableEntityId
      != static_cast<uint64>(HitFact.SourceAgentId)
    || SourceFacts.StableEntityRef.LifecycleSerial
      != static_cast<uint32>(HitFact.SourceLifecycleSerial)) return false;

  OutContext.AgentFacts = SourceFacts;
  OutContext.RequestedBehavior = ECrowdActiveBehavior::Attack;
  OutContext.FixedStepIndex = HitFact.ApplyFixedStep;
  OutContext.TransitionRevision = TransitionRevision;
  OutContext.TargetRef = {
    SourceFacts.StableEntityRef.ProviderId,
    static_cast<uint64>(HitFact.TargetAgentId),
    static_cast<uint32>(HitFact.TargetLifecycleSerial)};
  OutContext.TargetLocation = HitFact.HitPosition;
  OutContext.ObjectiveKey = HitFact.HitFlashProfileKey;
  OutContext.MovementProfileKey = SourceFacts.MovementProfileKey;
  OutContext.InteractionPayloadKey = HitFact.HitFlashProfileKey;
  OutContext.InteractionQuantity = FMath::Max(1, FMath::RoundToInt(HitFact.Damage));
  OutContext.ExternalCommitId = HitFact.HitEventId;
  OutContext.bInteractionReady = true;
  return true;
}

bool FCrowdDemoBehaviorProviderSet::Supports(
  const ECrowdActiveBehavior Behavior) const
{
  return Basic.Supports(Behavior)
    || Logistics.Supports(Behavior)
    || Combat.Supports(Behavior);
}

bool FCrowdDemoBehaviorProviderSet::Evaluate(
  const FCrowdRuntimeBehaviorContext& Context,
  FCrowdRuntimeBehaviorOutput& OutOutput) const
{
  if (Logistics.Supports(Context.RequestedBehavior))
    return Logistics.Evaluate(Context, OutOutput);
  if (Combat.Supports(Context.RequestedBehavior))
    return Combat.Evaluate(Context, OutOutput);
  return Basic.Evaluate(Context, OutOutput);
}

ECrowdDemoBusinessCommitAcceptResult FCrowdDemoBusinessCommitLedger::Apply(
  const FCrowdBusinessCommitRequest& Request)
{
  if (!Request.IsValid() || Request.Kind == ECrowdBusinessCommitKind::None)
    return ECrowdDemoBusinessCommitAcceptResult::Rejected;
  if (AppliedCommitIds.Contains(Request.CommitId))
    return ECrowdDemoBusinessCommitAcceptResult::Duplicate;

  switch (Request.Kind)
  {
  case ECrowdBusinessCommitKind::CargoPickup:
    {
      if (!Request.TaskRef.IsValid()) return ECrowdDemoBusinessCommitAcceptResult::Rejected;
      const uint64* ExistingCarrier =
        CargoCarrierByTask.Find(Request.TaskRef.StableEntityId);
      if (ExistingCarrier && *ExistingCarrier != 0)
        return ECrowdDemoBusinessCommitAcceptResult::Rejected;
      CargoCarrierByTask.Add(
        Request.TaskRef.StableEntityId, Request.AgentRef.StableEntityId);
      ++PickupCount;
      break;
    }
  case ECrowdBusinessCommitKind::CargoDeliver:
    {
      if (!Request.TaskRef.IsValid()) return ECrowdDemoBusinessCommitAcceptResult::Rejected;
      const uint64* ExistingCarrier =
        CargoCarrierByTask.Find(Request.TaskRef.StableEntityId);
      if (!ExistingCarrier || *ExistingCarrier != Request.AgentRef.StableEntityId)
        return ECrowdDemoBusinessCommitAcceptResult::Rejected;
      CargoCarrierByTask.Add(Request.TaskRef.StableEntityId, 0);
      ++DeliveryCount;
      break;
    }
  case ECrowdBusinessCommitKind::CombatHit:
    if (!Request.TargetRef.IsValid())
      return ECrowdDemoBusinessCommitAcceptResult::Rejected;
    CombatHitQuantityByTarget.FindOrAdd(Request.TargetRef.StableEntityId) += Request.Quantity;
    break;
  default:
    return ECrowdDemoBusinessCommitAcceptResult::Rejected;
  }
  AppliedCommitIds.Add(Request.CommitId);
  return ECrowdDemoBusinessCommitAcceptResult::Applied;
}

uint64 FCrowdDemoBusinessCommitLedger::GetCargoCarrier(
  const uint64 CargoStableEntityId) const
{
  const uint64* Carrier = CargoCarrierByTask.Find(CargoStableEntityId);
  return Carrier ? *Carrier : 0;
}

int32 FCrowdDemoBusinessCommitLedger::GetCombatHitQuantity(
  const uint64 TargetStableEntityId) const
{
  const int32* Quantity = CombatHitQuantityByTarget.Find(TargetStableEntityId);
  return Quantity ? *Quantity : 0;
}
