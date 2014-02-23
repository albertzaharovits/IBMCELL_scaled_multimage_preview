#define NUM_STREAMS             16      
#define MAX_FRAMES              100     //there are at most 100 frames available
#define MAX_PATH_LEN            256
#define IMAGE_TYPE_LEN          2
#define SMALL_BUF_SIZE          16
#define SCALE_FACTOR            4
#define NUM_CHANNELS            3 //red, green and blue
#define MAX_COLOR               255
#define NUM_IMAGES_HEIGHT	4
#define NUM_IMAGES_WIDTH	4
#define MAX_SPU_THREADS 	8

#define MODE_SIMPLE		0
#define MODE_2LINES		1
#define MODE_DOUBLE		2
#define MODE_DMALIST	3

struct image{
	unsigned int width, height;
	unsigned char *src;
	//for parallel implementation
	unsigned char *dst;
	unsigned int block_nr;
	char padding[12]; //pad to 32 bytes
};

/* data layout for an image:
 * if RED_i, GREEN_i, BLUE_i are the red, green and blue values for 
 * the i-th pixel in the image than the data array inside struct image
 * looks like this:
 * RED_0 GREEN_0 BLUE_0 RED_1 GREEN_1 BLUE_1 RED_2 ...
 */


