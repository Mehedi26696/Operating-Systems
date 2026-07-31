#ifndef _PRODUCERCONSUMER_DRIVER_H_
#define _PRODUCERCONSUMER_DRIVER_H_

#define BUFFER_SIZE 8
#define NPRODUCERS 2
#define NCONSUMERS 5
#define ITEMS_PER_PRODUCER 32

struct pc_data {
	unsigned producer;
	unsigned item;
};

void producerconsumer_startup(void);
void producerconsumer_shutdown(void);
void producer_produce(struct pc_data data);
struct pc_data consumer_consume(void);
int run_producerconsumer(int nargs, char **args);

#endif /* _PRODUCERCONSUMER_DRIVER_H_ */