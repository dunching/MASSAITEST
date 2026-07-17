#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CrowdDemoTypes.h"
#include "CrowdDemoRoundSimProcessors.generated.h"

MASSAICROWDDEMO_API bool CrowdDemoUsesSteeringFirstSf4Pipeline(ECrowdDemoScenario Scenario);
MASSAICROWDDEMO_API bool CrowdDemoUsesLegacySf4ReservationPipeline(ECrowdDemoScenario Scenario);

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
class MASSAICROWDDEMO_API UCrowdDemoRoundPositionCandidateBuildProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundPositionCandidateBuildProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundPositionAssignmentProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundPositionAssignmentProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private: FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundHoldingCandidateBuildProcessor : public UMassProcessor
{ GENERATED_BODY() public: UCrowdDemoRoundHoldingCandidateBuildProcessor(); protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
  virtual void Execute(FMassEntityManager&, FMassExecutionContext&) override;
private: FMassEntityQuery EntityQuery; };

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundHoldingCompatibilityBuildProcessor : public UMassProcessor
{ GENERATED_BODY() public: UCrowdDemoRoundHoldingCompatibilityBuildProcessor(); protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
  virtual void Execute(FMassEntityManager&, FMassExecutionContext&) override;
private: FMassEntityQuery EntityQuery; };

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundHoldingAssignmentProcessor : public UMassProcessor
{ GENERATED_BODY() public: UCrowdDemoRoundHoldingAssignmentProcessor(); protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
  virtual void Execute(FMassEntityManager&, FMassExecutionContext&) override;
private: FMassEntityQuery EntityQuery; };

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundCommitRequestBuildProcessor : public UMassProcessor
{ GENERATED_BODY() public: UCrowdDemoRoundCommitRequestBuildProcessor(); protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
  virtual void Execute(FMassEntityManager&, FMassExecutionContext&) override;
private: FMassEntityQuery EntityQuery; };

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundCommitGateScheduleProcessor : public UMassProcessor
{ GENERATED_BODY() public: UCrowdDemoRoundCommitGateScheduleProcessor(); protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
  virtual void Execute(FMassEntityManager&, FMassExecutionContext&) override;
private: FMassEntityQuery EntityQuery; };

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundSteeringStateBoundaryApplyProcessor : public UMassProcessor
{ GENERATED_BODY() public: UCrowdDemoRoundSteeringStateBoundaryApplyProcessor(); protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
  virtual void Execute(FMassEntityManager&, FMassExecutionContext&) override;
private: FMassEntityQuery EntityQuery; };

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundSteeringFirstPositionGuidanceProcessor : public UMassProcessor
{ GENERATED_BODY() public: UCrowdDemoRoundSteeringFirstPositionGuidanceProcessor(); protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
  virtual void Execute(FMassEntityManager&, FMassExecutionContext&) override;
private: FMassEntityQuery EntityQuery; };

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundFrontAdmissionProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundFrontAdmissionProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private: FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundPositionApproachRouteProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundPositionApproachRouteProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private: FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundFrontPhaseReservationProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundFrontPhaseReservationProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundFrontPhaseReservationApplyProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundFrontPhaseReservationApplyProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private: FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundPursuitPositionGuidanceProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundPursuitPositionGuidanceProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private: FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundMovementIntentComposeProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundMovementIntentComposeProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private: FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundElasticCrowdShadowProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundElasticCrowdShadowProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private: FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundCrowdTrafficFieldBuildProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundCrowdTrafficFieldBuildProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundPortalScheduleProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundPortalScheduleProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundPassingBandGuidanceProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundPassingBandGuidanceProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundDeterministicOrcaProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundDeterministicOrcaProcessor();
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
class MASSAICROWDDEMO_API UCrowdDemoRoundHardSeparationPbdProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundHardSeparationPbdProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundObstacleReprojectProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundObstacleReprojectProcessor();
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
class MASSAICROWDDEMO_API UCrowdDemoRoundPositionIngressDiagnosticProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundPositionIngressDiagnosticProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundSteeringFirstDiagnosticProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundSteeringFirstDiagnosticProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
  virtual void Execute(FMassEntityManager&, FMassExecutionContext&) override;
private: FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundOverlapSampleProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundOverlapSampleProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundResidualPositioningDiagnosticProcessor : public UMassProcessor
{
  GENERATED_BODY()
public: UCrowdDemoRoundResidualPositioningDiagnosticProcessor();
protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
  virtual void Execute(FMassEntityManager&, FMassExecutionContext&) override;
private:
  FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundSeparationProcessor : public UMassProcessor
{
  GENERATED_BODY()
public:
  UCrowdDemoRoundSeparationProcessor();
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
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundPositionCandidateBuildProcessor> PositionCandidateBuildProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundPositionAssignmentProcessor> PositionAssignmentProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundHoldingCandidateBuildProcessor> HoldingCandidateBuildProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundHoldingCompatibilityBuildProcessor> HoldingCompatibilityBuildProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundHoldingAssignmentProcessor> HoldingAssignmentProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundCommitRequestBuildProcessor> CommitRequestBuildProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundCommitGateScheduleProcessor> CommitGateScheduleProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundSteeringStateBoundaryApplyProcessor> SteeringStateBoundaryApplyProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundSteeringFirstPositionGuidanceProcessor> SteeringFirstPositionGuidanceProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundFrontAdmissionProcessor> FrontAdmissionProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundPositionApproachRouteProcessor> PositionApproachRouteProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundFrontPhaseReservationProcessor> FrontPhaseReservationProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundFrontPhaseReservationApplyProcessor> FrontPhaseReservationApplyProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundFlowPreferredVelocityProcessor> FlowPreferredVelocityProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundCrowdTrafficFieldBuildProcessor> CrowdTrafficFieldBuildProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundPortalScheduleProcessor> PortalScheduleProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundPassingBandGuidanceProcessor> PassingBandGuidanceProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundPursuitPositionGuidanceProcessor> PursuitPositionGuidanceProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundMovementIntentComposeProcessor> MovementIntentComposeProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundElasticCrowdShadowProcessor> ElasticCrowdShadowProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundDeterministicOrcaProcessor> DeterministicOrcaProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundLocalPredictiveInteractionProcessor> LocalPredictiveInteractionProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundMovementPredictProcessor> MovementPredictProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundParticleConstraintProcessor> ParticleConstraintProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundObstacleConstraintProcessor> ObstacleConstraintProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundHardSeparationPbdProcessor> HardSeparationPbdProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundObstacleReprojectProcessor> ObstacleReprojectProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundMovementFinalizeProcessor> MovementFinalizeProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundPositionIngressDiagnosticProcessor> PositionIngressDiagnosticProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundSteeringFirstDiagnosticProcessor> SteeringFirstDiagnosticProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundResidualPositioningDiagnosticProcessor> ResidualPositioningDiagnosticProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundOverlapSampleProcessor> OverlapSampleProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundSeparationProcessor> SeparationProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundAuthorityCommitProcessor> AuthorityCommitProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundClientPredictionCommitProcessor> ClientPredictionCommitProcessor;
  UPROPERTY(Transient) TObjectPtr<UCrowdDemoRoundCheckpointPublisherProcessor> CheckpointPublisherProcessor;
};
