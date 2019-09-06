#include "inifile.h"

int backup_ini(char *inifile)
{
#ifndef NO_INI_BACKUP
	char file1[512],file2[512];
	char command[2048];
	int ret;

	ret=0;
#ifndef WIN32
	if (!getuid())
	{
#endif
		strcpy(file1,inifile);
		strcat(file1,".1");
		strcpy(file2,inifile);
		strcat(file2,".2");
		unlink(file2);
#ifdef WIN32
		sprintf(command,"copy \"%s\" \"%s\"",file1,file2);
#else
		sprintf(command,"cp -p %s %s",file1,file2);
#endif
		system(command);
#ifdef WIN32
		sprintf(command,"copy \"%s\" \"%s\"",inifile,file1);
#else
		sprintf(command,"cp -p %s %s",inifile,file1);
#endif
		ret=system(command);
		if (ret!=-1) ret=0;
#ifndef WIN32
	}
#endif
	return ret;
#else
	return 0;
#endif
}

int restore_ini(char *inifile,int ret)
{
	char file1[512],file2[512];
	char command[2048];
	int ret1;

	ret1=0;
/*
#ifndef WIN32
	if (!getuid())
	{
#endif
*/
		if (ret<0)
		{
			strcpy(file1,inifile);
			strcat(file1,".1");
			strcpy(file2,inifile);
			strcat(file2,".1");
			unlink(inifile);
#ifdef WIN32
			sprintf(command,"ren %s %s",file1,inifile);
			ret1=system(command);
			if (ret1>=0)
			{
				sprintf(command,"ren %s %s",file2,file1);
				ret1=system(command);
			}
#else
			sprintf(command,"mv %s %s",file1,inifile);
			ret1=system(command);
			if (ret1>=0)
			{
				sprintf(command,"mv %s %s",file2,file1);
				ret1=system(command);
			}
#endif
		}
/*
#ifndef WIN32
	}
#endif
*/
	return ret1;
}

// #ifdef WIN32
// static long rekursiv=0;
// #else
static void doinilock(char *sec,char *key,char *val)
{
	char buf[2048];
	int i,j;

//	strcpy(lockfile,inifile);
//	strcat(lockfile,".lock");
	j=0;
	if (lockhan>0 && log_message)
	{
		sprintf(buf,"doinilock(%s,%s,%s): lockhan already set before doinilock",sec,key,val);
		log_message(buf);
	}
	while (lockhan>0 && j<300) // in case of Multithreading another thread might hold lockhan, without check we'd lose this lockhan and can unlock the file
	{
#ifdef WIN32
		Sleep(10);
#else
		usleep(10000);
#endif
		j++;
	}
	if (lockhan<=0)
	do
	{
#ifdef WIN32
		lockhan=sopen(lockfile,O_CREAT|O_RDWR,SH_DENYNO,S_IREAD|S_IWRITE);
#else
		lockhan=open(lockfile,O_CREAT|O_RDWR,S_IREAD|S_IWRITE);
#endif
		if (lockhan>0)
		{
#ifdef WIN32
			i=locking(lockhan,LK_NBLCK,100);
#else
			i=flock(lockhan,LOCK_EX|LOCK_NB);
#endif
			if (i<0)
			{
				close(lockhan);
				lockhan=0;
#ifdef WIN32
				Sleep(10);
#else
				usleep(10000);
#endif
				j++;
			}
			else
			{
				sprintf(buf,"%10d [%s] %s=%s\n",getpid(),sec,key,val);
				write(lockhan,buf,strlen(buf));
			}
		}
		else 
		{
#ifdef WIN32
			Sleep(10);
#else
			usleep(10000);
#endif
			j++;
		}
	}
	while(lockhan<=0 && j<500); // max 5s wait
	if (lockhan<=0 && log_message)
	{
		sprintf(buf,"doinilock(%s,%s,%s): could not get lockhan, error: %d (%s)",sec,key,val, errno,strerror(errno));
		log_message(buf);
#ifdef WIN32
		sprintf(buf,"copy %s %s.locking",lockfile,lockfile);system(buf);
#else
		sprintf(buf,"cp -p %s %s.locking",lockfile,lockfile);system(buf);
#endif
	}
}

static void doiniunlock(void)
{

//	strcpy(lockfile,inifile);
//	strcat(lockfile,".lock");
	if (lockhan>0)
	{
#ifdef WIN32
		lseek(lockhan,0,SEEK_SET);
		locking(lockhan,LK_UNLCK,100);
#else
		flock(lockhan,LOCK_UN);
#endif
		close(lockhan);
		lockhan=0;
#ifndef NO_LOCK_DELETE
		unlink(lockfile);
#endif
	}
}
// #endif

void setlockfile(char *filename)
{
#ifndef WIN32
	if (!strncmp(filename,"/etc/",5))
	{
		char *p,*q;

		for(q=p=filename;*p;p++) if (*p=='/') q=p+1;
		strcpy(lockfile,"/var/run/");
		strcat(lockfile,q);
		strcat(lockfile,".lock");
	}
	else
	{
		strcpy(lockfile,filename);
		strcat(lockfile,".lock");
	}
#else
	strcpy(lockfile,filename);
	strcat(lockfile,".lock");
#endif
}

void setinifile(char *filename)
{
	int i;
	char buf[512];
	int getstringini(char *sec,char *key,char *val);

	strcpy(inifile,filename);
	i=getstringini("symbolic-link","target",buf);
	if (i>=0)
	{
		setinifile(buf);
	}
	setlockfile(inifile);
}

void setinifile1(char *filename)
{
	strcpy(inifile,filename);
	setlockfile(inifile);
}

char *getinifilename(char *filename)
{
	if (filename)
	{
		strcpy(filename,inifile);
	}
	return inifile;
}

int search_section(char *buf,int start)
{
	int i;
	char *p,*q;

	p=&buf[start];i=0;
	while(*p && !i)
	{
		while(*p==' ' && *p=='\t') p++;
		if (*p=='[')
		{
			i=1;
		}
		else
		{
			q=p;
			p=strchr(p,'\n');
			if (p)
			{
				if (*p=='\n' || *p=='\r') p++;
				if (*p=='\n' || *p=='\r') p++;
			}
			else
			{
				p=q;
				while(*p) p++;
			}
		}
	}
	return p-buf;
}

int getinifile(char *section,char *entry,char *ret)

{
	int i,j,k,l,m,n,f;
	char buf[MAX_INI_FILE_SIZE];
	char sec[64];

	if (!inifile[0]) setinifile("SeismoWeb.ini");
	k=-1;l=strlen(entry);
	if (section[0]!='[')
	{
		sec[0]='[';strcpy(&sec[1],section);n=strlen(sec);sec[n]=']';sec[n+1]=0;n++;
	}
	else
	{
		strcpy(sec,section);
		n=strlen(sec);
	}
	i=open(inifile,FILE_FLAGS_RD);
	if (i>0)
	{
		j=read(i,buf,sizeof(buf)-1);
		close(i);
		if (j>=0) buf[j]=0; else buf[0]=0;
		for (i=f=0;i<j;)
		{
			while(buf[i]==' ' || buf[i]=='\t') i++;
			if (!strnicmp(&buf[i],sec,n))
			{
				f=1;
				while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
				memmove(buf,&buf[i],sizeof(buf)-i);
				j-=i;
				i=search_section(buf,0);
				if (buf[i]=='[')
				{
					memset(&buf[i],0,sizeof(buf)-i);
					j=i;
				}
			}
			else
			{
				while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
			}
		}
		if (!f) buf[j=0]=0;
		for (i=0;i<j;)
		{
			while(buf[i]==' ' || buf[i]=='\t') i++;
			if (buf[i]!=';')
			{
				if (!strnicmp(&buf[i],entry,l))
				{
					i=i+l;
					while(buf[i]==' ' || buf[i]=='\t') i++;
					if (buf[i]=='=')
					{
						i++;m=0;
						while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) ret[m++]=buf[i++];
						ret[m]=0;
						i=j;k=0;
					}
				}
			}
			while(buf[i] && buf[i]!='\n' && buf[i]!='\r' && i<j) i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
		}
	}
	return k;
}

int getsectionini(char *section,char *res,int len)
{
	int i,j,m,n,ret;
	char buf[MAX_INI_FILE_SIZE];
	char sec[64];

	ret=-1;
	if (!inifile[0]) setinifile("SeismoWeb.ini");
	//k=-1;
        sec[0]='[';strcpy(&sec[1],section);n=strlen(sec);sec[n]=']';sec[n+1]=0;n++;
	i=open(inifile,FILE_FLAGS_RD);
	if (i>0)
	{
		j=read(i,buf,sizeof(buf)-1);
		close(i);
		if (j>=0) buf[j]=0; else buf[0]=0;
		for (i=0;i<j;)
		{
			while(buf[i]==' ' || buf[i]=='\t') i++;
			if (!strnicmp(&buf[i],sec,n))
			{
				while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
				m=i;
				i=search_section(buf,m);
				if ((i-m+1)<len)
				{
					memcpy(res,&buf[m],i-m);
					res[i-m]=0;
					ret=i-m+1;
				}
			}
			else
			{
				while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
			}
		}
	}
	return ret;
}


int getinifileskip(char *section,char *entry,char *ret,int skip)

{
	int i,j,k,l,m,n,f,skipped;
	char buf[MAX_INI_FILE_SIZE];
	char sec[64];

	k=-1;l=strlen(entry);sec[0]='[';strcpy(&sec[1],section);n=strlen(sec);sec[n]=']';sec[n+1]=0;n++;
	skipped=0;
	i=open(inifile,FILE_FLAGS_RD);
	if (i>0)
	{
		j=read(i,buf,sizeof(buf)-1);
		close(i);
		if (j>=0) buf[j]=0; else buf[0]=0;
		for (i=f=0;i<j;)
		{
			while(buf[i]==' ' || buf[i]=='\t') i++;
			if (!strnicmp(&buf[i],sec,n))
			{
				f=1;
				while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
				memmove(buf,&buf[i],sizeof(buf)-i);
				j-=i;
				i=search_section(buf,0);
				if (buf[i]=='[')
				{
					memset(&buf[i],0,sizeof(buf)-i);
					j=i;
				}
			}
			else
			{
				while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
			}
		}
		if (!f) buf[j=0]=0;
		for (i=0;i<j;)
		{
			while(buf[i]==' ' || buf[i]=='\t') i++;
			if (buf[i]!=';')
			{
				if (!strnicmp(&buf[i],entry,l))
				{
					i=i+l;
					while(buf[i]==32) i++;
					if (buf[i]=='=')
					{
						if (skipped==skip)
						{
							i++;m=0;
							while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) ret[m++]=buf[i++];
							ret[m]=0;
							i=j;k=0;
						}
						else skipped++;
					}
				}
			}
			while(buf[i] && buf[i]!='\n' && buf[i]!='\r' && i<j) i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
		}
	}
	return k;
}


int getinilineskip(char *section,char *returnentry,char *ret,int skip)

{
	int i,j,k,l,m,n,skipped,f;
	char buf[MAX_INI_FILE_SIZE];
	char sec[64],entry[512];

	k=-1;sec[0]='[';strcpy(&sec[1],section);n=strlen(sec);sec[n]=']';sec[n+1]=0;n++;
	skipped=0;
	i=open(inifile,FILE_FLAGS_RD);
	if (i>0)
	{
		j=read(i,buf,sizeof(buf)-1);
		close(i);
		if (j>=0) buf[j]=0; else buf[0]=0;
		for (i=f=0;i<j;)
		{
			while(buf[i]==' ' || buf[i]=='\t') i++;
			if (!strnicmp(&buf[i],sec,n))
			{
				f=1;
				while(buf[i]!='\n' && buf[i]!='\r' && buf[i])
				{
					i++;
				}
				if (buf[i]=='\n' || buf[i]=='\r') i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
				memmove(buf,&buf[i],sizeof(buf)-i);
				j-=i;
				i=search_section(buf,0);
				if (buf[i]=='[')
				{
					memset(&buf[i],0,sizeof(buf)-i);
					j=i;
				}
			}
			else
			{
				while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
				if (buf[i]=='\n' || buf[i]=='\r') i++;
			}
		}
		if (!f) buf[j=0]=0;
		for (i=0;i<j;)
		{
			while(buf[i]==' ' || buf[i]=='\t') i++;
			if (buf[i]!=';')
			{
				l=0;
				while(buf[i] && buf[i]!='\n' && buf[i]!='\r' && buf[i]!=';' && buf[i]!='=')
				{
					entry[l++]=buf[i++];
				}
				entry[l]=0;
				while(buf[i]==32) i++;
				if (buf[i]=='=')
				{
					if (skipped==skip)
					{
						i++;m=0;
						while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) ret[m++]=buf[i++];
						ret[m]=0;
						strcpy(returnentry,entry);
						i=j;k=0;
					}
					else skipped++;
				}
			}
			while(buf[i] && buf[i]!='\n' && buf[i]!='\r' && i<j) i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
		}
		if (skipped<skip)
		{
			k=-99;
		}
	}
	return k;
}


int getvalini(char *sec,char *key)

{
	int i,j;
	char buf[4096];

	buf[0]=0;
	i=getinifile(sec,key,buf);
	if (!i)
	{
		for(j=0;buf[j]==32;j++);
		if (buf[j]=='0' && (buf[j+1]=='x' || buf[j+1]=='X'))
		{
			sscanf(&buf[j+2],"%x",&i);
		}
		else sscanf(&buf[j],"%d",&i);
	}
	else i=-32768;
	return i;
}

int getvaliniskip(char *sec,char *key,int skip)

{
	int i,j;
	char buf[256];

	buf[0]=0;
	i=getinifileskip(sec,key,buf,skip);
	if (!i)
	{
		for(j=0;buf[j]==32;j++);
		if (buf[j]=='0' && (buf[j+1]=='x' || buf[j+1]=='X'))
		{
			sscanf(&buf[j+2],"%x",&i);
		}
		else sscanf(&buf[j],"%d",&i);
	}
	return i;
}


int getboolini(char *sec,char *key)

{
 int i,j;
 char buf[4096];

 buf[0]=0;
 i=getinifile(sec,key,buf);
 if (!i)
  {
   i=-1;
   for(j=0;buf[j]==32;j++);
   if (!strcmpi(&buf[j],"YES")) i=1;
   else if (!strcmpi(&buf[j],"NO")) i=0;
   else if (!strcmpi(&buf[j],"TRUE")) i=1;
   else if (!strcmpi(&buf[j],"FALSE")) i=0;
   else if (!strcmpi(&buf[j],"ON")) i=1;
   else if (!strcmpi(&buf[j],"OFF")) i=0;
  }
 return i;
}

int getstringini(char *sec,char *key,char *val)

{
 int i,j;
 char buf[4096];

 val[0]=0;
 i=getinifile(sec,key,buf);
 if (!i)
  {
   for(j=0;buf[j]==32;j++);
   strcpy(val,&buf[j]);
  }
 return i;
}

static int local_getmultifile(char *section,char *entry,char *ret,int *multi_len,char *multi_buf)

{
	int i,j,k,l,m,n,f;char *buf,sec[64];
	int startpos,endpos;

	j=*multi_len;
	buf=multi_buf;
	if (j>=0) buf[j]=0; else buf[0]=0;
	k=-1;l=strlen(entry);
	if (section[0]!='[')
	{
		sec[0]='[';strcpy(&sec[1],section);n=strlen(sec);sec[n]=']';sec[n+1]=0;n++;
	}
	else
	{
		strcpy(sec,section);
		n=strlen(sec);
	}
	for (i=f=0;i<j;)
	{
		while(buf[i]==' ' || buf[i]=='\t') i++;
		if (!strnicmp(&buf[i],sec,n))
		{
			f=1;
			while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			startpos=i;
			i=search_section(buf,i);
			if (buf[i]=='[' || i>=j)
			{
				endpos=i;
			}
		}
		else
		{
			while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
		}
	}
	if (!f) startpos=endpos=j;
	for (i=startpos;i<endpos;)
	{
		while(buf[i]==' ' || buf[i]=='\t') i++;
		if (buf[i]!=';')
		{
			if (!strnicmp(&buf[i],entry,l))
			{
				i=i+l;
				while(buf[i]==' ' || buf[i]=='\t') i++;
				if (buf[i]=='=')
				{
					i++;m=0;
					while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) ret[m++]=buf[i++];
					ret[m]=0;
					i=j;k=0;
				}
			}
		}
		while(buf[i] && buf[i]!='\n' && buf[i]!='\r' && i<j) i++;
		if (buf[i]=='\n' || buf[i]=='\r') i++;
		if (buf[i]=='\n' || buf[i]=='\r') i++;
	}
	return k;
}

static int local_getvalmulti(char *sec,char *key,int *multi_len,char *multi_buf)

{
	int i,j;
	char buf[4096];

	buf[0]=0;
	i=local_getmultifile(sec,key,buf,multi_len,multi_buf);
	if (!i)
	{
		for(j=0;buf[j]==32;j++);
		if (buf[j]=='0' && (buf[j+1]=='x' || buf[j+1]=='X'))
		{
			sscanf(&buf[j+2],"%x",&i);
		}
		else sscanf(&buf[j],"%d",&i);
	}
	else i=-32768;
	return i;
}


static int local_openmulti(int *multi_filelen,char *multi_buf)
{
	int fd,filelen,ret;
	char *buf;

	ret=-1;
	buf=multi_buf;
	if (buf)
	{
		ret=0;
		fd=open(inifile,FILE_FLAGS_RD);
		if (fd>0)
		{
			filelen=read(fd,buf,MAX_INI_FILE_SIZE-1);
			close(fd);
		}
		else
		{
			filelen=0;
			buf[0]=0;
		}
		if (filelen>0) buf[filelen]=0; else buf[0]=0;
		*multi_filelen=filelen;
	}
	else 
	{
		ret=-1;
		buf[0]=0;
		*multi_filelen=0;
	}
	return ret;
}

static int local_closemulti(int *multi_filelen, char *multi_buf)
{
	int i, fd,ret,filelen;
	char *buf;

	buf=multi_buf;
	filelen=*multi_filelen;
	ret=backup_ini(inifile);
	if (!ret)
	{
		fd=open(inifile,FILE_FLAGS_RW,S_IREAD|S_IWRITE);
		if (fd>0)
		{
			i=write(fd,buf,filelen);
#ifdef WIN32
			if (i<filelen)
#else
			if (i!=filelen)
#endif
			{
				ret=-2;
			}
			i=close(fd);
			if (ret>=0 && i<0)
			{
				ret=-2;
			}
			restore_ini(inifile,ret);
		}
		else
		{
			ret=-2;
		}
	}
	return ret;
}


static int local_setstringmulti(char *section,char *entry,char *val,int *multi_len,char *multi_buf)
{
	int i,j,k,l,n,ret,filelen,startpos,endpos,entrystart,entryend;
	char *buf,sec[64],*p,temp[16384];

	ret=0;startpos=endpos=entrystart=entryend=0;
	k=-1;l=strlen(entry);sec[0]='[';strcpy(&sec[1],section);n=strlen(sec);sec[n]=']';sec[n+1]=0;n++;
	filelen=*multi_len;
	buf=multi_buf;
	if (!buf) return(-1);
	for (i=0;i<filelen;)
	{
		while(buf[i]==' ' || buf[i]=='\t') i++;
		if (!strnicmp(&buf[i],sec,n))
		{
			while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
			if (!buf[i]) filelen=i;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			startpos=i;
			i=search_section(buf,i);
			if (buf[i]=='[' || i>=filelen)
			{
				endpos=i;
			}
		}
		else
		{
			while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
			if (!buf[i]) filelen=i;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
		}
	}
	for (i=startpos,entrystart=entryend=0;i<endpos && !entryend;)
	{
		j=i;
		while((buf[i]==' ' || buf[i]=='\t') && i<endpos) i++;
		if (buf[i]!=';' && i<endpos)
		{
			if (!strnicmp(&buf[i],entry,l))
			{
				i=i+l;
				while(buf[i]==' ' || buf[i]=='\t') i++;
				if (buf[i]=='=')
				{
					entrystart=j;
					i++;//m=0;
					while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
					entryend=i;
				}
			}
		}
		while(buf[i] && buf[i]!='\n' && buf[i]!='\r' && i<endpos) i++;
		if (buf[i]=='\n' || buf[i]=='\r') i++;
		if (buf[i]=='\n' || buf[i]=='\r') i++;
	}
	if (entryend)	// entry exists, replace it
	{
		if (val)
		{
			sprintf(temp,"%s=%s",entry,val);
			j=strlen(temp);
			k=entryend-entrystart;
			if ((filelen+j-k)<MAX_INI_FILE_SIZE)
			{
				p=&buf[entrystart];
				if (j!=k)
				{
					memmove(&buf[entryend+j-k],&buf[entryend],filelen-entryend);
				}
				memcpy(p,temp,j);
				filelen+=j-k;
			}
			else
			{
				ret=-3;
			}
		}
		else
		{		// delete entry altogether
			if (buf[entryend]=='\n')
			{
				entryend++;
				if (buf[entryend]=='\r') entryend++;
			}
			else if (buf[entryend]=='\r')
			{
				entryend++;
				if (buf[entryend]=='\n') entryend++;
			}
			k=entryend-entrystart;
			memmove(&buf[entrystart],&buf[entryend],filelen-entryend);
			filelen-=k;
		}
	}
	else
	{		// new entry, insert entry at end of section
		if (val)	// if entry is not to be deleted
		{
			if (endpos)
			{		// section exists
				sprintf(temp,"%s=%s\n",entry,val);
				j=strlen(temp);
				p=&buf[endpos];
				while(*(p-1)=='\n' || *(p-1)=='\r')
				{
					p--;
					endpos--;
				}
				if (*p=='\n')
				{
					p++;endpos++;
					if (*p=='\r') p++,endpos++;
				}
				else if (*p=='\r')
				{
					p++;endpos++;
					if (*p=='\n') p++,endpos++;
				}
				if ((filelen+j)<MAX_INI_FILE_SIZE)
				{
					if (endpos<filelen) memmove(p+j,p,filelen-endpos);
					memcpy(p,temp,j);
					filelen+=j;
				}
				else
				{
					ret=-3;
				}
			}
			else
			{		// new section, too, insert section at the end of file
				p=&buf[filelen];
				sprintf(temp,"%s\n%s=%s\n",sec,entry,val);
				j=strlen(temp);
				if ((filelen+j)<MAX_INI_FILE_SIZE)
				{
					strcpy(p,temp);
					filelen+=j;
				}
				else
				{
					ret=-3;
				}
			}
		}
	}
	*multi_len=filelen;
	return ret;
}


static int local_setstringini(char *section,char *entry,char *val)
{
	int i,j,k,l,m,n,ret,filelen,fd,startpos,endpos,entrystart,entryend;
	char buf[MAX_INI_FILE_SIZE];
	char sec[64],*p,temp[16384];

	ret=0;startpos=endpos=entrystart=entryend=0;
	k=-1;l=strlen(entry);sec[0]='[';strcpy(&sec[1],section);n=strlen(sec);sec[n]=']';sec[n+1]=0;n++;
	fd=open(inifile,FILE_FLAGS_RD);
	if (fd>0)
	{
		filelen=read(fd,buf,sizeof(buf)-1);
		close(fd);
	}
	else
	{
		filelen=0;
		buf[0]=0;
	}
	if (filelen>0) buf[filelen]=0; else buf[0]=0;
	for (i=0;i<filelen;)
	{
		while(buf[i]==' ' || buf[i]=='\t') i++;
		if (!strnicmp(&buf[i],sec,n))
		{
			while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
			if (!buf[i]) filelen=i;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			startpos=i;
			i=search_section(buf,i);
			if (buf[i]=='[' || i>=filelen)
			{
				endpos=i;
			}
		}
		else
		{
			while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
			if (!buf[i]) filelen=i;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
		}
	}
	for (i=startpos,entrystart=entryend=0;i<endpos && !entryend;)
	{
		j=i;
		while((buf[i]==' ' || buf[i]=='\t') && i<endpos) i++;
		if (buf[i]!=';' && i<endpos)
		{
			if (!strnicmp(&buf[i],entry,l))
			{
				i=i+l;
				while(buf[i]==' ' || buf[i]=='\t') i++;
				if (buf[i]=='=')
				{
					entrystart=j;
					i++;m=0;
					while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
					entryend=i;
				}
			}
		}
		while(buf[i] && buf[i]!='\n' && buf[i]!='\r' && i<endpos) i++;
		if (buf[i]=='\n' || buf[i]=='\r') i++;
		if (buf[i]=='\n' || buf[i]=='\r') i++;
	}
	if (entryend)	// entry exists, replace it
	{
		if (val)
		{
			sprintf(temp,"%s=%s",entry,val);
			j=strlen(temp);
			k=entryend-entrystart;
			if ((filelen+j-k)<sizeof(buf))
			{
				p=&buf[entrystart];
				if (j!=k)
				{
					memmove(&buf[entryend+j-k],&buf[entryend],filelen-entryend);
				}
				memcpy(p,temp,j);
				filelen+=j-k;
			}
			else
			{
				ret=-3;
			}
		}
		else
		{		// delete entry altogether
			if (buf[entryend]=='\n')
			{
				entryend++;
				if (buf[entryend]=='\r') entryend++;
			}
			else if (buf[entryend]=='\r')
			{
				entryend++;
				if (buf[entryend]=='\n') entryend++;
			}
			k=entryend-entrystart;
			memmove(&buf[entrystart],&buf[entryend],filelen-entryend);
			filelen-=k;
		}
	}
	else
	{		// new entry, insert entry at end of section
		if (val)	// if entry is not to be deleted
		{
			if (endpos)
			{		// section exists
				sprintf(temp,"%s=%s\n",entry,val);
				j=strlen(temp);
				p=&buf[endpos];
				while(*(p-1)=='\n' || *(p-1)=='\r')
				{
					p--;
					endpos--;
				}
				if (*p=='\n')
				{
					p++;endpos++;
					if (*p=='\r') p++,endpos++;
				}
				else if (*p=='\r')
				{
					p++;endpos++;
					if (*p=='\n') p++,endpos++;
				}
				if ((filelen+j)<sizeof(buf))
				{
					if (endpos<filelen) memmove(p+j,p,filelen-endpos);
					memcpy(p,temp,j);
					filelen+=j;
				}
				else
				{
					ret=-3;
				}
			}
			else
			{		// new section, too, insert section at the end of file
				p=&buf[filelen];
				sprintf(temp,"%s\n%s=%s\n",sec,entry,val);
				j=strlen(temp);
				if ((filelen+j)<sizeof(buf))
				{
					strcpy(p,temp);
					filelen+=j;
				}
				else
				{
					ret=-3;
				}
			}
		}
	}
	if (!ret)
	{
		ret=backup_ini(inifile);
		if (!ret)
		{
			fd=open(inifile,FILE_FLAGS_RW,S_IREAD|S_IWRITE);
			if (fd>0)
			{
				i=write(fd,buf,filelen);
#ifdef WIN32
				if (i<filelen)
#else
				if (i!=filelen)
#endif
				{
					ret=-2;
				}
				i=close(fd);
				if (ret>=0 && i<0)
				{
					ret=-2;
				}
				restore_ini(inifile,ret);
			}
			else
			{
				ret=-2;
			}
		}
	}
	return ret;
}

int setstringini(char *section,char *entry,char *val)
{
	int ret;

#ifdef WIN32
	doinilock(section,entry,val);
#else
	doinilock(section,entry,val);
#endif
	ret=local_setstringini(section,entry,val);
#ifdef WIN32
	doiniunlock();
#else
	doiniunlock();
#endif
	return ret;
}

static int local_setsectionini(char *section,char *block)
{
	int i,j,k,n,ret,filelen,fd,startpos,endpos,entrystart,entryend;
	char buf[MAX_INI_FILE_SIZE];
	char sec[64],temp[16384];

	ret=0;startpos=endpos=entrystart=entryend=0;
	k=-1;sec[0]='[';strcpy(&sec[1],section);n=strlen(sec);sec[n]=']';sec[n+1]=0;n++;
	fd=open(inifile,FILE_FLAGS_RD);
	if (fd>0)
	{
		filelen=read(fd,buf,sizeof(buf)-1);
		close(fd);
	}
	else
	{
		filelen=0;
		buf[0]=0;
	}
	if (filelen>0) buf[filelen]=0; else buf[0]=0;
	for (i=0;i<filelen;)
	{
		while(buf[i]==' ' || buf[i]=='\t') i++;
		if (!strnicmp(&buf[i],sec,n))
		{
			while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
			if (!buf[i]) filelen=i;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			startpos=i;
			i=search_section(buf,i);
			if (buf[i]=='[' || i>=filelen)
			{
				endpos=i;
			}
		}
		else
		{
			while(buf[i]!='\n' && buf[i]!='\r' && buf[i]) i++;
			if (!buf[i]) filelen=i;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
			if (buf[i]=='\n' || buf[i]=='\r') i++;
		}
	}
	if (!endpos)	// section does not yet exist
	{
		i=sprintf(temp,"\n%s\n",sec);
		if (filelen+i<sizeof(buf))
		{
			memcpy(&buf[filelen],temp,i);
			filelen+=i;
			startpos=endpos=filelen;
		}
		else
		{
			ret=-3;
		}
	}
	if (!ret)
	{
		j=strlen(block);
		i=endpos-startpos;
		if ((filelen+j-i)<sizeof(buf))
		{
			memmove(&buf[startpos+j],&buf[endpos],filelen-endpos);
			filelen+=j-i;
			memcpy(&buf[startpos],block,j);
			ret=0;
		}
		else
		{
			ret=-3;
		}
	}
	if (!ret)
	{
		ret=backup_ini(inifile);
		if (!ret)
		{
			fd=open(inifile,FILE_FLAGS_RW,S_IREAD|S_IWRITE);
			if (fd>0)
			{
				i=write(fd,buf,filelen);
#ifdef WIN32
				if (i<filelen)
#else
				if (i!=filelen)
#endif
				{
					time_t lt;
					struct tm *tm;
					FILE *fp;

					lt=time(0L);tm=gmtime(&lt);
					fp=fopen("/home/www/avherald/logs/inilog.log","at");
					if (fp)
					{
						fprintf(fp,"%04d.%02d.%02d %02d:%02d:%02d Write buflen error %s: %d (%s)\n",tm->tm_year+1900,tm->tm_mon,tm->tm_mday,tm->tm_hour,tm->tm_min,tm->tm_sec,inifile,errno,strerror(errno));
						fclose(fp);
					}
					ret=-2;
				}
				i=close(fd);
				if (ret>=0 && i<0)
				{
					time_t lt;
					struct tm *tm;
					FILE *fp;

					lt=time(0L);tm=gmtime(&lt);
					fp=fopen("/home/www/avherald/logs/inilog.log","at");
					if (fp)
					{
						fprintf(fp,"%04d.%02d.%02d %02d:%02d:%02d Close error %s: %d (%s)\n",tm->tm_year+1900,tm->tm_mon,tm->tm_mday,tm->tm_hour,tm->tm_min,tm->tm_sec,inifile,errno,strerror(errno));
						fclose(fp);
					}
					ret=-2;
				}
				restore_ini(inifile,ret);
			}
			else
			{
				time_t lt;
				struct tm *tm;
				FILE *fp;

				lt=time(0L);tm=gmtime(&lt);
				fp=fopen("/home/www/avherald/logs/inilog.log","at");
				if (fp)
				{
					fprintf(fp,"%04d.%02d.%02d %02d:%02d:%02d Open error %s: %d (%s)\n",tm->tm_year+1900,tm->tm_mon,tm->tm_mday,tm->tm_hour,tm->tm_min,tm->tm_sec,inifile,errno,strerror(errno));
					fclose(fp);
				}
				ret=-2;
			}
		}
		else
		{
			time_t lt;
			struct tm *tm;
			FILE *fp;

			lt=time(0L);tm=gmtime(&lt);
			fp=fopen("/home/www/avherald/logs/inilog.log","at");
			if (fp)
			{
				fprintf(fp,"%04d.%02d.%02d %02d:%02d:%02d Backup error %s: %d (%s)\n",tm->tm_year+1900,tm->tm_mon,tm->tm_mday,tm->tm_hour,tm->tm_min,tm->tm_sec,inifile,errno,strerror(errno));
				fclose(fp);
			}
		}
	}
	return ret;
}

int setsectionini(char *section,char *block)
{
	int ret;

#ifdef WIN32
	doinilock(section,"ini_setsection",block);
#else
	doinilock(section,"",block);
#endif
	ret=local_setsectionini(section,block);
#ifdef WIN32
	doiniunlock();
#else
	doiniunlock();
#endif
	return ret;
}

int ini_increasehit(char *section,char *entry)
{
	int ret;
	char val[512];

#ifdef WIN32
	doinilock(section,entry,"ini_increase");
#else
	doinilock(section,entry,"ini_increase");
#endif
	ret=getvalini(section,entry);
	if (ret<0)
	{
		ret=0;
	}
	sprintf(val,"%d:%lu",ret+1,time(0L));
	ret=local_setstringini(section,entry,val);
#ifdef WIN32
	doiniunlock();
#else
	doiniunlock();
#endif
	return ret;
}

int ini_read_increasehit(char *section,char *entry)
{
	int ret,ret1;
	char val[512];

#ifdef WIN32
	doinilock(section,entry,"ini_read_increase");
#else
	doinilock(section,entry,"ini_read_increase");
#endif
	ret=getvalini(section,entry);
	if (ret<0)
	{
		ret=0;
	}
	sprintf(val,"%d:%lu",++ret,time(0L));
	ret1=local_setstringini(section,entry,val);
	if (ret1<0) ret=ret1;
#ifdef WIN32
	doiniunlock();
#else
	doiniunlock();
#endif
	return ret;
}

int ini_multi_increasehit(int n,char *section[],char *entry[])
{
	int ret,ret1,i;
	char val[512];
	char multi_buf[MAX_INI_FILE_SIZE];
	int multi_len;

	multi_len=0;multi_buf[0]=0;
#ifdef WIN32
	doinilock(section[0],entry[0],"ini_multi_increase");
#else
	doinilock(section[0],entry[0],"ini_multi_increase");
#endif
	ret=local_openmulti(&multi_len,multi_buf);
	if (ret>=0)
	{
		for(i=0;i<n && ret>=0;i++)
		{
			ret=local_getvalmulti(section[i],entry[i],&multi_len,multi_buf);
			if (ret<0)
			{
				ret=0;
			}
			sprintf(val,"%d:%lu",++ret,time(0L));
			ret1=local_setstringmulti(section[i],entry[i],val,&multi_len,multi_buf);
			if (ret1<0) ret=ret1;
		}
		if (ret>=0)
		{
			local_closemulti(&multi_len,multi_buf);
		}
	}
#ifdef WIN32
	doiniunlock();
#else
	doiniunlock();
#endif
	return ret;
}

int ini_multi_setstring(int n,char *section[],char *entry[],char *val[])
{
	int ret,ret1,i;
	char multi_buf[MAX_INI_FILE_SIZE];
	int multi_len;

	multi_len=0;multi_buf[0]=0;
#ifdef WIN32
	doinilock(section[0],entry[0],val[0]);
#else
	doinilock(section[0],entry[0],val[0]);
#endif
	ret=local_openmulti(&multi_len,multi_buf);
	if (ret>=0)
	{
		for(i=0;i<n && ret>=0;i++)
		{
			ret=local_getvalmulti(section[i],entry[i],&multi_len,multi_buf);
			if (ret<0)
			{
				ret=0;
			}
			ret1=local_setstringmulti(section[i],entry[i],val[i],&multi_len,multi_buf);
			if (ret1<0) ret=ret1;
		}
		if (ret>=0)
		{
			local_closemulti(&multi_len,multi_buf);
		}
	}
#ifdef WIN32
	doiniunlock();
#else
	doiniunlock();
#endif
	return ret;
}

int ini_set_log_message(void (*log)(char *))
{
	log_message=log;
	return 0;
}