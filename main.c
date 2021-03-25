#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>

#define MAX_MEMORY_SIZE 64
//#define THREAD_NUM 8
#define BUFFER_SIZE 10

sem_t semEmpty;         // Semaphore for # of empty spots in buffer
sem_t semFull;          // Semaphore for # of full spots in buffer

pthread_mutex_t mutx;	// Mutex
pthread_mutex_t mutx2;	// Mutex

int THREAD_NUM;			// # of threads requesting memory from the MMS thread 
int FUNCTION_NUM;		// 1 = first fit, 2 = best fit, 3 = worst fit 
int defrag;             // 0 = defragmentation disabled, 1 = defragmentation enabled

int memory_table[20][6];
int count = 1;
int curr_mem_size = 0;		// Keep track of how much memory is available
int* memory_block_ptr;

char mms_thread[]= "MMS Thread";

struct user_thread_info {
	int num_thread;			// Thread #
	int* ptr;				// Pointer to first memory space
	int sleep_time;			// Time while user sleeps with allocated memory
	int mem_size;			// Requested memory size
	bool waiting;			// True until MMS allocates requested memory
	bool serviced;			// False until MMS dealloactes user memory
	bool wake_up;			// False until need to wake up thread before sleep_time = 0
};

struct user_thread_info* user_th_buff[BUFFER_SIZE];
int position = 0;
struct user_thread_info* user_th_buff2[BUFFER_SIZE];
int position2 = 0;

void print_memory() 
{
	int i, j;
	printf("----------MEMORY STATUS----------\n\n");
	printf("Block\tSize\tStart\tEnd\tFlag\tThread\n");
	for(i = 0; i < count; i++) {
		for(int j = 0; j < 6; j++) {
			printf("%d\t", memory_table[i][j]);
		}
		printf("\n");		
	}
	printf("\nNumber of Blocks = %d\n\n", count);
}

void insert_memory_block(int row, int size, int thread_id)
{
	int i;
	curr_mem_size += size;
	if(row != count - 1 && size < memory_table[row][1]) {			
		for(i = count; i > row + 1; i--) {						// Shift memory down but only increment block #
			memory_table[i][0] = memory_table[i - 1][0] + 1;
			memory_table[i][1] = memory_table[i - 1][1];
			memory_table[i][2] = memory_table[i - 1][2];
			memory_table[i][3] = memory_table[i - 1][3];
			memory_table[i][4] = memory_table[i - 1][4];
			memory_table[i][5] = memory_table[i - 1][5];
		}
		memory_table[row + 1][0] = memory_table[row][0] + 1;
		memory_table[row + 1][1] = memory_table[row][1] - size;
		memory_table[row + 1][2] = memory_table[row][2] + size;
		memory_table[row + 1][3] = memory_table[row + 1][2] + (memory_table[row + 1][1] - 1);
		memory_table[row + 1][4] = 0;
		memory_table[row + 1][5] = 0;
		count++;								// Will create smaller hole so need to increment count
	}
	else {
		if(size != memory_table[row][1]) {
			for(i = count; i > row; i--) {
				memory_table[i][0] = memory_table[i - 1][0] + 1;
				memory_table[i][1] = memory_table[i - 1][1] - size;
				memory_table[i][2] = memory_table[i - 1][2] + size;
				memory_table[i][3] = memory_table[i][2] + (memory_table[i][1] - 1);
				memory_table[i][4] = memory_table[i - 1][4];
				memory_table[i][5] = memory_table[i - 1][5];
			}	
			count++;
		}
	}

	memory_table[row][1] = size;
	memory_table[row][3] = memory_table[row][2] + (memory_table[row][1]- 1);
	memory_table[row][4] = 1;
	memory_table[row][5] = thread_id;
}

void concatenate()
{
	int i, k;

	for(i = 0; i < count; i++) {
		//if(count != 1 && memory_table[i][4] == 0) {
		if(memory_table[i][4] == 0) {
			if(i != count - 1 && memory_table[i + 1][4] == 0) {
				memory_table[i][1] += memory_table[i + 1][1];
				memory_table[i][3] += memory_table[i + 1][1];
				count--;
				for(k = i + 1; k < count; k++) {
					// Shift memory table up
					memory_table[k][0] = memory_table[k - 1][0] + 1;
					memory_table[k][1] = memory_table[k + 1][1];
					memory_table[k][2] = memory_table[k - 1][3] + 1;
					memory_table[k][3] = memory_table[k + 1][3];
					memory_table[k][4] = memory_table[k + 1][4];
					memory_table[k][5] = memory_table[k + 1][5];					
				}
				i--;
			}
			else {
				i++;
			}
		}
	}
	//concatenate();
}

int* first_fit(int size_request, int thread_num) 
{
	int i;
	int* starting_ptr;

	printf("First Fit\n\n");
	for(i = 0; i < count; i++) {
		if(memory_table[i][4] != 1 && memory_table[i][1] >= size_request) {
			insert_memory_block(i, size_request, thread_num);
			starting_ptr = memory_block_ptr;
			return starting_ptr;	
		}
	}
}

int* memory_allocate(int size, int num) 
{
	int* starting_ptr;

	if(FUNCTION_NUM == 1) {
		starting_ptr = first_fit(size, num);
		if(starting_ptr != NULL) {
			//curr_mem_size += size;
			if(curr_mem_size == MAX_MEMORY_SIZE) {
				count--;
			}
		}
		return starting_ptr;
	}
}

void memory_deallocate(int size, int num)
{
	int i;

	printf("MEMORY FREE\n\n");
	for(i = 0; i < count; i++) {
		if(memory_table[i][5] == num) {
			memory_table[i][4] = 0;
			memory_table[i][5] = 0;
			curr_mem_size -= size;
			concatenate();
			break;			
		}
	}

}

void free_memory() 
{
	int i;

	for(i = 0; i < 4; i++) {
		if(user_th_buff[i % BUFFER_SIZE] != NULL && user_th_buff[i % BUFFER_SIZE]->wake_up != true) {
			user_th_buff[i % BUFFER_SIZE]->wake_up = true;
			printf("In free_memory() waking up Thread %d", user_th_buff[i % BUFFER_SIZE]->num_thread);
			//user_th_buff[i % BUFFER_SIZE] = NULL;
		}
	}
}

void *mms(void *arg)        // Starting function for MMS thread
{
	int j = 0, i = 0;
	int temp_size, temp_size2, temp_num, temp_num2;
	int *starting_ptr;

	while(i != THREAD_NUM) {				// Loop until all users have been serviced
		if(user_th_buff[j % BUFFER_SIZE] != NULL && user_th_buff[j % BUFFER_SIZE]->waiting == true) {
			temp_size = user_th_buff[j % BUFFER_SIZE]->mem_size;
			temp_num = user_th_buff[j % BUFFER_SIZE]->num_thread;
			if(temp_size + curr_mem_size > MAX_MEMORY_SIZE) {
				printf("NO SPACE AVAILABLE, NEED TO FREE MEMORY\n");
				free_memory();
				sleep(2);
			}
			else {
				printf("MMS: Receives request of %d memory space from Thread %d\n", temp_size, temp_num);
				starting_ptr = memory_allocate(temp_size, temp_num);
				//if(starting_ptr == NULL) {		// Means no large enough hole
				//	printf("HERE FIX\n");
				//}
				print_memory();
				user_th_buff[j % BUFFER_SIZE]->waiting = false;		// user no longer waiting once mms allocates the memory
				j++;
			}
		}	
		if(user_th_buff2[i % BUFFER_SIZE] != NULL && user_th_buff2[i % BUFFER_SIZE]->waiting == false) {
			temp_size2 = user_th_buff2[i % BUFFER_SIZE]->mem_size;
			temp_num2 = user_th_buff2[i % BUFFER_SIZE]->num_thread;
			printf("MMS: Receives deallocation request of %d memory space from Thread %d\n\n", temp_size2, temp_num2);
			memory_deallocate(temp_size2, temp_num2);
			print_memory();
			user_th_buff2[i % BUFFER_SIZE]->serviced = true;			
			i++;	
		}

	}
}

void *user(void *arg)       // Starting function for user threads
{
	struct user_thread_info* user_thread = malloc(sizeof(struct user_thread_info));
	user_thread->num_thread = *(int*)arg;
	user_thread->sleep_time = (rand() % 20) + 1;
	user_thread->mem_size = (rand() % MAX_MEMORY_SIZE / 4) + 1;
	if(user_thread->mem_size % 2 != 0) {
		user_thread->mem_size = user_thread->mem_size + 1;
	}
	user_thread->serviced = false;
	user_thread->waiting = true;
	user_thread->wake_up = false;

	// Request memory and leave loop after memory is deallocated
	sem_wait(&semEmpty);
	pthread_mutex_lock(&mutx);
	user_th_buff[position % BUFFER_SIZE] = user_thread;
	position++;
	pthread_mutex_unlock(&mutx);
	//sleep(1);
	printf("Thread %d: request %d memory space, going to sleep\n", user_thread->num_thread, user_thread->mem_size);
	sem_post(&semFull);
	while(user_thread->waiting) {}	// Wait for MMS to service request
	while(!user_thread->wake_up && user_thread->sleep_time != 0) {
		user_thread->sleep_time--;
	}
	printf("Thread %d waking up\n", user_thread->num_thread);
	pthread_mutex_lock(&mutx2);
	user_th_buff2[position2 % BUFFER_SIZE] = user_thread;			// Request memory to be deallocated
	position2++;
	pthread_mutex_unlock(&mutx2);
	while(!user_thread->serviced) {}			// Loop until MMS deallocates user memory
	//printf("About to free Thread %d\n", user_thread->num_thread);
	free(user_thread);
}

int main(int argc, char **argv)
{
	srand(time(NULL));	
	//THREAD_NUM = atoi(argv[1]);
	//FUNCTION_NUM = atoi(argv[2]);

	THREAD_NUM = 6;
	FUNCTION_NUM = 1;
	//defrag = atoi(argv[3]);
	int i;
	memory_block_ptr = malloc(MAX_MEMORY_SIZE * sizeof(int));		// Allocate block of memory
	pthread_t mms_th;
	pthread_t user_th[THREAD_NUM];
	int thread_args[THREAD_NUM];
	for(i = 0; i < THREAD_NUM; i++) {
		thread_args[i] = i + 1;
	}
	for(i = 0; i < BUFFER_SIZE; i++) {
		user_th_buff[i] = NULL;
		user_th_buff2[i] = NULL;
	}

	int state1, state2, state3, state4;
	state1 = pthread_mutex_init(&mutx, NULL);
	state4 = pthread_mutex_init(&mutx2, NULL);
    state2 = sem_init(&semEmpty, 0, BUFFER_SIZE);
    state3 = sem_init(&semFull, 0, 0);
	//mutex initialization
	//semaphore initialization, first value = 0

	if(state1||state2||state3||state4!=0) {
		puts("Error mutex & semaphore initialization!!!");
	}

	memory_table[0][0] = 1;
	memory_table[0][1] = MAX_MEMORY_SIZE;
	memory_table[0][2] = 0;
	memory_table[0][3] = MAX_MEMORY_SIZE - 1;
	memory_table[0][4] = 0;
	memory_table[0][5] = 0;
	printf("Memory Initial State\n\n");
	print_memory();

	// Create MMS Thread and THREAD_NUM User Threads
	if(pthread_create(&mms_th, NULL, mms, &mms_thread) != 0) {
        perror("Failed to create MMS thread");   
    }
	for(i = 0; i < THREAD_NUM; i++) {
		if(pthread_create(user_th + i, NULL, user, &thread_args[i]) != 0) {
            perror("Failed to create user thread");
        }  	
	}

	// Waiting thread to terminate
	if(pthread_join(mms_th, NULL) != 0) {
        perror("Failed to join MMS thread");
    }
	for(i = 0; i < THREAD_NUM; i++) {
		if(pthread_join(user_th[i], NULL) != 0) {
            perror("Failed to join user thread");
        }
	}

	sem_destroy(&semEmpty);         // Destroy semaphore
    sem_destroy(&semFull);	        // Destroy semaphore
	pthread_mutex_destroy(&mutx);	// Destroy mutex
	pthread_mutex_destroy(&mutx2);	// Destroy mutex
	return 0;
}
