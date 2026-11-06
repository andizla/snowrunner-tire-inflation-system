# Builds the Next Gen Tire Inflation manual (English) as HTML and prints it to PDF with Edge, into out\manual\. The version in the
# output's name is the one in src\panel.h. Numbers in the tables are computed from the mod's own values, not typed by hand.
# The Russian manual: make_manuals_ru.py. The texts: Part 1 below, Part 2 in tech_tire.py, the Russian in ru_tire.py.
#   python docs\manual\make_manuals.py
# At a release the two PDFs are copied into docs\ as Next_Gen_Tire_Inflation_Manual.pdf and Next_Gen_Tire_Inflation_Manual_ru.pdf.
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, 'out', 'manual')
EDGE = r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
VERSION = re.search(r'#define TP_VERSION "([^"]+)"', open(os.path.join(ROOT, 'src', 'panel.h'), encoding='utf-8').read()).group(1)
TIRE_IMG = 'file:///' + ROOT.replace('\\', '/')

# ---------------------------------------------------------------- tire mod model (panel.h, outside.h, FactorsAt)
P = [0.45, 0.65, 1.0, 1.25]
ATM = 14.7 / 35.0
K = ['dirt', 'asphalt', 'mud', 'offset', 'fuel', 'steer', 'gravel', 'sand', 'rock', 'minv', 'maxv', 'mind', 'maxd', 'tick']
def M(*v): return dict(zip(K, v))
NORMAL = M(1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0)
SETS = {
 'default': [M(3.5, 0.85, 1.3, 0.10, 1.5, 0.7, 2.75, 4.0, 3.5, 20, 30, 4, 8, 7), M(3.0, 0.95, 1.15, 0.06, 1.25, 0.85, 2.4, 3.4, 3.0, 35, 45, 2, 6, 10), NORMAL,
             M(0.9, 1.3, 0.85, 0.0, 0.85, 1.1, 0.95, 0.85, 0.9, 0, 0, 0, 0, 7)],
 'hard':    [M(1.6, 0.8, 1.35, 0.05, 1.0, 0.8, 1.3, 1.9, 1.6, 30, 55, 2, 10, 8), M(1.3, 0.9, 1.18, 0.025, 1.0, 0.9, 1.15, 1.45, 1.3, 50, 75, 1, 6, 10), NORMAL,
             M(0.92, 0.97, 0.9, 0.0, 1.0, 1.05, 0.95, 0.85, 0.92, 0, 0, 0, 0, 7)],
 'insane':  [M(2.0, 0.4, 1.5, 0.12, 1.0, 0.6, 1.6, 2.3, 2.0, 15, 20, 8, 16, 2), M(1.5, 0.45, 1.25, 0.06, 1.0, 0.75, 1.3, 1.7, 1.5, 20, 25, 4, 12, 2), NORMAL,
             M(0.92, 0.97, 0.9, 0.0, 1.0, 1.05, 0.95, 0.85, 0.92, 0, 0, 0, 0, 7)],
}
ENVELOPE = [(0.9, 3.2, 0.4), (1.0, 3.2, 0.2), (1.5, 1.5, 2.0), (1.6, 1.5, 1.9), (2.0, 3.0, 1.2), (2.2, 2.6, 1.3), (2.8, 1.2, 1.8), (2.85, 1.2, 1.78),
            (3.0, 0.5, 8.0), (3.0, 0.8, 3.5), (3.0, 1.2, 1.3), (3.1, 0.8, 3.4), (3.1, 1.0, 1.9), (3.2, 0.8, 1.5), (3.2, 1.0, 1.3), (3.3, 0.6, 2.2)]

def effect(c):
    below, above = max(0.0, 15 - c), max(0.0, c - 15)
    return dict(pressure=(c + 273.15) / 288.15, hard=max(0.7, min(1.08, 1 - 0.006 * below + 0.003 * above)),
                soft=max(0.85, 1 - 0.003 * below), speed=max(0.75, min(1.25, 1 + 0.006 * (15 - c))))
def air(p, c): return (p + ATM) * effect(c)['pressure'] - ATM
def bfactor(g, a, m):
    best = 0.0
    for t in ENVELOPE:
        r = min(t[0] / g, t[1] / a, t[2] / m)
        best = max(best, r)
    return min(1.0, best)
def balance(g, a, m, s):
    f = bfactor(g, a, m)
    return 1 - s * (1 - f) if s <= 1 else f * (1 - (s - 1) * (1 - f))

def x(v): return f'×{v:.2f}'.replace('.00', '') if v in (1, 1.0) else f'×{v:.2f}'.rstrip('0').rstrip('.')
def kmh(ms): return f'{ms:.0f} km/h'

def table(cols, rows, cap='', cls='', first_left=True):
    h = f'<table class="{cls}">' + (f'<caption>{cap}</caption>' if cap else '') + '<thead><tr>' + ''.join(f'<th>{c}</th>' for c in cols) + '</tr></thead><tbody>'
    for r in rows:
        h += '<tr>' + ''.join(f'<td>{c}</td>' for c in r) + '</tr>'
    return h + '</tbody></table>'

def modes_table(name, cap, with_increased=True):
    s = SETS[name]
    idx = [0, 1, 3] if with_increased else [0, 1]
    heads = ['', 'Low', 'Reduced'] + (['Increased'] if with_increased else [])
    rows = [['Grip on dirt'] + [x(s[i]['dirt']) for i in idx], ['Grip on gravel'] + [x(s[i]['gravel']) for i in idx],
            ['Grip on sand'] + [x(s[i]['sand']) for i in idx], ['Grip on rock'] + [x(s[i]['rock']) for i in idx],
            ['Grip on asphalt'] + [x(s[i]['asphalt']) for i in idx], ['Grip in mud'] + [x(s[i]['mud']) for i in idx],
            ['Tire radius, up to'] + [f'{s[i]["offset"] * 100:g} cm less' if s[i]['offset'] else 'unchanged' for i in idx],
            ['Fuel use while moving'] + [x(s[i]['fuel']) for i in idx], ['Steering speed'] + [x(s[i]['steer']) for i in idx],
            ['Wear starts above'] + [kmh(s[i]['minv']) if s[i]['maxd'] else 'no wear' for i in idx],
            ['Most wear from'] + [kmh(s[i]['maxv']) if s[i]['maxd'] else '' for i in idx],
            ['Damage per wheel'] + [f'{s[i]["mind"]:g} to {s[i]["maxd"]:g}' if s[i]['maxd'] else '' for i in idx],
            ['Every'] + [f'{s[i]["tick"]:g} seconds' if s[i]['maxd'] else '' for i in idx]]
    return table(heads, rows, cap)

def temp_table():
    rows = []
    for c in [-40, -30, -20, -10, 0, 10, 15, 25, 35, 45]:
        e = effect(c)
        lab = f'{c} °C' if c != 15 else '15 °C (reference)'
        rows.append([lab, f'{air(1.0, c) * 100:.0f}%', f'{air(0.45, c) / 0.45 * 100:.0f}%', x(e['hard']), x(e['soft']), x(e['speed'])])
    return table(['Outside', 'Normal holds', 'Low holds (of its own)', 'Asphalt and rock', 'Dirt, gravel, sand, mud', 'Wear speeds'], rows,
                 'Table 5. What the outside temperature does')

def balance_table():
    rows = []
    for name, (g, a, m) in [('A strong modded tire', (4.5, 5.0, 4.0)), ('A modded Ural', (3.55, 3.5, 2.8)), ('A mud tire, strong in mud only', (2.0, 0.6, 5.0)),
                            ('Any vanilla tire', (2.2, 2.6, 1.3))]:
        cells = [f'{name} ({g:g} / {a:g} / {m:g})']
        for s in [0.5, 1.0, 1.5, 2.0]:
            k = balance(g, a, m, s)
            cells.append(f'{x(k)} <span class="dim">{g * k:.2f} / {a * k:.2f} / {m * k:.2f}</span>')
        rows.append(cells)
    return table(['Tire (ground / asphalt / mud)', 'Strength 0.5', '1', '1.5', '2'], rows, 'Table 6. The vanilla balance at different strengths', 'wide')

# ---------------------------------------------------------------- shared look
CSS = '''
@page { size: A4; margin: 16mm 15mm 16mm 15mm; }
body { font-family: "Segoe UI", Arial, sans-serif; font-size: 10pt; line-height: 1.45; color: #1d2329; background: #fff; }
.cover { border-bottom: 3px solid #c6532a; margin-bottom: 12pt; padding-bottom: 6pt; }
.cover h1 { font-size: 22pt; margin: 0; color: #12171c; }
.cover .sub { color: #5a6570; margin: 2pt 0 0; }
.part { font-size: 15pt; color: #fff; background: #2b333b; padding: 5pt 9pt; margin: 18pt 0 6pt; break-after: avoid; }
.part.break { break-before: page; }
h2 { font-size: 13pt; margin: 14pt 0 4pt; color: #c6532a; break-after: avoid; }
h3 { font-size: 10.5pt; margin: 10pt 0 2pt; color: #12171c; break-after: avoid; }
p { margin: 4pt 0; }
ul, ol { margin: 3pt 0 4pt 15pt; padding: 0; }
li { margin: 1.5pt 0; }
code { font-family: Consolas, monospace; font-size: 9pt; background: #f3f1ee; padding: 0 2pt; }
pre { font-family: Consolas, monospace; font-size: 8.8pt; background: #f3f1ee; border-left: 3px solid #c6532a; padding: 5pt 8pt; white-space: pre-wrap; }
.box { background: #eef4f8; border-left: 3px solid #3a7fb0; padding: 5pt 9pt; margin: 6pt 0; }
.warn { background: #fbf4e6; border-left: 3px solid #e0a63a; padding: 5pt 9pt; margin: 6pt 0; }
table { border-collapse: collapse; width: 100%; margin: 7pt 0 6pt; font-size: 9pt; break-inside: avoid; }
caption { text-align: left; font-weight: 600; padding-bottom: 3pt; color: #12171c; }
th { background: #2b333b; color: #fff; font-weight: 600; padding: 3pt 5pt; text-align: left; }
td { padding: 2.5pt 5pt; border-bottom: 1px solid #e3e1dd; vertical-align: top; }
.dim { color: #7a858f; font-size: 8.3pt; }
figure { margin: 8pt 0; break-inside: avoid; }
figure img { max-width: 100%; border: 1px solid #d8d4cd; }
figcaption { font-size: 8.5pt; color: #5a6570; }
.toc { columns: 2; font-size: 9.5pt; }
.toc li { break-inside: avoid; }
.toc { margin-left: 0; } .toc li { list-style: none; }
.toc .grp { font-weight: 600; color: #c6532a; margin-top: 3pt; }
.intro { color: #3d454d; margin: 6pt 0 10pt; }
.sect { font-size: 11.5pt; font-weight: 600; color: #2b333b; border-bottom: 1.5px solid #2b333b; margin: 14pt 0 4pt; padding-bottom: 2pt; break-after: avoid; }
.sect.break { break-before: page; }
.eq { font-family: "Cambria Math", Cambria, "Times New Roman", serif; font-size: 11pt; margin: 5pt 0 5pt 16pt; break-inside: avoid; }
.eq .n { float: right; font-family: "Segoe UI", Arial, sans-serif; font-size: 8.5pt; color: #7a858f; }
.where { font-size: 9pt; margin: 2pt 0 6pt 16pt; color: #3d454d; }
.src { font-size: 8.3pt; color: #7a858f; }
table.k td:first-child { font-family: Consolas, monospace; font-size: 8.6pt; white-space: nowrap; }
table.small { font-size: 8.4pt; }
figure svg { display: block; border: 1px solid #e3e1dd; background: #fff; }
'''
def page(title, body):
    return f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>{title}</title><style>{CSS}</style></head><body>{body}</body></html>'

# ---------------------------------------------------------------- tire manual
TIRE = f'''
<div class="cover"><h1>Next Gen Tire Inflation</h1><p class="sub">Manual for version 1.3.4 (beta), November 2026</p></div>
<!--TOC-->

<p class="intro">Part 1, for dummies, says what the mod does and how to use it, first the basics and then feature by feature. Part 2, the technical reference, gives the models behind it: the formulas and constants and where each number comes from, how the mod gets into the game and reads and writes its memory, and what it cannot do.</p>
<div class="part">Part 1. For dummies</div>
<div class="sect">Getting started</div>

<h2>1. What it does</h2>
<p>The truck you drive gets tire pressure you can change while you play, as in Expeditions: A MudRunner Game. Let air out and the tires flatten: they grip harder in dirt, sand, on rock and in mud, but lose grip on asphalt, and the truck burns more fuel and steers slower. Pump them up for the road. Drive too fast on soft tires and they wear out and go flat.</p>
{table(['Mode', 'Use it for', 'Keep under'], [['Low', 'Deep mud, sand, rocks, crawling', '20 km/h'], ['Reduced', 'Forest tracks, mixed ground', '35 km/h'], ['Normal', 'Everything else, as the game ships it', 'no limit'], ['Increased', 'Roads: more grip on asphalt, less fuel', 'no limit']])}

<h2>2. What you need</h2>
<ul><li>SnowRunner for Windows. The mod is made on the Steam version. The Epic Games Store and Game Pass versions have not been tested: the mod checks the game itself at every start, and where it cannot work it does nothing and says so in its log.</li>
<li>ReShade 6.8.0, which comes in the zip as <code>dxgi.dll</code> and <code>ReShade.ini</code>, for compatibility with overlays such as RivaTuner, Discord, Nvidia and Steam. It shows nothing of its own; Home opens its window. A ReShade already in the game works as it is (any ReShade from 6.8.0 on with add-on support). Without ReShade the game can crash with the RivaTuner or Discord overlay or with multiple monitors, and the mod says so at the start.</li></ul>

<h2>3. Install</h2>
<ol><li>Close the game.</li>
<li>Open the game's <code>Sources\\Bin</code> folder, where <code>SnowRunner.exe</code> is. In Steam: right click SnowRunner, Manage, Browse local files, then <code>Sources\\Bin</code>.</li>
<li>Copy the files from the zip there: <code>version.dll</code>, <code>TirePressure.asi</code>, <code>dxgi.dll</code> and <code>ReShade.ini</code>.</li>
<li>Start the game. F2 opens the NEXT GEN menu, which has a Tire Inflation tab.</li></ol>
<p>On Linux (Proton, the Steam Deck) set SnowRunner's launch options in Steam (right click, Properties, General) to <code>WINEDLLOVERRIDES="version=n,b" %command%</code>. Then the game takes this <code>version.dll</code> instead of Wine's own. The mod there is not yet tested by a player.</p>
<div class="box"><b>Already using ReShade?</b> Choose Skip for <code>dxgi.dll</code> and <code>ReShade.ini</code> when Windows asks, and yours stays. The mods draw without ReShade too. It is in the zip for RivaTuner users, whose game does not boot without it. With Spotter installed, the overlay draws through Spotter's frame hook; <code>NextGen.ini</code> <code>[Overlay] Spotter=0</code> turns it off. <b>Already using other mods?</b> If other <code>.asi</code> mods already work in that folder, you have an ASI loader: copy only <code>TirePressure.asi</code>. If another mod's <code>version.dll</code> is there and it is not an ASI loader, rename that one to <code>version_chain.dll</code> first; this loader passes everything on to it. To update, copy the new <code>TirePressure.asi</code> over the old one, and keep only one <code>.asi</code> file of this mod in the folder. Coming from 1.2 with the ReShade it brought: this zip's <code>dxgi.dll</code> can go over it.</div>

<h2>4. Your first drive</h2>
<ol><li>Stop the truck. By default the pressure only changes while the truck stands still.</li>
<li>On a pad, hold LB and press the d-pad down: the panel opens. Pick a mode with the d-pad left or right and confirm with A. On the keyboard, press F3: it opens the panel and steps the pressure down; leave it alone for five seconds and the choice confirms itself.</li>
<li>Listen: the air goes out (or in) for up to six seconds, and the tires flatten.</li>
<li>Drive. If the red <b>TIRE DAMAGE</b> warning shows at the top left, you are too fast for the pressure: slow down or pump the tires up.</li></ol>
<figure><img src="{TIRE_IMG}/docs/panel.png"><figcaption>The panel in Low, Reduced, Normal and Increased, and as the keyboard shows it.</figcaption></figure>
<figure><img src="{TIRE_IMG}/out/preview/warning.png" style="width:55%"><figcaption>The warning while the truck is too fast for its tires, with the most worn tire's damage.</figcaption></figure>

<h2>5. The settings worth knowing</h2>
<p>All of them are in the Tire Inflation tab of the NEXT GEN menu and save themselves into <code>TirePressure.ini</code>. F2 opens and closes the menu; to give it another key, click the key's name at the menu's top right and press the new one. The key is kept in <code>NextGen.ini</code>, which the Next Gen mods share, and while the menu is open the game gets no mouse and no keys, and the panel's key and pad buttons rest. With Next Gen Graphics installed the menu also has a Graphics tab. It switches Graphics' effects while you play. Its F keys are off then; KeysWithMenu=1 in SnowRunnerShadows.ini keeps them.</p>
<ul><li><b>Hard mode</b>: the numbers and options nearest to real tires, all at once. Everything else is greyed out while it is on.</li>
<li><b>Globalhacker insanity mode</b>: harder than real tires, for players who want the game to fight back.</li>
<li><b>Pressure changes only while the truck stands still</b>: on by default. Switch it off to change pressure while driving.</li>
<li><b>Tires feel the outside temperature</b>: on by default. Cold air means softer tires and cold rubber grips less on the road. With Next Gen Gameplay running, the temperature follows the map and the hour.</li>
<li><b>Vanilla balance</b>: for modded trucks with tires that grip better than anything in the game. It brings them down to the vanilla level, or below with a strength over 1.</li>
<li><b>Sound volume, the beep, the panel key and the panel's size.</b></li></ul>

<h2>6. When something is wrong</h2>
<ul><li>Open <code>TirePressure.log</code> in <code>Sources\\Bin</code>. If an early line says "standing down", the mod could not work with your build of the game and changed nothing: send that log to the author.</li>
<li>When another program holds the game's swap chain in a way the mod cannot draw on, <code>TirePressure.log</code> names that program.</li>
<li>No panel and no menu: the "overlay:" lines in <code>TirePressure.log</code> say why. A minimap mod with a ReShade add-on mode of its own needs that mode on next to ReShade: without it the game quits while loading.</li>
<li>The game crashes without ReShade with the RivaTuner or Discord overlay or with multiple monitors: put ReShade in from the zip (Install, step 3). With ReShade the same PCs ran fine.</li>
<li>Support, discussion and ideas: the author's channel on EnthusiastRunner's Discord, <a href="https://discord.com/invite/MssDb2Pvcj" target="_blank" rel="noopener noreferrer">discord.com/invite/MssDb2Pvcj</a>, or an issue on GitHub for a bug report; <code>GrabDumps.bat</code> from the zip gathers the logs, the ini files and the crash dump into one zip for it.</li>
<li>"key pressed, no driven truck found" in the log: you were not driving a truck.</li>
<li>To remove the mod, delete <code>TirePressure.asi</code>, <code>TirePressure.ini</code>, <code>TirePressure.log</code> and <code>TirePressure.prev.log</code> from <code>Sources\\Bin</code>. <code>version.dll</code> and <code>AsiLoader.log</code> go too when no other <code>.asi</code> mod is left (Next Gen Gameplay loads through them). If you renamed another mod's <code>version.dll</code> to <code>version_chain.dll</code>, rename it back. <code>NextGen.ini</code> goes too when no other Next Gen mod is left. ReShade from the zip goes the same way when nothing else uses it: <code>dxgi.dll</code>, <code>ReShade.ini</code> and <code>ReShade.log</code>.</li></ul>

<div class="sect break">Feature by feature</div>

<h2>7. Pressure modes and the pressure scale</h2>
<p>The numbers are multipliers against the tire's own values from its file; Normal is the tire as the game ships it.</p>
{modes_table('default', 'Table 1. The modes as they come')}
<p>The pressure itself is one number on a scale, and every mode is a point on it: Low 45%, Reduced 65%, Normal 100% and Increased 125% of Normal's pressure. Every factor is read off the scale straight between the two nearest modes, so a change walks through all the values in between instead of jumping. Normal to Low takes <code>Seconds</code> (6 by default) and a smaller step its share: Normal to Reduced about two thirds of it. A choice changed half way turns round where the pressure stands.</p>
<p>The mode belongs to the truck you drive and stays with it (<i>Each truck keeps its own pressure</i>, below). A truck not driven before starts in the highest mode on offer. <code>Increased=0</code> leaves the road mode out. Every number in the table is editable in the tab, per mode.</p>

<h2>8. Grip by surface</h2>
<p>A SnowRunner tire file has three grip numbers: <code>BodyFriction</code> for plain ground, <code>BodyFrictionAsphalt</code> for paved ground and <code>SubstanceFriction</code> for mud. The mod gets six surfaces out of them from how the game applies them.</p>
<ul><li>Every frame the game marks each wheel with what lies under it: paved, gravel, sand, hard (rock), and the soil and mud layers. The mod reads those marks 20 times a second.</li>
<li>The game's contact code sets the wheel's physical friction from one of the tire's numbers: on paved ground from the asphalt value on the wheel, otherwise from a per-wheel "ground grip" list on the truck that holds each tire's <code>BodyFriction</code>, and in mud from the mud value.</li>
<li>The mod writes the inputs, never the physical friction, which the game rewrites every frame. Into each wheel's slot of the ground list it puts the tire's own <code>BodyFriction</code> times the mode's factor for what is under that wheel right now: dirt, gravel, sand or rock. Asphalt and mud get their own factors on the wheel. Gravel, sand and rock are not separate game values; the mod swaps the one ground number per wheel as the ground under it changes.</li>
<li>Each wheel is on its own: a truck with the left side on a gravel road and the right side in sand grips differently left and right. Ice is left to the game, which has its own ice friction per wheel. Each grip multiplier stops at 10, as in Expeditions, and so does the ground grip the game is given.</li></ul>

<h2>9. Flattening and how a flat tire rolls</h2>
<p>A lower pressure shrinks the wheel's collision cylinder, and the game's own soft tire rendering draws the flattening. The game's wheel shader can only draw a tire so flat before the tread sinks into the ground, and that limit depends on the tire's size. The mod works it out from the shader's own formula:</p>
{table(['Tire radius', '0.35 m', '0.47 m', '0.65 m', '0.80 m', '1.00 m'], [['Can be drawn flatter by', '7.1 cm', '8.6 cm', '10.2 cm', '10.8 cm', '10.3 cm']], 'Table 2. How flat the game can draw a tire')}
<p>The tire's own offset from its file takes part of that room. Where the deepest mode asks for more than a tire has, every mode's flattening shrinks by the same share, so the deepest mode takes all the room: a scout on 94 cm tires gets 6.7 cm at Low and 4 cm at Reduced. The cylinder always keeps at least 40% of the wheel's radius.</p>
<p><b>Stiff base.</b> One checkbox in the Realism section, or <code>StiffBase=1</code>. It is on by default. Each tire type in the game has a softness: on a wheel of radius 1 a Stiff tire sits 2 cm into its round shape, a Soft one 3 cm and some scout tires 10 cm. The modes go on top of that, so a soft tire at Low was flatter than a Stiff one. With this on every tire starts at most as far in as a Stiff one of its size. A tire already stiffer keeps its own. Untick it for each tire's own softness. The mod takes the tire file's radius as 1, as every vanilla tire file has it. A modded tire file with another radius is held a little off, never past its radius.</p>
<p><b>Tires that come soft.</b> Some modded tires sink into the ground under the truck's weight: their contact sits inside the round shape the game collides with, and the game draws a tire by where it touches the ground, so such a tire already looks flatter at Normal. The mod measures that sink under each wheel, smoothed over a couple of seconds, and takes it off the room first: a soft tire is flattened less, and no tire is drawn into the ground. A vanilla tire sinks next to nothing and is flattened as the table says. The tab's truck section shows the first wheel's sink and the room left. In the cold no mode goes past the room either.</p>
<p>A real tire's belt keeps its length, so a flattened tire rolls nearly as far per turn as before. In the game the wheel <i>is</i> the collision cylinder, so a flattened tire would lose about three times the speed a real one does. With <b>Flattened tires roll like real ones</b> (<code>RollingRadius=1</code>, the default) the mod raises the truck's gear speeds by the difference: that scout loses about 5% of its speed at Low and 3% at Reduced instead of 15% and 9%. Worn out tires are left out of that sum.</p>

<h2>10. Fuel and steering</h2>
<p>The fuel factor scales the truck's engine and gearbox <code>FuelConsumption</code>, and only counts while the truck moves: standing at idle burns as usual. The steering factor scales the truck's steering speed, so soft tires turn slower.</p>

<h2>11. Rolling drag</h2>
<p>One checkbox in the Realism section, or <code>RollingDrag=1</code>. It is on by default; untick it to turn it off. Hard mode and the insanity mode keep it on. A tire rolls harder on soft ground and when it is aired down. The game slows a rolling truck by itself, but its tires roll the same at every pressure. With this on the mod adds a rolling resistance by the ground and the pressure.</p>
<p>The resistance is a force of Crr × N on each wheel on the ground. N is the wheel's share of the truck's weight. Crr goes by the ground under the wheel:</p>
{table(['Ground', 'Crr', 'Slows the truck at Normal, m/s²', 'At Low, m/s²'], [['Paved', '0.010', '0.10', '0.15'], ['Rock', '0.015', '0.15', '0.22'], ['Gravel', '0.020', '0.20', '0.29'], ['Dirt, mud and no flag', '0.030', '0.29', '0.44'], ['Sand', '0.060', '0.59', '0.88'], ['Ice', '0', '0', '0']], 'Table 11. Rolling drag by ground')}
<p>The pressure scales it by (p / p<sub>Normal</sub>)<sup>−0.5</sup>: Low ×1.49, Reduced ×1.24, Normal ×1, Increased ×0.89. It follows the pressure through a change. The numbers are the mod's own, from published truck tire figures (0.006 to 0.01 on asphalt at the rated pressure).</p>
<ul><li>The game shows mud to the mod as no ground of its own. So mud gets dirt's number, on top of the game's own drag in mud. The mod cannot yet tell the two apart.</li>
<li>The weight is the truck's as the game's physics has it, its gravity included. Cargo and a trailer are not counted.</li>
<li>The drag goes in through each wheel's own damping, the way the game drags a flat tire. It acts with the foot off too. A broken wheel keeps the game's own.</li>
<li>It needs the game's truck update, as tire damage on the game's thread does. On a build where the mod does not find it, rolling drag stays off and the log says so.</li>
<li>Fuel: the engine works harder against the drag and burns more by itself. So while rolling drag is on, every mode's fuel factor is 1, the default modes' too. Their Fuel use rows keep their values, which count again with rolling drag off.</li></ul>

<h2>12. Tire damage</h2>
<p>While the truck is faster than the mode's first speed, a timer runs. Each time it passes the mode's interval, every wheel on the ground takes damage: the least at the first speed, rising to the most at the second speed and above. Below the first speed for a full second, the timer starts over. Increased and Normal never wear.</p>
<ul><li>A wheel takes about 50 damage; the exact number is the wheel's own. The warning shows the most worn tire in percent, with a bar.</li>
<li>A worn out tire goes flat. It is the game's own wheel damage: the HUD's wheel icon turns red, the damage stays in the save, and a garage or a service repairs it like any other.</li>
<li>A worn out tire keeps <b>a slight grip</b>, <code>WornGrip</code> percent of its own on the ground, on asphalt and in mud (10 by default, so the truck still moves; 0 gives none, as from 1.2), and is <b>drawn flat</b>, as flat as the game can draw it, until it is repaired.</li>
<li>The mod deals the damage on the game's own thread, at the point in its frame where the game deals damage itself, and runs the game's damage update after it. On a build of the game where it does not find that place, it deals the damage from its own thread and makes the tire flat itself; the game's damage display then catches up at the next damage the game deals.</li>
<li>The outside temperature moves the two speeds (section 14).</li></ul>

<h2>12. Changing only while standing still</h2>
<p>With <code>StandStill=1</code> (the default) the pressure only moves while the truck stands, below about 1 km/h. A choice made while driving waits, and a change under way pauses and goes on when the truck stops again. While it waits, an amber hint "TIRE PRESSURE: Stop the truck to change the pressure" shows at the top left.</p>
<p><b>Each truck keeps its own pressure.</b> With <code>PerTruck=1</code> (the default) the mode chosen for a truck is saved in <code>TirePressure.trucks.ini</code> next to the exe, with a line such as <code>Mode=Low</code>. It goes by the game's own id for the truck, in a section <code>[kind:{{id}}]</code> with <code>Own=1</code>. A section <code>[kind]</code> holds what a truck of that kind no one has changed reads. The mode in force is the truck's own, else its kind's, else the start mode. The moment that truck is driven again its mode comes back, and the pressure with it: no sound, no compressor, no wait. It holds across map loads and game starts. A truck not driven before starts in the highest mode on offer: Increased, or Normal with Increased off. Two trucks of one kind each keep their own. <b>This truck only</b>, under the checkbox, is ticked unless you untick it for a truck. Unticked, the truck's own section stays with <code>Own=0</code>, the kind's mode is in force, and a mode chosen goes to the kind's section. Where the kind has no section yet, the truck's mode goes up into it first, so nothing moves. Ticked again, the truck's own mode comes back. It is greyed out while the game gives no id for the truck, which then goes by its kind. When the game makes a truck anew under another id (a recovery, a garage), the new id takes the old one's mode over, as Next Gen Gameplay does, and the log says so in one line. It does so when the truck comes back within 20 seconds, or on the same truck however long it took, as after a visit to the garage. The old id's section stays, marked <code>TakenBy</code> and <code>TakenAt</code>, and only the newest 10 marked sections of the file are kept. A file from 1.3.1 to 1.3.3 reads as before: a truck takes its kind's mode until a mode is chosen for it, which then goes into its own section. With <code>PerTruck=0</code> one mode serves every truck, and every game start begins in the highest mode on offer. The checkbox is in the tab's Realism section, and the tab's truck section names the kind.</p>

<h2>13. The outside temperature</h2>
<p>With <code>Temperature=1</code> (the default) the outside temperature acts three ways. All the modes' numbers are set for 15 °C.</p>
<ul><li><b>The air.</b> It follows the gas law on absolute pressure, with Normal taken as 35 psi on the gauge (49.7 psi absolute). Cold air is less pressure, and since every factor is read off the pressure scale, a cold tire slides toward the next mode down: at minus 20, Normal sits halfway between Reduced and Normal and gets about half of Reduced's grip on soft ground and half its flattening. Heat slides it up toward Increased, even when the Increased mode is switched off.</li>
<li><b>The rubber.</b> Cold rubber hardens and grips less, most on hard surfaces (asphalt and rock); warm rubber grips a little better on them.</li>
<li><b>The wear.</b> A soft tire heats its sidewalls at speed: in the heat it wears from lower speeds, in the cold from higher ones.</li></ul>
{temp_table()}
<p>The temperature is <code>AmbientC</code> in the ini (15 to begin with), or, while the Gauges mod runs, the map's and the hour's from Gauges, and then the tab's slider has no effect. It is taken in half-degree steps. The tab line "the tires hold A% of Normal's pressure, B% once the air's temperature counts" shows the mode's place on the scale (A) and where the temperature moved it (B). A separate document goes through the temperature model with full tables.</p>

<h2>14. Hard mode</h2>
<p>One checkbox at the top of the Realism section, or <code>HardMode=1</code>. It sets the numbers and options nearest to real tires: a change takes half a minute and only while the truck stands, the tires feel the outside temperature, soft tires wear, flattened tires roll like real ones, vanilla balance is on at strength 1, rolling drag is on, no asphalt floor and no base grip. The modes' fuel factors are 1, as the engine burns more against the drag by itself. The rest of the settings are greyed out; the ini keeps your own numbers for when it is off.</p>
{modes_table('hard', 'Table 3. Hard mode')}
<p>Airing a tire down to half its pressure lengthens its contact patch by about half: that much more grip on sand and soft ground, a little less on hard ground. Over-inflating does the reverse and gains nothing on asphalt. A soft tire's sidewalls flex and heat at speed, so its wear starts higher up than the defaults but ends a tire faster.</p>

<h2>15. Globalhacker insanity mode</h2>
<p>One checkbox under hard mode, or <code>InsaneMode=1</code>; it excludes hard mode and greys out the same settings. It is built from the own settings of the player Globalhacker and is harder than real tires: a change takes 25 seconds and only while the truck stands, the tires feel the temperature, flattened tires lose the speed the smaller wheel loses, there is no Increased mode, the vanilla balance runs at strength 2, and rolling drag is on. The fuel factors are 1, as in hard mode.</p>
{modes_table('insane', 'Table 4. Globalhacker insanity mode', with_increased=False)}
<div class="warn">These numbers were made for tire files already cut by 45% on the ground and in mud. On vanilla tires they are milder than they look: Low ends near vanilla grip, not far above it. The next version also sets the base grip on the ground and in mud to ×0.55 in this mode, so it plays as its author plays it.</div>

<h2>16. Vanilla balance</h2>
<p>Some modded trucks come with tires that grip better than any tire in the game on the ground, on asphalt and in mud at once. The vanilla balance brings such a tire down to the vanilla level.</p>
<ul><li>The yardstick is the <b>vanilla envelope</b>: of the game's 441 vanilla truck tires (94 different grip sets), the 16 that no other tire matches or beats in all three numbers at once. It is read from the game's stock files, so tire edits made by other mods do not move it.</li>
<li>A tire is cut by the smallest factor that lets some vanilla tire match or beat it in all three. Vanilla tires are inside the envelope and never change. A tire strong in one direction only loses little; one stronger than every vanilla tire everywhere ends level with the best vanilla all-rounder.</li>
<li><b>Strength</b> 0 to 1 goes from none to all the way to the vanilla level. Above 1 the cut goes on: the tire's ratio to the vanilla tire is applied again, so at 2 a tire ends as far below the vanilla tire as it started above it. It never reaches zero, and vanilla tires stay as they are at any strength.</li>
<li>The tab's line "Vanilla balance makes that ×k: ground ..., asphalt ..., mud ..." shows the result for the truck you drive while you move the slider.</li></ul>
{balance_table()}
<div class="warn">Mods that <i>raise</i> vanilla tires' grip clash with the balance: a vanilla tire made stronger than the stock envelope gets cut back, in all three numbers. Mods that lower vanilla tires are fine.</div>

<h2>17. Base grip and the asphalt floor</h2>
<p><b>Base grip</b> (<code>BaseGround</code>, <code>BaseAsphalt</code>, <code>BaseMud</code>) scales every mode's grip, Normal too, on top of the vanilla balance: a quick way to make every truck grip less (or more) without touching the mode table. Base ground covers dirt, gravel, sand and rock.</p>
<p>The <b>asphalt floor</b> (<code>AsphaltFloor</code>, 0 = off) lifts slippery tires to at least that much grip on paved ground, as if their tire file said so; the modes act on top. Vanilla off-road and mud tires have 0.4 to 1.2 on asphalt, highway tires about 3. The idea is Naybour's.</p>

<h2>18. The panel, the keyboard and the pad</h2>
{table(['What', 'Pad (default)', 'Keyboard'], [['Open the panel', 'LB + d-pad down', 'F3'], ['Lower the pressure', 'd-pad left (with the panel open)', 'F3 steps down, from Low round to the highest mode'], ['Raise the pressure', 'd-pad right (with the panel open)', ''], ['Confirm', 'A', 'leave it: it confirms itself after 5 s'], ['Close unchanged', 'B', '']], 'Table 7. Controls')}
<ul><li>While the panel is open the d-pad works without LB, as in Expeditions, and the mod hides the plain d-pad and the panel's buttons from the game. LB + d-pad left or right is never the panel's, open or closed, so a gear on that chord keeps working. In menus and outside a truck the pad's buttons are the game's alone. While the game's function menu is up, and for a quarter second after it closes, the panel's pad and key controls wait and the mod hides nothing from the game.</li>
<li>Every binding can be set in the tab: press its button, hold the pad buttons, let go. Any pad Windows sees as an Xbox pad works.</li>
<li><code>ConfirmSeconds</code> (5) is how long a choice waits before it applies itself; 0 means only the pad's confirm. The keyboard has no confirm key, so it needs this.</li>
<li><b>Show the panel</b> off (<code>UI=0</code>): the key changes the pressure at once, without the panel. With the beep on, the mod beeps once for Normal, twice for Reduced, three times for Low and once high for Increased.</li></ul>

<p>With a pad, the triggers are shaped before the game reads them: a trigger's share is raised to the power set by <b>Pad throttle curve</b> and <b>Pad brake curve</b> in the tab's Controls section (<code>PadThrottleCurve</code>, <code>PadBrakeCurve</code>, 1 to 3, 2 by default). At 2 half the trigger gives a quarter and the full pull stays full, so a light pull is gentle; at 1 the game reads the trigger as it is. It works only while a truck is driven, so menus see the pad as it is.</p>
<h2>19. Sounds</h2>
<ul><li>Deflating plays the air being let out, weaker as the tires empty, and stops with a puff. Filling plays the air going in. Both are recordings of a real tire made for this mod.</li>
<li>The compressor does not run at every filling. The air comes from a tank: the compressor fills it again once it has fallen far enough, at every second filling from Low to Normal and every fourth of a single step. It then runs for about 17 seconds and lets its own air go as it stops. The compressor is made by the mod's code; the air it lets go is a sound of SnowRunner itself, read from the game's sound file while it runs.</li>
<li>You hear them while you drive a truck and the game's window is in front. <code>SoundVolume</code> 0 turns them off.</li>
<li><code>AirOutSound</code>, <code>AirInSound</code>, <code>CompressorSound</code> and <code>CompressorStopSound</code> put another sound in place of one of the four: a WAV file in <code>Sources\\Bin</code>, or a sample from the game's <code>shared_sound.pak</code> such as <code>[sound]\\actors\\actor_lamp_generator_loop.pcm</code>, or <code>none</code>. A WAV with a loop marked in it plays up to the loop, goes round in it, and plays what follows as the sound stops.</li></ul>

<h2>20. With other mods</h2>
<ul><li><b>Gauges</b> (the gauge cluster by the same author). Each mod publishes a small block of shared memory for the other. Gauges hands over the outside temperature by map and hour; the tire mod hands over its mode, pressure and wear, which Gauges shows as a tenth lamp: a tire with L, R, N or +, amber at Low and Reduced, green at Increased, blinking while the pressure moves, red while the tires take damage or one is worn out.</li>
<li><b>ASI loaders</b>: see section 3. A second copy of this mod under another name stands down and says so in the log.</li>
<li><b>Mods that edit tire files</b>: the modes multiply whatever values the game loaded, so they work on top. Only the vanilla balance compares against the stock values (section 17).</li>
<li>The mod writes no game file, so it does not conflict with pak mods.</li></ul>

<h2>21. How the mod finds its way into the game</h2>
<p>There is no list of supported builds. At every start the mod looks through the running game's code for the places it needs, by the bytes around them, and checks that the game's objects are laid out as it expects. Each place has to be found exactly once.</p>
<ul><li><b>Essential:</b> the wheel and cylinder classes, the global that leads to the driven truck (looked for three ways), and six checks of where the game keeps the values the mod writes. Without them the mod stands down and changes nothing.</li>
<li><b>Optional:</b> the game's damage update and the truck update's call into Windows. Without them the mod runs and deals tire damage from its own thread.</li>
<li><code>DumpImage=1</code> in the ini writes the game's code as the mod sees it into <code>TirePressure_image_&lt;stamp&gt;.bin</code> next to the exe at the next start. That file lets the author see exactly why a build fails; take the line out again afterwards. A Game Pass install cannot be copied from its folder, but this can.</li>
<li>What the mod writes into the game's memory: the wheels' asphalt and mud grip and collision radius, the truck's ground grip list, fuel consumption, steering speed and gear speeds, the wheels' damage, one entry of the game's import table (for the game-thread damage) and the game's pointers to the pad function (two in the Steam build). All reads and writes go through checked calls, so an object the game frees gives an error, not a crash.</li></ul>

<h2>22. Every setting in the ini</h2>
<p><code>TirePressure.ini</code> next to the exe is written with the defaults and a short guide on the first start. The tab edits all of it except the four sound names and <code>DumpImage</code>. Each truck's mode goes into <code>TirePressure.trucks.ini</code> beside it (section 13).</p>
{table(['Key', 'Default', 'Range', 'What it does'], [
 ['Key', '114 (F3)', 'virtual key code', 'The panel key'], ['Beep', '0', '0 or 1', 'Beep as a change begins'], ['Seconds', '6', '0 to 60', 'Normal to Low takes this long'],
 ['SoundVolume', '50', '0 to 100', '0 = no sound'], ['UI', '1', '0 or 1', 'Show the panel'], ['UIScale', '1', '0.3 to 3', 'Panel size'],
 ['ConfirmSeconds', '5', '0 to 30', 'A choice confirms itself after this; 0 = never'],
 ['PadOpen, PadLower, PadRaise, PadConfirm, PadCancel', 'LB+DPadDown, DPadLeft, DPadRight, A, B', 'buttons joined with +, or None; PadLower and PadRaise without LB or RB', 'Pad bindings'],
 ['BaseGround, BaseAsphalt, BaseMud', '1', '0.1 to 5', 'Scale every mode\'s grip'], ['VanillaBalance', '0', '0 or 1', 'Vanilla balance on'],
 ['VanillaBalanceStrength', '1', '0 to 2', 'How far, past 1 below vanilla'], ['AsphaltFloor', '0', '0 to 10', 'Least grip on paved ground; 0 = off'], ['WornGrip', '10', '0 to 30', 'Percent of its grip a worn out tire keeps; 0 = none'],
 ['Increased', '1', '0 or 1', 'The road mode'], ['TireDamage', '1', '0 or 1', 'Soft tires wear at speed'], ['RollingRadius', '1', '0 or 1', 'Flattened tires roll like real ones'],
 ['StandStill', '1', '0 or 1', 'Change only while standing'], ['PerTruck', '1', '0 or 1', 'Each truck keeps its own pressure mode'], ['Temperature', '1', '0 or 1', 'Tires feel the outside temperature'], ['RollingDrag', '1', '0 or 1', 'Tires roll harder on soft ground and aired down'], ['StiffBase', '1', '0 or 1', 'Every tire at least as stiff as a Stiff one'], ['AmbientC', '15', '-40 to 50', 'Outside temperature without Gauges'],
 ['HardMode', '0', '0 or 1', 'Hard mode'], ['InsaneMode', '0', '0 or 1', 'Globalhacker insanity mode; wins over HardMode'],
 ['AirOutSound, AirInSound, CompressorSound, CompressorStopSound', 'empty', 'WAV file, pak sample or none', 'Replace a sound'],
 ['DumpImage', '0', '0 or 1', 'Write the game\'s code image at the next start, for a report']], 'Table 8. [TirePressure]')}
{table(['Key', 'Range', 'What it does'], [
 ['OnModelFriction', '0.1 to 10', 'Grip on dirt (plain ground)'], ['GravelFriction, SandFriction, RockFriction', '0.1 to 10', 'Grip on gravel, sand, rock'],
 ['BodyFrictionAsphalt', '0.1 to 10', 'Grip on paved ground'], ['SubstanceFriction', '0.1 to 10', 'Grip in mud'], ['AdditionalRadiusOffset', '0 to 0.25', 'Metres off the radius'],
 ['FuelConsumptionModifier', '0.1 to 10', 'Fuel while moving'], ['SteeringSpeedModifier', '0.1 to 10', 'Steering speed'],
 ['MinVel, MaxVel', '0 to 100', 'Wear speeds in metres per second (10 = 36 km/h)'], ['MinDamage, MaxDamage', '0 to 100', 'Damage per tick; MaxDamage 0 = no wear'],
 ['DamageTick', '1 to 100', 'Seconds between damages']], 'Table 9. [Low], [Reduced], [Increased] (Expeditions\' own key names)')}

<h2>23. Reading the log</h2>
{table(['The log says', 'Meaning'], [
 ['game code found at look N', 'Everything found; all is well.'],
 ['the game\'s truck update is not where the mod knows it, so tire damage will be dealt from the mod\'s own thread', 'Runs on this build; damage is dealt by hand.'],
 ['this game version is not one the mod can work on: standing down. Not as the mod needs it: ...', 'Nothing done. Send the log; DumpImage=1 helps further.'],
 ['this mod is in the folder twice', 'Keep only one .asi file of the mod.'],
 ['outside temperature: X C from another mod\'s block', 'The temperature comes from Gauges.'],
 ['outside temperature: AmbientC X C (no other mod publishes one)', 'The temperature is AmbientC.'],
 ['state published for other mods in Local\\SnowRunnerTirePressure.State...', 'The block for Gauges is there.'],
 ['wheel ... worn out: 10 percent of its grip until it is repaired', 'A tire wore out.'],
 ['key pressed, no driven truck found', 'The key was pressed outside a truck.'],
 ['panel: the overlay did not start: the key cycles the modes directly', 'The overlay found no swap chain; the overlay: lines say why.']], 'Table 10. Lines worth knowing')}

<!--TECH-->
<h2>Licence and credits</h2>
<p>GNU General Public License v3.0 (GPL-3.0-only). Source: github.com/andizla/snowrunner-tire-inflation-system.</p>
<p>The tire inflation system, its panel and its numbers are from Expeditions: A MudRunner Game by Saber Interactive. The panel and the menu are drawn with Dear ImGui by Omar Cornut, and their text is baked with stb_truetype by Sean Barrett; ReShade by Patrick Mours (crosire), which the release brings along without its start banner (BSD 3-Clause licence), hands the mod the game's swap chains through its add-on API. The asphalt floor follows an idea by Naybour. The insanity mode is built from the settings of the player Globalhacker. SnowRunner and Expeditions are games by Saber Interactive; this project is not affiliated with Saber Interactive or Focus Entertainment and contains no game files.</p>
<p>A big thank you to the testers who gave their time to the early builds: Milos_Mk6, Globalhacker and Zenosxy. Your runs, logs, ideas and patience shaped this release, and it would not be what it is without you.</p>
'''

def finish(body, tech):
    body = body.replace('<!--TECH-->', tech)
    n = [0]
    def h2(m):
        n[0] += 1
        return f'<h2 id="s{n[0]}">{n[0]}. '
    body = re.sub(r'<h2>(?:\d+\. )?', h2, body)
    for tag, word in [('caption', 'Table'), ('figcaption', 'Figure')]:
        k = [0]
        def cap(m, word=word, tag=tag):
            k[0] += 1
            return f'<{tag}>{word} {k[0]}. '
        body = re.sub(rf'<{tag}>(?:{word} \d+\. )?', cap, body)
    toc = []
    for m in re.finditer(r'<div class="(part|sect)[^"]*">([^<]+)</div>|<h2 id="s(\d+)">([^<]+)</h2>', body):
        toc.append(f'<li class="grp">{m.group(2)}</li>' if m.group(1) else f'<li>{m.group(4)}</li>')
    return body.replace('<!--TOC-->', '<ol class="toc">' + ''.join(toc) + '</ol>')

def print_pdf(name, html_text):
    os.makedirs(OUT, exist_ok=True)
    html = os.path.join(OUT, name + '.html')
    pdf = os.path.join(OUT, name + '.pdf')
    with open(html, 'w', encoding='utf-8') as f:
        f.write(html_text)
    subprocess.run([EDGE, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--allow-file-access-from-files', f'--print-to-pdf={pdf}',
                    'file:///' + html.replace('\\', '/')], capture_output=True, text=True, timeout=180)
    print(pdf, os.path.getsize(pdf) if os.path.exists(pdf) else 'NO PDF')

if __name__ == '__main__':
    import tech_tire
    print_pdf('TirePressure_manual_' + VERSION, page('Next Gen Tire Inflation manual', finish(TIRE, tech_tire.body())))
