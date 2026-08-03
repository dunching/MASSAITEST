#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "MassCrowdReplicationActor.h"
#include "MassCrowdRuntimeSubsystem.h"
#include "MassCrowdWorkerMovementAuthority.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationEditorCommon.h"

namespace CrowdDemoPersistentWorkerPIETestsPrivate
{
  struct FSavedPlaySettings
  {
    EPlayNetMode NetMode = PIE_Standalone;
    bool bRunUnderOneProcess = false;
    int32 ClientCount = 1;
    bool bLaunchSeparateServer = false;
    bool bEnableGameSound = false;
    int32 AuthorityCorrectionEnabled = 1;
  };

  class FWaitForDualPieWorkerRuntime final
    : public IAutomationLatentCommand
  {
  public:
    FWaitForDualPieWorkerRuntime(
      FAutomationTestBase& InTest,
      const double InTimeoutSeconds)
      : Test(InTest)
      , DeadlineSeconds(
          FPlatformTime::Seconds() + InTimeoutSeconds)
    {
    }

    virtual bool Update() override
    {
      TArray<UWorld*> PieWorlds;
      if (GEngine)
      {
        for (const FWorldContext& Context :
          GEngine->GetWorldContexts())
        {
          if (Context.WorldType == EWorldType::PIE
            && Context.World())
            PieWorlds.Add(Context.World());
        }
      }
      if (PieWorlds.Num() >= 2)
      {
        TSet<const UMassCrowdRuntimeSubsystem*> Subsystems;
        bool bServerRuntimeReady = false;
        bool bAllRuntimesReady = true;
        for (UWorld* World : PieWorlds)
        {
          UMassCrowdRuntimeSubsystem* Subsystem =
            World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
          if (!Subsystem)
            continue;
          Subsystems.Add(Subsystem);
          const FCrowdAsyncSimulationRuntimeMetrics Metrics =
            Subsystem->GetAsyncSimulationRuntime().GetMetrics();
          bAllRuntimesReady &=
            Subsystem->GetAsyncSimulationRuntime().GetState()
              == ECrowdAsyncSimulationRuntimeState::Running
            && Metrics.MirrorEntityCount == 20
            && !Metrics.bRequiresResnapshot;
          if (World->GetNetMode() == NM_ListenServer
            || World->GetNetMode() == NM_DedicatedServer)
          {
            bServerRuntimeReady =
              Metrics.MirrorEntityCount == 20
              && Metrics.ResnapshotCount >= 1
              && !Metrics.bRequiresResnapshot
              && Subsystem->GetWorkerMovementAuthority().GetMode()
                == ECrowdWorkerMovementAuthorityMode::Production;
          }
        }
        if (Subsystems.Num() == PieWorlds.Num()
          && bServerRuntimeReady && bAllRuntimesReady)
        {
          if (!bPredictionWindowStarted)
          {
            for (UWorld* World : PieWorlds)
            {
              UMassCrowdRuntimeSubsystem* Subsystem =
                World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
              const FCrowdAsyncSimulationRuntimeMetrics Metrics =
                Subsystem->GetAsyncSimulationRuntime().GetMetrics();
              RuntimeBaselines.Add(Subsystem, {
                Metrics.Generation,
                Metrics.WorkerEpoch,
                Metrics.ResnapshotCount,
                Metrics.AuthorityCorrectionCount,
                Metrics.WorkerV2.PublishedDirtyStateCount,
                Metrics.FullMirrorSerializationCount});
              for (TActorIterator<AMassCrowdReplicationActor> It(World);
                It; ++It)
              {
                const FCrowdWorkerNetworkTrafficMetrics& Traffic =
                  It->GetWorkerTrafficMetrics();
                NetworkBaselines.Add(*It, {
                  Traffic.CorrectionBytes,
                  Traffic.CheckpointBytes,
                  Traffic.DigestMismatchCount});
              }
            }
            bPredictionWindowStarted = true;
          }
          bool bWindowComplete = true;
          for (UWorld* World : PieWorlds)
          {
            UMassCrowdRuntimeSubsystem* Subsystem =
              World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
            const FRuntimeBaseline* Baseline =
              RuntimeBaselines.Find(Subsystem);
            const FCrowdAsyncSimulationRuntimeMetrics Metrics =
              Subsystem->GetAsyncSimulationRuntime().GetMetrics();
            bWindowComplete &= Baseline
              && Metrics.WorkerEpoch >= Baseline->WorkerEpoch + 300;
          }
          if (bWindowComplete && !bCorrectionRecoveryStarted)
          {
            Test.TestTrue(
              TEXT("single process owns at least two PIE worlds"),
              PieWorlds.Num() >= 2);
            Test.TestEqual(
              TEXT("every PIE world owns a distinct runtime subsystem"),
              Subsystems.Num(), PieWorlds.Num());
            for (UWorld* World : PieWorlds)
            {
              UMassCrowdRuntimeSubsystem* Subsystem =
                World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
              const FRuntimeBaseline& Baseline =
                RuntimeBaselines.FindChecked(Subsystem);
              const FCrowdAsyncSimulationRuntimeMetrics Metrics =
                Subsystem->GetAsyncSimulationRuntime().GetMetrics();
              Test.TestEqual(TEXT("runtime generation does not restart"),
                Metrics.Generation, Baseline.Generation);
              Test.TestEqual(TEXT("runtime does not resnapshot"),
                Metrics.ResnapshotCount, Baseline.ResnapshotCount);
              Test.TestEqual(TEXT("authority correction stays disabled"),
                Metrics.AuthorityCorrectionCount,
                Baseline.AuthorityCorrectionCount);
              Test.TestTrue(TEXT("runtime publishes predicted dirty state"),
                Metrics.WorkerV2.PublishedDirtyStateCount
                  > Baseline.PublishedDirtyStateCount);
              Test.TestTrue(
                TEXT("runtime predicts at least 300 epochs without correction"),
                Metrics.MaxPredictionEpochsWithoutCorrection >= 300);
              Test.TestTrue(
                TEXT("prediction window performs at most one checkpoint mirror serialization"),
                Metrics.FullMirrorSerializationCount
                  <= Baseline.FullMirrorSerializationCount + 1);
              for (TActorIterator<AMassCrowdReplicationActor> It(World);
                It; ++It)
              {
                const FNetworkBaseline* NetworkBaseline =
                  NetworkBaselines.Find(*It);
                if (!NetworkBaseline) continue;
                const FCrowdWorkerNetworkTrafficMetrics& Traffic =
                  It->GetWorkerTrafficMetrics();
                Test.TestEqual(TEXT("correction bytes stay zero in window"),
                  Traffic.CorrectionBytes,
                  NetworkBaseline->CorrectionBytes);
                Test.TestEqual(TEXT("checkpoint bytes stay zero in window"),
                  Traffic.CheckpointBytes,
                  NetworkBaseline->CheckpointBytes);
              }
            }
            UMassCrowdRuntimeSubsystem* ClientSubsystem = nullptr;
            for (UWorld* World : PieWorlds)
            {
              if (World->GetNetMode() != NM_Client) continue;
              ClientSubsystem =
                World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
              if (ClientSubsystem) break;
            }
            FCrowdWorkerMirrorSnapshot ClientMirror;
            const bool bCanInject = ClientSubsystem
              && ClientSubsystem->GetAsyncSimulationRuntime()
                .ReadMirrorSnapshot(ClientMirror)
              && !ClientMirror.EntityRefs.IsEmpty();
            Test.TestTrue(TEXT("client runtime supplies a corruption target"),
              bCanInject);
            if (!bCanInject) return true;
            const FCrowdAsyncSimulationRuntimeMetrics ClientMetrics =
              ClientSubsystem->GetAsyncSimulationRuntime().GetMetrics();
            Test.TestTrue(TEXT("client movement corruption queues"),
              ClientSubsystem->GetAsyncSimulationRuntime()
                .QueueDiagnosticMovementCorruption(
                  ClientMetrics.Generation,
                  ClientMirror.EntityRefs[0],
                  FVector(1000.0f, 0.0f, 0.0f),
                  FVector::ZeroVector,
                  0.0f));
            CorruptedClientRuntime = ClientSubsystem;
            if (IConsoleVariable* CorrectionEnabled =
              IConsoleManager::Get().FindConsoleVariable(
                TEXT("crowd.Worker.AuthorityCorrectionEnabled")))
              CorrectionEnabled->Set(1, ECVF_SetByCode);
            bCorrectionRecoveryStarted = true;
            return false;
          }
          if (bCorrectionRecoveryStarted)
          {
            bool bRecoveryComplete = CorruptedClientRuntime != nullptr;
            bool bObservedCorrectionTraffic = false;
            bool bObservedSingleDigestMismatch = false;
            for (UWorld* World : PieWorlds)
            {
              UMassCrowdRuntimeSubsystem* Subsystem =
                World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
              const FRuntimeBaseline& Baseline =
                RuntimeBaselines.FindChecked(Subsystem);
              const FCrowdAsyncSimulationRuntimeMetrics Metrics =
                Subsystem->GetAsyncSimulationRuntime().GetMetrics();
              Test.TestEqual(TEXT("correction preserves runtime generation"),
                Metrics.Generation, Baseline.Generation);
              Test.TestEqual(TEXT("correction does not resnapshot runtime"),
                Metrics.ResnapshotCount, Baseline.ResnapshotCount);
              if (Subsystem == CorruptedClientRuntime)
              {
                if (Metrics.AuthorityCorrectionCount
                  < Baseline.AuthorityCorrectionCount + 1)
                {
                  bRecoveryComplete = false;
                }
                else
                {
                  if (MinimumPostCorrectionEpoch == 0)
                    MinimumPostCorrectionEpoch = Metrics.WorkerEpoch + 30;
                  bRecoveryComplete &= Metrics.WorkerEpoch
                    >= MinimumPostCorrectionEpoch;
                }
              }
              for (TActorIterator<AMassCrowdReplicationActor> It(World);
                It; ++It)
              {
                const FNetworkBaseline* NetworkBaseline =
                  NetworkBaselines.Find(*It);
                if (!NetworkBaseline) continue;
                const FCrowdWorkerNetworkTrafficMetrics& Traffic =
                  It->GetWorkerTrafficMetrics();
                Test.TestEqual(TEXT("correction keeps checkpoint traffic frozen"),
                  Traffic.CheckpointBytes,
                  NetworkBaseline->CheckpointBytes);
                bObservedCorrectionTraffic |= Traffic.CorrectionBytes
                  > NetworkBaseline->CorrectionBytes;
                const uint64 DigestMismatchIncrease =
                  Traffic.DigestMismatchCount
                    - NetworkBaseline->DigestMismatchCount;
                bObservedSingleDigestMismatch |= DigestMismatchIncrease == 1;
                if (DigestMismatchIncrease > 1) bRecoveryComplete = false;
              }
            }
            if (bRecoveryComplete)
            {
              const FRuntimeBaseline& ClientBaseline =
                RuntimeBaselines.FindChecked(CorruptedClientRuntime);
              const FCrowdAsyncSimulationRuntimeMetrics ClientMetrics =
                CorruptedClientRuntime->GetAsyncSimulationRuntime()
                  .GetMetrics();
              Test.TestEqual(TEXT("one sparse correction applies"),
                ClientMetrics.AuthorityCorrectionCount,
                ClientBaseline.AuthorityCorrectionCount + 1);
              Test.TestEqual(TEXT("correction isolates one scope"),
                ClientMetrics.LastCorrectionScopeCount, 1);
              Test.TestTrue(TEXT("correction stays below full-world scope"),
                ClientMetrics.LastCorrectionEntityCount > 0
                  && ClientMetrics.LastCorrectionEntityCount
                    < ClientMetrics.MirrorEntityCount);
              Test.TestTrue(TEXT("correction records position error"),
                ClientMetrics.LastCorrectionBeforePositionErrorCm >= 100.0);
              Test.TestTrue(TEXT("correction removes position error"),
                ClientMetrics.LastCorrectionAfterPositionErrorCm
                  <= UE_DOUBLE_SMALL_NUMBER);
              Test.TestTrue(TEXT("recovery emits a sparse correction packet"),
                bObservedCorrectionTraffic);
              Test.TestTrue(TEXT("next digest has no repeated mismatch"),
                bObservedSingleDigestMismatch);
              return true;
            }
          }
        }
      }
      if (FPlatformTime::Seconds() >= DeadlineSeconds)
      {
        for (UWorld* World : PieWorlds)
        {
          if (UMassCrowdRuntimeSubsystem* Subsystem =
            World->GetSubsystem<UMassCrowdRuntimeSubsystem>())
          {
            const FCrowdAsyncSimulationRuntimeMetrics Metrics =
              Subsystem->GetAsyncSimulationRuntime().GetMetrics();
            Test.AddError(FString::Printf(
              TEXT("worker timeout role=%d state=%u epoch=%llu entities=%d resnapshots=%llu corrections=%llu input=%llu prediction_started=%d recovery_started=%d"),
              static_cast<int32>(World->GetNetMode()),
              static_cast<uint8>(
                Subsystem->GetAsyncSimulationRuntime().GetState()),
              Metrics.WorkerEpoch,
              Metrics.MirrorEntityCount,
              Metrics.ResnapshotCount,
              Metrics.AuthorityCorrectionCount,
              Metrics.LastAppliedInputSequence,
              bPredictionWindowStarted,
              bCorrectionRecoveryStarted));
          }
        }
        Test.AddError(
          TEXT("single-process dual PIE worker runtime timed out"));
        return true;
      }
      return false;
    }

  private:
    struct FRuntimeBaseline
    {
      uint64 Generation = 0;
      uint64 WorkerEpoch = 0;
      uint64 ResnapshotCount = 0;
      uint64 AuthorityCorrectionCount = 0;
      uint64 PublishedDirtyStateCount = 0;
      uint64 FullMirrorSerializationCount = 0;
    };
    struct FNetworkBaseline
    {
      uint64 CorrectionBytes = 0;
      uint64 CheckpointBytes = 0;
      uint64 DigestMismatchCount = 0;
    };
    FAutomationTestBase& Test;
    double DeadlineSeconds = 0.0;
    bool bPredictionWindowStarted = false;
    bool bCorrectionRecoveryStarted = false;
    uint64 MinimumPostCorrectionEpoch = 0;
    const UMassCrowdRuntimeSubsystem* CorruptedClientRuntime = nullptr;
    TMap<const UMassCrowdRuntimeSubsystem*, FRuntimeBaseline>
      RuntimeBaselines;
    TMap<const AMassCrowdReplicationActor*, FNetworkBaseline>
      NetworkBaselines;
  };

  class FWaitForPieTeardown final
    : public IAutomationLatentCommand
  {
  public:
    FWaitForPieTeardown(
      FAutomationTestBase& InTest,
      const double InTimeoutSeconds,
      const FSavedPlaySettings& InSaved)
      : Test(InTest)
      , DeadlineSeconds(
          FPlatformTime::Seconds() + InTimeoutSeconds)
      , Saved(InSaved)
    {
    }

    virtual bool Update() override
    {
      int32 PieWorldCount = 0;
      if (GEngine)
      {
        for (const FWorldContext& Context :
          GEngine->GetWorldContexts())
        {
          if (Context.WorldType == EWorldType::PIE
            && Context.World())
            ++PieWorldCount;
        }
      }
      if (PieWorldCount == 0
        || FPlatformTime::Seconds() >= DeadlineSeconds)
      {
        ULevelEditorPlaySettings* Settings =
          GetMutableDefault<ULevelEditorPlaySettings>();
        Settings->SetPlayNetMode(Saved.NetMode);
        Settings->SetRunUnderOneProcess(
          Saved.bRunUnderOneProcess);
        Settings->SetPlayNumberOfClients(Saved.ClientCount);
        Settings->bLaunchSeparateServer =
          Saved.bLaunchSeparateServer;
        Settings->EnableGameSound = Saved.bEnableGameSound;
        if (IConsoleVariable* CorrectionEnabled =
          IConsoleManager::Get().FindConsoleVariable(
            TEXT("crowd.Worker.AuthorityCorrectionEnabled")))
        {
          CorrectionEnabled->Set(
            Saved.AuthorityCorrectionEnabled,
            ECVF_SetByCode);
        }
        if (PieWorldCount != 0)
          Test.AddError(TEXT("PIE worlds did not teardown"));
        else
          Test.TestEqual(
            TEXT("all PIE worlds teardown"), PieWorldCount, 0);
        return true;
      }
      return false;
    }

  private:
    FAutomationTestBase& Test;
    double DeadlineSeconds = 0.0;
    FSavedPlaySettings Saved;
  };
}

using namespace CrowdDemoPersistentWorkerPIETestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoPersistentWorkerSingleProcessDualPIETest,
  "CrowdDemo.PersistentWorker.SingleProcessDualPIE",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPersistentWorkerSingleProcessDualPIETest::RunTest(
  const FString& Parameters)
{
  ULevelEditorPlaySettings* Settings =
    GetMutableDefault<ULevelEditorPlaySettings>();
  EPlayNetMode CurrentNetMode = PIE_Standalone;
  bool bCurrentRunUnderOneProcess = false;
  int32 CurrentClientCount = 1;
  Settings->GetPlayNetMode(CurrentNetMode);
  Settings->GetRunUnderOneProcess(
    bCurrentRunUnderOneProcess);
  Settings->GetPlayNumberOfClients(CurrentClientCount);
  FSavedPlaySettings Saved{
    CurrentNetMode,
    bCurrentRunUnderOneProcess,
    CurrentClientCount,
    Settings->bLaunchSeparateServer,
    Settings->EnableGameSound,
    1};

  if (IConsoleVariable* CorrectionEnabled =
    IConsoleManager::Get().FindConsoleVariable(
      TEXT("crowd.Worker.AuthorityCorrectionEnabled")))
  {
    Saved.AuthorityCorrectionEnabled =
      CorrectionEnabled->GetInt();
    CorrectionEnabled->Set(0, ECVF_SetByCode);
  }

  Settings->SetPlayNetMode(PIE_ListenServer);
  Settings->SetRunUnderOneProcess(true);
  Settings->SetPlayNumberOfClients(2);
  Settings->bLaunchSeparateServer = false;
  Settings->EnableGameSound = false;
  FCommandLine::Append(
    TEXT(" -CrowdDemoEntityCount=20")
    TEXT(" -CrowdDemoScenario=StaticTarget")
    TEXT(" -CrowdDemoDurationSeconds=40")
    TEXT(" -CrowdDemoAutomationRoundDurationSeconds=40")
    TEXT(" -CrowdWorkerMovementMode=Production")
    TEXT(" -CrowdWorkerBehaviorMode=Production")
    TEXT(" -unattended -NoSound"));

  ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
  ADD_LATENT_AUTOMATION_COMMAND(
    FWaitForDualPieWorkerRuntime(*this, 55.0));
  ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
  ADD_LATENT_AUTOMATION_COMMAND(
    FWaitForPieTeardown(*this, 30.0, Saved));
  return true;
}

#endif
