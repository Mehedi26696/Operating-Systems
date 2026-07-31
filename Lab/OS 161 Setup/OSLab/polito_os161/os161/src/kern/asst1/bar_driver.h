#ifndef _ASST1_BAR_DRIVER_H_
#define _ASST1_BAR_DRIVER_H_

#define BAR_NCUSTOMERS 10
#define BAR_NBARTENDERS 3
#define BAR_DRINKS_PER_CUSTOMER 10

void bottle_reset_stats(void);
void bottle_record_dose(unsigned bottle);
unsigned bottle_get_doses(unsigned bottle);
int runbar(int nargs, char **args);

#endif /* _ASST1_BAR_DRIVER_H_ */