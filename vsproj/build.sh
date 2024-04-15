echo "========  build.sh =========="
CURR_PATH=$(pwd)

echo "== 1 =="
cd /home/av/imx/
pwd
MACHINE=ucm-imx8m-plus
source compulab-setup-env -b build-${MACHINE}

echo "== 2 =="
cd ${CURR_PATH}
pwd

echo "== 3 =="
bitbake -c $1 tflow-vstream

echo "========  EOF build.sh =========="