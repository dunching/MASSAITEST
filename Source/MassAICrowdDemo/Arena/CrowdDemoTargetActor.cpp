#include "Arena/CrowdDemoTargetActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ACrowdDemoTargetActor::ACrowdDemoTargetActor()
{
  PrimaryActorTick.bCanEverTick = false;
  bReplicates = true;

  SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
  SetRootComponent(SceneRoot);

  TargetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TargetMesh"));
  TargetMesh->SetupAttachment(SceneRoot);
  TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
  TargetMesh->SetMobility(EComponentMobility::Movable);
  TargetMesh->SetWorldScale3D(FVector(1.5f, 1.5f, 0.5f));

  static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
  if (SphereMesh.Succeeded())
  {
    TargetMesh->SetStaticMesh(SphereMesh.Object);
  }
}

void ACrowdDemoTargetActor::BeginPlay()
{
  Super::BeginPlay();

  SetActorLocation(FVector(2200.0f, 1600.0f, 60.0f));
}

void ACrowdDemoTargetActor::Tick(const float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);

  (void)DeltaSeconds;
}

void ACrowdDemoTargetActor::AdvanceAlongWaypoints(const float DeltaSeconds)
{
  if (Waypoints.IsEmpty())
  {
    CurrentVelocity = FVector::ZeroVector;
    return;
  }

  const FVector CurrentLocation = GetActorLocation();
  const FVector GoalLocation = Waypoints[WaypointIndex % Waypoints.Num()];
  const FVector ToGoal = GoalLocation - CurrentLocation;
  const float Distance = ToGoal.Size2D();
  if (Distance <= 20.0f)
  {
    SetActorLocation(GoalLocation);
    WaypointIndex = (WaypointIndex + 1) % Waypoints.Num();
    CurrentVelocity = FVector::ZeroVector;
    return;
  }

  const FVector Direction = ToGoal.GetSafeNormal2D();
  CurrentVelocity = Direction * MoveSpeedCmPerSecond;
  const FVector Step = CurrentVelocity * DeltaSeconds;
  SetActorLocation(CurrentLocation + Step.GetClampedToMaxSize2D(Distance));
}

void ACrowdDemoTargetActor::ConfigureScenario(const ECrowdDemoScenario InScenario)
{
  Scenario = InScenario;
  SetActorLocation(FVector(2200.0f, 1600.0f, 60.0f));
  ForceNetUpdate();
}

void ACrowdDemoTargetActor::BuildWaypointsForScenario()
{
  Waypoints.Reset();
  SetActorLocation(FVector(2200.0f, 1600.0f, 60.0f));
  CurrentVelocity = FVector::ZeroVector;
}
