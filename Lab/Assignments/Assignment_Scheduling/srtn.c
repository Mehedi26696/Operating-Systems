#include <stdio.h>

#define MAX_PROCESSES 100

int main(void)
{
    int n;
    int arrival[MAX_PROCESSES];
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

    printf("Enter arrival time and burst time for each process:\n");
    for (int i = 0; i < n; i++) {
        printf("P%d: ", i + 1);
        scanf("%d %d", &arrival[i], &burst[i]);

        if (arrival[i] < 0 || burst[i] <= 0) {
            printf("Invalid arrival time or burst time.\n");
            return 1;
        }

        remaining[i] = burst[i];
    }

    printf("\nExecution timeline:\n");

    while (completed < n) {
        int shortest = -1;

        for (int i = 0; i < n; i++) {
            if (arrival[i] <= currentTime && remaining[i] > 0) {
                if (shortest == -1 || remaining[i] < remaining[shortest]) {
                    shortest = i;
                }
            }
        }

        if (shortest == -1) {
            printf("Time %d-%d: Idle\n", currentTime, currentTime + 1);
            currentTime++;
            continue;
        }

        printf("Time %d-%d: P%d\n",
               currentTime, currentTime + 1, shortest + 1);

        remaining[shortest]--;
        currentTime++;

        if (remaining[shortest] == 0) {
            completion[shortest] = currentTime;
            completed++;
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
               i + 1, arrival[i], burst[i], completion[i],
               waiting[i], turnaround[i]);
    }

    printf("\nAverage waiting time: %.2f\n", totalWaiting / n);
    printf("Average turnaround time: %.2f\n", totalTurnaround / n);

    return 0;
}
