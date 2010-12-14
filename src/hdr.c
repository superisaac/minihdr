#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "gd.h"

#define GRAY(r, g, b) (((5 * r + 9 * g + 2 * b) >>4))

static char * output_filename = "hdr.JPG";
static int prefered_gray = 128;

typedef struct {
  int gray;
  int color[3];
  int x;
  int y;
} SAMPLE;

typedef struct {
  gdImagePtr ptr;
  int numPixel;
  int channelHist[256];
  double avg;
  double var;
  int width;
  int height;
  char * filename;
  SAMPLE samples[7];
  int transmit[3][256];
} IMAGE;

void getPixel(SAMPLE * ps, gdImagePtr ptr, int x, int y) {
      int color = gdImageGetPixel(ptr, x, y);
      ps->color[0] = gdImageRed(ptr, color);
      ps->color[1] = gdImageGreen(ptr, color);
      ps->color[2] = gdImageBlue(ptr, color);
      ps->gray = GRAY(ps->color[0], ps->color[1], ps->color[2]);
}

static int get_gray(gdImagePtr ptr, int x, int y) {
  int c = gdImageGetPixel(ptr, x, y);
  int r = gdImageRed(ptr, c);
  int g = gdImageGreen(ptr, c);
  int b = gdImageBlue(ptr, c);
  return GRAY(r, g, b);
}

void initializeImage(IMAGE * im, char * filename) {
  int i, x, y, c;
  FILE * f = fopen(filename, "rb");
  printf("Processing %s ...\n", filename);
  im->filename = filename;
  im->ptr = gdImageCreateFromJpeg(f);
  fclose(f);
  im->width = gdImageSX(im->ptr);
  im->height = gdImageSY(im->ptr);
  im->numPixel = 0;
  im->avg = 0;
  im->var = 0;

  im->samples[0].gray = 0;
  im->samples[0].x = -1;
  im->samples[0].y = -1;
  im->samples[6].gray = 0xff;
  im->samples[6].x = -1;
  im->samples[6].y = -1;

  for(c=0; c<3; c++) {
    im->samples[0].color[c] = 0;
    im->samples[6].color[c] = 0xff;
  }

  for(i=0; i<256; i++) {
    im->channelHist[i] = 0;
  }

  double sumgray = 0;
  for(y=0; y<im->height; y++) {
    for(x=0; x<im->width; x++) {
      int gray = get_gray(im->ptr, x, y);
      sumgray += ((double)gray)/256.0;
      im->channelHist[gray] += 1;
      im->numPixel++;
    }
  }
  im->numPixel = im->width * im->height;
  double avg = sumgray/ (double)(im->numPixel);
  im->avg = (int)(sumgray/ (double)(im->numPixel) * 256);
  //im->var = 1000.0/(double)(1.0 + im->avg * (255 - im->avg));
  im->var = (im->avg - prefered_gray) * (im->avg - prefered_gray);

}

#define CELL 10
#define CELL_SZ 100
static int sort_by_sample_gray(const void * s1, const void * s2) {
  return ((SAMPLE*)s1)->gray - ((SAMPLE*)s2)->gray;
}

static void measureSamples(IMAGE * im) {
  int i=0, c;
  int x, y;
  SAMPLE samples[CELL_SZ];
  int wc = im->width / CELL;
  int hc = im->height / CELL;
  for(y=hc ; y < im->height; y += hc) {
    for(x=wc; x < im->width; x += wc) {
      samples[i].x = x;
      samples[i].y = y;
      getPixel(samples + i, im->ptr, x, y);
      i++;
    }
  }

  qsort(samples, i, sizeof(SAMPLE), sort_by_sample_gray);
  memcpy(im->samples + 1, samples, sizeof(SAMPLE));
  memcpy(im->samples + 2, samples + (i>>2), sizeof(SAMPLE));
  memcpy(im->samples + 3, samples + (i>>1), sizeof(SAMPLE));
  memcpy(im->samples + 4, samples + ((3 * i)>>2), sizeof(SAMPLE));
  memcpy(im->samples + 5, samples + i - 1, sizeof(SAMPLE));

  for(i=0; i<256; i++) {
    for(c=0; c<3; c++) {
      im->transmit[c][i] = i;
    }
  }
}

static void getColorBound(IMAGE * im, int which, int index, int *low_cv, int *high_cv)
{
  *low_cv = im->samples[index - 1].color[which];
  *high_cv = im->samples[index].color[which];
}

static void buildMapping(IMAGE * im, IMAGE * template) {
  int i;
  for(i=1; i<6; i++) {
    im->samples[i].x = template->samples[i].x;
    im->samples[i].y = template->samples[i].y;
    getPixel(im->samples + i, im->ptr, template->samples[i].x,
	     template->samples[i].y);
  }

  int color;
  for(color=0; color<3; color++) {
    int didx;
    for(i=0; i<256; i++) {
      im->transmit[color][i] = i;
    }

    for(didx=1; didx<=6; didx++) {
      int low_cv = 0;
      int high_cv = 256;
      getColorBound(im, color, didx, &low_cv, &high_cv);

      int t_low_cv = 0;
      int t_high_cv = 255;
      getColorBound(template, color, didx, &t_low_cv, &t_high_cv);
      int cv;
      for(cv=low_cv; cv < high_cv; cv++) {
	double t = (double)(cv - low_cv)/(double)(high_cv - low_cv);
	int new_cv = (int)(t * (double)(t_high_cv - t_low_cv) + t_low_cv);
	if(new_cv >= 256 || new_cv < 0) {
	  fprintf(stderr, "Illegal cv %d\n", new_cv);
	}
	im->transmit[color][cv] = new_cv;
      }
      if(low_cv == high_cv) {
	im->transmit[color][low_cv] = t_low_cv;
      }
    }
  }
}

void destroyImage(IMAGE * im){
  gdImageDestroy(im->ptr);
}

static void printImage(IMAGE * im) {
  printf("im: file=%s\n  size=(%dx%d)\n  average=%g\n  var=%f\n", 
	 im->filename, im->width, im->height, im->avg,
	 im->var);
}

int parseOptions(int argc, char ** argv) {
  static const struct option long_options[] = {
    {"output", required_argument, NULL, 'o'},
    {"gray-level", required_argument, NULL, 'g'},
    {"help", no_argument, NULL, 'h'},
    {NULL, no_argument, NULL, 0}
  };

  int c, v;
  int long_idx;
  while((c = getopt_long(argc, argv, "o:g:h", long_options, &long_idx)) != -1) {
    switch(c) {
    case 'o':
      output_filename = strdup(optarg);
      break;
    case 'g':
      v = atol(optarg);
      if(v >= 0 && v <= 255) {
	prefered_gray = v;
      } else {
	fprintf(stderr, "Illegal gray value %d, gray-level should be in [0, 255]\n", v);
	exit(-4);
      }
      break;
    case 'h':
      printf("Usage: %s [-o output] [-g gray-level ] <jpeg files>\n", argv[0]);
      exit(0);
    default:
      printf("long_idx is %s long_idx\n", long_idx);
      break;
    }
  }
  return optind;
}

int main(int argc, char ** argv)
{
  FILE * outfile;
  int i, x, y, color;

  if(argc <= 1) 
  {
    printf("Usage: %s [-o output] [-g gray-level ] <jpeg files>\n", argv[0]);
    return 2;
  }

  int ind = parseOptions(argc, argv);
  argc -= ind;
  argv += ind;
  int candidate_sz = argc;
  IMAGE * imglist = (IMAGE*)calloc(candidate_sz, sizeof(IMAGE));
  IMAGE * template = NULL;
  gdImagePtr destIm;

  for(i=0; i<candidate_sz; i++) {
    initializeImage(imglist + i, argv[i]);
    //printImage(imglist + i);
  }

  // find template IMAGE
  for(i=0; i<candidate_sz; i++) {
    if(!template || template->var > imglist[i].var) {
      template = imglist + i;
    }
  }

  printf("Template IMAGE is %s\n", template->filename);
  measureSamples(template);

  for(i=0; i<candidate_sz; i++) {
    if(imglist +i != template) {
      buildMapping(imglist + i, template);
    }
  }
  printf("Generating HDR from %d images ...\n", candidate_sz);
  destIm = gdImageCreateTrueColor(template->width, template->height);
  //gdImageCopy(destIm, template->ptr, 0, 0, 0, 0, template->width, template->height);
  double ka = 10.0/(double)prefered_gray;
  double kb = 10.0/(255.0 - prefered_gray);
  for(y=0; y<template->height; y++) {
    for(x=0; x<template->width; x++) {
      int sum_w[3];
      int sum_cv[3];
      for(color=0; color<3; color++) {
	sum_w[color] = 0;
	sum_cv[color] = 0;
      }

      for(i=0; i<candidate_sz; i++) {
	IMAGE * im = imglist + i;
	SAMPLE sample;
	getPixel(&sample, im->ptr, x, y);
	for(color=0; color<3; color++) {
	  int cv = sample.color[color];
	  //int w = cv * (255 - cv) + 1;
	  //int w = (cv >= 128)?(256 - cv): (cv + 1);
	  int w = (cv >= prefered_gray)?(ka* cv + 1):(kb * (255 - cv) + 1);
	  cv = im->transmit[color][cv];
	  sum_cv[color] += cv * w;
	  sum_w[color] += w;
	}
      }

      int curr_color[3];
      for(color=0; color<3; color++) {
	curr_color[color] = sum_cv[color]/sum_w[color];
      }
      int new_c = gdImageColorAllocate(destIm, curr_color[0], curr_color[1],
				       curr_color[2]);
      gdImageSetPixel(destIm, x, y, new_c);
    }
  }

  
  printf("Write output to %s\n", output_filename);
  outfile = fopen(output_filename, "wb");
  gdImageJpeg(destIm, outfile, -1);
  fclose(outfile);
  gdImageDestroy(destIm);

  for(i=0; i<candidate_sz; i++) {
    destroyImage(imglist+i);
  }
  free(imglist);
  return 0;
}
