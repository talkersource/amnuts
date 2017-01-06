/*****************************************************************************
             Amnuts version 2.1.1 - Copyright (C) Andrew Collington
                        Last update: 25th May, 1999

      email: amnuts@iname.com    homepage: http://www.talker.com/amnuts/
                  personal: http://www.andyc.dircon.co.uk/

                        which is (heavily) modified

    NUTS version 3.3.3 (Triple Three :) - Copyright (C) Neil Robertson 1996
                      Last update: 18th November 1996
 *****************************************************************************
     MAIN CODE - MAIN CODE - MAIN CODE - MAIN CODE - MAIN CODE - MAIN CODE
 *****************************************************************************/


#include <stdio.h>
#ifdef _AIX
#include <sys/select.h> 
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <setjmp.h>
#include <errno.h>
#include <sys/file.h>
#include <dirent.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <crypt.h>

#include "amnuts211.h"
#include "commands.h"
#include "prototypes.h"


/* The following ifdef must be made after all of the above.
   I have had a number of requests to seperate the Amnuts code into smaller
   files and also to take out the Netlink functions.  This is a compromise.
   Whereas there is no makefile, so you have to always recompile everything,
   it is an easy way to seperate up the code.  And by using ifdef for the
   Netlinks you can compile without any Netlink functionality.
   I will openly admit that this is a bit of a hack way of doing it.  If you
   don't like the way this is done, then I don't really care :-)
*/

#ifdef NETLINKS
  #include "netlinks.h"
  #include "netlinks.c"
#endif


/******************************************************************************
 The setting up of the talker configs and the main program loop
 *****************************************************************************/


int main(int argc,char *argv[])
{
fd_set readmask; 
int i,len; 
char inpstr[ARR_SIZE];
UR_OBJECT user,next;
#ifdef NETLINKS
  NL_OBJECT nl;
#endif


strcpy(progname,argv[0]);
if (argc<2) strcpy(confile,CONFIGFILE);
else strcpy(confile,argv[1]);


/* Run in background automatically. */
/* moved here because of .reboot wasn't working properly with the forking after
   the rest of the initiation had taken place.
   */
switch(fork()) {
  case -1: boot_exit(11);  /* fork failure */
  case  0: break; /* child continues */
  default: sleep(1); exit(0);  /* parent dies */
  }


/* Startup and do initial counts and parses */
printf("\n*** Amnuts %s server booting ***\n\n",AMNUTSVER);
init_globals();
write_syslog("\n*** SERVER BOOTING ***\n",0,SYSLOG);
set_date_time();
init_signals();
load_and_parse_config();
#ifndef NETLINKS
  printf("Netlinks disabled.\n");
#endif
if (personal_rooms) {
  if (startup_room_parse) parse_user_rooms();
  else printf("Personal rooms are active, but not being parsed at startup.\n");
  }
else printf("Personal rooms disabled\n");
printf("Checking user directory structure\n");
check_directories();
printf("Processing user list\n");
process_users();
printf("Counting users\n");
count_users();
printf("Parsing command structure\n");
parse_commands();
purge(0,NULL,0);
if (!auto_purge) printf("PURGE: Auto-purge is turned off\n");
else printf("PURGE: Checked %d user%s, %d %s deleted due to lack of use.\n",purge_count,PLTEXT_S(purge_count),users_purged,PLTEXT_WAS(users_purged));
check_messages(NULL,1);
count_suggestions();
printf("There %s %d suggestion%s.\n",PLTEXT_WAS(suggestion_count),suggestion_count,PLTEXT_S(suggestion_count));
count_motds(0);
printf("There %s %d login motd%s and %d post-login motd%s\n",PLTEXT_WAS(motd1_cnt),motd1_cnt,PLTEXT_S(motd1_cnt),motd2_cnt,PLTEXT_S(motd2_cnt));

/* open the talker after everything else has parsed */
init_sockets();
#ifdef NETLINKS
  if (auto_connect) init_connections();
  else printf("Skipping connect stage.\n");
#endif

/* finish off the boot-up process */
reset_alarm();
printf("\n*** Booted with PID %d ***\n\n",(int)getpid());
sprintf(text,"*** SERVER BOOTED with PID %d %s ***\n\n",(int)getpid(),long_date(1));
write_syslog(text,0,SYSLOG);

/**** Main program loop. *****/
setjmp(jmpvar); /* jump to here if we crash and crash_action = IGNORE */
while(1) {
  /* set up mask then wait */
  setup_readmask(&readmask);
  if (select(FD_SETSIZE,&readmask,0,0,0)==-1) continue;
  /* check for connection to listen sockets */
  for(i=0;i<port_total;++i) {
    if (FD_ISSET(listen_sock[i],&readmask)) 
      accept_connection(listen_sock[i],i);
      }
  #ifdef NETLINKS
    /* Cycle through client-server connections to other talkers */
    for(nl=nl_first;nl!=NULL;nl=nl->next) {
      no_prompt=0;
      if (nl->type==UNCONNECTED || !FD_ISSET(nl->socket,&readmask)) 
	continue;
      /* See if remote site has disconnected */
      if (!(len=read(nl->socket,inpstr,sizeof(inpstr)-3))) {
	if (nl->stage==UP)
	  sprintf(text,"NETLINK: Remote disconnect by %s.\n",nl->service);
	else sprintf(text,"NETLINK: Remote disconnect by site %s.\n",nl->site);
	write_syslog(text,1,NETLOG);
	sprintf(text,"~OLSYSTEM:~RS Lost link to %s in the %s.\n",nl->service,nl->connect_room->name);
	write_room(NULL,text);
	shutdown_netlink(nl);
	continue;
        }
      inpstr[len]='\0'; 
      exec_netcom(nl,inpstr);
      }
  #endif
  /* Cycle through users. Use a while loop instead of a for because
     user structure may be destructed during loop in which case we
     may lose the user->next link. */
  user=user_first;
  while (user!=NULL) {
    next=user->next; /* store in case user object is destructed */
    /* If remote user or clone ignore */
    if (user->type!=USER_TYPE) {  user=next;  continue; }
    /* see if any data on socket else continue */
    if (!FD_ISSET(user->socket,&readmask)) { user=next;  continue; }    
    /* see if client (eg telnet) has closed socket */
    inpstr[0]='\0';
    if (!(len=read(user->socket,inpstr,sizeof(inpstr)))) {
      disconnect_user(user);  user=next;
      continue;
      }
    /* ignore control code replies */
    if ((unsigned char)inpstr[0]==255) { user=next;  continue; }
    /* Deal with input chars. If the following if test succeeds we
       are dealing with a character mode client so call function. */
    if (inpstr[len-1]>=32 || user->buffpos) {
      if (get_charclient_line(user,inpstr,len)) goto GOT_LINE;
      user=next;  continue;
      }
    else terminate(inpstr);
    
  GOT_LINE:
    no_prompt=0;  
    com_num=-1;
    force_listen=0; 
    destructed=0;
    user->buff[0]='\0';  
    user->buffpos=0;
    user->last_input=time(0);
    if (user->login>0) {
      login(user,inpstr);  user=next;  continue;  
      }
    
    /* If a dot on its own then execute last inpstr unless its a misc
       op or the user is on a remote site */
    if (!user->misc_op) {
      if (!strcmp(inpstr,".") && user->inpstr_old[0]) {
	strcpy(inpstr,user->inpstr_old);
	sprintf(text,"%s\n",inpstr);
	write_user(user,text);
        }
      /* else save current one for next time */
      else {
	if (inpstr[0]) strncpy(user->inpstr_old,inpstr,REVIEW_LEN);
        }
      }
    
    /* Main input check */
    clear_words();
    check_macros(user,inpstr);
    word_count=wordfind(inpstr);
    if (user->afk) {
      if (user->afk==2) {
	if (!word_count) {  
	  if (user->command_mode) prompt(user);
	  user=next;  continue;  
	  }
	if (strcmp((char *)crypt(word[0],crypt_salt),user->pass)) {
	  write_user(user,"Incorrect password.\n"); 
	  prompt(user);  user=next;  continue;
	  }
	cls(user);
	write_user(user,"Session unlocked, you are no longer AFK.\n");
        }	
      else write_user(user,"You are no longer AFK.\n");  
      user->afk_mesg[0]='\0';
      if (user->afkbuff[0][0]) 
	write_user(user,"\nYou have some tells in your afk review buffer.  Use ~FTrevafk~RS to view them.\n\n");
      if (user->vis) {
	sprintf(text,"%s comes back from being AFK.\n",user->recap);
	write_room_except(user->room,text,user);
        }
      if (user->afk==2) {
	user->afk=0;  prompt(user);  user=next;  continue;
        }
      user->afk=0;
      }
    if (!word_count) {
      if (misc_ops(user,inpstr))  {  user=next;  continue;  }
      #ifdef NETLINKS
        if (user->room==NULL) {
	  sprintf(text,"ACT %s NL\n",user->name);
	  write_sock(user->netlink->socket,text);
          }
      #endif
      if (user->command_mode) prompt(user);
      user=next;  continue;
      }
    if (misc_ops(user,inpstr))  {  user=next;  continue;  }
    com_num=-1;
    if (user->command_mode || strchr(".>;:</&![@'*+-,",inpstr[0])) 
      exec_com(user,inpstr);
    else say(user,inpstr);
    if (!destructed) {
      if (user->room!=NULL)  prompt(user); 
      else {
	switch(com_num) {
	  /* case -1  :  Not in enumerated values - Unknown command */
        #ifdef NETLINKS
	  case HOME:
        #endif
	  case QUIT:
	  case MODE:
	  case PROMPT: 
	  case SUICIDE:
	  case REBOOT:
	  case SHUTDOWN: prompt(user);
  	  default: break;
	  }
        }
      }
    user=next;
    } /* end while(user) */
  } /* end while(1) */
} /* main */



/******************************************************************************
 String functions - comparisons, convertions, etc
 *****************************************************************************/



/*** Attempt to get '\n' terminated line of input from a character
     mode client else store data read so far in user buffer. ***/
int get_charclient_line(UR_OBJECT user,char *inpstr,int len)
{
int l;

for(l=0;l<len;++l) {
  /* see if delete entered */
  if (inpstr[l]==8 || inpstr[l]==127) {
    if (user->buffpos) {
      user->buffpos--;
      if (user->charmode_echo) write_user(user,"\b \b");
      }
    continue;
    }
  user->buff[user->buffpos]=inpstr[l];
  /* See if end of line */
  if (inpstr[l]<32 || user->buffpos+2==ARR_SIZE) {
    terminate(user->buff);
    strcpy(inpstr,user->buff);
    if (user->charmode_echo) write_user(user,"\n");
    return 1;
    }
  user->buffpos++;
  }
if (user->charmode_echo
    && ((user->login!=LOGIN_PASSWD && user->login!=LOGIN_CONFIRM) || (password_echo || user->show_pass))) 
  write(user->socket,inpstr,len);
return 0;
}


/*** Put string terminate char. at first char < 32 ***/
void terminate(char *str)
{
int i;
for (i=0;i<ARR_SIZE;++i) if (*(str+i)<32) {  *(str+i)=0;  return;  } 
str[i-1]=0;
}


/*** Get words from sentence. This function prevents the words in the 
     sentence from writing off the end of a word array element. This is
     difficult to do with sscanf() hence I use this function instead. ***/
int wordfind(char *inpstr)
{
int wn,wpos;

wn=0; wpos=0;
do {
  while(*inpstr<33) if (!*inpstr++) return wn;
  while(*inpstr>32 && wpos<WORD_LEN-1) {
    word[wn][wpos]=*inpstr++;  wpos++;
    }
  word[wn][wpos]='\0';
  wn++;  wpos=0;
  } while (wn<MAX_WORDS);
return wn-1;
}


/*** clear word array etc. ***/
void clear_words()
{
int w;
for(w=0;w<MAX_WORDS;++w) word[w][0]='\0';
word_count=0;
}


/** check to see if string given is YES or NO **/
int yn_check(char *wd)
{
if (!strcmp(wd,"YES")) return 1;
if (!strcmp(wd,"NO")) return 0;
return -1;
}


/** check to see if string given is ON or OFF **/
int onoff_check(char *wd)
{
if (!strcmp(wd,"ON")) return 1;
if (!strcmp(wd,"OFF")) return 0;
return -1;
}


/** check to see if string given is OFF, MIN or MAX **/
int minmax_check(char *wd)
{
if (!strcmp(wd,"OFF")) return OFF;
if (!strcmp(wd,"MIN")) return MIN;
if (!strcmp(wd,"MAX")) return MAX;
return -1;
}


/*** Tell telnet not to echo characters - for password entry ***/
void echo_off(UR_OBJECT user)
{
char seq[4];

if (password_echo || user->show_pass) return;
sprintf(seq,"%c%c%c",255,251,1);
write_user(user,seq);
}


/*** Tell telnet to echo characters ***/
void echo_on(UR_OBJECT user)
{
char seq[4];

if (password_echo || user->show_pass) return;
sprintf(seq,"%c%c%c",255,252,1);
write_user(user,seq);
}


/*** Return pos. of second word in inpstr ***/
char *remove_first(char *inpstr)
{
char *pos=inpstr;
while(*pos<33 && *pos) ++pos;
while(*pos>32) ++pos;
while(*pos<33 && *pos) ++pos;
return pos;
}


/*** See if string contains any swearing ***/
int contains_swearing(char *str)
{
char *s;
int i;

if ((s=(char *)malloc(strlen(str)+1))==NULL) {
  write_syslog("ERROR: Failed to allocate memory in contains_swearing().\n",0,SYSLOG);
  return 0;
  }
strcpy(s,str);
strtolower(s); 
i=0;
while(swear_words[i][0]!='*') {
  if (strstr(s,swear_words[i])) {  free(s);  return 1;  }
  ++i;
  }
free(s);
return 0;
}


/* go through the given string and replace any of the words found in the
   swear_words array with the default swear censor, *swear_censor
   */
char *censor_swear_words(char *has_swears)
{
int i;
char *clean;

clean='\0';

i=0;
while (swear_words[i][0]!='*') {
  while(has_swears!=NULL) {
    clean=has_swears;
    has_swears=replace_string(clean,swear_words[i],swear_censor);
    }
  ++i;
  has_swears=clean;
  }
return clean;
}


/*** Count the number of colour commands in a string ***/
int colour_com_count(char *str)
{
char *s;
int i,cnt;

s=str;  cnt=0;
while(*s) {
  if (*s=='~') {
    ++s;
    for(i=0;i<NUM_COLS;++i) {
      if (!strncmp(s,colour_codes[i].txt_code,2)) { cnt++;  s++;  break;; }
      }
    continue;
    }
  ++s;
  }
return cnt;
}


/*** Strip out colour commands from string for when we are sending strings
     over a netlink to a talker that doesn't support them ***/
char *colour_com_strip(char *str)
{
char *s,*t;
static char text2[ARR_SIZE];
int i;

s=str;  t=text2;
while(*s) {
  if (*s=='~') {
    ++s;
    for(i=0;i<NUM_COLS;++i) {
      if (!strncmp(s,colour_codes[i].txt_code,2)) {  s++;  goto CONT;  }
      }
    --s;  *t++=*s;
    }
  else *t++=*s;
CONT:
  s++;
  }
*t='\0';
return text2;
}


/*** Convert string to upper case ***/
void strtoupper(char *str)
{
while(*str) {  *str=toupper(*str);  str++; }
}


/*** Convert string to lower case ***/
void strtolower(char *str)
{
while(*str) {  *str=tolower(*str);  str++; }
}


/*** Returns 1 if string is a positive number ***/
int is_number(char *str)
{
while(*str) if (!isdigit(*str++)) return 0;
return 1;
}



/*** Peforms the same as strstr, in that it returns a pointer to the first occurence
     of pat in str - except that this is performed case insensitive
     ***/
char *istrstr(char *str, char *pat)
{
char *pptr, *sptr, *start;
int  slen, plen;

slen=strlen(str);
plen=strlen(pat);
for (start=(char *)str,pptr=(char *)pat;slen>=plen;start++,slen--) {
  /* find start of pattern in string */
  while (toupper(*start)!=toupper(*pat)) {
    start++;  slen--;
    /* if pattern longer than string */
    if (slen<plen) return(NULL);
    }
  sptr=start;
  pptr=(char *)pat;
  while (toupper(*sptr)==toupper(*pptr)) {
    sptr++;  pptr++;
    /* if end of pattern then pattern was found */
    if (*pptr=='\0') return (start);
    } /* end while */
  } /* end for */
return(NULL);
}


/*** Take the string 'inpstr' and replace any occurence of 'old' with
     the string 'new'
     ***/
char *replace_string(char *inpstr, char *old, char *new)
{
int old_len,new_len;
char *x,*y;

if (NULL==(x=(char *)istrstr(inpstr,old))) return x;
old_len=strlen(old);
new_len=strlen(new);
memmove(y=x+new_len,x+old_len,strlen(x+old_len)+1);
memcpy(x,new,new_len);
return inpstr;
}


/*** Searches string s1 for string s2 ***/
int instr(char *s1, char *s2)
{
int f,g;
for (f=0;*(s1+f);++f) {
  for (g=0;;++g) {
    if (*(s2+g)=='\0' && g>0) return f;
    if (*(s2+g)!=*(s1+f+g)) break;
    }
  }
return -1;
}


/* used to copy out a chuck of text in macros */
void midcpy(char *strf, char *strt, int fr, int to)
{
int f;
for (f=fr;f<=to;++f) {
   if (!strf[f]) { strt[f-fr]='\0';  return; }
   strt[f-fr]=strf[f];
   }
strt[f-fr]='\0';
}


/*** Get ordinal value of a date and return the string ***/
char *ordinal_text(int num)
{
char *ords[]={"th","st","nd","rd"};

if (((num%=100)>9 && num<20) || (num%=10)>3) num=0;
return ords[num];
}


/*** Date string for board messages, mail, .who and .allclones, etc ***/
char *long_date(int which)
{
static char dstr[80];
int ap,hour;

if (thour>=12) {
  (thour>12) ? (hour=(int)thour-12) : (hour=12);
  ap=1;
  }
else {
  (!thour) ? (hour=12) : (hour=(int)thour);
  ap=0;
  }
if (which) sprintf(dstr,"on %s %d%s %s %d at %02d:%02d%s",day[twday],tmday,ordinal_text(tmday),month[tmonth],(int)tyear,hour,(int)tmin,!ap?"am":"pm");
else sprintf(dstr,"[ %s %d%s %s %d at %02d:%02d%s ]",day[twday],tmday,ordinal_text(tmday),month[tmonth],(int)tyear,hour,(int)tmin,!ap?"am":"pm");
return dstr;
}


/* takes string str and determines what smiley type it should have.  The type is then
   stored in 'type'.  The smiley type is determind by the last 2 characters in str.
*/
void smiley_type(char *str, char *type) {
switch(str[strlen(str)-1]) {
  case '?': strcpy(type,"ask");  break;
  case '!': strcpy(type,"exclaim");  break;
  case ')': if (str[strlen(str)-2]==':') strcpy(type,"smile");
            else if (str[strlen(str)-2]=='=') strcpy(type,"smile");
            else if (str[strlen(str)-2]==';') strcpy(type,"wink");
            else if (str[strlen(str)-2]=='8') strcpy(type,"glaze");
            else strcpy(type,"say");
            break;
  case '(': if (str[strlen(str)-2]==':' || str[strlen(str)-2]=='=') strcpy(type,"frown");
            else strcpy(type,"say");
            break;
  case ':': if (str[strlen(str)-2]=='(') strcpy(type,"smile");
            else if (str[strlen(str)-2]==')') strcpy(type,"frown");
            else strcpy(type,"say");
            break;
  case '=': if (str[strlen(str)-2]=='(') strcpy(type,"smile");
            else if (str[strlen(str)-2]==')') strcpy(type,"frown");
            else strcpy(type,"say");
            break;
  case ';': if (str[strlen(str)-2]=='(') strcpy(type,"wink");
            else if (str[strlen(str)-2]==')') strcpy(type,"frown");
            else strcpy(type,"say");
            break;
  case '8': if (str[strlen(str)-2]=='(') strcpy(type,"gaze");
            else strcpy(type,"say");
            break;
  default : strcpy(type,"say"); break;
  }
}



/******************************************************************************
 Object functions
 *****************************************************************************/



/*** Construct user/clone object ***/
UR_OBJECT create_user(void)
{
UR_OBJECT user;

if ((user=(UR_OBJECT)malloc(sizeof(struct user_struct)))==NULL) {
  write_syslog("ERROR: Memory allocation failure in create_user().\n",0,SYSLOG);
  return NULL;
  }
/* Append object into linked list. */
if (user_first==NULL) {  
  user_first=user;  user->prev=NULL;  
  }
else {  
  user_last->next=user;  user->prev=user_last;  
  }
user->next=NULL;
user_last=user;

/* initialise user structure */
user->type=USER_TYPE;
user->socket=-1;
user->attempts=0;
user->login=0;
user->port=0;
user->site_port=0;
user->name[0]='\0';
user->site[0]='\0';
reset_user(user);
return user;
}


/* reset the user variables */
void reset_user(UR_OBJECT user)
{
int i;

strcpy(user->email,"#UNSET");
strcpy(user->homepage,"#UNSET");
strcpy(user->verify_code,"#NONE");
strcpy(user->version,AMNUTSVER);
user->recap[0]='\0';
user->desc[0]='\0';
user->in_phrase[0]='\0';	
user->out_phrase[0]='\0';
user->afk_mesg[0]='\0';
user->pass[0]='\0';
user->last_site[0]='\0';
user->page_file[0]='\0';
user->mail_to[0]='\0';
user->inpstr_old[0]='\0';
user->buff[0]='\0'; 
user->tname[0]='\0';
user->tsite[0]='\0';
user->tport[0]='\0';
user->call[0]='\0';
user->samesite_check_store[0]='\0';
user->hang_word[0]='\0';
user->hang_word_show[0]='\0';
user->hang_guess[0]='\0';
user->invite_by[0]='\0';
user->logout_room[0]='\0';
user->date[0]='\0';
strcpy(user->icq,"#UNSET");
for (i=0; i<MAX_IGNORES; ++i) user->ignoreuser[i][0]='\0';
for (i=0;i<MAX_COPIES;++i) user->copyto[i][0]='\0';
for (i=0;i<10;++i) user->macros[i][0]='\0';
for (i=0;i<MAX_FRIENDS;++i) user->friend[i][0]='\0';
for(i=0;i<REVTELL_LINES;++i) user->afkbuff[i][0]='\0';
for(i=0;i<REVTELL_LINES;++i) user->editbuff[i][0]='\0';
for(i=0;i<REVTELL_LINES;++i) user->revbuff[i][0]='\0';
#ifdef NETLINKS
  user->netlink=NULL;
  user->pot_netlink=NULL; 
#endif
user->room=NULL;
user->invite_room=NULL;
user->malloc_start=NULL;
user->malloc_end=NULL;
user->owner=NULL;
user->wrap_room=NULL;
user->t_expire=time(0)+(NEWBIE_EXPIRES*86400);
user->read_mail=time(0);
user->last_input=time(0);
user->last_login=time(0);
user->level=NEW;
user->unarrest=NEW;
user->arrestby=0;
user->buffpos=0;
user->filepos=0;
user->command_mode=0;
user->vis=1;
user->ignall=0;
user->ignall_store=0;
user->ignshouts=0;
user->igntells=0;
user->muzzled=0;
user->remote_com=-1;
user->last_login_len=0;
user->total_login=0;
user->prompt=prompt_def;
user->colour=colour_def;
user->charmode_echo=charecho_def;
user->misc_op=0;
user->edit_op=0;
user->edit_line=0;
user->charcnt=0;
user->warned=0;
user->accreq=0;
user->afk=0;
user->revline=0;
user->clone_hear=CLONE_HEAR_ALL;
user->wipe_to=0;
user->wipe_from=0;
user->wrap=0;
user->pager=23;
user->logons=0;
user->expire=1;
user->lroom=0;
user->monitor=0;
user->gender=NEUTER;
user->age=0;
user->hideemail=1;
user->misses=0;
user->hits=0;
user->kills=0;
user->deaths=0;
user->bullets=6;
user->hps=10;
user->alert=0;
user->mail_verified=0;
user->autofwd=0;
user->editing=0;
user->hwrap_lev=0;
user->ignpics=0;
user->ignlogons=0;
user->igngreets=0;
user->ignwiz=0;
user->ignbeeps=0;
user->afkline=0;
user->editline=0;
user->show_pass=0;
user->samesite_all_store=0;
user->hwrap_id=0;
user->hwrap_same=0;
user->hwrap_func=0;
user->cmd_type=0;
user->hang_stage=-1;
user->show_rdesc=1;
user->lmail_lev=-3; /* has to be -3 */
for (i=0;i<MAX_XCOMS;i++) user->xcoms[i]=-1;
for (i=0;i<MAX_GCOMS;i++) user->gcoms[i]=-1;
for (i=0;i<MAX_PAGES;i++) user->pages[i]=0;
user->pagecnt=0;
user->login_prompt=1;
user->user_page_pos=0;
user->user_page_lev=0;
}


/*** Destruct an object. ***/
void destruct_user(UR_OBJECT user)
{
/* Remove from linked list */
if (user==user_first) {
  user_first=user->next;
  if (user==user_last) user_last=NULL;
  else user_first->prev=NULL;
  }
else {
  user->prev->next=user->next;
  if (user==user_last) { 
    user_last=user->prev;  user_last->next=NULL; 
    }
  else user->next->prev=user->prev;
  }
free(user);
destructed=1;
}


/*** Construct room object ***/
RM_OBJECT create_room(void)
{
RM_OBJECT room;
int i;

if ((room=(RM_OBJECT)malloc(sizeof(struct room_struct)))==NULL) {
  fprintf(stderr,"Amnuts: Memory allocation failure in create_room().\n");
  boot_exit(1);
  }
/* Append object into linked list. */
if (room_first==NULL) {  
  room_first=room;  room->prev=NULL;  
  }
else {  
  room_last->next=room;  room->prev=room_last;
  }
room->next=NULL;
room_last=room;

room->name[0]='\0';
room->label[0]='\0';
room->desc[0]='\0';
room->topic[0]='\0';
room->access=-1;
room->revline=0;
room->mesg_cnt=0;
#ifdef NETLINKS
  room->inlink=0;
  room->netlink=NULL;
  room->netlink_name[0]='\0';
#endif
room->next=NULL;
for(i=0;i<MAX_LINKS;++i) {
  room->link_label[i][0]='\0';  room->link[i]=NULL;
  }
for(i=0;i<REVIEW_LINES;++i) room->revbuff[i][0]='\0';
return room;
}


/*** Destruct a room object. ***/
void destruct_room(RM_OBJECT rm) {
/* Remove from linked list */
if (rm==room_first) {
  room_first=rm->next;
  if (rm==room_last) room_last=NULL;
  else room_first->prev=NULL;
  }
else {
  rm->prev->next=rm->next;
  if (rm==room_last) { 
    room_last=rm->prev;  room_last->next=NULL; 
    }
  else rm->next->prev=rm->prev;
  }
free(rm);
}


/* add a command to the commands linked list.  Get which command via the passed id
   int and the enum in the header file */
int add_command(int cmd_id)
{
int inserted;
struct command_struct *cmd,*tmp;

if ((cmd=(struct command_struct *)malloc(sizeof(struct command_struct)))==NULL) {
  write_syslog("ERROR: Memory allocation failure in add_command().\n",0,SYSLOG);
  return 0;
  }

/* 
   do an insertion sort on the linked list
   this could take a long time, but it only needs to be done once when booting, so it
   doesn't really matter
*/

inserted=0;
strcpy(cmd->name,command_table[cmd_id].name);
if (first_command==NULL) {
  first_command=cmd;
  cmd->prev=NULL;
  cmd->next=NULL;
  last_command=cmd;
  }
else {
  tmp=first_command;
  inserted=0;
  while(tmp!=NULL) {
    /* insert as first item in the list */
    if ((strcmp(cmd->name,tmp->name)<0) && tmp==first_command) {
      first_command->prev=cmd;
      cmd->prev=NULL;
      cmd->next=first_command;
      first_command=cmd;
      inserted=1;
      }
    /* insert in the middle of the list somewhere */
    else if (strcmp(cmd->name,tmp->name)<0) {
      tmp->prev->next=cmd;
      cmd->prev=tmp->prev;
      tmp->prev=cmd;
      cmd->next=tmp;
      inserted=1;
      }
    if (inserted) break;
    tmp=tmp->next;
    } /* end while */
  /* insert at the end of the list */
  if (!inserted) {
    last_command->next=cmd;
    cmd->prev=last_command;
    cmd->next=NULL;
    last_command=cmd;
    }
  } /* end else */

cmd->id=cmd_id;
strcpy(cmd->alias,command_table[cmd_id].alias);
cmd->min_lev=command_table[cmd_id].level;
cmd->function=command_table[cmd_id].function;
cmd->count=0;
return 1;
}


/* destruct command nodes */
int rem_command(int cmd_id)
{
struct command_struct *cmd;

cmd=first_command;
/* as command object not being passed, first find what node we want to delete
   for the id integer that *is* passed.
   */
while(cmd!=NULL) {
  if (cmd->id==cmd_id) break;
  cmd=cmd->next;
  }
if (cmd==first_command) {
  first_command=cmd->next;
  if (cmd==last_command) last_command=NULL;
  else first_command->prev=NULL;
  }
else {
  cmd->prev->next=cmd->next;
  if (cmd==last_command) { 
    last_command=cmd->prev;  last_command->next=NULL; 
    }
  else cmd->next->prev=cmd->prev;
  }
free(cmd);
return 1;
}


/* add a user node to the user linked list */
int add_user_node(char *name, int level)
{
struct user_dir_struct *new;

if ((new=(struct user_dir_struct *)malloc(sizeof(struct user_dir_struct)))==NULL) {
  write_syslog("ERROR: Memory allocation failure in add_user_node().\n",0,SYSLOG);
  return 0;
  }
if (first_dir_entry==NULL) {
  first_dir_entry=new;
  new->prev=NULL;
  }
else {
  last_dir_entry->next=new;
  new->prev=last_dir_entry;
  }
new->next=NULL;
last_dir_entry=new;

++user_count;
strcpy(new->name,name);
new->level=level;
new->date[0]='\0';
return 1;
}


/* remove a user node from the user linked list
   have needed to use an additional check for the correct user, ie, lev.
   if lev = -1 then don't do a check on the user node's level, else use
   the lev and the name as a check
   */
int rem_user_node(char *name, int lev)
{
int level,found;
struct user_dir_struct *entry;

entry=first_dir_entry;
found=0;
if (lev!=-1) {
  while(entry!=NULL) {
    if ((!strcmp(entry->name,name)) && (entry->level==lev)) {
      level=entry->level;  found=1;  break;
      }
    entry=entry->next;
    }
  }
else {
  while(entry!=NULL) {
    if (!strcmp(entry->name,name)) {
      level=entry->level;  found=1;  break;
      }
    entry=entry->next;
    }
  }
if (!found) return 0;
if (entry==first_dir_entry) {
  first_dir_entry=entry->next;
  if (entry==last_dir_entry) last_dir_entry=NULL;
  else first_dir_entry->prev=NULL;
  }
else {
  entry->prev->next=entry->next;
  if (entry==last_dir_entry) { 
    last_dir_entry=entry->prev;  last_dir_entry->next=NULL; 
    }
  else entry->next->prev=entry->prev;
  }
free(entry);
--user_count;
return 1;
}


/* put a date string in a node in the directory linked list that
   matches name
   */
void add_user_date_node(char *name, char *date)
{
struct user_dir_struct *entry;

for (entry=first_dir_entry;entry!=NULL;entry=entry->next) {
  if (!strcmp(entry->name,name)) {
    strcpy(entry->date,date);
    break;
    }
  }
return;
}


/* add a node to the wizzes linked list */
int add_wiz_node(char *name, int level)
{
struct wiz_list_struct *new;

if ((new=(struct wiz_list_struct *)malloc(sizeof(struct wiz_list_struct)))==NULL) {
  write_syslog("ERROR: Memory allocation failure in add_wiz_node().\n",0,SYSLOG);
  return 0;
  }
if (first_wiz_entry==NULL) {
  first_wiz_entry=new;
  new->prev=NULL;
  }
else {
  last_wiz_entry->next=new;
  new->prev=last_wiz_entry;
  }
new->next=NULL;
last_wiz_entry=new;

strcpy(new->name,name);
new->level=level;
return 1;
}


/* remove a node from the wizzes linked list
   have needed to use an additional check for the correct user, ie, lev.
   if lev = -1 then don't do a check on the user node's level, else use
   the lev and the name as a check
   */
int rem_wiz_node(char *name)
{
int level,found;
struct wiz_list_struct *entry;

entry=first_wiz_entry;
found=0;

while(entry!=NULL) {
  if (!strcmp(entry->name,name)) {
    level=entry->level;  found=1;  break;
    }
  entry=entry->next;
  }
if (!found) return 0;
if (entry==first_wiz_entry) {
  first_wiz_entry=entry->next;
  if (entry==last_wiz_entry) last_wiz_entry=NULL;
  else first_wiz_entry->prev=NULL;
  }
else {
  entry->prev->next=entry->next;
  if (entry==last_wiz_entry) { 
    last_wiz_entry=entry->prev;  last_wiz_entry->next=NULL; 
    }
  else entry->next->prev=entry->prev;
  }
free(entry);
return 1;
}


/*** alter the level of a node in the user linked list ***/
int user_list_level(char *name, int lvl)
{
struct user_dir_struct *entry;

entry=first_dir_entry;
while(entry!=NULL) {
  if (!strcmp(entry->name,name)) {
    entry->level=lvl;
    return(1);
    }
  entry=entry->next;
  }
return(0);
}



/******************************************************************************
 Perfom checks and searches
 *****************************************************************************/



/*** Check to see if the directory structure in USERFILES is correct, ie, there
     is one directory for each of the level names given in *level_name[]
     Also, check if level names are unique.
     ***/
void check_directories(void) {
struct stat stbuf;
int levels,found,i,j;
char dirname[80];

levels=found=0;
/* check to see how many levels there are */
while (level_name[levels][0]!='*') levels++;
/* Check for unique directory names */
for (i=0; i<levels-1; ++i) {
  for (j=i+1; j<levels; ++j) {
    if (!strcmp(level_name[i],level_name[j])) {
      fprintf(stderr,"Amnuts: Level names are not unique.\n");
      boot_exit(14);
      }
    }
  }
i=0;
/* check the directories needed exist */
strcpy(dirname,USERFILES);
if (stat(dirname,&stbuf)==-1) {
  fprintf(stderr,"Amnuts: Directory stat failure in check_directories().\n");
  boot_exit(15);
  }
if ((stbuf.st_mode & S_IFMT)!=S_IFDIR) goto SKIP;
sprintf(dirname,"%s/%s",USERFILES,USERMAILS);
if (stat(dirname,&stbuf)==-1) {
  fprintf(stderr,"Amnuts: Directory stat failure in check_directories().\n");
  boot_exit(15);
  }
if ((stbuf.st_mode & S_IFMT)!=S_IFDIR) goto SKIP;
sprintf(dirname,"%s/%s",USERFILES,USERPROFILES);
if (stat(dirname,&stbuf)==-1) {
  fprintf(stderr,"Amnuts: Directory stat failure in check_directories().\n");
  boot_exit(15);
  }
if ((stbuf.st_mode & S_IFMT)!=S_IFDIR) goto SKIP;
sprintf(dirname,"%s/%s",USERFILES,USERFRIENDS);
if (stat(dirname,&stbuf)==-1) {
  fprintf(stderr,"Amnuts: Directory stat failure in check_directories().\n");
  boot_exit(15);
  }
if ((stbuf.st_mode & S_IFMT)!=S_IFDIR) goto SKIP;
sprintf(dirname,"%s/%s",USERFILES,USERHISTORYS);
if (stat(dirname,&stbuf)==-1) {
  fprintf(stderr,"Amnuts: Directory stat failure in check_directories().\n");
  boot_exit(15);
  }
if ((stbuf.st_mode & S_IFMT)!=S_IFDIR) goto SKIP;
sprintf(dirname,"%s/%s",USERFILES,USERCOMMANDS);
if (stat(dirname,&stbuf)==-1) {
  fprintf(stderr,"Amnuts: Directory stat failure in check_directories().\n");
  boot_exit(15);
  }
if ((stbuf.st_mode & S_IFMT)!=S_IFDIR) goto SKIP;
sprintf(dirname,"%s/%s",USERFILES,USERMACROS);
if (stat(dirname,&stbuf)==-1) {
  fprintf(stderr,"Amnuts: Directory stat failure in check_directories().\n");
  boot_exit(15);
  }
if ((stbuf.st_mode & S_IFMT)!=S_IFDIR) goto SKIP;
sprintf(dirname,"%s/%s",USERFILES,USERROOMS);
if (stat(dirname,&stbuf)==-1) {
  fprintf(stderr,"Amnuts: Directory stat failure in check_directories().\n");
  boot_exit(15);
  }
if ((stbuf.st_mode & S_IFMT)!=S_IFDIR) goto SKIP;
return;
SKIP:
fprintf(stderr,"Amnuts: Directory structure is incorrect.\n");
boot_exit(16);
}


/*** Check to see if the pattern 'pat' appears in the string 'str'.
     Uses recursion to acheive this 
     ***/
int pattern_match(char *str, char *pat) {
int  i, slraw;

/* if end of both, strings match */
if ((*pat=='\0') && (*str=='\0')) return(1);
if (*pat=='\0') return(0);
if (*pat=='*') {
  if (*(pat+1)=='\0') return(1);
  for(i=0,slraw=strlen(str);i<=slraw;i++)
    if ((*(str+i)==*(pat+1)) || (*(pat+1)=='?'))
      if (pattern_match(str+i+1,pat+2)==1) return(1);
  } /* end if */
else {
  if (*str=='\0') return(0);
  if ((*pat=='?') || (*pat==*str))
    if (pattern_match(str+1,pat+1)==1) return(1);
  } /* end else */
return(0); 
}


/*** See if users site is banned ***/
int site_banned(char *sbanned, int new) {
FILE *fp;
char line[82],filename[80];

if (new) sprintf(filename,"%s/%s",DATAFILES,NEWBAN);
else sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
if (!(fp=fopen(filename,"r"))) return (0);
fscanf(fp,"%s",line);
while(!feof(fp)) {
  /* first do full name comparison */
  if (!strcmp(sbanned,line)) {  fclose(fp);  return 1;  }
  /* check using pattern matching */
  if (pattern_match(sbanned,line)) {
    fclose(fp);
    return(1);
    }
  fscanf(fp,"%s",line);
  }
fclose(fp);
return(0);
}


/*** See if user is banned ***/
int user_banned(char *name) {
FILE *fp;
char line[82],filename[80];

sprintf(filename,"%s/%s",DATAFILES,USERBAN);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%s",line);
while(!feof(fp)) {
  if (!strcmp(line,name)) {  fclose(fp);  return 1;  }
  fscanf(fp,"%s",line);
  }
fclose(fp);
return 0;
}


/*** Set room access back to public if not enough users in room ***/
void reset_access(RM_OBJECT rm) {
UR_OBJECT u;
int cnt;

if (rm==NULL || rm->access!=PRIVATE) return; 
cnt=0;
for(u=user_first;u!=NULL;u=u->next) if (u->room==rm) ++cnt;
if (cnt<min_private_users) {
  write_room(rm,"Room access returned to ~FGPUBLIC.\n");
  rm->access=PUBLIC;
  /* Reset any invites into the room & clear review buffer */
  for(u=user_first;u!=NULL;u=u->next) {
    if (u->invite_room==rm) u->invite_room=NULL;
    }
  clear_revbuff(rm);
  }
}


/*** Get user struct pointer from name ***/
UR_OBJECT get_user(char *name) {
UR_OBJECT u;

name[0]=toupper(name[0]);
/* Search for exact name */
for(u=user_first;u!=NULL;u=u->next) {
  if (u->login || u->type==CLONE_TYPE) continue;
  if (!strcmp(u->name,name))  return u;
  }
return NULL;
}


/*** Get user struct pointer from abreviated name ***/
UR_OBJECT get_user_name(UR_OBJECT user, char *i_name) {
UR_OBJECT u,last;
int found=0;
char name[USER_NAME_LEN], text2[ARR_SIZE];

last=NULL;
strncpy(name,i_name,USER_NAME_LEN);
name[0]=toupper(name[0]); text[0]=0;
for (u=user_first;u!=NULL;u=u->next)
  if (!strcmp(u->name,name) && u->room!=NULL) return u;
for (u=user_first;u!=NULL;u=u->next) {
  if (u->type==CLONE_TYPE) continue;
    if (instr(u->name,name) != -1) {
      strcat(text,u->name);
      strcat(text, "  ");
      found++;
      last=u;
      }
  } /* end for */
if (found == 0) return NULL;
if (found >1) {
  write_user(user, "~FR~OLName is not unique.\n\n");
  sprintf(text2,"   ~OL%s\n\n",text);
  write_user(user,text2);
  return NULL;
  }
else  return(last);
}


/*** Get room struct pointer from abbreviated name ***/
RM_OBJECT get_room(char *name) {
RM_OBJECT rm;

for(rm=room_first;rm!=NULL;rm=rm->next)
  if (!strncmp(rm->name,name,strlen(name))) return rm;
return NULL;
}


/*** Get room struct pointer from full name ***/
RM_OBJECT get_room_full(char *name) {
RM_OBJECT rm;

for(rm=room_first;rm!=NULL;rm=rm->next)
  if (!strcmp(rm->name,name)) return rm;
return NULL;
}


/*** Return level value based on level name ***/
int get_level(char *name) {
int i;

i=0;
while(level_name[i][0]!='*') {
  if (!strcmp(level_name[i],name)) return i;
  ++i;
  }
return -1;
}


/*** See if a user has access to a room. If room is fixed to private then
	it is considered a wizroom so grant permission to any user of WIZ and
	above for those. ***/
int has_room_access(UR_OBJECT user, RM_OBJECT rm) {
int i=0;

/* level room checks */
while (priv_room[i].name[0]!='*') {
  if (!strcmp(rm->name,priv_room[i].name)
      && user->level<priv_room[i].level
      && user->invite_room!=rm) return 0;
  i++;
  }
/* personal room checks */
if (rm->access==PERSONAL_UNLOCKED) return 1; /* open to all */
if ((rm->access==PERSONAL_LOCKED && is_my_room(user,rm))
    || has_room_key(user->name,rm)
    || user->level==GOD
    || user->invite_room==rm) return 1;
if ((rm->access==PERSONAL_LOCKED && !is_my_room(user,rm))
    && user->level<GOD
    && user->invite_room!=rm) return 0;
/* fixed room checks */
if ((rm->access & PRIVATE) 
    && user->level<gatecrash_level 
    && user->invite_room!=rm
    && !((rm->access & FIXED) && user->level>=WIZ)) return 0;
/* have access */
return 1;
}


/* Check the room you're logging into isn't private */
void check_start_room(UR_OBJECT user) {
RM_OBJECT rm;

if (!user->logout_room[0] || !user->lroom) {
  user->room=room_first;
  return;
  }
rm=get_room(user->logout_room);
if (rm==NULL) {
  user->room=room_first;
  return;
  }
if (rm->access==PRIVATE || (rm->access==FIXED_PRIVATE && user->level<WIZ)) {
  sprintf(text,"\nThe room you logged out of is now private - connecting into the %s.\n\n",room_first->name);
  write_user(user,text);
  user->room=room_first;
  return;
  }
user->room=rm;
if (user->lroom==2) sprintf(text,"\nYou have been ~FRshackled~RS to the ~FG%s~RS room.\n\n",rm->name);
else sprintf(text,"\nYou are connecting into the ~FG%s~RS room.\n\n",rm->name);
write_user(user,text);
}


/*** find out if a user is listed in the user linked list ***/
int find_user_listed(char *name) {
struct user_dir_struct *entry;

name[0]=toupper(name[0]);
for (entry=first_dir_entry;entry!=NULL;entry=entry->next)
  if (!strcmp(entry->name,name)) return 1;
return 0;
}


/*** check to see if a given email has the correct format ***/
int validate_email(char *email) {
int dots;
char *p;

dots=0;
p=email;
if (!*p) return 0;
while (*p!='@' && *p) {
  if (!isalnum(*p) && *p!='.' && *p!='_' && *p!='-') return 0;
  p++;
  }
if (*p!='@') return 0;
p++;
if (*p==' ' || *p=='.' || *p=='@' || !*p) return 0;
while (*p) {
  while (*p!='.') {
    if (!*p)
      if (!(dots)) return 0;
      else return 1;
    p++;
    } /* end while */
  dots++;
  p++;
  if (*p==' ' || *p=='.' || !*p) return 0;
  if (!*p) return 1;
  } /* end while */
return 1;
}


/*** Checks to see if a user with the given name is currently logged on ***/
int user_logged_on(char *name) {
UR_OBJECT u;

for (u=user_first;u!=NULL;u=u->next) if (!strcmp(name,u->name)) return 1;
return 0;
}


/*** Checks to see if the room a user is in is private ***/
int in_private_room(UR_OBJECT user) {
if (user->room->access==PRIVATE || user->room->access==FIXED_PRIVATE)  return 1;
return 0;
}


/*** Check to see if a command has been given to a user ***/
int has_gcom(UR_OBJECT user, int cmd_id) {
int i;

for (i=0;i<MAX_GCOMS;i++) if (user->gcoms[i]==cmd_id) return 1;
return 0;
}


/*** Check to see if a command has been taken from a user ***/
int has_xcom(UR_OBJECT user, int cmd_id) {
int i;

for (i=0;i<MAX_XCOMS;i++) if (user->xcoms[i]==cmd_id) return 1;
return 0;
}


/**** check to see if the room given is a personal room ***/
int is_personal_room(RM_OBJECT rm) {
  if (rm->access==PERSONAL_LOCKED || rm->access==PERSONAL_UNLOCKED) return 1;
  return 0;
}


/**** find out if the room given is the perosnal room of the user ***/
int is_my_room(UR_OBJECT user,RM_OBJECT rm) {
char name[ROOM_NAME_LEN+1];

sprintf(name,"(%s)",user->name);
strtolower(name);
if (!strcmp(name,rm->name)) return 1;
return 0;
}


/*** check to see how many people are in a given room ***/
int room_visitor_count(RM_OBJECT rm) {
UR_OBJECT u;
int cnt=0;

if (rm==NULL) return cnt;
for (u=user_first;u!=NULL;u=u->next) if (u->room==rm) ++cnt;
return cnt;
}


/*** See if user is banned ***/
int has_room_key(char *visitor,RM_OBJECT rm) {
FILE *fp;
char line[82],filename[80],rmname[USER_NAME_LEN];

/* get user's name from room name */
midcpy(rm->name,rmname,1,strlen(rm->name)-2);
rmname[0]=toupper(rmname[0]);
/* check if user is listed */
sprintf(filename,"%s/%s/%s.K",USERFILES,USERROOMS,rmname);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%s",line);
while(!feof(fp)) {
  if (!strcmp(line,visitor)) {  fclose(fp);  return 1;  }
  fscanf(fp,"%s",line);
  }
fclose(fp);
return 0;
}



/******************************************************************************
 Setting up of the sockets
 *****************************************************************************/



/*** Set up readmask for select ***/
void setup_readmask(fd_set *mask) {
UR_OBJECT user;
#ifdef NETLINKS
  NL_OBJECT nl;
#endif
int i;
  
FD_ZERO(mask);
for(i=0;i<port_total;++i) FD_SET(listen_sock[i],mask);
/* Do users */
for (user=user_first;user!=NULL;user=user->next) 
  if (user->type==USER_TYPE) FD_SET(user->socket,mask);
/* Do client-server stuff */
#ifdef NETLINKS
  for(nl=nl_first;nl!=NULL;nl=nl->next) 
    if (nl->type!=UNCONNECTED) FD_SET(nl->socket,mask);
#endif
}


/*** Accept incoming connections on listen sockets ***/
void accept_connection(int lsock, int num) {
UR_OBJECT user;
#ifdef NETLINKS
  NL_OBJECT create_netlink();
#endif
char named_site[80],ip_site[16],motdname[80];
struct sockaddr_in acc_addr;
struct hostent *host;
int accept_sock,size;

named_site[0]=ip_site[0]='\0';
size=sizeof(struct sockaddr_in);
accept_sock=accept(lsock,(struct sockaddr *)&acc_addr,&size);
#ifdef NETLINKS
if (num==2) {
  accept_server_connection(accept_sock,acc_addr);
  return;
  }
#endif
/* Get number addr */
strcpy(ip_site,(char *)inet_ntoa(acc_addr.sin_addr));
/* Get named site
   Use the compile option -DRESOLVEIP if your talker is hanging for no reason.
   Hanging usually happens on BSD systems when using gethostbyaddr and this attempts
   to fix it.
*/
#ifdef RESOLVEIP
  strcpy(named_site,((char *)resolve_ip(ip_site)));
#else
  if ((host=gethostbyaddr((char *)&acc_addr.sin_addr,4,AF_INET))!=NULL) {
    strcpy(named_site,host->h_name);
    strtolower(named_site);
    }
  else strncpy(named_site,ip_site,strlen(ip_site)-1);
#endif
if (site_banned(ip_site,0) || site_banned(named_site,0)) {
  write_sock(accept_sock,"\n\rLogins from your site/domain are banned.\n\n\r");
  close(accept_sock);
  sprintf(text,"Attempted login from banned site %s (%s).\n",named_site,ip_site);
  write_syslog(text,1,SYSLOG);
  return;
  }
/* get random motd1 and send  pre-login message */
if (motd1_cnt) {
  sprintf(motdname,"%s/motd1/motd%d",MOTDFILES,(get_motd_num(1)));
  more(NULL,accept_sock,motdname);
  }
else {
  sprintf(text,"Welcome to %s!\n\n\rSorry, but the login screen appears to be missing at this time.\n\r",talker_name);
  write_sock(accept_sock,text);
  }
if (num_of_users+num_of_logins>=max_users && !num) {
  write_sock(accept_sock,"\n\rSorry, but we cannot accept any more connections at the moment.  Please try later.\n\n\r");
  close(accept_sock);  
  return;
  }
if ((user=create_user())==NULL) {
  sprintf(text,"\n\r%s: unable to create session.\n\n\r",syserror);
  write_sock(accept_sock,text);
  close(accept_sock);  
  return;
  }
user->socket=accept_sock;
user->login=LOGIN_NAME;
user->last_input=time(0);
if (!num) user->port=port[0]; 
else {
  user->port=port[1];
  write_user(user,"** Wizport login **\n\n");
  }
strcpy(user->site,named_site);
strcpy(user->ipsite,ip_site);
user->site_port=(int)ntohs(acc_addr.sin_port);
echo_on(user);
write_user(user,"Give me a name: ");
num_of_logins++;
}


/*** Resolve an IP address if the -DRESOLVEIP compile option was used.
     This code was written by tref, and submitted to ewtoo.org by ruGG.
     Claimed useful for BSD systems in which gethostbyaddr() calls caused
     extreme hanging/blocking of the talker.  Note, popen is a blocking 
     call however.
     ***/
char *resolve_ip(char* host) {
FILE* hp;
static char str[256];
char *txt,*t;

sprintf(str,"/usr/bin/host %s",host);
hp=popen(str,"r");

*str=0;
fgets(str,255,hp);
pclose(hp);

txt=strchr(str,':');
if (txt) {
  txt++;
  while (isspace(*txt)) txt++;  t=txt;
  while (*t && *t!='\n') t++;   *t=0;
  return(txt);
  }
return(host);
}


/*** Initialise sockets on ports ***/
void init_sockets(void) {
struct sockaddr_in bind_addr;
int i,on,size;

#ifdef NETLINKS
  printf("Initialising sockets on ports: %d, %d, %d\n",port[0],port[1],port[2]);
#else
  printf("Initialising sockets on ports: %d, %d\n",port[0],port[1]);
#endif
on=1;
size=sizeof(struct sockaddr_in);
bind_addr.sin_family=AF_INET;
bind_addr.sin_addr.s_addr=INADDR_ANY;
for(i=0;i<port_total;++i) {
  /* create sockets */
  if ((listen_sock[i]=socket(AF_INET,SOCK_STREAM,0))==-1) boot_exit(i+2);
  /* allow reboots on port even with TIME_WAITS */
  setsockopt(listen_sock[i],SOL_SOCKET,SO_REUSEADDR,(char *)&on,sizeof(on));
  /* bind sockets and set up listen queues */
  bind_addr.sin_port=htons(port[i]);
  if (bind(listen_sock[i],(struct sockaddr *)&bind_addr,size)==-1) boot_exit(i+5);
  if (listen(listen_sock[i],10)==-1) boot_exit(i+8);
  /* Set to non-blocking */
  fcntl(listen_sock[i],F_SETFL,O_NDELAY);
  }
}



/******************************************************************************
 The loading up and parsing of the configuration file
 *****************************************************************************/



void load_and_parse_config(void) {
FILE *fp;
char line[81]; /* Should be long enough */
char c,filename[80];
int i,section_in,got_init,got_rooms,got_topics;
RM_OBJECT rm1,rm2;
#ifdef NETLINKS
  NL_OBJECT nl;
#endif

section_in=0;
got_init=0;
got_rooms=0;
got_topics=0;

sprintf(filename,"%s/%s",DATAFILES,confile);
printf("Parsing config file \"%s\"...\n",filename);
if (!(fp=fopen(filename,"r"))) {
  perror("Amnuts: Can't open config file");  boot_exit(1);
  }
/* Main reading loop */
config_line=0;
fgets(line,81,fp);
while(!feof(fp)) {
  config_line++;
  for(i=0;i<8;++i) wrd[i][0]='\0';
  sscanf(line,"%s %s %s %s %s %s %s %s",wrd[0],wrd[1],wrd[2],wrd[3],wrd[4],wrd[5],wrd[6],wrd[7]);
  if (wrd[0][0]=='#' || wrd[0][0]=='\0') {
    fgets(line,81,fp);  continue;
    }
  /* See if new section */
  if (wrd[0][strlen(wrd[0])-1]==':') {
    if (!strcmp(wrd[0],"INIT:")) section_in=1;
    else if (!strcmp(wrd[0],"ROOMS:")) section_in=2;
    else if (!strcmp(wrd[0],"TOPICS:")) section_in=3;
    else if (!strcmp(wrd[0],"SITES:")) section_in=4;
    else {
      fprintf(stderr,"Amnuts: Unknown section header on line %d.\n",config_line);
      fclose(fp);  boot_exit(1);
      }
    }
  switch(section_in) {
    case 1: parse_init_section();  got_init=1; break;
    case 2: parse_rooms_section(); got_rooms=1; break;
    case 3: parse_topics_section(remove_first(line)); got_topics=1; break;
    case 4:
      #ifdef NETLINKS
        parse_sites_section(); break;
      #else
	break;
      #endif
    default:
      fprintf(stderr,"Amnuts: Section header expected on line %d.\n",config_line);
      fclose(fp);  boot_exit(1);
    }
  fgets(line,81,fp);
  }
fclose(fp);
/* See if required sections were present (SITES and TOPICS is optional) and if
   required parameters were set. */
if (!got_init) {
  fprintf(stderr,"Amnuts: INIT section missing from config file.\n");
  boot_exit(1);
  }
if (got_topics && !got_rooms) {
  fprintf(stderr,"Amnuts: TOPICS section must come after ROOMS section in the config file.\n");
  boot_exit(1);
  }
if (!got_rooms) {
  fprintf(stderr,"Amnuts: ROOMS section missing from config file.\n");
  boot_exit(1);
  }
if (!port[0]) {
  fprintf(stderr,"Amnuts: Main port number not set in config file.\n");
  boot_exit(1);
  }
if (!port[1]) {
  fprintf(stderr,"Amnuts: Wiz port number not set in config file.\n");
  boot_exit(1);
  }
#ifdef NETLINKS
if (!port[2]) {
  fprintf(stderr,"Amnuts: Link port number not set in config file.\n");
  boot_exit(1);
  }
if (!verification[0]) {
  fprintf(stderr,"Amnuts: Verification not set in config file.\n");
  boot_exit(1);
  }
if (port[0]==port[1] || port[1]==port[2] || port[0]==port[2]) {
#else
if (port[0]==port[1]) {
#endif
  fprintf(stderr,"Amnuts: Port numbers must be unique.\n");
  boot_exit(1);
  }
if (room_first==NULL) {
  fprintf(stderr,"Amnuts: No rooms configured in config file.\n");
  boot_exit(1);
  }

/* Parsing done, now check data is valid. Check room stuff first. */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
  for(i=0;i<MAX_LINKS;++i) {
    if (!rm1->link_label[i][0]) break;
    for(rm2=room_first;rm2!=NULL;rm2=rm2->next) {
      if (rm1==rm2) continue;
      if (!strcmp(rm1->link_label[i],rm2->label)) {
	rm1->link[i]=rm2;
	break;
        }
      }
    if (rm1->link[i]==NULL) {
      fprintf(stderr,"Amnuts: Room %s has undefined link label '%s'.\n",rm1->name,rm1->link_label[i]);
      boot_exit(1);
      }
    }
  }

#ifdef NETLINKS
/* Check external links */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
  for(nl=nl_first;nl!=NULL;nl=nl->next) {
    if (!strcmp(nl->service,rm1->name)) {
      fprintf(stderr,"Amnuts: Service name %s is also the name of a room.\n",nl->service);
      boot_exit(1);
      }
    if (rm1->netlink_name[0] && !strcmp(rm1->netlink_name,nl->service)) {
      rm1->netlink=nl;
      break;
      }
    }
  if (rm1->netlink_name[0] && rm1->netlink==NULL) {
    fprintf(stderr,"Amnuts: Service name %s not defined for room %s.\n",rm1->netlink_name,rm1->name);
    boot_exit(1);
    }
  }
#endif

/* Load room descriptions */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
  sprintf(filename,"%s/%s.R",DATAFILES,rm1->name);
  if (!(fp=fopen(filename,"r"))) {
    fprintf(stderr,"Amnuts: Can't open description file for room %s.\n",rm1->name);
    sprintf(text,"ERROR: Couldn't open description file for room %s.\n",rm1->name);
    write_syslog(text,0,SYSLOG);
    continue;
    }
  i=0;
  c=getc(fp);
  while(!feof(fp)) {
    if (i==ROOM_DESC_LEN) {
      fprintf(stderr,"Amnuts: Description too long for room %s.\n",rm1->name);
      sprintf(text,"ERROR: Description too long for room %s.\n",rm1->name);
      write_syslog(text,0,SYSLOG);
      break;
      }
    rm1->desc[i]=c;  
    c=getc(fp);
    ++i;
    }
  rm1->desc[i]='\0';
  fclose(fp);
  }
}


/*** Parse init section ***/
void parse_init_section(void) {
static int in_section=0;
int op,val;
char *options[]={ 
  "mainport","wizport","linkport","system_logging","minlogin_level","mesg_life",
  "wizport_level","prompt_def","gatecrash_level","min_private","ignore_mp_level",
  "rem_user_maxlevel","rem_user_deflevel","verification","mesg_check_time",
  "max_users","heartbeat","login_idle_time","user_idle_time","password_echo",
  "ignore_sigterm","auto_connect","max_clones","ban_swearing","crash_action",
  "colour_def","time_out_afks","charecho_def","time_out_maxlevel","auto_purge",
  "allow_recaps","auto_promote","personal_rooms","random_motds","startup_room_parse",
  "*"
  };

if (!strcmp(wrd[0],"INIT:")) { 
  if (++in_section>1) {
    fprintf(stderr,"Amnuts: Unexpected INIT section header on line %d.\n",config_line);
    boot_exit(1);
    }
  return;
  }
op=0;
while(strcmp(options[op],wrd[0])) {
  if (options[op][0]=='*') {
    fprintf(stderr,"Amnuts: Unknown INIT option on line %d.\n",config_line);
    boot_exit(1);
    }
  ++op;
  }
if (!wrd[1][0]) {
  fprintf(stderr,"Amnuts: Required parameter missing on line %d.\n",config_line);
  boot_exit(1);
  }
if (wrd[2][0] && wrd[2][0]!='#') {
  fprintf(stderr,"Amnuts: Unexpected word following init parameter on line %d.\n",config_line);
  boot_exit(1);
  }
val=atoi(wrd[1]);
switch(op) {
  case 0: /* main port */
  case 1: /* wiz */
#ifdef NETLINKS
  case 2: /* link */
#endif
    if ((port[op]=val)<1 || val>65535) {
      fprintf(stderr,"Amnuts: Illegal port number on line %d.\n",config_line);
      boot_exit(1);
      }
    return;
  
  case 3:  
    if ((system_logging=onoff_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: System_logging must be ON or OFF on line %d.\n",config_line);
      boot_exit(1);
      }
    return;
  
  case 4:
    if ((minlogin_level=get_level(wrd[1]))==-1) {
      if (strcmp(wrd[1],"NONE")) {
	fprintf(stderr,"Amnuts: Unknown level specifier for minlogin_level on line %d.\n",config_line);
	boot_exit(1);	
        }
      minlogin_level=-1;
      }
    return;
    
  case 5:  /* message lifetime */
    if ((mesg_life=val)<1) {
      fprintf(stderr,"Amnuts: Illegal message lifetime on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 6: /* wizport_level */
    if ((wizport_level=get_level(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Unknown level specifier for wizport_level on line %d.\n",config_line);
      boot_exit(1);	
      }
    return;

  case 7: /* prompt defaults */
    if ((prompt_def=onoff_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Prompt_def must be ON or OFF on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 8: /* gatecrash level */
    if ((gatecrash_level=get_level(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Unknown level specifier for gatecrash_level on line %d.\n",config_line);
      boot_exit(1);	
      }
    return;

  case 9:
    if (val<1) {
      fprintf(stderr,"Amnuts: Number too low for min_private_users on line %d.\n",config_line);
      boot_exit(1);
      }
    min_private_users=val;
    return;

  case 10:
    if ((ignore_mp_level=get_level(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Unknown level specifier for ignore_mp_level on line %d.\n",config_line);
      boot_exit(1);	
      }
    return;

  case 11: 
    /* Max level a remote user can remotely log in if he doesn't have a local
       account. ie if level set to WIZ a GOD can only be a WIZ if logging in 
       from another server unless he has a local account of level GOD */
    if ((rem_user_maxlevel=get_level(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Unknown level specifier for rem_user_maxlevel on line %d.\n",config_line);
      boot_exit(1);	
      }
    return;

  case 12:
    /* Default level of remote user who does not have an account on site and
       connection is from a server of version 3.3.0 or lower. */
    if ((rem_user_deflevel=get_level(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Unknown level specifier for rem_user_deflevel on line %d.\n",config_line);
      boot_exit(1);	
      }
    return;

#ifdef NETLINKS
  case 13:
    if (strlen(wrd[1])>VERIFY_LEN) {
      fprintf(stderr,"Amnuts: Verification too long on line %d.\n",config_line);
      boot_exit(1);	
      }
    strcpy(verification,wrd[1]);
    return;
#endif

  case 14: /* mesg_check_time */
    if (wrd[1][2]!=':'
	|| strlen(wrd[1])>5
	|| !isdigit(wrd[1][0]) 
	|| !isdigit(wrd[1][1])
	|| !isdigit(wrd[1][3]) 
	|| !isdigit(wrd[1][4])) {
      fprintf(stderr,"Amnuts: Invalid message check time on line %d.\n",config_line);
      boot_exit(1);
      }
    sscanf(wrd[1],"%d:%d",&mesg_check_hour,&mesg_check_min);
    if (mesg_check_hour>23 || mesg_check_min>59) {
      fprintf(stderr,"Amnuts: Invalid message check time on line %d.\n",config_line);
      boot_exit(1);	
      }
    return;

  case 15:
    if ((max_users=val)<1) {
      fprintf(stderr,"Amnuts: Invalid value for max_users on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 16:
    if ((heartbeat=val)<1) {
      fprintf(stderr,"Amnuts: Invalid value for heartbeat on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 17:
    if ((login_idle_time=val)<10) {
      fprintf(stderr,"Amnuts: Invalid value for login_idle_time on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 18:
    if ((user_idle_time=val)<10) {
      fprintf(stderr,"Amnuts: Invalid value for user_idle_time on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 19: 
    if ((password_echo=yn_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Password_echo must be YES or NO on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 20: 
    if ((ignore_sigterm=yn_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Ignore_sigterm must be YES or NO on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 21:
    if ((auto_connect=yn_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Auto_connect must be YES or NO on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 22:
    if ((max_clones=val)<0) {
      fprintf(stderr,"Amnuts: Invalid value for max_clones on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 23:
    if ((ban_swearing=minmax_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Ban_swearing must be OFF, MIN or MAX on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 24:
    if (!strcmp(wrd[1],"NONE")) crash_action=0;
    else if (!strcmp(wrd[1],"IGNORE")) crash_action=1;
    else if (!strcmp(wrd[1],"REBOOT")) crash_action=2;
    else {
      fprintf(stderr,"Amnuts: Crash_action must be NONE, IGNORE or REBOOT on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 25:
    if ((colour_def=onoff_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Colour_def must be ON or OFF on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 26:
    if ((time_out_afks=yn_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Time_out_afks must be YES or NO on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 27:
    if ((charecho_def=onoff_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Charecho_def must be ON or OFF on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 28:
    if ((time_out_maxlevel=get_level(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Unknown level specifier for time_out_maxlevel on line %d.\n",config_line);
      boot_exit(1);	
      }
    return;

  case 29: /* auto purge on boot up */
    if ((auto_purge=yn_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Auto_purge must be YES or NO on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 30: /* auto purge on boot up */
    if ((allow_recaps=yn_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Allow_recaps must be YES or NO on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 31: /* define whether auto promotes are on or off */
    if ((auto_promote=yn_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: auto_promote must be YES or NO on line %d.\n",config_line);
      boot_exit(1);
      } 
    return;

  case 32:
    if ((personal_rooms=yn_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Personal_rooms must be YES or NO on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 33:
    if ((random_motds=yn_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Random_motds must be YES or NO on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  case 34:
    if ((startup_room_parse=yn_check(wrd[1]))==-1) {
      fprintf(stderr,"Amnuts: Startup_room_parse must be YES or NO on line %d.\n",config_line);
      boot_exit(1);
      }
    return;

  } /* end switch */
}



/*** Parse rooms section ***/
void parse_rooms_section(void) {
static int in_section=0;
int i;
char *ptr1,*ptr2,c;
RM_OBJECT room;

if (!strcmp(wrd[0],"ROOMS:")) {
  if (++in_section>1) {
    fprintf(stderr,"Amnuts: Unexpected ROOMS section header on line %d.\n",config_line);
    boot_exit(1);
    }
  return;
  }
if (!wrd[2][0]) {
  fprintf(stderr,"Amnuts: Required parameter(s) missing on line %d.\n",config_line);
  boot_exit(1);
  }
if (strlen(wrd[0])>ROOM_LABEL_LEN) {
  fprintf(stderr,"Amnuts: Room label too long on line %d.\n",config_line);
  boot_exit(1);
  }
if (strlen(wrd[1])>ROOM_NAME_LEN) {
  fprintf(stderr,"Amnuts: Room name too long on line %d.\n",config_line);
  boot_exit(1);
  }
/* Check for duplicate label or name */
for(room=room_first;room!=NULL;room=room->next) {
  if (!strcmp(room->label,wrd[0])) {
    fprintf(stderr,"Amnuts: Duplicate room label on line %d.\n",config_line);
    boot_exit(1);
    }
  if (!strcmp(room->name,wrd[1])) {
    fprintf(stderr,"Amnuts: Duplicate room name on line %d.\n",config_line);
    boot_exit(1);
    }
  }
room=create_room();
strcpy(room->label,wrd[0]);
strcpy(room->name,wrd[1]);

/* Parse internal links bit ie hl,gd,of etc. MUST NOT be any spaces between
   the commas */
i=0;
ptr1=wrd[2];
ptr2=wrd[2];
while(1) {
  while(*ptr2!=',' && *ptr2!='\0') ++ptr2;
  if (*ptr2==',' && *(ptr2+1)=='\0') {
    fprintf(stderr,"Amnuts: Missing link label on line %d.\n",config_line);
    boot_exit(1);
    }
  c=*ptr2;  *ptr2='\0';
  if (!strcmp(ptr1,room->label)) {
    fprintf(stderr,"Amnuts: Room has a link to itself on line %d.\n",config_line);
    boot_exit(1);
    }
  strcpy(room->link_label[i],ptr1);
  if (c=='\0') break;
  if (++i>=MAX_LINKS) {
    fprintf(stderr,"Amnuts: Too many links on line %d.\n",config_line);
    boot_exit(1);
    }
  *ptr2=c;
  ptr1=++ptr2;  
  }

/* Parse access privs */
if (wrd[3][0]=='#') {  room->access=PUBLIC;  return;  }
if (!wrd[3][0] || !strcmp(wrd[3],"BOTH")) room->access=PUBLIC; 
else if (!strcmp(wrd[3],"PUB")) room->access=FIXED_PUBLIC; 
else if (!strcmp(wrd[3],"PRIV")) room->access=FIXED_PRIVATE;
else {
  fprintf(stderr,"Amnuts: Unknown room access type on line %d.\n",config_line);
  boot_exit(1);
  }
/* Parse external link stuff */
#ifdef NETLINKS
  if (!wrd[4][0] || wrd[4][0]=='#') return;
  if (!strcmp(wrd[4],"ACCEPT")) {  
    if (wrd[5][0] && wrd[5][0]!='#') {
      fprintf(stderr,"Amnuts: Unexpected word following ACCEPT keyword on line %d.\n",config_line);
      boot_exit(1);
      }
    room->inlink=1;  
    return;
    }
  if (!strcmp(wrd[4],"CONNECT")) {
    if (!wrd[5][0]) {
      fprintf(stderr,"Amnuts: External link name missing on line %d.\n",config_line);
      boot_exit(1);
      }
    if (wrd[6][0] && wrd[6][0]!='#') {
      fprintf(stderr,"Amnuts: Unexpected word following external link name on line %d.\n",config_line);
      boot_exit(1);
      }
    strcpy(room->netlink_name,wrd[5]);
    return;
    }
  fprintf(stderr,"Amnuts: Unknown connection option on line %d.\n",config_line);
  boot_exit(1);
#else
  return;
#endif
}



/*** Parse rooms desc (topic) section ***/
void parse_topics_section(char *topic) {
static int in_section=0;
int exists;
RM_OBJECT room;

if (!strcmp(wrd[0],"TOPICS:")) {
  if (++in_section>1) {
    fprintf(stderr,"Amnuts: Unexpected TOPICS section header on line %d.\n",config_line);
    boot_exit(1);
    }
  return;
  }
if (!wrd[1][0]) {
  fprintf(stderr,"Amnuts: Required parameter(s) missing on line %d.\n",config_line);
  boot_exit(1);
  }
/* Check to see if room exists */
exists=0;
for(room=room_first;room!=NULL;room=room->next)
  if (!strcmp(room->name,wrd[0])) {  ++exists;  break;  }
if (!exists) {
  fprintf(stderr,"Amnuts: Room does not exist on line %d.\n",config_line);
  boot_exit(1);
  }
if (topic[strlen(topic)-1]=='\n') topic[strlen(topic)-1]='\0';
strncpy(room->topic,topic,TOPIC_LEN);
}


#ifdef NETLINKS

/*** Parse sites section ***/
void parse_sites_section(void) {
NL_OBJECT nl;
static int in_section=0;

if (!strcmp(wrd[0],"SITES:")) { 
  if (++in_section>1) {
    fprintf(stderr,"Amnuts: Unexpected SITES section header on line %d.\n",config_line);
    boot_exit(1);
    }
  return;
  }
if (!wrd[3][0]) {
  fprintf(stderr,"Amnuts: Required parameter(s) missing on line %d.\n",config_line);
  boot_exit(1);
  }
if (strlen(wrd[0])>SERV_NAME_LEN) {
  fprintf(stderr,"Amnuts: Link name length too long on line %d.\n",config_line);
  boot_exit(1);
  }
if (strlen(wrd[3])>VERIFY_LEN) {
  fprintf(stderr,"Amnuts: Verification too long on line %d.\n",config_line);
  boot_exit(1);
  }
if ((nl=create_netlink())==NULL) {
  fprintf(stderr,"Amnuts: Memory allocation failure creating netlink on line %d.\n",config_line);
  boot_exit(1);
  }
if (!wrd[4][0] || wrd[4][0]=='#' || !strcmp(wrd[4],"ALL")) nl->allow=ALL;
else if (!strcmp(wrd[4],"IN")) nl->allow=IN;
else if (!strcmp(wrd[4],"OUT")) nl->allow=OUT;
else {
  fprintf(stderr,"Amnuts: Unknown netlink access type on line %d.\n",config_line);
  boot_exit(1);
  }
if ((nl->port=atoi(wrd[2]))<1 || nl->port>65535) {
  fprintf(stderr,"Amnuts: Illegal port number on line %d.\n",config_line);
  boot_exit(1);
  }
strcpy(nl->service,wrd[0]);
strtolower(wrd[1]);
strcpy(nl->site,wrd[1]);
strcpy(nl->verification,wrd[3]);
}

#endif


/*** Parse the user rooms ***/
void parse_user_rooms(void) {
DIR *dirp;
struct dirent *dp;
char dirname[80],name[USER_NAME_LEN];
RM_OBJECT rm;

sprintf(dirname,"%s/%s",USERFILES,USERROOMS);
if (!(dirp=opendir(dirname))) {
  fprintf(stderr,"Amnuts: Directory open failure in parse_user_rooms().\n");
  boot_exit(19);
  }
/* parse the names of the files but don't include . and .. */
while((dp=readdir(dirp))!=NULL) {
  if (!strcmp(dp->d_name,".") || !strcmp(dp->d_name,"..")) continue;
  if (strstr(dp->d_name,".R")) {
    strcpy(name,dp->d_name);
    name[strlen(name)-2]='\0';
    rm=create_room();
    if (!(personal_room_store(name,0,rm))) {
      write_syslog("ERROR: Could not read personal room attributes.  Using standard config.\n",1,SYSLOG);
      }
    strtolower(name);
    sprintf(rm->name,"(%s)",name);
    }
  }
(void) closedir(dirp);
}



/******************************************************************************
 Signal handlers and exit functions
 *****************************************************************************/



/*** Initialise the signal traps etc ***/
void init_signals(void)
{
SIGNAL(SIGTERM,sig_handler);
SIGNAL(SIGSEGV,sig_handler);
SIGNAL(SIGBUS,sig_handler);
SIGNAL(SIGILL,SIG_IGN);
SIGNAL(SIGTRAP,SIG_IGN);
SIGNAL(SIGIOT,SIG_IGN);
SIGNAL(SIGTSTP,SIG_IGN);
SIGNAL(SIGCONT,SIG_IGN);
SIGNAL(SIGHUP,SIG_IGN);
SIGNAL(SIGINT,SIG_IGN);
SIGNAL(SIGQUIT,SIG_IGN);
SIGNAL(SIGABRT,SIG_IGN);
SIGNAL(SIGFPE,SIG_IGN);
SIGNAL(SIGPIPE,SIG_IGN);
SIGNAL(SIGTTIN,SIG_IGN);
SIGNAL(SIGTTOU,SIG_IGN);
}


/*** Talker signal handler function. Can either shutdown, ignore or reboot
	if a unix error occurs though if we ignore it we're living on borrowed
	time as usually it will crash completely after a while anyway. ***/
void sig_handler(int sig)
{
force_listen=1;
dump_commands(sig);
switch(sig) {
  case SIGTERM:
    if (ignore_sigterm) {
      write_syslog("SIGTERM signal received - ignoring.\n",1,SYSLOG);
      return;
      }
    write_room(NULL,"\n\n~OLSYSTEM:~FR~LI SIGTERM received, initiating shutdown!\n\n");
    talker_shutdown(NULL,"a termination signal (SIGTERM)",0); 
    break; /* don't really need this here, but better safe... */

  case SIGSEGV:
    switch(crash_action) {
      case 0:	
	write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI PANIC - Segmentation fault, initiating shutdown!\n\n");
	talker_shutdown(NULL,"a segmentation fault (SIGSEGV)",0); 
	break;

      case 1:	
	write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI WARNING - A segmentation fault has just occured!\n\n");
	write_syslog("WARNING: A segmentation fault occured!\n",1,SYSLOG);
	longjmp(jmpvar,0);
	break;

      case 2:
	write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI PANIC - Segmentation fault, initiating reboot!\n\n");
	talker_shutdown(NULL,"a segmentation fault (SIGSEGV)",1); 
	break;
    } /* end switch */

  case SIGBUS:
    switch(crash_action) {
      case 0:
	write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI PANIC - Bus error, initiating shutdown!\n\n");
	talker_shutdown(NULL,"a bus error (SIGBUS)",0);
	break;

      case 1:
	write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI WARNING - A bus error has just occured!\n\n");
	write_syslog("WARNING: A bus error occured!\n",1,SYSLOG);
	longjmp(jmpvar,0);
	break;

      case 2:
	write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI PANIC - Bus error, initiating reboot!\n\n");
	talker_shutdown(NULL,"a bus error (SIGBUS)",1);
	break;
    } /* end switch */
  } /* end switch */
}


/*** Exit because of error during bootup ***/
void boot_exit(int code)
{
switch(code) {
  case 1:
    write_syslog("BOOT FAILURE: Error while parsing configuration file.\n",1,SYSLOG);
    exit(1);

  case 2:
    perror("Amnuts: Can't open main port listen socket");
    write_syslog("BOOT FAILURE: Can't open main port listen socket.\n",1,SYSLOG);
    exit(2);

  case 3:
    perror("Amnuts: Can't open wiz port listen socket");
    write_syslog("BOOT FAILURE: Can't open wiz port listen socket.\n",1,SYSLOG);
    exit(3);

  case 4:
    perror("Amnuts: Can't open link port listen socket");
    write_syslog("BOOT FAILURE: Can't open link port listen socket.\n",1,SYSLOG);
    exit(4);

  case 5:
    perror("Amnuts: Can't bind to main port");
    write_syslog("BOOT FAILURE: Can't bind to main port.\n",1,SYSLOG);
    exit(5);

  case 6:
    perror("Amnuts: Can't bind to wiz port");
    write_syslog("BOOT FAILURE: Can't bind to wiz port.\n",1,SYSLOG);
    exit(6);

  case 7:
    perror("Amnuts: Can't bind to link port");
    write_syslog("BOOT FAILURE: Can't bind to link port.\n",1,SYSLOG);
    exit(7);

  case 8:
    perror("Amnuts: Listen error on main port");
    write_syslog("BOOT FAILURE: Listen error on main port.\n",1,SYSLOG);
    exit(8);

  case 9:
    perror("Amnuts: Listen error on wiz port");
    write_syslog("BOOT FAILURE: Listen error on wiz port.\n",1,SYSLOG);
    exit(9);

  case 10:
    perror("Amnuts: Listen error on link port");
    write_syslog("BOOT FAILURE: Listen error on link port.\n",1,SYSLOG);
    exit(10);

  case 11:
    perror("Amnuts: Failed to fork");
    write_syslog("BOOT FAILURE: Failed to fork.\n",1,SYSLOG);
    exit(11);

  case 12:
    perror("Amnuts: Failed to parse user structure");
    write_syslog("BOOT FAILURE: Failed to parse user structure.\n",1,SYSLOG);
    exit(12);

  case 13:
    perror("Amnuts: Failed to parse user commands");
    write_syslog("BOOT FAILURE: Failed to parse user commands.\n",1,SYSLOG);
    exit(13);

  case 14:
    write_syslog("BOOT FAILURE: Level names are not unique.\n",1,SYSLOG);
    exit(14);

  case 15:
    write_syslog("BOOT FAILURE: Failed to read directory structure in USERFILES.\n",1,SYSLOG);
    exit(15);

  case 16:
    perror("Amnuts: Directory structure in USERFILES is incorrect");
    write_syslog("BOOT FAILURE: Directory structure in USERFILES is incorrect.\n",1,SYSLOG);
    exit(16);

  case 17:
    perror("Amnuts: Failed to create temp user structure");
    write_syslog("BOOT FAILURE: Failed to create temp user structure.\n",1,SYSLOG);
    exit(17);

  case 18:
    perror("Amnuts: Failed to parse a user structure");
    write_syslog("BOOT FAILURE: Failed to parse a user structure.\n",1,SYSLOG);
    exit(18);

  case 19:
    perror("Amnuts: Failed to open directory USERROOMS for reading");
    write_syslog("BOOT FAILURE: Failed to open directory USERROOMS.\n",1,SYSLOG);
    exit(19);

  case 20:
    perror("Amnuts: Failed to open directory in MOTDFILES for reading");
    write_syslog("BOOT FAILURE: Failed to open a directory MOTDFILES.\n",1,SYSLOG);
    exit(20);
  } /* end switch */
}



/******************************************************************************
 Event functions
 *****************************************************************************/



void do_events(int sig)
{
  set_date_time();
  check_reboot_shutdown();
  check_idle_and_timeout();
  #ifdef NETLINKS
    check_nethangs_send_keepalives(); 
  #endif
  check_messages(NULL,0);
  reset_alarm();
}


void reset_alarm(void)
{
  SIGNAL(SIGALRM,do_events);
  alarm(heartbeat);
}


/*** See if timed reboot or shutdown is underway ***/
void check_reboot_shutdown(void)
{
int secs;
char *w[]={ "~FRShutdown","~FYRebooting" };

if (rs_user==NULL) return;
rs_countdown-=heartbeat;
if (rs_countdown<=0) talker_shutdown(rs_user,NULL,rs_which);

/* Print countdown message every minute unless we have less than 1 minute
   to go when we print every 10 secs */
secs=(int)(time(0)-rs_announce);
if (rs_countdown>=60 && secs>=60) {
  sprintf(text,"~OLSYSTEM: %s in %d minute%s, %d second%s.\n",
	  w[rs_which],rs_countdown/60,PLTEXT_S(rs_countdown/60),rs_countdown%60,PLTEXT_S(rs_countdown%60));
  write_room(NULL,text);
  rs_announce=time(0);
  }
if (rs_countdown<60 && secs>=10) {
  sprintf(text,"~OLSYSTEM: %s in %d second%s.\n",w[rs_which],rs_countdown,PLTEXT_S(rs_countdown));
  write_room(NULL,text);
  rs_announce=time(0);
  }
}


/*** login_time_out is the length of time someone can idle at login, 
     user_idle_time is the length of time they can idle once logged in. 
     Also ups users total login time. ***/
void check_idle_and_timeout(void)
{
UR_OBJECT user,next;
int tm;

/* Use while loop here instead of for loop for when user structure gets
   destructed, we may lose ->next link and crash the program */
user=user_first;
while(user) {
  next=user->next;
  if (user->type==CLONE_TYPE) {  user=next;  continue;  }
  user->total_login+=heartbeat; 
  if (user->level>time_out_maxlevel) {  user=next;  continue;  }
  tm=(int)(time(0) - user->last_input);
  if (user->login && tm>=login_idle_time) {
    write_user(user,"\n\n*** Time out ***\n\n");
    disconnect_user(user);
    user=next;
    continue;
    }
  if (user->warned) {
    if (tm<user_idle_time-60) {  user->warned=0;  continue;  }
    if (tm>=user_idle_time) {
      write_user(user,"\n\n\07~FR~OL~LI*** You have been timed out. ***\n\n");
      disconnect_user(user);
      user=next;
      continue;
      }
    }
  if ((!user->afk || (user->afk && time_out_afks)) 
      && !user->login 
      && !user->warned
      && tm>=user_idle_time-60) {
    write_user(user,"\n\07~FY~OL~LI*** WARNING - Input within 1 minute or you will be disconnected. ***\n\n");
    user->warned=1;
    }
  user=next;
  }
}


/*** Remove any expired messages from boards unless force = 2 in which case
     just do a recount. ***/
void check_messages(UR_OBJECT user, int chforce)
{
RM_OBJECT rm;
FILE *infp,*outfp;
char id[82],filename[80],line[82],rmname[USER_NAME_LEN];
int valid,pt,write_rest;
int board_cnt,old_cnt,bad_cnt,tmp;
static int done=0;

infp=outfp=NULL;
switch(chforce) {
  case 0:
    if (mesg_check_hour==thour && mesg_check_min==tmin) {
      if (done) return;
      }
    else {  done=0;  return;  }
    break;
    
  case 1:
    printf("Checking boards...\n");

  case 2:
    if (word_count>=2) {
      strtolower(word[1]);
      if (strcmp(word[1],"motds")) {
	write_user(user,"Usage: recount [motds]\n");
	return;
        }
      if (!count_motds(1)) {
	write_user(user,"Sorry, could not recount the motds at this time.\n");
	write_syslog("ERROR: Could not recount motds in check_messages().\n",1,SYSLOG);
	return;
        }
      sprintf(text,"There %s %d login motd%s and %d post-login motd%s\n",PLTEXT_WAS(motd1_cnt),motd1_cnt,PLTEXT_S(motd1_cnt),motd2_cnt,PLTEXT_S(motd2_cnt));
      write_user(user,text);
      sprintf(text,"%s recounted the MOTDS.\n",user->name);
      write_syslog(text,1,SYSLOG);
      return;
      }
  }
done=1;
board_cnt=0;
old_cnt=0;
bad_cnt=0;

for(rm=room_first;rm!=NULL;rm=rm->next) {
  tmp=rm->mesg_cnt;  
  rm->mesg_cnt=0;
  if (rm->access==PERSONAL_LOCKED || rm->access==PERSONAL_UNLOCKED) {
    midcpy(rm->name,rmname,1,strlen(rm->name)-2);
    rmname[0]=toupper(rmname[0]);
    sprintf(filename,"%s/%s/%s.B",USERFILES,USERROOMS,rmname);
    }
  else sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
  if (!(infp=fopen(filename,"r"))) continue;
  if (chforce<2) {
    if (!(outfp=fopen("tempfile","w"))) {
      if (chforce) fprintf(stderr,"Amnuts: Couldn't open tempfile.\n");
      write_syslog("ERROR: Couldn't open tempfile in check_messages().\n",0,SYSLOG);
      fclose(infp);
      return;
      }
    }
  board_cnt++;
  /* We assume that once 1 in date message is encountered all the others
     will be in date too , hence write_rest once set to 1 is never set to
     0 again */
  valid=1; write_rest=0;
  fgets(line,82,infp); /* max of 80+newline+terminator = 82 */
  while(!feof(infp)) {
    if (*line=='\n') valid=1;
    sscanf(line,"%s %d",id,&pt);
    if (!write_rest) {
      if (valid && !strcmp(id,"PT:")) {
	if (chforce==2) rm->mesg_cnt++;
	else {
	  /* 86400 = num. of secs in a day */
	  if ((int)time(0) - pt < mesg_life*86400) {
	    fputs(line,outfp);
	    rm->mesg_cnt++;
	    write_rest=1;
	    }
	  else old_cnt++;
	  }
	valid=0;
        }
      }
    else {
      fputs(line,outfp);
      if (valid && !strcmp(id,"PT:")) {
	rm->mesg_cnt++;  valid=0;
        }
      }
    fgets(line,82,infp);
    }
  fclose(infp);
  if (chforce<2) {
    fclose(outfp);
    unlink(filename);
    if (!write_rest) unlink("tempfile");
    else rename("tempfile",filename);
    }
  if (rm->mesg_cnt!=tmp) bad_cnt++;
  }
switch(chforce) {
  case 0:
    if (bad_cnt) 
      sprintf(text,"CHECK_MESSAGES: %d file%s checked, %d had an incorrect message count, %d message%s deleted.\n",
	      board_cnt,PLTEXT_S(board_cnt),bad_cnt,old_cnt,PLTEXT_S(old_cnt));
    else sprintf(text,"CHECK_MESSAGES: %d file%s checked, %d message%s deleted.\n",board_cnt,PLTEXT_S(board_cnt),old_cnt,PLTEXT_S(old_cnt));
    write_syslog(text,1,SYSLOG);
    break;
    
  case 1:
    printf("  %d board file%s checked, %d out of date message%s found.\n",board_cnt,PLTEXT_S(board_cnt),old_cnt,PLTEXT_S(old_cnt));
    break;
  
  case 2:
    sprintf(text,"%d board file%s checked, %d had an incorrect message count.\n",board_cnt,PLTEXT_S(board_cnt),bad_cnt);
    write_user(user,text);
    sprintf(text,"%s forced a recount of the message boards.\n",user->name);
    write_syslog(text,1,SYSLOG);
  }
}


/*** Records when the user last logged on for use with the .last command ***/
void record_last_login(char *name)
{
int i;

for (i=LASTLOGON_NUM;i>0;i--) {
  strcpy(last_login_info[i].name,last_login_info[i-1].name);
  strcpy(last_login_info[i].time,last_login_info[i-1].time);
  last_login_info[i].on=last_login_info[i-1].on;
  }    
strcpy(last_login_info[0].name,name);
strcpy(last_login_info[0].time,long_date(1));
last_login_info[0].on=1;
}


/*** Records when the user last logged out for use with the .last command ***/
void record_last_logout(char *name)
{
int i;

i=0;
while (i<LASTLOGON_NUM) {
  if (!strcmp(last_login_info[i].name,name)) break;
  i++;
  }
if (i!=LASTLOGON_NUM) last_login_info[i].on=0;
}



/******************************************************************************
 Initializing of globals and other stuff
 *****************************************************************************/



/*** Initialise globals ***/
void init_globals(void)
{
int i;

for (i=0;i<port_total;i++) {
  port[i]=0;
  listen_sock[i]=0;
  }
#ifdef NETLINKS
  verification[0]='\0';
#endif
auto_connect=1;
max_users=50;
max_clones=1;
ban_swearing=0;
heartbeat=2;
keepalive_interval=60; /* DO NOT TOUCH!!! */
net_idle_time=300; /* Must be > than the above */
login_idle_time=180;
user_idle_time=300;
time_out_afks=0;
wizport_level=WIZ;
minlogin_level=-1;
mesg_life=1;
no_prompt=0;
num_of_users=0;
num_of_logins=0;
system_logging=1;
password_echo=0;
ignore_sigterm=0;
crash_action=2;
prompt_def=1;
colour_def=1;
charecho_def=0;
time_out_maxlevel=USER;
mesg_check_hour=0;
mesg_check_min=0;
rs_countdown=0;
rs_announce=0;
rs_which=-1;
rs_user=NULL;
gatecrash_level=GOD+1; /* minimum user level which can enter private rooms */
min_private_users=2; /* minimum num. of users in room before can set to priv */
ignore_mp_level=GOD; /* User level which can ignore the above var. */
rem_user_maxlevel=USER;
rem_user_deflevel=USER;
user_first=NULL;
user_last=NULL;
room_first=NULL;
room_last=NULL; /* This variable isn't used yet */
#ifdef NETLINKS
  nl_first=NULL;
  nl_last=NULL;
#endif
clear_words();
time(&boot_time);
logons_old=0;
logons_new=0;
purge_count=0;
purge_skip=0;
users_purged=0;
purge_date=1;
suggestion_count=0;
forwarding=1;
logon_flag=0;
auto_purge=0;
user_count=0;
first_dir_entry=NULL;
first_command=NULL;
allow_recaps=1;
for (i=0;i<LASTLOGON_NUM;i++) {
  last_login_info[i].name[0]='\0';
  last_login_info[i].time[0]='\0';
  last_login_info[i].on=0;
  }
auto_promote=1;
first_wiz_entry=NULL;
last_wiz_entry=NULL;
personal_rooms=1;
startup_room_parse=1;
for (i=0;i<16;i++) cmd_history[i][0]='\0';
motd1_cnt=0;
motd2_cnt=0;
random_motds=1;
last_cmd_cnt=0;
}


/*** Load the users details ***/
int load_user_details(UR_OBJECT user)
{
FILE *fp;
char line[82],filename[80];
int temp1,temp2,temp3,temp4;

sprintf(filename,"%s/%s.D",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) return 0;

/* Password */
fscanf(fp,"%s\n",user->pass);
/* version control of user structure */
fgets(line,82,fp);  line[strlen(line)-1]=0;  strcpy(user->version,line);
/* date when user last promoted/demoted */
fgets(line,82,fp);  line[strlen(line)-1]=0;  strcpy(user->date,line);
/* times, levels, and important stats */
fscanf(fp,"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",&temp1,&temp2,&user->last_login_len,&temp3,
       &user->level,&user->prompt,&user->muzzled,&user->charmode_echo,&user->command_mode,&user->vis,
       &user->monitor,&user->unarrest,&user->logons,&user->accreq,&user->mail_verified,&user->arrestby);
/* stats set using the 'set' function */
fscanf(fp,"%d %d %d %d %d %d %d %d %d %d %d %d %s",&user->gender,&user->age,&user->wrap,&user->pager,
       &user->hideemail,&user->colour,&user->lroom,&user->alert,&user->autofwd,&user->show_pass,
       &user->show_rdesc,&user->cmd_type,user->icq);
/* ignore status */
fscanf(fp,"%d %d %d %d %d %d %d %d",&user->ignall,&user->igntells,&user->ignshouts,&user->ignpics,
       &user->ignlogons,&user->ignwiz,&user->igngreets,&user->ignbeeps);
/* Gun fight information */
fscanf(fp,"%d %d %d %d %d %d",&user->hits,&user->misses,&user->deaths,&user->kills,&user->bullets,&user->hps);
/* user expires and date */
fscanf(fp,"%d %d",&user->expire,&temp4);
/* site address and last room they were in */
fscanf(fp,"%s %s %s\n",user->last_site,user->logout_room,user->verify_code);
/* general text stuff 
   Need to do the rest like this 'cos they may be more than 1 word each 
   possible for one colour for each letter, *3 for the code length */
fgets(line,USER_DESC_LEN+USER_DESC_LEN*3,fp);  line[strlen(line)-1]=0;  strcpy(user->desc,line);
fgets(line,PHRASE_LEN+PHRASE_LEN*3,fp);  line[strlen(line)-1]=0;  strcpy(user->in_phrase,line);
fgets(line,PHRASE_LEN+PHRASE_LEN*3,fp);  line[strlen(line)-1]=0;  strcpy(user->out_phrase,line);
fgets(line,81,fp);  line[strlen(line)-1]=0;  strcpy(user->email,line);
fgets(line,81,fp);  line[strlen(line)-1]=0;  strcpy(user->homepage,line);
fscanf(fp,"%s\n",user->recap);
fclose(fp);

if (strcmp(user->version,AMNUTSVER)) {
  /* As default, if the version cannot be read then it'll load up version 1.4.2 files */
  if (!strcmp(user->version,"2.0.0")) {
    load_oldversion_user(user,200);
    write_syslog("Reading old user file (version 2.0.0) in load_user_details()\n",1,SYSLOG);
    }
  else if (!strcmp(user->version,"2.0.1")) {
    load_oldversion_user(user,201);
    write_syslog("Reading old user file (version 2.0.1) in load_user_details()\n",1,SYSLOG);
    }
  else if (!strcmp(user->version,"2.1.0")) {
    load_oldversion_user(user,210);
    write_syslog("Reading old user file (version 2.1.0) in load_user_details()\n",1,SYSLOG);
    }
  else {
    load_oldversion_user(user,142);
    write_syslog("Reading old user file (version 1.4.2) in load_user_details()\n",1,SYSLOG);
    }
  strcpy(user->version,AMNUTSVER);
  if (user->level>NEW) user->accreq=1;
  }
else {
  user->last_login=(time_t)temp1;
  user->total_login=(time_t)temp2;
  user->read_mail=(time_t)temp3;
  user->t_expire=(time_t)temp4;
  }
if (!allow_recaps) strcpy(user->recap,user->name);
get_macros(user);
get_xgcoms(user);
get_friends(user);
return 1;
}


/*** Save a users stats ***/
int save_user_details(UR_OBJECT user, int save_current)
{
FILE *fp;
char filename[80];

if (user->type==REMOTE_TYPE || user->type==CLONE_TYPE) return 0;
sprintf(filename,"%s/%s.D",USERFILES,user->name);
if (!(fp=fopen(filename,"w"))) {
  sprintf(text,"%s: failed to save your details.\n",syserror);	
  write_user(user,text);
  sprintf(text,"SAVE_USER_STATS: Failed to save %s's details.\n",user->name);
  write_syslog(text,1,SYSLOG);
  return 0;
  }
/* Password */
fprintf(fp,"%s\n",user->pass);
/* Version control of user structure */
fprintf(fp,"%s\n",AMNUTSVER);
/* date when user was promoted */
fprintf(fp,"%s\n",user->date);
/* times, levels, and important stats */
if (save_current) fprintf(fp,"%d %d %d ",(int)time(0),(int)user->total_login,(int)(time(0)-user->last_login));
else fprintf(fp,"%d %d %d ",(int)user->last_login,(int)user->total_login,user->last_login_len);
fprintf(fp,"%d %d %d %d %d %d %d %d %d %d %d %d %d\n",(int)user->read_mail,user->level,user->prompt,
	user->muzzled,user->charmode_echo,user->command_mode,user->vis,user->monitor,user->unarrest,
	user->logons,user->accreq,user->mail_verified,user->arrestby);
/* stats set using the 'set' function */
fprintf(fp,"%d %d %d %d %d %d %d %d %d %d %d %d %s\n",user->gender,user->age,user->wrap,user->pager,
	user->hideemail,user->colour,user->lroom,user->alert,user->autofwd,user->show_pass,user->show_rdesc,
	user->cmd_type,user->icq);
/* ignore status */
fprintf(fp,"%d %d %d %d %d %d %d %d\n",user->ignall,user->igntells,user->ignshouts,user->ignpics,user->ignlogons,
	user->ignwiz,user->igngreets,user->ignbeeps);
/* Gun fight information */
fprintf(fp,"%d %d %d %d %d %d\n",user->hits,user->misses,user->deaths,user->kills,user->bullets,user->hps);
/* If user expires and time */
if (!save_current) fprintf(fp,"%d %d\n",user->expire,(int)user->t_expire);
else {
    if (user->level==NEW) fprintf(fp,"%d %d\n",user->expire,(int)(time(0)+(NEWBIE_EXPIRES*86400)));
    else fprintf(fp,"%d %d\n",user->expire,(int)(time(0)+(USER_EXPIRES*86400)));
    }
/* site address and last room they were in */
if (save_current) fprintf(fp,"%s %s %s\n",user->site,user->room->name,user->verify_code);
else fprintf(fp,"%s %s %s\n",user->last_site,user->logout_room,user->verify_code);
/* general text things */
fprintf(fp,"%s\n",user->desc);
fprintf(fp,"%s\n",user->in_phrase);
fprintf(fp,"%s\n",user->out_phrase);
fprintf(fp,"%s\n",user->email);
fprintf(fp,"%s\n",user->homepage);
fprintf(fp,"%s\n",user->recap);
fclose(fp);
return 1;
}



/* You can put any old version of load_user_details in here - minus the loading of macros -
   so that you don't have to nuke any userfiles if you upgrade the structure, or you don't
   have to use an external program to alter the userfiles.  (though one is included in the
   Amnuts distribution).
   If you have altered the version number but have not made any changes to what the user
   structure loads or saves then you can just put a 'return;' in this procedure and 
   nothing else.  But ONLY do this if you are POSITIVE that all users have the same user
   structure.
   Below is the loading for Amnuts version 1.4.2 and 2.x.x user structures.
   */
int load_oldversion_user(UR_OBJECT user,int version)
{
FILE *fp;
char line[81],filename[80];
int temp1,temp2,temp3,temp4,oldvote;

reset_user(user); /* make sure reads in fresh */
sprintf(filename,"%s/%s.D",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) return 0;

/* Password */
fscanf(fp,"%s\n",user->pass);
/* read version control and date last promoted if version 2.0.0, 2.0.1, 2.1.0 */
if (version>=200) {
  fgets(line,82,fp);  line[strlen(line)-1]=0;  strcpy(user->version,line);
  fgets(line,82,fp);  line[strlen(line)-1]=0;  strcpy(user->date,line);
  }
else {
  strcpy(user->version,"1.4.2");
  strcpy(user->date,long_date(1));
  }
if (version>=200) {
  /* times, levels, and important stats */
  fscanf(fp,"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",&temp1,&temp2,&user->last_login_len,&temp3,
	 &user->level,&user->prompt,&user->muzzled,&user->charmode_echo,&user->command_mode,&user->vis,
	 &user->monitor,&user->unarrest,&user->logons,&user->accreq,&user->mail_verified);
  }
else {
  /* times, levels, and important stats */
  fscanf(fp,"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",&temp1,&temp2,&user->last_login_len,&temp3,
	 &user->level,&user->prompt,&user->muzzled,&user->charmode_echo,&user->command_mode,&user->vis,
	 &user->monitor,&oldvote,&user->unarrest,&user->logons,&user->accreq,&user->mail_verified);
  }
if (version>=210) {
  /* stats set using the 'set' function */
  fscanf(fp,"%d %d %d %d %d %d %d %d %d %d %d %d %s",&user->gender,&user->age,&user->wrap,&user->pager,&user->hideemail,
	 &user->colour,&user->lroom,&user->alert,&user->autofwd,&user->show_pass,&user->show_rdesc,&user->cmd_type,user->icq);
  }
else if (version>=200) {
  /* stats set using the 'set' function */
  fscanf(fp,"%d %d %d %d %d %d %d %d %d %d %d %d",&user->gender,&user->age,&user->wrap,&user->pager,&user->hideemail,
	 &user->colour,&user->lroom,&user->alert,&user->autofwd,&user->show_pass,&user->show_rdesc,&user->cmd_type);
  }
else {
  /* stats set using the 'set' function */
  fscanf(fp,"%d %d %d %d %d %d %d %d %d %d",&user->gender,&user->age,&user->wrap,&user->pager,&user->hideemail,
	 &user->colour,&user->lroom,&user->alert,&user->autofwd,&user->show_pass);
  user->show_rdesc=1;
  user->cmd_type=0;
  }
/* ignore status */
fscanf(fp,"%d %d %d %d %d %d %d %d",&user->ignall,&user->igntells,&user->ignshouts,&user->ignpics,
       &user->ignlogons,&user->ignwiz,&user->igngreets,&user->ignbeeps);
/* Gun fight information */
fscanf(fp,"%d %d %d %d %d %d",&user->hits,&user->misses,&user->deaths,&user->kills,&user->bullets,&user->hps);
/* user expires and date */
fscanf(fp,"%d %d",&user->expire,&temp4);
/* site address and last room they were in */
fscanf(fp,"%s %s %s\n",user->last_site,user->logout_room,user->verify_code);
/* general text stuff 
   Need to do the rest like this 'cos they may be more than 1 word each 
   possible for one colour for each letter, *3 for the code length */
fgets(line,USER_DESC_LEN+USER_DESC_LEN*3,fp);  line[strlen(line)-1]=0;  strcpy(user->desc,line);
fgets(line,PHRASE_LEN+PHRASE_LEN*3,fp);  line[strlen(line)-1]=0;  strcpy(user->in_phrase,line);
fgets(line,PHRASE_LEN+PHRASE_LEN*3,fp);  line[strlen(line)-1]=0;  strcpy(user->out_phrase,line);
fgets(line,82,fp);  line[strlen(line)-1]=0;  strcpy(user->email,line);
fgets(line,82,fp);  line[strlen(line)-1]=0;  strcpy(user->homepage,line);
if (version>=200) {
  fscanf(fp,"%s\n",user->recap);
  }
else {
  strcpy(user->recap,user->name);
  }
fclose(fp);
user->last_login=(time_t)temp1;
user->total_login=(time_t)temp2;
user->read_mail=(time_t)temp3;
user->t_expire=(time_t)temp4;
return 1;
}


/*** Set global vars. hours,minutes,seconds,date,day,month,year ***/
void set_date_time(void)
{
struct tm *tm_struct; /* structure is defined in time.h */
time_t tm_num;

/* Set up the structure */
time(&tm_num);
tm_struct=localtime(&tm_num);

/* Get the values */
tday=tm_struct->tm_yday;
tyear=1900+tm_struct->tm_year; /* Will this work past the year 2000? Hmm... */
tmonth=tm_struct->tm_mon;
tmday=tm_struct->tm_mday;
twday=tm_struct->tm_wday;
thour=tm_struct->tm_hour;
tmin=tm_struct->tm_min;
tsec=tm_struct->tm_sec; 
}


/*** Get all users from the user directories and add them to the user lists.
     If verbose mode is on, then attempt to get date string as well
     ***/
void process_users(void)
{
char name[USER_NAME_LEN+3];
DIR *dirp;
struct dirent *dp;
UR_OBJECT u; 

/* open the directory file up */
dirp=opendir(USERFILES);
if (dirp==NULL) {
  fprintf(stderr,"Amnuts: Directory open failure in process_users().\n");
  boot_exit(12);
  }
if ((u=create_user())==NULL) {
  fprintf(stderr,"Amnuts: Create user failure in process_users().\n");
  (void) closedir(dirp);
  boot_exit(17);
  }
/* count up how many files in the directory - this include . and .. */
while((dp=readdir(dirp))!=NULL) {
  if (!strcmp(dp->d_name,".") || !strcmp(dp->d_name,"..")) continue;
  if (strstr(dp->d_name,".D")) {
    strcpy(name,dp->d_name);
    name[strlen(name)-2]='\0';
    strcpy(u->name,name);
    if (load_user_details(u)) {
      add_user_node(u->name,u->level);
      if (u->level>=WIZ) add_wiz_node(u->name,u->level);
      add_user_date_node(u->name,u->date);
      } /* end if */
    else {
      fprintf(stderr,"Amnuts: Could not load userfile for '%s' in process_users().\n",name);
      (void) closedir(dirp);
      boot_exit(18);
      }
    } /* end if */
  reset_user(u);
  } /* end while */
destruct_user(u);
(void) closedir(dirp);
}


/* count up how many users of a certain level and in total in the linked user list */
void count_users(void)
{
struct user_dir_struct *entry;
int i;

/* first zero out all level counts */
user_count=0;
for (i=JAILED;i<=GOD;i++) level_count[i]=0;
/* count up users */
for (entry=first_dir_entry;entry!=NULL;entry=entry->next) {
  level_count[entry->level]++;
  user_count++;
  }
}


/* Put commands in an ordered linked list for viewing with .help */
void parse_commands(void)
{
int cnt;

cnt=0;
while(command_table[cnt].name[0]!='*') {
  if (!(add_command(cnt))) {
    fprintf(stderr,"Amnuts: Memory allocation failure in parse_commands().\n");
    boot_exit(13);
    }
  ++cnt;
  }
return;
}


/* needs only doing once when booting */
void count_suggestions(void)
{
char filename[80],line[82],id[20];
FILE *fp;
int valid;

sprintf(filename,"%s/%s",MISCFILES,SUGBOARD);
if (!(fp=fopen(filename,"r"))) return;
valid=1;
fgets(line,82,fp);
while(!feof(fp)) {
  if (line[0]=='\n') valid=1;
  sscanf(line,"%s",id);
  if (valid && strstr(id,"From:")) {
    ++suggestion_count;
    valid=0;
    }
  fgets(line,82,fp);
  }
fclose(fp);
}


/* work out how many motds are stored - if an motd file is deleted after
   this count has been made then you need to recount them with the recount
   command.  If you fail to do this and something buggers up because the count
   is wrong then it's your own fault.
   */
int count_motds(int forcecnt)
{
char filename[80];
DIR *dirp;
struct dirent *dp; 
int i;

motd1_cnt=0;  motd2_cnt=0;
for (i=1;i<=2;i++) {
  /* open the directory file up */
  sprintf(filename,"%s/motd%d",MOTDFILES,i);
  if ((dirp=opendir(filename))==NULL) {
    switch(forcecnt) {
      case 0:
	fprintf(stderr,"Amnuts: Directory open failure in count_motds().\n");
	boot_exit(20);
      case 1: return 0;
      }
    }
  /* count up how many files in the directory - not including . and .. */
  while((dp=readdir(dirp))!=NULL) {
    if (strstr(dp->d_name,"motd")) (i==1) ? ++motd1_cnt : ++motd2_cnt;
    }
  (void) closedir(dirp);
  }
return 1;
}


/* return a random number for an motd file - if 0 then return 1 */
int get_motd_num(int motd)
{
int num;
srand(time(0));

if (!random_motds) return 1;
switch(motd) {
  case 1: num=rand()%motd1_cnt+1;  break;
  case 2: num=rand()%motd2_cnt+1;  break;
  default: num=0;  break;
  }
return (!num) ? 1:num;
}



/******************************************************************************
 File functions - reading, writing, counting of lines, etc
 *****************************************************************************/



/* wipes ALL the files belonging to the user with name given */
void clean_files(char *name)
{
char filename[80];

name[0]=toupper(name[0]);
sprintf(filename,"%s/%s.D",USERFILES,name);
unlink(filename);
sprintf(filename,"%s/%s/%s.M",USERFILES,USERMAILS,name);
unlink(filename);
sprintf(filename,"%s/%s/%s.P",USERFILES,USERPROFILES,name);
unlink(filename);
sprintf(filename,"%s/%s/%s.H",USERFILES,USERHISTORYS,name);
unlink(filename);
sprintf(filename,"%s/%s/%s.C",USERFILES,USERCOMMANDS,name);
unlink(filename);
sprintf(filename,"%s/%s/%s.MAC",USERFILES,USERMACROS,name);
unlink(filename);
sprintf(filename,"%s/%s/%s.F",USERFILES,USERFRIENDS,name);
unlink(filename);
sprintf(filename,"%s/%s/%s.R",USERFILES,USERROOMS,name);
unlink(filename);
sprintf(filename,"%s/%s/%s.B",USERFILES,USERROOMS,name);
unlink(filename);
sprintf(filename,"%s/%s/%s.K",USERFILES,USERROOMS,name);
unlink(filename);
}


/* remove a line from the top or bottom of a file.
   where==0 then remove from the top
   where==1 then remove from the bottom 
 */
int remove_top_bottom(char *filename, int where)
{
char line[ARR_SIZE];
FILE *fpi, *fpo;
int i,cnt;

if (!(fpi=fopen(filename,"r"))) return 0;
if (!(fpo=fopen("temp_file","w"))) {
  fclose(fpi);  return 0;
  }
/* remove from top */
if (where==0) {
  /* get the first line */
  fgets(line,ARR_SIZE,fpi);
  /* get rest of file */
  fgets(line,ARR_SIZE,fpi);
  while(!feof(fpi)) {
    line[strlen(line)-1]='\0';
    fprintf(fpo,"%s\n",line);
    fgets(line,ARR_SIZE-1,fpi);
    }
  }
/* remove from bottom of file */
else {
  i=0;
  cnt=count_lines(filename);
  cnt--;
  fgets(line,ARR_SIZE,fpi);
  while(!feof(fpi)) {
    if (i<cnt) {
      line[strlen(line)-1]='\0';
      fprintf(fpo,"%s\n",line);
      }
    i++;
    fgets(line,ARR_SIZE-1,fpi);
    }
  }
fclose(fpi);  fclose(fpo);
rename("temp_file",filename);
if (!(cnt=count_lines(filename))) unlink(filename);
return 1;
}


/* counts how many lines are in a file */
int count_lines(char *filename)
{
int i,c;
FILE *fp;

i=0;
if (!(fp=fopen(filename,"r"))) return i;
c=getc(fp);
while (!feof(fp)) {
  if (c=='\n') i++;
  c=getc(fp);
  }
fclose(fp);
return i;
}



/******************************************************************************
 Write functions - users, rooms, system logs
 *****************************************************************************/



/*** Write a NULL terminated string to a socket ***/
void write_sock(int sock, char *str)
{
  write(sock,str,strlen(str));
}


/*** Send message to user ***/
void write_user(UR_OBJECT user, char *str)
{
int buffpos,sock,i,cnt;
char *start,buff[OUT_BUFF_SIZE];
#ifdef NETLINKS
  char mesg[ARR_SIZE];
#endif

if (user==NULL) return;
#ifdef NETLINKS
  if (user->type==REMOTE_TYPE) {
    if (user->netlink->ver_major<=3 
	&& user->netlink->ver_minor<2) str=colour_com_strip(str);
    if (str[strlen(str)-1]!='\n') 
      sprintf(mesg,"MSG %s\n%s\nEMSG\n",user->name,str);
    else sprintf(mesg,"MSG %s\n%sEMSG\n",user->name,str);
    write_sock(user->netlink->socket,mesg);
    return;
    }
#endif
start=str;
buffpos=0;
sock=user->socket;
/* Process string and write to buffer. We use pointers here instead of arrays 
   since these are supposedly much faster (though in reality I guess it depends
   on the compiler) which is necessary since this routine is used all the 
   time. */
cnt=0;
while(*str) {
  if (*str=='\n') {
    if (buffpos>OUT_BUFF_SIZE-6) {
      write(sock,buff,buffpos);  buffpos=0;
      }
    /* Reset terminal before every newline */
    if (user->colour) {
      memcpy(buff+buffpos,"\033[0m",4);  buffpos+=4;
      }
    *(buff+buffpos)='\n';  *(buff+buffpos+1)='\r';  
    buffpos+=2;  ++str;
    cnt=0;
    }
  else if (user->wrap && cnt==SCREEN_WRAP) {
    *(buff+buffpos)='\n';  *(buff+buffpos+1)='\r';  
    buffpos+=2;
    cnt=0;
    }
  else {  
    /* See if its a ^ before a ~ , if so then we print colour command
       as text */
    if (*str=='^' && *(str+1)=='~') {  ++str;  continue;  }
    if (str!=start && *str=='~' && *(str-1)=='^') {
      *(buff+buffpos)=*str;  goto CONT;
      }
    /* Process colour commands eg ~FR. We have to strip out the commands 
       from the string even if user doesnt have colour switched on hence 
       the user->colour check isnt done just yet */
    if (*str=='~') {
      if (buffpos>OUT_BUFF_SIZE-6) {
	write(sock,buff,buffpos);  buffpos=0;
        }
      ++str;
      for(i=0;i<NUM_COLS;++i) {
	if (!strncmp(str,colour_codes[i].txt_code,2)) {
	  if (user->colour) {
	    memcpy(buff+buffpos,colour_codes[i].esc_code,strlen(colour_codes[i].esc_code));
	    buffpos+=strlen(colour_codes[i].esc_code)-1;  
	    }
	  else buffpos--;
	  ++str;  --cnt;
	  goto CONT;
	  }
        }
      *(buff+buffpos)=*(--str);
      }
    else *(buff+buffpos)=*str;
  CONT:
    ++buffpos;   ++str;   cnt++;
    }
  if (buffpos==OUT_BUFF_SIZE) {
    write(sock,buff,OUT_BUFF_SIZE);  buffpos=0;
    }
  }
if (buffpos) write(sock,buff,buffpos);
/* Reset terminal at end of string */
if (user->colour) write_sock(sock,"\033[0m"); 
}


/*** Write to users of level 'level' and above or below depending on above
     variable; if 1 then above else below.  If user given then ignore that user
     when writing to their level.
     ***/
void write_level(int level, int above, char *str, UR_OBJECT user)
{
UR_OBJECT u;

for(u=user_first;u!=NULL;u=u->next) {
  if ((check_igusers(u,user))!=-1 && user->level<GOD) continue;
  if ((u->ignwiz && (com_num==WIZSHOUT || com_num==WIZEMOTE)) || (u->ignlogons && logon_flag)) 
    continue;
  if (u!=user && !u->login && u->type!=CLONE_TYPE) {
    if ((above && u->level>=level) || (!above && u->level<=level)) {
      if (u->afk) {
	record_afk(u,str);
	continue;
        }
      if (u->editing) {
	record_edit(u,str);
	continue;
        }
      if (!u->ignall && !u->editing) write_user(u,str);
      record_tell(u,str);
      }
    }
  }
}


/*** Subsid function to below but this one is used the most ***/
void write_room(RM_OBJECT rm, char *str)
{
  write_room_except(rm,str,NULL);
}


/*** Write to everyone in room rm except for "user". If rm is NULL write 
     to all rooms. ***/
void write_room_except(RM_OBJECT rm, char *str, UR_OBJECT user)
{
UR_OBJECT u;
char text2[ARR_SIZE];

for(u=user_first;u!=NULL;u=u->next) {
  if (u->login 
      || u->room==NULL 
      || (u->room!=rm && rm!=NULL) 
      || (u->ignall && !force_listen)
      || (u->ignshouts && (com_num==SHOUT || com_num==SEMOTE))
      || (u->ignlogons && logon_flag)
      || (u->igngreets && com_num==GREET)
      || u==user) continue;
  if ((check_igusers(u,user))!=-1 && user->level<ARCH) continue;
  if (u->type==CLONE_TYPE) {
    if (u->clone_hear==CLONE_HEAR_NOTHING || u->owner->ignall) continue;
    /* Ignore anything not in clones room, eg shouts, system messages
       and semotes since the clones owner will hear them anyway. */
    if (rm!=u->room) continue;
    if (u->clone_hear==CLONE_HEAR_SWEARS) {
      if (!contains_swearing(str)) continue;
      }
    sprintf(text2,"~FT[ %s ]:~RS %s",u->room->name,str);
    write_user(u->owner,text2);
    }
  else write_user(u,str); 
  } /* end for */
}


/*** Write to everyone on the users friends list
     if revt=1 then record to the friends tell buffer
     ***/
void write_friends(UR_OBJECT user, char *str, int revt)
{
UR_OBJECT u;

for(u=user_first;u!=NULL;u=u->next) {
  if (u->login 
      || u->room==NULL
      || u->type==CLONE_TYPE
      || (u->ignall && !force_listen)
      || u==user) continue;
  if ((check_igusers(u,user))!=-1 && user->level<GOD) continue;
  if (!(user_is_friend(user,u))) continue;
  write_user(u,str); 
  if (revt) record_tell(u,str);
  } /* end for */
}


/*** Write a string to system log ***/
void write_syslog(char *str, int write_time, int type)
{
FILE *fp;

fp=NULL;
switch(type) {
  case SYSLOG:
    if (!system_logging || !(fp=fopen(MAINSYSLOG,"a"))) return;
    break;
  case REQLOG:
    if (!system_logging || !(fp=fopen(REQSYSLOG,"a"))) return;
    break;
#ifdef NETLINKS
  case NETLOG:
    if (!system_logging || !(fp=fopen(NETSYSLOG,"a"))) return;
    break;
#endif
  }
if (!write_time) fputs(str,fp);
else fprintf(fp,"%02d/%02d %02d:%02d:%02d: %s",tmday,tmonth+1,thour,tmin,tsec,str);
fclose(fp);
}


/* this version of the the last command log - the two procedures below - are
   thanks to Karri (The Bat) Kalpio who makes KTserv */

/* record the last command executed - helps find crashes */
void record_last_command(UR_OBJECT user, int comnum)
{
sprintf(cmd_history[last_cmd_cnt & 15],"[%5d] %02d/%02d, %02d:%02d:%02d  -  %-20s (%s)",
	last_cmd_cnt,tmday,tmonth+1,thour,tmin,tsec,command_table[comnum].name,user->name);
last_cmd_cnt++;
}


/* write the commands to the files */
void dump_commands(int foo) {
FILE *fp;
char filename[32];

sprintf(filename,"%s/%s.%d",LASTCMDLOGS,LAST_CMD,(int)getpid());
if((fp=fopen(filename, "w"))) {
  int i,j;
  for (i=((j=last_cmd_cnt-16)>0?j:0);i<last_cmd_cnt;i++) fprintf(fp, "%s\n", cmd_history[i&15]);
  fclose(fp);
  }
}  


/*** shows the name of a user if they are invis.  Records to tell buffer if rec=1 ***/
void write_monitor(UR_OBJECT user, RM_OBJECT rm, int rec)
{
UR_OBJECT u;

sprintf(text,"~BB~FG[%s]~RS ",user->name);
for(u=user_first;u!=NULL;u=u->next) {
  if (u==user || u->login || u->type==CLONE_TYPE) continue;
  if (u->level<command_table[MONITOR].level || !u->monitor) continue;
  if (u->room==rm || rm==NULL) {
    if (!u->ignall && !u->editing) write_user(u,text);
    if (rec) record_tell(u,text);
    }
  } /* end for */
}


/*** Page a file out to user. Colour commands in files will only work if 
     user!=NULL since if NULL we dont know if his terminal can support colour 
     or not. Return values: 
       0 = cannot find file, 1 = found file, 2 = found and finished ***/
int more(UR_OBJECT user, int sock, char *filename)
{
int i,buffpos,num_chars,lines,retval,len,cnt,pager,fsize,fperc;
char buff[OUT_BUFF_SIZE],text2[83],*str;
struct stat stbuf;
FILE *fp;

/* check if file exists */
if (!(fp=fopen(filename,"r"))) {
  if (user!=NULL) user->filepos=0;  
  return 0;
  }
/* get file size */
if (stat(filename,&stbuf)==-1) fsize=0;
else fsize=stbuf.st_size;
/* jump to reading posn in file */
if (user!=NULL) fseek(fp,user->filepos,0);
if (user==NULL) pager=23;
else {
  if (user->pager<MAX_LINES || user->pager>99) pager=23;
  else pager=user->pager;
  }
text[0]='\0';  
buffpos=0;  
num_chars=0;
retval=1; 
len=0;
cnt=0;

/* If user is remote then only do 1 line at a time */
if (sock==-1) {
  lines=1;  fgets(text2,82,fp);
  }
else {
  lines=0;  fgets(text,sizeof(text)-1,fp);
  }

/* Go through file */
while(!feof(fp) && (lines<pager || user==NULL)) {
#ifdef NETLINKS
  if (sock==-1) {
    lines++;  
    if (user->netlink->ver_major<=3 && user->netlink->ver_minor<2) 
      str=colour_com_strip(text2);
    else str=text2;
    if (str[strlen(str)-1]!='\n') 
      sprintf(text,"MSG %s\n%s\nEMSG\n",user->name,str);
    else sprintf(text,"MSG %s\n%sEMSG\n",user->name,str);
    write_sock(user->netlink->socket,text);
    num_chars+=strlen(str);
    fgets(text2,82,fp);
    continue;
    }
#endif
  str=text;
  /* Process line from file */
  while(*str) {
    if (*str=='\n') {
      if (buffpos>OUT_BUFF_SIZE-6) {
	write(sock,buff,buffpos);  buffpos=0;
        }
      /* Reset terminal before every newline */
      if (user!=NULL && user->colour) {
	memcpy(buff+buffpos,"\033[0m",4);  buffpos+=4;
        }
      *(buff+buffpos)='\n';  *(buff+buffpos+1)='\r';  
      buffpos+=2;  ++str;
      cnt=0;
      }
    else if (user!=NULL && user->wrap && cnt==SCREEN_WRAP) {
      *(buff+buffpos)='\n';  *(buff+buffpos+1)='\r';  
      buffpos+=2;
      cnt=0;
      }
    else {  
      /* Process colour commands in the file. See write_user()
	 function for full comments on this code. */
      if (*str=='^' && *(str+1)=='~') {  ++str;  continue;  }
      if (str!=text && *str=='~' && *(str-1)=='^') {
	*(buff+buffpos)=*str;  goto CONT;
        }
      if (*str=='~') {
	++str;
	/* process if user name variable */
	if (*str=='$') {
	  if (buffpos>OUT_BUFF_SIZE-USER_NAME_LEN) {
	    write(sock,buff,buffpos);  buffpos=0;
	    }
	  if (user!=NULL) {
	    memcpy(buff+buffpos,user->recap,strlen(user->recap));
	    buffpos+=strlen(user->recap)-1;
	    cnt+=strlen(user->recap)-1;
	    }
	  else {
	    buffpos--;  cnt--;
	    }
	  goto CONT;
	  }
	/* process if colour variable */
	if (buffpos>OUT_BUFF_SIZE-6) {
	  write(sock,buff,buffpos);  buffpos=0;
	  }
	/* ++str; */
	for(i=0;i<NUM_COLS;++i) {
	  if (!strncmp(str,colour_codes[i].txt_code,2)) {
	    if (user!=NULL && user->colour) {
	      memcpy(buffpos+buff,colour_codes[i].esc_code,strlen(colour_codes[i].esc_code));
	      buffpos+=strlen(colour_codes[i].esc_code)-1;
	      }
	    else buffpos--;
	    ++str;  --cnt;
	    goto CONT;
	    }
	  }
	*(buff+buffpos)=*(--str);
        }
      else *(buff+buffpos)=*str;
    CONT:
      ++buffpos;   ++str;   ++cnt;
      }
    if (buffpos==OUT_BUFF_SIZE) {
      write(sock,buff,OUT_BUFF_SIZE);  buffpos=0;
      }
    }
  len=strlen(text);
  num_chars+=len;
  lines+=len/80+(len<80);
  fgets(text,sizeof(text)-1,fp);
  }
if (buffpos && sock!=-1) write(sock,buff,buffpos);

/* if user is logging on dont page file */
if (user==NULL) {  fclose(fp);  return 2;  write_sock(sock,"\n"); };
if (feof(fp)) {
  user->filepos=0;  no_prompt=0;  retval=2;
  for (i=0;i<MAX_PAGES;i++) user->pages[i]=0;
  user->pagecnt=0;
  write_user(user,"\n");
  }
else  {
  /* store file position and file name */
  user->filepos+=num_chars;
  user->pages[++user->pagecnt]=user->filepos;
  strcpy(user->page_file,filename);
  /* work out % of file displayed */
  fperc=(int)(user->filepos*100)/fsize;
  /* We use E here instead of Q because when on a remote system and
     in COMMAND mode the Q will be intercepted by the home system and 
     quit the user */
  sprintf(text,"~BB~FG-=[~OL%d%%~RS~BB~FG]=- (~OLR~RS~BB~FG)EDISPLAY, (~OLB~RS~BB~FG)ACK, (~OLE~RS~BB~FG)XIT, <RETURN> TO CONTINUE:~RS ",fperc);
  write_user(user,text);
  no_prompt=1;
  }
fclose(fp);
return retval;
}


/*** Page out a list of users of the level given.  If lev is -1 then page out
     all of the users
     ***/
int more_users(UR_OBJECT user)
{
struct user_dir_struct *entry;
int i,lines,retval;

retval=1;  entry=NULL;
/* page all of the users */
if (first_dir_entry==NULL) return 0;
if (user->user_page_lev==-1) {
  /* get to where user is so far */
  if (!user->user_page_pos) entry=first_dir_entry;
  else for (i=0;i<user->user_page_pos;i++) entry=entry->next;
  lines=0;
  while (entry!=NULL) {
    if (lines>=user->pager) {
      write_user(user,"~BB~FG-=[*]=- PRESS <RETURN>, E TO EXIT:~RS ");
      no_prompt=1;
      user->misc_op=16;
      return retval;
      }
    sprintf(text,"%d) %-*s : %s\n",entry->level,USER_NAME_LEN,entry->name,entry->date);
    write_user(user,text);
    entry=entry->next;
    user->user_page_pos++;
    lines++;
    } /* end while */
  retval=2;
  user->user_page_pos=0;  user->user_page_lev=0;  user->misc_op=0;
  return retval;
  } /* end if for listing all users */

/* list only those users of a specific level */
if (!level_count[user->user_page_lev]) return 0;
if (!user->user_page_pos) entry=first_dir_entry;
else {
  /* get to the position of the user level */
  entry=first_dir_entry;
  i=0;
  while (entry!=NULL) {
    if (entry->level!=user->user_page_lev) {
      entry=entry->next;
      continue;
      }
    if (i==user->user_page_pos) break;
    ++i;
    entry=entry->next;
    }
  }
lines=0;
while (entry!=NULL) {
  if (entry->level!=user->user_page_lev) {
    entry=entry->next;
    continue;
    }
  if (lines>=user->pager) {
    write_user(user,"~BB~FG-=[*]=- PRESS <RETURN>, E TO EXIT:~RS ");
    no_prompt=1;
    return retval;
    }
  /* doesn't matter if didn't boot in verbose mode as entry->date will be null anyway */
  sprintf(text,"%-*s : %s\n",USER_NAME_LEN,entry->name,entry->date);
  write_user(user,text);
  entry=entry->next;
  user->user_page_pos++;
  lines++;
  } /* end while */
write_user(user,"\n");
retval=2;
user->user_page_pos=0;  user->user_page_lev=0;  user->misc_op=0;
return retval;
}


/* adds a string to the user's history list */
void add_history(char *name, int showtime, char *str)
{
FILE *fp;
char filename[80];

name[0]=toupper(name[0]);
/* add to actual history listing */
sprintf(filename,"%s/%s/%s.H",USERFILES,USERHISTORYS,name);
if ((fp=fopen(filename,"a"))) {
  if (!showtime) fprintf(fp,"%s",str);
  else fprintf(fp,"%02d/%02d %02d:%02d: %s",tmday,tmonth+1,thour,tmin,str);
  fclose(fp);
  }
}



/******************************************************************************
 Logon/off fucntions
 *****************************************************************************/



/*** Login function. Lots of nice inline code :) ***/
void login(UR_OBJECT user, char *inpstr)
{
UR_OBJECT u;
int i;
char name[ARR_SIZE],passwd[ARR_SIZE],filename[80],motdname[80];

name[0]='\0';  passwd[0]='\0';
switch(user->login) {
  case LOGIN_NAME:
    sscanf(inpstr,"%s",name);
    if(name[0]<33) {
      write_user(user,"\nGive me a name: ");  return;
      }
    if (!strcmp(name,"quit")) {
      write_user(user,"\n\n*** Abandoning login attempt ***\n\n");
      disconnect_user(user);  return;
      }
    if (!strcmp(name,"who")) {
      who(user,0);  
      write_user(user,"\nGive me a name: ");
      return;
      }
    if (!strcmp(name,"version")) {
      sprintf(text,"\nAmnuts version %s\n\nGive me a name: ",AMNUTSVER);
      write_user(user,text);  return;
      }
    if (!strcmp(name,"Version")) {
      sprintf(text,"\nAmnuts version %s\n\nGive me a name: ",AMNUTSVER);
      write_user(user,text);  return;
      }
    if (strlen(name)<3) {
      write_user(user,"\nName too short.\n\n");  
      attempts(user);  return;
      }
    if (strlen(name)>USER_NAME_LEN) {
      write_user(user,"\nName too long.\n\n");
      attempts(user);  return;
      }
    /* see if only letters in login */
    for (i=0;i<strlen(name);++i) {
      if (!isalpha(name[i])) {
	write_user(user,"\nOnly letters are allowed in a name.\n\n");
	attempts(user);  return;
        }
      }
    if (contains_swearing(name)) {
      write_user(user,"\nYou cannot use a name like that, sorry!\n\n");
      attempts(user);  return;
      }
    strtolower(name);
    name[0]=toupper(name[0]);
    if (user_banned(name)) {
      write_user(user,"\nYou are banned from this talker.\n\n");
      disconnect_user(user);
      sprintf(text,"Attempted login by banned user %s.\n",name);
      write_syslog(text,1,SYSLOG);
      return;
      }
    strcpy(user->name,name);
    /* If user has hung on another login clear that session */
    for(u=user_first;u!=NULL;u=u->next) {
      if (u->login && u!=user && !strcmp(u->name,user->name)) {
	disconnect_user(u);  break;
        }
      }
    if (!load_user_details(user)) {
      /* we presume that port 1 is the wizport */
      if (user->port==port[1]) {
	write_user(user,"\nSorry, new logins cannot be created on this port.\n\n");
	disconnect_user(user);  
	return;
        }
      if (minlogin_level>-1) {
	write_user(user,"\nSorry, new logins cannot be created at this time.\n\n");
	disconnect_user(user);  
	return;
        }
      if (site_banned(user->site,1)) {
	write_user(user,"\nSorry, new accounts from your site have been banned.\n\n");
	sprintf(text,"Attempted login by a new user from banned new users site %s.\n",user->site);
	write_syslog(text,1,SYSLOG);
	disconnect_user(user);
	return;
        }
      write_user(user,"New user...\n");
      }
    else {
      if (user->port==port[1] && user->level<wizport_level) {
	sprintf(text,"\nSorry, only users of level %s and above can log in on this port.\n\n",level_name[wizport_level]);
	write_user(user,text);
	disconnect_user(user);  
	return;
        }
      if (user->level<minlogin_level) {
	write_user(user,"\nSorry, the talker is currently locked out to users of your level.\n\n");
	disconnect_user(user);  
	return;
        }
      }
    write_user(user,"Give me a password: ");
    echo_off(user);
    user->login=LOGIN_PASSWD;
    return;

  case LOGIN_PASSWD:
    sscanf(inpstr,"%s",passwd);
    if (strlen(passwd)<3) {
      write_user(user,"\n\nPassword too short.\n\n");  
      attempts(user);  return;
      }
    if (strlen(passwd)>PASS_LEN) {
      write_user(user,"\n\nPassword too long.\n\n");
      attempts(user);  return;
      }
    /* if new user... */
    if (!user->pass[0]) {
      strcpy(user->pass,(char *)crypt(passwd,crypt_salt));
      write_user(user,"\n");
      sprintf(filename,"%s/%s",MISCFILES,RULESFILE);
      if (more(NULL,user->socket,filename)) {
	write_user(user,"\nBy typing your password in again you are accepting the above rules.\n");
	write_user(user,"If you do not agree with the rules, then disconnect now.\n");
        }
      write_user(user,"\nPlease confirm password: ");
      user->login=LOGIN_CONFIRM;
      }
    else {
      if (!strcmp(user->pass,(char *)crypt(passwd,crypt_salt))) {
	echo_on(user);  logons_old++;
	/* Instead of connecting the user with:  'connect_user(user);  return;'
	   Show the user the MOTD2 so that they can read it.  If you wanted, you
	   could make showing the MOTD2 optional, in which case use an 'if' clause
	   to either do the above or the following...
	   */
	cls(user);
	/* If there is no motd2 files then don't display them */
	if (motd2_cnt) {
	  sprintf(motdname,"%s/motd2/motd%d",MOTDFILES,(get_motd_num(2)));
	  more(user,user->socket,motdname);
	  } 
	write_user(user,"Press return to continue: ");
	user->login=LOGIN_PROMPT;
	return;
        }
      write_user(user,"\n\nIncorrect login.\n\n");
      attempts(user);
      }
    return;

  case LOGIN_CONFIRM:
    sscanf(inpstr,"%s",passwd);
    if (strcmp(user->pass,(char*)crypt(passwd,crypt_salt))) {
      write_user(user,"\n\nPasswords do not match.\n\n");
      attempts(user);
      return;
      }
    echo_on(user);
    strcpy(user->desc,"is a newbie needing a desc.");
    strcpy(user->in_phrase,"enters");	
    strcpy(user->out_phrase,"goes");	
    strcpy(user->date,(long_date(1)));
    strcpy(user->recap,user->name);
    user->last_site[0]='\0';
    user->level=NEW;
    user->unarrest=NEW;
    user->muzzled=0;
    user->command_mode=0;
    user->prompt=prompt_def;
    user->colour=colour_def;
    user->charmode_echo=charecho_def;
    save_user_details(user,1);
    add_user_node(user->name,user->level);
    add_user_date_node(user->name,(long_date(1)));
    add_history(user->name,1,"Was initially created.\n");
    sprintf(text,"New user \"%s\" created.\n",user->name);
    write_syslog(text,1,SYSLOG);
    logons_new++;
    level_count[user->level]++;
    /* Check out above for explaination of this */
    cls(user);
    /* If there is no motd2 files then don't display them */
    if (motd2_cnt) {
      sprintf(motdname,"%s/motd2/motd%d",MOTDFILES,(get_motd_num(2)));
      more(user,user->socket,motdname);
      } 
    write_user(user,"Press return to continue: ");
    user->login=LOGIN_PROMPT;
    return;

  case LOGIN_PROMPT:
    user->login=0;
    write_user(user,"\n\n");
    connect_user(user);
    return;
  } /* end switch */
}
	


/*** Count up attempts made by user to login ***/
void attempts(UR_OBJECT user)
{
user->attempts++;
if (user->attempts==LOGIN_ATTEMPTS) {
  write_user(user,"\nMaximum attempts reached.\n\n");
  disconnect_user(user);
  return;
  }
reset_user(user);
user->login=LOGIN_NAME;
user->pass[0]='\0';
write_user(user,"Give me a name: ");
echo_on(user);
}



/*** Connect the user to the talker proper ***/
void connect_user(UR_OBJECT user)
{
UR_OBJECT u,u2;
RM_OBJECT rm;
char temp[30],rmname[ROOM_NAME_LEN+20],text2[ARR_SIZE];
char *see[]={"INVISIBLE","VISIBLE"};
int cnt,yes,newmail;

/* See if user already connected */
for(u=user_first;u!=NULL;u=u->next) {
  if (user!=u && user->type!=CLONE_TYPE && !strcmp(user->name,u->name)) {
    rm=u->room;
  #ifdef NETLINKS
    /* deal with a login if the user is connected to a remote talker */
    if (u->type==REMOTE_TYPE) {
      write_user(u,"\n~FB~OLYou are pulled back through cyberspace...\n");
      sprintf(text,"REMVD %s\n",u->name);
      write_sock(u->netlink->socket,text);
      sprintf(text,"%s vanishes.\n",u->name);
      destruct_user(u);
      write_room(rm,text);
      reset_access(rm);
      num_of_users--;
      break;
      }
  #endif
    write_user(user,"\n\nYou are already connected - switching to old session...\n");
    sprintf(text,"%s swapped sessions (%s)\n",user->name,user->site);
    write_syslog(text,1,SYSLOG);
    close(u->socket);
    u->socket=user->socket;
    strcpy(u->site,user->site);
    u->site_port=user->site_port;
    destruct_user(user);
    num_of_logins--;
    if (rm==NULL) {
      #ifdef NETLINKS
        sprintf(text,"ACT %s look\n",u->name);
	write_sock(u->netlink->socket,text);
      #endif
      }
    else {
      look(u);  prompt(u);
      }
    /* Reset the sockets on any clones */
    for(u2=user_first;u2!=NULL;u2=u2->next) {
      if (u2->type==CLONE_TYPE && u2->owner==user) {
	u2->socket=u->socket;  u->owner=u;
        }
      }
    return;
    }
  }
alert_friends(user);
logon_flag=1;
if (!user->vis && user->level<command_table[INVIS].level) user->vis=1;
if (user->level==JAILED) {
  user->room=get_room(default_jail);
  if (user->room==NULL) user->room=room_first;
  }
else check_start_room(user);
if (user->room==room_first) rmname[0]='\0';
else if (user->room->access==PERSONAL_LOCKED || user->room->access==PERSONAL_UNLOCKED) sprintf(rmname,"~RS %s~OL",user->room->name);
else sprintf(rmname,"~RS (%s)~OL",user->room->name);
if (user->level==JAILED) {
  sprintf(text,"~OL[Being thrown into jail is:~RS %s %s~RS~OL]\n",user->recap,user->desc);
  write_room_except(NULL,text,user);
  sprintf(text,"~FT%s (Site %s : Site Port : %d : Talker Port %d)\n",user->name,user->site,user->site_port,user->port);
  write_level(ARCH,1,text,user);
  }
else {
  if (user->vis) {
    if (user->level<WIZ) sprintf(text,"~OL[Entering%s is:~RS %s %s~RS~OL]\n",rmname,user->recap,user->desc);
    else sprintf(text,"\07~OL[Entering%s is:~RS %s %s~RS~OL]\n",rmname,user->recap,user->desc);
    write_room(NULL,text);
    sprintf(text,"~FT%s (Site %s : Site Port %d : Talker Port %d)\n",user->name,user->site,user->site_port,user->port);
    write_level(ARCH,1,text,user);
    }
  else {
    if (user->level<ARCH) write_room_except(user->room,invisenter,user);
    sprintf(text,"~OL~FY[ INVIS ]~RS ~OL[Entering%s is:~RS %s %s~RS~OL]\n",rmname,user->recap,user->desc);
    write_level(WIZ,1,text,user);
    sprintf(text,"~OL~FY[ INVIS ]~RS ~FT%s (Site %s : Site Port %d : Talker Port : %d)\n",user->name,user->site,user->site_port,user->port);
    write_level(ARCH,1,text,user); 
    }
  }
logon_flag=0;
user->logons++;
if (user->level==NEW) user->t_expire=time(0)+(NEWBIE_EXPIRES*86400);
else user->t_expire=time(0)+(USER_EXPIRES*86400);

if (user->last_site[0]) {
  sprintf(temp,"%s",ctime(&user->last_login));
  temp[strlen(temp)-1]='\0';
  sprintf(text,"~OLWelcome~RS %s...\n\n~BBYou were last logged in on %s from %s.\n\n",user->recap,temp,user->last_site);
  }
else sprintf(text,"~OLWelcome~RS %s...\n\n",user->recap);
write_user(user,text);
sprintf(text,"~FTYour level is:~RS~OL %s\n",level_name[user->level]);
write_user(user,text);
text2[0]='\0';
if (user->level>=command_table[INVIS].level) {
  sprintf(text,"~FTYou are currently~RS ~OL%s~RS",see[user->vis]);
  if (user->level>=command_table[MONITOR].level) {
    sprintf(text2," ~FTand your monitor is~RS ~OL%s~RS\n",offon[user->monitor]);
    strcat(text,text2);
    }
  else strcat(text,"\n");
  write_user(user,text);
  }
else if (user->level>=command_table[MONITOR].level) {
  sprintf(text,"~FTYour monitor is currently~RS ~OL%s~RS\n",offon[user->monitor]);
  write_user(user,text);
  }
yes=0;  text2[0]='\0';
if (user->ignall) { strcat(text2,"~OL~FREVERYTHING!~RS");  yes=1; }
if (!yes) {
  if (user->igntells) { strcat(text2,"~OLTells~RS   ");  yes=1; }
  if (user->ignshouts) { strcat(text2,"~OLShouts~RS   ");  yes=1; }
  if (user->ignpics) { strcat(text2,"~OLPics~RS   ");  yes=1; }
  if (user->ignlogons) { strcat(text2,"~OLLogons~RS   ");  yes=1; }
  if (user->ignwiz) { strcat(text2,"~OLWiztells~RS   ");  yes=1; }
  if (user->igngreets) { strcat(text2,"~OLGreets~RS   ");  yes=1; }
  if (user->ignbeeps) { strcat(text2,"~OLBeeps~RS   ");  yes=1; }
  }
if (yes) {
  sprintf(text,"~FTIgnoring:~RS %s\n",text2);
  write_user(user,text);
  }
write_user(user,"\n");

user->last_login=time(0); /* set to now */
look(user);
/* show how much mail the user has */
newmail=mail_sizes(user->name,1);
if (newmail) {
  sprintf(text,"\007~FT~OL*** YOU HAVE ~RS~OL%d~FT UNREAD MAIL MESSAGE%s ***\n",newmail,newmail==1?"":"S");
  write_user(user,text);
  }
else if ((cnt=mail_sizes(user->name,0))) {
  sprintf(text,"~FT*** You have ~RS~OL%d~RS~FT message%s in your mail box ***\n",cnt,PLTEXT_S(cnt));
  write_user(user,text);
  }
/* should they get the autopromote message? */
if (user->accreq!=1 && auto_promote) {
  write_user(user,"\n\007~OL~FY****************************************************************************\n");
  write_user(user,"~OL~FY*               ~FRTO BE AUTO-PROMOTED PLEASE READ CAREFULLY~FY                  *\n");
  write_user(user,"~OL~FY* You must set your description (.desc), set your gender (.set gender) and *\n");
  write_user(user,"~OL~FY*   use the .accreq command - once you do all these you will be promoted   *\n");
  write_user(user,"~OL~FY****************************************************************************\n\n");
  }

prompt(user);
record_last_login(user->name);
sprintf(text,"%s logged in on port %d from %s:%d.\n",user->name,user->port,user->site,user->site_port);
write_syslog(text,1,SYSLOG);
num_of_users++;
num_of_logins--;
}


/*** Disconnect user from talker ***/
void disconnect_user(UR_OBJECT user)
{
RM_OBJECT rm;
#ifdef NETLINKS
  NL_OBJECT nl;
#endif
long int onfor,hours,mins;

rm=user->room;
if (user->login) {
  close(user->socket);  
  destruct_user(user);
  num_of_logins--;  
  return;
  }
if (user->type!=REMOTE_TYPE) {
  onfor=(int)(time(0)-user->last_login);
  hours=(onfor%86400)/3600;
  mins=(onfor%3600)/60;
  save_user_details(user,1);  
  sprintf(text,"%s logged out.\n",user->name);
  write_syslog(text,1,SYSLOG);
  write_user(user,"\n~OL~FBYou are removed from this reality...\n\n");
  sprintf(text,"You were logged on from site %s\n",user->site);
  write_user(user,text);
  sprintf(text,"On %s, %d%s %s, for a total of %d hour%s and %d minute%s.\n\n",
	  day[twday],tmday,ordinal_text(tmday),month[tmonth],(int)hours,PLTEXT_S(hours),(int)mins,PLTEXT_S(mins));
  write_user(user,text);
  close(user->socket);
  logon_flag=1;
  if (user->vis) {
    sprintf(text,"~OL[Leaving is:~RS %s %s~RS~OL]\n",user->recap,user->desc);
    write_room(NULL,text);
    }
  else {
    sprintf(text,"~OL~FY[ INVIS ]~RS ~OL[Leaving is:~RS %s %s~RS~OL]\n",user->recap,user->desc);
    write_level(WIZ,1,text,NULL);
    }
  logon_flag=0;
#ifdef NETLINKS
  if (user->room==NULL) {
    sprintf(text,"REL %s\n",user->name);
    write_sock(user->netlink->socket,text);
    for(nl=nl_first;nl!=NULL;nl=nl->next) 
      if (nl->mesg_user==user) {  
	nl->mesg_user=(UR_OBJECT)-1;  break;  
        }
    }
#endif
  }
#ifdef NETLINKS
else {
  write_user(user,"\n~FR~OLYou are pulled back in disgrace to your own domain...\n");
  sprintf(text,"REMVD %s\n",user->name);
  write_sock(user->netlink->socket,text);
  sprintf(text,"~FR~OL%s is banished from here!\n",user->name);
  write_room_except(rm,text,user);
  sprintf(text,"NETLINK: Remote user %s removed.\n",user->name);
  write_syslog(text,1,NETLOG);
  }
#endif
if (user->malloc_start!=NULL) free(user->malloc_start);
num_of_users--;
record_last_logout(user->name);
/* Destroy any clones */
destroy_user_clones(user);
destruct_user(user);
reset_access(rm);
}



/******************************************************************************
 Misc and line editor functions
 *****************************************************************************/



/*** Stuff that is neither speech nor a command is dealt with here ***/
int misc_ops(UR_OBJECT user, char *inpstr)
{
char filename[80];
int i=0;

switch(user->misc_op) {
  case 1: 
    if (toupper(inpstr[0])=='Y') {
      if (rs_countdown && !rs_which) {
	if (rs_countdown>60) 
	  sprintf(text,"\n\07~OLSYSTEM: ~FR~LISHUTDOWN INITIATED, shutdown in %d minutes, %d seconds!\n\n",rs_countdown/60,rs_countdown%60);
	else sprintf(text,"\n\07~OLSYSTEM: ~FR~LISHUTDOWN INITIATED, shutdown in %d seconds!\n\n",rs_countdown);
	write_room(NULL,text);
	sprintf(text,"%s initiated a %d seconds SHUTDOWN countdown.\n",user->name,rs_countdown);
	write_syslog(text,1,SYSLOG);
	rs_user=user;
	rs_announce=time(0);
	user->misc_op=0;  
	prompt(user);
	return 1;
        }
      talker_shutdown(user,NULL,0); 
      }
    /* This will reset any reboot countdown that was started, oh well */
    rs_countdown=0;
    rs_announce=0;
    rs_which=-1;
    rs_user=NULL;
    user->misc_op=0;  
    prompt(user);
    return 1;
    
  case 2:
    if (toupper(inpstr[0])=='E') {
      user->misc_op=0;  user->filepos=0;  user->page_file[0]='\0';
      for (i=0;i<MAX_PAGES;i++) user->pages[i]=0;
      user->pagecnt=0;
      prompt(user);
      return 1;
      }
    else if (toupper(inpstr[0])=='R') {
      if (!user->pagecnt) user->pagecnt=0;
      else user->pagecnt--;
      user->filepos=user->pages[user->pagecnt];
      }
    else if (toupper(inpstr[0])=='B') {
      if (user->pagecnt<2) user->pagecnt=0;
      else user->pagecnt=user->pagecnt-2;
      user->filepos=user->pages[user->pagecnt];
      }
    if (more(user,user->socket,user->page_file)!=1) {
      user->misc_op=0;  user->filepos=0;  user->page_file[0]='\0';
      for (i=0;i<MAX_PAGES;i++) user->pages[i]=0;
      user->pagecnt=0;
      prompt(user);
      }
    return 1;

  case 3: /* writing on board */
  case 4: /* Writing mail */
  case 5: /* doing profile */
    editor(user,inpstr);
    return 1;

  case 6:
    if (toupper(inpstr[0])=='Y') delete_user(user,1); 
    else {  user->misc_op=0;  prompt(user);  }
    return 1;
    
  case 7:
    if (toupper(inpstr[0])=='Y') {
      if (rs_countdown && rs_which==1) {
	if (rs_countdown>60) 
	  sprintf(text,"\n\07~OLSYSTEM: ~FY~LIREBOOT INITIATED, rebooting in %d minutes, %d seconds!\n\n",rs_countdown/60,rs_countdown%60);
	else sprintf(text,"\n\07~OLSYSTEM: ~FY~LIREBOOT INITIATED, rebooting in %d seconds!\n\n",rs_countdown);
	write_room(NULL,text);
	sprintf(text,"%s initiated a %d seconds REBOOT countdown.\n",user->name,rs_countdown);
	write_syslog(text,1,SYSLOG);
	rs_user=user;
	rs_announce=time(0);
	user->misc_op=0;  
	prompt(user);
	return 1;
        }
      talker_shutdown(user,NULL,1); 
      }
    if (rs_which==1 && rs_countdown && rs_user==NULL) {
      rs_countdown=0;
      rs_announce=0;
      rs_which=-1;
      }
    user->misc_op=0;  
    prompt(user);
    return 1;

  case 8: /* Doing suggestion */
  case 9: /* Level specific mail */
    editor(user,inpstr);
    return 1;

  case 10:
    if (toupper(inpstr[0])=='E') {
      user->misc_op=0;  user->wrap_room=NULL;  prompt(user);
      }
    else rooms(user,0,1);
    return 1;
    
  case 11:
    if (toupper(inpstr[0])=='E') {
      user->misc_op=0;  user->wrap_room=NULL;  prompt(user);
      }
    else rooms(user,1,1);
    return 1;
    
  case 12:
    if (!inpstr[0]) {
      write_user(user,"Abandoning your samesite look-up.\n");
      user->misc_op=0;  user->samesite_all_store=0;  user->samesite_check_store[0]='\0';
      prompt(user);
      }
    else {
      user->misc_op=0;
      word[0][0]=toupper(word[0][0]);
      strcpy(user->samesite_check_store,word[0]);
      samesite(user,1);
      }
    return 1;
    
  case 13:
    if (!inpstr[0]) {
      write_user(user,"Abandoning your samesite look-up.\n");
      user->misc_op=0;  user->samesite_all_store=0;  user->samesite_check_store[0]='\0';
      prompt(user);
      }
    else {
      user->misc_op=0;
      strcpy(user->samesite_check_store,word[0]);
      samesite(user,2);
      }
    return 1;
    
  case 14:
    if (toupper(inpstr[0])=='E') {
      user->misc_op=0;  user->hwrap_lev=0;  user->hwrap_id=0;  user->hwrap_same=0;
      prompt(user);
      }
    else help_commands_level(user,1);
    return 1;
    
  case 15:
    if (toupper(inpstr[0])=='E') {
      user->misc_op=0;  user->hwrap_lev=0;  user->hwrap_func=0;  user->hwrap_id=0;  user->hwrap_same=0;
      prompt(user);
      }
    else help_commands_function(user,1);
    return 1;

  case 16:
    if (toupper(inpstr[0])=='E' || more_users(user)!=1) {
      user->user_page_pos=0;  user->user_page_lev=0;  user->misc_op=0;
      prompt(user); 
      }
    return 1;

  case 17:
    if (toupper(inpstr[0])=='Y') {
      recount_users(user,1);
      }
    else { user->misc_op=0; prompt(user); }
    return 1;

  case 18:
    if (toupper(inpstr[0])=='Y') {
      sprintf(filename,"%s/%s/%s.M",USERFILES,USERMAILS,user->name);
      unlink(filename);
      write_user(user,"\n~OL~FRAll mail messages deleted.\n\n");
      }
    else write_user(user,"\nNo mail messages were deleted.\n\n");
    user->misc_op=0;
    return 1;

  case 19: /* decorating room */
    editor(user,inpstr);
    return 1;
  } /* end switch */
return 0;
}


/*** The editor used for writing profiles, mail and messages on the boards ***/
void editor(UR_OBJECT user, char *inpstr)
{
int cnt,line;
char *edprompt="\n~FG(~OLS~RS~FG)ave~RS, ~FT(~OLV~RS~FT)iew~RS, ~FY(~OLR~RS~FY)edo~RS or ~FR(~OLA~RS~FR)bort~RS: ";
char *ptr,*name;

if (user->edit_op) {
  switch(toupper(*inpstr)) {
    case 'S':
      /* if (*user->malloc_end--!='\n') *user->malloc_end--='\n'; */
      if (user->vis) name=user->recap;  else  name=invisname;
      if (!user->vis) write_monitor(user,user->room,0);
      sprintf(text,"%s finishes composing some text.\n",name);
      write_room_except(user->room,text,user);
      switch(user->misc_op) {
        case 3:  write_board(user,NULL,1);  break;
        case 4:  smail(user,NULL,1);  break;
        case 5:  enter_profile(user,1);  break;
        case 8:  suggestions(user,1);  break;
        case 9:  level_mail(user,inpstr,1);  break;
        case 19: personal_room_decorate(user,1);  break;
        }
      editor_done(user);
      return;

    case 'R':
      user->edit_op=0;
      user->edit_line=1;
      user->charcnt=0;
      user->malloc_end=user->malloc_start;
      *user->malloc_start='\0';
      write_user(user,"\nRedo message...\n\n");
      sprintf(text,"[---------------- Please try to keep between these two markers ----------------]\n\n~FG%d>~RS",user->edit_line);
      write_user(user,text);
      return;
      
    case 'A':
      write_user(user,"\nMessage aborted.\n");
      if (user->vis) name=user->recap;  else  name=invisname;
      if (!user->vis) write_monitor(user,user->room,0);
      sprintf(text,"%s gives up composing some text.\n",name);
      write_room_except(user->room,text,user);
      editor_done(user);  
      return;

    case 'V':
      write_user(user,"\nYou have composed the following text...\n\n");
      write_user(user,user->malloc_start);
      write_user(user,edprompt);
      return;
      
    default: 
      write_user(user,edprompt);
      return;
    } /* end switch */
  }
/* Allocate memory if user has just started editing */
if (user->malloc_start==NULL) {
  if ((user->malloc_start=(char *)malloc(MAX_LINES*81))==NULL) {
    sprintf(text,"%s: failed to allocate buffer memory.\n",syserror);
    write_user(user,text);
    write_syslog("ERROR: Failed to allocate memory in editor().\n",0,SYSLOG);
    user->misc_op=0;
    prompt(user);
    return;
    }
  clear_edit(user);
  user->ignall_store=user->ignall;
  user->ignall=1; /* Dont want chat mucking up the edit screen */
  user->edit_line=1;
  user->charcnt=0;
  user->editing=1;
  user->malloc_end=user->malloc_start;
  *user->malloc_start='\0';
  sprintf(text,"~FTMaximum of %d lines, end with a '.' on a line by itself.\n\n",MAX_LINES);
  write_user(user,text);
  write_user(user,"[---------------- Please try to keep between these two markers ----------------]\n\n~FG1>~RS");
  if (user->vis) name=user->recap;  else  name=invisname;
  if (!user->vis) write_monitor(user,user->room,0);
  sprintf(text,"%s starts composing some text...\n",name);
  write_room_except(user->room,text,user);
  return;
  }
/* Check for empty line */
if (!word_count) {
  if (!user->charcnt) {
    sprintf(text,"~FG%d>~RS",user->edit_line);
    write_user(user,text);
    return;
    }
  *user->malloc_end++='\n';
  if (user->edit_line==MAX_LINES) goto END;
  sprintf(text,"~FG%d>~RS",++user->edit_line);
  write_user(user,text);
  user->charcnt=0;
  return;
  }
/* If nothing carried over and a dot is entered then end */
if (!user->charcnt && !strcmp(inpstr,".")) goto END;
line=user->edit_line;
cnt=user->charcnt;
/* loop through input and store in allocated memory */
while(*inpstr) {
  *user->malloc_end++=*inpstr++;
  if (++cnt==80) {  user->edit_line++;  cnt=0;  }
  if (user->edit_line>MAX_LINES 
      || user->malloc_end - user->malloc_start>=MAX_LINES*81) goto END;
  }
if (line!=user->edit_line) {
  ptr=(char *)(user->malloc_end-cnt);
  *user->malloc_end='\0';
  sprintf(text,"~FG%d>~RS%s",user->edit_line,ptr);
  write_user(user,text);
  user->charcnt=cnt;
  return;
  }
else {
  *user->malloc_end++='\n';
  user->charcnt=0;
  }
if (user->edit_line!=MAX_LINES) {
  sprintf(text,"~FG%d>~RS",++user->edit_line);
  write_user(user,text);
  return;
  }

/* User has finished his message , prompt for what to do now */
END:
*user->malloc_end='\0';
if (*user->malloc_start) {
  write_user(user,edprompt);
  user->edit_op=1;  return;
  }
write_user(user,"\nNo text.\n");
if (user->vis) name=user->recap;  else  name=invisname;
if (!user->vis) write_monitor(user,user->room,0);
sprintf(text,"%s gives up composing some text.\n",name);
write_room_except(user->room,text,user);
editor_done(user);
}


/*** Reset some values at the end of editing ***/
void editor_done(UR_OBJECT user)
{
user->misc_op=0;
user->edit_op=0;
user->edit_line=0;
free(user->malloc_start);
user->malloc_start=NULL;
user->malloc_end=NULL;
user->ignall=user->ignall_store;
user->editing=0;
if (user->editbuff[0][0]) 
  write_user(user,"\nYou have some tells in your edit review buffer.  Use ~FTrevedit~RS to view them.\n\n");
prompt(user);
}



/******************************************************************************
 User command functions and their subsids
 *****************************************************************************/



/*** Deal with user input ***/
int exec_com(UR_OBJECT user, char *inpstr)
{
int i,len;
char filename[80],*comword=NULL;
struct command_struct *cmd;

com_num=-1;
if (word[0][0]=='.') comword=(word[0]+1);
else comword=word[0];
if (!comword[0]) {
  write_user(user,"Unknown command.\n");  return 0;
  }

/* get com_num */
if (!strcmp(word[0],">")) strcpy(word[0],"tell");
if (!strcmp(word[0],";")) strcpy(word[0],"emote");
if (!strcmp(word[0],":")) strcpy(word[0],"emote");
if (!strcmp(word[0],"<")) strcpy(word[0],"pemote");
if (!strcmp(word[0],"/")) strcpy(word[0],"pemote");
if (!strcmp(word[0],"&")) strcpy(word[0],"semote");
if (!strcmp(word[0],"!")) strcpy(word[0],"semote");
if (!strcmp(word[0],"[")) strcpy(word[0],"shout");
if (!strcmp(word[0],"@")) strcpy(word[0],"who");
if (!strcmp(word[0],"'")) strcpy(word[0],"show");
if (!strcmp(word[0],"*")) strcpy(word[0],"cbuff");
if (!strcmp(word[0],"+")) strcpy(word[0],"echo");
if (!strcmp(word[0],"-")) strcpy(word[0],"sayto");
if (!strcmp(word[0],",")) {
  if (!user->call[0]) {
    write_user(user,"Quick call not set.\n");
    return 0;
    }
  strcpy(word[0],"tell");
  goto QCSKIP;
  }

if (inpstr[0]=='>' && !isspace(inpstr[1]))    { strcpy(word[1],word[0]+1); strcpy(word[0],"tell"); }
else if (inpstr[0]==';' && !isspace(inpstr[1]))    { strcpy(word[0],"emote"); inpstr++; }
else if (inpstr[0]==':' && !isspace(inpstr[1]))    { strcpy(word[0],"emote"); inpstr++; }
else if (inpstr[0]=='<' && !isspace(inpstr[1]))    { strcpy(word[1],word[0]+1); strcpy(word[0],"pemote"); }
else if (inpstr[0]=='/' && !isspace(inpstr[1]))    { strcpy(word[1],word[0]+1); strcpy(word[0],"pemote"); }
else if (inpstr[0]=='&' && !isspace(inpstr[1]))    { strcpy(word[0],"semote"); inpstr++; }
else if (inpstr[0]=='!' && !isspace(inpstr[1]))    { strcpy(word[0],"semote"); inpstr++; }
else if (inpstr[0]=='[' && !isspace(inpstr[1]))    { strcpy(word[0],"shout"); inpstr++; }
else if (inpstr[0]=='\''  && !isspace(inpstr[1]))  { strcpy(word[0],"show"); inpstr++; }
else if (inpstr[0]=='+' && !isspace(inpstr[1]))    { strcpy(word[0],"echo"); inpstr++; }
else if (inpstr[0]=='-' && !isspace(inpstr[1]))    { strcpy(word[1],word[0]+1); strcpy(word[0],"sayto"); }
else if (inpstr[0]==',' && !isspace(inpstr[1])) {
  if (!user->call[0]) {
    write_user(user,"Quick call not set.\n");
    return 0;
    }
  strcpy(word[1],word[0]+1);
  strcpy(word[0],"tell");
  }
else inpstr=remove_first(inpstr);

QCSKIP:
i=0;
len=strlen(comword);
while(command_table[i].name[0]!='*') {
  if (!strncmp(command_table[i].name,comword,len)) {  com_num=i;  break;  }
  ++i;
  }
if (user->room!=NULL && com_num==-1) {
  write_user(user,"Unknown command.\n");  return 0;
  }
record_last_command(user,com_num);

/* You may wonder why I'm using com_num above and then scrolling through the command
   linked list again.  This is because many people like to put their commands in a
   certain order, even though they want them viewed alphabetically.  So that is they
   type .h they will get help rather than hangman.  Using the commands as they were
   originally entered (command[]) allows you to do this.  But to get the number of
   times the command has been used we still need to ++ that commands node, hence
   scrolling through the linked list.
   Also have to check the level using the command list nodes because the level of
   the commands can now be altered, therefore reading the hard-coded levels from
   com_level[] is wrong
   Then again, you might be wondering ;)
   */

for (cmd=first_command;cmd!=NULL;cmd=cmd->next) {
  if (cmd->id==com_num) {
    if (user->room!=NULL && (has_gcom(user,cmd->id))) {
      ++cmd->count;
      break;
      }
    if (user->room!=NULL && (has_xcom(user,cmd->id))) {
      write_user(user,"You cannot currently use that command.\n");  return 0;
      }
    if (user->room!=NULL && (cmd->min_lev > user->level)) {
      write_user(user,"Unknown command.\n");  return 0;
      }
    ++cmd->count;
    break;
    }
  } /* end for */


#ifdef NETLINKS
/* See if user has gone across a netlink and if so then intercept certain 
   commands to be run on home site */
if (user->room==NULL) {
  switch(com_num) {
    case HOME: 
    case QUIT:
    case MODE:
    case PROMPT: 
    case COLOUR:
    case SUICIDE: 
    case CHARECHO:
      write_user(user,"~FY~OL*** Home execution ***\n");
      break;
    default:
      sprintf(text,"ACT %s %s %s\n",user->name,word[0],inpstr);
      write_sock(user->netlink->socket,text);
      no_prompt=1;
      return 1;
    } /* end switch */
  } /* end if */
/* Dont want certain commands executed by remote users */
if (user->type==REMOTE_TYPE) {
  switch(com_num) {
    case PASSWD  :
    case ENTPRO  :
    case ACCREQ  :
    case CONN    :
    case DISCONN :
    case SHUTDOWN:
    case REBOOT  :
    case SETAUTOPROMO:
    case DELETE  :
    case SET     :
    case PURGE   :
    case EXPIRE  :
    case LOGGING :
      write_user(user,"Sorry, remote users cannot use that command.\n");
      return 0;
      break; /* shouldn't need this */
    default: break;
    }
  }
#endif

/* Main switch */
switch(com_num) {
  case QUIT: disconnect_user(user);  break;
  case LOOK: look(user);  break;
  case MODE: toggle_mode(user);  break;
  case SAY : 
    if (word_count<2) write_user(user,"Say what?\n");
    else say(user,inpstr);
    break;
  case SHOUT : shout(user,inpstr);  break;
  case TELL  : tell(user,inpstr);   break;
  case EMOTE : emote(user,inpstr);  break;
  case SEMOTE: semote(user,inpstr); break;
  case PEMOTE: pemote(user,inpstr); break;
  case ECHO  : echo(user,inpstr);   break;
  case GO    : go(user);  break;
  case IGNALL: toggle_ignall(user);  break;
  case PROMPT: toggle_prompt(user);  break;
  case DESC  : set_desc(user,inpstr);  break;
  case INPHRASE : 
  case OUTPHRASE: 
    set_iophrase(user,inpstr);  break; 
  case PUBCOM :
  case PRIVCOM: set_room_access(user);  break;
  case LETMEIN: letmein(user);  break;
  case INVITE : invite(user);   break;
  case TOPIC  : set_topic(user,inpstr);  break;
  case MOVE   : move(user);  break;
  case BCAST  : bcast(user,inpstr,0);  break;
  case WHO    : who(user,0);  break;
  case PEOPLE : who(user,2);  break;
  case HELP   : help(user);  break;
  case SHUTDOWN: shutdown_com(user);  break;
  case NEWS:
    sprintf(filename,"%s/%s",MISCFILES,NEWSFILE);
    switch(more(user,user->socket,filename)) {
      case 0: write_user(user,"There is no news.\n");  break;
      case 1: user->misc_op=2;
      }
    break;
  case READ  : read_board(user);  break;
  case WRITE : write_board(user,inpstr,0);  break;
  case WIPE  : wipe_board(user);  break;
  case SEARCH: search_boards(user);  break;
  case REVIEW: review(user);  break;
  #ifdef NETLINKS
    case HOME  : home(user);  break;
  #endif
  case STATUS: status(user);  break;
  case VER: show_version(user);  break;
  case RMAIL   : rmail(user);  break;
  case SMAIL   : smail(user,inpstr,0);  break;
  case DMAIL   : dmail(user);  break;
  case FROM    : mail_from(user);  break;
  case ENTPRO  : enter_profile(user,0);  break;
  case EXAMINE : examine(user);  break;
  case RMST    : rooms(user,1,0);  break;
  #ifdef NETLINKS
    case RMSN    : rooms(user,0,0);  break;
    case NETSTAT : netstat(user);  break;
    case NETDATA : netdata(user);  break;
    case CONN    : connect_netlink(user);  break;
    case DISCONN : disconnect_netlink(user);  break;
  #endif
  case PASSWD  : change_pass(user);  break;
  case KILL    : kill_user(user);  break;
  case PROMOTE : promote(user);  break;
  case DEMOTE  : demote(user);  break;
  case LISTBANS: listbans(user);  break;
  case BAN     : ban(user);  break;
  case UNBAN   : unban(user);  break;
  case VIS     : visibility(user,1);  break;
  case INVIS   : visibility(user,0);  break;
  case SITE    : site(user);  break;
  case WAKE    : wake(user);  break;
  case WIZSHOUT: wizshout(user,inpstr);  break;
  case MUZZLE  : muzzle(user);  break;
  case UNMUZZLE: unmuzzle(user);  break;
  case MAP:
    sprintf(filename,"%s/%s",DATAFILES,MAPFILE);
    switch(more(user,user->socket,filename)) {
      case 0: write_user(user,"You don't need no map - where you are is where it's at!\n");  break;
      case 1: user->misc_op=2;
      }
    break;
  case LOGGING  : logging(user); break;
  case MINLOGIN : minlogin(user);  break;
  case SYSTEM   : system_details(user);  break;
  case CHARECHO : toggle_charecho(user);  break;
  case CLEARLINE: clearline(user);  break;
  case FIX      : change_room_fix(user,1);  break;
  case UNFIX    : change_room_fix(user,0);  break;
  case VIEWLOG  : viewlog(user);  break;
  case ACCREQ   : account_request(user,inpstr);  break;
  case REVCLR   : revclr(user);  break;
  case CREATE   : create_clone(user);  break;
  case DESTROY  : destroy_clone(user);  break;
  case MYCLONES : myclones(user);  break;
  case ALLCLONES: allclones(user);  break;
  case SWITCH: clone_switch(user);  break;
  case CSAY  : clone_say(user,inpstr);  break;
  case CHEAR : clone_hear(user);  break;
  #ifdef NETLINKS
    case RSTAT : remote_stat(user);  break;
  #endif
  case SWBAN : toggle_swearban(user);  break;
  case AFK   : afk(user,inpstr);  break;
  case CLS   : cls(user);  break;
  case COLOUR  : display_colour(user);  break;
  case IGNSHOUTS: set_ignore(user);  break;
  case IGNTELLS : set_ignore(user);  break;
  case SUICIDE : suicide(user);  break;
  case DELETE  : delete_user(user,0);  break;
  case REBOOT  : reboot_com(user);  break;
  case RECOUNT : check_messages(user,2);  break;
  case REVTELL : revtell(user);  break;
  case PURGE: purge_users(user);  break;
  case HISTORY: user_history(user);  break;
  case EXPIRE: user_expires(user);  break;
  case BBCAST: bcast(user,inpstr,1);  break;
  case SHOW: show(user,inpstr);  break;
  case RANKS: show_ranks(user);  break;
  case WIZLIST: wiz_list(user);  break;
  case TIME: get_time(user);  break;
  case CTOPIC: clear_topic(user);  break;
  case COPYTO: copies_to(user);  break;
  case NOCOPIES: copies_to(user);  break;
  case SET: set_attributes(user,inpstr);  break;
  case MUTTER: mutter(user,inpstr);  break;
  case MKVIS: make_vis(user);  break;
  case MKINVIS: make_invis(user);  break;
  case SOS: plead(user,inpstr);  break;
  case PTELL: picture_tell(user);  break;
  case PREVIEW: preview(user);  break;
  case PICTURE: picture_all(user);  break;
  case GREET: greet(user,inpstr);  break;
  case THINK: think_it(user,inpstr);  break;
  case SING: sing_it(user,inpstr);  break;
  case WIZEMOTE: wizemote(user,inpstr);  break;
  case SUG: suggestions(user,0);  break;
  case RSUG: suggestions(user,0);  break;
  case DSUG: delete_suggestions(user);  break;
  case LAST: show_last_login(user);  break;
  case MACROS: macros(user);  break;
  case RULES :
    sprintf(filename,"%s/%s",MISCFILES,RULESFILE);
    switch(more(user,user->socket,filename)) {
      case 0: write_user(user,"\nThere are currrently no rules...\n");  break;
      case 1: user->misc_op=2;
      }
    break;
  case UNINVITE: uninvite(user);  break; 
  case LMAIL: level_mail(user,inpstr,0);  break;
  case ARREST: arrest(user);  break;
  case UNARREST: unarrest(user);  break;
  case VERIFY: verify_email(user);  break;
  case ADDHISTORY: manual_history(user,inpstr);  break;
  case FORWARDING: 
    switch((int)forwarding) {
      case 0: write_user(user,"You have turned ~FGon~RS smail auto-forwarding.\n");
	forwarding=1;
	sprintf(text,"%s turned ON mail forwarding.\n",user->name);
	write_syslog(text,1,SYSLOG);
	break;
      case 1: write_user(user,"You have turned ~FRoff~RS smail auto-forwarding.\n");
	forwarding=0;
	sprintf(text,"%s turned OFF mail forwarding.\n",user->name);
	write_syslog(text,1,SYSLOG);
	break;
      }
    break;
  case REVSHOUT: revshout(user);  break;
  case CSHOUT: clear_shouts();
    write_user(user,"Shouts buffer has now been cleared.\n");
    break;
  case CTELLS: clear_tells(user); 
    write_user(user,"Your tells have now been cleared.\n");
    break;
  case MONITOR :
    switch(user->monitor) {
      case 0: user->monitor=1;
	write_user(user,"You will now monitor certain things.\n");
	break;
      case 1: user->monitor=0;
	write_user(user,"You will no longer monitor certain things.\n");
      }
    break;
  case QCALL: quick_call(user);  break;
  case UNQCALL: user->call[0]='\0';
    write_user(user,"You no longer have your quick call set.\n");
    break;
  case IGNUSER : set_igusers(user); break;
  case IGNPICS  : set_ignore(user);  break;
  case IGNWIZ   : set_ignore(user);  break;
  case IGNLOGONS: set_ignore(user);  break;
  case IGNGREETS: set_ignore(user);  break;
  case IGNBEEPS : set_ignore(user);  break;
  case IGNLIST: show_ignlist(user);  break;
  case ACCOUNT: create_account(user); break;
  case SAMESITE: samesite(user,0);  break;
  case BFROM: board_from(user);  break;
  case SAVEALL: force_save(user);  break;
  case JOIN: join(user);  break;
  case SHACKLE: shackle(user);  break;
  case UNSHACKLE: unshackle(user);  break;
  case REVAFK: revafk(user);   break;
  case CAFK: clear_afk(user);  
    write_user(user,"Your AFK review buffer has now been cleared.\n");
    break;
  case REVEDIT: revedit(user);   break;
  case CEDIT: clear_edit(user);  
    write_user(user,"Your EDIT review buffer has now been cleared.\n");
    break;
  case CEMOTE: clone_emote(user,inpstr);  break;
  case LISTEN: user_listen(user);  break;
  case HANGMAN: play_hangman(user);  break;
  case GUESS: guess_hangman(user);  break;
  case RETIRE : retire_user(user);  break;
  case UNRETIRE : unretire_user(user);  break;
  case MEMCOUNT: show_memory(user);  break;
  case CMDCOUNT: show_command_counts(user);  break;
  case RCOUNTU: recount_users(user,0);  break;
  case RECAPS:
    switch(allow_recaps) {
      case 0: write_user(user,"You ~FGallow~RS names to be recapped.\n");
	      allow_recaps=1;
	      sprintf(text,"%s turned ON recapping of names.\n",user->name);
	      write_syslog(text,1,SYSLOG);
	      break;
      case 1: write_user(user,"You ~FRdisallow~RS names to be recapped.\n");
	      allow_recaps=0;
	      sprintf(text,"%s turned OFF recapping of names.\n",user->name);
	      write_syslog(text,1,SYSLOG);
	      break;
      }
    break;
  case SETCMDLEV: set_command_level(user);  break;
  case GREPUSER: grep_users(user);  break;
  case SHOOT: shoot_user(user);  break;
  case RELOAD: reload_gun(user);  break;
  case XCOM: user_xcom(user);  break;
  case GCOM: user_gcom(user);  break;
  case SFROM: suggestions_from(user);  break;
  case RLOADRM: reload_room_description(user);  break;
  case SETAUTOPROMO: 
    switch(auto_promote) {
      case 0: write_user(user,"You have turned ~FGon~RS auto-promotes for new users.\n");
	auto_promote=1;
	sprintf(text,"%s turned ON auto-promotes.\n",user->name);
	write_syslog(text,1,SYSLOG);
	break;
      case 1: write_user(user,"You have turned ~FRoff~RS auto-promotes for new users.\n");
	auto_promote=0;
	sprintf(text,"%s turned OFF auto-promotes.\n",user->name);
	write_syslog(text,1,SYSLOG);
	break;
      }
    break;
  case SAYTO: say_to(user,inpstr);  break;
  case FRIENDS: friends(user);  break;
  case FSAY: friend_say(user,inpstr);  break;
  case FEMOTE: friend_emote(user,inpstr);  break;
  case BRING: bring(user);  break;
  case FORCE: force(user,inpstr);  break;
  case CALENDAR: show_calendar(user);  break;
  case FWHO: who(user,1);  break;
  case MYROOM: personal_room(user);  break;
  case MYLOCK: personal_room_lock(user);  break;
  case VISIT: personal_room_visit(user);  break;
  case MYPAINT: personal_room_decorate(user,0);  break;
  case BEEP: beep(user,inpstr);  break;
  case RMADMIN: personal_room_admin(user);  break;
  case MYKEY: personal_room_key(user);  break;
  case MYBGONE: personal_room_bgone(user);  break;
  default: write_user(user,"Command not executed.\n");
  } /* end main switch */
return 1;
}


/*** Shutdown the talker ***/
void talker_shutdown(UR_OBJECT user, char *str, int reboot)
{
UR_OBJECT u,next;
#ifdef NETLINKS
  NL_OBJECT nl;
#endif
int i;
char *ptr;
char *args[]={ progname,confile,NULL };

if (user!=NULL) ptr=user->recap; else ptr=str;
if (reboot) {
  write_room(NULL,"\007\n~OLSYSTEM:~FY~LI Rebooting now!!\n\n");
  sprintf(text,"*** REBOOT initiated by %s ***\n",ptr);
  }
else {
  write_room(NULL,"\007\n~OLSYSTEM:~FR~LI Shutting down now!!\n\n");
  sprintf(text,"*** SHUTDOWN initiated by %s ***\n",ptr);
  }
write_syslog(text,0,SYSLOG);
#ifdef NETLINKS
  for(nl=nl_first;nl!=NULL;nl=nl->next) shutdown_netlink(nl);
#endif
u=user_first;
while (u) {
  next=u->next;
  disconnect_user(u);
  u=next;
  }
for(i=0;i<port_total;++i) close(listen_sock[i]); 
if (reboot) {
  /* If someone has changed the binary or the config filename while this 
     prog has been running this won't work */
  execvp(progname,args);
  /* If we get this far it hasn't worked */
  sprintf(text,"*** REBOOT FAILED %s: %s ***\n\n",long_date(1),sys_errlist[errno]);
  write_syslog(text,0,SYSLOG);
  exit(12);
  }
sprintf(text,"*** SHUTDOWN complete %s ***\n\n",long_date(1));
write_syslog(text,0,SYSLOG);
exit(0);
}


/*** Shutdown talker interface func. Countdown time is entered in seconds so
	we can specify less than a minute till reboot. ***/
void shutdown_com(UR_OBJECT user)
{
if (rs_which==1) {
  write_user(user,"The reboot countdown is currently active, you must cancel it first.\n");
  return;
  }
if (!strcmp(word[1],"cancel")) {
  if (!rs_countdown || rs_which!=0) {
    write_user(user,"The shutdown countdown is not currently active.\n");
    return;
    }
  if (rs_countdown && !rs_which && rs_user==NULL) {
    write_user(user,"Someone else is currently setting the shutdown countdown.\n");
    return;
    }
  write_room(NULL,"~OLSYSTEM:~RS~FG Shutdown cancelled.\n");
  sprintf(text,"%s cancelled the shutdown countdown.\n",user->name);
  write_syslog(text,1,SYSLOG);
  rs_countdown=0;
  rs_announce=0;
  rs_which=-1;
  rs_user=NULL;
  return;
  }
if (word_count>1 && !is_number(word[1])) {
  write_user(user,"Usage: shutdown [<secs>/cancel]\n");  return;
  }
if (rs_countdown && !rs_which) {
  write_user(user,"The shutdown countdown is currently active, you must cancel it first.\n");
  return;
  }
if (word_count<2) {
  rs_countdown=0;  
  rs_announce=0;
  rs_which=-1; 
  rs_user=NULL;
  }
else {
  rs_countdown=atoi(word[1]);
  rs_which=0;
  }
write_user(user,"\n\07~FR~OL~LI*** WARNING - This will shutdown the talker! ***\n\nAre you sure about this (y/n)? ");
user->misc_op=1;  
no_prompt=1;  
}


/*** Reboot talker interface func. ***/
void reboot_com(UR_OBJECT user)
{
if (!rs_which) {
  write_user(user,"The shutdown countdown is currently active, you must cancel it first.\n");
  return;
  }
if (!strcmp(word[1],"cancel")) {
  if (!rs_countdown) {
    write_user(user,"The reboot countdown is not currently active.\n");
    return;
    }
  if (rs_countdown && rs_user==NULL) {
    write_user(user,"Someone else is currently setting the reboot countdown.\n");
    return;
    }
  write_room(NULL,"~OLSYSTEM:~RS~FG Reboot cancelled.\n");
  sprintf(text,"%s cancelled the reboot countdown.\n",user->name);
  write_syslog(text,1,SYSLOG);
  rs_countdown=0;
  rs_announce=0;
  rs_which=-1;
  rs_user=NULL;
  return;
  }
if (word_count>1 && !is_number(word[1])) {
  write_user(user,"Usage: reboot [<secs>/cancel]\n");  return;
  }
if (rs_countdown) {
  write_user(user,"The reboot countdown is currently active, you must cancel it first.\n");
  return;
  }
if (word_count<2) {
  rs_countdown=0;  
  rs_announce=0;
  rs_which=-1; 
  rs_user=NULL;
  }
else {
  rs_countdown=atoi(word[1]);
  rs_which=1;
  }
write_user(user,"\n\07~FY~OL~LI*** WARNING - This will reboot the talker! ***\n\nAre you sure about this (y/n)? ");
user->misc_op=7;  
no_prompt=1;  
}


/*** Record speech and emotes in the room. ***/
void record(RM_OBJECT rm, char *str)
{
strncpy(rm->revbuff[rm->revline],str,REVIEW_LEN);
rm->revbuff[rm->revline][REVIEW_LEN]='\n';
rm->revbuff[rm->revline][REVIEW_LEN+1]='\0';
rm->revline=(rm->revline+1)%REVIEW_LINES;
}


/*** Records tells and pemotes sent to the user. ***/
void record_tell(UR_OBJECT user, char *str)
{
strncpy(user->revbuff[user->revline],str,REVIEW_LEN);
user->revbuff[user->revline][REVIEW_LEN]='\n';
user->revbuff[user->revline][REVIEW_LEN+1]='\0';
user->revline=(user->revline+1)%REVTELL_LINES;
}


/*** Records shouts and shemotes sent over the talker. ***/
void record_shout(char *str)
{
strncpy(shoutbuff[sbuffline],str,REVIEW_LEN);
shoutbuff[sbuffline][REVIEW_LEN]='\n';
shoutbuff[sbuffline][REVIEW_LEN+1]='\0';
sbuffline=(sbuffline+1)%REVIEW_LINES;
}


/*** Records tells and pemotes sent to the user when afk. ***/
void record_afk(UR_OBJECT user, char *str)
{
strncpy(user->afkbuff[user->afkline],str,REVIEW_LEN);
user->afkbuff[user->afkline][REVIEW_LEN]='\n';
user->afkbuff[user->afkline][REVIEW_LEN+1]='\0';
user->afkline=(user->afkline+1)%REVTELL_LINES;
}


/*** Clear the tell buffer of the user ***/
void clear_afk(UR_OBJECT user)
{
int c;
for(c=0;c<REVTELL_LINES;++c) user->afkbuff[c][0]='\0';
user->afkline=0;
}


/*** Records tells and pemotes sent to the user when in the line editor. ***/
void record_edit(UR_OBJECT user, char *str)
{
strncpy(user->editbuff[user->editline],str,REVIEW_LEN);
user->editbuff[user->editline][REVIEW_LEN]='\n';
user->editbuff[user->editline][REVIEW_LEN+1]='\0';
user->editline=(user->editline+1)%REVTELL_LINES;
}


/*** Clear the review buffer in the room ***/
void clear_revbuff(RM_OBJECT rm)
{
int c;
for(c=0;c<REVIEW_LINES;++c) rm->revbuff[c][0]='\0';
rm->revline=0;
}


/*** Clear the tell buffer of the user ***/
void clear_tells(UR_OBJECT user)
{
int c;
for(c=0;c<REVTELL_LINES;++c) user->revbuff[c][0]='\0';
user->revline=0;
}


/*** Clear the shout buffer of the talker ***/
void clear_shouts(void)
{
int c;
for(c=0;c<REVIEW_LINES;++c) shoutbuff[c][0]='\0';
sbuffline=0;
}


/*** Clear the tell buffer of the user ***/
void clear_edit(UR_OBJECT user)
{
int c;
for(c=0;c<REVTELL_LINES;++c) user->editbuff[c][0]='\0';
user->editline=0;
}


/*** Clear the screen ***/
void cls(UR_OBJECT user)
{
int i;

for(i=0;i<5;++i) write_user(user,"\n\n\n\n\n\n\n\n\n\n");		
}


/***** Mail functions *****/


/*** This is function that sends mail to other users ***/
int send_mail(UR_OBJECT user, char *to, char *ptr, int iscopy)
{
#ifdef NETLINKS
  NL_OBJECT nl;
#endif
FILE *infp,*outfp;
char *c,d,*service,filename[80],cc[4],header[ARR_SIZE];
int amount,size,tmp1,tmp2;
struct stat stbuf;

/* See if remote mail */
c=to;  service=NULL;
while(*c) {
  if (*c=='@') {  
    service=c+1;  *c='\0'; 
    #ifdef NETLINKS
      for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (!strcmp(nl->service,service) && nl->stage==UP) {
	  send_external_mail(nl,user,to,ptr);
	  return 1;
          }
        }
    #endif
    sprintf(text,"Service %s unavailable.\n",service);
    write_user(user,text); 
    return 0;
    }
  ++c;
  }
/* Local mail */
if (!(outfp=fopen("tempfile","w"))) {
  write_user(user,"Error in mail delivery.\n");
  write_syslog("ERROR: Couldn't open tempfile in send_mail().\n",0,SYSLOG);
  return 0;
  }
/* Copy current mail file into tempfile if it exists */
sprintf(filename,"%s/%s/%s.M",USERFILES,USERMAILS,to);
/* but first get the original sizes and write those to the temp file */
amount=mail_sizes(to,1); /* amount of new mail */
if (!amount) {
  if (stat(filename,&stbuf)==-1) size=0;
  else size=stbuf.st_size;
  }
else size=mail_sizes(to,2);
fprintf(outfp,"%d %d\r",++amount,size);

if ((infp=fopen(filename,"r"))) {
  /* Discard first line of mail file. */
  fscanf(infp,"%d %d\r",&tmp1,&tmp2);
  /* Copy rest of file */
  d=getc(infp);  
  while(!feof(infp)) {  putc(d,outfp);  d=getc(infp);  }
  fclose(infp);
  }
if (iscopy) strcpy(cc,"(CC)");
else cc[0]='\0';
header[0]='\0';
/* Put new mail in tempfile */
if (user!=NULL) {
  #ifdef NETLINKS
    if (user->type==REMOTE_TYPE)
      sprintf(header,"~OLFrom: %s@%s  %s %s\n",user->name,user->netlink->service,long_date(0),cc);
    else 
  #endif
      sprintf(header,"~OLFrom: %s  %s %s\n",user->recap,long_date(0),cc);
  }
else sprintf(header,"~OLFrom: MAILER  %s %s\n",long_date(0),cc);
fputs(header,outfp);
fputs(ptr,outfp);
fputs("\n",outfp);
fclose(outfp);
rename("tempfile",filename);
switch(iscopy) {
  case 0: sprintf(text,"Mail is delivered to %s\n",to); break;
  case 1: sprintf(text,"Mail is copied to %s\n",to); break;
  }
write_user(user,text);
if (!iscopy) {
  sprintf(text,"%s sent mail to %s\n",user->name,to);
  write_syslog(text,1,SYSLOG);
  }
write_user(get_user(to),"\07~FT~OL~LI** YOU HAVE NEW MAIL **\n");
forward_email(to,header,ptr);
return 1;
}


#ifdef NETLINKS

/*** Spool mail file and ask for confirmation of users existence on remote site ***/
void send_external_mail(NL_OBJECT nl, UR_OBJECT user, char *to, char *ptr)
{
FILE *fp;
char filename[80];

/* Write out to spool file first */
sprintf(filename,"%s/OUT_%s_%s@%s",MAILSPOOL,user->name,to,nl->service);
if (!(fp=fopen(filename,"a"))) {
  sprintf(text,"%s: unable to spool mail.\n",syserror);
  write_user(user,text);
  sprintf(text,"ERROR: Couldn't open file %s to append in send_external_mail().\n",filename);
  write_syslog(text,0,SYSLOG);
  return;
  }
putc('\n',fp);
fputs(ptr,fp);
fclose(fp);
/* Ask for verification of users existence */
sprintf(text,"EXISTS? %s %s\n",to,user->name);
write_sock(nl->socket,text);
/* Rest of delivery process now up to netlink functions */
write_user(user,"Mail sent to external talker.\n");
}

#endif


/*** Send mail message ***/
void smail(UR_OBJECT user, char *inpstr, int done_editing)
{
UR_OBJECT u;
int remote,has_account;
char *c;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot mail anyone.\n");  return;
  }
if (done_editing) {
  if (*user->malloc_end--!='\n') *user->malloc_end--='\n';
  send_mail(user,user->mail_to,user->malloc_start,0);
  send_copies(user,user->malloc_start);
  user->mail_to[0]='\0';
  return;
  }
if (word_count<2) {
  write_user(user,"Smail who?\n");  return;
  }
/* See if its to another site */
remote=0;
has_account=0;
c=word[1];
while(*c) {
  if (*c=='@') {  
    if (c==word[1]) {
      write_user(user,"Users name missing before @ sign.\n");  
      return;
      }
    remote=1;  break;  
    }
  ++c;
  }
word[1][0]=toupper(word[1][0]);
/* See if user exists */
if (!remote) {
  u=NULL;
  if (!(u=get_user(word[1]))) {
    if (!(find_user_listed(word[1]))) {
      write_user(user,nosuchuser);  return;
      }
    has_account=1;
    }
  if (u==user && user->level<ARCH) {
    write_user(user,"Trying to mail yourself is the fifth sign of madness.\n");
    return;
    }
  if ((check_igusers(u,user))!=-1 && user->level<GOD) {
    sprintf(text,"%s is ignoring smails from you.\n",u->recap);
    write_user(user,text);
    return;
    }
  if (u!=NULL) strcpy(word[1],u->name); 
  if (!has_account) {
    /* See if user has local account */
    if (!(find_user_listed(word[1]))) {
      sprintf(text,"%s is a remote user and does not have a local account.\n",u->name);
      write_user(user,text);  
      return;
      }
    }
  }
if (word_count>2) {
  /* One line mail */
  strcat(inpstr,"\n"); 
  send_mail(user,word[1],remove_first(inpstr),0);
  send_copies(user,remove_first(inpstr));
  return;
  }
#ifdef NETLINKS
  if (user->type==REMOTE_TYPE) {
    write_user(user,"Sorry, due to software limitations remote users cannot use the line editor.\nUse the '.smail <user> <mesg>' method instead.\n");
    return;
    }
#endif
sprintf(text,"\n~BB*** Writing mail message to %s ***\n\n",word[1]);
write_user(user,text);
user->misc_op=4;
strcpy(user->mail_to,word[1]);
editor(user,NULL);
}


/*** Read your mail ***/
void rmail(UR_OBJECT user) {
int ret,size;
char filename[80];
struct stat stbuf;

sprintf(filename,"%s/%s/%s.M",USERFILES,USERMAILS,user->name);
/* get file size */
if (stat(filename,&stbuf)==-1) size=0;
else size=stbuf.st_size;

/* Just reading the one message or all new mail */
if (word_count>1) {
  strtolower(word[1]);
  if (!strcmp(word[1],"new")) read_new_mail(user);
  else read_specific_mail(user);
  return;
  }
/* Update last read / new mail received time at head of file */
if (!(reset_mail_counts(user))) {
  write_user(user,"You don't have any mail.\n");
  return;
  }
/* Reading the whole mail box */
write_user(user,"\n~BB*** You mailbox has the following messages ***\n\n");
ret=more(user,user->socket,filename);
if (ret==1) user->misc_op=2;
}


/* allows a user to choose what message to read */
void read_specific_mail(UR_OBJECT user) {
FILE *fp;
int valid,cnt,total,smail_number,tmp1,tmp2;
char w1[ARR_SIZE],line[ARR_SIZE],filename[80];

if (word_count>2) {
  write_user(user,"Usage: rmail [new/<message #>]\n");
  return;
  }
if (!(total=mail_sizes(user->name,0))) {
  write_user(user,"You currently have no mail.\n");
  return;
  }
smail_number=atoi(word[1]);
if (!smail_number) {
  write_user(user,"Usage: rmail [new/<message #>]\n");
  return;
  }
if (smail_number>total) {
  sprintf(text,"You only have %d message%s in your mailbox.\n",total,PLTEXT_S(total));
  write_user(user,text);
  return;
  }
/* Update last read / new mail received time at head of file */
if (!(reset_mail_counts(user))) {
  write_user(user,"You don't have any mail.\n");
  return;
  }
sprintf(filename,"%s/%s/%s.M",USERFILES,USERMAILS,user->name);
if (!(fp=fopen(filename,"r"))) {
  write_user(user,"There was an error trying to read your mailbox.\n");
  sprintf(text,"Unable to open %s's mailbox in read_mail_specific.\n",user->name);
  write_syslog(text,0,SYSLOG);
  return;
  }
valid=1;  cnt=1;
fscanf(fp,"%d %d\r",&tmp1,&tmp2);
fgets(line,ARR_SIZE-1,fp);
while(!feof(fp)) {
  if (*line=='\n') valid=1;
  sscanf(line,"%s",w1);
  if (valid && (!strcmp(w1,"~OLFrom:") || !strcmp(w1,"From:"))) {
    if (smail_number==cnt) {
      write_user(user,"\n");
      while(*line!='\n') {
	write_user(user,line);
	fgets(line,ARR_SIZE-1,fp);
        }
      }
    valid=0;  cnt++;
    if (cnt>smail_number) goto SKIP; /* no point carrying on if read already */
    }
  fgets(line,ARR_SIZE-1,fp);
  }
SKIP:
fclose(fp);
sprintf(text,"\nMail message number ~FM~OL%d~RS out of ~FM~OL%d~RS.\n\n",smail_number,total);
write_user(user,text);
}


/*** Read just the new mail, taking the fseek size from the stat st_buf saved in the
     mail file first line - along with how many new mail messages there are
     ***/
void read_new_mail(UR_OBJECT user) {
char filename[80];
int total,new;

/* Get total number of mail */
if (!(total=mail_sizes(user->name,0))) {
  write_user(user,"You do not have any mail.\n");  return;
  }
/* Get the amount of new mail */
if (!(new=mail_sizes(user->name,1))) {
  write_user(user,"You do not have any new mail.\n");  return;
  }
if (new==total) {
  /* Update last read / new mail received time at head of file */
  if (!(reset_mail_counts(user))) {
    write_user(user,"You don't have any mail.\n");
    return;
    }
  sprintf(filename,"%s/%s/%s.M",USERFILES,USERMAILS,user->name);
  write_user(user,"\n~BB*** These are the new mail messages you have in your mailbox ***\n\n");
  more(user,user->socket,filename);
  return;
  }
/* Get where new mail starts */
user->filepos=mail_sizes(user->name,2);
/* Update last read / new mail received time at head of file */
if (!(reset_mail_counts(user))) {
  write_user(user,"You don't have any mail.\n");
  return;
  }
sprintf(filename,"%s/%s/%s.M",USERFILES,USERMAILS,user->name);
write_user(user,"\n~BB*** These are the new mail messages you have in your mailbox ***\n\n");
if (more(user,user->socket,filename)!=1) user->filepos=0;
else user->misc_op=2;
return;
}


/*** Delete some or all of your mail. A problem here is once we have deleted
     some mail from the file do we mark the file as read? If not we could
     have a situation where the user deletes all his mail but still gets
     the YOU HAVE UNREAD MAIL message on logging on if the idiot forgot to 
     read it first. ***/
void dmail(UR_OBJECT user)
{
int num,cnt;
char filename[80];

if (word_count<2) {
  write_user(user,"Usage: dmail all\n");
  write_user(user,"Usage: dmail <#>\n");
  write_user(user,"Usage: dmail to <#>\n");
  write_user(user,"Usage: dmail from <#> to <#>\n");
  return;
  }
if (get_wipe_parameters(user)==-1) return;
num=mail_sizes(user->name,0);
if (!num) {
  write_user(user,"You have no mail to delete.\n");  return;
  }
sprintf(filename,"%s/%s/%s.M",USERFILES,USERMAILS,user->name);
if (user->wipe_from==-1) {
  write_user(user,"\07~OL~FR~LIDelete all of your mail?~RS (y/n): ");
  user->misc_op=18;
  return;
  }
if (user->wipe_from>num) {
  sprintf(text,"You only have %d mail message%s.\n",num,PLTEXT_S(num));
  write_user(user,text);   return;
  }
cnt=wipe_messages(filename,user->wipe_from,user->wipe_to,1);
reset_mail_counts(user);
if (cnt==num) {
  unlink(filename);
  sprintf(text,"There %s only %d mail message%s, all now deleted.\n",PLTEXT_WAS(cnt),cnt,PLTEXT_S(cnt));
  write_user(user,text);
  return;
  }
sprintf(text,"%d mail message%s deleted.\n",cnt,PLTEXT_S(cnt));
write_user(user,text);
user->read_mail=time(0)+1;
}


/*** Show list of people your mail is from without seeing the whole lot ***/
void mail_from(UR_OBJECT user)
{
FILE *fp;
int valid,cnt,tmp1,tmp2,nmail;
char w1[ARR_SIZE],line[ARR_SIZE],filename[80];

sprintf(filename,"%s/%s/%s.M",USERFILES,USERMAILS,user->name);
if (!(fp=fopen(filename,"r"))) {
  write_user(user,"You have no mail.\n");
  return;
  }
write_user(user,"\n~BB*** Mail from ***\n\n");
valid=1;  cnt=0;
fscanf(fp,"%d %d\r",&tmp1,&tmp2); 
fgets(line,ARR_SIZE-1,fp);
while(!feof(fp)) {
  if (*line=='\n') valid=1;
  sscanf(line,"%s",w1);
  if (valid && (!strcmp(w1,"~OLFrom:") || !strcmp(w1,"From:"))) {
    cnt++;  valid=0;
    sprintf(text,"~FT%2d)~RS %s",cnt,remove_first(line));
    write_user(user,text);
    }
  fgets(line,ARR_SIZE-1,fp);
  }
fclose(fp);
nmail=mail_sizes(user->name,1);
sprintf(text,"\nTotal of ~OL%d~RS message%s, ~OL%d~RS of which %s new.\n\n",cnt,PLTEXT_S(cnt),nmail,PLTEXT_IS(nmail));
write_user(user,text);
}


/* get users which to send copies of smail to */
void copies_to(UR_OBJECT user)
{
int remote,i=0,docopy,found,cnt;
char *c;

if (com_num==NOCOPIES) {
  for (i=0; i<MAX_COPIES; i++) user->copyto[i][0]='\0';
  write_user(user,"Sending no copies of your next smail.\n");  return;
  }
if (word_count<2) {
  text[0]='\0';  found=0;
  for (i=0; i<MAX_COPIES; i++) {
    if (!user->copyto[i][0]) continue;
    if (++found==1) write_user(user,"Sending copies of your next smail to...\n");
    strcat(text,"   ");  strcat(text,user->copyto[i]);
    }
  strcat(text,"\n\n");
  if (!found) write_user(user,"You are not sending a copy to anyone.\n");
  else write_user(user,text);
  return;
  }
if (word_count>MAX_COPIES+1) {      /* +1 because 1 count for command */
  sprintf(text,"You can only copy to a maximum of %d people.\n",MAX_COPIES);
  write_user(user,text);  return;
  }
write_user(user,"\n");
cnt=0;
for (i=0; i<MAX_COPIES; i++) user->copyto[i][0]='\0';
for (i=1; i<word_count; i++) {
  remote=0;  docopy=1;
  /* See if its to another site */
  c=word[i];
  while(*c) {
    if (*c=='@') {
      if (c==word[i]) {
	sprintf(text,"Name missing before @ sign for copy to name '%s'.\n",word[i]);
	write_user(user,text);  docopy=0; goto SKIP;
        }
      remote=1;  docopy=1;  goto SKIP;
      }
    ++c;
    }
  /* See if user exists */
  if (get_user(word[i])==user && user->level<ARCH) {
    write_user(user,"You cannot send yourself a copy.\n");
    docopy=0;  goto SKIP;
    }
  word[i][0]=toupper(word[i][0]);
  if (!remote) {
    if (!(find_user_listed(word[i]))) {
      sprintf(text,"There is no such user with the name '%s' to copy to.\n",word[i]);
      write_user(user,text);
      docopy=0;
      }
    else docopy=1;
    }
SKIP:
  if (docopy) {
    strcpy(user->copyto[cnt],word[i]);  cnt++;
    }
  }
text[0]='\0';  i=0;  found=0;
for (i=0; i<MAX_COPIES; i++) {
  if (!user->copyto[i][0]) continue;
  if (++found==1) write_user(user,"Sending copies of your next smail to...\n");
  strcat(text,"   ");  strcat(text,user->copyto[i]);
  }
strcat(text,"\n\n");
if (!found) write_user(user,"You are not sending a copy to anyone.\n");
else write_user(user,text);
}


/* send out copy of smail to anyone that is in user->copyto */
void send_copies(UR_OBJECT user, char *ptr)
{
int i,found=0;

for (i=0; i<MAX_COPIES; i++) {
  if (!user->copyto[i][0]) continue;
  if (++found==1) write_user(user,"Attempting to send copies of smail...\n");
  if (send_mail(user,user->copyto[i],ptr,1)) {
    sprintf(text,"%s sent a copy of mail to %s.\n",user->name,user->copyto[i]);
    write_syslog(text,1,SYSLOG);
    }
  }
for (i=0; i<MAX_COPIES; i++) user->copyto[i][0]='\0';
}


/*** Send mail message to everyone ***/
void level_mail(UR_OBJECT user, char *inpstr, int done_editing)
{
int level,i;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot mail anyone.\n");  return;
  }
strtoupper(word[1]);
if (done_editing) {
  switch(user->lmail_lev) {
    case -1:
      for (i=WIZ;i<=GOD;i++) {
	if (send_broadcast_mail(user,user->malloc_start,i,-1)) {
	  sprintf(text,"You have sent mail to all the %ss.\n",level_name[i]);
	  write_user(user,text);
          }
        }
      sprintf(text,"%s sent mail to all the Wizzes.\n",user->name);
      write_syslog(text,1,SYSLOG);
      return;
    case -2:
      if (send_broadcast_mail(user,user->malloc_start,-1,-2))
	write_user(user,"You have sent mail to all the users.\n");
      sprintf(text,"%s sent mail to all the users.\n",user->name);
      write_syslog(text,1,SYSLOG);
      return;
    } /* end switch */
  if (send_broadcast_mail(user,user->malloc_start,user->lmail_lev,user->lmail_lev)) {
    sprintf(text,"You have sent mail to all the %ss.\n",level_name[user->lmail_lev]);
    write_user(user,text);
    }
  sprintf(text,"%s sent mail to all the %ss.\n",user->name,level_name[user->lmail_lev]);
  write_syslog(text,1,SYSLOG);
  user->lmail_lev=-3;
  return;
  }
if (word_count>2) {
  if ((level=get_level(word[1]))==-1) {
    if (!strcmp(word[1],"WIZZES")) level=-1;
    else if (!strcmp(word[1],"ALL")) level=-2;
    else {
      write_user(user,"Usage: lmail <level name>/wizzes/all [<message>]\n");
      return;
      }
    }
  strcat(inpstr,"\n"); /* risky but hopefully it'll be ok */
  switch(level) {
    case -1:
      for (i=WIZ;i<=GOD;i++) {
  	  if (send_broadcast_mail(user,remove_first(inpstr),i,-1)) {
	    sprintf(text,"You have sent mail to all the %ss.\n",level_name[i]);
	    write_user(user,text);
	    }
        }
      sprintf(text,"%s sent mail to all the Wizzes.\n",user->name);
      write_syslog(text,1,SYSLOG);
      return;
    case -2:
      if (send_broadcast_mail(user,remove_first(inpstr),-1,-2))
	write_user(user,"You have sent mail to all the users.\n");
      sprintf(text,"%s sent mail to all the users.\n",user->name);
      write_syslog(text,1,SYSLOG);
      return;
    } /* end switch */
  if (send_broadcast_mail(user,remove_first(inpstr),level,level)) {
    sprintf(text,"You have sent mail to all the %ss.\n",level_name[level]);
    write_user(user,text);
    }
  sprintf(text,"%s sent mail to all the %ss.\n",user->name,level_name[level]);
  write_syslog(text,1,SYSLOG);
  return;
  }
if (user->type==REMOTE_TYPE) {
  write_user(user,"Sorry, due to software limitations remote users cannot use the line editor.\nUse the '.lmail <level>/wizzes/all <mesg>' method instead.\n");
  return;
  }
if ((level=get_level(word[1]))==-1) {
  if (!strcmp(word[1],"WIZZES")) level=-1;
  else if (!strcmp(word[1],"ALL")) level=-2;
  else {
    write_user(user,"Usage: lmail <level name>/wizzes/all [<message>]\n");
    return;
    }
  }
user->lmail_lev=level;
write_user(user,"~FR~LICaution~RS:  This will hang the talker until the process has finished!\n");
write_user(user,"          Use only when there are no, or a nominal amount of users logged on.\n");
if (user->lmail_lev==-1) sprintf(text,"\n~FG*** Writing broadcast level mail message to all the Wizzes ***\n\n");
else if (user->lmail_lev==-2) sprintf(text,"\n~FG*** Writing broadcast level mail message to everyone ***\n\n");
else sprintf(text,"\n~FG*** Writing broadcast level mail message to all the %ss ***\n\n",level_name[user->lmail_lev]);
write_user(user,text);
user->misc_op=9;
editor(user,NULL);
}


/*** This is function that sends mail to other users ***/
int send_broadcast_mail(UR_OBJECT user, char *ptr, int lvl, int type)
{
FILE *infp,*outfp;
char d,*cc,header[ARR_SIZE],filename[80];
int tmp1,tmp2,amount,size,cnt=0;
struct user_dir_struct *entry;
struct stat stbuf;

if ((entry=first_dir_entry)==NULL) return 0;
while (entry!=NULL) {
  /*    just wizzes                            specific level    */
  if (((type==-1) && (entry->level!=lvl)) || ((type>=0) && (entry->level!=lvl))) {
    entry=entry->next;
    continue;
    }
  /* if type == -2 then do everyone */
  entry->name[0]=toupper(entry->name[0]);
  if (!(outfp=fopen("tempfile","w"))) {
    write_user(user,"Error in mail delivery.\n");
    write_syslog("ERROR: Couldn't open tempfile in send_broadcast_mail().\n",0,SYSLOG);
    entry=entry->next;
    continue;
    }
  /* Write current time on first line of tempfile */
  sprintf(filename,"%s/%s/%s.M",USERFILES,USERMAILS,entry->name);
  /* first get old file size if any new mail, and also new mail count */
  amount=mail_sizes(entry->name,1);
  if (!amount) {
    if (stat(filename,&stbuf)==-1) size=0;
    else size=stbuf.st_size;
    }
  else size=mail_sizes(entry->name,2);
  fprintf(outfp,"%d %d\r",++amount,size);
  if ((infp=fopen(filename,"r"))) {
    /* Discard first line of mail file. */
    fscanf(infp,"%d %d\r",&tmp1,&tmp2);
    /* Copy rest of file */
    d=getc(infp);
    while(!feof(infp)) {  putc(d,outfp);  d=getc(infp);  }
    fclose(infp);
    }
  header[0]='\0';   cc='\0';
  switch(type) {
  case -1: cc="(BCLM Wizzes)";  break;
  case -2: cc="(BCLM All users)";  break;
  default: sprintf(text,"(BCLM %s lvl)",level_name[lvl]);
    cc=text;
    break;
    }
  if (user!=NULL) {
    #ifdef NETLINKS
      if (user->type==REMOTE_TYPE)
      sprintf(header,"~OLFrom: %s@%s  %s %s\n",user->name,user->netlink->service,long_date(0),cc);
      else 
   #endif
	sprintf(header,"~OLFrom: %s  %s %s\n",user->recap,long_date(0),cc);
    }
  else sprintf(header,"~OLFrom: MAILER  %s %s\n",long_date(0),cc);
  fprintf(outfp,header);
  fputs(ptr,outfp);
  fputs("\n",outfp);
  fclose(outfp);
  rename("tempfile",filename);
  forward_email(entry->name,header,ptr);
  write_user(get_user(entry->name),"\07~FT~OL~LI*** YOU HAVE NEW MAIL ***\n");
  ++cnt;
  entry=entry->next;
  } /* end while */
return 1;
}


/*** returns stats on the mail file.  If type=0 then return total amount of mail message.
     if type=1 then return the number of new mail messages.  Else return size of mail file.
     ***/
int mail_sizes(char *name, int type)
{
FILE *fp;
int valid,cnt,new,size;
char w1[ARR_SIZE],line[ARR_SIZE],filename[80],*str;

cnt=new=size=0;
name[0]=toupper(name[0]);
sprintf(filename,"%s/%s/%s.M",USERFILES,USERMAILS,name);
if (!(fp=fopen(filename,"r"))) return cnt;
valid=1;
fscanf(fp,"%d %d\r",&new,&size);
/* return amount of new mail or size of mail file */
if (type) {
  fclose(fp);
  return type==1?new:size;
  }
/* count up total mail */
fgets(line,ARR_SIZE-1,fp);
while(!feof(fp)) {
  if (*line=='\n') valid=1;
  sscanf(line,"%s",w1);
  str=colour_com_strip(w1);
  if (valid && !strcmp(str,"From:")) {
    cnt++;  valid=0;
    }
  fgets(line,ARR_SIZE-1,fp);
  }
fclose(fp);
return cnt;
}


/*** Reset the new mail count ans file size at the top of a user's mail file ***/
int reset_mail_counts(UR_OBJECT user) {
FILE *infp,*outfp;
int size,tmp1,tmp2;
char c,filename[80];
struct stat stbuf;

sprintf(filename,"%s/%s/%s.M",USERFILES,USERMAILS,user->name);
/* get file size */
if (stat(filename,&stbuf)==-1) size=0;
else size=stbuf.st_size;

if (!(infp=fopen(filename,"r"))) return 0;
/* Update last read / new mail received time at head of file */
if ((outfp=fopen("tempfile","w"))) {
  fprintf(outfp,"0 %d\r",size);
  /* skip first line of mail file */
  fscanf(infp,"%d %d\r",&tmp1,&tmp2);
  /* Copy rest of file */
  c=getc(infp);
  while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }
  fclose(outfp);
  rename("tempfile",filename);
  }
user->read_mail=time(0);
fclose(infp);
return 1;
}


/*** verify user's email when it is set specified ***/
void set_forward_email(UR_OBJECT user)
{
FILE *fp;
char filename[80];

if (!user->email[0] || !strcmp(user->email,"#UNSET")) {
  write_user(user,"Your email address is currently ~FRunset~RS.  If you wish to use the\nauto-forwarding function then you must set your email address.\n\n");
  user->autofwd=0;
  return;
  }
if (!forwarding) {
  write_user(user,"Even though you have set your email, the auto-forwarding function is currently unavaliable.\n");
  user->mail_verified=0;
  user->autofwd=0;
  return;
  }
user->mail_verified=0;
user->autofwd=0;
/* Let them know by email */
sprintf(filename,"%s/%s.FWD",MAILSPOOL,user->name);
if (!(fp=fopen(filename,"w"))) {
  write_syslog("Unable to open forward mail file in set_forward_email()\n",0,SYSLOG);
  return;
  }
sprintf(user->verify_code,"amnuts%d",rand()%999);
/* email header */
fprintf(fp,"From: %s\n",talker_name);
fprintf(fp,"To: %s <%s>\n",user->name,user->email);
fprintf(fp,"Subject: Verification of auto-mail.\n");
fprintf(fp,"\n");
/* email body */
fprintf(fp,"Hello, %s.\n\n",user->name);
fprintf(fp,"Thank you for setting your email address, and now that you have done so you are\n");
fprintf(fp,"able to use the auto-forwarding function on The Talker to have any smail sent to\n");
fprintf(fp,"your email address.  To be able to do this though you must verify that you have\n");
fprintf(fp,"received this email.\n\n");
fprintf(fp,"Your verification code is: %s\n\n",user->verify_code);
fprintf(fp,"Use this code with the 'verify' command when you next log onto the talker.\n");
fprintf(fp,"You will then have to use the 'set' command to turn on/off auto-forwarding.\n\n");
fprintf(fp,"Thank you for coming to our talker - we hope you enjoy it!\n\n   The Staff.\n\n");
fputs(talker_signature,fp);
fclose(fp);
/* send the mail */
send_forward_email(user->email,filename);
sprintf(text,"%s had mail sent to them by set_forward_email().\n",user->name);
write_syslog(text,1,SYSLOG);
/* Inform them online */
write_user(user,"Now that you have set your email you can use the auto-forward functions.\n");
write_user(user,"You must verify your address with the code you will receive shortly, via email.\n");
write_user(user,"If you do not receive any email, then use ~FTset email <email>~RS again, making\nsure you have the correct address.\n\n");
}


/* verify that mail has been sent to the address supplied */
void verify_email(UR_OBJECT user)
{
if (word_count<2) {
  write_user(user,"Usage: verify <verification code>\n");
  return;
  }
if (!user->email[0] || !strcmp(user->email,"#UNSET")) {
  write_user(user,"You have not yet set your email address.  You must do this first.\n");
  return;
  }
if (!strcmp(user->verify_code,"#EMAILSET")) {
  write_user(user,"You have already verified your current email address.\n");
  return;
  }
if (strcmp(user->verify_code,word[1]) || !strcmp(user->verify_code,"#NONE")) {
  write_user(user,"That does not match your verification code.  Please check your code and try again.\n");
  return;
  }
strcpy(user->verify_code,"#EMAILSET");
user->mail_verified=1;
write_user(user,"\nThe verification code you gave was correct.\nYou may now use the auto-forward functions.\n\n");
}


/*** send smail to the email ccount ***/
void forward_email(char *name, char *from, char *message)
{
FILE *fp;
UR_OBJECT u;
char filename[80];
int on=0;

if (!forwarding) return;
if ((u=get_user(name))) {
  on=1;
  goto SKIP;
  }
/* Have to create temp user if not logged on to check if email verified, etc */
if ((u=create_user())==NULL) {
  write_syslog("ERROR: Unable to create temporary user object in forward_email().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,name);
if (!load_user_details(u)) {
  destruct_user(u);
  destructed=0;
  return;
  }
on=0;
SKIP:
if (!u->mail_verified) {
  if (!on) { destruct_user(u);  destructed=0; }
  return;
  }
if (!u->autofwd){
  if (!on) { destruct_user(u);  destructed=0; }
  return;
  } 

sprintf(filename,"%s/%s.FWD",MAILSPOOL,u->name);
if (!(fp=fopen(filename,"w"))) {
  write_syslog("Unable to open forward mail file in set_forward_email()\n",0,SYSLOG);
  return;
  }
fprintf(fp,"From: %s\n",talker_name);
fprintf(fp,"To: %s <%s>\n",u->name,u->email);
fprintf(fp,"Subject: Auto-forward of smail.\n");
fprintf(fp,"\n");
from=colour_com_strip(from);
fputs(from,fp);
fputs("\n",fp);
message=colour_com_strip(message);
fputs(message,fp);
fputs("\n\n",fp);
fputs(talker_signature,fp);
fclose(fp);
send_forward_email(u->email,filename);
sprintf(text,"%s had mail sent to their email address.\n",u->name);
write_syslog(text,1,SYSLOG);
if (!on) {
  destruct_user(u);
  destructed=0;
  }
return;
}


/*** stop zombie processes ***/
int send_forward_email(char *send_to, char *mail_file)
{
  switch(double_fork()) {
    case -1 : unlink(mail_file); return -1; /* double_fork() failed */
    case  0 : sprintf(text,"mail %s < %s",send_to,mail_file);
              system(text);
	      unlink(mail_file);
	      _exit(1);
	      break; /* should never get here */
    default: break;
    }
return 1;
}


/*** signal trapping not working, so fork twice ***/
int double_fork(void)
{
pid_t pid;
int dfstatus;

if (!(pid=fork())) {
  switch(fork()) {
    case  0: return 0;
    case -1: _exit(-1);
    default: _exit(0);
    }
  }
if (pid<0||waitpid(pid,&dfstatus,0)<0) return -1;
if (WIFEXITED(dfstatus))
  if(WEXITSTATUS(dfstatus)==0) return 1;
  else errno=WEXITSTATUS(dfstatus);
else errno=EINTR;
return -1;
}


/***** Message aboards *****/


/*** Read the message board ***/
void read_board(UR_OBJECT user)
{
RM_OBJECT rm;
char filename[80],*name,rmname[USER_NAME_LEN];
int ret;

rm=NULL;
if (word_count<2) rm=user->room;
else {
  if (word_count>=3) {
    if ((rm=get_room(word[1]))==NULL) {
      write_user(user,nosuchroom);  return;
      }
    read_board_specific(user,rm,atoi(word[2]));
    return;
    }
  if (word_count==2) {
    if (atoi(word[1])) {
      read_board_specific(user,user->room,atoi(word[1]));
      return;
      }
    else {
      if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
        }
      }
    }
  if (!has_room_access(user,rm)) {
    write_user(user,"That room is currently private, you cannot read the board.\n");
    return;
    }
  }	
sprintf(text,"\n~BB*** The %s message board ***\n\n",rm->name);
write_user(user,text);
if (rm->access==PERSONAL_LOCKED || rm->access==PERSONAL_UNLOCKED) {
  midcpy(rm->name,rmname,1,strlen(rm->name)-2);
  rmname[0]=toupper(rmname[0]);
  sprintf(filename,"%s/%s/%s.B",USERFILES,USERROOMS,rmname);
  }
else sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
if (!(ret=more(user,user->socket,filename))) 
  write_user(user,"There are no messages on the board.\n\n");
else if (ret==1) user->misc_op=2;
if (user->vis) name=user->recap; else name=invisname;
if (rm==user->room) {
  sprintf(text,"%s reads the message board.\n",name);
  write_room_except(user->room,text,user);
  }
}


/* Allows a user to read a specific message number */
void read_board_specific(UR_OBJECT user, RM_OBJECT rm, int msg_number)
{
FILE *fp;
int valid,cnt,pt;
char id[ARR_SIZE],line[ARR_SIZE],filename[80],*name,rmname[USER_NAME_LEN];

if (!rm->mesg_cnt) {
  sprintf(text,"There are no messages posted on the %s board.\n",rm->name);
  write_user(user,text);
  return;
  }
if (!msg_number) {
  write_user(user,"Usage: read [<room>] [<message #>]\n");
  return;
  }
if (msg_number>rm->mesg_cnt) {
  sprintf(text,"There %s only %d message%s posted on the %s board.\n",PLTEXT_IS(rm->mesg_cnt),rm->mesg_cnt,PLTEXT_S(rm->mesg_cnt),rm->name);
  write_user(user,text);
  return;
  }
if (rm!=user->room && !has_room_access(user,rm)) {
  write_user(user,"That room is currently private, you cannot read the board.\n");
  return;
  }
if (rm->access==PERSONAL_LOCKED || rm->access==PERSONAL_UNLOCKED) {
  midcpy(rm->name,rmname,1,strlen(rm->name)-2);
  rmname[0]=toupper(rmname[0]);
  sprintf(filename,"%s/%s/%s.B",USERFILES,USERROOMS,rmname);
  }
else sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
if (!(fp=fopen(filename,"r"))) {
  write_user(user,"There was an error trying to read the message board.\n");
  sprintf(text,"Unable to open message board for %s in read_board_specific().\n",rm->name);
  write_syslog(text,0,SYSLOG);
  return;
  }
sprintf(text,"\n~BB~FG*** The %s message board ***\n\n",rm->name);
write_user(user,text);
valid=1;  cnt=1;  id[0]='\0';
fgets(line,ARR_SIZE-1,fp);
while(!feof(fp)) {
  if (*line=='\n') valid=1;
  sscanf(line,"%s %d",id,&pt);
  if (valid && !strcmp(id,"PT:")) {
    if (msg_number==cnt) {
      while(*line!='\n') {
	write_user(user,line);
	fgets(line,ARR_SIZE-1,fp);
        }
      }
    valid=0;  cnt++;
    if (cnt>msg_number) goto SKIP; /* no point carrying on if read already */
    }
  fgets(line,ARR_SIZE-1,fp);
  }
SKIP:
fclose(fp);
sprintf(text,"\nMessage number ~FM~OL%d~RS out of ~FM~OL%d~RS.\n\n",msg_number,rm->mesg_cnt);
write_user(user,text);
if (user->vis) name=user->recap;  else name=invisname;
if (rm==user->room) {
  sprintf(text,"%s reads the message board.\n",name);
  if (user->level<GOD || user->vis) write_room_except(user->room,text,user);
  }
}


/*** Write on the message board ***/
void write_board(UR_OBJECT user, char *inpstr, int done_editing)
{
FILE *fp;
int cnt,inp;
char *ptr,*name,filename[80],rmname[USER_NAME_LEN];

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot write on the board.\n");  
  return;
  }
if (!done_editing) {
  if (word_count<2) {
  #ifdef NETLINKS
    if (user->type==REMOTE_TYPE) {
      /* Editor won't work over netlink cos all the prompts will go
	 wrong, I'll address this in a later version. */
      write_user(user,"Sorry, due to software limitations remote users cannot use the line editor.\nUse the '.write <mesg>' method instead.\n");
      return;
      }
  #endif
    write_user(user,"\n~BB*** Writing board message ***\n\n");
    user->misc_op=3;
    editor(user,NULL);
    return;
    }
  ptr=inpstr;
  inp=1;
  }
else { ptr=user->malloc_start;  inp=0; }
if (user->room->access==PERSONAL_LOCKED || user->room->access==PERSONAL_UNLOCKED) {
  midcpy(user->room->name,rmname,1,strlen(user->room->name)-2);
  rmname[0]=toupper(rmname[0]);
  sprintf(filename,"%s/%s/%s.B",USERFILES,USERROOMS,rmname);
  }
else sprintf(filename,"%s/%s.B",DATAFILES,user->room->name);
if (!(fp=fopen(filename,"a"))) {
  sprintf(text,"%s: cannot write to file.\n",syserror);
  write_user(user,text);
  sprintf(text,"ERROR: Couldn't open file %s to append in write_board().\n",filename);
  write_syslog(text,0,SYSLOG);
  return;
  }
if (user->vis) name=user->recap; else name=invisname;
/* The posting time (PT) is the time its written in machine readable form, this 
   makes it easy for this program to check the age of each message and delete 
   as appropriate in check_messages() */
#ifdef NETLINKS
  if (user->type==REMOTE_TYPE) 
    sprintf(text,"PT: %d\r~OLFrom: %s@%s  %s\n",(int)(time(0)),name,user->netlink->service,long_date(0));
  else 
#endif
    sprintf(text,"PT: %d\r~OLFrom: %s  %s\n",(int)(time(0)),name,long_date(0));
fputs(text,fp);
cnt=0;
while(*ptr!='\0') {
  putc(*ptr,fp);
  if (*ptr=='\n') cnt=0; else ++cnt;
  if (cnt==80) { putc('\n',fp); cnt=0; }
  ++ptr;
  }
if (inp) fputs("\n\n",fp); else putc('\n',fp);
fclose(fp);
write_user(user,"You write the message on the board.\n");
sprintf(text,"%s writes a message on the board.\n",name);
write_room_except(user->room,text,user);
user->room->mesg_cnt++;
}


/*** Wipe some messages off the board ***/
void wipe_board(UR_OBJECT user)
{
int cnt;
char filename[80],*name,rmname[USER_NAME_LEN];
RM_OBJECT rm;

rm=user->room;
if (word_count<2 && ((user->level>=WIZ && !is_personal_room(rm))
    || (is_personal_room(rm) && (is_my_room(user,rm) || user->level>=GOD)))) {
  write_user(user,"Usage: wipe all\n");
  write_user(user,"Usage: wipe <#>\n");
  write_user(user,"Usage: wipe to <#>\n");
  write_user(user,"Usage: wipe from <#> to <#>\n");
  return;
  }
else if (word_count<2 && ((user->level<WIZ && !is_personal_room(rm))
	 || (is_personal_room(rm) && !is_my_room(user,rm) && user->level<GOD))) {
  write_user(user,"Usage: wipe <#>\n");
  return;
  }
switch(is_personal_room(rm)) {
  case 0:
    if (user->level<WIZ && !(check_board_wipe(user))) return;
    else if (get_wipe_parameters(user)==-1) return;
    break;
  case 1:
    if (!is_my_room(user,rm) && user->level<GOD && !check_board_wipe(user)) return;
    else if (get_wipe_parameters(user)==-1) return;
    break;
  }
if (user->vis) name=user->recap; else name=invisname;
if (rm->access==PERSONAL_LOCKED || rm->access==PERSONAL_UNLOCKED) {
  midcpy(rm->name,rmname,1,strlen(rm->name)-2);
  rmname[0]=toupper(rmname[0]);
  sprintf(filename,"%s/%s/%s.B",USERFILES,USERROOMS,rmname);
  }
else sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
if (!rm->mesg_cnt) {
  write_user(user,"There are no messages on the room's board.\n");  return;
  }
if (user->wipe_from==-1) {
  unlink(filename);
  write_user(user,"All messages deleted.\n");
  sprintf(text,"%s wipes the message board.\n",name);
  if (user->level<GOD || user->vis) write_room_except(rm,text,user);
  sprintf(text,"%s wiped all messages from the board in the %s.\n",user->name,rm->name);
  write_syslog(text,1,SYSLOG);
  rm->mesg_cnt=0;
  return;
  }
if (user->wipe_from>rm->mesg_cnt) {
  sprintf(text,"There %s only %d message%s on the board.\n",PLTEXT_IS(rm->mesg_cnt),rm->mesg_cnt,PLTEXT_S(rm->mesg_cnt));
  write_user(user,text);
  return;
  }
cnt=wipe_messages(filename,user->wipe_from,user->wipe_to,0);
if (cnt==rm->mesg_cnt) {
  unlink(filename);
  sprintf(text,"There %s only %d message%s on the board, all now deleted.\n",PLTEXT_WAS(rm->mesg_cnt),rm->mesg_cnt,PLTEXT_S(rm->mesg_cnt));
  write_user(user,text);
  sprintf(text,"%s wipes the message board.\n",name);
  if (user->level<GOD || user->vis) write_room_except(rm,text,user);
  sprintf(text,"%s wiped all messages from the board in the %s.\n",user->name,rm->name);
  write_syslog(text,1,SYSLOG);
  rm->mesg_cnt=0;
  return;
  }
sprintf(text,"%d board message%s deleted.\n",cnt,PLTEXT_S(cnt));
write_user(user,text);
rm->mesg_cnt-=cnt;
sprintf(text,"%s wipes some messages from the board.\n",name);
if (user->level<GOD || user->vis) write_room_except(rm,text,user);
sprintf(text,"%s wiped %d message%s from the board in the %s.\n",user->name,cnt,PLTEXT_S(cnt),rm->name);
write_syslog(text,1,SYSLOG);
}


/* Check if a normal user can remove a message */
int check_board_wipe(UR_OBJECT user)
{
FILE *fp;
int valid,cnt,msg_number,yes,pt;
char w1[ARR_SIZE],w2[ARR_SIZE],line[ARR_SIZE],line2[ARR_SIZE],filename[80],id[ARR_SIZE],rmname[USER_NAME_LEN];
RM_OBJECT rm;

if (word_count<2) {
  write_user(user,"Usage: wipe <message #>\n");
  return 0;
  }
rm=user->room;
if (!rm->mesg_cnt) {
  write_user(user,"There are no messages on this board.\n");
  return 0;
  }
msg_number=atoi(word[1]);
if (!msg_number) {
  write_user(user,"Usage: wipe <#>\n");
  return 0;
  }
if (msg_number>rm->mesg_cnt) {
  sprintf(text,"There %s only %d message%s on the board.\n",PLTEXT_IS(rm->mesg_cnt),rm->mesg_cnt,PLTEXT_S(rm->mesg_cnt));
  write_user(user,text);
  return 0;
  }
if (rm->access==PERSONAL_LOCKED || rm->access==PERSONAL_UNLOCKED) {
  midcpy(rm->name,rmname,1,strlen(rm->name)-2);
  rmname[0]=toupper(rmname[0]);
  sprintf(filename,"%s/%s/%s.B",USERFILES,USERROOMS,rmname);
  }
else sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
if (!(fp=fopen(filename,"r"))) {
  write_user(user,"There was an error trying to read message board.\n");
  sprintf(text,"Unable to open message board for %s in check_board_wipe().\n",rm->name);
  write_syslog(text,0,SYSLOG);
  return 0;
  }
valid=1;  cnt=1;  yes=0;
id[0]='\0';  w1[0]='\0';  w2[0]='\0';
fgets(line,ARR_SIZE-1,fp);
while(!feof(fp)) {
  if (*line=='\n') valid=1;
  sscanf(line,"%s %d",id,&pt);
  if (valid && !strcmp(id,"PT:")) {
    line2[0]='\0';
    strcpy(line2,remove_first(remove_first(line)));
    sscanf(line2,"%s %s",w1,w2);
    if (msg_number==cnt) {
      /* lower case the name incase of recapping */
      strtolower(w2);
      w2[0]=toupper(w2[0]);
      if (!strcmp(w2,user->name)) {
	yes=1;
	goto SKIP; /* found result, no need to go through rest of file */
        }
      }
    valid=0;  cnt++;
    if (cnt>msg_number) goto SKIP; /* no point carrying on if checked already */
    }
  fgets(line,ARR_SIZE-1,fp);
  }
SKIP:
fclose(fp);
if (!yes) {
  write_user(user,"You did not post that message.  Use ~FTbfrom~RS to check the number again.\n");
  return 0;
  }
user->wipe_from=msg_number;
user->wipe_to=msg_number;
return 1;
}


/*** Show list of people board posts are from without seeing the whole lot ***/
void board_from(UR_OBJECT user)
{
FILE *fp;
int cnt;
char id[ARR_SIZE],line[ARR_SIZE],filename[80],rmname[USER_NAME_LEN];
RM_OBJECT rm;

if (word_count<2) rm=user->room;
else {
  if ((rm=get_room(word[1]))==NULL) {
    write_user(user,nosuchroom);  return;
    }
  if (!has_room_access(user,rm)) {
    write_user(user,"That room is currently private, you cannot read the board.\n");
    return;
    }
  }	
if (!rm->mesg_cnt) {
  write_user(user,"That room has no messages on it's board.\n");
  return;
  }
if (rm->access==PERSONAL_LOCKED || rm->access==PERSONAL_UNLOCKED) {
  midcpy(rm->name,rmname,1,strlen(rm->name)-2);
  rmname[0]=toupper(rmname[0]);
  sprintf(filename,"%s/%s/%s.B",USERFILES,USERROOMS,rmname);
  }
else sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
if (!(fp=fopen(filename,"r"))) {
  write_user(user,"There was an error trying to read message board.\n");
  sprintf(text,"Unable to open message board for %s in board_from().\n",rm->name);
  write_syslog(text,0,SYSLOG);
  return;
  }
sprintf(text,"\n~FG~BB*** Posts on the %s message board from ***\n\n",rm->name);
write_user(user,text);
cnt=0;  line[0]='\0';
fgets(line,ARR_SIZE-1,fp);
while(!feof(fp)) {
  sscanf(line,"%s",id);
  if (!strcmp(id,"PT:")) {
    cnt++;
    sprintf(text,"~FT%2d)~RS %s",cnt,remove_first(remove_first(remove_first(line))));
    write_user(user,text);
    }
  line[0]='\0';
  fgets(line,ARR_SIZE-1,fp);
  }
fclose(fp);
sprintf(text,"\nTotal of ~OL%d~RS messages.\n\n",rm->mesg_cnt);
write_user(user,text);
}


/*** Search all the boards for the words given in the list. Rooms fixed to
     private will be ignore if the users level is less than gatecrash_level ***/
void search_boards(UR_OBJECT user)
{
RM_OBJECT rm;
FILE *fp;
char filename[80],line[82],buff[(MAX_LINES+1)*82],w1[81],rmname[USER_NAME_LEN];
int w,cnt,message,yes,room_given;

if (word_count<2) {
  write_user(user,"Usage: search <word list>\n");  return;
  }
/* Go through rooms */
cnt=0;
for(rm=room_first;rm!=NULL;rm=rm->next) {
  if (rm->access==PERSONAL_LOCKED || rm->access==PERSONAL_UNLOCKED) {
    sscanf(rm->name,"%s",rmname);
    rmname[0]=toupper(rmname[0]);
    sprintf(filename,"%s/%s/%s.B",USERFILES,USERROOMS,rmname);
    }
  else sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
  if (!(fp=fopen(filename,"r"))) continue;
  if (!has_room_access(user,rm)) {  fclose(fp);  continue;  }
  /* Go through file */
  fgets(line,81,fp);
  yes=0;  message=0;  
  room_given=0;  buff[0]='\0';
  while(!feof(fp)) {
    if (*line=='\n') {
      if (yes) {  strcat(buff,"\n");  write_user(user,buff);  }
      message=0;  yes=0;  buff[0]='\0';
      }
    if (!message) {
      w1[0]='\0';  
      sscanf(line,"%s",w1);
      if (!strcmp(w1,"PT:")) {  
	message=1;  
	strcpy(buff,remove_first(remove_first(line)));
        }
      }
    else strcat(buff,line);
    for(w=1;w<word_count;++w) {
      if (!yes && strstr(line,word[w])) {  
	if (!room_given) {
	  sprintf(text,"~BB*** %s ***\n\n",rm->name);
	  write_user(user,text);
	  room_given=1;
	  }
	yes=1;  cnt++;  
        }
      }
    fgets(line,81,fp);
    }
  if (yes) {  strcat(buff,"\n");  write_user(user,buff);  }
  fclose(fp);
  }
if (cnt) {
  sprintf(text,"Total of %d matching message%s.\n\n",cnt,PLTEXT_S(cnt));
  write_user(user,text);
  }
else write_user(user,"No occurences found.\n");
}


/** Write a suggestion to the board, or read if if you can **/
void suggestions(UR_OBJECT user, int done_editing)
{
FILE *fp;
char filename[30],*c;
int cnt=0;

if (com_num==RSUG) {
  sprintf(filename,"%s/%s",MISCFILES,SUGBOARD);
  write_user(user,"~BB~FG*** The Suggestions board has the following ideas ***\n\n");
  switch(more(user,user->socket,filename)) {
    case 0: write_user(user,"There are no suggestions.\n\n");  break;
    case 1: user->misc_op=2;
    }
  return;
  }
if (user->type==REMOTE_TYPE) {
  write_user(user,"Remote users cannot use this command, sorry!\n");
  return;
  }
if (!done_editing) {
  write_user(user,"~BB~FG*** Writing a suggestion ***\n\n");
  user->misc_op=8;
  editor(user,NULL);
  return;
  }
sprintf(filename,"%s/%s",MISCFILES,SUGBOARD);
if (!(fp=fopen(filename,"a"))) {
  sprintf(text,"%s: couldn't add suggestion.\n",syserror);
  write_user(user,text);
  sprintf("ERROR: Couldn't open file %s to write in suggestions().\n",filename);
  write_syslog(text,0,SYSLOG);
  return;
  }
sprintf(text,"~OLFrom: %s  %s\n",user->recap,long_date(0));
fputs(text,fp);
c=user->malloc_start;
while(c!=user->malloc_end) {
  putc(*c++,fp);
  if (*c=='\n') cnt=0; else ++cnt;
  if (cnt==80) { putc('\n',fp); cnt=0; }
  }
fprintf(fp,"\n");
fclose(fp);
write_user(user,"Suggestion written.  Thank you for your contribution.\n");
suggestion_count++;
}


/** delete suggestions from the board **/
void delete_suggestions(UR_OBJECT user)
{
int cnt;
char filename[80];

if (word_count<2) {
  write_user(user,"Usage: dsug all\n");
  write_user(user,"Usage: dsug <#>\n");
  write_user(user,"Usage: dsug to <#>\n");
  write_user(user,"Usage: dsug from <#> to <#>\n");
  return;
  }
if (get_wipe_parameters(user)==-1) return;
sprintf(filename,"%s/%s",MISCFILES,SUGBOARD);
if (!suggestion_count) {
  write_user(user,"There are no suggestions to delete.\n");  return;
  }
if (user->wipe_from==-1) {
  unlink(filename);
  write_user(user,"All suggestions deleted.\n");
  sprintf(text,"%s wiped all suggestions from the %s board\n",user->name,SUGBOARD);
  write_syslog(text,1,SYSLOG);
  suggestion_count=0;
  return;
  }
if (user->wipe_from>suggestion_count) {
  sprintf(text,"There %s only %d suggestion%s on the board.\n",PLTEXT_IS(suggestion_count),suggestion_count,PLTEXT_S(suggestion_count));
  write_user(user,text);
  return;
  }
cnt=wipe_messages(filename,user->wipe_from,user->wipe_to,0);
if (cnt==suggestion_count) {
  unlink(filename);
  sprintf(text,"There %s only %d suggestion%s on the board, all now deleted.\n",PLTEXT_WAS(cnt),cnt,PLTEXT_S(cnt));
  write_user(user,text);
  sprintf(text,"%s wiped all suggestions from the %s board\n",user->name,SUGBOARD);
  write_syslog(text,1,SYSLOG);
  suggestion_count=0;
  return;
  }
sprintf(text,"%d suggestion%s deleted.\n",cnt,PLTEXT_S(cnt));
write_user(user,text);
suggestion_count-=cnt;
sprintf(text,"%s wiped %d suggestion%s from the %s board\n",user->name,cnt,PLTEXT_S(cnt),SUGBOARD);
write_syslog(text,1,SYSLOG);
}


/*** Show list of people suggestions are from without seeing the whole lot ***/
void suggestions_from(UR_OBJECT user)
{
FILE *fp;
int cnt;
char id[ARR_SIZE],line[ARR_SIZE],filename[80],*str;

if (!suggestion_count) {
  write_user(user,"There are currently no suggestions.\n");
  return;
  }
sprintf(filename,"%s/%s",MISCFILES,SUGBOARD);
if (!(fp=fopen(filename,"r"))) {
  write_user(user,"There was an error trying to read the suggestion board.\n");
  sprintf(text,"Unable to open suggestion board in suggestions_from().\n");
  write_syslog(text,0,SYSLOG);
  return;
  }
sprintf(text,"\n~BB*** Suggestions on the %s board from ***\n\n",SUGBOARD);
write_user(user,text);
cnt=0;  line[0]='\0';
fgets(line,ARR_SIZE-1,fp);
while(!feof(fp)) {
  sscanf(line,"%s",id);
  str=colour_com_strip(id);
  if (!strcmp(str,"From:")) {
    cnt++;
    sprintf(text,"~FT%2d)~RS %s",cnt,remove_first(line));
    write_user(user,text);
    }
  line[0]='\0';
  fgets(line,ARR_SIZE-1,fp);
  }
fclose(fp);
sprintf(text,"\nTotal of ~OL%d~RS suggestions.\n\n",suggestion_count);
write_user(user,text);
}


/* delete lines from boards or mail or suggestions, etc */
int get_wipe_parameters(UR_OBJECT user)
{
int retval=-1;
/* get delete paramters */
strtolower(word[1]);
if (!strcmp(word[1],"all")) {
  user->wipe_from=-1; user->wipe_to=-1;
  }
else if (!strcmp(word[1],"from")) {
  if (word_count<4 || strcmp(word[3],"to")) {
    write_user(user,"Usage: <command> from <#> to <#>\n");
    return(retval);
    }
  user->wipe_from=atoi(word[2]);
  user->wipe_to=atoi(word[4]);
  }
else if (!strcmp(word[1],"to")) {
  if (word_count<2) {
    write_user(user,"Usage: <command> to <#>\n");
    return(retval);
    }
  user->wipe_from=0;
  user->wipe_to=atoi(word[2]);
  }
else {
  user->wipe_from=atoi(word[1]);
  user->wipe_to=atoi(word[1]);
  }
if (user->wipe_from>user->wipe_to) {
  write_user(user,"The first number must be smaller than the second number.\n");
  return(retval);
  }
retval=1;
return(retval);
}


/* removes messages from one of the board types - room boards, smail, suggestions,
  etc.  It works on the premise that every message is seperated by a newline on a
  line by itself.  And as all messages have this form - no probs :)  Just don't go
  screwing with how the messages are stored and you'll be ok :P
  from = message to start deleting at
  to = message to stop deleting at (both inclusive)
  type = 1 then board is mail, otherwise any other board
  */
int wipe_messages(char *filename, int from, int to, int type)
{
FILE *fpi,*fpo;
char line[ARR_SIZE];
int rem,i,tmp1,tmp2;

rem=0;  line[0]='\0';
if (!(fpi=fopen(filename,"r"))) {
  return 0; /* return on no messages to delete */
  }
if (!(fpo=fopen("tempfile","w"))) {
  write_syslog("ERROR: Couldn't open tempfile in wipe_message().\n",0,SYSLOG);
  fclose(fpo);
  return -1; /* return on error */
  }
/* if type is mail */
if (type==1) {
  fscanf(fpi,"%d %d\r",&tmp1,&tmp2);
  fprintf(fpo,"%d %d\r",tmp1,tmp2);
  }
i=1;
while (i<from) {
  fgets(line,ARR_SIZE-1,fpi);
  while(*line!='\n') {
    if (feof(fpi)) goto SKIP_WIPE;
    fputs(line,fpo);
    fgets(line,ARR_SIZE-1,fpi);
    }
  fputs(line,fpo);
  /* message ended */
  i++;
  }
while (i<=to) {
  fgets(line,ARR_SIZE-1,fpi);
  if (feof(fpi)) goto SKIP_WIPE;
  while(*line!='\n') fgets(line,ARR_SIZE-1,fpi);
  rem++;  i++;
  }
fgets(line,ARR_SIZE-1,fpi);
while(!feof(fpi)) {
  fputs(line,fpo);
  if (*line=='\n') i++;
  fgets(line,ARR_SIZE-1,fpi);
  }
SKIP_WIPE:
fclose(fpi);
fclose(fpo);
unlink(filename);
rename("tempfile",filename);
return rem;
}


/***** Bans *****/


/*** List banned sites or users ***/
void listbans(UR_OBJECT user)
{
int i;
char filename[80];

strtolower(word[1]);
if (!strcmp(word[1],"sites")) {
  write_user(user,"\n~BB*** Banned sites/domains ***\n\n"); 
  sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
  switch(more(user,user->socket,filename)) {
    case 0:
      write_user(user,"There are no banned sites/domains.\n\n");
      return;
    case 1: user->misc_op=2;
    }
  return;
  }
if (!strcmp(word[1],"users")) {
  write_user(user,"\n~BB*** Banned users ***\n\n");
  sprintf(filename,"%s/%s",DATAFILES,USERBAN);
  switch(more(user,user->socket,filename)) {
    case 0:
      write_user(user,"There are no banned users.\n\n");
      return;
    case 1: user->misc_op=2;
    }
  return;
  }
if (!strcmp(word[1],"swears")) {
  write_user(user,"\n~BB*** Banned swear words ***\n\n");
  i=0;
  while(swear_words[i][0]!='*') {
    write_user(user,swear_words[i]);
    write_user(user,"\n");
    ++i;
    }
  if (!i) write_user(user,"There are no banned swear words.\n");
  if (ban_swearing) write_user(user,"\n");
  else write_user(user,"\n(Swearing ban is currently off)\n\n");
  return;
  }
if (strstr(word[1],"new")) {
  write_user(user,"\n~BB*** New users banned from sites/domains **\n\n");
  sprintf(filename,"%s/%s",DATAFILES,NEWBAN);
  switch(more(user,user->socket,filename)) {
    case 0:
      write_user(user,"There are no sites/domains where new users have been banned.\n\n");
      return;
    case 1: user->misc_op=2;
    }
  return;
  }
write_user(user,"Usage: lban sites/users/new/swears\n"); 
}


/*** Ban a site/domain or user ***/
void ban(UR_OBJECT user)
{
char *usage="Usage: ban site/user/new <site/user name/site>\n";

if (word_count<3) {
  write_user(user,usage);  return;
  }
strtolower(word[1]);
if (!strcmp(word[1],"site")) {  ban_site(user);  return;  }
if (!strcmp(word[1],"user")) {  ban_user(user);  return;  }
if (!strcmp(word[1],"new"))  {  ban_new(user);   return;  }
write_user(user,usage);
}


/*** Ban a site from logging onto the talker ***/
void ban_site(UR_OBJECT user)
{
FILE *fp;
char filename[80],host[81],bsite[80],*check;

gethostname(host,80);
/* check if full name matches the host's name */
if (!strcmp(word[2],host)) {
  write_user(user,"You cannot ban the machine that this program is running on.\n");
  return;
  }
/* check for variations of wild card */
if (!strcmp("*",word[2])) {
  write_user(user,"You cannot ban site '*'.\n");
  return;
  }
if (strstr(word[2],"**")) {
  write_user(user,"You cannot have ** in your site to ban.\n");
  return;
  }
if (strstr(word[2],"?*")) {
  write_user(user,"You cannot have ?* in your site to ban.\n");
  return;
  }
if (strstr(word[2],"*?")) {
  write_user(user,"You cannot have *? in your site to ban.\n");
  return;
  }
/* check if, with the wild cards, the name matches host's name */
check=word[2];
if (check[strlen(check)-1]!='*') strcat(check,"*");
if (pattern_match(host,check)) {
  write_user(user,"You cannot ban the machine that that program is running on.\n");
  return;
  }
sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
/* See if ban already set for given site */
if ((fp=fopen(filename,"r"))) {
  fscanf(fp,"%s",bsite);
  while(!feof(fp)) {
    if (!strcmp(bsite,word[2])) {
      write_user(user,"That site/domain is already banned.\n");
      fclose(fp);  return;
      }
    fscanf(fp,"%s",bsite);
    }
  fclose(fp);
  }
/* Write new ban to file */
if (!(fp=fopen(filename,"a"))) {
  sprintf(text,"%s: Can't open file to append.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Couldn't open file to append in ban_site().\n",0,SYSLOG);
  return;
  }
fprintf(fp,"%s\n",word[2]);
fclose(fp);
write_user(user,"Site/domain banned.\n");
sprintf(text,"%s BANNED site/domain %s.\n",user->name,word[2]);
write_syslog(text,1,SYSLOG);
}


/*** Ban an individual user from logging onto the talker ***/
void ban_user(UR_OBJECT user)
{
UR_OBJECT u;
FILE *fp;
char filename[80],name[USER_NAME_LEN+1];
struct user_dir_struct *entry;

word[2][0]=toupper(word[2][0]);
if (!strcmp(user->name,word[2])) {
  write_user(user,"Trying to ban yourself is the seventh sign of madness.\n");
  return;
  }
/* See if ban already set for given user */
sprintf(filename,"%s/%s",DATAFILES,USERBAN);
if ((fp=fopen(filename,"r"))) {
  fscanf(fp,"%s",name);
  while(!feof(fp)) {
    if (!strcmp(name,word[2])) {
      write_user(user,"That user is already banned.\n");
      fclose(fp);  return;
      }
    fscanf(fp,"%s",name);
    }
  fclose(fp);
  }
/* See if already on */
if ((u=get_user(word[2]))!=NULL) {
  if (u->level>=user->level) {
    write_user(user,"You cannot ban a user of equal or higher level than yourself.\n");
    return;
    }
  }
else {
  for (entry=first_dir_entry;entry!=NULL;entry=entry->next)
    if (!strcmp(entry->name,word[2])) break;
  if (entry==NULL) {
    write_user(user,nosuchuser);  return;
    }
  if (entry->level>=user->level) {
    write_user(user,"You cannot ban a user of equal or higher level than yourself.\n");
    return;
    }
  }
/* Write new ban to file */
if (!(fp=fopen(filename,"a"))) {
  sprintf(text,"%s: Can't open file to append.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Couldn't open file to append in ban_user().\n",0,SYSLOG);
  return;
  }
fprintf(fp,"%s\n",word[2]);
fclose(fp);
write_user(user,"User banned.\n");
sprintf(text,"%s BANNED user %s.\n",user->name,word[2]);
write_syslog(text,1,SYSLOG);
sprintf(text,"User's name was ~FRbanned~RS by %s.\n",user->name);
add_history(word[2],1,text); 
if (u!=NULL) {
  write_user(u,"\n\07~FR~OL~LIYou have been banished from here and banned from returning.\n\n");
  disconnect_user(u);
  }
}


/* Ban any new accounts from a given site */
void ban_new(UR_OBJECT user)
{
FILE *fp;
char filename[80],host[81],bsite[80],*check;

gethostname(host,80);
/* check if full name matches the host's name */
if (!strcmp(word[2],host)) {
  write_user(user,"You cannot ban the machine that this program is running on.\n");
  return;
  }
/* check for variations of wild card */
if (!strcmp("*",word[2])) {
  write_user(user,"You cannot ban site '*'.\n");
  return;
  }
if (strstr(word[2],"**")) {
  write_user(user,"You cannot have ** in your site to ban.\n");
  return;
  }
if (strstr(word[2],"?*")) {
  write_user(user,"You cannot have ?* in your site to ban.\n");
  return;
  }
if (strstr(word[2],"*?")) {
  write_user(user,"You cannot have *? in your site to ban.\n");
  return;
  }
/* check if, with the wild cards, the name matches host's name */
check=word[2];
if (check[strlen(check)-1]!='*') strcat(check,"*");
if (pattern_match(host,check)) {
  write_user(user,"You cannot ban the machine that that program is running on.\n");
  return;
  }
sprintf(filename,"%s/%s",DATAFILES,NEWBAN);
/* See if ban already set for given site */
if ((fp=fopen(filename,"r"))) {
  fscanf(fp,"%s",bsite);
  while(!feof(fp)) {
    if (!strcmp(bsite,word[2])) {
      write_user(user,"New users from that site/domain have already been banned.\n");
      fclose(fp);  return;
      }
    fscanf(fp,"%s",bsite);
    }
  fclose(fp);
  }
/* Write new ban to file */
if (!(fp=fopen(filename,"a"))) {
  sprintf(text,"%s: Can't open file to append.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Couldn't open file to append in ban_new().\n",0,SYSLOG);
  return;
  }
fprintf(fp,"%s\n",word[2]);
fclose(fp);
write_user(user,"New users from site/domain banned.\n");
sprintf(text,"%s BANNED new users from site/domain %s.\n",user->name,word[2]);
write_syslog(text,1,SYSLOG);
}


/*** uban a site (or domain) or user ***/
void unban(UR_OBJECT user)
{
char *usage="Usage: unban site/user/new <site/user name/site>\n";

if (word_count<3) {
  write_user(user,usage);  return;
  }
strtolower(word[1]);
if (!strcmp(word[1],"site")) {  unban_site(user);  return;  }
if (!strcmp(word[1],"user")) {  unban_user(user);  return;  }
if (!strcmp(word[1],"new")) {  unban_new(user);   return;  }
write_user(user,usage);
}


/*** remove a ban for a whole site ***/
void unban_site(UR_OBJECT user)
{
FILE *infp,*outfp;
char filename[80],ubsite[80];
int found,cnt;

sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
if (!(infp=fopen(filename,"r"))) {
  write_user(user,"That site/domain is not currently banned.\n");
  return;
  }
if (!(outfp=fopen("tempfile","w"))) {
  sprintf(text,"%s: Couldn't open tempfile.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Couldn't open tempfile to write in unban_site().\n",0,SYSLOG);
  fclose(infp);
  return;
  }
found=0;   cnt=0;
fscanf(infp,"%s",ubsite);
while(!feof(infp)) {
  if (strcmp(word[2],ubsite)) {  
    fprintf(outfp,"%s\n",ubsite);  cnt++;  
    }
  else found=1;
  fscanf(infp,"%s",ubsite);
  }
fclose(infp);
fclose(outfp);
if (!found) {
  write_user(user,"That site/domain is not currently banned.\n");
  unlink("tempfile");
  return;
  }
if (!cnt) {
  unlink(filename);  unlink("tempfile");
  }
else rename("tempfile",filename);
write_user(user,"Site ban removed.\n");
sprintf(text,"%s UNBANNED site %s.\n",user->name,word[2]);
write_syslog(text,1,SYSLOG);
}


/*** unban a user from logging onto the talker ***/
void unban_user(UR_OBJECT user)
{
FILE *infp,*outfp;
char filename[80],name[USER_NAME_LEN+1];
int found,cnt;

sprintf(filename,"%s/%s",DATAFILES,USERBAN);
if (!(infp=fopen(filename,"r"))) {
  write_user(user,"That user is not currently banned.\n");
  return;
  }
if (!(outfp=fopen("tempfile","w"))) {
  sprintf(text,"%s: Couldn't open tempfile.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Couldn't open tempfile to write in unban_user().\n",0,SYSLOG);
  fclose(infp);
  return;
  }
found=0;  cnt=0;
word[2][0]=toupper(word[2][0]);
fscanf(infp,"%s",name);
while(!feof(infp)) {
  if (strcmp(word[2],name)) {
    fprintf(outfp,"%s\n",name);  cnt++;
    }
  else found=1;
  fscanf(infp,"%s",name);
  }
fclose(infp);
fclose(outfp);
if (!found) {
  write_user(user,"That user is not currently banned.\n");
  unlink("tempfile");
  return;
  }
if (!cnt) {
  unlink(filename);  unlink("tempfile");
  }
else rename("tempfile",filename);
write_user(user,"User ban removed.\n");
sprintf(text,"%s UNBANNED user %s.\n",user->name,word[2]);
write_syslog(text,1,SYSLOG);
sprintf(text,"User's name was ~FGunbanned~RS by %s.\n",user->name);
add_history(word[2],0,text);
}


/* unban new accounts from a given site */
void unban_new(UR_OBJECT user)
{
FILE *infp,*outfp;
char filename[80],ubsite[80];
int found,cnt;

sprintf(filename,"%s/%s",DATAFILES,NEWBAN);
if (!(infp=fopen(filename,"r"))) {
  write_user(user,"New users from that site/domain are not currently banned.\n");
  return;
  }
if (!(outfp=fopen("tempfile","w"))) {
  sprintf(text,"%s: Couldn't open tempfile.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Couldn't open tempfile to write in unban_new().\n",0,SYSLOG);
  fclose(infp);
  return;
  }
found=0;   cnt=0;
fscanf(infp,"%s",ubsite);
while(!feof(infp)) {
  if (strcmp(word[2],ubsite)) {  
    fprintf(outfp,"%s\n",ubsite);  cnt++;  
    }
  else found=1;
  fscanf(infp,"%s",ubsite);
  }
fclose(infp);
fclose(outfp);
if (!found) {
  write_user(user,"New users from that site/domain are not currently banned.\n");
  unlink("tempfile");
  return;
  }
if (!cnt) {
  unlink(filename);  unlink("tempfile");
  }
else rename("tempfile",filename);
write_user(user,"New users from site ban removed.\n");
sprintf(text,"%s UNBANNED new users from site %s.\n",user->name,word[2]);
write_syslog(text,1,SYSLOG);
}


/*** Everything else ;) ***/


/*** Display details of room ***/
void look(UR_OBJECT user)
{
RM_OBJECT rm;
UR_OBJECT u;
char temp[81],null[1],*ptr;
char *uafk="~BR(AFK)";
int i,exits,users;

rm=user->room;
if (rm->access & PRIVATE) sprintf(text,"\n~FTRoom: ~FR%s\n\n",rm->name);
else sprintf(text,"\n~FTRoom: ~FG%s\n\n",rm->name);
write_user(user,text);
if (user->show_rdesc) write_user(user,user->room->desc);
exits=0;  null[0]='\0';
strcpy(text,"\n~FTExits are:");
for(i=0;i<MAX_LINKS;++i) {
  if (rm->link[i]==NULL) break;
  if (rm->link[i]->access & PRIVATE) sprintf(temp,"  ~FR%s",rm->link[i]->name);
  else sprintf(temp,"  ~FG%s",rm->link[i]->name);
  strcat(text,temp);
  ++exits;
  }
#ifdef NETLINKS
if (rm->netlink!=NULL && rm->netlink->stage==UP) {
  if (rm->netlink->allow==IN) sprintf(temp,"  ~FR%s*",rm->netlink->service);
  else sprintf(temp,"  ~FG%s*",rm->netlink->service);
  strcat(text,temp);
  }
else 
#endif
  if (!exits) strcpy(text,"\n~FTThere are no exits.");
strcat(text,"\n\n");
write_user(user,text);
users=0;
for(u=user_first;u!=NULL;u=u->next) {
  if (u->room!=rm || u==user || (!u->vis && u->level>user->level)) continue;
  if (!users++) write_user(user,"~FG~OLYou can see:\n");
  if (u->afk) ptr=uafk; else ptr=null;
  if (!u->vis) sprintf(text,"     ~FR*~RS%s %s~RS  %s\n",u->recap,u->desc,ptr);
  else sprintf(text,"      %s %s~RS  %s\n",u->recap,u->desc,ptr);
  write_user(user,text);
  }
if (!users) write_user(user,"~FGYou are all alone here.\n");
write_user(user,"\n");
strcpy(text,"Access is ");
switch(rm->access) {
  case PUBLIC:  strcat(text,"set to ~FGPUBLIC~RS");  break;
  case PRIVATE: strcat(text,"set to ~FRPRIVATE~RS");  break;
  case FIXED_PUBLIC:  strcat(text,"~FRfixed~RS to ~FGPUBLIC~RS");  break;
  case FIXED_PRIVATE: strcat(text,"~FRfixed~RS to ~FRPRIVATE~RS");  break;
  case PERSONAL_UNLOCKED: strcat(text,"personal ~FG(unlocked)~RS");  break;
  case PERSONAL_LOCKED: strcat(text,"personal ~FR(locked)~RS");  break;
  }
sprintf(temp," and there %s ~OL~FM%d~RS message%s on the board.\n",PLTEXT_IS(rm->mesg_cnt),rm->mesg_cnt,PLTEXT_S(rm->mesg_cnt));
strcat(text,temp);
write_user(user,text);
if (rm->topic[0]) {
  sprintf(text,"~FG~OLCurrent topic:~RS %s\n",rm->topic);
  write_user(user,text);
  return;
  }
write_user(user,"~FGNo topic has been set yet.\n");	
}


/*** Show who is on.  type 0=who, 1=fwho, 2=people ***/
void who(UR_OBJECT user, int type)
{
UR_OBJECT u;
int cnt,total,invis,mins,idle,logins,friend;
char line[USER_NAME_LEN+USER_DESC_LEN*2];
char rname[ROOM_NAME_LEN+1],portstr[5],idlestr[20],sockstr[3];
#ifdef NETLINKS
 int remote=0;
#endif 

total=0;  invis=0;  logins=0;  friend=0;

if ((type==2) && !strcmp(word[1],"key")) {
  write_user(user,"\n+----------------------------------------------------------------------------+\n");
  write_user(user,"| ~OL~FTUser login stages are as follows~RS                                           |\n");
  write_user(user,"+----------------------------------------------------------------------------+\n");
  sprintf(text,"| ~FY~OLStage %d :~RS The user has logged onto the port and is entering their name     |\n",LOGIN_NAME);
  write_user(user,text);
  sprintf(text,"| ~FY~OLStage %d :~RS The user is entering their password for the first time           |\n",LOGIN_PASSWD);
  write_user(user,text);
  sprintf(text,"| ~FY~OLStage %d :~RS The user is new and has been asked to confirm their password     |\n",LOGIN_CONFIRM);
  write_user(user,text);
  sprintf(text,"| ~FY~OLStage %d :~RS The user has entered the pre-login information prompt            |\n",LOGIN_PROMPT);
  write_user(user,text);
  write_user(user,"+----------------------------------------------------------------------------+\n\n");
  return;
  }
write_user(user,"\n+----------------------------------------------------------------------------+\n");
if (user->login) sprintf(text,"Current users %s\n",long_date(1));
else sprintf(text,"~FGCurrent users %s\n",long_date(1));
write_user(user,text);
switch(type) {
 case 0:
   if (user->login) write_user(user,"Name                                             : Room            : Tm/Id\n");
   else write_user(user,"~FTName                                             : Room            : Tm/Id\n");
   break;
 case 1:
   write_user(user,"~FTFriend                                           : Room            : Tm/Id\n");
   break;
 case 2:
   write_user(user,"~FTName            : Level Line Ignall Visi Idle Mins  Port  Site/Service\n");
   break;
  }
write_user(user,"+----------------------------------------------------------------------------+\n\n");
for(u=user_first;u!=NULL;u=u->next) {
  if (u->type==CLONE_TYPE) continue;
  mins=(int)(time(0) - u->last_login)/60;
  idle=(int)(time(0) - u->last_input)/60;
#ifdef NETLINKS
  if (u->type==REMOTE_TYPE) strcpy(portstr,"   @");
  else {
#endif
    if (u->port==port[0]) strcpy(portstr,"MAIN");
    else strcpy(portstr," WIZ");
#ifdef NETLINKS
    }
#endif
  if (u->login) {
    if (type!=2) continue;
    sprintf(text,"~FY[Login stage %d]~RS :  -      %2d   -    -  %4d    -  %s  %s:%d\n",u->login,u->socket,idle,portstr,u->site,u->site_port);
    write_user(user,text);
    logins++;
    continue;
    }
  if ((type==1) && !user_is_friend(user,u)) continue;
  ++total;
#ifdef NETLINKS
  if (u->type==REMOTE_TYPE) ++remote;
#endif
  if (!u->vis) { 
    ++invis;  
    if (u->level>user->level && !(user->level>=ARCH)) continue;  
    }
  if (type==2) {
    if (u->afk) strcpy(idlestr," ~FRAFK~RS");
    else if (u->editing) strcpy(idlestr,"~FTEDIT~RS");
    else sprintf(idlestr,"%4d",idle);
#ifdef NETLINKS
    if (u->type==REMOTE_TYPE) strcpy(sockstr," @");
    else
#endif
      sprintf(sockstr,"%2d",u->socket);
    sprintf(text,"%-15s : %-5.5s   %s  %s  %s %s %4d  %s  %s\n",u->name,level_name[u->level],sockstr,noyes1[u->ignall],noyes1[u->vis],idlestr,mins,portstr,u->site);
    write_user(user,text);
    continue;
    }
  sprintf(line,"  %s %s",u->recap,u->desc);
  if (!u->vis) line[0]='*';
#ifdef NETLINKS
  if (u->type==REMOTE_TYPE) line[1]='@';
  if (u->room==NULL) sprintf(rname,"@%s",u->netlink->service);
  else 
#endif
    strcpy(rname,u->room->name);
  /* Count number of colour coms to be taken account of when formatting */
  cnt=colour_com_count(line);
  if (u->afk) strcpy(idlestr,"~FRAFK~RS");
  else if (u->editing) strcpy(idlestr,"~FTEDIT~RS");
  else sprintf(idlestr,"%d/%d",mins,idle);
  sprintf(text,"%-*.*s~RS   %s : %-15.15s : %s\n",44+cnt*3,44+cnt*3,line,level_alias[u->level],rname,idlestr);
  write_user(user,text);
  }
switch(type) {
 case 0:
 case 2:
   #ifdef NETLINKS
     sprintf(text,"\nThere %s %d visible, %d invisible, %d remote users.\nTotal of %d user%s",
	     PLTEXT_IS(num_of_users-invis),num_of_users-invis,invis,remote,total,PLTEXT_S(total));
     if (type==2) sprintf(text,"%s and %d login%s.\n\n",text,logins,PLTEXT_S(logins));
     else strcat(text,".\n\n");
   #else
     sprintf(text,"\nThere %s %d visible, %d invisible.\nTotal of %d user%s",
	     PLTEXT_IS(num_of_users-invis),num_of_users-invis,invis,total,PLTEXT_S(total));
     if (type==2) sprintf(text,"%s and %d login%s.\n\n",text,logins,PLTEXT_S(logins));
     else strcat(text,".\n\n");
   #endif
   break;
 case 1:
   sprintf(text,"\nThere %s %d friend%s visible, %d invisible.\n",PLTEXT_IS(total-invis),total-invis,PLTEXT_S(total-invis),invis);
   sprintf(text,"%sTotal of %d friend%s.\n\n",text,total,PLTEXT_S(total));
   break;
 }
write_user(user,text);
}


/*** Do the help ***/
void help(UR_OBJECT user)
{
int i,ret,comnum,found;
char filename[80],text2[ARR_SIZE];
char *c;

if (word_count<2 || !strcmp(word[1],"commands")) {
  switch(user->cmd_type) {
    case 0: help_commands_level(user,0);  break;
    case 1: help_commands_function(user,0);  break;
    }
  return;
  }
/***
    can't access these at the moment due to the new system of abreviating
    and matching command names..  Don't think anyone used it anyway!
***
if (!strcmp(word[1],"files")) {
  sprintf(filename,"%s/helpfiles",HELPFILES);
  if (!(ret=more(user,user->socket,filename))) {
    write_user(user,"There is no list of help files at the moment.\n");
    return;
    }
  if (ret==1) user->misc_op=2;
  return;
  }
***/
if (!strcmp(word[1],"credits")) {  help_credits(user);  return;  }

/* Check for any illegal crap in searched for filename so they cannot list 
   out the /etc/passwd file for instance. */
c=word[1];
while(*c) {
  if (*c=='.' || *c++=='/') {
    write_user(user,"Sorry, there is no help on that topic.\n");
    return;
    }
  }
/* do full string match first */
found=0;  i=0;  comnum=-1;
text[0]=0;
while(command_table[i].name[0]!='*') {
  if (!strcmp(command_table[i].name,word[1])) {
    comnum=i;  found=1;
    break;
    }
  ++i;
  }
/* if full string wasn't found, try to match partial string to a command */
if (!found) {
  i=0;
  while(command_table[i].name[0]!='*') {
    if (!strncmp(command_table[i].name,word[1],strlen(word[1])) && user->level>=command_table[i].level) {
      if (!found) strcat(text,"   ~OL");
      else if (!(found%8)) strcat(text,"\n   ~OL");
      strcat(text,command_table[i].name);
      strcat(text, "  ");
      comnum=i;  ++found;
      }
    ++i;
    }
  if (found%8) strcat(text,"\n\n");
  else strcat(text,"\n");
  /* if more than one command matched */
  if (found>1) {
    text2[0]='\0';
    sprintf(text2, "~FR~OLCommand name is not unique. '~FT%s~RS~OL~FR' also matches:\n\n",word[1]);
    write_user(user,text2);
    write_user(user,text);
    return;
    }
  /* nothing was found still? */
  if (!found) {
    write_user(user,"Sorry, there is no help on that topic.\n");
    return;
    }
  } /* end if !found */
sprintf(filename,"%s/%s",HELPFILES,command_table[comnum].name);
if (!(ret=more(user,user->socket,filename)))
  write_user(user,"Sorry, there is no help on that topic.\n");
if (ret==1) user->misc_op=2;
if (ret==2) {
  sprintf(text,"~OLFor Lev :~RS %s and above\n\n",level_name[command_table[comnum].level]);
  write_user(user,text);
  }
}


/*** Show the command available listed by level ***/
void help_commands_level(UR_OBJECT user, int is_wrap)
{
int cnt,lev,total,lines;
char text2[ARR_SIZE],temp[20],temp1[20];
struct command_struct *cmd;

if (!is_wrap) {
  /* write out the header */
  write_user(user,"\n+----------------------------------------------------------------------------+\n");
  write_user(user,"| All commands start with a '.' (when in ~FYspeech~RS mode) and can be abbreviated |\n");
  write_user(user,"| Remember, a '.' by itself will repeat your last command or speech          |\n");
  write_user(user,"+----------------------------------------------------------------------------+\n");
  sprintf(text,"~FT~OLCommands available for level ~RS~OL%s~RS",level_name[user->level]);
  sprintf(text2,"| %-89s |\n",text);
  write_user(user,text2);
  write_user(user,"+----------------------------------------------------------------------------+\n\n");
  /* set some defaults */
  cmd=first_command;
  user->hwrap_lev=JAILED;
  user->hwrap_id=first_command->id;
  user->hwrap_same=0;
  }
/* if we've gone through a screen wrap then find which node we're on */
if (is_wrap) {
  cmd=first_command;
  while (cmd!=NULL) {
    if (cmd->id==user->hwrap_id) break;
    cmd=cmd->next;
    }
  }
/* scroll trough all the commands listing by level */
total=0;  lines=0;
for(lev=user->hwrap_lev;lev<=user->level;++lev) {
  cnt=0;
  /* colourize lines if need be */
  if (!is_wrap) {
    sprintf(text,"~FG~OL%c)~RS  ~FT",level_name[lev][0]);
    }
  else if (is_wrap && !user->hwrap_same) {
    sprintf(text,"~FG~OL%c)~RS  ~FT",level_name[lev][0]);
    }
  else sprintf(text,"    ");
  user->hwrap_same=1;
  /* scroll through all commands, format and print */
  while(cmd!=NULL) {
    temp1[0]='\0';
    if (cmd->min_lev!=lev) {  cmd=cmd->next;  continue;  }
    sprintf(temp1,"%s %s",cmd->name,cmd->alias);
    sprintf(temp,"%-13s  ",temp1);
    strcat(text,temp);
    cnt++;
    if (cnt==5) {  
      strcat(text,"\n");  
      write_user(user,text);  
      cnt=0;  ++lines;  text[0]='\0';
      }
    cmd=cmd->next;
    if (lines>=user->pager) {
      user->misc_op=14;
      user->hwrap_id=cmd->id;
      user->hwrap_lev=lev;
      write_user(user,"~BB~FG-=[*]=- PRESS <RETURN>, E TO EXIT:~RS ");
      return;
      }
    if (!cnt) strcat(text,"    ");
    } /* end while */
  if (cnt>0 && cnt<5) {
    strcat(text,"\n");  write_user(user,text);
    ++lines;  text[0]='\0';
    }
  user->hwrap_same=0;
  if (lines>=user->pager) {
    user->misc_op=14;
    user->hwrap_id=first_command->id;
    user->hwrap_lev=++lev;
    write_user(user,"~BB~FG-=[*]=- PRESS <RETURN>, E TO EXIT:~RS ");
    return;
    }
  cmd=first_command;
  } /* end if */
/* count up total number of commands for user's level */
cmd=first_command;
while(cmd!=NULL) { 
  if (cmd->min_lev>user->level) { cmd=cmd->next; continue; }
  cmd=cmd->next; ++total; 
  }
sprintf(text,"\nThere is a total of ~OL%d~RS command%s that you can use.\n\n",total,PLTEXT_S(total));
write_user(user,text);
/* reset variables */
user->hwrap_same=0;  user->hwrap_lev=0;  user->hwrap_id=0;  user->misc_op=0;
}


/*** Show the command available listed by function ***/
void help_commands_function(UR_OBJECT user, int is_wrap)
{
int cnt,total,lines,maxfunc,len;
char text2[ARR_SIZE],temp[20],temp1[20],*spacer=" ";
struct command_struct *cmd;

if (!is_wrap) {
  /* write out the header */
  write_user(user,"\n+----------------------------------------------------------------------------+\n");
  write_user(user,"| All commands start with a '.' (when in ~FYspeech~RS mode) and can be abbreviated |\n");
  write_user(user,"| Remember, a '.' by itself will repeat your last command or speech          |\n");
  write_user(user,"+----------------------------------------------------------------------------+\n");
  sprintf(text,"~FT~OLCommands available for level ~RS~OL%s~RS",level_name[user->level]);
  sprintf(text2,"| %-89s |\n",text);
  write_user(user,text2);
  write_user(user,"+----------------------------------------------------------------------------+\n\n");
  /* set some defaults */
  cmd=first_command;
  user->hwrap_func=0;
  user->hwrap_lev=JAILED;
  user->hwrap_id=first_command->id;
  user->hwrap_same=0;
  }
/* if we've gone through a screen wrap then find which node we're on */
if (is_wrap) {
  cmd=first_command;
  while (cmd!=NULL) {
    if (cmd->id==user->hwrap_id) break;
    cmd=cmd->next;
    }
  }

/* get how many functions there are and the length of the longest one */
maxfunc=0;  len=0;
while (command_types[maxfunc][0]!='*') {
  if (strlen(command_types[maxfunc])>len) len=strlen(command_types[maxfunc]);
  ++maxfunc;
  }
/* scroll trough all the commands listing by level */
total=0;  lines=0;
while (user->hwrap_func<=maxfunc) {
  cnt=0;
  /* colourize lines if need be */
  if (!is_wrap) {
    sprintf(text,"~FG~OL%*s)~RS  ~FT",len,command_types[user->hwrap_lev]);
    }
  else if (is_wrap && !user->hwrap_same) {
    sprintf(text,"~FG~OL%*s)~RS  ~FT",len,command_types[user->hwrap_lev]);
    }
  else sprintf(text,"%*s   ",len,spacer);
  user->hwrap_same=1;
  /* scroll through all commands, format and print */
  while(cmd!=NULL) {
    if (cmd->function!=user->hwrap_func || cmd->min_lev>user->level) {  cmd=cmd->next;  continue;  }
    cnt++;
    temp1[0]='\0';
    sprintf(temp1,"%s %s",cmd->name,cmd->alias);
    if (cnt==5) sprintf(temp,"%s",temp1); 
    else sprintf(temp,"%-13s ",temp1);
    strcat(text,temp);
    if (cnt==5) {  
      strcat(text,"\n");
      write_user(user,text);  
      cnt=0;  ++lines;  text[0]='\0';  temp[0]='\0';
      }
    cmd=cmd->next;
    if (lines>=user->pager) {
      user->misc_op=15;
      user->hwrap_id=cmd->id;
      write_user(user,"~BB~FG-=[*]=- PRESS <RETURN>, E TO EXIT:~RS ");
      return;
      }
    if (!cnt) {
      sprintf(temp,"%*s   ",len,spacer);
      strcat(text,temp);
      temp[0]='\0';
      }
    } /* end while */
  user->hwrap_func++;
  user->hwrap_lev++;
  if (cnt>0 && cnt<5) {
    strcat(text,"\n");  write_user(user,text);
    ++lines;  text[0]='\0';
    }
  user->hwrap_same=0;
  if (lines>=user->pager) {
    user->misc_op=15;
    user->hwrap_id=first_command->id;
    write_user(user,"~BB~FG-=[*]=- PRESS <RETURN>, E TO EXIT:~RS ");
    return;
    }
  cmd=first_command;
  } /* end while */
/* count up total number of commands for user's level */
cmd=first_command;
while(cmd!=NULL) { 
  if (cmd->min_lev>user->level) { cmd=cmd->next; continue; }
  cmd=cmd->next; ++total; 
  }
sprintf(text,"\nThere is a total of ~OL%d~RS command%s that you can use.\n\n",total,PLTEXT_S(total));
write_user(user,text);
/* reset variables */
user->hwrap_same=0;  user->hwrap_func=0;  user->hwrap_lev=0;  user->hwrap_id=0;  user->misc_op=0;
}



/*** Show the credits. Add your own credits here if you wish but PLEASE leave 
     my credits intact. Thanks. */
void help_credits(UR_OBJECT user)
{
sprintf(text,"\n~BB*** The Credits :) ***\n\n~BRNUTS version %s, Copyright (C) Neil Robertson 1996.\n\n",NUTSVER);
write_user(user,text);
write_user(user,"~BM             ~BB             ~BT             ~BG             ~BY             ~BR             ~RS\n");
write_user(user,"NUTS stands for Neils Unix Talk Server, a program which started out as a\nuniversity project in autumn 1992 and has progressed from thereon. In no\nparticular order thanks go to the following people who helped me develop or\n");
write_user(user,"debug this code in one way or another over the years:\n   ~FTDarren Seryck, Steve Guest, Dave Temple, Satish Bedi, Tim Bernhardt,\n   ~FTKien Tran, Jesse Walton, Pak Chan, Scott MacKenzie and Bryan McPhail.\n"); 
write_user(user,"Also thanks must go to anyone else who has emailed me with ideas and/or bug\nreports and all the people who have used NUTS over the intervening years.\n");
write_user(user,"I know I've said this before but this time I really mean it - this is the final\nversion of NUTS 3. In a few years NUTS 4 may spring forth but in the meantime\nthat, as they say, is that. :)\n\n");
write_user(user,"If you wish to email me my address is '~FGneil@ogham.demon.co.uk~RS' and should\nremain so for the forseeable future.\n\nNeil Robertson - November 1996.\n");
write_user(user,"~BM             ~BB             ~BT             ~BG             ~BY             ~BR             ~RS\n\n");
sprintf(text,"~OL~FTAmnuts version %s~RS, Copyright (C) Andrew Collington, 1998\n\n",AMNUTSVER);
write_user(user,text);
write_user(user,"Amnuts stands for ~OLA~RSndy's ~OLM~RSodified ~OLNUTS~RS, a Unix talker server written in C.\n");
write_user(user,"This code is based on Neil's NUTS (oo-er!) and started out as a little bit\n");
write_user(user,"of code for some friends, but progressed into a fully fledged generic\n");
write_user(user,"talker base for anyone who wanted it (do YOU want it?! ;)\n");
write_user(user,"Thanks to Neil for the original NUTS code and Cygnus for his Ncohafmuta code.\n");
write_user(user,"Many thanks to Simon for the live account to test Amnuts on, for testing\n");
write_user(user,"code, and for pointing out errors (and for being my Best Man!).  Thanks as\n");
write_user(user,"well to Squirt, Arny, Xan, Karri, Rudder and others for debugging, ideas, etc.\n");
write_user(user,"Thanks also to everyone who has given me suggestions and comments on the\n");
write_user(user,"Amnuts code, especially my fiancee, Lisa, for.. well.. everything :)\n\n");
write_user(user,"If you have any comments or suggestions about this code, then please feel\n");
write_user(user,"free to email me at: ~OL~FTamnuts@iname.com\n");
write_user(user,"If you have a web broswer, then you can see the Amnuts website which is\n");
write_user(user,"located at:  ~OL~FThttp://www.talker.com/amnuts/\n");
write_user(user,"I hope you enjoy the talker!\n\nAndrew Collington, May 1999\n");
write_user(user,"\n~BM             ~BB             ~BT             ~BG             ~BY             ~BR             ~RS\n\n");
}


/*** Say user speech. ***/
void say(UR_OBJECT user, char *inpstr)
{
char type[10],*name;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot speak.\n");  return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: if (!(in_private_room(user))) inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
#ifdef NETLINKS
  if (user->room==NULL) {
    sprintf(text,"ACT %s say %s\n",user->name,inpstr);
    write_sock(user->netlink->socket,text);
    no_prompt=1;
    return;
    }
#endif
if (word_count<2 && user->command_mode) {
  write_user(user,"Say what?\n");  return;
  }
smiley_type(inpstr,type);
if (user->type==CLONE_TYPE) {
  sprintf(text,"Clone of %s %ss: %s\n",user->recap,type,inpstr);
  write_room(user->room,text);
  record(user->room,text);
  return;
  }
sprintf(text,"~FGYou %s:~RS %s\n",type,inpstr);
write_user(user,text);
if (user->vis) name=user->recap; else name=invisname;
if (!user->vis) write_monitor(user,user->room,0);
sprintf(text,"~FG%s %ss:~RS %s\n",name,type,inpstr);
write_room_except(user->room,text,user);
record(user->room,text);
}


/*** Direct a say to someone, even though the whole room can hear it ***/
void say_to(UR_OBJECT user, char *inpstr)
{
char type[10],*name1,*name2;
UR_OBJECT u;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot speak.\n");  return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: if (!(in_private_room(user))) inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
if (word_count<3 && inpstr[0]!='-') {
  write_user(user,"Say what to who?\n");
  return;
  }
if (word_count<2 && inpstr[0]=='-') {
  write_user(user,"Say what to who?\n");
  return;
  }
if ((u=get_user_name(user,word[1]))==NULL) {
  write_user(user,notloggedon);
  return;
  }
if (u==user) {
  write_user(user,"Talking to yourself is the first sign of madness!\n");
  return;
  }
#ifdef NETLINKS
  /* can send over netlink as normal say */
 if (user->room==NULL) {
   sprintf(text,"ACT %s say %s\n",user->name,inpstr);
   write_sock(user->netlink->socket,text);
   no_prompt=1;
   return;
   }
#endif
smiley_type(inpstr,type);
inpstr=remove_first(inpstr);
sprintf(text,"~FTYou %s to %s:~RS %s\n",type,u->recap,inpstr);
write_user(user,text);
if (user->vis) name1=user->recap; else name1=invisname;
if (u->vis) name2=u->recap; else name2=invisname;
if (!user->vis) write_monitor(user,user->room,0);
sprintf(text,"~FT%s %ss (to %s):~RS %s\n",name1,type,name2,inpstr);
write_room_except(user->room,text,user);
record(user->room,text);
}


/*** Shout something ***/
void shout(UR_OBJECT user, char *inpstr)
{
char *name;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot shout.\n");  return;
  }
if (word_count<2 && inpstr[1]<33) {
  write_user(user,"Shout what?\n");  return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
sprintf(text,"~OLYou shout:~RS %s\n",inpstr);
write_user(user,text);
if (user->vis) name=user->recap; else name=invisname;
if (!user->vis) write_monitor(user,NULL,0);
sprintf(text,"~OL%s shouts:~RS %s\n",name,inpstr);
write_room_except(NULL,text,user);
record_shout(text);
}


/*** Tell another user something ***/
void tell(UR_OBJECT user, char *inpstr)
{
UR_OBJECT u;
char type[5],*name;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot tell anyone anything.\n");
  return;
  }
/* determine whether this is a quick call */
if (!strcmp(",",word[1])) { 
  if (word_count<2) { revtell(user);  return; }
  u=get_user_name(user,user->call);
  inpstr=remove_first(inpstr);
  goto QSKIP;
  }
if (inpstr[0]==',') {
  ++inpstr;
  u=get_user_name(user,user->call);
  goto QSKIP;
  }
/* if .t <u> with no message */
if (word_count<3 && inpstr[0]!='>') {
  revtell(user);  return;
  }
/* if > <u> with no message */
if (word_count<2 && inpstr[0]=='>') {
  revtell(user);  return;
  }
u=get_user_name(user,word[1]);
inpstr=remove_first(inpstr);
QSKIP:
if (!u) { write_user(user,notloggedon);  return; }
if (u==user) {
  write_user(user,"Talking to yourself is the first sign of madness.\n");
  return;
  }
if ((check_igusers(u,user))!=-1 && user->level<GOD) {
  sprintf(text,"%s is ignoring tells from you.\n",u->recap);
  write_user(user,text);
  return;
  }
if (u->igntells && (user->level<WIZ || u->level>user->level)) {
  sprintf(text,"%s is ignoring tells at the moment.\n",u->recap);
  write_user(user,text);
  return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
if (u->afk) {
  if (u->afk_mesg[0])
    sprintf(text,"%s is ~FRAFK~RS, message is: %s\n",u->recap,u->afk_mesg);
  else sprintf(text,"%s is ~FRAFK~RS at the moment.\n",u->recap);
  write_user(user,text);
  write_user(user,"Sending message to their afk review buffer.\n");
  if (inpstr[strlen(inpstr)-1]=='?') strcpy(type,"ask");
  else strcpy(type,"tell");
  if (user->vis || u->level>=user->level) name=user->recap; else name=invisname;
  sprintf(text,"~FG~OL%s %ss you:~RS %s\n",name,type,inpstr);
  record_afk(u,text);
  return;
  }
if (u->editing) {
  sprintf(text,"%s is in ~FTEDIT~RS mode at the moment (using the line editor).\n",u->recap);
  write_user(user,text);
  write_user(user,"Sending message to their edit review buffer.\n");
  if (inpstr[strlen(inpstr)-1]=='?') strcpy(type,"ask");
  else strcpy(type,"tell");
  if (user->vis || u->level>=user->level) name=user->recap; else name=invisname;
  sprintf(text,"~FG~OL%s %ss you:~RS %s\n",name,type,inpstr);
  record_edit(u,text);
  return;
  }
if (u->ignall && (user->level<WIZ || u->level>user->level)) {
  if (u->malloc_start!=NULL) 
    sprintf(text,"%s is using the editor at the moment.\n",u->recap);
  else sprintf(text,"%s is ignoring everyone at the moment.\n",u->recap);
  write_user(user,text);  
  return;
  }
#ifdef NETLINKS
  if (u->room==NULL) {
    sprintf(text,"%s is offsite and would not be able to reply to you.\n",u->recap);
    write_user(user,text);
    return;
    }
#endif
if (inpstr[strlen(inpstr)-1]=='?') strcpy(type,"ask");
else strcpy(type,"tell");
sprintf(text,"~OL~FTYou %s %s:~RS %s\n",type,u->recap,inpstr);
write_user(user,text);
record_tell(user,text);
if (user->vis || u->level>=user->level) name=user->recap; else name=invisname;
sprintf(text,"~FT%s %ss you:~RS %s\n",name,type,inpstr);
write_user(u,text);
record_tell(u,text);
}


/*** Emote something ***/
void emote(UR_OBJECT user, char *inpstr)
{
char *name;

name="";
if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot emote.\n");  return;
  }
if (word_count<2 && inpstr[1]<33) {
  write_user(user,"Emote what?\n");  return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: if (!(in_private_room(user))) inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
if (user->type==CLONE_TYPE) {
  if (inpstr[0]=='\'' && (inpstr[1]=='s' || inpstr[1]=='S')) sprintf(text,"The clone of %s%s\n",name,inpstr);
  else sprintf(text,"The clone of %s %s\n",user->recap,inpstr);
  write_room(user->room,text);
  record(user->room,text);
  return;
  }
if (user->vis) name=user->recap; else name=invisname;
if (!user->vis) write_monitor(user,user->room,0);
if (inpstr[0]=='\'' && (inpstr[1]=='s' || inpstr[1]=='S')) sprintf(text,"%s%s\n",name,inpstr);
else sprintf(text,"%s %s\n",name,inpstr);
write_room(user->room,text);
record(user->room,text);
}


/*** Do a shout emote ***/
void semote(UR_OBJECT user, char *inpstr)
{
char *name;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot emote.\n");  return;
  }
if (word_count<2 && inpstr[1]<33) {
  write_user(user,"Shout emote what?\n");  return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
if (user->vis) name=user->recap; else name=invisname;
if (!user->vis) write_monitor(user,NULL,0);
if (inpstr[0]=='\'' && (inpstr[1]=='s' || inpstr[1]=='S')) sprintf(text,"~OL!!!~RS %s%s\n",name,inpstr);
else sprintf(text,"~OL!!~RS %s %s\n",name,inpstr);
write_room(NULL,text);
record_shout(text);
}


/*** Do a private emote ***/
void pemote(UR_OBJECT user, char *inpstr)
{
char *name;
UR_OBJECT u;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot emote.\n");  return;
  }
if (word_count<3 && !strchr("</",inpstr[0])) {
  write_user(user,"Secret emote what?\n");  return;
  }
if (word_count<2 && strchr("</",inpstr[0])) {
  write_user(user,"Secret emote what?\n"); return;
  }
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);  return;
  }
if (u==user) {
  write_user(user,"Talking to yourself is the first sign of madness.\n");
  return;
  }
if ((check_igusers(u,user))!=-1 && user->level<GOD) {
  sprintf(text,"%s is ignoring private emotes from you.\n",u->recap);
  write_user(user,text);
  return;
  }
if (u->igntells && (user->level<WIZ || u->level>user->level)) {
  sprintf(text,"%s is ignoring private emotes at the moment.\n",u->recap);
  write_user(user,text);
  return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
if (u->afk) {
  if (u->afk_mesg[0])
    sprintf(text,"%s is ~FRAFK~RS, message is: %s\n",u->recap,u->afk_mesg);
  else sprintf(text,"%s is ~FRAFK~RS at the moment.\n",u->recap);
  write_user(user,text);
  write_user(user,"Sending message to their afk review buffer.\n");
  if (user->vis || u->level>=user->level) name=user->recap; else name=invisname;
  inpstr=remove_first(inpstr);
  sprintf(text,"~FG~OL(=>)~RS %s %s\n",name,inpstr);
  record_afk(u,text);
  return;
  }
if (u->editing) {
  sprintf(text,"%s is in ~FTEDIT~RS mode at the moment (using the line editor).\n",u->recap);
  write_user(user,text);
  write_user(user,"Sending message to their edit review buffer.\n");
  if (user->vis || u->level>=user->level) name=user->recap; else name=invisname;
  inpstr=remove_first(inpstr);
  sprintf(text,"~FG~OL(=>)~RS %s %s\n",name,inpstr);
  record_edit(u,text);
  return;
  }
if (u->ignall && (user->level<WIZ || u->level>user->level)) {
  if (u->malloc_start!=NULL) 
    sprintf(text,"%s is using the editor at the moment.\n",u->recap);
  else sprintf(text,"%s is ignoring everyone at the moment.\n",u->recap);
  write_user(user,text);  return;
  }
#ifdef NETLINKS
  if (u->room==NULL) {
    sprintf(text,"%s is offsite and would not be able to reply to you.\n",u->recap);
    write_user(user,text);
    return;
    }
#endif
if (user->vis || u->level>=user->level) name=user->recap; else name=invisname;
inpstr=remove_first(inpstr);
if (inpstr[0]=='\'' && (inpstr[1]=='s' || inpstr[1]=='S')) sprintf(text,"~FG~OL(%s =>)~RS %s%s\n",u->recap,name,inpstr);
else sprintf(text,"~FG~OL(%s =>)~RS %s %s\n",u->recap,name,inpstr);
write_user(user,text);
record_tell(user,text);
if (inpstr[0]=='\'' && (inpstr[1]=='s' || inpstr[1]=='S')) sprintf(text,"~FG~OL(=>)~RS %s%s\n",name,inpstr);
else sprintf(text,"~FG~OL(=>)~RS %s %s\n",name,inpstr);
write_user(u,text);
record_tell(u,text);
}


/*** Echo something to screen ***/
void echo(UR_OBJECT user, char *inpstr)
{
if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot echo.\n");  return;
  }
if (word_count<2 && inpstr[1]<33) {
  write_user(user,"Echo what?\n");  return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: if (!(in_private_room(user))) inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
write_monitor(user,user->room,0);
sprintf(text,"+ %s\n",inpstr);
write_room(user->room,text);
record(user->room,text);
}


/** Tell something to everyone but one person **/
void mutter(UR_OBJECT user, char *inpstr)
{
UR_OBJECT user2;
char *name;

if (word_count<3) {
  write_user(user,"Usage: mutter <name> <message>\n");
  return;
  }
if (!(user2=get_user_name(user,word[1]))) {
  write_user(user,notloggedon); return;
  }
inpstr=remove_first(inpstr);
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: if (!(in_private_room(user))) inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
	      return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
if (user->room!=user2->room) {
  sprintf(text,"%s is not in the same room, so speak freely of them.\n",user2->recap);
  write_user(user,text); return;
  }
if (user2==user) {
  write_user(user,"Talking about yourself is a sign of madness!\n");
  return;
  }
if (user->vis) name=user->recap;
else {
  name=invisname;
  write_monitor(user,user->room,0);
  }
sprintf(text,"~FT%s mutters:~RS %s ~FY~OL(to all but %s)\n",name,inpstr,user2->recap);
write_room_except(user->room,text,user2);
}


/** ask all the law, (sos), no muzzle restrictions **/
void plead(UR_OBJECT user, char *inpstr)
{
int found=0;
UR_OBJECT u;
char *err="Sorry, but there are no wizzes currently logged on.\n";

if (word_count<2) {
  write_user(user,"Usage: sos <message>\n");
  return;
  }
if (user->level>=WIZ) {
  write_user(user,"You are already a wizard!\n");
  return;
  }
for (u=user_first;u!=NULL;u=u->next)  if (u->level>=WIZ) found=1;
if (!found) {
  write_user(user,err); return;
  }
sprintf(text,"~OL[ SOS from %s ]~RS %s\n",user->recap,inpstr);
write_level(WIZ,1,text,NULL);
sprintf(text,"~OLYou sos to the wizzes:~RS %s\n",inpstr);
write_user(user,text);
record_tell(user,text);
}


/*** Shout something to other wizes and gods. If the level isnt given it
     defaults to WIZ level. ***/
void wizshout(UR_OBJECT user, char *inpstr)
{
int lev;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot wizshout.\n");  return;
  }
if (word_count<2) {
  write_user(user,"Usage: twiz [<superuser level>] <message>\n"); 
  return;
  }
/* Even wizzes cannot escapde the swear ban!  MWHAHahaha.... ahem.  */
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
strtoupper(word[1]);
if ((lev=get_level(word[1]))==-1) lev=WIZ;
else {
  if (lev<WIZ || word_count<3) {
    write_user(user,"Usage: twiz [<superuser level>] <message>\n");
    return;
    }
  if (lev>user->level) {
    write_user(user,"You cannot specifically shout to users of a higher level than yourself.\n");
    return;
    }
  inpstr=remove_first(inpstr);
  sprintf(text,"~FY<WIZ: ~OL[%s]~RS~FY>~RS %s\n",level_name[lev],inpstr);
  write_user(user,text);
  record_tell(user,text);
  sprintf(text,"~FY<WIZ: %s ~OL[%s]~RS~FY>~RS %s\n",user->recap,level_name[lev],inpstr);
  write_level(lev,1,text,user);
  return;
  }
sprintf(text,"~FY<WIZ>~RS %s\n",inpstr);
write_user(user,text);
record_tell(user,text);
sprintf(text,"~FY<WIZ: %s>~RS %s\n",user->recap,inpstr);
write_level(WIZ,1,text,user);
}


/*** Emote something to other wizes and gods. If the level isnt given it
     defaults to WIZ level. ***/
void wizemote(UR_OBJECT user, char *inpstr)
{
int lev;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot emote to the rest of the Wizzes.\n");  return;
  }
if (word_count<2) {
  write_user(user,"Usage: ewiz [<Wiz level>] <message>\n"); 
  return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
strtoupper(word[1]);
if ((lev=get_level(word[1]))==-1) lev=WIZ;
else {
  if (lev<WIZ || word_count<3) {
    write_user(user,"Usage: ewiz [<Wiz level>] <message>\n");
    return;
    }
  if (lev>user->level) {
    write_user(user,"You cannot specifically emote to users of a higher level than yourself.\n");
    return;
    }
  inpstr=remove_first(inpstr);
  sprintf(text,"~FY<WIZ: ~OL[%s]~RS~FY=>~RS %s %s\n",level_name[lev],user->recap,inpstr);
  write_level(lev,1,text,NULL);
  return;
  }
sprintf(text,"~FY<WIZ: =>~RS %s %s\n",user->recap,inpstr);
write_user(user,text);
record_tell(user,text);
sprintf(text,"~FY<WIZ: =>~RS %s %s\n",user->recap,inpstr);
write_level(WIZ,1,text,user);
}


/* Displays a picture to a person */
void picture_tell(UR_OBJECT user)
{
UR_OBJECT u;
char filename[80],*name,*c;
FILE *fp;

if (word_count<3) {
  write_user(user,"Usage: ptell <user> <picture name>\n");
  return;
  }
if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot tell anyone anything.\n");
  return;
  }
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);  return;
  }
if (u==user) {
  write_user(user,"There is an easier way to see pictures...\n");
  return;
  }
if ((check_igusers(u,user))!=-1 && user->level<GOD) {
  sprintf(text,"%s is ignoring pictures from you.\n",u->recap);
  write_user(user,text);
  return;
  }
if (u->ignall && (user->level<WIZ || u->level>user->level)) {
  if (u->malloc_start!=NULL)
    sprintf(text,"%s is writing a message at the moment.\n",u->recap);
  else sprintf(text,"%s is not listening at the moment.\n",u->recap);
  write_user(user,text);
  return;
  }
#ifdef NETLINKS
  if (u->room==NULL) {
    sprintf(text,"%s is offsite and would not be able to reply to you.\n",u->recap);
    write_user(user,text);
    return;
    }
#endif
if (u->ignpics) {
  sprintf(text,"%s is ignoring pictures at the moment.\n",u->recap);
  write_user(user,text);
  return;
  }
c=word[2];
while(*c) {
  if (*c=='.' || *c++=='/') {
    write_user(user,"Sorry, there is no picture with that name.\n");
    return;
    }
  }
sprintf(filename,"%s/%s",PICTFILES,word[2]);
if (!(fp=fopen(filename,"r"))) {
  write_user(user,"Sorry, there is no picture with that name.\n");
  return;
  }
fclose(fp);
if (!user->vis && u->level<user->level) name=invisname; else name=user->recap;
sprintf(text,"~FG~OL%s shows you the following picture...\n\n",name);
write_user(u,text);
switch(more(u,u->socket,filename)) {
  case 0: break;
  case 1: u->misc_op=2;
  }
sprintf(text,"~FG~OLYou show the following picture to %s\n\n",u->recap);
write_user(user,text);
switch(more(user,user->socket,filename)) {
  case 0: break;
  case 1: user->misc_op=2;
  }
}


/* see list of pictures availiable - file dictated in 'go' script */
void preview(UR_OBJECT user)
{
char filename[80],*c;
FILE *fp;

if (word_count<2) {
  sprintf(filename,"%s/pictlist",MISCFILES);
  write_user(user,"The following pictures can be viewed...\n\n");
  switch(more(user,user->socket,filename)) {
    case 0: write_user(user,"No list of the picture files is availiable.\n");
            break;
    case 1: user->misc_op=2;
    }
  return;
  }
c=word[1];
while(*c) {
  if (*c=='.' || *c++=='/') {
    write_user(user,"Sorry, there is no picture with that name.\n");
    return;
    }
  }
sprintf(filename,"%s/%s",PICTFILES,word[1]);
if (!(fp=fopen(filename,"r"))) {
  write_user(user,"Sorry, there is no picture with that name.\n");
  return;
  }
fclose(fp);
write_user(user,"~FG~OLYou preview the following picture...\n\n");
switch(more(user,user->socket,filename)) {
  case 0: break;
  case 1: user->misc_op=2;
  }
}


/*** Show a picture to the whole room that the user is in ***/
void picture_all(UR_OBJECT user)
{
UR_OBJECT u;
char filename[80],*name,*c;
FILE *fp;

if (word_count<2) {
  preview(user); 
  write_user(user,"\nUsage: picture <name>\n");
  return;
  }
if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot tell anyone anything.\n");
  return;
  }
c=word[1];
while(*c) {
  if (*c=='.' || *c++=='/') {
    write_user(user,"Sorry, there is no picture with that name.\n");
    return;
    }
  }
sprintf(filename,"%s/%s",PICTFILES,word[1]);
if (!(fp=fopen(filename,"r"))) {
  write_user(user,"Sorry, there is no picture with that name.\n");
  return;
  }
fclose(fp);
for (u=user_first;u!=NULL;u=u->next) {
  if (u->login
      || u->room==NULL
      || (u->room!=user->room && user->room!=NULL)
      || (u->ignall && !force_listen)
      || u->ignpics
      || u==user) continue;
  if ((check_igusers(u,user))!=-1 && user->level<GOD) continue;
  if (!user->vis && u->level<=user->level) name=invisname;  else  name=user->recap;
  if (u->type==CLONE_TYPE) {
    if (u->clone_hear==CLONE_HEAR_NOTHING || u->owner->ignall
	|| u->clone_hear==CLONE_HEAR_SWEARS) continue;
    /* Ignore anything not in clones room, eg shouts, system messages
       and shemotes since the clones owner will hear them anyway. */
    if (user->room!=u->room) continue;
    sprintf(text,"~BG~FK[ %s ]:~RS~FG~OL %s shows the following picture...\n\n",u->room->name,name);
    write_user(u->owner,text);
    switch(more(u,u->socket,filename)) {
      case 0: break;
      case 1: u->misc_op=2;
      }
    }
  else {
    sprintf(text,"~FG~OL%s shows the following picture...\n\n",name);
    write_user(u,text);
    switch(more(u,u->socket,filename)) {
      case 0: break;
      case 1: u->misc_op=2;
      }
    } /* end if else */
  } /* end for */
write_user(user,"~FG~OLThe following picture was sent to the room.\n\n");
switch(more(user,user->socket,filename)) {
    case 0: break;
    case 1: user->misc_op=2;
    }
}


/*** print out greeting in large letters ***/
void greet(UR_OBJECT user, char *inpstr)
{
char pbuff[ARR_SIZE],temp[8];
int slen,lc,c,i,j;
char *clr[]={"~OL~FR","~OL~FG","~OL~FT","~OL~FM","~OL~FY"};

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot greet.\n");  return;
  }
if (word_count<2) {
  write_user(user,"Usage: greet <message>\n"); return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
if (strlen(inpstr)>11) {
  write_user(user,"You can only have up to 11 letters in the greet.\n");
  return;
  }
write_monitor(user,user->room,0);
write_room(user->room,"\n");
slen=strlen(inpstr);
if (slen>11) slen=11;
for (i=0; i<5; ++i) {
  pbuff[0] = '\0';
  temp[0]='\0';
  for (c=0; c<slen; ++c) {
    /* check to see if it's a character a-z */
    if (isupper(inpstr[c]) || islower(inpstr[c])) {
      lc = tolower(inpstr[c]) - 'a';
      if ((lc >= 0) && (lc < 27)) { 
	for (j=0;j<5;++j) {
	  if(biglet[lc][i][j]) {
	    sprintf(temp,"%s#",clr[rand()%5]);
	    strcat(pbuff,temp);
	    }
	  else strcat(pbuff," ");
	  }
	strcat(pbuff,"  ");
        }
      }
    /* check if it's a character from ! to @ */
    if (isprint(inpstr[c])) {
      lc = inpstr[c] - '!';
      if ((lc >= 0) && (lc < 32)) { 
	for (j=0;j<5;++j) {
	  if(bigsym[lc][i][j]) {
	    sprintf(temp,"%s#",clr[rand()%5]);
	    strcat(pbuff,temp);
	    }
	  else strcat(pbuff," ");
	  }
	strcat(pbuff,"  ");
        }
      }
    }
  sprintf(text,"%s\n",pbuff);
  write_room(user->room,text);
  }
}


/** put speech in a think bubbles **/
void think_it(UR_OBJECT user, char *inpstr)
{
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: if (!(in_private_room(user))) inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
if (!user->vis) write_monitor(user,user->room,0);
if (word_count<2 && !user->vis) sprintf(text,"%s thinks nothing - now that's just typical!\n",invisname);
else if (word_count<2 && user->vis) sprintf(text,"%s thinks nothing - now that's just typical!\n",user->recap);
else if (!user->vis) sprintf(text,"%s thinks . o O ( %s )\n",invisname,inpstr);
else sprintf(text,"%s thinks . o O ( %s )\n",user->recap,inpstr);
write_room(user->room,text);
record(user->room,text);
}


/** put speech in a music notes **/
void sing_it(UR_OBJECT user, char *inpstr)
{
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: if (!(in_private_room(user))) inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
if (!user->vis) write_monitor(user,user->room,0);
if (word_count<2 && !user->vis) sprintf(text,"%s sings a tune... BADLY!\n",invisname);
else if (word_count<2 && user->vis) sprintf(text,"%s sings a tune... BADLY!\n",user->recap);
else if (!user->vis) sprintf(text,"%s sings o/~ %s o/~\n",invisname,inpstr);
else sprintf(text,"%s sings o/~ %s o/~\n",user->recap,inpstr);
write_room(user->room,text);
record(user->room,text);
}


/*** Broadcast an important message ***/
void bcast(UR_OBJECT user, char *inpstr, int beeps)
{
char *b="\007",null[1],*bp;

if (word_count<2) {
  switch(beeps) {
    case 0: write_user(user,"Usage: bcast <message>\n");  return;
    case 1: write_user(user,"Usage: bbcast <message>\n");  return;
    }
  }
/* wizzes should be trusted... But they ain't! */
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
force_listen=1;
null[0]='\0';
if (!beeps) bp=null; else bp=b;
write_monitor(user,NULL,0);
sprintf(text,"%s~FR~OL--==<~RS %s ~RS~FR~OL>==--\n",bp,inpstr);
write_room(NULL,text);
}


/*** Wake up some idle sod ***/
void wake(UR_OBJECT user)
{
UR_OBJECT u;
char *name;

if (word_count<2) {
  write_user(user,"Usage: wake <user>\n");  return;
  }
if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot wake anyone.\n");  return;
  }
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);  return;
  }
if (u==user) {
  write_user(user,"Trying to wake yourself up is the eighth sign of madness.\n");
  return;
  }
if ((check_igusers(u,user))!=-1 && user->level<GOD) {
  sprintf(text,"%s is ignoring wakes from you.\n",u->recap);
  write_user(user,text);
  return;
  }
if (u->afk) {
  write_user(user,"You cannot wake someone who is AFK.\n");  return;
  }
if (user->vis) name=user->recap; else name=invisname;
sprintf(text,"\n%s~BR*** %s says: ~OL~LIWAKE UP!!!~RS~BR ***\n\n",(u->ignbeeps)?"":"\07",name);
write_user(u,text);
write_user(user,"Wake up call sent.\n");
}


/* Beep a user - as tell but with audio warning */
void beep(UR_OBJECT user, char *inpstr) {
UR_OBJECT u;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot beep.\n");  return;
  }
if (word_count<2) {
  write_user(user,"Usage: beep <user> [message]\n");  return;
  }
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);  return;
  }
if (u==user) {
  write_user(user,"Beeping yourself is yet another sign of madness!\n");
  return;
  }
if (u->ignbeeps && user->level<GOD) {
  sprintf(text,"%s is ignoring beeps at the moment.\n",u->recap);
  write_user(user,text);  return;
  }
if ((check_igusers(u,user))!=-1 && user->level<GOD) {
  sprintf(text,"%s is ignoring tells from you.\n",u->recap);
  write_user(user,text);
  return;
  }
if (u->ignall && (user->level<GOD || u->level>user->level)) {
  if (u->malloc_start!=NULL) sprintf(text,"%s is writing a message at the moment.\n",u->recap);
  else sprintf(text,"%s is not listening at the moment.\n",u->recap);
  write_user(user,text);
  return;
  }
inpstr=remove_first(inpstr);
if (!u->vis && user->level<u->level) {
  write_user(user,"You cannot see that person here!\n");
  return;
  }
if (word_count<3) {
  if (!user->vis && u->level<user->level) write_user(u,"\007~FR~OLSomeone beeps to you:~RS~FR -=[*] BEEP [*]=-\n");
  else {
    sprintf(text,"\007~FR~OL%s beeps to you:~RS~FR -=[*] BEEP [*]=-\n",user->recap);
    write_user(u,text);
    }
  sprintf(text,"\007~FR~OLYou beep to %s:~RS~FR -=[*] BEEP [*]=-\n",u->recap);
  write_user(user,text);  return;
  }
if (!user->vis && u->level<user->level) sprintf(text,"\007~FR~OLSomeone beeps to you:~RS %s\n",inpstr);
else sprintf(text,"\007~FR~OL%s beeps to you:~RS %s\n",user->recap,inpstr);
write_user(u,text);
sprintf(text,"\007~FR~OLYou beep to %s:~RS %s\n",u->recap,inpstr);
write_user(user,text);
}


/* set a name for a quick call */
void quick_call(UR_OBJECT user)
{
UR_OBJECT u;

if (word_count<2) {
  if (!user->call[0]) {
    write_user(user,"Quick call not set.\n");
    return;
    }
  sprintf(text,"Quick call to: %s.\n",user->call);
  write_user(user,text);
  return;
  }
if (strlen(word[1])>USER_NAME_LEN) {
  write_user(user,"Name for quick call is too long.\n");
  return;
  }
if ((u=get_user_name(user,word[1]))==NULL) {
  write_user(user,"The user you want to call has to be online.\n");
  return;
  }
if (u==user) {
  write_user(user,"You cannot set your quick call to yourself.\n");
  return;
  }
strcpy(user->call,u->name);
user->call[0]=toupper(user->call[0]);
sprintf(text,"You have set a quick call to: %s.\n",user->call);
write_user(user,text);
}


/*** Show recorded tells and pemotes ***/
void revedit(UR_OBJECT user)
{
int i,cnt,line;
cnt=0;
for(i=0;i<REVTELL_LINES;++i) {
  line=(user->editline+i)%REVTELL_LINES;
  if (user->editbuff[line][0]) {
    cnt++;
    if (cnt==1) write_user(user,"\n~BB~FG*** Your edit review  buffer ***\n\n");
    write_user(user,user->editbuff[line]); 
    }
  }
if (!cnt) write_user(user,"EDIT review buffer is empty.\n");
else write_user(user,"\n~BB~FG*** End ***\n\n");
}


/*** Show recorded tells and pemotes ***/
void revafk(UR_OBJECT user)
{
int i,cnt,line;
cnt=0;
for(i=0;i<REVTELL_LINES;++i) {
  line=(user->afkline+i)%REVTELL_LINES;
  if (user->afkbuff[line][0]) {
    cnt++;
    if (cnt==1) write_user(user,"\n~BB~FG*** Your AFK review buffer ***\n\n");
    write_user(user,user->afkbuff[line]); 
    }
  }
if (!cnt) write_user(user,"AFK review buffer is empty.\n");
else write_user(user,"\n~BB~FG*** End ***\n\n");
}


/*** Clear the review buffer ***/
void revclr(UR_OBJECT user)
{
char *name;

clear_revbuff(user->room); 
write_user(user,"Review buffer cleared.\n");
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s has cleared the review buffer.\n",name);
write_room_except(user->room,text,user);
}


/*** See review of shouts ***/
void revshout(UR_OBJECT user)
{
int i,line,cnt;

cnt=0;
for(i=0;i<REVIEW_LINES;++i) {
  line=(sbuffline+i)%REVIEW_LINES;
  if (shoutbuff[line][0]) {
    cnt++;    
    if (cnt==1) write_user(user,"~BB~FG*** Shout review buffer ***\n\n");
    write_user(user,shoutbuff[line]); 
    }
  }
if (!cnt) write_user(user,"Shout review buffer is empty.\n");
else write_user(user,"\n~BB~FG*** End ***\n\n");
}


/*** Show recorded tells and pemotes ***/
void revtell(UR_OBJECT user)
{
int i,cnt,line;

cnt=0;
for(i=0;i<REVTELL_LINES;++i) {
  line=(user->revline+i)%REVTELL_LINES;
  if (user->revbuff[line][0]) {
    cnt++;
    if (cnt==1) write_user(user,"\n~BB~FG*** Your tell buffer ***\n\n");
    write_user(user,user->revbuff[line]); 
    }
  }
if (!cnt) write_user(user,"Revtell buffer is empty.\n");
else write_user(user,"\n~BB~FG*** End ***\n\n");
}


/*** See review of conversation ***/
void review(UR_OBJECT user)
{
RM_OBJECT rm=user->room;
int i,line,cnt;

if (word_count<2 || user->level<GOD) rm=user->room;
else {
  if ((rm=get_room(word[1]))==NULL) {
    write_user(user,nosuchroom);  return;
    }
  if (!has_room_access(user,rm)) {
    write_user(user,"That room is currently private, you cannot review the conversation.\n");
    return;
    }
  sprintf(text,"~FT(Review of %s room)\n",rm->name);
  write_user(user,text);
  }
cnt=0;
for(i=0;i<REVIEW_LINES;++i) {
  line=(rm->revline+i)%REVIEW_LINES;
  if (rm->revbuff[line][0]) {
    cnt++;
    if (cnt==1) write_user(user,"\n~BB~FG*** Room conversation buffer ***\n\n");
    write_user(user,rm->revbuff[line]); 
    }
  }
if (!cnt) write_user(user,"Review buffer is empty.\n");
else write_user(user,"\n~BB~FG*** End ***\n\n");
}


/*** Show some user stats ***/
void status(UR_OBJECT user)
{
UR_OBJECT u;
char ir[ROOM_NAME_LEN+1],text2[ARR_SIZE],text3[ARR_SIZE],rm[3],qcall[USER_NAME_LEN];
char homepg[82],email[82],icq[ICQ_LEN<15?15:ICQ_LEN],nm[5],muzlev[20],arrlev[20];
int days,hours,mins,hs,on,cnt,newmail;

if (word_count<2) {
  u=user;  on=1;
  }
else {
  if (!(u=get_user(word[1]))) {
    if ((u=create_user())==NULL) {
      sprintf(text,"%s: unable to create temporary user session.\n",syserror);
      write_user(user,text);
      write_syslog("ERROR: Unable to create temporary user session in status().\n",0,SYSLOG);
      return;
      }
    strcpy(u->name,word[1]);
    if (!load_user_details(u)) {
      write_user(user,nosuchuser);
      destruct_user(u);  destructed=0;
      return;
      }
    on=0;
    }
  else on=1;
  }
write_user(user,"\n\n");
if (!on) write_user(user,"+----- ~FTUser Info~RS -- ~FB(not currently logged on)~RS -------------------------------+\n");
else write_user(user,"+----- ~FTUser Info~RS -- ~OL~FY(currently logged on)~RS -----------------------------------+\n");
sprintf(text2,"%s %s",u->recap,u->desc);
cnt=colour_com_count(text2);
sprintf(text,"Name   : %-*.*s~RS Level : %s\n",45+cnt*3,45+cnt*3,text2,level_name[u->level]);
write_user(user,text);
mins=(int)(time(0) - u->last_login)/60;
days=u->total_login/86400;
hours=(u->total_login%86400)/3600;
if (u->invite_room==NULL) strcpy(ir,"<nowhere>");
else strcpy(ir,u->invite_room->name);
if (!u->age) sprintf(text2,"Unknown");
else sprintf(text2,"%d",u->age);
if (!on || u->login) sprintf(text3," ");
else sprintf(text3,"            Online for : %d min%s",mins,PLTEXT_S(mins));
sprintf(text,"Gender : %-8s      Age : %-8s %s\n",sex[u->gender],text2,text3);
write_user(user,text);
if (!strcmp(u->homepage,"#UNSET")) sprintf(homepg,"Currently unset");
else strcpy(homepg,u->homepage);
if (!strcmp(u->email,"#UNSET")) sprintf(email,"Currently unset");
else {
    if (!u->hideemail) strcpy(email,u->email);
    else if (u->hideemail && (user->level>=WIZ || u==user)) sprintf(email,"%s ~FB~OL(HIDDEN)~RS",u->email);
    else strcpy(email,"Currently only on view to the Wizzes");
    }
if (!strcmp(u->icq,"#UNSET")) sprintf(icq,"Currently unset");
else strcpy(icq,u->icq);
sprintf(text,"Email Address : %s\n",email);
write_user(user,text);
sprintf(text,"Homepage URL  : %s\n",homepg);
write_user(user,text);
sprintf(text,"ICQ Number    : %s\n",icq);
write_user(user,text);
mins=(u->total_login%3600)/60;
sprintf(text,"Total Logins  : %-9d  Total login : %d day%s, %d hour%s, %d minute%s\n",u->logons,days,PLTEXT_S(days),hours,PLTEXT_S(hours),mins,PLTEXT_S(mins));
write_user(user,text);
write_user(user,"+----- ~FTGeneral Info~RS ---------------------------------------------------------+\n");
sprintf(text,"Enter Msg     : %s %s\n",u->recap,u->in_phrase);
write_user(user,text);
sprintf(text,"Exit Msg      : %s %s~RS to the...\n",u->recap,u->out_phrase);
write_user(user,text);
newmail=mail_sizes(u->name,1);
if (!newmail) sprintf(nm,"NO");
else sprintf(nm,"%d",newmail);
if (!on || u->login) {
  sprintf(text,"New Mail      : %-13.13s  Muzzled : %-13.13s\n",nm,noyes2[(u->muzzled>0)]);
  write_user(user,text);
  }
else {
  sprintf(text,"Invited to    : %-13.13s  Muzzled : %-13.13s  Ignoring : %-13.13s\n",ir,noyes2[(u->muzzled>0)],noyes2[u->ignall]);
  write_user(user,text);
  #ifdef NETLINKS
    if (u->type==REMOTE_TYPE || u->room==NULL) {
      hs=0;  sprintf(ir,"<off site>");
      }
    else {
  #endif
      hs=1;  sprintf(ir,"%s",u->room->name);
  #ifdef NETLINKS
      }
  #endif
  sprintf(text,"In Area       : %-13.13s  At home : %-13.13s  New Mail : %-13.13s\n",ir,noyes2[hs],nm);
  write_user(user,text);
  }
sprintf(text,"Killed %d people, and died %d times.  Energy : %d, Bullets : %d\n",u->kills,u->deaths,u->hps,u->bullets);
write_user(user,text);
if (u==user || user->level>=WIZ) {
  write_user(user,"+----- ~FTUser Only Info~RS -------------------------------------------------------+\n");
  sprintf(text,"Char echo     : %-13.13s  Wrap    : %-13.13s  Monitor  : %-13.13s\n",noyes2[u->charmode_echo],noyes2[u->wrap],noyes2[u->monitor]);
  write_user(user,text);
  if (u->lroom==2) strcpy(rm,"YES");  else strcpy(rm,noyes2[u->lroom]);
  sprintf(text,"Colours       : %-13.13s  Pager   : %-13d  Logon rm : %-13.13s\n",noyes2[u->colour],u->pager,rm);
  write_user(user,text);
  if (!u->call[0]) strcpy(qcall,"<no one>");  else strcpy(qcall,u->call);
  sprintf(text,"Quick call to : %-13.13s  Autofwd : %-13.13s  Verified : %-13.13s\n",qcall,noyes2[u->autofwd],noyes2[u->mail_verified]);
  write_user(user,text);
  if (on && !u->login) {
    if (u==user && user->level<WIZ) sprintf(text,"On from site  : %s\n",u->site);
    else sprintf(text,"On from site  : %-42.42s  Port : %d\n",u->site,u->site_port);
    write_user(user,text);
    }
  }
if (user->level>=WIZ) {
  write_user(user,"+----- ~OL~FTWiz Only Info~RS --------------------------------------------------------+\n");
  if (!u->muzzled) strcpy(muzlev,"Unmuzzled");
  else strcpy(muzlev,level_name[u->muzzled]);
  if (!u->arrestby) strcpy(arrlev,"Unarrested");
  else strcpy(arrlev,level_name[u->arrestby]);
  sprintf(text,"Unarrest Lev  : %-13.13s  Arr lev : %-13.13s  Muz Lev  : %-13.13s\n",level_name[u->unarrest],arrlev,muzlev);
  write_user(user,text);
  if (u->lroom==2) sprintf(rm,"YES");  else sprintf(rm,"NO");
  sprintf(text,"Logon room    : %-38.38s  Shackled : %s\n",u->logout_room,rm);
  write_user(user,text);
  sprintf(text,"Last site     : %s\n",u->last_site);
  write_user(user,text);
  sprintf(text,"User Expires  : %-13.13s  On date : %s",noyes2[u->expire],ctime((time_t *)&u->t_expire));
  write_user(user,text);
  }
write_user(user,"+----------------------------------------------------------------------------+\n\n");

if (!on) {
  destruct_user(u); destructed=0;
  }
}


/*** Examine a user ***/
void examine(UR_OBJECT user)
{
UR_OBJECT u;
FILE *fp;
char filename[80],text2[ARR_SIZE];
int on,days,hours,mins,timelen,days2,hours2,mins2,idle,cnt,newmail;

if (word_count<2) {
  u=user;  on=1;
  }
else {
  if (!(u=get_user(word[1]))) {
    if ((u=create_user())==NULL) {
      sprintf(text,"%s: unable to create temporary user object.\n",syserror);
      write_user(user,text);
      write_syslog("ERROR: Unable to create temporary user object in examine().\n",0,SYSLOG);
      return;
      }
    strcpy(u->name,word[1]);
    if (!load_user_details(u)) {
      write_user(user,nosuchuser);   
      destruct_user(u);
      destructed=0;
      return;
      }
    on=0;
    }
  else on=1;
  }

days=u->total_login/86400;
hours=(u->total_login%86400)/3600;
mins=(u->total_login%3600)/60;
timelen=(int)(time(0) - u->last_login);
days2=timelen/86400;
hours2=(timelen%86400)/3600;
mins2=(timelen%3600)/60;

write_user(user,"+----------------------------------------------------------------------------+\n");
sprintf(text2,"%s %s",u->recap,u->desc);
cnt=colour_com_count(text2);
sprintf(text,"Name   : %-*.*s~RS Level : %s\n",45+cnt*3,45+cnt*3,text2,level_name[u->level]);
write_user(user,text);

if (!on) {
  sprintf(text,"Last login : %s",ctime((time_t *)&(u->last_login)));
  write_user(user,text);
  sprintf(text,"Which was  : %d day%s, %d hour%s, %d minute%s ago\n",days2,PLTEXT_S(days2),hours2,PLTEXT_S(hours2),mins2,PLTEXT_S(mins2));
  write_user(user,text);
  sprintf(text,"Was on for : %d hours, %d minutes\nTotal login: %d day%s, %d hour%s, %d minute%s\n",
	        u->last_login_len/3600,(u->last_login_len%3600)/60,days,PLTEXT_S(days),hours,PLTEXT_S(hours),mins,PLTEXT_S(mins));
  write_user(user,text);
  if (user->level>=WIZ) {
    sprintf(text,"Last site  : %s\n",u->last_site);
    write_user(user,text);
    }
  if ((newmail=mail_sizes(u->name,1))) {
    sprintf(text,"%s has unread mail (%d).\n",u->recap,newmail);
    write_user(user,text);
    }
  sprintf(text,"+----- Profile --------------------------------------------------------------+\n\n");
  write_user(user,text);
  sprintf(filename,"%s/%s/%s.P",USERFILES,USERPROFILES,u->name);
  if (!(fp=fopen(filename,"r"))) write_user(user,"User has not yet witten a profile.\n\n");
  else {
    fclose(fp);
    more(user,user->socket,filename);
    }
  write_user(user,"+----------------------------------------------------------------------------+\n\n");
  destruct_user(u);
  destructed=0;
  return;
  }
idle=(int)(time(0) - u->last_input)/60;
if (u->editing) sprintf(text,"Ignoring all: ~FTUsing Line Editor~RS\n");
else sprintf(text,"Ignoring all: %s\n",noyes2[u->ignall]);
write_user(user,text);
sprintf(text,"On since    : %sOn for      : %d hour%s, %d minute%s\n",ctime((time_t *)&u->last_login),hours2,PLTEXT_S(hours2),mins2,PLTEXT_S(mins2));
write_user(user,text);
if (u->afk) {
  sprintf(text,"Idle for    : %d minute%s ~BR(AFK)\n",idle,PLTEXT_S(idle));
  write_user(user,text);
  if (u->afk_mesg[0]) {
    sprintf(text,"AFK message : %s\n",u->afk_mesg);
    write_user(user,text);
    }
  }
else {
  sprintf(text,"Idle for    : %d minute%s\n",idle,PLTEXT_S(idle));
  write_user(user,text);
  }
sprintf(text,"Total login : %d day%s, %d hour%s, %d minute%s\n",days,PLTEXT_S(days),hours,PLTEXT_S(hours),mins,PLTEXT_S(mins));
write_user(user,text);
if (u->socket>=1) {
  if (user->level>=WIZ) {
    sprintf(text,"Site        : %-40.40s  Port : %d\n",u->site,u->site_port);
    write_user(user,text);
    }
  }
#ifdef NETLINKS
  else {
    sprintf(text,"Home service: %s\n",u->netlink->service);
    write_user(user,text);
    }
#endif
if ((newmail=mail_sizes(u->name,1))) {
  sprintf(text,"%s has unread mail (%d).\n",u->recap,newmail);
  write_user(user,text);
  }
sprintf(text,"+----- Profile --------------------------------------------------------------+\n\n");
write_user(user,text);
sprintf(filename,"%s/%s/%s.P",USERFILES,USERPROFILES,u->name);
if (!(fp=fopen(filename,"r"))) write_user(user,"User has not yet written a profile.\n\n");
else {
  fclose(fp);
  more(user,user->socket,filename);
  }
write_user(user,"+----------------------------------------------------------------------------+\n\n");
}


/* Set the user attributes */
void set_attributes(UR_OBJECT user, char *inpstr)
{
int i=0,tmp=0;
char name[USER_NAME_LEN+1],*recname;

set_val=-1;
if (word_count<2) goto ATT_JUMP;
i=0;
strtolower(word[1]);
while(setstr[i].type[0]!='*') {
  if (!strcmp(setstr[i].type,word[1])) {
    set_val=i;  break;
    }
  ++i;
  }
ATT_JUMP:
if (set_val==-1) {
  i=0;
  write_user(user,"Attributes you can set are:\n\n");
  while (setstr[i].type[0]!='*') {
    text[0]='\0';
    sprintf(text,"~FT%s~RS : %s\n",setstr[i].type,setstr[i].desc);
    write_user(user,text);
    i++;
    }
  i=0;
  }
write_user(user,"\n");
switch(set_val) {
  case SETSHOW: show_attributes(user); return;
  case SETGEND:
    inpstr=remove_first(inpstr);
    inpstr=colour_com_strip(inpstr);
    inpstr[0]=tolower(inpstr[0]);
    if (inpstr[0]=='m' || inpstr[0]=='f' || inpstr[0]=='n') {
      switch(inpstr[0]) {
        case 'm' :
	  user->gender=MALE;
	  write_user(user,"Gender set to Male\n");
	  break;
        case 'f' :
	  user->gender=FEMALE;
	  write_user(user,"Gender set to Female\n");
	  break;
        case 'n' :
	  user->gender=NEUTER;
	  write_user(user,"Gender set to Unset\n");
	  break;
        } /* end switch */
      if (auto_promote) check_autopromote(user,SET);
      return;
      }
    write_user(user,"Usage: set gender m/f/n\n");
    return;
  case SETAGE:
    if (word_count<3) {
      write_user(user,"Usage: set age <age>\n");
      return;
      }
    tmp=atoi(word[2]);
    if (tmp<0 || tmp>200) {
      write_user(user,"You can only set your age range between 0 (unset) and 200.\n");
      return;
      }
    user->age=tmp;
    sprintf(text,"Age now set to: %d\n",user->age);
    write_user(user,text);
    return;
  case SETWRAP:
    switch(user->wrap) {
      case 0: user->wrap=1;
	write_user(user,"Word wrap now ON\n");
	break;
      case 1: user->wrap=0;
	write_user(user,"Word wrap now OFF\n");
	break;
      }
    return;
  case SETEMAIL:
    inpstr=remove_first(inpstr);
    inpstr=colour_com_strip(inpstr);
    if (!inpstr[0]) strcpy(user->email,"#UNSET");
    else if (strlen(inpstr)>80) {
      write_user(user,"The maximum email length you can have is 80 characters.\n");
      return;
      }
    else {
      if (!validate_email(inpstr)) {
	write_user(user,"That email address format is incorrect.  Correct format: user@network.net\n");
	return;
        }
      strcpy(user->email,inpstr);
      } /* end else */
    if (!strcmp(user->email,"#UNSET")) sprintf(text,"Email set to : ~FRunset\n");
    else sprintf(text,"Email set to : ~FT%s\n",user->email);
    write_user(user,text);
    set_forward_email(user);
    return;
  case SETHOMEP:
    inpstr=remove_first(inpstr);
    inpstr=colour_com_strip(inpstr);
    if (!inpstr[0]) strcpy(user->homepage,"#UNSET");
    else if (strlen(inpstr)>80) {
      write_user(user,"The maximum homepage length you can have is 80 characters.\n");
      return;
      }
    else strcpy(user->homepage,inpstr);
    if (!strcmp(user->homepage,"#UNSET")) sprintf(text,"Homepage set to : ~FRunset\n");
    else sprintf(text,"Homepage set to : ~FT%s\n",user->homepage);
    write_user(user,text);
    return;
  case SETHIDEEMAIL:
    switch(user->hideemail) {
      case 0: user->hideemail=1;
	write_user(user,"Email now showing to only the admins.\n");
	break;
      case 1: user->hideemail=0;
	write_user(user,"Email now showing to everyone.\n");
	break;
      }
    return;
  case SETCOLOUR:
    switch(user->colour) {
      case 0: user->colour=1;
	write_user(user,"~FTColour now on\n");
	break;
      case 1: user->colour=0;
	write_user(user,"Colour now off\n");
	break;
      }
    return;
  case SETPAGER:
    if (word_count<3) {
      write_user(user,"Usage: set pager <length>\n");
      return;
      }
    user->pager=atoi(word[2]);
    if (user->pager<MAX_LINES || user->pager>99) {
      sprintf(text,"Pager can only be set between %d and 99 - setting to default\n",MAX_LINES);
      write_user(user,text);
      user->pager=23;
      }
    sprintf(text,"Pager length now set to: %d\n",user->pager);
    write_user(user,text);
    return;
  case SETROOM:
    switch(user->lroom) {
      case 0: user->lroom=1;
	write_user(user,"You will log on into the room you left from.\n");
	break;
      case 1: user->lroom=0;
	write_user(user,"You will log on into the main room.\n");
	break;
      }
    return;
  case SETFWD:
    if (!user->email[0] || !strcmp(user->email,"#UNSET")) {
      write_user(user,"You have not yet set your email address - autofwd cannot be used until you do.\n");
      return;
      }
    if (!user->mail_verified) {
      write_user(user,"You have not yet verified your email - autofwd cannot be used until you do.\n");
      return;
      }
    switch(user->autofwd) {
      case 0: user->autofwd=1;
	write_user(user,"You will also receive smails via email.\n");
	break;
      case 1: user->autofwd=0;
	write_user(user,"You will no longer receive smails via email.\n");
	break;
      }
    return;
  case SETPASSWD:
    switch(user->show_pass) {
      case 0: user->show_pass=1;
	write_user(user,"You will now see your password when entering it at login.\n");
	break;
      case 1: user->show_pass=0;
	write_user(user,"You will no longer see your password when entering it at login.\n");
	break;
      }
    return;
  case SETRDESC:
    switch(user->show_rdesc) {
      case 0: user->show_rdesc=1;
	write_user(user,"You will now see the room descriptions.\n");
	break;
      case 1: user->show_rdesc=0;
	write_user(user,"You will no longer see the room descriptions.\n");
	break;
      }
    return;
  case SETCOMMAND:
    switch(user->cmd_type) {
      case 0: user->cmd_type=1;
	write_user(user,"You will now see commands listed by functionality.\n");
	break;
      case 1: user->cmd_type=0;
	write_user(user,"You will now see commands listed by level.\n");
	break;
      }
    return;
  case SETRECAP:
    if (!allow_recaps) {
      write_user(user,"Sorry, names cannot be recapped at this present time.\n");
      return;
      }
    if (word_count<3) {
      write_user(user,"Usage: set recap <name as you would like it>\n");
      return;
      }
    recname=colour_com_strip(word[2]);
    strcpy(name,recname);
    strtolower(name);
    name[0]=toupper(name[0]);
    if (strcmp(user->name,name)) {
      write_user(user,"The recapped name still has to match your proper name.\n");
      return;
      }
    strcpy(user->recap,recname);
    sprintf(text,"Your name will now appear as ~OL%s~RS on the 'who', 'examine', tells, etc.\n",user->recap);
    write_user(user,text);
    return;
  case SETICQ:
    inpstr=colour_com_strip(remove_first(inpstr));
    if (!inpstr[0]) strcpy(user->icq,"#UNSET");
    else if (strlen(inpstr)>ICQ_LEN) {
      sprintf(text,"The maximum ICQ UIN length you can have is %d characters.\n",ICQ_LEN);
      write_user(user,text);
      return;
      }
    else strcpy(user->icq,inpstr);
    if (!strcmp(user->icq,"#UNSET")) sprintf(text,"ICQ number set to : ~FRunset\n");
    else sprintf(text,"ICQ number set to : ~FT%s\n",user->icq);
    write_user(user,text);
    return;
  case SETALERT:
    switch(user->alert) {
      case 0: user->alert=1;
	write_user(user,"You will now be alerted if anyone on your friends list logs on.\n");
	break;
      case 1: user->alert=0;
	write_user(user,"You will no longer be alerted if anyone on your friends list logs on.\n");
	break;
      }
    return;
  } /* end main switch */
}


void show_attributes(UR_OBJECT user)
{
char *onoff[]={"Off","On"};
char *shide[]={"Showing","Hidden"};
char *rm[]={"Main room","Last room in"};
char *cmd[]={"Level","Function"};
int i=0;
char text2[ARR_SIZE];

write_user(user,"\nStatus of the attributes:\n\n");
while (setstr[i].type[0]!='*') {
  text[0]='\0'; text2[0]='\0';
  sprintf(text,"   %-10.10s : (currently: ",setstr[i].type);
  switch(i) {
    case SETGEND:
      sprintf(text2,"~OL%s~RS)\n",sex[user->gender]);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETAGE:
      if (!user->age) sprintf(text2,"unset)\n");
      else sprintf(text2,"~OL%d~RS)\n",user->age);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETWRAP:
      sprintf(text2,"~OL%s~RS)\n",onoff[user->wrap]);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETEMAIL:
      if (!strcmp(user->email,"#UNSET")) sprintf(text2,"unset)\n");
      else {
	if (user->mail_verified) sprintf(text2,"~OL%s~RS - verified)\n",user->email);
	else sprintf(text2,"~OL%s~RS - not yet verified)\n",user->email);
        }
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETHOMEP:
      if (!strcmp(user->homepage,"#UNSET")) sprintf(text2,"unset)\n");
      else sprintf(text2,"~OL%s~RS)\n",user->homepage);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETHIDEEMAIL:
      sprintf(text2,"~OL%s~RS)\n",shide[user->hideemail]);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETCOLOUR:
      sprintf(text2,"~FT%s~RS)\n",onoff[user->colour]);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETPAGER:
      sprintf(text2,"~OL%d~RS)\n",user->pager);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETROOM:
      sprintf(text2,"~OL%s~RS)\n",rm[user->lroom]);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETFWD:
      sprintf(text2,"~OL%s~RS)\n",onoff[user->autofwd]);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETPASSWD:
      sprintf(text2,"~OL%s~RS)\n",onoff[user->show_pass]);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETRDESC:
      sprintf(text2,"~OL%s~RS)\n",onoff[user->show_rdesc]);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETCOMMAND:
      sprintf(text2,"~OL%s~RS)\n",cmd[user->cmd_type]);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETRECAP:
      sprintf(text2,"~OL%s~RS)\n",user->recap);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETICQ:
      if (!strcmp(user->icq,"#UNSET")) sprintf(text2,"unset)\n");
      else sprintf(text2,"~OL%s~RS)\n",user->icq);
      strcat(text,text2);
      write_user(user,text);
      break;
    case SETALERT:
      sprintf(text2,"~OL%s~RS)\n",onoff[user->alert]);
      strcat(text,text2);
      write_user(user,text);
      break;
    } /* end main switch */
  i++;
  }
write_user(user,"\n"); return;
}


/*** User prompt ***/
void prompt(UR_OBJECT user)
{
int hr,min,ign;

if (no_prompt) return;
#ifdef NETLINKS
  if (user->type==REMOTE_TYPE) {
    sprintf(text,"PRM %s\n",user->name);
    write_sock(user->netlink->socket,text);  
    return;
    }
#endif
ign=0;
if (user->ignall) ++ign;
if (user->igntells) ++ign;
if (user->ignshouts) ++ign;
if (user->ignpics) ++ign;
if (user->ignlogons) ++ign;
if (user->ignwiz) ++ign;
if (user->igngreets) ++ign;
if (user->ignbeeps) ++ign;
if (user->command_mode && !user->misc_op) {
  sprintf(text,"~FTCOM%s%s> ",!user->vis?"+":"",ign>0?"!":"");
  write_user(user,text);  
  return;  
  }
if (!user->prompt || user->misc_op) return;
hr=(int)(time(0)-user->last_login)/3600;
min=((int)(time(0)-user->last_login)%3600)/60;
sprintf(text,"~FT<%02d:%02d, %02d:%02d, %s%s%s>\n",thour,tmin,hr,min,user->recap,!user->vis?"+":"",ign>0?"!":"");
write_user(user,text);
}


/*** Switch prompt on and off ***/
void toggle_prompt(UR_OBJECT user)
{
if (user->prompt) {
  write_user(user,"Prompt ~FROFF.\n");
  user->prompt=0;
  return;
  }
write_user(user,"Prompt ~FGON.\n");
user->prompt=1;
}


/*** Switch between command and speech mode ***/
void toggle_mode(UR_OBJECT user)
{
if (user->command_mode) {
  write_user(user,"Now in ~FGSPEECH~RS mode.\n");
  user->command_mode=0;
  return;
  }
write_user(user,"Now in ~FTCOMMAND~RS mode.\n");
user->command_mode=1;
}


/*** Set the character mode echo on or off. This is only for users logging in
     via a character mode client, those using a line mode client (eg unix
     telnet) will see no effect. ***/
void toggle_charecho(UR_OBJECT user)
{
if (!user->charmode_echo) {
  write_user(user,"Echoing for character mode clients ~FGON.\n");
  user->charmode_echo=1;
  }
else {
  write_user(user,"Echoing for character mode clients ~FROFF.\n");
  user->charmode_echo=0;
  }
if (user->room==NULL) prompt(user);
}



/*** Set user description ***/
void set_desc(UR_OBJECT user, char *inpstr)
{
if (word_count<2) {
  sprintf(text,"Your current description is: %s\n",user->desc);
  write_user(user,text);
  return;
  }
if (strstr(word[1],"(CLONE)")) {
  write_user(user,"You cannot have that description.\n");
  return;
  }
if (strlen(inpstr)>USER_DESC_LEN) {
  write_user(user,"Description too long.\n");
  return;
  }
strcpy(user->desc,inpstr);
write_user(user,"Description set.\n");
/* check to see if user should be promoted */
if (auto_promote) check_autopromote(user,DESC);
}


/*** Set in and out phrases ***/
void set_iophrase(UR_OBJECT user, char *inpstr)
{
if (strlen(inpstr)>PHRASE_LEN) {
  write_user(user,"Phrase too long.\n");  return;
  }
if (com_num==INPHRASE) {
  if (word_count<2) {
    sprintf(text,"Your current in phrase is: %s\n",user->in_phrase);
    write_user(user,text);
    return;
    }
  strcpy(user->in_phrase,inpstr);
  write_user(user,"In phrase set.\n");
  return;
  }
if (word_count<2) {
  sprintf(text,"Your current out phrase is: %s\n",user->out_phrase);
  write_user(user,text);
  return;
  }
strcpy(user->out_phrase,inpstr);
write_user(user,"Out phrase set.\n");
}


/*** Enter user profile ***/
void enter_profile(UR_OBJECT user, int done_editing)
{
FILE *fp;
char *c,filename[80];

if (!done_editing) {
  write_user(user,"\n~BB*** Writing profile ***\n\n");
  user->misc_op=5;
  editor(user,NULL);
  return;
  }
sprintf(filename,"%s/%s/%s.P",USERFILES,USERPROFILES,user->name);
if (!(fp=fopen(filename,"w"))) {
  sprintf(text,"%s: couldn't save your profile.\n",syserror);
  write_user(user,text);
  sprintf("ERROR: Couldn't open file %s to write in enter_profile().\n",filename);
  write_syslog(text,0,SYSLOG);
  return;
  }
c=user->malloc_start;
while(c!=user->malloc_end) putc(*c++,fp);
fclose(fp);
write_user(user,"Profile stored.\n");
}


/*** A newbie is requesting an account. Get his email address off him so we
     can validate who he is before we promote him and let him loose as a 
     proper user. ***/
void account_request(UR_OBJECT user, char *inpstr)
{
if (user->level>NEW) {
  write_user(user,"This command is for new users only, you already have a full account.\n");
  return;
  }
/* This is so some pillock doesnt keep doing it just to fill up the syslog */
if (user->accreq==ACCREQ || user->accreq==1) {
  write_user(user,"You have already requested an account.\n");
  return;
  }
if (word_count<2) {
  write_user(user,"Usage: accreq <an email address we can contact you on + any relevent info>\n");
  return;
  }
if (!validate_email(inpstr)) {
  write_user(user,"That email address format is incorrect.  Correct format: user@network.net\n");
  return;
  }
sprintf(text,"%-*s : %s\n",USER_NAME_LEN,user->name,inpstr);
write_syslog(text,1,REQLOG);
sprintf(text,"~OLSYSTEM:~RS %s has made a request for an account.\n",user->name);
write_level(WIZ,1,text,NULL);
write_user(user,"Account request logged.\n");
add_history(user->name,1,"Made a request for an account.\n");
/* check to see if user should be promoted yet */
if (auto_promote) check_autopromote(user,ACCREQ);
else user->accreq=1;
}


/*** Do AFK ***/
void afk(UR_OBJECT user, char *inpstr)
{
if (word_count>1) {
  if (!strcmp(word[1],"lock")) {
  #ifdef NETLINKS
    if (user->type==REMOTE_TYPE) {
      /* This is because they might not have a local account and hence
	 they have no password to use. */
      write_user(user,"Sorry, due to software limitations remote users cannot use the lock option.\n");
      return;
      }
  #endif
    inpstr=remove_first(inpstr);
    if (strlen(inpstr)>AFK_MESG_LEN) {
      write_user(user,"AFK message too long.\n");  return;
      }
    write_user(user,"You are now AFK with the session locked, enter your password to unlock it.\n");
    if (inpstr[0]) {
      strcpy(user->afk_mesg,inpstr);
      write_user(user,"AFK message set.\n");
      }
    user->afk=2;
    }
  else {
    if (strlen(inpstr)>AFK_MESG_LEN) {
      write_user(user,"AFK message too long.\n");  return;
      }
    write_user(user,"You are now AFK, press <return> to reset.\n");
    if (inpstr[0]) {
      strcpy(user->afk_mesg,inpstr);
      write_user(user,"AFK message set.\n");
      }
    user->afk=1;
    }
  }
else {
  write_user(user,"You are now AFK, press <return> to reset.\n");
  user->afk=1;
  }
if (user->vis) {
  if (user->afk_mesg[0]) 
    sprintf(text,"%s goes AFK: %s\n",user->recap,user->afk_mesg);
  else sprintf(text,"%s goes AFK...\n",user->recap);
  write_room_except(user->room,text,user);
  }
clear_afk(user);
}


/* Get user's macros */
void get_macros(UR_OBJECT user)
{
char filename[80];
FILE *fp;
int i,l;

#ifdef NETLINKS
  if (user->type==REMOTE_TYPE) return;
#endif
sprintf(filename,"%s/%s/%s.MAC",USERFILES,USERMACROS,user->name);

if (!(fp=fopen(filename,"r"))) return;
for (i=0;i<10;++i) {
   fgets(user->macros[i],MACRO_LEN,fp);
   l=strlen(user->macros[i]);
   user->macros[i][l-1]='\0';
  }
fclose(fp);
}

/*** Display user macros ***/
void macros(UR_OBJECT user)
{
int i;

#ifdef NETLINKS
  if (user->type==REMOTE_TYPE) {
    write_user(user,"Due to software limitations, remote users cannot have macros.\n");
    return;
    }
#endif
write_user(user,"Your current macros:\n");
for (i=0;i<10;++i) {
   sprintf(text,"  ~OL%d)~RS %s\n",i,user->macros[i]);
   write_user(user,text);
   }
}


/*** See if command just executed by the user was a macro ***/
void check_macros(UR_OBJECT user, char *inpstr)
{
int i,macnum,lng;
char filename[80],line[ARR_SIZE];
FILE *fp;

if (inpstr[0]=='.' && inpstr[1]>='0' && inpstr[1]<='9') {
  #ifdef NETLINKS
    if (user->type==REMOTE_TYPE) {
      write_user(user,"Remote users cannot use macros at this time.\n");
      return;
      }
  #endif
  midcpy(inpstr,line,3,ARR_SIZE);
  macnum=inpstr[1]-'0';
  if (inpstr[2]=='=') {
    if (strlen(inpstr) > MACRO_LEN) {
      write_user(user,"That macro length was too long.\n");
      inpstr[0]=0;
      return;
      }
    strcpy(user->macros[macnum],line);
    inpstr[0]=0;
    sprintf(filename,"%s/%s/%s.MAC",USERFILES,USERMACROS,user->name);
    if (!(fp=fopen(filename,"w"))) {
      write_user(user,"Your macro file could not be accessed.\n");
      sprintf(text,"%s could not access macros file.\n",user->name);
      write_syslog(text,1,SYSLOG);
      return;
      }
    for (i=0;i<10;++i) {
      sprintf(text,"%s\n",user->macros[i]);
      fputs(text,fp);
      }
    fclose(fp);
    write_user(user,"Ok, macro set.\n");
    return;
    }
  else {
    lng=inpstr[2];
    strcpy(inpstr,user->macros[macnum]);
    if (lng) {
      strcat(inpstr," ");
      strcat(inpstr,line);
      }
    }
  }
}


/*** Set user visible or invisible ***/
void visibility(UR_OBJECT user, int vis)
{
if (vis) {
  if (user->vis) {
    write_user(user,"You are already visible.\n");  return;
    }
  write_user(user,"~FB~OLYou recite a melodic incantation and reappear.\n");
  sprintf(text,"~FB~OLYou hear a melodic incantation chanted and %s materialises!\n",user->recap);
  write_room_except(user->room,text,user);
  user->vis=1;
  return;
  }
if (!user->vis) {
  write_user(user,"You are already invisible.\n");  return;
  }
write_user(user,"~FB~OLYou recite a melodic incantation and fade out.\n");
sprintf(text,"~FB~OL%s recites a melodic incantation and disappears!\n",user->recap);
write_room_except(user->room,text,user);
user->vis=0;
}


/** Force a user to become invisible **/
void make_invis(UR_OBJECT user)
{
UR_OBJECT user2;

if (word_count<2) {
  write_user(user,"Usage: makeinvis <user>\n");  return;
  }
if (!(user2=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);  return;
  }
if (user==user2) {
  write_user(user,"There is an easier way to make yourself invisible!\n");
  return;
  }
if (!user2->vis) {
  sprintf(text,"%s is already invisible.\n",user2->recap);
  write_user(user,text);  return;
  }
if (user2->level>user->level) {
  sprintf(text,"%s cannot be forced invisible.\n",user2->recap);
  write_user(user,text);  return;
  }
user2->vis=0;
sprintf(text,"You force %s to become invisible.\n",user2->recap);
write_user(user,text);
write_user(user2,"You have been forced to become invisible.\n");
sprintf(text,"You see %s mysteriously disappear into the shadows!\n",user2->recap);
write_room_except(user2->room,text,user2);
}


/** Force a user to become visible **/
void make_vis(UR_OBJECT user)
{
UR_OBJECT user2;

if (word_count<2) {
  write_user(user,"Usage: makevis <user>\n");  return;
  }
if (!(user2=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);  return;
  }
if (user==user2) {
  write_user(user,"There is an easier way to make yourself visible!\n");
  return;
  }
if (user2->vis) {
  sprintf(text,"%s is already visible.\n",user2->recap);
  write_user(user,text);  return;
  }
if (user2->level>user->level) {
  sprintf(text,"%s cannot be forced visible.\n",user2->recap);
  write_user(user,text);  return;
  }
user2->vis=1;
sprintf(text,"You force %s to become visible.\n",user2->recap);
write_user(user,text);
write_user(user2,"You have been forced to become visible.\n");
sprintf(text,"You see %s mysteriously emerge from the shadows!\n",user2->recap);
write_room_except(user2->room,text,user2);
}


/* Set list of users that you ignore */
void show_igusers(UR_OBJECT user)
{
int i,cnt;

cnt=0;
write_user(user,"Currently ignoring the following users...\n\n");
for (i=0; i<MAX_IGNORES; ++i) {
  if (user->ignoreuser[i][0]) {
    sprintf(text,"   %2d) %s\n",cnt+1,user->ignoreuser[i]);
    write_user(user,text);
    cnt++;
    }
  }
if (!cnt) write_user(user,"   No one...\n");
write_user(user,"\n");
}


/*** check to see if user is ignoring person with the ignoring->name ***/
int check_igusers(UR_OBJECT user, UR_OBJECT ignoring)
{
int i;
if (user==NULL || ignoring==NULL) return -1;
for (i=0; i<MAX_IGNORES; ++i)
  if (!strcmp(user->ignoreuser[i],ignoring->name)) return i;
return -1;
}


/*** set to ignore/listen to a user ***/
void set_igusers(UR_OBJECT user)
{
UR_OBJECT u;
int i=0;

if (word_count<2) {
  show_igusers(user);
  return;
  }
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);  return;
  }
if (user==u) {
  write_user(user,"You cannot ignore yourself!\n");
  return;
  }
for (i=0; i<MAX_IGNORES; ++i) {
  if (!strcmp(user->ignoreuser[i],u->name)) {
    sprintf(text,"You once again listen to %s\n",user->ignoreuser[i]);
    write_user(user,text);
    user->ignoreuser[i][0]='\0';
    return;
    }
  if (!user->ignoreuser[i][0]) {
    strcpy(user->ignoreuser[i],u->name);
    sprintf(text,"You will now ignore tells from %s\n",user->ignoreuser[i]);
    write_user(user,text);
    return;
    }
  }
write_user(user,"You have ignored the maximum amount of users already.\n");
}


/*** sets up a channel for the user to ignore ***/
void set_ignore(UR_OBJECT user)
{
switch(com_num) {
  case IGNTELLS: 
      switch(user->igntells) {
          case 0: user->igntells=1;
	          write_user(user,"You will now ignore tells.\n");
	          break; 
	  case 1: user->igntells=0;
	          write_user(user,"You will now hear tells.\n");
	          break;
	  }
      break;
  case IGNSHOUTS: 
      switch(user->ignshouts) {
          case 0: user->ignshouts=1;
	          write_user(user,"You will now ignore shouts.\n");
	          break; 
	  case 1: user->ignshouts=0;
	          write_user(user,"You will now hear shouts.\n");
	          break;
	  }
      break;
  case IGNPICS: 
      switch(user->ignpics) {
          case 0: user->ignpics=1;
	          write_user(user,"You will now ignore pictures.\n");
	          break; 
	  case 1: user->ignpics=0;
	          write_user(user,"You will now see pictures.\n");
	          break;
	  }
      break;
  case IGNWIZ: 
      switch(user->ignwiz) {
          case 0: user->ignwiz=1;
	          write_user(user,"You will now ignore all wiztells and wizemotes.\n");
	          break;
	  case 1: user->ignwiz=0;
	          write_user(user,"You will now listen to all wiztells and wizemotes.\n");
	          break;
	  }
      break;
  case IGNLOGONS: 
      switch(user->ignlogons) {
          case 0: user->ignlogons=1;
	          write_user(user,"You will now ignore all logons and logoffs.\n");
	          break;
	  case 1: user->ignlogons=0;
	          write_user(user,"You will now see all logons and logoffs.\n");
	          break;
	  }
      break;
  case IGNGREETS: 
      switch(user->igngreets) {
          case 0: user->igngreets=1;
	          write_user(user,"You will now ignore all greets.\n");
	          break;
	  case 1: user->igngreets=0;
	          write_user(user,"You will now see all greets.\n");
	          break;
	  }
      break;
  case IGNBEEPS: 
      switch(user->ignbeeps) {
          case 0: user->ignbeeps=1;
	          write_user(user,"You will now ignore all beeps from users.\n");
	          break;
	  case 1: user->ignbeeps=0;
	          write_user(user,"You will now hear all beeps from users.\n");
	          break;
	  }
      break;
  default: break;
  }
}


/* displays what the user is currently listening to/ignoring */
void show_ignlist(UR_OBJECT user)
{
char *lstn[]={"~FGNot ignoring~RS","~FR~OLIgnoring~RS"};

write_user(user,"Your ignore states are as follows.\n\n");
if (user->ignall) {
  write_user(user,"You are currently ignoring ~OL~FReverything~RS.\n\n");
  return;
  }
sprintf(text,"~FTShouts~RS   : %s\n",lstn[user->ignshouts]);
write_user(user,text);
sprintf(text,"~FTTells~RS    : %s\n",lstn[user->igntells]);
write_user(user,text);
sprintf(text,"~FTLogons~RS   : %s\n",lstn[user->ignlogons]);
write_user(user,text);
sprintf(text,"~FTPictures~RS : %s\n",lstn[user->ignpics]);
write_user(user,text);
sprintf(text,"~FTGreets~RS   : %s\n",lstn[user->igngreets]);
write_user(user,text);
sprintf(text,"~FTBeeps~RS    : %s\n",lstn[user->ignbeeps]);
write_user(user,text);
if (user->level>=command_table[IGNWIZ].level) {
  sprintf(text,"~FTWiztells~RS : %s\n\n",lstn[user->ignwiz]);
  write_user(user,text);
  }
else write_user(user,"\n");
}


/*** Switch ignoring all on and off ***/
void toggle_ignall(UR_OBJECT user)
{
if (!user->ignall) {
  write_user(user,"You are now ignoring everyone.\n");
  if (user->vis) sprintf(text,"%s is now ignoring everyone.\n",user->recap);
  else sprintf(text,"%s is now ignoring everyone.\n",invisname);
  write_room_except(user->room,text,user);
  user->ignall=1;
  return;
  }
write_user(user,"You will now hear everyone again.\n");
if (user->vis) sprintf(text,"%s is listening again.\n",user->recap);
else sprintf(text,"%s is listening again.\n",invisname);
write_room_except(user->room,text,user);
user->ignall=0;
}


/*** Allows a user to listen to everything again ***/
void user_listen(UR_OBJECT user)
{
int yes;

yes=0;
if (user->ignall) { user->ignall=0;  yes=1; }
if (user->igntells) { user->igntells=0;  yes=1; }
if (user->ignshouts) { user->ignshouts=0;  yes=1; }
if (user->ignpics) { user->ignpics=0;  yes=1; }
if (user->ignlogons) { user->ignlogons=0;  yes=1; }
if (user->ignwiz) { user->ignwiz=0;  yes=1; }
if (user->igngreets) { user->igngreets=0;  yes=1; }
if (user->ignbeeps) { user->ignbeeps=0;  yes=1; }
if (yes) {
  write_user(user,"You listen to everything again.\n");
  if (user->vis) {
    sprintf(text,"%s is now listening to you all again.\n",user->name);
    write_room_except(user->room,text,user);
    }
  return;
  }
write_user(user,"You are already listening to everything.\n");
}


/*** Clone a user in another room ***/
void create_clone(UR_OBJECT user)
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;
int cnt;

/* Check room */
if (word_count<2) rm=user->room;
else {
  if ((rm=get_room(word[1]))==NULL) {
    write_user(user,nosuchroom);  return;
    }
  }	
/* If room is private then nocando */
if (!has_room_access(user,rm)) {
  write_user(user,"That room is currently private, you cannot create a clone there.\n");  
  return;
  }
/* Count clones and see if user already has a copy there , no point having 
   2 in the same room */
cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
  if (u->type==CLONE_TYPE && u->owner==user) {
    if (u->room==rm) {
      sprintf(text,"You already have a clone in the %s.\n",rm->name);
      write_user(user,text);
      return;
      }	
    if (++cnt==max_clones) {
      write_user(user,"You already have the maximum number of clones allowed.\n");
      return;
      }
    }
  }
/* Create clone */
if ((u=create_user())==NULL) {		
  sprintf(text,"%s: Unable to create copy.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create user copy in clone().\n",0,SYSLOG);
  return;
  }
u->type=CLONE_TYPE;
u->socket=user->socket;
u->room=rm;
u->owner=user;
u->vis=1;
strcpy(u->name,user->name);
strcpy(u->recap,user->recap);
strcpy(u->desc,"~BR~OL(CLONE)");
if (rm==user->room)
  write_user(user,"~FB~OLYou wave your hands, mix some chemicals and a clone is created here.\n");
else {
  sprintf(text,"~FB~OLYou wave your hands, mix some chemicals, and a clone is created in the %s.\n",rm->name);
  write_user(user,text);
  }
if (user->vis) name=user->recap; else name=invisname;
sprintf(text,"~FB~OL%s waves their hands...\n",name);
write_room_except(user->room,text,user);
sprintf(text,"~FB~OLA clone of %s appears in a swirling magical mist!\n",user->recap);
write_room_except(rm,text,user);
}


/*** Destroy all clones belonging to given user ***/
void destroy_user_clones(UR_OBJECT user)
{
UR_OBJECT u,next;

u=user_first;
while (u!=NULL) {
  next=u->next;
  if (u->type==CLONE_TYPE && u->owner==user) {
    sprintf(text,"The clone of %s is engulfed in magical blue flames and vanishes.\n",u->recap);
    write_room(u->room,text);
    destruct_user(u);
    }
  u=next;
  }
}


/*** Destroy user clone ***/
void destroy_clone(UR_OBJECT user)
{
UR_OBJECT u,u2;
RM_OBJECT rm;
char *name;

/* Check room and user */
if (word_count<2) rm=user->room;
else {
  if ((rm=get_room(word[1]))==NULL) {
    write_user(user,nosuchroom);  return;
    }
  }
if (word_count>2) {
  if ((u2=get_user_name(user,word[2]))==NULL) {
    write_user(user,notloggedon);  return;
    }
  if (u2->level>=user->level) {
    write_user(user,"You cannot destroy the clone of a user of an equal or higher level.\n");
    return;
    }
  }
else u2=user;
for(u=user_first;u!=NULL;u=u->next) {
  if (u->type==CLONE_TYPE && u->room==rm && u->owner==u2) {
    destruct_user(u);
    reset_access(rm);
    write_user(user,"~FM~OLYou whisper a sharp spell and the clone is destroyed.\n");
    if (user->vis) name=user->recap; else name=invisname;
    sprintf(text,"~FM~OL%s whispers a sharp spell...\n",name);
    write_room_except(user->room,text,user);
    sprintf(text,"~FM~OLThe clone of %s shimmers and vanishes.\n",u2->recap);
    write_room(rm,text);
    if (u2!=user) {
      sprintf(text,"~OLSYSTEM: ~FR%s has destroyed your clone in the %s.\n",user->recap,rm->name);
      write_user(u2,text);
      }
    destructed=0;
    return;
    }
  }
if (u2==user) sprintf(text,"You do not have a clone in the %s.\n",rm->name);
else sprintf(text,"%s does not have a clone the %s.\n",u2->recap,rm->name);
write_user(user,text);
}


/*** Show users own clones ***/
void myclones(UR_OBJECT user)
{
UR_OBJECT u;
int cnt;

cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
  if (u->type!=CLONE_TYPE || u->owner!=user) continue;
  if (++cnt==1) 
    write_user(user,"\n~BB*** Rooms you have clones in ***\n\n");
  sprintf(text,"  %s\n",u->room->name);
  write_user(user,text);
  }
if (!cnt) write_user(user,"You have no clones.\n");
else {
  sprintf(text,"\nTotal of ~OL%d~RS clone%s.\n\n",cnt,PLTEXT_S(cnt));
  write_user(user,text);
  }
}


/*** Show all clones on the system ***/
void allclones(UR_OBJECT user)
{
UR_OBJECT u;
int cnt;

cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
  if (u->type!=CLONE_TYPE) continue;
  if (++cnt==1) {
    sprintf(text,"\n~BB*** Current clones %s ***\n\n",long_date(1));
    write_user(user,text);
    }
  sprintf(text,"%-15s : %s\n",u->name,u->room->name);
  write_user(user,text);
  }
if (!cnt) write_user(user,"There are no clones on the system.\n");
else {
  sprintf(text,"\nTotal of ~OL%d~RS clone%s.\n\n",cnt,PLTEXT_S(cnt));
  write_user(user,text);
  }
}


/*** User swaps places with his own clone. All we do is swap the rooms the
	objects are in. ***/
void clone_switch(UR_OBJECT user)
{
UR_OBJECT u,tu;
RM_OBJECT rm;
int cnt=0;

/* if no room was given then check to see how many clones user has.  If 1, then
   move the user to that clone, else give an error
*/
tu=NULL;
rm=NULL;
if (word_count<2) {
  for(u=user_first;u!=NULL;u=u->next) {
    if (u->type==CLONE_TYPE && u->owner==user) {
      if (++cnt>1) {
	write_user(user,"You have more than one clone active.  Please specify a room to switch to.\n");
	return;
        }
      rm=u->room;
      tu=u;
      }
    }
  if (!cnt) {
    write_user(user,"You do not currently have any active clones.\n");
    return;
    }
  write_user(user,"\n~FB~OLYou experience a strange sensation...\n");
  tu->room=user->room;
  user->room=rm;
  sprintf(text,"The clone of %s comes alive!\n",user->recap);
  write_room_except(user->room,text,user);
  sprintf(text,"%s turns into a clone!\n",tu->recap);
  write_room_except(tu->room,text,tu);
  look(user);
  return;
  }

/* if a room name was given then try to switch to a clone there */

if ((rm=get_room(word[1]))==NULL) {
  write_user(user,nosuchroom);  return;
 }
for(u=user_first;u!=NULL;u=u->next) {
  if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) {
    write_user(user,"\n~FB~OLYou experience a strange sensation...\n");
    u->room=user->room;
    user->room=rm;
    sprintf(text,"The clone of %s comes alive!\n",u->name);
    write_room_except(user->room,text,user);
    sprintf(text,"%s turns into a clone!\n",u->recap);
    write_room_except(u->room,text,u);
    look(user);
    return;
    }
  }
write_user(user,"You do not have a clone in that room.\n");
}


/*** Make a clone speak ***/
void clone_say(UR_OBJECT user, char *inpstr)
{
RM_OBJECT rm;
UR_OBJECT u;

if (user->muzzled) {
  write_user(user,"You are muzzled, your clone cannot speak.\n");
  return;
  }
if (word_count<3) {
  write_user(user,"Usage: csay <room clone is in> <message>\n");
  return;
  }
if ((rm=get_room(word[1]))==NULL) {
  write_user(user,nosuchroom);  return;
  }
for(u=user_first;u!=NULL;u=u->next) {
  if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) {
    say(u,remove_first(inpstr));  return;
    }
  }
write_user(user,"You do not have a clone in that room.\n");
}


/*** Make a clone emote ***/
void clone_emote(UR_OBJECT user,char *inpstr)
{
RM_OBJECT rm;
UR_OBJECT u;

if (user->muzzled>1) {
  write_user(user,"You are muzzled, your clone cannot emote.\n");
  return;
  }
if (word_count<3) {
  write_user(user,"Usage: cemote <room clone is in> <message>\n");
  return;
  }
if ((rm=get_room(word[1]))==NULL) {
  write_user(user,nosuchroom);  return;
  }
for(u=user_first;u!=NULL;u=u->next) {
  if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) {
    emote(u,remove_first(inpstr));  return;
    }
  }
write_user(user,"You do not have a clone in that room.\n");
}


/*** Set what a clone will hear, either all speach , just bad language
	or nothing. ***/
void clone_hear(UR_OBJECT user)
{
RM_OBJECT rm;
UR_OBJECT u;

if (word_count<3  
    || (strcmp(word[2],"all") 
    && strcmp(word[2],"swears") 
    && strcmp(word[2],"nothing"))) {
  write_user(user,"Usage: chear <room clone is in> all/swears/nothing\n");
  return;
  }
if ((rm=get_room(word[1]))==NULL) {
  write_user(user,nosuchroom);  return;
  }
for(u=user_first;u!=NULL;u=u->next) {
  if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) break;
  }
if (u==NULL) {
  write_user(user,"You do not have a clone in that room.\n");
  return;
  }
if (!strcmp(word[2],"all")) {
  u->clone_hear=CLONE_HEAR_ALL;
  write_user(user,"Clone will now hear everything.\n");
  return;
  }
if (!strcmp(word[2],"swears")) {
  u->clone_hear=CLONE_HEAR_SWEARS;
  write_user(user,"Clone will now only hear swearing.\n");
  return;
  }
u->clone_hear=CLONE_HEAR_NOTHING;
write_user(user,"Clone will now hear nothing.\n");
}


/*** Move to another room ***/
void go(UR_OBJECT user)
{
RM_OBJECT rm;
#ifdef NETLINKS
  NL_OBJECT nl;
#endif
int i;

if (user->lroom==2) {
  write_user(user,"You have been shackled and cannot move.\n");
  return;
  }
if (word_count<2) {
  if (!(rm=get_room(default_warp))) {
    write_user(user,"You cannot warp to the main room at this time.\n");
    return;
    }
  if (user->room==rm) {
    sprintf(text,"You are already in the %s!\n",default_warp);
    write_user(user,text);
    return;
    }
  move_user(user,rm,1);
  return;
  }
#ifdef NETLINKS
  nl=user->room->netlink;
  if (nl!=NULL && !strncmp(nl->service,word[1],strlen(word[1]))) {
    if (user->pot_netlink==nl) {
      write_user(user,"The remote service may be lagged, please be patient...\n");
      return;
      }
    rm=user->room;
    if (nl->stage<2) {
      write_user(user,"The netlink is inactive.\n");
      return;
      }
    if (nl->allow==IN && user->netlink!=nl) {
      /* Link for incoming users only */
      write_user(user,"Sorry, link is for incoming users only.\n");
      return;
      }
    /* If site is users home site then tell home system that we have removed
       him. */
    if (user->netlink==nl) {
      write_user(user,"~FB~OLYou traverse cyberspace...\n");
      sprintf(text,"REMVD %s\n",user->name);
      write_sock(nl->socket,text);
      if (user->vis) {
	sprintf(text,"%s goes to the %s\n",user->recap,nl->service);
	write_room_except(rm,text,user);
        }
      else write_room_except(rm,invisleave,user);
      sprintf(text,"NETLINK: Remote user %s removed.\n",user->name);
      write_syslog(text,1,NETLOG);
      destroy_user_clones(user);
      destruct_user(user);
      reset_access(rm);
      num_of_users--;
      no_prompt=1;
      return;
      }
    /* Can't let remote user jump to yet another remote site because this will 
       reset his user->netlink value and so we will lose his original link.
       2 netlink pointers are needed in the user structure to allow this
       but it means way too much rehacking of the code and I don't have the 
       time or inclination to do it */
    if (user->type==REMOTE_TYPE) {
      write_user(user,"Sorry, due to software limitations you can only traverse one netlink.\n");
      return;
      }
    if (nl->ver_major<=3 && nl->ver_minor<=3 && nl->ver_patch<1) {
      if (!word[2][0]) 
	sprintf(text,"TRANS %s %s %s\n",user->name,user->pass,user->desc);
      else sprintf(text,"TRANS %s %s %s\n",user->name,(char *)crypt(word[2],crypt_salt),user->desc);
      }
    else {
      if (!word[2][0]) 
	sprintf(text,"TRANS %s %s %d %s\n",user->name,user->pass,user->level,user->desc);
      else sprintf(text,"TRANS %s %s %d %s\n",user->name,(char *)crypt(word[2],crypt_salt),user->level,user->desc);
      }
    write_sock(nl->socket,text);
    user->remote_com=GO;
    user->pot_netlink=nl;  /* potential netlink */
    no_prompt=1;
    return;
    }
  /* If someone tries to go somewhere else while waiting to go to a talker
     send the other site a release message */
  if (user->remote_com==GO) {
    sprintf(text,"REL %s\n",user->name);
    write_sock(user->pot_netlink->socket,text);
    user->remote_com=-1;
    user->pot_netlink=NULL;
    }
#endif
if ((rm=get_room(word[1]))==NULL) {
  write_user(user,nosuchroom);  return;
  }
if (rm==user->room) {
  sprintf(text,"You are already in the %s!\n",rm->name);
  write_user(user,text);
  return;
  }

/* See if link from current room */
for(i=0;i<MAX_LINKS;++i) {
  if (user->room->link[i]==rm) {
    move_user(user,rm,0);  return;
    }
  }
if (user->level<WIZ) {
  sprintf(text,"The %s is not adjoined to here.\n",rm->name);
  write_user(user,text);  
  return;
  }
if (rm->access==PERSONAL_UNLOCKED || rm->access==PERSONAL_LOCKED) {
  write_user(user,"To go to another user's home you must .visit them.\n");
  return;
  }
move_user(user,rm,1);
}


/*** Called by go() and move() ***/
void move_user(UR_OBJECT user, RM_OBJECT rm, int teleport)
{
RM_OBJECT old_room;

old_room=user->room;
if (teleport!=2 && !has_room_access(user,rm)) {
  write_user(user,"That room is currently private, you cannot enter.\n");  
  return;
  }
/* Reset invite room if in it */
if (user->invite_room==rm) user->invite_room=NULL;
if (!user->vis) {
  write_room(rm,invisenter);
  write_room_except(user->room,invisleave,user);
  goto SKIP;
  }
if (teleport==1) {
  sprintf(text,"~FT~OL%s appears in an explosion of blue magic!\n",user->recap);
  write_room(rm,text);
  sprintf(text,"~FT~OL%s chants a spell and vanishes into a magical blue vortex!\n",user->recap);
  write_room_except(old_room,text,user);
  goto SKIP;
  }
if (teleport==2) {
  write_user(user,"\n~FT~OLA giant hand grabs you and pulls you into a magical blue vortex!\n");
  sprintf(text,"~FT~OL%s falls out of a magical blue vortex!\n",user->recap);
  write_room(rm,text);
  #ifdef NETLINKS
    if (old_room==NULL) {
      sprintf(text,"REL %s\n",user->name);
      write_sock(user->netlink->socket,text);
      user->netlink=NULL;
      }
    else {
  #endif
      sprintf(text,"~FT~OLA giant hand grabs %s who is pulled into a magical blue vortex!\n",user->recap);
      write_room_except(old_room,text,user);
  #ifdef NETLINKS
      }
  #endif
  goto SKIP;
  }
sprintf(text,"%s %s.\n",user->recap,user->in_phrase);
write_room(rm,text);
sprintf(text,"%s %s to the %s.\n",user->recap,user->out_phrase,rm->name);
write_room_except(user->room,text,user);

SKIP:
user->room=rm;
look(user);
reset_access(old_room);
}


/*** Wizard moves a user to another room ***/
void move(UR_OBJECT user)
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;

if (word_count<2) {
  write_user(user,"Usage: move <user> [<room>]\n");  return;
  }
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);  return;
  }
if (word_count<3) rm=user->room;
else {
  if ((rm=get_room(word[2]))==NULL) {
    write_user(user,nosuchroom);  return;
    }
  }
if (user==u) {
  write_user(user,"Trying to move yourself this way is the fourth sign of madness.\n");  return;
  }
if (u->level>=user->level) {
  write_user(user,"You cannot move a user of equal or higher level than yourself.\n");
  return;
  }
if (rm==u->room) {
  sprintf(text,"%s is already in the %s.\n",u->recap,rm->name);
  write_user(user,text);
  return;
  };
if (!has_room_access(user,rm)) {
  sprintf(text,"The %s is currently private, %s cannot be moved there.\n",rm->name,u->recap);
  write_user(user,text);  
  return;
  }
write_user(user,"~FT~OLYou chant an ancient spell...\n");
if (user->vis) name=user->recap; else name=invisname;
sprintf(text,"~FT~OL%s chants an ancient spell...\n",name);
write_room_except(user->room,text,user);
move_user(u,rm,2);
prompt(u);
}


/*** Set rooms to public or private ***/
void set_room_access(UR_OBJECT user)
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;
int cnt;

rm=user->room;
if (word_count<2) rm=user->room;
else {
  if (user->level<gatecrash_level) {
    write_user(user,"You are not a high enough level to use the room option.\n");  
    return;
    }
  if ((rm=get_room(word[1]))==NULL) {
    write_user(user,nosuchroom);  return;
    }
  }
if (user->vis) name=user->recap; else name=invisname;
if (rm->access>PRIVATE) {
  if (rm==user->room) 
    write_user(user,"This room's access is fixed.\n"); 
  else write_user(user,"That room's access is fixed.\n");
  return;
  }
if (com_num==PUBCOM && rm->access==PUBLIC) {
  if (rm==user->room) 
    write_user(user,"This room is already public.\n");  
  else write_user(user,"That room is already public.\n"); 
  return;
  }
if (user->vis) name=user->recap; else name=invisname;
if (com_num==PRIVCOM) {
  if (rm->access==PRIVATE) {
    if (rm==user->room) 
      write_user(user,"This room is already private.\n");  
    else write_user(user,"That room is already private.\n"); 
    return;
    }
  cnt=0;
  for(u=user_first;u!=NULL;u=u->next) if (u->room==rm) ++cnt;
  if (cnt<min_private_users && user->level<ignore_mp_level) {
    sprintf(text,"You need at least %d users/clones in a room before it can be made private.\n",min_private_users);
    write_user(user,text);
    return;
    }
  write_user(user,"Room set to ~FRPRIVATE.\n");
  if (rm==user->room) {
    sprintf(text,"%s has set the room to ~FRPRIVATE.\n",name);
    write_room_except(rm,text,user);
    }
  else write_room(rm,"This room has been set to ~FRPRIVATE.\n");
  rm->access=PRIVATE;
  return;
  }
write_user(user,"Room set to ~FGPUBLIC.\n");
if (rm==user->room) {
  sprintf(text,"%s has set the room to ~FGPUBLIC.\n",name);
  write_room_except(rm,text,user);
  }
else write_room(rm,"This room has been set to ~FGPUBLIC.\n");
rm->access=PUBLIC;

/* Reset any invites into the room & clear review buffer */
for(u=user_first;u!=NULL;u=u->next) {
  if (u->invite_room==rm) u->invite_room=NULL;
  }
clear_revbuff(rm);
}


/*** Change whether a rooms access is fixed or not ***/
void change_room_fix(UR_OBJECT user, int fix)
{
RM_OBJECT rm;
char *name;

if (word_count<2) rm=user->room;
else {
  if ((rm=get_room(word[1]))==NULL) {
    write_user(user,nosuchroom);  return;
    }
  }
if (user->vis) name=user->recap; else name=invisname;
if (fix) {	
  if (rm->access & FIXED) {
    if (rm==user->room) 
      write_user(user,"This room's access is already fixed.\n");
    else write_user(user,"That room's access is already fixed.\n");
    return;
    }
  sprintf(text,"Access for room %s is now ~FRFIXED.\n",rm->name);
  write_user(user,text);
  if (user->room==rm) {
    sprintf(text,"%s has ~FRFIXED~RS access for this room.\n",name);
    write_room_except(rm,text,user);
    }
  else {
    sprintf(text,"This room's access has been ~FRFIXED.\n");
    write_room(rm,text);
    }
  sprintf(text,"%s FIXED access to room %s.\n",user->name,rm->name);
  write_syslog(text,1,SYSLOG);
  rm->access+=2;
  return;
  }
if (!(rm->access & FIXED)) {
  if (rm==user->room) 
    write_user(user,"This room's access is already unfixed.\n");
  else write_user(user,"That room's access is already unfixed.\n");
  return;
  }
sprintf(text,"Access for room %s is now ~FGUNFIXED.\n",rm->name);
write_user(user,text);
if (user->room==rm) {
  sprintf(text,"%s has ~FGUNFIXED~RS access for this room.\n",name);
  write_room_except(rm,text,user);
  }
else {
  sprintf(text,"This room's access has been ~FGUNFIXED.\n");
  write_room(rm,text);
  }
sprintf(text,"%s UNFIXED access to room %s.\n",user->name,rm->name);
write_syslog(text,1,SYSLOG);
rm->access-=2;
reset_access(rm);
}


/*** Invite a user into a private room ***/
void invite(UR_OBJECT user)
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;

if (word_count<2) {
  write_user(user,"Invite who?\n");  return;
  }
rm=user->room;
if (!(rm->access & PRIVATE)) {
  write_user(user,"This room is currently public.\n");
  return;
  }
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);  return;
  }
if (u==user) {
  write_user(user,"Inviting yourself to somewhere is the third sign of madness.\n");
  return;
  }
if (u->room==rm) {
  sprintf(text,"%s is already here!\n",u->recap);
  write_user(user,text);
  return;
  }
if (u->invite_room==rm) {
  sprintf(text,"%s has already been invited into here.\n",u->recap);
  write_user(user,text);
  return;
  }
sprintf(text,"You invite %s in.\n",u->recap);
write_user(user,text);
if (user->vis || u->level>=user->level) name=user->recap; else name=invisname;
sprintf(text,"%s has invited you into the %s.\n",name,rm->name);
write_user(u,text);
u->invite_room=rm;
strcpy(u->invite_by,user->name);
}


/* no longer invite a user to the room you're in if you invited them */
void uninvite(UR_OBJECT user)
{
UR_OBJECT u;
char *name;

if (word_count<2) {
  write_user(user,"Uninvite who?\n");  return;
  }
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);  return;
  }
if (u==user) {
  write_user(user,"You cannot uninvite yourself.\n");
  return;
  }
if (u->invite_room==NULL) {
  sprintf(text,"%s has not been invited anywhere.\n",u->recap);
  write_user(user,text);
  return;
  }
if (strcmp(u->invite_by,user->name)) {
  sprintf(text,"%s has not been invited anywhere by you!\n",u->recap);
  write_user(user,text);
  return;
  }
sprintf(text,"You cancel your invitation to %s.\n",u->name);
write_user(user,text);
if (user->vis || u->level>=user->level) name=user->recap; else name=invisname;
sprintf(text,"%s cancels your invitation.\n",name);
write_user(u,text);
u->invite_room=NULL;
u->invite_by[0]='\0';
}


/*** Ask to be let into a private room ***/
void letmein(UR_OBJECT user)
{
RM_OBJECT rm;
int i;

if (word_count<2) {
  write_user(user,"Knock on what door?\n");  return;
  }
if ((rm=get_room(word[1]))==NULL) {
  write_user(user,nosuchroom);  return;
  }
if (rm==user->room) {
  sprintf(text,"You are already in the %s!\n",rm->name);
  write_user(user,text);
  return;
  }
for(i=0;i<MAX_LINKS;++i) 
  if (user->room->link[i]==rm) goto GOT_IT;
sprintf(text,"The %s is not adjoined to here.\n",rm->name);
write_user(user,text);  
return;

GOT_IT:
if (!(rm->access & PRIVATE)) {
  sprintf(text,"The %s is currently public.\n",rm->name);
  write_user(user,text);
  return;
  }
sprintf(text,"You knock asking to be let into the %s.\n",rm->name);
write_user(user,text);
sprintf(text,"%s knocks asking to be let into the %s.\n",user->recap,rm->name);
write_room_except(user->room,text,user);
sprintf(text,"%s knocks asking to be let in.\n",user->recap);
write_room(rm,text);
}


/*** Show talker rooms ***/
void rooms(UR_OBJECT user, int show_topics, int wrap)
{
RM_OBJECT rm;
UR_OBJECT u;
#ifdef NETLINKS
  NL_OBJECT nl;
  char serv[SERV_NAME_LEN+1],stat[9];
#endif
char rmaccess[9];
int cnt,rm_cnt,rm_pub,rm_priv;

if (word_count<2) {
  if (!wrap) user->wrap_room=room_first;
  if (show_topics) 
    write_user(user,"\n~FTRoom name            : Access  Users  Mesgs  Topic\n\n");
  else write_user(user,"\n~FTRoom name            : Access  Users  Mesgs  Inlink  LStat  Service\n\n");
  rm_cnt=0;
  for(rm=user->wrap_room;rm!=NULL;rm=rm->next) {
    if (rm->access==PERSONAL_UNLOCKED || rm->access==PERSONAL_LOCKED) continue;
    if (rm_cnt==user->pager-4) {   /* -4 for the 'Room name...' header */
      switch (show_topics) {
	case 0: user->misc_op=10; break;
	case 1: user->misc_op=11; break;
	}
      write_user(user,"~BB~FG-=[*]=- PRESS <RETURN>, E TO EXIT:~RS ");
      return;
      }
    if (rm->access & PRIVATE) strcpy(rmaccess," ~FRPRIV");
    else strcpy(rmaccess,"  ~FGPUB");
    if (rm->access & FIXED) rmaccess[0]='*';
    cnt=0;
    for(u=user_first;u!=NULL;u=u->next) 
      if (u->type!=CLONE_TYPE && u->room==rm) ++cnt;
    if (show_topics)
      sprintf(text,"%-20s : %9s~RS    %3d    %3d  %s\n",rm->name,rmaccess,cnt,rm->mesg_cnt,rm->topic);
  #ifdef NETLINKS
    else {
      nl=rm->netlink;  serv[0]='\0';
      if (nl==NULL) {
	if (rm->inlink) strcpy(stat,"~FRDOWN");
	else strcpy(stat,"   -");
        }
      else {
	if (nl->type==UNCONNECTED) strcpy(stat,"~FRDOWN");
	else if (nl->stage==UP) strcpy(stat,"  ~FGUP");
	else strcpy(stat," ~FYVER");
        }
      if (nl!=NULL) strcpy(serv,nl->service);
      sprintf(text,"%-20s : %9s~RS    %3d    %3d     %s   %s~RS  %s\n",rm->name,rmaccess,cnt,rm->mesg_cnt,noyes1[rm->inlink],stat,serv);
      }
  #endif
    write_user(user,text);
    ++rm_cnt;  user->wrap_room=rm->next;
    }
  user->misc_op=0;
  rm_pub=rm_priv=0;
  for (rm=room_first;rm!=NULL;rm=rm->next) {
    if (rm->access==PERSONAL_UNLOCKED || rm->access==PERSONAL_LOCKED) continue;
    if (rm->access & PRIVATE) ++rm_priv;
    else ++rm_pub;
    }
  sprintf(text,"\nThere is a total of %d rooms.  %d %s public, and %d %s private.\n\n",rm_priv+rm_pub,rm_pub,PLTEXT_IS(rm_pub),rm_priv,PLTEXT_IS(rm_priv));
  write_user(user,text);
  return;
  }
strtoupper(word[1]);
rm_cnt=0;  cnt=0;
if (!strcmp(word[1],"LEVEL")) {
  write_user(user,"The following rooms are default...\n\n");
  sprintf(text,"Default main room : ~OL%s~RS\n",room_first->name);
  write_user(user,text);
  sprintf(text,"Default warp room : ~OL%s~RS\n",default_warp);
  write_user(user,text);
  sprintf(text,"Default jail room : ~OL%s~RS\n",default_jail);
  write_user(user,text);
  while(priv_room[rm_cnt].name[0]!='*') {
    if (++cnt==1) write_user(user,"\nThe following rooms are level specific...\n\n");
    sprintf(text,"~FT%s~RS is for users of level ~OL%s~RS and above.\n",priv_room[rm_cnt].name,level_name[priv_room[rm_cnt].level]);
    write_user(user,text);
    ++rm_cnt;
    }
  if (cnt) {
    sprintf(text,"\nThere is a total of ~OL%d~RS level specific rooms.\n\n",rm_cnt);
    write_user(user,text);
    }
  else write_user(user,"\nThere are no level specific rooms currently availiable.\n\n");
  return;
  }
write_user(user,"Usage: rooms [level]\n");
}


/* clears a room topic */
void clear_topic(UR_OBJECT user)
{
RM_OBJECT rm;
char *name;

strtolower(word[1]);
if (word_count<2) {
  rm=user->room;
  rm->topic[0]='\0';
  write_user(user,"Topic has been cleared\n");
  if (user->vis) name=user->name; else name=invisname;
  sprintf(text,"~FY~OL%s has cleared the topic.\n",name);
  write_room_except(rm,text,user);
  return;
  }
if (!strcmp(word[1],"all")) {
  if (user->level>command_table[CTOPIC].level || user->level>=ARCH) {
    for(rm=room_first;rm!=NULL;rm=rm->next) {
      rm->topic[0]='\0';
      write_room_except(rm,"\n~FY~OLThe topic has been cleared.\n",user);
      }
    write_user(user,"All room topics have now been cleared\n");
    return;
    }
  write_user(user,"You can only clear the topic of the room you are in.\n");
  return;
  }
write_user(user,"Usage: ctopic [all]\n");
}


/*** Join a user in another room ***/
void join(UR_OBJECT user)
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;

if (word_count<2) {
  write_user(user,"Usage: join <user>\n");  
  return;
  }
if (user->lroom==2) {
  write_user(user,"You have been shackled and cannot move.\n");
  return;
  }
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);
  return;
  }
if (user==u) {
  write_user(user,"You really want join yourself!  What would the neighbours think?\n");
  return;
  }
rm=u->room;
#ifdef NETLINKS
  if (rm==NULL) {
    sprintf(text,"%s is currently off site so you cannot join them.\n",u->name);
    write_user(user,text);
    return;
    }
#endif
if (rm==user->room) {
  sprintf(text,"You are already with %s in the %s.\n",u->recap,rm->name);
  write_user(user,text);
  return;
  };
if (!has_room_access(user,rm)) {
  sprintf(text,"That room is currently private, you cannot join %s there.\n",u->recap);
  write_user(user,text);
  return;
  }
if (user->vis) name=user->recap; else name=invisname;
sprintf(text,"~FT~OLYou join %s in the %s.~RS\n",u->name,u->room->name);
write_user(user,text);
if (user->level<GOD || user->vis) {
  sprintf(text,"%s %s\n",name,user->out_phrase);
  write_room_except(user->room,text,user);
  sprintf(text,"%s %s\n",name,user->in_phrase);
  write_room_except(rm,text,user);
  }
user->room=rm;
look(user);
}


/* Stop a user from using the go command and leaving the room they are currently in */
void shackle(UR_OBJECT user)
{
UR_OBJECT u;

if (word_count<2) {
  write_user(user,"Usage: shackle <user>\n");  
  return;
  }
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);
  return;
  }
if (user==u) {
  write_user(user,"You cannot shackle yourself!\n");
  return;
  }
#ifdef NETLINKS
  if (u->room==NULL) {
    sprintf(text,"%s is currently off site and cannot be shackled there.\n",u->name);
    write_user(user,text);
    return;
    }
#endif
if (u->level>=user->level) {
  write_user(user,"You cannot shackle someone of the same or higher level as yourself.\n");
  return;
  }
if (u->lroom==2) {
  sprintf(text,"%s has already been shackled.\n",u->recap);
  write_user(user,text);
  return;
  }
u->lroom=2;
sprintf(text,"\n~FR~OLYou have been shackled to the %s room.\n",u->room->name);
write_user(u,text);
sprintf(text,"~FR~OLYou shackled %s to the %s room.\n",u->recap,u->room->name);
write_user(user,text);
sprintf(text,"~FRShackled~RS to the ~FB%s~RS room by ~FB~OL%s~RS.\n",u->room->name,user->name);
add_history(u->name,1,text);
sprintf(text,"%s SHACKLED %s to the room: %s\n",user->name,u->name,u->room->name);
write_syslog(text,1,SYSLOG);
}


/* Allow a user to move between rooms again */
void unshackle(UR_OBJECT user)
{
UR_OBJECT u;

if (word_count<2) {
  write_user(user,"Usage: unshackle <user>\n");  
  return;
  }
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);
  return;
  }
if (user==u) {
  write_user(user,"You cannot unshackle yourself!\n");
  return;
  }
if (u->lroom!=2) {
  sprintf(text,"%s in not currently shackled.\n",u->recap);
  write_user(user,text);
  return;
  }
u->lroom=0;
write_user(u,"\n~FG~OLYou have been unshackled.\n");
write_user(u,"You can now use the ~FTset~RS command to alter the ~FBroom~RS attribute.\n");
sprintf(text,"~FG~OLYou unshackled %s from the %s room.\n",u->recap,u->room->name);
write_user(user,text);
sprintf(text,"~FGUnshackled~RS from the ~FB%s~RS room by ~FB~OL%s~RS.\n",u->room->name,user->name);
add_history(u->name,1,text);
sprintf(text,"%s UNSHACKLED %s from the room: %s\n",user->name,u->name,u->room->name);
write_syslog(text,1,SYSLOG);
}


/*** Set the room topic ***/
void set_topic(UR_OBJECT user, char *inpstr)
{
RM_OBJECT rm;
char *name;

rm=user->room;
if (word_count<2) {
  if (!strlen(rm->topic)) {
    write_user(user,"No topic has been set yet.\n");  return;
    }
  sprintf(text,"The current topic is: %s\n",rm->topic);
  write_user(user,text);
  return;
  }
if (strlen(inpstr)>TOPIC_LEN) {
  write_user(user,"Topic too long.\n");  return;
  }
sprintf(text,"Topic set to: %s\n",inpstr);
write_user(user,text);
if (user->vis) name=user->recap; else name=invisname;
sprintf(text,"%s has set the topic to: %s\n",name,inpstr);
write_room_except(rm,text,user);
strcpy(rm->topic,inpstr);
}


/*** Auto promote a user if they need to be and auto_promote is turned on ***/
void check_autopromote(UR_OBJECT user, int attrib)
{
int num=0;

if (user->level!=NEW) return; /* if user isn't NEW then don't promote */
if (user->accreq==1) return;  /* if already been promoted then accreq should be set */

num=user->accreq+attrib;
/* stop them from using same command twice */
if ((num==(ACCREQ*2)) || (num==(DESC*2)) || (num==(SET*2))) return;
if ((num==ACCREQ) || (num==DESC) || (num==SET)) {
  user->accreq=num;
  write_user(user,"\n~OL~FY*** You have completed step 1 of 3 for auto-promotion ***\n\n");
  return;
  }
else if ((num==(ACCREQ+DESC)) || (num==(ACCREQ+SET)) || (num==(DESC+SET))) {
  user->accreq=num;
  write_user(user,"\n~OL~FY*** You have completed step 2 of 3 for auto-promotion ***\n\n");
  return;
  }
else if (num==(ACCREQ+DESC+SET)) {
  user->accreq=1;
  --level_count[user->level];
  user->level++;  user->unarrest=user->level;
  user_list_level(user->name,user->level);
  strcpy(user->date,(long_date(1)));
  ++level_count[user->level];
  sprintf(text,"\n\07~OL~FY*** You have been auto-promoted to level %s ***\n\n",level_name[user->level]);
  write_user(user,text);
  sprintf(text,"Was auto-promoted to level %s.\n",level_name[user->level]);
  add_history(user->name,1,text);
  sprintf(text,"%s was AUTO-PROMOTED to level %s.\n",user->name,level_name[user->level]);
  write_syslog(text,1,SYSLOG);
  sprintf(text,"~OL[ AUTO-PROMOTE ]~RS %s to level %s.\n",user->name,level_name[user->level]);
  write_level(WIZ,1,text,NULL);
  return;
  }
}


/*** Promote a user ***/
void promote(UR_OBJECT user)
{
UR_OBJECT u;
char text2[80],*name;
int level;

level=-1;
if (word_count<2) {
  write_user(user,"Usage: promote <user> [<level>]\n");  return;
  }
if (word_count>3) {
  write_user(user,"Usage: promote <user> [<level>]\n");  return;
  }
if (word_count==3) {
  strtoupper(word[2]);
  if ((level=get_level(word[2]))==-1) {
    write_user(user,"You must select a level between USER and GOD.\n");
    return;
    }
  }
/* See if user is on atm */
if ((u=get_user(word[1]))!=NULL) {
  /* first runs checks if level option was used */
  if (word_count==3 && level<=u->level) {
    write_user(user,"You cannot promote a user to a level less than or equal to what they are now.\n");
    return;
    }
  if (word_count==3 && level>user->level) {
    write_user(user,"You cannot promote a user to a level higher than your own.\n");
    return;
    }
  /* now normal checks */
  if (u->level>=user->level) {
    write_user(user,"You cannot promote a user to a level higher than your own.\n");
    return;
    }
  if (u->level==JAILED) {
    write_user(user,"You cannot promote a user of level JAILED.\n");
    return;
    }
  if (user->vis) name=user->recap; else name=invisname;
  if (u->level>=WIZ) rem_wiz_node(u->name);
  --level_count[u->level];
  (word_count==3)?u->level=level:u->level++; 
  u->unarrest=u->level;
  user_list_level(u->name,u->level);
  strcpy(u->date,(long_date(1)));
  ++level_count[u->level];
  if (u->level>=WIZ) add_wiz_node(u->name,u->level);
  sprintf(text,"~FG~OL%s is promoted to level: ~RS~OL%s.\n",u->recap,level_name[u->level]);
  write_level(u->level,1,text,u);
  sprintf(text,"~FG~OL%s has promoted you to level: ~RS~OL%s!\n",name,level_name[u->level]);
  write_user(u,text);
  sprintf(text,"%s PROMOTED %s to level %s.\n",user->name,u->name,level_name[u->level]);
  write_syslog(text,1,SYSLOG);
  sprintf(text,"Was ~FGpromoted~RS by %s to level %s.\n",user->name,level_name[u->level]);
  add_history(u->name,1,text);
  u->accreq=1;
  add_user_date_node(u->name,(long_date(1)));
  return;
  }
/* Create a temp session, load details, alter , then save. This is inefficient
   but its simpler than the alternative */
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user object.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user object in promote().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
  write_user(user,nosuchuser);  
  destruct_user(u);
  destructed=0;
  return;
  }
if (word_count==3 && level<=u->level) {
  write_user(user,"You cannot promote a user to a level less than or equal to what they are now.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
if (word_count==3 && level>user->level) {
  write_user(user,"You cannot promote a user to a level higher than your own.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
  /* now normal checks */
if (u->level>=user->level) {
  write_user(user,"You cannot promote a user to a level higher than your own.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->level==JAILED) {
  write_user(user,"You cannot promote a user of level JAILED.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->level>=WIZ) rem_wiz_node(u->name);
--level_count[u->level];
(word_count==3)?u->level=level:u->level++;
u->unarrest=u->level;
user_list_level(u->name,u->level);
++level_count[u->level];
if (u->level>=WIZ) add_wiz_node(u->name,u->level);
u->socket=-2;
u->accreq=1;
sprintf(text,"%s",long_date(1));
strcpy(u->date,text);
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"You promote %s to level: ~OL%s.\n",u->name,level_name[u->level]);
write_user(user,text);
sprintf(text2,"~FG~OLYou have been promoted to level: ~RS~OL%s.\n",level_name[u->level]);
send_mail(user,word[1],text2,0);
sprintf(text,"%s PROMOTED %s to level %s.\n",user->name,word[1],level_name[u->level]);
write_syslog(text,1,SYSLOG);
sprintf(text,"Was ~FGpromoted~RS by %s to level %s.\n",user->name,level_name[u->level]);
add_history(u->name,1,text);
add_user_date_node(u->name,(long_date(1)));
destruct_user(u);
destructed=0;
}


/*** Demote a user ***/
void demote(UR_OBJECT user)
{
UR_OBJECT u;
char text2[80],*name;
int level;

level=-1;
if (word_count<2) {
  write_user(user,"Usage: demote <user> [<level>]\n");  return;
  }
if (word_count>3) {
  write_user(user,"Usage: demote <user> [<level>]\n");  return;
  }
if (word_count==3) {
  strtoupper(word[2]);
  if ((level=get_level(word[2]))==-1) {
    write_user(user,"You must select a level between NEW and ARCH.\n");
    return;
    }
  }
/* See if user is on atm */
if ((u=get_user(word[1]))!=NULL) {
  /* first runs checks if level option was used */
  if (word_count==3 && level>=u->level) {
    write_user(user,"You cannot demote a user to a level higher than or equal to what they are now.\n");
    return;
    }
  if (word_count==3 && level==JAILED) {
    write_user(user,"You cannot demote a user to the level JAILED.\n");
    return;
    }
  /* now normal checks */
  if (u->level<=NEW) {
    write_user(user,"You cannot demote a user of level NEW or JAILED.\n");
    return;
    }
  if (u->level>=user->level) {
    write_user(user,"You cannot demote a user of an equal or higher level than yourself.\n");
    return;
    }
  if (user->vis) name=user->recap; else name=invisname;
  if (u->level>=WIZ) rem_wiz_node(u->name);
  --level_count[u->level];
  /* if a user was a wiz then remove from retire list */
  if (u->level==WIZ) clean_retire_list(u->name);
  /* was a level given? */
  (word_count==3)?u->level=level:u->level--;
  u->unarrest=u->level;
  user_list_level(u->name,u->level);
  ++level_count[u->level];
  if (u->level>=WIZ) add_wiz_node(u->name,u->level);
  strcpy(u->date,(long_date(1)));
  sprintf(text,"~FR~OL%s is demoted to level: ~RS~OL%s.\n",u->recap,level_name[u->level]);
  write_level(u->level,1,text,u);
  sprintf(text,"~FR~OL%s has demoted you to level: ~RS~OL%s!\n",name,level_name[u->level]);
  write_user(u,text);
  sprintf(text,"%s DEMOTED %s to level %s.\n",user->name,u->name,level_name[u->level]);
  write_syslog(text,1,SYSLOG);
  sprintf(text,"Was ~FRdemoted~RS by %s to level %s.\n",user->name,level_name[u->level]);
  add_history(u->name,1,text);
  add_user_date_node(u->name,(long_date(1)));
  u->vis=1;
  return;
  }
/* User not logged on */
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user object.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user object in demote().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
  write_user(user,nosuchuser);  
  destruct_user(u);
  destructed=0;
  return;
  }
/* first runs checks if level option was used */
if (word_count==3 && level>=u->level) {
  write_user(user,"You cannot demote a user to a level higher than or equal to what they are now.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
if (word_count==3 && level==JAILED) {
  write_user(user,"You cannot demote a user to the level JAILED.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
  /* now normal checks */
if (u->level<=NEW) {
  write_user(user,"You cannot demote a user of level NEW or JAILED.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->level>=user->level) {
  write_user(user,"You cannot demote a user of an equal or higher level than yourself.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->level>=WIZ) rem_wiz_node(u->name);
--level_count[u->level];
/* if a user was a wiz then remove from retire list */
if (u->level==WIZ) clean_retire_list(u->name);
/* was a level given? */
(word_count==3)?u->level=level:u->level--;
u->unarrest=u->level;
user_list_level(u->name,u->level);
++level_count[u->level];
if (u->level>=WIZ) add_wiz_node(u->name,u->level);
u->socket=-2;
u->vis=1;
strcpy(u->site,u->last_site);
strcpy(u->date,(long_date(1)));
save_user_details(u,0);
sprintf(text,"You demote %s to level: ~OL%s.\n",u->recap,level_name[u->level]);
write_user(user,text);
sprintf(text2,"~FR~OLYou have been demoted to level: ~RS~OL%s.\n",level_name[u->level]);
send_mail(user,word[1],text2,0);
sprintf(text,"%s DEMOTED %s to level %s.\n",user->name,word[1],level_name[u->level]);
write_syslog(text,1,SYSLOG);
sprintf(text,"Was ~FRdemoted~RS by %s to level %s.\n",user->name,level_name[u->level]);
add_history(u->name,1,text);
add_user_date_node(u->name,(long_date(1)));
destruct_user(u);
destructed=0;
}


/*** Muzzle an annoying user so he cant speak, emote, echo, write, smail
     or bcast. Muzzles have levels from WIZ to GOD so for instance a wiz
     cannot remove a muzzle set by a god.  ***/
void muzzle(UR_OBJECT user)
{
UR_OBJECT u;

if (word_count<2) {
  write_user(user,"Usage: muzzle <user>\n");  return;
  }
if ((u=get_user(word[1]))!=NULL) {
  if (u==user) {
    write_user(user,"Trying to muzzle yourself is the ninth sign of madness.\n");
    return;
    }
  if (u->level>=user->level) {
    write_user(user,"You cannot muzzle a user of equal or higher level than yourself.\n");
    return;
    }
  if (u->muzzled>=user->level) {
    sprintf(text,"%s is already muzzled.\n",u->recap);
    write_user(user,text);  return;
    }
  sprintf(text,"~FR~OL%s now has a muzzle of level: ~RS~OL%s.\n",u->recap,level_name[user->level]);
  write_user(user,text);
  write_user(u,"~FR~OLYou have been muzzled!\n");
  sprintf(text,"%s muzzled %s (level %d).\n",user->name,u->name,user->level);
  write_syslog(text,1,SYSLOG);
  u->muzzled=user->level;
  sprintf(text,"Level %d (%s) ~FRmuzzle~RS put on by %s.\n",user->level,level_name[user->level],user->name);
  add_history(u->name,1,text);
  return;
  }
/* User not logged on */
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user object.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user object in muzzle().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
  write_user(user,nosuchuser);  
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->level>=user->level) {
  write_user(user,"You cannot muzzle a user of equal or higher level than yourself.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->muzzled>=user->level) {
  sprintf(text,"%s is already muzzled.\n",u->name);
  write_user(user,text); 
  destruct_user(u);
  destructed=0;
  return;
  }
u->socket=-2;
u->muzzled=user->level;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"~FR~OL%s given a muzzle of level: ~RS~OL%s.\n",u->recap,level_name[user->level]);
write_user(user,text);
send_mail(user,word[1],"~FR~OLYou have been muzzled!\n",0);
sprintf(text,"%s muzzled %s (level %d).\n",user->name,u->name,user->level);
write_syslog(text,1,SYSLOG);
sprintf(text,"Level %d (%s) ~FRmuzzle~RS put on by %s.\n",user->level,level_name[user->level],user->name);
add_history(u->name,1,text);
destruct_user(u);
destructed=0;
}


/*** Umuzzle the bastard now he's apologised and grovelled enough via email ***/
void unmuzzle(UR_OBJECT user)
{
UR_OBJECT u;

if (word_count<2) {
  write_user(user,"Usage: unmuzzle <user>\n");  return;
  }
if ((u=get_user(word[1]))!=NULL) {
  if (u==user) {
    write_user(user,"Trying to unmuzzle yourself is the tenth sign of madness.\n");
    return;
    }
  if (!u->muzzled) {
    sprintf(text,"%s is not muzzled.\n",u->recap);  return;
    }
  if (u->muzzled>user->level) {
    sprintf(text,"%s's muzzle is set to level %s, you do not have the power to remove it.\n",u->name,level_name[u->muzzled]);
    write_user(user,text);  return;
    }
  sprintf(text,"~FG~OLYou remove %s's muzzle.\n",u->name);
  write_user(user,text);
  write_user(u,"~FG~OLYou have been unmuzzled!\n");
  sprintf(text,"%s unmuzzled %s.\n",user->name,u->name);
  write_syslog(text,1,SYSLOG);
  u->muzzled=0;
  sprintf(text,"~FGUnmuzzled~RS by %s, level %d (%s).\n",user->name,user->level,level_name[user->level]);
  add_history(u->name,0,text);
  return;
  }
/* User not logged on */
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user object.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user object in unmuzzle().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
  write_user(user,nosuchuser);  
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->muzzled>user->level) {
  sprintf(text,"%s's muzzle is set to level %s, you do not have the power to remove it.\n",u->name,level_name[u->muzzled]);
  write_user(user,text);  
  destruct_user(u);
  destructed=0;
  return;
  }
u->socket=-2;
u->muzzled=0;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"~FG~OLYou remove %s's muzzle.\n",u->recap);
write_user(user,text);
send_mail(user,word[1],"~FG~OLYou have been unmuzzled.\n",0);
sprintf(text,"%s unmuzzled %s.\n",user->name,u->name);
write_syslog(text,1,SYSLOG);
sprintf(text,"~FGUnmuzzled~RS by %s, level %d (%s).\n",user->name,user->level,level_name[user->level]);
add_history(u->name,0,text);
destruct_user(u);
destructed=0;
}


/* Put annoying user in jail */
void arrest(UR_OBJECT user)
{
UR_OBJECT u;
RM_OBJECT rm;

if (word_count<2) {
  write_user(user,"Usage: arrest <user>\n");  return;
  }
word[1][0]=toupper(word[1][0]);
if ((u=get_user(word[1]))) {
  if (u==user) {
    write_user(user,"You cannot arrest yourself.\n");
    return;
    }
  if (u->level>=user->level) {
    write_user(user,"You cannot arrest anyone of the same or higher level than yourself.\n");
    return;
    }
  if (u->level==JAILED) {
    sprintf(text,"%s has already been arrested.\n",u->recap);
    write_user(user,text);  return;
    }
  u->vis=1;
  u->unarrest=u->level;
  u->arrestby=user->level;
  --level_count[u->level];
  u->level=JAILED;
  user_list_level(u->name,u->level);
  strcpy(u->date,(long_date(1)));
  ++level_count[u->level];
  rm=get_room(default_jail);
  write_room(NULL,"The Hand of Justice reaches through the air...\n");
  write_user(u,"You have been placed in jail.\n");
  if (rm==NULL) {
    sprintf(text,"Cannot find the jail, so %s is arrested but still in the %s.\n",u->recap,u->room->name);
    write_user(user,text);
    }
  else move_user(u,rm,2);
  sprintf(text,"%s has been placed under arrest...\n",u->recap);
  write_room_except(NULL,text,u);
  sprintf(text,"%s ARRESTED %s (at level %s)\n",user->name,u->name,level_name[u->arrestby]);
  write_syslog(text,1,SYSLOG);
  sprintf(text,"Was ~FRarrested~RS by %s (at level ~OL%s~RS).\n",user->name,level_name[u->arrestby]);
  add_history(u->name,1,text);
  return;
  }
/* Create a temp session, load details, alter , then save. This is inefficient
   but its simpler than the alternative */
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user object.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user object in arrest().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
  write_user(user,nosuchuser);  
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->level>=user->level) {
  write_user(user,"You cannot arrest anyone of the same or higher level than yourself.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->level==JAILED) {
  sprintf(text,"%s has already been arrested.\n",u->recap);
  write_user(user,text);
  destruct_user(u);
  destructed=0;
  return;
  }
u->vis=1;
u->unarrest=u->level;
u->arrestby=user->level;
--level_count[u->level];
u->level=JAILED;
user_list_level(u->name,u->level);
strcpy(u->date,(long_date(1)));
++level_count[u->level];
u->socket=-2;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"You place %s under arrest.\n",u->recap);
write_user(user,text);
sprintf(text,"~OLYou have been placed under ~FRarrest~RS~OL.\n");
send_mail(user,word[1],text,0);
sprintf(text,"%s ARRESTED %s (at level %s).\n",user->name,u->name,level_name[u->arrestby]);
write_syslog(text,1,SYSLOG);
sprintf(text,"Was ~FRarrested~RS by %s (at level ~OL%s~RS).\n",user->name,level_name[u->arrestby]);
add_history(u->name,1,text);
destruct_user(u);
destructed=0;
}


/* Unarrest a user who is currently under arrest/in jail */
void unarrest(UR_OBJECT user)
{
UR_OBJECT u;
RM_OBJECT rm;

if (word_count<2) {
  write_user(user,"Usage: unarrest <user>\n");  return;
  }
word[1][0]=toupper(word[1][0]);
if ((u=get_user(word[1]))) {
  if (u==user) {
    write_user(user,"You cannot unarrest yourself.\n");
    return;
    }
  if (u->level!=JAILED) {
    sprintf(text,"%s is not under arrest!\n",u->recap);
    write_user(user,text);  return;
    }
  if (user->level<u->arrestby) {
    sprintf(text,"%s can only be unarrested by a %s or higher.\n",u->name,level_name[u->arrestby]);
    write_user(user,text);
    return;
    }
  --level_count[u->level];
  u->level=u->unarrest;
  u->arrestby=0;
  user_list_level(u->name,u->level);
  strcpy(u->date,(long_date(1)));
  ++level_count[u->level];
  rm=get_room(default_warp);
  write_room(NULL,"The Hand of Justice reaches through the air...\n");
  write_user(u,"You have been unarrested...  Now try to behave!\n");
  if (rm==NULL) {
    sprintf(text,"Cannot find a room for ex-cons, so %s is still in the %s!\n",u->recap,u->room->name);
    write_user(user,text);
    }
  else move_user(u,rm,2);
  sprintf(text,"%s UNARRESTED %s\n",user->name,u->name);
  write_syslog(text,1,SYSLOG);
  sprintf(text,"Was ~FGunarrested~RS by %s.\n",user->name);
  add_history(u->name,1,text);
  return;
  }
/* Create a temp session, load details, alter , then save. This is inefficient
   but its simpler than the alternative */
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user object.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user object in unarrest().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
  write_user(user,nosuchuser);  
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->level!=JAILED) {
  sprintf(text,"%s is not under arrest!\n",u->recap);
  write_user(user,text);
  destruct_user(u);
  destructed=0;
  return;
  }
if (user->level<u->arrestby) {
  sprintf(text,"%s can only be unarrested by a %s or higher.\n",u->name,level_name[u->arrestby]);
  write_user(user,text);
  destruct_user(u);
  destructed=0;
  return;
  }
--level_count[u->level];
u->level=u->unarrest;
u->arrestby=0;
user_list_level(u->name,u->level);
strcpy(u->date,(long_date(1)));
++level_count[u->level];
u->socket=-2;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"You unarrest %s.\n",u->recap);
write_user(user,text);
sprintf(text,"~OLYou have been ~FGunarrested~RS~OL.  Please read the rules again and try to behave!\n");
send_mail(user,word[1],text,0);
sprintf(text,"%s UNARRESTED %s.\n",user->name,u->name);
write_syslog(text,1,SYSLOG);
sprintf(text,"Was ~FGunarrested~RS by %s.\n",user->name);
add_history(u->name,1,text);
destruct_user(u);
destructed=0;
}


/*** Change users password. Only GODs and above can change another users 
     password and they do this by specifying the user at the end. When this is 
     done the old password given can be anything, the wiz doesnt have to know it
     in advance. ***/
void change_pass(UR_OBJECT user)
{
UR_OBJECT u;
char *name;

if (word_count<3) {
  if (user->level<GOD)
    write_user(user,"Usage: passwd <old password> <new password>\n");
  else write_user(user,"Usage: passwd <old password> <new password> [<user>]\n");
  return;
  }
if (strlen(word[2])<3) {
  write_user(user,"New password too short.\n");  return;
  }
if (strlen(word[2])>PASS_LEN) {
  write_user(user,"New password too long.\n");  return;
  }
/* Change own password */
if (word_count==3) {
  if (strcmp((char *)crypt(word[1],crypt_salt),user->pass)) {
    write_user(user,"Old password incorrect.\n");  return;
    }
  if (!strcmp(word[1],word[2])) {
    write_user(user,"Old and new passwords are the same.\n");  return;
    }
  strcpy(user->pass,(char *)crypt(word[2],crypt_salt));
  save_user_details(user,0);
  cls(user);
  write_user(user,"Password changed.\n");
  add_history(user->name,1,"Changed passwords.\n");
  return;
  }
/* Change someone elses */
if (user->level<GOD) {
  write_user(user,"You are not a high enough level to use the user option.\n");  
  return;
  }
word[3][0]=toupper(word[3][0]);
if (!strcmp(word[3],user->name)) {
  /* security feature  - prevents someone coming to a wizes terminal and 
     changing his password since he wont have to know the old one */
  write_user(user,"You cannot change your own password using the <user> option.\n");
  return;
  }
if ((u=get_user(word[3]))) {
#ifdef NETLINKS
  if (u->type==REMOTE_TYPE) {
    write_user(user,"You cannot change the password of a user logged on remotely.\n");
    return;
    }
#endif
  if (u->level>=user->level) {
    write_user(user,"You cannot change the password of a user of equal or higher level than yourself.\n");
    return;
    }
  strcpy(u->pass,(char *)crypt(word[2],crypt_salt));
  cls(user);
  sprintf(text,"%s's password has been changed.\n",u->name);
  write_user(user,text);
  if (user->vis) name=user->name; else name=invisname;
  sprintf(text,"~FR~OLYour password has been changed by %s!\n",name);
  write_user(u,text);
  sprintf(text,"%s changed %s's password.\n",user->name,u->name);
  write_syslog(text,1,SYSLOG);
  sprintf(text,"Forced password change by %s.\n",user->name);
  add_history(u->name,0,text);
  return;
  }
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user object.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user object in change_pass().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,word[3]);
if (!load_user_details(u)) {
  write_user(user,nosuchuser);   
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->level>=user->level) {
  write_user(user,"You cannot change the password of a user of equal or higher level than yourself.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
strcpy(u->pass,(char *)crypt(word[2],crypt_salt));
save_user_details(u,0);
destruct_user(u);
destructed=0;
cls(user);
sprintf(text,"%s's password changed to \"%s\".\n",word[3],word[2]);
write_user(user,text);
sprintf(text,"Forced password change by %s.\n",user->name);
add_history(u->name,0,text);
sprintf(text,"%s changed %s's password.\n",user->name,u->name);
write_syslog(text,1,SYSLOG);
}


/*** Kill a user ***/
void kill_user(UR_OBJECT user)
{
UR_OBJECT victim;
RM_OBJECT rm;
char *name;
int msg;

if (word_count<2) {
  write_user(user,"Usage: kill <user>\n");  return;
  }
if (!(victim=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);  return;
  }
if (user==victim) {
  write_user(user,"Trying to commit suicide this way is the sixth sign of madness.\n");
  return;
  }
if (victim->level>=user->level) {
  write_user(user,"You cannot kill a user of equal or higher level than yourself.\n");
  sprintf(text,"%s tried to kill you!\n",user->recap);
  write_user(victim,text);
  return;
  }
sprintf(text,"%s KILLED %s.\n",user->name,victim->name);
write_syslog(text,1,SYSLOG);
write_user(user,"~FM~OLYou chant an evil incantation...\n");
if (user->vis) name=user->recap; else name=invisname;
sprintf(text,"~FM~OL%s chants an evil incantation...\n",name);
write_room_except(user->room,text,user);
/* display random kill message.  if you only want one message to be displayed
   then only have one message listed in kill_mesgs[].
   */
msg=rand()%MAX_KILL_MESGS;
write_user(victim,kill_mesgs[msg].victim_msg);
sprintf(text,kill_mesgs[msg].room_msg,victim->recap);
rm=victim->room;
write_room_except(rm,text,victim);
sprintf(text,"~FRKilled~RS by %s.\n",user->name);
add_history(victim->name,1,text);
disconnect_user(victim);
write_monitor(user,NULL,0);
write_room(NULL,"~FM~OLYou hear insane laughter from beyond the grave...\n");
}


/**** Allow a user to delete their own account ***/
void suicide(UR_OBJECT user)
{
if (word_count<2) {
  write_user(user,"Usage: suicide <your password>\n");  return;
  }
if (strcmp((char *)crypt(word[1],crypt_salt),user->pass)) {
  write_user(user,"Password incorrect.\n");  return;
  }
write_user(user,"\n\07~FR~OL~LI*** WARNING - This will delete your account! ***\n\nAre you sure about this (y/n)? ");
user->misc_op=6;  
no_prompt=1;
}


/*** Delete a user ***/
void delete_user(UR_OBJECT user, int this_user)
{
UR_OBJECT u;
char name[USER_NAME_LEN+1];
int level;

if (this_user) {
  /* User structure gets destructed in disconnect_user(), need to keep a
     copy of the name */
  strcpy(name,user->name);
  level=user->level;
  write_user(user,"\n~FR~LI~OLACCOUNT DELETED!\n");
  sprintf(text,"~OL~LI%s commits suicide!\n",user->name);
  write_room_except(user->room,text,user);
  sprintf(text,"%s SUICIDED.\n",name);
  write_syslog(text,1,SYSLOG);
  if (user->level>=WIZ) rem_wiz_node(user->name);
  disconnect_user(user);
  clean_retire_list(name);
  clean_files(name);
  rem_user_node(name,-1);
  level_count[level]--;
  return;
  }
if (word_count<2) {
  write_user(user,"Usage: nuke <user>\n");  return;
  }
word[1][0]=toupper(word[1][0]);
if (!strcmp(word[1],user->name)) {
  write_user(user,"Trying to delete yourself is the eleventh sign of madness.\n");
  return;
  }
if (get_user(word[1])!=NULL) {
  /* Safety measure just in case. Will have to .kill them first */
  write_user(user,"You cannot delete a user who is currently logged on.\n");
  return;
  }
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user object.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user object in delete_user().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
  write_user(user,nosuchuser);  
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->level>=user->level) {
  write_user(user,"You cannot delete a user of an equal or higher level than yourself.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
clean_files(u->name);
clean_retire_list(u->name);
rem_user_node(u->name,-1);
if (u->level>=WIZ) rem_wiz_node(u->name);
level_count[u->level]--;
sprintf(text,"\07~FR~OL~LIUser %s deleted!\n",u->name);
write_user(user,text);
sprintf(text,"%s DELETED %s.\n",user->name,u->name);
write_syslog(text,1,SYSLOG);
destruct_user(u);
destructed=0;
}


/* Purge users that haven't been on for the expire length of time */
int purge(int type, char *purge_site, int purge_days)
{
UR_OBJECT u;
struct user_dir_struct *entry;

/* don't do anything if not initiated by user and auto_purge isn't on */
if (!type && !auto_purge) {
  write_syslog("PURGE: Auto-purge is turned off.\n",0,SYSLOG);
  purge_date=time(0)+(USER_EXPIRES*86400);
  return 0; 
  }
/* write to syslog and update purge time where needed */
switch(type) {
  case 0: write_syslog("PURGE: Executed automatically on default.\n",1,SYSLOG);
          purge_date=time(0)+(USER_EXPIRES*86400);
          break;
  case 1: write_syslog("PURGE: Executed manually on default.\n",1,SYSLOG);
          purge_date=time(0)+(USER_EXPIRES*86400);
          break;
  case 2: write_syslog("PURGE: Executed manually on site matching.\n",1,SYSLOG);
          sprintf(text,"PURGE: Site given was '%s'.\n",purge_site);
	  purge_date=time(0)+(USER_EXPIRES*86400);
          write_syslog(text,0,SYSLOG);
          break;
  case 3: write_syslog("PURGE: Executed manually on days given.\n",1,SYSLOG);
          sprintf(text,"PURGE: Days given were '%d'.\n",purge_days);
	  purge_date=time(0)+(USER_EXPIRES*86400);
          write_syslog(text,0,SYSLOG);
          break;
  } /* end switch */
purge_count=purge_skip=users_purged=0;
entry=first_dir_entry;
while (entry!=NULL) {
  /* don't purge any logged on users */
  if (user_logged_on(entry->name)) {
    purge_skip++;
    goto PURGE_SKIP;
    }
  /* if user is not on, then free to check for purging */
  if ((u=create_user())==NULL) {
    write_syslog("ERROR: Unable to create temporary user object in purge().\n",0,SYSLOG);
    goto PURGE_SKIP;
    }
  strcpy(u->name,entry->name);
  if (!load_user_details(u)) {
    rem_user_node(u->name,-1); /* get rid of name from userlist */
    clean_files(u->name); /* just incase there are any odd files around */
    clean_retire_list(u->name); /* just incase the user is retired */
    level_count[u->level]--;
    destruct_user(u);
    destructed=0;
    goto PURGE_SKIP;
    }
  purge_count++;
  switch(type) {
    case 0: /* automatic */
    case 1: /* manual default */
      if (u->expire && (time(0)>u->t_expire)) {
        rem_user_node(u->name,-1);
	if (u->level>=WIZ) rem_wiz_node(u->name);
        clean_files(u->name);
	clean_retire_list(u->name);
        level_count[u->level]--;
        sprintf(text,"PURGE: removed user '%s'\n",u->name);
        write_syslog(text,0,SYSLOG);
        users_purged++;
        destruct_user(u);
        destructed=0;
        goto PURGE_SKIP;
        }
      break;
    case 2: /* purge on site */
      if (u->expire && pattern_match(u->last_site,purge_site)) {
        rem_user_node(u->name,-1);
	if (u->level>=WIZ) rem_wiz_node(u->name);
        clean_files(u->name);
	clean_retire_list(u->name);
        level_count[u->level]--;
        sprintf(text,"PURGE: removed user '%s'\n",u->name);
        write_syslog(text,0,SYSLOG);
        users_purged++;
        destruct_user(u);
        destructed=0;
        goto PURGE_SKIP;
        }
      break;
    case 3: /* given amount of days */
      if (u->expire && (u->last_login<(time(0)-(purge_days*86400)))) {
        rem_user_node(u->name,-1);
	if (u->level>=WIZ) rem_wiz_node(u->name);
        clean_files(u->name);
	clean_retire_list(u->name);
        level_count[u->level]--;
        sprintf(text,"PURGE: removed user '%s'\n",u->name);
        write_syslog(text,0,SYSLOG);
        users_purged++;
        destruct_user(u);
        destructed=0;
        goto PURGE_SKIP;
        }
      break;
    } /* end switch */
  /* user not purged */
  destruct_user(u);
  destructed=0;
PURGE_SKIP:
  entry=entry->next;
  }
sprintf(text,"PURGE: Checked %d user%s (%d skipped), %d %s purged.\n\n",
	purge_count,PLTEXT_S(purge_count),purge_skip,users_purged,PLTEXT_WAS(users_purged));
write_syslog(text,0,SYSLOG);
/* just make sure that the count is all ok */
count_users();
return 1;
}


/* allows the user to call the purge function */
void purge_users(UR_OBJECT user)
{
int exp=0;

if (word_count<2) {
  write_user(user,"Usage: purge [-d] [-s <site>] [-t <days>]\n");
  return;
  }
if (!strcmp(word[1],"-d")) {
  write_user(user,"~OL~FR***~RS Purging users with default settings ~OL~FR***\n");
  purge(1,NULL,0);
  }
else if (!strcmp(word[1],"-s")) {
  if (word_count<3) {
    write_user(user,"Usage: purge [-d] [-s <site>] [-t <days>]\n");
    return;
    }
  /* check for variations of wild card */
  if (!strcmp("*",word[2])) {
    write_user(user,"You cannot purge users from the site '*'.\n");
    return;
    }
  if (strstr(word[2],"**")) {
    write_user(user,"You cannot have ** in your site to purge.\n");
    return;
    }
  if (strstr(word[2],"?*")) {
    write_user(user,"You cannot have ?* in your site to purge.\n");
    return;
    }
  if (strstr(word[2],"*?")) {
    write_user(user,"You cannot have *? in your site to purge.\n");
    return;
    }
  sprintf(text,"~OL~FR***~RS Purging users with site '%s' ~OL~FR***\n",word[2]);
  write_user(user,text);
  purge(2,word[2],0);
  }
else if (!strcmp(word[1],"-t")) {
  if (word_count<3) {
    write_user(user,"Usage: purge [-d] [-s <site>] [-t <days>]\n");
    return;
    }
  exp=atoi(word[2]);
  if (exp<=USER_EXPIRES) {
    write_user(user,"You cannot purge users who last logged in less than the default time.\n");
    sprintf(text,"The current default is: %d days\n",USER_EXPIRES);
    write_user(user,text);
    return;
    }
  if (exp<0 || exp>999) {
    write_user(user,"You must enter the amount days from 0-999.\n");
    return;
    }
  sprintf(text,"~OL~FR***~RS Purging users who last logged in over '%d days' ago ~OL~FR***\n",exp);
  write_user(user,text);
  purge(3,NULL,exp);
  }
else {
  write_user(user,"Usage: purge [-d] [-s <site>] [-t <days>]\n");
  return;
  }
/* finished purging - give result */
sprintf(text,"Checked ~OL%d~RS user%s (~OL%d~RS skipped), ~OL%d~RS %s purged.  User count is now ~OL%d~RS.\n",
              purge_count,PLTEXT_S(purge_count),purge_skip,users_purged,PLTEXT_WAS(users_purged),user_count);
write_user(user,text);
}


/* Set a user to either expire after a set time, or never expire */
void user_expires(UR_OBJECT user)
{
UR_OBJECT u;

if (word_count<2) {
  write_user(user,"Usage: expire <user>\n");
  return;
  }
/* user logged on */
if ((u=get_user(word[1]))) {
  if (!u->expire) {
    u->expire=1;
    sprintf(text,"You have set it so %s will expire when a purge is run.\n",u->name);
    write_user(user,text);
    sprintf(text,"%s enables expiration with purge.\n",user->name);
    add_history(u->name,0,text);
    sprintf(text,"%s enabled expiration on %s.\n",user->name,u->name);
    write_syslog(text,1,SYSLOG);
    return;
    }
  u->expire=0;
  sprintf(text,"You have set it so %s will no longer expire when a purge is run.\n",u->name);
  write_user(user,text);
  sprintf(text,"%s disables expiration with purge.\n",user->name);
  add_history(u->name,0,text);
  sprintf(text,"%s disabled expiration on %s.\n",user->name,u->name);
  write_syslog(text,1,SYSLOG);
  return;
  }
/* user not logged on */
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user session.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user session in user_expires().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
  write_user(user,nosuchuser);
  destruct_user(u);
  destructed=0;
  return;
  }
if (!u->expire) {
  u->expire=1;
  sprintf(text,"You have set it so %s will expire when a purge is run.\n",u->name);
  write_user(user,text);
  sprintf(text,"%s enables expiration with purge.\n",user->name);
  add_history(u->name,0,text);
  sprintf(text,"%s enabled expiration on %s.\n",user->name,u->name);
  write_syslog(text,1,SYSLOG);
  save_user_details(u,0); destruct_user(u); destructed=0; return;
  }
u->expire=0;
sprintf(text,"You have set it so %s will no longer expire when a purge is run.\n",u->name);
write_user(user,text);
sprintf(text,"%s disables expiration with purge.\n",user->name);
add_history(u->name,0,text);
sprintf(text,"%s disabled expiration on %s.\n",user->name,u->name);
write_syslog(text,1,SYSLOG);
save_user_details(u,0); destruct_user(u); destructed=0; return;
}


/* Create an account for a user if new users from their site have been
   banned and they want to log on - you know they aint a trouble maker, etc */
void create_account(UR_OBJECT user)
{
UR_OBJECT u;
int i;

if (word_count<3) {
  write_user(user,"Usage: create <name> <passwd>\n");  return;
  }
/* user logged on */
if ((u=get_user(word[1]))!=NULL) {
  write_user(user,"You cannot create with the name of an existing user!\n");
  return;
  }
/* user not logged on */
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user session.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user session in create_account().\n",0,SYSLOG);
  return;
  }
if (strlen(word[1])>USER_NAME_LEN) {
  write_user(user,"Name was too long - account not created.\n");
  destruct_user(u);  destructed=0;  return;
  }
if (strlen(word[1])<3) {
  write_user(user,"Name was too short - account not created.\n");
  destruct_user(u);  destructed=0;  return;
  }
for (i=0;i<strlen(word[1]);++i) {
  if (!isalpha(word[1][i])) {
    write_user(user,"You can't have anything but letters in the name - account not created.\n\n");
    destruct_user(u);  destructed=0;  return;
    }
  }
if (contains_swearing(word[1])) {
  write_user(user,"You cannot use a name like that - account not created.\n\n");
  destruct_user(u);  destructed=0;  return;
  }
if (strlen(word[2])>PASS_LEN) {
  write_user(user,"Password was too long - account not created.\n");
  destruct_user(u);  destructed=0;  return;
  }
if (strlen(word[2])<3) {
  write_user(user,"Password was too short - account not created.\n");
  destruct_user(u);  destructed=0;  return;
  }
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
  strcpy(u->pass,(char *)crypt(word[2],crypt_salt));
  strcpy(u->recap,u->name);
  strcpy(u->desc,"is a newbie needing a desc.");
  strcpy(u->in_phrase,"wanders in.");
  strcpy(u->out_phrase,"wanders out");
  strcpy(u->last_site,"created_account");
  strcpy(u->site,u->last_site);
  strcpy(u->logout_room,"<none>");
  strcpy(u->verify_code,"#NONE");
  strcpy(u->email,"#UNSET");
  strcpy(u->homepage,"#UNSET");
  strcpy(u->icq,"#UNSET");
  u->prompt=prompt_def;
  u->charmode_echo=0;
  u->room=room_first;
  u->level=NEW; u->unarrest=NEW;
  level_count[u->level]++;
  user_count++;
  save_user_details(u,0);
  add_user_node(u->name,u->level);
  add_user_date_node(user->name,(long_date(1)));
  sprintf(text,"Was manually created by %s.\n",user->name);
  add_history(u->name,1,text);
  sprintf(text,"You have created an account with the name \"~FT%s~RS\" and password \"~FG%s~RS\".\n",u->name,word[2]);
  write_user(user,text);
  sprintf(text,"%s created a new account with the name '%s'\n",user->name,u->name);
  write_syslog(text,1,SYSLOG);
  destruct_user(u);  destructed=0;  return;
  }
write_user(user,"You cannot create an account with the name of an existing user!\n");
destruct_user(u);  destructed=0;
}


/* Force a save of all the users who are logged on */
void force_save(UR_OBJECT user)
{
UR_OBJECT u;
int cnt;

cnt=0;
for (u=user_first;u!=NULL;u=u->next) {
  #ifdef NETLINKS
    if (u->type==REMOTE_TYPE) continue;
  #endif
  if (u->type==CLONE_TYPE) continue;
  cnt++;
  save_user_details(u,1);
  }
sprintf(text,"Manually saved %d user's details.\n",cnt);
write_syslog(text,1,SYSLOG);
sprintf(text,"You have manually saved %d user's details.\n",cnt);
write_user(user,text);
}


/*** View the system log ***/
void viewlog(UR_OBJECT user)
{
FILE *fp;
char c,*emp="This log file is empty.\n\n",*logfile,filename[80];
int lines,cnt,cnt2,type,level,i;

if (word_count<2) {
  write_user(user,"Usage: viewlog sys/net/req/retired/<level name> [<lines from end>]\n");
  return;
  }
logfile="";
level=-1;
strtoupper(word[1]);
for (i=JAILED;i<=GOD;++i) {
  if (!strcmp(word[1],level_name[i])) level=i;
  }
if (word_count==2) {
  if (!strcmp(word[1],"SYS")) {
    logfile=MAINSYSLOG;
    write_user(user,"\n~BB~FG*** System log ***\n\n");
    }
  else if (!strcmp(word[1],"NET")) {
    logfile=NETSYSLOG;
    write_user(user,"\n~BB~FG*** Netlink log ***\n\n");
    }
  else if (!strcmp(word[1],"REQ")) {
    logfile=REQSYSLOG;
    write_user(user,"\n~BB~FG*** Account Request log ***\n\n");
    }
  else if (!strcmp(word[1],"RETIRED")) {
    sprintf(filename,"%s/%s",USERFILES,RETIRE_LIST);
    logfile=filename;
    write_user(user,"\n~BB~FG*** Retired Wiz log ***\n\n");
    }
  else if (level>=0) {
    sprintf(text,"\n~BB~FG*** User list for level '%s' ***\n\n",level_name[level]);
    write_user(user,text);
    if (!level_count[level]) {
      write_user(user,emp);
      return;
      }
    user->user_page_lev=level;
    switch(more_users(user)) {
      case 0: write_user(user,emp);  return;
      case 1: user->misc_op=16; 
      }
    return;
    }
  else {
    write_user(user,"Usage: viewlog sys/net/req/retired/<level name> [<lines from end>]\n");
    return;
    }
  switch(more(user,user->socket,logfile)) {
    case 0: write_user(user,emp);  return;
    case 1: user->misc_op=2; 
    }
  return;
  }
if ((lines=atoi(word[2]))<1) {
  write_user(user,"Usage: viewlog sys/net/req/retired/<level name> [<lines from the end>]\n");  return;
  }
type=0;
/* find out which log */
if (!strcmp(word[1],"SYS")) { logfile=MAINSYSLOG; type=SYSLOG; }
if (!strcmp(word[1],"NET")) { logfile=NETSYSLOG;  type=NETLOG; }
if (!strcmp(word[1],"REQ")) { logfile=REQSYSLOG;  type=REQLOG; }
if (!strcmp(word[1],"RETIRED")) {
  sprintf(filename,"%s/%s",USERFILES,RETIRE_LIST);
  logfile=filename;
  type=-1;
  }
if (level>=0) {
  if (!level_count[level]) {
    write_user(user,emp);
    return;
    }
  if (lines>level_count[level]) {
    sprintf(text,"There %s only %d line%s in the log.\n",PLTEXT_IS(level_count[level]),level_count[level],PLTEXT_S(level_count[level]));
    write_user(user,text);
    return;
    }
  if (lines==level_count[level])
    sprintf(text,"\n~BB~FG*** User list for level '%s' ***\n\n",level_name[level]);
  else {
    user->user_page_pos=level_count[level]-lines;
    sprintf(text,"\n~BB~FG*** User list for level '%s' (last %d line%s) ***\n\n",level_name[level],lines,PLTEXT_S(lines));
    }
  write_user(user,text);
  user->user_page_lev=level;
  switch(more_users(user)) {
    case 0: write_user(user,emp);  return;
    case 1: user->misc_op=16; 
    }
  return;
  }

/* count total lines */
if (!(fp=fopen(logfile,"r"))) {  write_user(user,emp);  return;  }
cnt=0;

c=getc(fp);
while(!feof(fp)) {
  if (c=='\n') ++cnt;
  c=getc(fp);
  }
if (cnt<lines) {
  sprintf(text,"There %s only %d line%s in the log.\n",PLTEXT_IS(cnt),cnt,PLTEXT_S(cnt));
  write_user(user,text);
  fclose(fp);
  return;
  }
if (cnt==lines) {
  switch(type) {
    case SYSLOG: write_user(user,"\n~BB~FG*** System log ***\n\n");  break;
    case NETLOG: write_user(user,"\n~BB~FG*** Netlink log ***\n\n");  break;
    case REQLOG: write_user(user,"\n~BB~FG*** Account Request log ***\n\n");  break;
        case -1: write_user(user,"\n~BB~FG*** Retired Wiz log ***\n\n");  break;
    }
  fclose(fp);  more(user,user->socket,logfile);  return;
  }

/* Find line to start on */
fseek(fp,0,0);
cnt2=0;
c=getc(fp);
while(!feof(fp)) {
  if (c=='\n') ++cnt2;
  c=getc(fp);
  if (cnt2==cnt-lines) {
    switch(type) {
      case SYSLOG: sprintf(text,"\n~BB~FG*** System log (last %d line%s) ***\n\n",lines,PLTEXT_S(lines));  break;
      case NETLOG: sprintf(text,"\n~BB~FG*** Netlink log (last %d line%s) ***\n\n",lines,PLTEXT_S(lines));  break;
      case REQLOG: sprintf(text,"\n~BB~FG*** Account Request log (last %d line%s) ***\n\n",lines,PLTEXT_S(lines));  break;
          case -1: sprintf(text,"\n~BB~FG*** Retired Wiz log (last %d line%s) ***\n\n",lines,PLTEXT_S(lines));  break;
      }
    write_user(user,text);
    user->filepos=ftell(fp)-1;
    fclose(fp);
    if (more(user,user->socket,logfile)!=1) user->filepos=0;
    else user->misc_op=2;
    return;
    }
  }
fclose(fp);
sprintf(text,"%s: Line count error.\n",syserror);
write_user(user,text);
write_syslog("ERROR: Line count error in viewlog().\n",0,SYSLOG);
}


/* Shows when a user was last logged on */
void show_last_login(UR_OBJECT user)
{
UR_OBJECT u;
int timelen,days,hours,mins,i;
char line[ARR_SIZE],tmp[ARR_SIZE];

if (word_count>2) {
  write_user(user,"Usage: last [<user>]\n");
  return;
  }
/* if checking last on a user */
if (word_count==2) {
  word[1][0]=toupper(word[1][0]);
  if (!strcmp(word[1],user->name)) {
    write_user(user,"You are already logged on!\n");
    return;
    }
  if ((u=get_user(word[1]))) {
    sprintf(text,"%s is currently logged on.\n",u->name);
    write_user(user,text);
    return;
    }
  /* User not logged in */
  if ((u=create_user())==NULL) {
    sprintf(text,"%s: unable to create temporary user object.\n",syserror);
    write_user(user,text);
    write_syslog("ERROR: Unable to create temporary user object in show_last().\n",0,SYSLOG);
    return;
    }
  strcpy(u->name,word[1]);
  if (!load_user_details(u)) {
    write_user(user,nosuchuser);
    destruct_user(u);
    destructed=0;
    return;
    }
  timelen=(int)(time(0)-u->last_login);
  days=timelen/86400;
  hours=(timelen%86400)/3600;
  mins=(timelen%3600)/60;
  write_user(user,"\n+----------------------------------------------------------------------------+\n");
  sprintf(text,"| ~FT~OLLast login details of %-52s~RS |\n",u->name);
  write_user(user,text);
  write_user(user,"+----------------------------------------------------------------------------+\n");
  strcpy(tmp,ctime((time_t *)&(u->last_login)));
  tmp[strlen(tmp)-1]='\0';
  sprintf(text,"| Was last logged in %-55s |\n",tmp);
  write_user(user,text);
  sprintf(tmp,"Which was %d day%s, %d hour%s and %d minute%s ago",days,PLTEXT_S(days),hours,PLTEXT_S(hours),mins,PLTEXT_S(mins));
  sprintf(text,"| %-74s |\n",tmp);
  write_user(user,text);
  sprintf(tmp,"Was on for %d hour%s and %d minute%s",
	  u->last_login_len/3600,PLTEXT_S(u->last_login_len/3600),(u->last_login_len%3600)/60,PLTEXT_S((u->last_login_len%3600)/60));
  sprintf(text,"| %-74s |\n",tmp);
  write_user(user,text);
  write_user(user,"+----------------------------------------------------------------------------+\n\n");
  destruct_user(u);
  destructed=0;
  return;
  } /* end if */
/* if checking all of the last users to log on */
/* get each line of the logins and check if that user is still on & print out the result. */
write_user(user,"\n+----------------------------------------------------------------------------+\n");
write_user(user,"| ~FT~OLThe last users to have logged in~RS                                           |\n");
write_user(user,"+----------------------------------------------------------------------------+\n");
for (i=0;i<LASTLOGON_NUM;i++) {
  if (!last_login_info[i].name[0]) continue;
  if (last_login_info[i].on && (!(u=get_user(last_login_info[i].name))->vis && user->level<WIZ)) continue;
  sprintf(line,"%s %s",last_login_info[i].name,last_login_info[i].time);
  if (last_login_info[i].on) sprintf(text,"| %-67s ~OL~FYONLINE~RS |\n",line);
  else sprintf(text,"| %-74s |\n",line);
  write_user(user,text);
  } /* end for */
write_user(user,"+----------------------------------------------------------------------------+\n\n");
return;
}


/* Display all the people logged on from the same site as user */
void samesite(UR_OBJECT user, int stage)
{
UR_OBJECT u,u_loop;
int found,cnt,same,on;
struct user_dir_struct *entry;

on=0;
if (!stage) {
  if (word_count<2) {
    write_user(user,"Usage: samesite user/site [all]\n");
    return;
    }
  strtolower(word[1]); strtolower(word[2]);
  if (word_count==3 && !strcmp(word[2],"all")) user->samesite_all_store=1;
  else user->samesite_all_store=0;
  if (!strcmp(word[1],"user")) {
    write_user(user,"Enter the name of the user to be checked against: ");
    user->misc_op=12;
    return;
    }
  if (!strcmp(word[1],"site")) {
    write_user(user,"~OL~FRNOTE:~RS Wildcards '*' and '?' can be used.\n");
    write_user(user,"Enter the site to be checked against: ");
    user->misc_op=13;
    return;
    }
  write_user(user,"Usage: samesite user/site [all]\n");
  return;
  }
/* check for users of same site - user supplied */
if (stage==1) {
  /* check just those logged on */
  if (!user->samesite_all_store) {
    found=cnt=same=0;
    if ((u=get_user(user->samesite_check_store))==NULL) {
      write_user(user,notloggedon);
      return;
      }
    for (u_loop=user_first;u_loop!=NULL;u_loop=u_loop->next) {
      cnt++;
      if (u_loop==u) continue;
      if (!strcmp(u->site,u_loop->site)) {
	same++;
	if (++found==1) {
	  sprintf(text,"\n~BB~FG*** Users logged on from the same site as ~OL%s~RS~BB~FG ***\n\n",u->name);
	  write_user(user,text);
          }
	sprintf(text,"    %s %s\n",u_loop->name,u_loop->desc);
	if (u_loop->type==REMOTE_TYPE) text[2]='@';
	if (!u_loop->vis) text[3]='*';
	write_user(user,text);
        }
      }
    if (!found) {
      sprintf(text,"No users currently logged on have the same site as %s.\n",u->name);
      write_user(user,text);
      }
    else {
      sprintf(text,"\nChecked ~FM~OL%d~RS users, ~FM~OL%d~RS had the site as ~FG~OL%s~RS ~FG(%s)\n\n",cnt,same,u->name,u->site);
      write_user(user,text);
      }
    return;
    }
  /* check all the users..  First, load the name given */
  if (!(u=get_user(user->samesite_check_store))) {
    if ((u=create_user())==NULL) {
      sprintf(text,"%s: unable to create temporary user session.\n",syserror);
      write_user(user,text);
      write_syslog("ERROR: Unable to create temporary user session in samesite() - stage 1/all.\n",0,SYSLOG);
      return;
      }
    strcpy(u->name,user->samesite_check_store);
    if (!load_user_details(u)) {
      destruct_user(u); destructed=0;
      sprintf(text,"Sorry, unable to load user file for %s.\n",user->samesite_check_store);
      write_user(user,text);
      write_syslog("ERROR: Unable to load user details in samesite() - stage 1/all.\n",0,SYSLOG);
      return;
      }
    on=0;
    }
  else on=1;
  /* read userlist and check against all users */
  found=cnt=same=0;
  entry=first_dir_entry;
  while (entry!=NULL) {
    entry->name[0]=toupper(entry->name[0]); /* just incase */
    /* create a user object if user not already logged on */
    if ((u_loop=create_user())==NULL) {
      write_syslog("ERROR: Unable to create temporary user session in samesite().\n",0,SYSLOG);
      goto SAME_SKIP1;
      }
    strcpy(u_loop->name,entry->name);
    if (!load_user_details(u_loop)) {
      destruct_user(u_loop); destructed=0;
      goto SAME_SKIP1;
      }
    cnt++;
    if ((on && !strcmp(u->site,u_loop->last_site)) || (!on && !strcmp(u->last_site,u_loop->last_site))) {
      same++;
      if (++found==1) {
	sprintf(text,"\n~BB~FG*** All users from the same site as ~OL%s~RS~BB~FG ***\n\n",u->name);
	write_user(user,text);
        }
      sprintf(text,"    %s %s\n",u_loop->name,u_loop->desc);
      write_user(user,text);
      destruct_user(u_loop);
      destructed=0;
      goto SAME_SKIP1;
      }
    destruct_user(u_loop);
    destructed=0;
  SAME_SKIP1:
    entry=entry->next;
    }
  if (!found) {
    sprintf(text,"No users have the same site as %s.\n",u->name);
    write_user(user,text);
    }
  else {
    if (!on) sprintf(text,"\nChecked ~FM~OL%d~RS users, ~FM~OL%d~RS had the site as ~FG~OL%s~RS ~FG(%s)\n\n",cnt,same,u->name,u->last_site);
    else sprintf(text,"\nChecked ~FM~OL%d~RS users, ~FM~OL%d~RS had the site as ~FG~OL%s~RS ~FG(%s)\n\n",cnt,same,u->name,u->site);
    write_user(user,text);
    }
  if (!on) { destruct_user(u);  destructed=0; }
  return;
  } /* end of stage 1 */

/* check for users of same site - site supplied */
if (stage==2) {
  /* check any wildcards are correct */
  if (strstr(user->samesite_check_store,"**")) {
    write_user(user,"You cannot have ** in your site to check.\n");
    return;
    }
  if (strstr(user->samesite_check_store,"?*")) {
    write_user(user,"You cannot have ?* in your site to check.\n");
    return;
    }
  if (strstr(user->samesite_check_store,"*?")) {
    write_user(user,"You cannot have *? in your site to check.\n");
    return;
    }
  /* check just those logged on */
  if (!user->samesite_all_store) {
    found=cnt=same=0;
    for (u=user_first;u!=NULL;u=u->next) {
      cnt++;
      if (pattern_match(u->site,user->samesite_check_store)) continue;
      same++;
      if (++found==1) {
	sprintf(text,"\n~BB~FG*** Users logged on from the same site as ~OL%s~RS~BB~FG ***\n\n",user->samesite_check_store);
	write_user(user,text);
        }
      sprintf(text,"    %s %s\n",u->name,u->desc);
      if (u->type==REMOTE_TYPE) text[2]='@';
      if (!u->vis) text[3]='*';
      write_user(user,text);
      }
    if (!found) {
      sprintf(text,"No users currently logged on have that same site as %s.\n",user->samesite_check_store);
      write_user(user,text);
      }
    else {
      sprintf(text,"\nChecked ~FM~OL%d~RS users, ~FM~OL%d~RS had the site as ~FG~OL%s\n\n",cnt,same,user->samesite_check_store);
      write_user(user,text);
      }
    return;
    }
  /* check all the users.. */
  /* open userlist to check against all users */
  found=cnt=same=0;
  entry=first_dir_entry;
  while (entry!=NULL) {
    entry->name[0]=toupper(entry->name[0]);
    /* create a user object if user not already logged on */
    if ((u_loop=create_user())==NULL) {
      write_syslog("ERROR: Unable to create temporary user session in samesite() - stage 2/all.\n",0,SYSLOG);
      goto SAME_SKIP2;
      }
    strcpy(u_loop->name,entry->name);
    if (!load_user_details(u_loop)) {
      destruct_user(u_loop); destructed=0;
      goto SAME_SKIP2;
      }
    cnt++;
    if ((pattern_match(u_loop->last_site,user->samesite_check_store))) {
      same++;
      if (++found==1) {
	sprintf(text,"\n~BB~FG*** All users that have the site ~OL%s~RS~BB~FG ***\n\n",user->samesite_check_store);
	write_user(user,text);
        }
      sprintf(text,"    %s %s\n",u_loop->name,u_loop->desc);
      write_user(user,text);
      destruct_user(u_loop);
      destructed=0;
      goto SAME_SKIP2;
      }
    destruct_user(u_loop);
    destructed=0;
  SAME_SKIP2:
    entry=entry->next;
    }
  if (!found) {
    sprintf(text,"No users have the same site as %s.\n",user->samesite_check_store);
    write_user(user,text);
    }
  else {
    if (!on) sprintf(text,"\nChecked ~FM~OL%d~RS users, ~FM~OL%d~RS had the site as ~FG~OL%s~RS\n\n",cnt,same,user->samesite_check_store);
    else sprintf(text,"\nChecked ~FM~OL%d~RS users, ~FM~OL%d~RS had the site as ~FG~OL%s~RS\n\n",cnt,same,user->samesite_check_store);
    write_user(user,text);
    }
  return;
  } /* end of stage 2 */
}


/*** Site a user ***/
void site(UR_OBJECT user)
{
UR_OBJECT u;

if (word_count<2) {
  write_user(user,"Usage: site <user>\n");  return;
  }
/* User currently logged in */
if ((u=get_user(word[1]))) {
  #ifdef NETLINKS
    if (u->type==REMOTE_TYPE) sprintf(text,"%s is remotely connected from %s.\n",u->name,u->site);
    else 
  #endif
      sprintf(text,"%s is logged in from %s (%s) : %d.\n",u->name,u->site,u->ipsite,u->site_port);
  write_user(user,text);
  return;
  }
/* User not logged in */
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user object.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user object in site().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
  write_user(user,nosuchuser);
  destruct_user(u);
  destructed=0;
  return;
  }
sprintf(text,"~OL%s~RS was last logged in from ~OL~FT%s~RS.\n",word[1],u->last_site);
write_user(user,text);
destruct_user(u);
destructed=0;
}


/* allows a user to add to another users history list */
void manual_history(UR_OBJECT user, char *inpstr)
{
if (word_count<3) {
  write_user(user,"Usage: addhistory <user> <text>\n");
  return;
  }
word[1][0]=toupper(word[1][0]);
if (!strcmp(user->name,word[1])) {
  write_user(user,"You cannot add to your own history list.\n");
  return;
  }
if (!(find_user_listed(word[1]))) {
  write_user(user,nosuchuser);  return;
  }
inpstr=remove_first(inpstr);
sprintf(text,"%-*s : %s\n",USER_NAME_LEN,user->name,inpstr);
add_history(word[1],0,text);
sprintf(text,"You have added to %s's history list.\n",word[1]);
write_user(user,text);
}


/* shows the history file of a given user */
void user_history(UR_OBJECT user)
{
char filename[80];

if (word_count<2) {
  write_user(user,"Usage: history <user>\n");  return;
  }
word[1][0]=toupper(word[1][0]);
if (!(find_user_listed(word[1]))) {
  write_user(user,nosuchuser);  return;
  }
sprintf(filename,"%s/%s/%s.H",USERFILES,USERHISTORYS,word[1]);
sprintf(text,"~BB~FG*** The history of user ~OL%s~RS~BB~FG is as follows ***\n\n",word[1]);
write_user(user,text);
switch(more(user,user->socket,filename)) {
  case 0: sprintf(text,"%s has no previously recorded history.\n\n",word[1]);
          write_user(user,text);  break;
  case 1: user->misc_op=2;  break;
  }
}


/*** Switch system logging on and off ***/
void logging(UR_OBJECT user)
{
if (system_logging) {
  write_user(user,"System logging ~FROFF.\n");
  sprintf(text,"%s switched system logging OFF.\n",user->name);
  write_syslog(text,1,SYSLOG);
  system_logging=0;
  return;
  }
system_logging=1;
write_user(user,"System logging ~FGON.\n");
sprintf(text,"%s switched system logging ON.\n",user->name);
write_syslog(text,1,SYSLOG);
}


/*** Set minlogin level ***/
void minlogin(UR_OBJECT user)
{
UR_OBJECT u,next;
char *usage="Usage: minlogin NONE/<user level>\n";
char levstr[5],*name;
int lev,cnt;

if (word_count<2) {
  write_user(user,usage);  return;
  }
strtoupper(word[1]);
if ((lev=get_level(word[1]))==-1) {
  if (strcmp(word[1],"NONE")) {
    write_user(user,usage);  return;
    }
  lev=-1;
  strcpy(levstr,"NONE");
  }
else strcpy(levstr,level_name[lev]);
if (lev>user->level) {
  write_user(user,"You cannot set minlogin to a higher level than your own.\n");
  return;
  }
if (minlogin_level==lev) {
  write_user(user,"It is already set to that.\n");  return;
  }
minlogin_level=lev;
sprintf(text,"Minlogin level set to: ~OL%s.\n",levstr);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s has set the minlogin level to: ~OL%s.\n",name,levstr);
write_room_except(NULL,text,user);
sprintf(text,"%s set the minlogin level to %s.\n",user->name,levstr);
write_syslog(text,1,SYSLOG);

/* Now boot off anyone below that level */
cnt=0;
u=user_first;
while(u) {
  next=u->next;
  if (!u->login && u->type!=CLONE_TYPE && u->level<lev) {
    write_user(u,"\n~FY~OLYour level is now below the minlogin level, disconnecting you...\n");
    disconnect_user(u);
    ++cnt;
    }
  u=next;
  }
sprintf(text,"Total of %d users were disconnected.\n",cnt);
destructed=0;
write_user(user,text);
}


/*** Show talker system parameters etc ***/
void system_details(UR_OBJECT user)
{
#ifdef NETLINKS
  NL_OBJECT nl;
#endif
RM_OBJECT rm;
UR_OBJECT u;
char bstr[40],min_login[5];
char *ca[]={ "NONE  ","IGNORE","REBOOT" };
int days,hours,mins,secs;
int netlinks,live,inc,outg;
int rms,inlinks,num_clones,mem,size;
write_user(user,"\n+----------------------------------------------------------------------------+\n");
sprintf(text,"~OL~FTSystem details for %s~RS (Amnuts version %s)\n",talker_name,AMNUTSVER);
write_user(user,text);
write_user(user,"+----------------------------------------------------------------------------+\n");

/* Get some values */
strcpy(bstr,ctime(&boot_time));
secs=(int)(time(0)-boot_time);
days=secs/86400;
hours=(secs%86400)/3600;
mins=(secs%3600)/60;
secs=secs%60;
num_clones=0;
mem=0;
size=sizeof(struct user_struct);
for(u=user_first;u!=NULL;u=u->next) {
  if (u->type==CLONE_TYPE) num_clones++;
  mem+=size;
  }
rms=0;  
inlinks=0;
size=sizeof(struct room_struct);
for(rm=room_first;rm!=NULL;rm=rm->next) {
  #ifdef NETLINKS
    if (rm->inlink) ++inlinks;
  #endif
  ++rms;  mem+=size;
  }
netlinks=0;  
live=0;
inc=0; 
outg=0;
#ifdef NETLINKS
  size=sizeof(struct netlink_struct);
  for(nl=nl_first;nl!=NULL;nl=nl->next) {
    if (nl->type!=UNCONNECTED && nl->stage==UP) live++;
    if (nl->type==INCOMING) ++inc;
    if (nl->type==OUTGOING) ++outg;
    ++netlinks;  mem+=size;
    }
#endif
if (minlogin_level==-1) strcpy(min_login,"NONE");
else strcpy(min_login,level_name[minlogin_level]);

/* Show header parameters */
#ifdef NETLINKS
  sprintf(text,"~FTProcess ID   : ~FG%-20d   ~FTPorts (M/W/L): ~FG%d,  %d,  %d\n",(int)getpid(),port[0],port[1],port[2]);
#else
  sprintf(text,"~FTProcess ID   : ~FG%-20d   ~FTPorts (M/W): ~FG%d,  %d\n",(int)getpid(),port[0],port[1]);
#endif
write_user(user,text);
sprintf(text,"~FTTalker booted: ~FG%s~FTUptime       : ~FG%d day%s, %d hour%s, %d minute%s, %d second%s\n",
	bstr,days,PLTEXT_S(days),hours,PLTEXT_S(hours),mins,PLTEXT_S(mins),secs,PLTEXT_S(secs));
write_user(user,text);
write_user(user,"+----------------------------------------------------------------------------+\n");
/* Show others */
sprintf(text,"Max users              : %-3d          Current num. of users  : %d\n",max_users,num_of_users);
write_user(user,text);
sprintf(text,"New users this boot    : %-3d          Old users this boot    : %d\n",logons_new,logons_old);
write_user(user,text);
sprintf(text,"Max clones             : %-2d           Current num. of clones : %d\n",max_clones,num_clones);
write_user(user,text);
sprintf(text,"Current minlogin level : %-4s         Login idle time out    : %d secs.\n",min_login,login_idle_time);
write_user(user,text);
sprintf(text,"User idle time out     : %-4d secs.   Heartbeat              : %d\n",user_idle_time,heartbeat);
write_user(user,text);
sprintf(text,"Remote user maxlevel   : %-12s Remote user deflevel   : %s\n",level_name[rem_user_maxlevel],level_name[rem_user_deflevel]);
write_user(user,text);
sprintf(text,"Wizport min login level: %-12s Gatecrash level        : %s\n",level_name[wizport_level],level_name[gatecrash_level]);
write_user(user,text);
sprintf(text,"Time out maxlevel      : %-12s Private room min count : %d\n",level_name[time_out_maxlevel],min_private_users);
write_user(user,text);
sprintf(text,"Message lifetime       : %-2d days      Message check time     : %02d:%02d\n",mesg_life,mesg_check_hour,mesg_check_min);
write_user(user,text);
sprintf(text,"Net idle time out      : %-4d secs.   Number of rooms        : %d\n",net_idle_time,rms);
write_user(user,text);
sprintf(text,"Num. accepting connects: %-2d           Total netlinks         : %d\n",inlinks,netlinks);
write_user(user,text);
sprintf(text,"Number which are live  : %-2d           Number incoming        : %d\n",live,inc);
write_user(user,text);
sprintf(text,"Number outgoing        : %-2d           Ignoring sigterm       : %s\n",outg,noyes2[ignore_sigterm]);
write_user(user,text);
sprintf(text,"Echoing passwords      : %s          Swearing ban status    : %s\n",noyes2[password_echo],minmax[ban_swearing]);
write_user(user,text);
sprintf(text,"Time out afks          : %s          Names recaps allowed   : %s\n",noyes2[time_out_afks],noyes2[allow_recaps]);
write_user(user,text);
sprintf(text,"New user prompt default: %s          New user colour default: %s\n",offon[prompt_def],offon[colour_def]);
write_user(user,text);
sprintf(text,"New user charecho def. : %s          System logging         : %s\n",offon[charecho_def],offon[system_logging]);
write_user(user,text);
sprintf(text,"Crash action           : %s       Object memory allocated: %d\n",ca[crash_action],mem);
write_user(user,text);
sprintf(text,"User purge length      : %-3d days     Newbie purge length    : %-3d days\n",USER_EXPIRES,NEWBIE_EXPIRES);
write_user(user,text);
sprintf(text,"Smail auto-forwarding  : %s          Auto purge on          : %s\n",offon[forwarding],noyes2[auto_purge]);
write_user(user,text);
sprintf(text,"Next auto purge date   : %s",ctime((time_t *)&purge_date));
write_user(user,text);
write_user(user,"+----------------------------------------------------------------------------+\n\n");
}


/*** Free a hung socket ***/
void clearline(UR_OBJECT user)
{
UR_OBJECT u;
int sock;

if (word_count<2 || !is_number(word[1])) {
  write_user(user,"Usage: clearline <line>\n");  return;
  }
sock=atoi(word[1]);
/* Find line amongst users */
for(u=user_first;u!=NULL;u=u->next) 
  if (u->type!=CLONE_TYPE && u->socket==sock) goto FOUND;
write_user(user,"That line is not currently active.\n");
return;
FOUND:
if (!u->login) {
  write_user(user,"You cannot clear the line of a logged in user.\n");
  return;
  }
write_user(u,"\n\nThis line is being cleared.\n\n");
disconnect_user(u); 
sprintf(text,"%s cleared line %d.\n",user->name,sock);
write_syslog(text,1,SYSLOG);
sprintf(text,"Line %d cleared.\n",sock);
write_user(user,text);
destructed=0;
no_prompt=0;
}


/*** Switch swearing ban on and off ***/
void toggle_swearban(UR_OBJECT user)
{
switch(ban_swearing) {
  case OFF: write_user(user,"Swearing ban now set to ~FGminimum ban~RS.\n");
            ban_swearing=MIN;
	    sprintf(text,"%s set swearing ban to MIN.\n",user->name);
	    write_syslog(text,1,SYSLOG);
	    break;
  case MIN: write_user(user,"Swearing ban now set to ~FRmaximum ban~RS.\n");
            ban_swearing=MAX;
	    sprintf(text,"%s set swearing ban to MAX.\n",user->name);
	    write_syslog(text,1,SYSLOG);
	    break;
  case MAX: write_user(user,"Swearing ban now set to ~FYoff~RS.\n");
            ban_swearing=OFF;
	    sprintf(text,"%s set swearing ban to OFF.\n",user->name);
	    write_syslog(text,1,SYSLOG);
	    break;
  }
}


/*** Display colours to user ***/
void display_colour(UR_OBJECT user)
{
int col;

if (user->room==NULL) {
  prompt(user);
  return;
  }
for(col=1;col<NUM_COLS;++col) {
  sprintf(text,"%s: ~%sAmnuts v. %s VIDEO TEST~RS\n",colour_codes[col].txt_code,colour_codes[col].txt_code,AMNUTSVER);
  write_user(user,text);
  }
}


/** Show command, ie 'Type --> <command>' **/
void show(UR_OBJECT user, char *inpstr)
{
if (word_count<2 && inpstr[1]<33) {
  write_user(user,"Usage: show <command>\n");  return;
  }
if (user->muzzled) {
  write_user(user,"You are currently muzzled and cannot broadcast.\n");
  return;
  }
write_monitor(user,user->room,0);
sprintf(text,"~FT~OLType -->~RS %s\n",inpstr);
write_room(user->room,text);
}


/* show the ranks and commands per level for the talker */
void show_ranks(UR_OBJECT user)
{
int i,total,cnt[GOD+1];
char null[1],*ptr;
char *cl="~FT~OL";

for (i=JAILED;i<=GOD;i++) cnt[i]=0;
total=i=0;
while (command_table[i].name[0]!='*') {
  cnt[command_table[i].level]++;
  i++;
  }
null[0]='\0';
write_user(user,"+----- The ranks are as follows ---------------------------------------------+\n\n");
for (i=JAILED; i<=GOD; i++) {
  if (i==user->level) ptr=cl;  else ptr=null;
  sprintf(text,"%s(%s) : %-10.10s : Lev %d : %3d cmds total : %2d cmds this level\n",ptr,level_alias[i],level_name[i],i,total+=cnt[i],cnt[i]);
  write_user(user,text);
  }
write_user(user,"\n+----------------------------------------------------------------------------+\n\n");
}


/* Show the wizzes that are currently logged on, and get a list of names from the lists saved */
void wiz_list(UR_OBJECT user)
{
UR_OBJECT u;
int some_on=0,count=0,cnt,i,inlist;
char text2[ARR_SIZE], temp[ARR_SIZE];
char *clrs[]={"~FT","~FM","~FG","~FB","~OL","~FR","~FY"};
struct wiz_list_struct *entry;

write_user(user,"+----- ~FGWiz List~RS -------------------------------------------------------------+\n\n");
for (i=GOD;i>=WIZ;i--) {
  text2[0]='\0';  cnt=0;  inlist=0;
  sprintf(text,"~OL%s%-10s~RS : ",clrs[i%4],level_name[i]);
  for(entry=first_wiz_entry;entry!=NULL;entry=entry->next) {
    if (in_retire_list(entry->name)) continue;
    if (entry->level==i) {
      if (cnt>3) { strcat(text2,"\n             ");  cnt=0; }
      sprintf(temp,"~OL%s%-*s~RS  ",clrs[rand()%7],USER_NAME_LEN,entry->name);
      strcat(text2,temp);
      cnt++;
      inlist=1;
      }
    }
  if (!cnt && !inlist) sprintf(text2,"~FR[none listed]\n~RS");
  strcat(text,text2);
  write_user(user,text);
  if (cnt) write_user(user,"\n");
  }
write_user(user,"\n+----- ~FGThose currently on~RS ---------------------------------------------------+\n\n");
for (u=user_first;u!=NULL;u=u->next)
if (u->room!=NULL)  {
  if (u->level>=WIZ) {
    if (!u->vis && (user->level<u->level && !(user->level>=ARCH)))  { ++count;  continue;  }
    else {
      if (u->vis) sprintf(text2,"  %s %s~RS",u->recap,u->desc);
      else sprintf(text2,"* %s %s~RS",u->recap,u->desc);
      cnt=colour_com_count(text2);
      sprintf(text,"%-*.*s : %15s : (%s) %s\n",43+cnt*3,43+cnt*3,text2,u->room->name,level_alias[u->level],level_name[u->level]);
      write_user(user,text);
      }
    }
  some_on=1;
  }
if (count>0) {
  sprintf(text,"Number of the wiz invisible to you : %d\n",count);
  write_user(user,text);
  }
if (!some_on) write_user(user,"Sorry, no wizzes are on at the moment...\n");
write_user(user,"\n");
write_user(user,"+----------------------------------------------------------------------------+\n");
}


/*** Get current system time ***/
void get_time(UR_OBJECT user)
{
char bstr[40],temp[80];
int secs,mins,hours,days;

/* Get some values */
strcpy(bstr,ctime(&boot_time));
bstr[strlen(bstr)-1]='\0';
secs=(int)(time(0)-boot_time);
days=secs/86400;
hours=(secs%86400)/3600;
mins=(secs%3600)/60;
secs=secs%60;
write_user(user,"+----------------------------------------------------------------------------+\n");
write_user(user,"| ~OL~FTTalker times~RS                                                               |\n");
write_user(user,"+----------------------------------------------------------------------------+\n");
sprintf(temp,"%s, %d %s, %02d:%02d:%02d %d",day[twday],tmday,month[tmonth],thour,tmin,tsec,tyear);
sprintf(text,"| The current system time is : ~OL%-45s~RS |\n",temp);
write_user(user,text);
sprintf(text,"| System booted              : ~OL%-45s~RS |\n",bstr);
write_user(user,text);
sprintf(temp,"%d days, %d hours, %d minutes, %d seconds",days,hours,mins,secs);
sprintf(text,"| Uptime                     : ~OL%-45s~RS |\n",temp);
write_user(user,text);
write_user(user,"+----------------------------------------------------------------------------+\n\n");
}


/*** Show version number and some small stats of the talker ***/
void show_version(UR_OBJECT user)
{
int rms,i;
RM_OBJECT rm;

write_user(user,"+----------------------------------------------------------------------------+\n");
sprintf(text,"| ~FT~OLAmnuts version %5s                           Andrew Collington, May 1999~RS |\n",AMNUTSVER,NUTSVER);
write_user(user,text);
write_user(user,"+----------------------------------------------------------------------------+\n");
sprintf(text,"| Logons this current boot : ~OL%4d~RS new users, ~OL%4d~RS old users~RS                  |\n",logons_new,logons_old);
write_user(user,text);
sprintf(text,"| Total number of users    : ~OL%-4d~RS  Maximum online users     : ~OL%-3d~RS            |\n",user_count,max_users);
write_user(user,text);
rms=0;  for(rm=room_first;rm!=NULL;rm=rm->next) ++rms;
sprintf(text,"| Total number of rooms    : ~OL%-3d~RS   Swear ban currently on   : ~OL%s~RS            |\n",rms,minmax[ban_swearing]);
write_user(user,text);
sprintf(text,"| Smail auto-forwarding on : ~OL%s~RS   Auto purge on            : ~OL%s~RS            |\n",noyes2[forwarding],noyes2[auto_purge]);
write_user(user,text);
sprintf(text,"| Maximum smail copies     : ~OL%-3d~RS   Names can be recapped    : ~OL%s~RS            |\n",MAX_COPIES,noyes2[allow_recaps]);
write_user(user,text);
sprintf(text,"| Maximum smail copies     : ~OL%-3d~RS   Personal rooms active    : ~OL%s~RS            |\n",MAX_COPIES,noyes2[personal_rooms]);
write_user(user,text);
sprintf(text,"| Maximum user idle time   : ~OL%-3d~RS minutes~RS                                     |\n",user_idle_time/60);
write_user(user,text);
if (user->level>=WIZ) {
  #ifdef NETLINKS
    write_user(user,"| Compiled netlinks        : ~OLYES~RS                                             |\n");
  #else
    write_user(user,"| Compiled netlinks        : ~OLNO~RS                                              |\n");
  #endif
  write_user(user,"+----------------------------------------------------------------------------+\n");
  for (i=JAILED;i<=GOD;i++) {
    sprintf(text,"| Number of users at level %-6s : ~OL%-4d~RS                                     |\n",level_name[i],level_count[i]);
    write_user(user,text);
    }
  }
write_user(user,"+----------------------------------------------------------------------------+\n");
}


/**** Show the amount of memory that the objects are currently taking up ***/
void show_memory(UR_OBJECT user)
{
int usize,rsize,nsize,dsize,csize,lsize,wsize,total,i;
int tusize,trsize,tnsize,tdsize,tcsize,tlsize,twsize;
float mb;
UR_OBJECT u;
RM_OBJECT r;
#ifdef NETLINKS
  NL_OBJECT n;
#endif
struct command_struct *c;
struct user_dir_struct *d;
struct wiz_list_struct *w;

usize=rsize=nsize=dsize=csize=lsize=wsize=0;
tusize=trsize=tnsize=tdsize=tcsize=tlsize=twsize=0;
usize=sizeof(struct user_struct);
for (u=user_first;u!=NULL;u=u->next) tusize+=sizeof(struct user_struct);
rsize=sizeof(struct room_struct);
for (r=room_first;r!=NULL;r=r->next) trsize+=sizeof(struct room_struct);
#ifdef NETLINKS
  nsize=sizeof(struct netlink_struct);
  for (n=nl_first;n!=NULL;n=n->next) tnsize+=sizeof(struct netlink_struct);
#endif
dsize=sizeof(struct user_dir_struct);
for (d=first_dir_entry;d!=NULL;d=d->next) tdsize+=sizeof(struct user_dir_struct);
csize=sizeof(struct command_struct);
for (c=first_command;c!=NULL;c=c->next) tcsize+=sizeof(struct command_struct);
lsize=sizeof(last_login_info[0]);
for (i=0;i<LASTLOGON_NUM;i++) tlsize+=sizeof(last_login_info[i]);
wsize=sizeof(struct wiz_list_struct);
for (w=first_wiz_entry;w!=NULL;w=w->next) twsize+=sizeof(struct wiz_list_struct);
total=tusize+trsize+tnsize+tdsize+tcsize+tlsize+twsize;
mb=(float)total/1000000;

write_user(user,"+----------------------------------------------------------------------------+\n");
write_user(user,"| ~OL~FTMemory Object Allocation~RS                                                   |\n");
write_user(user,"|----------------------------------------------------------------------------|\n");
sprintf(text,"|    user structure : %8d bytes    directory structure : %8d bytes |\n",usize,dsize);
write_user(user,text);
sprintf(text,"|                   : ~OL%8d~RS bytes                        : ~OL%8d~RS bytes |\n",tusize,tdsize);
write_user(user,text);
sprintf(text,"|    room structure : %8d bytes      command structure : %8d bytes |\n",rsize,csize);
write_user(user,text);
sprintf(text,"|                   : ~OL%8d~RS bytes                        : ~OL%8d~RS bytes |\n",trsize,tcsize);
write_user(user,text);
sprintf(text,"| wizlist structure : %8d bytes   last login structure : %8d bytes |\n",wsize,lsize);
write_user(user,text);
sprintf(text,"|                   : ~OL%8d~RS bytes                        : ~OL%8d~RS bytes |\n",twsize,tlsize);
write_user(user,text);
#ifdef NETLINKS
  sprintf(text,"| netlink structure : %8d bytes                                         |\n",nsize);
  write_user(user,text);
  sprintf(text,"|                   : ~OL%8d~RS bytes                                         |\n",tnsize);
  write_user(user,text);
#endif
write_user(user,"+----------------------------------------------------------------------------+\n");
sprintf(text,   "| Total object memory allocated : ~OL%9d~RS bytes   (%02.3f Mb)               |\n",total,mb);
write_user(user,text);
write_user(user,"+----------------------------------------------------------------------------+\n\n");
}


/* lets a user start, stop or check out their status of a game of hangman */
void play_hangman(UR_OBJECT user)
{
int i;
/* char *get_hang_word(); */

if (word_count<2) {
  write_user(user,"Usage: hangman [start/stop/status]\n");
  return;
  }
srand(time(0));
strtolower(word[1]);
i=0;
if (!strcmp("status",word[1])) {
  if (user->hang_stage==-1) {
    write_user(user,"You haven't started a game of hangman yet.\n");
    return;
    }
  write_user(user,"Your current hangman game status is:\n");
  if (strlen(user->hang_guess)<1) sprintf(text,hanged[user->hang_stage],user->hang_word_show,"None yet!");
  else sprintf(text,hanged[user->hang_stage],user->hang_word_show,user->hang_guess);
  write_user(user,text);
  write_user(user,"\n");
  return;
  }
if (!strcmp("stop",word[1])) {
  if (user->hang_stage==-1) {
    write_user(user,"You haven't started a game of hangman yet.\n");
    return;
    }
  user->hang_stage=-1;
  user->hang_word[0]='\0';
  user->hang_word_show[0]='\0';
  user->hang_guess[0]='\0';
  write_user(user,"You stop your current game of hangman.\n");
  return;
  }
if (!strcmp("start",word[1])) {
  if (user->hang_stage>-1) {
    write_user(user,"You have already started a game of hangman.\n");
    return;
    }
  get_hang_word(user->hang_word);
  strcpy(user->hang_word_show,user->hang_word);
  for (i=0;i<strlen(user->hang_word_show);++i) user->hang_word_show[i]='-';
  user->hang_stage=0;
  write_user(user,"Your current hangman game status is:\n\n");
  sprintf(text,hanged[user->hang_stage],user->hang_word_show,"None yet!");
  write_user(user,text);
  return;
  }
write_user(user,"Usage: hangman [start/stop/status]\n");
}



/* returns a word from a list for hangman.
   this will save loading words into memory, and the list could be updated as and when
   you feel like it */
char *get_hang_word(char *aword)
{
char filename[80];
FILE *fp;
int lines,cnt,i;

lines=cnt=i=0;
sprintf(filename,"%s/%s",MISCFILES,HANGDICT);
lines=count_lines(filename);
srand(time(0));
cnt=rand()%lines;
if (!(fp=fopen(filename,"r"))) return("hangman");
fscanf(fp,"%s\n",aword);
while (!feof(fp)) {
  if (i==cnt) {
    fclose(fp);
    return aword;
    }
  ++i;
  fscanf(fp,"%s\n",aword);
  }
fclose(fp);
/* if no word was found, just return a generic word */
return("hangman");
}


/* Lets a user guess a letter for hangman */
void guess_hangman(UR_OBJECT user)
{
int count,i,blanks;

count=blanks=i=0;
if (word_count<2) {
  write_user(user,"Usage: guess <letter>\n");
  return;
  }
if (user->hang_stage==-1) {
  write_user(user,"You haven't started a game of hangman yet.\n");
  return;
  }
if (strlen(word[1])>1) {
  write_user(user,"You can only guess one letter at a time!\n");
  return;
  }
strtolower(word[1]);
if (strstr(user->hang_guess,word[1])) {
  user->hang_stage++;
  write_user(user,"You have already guessed that letter!  And you know what that means...\n\n");
  sprintf(text,hanged[user->hang_stage],user->hang_word_show,user->hang_guess);
  write_user(user,text);
  if (user->hang_stage>=7) {
    write_user(user,"~FR~OLUh-oh!~RS  You couldn't guess the word and died!\n");
    user->hang_stage=-1;
    user->hang_word[0]='\0';
    user->hang_word_show[0]='\0';
    user->hang_guess[0]='\0';
    }
  write_user(user,"\n");
  return;
  }
for (i=0;i<strlen(user->hang_word);++i) {
  if (user->hang_word[i]==word[1][0]) {
    user->hang_word_show[i]=user->hang_word[i];
    ++count;
    }
  if (user->hang_word_show[i]=='-') ++blanks;
  }
strcat(user->hang_guess,word[1]);
if (!count) {
  user->hang_stage++;
  write_user(user,"That letter isn't in the word!  And you know what that means...\n");
  sprintf(text,hanged[user->hang_stage],user->hang_word_show,user->hang_guess);
  write_user(user,text);
  if (user->hang_stage>=7) {
    write_user(user,"~FR~OLUh-oh!~RS  You couldn't guess the word and died!\n");
    user->hang_stage=-1;
    user->hang_word[0]='\0';
    user->hang_word_show[0]='\0';
    user->hang_guess[0]='\0';
    }
  write_user(user,"\n");
  return;
  }
if (count==1) sprintf(text,"Well done!  There was 1 occurrence of the letter %s\n",word[1]);
else sprintf(text,"Well done!  There were %d occurrences of the letter %s\n",count,word[1]);
write_user(user,text);
sprintf(text,hanged[user->hang_stage],user->hang_word_show,user->hang_guess);
write_user(user,text);
if (!blanks) {
  write_user(user,"~FY~OLCongratz!~RS  You guessed the word without dying!\n");
  user->hang_stage=-1;
  user->hang_word[0]='\0';
  user->hang_word_show[0]='\0';
  user->hang_guess[0]='\0';
  }
}



/*** retire a member of the law - ie, remove from the wizlist but don't alter level ***/
void retire_user(UR_OBJECT user)
{
UR_OBJECT u;

if (word_count<2) {
  write_user(user,"Usage: retire <user>\n");  return;
  }
word[1][0]=toupper(word[1][0]);
if (in_retire_list(word[1])) {
  sprintf(text,"%s has already been retired from the wizlist.\n",word[1]);
  write_user(user,text);
  return;
  }
if ((u=get_user_name(user,word[1]))) {
  if (u==user) {
    write_user(user,"You cannot retire yourself.\n");
    return;
    }
  if (u->level<WIZ) {
    write_user(user,"You cannot retire anyone under the level WIZ\n");  return;
    }
  add_retire_list(u->name);
  sprintf(text,"You retire %s from the wizlist.\n",u->name);
  write_user(user,text);
  write_user(u,"You have been retired from the wizlist but still retain your level.\n");
  sprintf(text,"%s RETIRED %s\n",user->name,u->name);
  write_syslog(text,1,SYSLOG);
  sprintf(text,"Was ~FRretired~RS by %s.\n",user->name);
  add_history(u->name,1,text);
  return;
  }
/* Create a temp session, load details, alter , then save. This is inefficient
   but its simpler than the alternative */
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user object.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user object in retire_user().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
  write_user(user,nosuchuser);  
  destruct_user(u);
  destructed=0;
  return;
  }
if (u->level<WIZ) {
  write_user(user,"You cannot retire anyone under the level WIZ.\n");
  destruct_user(u);
  destructed=0;
  return;
  }
add_retire_list(u->name);
sprintf(text,"You retire %s from the wizlist.\n",u->name);
write_user(user,text);
sprintf(text,"%s RETIRED %s\n",user->name,u->name);
write_syslog(text,1,SYSLOG);
sprintf(text,"Was ~FRretired~RS by %s.\n",user->name);
add_history(u->name,1,text);
sprintf(text,"~OLYou have been ~FRretired~RS~OL from the wizlist but still retain your level.\n");
send_mail(user,u->name,text,0);
destruct_user(u);
destructed=0;
}



/*** Unretire a user - ie, put them back on show on the wizlist ***/
void unretire_user(UR_OBJECT user)
{
UR_OBJECT u;

if (word_count<2) {
  write_user(user,"Usage: unretire <user>\n");  return;
  }
word[1][0]=toupper(word[1][0]);
if (!in_retire_list(word[1])) {
  sprintf(text,"%s has not been retired from the wizlist.\n",word[1]);
  write_user(user,text);
  return;
  }
if ((u=get_user_name(user,word[1]))) {
  if (u==user) {
    write_user(user,"You cannot unretire yourself.\n");
    return;
    }
  if (u->level<WIZ) {
    write_user(user,"You cannot retire anyone under the level WIZ.\n");  return;
    }
  clean_retire_list(u->name);
  sprintf(text,"You unretire %s and put them back on the wizlist.\n",u->name);
  write_user(user,text);
  write_user(u,"You have been unretired and placed back on the wizlist.\n");
  sprintf(text,"%s UNRETIRED %s\n",user->name,u->name);
  write_syslog(text,1,SYSLOG);
  sprintf(text,"Was ~FGunretired~RS by %s.\n",user->name);
  add_history(u->name,1,text);
  return;
  }
/* Create a temp session, load details, alter , then save. This is inefficient
   but its simpler than the alternative */
if ((u=create_user())==NULL) {
  sprintf(text,"%s: unable to create temporary user object.\n",syserror);
  write_user(user,text);
  write_syslog("ERROR: Unable to create temporary user object in unretire_user().\n",0,SYSLOG);
  return;
  }
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
  write_user(user,nosuchuser);  
  destruct_user(u);
  destructed=0;
  return;
  }
if (!in_retire_list(u->name)) {
  sprintf(text,"%s has not been retired.\n",word[1]);
  write_user(user,text);
  destruct_user(u);
  destructed=0;
  return;
  }
clean_retire_list(u->name);
sprintf(text,"You unretire %s and put them back on the wizlist.\n",u->name);
write_user(user,text);
sprintf(text,"~OLYou have been ~FGunretired~RS~OL and put back on the wizlist.\n");
send_mail(user,u->name,text,0);
sprintf(text,"%s UNRETIRED %s.\n",user->name,u->name);
write_syslog(text,1,SYSLOG);
sprintf(text,"Was ~FGunretired~RS by %s.\n",user->name);
add_history(u->name,1,text);
destruct_user(u);
destructed=0;
}



/*** checks a name to see if it's in the retire list ***/
int in_retire_list(char *name)
{
char filename[80], check[USER_NAME_LEN+1];
FILE *fp;

sprintf(filename,"%s/%s",USERFILES,RETIRE_LIST);
if (!(fp=fopen(filename,"r"))) return 0;

name[0]=toupper(name[0]);
fscanf(fp,"%s",check);
while(!(feof(fp))) {
  check[0]=toupper(check[0]);
  if (!strcmp(name,check)) {
    fclose(fp); return 1;
    }
  fscanf(fp,"%s",check);
  }
fclose(fp);
return 0;
}


/*** adds a name to the retire list ***/
void add_retire_list(char *name)
{
FILE *fp;
char filename[80];

sprintf(filename,"%s/%s",USERFILES,RETIRE_LIST);
if ((fp=fopen(filename,"a"))) {
  fprintf(fp,"%-*s : %s\n",USER_NAME_LEN,name,long_date(1));
  fclose(fp);
  }
}


/*** removes a user from the retired list ***/
void clean_retire_list(char *name)
{
char filename[80], line[82], check[USER_NAME_LEN];
FILE *fpi,*fpo;
int cnt=0;

sprintf(filename,"%s/%s",USERFILES,RETIRE_LIST);
if (!(fpi=fopen(filename,"r"))) return;
if (!(fpo=fopen("templist","w"))) { fclose(fpi);  return; }

name[0]=toupper(name[0]);
fgets(line,82,fpi);
sscanf(line,"%s",check);
while(!(feof(fpi))) {
  check[0]=toupper(check[0]);
  if (strcmp(name,check)) { fprintf(fpo,"%s",line); cnt++; }
  fgets(line,82,fpi);
  sscanf(line,"%s",check);
  }
fclose(fpi);  fclose(fpo);
unlink(filename);
rename("templist",filename);
if (!cnt) unlink(filename);
return;
}



/*** Display how many times a command has been used, and its overall 
     percentage of showings compared to other commands
     ***/
void show_command_counts(UR_OBJECT user)
{
struct command_struct *cmd;
int total_hits,total_cmds,cmds_used,i,x;
char text2[ARR_SIZE];

x=i=total_hits=total_cmds=cmds_used=0;
text2[0]='\0';
/* get totals of commands and hits */
for (cmd=first_command;cmd!=NULL;cmd=cmd->next) {
  total_hits+=cmd->count;
  total_cmds++;
  }
write_user(user,"\n+----------------------------------------------------------------------------+\n");
write_user(user,"| ~FT~OLCommand usage statistics~RS                                                   |\n");
write_user(user,"+----------------------------------------------------------------------------+\n");
for (cmd=first_command;cmd!=NULL;cmd=cmd->next) {
  /* skip if command has not been used so as not to cause crash by trying to / by 0 */
  if (!cmd->count) continue;
  ++cmds_used;
  /* skip if user cannot use that command anyway */
  if (cmd->min_lev>user->level) continue;
  i=((cmd->count*10000)/total_hits);
  /* build up first half of the string */
  if (!x) {
    sprintf(text,"| %11.11s %4d %3d%% ",cmd->name,cmd->count,i/100);
    ++x;
    }
  /* build up full line and print to user */
  else if (x==1) {
    sprintf(text2,"   %11.11s %4d %3d%%   ",cmd->name,cmd->count,i/100);
    strcat(text,text2);
    write_user(user,text);
    text[0]='\0';  text2[0]='\0';
    ++x;
    }
  else {
    sprintf(text2,"   %11.11s %4d %3d%%  |\n",cmd->name,cmd->count,i/100);
    strcat(text,text2);
    write_user(user,text);
    text[0]='\0';  text2[0]='\0';
    x=0;
    }
  } /* end for */
/* If you've only printed first half of the string */
if (x==1) {
  strcat(text,"                                                     |\n");
  write_user(user,text);
  }
if (x==2) {
  strcat(text,"                          |\n");
  write_user(user,text);
  }
write_user(user,"|                                                                            |\n");
write_user(user,"| Any other commands have not yet been used, or you cannot view them         |\n");
write_user(user,"+----------------------------------------------------------------------------+\n");
sprintf(text2,"Total of ~OL%d~RS commands.    ~OL%d~RS command%s used a total of ~OL%d~RS time%s.",
	total_cmds,cmds_used,PLTEXT_S(cmds_used),total_hits,PLTEXT_S(total_hits));
sprintf(text,"| %-92s |\n",text2);
write_user(user,text);
write_user(user,"+----------------------------------------------------------------------------+\n");
}



/*** read all the user files to check if a user exists ***/
void recount_users(UR_OBJECT user, int ok)
{
int level,incorrect,correct,inlist,notin,added,removed;
char dirname[80],name[USER_NAME_LEN+3],filename[80];
DIR *dirp;
struct dirent *dp;
struct user_dir_struct *entry;
FILE *fp;
UR_OBJECT u;

if (!ok) {
  write_user(user,"~OL~FRWARNING:~RS This process may take some time if you have a lot of user accounts.\n");
  write_user(user,"         This should only be done if there are no, or minimal, users currently\n         logged on.\n");
  write_user(user,"\nDo you wish to continue (y/n)? ");
  user->misc_op=17;
  return;
  }
write_user(user,"\n+----------------------------------------------------------------------------+\n");
level=-1;
incorrect=correct=added=removed=0;
write_user(user,"~OLRecounting all of the users...~RS\n");
/* First process the files to see if there are any to add to the directory listing */
write_user(user,"Processing users to add...");

strcpy(dirname,USERFILES);
/* open the directory file up */
dirp=opendir(dirname);
if (dirp==NULL) {
  write_user(user,"ERROR: Failed to open userfile directory.\n");
  write_syslog("ERROR: Directory open failure in recount_users().\n",1,SYSLOG);
  return;
  }
if ((u=create_user())==NULL) {
  write_user(user,"ERROR: Cannot create user object.\n");
  write_syslog("ERROR: Cannot create user object in recount_users().\n",1,SYSLOG);
  (void) closedir(dirp);
  return;
  }
/* count up how many files in the directory - this include . and .. */
while((dp=readdir(dirp))!=NULL) {
  if (!strcmp(dp->d_name,".") || !strcmp(dp->d_name,"..")) continue;
  if (strstr(dp->d_name,".D")) {
    strcpy(name,dp->d_name);
    name[strlen(name)-2]='\0';
    inlist=0;
    for (entry=first_dir_entry;entry!=NULL;entry=entry->next) {
      if (strcmp(name,entry->name)) continue;
      inlist=1;  break;
      }
    if (!inlist) {
      strcpy(u->name,name);
      if (load_user_details(u)) {
	add_user_node(u->name,u->level);
	level_count[u->level]++;
	sprintf(text,"Added new user node for existing user '%s'\n",name);
	write_syslog(text,0,SYSLOG);
	++added;
	reset_user(u);
        }
      } /* end if inlist */
    else ++correct;
    }
  }
(void) closedir(dirp);
destruct_user(u);

/* Now process any nodes to remove the directory listing.  This may not be optimal to do one loop
   to add and then one to remove, but it's the best way I can think of doing it right now at 4:27am!
   */
write_user(user,"\nProcessing users to remove...");

/* Ok, now I know a lot of people think goto calls are the spawn of Satan, but it was, again, the
   best I could come up with, without having to copy the entire user_dir_struct - which could be
   pretty big if you have a lot of users.  If you don't like it, come up with something better and
   let me know...
   Done this way because you could destruct a node and then try to move onto the next one, but the
   link to do this, of course, has been destructed...  Let me know if I'm wrong!
   */
START_LIST:
notin=0;
entry=first_dir_entry;
while (entry!=NULL) {
  strcpy(name,entry->name);  level=entry->level;
  sprintf(filename,"%s/%s.D",USERFILES,name);
  if (!(fp=fopen(filename,"r"))) {
    notin=1;  break;
    }
  else fclose(fp);
  entry=entry->next;
  }
/* remove the node */
if (notin) {
  sprintf(text,"Removed user node for '%s' - user file does not exist.\n",name);
  write_syslog(text,0,SYSLOG);
  ++removed;
  --correct;
  rem_user_node(name,level);
  level_count[level]--;
  goto START_LIST;
  }

/* now to make sure that the user level counts are correct as show in .version */
count_users();
write_user(user,"\n+----------------------------------------------------------------------------+\n");
sprintf(text,"Checked ~OL%d~RS user%s.  ~OL%d~RS node%s %s added, and ~OL%d~RS node%s %s removed.\n",
	added+removed+correct,PLTEXT_S(added+removed+correct),
        added,PLTEXT_S(added),PLTEXT_WAS(added),
        removed,PLTEXT_S(removed),PLTEXT_WAS(removed));
write_user(user,text);
if (incorrect) write_user(user,"See the system log for further details.\n");
write_user(user,"+----------------------------------------------------------------------------+\n");
user->misc_op=0;
}


/*** This command allows you to do a search for any user names that match
     a particular pattern ***/
void grep_users(UR_OBJECT user)
{
int found,x;
char name[USER_NAME_LEN+1],pat[ARR_SIZE];
struct user_dir_struct *entry;

if (word_count<2) {
  write_user(user,"Usage: grepu <pattern>\n");
  return;
  }
if (strstr(word[1],"**")) {
  write_user(user,"You cannot have ** in your pattern.\n");
  return;
  }
if (strstr(word[1],"?*")) {
  write_user(user,"You cannot have ?* in your pattern.\n");
  return;
  }
if (strstr(word[1],"*?")) {
  write_user(user,"You cannot have *? in your pattern.\n");
  return;
  }
write_user(user,"\n+----------------------------------------------------------------------------+\n");
sprintf(text,"| ~FT~OLUser grep for pattern:~RS ~OL%-51s~RS |\n",word[1]);
write_user(user,text);
write_user(user,"+----------------------------------------------------------------------------+\n");
x=0; found=0; pat[0]='\0';
strcpy(pat,word[1]);
strtolower(pat);
for (entry=first_dir_entry;entry!=NULL;entry=entry->next) {
  strcpy(name,entry->name);
  name[0]=tolower(name[0]);
  if (pattern_match(name,pat)) {
    if (!x) sprintf(text,"| %-*s  ~FT%-20s~RS   ",USER_NAME_LEN,entry->name,level_name[entry->level]);
    else sprintf(text,"   %-*s  ~FT%-20s~RS |\n",USER_NAME_LEN,entry->name,level_name[entry->level]);
    write_user(user,text);
    x=!x;
    ++found;
    }
  }
if (x) write_user(user,"                                      |\n");
if (!found) {
  write_user(user,"|                                                                            |\n");
  write_user(user,"| ~OL~FRNo users have that pattern~RS                                                 |\n");
  write_user(user,"|                                                                            |\n");
  write_user(user,"+----------------------------------------------------------------------------+\n");
  return;
  }
write_user(user,"+----------------------------------------------------------------------------+\n");
sprintf(text,"  ~OL%d~RS user%s had the pattern ~OL%s\n",found,PLTEXT_S(found),word[1]);
write_user(user,text);
write_user(user,"+----------------------------------------------------------------------------+\n\n");
}



/*** Shoot another user... Fun! Fun! Fun! ;) ***/
void shoot_user(UR_OBJECT user)
{
UR_OBJECT user2;
RM_OBJECT rm;
int prob1,prob2;

rm=get_room(default_shoot);
if (rm==NULL) {
  write_user(user,"There is nowhere that you can shoot.\n");
  return;
  }
if (user->room!=rm) {
  sprintf(text,"Don't be shooting in a public place.  Go to the ~OL%s~RS to play.\n",rm->name);
  write_user(user,text);
  return;
  }
if (word_count<2) {
  if (user->bullets==0) {
    sprintf(text,"%s's gun goes *click* as they pull the trigger.\n",user->recap);
    write_room_except(rm,text,user);
    write_user(user,"Your gun goes *click* as you pull the trigger.\n");
    return;
    }
  sprintf(text,"%s fires their gun off into the air.\n",user->recap);
  write_room_except(rm,text,user);
  write_user(user,"You fire your gun off into the air.\n");
  --user->bullets;
  return;
  }
prob1=rand()%100;
prob2=rand()%100;
if (!(user2=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);  return;
  }
if (!user->vis) {
  write_user(user,"Be fair!  At least make a decent target - don't be invisible!\n");
  return;
  }
if ((!user2->vis && user2->level<user->level) || user2->room!=rm) {
  write_user(user,"You cannot see that person around here.\n");
  return;
  }
if (user==user2) {
  write_user(user,"Watch it!  You might shoot yourself in the foot!\n");
  return;
  }
if (user->bullets==0) {
  sprintf(text,"%s's gun goes *click* as they pull the trigger.\n",user->recap);
  write_room_except(rm,text,user);
  write_user(user,"Your gun goes *click* as you pull the trigger.\n");
  return;
  }
if (prob1>prob2) {
  sprintf(text,"A bullet flies from %s's gun and ~FR~OLhits~RS %s.\n",user->recap,user2->recap);
  write_room(rm,text);
  --user->bullets;
  ++user->hits;
  --user2->hps;
  write_user(user2,"~FR~OLYou've been hit!\n");
  write_user(user,"~FG~OLGood shot!\n");
  if (user2->hps<1) {
    ++user2->deaths;
    sprintf(text,"\nYou have won the shoot out, %s is dead!  You may now rejoice!\n",user2->recap);
    write_user(user,text);
    write_user(user2,"\nYou have received a fatal wound, and you feel your warm ~FRblood ~OLooze~RS out of you.\n");
    write_user(user2,"The room starts to fade and grow grey...\n");
    write_user(user2,"In the bleak mist of Death's shroud you see a man walk towards you.\n");
    write_user(user2,"The man is wearing a tall black hat, and a wide grin...\n\n");
    user2->hps=5*user2->level;
    sprintf(text,"%s shot dead by %s\n",user2->name,user->name);
    write_syslog(text,1,SYSLOG);
    disconnect_user(user2);
    ++user->kills;
    user->hps=user->hps+5;
    return;
    }
  return;
  }
sprintf(text,"A bullet flies from %s's gun and ~FG~OLmisses~RS %s.\n",user->recap,user2->recap);
write_room(rm,text);
--user->bullets;
++user->misses;
write_user(user2,"~FGThat was a close shave!\n");
write_user(user,"~FRYou couldn't hit the side of a barn!\n");
}


/*** well.. Duh!  Reload the gun ***/
void reload_gun(UR_OBJECT user)
{
RM_OBJECT rm;

rm=get_room(default_shoot);
if (rm==NULL) {
  write_user(user,"There is nowhere that you can shoot.\n");
  return;
  }
if (user->room!=rm) {
  sprintf(text,"Don't be shooting in a public place.  Go to the ~OL%s~RS to play.\n",rm->name);
  write_user(user,text);
  return;
  }
if (user->bullets>0) {
  sprintf(text,"You have ~OL%d~RS bullets left.\n",user->bullets);
  write_user(user,text);
  return;
  }
sprintf(text,"~FY%s reloads their gun.\n",user->recap);
write_room_except(user->room,text,user);
write_user(user,"~FYYou reload your gun.\n");
user->bullets=6;
}


/*** Allows a user to alter the minimum level which can use the command given ***/
void set_command_level(UR_OBJECT user)
{
struct command_struct *cmd;
int new_lev,found;

if (word_count<3) {
  write_user(user,"Usage: setcmdlev <command name> <level name>/norm\n");
  return;
  }
found=0;
/* levels and 'norm' are checked in upper case */
strtoupper(word[2]);
if (!strcmp(word[2],"NORM")) {
  cmd=first_command;
  while (cmd!=NULL) {
    if (!strncmp(word[1],cmd->name,strlen(word[1]))) {
      if (cmd->min_lev==command_table[cmd->id].level) {
	write_user(user,"That command is already at its normal level.\n");
	return;
        }
      cmd->min_lev=command_table[cmd->id].level;
      found=1;
      break;
      }
    cmd=cmd->next;
    } /* end while */
  if (found) {
    sprintf(text,"%s has returned level to normal for cmd '%s'\n",user->name,cmd->name);
    write_syslog(text,1,SYSLOG);
    write_monitor(user,NULL,0);
    sprintf(text,"~OL~FR--==<~RS The level for command ~OL%s~RS has been returned to %s ~OL~FR>==--\n",cmd->name,level_name[cmd->min_lev]);
    write_room(NULL,text);
    return;
    }
  else {
    sprintf(text,"The command '~OL%s~RS' could not be found.\n",word[1]);
    write_user(user,text);
    return;
    }
  } /* end if 'norm' */
if ((new_lev=get_level(word[2]))==-1) {
  write_user(user,"Usage: setcmdlev <command name> <level name>/norm\n");
  return;
  }
if (new_lev>user->level) {
  write_user(user,"You cannot set a command's level to one greater than your own.\n");
  return;
  }
found=0;
cmd=first_command;
while (cmd!=NULL) {
  if (!strncmp(word[1],cmd->name,strlen(word[1]))) {
    if (command_table[cmd->id].level>user->level) {
      write_user(user,"You are not a high enough level to alter that command's level.\n");
      return;
      }
    cmd->min_lev=new_lev;
    found=1;
    break;
    }
  cmd=cmd->next;
  } /* end while */
if (found) {
  sprintf(text,"%s has set the level for cmd '%s' to %d (%s)\n",user->name,cmd->name,cmd->min_lev,level_name[cmd->min_lev]);
  write_syslog(text,1,SYSLOG);
  write_monitor(user,NULL,0);
  sprintf(text,"~OL~FR--==<~RS The level for command ~OL%s~RS has been set to %s ~OL~FR>==--\n",cmd->name,level_name[cmd->min_lev]);
  write_room(NULL,text);
  }
else {
  sprintf(text,"The command '~OL%s~RS' could not be found.\n",word[1]);
  write_user(user,text);
  }
} /* end set_command_level */


/*** stop a user from using a certain command ***/
void user_xcom(UR_OBJECT user)
{
int i,x,cmd_id;
struct command_struct *cmd;
UR_OBJECT u;

if (word_count<2) {
  write_user(user,"Usage: xcom <user> [<command>]\n");
  return;
  }
if (!(u=get_user(word[1]))) {
  write_user(user,notloggedon);
  return;
  }
if (u==user) {
  write_user(user,"You cannot ban any commands of your own.\n");
  return;
  }
/* if no command is given, then just view banned commands */
if (word_count<3) {
  x=0;
  write_user(user,"+----------------------------------------------------------------------------+\n");
  sprintf(text,"~OL~FTBanned commands for user '%s'\n",u->name);
  write_user(user,text);
  write_user(user,"+----------------------------------------------------------------------------+\n");
  for (i=0;i<MAX_XCOMS;i++) {
    if (u->xcoms[i]!=-1) {
      for (cmd=first_command;cmd!=NULL;cmd=cmd->next) {
	if (cmd->id==u->xcoms[i]) {
	  sprintf(text,"~OL%s~RS (level %d)\n",cmd->name,cmd->min_lev);
	  write_user(user,text);
	  x=1;
	  }
        } /* end for */
      } /* end if */
    } /* end for */
  if (!x) write_user(user,"User has no banned commands.\n");
  write_user(user,"+----------------------------------------------------------------------------+\n\n");
  return;
  }
if (u->level>=user->level) {
  write_user(user,"You cannot ban the commands of a user with the same or higher level as yourself.\n");
  return;
  }
cmd_id=-1;
for (cmd=first_command;cmd!=NULL;cmd=cmd->next) {
  if (!strncmp(word[2],cmd->name,strlen(word[2]))) {
    if (u->level<cmd->min_lev) {
      sprintf(text,"%s is not of a high enough level to use that command anyway.\n",u->name);
      write_user(user,text);
      return;
      }
    cmd_id=cmd->id;
    break;
    }
  } /* end for */
if (cmd_id==-1) {
  write_user(user,"That command does not exist.\n");
  return;
  }
/* check to see is the user has previously been given the command */
if (has_gcom(u,cmd_id)) {
  write_user(user,"You cannot ban a command that a user has been specifically given.\n");
  return;
  }
/* user already has the command, so unabn it */
if (has_xcom(u,cmd_id)) {
  if (set_xgcom(user,u,cmd_id,1,0)) {
    sprintf(text,"You have unbanned the '%s' command for %s\n",word[2],u->name);
    write_user(user,text);
    sprintf(text,"The command '%s' has been unbanned and you can use it again.\n",word[2]);
    write_user(u,text);
    sprintf(text,"%s ~FGUNXCOM'd~RS the command '%s'\n",user->name,word[2]);
    add_history(u->name,1,text);
    sprintf(text,"%s UNXCOM'd the command '%s' for %s\n",user->name,word[2],u->name);
    write_syslog(text,1,SYSLOG);
    return;
    }
  else return;
  }
/* user doesn't have the command, so ban it */
if (set_xgcom(user,u,cmd_id,1,1)) {
  sprintf(text,"You have banned the '%s' command for %s\n",word[2],u->name);
  write_user(user,text);
  sprintf(text,"You have been banned from using the command '%s'.\n",word[2]);
  write_user(u,text);
  sprintf(text,"%s ~FRXCOM'd~RS the command '%s'\n",user->name,word[2]);
  add_history(u->name,1,text);
  sprintf(text,"%s XCOM'd the command '%s' for %s\n",user->name,word[2],u->name);
  write_syslog(text,1,SYSLOG);
  }
}


/*** stop a user from using a certain command ***/
void user_gcom(UR_OBJECT user)
{
int i,x,cmd_id;
struct command_struct *cmd;
UR_OBJECT u;

if (word_count<2) {
  write_user(user,"Usage: gcom <user> [<command>]\n");
  return;
  }
if (!(u=get_user(word[1]))) {
  write_user(user,notloggedon);
  return;
  }
if (u==user) {
  write_user(user,"You cannot give yourself any commands.\n");
  return;
  }
/* if no command is given, then just view given commands */
if (word_count<3) {
  x=0;
  write_user(user,"+----------------------------------------------------------------------------+\n");
  sprintf(text,"~OL~FTGiven commands for user '%s'\n",u->name);
  write_user(user,text);
  write_user(user,"+----------------------------------------------------------------------------+\n");
  for (i=0;i<MAX_GCOMS;i++) {
    if (u->gcoms[i]!=-1) {
      for (cmd=first_command;cmd!=NULL;cmd=cmd->next) {
	if (cmd->id==u->gcoms[i]) {
	  sprintf(text,"~OL%s~RS (level %d)\n",cmd->name,cmd->min_lev);
	  write_user(user,text);
	  x=1;
	  }
        } /* end for */
      } /* end if */
    } /* end for */
  if (!x) write_user(user,"User has no given commands.\n");
  write_user(user,"+----------------------------------------------------------------------------+\n\n");
  return;
  }
if (u->level>=user->level) {
  write_user(user,"You cannot give commands to a user with the same or higher level as yourself.\n");
  return;
  }
cmd_id=-1;
for (cmd=first_command;cmd!=NULL;cmd=cmd->next) {
  if (!strncmp(word[2],cmd->name,strlen(word[2]))) {
    if (u->level>cmd->min_lev) {
      sprintf(text,"%s can already use that command.\n",u->name);
      write_user(user,text);
      return;
      }
    cmd_id=cmd->id;
    break;
    }
  } /* end for */
if (cmd_id==-1) {
  write_user(user,"That command does not exist.\n");
  return;
  }
/* check to see if the user has previously been banned from using the command */
if (has_xcom(u,cmd_id)) {
  write_user(user,"You cannot give a command to a user that already has it banned.\n");
  return;
  }
/* user already has the command, so ungive it */
if (has_gcom(u,cmd_id)) {
  if (set_xgcom(user,u,cmd_id,0,0)) {
    sprintf(text,"You have removed the given command '%s' for %s\n",word[2],u->name);
    write_user(user,text);
    sprintf(text,"Access to the given command '%s' has now been taken away from you.\n",word[2]);
    write_user(u,text);
    sprintf(text,"%s ~FRUNGCOM'd~RS the command '%s'\n",user->name,word[2]);
    add_history(u->name,1,text);
    sprintf(text,"%s UNGCOM'd the command '%s' for %s\n",user->name,word[2],u->name);
    write_syslog(text,1,SYSLOG);
    return;
    }
  else return;
  }
/* user doesn't have the command, so give it */
if (set_xgcom(user,u,cmd_id,0,1)) {
  sprintf(text,"You have given the '%s' command for %s\n",word[2],u->name);
  write_user(user,text);
  sprintf(text,"You have been given access to the command '%s'.\n",word[2]);
  write_user(u,text);
  sprintf(text,"%s ~FGGCOM'd~RS the command '%s'\n",user->name,word[2]);
  add_history(u->name,1,text);
  sprintf(text,"%s GCOM'd the command '%s' for %s\n",user->name,word[2],u->name);
  write_syslog(text,1,SYSLOG);
  }
}


/*** set command bans/unbans
     banned=0/1 - 0 is unban and 1 is ban
     set=0/1 - 0 is unset and 1 is set
     ***/
int set_xgcom(UR_OBJECT user, UR_OBJECT u, int id, int banned, int set)
{
int cnt,i;
FILE *fp;
char filename[80];

/* if banning a command with .xcom */
if (banned) {
  /* if removing the command */
  if (!set) {
    for (i=0;i<MAX_XCOMS;++i) {
      if (u->xcoms[i]==id) {
	u->xcoms[i]=-1;
	goto XGCOM_SKIP;
        }
      } /* end for */
    write_user(user,"ERROR: Could not unban that command.\n");
    return 0;
    } /* end if */
  /* if adding the command */
  for (i=0;i<MAX_XCOMS;++i) {
    if (u->xcoms[i]==-1) {
      u->xcoms[i]=id;
      goto XGCOM_SKIP;
      }
    } /* end for */
  sprintf(text,"%s has had the maximum amount of commands banned.\n",u->name);
  write_user(user,text);
  return 0;
  } /* end if */

/* if giving a command with .gcom */
/* if removing the command */
if (!set) {
  for (i=0;i<MAX_GCOMS;++i) {
    if (u->gcoms[i]==id) {
      u->gcoms[i]=-1;
      goto XGCOM_SKIP;
      }
    } /* end for */
  write_user(user,"ERROR: Could not unban that command.\n");
  return 0;
  } /* end if */
/* if adding the command */
for (i=0;i<MAX_GCOMS;++i) {
  if (u->gcoms[i]==-1) {
    u->gcoms[i]=id;
    goto XGCOM_SKIP;
    }
  } /* end for */
sprintf(text,"%s has had the maximum amount of commands given.\n",u->name);
write_user(user,text);
return 0;
/* write out the commands to a file */
XGCOM_SKIP:
sprintf(filename,"%s/%s/%s.C",USERFILES,USERCOMMANDS,u->name);
if (!(fp=fopen(filename,"w"))) {
  write_user(user,"ERROR: Unable to open to command list file.\n");
  sprintf(text,"Unable to open %s's command list in set_xgcom().\n",u->name);
  write_syslog(text,1,SYSLOG);
  return 0;
  }
cnt=0;
for (i=0;i<MAX_XCOMS;++i) {
  if (u->xcoms[i]==-1) continue;
  fprintf(fp,"0 %d\n",u->xcoms[i]);
  cnt++;
  }
for (i=0;i<MAX_GCOMS;++i) {
  if (u->gcoms[i]==-1) continue;
  fprintf(fp,"1 %d\n",u->gcoms[i]);
  cnt++;
  }
fclose(fp);
if (!cnt) unlink(filename);
return 1;
}


/*** read any banned commands that a user may have ***/
int get_xgcoms(UR_OBJECT user)
{
int i,type,tmp;
FILE *fp;
char filename[80];

sprintf(filename,"%s/%s/%s.C",USERFILES,USERCOMMANDS,user->name);
if (!(fp=fopen(filename,"r"))) return 0;
i=0;
fscanf(fp,"%d %d",&type,&tmp);
while (!feof(fp)) {
  if (!type) user->xcoms[i]=tmp;
  else user->gcoms[i]=tmp;
  i++;
  fscanf(fp,"%d %d",&type,&tmp);
  }
fclose(fp);
return 1;
}


/*** Reloads the description for one or all rooms - incase you have edited the
     file and don't want to reboot the talker to to make the changes displayed
     ***/
void reload_room_description(UR_OBJECT user)
{
int i,error;
RM_OBJECT rm;
char c,filename[80],pat[4];
FILE *fp;

if (word_count<2) {
  write_user(user,"Usage: rloadrm -a/<room name>\n");
  return;
  }
/* if reload all of the rooms */
if (!strcmp(word[1],"-a")) {
  error=0;
  for(rm=room_first;rm!=NULL;rm=rm->next) {
    if (rm->access==PERSONAL_UNLOCKED || rm->access==PERSONAL_LOCKED) continue;
    sprintf(filename,"%s/%s.R",DATAFILES,rm->name);
    if (!(fp=fopen(filename,"r"))) {
      sprintf(text,"Sorry, cannot reload the description file for the room '%s'.\n",rm->name);
      write_user(user,text);
      sprintf(text,"ERROR: Couldn't reload the description file for room %s.\n",rm->name);
      write_syslog(text,0,SYSLOG);
      ++error;
      continue;
      }
    i=0;
    c=getc(fp);
    while(!feof(fp)) {
      if (i==ROOM_DESC_LEN) {
	sprintf(text,"The description is too long for the room '%s'.\n",rm->name);
	write_user(user,text);
	sprintf(text,"ERROR: Description too long when reloading for room %s.\n",rm->name);
	write_syslog(text,0,SYSLOG);
	break;
        } /* end if */
      rm->desc[i]=c;
      c=getc(fp);
      ++i;
      } /* end while */
    rm->desc[i]='\0';
    fclose(fp);
    } /* end for */
  if (!error) write_user(user,"You have now reloaded all room descriptions.\n");
  else  write_user(user,"You have now reloaded all room descriptions that you can.\n");
  sprintf(text,"%s reloaded all of the room descriptions.\n",user->name);
  write_syslog(text,1,SYSLOG);
  return;
  } /* end if */
/* if it's just one room to reload */
/* check first for personal room, and don't reload */
pat[0]='\0';
strcpy(pat,"(*)");
if (pattern_match(word[1],pat)) {
  write_user(user,"Sorry, but you cannot reload personal room descriptions.\n");
  return;
  }
if ((rm=get_room(word[1]))==NULL) {
  write_user(user,nosuchroom);
  return;
  }
sprintf(filename,"%s/%s.R",DATAFILES,rm->name);
if (!(fp=fopen(filename,"r"))) {
  sprintf(text,"Sorry, cannot reload the description file for the room '%s'.\n",rm->name);
  write_user(user,text);
  sprintf(text,"ERROR: Couldn't reload the description file for room %s.\n",rm->name);
  write_syslog(text,0,SYSLOG);
  return;
  }
i=0;
c=getc(fp);
while(!feof(fp)) {
  if (i==ROOM_DESC_LEN) {
    sprintf(text,"The description is too long for the room '%s'.\n",rm->name);
    write_user(user,text);
    sprintf(text,"ERROR: Description too long when reloading for room %s.\n",rm->name);
    write_syslog(text,0,SYSLOG);
    break;
    }
  rm->desc[i]=c;
  c=getc(fp);
  ++i;
  }
rm->desc[i]='\0';
fclose(fp);
sprintf(text,"You have now reloaded the desctiption for the room '%s'.\n",rm->name);
write_user(user,text);
sprintf(text,"%s reloaded the description for the room %s\n",user->name,rm->name);
write_syslog(text,1,SYSLOG);
}



/*****************************************************************************
 Friend commands and their subsids
 *****************************************************************************/


/* Determine whether user has u listed on their friends list */
int user_is_friend(UR_OBJECT user, UR_OBJECT u)
{
int i;

for (i=0;i<MAX_FRIENDS;++i) if (!strcmp(user->friend[i],u->name)) return 1;
return 0;
}


/* Alert anyone logged on who has user in their friends 
   list that the user has just loged on */ 
void alert_friends(UR_OBJECT user)
{
UR_OBJECT u;

for (u=user_first;u!=NULL;u=u->next) {
  if (!u->alert) continue;
  if ((user_is_friend(u,user)) && user->vis) {
    sprintf(text,"\n\07~FG~OL~LIHEY!~RS~OL~FG  Your friend ~FT%s~FG has just logged on\n\n",user->name);
    write_user(u,text);
    }
  }
}


/* Read from the friends file into the user structure */
void get_friends(UR_OBJECT user)
{
char filename[80],name[USER_NAME_LEN];
FILE *fp;
int i;

sprintf(filename,"%s/%s/%s.F",USERFILES,USERFRIENDS,user->name);
if (!(fp=fopen(filename,"r"))) return;
i=0;
fscanf(fp,"%s",name);
while(!feof(fp)) {
  strcpy(user->friend[i],name);
  i++;
  fscanf(fp,"%s",name);
  }
fclose(fp);
}


/* take a users name and add it to friends list */
void friends(UR_OBJECT user)
{
int i,cnt,found;
char filename[80];
FILE *fp;
struct user_dir_struct *entry;

cnt=0;
if (word_count<2) {
  for (i=0;i<MAX_FRIENDS;++i) {
    if (!user->friend[i][0]) continue;
    if (++cnt==1) {
      write_user(user,"+----------------------------------------------------------------------------+\n");
      write_user(user,"| ~FT~OLYou will be alerted when the following people log on~RS                       |\n");
      write_user(user,"+----------------------------------------------------------------------------+\n");
      }
    sprintf(text,"| %-74s |\n",user->friend[i]);
    write_user(user,text);
    }
  if (!cnt) write_user(user,"You are not being alerted when anyone logs on.\n");
  else {
    write_user(user,"+----------------------------------------------------------------------------+\n");
    if (!user->alert) write_user(user,"| ~FTYou are currently not being alerted~RS                                        |\n");
    else write_user(user,"| ~OL~FTYou are currently being alerted~RS                                            |\n");
    write_user(user,"+----------------------------------------------------------------------------+\n");
    }
  return;
  }
if (strlen(word[1])>USER_NAME_LEN) {
  write_user(user,"Your friend doesn't have a name that long!\n");
  return;
  }
if (strlen(word[1])<3) {
  write_user(user,"Your friend doesn't have a name that short!\n");
  return;
  }
word[1][0]=toupper(word[1][0]);
if (!strcmp(word[1],user->name)) {
  write_user(user,"You should know when you log on!\n");
  return;
  }
for (i=0;i<MAX_FRIENDS;++i) {
  if (!strcmp(user->friend[i],word[1])) {
    sprintf(text,"You have removed %s from your friend list.\n",user->friend[i]);
    write_user(user,text);
    user->friend[i][0]='\0';
    goto SKIP;
    }
  }
found=0;
for (entry=first_dir_entry;entry!=NULL;entry=entry->next) {
  if (!strcmp(entry->name,word[1])) {
    found=1;  break;
    }
  }
if (!found) {
  write_user(user,nosuchuser);
  return;
  }
for (i=0;i<MAX_FRIENDS;++i) {
  if (!user->friend[i][0]) {
    strcpy(user->friend[i],word[1]);
    sprintf(text,"You have added %s to your list of friends.\n",user->friend[i]);
    write_user(user,text);
    goto SKIP;
    }
  }
write_user(user,"You have the maximum amount of friends listed already.\n");
return;
SKIP:
sprintf(filename,"%s/%s/%s.F",USERFILES,USERFRIENDS,user->name);
if (!(fp=fopen(filename,"w"))) {
  write_user(user,"ERROR: Unable to open to friend list file.\n");
  sprintf(text,"Unable to open %s's friend list in friends().\n",user->name);
  write_syslog(text,1,SYSLOG);
  return;
  }
cnt=0;
for (i=0;i<MAX_FRIENDS;++i) {
  if (!user->friend[i][0]) continue;
  fprintf(fp,"%s\n",user->friend[i]);
  cnt++;
  }
fclose(fp);
if (!cnt) unlink(filename);
}


/*** Say user speech to all people listed on users friends list ***/
void friend_say(UR_OBJECT user, char *inpstr)
{
char type[10],*name;
int i,cnt;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot speak to your friends.\n");
  return;
  }
/* check to see if use has friends listed */
cnt=0;
for (i=0;i<MAX_FRIENDS;++i) if (user->friend[i][0]) ++cnt;
if (!cnt) {
  write_user(user,"You have no friends listed.\n");
  return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
if (word_count<2) {
  write_user(user,"Say what to your friends?\n");  return;
  }
smiley_type(inpstr,type);
sprintf(text,"~FGYou %s to your friends:~RS %s\n",type,inpstr);
write_user(user,text);
if (user->vis) name=user->recap; else name=invisname;
sprintf(text,"~FGFriend %s %ss:~RS %s\n",name,type,inpstr);
write_friends(user,text,1);
}


/*** Emote something to all the people on the suers friends list ***/
void friend_emote(UR_OBJECT user, char *inpstr)
{
char *name;
int i,cnt;

if (user->muzzled) {
  write_user(user,"You are muzzled, you cannot emote to your friends.\n");  return;
  }
if (word_count<2) {
  write_user(user,"Emote what to your friends?\n");
  return;
  }
/* check to see if use has friends listed */
cnt=0;
for (i=0;i<MAX_FRIENDS;++i) if (user->friend[i][0]) ++cnt;
if (!cnt) {
  write_user(user,"You have no friends listed.\n");
  return;
  }
if (ban_swearing && contains_swearing(inpstr)) {
  switch(ban_swearing) {
    case MIN: inpstr=censor_swear_words(inpstr);
              break;
    case MAX: write_user(user,noswearing);
              return;
    default : break; /* do nothing as ban_swearing is off */
    }
  }
if (user->vis) name=user->recap; else name=invisname;
if (inpstr[0]=='\'' && (inpstr[1]=='s' || inpstr[1]=='S')) sprintf(text,"~OL~FGFriend~RS %s%s\n",name,inpstr);
else sprintf(text,"~OL~FGFriend~RS %s %s\n",name,inpstr);
write_user(user,text);
record_tell(user,text);
write_friends(user,text,1);
}


/*****************************************************************************/


/*** bring a user to the same room ***/
void bring(UR_OBJECT user)
{
UR_OBJECT u;
RM_OBJECT rm;

if (word_count<2) {
  write_user(user,"Usage: bring <user>\n");
  return;
  }
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);
  return;
  }
rm=user->room;
if (user==u) {
  write_user(user,"You ~OLreally~RS want to bring yourself?!  What would others think?!\n");
  return;
  }
if (rm==u->room) {
  sprintf(text,"%s is already here!\n",u->recap);
  write_user(user,text);
  return;
  }
if (u->level>=user->level && user->level!=GOD) {
  write_user(user,"You cannot move a user of equal or higher level that yourself.\n");
  return;
  }
write_user(user,"You chant a mystic spell...\n");
if (user->vis) {
  sprintf(text,"%s chants a mystic spell...\n",user->recap);
  write_room_except(user->room,text,user);
  }
else {
  write_monitor(user,user->room,0);
  write_room_except(user->room,"Someone chants a mystic spell...\n",user);
  }
move_user(u,rm,2);
prompt(u);
}


/*** Force a user to do something ***/
/*** adapted from Ogham - Oh God Here's Another MUD - (c) Neil Robertson ***/
void force(UR_OBJECT user, char *inpstr) {
UR_OBJECT u;
int w;

if (word_count<3) {
  write_user(user,"Usage: force <user> <action>\n");
  return;
  }
word[1][0]=toupper(word[1][0]);
if ((u=get_user_name(user,word[1]))==NULL) {
  write_user(user,notloggedon);
  return;
  }
if (u==user) {
  write_user(user,"There is an easier way to do something yourself.\n");
  return;
  }
if (u->level>=user->level) {
  write_user(user,"You cannot force a user of the same or higher level as yourself.\n");
  return;
  }
inpstr=remove_first(inpstr);
sprintf(text,"%s FORCED %s to: '%s'\n",user->name,u->name,inpstr);
write_syslog(text,0,SYSLOG);

/* shift words down to pass to exec_com */
for (w=2;w<word_count;++w) strcpy(word[w-2],word[w]);
word[w][0]='\0';  
word[w+1][0]='\0';
word_count-=2;

sprintf(text,"%s forces you to: '%s'\n",user->name,inpstr);
write_user(u,text);
sprintf(text,"You force %s to: '%s'\n",u->name,inpstr);
write_user(user,text);
if (!exec_com(u,inpstr)) {
  sprintf(text,"Unable to execute the command for %s.\n",u->name);
  write_user(user,text);
  }
prompt(u);
}


/*****************************************************************************
          Calendar source code taken from Way Out West version 4.0.0
                     Copyright (C) Andrew Collington

              based upon scalar date routines by Ray Gardner
                and CAL - a calendar for DOS by Bob Stout
 *****************************************************************************/


/* determine if year is a leap year */
int is_leap(unsigned yr) {
  return yr%400==0 || (yr%4==0 && yr%100!=0);
}


/* convert months to days */
unsigned months_to_days(unsigned mn) {
  return (mn*3057-3007)/100;
}


/* convert years to days */
long years_to_days(unsigned yr) {
  return yr*365L+yr/4-yr/100+yr/400;
}


/* convert a given date (y/m/d) to a scalar */
long ymd_to_scalar(unsigned yr, unsigned mo, unsigned dy) {
long scalar;

scalar=dy+months_to_days(mo);
/* adjust if past February */
if (mo>2) scalar-=is_leap(yr)?1:2;
yr--;
scalar+=years_to_days(yr);
return scalar;
}


/* converts a scalar date to y/m/d */
void scalar_to_ymd(long scalar, unsigned *yr, unsigned *mo, unsigned *dy) {
unsigned n;

/* 146097 == years_to_days(400) */
for (n=(unsigned)((scalar*400L)/146097);years_to_days(n)<scalar;) n++;
*yr=n;
n=(unsigned)(scalar-years_to_days(n-1));
/* adjust if past February */
if (n>59) {                       
  n+=2;
  if (is_leap(*yr)) n-=n>62?1:2;
  }
/* inverse of months_to_days() */
*mo=(n*100+3007)/3057;
*dy=n-months_to_days(*mo);
}


/* determine if the y/m/d given is todays date */
int is_ymd_today(unsigned yr, unsigned mo, unsigned dy) {
  if (((int)yr==(int)tyear) && ((int)mo==(int)tmonth+1) && ((int)dy==(int)tmday)) return 1;
  return 0;
}


/* display the calendar to the user */
void show_calendar(UR_OBJECT user) {
int iday,day_1,numdays,j;
unsigned yr,mo;
char temp[ARR_SIZE];

if (word_count>3) {
  write_user(user,"Usage: calendar [<m> [<y>]]\n");
  write_user(user,"where: <m> = month from 1 to 12\n");
  write_user(user,"       <y> = year from 1 to 99, or 1800 to 3000\n");
  return;
  }
/* full date given */
if (word_count==3) {
  yr=atoi(word[2]);
  mo=atoi(word[1]);
  if (yr<100) yr+=1900;
  if ((yr>3000) || (yr<1800) || !mo || (mo>12)) {
    write_user(user,"Usage: calendar [<m> [<y>]]\n");
    write_user(user,"where: <m> = month from 1 to 12\n");
    write_user(user,"       <y> = year from 1 to 99, or 1800 to 3000\n");
    return;
    }
  }
/* only month given, so show for this year */
else if (word_count==2) {
  yr=tyear;
  mo=atoi(word[1]);
  if (!mo || (mo>12)) {
    write_user(user,"Usage: calendar [<m> [<y>]]\n");
    write_user(user,"where: <m> = month from 1 to 12\n");
    write_user(user,"       <y> = year from 1 to 99, or 1800 to 3000\n");
    return;
    }
  }
/* todays month and year */
else {
  yr=tyear;
  mo=tmonth+1;
  }
/* show calendar */
numdays=cal_days[mo-1];
if (2==mo && is_leap(yr)) ++numdays;
day_1=(int)((ymd_to_scalar(yr,mo,1) - (long)ISO)%7L);
temp[0]='\n';  text[0]='\n';
write_user(user,"\n+-----------------------------------+\n");
sprintf(temp,"~OL~FT%s %d~RS",month[mo-1],yr);
sprintf(text,"| %-42.42s |\n",temp);
write_user(user,text);
write_user(user,"+-----------------------------------+\n");
text[0]='\0';
strcat(text,"  ");
for (j=0;j<7;) {
   sprintf(temp,"~OL~FY%s~RS",cal_daynames[ISO+j]);
   strcat(text,temp);
   if (7!=++j) strcat(text,"  ");
   }
strcat(text,"\n+-----------------------------------+\n");
for (iday=0;iday<day_1;++iday) strcat(text,"     ");
for (iday=1;iday<=numdays;++iday,++day_1,day_1%=7) {
   if (!day_1 && 1!=iday) strcat(text,"\n\n");
   if (is_ymd_today(yr,mo,iday)) {
     sprintf(temp," ~OL~FG%3d~RS ",iday);
     strcat(text,temp);
     }
   else {
     sprintf(temp," %3d ",iday);
     strcat(text,temp);
     }
   }
for (;day_1;++day_1,day_1%=7) strcat(text,"      ");
strcat(text,"\n+-----------------------------------+\n\n");
write_user(user,text);
}


/*****************************************************************************/


/* lets a user enter their own room.  It creates the room if !exists */
void personal_room(UR_OBJECT user) {
char name[ROOM_NAME_LEN+1],filename[80];
RM_OBJECT rm;
int pcnt=0;

if (!personal_rooms) {
  write_user(user,"Personal room functions are currently disabled.\n");
  return;
  }
sprintf(name,"(%s)",user->name);
strtolower(name);
/* if the user wants to delete their room */
if (word_count>=2) {
  if (strcmp(word[1],"-d")) {
    write_user(user,"Usage: myroom [-d]\n");
    return;
    }
  /* move to the user out of the room if they are in it */
  if ((rm=get_room_full(name))==NULL) {
    write_user(user,"You do not have a personal room built.\n");
    return;
    }
  pcnt=room_visitor_count(rm);
  if (pcnt) {
    write_user(user,"You cannot destroy your room if any people are in it.\n");
    return;
    }
  write_user(user,"~OL~FRYou whistle a sharp spell and watch your room crumble into dust.~RS\n");
  destruct_room(rm);
  /* delete the files */
  sprintf(filename,"%s/%s/%s.R",USERFILES,USERROOMS,user->name);
  unlink(filename);
  sprintf(filename,"%s/%s/%s.B",USERFILES,USERROOMS,user->name);
  unlink(filename);
  sprintf(filename,"%s/%s/%s.K",USERFILES,USERROOMS,user->name);
  unlink(filename);
  sprintf(text,"%s destructed their personal room.\n",user->name);
  write_syslog(text,1,SYSLOG);
  return;
  }
/* if the user is moving to their room */
if (user->lroom==2) {
  write_user(user,"You have been shackled and cannot move.\n");
  return;
  }
/* if room doesn't exist then create it */
if ((rm=get_room_full(name))==NULL) {
  if ((rm=create_room())==NULL) {
    write_user(user,"Sorry, but your room could not be created at this time.\n");
    write_syslog("ERROR: Could not create room for in personal_room()\n",1,SYSLOG);
    return;
    }
  write_user(user,"\nYour room doesn't exists.  Building it now...\n\n");
  /* set up the new rooms attributes.  We presume that one room has already been parsed
     and that is the room everyone logs onto, and so we link to that */
  strcpy(rm->name,name);
  rm->access=PERSONAL_UNLOCKED;
  rm->link[0]=room_first;
  /* check to see if the room was just unloaded from memory first */
  if (!(personal_room_store(user->name,0,rm))) {
    strcpy(rm->desc,default_personal_room_desc);
    strcpy(rm->topic,"Welcome to my room!");
    sprintf(text,"%s creates their own room.\n",user->name);
    write_syslog(text,1,SYSLOG);
    if (!personal_room_store(user->name,1,rm)) {
      write_syslog("ERROR: Unable to save personal room status in personal_room_decorate()\n",1,SYSLOG);
      }
    }
  }
/* if room just created then shouldn't go in his block */
if (user->room==rm) {
  write_user(user,"You are already in your own room!\n");
  return;
  }
move_user(user,rm,1);
}


/* allows a user to lock their room out to access from anyone */
void personal_room_lock(UR_OBJECT user) {
char name[ROOM_NAME_LEN+1];
RM_OBJECT rm;

if (!personal_rooms) {
  write_user(user,"Personal room functions are currently disabled.\n");
  return;
  }
sprintf(name,"(%s)",user->name);
strtolower(name);
/* get room that user is in */
if ((rm=get_room_full(name))==NULL) {
  write_user(user,"Sorry, but you cannot use the room locking feature at this time.\n");
  return;
  }
if (user->room!=rm) {
  write_user(user,"You have to be in your personal room to lock and unlock it.\n");
  return;
  }
switch(rm->access) {
  case PERSONAL_UNLOCKED:
    rm->access=PERSONAL_LOCKED;
    write_user(user,"You have now ~OL~FRlocked~RS your room to all the other users.\n");
    break;
  case PERSONAL_LOCKED:
    rm->access=PERSONAL_UNLOCKED;
    write_user(user,"You have now ~OL~FGunlocked~RS your room to all the other users.\n");
    break;
  }
if (!personal_room_store(user->name,1,rm)) {
  write_syslog("ERROR: Unable to save personal room status in personal_room_decorate()\n",1,SYSLOG);
  }
}


/* lets a user go into another user's personal room if it's unlocked */
void personal_room_visit(UR_OBJECT user) {
char name[ROOM_NAME_LEN+1];
RM_OBJECT rm;

if (word_count<2) {
  write_user(user,"Usage: visit <name>\n");
  return;
  }
if (!personal_rooms) {
  write_user(user,"Personal room functions are currently disabled.\n");
  return;
  }
/* check if not same user */
word[1][0]=toupper(word[1][0]);
if (!strcmp(user->name,word[1])) {
  sprintf(text,"To go to your own room use the .%s command.\n",command_table[MYROOM].name);
  write_user(user,text);
  return;
  }
/* see if there is such a user */
if (!find_user_listed(word[1])) {
  write_user(user,nosuchuser);
  return;
  }
/* get room to go to */
sprintf(name,"(%s)",word[1]);
strtolower(name);
if ((rm=get_room_full(name))==NULL) {
  write_user(user,nosuchroom);
  return;
  }
/* can they go there? */
if (!has_room_access(user,rm)) {
  write_user(user,"That room is currently private, you cannot enter.\n");  
  return;
  }
move_user(user,rm,1);
}


/*** Enter a description for a personal room ***/
void personal_room_decorate(UR_OBJECT user, int done_editing) {
char *c,name[ROOM_NAME_LEN+1];
RM_OBJECT rm;
int i;

if (!personal_rooms) {
  write_user(user,"Personal room functions are currently disabled.\n");
  return;
  }
if (!done_editing) {
  sprintf(name,"(%s)",user->name);
  strtolower(name);
  /* get room that user is in */
  if ((rm=get_room_full(name))==NULL) {
    write_user(user,"Sorry, but you cannot use the room decorating feature at this time.\n");
    return;
    }
  if (user->room!=rm) {
    write_user(user,"You have to be in your personal room to decorate it.\n");
    return;
    }
  write_user(user,"\n~BB*** Decorating your personal room ***\n\n");
  user->misc_op=19;
  editor(user,NULL);
  return;
  }
/* rm should be personal room as check is done above */
rm=user->room;
i=0;
c=user->malloc_start;
while(c!=user->malloc_end) {
  if (i==ROOM_DESC_LEN) {
    sprintf(text,"The description is too long for the room '%s'.\n",rm->name);
    write_user(user,text);
    sprintf(text,"ERROR: Description too long when reloading for room %s.\n",rm->name);
    write_syslog(text,0,SYSLOG);
    break;
    } /* end if */
  rm->desc[i++]=*c++;
  }
rm->desc[i]='\0';
write_user(user,"You have now redecorated your personal room.\n");
if (!personal_room_store(user->name,1,rm)) {
  write_syslog("ERROR: Unable to save personal room status in personal_room_decorate()\n",1,SYSLOG);
  }
}


/*** allow a user to bump others from their personal room ***/
void personal_room_bgone(UR_OBJECT user)
{
RM_OBJECT rm, rmto;
UR_OBJECT u;
char name[ROOM_NAME_LEN+1];

if (!personal_rooms) {
  write_user(user,"Personal room functions are currently disabled.\n");
  return;
  }
if (word_count<2) {
  write_user(user,"Usage: mybgone <user>/all\n");
  return;
  }
sprintf(name,"(%s)",user->name);
strtolower(name);
/* get room that user is in */
if ((rm=get_room_full(name))==NULL) {
  write_user(user,"Sorry, but you cannot use the room bgone feature at this time.\n");
  return;
  }
if (user->room!=rm) {
  write_user(user,"You have to be in your personal room bounce people from it.\n");
  return;
  }
/* get room to bounce people to */
if (!(rmto=get_room(default_warp))) {
  write_user(user,"No one can be bounced from your personal room at this time.\n");
  return;
  }
strtolower(word[1]);
/* bounce everyone out - except GODS */
if (!strcmp(word[1],"all")) {
  for (u=user_first;u!=NULL;u=u->next) {
    if (u==user || u->room!=rm || u->level==GOD) continue;
    sprintf(text,"%s is forced from the room.\n",u->recap);
    write_user(user,text);
    write_user(u,"You are being forced from the room.\n");
    move_user(u,rmto,0);
    }
  return;
  }
/* send out just the one user */
if (!(u=get_user_name(user,word[1]))) {
  write_user(user,notloggedon);
  return;
  }
if (u->room!=rm) {
  sprintf(text,"%s is not in your personal room.\n",u->name);
  write_user(user,text);  return;
  }
if (u->level==GOD) {
  sprintf(text,"%s cannot be forced from your personal room.\n",u->name);
  write_user(user,text);  return;
  }
sprintf(text,"%s is forced from the room.\n",u->recap);
write_user(user,text);
write_user(u,"You are being forced from the room.\n");
move_user(u,rmto,0);
}


/* save and load personal room information for the user of name given.
   if store=0 then read info from file else store.
   */
int personal_room_store(char *name, int store, RM_OBJECT rm) {
FILE *fp;
char filename[80],line[TOPIC_LEN+1],c;
int i;

if (rm==NULL) return 0;
/* load the info */
if (!store) {
  strtolower(name);  name[0]=toupper(name[0]);
  sprintf(filename,"%s/%s/%s.R",USERFILES,USERROOMS,name);
  if (!(fp=fopen(filename,"r"))) {
    /* if can't open the file then just put in default attributes */
    rm->access=PERSONAL_UNLOCKED;
    strcpy(rm->desc,default_personal_room_desc);
    strcpy(rm->topic,"Welcome to my room!");
    return 0;
    }
  fscanf(fp,"%d\n",&rm->access);
  fgets(line,TOPIC_LEN+1,fp);
  i=0;
  c=getc(fp);
  while(!feof(fp)) {
    if (i==ROOM_DESC_LEN) {
      sprintf(text,"ERROR: Description too long when reloading for room %s.\n",rm->name);
      write_syslog(text,0,SYSLOG);
      break;
      } /* end if */
    rm->desc[i]=c;
    c=getc(fp);
    ++i;
    } /* end while */
  rm->desc[i]='\0';
  fclose(fp);
  line[strlen(line)-1]='\0';
  if (!strcmp(line,"#UNSET")) rm->topic[0]='\0';
  else strcpy(rm->topic,line);
  rm->link[0]=room_first;
  return 1;
  }
/* save info */
strtolower(name);  name[0]=toupper(name[0]);
sprintf(filename,"%s/%s/%s.R",USERFILES,USERROOMS,name);
if (!(fp=fopen(filename,"w"))) return 0;
fprintf(fp,"%d\n",rm->access);
(!rm->topic[0]) ? fprintf(fp,"#UNSET\n") : fprintf(fp,"%s\n",rm->topic);
i=0;
while (i!=ROOM_DESC_LEN) {
  if (rm->desc[i]=='\0') break;
  putc(rm->desc[i++],fp);
  }
fclose(fp);
return 1;
}


/* this function allows admin to control personal rooms */
void personal_room_admin(UR_OBJECT user) {
RM_OBJECT rm;
int rsize,trsize,rmcnt,locked,unlocked,pcnt;
char uname[USER_NAME_LEN+1],filename[80];

if (word_count<2) {
  write_user(user,"Usage: rmadmin -l / -m / -u <name> / -d <name>\n");
  return;
  }
if (!personal_rooms) {
  write_user(user,"Personal room functions are currently disabled.\n");
  return;
  }
strtolower(word[1]);
/* just display the amount of memory used by personal rooms */
if (!strcmp(word[1],"-m")) {
  write_user(user,"+----------------------------------------------------------------------------+\n");
  write_user(user,"| ~FT~OLPersonal Room Memory Usage~RS                                                 |\n");
  write_user(user,"+----------------------------------------------------------------------------+\n");
  rsize=trsize=rmcnt=locked=unlocked=0;
  rsize=sizeof(struct room_struct);
  for (rm=room_first;rm!=NULL;rm=rm->next) {
    if (is_personal_room(rm)) {
      (rm->access==PERSONAL_LOCKED) ? locked++ : unlocked++;
      trsize+=sizeof(struct room_struct);
      rmcnt++;
      }
    }
  sprintf(text,"| room structure : ~OL%4d~RS bytes      total memory : ~OL%8d~RS bytes  (%02.3f Mb) |\n",rsize,trsize,(float)trsize/1000000);
  write_user(user,text);
  sprintf(text,"|    total rooms : ~OL%4d~RS                  status : ~OL%2d~RS locked, ~OL%2d~RS unlocked     |\n",rmcnt,locked,unlocked);
  write_user(user,text);
  write_user(user,"+----------------------------------------------------------------------------+\n");
  return;
  }
/* list all the personal rooms in memory together with status */
if (!strcmp(word[1],"-l")) {
  rmcnt=0;
  write_user(user,"+----------------------------------------------------------------------------+\n");
  write_user(user,"| ~OL~FTPersonal Room Listings~RS                                                     |\n");
  write_user(user,"+----------------------------------------------------------------------------+\n");
  for (rm=room_first;rm!=NULL;rm=rm->next) {
    if (is_personal_room(rm)) {
      pcnt=room_visitor_count(rm);
      midcpy(rm->name,uname,1,strlen(rm->name)-2);
      uname[0]=toupper(uname[0]);
      sprintf(text,"| Owner : ~OL%-*s~RS       Status : ~OL%s~RS   Msg Count : ~OL%-2d~RS  People : ~OL%-2d~RS |\n",
	      USER_NAME_LEN,uname,(rm->access==PERSONAL_LOCKED)?"~FRlocked  ":"~FGunlocked",rm->mesg_cnt,pcnt);
      write_user(user,text);
      rmcnt++;
      }
    }
  if (!rmcnt) write_user(user,"| ~FRNo personal rooms are currently in memory~RS                                  |\n");
  write_user(user,"+----------------------------------------------------------------------------+\n");
  if (rmcnt) {
    sprintf(text,"| Total personal rooms : ~OL~FM%2d~RS                                                  |\n",rmcnt);
    write_user(user,text);
    write_user(user,"+----------------------------------------------------------------------------+\n");
    }
  return;
  }
/* unload a room from memory or delete it totally - all rooms files */ 
if (!strcmp(word[1],"-u") || !strcmp(word[1],"-d")) {
  if (word_count<3) {
    write_user(user,"Usage: rmadmin -l / -m / -u <name> / -d <name>\n");
    return;
    }
  sprintf(uname,"(%s)",word[2]);
  strtolower(uname);
  /* first do checks on the room */
  if ((rm=get_room_full(uname))==NULL) {
    write_user(user,"That user does not have a personal room built.\n");
    return;
    }
  pcnt=0;
  pcnt=room_visitor_count(rm);
  if (pcnt) {
    write_user(user,"You cannot remove a room if people are in it.\n");
    return;
    }
  destruct_room(rm);
  strtolower(word[2]);
  word[2][0]=toupper(word[2][0]);
  /* delete all files */
  if (!strcmp(word[1],"-d")) {
    sprintf(filename,"%s/%s/%s.R",USERFILES,USERROOMS,word[2]);
    unlink(filename);
    sprintf(filename,"%s/%s/%s.B",USERFILES,USERROOMS,word[2]);
    unlink(filename);
    sprintf(text,"%s deleted the personal room of %s.\n",user->name,word[2]);
    write_syslog(text,1,SYSLOG);
    sprintf(text,"You have now ~OL~FRdeleted~RS the room belonging to %s.\n",word[2]);
    write_user(user,text);
    }
  /* just unload from memory */
  else {
    sprintf(text,"%s unloaded the personal room of %s from memory.\n",user->name,word[2]);
    write_syslog(text,1,SYSLOG);
    sprintf(text,"You have now ~OL~FGunloaded~RS the room belonging to %s from memory.\n",word[2]);
    write_user(user,text);
    }
  return;
  }
/* wrong input given */
write_user(user,"Usage: rmadmin -l / -m / -u <name> / -d <name>\n");
}


/*** this function allows users to give access to others even if their personal room
     has been locked
     ***/
void personal_room_key(UR_OBJECT user) {
char name[ROOM_NAME_LEN+1],filename[80],line[USER_NAME_LEN+2],text2[ARR_SIZE];
RM_OBJECT rm;
FILE *fp;
int cnt=0;

if (!personal_rooms) {
  write_user(user,"Personal room functions are currently disabled.\n");
  return;
  }
sprintf(name,"(%s)",user->name);
strtolower(name);
/* see if user has a room created */
if ((rm=get_room_full(name))==NULL) {
  write_user(user,"Sorry, but you have not created a personal room yet.\n");
  return;
  }
/* if no name was given then display keys given */
if (word_count<2) {
  sprintf(filename,"%s/%s/%s.K",USERFILES,USERROOMS,user->name);
  if (!(fp=fopen(filename,"r"))) {
    write_user(user,"You have not given anyone a personal room key yet.\n");
    return;
    }
  write_user(user,"+----------------------------------------------------------------------------+\n");
  write_user(user,"| ~OL~FTYou have given the following people a key to your room~RS                     |\n");
  write_user(user,"+----------------------------------------------------------------------------+\n");
  text2[0]='\0';
  fscanf(fp,"%s",line);
  while(!feof(fp)) {
    switch (++cnt) {
      case 1: sprintf(text,"| %-24s",line);  strcat(text2,text);  break;
      case 2: sprintf(text," %-24s",line);  strcat(text2,text);  break;
      default:
	sprintf(text," %-24s |\n",line);
	strcat(text2,text);
	write_user(user,text2);
	cnt=0;  text2[0]='\0';
	break;
      }
    fscanf(fp,"%s",line);
    }
  fclose(fp);
  if (cnt==1) {
    strcat(text2,"                                                   |\n");
    write_user(user,text2);
    }
  else {
    if (cnt==2) strcat(text2,"                          |\n");
    write_user(user,text2);
    }
  write_user(user,"+----------------------------------------------------------------------------+\n");
  return;
  }
strtolower(word[1]);
word[1][0]=toupper(word[1][0]);
if (!strcmp(user->name,word[1])) {
  write_user(user,"You already have access to your own room!\n");
  return;
  }
/* check to see if the user is already listed before the adding part.  This is to
   ensure you can remove a user even if they have, for instance, suicided.
   */
if (has_room_key(word[1],rm)) {
  if (personal_key_remove(user,word[1])) {
    sprintf(text,"You take your personal room key away from ~FT~OL%s~RS.\n",word[1]);
    write_user(user,text);
    sprintf(text,"%s takes back their personal room key.\n",user->name);
    write_user(get_user(word[1]),text);
    return;
    }
  else {
    write_user(user,"There was an error taking the key away from that user.\n");
    return;
    }
  }
/* see if there is such a user */
if (!find_user_listed(word[1])) {
  write_user(user,nosuchuser);
  return;
  }
/* give them a key */
if (personal_key_add(user,word[1])) {
  sprintf(text,"You give ~FT~OL%s~RS a key to your personal room.\n",word[1]);
  write_user(user,text);
  sprintf(text,"%s gives you a key to their room.\n",user->name);
  write_user(get_user(word[1]),text);
  }
else write_user(user,"There was an error taking the key away from that user.\n");
}


/*** adds a name to the user's personal room key list ***/
int personal_key_add(UR_OBJECT user,char *name) {
FILE *fp;
char filename[80];

sprintf(filename,"%s/%s/%s.K",USERFILES,USERROOMS,user->name);
if ((fp=fopen(filename,"a"))) {
  fprintf(fp,"%s\n",name);
  fclose(fp);
  return 1;
  }
return 0;
}


/*** remove a name from the user's personal room key list ***/
int personal_key_remove(UR_OBJECT user,char *name) {
char filename[80], line[USER_NAME_LEN+2];
FILE *fpi,*fpo;
int cnt=0;

sprintf(filename,"%s/%s/%s.K",USERFILES,USERROOMS,user->name);
if (!(fpi=fopen(filename,"r"))) return 0;
if (!(fpo=fopen("tempkeylist","w"))) { fclose(fpi);  return 0; }

fscanf(fpi,"%s",line);
while(!(feof(fpi))) {
  if (strcmp(name,line)) {
    fprintf(fpo,"%s\n",line);  cnt++;
    }
  fscanf(fpi,"%s",line);
  }
fclose(fpi);
fclose(fpo);
unlink(filename);
rename("tempkeylist",filename);
if (!cnt) unlink(filename);
return 1;
}


/*****************************************************************************/


