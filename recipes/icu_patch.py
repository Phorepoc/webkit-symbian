# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause

"""icu_patch.py <icu source dir> -- the Phorepoc edits of ICU 74 (Unicode-3.0 licensed), keyed on __SYMBIAN32__ (defined only
by the Open C compile line, so ICU's native host-tools build of the same tree is unaffected). A file is written only when its
bytes change (a fresh mtime would rebuild every dependant).

  * putil.cpp uprv_timezone(): ICU copies *localtime(&t) -- Open C's localtime() returns NULL (no tz database), so jsc died
    with KERN-EXEC 3 in uprv_timezone at its first Date/Intl use. On Symbian the offset is the system's own:
    User::UTCOffset() (phsym.cpp ph_utc_offset_seconds); ICU wants seconds WEST of UTC.
"""
import sys

SRC = sys.argv[1]
EDITS = [
    ("common/putil.cpp",
     "U_CAPI int32_t U_EXPORT2\nuprv_timezone()\n{\n#ifdef U_TIMEZONE\n    return U_TIMEZONE;\n#else",
     "#if defined(__SYMBIAN32__)\nextern \"C\" int ph_utc_offset_seconds(void);   /* Phorepoc: User::UTCOffset() (cxxrt/phsym.cpp) */\n#endif\n"
     "U_CAPI int32_t U_EXPORT2\nuprv_timezone()\n{\n#if defined(__SYMBIAN32__)\n    return -ph_utc_offset_seconds();   /* Open C localtime() is NULL */\n"
     "#elif defined(U_TIMEZONE)\n    return U_TIMEZONE;\n#else"),
]
bad = 0
for rel, old, new in EDITS:
    p = f"{SRC}/{rel}"
    s = open(p, newline="").read()
    if new in s:
        continue
    if old not in s:
        print("ICU PATCH MISS:", rel)
        bad += 1
        continue
    open(p, "w", newline="").write(s.replace(old, new, 1))
sys.exit(1 if bad else 0)
