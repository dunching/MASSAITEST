#pragma once

#include "CoreMinimal.h"

struct FCrowdDemoHardSeparationPbdAgent
{
  int32 AgentId = INDEX_NONE;
  FVector Location = FVector::ZeroVector;
  float RadiusCm = 42.0f;
};

struct FCrowdDemoHardSeparationPbdPair
{
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  int32 MinAgentIndex = INDEX_NONE;
  int32 MaxAgentIndex = INDEX_NONE;
};

struct FCrowdDemoHardSeparationPbdSettings
{
  int32 IterationCount = 3;
  float MaxPairCorrectionPerIterationCm = 12.0f;
};

struct FCrowdDemoHardSeparationPbdResult
{
  int32 AgentId = INDEX_NONE;
  FVector CorrectedLocation = FVector::ZeroVector;
  FVector Correction = FVector::ZeroVector;
  int32 CorrectedPairCount = 0;
};

struct FCrowdDemoHardSeparationPbdSummary
{
  int32 CandidatePairCount = 0;
  int32 CorrectedAgentCount = 0;
  int32 CorrectedPairCount = 0;
  int32 IterationCount = 0;
  float MaxPairCorrectionCm = 0.0f;
  float MaxAgentTotalCorrectionCm = 0.0f;
};

struct FCrowdDemoHardSeparationPbdIterationPairCorrection
{
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  float PairCorrectionCm = 0.0f;
  FVector MinAgentCorrection = FVector::ZeroVector;
  FVector MaxAgentCorrection = FVector::ZeroVector;
};

struct FCrowdDemoHardSeparationPbdIterationDiagnostic
{
  int32 IterationIndex = INDEX_NONE;
  TArray<FCrowdDemoHardSeparationPbdIterationPairCorrection> PairCorrections;
  TArray<FCrowdDemoHardSeparationPbdResult> AgentResults;
  uint32 StableHash = 2166136261u;
};

class MASSAICROWDDEMO_API FCrowdDemoHardSeparationPbdKernel
{
public:
  static void BuildOverlapPairs(
    TConstArrayView<FCrowdDemoHardSeparationPbdAgent> Agents,
    float DistanceThresholdCm,
    TArray<FCrowdDemoHardSeparationPbdPair>& OutPairs);

  static void Solve(
    TConstArrayView<FCrowdDemoHardSeparationPbdAgent> Agents,
    const FCrowdDemoHardSeparationPbdSettings& Settings,
    TArray<FCrowdDemoHardSeparationPbdPair>& OutPairs,
    TArray<FCrowdDemoHardSeparationPbdResult>& OutResults,
    FCrowdDemoHardSeparationPbdSummary& OutSummary,
    TArray<FCrowdDemoHardSeparationPbdIterationDiagnostic>* OutIterationDiagnostics = nullptr);
};
