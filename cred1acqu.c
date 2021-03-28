#include "edtinc.h"
#include "ImageStruct.h"
#include "ImageStreamIO.h"
#include "cred1struct.h"

#define STREAMNAME "cred1"

CRED1STRUCT *camconf;


static void usage(char *progname, char *errmsg);

// ============================================================================
// ============================================================================
int main(int argc, char **argv) {
    int     i;
    int     unit = 1;
    int     channel = 0;
    int     overrun, overruns=0;
    int     timeout;
    int     timeouts, last_timeouts = 0;
    int     recovering_timeout = FALSE;
    char   *progname ;
    char   *cameratype;
    int     numbufs = 4;
    int     started;
    u_char *image_p;
    PdvDev *pdv_p;
    char    errstr[64];
    int     loops = 1;
    int     width, height, depth;
    char    edt_devname[128];
    char    camname[200];
    double  value_ave;
    int     pix, xsize, ysize, kw;
    unsigned short int *imageushort;

    // =====================================
    uid_t ruid; // Real UID (= user launching process at startup)
    uid_t euid; // Effective UID (= owner of executable at startup)
    uid_t suid; // Saved UID (= owner of executable at startup)

    /*
    int RT_priority = 70; //any number from 0-99
    struct sched_param schedpar;
    int ret;

    getresuid(&ruid, &euid, &suid);
    ret = seteuid(ruid);   // normal user privileges

    schedpar.sched_priority = RT_priority;
    #ifndef __MACH__
    ret = seteuid(euid); //This goes up to maximum privileges
    sched_setscheduler(0, SCHED_FIFO, &schedpar); //other option is SCHED_RR, might be faster
    ret = seteuid(ruid);//Go back to normal privileges
    #endif
    */
    // =====================================

    progname = argv[0];
    edt_devname[0] = '\0';

    // --- process command line arguments ---

    --argc;
    ++argv;
    while (argc && ((argv[0][0] == '-') || (argv[0][0] == '/'))) {
        switch (argv[0][1]) {

        case 'N': // ------------------------------------------------------
            ++argv;
            --argc;
            if (argc < 1) {
                usage(progname, "Error: option 'N' requires a numeric argument\n");
            }
            if ((argv[0][0] >= '0') && (argv[0][0] <= '9')) {
                numbufs = atoi(argv[0]);
            }
            else {
                usage(progname, "Error: option 'N' requires a numeric argument\n");
            }
            break;

        case 'u': // ------------------------------------------------------
            ++argv;
            --argc;
            printf("I know.\n");
            break;

        case 'l': // ------------------------------------------------------
            ++argv;
            --argc;
            if (argc < 1) {
                usage(progname, "Error: option 'l' requires a numeric argument\n");
            }
            if ((argv[0][0] >= '0') && (argv[0][0] <= '9')) {
                loops = atoi(argv[0]);
                printf("Requested %d images!\n", loops);
            }
            else {
                usage(progname, "Error: option 'l' requires a numeric argument\n");
            }
            break;

        case '-': // ------------------------------------------------------
            if (strcmp(argv[0], "--help") == 0) {
                usage(progname, "");
                exit(0);
            } else {
                fprintf(stderr, "unknown option: %s\n", argv[0]);
                usage(progname, "");
                exit(1);
            }
            break;


        default: // ------------------------------------------------------
            fprintf(stderr, "unknown flag -'%c'\n", argv[0][1]);
        case '?':
        case 'h':
            usage(progname, "");
            exit(0);
        }
        argc--;
        argv++;
    }

    initCRED1STRUCT();
    printCRED1STRUCT(0);

    // ------------------------------------------------------------
    // open the interface
    // EDT_INTERFACE defined in edtdef.h (included via edtinc.h)
    // ------------------------------------------------------------
    if ((pdv_p = pdv_open_channel(EDT_INTERFACE, unit, channel)) == NULL) {
        sprintf(errstr, "pdv_open_channel(%s%d_%d)", edt_devname, unit, channel);
        pdv_perror(errstr);
        return (1);
    }
    pdv_flush_fifo(pdv_p);

    IMAGE *imarray;    // pointer to array of images
    int NBIMAGES = 1;  // can hold 1 image
    long naxis;        // number of axis
    uint8_t atype;     // data type
    uint32_t *imsize;  // image size
    int shared;        // 1 if image in shared memory
    int NBkw;          // number of keywords supported
    sprintf(camname, "cred1");
    xsize =  camconf[0].row1 - camconf[0].row0 + 1;
    ysize = (camconf[0].col1 - camconf[0].col0 + 1) * 32;
    printf("row0 & row1  : %d & %d\n",camconf[0].row0 , camconf[0].row1);
    printf("col0 & col1  : %d & %d\n",camconf[0].col0 , camconf[0].col1);

    pdv_set_width(pdv_p, ysize);
    pdv_set_height(pdv_p, xsize);

    width      = pdv_get_width(pdv_p);
    height     = pdv_get_height(pdv_p);
    depth      = pdv_get_depth(pdv_p);
    timeout    = pdv_get_timeout(pdv_p);
    cameratype = pdv_get_cameratype(pdv_p);

    printf("image size  : %d x %d\n", width, height);
    printf("Timeout     : %d\n", timeout);
    printf("Camera type : %s\n", cameratype);

    // ================================================================
    //         allocate memory for array of images
    // ================================================================
    imarray = (IMAGE*) malloc(sizeof(IMAGE)*NBIMAGES);
    naxis = 2;
    imsize = (uint32_t *) malloc(sizeof(uint32_t)*naxis);
    imsize[0] = width;
    imsize[1] = height;
    atype = _DATATYPE_UINT16;

    shared = 1; // image will be in shared memory
    NBkw = 10;  // allocate space for 10 keywords

    ImageStreamIO_createIm(&imarray[0], STREAMNAME, naxis, imsize,
                           atype, shared, NBkw);
    free(imsize);

    // ================================================================
    //        allocate memory for data cubes to be saved
    // ================================================================
    // SAVING CUBES TO DISK
    // CHANGE NBIMAGES to 3
    /*
    int SAVECUBE = 0; // change to 1 when saving -> move to shared mem for interactive control
    int CUBEindex = 0; // 0 or 1
    long frameindex = 0;
    char imnamec0[200];
    char imnamec1[200];
    uint32_t CUBEsize = 1000; // number of slices in a cube
    naxis = 3;
    imsize[0] = width;
    imsize[1] = height;
    imsize[2] = CUBEsize;
    atype = _DATATYPE_INT16;
    imsize = (uint32_t *) malloc(sizeof(uint32_t)*naxis);
    sprintf(imnamec0, "%s_cube0", camname);
    ImageStreamIO_createIm(&imarray[1], imnamec0, naxis, imsize, atype, shared, NBkw);
    sprintf(imnamec1, "%s_cube1", camname);
    ImageStreamIO_createIm(&imarray[2], imnamec1, naxis, imsize, atype, shared, NBkw);
    free(imsize);
    */
    // ================================================================
    //                      Add keywords
    // ================================================================
    kw = 0;
    strcpy(imarray[0].kw[kw].name, "tint");
    imarray[0].kw[kw].type = 'D';
    imarray[0].kw[kw].value.numf = camconf[0].tint;
    strcpy(imarray[0].kw[kw].comment, "exposure time");

    kw = 1;
    strcpy(imarray[0].kw[kw].name, "fps");
    imarray[0].kw[kw].type = 'D';
    imarray[0].kw[kw].value.numf = camconf[0].fps;
    strcpy(imarray[0].kw[kw].comment, "frame rate");

    kw = 2;
    strcpy(imarray[0].kw[kw].name, "NDR");
    imarray[0].kw[kw].type = 'L';
    imarray[0].kw[kw].value.numl = camconf[0].NDR;
    strcpy(imarray[0].kw[kw].comment, "NDR");

    kw = 3;
    strcpy(imarray[0].kw[kw].name, "row0");
    imarray[0].kw[kw].type = 'L';
    imarray[0].kw[kw].value.numl = camconf[0].row0;
    strcpy(imarray[0].kw[kw].comment, "row0 (range 1-256)");

    kw = 4;
    strcpy(imarray[0].kw[kw].name, "row1");
    imarray[0].kw[kw].type = 'L';
    imarray[0].kw[kw].value.numl = camconf[0].row1;
    strcpy(imarray[0].kw[kw].comment, "row1 (range 1-256)");

    kw = 5;
    strcpy(imarray[0].kw[kw].name, "col0");
    imarray[0].kw[kw].type = 'L';
    imarray[0].kw[kw].value.numl = camconf[0].col0;
    strcpy(imarray[0].kw[kw].comment, "col0 (range 1-10)");

    kw = 6;
    strcpy(imarray[0].kw[kw].name, "col1");
    imarray[0].kw[kw].type = 'L';
    imarray[0].kw[kw].value.numl = camconf[0].col1;
    strcpy(imarray[0].kw[kw].comment, "col1 (range 1-10)");

    kw = 7;
    strcpy(imarray[0].kw[kw].name, "temp");
    imarray[0].kw[kw].type = 'D';
    imarray[0].kw[kw].value.numf = camconf[0].temperature;
    strcpy(imarray[0].kw[kw].comment, "detector temperature");

    kw = 8;
    strcpy(imarray[0].kw[kw].name, "mode");
    imarray[0].kw[kw].type = 'S';
    strcpy(imarray[0].kw[kw].value.valstr, camconf[0].readmode);
    strcpy(imarray[0].kw[kw].comment, "readout mode");

    // other keywords to add?
    // - timestamp
    // - gain

    fflush(stdout);

    // allocate four buffers for optimal pdv ring buffer pipeline
    pdv_multibuf(pdv_p, numbufs);

    printf("reading %d image%s from '%s'\nwidth %d height %d depth %d\n",
           loops, loops == 1 ? "" : "s", cameratype, width, height, depth);

    /*
     * prestart the first image or images outside the loop to get the
     * pipeline going. Start multiple images unless force_single set in
     * config file, since some cameras (e.g. ones that need a gap between
     * images or that take a serial command to start every image) don't
     * tolerate queueing of multiple images
     */

    if (pdv_p->dd_p->force_single) {
        pdv_start_image(pdv_p);
        started = 1;
    }
    else {
        pdv_start_images(pdv_p, numbufs);
        started = numbufs;
    }
    printf("\n");
    i = 0;

    int loopOK = 1;

    while(loopOK == 1) {

        // update shared memory keywords that can be updated without restart
        imarray[0].kw[0].value.numf = camconf[0].tint;
        imarray[0].kw[1].value.numf = camconf[0].fps;
        imarray[0].kw[2].value.numl = camconf[0].NDR;
        imarray[0].kw[7].value.numf = camconf[0].temperature;

        /*
         * get the image and immediately start the next one (if not the last
         * time through the loop). Processing (saving to a file in this case)
         * can then occur in parallel with the next acquisition
         */

        image_p = pdv_wait_image(pdv_p);

        if ((overrun = (edt_reg_read(pdv_p, PDV_STAT) & PDV_OVERRUN)))
            ++overruns;

        pdv_start_image(pdv_p);
        timeouts = pdv_timeouts(pdv_p);

        /*
         * check for timeouts or data overruns -- timeouts occur when data
         * is lost, camera isn't hooked up, etc, and application programs
         * should always check for them. data overruns usually occur as a
         * result of a timeout but should be checked for separately since
         * ROI can sometimes mask timeouts
         */
        if (timeouts > last_timeouts) {
            /*
             * pdv_timeout_cleanup helps recover gracefully after a timeout,
             * particularly if multiple buffers were prestarted
             */
            pdv_timeout_restart(pdv_p, TRUE);
            last_timeouts = timeouts;
            recovering_timeout = TRUE;
            printf("\ntimeout....\n");
        }
        else if (recovering_timeout) {
            pdv_timeout_restart(pdv_p, TRUE);
            recovering_timeout = FALSE;
            printf("\nrestarted....\n");
        }
        fflush(stdout);

        imageushort = (unsigned short *) image_p;

        imarray[0].md[0].write = 1; // set this flag to 1 when writing data

        memcpy(imarray[0].array.UI16, imageushort,
               sizeof(unsigned short)*width*height);

        // ==============================================================
        fflush(stdout);

        imarray[0].md[0].write = 0;
        // POST ALL SEMAPHORES
        ImageStreamIO_sempost(&imarray[0], -1);

        imarray[0].md[0].write = 0; // Done writing data
        imarray[0].md[0].cnt0++;
        imarray[0].md[0].cnt1++;
        camconf[0].frameindex = i;
        fflush(stdout);

        i++;
        if (i==loops)
            loopOK = 0;
    }
    puts("");

    printf("%d images %d timeouts %d overruns\n", loops, last_timeouts, overruns);

    if (last_timeouts) printf("check camera and connections\n");
    pdv_close(pdv_p);

    if (overruns || timeouts) exit(2);

    //free(imageushort);
    free(imarray);

    exit(0);
}


// ============================================================================
// ============================================================================
static void usage(char *progname, char *errmsg) {
    puts(errmsg);
    printf("EDT digital imaging interface board (PCI DV, PCI DVK, etc.)\n");
    puts("");
    printf("usage: %s [-b fname] [-l loops] [-N numbufs] [-u unit] [-c channel]\n",
           progname);
    printf("  -l loops     number of loops (images to take)\n");
    printf("  -N numbufs   number of ring buffers (see users guide) (default 4)\n");
    printf("  -h           this help message\n");
    exit(1);
}
