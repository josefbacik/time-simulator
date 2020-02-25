#include <errno.h>
#include <time-simulator.h>
#include <stdlib.h>

static void tree_insert(struct time_simulator *s, struct entity *e)
{
	struct rb_node **p = &s->entities.rb_node;
	struct rb_node *parent = NULL;
	struct entity *parent_entry;

	while (*p) {
		parent = *p;
		parent_entry = rb_entry(parent, struct entity, n);
		if (e->wake_time <= parent_entry->wake_time)
			p = &parent->rb_left;
		else
			p = &parent->rb_right;
	}

	rb_link_node(&e->n, parent, p);
	rb_insert_color(&e->n, &s->entities);
}

void entity_enqueue(struct time_simulator *s, struct entity *e, uint64_t delta)
{
	e->state = ENTITY_RUNNING;
	e->wake_time = s->time + delta;
	e->start_time = s->time;
	if (!s->running || delta)
		tree_insert(s, e);
	else
		list_add_tail(&e->list, &s->resched);
}

void entity_sleep(struct time_simulator *s, struct entity *e)
{
	e->state = ENTITY_SLEEPING;
	e->start_time = s->time;
	list_add_tail(&e->list, &s->sleepers);
}

void time_simulator_wake(struct time_simulator *s,
			 uint64_t (*wake)(struct time_simulator *s,
					  struct entity *e))
{
	struct entity *e, *tmp;

	list_for_each_entry_safe(e, tmp, &s->sleepers, list) {
		uint64_t wake_time = wake(s, e);
		if (wake_time == UINT64_MAX)
			continue;
		e->sleep_time += s->time - e->start_time;
		list_del_init(&e->list);
		entity_enqueue(s, e, wake_time);
	}
}

struct time_simulator *time_simulator_alloc(void)
{
	struct time_simulator *s = calloc(1, sizeof(struct time_simulator));
	if (!s)
		return NULL;
	s->entities = RB_ROOT;
	INIT_LIST_HEAD(&s->resched);
	INIT_LIST_HEAD(&s->sleepers);
	return s;
}

void entity_init(struct entity *e)
{
	RB_CLEAR_NODE(&e->n);
	INIT_LIST_HEAD(&e->list);
}

void time_simulator_clear(struct time_simulator *s)
{
	struct rb_node *n;

	while ((n = rb_first(&s->entities)))
		rb_erase(n, &s->entities);

	while (!list_empty(&s->resched))
		list_del_init(s->resched.next);
	while (!list_empty(&s->sleepers))
		list_del_init(s->sleepers.next);
	s->time = 0;
}

static void run_entities(struct time_simulator *s)
{
	struct rb_node *n;
	struct entity *e, *tmp;

	while ((n = rb_first(&s->entities))) {
		e = rb_entry(n, struct entity, n);
		if (e->wake_time > s->time)
			break;
		rb_erase(n, &s->entities);
		e->run(s, e);
	}

	list_for_each_entry_safe(e, tmp, &s->resched, list) {
		list_del_init(&e->list);
		tree_insert(s, e);
	}
}

void time_simulator_run(struct time_simulator *s, uint64_t time)
{
	struct rb_node *n;
	struct entity *e;

	if (time)
		time += s->time;
	s->running = true;
	while (!time || (s->time <= time)) {
		run_entities(s);
		n = rb_first(&s->entities);
		if (!n)
			break;
		e = rb_entry(n, struct entity, n);

		/* Just jump to the next wake up event. */
		if (e->wake_time > s->time)
			s->time = e->wake_time;
	}
	s->running = false;
}
