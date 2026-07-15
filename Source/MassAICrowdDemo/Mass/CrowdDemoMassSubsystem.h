#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "MassEntityHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "CrowdDemoMassSubsystem.generated.h"

struct FMassEntityManager;
class AActor;
class UCrowdDemoClientVisualMassProcessor;
class UCrowdDemoRoundSimFixedStepPipelineProcessor;

USTRUCT()
struct FCrowdDemoMassSpawnResult
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  int32 RequestedAgents = 0;

  UPROPERTY(Transient)
  int32 SpawnedAgents = 0;

  UPROPERTY(Transient)
  int32 AliveAgents = 0;

  UPROPERTY(Transient)
  bool bUsedMassEntitySubsystem = false;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoMassSubsystem : public UWorldSubsystem
{
  GENERATED_BODY()

public:
  virtual void Initialize(FSubsystemCollectionBase& Collection) override;
  virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
  virtual void OnWorldBeginPlay(UWorld& InWorld) override;
  virtual void Deinitialize() override;

  void SetTargetActor(AActor* InTargetActor);
  AActor* GetTargetActor() const;
  void SetScenario(ECrowdDemoScenario InScenario);
  ECrowdDemoScenario GetScenario() const { return CurrentScenario; }
  void SetSoftPressureTestCase(ECrowdDemoSoftPressureTestCase InTestCase)
  { SoftPressureTestCase = InTestCase; }
  ECrowdDemoSoftPressureTestCase GetSoftPressureTestCase() const
  { return SoftPressureTestCase; }
  FCrowdDemoMassSpawnResult SpawnAgents(int32 AgentCount);
  int32 GetTrackedAgentCount() const;
  int32 GetAliveAgentCount() const;
  int32 BuildVisualSnapshot(TArray<FCrowdDemoEntityState>& OutSnapshot, float ServerTimeSeconds) const;
  int32 BuildRoundAgentStates(TArray<FCrowdDemoRoundAgentState>& OutStates) const;

private:
  TArray<FMassEntityHandle> TrackedAgents;
  TWeakObjectPtr<AActor> TargetActor;
  ECrowdDemoScenario CurrentScenario = ECrowdDemoScenario::SimRoundObstacle;
  ECrowdDemoSoftPressureTestCase SoftPressureTestCase =
    ECrowdDemoSoftPressureTestCase::CorridorRoute;

  UPROPERTY(Transient)
  TObjectPtr<UCrowdDemoRoundSimFixedStepPipelineProcessor> RoundSimPipelineProcessor;

  UPROPERTY(Transient)
  TObjectPtr<UCrowdDemoClientVisualMassProcessor> ClientVisualProcessor;

  FVector MakeSpawnLocation(int32 AgentIndex, int32 AgentCount) const;
  void DestroyTrackedAgents();
  void InitializeAgentFragments(FMassEntityManager& EntityManager, FMassEntityHandle Entity, int32 AgentIndex, int32 AgentCount) const;
  void RegisterRoundSimProcessors();
  void UnregisterRoundSimProcessors();
};
