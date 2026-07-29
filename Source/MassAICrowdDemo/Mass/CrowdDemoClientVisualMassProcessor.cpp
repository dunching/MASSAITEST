#include "Mass/CrowdDemoClientVisualMassProcessor.h"

#include "CrowdDemoReplicator.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoPresentationAdapter.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Mass/CrowdDemoVatPlaybackKernel.h"
#include "MassCrowdPresentationSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"
#include "MassExecutionContext.h"

namespace
{
  constexpr float RoundSimOffsetDecayRate = 7.5f;
  constexpr float RoundSimYawOffsetDecayDegreesPerSecond = 540.0f;
  constexpr float RoundSimVisualCatchupSpeedScale = 1.5f;
  constexpr float RoundSimMaxInterpolationDurationSeconds = 0.5f;
  constexpr float TargetMarkerRedrawSeconds = 0.20f;
  constexpr float TargetMarkerLifetimeSeconds = 0.24f;

  void DrawCircleXY(
    UWorld& World,
    const FVector& Center,
    const float RadiusCm,
    const FColor& Color,
    const int32 Segments = 64)
  {
    if (RadiusCm <= 0.0f || Segments < 3)
    {
      return;
    }
    FVector Previous = Center + FVector(RadiusCm, 0.0f, 0.0f);
    for (int32 Segment = 1; Segment <= Segments; ++Segment)
    {
      const float Angle = 2.0f * PI * static_cast<float>(Segment)
        / static_cast<float>(Segments);
      const FVector Current = Center + FVector(
        FMath::Cos(Angle) * RadiusCm,
        FMath::Sin(Angle) * RadiusCm,
        0.0f);
      DrawDebugLine(&World, Previous, Current, Color, false,
        TargetMarkerLifetimeSeconds, 0, 2.0f);
      Previous = Current;
    }
  }

  void DrawTargetAcceptanceMarkers(
    UWorld& World,
    const UCrowdDemoRoundSimPipelineSubsystem& Pipeline)
  {
    if (!Pipeline.IsActive())
    {
      return;
    }
    const FCrowdDemoRoundRules& Rules = Pipeline.GetRules();
    const FCrowdDemoTargetFact& Target = Pipeline.GetTargetFact();
    if (Target.TargetId == INDEX_NONE)
    {
      return;
    }
    const FVector TargetCenter(Target.Location.X, Target.Location.Y, 65.0f);

    if (Rules.TargetRegionTransportSettings.bEnabled == 0)
    {
      return;
    }
    const int32 RegionCount = FMath::Max(
      1, Rules.TargetRegionTransportSettings.DemandRegionCount);
    const auto DrawBand = [&](const float MinimumDistance,
      const float MaximumDistance, const FColor& Color,
      const TConstArrayView<FCrowdDemoTargetPolarCell> Cells)
    {
      DrawCircleXY(World, TargetCenter, MinimumDistance, Color);
      DrawCircleXY(World, TargetCenter, MaximumDistance, Color, 96);
      for (int32 Region = 0; Region < RegionCount; ++Region)
      {
        const float Angle = 2.0f * PI * static_cast<float>(Region)
          / static_cast<float>(RegionCount);
        const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
        DrawDebugLine(&World,
          TargetCenter + Direction * MinimumDistance,
          TargetCenter + Direction * MaximumDistance,
          Color, false, TargetMarkerLifetimeSeconds, 0, 1.0f);
      }
      for (const FCrowdDemoTargetPolarCell& Cell : Cells)
      {
        if (!Cell.bFeasible || !Cell.bTerminal) continue;
        DrawDebugPoint(&World,
          FVector(Cell.WorldAnchorCm.X, Cell.WorldAnchorCm.Y, 68.0f),
          7.0f, Color, false, TargetMarkerLifetimeSeconds, 0);
      }
    };

    const TArray<FCrowdDemoTargetRegionCapabilityCohortRuntime>& Cohorts =
      Pipeline.GetCapabilityCohorts();
    if (Rules.bEnableHeterogeneousProfiles != 0 && Cohorts.Num() > 0)
    {
      static const FColor Colors[] = {
        FColor::Cyan, FColor::Green, FColor::Yellow, FColor::Magenta,
        FColor(255, 128, 32), FColor(64, 160, 255), FColor(180, 100, 255)};
      for (int32 Index = 0; Index < Cohorts.Num(); ++Index)
      {
        const FCrowdDemoTargetRegionCapabilityCohortRuntime& Cohort = Cohorts[Index];
        DrawBand(Cohort.Cohort.Profile.NormalizedMinimumCenterDistanceCm,
          Cohort.Cohort.Profile.NormalizedMaximumCenterDistanceCm,
          Colors[Index % UE_ARRAY_COUNT(Colors)], Cohort.Topology.Cells);
      }
      return;
    }
    DrawBand(
      Rules.TargetDistanceBandSettings.TargetPhysicalRadiusCm
        + Rules.TargetDistanceBandSettings.TargetHardSafetyGapCm,
      Rules.TargetDistanceBandSettings.DefaultMaximumCombatCenterDistanceCm,
      FColor::Cyan, Pipeline.GetPreparedTargetRegionTopology().Cells);
  }

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
  EntityQuery.AddRequirement<FCrowdDemoMassIdentityFragment>(EMassFragmentAccess::ReadOnly);
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
  // Coordinator-owned scenarios have their own presentation transaction and
  // profile lifetime. The Round client processor must not compete for the
  // shared profile while one of those scenarios is active.
  if (FParse::Param(FCommandLine::Get(), TEXT("CrowdDemoMixedSandbox"))
    || FParse::Param(
      FCommandLine::Get(), TEXT("CrowdDemoMixedCombatIntegration"))
    || FParse::Param(FCommandLine::Get(), TEXT("CrowdDemoContinuousLifecycle"))
    || FParse::Param(FCommandLine::Get(), TEXT("CrowdDemoFriendlyLogisticsSmall")))
  {
    return;
  }
  const double VisualProcessorStartSeconds = FPlatformTime::Seconds();
  UCrowdDemoRoundSimPipelineSubsystem* Pipeline =
    World->GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>();
  if (!Pipeline)
  {
    return;
  }

  ACrowdDemoReplicator* PreviousOwner = CachedVisualOwner.Get();
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
  UMassCrowdPresentationSubsystem* Presentation =
    World->GetSubsystem<UMassCrowdPresentationSubsystem>();
  if (!Presentation)
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
  if (bRebuildInstances && bPresentationProfileRegistered)
  {
    if (!Presentation->ResetProfile(1)
      || !Presentation->UnregisterProfile(1))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoClientVisual role=client stage=presentation_owner_release"));
      return;
    }
    bPresentationProfileRegistered = false;
    PresentedEntities.Reset();
  }
  if (bOwnerChanged && PreviousOwner && PreviousOwner != Replicator
    && PreviousOwner->IsLocalVisualHostOnly())
  {
    PreviousOwner->Destroy();
  }
  if (!bPresentationProfileRegistered)
  {
    Replicator->ClearCrowdVisualInstances();
    Replicator->RecordVisualInstanceRebuild();
    TSharedRef<FCrowdDemoIsmPresentationSink> Sink =
      MakeShared<FCrowdDemoIsmPresentationSink>(
        *Instances, *HitFlashInstances);
    if (!Presentation->RegisterProfile(1, Sink))
    {
      const bool bRecoveredStaleOwner =
        Presentation->ResetProfile(1)
        && Presentation->UnregisterProfile(1)
        && Presentation->RegisterProfile(
          1,
          MakeShared<FCrowdDemoIsmPresentationSink>(
            *Instances, *HitFlashInstances));
      if (!bRecoveredStaleOwner)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoClientVisual role=client stage=presentation_profile"));
        return;
      }
    }
    bPresentationProfileRegistered = true;
    bRebuildInstancesNextFrame = false;
  }
  Replicator->ResetClientMassEntityStates();

  const float DeltaSeconds = FMath::Max(Context.GetDeltaTimeSeconds(), 0.0f);
  const AGameStateBase* GameState = World->GetGameState();
  const float ClientServerSeconds = GameState
    ? GameState->GetServerWorldTimeSeconds()
    : World->GetTimeSeconds();
  const int32 AppliedCorrectionRevision = Pipeline->GetLastAppliedCorrectionRevision();
  ++PresentationSequence;
  int32 SubmittedCount = 0;
  int32 VatPlaybackCount = 0;
  int32 ActiveHitFlashCount = 0;
  int32 VisualStateCounts[FCrowdDemoVatPlaybackKernel::ClipCount] = {};
  TMap<int32, bool> OpenSpawnParticleActiveByAgentId;
  if (Pipeline->IsOpenSpawnRelaxation())
  {
    for (const FCrowdDemoOpenSpawnRelaxationAgentState& Agent
      : Pipeline->GetOpenSpawnRelaxationRuntime().Agents)
      OpenSpawnParticleActiveByAgentId.Add(Agent.AgentId, Agent.bParticleActive);
  }

  EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
  {
    const TConstArrayView<FCrowdDemoClientAuthorityFragment> Authorities =
      ChunkContext.GetFragmentView<FCrowdDemoClientAuthorityFragment>();
    const TConstArrayView<FCrowdDemoMassIdentityFragment> Identities =
      ChunkContext.GetFragmentView<FCrowdDemoMassIdentityFragment>();
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
      const bool bWasDisplayInitialized = Offset.bDisplayInitialized;
      const FVector PreviousSimLocation = Offset.LastSubmittedSimLocation;
      const FVector PreviousDisplayLocation = Offset.LastSubmittedDisplayLocation;
      const float PreviousSimServerTime = Offset.LastSubmittedSimServerTimeSeconds;
      const float PreviousSubmitWorldTime = Offset.LastSubmittedWorldSeconds;
      const bool bCorrectionBoundary = bWasDisplayInitialized
        && AppliedCorrectionRevision > Offset.LastRoundSimCorrectionRevision;
      const bool bPlanChanged = bWasDisplayInitialized
        && SimState.PlanRevision != Offset.LastSubmittedPlanRevision;
      const bool bOpenSpawnParticleActive =
        OpenSpawnParticleActiveByAgentId.FindRef(Identities[It].Id);
      const bool bTestBoundaryReset = bWasDisplayInitialized
        && Pipeline->IsOpenSpawnRelaxation()
        && bOpenSpawnParticleActive
          != Offset.bLastSubmittedOpenSpawnParticleActive;
      if (!Offset.bDisplayInitialized)
      {
        Offset.DisplayLocation = SimState.Location;
        Offset.DisplayYawDegrees = SimState.YawDegrees;
        Offset.InterpolationFromLocation = SimState.Location;
        Offset.InterpolationToLocation = SimState.Location;
        Offset.InterpolationFromYawDegrees = SimState.YawDegrees;
        Offset.InterpolationToYawDegrees = SimState.YawDegrees;
        Offset.InterpolationStartWorldSeconds = World->GetTimeSeconds();
        Offset.InterpolationDurationSeconds = Pipeline->GetCurrentFixedStepSeconds();
        Offset.RoundSimDisplayOffset = FVector::ZeroVector;
        Offset.RoundSimYawOffsetDegrees = 0.0f;
        Offset.LastRoundSimCorrectionRevision = AppliedCorrectionRevision;
        Offset.bDisplayInitialized = true;
      }
      else
      {
        const float WorldSeconds = World->GetTimeSeconds();
        const auto EvaluateInterpolationAlpha = [&]()
        {
          return Offset.InterpolationDurationSeconds > KINDA_SMALL_NUMBER
            ? FMath::Clamp(
                (WorldSeconds - Offset.InterpolationStartWorldSeconds)
                  / Offset.InterpolationDurationSeconds,
                0.0f, 1.0f)
            : 1.0f;
        };
        float InterpolationAlpha = EvaluateInterpolationAlpha();
        FVector InterpolatedLocation = FMath::Lerp(
          Offset.InterpolationFromLocation, Offset.InterpolationToLocation,
          InterpolationAlpha);
        float InterpolatedYaw = Offset.InterpolationFromYawDegrees
          + FMath::FindDeltaAngleDegrees(
              Offset.InterpolationFromYawDegrees, Offset.InterpolationToYawDegrees)
            * InterpolationAlpha;
        const float SimDeltaSeconds = FMath::Max(
          0.0f, SimState.SimulatedServerTimeSeconds - PreviousSimServerTime);
        const bool bNewSimSnapshot = SimDeltaSeconds > KINDA_SMALL_NUMBER || bPlanChanged;
        if (bNewSimSnapshot)
        {
          if (bPlanChanged || bTestBoundaryReset)
          {
            InterpolatedLocation = SimState.Location;
            InterpolatedYaw = SimState.YawDegrees;
          }
          Offset.InterpolationFromLocation = InterpolatedLocation;
          Offset.InterpolationToLocation = SimState.Location;
          Offset.InterpolationFromYawDegrees = InterpolatedYaw;
          Offset.InterpolationToYawDegrees = SimState.YawDegrees;
          Offset.InterpolationStartWorldSeconds = WorldSeconds;
          const float InterpolationDistanceCm = FVector::Dist2D(
            Offset.InterpolationFromLocation, Offset.InterpolationToLocation);
          const float VisualCatchupSpeedCmps = FMath::Max(
            Pipeline->GetRules().MaxSpeedCmPerSecond
              * RoundSimVisualCatchupSpeedScale,
            SimState.Velocity.Size2D());
          const float SpatialDurationSeconds = InterpolationDistanceCm
            / FMath::Max(VisualCatchupSpeedCmps, 1.0f);
          Offset.InterpolationDurationSeconds = FMath::Clamp(
            FMath::Max(SimDeltaSeconds, SpatialDurationSeconds),
            Pipeline->GetCurrentFixedStepSeconds(),
            RoundSimMaxInterpolationDurationSeconds);
          InterpolationAlpha = 0.0f;
        }
        const FVector BaseLocation = FMath::Lerp(
          Offset.InterpolationFromLocation, Offset.InterpolationToLocation,
          InterpolationAlpha);
        const float BaseYaw = Offset.InterpolationFromYawDegrees
          + FMath::FindDeltaAngleDegrees(
              Offset.InterpolationFromYawDegrees, Offset.InterpolationToYawDegrees)
            * InterpolationAlpha;
        if (bCorrectionBoundary)
        {
          Offset.RoundSimDisplayOffset = Offset.DisplayLocation - BaseLocation;
          Offset.RoundSimYawOffsetDegrees = FMath::FindDeltaAngleDegrees(
            BaseYaw,
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
        Offset.DisplayLocation = BaseLocation + Offset.RoundSimDisplayOffset;
        Offset.DisplayYawDegrees = BaseYaw + Offset.RoundSimYawOffsetDegrees;
      }

      if (bWasDisplayInitialized)
      {
        const float SimDeltaSeconds = FMath::Max(
          0.0f, SimState.SimulatedServerTimeSeconds - PreviousSimServerTime);
        const int32 CollapsedSimSteps = FMath::Max(0, FMath::RoundToInt(
          SimDeltaSeconds / FMath::Max(Pipeline->GetCurrentFixedStepSeconds(), KINDA_SMALL_NUMBER)));
        const float SimDeltaCm = FVector::Dist2D(SimState.Location, PreviousSimLocation);
        const float DisplayDeltaCm = FVector::Dist2D(Offset.DisplayLocation, PreviousDisplayLocation);
        const float SubmitIntervalMs = FMath::Max(
          0.0f, World->GetTimeSeconds() - PreviousSubmitWorldTime) * 1000.0f;
        const float ExpectedDisplayDeltaCm = FMath::Max(
          50.0f,
          SimState.Velocity.Size2D()
            * FMath::Max(SimDeltaSeconds, Pipeline->GetCurrentFixedStepSeconds()) * 1.5f
            + 5.0f);
        Replicator->RecordRoundSimVisualContinuity(
          Authority.VisualId,
          SubmitIntervalMs,
          SimDeltaCm,
          DisplayDeltaCm,
          ExpectedDisplayDeltaCm,
          CollapsedSimSteps,
          bCorrectionBoundary,
          bPlanChanged,
          bTestBoundaryReset,
          DisplayDeltaCm > ExpectedDisplayDeltaCm,
          Offset.LastSubmittedPlanRevision,
          SimState.PlanRevision,
          PreviousSimServerTime,
          SimState.SimulatedServerTimeSeconds,
          PreviousDisplayLocation,
          Offset.DisplayLocation);
      }
      Offset.LastSubmittedSimLocation = SimState.Location;
      Offset.LastSubmittedDisplayLocation = Offset.DisplayLocation;
      Offset.LastSubmittedSimServerTimeSeconds = SimState.SimulatedServerTimeSeconds;
      Offset.LastSubmittedWorldSeconds = World->GetTimeSeconds();
      Offset.LastSubmittedPlanRevision = SimState.PlanRevision;
      Offset.bLastSubmittedOpenSpawnParticleActive =
        bOpenSpawnParticleActive;

      const bool bSmoothingActive = Offset.RoundSimDisplayOffset.Size2D() > 0.5f
        || FMath::Abs(Offset.RoundSimYawOffsetDegrees) > 0.5f;
      Replicator->RecordRoundSimVisualSmoothing(
        Offset.RoundSimDisplayOffset.Size2D(),
        FMath::Abs(Offset.RoundSimYawOffsetDegrees),
        bSmoothingActive);

      const FTransform InstanceTransform = MakeInstanceTransform(
        Offset.DisplayLocation,
        Offset.DisplayYawDegrees);
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
        ++VatPlaybackCount;
        ActiveHitFlashCount += bHitFlashActive ? 1 : 0;
        ++VisualStateCounts[Playback.ClipIndex];
      }

      const FCrowdStableEntityRef PresentationRef{
        1,
        static_cast<uint64>(FMath::Max(0, Identities[It].Id)) + 1ull,
        static_cast<uint32>(FMath::Max(1, Identities[It].LifecycleSerial))};
      FCrowdPresentationState PresentationState;
      PresentationState.EntityRef = PresentationRef;
      PresentationState.Transform = InstanceTransform;
      PresentationState.ProfileKey = 1;
      PresentationState.VisualState =
        static_cast<uint32>(Authority.Combat.VisualState);
      PresentationState.CustomData = Playback.bValid
        ? FVector3f(
          Playback.Frame,
          Playback.PreviousFrame,
          Playback.HitFlashIntensity)
        : FVector3f::ZeroVector;
      PresentationState.Sequence = PresentationSequence;
      PresentationState.SampleServerSeconds = ClientServerSeconds;
      const bool bAlreadyPresented =
        PresentedEntities.Contains(PresentationRef);
      const ECrowdPresentationApplyResult PresentationResult =
        bAlreadyPresented
          ? Presentation->ApplyUpdate(PresentationState)
          : Presentation->ApplySpawn(PresentationState);
      if (PresentationResult
          != ECrowdPresentationApplyResult::Applied
        && PresentationResult
          != ECrowdPresentationApplyResult::Duplicate)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoClientVisual role=client stage=presentation_apply agent=%d result=%d"),
          Identities[It].Id,
          static_cast<int32>(PresentationResult));
        bRebuildInstancesNextFrame = true;
        continue;
      }
      PresentedEntities.Add(PresentationRef);

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
  const int32 ProductInstanceCount =
    Presentation->GetInstanceCount(1);
  if (ProductInstanceCount != SubmittedCount)
  {
    bRebuildInstancesNextFrame = true;
  }
  Instances->MarkRenderStateDirty();
  HitFlashInstances->MarkRenderStateDirty();
  const double NowSeconds = World->GetTimeSeconds();
  static const bool bDrawTargetAcceptanceMarkers = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoDrawTargetAcceptanceMarkers"));
  if (bDrawTargetAcceptanceMarkers
    && (LastTargetMarkerDrawSeconds < 0.0
    || NowSeconds - LastTargetMarkerDrawSeconds >= TargetMarkerRedrawSeconds)
    )
  {
    LastTargetMarkerDrawSeconds = NowSeconds;
    DrawTargetAcceptanceMarkers(*World, *Pipeline);
  }
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
      ProductInstanceCount,
      VatPlaybackCount,
      VisualStateCounts[0],
      VisualStateCounts[1],
      VisualStateCounts[2],
      VisualStateCounts[3],
      VisualStateCounts[4],
      VisualStateMask,
      ActiveHitFlashCount);
  }
  Replicator->RecordVisualProcessorPerformance(static_cast<float>(
    (FPlatformTime::Seconds() - VisualProcessorStartSeconds) * 1000.0));
}
