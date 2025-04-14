/*
This program converts P6 PPM file to P5 PGM file. Works for 255 max values.
Can be compiled normally with GCC for 64-bit ARM processors (tested for RPi3B).
Used from cmd - first arg is source file name (opens as rb), second arg is target file name (opens as wb).
By Jakub Grabowski
*/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>
#include <arm_neon.h>

#define BUFSIZE 256
#define MAXGRAY 255
#define MAXSIZE MAXGRAY+1
#define KSIZE 3
#define VECSIZE 8

typedef struct {
    uint8_t r, g, b;
} Pixel;

void error_handler(FILE* src, FILE* tgt, char* msg) {
    printf("%s", msg);
    if (src) fclose(src);
    if (tgt) fclose(tgt);
    exit(EXIT_FAILURE);
}

void add_two_vectors(uint8_t* v1, uint8_t* v2, uint8_t* res) {
    for (int i = 0; i < VECSIZE; i++) {
        res[i] = v1[i] + v2[i];
    }
}

void mul_two_vectors(uint8_t* v1, uint8_t* v2, uint8_t* res) {
    for (int i = 0; i < VECSIZE; i++) {
        res[i] = v1[i] * v2[i];
    }
}

void neon_add_two_vectors(uint8_t* v1, uint8_t* v2, uint8_t* res) {
    uint8x8_t vec1 = vld1_u8(v1);
    uint8x8_t vec2 = vld1_u8(v2);
    uint8x8_t result = vadd_u8(vec1, vec2);
    vst1_u8(res, result);
}

void neon_mul_two_vectors(uint8_t* v1, uint8_t* v2, uint8_t* res) {
    uint8x8_t vec1 = vld1_u8(v1);
    uint8x8_t vec2 = vld1_u8(v2);
    uint8x8_t result = vmul_u8(vec1, vec2);
    vst1_u8(res, result);
}

// fallback function
unsigned char ppm_to_pgm_weighted(Pixel* pixel) {
    // magic numbers
    double wr = 0.299, wg = 0.587, wb = 0.114; // weights sum up to 1, no division necessary 
    double wsum = wr * pixel->r + wg * pixel->g + wb * pixel->b;
    return wsum;
}

void neon_weighted_grayscale(int size, Pixel* pixels, uint8_t* grayscale) {
    // weights magic numbers scaled to 8-bit fixed-point (approx.)
    const uint8x8_t wr = vdup_n_u8(77);   // 0.299 * 256 c. 77
    const uint8x8_t wg = vdup_n_u8(150);  // 0.587 * 256 c. 150
    const uint8x8_t wb = vdup_n_u8(29);   // 0.114 * 256 c. 29

    int i;
    for (i = 0; i <= size - VECSIZE; i += VECSIZE) {
        uint8x8x3_t rgb = vld3_u8((uint8_t*)&pixels[i]); // load RGB into separate 8-lanes

        uint16x8_t r = vmull_u8(rgb.val[0], wr); // R * 0.299
        uint16x8_t g = vmull_u8(rgb.val[1], wg); // G * 0.587
        uint16x8_t b = vmull_u8(rgb.val[2], wb); // B * 0.114

        uint16x8_t sum = vaddq_u16(vaddq_u16(r, g), b);
        uint8x8_t result = vshrn_n_u16(sum, 8); // div. 256

        vst1_u8(&grayscale[i], result);
    }

    // scalar fallback for remaining pixels
    for (; i < size; i++) {
        grayscale[i] = ppm_to_pgm_weighted(&pixels[i]);
    }
}

void neon_mean_filter(int width, int height, uint8_t* grayscale, uint8_t* new_grayscale) {
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            // load 3×3 neighborhood manually
            uint8_t a = grayscale[(y - 1) * width + (x - 1)];
            uint8_t b = grayscale[(y - 1) * width + x];
            uint8_t c = grayscale[(y - 1) * width + (x + 1)];
            uint8_t d = grayscale[y * width + (x - 1)];
            uint8_t e = grayscale[y * width + x];
            uint8_t f = grayscale[y * width + (x + 1)];
            uint8_t g = grayscale[(y + 1) * width + (x - 1)];
            uint8_t h = grayscale[(y + 1) * width + x];
            uint8_t i = grayscale[(y + 1) * width + (x + 1)];

            // load into NEON register: 8 elements
            uint8x8_t vec = {a, b, c, d, e, f, g, h};

            // pairwise add and widen: gives 4 uint16 values (a+b, c+d, e+f, g+h)
            uint16x4_t pair_sums = vpaddl_u8(vec);

            // sum all parts
            uint16_t total = vget_lane_u16(pair_sums, 0) +
                             vget_lane_u16(pair_sums, 1) +
                             vget_lane_u16(pair_sums, 2) +
                             vget_lane_u16(pair_sums, 3) + i;

            uint8_t result = (uint8_t)(total / 9); // avg
            new_grayscale[y * width + x] = result;
        }
    }
}

// args: $1: file to convert, $2: file to save the results to
int main(int argc, char const *argv[]) {
    if (argc != 3) {
        printf("This program takes exactly 2 arguments.");
        exit(EXIT_FAILURE);
    }
    char const * src_file_name = argv[1];
    char const * res_file_name = argv[2];
    FILE* src = fopen(src_file_name, "rb");
    FILE* tgt = fopen(res_file_name, "wb");
    
    // file error handling
    if (src == NULL || tgt == NULL) {
        error_handler(src, tgt, "Could not open the files.");
    }

    char format[3], buffer[BUFSIZE];
    int width, height, max_val, size;

    // skip comment lines
    do {
        if (fgets(buffer, sizeof(buffer), src) == NULL) {
            error_handler(src, tgt, "Unexpected end of file (1).");
        }
    } while (buffer[0] == '#');

    // read magic (format ID)
    if (sscanf(buffer, "%2s", format) != 1 || format[0] != 'P' || format[1] != '6') {
        error_handler(src, tgt, "Bad file format.");
    }

    // skip comment lines before reading dimensions
    do {
        if (fgets(buffer, sizeof(buffer), src) == NULL) {
            error_handler(src, tgt, "Unexpected end of file (2).");
        }
    } while (buffer[0] == '#');

    if (sscanf(buffer, "%d %d", &width, &height) != 2) {
        error_handler(src, tgt, "Invalid image dimensions.");
    }

    // skip comment lines before reading max value
    do {
        if (fgets(buffer, sizeof(buffer), src) == NULL) {
            error_handler(src, tgt, "Unexpected end of file (3).");
        }
    } while (buffer[0] == '#');

    if (sscanf(buffer, "%d", &max_val) != 1) {
        error_handler(src, tgt, "Invalid max color value.");
    }

    if (max_val > MAXGRAY) {
        error_handler(src, tgt, "Unsupported max value > 255.");
    }

    if (width < 1 || height < 1) {
        error_handler(src, tgt, "Invalid image dimensions.");
    }
    size = width * height;

    Pixel* pixels = (Pixel*)malloc(size * sizeof(Pixel));
    if (pixels == NULL) {
        free(pixels);
        error_handler(src, tgt, "Could not allocate memory for the image.");
    }

    // read binary format
    size_t bytes_read = fread(pixels, sizeof(Pixel), size, src);
    if (bytes_read  != (size_t)(size)) {
        free(pixels);
        printf("Bytes read %d. Supposed to be %d.", (int)bytes_read, size);
        error_handler(src, tgt, "Unexpected end of file (4).");
    }
    fclose(src);

    // write header to target file
    fprintf(tgt, "P5\n%d %d\n255\n", width, height);

    uint8_t* grayscale = (uint8_t*)malloc(size);
    if (!grayscale) {
        free(pixels);
        error_handler(src, tgt, "Memory allocation failed for grayscale data.");
    }
    
    // write to grayscale
    neon_weighted_grayscale(size, pixels, grayscale);
    free(pixels);


    uint8_t* new_grayscale = (uint8_t*)malloc(size);
    if (!new_grayscale) {
        free(pixels);
        free(new_grayscale);
        error_handler(src, tgt, "Memory allocation failed for grayscale data manipulation.");
    }
    
    // use mean filter
    neon_mean_filter(width, height, grayscale, new_grayscale);

    fwrite(new_grayscale, sizeof(uint8_t), size, tgt);

    free(grayscale);
    free(new_grayscale);

    fclose(tgt);
    printf("File converted successfully.\n");
    return 0;
}
