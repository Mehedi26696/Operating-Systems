#include <stdio.h>

#define MAX_PROCESSES 100

int main(void)
{
    int n;
    int process[MAX_PROCESSES];
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
        process[i] = i + 1;
        printf("P%d: ", i + 1);
        scanf("%d", &burst[i]);

        if (burst[i] <= 0) {
            printf("Burst time must be positive.\n");
            return 1;
        }
    }

    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (burst[j] > burst[j + 1]) {
                int temp = burst[j];
                burst[j] = burst[j + 1];
                burst[j + 1] = temp;

                temp = process[j];
                process[j] = process[j + 1];
                process[j + 1] = temp;
            }
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

    printf("\nExecution order: ");
    for (int i = 0; i < n; i++) {
        printf("P%d", process[i]);
        if (i < n - 1) {
            printf(" -> ");
        }
    }

    printf("\n\nProcess\tBurst\tWaiting\tTurnaround\n");
    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t%d\t%d\n",
               process[i], burst[i], waiting[i], turnaround[i]);
    }

    printf("\nAverage waiting time: %.2f\n", totalWaiting / n);
    printf("Average turnaround time: %.2f\n", totalTurnaround / n);

    return 0;
}
