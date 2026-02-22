# Critical Gaps Specifications

**Purpose**: This directory contains foundational component specifications that are **referenced by multiple core specifications** but require independent documentation due to their cross-cutting nature.

## What Are Critical Gaps?

Critical gaps are:
- ✅ **Foundational infrastructure** needed by multiple systems
- ✅ **Cross-cutting concerns** that don't fit cleanly into a single numbered spec
- ✅ **Integration points** between major systems
- ✅ **Preserved for reference** even after integration into core specs

## Why Separate from Core Specs (02-21)?

1. **Prevent deletion confusion**: Previously, specs 22-26 were deleted after "integration" but the integration was incomplete
2. **Clear integration status**: These specs are reference material for integration INTO core specs
3. **Avoid number confusion**: Not part of the main 02-21 sequence
4. **Preserve detailed algorithms**: Core specs may summarize, these provide full algorithmic detail

## Current Critical Gap Specifications

| Spec | Title | Primary Integration Target | Doc Status | Implementation | Priority |
|------|-------|----------------------------|------------|----------------|----------|
| **22** | History-Buffer Integration | Spec 09 (History System) | ✅ 100% | ✅ 100% | ✅ COMPLETE |
| **23** | Interactive Completion Menu | Spec 12 (Completion System) | ✅ 100% | ✅ 100% (Pragmatic) | ✅ COMPLETE |
| **24** | Advanced Prompt Widget Hooks | Spec 07 (Extensibility Framework) | ✅ 100% | ⚠️ 5% | 🔥 P4 |
| **25** | Default Keybindings | Spec 13 (User Customization) | ✅ 100% | ✅ 100% | ✅ COMPLETE |
| **26** | Adaptive Terminal Integration | Spec 02 (Terminal System) | ⚠️ Partial | ✅ 100% | ✅ COMPLETE |
| **27** | Fuzzy Matching Library | Multiple (autocorrect, completion, history, search) | ❌ N/A | ❌ N/A | ⏸️ Later |
| **28** | Prompt Variable & Theme Integration | Spec 25 (Prompt Theme System) | ✅ 100% | ❌ 0% | 🔥 v1.5.0 Required |

**CRITICAL DISCOVERY (2025-11-02)**: All critical gap specs were integrated into documentation but **NOT implemented in code**. This represents a severe documentation-reality mismatch that must be addressed.

**UPDATE (2025-11-02)**: 
- ✅ **Spec 22 (History-Buffer Integration)**: COMPLETE (5 phases, compliance test passing)
- ✅ **Spec 23 (Interactive Completion Menu)**: COMPLETE via pragmatic approach (~3600 lines, 189 tests, all passing)
- ✅ **Spec 25 (Default Keybindings)**: COMPLETE (4 phases: Kill Ring, Keybinding Engine, 44 Actions, compliance test with 62 assertions)
- ✅ **Spec 26 (Adaptive Terminal Integration)**: COMPLETE (88 tests, graceful fallback system)

**4 of 6 critical gap specs now complete!**

See `SPEC_12_PRAGMATIC_APPROACH.md` for details on Spec 23 implementation decision.

## Integration Process

For each critical gap specification:

1. **Audit**: Deep comparison between critical gap spec and claimed integration target
2. **Document**: Record findings in `integration_status/audit_XX_into_YY.md`
3. **Fix**: Add missing algorithms/implementations to core spec
4. **Verify**: Test that core spec implementation matches critical gap requirements
5. **Mark Complete**: Update integration status table above

## Why These Exist

### **Specs 22-26: Historical Recovery**

These were originally created as detailed specifications, then claimed to be "integrated" into core specs 02-21, then deleted. Investigation revealed:
- ❌ Integration was incomplete (e.g., Spec 26 adaptive terminal detection was missing)
- ❌ Detailed algorithms were lost
- ❌ No verification was done

**Solution**: Restored from git history and placed here for reference and verification.

### **Spec 27: Newly Identified Gap**

Fuzzy matching algorithms are needed by:
- Autocorrect system (existing code in `src/autocorrect.c`)
- LLE Completion System (Spec 12)
- LLE History Search (Spec 09)
- Any future similarity-based features

Rather than duplicate these algorithms, they belong in a shared library.

## Implementation Guidance

**DO NOT implement critical gap specs directly.**

Instead:
1. Verify integration into core specs is complete
2. Implement the **core spec** (which incorporates the critical gap functionality)
3. Use critical gap specs as **reference** for algorithms, edge cases, and requirements
4. Ensure core spec implementation satisfies critical gap requirements

## Maintenance

When updating:
- ✅ Keep critical gap specs as reference (don't delete)
- ✅ Update core specs with integrated functionality
- ✅ Document integration in `integration_status/`
- ✅ Add tests verifying critical gap requirements are met

---

**Last Updated**: 2025-10-14  
**Maintained By**: LLE Development Team
