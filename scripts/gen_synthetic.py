#!/usr/bin/env python3
"""Generate a synthetic paper database and labelled reference batches.

Why synthetic data?
  * Pair A can test threading and scheduling without downloading 4 GB of arXiv.
  * We control the mix of cheap (real, with DOI) and expensive (fake) references,
    so we can create a "calm" and a "busy" batch of the same size.
  * Every reference has a ground-truth label -> accuracy is measurable.

Usage:
  python3 scripts/gen_synthetic.py --papers 20000 --refs 2000 --out data/synth
Creates:
  data/synth/papers.tsv      id, doi, title, authors, year
  data/synth/refs_calm.txt   ~90% real, 5% mutated, 5% fake
  data/synth/refs_busy.txt   ~40% real, 20% mutated, 40% fake
"""
import argparse
import os
import random

WORDS = (
    "learning neural deep graph transformer attention efficient scalable robust sparse "
    "adaptive contrastive generative language vision model models network networks "
    "representation reinforcement optimization stochastic gradient federated causal "
    "inference retrieval augmented diffusion self supervised multimodal benchmark "
    "parallel distributed memory hierarchical temporal spatial probabilistic bayesian "
    "kernel embedding embeddings alignment reasoning planning agents compression "
    "quantization pruning distillation fine tuning prompting evaluation hallucination "
    "detection segmentation recognition translation summarization question answering"
).split()
TEMPLATES = [
    "{A} {B} for {C} {D}",
    "Towards {A} {B} {C}",
    "{A} {B}: a {C} approach to {D} {E}",
    "On the {A} of {B} {C}",
    "{A} {B} with {C} {D} {E}",
    "Learning {A} {B} via {C} {D}",
    "{A} and {B} {C} in {D} {E}",
]
FIRST = "Anna Ben Chen Diego Elena Farid Grace Hiro Ines Jonas Kavya Liam Mei Noah Olga Priya Quinn Ravi Sara Tomas".split()
LAST = "Garcia Smith Wang Kumar Muller Rossi Tanaka Kim Novak Silva Ahmed Larsen Okafor Ivanova Dubois Cohen Moreau Patel Lopez Weber".split()


def make_title(rng):
    t = rng.choice(TEMPLATES)
    w = [rng.choice(WORDS) for _ in range(5)]
    title = t.format(A=w[0], B=w[1], C=w[2], D=w[3], E=w[4])
    return title[0].upper() + title[1:]


def make_authors(rng):
    return [f"{rng.choice(FIRST)} {rng.choice(LAST)}" for _ in range(rng.randint(1, 5))]


def cite(rng, title, authors, year, doi):
    """Format a reference the way an LLM typically writes it."""
    initials = [f"{a.split()[0][0]}. {a.split()[-1]}" for a in authors]
    who = ", ".join(initials[:3]) + (" et al." if len(initials) > 3 else "")
    venue = rng.choice(["NeurIPS", "ICML", "ICLR", "ACL", "CVPR", "arXiv preprint"])
    line = f'{who}. "{title}." {venue} {year}.'
    if doi:
        line += f" doi:{doi}"
    return line


def mutate(rng, p):
    """A real paper with one wrong detail (what LLMs often produce)."""
    title, authors, year = p["title"], list(p["authors"]), p["year"]
    kind = rng.choice(["year", "word", "author"])
    if kind == "year":
        year += rng.choice([-5, -4, -3, 3, 4, 5])
    elif kind == "word":
        words = title.split()
        i = rng.randrange(len(words))
        words[i] = rng.choice(WORDS)
        title = " ".join(words)
    else:
        authors[0] = f"{rng.choice(FIRST)} Zed{rng.randint(1, 999)}"
    return cite(rng, title, authors, year, None)


def batch(rng, papers, titles, n, p_real, p_mut):
    lines = []
    for _ in range(n):
        r = rng.random()
        if r < p_real:
            p = rng.choice(papers)
            # Only ~half the LLM citations include the DOI even when one exists.
            doi = p["doi"] if p["doi"] and rng.random() < 0.5 else None
            lines.append(cite(rng, p["title"], p["authors"], p["year"], doi) + "  # label=real")
        elif r < p_real + p_mut:
            lines.append(mutate(rng, rng.choice(papers)) + "  # label=mutated")
        else:
            while True:
                t = make_title(rng)
                if t.lower() not in titles:
                    break
            lines.append(cite(rng, t, make_authors(rng), rng.randint(2010, 2025), None) + "  # label=fake")
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--papers", type=int, default=20000)
    ap.add_argument("--refs", type=int, default=2000)
    ap.add_argument("--out", default="data/synth")
    ap.add_argument("--seed", type=int, default=42)
    a = ap.parse_args()

    rng = random.Random(a.seed)  # fixed seed -> reproducible experiments
    os.makedirs(a.out, exist_ok=True)

    papers, titles = [], set()
    while len(papers) < a.papers:
        t = make_title(rng)
        if t.lower() in titles:
            continue
        titles.add(t.lower())
        i = len(papers)
        papers.append({
            "id": f"synth-{i:07d}",
            # 10.5555 is a reserved test prefix, so these DOIs never collide with real ones.
            "doi": f"10.5555/synth.{i}" if rng.random() < 0.6 else "",
            "title": t,
            "authors": make_authors(rng),
            "year": rng.randint(2000, 2025),
        })

    with open(os.path.join(a.out, "papers.tsv"), "w", encoding="utf-8") as f:
        f.write("# id\tdoi\ttitle\tauthors\tyear\n")
        for p in papers:
            f.write(f'{p["id"]}\t{p["doi"]}\t{p["title"]}\t{";".join(p["authors"])}\t{p["year"]}\n')

    for name, p_real, p_mut in (("calm", 0.90, 0.05), ("busy", 0.40, 0.20)):
        lines = batch(rng, papers, titles, a.refs, p_real, p_mut)
        with open(os.path.join(a.out, f"refs_{name}.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")

    print(f"wrote {len(papers)} papers and 2 x {a.refs} references to {a.out}/")


if __name__ == "__main__":
    main()
