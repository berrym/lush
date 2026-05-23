/**
 * @file secure_memory.h
 * @brief Portable secure memory operations
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides portable implementations of:
 * - Secure memory wiping (prevents compiler optimization)
 * - Memory locking (prevents swapping to disk)
 *
 * Implementation: Spec 15 Minimal Secure Mode
 * Based on: MLOCK_PORTABILITY_ANALYSIS.md
 */

#ifndef LLE_SECURE_MEMORY_H
#define LLE_SECURE_MEMORY_H

#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * PLATFORM DETECTION
 * ============================================================================
 */

/* Detect mlock availability (POSIX systems) */
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) ||        \
    defined(__OpenBSD__) || defined(__NetBSD__) || defined(__sun) ||           \
    defined(_AIX) || defined(__hpux)
#define LLE_HAVE_MLOCK 1
#include <sys/mman.h>
#else
#define LLE_HAVE_MLOCK 0
#endif

/* Detect explicit_bzero availability */
/* Note: macOS does NOT have explicit_bzero - it uses memset_s instead,
 * but memset_s requires __STDC_WANT_LIB_EXT1__ before any headers.
 * We use the volatile pointer fallback on macOS which is equally safe. */
#if defined(__OpenBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 11) ||     \
    (defined(__GLIBC__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 25)
#define LLE_HAVE_EXPLICIT_BZERO 1
#if defined(__OpenBSD__) || defined(__FreeBSD__)
#include <strings.h> /* BSD location */
#else
#include <string.h> /* glibc location */
#endif
/* Declare explicit_bzero if not already declared */
#ifndef explicit_bzero
extern void explicit_bzero(void *, size_t);
#endif
#else
#define LLE_HAVE_EXPLICIT_BZERO 0
#endif

/* ============================================================================
 * SECURE MEMORY WIPE
 * ============================================================================
 */

/**
 * @brief Securely wipe memory contents
 *
 * Guarantees that the memory wipe will not be optimized away by the compiler.
 * Uses explicit_bzero() if available, falls back to volatile pointer trick.
 *
 * @param ptr Pointer to memory to wipe
 * @param len Length in bytes
 */
static inline void lle_secure_wipe(void *ptr, size_t len) {
    if (!ptr || len == 0) {
        return;
    }

#if LLE_HAVE_EXPLICIT_BZERO
    /* Use explicit_bzero if available - guaranteed not to be optimized away */
    explicit_bzero(ptr, len);
#else
    /* Portable fallback: volatile pointer prevents compiler optimization */
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) {
        *p++ = 0;
    }
#endif
}

/* ============================================================================
 * MEMORY LOCKING (PREVENT SWAPPING)
 * ============================================================================
 */

/**
 * @brief Lock memory to prevent swapping to disk
 *
 * Uses mlock() on POSIX systems. On systems without mlock or when
 * mlock fails (insufficient privileges), returns false but this is
 * not a fatal error - the buffer can still be used, just without
 * the anti-swap protection.
 *
 * @param addr Address of memory to lock
 * @param len Length in bytes
 * @return true if locked successfully, false otherwise
 */
static inline bool lle_memory_lock(void *addr, size_t len) {
    if (!addr || len == 0) {
        return false;
    }

#if LLE_HAVE_MLOCK
    /* Attempt to lock - may fail due to:
     * - Insufficient privileges (need CAP_IPC_LOCK on Linux)
     * - Exceeding RLIMIT_MEMLOCK
     * - System doesn't support mlock
     */
    return mlock(addr, len) == 0;
#else
    /* Platform doesn't have mlock - not an error, just unavailable */
    (void)addr;
    (void)len;
    return false;
#endif
}

/**
 * @brief Unlock previously locked memory
 *
 * Uses munlock() on POSIX systems. Should be called when memory
 * no longer needs to be protected from swapping.
 *
 * @param addr Address of memory to unlock
 * @param len Length in bytes
 * @return true if unlocked successfully, false otherwise
 */
static inline bool lle_memory_unlock(void *addr, size_t len) {
    if (!addr || len == 0) {
        return true; /* Nothing to unlock */
    }

#if LLE_HAVE_MLOCK
    return munlock(addr, len) == 0;
#else
    /* Platform doesn't have munlock - no-op, return success */
    (void)addr;
    (void)len;
    return true;
#endif
}

#endif /* LLE_SECURE_MEMORY_H */
