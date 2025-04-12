/*
This program converts P6 PPM file to P5 PGM file. Works for 255 max values.
Can be compiled normally with GCC without any flags or with makefile provided.
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

uint8_t round_clamp(double x) {
    double xm = round(x);
    if (x > 255) return 255;
    if (x < 0) return 0;
    return (uint8_t)x;
}

uint8_t ppm_to_pgm_weighted(Pixel* pixel) {
    // magic numbers
    double wr = 0.299, wg = 0.587, wb = 0.114; // weights sum up to 1, no division necessary 
    double wsum = wr * pixel->r + wg * pixel->g + wb * pixel->b;
    return round_clamp(wsum);
}

void neon_weighted_grayscale(int size, Pixel* pixels, uint8_t* grayscale) {
    // weights scaled to 8-bit fixed-point (approx.)
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

uint8_t get_safe_gval(int width, int height, int i, int j, uint8_t* grayscale) {
    // to avoid darkening on the edges, return nearest actual pixel from the img
    int ii = i, jj = j;
    if (i < 0) ii = 0;
    if (j < 0) jj = 0;
    if (i >= width) ii = width - 1;
    if (j >= height) jj = height - 1;
    return grayscale[jj * width + ii];
}

uint8_t* convolve_3x3(
    int width, int height, uint8_t* grayscale, uint8_t* new_grayscale, double* kernel) {
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            double acc = 0;
            for (int jj = 0; jj < KSIZE; jj++) {
                for (int ii = 0; ii < KSIZE; ii++) {
                    int ni = i + ii - 1;  // offset by kernel center
                    int nj = j + jj - 1;

                    double kval = kernel[jj * KSIZE + ii];
                    uint8_t gval = get_safe_gval(width, height, ni, nj, grayscale);

                    acc += kval * gval;
                }
            }
            new_grayscale[j * width + i] = round_clamp(acc);
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
        printf("Bytes read %u. Supposed to be %d.", bytes_read, size);
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

    // example approx. gaussian filter - can be changed to be any other 3x3 kernel
    double kernel[KSIZE * KSIZE] = {
        1.0 / 16, 2.0 / 16, 1.0 / 16,
        2.0 / 16, 4.0 / 16, 2.0 / 16,
        1.0 / 16, 2.0 / 16, 1.0 / 16
    };

    uint8_t* new_grayscale = (uint8_t*)malloc(size);
    if (!new_grayscale) {
        free(pixels);
        free(new_grayscale);
        error_handler(src, tgt, "Memory allocation failed for grayscale data manipulation.");
    }
    
    // neon_convolve_3x3(width, height, grayscale, new_grayscale, kernel);

    free(grayscale);
    fwrite(new_grayscale, sizeof(uint8_t), size, tgt);
    free(new_grayscale);

    fclose(tgt);
    printf("File converted successfully.\n");
    return 0;
}
