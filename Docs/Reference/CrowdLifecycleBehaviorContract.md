# Crowd Lifecycle and Behavior Contract

## 1. Purpose

Crowd simulation requires explicit separation between lifecycle, behavior, objective/navigation, movement constraints, correction, and acceptance. These concerns may be configured together, but they do not share ownership.

The questions are distinct:

| Concern | Question |
|---|---|
| Movement | Where should the Agent go? |
| Lifecycle | Does the Agent exist and participate? |
| Behavior | What does the Agent intend or allow? |
| Movement Constraint | What prevents normal movement? |
| Correction | How is authoritative state repaired? |
| Acceptance | Did the configured simulation satisfy the expected result? |

Keeping these questions separate prevents a scenario name, test phase, fixture index, or temporary state from becoming a hidden movement algorithm.

This document defines a target contract. It does not claim that every contract described here is already implemented. Current implementation status remains owned by [`../CurrentArchitecture.md`](../CurrentArchitecture.md), and implementation order remains owned by [`../PhasePlan.md`](../PhasePlan.md).

## 2. Unified Agent Model

The conceptual runtime model is:

```text
Lifecycle State
+ Behavior Source Set
+ Capability / Profile
+ Objective / FlowBinding
+ Movement Constraint
+ Interaction Participation
+ Correction State
        ↓
Persistent Worker Runtime
        ↓
Resolved Movement / Interaction / Final Kinematic State
```

Each part has a separate owner and purpose:

- **Lifecycle State** controls authoritative presence and lifecycle serial validity.
- **Behavior Source Set** expresses current intent and contributes to resolved channels.
- **Capability / Profile** contains stable facts about what the Agent can do.
- **Objective / FlowBinding** associates navigation intent with a versioned macro-navigation resource.
- **Movement Constraint** limits or locks otherwise valid movement.
- **Interaction Participation** selects which interaction domains the Agent participates in without redefining entity existence.
- **Correction State** records explicit, versioned authoritative repair.

The Host may publish versioned external facts and commands. The Persistent Worker validates, orders, executes, and owns resulting simulation state. Mass entities and presentation instances are proxies or consumers; they do not become a second simulation authority.

## 3. Lifecycle Contract

Lifecycle is a generic Worker-owned contract. It must not contain scenario names, test-case enums, map names, formation indices, or acceptance phases.

Illustrative lifecycle states are:

```text
SpawnPending
Active
Suspended
Removed
```

These names describe generic semantics rather than a required final enum. Their meanings are:

- `SpawnPending`: an admitted entity is awaiting authoritative activation.
- `Active`: the entity exists and may participate according to its other contracts.
- `Suspended`: the entity remains valid but normal participation is temporarily disabled by an explicit lifecycle policy.
- `Removed`: the lifecycle instance is no longer active; stale inputs must be rejected by `LifecycleSerial`.

A scenario or gameplay system may publish lifecycle input such as spawn, suspend, resume, or remove. The Worker owns admission, ordering, lifecycle-serial changes, state transition, dependent work, and observable results. A scenario must not execute lifecycle behavior by directly mutating Movement, Particle, Transform, or proxy state.

Lifecycle state answers whether the entity lifecycle is valid. It does not, by itself, select a destination, choose a Flow resource, author preferred velocity, or define an acceptance condition.

## 4. Behavior Source Contract

A Behavior Source expresses intent or an ongoing state contribution. Multiple sources may coexist and resolve into Movement, Facing, Constraint, Interaction, Business, or Presentation channels according to the Behavior Source architecture.

Illustrative behaviors include:

```text
Idle
Moving
Waiting
Recovering
Attacking
Locked
Dead
```

These are semantic examples, not a mandatory closed enum. Product-specific providers may define source types through stable registered contracts.

A Behavior Source may request movement, facing, a lock, a speed limit, an interaction, or a business action. It does not directly write:

```text
Velocity
Transform
Particle solver state
```

The Worker Runtime evaluates sources, resolves their channels deterministically, and executes the common production domains. Even the winning source cannot bypass Local Predictive, Particle safety, obstacle/bounds handling, final quantization, or lifecycle validation.

## 5. Movement Contract

Normal navigation follows one common production chain:

```text
Behavior
    ↓
Objective
    ↓
FlowBinding
    ↓
MovementPlanning
    ↓
Local Predictive
    ↓
Movement
    ↓
Particle
```

Behavior provides intent. `ObjectiveRef` identifies navigation intent. `FlowBinding` binds an entity and cohort to the versioned macro-navigation resource used for that objective. MovementPlanning samples from the Agent's current Worker-owned kinematic position. Local Predictive and Particle retain safety responsibility.

The following are forbidden production selectors:

```text
Scenario enum → movement algorithm or Flow selection
FormationIndex → continuous movement or preferred velocity
AgentId → movement branch, destination, or Flow selection
```

Scenario code may configure fixtures, objectives, cohorts, resources, capabilities, Behavior Sources, and acceptance. Once published, those inputs enter the same Worker production path as equivalent gameplay inputs.

## 6. Movement Constraint Contract

A Movement Constraint is a generic resolved limitation on otherwise valid movement intent. Examples include:

```text
Stunned
Frozen
WaitingForCommand
SpawnLock
Recovery
```

A constraint may lock translation, bound speed, restrict navigation layers, or otherwise limit a resolved movement request. It must have explicit source, scope, ordering, lifetime, and observability.

A constraint is not encoded as:

```text
PreferredVelocity = zero
Authoritative velocity override
Temporary MovementProfile mutation
```

Zero preferred velocity describes a value, not why movement is prohibited. An authoritative velocity override changes execution authority and therefore cannot substitute for a semantic lock. Constraints must resolve before normal movement execution while remaining subject to lifecycle and correction ordering.

## 7. Interaction Participation Contract

Entity existence and subsystem participation are separate concerns. A valid entity may participate in one domain and not another.

Examples of independently expressed participation include:

```text
Particle participation
Combat participation
Visibility / presentation participation
```

Participation does not redefine stable identity or lifecycle. Disabling visibility does not remove an entity. Disabling combat does not grant movement authority. Disabling Particle participation must not silently disable every other simulation domain.

A scenario may publish generic participation inputs as fixture or gameplay configuration. It must not directly toggle simulation subsystem internals. The Worker owns validation, effective-step ordering, domain enrollment, dependent work, and the resulting observable state.

## 8. Correction Contract

Correction is explicit authoritative repair of state that is stale, divergent, invalid, or intentionally repositioned by an authority transition.

Examples include:

```text
Network correction
LateJoin synchronization
Authoritative gameplay reposition
Recovery from invalid state
```

Every correction must be:

- **Explicit**: identified as a correction rather than hidden inside movement or profile data.
- **Versioned**: fenced by stable entity identity, lifecycle serial, generation, and correction revision as applicable.
- **Observable**: accepted, rejected, and applied corrections must be visible to diagnostics and result consumers.

Correction application belongs to the Worker authority boundary. It must coherently repair all affected authoritative fields and invalidate incompatible in-flight work before a new committed result becomes visible.

The following pattern is forbidden:

```text
if (TestCase)
{
    Teleport(...);
}
```

A test or scenario may request a generic, versioned authoritative reposition when its fixture requires one. It may not own a hidden teleport branch or directly write the authoritative Transform.

## 9. MovementProfile Boundary

`MovementProfile` contains stable movement capability facts such as:

```text
Maximum speed
Radius
Mobility
Physical or navigation capability data
```

`MovementProfile` does not contain transient semantic state such as:

```text
Idle
Settling
Removed
Temporary lock
Test phase
Recovery state
Spawn / despawn
Impulse or correction
```

Those facts belong to Behavior, Lifecycle, Movement Constraint, or explicit correction contracts. `MovementProfileRevision` must not be used as a generic envelope for unrelated temporary state.

## 10. T1 Mapping

T1 `OpenSpawnRelaxation` is not a new movement algorithm. Its scenario identity may define the fixture and acceptance criteria, but it must not define runtime movement, lifecycle execution, Particle enrollment, or correction behavior.

The old ownership shape is:

```text
OpenSpawnRelaxation
        ↓
Special movement / lifecycle mode
```

The target ownership shape is:

```text
Fixture
    ↓
Lifecycle State
    ↓
Behavior Source
    ↓
Movement Constraint
    ↓
Worker Runtime
    ↓
Acceptance Observer
```

The fixture may declare initial layout, staged inputs, and expected observations. Generic lifecycle and participation inputs express whether an Agent is active in the relevant domains. A Behavior Source and Movement Constraint express waiting or movement lock without publishing zero authoritative preferred velocity. The Worker executes those contracts. The Acceptance Observer measures activation, insertion/removal semantics, settling, pressure propagation, safety, and determinism without becoming a runtime simulation owner.

## 11. T3 Comparison

T3 previously had the wrong ownership chain:

```text
FormationIndex
    ↓
Flow selection
    ↓
Preferred velocity
```

Slice C replaced it with explicit generic data:

```text
CohortKey
    ↓
ObjectiveRef
    ↓
FlowBinding
```

This moved macro-navigation selection out of scenario/fixture identity and into the common Worker MovementPlanning path.

T1 has the analogous ownership error at the Agent-state boundary:

```text
Scenario identity
    ↓
Agent state ownership
```

Its target replacement is:

```text
Lifecycle
    ↓
Behavior Source
    ↓
Movement Constraint / Interaction Participation
    ↓
Common Worker domains
```

T3 unified navigation-resource selection. T1 must unify lifecycle, behavior, constraint, participation, and correction inputs without adding a T1-specific Worker mode.

## 12. Future Usage

The same separation supports systems beyond T1:

| Area | Examples | Contract use |
|---|---|---|
| Combat | Attack, HitReact, Death | Behavior intent, temporary constraints, lifecycle transition, interaction participation |
| Network | Correction, LateJoin | Versioned correction, lifecycle-safe snapshot/replay, observable rejection |
| Gameplay | Stun, Freeze, Escort | Behavior Source plus Movement Constraint and Objective/FlowBinding where navigation is required |
| Crowd | Spawn waves, despawn waves, reinforcement | Generic lifecycle commands, participation, stable capability, common Worker execution |

These uses differ in business policy but share the same simulation contracts. No product feature requires a scenario enum in a production movement or Particle domain.

## 13. Relationship With Existing Systems

| System | Contract relationship |
|---|---|
| `ObjectiveRef` | Identifies navigation intent; it does not contain movement execution or lifecycle. |
| `FlowBinding` | Associates an entity/cohort/objective with a versioned macro-navigation resource; it does not own capability or scheduling. |
| `MovementProfile` | Contains stable movement capability and physical facts; it does not carry temporary behavior, lifecycle, or correction. |
| `TargetRegion` | Provides near-target spatial distribution, capacity, plan/claim, and overflow semantics; it is not a lifecycle or permanent formation-slot system. |
| Particle | Provides final interaction and safety resolution; it does not decide business intent or scenario progression. |
| Worker Runtime | Owns validation, ordering, scheduling, domain execution, authoritative simulation state, and committed results. |

Related detailed contracts:

- [`../TargetArchitecture.md`](../TargetArchitecture.md) — final architecture and unified behavior rules.
- [`../CurrentArchitecture.md`](../CurrentArchitecture.md) — current implementation state and remaining migration debt.
- [`../EntityBehaviorSourceArchitecture.md`](../EntityBehaviorSourceArchitecture.md) — Behavior Source providers, commands, resolution, and channels.
- [`../MassCrowdUnifiedRuntimeAndReplicationContract.md`](../MassCrowdUnifiedRuntimeAndReplicationContract.md) — stable identity, lifecycle, correction, replication, and commit contracts.
- [`WorkerOwnershipMatrix.md`](WorkerOwnershipMatrix.md) — field ownership and writer boundaries.
