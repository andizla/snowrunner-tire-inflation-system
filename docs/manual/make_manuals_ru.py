# Builds the Russian Next Gen Tire Inflation manual (ru_tire.py) as HTML and prints it to PDF with Edge, as make_manuals.py does the English one.
# body() returns the whole document: cover, <!--TOC-->, both parts, licence; its <h2> headings and its <caption> /
# <figcaption> texts carry no numbers, finish_ru numbers them in order.
#   python docs\manual\make_manuals_ru.py
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from make_manuals import CSS, VERSION, print_pdf


def page_ru(title, body):
    return (f'<!doctype html><html lang="ru"><head><meta charset="utf-8"><title>{title}</title><style>{CSS}</style></head>'
            f'<body>{body}</body></html>')


def finish_ru(body):
    n = [0]

    def h2(m):
        n[0] += 1
        return f'<h2 id="s{n[0]}">{n[0]}. '
    body = re.sub(r'<h2>(?:\d+\. )?', h2, body)
    for tag, word in [('caption', 'Таблица'), ('figcaption', 'Рисунок')]:
        k = [0]

        def cap(m, word=word, tag=tag):
            k[0] += 1
            return f'<{tag}>{word} {k[0]}. '
        body = re.sub(rf'<{tag}>(?:{word} \d+\. )?', cap, body)
    toc = []
    for m in re.finditer(r'<div class="(part|sect)[^"]*">([^<]+)</div>|<h2 id="s(\d+)">([^<]+)</h2>', body):
        toc.append(f'<li class="grp">{m.group(2)}</li>' if m.group(1) else f'<li>{m.group(4)}</li>')
    return body.replace('<!--TOC-->', '<ol class="toc">' + ''.join(toc) + '</ol>')


if __name__ == '__main__':
    import ru_tire
    print_pdf('TirePressure_manual_' + VERSION + '_ru', page_ru('Руководство Next Gen Tire Inflation', finish_ru(ru_tire.body())))
