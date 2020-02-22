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
	e->wake_time = s->time + delta;
	tree_insert(s, e);
}

struct time_simulator *time_simulator_alloc(void)
{
	struct time_simulator *s = calloc(1, sizeof(struct time_simulator));
	if (!s)
		return NULL;
	s->entities = RB_ROOT;
	return s;
}

struct entity *entity_alloc(void)
{
	struct entity *e = calloc(1, sizeof(struct entity));
	if (!e)
		return NULL;
	RB_CLEAR_NODE(&e->n);
	INIT_LIST_HEAD(&e->list);
	return e;
}

static void run_entities(struct time_simulator *s)
{
	struct rb_node *n;
	struct entity *e, *tmp;

	while ((n = rb_first(&s->entities))) {
		rb_erase(n, &s->entities);
		e = rb_entry(n, struct entity, n);
		e->ops->run(s, e);
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

	time += s->time;
	while (time <= s->time) {
		run_entities(s);
		n = rb_first(&s->entities);
		if (!n)
			break;
		e = rb_entry(n, struct entity, n);

		/* Just jump to the next wake up event. */
		if (e->wake_time > s->time)
			s->time = e->wake_time;
	}
}
