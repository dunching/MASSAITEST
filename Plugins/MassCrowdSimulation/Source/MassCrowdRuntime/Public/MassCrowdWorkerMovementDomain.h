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
};
