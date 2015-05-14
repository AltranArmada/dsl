#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include "gridsearch.h"

using namespace dsl;

void save_map(const char* map, int width, int height, const char* filename)
{
  int i, ind;
  char data[width*height*3];
  FILE* file = fopen(filename, "w");
  assert(file);
  fprintf(file, "P6\n%d %d 255\n", width, height);
  ind = 0;
  for (i = 0; i < width*height; i++, ind+=3) {
    data[ind] = data[ind+1] = data[ind+2] = (char)(map[i]*100);
  }
  assert(ind == 3*width*height);
  assert((int)fwrite(data, sizeof(char), ind, file) == ind);
  fclose(file);
}


char* load_map(int* width, int* height, const char* filename)
{
  int i, size;
  char *map, *data;
  FILE* file = fopen(filename, "r");
  assert(file);
  assert(fscanf(file, "P6\n%d %d 255\n", width, height));
  size = (*width**height);
  map = (char*)malloc(size);
  data = (char*)malloc(size*3);
  assert(fread(data, sizeof(char), size*3, file));
  for (i = 0; i < size; i++)
    map[i] = (data[3*i] ? 1 : 0);
  free(data);
  fclose(file);
  return map;
}

void timer_start(struct timeval *time) 
{
  gettimeofday(time,(struct timezone*)0);
}

long timer_us(struct timeval *time) 
{
  struct timeval now;
  gettimeofday(&now,(struct timezone*)0);
  return 1000000*(now.tv_sec - time->tv_sec) + now.tv_usec - time->tv_usec;
}


int main(int argc, char** argv)
{
  int width, height; 
  char* chmap = load_map(&width, &height, "map.ppm");
  GridPath path, optPath;
  int x, y;
  char mapPath[width*height];
  struct timeval timer;
  long time;
  int i;

  // create a map
  double map[width*height];
  for (i = 0; i < width*height; ++i)
    map[i] = 1000*(double)chmap[i];

  // this is just for display
  memcpy(mapPath, chmap, width*height);

  // create planner
  GridSearch gdsl(width, height, map);
  gdsl.SetStart(0, height/2);
  gdsl.SetGoal(width - 1, height/2);
  // plan
  timer_start(&timer);
  gdsl.Plan(path);
  time = timer_us(&timer);
  printf("plan path time= %ld\n", time);
  printf("path: count=%d len=%f\n", path.count, path.len);
  // print results
  for (i = 0; i < path.count; ++i) {
    printf("(%d,%d) ", path.pos[2*i], path.pos[2*i+1]);
    mapPath[path.pos[2*i+1]*width + path.pos[2*i]] = 2;
  }
  printf("\n");
  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      printf("%d ", mapPath[y*width + x]);
    }
    printf("\n");
  }
  fflush(stdout);

  // save it to image for viewing
  save_map(mapPath, width, height, "path1.ppm");

  
  // follow path until (28,18)
  gdsl.SetStart(28,18);

  // simulate closing the narrow passage
  if (0) {
    // one way
    gdsl.SetCost(29,18,1000);
    gdsl.SetCost(30,18,1000);
    gdsl.SetCost(31,18,1000);
  } else {
    // a better way
    //    gdsl.RemoveVertex(29,18);
    gdsl.RemoveVertex(30,18);
    //    gdsl.RemoveVertex(31,18);
  }

  // this is just for display
  memcpy(mapPath, chmap, width*height);
  mapPath[18*width + 29] = 1;  mapPath[18*width + 30] = 1;  mapPath[18*width + 31] = 1;

  // replan
  timer_start(&timer);
  gdsl.Plan(path);
  time = timer_us(&timer);
  printf("replan path time= %ld us\n", time);
  printf("path: count=%d len=%f\n", path.count, path.len);
  fflush(stdout);
  

  /*
  // bypass the old vertex
  gdsl.AddEdge(29,18,31,18);
  
  // replan
  timer_start(&timer);
  gdsl.Plan(path);
  time = timer_ns(&timer);
  printf("replan path time= %ld\n", time);
  printf("path: count=%d len=%f\n", path.count, path.len);
  fflush(stdout);
  */

  // optimize path (experimental)
  timer_start(&timer);
  gdsl.OptPath(path, optPath);
  time = timer_us(&timer);
  printf("opt path time= %ld us\n", time);
  printf("optPath: count=%d len=%f\n", optPath.count, optPath.len);
  
  for (i = 0; i < path.count; ++i) {
    printf("(%d,%d) ", path.pos[2*i], path.pos[2*i+1]);
    mapPath[path.pos[2*i+1]*width + path.pos[2*i]] = 2;
  }
  printf("\n");
  for (i = 0; i < optPath.count; ++i) {
    printf("(%d,%d) ", optPath.pos[2*i], optPath.pos[2*i+1]);
    fflush(stdout);
    mapPath[optPath.pos[2*i+1]*width + optPath.pos[2*i]] = 3;
  }
  printf("\n");
  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      printf("%d ", mapPath[y*width + x]);
    }
    printf("\n");
  }

  // save it to image for viewing
  save_map(mapPath, width, height, "path2.ppm");

  getchar();

  return 0;
}