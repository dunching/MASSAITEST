#include "MassCrowdProjectileMassStore.h"

#include "MassCommonFragments.h"
#include "MassCrowdProjectileFragments.h"
#include "MassEntityManager.h"

bool FCrowdMassProjectileStore::Grow(
  FMassEntityManager& EntityManager,
  const int32 AddCount)
{
  if (AddCount <= 0)
    return true;
  const TArray<const UScriptStruct*> Fragments = {
    FCrowdMassProjectileTag::StaticStruct(),
    FCrowdMassProjectileFragment::StaticStruct(),
    FTransformFragment::StaticStruct()};
  const FMassArchetypeHandle Archetype =
    EntityManager.CreateArchetype(Fragments);
  if (!Archetype.IsValid())
    return false;
  TArray<FMassEntityHandle> Added;
  const TSharedRef<FMassEntityManager::FEntityCreationContext>
    CreationContext = EntityManager.BatchCreateEntities(
      Archetype, AddCount, Added);
  if (Added.Num() != AddCount)
    return false;
  for (const FMassEntityHandle Entity : Added)
  {
    EntityManager.GetFragmentDataChecked<
      FCrowdMassProjectileFragment>(Entity) = {};
    EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity)
      .GetMutableTransform().SetLocation(
        FVector(0.0f, 0.0f, -100000.0f));
  }
  Entities.Append(Added);
  return true;
}

bool FCrowdMassProjectileStore::EnsureCapacity(
  FMassEntityManager& EntityManager,
  const int32 RequiredCount,
  const int32 MaximumCapacity)
{
  if (RequiredCount < 0 || MaximumCapacity <= 0
    || RequiredCount > MaximumCapacity)
    return false;
  if (RequiredCount > Entities.Num()
    && !Grow(EntityManager, RequiredCount - Entities.Num()))
    return false;
  return Entities.Num() >= RequiredCount;
}

void FCrowdMassProjectileStore::DestroyAll(
  FMassEntityManager& EntityManager)
{
  for (const FMassEntityHandle Entity : Entities)
  {
    if (EntityManager.IsEntityValid(Entity))
      EntityManager.DestroyEntity(Entity);
  }
  Entities.Reset();
}

bool FCrowdMassProjectileStore::Gather(
  const FMassEntityManager& EntityManager,
  TArray<FCrowdProjectileState>& OutStates) const
{
  OutStates.Reset();
  for (const FMassEntityHandle Entity : Entities)
  {
    if (!EntityManager.IsEntityValid(Entity))
      return false;
    const FCrowdMassProjectileFragment& Fragment =
      EntityManager.GetFragmentDataChecked<
        FCrowdMassProjectileFragment>(Entity);
    if (!Fragment.bActive || Fragment.ProjectileId == 0)
      continue;
    FCrowdProjectileState& State =
      OutStates.AddDefaulted_GetRef();
    State.ProjectileId = Fragment.ProjectileId;
    State.Instigator = Fragment.GetInstigator();
    State.Target = Fragment.GetTarget();
    State.LastHitTarget = Fragment.GetLastHitTarget();
    State.FireSequence = Fragment.FireSequence;
    State.SpawnFixedStep = Fragment.SpawnFixedStep;
    State.AgeFixedSteps = Fragment.AgeFixedSteps;
    State.RemainingPierces = Fragment.RemainingPierces;
    State.SourceFactionId = Fragment.SourceFactionId;
    State.NavLayer = Fragment.NavLayer;
    State.ProjectileProfileId = Fragment.ProjectileProfileId;
    State.CollisionProfileId = Fragment.CollisionProfileId;
    State.EffectProfileId = Fragment.EffectProfileId;
    State.PreviousPosition = Fragment.PreviousPosition;
    State.Position = Fragment.Position;
    State.Velocity = Fragment.Velocity;
    State.RadiusCm = Fragment.RadiusCm;
    State.bActive = Fragment.bActive;
    State.bImpacted = Fragment.bImpacted;
    State.bExpired = Fragment.bExpired;
    if (!State.IsValid())
      return false;
  }
  OutStates.Sort([](
    const FCrowdProjectileState& A,
    const FCrowdProjectileState& B)
  {
    return A.ProjectileId < B.ProjectileId;
  });
  for (int32 Index = 1; Index < OutStates.Num(); ++Index)
  {
    if (OutStates[Index - 1].ProjectileId
      == OutStates[Index].ProjectileId)
      return false;
  }
  return true;
}

bool FCrowdMassProjectileStore::ValidatePreparedStates(
  const TConstArrayView<FCrowdProjectileState> States) const
{
  int32 ActiveCount = 0;
  uint64 PreviousId = 0;
  TArray<FCrowdProjectileState> Sorted(States);
  Sorted.Sort([](
    const FCrowdProjectileState& A,
    const FCrowdProjectileState& B)
  {
    return A.ProjectileId < B.ProjectileId;
  });
  for (const FCrowdProjectileState& State : Sorted)
  {
    if (!State.IsValid()
      || (PreviousId != 0 && PreviousId == State.ProjectileId))
      return false;
    PreviousId = State.ProjectileId;
    ActiveCount += State.bActive ? 1 : 0;
  }
  return ActiveCount <= Entities.Num();
}

void FCrowdMassProjectileStore::ApplyValidated(
  FMassEntityManager& EntityManager,
  const TConstArrayView<FCrowdProjectileState> States)
{
  check(ValidatePreparedStates(States));
  TArray<FCrowdProjectileState> Active;
  for (const FCrowdProjectileState& State : States)
  {
    if (State.bActive)
      Active.Add(State);
  }
  Active.Sort([](
    const FCrowdProjectileState& A,
    const FCrowdProjectileState& B)
  {
    return A.ProjectileId < B.ProjectileId;
  });
  for (int32 Index = 0; Index < Entities.Num(); ++Index)
  {
    const FMassEntityHandle Entity = Entities[Index];
    check(EntityManager.IsEntityValid(Entity));
    FCrowdMassProjectileFragment& Fragment =
      EntityManager.GetFragmentDataChecked<
        FCrowdMassProjectileFragment>(Entity);
    FTransform& Transform =
      EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity)
        .GetMutableTransform();
    if (!Active.IsValidIndex(Index))
    {
      Fragment = {};
      Transform.SetLocation(FVector(0.0f, 0.0f, -100000.0f));
      continue;
    }
    const FCrowdProjectileState& State = Active[Index];
    Fragment.ProjectileId = State.ProjectileId;
    Fragment.InstigatorProviderId = State.Instigator.ProviderId;
    Fragment.InstigatorStableEntityId =
      State.Instigator.StableEntityId;
    Fragment.InstigatorLifecycleSerial =
      State.Instigator.LifecycleSerial;
    Fragment.TargetProviderId = State.Target.ProviderId;
    Fragment.TargetStableEntityId = State.Target.StableEntityId;
    Fragment.TargetLifecycleSerial = State.Target.LifecycleSerial;
    Fragment.LastHitTargetProviderId =
      State.LastHitTarget.ProviderId;
    Fragment.LastHitTargetStableEntityId =
      State.LastHitTarget.StableEntityId;
    Fragment.LastHitTargetLifecycleSerial =
      State.LastHitTarget.LifecycleSerial;
    Fragment.FireSequence = State.FireSequence;
    Fragment.SpawnFixedStep = State.SpawnFixedStep;
    Fragment.AgeFixedSteps = State.AgeFixedSteps;
    Fragment.RemainingPierces = State.RemainingPierces;
    Fragment.SourceFactionId = State.SourceFactionId;
    Fragment.NavLayer = State.NavLayer;
    Fragment.ProjectileProfileId = State.ProjectileProfileId;
    Fragment.CollisionProfileId = State.CollisionProfileId;
    Fragment.EffectProfileId = State.EffectProfileId;
    Fragment.PreviousPosition = State.PreviousPosition;
    Fragment.Position = State.Position;
    Fragment.Velocity = State.Velocity;
    Fragment.RadiusCm = State.RadiusCm;
    Fragment.bActive = State.bActive;
    Fragment.bImpacted = State.bImpacted;
    Fragment.bExpired = State.bExpired;
    Transform.SetLocation(State.Position);
  }
}
