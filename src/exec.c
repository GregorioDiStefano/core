/* 

        Copyright (C) 1994-
        Free Software Foundation, Inc.

   This file is part of GNU cfengine - written and maintained 
   by Mark Burgess, Dept of Computing and Engineering, Oslo College,
   Dept. of Theoretical physics, University of Oslo
 
   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3, or (at your option) any
   later version. 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

*/

/*****************************************************************************/
/*                                                                           */
/* File: exec.c                                                              */
/*                                                                           */
/*****************************************************************************/

#include "cf3.defs.h"
#include "cf3.extern.h"

int main (int argc,char *argv[]);


#ifdef NT
#include <process.h>
#endif

/*******************************************************************/
/* GLOBAL VARIABLES                                                */
/*******************************************************************/

int NO_FORK = false;
int ONCE = false;
char MAILTO[CF_BUFSIZE];
char MAILFROM[CF_BUFSIZE];
int MAXLINES = -1;
const int INF_LINES = -2;

extern struct BodySyntax CFEX_CONTROLBODY[];

void StartServer(int argc,char **argv);
int ScheduleRun(void);
void *ExitCleanly(void);
static char *timestamp(time_t stamp, char *buf, size_t len);
void *LocalExec(void *scheduled_run);
int FileChecksum(char *filename,unsigned char digest[EVP_MAX_MD_SIZE+1],char type);
int CompareResult(char *filename,char *prev_file);
void MailResult(char *file,char *to);
int Dialogue(int sd,char *s);


/*******************************************************************/
/* Pthreads                                                        */
/*******************************************************************/

#ifdef HAVE_PTHREAD_H
# include <pthread.h>
#endif

#ifdef HAVE_SCHED_H
# include <sched.h>
#endif

#ifdef PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP
pthread_attr_t PTHREADDEFAULTS;
pthread_mutex_t MUTEX_COUNT = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
pthread_mutex_t MUTEX_HOSTNAME = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
#else
# if defined HAVE_PTHREAD_H && (defined HAVE_LIBPTHREAD || defined BUILDTIN_GCC_THREAD)
pthread_attr_t PTHREADDEFAULTS;
pthread_mutex_t MUTEX_COUNT = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MUTEX_HOSTNAME = PTHREAD_MUTEX_INITIALIZER;
# endif
#endif

/*******************************************************************/
/* Command line options                                            */
/*******************************************************************/

  /* GNU STUFF FOR LATER #include "getopt.h" */
 
 struct option OPTIONS[17] =
      {
      { "help",no_argument,0,'h' },
      { "debug",optional_argument,0,'d' },
      { "verbose",no_argument,0,'v' },
      { "dry-run",no_argument,0,'n'},
      { "version",no_argument,0,'V' },
      { "define",required_argument,0,'D' },
      { "negate",required_argument,0,'N' },
      { "no-lock",no_argument,0,'K'},
      { "inform",no_argument,0,'I'},
      { "syntax",no_argument,0,'S'},
      { "diagnostic",no_argument,0,'x'},
      { "no-fork",no_argument,0,'F' },
      { "once",no_argument,0,'1'},
      { "foreground",no_argument,0,'g'},
      { "parse-only",no_argument,0,'p'},
      { "ld-library-path",required_argument,0,'L'},
      { NULL,0,0,'\0' }
      };

/*****************************************************************************/

int main(int argc,char *argv[])

{
GenericInitialize(argc,argv,"executor");
ThisAgentInit();
KeepPromises();

StartServer(argc,argv);

if (!ONCE)
   {
   RestoreExecLock(); 
   ReleaseCurrentLock();
   }

return 0;
}

/*****************************************************************************/
/* Level 1                                                                   */
/*****************************************************************************/

void CheckOpts(int argc,char **argv)

{ extern char *optarg;
  struct Item *actionList;
  int optindex = 0;
  int c;
  char ld_library_path[CF_BUFSIZE];

while ((c=getopt_long(argc,argv,"d:vnIf:pD:N:VSxL:hFV1g",OPTIONS,&optindex)) != EOF)
  {
  switch ((char) c)
      {
      case 'f':

          strncpy(VINPUTFILE,optarg,CF_BUFSIZE-1);
          VINPUTFILE[CF_BUFSIZE-1] = '\0';
          MINUSF = true;
          break;

      case 'd': 
          AddClassToHeap("opt_debug");
          switch ((optarg==NULL) ? '3' : *optarg)
             {
             case '1':
                 D1 = true;
                 DEBUG = true;
                 break;
             case '2':
                 D2 = true;
                 DEBUG = true;
                 break;
             case '3':
                 D3 = true;
                 DEBUG = true;
                 VERBOSE = true;
                 break;
             case '4':
                 D4 = true;
                 DEBUG = true;
                 break;
             default:
                 DEBUG = true;
                 break;
             }
          break;
          
      case 'K': IGNORELOCK = true;
          break;
                    
      case 'D': AddMultipleClasses(optarg);
          break;
          
      case 'N': NegateCompoundClass(optarg,&VNEGHEAP);
          break;
          
      case 'I': INFORM = true;
          break;
          
      case 'v': VERBOSE = true;
          break;
          
      case 'n': DONTDO = true;
          IGNORELOCK = true;
          AddClassToHeap("opt_dry_run");
          break;
          
      case 'p': PARSEONLY = true;
          IGNORELOCK = true;
          break;          

      case 'q': NOSPLAY = true;
          break;
          
      case 'g': NO_FORK = true;
         break;
         
      case 'L': 
          snprintf(ld_library_path,CF_BUFSIZE-1,"LD_LIBRARY_PATH=%s",optarg);
          if (putenv(strdup(ld_library_path)) != 0)
             {
             }
          
          break;
          
      case 'F':
      case '1': ONCE = true;
          NO_FORK = true;
          break;

          
      case 'V': Version("Execution agent");
          exit(0);
          
      case 'h': Syntax("Execution agent");
          exit(0);

      case 'x': SelfDiagnostic();
          exit(0);
          
      default: Syntax("Execution agent");
          exit(1);
          
      }
  }
}

/*****************************************************************************/

void ThisAgentInit()

{
LOGGING = true;                    /* Do output to syslog */

 /* XXX Initialize workdir for non privileged users */

strcpy(CFWORKDIR,WORKDIR);

if (getuid() > 0)
   {
   char *homedir;
   if ((homedir = getenv("HOME")) != NULL)
      {
      strcpy(CFWORKDIR,homedir);
      strcat(CFWORKDIR,"/.cfagent");
      }
   }

snprintf(VBUFF,CF_BUFSIZE,"%s/inputs/update.conf",CFWORKDIR);
MakeDirectoriesFor(VBUFF,'y');
snprintf(VBUFF,CF_BUFSIZE,"%s/bin/cfagent -D from_cfexecd",CFWORKDIR);
MakeDirectoriesFor(VBUFF,'y');
snprintf(VBUFF,CF_BUFSIZE,"%s/outputs/spooled_reports",CFWORKDIR);
MakeDirectoriesFor(VBUFF,'y');

snprintf(VBUFF,CF_BUFSIZE,"%s/inputs",CFWORKDIR);
chmod(VBUFF,0700); 
snprintf(VBUFF,CF_BUFSIZE,"%s/outputs",CFWORKDIR);
chmod(VBUFF,0700);

strncpy(VLOCKDIR,CFWORKDIR,CF_BUFSIZE-1);
strncpy(VLOGDIR,CFWORKDIR,CF_BUFSIZE-1);

VCANONICALFILE = strdup(CanonifyName(VINPUTFILE));

MAILTO[0] = '\0';
MAILFROM[0] = '\0';
VIPADDRESS[0] = '\0';
VMAILSERVER[0] = '\0';
}

/*****************************************************************************/

void KeepPromises()

{ struct Body *body;
 struct Constraint *cp;

for (body = BODIES; body != NULL; body=body->next)
   {
   if (strcmp(body->type,CF_AGENTTYPES[cf_executor]) == 0)
      {
      if (strcmp(body->name,"control") == 0)
         {
         Debug("%s body for type %s\n",body->name,body->type);
         
         for (cp = body->conlist; cp != NULL; cp=cp->next)
            {
            if (strcmp(cp->lval,CFEX_CONTROLBODY[cfex_mailfrom].lval) == 0)
               {
               strcpy(MAILFROM,cp->rval);
               Debug("mailfrom = %s\n",MAILFROM);
               }

            if (strcmp(cp->lval,CFEX_CONTROLBODY[cfex_mailto].lval) == 0)
               {
               strcpy(MAILTO,cp->rval);
               Debug("mailto = %s\n",MAILTO);
               }

            if (strcmp(cp->lval,CFEX_CONTROLBODY[cfex_smtpserver].lval) == 0)
               {
               strcpy(VMAILSERVER,cp->rval);
               Debug("smtpserver = %s\n",VMAILSERVER);
               }

            if (strcmp(cp->lval,CFEX_CONTROLBODY[cfex_mailmaxlines].lval) == 0)
               {
               MAXLINES = atoi(cp->rval);
               Debug("maxlines = %d\n",MAXLINES);
               }

            if (strcmp(cp->lval,CFEX_CONTROLBODY[cfex_schedule].lval) == 0)
               {
               struct Rlist *rp;
               Debug("schedule ...\n");
               
               for (rp  = (struct Rlist *) cp->rval; rp != NULL; rp = rp->next)
                  {
                  if (!IsItemIn(SCHEDULE,rp->item))
                     {
                     AppendItem(&SCHEDULE,rp->item,NULL);
                     }
                  }
               }
            }
         }
      }
   }
}

/*****************************************************************************/

void StartServer(int argc,char **argv)

{ int pid,time_to_run = false;
  time_t now = time(NULL); 

Banner("Starting executor");

if ((!NO_FORK) && (fork() != 0))
   {
   snprintf(OUTPUT,CF_BUFSIZE*2,"cfExec starting %.24s\n",ctime(&now));
   CfLog(cfinform,OUTPUT,"");
   exit(0);
   }

if (!ONCE)
   {
   if (!GetLock("cfexecd","execd",0,0,VUQNAME,now))
      {
      snprintf(OUTPUT,CF_BUFSIZE*2,"cfexecd: Couldn't get a lock -- exists or too soon: IfElapsed %d, ExpireAfter %d\n",0,0);
      CfLog(cfverbose,OUTPUT,"");
      return;
      }

   SaveExecLock(); 
   }

if (!NO_FORK)
  {
  ActAsDaemon(0);
  }

WritePID("cfExecd.pid");
signal(SIGINT,(void *)ExitCleanly);
signal(SIGTERM,(void *)ExitCleanly);
signal(SIGHUP,SIG_IGN);
signal(SIGPIPE,SIG_IGN);
signal(SIGUSR1,HandleSignal);
signal(SIGUSR2,HandleSignal);
 
umask(077);

if (ONCE)
   {
   LocalExec((void *)0);
   }
else
   { char **nargv;
     int i;
#ifdef HAVE_PTHREAD_H
     pthread_t tid;
#endif

   /*
    * Append --once option to our arguments for spawned monitor process.
    */

   nargv = malloc(sizeof(char *) * (argc+2));
     
   for (i = 0; i < argc; i++)
      {
      nargv[i] = argv[i];
      }
   
   nargv[i++] = strdup("--once");
   nargv[i++] = NULL;
   
   while (true)
      {
      time_to_run = ScheduleRun();
      
      if (time_to_run)
         {
         if (!GetLock("cf3","exec",CF_EXEC_IFELAPSED,CF_EXEC_EXPIREAFTER,VUQNAME,time(NULL)))
            {
            snprintf(OUTPUT,CF_BUFSIZE*2,"cfExecd: Couldn't get exec lock -- exists or too soon: IfElapsed %d, ExpireAfter %d\n",CF_EXEC_IFELAPSED,CF_EXEC_EXPIREAFTER);
            CfLog(cfverbose,OUTPUT,"");
            continue;
            }         
#ifdef NT 
         /*
          * Spawn a separate process - spawn will work if the cfexecd binary
          * has changed (where cygwin's fork() would fail).
          */
         
         Debug("Spawning %s\n", nargv[0]);

         pid = spawnvp((int)_P_NOWAIT, nargv[0], nargv);

         if (pid < 1)
            {
            CfLog(cfinform,"Can't spawn run","spawnvp");
            }
#endif
#ifndef NT
#if (defined HAVE_LIBPTHREAD || defined BUILDTIN_GCC_THREAD)
         
         pthread_attr_init(&PTHREADDEFAULTS);
         pthread_attr_setdetachstate(&PTHREADDEFAULTS,PTHREAD_CREATE_DETACHED);
         
#ifdef HAVE_PTHREAD_ATTR_SETSTACKSIZE
         pthread_attr_setstacksize(&PTHREADDEFAULTS,(size_t)2048*1024);
#endif
         
         if (pthread_create(&tid,&PTHREADDEFAULTS,LocalExec,(void *)1) != 0)
            {
            CfLog(cfinform,"Can't create thread!","pthread_create");
            LocalExec((void *)1);
            }
         
         pthread_attr_destroy(&PTHREADDEFAULTS);
#else
         LocalExec((void *)1);  
#endif
#endif
         
         ReleaseCurrentLock();
         }
      }
   }
}

/*****************************************************************************/
/* Level                                                                     */
/*****************************************************************************/

int ScheduleRun()

{ time_t now;
  char timekey[64];
  struct Item *ip;

Verbose("Sleeping...\n");
sleep(60);                /* 1 Minute resolution is enough */ 

now = time(NULL);

snprintf(timekey,63,"%s",ctime(&now)); 
AddTimeClass(timekey); 

for (ip = SCHEDULE; ip != NULL; ip = ip->next)
   {
   Verbose("Checking schedule %s...\n",ip->name);
   if (IsDefinedClass(ip->name))
      {
      Verbose("Waking up the agent at %s ~ %s \n",timekey,ip->name);
      DeleteItemList(VHEAP);
      VHEAP = NULL;
      return true;
      }
   }

DeleteItemList(VHEAP);
VHEAP = NULL; 
return false;
}

/************************************************************************/

void *ExitCleanly()

{
ReleaseCurrentLock();
closelog();
unlink(PIDFILE);
exit(0);
}

/*************************************************************************/

static char *timestamp(time_t stamp, char *buf, size_t len)

{ struct tm *ltime;
 
ltime = localtime(&stamp);
snprintf(buf, len, "%04d-%02d-%02d--%02d-%02d-%02d",
         ltime->tm_year+1900,
         ltime->tm_mon+1,
         ltime->tm_mday,
         ltime->tm_hour,
         ltime->tm_min,
         ltime->tm_sec);
return buf;
}

/**************************************************************/

void *LocalExec(void *scheduled_run)

{ FILE *pp; 
  char line[CF_BUFSIZE],filename[CF_BUFSIZE],*sp;
  char cmd[CF_BUFSIZE];
  int print;
  time_t starttime = time(NULL);
  FILE *fp;
#ifdef HAVE_PTHREAD_SIGMASK
 sigset_t sigmask;

sigemptyset(&sigmask);
pthread_sigmask(SIG_BLOCK,&sigmask,NULL); 
#endif

 
Verbose("------------------------------------------------------------------\n\n");
Verbose("  LocalExec(%sscheduled) at %s\n", scheduled_run ? "" : "not ", ctime(&starttime));
Verbose("------------------------------------------------------------------\n"); 

/* Need to make sure we have LD_LIBRARY_PATH here or children will die  */

snprintf(cmd,CF_BUFSIZE-1,"%s/bin/cfagent%s -Dfrom_cfexecd%s",
		 CFWORKDIR,
		 NOSPLAY ? " -q" : "",
		 scheduled_run ? ":scheduled_run" : "");
 
timestamp(starttime, line, CF_BUFSIZE);

snprintf(filename,CF_BUFSIZE-1,"%s/outputs/cf_%s_%s",CFWORKDIR,CanonifyName(VFQNAME),line);
 
/* What if no more processes? Could sacrifice and exec() - but we need a sentinel */

if ((fp = fopen(filename,"w")) == NULL)
   {
   snprintf(OUTPUT,CF_BUFSIZE,"Couldn't open %s\n",filename);
   CfLog(cfinform,OUTPUT,"fopen");
   return NULL;
   }
 
if ((pp = cfpopen(cmd,"r")) == NULL)
   {
   snprintf(OUTPUT,CF_BUFSIZE,"Couldn't open pipe to command %s\n",cmd);
   CfLog(cfinform,OUTPUT,"cfpopen");
   fclose(fp);
   return NULL;
   }
 
while (!feof(pp) && ReadLine(line,CF_BUFSIZE,pp))
   {
   if (ferror(pp))
      {
      fflush(pp);
      break;
      }  
   
   print = false;
      
   for (sp = line; *sp != '\0'; sp++)
      {
      if (! isspace((int)*sp))
         {
         print = true;
         break;
         }
      }
   
   if (print)
      {
      fprintf(fp,"%s\n",line);
      
      /* If we can't send mail, log to syslog */
      
      if (strlen(MAILTO) == 0)
         {
         strncat(line,"\n",CF_BUFSIZE-1-strlen(line));
         if ((strchr(line,'\n')) == NULL)
            {
            line[CF_BUFSIZE-2] = '\n';
            }
         CfLog(cfinform,line,"");
         }
      
      line[0] = '\0';
      }
   }
 
cfpclose(pp);
Debug("Closing fp\n");
fclose(fp);
closelog();
  
MailResult(filename,MAILTO);
return NULL; 
}

/******************************************************************************/
/* Level 4                                                                    */
/******************************************************************************/

int FileChecksum(char *filename,unsigned char digest[EVP_MAX_MD_SIZE+1],char type)

{ FILE *file;
  EVP_MD_CTX context;
  int len, md_len;
  unsigned char buffer[1024];
  const EVP_MD *md = NULL;

Debug2("FileChecksum(%c,%s)\n",type,filename);

if ((file = fopen (filename, "rb")) == NULL)
   {
   printf ("%s can't be opened\n", filename);
   }
else
   {
   switch (type)
      {
      case 's': md = EVP_get_digestbyname("sha");
           break;
      case 'm': md = EVP_get_digestbyname("md5");
         break;
      default: FatalError("Software failure in ChecksumFile");
      }

   if (!md)
      {
      return 0;
      }
   
   EVP_DigestInit(&context,md);

   while (len = fread(buffer,1,1024,file))
      {
      EVP_DigestUpdate(&context,buffer,len);
      }

   EVP_DigestFinal(&context,digest,&md_len);
   fclose (file);
   return(md_len);
   }

return 0; 
}

/*******************************************************************/

int CompareResult(char *filename,char *prev_file)

{ int i;
  char digest1[EVP_MAX_MD_SIZE+1];
  char digest2[EVP_MAX_MD_SIZE+1];
  int  md_len1, md_len2;
  FILE *fp;
  int rtn = 0;

Verbose("Comparing files  %s with %s\n", prev_file, filename);

if ((fp=fopen(prev_file,"r")) != NULL)
   {
   fclose(fp);

   md_len1 = FileChecksum(prev_file, digest1, 'm');
   md_len2 = FileChecksum(filename,  digest2, 'm');

   if (md_len1 != md_len2)
      {
      rtn = 1;
      }
   else
      {
      for (i = 0; i < md_len1; i++)
          if (digest1[i] != digest2[i])
             {
      rtn = 1;
      break;
      }
      }
   }
else
   {
   /* no previous file */
   rtn = 1;
   }

unlink(prev_file);

if (symlink(filename, prev_file) == -1 )
   {
   snprintf(OUTPUT,CF_BUFSIZE,"Could not link %s and %s",filename,prev_file);
   CfLog(cfinform,OUTPUT,"symlink");
   rtn = 1;
   }

return(rtn);
}

/***********************************************************************/

void MailResult(char *file,char *to)

{ int sd, sent, count = 0, anomaly = false;
  char domain[256], prev_file[CF_BUFSIZE];
  struct hostent *hp;
  struct sockaddr_in raddr;
  struct servent *server;
  struct stat statbuf;
  time_t now = time(NULL);
  FILE *fp;

if ((strlen(VMAILSERVER) == 0) || (strlen(to) == 0))
   {
   /* Syslog should have done this */
   return;
   }
  
if (stat(file,&statbuf) == -1)
   {
   exit(0);
   }

snprintf(prev_file,CF_BUFSIZE-1,"%s/outputs/previous",CFWORKDIR);

if (statbuf.st_size == 0)
   {
   unlink(file);

   if ((fp = fopen(prev_file, "r")) != NULL )
      {
      fclose(fp);
      unlink(prev_file);
      }

   Debug("Nothing to report in %s\n",file);
   return;
   }

if ( CompareResult(file,prev_file) == 0 ) 
   {
   Verbose("Previous output is the same as current so do not mail it\n");
   return;
   }

if (MAXLINES == 0)
   {
   Debug("Not mailing: EmailMaxLines was zero\n");
   return;
   }
 
Debug("Mailing results of (%s) to (%s)\n",file,to);
 
if (strlen(to) == 0)
   {
   return;
   }

/* Check first for anomalies - for subject header */
 
if ((fp=fopen(file,"r")) == NULL)
   {
   snprintf(VBUFF,CF_BUFSIZE-1,"Couldn't open file %s",file);
   CfLog(cfinform,VBUFF,"fopen");
   return;
   }

 while (!feof(fp))
    {
    VBUFF[0] = '\0';
    fgets(VBUFF,CF_BUFSIZE,fp);
    if (strstr(VBUFF,"entropy"))
       {
       anomaly = true;
       break;
       }
    }
 
fclose(fp);
 
if ((fp=fopen(file,"r")) == NULL)
   {
   snprintf(VBUFF,CF_BUFSIZE-1,"Couldn't open file %s",file);
   CfLog(cfinform,VBUFF,"fopen");
   return;
   }
 
Debug("Looking up hostname %s\n\n",VMAILSERVER);

if ((hp = gethostbyname(VMAILSERVER)) == NULL)
   {
   printf("Unknown host: %s\n", VMAILSERVER);
   printf("Make sure that fully qualified names can be looked up at your site!\n");
   printf("i.e. www.gnu.org, not just www. If you use NIS or /etc/hosts\n");
   printf("make sure that the full form is registered too as an alias!\n");
   fclose(fp);
   return;
   }

if ((server = getservbyname("smtp","tcp")) == NULL)
   {
   CfLog(cfinform,"Unable to lookup smtp service","getservbyname");
   fclose(fp);
   return;
   }

memset(&raddr,0,sizeof(raddr));

raddr.sin_port = (unsigned int) server->s_port;
raddr.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
raddr.sin_family = AF_INET;  

Debug("Connecting...\n");

if ((sd = socket(AF_INET,SOCK_STREAM,0)) == -1)
   {
   CfLog(cfinform,"Couldn't open a socket","socket");
   fclose(fp);
   return;
   }
   
if (connect(sd,(void *) &raddr,sizeof(raddr)) == -1)
   {
   snprintf(OUTPUT,CF_BUFSIZE*2,"Couldn't connect to host %s\n",VMAILSERVER);
   CfLog(cfinform,OUTPUT,"connect");
   fclose(fp);
   close(sd);
   return;
   }

/* read greeting */
 
if (!Dialogue(sd,NULL))
   {
   goto mail_err;
   }
 
sprintf(VBUFF,"HELO %s\r\n",VFQNAME); 
Debug("%s",VBUFF);

if (!Dialogue(sd,VBUFF))
   {
   goto mail_err;
   }

 if (strlen(MAILFROM) == 0)
    {
    sprintf(VBUFF,"MAIL FROM: <cfengine@%s>\r\n",VFQNAME);
    Debug("%s",VBUFF);
    }
 else
    {
    sprintf(VBUFF,"MAIL FROM: <%s>\r\n",MAILFROM);
    Debug("%s",VBUFF);    
    }

if (!Dialogue(sd,VBUFF))
   {
   goto mail_err;
   }
 
sprintf(VBUFF,"RCPT TO: <%s>\r\n",to);
Debug("%s",VBUFF);

if (!Dialogue(sd,VBUFF))
    {
    goto mail_err;
    }

if (!Dialogue(sd,"DATA\r\n"))
    {
    goto mail_err;
    }

if (anomaly)
   {
   sprintf(VBUFF,"Subject: **!! (%s/%s)\r\n",VFQNAME,VIPADDRESS);
   Debug("%s",VBUFF);
   }
else
   {
   sprintf(VBUFF,"Subject: (%s/%s)\r\n",VFQNAME,VIPADDRESS);
   Debug("%s",VBUFF);
   }
 
sent=send(sd,VBUFF,strlen(VBUFF),0);

#if defined LINUX || defined NETBSD || defined FREEBSD || defined OPENBSD
strftime(VBUFF,CF_BUFSIZE,"Date: %a, %d %b %Y %H:%M:%S %z\r\n",localtime(&now));
sent=send(sd,VBUFF,strlen(VBUFF),0);
#endif

 if (strlen(MAILFROM) == 0)
    {
    sprintf(VBUFF,"From: cfengine@%s\r\n",VFQNAME);
    Debug("%s",VBUFF);
    }
 else
    {
    sprintf(VBUFF,"From: %s\r\n",MAILFROM);
    Debug("%s",VBUFF);    
    }
 
sent=send(sd,VBUFF,strlen(VBUFF),0);

sprintf(VBUFF,"To: %s\r\n\r\n",to); 
Debug("%s",VBUFF);
sent=send(sd,VBUFF,strlen(VBUFF),0);

while(!feof(fp))
   {
   VBUFF[0] = '\0';
   fgets(VBUFF,CF_BUFSIZE,fp);
   Debug("%s",VBUFF);
   
   if (strlen(VBUFF) > 0)
      {
      VBUFF[strlen(VBUFF)-1] = '\r';
      strcat(VBUFF, "\n");
      count++;
      sent=send(sd,VBUFF,strlen(VBUFF),0);
      }
   
   if ((MAXLINES != INF_LINES) && (count > MAXLINES))
      {
      sprintf(VBUFF,"\r\n[Mail truncated by cfengine. File is at %s on %s]\r\n",file,VFQNAME);
      sent=send(sd,VBUFF,strlen(VBUFF),0);
      break;
      }
   } 

if (!Dialogue(sd,".\r\n"))
   {
   Debug("mail_err\n");
   goto mail_err;
   }
 
Dialogue(sd,"QUIT\r\n");
Debug("Done sending mail\n");
fclose(fp);
close(sd);
return;
 
mail_err: 

fclose(fp);
close(sd); 
sprintf(VBUFF, "Cannot mail to %s.", to);
CfLog(cflogonly,VBUFF,"");
}

/******************************************************************/
/* Level 5                                                        */
/******************************************************************/

int Dialogue(int sd,char *s)

{ int sent;
  char ch,f = '\0';
  int charpos,rfclinetype = ' ';

if ((s != NULL) && (*s != '\0'))
   {
   sent=send(sd,s,strlen(s),0);
   Debug("SENT(%d)->%s",sent,s);
   }
else
   {
   Debug("Nothing to send .. waiting for opening\n");
   }

charpos = 0;
 
while (recv(sd,&ch,1,0))
   {
   charpos++;
   
   if (f == '\0')
      {
      f = ch;
      }

   if (charpos == 4)  /* Multiline RFC in form 222-Message with hyphen at pos 4 */
      {
      rfclinetype = ch;
      }
   
   Debug("%c",ch);
   
   if (ch == '\n' || ch == '\0')
      {
      charpos = 0;
      
      if (rfclinetype == ' ')
         {
         break;
         }
      }
   }

return ((f == '2') || (f == '3')); /* return code 200 or 300 from smtp*/
}



/* EOF */


