#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "GameFramework/Actor.h"
#include "MassCrowdRelevantSnapshot.h"
#include "MassCrowdReplicationChannel.h"
#include "CrowdDemoRoundSimCoordinator.generated.h"

class UCrowdDemoMassSubsystem;
class ACrowdDemoReplicator;
class APlayerController;
class AMassCrowdReplicationActor;

USTRUCT()
struct FCrowdDemoBootstrapSnapshotMetadata
{
  GENERATED_BODY()

  UPROPERTY() uint8 bValid = 0;
  UPROPERTY() float ServerTimeSeconds = 0.0f;
  UPROPERTY() FCrowdRelevantSnapshotHeader SnapshotHeader;
};

UCLASS()
class MASSAICROWDDEMO_API ACrowdDemoRoundSimCoordinator : public AActor
{
  GENERATED_BODY()

public:
  ACrowdDemoRoundSimCoordinator();

  virtual void BeginPlay() override;
  virtual void Tick(float DeltaSeconds) override;
  virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

  bool IsRoundSimActive() const;
  const FCrowdDemoRoundCompareMetrics& GetLastCompareMetrics() const;
  const FCrowdDemoRoundCheckpointFrameMetrics& GetLastCorrectionFrameMetrics() const;
  void NotifyValidationClientReady(int32 AgentCount, int32 VisibleInstances);

protected:
  UFUNCTION()
  void OnRep_CurrentRoundPlan();

  UFUNCTION(NetMulticast, Reliable)
  void MulticastRoundPlan(const FCrowdDemoRoundPlanPacket& Plan);

private:
  FCrowdDemoBootstrapSnapshotMetadata BootstrapSnapshotMetadata;

  UPROPERTY(ReplicatedUsing = OnRep_CurrentRoundPlan, Transient)
  FCrowdDemoRoundPlanPacket CurrentRoundPlan;

  UPROPERTY(Transient)
  FCrowdDemoRoundResultHeader RoundResultHeader;

  UPROPERTY(Transient)
  FCrowdDemoRoundCheckpointHeader RoundCheckpointHeader;

  FCrowdDemoRoundCompareMetrics LastCompareMetrics;
  FCrowdDemoRoundCheckpointFrameMetrics LastCorrectionFrameMetrics;
  FCrowdDemoRoundResultPacket RoundResultPacket;
  FCrowdDemoRoundResultPacket PendingClientResultPacket;
  TMap<int32, FCrowdDemoRoundResultHeader> PendingClientResultHeaders;
  TMap<int32, double> PendingClientResultHeaderReceiveTimes;
  FCrowdDemoRoundPlanPacket PendingServerRoundPlan;
  TMap<int32, FCrowdDemoRoundPlanPacket> PendingClientRoundPlans;
  TMap<int32, FCrowdDemoPendingRoundCheckpointAssembly> PendingRoundCheckpointAssemblies;
  TSet<int32> DroppedRoundCheckpointRevisions;
  TSet<int32> CompletedRoundCheckpointRevisions;
  TSet<int32> FuturePendingRoundCheckpointRevisions;
  TArray<FCrowdRelevantSnapshotChunk> CurrentProductBootstrapChunks;
  TMap<TWeakObjectPtr<APlayerController>,
    TWeakObjectPtr<AMassCrowdReplicationActor>> ProductReplicationChannels;
  TMap<TWeakObjectPtr<AMassCrowdReplicationActor>, uint64>
    NextProductReliableSequence;
  TMap<int32, FCrowdDemoRoundCheckpointHeader>
    ProductRoundCheckpointHeaders;
  TMap<int32, TArray<FCrowdDemoRoundAgentState>>
    ProductRoundCheckpointAgents;
  TArray<float> CorrectionFrameIntervalMsSamples;
  TArray<float> CorrectionFrameAssemblyMsSamples;
  TArray<float> CorrectionFrameAgeMsSamples;
  TArray<float> CorrectionFrameReplayMsSamples;
  TArray<float> CorrectionFramePublishIntervalMsSamples;
  double LastRoundCompletedWorldSeconds = -1000.0;
  double LastCorrectionFrameWorldSeconds = -1000.0;
  double LastClientCorrectionFrameWorldSeconds = -1000.0;
  double LastCorrectionFramePublishWorldSeconds = -1000.0;
  int32 NextRoundId = 1;
  int32 Revision = 0;
  int32 LastCheckpointRevision = 0;
  int32 LastReceivedRoundCheckpointStateFrameRevision = 0;
  int32 LastAppliedRoundCheckpointStateFrameRevision = 0;
  int32 CompletedRoundCount = 0;
  int32 CorrectionAppliedCount = 0;
  int32 CorrectionFramePublishedCount = 0;
  int32 CorrectionFrameReceivedCount = 0;
  int32 RoundCheckpointHeaderReceivedCount = 0;
  int32 CorrectionFrameChunkReceivedCount = 0;
  int32 LatestChunkRevisionSeen = 0;
  int32 CorrectionChunkReceivedCount = 0;
  int32 CorrectionUniqueChunkCount = 0;
  int32 CorrectionExpectedChunkCount = 0;
  int32 CorrectionAssemblyCompleteCount = 0;
  int32 CorrectionAssemblySupersededCount = 0;
  int32 CorrectionFrameCompleteCount = 0;
  int32 CorrectionFrameAppliedCount = 0;
  int32 CorrectionFrameDroppedOldCount = 0;
  int32 CorrectionFrameDroppedMismatchCount = 0;
  int32 CorrectionFrameFuturePendingCount = 0;
  int32 CorrectionFrameFutureDropCount = 0;
  int32 CorrectionFrameIncompleteDropCount = 0;
  int32 CorrectionFrameStaleDropCount = 0;
  int32 CorrectionFrameReplayToNowCount = 0;
  int32 CorrectionFrameRevisionGapCount = 0;
  int32 CorrectionFrameLatestRevisionSeen = 0;
  int32 CorrectionFrameLatestRevisionApplied = 0;
  int32 LastServerCorrectionChunkCount = 0;
  int32 LastServerCorrectionChunkSize = 0;
  uint32 LastConsumedProductBaselineRevision = 0;
  int32 DroppedResultWarningCount = 0;
  int32 DroppedCorrectionWarningCount = 0;
  int32 RoundPlanRevisionSeen = 0;
  int32 RoundPlanAppliedCount = 0;
  int32 RoundPlanGapCount = 0;
  int32 RoundPlanLateCount = 0;
  int32 SyntheticSkippedCheckpointCount = 0;
  int32 RoundResultBuiltCount = 0;
  int32 RoundResultHeaderPublishedCount = 0;
  int32 RoundResultHeaderReceivedCount = 0;
  int32 RoundResultCheckpointChunkReceivedCount = 0;
  int32 RoundResultAssemblyCompleteCount = 0;
  int32 RoundResultPipelineQueuedCount = 0;
  int32 RoundResultHeaderWaitTimeoutCount = 0;
  int32 RoundResultChunkWaitTimeoutCount = 0;
  int32 RoundResultRevisionMismatchCount = 0;
  int32 LastAppliedRoundPlanRevision = 0;
  int32 LastReportedPlanGapRevision = 0;
  bool bHasContinuousRoundPlan = false;
  TArray<float> RoundBoundaryCenterJumpCmSamples;
  TArray<float> RoundBoundaryYawJumpDegSamples;
  TArray<float> RoundBoundaryVelocityJumpCmpsSamples;
  bool bRoundResultPublished = false;
  bool bClientComparedLatestResult = false;
  bool bNextRoundPlanPublished = false;
  bool bRequireValidationClientReady = false;
  bool bValidationClientReady = false;
  bool bValidationReadyTimeoutLogged = false;
  double ValidationReadyServerTimeSeconds = -1.0;
  float ValidationReadyLeadSeconds = 3.0f;
  float ValidationReadyTimeoutSeconds = 60.0f;
  bool bPendingProjectileVisualValidation = false;
  int32 PendingProjectileVisualRoundId = INDEX_NONE;
  double PendingProjectileVisualValidationStartSeconds = 0.0;

  FCrowdDemoRoundBootstrapPacket RoundBootstrapPacket;

  void TickServer();
  void TickClient();
  void TryValidateProjectileVisualEvents();
  void StartServerRound(UCrowdDemoMassSubsystem& MassSubsystem, float StartServerTimeSeconds);
  void ActivateServerRoundPlan(UCrowdDemoMassSubsystem& MassSubsystem, const FCrowdDemoRoundPlanPacket& Plan);
  void PublishServerRoundPlan(const FCrowdDemoRoundPlanPacket& Plan);
  FCrowdDemoRoundPlanPacket BuildRoundPlanPacket(const UCrowdDemoMassSubsystem& MassSubsystem, int32 RoundId, int32 PlanRevision, int32 PreviousCheckpointRevision, float StartServerTimeSeconds, const FVector& StartLocation, int32 AgentCount) const;
  void QueueClientRoundPlan(const FCrowdDemoRoundPlanPacket& Plan);
  void TryActivateClientRoundPlans(float ClientServerTimeSeconds);
  void ActivateClientRoundPlan(const FCrowdDemoRoundPlanPacket& Plan, float ClientServerTimeSeconds, bool bLateJoinBaseline);
  void PublishServerResult(UCrowdDemoMassSubsystem& MassSubsystem, float EndServerTimeSeconds);
  void RefreshProductReplicationChannels();
  bool PublishProductBaseline(AMassCrowdReplicationActor& Channel);
  bool PublishProductReliable(
    AMassCrowdReplicationActor& Channel,
    ECrowdReliableStateKind Kind,
    const FCrowdStableEntityRef& EntityRef,
    uint32 Revision,
    TConstArrayView<uint8> Payload);
  void PublishProductRoundCheckpoint(
    const FCrowdDemoRoundResultPacket& Result);
  void PublishProductRoundResultHeader(
    const FCrowdDemoRoundResultHeader& Header);
  void PublishProductProjectileEvents(
    TConstArrayView<FCrowdDemoProjectileVisualEvent> Events);
  void PublishProductT7PresentationEvents(
    TConstArrayView<FCrowdDemoT7PresentationEvent> Events);
  void ConsumeProductReplicationChannels();
  void ConsumeProductReliableRecord(
    AMassCrowdReplicationActor& Channel,
    const FCrowdReliableStateRecord& Record);
  void ConsumeProductRoundResultHeader(
    const FCrowdDemoRoundResultHeader& Header);
  void TryFinalizeProductRoundCheckpoint(int32 StateFrameRevision);
  FCrowdDemoRoundRules BuildRoundRules(const UCrowdDemoMassSubsystem& MassSubsystem, int32 RoundId, float StartServerTimeSeconds, const FVector& StartLocation, int32 AgentCount) const;
  void TryProcessClientResult();
  bool TryBuildClientRoundResult(const FCrowdDemoPendingRoundCheckpointAssembly& Assembly, FCrowdDemoRoundResultPacket& OutResult);
  void CacheClientRoundCheckpointHeader(const FCrowdDemoRoundCheckpointHeader& Header);
  void CacheClientRoundCheckpointChunk(const FCrowdDemoRoundCheckpointChunk& Chunk);
  void TryProcessClientRoundCheckpoints();
  bool TryApplyClientRoundCheckpoint(FCrowdDemoPendingRoundCheckpointAssembly& Assembly);
  void DropExpiredRoundCheckpoints();
  void RefreshLastCompareCounters();
  void RefreshLastCorrectionCounters();
  void RecordRoundBoundaryMetrics(TConstArrayView<FCrowdDemoRoundAgentState> PreviousAgents, TConstArrayView<FCrowdDemoRoundAgentState> NextAgents);
};
