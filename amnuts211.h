/*****************************************************************************
                    Header file for Amnuts version 2.1.1
        Copyright (C) Andrew Collington - Last update: 25th May, 1998
 *****************************************************************************/

/* version number - you can add and check against your own version number
   but the Amnuts and NUTS must stay the same as listed below
   */
#define AMNUTSVER "2.1.1"
#define NUTSVER "3.3.3"

/* general directories */
#define DATAFILES "datafiles"
#define HELPFILES "helpfiles"
#define MAILSPOOL "mailspool"
#define MISCFILES "miscfiles"
#define PICTFILES "pictfiles"
#define LASTCMDLOGS "lastcmdlogs"
#define MOTDFILES "motds"

/* user directories */
#define USERFILES "userfiles"
#define USERMAILS "mail"
#define USERPROFILES "profiles"
#define USERFRIENDS "friends"
#define USERHISTORYS "historys"
#define USERCOMMANDS "xgcoms"
#define USERMACROS "macros"
#define USERROOMS "rooms"

/* files */
#define CONFIGFILE "config"
#define NEWSFILE "newsfile"
#define MAPFILE "mapfile"
#define SITEBAN "siteban"
#define USERBAN "userban"
#define NEWBAN "newban"
#define SUGBOARD "suggestions"
#define RULESFILE "rules"
#define HANGDICT "hangman_words"

/* system logs */
#define LAST_CMD   "last_command"
#define MAINSYSLOG "syslog"
#define NETSYSLOG  "netlog"
#define REQSYSLOG  "reqlog"
#define SYSLOG 0
#define NETLOG 1
#define REQLOG 2

/* general defines */
#define OUT_BUFF_SIZE 1000
#define MAX_WORDS 10
#define WORD_LEN 40
#define ARR_SIZE 1000
#define MAX_LINES 15
#define REVIEW_LINES 15
#define REVTELL_LINES 10
#define REVIEW_LEN 400
#define BUFSIZE 1000
#define ROOM_NAME_LEN 20
#define ROOM_LABEL_LEN 5
#define OFF 0
#define MIN 1
#define MAX 2
#define LASTLOGON_NUM 5

/* netlink defines */
#ifdef NETLINKS
  #define SERV_NAME_LEN 80
  #define SITE_NAME_LEN 80
  #define VERIFY_LEN 20
  #define UNCONNECTED 0 
  #define INCOMING 1 
  #define OUTGOING 2
  #define DOWN 0
  #define VERIFYING 1
  #define UP 2
  #define ALL 0
  #define IN 1
  #define OUT 2
#endif

/* user defines */
#define USER_NAME_LEN 12
#define USER_DESC_LEN 35
#define AFK_MESG_LEN 60
#define PHRASE_LEN 40
#define PASS_LEN 20 /* only the 1st 8 chars will be used by crypt() though */
#define ROOM_DESC_LEN (MAX_LINES*80)+MAX_LINES /* MAX_LINES lines of 80 chars each + MAX_LINES nl */
#define TOPIC_LEN 60
#define ICQ_LEN 20
#define NEUTER 0
#define MALE   1
#define FEMALE 2
#define NEWBIE_EXPIRES 20 /* days */
#define USER_EXPIRES   40 /* days */
#define SCREEN_WRAP 80 /* how many characters to wrap to */
#define MAX_COPIES 6 /* of smail */
#define MACRO_LEN 65
#define MAX_FRIENDS 10
#define MAX_IGNORES 10 /* number of users you can ignore */
#define MAX_XCOMS 10
#define MAX_GCOMS 10
#define MAX_PAGES 1000 /* should be enough! */

/* rooms */
#define MAX_LINKS 20
#define PUBLIC 0
#define PRIVATE 1
#define FIXED 2
#define FIXED_PUBLIC 2
#define FIXED_PRIVATE 3
#define PERSONAL_UNLOCKED 4
#define PERSONAL_LOCKED 5

/* levels */
#define JAILED 0
#define NEW    1
#define USER   2
#define SUPER  3
#define WIZ    4
#define ARCH   5
#define GOD    6
#define RETIRE_LIST "retired_wiz"

/* user and clone types */
#define USER_TYPE 0
#define CLONE_TYPE 1
#define REMOTE_TYPE 2
#define CLONE_HEAR_NOTHING 0
#define CLONE_HEAR_SWEARS 1
#define CLONE_HEAR_ALL 2

/* logon prompt stuff */
#define LOGIN_ATTEMPTS 3
#define LOGIN_NAME 1
#define LOGIN_PASSWD 2
#define LOGIN_CONFIRM 3
#define LOGIN_PROMPT 4

/* some macros that are used in the code */
#define PLTEXT_S(n) &"s"[(1==(n))]
#define PLTEXT_ES(n) &"es"[(1==(n))<<1]
#define PLTEXT_IS(n) ((1==(n))?"is":"are")
#define PLTEXT_WAS(n) ((1==(n))?"was":"were")
#define SIZEOF(table) (sizeof(table)/sizeof(table[0]))

/* attempt to stop freezing time.  Thanks to Arny ('Paris' code creator)
   and Cygnus ('Ncohafmuta' code creator) for this */
#if !defined(__GLIBC__) || (__GLIBC__ < 2)
#define SIGNAL(x,y) signal(x,y)
#else
#define SIGNAL(x,y) sysv_signal(x,y)
#endif

/* syserrlist - hopefully it'll work like this */
#if !defined(__GLIBC__)
  extern char *const sys_errlist[];
#endif

/* user variables - some are saved in the user file, and some are not */
struct user_struct {
  char name[USER_NAME_LEN+1],desc[USER_DESC_LEN+1],pass[PASS_LEN+6];
  char in_phrase[PHRASE_LEN+1],out_phrase[PHRASE_LEN+1];
  char buff[BUFSIZE],site[81],ipsite[81],last_site[81],page_file[81];
  char mail_to[WORD_LEN+1],revbuff[REVTELL_LINES][REVIEW_LEN+2];
  char afk_mesg[AFK_MESG_LEN+1],inpstr_old[REVIEW_LEN+1];
  char tname[80],tsite[80],tport[5],logout_room[ROOM_NAME_LEN+1],version[10];
  char copyto[MAX_COPIES][USER_NAME_LEN+1],invite_by[USER_NAME_LEN+1],date[80];
  char email[81],homepage[81],ignoreuser[MAX_IGNORES][USER_NAME_LEN+1],recap[USER_NAME_LEN+1];
  char call[USER_NAME_LEN+1],macros[10][MACRO_LEN],friend[MAX_FRIENDS][USER_NAME_LEN+1];
  char verify_code[80],afkbuff[REVTELL_LINES][REVIEW_LEN+2],editbuff[REVTELL_LINES][REVIEW_LEN+2];
  char samesite_check_store[ARR_SIZE],hang_word[WORD_LEN+1],hang_word_show[WORD_LEN+1],hang_guess[WORD_LEN+1];
  char *malloc_start,*malloc_end,icq[ICQ_LEN+1];
  int type,login,attempts,vis,ignall,prompt,command_mode,muzzled,charmode_echo;
  int gender,hideemail,edit_line,warned,accreq,ignall_store,igntells;
  int afk,clone_hear,colour,ignshouts,unarrest,arrestby,pager,expire,lroom,monitor;
  int show_rdesc,wrap,alert,mail_verified,autofwd,editing,show_pass,pagecnt,pages[MAX_PAGES];
  int ignpics,ignlogons,ignwiz,igngreets,ignbeeps,hang_stage,samesite_all_store;
  int port,site_port,socket,buffpos,filepos,remote_com,charcnt,misc_op,last_login_len;
  int edit_op,revline,level,wipe_from,wipe_to,logons,cmd_type,user_page_pos,user_page_lev;
  int age,misses,hits,kills,deaths,bullets,hps,afkline,editline,login_prompt;
  int lmail_lev,hwrap_lev,hwrap_id,hwrap_same,hwrap_func,gcoms[MAX_GCOMS],xcoms[MAX_XCOMS];
  struct room_struct *room,*invite_room,*wrap_room;
  struct user_struct *prev,*next,*owner;
  time_t last_input,last_login,total_login,read_mail,t_expire;
  #ifdef NETLINKS
    struct netlink_struct *netlink,*pot_netlink;
  #endif
  };
typedef struct user_struct *UR_OBJECT;
UR_OBJECT user_first,user_last;

/* structure to see who last logged on */
struct {
  char name[USER_NAME_LEN+1],time[80];
  short int on;
  } 
last_login_info[LASTLOGON_NUM+1];

/* room informaytion structure */
struct room_struct {
  char name[ROOM_NAME_LEN+1];
  char label[ROOM_LABEL_LEN+1];
  char desc[ROOM_DESC_LEN+1];
  char topic[TOPIC_LEN+1];
  char revbuff[REVIEW_LINES][REVIEW_LEN+2];
  int access; /* public , private etc */
  int revline; /* line number for review */
  int mesg_cnt;
  char link_label[MAX_LINKS][ROOM_LABEL_LEN+1]; /* temp store for parse */
  struct room_struct *link[MAX_LINKS];
  struct room_struct *next,*prev;
  #ifdef NETLINKS
    int inlink; /* 1 if room accepts incoming net links */
    char netlink_name[SERV_NAME_LEN+1]; /* temp store for config parse */
    struct netlink_struct *netlink; /* for net links, 1 per room */
  #endif
  };
typedef struct room_struct *RM_OBJECT;
RM_OBJECT room_first,room_last;
RM_OBJECT create_room();

#ifdef NETLINKS
  /* Structure for net links, ie server initiates them */
  struct netlink_struct {
    char service[SERV_NAME_LEN+1];
    char site[SITE_NAME_LEN+1];
    char verification[VERIFY_LEN+1];
    char buffer[ARR_SIZE*2];
    char mail_to[WORD_LEN+1];
    char mail_from[WORD_LEN+1];
    FILE *mailfile;
    time_t last_recvd; 
    int port,socket,type,connected;
    int stage,lastcom,allow,warned,keepalive_cnt;
    int ver_major,ver_minor,ver_patch;
    struct user_struct *mesg_user;
    struct room_struct *connect_room;
    struct netlink_struct *prev,*next;
  };
  typedef struct netlink_struct *NL_OBJECT;
  NL_OBJECT nl_first,nl_last;
  NL_OBJECT create_netlink();
#endif

/* main user list structure */
struct user_dir_struct {
  char name[USER_NAME_LEN+1],date[80];
  short int level;
  struct user_dir_struct *next,*prev;
  };
struct user_dir_struct *first_dir_entry,*last_dir_entry;

/* main list of wizzes */
struct wiz_list_struct {
  char name[USER_NAME_LEN+1];
  short int level;
  struct wiz_list_struct *next,*prev;
  };
struct wiz_list_struct *first_wiz_entry,*last_wiz_entry;

/* command list */
struct command_struct {
  char name[15],alias[5]; /* 15 and 5 characters should be long enough */
  short int id,min_lev,function;
  int count;
  struct command_struct *next,*prev;
  };
struct command_struct *first_command,*last_command;
char cmd_history[16][128];


/* levels used on the talker */
char *level_name[]={
  "JAILED","NEW","USER","SUPER","WIZ","ARCH","GOD","*"
  };

char *level_alias[]={
  "|","_"," "," ","^","+","*"
  };


/* default rooms */
char *default_jail="cupboard";
char *default_warp="reception";
char *default_shoot="directors";

/* The rooms listed here are just examples of what can be added
   You may add more or remove as many as you like, but you MUST
   keep the stopping clause in */
struct { 
  char *name; int level; 
  } priv_room[]={
    { "wizroom",WIZ }, /* a room for wizzes+ only */
    { "andys_computer",GOD }, /* only top people can get in this place! */
    { "*",0 } /* stopping clause */
    };

/* you can set a standard room desc for those people who are creating a new
   personal room */
char *default_personal_room_desc=
"The walls are stark and the floors are bare.  Either the owner is new\n\
or just plain lazy.  Perhaps they just don't know how to use the .mypaint\n\
command yet?\n";


/* colour code values */
struct {
  char *esc_code;
  char *txt_code;
  } colour_codes[]={
    /* Standard stuff */
    { "\033[0m", "RS" }, /* reset */
    { "\033[1m", "OL" }, /* bold */
    { "\033[4m", "UL" }, /* underline */
    { "\033[5m", "LI" }, /* blink */
    { "\033[7m", "RV" }, /* reverse */
    /* Foreground colour */
    { "\033[30m", "FK" }, /* black */
    { "\033[31m", "FR" }, /* red */
    { "\033[32m", "FG" }, /* green */
    { "\033[33m", "FY" }, /* yellow */
    { "\033[34m", "FB" }, /* blue */
    { "\033[35m", "FM" }, /* magenta */
    { "\033[36m", "FT" }, /* turquiose */
    { "\033[37m", "FW" }, /* white */
    /* Background colour */
    { "\033[40m", "BK" }, /* black */
    { "\033[41m", "BR" }, /* red */
    { "\033[42m", "BG" }, /* green */
    { "\033[43m", "BY" }, /* yellow */
    { "\033[44m", "BB" }, /* blue */
    { "\033[45m", "BM" }, /* magenta */
    { "\033[46m", "BT" }, /* turquiose */
    { "\033[47m", "BW" }, /* white */
  };
#define NUM_COLS SIZEOF(colour_codes)


/* some general arrays being defined */
char *month[12]={
  "January","February","March","April","May","June",
  "July","August","September","October","November","December"
  };

char *day[7]={
  "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
  };

char *noyes1[]={ " NO","YES" };
char *noyes2[]={ "NO ","YES" };
char *offon[]={ "OFF","ON " };
char *minmax[]={"OFF","MIN","MAX"};
char *sex[]={"Neuter","Male","Female"};


/* random kill messages */
struct {
  char *victim_msg;
  char *room_msg;
  } kill_mesgs[]={
    {"You are killed\n","%s is killed\n"},
    {"You have been totally splatted\n","A hammer splats %s\n"},
    {"The Hedgehog Of Doom runs over you with a car.\n","The Hedgehog Of Doom runs over %s with a car.\n"},
    {"The Inquisitor deletes the worthless, prunes away the wastrels... ie, YOU!","The Inquisitor prunes away %s.\n"}
  };
#define MAX_KILL_MESGS SIZEOF(kill_mesgs)


/* other strings used on the talker */
char *syserror="Sorry, a system error has occured";
char *nosuchroom="There is no such room.\n";
char *nosuchuser="There is no such user.\n";
char *notloggedon="There is no one of that name logged on.\n";
char *invisenter="A presence enters the room...\n";
char *invisleave="A presence leaves the room...\n";
char *invisname="A presence";
char *talker_name="Amnuts";
char *crypt_salt="NU";
char *long_date();


/* you can change this for whatever sig you want - of just "" if you don't want
   to have a sig file attached at the end of emails */
char *talker_signature=
"+--------------------------------------------------------------------------+\n\
|  This message has been smailed to you on The Amnuts Talker, and this is  |\n\
|      your auto-forward.  Please do not reply directly to this email.     |\n\
|                                                                          |\n\
|               Amnuts - A talker running at foo.bar.com 666               |\n\
|         email 'me@my.place' if you have any questions/comments           |\n\
+--------------------------------------------------------------------------+\n";


/* swear words array.  These must all be lowercase.  * is the stopping clause and
   must remain in the array even if you have no words listed.
   */
char *swear_words[]={
  "fuck","shit","cunt","cock","bastard","dyke","fag","pussy","bitch","*"
  };

/* This is what replaces any swear words in a string if the swearban is set to MIN. */
char *swear_censor="smeg"; /* alright so I'm a Red Dwarf fan! */
char *noswearing="Swearing is not allowed here.\n";


/* Other global variables */
char text[ARR_SIZE*2];
char word[MAX_WORDS][WORD_LEN+1];
char wrd[8][81];
char progname[40],confile[40];
time_t boot_time;
jmp_buf jmpvar;
#ifdef NETLINKS
  char verification[SERV_NAME_LEN+1];
  int listen_sock[3],port[3],port_total=3;
#else 
  int listen_sock[2],port[2],port_total=2;
#endif
int wizport_level,minlogin_level;
int colour_def,password_echo,ignore_sigterm;
int max_users,max_clones,num_of_users,num_of_logins,heartbeat;
int login_idle_time,user_idle_time,config_line,word_count;
int tyear,tmonth,tday,tmday,twday,thour,tmin,tsec;
int mesg_life,system_logging,prompt_def,no_prompt,auto_promote;
int force_listen,gatecrash_level,min_private_users;
int ignore_mp_level,rem_user_maxlevel,rem_user_deflevel;
int destructed,mesg_check_hour,mesg_check_min,net_idle_time;
int keepalive_interval,auto_connect,ban_swearing,crash_action;
int time_out_afks,rs_countdown,allow_recaps,user_count;
int charecho_def,time_out_maxlevel,auto_purge,personal_rooms;
int logons_old,logons_new,purge_count,purge_skip,users_purged,suggestion_count;
int startup_room_parse,motd1_cnt,motd2_cnt,random_motds;
int level_count[GOD+1];  /* keep count of users/level - MUST be max. level + 1 */
static int last_cmd_cnt;
time_t rs_announce,rs_which,purge_date,sbuffline,forwarding,logon_flag;
char shoutbuff[REVIEW_LINES][REVIEW_LEN+2];
UR_OBJECT rs_user;


/* Letter array map - for greet() */
int biglet[26][5][5] = {
  {{0,1,1,1,0},{1,0,0,0,1},{1,1,1,1,1},{1,0,0,0,1},{1,0,0,0,1}},
  {{1,1,1,1,0},{1,0,0,0,1},{1,1,1,1,0},{1,0,0,0,1},{1,1,1,1,0}},
  {{0,1,1,1,1},{1,0,0,0,0},{1,0,0,0,0},{1,0,0,0,0},{0,1,1,1,1}},
  {{1,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,1,1,1,0}},
  {{1,1,1,1,1},{1,0,0,0,0},{1,1,1,1,0},{1,0,0,0,0},{1,1,1,1,1}},
  {{1,1,1,1,1},{1,0,0,0,0},{1,1,1,1,0},{1,0,0,0,0},{1,0,0,0,0}},
  {{0,1,1,1,0},{1,0,0,0,0},{1,0,1,1,0},{1,0,0,0,1},{0,1,1,1,0}},
  {{1,0,0,0,1},{1,0,0,0,1},{1,1,1,1,1},{1,0,0,0,1},{1,0,0,0,1}},
  {{0,1,1,1,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,1,1,1,0}},
  {{0,0,0,0,1},{0,0,0,0,1},{0,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}},
  {{1,0,0,0,1},{1,0,0,1,0},{1,0,1,0,0},{1,0,0,1,0},{1,0,0,0,1}},
  {{1,0,0,0,0},{1,0,0,0,0},{1,0,0,0,0},{1,0,0,0,0},{1,1,1,1,1}},
  {{1,0,0,0,1},{1,1,0,1,1},{1,0,1,0,1},{1,0,0,0,1},{1,0,0,0,1}},
  {{1,0,0,0,1},{1,1,0,0,1},{1,0,1,0,1},{1,0,0,1,1},{1,0,0,0,1}},
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}},
  {{1,1,1,1,0},{1,0,0,0,1},{1,1,1,1,0},{1,0,0,0,0},{1,0,0,0,0}},
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,1,0,1},{1,0,0,1,1},{0,1,1,1,0}},
  {{1,1,1,1,0},{1,0,0,0,1},{1,1,1,1,0},{1,0,0,1,0},{1,0,0,0,1}},
  {{0,1,1,1,1},{1,0,0,0,0},{0,1,1,1,0},{0,0,0,0,1},{1,1,1,1,0}},
  {{1,1,1,1,1},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0}},
  {{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,1,1,1,1}},
  {{1,0,0,0,1},{1,0,0,0,1},{0,1,0,1,0},{0,1,0,1,0},{0,0,1,0,0}},
  {{1,0,0,0,1},{1,0,0,0,1},{1,0,1,0,1},{1,1,0,1,1},{1,0,0,0,1}},
  {{1,0,0,0,1},{0,1,0,1,0},{0,0,1,0,0},{0,1,0,1,0},{1,0,0,0,1}},
  {{1,0,0,0,1},{0,1,0,1,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0}},
  {{1,1,1,1,1},{0,0,0,1,0},{0,0,1,0,0},{0,1,0,0,0},{1,1,1,1,1}}
  };

/* Symbol array map - for greet() */
int bigsym[32][5][5] = {
  {{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,0,0,0},{0,0,1,0,0}},
  {{0,1,0,1,0},{0,1,0,1,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}},
  {{0,1,0,1,0},{1,1,1,1,1},{0,1,0,1,0},{1,1,1,1,1},{0,1,0,1,0}},
  {{0,1,1,1,1},{1,0,1,0,0},{0,1,1,1,0},{0,0,1,0,1},{1,1,1,1,0}},
  {{1,1,0,0,1},{1,1,0,1,0},{0,0,1,0,0},{0,1,0,1,1},{1,0,0,1,1}},
  {{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}},
  {{0,0,1,0,0},{0,0,1,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}},
  {{0,0,1,1,0},{0,1,0,0,0},{0,1,0,0,0},{0,1,0,0,0},{0,0,1,1,0}},
  {{0,1,1,0,0},{0,0,0,1,0},{0,0,0,1,0},{0,0,0,1,0},{0,1,1,0,0}},
  {{1,0,1,0,1},{0,1,1,1,0},{1,1,1,1,1},{0,1,1,1,0},{1,0,1,0,1}},
  {{0,0,1,0,0},{0,0,1,0,0},{1,1,1,1,1},{0,0,1,0,0},{0,0,1,0,0}},
  {{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,1,0,0,0},{1,1,0,0,0}},
  {{0,0,0,0,0},{0,0,0,0,0},{1,1,1,1,1},{0,0,0,0,0},{0,0,0,0,0}},
  {{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{1,1,0,0,0},{1,1,0,0,0}},
  {{0,0,0,0,1},{0,0,0,1,0},{0,0,1,0,0},{0,1,0,0,0},{1,0,0,0,0}},
  {{0,1,1,1,0},{1,0,0,1,1},{1,0,1,0,1},{1,1,0,0,1},{0,1,1,1,0}},
  {{0,0,1,0,0},{0,1,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,1,1,1,0}},
  {{1,1,1,1,0},{0,0,0,0,1},{0,1,1,1,0},{1,0,0,0,0},{1,1,1,1,1}},
  {{1,1,1,1,0},{0,0,0,0,1},{0,1,1,1,0},{0,0,0,0,1},{1,1,1,1,0}},
  {{0,0,1,1,0},{0,1,0,0,0},{1,0,0,1,0},{1,1,1,1,1},{0,0,0,1,0}},
  {{1,1,1,1,1},{1,0,0,0,0},{1,1,1,1,0},{0,0,0,0,1},{1,1,1,1,0}},
  {{0,1,1,1,0},{1,0,0,0,0},{1,1,1,1,0},{1,0,0,0,1},{0,1,1,1,0}},
  {{1,1,1,1,1},{0,0,0,0,1},{0,0,0,1,0},{0,0,1,0,0},{0,1,0,0,0}},
  {{0,1,1,1,0},{1,0,0,0,1},{0,1,1,1,0},{1,0,0,0,1},{0,1,1,1,0}},
  {{1,1,1,1,1},{1,0,0,0,1},{1,1,1,1,1},{0,0,0,0,1},{0,0,0,0,1}},
  {{0,0,0,0,0},{0,0,1,0,0},{0,0,0,0,0},{0,0,1,0,0},{0,0,0,0,0}},
  {{0,0,0,0,0},{0,0,1,0,0},{0,0,0,0,0},{0,0,1,0,0},{0,1,0,0,0}},
  {{0,0,0,1,0},{0,0,1,0,0},{0,1,0,0,0},{0,0,1,0,0},{0,0,0,1,0}},
  {{0,0,0,0,0},{1,1,1,1,1},{0,0,0,0,0},{1,1,1,1,1},{0,0,0,0,0}},
  {{0,1,0,0,0},{0,0,1,0,0},{0,0,0,1,0},{0,0,1,0,0},{0,1,0,0,0}},
  {{0,1,1,1,1},{0,0,0,0,1},{0,0,1,1,1},{0,0,0,0,0},{0,0,1,0,0}},
  {{0,1,0,0,0},{1,0,1,1,1},{1,0,1,0,1},{1,0,1,1,1},{0,1,1,1,0}}
  };


/* hangman picture for the hangman game */
char *hanged[8]={
  "~FY~OL+~RS~FY---~OL+  \n~FY|      \n~FY|~RS           ~OLWord:~RS %s\n~FY|~RS           ~OLLetters guessed:~RS %s\n~FY|~RS      \n~FY|______\n",
  "~FY~OL+~RS~FY---~OL+  \n~FY|   |  \n~FY|~RS           ~OLWord:~RS %s\n~FY|~RS           ~OLLetters guessed:~RS %s\n~FY|~RS      \n~FY|______\n",
  "~FY~OL+~RS~FY---~OL+  \n~FY|   |  \n~FY|~RS   O       ~OLWord:~RS %s\n~FY|~RS           ~OLLetters guessed:~RS %s\n~FY|~RS      \n~FY|______\n",
  "~FY~OL+~RS~FY---~OL+  \n~FY|   |  \n~FY|~RS   O       ~OLWord:~RS %s\n~FY|~RS   |       ~OLLetters guessed:~RS %s\n~FY|~RS      \n~FY|______\n",
  "~FY~OL+~RS~FY---~OL+  \n~FY|   |  \n~FY|~RS   O       ~OLWord:~RS %s\n~FY|~RS  /|       ~OLLetters guessed:~RS %s\n~FY|~RS      \n~FY|______\n",
  "~FY~OL+~RS~FY---~OL+  \n~FY|   |  \n~FY|~RS   O       ~OLWord:~RS %s\n~FY|~RS  /|\\      ~OLLetters guessed:~RS %s\n~FY|~RS      \n~FY|______\n",
  "~FY~OL+~RS~FY---~OL+  \n~FY|   |  \n~FY|~RS   O       ~OLWord:~RS %s\n~FY|~RS  /|\\      ~OLLetters guessed:~RS %s\n~FY|~RS  /   \n~FY|______\n",
  "~FY~OL+~RS~FY---~OL+  \n~FY|   |  \n~FY|~RS   O       ~OLWord:~RS %s\n~FY|~RS  /|\\      ~OLLetters guessed:~RS %s\n~FY|~RS  / \\ \n~FY|______\n"
  };



 /*****************************************************************************
            Calendar code taken from Way Out West version 4.0.0
                      Copyright (C) Andrew Collington

               based upon scalar date routines by Ray Gardner   
 *****************************************************************************/


/* Define ISO to be 1 for ISO (Mon-Sun) calendars
   ISO defines the first week with 4 or more days in it to be week #1.
   */
#ifndef ISO
 #define ISO 0
#endif

#if (ISO!=0 && ISO!=1)
 #error ISO must be set to either 0 or 1
#endif

int cal_days[12]={31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
char *cal_daynames[8]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
