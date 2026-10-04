#!/usr/bin/env python3
"""Extract verse text from every bible JSON into a flat corpus.

Data preparation only: walks data/bible/<T>/<T>_bible.json ({Book:{Chapter:
{Verse:text}}}), emits data/bible_corpus.txt as
    TRANSLATION|Book|Chapter|Verse<TAB>text
one verse per line, plus data/bible_manifest.json with per-file counts.
All tokenizing stays in C. Source JSONs live on the bible-translations
legacy-corpus-2025 branch and are NOT committed (see .gitignore).
"""
import json, os, sys, glob
OUT="/home/smalley/.local/src/saphira-gigatoken/data/bible_corpus.txt"
MAN="/home/smalley/.local/src/saphira-gigatoken/data/bible_manifest.json"
pats=sorted(glob.glob("/home/smalley/.local/src/saphira-gigatoken/data/bible/*/*_bible.json"))
print(f"full-bible files: {len(pats)}")
nverse=nbytes=0; per={}
with open(OUT,"w",encoding="utf-8") as f:
    for p in pats:
        tr=os.path.basename(os.path.dirname(p)).split("_")[0]
        # translation dir name == prefix; use dir name
        tr=os.path.basename(os.path.dirname(p))
        d=json.load(open(p, encoding="utf-8"))
        tverse=tbytes=0
        for book,chaps in d.items():
            if not isinstance(chaps, dict): continue
            for chap,verses in chaps.items():
                if not isinstance(verses, dict): continue
                for verse,text in verses.items():
                    line=f"{tr}|{book}|{chap}|{verse}\t{text}\n"
                    f.write(line)
                    nverse+=1; tverse+=1
                    b=len(line.encode()); nbytes+=b; tbytes+=b
        per[tr]={"verses":tverse,"bytes":tbytes}
json.dump({"files":len(pats),"verses":nverse,"bytes":nbytes,"per_translation":per},
          open(MAN,"w"), indent=1, sort_keys=True)
print(f"files={len(pats)} verses={nverse} bytes={nbytes}")
for tr,v in sorted(per.items()): print(f"  {tr:8} verses={v['verses']:6} MB={v['bytes']/1e6:.1f}")
