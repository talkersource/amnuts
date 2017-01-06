/*****************************************************************************
             Amncat version 1.0.0 - Copyright (C) Andrew Collington
                        Last update: 25th May, 1999

      email: amnuts@iname.com    homepage: http://www.talker.com/amnuts/
                  personal: http://www.andyc.dircon.co.uk/
 *****************************************************************************/


#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define DEFAULT_NAME "Snarfo"
#define ARR_SIZE 1000
#define OUT_BUFF_SIZE 1000
#define SIZEOF(table) (sizeof(table)/sizeof(table[0]))

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

/* prototype */
int cat (char *filename);



/* main routine */
void main(int argc, char *argv[])
{
  if (argc<2) {
    printf("\nUsage: %s <filename>\n\n",argv[0]);
    exit(1);
    }
  if (!cat(argv[1])) {
    printf("\n%s: file not found\n\n",argv[0]);
    exit(1);
    }
  exit(0);
}


/* cat routine */
int cat(char *filename)
{
int i,buffpos;
char buff[OUT_BUFF_SIZE],*str;
FILE *fp;
char text[ARR_SIZE*2];

buff[0]='\0';
text[0]='\0';
buffpos=0;
str="";

/* check if file exists */
if (!(fp=fopen(filename,"r")))  return 0;

fgets(text,sizeof(text)-1,fp);
/* Go through file */
while(!feof(fp)) {
  str=text;
  /* Process line from file */
  while(*str) {
    if (*str=='\n') {
      if (buffpos>OUT_BUFF_SIZE-6) {
	buff[buffpos]='\0';
	printf("%s",buff);  buffpos=0;  buff[0]='\0';
        }
      /* Reset terminal before every newline */
      memcpy(buff+buffpos,"\033[0m",4);  buffpos+=4;
      *(buff+buffpos)='\n';  *(buff+buffpos+1)='\r';  
      buffpos+=2;  ++str;
      }
    else {  
      /* Process colour commands in the file */
      if (*str=='^' && *(str+1)=='~') {  ++str;  continue;  }
      if (str!=text && *str=='~' && *(str-1)=='^') {
	*(buff+buffpos)=*str;  goto CONT;
        }
      if (*str=='~') {
	++str;
	/* process if user name variable */
	if (*str=='$') {
	  if (buffpos>OUT_BUFF_SIZE-strlen(DEFAULT_NAME)) {
	    buff[buffpos]='\0';
	    printf("%s",buff);  buffpos=0;  buff[0]='\0';
	    }
	  memcpy(buff+buffpos,DEFAULT_NAME,strlen(DEFAULT_NAME));
	  buffpos+=strlen(DEFAULT_NAME)-1;
	  goto CONT;
	  }
	/* process if colour variable */
	if (buffpos>OUT_BUFF_SIZE-6) {
	  buff[buffpos]='\0';
	  printf("%s",buff);  buffpos=0;  buff[0]='\0';
	  }
	for(i=0;i<NUM_COLS;++i) {
	  if (!strncmp(str,colour_codes[i].txt_code,2)) {
	    memcpy(buffpos+buff,colour_codes[i].esc_code,strlen(colour_codes[i].esc_code));
	    buffpos+=strlen(colour_codes[i].esc_code)-1;
	    ++str;
	    goto CONT;
	    }
	  }
	*(buff+buffpos)=*(--str);
        }
      else *(buff+buffpos)=*str;
    CONT:
      ++buffpos;   ++str;
      }
    if (buffpos==OUT_BUFF_SIZE) {
      buff[buffpos]='\0';
      printf("%s",buff);  buffpos=0;  buff[0]='\0';
      }
    }
  fgets(text,sizeof(text)-1,fp);
  }
if (buffpos) {
  buff[buffpos]='\0';
  printf("%s",buff);
  }
fclose(fp);
return 1;
}

/****************************************************************************/
