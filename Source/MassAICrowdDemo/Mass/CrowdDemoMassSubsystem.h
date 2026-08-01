#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "MassEntityHandle.h"
#include "MassCrowdProjectileMassStore.h"
#include "MassCrowdRuntimeBridge.h"
#include "Subsystems/WorldSubsystem.h"
#include "CrowdDemoMassSubsystem.generated.h"

struct FMassEntityManager;
struct FMassEntityTemplateData;
struct FMassEntityTemplateID;
class AActor;
class UCrowdDemoClientVisualMassProcessor;
class UCrowdDemoWorkerInputSyncProcessor;
class UCrowdDemoWorkerResultApplyProcessor;

MASSAICROWDDEMO_API void BuildCrowdDemoAuthorityTemplateData(
  FMassEntityTemplateData& TemplateData,
  const FMassEntityTemplateID& TemplateID,
  ECrowdDemoMassCapability Capabilities);

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
  bool BuildProductBoundarySnapshot(
    int32 FixedStepIndex,
    int32 PlanRevision,
    FCrowdMassBoundarySnapshot& OutSnapshot,
    TArray<FCrowdMassCommitTarget>& OutTargets) const;
  bool ApplyProductBoundaryCommit(
    const FCrowdMassCommitPlan& Plan,
    TConstArrayView<FCrowdMassCommitTarget> Targets);
  bool RecycleTrackedAgent(
    const FCrowdStableEntityRef& EntityRef,
    FCrowdStableEntityRef& OutReplacementRef);
  bool PrepareProjectileCapacity(int32 RequiredCount);
  void ApplyProjectileStates(
    TConstArrayView<struct FCrowdProjectileState> Projectiles);
  bool GatherProjectileStates(
    TArray<struct FCrowdProjectileState>& OutProjectiles) const;
  void ResetProjectileStates();
  int32 GetTrackedProjectilePoolCount() const
  {
    return ProjectileStore.GetCapacity();
  }
private:
  TArray<FMassEntityHandle> TrackedAgents;
  FCrowdMassProjectileStore ProjectileStore;
  TWeakObjectPtr<AActor> TargetActor;
  ECrowdDemoScenario CurrentScenario = ECrowdDemoScenario::SimRoundObstacle;
  ECrowdDemoSoftPressureTestCase SoftPressureTestCase =
    ECrowdDemoSoftPressureTestCase::CorridorRoute;

  UPROPERTY(Transient)
  TObjectPtr<UCrowdDemoWorkerInputSyncProcessor>
    WorkerInputSyncProcessor;

  UPROPERTY(Transient)
  TObjectPtr<UCrowdDemoWorkerResultApplyProcessor>
    WorkerResultApplyProcessor;

  UPROPERTY(Transient)
  TObjectPtr<UCrowdDemoClientVisualMassProcessor> ClientVisualProcessor;

  FVector MakeSpawnLocation(int32 AgentIndex, int32 AgentCount) const;
  void DestroyTrackedAgents();
  void InitializeAgentFragments(FMassEntityManager& EntityManager, FMassEntityHandle Entity, int32 AgentIndex, int32 AgentCount) const;
  void RegisterRoundSimProcessors();
  void UnregisterRoundSimProcessors();
};
