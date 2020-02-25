#ifndef _TIME_SIMULATOR_H
#define _TIME_SIMULATOR_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/list.h>
#include <kernel/rbtree_augmented.h>

struct entity;

#define NSEC_PER_SEC 1000000000
#define NSEC_PER_USEC 1000

enum entity_state {
	ENTITY_RUNNING,
	ENTITY_SLEEPING,
};

struct time_simulator {
	uint64_t time;
	struct rb_root entities;
	struct list_head resched;
	struct list_head sleepers;
	struct list_head entity_list;
	bool running;
	void (*free_entity)(struct entity *e);
};

struct entity {
	uint64_t wake_time;
	uint64_t start_time;
	uint64_t sleep_time;
	uint64_t run_time;
	struct rb_node n;
	struct entity_ops *ops;
	struct list_head list;
	struct list_head main_list;
	enum entity_state state;
	void (*run)(struct time_simulator *s, struct entity *e);
};

struct time_simulator *
time_simulator_alloc(void (*free_entity)(struct entity *e));
void time_simulator_run(struct time_simulator *s, uint64_t time);
void time_simulator_clear(struct time_simulator *s);
void time_simulator_wake(struct time_simulator *s,
			 uint64_t (*wake)(struct time_simulator *s,
					  struct entity *e));

void entity_init(struct time_simulator *s, struct entity *e);
void entity_enqueue(struct time_simulator *s, struct entity *e, uint64_t delta);
void entity_sleep(struct time_simulator *s, struct entity *e);
#endif /* _TIME_SIMULATOR_H */
