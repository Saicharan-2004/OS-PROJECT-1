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

#define veryLarge 100

typedef struct
{
    int serial_number;
    char name[veryLarge];
    float price;
} MenuItem;

int readMenu(const char *filename, MenuItem menu[], int *itemCount)
{
    FILE *file;
    char line[veryLarge];
    int serialNumber;
    char itemName[veryLarge];
    float price;

    file = fopen(filename, "r");
    if (file == NULL)
    {
        perror("Error opening file");
        return 0; // Return 0 to indicate failure
    }

    while (fgets(line, sizeof(line), file))
    {
        serialNumber = 0;
        memset(itemName, 0, sizeof(itemName));
        price = 0.0f;

        char *token = strtok(line, ".");
        if (token != NULL)
        {
            serialNumber = atoi(token);
            token = strtok(NULL, "INR");
            if (token != NULL)
            {
                sscanf(token, "%49[^0-9] %f", itemName, &price);
                menu[*itemCount].serial_number = serialNumber;
                strncpy(menu[*itemCount].name, itemName, sizeof(menu[*itemCount].name) - 1);
                menu[*itemCount].price = price;
                (*itemCount)++;
            }
        }
    }

    fclose(file);
    return 1;
}

int isValid(MenuItem menu[], int itemCount, int (*shared_order)[6][veryLarge])
{

    int num_customers = (*shared_order)[0][0]; // number of customers at the corresponding table

    for (int i = 1; i <= num_customers; i++)
    {
        if ((*shared_order)[i][0] == 0) // if the customer has not ordered anything then it is correct order so dont do anything
        {
            continue;
        }
        for (int j = 1; j <= (*shared_order)[i][0]; j++) // check if all the orders entered by the customer are correct
        {
            int item = (*shared_order)[i][j];
            int not_found = 1;
            for (int k = 0; k < itemCount; k++)
            {
                if (item == menu[k].serial_number)
                {
                    not_found = 0;
                    break;
                }
            }
            if (not_found)
            {
                return 0;
            }
        }
    }

    return 1;
}

int total_bill(MenuItem menu[], int itemCount, int (*shared_order)[6][veryLarge])
{

    int num_customers = (*shared_order)[0][0];
    int totalOrderBill = 0;

    for (int i = 1; i <= num_customers; i++)
    {
        if ((*shared_order)[i][0] == 0)
            continue;

        for (int j = 1; j <= (*shared_order)[i][0]; j++)
        {
            int item = (*shared_order)[i][j];
            for (int k = 0; k < itemCount; k++)
            {
                if (item == menu[k].serial_number)
                {
                    totalOrderBill += menu[k].price;
                }
            }
        }
    }

    return totalOrderBill;
}

int main()
{
    int waiterId;
    printf("Enter Waiter ID: ");
    scanf("%d", &waiterId);

    MenuItem menu[veryLarge];
    int itemCount = 0;

    const char *filename = "menu.txt";

    if (!readMenu(filename, menu, &itemCount))
    {
        fprintf(stderr, "Failed to read menu from %s\n", filename);
        return -1;
    }

    key_t waiter_key = ftok(".", waiterId);
    if (waiter_key == -1)
    {
        perror("ftok1");
        exit(1);
    }

    int waiter_shm_id = shmget(waiter_key, sizeof(int) * 6 * veryLarge, 0666);
    if (waiter_shm_id == -1)
    {
        perror("shmget1");
        exit(1);
    }

    int(*shared_order)[6][veryLarge];
    shared_order = (int(*)[6][veryLarge])shmat(waiter_shm_id, NULL, 0);
    if (shared_order == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }

    key_t manager_key = ftok("hotelmanager.c", waiterId * 10);
    if (manager_key == -1)
    {
        perror("ftok1");
        exit(1);
    }

    int manager_shm_id = shmget(manager_key, sizeof(int) * veryLarge, 0666);
    if (manager_shm_id == -1)
    {
        perror("shmget2");
        exit(1);
    }

    int(*manager_shared_memory)[veryLarge];
    manager_shared_memory = shmat(manager_shm_id, NULL, 0);
    if (manager_shared_memory == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }
    // (*manager_shared_memory)[waiterId * 10] = 1;
    (*manager_shared_memory)[3] = 0; // 1 -> waiter has terminated, 0 -> waiter process is still running
    // (*manager_shared_memory)[10] = 500;
    (*manager_shared_memory)[255] = 0;

    while (1) // iterate until the table doesnt terminate the waiter
    {
        // (*manager_shared_memory)[waiterId * 10] = 1;
        // (*manager_shared_memory)[10] = 500;
        (*manager_shared_memory)[4] = 0; // has manager revieved the bill flag
        if ((*shared_order)[0][5])       // terminate if table has terminated
        {
            break;
        }

        (*manager_shared_memory)[1] = 0; // has bill been calculated by the waiter

        while ((*shared_order)[0][4] != 1) // wait until all the customers have entered the order
        {
            sleep(1);
            if ((*shared_order)[0][5])
            {
                break;
            }
        }

        if ((*shared_order)[0][5])
        {
            break;
        }

        (*shared_order)[0][4] = 0; // reset the flag
        // (*shared_order)[0][1] = -1;
        int isValidOrder = isValid(menu, itemCount, shared_order);
        (*shared_order)[0][1] = isValidOrder; // share with table whether the order is valid or not
        if (!isValidOrder)
        {
            continue;
        }

        int totalOrderBill = total_bill(menu, itemCount, shared_order);
        (*shared_order)[0][3] = totalOrderBill; // pass the total bill to the table
        (*shared_order)[0][2] = 1;              // signal table that the total bill is calculated
        // (*manager_shared_memory)[10] = 500;
        (*manager_shared_memory)[2] = totalOrderBill; // pass the total bill to the manager
        (*manager_shared_memory)[1] = 1;              // signal manager that the total bill has been calculated

        while (!(*manager_shared_memory)[4]) // wait while the manager acknowledges it has recieved the bill
        {
            sleep(1);
            if ((*shared_order)[0][5])
            {
                break;
            }
        }
    }

    // (*manager_shared_memory)[waiterId * 10] = 1;
    (*manager_shared_memory)[3] = 1;   // signal manager that the waiter has terminated
    (*manager_shared_memory)[255] = 1; // signal the manager that the waiter has terminated

    // Detach from the shared_order memory segment
    if (shmdt(shared_order) == -1)
    {
        perror("shmdt for shared_order");
        exit(1);
    }

    // Detach from the manager_shared_memory segment
    if (shmdt(manager_shared_memory) == -1)
    {
        perror("shmdt for manager_shared_memory");
        exit(1);
    }

    return 0;
}
