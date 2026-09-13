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

echo 'delete block tests...'

DEL_SRC='del-src'
DEL_CF='del-test.cf'
DEL_OUT='del-out'
FIX_FLAT="$SCRIPT_DIR/fixtures/f15-flat"
FIX_F16Z="$SCRIPT_DIR/fixtures/f16z-inner"
FIX_F16="$SCRIPT_DIR/fixtures/f16-zeropad-hex"

rm -rf "$DEL_SRC" "$DEL_OUT" "$DEL_CF" del-test2.cf del-list.txt

mkdir "$DEL_SRC"
printf 'one' > "$DEL_SRC/alpha"
printf 'two-two' > "$DEL_SRC/beta"
printf 'three-three-three' > "$DEL_SRC/gamma"
printf 't1' > "$DEL_SRC/test1"
printf 't2' > "$DEL_SRC/test2"
printf 'dot' > "$DEL_SRC/file.name"

$UNPACK -build -nopack "$DEL_SRC" "$DEL_CF" >/dev/null
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi

# точное имя, правка на месте (размер файла не меняется)
cp "$DEL_CF" del-exact.cf
before=$(wc -c < del-exact.cf | tr -d ' ')
$UNPACK -delete del-exact.cf beta >/dev/null
after=$(wc -c < del-exact.cf | tr -d ' ')
if [ "$before" != "$after" ]; then
	echo Failed
	exit 1
fi
$UNPACK -listfiles del-exact.cf > del-names.txt
if grep -qx beta del-names.txt || ! grep -qx alpha del-names.txt; then
	echo Failed
	exit 1
fi
$UNPACK -parse del-exact.cf "$DEL_OUT" >/dev/null
if [ ! -f "$DEL_OUT/alpha" ] || [ -e "$DEL_OUT/beta" ] || [ ! -f "$DEL_OUT/gamma" ]; then
	echo Failed
	exit 1
fi
diff "$DEL_SRC/alpha" "$DEL_OUT/alpha" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$DEL_OUT" del-exact.cf del-names.txt

# маска *
cp "$DEL_CF" del-star.cf
$UNPACK -del del-star.cf 'test*' >/dev/null
$UNPACK -parse del-star.cf "$DEL_OUT" >/dev/null
if [ -e "$DEL_OUT/test1" ] || [ -e "$DEL_OUT/test2" ] || [ ! -f "$DEL_OUT/alpha" ] || [ ! -f "$DEL_OUT/file.name" ]; then
	echo Failed
	exit 1
fi
rm -rf "$DEL_OUT" del-star.cf

# маска ?
cp "$DEL_CF" del-q.cf
$UNPACK -delete del-q.cf '????' >/dev/null
$UNPACK -parse del-q.cf "$DEL_OUT" >/dev/null
if [ -e "$DEL_OUT/beta" ] || [ ! -f "$DEL_OUT/alpha" ] || [ ! -f "$DEL_OUT/gamma" ]; then
	echo Failed
	exit 1
fi
rm -rf "$DEL_OUT" del-q.cf

# маска в середине и несколько масок
cp "$DEL_CF" del-multi.cf
$UNPACK -delete del-multi.cf 'a*a' 'file.*' >/dev/null
$UNPACK -parse del-multi.cf "$DEL_OUT" >/dev/null
if [ -e "$DEL_OUT/alpha" ] || [ -e "$DEL_OUT/file.name" ] || [ ! -f "$DEL_OUT/beta" ] || [ ! -f "$DEL_OUT/test1" ]; then
	echo Failed
	exit 1
fi
rm -rf "$DEL_OUT" del-multi.cf

# нет совпадений — все блоки на месте
cp "$DEL_CF" del-nomatch.cf
$UNPACK -delete del-nomatch.cf 'zzz*' >/dev/null
$UNPACK -parse del-nomatch.cf "$DEL_OUT" >/dev/null
diff -r "$DEL_SRC" "$DEL_OUT" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$DEL_OUT" del-nomatch.cf

# удаление всех блоков
cp "$DEL_CF" del-all.cf
$UNPACK -delete del-all.cf '*' >/dev/null
$UNPACK -parse del-all.cf "$DEL_OUT" >/dev/null
if [ -e "$DEL_OUT/alpha" ] || [ -e "$DEL_OUT/beta" ] || [ -e "$DEL_OUT/gamma" ]; then
	echo Failed
	exit 1
fi
rm -rf "$DEL_OUT" del-all.cf

# list-файл
cp "$DEL_CF" del-list1.cf
cp "$DEL_CF" del-list2.cf
printf '%b\n%b' "del-list1.cf;alpha" "del-list2.cf;test?;file.*" > del-list.txt
$UNPACK -delete -list del-list.txt >/dev/null
$UNPACK -parse del-list1.cf del-list1-out >/dev/null
$UNPACK -parse del-list2.cf del-list2-out >/dev/null
if [ -e del-list1-out/alpha ] || [ ! -f del-list1-out/beta ]; then
	echo Failed
	exit 1
fi
if [ -e del-list2-out/test1 ] || [ -e del-list2-out/test2 ] || [ -e del-list2-out/file.name ] || [ ! -f del-list2-out/alpha ]; then
	echo Failed
	exit 1
fi
rm -rf del-list1-out del-list2-out del-list1.cf del-list2.cf del-list.txt

# фикстура f15-paged-toc (запись TOC на границе страниц)
FIX_PAGED="$SCRIPT_DIR/fixtures/f15-paged-toc"
cp "$FIX_PAGED/in.cf" del-paged.cf
$UNPACK -delete del-paged.cf e2 >/dev/null
$UNPACK -parse del-paged.cf "$DEL_OUT" >/dev/null
if [ -e "$DEL_OUT/e2" ] || [ ! -f "$DEL_OUT/e0" ] || [ ! -f "$DEL_OUT/e1" ] || [ ! -f "$DEL_OUT/e3" ] || [ ! -f "$DEL_OUT/e4" ]; then
	echo Failed
	exit 1
fi
diff "$FIX_PAGED/expected/e1" "$DEL_OUT/e1" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$DEL_OUT" del-paged.cf

# фикстура f15
cp "$FIX_FLAT/in.cf" del-fix15.cf
$UNPACK -delete del-fix15.cf 'a*' '?amma' >/dev/null
$UNPACK -parse del-fix15.cf "$DEL_OUT" >/dev/null
if [ -e "$DEL_OUT/alpha" ] || [ -e "$DEL_OUT/gamma" ] || [ ! -f "$DEL_OUT/beta" ]; then
	echo Failed
	exit 1
fi
diff "$FIX_FLAT/expected/beta" "$DEL_OUT/beta" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$DEL_OUT" del-fix15.cf

# фикстура f16z
cp "$FIX_F16Z/in.cf" del-fix16z.cf
$UNPACK -delete del-fix16z.cf info >/dev/null
$UNPACK -parse del-fix16z.cf "$DEL_OUT" >/dev/null
if [ -e "$DEL_OUT/info" ] || [ ! -f "$DEL_OUT/form" ]; then
	echo Failed
	exit 1
fi
diff "$FIX_F16Z/expected/form" "$DEL_OUT/form" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$DEL_OUT" del-fix16z.cf

# фикстура f16 (с placeholder)
cp "$FIX_F16/in.cf" del-fix16.cf
$UNPACK -delete del-fix16.cf 'z*' >/dev/null
$UNPACK -parse del-fix16.cf "$DEL_OUT" >/dev/null
if [ -e "$DEL_OUT/zeroed" ]; then
	echo Failed
	exit 1
fi
rm -rf "$DEL_OUT" del-fix16.cf

# ошибка: нет файла
$UNPACK -delete missing-file.cf alpha >/dev/null 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi

echo Passed

echo 'add block tests...'

ADD_CF='add-test.cf'
ADD_OUT='add-out'
ADD_SRC='add-src'
rm -rf "$ADD_OUT" "$ADD_SRC" "$ADD_CF" add-extra.txt add-plain.txt add-list.txt add-dir add-stdin.cf

cp "$DEL_CF" "$ADD_CF"

# PACK по умолчанию, имя из файла
printf 'added-raw' > add-extra.txt
$UNPACK -add add-extra.txt "$ADD_CF" >/dev/null
$UNPACK -parse "$ADD_CF" "$ADD_OUT" >/dev/null
if [ ! -f "$ADD_OUT/add-extra.txt" ]; then
	echo Failed
	exit 1
fi
diff add-extra.txt "$ADD_OUT/add-extra.txt" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$ADD_OUT"

# повторное ADD с тем же именем — ошибка
$UNPACK -add add-extra.txt "$ADD_CF" >/dev/null 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi
$UNPACK -add -n alpha add-extra.txt "$ADD_CF" >/dev/null 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi

# -NAME
printf 'named-body' > add-plain.txt
$UNPACK -add -n custom.name add-plain.txt "$ADD_CF" >/dev/null
$UNPACK -parse "$ADD_CF" "$ADD_OUT" >/dev/null
if [ ! -f "$ADD_OUT/custom.name" ]; then
	echo Failed
	exit 1
fi
diff add-plain.txt "$ADD_OUT/custom.name" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$ADD_OUT"

# -BUILD (deflate)
printf 'build-payload' > add-build.txt
$UNPACK -add -build add-build.txt "$ADD_CF" >/dev/null
$UNPACK -parse "$ADD_CF" "$ADD_OUT" >/dev/null
if [ ! -f "$ADD_OUT/add-build.txt" ]; then
	echo Failed
	exit 1
fi
diff add-build.txt "$ADD_OUT/add-build.txt" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$ADD_OUT"

# -BUILD -NOPACK для каталога
mkdir add-dir
printf 'child-a' > add-dir/child
$UNPACK -add -build -nopack add-dir "$ADD_CF" >/dev/null
$UNPACK -parse "$ADD_CF" "$ADD_OUT" >/dev/null
if [ ! -f "$ADD_OUT/add-dir/child" ]; then
	echo Failed
	exit 1
fi
diff add-dir/child "$ADD_OUT/add-dir/child" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$ADD_OUT"

# stdin + -NAME
cp "$DEL_CF" add-stdin.cf
printf 'from-stdin' | $UNPACK -add -name stdinfile - add-stdin.cf >/dev/null
$UNPACK -parse add-stdin.cf "$ADD_OUT" >/dev/null
if [ ! -f "$ADD_OUT/stdinfile" ]; then
	echo Failed
	exit 1
fi
printf 'from-stdin' > add-stdin-expected.txt
diff add-stdin-expected.txt "$ADD_OUT/stdinfile" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$ADD_OUT" add-stdin.cf add-stdin-expected.txt

# -LISTFILES
cp "$DEL_CF" add-lf.cf
printf 'one-list' > add-l1.txt
printf 'two-list' > add-l2.txt
printf '%s\n%s\n' add-l1.txt add-l2.txt > add-list.txt
$UNPACK -add -lf add-list.txt add-lf.cf >/dev/null
$UNPACK -parse add-lf.cf "$ADD_OUT" >/dev/null
if [ ! -f "$ADD_OUT/add-l1.txt" ] || [ ! -f "$ADD_OUT/add-l2.txt" ]; then
	echo Failed
	exit 1
fi
diff add-l1.txt "$ADD_OUT/add-l1.txt" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$ADD_OUT" add-lf.cf add-l1.txt add-l2.txt add-list.txt

# удаление + добавление в свободный слот, размер не растёт
cp "$DEL_CF" add-reuse.cf
printf 'zz' > add-reuse.txt
before=$(wc -c < add-reuse.cf | tr -d ' ')
$UNPACK -delete add-reuse.cf beta >/dev/null
$UNPACK -add -name zzzz add-reuse.txt add-reuse.cf >/dev/null
after=$(wc -c < add-reuse.cf | tr -d ' ')
if [ "$before" != "$after" ]; then
	echo Failed
	exit 1
fi
$UNPACK -parse add-reuse.cf "$ADD_OUT" >/dev/null
if [ -e "$ADD_OUT/beta" ] || [ ! -f "$ADD_OUT/zzzz" ]; then
	echo Failed
	exit 1
fi
diff add-reuse.txt "$ADD_OUT/zzzz" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$ADD_OUT" add-reuse.cf add-reuse.txt

# фикстура f16z
cp "$FIX_F16Z/in.cf" add-fix16z.cf
printf 'new-form' > add-f16z.txt
$UNPACK -add -name extra add-f16z.txt add-fix16z.cf >/dev/null
$UNPACK -parse add-fix16z.cf "$ADD_OUT" >/dev/null
if [ ! -f "$ADD_OUT/extra" ] || [ ! -f "$ADD_OUT/info" ]; then
	echo Failed
	exit 1
fi
diff add-f16z.txt "$ADD_OUT/extra" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$ADD_OUT" add-fix16z.cf add-f16z.txt

# stdin без -NAME
printf 'x' | $UNPACK -add - "$ADD_CF" >/dev/null 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi

echo Passed

echo 'put block tests...'

# PUT нового имени — как ADD
printf 'put-new' > put-new.txt
$UNPACK -put -n put-new put-new.txt "$ADD_CF" >/dev/null
$UNPACK -parse "$ADD_CF" "$ADD_OUT" >/dev/null
if [ ! -f "$ADD_OUT/put-new" ]; then
	echo Failed
	exit 1
fi
diff put-new.txt "$ADD_OUT/put-new" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$ADD_OUT"

# PUT существующего — замена содержимого
printf 'alpha-replaced' > put-alpha.txt
$UNPACK -put -n alpha put-alpha.txt "$ADD_CF" >/dev/null
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
$UNPACK -parse "$ADD_CF" "$ADD_OUT" >/dev/null
if [ ! -f "$ADD_OUT/alpha" ]; then
	echo Failed
	exit 1
fi
diff put-alpha.txt "$ADD_OUT/alpha" >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
printf 'one' > put-alpha-old.txt
diff put-alpha-old.txt "$ADD_OUT/alpha" >$DIFFLOG 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi
rm -rf "$ADD_OUT" put-new.txt put-alpha.txt put-alpha-old.txt

echo Passed

echo 'unpack stdout / pipe add tests...'

FIX_FLAT="$SCRIPT_DIR/fixtures/f15-flat"
cp "$FIX_FLAT/in.cf" unpack-src.cf
$UNPACK -unpack unpack-src.cf - alpha > unpack-stdout.bin
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
diff "$FIX_FLAT/expected/alpha" unpack-stdout.bin >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi

cp "$FIX_FLAT/in.cf" pipe-dest.cf
$UNPACK -unpack unpack-src.cf - beta | $UNPACK -add -name beta-copy - pipe-dest.cf >/dev/null
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi
$UNPACK -parse pipe-dest.cf pipe-out >/dev/null
if [ ! -f pipe-out/beta-copy ]; then
	echo Failed
	exit 1
fi
diff "$FIX_FLAT/expected/beta" pipe-out/beta-copy >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi

$UNPACK -unpack unpack-src.cf - >/dev/null 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi

$UNPACK -unpack unpack-src.cf - missing-block >/dev/null 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi

echo Passed

echo 'versions file tests...'

VF_UUID='vf-uuid.txt'
VF_CI='vf-configinfo.txt'
printf '\xef\xbb\xbf{1,2,"",aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa,"alpha",bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb}' > "$VF_UUID"
printf '%s' $'\xef\xbb\xbf{0,\r\n{216,0,\r\n{80317,0}\r\n}\r\n},\r\n{2,79165599-87f3-4ac0-a26f-bec87ea8f585,YZsp1SBerILCqmJ930K1cmU+5QPr64+4LgI0qUooM2SL6iy53LudBp+6DuIrnfwV},\r\n{2,"alpha",3zNTcmSVEx7JH9YSULpruqXpBC4=,"beta",mYC3tw2+8OkCY89Uyq+bcg+6WHE=}' > "$VF_CI"

$UNPACK -vf -show "$VF_UUID" > vf-show.txt
if ! grep -q 'alpha' vf-show.txt || ! grep -q 'bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb' vf-show.txt; then
	echo Failed
	exit 1
fi

got=$($UNPACK -vf -get "$VF_UUID" alpha | tr -d '\r')
if [ "$got" != "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb" ]; then
	echo Failed
	exit 1
fi

$UNPACK -vf -set "$VF_UUID" alpha cccccccc-cccc-cccc-cccc-cccccccccccc >/dev/null
got=$($UNPACK -vf -get "$VF_UUID" alpha | tr -d '\r')
if [ "$got" != "cccccccc-cccc-cccc-cccc-cccccccccccc" ]; then
	echo Failed
	exit 1
fi

# пустое имя и исходная версия контейнера сохранились
got=$($UNPACK -vf -get "$VF_UUID" "" | tr -d '\r')
if [ "$got" != "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa" ]; then
	echo Failed
	exit 1
fi

$UNPACK -vf -set "$VF_UUID" alpha not-a-uuid >/dev/null 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi
got=$($UNPACK -vf -get "$VF_UUID" alpha | tr -d '\r')
if [ "$got" != "cccccccc-cccc-cccc-cccc-cccccccccccc" ]; then
	echo Failed
	exit 1
fi

old=$got
new=$($UNPACK -vf -update "$VF_UUID" alpha | tr -d '\r')
if [ -z "$new" ] || [ "$new" = "$old" ]; then
	echo Failed
	exit 1
fi
case "$new" in
	[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]) ;;
	*) echo Failed; exit 1 ;;
esac
got=$($UNPACK -versionsfile -get "$VF_UUID" alpha | tr -d '\r')
if [ "$got" != "$new" ]; then
	echo Failed
	exit 1
fi

$UNPACK -vf -get "$VF_UUID" missing >/dev/null 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi

got=$($UNPACK -vf -get "$VF_CI" alpha | tr -d '\r')
if [ "$got" != "3zNTcmSVEx7JH9YSULpruqXpBC4=" ]; then
	echo Failed
	exit 1
fi

cp "$VF_CI" vf-ci-before.txt
$UNPACK -vf -set "$VF_CI" beta AAAAAAAAAAAAAAAAAAAAAAAAAAA= >/dev/null
got=$($UNPACK -vf -get "$VF_CI" beta | tr -d '\r')
if [ "$got" != "AAAAAAAAAAAAAAAAAAAAAAAAAAA=" ]; then
	echo Failed
	exit 1
fi
got=$($UNPACK -vf -get "$VF_CI" alpha | tr -d '\r')
if [ "$got" != "3zNTcmSVEx7JH9YSULpruqXpBC4=" ]; then
	echo Failed
	exit 1
fi
# version/root не должны меняться
head -c 80 vf-ci-before.txt > vf-ci-head1.bin
head -c 80 "$VF_CI" > vf-ci-head2.bin
diff vf-ci-head1.bin vf-ci-head2.bin >$DIFFLOG 2>&1
if [ $? -ne 0 ]; then
	echo Failed
	exit 1
fi

$UNPACK -vf -set "$VF_CI" beta not-base64 >/dev/null 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi
$UNPACK -vf -set "$VF_CI" beta YQ== >/dev/null 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi

old=$($UNPACK -vf -get "$VF_CI" alpha | tr -d '\r')
new=$($UNPACK -vf -update "$VF_CI" alpha | tr -d '\r')
if [ -z "$new" ] || [ "$new" = "$old" ] || [ ${#new} -ne 28 ]; then
	echo Failed
	exit 1
fi
got=$($UNPACK -vf -get "$VF_CI" alpha | tr -d '\r')
if [ "$got" != "$new" ]; then
	echo Failed
	exit 1
fi

$UNPACK -vf -show missing-vf.txt >/dev/null 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi

# SET -LF: несколько строк, лишние пробелы и пустые строки
printf '\xef\xbb\xbf{1,3,"",aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa,"alpha",bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb,"beta",eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee}' > vf-list-uuid.txt
printf '%s\n' \
	'alpha    11111111-1111-1111-1111-111111111111' \
	'' \
	'beta	22222222-2222-2222-2222-222222222222' \
	> vf-set-list.txt
$UNPACK -vf -set -lf vf-set-list.txt vf-list-uuid.txt >/dev/null
got=$($UNPACK -vf -get vf-list-uuid.txt alpha | tr -d '\r')
if [ "$got" != "11111111-1111-1111-1111-111111111111" ]; then
	echo Failed
	exit 1
fi
got=$($UNPACK -vf -get vf-list-uuid.txt beta | tr -d '\r')
if [ "$got" != "22222222-2222-2222-2222-222222222222" ]; then
	echo Failed
	exit 1
fi

# ошибка в списке не меняет файл
printf '%s\n' \
	'alpha 33333333-3333-3333-3333-333333333333' \
	'missing 44444444-4444-4444-4444-444444444444' \
	> vf-set-bad.txt
$UNPACK -vf -set -list vf-set-bad.txt vf-list-uuid.txt >/dev/null 2>&1
if [ $? -eq 0 ]; then
	echo Failed
	exit 1
fi
got=$($UNPACK -vf -get vf-list-uuid.txt alpha | tr -d '\r')
if [ "$got" != "11111111-1111-1111-1111-111111111111" ]; then
	echo Failed
	exit 1
fi

# UPDATE -LF
printf '%s\n' alpha beta > vf-upd-list.txt
old_alpha=$($UNPACK -vf -get vf-list-uuid.txt alpha | tr -d '\r')
old_beta=$($UNPACK -vf -get vf-list-uuid.txt beta | tr -d '\r')
$UNPACK -vf -update -lf vf-upd-list.txt vf-list-uuid.txt > vf-upd-out.txt
new_alpha=$($UNPACK -vf -get vf-list-uuid.txt alpha | tr -d '\r')
new_beta=$($UNPACK -vf -get vf-list-uuid.txt beta | tr -d '\r')
if [ -z "$new_alpha" ] || [ "$new_alpha" = "$old_alpha" ] || [ "$new_beta" = "$old_beta" ]; then
	echo Failed
	exit 1
fi
if ! grep -q "$new_alpha" vf-upd-out.txt || ! grep -q "$new_beta" vf-upd-out.txt; then
	echo Failed
	exit 1
fi

# SET -LF для configinfo
printf '%s\n' \
	'alpha  AAAAAAAAAAAAAAAAAAAAAAAAAAA=' \
	'beta   3zNTcmSVEx7JH9YSULpruqXpBC4=' \
	> vf-set-ci.txt
$UNPACK -vf -set -lf vf-set-ci.txt "$VF_CI" >/dev/null
got=$($UNPACK -vf -get "$VF_CI" alpha | tr -d '\r')
if [ "$got" != "AAAAAAAAAAAAAAAAAAAAAAAAAAA=" ]; then
	echo Failed
	exit 1
fi
got=$($UNPACK -vf -get "$VF_CI" beta | tr -d '\r')
if [ "$got" != "3zNTcmSVEx7JH9YSULpruqXpBC4=" ]; then
	echo Failed
	exit 1
fi

echo Passed

rm -rf "$DEL_SRC" "$DEL_OUT" "$DEL_CF" "$ADD_OUT" "$ADD_SRC" "$ADD_CF" add-dir pipe-out
rm -f add-extra.txt add-plain.txt add-build.txt unpack-src.cf unpack-stdout.bin pipe-dest.cf \
	"$VF_UUID" "$VF_CI" vf-show.txt vf-ci-before.txt vf-ci-head1.bin vf-ci-head2.bin \
	vf-list-uuid.txt vf-set-list.txt vf-set-bad.txt vf-upd-list.txt vf-upd-out.txt vf-set-ci.txt $DIFFLOG

rm -rf $DIRNAME
rm -rf $OUTDIRNAME
rm -f $TMPFILE $DIFFLOG
