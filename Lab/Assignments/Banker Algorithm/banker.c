#include <stdio.h>
#include <stdbool.h>

#define MAX_PROCESSES 100
#define MAX_RESOURCES 100

static int readNextInt(FILE *file, int *value)
{
    int ch;

    while ((ch = fgetc(file)) != EOF)
    {
        if (ch == '-' || (ch >= '0' && ch <= '9'))
        {
            ungetc(ch, file);
            return fscanf(file, "%d", value) == 1;
        }
    }

    return 0;
}

int main(void)
{
    int n, m;
    const char *inputFileName = "input.txt";
    FILE *inputFile = fopen(inputFileName, "r");

    if (inputFile == NULL)
    {
        printf("Could not open input file: %s\n", inputFileName);
        return 1;
    }

    int existing[MAX_RESOURCES];
    int allocation[MAX_PROCESSES][MAX_RESOURCES];
    int maximum[MAX_PROCESSES][MAX_RESOURCES];
    int need[MAX_PROCESSES][MAX_RESOURCES];
    int available[MAX_RESOURCES];

    int work[MAX_RESOURCES];
    bool finish[MAX_PROCESSES] = {false};
    int safeSequence[MAX_PROCESSES];

    if (!readNextInt(inputFile, &n))
    {
        printf("Could not read number of processes from input file.\n");
        fclose(inputFile);
        return 1;
    }

    if (!readNextInt(inputFile, &m))
    {
        printf("Could not read number of resource types from input file.\n");
        fclose(inputFile);
        return 1;
    }

    if (n <= 0 || n > MAX_PROCESSES ||
        m <= 0 || m > MAX_RESOURCES)
    {
        printf("Invalid number of processes or resources.\n");
        fclose(inputFile);
        return 1;
    }

    for (int j = 0; j < m; j++)
    {
        if (!readNextInt(inputFile, &existing[j]))
        {
            printf("Could not read Existing Resources vector from input file.\n");
            fclose(inputFile);
            return 1;
        }

        if (existing[j] < 0)
        {
            printf("Resource values cannot be negative.\n");
            fclose(inputFile);
            return 1;
        }
    }

    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < m; j++)
        {
            if (!readNextInt(inputFile, &allocation[i][j]))
            {
                printf("Could not read Allocation Matrix from input file.\n");
                fclose(inputFile);
                return 1;
            }

            if (allocation[i][j] < 0)
            {
                printf("Allocation cannot be negative.\n");
                fclose(inputFile);
                return 1;
            }
        }
    }

    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < m; j++)
        {
            if (!readNextInt(inputFile, &maximum[i][j]))
            {
                printf("Could not read Maximum Demand Matrix from input file.\n");
                fclose(inputFile);
                return 1;
            }

            if (maximum[i][j] < allocation[i][j])
            {
                printf(
                    "Invalid input: Maximum demand of P%d for R%d "
                    "is less than its allocation.\n",
                    i, j
                );

                fclose(inputFile);
                return 1;
            }
        }
    }

    fclose(inputFile);

    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < m; j++)
        {
            need[i][j] = maximum[i][j] - allocation[i][j];
        }
    }

    for (int j = 0; j < m; j++)
    {
        int allocatedTotal = 0;

        for (int i = 0; i < n; i++)
        {
            allocatedTotal += allocation[i][j];
        }

        available[j] = existing[j] - allocatedTotal;

        if (available[j] < 0)
        {
            printf(
                "\nInvalid input: Total allocation of R%d "
                "is greater than existing resources.\n",
                j
            );

            return 1;
        }

        work[j] = available[j];
    }

    printf("\nComputed Need Matrix:\n");

    printf("Process\t");
    for (int j = 0; j < m; j++)
    {
        printf("R%d\t", j);
    }
    printf("\n");

    for (int i = 0; i < n; i++)
    {
        printf("P%d\t", i);

        for (int j = 0; j < m; j++)
        {
            printf("%d\t", need[i][j]);
        }

        printf("\n");
    }

    printf("\nComputed Available Vector:\n");

    for (int j = 0; j < m; j++)
    {
        printf("R%d = %d\n", j, available[j]);
    }

    int completed = 0;

    while (completed < n)
    {
        bool processFound = false;

        for (int i = 0; i < n; i++)
        {
            if (!finish[i])
            {
                bool canExecute = true;

                for (int j = 0; j < m; j++)
                {
                    if (need[i][j] > work[j])
                    {
                        canExecute = false;
                        break;
                    }
                }

                if (canExecute)
                {
                    for (int j = 0; j < m; j++)
                    {
                        work[j] += allocation[i][j];
                    }

                    safeSequence[completed] = i;
                    completed++;

                    finish[i] = true;
                    processFound = true;
                }
            }
        }
 
        if (!processFound)
        {
            break;
        }
    }

    if (completed == n)
    {
        printf("\nThe system is in a SAFE state.\n");

        printf("Safe Sequence: < ");

        for (int i = 0; i < n; i++)
        {
            printf("P%d", safeSequence[i]);

            if (i < n - 1)
            {
                printf(", ");
            }
        }

        printf(" >\n");

        printf("Final Work Vector: ");

        for (int j = 0; j < m; j++)
        {
            printf("%d", work[j]);

            if (j < m - 1)
            {
                printf(", ");
            }
        }

        printf("\n");
    }
    else
    {
        printf("\nThe system is in an UNSAFE state.\n");
        printf("No safe sequence exists.\n");
    }

    return 0;
}
