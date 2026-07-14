#include "Arena/CrowdDemoArenaBuilder.h"

#include "Components/StaticMeshComponent.h"
#include "CrowdDemoScenarioConfigActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "UObject/ConstructorHelpers.h"

ACrowdDemoArenaBuilder::ACrowdDemoArenaBuilder()
{
  PrimaryActorTick.bCanEverTick = false;
  bReplicates = true;
  bAlwaysRelevant = true;
  SetReplicateMovement(false);

  SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
  SetRootComponent(SceneRoot);
  SceneRoot->SetMobility(EComponentMobility::Static);

  static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
  if (CubeMeshFinder.Succeeded())
  {
    CubeMesh = CubeMeshFinder.Object;
  }
}

void ACrowdDemoArenaBuilder::BeginPlay()
{
  Super::BeginPlay();

  const TArray<FCrowdDemoSharedFlowObstacleSpec> Obstacles = BuildObstacleList();
  for (int32 Index = 0; Index < Obstacles.Num(); ++Index)
  {
    CreateObstacleMesh(Obstacles[Index], Index);
  }
  UE_LOG(LogTemp, Display, TEXT("CrowdDemoArena: visuals=%d role=%s"), Obstacles.Num(), HasAuthority() ? TEXT("server") : TEXT("client"));

}

TArray<FCrowdDemoSharedFlowObstacleSpec> ACrowdDemoArenaBuilder::BuildObstacleList() const
{
  return FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1).ObstacleSpecs;
}

void ACrowdDemoArenaBuilder::CreateObstacleMesh(const FCrowdDemoSharedFlowObstacleSpec& Obstacle, const int32 Index)
{
  if (!CubeMesh)
  {
    return;
  }

  UStaticMeshComponent* ObstacleMesh = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Obstacle_%02d"), Index));
  if (!ObstacleMesh)
  {
    return;
  }

  ObstacleMesh->SetupAttachment(SceneRoot);
  ObstacleMesh->SetStaticMesh(CubeMesh);
  ObstacleMesh->SetCollisionEnabled(HasAuthority() ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
  ObstacleMesh->SetMobility(EComponentMobility::Static);
  ObstacleMesh->SetWorldLocation(Obstacle.Center);
  ObstacleMesh->SetWorldScale3D(FVector(Obstacle.Extent.X / 50.0f, Obstacle.Extent.Y / 50.0f, Obstacle.Extent.Z / 50.0f));
  ObstacleMesh->RegisterComponent();
}
