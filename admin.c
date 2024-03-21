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

int main()
{
    char input;
    int shmId;

    key_t manager_key = ftok(".", 527);
    if (manager_key == -1)
    {
        perror("ftok1");
        exit(1);
    }

    int manager_shm_id = shmget(manager_key, sizeof(int) * veryLarge, IPC_CREAT | 0666);
    if (manager_shm_id == -1)
    {
        perror("shmget");
        exit(1);
    }

    int(*manager_shared_memory)[veryLarge];
    manager_shared_memory = shmat(manager_shm_id, NULL, 0);
    if (manager_shared_memory == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }

    (*manager_shared_memory)[0] = 0; // flag to indicate termninate signal to hotelmanager

    while (1)
    {
        printf("Do you want to close the hotel? Enter Y for Yes and N for No: ");
        scanf(" %c", &input);

        if (input == 'N') // if user enters 'N' continue
        {
            continue;
        }
        else if (input == 'Y') // signal hotelmanager to close and then terminate
        {
            (*manager_shared_memory)[0] = 1;
            break;
        }
        else
        {
            printf("Invalid input, please enter Y or N.\n");
        }
    }

    // Detaching from the manager shared memory segment
    if (shmdt(manager_shared_memory) == -1)
    {
        perror("shmdt for manager_shared_memory");
    }
    else
    {
        // Mark the manager shared memory segment for deletion
        if (shmctl(manager_shm_id, IPC_RMID, NULL) == -1)
        {
            perror("shmctl for manager_shm_id");
        }
    }

    printf("Admin process terminated.\n");

    return 0;
}
