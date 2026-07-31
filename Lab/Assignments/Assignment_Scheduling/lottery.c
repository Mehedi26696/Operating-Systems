#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_PROCESSES 100

int main(void)
{
    int n, quantum;
    int burst[MAX_PROCESSES];
    int remaining[MAX_PROCESSES];
    int tickets[MAX_PROCESSES];
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

    printf("Enter burst time and number of tickets for each process:\n");
    for (int i = 0; i < n; i++) {
        printf("P%d: ", i + 1);
        scanf("%d %d", &burst[i], &tickets[i]);

        if (burst[i] <= 0 || tickets[i] <= 0) {
            printf("Burst time and ticket count must be positive.\n");
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

 
    srand((unsigned int)time(NULL));

    printf("\nLottery execution log:\n");

    while (completed < n) {
        int totalTickets = 0;
        int winner = -1;

        for (int i = 0; i < n; i++) {
            if (remaining[i] > 0) {
                totalTickets += tickets[i];
            }
        }

        int winningTicket = rand() % totalTickets + 1;
        int ticketSum = 0;

        for (int i = 0; i < n; i++) {
            if (remaining[i] > 0) {
                ticketSum += tickets[i];

                if (winningTicket <= ticketSum) {
                    winner = i;
                    break;
                }
            }
        }

        int runTime;
        if (remaining[winner] < quantum) {
            runTime = remaining[winner];
        } else {
            runTime = quantum;
        }

        printf("Ticket %d of %d: P%d runs from %d to %d\n",
               winningTicket, totalTickets, winner + 1,
               currentTime, currentTime + runTime);

        currentTime += runTime;
        remaining[winner] -= runTime;

        if (remaining[winner] == 0) {
            completion[winner] = currentTime;
            completed++;
        }
    }

    for (int i = 0; i < n; i++) {
        turnaround[i] = completion[i];
        waiting[i] = turnaround[i] - burst[i];
        totalWaiting += waiting[i];
        totalTurnaround += turnaround[i];
    }

    printf("\nProcess\tBurst\tTickets\tCompletion\tWaiting\tTurnaround\n");
    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t%d\t%d\t\t%d\t%d\n",
               i + 1, burst[i], tickets[i], completion[i],
               waiting[i], turnaround[i]);
    }

    printf("\nAverage waiting time: %.2f\n", totalWaiting / n);
    printf("Average turnaround time: %.2f\n", totalTurnaround / n);

    return 0;
}
