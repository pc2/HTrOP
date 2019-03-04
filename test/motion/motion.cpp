#include <unistd.h>
#include <time.h>
#include <functional>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <getopt.h>
#include <sys/time.h>
#include <string>
#include <string.h>
#include <sstream>
#include <vector>
#include <map>
#include <iostream>
#include <chrono>

#include "motion.h"

#ifdef MEASURE_ENERGY
#include "../measureEnergy.h"
#endif

#if VALIDATE
#include "gtest/gtest.h"
#endif

typedef struct globalArgs_t {
    char *imageImage0;
    char *imageImage1;
    char *outputImage;
    bool checkResults;
} GlobalArgs;

static const char *optString = "i:j:o:c:";

static struct option longOpts[] = {
   { "imageImage0", required_argument, NULL, 'i' },
   { "imageImage1", required_argument, NULL, 'j' },
   { "outputImage", required_argument, NULL, 'o' },
   {"checkResults", optional_argument, NULL, 'c'}
};

GlobalArgs globalArgs;

void parseCommmand(int argc, char** argv){
    int longIndex;
    int opt;
    
    while((opt = getopt_long(argc, argv, optString, longOpts, &longIndex)) != -1 ) {
        switch( opt ) {
            case 'i':
                globalArgs.imageImage0 = optarg;
              break;
            case 'j':
                globalArgs.imageImage1 = optarg;
                break;
            case 'o':
              globalArgs.outputImage = optarg;
              break;
            case 'c':
                globalArgs.checkResults = std::atoi(optarg);
                break;
        }
    }
}

void kernel_gauss(unsigned char *inputImage_data, unsigned char *outputImage_data, long rows, long cols) {
    int32_t temp_red = 0, temp_green = 0, temp_blue = 0;
    long i, j, pos;
    
    for (i=0; i<rows; ++i) {
        for (j=0; j<cols; ++j) {
            pos = i*COLS+j;
            
            // Skip borders.
            if( (i) < 3 || (j) < 3 || (i+3) >= rows || (j+3) >= cols  ) {
                continue;
            }
            
            temp_red    =      inputImage_data[(pos-(2*cols)-2) * 3 + 2]
            +    5*inputImage_data[(pos-(2*cols)-1) * 3 + 2]
            +    7*inputImage_data[(pos-(2*cols)  ) * 3 + 2]
            +    5*inputImage_data[(pos-(2*cols)+1) * 3 + 2]
            +      inputImage_data[(pos-(2*cols)+2) * 3 + 2];
            temp_green  =      inputImage_data[(pos-(2*cols)-2) * 3 + 1]
            +    5*inputImage_data[(pos-(2*cols)-1) * 3 + 1]
            +    7*inputImage_data[(pos-(2*cols)  ) * 3 + 1]
            +    5*inputImage_data[(pos-(2*cols)+1) * 3 + 1]
            +      inputImage_data[(pos-(2*cols)+2) * 3 + 1];
            temp_blue   =      inputImage_data[(pos-(2*cols)-2) * 3 + 0]
            +    5*inputImage_data[(pos-(2*cols)-1) * 3 + 0]
            +    7*inputImage_data[(pos-(2*cols)  ) * 3 + 0]
            +    5*inputImage_data[(pos-(2*cols)+1) * 3 + 0]
            +      inputImage_data[(pos-(2*cols)+2) * 3 + 0];
            
            temp_red    +=   5*inputImage_data[(pos-(  cols)-2) * 3 + 2]
            +   20*inputImage_data[(pos-(  cols)-1) * 3 + 2]
            +   33*inputImage_data[(pos-(  cols)  ) * 3 + 2]
            +   20*inputImage_data[(pos-(  cols)+1) * 3 + 2]
            +    5*inputImage_data[(pos-(  cols)+2) * 3 + 2];
            temp_green  +=   5*inputImage_data[(pos-(  cols)-2) * 3 + 1]
            +   20*inputImage_data[(pos-(  cols)-1) * 3 + 1]
            +   33*inputImage_data[(pos-(  cols)  ) * 3 + 1]
            +   20*inputImage_data[(pos-(  cols)+1) * 3 + 1]
            +    5*inputImage_data[(pos-(  cols)+2) * 3 + 1];
            temp_blue   +=   5*inputImage_data[(pos-(  cols)-2) * 3 + 0]
            +   20*inputImage_data[(pos-(  cols)-1) * 3 + 0]
            +   33*inputImage_data[(pos-(  cols)  ) * 3 + 0]
            +   20*inputImage_data[(pos-(  cols)+1) * 3 + 0]
            +    5*inputImage_data[(pos-(  cols)+2) * 3 + 0];
            
            temp_red    +=   7*inputImage_data[(pos-2) * 3 + 2]
            +   33*inputImage_data[(pos-1) * 3 + 2]
            +   55*inputImage_data[(pos  ) * 3 + 2]
            +   33*inputImage_data[(pos+1) * 3 + 2]
            +    7*inputImage_data[(pos+2) * 3 + 2];
            temp_green  +=   7*inputImage_data[(pos-2) * 3 + 1]
            +   33*inputImage_data[(pos-1) * 3 + 1]
            +   55*inputImage_data[(pos  ) * 3 + 1]
            +   33*inputImage_data[(pos+1) * 3 + 1]
            +    7*inputImage_data[(pos+2) * 3 + 1];
            temp_blue   +=   7*inputImage_data[(pos-2) * 3 + 0]
            +   33*inputImage_data[(pos-1) * 3 + 0]
            +   55*inputImage_data[(pos  ) * 3 + 0]
            +   33*inputImage_data[(pos+1) * 3 + 0]
            +    7*inputImage_data[(pos+2) * 3 + 0];
            
            temp_red    +=   5*inputImage_data[(pos+(  cols)-2) * 3 + 2]
            +   20*inputImage_data[(pos+(  cols)-1) * 3 + 2]
            +   33*inputImage_data[(pos+(  cols)  ) * 3 + 2]
            +   20*inputImage_data[(pos+(  cols)+1) * 3 + 2]
            +    5*inputImage_data[(pos+(  cols)+2) * 3 + 2];
            temp_green  +=   5*inputImage_data[(pos+(  cols)-2) * 3 + 1]
            +   20*inputImage_data[(pos+(  cols)-1) * 3 + 1]
            +   33*inputImage_data[(pos+(  cols)  ) * 3 + 1]
            +   20*inputImage_data[(pos+(  cols)+1) * 3 + 1]
            +    5*inputImage_data[(pos+(  cols)+2) * 3 + 1];
            temp_blue   +=   5*inputImage_data[(pos+(  cols)-2) * 3 + 0]
            +   20*inputImage_data[(pos+(  cols)-1) * 3 + 0]
            +   33*inputImage_data[(pos+(  cols)  ) * 3 + 0]
            +   20*inputImage_data[(pos+(  cols)+1) * 3 + 0]
            +    5*inputImage_data[(pos+(  cols)+2) * 3 + 0];
            
            temp_red    +=     inputImage_data[(pos+(2*cols)-2) * 3 + 2]
            +    5*inputImage_data[(pos+(2*cols)-1) * 3 + 2]
            +    7*inputImage_data[(pos+(2*cols)  ) * 3 + 2]
            +    5*inputImage_data[(pos+(2*cols)+1) * 3 + 2]
            +      inputImage_data[(pos+(2*cols)+2) * 3 + 2];
            temp_green  +=     inputImage_data[(pos+(2*cols)-2) * 3 + 1]
            +    5*inputImage_data[(pos+(2*cols)-1) * 3 + 1]
            +    7*inputImage_data[(pos+(2*cols)  ) * 3 + 1]
            +    5*inputImage_data[(pos+(2*cols)+1) * 3 + 1]
            +      inputImage_data[(pos+(2*cols)+2) * 3 + 1];
            temp_blue   +=     inputImage_data[(pos+(2*cols)-2) * 3 + 0]
            +    5*inputImage_data[(pos+(2*cols)-1) * 3 + 0]
            +    7*inputImage_data[(pos+(2*cols)  ) * 3 + 0]
            +    5*inputImage_data[(pos+(2*cols)+1) * 3 + 0]
            +      inputImage_data[(pos+(2*cols)+2) * 3 + 0];
            
            outputImage_data[pos * 3 + 2]   = temp_red      / 339;
            outputImage_data[pos * 3 + 1]   = temp_green    / 339;
            outputImage_data[pos * 3 + 0]   = temp_blue     / 339;
        }
    }
}

void kernel_rgb2grey(unsigned char* inputImage_data, unsigned char* outputImage_data, long rows, long cols) {  
    
    long row = 0;
    long col = 0;
    
    unsigned char redValue;
    unsigned char greenValue;
    unsigned char blueValue;
    unsigned char grayValue;
    
    for(row = 0; row < rows; row++) {
        for(col = 0; col < cols; col++) {
            long pos = row*COLS + col;
            
            blueValue =  inputImage_data[pos * 3 + 0];
            greenValue = inputImage_data[pos * 3 + 1];
            redValue =   inputImage_data[pos * 3 + 2];
            
            grayValue = (unsigned char) (0.299*redValue + 0.587*greenValue + 0.114*blueValue);
            
            outputImage_data[pos * 3 + 0] = grayValue;
            outputImage_data[pos * 3 + 1] = grayValue;
            outputImage_data[pos * 3 + 2] = grayValue;
        }
    }
}

void kernel_motion(unsigned char* originalImage_data, unsigned char* backgroundImage_data, unsigned char* outputImage_data, long rows, long cols) {
    long row, col;
    
    for(row=0; row<rows; row++) {
        for (col=0; col<cols; col++) {
            long pos = row*COLS + col;
            
            if(abs(originalImage_data[pos * 3 + 0]-backgroundImage_data[pos * 3 + 0])>MD_THRESHOLD)
                outputImage_data[pos * 3 + 0]=FG;
            else
                outputImage_data[pos * 3 + 0]=BG;
            if(abs(originalImage_data[pos * 3 + 1]-backgroundImage_data[pos * 3 + 1])>MD_THRESHOLD)
                outputImage_data[pos * 3 + 1]=FG;
            else
                outputImage_data[pos * 3 + 1]=BG;
            if(abs(originalImage_data[pos * 3 + 2]-backgroundImage_data[pos * 3 + 2])>MD_THRESHOLD)
                outputImage_data[pos * 3 + 2]=FG;
            else
                outputImage_data[pos * 3 + 2]=BG;
        }
    }  
}

void kernel_erosion(unsigned char* inputImage_data, unsigned char* outputImage_data, int rows, int cols) {
    int row, col;
    
    //   #pragma omp parallel for num_threads(nThread) private(r, c, r1, c1, marker, count) //default(shared) //shared(edgeImage, GX, GY, greyImage) //collapse(2)
    for(row=0; row<rows; row++){
        for (col=0; col<cols; col++){
            
            int marker_0 = 0;
            int marker_1 = 0;
            int marker_2 = 0;
            int count = 0;
            
            for(int r1=row-3; r1<row+3; r1++){
                for(int c1=col-3; c1<col+3; c1++){
                    
                    // Skip borders.
                    if((r1*COLS+c1) * 3 + 0 < 0 || (r1*COLS+c1) * 3 + 2 > ROWS*COLS*DEPTH) {
                        continue;
                    }
                    
                    count++;
                    marker_0+=(inputImage_data[(r1*COLS+c1) * 3 + 0]==FG)? 1:0;
                    marker_1+=(inputImage_data[(r1*COLS+c1) * 3 + 1]==FG)? 1:0;
                    marker_2+=(inputImage_data[(r1*COLS+c1) * 3 + 2]==FG)? 1:0;
                }
            }
            
            unsigned char erosionValue;
            
            if((float)marker_0/count>ER_THRESHOLD 
                || (float)marker_1/count>ER_THRESHOLD
                || (float)marker_2/count>ER_THRESHOLD
            ){
                erosionValue=FG;
            } else {
                erosionValue=BG;                
            }
            
            outputImage_data[(row*COLS+col) * 3 + 0]=erosionValue;
            outputImage_data[(row*COLS+col) * 3 + 1]=erosionValue;
            outputImage_data[(row*COLS+col) * 3 + 2]=erosionValue;
            
        }
    }  
}

void kernel_sobel(unsigned char* greyImage_data, unsigned char* edgeImage_data, int rows, int cols) {
    int c, r;
    
    for(r=0; r<rows; r++)  {
        for(c=0; c<cols; c++)  {
            long sumX = 0;
            long sumY = 0;
            
            int  i, j;
            int  SUM;
            
            // Skip borders.
            if( (r) < 1 || (c) < 1 || (r) > rows || (c) > cols  ) {
                continue;
            }
            
            sumX = sumX + greyImage_data[(c + -1 + (r + -1)*COLS)*3+0] * -1;
            sumX = sumX + greyImage_data[(c + -1 + (r + 0)*COLS)*3+0] * 0;
            sumX = sumX + greyImage_data[(c + -1 + (r + 1)*COLS)*3+0] * 1;
            sumX = sumX + greyImage_data[(c + 0 + (r + -1)*COLS)*3+0] * -2;
            sumX = sumX + greyImage_data[(c + 0 + (r + 0)*COLS)*3+0] * 0;
            sumX = sumX + greyImage_data[(c + 0 + (r + 1)*COLS)*3+0] * 2;
            sumX = sumX + greyImage_data[(c + 1 + (r + -1)*COLS)*3+0] * -1;
            sumX = sumX + greyImage_data[(c + 1 + (r + 0)*COLS)*3+0] * 0;
            sumX = sumX + greyImage_data[(c + 1 + (r + 1)*COLS)*3+0] * 1;
            
            sumY = sumY + greyImage_data[(c + -1 + (r + -1)*COLS)*3+0] * 1;
            sumY = sumY + greyImage_data[(c + -1 + (r + 0)*COLS)*3+0] * 2;
            sumY = sumY + greyImage_data[(c + -1 + (r + 1)*COLS)*3+0] * 1;
            sumY = sumY + greyImage_data[(c + 0 + (r + -1)*COLS)*3+0] * 0;
            sumY = sumY + greyImage_data[(c + 0 + (r + 0)*COLS)*3+0] * 0;
            sumY = sumY + greyImage_data[(c + 0 + (r + 1)*COLS)*3+0] * 0;
            sumY = sumY + greyImage_data[(c + 1 + (r + -1)*COLS)*3+0] * -1;
            sumY = sumY + greyImage_data[(c + 1 + (r + 0)*COLS)*3+0] * -2;
            sumY = sumY + greyImage_data[(c + 1 + (r + 1)*COLS)*3+0] * -1;
            
            SUM = sqrt(sumX*sumX+sumY*sumY);
            
            edgeImage_data[(c + r*COLS)*3+0] = /*255 -*/ (unsigned char)(SUM);
        }
    }
}

void kernel_greyedge2rgb(unsigned char* inputImage_data, unsigned char* edgeImage_data, unsigned char* outputImage_data, int rows, int cols) {
    int r;
    int c;
    
    // Iterate over image.
    for(r=0; r < rows; r++) {
        for(c=0; c < cols; c++){
            int pos = r*COLS+c;
            if(edgeImage_data[pos*3+0]>100){
                outputImage_data[pos*3+0] = 0;
                outputImage_data[pos*3+1] = 0;
                outputImage_data[pos*3+2] = 255;
            } else{
                outputImage_data[pos*3+0] = inputImage_data[pos*3+0];
                outputImage_data[pos*3+1] = inputImage_data[pos*3+1];
                outputImage_data[pos*3+2] = inputImage_data[pos*3+2];
            }
        }
    }
}

void motion_appl(unsigned char * inputImage_data, unsigned char * inputImage2_data, unsigned char * outputImage_data, int depth, long rows, long cols) {
    unsigned char *L_tmp = (unsigned char *) malloc(sizeof(unsigned char) * depth * rows * cols);
    unsigned char *M_tmp = (unsigned char *) malloc(sizeof(unsigned char) * depth * rows * cols);
    unsigned char *N_tmp = (unsigned char *) malloc(sizeof(unsigned char) * depth * rows * cols);
    
    // Apply filters on image 1.
    kernel_gauss(inputImage_data, L_tmp, rows, cols);
    kernel_rgb2grey(L_tmp, M_tmp, rows, cols);
    
    // Apply same filters on image 1.
    kernel_gauss(inputImage2_data, L_tmp, rows, cols);
    kernel_rgb2grey(L_tmp, N_tmp, rows, cols);
    
    // Compute motion difference.
    kernel_motion(M_tmp, N_tmp, L_tmp, rows, cols);
    
    // Apply erosion to reduce noise.
    kernel_erosion(L_tmp, M_tmp, rows, cols);
    
    // Apply sobel to highlight edges.
    kernel_sobel(M_tmp, N_tmp, rows, cols);
    
    // Overlay results in input image.
    kernel_greyedge2rgb(inputImage_data, N_tmp, outputImage_data, rows, cols);
    
    free(L_tmp);
    free(M_tmp);
    free(N_tmp);
}

// Check function to confirm correctness of results.
//   Will be excluded from parallelization.
void motion_appl_check2000(unsigned char * inputImage_data, unsigned char * inputImage2_data, unsigned char * outputImage_data, int depth, long rows, long cols) {
    unsigned char *L_tmp = (unsigned char *) malloc(sizeof(unsigned char) * depth * rows * cols);
    unsigned char *M_tmp = (unsigned char *) malloc(sizeof(unsigned char) * depth * rows * cols);
    unsigned char *N_tmp = (unsigned char *) malloc(sizeof(unsigned char) * depth * rows * cols);
    
    // Apply filters on image 1.
    kernel_gauss(inputImage_data, L_tmp, rows, cols);
    kernel_rgb2grey(L_tmp, M_tmp, rows, cols);
    
    // Apply same filters on image 1.
    kernel_gauss(inputImage2_data, L_tmp, rows, cols);
    kernel_rgb2grey(L_tmp, N_tmp, rows, cols);
    
    // Compute motion difference.
    kernel_motion(M_tmp, N_tmp, L_tmp, rows, cols);
    
    // Apply erosion to reduce noise.
    kernel_erosion(L_tmp, M_tmp, rows, cols);
    
    // Apply sobel to highlight edges.
    kernel_sobel(M_tmp, N_tmp, rows, cols);
    
    // Overlay results in input image.
    kernel_greyedge2rgb(inputImage_data, N_tmp, outputImage_data, rows, cols);
    
    free(L_tmp);
    free(M_tmp);
    free(N_tmp);
}

int main(int argc, char **argv) {
    
    parseCommmand(argc, argv);
    
    #if VALIDATE
    if (globalArgs.checkResults) {
        ::testing::InitGoogleTest(&argc, argv);
    }
    #endif
    
    #ifdef MEASURE_ENERGY
    MeasureEnergyInit()
    #endif
    #if MEASURE
    std::chrono::steady_clock::time_point appStartTime = std::chrono::steady_clock::now();
    #endif
    
    #if MEASURE
    std::chrono::steady_clock::time_point startTime = appStartTime;
    #endif
    
    // Image sizes.
    int rows = ROWS;
    int cols = COLS;
    
    // Filenames  
    std::string fileName0 = globalArgs.imageImage0;
    std::string fileName1 = globalArgs.imageImage1;
    std::string fileNameOut = globalArgs.outputImage;
    
    
    // File handle helper.
    FILE *bmpHandle;
    int bits = 0;
    int fileSize = 0;
    int results;
    
    unsigned int header_size = 54;
    unsigned char* inputFileImage_header = (unsigned char *)malloc(sizeof(unsigned char)*header_size);
    
    bmpHandle = fopen(fileName0.c_str(), "rb");  
    if(!bmpHandle) {
        std::cout << "File not found: " << fileName0.c_str() << std::endl;
        exit(0);  
    }
    
    results=fread(inputFileImage_header, sizeof(char)*header_size, 1, bmpHandle);  
    memcpy(&fileSize, &inputFileImage_header[2], 4);
    memcpy(&bits,&inputFileImage_header[28], 4);
    
    int depth = DEPTH;
    
    memcpy(&cols, &inputFileImage_header[18], 4);
    memcpy(&rows, &inputFileImage_header[22], 4);
    std::cout << "\n row : col : :  " << rows << ":" << cols << std::endl;
    std::cout.flush();
    
    if(bits!=24 || fileSize!=rows*cols*depth+header_size) {
        std::cout << "Wrong image format in " << fileName0.c_str() << ": accepted only 24 bit without padding!" << std::endl;
        exit(0);
    }
    
    
    #if MEASURE
    std::chrono::steady_clock::time_point mallocTime = std::chrono::steady_clock::now();
    #endif
    unsigned char *inputFileImage_data = (unsigned char *) malloc(sizeof(unsigned char) * rows * cols * depth);
    unsigned char *inputFileImage2_data = (unsigned char *) malloc(sizeof(unsigned char) * rows * cols * depth);
    unsigned char *outputImage_data = (unsigned char *) malloc(sizeof(unsigned char) * rows * cols * depth);
    
    
    #if MEASURE
    std::cout << "\nMEASURE-TIME: INAPP malloc : " << ((std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - mallocTime))).count();
    std::cout.flush();
    startTime = std::chrono::steady_clock::now();
    #endif
    
    //read the first image
    fseek(bmpHandle, header_size, SEEK_SET);
    results = fread(inputFileImage_data, sizeof(char), (rows*cols*depth), bmpHandle);
    fclose(bmpHandle);
    
    //read the second image
    bmpHandle = fopen(fileName1.c_str(), "rb");  
    if(!bmpHandle) {
        std::cout << "File not found: " << fileName0.c_str() << std::endl;
        exit(0);  
    }
    fseek(bmpHandle, header_size, SEEK_SET);
    results = fread(inputFileImage2_data, sizeof(char), (rows*cols*depth), bmpHandle);
    fclose(bmpHandle);
    
    #if MEASURE
    std::cout << "\nMEASURE-TIME: INAPP initialization : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
    std::cout.flush();
    startTime = std::chrono::steady_clock::now();
    #endif
    
    #ifdef MEASURE_ENERGY
    MeasureEnergyKernelStart()
    #endif
    
    motion_appl(inputFileImage_data, inputFileImage2_data, outputImage_data, depth, rows, cols);
    
    #ifdef MEASURE_ENERGY
    MeasureEnergyKernelStop()
    #endif
    #if MEASURE
    std::cout << "\nMEASURE-TIME: INAPP kernel : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
    std::cout.flush();
    startTime = std::chrono::steady_clock::now();
    #endif
    
    bmpHandle = fopen(fileNameOut.c_str(), "wb");  
    if(bmpHandle){
        fwrite(inputFileImage_header, sizeof(char), header_size, bmpHandle);
        fwrite(outputImage_data, sizeof(char), (rows*cols*depth), bmpHandle);
        fclose(bmpHandle);
    } else {
        std::cout << "File not opened: " << fileNameOut.c_str() << std::endl;
        exit(0);  
    }
    
    #if MEASURE
    std::cout << "\nMEASURE-TIME: INAPP saveResult : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
    std::cout.flush();
    #endif
    
    #if VALIDATE
    // Check results for correctness
    if (globalArgs.checkResults) {
        std::cout << "\nVALIDATION_CHECK: \n";
        // Alloc check buffers.
        unsigned char *outputImage_data_cpu = (unsigned char *) malloc(sizeof(unsigned char) * rows * cols * depth);
        
        // Run computation on CPU.
        motion_appl_check2000(inputFileImage_data, inputFileImage2_data, outputImage_data_cpu, depth, rows, cols);
        
        // Check differences.
        for (long row = 25; row < rows - 25; row++) {
            for (long col = 25; col < cols - 25; col++) {
                long pos = row * COLS + col;
                
                EXPECT_EQ(outputImage_data[pos],
                          outputImage_data_cpu[pos]) << "Vectors x_cpu and x_acc differ at index: [" << pos << "],  x_cpu = " << outputImage_data_cpu[pos] << " : x_acc = " << outputImage_data[pos] <<
                          "\n";
            }
        }
        free(outputImage_data_cpu);
    }
    #endif
    
    
    #if MEASURE
    startTime = std::chrono::steady_clock::now();
    #endif
    
    // Free memory.
    free(inputFileImage_data);
    free(inputFileImage2_data);
    free(outputImage_data);
    
    #if MEASURE
    std::cout << "\nMEASURE-TIME: INAPP free : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - startTime).count());
    std::cout.flush();
    printf("\nAppEnd\n");
    #endif
    
    
    #if MEASURE
    std::cout << "\nMEASURE-TIME: INAPP execution : " << (std::chrono::duration_cast < std::chrono::microseconds > (std::chrono::steady_clock::now() - appStartTime).count());
    std::cout.flush();
    #endif
    
    #ifdef MEASURE_ENERGY
    MeasureEnergyFin()
    #endif
    #if VALIDATE
    if (globalArgs.checkResults) {
        std::cout << "\n";
        return RUN_ALL_TESTS();
    }
    #endif
    
    return 0;
}
