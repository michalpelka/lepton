#pragma once
#include <stdio.h>

static void save_pgm_file(unsigned int lepton_image[240][80])
{
    int i;
    int j;
    unsigned int maxval = 0;
    unsigned int minval = UINT_MAX;
    char image_name[32] = "IMG.pgm";
    int image_index = 0;

    FILE *f = fopen(image_name, "w+");
    if (f == NULL)
    {
        printf("Error opening file!\n");
        exit(1);
    }

    printf("Calculating min/max values for proper scaling...\n");
    for(i = 0; i < 240; i++)
    {

        for(j = 0; j < 80; j++)
        {
            if (lepton_image[i][j] > maxval) {
                maxval = lepton_image[i][j];
            }
            if (lepton_image[i][j] < minval) {
                minval = lepton_image[i][j];
            }
        }
    }
    printf("maxval = %u\n",maxval);
    printf("minval = %u\n",minval);

    fprintf(f,"P2\n160 120\n%u\n",maxval-minval);
    for(i=0; i < 240; i += 2)
    {
        /* first 80 pixels in row */
        for(j = 0; j < 80; j++)
        {
            fprintf(f,"%d ", lepton_image[i][j] - minval);
        }

        /* second 80 pixels in row */
        for(j = 0; j < 80; j++)
        {
            fprintf(f,"%d ", lepton_image[i + 1][j] - minval);
        }
        fprintf(f,"\n");
    }
    fprintf(f,"\n\n");

    fclose(f);

    //launch image viewer
    //execlp("gpicview", image_name, NULL);
}
