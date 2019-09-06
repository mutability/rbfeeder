/*
 * AirNav
 */

#include "airnav.h"
#include "util.h"
#include "dump1090.h"
#include "dictionary.h"
#include "iniparser.h"
#include <stdlib.h>
#ifdef RBCSRBLC
#include "mod_mmio.h"

unsigned int SUNXI_PIO_BASE = 0;
static volatile long int *gpio_map = NULL;


#endif

/*
 * Main function for AirNav Feeder
 */
void airnav_main() {

    createPidFile();    
    
    date_time_set = 0;

#ifdef RBCSRBLC    

    // CPU Temperature
    mmio_write(0x01c25000, 0x0027003f);
    mmio_write(0x01c25010, 0x00040000);
    mmio_write(0x01c25018, 0x00010fff);
    mmio_write(0x01c25004, 0x00000090);

    int result;

    result = sunxi_gpio_init();
    if (result == SETUP_DEVMEM_FAIL) {
        printf("No access to /dev/mem. Try running as root!");
        exit(EXIT_FAILURE);
    } else if (result == SETUP_MALLOC_FAIL) {
        printf("No memory");
        exit(EXIT_FAILURE);
    } else if (result == SETUP_MMAP_FAIL) {
        printf("Mmap failed on module import");
        exit(EXIT_FAILURE);
    }

    // Configure GPIO for output
    sunxi_gpio_set_cfgpin(PIN_PG4, OUTPUT); // ADS-B
    sunxi_gpio_set_cfgpin(PIN_PG5, OUTPUT); // Status
    sunxi_gpio_set_cfgpin(PIN_PG6, OUTPUT); // GPS
    sunxi_gpio_set_cfgpin(PIN_PG7, OUTPUT); // PC
    sunxi_gpio_set_cfgpin(PIN_PG8, OUTPUT); // Error
    sunxi_gpio_set_cfgpin(PIN_PG9, OUTPUT); // VHF
    sunxi_gpio_set_cfgpin(PIN_PE8, OUTPUT); // RF_SW1
    sunxi_gpio_set_cfgpin(PIN_PE9, OUTPUT); // RF_SW2    

    led_off(LED_ADSB);
    led_on(LED_STATUS);
    led_off(LED_GPS);
    led_off(LED_PC);
    led_off(LED_ERROR);
    led_off(LED_VHF);
#endif        


    for (int ab = 0; ab < MAX_ANRB; ab++) {
        anrbList[ab].active = 0;
        anrbList[ab].socket = malloc(sizeof (int));
        *anrbList[ab].socket = -1;
    }


    airnav_log("System: %s\n", F_ARCH);
    #ifdef DEBUG_RELEASE
    airnav_log("****** Debug RELEASE ******\n");
    #endif
    airnav_log("Start date/time: %s\n", start_datetime);
    struct sigaction sigchld_action = {
        .sa_handler = SIG_DFL,
        .sa_flags = SA_NOCLDWAIT
    };

    sigaction(SIGCHLD, &sigchld_action, NULL);
    signal(SIGPIPE, sigpipe_handler);
    txend[0] = '~';
    txend[1] = '*';

    // Initialize encryption variables
    strcpy((char *)key, DEF_CONN_KEY );
    strcpy((char *)nonce, DEF_CONN_NONCE);    


#ifdef RBCS
    // Init GPS
    if ((rc = gps_open("localhost", "2947", &gps_data_airnav)) == -1) {
        airnav_log("code: %d, reason: %s\n", rc, gps_errstr(rc));
        gps_ok = 0;
    } else {
        gps_stream(&gps_data_airnav, WATCH_ENABLE | WATCH_JSON, NULL);
        gps_ok = 1;
    }

    /*
     * RF Filter
     * SW1=1 + SW2=0 => Filter ON
     * SW1=0 + SW2=1 => Filter OFF     
     */
    if (rf_filter_status == 0) {
        sunxi_gpio_output(RF_SW1, LOW);
        sunxi_gpio_output(RF_SW2, HIGH);
    } else {
        sunxi_gpio_output(RF_SW1, HIGH);
        sunxi_gpio_output(RF_SW2, LOW);
    }

#endif


    // Create mutex for counters
    if (pthread_mutex_init(&m_packets_counter, NULL) != 0) {
        printf("\n mutex init failed\n");
        exit(EXIT_FAILURE);
    }

    /*
     * Socket Mutex
     */
    if (pthread_mutex_init(&m_socket, NULL) != 0) {
        printf("\n mutex init failed\n");
        exit(EXIT_FAILURE);
    }

    /*
     * Copy Mutex
     */
    if (pthread_mutex_init(&m_copy, NULL) != 0) {
        printf("\n mutex init failed\n");
        exit(EXIT_FAILURE);
    }

    /*
     * Copy Mutex2
     */
    if (pthread_mutex_init(&m_copy2, NULL) != 0) {
        printf("\n mutex init failed\n");
        exit(EXIT_FAILURE);
    }


    /*
     * Cmd Mutex
     */
    if (pthread_mutex_init(&m_cmd, NULL) != 0) {
        printf("\n mutex init failed\n");
        exit(EXIT_FAILURE);
    }

    /*
     * Led ADSB Mutex
     */
    if (pthread_mutex_init(&m_led_adsb, NULL) != 0) {
        printf("\n mutex init failed\n");
        exit(EXIT_FAILURE);
    }


    /*
     * INI Mutex
     */
    if (pthread_mutex_init(&m_ini, NULL) != 0) {
        printf("\n mutex init failed\n");
        exit(EXIT_FAILURE);
    }


    packets_total = 0;
    packets_last = 0;

    // Thread to wait commands
    pthread_create(&t_waitcmd, NULL, tWaitCmds, NULL);

    if (net_mode > 0) {
        /*
         * Create thread that read from external dump and
         * send data to internal (local) dump
         */
        airnav_log_level(3, "Creating external source thread...\n");
        pthread_create(&t_ext_source, NULL, airnav_extSourceProccess, NULL);
    }

    // Thread to monitor connection with AirNav server
    pthread_create(&t_monitor, NULL, airnav_monitorConnection, NULL);


    // Start thread that prepare data and send
    pthread_create(&t_prepareData, NULL, airnav_prepareData, NULL);


    // Thread to show statistics on screen
    pthread_create(&t_statistics, NULL, airnav_statistics, NULL);


    // Thread to send stats
    pthread_create(&t_stats, NULL, airnav_send_stats_thread, NULL);

    // Thread to send data
    pthread_create(&t_send_data, NULL, threadSendData, NULL);


    // Thread for ANRB
    pthread_create(&t_anrb, NULL, thread_waitNewANRB, NULL);

    pthread_create(&t_anrb_send, NULL, thread_SendDataANRB, NULL);


#ifdef RBCS

    // Thread for VHF LED
    pthread_create(&t_vhf_led, NULL, airnavMonitorVhfLed, NULL);

#endif

#ifdef RBCSRBLC

    // Thread to updated ADS-B Led
    pthread_create(&t_led_adsb, NULL, thread_LED_ADSB, NULL);

#endif

#ifdef RBCS

    if (autostart_acars) {
        if (!checkACARSRunning()) {
            startACARS();
        }
    }

    if (autostart_vhf) {
        if (!checkVhfRunning()) {
            startVhf();
        }
    }

#endif

    p_mlat = 0;
    if (autostart_mlat) {
        if (!checkMLATRunning()) {
            startMLAT();
        }
    }

}

/*
 * Close airnav socket
 */
void closeCon() {
    if (airnav_socket != -1) {
        close(airnav_socket);
        airnav_socket = -1;
    }
    return;
}

/*
 * Get patch of current file
 */
void call_realpath(char * argv0) {
    char resolved_path[PATH_MAX];
    binpath = realpath(argv0, resolved_path);

    if (binpath != NULL) {
        return;
    }


    int length;
    char fullpath[250] = {0};

    length = readlink("/proc/self/exe", fullpath, sizeof (fullpath));

    if (length < 0) {
        return;
    }
    if (length >= sizeof (fullpath)) {
        return;
    }

    binpath = malloc(length + 1);
    memset(binpath, '\0', length + 1);

#ifdef RBCSRBLC
    sprintf(binpath, "/radarbox/client/rbfeeder");
#else
    sprintf(binpath, "%s", fullpath);
#endif   

    return;

}

/*
 * Update current executable
 */
void doUpdate(char payload[501]) {

    MODES_NOTUSED(payload);
    airnav_log_level(3, "Running doUpdate function\n");
    call_realpath(program_invocation_name);

    char t_file[200] = {0};

    airnav_log_level(3, "Bin Path: '%s'\n", binpath);

    char *path = strdup(binpath);
    sprintf(t_file, "%s.tmp", path);


    if (last_payload[0] == '1') {

        if (strlen(last_payload) < 40) {
            airnav_log_level(3, "Invalid data from server, payload too small for update confirmation and url with checksum.\n");
            free(path);
            return;
        }

        // Extract CheckSum from payload
        char csum[33] = {0};
        memset(&csum, 0, 33);
        for (int x = 2; x < 34; x++) {
            csum[x - 2] = last_payload[x];
        }
        airnav_log_level(3, "Checksum (expected): %s\n", csum);

        char url[150] = {0};
        memset(&url, 0, 150);
        for (int x = 35; x < strlen(last_payload); x++) {
            url[x - 35] = last_payload[x];
        }
        airnav_log_level(3, "There is an update available at: %s\n", url);
        airnav_log_level(3, "Downloading to file: %s\n", t_file);


        if (access(t_file, F_OK) != -1) {
            // file exists
            airnav_log_level(3, "TMP file already exists!Trying to remove...\n");

            if (remove(t_file) != 0) {
                airnav_log_level(3, "Failed to delete temporary files.\n");
                free(path);
                return;
            }

        }

        if (get_page(url, t_file) == 1) {


            char *cs = md5sumFile(t_file);

            airnav_log_level(3, "Checksum received file: %s, informed by RPiserver: %s\n", cs, csum);

            if (strcmp(cs, csum) == 0) { // Checksum match
                char mode[] = "0555";
                int i;
                i = strtol(mode, 0, 8);
                if (chmod(t_file, i) < 0) {
                    fprintf(stderr, "error in chmod(%s, %s) - %d (%s)\n", t_file, mode, errno, strerror(errno));
                    free(cs);
                    free(path);
                    return;
                }

                char oldfile[1024] = {0};
                char curfile[1024] = {0};

                strcpy(curfile, path);
                sprintf(curfile, "%s", path);
                sprintf(oldfile, "%s.old", curfile);

                // If there's any old version, delete
                if (access(oldfile, F_OK) != -1) {
                    // file exists
                    if (remove(oldfile) != 0) {
                        airnav_log_level(3, "Failed to delete old file.\n");
                        perror("Error deleting file");
                        free(path);
                        return;
                    } else {
                        airnav_log_level(3, "OLD file deleted (%s)\n", oldfile);
                    }

                }


                if (access(curfile, F_OK) != -1) {
                    // Move original filename                
                    if (rename(curfile, oldfile) != 0) {
                        airnav_log_level(3, "Error renaming file to older\n");
                        perror("Error renaming file");
                        free(path);
                        return;
                    } else {
                        airnav_log_level(3, "Rename of current file (%s) to old file (%s) succesfull\n", curfile, oldfile);
                    }
                }
                sleep(1);
                if (access(curfile, F_OK) != -1) {
                    airnav_log_level(3, "Curfile exist (and should not exist!)! (%s)\n", curfile);
                }
                if (access(t_file, F_OK) != -1) {
                    airnav_log_level(3, "t_file exist! (%s)\n", t_file);
                }

                // Move tmp file to destination file            
                if (rename(t_file, curfile) != 0) {
                    airnav_log_level(3, "Error renaming file to latest version. t_file: %s, curfile: %s\n", t_file, curfile);
                    perror("Error renaming file");
                    free(path);
                    return;
                } else {
                    airnav_log_level(3, "Rename succesfull\n");
                }

                airnav_log("Finished downloading file!Update completed\nExiting now to restart...\n");
                //system("/sbin/reboot");
                Modes.exit = 1;
                free(path);
                Modes.exit = 1;
                return;

            } else {
                airnav_log_level(3, "Checksum of download file doesn't match.\n");
                free(cs);
                free(path);
                return;
            }


        } else {
            airnav_log_level(3, "Failed to download update file at: %s\n", url);
            free(path);
            return;
        }


    } else if (last_payload[0] == '0') {
        airnav_log_level(3, "There is no update available at this time.\n");
        free(path);
        return;
    }

    free(path);
    return;
}

/*
 * Send ACK and wait for reply from server
 */
int sendAck(void) {
    if (airnav_socket == -1) {
        airnav_log_level(7, "Socket not created!\n");
        return 0;
    }

    struct p_data *tmp = preparePacket();
    tmp->cmd = 4;
    if (!sendPacket(tmp)) {
        airnav_log_level(5, "Error sending ACK packet.\n");
        return -1;
    }

    return 1;

}

/*
 * Wait for specific CMD on socket
 */
int waitCmd(int cmd) {

    signal(SIGPIPE, sigpipe_handler);
    static uint64_t next_update;
    uint64_t now = mstime();
    int timeout = 0;
    int abort = 0;
    next_update = now + 1000;

    if (airnav_socket == -1) {
        airnav_log("Not connected to AirNAv Server\n");
        return 0;
    }

    pthread_mutex_lock(&m_cmd);
    expected = cmd;
    expected_arrived = 0;
    pthread_mutex_unlock(&m_cmd);

    // Initial com
    while (!abort) {

        if (Modes.exit) {
            abort = 1;
        }

        now = mstime();
        if (now >= next_update) {
            next_update = now + 1000;
            timeout++;
            airnav_log_level(4, "Timeout...%d (of %d)\n", timeout, AIRNAV_WAIT_PACKET_TIMEOUT);
        }

        if (timeout >= AIRNAV_WAIT_PACKET_TIMEOUT) {
            abort = 1;
        }

        pthread_mutex_lock(&m_cmd);
        if (expected_arrived == 1) {
            airnav_log_level(4, "Expected CMD has arrived!\n");
            expected_arrived = 0;
            expected = 0;
            pthread_mutex_unlock(&m_cmd);
            return cmd;
        }
        pthread_mutex_unlock(&m_cmd);

        usleep(100000);

    }
    airnav_log_level(4, "Expected packet did not arrived :(\n");

    return 0;
}

/*
 * Return the number of strings in char array
 */
int getArraySize(char *array) {
    int i = 0;
    if (array != NULL) {
        while (array[i] != '\0') {
            i++;
        }
    }
    return i;
}

/*
 * Send key and version to server.
 */
int sendKey(void) {
    if (airnav_socket == -1) {
        airnav_log_level(5, "Socket not created!\n");
        return 0;
    }

    airnav_log_level(3, "Sending key....\n");
    // KEY cmd = 1
    struct p_data *tmp = preparePacket();
    tmp->cmd = 1;
    tmp->c_version = c_version_int;
    tmp->c_version_set = 1;
    strcpy(tmp->c_key, sharing_key);
    tmp->c_key[32] = '\0';
    tmp->c_key_set = 1;
    tmp->c_type_set = 1;
    sendPacket(tmp);

    airnav_log_level(3, "Step 1 of sending key\n");

    // Wait for reply from server
    //
    int reply = waitCmd(2);
    airnav_log_level(3, "WaitCMD done!\n");
    if (reply == 2) {
        airnav_log_level(7, "Got OK from server!\n");
        return 1;
    } else if (reply == 0) {
        airnav_log_level(7, "Got no response from server\n");
        return 0;
    } else if (reply == 3) {
        airnav_log("Could not authenticate sharing key: %s\n", last_payload);
        return -1;
    } else {
        airnav_log_level(7, "Got another cmd from server :( (%d)\n", reply);
        return -1;
    }

    airnav_log_level(3, "Something wrong....\n");
    return 1;

}

/*
 * Send key request 
 */
int sendKeyRequest(void) {
    if (airnav_socket == -1) {
        airnav_log_level(5, "Socket not created!\n");
        return 0;
    }

    airnav_log_level(5, "Requesting new key!\n");

    // Request CMD = 6
    struct p_data *tmp = preparePacket();
    tmp->cmd = 6;
    tmp->c_version = c_version_int;
    tmp->c_version_set = 1;

    if ((strcmp(F_ARCH, "raspberry") == 0) || (strcmp(F_ARCH, "rblc2") == 0)) {
        uint64_t cpuserial = getSerial2();
        if (cpuserial != 0) {
            char cpserial[60] = {0};
            //sprintf(cpserial, "%016"PRIx32"", cpuserial);
            sprintf(cpserial, "%016llx", getSerial2());
            strcpy(tmp->payload, cpserial);
            airnav_log_level(2, "CPU Serial to send for key request: %s\n", cpserial);
            tmp->payload_set = 1;
        } else {
            airnav_log_level(2, "CPU Serial empty.\n");
        }
    } else if ((strcmp(F_ARCH, "rblc") == 0)) {
        char *cpuserial = getCPUSerial_RBLC();
        airnav_log_level(3, "Got CPU Serial: '%s'\n", cpuserial);
        if (cpuserial != NULL) {
            if (strlen(cpuserial) == 16) {
                sprintf(tmp->payload, "%s", cpuserial);
                tmp->payload_set = 1;
                airnav_log_level(2, "Sending key request with this serial: '%s'\n", cpuserial);
            } else {
                airnav_log_level(2, "Erro in serial size: '%d'\n", strlen(cpuserial));
            }
        }
    }

    if (strcmp(F_ARCH, "raspberry") == 0) {
        tmp->c_type = 0;
        tmp->c_type_set = 1;
        airnav_log_level(3, "Setting client type to 0 (Raspberry)\n");
    } else if (strcmp(F_ARCH, "rbcs") == 0) {
        tmp->c_type = 1;
        tmp->c_type_set = 1;
        airnav_log_level(3, "Setting client type to 1 (RBCS)\n");
    } else if (strcmp(F_ARCH, "rblc") == 0) {
        tmp->c_type = 2;
        tmp->c_type_set = 1;
        airnav_log_level(3, "Setting client type to 2 (RBLC)\n");
    } else if (strcmp(F_ARCH, "rblc2") == 0) {
        tmp->c_type = 2;
        tmp->c_type_set = 1;
        airnav_log_level(3, "Setting client type to 2 (RBLC2)\n");
    }

    if (!sendPacket(tmp)) {
        airnav_log("Error sending sharing key request.\n");
        return -1;
    }

    // Wait for reply from server
    int reply = waitCmd(2);
    if (reply == 2) {
        airnav_log_level(7, "Got OK from server! Payload on OK Message: %s\n", last_payload);
        return 1;
    } else if (reply == 0) {
        airnav_log_level(7, "Got no response from server\n");
        return 0;
    } else if (reply == 3) {
        airnav_log("Error requesting new sharing key: %s.\n", last_payload);
        return -1;
    } else {
        airnav_log_level(7, "Got another cmd from server :( (%d)\n", reply);
        return -1;
    }

}

/*
 * Connect to airnav server (socket)
 */
int airnav_connect(void) {
    //
    signal(SIGPIPE, sigpipe_handler);
    if (airnav_socket != -1) {
        close(airnav_socket);
    }

    airnav_socket = socket(AF_INET, SOCK_STREAM, 0);

    char *hostname = malloc(strlen(airnav_host) + 1);
    strcpy(hostname, airnav_host);

    char ip[100] = {0};

    if (hostname_to_ip(hostname, ip)) { // Error
        airnav_log_level(2, "Could not resolve hostname....using default IP.\n");
        strcpy(ip, "45.63.1.41"); // Default IP
    }
    airnav_log_level(3, "Host %s resolved as %s\n", hostname, ip);
    free(hostname);

    addr_airnav.sin_family = AF_INET;
    addr_airnav.sin_port = htons(airnav_port);
    inet_pton(AF_INET, ip, &(addr_airnav.sin_addr));

    enable_keepalive(airnav_socket);

    if (connect(airnav_socket, (struct sockaddr *) &addr_airnav, sizeof (addr_airnav)) != -1) {
        /* Success */
        airnav_log("Connection established.\n");
        airnav_log_level(3, "Connected to %s on port %d\n", airnav_host, airnav_port);
        return 1;
    } else {
        airnav_log("Can't connect to AirNav Server. Retry in %d seconds.\n", AIRNAV_MONITOR_SECONDS);
        airnav_log_level(3, "Can't connect to %s on port %d\n", airnav_host, airnav_port);
        airnav_socket = -1;
        return 0;
    }


}

/*
 * Initial communication with AirNavServer
 */
int airnav_initial_com(void) {

    airnav_log_level(7, "Starting initial protocol...\n");
    signal(SIGPIPE, sigpipe_handler);
    int reply = -2;

    ini_getString(&sharing_key, configuration_file, "client", "key", "");

    // Check if sharing-key is valid
    if (getArraySize((char*) sharing_key) > 0 && getArraySize((char*) sharing_key) < 32) {
        airnav_log("Error: invalid sharing-key. Check your key and try again.\n");
        airnav_log("If you don't have a sharing-key, leave field 'key' empty (in rbfeeder.ini) and the feeder will try to auto-generate a new sharing key.\n");
        airnav_com_inited = 0;
        close(airnav_socket);
        airnav_socket = -1;
        return 0;
    }

    if (airnav_connect() != 1) {
        return 0;
    }

    if (getArraySize((char*) sharing_key) == 0) {

        airnav_log("Empty sharing key. We will try to create a new one for you!\n");
        reply = sendKeyRequest();
        if (reply == 1) {
            airnav_log_level(5, "New key generated! This is the key: %s\n", last_payload);
            airnav_log_level(5, "Size of new payload (KEY): %d\n", getArraySize((char*) &last_payload));
            if (getArraySize((char*) &last_payload) == 32) { // Check if is exactly 32 chars on key

                ini_saveGeneric(configuration_file, "client", "key", last_payload);
                sharing_key = malloc(33);
                memcpy(sharing_key, &last_payload, 32);
                sharing_key[32] = '\0';
                airnav_log("Your new key is %s. Please save this key for future use. You will have to know this key to link this receiver to your account in RadarBox24.com. This key is also saved in configuration file (%s)\n", sharing_key, configuration_file);
                airnav_com_inited = 0;
                close(airnav_socket);
                airnav_socket = -1;
                return 1;
            } else {
                airnav_log("Key received from server is invalid.\n");
                airnav_com_inited = 0;
                close(airnav_socket);
                airnav_socket = -1;
                return 0;
            }

        } else if (reply == 0) {
            airnav_log_level(5, "Timeout waiting for new key.\n");
            airnav_com_inited = 0;
            close(airnav_socket);
            airnav_socket = -1;
            return 0;
        } else {
            airnav_log_level(5, "Could not generate new key. Error from server: %s\n", last_payload);
            airnav_com_inited = 0;
            close(airnav_socket);
            airnav_socket = -1;
            return 0;
        }

    } else {

        airnav_log_level(7, "Sending sharing key to server...\n");
        reply = sendKey();
        if (reply == 1) {
            airnav_com_inited = 1;
            airnav_log("Connection with RadarBox24 server OK! Key accepted by server.\n");
            // Request our serial number
            struct p_data *tmp1 = preparePacket();
            tmp1->cmd = 8; // Request our SN
            sendPacket(tmp1);

            // Request date/time
            struct p_data *jon1 = preparePacket();
            jon1->cmd = 14;
            sendPacket(jon1);
            airnav_log_level(3, "DATE/TIME request sent!\n");
            sleep(1);

            // Request update information
            struct p_data *jon = preparePacket();
            char str[200];
            memset(str, 0, 200);
            jon->cmd = 7;
            sprintf(str, "%s|%s", c_version_str, F_ARCH);
            strcpy(jon->payload, str);
            jon->payload_set = 1;
            sendPacket(jon);

            // Send our system version
            sendSystemVersion();
            airnav_log_level(3, "Init OK!!!\n");
            return 1;
        } else {
            airnav_log("Could not start connection. Timeout.\n");
            if (last_cmd == 3) {
                airnav_log("Last server error: %s\n", last_payload);
            }
            airnav_com_inited = 0;
            close(airnav_socket);
            airnav_socket = -1;
            return 0;
        }


    }



    return 1;
}

/*
 * Function to monitor connection with AirNav
 * Server.
 */
void *airnav_monitorConnection(void *arg) {
    MODES_NOTUSED(arg);
    int local_counter = 0;
    int local_counter2 = 0;
    int position_send_time = SEND_POSITION_TIME - 60;
    gps_led = 0;
    error_led = 0;
    int pc_led = 0;
    sleep(1);
    airnav_initial_com();
    struct p_data *tmp = NULL;



    while (!Modes.exit) {


#ifdef RBCSRBLC
        
        getCPUTemp();

#ifdef RBCS
        // Update GPS localization
        if (gps_ok == 1) {
            updateGPSData();

            if (!gps_fixed) {
                if (!gps_led) {
                    gps_led = 1;
                    led_on(LED_GPS);
                } else {
                    gps_led = 0;
                    led_off(LED_GPS);
                }

            } else {
                if (!gps_led) {
                    led_on(LED_GPS);
                    gps_led = 1;
                }
            }
        }
#endif

        // Error led
        if (airnav_com_inited != 1) {

            if (!error_led) {
                error_led = 1;
                led_on(LED_ERROR);
            } else {
                error_led = 0;
                led_off(LED_ERROR);
            }

        } else {
            if (error_led) {
                led_off(LED_ERROR);
                error_led = 0;
            }
        }

        // PC Led
        if (isANRBConnected() == 1) {

            if (pc_led == 0) {
                led_on(LED_PC);
                pc_led = 1;
            }

        } else {
            if (pc_led == 1) {
                led_off(LED_PC);
                pc_led = 0;
            }
        }

#endif

        // Check update interval
        if (local_counter2 >= UPDATE_CHECK_TIME) {
            local_counter2 = 0;

            if (airnav_com_inited == 1) {
                airnav_log_level(2, "Sending update request information...\n");
                struct p_data *jon = preparePacket();
                char str[200];
                memset(str, 0, 200);
                jon->cmd = 7;
                sprintf(str, "%s|%s", c_version_str, F_ARCH);
                strcpy(jon->payload, str);
                jon->payload_set = 1;
                sendPacket(jon);
            }

        } else {
            local_counter2++;
        }

#ifndef RBLC
        // Send position interval
        if (position_send_time >= SEND_POSITION_TIME) {
            position_send_time = 0;



            if (g_lat != 0 && g_lon != 0 && g_alt != -999) {

                if (g_lat != 0) {
                    char slat[30] = {0};
                    sprintf(slat, "%f", g_lat);
                    ini_saveGeneric(configuration_file, "client", "lat", slat);
                }
                if (g_lon != 0) {
                    char slon[30] = {0};
                    sprintf(slon, "%f", g_lon);
                    ini_saveGeneric(configuration_file, "client", "lon", slon);
                }
                if (g_alt != 0) {
                    char salt[30] = {0};
                    sprintf(salt, "%d", g_alt);
                    ini_saveGeneric(configuration_file, "client", "alt", salt);
                }
                char s_used[30] = {0};
                char s_visi[30] = {0};
                sprintf(s_used, "%d", sats_used);
                sprintf(s_visi, "%d", sats_visible);
                ini_saveGeneric(configuration_file, "client", "sat_used", s_used);
                ini_saveGeneric(configuration_file, "client", "sat_visible", s_visi);

                if (airnav_com_inited == 1) {
                    airnav_log_level(2, "Sending position packet...\n");
                    tmp = preparePacket();
                    tmp->cmd = 9;
                    tmp->lat = g_lat;
                    tmp->lon = g_lon;
                    tmp->altitude = g_alt;
                    tmp->position_set = 1;
                    tmp->altitude_set = 1;
                    sendPacket(tmp);
                }
            }
        } else {
            position_send_time++;
        }
#endif

        if (local_counter >= AIRNAV_MONITOR_SECONDS) {
            local_counter = 0;

            if (airnav_com_inited == 0) {
                airnav_log_level(5, "[MONITOR1] Connection not initialized. Trying init protocol.\n");
                close(airnav_socket);
                airnav_socket = -1;
                airnav_initial_com();
            } else {
                airnav_log_level(5, "[MONITOR4] Connection OK. Sending ACK...\n");
                sendAck();
            }

        } else {
            local_counter++;
        }

        sleep(1);
    }

    return NULL;
}

/*
 * Thread to show statistics on screen
 */
void *airnav_statistics(void *arg) {

    MODES_NOTUSED(arg);

    int local_counter = 0;

    while (!Modes.exit) {

        if (local_counter == AIRNV_STATISTICS_INTERVAL) {
            local_counter = 0;
            airnav_log("******** Statistics updated every %d seconds ********\n", AIRNV_STATISTICS_INTERVAL);
            pthread_mutex_lock(&m_packets_counter);
            airnav_log("Packets sent in the last %d seconds: %ld, Total packets sent since startup: %d\n", AIRNV_STATISTICS_INTERVAL, packets_last, packets_total);
            packets_last = 0;
            pthread_mutex_unlock(&m_packets_counter);

#ifdef RBCSRBLC
            if (rf_filter_status == 0) {
                airnav_log("RF Filter: Off (SW1 = %d, SW2 = %d)\n", sunxi_gpio_input(RF_SW1), sunxi_gpio_input(RF_SW2));
            } else {
                airnav_log("RF Filter: On (SW1 = %d, SW2 = %d)\n", sunxi_gpio_input(RF_SW1), sunxi_gpio_input(RF_SW2));
            }
            airnav_log("\n");
#endif
            /*
            pthread_mutex_lock(&m_anrb_list);
            for (int i = 0; i < MAX_ANRB; i++) {
                if (anrbList[i].active == 1) {
                    int tmp_sent = -1;
                    char *abc = xorencrypt((char*) "teste123", (char*) "1234567890");
                    tmp_sent = send(*anrbList[i].socket, abc, strlen(abc), 0);
                    //tmp_sent = send(*anrbList[i].socket, (char*)"§teste123", 8, 0);
                    if (tmp_sent < 0) {
                        airnav_log_level(2, "Error sending data to ANRB.\n");
                    }
                    tmp_sent = send(*anrbList[i].socket, &txend[0], 1, 0);
                    tmp_sent = send(*anrbList[i].socket, &txend[1], 1, 0);

                }
            }
            pthread_mutex_unlock(&m_anrb_list);
             */
        } else {
            local_counter++;
        }
        sleep(1);

    }

    return NULL;

}

/*
 * Tis function get data from ModeS Decoder and prepare
 * to send to AirNAv
 */
void *airnav_prepareData(void *arg) {
    struct aircraft *b = Modes.aircrafts;
    uint64_t now = mstime();
    int send = 0;
    struct packet_list *tmp, *tmp2;

    MODES_NOTUSED(arg);


    struct p_data *acf, *acf2;

    while (!Modes.exit) {

        pthread_mutex_lock(&Modes.data_mutex);
        pthread_mutex_lock(&m_copy);
        pthread_mutex_lock(&m_copy2);
        packet_list_count = 0;

        b = Modes.aircrafts;
        if (b) {

            b = Modes.aircrafts;

            while (b) {
                now = mstime();

                int msgs = b->messages;
                int flags = b->modeACflags;

                if ((((flags & (MODEAC_MSG_FLAG)) == 0) && (msgs > 1))
                        || (((flags & (MODEAC_MSG_MODES_HIT | MODEAC_MSG_MODEA_ONLY)) == MODEAC_MSG_MODEA_ONLY) && (msgs > 4))
                        || (((flags & (MODEAC_MSG_MODES_HIT | MODEAC_MSG_MODEC_OLD)) == 0) && (msgs > 127))
                        ) {

                    acf = preparePacket();
                    acf2 = preparePacket(); // ANRB
                    send = 0;

                    if (b->addr & MODES_NON_ICAO_ADDRESS) {
                        airnav_log_level(5, "Invalid ICAO code.\n");
                    } else {
                        acf->modes_addr = b->addr;
                        acf->modes_addr_set = 1;
                        // ANRB
                        acf2->modes_addr = b->addr;
                        acf2->modes_addr_set = 1;
                        send = 1;
                    }
                    acf->timestp = now;
                    // ANRB
                    acf2->timestp = now;

                    // Check if Callsign updated
                    if (trackDataAge(&b->callsign_valid, now) <= AIRNAV_MAX_ITEM_AGE) {
                        strcpy(acf->callsign, b->callsign);
                        acf->callsign_set = 1;
                        // ANRB
                        strcpy(acf2->callsign, b->callsign);
                        acf2->callsign_set = 1;
                        send = 0;
                    }

                    // Check if Alt updated
                    if (trackDataValid(&b->airground_valid) && b->airground == AG_GROUND) {
                        acf->airborne = 0;
                        acf->airborne_set = 1;
                        // ANRB
                        acf2->airborne = 0;
                        acf2->airborne_set = 1;
                        send = 1;
                    } else if (trackDataValid(&b->altitude_valid)) {
                        if (trackDataAge(&b->altitude_valid, now) <= AIRNAV_MAX_ITEM_AGE) {
                            if (b->altitude > 0) {
                                acf->airborne = 1;
                                acf->airborne_set = 1;
                                // ANRB
                                acf2->airborne = 1;
                                acf2->airborne_set = 1;
                            }
                            acf->altitude = b->altitude;
                            acf->altitude_set = 1;
                            // ANRB
                            acf2->altitude = b->altitude;
                            acf2->altitude_set = 1;
                            send = 1;
                        }
                    }



                    // Position
                    if (trackDataAge(&b->position_valid, now) <= AIRNAV_MAX_ITEM_AGE) {
                        if (trackDataValid(&b->position_valid)) {
                            acf->lat = b->lat;
                            acf->lon = b->lon;
                            acf->position_set = 1;
                            // ANRB
                            acf2->lat = b->lat;
                            acf2->lon = b->lon;
                            acf2->position_set = 1;
                            send = 1;
                        }
                    }


                    // Heading
                    if (trackDataAge(&b->heading_valid, now) <= AIRNAV_MAX_ITEM_AGE) {
                        if (trackDataValid(&b->heading_valid)) {
                            acf->heading = (b->heading / 10);
                            acf->heading_set = 1;
                            // ANRB
                            acf2->heading = (b->heading / 10);
                            acf2->heading_set = 1;
                            send = 1;
                        }
                    }

                    // Check if Speed updated
                    if (trackDataAge(&b->speed_valid, now) <= AIRNAV_MAX_ITEM_AGE) {
                        if (trackDataValid(&b->speed_valid)) {
                            acf->gnd_speed = (b->speed / 10);
                            acf->gnd_speed_set = 1;
                            // ANRB
                            acf2->gnd_speed = (b->speed / 10);
                            acf2->gnd_speed_set = 1;
                            send = 1;
                        }
                    }

                    // Check vertical rate
                    if (trackDataAge(&b->vert_rate_valid, now) <= AIRNAV_MAX_ITEM_AGE) {
                        acf->vert_rate = (b->vert_rate / 10);
                        acf->vert_rate_set = 1;
                        // ANRB
                        acf2->vert_rate = (b->vert_rate / 10);
                        acf2->vert_rate_set = 1;
                        send = 1;
                    }

                    // Check if Squawk updated
                    if (trackDataAge(&b->squawk_valid, now) <= AIRNAV_MAX_ITEM_AGE) {
                        if (trackDataValid(&b->squawk_valid)) {
                            acf->squawk = b->squawk;
                            acf->squawk_set = 1;
                            // ANRB
                            acf2->squawk = b->squawk;
                            acf2->squawk_set = 1;
                            send = 1;
                        }
                    }

                    // Check if IAS updated
                    if (trackDataAge(&b->speed_ias_valid, now) <= AIRNAV_MAX_ITEM_AGE) {
                        acf->ias = (b->speed_ias / 10);
                        acf->ias_set = 1;
                        // ANRB
                        acf2->ias = (b->speed_ias / 10);
                        acf2->ias_set = 1;
                        send = 1;
                    }


                    if (send == 1) {
                        airnav_log_level(12, "[Lat:%8.05f,Lon:%8.05f]Hex:%06x CLS:%s HDG:%d ALT:%d GSD:%d VR:%d SQW:%04x IAS:%d AIRBRN: %d\n", acf->lat, acf->lon, acf->modes_addr, acf->callsign, (acf->heading * 10), acf->altitude, (acf->gnd_speed * 10), (acf->vert_rate * 10), acf->squawk, acf->ias, acf->airborne);
                        packet_cache_count++;
                        acf->cmd = 5;
                        // ANRB
                        acf2->cmd = 5;

                        tmp = malloc(sizeof (struct packet_list));
                        tmp->next = flist;
                        tmp->packet = acf;
                        flist = tmp;


                        // ANRB
                        tmp2 = malloc(sizeof (struct packet_list));
                        tmp2->next = flist2;
                        tmp2->packet = acf2;
                        flist2 = tmp2;

                        send = 0;


                    } else {
                        free(acf);
                        // ANRB
                        free(acf2);
                    }
                }
                // printf("Addr: %06x, Callsign: %-8s, Speed: %3d, Alt: %6d, Squawk: %04x, Lat: %7.03f, Lon: %8.03f, Tout: %2.0f\n", b->addr, b->callsign, b->speed, b->altitude, b->squawk, b->lat, b->lon, ((now - b->seen) / 1000.0));
                b = b->next;
            }


        }
        pthread_mutex_unlock(&m_copy2);
        pthread_mutex_unlock(&m_copy);
        pthread_mutex_unlock(&Modes.data_mutex);
        sleep(AIRNAV_SEND_INTERVAL); // Send every second
    }


    return NULL;
}

/*
 * Send packet data, but only filled fields
 */
int sendPacket(struct p_data *pk) {

    signal(SIGPIPE, sigpipe_handler);
    int data_sent = 0;
    int tmp_sent = 0;

    if (airnav_socket == -1) {
        airnav_log_level(7, "Socket not created!\n");
        free(pk);
        return 0;
    }


    int p_size = 7; // Because Size (2), Flags(4) and CMD(1) are always set, at least
    uint32_t flags = 0;


    // Is ADDR set?
    if (pk->modes_addr_set == 1) {
        set_bit(&flags, 0);
        p_size = p_size + sizeof (int32_t);
    }

    // client version is set?
    if (pk->c_version_set == 1) {
        set_bit(&flags, 1);
        p_size = p_size + sizeof (uint64_t);
    }

    // Key is set?    
    if (pk->c_key_set == 1) {
        set_bit(&flags, 2);
        p_size = p_size + 32;
    }

    // CallSign is set?
    if (pk->callsign_set == 1) {
        set_bit(&flags, 3);
        p_size = p_size + 8;
    }

    // Altitude is set?
    if (pk->altitude_set == 1) {
        set_bit(&flags, 4);
        p_size = p_size + 4;
    }

    // Position is set?
    if (pk->position_set == 1) {
        set_bit(&flags, 5);
        p_size = p_size + 16;
    }

    // Heading is set?
    if (pk->heading_set == 1) {
        set_bit(&flags, 6);
        p_size = p_size + 2;
    }

    // GND_SPD is set?
    if (pk->gnd_speed_set == 1) {
        set_bit(&flags, 7);
        p_size = p_size + 2;
    }

    // IAS is set?
    if (pk->ias_set == 1) {
        set_bit(&flags, 8);
        p_size = p_size + 2;
    }

    // VRATE is set?
    if (pk->vert_rate_set == 1) {
        set_bit(&flags, 9);
        p_size = p_size + 2;
    }

    // SQUAWK is set?
    if (pk->squawk_set == 1) {
        set_bit(&flags, 10);
        p_size = p_size + 2;
    }

    // AIRBORNE is set?
    if (pk->airborne_set == 1) {
        set_bit(&flags, 11);
        p_size = p_size + 1;
    }

    // PAYLOAD is set?
    if (pk->payload_set == 1) {
        set_bit(&flags, 12);
        p_size = p_size + 500;
    }

#ifdef RBCSRBLC
    set_bit(&flags, 13);
#endif

    // C_TYPE is set?
    if (pk->c_type_set == 1) {
        set_bit(&flags, 14);
        p_size = p_size + 1;
    }




    int p_size_2;
    p_size_2 = p_size - 2;
    char *saida = malloc(p_size_2);
    memset(saida, 0, p_size_2);

    int32_t tmp_htonl = 0;
    short tmp_htons = 0;
    airnav_log_level(7, "Total data size without basic elements: %d\n", p_size_2);

    int idxc = 0;
    // Build array with all data 

    // Flags
    tmp_htonl = htonl(flags);
    memcpy(&saida[idxc], &tmp_htonl, sizeof (int32_t));
    idxc = idxc + sizeof (uint32_t);

    // CMD
    memcpy(&saida[idxc], &pk->cmd, 1);
    idxc = idxc + sizeof (char);


    // Is ADDR set?
    if (pk->modes_addr_set == 1) {
        tmp_htonl = htonl(pk->modes_addr);
        memcpy(&saida[idxc], &tmp_htonl, sizeof (int32_t));
        airnav_log_level(12, "ADDR: %d (%d)\n", pk->modes_addr, tmp_htonl);
        idxc = idxc + sizeof (int32_t);
    }

    // client version is set?
    if (pk->c_version_set == 1) {
        uint32_t c_v_ints[sizeof pk->c_version / sizeof (uint32_t)];
        memcpy(&c_v_ints, &pk->c_version, sizeof (uint64_t));
        c_v_ints[0] = htonl(c_v_ints[0]);
        c_v_ints[1] = htonl(c_v_ints[1]);
        memcpy(&saida[idxc], &c_v_ints[0], sizeof (uint32_t));
        idxc = idxc + sizeof (uint32_t);
        memcpy(&saida[idxc], &c_v_ints[1], sizeof (uint32_t));
        idxc = idxc + sizeof (uint32_t);
    }

    // Key is set?    
    if (pk->c_key_set == 1) {
        memcpy(&saida[idxc], &pk->c_key, 32);
        idxc = idxc + 32;
    }

    // CallSign is set?
    if (pk->callsign_set == 1) {
        memcpy(&saida[idxc], &pk->callsign, 8);
        airnav_log_level(12, "CALLSIGN: %s\n", pk->callsign);
        idxc = idxc + 8;
    }

    // Altitude is set?
    if (pk->altitude_set == 1) {
        tmp_htonl = htonl(pk->altitude);
        memcpy(&saida[idxc], &tmp_htonl, sizeof (int32_t));
        airnav_log_level(12, "ALTITUDE: %d (%d)\n", pk->altitude, tmp_htonl);
        idxc = idxc + sizeof (int32_t);
    }

    // Position is set?
    if (pk->position_set == 1) {
        uint32_t c_l_ints[sizeof pk->lat / sizeof (uint32_t)];
        // Latitude
        memcpy(&c_l_ints, &pk->lat, sizeof (double));
        c_l_ints[0] = htonl(c_l_ints[0]);
        c_l_ints[1] = htonl(c_l_ints[1]);
        memcpy(&saida[idxc], &c_l_ints[0], sizeof (uint32_t));
        idxc = idxc + sizeof (uint32_t);
        memcpy(&saida[idxc], &c_l_ints[1], sizeof (uint32_t));
        idxc = idxc + sizeof (uint32_t);
        // Longitude
        memcpy(&c_l_ints, &pk->lon, sizeof (uint64_t));
        c_l_ints[0] = htonl(c_l_ints[0]);
        c_l_ints[1] = htonl(c_l_ints[1]);
        memcpy(&saida[idxc], &c_l_ints[0], sizeof (uint32_t));
        idxc = idxc + sizeof (uint32_t);
        memcpy(&saida[idxc], &c_l_ints[1], sizeof (uint32_t));
        idxc = idxc + sizeof (uint32_t);

    }

    // Heading is set?
    if (pk->heading_set == 1) {
        tmp_htons = htons(pk->heading);
        memcpy(&saida[idxc], &tmp_htons, sizeof (short));
        idxc = idxc + sizeof (short);
    }

    // GND_SPD is set?
    if (pk->gnd_speed_set == 1) {
        tmp_htons = htons(pk->gnd_speed);
        memcpy(&saida[idxc], &tmp_htons, sizeof (short));
        idxc = idxc + sizeof (short);
    }

    // IAS is set?
    if (pk->ias_set == 1) {
        tmp_htons = htons(pk->ias);
        memcpy(&saida[idxc], &tmp_htons, sizeof (short));
        idxc = idxc + sizeof (short);
    }

    // VRATE is set?
    if (pk->vert_rate_set == 1) {
        tmp_htons = htons(pk->vert_rate);
        memcpy(&saida[idxc], &tmp_htons, sizeof (short));
        idxc = idxc + sizeof (short);
    }

    // SQUAWK is set?
    if (pk->squawk_set == 1) {
        tmp_htons = htons(pk->squawk);
        memcpy(&saida[idxc], &tmp_htons, sizeof (short));
        idxc = idxc + sizeof (short);
    }

    // AIRBORNE is set?
    if (pk->airborne_set == 1) {
        memcpy(&saida[idxc], &pk->airborne, 1);
        idxc = idxc + 1;
    }

    // PAYLOAD is set?
    if (pk->payload_set == 1) {
        memcpy(&saida[idxc], &pk->payload, 500);
        idxc = idxc + 500;
    }

    // C_TYPE is set?
    if (pk->c_type_set == 1) {
        memcpy(&saida[idxc], &pk->c_type, 1);
        idxc = idxc + 1;
    }


    // start sending bytes

    // Send size data
    airnav_log_level(7, "Sending SIZE field with value = %d\n", p_size);
    p_size = htons(p_size);
    tmp_sent = send(airnav_socket, &p_size, sizeof (short), 0);
    if (tmp_sent < 0) {
        airnav_log_level(7, "Error sending packet data (size).\n");
        airnav_com_inited = 0;
        free(pk);
        free(saida);
        closeCon();
        return 0;
    } else {
        data_sent = data_sent + tmp_sent;
    }

    s20_crypt(key, S20_KEYLEN_128, nonce, 0, saida, p_size_2);
    airnav_log_level(7, "Tamanho final da string: %d\n", p_size_2);


    for (int i = 0; i < p_size_2; i++) {
        //airnav_log_level(7,"Enviando byte %d...\n",i);
        tmp_sent = send(airnav_socket, &saida[i], 1, 0);
        if (tmp_sent < 0) {
            airnav_log_level(7, "Error sending packet data (payload byte %d).\n", i);
            airnav_com_inited = 0;
            free(pk);
            free(saida);
            closeCon();
            return 0;
        } else {
            data_sent = data_sent + tmp_sent;
        }
    }

    // Send end of TX 1
    tmp_sent = send(airnav_socket, &txend[0], 1, 0);
    if (tmp_sent < 0) {
        airnav_log_level(7, "Error sending packet data (end of line 1).\n");
        airnav_com_inited = 0;
        free(pk);
        free(saida);
        closeCon();
        return 0;
    } else {
        data_sent = data_sent + tmp_sent;
    }

    // Send end of TX 1
    tmp_sent = send(airnav_socket, &txend[1], 1, 0);
    if (tmp_sent < 0) {
        airnav_log_level(7, "Error sending packet data (end of line 2).\n");
        airnav_com_inited = 0;
        free(pk);
        free(saida);
        closeCon();
        return 0;
    } else {
        data_sent = data_sent + tmp_sent;
    }


    free(pk);
    free(saida);
    return 1;
}

/*
 * Initialize a new instance of packet
 */
struct p_data *preparePacket(void) {
    struct p_data *pakg = malloc(sizeof (struct p_data));

    pakg->cmd = 0;
    pakg->c_version = 0;
    pakg->c_type = 0;
    pakg->c_type_set = 0;
    memset(pakg->c_key, 0, 33);
    pakg->c_key_set = 0;
    pakg->c_version_set = 0;
    pakg->modes_addr = 0;
    pakg->modes_addr_set = 0;
    memset(pakg->callsign, 0, 9);
    pakg->callsign_set = 0;
    pakg->altitude = 0;
    pakg->altitude_set = 0;
    pakg->lat = 0;
    pakg->lon = 0;
    pakg->position_set = 0;
    pakg->heading = 0;
    pakg->heading_set = 0;
    pakg->gnd_speed = 0;
    pakg->gnd_speed_set = 0;
    pakg->ias = 0;
    pakg->ias_set = 0;
    pakg->vert_rate = 0;
    pakg->vert_rate_set = 0;
    pakg->squawk = 0;
    pakg->squawk_set = 0;
    pakg->airborne = 0;
    pakg->airborne_set = 0;
    memset(pakg->payload, 0, 501); // Payload for error and messages
    pakg->payload_set = 0;
    memset(pakg->c_ip, 0, 20);



    if (strcmp(F_ARCH, "rbcs") == 0) {
        pakg->c_type = 1;
        //airnav_log_level(3,"Setting client type to 1 (RBCS)\n");
    } else if (strcmp(F_ARCH, "rblc") == 0) {
        pakg->c_type = 2;
        //airnav_log_level(3,"Setting client type to 2 (RBLC)\n");
    } else if (strcmp(F_ARCH, "rblc2") == 0) {
        pakg->c_type = 2;
        pakg->c_type_set = 1;
    } else if (strcmp(F_ARCH, "raspberry") == 0) {
        pakg->c_type = 0;
    } else {
        //airnav_log_level(3,"Setting client type to %d \n",pakg->c_type);
    }


    return pakg;
}

/*
 *  Thread that connect to external source, read
 * data and send to local dump1090 using TCP connection
 */
void *airnav_extSourceProccess(void *arg) {
    MODES_NOTUSED(arg);

    int sock_ext, sock_int;
    struct sockaddr_in addr_ext, addr_int;
    int * p_int, * p_int1;
    int bytecount;

START_EXT:
    sock_ext = socket(AF_INET, SOCK_STREAM, 0);
    sock_int = socket(AF_INET, SOCK_STREAM, 0);


    addr_ext.sin_family = AF_INET;
    addr_ext.sin_port = htons(external_port);
    inet_pton(AF_INET, external_host, &(addr_ext.sin_addr));

    addr_int.sin_family = AF_INET;
    addr_int.sin_port = htons(atoi(net_mode == 1 ? local_input_port : beast_in_port));
    inet_pton(AF_INET, "127.0.0.1", &(addr_int.sin_addr));


    p_int = (int*) malloc(sizeof (int));
    *p_int = 1;
    if ((setsockopt(sock_ext, SOL_SOCKET, SO_REUSEADDR, (char*) p_int, sizeof (int)) == -1) ||
            (setsockopt(sock_ext, SOL_SOCKET, SO_KEEPALIVE, (char*) p_int, sizeof (int)) == -1)) {
        airnav_log("Error setting options %d\n", errno);
        free(p_int);
    }
    free(p_int);


    p_int1 = (int*) malloc(sizeof (int));
    *p_int1 = 1;

    if ((setsockopt(sock_int, SOL_SOCKET, SO_REUSEADDR, (char*) p_int1, sizeof (int)) == -1) ||
            (setsockopt(sock_int, SOL_SOCKET, SO_KEEPALIVE, (char*) p_int1, sizeof (int)) == -1)) {
        airnav_log("Error setting options %d\n", errno);
        free(p_int1);
    }
    free(p_int1);


    sleep(3);

    int res = -1;
    res = connect(sock_ext, (struct sockaddr *) &addr_ext, sizeof (addr_ext));
    while (res != 0 && Modes.exit != 1) {
        airnav_log("Can't connect to external source (%s:%d). Waiting 5 second...\n", external_host, external_port);
        sleep(5);
        res = connect(sock_ext, (struct sockaddr *) &addr_ext, sizeof (addr_ext));
    }


    res = connect(sock_int, (struct sockaddr *) &addr_int, sizeof (addr_int));
    while (res != 0 && Modes.exit != 1) {
        airnav_log("[1]Can't connect to internal destination. Waiting 5 second...\n");
        airnav_log_level(2, "Internal port: %s\n", beast_in_port);
        sleep(5);
        res = connect(sock_int, (struct sockaddr *) &addr_int, sizeof (addr_int));
    }


    //int counter = 0;

    int s_write = -1;
    int buffer_size = 4096;
    char tmp_byte[buffer_size];
    int r = 0;

    while (!Modes.exit) {

        r = recv(sock_ext, &tmp_byte, buffer_size, MSG_DONTWAIT);
        if (r > 0) {
            s_write = write(sock_int, &tmp_byte, r);
            r = 0;
            memset(&tmp_byte, 0, buffer_size);
            usleep(80000);
        } else if (r == 0) {
            close(sock_ext);
            close(sock_int);
            sleep(5);
            goto START_EXT;

        } else {
            usleep(70000);
        }


    }

    return NULL;

}

/*
 * Load configuration from ini file
 */
void airnav_loadConfig(int argc, char **argv) {

#ifdef RBCSRBLC
    airnav_log("Compiled with RBCSRBLC option\n");
#endif
#ifdef RBCS
    airnav_log("Compiled with RBCS option\n");
#endif
#ifdef RBLC
    airnav_log("Compiled with RBLC option\n");
#endif

    airnav_log("Compiled with arch=%s\n", F_ARCH);

    int i = 0;
    int j;
    char tmp_str[7];
    Modes.quiet = 1;
    max_cpu_temp = 0;
    max_pmu_temp = 0;


    // Get MAC ADDress
    int sm;
    struct ifreq ifr;
    sm = socket(AF_INET, SOCK_DGRAM, 0);
    strcpy(ifr.ifr_name, "eth0");
    ioctl(sm, SIOCGIFHWADDR, &ifr);
    sprintf(mac_a, "%02X.%02X.%02X.%02X.%02X.%02X", ((char*) ifr.ifr_hwaddr.sa_data)[0], ((char*) ifr.ifr_hwaddr.sa_data)[1], ((char*) ifr.ifr_hwaddr.sa_data)[2], ((char*) ifr.ifr_hwaddr.sa_data)[3], ((char*) ifr.ifr_hwaddr.sa_data)[4], ((char*) ifr.ifr_hwaddr.sa_data)[5]);
    close(sm);

    start_datetime[0] = '\0';
    // application start date/time
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    sprintf(start_datetime, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);


    // Now load version into variables and display initial msg
    // Set version String
    int dd, mo, hh, mm, ss, yy;
    char *p, *q;
    char buf[512], buf1[512], buf2[512];
    sprintf((char *) buf, "%s", __DATE__);
    sscanf((char *) buf, "%s %d %d", buf2, &dd, &yy);
    sprintf((char *) buf, "%s", __TIME__);
    sscanf((char *) buf, "%d:%d:%d", &hh, &mm, &ss);
    q = "JanFebMarAprMayJunJulAugSepOctNovDec";
    p = strstr(q, buf2);
    if (p) {
        mo = ((p - q) / 3) + 1;
    } else {
        mo = 0;
    }
    sprintf((char *) c_version_str, "%04d%02d%02d%02d%02d%02d", yy, mo, dd, hh, mm, ss);
    // Enf of Version

    char* endptr;
    endptr = "0123456789";
    c_version_int = strtoimax(c_version_str, &endptr, 10);

    device_n = -1;

    net_mode = 0;
    configuration_file = malloc(strlen(AIRNAV_INIFILE) + 1);
    strcpy(configuration_file, AIRNAV_INIFILE);

    short device_n_changed = 0;
    short show_key = 0;
    short define_key = 0;
    short network_mode = 0;
    short network_mode_changed = 0;
    short protocol = 0;
    short protocol_changed = 0;
    char *network_host;
    short network_host_changed = 0;
    int network_port = 0;
    short network_port_changed = 0;
    short no_start = 0;

    // Print version information
    if (argc > 1) {
        for (j = 1; j < argc; j++) {

            if (!strcmp(argv[j], "--version") || !strcmp(argv[j], "-v")) {
                printf("RBFeeder v%s (build %" PRIu64 ")\n", AIRNAV_VER_PRINT, c_version_int);
                exit(EXIT_SUCCESS);
            } else if (!strcmp(argv[j], "--version2") || !strcmp(argv[j], "-v2")) {
                printf("RBFeeder v%s (build %" PRIu64 " Arch: %s)\n", AIRNAV_VER_PRINT, c_version_int, F_ARCH);
                exit(EXIT_SUCCESS);
            } else if (!strcmp(argv[j], "--config") || !strcmp(argv[j], "-c")) {
                if (argc - 1 > j && argv[j + 1][0] != '-') {
                    configuration_file = strdup(argv[++j]);
                } else {
                    airnav_log("Invalid argument for configuration file (--config).\n");
                    exit(EXIT_FAILURE);
                }
            } else if (!strcmp(argv[j], "--device") || !strcmp(argv[j], "-d")) {
                if (argc - 1 > j && argv[j + 1][0] != '-') {
                    Modes.dev_name = strdup(argv[++j]);
                    device_n = atoi(Modes.dev_name);
                    device_n_changed = 1;
                } else {
                    airnav_log("Invalid argument for device (--device).\n");
                    exit(EXIT_FAILURE);
                }
            } else if (!strcmp(argv[j], "--help") || !strcmp(argv[j], "-h")) {
                airnav_showHelp();
                exit(EXIT_SUCCESS);
            } else if (!strcmp(argv[j], "--interactive") || !strcmp(argv[j], "-it")) {
                Modes.interactive = Modes.throttle = 1;
            } else if (!strcmp(argv[j], "--showkey") || !strcmp(argv[j], "-sw")) {
                show_key = 1;
            } else if (!strcmp(argv[j], "--setkey") || !strcmp(argv[j], "-sk")) {
                if (argc - 1 > j && argv[j + 1][0] != '-') {
                    char *tmp_key;
                    tmp_key = strdup(argv[++j]);
                    if (strlen(tmp_key) != 32) {
                        airnav_log("Invalid sharing key. Sharing key must have exactly 32 chars.\n");
                        exit(EXIT_FAILURE);
                    } else {
                        sharing_key = strdup(tmp_key);
                        define_key = 1;
                    }
                } else {
                    airnav_log("Invalid argument for sharing key.\n");
                    exit(EXIT_FAILURE);
                }

            } else if (!strcmp(argv[j], "--set-network-mode") || !strcmp(argv[j], "-snm")) {
                if (argc - 1 > j && argv[j + 1][0] != '-') {

                    if (!strcmp(argv[j + 1], "1") || !strcmp(argv[j + 1], "on")) {
                        network_mode = 1;
                        network_mode_changed = 1;
                        ++j;
                    } else if (!strcmp(argv[j + 1], "0") || !strcmp(argv[j + 1], "off")) {
                        network_mode = 0;
                        network_mode_changed = 1;
                        ++j;
                    } else {
                        airnav_log("Invalid argument for network mode.\n");
                        exit(EXIT_FAILURE);
                    }

                } else {
                    airnav_log("Invalid argument for network mode.\n");
                    exit(EXIT_FAILURE);
                }
            } else if (!strcmp(argv[j], "--set-network-protocol") || !strcmp(argv[j], "-snp")) {
                if (argc - 1 > j && argv[j + 1][0] != '-') {

                    if (!strcmp(argv[j + 1], "beast")) {
                        protocol = 1;
                        protocol_changed = 1;
                        ++j;
                    } else if (!strcmp(argv[j + 1], "raw")) {
                        protocol = 2;
                        protocol_changed = 1;
                        ++j;
                    } else {
                        airnav_log("Invalid argument for network protocol.\n");
                        exit(EXIT_FAILURE);
                    }

                } else {
                    airnav_log("Invalid argument for network protocol.\n");
                    exit(EXIT_FAILURE);
                }
            } else if (!strcmp(argv[j], "--set-network-host") || !strcmp(argv[j], "-snh")) {
                if (argc - 1 > j && argv[j + 1][0] != '-') {

                    network_host = strdup(argv[++j]);
                    network_host_changed = 1;

                } else {
                    airnav_log("Invalid argument for network host.\n");
                    exit(EXIT_FAILURE);
                }
            } else if (!strcmp(argv[j], "--set-network-port")) {
                if (argc - 1 > j && argv[j + 1][0] != '-') {

                    network_port = atoi(argv[++j]);
                    network_port_changed = 1;

                } else {
                    airnav_log("Invalid argument for network port.\n");
                    exit(EXIT_FAILURE);
                }
            } else if (!strcmp(argv[j], "--interactive")) {
                Modes.interactive = Modes.throttle = 1;
            } else if (!strcmp(argv[j], "--no-start")) {
                no_start = 1;
            }

        }

    }

    if (access(configuration_file, F_OK) != -1) {
        airnav_log_level(5, "Configuration file exist and is valid\n");
    } else {
        airnav_log("Configuration file (%s) doesn't exist.\n", configuration_file);
        exit(EXIT_FAILURE);
    }

    if (device_n_changed == 1) {
        ini_saveGeneric(configuration_file, "client", "dump_device", Modes.dev_name);
    } else {
        device_n = ini_getInteger(configuration_file, "client", "dump_device", -1);
    }
    // If device number is set (>-1), set in dump too
    if (device_n > -1) {
        Modes.dev_name = malloc(5);
        sprintf(Modes.dev_name, "%d", device_n);
        airnav_log_level(5, "Device number: %s\n", Modes.dev_name);
    }


    debug_level = ini_getInteger(configuration_file, "client", "debug_level", 0);
    log_file = NULL;
    ini_getString(&log_file, configuration_file, "client", "log_file", NULL);


    if (define_key == 1) {
        ini_saveGeneric(configuration_file, "client", "key", sharing_key);
    } else {
        ini_getString(&sharing_key, configuration_file, "client", "key", "");
    }
    airnav_log_level(5, "Key carregada: %s\n", sharing_key);

    // Is network mode has changed using parameters, save to ini
    if (network_mode_changed == 1) {
        if (network_mode == 1) {
            ini_saveGeneric(configuration_file, "client", "network_mode", "true");
        } else {
            ini_saveGeneric(configuration_file, "client", "network_mode", "false");
        }
    }

    // Is network protocol has changed using parameters, save to ini
    if (protocol_changed == 1) {
        if (protocol == 1) {
            ini_saveGeneric(configuration_file, "network", "mode", "beast");
        } else {
            ini_saveGeneric(configuration_file, "network", "mode", "raw");
        }
    }

    // If network host has changed
    if (network_host_changed == 1) {
        ini_saveGeneric(configuration_file, "network", "external_host", network_host);
    }

    // If network port has changed
    if (network_port_changed == 1) {
        char *t_port = calloc(1, 20);
        sprintf(t_port, "%d", network_port);
        ini_saveGeneric(configuration_file, "network", "external_port", t_port);
    }


    // This must be the last item
    if (show_key == 1) {

        if (strlen(sharing_key) == 32) {
            printf("\nSharing key: %s\n", sharing_key);
            printf("You can link this sharing key to your account at http://www.radarbox24.com\n");
            printf("Configuration file: %s\n\n", configuration_file);
            exit(EXIT_SUCCESS);
        } else {
            printf("\nYour sharing key is not set or is not valid. If is not set, a new sharing key will be create on first connection.\n");
            printf("Configuration file: %s\n\n", configuration_file);
            exit(EXIT_SUCCESS);
        }


    }


    if (no_start == 1) {
        exit(EXIT_SUCCESS);
    }

    
    #ifdef DEBUG_RELEASE
        ini_getString(&airnav_host, configuration_file, "server", "a_host", "rpi-beta.rb24.com");
    #else
        ini_getString(&airnav_host, configuration_file, "server", "a_host", "rpi.rb24.com");
    #endif
    
    airnav_port = ini_getInteger(configuration_file, "server", "a_port", 33755);
    airnav_log_level(2, "Servidor de conexao: %s\n", airnav_host);
    airnav_log_level(2, "Porta de conexao: %d\n", airnav_port);

    // Port for data input in beast format (MLAT Return)
    ini_getString(&beast_in_port, configuration_file, "network", "beast_input_port", "32004");

    /*
     * Teste if client is setup for network data
     * or for local RTL data.
     */

    if (ini_getBoolean(configuration_file, "client", "network_mode", 0)) {
        char *external_host_name = NULL;
        Modes.net = 1;
        Modes.net_only = 1;
        airnav_log_level(3, "Network mode selected from ini\n");
        net_mode = 1;

        ini_getString(&local_input_port, configuration_file, "network", "intern_port", "32008");

        // Load remote host name from ini file
        ini_getString(&external_host_name, configuration_file, "network", "external_host", NULL);
        if (external_host_name == NULL) {

            airnav_log("When 'network_mode' is enabled, you must specify one remote host for connection using\n");
            airnav_log("'external_host' parameter in [network] section of configuration file.\n");
            airnav_log("If it's your first time running this program, please check configuration file and setup\n");
            airnav_log("basic configuration.\n");
            exit(EXIT_FAILURE);
        }

        // Resolve host name
        external_host = (char *) malloc(100 * sizeof (char));
        if (hostname_to_ip(external_host_name, external_host) != 0) {
            airnav_log("Could not resolve host name specified in 'external_host'.\n");
            exit(EXIT_FAILURE);
        }

        // Now we get external port for connection
        external_port = ini_getInteger(configuration_file, "network", "external_port", 0);
        if (external_port == 0) {
            airnav_log("When 'network_mode' is enabled, you must specify one remote port for connection using\n");
            airnav_log("'external_port' parameter in [network] section of configuration file.\n");
            airnav_log("If it's your first time running this program, please check configuration file and setup\n");
            airnav_log("basic configuration.\n");
            exit(EXIT_FAILURE);
        }


        char *network_mode = NULL;
        ini_getString(&network_mode, configuration_file, "network", "mode", NULL);
        // Let's define the network mode (RAW or BEAST)
        if (network_mode == NULL) {
            airnav_log("Unknow network mode (%s). Only RAW and BEAST are supported at this time.\n", tmp_str);
            airnav_log("If it's your first time running this program, please check configuration file and setup\n");
            airnav_log("basic configuration.\n");
            exit(EXIT_FAILURE);
        }


        if (strstr(network_mode, "raw") != NULL) { // RAW mode
            net_mode = 1;
            free(Modes.net_input_raw_ports);
            Modes.net_input_raw_ports = strdup(local_input_port);
        } else if (strstr(network_mode, "beast") != NULL) { // beast mode
            net_mode = 2;
            free(Modes.net_input_beast_ports);
            Modes.net_input_beast_ports = strdup(beast_in_port);
        } else {
            airnav_log("Unknow network mode (%s). Only RAW and BEAST are supported at this time.\n", network_mode);
            airnav_log("If it's your first time running this program, please check configuration file and setup\n");
            airnav_log("basic configuration.\n");
            exit(EXIT_FAILURE);
        }


    } else {
        Modes.net = 1;
        Modes.net_only = 0;
        net_mode = 0;
    }


    airnav_log("Starting RBFeeder Version %s (build %" PRIu64 ")\n", AIRNAV_VER_PRINT, c_version_int);
    airnav_log("Using configuration file: %s\n", configuration_file);
    if (net_mode > 0) {
        airnav_log("Network-mode enabled.\n");
        airnav_log("\t\tRemote host to fetch data: %s\n", external_host);
        airnav_log("\t\tRemote port: %d\n", external_port);
        airnav_log("\t\tRemote protocol: %s\n", (net_mode == 1 ? "RAW" : "BEAST"));
    } else {
        airnav_log("Network-mode disabled. Using local dongle.\n");
        if (device_n > -1) {
            airnav_log("\tDevice selected in configuration file: %d\n", device_n);
        }
    }


    // configs

    g_lat = ini_getDouble(configuration_file, "client", "lat", 0);
    g_lon = ini_getDouble(configuration_file, "client", "lon", 0);
    g_alt = ini_getInteger(configuration_file, "client", "alt", -999);
    ini_getString(&sn, configuration_file, "client", "sn", NULL);

    double dump_gain = ini_getDouble(configuration_file, "client", "dump_gain", MODES_AUTO_GAIN);
    if (dump_gain != MODES_AUTO_GAIN) {
        Modes.gain = (int) ((double) dump_gain * (double) 10);
    } else {
        Modes.gain = MODES_AUTO_GAIN;
    }

    Modes.enable_agc = ini_getBoolean(configuration_file, "client", "dump_agc", 0);
    Modes.nfix_crc = ini_getBoolean(configuration_file, "client", "dump_fix", 1);
    Modes.mode_ac = ini_getBoolean(configuration_file, "client", "dump_mode_ac", 0);
#ifdef RBCSRBLC
    Modes.dc_filter = 0;
#else
    Modes.dc_filter = ini_getBoolean(configuration_file, "client", "dump_dc_filter", 1);
#endif
    Modes.fUserLat = ini_getDouble(configuration_file, "client", "lat", 0.0);
    Modes.fUserLon = ini_getDouble(configuration_file, "client", "lon", 0.0);
    Modes.check_crc = ini_getBoolean(configuration_file, "client", "dump_check_crc", 1);
    Modes.ppm_error = ini_getInteger(configuration_file, "client", "dump_ppm_error", 0);
    ini_getString(&pidfile, configuration_file, "client", "pid", "/var/run/rbfeeder.pid");

    if (ini_hasSection(configuration_file, "vhf") == 1) {
        // VHF
        // Load VHF configs
        ini_getString(&vhf_config_file, configuration_file, "vhf", "config_file", "/etc/rtl-airband-rb.conf");
        ini_getString(&ice_host, configuration_file, "vhf", "icecast_host", "airnavsystems.com");
        ini_getString(&ice_user, configuration_file, "vhf", "icecast_user", "source");
        ini_getString(&ice_pwd, configuration_file, "vhf", "icecast_pwd", "hackme");
        ice_port = ini_getInteger(configuration_file, "vhf", "icecast_port", 8000);
        ini_getString(&vhf_mode, configuration_file, "vhf", "mode", "scan");
        ini_getString(&vhf_freqs, configuration_file, "vhf", "freqs", "118000000");

        if (sn == NULL) {
            ini_getString(&ice_mountpoint, configuration_file, "vhf", "mountpoint", "010101");
        } else {
            ini_getString(&ice_mountpoint, configuration_file, "vhf", "mountpoint", sn);
        }

        vhf_device = ini_getInteger(configuration_file, "vhf", "device", 1);
        vhf_gain = ini_getDouble(configuration_file, "vhf", "gain", 42);
        vhf_squelch = ini_getInteger(configuration_file, "vhf", "squelch", -1);
        vhf_correction = ini_getInteger(configuration_file, "vhf", "correction", 0);
        vhf_afc = ini_getInteger(configuration_file, "vhf", "afc", 0);
        autostart_vhf = ini_getBoolean(configuration_file, "vhf", "autostart_vhf", 0);

        ini_getString(&vhf_pidfile, configuration_file, "vhf", "pid", NULL);
        if (vhf_pidfile == NULL) {
            ini_getString(&vhf_pidfile, configuration_file, "vhf", "pid", "/run/rtl_airband.pid");
        }

        ini_getString(&vhf_cmd, configuration_file, "vhf", "vhf_cmd", NULL);
        if (vhf_cmd == NULL) { // If is not set by user

            // Check if rtl-aorband-rb is installed
            if (file_exist("/usr/bin/rtl_airband") && file_exist("/lib/systemd/system/rtl-airband-rb.service")) {
                ini_getString(&vhf_cmd, configuration_file, "vhf", "vhf_cmd", "systemctl start rtl-airband-rb.service");
            }

        }

        // airnav_log("Ganho VHF: %d\n",vhf_gain);
        saveVhfConfig();
    }

    // MLAT
    ini_getString(&mlat_cmd, configuration_file, "mlat", "mlat_cmd", NULL);
    if (mlat_cmd == NULL) {

        if (file_exist("/usr/bin/mlat-client")) {
            ini_getString(&mlat_cmd, configuration_file, "mlat", "mlat_cmd", "/usr/bin/mlat-client");
        }

    }

    ini_getString(&mlat_server, configuration_file, "mlat", "server", DEFAULT_MLAT_SERVER);
    ini_getString(&mlat_pidfile, configuration_file, "mlat", "pid", NULL);
    if (mlat_pidfile == NULL) {

        if (file_exist("/usr/bin/mlat-client") && file_exist("/etc/default/mlat-client-config-rb")) {
            ini_getString(&mlat_pidfile, configuration_file, "mlat", "pid", "/run/mlat-client-config-rb.pid");
        }

    }
    
    
    autostart_mlat = ini_getBoolean(configuration_file, "mlat", "autostart_mlat", 0);
    
    ini_getString(&mlat_config, configuration_file, "mlat", "config", NULL);
    airnav_log_level(3,"MLAT Configuration file: %s\n",mlat_config);
    if (mlat_config == NULL) {
        
        if (file_exist("/etc/default/mlat-client-config-rb")) {
            ini_getString(&mlat_config, configuration_file, "mlat", "config", "/etc/default/mlat-client-config-rb");
            airnav_log_level(3,"MLAT Configuration file(2): %s\n",mlat_config);
        }
    }

    // ACARS
    ini_getString(&acars_pidfile, configuration_file, "acars", "pid", NULL);
    ini_getString(&acars_cmd, configuration_file, "acars", "acars_cmd", NULL);

    ini_getString(&acars_server, configuration_file, "acars", "server", "airnavsystems.com:9743");
    acars_device = ini_getInteger(configuration_file, "acars", "device", 1);
    ini_getString(&acars_freqs, configuration_file, "acars", "freqs", "131.550");
    autostart_acars = ini_getBoolean(configuration_file, "acars", "autostart_acars", 0);

    anrb_port = ini_getInteger(configuration_file, "client", "anrb_port", 32088);

    ini_getString(&xorkey, configuration_file, "client", "xorkey", DEFAULT_XOR_KEY);

    // Always enable net mode.

    Modes.quiet = ini_getBoolean(configuration_file, "client", "dump_quiet", 1);
    int closed = ini_getBoolean(configuration_file, "client", "dump_closed", 1);

    // If we should open or not network mode
    if (closed) {
        free(Modes.net_bind_address);
        Modes.net_bind_address = strdup("127.0.0.1");
    }

    // Port for data output in beast format
    ini_getString(&beast_out_port, configuration_file, "network", "beast_output_port", "32457");
    ini_getString(&raw_out_port, configuration_file, "network", "raw_output_port", "32458");
    ini_getString(&sbs_out_port, configuration_file, "network", "sbs_output_port", "32459");

    // JSON output to shared-mem dir
    if ((strcmp(F_ARCH, "rbcs") == 0) || (strcmp(F_ARCH, "rblc") == 0) || (strcmp(F_ARCH, "rblc2") == 0)) {
        ini_getString(&Modes.json_dir, configuration_file, "client", "json_dir", "/run/shm/");
    } else if ((strcmp(F_ARCH, "raspberry") == 0)) {
        ini_getString(&Modes.json_dir, configuration_file, "client", "json_dir", "/dev/shm/");
    }


    Modes.net = 1;
    Modes.mlat = 1;

    // Enable input for MLAT returning from server

    
    // Always enable input-port
        free(Modes.net_input_beast_ports);
        Modes.net_input_beast_ports = strdup(beast_in_port);


    // n RBCS, always listen on this port for MLAT client 
    Modes.net_output_beast_ports = strdup(beast_out_port);
    free(Modes.net_output_raw_ports);
    Modes.net_output_raw_ports = strdup(raw_out_port);

    free(Modes.net_output_sbs_ports);
    Modes.net_output_sbs_ports = strdup(sbs_out_port);

#ifdef RBCS
    // VHF pipe
    ini_getString(&vhf_pipe, configuration_file, "vhf", "pipe", NULL);
    saveVhfConfig();
#endif

    rf_filter_status = ini_getBoolean(configuration_file, "client", "rf_filter", 0);

    // HTTP Port for statics
#ifdef ENABLE_WEBSERVER
    char *tmp_http = NULL;
    ini_getString(&tmp_http, configuration_file, "client", "http_port", "32084");
    Modes.net_http_ports = strdup(tmp_http);
    if (tmp_http != NULL) {
        free(tmp_http);
    }
#endif



}

/*
 * Display help message in console
 */
void airnav_showHelp(void) {
    fprintf(stderr, "Starting RBFeeder Version %s\n", c_version_str);
    fprintf(stderr, "\n");
    fprintf(stderr, "\t--config [-c]\t <filename>\t\tSpecify configuration file to use. (default: /etc/rbfeeder.ini)\n");
    fprintf(stderr, "\t--device [-d]\t <number>\t\tSpecify device number to use.\n");
    fprintf(stderr, "\t--showkey [-sw]\t\t\t\tShow current sharing key set in  configuration file.\n");
    fprintf(stderr, "\t--setkey [-sk] <sharing key>\t\tSet sharing key and store in configuration file.\n");
    fprintf(stderr, "\t--set-network-mode <on / off>\t\tEnable or disable network mode (get data from external host).\n");
    fprintf(stderr, "\t--set-network-protocol <beast/raw>\tSet network protocol for external host.\n");
    fprintf(stderr, "\t--set-network-port <port>\t\tSet network port for external host.\n");
    fprintf(stderr, "\t--no-start\t\t\t\tDon't start application, just set options and save to configuration file.\n");
    fprintf(stderr, "\t--help [-h]\t\t\t\tDisplay this message.\n");
    fprintf(stderr, "\n");
}

static void sigintHandler(int dummy) {
    MODES_NOTUSED(dummy);
    signal(SIGINT, SIG_DFL); // reset signal handler - bit extra safety
    Modes.exit = 1; // Signal to threads that we are done
    free(key);
    free(nonce);
#ifdef RBCSRBLC
    led_off(LED_ADSB);
    led_off(LED_STATUS);
    led_off(LED_GPS);
    led_off(LED_PC);
    led_off(LED_ERROR);
    led_off(LED_VHF);
#endif

}

static void sigtermHandler(int dummy) {
    MODES_NOTUSED(dummy);
    signal(SIGTERM, SIG_DFL); // reset signal handler - bit extra safety
    Modes.exit = 1; // Signal to threads that we are done
    free(key);
    free(nonce);
#ifdef RBCSRBLC
    led_off(LED_ADSB);
    led_off(LED_STATUS);
    led_off(LED_GPS);
    led_off(LED_PC);
    led_off(LED_ERROR);
    led_off(LED_VHF);
#endif
}

/* function to check whether the position is set to 1 or not */
int check_bit(int number, int position) {
    return (number >> position) & 1;
}

static inline void set_bit(uint32_t *number, int position) {
    *number |= 1 << position;
}

static inline void clear_bit(uint32_t *number, int position) {
    *number &= ~(1 << position);
}

char *md5sumFile(char *fname) {


    FILE *inFile = fopen(fname, "rb");

    int bytes;
    unsigned char data[1024];

    if (inFile == NULL) {
        printf("%s can't be opened.\n", fname);
        return NULL;
    }


    MD5_CTX *md5s;
    md5s = malloc(sizeof (MD5_CTX));
    MD5_Init(md5s);

    unsigned char *final = malloc(33);

    while ((bytes = fread(data, 1, 1024, inFile)) != 0)
        MD5_Update(md5s, data, bytes);

    MD5_Final(final, md5s);
    char *output = malloc(33);
    memset(output, 0, 33);
    for (int i = 0; i < 16; i++) {
        sprintf(output, "%s%.2x", output, final[i]);
    }
    output[33] = '\0';
    airnav_log_level(4, "MD5 final: %s\n", output);
    free(final);
    fclose(inFile);
    return output;
}

/*
 * Function to download file using CURL library
 */
int get_page(const char* url, const char* file_name) {

    CURL* easyhandle = curl_easy_init();
    curl_easy_setopt(easyhandle, CURLOPT_URL, url);
    FILE* file = fopen(file_name, "w");
    curl_easy_setopt(easyhandle, CURLOPT_WRITEDATA, file);
    curl_easy_perform(easyhandle);
    curl_easy_cleanup(easyhandle);
    fclose(file);

    return 1;
}

/*
 * Send statistics to FW
 */
void *airnav_send_stats_thread(void *argv) {

    MODES_NOTUSED(argv);

    static uint64_t next_update;
    static uint64_t next_mlat_check;
    uint64_t now = mstime();

    next_update = now + (AIRNAV_STATS_SEND_TIME * 1000);
    next_mlat_check = now + 30 * 1000;


    airnav_log_level(3, "Starting stats thread...\n");
    while (Modes.exit == 0) {

        now = mstime();
        if (now >= next_mlat_check) {
            next_mlat_check = now + 30 * 1000;
            if (autostart_mlat) {
                startMLAT();
            }
        }
        if (now >= next_update) {
            next_update = now + (AIRNAV_STATS_SEND_TIME * 1000);
            sendStats();
        }

        usleep(1000000);
    }


    return NULL;
}


#ifdef RBCSRBLC

/*
 * Get PMU Temperature - only for RBCS
 */
float getPMUTemp(void) {

    static int i2chandle, pmu_temp, i;
    i2chandle = open("/dev/i2c-0", O_RDWR);
    if (i2chandle > 0) {
        ioctl(i2chandle, I2C_SLAVE_FORCE, 0x34); // I2C_SLAVE_FORCE
        i = i2c_smbus_read_word_data(i2chandle, 0x5e);
        pmu_temp = ((i & 0xff)*16 + ((i & 0xf00) >> 8) - 1447)*100;
        close(i2chandle);
        //if (pmu_temp > pmu_max_temp) pmu_max_temp = pmu_temp;
    } else {
        pmu_temp = 0;
    }
    double pmu_temp2 = (double) ((double) pmu_temp / (double) 1000);

    if (pmu_temp2 > max_pmu_temp) {
        max_pmu_temp = pmu_temp2;
    }
    return pmu_temp2;
}
#endif    

/*
 * Get CPU Temperature
 */
float getCPUTemp(void) {

#ifdef __arm__

#ifndef RBCSRBLC
    float systemp, millideg;
    FILE *thermal;
    int n;

    thermal = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    n = fscanf(thermal, "%f", &millideg);
    fclose(thermal);
    systemp = millideg / 1000;

    if (systemp > max_cpu_temp) {
        max_cpu_temp = systemp;
    }
    airnav_log_level(4, "CPU temperature is %.2f degrees C\n", systemp);
    return systemp;
#else

    double temp_cpu = 0;
    double temp = 0;
    temp_cpu = (float) mmio_read(0x01c25020);

    temp = (double) (((double) temp_cpu - (double) 1447) / (double) 10);
    if (temp < 0 || temp > 250) {
        temp = 0;
    }


    if (temp > max_cpu_temp) {
        max_cpu_temp = temp;
    }

    //    airnav_log_level(3, "CPU temp.: %.2fC (%.2fC Max)\n", temp, max_cpu_temp);

    return temp;


#endif



#else
    return 0;
#endif

    return 0;
}

/*
 * Thread to send data
 */
void *threadSendData(void *argv) {
    MODES_NOTUSED(argv);


    signal(SIGPIPE, sigpipe_handler);
    int contador = 0;
    int contador_cache = 0;
    struct packet_list *tmp1, *local_list;
    int l_packets_total = 0;
    int l_packets_last = 0;

    uint64_t now = mstime();
    int invalid_counter = 0;


    while (!Modes.exit) {

        contador_cache = 0;
        contador = 0;

        pthread_mutex_lock(&m_copy);
        local_list = flist;
        flist = NULL;
        contador_cache = packet_cache_count;
        packet_cache_count = 0;
        pthread_mutex_unlock(&m_copy);

        l_packets_total = 0;
        l_packets_last = 0;
        now = mstime();
        invalid_counter = 0;

        while (local_list != NULL) {
            contador++;
            if (local_list->packet != NULL) {
                if ((now - local_list->packet->timestp) > 60000) {
                    airnav_log("Address %06X invalid (more than 60 seconds timestamp). Now: $llu, packet timestamp: %llu\n", local_list->packet->modes_addr,
                            now, local_list->packet->timestp);
                    invalid_counter++;
                }
            }

            if (airnav_com_inited == 1) {
                sendPacket(local_list->packet);
                l_packets_total++;
                l_packets_last++;
            } else {
                // To-do - save packets for future send.
                // For now, just clear memory
                free(local_list->packet);
            }
            tmp1 = local_list;
            local_list = local_list->next;
            FREE(tmp1);
        }

        if (l_packets_last > 0 || l_packets_total > 0) {
            pthread_mutex_lock(&m_packets_counter);
            packets_total = packets_total + l_packets_total;
            packets_last = packets_last + l_packets_last;
            pthread_mutex_unlock(&m_packets_counter);
        }
        if (invalid_counter > 0) {
            airnav_log("Invalid (old) flights: %d\n", invalid_counter);
        }
        airnav_log_level(7, "Local counter: %d, global counter: %d\n", contador, contador_cache);

        sleep(1);
    }

    return NULL;
}

/*
 * Enable keep-alive and other socket options
 */
void enable_keepalive(int sock) {
    MODES_NOTUSED(sock);

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof (int));

    int idle = 120;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof (int));

    int interval = 15;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof (int));

    int maxpkt = 20;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &maxpkt, sizeof (int));

    int reusable = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reusable, sizeof (int));

    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout, sizeof (timeout)) < 0)
        airnav_log("Error: setsockopt failed\n");

    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *) &timeout, sizeof (timeout)) < 0)
        airnav_log("Error: setsockopt failed\n");


}

/*
 * Proccess packet 2 (all packets)
 */
int procPacket2(char packet[BUFFLEN], int psize) {
    airnav_log_level(7, "Decoding packet...\n");
    // Variables to hold package data
    short p_size;
    uint32_t p_flags = 0;
    char p_cmd = 0;
    uint64_t p_client_version = -1;
    char p_client_key[33];
    int32_t p_modes_addr = 0;
    char p_callsign[9];
    int32_t p_altitude = -1;
    short p_heading = -1;
    short p_gnd_speed = -1;
    short p_ias = -1;
    short p_vert_rate = 0;
    short p_squawk = -1;
    double lat = 0;
    double lon = 0;
    char p_airborne = 0;
    // Control and general variables
    int byte_idx = 0;
    char p_payload_set = 0;
    char p_position_set = 0;
    char p_altitude_set = 0;
    char p_airborne_set = 0;
    char p_client_version_set = 0;

    // Clear Callsign other arrays
    memset(&p_callsign, 0, 9);
    memset(&p_client_key, 0, 33);

    memset(&last_payload, 0, 501);
    last_cmd = 0;

    // Read size byte and icrement local index
    p_size = ntohs(*(short*) (packet + byte_idx));
    byte_idx = byte_idx + sizeof (short);
    int size_without_fixed = (psize - 2);

    // If packet is less than 7 bytes, discart
    if (psize < 7) {
        airnav_log_level(3, "Packet too small (%d). Minimum size is 7 bytes\n", psize);
        return 0;
    }

    airnav_log_level(7, "Package size is p_size=%d, buffer_size=%d\n", p_size, psize);
    if (p_size == psize) {
        airnav_log_level(7, "Sizes match!\n");
    } else {
        airnav_log_level(3, "Packet size doesn't match! Informed: $d, current: %d\n", p_size, psize);
        return 0;
    }

    /*
     * Encryption part start
     */
    // Copy sncrypted part of packet to local char array
    char tmp_crypted[size_without_fixed];
    for (int f = 0; f < size_without_fixed; f++) {
        tmp_crypted[f] = packet[f + 2];
    }
    // Desencrypt 
    s20_crypt(key, S20_KEYLEN_128, nonce, 0, tmp_crypted, size_without_fixed);
    // Copy desencrypted part to original packet part
    for (int f = 0; f < size_without_fixed; f++) {
        packet[f + 2] = tmp_crypted[f];
    }
    /*
     * Encryption part end
     */

    // Read flags byte
    p_flags = ntohl(*(uint32_t *) (packet + byte_idx));
    byte_idx = byte_idx + sizeof (uint32_t);
    airnav_log_level(7, "Flags value: %d\n", p_flags);

    // Read CMD byte
    p_cmd = packet[byte_idx];
    byte_idx++;
    //airnav_log_level(7, "CMD value: %d\n", p_cmd);
    last_cmd = p_cmd;
    airnav_log_level(4, "CMD Arrived: %d\n", p_cmd);


    // Start payload data

    // ADDR field is filled?
    if (check_bit((int) p_flags, 0)) {
        p_modes_addr = ntohl(*(int32_t *) (packet + byte_idx));
        byte_idx = byte_idx + sizeof (int32_t);
        airnav_log_level(7, "Field ADDR filled with value: %d\n", p_modes_addr);
        airnav_log_level(7, "ModeS-ADDR (HEX): %06x\n", p_modes_addr);
    }


    // C_VERSION field is filled?
    if (check_bit((int) p_flags, 1)) {
        uint32_t c_v_ints[2];

        // first part
        c_v_ints[0] = ntohl(*(uint32_t *) (packet + byte_idx));
        byte_idx = byte_idx + 4;
        // second part
        c_v_ints[1] = ntohl(*(uint32_t *) (packet + byte_idx));
        byte_idx = byte_idx + 4;

        // Concact both
        memcpy(&p_client_version, c_v_ints, 8);
        p_client_version_set = 1;
        airnav_log_level(7, "Field C_VERSION filled with value: %lu\n", p_client_version);
    }

    // Key is set?           
    if (check_bit((int) p_flags, 2)) {
        for (int i = 0; i < 32; i++) {
            p_client_key[i] = packet[byte_idx];
            //printf("%c",packet[byte_idx]);
            byte_idx = byte_idx + 1;
        }
        //printf("\n");        
        p_client_key[32] = '\0';
        airnav_log_level(7, "Field KEY filled with value: %s\n", p_client_key);
    }

    // Callsign is set?        
    if (check_bit((int) p_flags, 3)) {
        for (int i = 0; i < 8; i++) {
            p_callsign[i] = packet[byte_idx];
            byte_idx = byte_idx + 1;
        }
        p_callsign[8] = '\0';
        airnav_log_level(7, "Field CALLSIGN filled with value: %s\n", p_callsign);
    }

    // ALTITUDE field is filled?
    if (check_bit((int) p_flags, 4)) {
        p_altitude = ntohl(*(int32_t *) (packet + byte_idx));
        byte_idx = byte_idx + sizeof (int32_t);
        airnav_log_level(7, "Field ALTITUDE filled with value: %d\n", p_altitude);
        p_altitude_set = 1;
    }

    // Position field is filled?
    if (check_bit((int) p_flags, 5)) {
        uint32_t c_v_ints[2];
        // Latitude
        // first part
        c_v_ints[0] = ntohl(*(uint32_t *) (packet + byte_idx));
        byte_idx = byte_idx + 4;
        // second part
        c_v_ints[1] = ntohl(*(uint32_t *) (packet + byte_idx));
        byte_idx = byte_idx + 4;

        // Concact both
        memcpy(&lat, c_v_ints, 8);
        airnav_log_level(7, "Field LAT filled with value: %f\n", lat);

        // Lon
        // first part
        c_v_ints[0] = ntohl(*(uint32_t *) (packet + byte_idx));
        byte_idx = byte_idx + 4;
        // second part
        c_v_ints[1] = ntohl(*(uint32_t *) (packet + byte_idx));
        byte_idx = byte_idx + 4;

        // Concact both
        memcpy(&lon, c_v_ints, 8);
        airnav_log_level(7, "Field LON filled with value: %f\n", lon);
        p_position_set = 1;
    }


    // HEADING field is filled?
    if (check_bit((int) p_flags, 6)) {
        p_heading = ntohs(*(short *) (packet + byte_idx));
        byte_idx = byte_idx + sizeof (short);
        airnav_log_level(7, "Field HEADING filled with value: %d\n", p_heading);
    }

    // GND_SPD field is filled?
    if (check_bit((int) p_flags, 7)) {
        p_gnd_speed = ntohs(*(short *) (packet + byte_idx));
        byte_idx = byte_idx + sizeof (short);
        airnav_log_level(7, "Field GND_SPD filled with value: %d\n", p_gnd_speed);
    }

    // IAS field is filled?
    if (check_bit((int) p_flags, 8)) {
        p_ias = ntohs(*(short *) (packet + byte_idx));
        byte_idx = byte_idx + sizeof (short);
        airnav_log_level(7, "Field IAS filled with value: %d\n", p_ias);
    }

    // VRATE field is filled?
    if (check_bit((int) p_flags, 9)) {
        p_vert_rate = ntohs(*(short *) (packet + byte_idx));
        byte_idx = byte_idx + sizeof (short);
        airnav_log_level(7, "Field VRATE filled with value: %d\n", p_vert_rate);
    }

    // SQUAWK field is filled?
    if (check_bit((int) p_flags, 10)) {
        p_squawk = ntohs(*(short *) (packet + byte_idx));
        byte_idx = byte_idx + sizeof (short);
        airnav_log_level(7, "Field SQUAWK filled with value: %d\n", p_squawk);
    }

    // AIRBORNE field is filled?
    if (check_bit((int) p_flags, 11)) {
        p_airborne = packet[byte_idx];
        byte_idx = byte_idx + 1;
        airnav_log_level(7, "Field AIRBORNE filled with value: %d\n", p_airborne);
        p_airborne_set = 1;
    }

    // Payload is set?        
    if (check_bit((int) p_flags, 12)) {
        for (int i = 0; i < 500; i++) {
            last_payload[i] = packet[byte_idx];
            byte_idx = byte_idx + 1;
        }
        last_payload[500] = '\0';
        p_payload_set = 1;
        airnav_log_level(7, "Field PAYLOAD filled with value: %s\n", last_payload);
    }

    // Proc waitCmd
    pthread_mutex_lock(&m_cmd);
    if (p_cmd == expected) {
        expected_arrived = 1;
    }
    pthread_mutex_unlock(&m_cmd);


    if (p_cmd == 2) {
        airnav_log_level(1, "ACK Received from server.\n");
    } else if (p_cmd == 4) {

        sendDumpConfig();

    } else if (p_cmd == 5) {

        sendVhfConfig();

    } else if (p_cmd == 6) {

        sendMLATConfig();

    } else if (p_cmd == 7) {

        sendACARSConfig();

    } else if (p_cmd == 8) { // Received update response

        airnav_log_level(3, "Command 8 received!\n");
        if (p_payload_set == 1) {
            doUpdate(last_payload);
        }

    } else if (p_cmd == 9) { // Received client SN
        airnav_log_level(3, "CMD 9. Received SN.\n");
        if (p_payload_set == 1) {
            airnav_log_level(4, "CMD=9, Payload set.\n");
            ini_saveGeneric(configuration_file, "client", "sn", last_payload);
            ini_getString(&sn, configuration_file, "client", "sn", NULL);
        } else {
            airnav_log_level(3, "CMD 9 received, but no payload.\n");
        }

    } else if (p_cmd == 14) { // Received date/time
        airnav_log_level(3, "Step 16...\n");
#ifdef RBCSRBLC
        if (p_client_version_set == 1) {
            time_t now2 = time(NULL);
            airnav_log_level(3, "Date/Time received: %lu\n", (unsigned long) p_client_version);
            airnav_log_level(3, "Current timestamp: %lu\n", (unsigned long) now2);
            int dif = 0;
            dif = (unsigned long) now2 - (unsigned long) p_client_version;
            if (dif > 300 || dif < -300) {
                airnav_log("Time difference from server is more than 5 minutes (Dif=%d, Server=%lu, Local=%lu). Setting new local time.\n",
                        dif, (unsigned long) p_client_version, (unsigned long) now2);
                char d_cmd[100] = {0};
                sprintf(d_cmd, "date -s \"@%lu\"", (unsigned long) p_client_version);
                system(d_cmd);
                system("hwclock -uw");
                airnav_log("Done setting date.\n");
            }

        } else {
            airnav_log_level(3, "CMD 14 Received, but client_version not set.\n");
        }
#endif

    } else if (p_cmd == 21) {
        airnav_log_level(3, "CMD 21. Let's start vhf!\n");
        startVhf();
    } else if (p_cmd == 22) {
        airnav_log_level(3, "CMD 22. Let's start MLAT!\n");
        startMLAT();
    } else if (p_cmd == 23) {
        airnav_log_level(3, "CMD 23. Let's stop VHF ACARS!\n");
        startACARS();
    } else if (p_cmd == 41) {
        airnav_log_level(3, "CMD 41. Let's stop vhf!\n");
        stopVhf();
    } else if (p_cmd == 42) {
        airnav_log_level(3, "CMD 42. Let's stop MLAT!\n");
        stopMLAT();
    } else if (p_cmd == 43) {
        airnav_log_level(3, "CMD 43. Let's stop ACARS!\n");
        stopACARS();
    } else if (p_cmd == 51) { // Save vhf command line        
        airnav_log_level(3, "CMD 51. Save Vhf command.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "vhf_cmd", last_payload);
            ini_getString(&vhf_cmd, configuration_file, "vhf", "vhf_cmd", NULL);
        }

    } else if (p_cmd == 52) { // Save vhf icecast host
        airnav_log_level(3, "CMD 52. IceCast Host.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "icecast_host", last_payload);
            ini_getString(&ice_host, configuration_file, "vhf", "icecast_host", "airnavsystems.com");
            saveVhfConfig();
        }

    } else if (p_cmd == 53) { // Save vhf username
        airnav_log_level(3, "CMD 53. IceCast Usert.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "icecast_user", last_payload);
            ini_getString(&ice_user, configuration_file, "vhf", "icecast_user", "user1");
            saveVhfConfig();
        }

    } else if (p_cmd == 54) { // Save vhf Pwd
        airnav_log_level(3, "CMD 54. IceCast Pwd.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "icecast_pwd", last_payload);
            ini_getString(&ice_pwd, configuration_file, "vhf", "icecast_pwd", "senha123");
            saveVhfConfig();
        }

    } else if (p_cmd == 55) { // Save vhf port
        airnav_log_level(3, "CMD 55. IceCast Port.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "icecast_port", last_payload);
            ice_port = ini_getInteger(configuration_file, "vhf", "icecast_port", 8000);
            saveVhfConfig();
        }

    } else if (p_cmd == 56) { // Save vhf mode
        airnav_log_level(3, "CMD 56. IceCast mode.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "mode", last_payload);
            ini_getString(&vhf_mode, configuration_file, "vhf", "mode", "scan");
            saveVhfConfig();
        }

    } else if (p_cmd == 57) { // Save vhf mode
        airnav_log_level(3, "CMD 57. IceCast frequencies.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "freqs", last_payload);
            ini_getString(&vhf_freqs, configuration_file, "vhf", "freqs", "118000000");
            saveVhfConfig();
        }

    } else if (p_cmd == 58) { // Save vhf mode
        airnav_log_level(3, "CMD 58. IceCast mountpoint.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "mountpoint", last_payload);
            ini_getString(&ice_mountpoint, configuration_file, "vhf", "mountpoint", NULL);
            saveVhfConfig();
        }

    } else if (p_cmd == 59) { // Save vhf mode
        airnav_log_level(3, "CMD 59. VHF Device.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "device", last_payload);
            vhf_device = ini_getInteger(configuration_file, "vhf", "device", 1);
            saveVhfConfig();
        }

    } else if (p_cmd == 60) { // Save vhf mode
        airnav_log_level(3, "CMD 60. VHF Gain.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "gain", last_payload);
            vhf_gain = ini_getDouble(configuration_file, "vhf", "gain", 42);
            saveVhfConfig();
        }

    } else if (p_cmd == 61) { // Save vhf mode
        airnav_log_level(3, "CMD 61. VHF Squelch.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "squelch", last_payload);
            vhf_squelch = ini_getInteger(configuration_file, "vhf", "squelch", -1);
            saveVhfConfig();
        }

    } else if (p_cmd == 62) { // Save vhf mode
        airnav_log_level(3, "CMD 62. VHF Correction.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "correction", last_payload);
            vhf_correction = ini_getInteger(configuration_file, "vhf", "correction", 0);
            saveVhfConfig();
        }

    } else if (p_cmd == 63) { // Save vhf mode
        airnav_log_level(3, "CMD 63. VHF Afc.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "vhf", "afc", last_payload);
            vhf_afc = ini_getInteger(configuration_file, "vhf", "afc", 0);
            saveVhfConfig();
        }

    } else if (p_cmd == 64) { // Save vhf mode
        airnav_log_level(3, "CMD 64. MLAT CMD.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "mlat", "mlat_cmd", last_payload);
            ini_getString(&mlat_cmd, configuration_file, "mlat", "mlat_cmd", NULL);
        }

    } else if (p_cmd == 65) { // MLAT Server
        airnav_log_level(3, "CMD 65. MLAT Server.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "mlat", "server", last_payload);
            ini_getString(&mlat_server, configuration_file, "mlat", "server", DEFAULT_MLAT_SERVER);
        }

    } else if (p_cmd == 66) { // Set receiver positions
        airnav_log_level(3, "CMD 66. Set receiver position.\n");
        if (p_position_set == 1 && p_altitude_set == 1) {
            char tmp_lat[30] = {0};
            char tmp_lon[30] = {0};
            char tmp_alt[30] = {0};
            sprintf(tmp_lat, "%f", lat);
            sprintf(tmp_lon, "%f", lon);
            sprintf(tmp_alt, "%d", p_altitude);
            airnav_log_level(3, "Lat: %s, Lon: %s, Alt: %s\n", tmp_lat, tmp_lon, tmp_alt);
            ini_saveGeneric(configuration_file, "client", "lat", tmp_lat);
            ini_saveGeneric(configuration_file, "client", "lon", tmp_lon);
            ini_saveGeneric(configuration_file, "client", "alt", tmp_alt);
            g_lat = ini_getDouble(configuration_file, "client", "lat", 0);
            g_lon = ini_getDouble(configuration_file, "client", "lon", 0);
            g_alt = ini_getDouble(configuration_file, "client", "alt", -999);
        } else {
            airnav_log_level(3, "Position CMD received, but no lat/lon or altitude received.\n");
        }

    } else if (p_cmd == 67) { // Set Dump AGC
        airnav_log_level(3, "CMD 67 Received.\n");
        if (p_airborne_set == 1) {
            if (p_airborne == 1) {
                ini_saveGeneric(configuration_file, "client", "dump_agc", "true");
            } else {
                ini_saveGeneric(configuration_file, "client", "dump_agc", "false");
            }
        }
    } else if (p_cmd == 68) { // Set Dump DC Filter
        airnav_log_level(3, "CMD 68 Received.\n");
        if (p_airborne_set == 1) {
            if (p_airborne == 1) {
                ini_saveGeneric(configuration_file, "client", "dump_dc_filter", "true");
            } else {
                ini_saveGeneric(configuration_file, "client", "dump_dc_filter", "false");
            }
        }
    } else if (p_cmd == 69) { // Set Dump fix
        airnav_log_level(3, "CMD 69 Received.\n");
        if (p_airborne_set == 1) {
            if (p_airborne == 1) {
                ini_saveGeneric(configuration_file, "client", "dump_fix", "true");
            } else {
                ini_saveGeneric(configuration_file, "client", "dump_fix", "false");
            }
        }
    } else if (p_cmd == 70) { // Set Dump crc_check
        airnav_log_level(3, "CMD 70 Received.\n");
        if (p_airborne_set == 1) {
            if (p_airborne == 1) {
                ini_saveGeneric(configuration_file, "client", "dump_check_crc", "true");
            } else {
                ini_saveGeneric(configuration_file, "client", "dump_check_crc", "false");
            }
        }
    } else if (p_cmd == 71) { // Set Dump Mode AC
        airnav_log_level(3, "CMD 71 Received.\n");
        if (p_airborne_set == 1) {
            if (p_airborne == 1) {
                ini_saveGeneric(configuration_file, "client", "dump_agc", "true");
            } else {
                ini_saveGeneric(configuration_file, "client", "dump_agc", "false");
            }
        }
    } else if (p_cmd == 72) { // Set Dump PPM Correction
        airnav_log_level(3, "CMD 72 Received.\n");
        if (p_altitude_set == 1) {
            char tmp_ppm[10] = {0};
            sprintf(tmp_ppm, "%d", p_altitude);
            ini_saveGeneric(configuration_file, "client", "dump_ppm_error", tmp_ppm);
        }


    } else if (p_cmd == 73) { // Set Dump Device
        airnav_log_level(3, "CMD 73 Received.\n");
        if (p_altitude_set == 1) {
            char tmp_dev[4] = {0};
            sprintf(tmp_dev, "%d", p_altitude);
            ini_saveGeneric(configuration_file, "client", "dump_device", tmp_dev);
        }

    } else if (p_cmd == 74) { // Set Dump Gain
        airnav_log_level(3, "CMD 74 Received.\n");
        if (p_altitude_set == 1) {
            char tmp_gain[4] = {0};
            sprintf(tmp_gain, "%d", p_altitude);
            ini_saveGeneric(configuration_file, "client", "dump_gain", tmp_gain);
        }

    } else if (p_cmd == 75) { // Server requested configuration
        sendConfig();
    } else if (p_cmd == 76) { // ACARS Server
        airnav_log_level(3, "CMD 76. ACARS Server.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "acars", "server", last_payload);
            ini_getString(&acars_server, configuration_file, "acars", "server", "airnavsystems.com:9743");
        }

    } else if (p_cmd == 77) { // ACARS cmd
        airnav_log_level(3, "CMD 77. ACARS Cmd.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "acars", "acars_cmd", last_payload);
            ini_getString(&acars_cmd, configuration_file, "acars", "acars_cmd", NULL);
        }

    } else if (p_cmd == 78) { // ACARS Freqs
        airnav_log_level(3, "CMD 78. ACARS Freq.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "acars", "freqs", last_payload);
            ini_getString(&acars_freqs, configuration_file, "acars", "freqs", "131.550");
        }

    } else if (p_cmd == 79) { // ACARS Device
        airnav_log_level(3, "CMD 79. ACARS Device.\n");
        if (p_payload_set == 1) {
            ini_saveGeneric(configuration_file, "acars", "device", last_payload);
            acars_device = ini_getInteger(configuration_file, "acars", "device", 1);
        }


    } else if (p_cmd == 91) { // Enable vhf-autostart
        airnav_log_level(3, "CMD 91. Enable Vhf auto-start.\n");
        ini_saveGeneric(configuration_file, "vhf", "autostart_vhf", "true");
        autostart_vhf = 1;

    } else if (p_cmd == 93) { // Enable vhf-autostart
        airnav_log_level(3, "CMD 93. Disable Vhf auto-start.\n");
        ini_saveGeneric(configuration_file, "vhf", "autostart_vhf", "false");
        autostart_vhf = 0;

    } else if (p_cmd == 94) { // Enable vhf-autostart
        airnav_log_level(3, "CMD 94. Enable MLAT auto-start.\n");
        ini_saveGeneric(configuration_file, "mlat", "autostart_mlat", "true");
        autostart_mlat = 1;

    } else if (p_cmd == 95) { // Disable mlat-autostart
        airnav_log_level(3, "CMD 95. Disable MLAT auto-start.\n");
        ini_saveGeneric(configuration_file, "mlat", "autostart_mlat", "false");
        autostart_mlat = 0;

    } else if (p_cmd == 96) { // Enable ACARS autostart
        airnav_log_level(3, "CMD 96. Enable ACARS autostart.\n");
        ini_saveGeneric(configuration_file, "acars", "autostart_acars", "true");
        autostart_acars = 0;

    } else if (p_cmd == 97) { // Disable acars-autostart
        airnav_log_level(3, "CMD 97. Disable ACARS auto-start.\n");
        ini_saveGeneric(configuration_file, "acars", "autostart_acars", "false");
        autostart_acars = 0;

    } else if (p_cmd == 98) { // Set user-level for manual configuration in device status page
        airnav_log_level(3, "CMD 98. Set user level.\n");
        if (p_altitude_set == 1) {
            ini_saveInteger(configuration_file, "client", "user_level", p_altitude);
        } else {
            airnav_log("Error saving parameter (remote ADM commando).\n");
        }
    } else {
        airnav_log_level(3, "Unknow command: %d\n", p_cmd);
    }

    return p_cmd;

}

/*
 * Thread that will wait for any incoming packets
 */
void *tWaitCmds(void * argv) {

    MODES_NOTUSED(argv);
    signal(SIGPIPE, sigpipe_handler);
    //char abort = 0;
    int read_size;
    char temp_char;
    static uint64_t next_update;
    uint64_t now = mstime();
    int timeout = 0;
    int buf_idx = 0;
    short *tmp_size = malloc(sizeof (short));
    *tmp_size = 9999;
    char buf[BUFFLEN] = {0};

    while (!Modes.exit) {

        if (airnav_socket != -1) {

            pthread_mutex_lock(&m_socket);
            read_size = recv(airnav_socket, &temp_char, 1, MSG_PEEK | MSG_DONTWAIT);
            pthread_mutex_unlock(&m_socket);


            if (read_size > 0) { // There's data!

                pthread_mutex_lock(&m_socket);
                if (buf_idx < BUFFLEN && (read_size = recv(airnav_socket, &temp_char, 1, 0) > 0)) {
                    pthread_mutex_unlock(&m_socket);
                    //airnav_log_level(4, "Data arrived.....\n");
                    if (buf_idx == 2) {
                        memcpy(tmp_size, buf, sizeof (short));
                        *tmp_size = ntohs(*tmp_size);
                    }
                    // We need to check if 2 last bytes are end of TX and buffer size is at least the size defined in firs t2 bytes
                    if (buf[buf_idx - 1] == txend[0] && temp_char == txend[1] && (buf_idx - 1) >= *tmp_size) {
                        last_success_received = (int) time(NULL); // Reset receiving timeout timer 
                        airnav_log_level(4, "Packet arrived.\n");
                        procPacket2(buf, buf_idx - 1); // Proccess the package                    
                        memset(&buf, 0, sizeof (buf)); // Clear the buffer
                        buf_idx = 0;
                        temp_char = 0;
                        *tmp_size = 9999;
                    } else {
                        buf[buf_idx] = temp_char;
                        buf_idx++;
                        temp_char = 0;
                    }


                } else {
                    pthread_mutex_unlock(&m_socket);
                    airnav_log_level(6, "Buffer Cheio!\n");
                    memset(&buf, 0, sizeof (buf)); // Clear the buffer
                    buf_idx = 0;
                    temp_char = 0;
                    *tmp_size = 9999;
                }


            } else { // No data.....sleep for 100 miliseconds to avoid 100% CPU usage
                usleep(100000);
            }


        } else {
            //   airnav_log_level(4, "Socket = -1!\n");
            usleep(100000);
        }


    }

    free(tmp_size);

    return 0;
}

/*
 * Avoid program crash while using invalid socket
 */
void sigpipe_handler() {
    airnav_log_level(2, "SIGPIPE caught......but not crashing!\n");
    if (airnav_socket < 0) {
        close(airnav_socket);
        airnav_com_inited = 0;
        airnav_socket = -1;
    }



}

pid_t run_cmd3(char *cmd) {
    pid_t pid;
    char *argv[30];
    int i = 0;
    int status;
    char path[1000];

    airnav_log_level(2, "Run command: %s\n", cmd);

    argv[0] = strtok(cmd, " ");

    strcpy(path, argv[0]);

    while (argv[i] != NULL) {
        argv[++i] = strtok(NULL, " ");
    }

    airnav_log_level(2, "cmd: %s\n", path);
    for (i = 0; argv[i] != NULL; i++) {
        airnav_log_level(2, "cmd arg %d: %s\n", i, argv[i]);
    }

    status = posix_spawn(&pid, path, NULL, NULL, argv, environ);

    if (status == 0) {
        airnav_log_level(2, "Child pid: %i\n", pid);
    } else {
        airnav_log_level(2, "posix_spawn: %s\n", strerror(status));
        pid = -1;
    }

    return pid;
}

void run_cmd2(char *cmd) {
    pid_t pid;
    char *argv[] = {"sh", "-c", cmd, NULL};
    int status;
    airnav_log_level(2, "Run command: %s\n", cmd);
    status = posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, environ);
    if (status == 0) {
        airnav_log_level(2, "Child pid: %i\n", pid);
    } else {
        airnav_log_level(2, "posix_spawn: %s\n", strerror(status));
    }
}

/*
 * Run external command and return pid
 */
pid_t run_cmd(const char *cmd) {
    pid_t pid, ret;
    char *argv[] = {(char*) cmd, NULL};
    int status, s;
    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_t *file_actionsp;

    s = posix_spawn_file_actions_init(&file_actions);
    if (s != 0)
        return 0;

    //STDERR_FILENO
    s = posix_spawn_file_actions_addclose(&file_actions, STDERR_FILENO);
    if (s != 0)
        return 0;

    file_actionsp = &file_actions;

    char *cmd2 = malloc(100);
    memset(cmd2, 0, 100);
    airnav_log_level(3, "Cmd passado, letra por letra: \n");
    for (int d = 0; d < strlen(cmd); d++) {
        if (isspace(cmd[d])) {
            break;
        }
        cmd2[d] = cmd[d];
    }

    airnav_log_level(2, "Cmd: %s\n", cmd);
    airnav_log_level(2, "Cmd2: %s\n", cmd2);
    status = posix_spawn(&pid, cmd2, file_actionsp, NULL, argv, environ);
    if (status == 0) {
        airnav_log_level(8, "Child pid: %i\n", pid);
        ret = pid;
    } else {
        ret = 0;
    }

    return ret;
}




#ifdef RBCSRBLC

int set_led(char *param) {
    int ret;
#ifndef WIN32
    int i, j, k;
    char *q;
    uint32_t *on_led_cfg, *on_led_data, on_led_cfg_mask, on_led_data_mask, val;

    for (q = param; isspace(*q); q++);
    if (*q == '-') k = 0, q++;
    else k = 1;
    i = toupper(*q) - 0x41;
    if (i >= 0 && i <= 9) {
        i *= 0x24;
    } else i = 0x104;
    sscanf(q + 1, "%d", &j);
    if (j >= 0 && j < 32) {
        on_led_cfg = gpio_base + i + ((j >> 1)&0xc);
        on_led_cfg_mask = 7 << ((j & 7) << 2);
        on_led_data = gpio_base + i + 16;
        on_led_data_mask = 1 << j;
        val = *on_led_cfg;
        val &= ~on_led_cfg_mask;
        val |= 0x11111111 & on_led_cfg_mask; // set H20 to output
        *on_led_cfg = val;
        if (k) {
            *on_led_data = *on_led_data | on_led_data_mask;
        } else {
            *on_led_data = *on_led_data&~on_led_data_mask;
        }
        ret = 0;
    } else ret = -1;
#else
    ret = -1;
#endif
    return ret;
}
#endif

#ifdef RBCSRBLC

int allwinner_init(void) {
#ifndef WIN32
    int ret;
    int mem_han;
    uint32_t pagemask;
    off_t addr_start, addr_offset;

    gpio_base = 0;
    mapped_pages = 0;
    mem_han = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_han > 0) {
        pagesize = sysconf(_SC_PAGESIZE); // on Radarbox defaults to 4096/0x1000
        pagemask = (~(pagesize - 1));
        addr_start = GPIO_BASE & pagemask;
        addr_offset = GPIO_BASE & ~pagemask;
        mapped_pages = GPIO_SIZE / pagesize + 1;
        mapped_base = mmap(0, pagesize*mapped_pages, PROT_READ | PROT_WRITE, MAP_SHARED, mem_han, addr_start);
        if (mapped_base != MAP_FAILED) {
            gpio_base = mapped_base + addr_offset;
        } else ret = -3;
        close(mem_han);
    } else ret = -2;

    return ret;
#else
    return -1;
#endif
}
#endif

#ifdef RBCSRBLC

int allwinner_exit(void) {
#ifdef WIN32
    return -1;
#else

    if (gpio_base) {

        munmap(mapped_base, pagesize * mapped_pages);
        gpio_base = 0;
    }
    return 0;
#endif
}
#endif



#ifdef RBCS

/*
 * Update GPS position
 */
void updateGPSData(void) {

    int send = 0;

    /* wait for 2 seconds to receive data */
    if (gps_waiting(&gps_data_airnav, 1000000)) {
        /* read data */
        if ((rc = gps_read(&gps_data_airnav)) == -1) {
            airnav_log_level(5, "error occured reading gps data. code: %d, reason: %s\n", rc, gps_errstr(rc));
            gps_fixed = 0;
        } else {
            /* Display data from the GPS receiver. */
            if ((gps_data_airnav.status == STATUS_FIX) &&
                    (gps_data_airnav.fix.mode == MODE_2D || gps_data_airnav.fix.mode == MODE_3D) &&
                    !isnan(gps_data_airnav.fix.latitude) &&
                    !isnan(gps_data_airnav.fix.longitude) &&
                    !isnan(gps_data_airnav.fix.altitude)) {

                gps_fixed = 1;


                char buff[20];
                char datetime_cmd[60];
                time_t now = gps_data_airnav.fix.time;
                strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));

                sprintf(datetime_cmd, "date -s \"%s\"", buff);

                char buff2[20];
                time_t now2 = time(NULL);
                strftime(buff2, 20, "%Y-%m-%d %H:%M:%S", localtime(&now2));

                airnav_log_level(2, "GPS Timestamp: \t\t%s\n", buff);
                airnav_log_level(2, "Local date/time: \t%s\n", buff2);

                double diff_t;
                diff_t = difftime(now, now2);
                //airnav_log("Diff: %.2f\n",diff_t);
                if (diff_t > 60 && date_time_set == 0) {
                    //airnav_log("Date/time more than 60 seconds, setting from GPS...\n");
                    system(datetime_cmd);
                    sleep(2);
                    system("hwclock -w");
                    date_time_set = 1;
                }


                if (g_lat != gps_data_airnav.fix.latitude) {
                    g_lat = gps_data_airnav.fix.latitude;
                    send = 1;
                }

                if (g_lon != gps_data_airnav.fix.longitude) {
                    g_lon = gps_data_airnav.fix.longitude;
                    Modes.fUserLon = g_lon;
                    send = 1;
                }

                if (sats_used != gps_data_airnav.satellites_used) {
                    sats_used = gps_data_airnav.satellites_used;
                }

                if (sats_visible != gps_data_airnav.satellites_visible) {
                    sats_visible = gps_data_airnav.satellites_visible;
                }

                if (gps_data_airnav.fix.mode == MODE_2D) {
                    fix_mode = 1;
                } else if (gps_data_airnav.fix.mode == MODE_3D) {
                    fix_mode = 2;
                    if (g_alt != (int) gps_data_airnav.fix.altitude) {
                        g_alt = (int) gps_data_airnav.fix.altitude;
                        send = 1;
                    }
                } else {
                    fix_mode = 0;
                }

                airnav_log_level(20, "latitude: %f, longitude: %f, speed: %f, timestamp: %ld, altitude: %.2f, visiable satellites: %d, used satellites: %d\n", gps_data_airnav.fix.latitude, gps_data_airnav.fix.longitude, gps_data_airnav.fix.speed, gps_data_airnav.fix.time, gps_data_airnav.fix.altitude, gps_data_airnav.satellites_visible, gps_data_airnav.satellites_used); //EDIT: Replaced tv.tv_sec with gps_data.fix.time
            } else {
                gps_fixed = 0;
                airnav_log_level(20, "no GPS data available\n");
            }
        }
    }


    return;
}
#endif

/*
 * Function to send statistics
 */
int sendStats(void) {

    if (airnav_com_inited == 0) {
        return 0;
    }

    struct stats *st = &Modes.stats_1min[Modes.stats_latest_1min];

    char *json = malloc(501);
    memset(json, 0, 501);
    struct p_data *pkt = NULL;


    st = &Modes.stats_1min[Modes.stats_latest_1min];
    airnav_log_level(3, "\n");
    airnav_log_level(3, "************ STATS ************\n");

    airnav_log_level(3, "JSON: %s\n", json);

    airnav_log_level(3, "Local receiver:\n");
    sprintf(json, "{\"1\":%llu", (unsigned long long) st->samples_processed);
    airnav_log_level(3, "JSON: %s\n", json);
    airnav_log_level(3, "  %llu samples processed\n", (unsigned long long) st->samples_processed);

    //sprintf(json, "%s,\"2\":%llu", json, (unsigned long long) st->samples_dropped);
    sprintf(json + strlen(json), ",\"2\":%llu", (unsigned long long) st->samples_dropped);
    airnav_log_level(3, "JSON: %s\n", json);
    airnav_log_level(3, "  %llu samples dropped\n", (unsigned long long) st->samples_dropped);

    sprintf(json + strlen(json), ",\"3\":%u", st->demod_modeac);
    airnav_log_level(3, "JSON: %s\n", json);
    airnav_log_level(3, "  %u Mode A/C messages received\n", st->demod_modeac);

    sprintf(json + strlen(json), ",\"4\":%u", st->demod_preambles);
    airnav_log_level(3, "JSON: %s\n", json);
    airnav_log_level(3, "  %u Mode-S message preambles received\n", st->demod_preambles);

    sprintf(json + strlen(json), ",\"5\":%u", st->demod_rejected_bad);
    airnav_log_level(3, "    %u with bad message format or invalid CRC\n", st->demod_rejected_bad);

    sprintf(json + strlen(json), ",\"6\":%u", st->demod_rejected_unknown_icao);
    airnav_log_level(3, "    %u with unrecognized ICAO address\n", st->demod_rejected_unknown_icao);

    sprintf(json + strlen(json), ",\"7\":%u", st->demod_accepted[0]);
    airnav_log_level(3, "    %u accepted with correct CRC\n", st->demod_accepted[0]);


    if (st->noise_power_sum > 0 && st->noise_power_count > 0) {
        sprintf(json + strlen(json), ",\"8\":%.1f", 10 * log10(st->noise_power_sum / st->noise_power_count));
        airnav_log_level(3, "  %.1f dBFS noise power\n",
                10 * log10(st->noise_power_sum / st->noise_power_count));
    }

    if (st->signal_power_sum > 0 && st->signal_power_count > 0) {
        sprintf(json + strlen(json), ",\"9\":%.1f", 10 * log10(st->signal_power_sum / st->signal_power_count));
        airnav_log_level(3, "  %.1f dBFS mean signal power\n",
                10 * log10(st->signal_power_sum / st->signal_power_count));
    }

    if (st->peak_signal_power > 0) {
        sprintf(json + strlen(json), ",\"10\":%.1f", 10 * log10(st->peak_signal_power));
        airnav_log_level(3, "  %.1f dBFS peak signal power\n",
                10 * log10(st->peak_signal_power));
    }

    sprintf(json + strlen(json), ",\"11\":%u", st->strong_signal_count);
    airnav_log_level(3, "  %u messages with signal power above -3dBFS\n", st->strong_signal_count);

    sprintf(json + strlen(json), ",\"12\":%u", st->messages_total);
    airnav_log_level(3, "%u total usable messages\n", st->messages_total);

    sprintf(json + strlen(json), ",\"13\":%u", st->cpr_surface);
    airnav_log_level(3, "%u surface position messages received\n", st->cpr_surface);

    sprintf(json + strlen(json), ",\"14\":%u", st->cpr_airborne);
    airnav_log_level(3, "%u airborne position messages received\n", st->cpr_airborne);

    sprintf(json + strlen(json), ",\"15\":%u", st->cpr_global_ok);
    airnav_log_level(3, "%u global CPR attempts with valid positions\n", st->cpr_global_ok);

    sprintf(json + strlen(json), ",\"16\":%u", st->cpr_global_bad);
    airnav_log_level(3, "%u global CPR attempts with bad data\n", st->cpr_global_bad);

    sprintf(json + strlen(json), ",\"17\":%u", st->cpr_global_range_checks);
    airnav_log_level(3, "%u global CPR attempts that failed the range check\n", st->cpr_global_range_checks);

    sprintf(json + strlen(json), ",\"18\":%u", st->cpr_global_speed_checks);
    airnav_log_level(3, "%u global CPR attempts that failed the speed check\n", st->cpr_global_speed_checks);

    sprintf(json + strlen(json), ",\"19\":%u", st->cpr_global_skipped);
    airnav_log_level(3, "%u global CPR attempts with insufficient data\n", st->cpr_global_skipped);

    sprintf(json + strlen(json), ",\"20\":%u", st->cpr_local_ok);
    airnav_log_level(3, "%u local CPR attempts with valid positions\n", st->cpr_local_ok);

    sprintf(json + strlen(json), ",\"21\":%u", st->cpr_local_aircraft_relative);
    airnav_log_level(3, "%u aircraft-relative positions\n", st->cpr_local_aircraft_relative);

    sprintf(json + strlen(json), ",\"22\":%u", st->cpr_local_receiver_relative);
    airnav_log_level(3, "%u receiver-relative positions\n", st->cpr_local_receiver_relative);

    sprintf(json + strlen(json), ",\"23\":%u", st->cpr_local_skipped);
    airnav_log_level(3, "%u local CPR attempts that did not produce useful positions\n", st->cpr_local_skipped);

    sprintf(json + strlen(json), ",\"24\":%u", st->cpr_local_range_checks);
    airnav_log_level(3, "%u local CPR attempts that failed the range check\n", st->cpr_local_range_checks);

    sprintf(json + strlen(json), ",\"25\":%u", st->cpr_local_speed_checks);
    airnav_log_level(3, "%u local CPR attempts that failed the speed check\n", st->cpr_local_speed_checks);

    sprintf(json + strlen(json), ",\"26\":%u", st->cpr_filtered);
    airnav_log_level(3, "%u CPR messages that look like transponder failures filtered\n", st->cpr_filtered);

    sprintf(json + strlen(json), ",\"27\":%u", st->suppressed_altitude_messages);
    airnav_log_level(3, "%u non-ES altitude messages from ES-equipped aircraft ignored\n", st->suppressed_altitude_messages);

    sprintf(json + strlen(json), ",\"28\":%u", st->unique_aircraft);
    airnav_log_level(3, "%u unique aircraft tracks\n", st->unique_aircraft);

    sprintf(json + strlen(json), ",\"29\":%u", st->single_message_aircraft);
    airnav_log_level(3, "%u aircraft tracks where only one message was seen\n", st->single_message_aircraft);


    uint64_t demod_cpu_millis = (uint64_t) st->demod_cpu.tv_sec * 1000UL + st->demod_cpu.tv_nsec / 1000000UL;
    uint64_t reader_cpu_millis = (uint64_t) st->reader_cpu.tv_sec * 1000UL + st->reader_cpu.tv_nsec / 1000000UL;
    uint64_t background_cpu_millis = (uint64_t) st->background_cpu.tv_sec * 1000UL + st->background_cpu.tv_nsec / 1000000UL;

    sprintf(json + strlen(json), ",\"30\":%.1f", 100.0 * (demod_cpu_millis + reader_cpu_millis + background_cpu_millis) / (st->end - st->start + 1));
    airnav_log_level(3, "CPU load: %.1f%%\n", 100.0 * (demod_cpu_millis + reader_cpu_millis + background_cpu_millis) / (st->end - st->start + 1));
    airnav_log_level(3, "  %llu ms for demodulation\n", (unsigned long long) demod_cpu_millis);
    airnav_log_level(3, "  %llu ms for reading from USB\n", (unsigned long long) reader_cpu_millis);
    airnav_log_level(3, "  %llu ms for network input and background tasks\n", (unsigned long long) background_cpu_millis);


#ifndef RBCSRBLC
    // Remote - only valid for Client
    sprintf(json + strlen(json), ",\"31\":%u", st->remote_accepted[0]);
    airnav_log_level(3, "[remote] %u accepted messages\n", st->remote_accepted[0]);

    sprintf(json + strlen(json), ",\"32\":%u", st->remote_received_modeac);
    airnav_log_level(3, "[remote] %u Mode AC messages\n", st->remote_received_modeac);

    sprintf(json + strlen(json), ",\"33\":%u", st->remote_received_modes);
    airnav_log_level(3, "[remote] %u Mode S messages\n", st->remote_received_modes);

    sprintf(json + strlen(json), ",\"34\":%u", st->remote_rejected_bad);
    airnav_log_level(3, "[remote] %u Rejected messages (bad)\n", st->remote_rejected_bad);

    sprintf(json + strlen(json), ",\"35\":%u", st->remote_rejected_unknown_icao);
    airnav_log_level(3, "[remote] %u unknow ICAO\n", st->remote_rejected_unknown_icao);
#endif

    sprintf(json + strlen(json), ",\"36\":%u", net_mode);
    airnav_log_level(3, "%u Network mode\n", net_mode);

    sprintf(json + strlen(json), ",\"cpu_temp\":%.2f", getCPUTemp());
    airnav_log_level(3, "%.2f CPU Temp\n", getCPUTemp());

#ifdef RBCSRBLC
    sprintf(json + strlen(json), ",\"pmu_temp\":%.2f", getPMUTemp());
    airnav_log_level(3, "%.2f PMU Temp\n", getPMUTemp());
#endif    


    sprintf(json + strlen(json), ",\"37\":%d", sats_visible);
    airnav_log_level(3, "%d Satellites visible\n", sats_visible);

    sprintf(json + strlen(json), ",\"38\":%d", sats_used);
    airnav_log_level(3, "%d Satellites used\n", sats_used);


    int vr = checkVhfRunning();
    sprintf(json + strlen(json), ",\"vhf_running\":%d", vr);
    airnav_log_level(3, "VHF Is running: %d\n", vr);

    int mr = checkMLATRunning();
    sprintf(json + strlen(json), ",\"mlat_running\":%d", mr);
    airnav_log_level(3, "MLAT Is running: %d\n", mr);

    int ar = checkACARSRunning();
    sprintf(json + strlen(json), ",\"acars_running\":%d", ar);
    airnav_log_level(3, "ACARS Is running: %d\n", ar);


    // close json bracket
    sprintf(json + strlen(json), "}"); // Don't finalize json string because we need to add more itens in FW


    if (airnav_com_inited == 1) {
        airnav_log_level(3, "Sending stats....\n");
        airnav_log_level(3, "JSON stats: \n");
        //  printf("%s\n",json);
        pkt = preparePacket();
        pkt->cmd = 30;
        strcpy(pkt->payload, json);
        pkt->payload_set = 1;
        sendPacket(pkt);
    }



    free(json);
    return 1;

}


#ifdef RBCSRBLC

void *thread_LED_ADSB(void *argv) {

    MODES_NOTUSED(argv);
    int led = 0;
    while (!Modes.exit) {
        //airnav_log_level(4,"Thread do LED ADSB!\n");
        pthread_mutex_lock(&m_led_adsb);
        led = led_adsb;
        led_adsb = 0;
        pthread_mutex_unlock(&m_led_adsb);

        if (led == 1) {
            //set_led("g6");
            led_on(LED_ADSB);
            usleep(15000);
            //set_led("-g6");
            led_off(LED_ADSB);
        } else {
            usleep(5000);
        }

    }

    return NULL;
}
#endif

/*
 * Function to create PID file
 */
void createPidFile(void) {
    FILE *f = fopen(pidfile, "w");
    if (f == NULL) {
        airnav_log_level(1, "Cannot write pidfile: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    } else {
        fprintf(f, "%ld\n", (long) getpid());
        fclose(f);
    }

    return;
}

/*
 * Check if vhf is running
 */
int checkVhfRunning(void) {

    if (vhf_pidfile == NULL) {
        return 0;
    }

    FILE *f = fopen(vhf_pidfile, "r");
    if (f == NULL) {
        airnav_log_level(5, "Vhf pidfile (%s) does not exist.\n", vhf_pidfile);
        return 0;
    } else {
        char tmp[20];
        memset(&tmp, 0, 20);
        fgets(tmp, 20, (FILE*) f);
        fclose(f);
        airnav_log_level(5, "Pid no arquivo: %s", tmp);
        if (strlen(tmp) > 2) {
            char* endptr;
            endptr = "0123456789";
            p_vhf = strtoimax(tmp, &endptr, 10);
            if (kill(p_vhf, 0) == 0) {
                return 1;
            } else {
                p_vhf = 0;
                remove(vhf_pidfile);
                return 0;
            }

        } else {

            return 0;
        }

    }
}

/*
 * Start vhf, if not running
 */
void startVhf(void) {

    if (vhf_pidfile == NULL) {
        airnav_log("VHF PID file not defined.\n");
        return;
    }

    checkVhfRunning();

    if (checkVhfRunning() != 0) {
        airnav_log_level(3, "Looks like vhf is already running.\n");
        return;
    }


    if (vhf_cmd == NULL) {
        airnav_log_level(3, "Vhf command line not defined.\n");
        return;
    }

    airnav_log_level(3, "Starting vhf with this command: '%s'\n", vhf_cmd);

    run_cmd2((char*) vhf_cmd);
    sleep(3);
    //  checkVhfRunning();

    if (checkVhfRunning() != 0) {
        airnav_log_level(3, "Ok, started! Pid is: %i\n", p_vhf);
        sendStats();
    } else {

        airnav_log_level(3, "Error starting vhf\n");
        p_vhf = 0;
        sendStats();
    }

    return;
}

/*
 * Stop vhf
 */
void stopVhf(void) {

    if (checkVhfRunning() == 0) {
        airnav_log_level(3, "Vhf is not running.\n");
        return;
    }
    if (kill(p_vhf, SIGTERM) == 0) {
        checkVhfRunning();
        airnav_log_level(3, "Succesfull kill vhf!\n");
        sleep(2);
        sendStats();
        return;
    } else {
        airnav_log_level(3, "Error killing vhf.\n");

        return;
    }

    return;
}

/*
 * Save VHF configuration to file
 */
int saveVhfConfig(void) {

    if ((strcmp(F_ARCH, "rbcs") != 0) && (strcmp(F_ARCH, "rblc") != 0)) {
        return 1;
    }

    char sq[30];
    memset(&sq, 0, 30);

    char af[30];
    memset(&af, 0, 30);

    char *template = "\ndevices:\n"
            "({\n"
            "  index = %d;\n"
            "  gain = %.0f;\n"
            "  correction = %d;\n"
            "  mode = \"%s\";\n"
            "  channels:\n"
            "  (\n"
            "    {\n"
            "      freqs = ( %s );\n%s%s"
            "      outputs: (\n"
            "        {\n"
            "	  type = \"icecast\";\n"
            "	  server = \"%s\";\n"
            "          port = %d;\n"
            "          mountpoint = \"%s\";\n"
            "          genre = \"ATC\";\n"
            "          username = \"%s\";\n"
            "          name = \"RadaBox24.com Live ATC\";\n"
            "          password = \"%s\";\n"
            "	  send_scan_freq_tags = false;\n"
            "        }\n"
            "      );\n"
            "    }\n"
            "  );\n"
            " }\n"
            ");\n";

    if (vhf_squelch > -1) {
        sprintf(sq, "      squelch = %d;\n", vhf_squelch);
    } else {
        sprintf(sq, " ");
    }

    if (vhf_afc > 0) {
        sprintf(af, "      afc = %d;\n", vhf_afc);
    } else {
        sprintf(af, " ");
    }

    FILE *f = fopen(vhf_config_file, "w");
    if (f == NULL) {
        airnav_log("Cannot write VHF Config file: %s\n", strerror(errno));
        return 0;
    } else {

        char mpoint[50] = {0};
        if (ice_mountpoint == NULL) {
            strcpy(mpoint, sn);
        } else {
            strcpy(mpoint, ice_mountpoint);
        }

        airnav_log_level(3, "Savinf VHF Config:\n");
        airnav_log_level(3, "Device: %.0f\n", vhf_device);
        airnav_log_level(3, "Gain: %.1f\n", vhf_gain);
        airnav_log_level(3, "Correction: %d\n", vhf_correction);
        airnav_log_level(3, "VHF Mode: %s\n", vhf_mode);
        airnav_log_level(3, "Freqs: %s\n", vhf_freqs);
        airnav_log_level(3, "SQ: %s\n", sq);
        airnav_log_level(3, "AF: %s\n", af);
        airnav_log_level(3, "Host: %s\n", ice_host);
        airnav_log_level(3, "Port: %d\n", ice_port);
        airnav_log_level(3, "Mount: %s\n", sn);
        airnav_log_level(3, "User: %s\n", ice_user);
        airnav_log_level(3, "Pwd: %s\n", ice_pwd);

        fprintf(f, template, vhf_device, vhf_gain, vhf_correction, vhf_mode, vhf_freqs, sq, af, ice_host, ice_port, sn, ice_user, ice_pwd);
        fclose(f);

        return 1;
    }


    return 1;
}

/*
 * Save MLAT configuration to file
 */
int saveMLATConfig(void) {

    char *template = "\n# MLAT - RadarBoxPi\n"
            "\n"
            "START_CLIENT=\"yes\"\n"
            "RUN_AS_USER=\"mlat\"\n"
            "SERVER_USER=\"%s\"\n"
            "LOGFILE=\"/var/log/mlat-client-config-rb.log\"\n"
            "INPUT_TYPE=\"dump1090\"\n"
            "INPUT_HOSTPORT=\"127.0.0.1:%d\"\n"
            "SERVER_HOSTPORT=\"%s\"\n"
            "LAT=\"%f\"\n"
            "LON=\"%f\"\n"
            "ALT=\"%d\"\n"
            "RESULTS=\"beast,connect,127.0.0.1:%s\"\n"
            "EXTRA_ARGS=\"\"\n"
            "\n";

    
    if (g_lat != 0 && g_lon != 0 && g_alt != -999 && sn != NULL && mlat_config != NULL) {
        
        FILE *f = fopen(mlat_config, "w");
        if (f == NULL) {
            airnav_log("Cannot write MLAT Config file (%s): %s\n", mlat_config, strerror(errno));
            return 0;
        } else {

            fprintf(f, template, sn, beast_out_port, mlat_server, g_lat, g_lon, g_alt, beast_in_port);
            fclose(f);

            return 1;
        }
    
    }

    return 0;
}

/*
 * Check if MLAT is running
 */
int checkMLATRunning(void) {
    if (p_mlat <= 0) {
        return 0;
    }

    if (kill(p_mlat, 0) == 0) {
        return 1;
    } else {
        p_mlat = 0;
        return 0;
    }
}

/*
 * Start MLAT, if not running
 */
void startMLAT(void) {

    if (checkMLATRunning() != 0) {
        airnav_log_level(3, "Looks like MLAT is already running.\n");
        return;
    }


    if (mlat_cmd == NULL) {
        airnav_log_level(3, "MLAT command line not defined.\n");
        return;
    }

    if (g_lat != 0 && g_lon != 0 && g_alt != -999 && sn != NULL) {

        
        char *tmp_cmd = malloc(300);
        memset(tmp_cmd, 0, 300);
        
        sprintf(tmp_cmd, "%s --input-type dump1090 --input-connect 127.0.0.1:%s --server %s --lat %f --lon %f --alt %d --user %s --results beast,connect,127.0.0.1:%s", mlat_cmd, beast_out_port, mlat_server, g_lat, g_lon, g_alt, sn, beast_in_port);
        
        airnav_log_level(3, "Starting MLAT with this command: '%s'\n", tmp_cmd);

        p_mlat = run_cmd3(tmp_cmd);
        free(tmp_cmd);

        sleep(3);

        if (checkMLATRunning() != 0) {
            airnav_log_level(3, "Ok, MLAT started! Pid is: %i\n", p_mlat);
            sendStats();
        } else {
            airnav_log_level(3, "Error starting MLAT\n");
            sendStats();
        }
    } else {
        airnav_log_level(2, "Can't start MLAT. Missing parameters.\n");
    }

    return;
}

/*
 * Stop MLAT
 */
void stopMLAT(void) {

    if (checkMLATRunning() == 0) {
        airnav_log_level(3, "MLAT is not running.\n");
        return;
    }
    if (kill(p_mlat, SIGTERM) == 0) {
        airnav_log_level(3, "Succesfully stopped MLAT!\n");
        sleep(2);
        sendStats();
        return;
    } else {
        airnav_log_level(3, "Error stopping MLAT.\n");
        return;
    }

    return;
}


#ifdef RBCS

/*
 * Thread to monitor VHF LED status using named pipe
 */
void *airnavMonitorVhfLed(void *argv) {
    MODES_NOTUSED(argv);
    airnav_log_level(2, "Starting VHF Led thread...\n");

    char buf = 0;
    int status;
    int fd = -1;
    int last = 0;

    if (vhf_pipe == NULL) {
        return NULL;
    }

    last = 0;
    led_off(LED_VHF);

    while (!Modes.exit) {

        if (fd > -1) {

            status = read(fd, &buf, sizeof (char));
            //airnav_log_level(2,"Valor lido: %d \n",(int)buf);
            if (last != (int) buf) {
                last = (int) buf;
                if (last > 0) {
                    //set_led("g9");
                    led_on(LED_VHF);
                    airnav_log_level(2, "Ligando LED!!!\n");
                } else {
                    //set_led("-g9");
                    led_off(LED_VHF);
                    airnav_log_level(2, "DesLigando LED!!!\n");
                }

            }

        } else {
            fd = open(vhf_pipe, O_RDONLY | O_NONBLOCK);
            last = 0;
            //set_led("-g9");
            led_off(LED_VHF);
        }

        usleep(250000);
    }

    //free(path);
    return NULL;
}
#endif

/*
 * Send client configuration to server
 */
int sendConfig() {

    return 0;
}

/*
 * Return my IP
 */
char *getLocalIp() {
    int fd;
    struct ifreq ifr;
    char *out = calloc(1, 60);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    /* I want to get an IPv4 IP address */
    ifr.ifr_addr.sa_family = AF_INET;
    /* I want IP address attached to "eth0" */
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);

    strcpy(out, inet_ntoa(((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr));
    return out;
}

/*
 * Check if ACARS is running
 */
int checkACARSRunning(void) {

    if (acars_pidfile == NULL) {
        return 0;
    }

    FILE *f = fopen(acars_pidfile, "r");
    if (f == NULL) {
        airnav_log_level(5, "ACARS pidfile (%s) does not exist.\n", acars_pidfile);
        return 0;
    } else {
        char tmp[20];
        memset(&tmp, 0, 20);
        fgets(tmp, 20, (FILE*) f);
        fclose(f);
        airnav_log_level(5, "Pid no arquivo: %s\n", tmp);
        if (strlen(tmp) > 2) {
            char* endptr;
            endptr = "0123456789";
            p_acars = strtoimax(tmp, &endptr, 10);
            if (kill(p_acars, 0) == 0) {
                return 1;
            } else {
                p_acars = 0;
                remove(acars_pidfile);
                return 0;
            }

        } else {

            return 0;
        }

    }
}

/*
 * Start MLAT, if not running
 */
void startACARS(void) {

    if (acars_pidfile == NULL) {
        airnav_log("ACARS PID file not defined.\n");
        return;
    }

    if (checkACARSRunning() != 0) {
        airnav_log_level(3, "Looks like ACARS is already running.\n");
        return;
    }


    if (acars_cmd == NULL) {
        airnav_log_level(3, "ACARS command line not defined.\n");
        return;
    }


    char *tmp_cmd = malloc(300);
    memset(tmp_cmd, 0, 300);

#ifdef RBCS
    sprintf(tmp_cmd, "/sbin/start-stop-daemon --start --make-pidfile --background --pidfile %s --exec %s -- -n %s -r %d %s", acars_pidfile, acars_cmd, acars_server, acars_device, acars_freqs);
#else        
    sprintf(tmp_cmd, "%s >/dev/null 2>&1 &", acars_cmd);
#endif        

    airnav_log_level(3, "Starting ACARS with this command: '%s'\n", tmp_cmd);

    //system(tmp_cmd);
    run_cmd2(tmp_cmd);
    sleep(3);

    if (checkACARSRunning() != 0) {
        airnav_log_level(3, "Ok, started! Pid is: %i\n", p_acars);
        sendStats();
    } else {

        airnav_log_level(3, "Error starting ACARS\n");
        p_acars = 0;
        sendStats();
    }

    return;
}

/*
 * Stop ACARS
 */
void stopACARS(void) {

    if (checkACARSRunning() == 0) {
        airnav_log_level(3, "ACARS is not running.\n");
        return;
    }
    if (kill(p_acars, SIGTERM) == 0) {
        checkACARSRunning();
        airnav_log_level(3, "Succesfull kill ACARS!\n");
        sleep(2);
        sendStats();
        return;
    } else {
        airnav_log_level(3, "Error killing ACARS.\n");
        return;
    }

    return;
}

/*
 * New function to save ini files
 */
int ini_saveGeneric(char *ini_file, char *section, char *key, char *value) {

    GKeyFile *localini;
    localini = g_key_file_new();
    GError *error = NULL;
    gsize length;


    if (access(ini_file, F_OK) == -1) {

        airnav_log_level(5, "File does not exists, let's try to create.\n");
        FILE *fp = NULL;
        fp = fopen(ini_file, "w");
        if (fp != NULL) {
            airnav_log_level(5, "File created successfull!\n");
            fprintf(fp, "\n");
            fclose(fp);
        }
    }



    if (g_key_file_load_from_file(localini, ini_file, G_KEY_FILE_KEEP_COMMENTS, &error) == FALSE) {
        airnav_log("Error loading ini file for save.\n");
        g_key_file_free(localini);

        if (error != NULL) {
            g_error_free(error);
        }
        return 0;
    }

    g_key_file_set_string(localini, section, key, value);

    gchar *tmpdata;
    tmpdata = g_key_file_to_data(localini, &length, &error);


    FILE *inif;
    if ((inif = fopen(ini_file, "w")) == NULL) {
        airnav_log_level(2, "Can't open ini file for writting\n");
        if (tmpdata != NULL) {
            free(tmpdata);
        }
        return 0;
    }
    fprintf(inif, "%s", tmpdata);
    fclose(inif);
    airnav_log_level(2, "Done saving ini file!\n");

    g_key_file_free(localini);
    if (error != NULL) {
        g_error_free(error);
    }

    if (tmpdata != NULL) {
        free(tmpdata);
    }

    return 1;
}

int ini_saveInteger(char *ini_file, char *section, char *key, int value) {

    GKeyFile *localini;
    localini = g_key_file_new();
    GError *error = NULL;
    gsize length;

    if (access(ini_file, F_OK) == -1) {

        airnav_log_level(5, "File does not exists, let's try to create.\n");
        FILE *fp = NULL;
        fp = fopen(ini_file, "w");
        if (fp != NULL) {
            airnav_log_level(5, "File created successfull!\n");
            fprintf(fp, "\n");
            fclose(fp);
        }
    }


    if (g_key_file_load_from_file(localini, ini_file, G_KEY_FILE_KEEP_COMMENTS, &error) == FALSE) {
        airnav_log("Error loading ini file for save.\n");
        g_key_file_free(localini);

        if (error != NULL) {
            g_error_free(error);
        }
        return 0;
    }

    g_key_file_set_integer(localini, section, key, value);

    gchar *tmpdata;
    tmpdata = g_key_file_to_data(localini, &length, &error);
    //airnav_log("Conteudo: \n%s\nTamanho: %d\n",tmpdata,length);


    FILE *inif;
    if ((inif = fopen(ini_file, "w")) == NULL) {
        airnav_log_level(2, "Can't open ini file for writting\n");
        if (tmpdata != NULL) {
            free(tmpdata);
        }
        return 0;
    }
    fprintf(inif, "%s", tmpdata);
    fclose(inif);
    airnav_log_level(2, "Done saving ini file!\n");

    g_key_file_free(localini);
    if (error != NULL) {
        g_error_free(error);
    }
    if (tmpdata != NULL) {
        free(tmpdata);
    }
    return 1;
}

int ini_saveDouble(char *ini_file, char *section, char *key, double value) {

    GKeyFile *localini;
    localini = g_key_file_new();
    GError *error = NULL;
    gsize length;

    if (access(ini_file, F_OK) == -1) {

        airnav_log_level(5, "File does not exists, let's try to create.\n");
        FILE *fp = NULL;
        fp = fopen(ini_file, "w");
        if (fp != NULL) {
            airnav_log_level(5, "File created successfull!\n");
            fprintf(fp, "\n");
            fclose(fp);
        }
    }


    if (g_key_file_load_from_file(localini, ini_file, G_KEY_FILE_KEEP_COMMENTS, &error) == FALSE) {
        airnav_log("Error loading ini file for save.\n");
        g_key_file_free(localini);

        if (error != NULL) {
            g_error_free(error);
        }
        return 0;
    }

    g_key_file_set_double(localini, section, key, value);

    gchar *tmpdata;
    tmpdata = g_key_file_to_data(localini, &length, &error);
    //airnav_log("Conteudo: \n%s\nTamanho: %d\n",tmpdata,length);


    FILE *inif;
    if ((inif = fopen(ini_file, "w")) == NULL) {
        airnav_log_level(2, "Can't open ini file for writting\n");
        return 0;
    }
    fprintf(inif, "%s", tmpdata);
    fclose(inif);
    airnav_log_level(2, "Done saving ini file!\n");

    g_key_file_free(localini);
    if (error != NULL) {
        g_error_free(error);
    }
    return 1;
}

/*
 * New function to read string from
 * ini file using GLIB
 */
void ini_getString(char **item, char *ini_file, char *section, char *key, char *def_value) {
    GKeyFile *localini;
    GError *error = NULL;
    localini = g_key_file_new();

    if (*item != NULL) {
        free(*item);
    }

    airnav_log_level(4, "Reading key %s.....\n", key);
    if (g_key_file_load_from_file(localini, ini_file, G_KEY_FILE_KEEP_COMMENTS, &error) == FALSE) {
        airnav_log("Error loading ini file (%s).\n", ini_file);
        g_key_file_free(localini);

        if (error != NULL) {
            g_error_free(error);
        }

        //*item =  def_value;
        if (def_value != NULL) {
            *item = malloc(strlen(def_value) + 1);
            strcpy(*item, def_value);
        }

        airnav_log_level(4, "Returning value: %s\n", *item);
        return;
    }


    *item = g_key_file_get_string(localini, section, key, &error);
    airnav_log_level(4, "Value: %s\n", *item);
    g_key_file_free(localini);

    if (error != NULL) {
        g_error_free(error);
    }


    if (*item == NULL) {
        if (def_value != NULL) {
            *item = malloc(strlen(def_value) + 1);
            strcpy(*item, def_value);
        }
    }
    airnav_log_level(4, "Returning value: %s\n", *item);
    return;

}

/*
 * New function check if section exists in .ini file
 */
int ini_hasSection(char *ini_file, char *section) {
    GKeyFile *localini;
    GError *error = NULL;
    localini = g_key_file_new();


    airnav_log_level(4, "Checking if group exsts '%s'.....\n", section);
    if (g_key_file_load_from_file(localini, ini_file, G_KEY_FILE_KEEP_COMMENTS, &error) == FALSE) {
        airnav_log("Error loading ini file (%s).\n", ini_file);
        g_key_file_free(localini);

        if (error != NULL) {
            g_error_free(error);
        }

        return 0;
    }

    if (g_key_file_has_group(localini, section)) {
        g_key_file_free(localini);
        return 1;
    } else {
        g_key_file_free(localini);
        return 0;
    }

    g_key_file_free(localini);


    return 0;

}

/*
 * New function to read double from
 * ini file using GLIB
 */
double ini_getDouble(char *ini_file, char *section, char *key, double def_value) {
    GKeyFile *localini;
    GError *error = NULL;
    double abc = 0;

    localini = g_key_file_new();

    if (g_key_file_load_from_file(localini, ini_file, G_KEY_FILE_KEEP_COMMENTS, &error) == FALSE) {
        airnav_log("Error loading ini file.\n");
        g_key_file_free(localini);
        if (error != NULL) {
            g_error_free(error);
        }
        return def_value;
    }

    abc = g_key_file_get_double(localini, section, key, &error);
    airnav_log_level(4, "Section: %s, Key: %s, value: %.5f\n", section, key, abc);
    g_key_file_free(localini);

    if (error != NULL) {
        if (error->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND || error->code == G_KEY_FILE_ERROR_INVALID_VALUE || error->code == G_KEY_FILE_ERROR_GROUP_NOT_FOUND) {
            g_error_free(error);
            return def_value;
        } else {
            g_error_free(error);
            return abc;
        }
    } else {
        return abc;
    }
}

/*
 * New function to read boolnea from
 * ini file using GLIB
 */
int ini_getBoolean(char *ini_file, char *section, char *key, int def_value) {
    GKeyFile *localini;
    GError *error = NULL;
    int abc = 0;

    localini = g_key_file_new();

    if (g_key_file_load_from_file(localini, ini_file, G_KEY_FILE_KEEP_COMMENTS, &error) == FALSE) {
        airnav_log("Error loading ini file.\n");
        g_key_file_free(localini);
        if (error != NULL) {
            g_error_free(error);
        }
        return def_value;
    }

    abc = g_key_file_get_boolean(localini, section, key, &error);
    g_key_file_free(localini);

    if (error != NULL) {
        if (error->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND || error->code == G_KEY_FILE_ERROR_INVALID_VALUE || error->code == G_KEY_FILE_ERROR_GROUP_NOT_FOUND) {
            g_error_free(error);
            return def_value;
        } else {
            g_error_free(error);
            return abc;
        }
    } else {
        return abc;
    }
}

/*
 * New function to read integer from
 * ini file using GLIB
 */
int ini_getInteger(char *ini_file, char *section, char *key, int def_value) {
    GKeyFile *localini;
    GError *error = NULL;
    int abc = 0;

    localini = g_key_file_new();

    if (g_key_file_load_from_file(localini, ini_file, G_KEY_FILE_KEEP_COMMENTS, &error) == FALSE) {
        airnav_log("Error loading ini file.\n");
        g_key_file_free(localini);
        if (error != NULL) {
            g_error_free(error);
        }
        return def_value;
    }

    abc = g_key_file_get_integer(localini, section, key, &error);
    airnav_log_level(4, "Section: %s, Key: %s, Value: %d, Default Value: %d\n", section, key, abc, def_value);
    g_key_file_free(localini);

    if (error != NULL) {
        airnav_log_level(4, "Erro is NOT NULL!!!!\n");
        airnav_log_level(4, "Error format: %s\n", error->message);
        if (error->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND || error->code == G_KEY_FILE_ERROR_INVALID_VALUE || error->code == G_KEY_FILE_ERROR_GROUP_NOT_FOUND) {
            g_error_free(error);
            return def_value;
        } else {
            g_error_free(error);
            return abc;
        }
    } else {
        return abc;
    }


}

/*
 * Stop and start VHF, if running
 */
void restartVhf() {

    if (checkVhfRunning() == 1) {
        stopVhf();
        startVhf();
    } else {

    }

}

/*
 * Stop and start VHF, if running
 */
void restartMLAT() {

    if (checkMLATRunning() == 1) {
        stopMLAT();
        startMLAT();
    }

}

/*
 * Stop and start ACARS, if running
 */
void restartACARS() {

    if (checkACARSRunning() == 1) {
        stopACARS();
        startACARS();
    }

}

/*
 * Send dump configuration to server
 */
void sendDumpConfig(void) {
    if (airnav_com_inited != 1) {
        return;
    }

    char tmp_s[500] = {0};
    char *t_dump_agc = NULL;
    char *t_dump_fix = NULL;
    char *t_dump_mode_ac = NULL;
    char *t_dump_dc_filter = NULL;
    char *t_dump_checkcrc = NULL;

    ini_getString(&t_dump_agc, configuration_file, "client", "dump_agc", "false");
    ini_getString(&t_dump_fix, configuration_file, "client", "dump_fix", "false");
    ini_getString(&t_dump_mode_ac, configuration_file, "client", "dump_mode_ac", "false");
    ini_getString(&t_dump_dc_filter, configuration_file, "client", "dump_dc_filter", "false");
    ini_getString(&t_dump_checkcrc, configuration_file, "client", "dump_check_crc", "false");

    sprintf(tmp_s, "{"
            "\"dump_gain\":%d,"
            "\"dump_agc\":%s,"
            "\"dump_fix\":%s,"
            "\"dump_mode_ac\":%s,"
            "\"dump_dc_filter\":%s,"
            "\"dump_check_crc\":%s,"
            "\"dump_ppm_error\":%d,"
            "\"dump_device\":%d"
            "}",
            ini_getInteger(configuration_file, "client", "dump_gain", -99),
            t_dump_agc,
            t_dump_fix,
            t_dump_mode_ac,
            t_dump_dc_filter,
            t_dump_checkcrc,
            ini_getInteger(configuration_file, "client", "dump_ppm_error", 0),
            ini_getInteger(configuration_file, "client", "dump_device", 0)
            );

    airnav_log_level(2, "Config a ser enviada: \n");
    airnav_log_level(2, "%s\n", tmp_s);
    struct p_data *tp = preparePacket();
    tp->cmd = 10;
    strcpy(tp->payload, tmp_s);
    tp->payload_set = 1;
    sendPacket(tp);

}

/*
 * Send Vhf configuration to server
 */
void sendVhfConfig(void) {
    if (airnav_com_inited != 1) {

        return;
    }

    char tmp_s[500] = {0};
    sprintf(tmp_s, "{"
            "\"config_file\":\"%s\","
            "\"icecast_host\":\"%s\","
            "\"icecast_user\":\"%s\","
            "\"icecast_pwd\":\"%s\","
            "\"icecast_port\":%d,"
            "\"mode\":\"%s\","
            "\"freqs\":\"%s\","
            "\"mountpoint\":\"%s\","
            "\"device\":%d,"
            "\"gain\":%.1f,"
            "\"squelch\":%d,"
            "\"correction\":%d,"
            "\"afc\":%d,"
            "\"autostart_vhf\":%s,"
            "\"vhf_cmd\":\"%s\","
            "\"pid\":\"%s\","
            "\"pipe\":\"%s\""
            "}",
            vhf_config_file,
            ice_host,
            ice_user,
            ice_pwd,
            ice_port,
            vhf_mode,
            vhf_freqs,
            ice_mountpoint,
            vhf_device,
            vhf_gain,
            vhf_squelch,
            vhf_correction,
            vhf_afc,
            (autostart_vhf == 1 ? "true" : "false"),
            vhf_cmd,
            vhf_pidfile,
            vhf_pipe
            );

    airnav_log_level(2, "Config Vhf a ser enviada: \n");
    airnav_log_level(2, "%s\n", tmp_s);
    struct p_data *tp = preparePacket();
    tp->cmd = 11;
    strcpy(tp->payload, tmp_s);
    tp->payload_set = 1;
    sendPacket(tp);

}

/*
 * Send MLAT configuration to server
 */
void sendMLATConfig(void) {
    if (airnav_com_inited != 1) {

        return;
    }

    char tmp_s[500] = {0};
    sprintf(tmp_s, "{"
            "\"mlat_cmd\":\"%s\","
            "\"server\":\"%s\","
            "\"pid\":\"%s\","
            "\"autostart_mlat\":%s"
            "}",
            mlat_cmd,
            mlat_server,
            mlat_pidfile,
            (autostart_mlat == 1 ? "true" : "false")
            );

    airnav_log_level(2, "Config a ser enviada: \n");
    airnav_log_level(2, "%s\n", tmp_s);
    struct p_data *tp = preparePacket();
    tp->cmd = 12;
    strcpy(tp->payload, tmp_s);
    tp->payload_set = 1;
    sendPacket(tp);

}

/*
 * Send ACARS configuration to server
 */
void sendACARSConfig(void) {
    if (airnav_com_inited != 1) {

        return;
    }

    char tmp_s[500] = {0};
    sprintf(tmp_s, "{"
            "\"acars_cmd\":\"%s\","
            "\"server\":\"%s\","
            "\"pid\":\"%s\","
            "\"autostart_acars\":%s,"
            "\"device\":%d,"
            "\"freqs\":\"%s\""
            "}",
            acars_cmd,
            acars_server,
            acars_pidfile,
            (autostart_acars == 1 ? "true" : "false"),
            acars_device,
            acars_freqs
            );

    airnav_log_level(2, "Config a ser enviada: \n");
    airnav_log_level(2, "%s\n", tmp_s);
    struct p_data *tp = preparePacket();
    tp->cmd = 13;
    strcpy(tp->payload, tmp_s);
    tp->payload_set = 1;
    sendPacket(tp);

}

#ifdef RBCSRBLC

int sunxi_gpio_init(void) {
    int fd;
    unsigned int addr_start, addr_offset;
    unsigned int PageSize, PageMask;


    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) {
        return SETUP_DEVMEM_FAIL;
    }

    PageSize = sysconf(_SC_PAGESIZE);
    PageMask = (~(PageSize - 1));

    addr_start = SW_PORTC_IO_BASE & PageMask;
    addr_offset = SW_PORTC_IO_BASE & ~PageMask;

    gpio_map = (void *) mmap(0, PageSize * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr_start);
    if (gpio_map == MAP_FAILED) {
        return SETUP_MMAP_FAIL;
    }

    SUNXI_PIO_BASE = (unsigned int) gpio_map;
    SUNXI_PIO_BASE += addr_offset;

    close(fd);

    return SETUP_OK;
}

int sunxi_gpio_set_cfgpin(unsigned int pin, unsigned int val) {

    unsigned int cfg;
    unsigned int bank = GPIO_BANK(pin);
    unsigned int index = GPIO_CFG_INDEX(pin);
    unsigned int offset = GPIO_CFG_OFFSET(pin);

    if (SUNXI_PIO_BASE == 0) {
        return -1;
    }

    struct sunxi_gpio *pio =
            &((struct sunxi_gpio_reg *) SUNXI_PIO_BASE)->gpio_bank[bank];


    cfg = *(&pio->cfg[0] + index);
    cfg &= ~(0xf << offset);
    cfg |= val << offset;

    *(&pio->cfg[0] + index) = cfg;

    return 0;
}

int sunxi_gpio_get_cfgpin(unsigned int pin) {

    unsigned int cfg;
    unsigned int bank = GPIO_BANK(pin);
    unsigned int index = GPIO_CFG_INDEX(pin);
    unsigned int offset = GPIO_CFG_OFFSET(pin);
    if (SUNXI_PIO_BASE == 0) {
        return -1;
    }
    struct sunxi_gpio *pio = &((struct sunxi_gpio_reg *) SUNXI_PIO_BASE)->gpio_bank[bank];
    cfg = *(&pio->cfg[0] + index);
    cfg >>= offset;

    return (cfg & 0xf);
}

int sunxi_gpio_output(unsigned int pin, unsigned int val) {

    unsigned int bank = GPIO_BANK(pin);
    unsigned int num = GPIO_NUM(pin);

    if (SUNXI_PIO_BASE == 0) {
        return -1;
    }
    struct sunxi_gpio *pio = &((struct sunxi_gpio_reg *) SUNXI_PIO_BASE)->gpio_bank[bank];

    if (val)
        *(&pio->dat) |= 1 << num;
    else
        *(&pio->dat) &= ~(1 << num);

    return 0;
}

int sunxi_gpio_input(unsigned int pin) {

    unsigned int dat;
    unsigned int bank = GPIO_BANK(pin);
    unsigned int num = GPIO_NUM(pin);

    if (SUNXI_PIO_BASE == 0) {
        return -1;
    }

    struct sunxi_gpio *pio = &((struct sunxi_gpio_reg *) SUNXI_PIO_BASE)->gpio_bank[bank];

    dat = *(&pio->dat);
    dat >>= num;

    return (dat & 0x1);
}

void sunxi_gpio_cleanup(void) {
    unsigned int PageSize;
    if (gpio_map == NULL)

        return;

    PageSize = sysconf(_SC_PAGESIZE);
    munmap((void*) gpio_map, PageSize * 2);
}

void led_on(int led) {

    sunxi_gpio_output(led, HIGH);
}

void led_off(int led) {
    sunxi_gpio_output(led, LOW);
}
#endif

char * xorencrypt(char * message, char * key) {
    size_t messagelen = strlen(message);
    size_t keylen = strlen(key);

    char * encrypted = malloc(messagelen + 1);

    int i;
    for (i = 0; i < messagelen; i++) {
        encrypted[i] = message[i] ^ key[i % keylen];
    }
    encrypted[messagelen] = '\0';

    return encrypted;
}

/*
 * Thread that wait for servers to connect
 */
void *thread_waitNewANRB(void *arg) {

    MODES_NOTUSED(arg);

    int socket_desc, client_sock, c, slot;
    struct sockaddr_in server, client;

    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, sigpipe_handler);
    //Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_desc == -1) {
        printf("Could not create socket");
    }
    // Set reusable option
    int iSetOption = 1;
    setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, (char*) &iSetOption, sizeof (iSetOption));

    //Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(anrb_port);

    //Bind
    if (bind(socket_desc, (struct sockaddr *) &server, sizeof (server)) < 0) {
        airnav_log("Bind failed on ANRB channel. Port: %d\n", anrb_port);
        return NULL;
        //  exit(EXIT_FAILURE);
    }

    //Listen
    listen(socket_desc, MAX_ANRB);

    if (fcntl(socket_desc, F_GETFL) & O_NONBLOCK) {
        // socket is non-blocking
        airnav_log_level(2, "Socket is in non-blocking mode!\n");
    } else {
        airnav_log_level(2, "Socket is in blocking mode!\n");
    }

    //Accept and incoming connection
    airnav_log("Socket for ANRB created. Waiting for connections on port %d\n", anrb_port);
    c = sizeof (struct sockaddr_in);

    fd_set readSockSet;
    struct timeval timeout;
    int retval = -1;

    while (!Modes.exit) {


        FD_ZERO(&readSockSet);
        FD_SET(socket_desc, &readSockSet);
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;


        retval = select(socket_desc + 1, &readSockSet, NULL, NULL, &timeout);
        if (retval > 0) {
            if (FD_ISSET(socket_desc, &readSockSet)) {
                airnav_log("TCP Client (ANRB) requesting to connect...\n");

                client_sock = accept(socket_desc, (struct sockaddr *) &client, (socklen_t*) & c);
                slot = getNextFreeANRBSlot();

                if (slot > -1) {
                    //new_sock = malloc(sizeof (int));
                    //*new_sock = client_sock;
                    pthread_mutex_lock(&m_anrb_list);
                    //serverList[slot].socket = malloc(sizeof (int));
                    *anrbList[slot].socket = client_sock;
                    anrbList[slot].active = 1;
                    //anrbList[slot].packets = NULL;
                    pthread_mutex_unlock(&m_anrb_list);
                    airnav_log("[Slot %d] New ANRB connection from IP %s, remote port %d, socket: %d\n", slot, inet_ntoa(client.sin_addr), ntohs(client.sin_port), client_sock);
                    enable_keepalive(client_sock);

                    if (pthread_create(&anrbList[slot].s_thread, NULL, thread_handler_ANRBData, (void*) anrbList[slot].socket) < 0) {
                        perror("could not create [anrb] thread");
                        //exit(1);
                    }

                } else {
                    airnav_log("No more slot for anrb connection.\n");
                }


            }

        } else if (retval < 0) {
            airnav_log("Unknow error while waiting for connection...\n");
        }
        usleep(1000);
    }

    airnav_log_level(2, "Exited newServers Successfull!\n");
    return NULL;

}

short getNextFreeANRBSlot(void) {

    for (int i = 0; i < MAX_ANRB; i++) {
        if (anrbList[i].active == 0) {
            return i;
        }
    }

    return -1;
}

/*
 * This will handle server connections
 * */
void *thread_handler_ANRBData(void *socket_desc) {

    //Get the socket descriptor
    int *sock = NULL;
    sock = socket_desc;
    int read_size = 0;
    char *temp_char = malloc(4097);
    int slot = -1;
    int abort = 0;
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, sigpipe_handler);

    if (pthread_detach(pthread_self()) != 0) {
        airnav_log("Erro detaching thread.\n");
    } else {
        airnav_log_level(2, "Detach successfull!\n");
    }

    // Find which slot we are!
    airnav_log_level(2, "Sock no data handler: %d\n", *sock);
    pthread_mutex_lock(&m_anrb_list);
    for (int j = 0; j < MAX_ANRB; j++) {
        if (anrbList[j].socket != NULL) {
            if (*anrbList[j].socket == *sock) {
                slot = j;
                break;
            }
        }
    }
    pthread_mutex_unlock(&m_anrb_list);

    if (slot > -1) {

        airnav_log_level(5, "Slot for this anrb connection is: %d\n", slot);

        // Send initial info
        char *tmp_vers = malloc(64);
        sprintf(tmp_vers, "$VER,%s", c_version_str);
        char *ver = xorencrypt(tmp_vers, xorkey);
        gchar *base = g_base64_encode((const guchar*) ver, strlen(ver));
        airnav_log_level(2, "Base64: %s\n", base);
        int tamanho = strlen(base);

        send(*sock, base, tamanho, 0);
        //send(*sock,(char*)"\n",1,0);
        send(*sock, &txend[0], 1, 0);
        send(*sock, &txend[1], strlen((char*) "*"), 0);
        free(tmp_vers);
        free(ver);
        g_free(base);





        // Initial com
        while (abort != 1) {

            if (Modes.exit) {
                abort = 1;
            }

            // Let's clear the incoming buffer, if exists
            read_size = recv(*sock, temp_char, 4096, MSG_DONTWAIT);
            //airnav_log_level(3, "READ-SIZE for slot %d: %d, sock value: %d, sock address: %p\n", slot, read_size, *sock, sock);
            if (read_size != 0) { // Connection still active
                usleep(10000);
            } else {
                abort = 1;
                break;
            }


        }
    } else {
        airnav_log("[anrb_handle] Invalid slot for this anrb (%d)\n", slot);
    }

    airnav_log_level(3, "ANRB [%d] disconnected. Cleaning lists...\n", slot);
    pthread_mutex_lock(&m_anrb_list);
    close(*anrbList[slot].socket);
    *anrbList[slot].socket = -1;
    // Let's clear some info and free memory    
    anrbList[slot].active = 0;
    anrbList[slot].port = 0;

    airnav_log("ANRB at slot %d disconnected.\n", slot);
    pthread_mutex_unlock(&m_anrb_list);
    free(temp_char);
    pthread_exit(EXIT_SUCCESS);

}

/*
 * Send a packet to server socket (PIT/PTA format)
 */
int sendANRBPacket(void *socket_, struct p_data *pac) {

#define SP_BUF_SUZE 4096
    int sock = *(int*) socket_;
    char *buf_local = malloc(SP_BUF_SUZE);
    memset(buf_local, 0, SP_BUF_SUZE);

    char callsign[12] = {0};
    char altitude[20] = {0};
    char gnd_spd[10] = {0};
    char heading[10] = {0};
    char vrate[10] = {0};
    char lat[30] = {0};
    char lon[30] = {0};
    char ias[10] = {0};
    char squawk[10] = {0};
    char prefix[10] = {0};
    char modes[30] = {0};
    int sendd = 0;
    char p_airborne[2] = {""};

    if (pac->callsign_set == 1) {
        memcpy(&callsign, &pac->callsign, 9);
        sendd = 1;
    }

    if (pac->altitude_set == 1) {
        sprintf(altitude, "%d", pac->altitude);
        sendd = 1;
    }

    if (pac->gnd_speed_set == 1) {
        sprintf(gnd_spd, "%d", pac->gnd_speed * 10);
        sendd = 1;
    }

    if (pac->heading_set == 1) {
        sprintf(heading, "%d", pac->heading * 10);
        sendd = 1;
    }

    if (pac->vert_rate_set == 1) {
        sprintf(vrate, "%d", pac->vert_rate * 10);
        sendd = 1;
    }

    if (pac->position_set == 1) {
        sprintf(lat, "%.13f", pac->lat);
        sprintf(lon, "%.13f", pac->lon);
        sendd = 1;
    }

    if (pac->ias_set == 1) {
        sprintf(ias, "%d", pac->ias * 10);
        sendd = 1;
    }

    if (pac->squawk_set == 1) {
        sprintf(squawk, "%04x", pac->squawk);
        sendd = 1;
    }

#ifdef RBCS
    sprintf(prefix, "RBCS");
#elif RBLC
    sprintf(prefix, "RBLC");
#else
    sprintf(prefix, "RPI");
#endif


    if (pac->modes_addr_set == 1) {

        sendd = 1;
        if (pac->modes_addr & MODES_NON_ICAO_ADDRESS) {
            airnav_log_level(3, "Invalid ICAO code.\n");
        } else {
            sprintf(modes, "%06X", (pac->modes_addr & 0xffffff));

            if (strlen(modes) > 6) {
                airnav_log_level(3, "HEX maior que 6. Tamanho: %d, valor: '%s'\n", strlen(modes), modes);
                memset(&modes, 0, 30);
            }
        }


    }

    if (pac->airborne_set == 1) {
        if (pac->airborne == 1) {
            sprintf(p_airborne, "1");
        } else {
            sprintf(p_airborne, "0");
        }
    }

    // Timestamp packet
    time_t timer;
    struct tm* tm_info;
    time(&timer);
    tm_info = localtime(&timer);
    strftime(pac->p_timestamp, 20, "%Y%m%d%H%M%S", tm_info);
    pac->p_timestamp[20] = '\0';

    sprintf(buf_local,
            "$PTA,%06X,%s," //
            "%-s," // Callsign      
            "%s," // Altitude
            "%s," // ground Speed
            "%s," // Heading
            "%s,," // VRate
            "%s," // Lat
            "%s," // Lon
            "%s," // IAS
            "%s,," // Squawk
            "%s,," // Airborne
            ""
            ,
            pac->modes_addr, pac->p_timestamp,
            callsign,
            altitude,
            gnd_spd,
            heading,
            vrate,
            lat,
            lon,
            ias,
            squawk,
            p_airborne
            );



    if (sendd == 1) {
        // airnav_log("Sending.....\n");
        int tamanho = strlen(buf_local);
        char *abc = xorencrypt(buf_local, xorkey);
        gchar *base = g_base64_encode((const guchar*) abc, tamanho);

        send(sock, base, strlen(base), 0);

        send(sock, &txend[0], 1, 0);
        send(sock, &txend[1], strlen((char*) "*"), 0);

        airnav_log_level(2, "Sent: %s\n", base);
        free(abc);

        g_free(base);

    }
    free(buf_local);

    return 1;
}

/*
 * Return if there is any ANRB connected or not
 */
int isANRBConnected(void) {

    for (int i = 0; i < MAX_ANRB; i++) {
        if (anrbList[i].active == 1) {
            return 1;
        }
    }

    return 0;
}

/*
 * Thread to send data
 */
void *thread_SendDataANRB(void *argv) {
    MODES_NOTUSED(argv);

    signal(SIGPIPE, sigpipe_handler);
    struct packet_list *tmp1, *local_list;
    int l_packets_total = 0;
    int l_packets_last = 0;

    uint64_t now = mstime();
    int invalid_counter = 0;


    while (!Modes.exit) {

        pthread_mutex_lock(&m_copy2);
        local_list = flist2;
        flist2 = NULL;
        pthread_mutex_unlock(&m_copy2);

        l_packets_total = 0;
        l_packets_last = 0;
        now = mstime();

        while (local_list != NULL) {
            if (local_list->packet != NULL) {
                if ((now - local_list->packet->timestp) > 60000) {
                    airnav_log("Address %06X invalid (more than 60 seconds timestamp). Now: $llu, packet timestamp: %llu\n", local_list->packet->modes_addr,
                            now, local_list->packet->timestp);
                }
            }

            pthread_mutex_lock(&m_anrb_list);
            for (int i = 0; i < MAX_ANRB; i++) {
                if (anrbList[i].active == 1) {
                    if (local_list->packet != NULL) {
                        sendANRBPacket(anrbList[i].socket, local_list->packet);
                    }
                }
            }
            pthread_mutex_unlock(&m_anrb_list);

            if (local_list->packet != NULL) {
                free(local_list->packet);
            }

            tmp1 = local_list;
            local_list = local_list->next;
            FREE(tmp1);
        }

        sleep(1);
    }

    return NULL;
}

void sendSystemVersion(void) {

    if (airnav_com_inited != 1) {
        return;
    }

    struct utsname buf;
    int res = uname(&buf);
    if (res != 0) {
        return;
    }

    char tmp_s[500] = {0};


    sprintf(tmp_s, "{"
            "\"sysname\":\"%s\","
            "\"nodename\":\"%s\","
            "\"release\":\"%s\","
            "\"version\":\"%s\","
            "\"machine\":\"%s\""
            "}",
            buf.sysname,
            buf.nodename,
            buf.release,
            buf.version,
            buf.machine
            );

    airnav_log_level(2, "Config a ser enviada: \n");
    airnav_log_level(2, "%s\n", tmp_s);
    struct p_data *tp = preparePacket();
    tp->cmd = 31;
    strcpy(tp->payload, tmp_s);
    tp->payload_set = 1;
    sendPacket(tp);



}

static char *trimWhiteSpace(char *string) {

    if (string == NULL) {
        return NULL;
    }

    while (isspace(*string)) {
        string++;
    }

    if (*string == '\0') {
        return string;
    }

    char *end = string;

    while (*end) {
        ++end;
    }
    --end;

    while ((end > string) && isspace(*end)) {
        end--;
    }

    *(end + 1) = 0;
    return string;
}

static uint32_t getCPUSerial(void) {

    if ((strcmp(F_ARCH, "raspberry") == 0) || (strcmp(F_ARCH, "rblc2") == 0)) {
        uint32_t serial2 = 0;

        FILE *fp = fopen("/proc/cpuinfo", "r");

        if (fp == NULL) {
            airnav_log_level(1, "Error opening CPU Information for serial retrieve.\n");
            return 0;
        }

        char entry[80];
        while (fgets(entry, sizeof (entry), fp) != NULL) {
            char* saveptr = NULL;
            char *key = trimWhiteSpace(strtok_r(entry, ":", &saveptr));
            char *value = trimWhiteSpace(strtok_r(NULL, ":", &saveptr));

            if (strcasecmp("Serial", key) == 0) {
                //serial = strtoul(value, NULL, 16);
                serial2 = strtoul(value, NULL, 16);
            }
        }

        fclose(fp);

        airnav_log_level(1, "CPU Serial: %016"PRIx32"\n", serial2);

        return serial2;
    }

    return 0;

}

static char *getCPUSerial_RBLC(void) {

    if (strcmp(F_ARCH, "rblc") == 0) {
        FILE *fp = fopen("/proc/cpuinfo", "r");

        if (fp == NULL) {
            airnav_log_level(1, "Error opening CPU Information for serial retrieve.\n");
            return NULL;
        }

        char entry[80];
        while (fgets(entry, sizeof (entry), fp) != NULL) {
            char* saveptr = NULL;
            char *key = trimWhiteSpace(strtok_r(entry, ":", &saveptr));
            char *value = trimWhiteSpace(strtok_r(NULL, ":", &saveptr));

            if (strcasecmp("Serial", key) == 0) {
                airnav_log_level(1, "CPU Serial: %s\n", value);
                return value;
            }
        }

        fclose(fp);



        return NULL;
    }

    return NULL;

}

int file_exist(char *filename) {
    struct stat buffer;
    return (stat(filename, &buffer) == 0);
}

static uint64_t getSerial2(void)
{
   static uint64_t serial = 0;

   FILE *filp;
   char buf[512];
   //char term;

   filp = fopen ("/proc/cpuinfo", "r");

   if (filp != NULL)
   {
      while (fgets(buf, sizeof(buf), filp) != NULL)
      {
         if (!strncasecmp("serial\t\t:", buf, 9))
         {
            sscanf(buf+9, "%Lx", &serial);            
         }
      }

      fclose(filp);
   }
   
   return serial;
}
