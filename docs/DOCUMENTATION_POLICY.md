# Enforced Documentation Policy

**Status**: ACTIVE - Enforced by pre-commit hooks
**Date**: 2025-10-20
**Enforcement**: Automatic - commits will FAIL if policy violated

---

## POLICY OBJECTIVES

This policy is **ENFORCED** not suggested. The pre-commit hook system will **REJECT** any commit that violates these rules.

**Keywords**: SAFE | ORGANIZED | CLEAN | PRUNED

1. **SAFE** - Never lose useful information
2. **ORGANIZED** - Structured, navigable documentation
3. **CLEAN** - Root directory minimal, no clutter
4. **PRUNED** - Remove obsolete documents when safe

---

## ENFORCED RULES (Pre-commit Hook Checks)

### Rule 1: Root Directory Cleanliness

**Allowed in Root** (exhaustive list):
- `README.md` (project introduction)
- `LICENSE` (project license)
- `meson.build`, `meson_options.txt` (build configuration)
- `.gitignore`, `.editorconfig`, `.clang-format` (config files)
- Directories: `src/`, `include/`, `docs/`, `tests/`, `scripts/`, `examples/`, `build*/`, `audit_results/`

**PROHIBITED in Root**:
- `.o` files (build artifacts)
- `core.*` files (core dumps)
- `*.log` files (except active logs in proper directories)
- Markdown files other than README.md
- Test scripts (belong in `scripts/`)
- Temporary files

**Enforcement**: Pre-commit hook will **REJECT** commits that add prohibited files to root.

---

### Rule 2: Documentation Structure

**Required Structure**:
```
docs/
+-- DOCUMENTATION_POLICY.md        # THIS FILE
+-- VISION.md                      # Project philosophy
+-- user/                          # End-user documentation
+-- project/                       # Project-level documentation
+-- development/                   # Development docs
+-- lle_specification/             # LLE specifications
+-- lle_implementation/            # LLE implementation docs
+-- archived/                      # Historical/completed documents
```

**Enforcement**: Pre-commit hook will **WARN** if new markdown files are added outside this structure.

---

### Rule 3: Safe Archival (Manual Review Required)

**When to Archive**:
- Completion documents after phase is done and verified
- Audit documents after compliance is achieved
- Test result documents after issues are resolved

**NEVER Archive**:
- Active specifications
- Current development guides
- Unsolved issue reports

**Archive Process**:
1. Use `git mv` (preserves history)
2. Move to appropriate `docs/archived/` subdirectory
3. Update any references in active documents
4. Document the archival in commit message

---

### Rule 4: Pruning Policy (SAFE-ONLY)

**Pruning = Safe Deletion** (not archival)

**Can Be Pruned**:
- Build artifacts (.o files, core dumps)
- Duplicate documents (keep most recent, archive older)
- Temporary scratch files
- Generated documentation that can be regenerated
- Empty or near-empty placeholder files

**NEVER Prune**:
- Unique information (even if seems obsolete)
- Historical context that explains decisions
- Error reports that might recur
- Performance benchmarks (historical data valuable)

**Pruning Process**:
1. Verify content is truly duplicate/regenerable/obsolete
2. If ANY doubt, archive instead of prune
3. Document pruning in commit message with justification

**Enforcement**: Pre-commit hook will **WARN** on deletions.

---

## POLICY EVOLUTION

This policy is **evolvable** but changes require:

1. **User Approval** - No AI assistant can modify policy unilaterally
2. **Documentation** - Policy changes must be documented in this file
3. **Enforcement Update** - Pre-commit hooks must be updated to match

---

## QUICK REFERENCE

**Before Committing**:
1. Is root directory clean? (only allowed files)
2. Are new docs in correct docs/ subdirectory?
3. If files deleted, is justification in commit message?
4. If files moved, used `git mv` for traceability?

**Keywords to Remember**: **SAFE | ORGANIZED | CLEAN | PRUNED**

---

**Policy Owner**: Project Maintainer
**Enforcement**: Automated (pre-commit hooks)
**Compliance**: MANDATORY - Non-negotiable for all commits
