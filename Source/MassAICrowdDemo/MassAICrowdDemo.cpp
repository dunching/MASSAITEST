#include "MassAICrowdDemo.h"

#include "CrowdDemoBehaviorSourceProvider.h"
#include "Modules/ModuleManager.h"

class FMassAICrowdDemoModule final : public FDefaultGameModuleImpl
{
public:
  virtual void StartupModule() override
  {
    FDefaultGameModuleImpl::StartupModule();
    BehaviorProvider = CreateCrowdDemoBehaviorSourceProvider();
    verify(RegisterCrowdBehaviorSourceProvider(
      BehaviorProvider.ToSharedRef()));
  }

private:
  TSharedPtr<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>
    BehaviorProvider;
};

IMPLEMENT_PRIMARY_GAME_MODULE(
  FMassAICrowdDemoModule,
  MassAICrowdDemo,
  "MassAICrowdDemo");
