#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

#include "edtinc.h"             // for EDT PCI board
#include "cred1struct.h"        // CREDSTRUCT data structure


// tmux sessions names
#define TMUXCTRLNAME "cred1ctrl"
#define TMUXACQUNAME "cred1acqu"

// Frame grabber unit
#define FRAMEGRABBERUNIT 1

// Acquisition executable name
#define ACQUEXECNAME "./cred1acqu"




#define CAMCONFINDEX 0

#define SERBUFFSIZE 512
static char buf[SERBUFFSIZE+1];


#define STRINGMAXLEN_CLICMDSTRING 200
static char cmdstring[STRINGMAXLEN_CLICMDSTRING];



int verbose = 0;

CRED1STRUCT *camconf;
char *logfile = "logserver.log";


static EdtDev *ed;
#define STRINGMAXLEN_SERIALCMD             2048
#define STRINGMAXLEN_CAMOUTPUTBUFF         2048
static char camoutbuff[STRINGMAXLEN_CAMOUTPUTBUFF];



// camera command type
#define STRINGMAXLEN_CAMCMD_CALLSTRING           32
#define STRINGMAXLEN_CAMCMD_COMMANDSTRING       128
#define STRINGMAXLEN_CAMCMD_COMMANDSTRINGARGS   256
#define STRINGMAXLEN_CAMCMD_HELPSTRING          200

typedef struct
{
    // CLI call string
    char callstring[STRINGMAXLEN_CAMCMD_CALLSTRING];

    // command string to be issued (empty if no command)
    char commandstring[STRINGMAXLEN_CAMCMD_COMMANDSTRING];

    // CLI call string args
    char CLIargs[STRINGMAXLEN_CAMCMD_COMMANDSTRINGARGS];

    char helpstring[STRINGMAXLEN_CAMCMD_HELPSTRING];

    int (*cmdfunc)();

} CAMCMD;




/* =========================================================================
 *               CURRENT STATE
 * ========================================================================= */

// Is camera currently acquiring ?
static int camstatus_acquisition = 0;














/* =========================================================================
 *                  Displays a help menu upon request
 * ========================================================================= */
int print_help() {
    char fmt[20] = "%15s %20s %40s\n";
    char line[80] =
        "-----------------------------------------------------------------------------\n";
    printf("%s", "\033[01;34m");
    printf("%s", line);
    printf("               CRED1\n");
    printf("%s", line);
    printf(fmt, "command", "parameters", "description");
    printf("%s", line);
    printf(fmt, "status", "",        "ready, isbeingcooled, standby, ...");
    printf(fmt, "start", "",         "start the acquisition (inf. loop)");
    printf(fmt, "stop",  "",         "stop the acquisition");
    printf(fmt, "shutdown", "",      "stops the camera!");
    printf("%s", line);
    printf(fmt, "tags", "",          "turns image tagging on/off");
    printf(fmt, "gmode", "",         "get current camera readout mode");
    printf(fmt, "smode", "readmode", "set camera readout mode");
    printf(fmt, "ggain", "",         "get detector gain");
    printf(fmt, "sgain", "[gain]",   "set detector gain");
    printf(fmt, "gtint", "",         "get exposure time (u-second)");
    printf(fmt, "stint", "[tint]",   "set exposure time (u-second)");
    printf(fmt, "gtemp", "",         "get cryo temperature");
    printf(fmt, "gfpsmax", "",       "get max fps available (Hz)");
    printf(fmt, "gfps", "",          "get current fps (Hz)");
    printf(fmt, "sfps", "[fps]",     "set new fps (Hz)");
    //printf(fmt, "", "", "");
    printf("%s", line);
    printf(fmt, "help",  "", "lists commands");
    printf(fmt, "quit",  "", "exit server");
    printf(fmt, "exit",  "", "exit server");
    printf(fmt, "RAW",   "CRED1 command", "serial comm test (expert mode)");
    printf("%s", line);
    printf("%s", "\033[00m");
}

/* =========================================================================
 *                  read pdv command line response
 * ========================================================================= */
int readpdvcli(EdtDev *ed, char *outbuf) {
    int     ret = 0;
    u_char  lastbyte, waitc;
    int     length=0;

    outbuf[0] = '\0';
    do {
        ret = pdv_serial_read(ed, buf, STRINGMAXLEN_CAMOUTPUTBUFF);
        if (verbose)
            printf("read returned %d\n", ret);

        if (*buf)
            lastbyte = (u_char)buf[strlen(buf)-1];

        if (ret != 0) {
            buf[ret + 1] = 0;
            strcat(outbuf, buf);
            length += ret;
        }

        if (ed->devid == PDVFOI_ID)
            ret = pdv_serial_wait(ed, 500, 0);
        else if (pdv_get_waitchar(ed, &waitc) && (lastbyte == waitc))
            ret = 0; /* jump out if waitchar is enabled/received */
        else ret = pdv_serial_wait(ed, 500, 64);
    } while (ret > 0);
}

/* =========================================================================
 *                        generic server command
 * ========================================================================= */
int server_command(EdtDev *ed, const char *cmd) {
    char tmpbuf[STRINGMAXLEN_CAMOUTPUTBUFF];

    readpdvcli(ed, camoutbuff); // flush
    sprintf(tmpbuf, "%s\r", cmd);
    pdv_serial_command(ed, tmpbuf);
    if (verbose)
        printf("command: %s", tmpbuf);
    return 0;
}

/* =========================================================================
 *                    generic server query (expects float)
 * ========================================================================= */
float server_query_float(EdtDev *ed, const char *cmd) {
    float fval;

    server_command(ed, cmd);
    usleep(100000);
    readpdvcli(ed, camoutbuff);
    sscanf(camoutbuff, "%f", &fval);

    return fval;
}

/* =========================================================================
 *                 log server interaction in a file
 * ========================================================================= */

void add_log_entry(const char* format, ...)
{
    FILE *fd = fopen(logfile, "a");
    struct tm *tm;
    time_t t0 = time(0);

    tm = localtime(&t0);
    //sprintf(timestamp, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    char msg[1000];
    va_list argptr;
    va_start(argptr, format);
    vsprintf(msg, format, argptr);
    va_end(argptr);

    FILE *fp = fopen(logfile, "a");
    fprintf(fp, "%02d:%02d:%02d  %s\n", tm->tm_hour, tm->tm_min, tm->tm_sec, msg);
    fclose(fp);
}


/* =========================================================================
 *       send command to the acquisition process (shorthand for more concise code)
 * ========================================================================= */
int sendacqucmd(char *msg) {
    char cmd[256];
    int ret;

    sprintf(cmd, "tmux send-keys -t %s \"%s\" \"C-m\"", TMUXACQUNAME, msg);
    ret = system(cmd);

    return ret;
}



/* ==========================================================================
          Start Acquisition
============================================================================= */

int start_acquisition(int nbframes)
{
    char cmd[256];

    snprintf(cmd, 256, "tmux send-keys -t %s \"%s -u %d -l %d\" \"C-m\"",
             TMUXACQUNAME,
             ACQUEXECNAME,
             FRAMEGRABBERUNIT,
             nbframes);
    printf("cmd: %s\n", cmd);
    int ret = system(cmd);

    return ret;
}


















/* =========================================================================
 *               CLI CAMERA COMMANDS
 * ========================================================================= */



int nullcommand()
{
    return 0;
}



// Start camera acquisition
int camcmd_start_acquisition()
{
    if (camstatus_acquisition == 0)
    {
        start_acquisition(0);
        add_log_entry("start image acquisition");
        camstatus_acquisition = 1;
    }
    else
    {
        printf("IGNORING COMMAND: camera already acquiring\n");
    }

    return 0;
}


// Start camera acquisition, N frames
int camcmd_take()
{
    if (camstatus_acquisition == 0)
    {
        int nbim = 5;
        char str0[200];
        sscanf(cmdstring, "%s %d", str0, &nbim);
        start_acquisition(nbim);
        add_log_entry("take %d images", nbim);
    }
    else
    {
        printf("IGNORING COMMAND: camera already acquiring\n");
    }

    return 0;
}



// Stop camera acquisition
int camcmd_stop_acquisition()
{
    if (camstatus_acquisition == 1)
    {
        sendacqucmd("C-c");
        add_log_entry("stop image acquisition");
        camstatus_acquisition = 0;
    }
    else
    {
        printf("IGNORING COMMAND: camera acquisition already stopped\n");
    }

    return 0;
}






// Set camera crop rows
int camcmd_set_restart_acquisition()
{
    if (camstatus_acquisition == 1)
    {
        sendacqucmd("C-c");
        add_log_entry("restarting acquisition");
        start_acquisition(0);
    }
}




// Get camera crop rows
int camcmd_get_crop_rows()
{
    int row0, row1;
    sscanf(camoutbuff, "rows: %d-%d", &row0, &row1);
    camconf[CAMCONFINDEX].row0 = row0;
    camconf[CAMCONFINDEX].row1 = row1;
    printf("cropmode: \033[01;31mROWS: %d-%d\033[00m\n", row0, row1);
}


// Get camera crop cols
int camcmd_get_crop_cols()
{
    int col0, col1;
    sscanf(camoutbuff, "columns: %d-%d", &col0, &col1);
    camconf[CAMCONFINDEX].col0 = col0;
    camconf[CAMCONFINDEX].col1 = col1;
    printf("cropmode: \033[01;31mCOLS: %d-%d\033[00m\n", col0, col1);
}


// Set camera crop rows
int camcmd_set_crop_rows()
{
    int row0, row1;

    char str0[200];
    sscanf(cmdstring, "%s %d %d", str0, &row0, &row1);

    // verify settings
    int cmdinputOK = 0;
    if ((0 <= row0) && (row1 <= 256) && (0 <= row1) &&
            (row1 <= 256) && (row0 <= row1))
    {
        cmdinputOK = 1;
    }

    if(cmdinputOK==1)
    {
        char serialcmd[STRINGMAXLEN_SERIALCMD];

        camconf[CAMCONFINDEX].row0 = row0;
        camconf[CAMCONFINDEX].row1 = row1;
        printf("cropmode: \033[01;31mROWS: %d-%d\033[00m\n", row0, row1);
        sprintf(serialcmd, "set cropping rows %d-%d", row0, row1);
        server_command(ed, serialcmd);
        add_log_entry("set crop rows %d %d", row0, row1);

        if(camstatus_acquisition == 1)
        {   // restart acquisition
            sendacqucmd("C-c");
            start_acquisition(0);
        }
    }
    else
    {
        printf("Not a valid combination.\n");
    }
}




// Set camera crop cols
int camcmd_set_crop_cols()
{
    int col0, col1;

    char str0[200];
    sscanf(cmdstring, "%s %d %d", str0, &col0, &col1);

    // verify settings
    int cmdinputOK = 0;
    if ((0 <= col0) && (col0 <= 10) && (0 <= col1) &&
            (col1 <= 10) && (col0 <= col1))
    {
        cmdinputOK = 1;
    }

    if(cmdinputOK==1)
    {
        char serialcmd[STRINGMAXLEN_SERIALCMD];

        camconf[CAMCONFINDEX].col0 = col0;
        camconf[CAMCONFINDEX].col1 = col1;
        printf("cropmode: \033[01;31mCOLS: %d-%d\033[00m\n", col0, col1);
        sprintf(serialcmd, "set cropping columns %d-%d", col0, col1);
        server_command(ed, serialcmd);
        add_log_entry("set crop cols %d %d", col0, col1);

        if(camstatus_acquisition == 1)
        {   // restart acquisition
            sendacqucmd("C-c");
            start_acquisition(0);
        }
    }
    else
    {
        printf("Not a valid combination.\n");
    }
}




int camcmd_set_readout_mode_globalresetsingle()
{
    sprintf(camconf[CAMCONFINDEX].readmode, "global_sng");
    if(camstatus_acquisition == 1)
    {   // restart acquisition
        sendacqucmd("C-c");
        start_acquisition(0);
    }
    add_log_entry("readout mode globalresetsingle");

    return 0;
}

int camcmd_set_readout_mode_globalresetcds()
{
    sprintf(camconf[CAMCONFINDEX].readmode, "global_cds");
    if(camstatus_acquisition == 1)
    {   // restart acquisition
        sendacqucmd("C-c");
        start_acquisition(0);
    }
    add_log_entry("readout mode globalresetcds");

    return 0;
}


int camcmd_set_readout_mode_globalresetbursts()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];

    sprintf(camconf[CAMCONFINDEX].readmode, "global_nro");

    sprintf(serialcmd, "set imagetags on");
    server_command(ed, serialcmd);
    if(camstatus_acquisition == 1)
    {   // restart acquisition
        sendacqucmd("C-c");
        start_acquisition(0);
    }
    add_log_entry("readout mode globalresetbursts");

    return 0;
}


int camcmd_set_readout_mode_rollingresetsingle()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];

    sprintf(camconf[CAMCONFINDEX].readmode, "rolling_sng");

    if(camstatus_acquisition == 1)
    {   // restart acquisition
        sendacqucmd("C-c");
        start_acquisition(0);
    }
    add_log_entry("readout mode rollingresetsingle");

    return 0;
}


int camcmd_set_readout_mode_rollingresetcds()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];

    sprintf(camconf[CAMCONFINDEX].readmode, "rolling_cds");

    if(camstatus_acquisition == 1)
    {   // restart acquisition
        sendacqucmd("C-c");
        start_acquisition(0);
    }
    add_log_entry("readout mode rollingresetcds");

    return 0;
}


int camcmd_set_readout_mode_rollingresetnro()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];

    sprintf(camconf[CAMCONFINDEX].readmode, "rolling_nro");

    sprintf(serialcmd, "set imagetags on");
    server_command(ed, serialcmd);
    if(camstatus_acquisition == 1)
    {   // restart acquisition
        sendacqucmd("C-c");
        start_acquisition(0);
    }
    add_log_entry("readout mode rollingresetnro");

    return 0;
}


int camcmd_get_pressure()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];

    sprintf(serialcmd, "pressure raw");
    float fval = server_query_float(ed, serialcmd);
    add_log_entry("pressure: %f millibar", fval);
    printf("pressure: \033[01;31m%f\033[00m millibar\n", fval);

    return 0;
}



int camcmd_get_gain()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];

    sprintf(serialcmd, "gain raw");
    camconf[CAMCONFINDEX].gain = server_query_float(ed, serialcmd);
    printf("gain: \033[01;31m%f\033[00m\n", camconf[CAMCONFINDEX].gain);

    return 0;
}


int camcmd_set_gain()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];
    char str0[200];
    float gainval;

    sscanf(cmdstring, "%s %f", str0, &gainval);
    sprintf(serialcmd, "set gain %f", gainval);
    server_command(ed, serialcmd);
    camconf[CAMCONFINDEX].gain = gainval;
    add_log_entry("set gain %f", gainval);
}


int camcmd_get_fps()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];

    sprintf(serialcmd, "fps raw");
    camconf[CAMCONFINDEX].fps = server_query_float(ed, serialcmd);
    camconf[CAMCONFINDEX].tint = 1000.0/camconf[CAMCONFINDEX].fps;
    printf("fps: \033[01;31m%f\033[00m Hz\n", camconf[CAMCONFINDEX].fps);

    return 0;
}


int camcmd_get_maxfps()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];

    sprintf(serialcmd, "maxfps raw");
    camconf[CAMCONFINDEX].maxfps = server_query_float(ed, serialcmd);
    printf("maxfps: \033[01;31m%f\033[00m Hz\n", camconf[CAMCONFINDEX].maxfps);

    return 0;
}


int camcmd_set_fps()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];
    char str0[200];
    float fpsval;

    sscanf(cmdstring, "%s %f", str0, &fpsval);
    sprintf(serialcmd, "set fps %f", fpsval);
    server_command(ed, serialcmd);
    camconf[CAMCONFINDEX].fps = fpsval;
    camconf[CAMCONFINDEX].tint = 1000.0/camconf[CAMCONFINDEX].fps;
    add_log_entry("set fps %f Hz", fpsval);
}




int camcmd_get_tint()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];

    sprintf(serialcmd, "fps raw");
    camconf[CAMCONFINDEX].fps = server_query_float(ed, serialcmd);
    camconf[CAMCONFINDEX].tint = 1000.0/camconf[CAMCONFINDEX].fps;
    printf("tint: \033[01;31m%f\033[00m ms\n", camconf[CAMCONFINDEX].tint);

    return 0;
}

int camcmd_set_tint()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];
    char str0[200];
    float fpsval;
    float tintval;

    sscanf(cmdstring, "%s %f", str0, &tintval);
    fpsval = 1000.0/tintval;
    sprintf(serialcmd, "set fps %f", fpsval);
    server_command(ed, serialcmd);
    camconf[CAMCONFINDEX].fps = fpsval;
    camconf[CAMCONFINDEX].tint = 1000.0/camconf[CAMCONFINDEX].fps;
    add_log_entry("set fps %f Hz", fpsval);
}





int camcmd_get_temp()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];

    sprintf(serialcmd, "temperatures cryostat diode raw");
    camconf[CAMCONFINDEX].temperature = server_query_float(ed, serialcmd);
    printf("temp: \033[01;31m%f\033[00m K\n", camconf[CAMCONFINDEX].temperature);

    return 0;
}


int camcmd_get_NDR()
{
    char serialcmd[STRINGMAXLEN_SERIALCMD];

    sprintf(serialcmd, "nbreadworeset raw");
    camconf[CAMCONFINDEX].NDR = server_query_float(ed, serialcmd);
    printf("NDR: \033[01;31m%d\033[00m\n", camconf[CAMCONFINDEX].NDR);

    return 0;
}




static CAMCMD camcommand[] =
{
    {
        "status", "status raw",
        "",
        "ready, isbeingcooled, standby, ...",
        &nullcommand
    },
    {
        "start", "",
        "",
        "start camera acquisition",
        &camcmd_start_acquisition
    },
    {
        "take", "",
        "NBimage[int]",
        "take N images",
        &camcmd_take
    },
    {
        "stop", "",
        "",
        "stop camera acquisition",
        &camcmd_stop_acquisition
    },
    {
        "gcropr", "cropping rows",
        "",
        "Get crop rows",
        &camcmd_get_crop_rows
    },
    {
        "gcropc", "cropping columns",
        "",
        "Get crop cols",
        &camcmd_get_crop_cols
    },
    {
        "scropON", "set cropping on",
        "",
        "Set crop ON",
        &camcmd_set_restart_acquisition
    },
    {
        "scropOFF", "set cropping off",
        "",
        "Set crop OFF",
        &camcmd_set_restart_acquisition
    },
    {
        "scropr", "",
        "x0[int] x1[int]",
        "Set crop rows",
        &camcmd_set_crop_rows
    },
    {
        "scropc", "",
        "c0[int] c1[int]",
        "Set crop cols",
        &camcmd_set_crop_cols
    },
    {
        "tagsON", "set imagetags on",
        "",
        "Set image tags ON",
        &nullcommand
    },
    {   // set image tags off
        "tagsOFF", "set imagetags off",
        "",
        "Set image tags OFF",
        &nullcommand
    },
    {   // get readout mode
        "gmode", "mode raw",
        "",
        "get readout mode",
        &nullcommand
    },
    {   // set readout mode to globalresetsingle
        "smode_globalresetsingle", "set mode globalresetsingle",
        "",
        "set readout mode to globalresetsingle",
        &camcmd_set_readout_mode_globalresetsingle
    },
    {   // set readout mode to globalresetcds
        "smode_globalresetcds", "set mode globalresetcds",
        "",
        "set readout mode to globalresetcds",
        &camcmd_set_readout_mode_globalresetcds
    },
    {   // set readout mode to globalresetbursts
        "smode_globalresetbursts", "set mode globalresetbursts",
        "",
        "set readout mode to globalresetbursts",
        &camcmd_set_readout_mode_globalresetbursts
    },
    {   // set readout mode to rollingresetsingle
        "smode_rollingresetsingle", "set mode rollingresetsingle",
        "",
        "set readout mode to rollingresetsingle",
        &camcmd_set_readout_mode_rollingresetsingle
    },
    {   // set readout mode to rollingresetcds
        "smode_rollingresetcds", "set mode rollingresetcds",
        "",
        "set readout mode to rollingresetcds",
        &camcmd_set_readout_mode_rollingresetcds
    },
    {   // set readout mode to rollingresetnro
        "smode_rollingresetnro", "set mode rollingresetnro",
        "",
        "set readout mode to rollingresetnro",
        &camcmd_set_readout_mode_rollingresetnro
    },
    {   // get pressure
        "gpressure", "",
        "",
        "get cryostat pressure",
        &camcmd_get_pressure
    },
    {   // get gain
        "ggain", "",
        "",
        "get detector gain",
        &camcmd_get_gain
    },
    {   // set gain
        "sgain", "",
        "gain[float]",
        "set detector gain",
        &camcmd_set_gain
    },
    {   // get maxfps
        "gmaxfps", "",
        "",
        "get detector maxfps",
        &camcmd_get_maxfps
    },
    {   // get fps
        "gfps", "",
        "",
        "get detector fps",
        &camcmd_get_fps
    },
    {   // set fps
        "sfps", "",
        "fps[Hz]",
        "set detector fps",
        &camcmd_set_fps
    },
    {   // get tint
        "gtint", "",
        "",
        "get detector tint [ms]",
        &camcmd_get_tint
    },
    {   // set tint
        "stint", "",
        "tint[ms]",
        "set detector tint",
        &camcmd_set_tint
    },
    {   // get temperature
        "gtemp", "",
        "",
        "get cyostat diode temp",
        &camcmd_get_temp
    },
    {   // get NDR
        "gNDR", "",
        "",
        "get number of read without reset",
        &camcmd_get_NDR
    }
};









/* =========================================================================
 *                            Main program
 * ========================================================================= */
int main() {
    char prompt[200];
    char serialcmd[500];
    char loginfo[2000];

    char str0[20];
    char str1[20];
    char *copy;
    char *token;

    int cmdOK = 0;
    //int unit = FRAMEGRABBERUNIT;
    int baud = 115200;
    int timeout = 0;
    //char outbuf[2000];
    float fval = 0.0; // float value return
    float ival = 0; // integer value return
    int acq_is_on = 0;
    int acq_was_on = 0;
    int tagging = 0;
    int row0, row1, col0, col1; //

    printf("%s", "\033[01;32m");
    printf("CRED1 camera server\n");
    printf("%s", "\033[00m");

    // --------------- initialize data structures -----------------
    initCRED1STRUCT();
    camconf[CAMCONFINDEX].row0 = 1;
    camconf[CAMCONFINDEX].row1 = 256;
    camconf[CAMCONFINDEX].col0 = 1;
    camconf[CAMCONFINDEX].col1 = 10;

    printCRED1STRUCT(0);

    // -------------- open a handle to the device -----------------
    ed = pdv_open_channel(EDT_INTERFACE, FRAMEGRABBERUNIT, CAMCONFINDEX);
    if (ed == NULL) {
        pdv_perror(EDT_INTERFACE);
        return -1;
    }
    printf("device name is: %s\n", ed->edt_devname);
    pdv_set_baud(ed, baud);
    printf("serial timeout: %d\n", ed->dd_p->serial_timeout);

    // ---------------- command line interpreter ------------------
    sprintf(prompt, "\033[01;33m%s>\033[00m", "cred1");



    int NBcamcmd = sizeof(camcommand)/sizeof(CAMCMD);
    printf("   %d commands\n", NBcamcmd);

    for (;;) {
        cmdOK = 0;
        printf("%s ", prompt);
        fgets(cmdstring, STRINGMAXLEN_CLICMDSTRING, stdin);



        if (cmdOK == 0)
        {
            if (strncmp(cmdstring, "help", strlen("help")) == 0)
            {
                // Print help

                char fmt[20] = "%15s %20s %40s\n";
                char spacerline[80] = "-----------------------------------------------------------------------------\n";
                printf("%s", "\033[01;34m");
                printf("%s", spacerline);
                printf("               CRED1\n");
                printf("%s", spacerline);
                printf(fmt, "command", "parameters", "description");
                printf("%s", spacerline);
                for(int cmdi=0; cmdi<NBcamcmd; cmdi++)
                {
                    printf(fmt, camcommand[cmdi].callstring, camcommand[cmdi].CLIargs, camcommand[cmdi].helpstring);
                }
                printf("%s", spacerline);
                cmdOK = 1;
            }
        }


        for(int cmdi=0; cmdi<NBcamcmd; cmdi++)
        {
            if(cmdOK==0)
            {
                // look for and execute command

                if(strncmp(cmdstring, camcommand[cmdi].callstring, strlen(camcommand[cmdi].callstring)) == 0)
                {
                    if(strlen(camcommand[cmdi].commandstring)>0)
                    {
                        printf("Issuing command : \"%s\"\n", camcommand[cmdi].commandstring);
                        sprintf(serialcmd, "%s", camcommand[cmdi].commandstring);
                        server_command(ed, serialcmd);
                        readpdvcli(ed, camoutbuff);
                        //sscanf(camoutbuff, "%s", str0);
                        printf("   \033[01;31m%s\033[00m\n", camoutbuff);
                    }
                    if(&camcommand[cmdi].cmdfunc != NULL)
                    {   // call custom function
                        camcommand[cmdi].cmdfunc();
                    }
                    cmdOK = 1;
                }
            }
        }








        // ------------------------------------------------------------------------
        //                         CONVENIENCE TOOLS
        // ------------------------------------------------------------------------


        if (cmdOK == 0)
            if (strncmp(cmdstring, "RAW", strlen("RAW")) == 0) {
                copy = strdup(cmdstring);
                token = strsep(&copy, " ");
                server_command(ed, copy);
                readpdvcli(ed, camoutbuff);
                printf("camoutbuff: \033[01;31m%s\033[00m\n", camoutbuff);
                //fprintf(logfile, "%s\n", outbuf);
                cmdOK = 1;
            }

        if (cmdOK == 0)
            if (strncmp(cmdstring, "readconf", strlen("readconf")) == 0) {
                printCRED1STRUCT(0);
                cmdOK = 1;
            }

        if (cmdOK == 0)
            if (strncmp(cmdstring, "_help", strlen("_help")) == 0) {
                print_help();
                cmdOK = 1;
            }

        if (cmdOK == 0)
            if (strncmp(cmdstring, "quit", strlen("quit")) == 0) {
                pdv_close(ed);
                printf("Bye!\n");
                exit(0);
            }

        if (cmdOK == 0)
            if (strncmp(cmdstring, "exit", strlen("exit")) == 0) {
                pdv_close(ed);
                printf("Bye!\n");
                exit(0);
            }

        if (cmdOK == 0) {
            printf("Unkown command: %s\n", cmdstring);
            print_help();
        }
    }

    exit(0);
}

