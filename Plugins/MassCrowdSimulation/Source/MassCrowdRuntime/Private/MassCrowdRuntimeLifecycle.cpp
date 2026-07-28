#include "MassCrowdRuntimeLifecycle.h"

namespace
{
  constexpr uint64 FnvOffset = 14695981039346656037ull;
  constexpr uint64 FnvPrime = 1099511628211ull;

  void FoldByte(uint64& Hash, const uint8 Value)
  {
    Hash ^= Value;
    Hash *= FnvPrime;
  }

  template<typename T>
  void FoldUnsigned(uint64& Hash, const T Value)
  {
    for (uint32 ByteIndex = 0; ByteIndex < sizeof(T); ++ByteIndex)
      FoldByte(Hash, static_cast<uint8>(Value >> (ByteIndex * 8)));
  }

  void FoldRef(uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    FoldUnsigned(Hash, Ref.ProviderId);
    FoldUnsigned(Hash, Ref.StableEntityId);
    FoldUnsigned(Hash, Ref.LifecycleSerial);
  }

  void FoldFacts(uint64& Hash, const FCrowdAgentFacts& Facts)
  {
    FoldRef(Hash, Facts.StableEntityRef);
    FoldUnsigned(Hash, Facts.FactionKey);
    FoldUnsigned(Hash, Facts.CapabilitySet.Bits);
    FoldByte(Hash, static_cast<uint8>(Facts.ActiveBehavior));
    FoldRef(Hash, Facts.BusinessTaskRef);
    FoldRef(Hash, Facts.TargetRef);
    FoldUnsigned(Hash, Facts.MovementProfileKey);
    FoldUnsigned(Hash, Facts.PresentationProfileKey);
    FoldUnsigned(Hash, Facts.RuntimeState);
  }

  bool IsStrictlySorted(
    const FCrowdStableEntityRef& Previous,
    const FCrowdStableEntityRef& Current)
  {
    return Previous < Current && !Previous.IsSameEntitySlot(Current);
  }
}

bool FCrowdMassRuntimeLifecycleStore::Initialize(
  FMassEntityManager& InEntityManager,
  const FMassArchetypeHandle& InArchetype,
  const TConstArrayView<FCrowdMassRuntimeLifecycleEntity> Entities)
{
  if (bInitialized || !InArchetype.IsValid()) return false;
  FCrowdStableEntityRef Previous;
  bool bHasPrevious = false;
  for (const FCrowdMassRuntimeLifecycleEntity& Entity : Entities)
  {
    const FCrowdStableEntityRef& Ref = Entity.AgentFacts.StableEntityRef;
    if (!Entity.AgentFacts.IsWellFormed()
      || (bHasPrevious && !IsStrictlySorted(Previous, Ref)))
    {
      return false;
    }
    Previous = Ref;
    bHasPrevious = true;
  }

  EntityManager = &InEntityManager;
  Archetype = InArchetype;
  TArray<FEntityRecord> CreatedRecords;
  for (const FCrowdMassRuntimeLifecycleEntity& Entity : Entities)
  {
    FMassEntityHandle Handle;
    if (!CreateEntity(Entity.AgentFacts, Entity.MembershipKey, Handle))
    {
      for (const FEntityRecord& Created : CreatedRecords)
      {
        if (EntityManager->IsEntityValid(Created.Entity))
          EntityManager->DestroyEntity(Created.Entity);
      }
      EntityManager = nullptr;
      Archetype = {};
      return false;
    }
    CreatedRecords.Add(FEntityRecord{Entity.AgentFacts.StableEntityRef, Handle, true});
  }
  EntityRecords = MoveTemp(CreatedRecords);
  bInitialized = true;
  return true;
}

bool FCrowdMassRuntimeLifecycleStore::Spawn(
  const TConstArrayView<FCrowdMassRuntimeLifecycleEntity> Entities)
{
  if (!bInitialized || !EntityManager || Entities.IsEmpty()) return false;
  FCrowdStableEntityRef Previous;
  bool bHasPrevious = false;
  for (const FCrowdMassRuntimeLifecycleEntity& Entity : Entities)
  {
    const FCrowdStableEntityRef& Ref = Entity.AgentFacts.StableEntityRef;
    if (!Entity.AgentFacts.IsWellFormed()
      || (bHasPrevious && !IsStrictlySorted(Previous, Ref))) return false;
    const int32 SlotIndex = FindSlot(Ref);
    if (SlotIndex != INDEX_NONE
      && (EntityRecords[SlotIndex].bActive
        || Ref.LifecycleSerial <= EntityRecords[SlotIndex].EntityRef.LifecycleSerial))
    {
      return false;
    }
    Previous = Ref;
    bHasPrevious = true;
  }

  TArray<FEntityRecord> CreatedRecords;
  for (const FCrowdMassRuntimeLifecycleEntity& Entity : Entities)
  {
    FMassEntityHandle Handle;
    if (!CreateEntity(Entity.AgentFacts, Entity.MembershipKey, Handle))
    {
      for (const FEntityRecord& Created : CreatedRecords)
      {
        if (EntityManager->IsEntityValid(Created.Entity))
          EntityManager->DestroyEntity(Created.Entity);
      }
      return false;
    }
    CreatedRecords.Add(FEntityRecord{Entity.AgentFacts.StableEntityRef, Handle, true});
  }
  for (const FEntityRecord& Created : CreatedRecords)
  {
    const int32 SlotIndex = FindSlot(Created.EntityRef);
    if (SlotIndex == INDEX_NONE) EntityRecords.Add(Created);
    else EntityRecords[SlotIndex] = Created;
  }
  EntityRecords.Sort([](const FEntityRecord& A, const FEntityRecord& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  return true;
}

bool FCrowdMassRuntimeLifecycleStore::Despawn(
  const TConstArrayView<FCrowdStableEntityRef> EntityRefs)
{
  if (!bInitialized || !EntityManager || EntityRefs.IsEmpty()) return false;
  TArray<int32> RecordIndices;
  FCrowdStableEntityRef Previous;
  bool bHasPrevious = false;
  for (const FCrowdStableEntityRef& Ref : EntityRefs)
  {
    if (!Ref.IsValid() || (bHasPrevious && !IsStrictlySorted(Previous, Ref))) return false;
    const int32 Index = FindSlot(Ref);
    if (Index == INDEX_NONE
      || !EntityRecords[Index].bActive
      || !(EntityRecords[Index].EntityRef == Ref)
      || !EntityManager->IsEntityValid(EntityRecords[Index].Entity)) return false;
    RecordIndices.Add(Index);
    Previous = Ref;
    bHasPrevious = true;
  }
  for (const int32 Index : RecordIndices)
  {
    EntityManager->DestroyEntity(EntityRecords[Index].Entity);
    EntityRecords[Index].Entity = {};
    EntityRecords[Index].bActive = false;
  }
  return true;
}

bool FCrowdMassRuntimeLifecycleStore::UpdateMembership(
  const TConstArrayView<FCrowdMassRuntimeMembershipChange> Changes)
{
  if (!bInitialized || !EntityManager || Changes.IsEmpty()) return false;
  TArray<FCrowdMassMembershipFragment*> Fragments;
  FCrowdStableEntityRef Previous;
  bool bHasPrevious = false;
  for (const FCrowdMassRuntimeMembershipChange& Change : Changes)
  {
    if (!Change.EntityRef.IsValid()
      || Change.PreviousMembershipKey == Change.NewMembershipKey
      || (bHasPrevious && !IsStrictlySorted(Previous, Change.EntityRef))) return false;
    FMassEntityHandle Entity;
    if (!TryGetEntityHandle(Change.EntityRef, Entity)) return false;
    FCrowdMassMembershipFragment* Fragment =
      EntityManager->GetFragmentDataPtr<FCrowdMassMembershipFragment>(Entity);
    if (!Fragment || Fragment->MembershipKey != Change.PreviousMembershipKey) return false;
    Fragments.Add(Fragment);
    Previous = Change.EntityRef;
    bHasPrevious = true;
  }
  for (int32 Index = 0; Index < Changes.Num(); ++Index)
    Fragments[Index]->MembershipKey = Changes[Index].NewMembershipKey;
  return true;
}

bool FCrowdMassRuntimeLifecycleStore::ApplyAgentFactsCorrection(
  const FCrowdAgentFacts& CorrectedFacts)
{
  return ApplyAgentFactsCorrectionsAtomic(
    MakeArrayView(&CorrectedFacts, 1));
}

bool FCrowdMassRuntimeLifecycleStore::ApplyAgentFactsCorrectionsAtomic(
  const TConstArrayView<FCrowdAgentFacts> CorrectedFacts)
{
  if (CorrectedFacts.IsEmpty() || !EntityManager) return false;
  TArray<FCrowdMassBehaviorFragment*> Behaviors;
  Behaviors.Reserve(CorrectedFacts.Num());
  FCrowdStableEntityRef PreviousRef;
  for (const FCrowdAgentFacts& Facts : CorrectedFacts)
  {
    if (!Facts.IsWellFormed()
      || (PreviousRef.IsValid()
        && !(PreviousRef < Facts.StableEntityRef)))
      return false;
    FMassEntityHandle Entity;
    if (!TryGetEntityHandle(Facts.StableEntityRef, Entity)) return false;
    const FCrowdMassAgentFragment* Identity =
      EntityManager->GetFragmentDataPtr<FCrowdMassAgentFragment>(Entity);
    FCrowdMassBehaviorFragment* Behavior =
      EntityManager->GetFragmentDataPtr<FCrowdMassBehaviorFragment>(Entity);
    if (!Identity || !Behavior
      || !(Identity->GetStableEntityRef() == Facts.StableEntityRef))
      return false;
    Behaviors.Add(Behavior);
    PreviousRef = Facts.StableEntityRef;
  }
  for (int32 Index = 0; Index < CorrectedFacts.Num(); ++Index)
    Behaviors[Index]->SetAgentFacts(CorrectedFacts[Index]);
  return true;
}

void FCrowdMassRuntimeLifecycleStore::Reset()
{
  if (EntityManager)
  {
    for (const FEntityRecord& Record : EntityRecords)
      if (Record.bActive && EntityManager->IsEntityValid(Record.Entity))
        EntityManager->DestroyEntity(Record.Entity);
  }
  EntityManager = nullptr;
  Archetype = {};
  EntityRecords.Reset();
  bInitialized = false;
}

int32 FCrowdMassRuntimeLifecycleStore::GetActiveEntityCount() const
{
  int32 Count = 0;
  for (const FEntityRecord& Record : EntityRecords) Count += Record.bActive ? 1 : 0;
  return Count;
}

uint64 FCrowdMassRuntimeLifecycleStore::CalculateEntitySetHash() const
{
  if (!bInitialized || !EntityManager) return 0;
  uint64 Hash = FnvOffset;
  FoldUnsigned(Hash, uint16{1});
  for (const FEntityRecord& Record : EntityRecords)
  {
    if (!Record.bActive) continue;
    if (!EntityManager->IsEntityValid(Record.Entity)) return 0;
    const FCrowdMassAgentFragment* Identity =
      EntityManager->GetFragmentDataPtr<FCrowdMassAgentFragment>(Record.Entity);
    const FCrowdMassBehaviorFragment* Behavior =
      EntityManager->GetFragmentDataPtr<FCrowdMassBehaviorFragment>(Record.Entity);
    const FCrowdMassMembershipFragment* Membership =
      EntityManager->GetFragmentDataPtr<FCrowdMassMembershipFragment>(Record.Entity);
    if (!Identity || !Behavior || !Membership) return 0;
    const FCrowdAgentFacts Facts = Behavior->GetAgentFacts(*Identity);
    if (!Facts.IsWellFormed() || !(Facts.StableEntityRef == Record.EntityRef)) return 0;
    FoldFacts(Hash, Facts);
    FoldUnsigned(Hash, Membership->MembershipKey);
  }
  return Hash;
}

bool FCrowdMassRuntimeLifecycleStore::TryGetEntityHandle(
  const FCrowdStableEntityRef& EntityRef,
  FMassEntityHandle& OutEntity) const
{
  const int32 Index = FindSlot(EntityRef);
  if (Index == INDEX_NONE
    || !EntityRecords[Index].bActive
    || !(EntityRecords[Index].EntityRef == EntityRef)
    || !EntityManager
    || !EntityManager->IsEntityValid(EntityRecords[Index].Entity)) return false;
  OutEntity = EntityRecords[Index].Entity;
  return true;
}

bool FCrowdMassRuntimeLifecycleStore::CreateEntity(
  const FCrowdAgentFacts& AgentFacts,
  const uint32 MembershipKey,
  FMassEntityHandle& OutEntity)
{
  if (!EntityManager || !Archetype.IsValid() || !AgentFacts.IsWellFormed()) return false;
  OutEntity = EntityManager->CreateEntity(Archetype);
  if (!EntityManager->IsEntityValid(OutEntity)) return false;
  FCrowdMassAgentFragment* Identity =
    EntityManager->GetFragmentDataPtr<FCrowdMassAgentFragment>(OutEntity);
  FCrowdMassBehaviorFragment* Behavior =
    EntityManager->GetFragmentDataPtr<FCrowdMassBehaviorFragment>(OutEntity);
  FCrowdMassMembershipFragment* Membership =
    EntityManager->GetFragmentDataPtr<FCrowdMassMembershipFragment>(OutEntity);
  if (!Identity || !Behavior || !Membership)
  {
    EntityManager->DestroyEntity(OutEntity);
    OutEntity = {};
    return false;
  }
  Identity->AgentId = AgentFacts.StableEntityRef.StableEntityId <= static_cast<uint64>(MAX_int32)
    ? static_cast<int32>(AgentFacts.StableEntityRef.StableEntityId)
    : INDEX_NONE;
  Identity->SetStableEntityRef(AgentFacts.StableEntityRef);
  Behavior->SetAgentFacts(AgentFacts);
  Membership->MembershipKey = MembershipKey;
  return true;
}

int32 FCrowdMassRuntimeLifecycleStore::FindSlot(
  const FCrowdStableEntityRef& EntityRef) const
{
  for (int32 Index = 0; Index < EntityRecords.Num(); ++Index)
  {
    if (EntityRecords[Index].EntityRef.IsSameEntitySlot(EntityRef)) return Index;
  }
  return INDEX_NONE;
}
