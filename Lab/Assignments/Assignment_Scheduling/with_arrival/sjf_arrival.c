#include <stdio.h>

#define MAX_PROCESSES 100
#define INF 999999

int main()
{
    int n;
    int process[MAX_PROCESSES];
    int arrival[MAX_PROCESSES];
    int burst[MAX_PROCESSES];
    int completion[MAX_PROCESSES];
    int waiting[MAX_PROCESSES];
    int turnaround[MAX_PROCESSES];
    int completed[MAX_PROCESSES];

    int executionOrder[MAX_PROCESSES];
    int startTime[MAX_PROCESSES];
    int endTime[MAX_PROCESSES];

    int currentTime = 0;
    int completedCount = 0;
    int orderCount = 0;

    float totalWaiting = 0;
    float totalTurnaround = 0;

    printf("Enter number of processes: ");
    scanf("%d", &n);

    if (n <= 0 || n > MAX_PROCESSES)
    {
        printf("Invalid number of processes.\n");
        return 1;
    }

    printf("\nEnter arrival time and burst time:\n");

    for (int i = 0; i < n; i++)
    {
        process[i] = i + 1;
        completed[i] = 0;

        printf("P%d Arrival Time: ", i + 1);
        scanf("%d", &arrival[i]);

        printf("P%d Burst Time: ", i + 1);
        scanf("%d", &burst[i]);

        if (arrival[i] < 0)
        {
            printf("Arrival time cannot be negative.\n");
            return 1;
        }

        if (burst[i] <= 0)
        {
            printf("Burst time must be positive.\n");
            return 1;
        }
    }

    while (completedCount < n)
    {
        int idx = -1;
        int minBurst = INF;
 
        for (int i = 0; i < n; i++)
        {
            if (arrival[i] <= currentTime && completed[i] == 0)
            {
                if (burst[i] < minBurst)
                {
                    minBurst = burst[i];
                    idx = i;
                }
                else if (burst[i] == minBurst)
                {
 
                    if (idx == -1 || arrival[i] < arrival[idx])
                    {
                        idx = i;
                    }
                }
            }
        }

 
        if (idx == -1)
        {
            int nextArrival = INF;

            for (int i = 0; i < n; i++)
            {
                if (completed[i] == 0 && arrival[i] < nextArrival)
                {
                    nextArrival = arrival[i];
                }
            }

            currentTime = nextArrival;
        }
        else
        {
            startTime[orderCount] = currentTime;
            executionOrder[orderCount] = process[idx];

            currentTime += burst[idx];

            endTime[orderCount] = currentTime;
            orderCount++;

            completion[idx] = currentTime;
            turnaround[idx] = completion[idx] - arrival[idx];
            waiting[idx] = turnaround[idx] - burst[idx];

            totalWaiting += waiting[idx];
            totalTurnaround += turnaround[idx];

            completed[idx] = 1;
            completedCount++;
        }
    }

    printf("\nExecution Order:\n");
    for (int i = 0; i < orderCount; i++)
    {
        printf("P%d", executionOrder[i]);

        if (i < orderCount - 1)
        {
            printf(" -> ");
        }
    }

    printf("\n\nGantt Chart:\n");
    for (int i = 0; i < orderCount; i++)
    {
        printf("|  P%d  ", executionOrder[i]);
    }
    printf("|\n");

    if (orderCount > 0)
    {
        printf("%d", startTime[0]);
        for (int i = 0; i < orderCount; i++)
        {
            printf("      %d", endTime[i]);
        }
    }

    printf("\n\nProcess\tArrival\tBurst\tCompletion\tWaiting\tTurnaround\n");

    for (int i = 0; i < n; i++)
    {
        printf("P%d\t%d\t%d\t%d\t\t%d\t%d\n",
               process[i],
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
