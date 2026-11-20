#!/usr/bin/env python3
"""Convert the arXiv metadata snapshot (JSON lines) to our TSV format.  [Week 2, Pair B]

Download "arxiv-metadata-oai-snapshot.json" from Kaggle (Cornell-University/arxiv).
Usage:
  python3 scripts/convert_arxiv.py arxiv-metadata-oai-snapshot.json data/arxiv.tsv [--limit 20000]

Only the fields we need are kept (id, doi, title, authors, year), which makes
the C++ loading stage — our largest sequential cost — several times faster.
"""
import argparse
import json


def clean(s):
    return " ".join((s or "").replace("\t", " ").split())


def year_of(rec):
    versions = rec.get("versions") or []
    if versions and "created" in versions[0]:
        # e.g. "Mon, 12 Jun 2017 17:57:34 GMT" -> 2017
        for tok in versions[0]["created"].split():
            if tok.isdigit() and len(tok) == 4:
                return tok
    return (rec.get("update_date") or "0000")[:4]


def authors_of(rec):
    parsed = rec.get("authors_parsed")
    if parsed:  # [["Vaswani", "Ashish", ""], ...]
        return ";".join(clean(f"{a[1]} {a[0]}") for a in parsed)
    return ";".join(clean(a) for a in (rec.get("authors") or "").replace(" and ", ",").split(","))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--limit", type=int, default=0, help="stop after N papers (0 = all)")
    a = ap.parse_args()
    n = 0
    with open(a.src, encoding="utf-8") as fin, open(a.dst, "w", encoding="utf-8") as fout:
        fout.write("# id\tdoi\ttitle\tauthors\tyear\n")
        for line in fin:
            rec = json.loads(line)
            doi = clean(rec.get("doi")).split(" ")[0]
            fout.write(f'{rec["id"]}\t{doi}\t{clean(rec.get("title"))}\t{authors_of(rec)}\t{year_of(rec)}\n')
            n += 1
            if a.limit and n >= a.limit:
                break
    print(f"wrote {n} papers to {a.dst}")


if __name__ == "__main__":
    main()
