#ifndef _CRED1STRUCT_H
#define _CRED1STRUCT_H
#define camconf_name "/tmp/cred1conf.shm"

#define NBconf  2

typedef struct {
    int    FGchannel;         // frame grabber channel
    float  tint;              // integration time for each read
    int    NDR;               // number of reads per reset
    char   readmode[16];      // readout mode
    float  temperature;       // current cryostat temperature
    float  maxfps;            // maximum frame rate
    float  fps;               // current number of frames in Hz
    float  gain;              //

    // cropping parameters
    int cropmode;            // 0: OFF, 1: ON
    int row0; // range 1 - 256 (granularity = 1)
    int row1; // range 1 - 256 (granularity = 1)
    int col0; // range 1 -  10 (granularity = 32)
    int col1; // range 1 -  10 (granularity = 32)

    int sensibility;
    // 0: low
    // 1: medium
    // 2: high

    long frameindex;

} CRED1STRUCT;


int printCRED1STRUCT(int cam);
int initCRED1STRUCT();

#endif
