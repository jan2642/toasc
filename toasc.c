#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <getopt.h>
#include <stdarg.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

typedef struct font_char_s {
    unsigned char num;
    int inverted;
    int allowed;
    int avg;
    unsigned char data[];
} font_char_t;

typedef struct font_s {
    int nr_chars;
    int width;
    int height;
    int min;
    int max;
    font_char_t *chars[];
} font_t;

//static font_t *font;

static unsigned char *img;
static int verbose_enabled = 0;

static void verbose(char *fmt, ...) {
    char p[256];
    va_list ap;

    if (!verbose_enabled)
        return;
    
    va_start(ap, fmt);
    (void) vsnprintf(p, 128, fmt, ap);
    va_end(ap);
    printf("%s", p);
}

/* Calculate the average value of an image */
static int average(unsigned char *p1, int p1w, int w, int h) {
    int sum = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            sum += p1[y * p1w + x];
    return sum / (w * h);
}

#if 0
/* Calculate the variance of an image */
static double variance(unsigned char *p1, int p1w, double mu1, int w, int h) {
    double sum = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double t1 = (double)p1[y * p1w + x] - mu1;
            sum += t1 * t1;
        }
    }
    return sum / (w * h);
}

/* Calculate the covariance of 2 images */
static double covariance(unsigned char *p1, int p1w, double mu1, unsigned char *p2, int p2w, double mu2, int w, int h) {
    double sum = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double t1 = (double)p1[y * p1w + x] - mu1;
            double t2 = (double)p2[y * p2w + x] - mu2;
            sum += t1 * t2;
        }
    }
    return sum / (w * h);
}
#endif

/* Load the image and resize it */
static unsigned int image_load(char *filename, int width) {
    int w, h, bpp;
    unsigned char *orig_img = stbi_load(filename, &w, &h, &bpp, STBI_grey);
    int height = (h * width) / w;

    verbose("orig:    %d x %d x %d\n", w, h, bpp);

    img = malloc(width * height);
    stbir_resize_uint8(orig_img, w , h , 0,
                       img, width, height, 0, 1);
    
    verbose("resized: %d x %d x %d\n", width, height, 1);

    return height;
}

static void image_free() {
    stbi_image_free(img);    
}

/* Calculate a guassian blur kernel, the first element of the array containes the kernel radius */
static double *gb_kernel(int radius, double mod) {
    double *kernel;
    double twoRadiusSquaredRecip = 1.0 / (2.0 * radius * radius);
    double sqrtTwoPiTimesRadiusRecip = 1.0 / (sqrt(2.0 * M_PI) * radius);
    double s = 0;
    kernel = malloc((radius * 2 + 2) * sizeof(double));

    for (int i = 0, r = -radius; i < radius * 2 + 1; i++, r++) {
        kernel[i + 1] = sqrtTwoPiTimesRadiusRecip * exp(-(r * r * mod * mod) * twoRadiusSquaredRecip);
        s += kernel[i + 1];
    }

    for (int i = 0; i < radius * 2 + 1; i++)
        kernel[i + 1] /= s;

    kernel[0] = radius;

    return kernel;
}

/* Gaussian blur */
static void gb_blur(double *kernel, unsigned char *p, int pw, int w, int h) {
    int radius = kernel[0];
    unsigned char *t = calloc(1, (w + 2 * radius) * (h + 2 * radius));

    for (int y = 0; y < h; y++) /* Copy the original image */
        memcpy(t + ((w + 2 * radius) * (y + radius)) + radius, p + (pw * y), w);

    for (int y = 0; y < radius; y++) /* Extend top row */
        memcpy(t + ((w + 2 * radius) * y) + radius, p, w);

    for (int y = h; y < h + radius; y++) /* Extend bottom row */
        memcpy(t + ((w + 2 * radius) * y) + radius, p + (pw * (h - 1)), w);

    for (int x = 0; x < radius; x++) /* Extend left column */
        for (int y = 0; y < h + 2 * radius; y++)
            t[y * (w + 2 * radius) + x] = t[y * (w + 2 * radius) + radius];

    for (int x = w; x < w + radius; x++) /* Extend right column */
        for (int y = 0; y < h + 2 * radius; y++)
            t[y * (w + 2 * radius) + x] = t[y * (w + 2 * radius) + (w - 1)];
    
    for (int y = 0; y < h; y++) /* Blur */
        for (int x = 0; x < w; x++) {
            double np = 0;
            for (int ry = -radius; ry <= radius; ry++)
                for (int rx = -radius; rx <= radius; rx++)
                    np += t[(y + ry + radius) * (pw + 2 * radius) + radius + x + rx] * kernel[rx + radius + 1] * kernel[ry + radius + 1];
            p[y * pw + x] = np;
        }

    free(t);
}

static font_t *font_load(char *filename, int invert, int font_width, int font_height) {
    font_t *font;
    unsigned char *font_img;
    int width, height, bpp;
    font_img = stbi_load(filename, &width, &height, &bpp, STBI_grey);
    verbose("%d x %d x %d\n", width, height, bpp);
    assert(width == (font_width * 16));
    assert(height == (font_height * 16));
    assert(bpp == 1);

    unsigned char inv = (invert ? 0 : 255);
    int nr_chars = 256;
    font = calloc(1, sizeof(font_t) + nr_chars * sizeof(font_char_t *));
    font->nr_chars = nr_chars;
    font->width = font_width;
    font->height = font_height;

    double *k = gb_kernel(2.0, 2.0);
    
    for (int c = 0; c < 256; c++) {
        int row = c / 16;
        int col = c % 16;

        font_char_t *fc = calloc(1, sizeof(font_char_t) + font->width * font->height);
        font->chars[c] = fc;

        for (int y = 0; y < font->height; y++) {
            for (int x = 0; x < font->width; x++) {
                fc->num = c;
                fc->allowed = 0;
                unsigned char p = font_img[(row * font->height + y) * (16 * font->width) + (col * font->width + x)];
                fc->data[y * font->width + x] = (p != 0 ? inv : ~inv);
                //printf("%c", p == 0 ? ' ' : '#');
            }
            //printf("\n");
        }
        fc->avg = average(fc->data, font->width, font->width, font->height);
#if 0
        fc->var = variance(fc->data, font->width, fc->avg, font->width, font->height);
#endif

        gb_blur(k, fc->data, font->width, font->width, font->height);
    }
    stbi_image_free(font_img);    
    
    return font;
}

static void font_normalize(font_t *f) {
    f->max = 0;
    f->min = 255;
    for (int c = 0; c < f->nr_chars; c++) {
        if (f->chars[c]->allowed && f->chars[c]->avg > f->max)
            f->max = f->chars[c]->avg;
        if (f->chars[c]->allowed && f->chars[c]->avg < f->min)
            f->min = f->chars[c]->avg;
    }

    verbose("MIN: %d   MAX: %d\n", f->min, f->max);
}

static void font_free(font_t *f) {
    for (int c = 0; c < f->nr_chars; c++)
        free(f->chars[c]);
    free(f);
}

static void font_set_allowed(font_t *f, int nr, int allowed) {
    f->chars[nr]->allowed = allowed;
}

static void font_set_allowed_range(font_t *f, int start, int end, int allowed) {
    for (int c = start; c <= end; c++)
        f->chars[c]->allowed = allowed;
}

static int distance_diff(font_t *f, int c, unsigned char *p, int pw, int max_dist) {
    int distance = 0;
    for (int y = 0; y < f->height; y++) {
        for (int x = 0; x < f->width; x++) {
            int diff = abs(p[y * pw + x] - f->chars[c]->data[y * f->width + x]);
            distance += (diff * diff);
            if (distance > max_dist)
                return distance;
        }
    }

    return distance;
}

static int distance_avg_diff(font_t *f, int c, unsigned char *p, int pw, int max_dist) {
    return f->width * f->height * abs(f->chars[c]->avg - average(p, pw, f->width, f->height));
} 

static int find_lowest_distance(font_t *f, unsigned char *p, int pw, int flags) {
    int min = INT_MAX;
    int min_c = 0;
    for (int c = 0; c < f->nr_chars; c++) {
        if (f->chars[c]->allowed) {
            int fdist = 1, adist = 1;

            if (flags & 1)
                fdist = distance_diff(f, c, p, pw, min);

            if (flags & 2)
                adist = sqrt(distance_avg_diff(f, c, p, pw, min) + 0.1);

            int dist = fdist * adist;

            if (dist < min) {
                min = dist;
                min_c = c;
            }
        }
    }
    //printf("%c %d %d\n", min_c, min_c, min);
    return min_c;
}

static void error(char *fmt, ...) {
    char p[256];
    va_list ap;
    
    va_start(ap, fmt);
    (void) vsnprintf(p, 128, fmt, ap);
    va_end(ap);
    printf("%s", p);
    exit(1);
}

static void parse_allowed_chars(font_t *f, char *str) {
    char *p = strdup(str);
    char *tok, *r;
    int allowed;
    while ((tok = strsep(&p, ",")) != NULL) {
        allowed = 1;
        if (tok[0] == '-') {
            allowed = 0;
            tok++;
        }
        if ((r = index(tok, '-')) != NULL) { /* range */
            *r = '\0'; r++;
            int cb = atoi(tok);
            if (cb < 0 || cb > 255)
                error("Invalid character: %i\n", cb);
            int ce = atoi(r);
            if (ce < 0 || ce > 255)
                error("Invalid character: %i\n", ce);
            if (cb >= ce)
                error("Invalid range: %d - %d\n", cb, ce);

            font_set_allowed_range(f, cb, ce, allowed);
        }
        else { /* Single character */
            int c = atoi(tok);
            if (c < 0 || c > 255)
                error("Invalid character: %i\n", c);
            font_set_allowed(f, c, allowed);
        }
    }

    free(p);
}

static void help(const char *name, int exitvalue) {
    printf("Convert image to ASCII\n"
           "       %s [option] <in_file>\n"
           "option:\n"
           "  -h, --help              show this help messagen\n"
           "  -v, --verbose           show info about the process\n"
           "  -w, --width <width>     set the width (default = 56\n"
           "  -i, --inverse           invert the font, (black on white)\n"
           "  -a, --noaverage         disable the 'average' component in the distance calculation\n"
           "  -s, --noshape           disable the 'shape' component in the distance calculation\n"
           "  -n, --nonormalize       disable normalization of the image based on the selected characters\n"
           "  -c, --chars <charlist>  select the characters. (default = 32-254,-130-140,-145-156,-159)\n"
           "                          other examples:   32-127              (plain ascii characters\n"
           "                                            32,176-178,219-223  (shape blocks)\n"
           "\n",
           name);
    exit(exitvalue);
}

static const struct option longopts[] =
{
  { "help",        no_argument,       NULL, 'h' },
  { "verbose",     no_argument,       NULL, 'v' },
  { "width",       required_argument, NULL, 'w' },
  { "inverse",     no_argument,       NULL, 'i' },
  { "noaverage",   no_argument,       NULL, 'a' },
  { "noshape",     no_argument,       NULL, 's' },
  { "nonormalize", no_argument,       NULL, 'n' },
  { "chars",       required_argument, NULL, 'c' },
  { NULL,     0,                  NULL, 0 }
};

int main(int argc, char **argv) {
    static font_t *font;
    int w = 56;
    int opt;
    int ret = 0;
    char *input = NULL;
    int invert = 0;
    char *characters = "32-254,-130-140,-145-156,-159";

    int flags = 3;
    int normalize = 1;

    while ((opt = getopt_long(argc, (char * const *)argv, "nsac:iw:hv", longopts, 0)) != -1) {
        switch (opt) {
            case 'n':
                normalize = 0;
                break;
            case 's':
                flags &= ~1;
                break;
            case 'a':
                flags &= ~2;
                break;
            case 'c':
                characters = optarg;
                break;
            case 'w':
                w = atoi(optarg);
                break;
            case 'h':
                help(argv[0], 0);
                break;
            case 'v':
                verbose_enabled = 1;
                break;
            case 'i':
                invert = 1;
                break;
            default:
                printf("unknown option : -%c\n", opt);
                help(argv[0], -1);
        }
    }

    if (optind == argc) {
        printf("must input source image\n");
        help(argv[0], -1);
    }
    else {
        input = (char *)argv[optind];
    }

    font = font_load("ASCII8.ASC.png", invert, FONT_WIDTH, FONT_HEIGHT);
    parse_allowed_chars(font, characters);

    /* Find the minimum and maximum 'brightness' of the allowed characters */
    font_normalize(font);

    /* Load the image */
    int pw = w * font->width;
    int ph = image_load(input, pw);
    int h = ph / font->height;

    /* Blur the image a bit */
    double *k = gb_kernel(3, 2.0);
    gb_blur(k, img, pw, pw, ph);

    if (normalize) {
        int img_min = 255;
        int img_max = 0;
        int range = font->max - font->min;
        for (int p = 0; p < pw * ph; p++) {
            if (img[p] < img_min) img_min = img[p];
            if (img[p] > img_max) img_max = img[p];
            img[p] = (unsigned char)((img[p] * range) / 255.0 + font->min);
        }
        verbose("IMG: MIN: %d  MAX: %d\n", img_min, img_max);
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned char *p = &img[y * font->height * pw + x * font->width]; /* top left of char cell */
            int c = find_lowest_distance(font, p, pw, flags);
            printf("%c", font->chars[c]->num);
        }
        printf("\n");
    }
    
    font_free(font);
}

// vim: expandtab:ts=4:sw=4
