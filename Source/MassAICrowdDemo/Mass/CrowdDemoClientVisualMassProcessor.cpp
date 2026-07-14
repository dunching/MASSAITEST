#include "Mass/CrowdDemoClientVisualMassProcessor.h"

#include "CrowdDemoReplicator.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "MassExecutionContext.h"

namespace
{
  constexpr float RoundSimOffsetDecayRate = 7.5f;
  constexpr float RoundSimYawOffsetDecayDegreesPerSecond = 540.0f;

  FTransform MakeInstanceTransform(
    const FVector& DisplayLocation,
    const float DisplayYawDegrees)
  {
    const FVector Scale(1.55f, 0.48f, 0.32f);
    return FTransform(FRotator(0.0f, DisplayYawDegrees, 0.0f), DisplayLocation, Scale);
  }

  ACrowdDemoReplicator* FindVisualOwner(
    UWorld& World,
    TWeakObjectPtr<ACrowdDemoReplicator>& CachedOwner,
    bool& bOutOwnerChanged)
  {
    bOutOwnerChanged = false;
    ACrowdDemoReplicator* ReplicatedOwner = nullptr;
    ACrowdDemoReplicator* LocalFallback = nullptr;
    for (TActorIterator<ACrowdDemoReplicator> It(&World); It; ++It)
    {
      ACrowdDemoReplicator* Candidate = *It;
      if (!Candidate)
      {
        continue;
      }
      if (Candidate->IsLocalVisualHostOnly())
      {
        LocalFallback = LocalFallback ? LocalFallback : Candidate;
      }
      else
      {
        ReplicatedOwner = ReplicatedOwner ? ReplicatedOwner : Candidate;
      }
    }

    ACrowdDemoReplicator* Selected = ReplicatedOwner ? ReplicatedOwner : LocalFallback;
    if (!Selected && World.GetNetMode() == NM_Client)
    {
      FActorSpawnParameters Params;
      Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
      Params.ObjectFlags |= RF_Transient;
      Selected = World.SpawnActor<ACrowdDemoReplicator>(ACrowdDemoReplicator::StaticClass(), Params);
      if (Selected)
      {
        Selected->SetLocalVisualHostOnly(true);
      }
    }

    if (ReplicatedOwner && LocalFallback && ReplicatedOwner != LocalFallback)
    {
      LocalFallback->ClearCrowdVisualInstances();
      LocalFallback->Destroy();
    }
    if (CachedOwner.Get() != Selected)
    {
      CachedOwner = Selected;
      bOutOwnerChanged = true;
      if (Selected)
      {
        UE_LOG(
          LogTemp,
          Display,
          TEXT("CrowdDemoVisualOwner: selected=%s local_host=%d source=MassClientVisualProcessor"),
          *Selected->GetName(),
          Selected->IsLocalVisualHostOnly() ? 1 : 0);
      }
    }
    return Selected;
  }
}

UCrowdDemoClientVisualMassProcessor::UCrowdDemoClientVisualMassProcessor()
  : EntityQuery(*this)
{
  ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
  bAutoRegisterWithProcessingPhases = false;
  QueryBasedPruning = EMassQueryBasedPruning::Never;
  ExecutionOrder.ExecuteAfter.Add(TEXT("MassReplicationProcessor"));
  ExecutionOrder.ExecuteAfter.Add(TEXT("CrowdDemoRoundSimFixedStepPipelineProcessor"));
  bRequiresGameThreadExecution = true;
}

void UCrowdDemoClientVisualMassProcessor::ConfigureQueries(
  const TSharedRef<FMassEntityManager>& EntityManager)
{
  EntityQuery.AddRequirement<FCrowdDemoClientAuthorityFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoRoundSimStateFragment>(EMassFragmentAccess::ReadOnly);
  EntityQuery.AddRequirement<FCrowdDemoClientVisualOffsetFragment>(EMassFragmentAccess::ReadWrite);
  EntityQuery.AddTagRequirement<FCrowdDemoMassAgentTag>(EMassFragmentPresence::All);
}

void UCrowdDemoClientVisualMassProcessor::Execute(
  FMassEntityManager& EntityManager,
  FMassExecutionContext& Context)
{
  UWorld* World = EntityManager.GetWorld();
  if (!World || World->GetNetMode() == NM_DedicatedServer)
  {
    return;
  }
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline =
    World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>();
  if (!Pipeline)
  {
    return;
  }

  bool bOwnerChanged = false;
  ACrowdDemoReplicator* Replicator = FindVisualOwner(*World, CachedVisualOwner, bOwnerChanged);
  UInstancedStaticMeshComponent* Instances = Replicator
    ? Replicator->GetCrowdInstancesForClientVisuals()
    : nullptr;
  if (!Replicator || !Instances)
  {
    return;
  }
  if (Instances->NumCustomDataFloats < 3)
  {
    Instances->NumCustomDataFloats = 3;
  }
  const bool bRebuildInstances = bRebuildInstancesNextFrame || bOwnerChanged;
  if (bRebuildInstances)
  {
    Replicator->ClearCrowdVisualInstances();
    bRebuildInstancesNextFrame = false;
  }
  Replicator->ResetClientMassEntityStates();

  const float DeltaSeconds = FMath::Max(Context.GetDeltaTimeSeconds(), 0.0f);
  const AGameStateBase* GameState = World->GetGameState();
  const float ClientServerSeconds = GameState
    ? GameState->GetServerWorldTimeSeconds()
    : World->GetTimeSeconds();
  const int32 AppliedCorrectionRevision = Pipeline->GetLastAppliedCorrectionRevision();
  int32 SubmittedCount = 0;

  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoClientAuthorityFragment> Authorities =
      ChunkContext.GetFragmentView<FCrowdDemoClientAuthorityFragment>();
    const TConstArrayView<FCrowdDemoRoundSimStateFragment> SimStates =
      ChunkContext.GetFragmentView<FCrowdDemoRoundSimStateFragment>();
    const TArrayView<FCrowdDemoClientVisualOffsetFragment> VisualOffsets =
      ChunkContext.GetMutableFragmentView<FCrowdDemoClientVisualOffsetFragment>();

    for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
    {
      const FCrowdDemoClientAuthorityFragment& Authority = Authorities[It];
      const FCrowdDemoRoundSimStateFragment& SimState = SimStates[It];
      if (!Authority.bInitialized || Authority.VisualId == INDEX_NONE || !SimState.bInitialized)
      {
        continue;
      }

      FCrowdDemoClientVisualOffsetFragment& Offset = VisualOffsets[It];
      if (!Offset.bDisplayInitialized)
      {
        Offset.DisplayLocation = SimState.Location;
        Offset.DisplayYawDegrees = SimState.YawDegrees;
        Offset.RoundSimDisplayOffset = FVector::ZeroVector;
        Offset.RoundSimYawOffsetDegrees = 0.0f;
        Offset.LastRoundSimCorrectionRevision = AppliedCorrectionRevision;
        Offset.bDisplayInitialized = true;
      }
      else
      {
        if (AppliedCorrectionRevision > Offset.LastRoundSimCorrectionRevision)
        {
          Offset.RoundSimDisplayOffset = Offset.DisplayLocation - SimState.Location;
          Offset.RoundSimYawOffsetDegrees = FMath::FindDeltaAngleDegrees(
            SimState.YawDegrees,
            Offset.DisplayYawDegrees);
          Offset.LastRoundSimCorrectionRevision = AppliedCorrectionRevision;
        }
        Offset.RoundSimDisplayOffset = FMath::VInterpTo(
          Offset.RoundSimDisplayOffset,
          FVector::ZeroVector,
          DeltaSeconds,
          RoundSimOffsetDecayRate);
        Offset.RoundSimYawOffsetDegrees = FMath::FixedTurn(
          Offset.RoundSimYawOffsetDegrees,
          0.0f,
          RoundSimYawOffsetDecayDegreesPerSecond * DeltaSeconds);
        Offset.DisplayLocation = SimState.Location + Offset.RoundSimDisplayOffset;
        Offset.DisplayYawDegrees = SimState.YawDegrees + Offset.RoundSimYawOffsetDegrees;
      }

      const bool bSmoothingActive = Offset.RoundSimDisplayOffset.Size2D() > 0.5f
        || FMath::Abs(Offset.RoundSimYawOffsetDegrees) > 0.5f;
      Replicator->RecordRoundSimVisualSmoothing(
        Offset.RoundSimDisplayOffset.Size2D(),
        FMath::Abs(Offset.RoundSimYawOffsetDegrees),
        bSmoothingActive);

      const FTransform InstanceTransform = MakeInstanceTransform(
        Offset.DisplayLocation,
        Offset.DisplayYawDegrees);
      if (bRebuildInstances || Offset.InstanceIndex == INDEX_NONE
        || Offset.InstanceIndex >= Instances->GetInstanceCount())
      {
        Offset.InstanceIndex = Instances->AddInstance(InstanceTransform, true);
      }
      else
      {
        Instances->UpdateInstanceTransform(
          Offset.InstanceIndex,
          InstanceTransform,
          true,
          false,
          true);
      }
      Instances->SetCustomDataValue(Offset.InstanceIndex, 0, Authority.VatClipIndex, false);
      Instances->SetCustomDataValue(Offset.InstanceIndex, 1, Authority.VatPhaseByte / 255.0f, false);
      Instances->SetCustomDataValue(Offset.InstanceIndex, 2, Authority.VatPlayRateByte / 128.0f, false);

      const float SampleAgeMs = Authority.ServerSampleTimeSeconds > 0.0f
        ? FMath::Max(0.0f, ClientServerSeconds - Authority.ServerSampleTimeSeconds) * 1000.0f
        : -1.0f;
      Replicator->RecordClientVisualSample(
        SampleAgeMs,
        FVector::Dist2D(Offset.DisplayLocation, SimState.Location));

      FCrowdDemoEntityState State;
      State.Id = Authority.VisualId;
      State.LifecycleSerial = Authority.LifecycleSerial;
      State.Location = FVector_NetQuantize10(SimState.Location);
      State.Velocity = FVector_NetQuantize10(SimState.Velocity);
      State.YawCentidegrees = static_cast<int16>(FMath::RoundToInt(SimState.YawDegrees * 100.0f));
      State.AnimState = Authority.AnimState;
      State.VatClipIndex = Authority.VatClipIndex;
      State.VatPhaseByte = Authority.VatPhaseByte;
      State.VatPlayRateByte = Authority.VatPlayRateByte;
      State.ServerTimeSeconds = Authority.ServerSampleTimeSeconds;
      State.LastReceiveWorldTimeSeconds = Authority.LastReceiveWorldTimeSeconds;
      Replicator->UpsertClientMassEntityState(State);
      ++SubmittedCount;
    }
  });

  if (SubmittedCount <= 0)
  {
    return;
  }
  if (Instances->GetInstanceCount() != SubmittedCount)
  {
    bRebuildInstancesNextFrame = true;
  }
  Instances->MarkRenderStateDirty();
  const double NowSeconds = World->GetTimeSeconds();
  if (NowSeconds - LastVisualLogSeconds >= 2.0)
  {
    LastVisualLogSeconds = NowSeconds;
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoVisual: submitted=%d instances=%d visual_mode=RoundSim round_visual_smoothing=1 source=MassClientVisualProcessor"),
      SubmittedCount,
      Instances->GetInstanceCount());
  }
}
