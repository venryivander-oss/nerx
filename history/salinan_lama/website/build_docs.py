# Build website/docs.html from SYNTAX.en.txt. Run again after SYNTAX.en.txt changes:
#   python website/build_docs.py
import html, os, re

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
lines = open(os.path.join(root, 'SYNTAX.en.txt'), encoding='ascii').read().replace('\r\n', '\n').split('\n')

def slug(s):
    return re.sub(r'[^a-z0-9]+', '-', s.lower()).strip('-')

# sections: "====" line, "N. TITLE", "====" line; libraries: "----" line, "lib x - y", "----" line
sections = []          # [title, id, [ (sub title, sub id, body lines) ... ]]
cur_sub = None
i = 0
while i < len(lines):
    ln = lines[i]
    if ln.startswith('====') and i + 2 < len(lines) and lines[i + 2].startswith('===='):
        title = lines[i + 1].strip()
        m = re.match(r'(\d+)\.\s+(.*)', title)
        if m:
            name = m.group(2).capitalize().replace('Vs code', 'VS Code')
            sections.append([name, slug(name), []])
            cur_sub = [None, None, []]
            sections[-1][2].append(cur_sub)
        i += 3
        continue
    if ln.startswith('----') and i + 2 < len(lines) and lines[i + 2].startswith('----') and sections:
        name = lines[i + 1].strip()
        cur_sub = [name, slug(name), []]
        sections[-1][2].append(cur_sub)
        i += 3
        continue
    if cur_sub is not None:
        cur_sub[2].append(ln)
    i += 1

def render(body):
    out = []
    blocks, blk = [], []
    for ln in body:
        if ln.strip() == '':
            if blk:
                blocks.append(blk)
                blk = []
        else:
            blk.append(ln)
    if blk:
        blocks.append(blk)
    for b in blocks:
        h = re.match(r'^--- (.+) ---$', b[0].strip())
        if len(b) == 1 and h:
            out.append('<h4>%s</h4>' % html.escape(h.group(1)))
            continue
        if all(l.startswith('  ') for l in b):
            # 4 spaces = program text (dark box), 2 spaces = reference table
            prog = all(l.startswith('    ') for l in b)
            width = min(len(l) - len(l.lstrip()) for l in b)
            text = '\n'.join(l[width:] for l in b)
            out.append('<pre class="%s">%s</pre>' % ('prog' if prog else 'ref', html.escape(text)))
        elif b[0].startswith('- '):
            items, item = [], []
            for l in b:
                if l.startswith('- '):
                    if item:
                        items.append(' '.join(item))
                    item = [l[2:].strip()]
                else:
                    item.append(l.strip())
            items.append(' '.join(item))
            out.append('<ul>' + ''.join('<li>%s</li>' % inline(t) for t in items) + '</ul>')
        else:
            out.append('<p>%s</p>' % inline(' '.join(l.strip() for l in b)))
    return '\n'.join(out)

def inline(t):
    return html.escape(t)

toc, doc = [], []
for name, sid, subs in sections:
    toc.append('<li><a href="#%s">%s</a></li>' % (sid, html.escape(name)))
    doc.append('<section class="part" data-part>')
    doc.append('<h2 id="%s">%s</h2>' % (sid, html.escape(name)))
    for sname, ssid, body in subs:
        if sname:
            toc.append('<li class="sub"><a href="#%s">%s</a></li>' % (ssid, html.escape(sname.split('  -  ')[0])))
            doc.append('<h3 id="%s">%s</h3>' % (ssid, html.escape(sname)))
        doc.append(render(body))
    doc.append('</section>')

page = open(os.path.join(root, 'website', 'docs_template.html'), encoding='utf-8').read()
page = page.replace('{{TOC}}', '\n'.join(toc)).replace('{{DOC}}', '\n'.join(doc))
open(os.path.join(root, 'website', 'docs.html'), 'w', encoding='utf-8', newline='\n').write(page)
print('docs.html: %d sections' % len(sections))
