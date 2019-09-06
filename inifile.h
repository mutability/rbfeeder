/* 
 * File:   inifile.h
 *
 * Created on 5 de Fevereiro de 2017, 11:25
 */

#ifndef INIFILE_H
#define INIFILE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sys/file.h>


#ifndef WIN32
#include <unistd.h>
#include <errno.h>
#define FILE_FLAGS_RD	O_RDONLY
#define FILE_FLAGS_RW	O_RDWR|O_CREAT|O_TRUNC
#define strcmpi	strcasecmp
#define strnicmp strncasecmp
#else
#include <windows.h>
#include <io.h>
#include <sys/locking.h>
#include <share.h>
#include <process.h>
#define FILE_FLAGS_RD	O_RDONLY|O_TEXT
#define FILE_FLAGS_RW	O_RDWR|O_TEXT|O_CREAT|O_TRUNC
#endif

static char inifile[512],lockfile[512];
static volatile int lockhan;

static void (*log_message)(char *);

#define MAX_INI_FILE_SIZE	2097152


int backup_ini(char *inifile);
int restore_ini(char *inifile,int ret);
static void doinilock(char *sec,char *key,char *val);
static void doiniunlock(void);
void setlockfile(char *filename);
void setinifile(char *filename);
void setinifile1(char *filename);
char *getinifilename(char *filename);
int search_section(char *buf,int start);
int getinifile(char *section,char *entry,char *ret);
int getsectionini(char *section,char *res,int len);
int getinifileskip(char *section,char *entry,char *ret,int skip);
int getinilineskip(char *section,char *returnentry,char *ret,int skip);
int getvalini(char *sec,char *key);
int getvaliniskip(char *sec,char *key,int skip);
int getboolini(char *sec,char *key);
int getstringini(char *sec,char *key,char *val);
static int local_getmultifile(char *section,char *entry,char *ret,int *multi_len,char *multi_buf);
static int local_getvalmulti(char *sec,char *key,int *multi_len,char *multi_buf);
static int local_openmulti(int *multi_filelen,char *multi_buf);
static int local_closemulti(int *multi_filelen, char *multi_buf);
static int local_setstringmulti(char *section,char *entry,char *val,int *multi_len,char *multi_buf);
static int local_setstringini(char *section,char *entry,char *val);
int setstringini(char *section,char *entry,char *val);
static int local_setsectionini(char *section,char *block);
int setsectionini(char *section,char *block);
int ini_increasehit(char *section,char *entry);
int ini_read_increasehit(char *section,char *entry);
int ini_multi_increasehit(int n,char *section[],char *entry[]);
int ini_multi_setstring(int n,char *section[],char *entry[],char *val[]);
int ini_set_log_message(void (*log)(char *));


#endif /* INIFILE_H */

