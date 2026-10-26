// Linux-only reservation lifecycle tests: real mmap collisions, rollback, and
// table exhaustion exercised against the shipped guest_maps implementation.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <sys/mman.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#include "port/guest_layout.h"

static void testReserveLifecycleRepeatedAndRelease(void) {
    guest_layout_release_slices();
    GuestLayoutAudit first;
    assert(guest_layout_reserve_slices_nondestructive(&first) == 1);
    assert(first.fullyAvailable == 1);

    // A second call must see the process's own PROT_NONE slices as belonging
    // to this process, not re-map or forget them. The launcher relies on this
    // re-audit as its post-reservation verification and logs the report.
    GuestLayoutAudit second;
    assert(guest_layout_reserve_slices_nondestructive(&second) == 1);
    assert(second.fullyAvailable == 1);
    for (int i = 0; i < GUEST_REGION_COUNT; i++) {
        assert(second.regions[i].status == GUEST_RES_PRE_RESERVED);
    }
    char report[2048];
    assert(guest_layout_format_audit_report(&second, report, sizeof(report)) > 0);
    assert(strstr(report, "PRE_RESERVED") != NULL);
    assert(strstr(report, "CONFLICT") == NULL);

    guest_layout_release_slices();
    guest_layout_release_slices(); // idempotent cleanup

    GuestLayoutAudit third;
    assert(guest_layout_reserve_slices_nondestructive(&third) == 1);
    guest_layout_release_slices();
    printf("[PASS] Repeated reservation, release, and re-reservation verified\n");
}

static void testReserveRejectsOccupiedWindow(void) {
    guest_layout_release_slices();
    void* intruder = mmap((void*)(uintptr_t)0x90000000u, 0x1000u,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (intruder != (void*)(uintptr_t)0x90000000u) {
        if (intruder != MAP_FAILED) munmap(intruder, 0x1000u);
        printf("[SKIP] cannot place an occupied-window probe on this host\n");
        return;
    }

    GuestLayoutAudit audit;
    int ok = guest_layout_reserve_slices_nondestructive(&audit);
    assert(ok == 0);
    assert(audit.fullyAvailable == 0);
    assert(audit.totalConflicts >= 1);
    assert(audit.regions[GUEST_REGION_MEM2].status == GUEST_RES_CONFLICT);

    // The foreign mapping must survive the refusal.
    assert(((volatile char*)intruder)[0] == 0);
    munmap(intruder, 0x1000u);

    // A failed attempt must not leave stale bookkeeping that blocks the next.
    GuestLayoutAudit retry;
    assert(guest_layout_reserve_slices_nondestructive(&retry) == 1);
    guest_layout_release_slices();
    printf("[PASS] Occupied window is refused and the next reservation succeeds\n");
}

static void testReserveTableExhaustionRollsBackNewSlices(void) {
    guest_layout_release_slices();
    const GuestRegionDef* mem1 = guest_layout_get_region(GUEST_REGION_MEM1);
    const GuestRegionDef* mem2 = guest_layout_get_region(GUEST_REGION_MEM2);
    assert(mem1 && mem2);

    // A full-region PROT_NONE guard has the same shape as the pathless window
    // libreserve leaves when VMA naming is unavailable. It must stay untouched
    // while the reservation table fills up around it.
    void* guard = mmap((void*)mem1->base, mem1->size, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (guard != (void*)mem1->base) {
        if (guard != MAP_FAILED) munmap(guard, mem1->size);
        printf("[SKIP] cannot place a full-region guard on this host\n");
        return;
    }

    GuestLayoutAudit first;
    assert(guest_layout_reserve_slices_nondestructive(&first) == 1);

    // Drop the tracked slices behind the library's back, leaving their table
    // entries counted: the next call maps MEM1's neighbour, then exhausts the
    // table and must roll that new slice back (MEM1 itself is skipped).
    for (int i = 1; i < GUEST_REGION_COUNT; i++) {
        const GuestRegionDef* def = guest_layout_get_region((GuestRegionId)i);
        assert(def && munmap((void*)def->base, def->size) == 0);
    }

    GuestLayoutAudit second;
    assert(guest_layout_reserve_slices_nondestructive(&second) == 0);
    assert(second.fullyAvailable == 0);
    assert(second.regions[GUEST_REGION_MEM1].status == GUEST_RES_PRE_RESERVED);
    // The rollback must be reflected in the report, not left ACTIVE_MAPPED.
    assert(second.regions[GUEST_REGION_MEM2].status == GUEST_RES_UNMAPPED);

    // MEM2 was mapped and rolled back, so a fresh mapping must fit there; the
    // guard must not have been replaced by the failed attempt.
    void* probe = mmap((void*)mem2->base, 0x1000u, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(probe == (void*)mem2->base);
    munmap(probe, 0x1000u);
    assert(guard == (void*)mem1->base);

    guest_layout_release_slices();
    munmap(guard, mem1->size);

    GuestLayoutAudit fresh;
    assert(guest_layout_reserve_slices_nondestructive(&fresh) == 1);
    guest_layout_release_slices();
    printf("[PASS] Table exhaustion rolls back only the new slices\n");
}

int main(void) {
    printf("=== Running Linux guest reservation tests ===\n");
    testReserveRejectsOccupiedWindow();
    testReserveTableExhaustionRollsBackNewSlices();
    testReserveLifecycleRepeatedAndRelease();
    printf("All Linux guest reservation tests passed successfully.\n");
    return 0;
}
