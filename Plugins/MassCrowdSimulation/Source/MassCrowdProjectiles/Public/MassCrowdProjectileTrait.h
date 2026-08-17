#pragma once

#include "MassEntityTraitBase.h"
#include "MassCrowdProjectileTrait.generated.h"

UCLASS(BlueprintType, EditInlineNew, meta=(DisplayName="Mass Crowd Projectile"))
class MASSCROWDPROJECTILES_API UMassCrowdProjectileTrait
  : public UMassEntityTraitBase
{
  GENERATED_BODY()

protected:
  virtual void BuildTemplate(
    FMassEntityTemplateBuildContext& BuildContext,
    const UWorld& World) const override;
};
