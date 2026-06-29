cd /data
make BOARD=nrf52840dk
docker run -it --rm   --platform linux/amd64   -v $(pwd):/data   riot/riotbuild
ls
exit
cd /data/apps
ls
cd hello-world/
ls
make BOARD=nrf52840dk
exit
cd /data/apps/hello-world
make BOARD=nrf52840dk
exit
cd /data/apps/hello-world
ls
make clean
make BOARD=nrf52840dk
ls /data/RIOT/pkg
make clean
rm -rf bin
make BOARD=nrf52840dk
exit
cd /data/apps/hello-world
make clean
make BOARD=nrf52840dk
cd /data/RIOT/pkg/mpaland-printf
rm -rf .git/rebase-apply
rm -rf .git/rebase-merge
git am --abort || true
git reset --hard
cd /data/RIOT
rm -rf build/pkg/mpaland-printf
cd /data/apps/hello-world
make clean
make BOARD=nrf52840dk
exit
docker run -it --rm   --platform linux/amd64   -v $(pwd):/data   riot/riotbuild
cd /data/apps/hello-world
make BOARD=seeedstudio-xiao-nrf52840-sense
exit
