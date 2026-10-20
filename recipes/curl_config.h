/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: BSD-2-Clause */

/* curl_config.h -- libcurl's build configuration for Symbian's
 * Open C / P.I.P.S. libc on the amd64 (x32) Phorepoc board -- what curl's configure would have found. curl ships
 * hand-written configs for platforms without configure (lib/config-plan9.h, -riscos.h ...); this is the same for
 * Open C. The engine's configuration: HTTP(S) (the TLS backend, OpenSSL, is selected by the build line: -DUSE_OPENSSL=1),
 * file:, the threaded resolver, every other protocol off.
 *
 * Facts from the Open C headers (.epoc32/include/stdapis): no poll.h (select only), no netinet/tcp.h, no libgen.h,
 * no termios.h; time_t is 32-bit, off_t 64-bit; x32: int, long, size_t and pointers are 4 bytes.
 */
#ifndef HEADER_CURL_CONFIG_PHOREPOC_H
#define HEADER_CURL_CONFIG_PHOREPOC_H

#define OS "symbian-x32"
#define PACKAGE "curl"
#define PACKAGE_NAME "curl"
#define PACKAGE_VERSION "8.10.1"
#define VERSION "8.10.1"
#define CURL_STATICLIB 1
#define BUILDING_LIBCURL 1
#define STDC_HEADERS 1

/* --- protocols: HTTP(S), and file: when PH_CURL_FILE is defined --- */
#define CURL_DISABLE_FTP 1
#ifndef PH_CURL_FILE   /* the WebKit build (build_curl_ossl.sh) keeps file: -- CurlHandle sets CURLOPT_PROTOCOLS_STR "file,http,https"
                          and curl 8.10's protocol2num stops at the first unknown scheme with allowed_protocols left at 0:
                          every transfer then failed "Unsupported protocol" */
#define CURL_DISABLE_FILE 1
#endif
#define CURL_DISABLE_TELNET 1
#define CURL_DISABLE_DICT 1
#define CURL_DISABLE_LDAP 1
#define CURL_DISABLE_LDAPS 1
#define CURL_DISABLE_TFTP 1
#define CURL_DISABLE_POP3 1
#define CURL_DISABLE_IMAP 1
#define CURL_DISABLE_SMTP 1
#define CURL_DISABLE_RTSP 1
#define CURL_DISABLE_MQTT 1
#define CURL_DISABLE_GOPHER 1
#define CURL_DISABLE_SMB 1
#define CURL_DISABLE_NTLM 1
#define CURL_DISABLE_KERBEROS_AUTH 1
#define CURL_DISABLE_NEGOTIATE_AUTH 1
#define CURL_DISABLE_AWS 1
#define CURL_DISABLE_DOH 1
#define CURL_DISABLE_NETRC 1
#define CURL_DISABLE_SOCKETPAIR 1
#define CURL_DISABLE_WEBSOCKETS 1
#define CURL_DISABLE_ALTSVC 1
#define CURL_DISABLE_HSTS 1
#define CURL_DISABLE_PROGRESS_METER 1
/* (verbose strings kept: they print only when asked, the way to diagnose a fetch) */

/* --- sizes (x32) --- */
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_OFF_T 8
#define SIZEOF_CURL_OFF_T 8
#define SIZEOF_CURL_SOCKET_T 4
#define SIZEOF_SIZE_T 4
#define SIZEOF_TIME_T 4
#define HAVE_LONGLONG 1
#define HAVE_BOOL_T 1
#define HAVE_STDBOOL_H 1

/* --- headers --- */
#define HAVE_ARPA_INET_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LOCALE_H 1
#define HAVE_NETDB_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_PWD_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_UN_H 1
#define HAVE_UNISTD_H 1
#define HAVE_UTIME_H 1
/* content decoding: the engine asks for gzip (CURLOPT_ENCODING) -- Open C's zlib (libz.dll);
 * without it a compressed response failed with CURLE_BAD_CONTENT_ENCODING (61) */
#define HAVE_LIBZ 1
#define HAVE_ZLIB_H 1

/* --- functions --- */
#define HAVE_SOCKET 1
#define HAVE_SELECT 1
#define HAVE_RECV 1
#define HAVE_SEND 1
#define HAVE_FCNTL 1
#define HAVE_FCNTL_O_NONBLOCK 1
#define HAVE_GETADDRINFO 1
#define HAVE_FREEADDRINFO 1
#define HAVE_GETHOSTNAME 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_GMTIME_R 1
#define HAVE_INET_NTOP 1
#define HAVE_INET_PTON 1
#define HAVE_SETLOCALE 1
#define HAVE_SIGNAL 1
#define HAVE_STRDUP 1
#define HAVE_STRTOLL 1
#define HAVE_STRUCT_TIMEVAL 1
#define HAVE_SOCKADDR_IN6_SIN6_SCOPE_ID 1
#define HAVE_STRUCT_SOCKADDR_STORAGE 1
#define USE_IPV6 1

/* recv/send prototypes (Open C sys/socket.h: ssize_t recv(int, void *, size_t, int)) */
#define RECV_TYPE_ARG1 int
#define RECV_TYPE_ARG2 void *
#define RECV_TYPE_ARG3 size_t
#define RECV_TYPE_ARG4 int
#define RECV_TYPE_RETV ssize_t
#define SEND_QUAL_ARG2 const
#define SEND_TYPE_ARG1 int
#define SEND_TYPE_ARG2 void *
#define SEND_TYPE_ARG3 size_t
#define SEND_TYPE_ARG4 int
#define SEND_TYPE_RETV ssize_t

/* the resolver: curl's threaded one -- getaddrinfo in an Open C pthread, so DNS does not block the caller;
 * with CURL_DISABLE_SOCKETPAIR (above) curl polls the resolver thread instead of waiting on a wake-up fd */
#define USE_THREADS_POSIX 1
#define HAVE_PTHREAD_H 1

#endif /* HEADER_CURL_CONFIG_PHOREPOC_H */
