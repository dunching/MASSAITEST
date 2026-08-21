#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerRuntimeV2.h"

enum class ECrowdWorkerLifecyclePhase : uint8
{
  SpawnPending = 0,
  Active,
  Suspended,
  Removed,
  Count
};

struct MASSCROWDRUNTIME_API FCrowdWorkerLifecycleState
{
  FCrowdStableEntityRef EntityRef;
  uint64 Revision = 0;
  uint64 SourceInputSequence = 0;
  uint64 InitialStateHash = 0;
  ECrowdWorkerLifecyclePhase Phase =
    ECrowdWorkerLifecyclePhase::SpawnPending;

  bool IsValid() const
  {
    return EntityRef.IsValid()
      && Revision != 0
      && SourceInputSequence != 0
      && InitialStateHash != 0
      && Phase < ECrowdWorkerLifecyclePhase::Count;
  }

  bool operator==(const FCrowdWorkerLifecycleState& Other) const = default;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerLifecycleTransition
{
  FCrowdStableEntityRef EntityRef;
  uint64 ExpectedRevision = 0;
  uint64 Revision = 0;
  ECrowdWorkerLifecyclePhase TargetPhase =
    ECrowdWorkerLifecyclePhase::SpawnPending;

  bool IsValid() const
  {
    return EntityRef.IsValid()
      && Revision != 0
      && Revision > ExpectedRevision
      && TargetPhase < ECrowdWorkerLifecyclePhase::Count;
  }

  bool operator==(const FCrowdWorkerLifecycleTransition& Other) const =
    default;
};

class MASSCROWDRUNTIME_API FCrowdWorkerLifecycleStateCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574C46u;
  static constexpr uint16 SchemaVersion = 2;

  static bool Encode(
    const FCrowdWorkerLifecycleState& State,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerLifecycleState& OutState);
};

class MASSCROWDRUNTIME_API FCrowdWorkerLifecycleTransitionCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574C54u;
  static constexpr uint16 SchemaVersion = 1;

  static bool Encode(
    const FCrowdWorkerLifecycleTransition& Transition,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerLifecycleTransition& OutTransition);
};

class MASSCROWDRUNTIME_API FCrowdWorkerLifecycleStateMachine
{
public:
  static bool CanTransition(
    ECrowdWorkerLifecyclePhase From,
    ECrowdWorkerLifecyclePhase To);

  // A null Current state admits only the initial SpawnPending state.
  static bool Apply(
    const FCrowdWorkerLifecycleState* Current,
    const FCrowdWorkerLifecycleTransition& Transition,
    uint64 SourceInputSequence,
    uint64 InitialStateHash,
    FCrowdWorkerLifecycleState& OutState);
};

enum class ECrowdWorkerParticipationChannel : uint8
{
  Particle = 0,
  Combat,
  Presentation,
  Count
};

constexpr uint8 CrowdWorkerParticipationBit(
  const ECrowdWorkerParticipationChannel Channel)
{
  return Channel < ECrowdWorkerParticipationChannel::Count
    ? uint8{1} << static_cast<uint8>(Channel)
    : 0;
}

namespace CrowdWorkerParticipation
{
  constexpr uint8 All =
    CrowdWorkerParticipationBit(
      ECrowdWorkerParticipationChannel::Particle)
    | CrowdWorkerParticipationBit(
      ECrowdWorkerParticipationChannel::Combat)
    | CrowdWorkerParticipationBit(
      ECrowdWorkerParticipationChannel::Presentation);
}

struct MASSCROWDRUNTIME_API FCrowdWorkerParticipationState
{
  FCrowdStableEntityRef EntityRef;
  uint64 Revision = 0;
  uint8 EnabledMask = CrowdWorkerParticipation::All;

  bool IsValid() const
  {
    return EntityRef.IsValid()
      && Revision != 0
      && (EnabledMask & ~CrowdWorkerParticipation::All) == 0;
  }

  bool IsEnabled(const ECrowdWorkerParticipationChannel Channel) const
  {
    const uint8 Bit = CrowdWorkerParticipationBit(Channel);
    return Bit != 0 && (EnabledMask & Bit) != 0;
  }

  bool operator==(const FCrowdWorkerParticipationState& Other) const =
    default;
};

class MASSCROWDRUNTIME_API FCrowdWorkerParticipationStateCodec
{
public:
  static constexpr uint32 SchemaId = 0x43575041u;
  static constexpr uint16 SchemaVersion = 1;

  static bool Encode(
    const FCrowdWorkerParticipationState& State,
    FCrowdWorkerPayload& OutPayload);
  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerParticipationState& OutState);
};
