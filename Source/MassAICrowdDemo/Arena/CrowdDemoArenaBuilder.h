#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrowdDemoTypes.h"
#include "CrowdDemoArenaBuilder.generated.h"

class UStaticMesh;

UCLASS()
class MASSAICROWDDEMO_API ACrowdDemoArenaBuilder : public AActor
{
  GENERATED_BODY()

public:
  ACrowdDemoArenaBuilder();

  virtual void BeginPlay() override;

private:
  UPROPERTY(VisibleAnywhere)
  TObjectPtr<USceneComponent> SceneRoot;

  UPROPERTY()
  TObjectPtr<UStaticMesh> CubeMesh;

  TArray<FCrowdDemoSharedFlowObstacleSpec> BuildObstacleList() const;
  void CreateObstacleMesh(const FCrowdDemoSharedFlowObstacleSpec& Obstacle, int32 Index);
};
