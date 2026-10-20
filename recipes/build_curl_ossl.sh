#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# build_curl_ossl.sh -- the browser's libcurl: curl 8.10.1 in the configuration of recipes/curl_config.h (hand-written, as
# curl does for platforms without configure) with OpenSSL 3.5 (build_webdeps.sh openssl) instead of mbedTLS, on the pure Open C
# line. Our include dirs go FIRST: Open C's stdapis carry an OpenSSL 0.9.8 tree of their own. Out: $V/deps-x32/{lib/libcurl.a,include/curl}.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
P=$DEPS; J=${JOBS:-4}; LOG=$LOG/webdeps
WORK=$V/wd/curl-ossl; SRC=$WORK/curl-8.10.1
bash "$HERE/fetch_sources.sh" curl >/dev/null || exit 1
pure_wrappers || exit 1
[ -f $P/lib/libssl.a ] || { echo "no OpenSSL (build_webdeps.sh openssl)"; exit 1; }
[ -d $SRC ] || { mkdir -p $WORK; tar -xzf "$(tarball curl)" -C $WORK || exit 1; }
cp -f $HERE/curl_config.h $SRC/lib/curl_config.h
FL=(-O2 -g0 -U__linux__ -U__linux -DPHOREPOC=1 -I$P/include -iquote $SRC/lib -I$HERE -I$SRC/include -I$SRC/lib -include sys/select.h -include signal.h -DSYMBIAN_OE_POSIX_SIGNALS
    -DHAVE_CONFIG_H -DPH_CURL_FILE=1 -DUSE_OPENSSL=1 -DHAVE_OPENSSL_SRP=0 -Werror=implicit-function-declaration -Wno-attributes)
SOURCES=$(cd $SRC/lib && printf 'include Makefile.inc\nprint:\n\t@echo $(CSOURCES)\n' | make -s -f - print)
O=$WORK/obj; rm -rf $O; mkdir -p $O $LOG; : > $LOG/curl-ossl.log
export SRC O; export FLS="${FL[*]}"
printf '%s\n' $SOURCES | xargs -P $J -I{} sh -c 'b=$(echo {} | tr / _); cd $SRC/lib && $W/bin/pp-gcc $FLS -c {} -o $O/${b%.c}.o 2>>$O/err.log || echo "FAIL {}" >> $O/fail'
if [ -s $O/fail ]; then cat $O/fail; grep -m20 "error" $O/err.log; exit 1; fi
rm -f $P/lib/libcurl.a; ${CROSS}ar rcs $P/lib/libcurl.a $O/*.o; rm -rf $P/include/curl; cp -r $SRC/include/curl $P/include/
# curl.h takes fd_set from <sys/select.h> only on its platform list (curl 7.x had __SYMBIAN32__ there, 8.x dropped it): the installed
# header gets it back for clients (WebCore), like a force-include
python3 - $P/include/curl/curl.h <<'PY'
import sys
p = sys.argv[1]; s = open(p).read(); m = "#define CURLINC_CURL_H\n"
if "Phorepoc" not in s:
    open(p, "w").write(s.replace(m, m + "#if defined(__SYMBIAN32__)   /* Phorepoc: Open C's fd_set */\n#include <sys/select.h>\n#endif\n", 1))
PY
grep -q Phorepoc $P/include/curl/curl.h || { echo "curl.h patch missed"; exit 1; }
ls -l $P/lib/libcurl.a
