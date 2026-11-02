#!/usr/bin/env python3
"""mkhelp.py DOCDIR OUT -- fsv's HTML manual as plain text for Help ->
Contents (a Motif text window, no browser needed)."""
import html, re, sys

PAGES = ["t1.html", "startup.html", "window.html", "menus.html", "mouse.html", "about.html"]


def text(path):
    s = open(path, encoding="latin-1").read()
    s = re.sub(r"(?is)<(script|style|head).*?</\1>", "", s)
    s = re.sub(r"(?i)<br\s*/?>|</?(p|div|li|tr|h[1-6]|dt|dd|table)\b[^>]*>", "\n", s)
    s = re.sub(r"<[^>]+>", "", s)
    s = html.unescape(s)
    s = re.sub(r"[ \t]+", " ", s)
    s = re.sub(r" *\n *", "\n", s)
    return re.sub(r"\n{3,}", "\n\n", s).strip()


out = [text("%s/%s" % (sys.argv[1], p)) for p in PAGES]
open(sys.argv[2], "w", encoding="latin-1", errors="replace").write("\n\n\n".join(out) + "\n")
