

#include "glide.h"


main()
{
   int type;
   GrHwConfiguration config;

   grGlideInit();

   type = grSstQueryHardware(&config);
   printf("type = %d\n", type);
   return 0;
}

