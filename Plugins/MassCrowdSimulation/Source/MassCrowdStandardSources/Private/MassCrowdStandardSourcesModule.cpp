#include "MassCrowdStandardSources.h"

#include "Modules/ModuleManager.h"

class FMassCrowdStandardSourcesModule final : public IModuleInterface
{
public:
  void StartupModule() override
  {
    Provider = CreateCrowdStandardSourcesProvider();
    verify(RegisterCrowdBehaviorSourceProvider(Provider.ToSharedRef()));
  }

private:
  TSharedPtr<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>
    Provider;
};

IMPLEMENT_MODULE(
  FMassCrowdStandardSourcesModule,
  MassCrowdStandardSources)
