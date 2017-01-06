/*****************************************************************************
                Amnuts version 1.4.0 - by Andrew Collington
                      Last update: 2nd November, 1997

                         email: andyc@dircon.co.uk
             homepage: http://www.geocities.com/Hollywood/1900

                           which is modified

    NUTS version 3.3.3 (Triple Three :) - Copyright (C) Neil Robertson 1996
                     Last update: 18th November 1996
 *****************************************************************************/

#include <stdio.h>
#ifdef _AIX
#include <sys/select.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <setjmp.h>
#include <errno.h>

#include "amnuts140.h"

#define NUTSVER "3.3.3"
#define AMNUTSVER "1.4.0"

/*** This function calls all the setup routines and also contains the
	main program loop ***/
main(argc,argv)
int argc;
char *argv[];
{
fd_set readmask; 
int i,len; 
char inpstr[ARR_SIZE];
char *remove_first();
UR_OBJECT user,next;
NL_OBJECT nl;

strcpy(progname,argv[0]);
if (argc<2) strcpy(confile,CONFIGFILE);
else strcpy(confile,argv[1]);

/* Startup */
printf("\n*** Amnuts %s server booting ***\n\n",AMNUTSVER);
init_globals();
write_syslog("\n*** SERVER BOOTING ***\n",0,SYSLOG);
set_date_time();
init_signals();
load_and_parse_config();
init_sockets();
if (auto_connect) init_connections();
else printf("Skipping connect stage.\n");
check_messages(NULL,1);
count_users();
purge(0);
if (!auto_purge) printf("PURGE: Auto-purge is turned off\n");
else printf("PURGE: Checked %d users, %d were deleted due to lack of use.\n",purge_count,users_purged);
printf("Ordering commands.\n");
command_order();
count_suggestions();
printf("There are %d suggestions.\n",sug_num);

/* Run in background automatically. */
switch(fork()) {
	case -1: boot_exit(11);  /* fork failure */
	case  0: break; /* child continues */
	default: sleep(1); exit(0);  /* parent dies */
	}
reset_alarm();
printf("\n*** Booted with PID %d ***\n\n",getpid());
sprintf(text,"*** Booted successfully with PID %d %s ***\n\n",getpid(),long_date(1));
write_syslog(text,0,SYSLOG);

/**** Main program loop. *****/
setjmp(jmpvar); /* jump to here if we crash and crash_action = IGNORE */
while(1) {
	/* set up mask then wait */
	setup_readmask(&readmask);
	if (select(FD_SETSIZE,&readmask,0,0,0)==-1) continue;

	/* check for connection to listen sockets */
	for(i=0;i<3;++i) {
		if (FD_ISSET(listen_sock[i],&readmask)) 
			accept_connection(listen_sock[i],i);
		}

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

	/* Cycle through users. Use a while loop instead of a for because
	    user structure may be destructed during loop in which case we
	    may lose the user->next link. */
	user=user_first;
	while(user!=NULL) {
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
		if (user->login) {
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
				if (strcmp((char *)crypt(word[0],"NU"),user->pass)) {
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
				sprintf(text,"%s comes back from being AFK.\n",user->name);
				write_room_except(user->room,text,user);
				}
			if (user->afk==2) {
				user->afk=0;  prompt(user);  user=next;  continue;
				}
			user->afk=0;
			}
		if (!word_count) {
			if (misc_ops(user,inpstr))  {  user=next;  continue;  }
			if (user->room==NULL) {
				sprintf(text,"ACT %s NL\n",user->name);
				write_sock(user->netlink->socket,text);
				}
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
					case -1  : /* Unknown command */
					case HOME:
					case QUIT:
					case MODE:
					case PROMPT: 
					case SUICIDE:
					case REBOOT:
					case SHUTDOWN: prompt(user);
					}
				}
			}
		user=next;
		}
	} /* end while */
}


/************ MAIN LOOP FUNCTIONS ************/

/*** Set up readmask for select ***/
setup_readmask(mask)
fd_set *mask;
{
UR_OBJECT user;
NL_OBJECT nl;
int i;

FD_ZERO(mask);
for(i=0;i<3;++i) FD_SET(listen_sock[i],mask);
/* Do users */
for (user=user_first;user!=NULL;user=user->next) 
	if (user->type==USER_TYPE) FD_SET(user->socket,mask);

/* Do client-server stuff */
for(nl=nl_first;nl!=NULL;nl=nl->next) 
	if (nl->type!=UNCONNECTED) FD_SET(nl->socket,mask);
}


/*** Accept incoming connections on listen sockets ***/
accept_connection(lsock,num)
int lsock,num;
{
UR_OBJECT user,create_user();
NL_OBJECT create_netlink();
char *get_ip_address(),site[80];
struct sockaddr_in acc_addr;
int accept_sock,size;

size=sizeof(struct sockaddr_in);
accept_sock=accept(lsock,(struct sockaddr *)&acc_addr,&size);
if (num==2) {
	accept_server_connection(accept_sock,acc_addr);  return;
	}
strcpy(site,get_ip_address(acc_addr));
if (site_banned(site)) {
	write_sock(accept_sock,"\n\rLogins from your site/domain are banned.\n\n\r");
	close(accept_sock);
	sprintf(text,"Attempted login from banned site %s.\n",site);
	write_syslog(text,1,SYSLOG);
	return;
	}
more(NULL,accept_sock,MOTD1); /* send pre-login message */
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
user->login=3;
user->last_input=time(0);
if (!num) user->port=port[0]; 
else {
	user->port=port[1];
	write_user(user,"** Wizport login **\n\n");
	}
strcpy(user->site,site);
user->site_port=(int)ntohs(acc_addr.sin_port);
echo_on(user);
write_user(user,"Give me a name: ");
num_of_logins++;
}


/*** Get net address of accepted connection ***/
char *get_ip_address(acc_addr)
struct sockaddr_in acc_addr;
{
static char site[80];
struct hostent *host;

strcpy(site,(char *)inet_ntoa(acc_addr.sin_addr)); /* get number addr */
if ((host=gethostbyaddr((char *)&acc_addr.sin_addr,4,AF_INET))!=NULL)
	strcpy(site,host->h_name); /* copy name addr. */
strtolower(site);
return site;
}


/*** See if users site is banned ***/
site_banned(site)
char *site;
{
FILE *fp;
char line[82],filename[80],*given,*check;

sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%s",line);
while(!feof(fp)) {
        given=NULL;  check=NULL;
	/* first do full name comparison */
	if (!strcmp(site,line)) {  fclose(fp);  return 1;  }
	given=site;  check=line;
	/* check if it has wild card */
	while(*check) {
	    if (*check=='*') {
	      ++check;
	      if (!*check) return 1;
	      while (*given && *given!=*check) ++given;
	      }
	    if (*check==*given) { ++check;  ++given; }
	    else goto SKIP;
	    }
	return 1;
      SKIP:
	fscanf(fp,"%s",line);
	}
fclose(fp);
return 0;
}


/*** See if user is banned ***/
user_banned(name)
char *name;
{
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


/*** Attempt to get '\n' terminated line of input from a character
     mode client else store data read so far in user buffer. ***/
get_charclient_line(user,inpstr,len)
UR_OBJECT user;
char *inpstr;
int len;
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
    && ((user->login!=2 && user->login!=1) || (password_echo || user->show_pass))) 
	write(user->socket,inpstr,len);
return 0;
}


/*** Put string terminate char. at first char < 32 ***/
terminate(str)
char *str;
{
int i;
for (i=0;i<ARR_SIZE;++i)  {
	if (*(str+i)<32) {  *(str+i)=0;  return;  } 
	}
str[i-1]=0;
}


/*** Get words from sentence. This function prevents the words in the 
     sentence from writing off the end of a word array element. This is
     difficult to do with sscanf() hence I use this function instead. ***/
wordfind(inpstr)
char *inpstr;
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
clear_words()
{
int w;
for(w=0;w<MAX_WORDS;++w) word[w][0]='\0';
word_count=0;
}


/************ PARSE CONFIG FILE **************/

load_and_parse_config()
{
FILE *fp;
char line[81]; /* Should be long enough */
char c,filename[80];
int i,section_in,got_init,got_rooms;
RM_OBJECT rm1,rm2;
NL_OBJECT nl;

section_in=0;
got_init=0;
got_rooms=0;

sprintf(filename,"%s/%s",DATAFILES,confile);
printf("Parsing config file \"%s\"...\n",filename);
if (!(fp=fopen(filename,"r"))) {
	perror("NUTS: Can't open config file");  boot_exit(1);
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
			else if (!strcmp(wrd[0],"SITES:")) section_in=3; 
				else {
					fprintf(stderr,"NUTS: Unknown section header on line %d.\n",config_line);
					fclose(fp);  boot_exit(1);
					}
		}
	switch(section_in) {
		case 1: parse_init_section();  got_init=1;  break;
		case 2: parse_rooms_section(); got_rooms=1; break;
		case 3: parse_sites_section(); break;
		default:
			fprintf(stderr,"NUTS: Section header expected on line %d.\n",config_line);
			boot_exit(1);
		}
	fgets(line,81,fp);
	}
fclose(fp);

/* See if required sections were present (SITES is optional) and if
   required parameters were set. */
if (!got_init) {
	fprintf(stderr,"NUTS: INIT section missing from config file.\n");
	boot_exit(1);
	}
if (!got_rooms) {
	fprintf(stderr,"NUTS: ROOMS section missing from config file.\n");
	boot_exit(1);
	}
if (!verification[0]) {
	fprintf(stderr,"NUTS: Verification not set in config file.\n");
	boot_exit(1);
	}
if (!port[0]) {
	fprintf(stderr,"NUTS: Main port number not set in config file.\n");
	boot_exit(1);
	}
if (!port[1]) {
	fprintf(stderr,"NUTS: Wiz port number not set in config file.\n");
	boot_exit(1);
	}
if (!port[2]) {
	fprintf(stderr,"NUTS: Link port number not set in config file.\n");
	boot_exit(1);
	}
if (port[0]==port[1] || port[1]==port[2] || port[0]==port[2]) {
	fprintf(stderr,"NUTS: Port numbers must be unique.\n");
	boot_exit(1);
	}
if (room_first==NULL) {
	fprintf(stderr,"NUTS: No rooms configured in config file.\n");
	boot_exit(1);
	}

/* Parsing done, now check data is valid. Check room stuff first. */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
	for(i=0;i<MAX_LINKS;++i) {
		if (!rm1->link_label[i][0]) break;
		for(rm2=room_first;rm2!=NULL;rm2=rm2->next) {
			if (rm1==rm2) continue;
			if (!strcmp(rm1->link_label[i],rm2->label)) {
				rm1->link[i]=rm2;  break;
				}
			}
		if (rm1->link[i]==NULL) {
			fprintf(stderr,"NUTS: Room %s has undefined link label '%s'.\n",rm1->name,rm1->link_label[i]);
			boot_exit(1);
			}
		}
	}

/* Check external links */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
	for(nl=nl_first;nl!=NULL;nl=nl->next) {
		if (!strcmp(nl->service,rm1->name)) {
			fprintf(stderr,"NUTS: Service name %s is also the name of a room.\n",nl->service);
			boot_exit(1);
			}
		if (rm1->netlink_name[0] 
		    && !strcmp(rm1->netlink_name,nl->service)) {
			rm1->netlink=nl;  break;
			}
		}
	if (rm1->netlink_name[0] && rm1->netlink==NULL) {
		fprintf(stderr,"NUTS: Service name %s not defined for room %s.\n",rm1->netlink_name,rm1->name);
		boot_exit(1);
		}
	}

/* Load room descriptions */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
	sprintf(filename,"%s/%s.R",DATAFILES,rm1->name);
	if (!(fp=fopen(filename,"r"))) {
		fprintf(stderr,"NUTS: Can't open description file for room %s.\n",rm1->name);
		sprintf(text,"ERROR: Couldn't open description file for room %s.\n",rm1->name);
		write_syslog(text,0,SYSLOG);
		continue;
		}
	i=0;
	c=getc(fp);
	while(!feof(fp)) {
		if (i==ROOM_DESC_LEN) {
			fprintf(stderr,"NUTS: Description too long for room %s.\n",rm1->name);
			sprintf(text,"ERROR: Description too long for room %s.\n",rm1->name);
			write_syslog(text,0,SYSLOG);
			break;
			}
		rm1->desc[i]=c;  
		c=getc(fp);  ++i;
		}
	rm1->desc[i]='\0';
	fclose(fp);
	}
}



/*** Parse init section ***/
parse_init_section()
{
static int in_section=0;
int op,val;
char *options[]={ 
"mainport","wizport","linkport","system_logging","minlogin_level","mesg_life",
"wizport_level","prompt_def","gatecrash_level","min_private","ignore_mp_level",
"rem_user_maxlevel","rem_user_deflevel","verification","mesg_check_time",
"max_users","heartbeat","login_idle_time","user_idle_time","password_echo",
"ignore_sigterm","auto_connect","max_clones","ban_swearing","crash_action",
"colour_def","time_out_afks","allow_caps_in_name","charecho_def",
"time_out_maxlevel","auto_purge","*"
};

if (!strcmp(wrd[0],"INIT:")) { 
	if (++in_section>1) {
		fprintf(stderr,"NUTS: Unexpected INIT section header on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
op=0;
while(strcmp(options[op],wrd[0])) {
	if (options[op][0]=='*') {
		fprintf(stderr,"NUTS: Unknown INIT option on line %d.\n",config_line);
		boot_exit(1);
		}
	++op;
	}
if (!wrd[1][0]) {
	fprintf(stderr,"NUTS: Required parameter missing on line %d.\n",config_line);
	boot_exit(1);
	}
if (wrd[2][0] && wrd[2][0]!='#') {
	fprintf(stderr,"NUTS: Unexpected word following init parameter on line %d.\n",config_line);
	boot_exit(1);
	}
val=atoi(wrd[1]);
switch(op) {
	case 0: /* main port */
	case 1: /* wiz */
	case 2: /* link */
	if ((port[op]=val)<1 || val>65535) {
		fprintf(stderr,"NUTS: Illegal port number on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 3:  
	if ((system_logging=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: System_logging must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 4:
	if ((minlogin_level=get_level(wrd[1]))==-1) {
		if (strcmp(wrd[1],"NONE")) {
			fprintf(stderr,"NUTS: Unknown level specifier for minlogin_level on line %d.\n",config_line);
			boot_exit(1);	
			}
		minlogin_level=-1;
		}
	return;

	case 5:  /* message lifetime */
	if ((mesg_life=val)<1) {
		fprintf(stderr,"NUTS: Illegal message lifetime on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 6: /* wizport_level */
	if ((wizport_level=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for wizport_level on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 7: /* prompt defaults */
	if ((prompt_def=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Prompt_def must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 8: /* gatecrash level */
	if ((gatecrash_level=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for gatecrash_level on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 9:
	if (val<1) {
		fprintf(stderr,"NUTS: Number too low for min_private_users on line %d.\n",config_line);
		boot_exit(1);
		}
	min_private_users=val;
	return;

	case 10:
	if ((ignore_mp_level=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for ignore_mp_level on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 11: 
	/* Max level a remote user can remotely log in if he doesn't have a local
	   account. ie if level set to WIZ a GOD can only be a WIZ if logging in 
	   from another server unless he has a local account of level GOD */
	if ((rem_user_maxlevel=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for rem_user_maxlevel on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 12:
	/* Default level of remote user who does not have an account on site and
	   connection is from a server of version 3.3.0 or lower. */
	if ((rem_user_deflevel=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for rem_user_deflevel on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 13:
	if (strlen(wrd[1])>VERIFY_LEN) {
		fprintf(stderr,"NUTS: Verification too long on line %d.\n",config_line);
		boot_exit(1);	
		}
	strcpy(verification,wrd[1]);
	return;

	case 14: /* mesg_check_time */
	if (wrd[1][2]!=':'
	    || strlen(wrd[1])>5
	    || !isdigit(wrd[1][0]) 
	    || !isdigit(wrd[1][1])
	    || !isdigit(wrd[1][3]) 
	    || !isdigit(wrd[1][4])) {
		fprintf(stderr,"NUTS: Invalid message check time on line %d.\n",config_line);
		boot_exit(1);
		}
	sscanf(wrd[1],"%d:%d",&mesg_check_hour,&mesg_check_min);
	if (mesg_check_hour>23 || mesg_check_min>59) {
		fprintf(stderr,"NUTS: Invalid message check time on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 15:
	if ((max_users=val)<1) {
		fprintf(stderr,"NUTS: Invalid value for max_users on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 16:
	if ((heartbeat=val)<1) {
		fprintf(stderr,"NUTS: Invalid value for heartbeat on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 17:
	if ((login_idle_time=val)<10) {
		fprintf(stderr,"NUTS: Invalid value for login_idle_time on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 18:
	if ((user_idle_time=val)<10) {
		fprintf(stderr,"NUTS: Invalid value for user_idle_time on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 19: 
	if ((password_echo=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Password_echo must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 20: 
	if ((ignore_sigterm=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Ignore_sigterm must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 21:
	if ((auto_connect=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Auto_connect must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 22:
	if ((max_clones=val)<0) {
		fprintf(stderr,"NUTS: Invalid value for max_clones on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 23:
	if ((ban_swearing=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Ban_swearing must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 24:
	if (!strcmp(wrd[1],"NONE")) crash_action=0;
	else if (!strcmp(wrd[1],"IGNORE")) crash_action=1;
		else if (!strcmp(wrd[1],"REBOOT")) crash_action=2;
			else {
				fprintf(stderr,"NUTS: Crash_action must be NONE, IGNORE or REBOOT on line %d.\n",config_line);
				boot_exit(1);
				}
	return;

	case 25:
	if ((colour_def=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Colour_def must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 26:
	if ((time_out_afks=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Time_out_afks must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 27:
	if ((allow_caps_in_name=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Allow_caps_in_name must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 28:
	if ((charecho_def=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Charecho_def must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 29:
	if ((time_out_maxlevel=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for time_out_maxlevel on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 30: /* auto purge on boot up */
	if ((auto_purge=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Auto-purge must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
}



/*** Parse rooms section ***/
parse_rooms_section()
{
static int in_section=0;
int i;
char *ptr1,*ptr2,c;
RM_OBJECT room;

if (!strcmp(wrd[0],"ROOMS:")) {
	if (++in_section>1) {
		fprintf(stderr,"NUTS: Unexpected ROOMS section header on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
if (!wrd[2][0]) {
	fprintf(stderr,"NUTS: Required parameter(s) missing on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[0])>ROOM_LABEL_LEN) {
	fprintf(stderr,"NUTS: Room label too long on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[1])>ROOM_NAME_LEN) {
	fprintf(stderr,"NUTS: Room name too long on line %d.\n",config_line);
	boot_exit(1);
	}
/* Check for duplicate label or name */
for(room=room_first;room!=NULL;room=room->next) {
	if (!strcmp(room->label,wrd[0])) {
		fprintf(stderr,"NUTS: Duplicate room label on line %d.\n",config_line);
		boot_exit(1);
		}
	if (!strcmp(room->name,wrd[1])) {
		fprintf(stderr,"NUTS: Duplicate room name on line %d.\n",config_line);
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
		fprintf(stderr,"NUTS: Missing link label on line %d.\n",config_line);
		boot_exit(1);
		}
	c=*ptr2;  *ptr2='\0';
	if (!strcmp(ptr1,room->label)) {
		fprintf(stderr,"NUTS: Room has a link to itself on line %d.\n",config_line);
		boot_exit(1);
		}
	strcpy(room->link_label[i],ptr1);
	if (c=='\0') break;
	if (++i>=MAX_LINKS) {
		fprintf(stderr,"NUTS: Too many links on line %d.\n",config_line);
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
			fprintf(stderr,"NUTS: Unknown room access type on line %d.\n",config_line);
			boot_exit(1);
			}
/* Parse external link stuff */
if (!wrd[4][0] || wrd[4][0]=='#') return;
if (!strcmp(wrd[4],"ACCEPT")) {  
	if (wrd[5][0] && wrd[5][0]!='#') {
		fprintf(stderr,"NUTS: Unexpected word following ACCEPT keyword on line %d.\n",config_line);
		boot_exit(1);
		}
	room->inlink=1;  
	return;
	}
if (!strcmp(wrd[4],"CONNECT")) {
	if (!wrd[5][0]) {
		fprintf(stderr,"NUTS: External link name missing on line %d.\n",config_line);
		boot_exit(1);
		}
	if (wrd[6][0] && wrd[6][0]!='#') {
		fprintf(stderr,"NUTS: Unexpected word following external link name on line %d.\n",config_line);
		boot_exit(1);
		}
	strcpy(room->netlink_name,wrd[5]);
	return;
	}
fprintf(stderr,"NUTS: Unknown connection option on line %d.\n",config_line);
boot_exit(1);
}



/*** Parse sites section ***/
parse_sites_section()
{
NL_OBJECT nl;
static int in_section=0;

if (!strcmp(wrd[0],"SITES:")) { 
	if (++in_section>1) {
		fprintf(stderr,"NUTS: Unexpected SITES section header on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
if (!wrd[3][0]) {
	fprintf(stderr,"NUTS: Required parameter(s) missing on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[0])>SERV_NAME_LEN) {
	fprintf(stderr,"NUTS: Link name length too long on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[3])>VERIFY_LEN) {
	fprintf(stderr,"NUTS: Verification too long on line %d.\n",config_line);
	boot_exit(1);
	}
if ((nl=create_netlink())==NULL) {
	fprintf(stderr,"NUTS: Memory allocation failure creating netlink on line %d.\n",config_line);
	boot_exit(1);
	}
if (!wrd[4][0] || wrd[4][0]=='#' || !strcmp(wrd[4],"ALL")) nl->allow=ALL;
else if (!strcmp(wrd[4],"IN")) nl->allow=IN;
	else if (!strcmp(wrd[4],"OUT")) nl->allow=OUT;
		else {
			fprintf(stderr,"NUTS: Unknown netlink access type on line %d.\n",config_line);
			boot_exit(1);
			}
if ((nl->port=atoi(wrd[2]))<1 || nl->port>65535) {
	fprintf(stderr,"NUTS: Illegal port number on line %d.\n",config_line);
	boot_exit(1);
	}
strcpy(nl->service,wrd[0]);
strtolower(wrd[1]);
strcpy(nl->site,wrd[1]);
strcpy(nl->verification,wrd[3]);
}


yn_check(wd)
char *wd;
{
if (!strcmp(wd,"YES")) return 1;
if (!strcmp(wd,"NO")) return 0;
return -1;
}


onoff_check(wd)
char *wd;
{
if (!strcmp(wd,"ON")) return 1;
if (!strcmp(wd,"OFF")) return 0;
return -1;
}


/************ INITIALISATION FUNCTIONS *************/

/*** Initialise globals ***/
init_globals()
{
int i;

verification[0]='\0';
port[0]=0;
port[1]=0;
port[2]=0;
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
allow_caps_in_name=1;
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
nl_first=NULL;
nl_last=NULL;
clear_words();
time(&boot_time);
user_number=0;
jailed_cnt=0;
new_cnt=0;
user_cnt=0;
super_cnt=0;
wiz_cnt=0;
arch_cnt=0;
god_cnt=0;
logons_old=0;
logons_new=0;
purge_count=0;
users_purged=0;
purge_date=1;
/* create one sorted list of commands */
for (i=0;i<NUM_CMDS;++i) { ordcom[i].name[0]='\0'; ordcom[i].level=0; }
sug_num=0;
forwarding=1;
logon_flag=0;
auto_purge=0;
}


/*** Initialise the signal traps etc ***/
init_signals()
{
void sig_handler();

signal(SIGTERM,sig_handler);
signal(SIGSEGV,sig_handler);
signal(SIGBUS,sig_handler);
signal(SIGILL,SIG_IGN);
signal(SIGTRAP,SIG_IGN);
signal(SIGIOT,SIG_IGN);
signal(SIGTSTP,SIG_IGN);
signal(SIGCONT,SIG_IGN);
signal(SIGHUP,SIG_IGN);
signal(SIGINT,SIG_IGN);
signal(SIGQUIT,SIG_IGN);
signal(SIGABRT,SIG_IGN);
signal(SIGFPE,SIG_IGN);
signal(SIGPIPE,SIG_IGN);
signal(SIGTTIN,SIG_IGN);
signal(SIGTTOU,SIG_IGN);
}


/*** Talker signal handler function. Can either shutdown , ignore or reboot
	if a unix error occurs though if we ignore it we're living on borrowed
	time as usually it will crash completely after a while anyway. ***/
void sig_handler(sig)
int sig;
{
force_listen=1;
switch(sig) {
	case SIGTERM:
	if (ignore_sigterm) {
		write_syslog("SIGTERM signal received - ignoring.\n",1,SYSLOG);
		return;
		}
	write_room(NULL,"\n\n~OLSYSTEM:~FR~LI SIGTERM received, initiating shutdown!\n\n");
	talker_shutdown(NULL,"a termination signal (SIGTERM)",0); 

	case SIGSEGV:
	switch(crash_action) {
		case 0:	
		write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI PANIC - Segmentation fault, initiating shutdown!\n\n");
		talker_shutdown(NULL,"a segmentation fault (SIGSEGV)",0); 

		case 1:	
		write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI WARNING - A segmentation fault has just occured!\n\n");
		write_syslog("WARNING: A segmentation fault occured!\n",1,SYSLOG);
		longjmp(jmpvar,0);

		case 2:
		write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI PANIC - Segmentation fault, initiating reboot!\n\n");
		talker_shutdown(NULL,"a segmentation fault (SIGSEGV)",1); 
		}

	case SIGBUS:
	switch(crash_action) {
		case 0:
		write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI PANIC - Bus error, initiating shutdown!\n\n");
		talker_shutdown(NULL,"a bus error (SIGBUS)",0);

		case 1:
		write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI WARNING - A bus error has just occured!\n\n");
		write_syslog("WARNING: A bus error occured!\n",1,SYSLOG);
		longjmp(jmpvar,0);

		case 2:
		write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI PANIC - Bus error, initiating reboot!\n\n");
		talker_shutdown(NULL,"a bus error (SIGBUS)",1);
		}
	}
}

	
/*** Initialise sockets on ports ***/
init_sockets()
{
struct sockaddr_in bind_addr;
int i,on,size;

printf("Initialising sockets on ports: %d, %d, %d\n",port[0],port[1],port[2]);
on=1;
size=sizeof(struct sockaddr_in);
bind_addr.sin_family=AF_INET;
bind_addr.sin_addr.s_addr=INADDR_ANY;
for(i=0;i<3;++i) {
	/* create sockets */
	if ((listen_sock[i]=socket(AF_INET,SOCK_STREAM,0))==-1) boot_exit(i+2);

	/* allow reboots on port even with TIME_WAITS */
	setsockopt(listen_sock[i],SOL_SOCKET,SO_REUSEADDR,(char *)&on,sizeof(on));

	/* bind sockets and set up listen queues */
	bind_addr.sin_port=htons(port[i]);
	if (bind(listen_sock[i],(struct sockaddr *)&bind_addr,size)==-1) 
		boot_exit(i+5);
	if (listen(listen_sock[i],10)==-1) boot_exit(i+8);

	/* Set to non-blocking , do we need this? Not really. */
	fcntl(listen_sock[i],F_SETFL,O_NDELAY);
	}
}


/*** Initialise connections to remote servers. Basically this tries to connect
     to the services listed in the config file and it puts the open sockets in 
	the NL_OBJECT linked list which the talker then uses ***/
init_connections()
{
NL_OBJECT nl;
RM_OBJECT rm;
int ret,cnt=0;

printf("Connecting to remote servers...\n");
errno=0;
for(rm=room_first;rm!=NULL;rm=rm->next) {
	if ((nl=rm->netlink)==NULL) continue;
	++cnt;
	printf("  Trying service %s at %s %d: ",nl->service,nl->site,nl->port);
	fflush(stdout);
	if ((ret=connect_to_site(nl))) {
		if (ret==1) {
			printf("%s.\n",sys_errlist[errno]);
			sprintf(text,"NETLINK: Failed to connect to %s: %s.\n",nl->service,sys_errlist[errno]);
			}
		else {
			printf("Unknown hostname.\n");
			sprintf(text,"NETLINK: Failed to connect to %s: Unknown hostname.\n",nl->service);
			}
		write_syslog(text,1,SYSLOG);
		}
	else {
		printf("CONNECTED.\n");
		sprintf(text,"NETLINK: Connected to %s (%s %d).\n",nl->service,nl->site,nl->port);
		write_syslog(text,1,SYSLOG);
		nl->connect_room=rm;
		}
	}
if (cnt) printf("  See system log for any further information.\n");
else printf("  No remote connections configured.\n");
}


/*** Do the actual connection ***/
connect_to_site(nl)
NL_OBJECT nl;
{
struct sockaddr_in con_addr;
struct hostent *he;
int inetnum;
char *sn;

sn=nl->site;
/* See if number address */
while(*sn && (*sn=='.' || isdigit(*sn))) sn++;

/* Name address given */
if(*sn) {
	if(!(he=gethostbyname(nl->site))) return 2;
	memcpy((char *)&con_addr.sin_addr,he->h_addr,(size_t)he->h_length);
	}
/* Number address given */
else {
	if((inetnum=inet_addr(nl->site))==-1) return 1;
	memcpy((char *)&con_addr.sin_addr,(char *)&inetnum,(size_t)sizeof(inetnum));
	}
/* Set up stuff and disable interrupts */
if ((nl->socket=socket(AF_INET,SOCK_STREAM,0))==-1) return 1;
con_addr.sin_family=AF_INET;
con_addr.sin_port=htons(nl->port);
signal(SIGALRM,SIG_IGN);

/* Attempt the connect. This is where the talker may hang. */
if (connect(nl->socket,(struct sockaddr *)&con_addr,sizeof(con_addr))==-1) {
	reset_alarm();  return 1;
	}
reset_alarm();
nl->type=OUTGOING;
nl->stage=VERIFYING;
nl->last_recvd=time(0);
return 0;
}

	

/************* WRITE FUNCTIONS ************/

/*** Write a NULL terminated string to a socket ***/
write_sock(sock,str)
int sock;
char *str;
{
write(sock,str,strlen(str));
}



/*** Send message to user ***/
write_user(user,str)
UR_OBJECT user;
char *str;
{
int buffpos,sock,i,cnt;
char *start,buff[OUT_BUFF_SIZE],mesg[ARR_SIZE],*colour_com_strip();

if (user==NULL) return;
if (user->type==REMOTE_TYPE) {
	if (user->netlink->ver_major<=3 
	    && user->netlink->ver_minor<2) str=colour_com_strip(str);
	if (str[strlen(str)-1]!='\n') 
		sprintf(mesg,"MSG %s\n%s\nEMSG\n",user->name,str);
	else sprintf(mesg,"MSG %s\n%sEMSG\n",user->name,str);
	write_sock(user->netlink->socket,mesg);
	return;
	}
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
		/* See if its a / before a ~ , if so then we print colour command
		   as text 
		   Changed / to a % -- Andy */
		if (*str=='%' && *(str+1)=='~') {  ++str;  continue;  }
		if (str!=start && *str=='~' && *(str-1)=='%') {
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
				if (!strncmp(str,colcom[i],2)) {
					if (user->colour) {
						memcpy(buff+buffpos,colcode[i],strlen(colcode[i]));
						buffpos+=strlen(colcode[i])-1;  
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
     variable; if 1 then above else below ***/
write_level(level,above,str,user)
int level,above;
char *str;
UR_OBJECT user;
{
UR_OBJECT u;

for(u=user_first;u!=NULL;u=u->next) {
        if ((check_igusers(u,user))!=-1 && user->level<GOD) continue;
	if ((u->ignwiz && (com_num==WIZSHOUT || com_num==WIZEMOTE)) || (u->ignlogons && logon_flag)) continue;
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
write_room(rm,str)
RM_OBJECT rm;
char *str;
{
write_room_except(rm,str,NULL);
}



/*** Write to everyone in room rm except for "user". If rm is NULL write 
     to all rooms. ***/
write_room_except(rm,str,user)
RM_OBJECT rm;
char *str;
UR_OBJECT user;
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
	}
}



/*** Write a string to system log ***/
write_syslog(str,write_time,type)
char *str;
int write_time,type;
{
FILE *fp;

switch(type) {
 case SYSLOG: if (!system_logging || !(fp=fopen(MAINSYSLOG,"a"))) return;
              break;
 case REQLOG: if (!system_logging || !(fp=fopen(REQSYSLOG,"a"))) return;
              break;
 case NETLOG: if (!system_logging || !(fp=fopen(NETSYSLOG,"a"))) return;
              break;
 }
if (!write_time) fputs(str,fp);
else fprintf(fp,"%02d/%02d %02d:%02d:%02d: %s",tmday,tmonth+1,thour,tmin,tsec,str);
fclose(fp);
}



/******** LOGIN/LOGOUT FUNCTIONS ********/

/*** Login function. Lots of nice inline code :) ***/
login(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
int i;
char name[ARR_SIZE],passwd[ARR_SIZE],filename[80];

name[0]='\0';  passwd[0]='\0';
switch(user->login) {
	case 3:
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
		sprintf(text,"\nAmnuts version %s (modified NUTS %s)\n\nGive me a name: ",AMNUTSVER,NUTSVER);
		write_user(user,text);  return;
		}
	if (!strcmp(name,"Version")) {
		sprintf(text,"\nAmnuts version %s (modified NUTS %s)\n\nGive me a name: ",AMNUTSVER,NUTSVER);
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
	if (!allow_caps_in_name) strtolower(name);
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
		if (new_banned(user->site)) {
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
	user->login=2;
	return;

	case 2:
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
		strcpy(user->pass,(char *)crypt(passwd,"NU"));
		write_user(user,"\n");
		sprintf(filename,"%s/%s",MISCFILES,RULESFILE);
		if (more(NULL,user->socket,filename)) {
			write_user(user,"\nBy typing your password in again you are accepting the above rules.\n");
			write_user(user,"If you do not agree with the rules, then disconnect now.\n");
			}
		write_user(user,"\nPlease confirm password: ");
		user->login=1;
		}
	else {
		if (!strcmp(user->pass,(char *)crypt(passwd,"NU"))) {
		  if (!(in_userlist(user->name))) { add_userlist(user->name); }
		  echo_on(user);  logons_old++;  connect_user(user);  return;
		  }
		write_user(user,"\n\nIncorrect login.\n\n");
		attempts(user);
		}
	return;

	case 1:
	sscanf(inpstr,"%s",passwd);
	if (strcmp(user->pass,(char*)crypt(passwd,"NU"))) {
		write_user(user,"\n\nPasswords do not match.\n\n");
		attempts(user);
		return;
		}
	echo_on(user);
	strcpy(user->desc,"is a newbie needing a desc.");
	strcpy(user->in_phrase,"enters");	
	strcpy(user->out_phrase,"goes");	
	user->last_site[0]='\0';
	user->level=NEW;
	user->unarrest=NEW;
	user->muzzled=0;
	user->command_mode=0;
	user->prompt=prompt_def;
	user->colour=colour_def;
	user->charmode_echo=charecho_def;
	save_user_details(user,1);
	add_userlist(user->name);
	add_level_list(user->name,user->level);
	add_history(user->name,1,"Was initially created.\n");
	sprintf(text,"New user \"%s\" created.\n",user->name);
	write_syslog(text,1,SYSLOG);
	connect_user(user);
	logons_new++;  user_number++;
	}
}
	


/*** Count up attempts made by user to login ***/
attempts(user)
UR_OBJECT user;
{
user->attempts++;
if (user->attempts==3) {
	write_user(user,"\nMaximum attempts reached.\n\n");
	disconnect_user(user);  return;
	}
user->login=3;
user->pass[0]='\0';
write_user(user,"Give me a name: ");
echo_on(user);
}



/*** Load the users details ***/
load_user_details(user)
UR_OBJECT user;
{
FILE *fp;
char line[81],filename[80];
int temp1,temp2,temp3,temp4;

sprintf(filename,"%s/%s.D",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) return 0;

/* Password */
fscanf(fp,"%s",user->pass);
/* times, levels, and important stats */
fscanf(fp,"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",&temp1,&temp2,&user->last_login_len,&temp3,&user->level,&user->prompt,&user->muzzled,&user->charmode_echo,&user->command_mode,&user->vis,&user->monitor,&user->vote,&user->unarrest,&user->logons,&user->accreq,&user->mail_verified);
/* stats set using the 'set' function */
fscanf(fp,"%d %d %d %d %d %d %d %d %d %d",&user->gender,&user->age,&user->wrap,&user->pager,&user->hideemail,&user->colour,&user->lroom,&user->alert,&user->autofwd,&user->show_pass);
/* ignore status */
fscanf(fp,"%d %d %d %d %d %d %d %d",&user->ignall,&user->igntells,&user->ignshouts,&user->ignpics,&user->ignlogons,&user->ignwiz,&user->igngreets,&user->ignbeeps);
/* Gun fight information */
fscanf(fp,"%d %d %d %d %d %d",&user->hits,&user->misses,&user->deaths,&user->kills,&user->bullets,&user->hps);
/* user expires and date */
fscanf(fp,"%d %d",&user->expire,&user->t_expire);
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
fclose(fp);
user->last_login=(time_t)temp1;
user->total_login=(time_t)temp2;
user->read_mail=(time_t)temp3;
get_macros(user);
return 1;
}



/*** Save a users stats ***/
save_user_details(user,save_current)
UR_OBJECT user;
int save_current;
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
/* times, levels, and important stats */
if (save_current)
	fprintf(fp,"%d %d %d ",(int)time(0),(int)user->total_login,(int)(time(0)-user->last_login));
else fprintf(fp,"%d %d %d ",(int)user->last_login,(int)user->total_login,user->last_login_len);
fprintf(fp,"%d %d %d %d %d %d %d %d %d %d %d %d %d\n",(int)user->read_mail,user->level,user->prompt,user->muzzled,user->charmode_echo,user->command_mode,user->vis,user->monitor,user->vote,user->unarrest,user->logons,user->accreq,user->mail_verified);
/* stats set using the 'set' function */
fprintf(fp,"%d %d %d %d %d %d %d %d %d %d\n",user->gender,user->age,user->wrap,user->pager,user->hideemail,user->colour,user->lroom,user->alert,user->autofwd,user->show_pass);
/* ignore status */
fprintf(fp,"%d %d %d %d %d %d %d %d\n",user->ignall,user->igntells,user->ignshouts,user->ignpics,user->ignlogons,user->ignwiz,user->igngreets,user->ignbeeps);
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
fclose(fp);
return 1;
}


/*** Connect the user to the talker proper ***/
connect_user(user)
UR_OBJECT user;
{
UR_OBJECT u,u2;
RM_OBJECT rm,get_room();
char temp[30],rmname[ROOM_NAME_LEN+20],filename[80],text2[ARR_SIZE];
char *see[]={"INVISIBLE","VISIBLE"};
int cnt,secs,days,hours,mins;

/* See if user already connected */
for(u=user_first;u!=NULL;u=u->next) {
	if (user!=u && user->type!=CLONE_TYPE && !strcmp(user->name,u->name)) {
		rm=u->room;
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
		write_user(user,"\n\nYou are already connected - switching to old session...\n");
		sprintf(text,"%s swapped sessions.\n",user->name);
		write_syslog(text,1,SYSLOG);
		close(u->socket);
		u->socket=user->socket;
		strcpy(u->site,user->site);
		u->site_port=user->site_port;
		destruct_user(user);
		num_of_logins--;
		if (rm==NULL) {
			sprintf(text,"ACT %s look\n",u->name);
			write_sock(u->netlink->socket,text);
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
/* Announce users logon. You're probably wondering why Ive done it this strange
   way , well its to avoid a minor bug that arose when as in 3.3.1 it created 
   the string in 2 parts and sent the parts seperately over a netlink. If you 
   want more details email me. */	
cls(user);
more(user,user->socket,MOTD2); 

logon_flag=1;
if (!user->vis && user->level<com_level[INVIS]) user->vis=1;
if (user->level==JAILED) {
    user->room=get_room(default_jail);
    if (user->room==NULL) user->room=room_first;
    }
else check_start_room(user);
if (user->room==room_first) rmname[0]='\0';
else sprintf(rmname,"~RS (%s)~OL",user->room->name);
if (user->vis) {
  if (user->level<WIZ) sprintf(text,"~OL[Entering%s is:~RS %s %s~RS~OL]\n",rmname,user->name,user->desc);
  else sprintf(text,"\07~OL[Entering%s is:~RS %s %s~RS~OL]\n",rmname,user->name,user->desc);
  write_room(NULL,text);
  sprintf(text,"~FT%s (Site %s : Site Port %d : Talker Port %d)\n",user->name,user->site,user->site_port,user->port);
  write_level(ARCH,1,text,NULL);
}
else {
  if (user->level<ARCH) write_room_except(user->room,invisenter,user);
  sprintf(text,"[ INVIS ] ~OL[Entering%s is:~RS %s %s~RS~OL]\n",rmname,user->name,user->desc);
  write_level(WIZ,1,text,NULL);
  sprintf(text,"[ INVIS ] ~FT%s (Site %s : Site Port %d : Talker Port : %d)\n",user->name,user->site,user->site_port,user->port);
  write_level(ARCH,1,text,NULL); 
}
logon_flag=0;
user->logons++;
if (user->level==NEW) user->t_expire=time(0)+(NEWBIE_EXPIRES*86400);
else user->t_expire=time(0)+(USER_EXPIRES*86400);

if (user->last_site[0]) {
	sprintf(temp,"%s",ctime(&user->last_login));
	temp[strlen(temp)-1]=0;
	sprintf(text,"~OLWelcome~RS %s...\n\n~BBYou were last logged in on %s from %s.\n\n",user->name,temp,user->last_site);
	}
else sprintf(text,"~OLWelcome~RS %s...\n\n",user->name);
write_user(user,text);
sprintf(text,"~FTYour level is:~RS~OL %s\n",level_name[user->level]);
write_user(user,text);
text2[0]='\0';
if (user->level>=com_level[INVIS]) {
	sprintf(text,"You are currently ~OL%s~RS",see[user->vis]);
	if (user->level>=com_level[MONITOR]) {
	    sprintf(text2," and your monitor is ~OL%s~RS\n",offon[user->monitor]);
	    strcat(text,text2);
	    }
	else strcat(text,"\n");
	write_user(user,text);
	}
else if (user->level>=com_level[MONITOR]) {
       sprintf(text,"Your monitor is currently ~OL%s~RS\n",offon[user->monitor]);
       write_user(user,text);
       }
write_user(user,"\n");

user->last_login=time(0); /* set to now */
look(user);
if (has_unread_mail(user)) write_user(user,"\07~FT~OL~LI*** YOU HAVE UNREAD MAIL ***\n");
else if ((cnt=mail_count(user))!=0) {
  sprintf(text,"~FT*** You have ~RS~OL%d~RS~FT messages in your mail box ***\n",cnt);
  write_user(user,text);
  }
prompt(user);

/* write to syslog and set up some vars */
sprintf(text,"%s logged in on port %d from %s:%d.\n",user->name,user->port,user->site,user->site_port);
write_syslog(text,1,SYSLOG);
num_of_users++;
num_of_logins--;
user->login=0;
}


/*** Disconnect user from talker ***/
disconnect_user(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;
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
	sprintf(text,"On %s, %d %s, for a total of %d hours and %d minutes.\n\n",day[twday],tmday,month[tmonth],hours,mins);
	write_user(user,text);
	close(user->socket);
	logon_flag=1;
	if (user->vis) {
	  sprintf(text,"~OL[Leaving is:~RS %s %s~RS~OL]\n",user->name,user->desc);
	  write_room(NULL,text);
	  }
	else {
	  sprintf(text,"[ INVIS ] ~OL[Leaving is:~RS %s %s~RS~OL]\n",user->name,user->desc);
	  write_level(WIZ,1,text,NULL);
	  }
	logon_flag=0;
	if (user->room==NULL) {
		sprintf(text,"REL %s\n",user->name);
		write_sock(user->netlink->socket,text);
		for(nl=nl_first;nl!=NULL;nl=nl->next) 
			if (nl->mesg_user==user) {  
				nl->mesg_user=(UR_OBJECT)-1;  break;  
				}
		}
	}
else {
	write_user(user,"\n~FR~OLYou are pulled back in disgrace to your own domain...\n");
	sprintf(text,"REMVD %s\n",user->name);
	write_sock(user->netlink->socket,text);
	sprintf(text,"~FR~OL%s is banished from here!\n",user->name);
	write_room_except(rm,text,user);
	sprintf(text,"NETLINK: Remote user %s removed.\n",user->name);
	write_syslog(text,1,NETLOG);
	}
if (user->malloc_start!=NULL) free(user->malloc_start);
num_of_users--;

/* Destroy any clones */
destroy_user_clones(user);
destruct_user(user);
reset_access(rm);
destructed=0;
}


/*** Tell telnet not to echo characters - for password entry ***/
echo_off(user)
UR_OBJECT user;
{
char seq[4];

if (password_echo || user->show_pass) return;
sprintf(seq,"%c%c%c",255,251,1);
write_user(user,seq);
}


/*** Tell telnet to echo characters ***/
echo_on(user)
UR_OBJECT user;
{
char seq[4];

if (password_echo || user->show_pass) return;
sprintf(seq,"%c%c%c",255,252,1);
write_user(user,seq);
}



/************ MISCELLANIOUS FUNCTIONS *************/

/*** Stuff that is neither speech nor a command is dealt with here ***/
misc_ops(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
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
	if (toupper(inpstr[0])=='E'
	    || more(user,user->socket,user->page_file)!=1) {
		user->misc_op=0;  user->filepos=0;  user->page_file[0]='\0';
		prompt(user); 
		}
	return 1;

	case 3: /* writing on board */
	case 4: /* Writing mail */
	case 5: /* doing profile */
	editor(user,inpstr);  return 1;

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
	editor(user,inpstr);  return 1;

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
	if (toupper(inpstr[0])=='E') {
		user->misc_op=0;  user->hwrap_lev=0;  user->hwrap_com=0;
		prompt(user);
		}
	else help_commands(user,1);
	return 1;

        case 13:
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

        case 14:
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
	}
return 0;
}


/*** The editor used for writing profiles, mail and messages on the boards ***/
editor(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int cnt,line;
char *edprompt="\n~FG(~OLS~RS~FG)ave~RS, ~FT(~OLV~RS~FT)iew~RS, ~FY(~OLR~RS~FY)edo~RS or ~FR(~OLA~RS~FR)bort~RS: ";
char *ptr;

if (user->edit_op) {
	switch(toupper(*inpstr)) {
	        case 'S':
/* if (*user->malloc_end--!='\n') *user->malloc_end--='\n'; */
		sprintf(text,"%s finishes composing some text.\n",user->name);
		write_room_except(user->room,text,user);
		switch(user->misc_op) {
			case 3: write_board(user,NULL,1);  break;
			case 4: smail(user,NULL,1);  break;
			case 5: enter_profile(user,1);  break;
			case 8: suggestions(user,1);  break;
			case 9: level_mail(user,inpstr,1);  break;
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
		sprintf(text,"%s gives up composing some text.\n",user->name);
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
		}
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
	sprintf(text,"%s starts composing some text...\n",user->name);
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
sprintf(text,"%s gives up composing some text.\n",user->name);
write_room_except(user->room,text,user);
editor_done(user);
}


/*** Reset some values at the end of editing ***/
editor_done(user)
UR_OBJECT user;
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


/*** Record speech and emotes in the room. ***/
record(rm,str)
RM_OBJECT rm;
char *str;
{
strncpy(rm->revbuff[rm->revline],str,REVIEW_LEN);
rm->revbuff[rm->revline][REVIEW_LEN]='\n';
rm->revbuff[rm->revline][REVIEW_LEN+1]='\0';
rm->revline=(rm->revline+1)%REVIEW_LINES;
}


/*** Records tells and pemotes sent to the user. ***/
record_tell(user,str)
UR_OBJECT user;
char *str;
{
strncpy(user->revbuff[user->revline],str,REVIEW_LEN);
user->revbuff[user->revline][REVIEW_LEN]='\n';
user->revbuff[user->revline][REVIEW_LEN+1]='\0';
user->revline=(user->revline+1)%REVTELL_LINES;
}



/*** Set room access back to public if not enough users in room ***/
reset_access(rm)
RM_OBJECT rm;
{
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



/*** Exit because of error during bootup ***/
boot_exit(code)
int code;
{
switch(code) {
	case 1:
	write_syslog("BOOT FAILURE: Error while parsing configuration file.\n",0,SYSLOG);
	exit(1);

	case 2:
	perror("NUTS: Can't open main port listen socket");
	write_syslog("BOOT FAILURE: Can't open main port listen socket.\n",0,SYSLOG);
	exit(2);

	case 3:
	perror("NUTS: Can't open wiz port listen socket");
	write_syslog("BOOT FAILURE: Can't open wiz port listen socket.\n",0,SYSLOG);
	exit(3);

	case 4:
	perror("NUTS: Can't open link port listen socket");
	write_syslog("BOOT FAILURE: Can't open link port listen socket.\n",0,SYSLOG);
	exit(4);

	case 5:
	perror("NUTS: Can't bind to main port");
	write_syslog("BOOT FAILURE: Can't bind to main port.\n",0,SYSLOG);
	exit(5);

	case 6:
	perror("NUTS: Can't bind to wiz port");
	write_syslog("BOOT FAILURE: Can't bind to wiz port.\n",0,SYSLOG);
	exit(6);

	case 7:
	perror("NUTS: Can't bind to link port");
	write_syslog("BOOT FAILURE: Can't bind to link port.\n",0,SYSLOG);
	exit(7);
	
	case 8:
	perror("NUTS: Listen error on main port");
	write_syslog("BOOT FAILURE: Listen error on main port.\n",0,SYSLOG);
	exit(8);

	case 9:
	perror("NUTS: Listen error on wiz port");
	write_syslog("BOOT FAILURE: Listen error on wiz port.\n",0,SYSLOG);
	exit(9);

	case 10:
	perror("NUTS: Listen error on link port");
	write_syslog("BOOT FAILURE: Listen error on link port.\n",0,SYSLOG);
	exit(10);

	case 11:
	perror("NUTS: Failed to fork");
	write_syslog("BOOT FAILURE: Failed to fork.\n",0,SYSLOG);
	exit(11);
	}
}



/*** User prompt ***/
prompt(user)
UR_OBJECT user;
{
int hr,min;

if (no_prompt) return;
if (user->type==REMOTE_TYPE) {
	sprintf(text,"PRM %s\n",user->name);
	write_sock(user->netlink->socket,text);  
	return;
	}
if (user->command_mode && !user->misc_op) {  
	if (!user->vis) write_user(user,"~FTCOM+> ");
	else write_user(user,"~FTCOM> ");  
	return;  
	}
if (!user->prompt || user->misc_op) return;
hr=(int)(time(0)-user->last_login)/3600;
min=((int)(time(0)-user->last_login)%3600)/60;
if (!user->vis)
	sprintf(text,"~FT<%02d:%02d, %02d:%02d, %s+>\n",thour,tmin,hr,min,user->name);
else sprintf(text,"~FT<%02d:%02d, %02d:%02d, %s>\n",thour,tmin,hr,min,user->name);
write_user(user,text);
}



/*** Page a file out to user. Colour commands in files will only work if 
     user!=NULL since if NULL we dont know if his terminal can support colour 
     or not. Return values: 
	        0 = cannot find file, 1 = found file, 2 = found and finished ***/
more(user,sock,filename)
UR_OBJECT user;
int sock;
char *filename;
{
int i,buffpos,num_chars,lines,retval,len,cnt,pager;
char buff[OUT_BUFF_SIZE],text2[83],*str,*colour_com_strip();
FILE *fp;

if (!(fp=fopen(filename,"r"))) {
	if (user!=NULL) user->filepos=0;  
	return 0;
	}
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
			if (*str=='%' && *(str+1)=='~') {  ++str;  continue;  }
			if (str!=text && *str=='~' && *(str-1)=='%') {
				*(buff+buffpos)=*str;  goto CONT;
				}
			if (*str=='~') {
				if (buffpos>OUT_BUFF_SIZE-6) {
					write(sock,buff,buffpos);  buffpos=0;
					}
				++str;
				for(i=0;i<NUM_COLS;++i) {
					if (!strncmp(str,colcom[i],2)) {
						if (user!=NULL && user->colour) {
							memcpy(buffpos+buff,colcode[i],strlen(colcode[i]));
							buffpos+=strlen(colcode[i])-1;
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
if (user==NULL) {  fclose(fp);  return 2;  write_sock(sock,"\n",1); };
if (feof(fp)) {
	user->filepos=0;  no_prompt=0;  retval=2;
	write_user(user,"\n");
	}
else  {
	/* store file position and file name */
	user->filepos+=num_chars;
	strcpy(user->page_file,filename);
	/* We use E here instead of Q because when on a remote system and
	   in COMMAND mode the Q will be intercepted by the home system and 
	   quit the user */
	write_user(user,"~BB~FG-=[*]=- PRESS <RETURN>, E TO EXIT:~RS ");
	no_prompt=1;
	}
fclose(fp);
return retval;
}



/*** Set global vars. hours,minutes,seconds,date,day,month,year ***/
set_date_time()
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



/*** Return pos. of second word in inpstr ***/
char *remove_first(inpstr)
char *inpstr;
{
char *pos=inpstr;
while(*pos<33 && *pos) ++pos;
while(*pos>32) ++pos;
while(*pos<33 && *pos) ++pos;
return pos;
}


/*** Get user struct pointer from name ***/
UR_OBJECT get_user(name)
char *name;
{
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
UR_OBJECT get_user_name(user,i_name)
UR_OBJECT user;
char *i_name;
{
UR_OBJECT u,last;
int found=0;
char name[USER_NAME_LEN], text2[ARR_SIZE];

strncpy(name,i_name,USER_NAME_LEN);
name[0]=toupper(name[0]); text[0]=0;
for (u=user_first;u!=NULL;u=u->next)
    if (!strcmp(u->name,name) && u->room!=NULL)
    return u;
for (u=user_first;u!=NULL;u=u->next) {
	 if (u->type==CLONE_TYPE) continue;
    if (instr(u->name,name) != -1) {
       strcat(text,u->name);
		 strcat(text, "  ");
		 found++;
       last=u;
       }
    }
if (found == 0) return NULL;
if (found >1) {
    write_user(user, "Name is not unique.\n");
	 sprintf(text2,"   %s\n",text);
	 write_user(user,text2);
	 return NULL;
	 }
else  return(last);
}


/*** Get room struct pointer from abbreviated name ***/
RM_OBJECT get_room(name)
char *name;
{
RM_OBJECT rm;

for(rm=room_first;rm!=NULL;rm=rm->next)
     if (!strncmp(rm->name,name,strlen(name))) return rm;
return NULL;
}


/*** Return level value based on level name ***/
get_level(name)
char *name;
{
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
has_room_access(user,rm)
UR_OBJECT user;
RM_OBJECT rm;
{
int i=0;
while (priv_room[i].name[0]!='*') {
  if (!strcmp(rm->name,priv_room[i].name)
      && user->level<priv_room[i].level
      && user->invite_room!=rm) return 0;
  i++;
  }
if ((rm->access & PRIVATE) 
    && user->level<gatecrash_level 
    && user->invite_room!=rm
    && !((rm->access & FIXED) && user->level>=WIZ)) return 0;
return 1;
}


/*** See if user has unread mail, mail file has last read time on its 
     first line ***/
has_unread_mail(user)
UR_OBJECT user;
{
FILE *fp;
int tm;
char filename[80];

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%d",&tm);
fclose(fp);
if (tm>(int)user->read_mail) return 1;
return 0;
}


/*** This is function that sends mail to other users ***/
send_mail(user,to,ptr,iscopy)
UR_OBJECT user;
char *to,*ptr;
int iscopy;
{
NL_OBJECT nl;
FILE *infp,*outfp;
char *c,d,*service,filename[80],line[DNL+1],cc[4],header[ARR_SIZE];

/* See if remote mail */
c=to;  service=NULL;
while(*c) {
	if (*c=='@') {  
		service=c+1;  *c='\0'; 
		for(nl=nl_first;nl!=NULL;nl=nl->next) {
			if (!strcmp(nl->service,service) && nl->stage==UP) {
				send_external_mail(nl,user,to,ptr);
				return;
				}
			}
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
/* Write current time on first line of tempfile */
fprintf(outfp,"%d\r",(int)time(0));

/* Copy current mail file into tempfile if it exists */
sprintf(filename,"%s/%s.M",USERFILES,to);
if (infp=fopen(filename,"r")) {
	/* Discard first line of mail file. */
	fgets(line,DNL,infp);

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
	if (user->type==REMOTE_TYPE)
		sprintf(header,"~OLFrom: %s@%s  %s %s\n",user->name,user->netlink->service,long_date(0),cc);
	else sprintf(header,"~OLFrom: %s  %s %s\n",user->name,long_date(0),cc);
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


/*** Spool mail file and ask for confirmation of users existence on remote
	site ***/
send_external_mail(nl,user,to,ptr)
NL_OBJECT nl;
UR_OBJECT user;
char *to,*ptr;
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
write_user(user,"Mail sent.\n");
}


/*** See if string contains any swearing ***/
contains_swearing(str)
char *str;
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


/*** Count the number of colour commands in a string ***/
colour_com_count(str)
char *str;
{
char *s;
int i,cnt;

s=str;  cnt=0;
while(*s) {
     if (*s=='~') {
          ++s;
          for(i=0;i<NUM_COLS;++i) {
               if (!strncmp(s,colcom[i],2)) {
                    cnt++;  s++;  continue;
                    }
               }
          continue;
          }
     ++s;
     }
return cnt;
}


/*** Strip out colour commands from string for when we are sending strings
     over a netlink to a talker that doesn't support them ***/
char *colour_com_strip(str)
char *str;
{
char *s,*t;
static char text2[ARR_SIZE];
int i;

s=str;  t=text2;
while(*s) {
	if (*s=='~') {
		++s;
		for(i=0;i<NUM_COLS;++i) {
			if (!strncmp(s,colcom[i],2)) {  s++;  goto CONT;  }
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


/*** Date string for board messages, mail, .who and .allclones ***/
char *long_date(which)
int which;
{
static char dstr[80];

if (which) sprintf(dstr,"on %s %d %s %d at %02d:%02d",day[twday],tmday,month[tmonth],tyear,thour,tmin);
else sprintf(dstr,"[ %s %d %s %d at %02d:%02d ]",day[twday],tmday,month[tmonth],tyear,thour,tmin);
return dstr;
}


/*** Clear the review buffer in the room ***/
clear_revbuff(rm)
RM_OBJECT rm;
{
int c;
for(c=0;c<REVIEW_LINES;++c) rm->revbuff[c][0]='\0';
rm->revline=0;
}


/*** Clear the screen ***/
cls(user)
UR_OBJECT user;
{
int i;

for(i=0;i<5;++i) write_user(user,"\n\n\n\n\n\n\n\n\n\n");		
}


/*** Convert string to upper case ***/
strtoupper(str)
char *str;
{
while(*str) {  *str=toupper(*str);  str++; }
}


/*** Convert string to lower case ***/
strtolower(str)
char *str;
{
while(*str) {  *str=tolower(*str);  str++; }
}


/*** Returns 1 if string is a positive number ***/
isnumber(str)
char *str;
{
while(*str) if (!isdigit(*str++)) return 0;
return 1;
}


/************ OBJECT FUNCTIONS ************/

/*** Construct user/clone object ***/
UR_OBJECT create_user()
{
UR_OBJECT user;
int i;

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
user->name[0]='\0';
user->desc[0]='\0';
user->in_phrase[0]='\0'; 
user->out_phrase[0]='\0';   
user->afk_mesg[0]='\0';
user->pass[0]='\0';
user->site[0]='\0';
user->site_port=0;
user->last_site[0]='\0';
user->page_file[0]='\0';
user->mail_to[0]='\0';
user->inpstr_old[0]='\0';
user->buff[0]='\0';  
user->buffpos=0;
user->filepos=0;
user->read_mail=time(0);
user->room=NULL;
user->invite_room=NULL;
user->port=0;
user->login=0;
user->socket=-1;
user->attempts=0;
user->command_mode=0;
user->level=0;
user->vis=1;
user->ignall=0;
user->ignall_store=0;
user->ignshouts=0;
user->igntells=0;
user->muzzled=0;
user->remote_com=-1;
user->netlink=NULL;
user->pot_netlink=NULL; 
user->last_input=time(0);
user->last_login=time(0);
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
user->malloc_start=NULL;
user->malloc_end=NULL;
user->owner=NULL;
for(i=0;i<REVTELL_LINES;++i) user->revbuff[i][0]='\0';
user->wipe_to=0;
user->wipe_from=0;
user->tname[0]='\0';
user->tsite[0]='\0';
user->tport[0]='\0';
user->wrap=0;
user->unarrest=NEW;
user->pager=23;
user->logons=0;
user->expire=1;
user->lroom=0;
user->logout_room[0]='\0';
user->t_expire=time(0)+(NEWBIE_EXPIRES*86400);
user->vote=0;
user->monitor=0;
for (i=0;i<MAX_COPIES;++i) user->copyto[i][0]='\0';
user->invite_by[0]='\0';
user->gender=NEUTER;
user->age=0;
user->hideemail=1;
user->misses=0;
user->hits=0;
user->kills=0;
user->deaths=0;
user->bullets=6;
user->hps=10;
user->wrap_room=NULL;
strcpy(user->email,"#UNSET");
strcpy(user->homepage,"#UNSET");
for (i=0; i<MAX_IGNORES; ++i) user->ignoreuser[i][0]='\0';
user->call[0]='\0';
for (i=0;i<10;++i) user->macros[i][0]='\0';
for (i=0;i<MAX_FRIENDS;++i) user->friend[i][0]='\0';
user->alert=0;
user->lmail_lev=-3; /* has to be -3 so as not to be confused with a level and cant be -1 or -2 */
user->welcomed=0;
strcpy(user->verify_code,"#NONE");
user->mail_verified=0;
user->autofwd=0;
user->editing=0;
user->hwrap_lev=0;
user->hwrap_com=0;
user->ignpics=0;
user->ignlogons=0;
user->igngreets=0;
user->ignwiz=0;
user->ignbeeps=0;
for(i=0;i<REVTELL_LINES;++i) user->afkbuff[i][0]='\0';
user->afkline=0;
for(i=0;i<REVTELL_LINES;++i) user->editbuff[i][0]='\0';
user->editline=0;
user->show_pass=0;
user->samesite_all_store=0;
user->samesite_check_store[0]='\0';
user->hang_stage=-1;
user->hang_word[0]='\0';
user->hang_word_show[0]='\0';
user->hang_guess[0]='\0';
return user;
}



/*** Destruct an object. ***/
destruct_user(user)
UR_OBJECT user;
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
RM_OBJECT create_room()
{
RM_OBJECT room;
int i;

if ((room=(RM_OBJECT)malloc(sizeof(struct room_struct)))==NULL) {
	fprintf(stderr,"NUTS: Memory allocation failure in create_room().\n");
	boot_exit(1);
	}
room->name[0]='\0';
room->label[0]='\0';
room->desc[0]='\0';
room->topic[0]='\0';
room->access=-1;
room->revline=0;
room->mesg_cnt=0;
room->inlink=0;
room->netlink=NULL;
room->netlink_name[0]='\0';
room->next=NULL;
for(i=0;i<MAX_LINKS;++i) {
	room->link_label[i][0]='\0';  room->link[i]=NULL;
	}
for(i=0;i<REVIEW_LINES;++i) room->revbuff[i][0]='\0';
if (room_first==NULL) room_first=room;
else room_last->next=room;
room_last=room;
return room;
}


/*** Construct link object ***/
NL_OBJECT create_netlink()
{
NL_OBJECT nl;

if ((nl=(NL_OBJECT)malloc(sizeof(struct netlink_struct)))==NULL) {
	sprintf(text,"NETLINK: Memory allocation failure in create_netlink().\n");
	write_syslog(text,1,NETLOG);
	return NULL;
	}
if (nl_first==NULL) { 
	nl_first=nl;  nl->prev=NULL;  nl->next=NULL;
	}
else {  
	nl_last->next=nl;  nl->next=NULL;  nl->prev=nl_last;
	}
nl_last=nl;

nl->service[0]='\0';
nl->site[0]='\0';
nl->verification[0]='\0';
nl->mail_to[0]='\0';
nl->mail_from[0]='\0';
nl->mailfile=NULL;
nl->buffer[0]='\0';
nl->ver_major=0;
nl->ver_minor=0;
nl->ver_patch=0;
nl->keepalive_cnt=0;
nl->last_recvd=0;
nl->port=0;
nl->socket=0;
nl->mesg_user=NULL;
nl->connect_room=NULL;
nl->type=UNCONNECTED;
nl->stage=DOWN;
nl->connected=0;
nl->lastcom=-1;
nl->allow=ALL;
nl->warned=0;
return nl;
}


/*** Destruct a netlink (usually a closed incoming one). ***/
destruct_netlink(nl)
NL_OBJECT nl;
{
if (nl!=nl_first) {
	nl->prev->next=nl->next;
	if (nl!=nl_last) nl->next->prev=nl->prev;
	else { nl_last=nl->prev; nl_last->next=NULL; }
	}
else {
	nl_first=nl->next;
	if (nl!=nl_last) nl_first->prev=NULL;
	else nl_last=NULL; 
	}
free(nl);
}


/*** Destroy all clones belonging to given user ***/
destroy_user_clones(user)
UR_OBJECT user;
{
UR_OBJECT u;

for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->owner==user) {
		sprintf(text,"The clone of %s enters a wormhole and vanishes.\n",u->name);
		write_room(u->room,text);
		destruct_user(u);
		}
	}
}


/************ NUTS PROTOCOL AND LINK MANAGEMENT FUNCTIONS ************/
/* Please don't alter these functions. If you do you may introduce 
   incompatabilities which may prevent other systems connecting or cause
   bugs on the remote site and yours. You may think it looks simple but
   even the newline count is important in some places. */

/*** Accept incoming server connection ***/
accept_server_connection(sock,acc_addr)
int sock;
struct sockaddr_in acc_addr;
{
NL_OBJECT nl,nl2,create_netlink();
RM_OBJECT rm;
char site[81];

/* Send server type id and version number */
sprintf(text,"NUTS %s\n",NUTSVER);
write_sock(sock,text);
strcpy(site,get_ip_address(acc_addr));
sprintf(text,"NETLINK: Received request connection from site %s.\n",site);
write_syslog(text,1,NETLOG);

/* See if legal site, ie site is in config sites list. */
for(nl2=nl_first;nl2!=NULL;nl2=nl2->next) 
	if (!strcmp(nl2->site,site)) goto OK;
write_sock(sock,"DENIED CONNECT 1\n");
close(sock);
write_syslog("NETLINK: Request denied, remote site not in valid sites list.\n",1,NETLOG);
return;

/* Find free room link */
OK:
for(rm=room_first;rm!=NULL;rm=rm->next) {
	if (rm->netlink==NULL && rm->inlink) {
		if ((nl=create_netlink())==NULL) {
			write_sock(sock,"DENIED CONNECT 2\n");  
			close(sock);  
			write_syslog("NETLINK: Request denied, unable to create netlink object.\n",1,NETLOG);
			return;
			}
		rm->netlink=nl;
		nl->socket=sock;
		nl->type=INCOMING;
		nl->stage=VERIFYING;
		nl->connect_room=rm;
		nl->allow=nl2->allow;
		nl->last_recvd=time(0);
		strcpy(nl->service,"<verifying>");
		strcpy(nl->site,site);
		write_sock(sock,"GRANTED CONNECT\n");
		write_syslog("NETLINK: Request granted.\n",1,NETLOG);
		return;
		}
	}
write_sock(sock,"DENIED CONNECT 3\n");
close(sock);
write_syslog("NETLINK: Request denied, no free room links.\n",1,NETLOG);
}
		

/*** Deal with netlink data on link nl ***/
exec_netcom(nl,inpstr)
NL_OBJECT nl;
char *inpstr;
{
int netcom_num,lev;
char w1[ARR_SIZE],w2[ARR_SIZE],w3[ARR_SIZE],*c,ctemp;

/* The most used commands have been truncated to save bandwidth, ie ACT is
   short for action, EMSG is short for end message. Commands that don't get
   used much ie VERIFICATION have been left long for readability. */
char *netcom[]={
"DISCONNECT","TRANS","REL","ACT","GRANTED",
"DENIED","MSG","EMSG","PRM","VERIFICATION",
"VERIFY","REMVD","ERROR","EXISTS?","EXISTS_NO",
"EXISTS_YES","MAIL","ENDMAIL","MAILERROR","KA",
"RSTAT","*"
};

/* The buffer is large (ARR_SIZE*2) but if a bug occurs with a remote system
   and no newlines are sent for some reason it may overflow and this will 
   probably cause a crash. Oh well, such is life. */
if (nl->buffer[0]) {
	strcat(nl->buffer,inpstr);  inpstr=nl->buffer;
	}
nl->last_recvd=time(0);

/* Go through data */
while(*inpstr) {
	w1[0]='\0';  w2[0]='\0';  w3[0]='\0';  lev=0;
	if (*inpstr!='\n') sscanf(inpstr,"%s %s %s %d",w1,w2,w3,&lev);
	/* Find first newline */
	c=inpstr;  ctemp=1; /* hopefully we'll never get char 1 in the string */
	while(*c) {
		if (*c=='\n') {  ctemp=*(c+1); *(c+1)='\0';  break; }
		++c;
		}
	/* If no newline then input is incomplete, store and return */
	if (ctemp==1) {  
		if (inpstr!=nl->buffer) strcpy(nl->buffer,inpstr);  
		return;  
		}
	/* Get command number */
	netcom_num=0;
	while(netcom[netcom_num][0]!='*') {
		if (!strcmp(netcom[netcom_num],w1))  break;
		netcom_num++;
		}
	/* Deal with initial connects */
	if (nl->stage==VERIFYING) {
		if (nl->type==OUTGOING) {
			if (strcmp(w1,"NUTS")) {
				sprintf(text,"NETLINK: Incorrect connect message from %s.\n",nl->service);
				write_syslog(text,1,NETLOG);
				shutdown_netlink(nl);
				return;
				}	
			/* Store remote version for compat checks */
			nl->stage=UP;
			w2[10]='\0'; 
			sscanf(w2,"%d.%d.%d",&nl->ver_major,&nl->ver_minor,&nl->ver_patch);
			goto NEXT_LINE;
			}
		else {
			/* Incoming */
			if (netcom_num!=9) {
				/* No verification, no connection */
				sprintf(text,"NETLINK: No verification sent by site %s.\n",nl->site);
				write_syslog(text,1,NETLOG);
				shutdown_netlink(nl);  
				return;
				}
			nl->stage=UP;
			}
		}
	/* If remote is currently sending a message relay it to user, don't
	   interpret it unless its EMSG or ERROR */
	if (nl->mesg_user!=NULL && netcom_num!=7 && netcom_num!=12) {
		/* If -1 then user logged off before end of mesg received */
		if (nl->mesg_user!=(UR_OBJECT)-1) write_user(nl->mesg_user,inpstr);   
		goto NEXT_LINE;
		}
	/* Same goes for mail except its ENDMAIL or ERROR */
	if (nl->mailfile!=NULL && netcom_num!=17) {
		fputs(inpstr,nl->mailfile);  goto NEXT_LINE;
		}
	nl->lastcom=netcom_num;
	switch(netcom_num) {
		case  0: 
		if (nl->stage==UP) {
			sprintf(text,"~OLSYSTEM:~FY~RS Disconnecting from service %s in the %s.\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			}
		shutdown_netlink(nl);  
		break;

		case  1: nl_transfer(nl,w2,w3,lev,inpstr);  break;
		case  2: nl_release(nl,w2);  break;
		case  3: nl_action(nl,w2,inpstr);  break;
		case  4: nl_granted(nl,w2);  break;
		case  5: nl_denied(nl,w2,inpstr);  break;
		case  6: nl_mesg(nl,w2); break;
		case  7: nl->mesg_user=NULL;  break;
		case  8: nl_prompt(nl,w2);  break;
		case  9: nl_verification(nl,w2,w3,0);  break;
		case 10: nl_verification(nl,w2,w3,1);  break;
		case 11: nl_removed(nl,w2);  break;
		case 12: nl_error(nl);  break;
		case 13: nl_checkexist(nl,w2,w3);  break;
		case 14: nl_user_notexist(nl,w2,w3);  break;
		case 15: nl_user_exist(nl,w2,w3);  break;
		case 16: nl_mail(nl,w2,w3);  break;
		case 17: nl_endmail(nl);  break;
		case 18: nl_mailerror(nl,w2,w3);  break;
		case 19: /* Keepalive signal, do nothing */ break;
		case 20: nl_rstat(nl,w2);  break;
		default: 
			sprintf(text,"NETLINK: Received unknown command '%s' from %s.\n",w1,nl->service);
			write_syslog(text,1,NETLOG);
			write_sock(nl->socket,"ERROR\n"); 
		}
	NEXT_LINE:
	/* See if link has closed */
	if (nl->type==UNCONNECTED) return;
	*(c+1)=ctemp;
	inpstr=c+1;
	}
nl->buffer[0]='\0';
}


/*** Deal with user being transfered over from remote site ***/
nl_transfer(nl,name,pass,lev,inpstr)
NL_OBJECT nl;
char *name,*pass,*inpstr;
int lev;
{
UR_OBJECT u,create_user();

/* link for outgoing users only */
if (nl->allow==OUT) {
	sprintf(text,"DENIED %s 4\n",name);
	write_sock(nl->socket,text);
	return;
	}
if (strlen(name)>USER_NAME_LEN) name[USER_NAME_LEN]='\0';

/* See if user is banned */
if (user_banned(name)) {
	if (nl->ver_major==3 && nl->ver_minor>=3 && nl->ver_patch>=3) 
		sprintf(text,"DENIED %s 9\n",name); /* new error for 3.3.3 */
	else sprintf(text,"DENIED %s 6\n",name); /* old error to old versions */
	write_sock(nl->socket,text);
	return;
	}

/* See if user already on here */
if (u=get_user(name)) {
	sprintf(text,"DENIED %s 5\n",name);
	write_sock(nl->socket,text);
	return;
	}

/* See if user of this name exists on this system by trying to load up
   datafile */
if ((u=create_user())==NULL) {		
	sprintf(text,"DENIED %s 6\n",name);
	write_sock(nl->socket,text);
	return;
	}
u->type=REMOTE_TYPE;
strcpy(u->name,name);
if (load_user_details(u)) {
	if (strcmp(u->pass,pass)) {
		/* Incorrect password sent */
		sprintf(text,"DENIED %s 7\n",name);
		write_sock(nl->socket,text);
		destruct_user(u);
		destructed=0;
		return;
		}
	}
else {
	/* Get the users description */
	if (nl->ver_major<=3 && nl->ver_minor<=3 && nl->ver_patch<1) 
		strcpy(text,remove_first(remove_first(remove_first(inpstr))));
	else strcpy(text,remove_first(remove_first(remove_first(remove_first(inpstr)))));
	text[USER_DESC_LEN]='\0';
	terminate(text);
	strcpy(u->desc,text);
	strcpy(u->in_phrase,"enters");
	strcpy(u->out_phrase,"goes");
	if (nl->ver_major==3 && nl->ver_minor>=3 && nl->ver_patch>=1) {
		if (lev>rem_user_maxlevel) u->level=rem_user_maxlevel;
		else u->level=lev; 
		}
	else u->level=rem_user_deflevel;
	}
/* See if users level is below minlogin level */
if (u->level<minlogin_level) {
	if (nl->ver_major==3 && nl->ver_minor>=3 && nl->ver_patch>=3) 
		sprintf(text,"DENIED %s 8\n",u->name); /* new error for 3.3.3 */
	else sprintf(text,"DENIED %s 6\n",u->name); /* old error to old versions */
	write_sock(nl->socket,text);
	destruct_user(u);
	destructed=0;
	return;
	}
strcpy(u->site,nl->service);
sprintf(text,"%s enters from cyberspace.\n",u->name);
write_room(nl->connect_room,text);
sprintf(text,"NETLINK: Remote user %s received from %s.\n",u->name,nl->service);
write_syslog(text,1,NETLOG);
u->room=nl->connect_room;
u->netlink=nl;
u->read_mail=time(0);
u->last_login=time(0);
num_of_users++;
sprintf(text,"GRANTED %s\n",name);
write_sock(nl->socket,text);
}
		

/*** User is leaving this system ***/
nl_release(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if ((u=get_user(name))!=NULL && u->type==REMOTE_TYPE) {
	sprintf(text,"%s leaves this plain of existence.\n",u->name);
	write_room_except(u->room,text,u);
	sprintf(text,"NETLINK: Remote user %s released.\n",u->name);
	write_syslog(text,1,NETLOG);
	destroy_user_clones(u);
	destruct_user(u);
	num_of_users--;
	return;
	}
sprintf(text,"NETLINK: Release requested for unknown/invalid user %s from %s.\n",name,nl->service);
write_syslog(text,1,NETLOG);
}


/*** Remote user performs an action on this system ***/
nl_action(nl,name,inpstr)
NL_OBJECT nl;
char *name,*inpstr;
{
UR_OBJECT u;
char *c,ctemp;

if (!(u=get_user(name))) {
	sprintf(text,"DENIED %s 8\n",name);
	write_sock(nl->socket,text);
	return;
	}
if (u->socket!=-1) {
	sprintf(text,"NETLINK: Action requested for local user %s from %s.\n",name,nl->service);
	write_syslog(text,1,NETLOG);
	return;
	}
inpstr=remove_first(remove_first(inpstr));
/* remove newline character */
c=inpstr; ctemp='\0';
while(*c) {
	if (*c=='\n') {  ctemp=*c;  *c='\0';  break;  }
	++c;
	}
u->last_input=time(0);
if (u->misc_op) {
	if (!strcmp(inpstr,"NL")) misc_ops(u,"\n");  
	else misc_ops(u,inpstr+4);
	return;
	}
if (u->afk) {
	write_user(u,"You are no longer AFK.\n");  
	if (u->vis) {
		sprintf(text,"%s comes back from being AFK.\n",u->name);
		write_room_except(u->room,text,u);
		}
	u->afk=0;
	}
word_count=wordfind(inpstr);
if (!strcmp(inpstr,"NL")) return; 
exec_com(u,inpstr);
if (ctemp) *c=ctemp;
if (!u->misc_op) prompt(u);
}


/*** Grant received from remote system ***/
nl_granted(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;
RM_OBJECT old_room;

if (!strcmp(name,"CONNECT")) {
	sprintf(text,"NETLINK: Connection to %s granted.\n",nl->service);
	write_syslog(text,1,NETLOG);
	/* Send our verification and version number */
	sprintf(text,"VERIFICATION %s %s\n",verification,NUTSVER);
	write_sock(nl->socket,text);
	return;
	}
if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Grant received for unknown user %s from %s.\n",name,nl->service);
	write_syslog(text,1,NETLOG);
	return;
	}
/* This will probably occur if a user tried to go to the other site , got 
   lagged then changed his mind and went elsewhere. Don't worry about it. */
if (u->remote_com!=GO) {
	sprintf(text,"NETLINK: Unexpected grant for %s received from %s.\n",name,nl->service);
	write_syslog(text,1,NETLOG);
	return;
	}
/* User has been granted permission to move into remote talker */
write_user(u,"~FB~OLYou traverse cyberspace...\n");
if (u->vis) {
	sprintf(text,"%s %s to the %s.\n",u->name,u->out_phrase,nl->service);
	write_room_except(u->room,text,u);
	}
else write_room_except(u->room,invisleave,u);
sprintf(text,"NETLINK: %s transfered to %s.\n",u->name,nl->service);
write_syslog(text,1,NETLOG);
old_room=u->room;
u->room=NULL; /* Means on remote talker */
u->netlink=nl;
u->pot_netlink=NULL;
u->remote_com=-1;
u->misc_op=0;  
u->filepos=0;  
u->page_file[0]='\0';
reset_access(old_room);
sprintf(text,"ACT %s look\n",u->name);
write_sock(nl->socket,text);
}


/*** Deny received from remote system ***/
nl_denied(nl,name,inpstr)
NL_OBJECT nl;
char *name,*inpstr;
{
UR_OBJECT u,create_user();
int errnum;
char *neterr[]={
"this site is not in the remote services valid sites list",
"the remote service is unable to create a link",
"the remote service has no free room links",
"the link is for incoming users only",
"a user with your name is already logged on the remote site",
"the remote service was unable to create a session for you",
"incorrect password. Use '.go <service> <remote password>'",
"your level there is below the remote services current minlogin level",
"you are banned from that service"
};

errnum=0;
sscanf(remove_first(remove_first(inpstr)),"%d",&errnum);
if (!strcmp(name,"CONNECT")) {
	sprintf(text,"NETLINK: Connection to %s denied, %s.\n",nl->service,neterr[errnum-1]);
	write_syslog(text,1,NETLOG);
	/* If wiz initiated connect let them know its failed */
	sprintf(text,"~OLSYSTEM:~RS Connection to %s failed, %s.\n",nl->service,neterr[errnum-1]);
	write_level(com_level[CONN],1,text,NULL);
	close(nl->socket);
	nl->type=UNCONNECTED;
	nl->stage=DOWN;
	return;
	}
/* Is for a user */
if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Deny for unknown user %s received from %s.\n",name,nl->service);
	write_syslog(text,1,NETLOG);
	return;
	}
sprintf(text,"NETLINK: Deny %d for user %s received from %s.\n",errnum,name,nl->service);
write_syslog(text,1,NETLOG);
sprintf(text,"Sorry, %s.\n",neterr[errnum-1]);
write_user(u,text);
prompt(u);
u->remote_com=-1;
u->pot_netlink=NULL;
}


/*** Text received to display to a user on here ***/
nl_mesg(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Message received for unknown user %s from %s.\n",name,nl->service);
	write_syslog(text,1,NETLOG);
	nl->mesg_user=(UR_OBJECT)-1;
	return;
	}
nl->mesg_user=u;
}


/*** Remote system asking for prompt to be displayed ***/
nl_prompt(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Prompt received for unknown user %s from %s.\n",name,nl->service);
	write_syslog(text,1,NETLOG);
	return;
	}
if (u->type==REMOTE_TYPE) {
	sprintf(text,"NETLINK: Prompt received for remote user %s from %s.\n",name,nl->service);
	write_syslog(text,1,NETLOG);
	return;
	}
prompt(u);
}


/*** Verification received from remote site ***/
nl_verification(nl,w2,w3,com)
NL_OBJECT nl;
char *w2,*w3;
int com;
{
NL_OBJECT nl2;

if (!com) {
	/* We're verifiying a remote site */
	if (!w2[0]) {
		shutdown_netlink(nl);  return;
		}
	for(nl2=nl_first;nl2!=NULL;nl2=nl2->next) {
		if (!strcmp(nl->site,nl2->site) && !strcmp(w2,nl2->verification)) {
			switch(nl->allow) {
				case IN : write_sock(nl->socket,"VERIFY OK IN\n");  break;
				case OUT: write_sock(nl->socket,"VERIFY OK OUT\n");  break;
				case ALL: write_sock(nl->socket,"VERIFY OK ALL\n"); 
				}
			strcpy(nl->service,nl2->service);

			/* Only 3.2.0 and above send version number with verification */
			sscanf(w3,"%d.%d.%d",&nl->ver_major,&nl->ver_minor,&nl->ver_patch);
			sprintf(text,"NETLINK: Connected to %s in the %s.\n",nl->service,nl->connect_room->name);
			write_syslog(text,1,NETLOG);
			sprintf(text,"~OLSYSTEM:~RS New connection to service %s in the %s.\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			return;
			}
		}
	write_sock(nl->socket,"VERIFY BAD\n");
	shutdown_netlink(nl);
	return;
	}
/* The remote site has verified us */
if (!strcmp(w2,"OK")) {
	/* Set link permissions */
	if (!strcmp(w3,"OUT")) {
		if (nl->allow==OUT) {
			sprintf(text,"NETLINK: WARNING - Permissions deadlock, both sides are outgoing only.\n");
			write_syslog(text,1,NETLOG);
			}
		else nl->allow=IN;
		}
	else {
		if (!strcmp(w3,"IN")) {
			if (nl->allow==IN) {
				sprintf(text,"NETLINK: WARNING - Permissions deadlock, both sides are incoming only.\n");
				write_syslog(text,1,NETLOG);
				}
			else nl->allow=OUT;
			}
		}
	sprintf(text,"NETLINK: Connection to %s verified.\n",nl->service);
	write_syslog(text,1,NETLOG);
	sprintf(text,"~OLSYSTEM:~RS New connection to service %s in the %s.\n",nl->service,nl->connect_room);
	write_room(NULL,text);
	return;
	}
if (!strcmp(w2,"BAD")) {
	sprintf(text,"NETLINK: Connection to %s has bad verification.\n",nl->service);
	write_syslog(text,1,NETLOG);
	/* Let wizes know its failed , may be wiz initiated */
	sprintf(text,"~OLSYSTEM:~RS Connection to %s failed, bad verification.\n",nl->service);
	write_level(com_level[CONN],1,text,NULL);
	shutdown_netlink(nl);  
	return;
	}
sprintf(text,"NETLINK: Unknown verify return code from %s.\n",nl->service);
write_syslog(text,1,NETLOG);
shutdown_netlink(nl);
}


/* Remote site only sends REMVD (removed) notification if user on remote site 
   tries to .go back to his home site or user is booted off. Home site doesn't
   bother sending reply since remote site will remove user no matter what. */
nl_removed(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Removed notification for unknown user %s received from %s.\n",name,nl->service);
	write_syslog(text,1,NETLOG);
	return;
	}
if (u->room!=NULL) {
	sprintf(text,"NETLINK: Removed notification of local user %s received from %s.\n",name,nl->service);
	write_syslog(text,1,NETLOG);
	return;
	}
sprintf(text,"NETLINK: %s returned from %s.\n",u->name,u->netlink->service);
write_syslog(text,1,NETLOG);
u->room=u->netlink->connect_room;
u->netlink=NULL;
if (u->vis) {
	sprintf(text,"%s %s\n",u->name,u->in_phrase);
	write_room_except(u->room,text,u);
	}
else write_room_except(u->room,invisenter,u);
look(u);
prompt(u);
}


/*** Got an error back from site, deal with it ***/
nl_error(nl)
NL_OBJECT nl;
{
if (nl->mesg_user!=NULL) nl->mesg_user=NULL;
/* lastcom value may be misleading, the talker may have sent off a whole load
   of commands before it gets a response due to lag, any one of them could
   have caused the error */
sprintf(text,"NETLINK: Received ERROR from %s, lastcom = %d.\n",nl->service,nl->lastcom);
write_syslog(text,1,NETLOG);
}


/*** Does user exist? This is a question sent by a remote mailer to
     verifiy mail id's. ***/
nl_checkexist(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
FILE *fp;
char filename[80];

sprintf(filename,"%s/%s.D",USERFILES,to);
if (!(fp=fopen(filename,"r"))) {
	sprintf(text,"EXISTS_NO %s %s\n",to,from);
	write_sock(nl->socket,text);
	return;
	}
fclose(fp);
sprintf(text,"EXISTS_YES %s %s\n",to,from);
write_sock(nl->socket,text);
}


/*** Remote user doesnt exist ***/
nl_user_notexist(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
UR_OBJECT user;
char filename[80];
char text2[ARR_SIZE];

if ((user=get_user(from))!=NULL) {
	sprintf(text,"~OLSYSTEM:~RS User %s does not exist at %s, your mail bounced.\n",to,nl->service);
	write_user(user,text);
	}
else {
	sprintf(text2,"There is no user named %s at %s, your mail bounced.\n",to,nl->service);
	send_mail(NULL,from,text2,0);
	}
sprintf(filename,"%s/OUT_%s_%s@%s",MAILSPOOL,from,to,nl->service);
unlink(filename);
}


/*** Remote users exists, send him some mail ***/
nl_user_exist(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
UR_OBJECT user;
FILE *fp;
char text2[ARR_SIZE],filename[80],line[82];

sprintf(filename,"%s/OUT_%s_%s@%s",MAILSPOOL,from,to,nl->service);
if (!(fp=fopen(filename,"r"))) {
	if ((user=get_user(from))!=NULL) {
		sprintf(text,"~OLSYSTEM:~RS An error occured during mail delivery to %s@%s.\n",to,nl->service);
		write_user(user,text);
		}
	else {
		sprintf(text2,"An error occured during mail delivery to %s@%s.\n",to,nl->service);
		send_mail(NULL,from,text2,0);
		}
	return;
	}
sprintf(text,"MAIL %s %s\n",to,from);
write_sock(nl->socket,text);
fgets(line,80,fp);
while(!feof(fp)) {
	write_sock(nl->socket,line);
	fgets(line,80,fp);
	}
fclose(fp);
write_sock(nl->socket,"\nENDMAIL\n");
unlink(filename);
}


/*** Got some mail coming in ***/
nl_mail(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
char filename[80];

sprintf(text,"NETLINK: Mail received for %s from %s.\n",to,nl->service);
write_syslog(text,1,NETLOG);
sprintf(filename,"%s/IN_%s_%s@%s",MAILSPOOL,to,from,nl->service);
if (!(nl->mailfile=fopen(filename,"w"))) {
	sprintf(text,"ERROR: Couldn't open file %s to write in nl_mail().\n",filename);
	write_syslog(text,0,SYSLOG);
	sprintf(text,"MAILERROR %s %s\n",to,from);
	write_sock(nl->socket,text);
	return;
	}
strcpy(nl->mail_to,to);
strcpy(nl->mail_from,from);
}


/*** End of mail message being sent from remote site ***/
nl_endmail(nl)
NL_OBJECT nl;
{
FILE *infp,*outfp;
char c,infile[80],mailfile[80],line[DNL+1];

fclose(nl->mailfile);
nl->mailfile=NULL;

sprintf(mailfile,"%s/IN_%s_%s@%s",MAILSPOOL,nl->mail_to,nl->mail_from,nl->service);

/* Copy to users mail file to a tempfile */
if (!(outfp=fopen("tempfile","w"))) {
	write_syslog("ERROR: Couldn't open tempfile in netlink_endmail().\n",0,SYSLOG);
	sprintf(text,"MAILERROR %s %s\n",nl->mail_to,nl->mail_from);
	write_sock(nl->socket,text);
	goto END;
	}
fprintf(outfp,"%d\r",(int)time(0));

/* Copy old mail file to tempfile */
sprintf(infile,"%s/%s.M",USERFILES,nl->mail_to);
if (!(infp=fopen(infile,"r"))) goto SKIP;
fgets(line,DNL,infp);
c=getc(infp);
while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }
fclose(infp);

/* Copy received file */
SKIP:
if (!(infp=fopen(mailfile,"r"))) {
	sprintf(text,"ERROR: Couldn't open file %s to read in netlink_endmail().\n",mailfile);
	write_syslog(text,0,SYSLOG);
	sprintf(text,"MAILERROR %s %s\n",nl->mail_to,nl->mail_from);
	write_sock(nl->socket,text);
	goto END;
	}
fprintf(outfp,"~OLFrom: %s@%s  %s",nl->mail_from,nl->service,long_date(0));
c=getc(infp);
while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }
fclose(infp);
fclose(outfp);
rename("tempfile",infile);
write_user(get_user(nl->mail_to),"\07~FT~OL~LI** YOU HAVE NEW MAIL **\n");

END:
nl->mail_to[0]='\0';
nl->mail_from[0]='\0';
unlink(mailfile);
}


/*** An error occured at remote site ***/
nl_mailerror(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
UR_OBJECT user;

if ((user=get_user(from))!=NULL) {
	sprintf(text,"~OLSYSTEM:~RS An error occured during mail delivery to %s@%s.\n",to,nl->service);
	write_user(user,text);
	}
else {
	sprintf(text,"An error occured during mail delivery to %s@%s.\n",to,nl->service);
	send_mail(NULL,from,text,0);
	}
}


/*** Send statistics of this server to requesting user on remote site ***/
nl_rstat(nl,to)
NL_OBJECT nl;
char *to;
{
char str[80];

gethostname(str,80);
if (nl->ver_major<=3 && nl->ver_minor<2)
	sprintf(text,"MSG %s\n\n*** Remote statistics ***\n\n",to);
else sprintf(text,"MSG %s\n\n~BB*** Remote statistics ***\n\n",to);
write_sock(nl->socket,text);
sprintf(text,"NUTS version         : %s\nHost                 : %s\n",NUTSVER,str);
write_sock(nl->socket,text);
sprintf(text,"Ports (Main/Wiz/Link): %d ,%d, %d\n",port[0],port[1],port[2]);
write_sock(nl->socket,text);
sprintf(text,"Number of users      : %d\nRemote user maxlevel : %s\n",num_of_users,level_name[rem_user_maxlevel]);
write_sock(nl->socket,text);
sprintf(text,"Remote user deflevel : %s\n\nEMSG\nPRM %s\n",level_name[rem_user_deflevel],to);
write_sock(nl->socket,text);
}


/*** Shutdown the netlink and pull any remote users back home ***/
shutdown_netlink(nl)
NL_OBJECT nl;
{
UR_OBJECT u;
char mailfile[80];

if (nl->type==UNCONNECTED) return;

/* See if any mail halfway through being sent */
if (nl->mail_to[0]) {
	sprintf(text,"MAILERROR %s %s\n",nl->mail_to,nl->mail_from);
	write_sock(nl->socket,text);
	fclose(nl->mailfile);
	sprintf(mailfile,"%s/IN_%s_%s@%s",MAILSPOOL,nl->mail_to,nl->mail_from,nl->service);
	unlink(mailfile);
	nl->mail_to[0]='\0';
	nl->mail_from[0]='\0';
	}
write_sock(nl->socket,"DISCONNECT\n");
close(nl->socket);  
for(u=user_first;u!=NULL;u=u->next) {
	if (u->pot_netlink==nl) {  u->remote_com=-1;  continue;  }
	if (u->netlink==nl) {
		if (u->room==NULL) {
			write_user(u,"~FB~OLYou feel yourself dragged back across the ether...\n");
			u->room=u->netlink->connect_room;
			u->netlink=NULL;
			if (u->vis) {
				sprintf(text,"%s %s\n",u->name,u->in_phrase);
				write_room_except(u->room,text,u);
				}
			else write_room_except(u->room,invisenter,u);
			look(u);  prompt(u);
			sprintf(text,"NETLINK: %s recovered from %s.\n",u->name,nl->service);
			write_syslog(text,1,NETLOG);
			continue;
			}
		if (u->type==REMOTE_TYPE) {
			sprintf(text,"%s vanishes!\n",u->name);
			write_room(u->room,text);
			destruct_user(u);
			num_of_users--;
			}
		}
	}
if (nl->stage==UP) 
	sprintf(text,"NETLINK: Disconnected from %s.\n",nl->service);
else sprintf(text,"NETLINK: Disconnected from site %s.\n",nl->site);
write_syslog(text,1,NETLOG);
if (nl->type==INCOMING) {
	nl->connect_room->netlink=NULL;
	destruct_netlink(nl);  
	return;
	}
nl->type=UNCONNECTED;
nl->stage=DOWN;
nl->warned=0;
}



/*************** START OF COMMAND FUNCTIONS AND THEIR SUBSIDS **************/

/*** Deal with user input ***/
exec_com(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int i,len;
char filename[80],*comword=NULL;

com_num=-1;
if (word[0][0]=='.') comword=(word[0]+1);
else comword=word[0];
if (!comword[0]) {
	write_user(user,"Unknown command.\n");  return;
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
if (!strcmp(word[0],"-")) strcpy(word[0],"echo");
if (!strcmp(word[0],",")) {
    if (!user->call[0]) {
        write_user(user,"Quick call not set.\n");
        return;
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
else if (inpstr[0]=='-' && !isspace(inpstr[1]))    { strcpy(word[0],"echo"); inpstr++; }
else if (inpstr[0]==',' && !isspace(inpstr[1])) {
    if (!user->call[0]) {
        write_user(user,"Quick call not set.\n");
        return;
        }
    strcpy(word[0],"tell");
    }
else inpstr=remove_first(inpstr);
QCSKIP:
i=0;
len=strlen(comword);
while(command[i][0]!='*') {
	if (!strncmp(command[i],comword,len)) {  com_num=i;  break;  }
	++i;
	}
if (user->room!=NULL && (com_num==-1 || com_level[com_num] > user->level)) {
	write_user(user,"Unknown command.\n");  return;
	}
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
		write_user(user,"~FY~OL*** Home execution ***\n");  break;

		default:
		sprintf(text,"ACT %s %s %s\n",user->name,word[0],inpstr);
		write_sock(user->netlink->socket,text);
		no_prompt=1;
		return;
		}
	}
/* Dont want certain commands executed by remote users */
if (user->type==REMOTE_TYPE) {
	switch(com_num) {
		case PASSWD :
		case ENTPRO :
		case ACCREQ :
		case CONN   :
		case DISCONN:
			write_user(user,"Sorry, remote users cannot use that command.\n");
			return;
		}
	}

/* Main switch */
switch(com_num) {
	case QUIT: disconnect_user(user);  break;
	case LOOK: look(user);  break;
	case MODE: toggle_mode(user);  break;
	case SAY : 
		if (word_count<2) {
			write_user(user,"Say what?\n");  return;
			}
		say(user,inpstr);
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
	case PEOPLE : who(user,1);  break;
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
	case HOME  : home(user);  break;
	case STATUS: status(user);  break;
	case VER: show_version(user);  break;
	case RMAIL   : rmail(user);  break;
	case SMAIL   : smail(user,inpstr,0);  break;
	case DMAIL   : dmail(user);  break;
	case FROM    : mail_from(user);  break;
	case ENTPRO  : enter_profile(user,0);  break;
	case EXAMINE : examine(user);  break;
	case RMST    : rooms(user,1,0);  break;
	case RMSN    : rooms(user,0,0);  break;
	case NETSTAT : netstat(user);  break;
	case NETDATA : netdata(user);  break;
	case CONN    : connect_netlink(user);  break;
	case DISCONN : disconnect_netlink(user);  break;
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
			case 0: write_user(user,"There is no map.\n");  break;
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
	case RSTAT : remote_stat(user);  break;
	case SWBAN : swban(user);  break;
	case AFK   : afk(user,inpstr);  break;
	case CLS   : cls(user);  break;
	case COLOUR  : toggle_colour(user);  break;
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
	case PLEAD: plead(user,inpstr);  break;
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
	case LAST: show_last(user);  break;
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
	   switch(forwarding) {
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
	default: write_user(user,"Command not executed.\n");
	}	
}



/*** Display details of room ***/
look(user)
UR_OBJECT user;
{
RM_OBJECT rm;
UR_OBJECT u;
char temp[81],null[1],*ptr;
char *afk="~BR(AFK)";
int i,exits,users;

rm=user->room;
if (rm->access & PRIVATE) sprintf(text,"\n~FTRoom: ~FR%s\n\n",rm->name);
else sprintf(text,"\n~FTRoom: ~FG%s\n\n",rm->name);
write_user(user,text);
write_user(user,user->room->desc);
exits=0;  null[0]='\0';
strcpy(text,"\n~FTExits are:");
for(i=0;i<MAX_LINKS;++i) {
	if (rm->link[i]==NULL) break;
	if (rm->link[i]->access & PRIVATE) 
		sprintf(temp,"  ~FR%s",rm->link[i]->name);
	else sprintf(temp,"  ~FG%s",rm->link[i]->name);
	strcat(text,temp);
	++exits;
	}
if (rm->netlink!=NULL && rm->netlink->stage==UP) {
	if (rm->netlink->allow==IN) sprintf(temp,"  ~FR%s*",rm->netlink->service);
	else sprintf(temp,"  ~FG%s*",rm->netlink->service);
	strcat(text,temp);
	}
else if (!exits) strcpy(text,"\n~FTThere are no exits.");
strcat(text,"\n\n");
write_user(user,text);

users=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->room!=rm || u==user || (!u->vis && u->level>user->level)) 
		continue;
	if (!users++) write_user(user,"~FG~OLYou can see:\n");
	if (u->afk) ptr=afk; else ptr=null;
	if (!u->vis) sprintf(text,"     ~FR*~RS%s %s~RS  %s\n",u->name,u->desc,ptr);
	else sprintf(text,"      %s %s~RS  %s\n",u->name,u->desc,ptr);
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
	}
sprintf(temp," and there are ~OL~FM%d~RS messages on the board.\n",rm->mesg_cnt);
strcat(text,temp);
write_user(user,text);
if (rm->topic[0]) {
	sprintf(text,"~FG~OLCurrent topic:~RS %s\n",rm->topic);
	write_user(user,text);
	return;
	}
write_user(user,"~FGNo topic has been set yet.\n");	
}



/*** Switch between command and speech mode ***/
toggle_mode(user)
UR_OBJECT user;
{
if (user->command_mode) {
	write_user(user,"Now in ~FGSPEECH~RS mode.\n");
	user->command_mode=0;  return;
	}
write_user(user,"Now in ~FTCOMMAND~RS mode.\n");
user->command_mode=1;
}


/*** Shutdown the talker ***/
talker_shutdown(user,str,reboot)
UR_OBJECT user;
char *str;
int reboot;
{
UR_OBJECT u;
NL_OBJECT nl;
int i;
char *ptr;
char *args[]={ progname,confile,NULL };

if (user!=NULL) ptr=user->name; else ptr=str;
if (reboot) {
	write_room(NULL,"\07\n~OLSYSTEM:~FY~LI Rebooting now!!\n\n");
	sprintf(text,"*** REBOOT initiated by %s ***\n",ptr);
	}
else {
	write_room(NULL,"\07\n~OLSYSTEM:~FR~LI Shutting down now!!\n\n");
	sprintf(text,"*** SHUTDOWN initiated by %s ***\n",ptr);
	}
write_syslog(text,0,SYSLOG);
for(nl=nl_first;nl!=NULL;nl=nl->next) shutdown_netlink(nl);
for(u=user_first;u!=NULL;u=u->next) disconnect_user(u);
for(i=0;i<3;++i) close(listen_sock[i]); 
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


/*** Say user speech. ***/
say(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char type[10],*name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot speak.\n");  return;
	}
if (user->room==NULL) {
	sprintf(text,"ACT %s say %s\n",user->name,inpstr);
	write_sock(user->netlink->socket,text);
	no_prompt=1;
	return;
	}
if (word_count<2 && user->command_mode) {
	write_user(user,"Say what?\n");  return;
	}
switch(inpstr[strlen(inpstr)-1]) {
     case '?': strcpy(type,"ask");  break;
     case '!': strcpy(type,"exclaim");  break;
     default : strcpy(type,"say");
     }
if (user->type==CLONE_TYPE) {
	sprintf(text,"Clone of %s %ss: %s\n",user->name,type,inpstr);
	write_room(user->room,text);
	record(user->room,text);
	return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
sprintf(text,"~FGYou %s:~RS %s\n",type,inpstr);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
if (!user->vis) write_monitor(user,user->room,0);
sprintf(text,"~FG%s %ss:~RS %s\n",name,type,inpstr);
write_room_except(user->room,text,user);
record(user->room,text);
}


/*** Shout something ***/
shout(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot shout.\n");  return;
	}
if (word_count<2 && inpstr[1]<33) {
	write_user(user,"Shout what?\n");  return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
sprintf(text,"~OLYou shout:~RS %s\n",inpstr);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
if (!user->vis) write_monitor(user,NULL,0);
sprintf(text,"~OL%s shouts:~RS %s\n",name,inpstr);
write_room_except(NULL,text,user);
record_shout(text);
}


/*** Tell another user something ***/
tell(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
char type[5],*name,temp[WORD_LEN];

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot tell anyone anything.\n");
	return;
        }
sscanf(inpstr,"%s",temp);
if (!strcmp(temp,",")) { 
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
temp[0]='\0';
if (word_count<3 && inpstr[0]!='>') {
        revtell(user);  return;
        }
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
if (u->afk) {
	if (u->afk_mesg[0])
		sprintf(text,"%s is ~FRAFK~RS, message is: %s\n",u->name,u->afk_mesg);
	else sprintf(text,"%s is ~FRAFK~RS at the moment.\n",u->name);
	write_user(user,text);
	write_user(user,"Sending message to their afk review buffer.\n");
	if (inpstr[strlen(inpstr)-1]=='?') strcpy(type,"ask");
	else strcpy(type,"tell");
	if (user->vis || u->level>=user->level) name=user->name; else name=invisname;
	sprintf(text,"~FG~OL%s %ss you:~RS %s\n",name,type,inpstr);
	record_afk(u,text);
	return;
	}
if (u->editing) {
	sprintf(text,"%s is in ~FTEDIT~RS mode at the moment (using the line editor).\n",u->name);
	write_user(user,text);
	write_user(user,"Sending message to their edit review buffer.\n");
	if (inpstr[strlen(inpstr)-1]=='?') strcpy(type,"ask");
	else strcpy(type,"tell");
	if (user->vis || u->level>=user->level) name=user->name; else name=invisname;
	sprintf(text,"~FG~OL%s %ss you:~RS %s\n",name,type,inpstr);
	record_edit(u,text);
	return;
	}
if (u->ignall && (user->level<WIZ || u->level>user->level)) {
	if (u->malloc_start!=NULL) 
		sprintf(text,"%s is using the editor at the moment.\n",u->name);
	else sprintf(text,"%s is ignoring everyone at the moment.\n",u->name);
	write_user(user,text);  
	return;
	}
if ((check_igusers(u,user))!=-1 && user->level<GOD) {
        sprintf(text,"%s is ignoring tells from you.\n",u->name);
	write_user(user,text);
	return;
        }
if (u->igntells && (user->level<WIZ || u->level>user->level)) {
	sprintf(text,"%s is ignoring tells at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
if (u->room==NULL) {
	sprintf(text,"%s is offsite and would not be able to reply to you.\n",u->name);
	write_user(user,text);
	return;
	}
if (inpstr[strlen(inpstr)-1]=='?') strcpy(type,"ask");
else strcpy(type,"tell");
sprintf(text,"~OL~FTYou %s %s:~RS %s\n",type,u->name,inpstr);
write_user(user,text);
record_tell(user,text);
if (user->vis || u->level>=user->level) name=user->name; else name=invisname;
sprintf(text,"~FT%s %ss you:~RS %s\n",name,type,inpstr);
write_user(u,text);
record_tell(u,text);
}


/*** Emote something ***/
emote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (word_count<2 && inpstr[1]<33) {
	write_user(user,"Emote what?\n");  return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
if (user->type==CLONE_TYPE) {
  sprintf(text,"The clone of %s %s\n",user->name,inpstr);
  write_room(user->room,text);
  record(user->room,text);
  return;
  }
if (user->vis) name=user->name; else name=invisname;
if (!user->vis) write_monitor(user,user->room,0);
sprintf(text,"%s %s\n",name,inpstr);
write_room(user->room,text);
record(user->room,text);
}


/*** Do a shout emote ***/
semote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (word_count<2 && inpstr[1]<33) {
	write_user(user,"Shout emote what?\n");  return;
	}
if (user->vis) name=user->name; else name=invisname;
if (!user->vis) write_monitor(user,NULL,0);
sprintf(text,"~OL!!~RS %s %s\n",name,inpstr);
write_room(NULL,text);
record_shout(text);
}


/*** Do a private emote ***/
pemote(user,inpstr)
UR_OBJECT user;
char *inpstr;
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
if (u->afk) {
	if (u->afk_mesg[0])
		sprintf(text,"%s is ~FRAFK~RS, message is: %s\n",u->name,u->afk_mesg);
	else sprintf(text,"%s is ~FRAFK~RS at the moment.\n",u->name);
	write_user(user,text);
	write_user(user,"Sending message to their afk review buffer.\n");
	if (user->vis || u->level>=user->level) name=user->name; else name=invisname;
	inpstr=remove_first(inpstr);
	sprintf(text,"~FG~OL(=>)~RS %s %s\n",name,inpstr);
	record_afk(u,text);
	return;
	}
if (u->editing) {
	sprintf(text,"%s is in ~FTEDIT~RS mode at the moment (using the line editor).\n",u->name);
	write_user(user,text);
	write_user(user,"Sending message to their edit review buffer.\n");
	if (user->vis || u->level>=user->level) name=user->name; else name=invisname;
	inpstr=remove_first(inpstr);
	sprintf(text,"~FG~OL(=>)~RS %s %s\n",name,inpstr);
	record_edit(u,text);
	return;
	}
if (u->ignall && (user->level<WIZ || u->level>user->level)) {
	if (u->malloc_start!=NULL) 
		sprintf(text,"%s is using the editor at the moment.\n",u->name);
	else sprintf(text,"%s is ignoring everyone at the moment.\n",u->name);
	write_user(user,text);  return;
	}
if ((check_igusers(u,user))!=-1 && user->level<GOD) {
        sprintf(text,"%s is ignoring tells from you.\n",u->name);
	write_user(user,text);
	return;
        }
if (u->igntells && (user->level<WIZ || u->level>user->level)) {
	sprintf(text,"%s is ignoring private emotes at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
if (u->room==NULL) {
	sprintf(text,"%s is offsite and would not be able to reply to you.\n",u->name);
	write_user(user,text);
	return;
	}
if (user->vis || u->level>=user->level) name=user->name; else name=invisname;
inpstr=remove_first(inpstr);
sprintf(text,"~FG~OL(%s=>)~RS %s %s\n",u->name,name,inpstr);
write_user(user,text);
record_tell(user,text);
sprintf(text,"~FG~OL(=>)~RS %s %s\n",name,inpstr);
write_user(u,text);
record_tell(u,text);
}


/*** Echo something to screen ***/
echo(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot echo.\n");  return;
	}
if (word_count<2 && inpstr[1]<33) {
	write_user(user,"Echo what?\n");  return;
	}
write_monitor(user,user->room,0);
sprintf(text,"- %s\n",inpstr);
write_room(user->room,text);
record(user->room,text);
}



/*** Move to another room ***/
go(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;
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
			sprintf(text,"%s goes to the %s\n",user->name,nl->service);
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
		else sprintf(text,"TRANS %s %s %s\n",user->name,(char *)crypt(word[2],"NU"),user->desc);
		}
	else {
		if (!word[2][0]) 
			sprintf(text,"TRANS %s %s %d %s\n",user->name,user->pass,user->level,user->desc);
		else sprintf(text,"TRANS %s %s %d %s\n",user->name,(char *)crypt(word[2],"NU"),user->level,user->desc);
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
move_user(user,rm,1);
}


/*** Called by go() and move() ***/
move_user(user,rm,teleport)
UR_OBJECT user;
RM_OBJECT rm;
int teleport;
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
	sprintf(text,"~FT~OL%s appears in an explosion of blue magic!\n",user->name);
	write_room(rm,text);
	sprintf(text,"~FT~OL%s chants a spell and vanishes into a magical blue vortex!\n",user->name);
	write_room_except(old_room,text,user);
	goto SKIP;
	}
if (teleport==2) {
	write_user(user,"\n~FT~OLA giant hand grabs you and pulls you into a magical blue vortex!\n");
	sprintf(text,"~FT~OL%s falls out of a magical blue vortex!\n",user->name);
	write_room(rm,text);
	if (old_room==NULL) {
		sprintf(text,"REL %s\n",user->name);
		write_sock(user->netlink->socket,text);
		user->netlink=NULL;
		}
	else {
		sprintf(text,"~FT~OLA giant hand grabs %s who is pulled into a magical blue vortex!\n",user->name);
		write_room_except(old_room,text,user);
		}
	goto SKIP;
	}
sprintf(text,"%s %s.\n",user->name,user->in_phrase);
write_room(rm,text);
sprintf(text,"%s %s to the %s.\n",user->name,user->out_phrase,rm->name);
write_room_except(user->room,text,user);

SKIP:
user->room=rm;
look(user);
reset_access(old_room);
}


/*** Switch ignoring all on and off ***/
toggle_ignall(user)
UR_OBJECT user;
{
if (!user->ignall) {
	write_user(user,"You are now ignoring everyone.\n");
	sprintf(text,"%s is now ignoring everyone.\n",user->name);
	write_room_except(user->room,text,user);
	user->ignall=1;
	return;
	}
write_user(user,"You will now hear everyone again.\n");
sprintf(text,"%s is listening again.\n",user->name);
write_room_except(user->room,text,user);
user->ignall=0;
}


/*** Switch prompt on and off ***/
toggle_prompt(user)
UR_OBJECT user;
{
if (user->prompt) {
	write_user(user,"Prompt ~FROFF.\n");
	user->prompt=0;  return;
	}
write_user(user,"Prompt ~FGON.\n");
user->prompt=1;
}


/*** Set user description ***/
set_desc(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (word_count<2) {
	sprintf(text,"Your current description is: %s\n",user->desc);
	write_user(user,text);
	return;
	}
if (strstr(word[1],"(CLONE)")) {
	write_user(user,"You cannot have that description.\n");  return;
	}
if (strlen(inpstr)>USER_DESC_LEN) {
	write_user(user,"Description too long.\n");  return;
	}
strcpy(user->desc,inpstr);
write_user(user,"Description set.\n");
}


/*** Set in and out phrases ***/
set_iophrase(user,inpstr)
UR_OBJECT user;
char *inpstr;
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


/*** Set rooms to public or private ***/
set_room_access(user)
UR_OBJECT user;
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
if (user->vis) name=user->name; else name=invisname;
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
if (user->vis) name=user->name; else name=invisname;
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


/*** Ask to be let into a private room ***/
letmein(user)
UR_OBJECT user;
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
sprintf(text,"%s knocs asking to be let into the %s.\n",user->name,rm->name);
write_room_except(user->room,text,user);
sprintf(text,"%s knocks asking to be let in.\n",user->name);
write_room(rm,text);
}


/*** Invite a user into a private room ***/
invite(user)
UR_OBJECT user;
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
	sprintf(text,"%s is already here!\n",u->name);
	write_user(user,text);
	return;
	}
if (u->invite_room==rm) {
	sprintf(text,"%s has already been invited into here.\n",u->name);
	write_user(user,text);
	return;
	}
sprintf(text,"You invite %s in.\n",u->name);
write_user(user,text);
if (user->vis || u->level>=user->level) name=user->name; else name=invisname;
sprintf(text,"%s has invited you into the %s.\n",name,rm->name);
write_user(u,text);
u->invite_room=rm;
strcpy(u->invite_by,user->name);
}


/*** Set the room topic ***/
set_topic(user,inpstr)
UR_OBJECT user;
char *inpstr;
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
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s has set the topic to: %s\n",name,inpstr);
write_room_except(rm,text,user);
strcpy(rm->topic,inpstr);
}


/*** Wizard moves a user to another room ***/
move(user)
UR_OBJECT user;
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
	sprintf(text,"%s is already in the %s.\n",u->name,rm->name);
	write_user(user,text);
	return;
	};
if (!has_room_access(user,rm)) {
	sprintf(text,"The %s is currently private, %s cannot be moved there.\n",rm->name,u->name);
	write_user(user,text);  
	return;
	}
write_user(user,"~FT~OLYou chant an ancient spell...\n");
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~FT~OL%s chants an ancient spell...\n",name);
write_room_except(user->room,text,user);
move_user(u,rm,2);
prompt(u);
}


/*** Broadcast an important message ***/
bcast(user,inpstr,beeps)
UR_OBJECT user;
char *inpstr;
int beeps;
{
char *b="\07",null[1],*beep;

if (word_count<2) {
	switch(beeps) {
		case 0: write_user(user,"Usage: bcast <message>\n");  return;
		case 1: write_user(user,"Usage: bbcast <message>\n");  return;
		}
	}
force_listen=1;
null[0]='\0';
if (!beeps) beep=null; else beep=b;
write_monitor(user,NULL,0);
sprintf(text,"%s~FR~OL--==<~RS %s ~RS~FR~OL>==--\n",beep,inpstr);
write_room(NULL,text);
}


/*** Show who is on ***/
who(user,people)
UR_OBJECT user;
int people;
{
UR_OBJECT u;
int cnt,total,invis,mins,remote,idle,logins;
char line[USER_NAME_LEN+USER_DESC_LEN*2];
char rname[ROOM_NAME_LEN+1],portstr[5],idlestr[20],sockstr[3];

total=0;  invis=0;  remote=0;  logins=0;
write_user(user,"\n+----------------------------------------------------------------------------+\n");
if (user->login) sprintf(text,"Current users %s\n",long_date(1));
else sprintf(text,"~FGCurrent users %s\n",long_date(1));
write_user(user,text);
if (people) write_user(user,"~FTName            : Level Line Ignall Visi Idle Mins  Port  Site/Service\n");
else {
  if (user->login) write_user(user,"Name                                             : Room            : Tm/Id\n");
  else write_user(user,"~FTName                                             : Room            : Tm/Id\n");
  }
write_user(user,"+----------------------------------------------------------------------------+\n\n");
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE) continue;
	mins=(int)(time(0) - u->last_login)/60;
	idle=(int)(time(0) - u->last_input)/60;
	if (u->type==REMOTE_TYPE) strcpy(portstr,"   @");
	else {
		if (u->port==port[0]) strcpy(portstr,"MAIN");
		else strcpy(portstr," WIZ");
		}
	if (u->login) {
		if (!people) continue;
		sprintf(text,"~FY[Login stage %d]~RS :  -      %2d   -    -  %4d    -  %s  %s:%d\n",4 - u->login,u->socket,idle,portstr,u->site,u->site_port);
		write_user(user,text);
		logins++;
		continue;
		}
	++total;
	if (u->type==REMOTE_TYPE) ++remote;
	if (!u->vis) { 
		++invis;  
		if (u->level>user->level && !(user->level>=ARCH)) continue;  
		}
	if (people) {
		if (u->afk) strcpy(idlestr," ~FRAFK~RS");
		else if (u->editing) strcpy(idlestr,"~FTEDIT~RS");
		else sprintf(idlestr,"%4d",idle);
		if (u->type==REMOTE_TYPE) strcpy(sockstr," @");
		else sprintf(sockstr,"%2d",u->socket);
		sprintf(text,"%-15s : %-5.5s   %s  %s  %s %s %4d  %s  %s\n",u->name,level_name[u->level],sockstr,noyes1[u->ignall],noyes1[u->vis],idlestr,mins,portstr,u->site);
		write_user(user,text);
		continue;
		}
	sprintf(line,"  %s %s",u->name,u->desc);
	if (!u->vis) line[0]='*';
	if (u->type==REMOTE_TYPE) line[1]='@';
	if (u->room==NULL) sprintf(rname,"@%s",u->netlink->service);
	else strcpy(rname,u->room->name);
	/* Count number of colour coms to be taken account of when formatting */
	cnt=colour_com_count(line);
	if (u->afk) strcpy(idlestr,"~FRAFK~RS");
	else if (u->editing) strcpy(idlestr,"~FTEDIT~RS");
	else sprintf(idlestr,"%d/%d",mins,idle);
	sprintf(text,"%-*.*s~RS   %s : %-15.15s : %s\n",44+cnt*3,44+cnt*3,line,level_alias[u->level],rname,idlestr);
	write_user(user,text);
	}
sprintf(text,"\nThere are %d visible, %d invisible, %d remote users.\nTotal of %d users",num_of_users-invis,invis,remote,total);
if (people) sprintf(text,"%s and %d logins.\n\n",text,logins);
else strcat(text,".\n\n");
write_user(user,text);
}


/*** Do the help ***/
help(user)
UR_OBJECT user;
{
int ret;
char filename[80];
char *c;

if (word_count<2 || !strcmp(word[1],"commands")) {
  help_commands(user,0);
  return;
  }
if (!strcmp(word[1],"files")) {
  sprintf(filename,"%s/helpfiles",HELPFILES);
  if (!(ret=more(user,user->socket,filename))) {
    write_user(user,"There is no list of help files at the moment.\n");
    return;
    }
  if (ret==1) user->misc_op=2;
  return;
  }
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
sprintf(filename,"%s/%s",HELPFILES,word[1]);
if (!(ret=more(user,user->socket,filename)))
	write_user(user,"Sorry, there is no help on that topic.\n");
if (ret==1) user->misc_op=2;
}


/*** Show the command available ***/
help_commands(user,is_wrap)
UR_OBJECT user;
int is_wrap;
{
int com,cnt,lev,total,lines,start;
char temp[20],*c,null[1];

if (!is_wrap) {
    write_user(user,"\nAll commands start with a '.' (when is ~FGspeech~RS mode) and can be abbreviated.\n");
    write_user(user,"For further help type '~FGhelp <command>~RS' or '~FGhelp files~RS'\n");
    write_user(user,"Remember, a '.' by itself will repeat your last command or speech.\n\n");
    sprintf(text,"\n~BB*** Commands available for level: %s ***\n\n",level_name[user->level]);
    write_user(user,text);
    }

total=0;  null[0]='\0';   lines=0;  start=1;
if (!is_wrap) user->hwrap_lev=JAILED;
if (!is_wrap) user->hwrap_com=0;
for(lev=user->hwrap_lev;lev<=user->level;++lev,++user->hwrap_lev) {
    cnt=0;  text[0]='\0';
    if (!is_wrap || !start) {
        sprintf(text,"\r~BB~FG~OL%c)~RS ",level_name[lev][0]);
	write_user(user,text);
	c="~FT";
        }
    else {
        c=null;
	sprintf(text,"   ");
        }
	while(ordcom[user->hwrap_com].name[0]!='*') {
		if (ordcom[user->hwrap_com].level!=lev) {  user->hwrap_com++;  continue;  }
		sprintf(temp,"%s%-11s ",c,ordcom[user->hwrap_com].name);
		strcat(text,temp);
		c=null;
		cnt++;
		if (cnt==5) {  
			strcat(text,"\n");  
			write_user(user,text);  
			text[0]='\0';  cnt=0;  ++lines;
			}
		if (!cnt) strcat(text,"   ");
		user->hwrap_com++;
		if (lines>=user->pager) {
		    user->misc_op=12;
		    write_user(user,"~BB~FG-=[*]=- PRESS <RETURN>, E TO EXIT:~RS ");
		    return;
		    }
		}
	if (cnt>0 && cnt<5) {
		strcat(text,"\n");  write_user(user,text);
		++lines;
		}
	if (lines>=user->pager) {
	       user->misc_op=12;
	       write_user(user,"~BB~FG-=[*]=- PRESS <RETURN>, E TO EXIT:~RS ");
	       return;
	       }
        user->hwrap_com=0;
        start=0;
	}
com=0;
while(ordcom[com].name[0]!='*') { 
  if (ordcom[com].level>user->level) { ++com; continue; }
  ++com; ++total; 
  }
sprintf(text,"\nThere is a total of ~OL%d~RS commands that you can use.\n\n",total);
write_user(user,text);
user->hwrap_lev=0;  user->hwrap_com=0;  user->misc_op=0;
}


/*** Show the credits. Add your own credits here if you wish but PLEASE leave 
     my credits intact. Thanks. */
help_credits(user)
UR_OBJECT user;
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
sprintf(text,"~OLAndy's modified NUTS~RS version %s\n\n",AMNUTSVER); 
write_user(user,text);
write_user(user,"Amnuts stands for Andy's modified NUTS - not brilliant, not clever, but\nit works ;)");
write_user(user,"    Many other people have made their own version, and here is my\nattempt.  I have added all the things that I think the original NUTS code is\n");
write_user(user,"lacking, and things that help out the running of the talker.\n");
write_user(user,"If you have any questions, comment, or suggestions on how to modify my code\nto make it run better, then please let me know.\n");
write_user(user,"\nMy own talker is   : ~FG~OLWay Out West~RS @ ~FG~OLtalker.com 2574~RS\n");
write_user(user,"My email address is: ~FTandyc@dircon.co.uk~RS\n\n");
write_user(user,"Hope you enjoy the talker.. Have fun!! :)\n\nAndrew Collington - November 1997\n");
write_user(user,"~BM             ~BB             ~BT             ~BG             ~BY             ~BR             ~RS\n\n");
}


/*** Read the message board ***/
read_board(user)
UR_OBJECT user;
{
RM_OBJECT rm;
char filename[80],*name;
int ret;

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
sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
if (!(ret=more(user,user->socket,filename))) 
	write_user(user,"There are no messages on the board.\n\n");
else if (ret==1) user->misc_op=2;
if (user->vis) name=user->name; else name=invisname;
if (rm==user->room) {
	sprintf(text,"%s reads the message board.\n",name);
	write_room_except(user->room,text,user);
	}
}


/*** Write on the message board ***/
write_board(user,inpstr,done_editing)
UR_OBJECT user;
char *inpstr;
int done_editing;
{
FILE *fp;
int cnt,inp;
char *ptr,*name,filename[80];

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot write on the board.\n");  
	return;
	}
if (!done_editing) {
	if (word_count<2) {
		if (user->type==REMOTE_TYPE) {
			/* Editor won't work over netlink cos all the prompts will go
			   wrong, I'll address this in a later version. */
			write_user(user,"Sorry, due to software limitations remote users cannot use the line editor.\nUse the '.write <mesg>' method instead.\n");
			return;
			}
		write_user(user,"\n~BB*** Writing board message ***\n\n");
		user->misc_op=3;
		editor(user,NULL);
		return;
		}
	ptr=inpstr;
	inp=1;
	}
else {
	ptr=user->malloc_start;  inp=0;
	}

sprintf(filename,"%s/%s.B",DATAFILES,user->room->name);
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: cannot write to file.\n",syserror);
	write_user(user,text);
	sprintf(text,"ERROR: Couldn't open file %s to append in write_board().\n",filename);
	write_syslog(text,0,SYSLOG);
	return;
	}
if (user->vis) name=user->name; else name=invisname;
/* The posting time (PT) is the time its written in machine readable form, this 
   makes it easy for this program to check the age of each message and delete 
   as appropriate in check_messages() */
if (user->type==REMOTE_TYPE) 
	sprintf(text,"PT: %d\r~OLFrom: %s@%s  %s\n",(int)(time(0)),name,user->netlink->service,long_date(0));
else sprintf(text,"PT: %d\r~OLFrom: %s  %s\n",(int)(time(0)),name,long_date(0));
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
wipe_board(user)
UR_OBJECT user;
{
int num,cnt,valid,i;
char infile[80],line[82],id[82],*name;
FILE *infp,*outfp;
RM_OBJECT rm;

if (word_count<2 && user->level>=WIZ) {
    write_user(user,"Usage: wipe all\n");
    write_user(user,"Usage: wipe <#>\n");
    write_user(user,"Usage: wipe to <#>\n");
    write_user(user,"Usage: wipe from <#> to <#>\n");
    return;
   }
if (word_count<2 && user->level<WIZ) {
  write_user(user,"Usage: wipe <#>\n");
  return;
  }
if (user->level<WIZ && !(check_board_wipe(user))) return;
else if (get_wipe_parameters(user)==-1) return;

rm=user->room;
if (user->vis) name=user->name; else name=invisname;
sprintf(infile,"%s/%s.B",DATAFILES,rm->name);
if (user->wipe_from==-1) {
    unlink(infile);
    write_user(user,"All messages deleted.\n");
	sprintf(text,"%s wipes the message board.\n",name);
	if (user->level<GOD || user->vis) write_room_except(rm,text,user);
	sprintf(text,"%s wiped all messages from the board in the %s.\n",user->name,rm->name);
	write_syslog(text,1,SYSLOG);
	rm->mesg_cnt=0;
	return;
	}
if (user->wipe_from>rm->mesg_cnt) {
	sprintf(text,"There are only %d messages on the board.\n",rm->mesg_cnt);
	write_user(user,text);   return;
	}
if (!(infp=fopen(infile,"r"))) {
	write_user(user,"The message board is empty.\n");
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile in wipe_board().\n",0,SYSLOG);
	fclose(infp);
	return;
	}
cnt=0;
for (i=1;i<user->wipe_from;i++) {
	fgets(line,82,infp);
	while(line[0]!='\n') {
		if (feof(infp)) goto SKIP_WIPE;
		fputs(line,outfp);  fgets(line,82,infp);
		}
	fputs(line,outfp);
	}
for (;i<=user->wipe_to;i++) {
	fgets(line,82,infp);
	if (i==rm->mesg_cnt) { cnt++; goto SKIP_WIPE; }
	while(line[0]!='\n') fgets(line,82,infp);
	cnt++;
	}
fgets(line,82,infp);
while(!feof(infp)) {
	fputs(line,outfp);
    if (line[0]=='\n') i++;
	fgets(line,82,infp);
    }
SKIP_WIPE:
fclose(infp);
fclose(outfp);
unlink(infile);
if (user->wipe_from==0 && i<user->wipe_to) {
    unlink("tempfile");
    sprintf(text,"There were only %d messages on the board, all now deleted.\n",cnt);
    write_user(user,text);
	sprintf(text,"%s wipes the message board.\n",name);
	if (user->level<GOD || user->vis) write_room_except(rm,text,user);
	sprintf(text,"%s wiped all messages from the board in the %s.\n",user->name,rm->name);
	write_syslog(text,1,SYSLOG);
	rm->mesg_cnt=0;
    return;
    }
if ((user->wipe_from==0 && i==user->wipe_to) || cnt==rm->mesg_cnt) {
    unlink("tempfile"); /* cos it'll be empty anyway */
    write_user(user,"All messages deleted.\n");
	sprintf(text,"%s wipes the message board.\n",name);
	if (user->level<GOD || user->vis) write_room_except(rm,text,user);
	rm->mesg_cnt=0;
	sprintf(text,"%s wiped all messages from the board in the %s.\n",user->name,rm->name);
    }
else {
    rename("tempfile",infile);
    sprintf(text,"%d board messages deleted.\n",cnt);
    write_user(user,text);
	rm->mesg_cnt-=cnt;
	sprintf(text,"%s wipes some messages from the board.\n",name);
	if (user->level<GOD || user->vis) write_room_except(rm,text,user);
	sprintf(text,"%s wiped %d messages from the board in the %s.\n",user->name,cnt,rm->name);
    }
write_syslog(text,1,SYSLOG);
}

	

/*** Search all the boards for the words given in the list. Rooms fixed to
	private will be ignore if the users level is less than gatecrash_level ***/
search_boards(user)
UR_OBJECT user;
{
RM_OBJECT rm;
FILE *fp;
char filename[80],line[82],buff[(MAX_LINES+1)*82],w1[81];
int w,cnt,message,yes,room_given;

if (word_count<2) {
	write_user(user,"Usage: search <word list>\n");  return;
	}
/* Go through rooms */
cnt=0;
for(rm=room_first;rm!=NULL;rm=rm->next) {
	sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
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
	sprintf(text,"Total of %d matching messages.\n\n",cnt);
	write_user(user,text);
	}
else write_user(user,"No occurences found.\n");
}



/*** See review of conversation ***/
review(user)
UR_OBJECT user;
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


/*** Return to home site ***/
home(user)
UR_OBJECT user;
{
if (user->room!=NULL) {
	write_user(user,"You are already on your home system.\n");
	return;
	}
write_user(user,"~FB~OLYou traverse cyberspace...\n");
sprintf(text,"REL %s\n",user->name);
write_sock(user->netlink->socket,text);
sprintf(text,"NETLINK: %s returned from %s.\n",user->name,user->netlink->service);
write_syslog(text,1,NETLOG);
user->room=user->netlink->connect_room;
user->netlink=NULL;
if (user->vis) {
	sprintf(text,"%s %s\n",user->name,user->in_phrase);
	write_room_except(user->room,text,user);
	}
else write_room_except(user->room,invisenter,user);
look(user);
}


/*** Show some user stats ***/
status(user)
UR_OBJECT user;
{
UR_OBJECT u;
char ir[ROOM_NAME_LEN+1], text2[ARR_SIZE], text3[ARR_SIZE], rm[3];
int days,hours,mins,hs,on,cnt;
char homepg[80], email[80];

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
            destruct_user(u); destructed=0;
            return;
            }
        on=0;
        }
    else on=1;
    }
write_user(user,"\n\n");
if (!on) write_user(user,"+----- ~FTUser Info~RS -- ~FB(not currently logged on)~RS -------------------------------+\n");
else write_user(user,"+----- ~FTUser Info~RS -- ~FB(currently logged on)~RS -----------------------------------+\n");

sprintf(text2,"%s %s",u->name,u->desc);
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
else sprintf(text3,"            Online for : %d mins",mins);
sprintf(text,"Gender : %-8s      Age : %-8s %s\n",sex[u->gender],text2,text3);
write_user(user,text);
if (!strcmp(u->homepage,"#UNSET")) sprintf(homepg,"Currently unset");
else strcpy(homepg,u->homepage);
if (!strcmp(u->email,"#UNSET")) sprintf(email,"Currently unset");
else {
    if (!u->hideemail || user->level>=WIZ) strcpy(email,u->email);
    else strcpy(email,"Currently only on view to Wizzes");
    }
sprintf(text,"Email Address : %s\n",email);
write_user(user,text);
sprintf(text,"Homepage URL  : %s\n",homepg);
write_user(user,text);
mins=(u->total_login%3600)/60;
sprintf(text,"Total Logins  : %-9d  Total login : %d days, %d hours, %d minutes\n",u->logons,days,hours,mins);
write_user(user,text);
write_user(user,"+----- ~FTGeneral Info~RS ---------------------------------------------------------+\n");
sprintf(text,"Enter Msg     : %s %s\n",u->name,u->in_phrase);
write_user(user,text);
sprintf(text,"Exit Msg      : %s %s~RS to the...\n",u->name,u->out_phrase);
write_user(user,text);
if (!on || u->login) {
    sprintf(text,"New Mail      : %-13s  Muzzled : %-13s\n",noyes2[has_unread_mail(u)],noyes2[(u->muzzled>0)]);
    write_user(user,text);
    }
else {
    sprintf(text,"Invited to    : %-13s  Muzzled : %-13s  Ignoring : %s\n",ir,noyes2[(u->muzzled>0)],noyes2[u->ignall]);
    write_user(user,text);
    if (u->type==REMOTE_TYPE || u->room==NULL) hs=0; else hs=1;
    if (u->room==NULL) sprintf(ir,"<off site>");
    else sprintf(ir,"%s",u->room);
    sprintf(text,"In Area       : %-13s  At home : %-13s  New Mail : %s\n",ir,noyes2[hs],noyes2[has_unread_mail(u)]);
    write_user(user,text);
    }
if (u==user || user->level>=WIZ) {
    write_user(user,"+----- ~FTUser Only Info~RS -------------------------------------------------------+\n");
    sprintf(text,"Char echo     : %-13s  Wrap    : %-13s  Monitor  : %s\n",noyes2[u->charmode_echo],noyes2[u->wrap],noyes2[u->monitor]);
    write_user(user,text);
    sprintf(text,"Colours       : %-13s  Pager   : %-13d\n",noyes2[u->colour],u->pager);
    write_user(user,text);
     if (u->lroom==2) sprintf(rm,"YES");  else sprintf(rm,"%s",noyes2[u->lroom]);
    sprintf(text,"Set logon room: %-13s  Autofwd : %-13s  Verified : %-13s\n",rm,noyes2[u->autofwd],noyes2[u->mail_verified]);
    write_user(user,text);
    if (on && !u->login) {
	if (!u->call[0]) write_user(user,"Quick call to : <no one>\n");
	else {
	    sprintf(text,"Quick call to : %-13s\n",u->call);
	    write_user(user,text);
	    }
        if (u==user && user->level<WIZ) sprintf(text,"On from site  : %s\n",u->site);
        else sprintf(text,"On from site  : %-42.42s  Port : %d\n",u->site,u->site_port);
        write_user(user,text);
        }
    }
if (user->level>=WIZ) {
    write_user(user,"+----- ~OL~FTWiz Only Info~RS --------------------------------------------------------+\n");
    if (u->lroom==2) sprintf(rm,"YES");  else sprintf(rm,"NO");
    sprintf(text,"Logon room    : %-38s  Shackled : %s\n",u->logout_room,rm);
    write_user(user,text);
    sprintf(text,"Last site     : %s\n",u->last_site);
    write_user(user,text);
    if (!u->muzzled) strcpy(text2,"Not Muzzled");
    else strcpy(text2,level_name[u->muzzled]);
    sprintf(text,"Unarrest Lev  : %-13s  Muz Lev : %-13s\n",level_name[u->unarrest],text2);
    write_user(user,text);
    sprintf(text,"User Expires  : %-13s  On date : %s",noyes2[u->expire],ctime((time_t *)&u->t_expire));
    write_user(user,text);
    }
write_user(user,"+----------------------------------------------------------------------------+\n\n");

if (!on) {
    destruct_user(u); destructed=0;
    }
}



/*** Read your mail ***/
rmail(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
int ret;
char c,filename[80],line[DNL+1];

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"You have no mail.\n");  return;
	}
/* Update last read / new mail received time at head of file */
if (outfp=fopen("tempfile","w")) {
	fprintf(outfp,"%d\r",(int)(time(0)));
	/* skip first line of mail file */
	fgets(line,DNL,infp);

	/* Copy rest of file */
	c=getc(infp);
	while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }

	fclose(outfp);
	rename("tempfile",filename);
	}
user->read_mail=time(0);
fclose(infp);
/* Just reading the one message */
if (word_count>1) {
  read_specific_mail(user);
  return;
  }
/* Readong the whole mail box */
write_user(user,"\n~BB*** Your mail ***\n\n");
ret=more(user,user->socket,filename);
if (ret==1) user->misc_op=2;
}



/*** Send mail message ***/
smail(user,inpstr,done_editing)
UR_OBJECT user;
char *inpstr;
int done_editing;
{
UR_OBJECT u;
FILE *fp;
int remote,has_account;
char *c,filename[80];

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
		sprintf(filename,"%s/%s.D",USERFILES,word[1]);
		if (!(fp=fopen(filename,"r"))) {
			write_user(user,nosuchuser);  return;
			}
		has_account=1;
		fclose(fp);
		}
	if (u==user && user->level<ARCH) {
		write_user(user,"Trying to mail yourself is the fifth sign of madness.\n");
		return;
		}
	if (u!=NULL) strcpy(word[1],u->name); 
	if (!has_account) {
		/* See if user has local account */
		sprintf(filename,"%s/%s.D",USERFILES,word[1]);
		if (!(fp=fopen(filename,"r"))) {
			sprintf(text,"%s is a remote user and does not have a local account.\n",u->name);
			write_user(user,text);  
			return;
			}
		fclose(fp);
		}
	}
if (word_count>2) {
	/* One line mail */
	strcat(inpstr,"\n"); 
	send_mail(user,word[1],remove_first(inpstr),0);
	send_copies(user,remove_first(inpstr));
	return;
	}
if (user->type==REMOTE_TYPE) {
	write_user(user,"Sorry, due to software limitations remote users cannot use the line editor.\nUse the '.smail <user> <mesg>' method instead.\n");
	return;
	}
sprintf(text,"\n~BB*** Writing mail message to %s ***\n\n",word[1]);
write_user(user,text);
user->misc_op=4;
strcpy(user->mail_to,word[1]);
editor(user,NULL);
}


/*** Delete some or all of your mail. A problem here is once we have deleted
     some mail from the file do we mark the file as read? If not we could
     have a situation where the user deletes all his mail but still gets
     the YOU HAVE UNREAD MAIL message on logging on if the idiot forgot to 
     read it first. ***/
dmail(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
int num,valid,cnt,i;
char filename[80],w1[ARR_SIZE],line[ARR_SIZE];

if (word_count<2) {
    write_user(user,"Usage: dmail all\n");
    write_user(user,"Usage: dmail <#>\n");
    write_user(user,"Usage: dmail to <#>\n");
    write_user(user,"Usage: dmail from <#> to <#>\n");
    return;
   }
if (get_wipe_parameters(user)==-1) return;
num=mail_count(user);
sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (user->wipe_from==-1) {
    unlink(filename);
    write_user(user,"All mail messages deleted.\n");
    return;
    }
if (user->wipe_from>num) {
	sprintf(text,"You only have %d mail messages.\n",num);
	write_user(user,text);   return;
	}
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"You have no mail to delete.\n");  return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile in dmail().\n",0,SYSLOG);
	fclose(infp);
	return;
	}
fprintf(outfp,"%d\r",(int)time(0));
user->read_mail=time(0)+1;
cnt=0;
fgets(line,DNL,infp);
for (i=1;i<user->wipe_from;i++) {
	fgets(line,82,infp);
	while(line[0]!='\n') {
		if (feof(infp)) goto SKIP_WIPE;
		fputs(line,outfp);  fgets(line,82,infp);
		}
	fputs(line,outfp);
	}
for (;i<=user->wipe_to;i++) {
	fgets(line,82,infp);
	if (i==num) { cnt++; goto SKIP_WIPE; }
	while(line[0]!='\n') fgets(line,82,infp);
	cnt++;
	}
fgets(line,82,infp);
while(!feof(infp)) {
	fputs(line,outfp);
    if (line[0]=='\n') i++;
	fgets(line,82,infp);
	 }
SKIP_WIPE:
fclose(infp);
fclose(outfp);
unlink(filename);
if (user->wipe_from==0 && i<user->wipe_to) {
    unlink("tempfile");
    sprintf(text,"There were only %d mail messages, all now deleted.\n",cnt);
    write_user(user,text);
    return;
    }
if ((user->wipe_from==0 && i==user->wipe_to) || cnt==num) {
    unlink("tempfile"); /* cos it'll be empty anyway */
    write_user(user,"All mail messages deleted.\n");
	 }
else {
    rename("tempfile",filename);
    sprintf(text,"%d mail messages deleted.\n",cnt);
    write_user(user,text);
    }
}


/*** Show list of people your mail is from without seeing the whole lot ***/
mail_from(user)
UR_OBJECT user;
{
FILE *fp;
int valid,cnt;
char w1[ARR_SIZE],line[ARR_SIZE],filename[80];

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) {
	write_user(user,"You have no mail.\n");  return;
	}
write_user(user,"\n~BB*** Mail from ***\n\n");
valid=1;  cnt=0;
fgets(line,DNL,fp); 
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
sprintf(text,"\nTotal of ~OL%d~RS messages.\n\n",cnt);
write_user(user,text);
}



/*** Enter user profile ***/
enter_profile(user,done_editing)
UR_OBJECT user;
int done_editing;
{
FILE *fp;
char *c,filename[80];

if (!done_editing) {
	write_user(user,"\n~BB*** Writing profile ***\n\n");
	user->misc_op=5;
	editor(user,NULL);
	return;
	}
sprintf(filename,"%s/%s.P",USERFILES,user->name);
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


/*** Examine a user ***/
examine(user)
UR_OBJECT user;
{
UR_OBJECT u,u2;
FILE *fp;
char filename[80],line[82],text2[ARR_SIZE];
int new_mail,days,hours,mins,timelen,days2,hours2,mins2,idle,cnt;

if (word_count<2) {
	write_user(user,"Examine who?\n");  return;
	}
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
	u2=NULL;
	}
else u2=u;

sprintf(filename,"%s/%s.M",USERFILES,u->name);
if (!(fp=fopen(filename,"r"))) new_mail=0;
else {
	fscanf(fp,"%d",&new_mail);
	fclose(fp);
	}

days=u->total_login/86400;
hours=(u->total_login%86400)/3600;
mins=(u->total_login%3600)/60;
timelen=(int)(time(0) - u->last_login);
days2=timelen/86400;
hours2=(timelen%86400)/3600;
mins2=(timelen%3600)/60;

write_user(user,"+----------------------------------------------------------------------------+\n");
sprintf(text2,"%s %s",u->name,u->desc);
cnt=colour_com_count(text2);
sprintf(text,"Name   : %-*.*s~RS Level : %s\n",45+cnt*3,45+cnt*3,text2,level_name[u->level]);
write_user(user,text);

if (u2==NULL) {
	sprintf(text,"Last login : %s",ctime((time_t *)&(u->last_login)));
	write_user(user,text);
	sprintf(text,"Which was  : %d days, %d hours, %d minutes ago\n",days2,hours2,mins2);
	write_user(user,text);
	sprintf(text,"Was on for : %d hours, %d minutes\nTotal login: %d days, %d hours, %d minutes\n",u->last_login_len/3600,(u->last_login_len%3600)/60,days,hours,mins);
	write_user(user,text);
	if (user->level>=WIZ) {
		sprintf(text,"Last site  : %s\n",u->last_site);
		write_user(user,text);
		}
	if (new_mail>u->read_mail) {
		sprintf(text,"%s has unread mail.\n",u->name);
		write_user(user,text);
	      }
	sprintf(text,"+----- Profile --------------------------------------------------------------+\n\n");
	write_user(user,text);
	sprintf(filename,"%s/%s.P",USERFILES,u->name);
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
sprintf(text,"On since    : %sOn for      : %d hours, %d minutes\n",ctime((time_t *)&u->last_login),hours2,mins2);
write_user(user,text);
if (u->afk) {
	sprintf(text,"Idle for    : %d minutes ~BR(AFK)\n",idle);
	write_user(user,text);
	if (u->afk_mesg[0]) {
		sprintf(text,"AFK message : %s\n",u->afk_mesg);
		write_user(user,text);
		}
	}
else {
	sprintf(text,"Idle for    : %d minutes\n",idle);
	write_user(user,text);
	}
sprintf(text,"Total login : %d days, %d hours, %d minutes\n",days,hours,mins);
write_user(user,text);
if (u->socket==-1) {
	sprintf(text,"Home service: %s\n",u->netlink->service);
	write_user(user,text);
	}
else {
	if (user->level>=WIZ) {
		sprintf(text,"Site        : %-40.40s  Port : %d\n",u->site,u->site_port);
		write_user(user,text);
		}
	}
if (new_mail>u->read_mail) {
	sprintf(text,"%s has unread mail.\n",u->name);
	write_user(user,text);
	}
sprintf(text,"+----- Profile --------------------------------------------------------------+\n\n");
write_user(user,text);
sprintf(filename,"%s/%s.P",USERFILES,u->name);
if (!(fp=fopen(filename,"r"))) write_user(user,"User has not yet written a profile.\n\n");
else {
  fclose(fp);
  more(user,user->socket,filename);
  }
write_user(user,"+----------------------------------------------------------------------------+\n\n");
}



/*** Show talker rooms ***/
rooms(user,show_topics,wrap)
UR_OBJECT user;
int show_topics,wrap;
{
RM_OBJECT rm;
UR_OBJECT u;
NL_OBJECT nl;
char access[9],stat[9],serv[SERV_NAME_LEN+1];
int cnt,rm_cnt,rm_pub,rm_priv;

if (word_count<2) {
  if (!wrap) user->wrap_room=room_first;
  if (show_topics) 
    write_user(user,"\n~FTRoom name            : Access  Users  Mesgs  Topic\n\n");
  else write_user(user,"\n~FTRoom name            : Access  Users  Mesgs  Inlink  LStat  Service\n\n");
  rm_cnt=0;
  for(rm=user->wrap_room;rm!=NULL;rm=rm->next) {
    if (rm_cnt==user->pager-4) {   /* -4 for the 'Room name...' header */
      switch (show_topics) {
	case 0: user->misc_op=10; break;
	case 1: user->misc_op=11; break;
	}
      write_user(user,"~BB~FG-=[*]=- PRESS <RETURN>, E TO EXIT:~RS ");
      return;
      }
    if (rm->access & PRIVATE) strcpy(access," ~FRPRIV");
    else strcpy(access,"  ~FGPUB");
    if (rm->access & FIXED) access[0]='*';
    cnt=0;
    for(u=user_first;u!=NULL;u=u->next) 
      if (u->type!=CLONE_TYPE && u->room==rm) ++cnt;
    if (show_topics)
      sprintf(text,"%-20s : %9s~RS    %3d    %3d  %s\n",rm->name,access,cnt,rm->mesg_cnt,rm->topic);
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
      sprintf(text,"%-20s : %9s~RS    %3d    %3d     %s   %s~RS  %s\n",rm->name,access,cnt,rm->mesg_cnt,noyes1[rm->inlink],stat,serv);
    }
    write_user(user,text);
    ++rm_cnt;  user->wrap_room=rm->next;
  }
  user->misc_op=0;
  rm_pub=rm_priv=0;
  for (rm=room_first;rm!=NULL;rm=rm->next) {
    if (rm->access & PRIVATE) ++rm_priv;
    else ++rm_pub;
    }
  sprintf(text,"\nThere is a total of %d rooms.  %d are public, and %d are private.\n\n",rm_priv+rm_pub,rm_pub,rm_priv);
  write_user(user,text);
  return;
  }
strtoupper(word[1]);
rm_cnt=0;  cnt=0;
if (!strcmp(word[1],"LEVEL")) {
  write_user(user,"The following rooms are default...\n\n");
  sprintf(text,"Default main room : %s\n",room_first->name);
  write_user(user,text);
  sprintf(text,"Default warp room : %s\n",default_warp);
  write_user(user,text);
  sprintf(text,"Default jail room : %s\n",default_jail);
  write_user(user,text);
  while(priv_room[rm_cnt].name[0]!='*') {
    if (++cnt==1) write_user(user,"\nThe following rooms are level specific...\n\n");
    sprintf(text,"~FT%s~RS if for users of level ~OL%s~RS and above.\n",priv_room[rm_cnt].name,level_name[priv_room[rm_cnt].level]);
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


/*** List defined netlinks and their status ***/
netstat(user)
UR_OBJECT user;
{
NL_OBJECT nl;
UR_OBJECT u;
char *allow[]={ "  ?","ALL"," IN","OUT" };
char *type[]={ "  -"," IN","OUT" };
char portstr[6],stat[9],vers[8];
int iu,ou,a;

if (nl_first==NULL) {
	write_user(user,"No remote connections configured.\n");  return;
	}
write_user(user,"\n~BB*** Netlink data & status ***\n\n~FTService name    : Allow Type Status IU OU Version  Site\n\n");
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	iu=0;  ou=0;
	if (nl->stage==UP) {
		for(u=user_first;u!=NULL;u=u->next) {
			if (u->netlink==nl) {
				if (u->type==REMOTE_TYPE)  ++iu;
				if (u->room==NULL) ++ou;
				}
			}
		}
	if (nl->port) sprintf(portstr,"%d",nl->port);  else portstr[0]='\0';
	if (nl->type==UNCONNECTED) {
		strcpy(stat,"~FRDOWN");  strcpy(vers,"-");
		}
	else {
		if (nl->stage==UP) strcpy(stat,"  ~FGUP");
		else strcpy(stat," ~FYVER");
		if (!nl->ver_major) strcpy(vers,"3.?.?"); /* Pre - 3.2 version */  
		else sprintf(vers,"%d.%d.%d",nl->ver_major,nl->ver_minor,nl->ver_patch);
		}
	/* If link is incoming and remoter vers < 3.2 we have no way of knowing 
	   what the permissions on it are so set to blank */
	if (!nl->ver_major && nl->type==INCOMING && nl->allow!=IN) a=0; 
	else a=nl->allow+1;
	sprintf(text,"%-15s :   %s  %s   %s~RS %2d %2d %7s  %s %s\n",nl->service,allow[a],type[nl->type],stat,iu,ou,vers,nl->site,portstr);
	write_user(user,text);
	}
write_user(user,"\n");
}



/*** Show type of data being received down links (this is usefull when a
     link has hung) ***/
netdata(user)
UR_OBJECT user;
{
NL_OBJECT nl;
char from[80],name[USER_NAME_LEN+1];
int cnt;

cnt=0;
write_user(user,"\n~BB*** Mail receiving status ***\n\n");
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type==UNCONNECTED || nl->mailfile==NULL) continue;
	if (++cnt==1) write_user(user,"To              : From                       Last recv.\n\n");
	sprintf(from,"%s@%s",nl->mail_from,nl->service);
	sprintf(text,"%-15s : %-25s  %d seconds ago.\n",nl->mail_to,from,(int)(time(0)-nl->last_recvd));
	write_user(user,text);
	}
if (!cnt) write_user(user,"No mail being received.\n\n");
else write_user(user,"\n");

cnt=0;
write_user(user,"\n~BB*** Message receiving status ***\n\n");
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type==UNCONNECTED || nl->mesg_user==NULL) continue;
	if (++cnt==1) write_user(user,"To              : From             Last recv.\n\n");
	if (nl->mesg_user==(UR_OBJECT)-1) strcpy(name,"<unknown>");
	else strcpy(name,nl->mesg_user->name);
	sprintf(text,"%-15s : %-15s  %d seconds ago.\n",name,nl->service,(time(0)-nl->last_recvd));
	write_user(user,text);
	}
if (!cnt) write_user(user,"No messages being received.\n\n");
else write_user(user,"\n");
}


/*** Connect a netlink. Use the room as the key ***/
connect_netlink(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;
int ret,tmperr;

if (word_count<2) {
	write_user(user,"Usage: connect <room service is linked to>\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
if ((nl=rm->netlink)==NULL) {
	write_user(user,"That room is not linked to a service.\n");
	return;
	}
if (nl->type!=UNCONNECTED) {
	write_user(user,"That rooms netlink is already up.\n");  return;
	}
write_user(user,"Attempting connect (this may cause a temporary hang)...\n");
sprintf(text,"NETLINK: Connection attempt to %s initiated by %s.\n",nl->service,user->name);
write_syslog(text,1,NETLOG);
errno=0;
if (!(ret=connect_to_site(nl))) {
	write_user(user,"~FGInitial connection made...\n");
	sprintf(text,"NETLINK: Connected to %s (%s %d).\n",nl->service,nl->site,nl->port);
	write_syslog(text,1,NETLOG);
	nl->connect_room=rm;
	return;
	}
tmperr=errno; /* On Linux errno seems to be reset between here and sprintf */
write_user(user,"~FRConnect failed: ");
write_syslog("NETLINK: Connection attempt failed: ",1,NETLOG);
if (ret==1) {
	sprintf(text,"%s.\n",sys_errlist[tmperr]);
	write_user(user,text);
	write_syslog(text,0,SYSLOG);
	return;
	}
write_user(user,"Unknown hostname.\n");
write_syslog("Unknown hostname.\n",0,SYSLOG);
}



/*** Disconnect a link ***/
disconnect_netlink(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;

if (word_count<2) {
	write_user(user,"Usage: disconnect <room service is linked to>\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
nl=rm->netlink;
if (nl==NULL) {
	write_user(user,"That room is not linked to a service.\n");
	return;
	}
if (nl->type==UNCONNECTED) {
	write_user(user,"That rooms netlink is not connected.\n");  return;
	}
/* If link has hung at verification stage don't bother announcing it */
if (nl->stage==UP) {
	sprintf(text,"~OLSYSTEM:~RS Disconnecting from %s in the %s.\n",nl->service,rm->name);
	write_room(NULL,text);
	sprintf(text,"NETLINK: Link to %s in the %s disconnected by %s.\n",nl->service,rm->name,user->name);
	write_syslog(text,1,NETLOG);
	}
else {
	sprintf(text,"NETLINK: Link to %s disconnected by %s.\n",nl->service,user->name);
	write_syslog(text,1,NETLOG);
	}
shutdown_netlink(nl);
write_user(user,"Disconnected.\n");
}


/*** Change users password. Only ARCHes and above can change another users 
	password and they do this by specifying the user at the end. When this is 
	done the old password given can be anything, the wiz doesnt have to know it
	in advance. ***/
change_pass(user)
UR_OBJECT user;
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
	if (strcmp((char *)crypt(word[1],"NU"),user->pass)) {
		write_user(user,"Old password incorrect.\n");  return;
		}
	if (!strcmp(word[1],word[2])) {
		write_user(user,"Old and new passwords are the same.\n");  return;
		}
	strcpy(user->pass,(char *)crypt(word[2],"NU"));
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
if (u=get_user(word[3])) {
	if (u->type==REMOTE_TYPE) {
		write_user(user,"You cannot change the password of a user logged on remotely.\n");
		return;
		}
	if (u->level>=user->level) {
		write_user(user,"You cannot change the password of a user of equal or higher level than yourself.\n");
		return;
		}
	strcpy(u->pass,(char *)crypt(word[2],"NU"));
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
strcpy(u->pass,(char *)crypt(word[2],"NU"));
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
kill_user(user)
UR_OBJECT user;
{
UR_OBJECT victim;
RM_OBJECT rm;
char *name;

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
	sprintf(text,"%s tried to kill you!\n",user->name);
	write_user(victim,text);
	return;
	}
sprintf(text,"%s KILLED %s.\n",user->name,victim->name);
write_syslog(text,1,SYSLOG);
write_user(user,"~FM~OLYou chant an evil incantation...\n");
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~FM~OL%s chants an evil incantation...\n",name);
write_room_except(user->room,text,user);
write_user(victim,"~FM~OLA shrieking furie rises up out of the ground, and devours you!!!\n");
sprintf(text,"~FM~OLA shrieking furie rises up out of the ground, devours %s and vanishes!!!\n",victim->name);
rm=victim->room;
write_room_except(rm,text,victim);
sprintf(text,"~FRKilled~RS by %s.\n",user->name);
add_history(victim->name,1,text);
disconnect_user(victim);
write_monitor(user,NULL,0);
write_room(NULL,"~FM~OLYou hear insane laughter from the beyond the grave...\n");
}


/*** Promote a user ***/
promote(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
char text2[80],*name;

if (word_count<2) {
	write_user(user,"Usage: promote <user>\n");  return;
	}
/* See if user is on atm */
if ((u=get_user(word[1]))!=NULL) {
	if (u->level>=user->level) {
		write_user(user,"You cannot promote a user to a level higher than your own.\n");
		return;
		}
	if (user->vis) name=user->name; else name=invisname;
	clean_level_list(u->name,u->level);
	u->level++;  u->unarrest=u->level;
	add_level_list(u->name,u->level);
	rm=user->room;
	user->room=NULL;
	sprintf(text,"~FG~OL%s is promoted to level: ~RS~OL%s.\n",u->name,level_name[u->level]);
	write_level(u->level,1,text,u);
	user->room=rm;
	sprintf(text,"~FG~OL%s has promoted you to level: ~RS~OL%s!\n",name,level_name[u->level]);
	write_user(u,text);
	sprintf(text,"%s PROMOTED %s to level %s.\n",name,u->name,level_name[u->level]);
	write_syslog(text,1,SYSLOG);
	sprintf(text,"Was ~FGpromoted~RS by %s to level %s.\n",user->name,level_name[u->level]);
	add_history(u->name,1,text);
	u->accreq=1;
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
if (u->level>=user->level) {
	write_user(user,"You cannot promote a user to a level higher than your own.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
clean_level_list(u->name,u->level);
u->level++;  u->unarrest=u->level;
add_level_list(u->name,u->level);
u->socket=-2;
u->accreq=1;
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
destruct_user(u);
destructed=0;
}


/*** Demote a user ***/
demote(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
char text2[80],*name;

if (word_count<2) {
	write_user(user,"Usage: demote <user>\n");  return;
	}
/* See if user is on atm */
if ((u=get_user(word[1]))!=NULL) {
	if (u->level<=NEW) {
		write_user(user,"You cannot demote a user of level NEW or JAILED.\n");
		return;
		}
	if (u->level>=user->level) {
		write_user(user,"You cannot demote a user of an equal or higher level than yourself.\n");
		return;
		}
	if (user->vis) name=user->name; else name=invisname;
	clean_level_list(u->name,u->level);
	u->level--;  u->unarrest=u->level;
	add_level_list(u->name,u->level);
	rm=user->room;
	user->room=NULL;
	sprintf(text,"~FR~OL%s is demoted to level: ~RS~OL%s.\n",u->name,level_name[u->level]);
        write_level(u->level,1,text,u);
	user->room=rm;
	sprintf(text,"~FR~OL%s has demoted you to level: ~RS~OL%s!\n",name,level_name[u->level]);
	write_user(u,text);
	sprintf(text,"%s DEMOTED %s to level %s.\n",user->name,u->name,level_name[u->level]);
	write_syslog(text,1,SYSLOG);
	sprintf(text,"Was ~FRdemoted~RS by %s to level %s.\n",user->name,level_name[u->level]);
	add_history(u->name,1,text);
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
clean_level_list(u->name,u->level);
u->level--;
u->unarrest=u->level;
add_level_list(u->name,u->level);
u->socket=-2;
u->vis=1;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"You demote %s to level: ~OL%s.\n",u->name,level_name[u->level]);
write_user(user,text);
sprintf(text2,"~FR~OLYou have been demoted to level: ~RS~OL%s.\n",level_name[u->level]);
send_mail(user,word[1],text2,0);
sprintf(text,"%s DEMOTED %s to level %s.\n",user->name,word[1],level_name[u->level]);
write_syslog(text,1,SYSLOG);
sprintf(text,"Was ~FRdemoted~RS by %s to level %s.\n",user->name,level_name[u->level]);
add_history(u->name,1,text);
destruct_user(u);
destructed=0;
}


/*** List banned sites or users ***/
listbans(user)
UR_OBJECT user;
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
ban(user)
UR_OBJECT user;
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


ban_site(user)
UR_OBJECT user;
{
FILE *fp;
char filename[80],host[81],site[80],*given,*check;

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
check=word[2];
while (*check) {
    if (*check=='*') {
        ++check;
	if (*check=='*') {
	    write_user(user,"You cannot use two wild-cards next to each other.\n");
	    return;
	    }
        }
    ++check;
    }
given=host;  check=word[2];
/* check if, with the wild card, the name matches host's name */
while(*given) {
    if (*check=='*') {
      ++check;
      if (!*check) {
	  write_user(user,"You cannot ban the machine that that program is running on.\n");
	  return;
	  }
      while (*given && *given!=*check) ++given;
      }
    if (*check==*given) { ++check;  ++given; }
    else goto SKIP;
    }
write_user(user,"You cannot ban the machine that that program is running on.\n");
return;

SKIP:
sprintf(filename,"%s/%s",DATAFILES,SITEBAN);

/* See if ban already set for given site */
if (fp=fopen(filename,"r")) {
	fscanf(fp,"%s",site);
	while(!feof(fp)) {
		if (!strcmp(site,word[2])) {
			write_user(user,"That site/domain is already banned.\n");
			fclose(fp);  return;
			}
		fscanf(fp,"%s",site);
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


ban_user(user)
UR_OBJECT user;
{
UR_OBJECT u;
FILE *fp;
char filename[80],filename2[80],p[20],name[USER_NAME_LEN+1];
int a,b,c,d,level;

word[2][0]=toupper(word[2][0]);
if (!strcmp(user->name,word[2])) {
	write_user(user,"Trying to ban yourself is the seventh sign of madness.\n");
	return;
	}

/* See if ban already set for given user */
sprintf(filename,"%s/%s",DATAFILES,USERBAN);
if (fp=fopen(filename,"r")) {
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
	/* User not on so load up his data */
	sprintf(filename2,"%s/%s.D",USERFILES,word[2]);
	if (!(fp=fopen(filename2,"r"))) {
		write_user(user,nosuchuser);  return;
		}
	fscanf(fp,"%s\n%d %d %d %d %d",p,&a,&b,&c,&d,&level);
	fclose(fp);
	if (level>=user->level) {
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
	write_user(u,"\n\07~FR~OL~LIYou have been banned from the Observatory.\n\n");
	disconnect_user(u);
	}
}

	

/*** uban a site (or domain) or user ***/
unban(user)
UR_OBJECT user;
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


unban_site(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
char filename[80],site[80];
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
fscanf(infp,"%s",site);
while(!feof(infp)) {
	if (strcmp(word[2],site)) {  
		fprintf(outfp,"%s\n",site);  cnt++;  
		}
	else found=1;
	fscanf(infp,"%s",site);
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


unban_user(user)
UR_OBJECT user;
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



/*** Set user visible or invisible ***/
visibility(user,vis)
UR_OBJECT user;
int vis;
{
if (vis) {
	if (user->vis) {
		write_user(user,"You are already visible.\n");  return;
		}
	write_user(user,"~FB~OLYou recite a melodic incantation and reappear.\n");
	sprintf(text,"~FB~OLYou hear a melodic incantation chanted and %s materialises!\n",user->name);
	write_room_except(user->room,text,user);
	user->vis=1;
	return;
	}
if (!user->vis) {
	write_user(user,"You are already invisible.\n");  return;
	}
write_user(user,"~FB~OLYou recite a melodic incantation and fade out.\n");
sprintf(text,"~FB~OL%s recites a melodic incantation and disappears!\n",user->name);
write_room_except(user->room,text,user);
user->vis=0;
}


/*** Site a user ***/
site(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_user(user,"Usage: site <user>\n");  return;
	}
/* User currently logged in */
if (u=get_user(word[1])) {
	if (u->type==REMOTE_TYPE) sprintf(text,"%s is remotely connected from %s.\n",u->name,u->site);
	else sprintf(text,"%s is logged in from %s:%d.\n",u->name,u->site,u->site_port);
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
sprintf(text,"%s was last logged in from %s.\n",word[1],u->last_site);
write_user(user,text);
destruct_user(u);
destructed=0;
}


/*** Wake up some sleepy herbert ***/
wake(user)
UR_OBJECT user;
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
if (u->afk) {
	write_user(user,"You cannot wake someone who is AFK.\n");  return;
	}
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"\07\n~BR*** %s says: ~OL~LIWAKE UP!!!~RS~BR ***\n\n",name);
write_user(u,text);
write_user(user,"Wake up call sent.\n");
}


/*** Shout something to other wizes and gods. If the level isnt given it
	defaults to WIZ level. ***/
wizshout(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int lev;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot wizshout.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Usage: twiz [<superuser level>] <message>\n"); 
	return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
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
	sprintf(text,"~FY<WIZ: %s ~OL[%s]~RS~FY>~RS %s\n",user->name,level_name[lev],inpstr);
	write_level(lev,1,text,user);
	return;
	}
sprintf(text,"~FY<WIZ>~RS %s\n",inpstr);
write_user(user,text);
record_tell(user,text);
sprintf(text,"~FY<WIZ: %s>~RS %s\n",user->name,inpstr);
write_level(WIZ,1,text,user);
}


/*** Muzzle an annoying user so he cant speak, emote, echo, write, smail
	or bcast. Muzzles have levels from WIZ to GOD so for instance a wiz
     cannot remove a muzzle set by a god.  ***/
muzzle(user)
UR_OBJECT user;
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
		sprintf(text,"%s is already muzzled.\n",u->name);
		write_user(user,text);  return;
		}
	sprintf(text,"~FR~OL%s now has a muzzle of level: ~RS~OL%s.\n",u->name,level_name[user->level]);
	write_user(user,text);
	write_user(u,"~FR~OLYou have been muzzled!\n");
	sprintf(text,"%s muzzled %s.\n",user->name,u->name);
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
sprintf(text,"~FR~OL%s given a muzzle of level: ~RS~OL%s.\n",u->name,level_name[user->level]);
write_user(user,text);
send_mail(user,word[1],"~FR~OLYou have been muzzled!\n",0);
sprintf(text,"%s muzzled %s.\n",user->name,u->name);
write_syslog(text,1,SYSLOG);
sprintf(text,"Level %d (%s) ~FRmuzzle~RS put on by %s.\n",user->level,level_name[user->level],user->name);
add_history(u->name,1,text);
destruct_user(u);
destructed=0;
}



/*** Umuzzle the bastard now he's apologised and grovelled enough via email ***/
unmuzzle(user)
UR_OBJECT user;
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
		sprintf(text,"%s is not muzzled.\n",u->name);  return;
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
sprintf(text,"~FG~OLYou remove %s's muzzle.\n",u->name);
write_user(user,text);
send_mail(user,word[1],"~FG~OLYou have been unmuzzled.\n",0);
sprintf(text,"%s unmuzzled %s.\n",user->name,u->name);
write_syslog(text,1,SYSLOG);
sprintf(text,"~FGUnmuzzled~RS by %s, level %d (%s).\n",user->name,user->level,level_name[user->level]);
add_history(u->name,0,text);
destruct_user(u);
destructed=0;
}



/*** Switch system logging on and off ***/
logging(user)
UR_OBJECT user;
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
minlogin(user)
UR_OBJECT user;
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
system_details(user)
UR_OBJECT user;
{
NL_OBJECT nl;
RM_OBJECT rm;
UR_OBJECT u;
char bstr[40],minlogin[5];
char *ca[]={ "NONE  ","IGNORE","REBOOT" };
int days,hours,mins,secs;
int netlinks,live,inc,outg;
int rms,inlinks,num_clones,mem,size;
sprintf(text,"\n+----- ~FGSystem Status~RS --------------------------------------------------------+\n\n");
write_user(user,text);
sprintf(text,"Amnuts version %s (Andy's modifed NUTS - modifed from NUTS version %s)\n",AMNUTSVER,NUTSVER);
write_user(user,text);

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
	if (rm->inlink) ++inlinks;
	++rms;  mem+=size;
	}

netlinks=0;  
live=0;
inc=0; 
outg=0;
size=sizeof(struct netlink_struct);
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type!=UNCONNECTED && nl->stage==UP) live++;
	if (nl->type==INCOMING) ++inc;
	if (nl->type==OUTGOING) ++outg;
	++netlinks;  mem+=size;
	}
if (minlogin_level==-1) strcpy(minlogin,"NONE");
else strcpy(minlogin,level_name[minlogin_level]);

/* Show header parameters */
sprintf(text,"~FTProcess ID   : ~FG%d\n~FTTalker booted: ~FG%s~FTUptime       : ~FG%d days, %d hours, %d minutes, %d seconds\n",getpid(),bstr,days,hours,mins,secs);
write_user(user,text);
sprintf(text,"~FTPorts (M/W/L/WWW): ~FG%d,  %d,  %d\n\n",port[0],port[1],port[2]);
write_user(user,text);

/* Show others */
sprintf(text,"Max users              : %-3d          Current num. of users  : %d\n",max_users,num_of_users);
write_user(user,text);
sprintf(text,"New users this boot    : %-3d          Old users this boot    : %d\n",logons_new,logons_old);
write_user(user,text);
sprintf(text,"Max clones             : %-2d           Current num. of clones : %d\n",max_clones,num_clones);
write_user(user,text);
sprintf(text,"Current minlogin level : %-4s         Login idle time out    : %d secs.\n",minlogin,login_idle_time);
write_user(user,text);
sprintf(text,"User idle time out     : %-4d secs.   Heartbeat              : %d\n",user_idle_time,heartbeat);
write_user(user,text);
sprintf(text,"Remote user maxlevel   : %-4s         Remote user deflevel   : %s\n",level_name[rem_user_maxlevel],level_name[rem_user_deflevel]);
write_user(user,text);
sprintf(text,"Wizport min login level: %-4s         Gatecrash level        : %s\n",level_name[wizport_level],level_name[gatecrash_level]);
write_user(user,text);
sprintf(text,"Time out maxlevel      : %-4s         Private room min count : %d\n",level_name[time_out_maxlevel],min_private_users);
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
sprintf(text,"Echoing passwords      : %s          Swearing banned        : %s\n",noyes2[password_echo],noyes2[ban_swearing]);
write_user(user,text);
sprintf(text,"Time out afks          : %s          Allowing caps in name  : %s\n",noyes2[time_out_afks],noyes2[allow_caps_in_name]);
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


/*** Set the character mode echo on or off. This is only for users logging in
     via a character mode client, those using a line mode client (eg unix
     telnet) will see no effect. ***/
toggle_charecho(user)
UR_OBJECT user;
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


/*** Free a hung socket ***/
clearline(user)
UR_OBJECT user;
{
UR_OBJECT u;
int sock;

if (word_count<2 || !isnumber(word[1])) {
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


/*** Change whether a rooms access is fixed or not ***/
change_room_fix(user,fix)
UR_OBJECT user;
int fix;
{
RM_OBJECT rm;
char *name;

if (word_count<2) rm=user->room;
else {
	if ((rm=get_room(word[1]))==NULL) {
		write_user(user,nosuchroom);  return;
		}
	}
if (user->vis) name=user->name; else name=invisname;
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


/*** A newbie is requesting an account. Get his email address off him so we
     can validate who he is before we promote him and let him loose as a 
     proper user. ***/
account_request(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (user->level>NEW) {
	write_user(user,"This command is for new users only, you already have a full account.\n");
	return;
	}
/* This is so some pillock doesnt keep doing it just to fill up the syslog */
if (user->accreq) {
	write_user(user,"You have already requested an account.\n");
	return;
	}
if (word_count<2) {
	write_user(user,"Usage: accreq <an email address we can contact you on + any relevent info>\n");
	return;
	}
/* Could check validity of email address I guess but its a waste of time.
   If they give a duff address they don't get an account, simple. ***/
sprintf(text,"ACCREQ: %-*s : %s.\n",USER_NAME_LEN,user->name,inpstr);
write_syslog(text,1,REQLOG);
sprintf(text,"~OLSYSTEM:~RS %s has made a request for an account.\n",user->name);
write_level(WIZ,1,text,NULL);
write_user(user,"Account request logged.\n");
user->accreq=1;
add_history(user->name,1,"Made a request for an account.\n");
}


/*** Clear the review buffer ***/
revclr(user)
UR_OBJECT user;
{
char *name;

clear_revbuff(user->room); 
write_user(user,"Review buffer cleared.\n");
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s has cleared the review buffer.\n",name);
write_room_except(user->room,text,user);
}


/*** Clone a user in another room ***/
create_clone(user)
UR_OBJECT user;
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
strcpy(u->name,user->name);
strcpy(u->desc,"~BR(CLONE)");

if (rm==user->room)
	write_user(user,"~FB~OLYou wave your hands, mix some chemicals and a clone is created here.\n");
else {
	sprintf(text,"~FB~OLYou wave your hands, mix some chemicals, and a clone is created in the %s.\n",rm->name);
	write_user(user,text);
	}
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~FB~OL%s waves their hands...\n",name);
write_room_except(user->room,text,user);
sprintf(text,"~FB~OLA clone of %s appears in a swirling magical mist!\n",user->name);
write_room_except(rm,text,user);
}


/*** Destroy user clone ***/
destroy_clone(user)
UR_OBJECT user;
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
		if (user->vis) name=user->name; else name=invisname;
		sprintf(text,"~FM~OL%s whispers a sharp spell...\n",name);
		write_room_except(user->room,text,user);
		sprintf(text,"~FM~OLThe clone of %s shimmers and vanishes.\n",u2->name);
		write_room(rm,text);
		if (u2!=user) {
			sprintf(text,"~OLSYSTEM: ~FR%s has destroyed your clone in the %s.\n",user->name,rm->name);
			write_user(u2,text);
			}
		destructed=0;
		return;
		}
	}
if (u2==user) sprintf(text,"You do not have a clone in the %s.\n",rm->name);
else sprintf(text,"%s does not have a clone the %s.\n",u2->name,rm->name);
write_user(user,text);
}


/*** Show users own clones ***/
myclones(user)
UR_OBJECT user;
{
UR_OBJECT u;
int cnt;

cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type!=CLONE_TYPE || u->owner!=user) continue;
	if (++cnt==1) 
		write_user(user,"\n~BB*** Rooms you have clones in ***\n\n");
	sprintf(text,"  %s\n",u->room);
	write_user(user,text);
	}
if (!cnt) write_user(user,"You have no clones.\n");
else {
	sprintf(text,"\nTotal of %d clones.\n\n",cnt);
	write_user(user,text);
	}
}


/*** Show all clones on the system ***/
allclones(user)
UR_OBJECT user;
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
	sprintf(text,"%-15s : %s\n",u->name,u->room);
	write_user(user,text);
	}
if (!cnt) write_user(user,"There are no clones on the system.\n");
else {
	sprintf(text,"\nTotal of %d clones.\n\n",cnt);
	write_user(user,text);
	}
}


/*** User swaps places with his own clone. All we do is swap the rooms the
	objects are in. ***/
clone_switch(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;

if (word_count<2) {
	write_user(user,"Usage: switch <room clone is in>\n");  return;
	}
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
		sprintf(text,"%s turns into a clone!\n",u->name);
		write_room_except(u->room,text,u);
		look(user);
		return;
		}
	}
write_user(user,"You do not have a clone in that room.\n");
}


/*** Make a clone speak ***/
clone_say(user,inpstr)
UR_OBJECT user;
char *inpstr;
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


/*** Set what a clone will hear, either all speach , just bad language
	or nothing. ***/
clone_hear(user)
UR_OBJECT user;
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


/*** Stat a remote system ***/
remote_stat(user)
UR_OBJECT user;
{
NL_OBJECT nl;
RM_OBJECT rm;

if (word_count<2) {
	write_user(user,"Usage: rstat <room service is linked to>\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
if ((nl=rm->netlink)==NULL) {
	write_user(user,"That room is not linked to a service.\n");
	return;
	}
if (nl->stage!=2) {
	write_user(user,"Not (fully) connected to service.\n");
	return;
	}
if (nl->ver_major<=3 && nl->ver_minor<1) {
	write_user(user,"The NUTS version running that service does not support this facility.\n");
	return;
	}
sprintf(text,"RSTAT %s\n",user->name);
write_sock(nl->socket,text);
write_user(user,"Request sent.\n");
}


/*** Switch swearing ban on and off ***/
swban(user)
UR_OBJECT user;
{
if (!ban_swearing) {
	write_user(user,"Swearing ban ~FGON.\n");
	sprintf(text,"%s switched swearing ban ON.\n",user->name);
	write_syslog(text,1,SYSLOG);
	ban_swearing=1;  return;
	}
write_user(user,"Swearing ban ~FROFF.\n");
sprintf(text,"%s switched swearing ban OFF.\n",user->name);
write_syslog(text,1,SYSLOG);
ban_swearing=0;
}


/*** Do AFK ***/
afk(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (word_count>1) {
	if (!strcmp(word[1],"lock")) {
		if (user->type==REMOTE_TYPE) {
			/* This is because they might not have a local account and hence
			   they have no password to use. */
			write_user(user,"Sorry, due to software limitations remote users cannot use the lock option.\n");
			return;
			}
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
		sprintf(text,"%s goes AFK: %s\n",user->name,user->afk_mesg);
	else sprintf(text,"%s goes AFK...\n",user->name);
	write_room_except(user->room,text,user);
	}
clear_afk(user);
}


/*** Toggle user colour on and off ***/
toggle_colour(user)
UR_OBJECT user;
{
int col;

if (user->room==NULL) {
  prompt(user);
  return;
  }
for(col=1;col<NUM_COLS;++col) {
  sprintf(text,"%s: ~%sAmnuts v %s VIDEO TEST~RS\n",colcom[col],colcom[col],AMNUTSVER);
  write_user(user,text);
  }
}


suicide(user)
UR_OBJECT user;
{
if (word_count<2) {
	write_user(user,"Usage: suicide <your password>\n");  return;
	}
if (strcmp((char *)crypt(word[1],"NU"),user->pass)) {
	write_user(user,"Password incorrect.\n");  return;
	}
write_user(user,"\n\07~FR~OL~LI*** WARNING - This will delete your account! ***\n\nAre you sure about this (y/n)? ");
user->misc_op=6;  
no_prompt=1;
}


/*** Delete a user ***/
delete_user(user,this_user)
UR_OBJECT user;
int this_user;
{
UR_OBJECT u;
char filename[80],name[USER_NAME_LEN+1];
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
	disconnect_user(user);
	clean_files(name);
	clean_level_list(name,level);
	clean_userlist(name);
	user_number--;
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
clean_level_list(u->name,u->level);
clean_userlist(u->name);
sprintf(text,"\07~FR~OL~LIUser %s deleted!\n",u->name);
write_user(user,text);
sprintf(text,"%s DELETED %s.\n",user->name,u->name);
write_syslog(text,1,SYSLOG);
destruct_user(u);
destructed=0;
}


/*** Shutdown talker interface func. Countdown time is entered in seconds so
	we can specify less than a minute till reboot. ***/
shutdown_com(user)
UR_OBJECT user;
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
if (word_count>1 && !isnumber(word[1])) {
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
reboot_com(user)
UR_OBJECT user;
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
if (word_count>1 && !isnumber(word[1])) {
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



/*** Show recorded tells and pemotes ***/
revtell(user)
UR_OBJECT user;
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



/**************************** EVENT FUNCTIONS ******************************/

void do_events()
{
set_date_time();
check_reboot_shutdown();
check_idle_and_timeout();
check_nethangs_send_keepalives(); 
check_messages(NULL,0);
reset_alarm();
}


reset_alarm()
{
signal(SIGALRM,do_events);
alarm(heartbeat);
}



/*** See if timed reboot or shutdown is underway ***/
check_reboot_shutdown()
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
	sprintf(text,"~OLSYSTEM: %s in %d minutes, %d seconds.\n",w[rs_which],rs_countdown/60,rs_countdown%60);
	write_room(NULL,text);
	rs_announce=time(0);
	}
if (rs_countdown<60 && secs>=10) {
	sprintf(text,"~OLSYSTEM: %s in %d seconds.\n",w[rs_which],rs_countdown);
	write_room(NULL,text);
	rs_announce=time(0);
	}
}



/*** login_time_out is the length of time someone can idle at login, 
     user_idle_time is the length of time they can idle once logged in. 
     Also ups users total login time. ***/
check_idle_and_timeout()
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
	


/*** See if any net connections are dragging their feet. If they have been idle
     longer than net_idle_time the drop them. Also send keepalive signals down
     links, this saves having another function and loop to do it. ***/
check_nethangs_send_keepalives()
{
NL_OBJECT nl;
int secs;

for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type==UNCONNECTED) {
		nl->warned=0;  continue;
		}

	/* Send keepalives */
	nl->keepalive_cnt+=heartbeat;
	if (nl->keepalive_cnt>=keepalive_interval) {
		write_sock(nl->socket,"KA\n");
		nl->keepalive_cnt=0;
		}

	/* Check time outs */
	secs=(int)(time(0) - nl->last_recvd);
	if (nl->warned) {
		if (secs<net_idle_time-60) nl->warned=0;
		else {
			if (secs<net_idle_time) continue;
			sprintf(text,"~OLSYSTEM:~RS Disconnecting hung netlink to %s in the %s.\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			shutdown_netlink(nl);
			nl->warned=0;
			}
		continue;
		}
	if (secs>net_idle_time-60) {
		sprintf(text,"~OLSYSTEM:~RS Netlink to %s in the %s has been hung for %d seconds.\n",nl->service,nl->connect_room->name,secs);
		write_level(ARCH,1,text,NULL);
		nl->warned=1;
		}
	}
destructed=0;
}



/*** Remove any expired messages from boards unless force = 2 in which case
	just do a recount. ***/
check_messages(user,force)
UR_OBJECT user;
int force;
{
RM_OBJECT rm;
FILE *infp,*outfp;
char id[82],filename[80],line[82];
int valid,pt,write_rest;
int board_cnt,old_cnt,bad_cnt,tmp;
static int done=0;

switch(force) {
	case 0:
	if (mesg_check_hour==thour && mesg_check_min==tmin) {
		if (done) return;
		}
	else {  done=0;  return;  }
	break;

	case 1:
	printf("Checking boards...\n");
	}
done=1;
board_cnt=0;
old_cnt=0;
bad_cnt=0;

for(rm=room_first;rm!=NULL;rm=rm->next) {
	tmp=rm->mesg_cnt;  
	rm->mesg_cnt=0;
	sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
	if (!(infp=fopen(filename,"r"))) continue;
	if (force<2) {
		if (!(outfp=fopen("tempfile","w"))) {
			if (force) fprintf(stderr,"NUTS: Couldn't open tempfile.\n");
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
				if (force==2) rm->mesg_cnt++;
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
	if (force<2) {
		fclose(outfp);
		unlink(filename);
		if (!write_rest) unlink("tempfile");
		else rename("tempfile",filename);
		}
	if (rm->mesg_cnt!=tmp) bad_cnt++;
	}
switch(force) {
	case 0:
	if (bad_cnt) 
		sprintf(text,"CHECK_MESSAGES: %d files checked, %d had an incorrect message count, %d messages deleted.\n",board_cnt,bad_cnt,old_cnt);
	else sprintf(text,"CHECK_MESSAGES: %d files checked, %d messages deleted.\n",board_cnt,old_cnt);
	write_syslog(text,1,SYSLOG);
	break;

	case 1:
	printf("  %d board files checked, %d out of date messages found.\n",board_cnt,old_cnt);
	break;

	case 2:
	sprintf(text,"%d board files checked, %d had an incorrect message count.\n",board_cnt,bad_cnt);
	write_user(user,text);
	sprintf(text,"%s forced a recount of the message boards.\n",user->name);
	write_syslog(text,1,SYSLOG);
	}
}
/**************************** Made in England *******************************/

/*** Anything that follows was added by Andy, also some stuff in the main code ***/

/***  USER LISTING FUNCTIONS ***/

/* adds a name to the userlist */
add_userlist(name)
char *name;
{
FILE *fp;
char filename[80];

sprintf(filename,"%s/%s",USERFILES,USERLIST);
if (fp=fopen(filename,"a")) {
	fprintf(fp,"%s\n",name);
	fclose(fp);
	}
}

/* takes a name out of the userlist file */
clean_userlist(name)
char *name;
{
char filename[80], check[USER_NAME_LEN+1];
FILE *fpi,*fpo;

sprintf(filename,"%s/%s",USERFILES,USERLIST);
if (!(fpi=fopen(filename,"r"))) return;
if (!(fpo=fopen("templist","w"))) { fclose(fpi);  return; }

name[0]=toupper(name[0]);
fscanf(fpi,"%s",check);
while(!(feof(fpi))) {
	check[0]=toupper(check[0]);
	if (strcmp(name,check)) fprintf(fpo,"%s\n",check);
	fscanf(fpi,"%s",check);
	}
fclose(fpi);  fclose(fpo);
unlink(filename);
rename("templist",filename);
}


/* checks a name to see if it's in the userlist - incase of a bug, or userlst
   gets buggered up somehow */
in_userlist(name)
char *name;
{
char filename[80], check[USER_NAME_LEN+1];
FILE *fp;

sprintf(filename,"%s/%s",USERFILES,USERLIST);
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


/* adds a name to the level list given */
add_level_list(name,level)
char *name;
int level;
{
char filename[80];
FILE *fp;
char *long_date();

switch(level) {
   case JAILED: sprintf(filename,"%s/%s",USERFILES,JAILED_LIST); break; 
   case NEW   : sprintf(filename,"%s/%s",USERFILES,NEW_LIST);    break; 
   case USER  : sprintf(filename,"%s/%s",USERFILES,USER_LIST);   break;   
   case SUPER : sprintf(filename,"%s/%s",USERFILES,SUPER_LIST);   break;
   case WIZ   : sprintf(filename,"%s/%s",USERFILES,WIZ_LIST);    break; 
   case ARCH  : sprintf(filename,"%s/%s",USERFILES,ARCH_LIST);   break;  
   case GOD   : sprintf(filename,"%s/%s",USERFILES,GOD_LIST);    break;  
   default : return;
   }
name[0]=toupper(name[0]);
if (fp=fopen(filename,"a")) {
	fprintf(fp,"%-*s : %s\n",USER_NAME_LEN,name,long_date(1));
	fclose(fp);
	}
/* could cause an error in count if in above switch */
switch(level) {
   case JAILED: jailed_cnt++; break;
   case NEW   : new_cnt++;    break; 
   case USER  : user_cnt++;   break;   
   case SUPER : super_cnt++;  break;
   case WIZ   : wiz_cnt++;    break; 
   case ARCH  : arch_cnt++;   break;  
   case GOD   : god_cnt++;    break;  
   }
}

/* takes a name out of the userlist file */
clean_level_list(name,level)
char *name;
int level;
{
char filename[80], line[82], check[USER_NAME_LEN];
FILE *fpi,*fpo;
int cnt=0;

switch(level) {
   case JAILED: sprintf(filename,"%s/%s",USERFILES,JAILED_LIST); break;
   case NEW   : sprintf(filename,"%s/%s",USERFILES,NEW_LIST);    break; 
   case USER  : sprintf(filename,"%s/%s",USERFILES,USER_LIST);   break;   
   case SUPER : sprintf(filename,"%s/%s",USERFILES,SUPER_LIST);   break;
   case WIZ   : sprintf(filename,"%s/%s",USERFILES,WIZ_LIST);    break; 
   case ARCH  : sprintf(filename,"%s/%s",USERFILES,ARCH_LIST);   break;  
   case GOD   : sprintf(filename,"%s/%s",USERFILES,GOD_LIST);    break;  
   default : return;
   }
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
/* could cause an error in count if in above switch */
switch(level) {
   case JAILED: jailed_cnt--; break;
   case NEW   : new_cnt--;    break; 
   case USER  : user_cnt--;   break;
   case SUPER : super_cnt--;  break;
   case WIZ   : wiz_cnt--;    break; 
   case ARCH  : arch_cnt--;   break;  
   case GOD   : god_cnt--;    break;  
   }
}


/** count number of users listed in USERLIST for a global count **/
count_users()
{
int cnt,level,c;
char filename[80],name[USER_NAME_LEN+1];
FILE *fp;

cnt=0;
/* Do full user count from userlist */
sprintf(filename,"%s/%s",USERFILES,USERLIST);
if (!(fp=fopen(filename,"r"))) {
	user_number=0;  return;
	}
fscanf(fp,"%s",name);
while (!feof(fp)) {
	cnt++;
	fscanf(fp,"%s",name);
	}
fclose(fp);
user_number=cnt;
/* count number of people per level.  could, I suppose, just add these up
   to get overall user number... but what the hell?! ;) 
   */
for (level=JAILED;level<=GOD;++level) {
  switch(level) {
      case JAILED: sprintf(filename,"%s/%s",USERFILES,JAILED_LIST); break; 
      case NEW   : sprintf(filename,"%s/%s",USERFILES,NEW_LIST);    break; 
      case USER  : sprintf(filename,"%s/%s",USERFILES,USER_LIST);   break;   
      case SUPER : sprintf(filename,"%s/%s",USERFILES,SUPER_LIST);  break;
      case WIZ   : sprintf(filename,"%s/%s",USERFILES,WIZ_LIST);    break; 
      case ARCH  : sprintf(filename,"%s/%s",USERFILES,ARCH_LIST);   break;  
      case GOD   : sprintf(filename,"%s/%s",USERFILES,GOD_LIST);    break;  
      }
  if (!(fp=fopen(filename,"r"))) {
    switch(level) {
        case JAILED: jailed_cnt=0; break;
        case NEW   : new_cnt=0;    break; 
	case USER  : user_cnt=0;   break;
	case SUPER : super_cnt=0;  break;
	case WIZ   : wiz_cnt=0;    break; 
	case ARCH  : arch_cnt=0;   break;  
	case GOD   : god_cnt=0;    break;  
	}
    continue;
    }
    while((c=getc(fp))!=EOF) 
        if (c=='\n')
	    switch(level) {
                case JAILED: ++jailed_cnt; break;
	        case NEW   : ++new_cnt;    break; 
		case USER  : ++user_cnt;   break;
		case SUPER : ++super_cnt;  break;   
		case WIZ   : ++wiz_cnt;    break; 
		case ARCH  : ++arch_cnt;   break;  
		case GOD   : ++god_cnt;    break;  
		}
    fclose(fp);
    }
}

/* wipes ALL the files belonging to the user with name given */
clean_files(name)
char *name;
{
char filename[80];

name[0]=toupper(name[0]);
sprintf(filename,"%s/%s.D",USERFILES,name);
unlink(filename);
sprintf(filename,"%s/%s.M",USERFILES,name);
unlink(filename);
sprintf(filename,"%s/%s.P",USERFILES,name);
unlink(filename);
sprintf(filename,"%s/%s.H",USERFILES,name);
unlink(filename);
sprintf(filename,"%s/%s.MAC",USERFILES,name);
unlink(filename);
}


/* adds a string to the user's history list */
add_history(name,time,str)
char *name,*str;
int time;
{
FILE *fp;
char filename[80];

name[0]=toupper(name[0]);
/* first check user exists */
sprintf(filename,"%s/%s.D",USERFILES,name);
if (!(fp=fopen(filename,"r"))) return;
fclose(fp);
/* add to actual history listing */
sprintf(filename,"%s/%s.H",USERFILES,name);
if (fp=fopen(filename,"a")) {
    if (!time) fprintf(fp,"%s",str);
    else fprintf(fp,"%02d/%02d %02d:%02d: %s",tmday,tmonth+1,thour,tmin,str);
    fclose(fp);
    }
}

/* allows a user to add to another users history list */
manual_history(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
char filename[80];
FILE *fp;

if (word_count<3) {
  write_user(user,"Usage: addhistory <user> <text>\n");
  return;
  }
word[1][0]=toupper(word[1][0]);
if (!strcmp(user->name,word[1])) {
  write_user(user,"You cannot add to your own history list.\n");
  return;
  } 
sprintf(filename,"%s/%s.D",USERFILES,word[1]);
if (!(fp=fopen(filename,"r"))) {
  write_user(user,nosuchuser);  return;
  }
fclose(fp);
inpstr=remove_first(inpstr);
sprintf(text,"(%s adds) %s\n",user->name,inpstr);
add_history(word[1],0,text);
sprintf(text,"You have added to %s's history list.\n",word[1]);
write_user(user,text);
}


/* Purge users that haven't been on for the expire length of time */
purge(manual)
int manual;
{
char filename[80],name[USER_NAME_LEN],*c;
FILE *fpi,*fpo;
UR_OBJECT u;
int i;

/* don't do anything if not initiated by user and auto_purge isn't on */
if (!manual && !auto_purge) {
    sprintf(text,"PURGE: Auto-purge is turned off.\n");
    write_syslog(text,0,SYSLOG);
    return 0; 
    }
sprintf(filename,"%s/%s",USERFILES,USERLIST);
if (!(fpi=fopen(filename,"r"))) {
    write_syslog("ERROR: Unable to open userlist in purge().\n",0,SYSLOG);
    return;
    }
if (!(fpo=fopen("templist","w"))) {
    write_syslog("ERROR: Unable to open temporary file in purge().\n",0,SYSLOG);
    fclose(fpi);
    return;
    }
/* Copy names from the userlist to a temporary file to read from */
fscanf(fpi,"%s",name);
while (!feof(fpi)) {
    fprintf(fpo,"%s\n",name);
    fscanf(fpi,"%s",name);
    }
fclose(fpi);
fclose(fpo);

if (!(fpi=fopen("templist","r"))) {
    write_syslog("ERROR: Unable to open temporary user list in purge().\n",0,SYSLOG);
    return;
    }
purge_count=users_purged=0;
fscanf(fpi,"%s",name);
while (!feof(fpi)) {
    if ((u=create_user())==NULL) {
        write_syslog("ERROR: Unable to create temporary user object in purge().\n",0,SYSLOG);
        goto PURGE_SKIP;
        }
    strcpy(u->name,name);
    if (!load_user_details(u)) {
	clean_userlist(u->name); /* get rid of name from userlist */
	clean_files(u->name); /* just incase there are any odd files around */
	for (i=JAILED;i<=GOD;i++) clean_level_list(u->name,i); /* just incase name still in level lists */
	destruct_user(u);
	destructed=0;
	goto PURGE_SKIP;
	}
    purge_count++;
    if (u->expire && time(0)>u->t_expire) {
        clean_userlist(u->name);
        clean_files(u->name);
        clean_level_list(u->name,u->level);
	sprintf(text,"PURGE: removed user '%s'\n",u->name);
	write_syslog(text,0,SYSLOG);
	users_purged++;
	destruct_user(u);
	destructed=0;
	goto PURGE_SKIP;
        }
    destruct_user(u);
    destructed=0;
  PURGE_SKIP:
    fscanf(fpi,"%s",name);
    }
fclose(fpi);
unlink("templist");
sprintf(text,"PURGE: Checked %d users, %d were deleted due to lack of use\n",purge_count,users_purged);
write_syslog(text,0,SYSLOG);
user_number=purge_count-users_purged;
purge_date=time(0)+(USER_EXPIRES*86400);
}

/*** END OF USER LIST FUNCTIONS ***/

/* allows the user to call the purge function */
purge_users(user)
UR_OBJECT user;
{
purge(1);
sprintf(text,"Checked %d users, %d were deleted due to lack of use.  User count is now %d.\n",purge_count,users_purged,user_number);
write_user(user,text);
}

/* Set a user to either expire after a set time, or never expire */
user_expires(user)
UR_OBJECT user;
{
UR_OBJECT u;

    if (word_count<2) {
        write_user(user,"Usage: expire <user>\n");
        return;
        }
    /* user logged on */
    if (u=get_user(word[1])) {
        if (!u->expire) {
            u->expire=1;
            sprintf(text,"You have set it so %s will expire after %d day(s) of no use.\n",u->name,USER_EXPIRES);
            write_user(user,text);
	    sprintf(text,"%s enables expiration with purge.\n",user->name);
	    add_history(u->name,0,text);
	    sprintf(text,"%s enabled expiration on %s.\n",user->name,u->name);
	    write_syslog(text,1,SYSLOG);
            return;
            }
        u->expire=0;
        sprintf(text,"You have set it so %s will no longer expire after %d day(s) of no use.\n",u->name,USER_EXPIRES);
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
        sprintf(text,"You have set it so %s will expire after %d day(s) of no use.\n",u->name,USER_EXPIRES);
        write_user(user,text);
	sprintf(text,"%s enables expiration with purge.\n",user->name);
	add_history(u->name,0,text);
	sprintf(text,"%s enabled expiration on %s.\n",user->name,u->name);
	write_syslog(text,1,SYSLOG);
        save_user_details(u,0); destruct_user(u); destructed=0; return;
        }
    u->expire=0;
    sprintf(text,"You have set it so %s will no longer expire after %d day(s) of no use.\n",u->name,USER_EXPIRES);
    write_user(user,text);
    sprintf(text,"%s disables expiration with purge.\n",user->name);
    add_history(u->name,0,text);
    sprintf(text,"%s disabled expiration on %s.\n",user->name,u->name);
    write_syslog(text,1,SYSLOG);
    save_user_details(u,0); destruct_user(u); destructed=0; return;
}


/* Put commands in an ordered list for viewing with .help */
command_order()
{
int i,j,cnt,total;
struct { char name[30]; int level; } temp;

cnt=total=0;
while(command[cnt][0]!='*') {
    strcpy(ordcom[cnt].name,command[cnt]);
    ordcom[cnt].level=com_level[cnt];
    ++cnt;
    }
for (i=0; i<cnt-1; ++i) {
    for (j=i+1; j<cnt; ++j) {
        if (strcmp(ordcom[i].name,ordcom[j].name) > 0) {
            strcpy(temp.name,ordcom[i].name);
            temp.level=ordcom[i].level;
            strcpy(ordcom[i].name,ordcom[j].name);
            ordcom[i].level=ordcom[j].level;
            strcpy(ordcom[j].name,temp.name);
            ordcom[j].level=temp.level;
            }
        }
    }
/* stopping condition */
strcpy(ordcom[j].name,"*");
return;
}


/*** View the system log ***/
viewlog(user)
UR_OBJECT user;
{
FILE *fp;
char c,*emp="This log file is empty.\n\n",*logfile,where[80];
int lines,cnt,cnt2,type,level,i;

if (word_count<2) {
  write_user(user,"Usage: viewlog sys/net/req/<level name> [<lines from end>]\n");
  return;
  }
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
  else if (level>=0) {
    switch(level) {
      case JAILED: sprintf(where,"%s/%s",USERFILES,JAILED_LIST);  break;
      case NEW: sprintf(where,"%s/%s",USERFILES,NEW_LIST);  break;
      case USER: sprintf(where,"%s/%s",USERFILES,USER_LIST);  break;
      case SUPER : sprintf(where,"%s/%s",USERFILES,SUPER_LIST);   break;
      case WIZ: sprintf(where,"%s/%s",USERFILES,WIZ_LIST);  break;
      case ARCH: sprintf(where,"%s/%s",USERFILES,ARCH_LIST);  break;
      case GOD: sprintf(where,"%s/%s",USERFILES,GOD_LIST);  break;
      }
      logfile=where;
      sprintf(text,"\n~BB~FG*** User list for level '%s' ***\n\n",level_name[level]);
      write_user(user,text);
    }
  else {
    write_user(user,"Usage: viewlog sys/net/req/<level name> [<lines from end>]\n");
    return;
    }
  switch(more(user,user->socket,logfile)) {
	case 0: write_user(user,emp);  return;
	case 1: user->misc_op=2; 
	}
  return;
  }
if ((lines=atoi(word[2]))<1) {
	write_user(user,"Usage: viewlog sys/net/req/<level name> [<lines from the end>]\n");  return;
	}
/* find out which log */
if (!strcmp(word[1],"SYS")) { logfile=MAINSYSLOG; type=SYSLOG; }
if (!strcmp(word[1],"NET")) { logfile=NETSYSLOG;  type=NETLOG; }
if (!strcmp(word[1],"REQ")) { logfile=REQSYSLOG;  type=REQLOG; }
if (level>=0) {
    switch(level) {
      case JAILED: sprintf(where,"%s/%s",USERFILES,JAILED_LIST);  break;
      case NEW: sprintf(where,"%s/%s",USERFILES,NEW_LIST);  break;
      case USER: sprintf(where,"%s/%s",USERFILES,USER_LIST);  break;
      case SUPER : sprintf(where,"%s/%s",USERFILES,SUPER_LIST);   break;
      case WIZ: sprintf(where,"%s/%s",USERFILES,WIZ_LIST);  break;
      case ARCH: sprintf(where,"%s/%s",USERFILES,ARCH_LIST);  break;
      case GOD: sprintf(where,"%s/%s",USERFILES,GOD_LIST);  break;
      }
      logfile=where;
      type=-1;  /* so it doesn't bugger with any log files added later */
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
	sprintf(text,"There are only %d lines in the log.\n",cnt);
	write_user(user,text);
	fclose(fp);
	return;
	}
if (cnt==lines) {
        switch(type) {
	    case SYSLOG:  write_user(user,"\n~BB~FG*** System log ***\n\n");  break;
            case NETLOG:  write_user(user,"\n~BB~FG*** Netlink log ***\n\n");  break;
            case REQLOG:  write_user(user,"\n~BB~FG*** Account Request log ***\n\n");  break;
	    case -1: sprintf(text,"\n~BB~FG*** User list for level '%s' ***\n\n",level_name[level]);
	             write_user(user,text);
	             break;
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
            case SYSLOG: sprintf(text,"\n~BB~FG*** System log (last %d lines) ***\n\n",lines);  break;
            case NETLOG: sprintf(text,"\n~BB~FG*** Netlink log (last %d lines) ***\n\n",lines);  break;
            case REQLOG: sprintf(text,"\n~BB~FG*** Account Request log (last %d lines) ***\n\n",lines);  break;
            case -1: sprintf(text,"\n~BB~FG*** User list for level '%s' (last %d lines) ***\n\n",level_name[level],lines);
                     break;
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


/** Show command, ie 'Type --> <command>' **/
show(user,inpstr)
UR_OBJECT user;
char *inpstr;
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
show_ranks(user)
UR_OBJECT user;
{
int i,total,cnt[GOD+1];
char null[1],*ptr;
char *cl="~FT~OL";

for (i=JAILED;i<=GOD;i++) cnt[i]=0;
total=i=0;
while (command[i][0]!='*') {
	cnt[com_level[i]]++;
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
wiz_list(user)
UR_OBJECT user;
{
UR_OBJECT u;
int some_on=0,count=0,cnt,i;
char text2[ARR_SIZE], filename[80], line[82], name[USER_NAME_LEN], temp[USER_NAME_LEN+10];
char *spacer=" ",*clrs[]={"~FT","~FM","~FG","~FB","~OL","~FR","~FY"};
FILE *fp;

write_user(user,"+----- ~FGWiz List~RS -------------------------------------------------------------+\n\n");
for (i=GOD;i>=WIZ;i--) {
  text2[0]='\0';
  sprintf(text,"~OL%s%-10s~RS : ",clrs[i%4],level_name[i]);
	switch(i) {
		case WIZ  : sprintf(filename,"%s/%s",USERFILES,WIZ_LIST);  break;
		case ARCH : sprintf(filename,"%s/%s",USERFILES,ARCH_LIST); break;
		case GOD  : sprintf(filename,"%s/%s",USERFILES,GOD_LIST);  break;
		}
	if (!(fp=fopen(filename,"r"))) sprintf(text2,"~FR[none listed]~RS");
	else {
		cnt=0;  text2[0]='\0';
		fgets(line,82,fp);
		sscanf(line,"%s",name);
		while(!(feof(fp))) {
			/* using strcat to build up the string of names */
			if (cnt>3) { strcat(text2,"\n             ");  cnt=0; }
			sprintf(temp,"%s%-*s~RS  ",clrs[rand()%7],USER_NAME_LEN,name);
			strcat(text2,temp);
			cnt++;
			fgets(line,82,fp);
			sscanf(line,"%s",name);
			}
		fclose(fp);
		}
	strcat(text,text2);
	write_user(user,text);
	write_user(user,"\n");
	}
write_user(user,"\n+----- ~FGThose currently on~RS ---------------------------------------------------+\n\n");
for (u=user_first;u!=NULL;u=u->next)
if (u->room!=NULL)  {
	if (u->level>=WIZ) {
	        if (!u->vis && (user->level<u->level && !(user->level>=ARCH)))  { ++count;  continue;  }
		else {
			if (u->vis) sprintf(text2,"  %s %s~RS",u->name,u->desc);
			else sprintf(text2,"* %s %s~RS",u->name,u->desc);
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

/* Get current system time */
get_time(user)
UR_OBJECT user;
{
char bstr[40],localtm[40],text2[ARR_SIZE];
int secs,mins,hours,days;

/* Get some values */
strcpy(bstr,ctime(&boot_time));
secs=(int)(time(0)-boot_time);
days=secs/86400;
hours=(secs%86400)/3600;
mins=(secs%3600)/60;
secs=secs%60;
sprintf(text,"\nSystem booted              : ~OL%s~RS~FT~BK",bstr);
write_user(user,text);
sprintf(text,"Uptime                     : ~OL%d days, %d hours, %d minutes, %d seconds~RS~FT~BK\n",days,hours,mins,secs);
write_user(user,text);
sprintf(text,"The current system time is : ~OL%s, %d %s, %02d:%02d:%02d %d~RS~FT~BK\n\n",day[twday],tmday,month[tmonth],thour,tmin,tsec,tyear);
write_user(user,text);
}

/* Show version number and some small stats of the talker */
show_version(user)
UR_OBJECT user;
{
int rms,i,cnt;
RM_OBJECT rm;

sprintf(text,"\n~OLAmnuts~RS version %s, which is (modified) NUTS version %s\n\n",AMNUTSVER,NUTSVER);
write_user(user,text);
if (user->level>=WIZ) {
  for (i=JAILED;i<=GOD;i++) {
    switch(i) {
      case JAILED: cnt=jailed_cnt; break;
      case NEW   : cnt=new_cnt;    break;
      case USER  : cnt=user_cnt;   break;
      case SUPER : cnt=super_cnt;  break;
      case WIZ   : cnt=wiz_cnt;    break;
      case ARCH  : cnt=arch_cnt;   break;
      case GOD   : cnt=god_cnt;    break;
      }
    sprintf(text,"Number of users at level %-6s : ~OL%d~RS\n",level_name[i],cnt);
    write_user(user,text);
    }
  }
sprintf(text,"Total number of users    : ~OL%d~RS\n",user_number);
write_user(user,text);
sprintf(text,"Maximum online users     : ~OL%d~RS\n",max_users);
write_user(user,text);
sprintf(text,"Logons this current boot : ~OL%d~RS new users, ~OL%d~RS old users~RS\n",logons_new,logons_old);
write_user(user,text);
rms=0;  for(rm=room_first;rm!=NULL;rm=rm->next) ++rms;
sprintf(text,"Total number of rooms    : ~OL%d~RS\n",rms);
write_user(user,text);
sprintf(text,"Swear ban currently on   : ~OL%s~RS\n",noyes2[ban_swearing]);
write_user(user,text);
sprintf(text,"Smail auto-forwarding on : ~OL%s~RS\n",noyes2[forwarding]);
write_user(user,text);
sprintf(text,"Auto purge on            : ~OL%s~RS\n",noyes2[auto_purge]);
write_user(user,text);
sprintf(text,"Maximum user idle time   : ~OL%d~RS minutes~RS\n",user_idle_time/60);
write_user(user,text);
sprintf(text,"Maximum smail copies     : ~OL%d~RS\n",MAX_COPIES);
write_user(user,text);
write_user(user,"\nModified by Andy.\n\n");
}


/* clears a room topic */
clear_topic(user)
UR_OBJECT user;
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
	if (user->level>com_level[CTOPIC] || user->level>=ARCH) {
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


/* Check the room you're logging into isn't private */
check_start_room(user)
UR_OBJECT user;
{
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


/* get users which to send copies of smail to */
copies_to(user)
UR_OBJECT user;
{
FILE *fp;
int remote,i=0,docopy,found,cnt;
char *c,filename[80];

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
        sprintf(filename,"%s/%s.D",USERFILES,word[i]);
        if (!(fp=fopen(filename,"r"))) {
            sprintf(text,"There is no such user with the name '%s' to copy to.\n",word[i]);
            write_user(user,text);
            docopy=0;
            }
        else docopy=1;
        }
    fclose(fp);
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
send_copies(user,ptr)
UR_OBJECT user;
char *ptr;
{
int sent, i, found=0;

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



/* Set the user attributes */
set_attributes(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int i=0;
char text2[ARR_SIZE],*colour_com_strip();

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
                }
            return;
            }
        write_user(user,"Usage: set gender m/f/n\n");
        return;
    case SETAGE:
        if (word_count<3) {
            write_user(user,"Usage: set age <age>\n");
            return;
            }
        user->age=atoi(word[2]);
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
        else strcpy(user->email,inpstr);
		if (!strcmp(user->email,"#UNSET")) sprintf(text,"Email set to : ~FRunset\n");
        else sprintf(text,"Email set to : ~FT%s\n",user->email);
        write_user(user,text);
        set_forward_email(user);
        return;
    case SETHOMEP:
        inpstr=remove_first(inpstr);
		inpstr=colour_com_strip(inpstr);
		if (!inpstr[0]) strcpy(user->homepage,"#UNSET");
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
    }
}


show_attributes(user)
UR_OBJECT user;
{
char *onoff[]={"Off","On"};
char *shide[]={"Showing","Hidden"};
char *rm[]={"Main room","Last room in"};
int i=0, found=0;
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
        }
    i++;
    }
write_user(user,"\n"); return;
}


/** Tell something to everyone but one person **/
mutter(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u,user2;
char name[USER_NAME_LEN];

if (word_count<3) {
   write_user(user,"Usage: mutter <name> <message>\n");
   return;
   }
if (!(user2=get_user_name(user,word[1]))) {
      write_user(user,notloggedon); return;
      }
inpstr=remove_first(inpstr);
if (user->room!=user2->room) {
      sprintf(text,"%s is not in the same room, so speak freely of them.\n",user2->name);
      write_user(user,text); return;
      }
if (user2==user) {
   write_user(user,"Talking about yourself is a sign of madness!\n");
   return;
   }
for (u=user_first;u!=NULL;u=u->next) {
	if (u->room==NULL
	    || u->ignall
	    || (u->room!=user->room && user->room!=NULL)
	    || u==user2) continue;
    if (!user->vis && u->level<user->level)
        sprintf(text,"%s mutters: %s (to all but %s)\n",invisname,inpstr, user2->name);
    else sprintf(text,"%s mutters: %s (to all but %s)\n",user->name,inpstr,user2->name);
    write_user(u,text);
    }
}

/** Force a user to become invisible **/
make_invis(user)
UR_OBJECT user;
{
char name[USER_NAME_LEN];
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
   sprintf(text,"%s is already invisible.\n",user2->name);
   write_user(user,text);  return;
   }
if (user2->level>user->level) {
   sprintf(text,"%s cannot be forced invisible.\n",user2->name);
   write_user(user,text);  return;
   }
user2->vis=0;
sprintf(text,"You force %s to become invisible.\n",user2->name);
write_user(user,text);
write_user(user2,"You have been forced to become invisible.\n");
sprintf(text,"You see %s mysteriously disappear into the shadows!\n",user2->name);
write_room_except(user2->room,text,user2);
}

/** Force a user to become visible **/
make_vis(user)
UR_OBJECT user;
{
char name[USER_NAME_LEN];
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
   sprintf(text,"%s is already visible.\n",user2->name);
   write_user(user,text);  return;
   }
if (user2->level>user->level) {
   sprintf(text,"%s cannot be forced visible.\n",user2->name);
   write_user(user,text);  return;
   }
user2->vis=1;
sprintf(text,"You force %s to become visible.\n",user2->name);
write_user(user,text);
write_user(user2,"You have been forced to become visible.\n");
sprintf(text,"You see %s mysteriously emerge from the shadows!\n",user2->name);
write_room_except(user2->room,text,user2);
}


/** ask all the law, (sos), no muzzle restrictions **/
plead(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int found=0;
UR_OBJECT u;
char *err="Sorry, but there are no wizzes currently logged on.\n";

if (word_count<2) {
    write_user(user,"Usage: plead <message>\n");
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
sprintf(text,"~OL[ %s pleads ]~RS %s\n",user->name,inpstr);
write_level(WIZ,1,text,NULL);
sprintf(text,"~OLYou plead with the wizzes:~RS %s\n",inpstr);
write_user(user,text);
record_tell(user,text);
}



/* Displays a picture to a person */
picture_tell(user)
UR_OBJECT user;
{
UR_OBJECT u;
char filename[80],*name,*c;
FILE *fp;

if (word_count<3) {
    write_user(user,"Usage: ptell <user> <picture name>\n");
    return;
    }
if (user->muzzled) {
    write_user(user,"You are muzzled, you cannot tell anyone anything.\n");  return;
    }
if (!(u=get_user_name(user,word[1]))) {
   write_user(user,notloggedon);  return;
   }
if (u==user) {
   write_user(user,"There is an easier way to see pictures...\n");
   return;
   }
if (u->ignall && (user->level<WIZ || u->level>user->level)) {
    if (u->malloc_start!=NULL)
        sprintf(text,"%s is writing a message at the moment.\n",u->name);
    else sprintf(text,"%s is not listening at the moment.\n",u->name);
    write_user(user,text);
    return;
    }
if (u->room==NULL) {
    sprintf(text,"%s is offsite and would not be able to reply to you.\n",u->name);
    write_user(user,text);
    return;
    }
if (u->ignpics) {
    sprintf(text,"%s is ignoring pictures at the moment.\n",u->name);
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
    fclose(fp); return;
    }
fclose(fp);
if (!user->vis && u->level<user->level) name=invisname; else name=user->name;
sprintf(text,"~FG~OL%s shows you the following picture...\n\n",name);
write_user(u,text);
switch(more(u,u->socket,filename)) {
    case 0: break;
    case 1: u->misc_op=2;
    }
sprintf(text,"~FG~OLYou show the following picture to %s\n\n",u->name);
write_user(user,text);
switch(more(user,user->socket,filename)) {
    case 0: break;
    case 1: user->misc_op=2;
    }
}

/* see list of pictures availiable - file dictated in 'go' script */
preview(user)
UR_OBJECT user;
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
    fclose(fp); return;
    }
fclose(fp);
write_user(user,"~FG~OLYou preview the following picture...\n\n");
switch(more(user,user->socket,filename)) {
    case 0: break;
    case 1: user->misc_op=2;
    }
}

picture_all(user)
UR_OBJECT user;
{
UR_OBJECT u;
char filename[80],*name,*c;
FILE *fp;

if (word_count<2) {
    preview(user); 
    write_user(user,"\nUsage: picture <name>\n");  return;
    }
if (user->muzzled) {
    write_user(user,"You are muzzled, you cannot tell anyone anything.\n");  return;
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
    fclose(fp); return;
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
    if (!user->vis && u->level<=user->level) name=invisname;  else  name=user->name;
    if (u->type==CLONE_TYPE) {
        if (u->clone_hear==CLONE_HEAR_NOTHING || u->owner->ignall
            || u->clone_hear==CLONE_HEAR_SWEARS) continue;
        /* Ignore anything not in clones room, eg shouts, system messages
           and shemotes since the clones owner will hear them anyway. */
        if (user->room!=u->room) continue;
        sprintf(text,"~BG~FK[ %s ]:~RS~FG~OL %s shows the following picture...\n\n",u->room,name);
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
        }
    }
write_user(user,"~FG~OLThe following picture was sent to the room.\n\n");
switch(more(user,user->socket,filename)) {
    case 0: break;
    case 1: user->misc_op=2;
    }
}

/*** print out greeting in large letters ***/
greet(user,inpstr)
UR_OBJECT user;
char *inpstr;
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
    write_user(user,noswearing);
    return;
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
think_it(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
    if (!user->vis) write_monitor(user,user->room,0);
    if (word_count<2 && !user->vis) sprintf(text,"%s thinks nothing - now that's just typical!\n",invisname);
    else if (word_count<2 && user->vis) sprintf(text,"%s thinks nothing - now that's just typical!\n",user->name);
    else if (!user->vis) sprintf(text,"%s thinks . o O ( %s )\n",invisname,inpstr);
    else sprintf(text,"%s thinks . o O ( %s )\n",user->name,inpstr);
    write_room(user->room,text);
    record(user->room,text);
}

/** put speech in a music notes **/
sing_it(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
    if (!user->vis) write_monitor(user,user->room,0);
    if (word_count<2 && !user->vis) sprintf(text,"%s sings a tune... BADLY!\n",invisname);
    else if (word_count<2 && user->vis) sprintf(text,"%s sings a tune... BADLY!\n",user->name);
    else if (!user->vis) sprintf(text,"%s sings o/~ %s o/~\n",invisname,inpstr);
    else sprintf(text,"%s sings o/~ %s o/~\n",user->name,inpstr);
    write_room(user->room,text);
    record(user->room,text);
}

wizemote(user,inpstr)
UR_OBJECT user;
char *inpstr;
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
	write_user(user,noswearing);  return;
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
	sprintf(text,"~FY<WIZ: ~OL[%s]~RS~FY=>~RS %s %s\n",level_name[lev],user->name,inpstr);
	write_level(lev,1,text,NULL);
	return;
	}
sprintf(text,"~FY<WIZ: =>~RS %s %s\n",user->name,inpstr);
write_user(user,text);
record_tell(user,text);
sprintf(text,"~FY<WIZ: =>~RS %s %s\n",user->name,inpstr);
write_level(WIZ,1,text,user);
}


/** Write a suggestion to the board, or read if if you can **/
suggestions(user,done_editing)
UR_OBJECT user;
int done_editing;
{
FILE *fp;
char filename[30],name[USER_NAME_LEN], *c;
int sugarea,cnt=0;

if (com_num==RSUG) {
    sprintf(filename,"%s/%s",MISCFILES,SUGBOARD);
    write_user(user,"~BB~FG*** The Suggestions board has the following ideas ***\n\n");
    switch(more(user,user->socket,filename)) {
        case 0: write_user(user,"There are no suggestions.\n\n");  break;
        case 1: user->misc_op=2;
        }
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
sprintf(text,"From: ~OL%s~RS, %d %s\n",user->name,tmday,month[tmonth]);
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
sug_num++;
}

/** delete suggestions from the board **/
delete_suggestions(user)
UR_OBJECT user;
{
int num,cnt,valid,i;
char infile[80],line[ARR_SIZE],id[82],*name;
FILE *infp,*outfp;

if (word_count<2) {
    write_user(user,"Usage: dsug all\n");
    write_user(user,"Usage: dsug <#>\n");
    write_user(user,"Usage: dsug to <#>\n");
    write_user(user,"Usage: dsug from <#> to <#>\n");
    return;
   }
sprintf(infile,"%s/%s",MISCFILES,SUGBOARD);
if (get_wipe_parameters(user)==-1) return;
if (user->wipe_from==-1) {
    unlink(infile);
    write_user(user,"All suggestions deleted.\n");
    sprintf(text,"%s wiped all suggestions from the %s board\n",user->name,SUGBOARD);
    write_syslog(text,1,SYSLOG);
    sug_num=0;
    return;
    }
if (user->wipe_from>sug_num) {
    sprintf(text,"There are only %d suggestions on the board.\n",sug_num);
    write_user(user,text);
    return;
    }
if (!(infp=fopen(infile,"r"))) {
    write_user(user,"The suggestions board is empty.\n");
    return;
    }
if (!(outfp=fopen("tempfile","w"))) {
    sprintf(text,"%s: couldn't open tempfile.\n",syserror);
    write_user(user,text);
    write_syslog("ERROR: Couldn't open tempfile in delete_suggestions().\n",0,SYSLOG);
    fclose(infp);
    return;
    }
cnt=0;
for (i=1;i<user->wipe_from;i++) {
	fgets(line,82,infp);
	while(line[0]!='\n') {
		if (feof(infp)) goto SKIP_WIPE;
		fputs(line,outfp);  fgets(line,82,infp);
		}
	fputs(line,outfp);
	}
for (;i<=user->wipe_to;i++) {
	if (i==sug_num) { cnt++; goto SKIP_WIPE; }
	fgets(line,82,infp);
	while(line[0]!='\n') fgets(line,82,infp);
	cnt++;
	}
fgets(line,82,infp);
while(!feof(infp)) {
	fputs(line,outfp);
    if (line[0]=='\n') i++;
	fgets(line,82,infp);
    }
SKIP_WIPE:
fclose(infp);
fclose(outfp);
unlink(infile);
if (user->wipe_from==0 && i<user->wipe_to) {
    unlink("tempfile");
    sprintf(text,"There were only %d suggestions on the board, all now deleted.\n",cnt);
    write_user(user,text);
    sprintf(text,"%s wiped all suggestions from the %s board\n",user->name,SUGBOARD);
    write_syslog(text,1,SYSLOG);
    sug_num=0;
    return;
    }
if ((user->wipe_from==0 && i==user->wipe_to) || cnt==sug_num) {
    unlink("tempfile"); /* cos it'll be empty anyway */
    write_user(user,"All suggestions deleted.\n");
    sug_num=0;
    sprintf(text,"%s wiped all suggestions from the %s board\n",user->name,SUGBOARD);
    }
else {
    rename("tempfile",infile);
    sprintf(text,"%d suggestions deleted.\n",cnt);
    write_user(user,text);
    sug_num-=cnt;
    sprintf(text,"%s wiped %d suggestions from the %s board\n",user->name,cnt,SUGBOARD);
    }
write_syslog(text,1,SYSLOG);
}


/* needs only doing once when booting */
count_suggestions()
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
		++sug_num;
		valid=0;
		}
	fgets(line,82,fp);
	}
fclose(fp);
}


/* delete lines from boards or mail or suggestions, etc */
get_wipe_parameters(user)
UR_OBJECT user;
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


/* Shows when a user was last logged on */
show_last(user)
UR_OBJECT user;
{
UR_OBJECT u;
int timelen,days,hours,mins;

if (word_count<2) {
    write_user(user,"Usage: last <user>\n");
    return;
    }
word[1][0]=toupper(word[1][0]);
if (!strcmp(word[1],user->name)) {
    write_user(user,"You are already logged on!!\n");
    return;
    }
if (u=get_user(word[1])) {
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
timelen=(int)(time(0) - u->last_login);
days=timelen/86400;
hours=(timelen%86400)/3600;
mins=(timelen%3600)/60;
sprintf(text,"\n~FT%s~RS last logged in %s",u->name,ctime((time_t *)&(u->last_login)));
write_user(user,text);
sprintf(text,"Which was %d days, %d hours, %d minutes ago.\n",days,hours,mins);
write_user(user,text);
sprintf(text,"Was on for %d hours, %d minutes.\n\n",u->last_login_len/3600,(u->last_login_len%3600)/60);
write_user(user,text);
destruct_user(u);
destructed=0;
}


/* used to copy out a chuck of text in macros */
midcpy(strf,strt,fr,to)
char *strf,*strt;
int fr,to;
{
int f;
for (f=fr;f<=to;++f) {
   if (!strf[f]) { strt[f-fr]='\0';  return; }
   strt[f-fr]=strf[f];
   }
strt[f-fr]='\0';
}

/* Get user's macros */
get_macros(user)
UR_OBJECT user;
{
char filename[80];
FILE *fp;
int i,l;

if (user->type==REMOTE_TYPE) return;
sprintf(filename,"%s/%s.MAC",USERFILES,user->name);

if (!(fp=fopen(filename,"r"))) return;
for (i=0;i<10;++i) {
   fgets(user->macros[i],MACRO_LEN,fp);
   l=strlen(user->macros[i]);
   user->macros[i][l-1]='\0';
  }
fclose(fp);
}

/*** Display user macros ***/
macros(user)
UR_OBJECT user;
{
int i;

if (user->type==REMOTE_TYPE) {
    write_user(user,"Due to software limitations, remote users cannot have macros.\n");
    return;
    }
write_user(user,"Your current macros:\n");
for (i=0;i<10;++i) {
   sprintf(text,"  ~OL%d)~RS %s\n",i,user->macros[i]);
   write_user(user,text);
   }
}


check_macros(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int i,macnum,lng;
char filename[80],line[ARR_SIZE];
FILE *fp;

if (inpstr[0]=='.' && inpstr[1]>='0' && inpstr[1]<='9') {
    if (user->type==REMOTE_TYPE) {
        write_user(user,"Remote users cannot use macros at this time.\n");
        return;
        }
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
        sprintf(filename,"%s/%s.MAC",USERFILES,user->name);
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


/* no longer invite a user to the room you're in if you invited them */
uninvite(user)
UR_OBJECT user;
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
    sprintf(text,"%s has not been invited anywhere.\n",u->name);
    write_user(user,text);
    return;
    }
if (strcmp(u->invite_by,user->name)) {
    sprintf(text,"%s has not been invited anywhere by you!\n",u->name);
    write_user(user,text);
    return;
    }
sprintf(text,"You cancel your invitation to %s.\n",u->name);
write_user(user,text);
if (user->vis || u->level>=user->level) name=user->name; else name=invisname;
sprintf(text,"%s cancels your invitation.\n",name);
write_user(u,text);
u->invite_room=NULL;
u->invite_by[0]='\0';
}


/*** Send mail message to everyone ***/
level_mail(user,inpstr,done_editing)
UR_OBJECT user;
char *inpstr;
int done_editing;
{
FILE *fp;
int level,i;
char *c,filename[80];

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot mail anyone.\n");  return;
	}
strtoupper(word[1]);
if (done_editing) {
  switch(user->lmail_lev) {
     case -1: for (i=WIZ;i<=GOD;i++) {
              switch(i) {
                 case WIZ:  sprintf(filename,"%s/%s",USERFILES,WIZ_LIST);   break;
		 case ARCH: sprintf(filename,"%s/%s",USERFILES,ARCH_LIST);  break;
		 case GOD:  sprintf(filename,"%s/%s",USERFILES,GOD_LIST);   break;
		 }
	      if (send_broadcast_mail(user,user->malloc_start,filename,-1)) {
		sprintf(text,"You have sent mail to all the %ss.\n",level_name[i]);
		write_user(user,text);
	        }
 	      }
              sprintf(text,"%s sent mail to all the Wizzes.\n",user->name);
              write_syslog(text,1,SYSLOG);
              return;
     case -2: sprintf(filename,"%s/%s",USERFILES,USERLIST);
              if (send_broadcast_mail(user,user->malloc_start,filename,-2))
		write_user(user,"You have sent mail to all the users.\n");
              sprintf(text,"%s sent mail to all the users.\n",user->name);
              write_syslog(text,1,SYSLOG);
              return;
     case JAILED: sprintf(filename,"%s/%s",USERFILES,JAILED_LIST); break;
     case NEW:    sprintf(filename,"%s/%s",USERFILES,NEW_LIST);    break;
     case USER:   sprintf(filename,"%s/%s",USERFILES,USER_LIST);   break;
     case SUPER:  sprintf(filename,"%s/%s",USERFILES,SUPER_LIST);  break;
     case WIZ:    sprintf(filename,"%s/%s",USERFILES,WIZ_LIST);    break;
     case ARCH:   sprintf(filename,"%s/%s",USERFILES,ARCH_LIST);   break;
     case GOD:    sprintf(filename,"%s/%s",USERFILES,GOD_LIST);    break;
     }
  if (send_broadcast_mail(user,user->malloc_start,filename,user->lmail_lev)) {
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
    if (!strcmp(word[1],"ADMIN")) level=-1;
    else if (!strcmp(word[1],"ALL")) level=-2;
    else {
      write_user(user,"Usage: lmail <level name>/admin/all [<message>]\n");
      return;
      }
    }
  strcat(inpstr,"\n"); /* risky but hopefully it'll be ok */
  switch(level) {
    case -1: for (i=WIZ;i<=GOD;i++) {
                 switch(i) {
                     case WIZ:  sprintf(filename,"%s/%s",USERFILES,WIZ_LIST);   break;
		     case ARCH: sprintf(filename,"%s/%s",USERFILES,ARCH_LIST);  break;
		     case GOD:  sprintf(filename,"%s/%s",USERFILES,GOD_LIST);   break;
		     }
	         if (send_broadcast_mail(user,remove_first(inpstr),filename,-1)) {
		   sprintf(text,"You have sent mail to all the %ss.\n",level_name[i]);
		   write_user(user,text);
		   }
	         }
             sprintf(text,"%s sent mail to all the Wizzes.\n",user->name);
             write_syslog(text,1,SYSLOG);
             return;
    case -2: sprintf(filename,"%s/%s",USERFILES,USERLIST);
             if (send_broadcast_mail(user,remove_first(inpstr),filename,-2))
		 write_user(user,"You have sent mail to all the users.\n");
             sprintf(text,"%s sent mail to all the users.\n",user->name);
             write_syslog(text,1,SYSLOG);
             return;
    case JAILED: sprintf(filename,"%s/%s",USERFILES,JAILED_LIST); break;
    case NEW:    sprintf(filename,"%s/%s",USERFILES,NEW_LIST);    break;
    case USER:   sprintf(filename,"%s/%s",USERFILES,USER_LIST);   break;
    case SUPER:  sprintf(filename,"%s/%s",USERFILES,SUPER_LIST);  break;
    case WIZ:    sprintf(filename,"%s/%s",USERFILES,WIZ_LIST);    break;
    case ARCH:   sprintf(filename,"%s/%s",USERFILES,ARCH_LIST);   break;
    case GOD:    sprintf(filename,"%s/%s",USERFILES,GOD_LIST);    break;
    }
  if (send_broadcast_mail(user,remove_first(inpstr),filename,level)) {
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
    if (!strcmp(word[1],"ADMIN")) level=-1;
    else if (!strcmp(word[1],"ALL")) level=-2;
    else {
      write_user(user,"Usage: lmail <level name>/admin/all [<message>]\n");
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
send_broadcast_mail(user,ptr,filename,lvl)
UR_OBJECT user;
char *ptr,*filename;
int lvl;
{
FILE *infp,*outfp,*ulist;
char *c,d,line[DNL+1],name[USER_NAME_LEN+1],line2[ARR_SIZE],*who,header[ARR_SIZE];
int cnt=0;

if (!(ulist=fopen(filename,"r"))) return 0;
fgets(line2,82,ulist);
sscanf(line2,"%s",name);
while (!feof(ulist)) {
	name[0]=toupper(name[0]);
	if (!(outfp=fopen("tempfile","w"))) {
		write_user(user,"Error in mail delivery.\n");
		write_syslog("ERROR: Couldn't open tempfile in send_broadcast_mail().\n",0,SYSLOG);
		fclose(ulist);
		return 0;
		}
	/* Write current time on first line of tempfile */
	fprintf(outfp,"%d\r",(int)time(0));
	sprintf(filename,"%s/%s.M",USERFILES,name);
	if (infp=fopen(filename,"r")) {
		/* Discard first line of mail file. */
		fgets(line,DNL,infp);
		/* Copy rest of file */
		d=getc(infp);
		while(!feof(infp)) {  putc(d,outfp);  d=getc(infp);  }
		fclose(infp);
		}
	header[0]='\0';   who='\0';
	switch(lvl) {
	    case -1: who="(BCLM Wizzes)";  break;
	    case -2: who="(BCLM All users)";  break;
	    default: sprintf(text,"(BCLM %s lvl)",level_name[lvl]);
	             who=text;
	             break;
	    }
	if (user!=NULL) {
		if (user->type==REMOTE_TYPE)
		   sprintf(header,"~OLFrom: %s@%s  %s %s\n",user->name,user->netlink->service,long_date(0),who);
		else sprintf(header,"~OLFrom: %s  %s %s\n",user->name,long_date(0),who);
		}
	else sprintf(header,"~OLFrom: MAILER  %s %s\n",long_date(0),who);
	fprintf(outfp,header);
	fputs(ptr,outfp);
	fputs("\n",outfp);
	fclose(outfp);
	rename("tempfile",filename);
	forward_email(name,header,ptr);
	write_user(get_user(name),"\07~FT~OL~LI*** YOU HAVE NEW MAIL ***\n");
	++cnt;
	fgets(line2,82,ulist);
	sscanf(line2,"%s",name);
	}
fclose(ulist);
return 1;
}


/*** count number of messages in user's mail box***/
mail_count(user)
UR_OBJECT user;
{
FILE *fp;
int valid,cnt=0;
char w1[ARR_SIZE],line[ARR_SIZE],filename[80],*str,*colour_com_strip();

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) return cnt;
valid=1;
fgets(line,DNL,fp);
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


/*** Searches string s1 for string s2 ***/
instr(s1,s2)
char *s1,*s2;
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


/* shows the history file of a given user */
user_history(user)
UR_OBJECT user;
{
FILE *fp;
char filename[80];

if (word_count<2) {
	write_user(user,"Usage: history <user>\n");  return;
	}
word[1][0]=toupper(word[1][0]);
sprintf(filename,"%s/%s.D",USERFILES,word[1]);
if (!(fp=fopen(filename,"r"))) {
	write_user(user,nosuchuser);  return;
	}
fclose(fp);
sprintf(filename,"%s/%s.H",USERFILES,word[1]);
sprintf(text,"~BB~FG*** The history of user ~OL%s~RS~BB~FG is as follows ***\n\n",word[1]);
write_user(user,text);
switch(more(user,user->socket,filename)) {
	case 0: sprintf(text,"%s has no previously recorded history.\n\n",word[1]);
		write_user(user,text);  break;
	case 1: user->misc_op=2;  break;
	}
}


/* allows a user to choose what message to read */
read_specific_mail(user)
UR_OBJECT user;
{
FILE *fp;
int valid,cnt,total,smail_number;
char w1[ARR_SIZE],line[ARR_SIZE],filename[80],*str;

if (word_count>2) {
  write_user(user,"Usage: rmail [message #]\n");
  return;
  }
sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(total=mail_count(user))) {
  write_user(user,"You currently have no mail.\n");
  return;
  }
smail_number=atoi(word[1]);
if (!smail_number) {
  write_user(user,"Usage: rmail [message #]\n");
  return;
  }
if (smail_number>total) {
  sprintf(text,"You only have %d messages in your mailbox.\n",total);
  write_user(user,text);
  return;
  }
sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) {
  write_user(user,"There was an error trying to read your mailbox.\n");
  sprintf(text,"Unable to open %s's mailbox in read_mail_specific.\n",user->name);
  write_syslog(text,0,SYSLOG);
  return;
  }
valid=1;  cnt=1;
fgets(line,DNL,fp); 
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
sprintf(text,"\nMessage number ~FM~OL%d~RS out of ~FM~OL%d~RS.\n\n",smail_number,total);
write_user(user,text);
}


/* Put annoying user in jail */
arrest(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;

if (word_count<2) {
   write_user(user,"Usage: arrest <user>\n");  return;
   }
word[1][0]=toupper(word[1][0]);
if (u=get_user(word[1])) {
   if (u==user) {
      write_user(user,"You cannot arrest yourself.\n");
      return;
      }
   if (u->level>=user->level) {
      write_user(user,"You cannot arrest anyone of the same or higher level than yourself.\n");
      return;
      }
   if (u->level==JAILED) {
      sprintf(text,"%s has already been arrested.\n",u->name);
      write_user(user,text);  return;
      }
   u->vis=1;
   clean_level_list(u->name,u->level);
   u->unarrest=u->level;
   u->level=JAILED;
   add_level_list(u->name,u->level);
   rm=get_room(default_jail);
   write_room(NULL,"The Hand of Justice reaches through the air...\n");
   write_user(u,"You have been placed in jail.\n");
   if (rm==NULL) {
      sprintf(text,"Cannot find the jail, so %s is arrested but still in the %s.\n",u->name,u->room->name);
      write_user(user,text);
      }
   else move_user(u,rm,2);
   sprintf(text,"%s has been placed under arrest...\n",u->name);
   write_room_except(NULL,text,u);
   sprintf(text,"%s ARRESTED %s\n",user->name,u->name);
   write_syslog(text,1,SYSLOG);
   sprintf(text,"Was ~FRarrested~RS by %s.\n",user->name);
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
        sprintf(text,"%s has already been arrested.\n",u->name);
	write_user(user,text);
	destruct_user(u);
	destructed=0;
	return;
	}
u->vis=1;
clean_level_list(u->name,u->level);
u->unarrest=u->level;
u->level=JAILED;
add_level_list(u->name,u->level);
u->socket=-2;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"You place %s under arrest.\n",u->name);
write_user(user,text);
sprintf(text,"~OLYou have been placed under ~FRarrest~RS~OL.\n");
send_mail(user,word[1],text,0);
sprintf(text,"%s ARRESTED %s.\n",user->name,u->name);
write_syslog(text,1,SYSLOG);
sprintf(text,"Was ~FRarrested~RS by %s.\n",user->name);
add_history(u->name,1,text);
destruct_user(u);
destructed=0;
}



/* Unarrest a user who is currently under arrest/in jail */
unarrest(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;

if (word_count<2) {
   write_user(user,"Usage: unarrest <user>\n");  return;
   }
word[1][0]=toupper(word[1][0]);
if (u=get_user(word[1])) {
   if (u==user) {
      write_user(user,"You cannot unarrest yourself.\n");
      return;
      }
   if (u->level!=JAILED) {
      sprintf(text,"%s is not under arrest!\n",u->name);
      write_user(user,text);  return;
      }
   clean_level_list(u->name,u->level);
   u->level=u->unarrest;
   add_level_list(u->name,u->level);
   rm=get_room(default_warp);
   write_room(NULL,"The Hand of Justice reaches through the air...\n");
   write_user(u,"You have been unarrested...  Now try to behave!\n");
   if (rm==NULL) {
      sprintf(text,"Cannot find a room for ex-cons, so %s is still in the %s!\n",u->name,u->room->name);
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
        sprintf(text,"%s is not under arrest!\n",u->name);
	write_user(user,text);
	destruct_user(u);
	destructed=0;
	return;
	}
clean_level_list(u->name,u->level);
u->level=u->unarrest;
add_level_list(u->name,u->level);
u->socket=-2;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"You unarrest %s.\n",u->name);
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


/* verify user's email when it is set specified */
set_forward_email(user)
UR_OBJECT user;
{
FILE *fp;
char cmd[ARR_SIZE],filename[80];

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
fprintf(fp,"From: Amnuts\n");
fprintf(fp,"To: %s <%s>\n",user->name,user->email);
fprintf(fp,"Subject: Verification of auto-mail.\n");
fprintf(fp,"\n");
/* email body */
fprintf(fp,"Hello, %s.\n\n",user->name);
fprintf(fp,"Thank you for setting your email address, and now that you have done so you are able\n");
fprintf(fp,"to use the auto-forwarding function on The Talker to have any smail sent to your\n");
fprintf(fp,"email address.\n");
fprintf(fp,"To be able to do this though you must verify that you have received this email.\n\n");
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
verify_email(user)
UR_OBJECT user;
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


/* send smail to the email ccount */
forward_email(name,from,message)
char *name,*from,*message;
{
FILE *fp;
UR_OBJECT u;
char filename[80],cmd[ARR_SIZE];
int on=0;

if (!forwarding) return;
if (u=get_user(name)) {
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
fprintf(fp,"From: Amnuts\n");
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



/* stop zombie processes */
int send_forward_email(send_to,mail_file)
char *mail_file,*send_to;
{
switch(double_fork()) {
  case -1 : unlink(mail_file); return -1; /* double_fork() failed */
  case  0 : sprintf(text,"mail %s < %s",send_to,mail_file);
            system(text);
            unlink(mail_file);
            _exit(1);
  }
}

/* signal trapping not working, so fork twice */
int double_fork() {
pid_t pid;
int rc, status;

if (!(pid=fork())) {
  switch(fork()) {
      case  0:return 0;
      case -1:_exit(-1);
      default:_exit(0);
      }
  }
if (pid<0||waitpid(pid,&status,0)<0) return -1;
if (WIFEXITED(status))
  if(WEXITSTATUS(status)==0) return 1;
  else errno=WEXITSTATUS(status);
else errno=EINTR;

return -1;
}


/*** Clear the tell buffer of the user ***/
clear_tells(user)
UR_OBJECT user;
{
int c;
for(c=0;c<REVTELL_LINES;++c) user->revbuff[c][0]='\0';
user->revline=0;
}

/*** Records shouts and shemotes sent over the talker. ***/
record_shout(str)
char *str;
{
strncpy(shoutbuff[sbuffline],str,REVIEW_LEN);
shoutbuff[sbuffline][REVIEW_LEN]='\n';
shoutbuff[sbuffline][REVIEW_LEN+1]='\0';
sbuffline=(sbuffline+1)%REVIEW_LINES;
}

/*** Clear the shout buffer of the talker ***/
clear_shouts()
{
int c;
for(c=0;c<REVIEW_LINES;++c) shoutbuff[c][0]='\0';
sbuffline=0;
}

/*** See review of shouts ***/
revshout(user)
UR_OBJECT user;
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


/*** shows the name of a user if they are invis.  Records to tell buffer if rec=1 ***/
write_monitor(user,rm,rec)
int rec;
UR_OBJECT user;
RM_OBJECT rm;
{
UR_OBJECT u;

sprintf(text,"~BB~FG[%s]~RS ",user->name);
for(u=user_first;u!=NULL;u=u->next) {
  if (u==user || u->login || u->type==CLONE_TYPE) continue;
  if (u->level<com_level[MONITOR] || !u->monitor) continue;
  if (u->room==rm || rm==NULL) {
    if (!u->ignall && !u->editing) write_user(u,text);
    if (rec) record_tell(u,text);
    }
  }
}



/* set a name for a quick call */
quick_call(user)
UR_OBJECT user;
{
UR_OBJECT user2;
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
strcpy(user->call,word[1]);
user->call[0]=toupper(user->call[0]);
sprintf(text,"You have set a quick call to: %s.\n",user->call);
write_user(user,text);
}


/* Set list of users that you ignore */
show_igusers(user)
UR_OBJECT user;
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

check_igusers(user,ignoring)
UR_OBJECT user,ignoring;
{
int i;
if (user==NULL || ignoring==NULL) return -1;
for (i=0; i<MAX_IGNORES; ++i)
    if (!strcmp(user->ignoreuser[i],ignoring->name)) return i;
return -1;
}

set_igusers(user)
UR_OBJECT user;
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


/* sets up a channel for the user to ignore */
set_ignore(user)
UR_OBJECT user;
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
  }
}


/* displays what the user is currently listening to/ignoring */
show_ignlist(user)
UR_OBJECT user;
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
if (user->level>=com_level[IGNWIZ]) {
  sprintf(text,"~FTWiztells~RS : %s\n\n",lstn[user->ignwiz]);
  write_user(user,text);
  }
else write_user(user,"\n");
}



/*** See if users site is banned ***/
new_banned(site)
char *site;
{
FILE *fp;
char line[82],filename[80],*given,*check;

sprintf(filename,"%s/%s",DATAFILES,NEWBAN);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%s",line);
while(!feof(fp)) {
        given=NULL;  check=NULL;
	/* first do full name comparison */
	if (!strcmp(site,line)) {  fclose(fp);  return 1;  }
	given=site;  check=line;
	/* check if it has wild card */
	while(*check) {
	    if (*check=='*') {
	      ++check;
	      if (!*check) return 1;
	      while (*given && *given!=*check) ++given;
	      }
	    if (*check==*given) { ++check;  ++given; }
	    else goto SKIP;
	    }
	return 1;
      SKIP:
	fscanf(fp,"%s",line);
	}
fclose(fp);
return 0;
}



/* Ban any new accounts from a given site */
ban_new(user)
UR_OBJECT user;
{
FILE *fp;
char filename[80],host[81],site[80],*given,*check;

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
check=word[2];
while (*check) {
    if (*check=='*') {
        ++check;
	if (*check=='*') {
	    write_user(user,"You cannot use two wild-cards next to each other.\n");
	    return;
	    }
        }
    ++check;
    }
given=host;  check=word[2];
/* check if, with the wild card, the name matches host's name */
while(*given) {
    if (*check=='*') {
      ++check;
      if (!*check) {
	  write_user(user,"You cannot ban the machine that that program is running on.\n");
	  return;
	  }
      while (*given && *given!=*check) ++given;
      }
    if (*check==*given) { ++check;  ++given; }
    else goto SKIP;
    }
write_user(user,"You cannot ban the machine that that program is running on.\n");
return;
SKIP:
sprintf(filename,"%s/%s",DATAFILES,NEWBAN);
/* See if ban already set for given site */
if (fp=fopen(filename,"r")) {
	fscanf(fp,"%s",site);
	while(!feof(fp)) {
		if (!strcmp(site,word[2])) {
			write_user(user,"New users from that site/domain have already been banned.\n");
			fclose(fp);  return;
			}
		fscanf(fp,"%s",site);
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


/* unban new accounts from a given site */
unban_new(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
char filename[80],site[80];
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
fscanf(infp,"%s",site);
while(!feof(infp)) {
	if (strcmp(word[2],site)) {  
		fprintf(outfp,"%s\n",site);  cnt++;  
		}
	else found=1;
	fscanf(infp,"%s",site);
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


/* Create an account for a user if new users from their site have been
   banned and they want to log on - you know they aint a trouble maker, etc */
create_account(user)
UR_OBJECT user;
{
UR_OBJECT u;
char filename[80];
FILE *fp;
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
    strcpy(u->pass,(char *)crypt(word[2],"NU"));
    strcpy(u->desc,"is a newbie needing a desc.");
    strcpy(u->in_phrase,"wanders in.");
    strcpy(u->out_phrase,"wanders out");
    strcpy(u->last_site,"created_account");
    strcpy(u->site,u->last_site);
    strcpy(u->logout_room,"<none>");
    strcpy(u->verify_code,"#NONE");
    strcpy(u->email,"#UNSET");
    strcpy(u->homepage,"#UNSET");
    u->prompt=prompt_def;
    u->charmode_echo=0;
    u->room=room_first;
    u->level=NEW; u->unarrest=NEW;
    save_user_details(u,0);
    add_userlist(u->name);
    add_level_list(u->name,u->level);
    sprintf(text,"Was manually created by %s.\n",user->name);
    add_history(u->name,1,text);
    sprintf(text,"You have created an account with the name \"~FT%s~RS\" and password \"~FG%s~RS\".\n",u->name,word[2]);
    write_user(user,text);
    sprintf(text,"%s created a new account with the name '%s'\n",user->name,u->name);
    write_syslog(text,1,SYSLOG);
    user_number++;
    destruct_user(u);  destructed=0;  return;
    }
write_user(user,"You cannot create an account with the name of an existing user!\n");
destruct_user(u);  destructed=0;
}


/*** Join a user in another room ***/
join(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm,old_room;
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
if (rm==NULL) {
  sprintf(text,"%s is currently off site so you cannot join them.\n",u->name);
  write_user(user,text);
  return;
  }
if (rm==user->room) {
  sprintf(text,"You are already with %s in the %s.\n",u->name,rm->name);
  write_user(user,text);
  return;
  };
if (!has_room_access(user,rm)) {
  sprintf(text,"That room is currently private, you cannot join %s there.\n",u->name);
  write_user(user,text);
  return;
  }
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~FT~OLYou join %s in the %s.~RS\n",u->name,u->room);
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
shackle(user)
UR_OBJECT user;
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
if (u->room==NULL) {
  sprintf(text,"%s is currently off site and cannot be shackled there.\n",u->name);
  write_user(user,text);
  return;
  }
if (u->level>=user->level) {
  write_user(user,"You cannot shackle someone of the same or higher level as yourself.\n");
  return;
  }
if (u->lroom==2) {
  sprintf(text,"%s has already been shackled.\n",u->name);
  write_user(user,text);
  return;
  }
u->lroom=2;
sprintf(text,"\n~FR~OLYou have been shackled to the %s room.\n",u->room->name);
write_user(u,text);
sprintf(text,"~FR~OLYou shackled %s to the %s room.\n",u->name,u->room->name);
write_user(user,text);
sprintf(text,"~FRShackled~RS to the ~FB%s~RS room by ~FB~OL%s~RS.\n",u->room->name,user->name);
add_history(u,1,text);
}

/* Allow a user to move between rooms again */
unshackle(user)
UR_OBJECT user;
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
  sprintf(text,"%s in not currently shackled.\n",u->name);
  write_user(user,text);
  return;
  }
u->lroom=0;
write_user(u,"\n~FG~OLYou have been unshackled.\n");
write_user(u,"You can now use the ~FTset~RS command to alter the ~FBroom~RS attribute.\n");
sprintf(text,"~FG~OLYou unshackled %s from the %s room.\n",u->name,u->room->name);
write_user(user,text);
sprintf(text,"~FGUnshackled~RS from the ~FB%s~RS room by ~FB~OL%s~RS.\n",u->room->name,user->name);
add_history(u,1,text);
}

/* Force a save of all the users who are logged on */
force_save(user)
UR_OBJECT user;
{
UR_OBJECT u;
int cnt;

cnt=0;
for (u=user_first;u!=NULL;u=u->next) {
  if (u->type==REMOTE_TYPE || u->type==CLONE_TYPE) continue;
  cnt++;
  save_user_details(u,1);
  }
sprintf(text,"Manually saved %d user's details.\n",cnt);
write_syslog(text,1,SYSLOG);
sprintf(text,"You have manually saved %d user's details.\n",cnt);
write_user(user,text);
}


/*** Show list of people board posts are from without seeing the whole lot ***/
board_from(user)
UR_OBJECT user;
{
FILE *fp;
int cnt;
char id[ARR_SIZE],line[ARR_SIZE],filename[80];
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
sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
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


/* Check if a normal user can remove a message */
check_board_wipe(user)
UR_OBJECT user;
{
FILE *fp;
int valid,cnt,msg_number,yes,pt;
char w1[ARR_SIZE],w2[ARR_SIZE],line[ARR_SIZE],line2[ARR_SIZE],filename[80],id[ARR_SIZE];
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
  sprintf(text,"There are only %d messages on the board.\n",rm->mesg_cnt);
  write_user(user,text);
  return 0;
  }
sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
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


/* Allows a user to read a specific message number */
read_board_specific(user,rm,msg_number)
UR_OBJECT user;
RM_OBJECT rm;
int msg_number;
{
FILE *fp;
int valid,cnt,pt;
char id[ARR_SIZE],line[ARR_SIZE],filename[80],*name;

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
  sprintf(text,"There are only %d messages posted on the %s board.\n",rm->mesg_cnt,rm->name);
  write_user(user,text);
  return;
  }
if (rm!=user->room && !has_room_access(user,rm)) {
  write_user(user,"That room is currently private, you cannot read the board.\n");
  return;
  }

sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
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
if (user->vis) name=user->name;  else name=invisname;
if (rm==user->room) {
  sprintf(text,"%s reads the message board.\n",name);
  if (user->level<GOD || user->vis) write_room_except(user->room,text,user);
  }
}


/*** Records tells and pemotes sent to the user when afk. ***/
record_edit(user,str)
UR_OBJECT user;
char *str;
{
strncpy(user->editbuff[user->editline],str,REVIEW_LEN);
user->editbuff[user->editline][REVIEW_LEN]='\n';
user->editbuff[user->editline][REVIEW_LEN+1]='\0';
user->editline=(user->editline+1)%REVTELL_LINES;
}

/*** Clear the tell buffer of the user ***/
clear_edit(user)
UR_OBJECT user;
{
int c;
for(c=0;c<REVTELL_LINES;++c) user->editbuff[c][0]='\0';
user->editline=0;
}

/*** Show recorded tells and pemotes ***/
revedit(user)
UR_OBJECT user;
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

/*** Records tells and pemotes sent to the user when afk. ***/
record_afk(user,str)
UR_OBJECT user;
char *str;
{
strncpy(user->afkbuff[user->afkline],str,REVIEW_LEN);
user->afkbuff[user->afkline][REVIEW_LEN]='\n';
user->afkbuff[user->afkline][REVIEW_LEN+1]='\0';
user->afkline=(user->afkline+1)%REVTELL_LINES;
}

/*** Clear the tell buffer of the user ***/
clear_afk(user)
UR_OBJECT user;
{
int c;
for(c=0;c<REVTELL_LINES;++c) user->afkbuff[c][0]='\0';
user->afkline=0;
}

/*** Show recorded tells and pemotes ***/
revafk(user)
UR_OBJECT user;
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


/*** Make a clone emote ***/
clone_emote(user,inpstr)
UR_OBJECT user;
char *inpstr;
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


/* Allows a user to listen to everything again */
user_listen(user)
UR_OBJECT user;
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



/* Display all the people logged on from the same site as user */
samesite(user,stage)
UR_OBJECT user;
int stage;
{
UR_OBJECT u,u_loop;
int found,cnt,same,on;
char line[82],filename[80],name[USER_NAME_LEN+1];
FILE *fpi,*fpu;

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
    user->misc_op=13;
    return;
    }
  if (!strcmp(word[1],"site")) {
    write_user(user,"~OL~FRNOTE:~RS Partial site strings can be given, but NO wildcards.\n");
    write_user(user,"Enter the site to be checked against: ");
    user->misc_op=14;
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
      if (strstr(u->site,u_loop->site)) {
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
      sprintf(text,"No users currently logged on have that same site as %s.\n",u->name);
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
  /* open userlist to check against all users */
  sprintf(filename,"%s/%s",USERFILES,USERLIST);
  if (!(fpi=fopen(filename,"r"))) {
    write_syslog("ERROR: Unable to open userlist in samesite() - stage 1/all.\n",0,SYSLOG);
    write_user(user,"Sorry, you are unable to use the ~OLall~RS option at this time.\n");
    return;
    }
  found=cnt=same=0;
  fscanf(fpi,"%s",name);
  while (!feof(fpi)) {
    name[0]=toupper(name[0]);
    /* create a user object if user not already logged on */
    if ((u_loop=create_user())==NULL) {
      write_syslog("ERROR: Unable to create temporary user session in samesite().\n",0,SYSLOG);
      goto SAME_SKIP1;
      }
    strcpy(u_loop->name,name);
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
    fscanf(fpi,"%s",name);
    }
  fclose(fpi);
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
  /* check just those logged on */
  if (!user->samesite_all_store) {
    found=cnt=same=0;
    for (u=user_first;u!=NULL;u=u->next) {
      cnt++;
      if (!strstr(u->site,user->samesite_check_store)) continue;
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
  sprintf(filename,"%s/%s",USERFILES,USERLIST);
  if (!(fpi=fopen(filename,"r"))) {
    write_syslog("ERROR: Unable to open userlist in samesite() - stage 2/all.\n",0,SYSLOG);
    write_user(user,"Sorry, you are unable to use the ~OLall~RS option at this time.\n");
    return;
    }
  found=cnt=same=0;
  fscanf(fpi,"%s",name);
  while (!feof(fpi)) {
    name[0]=toupper(name[0]);
    /* create a user object if user not already logged on */
    if ((u_loop=create_user())==NULL) {
      write_syslog("ERROR: Unable to create temporary user session in samesite() - stage 2/all.\n",0,SYSLOG);
      goto SAME_SKIP2;
      }
    strcpy(u_loop->name,name);
    if (!load_user_details(u_loop)) {
      destruct_user(u_loop); destructed=0;
      goto SAME_SKIP2;
      }
    cnt++;
    if (strstr(u_loop->last_site,user->samesite_check_store)) {
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
    fscanf(fpi,"%s",name);
    }
  fclose(fpi);
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



/* lets a user start, stop or check out their status of a game of hangman */
play_hangman(user)
UR_OBJECT user;
{
int i;
char *get_hang_word();

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
char *get_hang_word(aword)
char *aword; 
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


/* counts how many lines are in a file */
int count_lines(filename)
char *filename;
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


/* Lets a user guess a letter for hangman */
guess_hangman(user)
UR_OBJECT user;
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
