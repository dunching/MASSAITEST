#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassCrowdMassLifecycleWorld.h"
#include "CrowdDemoContinuousLifecycleCoordinator.generated.h"

class APlayerController;
class AMassCrowdReplicationActor;

UENUM()
enum class ECrowdDemoContinuousLifecycleOperationKind : uint8
{
  Spawn = 1,
  Despawn = 2,
  Membership = 3
};

USTRUCT()
struct FCrowdDemoContinuousLifecycleConfig
{
  GENERATED_BODY()

  UPROPERTY() uint8 bValid = 0;
  UPROPERTY() int32 InitialEntityCount = 10;
  UPROPERTY() int32 PopulationLimit = 20;
  UPROPERTY() uint32 SnapshotRevision = 1;
  UPROPERTY() uint32 InitialRelevantSetRevision = 1;
  UPROPERTY() int64 InitialFixedStepIndex = 0;
};

USTRUCT()
struct FCrowdDemoContinuousLifecycleOperation
{
  GENERATED_BODY()

  UPROPERTY() ECrowdDemoContinuousLifecycleOperationKind Kind =
    ECrowdDemoContinuousLifecycleOperationKind::Spawn;
  UPROPERTY() uint64 Sequence = 0;
  UPROPERTY() uint32 RelevantSetRevision = 0;
  UPROPERTY() int64 FixedStepIndex = 0;
  UPROPERTY() uint64 StableEntityId = 0;
  UPROPERTY() uint32 LifecycleSerial = 0;
  UPROPERTY() uint32 PreviousMembershipKey = 0;
  UPROPERTY() uint32 NewMembershipKey = 0;
  UPROPERTY() uint8 DespawnReason = static_cast<uint8>(ECrowdDespawnReason::HostDestroyed);
};

UCLASS()
class MASSAICROWDDEMO_API ACrowdDemoContinuousLifecycleCoordinator : public AActor
{
  GENERATED_BODY()

public:
  ACrowdDemoContinuousLifecycleCoordinator();
  virtual void BeginPlay() override;
  virtual void Tick(float DeltaSeconds) override;
  virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
  UFUNCTION()
  void OnRep_Config();

private:
  struct FSlotState
  {
    uint32 LifecycleSerial = 0;
    uint32 MembershipKey = 0;
    bool bActive = false;
  };

  UPROPERTY(ReplicatedUsing = OnRep_Config, Transient)
  FCrowdDemoContinuousLifecycleConfig Config;

  FCrowdMassLifecycleWorld LifecycleWorld;
  FMassArchetypeHandle LifecycleArchetype;
  TMap<TWeakObjectPtr<APlayerController>,
    TWeakObjectPtr<AMassCrowdReplicationActor>> ReplicationChannels;
  TArray<FSlotState> Slots;
  double FixedStepAccumulatorSeconds = 0.0;
  double LastCheckpointWorldSeconds = -1000.0;
  int64 FixedStepIndex = 0;
  uint64 NextSequence = 1;
  uint32 RelevantSetRevision = 1;
  int32 PendingRespawnSlot = INDEX_NONE;
  int32 DespawnCursor = 0;
  int32 SpawnCount = 0;
  int32 DespawnCount = 0;
  int32 MembershipChangeCount = 0;
  int32 MaxObservedPopulation = 0;
  int32 StaleRejectCount = 0;
  int32 OperationPhase = 0;
  uint32 LastConsumedBaselineRevision = 0;
  bool bWorldInitialized = false;
  bool bVisualSyncPending = false;
  bool bClientVisualsInitialized = false;
  bool bPresentationProfileRegistered = false;
  float StartDelaySeconds = 25.0f;

  bool InitializeLifecycleWorld();
  void RefreshReplicationChannels();
  bool PublishBaseline(AMassCrowdReplicationActor& Channel);
  void PublishLifecycleOperation(
    const FCrowdDemoContinuousLifecycleOperation& Operation);
  void ConsumeProductReplication();
  void AdvanceServerFixedStep();
  bool BuildNextServerOperation(FCrowdDemoContinuousLifecycleOperation& OutOperation);
  bool ApplyOperation(const FCrowdDemoContinuousLifecycleOperation& Operation);
  void SyncClientVisualsIncremental();
  FTransform MakeVisualTransform(int32 SlotIndex) const;
  void LogCheckpoint();
  FCrowdAgentFacts MakeAgentFacts(int32 SlotIndex, uint32 LifecycleSerial) const;
  FCrowdLifecycleBatchHeader MakeBatchHeader(
    const FCrowdDemoContinuousLifecycleOperation& Operation) const;
};
