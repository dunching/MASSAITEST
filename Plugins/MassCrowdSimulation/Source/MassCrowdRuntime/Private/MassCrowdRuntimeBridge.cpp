#include "MassCrowdRuntimeBridge.h"

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

  bool IsValidProfile(const FCrowdSimulationProfile& Profile)
  {
    return FMath::IsFinite(Profile.FixedStepSeconds)
      && Profile.FixedStepSeconds > 0.0f;
  }

  const FCrowdSimulationProfile* FindProfile(
    TConstArrayView<FCrowdSimulationProfile> Profiles,
    const uint32 Key)
  {
    for (const FCrowdSimulationProfile& Profile : Profiles)
      if (Profile.StableProfileKey == Key) return &Profile;
    return nullptr;
  }

  bool IsValidBoundaryRecord(const FCrowdMassBoundaryAgentRecord& Record)
  {
    return Record.Identity.AgentId != INDEX_NONE
      && Record.Identity.LifecycleSerial > 0
      && Record.State.bInitialized
      && IsFiniteVector(Record.State.Position)
      && IsFiniteVector(Record.State.Velocity)
      && FMath::IsFinite(Record.State.YawDegrees)
      && FMath::IsFinite(Record.Properties.PhysicalRadiusCm)
      && FMath::IsFinite(Record.Properties.HardSafetyGapCm)
      && FMath::IsFinite(Record.Properties.SoftMarginCm)
      && FMath::IsFinite(Record.Properties.Mobility)
      && FMath::IsFinite(Record.Properties.MaximumSpeedCmps)
      && Record.Properties.PhysicalRadiusCm > 0.0f
      && Record.Properties.HardSafetyGapCm >= 0.0f
      && Record.Properties.SoftMarginCm >= 0.0f
      && Record.Properties.Mobility >= 0.0f
      && Record.Properties.MaximumSpeedCmps >= 0.0f;
  }
}

void FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
  const int32 FixedStepIndex,
  const int32 PlanRevision,
  const TConstArrayView<FCrowdMassBoundaryAgentRecord> RecordView,
  FCrowdMassBoundarySnapshot& OutSnapshot)
{
  OutSnapshot = {};
  if (FixedStepIndex < 0 || PlanRevision < 0 || RecordView.IsEmpty()) return;
  OutSnapshot.FixedStepIndex = FixedStepIndex;
  OutSnapshot.PlanRevision = PlanRevision;
  OutSnapshot.Agents = TArray<FCrowdMassBoundaryAgentRecord>(RecordView);
  OutSnapshot.Agents.Sort([](const auto& A, const auto& B)
  {
    return A.Identity.AgentId < B.Identity.AgentId;
  });
  uint32 Hash = Fold(FnvOffset, 1u);
  Hash = FoldInt(Hash, FixedStepIndex);
  Hash = FoldInt(Hash, PlanRevision);
  int32 PreviousAgentId = INDEX_NONE;
  for (const FCrowdMassBoundaryAgentRecord& Record : OutSnapshot.Agents)
  {
    if (!IsValidBoundaryRecord(Record)
      || Record.Identity.AgentId <= PreviousAgentId)
    {
      OutSnapshot = {};
      return;
    }
    PreviousAgentId = Record.Identity.AgentId;
    Hash = FoldInt(Hash, Record.Identity.AgentId);
    Hash = FoldInt(Hash, Record.Identity.LifecycleSerial);
    Hash = FoldFloat(Hash, static_cast<float>(Record.State.Position.X));
    Hash = FoldFloat(Hash, static_cast<float>(Record.State.Position.Y));
    Hash = FoldFloat(Hash, static_cast<float>(Record.State.Position.Z));
    Hash = FoldFloat(Hash, static_cast<float>(Record.State.Velocity.X));
    Hash = FoldFloat(Hash, static_cast<float>(Record.State.Velocity.Y));
    Hash = FoldFloat(Hash, static_cast<float>(Record.State.Velocity.Z));
    Hash = FoldFloat(Hash, Record.State.YawDegrees);
    Hash = FoldInt(Hash, Record.State.PlanRevision);
    Hash = FoldFloat(Hash, Record.Properties.PhysicalRadiusCm);
    Hash = FoldFloat(Hash, Record.Properties.HardSafetyGapCm);
    Hash = FoldFloat(Hash, Record.Properties.SoftMarginCm);
    Hash = FoldFloat(Hash, Record.Properties.Mobility);
    Hash = FoldFloat(Hash, Record.Properties.MaximumSpeedCmps);
    Hash = Fold(Hash, Record.Properties.CapabilityProfileKey);
  }
  OutSnapshot.StableHash = Hash;
  OutSnapshot.bValid = true;
}

bool FCrowdMassRuntimeBridge::BuildGuidanceRecords(
  const FCrowdMassBoundarySnapshot& Snapshot,
  const TConstArrayView<FCrowdGuidanceCandidate> SharedFlowView,
  const TConstArrayView<FCrowdGuidanceCandidate> TargetRegionView,
  const TConstArrayView<FCrowdGuidanceCandidate> BusinessView,
  TArray<FCrowdMassGatherRecord>& OutRecords)
{
  OutRecords.Reset();
  if (!Snapshot.bValid || Snapshot.Agents.IsEmpty()
    || SharedFlowView.Num() != Snapshot.Agents.Num())
    return false;
  const auto PrepareCandidates = [&Snapshot](
    const TConstArrayView<FCrowdGuidanceCandidate> Source,
    const ECrowdGuidanceProvider ExpectedProvider,
    const bool bRequireComplete,
    TArray<FCrowdGuidanceCandidate>& Out) -> bool
  {
    Out = TArray<FCrowdGuidanceCandidate>(Source);
    Out.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
    if (bRequireComplete && Out.Num() != Snapshot.Agents.Num()) return false;
    int32 PreviousAgentId = INDEX_NONE;
    for (const FCrowdGuidanceCandidate& Candidate : Out)
    {
      if (Candidate.AgentId == INDEX_NONE
        || Candidate.AgentId <= PreviousAgentId
        || Candidate.Provider != ExpectedProvider
        || Candidate.PlanRevision != Snapshot.PlanRevision
        || !Snapshot.Agents.ContainsByPredicate(
          [&Candidate](const FCrowdMassBoundaryAgentRecord& Agent)
          {
            return Agent.Identity.AgentId == Candidate.AgentId;
          }))
        return false;
      PreviousAgentId = Candidate.AgentId;
    }
    return true;
  };
  TArray<FCrowdGuidanceCandidate> SharedFlow;
  TArray<FCrowdGuidanceCandidate> TargetRegion;
  TArray<FCrowdGuidanceCandidate> Business;
  if (!PrepareCandidates(SharedFlowView, ECrowdGuidanceProvider::SharedFlow,
      true, SharedFlow)
    || !PrepareCandidates(TargetRegionView,
      ECrowdGuidanceProvider::TargetRegion, false, TargetRegion)
    || !PrepareCandidates(BusinessView,
      ECrowdGuidanceProvider::BusinessOverride, false, Business))
    return false;
  TMap<int32, const FCrowdGuidanceCandidate*> TargetByAgentId;
  for (const FCrowdGuidanceCandidate& Candidate : TargetRegion)
    TargetByAgentId.Add(Candidate.AgentId, &Candidate);
  TMap<int32, const FCrowdGuidanceCandidate*> BusinessByAgentId;
  for (const FCrowdGuidanceCandidate& Candidate : Business)
    BusinessByAgentId.Add(Candidate.AgentId, &Candidate);
  OutRecords.Reserve(Snapshot.Agents.Num());
  for (int32 Index = 0; Index < Snapshot.Agents.Num(); ++Index)
  {
    const FCrowdMassBoundaryAgentRecord& Base = Snapshot.Agents[Index];
    const FCrowdGuidanceCandidate& Flow = SharedFlow[Index];
    if (Flow.AgentId != Base.Identity.AgentId)
    {
      OutRecords.Reset();
      return false;
    }
    FCrowdMassGatherRecord& Record = OutRecords.AddDefaulted_GetRef();
    Record.Identity = Base.Identity;
    Record.State = Base.State;
    Record.Properties = Base.Properties;
    Record.Guidance.SharedFlow = Flow;
    if (const FCrowdGuidanceCandidate* const* Target =
      TargetByAgentId.Find(Base.Identity.AgentId))
      Record.Guidance.TargetRegion = **Target;
    if (const FCrowdGuidanceCandidate* const* Override =
      BusinessByAgentId.Find(Base.Identity.AgentId))
      Record.Guidance.BusinessOverride = **Override;
  }
  return true;
}

void FCrowdMassRuntimeBridge::BuildWorkBatches(
  const int32 FixedStepIndex,
  const int32 PlanRevision,
  const TConstArrayView<FCrowdSimulationProfile> ProfileView,
  const FCrowdEnvironmentSnapshot& Environment,
  const FCrowdTargetInput& Target,
  const TConstArrayView<FCrowdMassGatherRecord> RecordView,
  TArray<FCrowdMassWorkBatch>& OutBatches)
{
  OutBatches.Reset();
  if (FixedStepIndex < 0 || PlanRevision < 0 || !Environment.bValid) return;

  TArray<FCrowdSimulationProfile> Profiles(ProfileView);
  Profiles.Sort([](const auto& A, const auto& B)
  {
    return A.StableProfileKey < B.StableProfileKey;
  });
  uint32 PreviousProfileKey = 0;
  bool bHavePreviousProfile = false;
  for (const FCrowdSimulationProfile& Profile : Profiles)
  {
    if (!IsValidProfile(Profile)
      || (bHavePreviousProfile
        && Profile.StableProfileKey == PreviousProfileKey))
    {
      OutBatches.Reset();
      return;
    }
    PreviousProfileKey = Profile.StableProfileKey;
    bHavePreviousProfile = true;
  }

  TArray<FCrowdMassGatherRecord> Records(RecordView);
  Records.Sort([](const auto& A, const auto& B)
  {
    return A.Identity.AgentId < B.Identity.AgentId;
  });
  int32 PreviousAgentId = INDEX_NONE;
  for (const FCrowdMassGatherRecord& Record : Records)
  {
    if (Record.Identity.AgentId == INDEX_NONE
      || Record.Identity.AgentId <= PreviousAgentId
      || Record.Identity.LifecycleSerial <= 0
      || !Record.State.bInitialized
      || !IsFiniteVector(Record.State.Position)
      || !IsFiniteVector(Record.State.Velocity)
      || !FMath::IsFinite(Record.State.YawDegrees)
      || !FMath::IsFinite(Record.Properties.PhysicalRadiusCm)
      || !FMath::IsFinite(Record.Properties.HardSafetyGapCm)
      || !FMath::IsFinite(Record.Properties.SoftMarginCm)
      || !FMath::IsFinite(Record.Properties.Mobility)
      || !FMath::IsFinite(Record.Properties.MaximumSpeedCmps)
      || Record.Properties.PhysicalRadiusCm <= 0.0f
      || Record.Properties.HardSafetyGapCm < 0.0f
      || Record.Properties.SoftMarginCm < 0.0f
      || Record.Properties.Mobility < 0.0f
      || Record.Properties.MaximumSpeedCmps < 0.0f)
    {
      OutBatches.Reset();
      return;
    }
    PreviousAgentId = Record.Identity.AgentId;
    const FCrowdSimulationProfile* Profile = FindProfile(
      Profiles, Record.Properties.CapabilityProfileKey);
    if (!Profile)
    {
      OutBatches.Reset();
      return;
    }
    FCrowdMassWorkBatch* Batch = OutBatches.FindByPredicate(
      [Profile](const FCrowdMassWorkBatch& Candidate)
      {
        return Candidate.CapabilityProfileKey == Profile->StableProfileKey;
      });
    if (!Batch)
    {
      FCrowdMassWorkBatch& NewBatch = OutBatches.AddDefaulted_GetRef();
      NewBatch.CapabilityProfileKey = Profile->StableProfileKey;
      NewBatch.WorkInput.FixedStepIndex = FixedStepIndex;
      NewBatch.WorkInput.PlanRevision = PlanRevision;
      NewBatch.WorkInput.SimulationProfile = *Profile;
      NewBatch.WorkInput.Environment = Environment;
      NewBatch.WorkInput.Target = Target;
      Batch = &NewBatch;
    }
    FCrowdAgentInput& Agent = Batch->WorkInput.Agents.AddDefaulted_GetRef();
    Agent.AgentId = Record.Identity.AgentId;
    Agent.LifecycleSerial = static_cast<uint32>(Record.Identity.LifecycleSerial);
    Agent.Position = Record.State.Position;
    Agent.Velocity = Record.State.Velocity;
    Agent.PhysicalRadiusCm = Record.Properties.PhysicalRadiusCm;
    Agent.HardSafetyGapCm = Record.Properties.HardSafetyGapCm;
    Agent.SoftMarginCm = Record.Properties.SoftMarginCm;
    Agent.Mobility = Record.Properties.Mobility;
    Agent.MaximumSpeedCmps = Record.Properties.MaximumSpeedCmps;
    Agent.CapabilityProfileKey = Record.Properties.CapabilityProfileKey;
    const FCrowdGuidanceCandidate Candidates[] = {
      Record.Guidance.SharedFlow,
      Record.Guidance.TargetRegion,
      Record.Guidance.BusinessOverride};
    for (const FCrowdGuidanceCandidate& Candidate : Candidates)
    {
      if (Candidate.AgentId != INDEX_NONE
        && Candidate.AgentId != Record.Identity.AgentId)
      {
        OutBatches.Reset();
        return;
      }
      Batch->WorkInput.GuidanceCandidates.Add(Candidate);
    }
  }

  OutBatches.Sort([](const auto& A, const auto& B)
  {
    return A.CapabilityProfileKey < B.CapabilityProfileKey;
  });
  for (FCrowdMassWorkBatch& Batch : OutBatches)
  {
    uint32 Hash = Fold(FnvOffset, Batch.CapabilityProfileKey);
    Hash = FoldInt(Hash, FixedStepIndex);
    Hash = FoldInt(Hash, PlanRevision);
    Hash = Fold(Hash, Environment.StableHash);
    Hash = FoldInt(Hash, Environment.Revision);
    Hash = FoldInt(Hash, Target.TargetId);
    Hash = FoldInt(Hash, Target.Revision);
    Hash = FoldFloat(Hash, static_cast<float>(Target.Position.X));
    Hash = FoldFloat(Hash, static_cast<float>(Target.Position.Y));
    Hash = FoldFloat(Hash, static_cast<float>(Target.Position.Z));
    Hash = FoldFloat(Hash, static_cast<float>(Target.Velocity.X));
    Hash = FoldFloat(Hash, static_cast<float>(Target.Velocity.Y));
    Hash = FoldFloat(Hash, static_cast<float>(Target.Velocity.Z));
    Hash = FoldFloat(Hash, Batch.WorkInput.SimulationProfile.FixedStepSeconds,
      1000000.0f);
    Hash = FoldFloat(Hash,
      Batch.WorkInput.SimulationProfile.HardSafetyGapCm);
    Hash = FoldFloat(Hash,
      Batch.WorkInput.SimulationProfile.SoftMarginCm);
    for (const FCrowdAgentInput& Agent : Batch.WorkInput.Agents)
    {
      Hash = FoldInt(Hash, Agent.AgentId);
      Hash = Fold(Hash, Agent.LifecycleSerial);
      Hash = FoldFloat(Hash, static_cast<float>(Agent.Position.X));
      Hash = FoldFloat(Hash, static_cast<float>(Agent.Position.Y));
      Hash = FoldFloat(Hash, static_cast<float>(Agent.Position.Z));
      Hash = FoldFloat(Hash, Agent.PhysicalRadiusCm);
      Hash = FoldFloat(Hash, Agent.HardSafetyGapCm);
      Hash = FoldFloat(Hash, Agent.SoftMarginCm);
      Hash = FoldFloat(Hash, Agent.Mobility);
      Hash = FoldFloat(Hash, Agent.MaximumSpeedCmps);
    }
    for (const FCrowdGuidanceCandidate& Candidate
      : Batch.WorkInput.GuidanceCandidates)
      Hash = Fold(Hash, Candidate.StableHash);
    Batch.GatherHash = Hash;
    Batch.bValid = Batch.WorkInput.Agents.Num() > 0;
  }
}

void FCrowdMassRuntimeBridge::MergeWorkOutputs(
  const TConstArrayView<FCrowdMassWorkBatchOutput> InputView,
  FCrowdMassCommitPlan& OutPlan)
{
  OutPlan = {};
  TArray<FCrowdMassWorkBatchOutput> Inputs(InputView);
  Inputs.Sort([](const auto& A, const auto& B)
  {
    return A.CapabilityProfileKey < B.CapabilityProfileKey;
  });
  if (Inputs.IsEmpty()) return;
  OutPlan.FixedStepIndex = Inputs[0].WorkOutput.FixedStepIndex;
  OutPlan.PlanRevision = Inputs[0].WorkOutput.PlanRevision;
  uint32 Hash = Fold(FnvOffset, 1);
  uint32 PreviousProfileKey = 0;
  bool bHavePreviousProfile = false;
  for (FCrowdMassWorkBatchOutput& Input : Inputs)
  {
    if ((bHavePreviousProfile
        && Input.CapabilityProfileKey == PreviousProfileKey)
      || !Input.WorkOutput.bValid
      || Input.WorkOutput.FixedStepIndex != OutPlan.FixedStepIndex
      || Input.WorkOutput.PlanRevision != OutPlan.PlanRevision)
      return;
    PreviousProfileKey = Input.CapabilityProfileKey;
    bHavePreviousProfile = true;
    Input.WorkOutput.Movements.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
    Hash = Fold(Hash, Input.CapabilityProfileKey);
    Hash = Fold(Hash, Input.WorkOutput.StableHash);
    for (const FCrowdMovementOutput& Movement : Input.WorkOutput.Movements)
    {
      if (!Movement.bValid || Movement.AgentId == INDEX_NONE
        || !IsFiniteVector(Movement.Position)
        || !IsFiniteVector(Movement.Velocity)
        || !FMath::IsFinite(Movement.YawDegrees))
        return;
      FCrowdMassCommitRecord& Record = OutPlan.Records.AddDefaulted_GetRef();
      Record.CapabilityProfileKey = Input.CapabilityProfileKey;
      Record.PlanRevision = OutPlan.PlanRevision;
      Record.Movement = Movement;
    }
  }
  OutPlan.Records.Sort([](const auto& A, const auto& B)
  {
    return A.Movement.AgentId < B.Movement.AgentId;
  });
  int32 PreviousAgentId = INDEX_NONE;
  for (const FCrowdMassCommitRecord& Record : OutPlan.Records)
  {
    if (Record.Movement.AgentId <= PreviousAgentId) return;
    PreviousAgentId = Record.Movement.AgentId;
    Hash = FoldInt(Hash, Record.Movement.AgentId);
    Hash = Fold(Hash, Record.Movement.LifecycleSerial);
    Hash = FoldFloat(Hash, static_cast<float>(Record.Movement.Position.X));
    Hash = FoldFloat(Hash, static_cast<float>(Record.Movement.Position.Y));
    Hash = FoldFloat(Hash, static_cast<float>(Record.Movement.Position.Z));
    Hash = FoldFloat(Hash, static_cast<float>(Record.Movement.Velocity.X));
    Hash = FoldFloat(Hash, static_cast<float>(Record.Movement.Velocity.Y));
    Hash = FoldFloat(Hash, static_cast<float>(Record.Movement.Velocity.Z));
    Hash = FoldFloat(Hash, Record.Movement.YawDegrees, 100.0f);
    Hash = Fold(Hash, Record.Movement.StableHash);
  }
  OutPlan.StableHash = Hash;
  OutPlan.bValid = !OutPlan.Records.IsEmpty();
}

bool FCrowdMassRuntimeBridge::ValidateCommitTargets(
  const FCrowdMassCommitPlan& Plan,
  const TConstArrayView<FCrowdMassCommitTarget> TargetView)
{
  if (!Plan.bValid || Plan.Records.Num() != TargetView.Num()) return false;
  TArray<FCrowdMassCommitTarget> Targets(TargetView);
  Targets.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 0; Index < Plan.Records.Num(); ++Index)
    if (Plan.Records[Index].Movement.AgentId != Targets[Index].AgentId
      || Plan.Records[Index].Movement.LifecycleSerial
        != Targets[Index].LifecycleSerial)
      return false;
  return true;
}

bool FCrowdMassRuntimeBridge::ApplyMovementToState(
  const FCrowdMassCommitRecord& Record,
  const FCrowdMassCommitTarget& Target,
  FCrowdMassSimulationStateFragment& InOutState,
  FCrowdMassMovementOutputFragment& OutMovement)
{
  if (!Record.Movement.bValid
    || Record.Movement.AgentId != Target.AgentId
    || Record.Movement.LifecycleSerial != Target.LifecycleSerial)
    return false;
  InOutState.Position = Record.Movement.Position;
  InOutState.Velocity = Record.Movement.Velocity;
  InOutState.YawDegrees = Record.Movement.YawDegrees;
  InOutState.PlanRevision = Record.PlanRevision;
  InOutState.bInitialized = true;
  OutMovement.Value = Record.Movement;
  return true;
}
