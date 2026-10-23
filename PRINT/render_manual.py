#!/usr/bin/env python3
"""Render the Ukrainian assembly Markdown into a print-styled standalone HTML file."""

from html import escape
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "03-instruktsiia-zbirka-uk.md"
TARGET = ROOT / "03-instruktsiia-zbirka-uk.html"


def inline(text: str) -> str:
    text = escape(text, quote=False)
    text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
    text = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"\*(.+?)\*", r"<em>\1</em>", text)
    return text


def render(markdown: str) -> str:
    lines = markdown.splitlines()
    out: list[str] = []
    i = 0
    list_kind: str | None = None
    paragraph: list[str] = []

    def close_paragraph() -> None:
        nonlocal paragraph
        if paragraph:
            out.append("<p>" + inline(" ".join(paragraph)) + "</p>")
            paragraph = []

    def close_blocks() -> None:
        nonlocal list_kind
        close_paragraph()
        if list_kind:
            out.append(f"</{list_kind}>")
            list_kind = None

    while i < len(lines):
        line = lines[i]
        if not line.strip():
            close_blocks()
            i += 1
            continue

        if line.startswith("```"):
            close_blocks()
            i += 1
            code: list[str] = []
            while i < len(lines) and not lines[i].startswith("```"):
                code.append(lines[i])
                i += 1
            out.append("<pre><code>" + escape("\n".join(code)) + "</code></pre>")
            i += 1
            continue

        if line.startswith("|") and i + 1 < len(lines) and re.match(r"^\|[\s:|\-]+\|$", lines[i + 1]):
            close_blocks()
            rows: list[list[str]] = []
            while i < len(lines) and lines[i].startswith("|"):
                cells = [cell.strip() for cell in lines[i].strip().strip("|").split("|")]
                if not all(re.fullmatch(r"[:\- ]+", cell or " ") for cell in cells):
                    rows.append(cells)
                i += 1
            if rows:
                out.append('<table><thead><tr>' + "".join(f"<th>{inline(c)}</th>" for c in rows[0]) + "</tr></thead><tbody>")
                for row in rows[1:]:
                    row += [""] * max(0, len(rows[0]) - len(row))
                    out.append("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in row[:len(rows[0])]) + "</tr>")
                out.append("</tbody></table>")
            continue

        if line.startswith("> "):
            close_blocks()
            quote: list[str] = []
            while i < len(lines) and lines[i].startswith("> "):
                quote.append(inline(lines[i][2:].strip()))
                i += 1
            out.append('<aside class="warning">' + "<br>".join(quote) + "</aside>")
            continue

        heading = re.match(r"^(#{1,3})\s+(.+)$", line)
        if heading:
            close_blocks()
            level = len(heading.group(1))
            out.append(f"<h{level}>{inline(heading.group(2))}</h{level}>")
            i += 1
            continue

        item = re.match(r"^\s*(-|\d+\.)\s+(.+)$", line)
        if item:
            close_paragraph()
            kind = "ul" if item.group(1) == "-" else "ol"
            if list_kind != kind:
                if list_kind:
                    out.append(f"</{list_kind}>")
                out.append(f"<{kind}>")
                list_kind = kind
            out.append(f"<li>{inline(item.group(2))}</li>")
            i += 1
            continue

        paragraph.append(line.strip())
        i += 1

    close_blocks()
    return "\n".join(out)


content = render(SOURCE.read_text(encoding="utf-8"))
html = f'''<!doctype html>
<html lang="uk">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PowerBank64 — інструкція з макетування та складання</title>
<style>
@page {{ size: A4; margin: 16mm 15mm 18mm; @bottom-left {{ content: "POWERBANK64 · ІНСТРУКЦІЯ 0.1"; color: #64748b; font: 8pt sans-serif; }} @bottom-right {{ content: "Стор. " counter(page); color: #64748b; font: 8pt sans-serif; }} }}
* {{ box-sizing: border-box; }}
body {{ max-width: 180mm; margin: 0 auto; color: #1e293b; font: 10pt/1.48 "DejaVu Sans", Arial, sans-serif; }}
h1 {{ color: #102a43; font-size: 25pt; line-height: 1.12; margin: 0 0 8mm; padding: 0 0 4mm; border-bottom: 3px solid #0f766e; }}
h2 {{ color: #0f766e; font-size: 15pt; line-height: 1.2; margin: 8mm 0 3mm; padding-bottom: 1.5mm; border-bottom: 1px solid #cbd5e1; break-after: avoid; }}
h3 {{ color: #334155; font-size: 11pt; margin: 5mm 0 2mm; break-after: avoid; }}
p {{ margin: 0 0 3mm; }}
strong {{ color: #102a43; }}
code {{ font: 9pt "DejaVu Sans Mono", monospace; background: #eef2f7; padding: 0.2mm 1mm; border-radius: 1mm; }}
ul, ol {{ margin: 1mm 0 4mm; padding-left: 7mm; }}
li {{ margin: 0 0 1.5mm; padding-left: 1mm; }}
li::marker {{ color: #0f766e; font-weight: bold; }}
table {{ width: 100%; border-collapse: collapse; margin: 3mm 0 5mm; font-size: 8.8pt; break-inside: avoid; }}
th {{ background: #102a43; color: white; text-align: left; }}
th, td {{ border: 1px solid #cbd5e1; padding: 2mm; vertical-align: top; }}
tr:nth-child(even) td {{ background: #f8fafc; }}
pre {{ white-space: pre-wrap; border-left: 3px solid #0f766e; background: #f1f5f9; padding: 3mm; font: 8.4pt/1.4 "DejaVu Sans Mono", monospace; break-inside: avoid; }}
pre code {{ background: transparent; padding: 0; }}
.warning {{ margin: 3mm 0 5mm; padding: 4mm; border-left: 4px solid #c2410c; background: #fff7ed; color: #7c2d12; font-weight: 600; }}
h2, h3 {{ orphans: 3; widows: 3; }}
@media screen {{ body {{ max-width: 180mm; margin: 14mm auto; padding: 15mm; box-shadow: 0 4px 24px #94a3b8; }} }}
@media print {{ body {{ max-width: none; }} a {{ color: inherit; text-decoration: none; }} }}
</style>
</head>
<body>
{content}
</body>
</html>'''
TARGET.write_text(html, encoding="utf-8")
print(f"Wrote {TARGET}")
