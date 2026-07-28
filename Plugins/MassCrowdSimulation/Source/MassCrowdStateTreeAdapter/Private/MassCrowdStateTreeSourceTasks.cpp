#include "MassCrowdStateTreeSourceTasks.h"

#include "MassCrowdRuntimeSubsystem.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassCrowdStateTreeSourceTasks)

namespace
{
  UMassCrowdRuntimeSubsystem* ResolveRuntime(
    FStateTreeExecutionContext& Context)
  {
    UWorld* World = Context.GetWorld();
    return World
      ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>()
      : nullptr;
  }

  FCrowdBehaviorSourceHandle BuildHandle(
    const uint32 ProviderId,
    const int64 StableEntityId,
    const uint32 LifecycleSerial,
    const uint32 ControllerId,
    const uint32 SourceSequence)
  {
    if (StableEntityId <= 0) return {};
    return {
      {ProviderId, static_cast<uint64>(StableEntityId),
        LifecycleSerial},
      {ControllerId},
      SourceSequence};
  }
}

bool FCrowdStateTreeCommandBuilder::Build(
  const FCrowdStateTreeSourceCommandRequest& Request,
  FCrowdBehaviorSourceCommand& OutCommand)
{
  OutCommand = {};
  if (Request.EffectiveFixedStep < 0
    || !Request.Handle.IsValid()
    || Request.CommandSequence == 0
    || Request.Kind >= ECrowdBehaviorSourceCommandKind::Count
    || !Request.SourceTypeId.IsValid()
    || Request.Priority < MIN_int16
    || Request.Priority > MAX_int16
    || Request.LifetimeSteps < 0)
    return false;
  OutCommand.EffectiveFixedStep = Request.EffectiveFixedStep;
  OutCommand.Handle = Request.Handle;
  OutCommand.CommandSequence = Request.CommandSequence;
  OutCommand.Kind = Request.Kind;
  OutCommand.SourceTypeId = Request.SourceTypeId;
  OutCommand.Priority = static_cast<int16>(Request.Priority);
  OutCommand.LifetimeSteps = Request.LifetimeSteps;
  return OutCommand.Payload.Set(
    CrowdBuiltinBehaviorSchemas::Standard, Request.Payload)
    && OutCommand.IsValid();
}

FCrowdStateTreeSourceCommandTask::
FCrowdStateTreeSourceCommandTask()
{
  bShouldCallTick = false;
  bShouldCopyBoundPropertiesOnTick = false;
  bShouldCopyBoundPropertiesOnExitState = false;
}

EStateTreeRunStatus FCrowdStateTreeSourceCommandTask::EnterState(
  FStateTreeExecutionContext& Context,
  const FStateTreeTransitionResult& Transition) const
{
  FInstanceDataType& Data = Context.GetInstanceData(*this);
  UMassCrowdRuntimeSubsystem* Runtime = ResolveRuntime(Context);
  if (!Runtime) return EStateTreeRunStatus::Failed;

  FCrowdStateTreeSourceCommandRequest Request;
  Request.EffectiveFixedStep = Data.EffectiveFixedStep;
  Request.Handle = BuildHandle(
    Data.ProviderId, Data.StableEntityId, Data.LifecycleSerial,
    Data.ControllerId, Data.SourceSequence);
  Request.CommandSequence = Data.CommandSequence;
  Request.Kind =
    static_cast<ECrowdBehaviorSourceCommandKind>(Data.CommandKind);
  Request.SourceTypeId = {Data.SourceTypeId};
  Request.Priority = Data.Priority;
  Request.LifetimeSteps = Data.LifetimeSteps;
  Request.Payload.Vector = Data.Vector;
  Request.Payload.TargetRef = {
    Data.TargetProviderId,
    static_cast<uint64>(Data.TargetStableEntityId),
    Data.TargetLifecycleSerial};
  Request.Payload.CommitId =
    Data.CommitId > 0 ? static_cast<uint64>(Data.CommitId) : 0;
  Request.Payload.PrimaryId = Data.PrimaryId;
  Request.Payload.SecondaryId = Data.SecondaryId;
  Request.Payload.Quantity = Data.Quantity;
  Request.Payload.Flags = Data.Flags;
  FCrowdBehaviorSourceCommand Command;
  return FCrowdStateTreeCommandBuilder::Build(Request, Command)
    && Runtime->GetBehaviorSourceRuntime().QueueCommand(Command)
    ? EStateTreeRunStatus::Succeeded
    : EStateTreeRunStatus::Failed;
}

FCrowdStateTreeWaitSourceEventTask::
FCrowdStateTreeWaitSourceEventTask()
{
  bShouldCallTick = true;
  bShouldCopyBoundPropertiesOnTick = true;
  bShouldCopyBoundPropertiesOnExitState = false;
}

EStateTreeRunStatus FCrowdStateTreeWaitSourceEventTask::EnterState(
  FStateTreeExecutionContext& Context,
  const FStateTreeTransitionResult& Transition) const
{
  return Tick(Context, 0.0f);
}

EStateTreeRunStatus FCrowdStateTreeWaitSourceEventTask::Tick(
  FStateTreeExecutionContext& Context,
  const float DeltaTime) const
{
  const FInstanceDataType& Data = Context.GetInstanceData(*this);
  UMassCrowdRuntimeSubsystem* Runtime = ResolveRuntime(Context);
  if (!Runtime
    || Data.EventKind
      >= static_cast<uint8>(ECrowdBehaviorSourceEventKind::Count))
    return EStateTreeRunStatus::Failed;
  const FCrowdBehaviorSourceHandle Handle = BuildHandle(
    Data.ProviderId, Data.StableEntityId, Data.LifecycleSerial,
    Data.ControllerId, Data.SourceSequence);
  if (!Handle.IsValid()) return EStateTreeRunStatus::Failed;
  return Runtime->GetBehaviorSourceRuntime().HasCommittedEvent(
      Handle,
      static_cast<ECrowdBehaviorSourceEventKind>(Data.EventKind),
      Data.MinimumFixedStep)
    ? EStateTreeRunStatus::Succeeded
    : EStateTreeRunStatus::Running;
}
