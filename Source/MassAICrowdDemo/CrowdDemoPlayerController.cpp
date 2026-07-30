#include "CrowdDemoPlayerController.h"

#include "Arena/CrowdDemoArenaBuilder.h"
#include "CrowdDemoReplicator.h"
#include "CrowdDemoRoundSimCoordinator.h"
#include "EngineUtils.h"
#include "Mass/CrowdDemoMassReplication.h"

namespace
{
  bool RequiresCrowdDemoClientReady()
  {
    return FParse::Param(FCommandLine::Get(), TEXT("CrowdDemoRequireClientReady"));
  }

  int32 ResolveExpectedAgentCount()
  {
    int32 Count = 500;
    FParse::Value(FCommandLine::Get(), TEXT("CrowdDemoEntityCount="), Count);
    return FMath::Clamp(Count, 1, 10000);
  }
}

ACrowdDemoPlayerController::ACrowdDemoPlayerController()
{
  PrimaryActorTick.bCanEverTick = true;
}

void ACrowdDemoPlayerController::Tick(const float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);
  if (!IsLocalController() || HasAuthority() || bReadyReported || !RequiresCrowdDemoClientReady())
  {
    return;
  }

  UWorld* World = GetWorld();
  int32 AgentCount = 0;
  int32 VisibleInstances = 0;
  bool bHasReplicatedVisualOwner = false;
  bool bHasArena = false;
  bool bHasCoordinator = false;
  if (World)
  {
    for (TActorIterator<ACrowdDemoReplicator> It(World); It; ++It)
    {
      if (!It->IsLocalVisualHostOnly())
      {
        VisibleInstances = It->GetCrowdVisualInstanceCount();
        bHasReplicatedVisualOwner = true;
        break;
      }
    }
    for (TActorIterator<ACrowdDemoMassClientBubbleInfo> It(World); It; ++It)
    {
      AgentCount = FMath::Max(AgentCount, It->GetCrowdDemoSerializer().GetAgentCount());
    }
    bHasArena = TActorIterator<ACrowdDemoArenaBuilder>(World).operator bool();
    bHasCoordinator = TActorIterator<ACrowdDemoRoundSimCoordinator>(World).operator bool();
  }

  const int32 ExpectedCount = ResolveExpectedAgentCount();
  const bool bReady = AgentCount == ExpectedCount
    && VisibleInstances == ExpectedCount
    && bHasReplicatedVisualOwner
    && bHasArena
    && bHasCoordinator;
  ReadyStableSeconds = bReady ? ReadyStableSeconds + DeltaSeconds : 0.0f;
  if (ReadyStableSeconds >= 0.5f)
  {
    bReadyReported = true;
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoValidationReady role=client agents=%d visible_instances=%d stable_seconds=%.3f source=PlayerController"),
      AgentCount,
      VisibleInstances,
      ReadyStableSeconds);
    ServerReportCrowdDemoReady(AgentCount, VisibleInstances);
  }
}

void ACrowdDemoPlayerController::ServerReportCrowdDemoReady_Implementation(
  const int32 AgentCount,
  const int32 VisibleInstances)
{
  if (UWorld* World = GetWorld())
  {
    for (TActorIterator<ACrowdDemoRoundSimCoordinator> It(World); It; ++It)
    {
      It->NotifyValidationClientReady(AgentCount, VisibleInstances);
      break;
    }
  }
}
