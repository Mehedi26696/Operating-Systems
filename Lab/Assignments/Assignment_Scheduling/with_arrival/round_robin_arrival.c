#include <stdio.h>

#define MAX_PROCESSES 100

int main() {
    int n, quantum;

    int arrival[MAX_PROCESSES];
    int burst[MAX_PROCESSES];
    int remaining[MAX_PROCESSES];
    int completion[MAX_PROCESSES];
    int waiting[MAX_PROCESSES];
    int turnaround[MAX_PROCESSES];

    int currentTime = 0;
    int completed = 0;
    int index = 0;

    float totalWaiting = 0;
    float totalTurnaround = 0;

    printf("Enter number of processes: ");
    scanf("%d", &n);

    if (n <= 0 || n > MAX_PROCESSES) {
        printf("Invalid number of processes.\n");
        return 1;
    }

    printf("Enter arrival time and burst time for each process:\n");

    for (int i = 0; i < n; i++) {
        printf("P%d Arrival Time: ", i + 1);
        scanf("%d", &arrival[i]);

        printf("P%d Burst Time: ", i + 1);
        scanf("%d", &burst[i]);

        if (arrival[i] < 0 || burst[i] <= 0) {
            printf("Invalid arrival or burst time.\n");
            return 1;
        }

        remaining[i] = burst[i];
        completion[i] = 0;
        waiting[i] = 0;
        turnaround[i] = 0;
    }

    printf("Enter time quantum: ");
    scanf("%d", &quantum);

    if (quantum <= 0) {
        printf("Time quantum must be positive.\n");
        return 1;
    }

    printf("\nGantt Chart:\n");

    while (completed < n) {
        int found = 0;

        for (int count = 0; count < n; count++) {
            int i = (index + count) % n;

            if (arrival[i] <= currentTime && remaining[i] > 0) {
                found = 1;

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
 
                index = (i + 1) % n;

                break;
            }
        }

        if (found == 0) {
            int nextArrival = -1;

            for (int i = 0; i < n; i++) {
                if (remaining[i] > 0) {
                    if (nextArrival == -1 || arrival[i] < nextArrival) {
                        nextArrival = arrival[i];
                    }
                }
            }

            if (nextArrival > currentTime) {
                printf("Idle (%d-%d)\n", currentTime, nextArrival);
                currentTime = nextArrival;
            }
        }
    }

    for (int i = 0; i < n; i++) {
        turnaround[i] = completion[i] - arrival[i];
        waiting[i] = turnaround[i] - burst[i];

        totalWaiting += waiting[i];
        totalTurnaround += turnaround[i];
    }

    printf("\nProcess\tArrival\tBurst\tCompletion\tWaiting\tTurnaround\n");

    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t%d\t%d\t\t%d\t%d\n",
               i + 1,
               arrival[i],
               burst[i],
               completion[i],
               waiting[i],
               turnaround[i]);
    }

    printf("\nAverage Waiting Time: %.2f\n", totalWaiting / n);
    printf("Average Turnaround Time: %.2f\n", totalTurnaround / n);

    return 0;
}
