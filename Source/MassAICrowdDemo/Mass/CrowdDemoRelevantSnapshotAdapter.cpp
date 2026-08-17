#include "Mass/CrowdDemoRelevantSnapshotAdapter.h"

namespace
{
  class FCrowdDemoPayloadWriter
  {
  public:
    explicit FCrowdDemoPayloadWriter(TArray<uint8>& InBytes) : Bytes(InBytes)
    {
      Bytes.Reset();
    }

    void U8(const uint8 Value) { Bytes.Add(Value); }
    void U16(const uint16 Value) { Unsigned(Value); }
    void U32(const uint32 Value) { Unsigned(Value); }
    void U64(const uint64 Value) { Unsigned(Value); }
    void I32(const int32 Value) { U32(static_cast<uint32>(Value)); }

    void F32(const float Value)
    {
      uint32 Bits = 0;
      FPlatformMemory::Memcpy(&Bits, &Value, sizeof(Bits));
      U32(Bits);
    }

    bool QuantizedVector10(const FVector& Value)
    {
      if (!FMath::IsFinite(Value.X)
        || !FMath::IsFinite(Value.Y)
        || !FMath::IsFinite(Value.Z))
      {
        return false;
      }
      constexpr double Maximum = static_cast<double>(MAX_int32) / 10.0;
      if (FMath::Abs(Value.X) > Maximum
        || FMath::Abs(Value.Y) > Maximum
        || FMath::Abs(Value.Z) > Maximum)
      {
        return false;
      }
      I32(FMath::RoundToInt(Value.X * 10.0));
      I32(FMath::RoundToInt(Value.Y * 10.0));
      I32(FMath::RoundToInt(Value.Z * 10.0));
      return true;
    }

  private:
    template<typename T>
    void Unsigned(const T Value)
    {
      for (uint32 ByteIndex = 0; ByteIndex < sizeof(T); ++ByteIndex)
      {
        Bytes.Add(static_cast<uint8>(Value >> (ByteIndex * 8)));
      }
    }

    TArray<uint8>& Bytes;
  };

  class FCrowdDemoPayloadReader
  {
  public:
    explicit FCrowdDemoPayloadReader(const TConstArrayView<uint8> InBytes)
      : Bytes(InBytes)
    {
    }

    bool U8(uint8& OutValue)
    {
      if (Offset >= Bytes.Num()) return false;
      OutValue = Bytes[Offset++];
      return true;
    }

    bool U16(uint16& OutValue) { return Unsigned(OutValue); }
    bool U32(uint32& OutValue) { return Unsigned(OutValue); }
    bool U64(uint64& OutValue) { return Unsigned(OutValue); }

    bool I32(int32& OutValue)
    {
      uint32 Value = 0;
      if (!U32(Value)) return false;
      OutValue = static_cast<int32>(Value);
      return true;
    }

    bool F32(float& OutValue)
    {
      uint32 Bits = 0;
      if (!U32(Bits)) return false;
      FPlatformMemory::Memcpy(&OutValue, &Bits, sizeof(Bits));
      return FMath::IsFinite(OutValue);
    }

    bool QuantizedVector10(FVector& OutValue)
    {
      int32 X = 0;
      int32 Y = 0;
      int32 Z = 0;
      if (!I32(X) || !I32(Y) || !I32(Z)) return false;
      OutValue = FVector(
        static_cast<double>(X) / 10.0,
        static_cast<double>(Y) / 10.0,
        static_cast<double>(Z) / 10.0);
      return true;
    }

    bool AtEnd() const { return Offset == Bytes.Num(); }

  private:
    template<typename T>
    bool Unsigned(T& OutValue)
    {
      if (Offset < 0 || Bytes.Num() - Offset < static_cast<int32>(sizeof(T)))
      {
        return false;
      }
      OutValue = 0;
      for (uint32 ByteIndex = 0; ByteIndex < sizeof(T); ++ByteIndex)
      {
        OutValue |= static_cast<T>(Bytes[Offset++]) << (ByteIndex * 8);
      }
      return true;
    }

    TConstArrayView<uint8> Bytes;
    int32 Offset = 0;
  };

  bool EncodeAgent(
    const FCrowdDemoRoundAgentState& Agent,
    FCrowdRelevantSnapshotEntityPayload& OutPayload)
  {
    if (Agent.AgentId == INDEX_NONE
      || Agent.LifecycleSerial <= 0
      || !FMath::IsFinite(Agent.YawDegrees)
      || !FMath::IsFinite(Agent.RadiusCm)
      || Agent.RadiusCm <= 0.0f)
    {
      return false;
    }
    const FCrowdDemoCombatNetState& Combat = Agent.Combat;
    FCrowdDemoPayloadWriter Writer(OutPayload.Bytes);
    Writer.U16(FCrowdDemoRelevantSnapshotAdapter::AgentPayloadVersion);
    Writer.I32(Agent.AgentId);
    Writer.I32(Agent.LifecycleSerial);
    if (!Writer.QuantizedVector10(FVector(Agent.Location))
      || !Writer.QuantizedVector10(FVector(Agent.Velocity))) return false;
    Writer.F32(Agent.YawDegrees);
    Writer.F32(Agent.RadiusCm);

    Writer.F32(Combat.Health);
    Writer.F32(Combat.MaxHealth);
    Writer.U8(static_cast<uint8>(Combat.LifecycleState));
    Writer.U8(Combat.bAlive);
    Writer.U8(static_cast<uint8>(Combat.BusinessState));
    Writer.I32(Combat.BusinessStateRevision);
    Writer.I32(Combat.BusinessStateEnterFixedStep);
    Writer.I32(Combat.TargetAgentId);
    Writer.I32(Combat.TargetLifecycleSerial);
    Writer.U8(static_cast<uint8>(Combat.AttackPhase));
    Writer.I32(Combat.AttackPhaseEnterFixedStep);
    Writer.I32(Combat.CooldownEndFixedStep);
    Writer.I32(Combat.LockedTargetAgentId);
    Writer.I32(Combat.LockedTargetLifecycleSerial);
    if (!Writer.QuantizedVector10(FVector(Combat.LockedTargetLocation))) return false;
    Writer.I32(Combat.FireSequence);
    Writer.U8(Combat.bFireRequestIssued);
    Writer.U8(static_cast<uint8>(Combat.ReactiveMode));
    if (!Writer.QuantizedVector10(FVector(Combat.HorizontalReactiveVelocity))) return false;
    Writer.F32(Combat.VerticalReactiveVelocityCmps);
    Writer.I32(Combat.ReactiveStartFixedStep);
    Writer.I32(Combat.ReactiveEndFixedStep);
    Writer.I32(Combat.ReactiveRevision);
    Writer.U8(static_cast<uint8>(Combat.RestoreBusinessState));
    Writer.I32(Combat.ApexCount);
    Writer.I32(Combat.LandingCount);
    Writer.I32(Combat.HitFlashRevision);
    Writer.F32(Combat.HitFlashStartServerTimeSeconds);
    Writer.F32(Combat.HitFlashDurationSeconds);
    Writer.U32(Combat.HitFlashProfileKey);
    Writer.F32(Combat.HitFlashPeakIntensity);
    Writer.U64(Combat.LastConsumedHitEventId);
    Writer.U8(static_cast<uint8>(Combat.VisualState));
    Writer.I32(Combat.VisualRevision);
    Writer.F32(Combat.VisualStateStartServerTimeSeconds);
    Writer.U32(Combat.VisualPhaseSeed);
    return true;
  }

  bool DecodeAgent(
    const FCrowdRelevantSnapshotEntityPayload& Payload,
    FCrowdDemoRoundAgentState& OutAgent)
  {
    FCrowdDemoPayloadReader Reader(Payload.Bytes);
    uint16 Version = 0;
    FVector Location;
    FVector Velocity;
    FVector LockedTargetLocation;
    FVector HorizontalReactiveVelocity;
    uint8 LifecycleState = 0;
    uint8 BusinessState = 0;
    uint8 AttackPhase = 0;
    uint8 ReactiveMode = 0;
    uint8 RestoreBusinessState = 0;
    uint8 VisualState = 0;
    FCrowdDemoCombatNetState& Combat = OutAgent.Combat;
    if (!Reader.U16(Version)
      || Version != FCrowdDemoRelevantSnapshotAdapter::AgentPayloadVersion
      || !Reader.I32(OutAgent.AgentId)
      || !Reader.I32(OutAgent.LifecycleSerial)
      || !Reader.QuantizedVector10(Location)
      || !Reader.QuantizedVector10(Velocity)
      || !Reader.F32(OutAgent.YawDegrees)
      || !Reader.F32(OutAgent.RadiusCm)
      || !Reader.F32(Combat.Health)
      || !Reader.F32(Combat.MaxHealth)
      || !Reader.U8(LifecycleState)
      || !Reader.U8(Combat.bAlive)
      || !Reader.U8(BusinessState)
      || !Reader.I32(Combat.BusinessStateRevision)
      || !Reader.I32(Combat.BusinessStateEnterFixedStep)
      || !Reader.I32(Combat.TargetAgentId)
      || !Reader.I32(Combat.TargetLifecycleSerial)
      || !Reader.U8(AttackPhase)
      || !Reader.I32(Combat.AttackPhaseEnterFixedStep)
      || !Reader.I32(Combat.CooldownEndFixedStep)
      || !Reader.I32(Combat.LockedTargetAgentId)
      || !Reader.I32(Combat.LockedTargetLifecycleSerial)
      || !Reader.QuantizedVector10(LockedTargetLocation)
      || !Reader.I32(Combat.FireSequence)
      || !Reader.U8(Combat.bFireRequestIssued)
      || !Reader.U8(ReactiveMode)
      || !Reader.QuantizedVector10(HorizontalReactiveVelocity)
      || !Reader.F32(Combat.VerticalReactiveVelocityCmps)
      || !Reader.I32(Combat.ReactiveStartFixedStep)
      || !Reader.I32(Combat.ReactiveEndFixedStep)
      || !Reader.I32(Combat.ReactiveRevision)
      || !Reader.U8(RestoreBusinessState)
      || !Reader.I32(Combat.ApexCount)
      || !Reader.I32(Combat.LandingCount)
      || !Reader.I32(Combat.HitFlashRevision)
      || !Reader.F32(Combat.HitFlashStartServerTimeSeconds)
      || !Reader.F32(Combat.HitFlashDurationSeconds)
      || !Reader.U32(Combat.HitFlashProfileKey)
      || !Reader.F32(Combat.HitFlashPeakIntensity)
      || !Reader.U64(Combat.LastConsumedHitEventId)
      || !Reader.U8(VisualState)
      || !Reader.I32(Combat.VisualRevision)
      || !Reader.F32(Combat.VisualStateStartServerTimeSeconds)
      || !Reader.U32(Combat.VisualPhaseSeed)
      || !Reader.AtEnd())
    {
      return false;
    }

    OutAgent.Location = FVector_NetQuantize10(Location);
    OutAgent.Velocity = FVector_NetQuantize10(Velocity);
    Combat.LockedTargetLocation = FVector_NetQuantize10(LockedTargetLocation);
    Combat.HorizontalReactiveVelocity =
      FVector_NetQuantize10(HorizontalReactiveVelocity);
    Combat.LifecycleState = static_cast<ECrowdDemoLifecycleState>(LifecycleState);
    Combat.BusinessState = static_cast<ECrowdDemoBusinessState>(BusinessState);
    Combat.AttackPhase = static_cast<ECrowdDemoAttackPhase>(AttackPhase);
    Combat.ReactiveMode = static_cast<ECrowdDemoReactiveMotionMode>(ReactiveMode);
    Combat.RestoreBusinessState =
      static_cast<ECrowdDemoBusinessState>(RestoreBusinessState);
    Combat.VisualState = static_cast<ECrowdDemoVisualState>(VisualState);
    return OutAgent.AgentId != INDEX_NONE
      && OutAgent.LifecycleSerial > 0
      && OutAgent.RadiusCm > 0.0f
      && Combat.bAlive <= 1
      && Combat.bFireRequestIssued <= 1
      && LifecycleState <= static_cast<uint8>(ECrowdDemoLifecycleState::Dead)
      && BusinessState <= static_cast<uint8>(ECrowdDemoBusinessState::Dead)
      && AttackPhase <= static_cast<uint8>(ECrowdDemoAttackPhase::Cooldown)
      && ReactiveMode <= static_cast<uint8>(ECrowdDemoReactiveMotionMode::LandingRecovery)
      && RestoreBusinessState <= static_cast<uint8>(ECrowdDemoBusinessState::Dead)
      && VisualState <= static_cast<uint8>(ECrowdDemoVisualState::Death);
  }
}

FCrowdRelevantSnapshotLimits FCrowdDemoRelevantSnapshotAdapter::MakeLimits()
{
  FCrowdRelevantSnapshotLimits Limits;
  Limits.MaxEntityCount = 10000;
  Limits.MaxChunkCount = 1000;
  Limits.MaxEntitiesPerChunk = 100;
  Limits.MaxChunkPayloadBytes = 60 * 1024;
  Limits.MaxTotalPayloadBytes = 64 * 1024 * 1024;
  Limits.AssemblyTimeoutSeconds = 5.0;
  return Limits;
}

bool FCrowdDemoRelevantSnapshotAdapter::EncodeAgents(
  const TConstArrayView<FCrowdDemoRoundAgentState> Agents,
  TArray<FCrowdRelevantSnapshotEntityPayload>& OutPayloads)
{
  OutPayloads.Reset(Agents.Num());
  for (const FCrowdDemoRoundAgentState& Agent : Agents)
  {
    if (!EncodeAgent(Agent, OutPayloads.AddDefaulted_GetRef()))
    {
      OutPayloads.Reset();
      return false;
    }
  }
  return true;
}

bool FCrowdDemoRelevantSnapshotAdapter::DecodeAgents(
  const TConstArrayView<FCrowdRelevantSnapshotEntityPayload> Payloads,
  TArray<FCrowdDemoRoundAgentState>& OutAgents)
{
  OutAgents.Reset(Payloads.Num());
  int32 PreviousAgentId = INDEX_NONE;
  for (const FCrowdRelevantSnapshotEntityPayload& Payload : Payloads)
  {
    FCrowdDemoRoundAgentState& Agent = OutAgents.AddDefaulted_GetRef();
    if (!DecodeAgent(Payload, Agent)
      || (PreviousAgentId != INDEX_NONE && Agent.AgentId <= PreviousAgentId))
    {
      OutAgents.Reset();
      return false;
    }
    PreviousAgentId = Agent.AgentId;
  }
  return true;
}
