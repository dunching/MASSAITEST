#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoAttackPlanner.h"
#include "CrowdDemoBusinessSourceProvider.h"

struct FCrowdDemoBusinessScenarioId
{
  uint32 Value = 0;
  constexpr bool IsValid() const { return Value != 0; }
  constexpr auto operator<=>(const FCrowdDemoBusinessScenarioId&) const =
    default;
  friend uint32 GetTypeHash(const FCrowdDemoBusinessScenarioId Id)
  {
    return ::GetTypeHash(Id.Value);
  }
};

struct FCrowdDemoBusinessPlannerId
{
  uint32 Value = 0;
  constexpr bool IsValid() const { return Value != 0; }
  constexpr auto operator<=>(const FCrowdDemoBusinessPlannerId&) const =
    default;
  friend uint32 GetTypeHash(const FCrowdDemoBusinessPlannerId Id)
  {
    return ::GetTypeHash(Id.Value);
  }
};

struct FCrowdDemoObjectiveId
{
  uint32 Value = 0;
  constexpr bool IsValid() const { return Value != 0; }
  constexpr auto operator<=>(const FCrowdDemoObjectiveId&) const = default;
};

namespace CrowdDemoBusinessScenarios
{
  inline constexpr FCrowdDemoBusinessScenarioId NoBusiness{20001};
  inline constexpr FCrowdDemoBusinessScenarioId Mixed{20002};
  inline constexpr FCrowdDemoBusinessScenarioId FriendlyLogistics{20003};
  inline constexpr FCrowdDemoBusinessScenarioId VatShowcase{20004};
  inline constexpr FCrowdDemoBusinessScenarioId RangedProjectile{20005};
  inline constexpr FCrowdDemoBusinessScenarioId MixedCombat{20006};
}

namespace CrowdDemoBusinessPlanners
{
  inline constexpr FCrowdDemoBusinessPlannerId Logistics{21001};
  inline constexpr FCrowdDemoBusinessPlannerId PursueAttack{21002};
  inline constexpr FCrowdDemoBusinessPlannerId GuardFlee{21003};
  inline constexpr FCrowdDemoBusinessPlannerId Roam{21004};
  inline constexpr FCrowdDemoBusinessPlannerId Escort{21005};
  inline constexpr FCrowdDemoBusinessPlannerId VatShowcase{21006};
  inline constexpr FCrowdDemoBusinessPlannerId RangedAttack{21007};
  inline constexpr FCrowdDemoBusinessPlannerId MixedCombat{21008};
}

namespace CrowdDemoBusinessObjectives
{
  inline constexpr FCrowdDemoObjectiveId LogisticsSource{22001};
  inline constexpr FCrowdDemoObjectiveId LogisticsSink{22002};
  inline constexpr FCrowdDemoObjectiveId RoamRoute{22003};
}

namespace CrowdDemoBusinessActions
{
  inline constexpr uint32 Claim = 23001;
  inline constexpr uint32 Requeue = 23002;
  inline constexpr uint32 Cancel = 23003;
  inline constexpr uint32 Fire = 23004;
  inline constexpr uint32 InjectHit = 23005;
  inline constexpr uint32 Attack = 23006;
}

struct FCrowdDemoPlannerAssignment
{
  FCrowdDemoBusinessPlannerId PlannerId;
  uint32 CohortId = 0;
  uint16 Ordinal = 0;

  bool IsValid() const
  {
    return PlannerId.IsValid() && CohortId != 0;
  }
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoPlannerAgentFact
{
  FCrowdStableEntityRef EntityRef;
  FCrowdDemoPlannerAssignment Assignment;
  FCrowdCapabilitySet Capabilities;
  uint32 FactionId = 0;
  uint32 AttackProfileId = 0;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  FVector Facing = FVector::ForwardVector;
  FCrowdStableEntityRef TaskRef;
  int32 Health = 100;
  int64 LastAttackFixedStep = -1000;
  int64 LastLogisticsFixedStep = -1000;
  int64 HitReactionUntilFixedStep = INDEX_NONE;
  FVector HitReactionVelocity = FVector::ZeroVector;
  uint32 InteractionLayer = 0;
  uint32 TransitionRevision = 1;
  FCrowdDemoAttackState AttackState;
  bool bCarrying = false;
  bool bActive = true;

  bool IsValid() const;
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoObjectiveFact
{
  FCrowdDemoObjectiveId ObjectiveId;
  FCrowdStableEntityRef EntityRef;
  FVector Location = FVector::ZeroVector;
  uint64 Revision = 0;

  bool IsValid() const;
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoPlanningSettings
{
  int32 PopulationLimit = 20;
  float MaximumSpeedCmps = 500.0f;
  float ScaleMaximumSpeedCmps = 10.0f;
  float InteractionRadiusCm = 180.0f;
  float ScaleInteractionRadiusCm = 5000.0f;
  int32 LogisticsCooldownSteps = 30;
  int32 AttackCooldownSteps = 30;
  int32 RoamSwitchIntervalSteps = 300;
  int32 HitReactionDurationSteps = 6;

  bool IsValid() const;
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoPlanningSnapshot
{
  FCrowdDemoBusinessScenarioId ScenarioId;
  int64 FixedStepIndex = INDEX_NONE;
  uint64 FactRevision = 0;
  FCrowdDemoPlanningSettings Settings;
  TArray<FCrowdDemoPlannerAgentFact> Agents;
  TArray<FCrowdDemoObjectiveFact> Objectives;
  uint64 StableHash = 0;
  bool bValid = false;

  bool Finalize();
};

enum class ECrowdDemoContextRequestKind : uint8
{
  TargetKinematics = 1,
  FormationAnchor = 2
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoContextRequest
{
  ECrowdDemoContextRequestKind Kind =
    ECrowdDemoContextRequestKind::TargetKinematics;
  FCrowdStableEntityRef SubjectRef;
  FVector LocalOffset = FVector::ZeroVector;
  uint64 FactRevision = 0;

  bool IsValid() const;
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoHostIntent
{
  uint32 ActionTypeId = 0;
  uint64 CommitId = 0;
  FCrowdStableEntityRef InstigatorRef;
  FCrowdStableEntityRef TargetRef;
  uint32 PayloadTypeId = 0;
  uint32 ExpectedRevision = 0;
  int32 Quantity = 0;
  FVector Vector = FVector::ZeroVector;

  bool IsValid() const;
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoPlannerDecision
{
  FCrowdStableEntityRef EntityRef;
  FCrowdDemoBusinessPlannerId PlannerId;
  TArray<FCrowdDemoDesiredSource, TInlineAllocator<16>> DesiredSources;
  TArray<FCrowdDemoContextRequest, TInlineAllocator<8>> ContextRequests;
  TArray<FCrowdDemoHostIntent, TInlineAllocator<4>> HostIntents;
  uint32 DiagnosticLabel = 0;
  FCrowdStableEntityRef TargetRef;
  FCrowdStableEntityRef TaskRef;
  uint64 StableHash = 0;
  bool bValid = false;

  bool Finalize();
};

struct FCrowdDemoPlannerDecisionBatch
{
  FCrowdDemoBusinessScenarioId ScenarioId;
  int64 FixedStepIndex = INDEX_NONE;
  TArray<FCrowdDemoPlannerDecision> Decisions;
  uint64 StableHash = 0;
  bool bValid = false;
};

namespace CrowdDemoCanonicalPlannerPayload
{
  inline FCrowdStableEntityRef Ref(
    const FCrowdStableEntityRef& Value)
  {
    FCrowdStableEntityRef Out;
    FMemory::Memzero(&Out, sizeof(Out));
    Out.ProviderId = Value.ProviderId;
    Out.StableEntityId = Value.StableEntityId;
    Out.LifecycleSerial = Value.LifecycleSerial;
    return Out;
  }

  template<typename T>
  T Copy(const T& Value)
  {
    return Value;
  }

  inline FCrowdFollowEntityPayload Copy(
    const FCrowdFollowEntityPayload& Value)
  {
    FCrowdFollowEntityPayload Out;
    FMemory::Memzero(&Out, sizeof(Out));
    Out.TargetRef = Ref(Value.TargetRef);
    Out.LocalOffset = Value.LocalOffset;
    Out.MaximumSpeedCmps = Value.MaximumSpeedCmps;
    Out.AcceptanceRadiusCm = Value.AcceptanceRadiusCm;
    Out.PositionGain = Value.PositionGain;
    return Out;
  }

  inline FCrowdPursueEntityPayload Copy(
    const FCrowdPursueEntityPayload& Value)
  {
    FCrowdPursueEntityPayload Out;
    FMemory::Memzero(&Out, sizeof(Out));
    Out.TargetRef = Ref(Value.TargetRef);
    Out.MaximumSpeedCmps = Value.MaximumSpeedCmps;
    Out.AcceptanceRadiusCm = Value.AcceptanceRadiusCm;
    Out.MaximumPredictionSeconds = Value.MaximumPredictionSeconds;
    return Out;
  }

  inline FCrowdFleeFromEntityPayload Copy(
    const FCrowdFleeFromEntityPayload& Value)
  {
    FCrowdFleeFromEntityPayload Out;
    FMemory::Memzero(&Out, sizeof(Out));
    Out.TargetRef = Ref(Value.TargetRef);
    Out.MaximumSpeedCmps = Value.MaximumSpeedCmps;
    Out.SafeDistanceCm = Value.SafeDistanceCm;
    Out.MaximumPredictionSeconds = Value.MaximumPredictionSeconds;
    return Out;
  }

  inline FCrowdMaintainDistancePayload Copy(
    const FCrowdMaintainDistancePayload& Value)
  {
    FCrowdMaintainDistancePayload Out;
    FMemory::Memzero(&Out, sizeof(Out));
    Out.TargetRef = Ref(Value.TargetRef);
    Out.MinimumDistanceCm = Value.MinimumDistanceCm;
    Out.MaximumDistanceCm = Value.MaximumDistanceCm;
    Out.HysteresisCm = Value.HysteresisCm;
    Out.MaximumCorrectionSpeedCmps =
      Value.MaximumCorrectionSpeedCmps;
    return Out;
  }

  inline FCrowdFaceEntityPayload Copy(
    const FCrowdFaceEntityPayload& Value)
  {
    FCrowdFaceEntityPayload Out;
    FMemory::Memzero(&Out, sizeof(Out));
    Out.TargetRef = Ref(Value.TargetRef);
    return Out;
  }

  inline FCrowdSpeedLimitPayload Copy(
    const FCrowdSpeedLimitPayload& Value)
  {
    FCrowdSpeedLimitPayload Out;
    FMemory::Memzero(&Out, sizeof(Out));
    Out.MaximumSpeedCmps = Value.MaximumSpeedCmps;
    Out.AllowedNavLayerMask = Value.AllowedNavLayerMask;
    return Out;
  }

  inline FCrowdTimedImpulsePayload Copy(
    const FCrowdTimedImpulsePayload& Value)
  {
    FCrowdTimedImpulsePayload Out;
    FMemory::Memzero(&Out, sizeof(Out));
    Out.InitialVelocity = Value.InitialVelocity;
    Out.DecayMode = Value.DecayMode;
    return Out;
  }

  inline FCrowdDemoBehaviorSourcePayload Copy(
    const FCrowdDemoBehaviorSourcePayload& Value)
  {
    FCrowdDemoBehaviorSourcePayload Out;
    FMemory::Memzero(&Out, sizeof(Out));
    Out.Vector = Value.Vector;
    Out.TargetRef = Ref(Value.TargetRef);
    Out.CommitId = Value.CommitId;
    Out.PrimaryId = Value.PrimaryId;
    Out.SecondaryId = Value.SecondaryId;
    Out.Quantity = Value.Quantity;
    Out.Flags = Value.Flags;
    return Out;
  }
}

class MASSCROWDDEMOBUSINESS_API FCrowdDemoPlannerWriter
{
public:
  explicit FCrowdDemoPlannerWriter(FCrowdDemoPlannerDecision& InDecision)
    : Decision(InDecision) {}

  template <typename PayloadType>
  bool AddStandard(
    FCrowdBehaviorControllerId ControllerId,
    uint32 SourceSequence,
    FCrowdBehaviorSourceTypeId TypeId,
    const PayloadType& Payload,
    int32 LifetimeSteps = 0,
    int16 Priority = 0)
  {
    if (Decision.DesiredSources.Num()
      >= CrowdBehavior::MaxSourcesPerEntity)
      return false;
    FCrowdDemoDesiredSource& Entry =
      Decision.DesiredSources.AddDefaulted_GetRef();
    Entry.ControllerId = ControllerId;
    Entry.SourceSequence = SourceSequence;
    Entry.SourceTypeId = TypeId;
    Entry.Priority = Priority;
    Entry.LifetimeSteps = LifetimeSteps;
    const PayloadType Canonical =
      CrowdDemoCanonicalPlannerPayload::Copy(Payload);
    return Entry.Payload.Set(
      CrowdStandardSources::PayloadSchema(TypeId), Canonical);
  }

  bool AddDemo(
    FCrowdBehaviorControllerId ControllerId,
    uint32 SourceSequence,
    FCrowdBehaviorSourceTypeId TypeId,
    const FCrowdDemoBehaviorSourcePayload& Payload,
    int32 LifetimeSteps = 0,
    int16 Priority = 0);

  bool AddContext(const FCrowdDemoContextRequest& Request);
  bool AddHostIntent(const FCrowdDemoHostIntent& Intent);

private:
  FCrowdDemoPlannerDecision& Decision;
};

class MASSCROWDDEMOBUSINESS_API ICrowdDemoBusinessPlanner
{
public:
  virtual ~ICrowdDemoBusinessPlanner() = default;
  virtual FCrowdDemoBusinessPlannerId GetPlannerId() const = 0;
  virtual bool Evaluate(
    const FCrowdDemoPlanningSnapshot& Snapshot,
    const FCrowdDemoPlannerAgentFact& Agent,
    FCrowdDemoPlannerWriter& Writer) const = 0;
};

class MASSCROWDDEMOBUSINESS_API FCrowdDemoBusinessPlannerRegistry
{
public:
  bool Register(
    TSharedRef<const ICrowdDemoBusinessPlanner, ESPMode::ThreadSafe>
      Planner);
  bool Freeze();
  bool IsFrozen() const { return bFrozen; }
  const ICrowdDemoBusinessPlanner* Find(
    FCrowdDemoBusinessPlannerId PlannerId) const;
  uint64 GetStableHash() const { return StableHash; }

private:
  TArray<TSharedRef<const ICrowdDemoBusinessPlanner,
    ESPMode::ThreadSafe>> Planners;
  uint64 StableHash = 0;
  bool bFrozen = false;
};

class MASSCROWDDEMOBUSINESS_API FCrowdDemoBusinessPlannerRunner
{
public:
  static bool BuildDefaultRegistry(
    FCrowdDemoBusinessPlannerRegistry& OutRegistry);

  static bool Evaluate(
    const FCrowdDemoBusinessPlannerRegistry& Registry,
    const FCrowdDemoPlanningSnapshot& Snapshot,
    FCrowdDemoPlannerDecisionBatch& OutBatch);

  static bool EvaluateSharded(
    const FCrowdDemoBusinessPlannerRegistry& Registry,
    const FCrowdDemoPlanningSnapshot& Snapshot,
    int32 ShardSize,
    FCrowdDemoPlannerDecisionBatch& OutBatch,
    bool bReverseDispatchOrder = false);

  static bool BuildMixedAssignment(
    int32 SlotIndex,
    FCrowdDemoPlannerAssignment& OutAssignment);

  static bool GetObjectivePlacementOrdinal(
    const FCrowdDemoPlannerAssignment& Assignment,
    uint32& OutPlacementOrdinal);
};
