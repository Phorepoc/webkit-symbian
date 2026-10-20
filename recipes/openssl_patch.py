# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause

"""openssl_patch.py <openssl source dir> -- the Phorepoc edits of OpenSSL 3.5 (Apache-2.0), keyed on __SYMBIAN32__ (the Open C compile
line). Write-on-change, like icu_patch.py.

  * providers/implementations/rands/seeding/rand_unix.c ossl_pool_acquire_entropy(): Open C's /dev/urandom opens but every read
    fails (the WTF RandomDevice crash) and there is no getrandom(). The seed comes from the kernel's
    CSPRNG instead: ph_random_bytes (cxxrt/phsym.cpp -> Math::Random(TDes8&), SHA-256 based), full entropy per byte.
"""
import sys

SRC = sys.argv[1]
EDITS = [
    ("providers/implementations/rands/seeding/rand_unix.c",
     "size_t ossl_pool_acquire_entropy(RAND_POOL *pool)\n{\n#  if defined(OPENSSL_RAND_SEED_NONE)\n",
     "#if defined(__SYMBIAN32__)\nextern void ph_random_bytes(void *buf, unsigned len);   /* Phorepoc: cxxrt/phsym.cpp */\n#endif\n"
     "size_t ossl_pool_acquire_entropy(RAND_POOL *pool)\n{\n"
     "#  if defined(__SYMBIAN32__)   /* Phorepoc: the kernel CSPRNG (Open C has no working /dev/urandom) */\n"
     "    {\n        size_t bytes_needed = ossl_rand_pool_bytes_needed(pool, 1);\n"
     "        unsigned char *buffer = ossl_rand_pool_add_begin(pool, bytes_needed);\n\n"
     "        if (buffer != NULL && bytes_needed > 0) {\n            ph_random_bytes(buffer, (unsigned)bytes_needed);\n"
     "            ossl_rand_pool_add_end(pool, bytes_needed, 8 * bytes_needed);\n        }\n"
     "        return ossl_rand_pool_entropy_available(pool);\n    }\n#  endif\n"
     "#  if defined(OPENSSL_RAND_SEED_NONE)\n"),
]
bad = 0
for rel, old, new in EDITS:
    p = f"{SRC}/{rel}"
    s = open(p, newline="").read()
    if new in s:
        continue
    if old not in s:
        print("OPENSSL PATCH MISS:", rel)
        bad += 1
        continue
    open(p, "w", newline="").write(s.replace(old, new, 1))
sys.exit(1 if bad else 0)
