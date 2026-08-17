# WA8 — Legacy Round Transaction Removal

This note records the architecture cut performed after Full Worker Production first bypassed the legacy Demo Round transaction.

## Result

The Demo server live path now treats the persistent Worker runtime as the only simulation authority after the one-shot bootstrap/control installation step.

Removed from the Production Demo path:

- persistent `FCrowdDemoRoundWorkBatch`
- `BeginBoundaryTransaction`
- `TryPrepareRoundApply`
- `BoundaryOrchestrator` and its transaction/poll state
- old Round Stage struct surface
- Prepared Movement commit envelope
- Prepared Target/Resource commit envelope
- Prepared Particle Diagnostic commit envelope
- second-pass `ConsumeBoundaryMovementWork` / `ConsumeBoundaryParticleWork` / `ConsumeBoundaryFacingWork`
- legacy PostFinalize / AuthorityCommit / ClientPredictionCommit adapters

The remaining bootstrap compatibility work is deliberately one-shot and synchronous. It constructs the initial immutable Movement / Target / Projectile / Facing control inputs, submits them to the Worker, and immediately marks that fixed step as Worker-owned. It is not a persistent transaction scheduler and does not commit simulation authority itself.

The authoritative server commit remains:

`Worker Published Result -> Runtime Owner Commit Barrier -> Dirty Mass Apply -> Proxy/side effects -> Checkpoint -> FinishFixedStep`

`CrowdDemoRoundSimProcessors.h` now exposes only the two actual runtime processors: Input Sync and Result Apply. Bootstrap calculation helpers are implementation-local in the `.cpp` file.

## Validation status

Static structural validation passed before the temporary audit tooling was removed:

- all retired transaction / Stage / Prepared second-pass symbols were zero in Production source
- the processor header contained exactly two `UMassProcessor` classes
- the synchronous bootstrap graph, Worker intent path, Runtime Owner Barrier, Dirty Mass apply and checkpoint path were present
- Owner Barrier appeared before checkpoint publication in the live server advance path
- basic lexical balance checks passed for the modified Pipeline/Processor/Test files

This is **not** a UE runtime validation record. UE build, PIE, T1–T8 and scale regression remain deferred until the architecture-convergence batch is ready for runtime validation.
