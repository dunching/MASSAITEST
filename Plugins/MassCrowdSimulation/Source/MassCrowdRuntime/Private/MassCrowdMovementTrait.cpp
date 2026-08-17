#include "MassCrowdMovementTrait.h"

#include "MassCrowdRuntimeFragments.h"
#include "MassEntityTemplateRegistry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassCrowdMovementTrait)

void UMassCrowdMovementTrait::BuildTemplate(
  FMassEntityTemplateBuildContext& BuildContext,
  const UWorld& World) const
{
  BuildContext.AddTag<FCrowdMassAgentTag>();
  BuildContext.AddFragment<FCrowdMassAgentFragment>();
  FCrowdMassBehaviorFragment& Behavior =
    BuildContext.AddFragment_GetRef<FCrowdMassBehaviorFragment>();
  FCrowdCapabilitySet BaseCapabilities;
  BaseCapabilities.Add(ECrowdCapability::Move);
  BaseCapabilities.Add(ECrowdCapability::MoveTo);
  Behavior.CapabilityBits = BaseCapabilities.Bits;
  Behavior.DerivedBehaviorLabel = 0;
  BuildContext.AddFragment<FCrowdMassSimulationStateFragment>();
  FCrowdMassPropertiesFragment& Properties =
    BuildContext.AddFragment_GetRef<FCrowdMassPropertiesFragment>();
  Properties.PhysicalRadiusCm = PhysicalRadiusCm;
  Properties.MaximumSpeedCmps = MaximumSpeedCmps;
  Properties.CapabilityProfileKey = CapabilityProfileKey;
  BuildContext.AddFragment<FCrowdMassFacingFragment>();
  BuildContext.AddFragment<FCrowdMassMovementOutputFragment>();
}
