#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CrowdDemoTypes.h"
#include "CrowdDemoRoundSimProcessors.generated.h"

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
