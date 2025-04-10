/* Decide use which version IR by Environment Value.
 * Created by Xiaokang Fan on Feb 21 2025.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *envname = "PERCI_TUNER_ORIGINAL_VERSION";
static const char *original_version_flag = "TRUE";

int mvselect()
{
  char *mvval = getenv(envname);

  if (mvval == NULL)
  {
    // printf("mvselect: no env! use precision_first(original_version)\n");
    return 0;
  }
  if (strcmp(mvval, original_version_flag) == 0)
  {
    // printf("mvselect: precision_first(original_version)\n");
    return 0;
  }
  // printf("mvselect: speed_first(another_version)\n");
  return 1;
}
