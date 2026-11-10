#!/usr/bin/env python3
"""Genere site/docs.html a partir de la table des commandes de kernel/shell.c.
Usage : python3 site/gen-docs.py   (a relancer quand on ajoute une commande)"""
import html, os, re, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(root, "kernel/shell.c"), encoding="utf-8").read()
table = src[src.index("static const struct command commands[] = {"):]
table = table[:table.index("{ NULL, NULL, NULL, NULL, 0 }")]
rows = re.findall(r'\{ "(\w+)", (NULL|"\w+"), "([^"]*)", \w+, ([01]) \}', table)
if len(rows) < 30:
    sys.exit(f"table des commandes introuvable ou incomplete ({len(rows)} lignes)")

e = html.escape
lines = []
for name, alias, helptext, rootonly in rows:
    a = "" if alias == "NULL" else alias.strip('"')
    badge = ' <span class="tag-root">root</span>' if rootonly == "1" else ""
    lines.append(f"<tr><td>{e(name)}{badge}</td><td>{e(a)}</td><td>{e(helptext)}</td></tr>")
commands_html = "\n".join(lines)

page = open(os.path.join(root, "site/docs.template.html"), encoding="utf-8").read()
out = page.replace("@@COMMANDES@@", commands_html)
open(os.path.join(root, "site/docs.html"), "w", encoding="utf-8").write(out)
print(f"docs.html genere ({len(rows)} commandes)")
