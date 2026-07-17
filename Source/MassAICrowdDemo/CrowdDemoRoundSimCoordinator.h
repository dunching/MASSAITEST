#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "GameFramework/Actor.h"
#include "CrowdDemoRoundSimCoordinator.generated.h"

class UCrowdDemoMassSubsystem;
class ACrowdDemoReplicator;

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
  const FCrowdDemoCorrectionFrameMetrics& GetLastCorrectionFrameMetrics() const;
  void NotifyValidationClientReady(int32 AgentCount, int32 VisibleInstances);

protected:
  UFUNCTION()
  void OnRep_RoundBootstrapPacket();

  UFUNCTION()
  void OnRep_CurrentRoundPlan();

  UFUNCTION()
  void OnRep_RoundResultHeader();

  UFUNCTION(NetMulticast, Reliable)
  void MulticastCorrectionFrameChunk(const FCrowdDemoCorrectionFrameChunk& Chunk);

  UFUNCTION(NetMulticast, Reliable)
  void MulticastRoundPlan(const FCrowdDemoRoundPlanPacket& Plan);

  UFUNCTION(NetMulticast, Reliable)
  void MulticastProjectileVisualEvents(const TArray<FCrowdDemoProjectileVisualEvent>& Events);

private:
  UPROPERTY(ReplicatedUsing = OnRep_RoundBootstrapPacket, Transient)
  FCrowdDemoRoundBootstrapPacket RoundBootstrapPacket;

  UPROPERTY(ReplicatedUsing = OnRep_CurrentRoundPlan, Transient)
  FCrowdDemoRoundPlanPacket CurrentRoundPlan;

  UPROPERTY(ReplicatedUsing = OnRep_RoundResultHeader, Transient)
  FCrowdDemoRoundResultHeader RoundResultHeader;

  UPROPERTY(Transient)
  FCrowdDemoCorrectionFrameHeader CorrectionFrameHeader;

  FCrowdDemoRoundCompareMetrics LastCompareMetrics;
  FCrowdDemoCorrectionFrameMetrics LastCorrectionFrameMetrics;
  FCrowdDemoRoundResultPacket RoundResultPacket;
  FCrowdDemoRoundResultPacket PendingClientResultPacket;
  TMap<int32, FCrowdDemoRoundResultHeader> PendingClientResultHeaders;
  TMap<int32, double> PendingClientResultHeaderReceiveTimes;
  FCrowdDemoRoundPlanPacket PendingServerRoundPlan;
  TMap<int32, FCrowdDemoRoundPlanPacket> PendingClientRoundPlans;
  TMap<int32, FCrowdDemoPendingCorrectionAssembly> PendingCorrectionAssemblies;
  TSet<int32> DroppedCorrectionRevisions;
  TSet<int32> CompletedCorrectionRevisions;
  TSet<int32> FuturePendingCorrectionRevisions;
  TArray<FCrowdDemoCorrectionFrameChunk> PendingServerCorrectionChunks;
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
  int32 NextCorrectionRevision = 1;
  int32 LastReceivedCorrectionRevision = 0;
  int32 LastAppliedCorrectionRevision = 0;
  int32 CompletedRoundCount = 0;
  int32 CorrectionAppliedCount = 0;
  int32 CorrectionFramePublishedCount = 0;
  int32 CorrectionFrameReceivedCount = 0;
  int32 CorrectionFrameHeaderReceivedCount = 0;
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
  int32 NextPendingServerCorrectionChunkIndex = 0;
  int32 PendingServerCorrectionRevision = 0;
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
  void PublishServerCorrectionFrame();
  void FlushServerCorrectionChunks();
  FCrowdDemoRoundRules BuildRoundRules(const UCrowdDemoMassSubsystem& MassSubsystem, int32 RoundId, float StartServerTimeSeconds, const FVector& StartLocation, int32 AgentCount) const;
  void TryProcessClientResult();
  bool TryBuildClientRoundResult(const FCrowdDemoPendingCorrectionAssembly& Assembly, FCrowdDemoRoundResultPacket& OutResult);
  void CacheClientCorrectionHeader(const FCrowdDemoCorrectionFrameHeader& Header);
  void CacheClientCorrectionChunk(const FCrowdDemoCorrectionFrameChunk& Chunk);
  void TryProcessClientCorrectionAssemblies();
  bool TryApplyClientCorrectionAssembly(FCrowdDemoPendingCorrectionAssembly& Assembly);
  void DropExpiredCorrectionAssemblies();
  void RefreshLastCompareCounters();
  void RefreshLastCorrectionCounters();
  void RecordRoundBoundaryMetrics(TConstArrayView<FCrowdDemoRoundAgentState> PreviousAgents, TConstArrayView<FCrowdDemoRoundAgentState> NextAgents);
};
