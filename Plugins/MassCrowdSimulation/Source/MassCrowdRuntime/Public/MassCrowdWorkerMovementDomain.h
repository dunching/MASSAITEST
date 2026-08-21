#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerRuntimeV2.h"

class MASSCROWDRUNTIME_API
FCrowdWorkerFlowResourceDomainExecutor final
  : public ICrowdWorkerDomainExecutor
{
public:
  virtual ECrowdWorkerDomainId GetDomainId() const override
  {
    return ECrowdWorkerDomainId::FlowResource;
  }

  virtual void GetDependencies(
    TArray<ECrowdWorkerDomainId>& OutDependencies) const override;

  virtual bool Execute(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
    FCrowdWorkerDomainOutput& OutOutput) override;
};

class MASSCROWDRUNTIME_API FCrowdWorkerMovementDomainExecutor final
  : public ICrowdWorkerDomainExecutor
{
public:
  virtual ECrowdWorkerDomainId GetDomainId() const override
  {
    return ECrowdWorkerDomainId::Movement;
  }

  virtual void GetDependencies(
    TArray<ECrowdWorkerDomainId>& OutDependencies) const override;

  virtual bool Execute(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
    FCrowdWorkerDomainOutput& OutOutput) override;
};

class MASSCROWDRUNTIME_API
FCrowdWorkerMovementPlanningDomainExecutor final
  : public ICrowdWorkerDomainExecutor
{
public:
  struct FExecutionStats
  {
    int32 FlowDecodeCount = 0;
    int32 FlowValidationCount = 0;
    int32 DistinctFlowResourceCount = 0;
  };

  explicit FCrowdWorkerMovementPlanningDomainExecutor(
    FExecutionStats* InExecutionStats = nullptr)
    : ExecutionStats(InExecutionStats)
  {
  }

  ECrowdWorkerDomainId GetDomainId() const override
  {
    return ECrowdWorkerDomainId::MovementPlanning;
  }
  void GetDependencies(
    TArray<ECrowdWorkerDomainId>& OutDependencies) const override;
  bool Execute(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
    FCrowdWorkerDomainOutput& OutOutput) override;

private:
  FExecutionStats* ExecutionStats = nullptr;
};
