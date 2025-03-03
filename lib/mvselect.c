/* Decide use which version IR by Environment Value.
 * Created by Xiaokang Fan on Feb 21 2025.
 */
#include <stdlib.h>
#include <string.h>
// #include <stdio.h>

static const char *envname = "PERCI_TUNER_SPEED_FIRST";
static const char *val_speed_first = "TRUE";

int mvselect()
{
  int res = 0;

  char *mvval = getenv(envname);

  if (mvval == NULL)
  {
    // printf("mvselect: no env! use precision_first mode");
    res = 0;
  }
  else if (strcmp(mvval, val_speed_first) == 0)
  {
    // printf("mvselect: speed_first");
    res = 1;
  }
  else
  {
    // printf("mvselect: precision_first");
    res = 0;
  }
  return res;
}
