#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "cred1struct.h"



extern CRED1STRUCT *camconf;

// ============================================================================
// ============================================================================
int printCRED1STRUCT(int cam) {
    char ifmt[20] = "%25s %11d %3s\n";
    char ffmt[20] = "%25s %11.5f %3s\n";
    char cfmt[20] = "%25s %15s\n";
    char line[80] =
        "============================================================\n";
    printf("%s", "\033[01;36m");
    printf("%s", line);
    printf(ifmt, "unit", cam, "");
    printf("%s", line);
    printf(ifmt, "FGchannel",  camconf[cam].FGchannel, "");
    printf(ffmt, "Exp. time",  camconf[cam].tint, "us");
    printf(ffmt, "Frame rate", camconf[cam].fps, "Hz");
    printf(ffmt, "Max FPS",    camconf[cam].maxfps, "Hz");
    printf(ifmt, "NDR",       camconf[cam].NDR, "");
    printf(ffmt, "Cryo temp.", camconf[cam].temperature, "K");
    printf(ifmt, "Current frame", camconf[cam].frameindex, "");
    printf("%s", line);

    //camconf[cam].temperature_setpoint);

    printf(ifmt, "cropping ON=", camconf[cam].cropmode, "");
    printf("%25s %3d-%3d\n", "cropping row", camconf[cam].row0, camconf[cam].row1);
    printf("%25s %3d-%3d\n", "cropping col", camconf[cam].col0, camconf[cam].col1);
    printf("%25s %3d x %3d\n",
           "Resulting window",
           camconf[cam].row1 - camconf[cam].row0 + 1,
           (camconf[cam].col1 - camconf[cam].col0 + 1) * 32);
    printf("%s", line);
    printf(cfmt, "readout mode", camconf[cam].readmode);
    printf("%s", line);
    printf("%s", "\033[00m");

    return (0);
}

// ============================================================================
// ============================================================================
int initCRED1STRUCT() {
    int SM_fd;        // shared memory file descriptor
    int create = 0;   // 1 if we need to re-create shared memory
    struct stat file_stat;

    SM_fd = open(camconf_name, O_RDWR);
    if(SM_fd==-1) {
        printf("Cannot import file \"%s\" -> creating file\n", camconf_name);
        create = 1;
    }
    else {
        fstat(SM_fd, &file_stat);  // read file stats
        printf("File %s size: %zd\n", camconf_name, file_stat.st_size);
        if(file_stat.st_size!=sizeof(CRED1STRUCT)*NBconf) {
            printf("File \"%s\" size is wrong -> recreating file\n", camconf_name);
            create = 1;
            close(SM_fd);
        }
    }

    if(create == 1) {
        printf("======== CREATING SHARED MEMORY FILE =======\n");
        int result;

        SM_fd = open(camconf_name, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);

        if (SM_fd == -1) {
            perror("Error opening file for writing");
            exit(0);
        }

        result = lseek(SM_fd, sizeof(CRED1STRUCT)*NBconf-1, SEEK_SET);
        if (result == -1) {
            close(SM_fd);
            perror("Error calling lseek() to 'stretch' the file");
            exit(0);
        }

        result = write(SM_fd, "", 1);
        if (result != 1) {
            close(SM_fd);
            perror("Error writing last byte of the file");
            exit(0);
        }
    }


    camconf = (CRED1STRUCT*) mmap(0, sizeof(CRED1STRUCT)*NBconf,
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED, SM_fd, 0);
    if (camconf == MAP_FAILED) {
        close(SM_fd);
        perror("Error mmapping the file");
        exit(0);
    }

    return 0;
}

