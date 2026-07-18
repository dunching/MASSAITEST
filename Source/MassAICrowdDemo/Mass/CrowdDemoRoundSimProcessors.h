#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CrowdDemoTypes.h"
#include "CrowdDemoRoundSimProcessors.generated.h"


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
class MASSAICROWDDEMO_API UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
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
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private: FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundTargetSlotLayoutPrepareProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundTargetSlotLayoutPrepareProcessor();
  virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override { return false; }
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundTargetApproachScheduleProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundTargetApproachScheduleProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundTargetApproachCommitProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundTargetApproachCommitProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundTargetApproachGuidanceProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundTargetApproachGuidanceProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};





















UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundHitResponseBoundaryApplyProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundHitResponseBoundaryApplyProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundRangedCombatProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundRangedCombatProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundReactiveMotionIntentComposeProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundReactiveMotionIntentComposeProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundVisualStateResolveProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundVisualStateResolveProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundLocalPredictiveInteractionProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundLocalPredictiveInteractionProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundMovementPredictProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundMovementPredictProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundParticleConstraintProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundParticleConstraintProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundObstacleConstraintProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundObstacleConstraintProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundFacingResolveProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundFacingResolveProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};



UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundMovementFinalizeProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundMovementFinalizeProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};






UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundAuthorityCommitProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundAuthorityCommitProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundClientPredictionCommitProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundClientPredictionCommitProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundCheckpointPublisherProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundCheckpointPublisherProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
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
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundRangedCombatProcessor> RangedCombatProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundHitResponseBoundaryApplyProcessor> HitResponseBoundaryApplyProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundReactiveMotionIntentComposeProcessor> ReactiveMotionIntentComposeProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundVisualStateResolveProcessor> VisualStateResolveProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundPlanApplyProcessor> PlanApplyProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundOpenSpawnRelaxationPhasePrepareProcessor> OpenSpawnRelaxationPhasePrepareProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundSharedFlowFieldBuildProcessor> SharedFlowFieldBuildProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetFactApplyProcessor> TargetFactApplyProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetPolarTopologyBuildProcessor> TargetPolarTopologyBuildProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetRegionPopulationBuildProcessor> TargetRegionPopulationBuildProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetRegionTransportSolveProcessor> TargetRegionTransportSolveProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetRegionGuidanceProcessor> TargetRegionGuidanceProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetSlotLayoutPrepareProcessor> TargetSlotLayoutPrepareProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetApproachScheduleProcessor> TargetApproachScheduleProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetApproachCommitProcessor> TargetApproachCommitProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundTargetApproachGuidanceProcessor> TargetApproachGuidanceProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundFlowPreferredVelocityProcessor> FlowPreferredVelocityProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundLocalPredictiveInteractionProcessor> LocalPredictiveInteractionProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundMovementPredictProcessor> MovementPredictProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundParticleConstraintProcessor> ParticleConstraintProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundObstacleConstraintProcessor> ObstacleConstraintProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundFacingResolveProcessor> FacingResolveProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundMovementFinalizeProcessor> MovementFinalizeProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundAuthorityCommitProcessor> AuthorityCommitProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundClientPredictionCommitProcessor> ClientPredictionCommitProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundCheckpointPublisherProcessor> CheckpointPublisherProcessor;
  int32 ConsecutiveCatchupCpuBudgetHitFrames = 0;
};
