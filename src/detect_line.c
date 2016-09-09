#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "interpolate.h"
#include "profile.h"
#include "resampling.h"
#include "detect_line.h"

#include "camera_parameters.h"
//Needds to be computed from camera calibration results using resampling.c

double cam_ct[12] ;

char line_detection_kernel[9] = { -1, 0, 1, -1, 0, 1, -1, 0, 1 };

#define IMG_HEIGHT 480
#define SAMPLES_FIRST_LINE 8
#define SAMPLES_LAST_LINE IMG_HEIGHT
#define NB_LINES_SAMPLED 16
#define SAMPLES_SPACE ((SAMPLES_LAST_LINE - SAMPLES_FIRST_LINE)/NB_LINES_SAMPLED)

float rand_a_b(int a, int b) {
	//return ((rand() % (b - a) + a;
	float rand_0_1 = (((float) rand()) / ((float) RAND_MAX));
	return (rand_0_1 * (b - a)) + a;
}

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

void kernel_horiz(IplImage * img, int * kernel_response, int v) {
	unsigned int i;
	kernel_response[0] = 0;
	unsigned char * upixel_ptr = (unsigned char *) img->imageData;
	for (i = 1; i < (img->width - 1); i++) {
		int response = 0;
		response -= upixel_ptr[(v - 1) * img->widthStep + (i - 1)];
		response -= upixel_ptr[(v) * img->widthStep + (i - 1)];
		response -= upixel_ptr[(v + 1) * img->widthStep + (i - 1)];

		response += upixel_ptr[(v - 1) * img->widthStep + (i + 1)];
		response += upixel_ptr[(v) * img->widthStep + (i + 1)];
		response += upixel_ptr[(v + 1) * img->widthStep + (i + 1)];

		kernel_response[i] = response;

	}
	kernel_response[i] = 0;
}

float distance_to_curve(curve * l, float x, float y) {
	//need to find a fast way to implement ...
	return 0.;
}

#define RANSAC_LIST (POLY_LENGTH)
#define RANSAC_NB_LOOPS 10
#define RANSAC_INLIER_LIMIT 8.0
float fit_line(point * pts, unsigned int nb_pts, curve * l) {
	int i, j;
	float * x = malloc(nb_pts * sizeof(float));
	float * y = malloc(nb_pts * sizeof(float));
	char * used = malloc(nb_pts * sizeof(char));
	unsigned char * inliers = malloc(nb_pts * sizeof(char));
	unsigned char * max_inliers = malloc(nb_pts * sizeof(char));
	float fct_params[POLY_LENGTH];
	int max_consensus = 0;
	for (i = 0; i < RANSAC_NB_LOOPS; i++) {
		int pt_index = 0;
		int nb_consensus = 0;
		int idx = 0;
		float max_x_temp = 0;
		float min_x_temp = 3000.; //arbitrary ...
		memset(used, 0, NB_LINES_SAMPLED * sizeof(char)); //zero used index
		while (pt_index < RANSAC_LIST) {
			//Select set of samples, with distance constraint
			idx = rand_a_b(0, (nb_pts - 1));
			while (used[idx] != 0)
				idx = (idx + 1) % NB_LINES_SAMPLED;
			//continue;
			/*int closest_neighbour = NB_LINES_SAMPLED;
			 for (j = 0; j < NB_LINES_SAMPLED; j++) {
			 int idx_to_test = (idx + j) % NB_LINES_SAMPLED;
			 if (used[idx_to_test] == 1
			 && abs(idx_to_test - idx) < closest_neighbour) {
			 closest_neighbour = abs(idx_to_test - idx);
			 }
			 }*/

			//if (closest_neighbour > 1) {
			y[pt_index] = pts[idx].y;
			x[pt_index] = pts[idx].x;
			if (x[pt_index] > max_x_temp)
				max_x_temp = x[pt_index];
			used[idx] = 1;
			inliers[nb_consensus] = idx;
			nb_consensus++;
			pt_index++;
			//}
		}
		//From initial set, compute polynom
		compute_interpolation(x, y, fct_params, POLY_LENGTH, pt_index);
		max_x_temp = 0;
		idx = 0;
		for (idx = 0; idx < nb_pts; idx++) {
			if (used[idx] == 1) {
				continue;
			}
			used[idx] = 1;

			//Distance should be computed properly
			float resp = 0.;
			for (j = 0; j < POLY_LENGTH; j++) {
				resp += fct_params[j] * pow(pts[idx].x, j);
			}
			float error = abs(resp - pts[idx].y);

			if (error < RANSAC_INLIER_LIMIT) {
				inliers[nb_consensus] = idx;
				nb_consensus++;
				if (pts[idx].x > max_x_temp)
					max_x_temp = pts[idx].x;
				if (pts[idx].x < min_x_temp)
					min_x_temp = pts[idx].x;
			}
			if (nb_consensus > max_consensus) {
				max_consensus = nb_consensus;
				l->max_x = max_x_temp;
				l->min_x = min_x_temp;
			}
		}
		memcpy(max_inliers, inliers, nb_consensus * sizeof(char));
		memcpy(l->p, fct_params, POLY_LENGTH * sizeof(float));

	}

	for (i = 0; i < max_consensus; i++) {
		int idx = max_inliers[i];
		y[i] = pts[idx].y;
		x[i] = pts[idx].x;
	}

	//simple test to evaluate solution that includes all detected points
	/*max_consensus = 0;
	 for (i = 0; i < nb_pts; i++) {
	 if (pts[i] > 0.) {
	 y[max_consensus] = pts[i].y;
	 x[max_consensus] = pts[i].x;
	 max_consensus++;
	 }
	 }*/

	compute_interpolation(x, y, l->p, POLY_LENGTH, max_consensus);
	free(x);
	free(y);
	free(used);
	free(inliers);
	free(max_inliers);
	/*printf("Max consensus %d \n", max_consensus);
	 printf("%f + %f*x + %f*x^2 \n", l->p[0], l->p[1], l->p[2]);
	 */
	float confidence = ((float) (max_consensus + RANSAC_LIST))
			/ ((float) NB_LINES_SAMPLED);
	//printf("Confidence %f \n", confidence);

	return confidence;
}

#define SCORE_THRESHOLD 200
#define WIDTH_THRESHOLD 100
float detect_line(IplImage * img, curve * l, point * pts, int * nb_pts) {
	unsigned int i, j;
	(*nb_pts) = 0;
	int * sampled_lines = malloc(img->width * sizeof(int));
	srand(time(NULL));
	for (i = 0; i < NB_LINES_SAMPLED; i++) {
		kernel_horiz(img, sampled_lines,
				(SAMPLES_FIRST_LINE + i * (SAMPLES_SPACE)));
		int max = 0, min = 0, max_index = 0, min_index = 0, sig = 0;
		int score, width;
		for (j = 1; j < (img->width - 1); j++) {
			//Track is white on black, so we expect maximum gradient then minimum
			if (sampled_lines[j] < min) {
				if (max > 0) { //we have a signature ...
					min = sampled_lines[j];
					min_index = j;
					sig = 1;
				}
			}
			if (sampled_lines[j] > max) {
				if (min > 0) {
					min = 0;
					max = sampled_lines[j];
					max_index = j;
					sig = 0;
				} else {
					max = sampled_lines[j];
					max_index = j;
				}
			}
		}
		score = abs(min) + abs(max);
		width = max_index - min_index;
		if (sig == 1 && score > SCORE_THRESHOLD && width < WIDTH_THRESHOLD) {
			pts[(*nb_pts)].x = ((float) (max_index + min_index)) / 2.;
			pts[(*nb_pts)].y = (SAMPLES_FIRST_LINE + i * (SAMPLES_SPACE));
			(*nb_pts)++;
		}
	}
	free(sampled_lines);
	//project all points in robot frame before fiting
	//
	for (i = 0; i < (*nb_pts); i++) {
		pixel_to_ground_plane(cam_ct, pts[i].x, pts[i].y, &(pts[i].x),
				&(pts[i].y));
	}

	if ((*nb_pts) > (POLY_LENGTH * 2)) {
		return fit_line(pts, (*nb_pts), l);
	} else {
		return 0.;
	}
//return 0.;
//TODO : for each detected point, compute its projection in the world frame instead of plain image coordinates
//

}

float steering_from_curve(curve * c, float x_lookahead) {
	float y_lookahead = 0;
	int i;
	for (i = 0; i < POLY_LENGTH; i++) {
		y_lookahead += c->p[i] * pow(x_lookahead, i);
	}
	float D_square = pow(x_lookahead, 2) + pow(y_lookahead, 2);
	float r = D_square / (2.0 * x_lookahead);
	float curvature = 1.0 / r;
	return curvature;
}

int detect_line_test(int argc, char ** argv) {
	int i, nb_pts;
	curve detected;
	point pts[NB_LINES_SAMPLED];
	if (argc < 2) {
		printf("Requires image path \n");
		exit(-1);
	}
	calc_ct_and_H(camera_pose, K, cam_ct, NULL) ; //compute projection matrix from camera coordinates to world coordinates
	IplImage * line_image = cvLoadImage(argv[1], CV_LOAD_IMAGE_GRAYSCALE);
	init_profile(0);
	start_profile(0);
	detect_line(line_image, &detected, pts, &nb_pts);
	end_profile(0);
	print_profile_time("Took :", 0);
//there is a pi/2 rotation on Z
	for (i = 0; i < nb_pts; i++) {
		cvLine(line_image, cvPoint(0, pts[i].x),
				cvPoint(line_image->width - 1, pts[i].x),
				cvScalar(255, 255, 255, 0), 1, 8, 0);
		cvCircle(line_image, cvPoint((int) (pts[i].y), (int) (pts[i].x)), 4,
				cvScalar(0, 0, 0, 0), 4, 8, 0);
	}

	for (i = 0; i < line_image->height; i += 10) {
		float resp1 = 0., resp2 = 0.;
		int j;
		for (j = 0; j < POLY_LENGTH; j++) {
			resp1 += detected.p[j] * pow(i, j);
			resp2 += detected.p[j] * pow((i + 10), j);
		}
		cvLine(line_image, cvPoint(resp1, i), cvPoint(resp2, i + 10),
				cvScalar(0, 0, 0, 0), 1, 8, 0);
	}

	cvShowImage("orig", line_image);
	cvWaitKey(0);
	return 0;
}
