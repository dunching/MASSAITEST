#include "CrowdDemoGameMode.h"

#include "Arena/CrowdDemoArenaBuilder.h"
#include "Arena/CrowdDemoTargetActor.h"
#include "CrowdDemoReplicator.h"
#include "CrowdDemoPlayerController.h"
#include "CrowdDemoRoundSimCoordinator.h"
#include "CrowdDemoScenarioConfigActor.h"
#include "CrowdDemoScenarioRegistry.h"
#include "CrowdDemoSpectatorPawn.h"
#include "EngineUtils.h"
#include "Mass/CrowdDemoMassSubsystem.h"

namespace
{
  int32 ResolveAgentCount()
  {
    int32 Count = 500;
    FParse::Value(FCommandLine::Get(), TEXT("CrowdDemoEntityCount="), Count);
    return FMath::Clamp(Count, 1, 2000);
  }

  const ACrowdDemoScenarioConfigActor* FindScenarioConfig(const UWorld& World)
  {
    for (TActorIterator<ACrowdDemoScenarioConfigActor> It(&World); It; ++It)
    {
      return *It;
    }
    return nullptr;
  }

  ECrowdDemoScenario ResolveScenario(const ACrowdDemoScenarioConfigActor* Config)
  {
    if (Config && CrowdDemoScenarioRegistry::IsValidValue(Config->ScenarioOverrideValue))
    {
      return static_cast<ECrowdDemoScenario>(Config->ScenarioOverrideValue);
    }

    FString Value;
    if (FParse::Value(FCommandLine::Get(), TEXT("CrowdDemoScenario="), Value))
    {
      ECrowdDemoScenario Parsed = ECrowdDemoScenario::SimRoundObstacle;
      if (CrowdDemoScenarioRegistry::TryParse(Value, Parsed))
      {
        return Parsed;
      }
      UE_LOG(LogTemp, Warning, TEXT("CrowdDemoScenario: unsupported=%s fallback=SimRoundObstacle"), *Value);
    }
    return ECrowdDemoScenario::SimRoundObstacle;
  }

  ACrowdDemoReplicator* FindReplicator(UWorld& World)
  {
    ACrowdDemoReplicator* Result = nullptr;
    for (TActorIterator<ACrowdDemoReplicator> It(&World); It; ++It)
    {
      ACrowdDemoReplicator* Candidate = *It;
      if (!Candidate || Candidate->IsLocalVisualHostOnly())
      {
        continue;
      }
      if (!Result)
      {
        Result = Candidate;
      }
      else
      {
        Candidate->ClearCrowdVisualInstances();
        Candidate->Destroy();
      }
    }
    return Result;
  }
}

AMassAICrowdDemoGameMode::AMassAICrowdDemoGameMode()
{
  DefaultPawnClass = ACrowdDemoSpectatorPawn::StaticClass();
  PlayerControllerClass = ACrowdDemoPlayerController::StaticClass();
}

void AMassAICrowdDemoGameMode::BeginPlay()
{
  Super::BeginPlay();
  UWorld* World = GetWorld();
  if (!World || !HasAuthority())
  {
    return;
  }

  const ACrowdDemoScenarioConfigActor* Config = FindScenarioConfig(*World);
  const ECrowdDemoScenario Scenario = ResolveScenario(Config);
  UCrowdDemoMassSubsystem* MassSubsystem = World->GetSubsystem<UCrowdDemoMassSubsystem>();
  if (!MassSubsystem)
  {
    UE_LOG(LogTemp, Warning, TEXT("CrowdDemoMass: missing UCrowdDemoMassSubsystem"));
    return;
  }

  FActorSpawnParameters SpawnParams;
  SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  SpawnParams.Name = TEXT("CrowdDemoArena");
  World->SpawnActor<ACrowdDemoArenaBuilder>(ACrowdDemoArenaBuilder::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

  SpawnParams.Name = TEXT("CrowdDemoTarget");
  ACrowdDemoTargetActor* Target = World->SpawnActor<ACrowdDemoTargetActor>(ACrowdDemoTargetActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
  MassSubsystem->SetTargetActor(Target);
  MassSubsystem->SetScenario(Scenario);

  const int32 AgentCount = Config && Config->EntityCountOverride > 0 ? Config->EntityCountOverride : ResolveAgentCount();
  const FCrowdDemoMassSpawnResult SpawnResult = MassSubsystem->SpawnAgents(AgentCount);
  UE_LOG(LogTemp, Display, TEXT("CrowdDemoMass: GameModeSpawn requested=%d agents=%d scenario=%s config_actor=%d"),
    SpawnResult.RequestedAgents,
    SpawnResult.SpawnedAgents,
    CrowdDemoScenarioRegistry::ToString(Scenario),
    Config ? 1 : 0);

  if (!FindReplicator(*World))
  {
    SpawnParams.Name = TEXT("CrowdDemoReplicator");
    World->SpawnActor<ACrowdDemoReplicator>(ACrowdDemoReplicator::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
  }

  SpawnParams.Name = TEXT("CrowdDemoRoundSimCoordinator");
  World->SpawnActor<ACrowdDemoRoundSimCoordinator>(ACrowdDemoRoundSimCoordinator::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
}
