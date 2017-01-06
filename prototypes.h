/*****************************************************************************
                Prototypes header file for Amnuts version 2.1.1
        Copyright (C) Andrew Collington - Last update: 25th May, 1999
 *****************************************************************************/

#define args(list) list

int       main args((int argc, char *argv[]));

/* string functions - comparisons, convertions, etc */

int       get_charclient_line args((UR_OBJECT user, char *inpstr, int len));
void      terminate args((char *str));
int       wordfind args((char *inpstr));
void      clear_words args((void));
int       yn_check args((char *wd));
int       onoff_check args((char *wd));
int       minmax_check args((char *wd));
void      echo_off args((UR_OBJECT user));
void      echo_on args((UR_OBJECT user));
char *    remove_first args((char *inpstr));
int       contains_swearing args((char *str));
char *    censor_swear_words args((char *has_swears));
int       colour_com_count args((char *str));
char *    colour_com_strip args((char *str));
void      strtoupper args((char *str));
void      strtolower args((char *str));
int       is_number args((char *str));
char *    istrstr args((char *str, char *pat));
char *    replace_string args((char *inpstr,char *old,char *new));
int       instr args((char *s1,char *s2));
void      midcpy args((char *strf,char *strt,int fr,int to));
char *    ordinal_text args((int num));
char *    long_date args((int which));
void      smiley_type args((char *str, char *type));

/* Object functions */

UR_OBJECT create_user args((void));
void      reset_user args((UR_OBJECT user));
void      destruct_user args((UR_OBJECT user));
RM_OBJECT create_room args((void));
void      destruct_room args((RM_OBJECT rm));
int       add_command args((int cmd_id));
int       rem_command args((int cmd_id));
int       add_user_node args((char *name,int level));
int       rem_user_node args((char *name,int lev));
void      add_user_date_node args((char *name,char *date));
int       add_wiz_node args((char *name,int level));
int       rem_wiz_node args((char *name));
int       user_list_level args((char *name, int lvl));

/* performs checks and searchs */

void      check_directories args((void));
int       pattern_match args((char *str,char *pat));
int       site_banned args((char *sbanned,int new));
int       user_banned args((char *name));
void      reset_access args((RM_OBJECT rm));
UR_OBJECT get_user args((char *name));
UR_OBJECT get_user_name args((UR_OBJECT user,char *i_name));
RM_OBJECT get_room args((char *name));
RM_OBJECT get_room_full args((char *name));
int       get_level args((char *name));
int       has_room_access args((UR_OBJECT user,RM_OBJECT rm));
void      check_start_room args((UR_OBJECT user));
int       find_user_listed args((char *name));
int       validate_email args((char *email));
int       user_logged_on args((char *name));
int       in_private_room args((UR_OBJECT user));
int       has_gcom args((UR_OBJECT user,int cmd_id));
int       has_xcom args((UR_OBJECT user,int cmd_id));
int       is_personal_room args((RM_OBJECT rm));
int       is_my_room args((UR_OBJECT user,RM_OBJECT rm));
int       room_visitor_count args((RM_OBJECT rm));
int       has_room_key args((char *visitor,RM_OBJECT rm));

/* setting up of sockets */

void      setup_readmask args((fd_set *mask));
void      accept_connection args((int lsock,int num));
char *    get_ip_address args((struct sockaddr_in acc_addr));
char *    resolve_ip args((char *host));
void      init_sockets args((void));

/* loading up and parsing of the configuration files */

void      load_and_parse_config args((void));
void      parse_init_section args((void));
void      parse_rooms_section args((void));
void      parse_topics_section args((char *topic));
#ifdef NETLINKS
  void      parse_sites_section args((void));
#endif
void      parse_user_rooms args((void));

/* signal handlers and exit functions */

void      init_signals args((void));
void      sig_handler args((int sig));
void      boot_exit args((int code));

/* event functions */

void      do_events args((int sig));
void      reset_alarm args((void));
void      check_reboot_shutdown args((void));
void      check_idle_and_timeout args((void));
void      check_messages args((UR_OBJECT user, int chforce));
void      record_last_login args((char *name));
void      record_last_logout args((char *name));

/* initializing of the globals and other stuff */

void      init_globals args((void));
int       load_user_details args((UR_OBJECT user));
int       save_user_details args((UR_OBJECT user,int save_current));
int       load_oldversion_user args((UR_OBJECT user,int version));
void      set_date_time args((void));
void      process_users args((void));
void      count_users args((void));
void      parse_commands args((void));
void      count_suggestions args((void));
int       count_motds args((int forcecnt));
int       get_motd_num args((int motd));

/* file functions - reading, writing, counting of lines, etc */

void      clean_files args((char *name));
int       remove_top_bottom args((char *filename,int where));
int       count_lines args((char *filename));

/* write functions - users, rooms, system logs, etc */

void      write_sock args((int sock, char *str));
void      write_user args((UR_OBJECT user, char *str));
void      write_level args((int level,int above,char *str,UR_OBJECT user));
void      write_room args((RM_OBJECT rm,char *str));
void      write_room_except args((RM_OBJECT rm,char *str,UR_OBJECT user));
void      write_friends args((UR_OBJECT user,char *str,int revt));
void      write_syslog args((char *str,int write_time,int type));
void      record_last_command args((UR_OBJECT user,int comnum));
void      dump_commands args((int foo));
void      write_monitor args((UR_OBJECT user,RM_OBJECT rm,int rec));
int       more args((UR_OBJECT user,int sock,char *filename));
int       more_users args((UR_OBJECT user));
void      add_history args((char *name,int showtime,char *str));

/* logon/off functions */

void      login args((UR_OBJECT user, char *inpstr));
void      attempts args((UR_OBJECT user));
void      connect_user args((UR_OBJECT user));
void      cls args((UR_OBJECT user));
void      disconnect_user args((UR_OBJECT user));

/* misc and line editor functions */

int       misc_ops args((UR_OBJECT user, char *inpstr));
void      editor args((UR_OBJECT user, char *inpstr));
void      editor_done args((UR_OBJECT user));

/* user command functions and their subsids */

int       exec_com args((UR_OBJECT user, char *inpstr));
void      talker_shutdown args((UR_OBJECT user,char *str,int reboot));
void      shutdown_com args((UR_OBJECT user));
void      reboot_com args((UR_OBJECT user));
void      record args((RM_OBJECT rm,char *str));
void      record_tell args((UR_OBJECT user, char *str));
void      record_shout args((char *str));
void      record_afk args((UR_OBJECT user, char *str));
void      clear_afk args((UR_OBJECT user));
void      record_edit args((UR_OBJECT user, char *str));
void      clear_revbuff args((RM_OBJECT rm));
void      clear_tells args((UR_OBJECT user));
void      clear_shouts args((void));
void      clear_edit args((UR_OBJECT user));
void      cls args((UR_OBJECT user));
int       send_mail args((UR_OBJECT user,char *to,char *ptr,int iscopy));
#ifdef NETLINKS
  void      send_external_mail args((NL_OBJECT nl,UR_OBJECT user,char *to,char *ptr));
#endif
void      smail args((UR_OBJECT user, char *inpstr,int done_editing));
void      rmail args((UR_OBJECT user));
void      read_specific_mail args((UR_OBJECT user));
void      read_new_mail args((UR_OBJECT user));
void      dmail args((UR_OBJECT user));
void      mail_from args((UR_OBJECT user));
void      copies_to args((UR_OBJECT user));
void      send_copies args((UR_OBJECT user,char *ptr));
void      level_mail args((UR_OBJECT user, char *inpstr,int done_editing));
int       send_broadcast_mail args((UR_OBJECT user,char *ptr,int lvl,int type));
int       mail_sizes args((char *name,int type));
int       reset_mail_counts args((UR_OBJECT user));
void      set_forward_email args((UR_OBJECT user));
void      verify_email args((UR_OBJECT user));
void      forward_email args((char *name,char *from,char *message));
int       send_forward_email args((char *send_to,char *mail_file));
int       double_fork args((void));
void      read_board args((UR_OBJECT user));
void      read_board_specific args((UR_OBJECT user,RM_OBJECT rm,int msg_number));
void      write_board args((UR_OBJECT user, char *inpstr,int done_editing));
void      wipe_board args((UR_OBJECT user));
int       check_board_wipe args((UR_OBJECT user));
void      board_from args((UR_OBJECT user));
void      search_boards args((UR_OBJECT user));
void      suggestions args((UR_OBJECT user,int done_editing));
void      delete_suggestions args((UR_OBJECT user));
void      suggestions_from args((UR_OBJECT user));
int       get_wipe_parameters args((UR_OBJECT user));
int       wipe_messages args((char *filename,int from,int to,int type));
void      listbans args((UR_OBJECT user));
void      ban args((UR_OBJECT user));
void      ban_site args((UR_OBJECT user));
void      ban_user args((UR_OBJECT user));
void      ban_new args((UR_OBJECT user));
void      unban args((UR_OBJECT user));
void      unban_site args((UR_OBJECT user));
void      unban_user args((UR_OBJECT user));
void      unban_new args((UR_OBJECT user));
void      look args((UR_OBJECT user));
void      who args((UR_OBJECT user,int type));
void      help args((UR_OBJECT user));
void      help_commands_level args((UR_OBJECT user,int is_wrap));
void      help_commands_function args((UR_OBJECT user,int is_wrap));
void      help_credits args((UR_OBJECT user));
void      say args((UR_OBJECT user, char *inpstr));
void      say_to args((UR_OBJECT, char*inpstr));
void      shout args((UR_OBJECT user, char *inpstr));
void      tell args((UR_OBJECT user, char *inpstr));
void      emote args((UR_OBJECT user, char *inpstr));
void      semote args((UR_OBJECT user, char *inpstr));
void      pemote args((UR_OBJECT user, char *inpstr));
void      echo args((UR_OBJECT user, char *inpstr));
void      mutter args((UR_OBJECT user, char *inpstr));
void      plead args((UR_OBJECT user, char *inpstr));
void      wizshout args((UR_OBJECT user, char *inpstr));
void      wizemote args((UR_OBJECT user, char *inpstr));
void      picture_tell args((UR_OBJECT user));
void      preview args((UR_OBJECT user));
void      picture_all args((UR_OBJECT user));
void      greet args((UR_OBJECT user, char *inpstr));
void      think_it args((UR_OBJECT user, char *inpstr));
void      sing_it args((UR_OBJECT user, char *inpstr));
void      bcast args((UR_OBJECT user, char *inpstr,int beeps));
void      wake args((UR_OBJECT user));
void      beep args((UR_OBJECT user,char *inpstr));
void      quick_call args((UR_OBJECT user));
void      revedit args((UR_OBJECT user));
void      revafk args((UR_OBJECT user));
void      revclr args((UR_OBJECT user));
void      revshout args((UR_OBJECT user));
void      revtell args((UR_OBJECT user));
void      review args((UR_OBJECT user));
void      status args((UR_OBJECT user));
void      examine args((UR_OBJECT user));
void      set_attributes args((UR_OBJECT user, char *inpstr));
void      show_attributes args((UR_OBJECT user));
void      prompt args((UR_OBJECT user));
void      toggle_prompt args((UR_OBJECT user));
void      toggle_mode args((UR_OBJECT user));
void      toggle_charecho args((UR_OBJECT user));
void      set_desc args((UR_OBJECT user, char *inpstr));
void      set_iophrase args((UR_OBJECT user, char *inpstr));
void      enter_profile args((UR_OBJECT user,int done_editing));
void      account_request args((UR_OBJECT user, char *inpstr));
void      afk args((UR_OBJECT user, char *inpstr));
void      get_macros args((UR_OBJECT user));
void      macros args((UR_OBJECT user));
void      check_macros args((UR_OBJECT user, char *inpstr));
void      visibility args((UR_OBJECT user,int vis));
void      make_invis args((UR_OBJECT user));
void      make_vis args((UR_OBJECT user));
void      show_igusers args((UR_OBJECT user));
int       check_igusers args((UR_OBJECT user,UR_OBJECT ignoring));
void      set_igusers args((UR_OBJECT user));
void      set_ignore args((UR_OBJECT user));
void      show_ignlist args((UR_OBJECT user));
void      toggle_ignall args((UR_OBJECT user));
void      user_listen args((UR_OBJECT user));
void      create_clone args((UR_OBJECT user));
void      destroy_user_clones args((UR_OBJECT user));
void      destroy_clone args((UR_OBJECT user));
void      myclones args((UR_OBJECT user));
void      allclones args((UR_OBJECT user));
void      clone_switch args((UR_OBJECT user));
void      clone_say args((UR_OBJECT user, char *inpstr));
void      clone_emote args((UR_OBJECT user, char *inpstr));
void      clone_hear args((UR_OBJECT user));
void      go args((UR_OBJECT user));
void      move_user args((UR_OBJECT user,RM_OBJECT rm,int teleport));
void      move args((UR_OBJECT user));
void      set_room_access args((UR_OBJECT user));
void      change_room_fix args((UR_OBJECT user,int fix));
void      invite args((UR_OBJECT user));
void      uninvite args((UR_OBJECT user));
void      letmein args((UR_OBJECT user));
void      rooms args((UR_OBJECT user,int show_topics,int wrap));
void      clear_topic args((UR_OBJECT user));
void      join args((UR_OBJECT user));
void      shackle args((UR_OBJECT user));
void      unshackle args((UR_OBJECT user));
void      set_topic args((UR_OBJECT user, char *inpstr));
void      check_autopromote args((UR_OBJECT user,int attrib));
void      promote args((UR_OBJECT user));
void      demote args((UR_OBJECT user));
void      muzzle args((UR_OBJECT user));
void      unmuzzle args((UR_OBJECT user));
void      arrest args((UR_OBJECT user));
void      unarrest args((UR_OBJECT user));
void      change_pass args((UR_OBJECT user));
void      kill_user args((UR_OBJECT user));
void      suicide args((UR_OBJECT user));
void      delete_user args((UR_OBJECT user,int this_user));
int       purge args((int type,char *purge_site,int purge_days));
void      purge_users args((UR_OBJECT user));
void      user_expires args((UR_OBJECT user));
void      create_account args((UR_OBJECT user));
void      force_save args((UR_OBJECT user));
void      viewlog args((UR_OBJECT user));
void      show_last_login args((UR_OBJECT user));
void      samesite args((UR_OBJECT user,int stage));
void      site args((UR_OBJECT user));
void      manual_history args((UR_OBJECT user, char *inpstr));
void      user_history args((UR_OBJECT user));
void      logging args((UR_OBJECT user));
void      minlogin args((UR_OBJECT user));
void      system_details args((UR_OBJECT user));
void      clearline args((UR_OBJECT user));
void      toggle_swearban args((UR_OBJECT user));
void      display_colour args((UR_OBJECT user));
void      show args((UR_OBJECT user, char *inpstr));
void      show_ranks args((UR_OBJECT user));
void      wiz_list args((UR_OBJECT user));
void      get_time args((UR_OBJECT user));
void      show_version args((UR_OBJECT user));
void      show_memory args((UR_OBJECT user));
void      play_hangman args((UR_OBJECT user));
char *    get_hang_word args((char *aword));
void      guess_hangman args((UR_OBJECT user));
void      retire_user args((UR_OBJECT user));
void      unretire_user args((UR_OBJECT user));
int       in_retire_list args((char *name));
void      add_retire_list args((char *name));
void      clean_retire_list args((char *name));
void      show_command_counts args((UR_OBJECT user));
void      recount_users args((UR_OBJECT user,int ok));
void      set_command_level args((UR_OBJECT user));
void      grep_users args((UR_OBJECT user));
void      shoot_user args((UR_OBJECT user));
void      reload_gun args((UR_OBJECT user));
void      set_command_level args((UR_OBJECT user));
void      user_xcom args((UR_OBJECT user));
void      user_gcom args((UR_OBJECT user));
int       set_xgcom args((UR_OBJECT user,UR_OBJECT u,int id,int banned,int set));
int       get_xgcoms args((UR_OBJECT user));
void      reload_room_description args((UR_OBJECT user));

/* friends stuff */

int       user_is_friend args((UR_OBJECT user, UR_OBJECT u));
void      alert_friends args((UR_OBJECT user));
void      get_friends args((UR_OBJECT user));
void      friends args((UR_OBJECT user));
void      friend_say args((UR_OBJECT user, char *inpstr));
void      friend_emote args((UR_OBJECT user, char *inpstr));
void      bring args((UR_OBJECT user));
void      force args((UR_OBJECT user,char *inpstr));

/* calendar stuff */

int       is_leap args((unsigned yr));
unsigned  months_to_days args((unsigned mn));
long      years_to_days args((unsigned yr));
long      ymd_to_scalar args((unsigned yr,unsigned mo,unsigned dy));
void      scalar_to_ymd args((long scalar,unsigned *yr,unsigned *mo,unsigned *dy));
int       is_ymd_today args((unsigned yr,unsigned mo,unsigned dy));
void      show_calendar args((UR_OBJECT user));

/* personal rooms stuff */

void      personal_room args((UR_OBJECT user));
void      personal_room_lock args((UR_OBJECT user));
void      personal_room_visit args((UR_OBJECT user));
void      personal_room_decorate args((UR_OBJECT user,int done_editing));
int       personal_room_store args((char *name,int store,RM_OBJECT rm));
void      personal_room_admin args((UR_OBJECT user));
void      personal_room_key args((UR_OBJECT user));
int       personal_key_add args((UR_OBJECT user, char *name));
int       personal_key_remove args((UR_OBJECT user, char *name));
void      personal_room_bgone args((UR_OBJECT user));
