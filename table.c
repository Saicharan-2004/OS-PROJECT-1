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

#define largeValue 100

typedef struct
{
    int serial_number;
    char name[largeValue];
    float price;
} MenuItem;

int readMenu(const char *filename, MenuItem menu[], int *itemCount)
{
    FILE *file;
    char line[largeValue];
    int serialNumber;
    char itemName[largeValue];
    float price;

    file = fopen(filename, "r");
    if (file == NULL)
    {
        perror("Error opening file");
        return 0;
    }

    while (fgets(line, sizeof(line), file))
    {
        if (sscanf(line, "%d.%49[^0-9] %f", &serialNumber, itemName, &price) == 3)
        {
            menu[*itemCount].serial_number = serialNumber;
            strncpy(menu[*itemCount].name, itemName, sizeof(menu[*itemCount].name) - 1);
            menu[*itemCount].price = price;
            (*itemCount)++;
        }
    }

    fclose(file);
    return 1;
}

void displayMenu(const MenuItem menu[], int itemCount)
{
    printf("Menu:\n");
    for (int i = 0; i < itemCount; i++)
    {
        printf("%d. %s - %.2f INR\n", menu[i].serial_number, menu[i].name, menu[i].price);
    }
}

int main()
{
    int table_id;
    printf("Enter Table Number: ");
    scanf("%d", &table_id);

    MenuItem menu[largeValue];
    int itemCount = 0;
    const char *filename = "menu.txt";

    if (!readMenu(filename, menu, &itemCount))
    {
        fprintf(stderr, "Failed to read menu from %s\n", filename);
        return -1;
    }

    key_t table_key = ftok(".", table_id);
    if (table_key == -1)
    {
        perror("ftok1");
        exit(1);
    }

    int table_shm_id = shmget(table_key, sizeof(int) * 6 * largeValue, IPC_CREAT | 0666);
    if (table_shm_id == -1)
    {
        perror("shmgetx");
        exit(1);
    }

    int(*shared_order)[6][largeValue];

    shared_order = (int(*)[6][largeValue])shmat(table_shm_id, NULL, 0);
    if (shared_order == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }

    (*shared_order)[0][5] = 0; //  0 - table process running 1 - table process terminated hence terminate waiter

    key_t flags_key = ftok(".", 564);

    int flags_shm_id = shmget(flags_key, sizeof(int) * largeValue, IPC_CREAT | 0666);
    if (flags_shm_id == -1)
    {
        perror("shmget for flags");
        exit(1);
    }

    int *flags;
    flags = (int *)shmat(flags_shm_id, NULL, 0);
    if (flags == (void *)-1)
    {
        perror("shmat for flags");
        exit(1);
    }

    for (int i = 0; i < largeValue; i++)
    {
        flags[i] = 0;
    }

    while (1)
    {

        int num_customers;
        printf("Enter Number of Customers at Table (maximum no. of customers can be 5):");
        scanf("%d", &num_customers);

        (*shared_order)[0][0] = num_customers; // num_customers
        (*shared_order)[0][4] = 0;             // 1 if shared_order is filled by the table

        if (num_customers == -1) // exit condition
        {
            break;
        }

        int childToParentPipes[num_customers][2]; // creating child to parent pipes
        pid_t pids[num_customers];

        for (int i = 0; i < num_customers; i++)
        {
            if (pipe(childToParentPipes[i]) == -1)
            {
                perror("pipe creation failed");
                exit(1);
            }
        }

        printf("\n");
        displayMenu(menu, itemCount);
        printf("\n");

        for (int i = 0; i < num_customers; i++)
        {
            pids[i] = fork(); // fork all the customer processes
            if (pids[i] < 0)
            {
                perror("fork failed");
                exit(1);
            }
            else if (pids[i] == 0) // children logic
            {
                close(childToParentPipes[i][0]);

                int flag;
                do
                {
                    flag = flags[i]; // read the flags from table shared memory to determine the next steps

                    if (flag == 1)
                    { // Take order
                        int order[256] = {0}, idx = 0, srNo;
                        printf("Customer %d, enter your order (serial numbers, -1 to finish):\n", i + 1);
                        do
                        {
                            scanf("%d", &srNo);
                            if (srNo != -1)
                            {
                                order[idx++] = srNo;
                            }

                        } while (srNo != -1);

                        if (idx == 0)
                        {
                            order[0] = -1;
                            idx++;
                        }

                        int ack = 1;
                        write(childToParentPipes[i][1], &ack, sizeof(ack));        // acknowledge the parent that order has been taken for customer
                        write(childToParentPipes[i][1], order, idx * sizeof(int)); // send order to the parent
                        flags[i] = 0;                                              // reset the flags
                    }
                } while (flag != 2);

                close(childToParentPipes[i][1]); // close the unused end of the pipe
                exit(0);
            }
            else
            {
                close(childToParentPipes[i][1]); //
            }
        }

        int allOrdersValid = 0;
        while (!allOrdersValid) // loop until all the customers enter a valid order
        {
            (*shared_order)[0][2] = 0; // when total bill is calculated
            allOrdersValid = 1;        // assume all orders to be valid initially

            for (int i = 0; i < num_customers; i++) // signal children processes to read the input order
            {
                int flag = 1;
                flags[i] = 1;
                read(childToParentPipes[i][0], &flag, sizeof(flag)); // while the children has not taken the order, this line will implemnt a wait mechanism for synchronization
            }

            for (int i = 0; i < num_customers; i++)
            {
                int order[256];
                int bytesRead = read(childToParentPipes[i][0], order, sizeof(order)); // read orders from the pipe
                int numItems = bytesRead / sizeof(int);
                if (numItems == 1 && order[0] == -1) // if the customer has not ordered anything
                {
                    (*shared_order)[i + 1][0] = 0;
                }
                else
                { // communicate the order to the waiter through shared memory
                    (*shared_order)[i + 1][0] = numItems;
                    for (int j = 1; j <= numItems; j++)
                    {
                        (*shared_order)[i + 1][j] = order[j - 1];
                    }
                }
            }

            (*shared_order)[0][1] = -1; // is the order valid or not
            (*shared_order)[0][4] = 1;  // shared order is filled by the flag

            while ((*shared_order)[0][1] == -1) // wait while waiter calculates whether the order is valid or not
            {
                sleep(1);
            }

            allOrdersValid = (*shared_order)[0][1];

            if (!allOrdersValid) // retake all orders if current ones are not valid
            {
                printf("\ninvalid orders, taking all orders again\n\n");
            }
        }

        while ((*shared_order)[0][2] == 0) // wait until waiter calculates the bill
        {
            sleep(1);
        }

        int totalBill = (*shared_order)[0][3]; // revieve the total bill from waiter through shared memory

        printf("The total bill amount is %d INR. ", totalBill);
        printf("\n");

        for (int i = 0; i < num_customers; i++) // signal all child processes to terminate
        {
            flags[i] = 2;
        }

        for (int i = 0; i < num_customers; i++) // wait for all the child processes to terminate
        {
            waitpid(pids[i], NULL, 0);
        }

        for (int i = 0; i < num_customers; i++) // close all the open pipes
        {
            close(childToParentPipes[i][0]);
        }

        for (int i = 0; i < 100; i++) // resetting the flags for next iteration
        {
            flags[i] = 0;
        }
    }

    (*shared_order)[0][5] = 1; // terminate the table process

    printf("Table process terminated.\n");

    // Detach the shared order memory segment
    if (shmdt(shared_order) == -1)
    {
        perror("shmdt for shared_order");
        exit(1);
    }

    // Delete the shared order memory segment
    if (shmctl(table_shm_id, IPC_RMID, NULL) == -1)
    {
        perror("shmctl for table_shm_id");
        exit(1);
    }

    // Detach the flags memory segment
    if (shmdt(flags) == -1)
    {
        perror("shmdt for flags");
        exit(1);
    }

    // Delete the flags memory segment
    if (shmctl(flags_shm_id, IPC_RMID, NULL) == -1)
    {
        perror("shmctl for flags_shm_id");
        exit(1);
    }

    return 0;
}