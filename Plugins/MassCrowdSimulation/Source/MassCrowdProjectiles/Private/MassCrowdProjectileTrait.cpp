#include "MassCrowdProjectileTrait.h"

#include "MassCommonFragments.h"
#include "MassCrowdProjectileFragments.h"
#include "MassEntityTemplateRegistry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassCrowdProjectileTrait)

void UMassCrowdProjectileTrait::BuildTemplate(
  FMassEntityTemplateBuildContext& BuildContext,
  const UWorld& World) const
{
  BuildContext.AddTag<FCrowdMassProjectileTag>();
  BuildContext.AddFragment<FCrowdMassProjectileFragment>();
  BuildContext.AddFragment<FTransformFragment>();
}
