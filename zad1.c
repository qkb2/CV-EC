/*
This program converts P6 PPM file to P5 PGM file. Works for 255 max values.
Can be compiled normally with GCC without any flags or with makefile provided.
Used from cmd - first arg is source file name (opens as rb), second arg is target file name (opens as wb).
By Jakub Grabowski
*/

#include <stdio.h>
#include <stdlib.h>

#define BUFSIZE 256

typedef struct {
    unsigned char r, g, b;
} Pixel;

void error_handler(FILE* src, FILE* tgt, char* msg) {
    printf("%s", msg);
    fclose(src);
    fclose(tgt);
    exit(EXIT_FAILURE);
}

unsigned char ppm_to_pgm_avg(Pixel* pixel) {
    return (pixel->r + pixel->g + pixel->b) / 3;
}

unsigned char ppm_to_pgm_weighted(Pixel* pixel) {
    // magic numbers
    double wr = 0.299, wg = 0.587, wb = 0.114; // weights sum up to 1, no division necessary 
    double wsum = wr * pixel->r + wg * pixel->g + wb * pixel->b;
    return (unsigned char) wsum;
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
    int width, height, max_val;

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

    if (max_val > 255) {
        error_handler(src, tgt, "Unsupported max value > 255.");
    }

    Pixel* pixels = (Pixel*)malloc(width * height * sizeof(Pixel));
    if (pixels == NULL) {
        free(pixels);
        error_handler(src, tgt, "Could not allocate memory for the image.");
    }

    // read binary format
    size_t bytes_read = fread(pixels, sizeof(Pixel), width * height, src);
    if (bytes_read  != (size_t)(width * height)) {
        free(pixels);
        printf("Bytes read %ld. Supposed to be %d.", bytes_read, width * height);
        error_handler(src, tgt, "Unexpected end of file (4).");
    }

    // write header to target file
    fprintf(tgt, "P5\n%d %d\n255\n", width, height);

    unsigned char* grayscale = (unsigned char*)malloc(width * height);
    if (!grayscale) {
        free(pixels);
        error_handler(src, tgt, "Memory allocation failed for grayscale data.");
    }
    
    // write to grayscale
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            // grayscale[j * width + i] = ppm_to_pgm_avg(&pixels[j * width + i]); // avg
            grayscale[j * width + i] = ppm_to_pgm_weighted(&pixels[j * width + i]); // weighted avg
        }
    }
    
    fwrite(grayscale, sizeof(unsigned char), width * height, tgt);

    free(pixels);
    free(grayscale);
    fclose(src);
    fclose(tgt);
    printf("File converted successfully.\n");
    return 0;
}
