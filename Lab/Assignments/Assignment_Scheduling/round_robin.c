#include <stdio.h>

#define MAX_PROCESSES 100

int main(void)
{
    int n, quantum;
    int burst[MAX_PROCESSES];
    int remaining[MAX_PROCESSES];
    int completion[MAX_PROCESSES] = {0};
    int waiting[MAX_PROCESSES];
    int turnaround[MAX_PROCESSES];
    int currentTime = 0;
    int completed = 0;
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

        remaining[i] = burst[i];
    }

    printf("Enter time quantum: ");
    scanf("%d", &quantum);

    if (quantum <= 0) {
        printf("Time quantum must be positive.\n");
        return 1;
    }

    printf("\nExecution order:\n");

    while (completed < n) {
        for (int i = 0; i < n; i++) {
            if (remaining[i] > 0) {
                int startTime = currentTime;

                if (remaining[i] <= quantum) {
                    currentTime += remaining[i];
                    remaining[i] = 0;
                    completion[i] = currentTime;
                    completed++;
                } else {
                    currentTime += quantum;
                    remaining[i] -= quantum;
                }

                printf("P%d (%d-%d)\n", i + 1, startTime, currentTime);
            }
        }
    }

    for (int i = 0; i < n; i++) {
        turnaround[i] = completion[i];
        waiting[i] = turnaround[i] - burst[i];
        totalWaiting += waiting[i];
        totalTurnaround += turnaround[i];
    }

    printf("\nProcess\tBurst\tCompletion\tWaiting\tTurnaround\n");
    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t%d\t\t%d\t%d\n",
               i + 1, burst[i], completion[i], waiting[i], turnaround[i]);
    }

    printf("\nAverage waiting time: %.2f\n", totalWaiting / n);
    printf("Average turnaround time: %.2f\n", totalTurnaround / n);

    return 0;
}
