#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAsyncSimulationRuntime.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdNavRuntime.h"
#include "MassCrowdWorkerResultApply.h"
#include "MassCrowdWorkerLifecycleBehaviorDomain.h"
#include "MassCrowdWorkerMovementAuthority.h"
#include "MassCrowdWorkerShadowSync.h"
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

  FCrowdAsyncSimulationRuntime& GetAsyncSimulationRuntime()
  {
    check(AsyncSimulationRuntime);
    return *AsyncSimulationRuntime;
  }

  const FCrowdAsyncSimulationRuntime& GetAsyncSimulationRuntime() const
  {
    check(AsyncSimulationRuntime);
    return *AsyncSimulationRuntime;
  }

  FCrowdWorkerBoundaryShadowSync& GetWorkerShadowSync()
  {
    check(WorkerShadowSync);
    return *WorkerShadowSync;
  }

  const FCrowdWorkerBoundaryShadowSync& GetWorkerShadowSync() const
  {
    check(WorkerShadowSync);
    return *WorkerShadowSync;
  }

  FCrowdWorkerResultApplyProxy& GetWorkerResultApplyProxy()
  {
    check(WorkerResultApplyProxy);
    return *WorkerResultApplyProxy;
  }

  const FCrowdWorkerResultApplyProxy& GetWorkerResultApplyProxy() const
  {
    check(WorkerResultApplyProxy);
    return *WorkerResultApplyProxy;
  }

  FCrowdWorkerMovementAuthority& GetWorkerMovementAuthority()
  {
    check(WorkerMovementAuthority);
    return *WorkerMovementAuthority;
  }

  const FCrowdWorkerMovementAuthority&
    GetWorkerMovementAuthority() const
  {
    check(WorkerMovementAuthority);
    return *WorkerMovementAuthority;
  }

  FCrowdWorkerBehaviorAuthority& GetWorkerBehaviorAuthority()
  {
    check(WorkerBehaviorAuthority);
    return *WorkerBehaviorAuthority;
  }

  const FCrowdWorkerBehaviorAuthority&
    GetWorkerBehaviorAuthority() const
  {
    check(WorkerBehaviorAuthority);
    return *WorkerBehaviorAuthority;
  }

  // Adapts an upstream resource revision that may describe only part of a
  // composite payload (for example topology but not dynamic integration) to
  // a monotonic revision for the complete Worker resource payload.
  bool ResolveWorkerResourceRevision(
    uint64 ResourceId,
    uint64 UpstreamRevision,
    uint64 ContentHash,
    uint64& OutRevision);

private:
  struct FWorkerResourcePublication
  {
    uint64 Revision = 0;
    uint64 ContentHash = 0;
  };

  TUniquePtr<ICrowdNavDataProvider> NavDataProvider;
  FCrowdNavGraphResource NavGraphResource;
  FCrowdNavSurfaceGraphBuildConfig GraphBuildConfig;
  FCrowdNavFlowCache FlowCache;
  FCrowdBehaviorSourceRuntime BehaviorSourceRuntime;
  TUniquePtr<FCrowdAsyncSimulationRuntime> AsyncSimulationRuntime;
  TUniquePtr<FCrowdWorkerBoundaryShadowSync> WorkerShadowSync;
  TUniquePtr<FCrowdWorkerResultApplyProxy> WorkerResultApplyProxy;
  TUniquePtr<FCrowdWorkerMovementAuthority> WorkerMovementAuthority;
  TUniquePtr<FCrowdWorkerBehaviorAuthority> WorkerBehaviorAuthority;
  TMap<uint64, FWorkerResourcePublication>
    WorkerResourcePublications;
};
