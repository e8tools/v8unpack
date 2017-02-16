#!/usr/bin/env bash

function create_data {
	if [ $1 -eq 0 ];
	then
		dd if=/dev/urandom of=$2 bs=1 count=4096 >/dev/null 2>&1
	else
		mkdir $2
		for i in `seq 0 9`;
		do
			create_data `expr $1 - 1` "$2/$i"
		done
	fi

}

DIRNAME='in-test'
rm -rf $DIRNAME
depth=3
create_data $depth $DIRNAME

if [ -z "$1" ]; then
	UNPACK=../bin/Release/v8unpack
else
	UNPACK=$1
fi

OUTDIRNAME='out-test'
TMPFILE=file.tmp
DIFFLOG=diff.log

rm -rf $TMPFILE $OUTDIRNAME

echo 'build/parse test...'

# Собираем файл, распаковываем и сравниваем каталоги
$UNPACK -build $DIRNAME $TMPFILE >/dev/null
$UNPACK -parse $TMPFILE $OUTDIRNAME >/dev/null
diff -r $DIRNAME $OUTDIRNAME >$DIFFLOG 2>&1

if [ $? -ne 0 ];
then
	echo Failed
	exit 1
fi

echo Passed

rm -rf $OUTDIRNAME

echo 'build/unpack/pack/parse test...'

$UNPACK -unpack $TMPFILE $OUTDIRNAME >/dev/null
rm $TMPFILE
$UNPACK -pack $OUTDIRNAME $TMPFILE >/dev/null
rm -rf $OUTDIRNAME
$UNPACK -parse $TMPFILE $OUTDIRNAME >/dev/null
diff -r $DIRNAME $OUTDIRNAME >$DIFFLOG 2>&1

if [ $? -ne 0 ];
then
	echo Failed
	exit 1
fi

echo Passed

echo 'build/unpack/pack/parse via list test...'
LISTFILE=list.txt
DIRNAME2='in-test2'
TMPFILE2='file2.tmp'
OUTDIRNAME2='out-test2'

cp -r $DIRNAME $DIRNAME2
printf "$DIRNAME;$TMPFILE\n$DIRNAME2;$TMPFILE2" > $LISTFILE
$UNPACK -build -list $LISTFILE >/dev/null

printf "$TMPFILE;$OUTDIRNAME\n$TMPFILE2;$OUTDIRNAME2" > $LISTFILE
$UNPACK -parse -list $LISTFILE >/dev/null

diff -r $DIRNAME $OUTDIRNAME >$DIFFLOG 2>&1
if [ $? -ne 0 ];
then
	echo Failed
	exit 1
fi

diff -r $DIRNAME2 $OUTDIRNAME2 >$DIFFLOG 2>&1
if [ $? -ne 0 ];
then
	echo Failed
	exit 1
fi

echo Passed


rm -rf $DIRNAME2
rm -rf $OUTDIRNAME2
rm $TMPFILE2
rm $LISTFILE

rm -rf $DIRNAME
rm -rf $OUTDIRNAME
rm $TMPFILE
rm $DIFFLOG
