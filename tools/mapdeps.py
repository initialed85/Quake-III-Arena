#!/usr/bin/env python3
"""Turn a `fs_logDeps 1` log into a per-map asset manifest.

Run the engine with +set fs_logDeps 1 +devmap <map>, capture stdout, then:

  tools/mapdeps.py /tmp/deps.log baseq3

Prints unique files, uncompressed and compressed totals, and writes a manifest
of the files that map needs. The manifest is what a browser build prefetches.
"""
import sys, glob, zipfile, collections

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    log, pk3dir = sys.argv[1], sys.argv[2]

    unc, comp = {}, {}
    for pk3 in sorted(glob.glob(pk3dir + "/*.pk3")):
        with zipfile.ZipFile(pk3) as z:
            for i in z.infolist():
                unc[i.filename.lower()] = i.file_size
                comp[i.filename.lower()] = i.compress_size

    deps = set()
    for line in open(log, errors="replace"):
        if line.startswith("DEP "):
            deps.add(line[4:].strip().lower())

    missing = [d for d in deps if d not in unc]
    if missing:
        print("warn: %i deps not in any pk3: %s" % (len(missing), missing[:5]))

    byext = collections.Counter()
    for d in deps:
        if d in unc:
            byext[d.rsplit(".", 1)[-1] if "." in d else "?"] += comp[d]

    total_c = sum(comp[d] for d in deps if d in comp)
    print("%i unique files, %.1f MB uncompressed, %.1f MB compressed"
          % (len(deps), sum(unc[d] for d in deps if d in unc) / 1048576, total_c / 1048576))
    for e, s in byext.most_common(12):
        print("  %6.1f MB compressed  .%s" % (s / 1048576, e))

    out = log + ".manifest"
    with open(out, "w") as f:
        for d in sorted(deps):
            f.write("%s\n" % d)
    print("manifest -> %s" % out)
    return 0

sys.exit(main())
