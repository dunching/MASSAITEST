#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CrowdDemoTypes.h"
#include "CrowdDemoRoundSimProcessors.generated.h"

class UCrowdDemoRoundSimPipelineSubsystem;

struct FCrowdDemoWorkerResultApplyStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundPlanApplyStage
{
  void BindQuery(FMassEntityQuery& Query);
  void UseQuery(FMassEntityQuery& Query) { EntityQuery = &Query; }
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
  FMassEntityQuery* EntityQuery = nullptr;
};

struct FCrowdDemoRoundSharedFlowFieldBuildStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundOpenSpawnRelaxationPhasePrepareStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundFlowPreferredVelocityStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundTargetFactApplyStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundTargetPolarTopologyBuildStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundTargetRegionPopulationBuildStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundTargetRegionTransportSolveStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundTargetRegionGuidanceStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundMovementWorkStage
{
  float GetLastGuidanceWorkMilliseconds() const
  { return LastGuidanceWorkMilliseconds; }
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
  float LastGuidanceWorkMilliseconds = 0.0f;
};

struct FCrowdDemoRoundParticleConstraintStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundObstacleConstraintStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundFacingFinalizeStage
{
  void BindQuery(FMassEntityQuery& Query);
  void UseQuery(FMassEntityQuery& Query) { EntityQuery = &Query; }
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
  bool ApplyPreparedCommit(
    UCrowdDemoRoundSimPipelineSubsystem& Pipeline,
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context);
  FMassEntityQuery* EntityQuery = nullptr;
};

struct FCrowdDemoRoundPostFinalizeMetricsStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundAuthorityCommitStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundClientPredictionCommitStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};

struct FCrowdDemoRoundCheckpointPublisherStage
{
  void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
};
UCLASS()
class MASSAICROWDDEMO_API
UCrowdDemoWorkerInputSyncProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoWorkerInputSyncProcessor();
  virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override
  { return false; }
protected:
  virtual void ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context) override;
private:
  FMassEntityQuery InputSyncQuery;
};

UCLASS()
class MASSAICROWDDEMO_API
UCrowdDemoWorkerResultApplyProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoWorkerResultApplyProcessor();
  virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override
  { return false; }
protected:
  virtual void ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context) override;
private:
  FMassEntityQuery ResultCommitQuery;
};
