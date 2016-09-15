#include <visual_odometry.hpp>

typedef char binary_descriptor[DESCRIPTOR_LENGTH / 8];

typedef struct fxy {
	float x;
	float y;
} fxy;
typedef unsigned char comp_vect[4];

typedef struct feature {
	xy pos;
	binary_descriptor * desc;
} feature;

typedef struct descriptor_stack {
	unsigned int nb;
	unsigned int stack_size;
	feature ** stack;
} descriptor_stack;

descriptor_stack * current_stack = NULL;
descriptor_stack * last_stack = NULL;

unsigned int nb_descriptors_in_stack = 0;
comp_vect * briefPattern;

unsigned int first_line_to_sample;

void init_stack(descriptor_stack * stack, unsigned int stack_size) {
	stack->stack = (feature **) malloc(stack_size * sizeof(feature*));
	stack->nb = 0;
	stack->stack_size = stack_size;
}

int push_stack(descriptor_stack * stack, feature * desc) {
	if (stack->nb >= stack->stack_size)
		return 0;
	stack->stack[stack->nb] = desc;
	stack->nb++;
	return 1;
}

int pop_stack(descriptor_stack * stack, feature ** desc) {
	if (stack->nb == 0)
		return 0;
	stack->nb--;
	(*desc) = stack->stack[stack->nb];
	return 1;
}

int get_stack_at(descriptor_stack * stack, unsigned int index,
		feature ** desc) {
	if (index >= stack->nb)
		return 0;
	(*desc) = stack->stack[index];
	return 1;
}

int set_stack_at(descriptor_stack * stack, unsigned int index, feature * desc) {
	if (index >= stack->nb)
		return 0;
	stack->stack[index] = desc;
	return 1;
}

int rand_a_b_brief(int a, int b) {
	//return ((rand() % (b - a) + a;
	float rand_0_1 = (((float) rand()) / ((float) RAND_MAX));
	return (rand_0_1 * (b - a)) + a;
}

comp_vect * initBriefPattern(comp_vect * pattern, int size) {
	int i;
	if (!pattern) {
		pattern = (comp_vect *) malloc(size * sizeof(comp_vect));
	}
	srand(time(NULL));
	int dist_max = sqrt(pow(DESCRIPTOR_WINDOW, 2) + pow(DESCRIPTOR_WINDOW, 2));
	int dist_min = dist_max / 2; // could be altered to have increasing resolution in the descriptor
	for (i = 0; i < size; i++) {
		pattern[i][0] = rand_a_b_brief(0, DESCRIPTOR_WINDOW);
		pattern[i][1] = rand_a_b_brief(0, DESCRIPTOR_WINDOW);
		if (i % 32 == 0 && dist_min > 4) {
			dist_min = dist_min / 2;
			dist_max -= dist_min;
		}
		float dec_c, dec_l;
		int pos_l, pos_c;
		do {
			double dist = (((double) rand()) / ((double) RAND_MAX))
					* (dist_max - dist_min) + dist_min;
			double angle = (((double) rand()) / ((double) RAND_MAX)) * 2.0
					* M_PI;

			dec_c = (int) (cos(angle) * (double) dist);
			dec_l = (int) (sin(angle) * (double) dist);

			pos_l = ((int) pattern[i][0]) + dec_l;
			pos_c = ((int) pattern[i][1]) + dec_c;

		} while (pos_l < 0 || pos_c < 0 || pos_l >= DESCRIPTOR_WINDOW
				|| pos_c >= DESCRIPTOR_WINDOW);

		pattern[i][2] = pos_l;
		pattern[i][3] = pos_c;
	}
	return pattern;
}

binary_descriptor * compute_descriptor(Mat &img, xy pos) {
	unsigned int i, byte_index = 0, bit_count = 0;
	binary_descriptor * desc;
	desc = (binary_descriptor *) malloc(sizeof(binary_descriptor));
	for (i = 0; i < DESCRIPTOR_LENGTH; i++) {
		int lx = briefPattern[i][0];
		int px = briefPattern[i][1];
		int ly = briefPattern[i][2];
		int py = briefPattern[i][3];
		(*desc)[byte_index] = (*desc)[byte_index] << 1;
		unsigned int gauss_value_p = img.at<unsigned char>(lx + pos.y,
				px + pos.x);
		unsigned int gauss_value_n = img.at<unsigned char>(ly + pos.y,
				py + pos.x);
		if (gauss_value_n > gauss_value_p) {
			(*desc)[byte_index] |= 0x1;
		} else {
			(*desc)[byte_index] &= ~0x01;
		}
		bit_count++;
		if (bit_count >= 8) {
			bit_count = 0;
			byte_index++;
		}
	}
	return desc;
}

unsigned int get_match_score(binary_descriptor * d0, binary_descriptor * d1) {
	unsigned long int i, dist = 0; // j,
	unsigned long int * bits1_32 = (unsigned long int *) (*d0);
	unsigned long int * bits2_32 = (unsigned long int *) (*d1);

	for (i = 0; i < (DESCRIPTOR_LENGTH / 32); i++) {
		unsigned long int xored = bits1_32[i] ^ bits2_32[i];
		dist += __builtin_popcount(xored);
		if (dist >= DESCRIPTOR_MATCH_THRESHOLD)
			break; //don't try harder if this not a match
	}
	return dist;
}

int estimate_ground_speeds(Mat & img, unsigned int start_line, double * Ct,
		float * speeds) {
	unsigned int i, j;
	int nb_corners;
	xy* corners;
	fxy * flow_vectors = (fxy*) malloc(STACK_SIZE * sizeof(fxy));
	int flow_vector_size = 0;

	current_stack = (descriptor_stack *) malloc(sizeof(descriptor_stack));
	corners = fast9_detect_nonmax((img.data + (start_line * img.step)),
			img.cols, (img.rows - start_line), img.step, 60, &nb_corners);
	init_stack(current_stack, STACK_SIZE);
	for (i = 0; i < nb_corners; i++) {
		corners[i].y += start_line;
		feature * current = (feature *) malloc(sizeof(feature));
		current->pos.x = corners[i].x;
		current->pos.y = corners[i].y;
		current->desc = compute_descriptor(img, corners[i]);
		circle(img, Point(corners[i].x, corners[i].y), 2, Scalar(0, 0, 0, 0), 2,
				8, 0);
		if (push_stack(current_stack, current) == 0)
			break;
	}
	free(corners); //corners where copied in feature, it can be freed
	if (last_stack != NULL) {
		for (i = 0; i < current_stack->nb; i++) {
			feature * f0;
			get_stack_at(current_stack, i, &f0);
			for (j = 0; j < last_stack->nb; j++) {
				feature * f1;
				get_stack_at(last_stack, j, &f1);
				if (f1 == NULL)
					continue;
				unsigned int score = get_match_score(f1->desc, f0->desc);
				if (score < DESCRIPTOR_MATCH_THRESHOLD) {
					//project in robot frame
					float gp0x, gp0y, gp1x, gp1y;
					pixel_to_ground_plane(Ct, f0->pos.x, f0->pos.y, &gp0x,
							&gp0y);
					pixel_to_ground_plane(Ct, f1->pos.x, f1->pos.y, &gp1x,
							&gp1y);
					flow_vectors[flow_vector_size].x = gp0x - gp1x;
					flow_vectors[flow_vector_size].y = gp0y - gp1y;
					flow_vector_size++;
					free(f1->desc);
					free(f1);
					set_stack_at(last_stack, j, NULL);
					break;
				}
			}
		}
		while (last_stack->nb > 0) {
			feature * f1;
			pop_stack(last_stack, &f1);
			if (f1 != NULL) {
				free(f1->desc);
				free(f1);
			}
		}
		free(last_stack);
		last_stack = current_stack;

		for (i = 0; i < flow_vector_size; i++) {
			cout << flow_vectors[i].x << ", " << flow_vectors[i].y << endl;
		}
		return 1;
	} else {
		last_stack = current_stack;
		return 0; //exchange for the next frame
	}

	//Can now process the vectors in ground plane
	//Find a set of point that matches a movement model
	//Update movement model
}

void init_visual_odometry() {
	float u, v;
	briefPattern = initBriefPattern(briefPattern, DESCRIPTOR_LENGTH);
	calc_ct(camera_pose, K, cam_to_bot_in_world, cam_ct); //compute projection matrix from camera coordinates to world coordinates
	ground_plane_to_pixel(cam_ct, 400., 0., &u, &v);
	first_line_to_sample = (unsigned int) v;
}

int test_estimate_ground_speeds(int argc, char ** argv) {
	if (argc < 3) {
		printf("Requires image path \n");
		exit(-1);
	}
	init_visual_odometry();
	Mat first_image, second_image;
	first_image = imread(argv[1], IMREAD_GRAYSCALE);
	second_image = imread(argv[2], IMREAD_GRAYSCALE);
	estimate_ground_speeds(first_image, first_line_to_sample, cam_ct,
	NULL);
	estimate_ground_speeds(second_image, first_line_to_sample, cam_ct,
	NULL);

	imshow("first", first_image);
	imshow("second", second_image);
	waitKey(0);
	return 0;
}