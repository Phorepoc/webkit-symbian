#!/usr/bin/env python3
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause

"""icu_trim.py <item-list-from-icupkg--l> -> stdout: the items to REMOVE from icudt74l.dat for the Phorepoc browser build:
all locale data except root/en/en_US/en_001/en_GB, collation/currency/language/region/unit/zone tables except root/en, rbnf, transliteration,
CJK/Thai/Khmer/Lao/Burmese dictionaries and LSTM models, spoof/confusables, StringPrep, codepage converters (UTF-8/16/32 are built in) except the 23 WebKit asks for (TextCodecICU + EncodingTables) (KEEP_CNV: the
WHATWG legacy encodings WebKit has no codec of its own for -- without them 654 dom/nodes characterSet subtests failed).
Keeps: root, res_index, supplemental/meta data, normalization, case/props, break-iterator rules (brkitr/*.brk), coll/ucadata root, zoneinfo64."""
import re, sys
KEEP_CNV = {  # TextCodecICU.cpp (wpewebkit-2.54.1) names -> ICU converters (ucnv_getName); ISO-8859-16 is not in stock ICU data
    "ibm-912_P100-1995", "ibm-914_P100-1995", "ibm-915_P100-1995", "iso-8859_10-1998", "ibm-921_P100-1995", "iso-8859_14-1998",
    "ibm-923_P100-1998", "ibm-878_P100-1996", "macos-0_2-10.2", "ibm-5346_P100-1998", "ibm-5347_P100-1998", "ibm-5350_P100-1998",
    "ibm-9448_X100-2005", "ibm-5354_P100-1998", "macos-7_3-10.2", "macos-6_2-10.4", "macos-29-10.2", "macos-35-10.2", "euc-tw-2014",
    # EncodingTables.cpp builds its CJK index tables from these at run time (Big-5, EUC-JP, gb18030, windows-949): without
    # EUC-JP the jis0208 table overran (std::array assert, WPT xhr overridemimetype-...-shiftjis)
    "windows-950-2000", "euc-jp-2007", "gb18030-2022", "windows-949-2000",
    "ibm-1363_P11B-1998", "ibm-1373_P100-2002"}   # the bases windows-949-2000 / windows-950-2000 extend
keep_loc = {"root", "en", "en_US", "en_001", "en_GB", "res_index", "pool"}
rm = []
for line in open(sys.argv[1]):
    it = line.strip()
    if not it:
        continue
    m = re.match(r"^(?:(coll|curr|lang|region|unit|zone|rbnf|translit|brkitr)/)?([^/]+)$", it)
    d, name = (m.group(1), m.group(2)) if m else (None, it)
    base = name[:-4] if name.endswith(".res") else name
    drop = False
    if d in ("rbnf", "translit"):
        drop = True
    elif d == "brkitr":
        drop = name.endswith(".model") or (name.endswith(".res") and base not in keep_loc and not base.startswith(("root","en")))
        if name.endswith(".res") and base in ("root", "res_index"): drop = False
    elif d in ("coll", "curr", "lang", "region", "unit", "zone"):
        drop = name.endswith(".res") and base not in keep_loc
    elif d is None:
        if (name.endswith(".cnv") and name[:-4] not in KEEP_CNV) or name.endswith(".spp") or name.endswith(".cfu"):
            drop = True
        elif name.endswith(".res") and re.match(r"^[a-z]{2,3}(_[A-Za-z0-9]+)*$", base) and base not in keep_loc:
            drop = True
    if drop:
        rm.append(it)
print("\n".join(rm))
