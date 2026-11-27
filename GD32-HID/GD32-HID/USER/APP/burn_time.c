#include "burn_time.h"
#include "main.h"
#include <string.h>

static burn_time_result_t current[2], last[2];
static uint64_t changed[2], clock_cycles, probe_started, commit_started;
static burn_stage_t stage[2];
static uint8_t active[2];
static volatile uint32_t revision;

static uint64_t now_locked(void)
{
    uint32_t raw = DWT->CYCCNT;
    clock_cycles += (uint32_t)(raw - (uint32_t)clock_cycles);
    return clock_cycles;
}

void burn_time_sample(void)
{
    taskENTER_CRITICAL();
    now_locked();
    taskEXIT_CRITICAL();
}

void burn_time_probe_begin(void)
{
    taskENTER_CRITICAL();
    probe_started = now_locked();
    taskEXIT_CRITICAL();
}

void burn_time_probe_end(uint8_t ch, uint8_t found)
{
    uint8_t peer = ch ^ 1U;
    if(found) return;
    taskENTER_CRITICAL();
    if(active[peer] && stage[peer] == BT_SWD_LOCK) {
        /* The waiter cannot acquire SWD until this probe releases the mutex. */
        uint64_t start = changed[peer] > probe_started ? changed[peer] : probe_started;
        current[peer].miss_wait += now_locked() - start;
    }
    taskEXIT_CRITICAL();
}

void burn_time_commit_begin(void)
{
    taskENTER_CRITICAL();
    commit_started = now_locked();
    taskEXIT_CRITICAL();
}

void burn_time_commit_end(uint8_t ch)
{
    uint8_t peer = ch ^ 1U;
    uint64_t end;
    taskENTER_CRITICAL();
    end = now_locked();
    current[ch].commit_hold += end - commit_started;
    if(active[peer] && stage[peer] == BT_FS_LOCK) {
        /* The peer's acquire cannot finish before the committing socket gives back FS. */
        uint64_t start = changed[peer] > commit_started ? changed[peer] : commit_started;
        current[peer].commit_wait += end - start;
    }
    taskEXIT_CRITICAL();
}

void burn_time_begin(uint8_t ch, uint8_t dual)
{
    taskENTER_CRITICAL();
    memset(&current[ch], 0, sizeof(current[ch]));
    current[ch].dual = dual;
    current[ch].started = changed[ch] = now_locked();
    stage[ch] = BT_OTHER;
    active[ch] = 1;
    taskEXIT_CRITICAL();
}

burn_stage_t burn_time_stage(uint8_t ch, burn_stage_t next)
{
    burn_stage_t previous;
    taskENTER_CRITICAL();
    previous = stage[ch];
    if(active[ch]) {
        uint64_t now = now_locked();
        current[ch].cycles[previous] += now - changed[ch];
        changed[ch] = now;
        stage[ch] = next;
    }
    taskEXIT_CRITICAL();
    return previous;
}

void burn_time_end(uint8_t ch, uint8_t ok)
{
    taskENTER_CRITICAL();
    current[ch].ended = now_locked();
    current[ch].cycles[stage[ch]] += current[ch].ended - changed[ch];
    current[ch].valid = 1;
    current[ch].ok = ok;
    active[ch] = 0;
    last[ch] = current[ch];
    ++revision;
    taskEXIT_CRITICAL();
}

void burn_time_rearmed(uint8_t ch)
{
    taskENTER_CRITICAL();
    if(last[ch].valid && !last[ch].ready) {
        last[ch].ready = now_locked();
        ++revision;
    }
    taskEXIT_CRITICAL();
}

uint32_t burn_time_revision(void) { return revision; }

uint32_t burn_time_snapshot(burn_time_result_t result[2])
{
    uint32_t version;
    taskENTER_CRITICAL();
    memcpy(result, last, sizeof(last));
    version = revision;
    taskEXIT_CRITICAL();
    return version;
}

uint32_t burn_time_ms(uint64_t cycles)
{
    return (uint32_t)(cycles / (SystemCoreClock / 1000U));
}

uint32_t burn_time_pair_ms(const burn_time_result_t result[2])
{
    const burn_time_result_t *a = &result[0], *b = &result[1];
    if(!a->ok || !b->ok || a->ended <= b->started || b->ended <= a->started) return 0;
    return burn_time_ms((a->ended > b->ended ? a->ended : b->ended) -
                        (a->started < b->started ? a->started : b->started));
}
