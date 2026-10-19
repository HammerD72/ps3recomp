/* Internal private MFC ownership registry, including stopped-thread cleanup. */
typedef struct spu_mfc_slot {
    spu_context* ctx;
    mfc_engine mfc;
    struct spu_mfc_slot* next;
} spu_mfc_slot;

static spu_mfc_slot* s_mfc_slots;
static SRWLOCK s_mfc_claim_lock = SRWLOCK_INIT;

/* Release/range cleanup happens only after the affected SPU stops. A global
 * generation rejects TLS pointers after cross-thread release/address reuse. */
static volatile LONG64 s_mfc_generation = 1;
static SPU_THREAD_LOCAL struct {
    spu_context* ctx;
    mfc_engine* engine;
    LONG64 generation;
} s_mfc_cached;
static mfc_engine* mfc_remember(spu_context* ctx, mfc_engine* engine, LONG64 generation)
{
    s_mfc_cached.ctx = ctx;
    s_mfc_cached.engine = engine;
    s_mfc_cached.generation = generation;
    return engine;
}

static mfc_engine* mfc_for(spu_context* ctx)
{
    LONG64 generation = ReadAcquire64(&s_mfc_generation);
    if (s_mfc_cached.ctx == ctx && s_mfc_cached.generation == generation)
        return s_mfc_cached.engine;
    AcquireSRWLockShared(&s_mfc_claim_lock);
    for (spu_mfc_slot* slot = s_mfc_slots; slot; slot = slot->next) {
        if (slot->ctx == ctx) {
            ReleaseSRWLockShared(&s_mfc_claim_lock);
            return mfc_remember(ctx, &slot->mfc, generation);
        }
    }
    ReleaseSRWLockShared(&s_mfc_claim_lock);

    /* A concurrent miss may publish while the read lock is released. Recheck
     * under the writer lock before creating this context's private engine. */
    AcquireSRWLockExclusive(&s_mfc_claim_lock);
    for (spu_mfc_slot* slot = s_mfc_slots; slot; slot = slot->next) {
        if (slot->ctx == ctx) {
            ReleaseSRWLockExclusive(&s_mfc_claim_lock);
            return mfc_remember(ctx, &slot->mfc, generation);
        }
    }
    spu_mfc_slot* slot = (spu_mfc_slot*)malloc(sizeof(*slot));
    if (!slot) {
        fprintf(stderr, "[SPU] cannot allocate private MFC engine for context %p\n", (void*)ctx);
        fflush(stderr);
        abort(); /* Shared fallback state would silently corrupt DMA completion. */
    }
    slot->ctx = ctx;
    mfc_engine_init(&slot->mfc);
    slot->next = s_mfc_slots;
    s_mfc_slots = slot;
    ReleaseSRWLockExclusive(&s_mfc_claim_lock);
    return mfc_remember(ctx, &slot->mfc, generation);
}

void spu_mfc_release(spu_context* ctx)
{
    if (!ctx) return;
    AcquireSRWLockExclusive(&s_mfc_claim_lock);
    InterlockedIncrement64(&s_mfc_generation);
    for (spu_mfc_slot** link = &s_mfc_slots; *link; link = &(*link)->next) {
        if ((*link)->ctx != ctx) continue;
        spu_mfc_slot* slot = *link;
        *link = slot->next;
        free(slot);
        break;
    }
    ReleaseSRWLockExclusive(&s_mfc_claim_lock);
}

/* Exit safety net for contexts unwound off a departing thread's stack.
 * Pointer comparisons suffice: the abandoned context need not be readable. */
void spu_mfc_forget_range(uintptr_t lo, uintptr_t hi)
{
    AcquireSRWLockExclusive(&s_mfc_claim_lock);
    InterlockedIncrement64(&s_mfc_generation);
    for (spu_mfc_slot** link = &s_mfc_slots; *link;) {
        spu_mfc_slot* slot = *link;
        uintptr_t owner = (uintptr_t)slot->ctx;
        if (owner >= lo && owner < hi) {
            *link = slot->next;
            free(slot);
        } else {
            link = &slot->next;
        }
    }
    ReleaseSRWLockExclusive(&s_mfc_claim_lock);
}
