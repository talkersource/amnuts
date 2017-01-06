echo ""
echo "Creating picture list"
ls -x pictfiles/ > miscfiles/pictlist
echo "Executing Amnuts"
mv a.out Amnuts221
strip Amnuts221
./Amnuts221
