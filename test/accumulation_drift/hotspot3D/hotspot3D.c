// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm -arg=512 -arg=8 -arg=100 -arg=%S/data/power_512x8 -arg=%S/data/temp_512x8' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

#include <stdio.h>
#include <stdlib.h> 
#include <string.h>

#define STR_SIZE (256)
#define MAX_PD	(3.0e6)
/* required precision in degrees	*/
#define PRECISION	0.001
#define SPEC_HEAT_SI 1.75e6
#define K_SI 100
/* capacitance fitting factor	*/
#define FACTOR_CHIP	0.5


/* chip parameters	*/
double t_chip = 0.0005;
double chip_height = 0.016; double chip_width = 0.016; 
/* ambient temperature, assuming no package at all	*/
double amb_temp = 80.0;

void fatal(char *s)
{
    fprintf(stderr, "Error: %s\n", s);
}

void readinput(double *vect, int grid_rows, int grid_cols, int layers, char *file) {
    int i,j,k;
    FILE *fp;
    char str[STR_SIZE];
    double val;

    if( (fp  = fopen(file, "r" )) ==0 )
      fatal( "The file was not opened" );


    for (i=0; i <= grid_rows-1; i++) 
      for (j=0; j <= grid_cols-1; j++)
        for (k=0; k <= layers-1; k++)
          {
            if (fgets(str, STR_SIZE, fp) == NULL) fatal("Error reading file\n");
            if (feof(fp))
              fatal("not enough lines in file");
            if ((sscanf(str, "%lf", &val) != 1))
              fatal("invalid file format");
            vect[i*grid_cols+j+k*grid_rows*grid_cols] = val;
          }

    fclose(fp);	

}




void computeTempCPU(double *pIn, double* tIn, double *tOut, 
        int nx, int ny, int nz, double Cap, 
        double Rx, double Ry, double Rz, 
        double dt, int numiter) 
{   double ce, cw, cn, cs, ct, cb, cc;
    double stepDivCap = dt / Cap;
    ce = cw =stepDivCap/ Rx;
    cn = cs =stepDivCap/ Ry;
    ct = cb =stepDivCap/ Rz;

    cc = 1.0 - (2.0*ce + 2.0*cn + 3.0*ct);

    int c,w,e,n,s,b,t;
    int x,y,z;
    int i = 0;
    do{
        for(z = 0; z < nz; z++)
            for(y = 0; y < ny; y++)
                for(x = 0; x < nx; x++)
                {
                    c = x + y * nx + z * nx * ny;

                    w = (x == 0) ? c      : c - 1;
                    e = (x == nx - 1) ? c : c + 1;
                    n = (y == 0) ? c      : c - nx;
                    s = (y == ny - 1) ? c : c + nx;
                    b = (z == 0) ? c      : c - nx * ny;
                    t = (z == nz - 1) ? c : c + nx * ny;


                    tOut[c] = tIn[c]*cc + tIn[n]*cn + tIn[s]*cs + tIn[e]*ce + tIn[w]*cw + tIn[t]*ct + tIn[b]*cb + (dt/Cap) * pIn[c] + ct*amb_temp;
                }
        double *temp = tIn;
        tIn = tOut;
        tOut = temp; 
        i++;
    }
    while(i < numiter);

}

void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: %s <rows/cols> <layers> <iterations> <powerFile> <tempFile>\n", argv[0]);
    fprintf(stderr, "\t<rows/cols>  - number of rows/cols in the grid (positive integer)\n");
    fprintf(stderr, "\t<layers>  - number of layers in the grid (positive integer)\n");
    fprintf(stderr, "\t<iteration> - number of iterations\n");
    fprintf(stderr, "\t<powerFile>  - name of the file containing the initial power values of each cell\n");
    fprintf(stderr, "\t<tempFile>  - name of the file containing the initial temperature values of each cell\n");
    exit(1);
}



int main(int argc, char** argv)
{
    if (argc != 6)
    {
        usage(argc,argv);
    }

    char *pfile, *tfile;
    int iterations = atoi(argv[3]);

    pfile = argv[4];
    tfile = argv[5];
    int numCols = atoi(argv[1]);
    int numRows = atoi(argv[1]);
    int layers = atoi(argv[2]);

    /* calculating parameters*/

    double dx = chip_height/numRows;
    double dy = chip_width/numCols;
    double dz = t_chip/layers;

    double Cap = FACTOR_CHIP * SPEC_HEAT_SI * t_chip * dx * dy;
    double Rx = dy / (2.0 * K_SI * t_chip * dx);
    double Ry = dx / (2.0 * K_SI * t_chip * dy);
    double Rz = dz / (K_SI * dx * dy);

    double max_slope = MAX_PD / (FACTOR_CHIP * t_chip * SPEC_HEAT_SI);
    double dt = PRECISION / max_slope;


    double *powerIn, *tempOut, *tempIn;
    int size = numCols * numRows * layers;

    powerIn = (double*)calloc(size, sizeof(double));
    tempIn = (double*)calloc(size,sizeof(double));
    tempOut = (double*)calloc(size, sizeof(double));
    readinput(powerIn,numRows, numCols, layers,pfile);
    readinput(tempIn, numRows, numCols, layers, tfile);

    computeTempCPU(powerIn, tempIn, tempOut, numCols, numRows, layers, Cap, Rx, Ry, Rz, dt,iterations);

    /* 打印一个最核心的结果：中心网格点温度 */
    int center_idx = size / 2;
    printf("%.15e\n", tempOut[center_idx]);
    free(tempIn);
    free(tempOut); 
    free(powerIn);
    return 0;
}	


