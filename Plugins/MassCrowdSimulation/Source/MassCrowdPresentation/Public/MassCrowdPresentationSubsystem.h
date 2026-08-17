#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassCrowdPresentationSubsystem.generated.h"

enum class ECrowdPresentationApplyResult : uint8
{
  Applied = 0,
  Duplicate,
  IgnoredStale,
  MissingEntity,
  Conflict,
  Rejected
};

struct FCrowdPresentationState
{
  FCrowdStableEntityRef EntityRef;
  FTransform Transform = FTransform::Identity;
  uint32 ProfileKey = 0;
  uint32 VisualState = 0;
  FVector3f CustomData = FVector3f::ZeroVector;
  FCrowdStableEntityRef CargoRef;
  uint64 Sequence = 0;
  double SampleServerSeconds = 0.0;
};

enum class ECrowdPresentationOperationKind : uint8
{
  Spawn = 0,
  Update,
  Despawn
};

struct FCrowdPresentationOperation
{
  ECrowdPresentationOperationKind Kind =
    ECrowdPresentationOperationKind::Spawn;
  FCrowdPresentationState State;
  FCrowdStableEntityRef EntityRef;
  uint32 ProfileKey = 0;
  uint64 Sequence = 0;
};

struct FCrowdPreparedPresentationFrame
{
  static constexpr uint16 CurrentVersion = 1;

  uint16 Version = CurrentVersion;
  uint64 SourceFrameHash = 0;
  TArray<FCrowdPresentationOperation> Operations;
  uint64 StableHash = 0;
  bool bValid = false;
};

class MASSCROWDPRESENTATION_API ICrowdPresentationInstanceSink
{
public:
  virtual ~ICrowdPresentationInstanceSink() = default;
  virtual int32 AddInstance(const FCrowdPresentationState& State) = 0;
  virtual bool UpdateInstance(
    int32 Slot, const FCrowdPresentationState& State) = 0;
  virtual bool RemoveInstanceSwap(int32 Slot, int32 LastSlot) = 0;
};

class MASSCROWDPRESENTATION_API FCrowdPresentationSlotTable
{
public:
  explicit FCrowdPresentationSlotTable(
    TSharedRef<ICrowdPresentationInstanceSink> InSink);

  ECrowdPresentationApplyResult ApplySpawn(
    const FCrowdPresentationState& State);
  ECrowdPresentationApplyResult ApplyUpdate(
    const FCrowdPresentationState& State);
  ECrowdPresentationApplyResult ApplyDespawn(
    const FCrowdStableEntityRef& EntityRef, uint64 Sequence);
  ECrowdPresentationApplyResult ValidateSpawn(
    const FCrowdPresentationState& State) const;
  ECrowdPresentationApplyResult ValidateUpdate(
    const FCrowdPresentationState& State) const;
  ECrowdPresentationApplyResult ValidateDespawn(
    const FCrowdStableEntityRef& EntityRef, uint64 Sequence) const;
  bool Reset();

  int32 Num() const { return SlotToEntity.Num(); }
  const FCrowdPresentationState* Find(
    const FCrowdStableEntityRef& EntityRef) const;
  int32 FindSlot(const FCrowdStableEntityRef& EntityRef) const;
  bool ValidateBijection() const;

private:
  TSharedRef<ICrowdPresentationInstanceSink> Sink;
  TArray<FCrowdStableEntityRef> SlotToEntity;
  TMap<FCrowdStableEntityRef, int32> EntityToSlot;
  TMap<FCrowdStableEntityRef, FCrowdPresentationState> States;
  TMap<FCrowdStableEntityRef, uint64> Tombstones;
};

UCLASS()
class MASSCROWDPRESENTATION_API UMassCrowdPresentationSubsystem final
  : public UWorldSubsystem
{
  GENERATED_BODY()

public:
  bool RegisterProfile(
    uint32 ProfileKey,
    TSharedRef<ICrowdPresentationInstanceSink> Sink);
  bool UnregisterProfile(uint32 ProfileKey);
  bool ResetProfile(uint32 ProfileKey);

  ECrowdPresentationApplyResult ApplySpawn(
    const FCrowdPresentationState& State);
  ECrowdPresentationApplyResult ApplyUpdate(
    const FCrowdPresentationState& State);
  ECrowdPresentationApplyResult ApplyDespawn(
    const FCrowdStableEntityRef& EntityRef,
    uint32 ProfileKey,
    uint64 Sequence);
  bool PrepareFrame(
    uint64 SourceFrameHash,
    TConstArrayView<FCrowdPresentationOperation> Operations,
    FCrowdPreparedPresentationFrame& OutFrame) const;
  bool ApplyPreparedFrame(
    const FCrowdPreparedPresentationFrame& Frame);
  int32 GetInstanceCount(uint32 ProfileKey) const;

private:
  TMap<uint32, TUniquePtr<FCrowdPresentationSlotTable>> Profiles;
};
