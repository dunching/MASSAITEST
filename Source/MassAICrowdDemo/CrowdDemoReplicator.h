#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "GameFramework/Actor.h"
#include "CrowdDemoReplicator.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UCrowdDemoMassSubsystem;

struct FCrowdDemoProjectileVisualEventKey
{
  uint64 ProjectileId = 0;
  uint8 Kind = 0;

  bool operator==(const FCrowdDemoProjectileVisualEventKey& Other) const
  {
    return ProjectileId == Other.ProjectileId && Kind == Other.Kind;
  }

  friend uint32 GetTypeHash(const FCrowdDemoProjectileVisualEventKey& Key)
  {
    return HashCombineFast(GetTypeHash(Key.ProjectileId), GetTypeHash(Key.Kind));
  }
};

struct FCrowdDemoProjectileVisualRuntime
{
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float ServerTimeSeconds = 0.0f;
  float RadiusCm = 12.0f;
};

struct FCrowdDemoProjectileVisualRoundCounts
{
  int32 Spawn = 0;
  int32 Impact = 0;
  int32 Expire = 0;
};

UCLASS()
class MASSAICROWDDEMO_API ACrowdDemoReplicator : public AActor
{
  GENERATED_BODY()

public:
  ACrowdDemoReplicator();

  virtual void BeginPlay() override;
  virtual void Tick(float DeltaSeconds) override;
  virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
  UInstancedStaticMeshComponent* GetCrowdInstancesForClientVisuals() const;
  UInstancedStaticMeshComponent* GetCrowdHitFlashInstancesForClientVisuals() const;
  void ClearCrowdVisualInstances();
  int32 GetCrowdVisualInstanceCount() const;
  void RecordClientVisualSample(float ReplicationSampleAgeMs, float DisplayToAuthoritativeCm);
  void RecordRoundSimVisualSmoothing(float CorrectionOffsetCm, float YawOffsetDegrees, bool bSmoothingActive);
  void RecordRoundSimVisualContinuity(
    int32 AgentId,
    float SubmitIntervalMs,
    float SimDeltaCm,
    float DisplayDeltaCm,
    float ExpectedDisplayDeltaCm,
    int32 CollapsedSimSteps,
    bool bCorrectionBoundary,
    bool bPlanChanged,
    bool bDiscontinuity,
    int32 PreviousPlanRevision,
    int32 CurrentPlanRevision,
    float PreviousSimServerTimeSeconds,
    float CurrentSimServerTimeSeconds,
    const FVector& PreviousDisplayLocation,
    const FVector& CurrentDisplayLocation);
  void RecordVisualProcessorPerformance(float Milliseconds);
  void RecordVisualInstanceRebuild();
  void ResetClientMassEntityStates();
  void UpsertClientMassEntityState(const FCrowdDemoEntityState& State);
  void SetLocalVisualHostOnly(bool bInLocalVisualHostOnly);
  bool IsLocalVisualHostOnly() const { return bLocalVisualHostOnly; }
  void ApplyProjectileVisualEvents(TConstArrayView<FCrowdDemoProjectileVisualEvent> Events);
  bool GetProjectileVisualEventCounts(
    int32 RoundId,
    int32& OutSpawn,
    int32& OutImpact,
    int32& OutExpire,
    int32& OutActive) const;
  int32 GetActiveProjectileVisualCount() const { return ActiveProjectileVisuals.Num(); }

protected:
  UPROPERTY(VisibleAnywhere)
  TObjectPtr<USceneComponent> SceneRoot;

  UPROPERTY(VisibleAnywhere)
  TObjectPtr<UInstancedStaticMeshComponent> CrowdInstances;

  UPROPERTY(VisibleAnywhere)
  TObjectPtr<UInstancedStaticMeshComponent> CrowdHitFlashInstances;

  UPROPERTY(VisibleAnywhere)
  TObjectPtr<UInstancedStaticMeshComponent> ProjectileInstances;

  UPROPERTY(VisibleAnywhere)
  TObjectPtr<UInstancedStaticMeshComponent> ProjectileImpactInstances;

  UPROPERTY(VisibleAnywhere)
  TObjectPtr<UStaticMeshComponent> PreviewFloor;

private:
  int32 EntityCount = 500;
  float DurationSeconds = 12.0f;
  double StartedSeconds = 0.0;
  double LastMetricSeconds = 0.0;
  bool bServerSummaryLogged = false;
  bool bClientSummaryLogged = false;
  bool bVisualMaterialLoaded = false;
  bool bVatRuntimeMeshLoaded = false;
  bool bLocalVisualHostOnly = false;

  TArray<FCrowdDemoEntityState> EntityStates;
  TArray<float> ServerFrameMsSamples;
  TArray<float> SolverMsSamples;
  TArray<float> ClientFrameMsSamples;
  TArray<float> VisualProcessorMsSamples;
  TArray<float> ReplicationSampleAgeMsSamples;
  TArray<float> DisplayToAuthoritativeCmSamples;
  TArray<float> RoundVisualCorrectionOffsetCmSamples;
  TArray<float> RoundVisualYawOffsetDegSamples;
  TArray<float> VisualSubmitIntervalMsSamples;
  TArray<float> VisualSimDeltaCmSamples;
  TArray<float> VisualDisplayDeltaCmSamples;
  TArray<float> VisualCollapsedSimStepSamples;
  int32 RoundVisualSmoothingActiveCount = 0;
  int32 VisualCatchupSubmitCount = 0;
  int32 VisualCatchupDiscontinuityCount = 0;
  int32 NonCorrectionVisualDiscontinuityCount = 0;
  int32 RoundResetVisualJumpCount = 0;
  int32 VisualIsmRebuildCount = 0;
  TMap<uint64, FCrowdDemoProjectileVisualRuntime> ActiveProjectileVisuals;
  TMap<int32, FCrowdDemoProjectileVisualRoundCounts> ProjectileVisualRoundCounts;
  TSet<FCrowdDemoProjectileVisualEventKey> SeenProjectileVisualEvents;
  TArray<double> ProjectileImpactExpireWorldSeconds;

  void RefreshServerSummaryState();
  FCrowdDemoEntityState& FindOrAddEntityState(int32 Id, int32 LifecycleSerial);
  void LogSummaryIfReady();
  FCrowdDemoSummaryMetrics BuildSummaryMetrics() const;
  void UpdateProjectileVisuals();

  static float ComputeP95(TArray<float> Samples);
  static float ComputeMax(TConstArrayView<float> Samples);
  static int32 ResolveEntityCount();
  static float ResolveDurationSeconds();
};
