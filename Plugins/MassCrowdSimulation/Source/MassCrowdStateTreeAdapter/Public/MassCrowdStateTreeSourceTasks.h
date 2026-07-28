#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "StateTreeTaskBase.h"

#include "MassCrowdStateTreeSourceTasks.generated.h"

struct FCrowdStateTreeSourceCommandRequest
{
  int64 EffectiveFixedStep = INDEX_NONE;
  FCrowdBehaviorSourceHandle Handle;
  uint32 CommandSequence = 0;
  ECrowdBehaviorSourceCommandKind Kind =
    ECrowdBehaviorSourceCommandKind::Start;
  FCrowdBehaviorSourceTypeId SourceTypeId;
  int16 Priority = 0;
  int32 LifetimeSteps = 0;
  FCrowdBuiltinBehaviorSourcePayload Payload;
};
class MASSCROWDSTATETREEADAPTER_API FCrowdStateTreeCommandBuilder
{
public:
  static bool Build(
    const FCrowdStateTreeSourceCommandRequest& Request,
    FCrowdBehaviorSourceCommand& OutCommand);
};

USTRUCT()
struct FCrowdStateTreeSourceCommandTaskInstanceData
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, Category="Source")
  int64 EffectiveFixedStep = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  uint32 ProviderId = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  int64 StableEntityId = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  uint32 LifecycleSerial = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  uint32 ControllerId = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  uint32 SourceSequence = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  uint32 CommandSequence = 0;
  UPROPERTY(EditAnywhere, Category="Source", meta=(ClampMin="0", ClampMax="2"))
  uint8 CommandKind = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  uint32 SourceTypeId = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  int32 Priority = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  int32 LifetimeSteps = 0;
  UPROPERTY(EditAnywhere, Category="Payload")
  FVector Vector = FVector::ZeroVector;
  UPROPERTY(EditAnywhere, Category="Payload")
  uint32 TargetProviderId = 0;
  UPROPERTY(EditAnywhere, Category="Payload")
  int64 TargetStableEntityId = 0;
  UPROPERTY(EditAnywhere, Category="Payload")
  uint32 TargetLifecycleSerial = 0;
  UPROPERTY(EditAnywhere, Category="Payload")
  int64 CommitId = 0;
  UPROPERTY(EditAnywhere, Category="Payload")
  uint32 PrimaryId = 0;
  UPROPERTY(EditAnywhere, Category="Payload")
  uint32 SecondaryId = 0;
  UPROPERTY(EditAnywhere, Category="Payload")
  int32 Quantity = 0;
  UPROPERTY(EditAnywhere, Category="Payload")
  uint32 Flags = 0;
};

USTRUCT(meta=(
  DisplayName="Queue Crowd Behavior Source Command",
  Category="Mass Crowd"))
struct MASSCROWDSTATETREEADAPTER_API
  FCrowdStateTreeSourceCommandTask : public FStateTreeTaskCommonBase
{
  GENERATED_BODY()

  using FInstanceDataType =
    FCrowdStateTreeSourceCommandTaskInstanceData;

  FCrowdStateTreeSourceCommandTask();
  virtual const UStruct* GetInstanceDataType() const override
  {
    return FInstanceDataType::StaticStruct();
  }
  virtual EStateTreeRunStatus EnterState(
    FStateTreeExecutionContext& Context,
    const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FCrowdStateTreeWaitSourceEventTaskInstanceData
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, Category="Source")
  uint32 ProviderId = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  int64 StableEntityId = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  uint32 LifecycleSerial = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  uint32 ControllerId = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  uint32 SourceSequence = 0;
  UPROPERTY(EditAnywhere, Category="Source", meta=(ClampMin="0", ClampMax="4"))
  uint8 EventKind = 0;
  UPROPERTY(EditAnywhere, Category="Source")
  int64 MinimumFixedStep = 0;
};

USTRUCT(meta=(
  DisplayName="Wait for Crowd Behavior Source Event",
  Category="Mass Crowd"))
struct MASSCROWDSTATETREEADAPTER_API
  FCrowdStateTreeWaitSourceEventTask : public FStateTreeTaskCommonBase
{
  GENERATED_BODY()

  using FInstanceDataType =
    FCrowdStateTreeWaitSourceEventTaskInstanceData;

  FCrowdStateTreeWaitSourceEventTask();
  virtual const UStruct* GetInstanceDataType() const override
  {
    return FInstanceDataType::StaticStruct();
  }
  virtual EStateTreeRunStatus EnterState(
    FStateTreeExecutionContext& Context,
    const FStateTreeTransitionResult& Transition) const override;
  virtual EStateTreeRunStatus Tick(
    FStateTreeExecutionContext& Context,
    float DeltaTime) const override;
};
