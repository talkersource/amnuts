echo ""
echo "Creating picture list"
ls -x pictfiles/ > miscfiles/pictlist
echo "Executing Amnuts"
mv a.out Amnuts220
strip Amnuts220
./Amnuts220
