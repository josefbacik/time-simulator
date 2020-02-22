#ifndef _TIME_SIMULATOR_H
#define _TIME_SIMULATOR_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/list.h>
#include <kernel/rbtree_augmented.h>

#define NSEC_PER_SEC 1000000000

struct time_simulator {
	uint64_t time;
	struct rb_root entities;
	struct list_head resched;
};

struct entity {
	uint64_t wake_time;
	struct rb_node n;
	struct entity_ops *ops;
	struct list_head list;
};

struct entity_ops {
	void (*run)(struct time_simulator *s, struct entity *e);
};

struct time_simulator *time_simulator_alloc(void);
void time_simulator_run(struct time_simulator *s, uint64_t time);

struct entity *entity_alloc(void);
void entity_enqueue(struct time_simulator *s, struct entity *e, uint64_t delta);

#endif /* _TIME_SIMULATOR_H */
