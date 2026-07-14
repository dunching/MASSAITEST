# RVO2 reference LP adapter

- Upstream: https://github.com/snape/RVO2
- Fixed commit: `b577921d2bc1281a6b721c2d4778f397d37da97d`
- Upstream file adapted: `src/Agent.cc`, functions `linearProgram1` and `linearProgram2`
- License: Apache-2.0
- Copyright: 2008 University of North Carolina at Chapel Hill

This directory is compiled only when `WITH_DEV_AUTOMATION_TESTS` is enabled.
It is a differential-test reference and is not connected to shipping Mass
simulation. The adapter consumes `FCrowdDemoOrcaContinuousSolveInput`; it does
not build neighbors or ORCA constraints and does not include RVO2 Simulator,
Agent storage, KdTree, obstacles, OpenMP, or time stepping.

Local modifications are documented in the source header. The full upstream
license is included as `LICENSE`.
