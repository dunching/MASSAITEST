#pragma once

#include "MassEntityTraitBase.h"
#include "MassCrowdMovementTrait.generated.h"

UCLASS(BlueprintType, EditInlineNew, meta=(DisplayName="Mass Crowd Movement"))
class MASSCROWDRUNTIME_API UMassCrowdMovementTrait : public UMassEntityTraitBase
{
  GENERATED_BODY()

public:
  UPROPERTY(EditAnywhere, Category="Crowd")
  float PhysicalRadiusCm = 42.0f;

  UPROPERTY(EditAnywhere, Category="Crowd")
  float MaximumSpeedCmps = 300.0f;

  UPROPERTY(EditAnywhere, Category="Crowd")
  uint32 CapabilityProfileKey = 0;

protected:
  virtual void BuildTemplate(
    FMassEntityTemplateBuildContext& BuildContext,
    const UWorld& World) const override;
};
