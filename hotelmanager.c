#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

#define EARNINGS_FILE "earnings.txt"
#define veryLarge 100

typedef struct
{
    int tableId;
    int earnings;
} TableEarnings;

void writeEarningsToFile(int tableId, int earnings)
{
    FILE *file = fopen(EARNINGS_FILE, "a");
    if (file == NULL)
    {
        perror("Failed to open file");
        exit(1);
    }
    fprintf(file, "Earning from Table %d: %d INR\n", tableId, earnings);
    fclose(file);
}

int main()
{
    int numOfTables;
    printf("Enter the Total Number of Tables at the Hotel: ");
    scanf("%d", &numOfTables);

    key_t waiterKeys[numOfTables];
    int waiter_shm_id[numOfTables];
    int(*waiter_shared_memory[numOfTables])[veryLarge];

    for (int i = 0; i < numOfTables; i++)
    {
        waiterKeys[i] = ftok("hotelmanager.c", (i + 1) * 10);
        if (waiterKeys[i] == -1)
        {
            perror("ftok");
            exit(1);
        }

        waiter_shm_id[i] = shmget(waiterKeys[i], sizeof(int) * veryLarge, IPC_CREAT | 0666);
        if (waiter_shm_id[i] == -1)
        {
            perror("shmget");
            exit(1);
        }

        waiter_shared_memory[i] = (int(*)[veryLarge])shmat(waiter_shm_id[i], NULL, 0);
        if (waiter_shared_memory[i] == (void *)-1)
        {
            perror("shmat");
            exit(1);
        }
    }

    key_t admin_key = ftok(".", 527);
    if (admin_key == -1)
    {
        perror("ftok1");
        exit(1);
    }

    int admin_shm_id = shmget(admin_key, sizeof(int) * veryLarge, 0666);
    if (admin_shm_id == -1)
    {
        perror("shmget");
        exit(1);
    }

    int(*admin_shared_memory)[veryLarge];
    admin_shared_memory = shmat(admin_shm_id, NULL, 0);
    if (admin_shared_memory == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }

    (*admin_shared_memory)[0] = 0; // terminate hotelmanager flag

    int total_bill = 0;

    for (int i = 0; i < numOfTables; i++)
    {
        waiter_shared_memory[i][0][255] = 1; // has waiter terminated flag
    }

    int totalTableBills[100] = {0}; // an array to store the total bills of all tables

    while (1)
    {
        int newOrders = 0;

        for (int i = 0; i < numOfTables; i++)
        {
            if (waiter_shared_memory[i][0][1]) // if bill has been calculated then process it
            {
                total_bill += waiter_shared_memory[i][0][2]; // add the current bill to the total bill
                // writeEarningsToFile(i + 1, waiter_shared_memory[i][0][2]);
                totalTableBills[i] += waiter_shared_memory[i][0][2]; // add the current bill to that table's total bill
                waiter_shared_memory[i][0][1] = 0;                   // reset the bill calculated by the waiter flag
                waiter_shared_memory[i][0][4] = 1;                   // signal the waiter that the manager has processed the bill
                newOrders = 1;
            }
        }

        if ((*admin_shared_memory)[0] == 1 && !newOrders) // if admin has signalled the hotelmanager to terminate
        {
            int allWaitersReadyToTerminate = 1;
            for (int i = 0; i < numOfTables; i++) // wait for all the tables to terminate
            {
                if (waiter_shared_memory[i][0][255] != 1)
                {
                    allWaitersReadyToTerminate = 0;
                    break;
                }
            }
            if (allWaitersReadyToTerminate)
            {
                break;
            }
        }

        sleep(1);
    }

    for (int i = 0; i < numOfTables; i++) // write the earnings of all the tables
    {
        writeEarningsToFile(i + 1, totalTableBills[i]);
    }

    printf("\nTotal Earnings of Hotel: %d\n", total_bill);
    printf("Total Wages of Waiters: %d\n", (int)((0.4) * total_bill));
    printf("Total Profit: %d\n\n", (int)((0.6) * total_bill));

    FILE *file = fopen(EARNINGS_FILE, "a");
    if (file == NULL)
    {
        perror("Failed to open file");
        exit(1);
    }

    fprintf(file, "\n");
    fprintf(file, "Total Earnings of Hotel: %d\n", total_bill);
    fprintf(file, "Total Wages of Waiters: %d\n", (int)((0.4) * total_bill));
    fprintf(file, "Total Profit: %d\n", (int)((0.6) * total_bill));
    fclose(file);

    printf("\nThank you for visiting the Hotel!\n");

    // Detaching and deleting waiter shared memory segments
    for (int i = 0; i < numOfTables; i++)
    {
        if (shmdt(waiter_shared_memory[i]) == -1)
        {
            perror("shmdt for waiter_shared_memory");
        }
        else
        {
            if (shmctl(waiter_shm_id[i], IPC_RMID, NULL) == -1)
            {
                perror("shmctl for waiter_shm_id");
            }
        }
    }

    // Detaching from the admin shared memory segment
    if (shmdt(admin_shared_memory) == -1)
    {
        perror("shmdt for admin_shared_memory");
    }

    return 0;
}
