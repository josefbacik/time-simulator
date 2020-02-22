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
};

struct normal_entity {
	struct entity e;
	uint64_t throttled_time;
	struct list_head l;
};

static LIST_HEAD(entities);
static struct fs_state state;
static struct normal_entity trans_commit_entity;

static struct normal_entity *alloc_entity(void)
{
	struct normal_entity *n = calloc(1, sizeof(struct normal_entity));
	entity_init(&n->e);
	INIT_LIST_HEAD(&n->l);
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

static void transaction_run(struct time_simulator *s, struct entity *e)
{
	struct normal_entity *n;
	uint64_t runtime = 0;
	uint64_t i;

	n = container_of(e, struct normal_entity, e);

	for (i = 0; i < state.num_entries; i++) {
		uint64_t time = random() % MAX_RUNTIME;
		if (time < MIN_RUNTIME)
			time += MIN_RUNTIME;
		runtime += time;
	}

	printf("We ran %llu\n", (unsigned long long)state.num_entries);
	n->throttled_time += runtime;
	entity_enqueue(s, e, runtime + ((uint64_t)NSEC_PER_SEC * 30));
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
	entity_enqueue(s, e, state.run_period);
}

static void init_state(struct time_simulator *s)
{
	memset(&state, 0, sizeof(state));
	state.min_refs = 2;
	state.max_refs = 2;
	state.run_period = NSEC_PER_SEC >> 4;

	memset(&trans_commit_entity, 0, sizeof(trans_commit_entity));
	entity_init(&trans_commit_entity.e);
	trans_commit_entity.e.run = transaction_run;

	entity_enqueue(s, &trans_commit_entity.e, (uint64_t)NSEC_PER_SEC * 30);
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
	time_simulator_run(s, (uint64_t)NSEC_PER_SEC * 30);
	time_simulator_clear(s);
	printf("Transaction took %llu nanoseconds (%llu seconds) to run\n",
	       (unsigned long long)trans_commit_entity.throttled_time,
	       (unsigned long long)(trans_commit_entity.throttled_time /
				    NSEC_PER_SEC));
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
	nothrottle_test(s, 10);
	free_entities();
	free(s);
	return 0;
}
