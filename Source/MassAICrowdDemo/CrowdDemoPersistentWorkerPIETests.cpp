#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"
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
        for (UWorld* World : PieWorlds)
        {
          UMassCrowdRuntimeSubsystem* Subsystem =
            World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
          if (!Subsystem)
            continue;
          Subsystems.Add(Subsystem);
          if (World->GetNetMode() == NM_ListenServer
            || World->GetNetMode() == NM_DedicatedServer)
          {
            const FCrowdAsyncSimulationRuntimeMetrics Metrics =
              Subsystem->GetAsyncSimulationRuntime().GetMetrics();
            bServerRuntimeReady =
              Metrics.MirrorEntityCount == 20
              && Metrics.ResnapshotCount >= 1
              && !Metrics.bRequiresResnapshot
              && Subsystem->GetWorkerMovementAuthority().GetMode()
                == ECrowdWorkerMovementAuthorityMode::Production;
          }
        }
        if (Subsystems.Num() == PieWorlds.Num()
          && bServerRuntimeReady)
        {
          Test.TestTrue(
            TEXT("single process owns at least two PIE worlds"),
            PieWorlds.Num() >= 2);
          Test.TestEqual(
            TEXT("every PIE world owns a distinct runtime subsystem"),
            Subsystems.Num(), PieWorlds.Num());
          Test.TestTrue(
            TEXT("server persistent worker runtime reaches production"),
            bServerRuntimeReady);
          return true;
        }
      }
      if (FPlatformTime::Seconds() >= DeadlineSeconds)
      {
        Test.AddError(
          TEXT("single-process dual PIE worker runtime timed out"));
        return true;
      }
      return false;
    }

  private:
    FAutomationTestBase& Test;
    double DeadlineSeconds = 0.0;
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
  const FSavedPlaySettings Saved{
    CurrentNetMode,
    bCurrentRunUnderOneProcess,
    CurrentClientCount,
    Settings->bLaunchSeparateServer,
    Settings->EnableGameSound};

  Settings->SetPlayNetMode(PIE_ListenServer);
  Settings->SetRunUnderOneProcess(true);
  Settings->SetPlayNumberOfClients(2);
  Settings->bLaunchSeparateServer = false;
  Settings->EnableGameSound = false;
  FCommandLine::Append(
    TEXT(" -CrowdDemoEntityCount=20")
    TEXT(" -CrowdDemoScenario=StaticTarget")
    TEXT(" -CrowdDemoDurationSeconds=20")
    TEXT(" -CrowdWorkerMovementMode=Production")
    TEXT(" -unattended -NoSound"));

  ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
  ADD_LATENT_AUTOMATION_COMMAND(
    FWaitForDualPieWorkerRuntime(*this, 60.0));
  ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
  ADD_LATENT_AUTOMATION_COMMAND(
    FWaitForPieTeardown(*this, 30.0, Saved));
  return true;
}

#endif
