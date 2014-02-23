#include <spu_mfcio.h>
#include <stdio.h>
#include <libmisc.h>
#include <string.h>
#include <unistd.h>

#include "../common.h"

#define MY_TAG 5
#define DONE 1

struct dma_list_elem {
	union {
		unsigned int all32;
		struct {
			unsigned int stall		: 1;
			unsigned int reserved	: 15;
			unsigned int nbytes		: 16;
		} bits;
	} size;
	unsigned int ea_low;
};

struct dma_list_elem list0[4] __attribute__ ((aligned (16)));
struct dma_list_elem list1[4] __attribute__ ((aligned (16)));

struct dma_list_elem out_list0[8] __attribute__ ((aligned (16)));
struct dma_list_elem out_list1[8] __attribute__ ((aligned (16)));

struct dma_list_elem *p_list[2];
struct dma_list_elem *p_out_list[2];
struct dma_list_elem *list;
struct dma_list_elem *out_list;

typedef vector unsigned char __attribute__((__may_alias__)) vuc;

/* straight forward scaling of SCALE_FACTOR(4) full rows into 1 scaled row */
void process_image_simple(struct image* img) {
	unsigned char *input, *output, *temp;
	unsigned int addr1, addr2, i, j, k, r, g, b;
  /* index in the preview result image */
	int block_nr = img->block_nr;
	vector unsigned char *v1, *v2, *v3, *v4, *v5 ;

  /* SCALE_FACTOR full rows input buffer in local storage */
	input = malloc_align(NUM_CHANNELS * SCALE_FACTOR * img->width, 4);
  /* 1 scalled row output buffer in local storage */
	output = malloc_align(NUM_CHANNELS * img->width / SCALE_FACTOR, 4);
  /* 1 full row temporary buffer in local storage */
	temp = malloc_align(NUM_CHANNELS * img->width, 4);

	v1 = (vector unsigned char *) &input[0];
	v2 = (vector unsigned char *) &input[1 * img->width * NUM_CHANNELS];
	v3 = (vector unsigned char *) &input[2 * img->width * NUM_CHANNELS];
	v4 = (vector unsigned char *) &input[3 * img->width * NUM_CHANNELS];
	v5 = (vector unsigned char *) temp;

  /* address in the preview result image */
	addr2 = (unsigned int)img->dst; // start of preview image
	addr2 += (block_nr / NUM_IMAGES_HEIGHT) * img->width * NUM_CHANNELS * 
		img->height / NUM_IMAGES_HEIGHT; // start line for current spu block
	addr2 += (block_nr % NUM_IMAGES_WIDTH) * NUM_CHANNELS *
		img->width / NUM_IMAGES_WIDTH;

	for (i=0; i<img->height / SCALE_FACTOR; i++){
		/* address in main storage for current chunck */
		addr1 = ((unsigned int)img->src) + i * img->width * NUM_CHANNELS * SCALE_FACTOR;
    /* get SCALE_FACTOR(4) full rows at a time */
		mfc_get(input, addr1, SCALE_FACTOR * img->width * NUM_CHANNELS, MY_TAG, 0, 0);
    /* wait for transfer */
		mfc_write_tag_mask(1 << MY_TAG);
		mfc_read_tag_status_all();

		/* average the 4 full lines into one */
		for (j = 0; j < img->width * NUM_CHANNELS / 16; j++){
			v5[j] = spu_avg(spu_avg(v1[j], v2[j]), spu_avg(v3[j], v4[j]));
		}
    /* average 1 full line into 1 scaled line */
		for (j=0; j < img->width; j+=SCALE_FACTOR){
			r = g = b = 0;
			for (k = j; k < j + SCALE_FACTOR; k++) {
				r += temp[k * NUM_CHANNELS + 0];
				g += temp[k * NUM_CHANNELS + 1];
				b += temp[k * NUM_CHANNELS + 2];
			}
			r /= SCALE_FACTOR;
			b /= SCALE_FACTOR;
			g /= SCALE_FACTOR;

			output[j / SCALE_FACTOR * NUM_CHANNELS + 0] = (unsigned char) r;
			output[j / SCALE_FACTOR * NUM_CHANNELS + 1] = (unsigned char) g;
			output[j / SCALE_FACTOR * NUM_CHANNELS + 2] = (unsigned char) b;
		}

		/* put the scaled line in the result preview image */
		mfc_put(output, addr2, img->width / SCALE_FACTOR * NUM_CHANNELS, MY_TAG, 0, 0);
    /* update global storage address for the next scaled row */
		addr2 += img->width * NUM_CHANNELS; 
    /* wait for transfer */
		mfc_write_tag_mask(1 << MY_TAG);
		mfc_read_tag_status_all();
	}

	free_align(temp);
	free_align(input);
	free_align(output);
}

/* scaling of 8(SCALE_FACTOR*2) full rows into 2 scaled rows
 * buffers 2 x process_image_simple
 */
void process_image_2lines(struct image* img){
	unsigned char *input[2], *output[2], *temp[2];
	unsigned int addr1, addr2, i, j, k, r, g, b;
	vector unsigned char *v[10];
	int block_nr = img->block_nr;

  /* 2 x SCALE_FACTOR full rows input buffer in local storage */
	input[0] = malloc_align(NUM_CHANNELS * SCALE_FACTOR * img->width, 4);
	input[1] = malloc_align(NUM_CHANNELS * SCALE_FACTOR * img->width, 4);
  /* 2 x 1 scalled row output buffer in local storage */
	output[0] = malloc_align(NUM_CHANNELS * img->width / SCALE_FACTOR, 4);
	output[1] = malloc_align(NUM_CHANNELS * img->width / SCALE_FACTOR, 4);
  /* 2 x 1 full row temporary buffer in local storage */
	temp[0] = malloc_align(NUM_CHANNELS * img->width, 4);
	temp[1] = malloc_align(NUM_CHANNELS * img->width, 4);

	v[0] = (vector unsigned char *) &input[0][0];
	v[1] = (vector unsigned char *) &input[0][img->width * NUM_CHANNELS];
	v[2] = (vector unsigned char *) &input[0][2 * img->width * NUM_CHANNELS];
	v[3] = (vector unsigned char *) &input[0][3 * img->width * NUM_CHANNELS];
	v[4] = (vector unsigned char *) temp[0];
	v[5] = (vector unsigned char *) &input[1][0];
	v[6] = (vector unsigned char *) &input[1][img->width * NUM_CHANNELS];
	v[7] = (vector unsigned char *) &input[1][2 * img->width * NUM_CHANNELS];
	v[8] = (vector unsigned char *) &input[1][3 * img->width * NUM_CHANNELS];
	v[9] = (vector unsigned char *) temp[1];

  /* address in the preview result image */
	addr2 = (unsigned int)img->dst; //start of preview image
	addr2 += (block_nr / NUM_IMAGES_WIDTH) * img->width * NUM_CHANNELS * 
		img->height / NUM_IMAGES_HEIGHT; //start line for current spu block
	addr2 += (block_nr % NUM_IMAGES_WIDTH) * NUM_CHANNELS *
		img->width / NUM_IMAGES_WIDTH;

	addr1 = ((unsigned int)img->src);
	for (i=0; i<(img->height / SCALE_FACTOR); i+=2){
		/* get 8 lines (4 lines in input[0], 4 lines in input[1]) form main storage */
		mfc_get(input[0], addr1, SCALE_FACTOR * img->width * NUM_CHANNELS, MY_TAG, 0, 0);
		addr1 += img->width * NUM_CHANNELS * SCALE_FACTOR;
		mfc_get(input[1], addr1, SCALE_FACTOR * img->width * NUM_CHANNELS, MY_TAG, 0, 0);
		addr1 += img->width * NUM_CHANNELS * SCALE_FACTOR;

    /* wait for transfer to complete */
		mfc_write_tag_mask(1 << MY_TAG);
		mfc_read_tag_status_all();

		/* average the 8 full lines into 2 */
		for (j = 0; j < img->width * NUM_CHANNELS / 16; j++){
			v[4][j] = spu_avg(spu_avg(v[0][j], v[1][j]), spu_avg(v[2][j], v[3][j]));
			v[9][j] = spu_avg(spu_avg(v[5][j], v[6][j]), spu_avg(v[7][j], v[8][j]));
		}

    /* average 2 full lines into 2 scaled lines */
		for (j=0; j < img->width; j+=SCALE_FACTOR){
			r = g = b = 0;
			for (k = j; k < j + SCALE_FACTOR; k++) {
				r += temp[0][k * NUM_CHANNELS + 0];
				g += temp[0][k * NUM_CHANNELS + 1];
				b += temp[0][k * NUM_CHANNELS + 2];
			}
			r /= SCALE_FACTOR;
			b /= SCALE_FACTOR;
			g /= SCALE_FACTOR;

			output[0][j / SCALE_FACTOR * NUM_CHANNELS + 0] = (unsigned char) r;
			output[0][j / SCALE_FACTOR * NUM_CHANNELS + 1] = (unsigned char) g;
			output[0][j / SCALE_FACTOR * NUM_CHANNELS + 2] = (unsigned char) b;

			r = g = b = 0;
			for (k = j; k < j + SCALE_FACTOR; k++) {
				r += temp[1][k * NUM_CHANNELS + 0];
				g += temp[1][k * NUM_CHANNELS + 1];
				b += temp[1][k * NUM_CHANNELS + 2];
			}
			r /= SCALE_FACTOR;
			b /= SCALE_FACTOR;
			g /= SCALE_FACTOR;

			output[1][j / SCALE_FACTOR * NUM_CHANNELS + 0] = (unsigned char) r;
			output[1][j / SCALE_FACTOR * NUM_CHANNELS + 1] = (unsigned char) g;
			output[1][j / SCALE_FACTOR * NUM_CHANNELS + 2] = (unsigned char) b;
		}

		/* put the first scaled line in the result preview image */
		mfc_put(output[0], addr2, img->width / SCALE_FACTOR * NUM_CHANNELS, MY_TAG, 0, 0);
    /* update global storage address for the next scaled row */
		addr2 += img->width * NUM_CHANNELS;
		/* put the second scaled line in the result preview image */
		mfc_put(output[1], addr2, img->width / SCALE_FACTOR * NUM_CHANNELS, MY_TAG, 0, 0);
    /* update global storage address for the next scaled row */
		addr2 += img->width * NUM_CHANNELS; 

    /* wait for transfer to complete */
		mfc_write_tag_mask(1<<MY_TAG);
		mfc_read_tag_status_all();
	}

	free_align(temp[0]);
	free_align(input[0]);
	free_align(output[0]);
	free_align(temp[1]);
	free_align(input[1]);
	free_align(output[1]);
}

/* straight forward double buffer technique */
void process_image_double(struct image* img){
	unsigned char *input[2], *output[2], *temp[2];
  /* buf, next_buf are double buffer indices */
	unsigned int addr1, addr2, i, j, k, r, g, b, buf=0, next_buf=1, buf_trick;
	vector unsigned char *v[10];
	int block_nr = img->block_nr;

	/* allocating input, output, temp buffers */
  /* 2 x SCALE_FACTOR full rows input buffer in local storage */
	input[0] = malloc_align(NUM_CHANNELS * SCALE_FACTOR * img->width, 4);
	input[1] = malloc_align(NUM_CHANNELS * SCALE_FACTOR * img->width, 4);
  /* 2 x 1 scalled row output buffer in local storage */
	output[0] = malloc_align(NUM_CHANNELS * img->width / SCALE_FACTOR, 4);
	output[1] = malloc_align(NUM_CHANNELS * img->width / SCALE_FACTOR, 4);
  /* 2 x 1 full row temporary buffer in local storage */
	temp[0] = malloc_align(NUM_CHANNELS * img->width, 4);
	temp[1] = malloc_align(NUM_CHANNELS * img->width, 4);

	/* unrolled vector pointers alliasing */
	v[0] = (vector unsigned char *) &input[0][0];
	v[1] = (vector unsigned char *) &input[0][img->width * NUM_CHANNELS];
	v[2] = (vector unsigned char *) &input[0][2 * img->width * NUM_CHANNELS];
	v[3] = (vector unsigned char *) &input[0][3 * img->width * NUM_CHANNELS];
	v[4] = (vector unsigned char *) temp[0];
	v[5] = (vector unsigned char *) &input[1][0];
	v[6] = (vector unsigned char *) &input[1][img->width * NUM_CHANNELS];
	v[7] = (vector unsigned char *) &input[1][2 * img->width * NUM_CHANNELS];
	v[8] = (vector unsigned char *) &input[1][3 * img->width * NUM_CHANNELS];
	v[9] = (vector unsigned char *) temp[1];

  /* address in the preview result image */
	addr2 = (unsigned int)img->dst; //start of image
	addr2 += (block_nr / NUM_IMAGES_WIDTH) * img->width * NUM_CHANNELS * 
		img->height / NUM_IMAGES_HEIGHT; //start line of spu block
	addr2 += (block_nr % NUM_IMAGES_WIDTH) * NUM_CHANNELS *
		img->width / NUM_IMAGES_WIDTH;

	/* pointer where to get data from (to_scale input image) */
	addr1 = ((unsigned int)img->src);

	/* make request for buf data from main storage */
	mfc_get(input[buf], addr1, SCALE_FACTOR * img->width * NUM_CHANNELS, MY_TAG+buf, 0, 0);
  /* increment input pointer in main storage */
	addr1 += img->width * NUM_CHANNELS * SCALE_FACTOR;

	for (i=0; i<(img->height / SCALE_FACTOR)-1; i++){
    /* make request for next_buf data from main storage */
		mfc_get(input[next_buf], addr1, SCALE_FACTOR * img->width * NUM_CHANNELS, MY_TAG+next_buf, 0, 0);
		addr1 += img->width * NUM_CHANNELS * SCALE_FACTOR;

		/* wait for buf requested data */
		mfc_write_tag_mask(1 << (MY_TAG+buf));
		mfc_read_tag_status_all();

		/* average the received lines into 1 */
		buf_trick = (buf<<2) + buf;
		for (j = 0; j < img->width * NUM_CHANNELS / 16; j++) {
			v[4+buf_trick][j] = spu_avg(spu_avg(v[0+buf_trick][j], v[1+buf_trick][j]), 
					spu_avg(v[2+buf_trick][j], v[3+buf_trick][j]));
		}

    /* scale the averaged line from the temporary buffer */
		for (j=0; j < img->width; j+=SCALE_FACTOR){
			r = g = b = 0;
			for (k = j; k < j + SCALE_FACTOR; k++) {
				r += temp[buf][k * NUM_CHANNELS + 0];
				g += temp[buf][k * NUM_CHANNELS + 1];
				b += temp[buf][k * NUM_CHANNELS + 2];
			}
			r /= SCALE_FACTOR;
			b /= SCALE_FACTOR;
			g /= SCALE_FACTOR;

			output[buf][j / SCALE_FACTOR * NUM_CHANNELS + 0] = (unsigned char) r;
			output[buf][j / SCALE_FACTOR * NUM_CHANNELS + 1] = (unsigned char) g;
			output[buf][j / SCALE_FACTOR * NUM_CHANNELS + 2] = (unsigned char) b;
		}

		/* put the scaled lines in the preview result image */
		mfc_put(output[buf], addr2, img->width / SCALE_FACTOR * NUM_CHANNELS, MY_TAG+buf, 0, 0);
    /* update result pointer */
		addr2 += img->width * NUM_CHANNELS;

    /* swap buffers */
		next_buf += buf;
		buf = next_buf - buf;
		next_buf -= buf;
	}

	/* wait for last input buf */
	mfc_write_tag_mask(1 << (MY_TAG+buf));
	mfc_read_tag_status_all();

  /* average the last received lines into 1 */
	buf_trick = (buf<<2) + buf;
	for (j = 0; j < img->width * NUM_CHANNELS / 16; j++) {
		v[4+buf_trick][j] = spu_avg(spu_avg(v[0+buf_trick][j], v[1+buf_trick][j]), 
				spu_avg(v[2+buf_trick][j], v[3+buf_trick][j]));
	}

	for (j=0; j < img->width; j+=SCALE_FACTOR){
		r = g = b = 0;
		for (k = j; k < j + SCALE_FACTOR; k++) {
			r += temp[buf][k * NUM_CHANNELS + 0];
			g += temp[buf][k * NUM_CHANNELS + 1];
			b += temp[buf][k * NUM_CHANNELS + 2];
		}
		r /= SCALE_FACTOR;
		b /= SCALE_FACTOR;
		g /= SCALE_FACTOR;

		output[buf][j / SCALE_FACTOR * NUM_CHANNELS + 0] = (unsigned char) r;
		output[buf][j / SCALE_FACTOR * NUM_CHANNELS + 1] = (unsigned char) g;
		output[buf][j / SCALE_FACTOR * NUM_CHANNELS + 2] = (unsigned char) b;
	}

	/* place the last line in the preview image */
	mfc_put(output[buf], addr2, img->width / SCALE_FACTOR * NUM_CHANNELS, MY_TAG+buf, 0, 0);

	/* wait for both buffers */
	mfc_write_tag_mask((1<<(MY_TAG+buf))|(1<<(MY_TAG+next_buf)));
	mfc_read_tag_status_all();

	free_align(temp[0]);
	free_align(input[0]);
	free_align(output[0]);
	free_align(temp[1]);
	free_align(input[1]);
	free_align(output[1]);
}

/* double buffer technique using dmalists, and doubled buffers size */
void process_image_dmalist(struct image* img){
	unsigned char *input[2], *output[2], *temp[2];
  /* buf, next_buf are double buffer indices */
	unsigned int addr1, addr2, i, j, k, l, r, g, b, buf=0, next_buf=1;
	vuc *v[2][32];
	vuc *v_temp[2][8];
	int block_nr = img->block_nr;

	/* output line result size */
	unsigned int qsz = img->width / SCALE_FACTOR * NUM_CHANNELS;
	/* input line size */
	unsigned int sz = img->width * NUM_CHANNELS;

	/* building dma lists */

	/* configuring DMA list elements sizes 16KB maximum dma transfer size
   * the total size for a dma_list transfer is equivalent to 32 full lines for one image
   * the output buffer therefore has 8 "short" lines
   */
	list0[0].size.all32 = 16384;list1[0].size.all32 = 16384;
	list0[1].size.all32 = 16384;list1[1].size.all32 = 16384;
	list0[2].size.all32 = 16384;list1[2].size.all32 = 16384;
	list0[3].size.all32 = 12288;list1[3].size.all32 = 12288;

	/* output DMA list element sizes */
	for(i=0;i<8;i++) {
		out_list0[i].size.all32 = qsz;
		out_list1[i].size.all32 = qsz;
	}

	/* DMA list alias pointers (double buffer mechanics)
   * pointers to lists are the arguments to spu_mfcdma32
   */
	p_list[0] = &list0[0];
	p_list[1] = &list1[0];
	p_out_list[0] = &out_list0[0];
	p_out_list[1] = &out_list1[0];

	/* allocating input, output, temp buffers */
	input[0] = malloc_align(8 * SCALE_FACTOR * sz, 4);
	input[1] = malloc_align(8 * SCALE_FACTOR * sz, 4);
	output[0] = malloc_align(8 * qsz, 4);
	output[1] = malloc_align(8 * qsz, 4);
	/* scaled rows temporary buffer */
	temp[0] = malloc_align(8 * sz, 4);
	temp[1] = malloc_align(8 * sz, 4);

	/* vector alias pointers to each input row */
	for(i=0;i<32;i++) {
		v[0][i] = (vector unsigned char *) &input[0][i * sz];
		v[1][i] = (vector unsigned char *) &input[1][i * sz];
	}
  /* vector alias pointers to each compacted temp buffer */
	for(i=0;i<8;i++) {
		v_temp[0][i] = (vector unsigned char *) &temp[0][i * sz];
		v_temp[1][i] = (vector unsigned char *) &temp[1][i * sz];
	}

	/* pointer in the main storage where to place the results */
	addr2 = (unsigned int)img->dst;
	/* start line in the preview image in main storage for current spu block */
	addr2 += (block_nr / NUM_IMAGES_WIDTH) * sz * img->height / NUM_IMAGES_HEIGHT; 
	/* start column in the preview image in main storage for current spu block */
	addr2 += (block_nr % NUM_IMAGES_WIDTH) * sz / NUM_IMAGES_WIDTH;

	/* pointer in the main storage where the to_scaled image is placed */
	addr1 = ((unsigned int)img->src);

	/* request buff's first 4 lines */
	list = p_list[buf];
	list[0].ea_low = addr1;
	list[1].ea_low = addr1 + 16384;
	list[2].ea_low = addr1 + 32768;
	list[3].ea_low = addr1 + 49152;
	spu_mfcdma32((volatile void*)input[buf]/* input buffer in local storage*/, 
      (unsigned int)list /* transfer list pointer */, 
			4*sizeof(struct dma_list_elem), MY_TAG+buf, MFC_GETL_CMD);
  /* update pointer for input to_scaled image */
	addr1 += 8 * SCALE_FACTOR * sz;

	for (i=0; i<(img->height / (8 * SCALE_FACTOR))-1; i++) {
    /* request next buff's 4 lines */
		list = p_list[next_buf];
		list[0].ea_low = addr1;
		list[1].ea_low = addr1 + 16384;
		list[2].ea_low = addr1 + 32768;
		list[3].ea_low = addr1 + 49152;
		spu_mfcdma32((volatile void*)input[next_buf]/* input next_buf in local storage*/, 
      (unsigned int)list/* transfer list pointer */, 
			4*sizeof(struct dma_list_elem), MY_TAG+next_buf, MFC_GETL_CMD);
    /* update pointer for input to_scaled image */
		addr1 += 8 * SCALE_FACTOR * sz;

		/* wait for buf input */
		mfc_write_tag_mask(1 << (MY_TAG+buf));
		mfc_read_tag_status_all();

    /* average 32 lines to 8 */
		for (j = 0; j < img->width * NUM_CHANNELS / 16; j++) {
			v_temp[buf][0][j] = spu_avg(spu_avg(v[buf][0][j], v[buf][1][j]), 
					spu_avg(v[buf][2][j], v[buf][3][j]));
			v_temp[buf][1][j] = spu_avg(spu_avg(v[buf][4][j], v[buf][5][j]), 
					spu_avg(v[buf][6][j], v[buf][7][j]));
			v_temp[buf][2][j] = spu_avg(spu_avg(v[buf][8][j], v[buf][9][j]), 
					spu_avg(v[buf][10][j], v[buf][11][j]));
			v_temp[buf][3][j] = spu_avg(spu_avg(v[buf][12][j], v[buf][13][j]), 
					spu_avg(v[buf][14][j], v[buf][15][j]));
			v_temp[buf][4][j] = spu_avg(spu_avg(v[buf][16][j], v[buf][17][j]), 
					spu_avg(v[buf][18][j], v[buf][19][j]));
			v_temp[buf][5][j] = spu_avg(spu_avg(v[buf][20][j], v[buf][21][j]), 
					spu_avg(v[buf][22][j], v[buf][23][j]));
			v_temp[buf][6][j] = spu_avg(spu_avg(v[buf][24][j], v[buf][25][j]), 
					spu_avg(v[buf][26][j], v[buf][27][j]));
			v_temp[buf][7][j] = spu_avg(spu_avg(v[buf][28][j], v[buf][29][j]), 
					spu_avg(v[buf][30][j], v[buf][31][j]));
		}

		/* scale column wise, each scaled row */
		for (l=0; l < 8; l++) {
			for (j=0; j < img->width; j+=SCALE_FACTOR){
				r = g = b = 0;
				for (k = j; k < j + SCALE_FACTOR; k++) {
					r += temp[buf][(l * sz) + (k * NUM_CHANNELS) + 0];
					g += temp[buf][(l * sz) + (k * NUM_CHANNELS) + 1];
					b += temp[buf][(l * sz) + (k * NUM_CHANNELS) + 2];
				}
				r /= SCALE_FACTOR;
				b /= SCALE_FACTOR;
				g /= SCALE_FACTOR;

				output[buf][(l * qsz) + (j / SCALE_FACTOR * NUM_CHANNELS) + 0] = (unsigned char) r;
				output[buf][(l * qsz) + (j / SCALE_FACTOR * NUM_CHANNELS) + 1] = (unsigned char) g;
				output[buf][(l * qsz) + (j / SCALE_FACTOR * NUM_CHANNELS) + 2] = (unsigned char) b;
			}
		}

		/*put the scaled lines back */
		out_list = p_out_list[buf];
		/* build output DMA list */
		for(l=0;l<8;l++) {
			out_list[l].size.all32 = qsz;
			out_list[l].ea_low = addr2;
			addr2 += sz;
		}
		spu_mfcdma32((volatile void*)output[buf], (unsigned int)out_list, 
				8*sizeof(struct dma_list_elem), MY_TAG+buf, MFC_PUTL_CMD);

		next_buf += buf;
		buf = next_buf - buf;
		next_buf -= buf;
	}

	/* wait for buf input */
	mfc_write_tag_mask(1 << (MY_TAG+buf));
	mfc_read_tag_status_all();

	/* compute the scaled line (for buf)  - each v_temp is one scaled line*/
	for (j = 0; j < img->width * NUM_CHANNELS / 16; j++) {
			v_temp[buf][0][j] = spu_avg(spu_avg(v[buf][0][j], v[buf][1][j]), 
					spu_avg(v[buf][2][j], v[buf][3][j]));
			v_temp[buf][1][j] = spu_avg(spu_avg(v[buf][4][j], v[buf][5][j]), 
					spu_avg(v[buf][6][j], v[buf][7][j]));
			v_temp[buf][2][j] = spu_avg(spu_avg(v[buf][8][j], v[buf][9][j]), 
					spu_avg(v[buf][10][j], v[buf][11][j]));
			v_temp[buf][3][j] = spu_avg(spu_avg(v[buf][12][j], v[buf][13][j]), 
					spu_avg(v[buf][14][j], v[buf][15][j]));
			v_temp[buf][4][j] = spu_avg(spu_avg(v[buf][16][j], v[buf][17][j]), 
					spu_avg(v[buf][18][j], v[buf][19][j]));
			v_temp[buf][5][j] = spu_avg(spu_avg(v[buf][20][j], v[buf][21][j]), 
					spu_avg(v[buf][22][j], v[buf][23][j]));
			v_temp[buf][6][j] = spu_avg(spu_avg(v[buf][24][j], v[buf][25][j]), 
					spu_avg(v[buf][26][j], v[buf][27][j]));
			v_temp[buf][7][j] = spu_avg(spu_avg(v[buf][28][j], v[buf][29][j]), 
					spu_avg(v[buf][30][j], v[buf][31][j]));
	}

	/* scale column wise each scaled row */
	for (l=0; l < 8; l++) {
		for (j=0; j < img->width; j+=SCALE_FACTOR){
			r = g = b = 0;
			for (k = j; k < j + SCALE_FACTOR; k++) {
				r += temp[buf][(l * sz) + (k * NUM_CHANNELS) + 0];
				g += temp[buf][(l * sz) + (k * NUM_CHANNELS) + 1];
				b += temp[buf][(l * sz) + (k * NUM_CHANNELS) + 2];
			}
			r /= SCALE_FACTOR;
			b /= SCALE_FACTOR;
			g /= SCALE_FACTOR;

			output[buf][(l * qsz) + (j / SCALE_FACTOR * NUM_CHANNELS) + 0] = (unsigned char) r;
			output[buf][(l * qsz) + (j / SCALE_FACTOR * NUM_CHANNELS) + 1] = (unsigned char) g;
			output[buf][(l * qsz) + (j / SCALE_FACTOR * NUM_CHANNELS) + 2] = (unsigned char) b;
		}
	}

	/* put the scaled lines back */
	out_list = p_out_list[buf];
	for(l=0;l<8;l++) {
		out_list[l].ea_low = addr2;
		addr2 += sz;
	}

	spu_mfcdma32((volatile void*)output[buf], (unsigned int)out_list, 
			8*sizeof(struct dma_list_elem), MY_TAG+buf, MFC_PUTL_CMD);

	/* wait for all results to transfer */
	mfc_write_tag_mask((1<<(MY_TAG+buf))|(1<<(MY_TAG+next_buf)));
	mfc_read_tag_status_all();

	free_align(temp[0]);
	free_align(input[0]);
	free_align(output[0]);
	free_align(temp[1]);
	free_align(input[1]);
	free_align(output[1]);
}

int main(uint64_t speid, uint64_t argp, uint64_t envp){
	unsigned int data[NUM_STREAMS];
	unsigned int num_spus = (unsigned int)argp, i, num_images;
  /* image header in local storage */
	struct image my_image __attribute__ ((aligned(16)));
  /* scale algo switch */
	int mode = (int)envp;

	speid = speid; //get rid of warning

	while(1){
		num_images = 0;
		for (i = 0; i < NUM_STREAMS / num_spus; i++){
			/* assume NUM_STREAMS is a multiple of num_spus */
			while(spu_stat_in_mbox() == 0);
      /* get addres of image header in main storage */
			data[i] = spu_read_in_mbox();
      /* no more images to process */
			if (!data[i])
				return 0;
			num_images++;
		}

		for (i = 0; i < num_images; i++){
      /* get image header in local storage */
			mfc_get(&my_image, data[i], sizeof(struct image), MY_TAG, 0, 0);
      /* wait for transfer */
			mfc_write_tag_mask(1 << MY_TAG);
			mfc_read_tag_status_all();
			switch(mode){
				default:
				case MODE_SIMPLE:
					process_image_simple(&my_image);
					break;
				case MODE_2LINES:
					process_image_2lines(&my_image);
					break;
				case MODE_DOUBLE:
					process_image_double(&my_image);
					break;
				case MODE_DMALIST:
					process_image_dmalist(&my_image);
					break;
			}
		}	
		data[0] = DONE;
    /* signal image completion */
		spu_write_out_intr_mbox(data[0]);	
	}

	return 0;
}
