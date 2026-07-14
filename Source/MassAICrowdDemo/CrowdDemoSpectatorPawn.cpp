#include "CrowdDemoSpectatorPawn.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/FloatingPawnMovement.h"

ACrowdDemoSpectatorPawn::ACrowdDemoSpectatorPawn()
{
  PrimaryActorTick.bCanEverTick = false;

  SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
  SetRootComponent(SceneRoot);

  Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
  Camera->SetupAttachment(SceneRoot);
  Camera->SetRelativeLocation(FVector::ZeroVector);
  Camera->SetRelativeRotation(FRotator::ZeroRotator);
  Camera->FieldOfView = 70.0f;

  Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
  Movement->MaxSpeed = 1800.0f;
}

void ACrowdDemoSpectatorPawn::BeginPlay()
{
  Super::BeginPlay();

  SetActorLocation(FVector(0.0f, -1500.0f, 5600.0f));
  SetActorRotation(FRotator(-90.0f, 90.0f, 0.0f));
  ApplyDemoViewTarget();
}

void ACrowdDemoSpectatorPawn::PawnClientRestart()
{
  Super::PawnClientRestart();
  ApplyDemoViewTarget();
}

void ACrowdDemoSpectatorPawn::ApplyDemoViewTarget()
{
  if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
  {
    PlayerController->SetViewTarget(this);
  }
}
