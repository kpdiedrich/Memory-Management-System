
all:
	gcc main.c -pthread -o main_run
	
clean:
	-rm *.o $(objects)
