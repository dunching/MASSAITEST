#include "Modules/ModuleManager.h"

class FMassCrowdStateTreeAdapterModule final : public IModuleInterface
{
};

IMPLEMENT_MODULE(
  FMassCrowdStateTreeAdapterModule,
  MassCrowdStateTreeAdapter)
