/* 
 * File:   airnav.h 
 *
 * Created on 24 de Março de 2017, 15:13
 */

#ifndef AIRNAV_H
#define AIRNAV_H

#define airnav_log_level(...) airnav_log_level_m( __FUNCTION__ , __VA_ARGS__)

//GPIO 1
#define PIN_PG0		SUNXI_GPG(0)
#define PIN_PG1		SUNXI_GPG(1)
#define PIN_PG2		SUNXI_GPG(2)
#define PIN_PG3		SUNXI_GPG(3)
#define PIN_PG4		SUNXI_GPG(4)
#define PIN_PG5		SUNXI_GPG(5)
#define PIN_PG6		SUNXI_GPG(6)
#define PIN_PG7		SUNXI_GPG(7)
#define PIN_PG8		SUNXI_GPG(8)
#define PIN_PG9		SUNXI_GPG(9)
#define PIN_PG10	SUNXI_GPG(10)
#define PIN_PG11	SUNXI_GPG(11)
//#define PIN_PD26	SUNXI_GPD(26)
//#define PIN_PD27	SUNXI_GPD(27)

//GPIO 2
//#define PIN_PB0		SUNXI_GPB(0)
#define PIN_PE0		SUNXI_GPE(0)
//#define PIN_PB1		SUNXI_GPB(1)
#define PIN_PE1		SUNXI_GPE(1)
#define PIN_PI0		SUNXI_GPI(0)
#define PIN_PE2		SUNXI_GPE(2)
#define PIN_PI1		SUNXI_GPI(1)
#define PIN_PE3		SUNXI_GPE(3)
#define PIN_PI2		SUNXI_GPI(2)
#define PIN_PE4		SUNXI_GPE(4)
#define PIN_PI3		SUNXI_GPI(3)
#define PIN_PE5		SUNXI_GPE(5)
#define PIN_PI10	SUNXI_GPI(10)
#define PIN_PE6		SUNXI_GPE(6)
#define PIN_PI11	SUNXI_GPI(11)
#define PIN_PE7		SUNXI_GPE(7)
#define PIN_PC3		SUNXI_GPC(3)
#define PIN_PE8		SUNXI_GPE(8)
#define PIN_PC7		SUNXI_GPC(7)
#define PIN_PE9		SUNXI_GPE(9)
#define PIN_PC16	SUNXI_GPC(16)
#define PIN_PE10	SUNXI_GPE(10)
#define PIN_PC17	SUNXI_GPC(17)
#define PIN_PE11	SUNXI_GPE(11)
#define PIN_PC18	SUNXI_GPC(18)
#define PIN_PI14	SUNXI_GPI(14)
#define PIN_PC23	SUNXI_GPC(23)
#define PIN_PI15	SUNXI_GPI(15)
#define PIN_PC24	SUNXI_GPC(24)
#define PIN_PB23	SUNXI_GPB(23)
#define PIN_PB22	SUNXI_GPB(22)

//GPIO 3
#define PIN_PH0		SUNXI_GPH(0)
#define PIN_PB3		SUNXI_GPB(3)
#define PIN_PH2		SUNXI_GPH(2)
#define PIN_PB4		SUNXI_GPB(4)
#define PIN_PH7		SUNXI_GPH(7)
#define PIN_PB5		SUNXI_GPB(5)
#define PIN_PH9		SUNXI_GPH(9)
#define PIN_PB6		SUNXI_GPB(6)
#define PIN_PH10	SUNXI_GPH(10)
#define PIN_PB7		SUNXI_GPB(7)
#define PIN_PH11	SUNXI_GPH(11)
#define PIN_PB8		SUNXI_GPB(8)
#define PIN_PH12	SUNXI_GPH(12)
#define PIN_PB10	SUNXI_GPB(10)
#define PIN_PH13	SUNXI_GPH(13)
#define PIN_PB11	SUNXI_GPB(11)
#define PIN_PH14	SUNXI_GPH(14)
#define PIN_PB12	SUNXI_GPB(12)
#define PIN_PH15	SUNXI_GPH(15)
#define PIN_PB13	SUNXI_GPB(13)
#define PIN_PH16	SUNXI_GPH(16)
#define PIN_PB14	SUNXI_GPB(14)
#define PIN_PH17	SUNXI_GPH(17)
#define PIN_PB15	SUNXI_GPB(15)
#define PIN_PH18	SUNXI_GPH(18)
#define PIN_PB16	SUNXI_GPB(16)
#define PIN_PH19	SUNXI_GPH(19)
#define PIN_PB17	SUNXI_GPB(17)
#define PIN_PH20	SUNXI_GPH(20)
#define PIN_PH24	SUNXI_GPH(24)
#define PIN_PH21	SUNXI_GPH(21)
#define PIN_PH25	SUNXI_GPH(25)
#define PIN_PH22	SUNXI_GPH(22)
#define PIN_PH26	SUNXI_GPH(26)
#define PIN_PH23	SUNXI_GPH(23)
#define PIN_PH27	SUNXI_GPH(27)


#define LED_ADSB    PIN_PG6
#define LED_STATUS  PIN_PG4
#define LED_GPS     PIN_PG8
#define LED_PC      PIN_PG5
#define LED_ERROR   PIN_PG7
#define LED_VHF     PIN_PG9
#define RF_SW1      PIN_PE8
#define RF_SW2      PIN_PE9


#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>
#include <inttypes.h>
#include "md5.h"
#include <math.h>
#include <curl/curl.h>
#include <errno.h>
#include <sys/stat.h>
#include "salsa20.h"
#include "util.h"
#include <spawn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <bits/signum.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <net/if.h>
#include <glib.h>
#include <string.h>
#include <sys/utsname.h>
#include "gpio_lib.h"


#ifdef RBCSRBLC
#include <linux/i2c-dev.h>

#ifdef RBCS
#include <gps.h>
struct gps_data_t gps_data_airnav;
#endif
#define KEY_FILE "/radarbox/client/key"

#endif



char *vhf_pipe;


#define FREE(ptr) do{ free((ptr)); (ptr) = NULL;  }while(0);


#include <sys/mman.h>
#define WORD64 int64_t
#define UWORD64 uint64_t
#define WORD32 int32_t
#define UWORD32 uint32_t
#define WORD16 int16_t
#define UWORD16 uint16_t
#define _atoi64(val) ((int64_t)atoll(val))
#define GPIO_BASE	(0x1c20800)
#define GPIO_SIZE	(0x8000)
static void *mapped_base;
static uint32_t pagesize;
static int mapped_pages;
static void *gpio_base;



// Definitions
#define AIRNAV_INIFILE "/etc/rbfeeder.ini"
#define AIRNAV_SEND_INTERVAL 2 // 2 second
#define AIRNAV_MONITOR_SECONDS 30 // Check is connection is valid every X seconds
#define AIRNAV_LAST_RECEIVED_TIMEOUT 120 // If last received message is more then X seconds, re-stabilish connection
#define BUFFLEN 4096
#define AIRNAV_WAIT_PACKET_TIMEOUT 10 // Wwait X seconds for a packet from server (waiting response)
#define AIRNAV_MAX_ITEM_AGE 3000 // 3 Seconds
#define AIRNV_STATISTICS_INTERVAL 30
#define UPDATE_CHECK_TIME 3600 // 1 hour, or 3600 seconds
#define AIRNAV_STATS_SEND_TIME 60
#define DEFAULT_MLAT_SERVER "mlat1.rb24.com:40900"
#define SEND_POSITION_TIME 600 // Send our position Every 10 minutes
#define MAX_ANRB 10



// This struct is only to organize local data before send.
// This struct will not be send this way!
struct p_data {        
    char    cmd;
    uint64_t    c_version;
    char    c_key[33];
    short   c_key_set;
    short   c_version_set;
    char        c_sn[30];
    int32_t modes_addr;
    short   modes_addr_set;
    char    callsign[9];
    short   callsign_set;
    int32_t altitude;
    short   altitude_set;
    double  lat;
    double  lon;
    short   position_set;
    short   heading;
    short   heading_set;
    short   gnd_speed;
    char    c_type;    
    short   c_type_set;
    short   gnd_speed_set;
    short   ias;
    short   ias_set;
    short   vert_rate;
    short   vert_rate_set;
    short   squawk;
    short   squawk_set;
    char   airborne;    
    short   airborne_set;
    char    payload[501]; // Payload for error and messages
    short   payload_set;
    uint64_t    timestp;
    char        p_timestamp[25];
    char        c_ip[20];
} airnav_packet;

struct packet_list {
    struct p_data *packet;
    struct packet_list *next;
};

struct s_anrb {
    int8_t               active;
    int32_t              port;
    pthread_t            s_thread;
    int                  *socket;
};


extern char **environ;

// Variables
int net_mode;
int airnav_com_inited; // Global variable to say if init comunication is stabilished or not
// External connection to get data
int external_port;
char *external_host;
char *local_input_port;
char *configuration_file;
//const char *local_input_port;
int last_success_received; // Time of last received packet
int airnav_socket;
struct sockaddr_in addr_airnav;
int *p_int_airnav;
char last_payload[501];
char last_cmd;
char *sharing_key;
char c_version_str[64]; // Store version in String format
//int32_t c_version_int; // Store version in integer format
uint64_t c_version_int; // Store version in integer format
int packet_list_count;
int packet_cache_count; // How many packets we have in cache
struct packet_list *flist, *flist2;
// New end of TX using multiple files
char txend[2];
int device_n;
int debug_level;
char *log_file;
char expected;
char expected_arrived;
// AirNAv server configuration
char *airnav_host;
int airnav_port;
//dictionary *ini;
long packets_total;
long packets_last;
extern char * program_invocation_name;
char *binpath;
char *sn;
char *xorkey;
struct s_anrb anrbList[MAX_ANRB];

int sats_visible;
int sats_used;
int fix_mode;
double max_cpu_temp;
double max_pmu_temp;


// GPS

int rc;
char gps_ok;
// GPS
double g_lat;
double g_lon;
int g_alt;
char gps_fixed;
char gps_led;
char error_led;
char led_adsb;

char daemon_mode;

char *pidfile;

char date_time_set;


// VHF
char *vhf_pidfile;
char *vhf_config_file;
char *ice_host;
char *ice_mountpoint;
int ice_port;
char *ice_user;
char *ice_pwd;
int vhf_device;
double vhf_gain;
int vhf_squelch;
int vhf_correction;
int vhf_afc;
char *vhf_mode;
char *vhf_freqs;
int autostart_vhf;
pid_t p_vhf;
char *vhf_cmd;
char mac_a[18];
char start_datetime[100];

int anrb_port;

// MLAT
pid_t p_mlat;
char *mlat_cmd;
char *mlat_server;
char *mlat_pidfile;
int autostart_mlat;
char *beast_out_port;
char *raw_out_port;
char *beast_in_port;
char *sbs_out_port;
char *mlat_config;

// ACARS
pid_t p_acars;
char *acars_pidfile;
char *acars_cmd;
char *acars_server;
int acars_device;
char *acars_freqs;
int autostart_acars;

int rf_filter_status;

// Encryption
uint8_t key[17];
uint8_t nonce[9];    

pthread_t t_ext_source;
pthread_t t_prepareData;
pthread_t t_monitor;
pthread_t t_statistics;
pthread_t t_update;
pthread_t t_stats;
pthread_t t_send_data;
pthread_t t_waitcmd;
pthread_t t_led_adsb;
pthread_t t_anrb;
pthread_t t_anrb_send;
#ifdef RBCS
pthread_t t_vhf_led;
#endif

// Mutexes
pthread_mutex_t m_packets_counter; // 
pthread_mutex_t m_socket; // Mutex socket
pthread_mutex_t m_copy; // Mutex copy
pthread_mutex_t m_copy2; // Mutex copy
pthread_mutex_t m_cmd; // Mutex copy
pthread_mutex_t m_led_adsb;
pthread_mutex_t m_ini;
pthread_mutex_t m_anrb_list;

// Function prototypes
void airnav_main();
void *airnav_extSourceProccess(void *arg);
void airnav_loadConfig(int argc, char **argv);
static void sigintHandler(int dummy);
static void sigtermHandler(int dummy);
void *airnav_prepareData(void *arg);
struct p_data *preparePacket(void);
int airnav_initial_com(void);
void *airnav_monitorConnection(void *arg);
int airnav_connect(void);
int sendKey(void);
int sendKeyRequest(void);
int waitCmd(int cmd);
int sendAck(void);

int getArraySize(char *array);
void *airnav_statistics(void *arg);
int sendPacket(struct p_data *pk);


void doUpdate(char payload[501]);
void call_realpath (char * argv0);
char *md5sumFile(char *fname);

void sendSystemVersion(void);

int get_page(const char* url, const char* file_name);
void closeCon();

// airnav_utils
void airnav_log(const char* format, ...);
void airnav_log_level_m(const char* fname,const int level, const char* format, ...);
int hostname_to_ip(char *hostname, char *ip);
void airnav_showHelp(void);
void *airnav_send_stats_thread(void *argv);
float getCPUTemp(void);
int check_bit(int number, int position);
static inline void set_bit(uint32_t *number, int position);
static inline void clear_bit(uint32_t *number, int position);
void *threadSendData(void *argv);
//void *thread_SendDataANRB(void *argv);
void enable_keepalive(int sock);
int procPacket2(char packet[BUFFLEN], int psize);
void *tWaitCmds(void * argv);
void sigpipe_handler();
pid_t run_cmd(const char *cmd);
void run_cmd2(char *cmd);
int sendStats(void);
void createPidFile(void);
void stopVhf(void);
void startVhf(void);
int checkVhfRunning(void);
int checkACARSRunning(void);
int saveVhfConfig(void);
int checkMLATRunning(void);
void startMLAT(void);
void stopMLAT(void);
void startACARS(void);
void stopACARS(void);
int sendConfig();
char *getLocalIp();
void restartVhf();
void restartMLAT();
void restartACARS();
void sendDumpConfig(void);
void sendVhfConfig(void);
void sendMLATConfig(void);
void sendACARSConfig(void);
int isANRBConnected(void);
void *thread_SendDataANRB(void *argv);


// New ini function
void ini_getString(char **item, char *ini_file, char *section, char *key, char *def_value);
//int ini_getInteger(char *section, char *key, int def_value);
//double ini_getDouble(char *section, char *key, double def_value);
//int ini_getBoolean(char *section, char *key, int def_value);
int ini_saveGeneric(char *ini_file, char *section, char *key, char *value);
int ini_saveInteger(char *ini_file,char *section, char *key, int value);
int ini_saveDouble(char *ini_file, char *section, char *key, double value);
int ini_hasSection(char *ini_file, char *section);
int ini_getInteger(char *ini_file, char *section, char *key, int def_value);
double ini_getDouble(char *ini_file, char *section, char *key, double def_value);
int ini_getBoolean(char *ini_file,char *section, char *key, int def_value);
long ini_getInteger64(char *ini_file, char *section, char *key, long def_value);
unsigned long ini_getUInteger64(char *ini_file, char *section, char *key, unsigned long def_value);
char * xorencrypt(char * message, char * key);
void *thread_waitNewANRB(void *arg);
short getNextFreeANRBSlot(void);
void *thread_handler_ANRBData(void *socket_desc);
int sendANRBPacket(void *socket_, struct p_data *pac);

int file_exist (char *filename);

static char *trimWhiteSpace(char *string);
static uint32_t getCPUSerial(void);
static char *getCPUSerial_RBLC(void);
static uint64_t getSerial2(void);
int saveMLATConfig(void);

#ifdef RBCSRBLC
int set_led(char *param);
int allwinner_init(void);
int allwinner_exit(void);
#ifdef RBCS
void updateGPSData(void);
#endif
void *thread_LED_ADSB(void *argv);
float getPMUTemp(void);
#ifdef RBCS
void *airnavMonitorVhfLed(void *argv);
int createVhfPipe(char pfile[250]);
#endif
void led_on(int led);
void led_off(int led);
#endif

// anrb.c
int command_server_connect(char *url);
int command_server_read(void);
int command_server_close(int flag);
int sockwrite_noblock(int sock,char *buf,int len);
int command_write_version(int flag);
int sockread(int sock,char *buf,int len);
int sockwrite(int sock,char *buf,int len);
int handle_command_buffer(char *buf1, int n, int flag);
int rb3_send_escape_buffer(int channel,int type,char *buffer,int len);
int rb3_send_escape_buffer_block(int channel,int type,char *buffer,int len);
int command_close(void);
int command_connect(char *url);


#endif /* AIRNAV_H */

