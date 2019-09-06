/* 
 * File:   rbmonitor.h
 *
 * Created on May 14, 2017, 10:24 AM
 */

#ifndef RBMONITOR_H
#define RBMONITOR_H

#ifdef __cplusplus
extern "C" {
#endif


#define NOTUSED(V) ((void) V)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>    
#include <inttypes.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>    
#include <spawn.h>
    char pabort;
    extern char **environ;


    pid_t p_feeder;

    static void sigintHandler(int dummy);
    int checkRbFeederRunning(void);
    void run_cmd2(char *cmd);
    int rbmonitor_main();
    void createPidFile(void);
    void createStatsFile();
    int checkMLATRunning(void);
    int checkVhfRunning(void);

#ifdef __cplusplus
}
#endif

#endif /* RBMONITOR_H */

