echo ""
echo "Creating picture list"
ls -x pictfiles/ > miscfiles/pictlist
echo "Executing Amnuts"
mv syslog syslog.bak
mv netlog netlog.bak
mv reqlog reqlog.bak
rm dead.letter
mv a.out Amnuts210
strip Amnuts210
./Amnuts210
