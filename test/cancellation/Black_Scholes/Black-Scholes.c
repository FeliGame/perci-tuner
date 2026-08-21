// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize -no-build "%s" -lm -arg=%S/input_test.txt -arg=%t.prices -arg=1000000' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

typedef struct OptionData_ {
        double s;          // spot price
        double strike;     // strike price
        double r;          // risk-free interest rate
        double divq;       // dividend rate
        double v;          // volatility
        double t;          // time to maturity or option expiration in years 
                           //     (1yr = 1.0, 6mos = 0.5, 3mos = 0.25, ..., etc)  
        char OptionType;   // Option type.  "P"=PUT, "C"=CALL
        double divs;       // dividend vals (not used in this test)
        double DGrefval;   // DerivaGem Reference Value
} OptionData;

OptionData *data;
double *prices;
int numOptions;

int    * otype;
double * sptprice;
double * strike;
double * rate;
double * volatility;
double * otime;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Cumulative Normal Distribution Function
// See Hull, Section 11.8, P.243-244
#define inv_sqrt_2xPI 0.39894228040143270286

double CNDF ( double InputX ) 
{
    int sign;

    double OutputX;
    double xInput;
    double xNPrimeofX;
    double expValues;
    double xK2;
    double xK2_2, xK2_3;
    double xK2_4, xK2_5;
    double xLocal, xLocal_1;
    double xLocal_2, xLocal_3;

    // Check for negative value of InputX
    if (InputX < 0.0) {
        InputX = -InputX;
        sign = 1;
    } else 
        sign = 0;

    xInput = InputX;
 
    // Compute NPrimeX term common to both four & six decimal accuracy calcs
    expValues = exp(-0.5 * InputX * InputX);
    xNPrimeofX = expValues;
    xNPrimeofX = xNPrimeofX * inv_sqrt_2xPI;

    xK2 = 0.2316419 * xInput;
    xK2 = 1.0 + xK2;
    xK2 = 1.0 / xK2;
    xK2_2 = xK2 * xK2;
    xK2_3 = xK2_2 * xK2;
    xK2_4 = xK2_3 * xK2;
    xK2_5 = xK2_4 * xK2;
    
    xLocal_1 = xK2 * 0.319381530;
    xLocal_2 = xK2_2 * (-0.356563782);
    xLocal_3 = xK2_3 * 1.781477937;
    xLocal_2 = xLocal_2 + xLocal_3;
    xLocal_3 = xK2_4 * (-1.821255978);
    xLocal_2 = xLocal_2 + xLocal_3;
    xLocal_3 = xK2_5 * 1.330274429;
    xLocal_2 = xLocal_2 + xLocal_3;

    xLocal_1 = xLocal_2 + xLocal_1;
    xLocal   = xLocal_1 * xNPrimeofX;
    xLocal   = 1.0 - xLocal;

    OutputX  = xLocal;
    
    if (sign) {
        OutputX = 1.0 - OutputX;
    }
    
    return OutputX;
} 

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
double BlkSchlsEqEuroNoDiv( double sptprice,
                            double strike, double rate, double volatility,
                            double time, int otype )
{
    double OptionPrice;

    // local private working variables for the calculation
    double xStockPrice;
    double xStrikePrice;
    double xRiskFreeRate;
    double xVolatility;
    double xTime;
    double xSqrtTime;

    double logValues;
    double xLogTerm;
    double xD1; 
    double xD2;
    double xPowerTerm;
    double xDen;
    double d1;
    double d2;
    double FutureValueX;
    double NofXd1;
    double NofXd2;
    double NegNofXd1;
    double NegNofXd2;    
    
    xStockPrice = sptprice;
    xStrikePrice = strike;
    xRiskFreeRate = rate;
    xVolatility = volatility;

    xTime = time;
    xSqrtTime = sqrt(xTime);

    logValues = log( sptprice / strike );
        
    xLogTerm = logValues;
        
    
    xPowerTerm = xVolatility * xVolatility;
    xPowerTerm = xPowerTerm * 0.5;
        
    xD1 = xRiskFreeRate + xPowerTerm;
    xD1 = xD1 * xTime;
    xD1 = xD1 + xLogTerm;

    xDen = xVolatility * xSqrtTime;
    xD1 = xD1 / xDen;
    xD2 = xD1 -  xDen;

    d1 = xD1;
    d2 = xD2;
    
    NofXd1 = CNDF( d1 );
    NofXd2 = CNDF( d2 );

    FutureValueX = strike * ( exp( -(rate)*(time) ) );        
    if (otype == 0) {            
        OptionPrice = (sptprice * NofXd1) - (FutureValueX * NofXd2);
    } else { 
        NegNofXd1 = (1.0 - NofXd1);
        NegNofXd2 = (1.0 - NofXd2);
        OptionPrice = (FutureValueX * NegNofXd2) - (sptprice * NegNofXd1);
    }
    
    return OptionPrice;
}

int main (int argc, char **argv)
{
    FILE *file;
    int i;
    int run;
    int numRuns = 10000;  // default repeat count to increase runtime
    int loopnum;
    double * buffer;
    int * buffer2;
    int rv;

   if (argc != 3 && argc != 4)
        {
                fprintf(stderr, "Usage:\n\t%s <inputFile> <dummyOutputName> [numRuns]\n", argv[0]);
                exit(1);
        }
    char *inputFile = argv[1];
    char *outputFile = argv[2];
    if (argc == 4) {
        numRuns = atoi(argv[3]);
        if (numRuns < 1) {
            numRuns = 1;
        }
    }

    //Read input data from file
    file = fopen(inputFile, "r");
    if(file == NULL) {
      fprintf(stderr, "ERROR: Unable to open file `%s'.\n", inputFile);
      exit(1);
    }
    rv = fscanf(file, "%i", &numOptions);
    if(rv != 1) {
      fprintf(stderr, "ERROR: Unable to read from file `%s'.\n", inputFile);
      fclose(file);
      exit(1);
    }

    // alloc spaces for the option data
    data = (OptionData*)malloc(numOptions*sizeof(OptionData));
    prices = (double*)malloc(numOptions*sizeof(double));
    for ( loopnum = 0; loopnum < numOptions; ++ loopnum )
    {
        rv = fscanf(file, "%lf %lf %lf %lf %lf %lf %c %lf %lf", &data[loopnum].s, &data[loopnum].strike, &data[loopnum].r, &data[loopnum].divq, &data[loopnum].v, &data[loopnum].t, &data[loopnum].OptionType, &data[loopnum].divs, &data[loopnum].DGrefval);
        if(rv != 9) {
          fprintf(stderr, "ERROR: Unable to read from file `%s'.\n", inputFile);
          fclose(file);
          exit(1);
        }
    }
    rv = fclose(file);
    if(rv != 0) {
      fprintf(stderr, "ERROR: Unable to close file `%s'.\n", inputFile);
      exit(1);
    }

#define PAD 256
#define LINESIZE 64

    buffer = (double *) malloc(5 * numOptions * sizeof(double) + PAD);
    sptprice = (double *) (((unsigned long long)buffer + PAD) & ~(LINESIZE - 1));
    strike = sptprice + numOptions;
    rate = strike + numOptions;
    volatility = rate + numOptions;
    otime = volatility + numOptions;

    buffer2 = (int *) malloc(numOptions * sizeof(double) + PAD);
    otype = (int *) (((unsigned long long)buffer2 + PAD) & ~(LINESIZE - 1));

    for (i=0; i<numOptions; i++) {
        otype[i]      = (data[i].OptionType == 'P') ? 1 : 0;
        sptprice[i]   = data[i].s;
        strike[i]     = data[i].strike;
        rate[i]       = data[i].r;
        volatility[i] = data[i].v;    
        otime[i]      = data[i].t;
    }

    // Serial computation of Black-Scholes prices, repeated numRuns times
    for (run = 0; run < numRuns; run++) {
        for (i = 0; i < numOptions; i++) {
            prices[i] = BlkSchlsEqEuroNoDiv(
                sptprice[i],
                strike[i],
                rate[i],
                volatility[i],
                otime[i],
                otype[i]
            );
        }
    }

    // Compute a single representative floating-point result:
    // the sum of all option prices.
    double sumPrices = 0.0;
    for (i = 0; i < numOptions; i++) {
        sumPrices += prices[i];
    }
    // Only output this core result for external tools to capture.
    printf("%.18f\n", sumPrices);

    free(data);
    free(prices);

    return 0;
}