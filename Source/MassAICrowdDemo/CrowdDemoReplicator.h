#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "GameFramework/Actor.h"
#include "CrowdDemoReplicator.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UCrowdDemoMassSubsystem;

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
  void ClearCrowdVisualInstances();
  int32 GetCrowdVisualInstanceCount() const;
  void RecordClientVisualSample(float ReplicationSampleAgeMs, float DisplayToAuthoritativeCm);
  void RecordRoundSimVisualSmoothing(float CorrectionOffsetCm, float YawOffsetDegrees, bool bSmoothingActive);
  void ResetClientMassEntityStates();
  void UpsertClientMassEntityState(const FCrowdDemoEntityState& State);
  void SetLocalVisualHostOnly(bool bInLocalVisualHostOnly);
  bool IsLocalVisualHostOnly() const { return bLocalVisualHostOnly; }

protected:
  UPROPERTY(VisibleAnywhere)
  TObjectPtr<USceneComponent> SceneRoot;

  UPROPERTY(VisibleAnywhere)
  TObjectPtr<UInstancedStaticMeshComponent> CrowdInstances;

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
  bool bLocalVisualHostOnly = false;

  TArray<FCrowdDemoEntityState> EntityStates;
  TArray<float> ServerFrameMsSamples;
  TArray<float> SolverMsSamples;
  TArray<float> ReplicationSampleAgeMsSamples;
  TArray<float> DisplayToAuthoritativeCmSamples;
  TArray<float> RoundVisualCorrectionOffsetCmSamples;
  TArray<float> RoundVisualYawOffsetDegSamples;
  int32 RoundVisualSmoothingActiveCount = 0;

  void RefreshServerSummaryState();
  FCrowdDemoEntityState& FindOrAddEntityState(int32 Id, int32 LifecycleSerial);
  void LogSummaryIfReady();
  FCrowdDemoSummaryMetrics BuildSummaryMetrics() const;

  static float ComputeP95(TArray<float> Samples);
  static int32 ResolveEntityCount();
  static float ResolveDurationSeconds();
};
