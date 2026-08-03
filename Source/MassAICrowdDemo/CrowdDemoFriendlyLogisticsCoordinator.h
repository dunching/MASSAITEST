#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdLogistics.h"
#include "MassCrowdNavRuntime.h"
#include "MassCrowdRuntimeBridge.h"
#include "CrowdDemoFriendlyLogisticsCoordinator.generated.h"

class APlayerController;
class AMassCrowdReplicationActor;
class UMassCrowdPresentationSubsystem;
class UCrowdDemoMassSubsystem;

UCLASS()
class MASSAICROWDDEMO_API ACrowdDemoFriendlyLogisticsCoordinator final
  : public AActor
{
  GENERATED_BODY()

public:
  ACrowdDemoFriendlyLogisticsCoordinator();
  virtual void BeginPlay() override;
  virtual void EndPlay(
    const EEndPlayReason::Type EndPlayReason) override;
  virtual void Tick(float DeltaSeconds) override;

private:
  enum class EProductBoundaryAdvance : uint8
  {
    Pending,
    Committed,
    Failed
  };

  struct FProductMovementWork
  {
    FCrowdMassCommitPlan Plan;
    FVector FailurePosition = FVector::ZeroVector;
    uint64 FailureNodeId = 0;
    uint64 FailureGoalNodeId = 0;
    uint32 FailureLayer = 0;
    uint8 FailureStage = 0;
    TAtomic<bool> bCompleted{false};
  };

  struct FPendingProductBoundary
  {
    TSharedPtr<FProductMovementWork, ESPMode::ThreadSafe> Work;
    FCrowdMassBoundarySnapshot Snapshot;
    TArray<FCrowdMassCommitTarget> Targets;
    FCrowdBehaviorPreparedBoundary PreparedBehavior;
    FCrowdLogisticsTaskFact Task;
    FCrowdStableEntityRef Carrier;
    uint64 PlannerDecisionHash = 0;
    uint64 WorkerBehaviorInputSequence = 0;
    int32 PendingCommandCheckpoint = 0;
    bool bMoveToSource = false;
    bool bMoveToSink = false;
    bool bWorkerBehaviorProduction = false;
    bool bDirectWorkerProductionApply = false;
  };

  FCrowdLogisticsTransactionStore Store;
  FCrowdBehaviorSourceRuntime* BehaviorSourceRuntime = nullptr;
  TMap<uint64, FCrowdStableEntityRef> BehaviorEntityRefsBySlot;
  TMap<FCrowdStableEntityRef, uint32>
    LastPublishedSourceSetRevisions;
  TArray<FCrowdStableEntityRef> PendingReliableLifecycleSpawns;
  FCrowdLogisticsTransactionStore CancellationStore;
  TMap<TWeakObjectPtr<APlayerController>,
    TWeakObjectPtr<AMassCrowdReplicationActor>> ReplicationChannels;
  double NextTransitionWorldSeconds = 0.0;
  double LastCheckpointWorldSeconds = -1000.0;
  uint64 NextReliableSequence = 1;
  uint64 NextCommitId = 1;
  uint64 LastConsumedSequence = 0;
  int32 ProductFixedStepIndex = 0;
  int32 ProductPlanRevision = 1;
  int32 PickupObservedFixedStep = INDEX_NONE;
  double ProductAccumulatorSeconds = 0.0;
  uint64 StateHash = 0;
  int32 UnreachableBackoffCount = 0;
  int32 CompetitionCount = 0;
  int32 DeathRecoveryCount = 0;
  int32 FallbackCount = 0;
  int32 CancellationCount = 0;
  int32 AppliedCount = 0;
  ECrowdLogisticsTaskState ClientTaskState =
    ECrowdLogisticsTaskState::Created;
  int32 ClientSourceOnHand = 0;
  int32 ClientSinkOnHand = 0;
  int32 ClientInTransit = 0;
  FCrowdStableEntityRef ClientCarrierRef;
  FCrowdStableEntityRef ClientCargoRef;
  TMap<FCrowdStableEntityRef, FVector> ClientLocations;
  TMap<uint64, FCrowdStableEntityRef> ClientEntitiesByStableId;
  TMap<uint64, FCrowdStableEntityRef> PresentedEntitiesByStableId;
  TMap<FCrowdStableEntityRef, FVector> AuthorityLocations;
  FVector SourceLocation = FVector(-300.0, 0.0, 60.0);
  FVector PrimarySinkLocation = FVector(300.0, 0.0, 60.0);
  FVector FallbackSinkLocation = FVector(300.0, -450.0, 60.0);
  bool bObjectiveMarkersResolved = false;
  float AcceptanceRadiusCm = 100.0f;
  float MaximumCarrierSpeedCmps = 260.0f;
  float MaximumObservedStepDistanceCm = 0.0f;
  int32 SourceAcceptanceCount = 0;
  int32 SinkAcceptanceCount = 0;
  uint64 LastProductCommitHash = 0;
  uint64 LastPlannerDecisionHash = 0;
  uint64 PresentationSequence = 0;
  uint64 ProductBoundaryGeneration = 1;
  TUniquePtr<FPendingProductBoundary> PendingProductBoundary;
  int32 CargoAttachCount = 0;
  int32 CargoDetachCount = 0;
  int32 CargoVisibleCount = 0;
  double VisualEvidenceCaptureWorldSeconds = 0.0;
  FString PendingVisualEvidencePath;
  FVector PendingVisualEvidenceFocus = FVector::ZeroVector;
  FCrowdStableEntityRef LastVisualEvidenceCarrierRef;
  bool bPendingVisualEvidenceViewActivated = false;
  bool bInitialized = false;
  bool bBehaviorEntitiesRegistered = false;
  bool bLastProductAppliedDirectWorker = false;
  bool bDeathInjected = false;
  bool bFallbackApplied = false;
  bool bServerPassLogged = false;
  bool bClientPassLogged = false;
  bool bPresentationProfileRegistered = false;
  bool bPresentationSpawned = false;
  bool bEmptyHandEvidenceRequested = false;
  bool bPickupEvidenceRequested = false;
  bool bCarryingEvidenceRequested = false;
  bool bDeliveredEvidenceRequested = false;

  bool TryInitialize();
  EProductBoundaryAdvance RunProductBoundary(float FixedStepSeconds);
  EProductBoundaryAdvance PollAndCommitProductBoundary(
    UCrowdDemoMassSubsystem& Mass);
  void PublishMovementCorrections(
    const FCrowdMassCommitPlan& Plan);
  bool IsCarrierWithin(
    const FCrowdStableEntityRef& Carrier,
    const FVector& Target) const;
  void AdvanceObservedState();
  bool Commit(
    FCrowdLogisticsTransactionStore& TargetStore,
    ECrowdLogisticsCommitKind Kind,
    FCrowdStableEntityRef Carrier);
  void RefreshReplicationChannels();
  bool PublishBaseline(AMassCrowdReplicationActor& Channel);
  void PublishState();
  void ConsumeState();
  bool SyncClientPresentation();
  void RequestVisualEvidence(
    const FString& EvidenceName,
    const FVector& Focus);
  void CapturePendingVisualEvidence();
  void LogCheckpoint();
  void TryLogPass();
  void EncodeState(TArray<uint8>& OutBytes) const;
  bool DecodeState(TConstArrayView<uint8> Bytes);
  static uint64 HashBytes(TConstArrayView<uint8> Bytes);
};
