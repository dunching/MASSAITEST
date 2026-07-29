#include "MassCrowdDemoBusinessModule.h"

#include "CrowdDemoBusinessSourceProvider.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "Modules/ModuleManager.h"

class FMassCrowdDemoBusinessModule final
  : public IMassCrowdDemoBusinessModule
{
public:
  void StartupModule() override
  {
    Provider = CreateCrowdDemoBehaviorSourceProvider();
    verify(RegisterCrowdBehaviorSourceProvider(Provider.ToSharedRef()));
  }

  void ShutdownModule() override
  {
    if (Provider.IsValid())
    {
      UnregisterCrowdBehaviorSourceProvider(
        CrowdDemoBehaviorSchemas::Provider);
      Provider.Reset();
    }
  }

private:
  TSharedPtr<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>
    Provider;
};

IMPLEMENT_MODULE(
  FMassCrowdDemoBusinessModule,
  MassCrowdDemoBusiness)
