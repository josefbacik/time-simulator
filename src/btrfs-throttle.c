#include <time-simulator.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_RUNTIME NSEC_PER_USEC
#define MAX_RUNTIME (NSEC_PER_SEC >> 1)

struct fs_state {
	uint64_t num_entries;
	uint64_t avg_time_per_run;
	uint64_t min_refs;
	uint64_t max_refs;
	uint64_t run_period;
	uint64_t entity_throttle_time;
	uint64_t entity_ops;
	uint64_t refs_seq;
	bool transaction_locked;
	bool async_running;
};

struct normal_entity {
	struct entity e;
	uint64_t throttled_time;
	int state;

	uint64_t nr_to_flush;
	uint64_t flush_time;
	uint64_t flushed;

	struct list_head s;
	struct list_head l;
};

static LIST_HEAD(entities);
static LIST_HEAD(sleepers);
static struct fs_state state;
static struct normal_entity trans_commit_entity;
static struct normal_entity async_worker;

static struct normal_entity *alloc_entity(void)
{
	struct normal_entity *n = calloc(1, sizeof(struct normal_entity));
	entity_init(&n->e);
	INIT_LIST_HEAD(&n->l);
	INIT_LIST_HEAD(&n->s);
	list_add_tail(&n->l, &entities);
	return n;
}

static void free_entities(void)
{
	while (!list_empty(&entities)) {
		struct normal_entity *n =
			list_first_entry(&entities, struct normal_entity, l);
		list_del_init(&n->l);
		free(n);
	}
}

static void enqueue_sleeping_tasks(struct time_simulator *s)
{
	bool woken = false;

	while (!list_empty(&sleepers)) {
		struct normal_entity *n =
			list_first_entry(&sleepers, struct normal_entity, s);
		list_del_init(&n->l);
		entity_enqueue(s, &n->e, 1);
		woken = true;
	}

	if (woken)
		entity_enqueue(s, &trans_commit_entity.e,
			       (uint64_t)NSEC_PER_SEC * 30);
}

static void wakeup_refs_waiters(struct time_simulator *sim)
{
	struct normal_entity *n, *tmp;

	list_for_each_entry_safe(n, tmp, &sleepers, s) {
		if (n->nr_to_flush == state.refs_seq) {
			list_del_init(&n->s);
			state.entity_throttle_time += sim->time - n->flush_time;
			if (!state.transaction_locked)
				entity_enqueue(sim, &n->e, state.run_period);
		}
	}
}

static bool need_flush(bool throttle)
{
	uint64_t time = state.num_entries * state.avg_time_per_run;

	if (time >= NSEC_PER_SEC)
		return true;
	if (!throttle)
		return false;
	return (time >= (NSEC_PER_SEC >> 1));
}

static int do_flushing(struct time_simulator *s, struct normal_entity *n)
{
	uint64_t time;

	if (!state.num_entries || !n->nr_to_flush) {
		n->nr_to_flush = 0;
		return 1;
	}

	time = random() % MAX_RUNTIME;
	if (time < MIN_RUNTIME)
		time += MIN_RUNTIME;
	state.num_entries--;
	n->nr_to_flush--;

	n->throttled_time += time;
	n->flush_time += time;
	n->flushed++;
	state.refs_seq++;

	wakeup_refs_waiters(s);
	entity_enqueue(s, &n->e, time);
	return 0;
}

static void calc_avg_time(uint64_t time, uint64_t nr)
{
	uint64_t avg;

	avg = state.avg_time_per_run * 3 + time;
	avg /= 4;
	state.avg_time_per_run = avg;
}

static void transaction_run(struct time_simulator *s, struct entity *e)
{
	struct normal_entity *n = container_of(e, struct normal_entity, e);

	if (n->state == 0) {
		n->nr_to_flush = state.num_entries;
		n->flush_time = 0;
		n->flushed = 0;
		n->state++;
	}

	if (n->state == 1 && do_flushing(s, n)) {
		calc_avg_time(n->flush_time, n->flushed);
		if (state.transaction_locked) {
			enqueue_sleeping_tasks(s);
			return;
		}
		state.transaction_locked = true;
		n->nr_to_flush = UINT64_MAX;
		entity_enqueue(s, e, 1);
	}
}

static void async_flusher_run(struct time_simulator *s, struct entity *e)
{
	struct normal_entity *n = container_of(e, struct normal_entity, e);

	if (n->state == 0) {
		if (state.transaction_locked) {
			state.async_running = false;
			return;
		}
		if (!need_flush(true)) {
			state.async_running = false;
			return;
		}
		n->nr_to_flush = state.num_entries >> 2;
		n->flush_time = 0;
		n->flushed = 0;
		n->state++;
	}

	if (n->state == 1 && do_flushing(s, n)) {
		calc_avg_time(n->flush_time, n->flushed);
		n->state = 0;
		entity_enqueue(s, e, 1);
	}
}

static uint64_t nr_refs(void)
{
	uint64_t refs = (random() % state.max_refs);
	if (refs < state.min_refs)
		refs += state.min_refs;
	if (refs > state.max_refs)
		refs += state.max_refs;
	return refs;
}

static void nothrottle_run(struct time_simulator *s, struct entity *e)
{
	uint64_t refs = nr_refs();
	state.num_entries += refs;
	state.entity_ops++;
	if (!state.transaction_locked)
		entity_enqueue(s, e, state.run_period);
}

static void async_nothrottle_run(struct time_simulator *s, struct entity *e)
{
	uint64_t refs = nr_refs();
	state.num_entries += refs;
	state.entity_ops++;
	if (!state.transaction_locked)
		entity_enqueue(s, e, state.run_period);
	if (!state.async_running && need_flush(false)) {
		state.async_running = true;
		entity_enqueue(s, &async_worker.e, 1);
	}
}

static void throttle_run(struct time_simulator *s, struct entity *e)
{
	struct normal_entity *n = container_of(e, struct normal_entity, e);
	uint64_t refs = nr_refs();
	state.num_entries += refs;
	state.entity_ops++;

	if (state.transaction_locked)
		return;
	if (need_flush(false)) {
		if (!state.async_running) {
			state.async_running = true;
			entity_enqueue(s, &async_worker.e, 1);
		}
		n->flush_time = s->time;
		n->nr_to_flush = state.refs_seq + refs;
		list_add_tail(&n->s, &sleepers);
	} else {
		entity_enqueue(s, e, state.run_period);
	}
}

static void init_state(struct time_simulator *s)
{
	memset(&state, 0, sizeof(state));
	state.min_refs = 2;
	state.max_refs = 2;
	state.run_period = NSEC_PER_SEC >> 4;
	state.avg_time_per_run = NSEC_PER_SEC >> 4;

	memset(&trans_commit_entity, 0, sizeof(trans_commit_entity));
	entity_init(&trans_commit_entity.e);
	trans_commit_entity.e.run = transaction_run;

	entity_enqueue(s, &trans_commit_entity.e, (uint64_t)NSEC_PER_SEC * 30);
}

static void init_async_worker(void)
{
	memset(&async_worker, 0, sizeof(async_worker));
	entity_init(&async_worker.e);
	async_worker.e.run = async_flusher_run;
}

static void nothrottle_test(struct time_simulator *s, int nr_workers)
{
	int i;

	init_state(s);
	for (i = 0; i < nr_workers; i++) {
		struct normal_entity *n = alloc_entity();
		if (!n) {
			fprintf(stderr, "Could only allocate %d workers\n", i);
			break;
		}
		n->e.run = nothrottle_run;
		entity_enqueue(s, &n->e, 0);
	}

	printf("starting no throttle run %d workers\n", nr_workers);
	time_simulator_run(s, 0);
	printf("Transaction took %llu nanoseconds (%llu seconds) to run\n",
	       (unsigned long long)trans_commit_entity.throttled_time,
	       (unsigned long long)(trans_commit_entity.throttled_time /
				    NSEC_PER_SEC));
	printf("Entities did %f ops per second\n",
	       (double)state.entity_ops / (s->time / NSEC_PER_SEC));
	printf("Final average time %llu\n",
	       (unsigned long long)state.avg_time_per_run);
	printf("Total time %lluns (%llus)\n", (unsigned long long)s->time,
	       (unsigned long long)(s->time / NSEC_PER_SEC));
	time_simulator_clear(s);
}

static void async_test(struct time_simulator *s, int nr_workers)
{
	int i;

	init_state(s);
	init_async_worker();
	for (i = 0; i < nr_workers; i++) {
		struct normal_entity *n = alloc_entity();
		if (!n) {
			fprintf(stderr, "Could only allocate %d workers\n", i);
			break;
		}
		n->e.run = async_nothrottle_run;
		entity_enqueue(s, &n->e, 0);
	}

	printf("starting async no throttle run %d workers\n", nr_workers);
	time_simulator_run(s, 0);
	printf("async flusher took %llu nanoseconds (%llu seconds) to run\n",
	       (unsigned long long)async_worker.throttled_time,
	       (unsigned long long)(async_worker.throttled_time /
				    NSEC_PER_SEC));
	printf("Entities did %f ops per second\n",
	       (double)state.entity_ops / (s->time / NSEC_PER_SEC));
	printf("Transaction took %llu nanoseconds (%llu seconds) to run\n",
	       (unsigned long long)trans_commit_entity.throttled_time,
	       (unsigned long long)(trans_commit_entity.throttled_time /
				    NSEC_PER_SEC));
	printf("Final average time %llu\n",
	       (unsigned long long)state.avg_time_per_run);
	printf("Total time %lluns (%llus)\n", (unsigned long long)s->time,
	       (unsigned long long)(s->time / NSEC_PER_SEC));
	time_simulator_clear(s);
}

static void throttle_test(struct time_simulator *s, int nr_workers)
{
	int i;

	init_state(s);
	init_async_worker();
	for (i = 0; i < nr_workers; i++) {
		struct normal_entity *n = alloc_entity();
		if (!n) {
			fprintf(stderr, "Could only allocate %d workers\n", i);
			break;
		}
		n->e.run = throttle_run;
		entity_enqueue(s, &n->e, 0);
	}

	printf("starting throttle run %d workers\n", nr_workers);
	time_simulator_run(s, 0);
	printf("async flusher took %llu nanoseconds (%llu seconds) to run\n",
	       (unsigned long long)async_worker.throttled_time,
	       (unsigned long long)(async_worker.throttled_time /
				    NSEC_PER_SEC));
	printf("Transaction took %llu nanoseconds (%llu seconds) to run\n",
	       (unsigned long long)trans_commit_entity.throttled_time,
	       (unsigned long long)(trans_commit_entity.throttled_time /
				    NSEC_PER_SEC));
	printf("Entities did %f ops per second\n",
	       (double)state.entity_ops / (s->time / NSEC_PER_SEC));
	printf("Entities were throttled %llu nanoseconds (%llu seconds)\n",
	       (unsigned long long)state.entity_throttle_time,
	       (unsigned long long)state.entity_throttle_time / NSEC_PER_SEC);
	printf("Final average time %llu\n",
	       (unsigned long long)state.avg_time_per_run);
	printf("Total time %lluns (%llus)\n", (unsigned long long)s->time,
	       (unsigned long long)(s->time / NSEC_PER_SEC));
	time_simulator_clear(s);
}

int main(int argc, char **argv)
{
	struct time_simulator *s;

	srandom(1);

	s = time_simulator_alloc();
	if (!s) {
		perror("Error allocating time simulator\n");
		return -1;
	}

	nothrottle_test(s, 1);
	printf("\n");
	nothrottle_test(s, 10);
	printf("\n");
	async_test(s, 1);
	printf("\n");
	async_test(s, 10);
	printf("\n");
	throttle_test(s, 1);
	printf("\n");
	throttle_test(s, 10);
	free_entities();
	free(s);
	return 0;
}
