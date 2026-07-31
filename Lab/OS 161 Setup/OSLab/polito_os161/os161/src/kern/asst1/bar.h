#ifndef _ASST1_BAR_H_
#define _ASST1_BAR_H_

#define BAR_MAX_INGREDIENTS 3
#define BAR_NBOTTLES 10
#define BAR_NO_INGREDIENT 0

struct bar_glass {
	unsigned contents[BAR_MAX_INGREDIENTS];
};

struct bar_order {
	bool valid;
	unsigned requested[BAR_MAX_INGREDIENTS];
	struct bar_glass glass;
	struct semaphore *served;
};

void bar_open(void);
void bar_close(void);
void bar_place_order(struct bar_order *order);
struct bar_order *bar_get_order(void);
void bar_finish_order(struct bar_order *order);
void mix(struct bar_order *order);

#endif /* _ASST1_BAR_H_ */