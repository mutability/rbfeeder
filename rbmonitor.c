/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   rbmonitor.c
 * Author: jonis
 *
 * Created on May 14, 2017, 10:16 AM
 */
#include <stdlib.h>

#include "rbmonitor.h"
/*
 * 
 */

/*
 * Function to create daemon
 */
int main(int argc, char **argv) {

    NOTUSED(argc);
    NOTUSED(argv);


    char pidfile[] = "/var/run/rbmonitor.pid";
    // Check if pib file exists
    if (access(pidfile, F_OK) != -1) {
        printf("PID file exists, exiting.\n");
        exit(EXIT_FAILURE);
    }




    pid_t pid;

    /* Fork off the parent process */
    pid = fork();

    /* An error occurred */
    if (pid < 0) {
        printf("Error 1\n");
        exit(EXIT_FAILURE);
    }

    /* Success: Let the parent terminate */
    if (pid > 0) {
        ;
        ;
        //printf("Terminando parent....\n");
        exit(EXIT_SUCCESS);
    }

    /* On success: The child process becomes session leader */
    if (setsid() < 0) {
        printf("Error 3\n");
        exit(EXIT_FAILURE);
    }

    /* Catch, ignore and handle signals */
    //TODO: Implement a working signal handler */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* Fork off for the second time*/
    pid = fork();

    /* An error occurred */
    if (pid < 0) {
        printf("Error 4\n");
        exit(EXIT_FAILURE);
    }

    /* Success: Let the parent terminate */
    if (pid > 0) {
        //printf("Terminando parent 2....\n");
        exit(EXIT_SUCCESS);
    }

    /* Set new file permissions */
    umask(0);

    /* Change the working directory to the root directory */
    /* or another appropriated directory */
    //chdir("/radarbox/fwclient/");

    /* Close all open file descriptors */
    //int x;
    //for (x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
    //    close(x);
    //}

    //daemon_mode = 1;

    rbmonitor_main();
    /* Open the log file */
    //openlog("firstdaemon", LOG_PID, LOG_DAEMON);
}

int rbmonitor_main() {

    //NOTUSED(argc);
    //NOTUSED(argv);               
    pabort = 0;

    signal(SIGINT, sigintHandler);
    signal(SIGTERM, sigintHandler);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, sigintHandler);

    createPidFile();

    int x;
    for (x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
        close(x);
    }


    openlog("rbmonitor", LOG_PID, LOG_DAEMON);


    while (!pabort) {

        if (access("/radarbox/client/rbfeeder.nostart", F_OK) == -1) { // File doesn't exists
            if (!checkRbFeederRunning()) {
                run_cmd2("/radarbox/client/rbfeeder");
                sleep(5);
            }
        }
        
        sleep(10);
    }

    syslog(LOG_NOTICE, "Successfull exit!");
    char pidfile[] = "/var/run/rbmonitor.pid";
    remove(pidfile);
    remove("/data/status.json");

    exit(EXIT_SUCCESS);
}

/*
 * Check if vhf is running
 */
int checkRbFeederRunning(void) {

    FILE *f = fopen("/var/run/rbfeeder.pid", "r");
    if (f == NULL) {
        return 0;
    } else {
        char tmp[20];
        memset(&tmp, 0, 20);
        fgets(tmp, 20, (FILE*) f);
        fclose(f);

        if (strlen(tmp) > 2) {
            char* endptr;
            endptr = "0123456789";
            p_feeder = strtoimax(tmp, &endptr, 10);
            if (kill(p_feeder, 0) == 0) {
                return 1;
            } else {
                p_feeder = 0;
                remove("/var/run/rbfeeder.pid");
                return 0;
            }

        } else {
            return 0;
        }

    }
}

/*
 * Check if vhf is running
 */
int checkVhfRunning(void) {

    FILE *f = fopen("/var/run/rtl_airband.pid", "r");
    pid_t p_vhf;
    if (f == NULL) {
        return 0;
    } else {
        char tmp[20];
        memset(&tmp, 0, 20);
        fgets(tmp, 20, (FILE*) f);
        fclose(f);

        if (strlen(tmp) > 2) {
            char* endptr;
            endptr = "0123456789";
            p_vhf = strtoimax(tmp, &endptr, 10);
            if (kill(p_vhf, 0) == 0) {
                return 1;
            } else {
                p_vhf = 0;
                remove("/var/run/rtl_airband.pid");
                return 0;
            }

        } else {
            return 0;
        }

    }
}

/*
 * Check if vhf is running
 */
int checkMLATRunning(void) {

    FILE *f = fopen("/var/run/mlat-client.pid", "r");
    pid_t p_mlat = 0;
    if (f == NULL) {
        return 0;
    } else {
        char tmp[20];
        memset(&tmp, 0, 20);
        fgets(tmp, 20, (FILE*) f);
        fclose(f);

        if (strlen(tmp) > 2) {
            char* endptr;
            endptr = "0123456789";
            p_mlat = strtoimax(tmp, &endptr, 10);
            if (kill(p_mlat, 0) == 0) {
                return 1;
            } else {
                p_mlat = 0;
                remove("/var/run/mlat-client.pid");
                return 0;
            }

        } else {
            return 0;
        }

    }
}

static void sigintHandler(int dummy) {
    NOTUSED(dummy);
    signal(SIGINT, SIG_DFL); // reset signal handler - bit extra safety
    signal(SIGTERM, SIG_DFL); // reset signal handler - bit extra safety
    pabort = 1;
    printf("Aborting....\n");
}

void run_cmd2(char *cmd) {
    pid_t pid;
    char *argv[] = {"sh", "-c", cmd, NULL};
    int status;

    status = posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, environ);

    if (status == 0) {
        syslog(LOG_NOTICE, "RBFeeder started successfull");
    } else {
        syslog(LOG_ERR, "Error sarting RBFeeder proccess: %s", strerror(status));
        //airnav_log_level(2, "posix_spawn: %s\n", strerror(status));
    }

}

/*
 * Function to create PID file
 */
void createPidFile(void) {
    char pidfile[] = "/var/run/rbmonitor.pid";

    FILE *f = fopen(pidfile, "w");
    if (f == NULL) {
        exit(EXIT_FAILURE);
    } else {
        fprintf(f, "%ld\n", (long) getpid());
        fclose(f);
    }

    return;
}

/*
 * Function to create status file in JSON format
 */
void createStatsFile() {
    char pidfile[] = "/data/status.json";

    FILE *f = fopen(pidfile, "w");

    if (f == NULL) {
        return;
    } else {
        int vr = checkVhfRunning();
        int mr = checkMLATRunning();
        int fr = checkRbFeederRunning();

        fprintf(f, "{\"rbfeeder\":%d,\"mlat\":%d,\"vhf\":%d}", fr, mr, vr);
        fclose(f);
    }

}
