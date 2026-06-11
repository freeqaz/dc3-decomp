-- Wave-9 DB hygiene: exclude the wave-8 lane-A drift rows that inflate the
-- authorable backlog (99g-WAVE-8-RESULTS.md "DB-DRIFT cleanup surfaced").
-- Idempotent; run by the orchestrator (single writer) at the apply step.

-- (1) 27 link_glue zero-start rows ABSENT from report.json (db drift: ObjPtrList
--     template glue the report plane no longer carries).
UPDATE functions SET excluded = 1,
  exclusion_reason = 'db-drift: link_glue zero-start absent from report.json (wave-8 lane A, 27/30; excluded wave-9)'
WHERE unit LIKE '%link_glue%' AND excluded = 0 AND is_stub = 0
  AND (current_percent IS NULL OR current_percent <= 1)
  AND symbol NOT IN ('?EaseLinear@@YAMMMM@Z', '?Flush@HDCache@@AAAXXZ', 'asinf');

-- (2) 3 link_glue rows present in report but unpairable (fuzzy=None, size 4-36
--     branch-island/thunk glue; wave-8 lane A "reverse artifacts").
UPDATE functions SET excluded = 1,
  exclusion_reason = 'target link-glue thunk/branch-island (size<=36, fuzzy=None unpairable); not source-authorable (wave-8 lane A reverse-artifact; excluded wave-9)'
WHERE unit LIKE '%link_glue%' AND excluded = 0 AND is_stub = 0
  AND (current_percent IS NULL OR current_percent <= 1)
  AND symbol IN ('?EaseLinear@@YAMMMM@Z', '?Flush@HDCache@@AAAXXZ', 'asinf');

-- (3) Matrix3 Multiply mis-attribution: authored in src/system/math/mtx.cpp; the
--     target instance was COMDAT-placed in CharLookAt.obj by the original link, so
--     per-unit pairing can never score it (report fuzzy=None). Stale 28.85%
--     current_percent is from an old cross-unit diff attempt.
UPDATE functions SET excluded = 1,
  exclusion_reason = 'cross-unit COMDAT placement: authored in math/mtx.cpp, target instance in CharLookAt.obj; unpairable per-unit (wave-8 lane A; excluded wave-9)'
WHERE symbol = '?Multiply@@YAXABVMatrix3@Hmx@@0AAV12@@Z'
  AND unit = 'default/system/char/CharLookAt' AND excluded = 0;
