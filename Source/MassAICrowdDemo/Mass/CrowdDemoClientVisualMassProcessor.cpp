#include "Mass/CrowdDemoClientVisualMassProcessor.h"

#include "CrowdDemoReplicator.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Mass/CrowdDemoVatPlaybackKernel.h"
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
    // UE imports the generated source at roughly 2.4 cm across. Scaling by
    // 34 produces an approximately 82 cm visual footprint, matching the
    // 42 cm particle radius without changing gameplay collision facts.
    const FVector Scale(34.0f);
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
  UInstancedStaticMeshComponent* HitFlashInstances = Replicator
    ? Replicator->GetCrowdHitFlashInstancesForClientVisuals()
    : nullptr;
  if (!Replicator || !Instances || !HitFlashInstances)
  {
    return;
  }
  if (Instances->NumCustomDataFloats != 3)
  {
    Instances->NumCustomDataFloats = 3;
  }
  if (HitFlashInstances->NumCustomDataFloats != 3)
  {
    HitFlashInstances->NumCustomDataFloats = 3;
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
  int32 VatPlaybackCount = 0;
  int32 ActiveHitFlashCount = 0;
  int32 VisualStateCounts[FCrowdDemoVatPlaybackKernel::ClipCount] = {};

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
      FCrowdDemoVatPlaybackInput PlaybackInput;
      PlaybackInput.VisualState = Authority.Combat.VisualState;
      PlaybackInput.ServerTimeSeconds = ClientServerSeconds;
      PlaybackInput.StateStartServerTimeSeconds = Authority.Combat.VisualStateStartServerTimeSeconds;
      PlaybackInput.PlayRate = Authority.VatPlayRateByte / 128.0f;
      PlaybackInput.PhaseSeed = Authority.Combat.VisualPhaseSeed;
      PlaybackInput.HitFlashRevision = Authority.Combat.HitFlashRevision;
      PlaybackInput.HitFlashStartServerTimeSeconds = Authority.Combat.HitFlashStartServerTimeSeconds;
      PlaybackInput.HitFlashDurationSeconds = Authority.Combat.HitFlashDurationSeconds;
      PlaybackInput.HitFlashPeakIntensity = Authority.Combat.HitFlashPeakIntensity;
      const FCrowdDemoVatPlaybackResult Playback = FCrowdDemoVatPlaybackKernel::Evaluate(PlaybackInput);
      const bool bHitFlashActive = Playback.bValid
        && Playback.HitFlashIntensity > KINDA_SMALL_NUMBER;
      if (Playback.bValid)
      {
        Instances->SetCustomDataValue(Offset.InstanceIndex, 0, Playback.Frame, false);
        Instances->SetCustomDataValue(Offset.InstanceIndex, 1, Playback.PreviousFrame, false);
        Instances->SetCustomDataValue(Offset.InstanceIndex, 2, Playback.HitFlashIntensity, false);
        ++VatPlaybackCount;
        ActiveHitFlashCount += bHitFlashActive ? 1 : 0;
        ++VisualStateCounts[Playback.ClipIndex];
      }

      FTransform HitFlashTransform = InstanceTransform;
      HitFlashTransform.SetScale3D(bHitFlashActive
        ? InstanceTransform.GetScale3D() * (1.02f + 0.03f * Playback.HitFlashIntensity)
        : FVector::ZeroVector);
      if (bRebuildInstances || Offset.InstanceIndex >= HitFlashInstances->GetInstanceCount())
      {
        const int32 HitFlashIndex = HitFlashInstances->AddInstance(HitFlashTransform, true);
        if (HitFlashIndex != Offset.InstanceIndex)
        {
          bRebuildInstancesNextFrame = true;
        }
      }
      else
      {
        HitFlashInstances->UpdateInstanceTransform(
          Offset.InstanceIndex,
          HitFlashTransform,
          true,
          false,
          true);
      }
      if (Playback.bValid && Offset.InstanceIndex < HitFlashInstances->GetInstanceCount())
      {
        HitFlashInstances->SetCustomDataValue(Offset.InstanceIndex, 0, Playback.Frame, false);
        HitFlashInstances->SetCustomDataValue(Offset.InstanceIndex, 1, Playback.PreviousFrame, false);
        HitFlashInstances->SetCustomDataValue(Offset.InstanceIndex, 2, Playback.HitFlashIntensity, false);
      }

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
  HitFlashInstances->MarkRenderStateDirty();
  const double NowSeconds = World->GetTimeSeconds();
  uint32 VisualStateMask = 0;
  for (int32 ClipIndex = 0; ClipIndex < FCrowdDemoVatPlaybackKernel::ClipCount; ++ClipIndex)
  {
    VisualStateMask |= VisualStateCounts[ClipIndex] > 0 ? (1u << ClipIndex) : 0u;
  }
  const bool bVisualStateChanged = VisualStateMask != LastVisualStateMask;
  const bool bHitFlashChanged = ActiveHitFlashCount != LastHitFlashActiveCount;
  if (NowSeconds - LastVisualLogSeconds >= 2.0 || bVisualStateChanged || bHitFlashChanged)
  {
    LastVisualLogSeconds = NowSeconds;
    LastVisualStateMask = VisualStateMask;
    LastHitFlashActiveCount = ActiveHitFlashCount;
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoVisual: submitted=%d instances=%d vat_playback=%d states=[idle:%d move:%d attack:%d hit:%d death:%d] state_mask=%u hit_flash_active=%d visual_mode=RoundSimVAT round_visual_smoothing=1 source=MassClientVisualProcessor"),
      SubmittedCount,
      Instances->GetInstanceCount(),
      VatPlaybackCount,
      VisualStateCounts[0],
      VisualStateCounts[1],
      VisualStateCounts[2],
      VisualStateCounts[3],
      VisualStateCounts[4],
      VisualStateMask,
      ActiveHitFlashCount);
  }
}
