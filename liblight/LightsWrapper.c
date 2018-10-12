/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2014 The  Linux Foundation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


// #define LOG_NDEBUG 0

#include <cutils/log.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/ioctl.h>
#include <sys/types.h>

#include <hardware/lights.h>

/******************************************************************************/

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
struct light_device_t *rdev;

const char *const LCD_FILE
        = "/sys/class/leds/lcd-backlight/brightness";

unsigned short lightsMap[] = {
  0, 10, 10, 31, 32, 32, 32, 47, 48, 48, 48, 48, 63, 64, 64, 64, 79, 80,
  80, 80, 80, 95, 96, 96, 111, 112, 112, 112, 126, 128, 128, 142, 144,
  144, 159, 160, 160, 175, 176, 176, 191, 192, 207, 208, 208, 223, 224,
  239, 240, 240, 255, 256, 271, 272, 287, 288, 303, 304, 319, 320, 320,
  335, 336, 350, 352, 367, 368, 383, 384, 399, 400, 415, 416, 431, 447,
  448, 463, 464, 479, 480, 494, 496, 511, 512, 527, 528, 543, 544, 559,
  560, 575, 591, 592, 607, 608, 622, 624, 639, 655, 656, 671, 672, 687,
  703, 704, 719, 720, 735, 751, 752, 767, 782, 784, 799, 815, 816, 831,
  847, 848, 863, 879, 895, 896, 911, 926, 928, 943, 958, 975, 991, 992,
  1007, 1023, 1024, 1039, 1055, 1070, 1072, 1087, 1102, 1104, 1118, 1134,
  1151, 1152, 1167, 1183, 1199, 1200, 1215, 1230, 1232, 1246, 1263, 1279,
  1295, 1296, 1311, 1327, 1342, 1358, 1360, 1374, 1391, 1407, 1422, 1439,
  1455, 1471, 1487, 1503, 1519, 1535, 1551, 1567, 1583, 1599, 1615, 1631,
  1661, 1679, 1695, 1711, 1741, 1759, 1775, 1806, 1823, 1854, 1871, 1887,
  1918, 1951, 1967, 1998, 2015, 2045, 2077, 2109, 2126, 2157, 2189, 2221,
  2253, 2285, 2318, 2350, 2381, 2413, 2446, 2478, 2509, 2542, 2587, 2621,
  2654, 2685, 2718, 2765, 2797, 2829, 2862, 2908, 2941, 2973, 3021, 3053,
  3086, 3117, 3164, 3197, 3245, 3278, 3310, 3357, 3390, 3422, 3469, 3502,
  3533, 3580, 3614, 3645, 3677, 3723, 3758, 3790, 3835, 3869, 3902, 3934,
  3966, 4011, 4046, 4093, 4095,
};
/**
 * device methods
 */

void init_globals(void)
{
  // init the mutex
  pthread_mutex_init(&g_lock, NULL);
}

  static int
write_int(char const* path, int value)
{
  int fd;
  static int already_warned = 0;

  fd = open(path, O_RDWR);
  if (fd >= 0) {
    char buffer[20];
    int bytes = snprintf(buffer, sizeof(buffer), "%d\n", value);
    ssize_t amt = write(fd, buffer, (size_t)bytes);
    close(fd);
    return amt == -1 ? -errno : 0;
  } else {
    if (already_warned == 0) {
      ALOGE("write_int failed to open %s\n", path);
      already_warned = 1;
    }
    return -errno;
  }
}

  static int
rgb_to_brightness(struct light_state_t const* state)
{
  int color = state->color & 0x00ffffff;
  return ((77*((color>>16)&0x00ff))
      + (150*((color>>8)&0x00ff)) + (29*(color&0x00ff))) >> 8;
}

  static int
set_light_backlight(struct light_device_t* dev,
    struct light_state_t const* state)
{
  int err = 0;
  int brightness = rgb_to_brightness(state);
  if(!dev) {
    return -1;
  }
  pthread_mutex_lock(&g_lock);
  err = write_int(LCD_FILE, lightsMap[brightness]);
  pthread_mutex_unlock(&g_lock);
  return err;
}
  static int
close_lights(struct light_device_t *dev)
{
  if (dev) {
    free(dev);
  }
  if (rdev) {
    rdev->common.close((struct hw_device_t *)rdev);
  }
  return 0;
}
static int load(const char *path,
    struct hw_module_t **pHmi)
{
  int status = 0;
  void *handle = NULL;
  struct hw_module_t *hmi = NULL;

  handle = dlopen(path, RTLD_NOW);
  if (handle == NULL) {
    status = -EINVAL;
    goto done;
  }

  hmi = (struct hw_module_t *)dlsym(handle,
      HAL_MODULE_INFO_SYM_AS_STR);
  if (hmi == NULL) {
    status = -EINVAL;
    goto done;
  }

  hmi->dso = handle;

done:
  *pHmi = hmi;

  return status;
}

/******************************************************************************/

/**
 * module methods
 */

/** Open a new instance of a lights device using name */
static int open_lights(const struct hw_module_t* module, char const* name,
    struct hw_device_t** device)
{
  int (*set_light)(struct light_device_t* dev,
      struct light_state_t const* state);

  pthread_once(&g_init, init_globals);
  struct hw_module_t *rmodule;
  int rc = load("/system/lib64/hw/lights.real.so", &rmodule);;
  if(rc != 0)
    return -1;
  struct light_device_t *dev = malloc(sizeof(struct light_device_t));
  if(!dev)
    return -ENOMEM;

  rc = rmodule->methods->open(module, name, (struct hw_device_t **)&rdev);
  if(rc != 0)
    return -1;
  if (0 == strcmp(LIGHT_ID_BACKLIGHT, name))
    set_light = set_light_backlight;
  else
    set_light = rdev->set_light;
  memcpy(dev, rdev, sizeof(*dev));
  dev->common.module = (struct hw_module_t*)module;
  dev->common.close = (int (*)(struct hw_device_t*))close_lights;
  dev->set_light = set_light;

  *device = (struct hw_device_t*)dev;
  return 0;
}

static struct hw_module_methods_t lights_module_methods = {
  .open =  open_lights,
};

/*
 * The lights Module
 */
struct hw_module_t HAL_MODULE_INFO_SYM = {
  .tag = HARDWARE_MODULE_TAG,
  .version_major = 1,
  .version_minor = 0,
  .id = LIGHTS_HARDWARE_MODULE_ID,
  .name = "lights Module Wrapper",
  .author = "LineageOS Project.",
  .methods = &lights_module_methods,
};
