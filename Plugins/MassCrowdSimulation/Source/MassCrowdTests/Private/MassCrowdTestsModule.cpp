#include "MassCrowdTestBehaviorProvider.h"
#include "Modules/ModuleManager.h"

class FMassCrowdTestsModule final : public IModuleInterface
{
public:
  virtual void StartupModule() override
  {
    Provider = CreateMassCrowdTestBehaviorProvider();
    verify(RegisterCrowdBehaviorSourceProvider(Provider.ToSharedRef()));
  }

private:
  TSharedPtr<const ICrowdBehaviorSourceProvider, ESPMode::ThreadSafe>
    Provider;
};

IMPLEMENT_MODULE(FMassCrowdTestsModule, MassCrowdTests)
