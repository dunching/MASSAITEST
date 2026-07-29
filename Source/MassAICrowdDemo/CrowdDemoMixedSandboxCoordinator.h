#pragma once

#include "CoreMinimal.h"
#include "CrowdLocalPredictiveInteractionKernel.h"
#include "CrowdDemoContinuousLifecycleCoordinator.h"
#include "CrowdNavSurfaceGraph.h"
#include "GameFramework/Actor.h"
#include "Mass/CrowdDemoBehaviorAdapters.h"
#include "MassCrowdMassLifecycleWorld.h"
#include "MassCrowdBoundaryRunner.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdNavRuntime.h"
#include "MassCrowdSpatialSafety.h"
#include "CrowdDemoMixedSandboxCoordinator.generated.h"

class APlayerController;
class AMassCrowdReplicationActor;

USTRUCT()
struct FCrowdDemoMixedSandboxConfig
{
  GENERATED_BODY()

  UPROPERTY() uint8 bValid = 0;
  UPROPERTY() int32 PopulationLimit = 20;
  UPROPERTY() uint32 SnapshotRevision = 1;
  UPROPERTY() uint32 RelevantSetRevision = 1;
  UPROPERTY() uint64 NavTopologyHash = 0;
};

USTRUCT()
struct FCrowdDemoMixedAgentState
{
  GENERATED_BODY()

  UPROPERTY() uint64 StableEntityId = 0;
  UPROPERTY() uint32 LifecycleSerial = 0;
  UPROPERTY() uint32 MembershipKey = 0;
  UPROPERTY() FVector_NetQuantize10 Location = FVector::ZeroVector;
  UPROPERTY() uint32 DerivedBehaviorLabel = 0;
  UPROPERTY() uint8 Health = 0;
  UPROPERTY() uint32 TargetProviderId = 0;
  UPROPERTY() uint64 TargetStableEntityId = 0;
  UPROPERTY() uint32 TargetLifecycleSerial = 0;
  UPROPERTY() uint32 TaskProviderId = 0;
  UPROPERTY() uint64 TaskStableEntityId = 0;
  UPROPERTY() uint32 TaskLifecycleSerial = 0;
};

UCLASS()
class MASSAICROWDDEMO_API ACrowdDemoMixedSandboxCoordinator : public AActor
{
  GENERATED_BODY()

public:
  ACrowdDemoMixedSandboxCoordinator();
  virtual void BeginPlay() override;
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  virtual void Tick(float DeltaSeconds) override;
  virtual void GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
  UFUNCTION() void OnRep_Config();

private:
  struct FSlotState
  {
    FCrowdAgentFacts Facts;
    FVector Location = FVector::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
    float YawDegrees = 0.0f;
    uint64 AttachedNavNodeId = 0;
    uint64 CachedGoalNodeId = 0;
    uint32 InteractionLayer = 0;
    uint32 MembershipKey = 0;
    uint32 TransitionRevision = 1;
    int32 Health = 100;
    int64 LastAttackFixedStep = -1000;
    int64 LastLogisticsFixedStep = -1000;
    int64 HitReactionUntilFixedStep = INDEX_NONE;
    FVector HitReactionVelocity = FVector::ZeroVector;
    int32 PreviousBlockedAgeSteps = 0;
    bool bActive = false;
  };

  UPROPERTY(ReplicatedUsing=OnRep_Config, Transient)
  FCrowdDemoMixedSandboxConfig Config;

  FCrowdMassLifecycleWorld LifecycleWorld;
  FMassArchetypeHandle LifecycleArchetype;
  FCrowdBehaviorSourceRuntime* BehaviorSourceRuntime = nullptr;
  FCrowdDemoBusinessCommitLedger BusinessLedger;
  TSharedPtr<const FCrowdNavSurfaceGraph, ESPMode::ThreadSafe> NavGraphHandle;
  TMap<FName, FVector> MarkerLocations;
  TMap<uint64, FCrowdNavFlowHandle> FlowHandleByGoalNode;
  TMap<uint64, FCrowdStableEntityRef>
    PresentedEntitiesByStableId;
  FCrowdSpatialSafetyIndex SpatialSafety;
  TMap<TWeakObjectPtr<APlayerController>,
    TWeakObjectPtr<AMassCrowdReplicationActor>> ReplicationChannels;
  TMap<TWeakObjectPtr<APlayerController>, double>
    ReplicationChannelEligibleSeconds;
  TMap<FCrowdStableEntityRef, uint32>
    LastPublishedSourceSetRevisions;
  TMap<FCrowdStableEntityRef, uint64>
    LastPublishedHostFactHashes;
  TArray<FCrowdLocalPredictiveGrantState>
    MixedLocalPredictiveGrantStates;
  TArray<FSlotState> Slots;
  TArray<double> ServerStepMilliseconds;
  TArray<double> ClientFrameMilliseconds;
  double FixedStepAccumulatorSeconds = 0.0;
  double LastCheckpointWorldSeconds = -1000.0;
  double NextInitializationAttemptSeconds = 0.0;
  int64 FixedStepIndex = 0;
  uint64 NextLifecycleSequence = 1;
  uint64 NextStateSequence = 1;
  uint64 LastReceivedStateSequence = 0;
  int64 LastReceivedFixedStep = 0;
  uint64 LastExpectedEntitySetHash = 0;
  uint64 LastExpectedMembershipHash = 0;
  uint64 LastBoundaryCommitHash = 0;
  uint64 PresentationSequence = 0;
  uint32 LastConsumedBaselineRevision = 0;
  uint32 RelevantSetRevision = 1;
  int32 PendingRespawnSlot = INDEX_NONE;
  int32 PendingCombatDeathSlot = INDEX_NONE;
  int32 MembershipCursor = 1;
  int32 RecycleCursor = 17;
  int32 SpawnCount = 0;
  int32 DespawnCount = 0;
  int32 MembershipChangeCount = 0;
  int32 BehaviorTransitionCount = 0;
  int32 DuplicateCommitCount = 0;
  int32 SafetyHoldCount = 0;
  int32 StaleRejectCount = 0;
  int32 MaxObservedPopulation = 0;
  uint32 SeenBehaviorBits = 0;
  float MinimumSeparationCm = TNumericLimits<float>::Max();
  bool bWorldInitialized = false;
  bool bClientVisualsInitialized = false;
  bool bPresentationProfileRegistered = false;
  bool bVisualSyncPending = false;
  bool bServerPassLogged = false;
  bool bClientPassLogged = false;
  bool bCaptureRequested = false;
  bool bCaptureCompleted = false;
  bool bClientApplyFailureLogged = false;
  double CaptureAtWorldSeconds = 0.0;

  bool TryInitializeServer();
  bool InitializeLifecycleWorld();
  void InitializeSlotState(int32 SlotIndex, uint32 LifecycleSerial);
  FCrowdAgentFacts MakeAgentFacts(int32 SlotIndex, uint32 LifecycleSerial) const;
  void AdvanceServerFixedStep();
  bool EvaluateSlotBehavior(
    int32 SlotIndex,
    TArray<FSlotState>& InOutSlots);
  bool ApplyPreparedBehaviorBusiness(
    const FCrowdBehaviorPreparedBoundary& Prepared,
    TArray<FSlotState>& InOutSlots,
    FCrowdDemoBusinessCommitLedger& InOutLedger,
    int32& InOutDuplicateCommitCount,
    int32& InOutPendingCombatDeathSlot);
  bool RunProductMovementBoundary(
    const FCrowdBehaviorPreparedBoundary& PreparedBehavior,
    TArray<FSlotState>& InOutSlots,
    int32& OutSafetyHolds,
    uint64& OutCommitHash);
  bool GetOrBuildFlow(
    const FVector& Objective,
    const FCrowdNavSurfaceFlow*& OutFlow,
    uint64 PreferredGoalNodeId = 0,
    uint64* OutGoalNodeId = nullptr);
  bool RebuildSpatialSafety();
  bool BuildLifecycleOperation(FCrowdDemoContinuousLifecycleOperation& OutOperation);
  bool ApplyLifecycleOperation(const FCrowdDemoContinuousLifecycleOperation& Operation);
  FCrowdLifecycleBatchHeader MakeBatchHeader(
    const FCrowdDemoContinuousLifecycleOperation& Operation) const;
  void PublishProductStateFrame();
  void RefreshReplicationChannels();
  bool PublishBaseline(AMassCrowdReplicationActor& Channel);
  void ConsumeProductReplication();
  bool ApplyReplicatedAgentState(
    const FCrowdDemoMixedAgentState& State,
    int64 InFixedStepIndex);
  void SyncClientVisualsIncremental();
  void LogCheckpoint();
  void TryLogPass();
  FVector Marker(FName Tag, const FVector& Fallback) const;
  uint32 MembershipForDiagnosticLabel(ECrowdActiveBehavior Label) const;
  static double Percentile95(TArray<double> Values);
};
