#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "CrowdDemoSpectatorPawn.generated.h"

class UCameraComponent;
class UFloatingPawnMovement;

UCLASS()
class MASSAICROWDDEMO_API ACrowdDemoSpectatorPawn : public APawn
{
  GENERATED_BODY()

public:
  ACrowdDemoSpectatorPawn();

  virtual void BeginPlay() override;
  virtual void PawnClientRestart() override;

protected:
  UPROPERTY(VisibleAnywhere)
  TObjectPtr<USceneComponent> SceneRoot;

  UPROPERTY(VisibleAnywhere)
  TObjectPtr<UCameraComponent> Camera;

  UPROPERTY(VisibleAnywhere)
  TObjectPtr<UFloatingPawnMovement> Movement;

private:
  void ApplyDemoViewTarget();
};
