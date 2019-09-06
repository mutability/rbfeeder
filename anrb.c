/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "airnav.h"
#include "dump1090.h"
#define MAX_COMMAND_SOCK 50
int command_server_conn[50];
struct in_addr command_server_ip[50];
char command_server_url[512];
int command_server_sock, command_server_status;
time_t command_lastping;
FILE *debugfp, *commanddebugfp;
int command_socket;
int airnav_serial,airnav_locked_device;
char avhversionstring[64], command_url[512];
int command_status; // bit 0: socket opened, bit 1: authenticated, bit 2: socket operative, bit 3: adsb streaming enabled, bit 4: voice streaming enabled, bit 5: autoping, bit 6: gps streaming
int verboselevel;
int usb_socket;

int command_server_connect(char *url) {
    int ret, err, i;
    int sd;
    struct sockaddr_in sa;
    struct hostent *hs;
    //	struct hostent hs1;
    char host[512], *p, rhost[512];
    char message[1024];
    int port;
    WORD32 ip;
    static int lastmes;
    MODES_NOTUSED(url);

    /*
    strcpy(rhost, url);
    if (p = strchr(rhost, ':')) {
        strcpy(host, rhost);
        port = 0;
        sscanf(p + 1, "%d", &port);
        if (!port) port = 9760;
        p = strchr(host, ':');
     *p = 0;
    } else {
        strcpy(host, rhost);
        port = 9760;
    }
     */
    port = 9760;
    sprintf(host, "0.0.0.0");

    hs = gethostbyname(host);
    if (hs) {
        ip = *(WORD32 *) hs->h_addr_list[0];
    } else ip = 0;
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd != -1) {
        i = 0;
        i = 1;
        setsockopt(sd, SOL_SOCKET, SO_KEEPALIVE, (char *) &i, sizeof (i)); // make sure, TCP is doing the keep alive mechanism to detect broken network links

        memset(&sa, '\0', sizeof (sa));
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = ip; /* Server IP */
        sa.sin_port = htons((short) port); /* Server Port number */
        err = bind(sd, (struct sockaddr*) &sa, sizeof (sa));
        if (!err) {
            err = listen(sd, MAX_COMMAND_SOCK);
            if (err) {
                if (lastmes != 2) {
                    airnav_log("Command Server can not listen");
                }
                lastmes = 2;
            } else {
                lastmes = 0;
                sprintf(message, "Command Server at %s:%d is accepting connections", inet_ntoa(sa.sin_addr), port);
                airnav_log(message);
                i = 1;
#ifdef WIN32
                ioctlsocket(sd, FIONBIO, &i);
#else
                ioctl(sd, FIONBIO, &i);
#endif
            }
        } else {
            if (lastmes != 1) {
                airnav_log("Command Server can not bind");
            }
            lastmes = 1;
        }
        if (!err) {
            ret = 0;
            command_server_sock = sd;
            command_server_status |= 5;

            //if (debug_enabled)
            //{
            //	sprintf(remain,"check_http_header %d/%d buf size %d:\n%s",k,l,i,buf);
            //	airnav_log(remain);
            //}
        } else {
#ifdef WIN32
            closesocket(sd);
#else
            close(sd);
#endif
            command_server_status &= ~5;
            ret = -3;
        }
    } else ret = -2;
    return ret;
}

int command_server_close(int flag) {
    int i;
    char mes[2048];

    if (command_server_sock) {
        for (i = 0; i < MAX_COMMAND_SOCK; i++) {
            if (command_server_conn[i]) {
                if (!flag) {
                    sprintf(mes, "Command Server disconnected Client %d at %s\n", i, inet_ntoa(command_server_ip[i]));
                    airnav_log(mes);
                }
#ifdef WIN32
                closesocket(command_server_conn[i]);
#else
                close(command_server_conn[i]);
#endif
                command_server_conn[i] = 0;
                command_server_status &= ~5;
            }
        }
#ifdef WIN32
        closesocket(command_server_sock);
#else
        close(command_server_sock);
#endif
        command_server_sock = 0;
        command_server_status = 0;
        if (!flag) airnav_log("Command Server shut down\n");
    }
    return 0;
}

int command_server_read(void) {
    int i, j, k;
    // fd_set fds,rds;
    // struct timeval tv;
    struct sockaddr sa;
    char mes[65536];
    static int rxbuf;
    static char buf[65536];

    // FD_ZERO(&fds);
    // FD_SET(raw_sock,&fds);
    // tv.tv_sec=0;tv.tv_usec=30;
    // j=select(raw_sock+1,&fds,NULL,NULL,&tv);
    // if (j>0)
    if (command_server_conn[0]) {
        if (!(command_server_status & 32) && ((time(0L) - command_lastping) >= 30)) {
            sockwrite_noblock(command_server_conn[0], "ERROR: Time Out\r\n", 17);
            //k = errno1();
            i = 0;
#ifdef WIN32
            closesocket(command_server_conn[0]);
#else
            close(command_server_conn[0]);
#endif
            sprintf(mes, "command_server_read (ping): Command Server disconnected Client %d at %s, errno: %d (%s)", i, inet_ntoa(command_server_ip[i]), k, strerror(k));
            airnav_log(mes);
            command_server_conn[i] = 0;
            command_server_status &= 5; // lock command channel and clear all comm flags
        }
    }
    if (command_server_sock > 0) {
        j = sizeof (sa);
        k = accept(command_server_sock, &sa, &j);
        if (k > 0) {
            for (i = 0; i < MAX_COMMAND_SOCK && command_server_conn[i]; i++);
            if (i < MAX_COMMAND_SOCK) {
                struct linger so_linger;
                j = 1;
#ifdef WIN32
                ioctlsocket(k, FIONBIO, &j);
#else
                ioctl(k, FIONBIO, &j);
#endif
                so_linger.l_onoff = 1;
                so_linger.l_linger = 0;
                setsockopt(k, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
                j = 1;
                setsockopt(k, IPPROTO_TCP, TCP_NODELAY, (char *) &j, sizeof (int));
                command_server_conn[i] = k;
                command_server_ip[i] = *(struct in_addr *) &((struct sockaddr_in *) &sa)->sin_addr.s_addr;
                sprintf(mes, "Command Server connected Client %d at %s", i, inet_ntoa(command_server_ip[i]));
                airnav_log(mes);
                command_lastping = time(0L);
                command_write_version(1);
                // command_write_status();
                rxbuf = 0;
            } else {
                sprintf(mes, "Command Server could not connect Client at %s, all connections already in use", inet_ntoa(*(struct in_addr *) &((struct sockaddr_in *) &sa)->sin_addr.s_addr));
                airnav_log(mes);
                sockwrite_noblock(k, "ERROR: Another Command Channel Active Already\r\n", 47);
                close(k);
            }
        } else if (k < 0) {
#ifdef WIN32
            j = WSAGetLastError();
            if (j != WSAEWOULDBLOCK)
#else
            j = errno;
            if (j != EAGAIN && j != EWOULDBLOCK)
#endif
            {
                command_server_close(0);
                command_server_connect(command_server_url);
            }
        }
    }
    for (i = 0; i < MAX_COMMAND_SOCK; i++) { // just throw away incoming data
        if (command_server_conn[i]) {
            j = sockread(command_server_conn[i], buf + rxbuf, sizeof (buf) - rxbuf);
            if (j > 0) {
                //allwinner_set_usb_led_on();
                rxbuf += j;
                buf[rxbuf] = 0;
                if (commanddebugfp) {
                    fwrite(buf, 1, rxbuf, commanddebugfp);
                }
                rxbuf = handle_command_buffer(buf, rxbuf, 1);
                if (commanddebugfp) {
                    fprintf(commanddebugfp, "\x1b\x37-%d-999999999999999\x1b\x30", rxbuf);
                }
            }
#ifdef WIN32
            if (j < 0)
#else
            if (j < 0 && errno != EINTR)
#endif
            {
                //k = errno1();
#ifdef WIN32
                closesocket(command_server_conn[i]);
#else
                close(command_server_conn[i]);
#endif
                sprintf(mes, "command_server_read: Command Server disconnected Client %d at %s, errno: %d (%s)", i, inet_ntoa(command_server_ip[i]), k, strerror(k));
                airnav_log(mes);
                command_server_conn[i] = 0;
                command_server_status &= 5; // lock command channel and clear all comm flags
            }
        }
        if ((command_server_status & 66) == 66) {
            static double lastgps;

            struct {
                WORD32 lat;
                WORD32 lon;
                WORD16 alt;
                UBYTE flag;
                UBYTE vis;
                UWORD64 tim;
            } gpsdat;
            int ii, jj, kk;
            UWORD64 t1;
#ifdef WIN32
            struct timeval tv;
#else
            struct timespec tv;
#endif

#ifdef WIN32
            ftime(&ft);
            t1 = (UWORD64) ft.time * 1000000 + ft.millitm * 1000;
#else
            clock_gettime(CLOCK_REALTIME, &tv);
            t1 = (UWORD64) tv.tv_sec * 1000000000LL + tv.tv_nsec;
#endif
            
            //if ((t1 - lastgps) >= 5000000000LL) {
            //    lastgps = t1;
            //    get_airnav_gps(&gpsdat.lat, &gpsdat.lon, &gpsdat.alt, &ii, &jj, &kk);
            //    gpsdat.flag = ((ii & 7) << 5)+(kk & 31);
            //    gpsdat.vis = jj;
            //    gpsdat.tim = t1;
            //    rb3_send_escape_buffer(1, '8', (char *) &gpsdat, sizeof (gpsdat));
           // }
        }
    }
    return 0;
}

int command_server_send(char *buf, int len) {
    int i, j, k;
    char mes[2048];

    j = 0;
    for (i = 0; i < MAX_COMMAND_SOCK; i++) {
        if (command_server_conn[i]) {
            j = sockwrite_noblock(command_server_conn[i], buf, len);
            if (j < 0) {
#ifdef WIN32
                closesocket(command_server_conn[i]);
#else
                close(command_server_conn[i]);
#endif
                sprintf(mes, "command_server_send: Command Server disconnected Client %d at %s", i, inet_ntoa(command_server_ip[i]));
                airnav_log(mes);
                command_server_conn[i] = 0;
                command_server_status &= 5; // lock command channel and clear all comm flags
            }
        }
    }
    return j;
}

int command_server_send_block(char *buf, int len) {
    int i, j, k;
    char mes[2048];

    j = 0;
    for (i = 0; i < MAX_COMMAND_SOCK; i++) {
        if (command_server_conn[i]) {
            j = sockwrite(command_server_conn[i], buf, len);
            if (j < 0) {
#ifdef WIN32
                closesocket(command_server_conn[i]);
#else
                close(command_server_conn[i]);
#endif
                sprintf(mes, "command_server_send: Command Server disconnected Client %d at %s", i, inet_ntoa(command_server_ip[i]));
                airnav_log(mes);
                command_server_conn[i] = 0;
                command_server_status &= 5; // lock command channel and clear all comm flags
            }
        }
    }
    return j;
}

int sockwrite_noblock(int sock, char *buf, int len) {
    int i, j, k, l, to, ii;
    time_t lt;

    if (len) {
        l = 0;
        lt = time(0L);
        //to=len/240; if (to<30) to=30;
        j = len;
#ifdef WIN32
        ii = i = send(sock, &buf[l], j, 0);
#else
        ii = i = write(sock, &buf[l], j);
#endif
        if (i >= 0) {
            l += i;
            j -= i;
            if (i != j) i = -1;
            errno = EAGAIN;
        }
        if (i < 0) {
            if (errno == EAGAIN) {
#ifndef WIN32
                usleep(1000);
#else
                Sleep(1);

#endif
#ifdef WIN32
                ii = i = send(sock, &buf[l], j, 0);
#else
                ii = i = write(sock, &buf[l], j);
#endif
                if (i >= 0) l += i, j -= i;
                i = 0;
            }
        }
        if (i < 0) l = i;
    } else l = 0;
    return l;
}

int command_write_version(int flag) {
    int ret;
    char message[2048];

#ifndef WIN32
    ioctl(command_socket, TIOCOUTQ, &ret);
    if (ret < 2048)
#else
    if (1)
#endif
    {
        sprintf(message, "Radarbox ComStation V1.00 (SN: ANRB3%05d Firmware: %s)\x0d\x0a", airnav_serial, avhversionstring);
        if (!flag) {
            //allwinner_set_usb_led_on();
            ret = serial_write(command_socket, message, strlen(message));
            if (ret <= 0) {
                if (command_status & 4 && verboselevel >= 2) {
                    int err1;
                    char message[2048];

                   // err1 = errno1();
                    sprintf(message, "Command Channel write error: %d (%d/%s)", ret, err1, strerror(err1));
                    airnav_log(message);
                }
                if (ret < 0) { // usb connection broke, we need to reopen
                    command_close();
                    command_connect(command_url);
                } else {
                    // command_status&=~4;
                }
            }
        } else {
            //allwinner_set_usb_led_on();
            ret = command_server_send(message, strlen(message));
            if (ret <= 0) {
                if (command_server_status & 4 && verboselevel >= 2) {
                    int err1;
                    char message[2048];

                    //err1 = errno1();
                    sprintf(message, "Command Channel write error: %d (%d/%s)", ret, err1, strerror(err1));
                    airnav_log(message);
                }
                if (ret < 0) { // usb connection broke, we need to reopen
                    command_server_close(0);
                    command_server_connect(command_server_url);
                } else {
                    // command_server_status&=~4;
                }
            }
        }
    } else ret = 0;
    return ret;
}

int sockread(int sock, char *buf, int len) {
    int i, j, k, l, ii;

#ifdef WIN32
    l = sizeof (k);
    k = 1;
    i = -1;
    j = getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *) &k, &l);
    if (!j && !k) {
        i = recv(sock, buf, len, 0);
        if (i < 0) {
            int j;

            errno = j = WSAGetLastError();
            if (j == WSAEWOULDBLOCK) {
                i = 0;
            } else {
                j = 2;
            }
        } else if (i == 0) { // peer requests close!
            i = -999;
        }
    } else if (!j) {
        j = 1;
    } else {
        j = 0;
    }
#else
    do {
        ii = i = read(sock, buf, len);
    } while (i < 0 && errno == EINTR);
    if (i < 0) {
        if (errno == EAGAIN) i = 0;
    } else if (i == 0) { // peer requests close
        i = -999;
    } else if (sock == usb_socket && debugfp) {
        struct timeb ft;
        struct tm *tm;
        char buf1[65537];

        ftime(&ft);
        tm = gmtime(&ft.time);
        fprintf(debugfp, "%2d.%02d.%04d %02d:%02d:%02d.%03d <- %04x: ", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900, tm->tm_hour, tm->tm_min, tm->tm_sec, ft.millitm, i);
        if (i > 0) {
            for (j = 0; j < i; j++) {
                fprintf(debugfp, "%02x ", buf[j]);
                if (buf[j] >= 32) buf1[j] = buf[j];
                else buf1[j] = '.';
            }
            buf1[j] = 0;
            fprintf(debugfp, " \"%s\"\n", buf1);
        } else {
            //fprintf(debugfp, "Status: %d, errno: %d (%s)\n", i, errno1(), strerror1(errno1()));
        }
        // fflush(debugfp);
    }
#endif
    return i;
}

int sockwrite(int sock, char *buf, int len) {
    int i, j, k, l, to, ii;
    time_t lt;
#ifdef WIN32
    int err;
#endif

    if (len) {
        l = 0;
        lt = time(0L);
        //to=len/240; if (to<30) to=30;
        to = 270;
        do {
            k = 0;
            j = 1024;
            if (len < j) j = len;
#ifdef WIN32
            ii = i = send(sock, &buf[l], j, 0);
#else
            ii = i = write(sock, &buf[l], j);
#endif
            if (i < 0) {
#ifdef WIN32
                err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK || err == WSAEINTR)
#else
                if (errno == EAGAIN || errno == EINTR)
#endif
                {
                    k = 1;
#ifndef WIN32
                    usleep(1024);
#endif
                }
            } else if (i > 0) {
                lt = time(0L);
                l += i;
                len -= i;
                if (len > 0) k = 1;
            } else {
                i = -1;
            }
        } while (k && (time(0L) - lt)<(unsigned) to);
        if (i < 0) l = i;
        if (l >= 0 && k) l = -2;
#ifndef WIN32
        if (sock == usb_socket && debugfp) {
            struct timeb ft;
            struct tm *tm;
            char buf1[65537];

            ftime(&ft);
            tm = gmtime(&ft.time);
            fprintf(debugfp, "%2d.%02d.%04d %02d:%02d:%02d.%03d -> %04x: ", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900, tm->tm_hour, tm->tm_min, tm->tm_sec, ft.millitm, (l < 0) ? 0 : l);
            if (l > 0) {
                for (j = 0; j < l; j++) {
                    fprintf(debugfp, "%02x ", buf[j]);
                    if (buf[j] >= 32) buf1[j] = buf[j];
                    else buf1[j] = '.';
                }
                buf1[j] = 0;
                fprintf(debugfp, " \"%s\"\n", buf1);
                // fflush(debugfp);
            } else {
                //fprintf(debugfp, "Status: %d, errno: %d (%s)\n", i, errno1(), strerror1(errno1()));
            }
        }
#endif
    } else l = 0;
    return l;
}


int handle_command_buffer(char *buf1, int n, int flag) {

    MODES_NOTUSED(buf1);
    MODES_NOTUSED(n);
    MODES_NOTUSED(flag);
    
    /*
    char c, *p, *q, *r, *rr, message[16384], buf[512], buf2[512];
    int i, j, k, l, m, rxbuf, ret;
    unsigned long lt, lt1;
    struct tm *tm;
    time_t lt2;
    static UWORD32 seed = 0, question = 0, answer = 0;
    static time_t qlast;
#ifdef WIN32
    struct timeval tv;
#else
    struct timespec tv;
#endif
    struct timeb ft;
    double t1;

    rxbuf = n;
    for (p = buf1; *p;) {
        rr = r = q = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        c = *p;
        *p = 0;
        if (c) {
            if (verboselevel >= 2) {
                k = sprintf(message, "Command Channel received: %s", r);
                airnav_log(message);
            }
            while (isspace(*q)) q++;
            if (!strnicmp(q, "version", 7)) {
                command_write_version(flag);
            } else if (!strnicmp(q, "ping", 4)) {
                if (!flag) {
                    if (command_status & 2) {
                        command_lastping = time(0L);
#ifdef WIN32
                        ftime(&ft);
                        lt2 = ft.time;
                        t1 = (double) ft.millitm / 1000.;
#else
                        clock_gettime(CLOCK_REALTIME, &tv);
                        lt2 = tv.tv_sec;
                        t1 = (double) tv.tv_nsec / 1000000000.;
#endif
                        tm = gmtime(&lt2);
                        sprintf(message, "PONG %4d-%s-%02d %02d:%02d:%09.6f\x0d\x0a", tm->tm_year + 1900, months[tm->tm_mon], tm->tm_mday, tm->tm_hour, tm->tm_min, (double) tm->tm_sec + t1);
                        i = serial_write(command_socket, message, strlen(message));
                        if (verboselevel >= 9) {
                            sprintf(message, "Command Channel sent (%d): PONG %4d-%s-%02d %02d:%02d:%09.6f", i, tm->tm_year + 1900, months[tm->tm_mon], tm->tm_mday, tm->tm_hour, tm->tm_min, (double) tm->tm_sec + t1);
                            airnav_log(message);
                        }
                    } else {
                        if (verboselevel >= 2) airnav_log("Command Channel sent: LOCKED");
                        serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                    }
                } else {
                    if (command_server_status & 2) {
                        command_lastping = time(0L);
#ifdef WIN32
                        ftime(&ft);
                        lt2 = ft.time;
                        t1 = (double) ft.millitm / 1000.;
#else
                        clock_gettime(CLOCK_REALTIME, &tv);
                        lt2 = tv.tv_sec;
                        t1 = (double) tv.tv_nsec / 1000000000.;
#endif
                        tm = gmtime(&lt2);
                        sprintf(message, "PONG %4d-%s-%02d %02d:%02d:%09.6f\x0d\x0a", tm->tm_year + 1900, months[tm->tm_mon], tm->tm_mday, tm->tm_hour, tm->tm_min, (double) tm->tm_sec + t1);
                        i = command_server_send(message, strlen(message));
                        if (verboselevel >= 9) {
                            sprintf(message, "Command Channel sent (%d): PONG %4d-%s-%02d %02d:%02d:%09.6f", i, tm->tm_year + 1900, months[tm->tm_mon], tm->tm_mday, tm->tm_hour, tm->tm_min, (double) tm->tm_sec + t1);
                            airnav_log(message);
                        }
                    } else {
                        if (verboselevel >= 2) airnav_log("Command Channel sent: LOCKED");
                        command_server_send("LOCKED\x0d\x0a", 8);
                    }
                }
            } else if (!strnicmp(q, "unlock", 6)) {
                if (!flag) {
                    command_status &= ~2;
                    sscanf(q + 6, "%x", &i);
                    seed = i;
                    qlast = time(0L);
                    srand(qlast);
                    seed = SWAP32(seed);
                    seed = seed ^ rand();
                    seed = (seed >> 13) | (seed << 19);
                    question = seed ^ AUTH_KEY_MIX__QUESTION;
                    sprintf(message, "QUESTION %X\x0d\x0a", question);
                    i = serial_write(command_socket, message, strlen(message));
                } else {
                    command_server_status &= ~2;
                    sscanf(q + 6, "%x", &i);
                    seed = i;
                    qlast = time(0L);
                    srand(qlast);
                    seed = SWAP32(seed);
                    seed = seed ^ rand();
                    seed = (seed >> 13) | (seed << 19);
                    question = seed ^ AUTH_KEY_MIX__QUESTION;
                    sprintf(message, "QUESTION %X\x0d\x0a", question);
                    i = command_server_send(message, strlen(message));
                }
                if (verboselevel >= 2) {
                    sprintf(message, "Command Channel sent (%d): QUESTION %X", i, question);
                    airnav_log(message);
                }
            } else if (!strnicmp(q, "lock", 4)) {
                airnav_log("Command Channel locked");
                if (!flag) {
                    command_status &= ~(2 + 32); // clear authentication and autoping
                    serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    command_server_status &= ~(2 + 32); // clear authentication and autoping
                    command_server_send("LOCKED\x0d\x0a", 8);
                }
            } else if (!strnicmp(q, "answer", 6)) {
                sscanf(q + 6, "%x", &i);
                answer = i;
                lt2 = time(0L);
                if ((lt2 - qlast) < 10) {
                    j = SWAP32(question);
                    j = j ^ AUTH_KEY_MIX__ANSWER;
                    j = ((j >> 11)&0x001fffff) | ((j << 21)&0xffe00000);
                    j = j ^ AUTH_KEY_MIX__ANSWER;
                    if (j == answer) {
                        if (!flag) {
                            command_status |= 2;
                            serial_write(command_socket, "UNLOCKED\x0d\x0a", 10);
                        } else {
                            command_server_status |= 2;
                            command_server_send("UNLOCKED\x0d\x0a", 10);
                        }
                        airnav_log("Command Channel unlocked");
                        if (verboselevel >= 2) airnav_log("Command Channel sent: UNLOCKED");
                    } else {
                        if (!flag) {
                            command_status &= ~2;
                            serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                        } else {
                            command_server_status &= ~2;
                            command_server_send("LOCKED\x0d\x0a", 8);
                        }
                        if (verboselevel >= 2) airnav_log("Command Channel sent: LOCKED");
                        airnav_log("Command Channel NOT unlocked due to wrong answer");
                    }
                } else {
                    if (!flag) {
                        command_status &= ~2;
                        serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                    } else {
                        command_server_status &= ~2;
                        command_server_send("LOCKED\x0d\x0a", 8);
                    }
                    airnav_log("Command Channel NOT unlocked due to timeout");
                }
            } else if (!strnicmp(q, "~3NOMIS2504", 11)) {
                if (!flag) {
                    command_status |= 2 + 32; // unlock and autoping, no time out
                    serial_write(command_socket, "UNLOCKED\x0d\x0a", 10);
                } else {
                    command_server_status |= 2 + 32; // unlock and autoping, no time out
                    command_server_send("UNLOCKED\x0d\x0a", 10);
                }
                airnav_log("Command Channel unlocked with general key");
                if (verboselevel >= 2) airnav_log("Command Channel sent: UNLOCKED");
            } else if (!strnicmp(q, "~3SERVER_ON", 8)) {
                strcpy(message, "SERVER_ON\x0d\x0a");
                if (!flag) {
                    if (command_status & 2) {
                        airnav_set_on_off(1);
                        command_status |= 8;
                        serial_write(command_socket, message, strlen(message));
                    } else {
                        serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                    }
                } else {
                    if (command_server_status & 2) {
                        airnav_set_on_off(1);
                        command_server_send(message, strlen(message));
                    } else {
                        command_server_send(command_socket, "LOCKED\x0d\x0a", 8);
                    }
                }
            } else if (!strnicmp(q, "~3SERVER_OFF", 9)) {
                strcpy(message, "SERVER_OFF\x0d\x0a");
                if (!flag) {
                    if (command_status & 2) {
                        airnav_set_on_off(0);
                        serial_write(command_socket, message, strlen(message));
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        airnav_set_on_off(0);
                        command_server_send(message, strlen(message));
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            } else if (!strnicmp(q, "~3ADS_ON", 8)) {
                strcpy(message, "ADS_ON\x0d\x0a");
                if (!flag) {
                    if (command_status & 2) {
                        command_status |= 8;
                        serial_write(command_socket, message, strlen(message));
                    } else {
                        serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                    }
                } else {
                    if (command_server_status & 2) {
                        command_server_status |= 8;
                        command_server_send(message, strlen(message));
                    } else {
                        command_server_send(command_socket, "LOCKED\x0d\x0a", 8);
                    }
                }
            } else if (!strnicmp(q, "~3ADS_OFF", 9)) {
                strcpy(message, "ADS_OFF\x0d\x0a");
                if (!flag) {
                    if (command_status & 2) {
                        command_status &= ~8;
                        serial_write(command_socket, message, strlen(message));
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        command_server_status &= ~8;
                        command_server_send(message, strlen(message));
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            } else if (!strnicmp(q, "~3GPS_ON", 8)) {
                strcpy(message, "GPS_ON\x0d\x0a");
                if (!flag) {
                    if (command_status & 2) {
                        command_status |= 64;
                        serial_write(command_socket, message, strlen(message));
                    } else {
                        serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                    }
                } else {
                    if (command_server_status & 2) {
                        command_server_status |= 64;
                        command_server_send(message, strlen(message));
                    } else {
                        command_server_send(command_socket, "LOCKED\x0d\x0a", 8);
                    }
                }
            } else if (!strnicmp(q, "~3GPS_OFF", 9)) {
                strcpy(message, "GPS_OFF\x0d\x0a");
                if (!flag) {
                    if (command_status & 2) {
                        command_status &= ~64;
                        serial_write(command_socket, message, strlen(message));
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        command_server_status &= ~64;
                        command_server_send(message, strlen(message));
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            } else if (!strnicmp(q, "~3RAD_ON", 8)) {
                strcpy(message, "RAD_ON\x0d\x0a");
                if (!flag) {
                    if (command_status & 2) {
                        command_status |= 16;
                        dumpvhf_radio_onoff(0, 1);
                        serial_write(command_socket, message, strlen(message));
                    } else {
                        serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                    }
                } else {
                    if (command_server_status & 2) {
                        command_server_status |= 16;
                        dumpvhf_radio_onoff(0, 1);
                        command_server_send(message, strlen(message));
                    } else {
                        command_server_send(command_socket, "LOCKED\x0d\x0a", 8);
                    }
                }
            } else if (!strnicmp(q, "~3RAD_OFF", 9)) {
                strcpy(message, "RAD_OFF\x0d\x0a");
                if (!flag) {
                    if (command_status & 2) {
                        command_status &= ~16;
                        dumpvhf_radio_onoff(0, 0);
                        serial_write(command_socket, message, strlen(message));
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        command_server_status &= ~16;
                        dumpvhf_radio_onoff(0, 0);
                        command_server_send(message, strlen(message));
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            } else if (!strnicmp(q, "~3RAD_RESET", 11)) {
                strcpy(message, "RAD_RESET\x0d\x0a");
                if (!flag) {
                    if (command_status & 2) {
                        // command_status&=~16;
                        dumpvhf_close(0);
                        serial_write(command_socket, message, strlen(message));
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        // command_server_status&=~16;
                        dumpvhf_close(0);
                        command_server_send(message, strlen(message));
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            } else if (!strnicmp(q, "~3SQUELCH", 9)) {
                int dumpvhf_set_squelch(int channel, int level);
                int level, n;
                char *r;

                r = q;
                n = get_command(&r, message);
                level = 0;
                sscanf(message + 9, "%d", &level);
                if (level < 0) level = 0;
                if (level > 2047) level = 2047;
                if (!flag) {
                    if (command_status & 2) {
                        serial_write(command_socket, message, strlen(message));
                        dumpvhf_set_squelch(0, level);
                        sprintf(buf, "%d", level);
                        setstringini("VHF_00", "squelch", buf);
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        command_server_send(message, strlen(message));
                        dumpvhf_set_squelch(0, level);
                        sprintf(buf, "%d", level);
                        setstringini("VHF_00", "squelch", buf);
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            } else if (!strnicmp(q, "~3AUDIO_AUTOMATIC_GAIN", 22)) {
                int onoff, n;
                char *r, buf[256];

                r = q;
                n = get_command(&r, message);
                onoff = 1;
                buf[255] = 0;
                sscanf(message + 22, "%255s", buf);
                buf[255] = 0;
                if (buf[0] && stristr(buf, "off")) onoff = 0;
                if (!flag) {
                    if (command_status & 2) {
                        serial_write(command_socket, message, strlen(message));
                        dumpvhf_set_audio_automatic_gain(0, onoff);
                        setstringini("audio", "autogain", onoff ? "1" : "0");
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        command_server_send(message, strlen(message));
                        dumpvhf_set_audio_automatic_gain(0, onoff);
                        setstringini("audio", "autogain", onoff ? "1" : "0");
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            } else if (!strnicmp(q, "~3MOBILE", 8)) {
                int n;
                char *r;

                r = q;
                n = get_command(&r, message);
                if (!flag) {
                    if (command_status & 2) {
                        serial_write(command_socket, message, strlen(message));
                        mobile = 1;
                        setstringini("server", "gps_mobile", "1");
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        command_server_send(message, strlen(message));
                        mobile = 1;
                        setstringini("server", "gps_mobile", "1");
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            } else if (!strnicmp(q, "~3STATIONARY", 12)) {
                int n;
                char *r;

                r = q;
                n = get_command(&r, message);
                if (!flag) {
                    if (command_status & 2) {
                        serial_write(command_socket, message, strlen(message));
                        mobile = 0;
                        setstringini("server", "gps_mobile", "0");
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        command_server_send(message, strlen(message));
                        mobile = 0;
                        setstringini("server", "gps_mobile", "0");
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            }
#ifdef ALLWINNER
            else if (!strnicmp(q, "~3WATCHDOG", 10)) {
                int n, m;
                char *r;

                r = q;
                n = get_command(&r, message);
                sscanf(message + 10, "%d", &m);
                if (!flag) {
                    if (command_status & 2) {
                        serial_write(command_socket, message, strlen(message));
                        sprintf(buf2, "%d", m);
                        setstringini("server", "watchdog", buf2);
                        if (m >= 0) {
                            allwinner_start_watchdog(m);
                            watchdog_enable = m;
                        } else {
                            allwinner_stop_watchdog();
                            watchdog_enable = -1;
                        }
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        command_server_send(message, strlen(message));
                        sprintf(buf2, "%d", m);
                        setstringini("server", "watchdog", buf2);
                        if (m >= 0) {
                            allwinner_start_watchdog(m);
                            watchdog_enable = m;
                        } else {
                            allwinner_stop_watchdog();
                            watchdog_enable = -1;
                        }
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            }
#endif
            else if (!strnicmp(q, "~3FREQ", 6)) {
                int dumpvhf_set_frequencies(int channel, int n, uint32_t * freqs);
                int level, n, m;
                uint32_t freqs[2144];
                char *r;

                r = q;
                n = get_command(&r, message);
                level = 0;
                m = get_freq(message + 6, freqs);
                if (!flag) {
                    if (command_status & 2) {
                        serial_write(command_socket, message, strlen(message));
                        dumpvhf_set_frequencies(0, m, freqs);
                        for (i = 0; i < m; i++) {
                            sprintf(buf2, "freq_%02d", i);
                            sprintf(buf, "%d.%03d", freqs[i] / 1000000, (freqs[i] / 1000) % 1000);
                            setstringini("VHF_00", buf2, buf);
                        }
                        sprintf(buf2, "freq_%02d", i);
                        setstringini("VHF_00", buf2, "");
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        command_server_send(message, strlen(message));
                        dumpvhf_set_frequencies(0, m, freqs);
                        for (i = 0; i < m; i++) {
                            sprintf(buf2, "freq_%02d", i);
                            sprintf(buf, "%d.%03d", freqs[i] / 1000000, (freqs[i] / 1000) % 1000);
                            setstringini("VHF_00", buf2, buf);
                        }
                        sprintf(buf2, "freq_%02d", i);
                        setstringini("VHF_00", buf2, "");
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            } else if (!strnicmp(q, "~3RESET_RANGE", 13)) {
                int onoff, n;
                char *r, buf[256];

                r = q;
                n = get_command(&r, message);
                if (!flag) {
                    if (command_status & 2) {
                        sprintf(buf, "Reset Range File (command channel), Receiver Lat: %.2lf %.2lf Range Center: %.2lf %.2lf", receiver_lat, receiver_lon, range.rec_lat, range.rec_lon);
                        airnav_log(buf);
                        memset(&range, 0, sizeof (range));
                        range.rec_lat = receiver_lat;
                        range.rec_lon = receiver_lon;
                        save_range_file(rangefile);
                        serial_write(command_socket, message, strlen(message));
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        sprintf(buf, "Reset Range File (command channel), Receiver Lat: %.2lf %.2lf Range Center: %.2lf %.2lf", receiver_lat, receiver_lon, range.rec_lat, range.rec_lon);
                        airnav_log(buf);
                        memset(&range, 0, sizeof (range));
                        range.rec_lat = receiver_lat;
                        range.rec_lon = receiver_lon;
                        save_range_file(rangefile);
                        command_server_send(message, strlen(message));
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            } else if (!strnicmp(q, "~3GET_RANGE", 13)) {
                int onoff, n;
                char *r;

                r = q;
                n = get_command(&r, message);
                if (!flag) {
                    if (command_status & 2) {
                        serial_write(command_socket, message, strlen(message));
                        save_range_file(rangefile);
                        strcpy(message, rangefile);
                        strcat(message, ".kml");
                        command_send_file(0, message);
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        command_server_send(message, strlen(message));
                        save_range_file(rangefile);
                        strcpy(message, rangefile);
                        strcat(message, ".kml");
                        command_send_file(1, message);
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            }
#ifdef AIRNAV
            else if (!strnicmp(q, "~3NET", 5)) {
                if (!flag) {
                    if (command_status & 2) {
                        r = q + 5;
                        while (*r == ' ' || *r == '\r') r++;
                        if (!strnicmp(r, "DHCP", 4)) {
                            ret = set_interface_net_params(0, NULL, NULL, NULL, NULL, NULL);
                            if (ret < 0) {
                                j = sprintf(buf, "NET PARAMS ERROR %d\x0d\x0a", ret);
                                serial_write(command_socket, buf, j);
                            } else {
                                serial_write(command_socket, "NET PARAMS SET\x0d\x0a", 16);
                            }
                        } else if (!strnicmp(r, "STATIC", 6)) {
                            char ip[512], mask[512], gw[512], dns1[512], dns2[512];

                            // j=sscanf(r+6,"%s,%s,%s,%s,%s",ip,mask,gw,dns1,dns2);
                            // j=sscanf(r+6,"%[0-9.],%[0-9.],%[0-9.],%[0-9.],%[0-9.]",ip,mask,gw,dns1,dns2);
                            r += 6;
                            while (*r == ' ' || *r == '\t' || *r == ',') r++;
                            j = get_string(&r, ip);
                            if (j) j = get_string(&r, mask);
                            if (j) j = get_string(&r, gw);
                            if (j) j = get_string(&r, dns1);
                            if (j) get_string(&r, dns2);
                            if (j && !dns2[0]) {
                                ret = set_interface_net_params(1, ip, mask, gw, dns1, NULL);
                            } else if (j) {
                                ret = set_interface_net_params(1, ip, mask, gw, dns1, dns2);
                            } else {
                                ret = -99;
                            }
                            if (ret < 0) {
                                j = sprintf(buf, "NET PARAMS ERROR %d\x0d\x0a", ret);
                                serial_write(command_socket, buf, j);
                            } else {
                                serial_write(command_socket, "NET PARAMS SET\x0d\x0a", 16);
                            }
                        } else if (!strnicmp(r, "RESTART", 7)) {
                            serial_write(command_socket, "NET RESTARTING\x0d\x0a", 16);
                            ret = set_interface_net_restart();
                        } else {
                            serial_write(command_socket, "NET PARAMS ERROR -99\x0d\x0a", 22);
                        }
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        r = q + 5;
                        while (*r == ' ' || *r == '\r') r++;
                        if (!strnicmp(r, "DHCP", 4)) {
                            ret = set_interface_net_params(0, NULL, NULL, NULL, NULL, NULL);
                            if (ret < 0) {
                                j = sprintf(buf, "NET PARAMS ERROR %d\x0d\x0a", ret);
                                command_server_send(command_socket, buf, j);
                            } else {
                                command_server_send(command_socket, "NET PARAMS SET\x0d\x0a", 16);
                            }
                        } else if (!strnicmp(r, "STATIC", 6)) {
                            char ip[512], mask[512], gw[512], dns1[512], dns2[512];

                            // j=sscanf(r+6,"%[0-9.],%[0-9.],%[0-9.],%[0-9.],%[0-9.]",ip,mask,gw,dns1,dns2);
                            r += 6;
                            while (*r == ' ' || *r == '\t' || *r == ',') r++;
                            j = get_string(&r, ip);
                            if (j) j = get_string(&r, mask);
                            if (j) j = get_string(&r, gw);
                            if (j) j = get_string(&r, dns1);
                            if (j) get_string(&r, dns2);
                            if (j && !dns2[0]) {
                                ret = set_interface_net_params(1, ip, mask, gw, dns1, NULL);
                            } else if (j) {
                                ret = set_interface_net_params(1, ip, mask, gw, dns1, dns2);
                            } else {
                                ret = -99;
                            }
                            if (ret < 0) {
                                j = sprintf(buf, "NET PARAMS ERROR %d\x0d\x0a", ret);
                                command_server_send(command_socket, buf, j);
                            } else {
                                command_server_send(command_socket, "NET PARAMS SET\x0d\x0a", 16);
                            }
                        } else if (!strnicmp(r, "RESTART", 7)) {
                            command_server_send(command_socket, "NET RESTARTING\x0d\x0a", 16);
                            ret = set_interface_net_restart();
                        } else {
                            command_server_send(command_socket, "NET PARAMS ERROR -99\x0d\x0a", 22);
                        }
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            } else if (!strnicmp(q, "~3WLAN", 6)) {
                char ssid[512], key[512], keytype[512];
                int set_interface_wlan_params(char *ssid, int sec, char *key);

                ret = -99;
                ssid[0] = key[0] = 0;
                r = q + 6;
                while (*r == ' ' || *r == '\r') r++;
                j = get_string(&r, keytype);
                if (!flag) {
                    if (command_status & 2) {
                        if (!strnicmp(keytype, "WPS", 3)) {
                            ret = set_interface_wlan_params(NULL, -1, NULL);
                        } else if (!strnicmp(keytype, "NONE", 4)) {
                            j = get_string(&r, ssid);
                            if (j && ssid[0]) {
                                ret = set_interface_wlan_params(ssid, 0, NULL);
                            }
                        } else if (!strnicmp(keytype, "PEM", 3)) {
                            j = get_string(&r, ssid);
                            if (j) j = get_string(&r, key);
                            if (j && ssid[0] && key[0]) {
                                ret = set_interface_wlan_params(ssid, 1, key);
                            }
                        } else if (!strnicmp(keytype, "WPA2", 4)) {
                            j = get_string(&r, ssid);
                            if (j) j = get_string(&r, key);
                            if (j && ssid[0] && key[0]) {
                                ret = set_interface_wlan_params(ssid, 2, key);
                            }
                        } else if (!strnicmp(keytype, "WPA", 3)) {
                            j = get_string(&r, ssid);
                            if (j) j = get_string(&r, key);
                            if (j && ssid[0] && key[0]) {
                                ret = set_interface_wlan_params(ssid, 2, key);
                            }
                        }
                        if (ret < 0) {
                            j = sprintf(buf, "WLAN PARAMS ERROR %d\x0d\x0a", ret);
                            serial_write(command_socket, buf, j);
                        } else {
                            serial_write(command_socket, "WLAN PARAMS SET\x0d\x0a", 16);
                        }
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        if (!strnicmp(keytype, "WPS", 3)) {
                            ret = set_interface_wlan_params(NULL, -1, NULL);
                        } else if (!strnicmp(keytype, "NONE", 4)) {
                            j = get_string(&r, ssid);
                            // j=sscanf(q+4,"%s",ssid);
                            if (j && ssid[0]) {
                                ret = set_interface_wlan_params(ssid, 0, NULL);
                            }
                        } else if (!strnicmp(keytype, "PEM", 3)) {
                            j = get_string(&r, ssid);
                            if (j) j = get_string(&r, key);
                            if (j && ssid[0] && key[0]) {
                                ret = set_interface_wlan_params(ssid, 1, key);
                            }
                        } else if (!strnicmp(keytype, "WPA2", 4)) {
                            j = get_string(&r, ssid);
                            if (j) j = get_string(&r, key);
                            if (j && ssid[0] && key[0]) {
                                ret = set_interface_wlan_params(ssid, 2, key);
                            }
                        } else if (!strnicmp(keytype, "WPA", 3)) {
                            j = get_string(&r, ssid);
                            if (j) j = get_string(&r, key);
                            if (j && ssid[0] && key[0]) {
                                ret = set_interface_wlan_params(ssid, 2, key);
                            }
                        }
                        if (ret < 0) {
                            j = sprintf(buf, "WLAN PARAMS ERROR %d\x0d\x0a", ret);
                            command_server_send(command_socket, buf, j);
                        } else {
                            command_server_send(command_socket, "WLAN PARAMS SET\x0d\x0a", 16);
                        }
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            }
#endif
            else if (!strnicmp(q, "~3ENQ_SYS", 8)) {
                int allwinner_get_error(int **errlist);
                int get_gps_status(void);
                int ntp_get_status(void);
                int allwinner_get_watchdog_status(void);
                char *allwinner_strerror(int err);
                int n, m, o, *list;

                m = 0;
                m += sprintf(message + m, "Radarbox ComStation V1.00 (SN: ANRB3%05d Firmware: %s)\x0d\x0a", airnav_serial, avhversionstring);
                m += sprintf(message + m, "Status\x0d\x0a------\x0d\x0a");
                if (rtlsdr_status & 1) {
                    m += sprintf(message + m, "ADS-B Receiver Connected\x0d\x0a");
                } else {
                    m += sprintf(message + m, "ADS-B Receiver Not connected\x0d\x0a");
                }
                if (dumpvhf_get_connected(0)) {
                    m += sprintf(message + m, "VHF Receiver connected\x0d\x0a");
                } else {
                    m += sprintf(message + m, "VHF Receiver Not connected\x0d\x0a");
                }
                if (get_gps_status()) {
                    m += sprintf(message + m, "GPS connected\x0d\x0a");
                } else {
                    m += sprintf(message + m, "GPS Not connected\x0d\x0a");
                }
                if (ntp_get_status()) {
                    m += sprintf(message + m, "NTP connected\x0d\x0a");
                } else {
                    m += sprintf(message + m, "NTP Not connected\x0d\x0a");
                }
                if (allwinner_get_watchdog_status()) {
                    m += sprintf(message + m, "WATCHDOG connected\x0d\x0a");
                } else {
                    m += sprintf(message + m, "WATCHDOG Not connected\x0d\x0a");
                }
                m += sprintf(message + m, "Errors\x0d\x0a------\x0d\x0a");
                n = allwinner_get_error(&list);
                if (!n) {
                    m += sprintf(message + m, "No Errors Active\x0d\x0a");
                } else {
                    m += sprintf(message + m, "%d Errors Active:\x0d\x0a", n);
                    for (o = 0; o < n; o++) {
                        m += sprintf(message + m, "Error %d:%s\x0d\x0a", list[o], allwinner_strerror(list[o]));
                    }
                }
                lt2 = time(0L);
                tm = gmtime(&lt2);
                m += sprintf(message + m, "Statistics\x0d\x0a----------\x0d\x0aUp-Time: %us Current Time: %4d.%02d.%02d %02d:%02d:%02dZ\x0d\x0a", (unsigned int) (time(0L) - system_start_time), tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
                i = dump1090_statistics_simon(message + m);
                if (i >= 0) m += i;
                for (j = 0; j < 16; j++) {
                    i = vhf_statistics_simon(j, message + m);
                    if (i >= 0) m += i;
                }
                i = gps_statistics_simon(message + m);
                if (i >= 0) m += i;
                i = ntp_statistics_simon(message + m);
                if (i >= 0) m += i;
                i = allwinner_statistics_simon(message + m);
                if (i >= 0) m += i;
                if (!flag) {
                    if (command_status & 2) {
                        rb3_send_escape_buffer(0, '9', message, strlen(message));
                    } else serial_write(command_socket, "LOCKED\x0d\x0a", 8);
                } else {
                    if (command_server_status & 2) {
                        rb3_send_escape_buffer(1, '9', message, strlen(message));
                    } else command_server_send("LOCKED\x0d\x0a", 8);
                }
            }
            p++;
            while (*p == '\n' || *p == '\r') p++;
            memmove(rr, p, rxbuf + 1 - (p - buf1));
            rxbuf -= p - rr;
            p = r;
        } else {
            rxbuf = p - buf1;
        }
    }
    return rxbuf;
      
     */ 
    
    return 0;
}


int rb3_send_escape_buffer(int channel,int type,char *buffer,int len)
{
	int i,j,ret;
	char buf[256*1024];

	buf[0]=0x1b;
	buf[1]=type;
	for(j=2,i=0;i<len;i++)
	{
		if (buffer[i]==0x1b)
		{
			buf[j]=buf[j+1]=0x1b;j+=2;
		}
		else
		{
			buf[j++]=buffer[i];
		}
	}
	buf[j++]=0x1b;buf[j++]='0';
	if (!channel)
	{
		ret=serial_write(command_socket,buf,j);
	}
	else
	{
		ret=command_server_send(buf,j);
	}
	//if (ret>0) 	allwinner_set_usb_led_on();
	return ret;
}

int rb3_send_escape_buffer_block(int channel,int type,char *buffer,int len)
{
	int i,j,ret;
	char buf[256*1024];

	buf[0]=0x1b;
	buf[1]=type;
	for(j=2,i=0;i<len;i++)
	{
		if (buffer[i]==0x1b)
		{
			buf[j]=buf[j+1]=0x1b;j+=2;
		}
		else
		{
			buf[j++]=buffer[i];
		}
	}
	buf[j++]=0x1b;buf[j++]='0';
	if (!channel)
	{
		ret=serial_write(command_socket,buf,j);
	}
	else
	{
		ret=command_server_send_block(buf,j);
	}
	//if (ret>0) 	allwinner_set_usb_led_on();
	return ret;
}


int command_close(void)
{
	int ret;
	char mes[2048];

	if (command_socket)
	{
#ifdef WIN32
		ret=CloseHandle(command_socket);
#else
		ret=close(command_socket);
#endif
	}
	command_socket=0;
	command_status=0;
	if (verboselevel>=9)
	{
		sprintf(mes,"Command Channel %s disconnected",command_url);
		airnav_log(mes);
	}
	return ret;
}


int command_connect(char *url)
{
	int			err,i,j,k,useheartbeat;
	struct sockaddr_in sa;
	struct hostent *hs;
//	struct hostent hs1;
	char buf[32768];
	char message[1024];
	time_t lt;
	char mes[2048];
	int err1;
	static int lasterr=0,lastconn=0;
#ifdef WIN32
	HANDLE sd;
	DCB dcb;
	COMMTIMEOUTS timout;
#else
	int sd;
#endif

	if (url[0])
	{
#ifdef WIN32
		if (verboselevel>=10 && !lastconn)
		{
			lastconn=1;
			sprintf(mes,"Command Channel %s connecting",url);
			airnav_log(mes);
		}
		sd=CreateFile(url,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
		if (sd!=INVALID_HANDLE_VALUE)
		{
			err=GetCommState(sd,&dcb);
			if (err)
			{
				dcb.BaudRate=3000000;
				dcb.ByteSize=8;
				dcb.Parity=NOPARITY;
				dcb.StopBits=ONESTOPBIT;
				dcb.fBinary=1;
				dcb.fParity=0;
				dcb.fOutxCtsFlow=0;
				dcb.fOutxDsrFlow=0;
				dcb.fDtrControl=DTR_CONTROL_DISABLE;
				dcb.fDsrSensitivity=0;
				dcb.fTXContinueOnXoff=0;
				dcb.fOutX=0;
				dcb.fInX=0;
				dcb.fErrorChar=0;
				dcb.fNull=0;
				dcb.fRtsControl=RTS_CONTROL_DISABLE;
				dcb.fAbortOnError=0;
				err=SetCommState(sd,&dcb);
				if (err)
				{
					err=GetCommTimeouts(sd,&timout);
					if (err)
					{
						timout.ReadTotalTimeoutConstant=8;
						timout.ReadTotalTimeoutMultiplier=0; 
						timout.WriteTotalTimeoutConstant=8; 
						timout.WriteTotalTimeoutMultiplier=0;
						err=SetCommTimeouts(sd,&timout);
						if (err)
						{
							EscapeCommFunction(sd,SETRTS);
							EscapeCommFunction(sd,SETDTR);
							Sleep(111);
							do
							{
								i=serial_read(sd,buf,sizeof(buf));
							}
							while (i>0);
							err=PurgeComm(sd,PURGE_RXABORT);
							if (!err) printf("RXABORT fail\n");
							err=PurgeComm(sd,PURGE_TXABORT);
							if (!err) printf("TXABORT fail\n");
							err=PurgeComm(sd,PURGE_RXCLEAR);
							if (!err) printf("RXPURGE fail\n");
							err=PurgeComm(sd,PURGE_TXCLEAR);
							if (!err) printf("TXPURGE fail\n");
							EscapeCommFunction(sd,CLRRTS);
							err=0;
						}
						else
						{
							err=-5;
						}
					}
					else
					{
						err=-4;
					}
				}
				else
				{
					err=-3;
				}
#else
		sd = open(url, O_RDWR | O_NOCTTY | O_NONBLOCK);
		if (sd>0)
		{
			
			sig_serial_start(sd);

			memset(&newtio,0,sizeof(newtio));

			i=fcntl(sd, F_SETFL, O_NONBLOCK);
			if (!i)
			{
				i=fcntl(sd, F_SETFL, FASYNC);
			}
			if (!i) 
			{
				i=tcgetattr(sd,&oldtio); /* save current port settings */
				if (!i)
				{
					// newtio=oldtio;
					cfsetispeed( &newtio, B3000000 ); // B921600 B115200
					cfsetospeed( &newtio, B3000000 );

					newtio.c_cflag = B3000000 | CS8 | CREAD | CLOCAL;
					newtio.c_cc[VTIME]=1;
					newtio.c_cc[VMIN]=0;

					i=tcsetattr(sd,TCSANOW,&newtio);
					if (!i)
					{
						// err=ioctl(sd,TIOCMGET,&i);
						// if (err)
						// {
						//	printf("error ioctl TIOCMGET %d/%s\n",errno1(),strerror1(errno1()));
						// }
						
						err=0;
					}
					else
					{
						printf("tcsetattr error %d/%s\n",errno1(),strerror1(errno1()));
						err=-3;
					}
				}
				else
				{
					printf("tcgetattr error %d/%s\n",errno1(),strerror1(errno1()));
					err=-4;
				}
			}
			else
			{
				printf("fcntl error %d/%s\n",errno1(),strerror1(errno1()));
				err=-2;
			}
		}
		else
		{
			err=-1;
		}
		if (err>=0)
		{
			err=0;
			// i|=TIOCM_DTR|TIOCM_RTS;
			// err=ioctl(sd,TIOCMSET,&i);
			// if (err>=0) err=0;
			if (!err)
			{
				// usleep(111000);
				tcflush(sd,TCIFLUSH);
				usleep(10000);
				tcflush(sd,TCIFLUSH);
#endif
				if (err>=0)
				{
					if (verboselevel>=9)
					{
						sprintf(mes,"Command Channel %s connected",url);
						airnav_log(mes);
					}
					command_socket=sd;
					command_status=5;
					command_lastping=time(0L);
					command_write_version(0);
					// command_write_status();
				}
				else
				{
					if (sd>0)
					{
#ifdef WIN32
						CloseHandle(sd);
#else
						close(sd);
#endif
					}
					command_socket=0;
					command_status=0;
				}
			}
			else
			{
				err=-2;
				if (sd>0)
				{
#ifdef WIN32
					CloseHandle(sd);
#else
					close(sd);
#endif
				}
				command_socket=0;
				command_status=0;
			}
		}
		else
		{
			err=-1;
		}
		if (err<0 && verboselevel>=1)
		{
			err1=errno1();
			if (lasterr!=err1)
			{
				lasterr=err1;
				sprintf(mes,"Command Channel %s Connect error %d, errno %d (%s)",url,err,err1,strerror1(err1));
				airnav_log(mes);
			}
		}
		else lasterr=0;
	}
	else
	{
		err=-99;
	}
	return err;
}
