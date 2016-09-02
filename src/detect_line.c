#include "opencv2/highgui/highgui_c.h"
#include "opencv2/core/core_c.h"
#include "opencv2/core/types_c.h"
#include "opencv2/imgproc/imgproc_c.h"

#include "interpolate.h"

char line_detection_kernel[9] = { -1, 0, 1, -1, 0, 1, -1, 0, 1 };

#define IMG_HEIGHT 240
#define SAMPLES_FIRST_LINE 8
#define SAMPLES_LAST_LINE IMG_HEIGHT
#define NB_LINES_SAMPLED 16
#define SAMPLES_SPACE ((SAMPLES_LAST_LINE - SAMPLES_FIRST_LINE)/NB_LINES_SAMPLED)

float rand_a_b(int a, int b) {
	//return ((rand() % (b - a) + a;
	float rand_0_1 = (((float) rand()) / ((float) RAND_MAX));
	return (rand_0_1 * (b - a)) + a;
}

#define POLY_LENGTH 3
typedef struct curve {
	float p[POLY_LENGTH];
	float max_x;
	float min_x;
} curve;

void kernel_line(IplImage * img, char * kernel, int * kernel_response, int v) {
	unsigned int i;
	kernel_response[0] = 0;
	unsigned char * upixel_ptr = (unsigned char *) img->imageData;
	for (i = 1; i < (img->width - 1); i++) {
		int response = 0;
		response += kernel[0] * upixel_ptr[(v - 1) * img->widthStep + (i - 1)];
		response += kernel[1] * upixel_ptr[(v - 1) * img->widthStep + (i)];
		response += kernel[2] * upixel_ptr[(v - 1) * img->widthStep + (i + 1)];

		response += kernel[3] * upixel_ptr[(v) * img->widthStep + (i - 1)];
		response += kernel[4] * upixel_ptr[(v) * img->widthStep + (i)];
		response += kernel[5] * upixel_ptr[(v) * img->widthStep + (i + 1)];

		response += kernel[6] * upixel_ptr[(v + 1) * img->widthStep + (i - 1)];
		response += kernel[7] * upixel_ptr[(v + 1) * img->widthStep + (i)];
		response += kernel[8] * upixel_ptr[(v + 1) * img->widthStep + (i + 1)];

		kernel_response[i] = response;

	}
	kernel_response[i] = 0;
}

#define RANSAC_LIST POLY_LENGTH
#define RANSAC_NB_TEST 5
#define RANSAC_INLIER_LIMIT 1.5

float detect_line(IplImage * img, curve * l, float * pts) {
	unsigned int i, j;
	int nb_pts = 0;
	int * sampled_lines = malloc(img->width * sizeof(int));
	for (i = 0; i < NB_LINES_SAMPLED; i++) {

		kernel_line(img, line_detection_kernel, sampled_lines,
				(SAMPLES_FIRST_LINE + i * (SAMPLES_SPACE)));
		int max = 0, min = 0, max_index, min_index, sig = 0;

		for (j = 1; j < (img->width - 1); j++) {
			//printf("%d ", sampled_lines[i][j]);
			if (sampled_lines[j] > max) {
				if (min < 0) { //we have a signature ...
					max = sampled_lines[j];
					max_index = j;
					sig = 1;
				}
			}
			if (sampled_lines[j] < min) {
				if (max > 0) {
					max = 0;
					min = sampled_lines[j];
					min_index = j;
					sig = 0;
				} else {
					min = sampled_lines[j];
					min_index = j;
				}
			}
		}
		if (sig == 1) {
			pts[i] = ((float) (max_index + min_index)) / 2.;
			nb_pts++;
		} else {
			pts[i] = -1; //not found ...
		}
		//printf("\n");
	}
	free(sampled_lines);
	//TODO : for each detected point, compute its projection in the world frame

	float * x = malloc(RANSAC_LIST * sizeof(float));
	float * y = malloc(RANSAC_LIST * sizeof(float));
	char * used = malloc(NB_LINES_SAMPLED * sizeof(char));
	float fct_params[3];
	//Need to execute in a RANSAC way
	int max_consensus = 0;
	for (i = 0; i < RANSAC_NB_TEST; i++) {
		int pt_index = 0;
		int nb_consensus = 0;
		int nb_tests = 0;
		int idx;
		float max_x_temp = 0 ;
		float min_x_temp = img->width ;
		memset(used, 0, NB_LINES_SAMPLED * sizeof(char)); //zero used index
		while (pt_index < RANSAC_LIST) {
			idx = rand_a_b(0, NB_LINES_SAMPLED);
			int closest_neighbour = NB_LINES_SAMPLED ;
			for(j = 0  ; j < NB_LINES_SAMPLED ; j ++){
				if(used[j] == 1 && abs(j - idx) < closest_neighbour){
					closest_neighbour = abs(j - idx) ;
				}
			}

			if (used[idx] == 0 && pts[idx] > 0. && closest_neighbour > 3) {
				y[pt_index] = (SAMPLES_FIRST_LINE + idx * (SAMPLES_SPACE));
				x[pt_index] = pts[idx];
				if(x[pt_index] > max_x_temp) max_x_temp = x[pt_index]  ;
				used[idx] = 1;
				pt_index++;
				nb_tests++;
			}
		}
		compute_interpolation(x, y, fct_params, POLY_LENGTH, pt_index);

		while (1) {
			do {
				idx = rand_a_b(0, NB_LINES_SAMPLED);
			} while (used[idx] == 1 || pts[idx] < 0.);
			used[idx] = 1;

			float idx_y = (SAMPLES_FIRST_LINE + idx * (SAMPLES_SPACE));
			float resp = 0.;
			for (j = 0; j < pt_index; j++) {
				resp += fct_params[j] * pow(pts[idx], j);
			}
			float error = abs(resp - idx_y);
			if (error < RANSAC_INLIER_LIMIT){
				nb_consensus++;
				if(pts[idx] > max_x_temp) max_x_temp = pts[idx]  ;
				if(pts[idx] < min_x_temp) min_x_temp = pts[idx]  ;
			}
			if (nb_consensus > max_consensus) {
				max_consensus = nb_consensus;
				memcpy(l->p, fct_params, POLY_LENGTH * sizeof(float));
				l->max_x = max_x_temp ;
				l->min_x = min_x_temp ;
			}
			nb_tests++;
			if (nb_tests >= nb_pts) {
				break;
			}
		}

	}
	printf("Max consensus %d \n", (max_consensus + RANSAC_LIST));
	printf("%f + %f*x + %f*x^2 \n", l->p[0], l->p[1], l->p[2]);
	free(x);
	free(y);
	free(used);
	float confidence = ((float) (max_consensus + RANSAC_LIST))
			/ ((float) NB_LINES_SAMPLED);
	printf("Confidence %f \n", confidence);
	return confidence;
}

//Compute the line equation in the ground plane (need a calibrated camera system ...)
//Compute an interception curve in the ground plane
//Compute steering from interception curve and apply to vehicle (compute speed based on steering to avoid drift)

int main(void) {
	int i;
	curve detected;
	float pts[NB_LINES_SAMPLED];
	IplImage * line_image =
			cvLoadImage(
					"/home/jpiat/development/SOFTWARE/pi_line_follower/img_test/curve_view.png",
					CV_LOAD_IMAGE_GRAYSCALE);
	detect_line(line_image, &detected, pts);
	for (i = 0; i < NB_LINES_SAMPLED; i++) {
		cvLine(line_image,
				cvPoint(0, (SAMPLES_FIRST_LINE + i * (SAMPLES_SPACE))),
				cvPoint(line_image->width - 1,
						(SAMPLES_FIRST_LINE + i * (SAMPLES_SPACE))),
				cvScalar(255, 255, 255, 0), 1, 8, 0);
		if (pts[i] > 0) {
			cvCircle(line_image,
					cvPoint((int) (pts[i]),
							(SAMPLES_FIRST_LINE + i * (SAMPLES_SPACE))), 4,
					cvScalar(0, 0, 0, 0), 4, 8, 0);
		}
	}

	for (i = 0; i <= detected.max_x; i += 10) {
		float resp1 = 0., resp2 = 0.;
		int j;
		for (j = 0; j < POLY_LENGTH; j++) {
			resp1 += detected.p[j] * pow(i, j);
			resp2 += detected.p[j] * pow((i + 10), j);
		}
		cvLine(line_image, cvPoint(i, resp1), cvPoint(i + 10, resp2),
				cvScalar(255, 255, 255, 0), 1, 8, 0);
	}

	cvShowImage("orig", line_image);
	cvWaitKey(0);
	return 0;
}