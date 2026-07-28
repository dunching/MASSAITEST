#include "MassCrowdPresentationSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassCrowdPresentationSubsystem)

namespace
{
  constexpr uint64 FnvOffset = 14695981039346656037ull;
  constexpr uint64 FnvPrime = 1099511628211ull;

  uint64 Fold(uint64 Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>((Value >> (Byte * 8)) & 0xffull);
      Hash *= FnvPrime;
    }
    return Hash;
  }

  uint64 FoldRef(uint64 Hash, const FCrowdStableEntityRef& Ref)
  {
    Hash = Fold(Hash, Ref.ProviderId);
    Hash = Fold(Hash, Ref.StableEntityId);
    return Fold(Hash, Ref.LifecycleSerial);
  }

  uint64 FoldDouble(uint64 Hash, const double Value)
  {
    uint64 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    return Fold(Hash, Bits);
  }

  uint64 CalculatePreparedFrameHash(
    const FCrowdPreparedPresentationFrame& Frame)
  {
    uint64 Hash = Fold(FnvOffset, Frame.Version);
    Hash = Fold(Hash, Frame.SourceFrameHash);
    Hash = Fold(Hash, Frame.Operations.Num());
    for (const FCrowdPresentationOperation& Operation : Frame.Operations)
    {
      Hash = Fold(Hash, static_cast<uint8>(Operation.Kind));
      Hash = FoldRef(Hash, Operation.EntityRef);
      Hash = Fold(Hash, Operation.ProfileKey);
      Hash = Fold(Hash, Operation.Sequence);
      if (Operation.Kind != ECrowdPresentationOperationKind::Despawn)
      {
        Hash = FoldRef(Hash, Operation.State.EntityRef);
        Hash = Fold(Hash, Operation.State.ProfileKey);
        Hash = Fold(Hash, Operation.State.VisualState);
        Hash = FoldRef(Hash, Operation.State.CargoRef);
        Hash = Fold(Hash, Operation.State.Sequence);
        const FTransform& Transform = Operation.State.Transform;
        const FVector Location = Transform.GetLocation();
        const FQuat Rotation = Transform.GetRotation();
        const FVector Scale = Transform.GetScale3D();
        Hash = FoldDouble(Hash, Location.X);
        Hash = FoldDouble(Hash, Location.Y);
        Hash = FoldDouble(Hash, Location.Z);
        Hash = FoldDouble(Hash, Rotation.X);
        Hash = FoldDouble(Hash, Rotation.Y);
        Hash = FoldDouble(Hash, Rotation.Z);
        Hash = FoldDouble(Hash, Rotation.W);
        Hash = FoldDouble(Hash, Scale.X);
        Hash = FoldDouble(Hash, Scale.Y);
        Hash = FoldDouble(Hash, Scale.Z);
        Hash = FoldDouble(Hash, Operation.State.CustomData.X);
        Hash = FoldDouble(Hash, Operation.State.CustomData.Y);
        Hash = FoldDouble(Hash, Operation.State.CustomData.Z);
        Hash = FoldDouble(
          Hash, Operation.State.SampleServerSeconds);
      }
    }
    return Hash;
  }

  bool IsValidState(const FCrowdPresentationState& State)
  {
    return State.EntityRef.IsValid() && State.Sequence > 0
      && State.ProfileKey != 0
      && !State.Transform.ContainsNaN()
      && FMath::IsFinite(State.CustomData.X)
      && FMath::IsFinite(State.CustomData.Y)
      && FMath::IsFinite(State.CustomData.Z)
      && FMath::IsFinite(State.SampleServerSeconds)
      && (State.CargoRef.IsUnset() || State.CargoRef.IsValid());
  }

  bool IsEquivalent(
    const FCrowdPresentationState& A,
    const FCrowdPresentationState& B)
  {
    return A.EntityRef == B.EntityRef && A.ProfileKey == B.ProfileKey
      && A.VisualState == B.VisualState && A.CargoRef == B.CargoRef
      && A.CustomData == B.CustomData
      && A.Sequence == B.Sequence
      && A.Transform.Equals(B.Transform)
      && A.SampleServerSeconds == B.SampleServerSeconds;
  }
}

FCrowdPresentationSlotTable::FCrowdPresentationSlotTable(
  TSharedRef<ICrowdPresentationInstanceSink> InSink)
  : Sink(MoveTemp(InSink))
{
}

ECrowdPresentationApplyResult FCrowdPresentationSlotTable::ApplySpawn(
  const FCrowdPresentationState& State)
{
  const ECrowdPresentationApplyResult Validation = ValidateSpawn(State);
  if (Validation != ECrowdPresentationApplyResult::Applied)
    return Validation;
  const int32 ExpectedSlot = SlotToEntity.Num();
  const int32 ActualSlot = Sink->AddInstance(State);
  if (ActualSlot != ExpectedSlot) return ECrowdPresentationApplyResult::Rejected;
  SlotToEntity.Add(State.EntityRef);
  EntityToSlot.Add(State.EntityRef, ActualSlot);
  States.Add(State.EntityRef, State);
  return ECrowdPresentationApplyResult::Applied;
}

ECrowdPresentationApplyResult FCrowdPresentationSlotTable::ApplyUpdate(
  const FCrowdPresentationState& State)
{
  const ECrowdPresentationApplyResult Validation = ValidateUpdate(State);
  if (Validation != ECrowdPresentationApplyResult::Applied)
    return Validation;
  FCrowdPresentationState* Existing = States.Find(State.EntityRef);
  const int32* Slot = EntityToSlot.Find(State.EntityRef);
  check(Existing && Slot);
  if (!Sink->UpdateInstance(*Slot, State))
    return ECrowdPresentationApplyResult::Rejected;
  *Existing = State;
  return ECrowdPresentationApplyResult::Applied;
}

ECrowdPresentationApplyResult FCrowdPresentationSlotTable::ApplyDespawn(
  const FCrowdStableEntityRef& EntityRef,
  const uint64 Sequence)
{
  const ECrowdPresentationApplyResult Validation =
    ValidateDespawn(EntityRef, Sequence);
  if (Validation != ECrowdPresentationApplyResult::Applied
    && Validation != ECrowdPresentationApplyResult::MissingEntity)
    return Validation;
  const int32* SlotPtr = EntityToSlot.Find(EntityRef);
  const FCrowdPresentationState* State = States.Find(EntityRef);
  if (!SlotPtr || !State)
  {
    Tombstones.Add(EntityRef, Sequence);
    return ECrowdPresentationApplyResult::MissingEntity;
  }
  const int32 Slot = *SlotPtr;
  const int32 LastSlot = SlotToEntity.Num() - 1;
  if (!Sink->RemoveInstanceSwap(Slot, LastSlot))
    return ECrowdPresentationApplyResult::Rejected;
  if (Slot != LastSlot)
  {
    const FCrowdStableEntityRef Moved = SlotToEntity[LastSlot];
    SlotToEntity[Slot] = Moved;
    EntityToSlot.FindChecked(Moved) = Slot;
  }
  SlotToEntity.Pop(EAllowShrinking::No);
  EntityToSlot.Remove(EntityRef);
  States.Remove(EntityRef);
  Tombstones.Add(EntityRef, Sequence);
  return ECrowdPresentationApplyResult::Applied;
}

ECrowdPresentationApplyResult
FCrowdPresentationSlotTable::ValidateSpawn(
  const FCrowdPresentationState& State) const
{
  if (!IsValidState(State))
    return ECrowdPresentationApplyResult::Rejected;
  if (const uint64* Tombstone = Tombstones.Find(State.EntityRef))
    if (State.Sequence <= *Tombstone)
      return ECrowdPresentationApplyResult::IgnoredStale;
  if (const FCrowdPresentationState* Existing = States.Find(State.EntityRef))
    return IsEquivalent(*Existing, State)
      ? ECrowdPresentationApplyResult::Duplicate
      : ECrowdPresentationApplyResult::Conflict;
  return ECrowdPresentationApplyResult::Applied;
}

ECrowdPresentationApplyResult
FCrowdPresentationSlotTable::ValidateUpdate(
  const FCrowdPresentationState& State) const
{
  if (!IsValidState(State))
    return ECrowdPresentationApplyResult::Rejected;
  const FCrowdPresentationState* Existing = States.Find(State.EntityRef);
  const int32* Slot = EntityToSlot.Find(State.EntityRef);
  if (!Existing || !Slot)
    return ECrowdPresentationApplyResult::MissingEntity;
  if (State.ProfileKey != Existing->ProfileKey)
    return ECrowdPresentationApplyResult::Conflict;
  if (State.Sequence < Existing->Sequence)
    return ECrowdPresentationApplyResult::IgnoredStale;
  if (State.Sequence == Existing->Sequence)
    return IsEquivalent(*Existing, State)
      ? ECrowdPresentationApplyResult::Duplicate
      : ECrowdPresentationApplyResult::Conflict;
  return ECrowdPresentationApplyResult::Applied;
}

ECrowdPresentationApplyResult
FCrowdPresentationSlotTable::ValidateDespawn(
  const FCrowdStableEntityRef& EntityRef,
  const uint64 Sequence) const
{
  if (!EntityRef.IsValid() || Sequence == 0)
    return ECrowdPresentationApplyResult::Rejected;
  const uint64 ExistingTombstone = Tombstones.FindRef(EntityRef);
  if (ExistingTombstone != 0 && Sequence <= ExistingTombstone)
    return Sequence == ExistingTombstone
      ? ECrowdPresentationApplyResult::Duplicate
      : ECrowdPresentationApplyResult::IgnoredStale;
  const FCrowdPresentationState* State = States.Find(EntityRef);
  if (!State || !EntityToSlot.Contains(EntityRef))
    return ECrowdPresentationApplyResult::MissingEntity;
  if (Sequence < State->Sequence)
    return ECrowdPresentationApplyResult::IgnoredStale;
  return ECrowdPresentationApplyResult::Applied;
}

bool FCrowdPresentationSlotTable::Reset()
{
  for (int32 Slot = SlotToEntity.Num() - 1; Slot >= 0; --Slot)
    if (!Sink->RemoveInstanceSwap(Slot, SlotToEntity.Num() - 1))
      return false;
    else
      SlotToEntity.Pop(EAllowShrinking::No);
  EntityToSlot.Reset();
  States.Reset();
  Tombstones.Reset();
  return true;
}

const FCrowdPresentationState* FCrowdPresentationSlotTable::Find(
  const FCrowdStableEntityRef& EntityRef) const
{
  return States.Find(EntityRef);
}

int32 FCrowdPresentationSlotTable::FindSlot(
  const FCrowdStableEntityRef& EntityRef) const
{
  const int32* Slot = EntityToSlot.Find(EntityRef);
  return Slot ? *Slot : INDEX_NONE;
}

bool FCrowdPresentationSlotTable::ValidateBijection() const
{
  if (SlotToEntity.Num() != EntityToSlot.Num()
    || SlotToEntity.Num() != States.Num())
    return false;
  for (int32 Slot = 0; Slot < SlotToEntity.Num(); ++Slot)
  {
    const int32* Reverse = EntityToSlot.Find(SlotToEntity[Slot]);
    if (!Reverse || *Reverse != Slot || !States.Contains(SlotToEntity[Slot]))
      return false;
  }
  return true;
}

bool UMassCrowdPresentationSubsystem::RegisterProfile(
  const uint32 ProfileKey,
  TSharedRef<ICrowdPresentationInstanceSink> Sink)
{
  if (ProfileKey == 0 || Profiles.Contains(ProfileKey)) return false;
  Profiles.Add(ProfileKey,
    MakeUnique<FCrowdPresentationSlotTable>(MoveTemp(Sink)));
  return true;
}

bool UMassCrowdPresentationSubsystem::UnregisterProfile(
  const uint32 ProfileKey)
{
  TUniquePtr<FCrowdPresentationSlotTable>* Entry = Profiles.Find(ProfileKey);
  FCrowdPresentationSlotTable* Table = Entry ? Entry->Get() : nullptr;
  if (!Table || Table->Num() != 0) return false;
  return Profiles.Remove(ProfileKey) == 1;
}

bool UMassCrowdPresentationSubsystem::ResetProfile(
  const uint32 ProfileKey)
{
  TUniquePtr<FCrowdPresentationSlotTable>* Entry = Profiles.Find(ProfileKey);
  return Entry && Entry->IsValid() && (*Entry)->Reset();
}

ECrowdPresentationApplyResult UMassCrowdPresentationSubsystem::ApplySpawn(
  const FCrowdPresentationState& State)
{
  TUniquePtr<FCrowdPresentationSlotTable>* Entry =
    Profiles.Find(State.ProfileKey);
  FCrowdPresentationSlotTable* Table = Entry ? Entry->Get() : nullptr;
  return Table ? Table->ApplySpawn(State)
    : ECrowdPresentationApplyResult::Rejected;
}

ECrowdPresentationApplyResult UMassCrowdPresentationSubsystem::ApplyUpdate(
  const FCrowdPresentationState& State)
{
  TUniquePtr<FCrowdPresentationSlotTable>* Entry =
    Profiles.Find(State.ProfileKey);
  FCrowdPresentationSlotTable* Table = Entry ? Entry->Get() : nullptr;
  return Table ? Table->ApplyUpdate(State)
    : ECrowdPresentationApplyResult::Rejected;
}

ECrowdPresentationApplyResult UMassCrowdPresentationSubsystem::ApplyDespawn(
  const FCrowdStableEntityRef& EntityRef,
  const uint32 ProfileKey,
  const uint64 Sequence)
{
  TUniquePtr<FCrowdPresentationSlotTable>* Entry = Profiles.Find(ProfileKey);
  FCrowdPresentationSlotTable* Table = Entry ? Entry->Get() : nullptr;
  return Table ? Table->ApplyDespawn(EntityRef, Sequence)
    : ECrowdPresentationApplyResult::Rejected;
}

bool UMassCrowdPresentationSubsystem::PrepareFrame(
  const uint64 SourceFrameHash,
  const TConstArrayView<FCrowdPresentationOperation> Operations,
  FCrowdPreparedPresentationFrame& OutFrame) const
{
  OutFrame = {};
  if (SourceFrameHash == 0 || Operations.IsEmpty())
    return false;
  OutFrame.SourceFrameHash = SourceFrameHash;
  OutFrame.Operations = TArray<FCrowdPresentationOperation>(Operations);
  OutFrame.Operations.Sort([](const auto& A, const auto& B)
  {
    if (A.EntityRef != B.EntityRef) return A.EntityRef < B.EntityRef;
    if (A.ProfileKey != B.ProfileKey) return A.ProfileKey < B.ProfileKey;
    return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
  });
  for (int32 Index = 0; Index < OutFrame.Operations.Num(); ++Index)
  {
    const FCrowdPresentationOperation& Operation =
      OutFrame.Operations[Index];
    if (!Operation.EntityRef.IsValid() || Operation.ProfileKey == 0
      || Operation.Sequence == 0
      || (Index > 0
        && OutFrame.Operations[Index - 1].EntityRef
          == Operation.EntityRef))
      return false;
    const TUniquePtr<FCrowdPresentationSlotTable>* Entry =
      Profiles.Find(Operation.ProfileKey);
    const FCrowdPresentationSlotTable* Table =
      Entry && Entry->IsValid() ? Entry->Get() : nullptr;
    if (!Table) return false;
    ECrowdPresentationApplyResult Result =
      ECrowdPresentationApplyResult::Rejected;
    if (Operation.Kind == ECrowdPresentationOperationKind::Despawn)
    {
      Result = Table->ValidateDespawn(
        Operation.EntityRef, Operation.Sequence);
    }
    else
    {
      if (Operation.State.EntityRef != Operation.EntityRef
        || Operation.State.ProfileKey != Operation.ProfileKey
        || Operation.State.Sequence != Operation.Sequence)
        return false;
      Result = Operation.Kind == ECrowdPresentationOperationKind::Spawn
        ? Table->ValidateSpawn(Operation.State)
        : Table->ValidateUpdate(Operation.State);
    }
    const bool bAccepted =
      Result == ECrowdPresentationApplyResult::Applied
      || Result == ECrowdPresentationApplyResult::Duplicate
      || Result == ECrowdPresentationApplyResult::IgnoredStale
      || (Operation.Kind == ECrowdPresentationOperationKind::Despawn
        && Result == ECrowdPresentationApplyResult::MissingEntity);
    if (!bAccepted) return false;
  }
  OutFrame.StableHash = CalculatePreparedFrameHash(OutFrame);
  OutFrame.bValid = OutFrame.StableHash != 0;
  return OutFrame.bValid;
}

bool UMassCrowdPresentationSubsystem::ApplyPreparedFrame(
  const FCrowdPreparedPresentationFrame& Frame)
{
  if (!Frame.bValid
    || Frame.Version != FCrowdPreparedPresentationFrame::CurrentVersion
    || Frame.StableHash != CalculatePreparedFrameHash(Frame))
    return false;
  FCrowdPreparedPresentationFrame Revalidated;
  if (!PrepareFrame(
      Frame.SourceFrameHash, Frame.Operations, Revalidated)
    || Revalidated.StableHash != Frame.StableHash)
    return false;
  for (const FCrowdPresentationOperation& Operation : Frame.Operations)
  {
    const ECrowdPresentationApplyResult Result =
      Operation.Kind == ECrowdPresentationOperationKind::Spawn
        ? ApplySpawn(Operation.State)
        : Operation.Kind == ECrowdPresentationOperationKind::Update
          ? ApplyUpdate(Operation.State)
          : ApplyDespawn(
              Operation.EntityRef, Operation.ProfileKey,
              Operation.Sequence);
    checkf(Result == ECrowdPresentationApplyResult::Applied
        || Result == ECrowdPresentationApplyResult::Duplicate
        || Result == ECrowdPresentationApplyResult::IgnoredStale
        || (Operation.Kind == ECrowdPresentationOperationKind::Despawn
          && Result == ECrowdPresentationApplyResult::MissingEntity),
      TEXT("Prepared presentation operation failed after validation"));
  }
  return true;
}

int32 UMassCrowdPresentationSubsystem::GetInstanceCount(
  const uint32 ProfileKey) const
{
  const TUniquePtr<FCrowdPresentationSlotTable>* Table =
    Profiles.Find(ProfileKey);
  return Table && Table->IsValid() ? (*Table)->Num() : 0;
}
