#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "MassEntityView.h"
#include "MassReplicationProcessor.h"
#include "MassReplicationTransformHandlers.h"
#include "CrowdDemoMassReplication.generated.h"

USTRUCT()
struct FReplicatedCrowdDemoAgent : public FReplicatedAgentBase
{
  GENERATED_BODY()

  const FReplicatedAgentPositionYawData& GetReplicatedPositionYawData() const { return PositionYaw; }
  FReplicatedAgentPositionYawData& GetReplicatedPositionYawDataMutable() { return PositionYaw; }

  UPROPERTY(Transient)
  FVector_NetQuantize10 Velocity = FVector::ZeroVector;

  UPROPERTY(Transient)
  int32 VisualId = INDEX_NONE;

  UPROPERTY(Transient)
  int32 LifecycleSerial = 0;

  UPROPERTY(Transient)
  uint8 AnimState = 0;

  UPROPERTY(Transient)
  uint8 VatClipIndex = 0;

  UPROPERTY(Transient)
  uint8 VatPhaseByte = 0;

  UPROPERTY(Transient)
  uint8 VatPlayRateByte = 128;

  UPROPERTY(Transient)
  FCrowdDemoCombatNetState Combat;

  UPROPERTY(Transient)
  float ServerSampleTimeSeconds = 0.0f;

  UPROPERTY(Transient)
  uint32 NetworkIdValue = 0;

private:
  UPROPERTY(Transient)
  FReplicatedAgentPositionYawData PositionYaw;
};

USTRUCT()
struct FCrowdDemoMassFastArrayItem : public FMassFastArrayItemBase
{
  GENERATED_BODY()

  FCrowdDemoMassFastArrayItem() = default;
  FCrowdDemoMassFastArrayItem(const FReplicatedCrowdDemoAgent& InAgent, const FMassReplicatedAgentHandle InHandle)
    : FMassFastArrayItemBase(InHandle)
    , Agent(InAgent)
  {
  }

  typedef FReplicatedCrowdDemoAgent FReplicatedAgentType;

  UPROPERTY()
  FReplicatedCrowdDemoAgent Agent;
};

class MASSAICROWDDEMO_API FCrowdDemoMassClientBubbleHandler : public TClientBubbleHandlerBase<FCrowdDemoMassFastArrayItem>
{
public:
  typedef TClientBubbleHandlerBase<FCrowdDemoMassFastArrayItem> Super;

  bool UpdateAgent(FMassReplicatedAgentHandle Handle, const FReplicatedCrowdDemoAgent& Agent);
  bool UpdateAgentMinimal(FMassReplicatedAgentHandle Handle, const FReplicatedCrowdDemoAgent& Agent);
  int32 GetAgentCount() const;

protected:
#if UE_REPLICATION_COMPILE_CLIENT_CODE
  virtual void PostReplicatedAdd(TArrayView<int32> AddedIndices, int32 FinalSize) override;
  virtual void PostReplicatedChange(TArrayView<int32> ChangedIndices, int32 FinalSize) override;
  void SetReplicatedEntityData(const FMassEntityView& EntityView, const FReplicatedCrowdDemoAgent& Agent) const;
#endif
};

USTRUCT()
struct FCrowdDemoMassClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
  GENERATED_BODY()

  FCrowdDemoMassClientBubbleSerializer()
  {
    Bubble.Initialize(Agents, *this);
  }

  bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
  {
    return FFastArraySerializer::FastArrayDeltaSerialize<FCrowdDemoMassFastArrayItem, FCrowdDemoMassClientBubbleSerializer>(Agents, DeltaParams, *this);
  }

  int32 GetAgentCount() const { return Agents.Num(); }

  FCrowdDemoMassClientBubbleHandler Bubble;

private:
  UPROPERTY(Transient)
  TArray<FCrowdDemoMassFastArrayItem> Agents;
};

template<>
struct TStructOpsTypeTraits<FCrowdDemoMassClientBubbleSerializer> : public TStructOpsTypeTraitsBase2<FCrowdDemoMassClientBubbleSerializer>
{
  enum
  {
    WithNetDeltaSerializer = true,
    WithCopy = false,
  };
};

UCLASS()
class MASSAICROWDDEMO_API ACrowdDemoMassClientBubbleInfo : public AMassClientBubbleInfoBase
{
  GENERATED_BODY()

public:
  ACrowdDemoMassClientBubbleInfo(const FObjectInitializer& ObjectInitializer);

  FCrowdDemoMassClientBubbleSerializer& GetCrowdDemoSerializer() { return CrowdDemoSerializer; }
  const FCrowdDemoMassClientBubbleSerializer& GetCrowdDemoSerializer() const { return CrowdDemoSerializer; }

protected:
  virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
  UPROPERTY(Replicated, Transient)
  FCrowdDemoMassClientBubbleSerializer CrowdDemoSerializer;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoMassReplicator : public UMassReplicatorBase
{
  GENERATED_BODY()

public:
  virtual void AddRequirements(FMassEntityQuery& EntityQuery) override;
  virtual void ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext) override;
};
