#include <stdio.h>

#define MAX_PROCESSES 100

int main(void)
{
    int n;
    int burst[MAX_PROCESSES];
    int waiting[MAX_PROCESSES];
    int turnaround[MAX_PROCESSES];
    float totalWaiting = 0;
    float totalTurnaround = 0;

    printf("Enter number of processes: ");
    scanf("%d", &n);

    if (n <= 0 || n > MAX_PROCESSES) {
        printf("Invalid number of processes.\n");
        return 1;
    }

    printf("Enter burst times:\n");
    for (int i = 0; i < n; i++) {
        printf("P%d: ", i + 1);
        scanf("%d", &burst[i]);

        if (burst[i] <= 0) {
            printf("Burst time must be positive.\n");
            return 1;
        }
    }

 
    waiting[0] = 0;

    for (int i = 1; i < n; i++) {
        waiting[i] = waiting[i - 1] + burst[i - 1];
    }

    for (int i = 0; i < n; i++) {
        turnaround[i] = waiting[i] + burst[i];
        totalWaiting += waiting[i];
        totalTurnaround += turnaround[i];
    }

    printf("\nProcess\tBurst\tWaiting\tTurnaround\n");
    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t%d\t%d\n",
               i + 1, burst[i], waiting[i], turnaround[i]);
    }

    printf("\nAverage waiting time: %.2f\n", totalWaiting / n);
    printf("Average turnaround time: %.2f\n", totalTurnaround / n);

    return 0;
}
