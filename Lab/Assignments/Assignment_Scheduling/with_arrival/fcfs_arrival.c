#include <stdio.h>

#define MAX_PROCESSES 100

int main(void)
{
    int n;
    int id[MAX_PROCESSES];
    int arrival[MAX_PROCESSES];
    int burst[MAX_PROCESSES];
    int start[MAX_PROCESSES];
    int completion[MAX_PROCESSES];
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

    printf("Enter arrival time and burst time for each process:\n");
    for (int i = 0; i < n; i++) {
        id[i] = i + 1;

        printf("P%d arrival time: ", i + 1);
        scanf("%d", &arrival[i]);

        printf("P%d burst time: ", i + 1);
        scanf("%d", &burst[i]);

        if (arrival[i] < 0) {
            printf("Arrival time cannot be negative.\n");
            return 1;
        }

        if (burst[i] <= 0) {
            printf("Burst time must be positive.\n");
            return 1;
        }
    }
 
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (arrival[j] > arrival[j + 1] ||
                (arrival[j] == arrival[j + 1] && id[j] > id[j + 1])) {

                int temp;

                temp = arrival[j];
                arrival[j] = arrival[j + 1];
                arrival[j + 1] = temp;

                temp = burst[j];
                burst[j] = burst[j + 1];
                burst[j + 1] = temp;

                temp = id[j];
                id[j] = id[j + 1];
                id[j + 1] = temp;
            }
        }
    }

    int currentTime = 0;

    for (int i = 0; i < n; i++) {
        if (currentTime < arrival[i]) {
            currentTime = arrival[i];
        }

        start[i] = currentTime;
        completion[i] = start[i] + burst[i];
        turnaround[i] = completion[i] - arrival[i];
        waiting[i] = turnaround[i] - burst[i];

        totalWaiting += waiting[i];
        totalTurnaround += turnaround[i];

        currentTime = completion[i];
    }

    printf("\nFCFS Scheduling with Different Arrival Times\n");
    printf("Process\tArrival\tBurst\tStart\tCompletion\tWaiting\tTurnaround\n");

    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t%d\t%d\t%d\t\t%d\t%d\n",
               id[i], arrival[i], burst[i], start[i], completion[i], waiting[i], turnaround[i]);
    }

    printf("\nAverage waiting time: %.2f\n", totalWaiting / n);
    printf("Average turnaround time: %.2f\n", totalTurnaround / n);

    return 0;
}
