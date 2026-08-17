from pathlib import Path

ROOT = Path('.')
FILES = {
    'pipeline_h': Path('Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.h'),
    'pipeline_cpp': Path('Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp'),
    'processors_h': Path('Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.h'),
    'processors_cpp': Path('Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.cpp'),
    'tests': Path('Source/MassAICrowdDemo/CrowdDemoCombatStateTests.cpp'),
}
REPORT = Path('final_round_cut_audit.txt')
texts = {k: p.read_text(encoding='utf-8-sig') for k, p in FILES.items()}
production = ''.join(texts[k] for k in ['pipeline_h','pipeline_cpp','processors_h','processors_cpp'])

errors = []
lines = ['FINAL ROUND CUT STATIC AUDIT\n']

retired = [
    'FCrowdDemoRoundWorkBatch',
    'BeginBoundaryTransaction',
    'TryPrepareRoundApply',
    'BoundaryOrchestrator',
    'GetRoundWorkState',
    'ECrowdBoundaryTransactionState',
    'ECrowdBoundaryPollResult',
    'FCrowdBoundaryOrchestratorResult',
    'FCrowdDemoPreparedMovementBoundaryCommit',
    'FCrowdDemoPreparedTargetResourcePlan',
    'FCrowdDemoTargetResourceCommitToken',
    'FCrowdDemoPreparedParticleDiagnosticCommit',
    'ConsumeBoundaryMovementWork',
    'ConsumeBoundaryParticleWork',
    'ConsumeBoundaryFacingWork',
    'SetPreparedMovementBoundaryCommit',
    'PreparePendingTargetResourcePlan',
    'FinalValidatePreparedTargetResourcePlan',
    'ApplyPreparedTargetResourcePlanNoFail',
    'SetPreparedCombatBoundaryCommit',
    'GetPreparedCombatBoundaryCommit',
    'IsPreparedCombatBoundaryCommitCurrent',
    'PreparedRuntimeSharedFlowOutputs',
    'PreparedRuntimeComposedGuidance',
    'PreparedRuntimePredictedMovements',
    'PreparedRuntimeParticleResults',
    'PreparedRuntimeFinalKinematics',
    'PreparedRuntimeFacingResults',
    'PreparedFacingRollbackFacts',
    'PreparedParticleDiagnosticCommit',
    'CalculatePreparedTargetResourceHash',
    'FCrowdDemoRoundPostFinalizeMetricsStage',
    'FCrowdDemoRoundAuthorityCommitStage',
    'FCrowdDemoRoundClientPredictionCommitStage',
    'FCrowdDemoRoundCheckpointPublisherStage',
    'FCrowdDemoRoundMovementWorkStage',
    'FCrowdDemoRoundFacingFinalizeStage',
    'FCrowdDemoRoundParticleConstraintStage',
]
lines.append('\n## Retired production symbols\n')
for sym in retired:
    count = production.count(sym)
    lines.append(f'{sym}: {count}\n')
    if count:
        errors.append(f'retired symbol remains: {sym} x{count}')

# The bootstrap value type is allowed, but the old Pipeline member is not.
combat_member = 'FCrowdDemoPreparedCombatBoundaryCommit PreparedCombatBoundaryCommit;'
combat_member_count = texts['pipeline_h'].count(combat_member)
lines.append(f'PreparedCombat pipeline member: {combat_member_count}\n')
if combat_member_count:
    errors.append('prepared combat pipeline member remains')

processor_count = texts['processors_h'].count('public UMassProcessor')
lines.append(f'\n## Processor surface\nUMassProcessor count: {processor_count}\n')
if processor_count != 2:
    errors.append(f'expected 2 UMassProcessor classes, found {processor_count}')

required = {
    'synchronous bootstrap graph': ('pipeline_cpp', 'FCrowdDemoBootstrapSynchronousGraph'),
    'bootstrap begin': ('pipeline_cpp', 'BeginWorkerBootstrapPreparation('),
    'bootstrap submit implementation': ('pipeline_cpp', 'SubmitPreparedWorkerBootstrapInput()'),
    'bootstrap submit call': ('processors_cpp', 'SubmitPreparedWorkerBootstrapInput()'),
    'production intent submit': ('pipeline_cpp', 'TrySubmitFullWorkerProductionIntent()'),
    'worker intent API': ('pipeline_cpp', 'FCrowdDemoWorkerInputSync::SubmitIntentBatch('),
    'owner barrier': ('processors_cpp', 'FCrowdWorkerResultOwnerCommitBarrier::Commit('),
    'dirty mass apply': ('processors_cpp', 'ApplyValidatedWorkerMassDirtyPlan('),
    'dirty mass marker': ('processors_cpp', 'MarkCurrentStepWorkerDirtyMassApplied('),
    'checkpoint helper': ('processors_cpp', 'ExecuteRoundCheckpointPublisher(EntityManager, Context)'),
    'one-shot movement stage': ('processors_cpp', 'StageBoundaryMovementWork(MoveTemp(WorkInput))'),
    'one-shot particle stage': ('processors_cpp', 'StageBoundaryParticleWork(MoveTemp(ParticlePipelineInput))'),
    'facing bootstrap dispatch': ('processors_cpp', 'DispatchBoundarySoftPressureWorkGraph('),
    'current architecture test': ('tests', 'FCrowdDemoPersistentWorkerProductionStructureTest'),
}
lines.append('\n## Required current path\n')
for name, (key, needle) in required.items():
    count = texts[key].count(needle)
    lines.append(f'{name}: {count}\n')
    if count == 0:
        errors.append(f'missing required current path: {name}')

# Order check in the live server advance function.
proc = texts['processors_cpp']
advance = proc.find('bool AdvanceRoundWorkerFrame(')
owner = proc.find('FCrowdWorkerResultOwnerCommitBarrier::Commit(', advance)
checkpoint = proc.find('ExecuteRoundCheckpointPublisher(EntityManager, Context)', owner)
if advance < 0 or owner < 0 or checkpoint < 0 or not (advance < owner < checkpoint):
    errors.append('owner barrier/checkpoint order invalid')
lines.append(f'owner barrier/checkpoint order: advance={advance} owner={owner} checkpoint={checkpoint}\n')

# Basic lexical balance. This is deliberately a static sanity gate, not a C++ compiler.
def balance(path, text):
    stack=[]
    pairs={')':'(',']':'[','}':'{'}
    in_str=in_char=escape=line_comment=block_comment=False
    line=1
    for i,c in enumerate(text):
        n=text[i+1] if i+1 < len(text) else ''
        if c=='\n': line += 1
        if line_comment:
            if c=='\n': line_comment=False
            continue
        if block_comment:
            if c=='*' and n=='/': block_comment=False
            continue
        if in_str:
            if escape: escape=False
            elif c=='\\': escape=True
            elif c=='"': in_str=False
            continue
        if in_char:
            if escape: escape=False
            elif c=='\\': escape=True
            elif c=="'": in_char=False
            continue
        if c=='/' and n=='/': line_comment=True; continue
        if c=='/' and n=='*': block_comment=True; continue
        if c=='"': in_str=True; continue
        if c=="'": in_char=True; continue
        if c in '([{': stack.append((c,line))
        elif c in ')]}':
            if not stack or stack[-1][0] != pairs[c]:
                return False, f'unmatched {c} at line {line}'
            stack.pop()
    if in_str or in_char or block_comment:
        return False, 'unterminated string/char/block-comment state'
    if stack:
        return False, f'unclosed {stack[-1]}'
    return True, 'ok'

lines.append('\n## Lexical balance sanity\n')
for key in ['pipeline_h','pipeline_cpp','processors_h','processors_cpp','tests']:
    ok,msg=balance(FILES[key], texts[key])
    lines.append(f'{FILES[key]}: {msg}\n')
    if not ok: errors.append(f'{FILES[key]}: {msg}')

lines.append('\n## Result\n')
if errors:
    lines.append('FAIL\n')
    for e in errors: lines.append(f'- {e}\n')
else:
    lines.append('PASS\n')
REPORT.write_text(''.join(lines), encoding='utf-8')
print(''.join(lines))
raise SystemExit(1 if errors else 0)
