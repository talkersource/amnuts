#!/usr/bin/perl

#=-------------------------------------------------------------------
#= Filename: purge.pl
#= Author  : Andrew Collington, andyc@dircon.co.uk
#=
#= Created : 5th December, 1997
#= Revised : 5th December, 1997
#= Version : 1.0
#=
#= This program allows you to purge all the users that have not been
#= online for a set amount of time.  This program doesn't require
#= that you use any particular talker code as it should work on any
#= user file.
#=-------------------------------------------------------------------
#= This is original code and copyrighted, but feel free to distribute
#= it to whoever wants it.  Just please keep this text at the top of
#= the program to show that it is my original program, and also my
#= text at the top of the program when it runs.  Thanks :)
#= Although this program has been tested, I can not be held in any
#= way responsible for any damage that occurs as a result of using it.
#=-------------------------------------------------------------------
#= Tested only with perl version 5.003 so I apologize if it doesn't
#= work on any other versions.
#=-------------------------------------------------------------------


#=-- Set up some variables ------------------------------------------

$userdir='userfiles';  # what directory to load files from
$backup='USERS';       # what name you want the backup file to have
$filetype='D';         # what name of files you want to alter
$time_line=1;     # what line the 'last logged in' variable is on
$time_num=1;      # what variable number along the line it is
$dateprogram='/bin/date';
&getdate;

#=-------------------------------------------------------------------



print "+-------------------------------------------------------------------+\n";
print "|     This is a Perl program to purge userfiles for your talker     |\n";
print "+-------------------------------------------------------------------+\n";
print "| Andrew Collington            |     Way Out West : talker.com 2574 |\n";
print "| email: andyc\@dircon.co.uk    |         http://www.talker.com/west |\n";
print "+-------------------------------------------------------------------+\n\n";

opendir(DIR,$userdir) || die "ERROR: Cannot open the directory '$userdir': $!\n";
local(@filenames)=readdir(DIR);
closedir(DIR);
@filenames=sort(@filenames);

# Get list of all $filetype user files
foreach $file (@filenames) {
    if ($file=~/\.$filetype/) {
	$file=~s/\.$filetype//;
	push(@users,$file);
    }
}

$tmp=@users;
print "+-------------------------------------------------------------------+\n";
printf "| Loading file from directory: %-20s File type: .%-3s |\n",$userdir,$filetype;
printf "| Number of users to check   : %-4d                                 |\n",$tmp;
print "+-------------------------------------------------------------------+\n";

#=-- Give and example of userfile to get variable line --------------

open(FP,"$userdir/$users[1].$filetype") || die "Could not open userfile '$userdir/$users[1].$filetype' for example: $!\n";
@user_lines=<FP>;
close(FP);
print "\nAn example userfile looks like:\n\n";
$line_cnt=0;
foreach $line (@user_lines) {
    chomp($line);
    printf "   %2s> $line\n",++$line_cnt;
} # End foreach

#=-- Ask user what line to change and what to add -------------------

$is_get_var_ok="no";
while($is_get_var_ok=~/^n/i) {

    #=-- Get the number of the line variable in on ------------------

    $is_answer_ok="no";
    while($is_answer_ok=~/^n/i) {
	print "\nLast log in time on line? [1-$line_cnt] : ";
	$from_line=<STDIN>;
	chomp($from_line);
	if (($from_line=~/[a-zA-Z]/g) || ($from_line>$line_cnt) || ($from_line eq "0") || ($from_line eq "")) {
	    $is_answer_ok="no";
	    print "There was an error with the line count.  Try again.\n";
	}
	else { $is_answer_ok="yes"; }
    } # End while

    #=-- Determine how many variables are on the given line ---------

    my(@temp_vars)=split(/ /,$user_lines[$from_line-1]);
    $var_cnt=@temp_vars;
    printf "\n  %2d> $user_lines[$from_line-1]\n",$from_line;

    #=-- Get the number of the variable along the line --------------

    $is_answer_ok="no";
    while($is_answer_ok=~/^n/i) {
	print "\nLast log in time variable number? [1-$var_cnt] : ";
	$from_num=<STDIN>;
	chomp($from_num);
	if (($from_num=~/[a-zA-Z]/g) || ($from_num>$var_cnt) || ($from_num eq "0") || ($from_num eq "")) {
	    $is_answer_ok="no";
	    print "There was an error with the variable count.  Try again.\n";
	}
	else { $is_answer_ok="yes"; }
    } # End while

    #=-- Display the info so far ------------------------------------

    print "\nThe last log in time is on line $from_line, variable number $from_num.\n";
    print "(in this example '$temp_vars[$from_num-1]')\n\n";
    print "Is this correct? [y/n/q] : ";
    $is_get_var_ok=<STDIN>;
    chomp($is_get_var);
    if ($is_get_var_ok=~/^q/i) {
	print "\nYou have quit the program.  No userfiles were purged.\n\n";
	print "+-------------------------------------------------------------------+\n";
	print "|               Thank you for using this program                    |\n";
	print "|     If you have any questions or comments, please email me        |\n";
	print "+-------------------------------------------------------------------+\n\n";
	exit(1);
    }
    elsif ($is_get_var_ok=~/^y/i) { $is_get_var_ok="yes"; }
    else { $is_get_var_ok="no"; }
}  # End while


$from_line--;   # the arrays are indexed from 0, though when entering
$from_num--;    # we index from 1.

#=-- Ask to backup the files ----------------------------------------

$is_answer_ok="error";
while(!($is_answer_ok=~/^[nNyY]/)) {
    print "Do you wish to back up ALL the files first? [y/n] : ";
    $backup_files=<STDIN>;
    chomp($backup_files);
    if ($backup_files=~/^y/i) {
	print "Backing up now.  Please be patient...\n";
	system("tar -cf $backup$date.tar $userdir/*");
	system("gzip $backup$date.tar");
	$is_answer_ok="yes";
    }
    elsif ($backup_files=~/^n/i) { $is_answer_ok="no"; }
    else {
	print "You must select either 'y' or 'n'.\n";
	$is_answer_ok="error";
    }
}
if ($backup_files=~/^y/i) {
    print "You backed up ALL the old files with the name: $backup$date.tar.gz\n\n";
}
else {
    print "You did not back up any of the old files.\n\n";
}

#=-- Change the files and output results ----------------------------

# ($ucnt,$ecnt)=&purge_files;
print "\n+-------------------------------------------------------------------+\n";
printf "|      Change Files : %-4d                  Error Count : %-4d      |\n",$ucnt,$ecnt;
print "+-------------------------------------------------------------------+\n\n";
print "+-------------------------------------------------------------------+\n";
print "|               Thank you for using this program                    |\n";
print "|     If you have any questions or comments, please email me        |\n";
print "+-------------------------------------------------------------------+\n\n";


#=-- End of the program ---------------------------------------------





#=-------------------------------------------------------------------
#= sub : purge_files
#=     : This will purge all of the user files depending on when the
#=     : user last logged in.
#=     : Returns two numbers - $purge_count and $error_count
#=-------------------------------------------------------------------

sub purge_files {
    my($change_count,$error_count);
    $change_count=0;  $error_count=0;
    if ($add_to_line=~/^a/i) {
	foreach $name (@users) {
	    chomp($name);
	    unless (open(FPI,">>$userdir/$name.$filetype")) {
		print "Could not load file '$userdir/$name.$filetype' to append to: $!\n";
		$error_count++;
		next;
	    }
	    print FPI "$add_content\n";
	    close(FPI);
	    $change_count++;
        }
    } # end if
    else {
	foreach $name (@users) {
	    chomp($name);
	    unless (open(FPI,"$userdir/$name.$filetype")) {
	    print "Could not load file '$userdir/$name.$filetype': $!\n";
	    $error_count++;
	    next;
	}
	    unless (open(FPO,">tempuser")) {
		print "Could not open file 'tempuser' for writing to: $!\n";
		$error_count++;
		close(FPI);
		next;
	    }
	    $i=1;
	    while (<FPI>) {
		if ($i==$add_to_line) {
		    chomp($_);
		    print FPO $_ . " " . $add_content . "\n";
	        }
		else { print FPO $_; }
		++$i;
	    }
	    close(FPI);  close(FPO);
	    ++$change_count;
	    rename("tempuser","$userdir/$name.D");
        }
    } # end else
    return($change_count,$error_count);
}



#=-------------------------------------------------------------------
#= sub : getdate
#=     : requires nothing, returns date to append to backup filename
#=     : of the userfiles
#=-------------------------------------------------------------------

sub getdate {
    my($day);
    $day=`$dateprogram +"%d"`;
    chomp($day);
    unless ($day == 10 || $day == 20 || $day == 30) {
        $day =~ tr/0//;
    }
    $date=`$dateprogram +"%m$day%y"`;
    chomp($date);
} # End of  sub

