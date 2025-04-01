/*
This program converts P6 PPM file to P5 PGM file. Works for 255 max values.
Can be compiled normally with GCC without any flags or with makefile provided.
Used from cmd - first arg is source file name (opens as rb), second arg is target file name (opens as wb).
By Jakub Grabowski
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define BUFSIZE 256
#define MAXGRAY 255
#define MAXSIZE MAXGRAY+1
#define KSIZE 3

typedef struct {
    unsigned char r, g, b;
} Pixel;

void error_handler(FILE* src, FILE* tgt, char* msg) {
    printf("%s", msg);
    if (src) fclose(src);
    if (tgt) fclose(tgt);
    exit(EXIT_FAILURE);
}

unsigned char round_clamp(double x) {
    double xm = round(x);
    if (x > 255) return 255;
    if (x < 0) return 0;
    return (unsigned char)x;
}

unsigned char ppm_to_pgm_avg(Pixel* pixel) {
    return (pixel->r + pixel->g + pixel->b) / 3;
}

unsigned char ppm_to_pgm_weighted(Pixel* pixel) {
    // magic numbers
    double wr = 0.299, wg = 0.587, wb = 0.114; // weights sum up to 1, no division necessary 
    double wsum = wr * pixel->r + wg * pixel->g + wb * pixel->b;
    return round_clamp(wsum);
}

void histogram_transform(int size, unsigned char* grayscale) {
    // create a histogram
    // MAXGRAY+1 because there are bytes of MAXGRAY value (e.g. 256 values)
    int hist[MAXSIZE] = {0};
    for (int i = 0; i < size; i++) {
        unsigned char val = grayscale[i];
        hist[val]++;
    }
    
    // compute gmin
    int gmin = 0;
    for (int i = 0; i < MAXSIZE; i++) {
        if (hist[i] > 0) {
            gmin = i;
            break;
        }
    }

    // compute c.img histogram
    int histc[MAXSIZE] = {0};
    histc[0] = 0;
    for (int i = 1; i < MAXSIZE; i++) {
        histc[i] = histc[i-1] + hist[i];
    }
    int hmin = histc[gmin];

    // compute T values
    unsigned char tvals[MAXSIZE] = {0};
    for (int i = 1; i < MAXSIZE; i++) {
        // happy casting
        double val = MAXGRAY * ((double)(histc[i] - hmin) / (size - hmin));
        tvals[i] = round_clamp(val);
    }

    // rewrite grayscale
    for (int i = 0; i < size; i++) {
        unsigned char val = grayscale[i];
        grayscale[i] = tvals[val];
    }
}

void gamma_transform(int size, unsigned char* grayscale, double gamma) {
    // precompute gamma values
    unsigned char lookup[MAXSIZE] = {0};
    for (int i = 0; i < MAXSIZE; i++) {
        double val = (double) i / MAXGRAY;
        lookup[i] = round_clamp(MAXGRAY * pow(val, gamma));
    }

    for (int i = 0; i < size; i++) {
        grayscale[i] = lookup[grayscale[i]];
    }
}

unsigned char get_safe_gval(int width, int height, int i, int j, unsigned char* grayscale) {
    // to avoid darkening on the edges, return nearest actual pixel from the img
    int ii = i, jj = j;
    if (i < 0) ii = 0;
    if (j < 0) jj = 0;
    if (i >= width) ii = width - 1;
    if (j >= height) jj = height - 1;
    return grayscale[jj * width + ii];
}

unsigned char* convolve_3x3(
    int width, int height, unsigned char* grayscale, unsigned char* new_grayscale, double* kernel) {
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            double acc = 0;
            for (int jj = 0; jj < KSIZE; jj++) {
                for (int ii = 0; ii < KSIZE; ii++) {
                    int ni = i + ii - 1;  // offset by kernel center
                    int nj = j + jj - 1;

                    double kval = kernel[jj * KSIZE + ii];
                    unsigned char gval = get_safe_gval(width, height, ni, nj, grayscale);

                    acc += kval * gval;
                }
            }
            new_grayscale[j * width + i] = round_clamp(acc);
        }
    }
}

double arr_sum(int size, double* array) {
    double S = 0;
    for (int i = 0; i < size; i++) {
        S += array[i];
    }
    return S;
}

double arr_mean(int size, double* array) {
    return arr_sum(size, array) / size;
}

double arr_var(int size, double* probs, double* vals) {
    double var = 0;
    double mean = arr_mean(size, vals);
    for (int i = 0; i < size; i++) {
        var += probs[i] * pow(vals[i] - mean, 2);
    }
    return var;
}

void otsu_treshold(int size, unsigned char* grayscale) {
    // create a histogram
    // MAXGRAY+1 because there are bytes of MAXGRAY value (e.g. 256 values)
    double histv[MAXSIZE] = {0};
    double histp[MAXSIZE] = {0};
    for (int i = 0; i < size; i++) {
        unsigned char val = grayscale[i];
        histv[val] += 1;
    }
    // normalize to calculate prob.
    for (int i = 0; i < MAXSIZE; i++) {
        histp[i] = histv[i] / size;
    }

    // calculate all possible tresholds
    double vars[MAXSIZE-2] = {0};
    for (int i = 0; i < MAXSIZE-2; i++) {
        int size_b = i+1;
        int size_f = MAXSIZE-i-1;
        double* vslice_b = histv;
        double* vslice_f = histv + size_b;
        double* pslice_b = histp;
        double* pslice_f = histp + size_b;

        double var_b = arr_var(size_b, pslice_b, vslice_b);
        double var_f = arr_var(size_f, pslice_f, vslice_f);
        double om_b = arr_sum(size_b, pslice_b);
        double om_f = arr_sum(size_f, pslice_f);

        vars[i] = om_b * var_b + om_f * var_f;
    }

    // find min. var. threshold
    double tvar = vars[0];
    int th = 0;
    for (int i = 1; i < MAXSIZE-2; i++) {
        if (tvar > vars[i]) {
            tvar = vars[i];
            th = i;
        }
    }

    // transform to black and white
    for (int i = 0; i < size; i++) {
        if (grayscale[i] > th) {
            grayscale[i] = 255;
        } else {
            grayscale[i] = 0;
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
        printf("Bytes read %ld. Supposed to be %d.", bytes_read, size);
        error_handler(src, tgt, "Unexpected end of file (4).");
    }
    fclose(src);

    // write header to target file
    fprintf(tgt, "P5\n%d %d\n255\n", width, height);

    unsigned char* grayscale = (unsigned char*)malloc(size);
    if (!grayscale) {
        free(pixels);
        error_handler(src, tgt, "Memory allocation failed for grayscale data.");
    }
    
    // write to grayscale
    for (int i = 0; i < size; i++) {
            // grayscale[i] = ppm_to_pgm_avg(&pixels[i]); // avg
            grayscale[i] = ppm_to_pgm_weighted(&pixels[i]); // weighted avg
    }
    free(pixels);
    
    // transform grayscale with histogram
    histogram_transform(size, grayscale);
    // transform grayscale with gamma correction
    gamma_transform(size, grayscale, 2.0);

    // example approx. gaussian filter - can be changed to be any other 3x3 kernel
    double kernel[KSIZE * KSIZE] = {
        1.0 / 16, 2.0 / 16, 1.0 / 16,
        2.0 / 16, 4.0 / 16, 2.0 / 16,
        1.0 / 16, 2.0 / 16, 1.0 / 16
    };

    unsigned char* new_grayscale = (unsigned char*)malloc(size);
    if (!new_grayscale) {
        free(pixels);
        free(new_grayscale);
        error_handler(src, tgt, "Memory allocation failed for grayscale data manipulation.");
    }
    
    convolve_3x3(width, height, grayscale, new_grayscale, kernel);

    otsu_treshold(size, new_grayscale);

    free(grayscale);
    fwrite(new_grayscale, sizeof(unsigned char), size, tgt);
    free(new_grayscale);

    fclose(tgt);
    printf("File converted successfully.\n");
    return 0;
}
