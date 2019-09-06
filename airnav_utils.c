/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>
#include <stdarg.h>
#include "dump1090.h"
#include "airnav.h"

void airnav_log(const char* format, ...) {
    char timebuf[128];
    char timebuf2[128];
    char msg[1024];
    char msg2[1024];
    time_t now;
    struct tm local;
    va_list ap, ap2;
    now = time(NULL);
    localtime_r(&now, &local);
    strftime(timebuf, 128, "\x1B[35m[\x1B[33m%F %T\x1B[35m]\x1B[0m", &local);
    strftime(timebuf2, 128, "[%F %T]", &local);
    timebuf[127] = 0;
    timebuf2[127] = 0;
    va_start(ap, format);
    va_start(ap2, format);
    vsnprintf(msg, 1024, format, ap);
    vsnprintf(msg2, 1024, format, ap2);
    va_end(ap);
    va_end(ap2);
    msg[1023] = 0;
    msg2[1023] = 0;
    if (daemon_mode == 0) {
        fprintf(stderr, "%s  %s", timebuf, msg);
    } else {
        syslog(LOG_NOTICE, "%s  %s", timebuf2, msg);
    }


    if (log_file != NULL) {
        FILE * fp;
        fp = fopen(log_file, "a");
        if (fp == NULL) {
            printf("Can't create log file %s\n", log_file);
            return;
        }
        fprintf(fp, "%s  %s", timebuf2, msg2);
        fclose(fp);
    }


}

void airnav_log_level_m(const char* fname, const int level, const char* format, ...) {
    MODES_NOTUSED(level);
    MODES_NOTUSED(format);

    if (debug_level >= level) {
        char timebuf[128];
        char timebuf2[128];
        char msg[1024];
        char msg2[1024];
        time_t now;
        struct tm local;
        va_list ap, ap2;
        now = time(NULL);
        localtime_r(&now, &local);
        strftime(timebuf, 128, "\x1B[35m[\x1B[33m%F %T\x1B[35m]\x1B[0m", &local);
        strftime(timebuf2, 128, "[%F %T]", &local);
        timebuf[127] = 0;
        timebuf2[127] = 0;
        va_start(ap, format);
        va_start(ap2, format);
        vsnprintf(msg, 1024, format, ap);
        vsnprintf(msg2, 1024, format, ap2);
        va_end(ap);
        va_end(ap2);
        msg[1023] = 0;
        msg2[1023] = 0;
        if (daemon_mode == 0) {
            fprintf(stdout, "%s \x1B[35m[\x1B[31m%s\x1B[35m]\x1B[0m  %s", timebuf, fname, msg);
        } else {
            syslog(LOG_NOTICE, "%s [%s] %s", timebuf2, fname, msg);
        }


        if (log_file != NULL) {
            FILE * fp;
            fp = fopen(log_file, "a");
            if (fp == NULL) {
                printf("Can't create log file %s\n", log_file);
                return;
            }
            fprintf(fp, "%s [%s] %s", timebuf2, fname, msg2);
            fclose(fp);
        }

    }


}

