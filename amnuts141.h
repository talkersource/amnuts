/*****************************************************************************
                   Header file for Amnuts version 1.5.0
                        by Andrew Collington (1997)
     (based on NUTS version 3.3.3 - Copyright (C) Neil Robertson 1996)

                      Last Updated: 2nd November, 1997
 *****************************************************************************/

/* directories */
#define DATAFILES "datafiles"
#define USERFILES "userfiles"
#define HELPFILES "helpfiles"
#define MAILSPOOL "mailspool"
#define MISCFILES "miscfiles"
#define PICTFILES "pictfiles"
/* files */
#define CONFIGFILE "config"
#define NEWSFILE "newsfile"
#define MAPFILE "mapfile"
#define SITEBAN "siteban"
#define USERBAN "userban"
#define NEWBAN "newban"
#define MOTD1 "motd1"
#define MOTD2 "motd2"
#define USERLIST "userlist"
#define SUGBOARD "suggestions"
#define RULESFILE "rules"
#define HANGDICT "hangman_words"
/* system logs */
#define MAINSYSLOG "syslog"
#define NETSYSLOG  "netlog"
#define REQSYSLOG  "reqlog"
#define SYSLOG 0
#define NETLOG 1
#define REQLOG 2

#define OUT_BUFF_SIZE 1000
#define MAX_WORDS 10
#define WORD_LEN 40
#define ARR_SIZE 1000
#define MAX_LINES 15
#define NUM_COLS 21
#define NUM_CMDS 200

#define USER_NAME_LEN 12
#define USER_DESC_LEN 35
#define AFK_MESG_LEN 60
#define PHRASE_LEN 40
#define PASS_LEN 20 /* only the 1st 8 chars will be used by crypt() though */
#define BUFSIZE 1000
#define ROOM_NAME_LEN 20
#define ROOM_LABEL_LEN 5
#define ROOM_DESC_LEN 810 /* 10 lines of 80 chars each + 10 nl */
#define TOPIC_LEN 60
#define MAX_LINKS 20
#define SERV_NAME_LEN 80
#define SITE_NAME_LEN 80
#define VERIFY_LEN 20
#define REVIEW_LINES 15
#define REVTELL_LINES 10
#define REVIEW_LEN 400
/* DNL (Date Number Length) will have to become 12 on Sun Sep 9 02:46:40 2001 
   when all the unix timers will flip to 1000000000 :) */
#define DNL 11 

/* room status */
#define PUBLIC 0
#define PRIVATE 1
#define FIXED 2
#define FIXED_PUBLIC 2
#define FIXED_PRIVATE 3

/* levels */
#define JAILED 0
#define NEW    1
#define USER   2
#define SUPER  3
#define WIZ    4
#define ARCH   5
#define GOD    6
#define JAILED_LIST "jailed.lst"
#define NEW_LIST    "new.lst"
#define USER_LIST   "user.lst"
#define SUPER_LIST  "super.lst"
#define WIZ_LIST    "wiz.lst"
#define ARCH_LIST   "arch.lst"
#define GOD_LIST    "god.lst"

/* user and clone types */
#define USER_TYPE 0
#define CLONE_TYPE 1
#define REMOTE_TYPE 2
#define CLONE_HEAR_NOTHING 0
#define CLONE_HEAR_SWEARS 1
#define CLONE_HEAR_ALL 2

/* misc stuff */
#define NEUTER 0
#define MALE   1
#define FEMALE 2
#define NEWBIE_EXPIRES 20 /* days */
#define USER_EXPIRES   40 /* days */
#define SCREEN_WRAP 80 /* how many characters to wrap to */
#define MAX_COPIES 6 /* of smail */
#define MACRO_LEN 65
#define MAX_FRIENDS 10
#define MAX_IGNORES 5 /* number of users you can ignore */


/* The elements vis, ignall, prompt, command_mode etc could all be bits in 
   one flag variable as they're only ever 0 or 1, but I tried it and it
   made the code unreadable. Better to waste a few bytes */
struct user_struct {
	char name[USER_NAME_LEN+1];
	char desc[USER_DESC_LEN+1];
	char pass[PASS_LEN+6];
	char in_phrase[PHRASE_LEN+1],out_phrase[PHRASE_LEN+1];
	char buff[BUFSIZE],site[81],last_site[81],page_file[81];
	char mail_to[WORD_LEN+1],revbuff[REVTELL_LINES][REVIEW_LEN+2];
	char afk_mesg[AFK_MESG_LEN+1],inpstr_old[REVIEW_LEN+1];
	struct room_struct *room,*invite_room;
	int type,port,site_port,login,socket,attempts,buffpos,filepos;
	int vis,ignall,prompt,command_mode,muzzled,charmode_echo; 
	int level,misc_op,remote_com,edit_line,charcnt,warned;
	int accreq,last_login_len,ignall_store,clone_hear,afk;
	int edit_op,colour,ignshouts,igntells,revline;
	time_t last_input,last_login,total_login,read_mail;
	char *malloc_start,*malloc_end;
	struct netlink_struct *netlink,*pot_netlink;
	struct user_struct *prev,*next,*owner;
	/* Added to generic NUTS code... Some of this is not currently implimented
	   but is stored for future update */
	char tname[80],tsite[80],tport[5],logout_room[ROOM_NAME_LEN];
	char copyto[MAX_COPIES][USER_NAME_LEN],invite_by[USER_NAME_LEN];
	char email[80],homepage[80],ignoreuser[MAX_IGNORES][USER_NAME_LEN];
        char call[USER_NAME_LEN],macros[10][MACRO_LEN],friend[MAX_FRIENDS][USER_NAME_LEN];
	char verify_code[80],afkbuff[REVTELL_LINES][REVIEW_LEN+2],editbuff[REVTELL_LINES][REVIEW_LEN+2];
        char samesite_check_store[ARR_SIZE],hang_word[WORD_LEN],hang_word_show[WORD_LEN],hang_guess[WORD_LEN];
	int wipe_from,wipe_to,wrap,unarrest,pager,logons,expire,lroom,monitor,vote;
	int gender,age,hideemail,misses,hits,kills,deaths,bullets,hps,alert,afkline,editline;
	int lmail_lev,welcomed,mail_verified,autofwd,editing,hwrap_lev,hwrap_com,show_pass;
	int ignpics,ignlogons,ignwiz,igngreets,ignbeeps,samesite_all_store,hang_stage;
	struct room_struct *wrap_room;
	time_t t_expire;
	};

typedef struct user_struct* UR_OBJECT;
UR_OBJECT user_first,user_last;

struct room_struct {
	char name[ROOM_NAME_LEN+1];
	char label[ROOM_LABEL_LEN+1];
	char desc[ROOM_DESC_LEN+1];
	char topic[TOPIC_LEN+1];
	char revbuff[REVIEW_LINES][REVIEW_LEN+2];
	int inlink; /* 1 if room accepts incoming net links */
	int access; /* public , private etc */
	int revline; /* line number for review */
	int mesg_cnt;
	char netlink_name[SERV_NAME_LEN+1]; /* temp store for config parse */
	char link_label[MAX_LINKS][ROOM_LABEL_LEN+1]; /* temp store for parse */
	struct netlink_struct *netlink; /* for net links, 1 per room */
	struct room_struct *link[MAX_LINKS];
	struct room_struct *next;
	};

typedef struct room_struct *RM_OBJECT;
RM_OBJECT room_first,room_last;
RM_OBJECT create_room();

/* Netlink stuff */
#define UNCONNECTED 0 
#define INCOMING 1 
#define OUTGOING 2
#define DOWN 0
#define VERIFYING 1
#define UP 2
#define ALL 0
#define IN 1
#define OUT 2

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

char *syserror="Sorry, a system error has occured";
char *nosuchroom="There is no such room.\n";
char *nosuchuser="There is no such user.\n";
char *notloggedon="There is no one of that name logged on.\n";
char *invisenter="A presence enters the room...\n";
char *invisleave="A presence leaves the room...\n";
char *invisname="A presence";
char *noswearing="Swearing is not allowed here.\n";

char *level_name[]={
"JAILED","NEW","USER","SUPER","WIZ","ARCH","GOD","*"
};

char *level_alias[]={
"|","_"," "," ","^","+","*"
};

char *command[]={
"quit",     "look",      "mode",      "say",      "shout [",
"tell >",   "emote ;:",  "semote &!", "pemote </","echo -",
"go",       "ignall",    "prompt",    "desc",     "inmsg",
"outmsg",   "public",    "private",   "knock",    "invite",
"topic",    "move",      "bcast",     "who @",    "people",
"help",     "shutdown",  "news",      "read",     "write",
"wipe",     "search",    "review",    "home",     "ustat",
"version",  "rmail",     "smail",     "dmail",    "from",
"entpro",   "examine",   "rooms",     "rnet",     "netstat",
"netdata",  "connect",   "disconnect","passwd",   "kill",
"promote",  "demote",    "lban",      "ban",      "unban",
"vis",      "invis",     "site",      "wake",     "twiz",
"muzzle",   "unmuzzle",  "map",       "logging",  "minlogin",
"system",   "charecho",  "clearline", "fix",      "unfix",
"viewlog",  "accreq",    "cbuff *",   "clone",    "destroy",
"myclones", "allclones", "switch",    "csay",     "chear",
"rstat",    "swban",     "afk",       "cls",      "colour",
"ignshout", "igntell",   "suicide",   "nuke",     "reboot",
"recount",  "revtell",   "purge",     "history",  "expire",
"bbcast",   "show '",    "ranks",     "wizlist",  "time",
"ctopic",   "copyto",    "nocopys",   "set",      "mutter",
"makevis",  "makeinvis", "plead",     "ptell",    "preview",
"picture",  "greet",     "think",     "sing",     "ewiz",
"suggest",  "rsug",      "dsug",      "last",     "macros",
"rules",    "uninvite",  "lmail",     "arrest",   "unarrest",
"verify",   "addhistory","forwarding","revshout", "cshout",
"ctells",   "monitor",   "call ,",    "uncall",   "ignlist",
"ignpics",  "ignwiz",    "igngreets", "ignlogons","ignuser",
"create",   "bfrom",     "samesite",  "save",     "shackle",
"unshackle","join",      "cemote",    "revafk",   "cafk",
"revedit",  "cedit",     "listen",    "hangman",  "guess",
"*"
};


/* Values of commands , used in switch in exec_com() */
enum comvals {
QUIT,     LOOK,       MODE,       SAY,      SHOUT,
TELL,     EMOTE,      SEMOTE,     PEMOTE,   ECHO,
GO,       IGNALL,     PROMPT,     DESC,     INPHRASE,
OUTPHRASE,PUBCOM,     PRIVCOM,    LETMEIN,  INVITE,
TOPIC,    MOVE,       BCAST,      WHO,      PEOPLE,
HELP,     SHUTDOWN,   NEWS,       READ,     WRITE,
WIPE,     SEARCH,     REVIEW,     HOME,     STATUS,
VER,      RMAIL,      SMAIL,      DMAIL,    FROM,
ENTPRO,   EXAMINE,    RMST,       RMSN,     NETSTAT,
NETDATA,  CONN,       DISCONN,    PASSWD,   KILL,
PROMOTE,  DEMOTE,     LISTBANS,   BAN,      UNBAN,
VIS,      INVIS,      SITE,       WAKE,     WIZSHOUT,
MUZZLE,   UNMUZZLE,   MAP,        LOGGING,  MINLOGIN,
SYSTEM,   CHARECHO,   CLEARLINE,  FIX,      UNFIX,
VIEWLOG,  ACCREQ,     REVCLR,     CREATE,   DESTROY,
MYCLONES, ALLCLONES,  SWITCH,     CSAY,     CHEAR,
RSTAT,    SWBAN,      AFK,        CLS,      COLOUR,
IGNSHOUTS,IGNTELLS,   SUICIDE,    DELETE,   REBOOT,
RECOUNT,  REVTELL,    PURGE,      HISTORY,  EXPIRE,
BBCAST,   SHOW,       RANKS,      WIZLIST,  TIME,
CTOPIC,   COPYTO,     NOCOPIES,   SET,      MUTTER,
MKVIS,    MKINVIS,    PLEAD,      PTELL,    PREVIEW,
PICTURE,  GREET,      THINK,      SING,     WIZEMOTE,
SUG,      RSUG,       DSUG,       LAST,     MACROS,
RULES,    UNINVITE,   LMAIL,      ARREST,   UNARREST,
VERIFY,   ADDHISTORY, FORWARDING, REVSHOUT, CSHOUT,
CTELLS,   MONITOR,    QCALL,      UNQCALL,  IGNLIST,
IGNPICS,  IGNWIZ,     IGNGREETS,  IGNLOGONS,IGNUSER,
ACCOUNT,  BFROM,      SAMESITE,   SAVEALL,  SHACKLE,
UNSHACKLE,JOIN,       CEMOTE,     REVAFK,   CAFK,
REVEDIT,  CEDIT,      LISTEN,     HANGMAN,  GUESS
} com_num;


/* These are the minimum levels at which the commands can be executed. 
   Alter to suit. */
int com_level[]={
JAILED,  NEW,     NEW,     JAILED,  SUPER,
NEW,     NEW,     SUPER,   USER,    USER,
USER,    USER,    NEW,     NEW,     USER,
USER,    USER,    USER,    USER,    USER,
USER,    WIZ,     WIZ,     JAILED,  WIZ,
JAILED,  GOD,     USER,    NEW,     USER,
USER,    SUPER,   USER,    USER,    USER,
NEW,     NEW,     USER,    USER,    USER,
USER,    USER,    USER,    USER,    SUPER,
ARCH,    GOD,     GOD,     USER,    ARCH,
WIZ,     WIZ,     WIZ,     ARCH,    ARCH,
ARCH,    ARCH,    WIZ,     USER,    WIZ,
WIZ,     WIZ,     USER,    GOD,     GOD,
SUPER,   NEW,     ARCH,    GOD,     GOD,
WIZ,     NEW,     USER,    ARCH,    ARCH,
ARCH,    SUPER,   ARCH,    ARCH,    ARCH,
WIZ,     ARCH,    USER,    NEW,     NEW,
USER,    USER,    NEW,     GOD,     ARCH,
GOD,     USER,    GOD,     WIZ,     GOD,
ARCH,    SUPER,   NEW,     NEW,     USER,
SUPER,   USER,    USER,    NEW,     USER,
ARCH,    ARCH,    JAILED,  USER,    USER,
SUPER,   SUPER,   USER,    USER,    WIZ,
USER,    WIZ,     GOD,     USER,    USER,
JAILED,  USER,    ARCH,    WIZ,     WIZ,
NEW,     WIZ,     GOD,     USER,    SUPER,
USER,    WIZ,     USER,    USER,    USER,
USER,    WIZ,     USER,    USER,    USER,
WIZ,     USER,    WIZ,     WIZ,     ARCH,
ARCH,    SUPER,   ARCH,    USER,    USER,
USER,    USER,    USER,    USER,    USER
};

/* 
Colcode values equal the following:
RESET,BOLD,BLINK,REVERSE

Foreground & background colours in order..
BLACK,RED,GREEN,YELLOW/ORANGE,
BLUE,MAGENTA,TURQUIOSE,WHITE
*/

char *colcode[NUM_COLS]={
/* Standard stuff */
"\033[0m", "\033[1m", "\033[4m", "\033[5m", "\033[7m",
/* Foreground colour */
"\033[30m","\033[31m","\033[32m","\033[33m",
"\033[34m","\033[35m","\033[36m","\033[37m",
/* Background colour */
"\033[40m","\033[41m","\033[42m","\033[43m",
"\033[44m","\033[45m","\033[46m","\033[47m"
};

/* Codes used in a string to produce the colours when prepended with a '~' */
char *colcom[NUM_COLS]={
"RS","OL","UL","LI","RV",
"FK","FR","FG","FY",
"FB","FM","FT","FW",
"BK","BR","BG","BY",
"BB","BM","BT","BW"
};


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

char *sex[]={"Neuter","Male","Female"};

/* default rooms */
char *default_jail="jail";
char *default_warp="drive";

/* The rooms listed here are just examples of what can be added
   You may add more or remove as many as you like, but you MUST
   keep the stopping clause in */
struct { 
  char *name; int level; 
  } priv_room[]={
      { "wizzes",WIZ }, /* a room for wizzes+ only */
      { "gods_place",GOD }, /* only top people can get in this place! */
      { "*",0 } /* stopping clause */
    };


/* These MUST be in lower case - the contains_swearing() function converts
   the string to be checked to lower case before it compares it against
   these. Also even if you dont want to ban any words you must keep the 
   GOD as the first element in the array. */
char *swear_words[]={
"fuck","shit","cunt","cock","bastard","dyke","fag","pussy","bitch","*"
};

char verification[SERV_NAME_LEN+1];
char text[ARR_SIZE*2];
char word[MAX_WORDS][WORD_LEN+1];
char wrd[8][81];
char progname[40],confile[40];
time_t boot_time;
jmp_buf jmpvar;

int port[3],listen_sock[3],wizport_level,minlogin_level;
int colour_def,password_echo,ignore_sigterm;
int max_users,max_clones,num_of_users,num_of_logins,heartbeat;
int login_idle_time,user_idle_time,config_line,word_count;
int tyear,tmonth,tday,tmday,twday,thour,tmin,tsec;
int mesg_life,system_logging,prompt_def,no_prompt;
int force_listen,gatecrash_level,min_private_users;
int ignore_mp_level,rem_user_maxlevel,rem_user_deflevel;
int destructed,mesg_check_hour,mesg_check_min,net_idle_time;
int keepalive_interval,auto_connect,ban_swearing,crash_action;
int time_out_afks,allow_caps_in_name,rs_countdown;
int charecho_def,time_out_maxlevel,auto_purge;
time_t rs_announce,rs_which;
UR_OBJECT rs_user;

extern char *sys_errlist[];
char *long_date();

int logons_old,logons_new,user_number,jailed_cnt,new_cnt,user_cnt,super_cnt;
int wiz_cnt,arch_cnt,god_cnt,purge_count,users_purged,sug_num;
time_t purge_date;
struct { char name[30]; int level; } ordcom[NUM_CMDS];
char shoutbuff[REVIEW_LINES][REVIEW_LEN+2];
int sbuffline,forwarding,logon_flag;

/* for setting of attributes */
struct {
    char *type;
    char *desc;
    } setstr[]={
        {"show","show the current attributes setting"},
        {"gender","sets your gender (male, female, or neuter)"},
        {"age","set your age for people to see"},
        {"email","enter your email address"},
        {"homepage","enter your homepage address"},
        {"hide","makes your email visible to only you and the law, or everyone (toggle)"},
        {"wrap","sets screen wrap to be on or off (toggle)"},
        {"pager","sets how many lines per page of the pager you get"},
        {"colour","display in colour or not (toggle)"},
        {"room","lets you log back into the room you left from, if public (toggle)"},
	{"autofwd","lets you receive smails via your email address."},
	{"password","lets you see your password when entering it at the login (toggle)"},
        {"*",""}
    };
enum setval {
    SETSHOW,        SETGEND,    SETAGE,     SETEMAIL,       SETHOMEP,
    SETHIDEEMAIL,   SETWRAP,    SETPAGER,   SETCOLOUR,      SETROOM,
    SETFWD,         SETPASSWD
    } set_val;


char *talker_signature=
"+--------------------------------------------------------------------------+
|  This message has been smailed to you on The Amnuts Talker, and this is  |
|      your auto-forward.  Please do not reply directly to this email.     |
|                                                                          |
|               Amnuts - A talker running at foo.bar.com 666               |
|         email 'me@my.place' if you have any questions/comments           |
+--------------------------------------------------------------------------+\n";


/* Big letter array map - for greet() */
int biglet[26][5][5] = {
    0,1,1,1,0,1,0,0,0,1,1,1,1,1,1,1,0,0,0,1,1,0,0,0,1,
    1,1,1,1,0,1,0,0,0,1,1,1,1,1,0,1,0,0,0,1,1,1,1,1,0,
    0,1,1,1,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,1,1,1,
    1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,1,1,1,0,
    1,1,1,1,1,1,0,0,0,0,1,1,1,1,0,1,0,0,0,0,1,1,1,1,1,
    1,1,1,1,1,1,0,0,0,0,1,1,1,1,0,1,0,0,0,0,1,0,0,0,0,
    0,1,1,1,0,1,0,0,0,0,1,0,1,1,0,1,0,0,0,1,0,1,1,1,0,
    1,0,0,0,1,1,0,0,0,1,1,1,1,1,1,1,0,0,0,1,1,0,0,0,1,
    0,1,1,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,1,1,0,
    0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,0,0,0,1,0,1,1,1,0,
    1,0,0,0,1,1,0,0,1,0,1,0,1,0,0,1,0,0,1,0,1,0,0,0,1,
    1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,1,1,1,
    1,0,0,0,1,1,1,0,1,1,1,0,1,0,1,1,0,0,0,1,1,0,0,0,1,
    1,0,0,0,1,1,1,0,0,1,1,0,1,0,1,1,0,0,1,1,1,0,0,0,1,
    0,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0,
    1,1,1,1,0,1,0,0,0,1,1,1,1,1,0,1,0,0,0,0,1,0,0,0,0,
    0,1,1,1,0,1,0,0,0,1,1,0,1,0,1,1,0,0,1,1,0,1,1,1,0,
    1,1,1,1,0,1,0,0,0,1,1,1,1,1,0,1,0,0,1,0,1,0,0,0,1,
    0,1,1,1,1,1,0,0,0,0,0,1,1,1,0,0,0,0,0,1,1,1,1,1,0,
    1,1,1,1,1,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,
    1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,1,1,1,1,
    1,0,0,0,1,1,0,0,0,1,0,1,0,1,0,0,1,0,1,0,0,0,1,0,0,
    1,0,0,0,1,1,0,0,0,1,1,0,1,0,1,1,1,0,1,1,1,0,0,0,1,
    1,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0,1,0,0,0,1,
    1,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,
    1,1,1,1,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,1,1,1,1
    };

/* Symbol array map - for greet() */
int bigsym[32][5][5] = {
    0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,
    0,1,0,1,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,1,0,1,0,1,1,1,1,1,0,1,0,1,0,1,1,1,1,1,0,1,0,1,0,
    0,1,1,1,1,1,0,1,0,0,0,1,1,1,0,0,0,1,0,1,1,1,1,1,0,
    1,1,0,0,1,1,1,0,1,0,0,0,1,0,0,0,1,0,1,1,1,0,0,1,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,1,1,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,1,0,
    0,1,1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,1,1,0,0,
    1,0,1,0,1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,0,1,
    0,0,1,0,0,0,0,1,0,0,1,1,1,1,1,0,0,1,0,0,0,0,1,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0,0,
    0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,0,0,
    0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,
    0,1,1,1,0,1,0,0,1,1,1,0,1,0,1,1,1,0,0,1,0,1,1,1,0,
    0,0,1,0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,1,1,0,
    1,1,1,1,0,0,0,0,0,1,0,1,1,1,0,1,0,0,0,0,1,1,1,1,1,
    1,1,1,1,0,0,0,0,0,1,0,1,1,1,0,0,0,0,0,1,1,1,1,1,0,
    0,0,1,1,0,0,1,0,0,0,1,0,0,1,0,1,1,1,1,1,0,0,0,1,0,
    1,1,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,1,1,1,1,1,0,
    0,1,1,1,0,1,0,0,0,0,1,1,1,1,0,1,0,0,0,1,0,1,1,1,0,
    1,1,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,
    0,1,1,1,0,1,0,0,0,1,0,1,1,1,0,1,0,0,0,1,0,1,1,1,0,
    1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,0,0,0,0,1,0,0,0,0,1,
    0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,
    0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,
    0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
    0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,
    0,1,1,1,1,0,0,0,0,1,0,0,1,1,1,0,0,0,0,0,0,0,1,0,0,
    0,1,0,0,0,1,0,1,1,1,1,0,1,0,1,1,0,1,1,1,0,1,1,1,0
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






