#!/bin/bash

if ! [ -f .config.in ]; then
    touch .config.in
fi

if ! [ -f include/config.h ]; then
    touch include/config.h
fi

tools/configparser --input Config.in .config.in --defconfig .config.in.tmp --header include/config.h.tmp > /dev/null

# 检查 .config.in 是否有更新
diff .config.in .config.in.tmp
if [ $? != "0" ];then
    mv .config.in.tmp .config.in
else
    rm .config.in.tmp
fi

# 检查 include/config.h 是否有更新
diff include/config.h include/config.h.tmp
if [ $? != "0" ];then
    mv include/config.h.tmp include/config.h
else
    rm include/config.h.tmp
fi
