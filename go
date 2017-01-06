echo ""
echo "Creating picture list"
ls -x pictfiles/ > miscfiles/pictlist
echo "Executing AmNUTS"
mv syslog syslog.bak
mv netlog netlog.bak
mv reqlog reqlog.bak
rm dead.letter
mv a.out AmNUTS
strip AmNUTS
AmNUTS
