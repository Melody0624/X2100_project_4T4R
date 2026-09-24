#!/bin/bash

if [ "$NM" == "" ]; then
NM=mips-linux-gnu-nm
fi

elf_file=$1

str=`$NM -n $elf_file | grep -e " T "  -e " W " -e 't'`

a0=
a1=
a2=
i=0

for t in ${str}
do
    if [ "$a0" == "" ]; then
        a0=$t;
        continue;
    fi
    if [ "$a1" == "" ]; then
        a1=$t;
        continue;
    fi
    if [ "$a2" == "" ]; then
        a2=$t;
    fi

    echo "__func_symbol_str const char func_symbol_str$i[] = \"$a2\";"
    a0=
    a1=
    a2=
    let i+=1
done

a0=
a1=
a2=
i=0

echo "__func_symbol_index struct func_symbol func_symbol_index[] = {"

for t in ${str}
do
    if [ "$a0" == "" ]; then
        a0=$t;
        continue;
    fi
    if [ "$a1" == "" ]; then
        a1=$t;
        continue;
    fi
    if [ "$a2" == "" ]; then
        a2=$t;
    fi

    echo "    {(unsigned long)0x$a0, func_symbol_str$i},"
    a0=
    a1=
    a2=
    let i+=1
done

echo "};"
