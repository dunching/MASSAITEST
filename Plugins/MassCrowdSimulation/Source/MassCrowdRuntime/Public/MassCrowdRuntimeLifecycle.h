#pragma once

#include "MassCrowdRuntimeFragments.h"
#include "MassEntityManager.h"

struct FCrowdMassRuntimeLifecycleEntity
{
  FCrowdAgentFacts AgentFacts;
  uint32 MembershipKey = 0;
};

struct FCrowdMassRuntimeMembershipChange
{
  FCrowdStableEntityRef EntityRef;
  uint32 PreviousMembershipKey = 0;
  uint32 NewMembershipKey = 0;
};

class MASSCROWDRUNTIME_API FCrowdMassRuntimeLifecycleStore
{
public:
  bool Initialize(
    FMassEntityManager& InEntityManager,
    const FMassArchetypeHandle& InArchetype,
    TConstArrayView<FCrowdMassRuntimeLifecycleEntity> Entities);
  bool Spawn(TConstArrayView<FCrowdMassRuntimeLifecycleEntity> Entities);
  bool Despawn(TConstArrayView<FCrowdStableEntityRef> EntityRefs);
  bool UpdateMembership(
    TConstArrayView<FCrowdMassRuntimeMembershipChange> Changes);
  bool ApplyAgentFactsCorrection(const FCrowdAgentFacts& CorrectedFacts);
  bool ApplyAgentFactsCorrectionsAtomic(
    TConstArrayView<FCrowdAgentFacts> CorrectedFacts);
  void Reset();

  int32 GetActiveEntityCount() const;
  uint64 CalculateEntitySetHash() const;
  bool TryGetEntityHandle(
    const FCrowdStableEntityRef& EntityRef,
    FMassEntityHandle& OutEntity) const;

private:
  struct FEntityRecord
  {
    FCrowdStableEntityRef EntityRef;
    FMassEntityHandle Entity;
    bool bActive = false;
  };

  bool CreateEntity(
    const FCrowdAgentFacts& AgentFacts,
    uint32 MembershipKey,
    FMassEntityHandle& OutEntity);
  int32 FindSlot(const FCrowdStableEntityRef& EntityRef) const;

  FMassEntityManager* EntityManager = nullptr;
  FMassArchetypeHandle Archetype;
  TArray<FEntityRecord> EntityRecords;
  bool bInitialized = false;
};
