#include "MassCrowdRuntimeSubsystem.h"

#include "Engine/World.h"
#include "MassCrowdNavSurfaceGraphExtractor.h"

FCrowdRecastNavDataProvider::FCrowdRecastNavDataProvider(UWorld& InWorld)
  : World(&InWorld)
{
}

void FCrowdRecastNavDataProvider::InvalidateTopology()
{
  ++TopologyRevision;
  if (TopologyRevision == 0) TopologyRevision = 1;
}

bool FCrowdRecastNavDataProvider::BuildGraph(
  const FCrowdNavSurfaceGraphBuildConfig& Config,
  FCrowdNavSurfaceGraph& OutGraph,
  FString& OutFailureReason)
{
  OutGraph.Reset();
  OutFailureReason.Reset();
  UWorld* ResolvedWorld = World.Get();
  if (!ResolvedWorld)
  {
    OutFailureReason = TEXT("MissingWorld");
    return false;
  }
  TArray<FCrowdNavSurfacePolygonInput> Polygons;
  FCrowdNavSurfaceExtractionDiagnostics Diagnostics;
  if (!FCrowdNavSurfaceGraphExtractor::ExtractStaticRecast(
      *ResolvedWorld, MaxPolygons, Polygons, Diagnostics))
  {
    OutFailureReason = Diagnostics.FailureReason;
    return false;
  }
  if (!FCrowdNavSurfaceGraphKernel::Build(Polygons, Config, OutGraph))
  {
    OutFailureReason = TEXT("GraphBuildRejected");
    return false;
  }
  return true;
}

void UMassCrowdRuntimeSubsystem::Initialize(
  FSubsystemCollectionBase& Collection)
{
  Super::Initialize(Collection);
  NavDataProvider = MakeUnique<FCrowdRecastNavDataProvider>(*GetWorld());
  NavGraphResource = {};
  FlowCache.Reset();
  verify(BehaviorSourceRuntime.InitializeFromRegisteredProviders());
  AsyncSimulationRuntime =
    MakeUnique<FCrowdAsyncSimulationRuntime>();
  WorkerShadowSync =
    MakeUnique<FCrowdWorkerBoundaryShadowSync>();
  WorkerResultApplyProxy =
    MakeUnique<FCrowdWorkerResultApplyProxy>();
  WorkerMovementAuthority =
    MakeUnique<FCrowdWorkerMovementAuthority>();
}

void UMassCrowdRuntimeSubsystem::Deinitialize()
{
  if (WorkerShadowSync)
  {
    WorkerShadowSync->ResetQuiescent();
    WorkerShadowSync.Reset();
  }
  WorkerMovementAuthority.Reset();
  WorkerResultApplyProxy.Reset();
  if (AsyncSimulationRuntime)
  {
    AsyncSimulationRuntime->StopAndDrain(5.0);
    AsyncSimulationRuntime.Reset();
  }
  FlowCache.Reset();
  BehaviorSourceRuntime.Reset();
  NavGraphResource = {};
  NavDataProvider.Reset();
  Super::Deinitialize();
}

void UMassCrowdRuntimeSubsystem::SetNavDataProvider(
  TUniquePtr<ICrowdNavDataProvider>&& Provider)
{
  check(IsInGameThread());
  FlowCache.Reset();
  NavGraphResource = {};
  NavDataProvider = MoveTemp(Provider);
}

void UMassCrowdRuntimeSubsystem::InvalidateTopology()
{
  check(IsInGameThread());
  if (NavDataProvider) NavDataProvider->InvalidateTopology();
}

void UMassCrowdRuntimeSubsystem::SetGraphBuildConfig(
  const FCrowdNavSurfaceGraphBuildConfig& Config)
{
  check(IsInGameThread());
  GraphBuildConfig = Config;
  FlowCache.Reset();
  NavGraphResource = {};
}

bool UMassCrowdRuntimeSubsystem::BuildOrRefreshNavGraph(const bool bForce)
{
  check(IsInGameThread());
  if (!NavDataProvider)
  {
    NavGraphResource = {};
    NavGraphResource.State = ECrowdNavGraphResourceState::Failed;
    NavGraphResource.FailureReason = TEXT("MissingProvider");
    return false;
  }
  const uint32 Revision = NavDataProvider->GetTopologyRevision();
  if (!bForce && NavGraphResource.IsReady()
    && NavGraphResource.TopologyRevision == Revision)
    return true;

  FCrowdNavSurfaceGraph Built;
  FString FailureReason;
  if (!NavDataProvider->BuildGraph(
      GraphBuildConfig, Built, FailureReason))
  {
    FlowCache.Reset();
    NavGraphResource = {};
    NavGraphResource.State = ECrowdNavGraphResourceState::Failed;
    NavGraphResource.TopologyRevision = Revision;
    NavGraphResource.FailureReason = FailureReason.IsEmpty()
      ? TEXT("ProviderBuildFailed") : MoveTemp(FailureReason);
    return false;
  }

  FlowCache.Reset();
  NavGraphResource = {};
  NavGraphResource.State = ECrowdNavGraphResourceState::Ready;
  NavGraphResource.TopologyRevision = Revision;
  NavGraphResource.TopologyHash = Built.TopologyHash;
  NavGraphResource.Graph =
    MakeShared<FCrowdNavSurfaceGraph, ESPMode::ThreadSafe>(MoveTemp(Built));
  return true;
}

bool UMassCrowdRuntimeSubsystem::AcquireFlow(
  const FCrowdNavFlowKey& Key,
  FCrowdNavFlowHandle& OutHandle)
{
  if (!NavGraphResource.IsReady()
    || Key.TopologyRevision != NavGraphResource.TopologyRevision)
  {
    OutHandle = {};
    return false;
  }
  return FlowCache.Acquire(Key, *NavGraphResource.Graph, OutHandle);
}

bool UMassCrowdRuntimeSubsystem::ReleaseFlow(
  const FCrowdNavFlowHandle& Handle)
{
  return FlowCache.Release(Handle);
}

TSharedPtr<const FCrowdNavSurfaceFlow, ESPMode::ThreadSafe>
UMassCrowdRuntimeSubsystem::ResolveFlow(
  const FCrowdNavFlowHandle& Handle) const
{
  return FlowCache.Resolve(Handle);
}
