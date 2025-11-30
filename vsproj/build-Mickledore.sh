echo "========  build.sh =========="
CURR_PATH=$(pwd)

echo "== 1 =="

echo "== 2 =="
cd ${CURR_PATH}
pwd

echo "== 3 =="
bitbake -c $1 tflow-vstream


echo "========  EOF build.sh =========="