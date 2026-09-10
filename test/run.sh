#!/usr/bin/env bash

function create_data {
	if [ $1 -eq 0 ];
	then
		dd if=/dev/urandom of=$2 bs=1 count=4096 >/dev/null 2>&1
	else
		mkdir $2
		for i in $(seq 0 9 2>/dev/null || jot - 0 9);
		do
			create_data $(($1 - 1)) "$2/$i"
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

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

echo 'black-box parse fixtures...'
if ! bash "$SCRIPT_DIR/run_fixtures.sh" "$UNPACK"; then
	echo Failed
	exit 1
fi
echo Passed


OUTDIRNAME='out-test'
TMPFILE=file.tmp
DIFFLOG=diff.log
LISTFILE=list.txt
DIRNAME2='in-test2'
TMPFILE2='file2.tmp'
OUTDIRNAME2='out-test2'

rm -rf $TMPFILE $OUTDIRNAME $TMPFILE2 $OUTDIRNAME2

echo 'build/parse test...'

# Собираем файл, распаковываем и сравниваем каталоги
printf '%b\n%b' "-build;$DIRNAME;$TMPFILE" "-parse;$TMPFILE;$OUTDIRNAME" > $LISTFILE
$UNPACK -list $LISTFILE >/dev/null
diff -r $DIRNAME $OUTDIRNAME >$DIFFLOG 2>&1

if [ $? -ne 0 ];
then
	echo Failed
	exit 1
fi

echo Passed

rm -rf $OUTDIRNAME

echo 'build/unpack/pack/parse test...'

printf '%b\n%b\n%b' "-unpack;$TMPFILE;$OUTDIRNAME" \
	"-pack;$OUTDIRNAME;$TMPFILE2" \
	"-parse;$TMPFILE2;$OUTDIRNAME2"  > $LISTFILE
$UNPACK -list $LISTFILE >/dev/null
diff -r $DIRNAME $OUTDIRNAME2 >$DIFFLOG 2>&1

if [ $? -ne 0 ];
then
	echo Failed
	exit 1
fi

echo Passed

rm $TMPFILE $TMPFILE2
rm -rf $OUTDIRNAME $OUTDIRNAME2

echo 'build/unpack/pack/parse via list test...'

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
