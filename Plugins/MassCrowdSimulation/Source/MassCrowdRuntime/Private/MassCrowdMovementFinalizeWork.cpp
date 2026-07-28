#include "MassCrowdMovementFinalizeWork.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;

  uint32 Fold(uint32 Hash, const uint32 Value)
  {
    for (int32 Byte = 0; Byte < 4; ++Byte)
    {
      Hash ^= static_cast<uint8>((Value >> (Byte * 8)) & 0xffu);
      Hash *= FnvPrime;
    }
    return Hash;
  }

  uint32 FoldInt(uint32 Hash, const int32 Value)
  {
    return Fold(Hash, static_cast<uint32>(Value));
  }

  uint32 FoldFloat(uint32 Hash, const float Value, const float Scale = 100.0f)
  {
    return FoldInt(Hash, FMath::RoundToInt(Value * Scale));
  }

  bool IsFiniteVector(const FVector& Value)
  {
    return FMath::IsFinite(Value.X)
      && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }

  uint32 BuildMovementHash(const FCrowdMassMovementFinalizeRecord& Record)
  {
    uint32 Hash = Fold(FnvOffset, 1u);
    Hash = Fold(Hash, Record.EntityRef.ProviderId);
    Hash = Fold(Hash, static_cast<uint32>(Record.EntityRef.StableEntityId));
    Hash = Fold(Hash, static_cast<uint32>(
      Record.EntityRef.StableEntityId >> 32));
    Hash = Fold(Hash, Record.EntityRef.LifecycleSerial);
    Hash = FoldInt(Hash, Record.AgentId);
    Hash = Fold(Hash, Record.LifecycleSerial);
    Hash = Fold(Hash, Record.CapabilityProfileKey);
    Hash = FoldFloat(Hash, static_cast<float>(Record.Position.X));
    Hash = FoldFloat(Hash, static_cast<float>(Record.Position.Y));
    Hash = FoldFloat(Hash, static_cast<float>(Record.Position.Z));
    Hash = FoldFloat(Hash, static_cast<float>(Record.Velocity.X));
    Hash = FoldFloat(Hash, static_cast<float>(Record.Velocity.Y));
    Hash = FoldFloat(Hash, static_cast<float>(Record.Velocity.Z));
    Hash = FoldFloat(Hash, Record.YawDegrees);
    return Hash;
  }
}

bool FCrowdMassMovementFinalizeWork::BuildInputFromPrepared(
  const FCrowdMassBoundarySnapshot& Snapshot,
  const TConstArrayView<FCrowdMassFinalKinematicState> Kinematics,
  const TConstArrayView<FCrowdFacingResult> Facings,
  FCrowdMassMovementFinalizeWorkInput& OutInput,
  TArray<FCrowdMassCommitTarget>& OutTargets)
{
  OutInput = {};
  OutTargets.Reset();
  if (!Snapshot.bValid || Snapshot.FixedStepIndex < 0
    || Snapshot.PlanRevision < 0 || Snapshot.Agents.IsEmpty()
    || Kinematics.Num() != Snapshot.Agents.Num()
    || Facings.Num() != Snapshot.Agents.Num())
    return false;
  TArray<FCrowdMassFinalKinematicState> SortedKinematics(Kinematics);
  SortedKinematics.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  TArray<FCrowdFacingResult> SortedFacings(Facings);
  SortedFacings.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  OutInput.FixedStepIndex = Snapshot.FixedStepIndex;
  OutInput.PlanRevision = Snapshot.PlanRevision;
  for (int32 Index = 0; Index < Snapshot.Agents.Num(); ++Index)
  {
    const FCrowdMassBoundaryAgentRecord& Agent = Snapshot.Agents[Index];
    const FCrowdMassFinalKinematicState& Kinematic = SortedKinematics[Index];
    const FCrowdFacingResult& Facing = SortedFacings[Index];
    if (Agent.Identity.AgentId == INDEX_NONE
      || !Agent.AgentFacts.IsWellFormed()
      || Agent.Identity.GetStableEntityRef() != Agent.AgentFacts.StableEntityRef
      || Kinematic.AgentId != Agent.Identity.AgentId || !Kinematic.bValid
      || Facing.AgentId != Agent.Identity.AgentId
      || !IsFiniteVector(Kinematic.Position)
      || !IsFiniteVector(Kinematic.Velocity)
      || !FMath::IsFinite(Facing.ResolvedYawDegrees))
    {
      OutInput = {};
      OutTargets.Reset();
      return false;
    }
    FCrowdMassMovementFinalizeRecord& Record =
      OutInput.Records.AddDefaulted_GetRef();
    Record.EntityRef = Agent.AgentFacts.StableEntityRef;
    Record.AgentId = Agent.Identity.AgentId;
    Record.LifecycleSerial = Record.EntityRef.LifecycleSerial;
    Record.CapabilityProfileKey = Agent.Properties.CapabilityProfileKey;
    Record.Position = Kinematic.Position;
    Record.Velocity = Kinematic.Velocity;
    Record.YawDegrees = Facing.ResolvedYawDegrees;
    FCrowdMassCommitTarget& Target = OutTargets.AddDefaulted_GetRef();
    Target.EntityRef = Record.EntityRef;
    Target.AgentId = Record.AgentId;
    Target.LifecycleSerial = Record.LifecycleSerial;
  }
  return true;
}

FCrowdMassMovementFinalizeWorkOutput
FCrowdMassMovementFinalizeWork::BuildCommitPlan(
  const FCrowdMassMovementFinalizeWorkInput& Input)
{
  FCrowdMassMovementFinalizeWorkOutput Output;
  if (Input.FixedStepIndex < 0 || Input.PlanRevision < 0
    || Input.Records.IsEmpty())
    return Output;

  TArray<FCrowdMassMovementFinalizeRecord> Records = Input.Records;
  Records.Sort([](const auto& A, const auto& B)
  {
    if (A.CapabilityProfileKey != B.CapabilityProfileKey)
      return A.CapabilityProfileKey < B.CapabilityProfileKey;
    return A.EntityRef < B.EntityRef;
  });

  TArray<FCrowdMassWorkBatchOutput> Batches;
  TArray<FCrowdStableEntityRef> SeenEntityRefs;
  SeenEntityRefs.Reserve(Records.Num());
  for (const FCrowdMassMovementFinalizeRecord& Record : Records)
    SeenEntityRefs.Add(Record.EntityRef);
  SeenEntityRefs.Sort();
  for (int32 Index = 1; Index < SeenEntityRefs.Num(); ++Index)
    if (SeenEntityRefs[Index] == SeenEntityRefs[Index - 1])
      return Output;
  for (const FCrowdMassMovementFinalizeRecord& Record : Records)
  {
    if (!Record.EntityRef.IsValid()
      || Record.AgentId == INDEX_NONE
      || Record.LifecycleSerial != Record.EntityRef.LifecycleSerial
      || !IsFiniteVector(Record.Position)
      || !IsFiniteVector(Record.Velocity)
      || !FMath::IsFinite(Record.YawDegrees))
      return Output;

    FCrowdMassWorkBatchOutput* Batch = Batches.FindByPredicate(
      [&Record](const FCrowdMassWorkBatchOutput& Candidate)
      {
        return Candidate.CapabilityProfileKey == Record.CapabilityProfileKey;
      });
    if (!Batch)
    {
      FCrowdMassWorkBatchOutput& NewBatch = Batches.AddDefaulted_GetRef();
      NewBatch.CapabilityProfileKey = Record.CapabilityProfileKey;
      NewBatch.WorkOutput.FixedStepIndex = Input.FixedStepIndex;
      NewBatch.WorkOutput.PlanRevision = Input.PlanRevision;
      Batch = &NewBatch;
    }
    FCrowdMovementOutput& Movement =
      Batch->WorkOutput.Movements.AddDefaulted_GetRef();
    Movement.AgentId = Record.AgentId;
    Movement.LifecycleSerial = Record.LifecycleSerial;
    Movement.Position = Record.Position;
    Movement.Velocity = Record.Velocity;
    Movement.YawDegrees = Record.YawDegrees;
    Movement.StableHash = BuildMovementHash(Record);
    Movement.bValid = true;
    Batch->EntityRefs.Add(Record.EntityRef);
  }

  for (FCrowdMassWorkBatchOutput& Batch : Batches)
  {
    uint32 Hash = Fold(FnvOffset, 1u);
    Hash = Fold(Hash, Batch.CapabilityProfileKey);
    Hash = FoldInt(Hash, Input.FixedStepIndex);
    Hash = FoldInt(Hash, Input.PlanRevision);
    for (const FCrowdMovementOutput& Movement : Batch.WorkOutput.Movements)
      Hash = Fold(Hash, Movement.StableHash);
    Batch.WorkOutput.StableHash = Hash;
    Batch.WorkOutput.bValid = !Batch.WorkOutput.Movements.IsEmpty();
  }

  FCrowdMassRuntimeBridge::MergeWorkOutputs(Batches, Output.CommitPlan);
  if (!Output.CommitPlan.bValid
    || Output.CommitPlan.Records.Num() != Records.Num())
    return Output;
  uint32 Hash = Fold(FnvOffset, 1u);
  Hash = FoldInt(Hash, Input.FixedStepIndex);
  Hash = FoldInt(Hash, Input.PlanRevision);
  Hash = Fold(Hash, static_cast<uint32>(Output.CommitPlan.StableHash));
  Hash = Fold(Hash, static_cast<uint32>(Output.CommitPlan.StableHash >> 32));
  Output.StableHash = Hash;
  Output.bCompleted = true;
  return Output;
}
