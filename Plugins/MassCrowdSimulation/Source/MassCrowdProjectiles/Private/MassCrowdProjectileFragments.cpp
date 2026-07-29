#include "MassCrowdProjectileFragments.h"

FCrowdStableEntityRef FCrowdMassProjectileFragment::GetInstigator() const
{
  return {
    InstigatorProviderId,
    InstigatorStableEntityId,
    InstigatorLifecycleSerial};
}

FCrowdStableEntityRef FCrowdMassProjectileFragment::GetTarget() const
{
  return {
    TargetProviderId,
    TargetStableEntityId,
    TargetLifecycleSerial};
}

FCrowdStableEntityRef FCrowdMassProjectileFragment::GetLastHitTarget() const
{
  return {
    LastHitTargetProviderId,
    LastHitTargetStableEntityId,
    LastHitTargetLifecycleSerial};
}
