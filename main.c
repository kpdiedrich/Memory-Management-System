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
#define BUFFER_SIZE 10

sem_t buff;         // Semaphore for # of empty spots in buffer
sem_t buff2;

pthread_mutex_t mutx;	// Mutex
pthread_mutex_t mutx2;	// Mutex
pthread_mutex_t print;

int THREAD_NUM;			// # of threads requesting memory from the MMS thread 
int FUNCTION_NUM;		// 1 = first fit, 2 = best fit, 3 = worst fit 
int DEFRAGMENT;			// 0 = defragmentation disabled, 1 = defragmentation enabled

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
	pthread_mutex_lock(&print);
	printf("----------MEMORY STATUS----------\n\n");
	printf("Block\tSize\tStart\tEnd\tFlag\tThread\n");
	for(i = 0; i < count; i++) {
		for(int j = 0; j < 6; j++) {
			printf("%d\t", memory_table[i][j]);
		}
		printf("\n");		
	}
	printf("\nNumber of Blocks = %d\n\n", count);
	pthread_mutex_unlock(&print);
}

void insert_memory_block(int row, int size, int thread_id)
{
	int i;
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
}

int* first_fit(int size_request, int thread_num) 
{
	int i;
	int* starting_ptr = NULL;

	printf("First Fit\n\n");
	for(i = 0; i < count; i++) {
		if(memory_table[i][4] != 1 && memory_table[i][1] >= size_request) {
			insert_memory_block(i, size_request, thread_num);
			starting_ptr = memory_block_ptr;
			return starting_ptr;	
		}
	}
	return starting_ptr;
}

int* best_fit(int size_request, int thread_num)
{
	int i, smallest = MAX_MEMORY_SIZE + 1, mem_index;
	int* starting_ptr = NULL;

	printf("Best Fit\n\n");
	for(i = 0; i < count; i++) {
		if(memory_table[i][4] != 1 && memory_table[i][1] >= size_request) {
			if(memory_table[i][1] < smallest) {
				smallest = memory_table[i][1];
				mem_index = i;
				starting_ptr = memory_block_ptr;
			}
		}
	}

	if(smallest == MAX_MEMORY_SIZE + 1) {
		return NULL;
	}
	insert_memory_block(mem_index, size_request, thread_num);
	return starting_ptr;	
}

int* worst_fit(int size_request, int thread_num)
{
	int i, largest = -1, mem_index;
	int* starting_ptr = NULL;
	
	printf("Worst Fit\n\n");
	for(i = 0; i < count; i++) {
		if(memory_table[i][4] != 1 && memory_table[i][1] >= size_request) {
			if(memory_table[i][1] > largest) {
				largest = memory_table[i][1];
				mem_index = i;
				starting_ptr = memory_block_ptr;
			}
		}
	}

	if(largest == -1) {
		return NULL;
	}
	insert_memory_block(mem_index, size_request, thread_num);
	return starting_ptr;
}

void defragment()
{
	int i, j, k;
	int temp_size, temp_num, start;
	bool enter_loop = false;

	for(i = 0; i < count - 1; i++) {
		if(memory_table[i][5] == 0) {
			temp_num = memory_table[i][0];
			temp_size = memory_table[i][1];
			start = i;
			enter_loop = true;
			break;
		}
	}

	if(enter_loop) {
		i = 0;
		if(memory_table[count - 1][5] == 0) {
			memory_table[count - 1][1] += memory_table[start][1];
			for(k = start; k < count - 1; k++) {
				//Shift memory table up
				memory_table[k][0] = temp_num + i;
				i++;
				memory_table[k][1] = memory_table[k + 1][1];
				memory_table[k][2] = memory_table[k + 1][2] - temp_size;
				memory_table[k][3] = memory_table[k + 1][3] - temp_size;
				memory_table[k][4] = memory_table[k + 1][4];
				memory_table[k][5] = memory_table[k + 1][5];	
			}
			count--;
			memory_table[count - 1][0] = count;
			memory_table[count - 1][2] = memory_table[count - 2][3] + 1;
			memory_table[count - 1][3] = memory_table[count - 1][2] + (memory_table[count - 1][1] - 1);
			memory_table[count - 1][4] = 0;
			memory_table[count - 1][5] = 0;		
		}
		else {
			for(k = start; k < count - 1; k++) {
				memory_table[k][0] = temp_num + i;
				i++;
				memory_table[k][1] = memory_table[k + 1][1];
				memory_table[k][2] = memory_table[k + 1][2] - temp_size;
				memory_table[k][3] = memory_table[k + 1][3] - temp_size;
				memory_table[k][4] = memory_table[k + 1][4];
				memory_table[k][5] = memory_table[k + 1][5];
			}
			memory_table[count - 1][0] = count;
			memory_table[count - 1][1] = temp_size;
			memory_table[count - 1][2] = memory_table[count - 2][3] + 1;
			memory_table[count - 1][3] = memory_table[count - 1][2] + (temp_size - 1);
			memory_table[count - 1][4] = 0;
			memory_table[count - 1][5] = 0;		
		}
	}
	printf("COMPACTION RESULT\n\n");
}

int* memory_allocate(int size, int num) 
{
	int* starting_ptr;

	if(FUNCTION_NUM == 1) {
		starting_ptr = first_fit(size, num);
		if(starting_ptr != NULL) {
			curr_mem_size += size;
		}
		return starting_ptr;
	}

	else if(FUNCTION_NUM == 2) {
		starting_ptr = best_fit(size, num);
		if(starting_ptr != NULL) {
			curr_mem_size += size;
		}
		return starting_ptr;		
	}

	else {
		starting_ptr = worst_fit(size, num);
		if(starting_ptr != NULL) {
			curr_mem_size += size;
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

	for(i = 0; i < THREAD_NUM; i++) {
		if(user_th_buff[i % BUFFER_SIZE] != NULL && user_th_buff[i % BUFFER_SIZE]->serviced != true && user_th_buff[i % BUFFER_SIZE]->waiting != true) {
			user_th_buff[i % BUFFER_SIZE]->wake_up = true;
			printf("In free_memory() waking up Thread %d\n", user_th_buff[i % BUFFER_SIZE]->num_thread);
		}
	}
}

void *mms(void *arg)        // Starting function for MMS thread
{
	int i, j;
	int buff_count = 0;
	int buff2_count = 0;
	int temp_size, temp_size2, temp_num, temp_num2;
	int *starting_ptr;

	while(buff2_count != THREAD_NUM) {				// Loop until all user memory has been deallocated

		for(i = 0; i < BUFFER_SIZE; i++) {
			if(user_th_buff[i] != NULL && user_th_buff[i]-> waiting == true) {
				temp_size = user_th_buff[i]->mem_size;
				temp_num = user_th_buff[i]->num_thread;
				printf("MMS: Receives request of %d memory space from Thread %d\n\n", temp_size, temp_num);
				starting_ptr = memory_allocate(temp_size, temp_num);
				if(starting_ptr == NULL) {		// Means no large enough hole
					printf("NO SPACE AVAILABLE FOR THREAD %d, NEED TO FREE MEMORY\n", temp_num);
					free_memory();
					buff_count--;
					sleep(2);
					break;
				}
				else {
					print_memory();
					user_th_buff[i]->waiting = false;		// user no longer waiting once mms allocates the memory
					user_th_buff[i] = NULL;	
					sem_post(&buff);
					buff_count++;				
					break;					
				}
			}
		}

		for(j = 0; j < BUFFER_SIZE; j++) {
			if(user_th_buff2[j] != NULL) {
				temp_size2 = user_th_buff2[j]->mem_size;
				temp_num2 = user_th_buff2[j]->num_thread;
				printf("MMS: Receives deallocation request of %d memory space from Thread %d\n\n", temp_size2, temp_num2);
				memory_deallocate(temp_size2, temp_num2);
				print_memory();	
				if(DEFRAGMENT == 1) {
					defragment();
					print_memory();
				}
				user_th_buff2[j]->serviced = true;
				user_th_buff2[j] = NULL;
				sem_post(&buff2);
				buff2_count++;						
				break;
			}
		}
	}
}

void *user(void *arg)       // Starting function for user threads
{
	struct user_thread_info* user_thread = malloc(sizeof(struct user_thread_info));
	user_thread->num_thread = *(int*)arg;
	user_thread->sleep_time = (rand() % 1000) + 10000;
	user_thread->mem_size = (rand() % MAX_MEMORY_SIZE / 4) + 1;
	if(user_thread->mem_size % 2 != 0) {
		user_thread->mem_size = user_thread->mem_size + 1;
	}
	user_thread->serviced = false;
	user_thread->waiting = true;
	user_thread->wake_up = false;

	// Request memory and leave loop after allocated memory is deallocated
	sem_wait(&buff);
	pthread_mutex_lock(&mutx);
	while(user_th_buff[position % BUFFER_SIZE] != NULL) {		// Search for open element in buffer
		position++;
	}
	pthread_mutex_lock(&print);
	printf("Thread %d: request %d memory space, going to sleep\n", user_thread->num_thread, user_thread->mem_size);
	pthread_mutex_unlock(&print);
	user_th_buff[position % BUFFER_SIZE] = user_thread;
	pthread_mutex_unlock(&mutx);
	while(user_thread->waiting) {}	// Wait for MMS to service request, once become false, user has been allocated memory
	while(!user_thread->wake_up && user_thread->sleep_time != 0) {
		user_thread->sleep_time--;
	}
	pthread_mutex_lock(&print);
	printf("Thread %d waking up\n", user_thread->num_thread);
	pthread_mutex_unlock(&print);
	sem_wait(&buff2);		// Decrement buff2
	pthread_mutex_lock(&mutx2);
	//sleep(1);
	while(user_th_buff2[position2 % BUFFER_SIZE] != NULL) {		// Search for open element in buffer
		position2++;
	}
	user_th_buff2[position2 % BUFFER_SIZE] = user_thread;			// Request memory to be deallocated
	pthread_mutex_unlock(&mutx2);
	while(!user_thread->serviced) {}			// Loop until MMS deallocates user memory
	free(user_thread);
}

int main(int argc, char **argv)
{
	srand(time(NULL));	
	THREAD_NUM = atoi(argv[1]);
	FUNCTION_NUM = atoi(argv[2]);
	DEFRAGMENT = atoi(argv[3]);
	//THREAD_NUM = 12;
	//FUNCTION_NUM = 3;
	//DEFRAGMENT = 1;
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

	int state1, state2, state4, state5, state6;
	state1 = pthread_mutex_init(&mutx, NULL);
	state4 = pthread_mutex_init(&mutx2, NULL);
	state6 = pthread_mutex_init(&print, NULL);
	state2 = sem_init(&buff, 0, BUFFER_SIZE);
	state5 = sem_init(&buff2, 0, BUFFER_SIZE);
	//mutex initialization
	//semaphore initialization, first value = 0

	if((state1 || state2 || state4 || state5) != 0) {
		puts("Error mutex & semaphore initialization!!!");
	}

	// Initialize Memory
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

	sem_destroy(&buff);         // Destroy semaphore
	sem_destroy(&buff2);		// Destroy semaphore
	pthread_mutex_destroy(&mutx);	// Destroy mutex
	pthread_mutex_destroy(&mutx2);	// Destroy mutex
	pthread_mutex_destroy(&print);	// Destroy mutex
	return 0;
}
