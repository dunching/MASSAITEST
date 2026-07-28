#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdNavRuntime.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassCrowdRuntimeSubsystem.generated.h"

class UWorld;

class MASSCROWDRUNTIME_API FCrowdRecastNavDataProvider final
  : public ICrowdNavDataProvider
{
public:
  explicit FCrowdRecastNavDataProvider(UWorld& World);

  virtual uint32 GetTopologyRevision() const override
  {
    return TopologyRevision;
  }

  virtual void InvalidateTopology() override;

  virtual bool BuildGraph(
    const FCrowdNavSurfaceGraphBuildConfig& Config,
    FCrowdNavSurfaceGraph& OutGraph,
    FString& OutFailureReason) override;

  void SetMaxPolygons(int32 Value)
  {
    MaxPolygons = FMath::Max(1, Value);
  }

private:
  TWeakObjectPtr<UWorld> World;
  uint32 TopologyRevision = 1;
  int32 MaxPolygons = 65536;
};

UCLASS()
class MASSCROWDRUNTIME_API UMassCrowdRuntimeSubsystem
  : public UWorldSubsystem
{
  GENERATED_BODY()

public:
  virtual void Initialize(FSubsystemCollectionBase& Collection) override;
  virtual void Deinitialize() override;

  void SetNavDataProvider(TUniquePtr<ICrowdNavDataProvider>&& Provider);
  void InvalidateTopology();
  void SetGraphBuildConfig(
    const FCrowdNavSurfaceGraphBuildConfig& Config);
  bool BuildOrRefreshNavGraph(bool bForce = false);

  const FCrowdNavGraphResource& GetNavGraphResource() const
  {
    return NavGraphResource;
  }

  bool AcquireFlow(
    const FCrowdNavFlowKey& Key,
    FCrowdNavFlowHandle& OutHandle);
  bool ReleaseFlow(const FCrowdNavFlowHandle& Handle);

  TSharedPtr<const FCrowdNavSurfaceFlow, ESPMode::ThreadSafe> ResolveFlow(
    const FCrowdNavFlowHandle& Handle) const;

  FCrowdNavFlowCacheMetrics GetFlowCacheMetrics() const
  {
    return FlowCache.GetMetrics();
  }

  void SetFlowCacheLimits(const FCrowdNavFlowCacheLimits& Limits)
  {
    FlowCache.SetLimits(Limits);
  }

  FCrowdBehaviorSourceRuntime& GetBehaviorSourceRuntime()
  {
    return BehaviorSourceRuntime;
  }

  const FCrowdBehaviorSourceRuntime& GetBehaviorSourceRuntime() const
  {
    return BehaviorSourceRuntime;
  }

private:
  TUniquePtr<ICrowdNavDataProvider> NavDataProvider;
  FCrowdNavGraphResource NavGraphResource;
  FCrowdNavSurfaceGraphBuildConfig GraphBuildConfig;
  FCrowdNavFlowCache FlowCache;
  FCrowdBehaviorSourceRuntime BehaviorSourceRuntime;
};
