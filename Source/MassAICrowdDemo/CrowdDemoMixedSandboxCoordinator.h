#pragma once

#include "CoreMinimal.h"
#include "CrowdLocalPredictiveInteractionKernel.h"
#include "CrowdDemoContinuousLifecycleCoordinator.h"
#include "CrowdNavSurfaceGraph.h"
#include "GameFramework/Actor.h"
#include "CrowdDemoBusinessAdapters.h"
#include "CrowdDemoBusinessPlanner.h"
#include "CrowdDemoAttackPlanner.h"
#include "MassCrowdMassLifecycleWorld.h"
#include "MassCrowdBoundaryRunner.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdCombatResolver.h"
#include "MassCrowdNavRuntime.h"
#include "MassCrowdProjectileBoundary.h"
#include "MassCrowdProjectileMassStore.h"
#include "MassCrowdSpatialSafety.h"
#include "Mass/CrowdDemoAttackHostAdapter.h"
#include "CrowdDemoMixedSandboxCoordinator.generated.h"

class APlayerController;
class AMassCrowdReplicationActor;

USTRUCT()
struct FCrowdDemoMixedSandboxConfig
{
  GENERATED_BODY()

  UPROPERTY() uint8 bValid = 0;
  UPROPERTY() uint8 bMixedCombatIntegration = 0;
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
  UPROPERTY() uint32 FactionId = 0;
  UPROPERTY() uint32 AttackProfileId = 0;
  UPROPERTY() uint8 AttackPhase = 0;
  UPROPERTY() int64 AttackPhaseEnterFixedStep = 0;
  UPROPERTY() int64 AttackCooldownEndFixedStep = 0;
  UPROPERTY() uint32 AttackFireSequence = 0;
  UPROPERTY() uint32 AttackTargetProviderId = 0;
  UPROPERTY() uint64 AttackTargetStableEntityId = 0;
  UPROPERTY() uint32 AttackTargetLifecycleSerial = 0;
  UPROPERTY() uint32 TargetProviderId = 0;
  UPROPERTY() uint64 TargetStableEntityId = 0;
  UPROPERTY() uint32 TargetLifecycleSerial = 0;
  UPROPERTY() uint32 TaskProviderId = 0;
  UPROPERTY() uint64 TaskStableEntityId = 0;
  UPROPERTY() uint32 TaskLifecycleSerial = 0;
  UPROPERTY() int32 ProjectileExpectedCount = 0;
  UPROPERTY() int32 ProjectileSpawnedCount = 0;
  UPROPERTY() int32 ProjectileImpactCount = 0;
  UPROPERTY() int32 ProjectileDamageCount = 0;
  UPROPERTY() int32 ProjectileExpiredCount = 0;
  UPROPERTY() int32 ProjectileActiveCount = 0;
  UPROPERTY() int32 ProjectileDuplicateCount = 0;
  UPROPERTY() uint64 ProjectileTraceHash = 0;
  UPROPERTY() int32 AttackIntentCount = 0;
  UPROPERTY() int32 AttackImpactCount = 0;
  UPROPERTY() int32 AttackDamageCount = 0;
  UPROPERTY() int32 AttackDeathCount = 0;
  UPROPERTY() int32 AttackTargetSwitchCount = 0;
  UPROPERTY() int32 MeleeAttackIntentCount = 0;
  UPROPERTY() int32 MidRangeAttackIntentCount = 0;
  UPROPERTY() int32 RangedAttackIntentCount = 0;
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
    uint32 FactionId = 0;
    uint32 AttackProfileId = 0;
    FCrowdDemoAttackState AttackState;
    int32 Health = 100;
    int64 LastAttackFixedStep = -1000;
    int64 LastLogisticsFixedStep = -1000;
    int64 HitReactionUntilFixedStep = INDEX_NONE;
    FVector HitReactionVelocity = FVector::ZeroVector;
    FCrowdDemoPlannerAssignment PlannerAssignment;
    int32 PreviousBlockedAgeSteps = 0;
    bool bActive = false;
  };

  struct FMixedMovementWork
  {
    FCrowdMassCommitPlan Plan;
    TArray<FCrowdLocalPredictiveGrantState> GrantStates;
    TMap<int32, int32> BlockedAgeByAgentId;
    double MovementMilliseconds = 0.0;
    double ParticleMilliseconds = 0.0;
    double FacingMilliseconds = 0.0;
    int32 SafetyHolds = 0;
    int32 FailureCode = 0;
    bool bCompleted = false;
  };

  struct FPendingMixedMovement
  {
    TUniquePtr<FCrowdMassBoundaryRunner> Runner;
    TSharedPtr<FMixedMovementWork, ESPMode::ThreadSafe> Work;
    TSharedPtr<TArray<FSlotState>, ESPMode::ThreadSafe> StagedSlots;
    FCrowdMassBoundarySnapshot Snapshot;
    TArray<FCrowdMassCommitTarget> Targets;
    FCrowdBehaviorPreparedBoundary PreparedBehavior;
    TArray<uint32> ResolvedInteractionLayers;
    TArray<uint64> ResolvedAttachedNodeIds;
    TUniqueFunction<void(bool, int32, uint64)> Finalize;
    double ProductStartSeconds = 0.0;
    double GatherEndSeconds = 0.0;
  };

  UPROPERTY(ReplicatedUsing=OnRep_Config, Transient)
  FCrowdDemoMixedSandboxConfig Config;

  FCrowdMassLifecycleWorld LifecycleWorld;
  FMassArchetypeHandle LifecycleArchetype;
  FCrowdBehaviorSourceRuntime* BehaviorSourceRuntime = nullptr;
  FCrowdDemoBusinessPlannerRegistry BusinessPlannerRegistry;
  FCrowdDemoBusinessCommitLedger BusinessLedger;
  FCrowdMassProjectileStore ProjectileStore;
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
  uint64 LastPlannerDecisionHash = 0;
  uint64 PresentationSequence = 0;
  uint64 ProductBoundaryGeneration = 1;
  TUniquePtr<FPendingMixedMovement> PendingMixedMovement;
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
  int32 ProjectileExpectedCount = 0;
  int32 ProjectileSpawnedCount = 0;
  int32 ProjectileImpactCount = 0;
  int32 ProjectileDamageCount = 0;
  int32 ProjectileExpiredCount = 0;
  int32 ProjectileActiveCount = 0;
  int32 ProjectileDuplicateCount = 0;
  int32 AttackIntentCount = 0;
  int32 AttackImpactCount = 0;
  int32 AttackDamageCount = 0;
  int32 AttackDeathCount = 0;
  int32 AttackTargetSwitchCount = 0;
  int32 MeleeAttackIntentCount = 0;
  int32 MidRangeAttackIntentCount = 0;
  int32 RangedAttackIntentCount = 0;
  int32 MaxObservedPopulation = 0;
  uint32 SeenBehaviorBits = 0;
  float MinimumSeparationCm = TNumericLimits<float>::Max();
  uint64 ProjectileTraceHash = 14695981039346656037ull;
  bool bWorldInitialized = false;
  bool bClientVisualsInitialized = false;
  bool bPresentationProfileRegistered = false;
  bool bVisualSyncPending = false;
  bool bServerPassLogged = false;
  bool bClientPassLogged = false;
  bool bCaptureRequested = false;
  bool bCaptureCompleted = false;
  bool bClientApplyFailureLogged = false;
  bool bProjectileBatchSpawned = false;
  bool bMixedCombatIntegration = false;
  double CaptureAtWorldSeconds = 0.0;

  bool TryInitializeServer();
  bool InitializeLifecycleWorld();
  void InitializeSlotState(int32 SlotIndex, uint32 LifecycleSerial);
  FCrowdAgentFacts MakeAgentFacts(int32 SlotIndex, uint32 LifecycleSerial) const;
  void AdvanceServerFixedStep();
  bool PlanBusinessBoundary(
    TArray<FSlotState>& InOutSlots,
    FCrowdDemoPlannerDecisionBatch& OutDecisionBatch);
  bool PrepareProjectileBoundary(
    const TArray<FSlotState>& StagedSlots,
    FCrowdPreparedProjectileBoundary& OutPrepared,
    FCrowdPreparedHostHitCommit& OutHitCommit);
  bool PrepareMixedCombatAttackPlan(
    TArray<FSlotState>& InOutSlots,
    TArray<FCrowdDemoAttackIntent>& OutIntents,
    FCrowdDemoAttackPlanSummary& OutSummary,
    int32& OutTargetSwitchCount);
  bool PrepareMixedCombatBoundary(
    const TArray<FSlotState>& StagedSlots,
    TConstArrayView<FCrowdDemoAttackIntent> Intents,
    FCrowdDemoPreparedAttackBoundary& OutAttack,
    FCrowdPreparedProjectileBoundary& OutProjectile,
    FCrowdDemoPreparedAttackHealthPatch& OutHealthPatch);
  bool BeginProductMovementBoundary(
    const FCrowdBehaviorPreparedBoundary& PreparedBehavior,
    const TSharedRef<TArray<FSlotState>, ESPMode::ThreadSafe>& InOutSlots,
    TUniqueFunction<void(bool, int32, uint64)>&& Finalize);
  bool PollProductMovementBoundary();
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
  FCrowdDemoMixedAgentState BuildReplicatedAgentState(
    const FSlotState& Slot) const;
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
