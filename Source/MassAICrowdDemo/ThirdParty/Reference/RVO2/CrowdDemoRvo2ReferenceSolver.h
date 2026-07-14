#pragma once

#include "Mass/CrowdDemoDeterministicOrcaKernel.h"

#if WITH_DEV_AUTOMATION_TESTS

// Test/Development-only adapter over the RVO2 linearProgram1/2 algorithm.
// It consumes the project's already-built half-planes and never participates
// in constraint construction, simulation stepping, or shipping behavior.
class MASSAICROWDDEMO_API FCrowdDemoRvo2ReferenceSolver
{
public:
  static FCrowdDemoOrcaContinuousSolveResult Solve(
    const FCrowdDemoOrcaContinuousSolveInput& Input);
};

#endif
