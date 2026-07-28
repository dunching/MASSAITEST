#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CrowdDemoTypes.h"
#include "CrowdDemoRoundSimProcessors.generated.h"

class UCrowdDemoRoundSimPipelineSubsystem;

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundPlanApplyProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundPlanApplyProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundSharedFlowFieldBuildProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundSharedFlowFieldBuildProcessor();
  virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override { return false; }
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundBoundaryGatherProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundBoundaryGatherProcessor();
protected:
  virtual void ConfigureQueries(
    const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(
    FMassEntityManager& EntityManager,
    FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundFlowPreferredVelocityProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundFlowPreferredVelocityProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundTargetFactApplyProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundTargetFactApplyProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundTargetPolarTopologyBuildProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundTargetPolarTopologyBuildProcessor();
  virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override { return false; }
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundTargetRegionPopulationBuildProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundTargetRegionPopulationBuildProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private: FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundTargetRegionTransportSolveProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundTargetRegionTransportSolveProcessor();
  virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override { return false; }
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundTargetRegionGuidanceProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundTargetRegionGuidanceProcessor();
protected:
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};





















UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundCombatBoundaryProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundCombatBoundaryProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundMovementWorkProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundMovementWorkProcessor();
  float GetLastGuidanceWorkMilliseconds() const
  { return LastGuidanceWorkMilliseconds; }
protected:
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  float LastGuidanceWorkMilliseconds = 0.0f;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundParticleConstraintProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundParticleConstraintProcessor();
protected:
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundObstacleConstraintProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundObstacleConstraintProcessor();
protected:
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundFacingFinalizeProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundFacingFinalizeProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  bool ApplyPreparedCommit(
    UCrowdDemoRoundSimPipelineSubsystem& Pipeline,
    FMassExecutionContext& Context);
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundPostFinalizeMetricsProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundPostFinalizeMetricsProcessor();
protected:
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};






UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundAuthorityCommitProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundAuthorityCommitProcessor();
protected:
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundClientPredictionCommitProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundClientPredictionCommitProcessor();
protected:
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundCheckpointPublisherProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundCheckpointPublisherProcessor();
protected:
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundSimFixedStepPipelineProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundSimFixedStepPipelineProcessor();
  virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override { return false; }
protected:
  virtual void InitializeInternal(UObject& Owner, const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundCombatBoundaryProcessor> CombatBoundaryProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundMovementWorkProcessor> MovementWorkProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundPlanApplyProcessor> PlanApplyProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundBoundaryGatherProcessor> BoundaryGatherProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor> OpenSpawnRelaxationPhasePrepareProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundSharedFlowFieldBuildProcessor> SharedFlowFieldBuildProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetFactApplyProcessor> TargetFactApplyProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetPolarTopologyBuildProcessor> TargetPolarTopologyBuildProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetRegionPopulationBuildProcessor> TargetRegionPopulationBuildProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetRegionTransportSolveProcessor> TargetRegionTransportSolveProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetRegionGuidanceProcessor> TargetRegionGuidanceProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundFlowPreferredVelocityProcessor> FlowPreferredVelocityProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundParticleConstraintProcessor> ParticleConstraintProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundObstacleConstraintProcessor> ObstacleConstraintProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundFacingFinalizeProcessor> FacingFinalizeProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundPostFinalizeMetricsProcessor> PostFinalizeMetricsProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundAuthorityCommitProcessor> AuthorityCommitProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundClientPredictionCommitProcessor> ClientPredictionCommitProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundCheckpointPublisherProcessor> CheckpointPublisherProcessor;
  int32 ConsecutiveCatchupCpuBudgetHitFrames = 0;
};
