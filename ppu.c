#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <libspe2.h>
#include <pthread.h>
#include <libmisc.h>
#include "common.h"

#define PRINT_ERR_MSG_AND_EXIT(format, ...) \
{ \
	fprintf(stderr, "%s:%d: " format, __func__, __LINE__, ##__VA_ARGS__); \
	fflush(stderr); \
	exit(1); \
}

//macro for easily getting how much time has passed between two events
#define GET_TIME_DELTA(t1, t2) ((t2).tv_sec - (t1).tv_sec + \
		((t2).tv_usec - (t1).tv_usec) / 1000000.0)

extern spe_program_handle_t spu;
spe_context_ptr_t spes[MAX_SPU_THREADS];
int num_spu_threads, mode;

void *ppu_pthread_function(void *thread_arg) {
	unsigned int entry = SPE_DEFAULT_ENTRY;
	if (spe_context_run(spes[(int)thread_arg], &entry, 0, (void*)num_spu_threads ,
		(void*) mode, NULL) < 0) {
		perror ("Failed running context");
		exit (1);
	}

	pthread_exit(NULL);
}


//read a character from a file specified by a descriptor
char read_char(int fd, char* path){
	char c;
	int bytes_read;

	bytes_read = read(fd, &c, 1);
	if (bytes_read != 1){
		PRINT_ERR_MSG_AND_EXIT("Error reading from %s\n", path);
	}

	return c;
}

//allocate image data
void alloc_image(struct image* img){		
	img->src = calloc_align(NUM_CHANNELS * img->width * img->height, sizeof(char), 4);

	if (!img->src){
		PRINT_ERR_MSG_AND_EXIT("Calloc failed\n");
	}
}

//free image data
void free_image(struct image* img){
	free_align(img->src);
}

/* read from fd until character c is found
 * result will be atoi(str) where str is what was read before c was
 * found 
 */
unsigned int read_until(int fd, char c, char* path){

	char buf[SMALL_BUF_SIZE];
	int i;
	unsigned int res;

	i = 0;
	memset(buf, 0, SMALL_BUF_SIZE);
	buf[i] = read_char(fd, path);
	while (buf[i] != c){
		i++;
		if (i >= SMALL_BUF_SIZE){
			PRINT_ERR_MSG_AND_EXIT("Unexpected file format for %s\n", path);
		}
		buf[i] = read_char(fd, path);
	}
	res = atoi(buf);
	if (res <= 0) {
		PRINT_ERR_MSG_AND_EXIT("Result is %d when reading from %s\n", 
				res, path);
	}

	return res;
}

//read a pnm image
void read_pnm(char* path, struct image* img){
	int fd, bytes_read, bytes_left;
	char image_type[IMAGE_TYPE_LEN];
	unsigned char *ptr;
	unsigned int max_color;

	fd = open(path, O_RDONLY);

	if (fd < 0){
		PRINT_ERR_MSG_AND_EXIT("Error opening %s\n", path);
		exit(1);
	}

	//read image type; should be P6
	bytes_read = read(fd, image_type, IMAGE_TYPE_LEN);
	if (bytes_read != IMAGE_TYPE_LEN){
		PRINT_ERR_MSG_AND_EXIT("Couldn't read image type for %s\n", path);
	}
	if (strncmp(image_type, "P6", IMAGE_TYPE_LEN)){
		PRINT_ERR_MSG_AND_EXIT("Expecting P6 image type for %s. Got %s\n", 
				path, image_type);
	}

	//read \n
	read_char(fd, path);

	//read width, height and max color value
	img->width = read_until(fd, ' ', path);
	img->height = read_until(fd, '\n', path);
	max_color = read_until(fd, '\n', path);
	if (max_color != MAX_COLOR){
		PRINT_ERR_MSG_AND_EXIT("Unsupported max color value %d for %s\n", 
				max_color, path);
	}

	//allocate image data
	alloc_image(img);

	//read the actual data 
	bytes_left = img->width * img->height * NUM_CHANNELS;
	ptr = img->src;
	while (bytes_left > 0){
		bytes_read = read(fd, ptr, bytes_left);
		if (bytes_read <= 0){
			PRINT_ERR_MSG_AND_EXIT("Error reading from %s\n", path);
		}
		ptr += bytes_read;
		bytes_left -= bytes_read;
	}

	close(fd);
}

//write a pnm image
void write_pnm(char* path, struct image* img){
	int fd, bytes_written, bytes_left;
	char buf[32];
	unsigned char* ptr;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0){
		PRINT_ERR_MSG_AND_EXIT("Error opening %s\n", path);
	}

	//write image type, image width, height and max color
	sprintf(buf, "P6\n%d %d\n%d\n", img->width, img->height, MAX_COLOR);
	ptr = (unsigned char*)buf;
	bytes_left = strlen(buf);
	while (bytes_left > 0){
		bytes_written = write(fd, ptr, bytes_left);
		if (bytes_written <= 0){
			PRINT_ERR_MSG_AND_EXIT("Error writing to %s\n", path);
		}
		bytes_left -= bytes_written;
		ptr += bytes_written;
	}

	//write the actual data
	ptr = img->src;
	bytes_left = img->width * img->height * NUM_CHANNELS;
	while (bytes_left > 0){
		bytes_written = write(fd, ptr, bytes_left);
		if (bytes_written <= 0){
			PRINT_ERR_MSG_AND_EXIT("Error writing to %s\n", path);
		}
		bytes_left -= bytes_written;
		ptr += bytes_written;
	}

	close(fd);
}

int main(int argc, char** argv){
	int i, j, num_frames, num_ev, total_ev;
	char buf[MAX_PATH_LEN];
	char input_path[MAX_PATH_LEN];
	char output_path[MAX_PATH_LEN];
	struct image input[NUM_STREAMS] __attribute__ ((aligned(16)));
	struct image big_image __attribute__ ((aligned(16)));
	struct timeval t1, t2, t3, t4;
	double scale_time = 0, total_time = 0;
	pthread_t threads[MAX_SPU_THREADS];
	unsigned int data;
	spe_event_unit_t pevents[MAX_SPU_THREADS], events_received[MAX_SPU_THREADS];
	spe_event_handler_ptr_t event_handler;

	if (argc != 6){
		printf("Usage: ./serial input_path output_path num_frames num_spu_threads mode\n");
		printf("Available modes:\n\t0=simple\n\t1=2lines\n\t2=double\n\t3=dmalist\n");
		exit(1);
	}

	gettimeofday(&t3, NULL);
	strncpy(input_path, argv[1], MAX_PATH_LEN - 1);
	strncpy(output_path, argv[2], MAX_PATH_LEN - 1);
	num_frames = atoi(argv[3]);
	num_spu_threads = atoi(argv[4]);
	mode = atoi(argv[5]);

	if (num_spu_threads > MAX_SPU_THREADS)
		num_spu_threads = MAX_SPU_THREADS;
	if (NUM_STREAMS % num_spu_threads){
		perror("The number of streams should be a multiple of the number of SPU threads\n");
		exit(1);
	}
	if (num_frames > MAX_FRAMES)
		num_frames = MAX_FRAMES;

	for (i=0; i<num_spu_threads; i++){
		if ((spes[i] = spe_context_create (SPE_EVENTS_ENABLE, NULL)) == NULL) {
			perror ("Failed creating context");
			exit (1);
		}
		if (spe_program_load (spes[i], &spu)) {
			perror ("Failed loading program");
			exit (1);
		}
	}

	event_handler = spe_event_handler_create();
	for (i =0; i < num_spu_threads; i++){
		pevents[i].spe = spes[i];
		pevents[i].events = SPE_EVENT_OUT_INTR_MBOX;
		pevents[i].data.u32 = i; // just some data to pass
		spe_event_handler_register(event_handler, &pevents[i]);
	}

	for(i = 0; i < num_spu_threads; i++) {
		if (pthread_create (&threads[i], NULL, &ppu_pthread_function, (void*)i))  {
			perror ("Failed creating thread");
			exit (1);
		}
	}

	for (i = 0; i < num_frames; i++){
		printf("Processing Frame %d\n", i + 1);

		//read the input images
		for (j = 0; j < NUM_STREAMS; j++){
			sprintf(buf, "%s/stream%02d/image%d.pnm", input_path, 
					j + 1, i + 1);
			read_pnm(buf, &input[j]);
		}

		gettimeofday(&t1, NULL);

		//allocate big image
		big_image.height = input[0].height;
		big_image.width = input[0].width;
		alloc_image(&big_image);

		for (j = 0; j < NUM_STREAMS; j++){
			input[j].dst = big_image.src;
			input[j].block_nr = j;
		}

		//scale input images and create big images in parallel
		//send image address to each SPU
		for (j = 0; j < NUM_STREAMS; j++){
			data = (unsigned int) &input[j];
			spe_in_mbox_write(spes[j % num_spu_threads], &data, 1, SPE_MBOX_ALL_BLOCKING);
		}
		//wait for all SPUs to finish
		total_ev = 0;
		while(total_ev < num_spu_threads){
			num_ev = spe_event_wait(event_handler, events_received, num_spu_threads, -1);
			total_ev += num_ev;
			for (j =0; j < num_ev; j++) {
				if (events_received[j].events & SPE_EVENT_OUT_INTR_MBOX) {
					spe_out_intr_mbox_read(events_received[j].spe, (unsigned int*) &data, 1, SPE_MBOX_ALL_BLOCKING);
				}
				else {
					perror("Unexpected event\n");
					exit(1);
				}
			}
		}
		gettimeofday(&t2, NULL);
		scale_time += GET_TIME_DELTA(t1, t2);

		//write the big image
		sprintf(buf, "%s/result%d.pnm", output_path, i + 1);
		write_pnm(buf, &big_image);

		//free the image data
		for (j = 0; j < NUM_STREAMS; j++){
			free_image(&input[j]);
		}
		free_image(&big_image);
	}
	
	for (i=0; i<num_spu_threads; i++){
		spe_event_handler_deregister(event_handler, &pevents[i]);
	}
	spe_event_handler_destroy(event_handler);

	/* signal spu_threads to finish */
	for (i = 0; i < num_spu_threads; i++){
		data = 0;
		spe_in_mbox_write(spes[i], &data, 1, SPE_MBOX_ALL_BLOCKING); 
	}

	for (i =0; i < num_spu_threads; i++)
		pthread_join(threads[i], NULL);

	for (i=0; i<num_spu_threads; i++){
		/* Destroy context */
		if (spe_context_destroy(spes[i]) != 0) {
			perror("Failed destroying context");
			exit (1);
		}

	}

	gettimeofday(&t4, NULL);
	total_time += GET_TIME_DELTA(t3, t4);

	printf("Scale time: %lf\n", scale_time);
	printf("Total time: %lf\n", total_time);

	return 0;
}
