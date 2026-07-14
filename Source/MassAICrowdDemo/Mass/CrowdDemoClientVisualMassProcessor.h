#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "CrowdDemoClientVisualMassProcessor.generated.h"

class ACrowdDemoReplicator;

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoClientVisualMassProcessor : public UMassProcessor
{
  GENERATED_BODY()

public:
  UCrowdDemoClientVisualMassProcessor();

protected:
  virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
  virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
  FMassEntityQuery EntityQuery;
  TWeakObjectPtr<ACrowdDemoReplicator> CachedVisualOwner;
  double LastVisualLogSeconds = 0.0;
  bool bRebuildInstancesNextFrame = false;
};
