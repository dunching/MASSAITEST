#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "GameFramework/Actor.h"
#include "CrowdDemoTargetActor.generated.h"

class UStaticMeshComponent;

UCLASS()
class MASSAICROWDDEMO_API ACrowdDemoTargetActor : public AActor
{
  GENERATED_BODY()

public:
  ACrowdDemoTargetActor();

  virtual void BeginPlay() override;
  virtual void Tick(float DeltaSeconds) override;

  void ConfigureScenario(ECrowdDemoScenario InScenario);
  ECrowdDemoScenario GetScenario() const { return Scenario; }
  FVector GetTargetVelocity() const { return CurrentVelocity; }
  int32 GetTargetId() const { return TargetId; }

private:
  UPROPERTY(VisibleAnywhere)
  TObjectPtr<USceneComponent> SceneRoot;

  UPROPERTY(VisibleAnywhere)
  TObjectPtr<UStaticMeshComponent> TargetMesh;

  UPROPERTY(EditAnywhere, Category = "CrowdDemo")
  float MoveSpeedCmPerSecond = 260.0f;

  UPROPERTY(EditAnywhere, Category = "CrowdDemo")
  int32 TargetId = 1;

  UPROPERTY(EditAnywhere, Category = "CrowdDemo")
  ECrowdDemoScenario Scenario = ECrowdDemoScenario::SimRoundObstacle;

  TArray<FVector> Waypoints;
  FVector CurrentVelocity = FVector::ZeroVector;
  int32 WaypointIndex = 0;

  void BuildWaypointsForScenario();
  void AdvanceAlongWaypoints(float DeltaSeconds);
};
