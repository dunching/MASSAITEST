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

  ApplyConfiguredView();
  ApplyDemoViewTarget();
}

void ACrowdDemoSpectatorPawn::PawnClientRestart()
{
  Super::PawnClientRestart();
  ApplyConfiguredView();
  ApplyDemoViewTarget();
}

void ACrowdDemoSpectatorPawn::ApplyConfiguredView()
{
  FString VisualMode;
  FParse::Value(FCommandLine::Get(), TEXT("CrowdDemoVisualMode="), VisualMode);
  const bool bT7Closeup = VisualMode.Equals(TEXT("T7Closeup"), ESearchCase::IgnoreCase);
  if (bT7Closeup)
  {
    SetActorLocation(FVector(0.0f, -4100.0f, 1450.0f));
    SetActorRotation(FRotator(-48.0f, 90.0f, 0.0f));
    Camera->FieldOfView = 42.0f;
  }
  else
  {
    SetActorLocation(FVector(0.0f, -1500.0f, 5600.0f));
    SetActorRotation(FRotator(-90.0f, 90.0f, 0.0f));
    Camera->FieldOfView = 70.0f;
  }
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoCamera: mode=%s location=%s rotation=%s fov=%.1f source=SpectatorPawn"),
    bT7Closeup ? TEXT("T7Closeup") : TEXT("DefaultOverview"),
    *GetActorLocation().ToCompactString(),
    *GetActorRotation().ToCompactString(),
    Camera->FieldOfView);
}

void ACrowdDemoSpectatorPawn::ApplyDemoViewTarget()
{
  if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
  {
    PlayerController->SetViewTarget(this);
  }
}
