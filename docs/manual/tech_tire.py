# Part 2 of the tire manual: the technical reference. Every number is computed from the 1.3.0 values
# (branch 1.3-balance) held in make_manuals.py, or quoted from the code with its file and line.
from math import acos, cos, sin, pi, ceil
from make_manuals import SETS, ENVELOPE, P, ATM, effect, air, table, x
from figs import chart, diagram

RANGE = dict(offset=(0.0, 0.25))
GRIPS = ['dirt', 'asphalt', 'mud', 'gravel', 'sand', 'rock']


def clamp(v, a, b):
    return max(a, min(b, v))


def eq(body, n=''):
    return f'<div class="eq">{body}' + (f'<span class="n">({n})</span>' if n else '') + '</div>'


def factors_at(name, p):
    """FactorsAt (tire_pressure.cpp:813-834) for one of the three sets."""
    m = SETS[name]
    i = 0
    while i < 2 and p > P[i + 1]:
        i += 1
    t = clamp((p - P[i]) / (P[i + 1] - P[i]), -0.5, 1.5)
    out = {}
    for k in GRIPS + ['fuel', 'steer', 'offset']:
        lo, hi = RANGE.get(k, (0.1, 10.0))
        out[k] = clamp(m[i][k] + (m[i + 1][k] - m[i][k]) * t, lo, hi)
    return out


def flatten_limit(R):
    """FlattenLimit (tire_pressure.cpp:569-584): the deepest hub drop, on a 1 mm grid, the wheel shader still draws flat."""
    best, h = 0.0, 0.001
    while h < R * 0.5:
        squash = min(1.5 * h, 0.25 * R) + 0.02
        zone = (min(0.2 * R, squash) + 0.1) * 0.8 / R * pi
        end = acos((R - h) / R)
        a, fits = 0.0, True
        while a <= end and fits:
            fits = R - (R - h) / cos(a) <= squash * (0.8 - 1.1 * min(1.0, a / zone)) + 0.005 * R
            a += 0.00436
        if not fits:
            break
        best = h
        h += 0.001
    return best


def rolling(R, c):
    p = acos(clamp(c / R, 0.3, 1.0))
    return R if p < 0.01 else R * sin(p) / p


def roll_factor(R, stock, written):
    return rolling(R, written) / rolling(R, stock) * stock / written


def top_gain(a, roll):
    """How much a gear's top wheel speed rises when its number a is scaled by roll (Gauges' 2a + 5)."""
    return (2 * a * roll + 5) / (2 * a + 5)


def damage(name, mode, kmh, c=15.0):
    m = SETS[name][mode]
    if not m['maxd']:
        return 0
    sp = effect(c)['speed']
    lo, hi = m['minv'] * sp, m['maxv'] * sp
    if kmh <= lo:
        return 0
    t = clamp((kmh - lo) / (hi - lo), 0, 1) if hi - lo > 1e-4 else (1 if kmh >= hi else 0)
    return int(m['mind'] * (1 - t) + m['maxd'] * t)


# ---------------------------------------------------------------- the scout of 2026-10-04, the test truck
SCOUT_R, SCOUT_OWN = 0.47, 0.0188
SCOUT_LIMIT = flatten_limit(SCOUT_R)
SCOUT_ROOM = SCOUT_LIMIT - SCOUT_OWN
SCOUT_STOCK = SCOUT_R - SCOUT_OWN


def scout_offset(off, deepest=0.10):
    share = SCOUT_ROOM / deepest if deepest > SCOUT_ROOM else 1.0
    return off * share


def s_intro():
    return '''
<div class="part break">Part 2. Technical reference</div>
<p class="intro">This part is for readers who want the workings. It gives each model as the code has it, the formulas with every
constant, where a number comes from when the code says so, how the mod gets into the game and what it reads and writes there, and
what it cannot do. The numbers are those of version 1.3.4. File and line references (<code>tire_pressure.cpp:813</code>) point
into the mod's source. "The Steam build" is the game build of 22 July 2026 (time stamp <code>6a607c05</code>), the one the mod
was made on; an RVA is an offset from the start of <code>SnowRunner.exe</code> in memory, in that build.</p>
<div class="box"><b>Notation.</b> <i>P</i> is the tire pressure as a share of Normal's, so Normal is 1. Modes are numbered
Low 0, Reduced 1, Normal 2, Increased 3. A factor such as ×1.3 multiplies the game's own value. clamp(<i>v</i>, <i>a</i>, <i>b</i>)
holds <i>v</i> between <i>a</i> and <i>b</i>. Temperatures are in °C, speeds in km/h unless m/s is written.</div>
'''


def s_overview():
    boxes = {
        'ldr': (20, 18, 150, 46, ['version.dll', 'the loader, next to the exe'], 'file'),
        'asi': (245, 18, 150, 46, ['TirePressure.asi', 'loaded by the loader'], 'mod'),
        'ini': (470, 18, 150, 46, ['TirePressure.ini and .log', 'settings, log'], 'file'),
        'run': (245, 108, 150, 54, ['Run thread', '20 passes a second', 'all the models'], 'mod'),
        'mem': (20, 108, 150, 54, ['Game memory', 'truck, wheels,', 'ground grip list'], 'game'),
        'rs': (470, 108, 150, 54, ['Next Gen overlay', 'panel, tab, warnings'], 'other'),
        'gt': (20, 206, 150, 50, ['Game thread', 'damage update'], 'game'),
        'gb': (245, 206, 150, 50, ['Gauges block', 'outside temperature'], 'mem'),
        'tb': (470, 206, 150, 50, ['Tire block', 'state for other mods'], 'mem'),
    }
    arrows = [('ldr', 'asi', 'loads'), ('ini', 'asi', 'reads, writes', {'both': True}), ('asi', 'run', 'starts', {'lx': 22, 'ly': 3}),
              ('run', 'mem', 'reads, writes', {'both': True}), ('rs', 'run', 'draws for', {'both': True}),
              ('run', 'gt', 'damage jobs', {'dash': True, 'lx': 10}), ('gb', 'run', 'reads', {'lx': 18, 'ly': 3}), ('run', 'tb', 'writes', {'lx': -6})]
    steps = ['The pressure takes one step toward the target, unless the truck must stand still and does not.',
             'The air tank and the sounds follow the step.',
             'The outside temperature is read: from the Gauges block when it is fresh, otherwise <code>AmbientC</code>.',
             'The temperature effects are worked out for the temperature rounded to half a degree.',
             'The pressure in the tires is corrected by the gas law, and the mode factors are read off the pressure scale at that pressure.',
             'The rubber factors scale the grip: one for hard surfaces, one for soft.',
             'Each wheel gets its vanilla balance factor, and asphalt its floor.',
             'Each wheel\'s sink is measured, and the wheel is written: its asphalt and mud grip and its collision cylinder. A worn out tire gets WornGrip of its grip (10 percent) and a flat cylinder.',
             'The speed is read, the rolling flag set, and the ground under each wheel picked.',
             'The truck is written: fuel use, steering speed, the ground grip list and the gear speeds.',
             'The damage timer runs, and damage is dealt when it is due.',
             'The panel\'s view and the shared state block are updated.']
    return f'''
<h2>How the mod runs</h2>
<p>The loader, <code>version.dll</code>, is loaded by the game at its start because Windows looks for that DLL next to the exe
first. It passes every call on to the real <code>version.dll</code> and loads <code>TirePressure.asi</code>. The ASI starts one
thread, <code>Run</code>, inside the game. Everything else happens on that thread, outside the game's frame: it finds the game's code
once, then loops with a 50 ms sleep, about 20 passes a second (<code>tire_pressure.cpp:1475</code>). The only code that runs on the
game's own thread is the damage update, through the hook described in its section.</p>
{diagram(boxes, arrows, h=270, cap='The parts and what flows between them. Solid arrows run every pass, the dashed one only when a tire takes damage.')}
<p>Each pass with a truck, in order (<code>tire_pressure.cpp:1692-1838</code>):</p>
<ol>{''.join(f'<li>{t}</li>' for t in steps)}</ol>
<p>Before the first truck is found the wheels are looked up every tenth pass, twice a second; from then on, every pass. The
pressure step and the air tank take a fixed 0.05 s per pass (<code>kPass</code>), not the measured time, so a pass delayed by the
system stretches a change a little. The damage timer uses the measured time, at most 0.5 s per pass.</p>
'''


def s_scale():
    rows = []
    for i, n in enumerate(['Low', 'Reduced', 'Normal', 'Increased']):
        rows.append([n, f'{P[i]:g}', f'{P[i] * 35:.2f} psi', f'{P[i] * 35 * 0.0689476:.2f} bar', f'{(P[i] + ATM) / (1 + ATM):.3f}'])
    t1 = table(['Mode', 'P', 'Gauge pressure', 'In bar', 'Absolute, share of Normal\'s'], rows, 'The pressure scale (panel.h:140)')
    xs = [0.30 + 0.005 * k for k in range(0, 241)]
    ser = [(lab, [(p, factors_at('default', p)[k]) for p in xs]) for lab, k in
           [('Sand', 'sand'), ('Dirt and rock', 'dirt'), ('Gravel', 'gravel'), ('Mud', 'mud'), ('Asphalt', 'asphalt')]]
    marks = [(P[i], factors_at('default', P[i])['sand'], n) for i, n in enumerate(['Low', 'Reduced', 'Normal', 'Increased'])]
    fig = chart(ser, (0.3, 1.5), (0, 4.5), 0.1, 0.5, 'Pressure P, share of Normal', 'Grip multiplier',
                'The grip multipliers along the pressure scale, default numbers. Straight between the modes, half a step on past either end, then flat.',
                marks=marks, vlines=[(0.35, 'stops'), (1.375, 'stops')], xfmt=lambda v: f'{v:.1f}', legend='out')
    hot = factors_at('default', air(1.0, 35))
    return f'''
<h2>The pressure scale</h2>
<p>The four modes sit on one scale of pressure. Normal is taken as 35 psi gauge; the other three are the mod's choice
(<code>panel.h:138-140</code>).</p>
{t1}
<p>Every factor the mod uses (grip on each surface, flattening, fuel, steering) is read off this scale at the current pressure
<i>P</i> by <code>FactorsAt</code> (<code>tire_pressure.cpp:813-834</code>). With <i>P</i><sub>0..3</sub> the four modes and
<i>f</i><sub>0..3</sub> a factor's value at each:</p>
{eq('<i>i</i> = the first segment with <i>P</i> ≤ <i>P</i><sub><i>i</i>+1</sub>, at most 2', 1)}
{eq('<i>t</i> = clamp( (<i>P</i> − <i>P<sub>i</sub></i>) / (<i>P</i><sub><i>i</i>+1</sub> − <i>P<sub>i</sub></i>), −0.5, 1.5 )', 2)}
{eq('<i>f</i>(<i>P</i>) = clamp( <i>f<sub>i</sub></i> + (<i>f</i><sub><i>i</i>+1</sub> − <i>f<sub>i</sub></i>) · <i>t</i>, range )', 3)}
<p class="where">The ranges are those of the game Expeditions for the same keys: 0.1 to 10 for grip, fuel and steering, 0 to 0.25 m
for the radius offset (<code>panel.h:81-83</code>). The clamp on <i>t</i> lets the last slope go on for half a step past Low and past
Increased and then stop: below <i>P</i> = 0.35 and above 1.375 nothing changes any more. Only the temperature can push the
pressure there.</p>
{fig}
<p>The segment above Normal always uses the stored Increased numbers, also when Increased is switched off (as in the insanity
mode). In hot weather Normal's pressure rises above 1 and slides a little toward them: at 35 °C, {(air(1.0, 35) - 1) / 0.25 * 100:.0f}% of
the way, which is asphalt ×{hot['asphalt']:.2f}, dirt ×{hot['dirt']:.2f} and fuel ×{hot['fuel']:.2f} with the default numbers,
before the rubber factors.</p>
<p>For the sounds and the air tank the pressure is also counted in modes, unclamped (<code>PressureInModes</code>):</p>
{eq('<i>m</i>(<i>P</i>) = <i>i</i> + (<i>P</i> − <i>P<sub>i</sub></i>) / (<i>P</i><sub><i>i</i>+1</sub> − <i>P<sub>i</sub></i>)', 4)}
'''


def s_speed():
    pairs = [(0, 1), (0, 2), (0, 3), (1, 2), (1, 3), (2, 3)]
    names = ['Low', 'Reduced', 'Normal', 'Increased']
    rows = []
    for a, b in pairs:
        d = abs(P[b] - P[a]) / 0.55
        rows.append([f'{names[a]} and {names[b]}', f'{d * 6:.1f} s', f'{d * 30:.1f} s', f'{d * 25:.1f} s' if b != 3 else 'no Increased'])
    t = table(['Between', 'Default (6 s)', 'Hard mode (30 s)', 'Insanity mode (25 s)'], rows, 'How long a change takes')
    return f'''
<h2>How fast the pressure moves</h2>
<p>The pressure moves at one pace whatever the step: <code>Seconds</code> is the time from Normal to Low, a span of 0.55 on the
scale (<code>PressureStep</code>, <code>tire_pressure.cpp:847-852</code>). Each pass moves it by</p>
{eq('Δ<i>P</i> = 0.55 · Δ<i>t</i> / <i>S</i>,   Δ<i>t</i> = 0.05 s', 5)}
<p class="where">and snaps it onto the target once it is within one step. <i>S</i> = 0 changes it at once. A change from <i>P</i><sub>a</sub>
to <i>P</i><sub>b</sub> therefore takes |<i>P</i><sub>b</sub> − <i>P</i><sub>a</sub>| / 0.55 · <i>S</i>:</p>
{t}
<p>The choice itself counts at once; only the pressure follows. The fixed Δ<i>t</i> makes the real time a little longer than the
table, by the time each pass takes beyond its 50 ms sleep.</p>
<h3>Standing still</h3>
<p>With <code>StandStill</code> on, the pressure only moves while the truck stands:</p>
{eq('standing = (speed unknown) or |<b>v</b> · <b>f</b>| &lt; 0.3 m/s', 6)}
<p class="where"><b>v</b> is the chassis body's velocity and <b>f</b> its forward axis (<code>tire_pressure.cpp:908-916, 1803</code>);
0.3 m/s is about 1 km/h. A paused game leaves its last velocity in the body, so a truck that moved less than 0.01 mm in each of
4 passes (0.2 s) counts as still, whatever the velocity says. A change that is under way pauses where it is when the truck moves
and goes on when it stops. The test uses the previous pass's speed, one pass (50 ms) late.</p>
<h3>The air tank</h3>
<p>The air for a filling comes from a tank, and only the tank's compressor makes noise (<code>tire_pressure.cpp:860-879</code>).
Raising the pressure by Δ<i>m</i> modes takes 0.17 · Δ<i>m</i> of the tank. Below 0.45 the compressor starts, and fills 0.04 of the
tank a second until it is full. From full, that is the second filling from Low to Normal or the fourth filling of one mode; the
compressor then runs for about 17 seconds.</p>
'''


def s_grip():
    keys = [('Dirt and plain ground', 'dirt', 'OnModelFriction'), ('Gravel', 'gravel', 'GravelFriction'), ('Sand', 'sand', 'SandFriction'),
            ('Rock', 'rock', 'RockFriction'), ('Asphalt (paved)', 'asphalt', 'BodyFrictionAsphalt'), ('Mud', 'mud', 'SubstanceFriction'),
            ('Radius offset', 'offset', 'AdditionalRadiusOffset'), ('Fuel use', 'fuel', 'FuelConsumptionModifier'),
            ('Steering speed', 'steer', 'SteeringSpeedModifier')]
    rows = []
    for lab, k, ini in keys:
        cells = [f'{lab}<br><span class="dim">{ini}</span>']
        for i in range(4):
            v = SETS['default'][i][k]
            cells.append(f'{v * 100:g} cm' if k == 'offset' and v else ('none' if k == 'offset' else x(v)))
        rows.append(cells)
    t = table(['Value (ini key)', 'Low', 'Reduced', 'Normal', 'Increased'], rows, 'The default numbers (panel.h:131-134)')
    return f'''
<h2>Grip: what the game has and what the mod writes</h2>
<p>SnowRunner keeps three grip values that matter here:</p>
<ul><li>each wheel's asphalt grip, <code>BodyFrictionAsphalt</code> in the tire files (wheel +0xA4), used on paved ground;</li>
<li>each wheel's mud grip, <code>SubstanceFriction</code> (wheel +0xA0), used where the wheel is in mud;</li>
<li>the truck's ground grip list, one value per wheel (truck action +0x98), filled from the tire's <code>BodyFriction</code>
once the truck moves, and used on every other ground.</li></ul>
<p>The game marks each frame which ground a wheel stands on, with a byte per kind where that layer is more than half of the mix
(wheel +0x1C8 paved, +0x1C9 gravel, +0x1CA sand, +0x1CD hard). The mod picks the factor for the ground list in that order of
priority: sand, then gravel, then rock (hard), otherwise dirt (<code>GroundFactor</code>, <code>tire_pressure.cpp:898-903</code>).
On paved ground the game uses the wheel's asphalt value through its contact function (Steam RVA 0xc402d0) instead.</p>
<p>With <i>g</i>(<i>P</i>) the factors read off the scale, <i>r</i><sub>soft</sub> and <i>r</i><sub>hard</sub> the rubber factors
of the outside temperature, <i>k</i> the wheel's vanilla balance factor and <i>F</i> the asphalt floor, each wheel gets
(<code>tire_pressure.cpp:1744-1782</code>):</p>
{eq('asphalt = <i>A</i><sub>0</sub> · <i>g</i><sub>asphalt</sub>(<i>P</i><sub>air</sub>) · <i>r</i><sub>hard</sub> · max(<i>k</i>, <i>F</i> / <i>A</i><sub>0</sub>)', 7)}
{eq('mud = <i>M</i><sub>0</sub> · <i>g</i><sub>mud</sub>(<i>P</i><sub>air</sub>) · <i>r</i><sub>soft</sub> · <i>k</i>', 8)}
{eq('ground = min( 10, <i>G</i><sub>0</sub> · <i>g</i><sub>ground</sub>(<i>P</i><sub>air</sub>) · <i>r</i> · <i>k</i> )', 9)}
<p class="where"><i>A</i><sub>0</sub>, <i>M</i><sub>0</sub> and <i>G</i><sub>0</sub> are the tire's own values. <i>g</i><sub>ground</sub>
is the dirt, gravel, sand or rock factor by the ground; <i>r</i> is <i>r</i><sub>hard</sub> for rock and <i>r</i><sub>soft</sub>
for the others. The max with <i>F</i>/<i>A</i><sub>0</sub> only applies with a floor set. A worn out tire gets 0 in all three.</p>
<p>The body's own friction (body +0xCC) is never written. Writing it once gave frictions in the thousands, flat tires and a
rolled truck (2026-10-02, <code>tire_pressure.cpp:661-663</code>).</p>
{t}
<p>Where the numbers come from (<code>panel.h:118-130</code>): Reduced and Low are the "default tire inflation" set of an
Expeditions tire file, all but asphalt. Expeditions has 1.3 and 2.0 there for its rock models, but in SnowRunner that value only
acts on paved ground, where an aired down tire loses grip to its softer sidewall, so the mod has 0.95 and 0.85. Increased is a road
mode: Expeditions' newer tire files trade grip for fuel and steering, and the mod adds better asphalt grip. The split into gravel,
sand and rock is the mod's own: sand gains the most from a long contact patch, rock as much as dirt, gravel the least.</p>
<p>Base grip (<code>BaseGround</code>, <code>BaseAsphalt</code>, <code>BaseMud</code>) multiplies all four modes, Normal too,
before the scale's clamps (<code>tire_pressure.cpp:366-374</code>).</p>
'''


def s_flat():
    Rs = [0.30 + 0.01 * k for k in range(0, 81)]
    lim = [(R, flatten_limit(R) * 100) for R in Rs]
    fig = chart([('Deepest drop the game draws flat', lim)], (0.3, 1.1), (0, 14), 0.1, 2, 'Tire radius R, m', 'Hub drop h, cm',
                'FlattenLimit: how far the hub can sit below the tire\'s radius before the drawn tread sinks into hard ground.',
                hlines=[(10, 'Low asks for 10 cm'), (6, 'Reduced asks for 6 cm')], legend='br', xfmt=lambda v: f'{v:.1f}')
    lo, re_ = scout_offset(0.10), scout_offset(0.06)
    hand = ', '.join(f'{R:g} m: {flatten_limit(R) * 100:.1f} cm' for R in [0.35, 0.47, 0.65, 0.8, 1.0])
    return f'''
<h2>Flattening</h2>
<p>An aired down tire in Expeditions is a smaller collision cylinder: the game takes the mode's <code>AdditionalRadiusOffset</code>
off the radius, in metres (its RVA 0xcd40c0). SnowRunner has the same wheel, but its wheel shader only draws a tire so flat.
Past that the tread is drawn under the ground: a scout on 0.47 m tires at 10 cm sank 37 mm (2026-10-04).</p>
<p>The game squashes the drawn tire by an amount it takes from the hub's height over the contact (the wheel model's update,
Steam RVA 0xc235f0), and the shader (<code>SpinTires/TruckWheel.fxs</code>, <code>g_softParams.w</code>) pulls the tread in by
less the further an angle <i>a</i> is from the contact. For a hub <i>h</i> below the radius <i>R</i>:</p>
{eq('<i>q</i> = min(1.5 <i>h</i>, 0.25 <i>R</i>) + 0.02,    <i>z</i> = (min(0.2 <i>R</i>, <i>q</i>) + 0.1) · 0.8π / <i>R</i>', 10)}
{eq('pull(<i>a</i>) = <i>q</i> · (0.8 − 1.1 · min(1, <i>a</i> / <i>z</i>))', 11)}
<p class="where">On flat hard ground the tire's circle reaches under the ground by <i>R</i> − (<i>R</i> − <i>h</i>) / cos <i>a</i> at the angle
<i>a</i>, out to where it leaves the ground, <i>a</i> = acos((<i>R</i> − <i>h</i>)/<i>R</i>). The drop is drawn flat as long as</p>
{eq('<i>R</i> − (<i>R</i> − <i>h</i>) / cos <i>a</i>  ≤  pull(<i>a</i>) + 0.005 <i>R</i>   for every such <i>a</i>', 12)}
<p class="where">Half a percent of <i>R</i> under the ground is let through: nobody sees it, and on very large wheels the shader's shape
misses a flat contact by that much early on. <code>FlattenLimit</code> (<code>tire_pressure.cpp:569-584</code>) steps <i>h</i> by
1 mm and <i>a</i> by a quarter of a degree and keeps the last <i>h</i> that fits: {hand}.</p>
{fig}
<p>A tire file can already give the wheel an offset of its own (wheel +0xB4), which uses up part of that room. Every mode's
offset then shrinks by the same share, so the deepest mode in use takes all the room there is
(<code>Cylinder</code>, <code>tire_pressure.cpp:590-594</code>):</p>
{eq('room = max(0, FlattenLimit(<i>R</i>) − <i>o</i><sub>own</sub> − sink),    σ = min(1, room / <i>o</i><sub>deepest</sub>)', 13)}
{eq('cylinder = max( 0.4 <i>R</i>, <i>R</i> − <i>o</i><sub>own</sub> − min(<i>o</i>(<i>P</i>) · σ, room) )', 14)}
<p class="where">Example, the test scout (<i>R</i> = 0.47 m, own offset 1.88 cm): FlattenLimit {SCOUT_LIMIT * 100:.1f} cm, room
{SCOUT_ROOM * 100:.2f} cm. Low gets {lo * 100:.1f} cm and Reduced {re_ * 100:.1f} cm. The cylinder never goes below 40% of the
radius, whatever the ini says. No offset goes past the room: in the cold Low's offset is carried past the deepest mode's (equation 3, up to 12 cm), and before 1.3.0 that went up to 1.2 times the room.</p>
<h3>Tires that come soft</h3>
<p>The game draws the squash from the distance <i>d</i> between the hub and the contact, not from the cylinder. Its wheel model update reads the contact at wheel model +0x21C and +0x220 and works out (Steam build, RVA 0xc239cc):</p>
{eq('<i>d</i> = √(<i>x</i>² + <i>y</i>²),    squash = min( (<i>R</i> − <i>d</i>) · 1.5, 0.25 <i>R</i> ) + 0.02,  × wheel +0x128', 15)}
<p class="where">On hard ground a firm tire touches at its cylinder, <i>d</i> = <i>c</i>, and the hub drop is <i>R</i> − <i>c</i>, the tire's own offset plus the mode's: what FlattenLimit counts. But each wheel carries a Havok soft contact built with its <code>SoftForceScale</code> (wheel +0x120), and a soft tire's contact sits inside the cylinder: the hub is lower again by sink = <i>c</i> − <i>d</i>, and the tread is drawn that much deeper. A modded tire that came soft went far too flat at Low (a tester, 2026-10-18). Since 1.3.0 the mod measures it under every wheel on the ground (wheel +0x1A4), each pass:</p>
{eq('sink ← sink + 0.025 · ( max(0, <i>c</i><sub>written</sub> − <i>d</i>) − sink ),    taken only for 0.85 <i>c</i> ≤ <i>d</i> ≤ 1.02 <i>c</i>', 16)}
<p class="where">0.025 a pass settles in about two seconds at 20 passes a second. A reading outside the window (a wheel in the air, a field a game update moved) is not taken. The sink is used in 5 mm steps, moved to the nearest step once it is three quarters of a step away, so the cylinder is not written again for every bump. It comes off the room first, equation (13), so a soft tire is flattened less and never drawn into the ground. A vanilla tire sinks next to nothing: the scout's test at 10 cm sank its tread 37 mm under the ground, which is exactly what equations (10) to (12) give with no sink. The log's wheel lines and the tab's truck section show each wheel's sink.</p>
'''


def s_roll():
    hs = [k * 0.001 for k in range(0, 101)]
    real = [(h * 100, rolling(SCOUT_R, SCOUT_STOCK - h) / rolling(SCOUT_R, SCOUT_STOCK) * 100) for h in hs]
    game = [(h * 100, (SCOUT_STOCK - h) / SCOUT_STOCK * 100) for h in hs]
    lo, re_ = scout_offset(0.10), scout_offset(0.06)
    rl, rr = roll_factor(SCOUT_R, SCOUT_STOCK, SCOUT_STOCK - lo), roll_factor(SCOUT_R, SCOUT_STOCK, SCOUT_STOCK - re_)
    keep_real = rolling(SCOUT_R, SCOUT_STOCK - lo) / rolling(SCOUT_R, SCOUT_STOCK)
    keep_re = rolling(SCOUT_R, SCOUT_STOCK - re_) / rolling(SCOUT_R, SCOUT_STOCK)
    keep_game = (SCOUT_STOCK - lo) / SCOUT_STOCK
    fig = chart([('A real tire, and the mod with RollingRadius=1', real), ('The game\'s cylinder alone', game)], (0, 10), (84, 100), 1, 2,
                'Hub drop below stock, cm (scout, R = 0.47 m)', 'Speed kept, % of stock', 'Speed per wheel turn as the tire flattens.',
                marks=[(lo * 100, keep_real * 100, 'Low'), (re_ * 100, keep_re * 100, 'Reduced')], legend='bl')
    return f'''
<h2>How a flattened tire rolls</h2>
<p>A real tire's belt keeps its length. Flattened by <i>h</i>, its contact patch reaches <i>R</i> sin <i>p</i> to either side of the
hub while the belt runs <i>R p</i> over the same arc, with</p>
{eq('cos <i>p</i> = (<i>R</i> − <i>h</i>) / <i>R</i>', 17)}
<p class="where">so the tire rolls as a wheel of radius</p>
{eq('<i>r</i><sub>roll</sub> = <i>R</i> sin <i>p</i> / <i>p</i>', 18)}
<p class="where">which stays close to <i>R</i> for small drops. The game's wheel is its collision cylinder, <i>R</i> − <i>h</i>, and loses
about three times as much speed per turn. With <code>RollingRadius=1</code> the mod raises the truck's gear speeds by the
difference (<code>RollFactor</code>, <code>tire_pressure.cpp:601-609</code>):</p>
{eq('roll = <i>r</i><sub>roll</sub>(cylinder) / <i>r</i><sub>roll</sub>(stock) · stock / cylinder', 19)}
<p class="where"><i>p</i> is held to cos <i>p</i> ≥ 0.3, and below <i>p</i> = 0.01 the radius is taken as <i>R</i>. The truck's
gear speed list (truck action +0x58, at most 32 values) is raised so each gear's top grows by the mean roll of its wheels, worn out tires left out. The game governs a gear to 2<i>a</i> + 5 rad/s for its number <i>a</i>, <i>a</i> + 2 in the high gear (the Gauges mod's reading of the game, its <code>models.h:127-135</code>), so each number gets:</p>
{eq('<i>a</i>′ = roll · <i>a</i> + 2.5 (roll − 1),    high gear: <i>a</i>′ = roll · <i>a</i> + 2 (roll − 1)', 20)}
<p class="where">Then 2<i>a</i>′ + 5 = roll · (2<i>a</i> + 5) exactly. Before 1.3.0 the number alone was scaled, which fell short most in the low gears: with <i>a</i> = 5, ×{top_gain(5, rl):.3f} for ×{rl:.3f}. For the scout: Low ×{rl:.3f}, Reduced ×{rr:.3f}; at Low the truck keeps {keep_real * 100:.1f}% of its speed against
{keep_game * 100:.1f}% for the smaller wheel alone.</p>
{fig}
<div class="warn"><b>The pull.</b> If the game drove a wheel with a fixed torque at its axle, the push at the ground would be that
torque over the cylinder's radius, and a smaller cylinder a shorter lever: for the scout at Low ×{SCOUT_STOCK / (SCOUT_STOCK - lo):.3f},
where a real tire pushes ×{1 / keep_real:.3f}. The Gauges mod's tests found that the game pulls less in a gear with a higher top
speed (Gauges <code>models.h:1081-1086</code>), as a wheel torque that goes with 1 over the gear's top would. Then raising each gear's
top by roll also takes the lever's extra back, and Low comes to ×{SCOUT_STOCK / (SCOUT_STOCK - lo) / rl:.3f}, a real tire's. With <code>RollingRadius=0</code>, as in the
insanity mode, the lever's ×{SCOUT_STOCK / (SCOUT_STOCK - lo):.2f} stands. None of this is measured; the Gauges mod's pull test, run
at Normal and at Low on the same road, would settle it.</div>
'''


def s_fuel():
    return '''
<h2>Fuel and steering</h2>
<p>The game's fuel rate (Steam RVA 0xd670b0) multiplies by the sum of two values the truck carries: the engine's and the
gearbox's <code>FuelConsumption</code> (vehicle +0xE38 and +0xE3C). The mod writes each as its stock value × the mode's fuel
factor, but only while the truck rolls; standing, the factor is 1. Rolling starts above 0.5 m/s and lasts while above
0.25 m/s, as Expeditions applies its modifier only above a speed (its fuel rate, RVA 0xe168a0).</p>
<p>The steering speed is the truck's <code>SteerSpeed</code> (vehicle +0x69C) × the mode's factor; the game reads it every frame.</p>
<p>Every value the mod writes follows one rule (<code>ApplyValue</code>, <code>tire_pressure.cpp:745-757</code>): a value that differs
from what the mod wrote last is taken as the new stock, if it lies in a range a tire or truck file can hold (fuel 0 to 1000,
steering 0.0001 to 10, ground grip 0.05 to 10, gear speeds −1000 to 1000); the mod then writes min(cap, stock × factor). So a
truck changed in the garage, or a value another mod writes, becomes the new stock instead of being overwritten.</p>
'''


def s_drag():
    return f'''
<h2>Rolling drag</h2>
<p>With <code>RollingDrag=1</code> (or hard or insanity mode) the mod writes each grounded, unbroken wheel's Havok angular damping
(body +0x216, a half float: a float's upper 16 bits, rounded up by 1.003906 first). Havok slows a body's spin by <i>c</i>·ω, a
torque of <i>I</i>·<i>c</i>·ω, and the tire's grip passes it to the truck. A rolling resistance is the force Crr·<i>N</i>, the
torque Crr·<i>N</i>·<i>r</i>. So (<code>rolling_drag.h</code>):</p>
{eq('<i>c</i> = <i>c</i><sub>stock</sub> + Crr · <i>f</i>(<i>p</i>) · <i>N</i> · <i>r</i> / (<i>I</i> · max(|ω|, 2 m/s / <i>r</i>)),    <i>f</i>(<i>p</i>) = (<i>p</i> / <i>p</i><sub>Normal</sub>)<sup>−0.5</sup>,    <i>N</i> = <i>M</i> <i>g</i> / <i>n</i>')}
<p class="where"><i>c</i><sub>stock</sub> is wheel +0xAC, the game's own figure, never the body's word read back. <i>r</i> is
wheel +0x94. <i>I</i> is the wheel body's inertia about its axle: 1 over body +0x220's x, y or z, the one that stands apart
(<code>RollAxle</code>). ω is the body's angular velocity (+0x240) along that axle's column of the rotation (+0x170). <i>M</i> is
the mass the physics moves: model +0x784, the game's own sum of the truck's bodies but the wheels (the chassis, a cab on a hinge,
an antenna, the steering pivot; the chassis's body alone misses the cab), and every wheel body's 1/w (body +0x22C). In <i>N</i>
each body comes with its Havok gravity factor: <i>N</i> = (model +0x784 × the chassis's factor + Σ each wheel's 1/w × its own) ×
<i>g</i> / <i>n</i>. Havok keeps the factor as a half in the body's motion after the velocities, at body +0x282. The game's code
confirms the place: the truck file's GravityFactor goes into the body's construction info, and Havok's body constructor stores
it there beside the damping, the same in both builds read. The truck's line gives the chassis's factor. <i>n</i> is the wheels with
contact (+0x1A4). <i>p</i> is the mod's pressure share as it moves. <i>c</i> is held at 20 at most (the probe measured up to 8; at 60 frames a second <i>c</i>·dt reaches 1 at <i>c</i> = 60); the line a second counts the frames where it bites.</p>
<p>ω is the body's own spin, not <i>v</i> / <i>r</i>, so a wheel spinning in sand gets the same torque as one rolling. Above the
floor the torque is Crr·<i>N</i>·<i>r</i>. Below it the torque falls with the spin, to nothing standing: the damping never
pushes a wheel. Over all the wheels the drag slows the truck by Crr · <i>f</i> · <i>g</i>, whatever its mass.</p>
{table(['Ground', 'Flag', 'Crr', 'At Normal, m/s²', 'At Low, m/s²'], [['Paved', '<code>+0x1C8</code>', '0.010', '0.10', '0.15'], ['Rock', '<code>+0x1CD</code>', '0.015', '0.15', '0.22'], ['Gravel', '<code>+0x1C9</code>', '0.020', '0.20', '0.29'], ['Dirt, mud and no flag', 'none', '0.030', '0.29', '0.44'], ['Sand', '<code>+0x1CA</code>', '0.060', '0.59', '0.88'], ['Ice', '<code>+0x1C0</code>', '0', '0', '0']], 'Crr by the ground flags the game\'s check leaves in the wheel')}
<p>Where several flags are set: ice, then paved, sand, gravel, rock, and dirt for the rest. No flag counts as dirt. Mud sets
none, so mud gets dirt's 0.030 on top of the game's own mud drag; +0x1CE is not mud (it is on over paved road). Ice is 0.</p>
<p><b>When it writes.</b> <code>RollingDragFrame</code> runs in the truck update, on the game's thread, every frame, on reads of
that frame. A wheel's <i>c</i> goes in when the mod has no word of its own in it, when the body holds another word than the mod's
last (the game wrote it: compared as a word, never taken as a stock), or when <i>c</i> moved by over 2 % since. The mod keeps its
last word itself. A broken wheel (model +0x328) keeps the game's own. A wheel off the ground, the truck left for another and the
setting off get the stock back, where the body still holds the mod's word. While the probe (<code>ProbeRoll</code>) holds a
damping of its own, the drag waits. Without the truck update hook it stays off, with a log line.</p>
<p><b>The log.</b> A line per truck with the chassis body's 1/w, the files' sum of the bodies (model +0x784), the wheels, the gravity factor, <i>N</i> and the mass, the first wheel's <i>I</i>, <i>r</i> and stock damping, the
Crr and the pressure factor. With <code>ProbeRoll=1</code> also one a second while the truck moves: each wheel's ground,
Crr × <i>f</i>, the <i>c</i> in its body, its spin, and the torque <i>I</i>·(<i>c</i> − <i>c</i><sub>stock</sub>)·ω against
Crr·<i>f</i>·<i>N</i>·<i>r</i>.</p>
<p>The probe's coast-downs showed that body +0x216 reaches the truck as <i>I</i>·<i>c</i>·ω, for torques well above any rolling
drag writes. Fuel: the fuel factors of hard mode and the insanity mode are 1, as the engine burns more against the drag by itself. The default modes keep
theirs until a drive says otherwise.</p>
'''


def s_damage():
    sets = [('Default Low', 'default', 0), ('Default Reduced', 'default', 1), ('Hard Low', 'hard', 0), ('Hard Reduced', 'hard', 1),
            ('Insanity Low', 'insane', 0), ('Insanity Reduced', 'insane', 1)]
    cols = ['#c6532a', '#c6532a', '#3a7fb0', '#3a7fb0', '#8a5aa8', '#8a5aa8']
    series = []
    for j, ((lab, name, mode), col) in enumerate(zip(sets, cols)):
        tick = SETS[name][mode]['tick']
        pts = [(k * 0.1, damage(name, mode, k * 0.1) / tick * 60 / 50 * 100) for k in range(0, 901)]
        series.append((lab, pts, {'color': col, 'dash': '5 3' if j % 2 else ''}))
    fig = chart(series, (0, 90), (0, 1000), 10, 100, 'Speed, km/h (outside 15 °C)', 'Wear, % of a tire a minute',
                'How fast a tire wears by speed, for a SnowRunner wheel of 50 damage points. 100% a minute wears a new tire out in a minute.',
                legend='out', h=270)
    rows = []
    for lab, name, mode in sets:
        m = SETS[name][mode]
        rows.append([lab, f'{m["minv"]:g} to {m["maxv"]:g} km/h', f'{m["mind"]:g} to {m["maxd"]:g}', f'{m["tick"]:g} s',
                     f'{ceil(50 / m["mind"]) * m["tick"]:g} s', f'{ceil(50 / m["maxd"]) * m["tick"]:g} s'])
    t = table(['Set and mode', 'Speeds', 'Damage per tick', 'Tick', 'New to worn out, just too fast', 'At full speed'], rows,
              'Tire damage, and how long a new tire lasts (50 damage points, 15 °C)')
    return f'''
<h2>Tire damage</h2>
<p>A soft tire run too fast flexes and heats. Each mode below Normal has a speed above which its tires take damage, in ticks.
With <i>v</i> the forward speed and <i>r</i><sub>speed</sub> the temperature's factor (see the outside temperature):</p>
{eq('too fast = <i>v</i> &gt; <i>v</i><sub>min</sub> · <i>r</i><sub>speed</sub>', 21)}
{eq('<i>s</i> = clamp( (<i>v</i> − <i>v</i><sub>min</sub>) / (<i>v</i><sub>max</sub> − <i>v</i><sub>min</sub>), 0, 1 ),    damage per tick = ⌊ <i>D</i><sub>min</sub> (1 − <i>s</i>) + <i>D</i><sub>max</sub> <i>s</i> ⌋', 22)}
<p class="where">with <i>v</i><sub>min</sub> and <i>v</i><sub>max</sub> both × <i>r</i><sub>speed</sub> (Expeditions' formula, its RVA 0xccfdf0).
While too fast a timer runs on the measured time; when it passes the tick length, every wheel on the ground takes the damage and
the timer starts again. Below the speed for a full second the timer resets: Expeditions checks every frame, the mod 20 times a
second, and a single missed reading must not cost a whole tick (<code>tire_pressure.cpp:1839-1883</code>).</p>
{fig}
{t}
<p>Where the numbers come from (<code>panel.h:126-129</code>): Expeditions' off-road tires take 2 to 4 at Low from 27 to 36 km/h and
1 to 3 at Reduced from 36 to 54 km/h (<code>_templates\\trucks.xml</code>). The mod doubles the damage and lowers the speeds, set by
driving on 2026-10-04, as a SnowRunner truck is slower off the road. Version 1.0.0 used 15 to 25 and 25 to 35 km/h, which more
driving found too low. The tick lengths have no stated source.</p>
<h3>How damage reaches the game</h3>
<p>The truck's damage lives in blocks behind a handle at vehicle +0x218 (the game's accessor, Steam RVA 0xa749a0): the block's
address is the handle with its second byte cleared (mask <code>0xFF00FFFFFFFFFFFF</code>); its 16-bit flags at +0x10 must have
bit 0 set and bit 2 clear; its data starts at +0x38. The wheels' list holds a pointer at +8 and a count at +0x10, and each wheel's
block is three values: damage (int), capacity (int) and effect (float). The mod only touches a block whose capacity equals the one
the wheel model was built with (wheel model +0x324) and whose damage is between 0 and that capacity, and adds to each wheel on the
ground (wheel +0x1A4):</p>
{eq('damage′ = min( capacity, damage + added )', 23)}
<p class="where">Then, on the game's own thread, it runs the game's damage update for the truck (Steam RVA 0xd6f690). That update is
what flattens a worn out tire, mends a repaired one, sets each part's effect, sums the truck's damage (vehicle +0x108 and +0x10C)
and tells the truck's script, which brings up the truck card and turns the wheel icon red.</p>
<p>Without the game-thread path the mod breaks a worn out wheel by hand: it writes the wheel's broken radius (wheel +0x9C) into
its radius (+0x94) and its shape's radius (shape +0x28), the broken damping into the body as Havok's 16-bit float (the upper half
of the 32-bit float of <i>v</i> × 1.003906, the game's rounding), and the broken flag (wheel model +0x328). The game's own break
builds a new Havok shape, which needs its own thread; the by-hand break cannot, and the game's display only follows at the next
damage the game deals itself.</p>
<h3>A worn out tire</h3>
<p>When the wheel model's broken flag (+0x328) is set, the mod writes 0 into the wheel's asphalt and mud grip and its ground grip
entry, and draws it flat (<code>ApplyWornWheel</code>, <code>tire_pressure.cpp:618-655</code>):</p>
{eq('cylinder = max( 0.4 <i>b</i>, <i>b</i> − FlattenLimit(<i>b</i>) ),    <i>b</i> = the broken radius', 24)}
<p class="where">A worn wheel is left out of the gear speeds' mean. A repair makes the game rebuild the shape at stock size with the
zero grip still on it; the mod sees a cylinder back near stock, forgets the worn state and writes the mode's values again.</p>
'''


def s_temp():
    Ts = [-40 + 0.5 * k for k in range(0, 181)]
    fig1 = chart([('Normal', [(c, air(1.0, c) * 100) for c in Ts]), ('Reduced, of its own', [(c, air(0.65, c) / 0.65 * 100) for c in Ts]),
                  ('Low, of its own', [(c, air(0.45, c) / 0.45 * 100) for c in Ts])],
                 (-40, 50), (50, 130), 10, 10, 'Outside temperature, °C', 'Pressure held, %',
                 'The gas law: the pressure each mode holds, as a share of what it holds at 15 °C. A low pressure loses more of itself in the cold.',
                 vlines=[(15, '15 °C, set')], legend='tl')
    fig2 = chart([('Hard surfaces (asphalt, rock)', [(c, effect(c)['hard']) for c in Ts]),
                  ('Soft surfaces (dirt, gravel, sand, mud)', [(c, effect(c)['soft']) for c in Ts]),
                  ('Damage speeds', [(c, effect(c)['speed']) for c in Ts], {'dash': '5 3'})],
                 (-40, 50), (0.6, 1.3), 10, 0.1, 'Outside temperature, °C', 'Factor', 'The rubber factors and the damage speed factor.',
                 vlines=[(15, '')], legend='tr', yfmt=lambda v: f'{v:.1f}')
    rows = []
    for c in [-30, -20, -10, 0, 15, 30, 45]:
        pa = air(0.45, c)
        f, e = factors_at('default', pa), effect(c)
        rows.append([f'{c} °C', f'{pa:.3f}', x(round(f['dirt'] * e['soft'], 2)), x(round(f['sand'] * e['soft'], 2)),
                     x(round(f['asphalt'] * e['hard'], 2)), x(round(f['mud'] * e['soft'], 2)), f'{f["offset"] * 100:.1f} cm',
                     f'{SETS["default"][0]["minv"] * e["speed"]:.0f} km/h'])
    t = table(['Outside', 'P at Low', 'Dirt', 'Sand', 'Asphalt', 'Mud', 'Offset asked', 'Wear above'], rows,
              'Low pressure, default numbers, at different outside temperatures: everything together')
    return f'''
<h2>The outside temperature</h2>
<p>Tires are taken as set at 15 °C. The air in a tire of fixed volume follows the gas law on absolute pressure, gauge pressure
plus the atmosphere's 14.7 psi. With Normal at 35 psi gauge the atmosphere is 14.7 / 35 = 0.42 of Normal's pressure, and a
tire set to <i>P</i> at 15 °C holds, at <i>T</i> (<code>outside.h:109-124</code>):</p>
{eq('<i>P</i><sub>air</sub> = (<i>P</i> + 0.42) · (<i>T</i> + 273.15) / 288.15 − 0.42', 25)}
<p class="where">At −20 °C Normal holds 0.83 and Low 0.34 (76% of its own); at 35 °C Normal holds 1.10. A low pressure loses a
larger share of itself, because the atmosphere's part does not shrink. The mode factors are read off the scale at
<i>P</i><sub>air</sub>, so cold air moves every mode a little lower on the scale.</p>
{fig1}
<p>Rubber also changes with the temperature: cold rubber hardens and grips less, most on hard surfaces. With
below = max(0, 15 − <i>T</i>) and above = max(0, <i>T</i> − 15) (<code>outside.h:112-121</code>):</p>
{eq('<i>r</i><sub>hard</sub> = clamp( 1 − 0.006 · below + 0.003 · above, 0.7, 1.08 )', 26)}
{eq('<i>r</i><sub>soft</sub> = max( 0.85, 1 − 0.003 · below )', 27)}
{eq('<i>r</i><sub>speed</sub> = clamp( 1 + 0.006 · (15 − <i>T</i>), 0.75, 1.25 )', 28)}
<p class="where"><i>r</i><sub>hard</sub> scales asphalt and rock, <i>r</i><sub>soft</sub> dirt, gravel, sand and mud. The damage speeds are
multiplied by <i>r</i><sub>speed</sub>: a soft tire's sidewalls heat as they flex, so in cold air the damage starts at a higher speed and
in hot air at a lower one. The clamps bite at −35 °C (hard and soft), −26.7 °C (speed) and 41.7 °C (hard). The code gives the
reasoning (cold rubber "a fifth less" on hard ground at 20 below) but no source for the slopes.</p>
{fig2}
{t}
<p>The temperature is rounded to half a degree before any of this, so a drifting reading does not rewrite the wheels every pass.
With <code>Temperature=0</code> every factor is 1.</p>
<h3>Where the temperature comes from</h3>
<p>From the Gauges mod's shared block (see Shared memory) when it is open and fresh: magic "GAUG", version 1 or later, size 64 or
more, written within the last 2 seconds, and a temperature between −90 and 90 °C. Otherwise from <code>AmbientC</code> in the ini
(−40 to 50, 15 by default). The block is looked for every 5 seconds until found, then kept open.</p>
'''


def s_presets():
    fields = [('Dirt', 'dirt'), ('Gravel', 'gravel'), ('Sand', 'sand'), ('Rock', 'rock'), ('Asphalt', 'asphalt'), ('Mud', 'mud'),
              ('Radius offset', 'offset'), ('Fuel', 'fuel'), ('Steering', 'steer'), ('Wear from', 'minv'), ('Most wear from', 'maxv'),
              ('Damage per tick', 'mind'), ('Tick', 'tick')]

    def preset_table(name, cap, inc=True):
        idx = [0, 1, 3] if inc else [0, 1]
        rows = []
        for lab, k in fields:
            cells = [lab]
            for i in idx:
                m = SETS[name][i]
                if k == 'offset':
                    cells.append(f'{m[k] * 100:g} cm' if m[k] else 'none')
                elif k in ('minv', 'maxv'):
                    cells.append(f'{m[k]:g} km/h' if m['maxd'] else 'no wear')
                elif k == 'mind':
                    cells.append(f'{m["mind"]:g} to {m["maxd"]:g}' if m['maxd'] else '')
                elif k == 'tick':
                    cells.append(f'{m[k]:g} s' if m['maxd'] else '')
                else:
                    cells.append(x(m[k]))
            rows.append(cells)
        return table(['', 'Low', 'Reduced'] + (['Increased'] if inc else []), rows, cap)

    forced = [['Seconds (Normal to Low)', '30', '25'], ['Base grip, all three', '1', '1'], ['Vanilla balance', 'on, strength 1', 'on, strength 2'],
              ['Asphalt floor', 'off', 'off'], ['Increased', 'on', 'off'], ['Tire damage', 'on', 'on'], ['Rolling radius', 'on', 'off'],
              ['Change only standing', 'on', 'on'], ['Temperature', 'on', 'on'], ['Rolling drag', 'on', 'on'], ['Left alone', 'key, beep, volume, panel, pads, AmbientC', 'the same']]
    return f'''
<h2>Hard mode and the insanity mode: the full numbers</h2>
<p>Both replace the player's numbers and options while ticked; the ini keeps the player's own, and unticking brings them back
(<code>PresetSettings</code>, <code>panel.h:197-201</code>). The insanity mode wins if both are set in the ini; the tab's two boxes
exclude each other. The shared state flag "hard mode" is set for either.</p>
{table(['Setting', 'Hard mode', 'Insanity mode'], forced, 'What each one forces (panel.h:156-194)')}
{preset_table('hard', 'Hard mode (kHardModes, panel.h:148-151)')}
<p>Hard mode's reasoning (<code>panel.h:142-147</code>): halving a tire's pressure lengthens its contact patch by about half, which
is that much more grip on soft ground and a little less on hard; rolling resistance rises as the pressure falls, and the mod's rolling drag charges it, so the fuel factors are 1; a
soft tire's sidewalls flex and heat at speed; a change takes half a minute, as a truck's onboard compressor does. The numbers are
the mod's; the reasoning is the physics.</p>
{preset_table('insane', 'Globalhacker insanity mode (kInsaneModes, panel.h:175-178)', inc=False)}
<p>The insanity mode is built from the player Globalhacker's own hardcore set (2026-10). With the balance at strength 2, a modded
tire twice as strong as the best vanilla one ends at half of vanilla (see the next section); Low then takes 0.4 of that on
asphalt. At 20 km/h Low wears a new tire out in 8 seconds.</p>
'''


def s_balance():
    env = table(['', 'Ground', 'Asphalt', 'Mud'], [[str(i + 1), f'{g:g}', f'{a:g}', f'{m:g}'] for i, (g, a, m) in enumerate(ENVELOPE)],
                'The 16 vanilla tires no other vanilla tire matches or beats in all three (vanilla_tires.h)', 'small')
    ss = [k * 0.02 for k in range(0, 101)]
    ser = [(f'f = {f:g}', [(s, (1 - s * (1 - f)) if s <= 1 else f * (1 - (s - 1) * (1 - f))) for s in ss]) for f in [0.2, 0.4, 0.6, 0.8, 1.0]]
    fig = chart(ser, (0, 2), (0, 1.05), 0.25, 0.1, 'Strength s', 'Factor k on the tire\'s grip',
                'The balance factor by strength, for tires of different factor f. At 1 the tire ends level with vanilla, at 2 at f² of its own.',
                legend='bl', yfmt=lambda v: f'{v:.1f}')
    return f'''
<h2>The vanilla balance</h2>
<p>A tools script (<code>tools/vanilla_tires.ps1</code>) reads every truck tire in the game's <code>initial.pak</code>, following
each <code>WheelFriction</code> through its templates, and leaves out tires without a class and invisible ones. Of 441 tires with
94 different grip sets, 16 are not matched or beaten in all three of ground (<code>BodyFriction</code>), asphalt and mud by any
other. Those 16 are the envelope: any vanilla tire is inside it.</p>
{env}
<p>For a tire with ground, asphalt and mud grip <i>G</i>, <i>A</i>, <i>M</i>, the factor is the smallest cut that brings some
envelope tire level with it or ahead of it in all three (<code>vanilla_balance.h:9-22</code>):</p>
{eq('<i>f</i> = min( 1, max over the envelope of min( <i>G</i><sub>e</sub>/<i>G</i>, <i>A</i><sub>e</sub>/<i>A</i>, <i>M</i><sub>e</sub>/<i>M</i> ) )', 29)}
<p class="where">A ratio counts as very large when the tire's own value is 0. A vanilla tire has <i>f</i> = 1. The strength <i>s</i> (0 to 2)
sets how much of the cut is applied (<code>vanilla_balance.h:27-32</code>):</p>
{eq('<i>k</i> = 1 − <i>s</i> (1 − <i>f</i>)   for <i>s</i> ≤ 1,     <i>k</i> = <i>f</i> · (1 − (<i>s</i> − 1)(1 − <i>f</i>))   for <i>s</i> &gt; 1', 30)}
<p class="where">At <i>s</i> = 2, <i>k</i> = <i>f</i>²: a tire three times the vanilla grip gets <i>f</i> = 1/3 and <i>k</i> = 1/9, and ends at a third
of vanilla. The same <i>k</i> multiplies the ground list, mud and asphalt; the modes and base grip act on top. Asphalt can be held
up by the floor <i>F</i>: max(<i>k</i>, <i>F</i>/<i>A</i><sub>0</sub>), equation (7). The floor follows an idea by Naybour.</p>
{fig}
<p>Inputs per wheel: the ground value is the truck's ground grip list for that wheel, asphalt and mud the wheel's own stock
values. The list only fills once the truck moves, so the balance (and its readout on the tab) waits for the first metres. The
envelope is the whole game's: a modded highway tire is compared with the best vanilla tire of any kind, not of its class.</p>
'''


def s_loader():
    return '''
<h2>Getting into the game: the loader</h2>
<p><code>version.dll</code> is a proxy. Windows loads a DLL the exe imports from the exe's folder before System32, and SnowRunner
imports <code>version.dll</code>. The proxy has the same 17 exports; each is a jump through a table filled at load
(<code>loader.cpp</code>, <code>version_stubs.asm</code>):</p>
<ul><li>If <code>version_chain.dll</code> sits next to it (another loader, renamed), each export is taken from that one when it
has it, otherwise from <code>System32\\version.dll</code>. A copy of the proxy running as <code>version_chain.dll</code> forwards
to System32 only and loads nothing, so two copies cannot call each other in a circle.</li>
<li><code>.asi</code> files in the folder are loaded on a thread of their own once the loader lock is free. Only names ending
exactly in <code>.asi</code> load, so <code>x.asi_off</code> is skipped; a module another ASI loader has already loaded is left
alone. It writes <code>AsiLoader.log</code>.</li>
<li>If <code>System32\\version.dll</code> cannot be loaded the proxy refuses to load, and the game reports the missing DLL instead of crashing later.
An export that neither file has (Wine's and Proton's <code>version.dll</code> lack <code>GetFileVersionInfoByHandle</code>) fails only its
own call, with <code>ERROR_PROC_NOT_FOUND</code>, and <code>AsiLoader.log</code> names it.</li></ul>
<p>The ASI does nothing outside <code>SnowRunner.exe</code>. A mutex <code>Local\\SnowRunnerTirePressure-&lt;process id&gt;</code> lets only
one copy run; a second one logs it and stops.</p>
'''


def s_find():
    pats = [['SteerSpeed, vehicle +0x69C', 'f3 0f 10 96 9c 06 00 00 f3 41 0f 5f d1'],
            ['Fuel, +0xE38 and +0xE3C', 'f3 0f 10 83 3c 0e 00 00 f3 0f 58 83 38 0e 00 00 f3 0f 59 f0'],
            ['Gear speeds +0x58 by gear +0x74', '48 63 4b 74 44 0f 29 44 24 50'],
            ['Ground list, paved flag wheel +0x1C8', 'f3 42 0f 10 0c b0 80 bf c8 01 00 00 00 0f 84'],
            ['Asphalt grip, wheel +0xA4', 'f3 0f 59 8f a4 00 00 00 f3 0f 59 15'],
            ['Radius and own offset, +0x94 and +0xB4', 'f3 0f 10 9b 94 00 00 00 f3 0f 5c 9b b4 00 00 00']]
    pats = [[a, f'<code>{b}</code>'] for a, b in pats]
    nums = [['Time stamp', '0x6a607c05 (22 July 2026)'], ['Image size', '0x2e02000'], ['Control global', '0x2a8eb78'],
            ['Wheel vtable', '0x2258918'], ['Cylinder vtable', '0x23c2c08'], ['Damage update', '0xd6f690'],
            ['GetForegroundWindow import slot', '0x21be360'], ['Truck update, return address of the call', '0xa57535'],
            ['First argument, from the return address', '0x418']]
    return f'''
<h2>Finding the game's code</h2>
<p>The mod carries no addresses. At every start it copies the game's image out of its own process (<code>ReadProcessMemory</code>
in 64 KB pieces, 4 KB pages where a piece fails, unreadable pages left as zeros) and searches it (<code>build_find.h</code>).
Patterns are only searched in the image's code sections; a place must match exactly once.</p>
<h3>What it must find</h3>
<ul><li>The wheel's virtual table, from the game's type information for <code>.?AUTRUCK_WHEEL_PARAMS@combine@@</code>, and the
cylinder shape's, from <code>.?AVhkpCylinderShape@@</code>. From a type name the type descriptor is 16 bytes before it; the object
locator is the one with signature 1 and offset 0 that points at that descriptor and at itself; the table starts 8 bytes after the
pointer to the locator. Exactly one must be found.</li>
<li>The control global, which leads to the truck you drive (+8). Found three ways, the first that matches exactly once wins:
(1) three accessor calls in a row with the Steam build's stack offsets, inside the truck update, where the second call's target is
<code>lea rax,[rip+x]; ret</code> and <i>x</i> is the global; (2) the same with the stack offsets left open; (3) a short accessor
between padding, <code>c3 cc×8 48 8d 05 ?? ?? ?? ?? c3</code>, that returns the object the global sits in, +0x18. The Game Pass
build of October 2026 (stamp 6a607d39) had neither the call nor way 1, from a player's log of 2026-10-15; ways 2 and 3 came with
1.2.1.</li>
<li>Six pieces of the game's code that use the same field offsets the mod writes. If a game update moves a field, its piece no
longer matches and the mod stands down instead of writing to the wrong place.</li></ul>
{table(['Field checked', 'Bytes'], pats, 'The layout checks (build_find.h:237-244)')}
<h3>The game-thread path (all or nothing)</h3>
<p>The damage update's start (<code>48 8b c4 48 89 48 08 55 56 57 41 54 ...</code>); the <code>GetForegroundWindow</code> slot in the
import table; and the place in the truck update that calls it through that slot, whose function must start with the expected
33-byte head (four arguments stored, eight registers pushed). From the head the mod works out where the update's first argument
lies relative to the return address: locals + 8 × pushes + 16, which is 0x3C8 + 0x40 + 0x10 = 0x418 in the Steam build.</p>
<h3>Waiting for the code</h3>
<p>Steam's exe only makes its code readable as it starts, so the mod looks up to 120 times, half a second apart. It stops as soon as
everything is found. If the essentials are found but the game-thread path is not, the same report must come back 10 looks in a
row (5 seconds) before the mod goes on without it, so a look made while the exe is still unpacking cannot pass for the real
answer. If the essentials are still missing after a minute, the mod stands down: it writes nothing and the tab says so.</p>
{table(['Place', 'Steam build'], nums, 'What the search finds in the Steam build (the offline test checks these)')}
<p><code>DumpImage=1</code> writes the copied image to <code>TirePressure_image_&lt;stamp&gt;.bin</code> next to the exe after the
search. A Game Pass install cannot be copied out of its folder; this copy, taken from memory, can be sent in for a build the mod
does not know.</p>
'''


def s_memory():
    rows = [['control (global)', '+0x8', 'the truck you drive', 'read'],
            ['vehicle', '+0x60', 'model; its +0x230 is the chassis body (velocity +0x230, forward axis +0x170, position +0x1A0)', 'read'],
            ['vehicle', '+0x68', 'truck action', 'read'],
            ['vehicle', '+0x108, +0x10C', 'damage sum and capacity', 'read'],
            ['vehicle', '+0x200, +0x208', 'wheel model array (at most 64)', 'read'],
            ['vehicle', '+0x218', 'damage handle', 'read'],
            ['vehicle', '+0x69C', 'SteerSpeed', 'written'],
            ['vehicle', '+0xE38, +0xE3C', 'engine and gearbox FuelConsumption', 'written'],
            ['truck action', '+0x58, +0x60', 'gear speed list', 'written'],
            ['truck action', '+0x74', 'gear index', 'checked'],
            ['truck action', '+0x98, +0xA0', 'ground grip list', 'written'],
            ['wheel model', '+0x2C8', 'the wheel', 'read'],
            ['wheel model', '+0x21C, +0x220', 'the contact, from the hub (the sink)', 'read'],
            ['wheel model', '+0x324', 'damage capacity it was built with', 'read'],
            ['wheel model', '+0x328, +0x329', 'broken flag, old flag', 'read; written by the by-hand break'],
            ['wheel', '+0x18', 'body', 'read'],
            ['wheel', '+0x94', 'radius (the broken one once broken)', 'read; written by the by-hand break'],
            ['wheel', '+0x98, +0x9C', 'own radius, broken radius', 'read'],
            ['wheel', '+0xA0, +0xA4', 'mud grip, asphalt grip', 'written'],
            ['wheel', '+0xA8, +0xAC', 'broken and normal damping', 'read'],
            ['wheel', '+0xB4', 'own radius offset', 'read'],
            ['wheel', '+0x1A4', 'touches the ground', 'read'],
            ['wheel', '+0x1C8 to +0x1CD', 'ground kind flags: paved, gravel, sand, hard', 'read'],
            ['body', '+0x20', 'shape (hkpCylinderShape); its +0x28 is the radius', 'written'],
            ['body', '+0xCC', 'BodyFriction', 'never written'],
            ['body', '+0x216', 'damping, Havok 16-bit float', 'written by the by-hand break']]
    return f'''
<h2>The memory map</h2>
{table(['Object', 'Offset', 'What', 'The mod'], rows, 'Every field the mod reads or writes (Steam build)', 'small')}
<h3>Guards</h3>
<ul><li>Every read and write goes through <code>ReadProcessMemory</code> and <code>WriteProcessMemory</code> on the game's own
process, and addresses below 0x10000 are refused. An object the game frees under the mod is an error return, not a crash. No game
code is changed, apart from the one import slot of the game-thread path.</li>
<li>Wheel and cylinder are recognised by their virtual tables at every lookup, and the whole chain from the control global down
is checked again before writes.</li>
<li>A wheel's values are taken as stock only from a fresh-looking wheel: radius 0.05 to 5 m, own offset at most 0.3 <i>R</i>,
asphalt above 0 and at most 10.5, mud 0 to 10.5, cylinder equal to <i>R</i> minus its offset. Anything else is left alone and
logged (up to 8 times).</li>
<li>Each value is recorded as soon as it is written, so a write that fails part way leaves a true record. Two floats count as
equal within 0.0005 × max(1, |<i>b</i>|).</li>
<li>List lengths are checked (ground list one value per wheel, gear list at most 32), a speed must be under 200 m/s, and floats
from the ini must be finite.</li></ul>
'''


def s_thread():
    boxes = {'tu': (20, 24, 150, 50, ['Truck update', 'game code, every frame'], 'game'),
             'slot': (245, 24, 150, 50, ['Import slot', 'GetForegroundWindow'], 'game'),
             'hook': (470, 24, 150, 50, ['ForegroundHook', 'the mod'], 'mod'),
             'u32': (20, 140, 150, 50, ['USER32', 'the real function'], 'game'),
             'du': (245, 140, 150, 50, ['Damage update', 'game code'], 'game'),
             'job': (470, 140, 150, 50, ['Damage job', 'posted by the Run thread'], 'mem')}
    arrows = [('tu', 'slot', 'call [slot]'), ('slot', 'hook', 'now points to'), ('job', 'hook', 'taken under a lock', {'lx': 46, 'ly': 3}),
              ('hook', 'du', 'calls, for the truck', {'lx': 30}), ('hook', 'u32', 'always calls on', {'lx': -40, 'ly': -8})]
    return f'''
<h2>The game-thread hook</h2>
<p>The game's damage update builds Havok shapes and must run on the game's thread. The mod gets there without patching code: the
truck update calls <code>GetForegroundWindow</code> through the import table once a frame, and the mod points that one slot at its
own function (<code>HookTruckUpdate</code>, <code>tire_pressure.cpp:1060-1115</code>).</p>
{diagram(boxes, arrows, h=210, cap='The game-thread hook. Only a call coming back to the truck update does anything; every call gets the real function\'s answer.')}
<ul><li>Before the swap it checks the call (<code>FF 15</code> 6 bytes before the return address, pointing at the slot), the damage
update's first 8 bytes, and that the slot's target is committed executable memory. The swap itself is one atomic pointer exchange.</li>
<li>When the return address is the truck update's, the hook reads the update's first argument at the return address + 0x418,
and the vehicle at its +0x20. If a damage job is posted for that same vehicle, which is the one you drive, it deals the damage
and calls the damage update. It then calls the real <code>GetForegroundWindow</code> and returns its answer.</li>
<li>The Run thread uses this path while the hook is in place and a truck update was seen in the last 500 ms; a job not taken
within a second is dropped. Otherwise it falls back to the by-hand break.</li></ul>
<h2>Drawing</h2>
<p>The panel, the warnings and the Tire Inflation tab are drawn by Next Gen's overlay, the same code as Next Gen Gameplay's
(<code>ng_overlay.cpp</code>), with Dear ImGui on the game's device. It hooks Present on the game's own swap chains through method
tables of their own: where ReShade is installed, ReShade's presents hand it each swap chain the game makes (<code>ng_reshade.cpp</code>);
without ReShade it takes the game's imports of <code>CreateDXGIFactory1</code> and <code>D3D11CreateDevice</code> and gives each factory
and swap chain they bring a method table of its own. DirectX's own tables are never written, so the Steam overlay, which hooks what
those hold, never meets the overlay's Present. The release brings ReShade 6.8.0 without its start banner, with a <code>ReShade.ini</code> that loads no effects and keeps Home for ReShade's own window (<code>tools/reshade_nobanner.py</code> makes that <code>dxgi.dll</code> from ReShade's own): on a tester's PC with RivaTuner, Discord's overlay and two monitors the game crashed without ReShade and not with it. Where ReShade is missing or does not take the overlay as an add-on, the start says so in the log, in a line at the top for 15 s that the first Next Gen mod in order draws, and in the menu above the tab's page (<code>NgReShadeCheck</code>). The NEXT GEN menu holds every Next Gen mod's tab in one window, on F2 or the key in <code>NextGen.ini</code>; while it
is open the game gets no mouse and no keys. Next Gen Graphics offers its switches through <code>NgGraphicsGetApi</code> (<code>ng_graphics.h</code>): the first Next Gen mod to claim it in the block draws a Graphics tab from them, and Next Gen Graphics silences its F keys meanwhile. Up to 1.2 all of this was drawn through ReShade's add-on overlay.</p>
<h2>The pad</h2>
<p>With the overlay running, every 2 seconds the mod replaces each copy of the address of <code>XInputGetState</code> (from
<code>xinput9_1_0.dll</code>) in the exe's data with its own filter (two copies in the Steam build: the input system and one filled
lazily). The filter hides the panel's buttons from the game while they act on the panel: the open binding while held, the step
chords, every panel button while the panel is open, and any of them still held after it closes until let go. It acts only while a
truck is driven. Before the game has loaded XInput, the mod reads the pad itself through <code>xinput1_4.dll</code>; without the
addresses found, the game also sees the presses. The same filter maps the two triggers through a table per trigger, built from <code>PadThrottleCurve</code> and <code>PadBrakeCurve</code> (the trigger's share to that power, 255 steps, identity at 1), only while a truck is driven.</p>
'''


def s_shared():
    gauges = [['0', 'u32', 'magic 0x47554147, "GAUG"'], ['4', 'u32', 'version (1 or later accepted)'], ['8', 'u32', 'size (64 or more accepted)'],
              ['12', 'i32', 'sequence, odd while being written'], ['16', 'u64', 'tick: GetTickCount64 at the write, ms'],
              ['24', 'u64', 'vehicle (0: none)'], ['32', 'f32', 'outside temperature, °C'], ['36', 'f32', 'coolant, °C'],
              ['40', 'f32', 'engine speed, rpm'], ['44', 'f32', 'boost, bar'], ['48', 'u32', 'flags: bit 0 vehicle, 1 temperature by map, 2 engine on'],
              ['52', 'u32 × 3', 'reserved']]
    tire = [['0', 'u32', 'magic 0x45524954, "TIRE"'], ['4', 'u32', 'version 1'], ['8', 'u32', 'size 64'], ['12', 'i32', 'sequence'],
            ['16', 'u64', 'tick, ms'], ['24', 'u64', 'vehicle (0: no truck)'], ['32', 'i32', 'mode: 0 Low, 1 Reduced, 2 Normal, 3 Increased'],
            ['36', 'i32', 'modes in use, 3 or 4'], ['40', 'f32', 'pressure, share of Normal\'s'], ['44', 'f32', 'pressure after the temperature'],
            ['48', 'f32', 'target: the mode\'s pressure'], ['52', 'f32', 'outside temperature, °C'], ['56', 'u32', 'flags, below'],
            ['60', 'i32', 'wear: % of the most worn tire, −1 unknown']]
    flags = [['0', 'a truck is driven'], ['1', 'the pressure is changing'], ['2', 'held: the truck must stop for it to go on'],
             ['3', 'too fast: the tires are taking damage'], ['4', 'a tire is worn out'], ['5', 'hard mode or the insanity mode'],
             ['6', 'the outside temperature comes from the Gauges mod']]
    return f'''
<h2>Shared memory</h2>
<p>The two mods talk through named blocks of shared memory, 64 bytes each, one per game process. Each is written about 20 times
a second and read without locks: the writer makes the sequence odd, writes, and makes it even again; a reader copies the block
and keeps the copy only if the sequence was even and did not change during the copy (up to 8 tries). A block whose tick is more
than 2 seconds old means the writer is gone. Later versions only add fields at the end.</p>
<pre>writer:  seq += 1 (odd)   barrier   fields   barrier   seq += 1 (even)
reader:  s = seq; if s odd, retry;   copy;   barrier;   if seq != s, retry</pre>
{table(['Offset', 'Type', 'Field'], gauges, 'Read: Local\\SnowRunnerGauges.State.&lt;process id&gt;, written by the Gauges mod', 'small')}
{table(['Offset', 'Type', 'Field'], tire, 'Written: Local\\SnowRunnerTirePressure.State.&lt;process id&gt; (tire_state.h)', 'small')}
{table(['Bit', 'Meaning'], flags, 'The tire block\'s flags', 'small')}
<p>The tire block is created once the game's code is found, so a mod that stood down publishes nothing. Before the first truck it
is written twice a second. <code>tire_state.h</code> carries a reader's copy function for other mods.</p>
'''


def s_sounds():
    rows = [['Air let out', 'AirOutSound', 'recording', '0.005 / 0.12 s', '0.56', 'loop while the pressure falls'],
            ['Air going in', 'AirInSound', 'recording', '0.005 / 0.12 s', '0.44', 'loop while it rises'],
            ['Compressor', 'CompressorSound', 'made by the mod', '0.5 / 0.7 s', '0.8', 'loop while it runs'],
            ['Compressor stopping', 'CompressorStopSound', 'the game\'s brake release sample', '', '0.4', 'once']]
    return f'''
<h2>Sounds</h2>
{table(['Voice', 'Ini key', 'Source', 'Attack / release', 'Level', 'Plays'], rows, 'The voices (sound.h:68-72)', 'small')}
<p>The two air recordings are the author's own, each cut into a start, a loop and an ending. The compressor's stop is read from
the game's <code>shared_sound.pak</code> (a 5.8 GB zip; only stored entries can be read); without it a made puff stands in.
Every sound is brought to the same loudness, RMS 0.1, with peaks held to 0.6; the volume is <code>SoundVolume</code>/100 × 0.22,
set by ear in the game at 50. They play through XAudio2 2.9, only with the game window in front and a truck driven.</p>
<p>The shapes follow the change (<code>SoundShapeAt</code>, <code>sound.h:471-499</code>), with <i>p</i> the progress:</p>
{eq('air out:  gain = <i>e</i> · exp(−1.036 <i>p</i>),   <i>p</i> = clamp((2 − <i>m</i>)/2, −0.5, 1)', 31)}
{eq('air in:  gain = <i>e</i> (1 − 0.3 <i>p</i>),   pitch = 1 + 0.08 <i>p</i>', 32)}
{eq('compressor:  gain = <i>e</i><sup>0.8</sup>,   pitch = (0.5 + 0.5 (1 − (1 − <i>e</i>)²)) · (1 − 0.07 <i>p</i>)', 33)}
<p class="where"><i>e</i> is the voice's envelope and <i>m</i> the pressure in modes (equation 4). Air out is 9 dB quieter at Low than at
Normal, as in the recording, about 1 dB a second; air in rises a tenth in tone, as the weaker of two bursts does in the recording;
the compressor's progress is the tank's level, (level − 0.3)/0.7.</p>
<p>The compressor is made from scratch at 44.1 kHz: 47 strokes a second, each a knock (rise 1.3 ms, fall 3 ms, timing jitter 1.2%,
strength ±8%) through six ringing band-passes at 94, 193, 310, 640, 1250 and 2300 Hz, intake air as noise falling off through each
stroke, and a hum at the 2nd and 7th harmonics. The puff is a second of shaped hiss: band-passed noise at 4500 and 1600 Hz, falling
off with a 0.2 s time constant.</p>
'''


def cold_past_room(c):
    """How far past the shared room equation (14) puts Low at the outside temperature c, as a share of the room."""
    return factors_at('default', air(0.45, c))['offset'] / 0.10


def s_limits():
    o0, o25 = cold_past_room(0), cold_past_room(-25)
    return f'''
<h2>Limits and known issues</h2>
<h3>By design</h3>
<ul><li>Made on the Steam build of 22 July 2026. The Epic build has not been tested; the Game Pass build is addressed by the 1.2.1
search but not confirmed. After a game update the mod may stand down until it is updated.</li>
<li>Only the driven truck's wheels are changed; trailers keep their tires. Co-op has not been tested.</li>
<li>Should the overlay not start (no swap chain found; the log says why) there is no panel or tab, the key cycles the modes and the pad is not filtered. Without a sound device the
sounds are off.</li>
<li>On the by-hand damage path the game's damage display follows at the next damage the game deals itself.</li>
<li>The ground grip list only fills once the truck moves, so the balance waits for the first metres.</li>
<li>The models are the mod's own reasoning: the scale, the rubber factors and the damage speeds have no measured source beyond
what each section names.</li></ul>
<h3>Fixed after this manual's first version (1.3.0)</h3>
<ul><li><b>Cold weather flattened past what the game draws.</b> Below 15 °C Low's pressure falls under Low on the scale, and its
offset is carried past 10 cm, up to 12 cm from about −18 °C down (default numbers). The room is shared out for the deepest mode's
10 cm, so before 1.3.0 equation (14) gave ×{o0:.2f} of the room at 0 °C and ×{o25:.1f} at −25 °C: on the test scout the tread sank into hard
ground by about {(o0 - 1) * SCOUT_ROOM * 1000:.0f} and {(o25 - 1) * SCOUT_ROOM * 1000:.0f} mm, less the 0.005 <i>R</i> let through. Now no offset goes past the room.</li>
<li><b>A tire that comes soft went far too flat at Low.</b> Its contact sinks inside the cylinder, which FlattenLimit did not count; now the sink is measured and taken off the room (Tires that come soft).</li>
<li><b>The gear compensation fell short in the low gears.</b> Now each gear's top grows by roll exactly, equation (20).</li></ul>
<h3>Changed in 1.3.4</h3>
<ul><li>Rolling drag (<code>RollingDrag</code>, the Realism section's "Rolling drag", on by default, untick it or set <code>RollingDrag=0</code>
to turn it off): tires roll harder on soft ground and when aired down. Each wheel on the ground gets a rolling resistance by its ground
and the pressure, through its own damping, as the game drags a flat tire. Mud counts as dirt for now. The weight is the truck's as the
game's physics has it, its gravity included, without cargo or a trailer. Hard mode and the insanity mode turn it on. Hard mode's fuel
factors are 1 now, as the engine burns more against the drag by itself.</li>
<li>The insanity mode's fuel factors are 1, as rolling drag now does what they did.</li>
<li>While rolling drag is on, every mode's fuel factor is 1: the drag makes a soft tire cost fuel by itself. The Fuel use row keeps its
value and counts again with rolling drag off.</li>
<li>The panel's pad and key controls wait while the game's function menu is up. The mod then hides no button from the menu, so the
d-pad and A work it with the panel open.</li>
<li>The last start's log is kept as <code>TirePressure.prev.log</code>.</li>
<li><code>TirePressure.log</code> names an invalid parameter in the C runtime and abort, which can end the game at once (0xC0000409). With
Next Gen Gameplay in, its log names the game's and this one names the mod's own.</li>
<li>The ASI loader starts on Linux (Proton and the Steam Deck) with the launch option <code>WINEDLLOVERRIDES="version=n,b" %command%</code>.
Before, the mods never loaded there, and with that option the game would not start.</li>
<li>With Spotter installed (its draw API, version 1 or later), the overlay draws through Spotter's frame hook instead of a hook of its
own, where ReShade has not taken it as an add-on. <code>NextGen.ini</code> <code>[Overlay] Spotter=0</code> turns it off. While the NEXT GEN
menu is open, the game gets no mouse or keys and Spotter's hotkeys rest. While Spotter's own menu or big map is open, the menu's key
does nothing.</li>
<li>Nothing about ReShade is shown on screen any more: the notice in the NEXT GEN menu and the line at the top for 15 seconds are gone.
The log says which route the overlay takes. ReShade's <code>dxgi.dll</code> and <code>ReShade.ini</code> stay in the zip for RivaTuner users,
whose game does not boot without it. The mods draw without it.</li></ul>
<h3>Changed in 1.3.3</h3>
<ul><li>The step bindings are the plain d-pad left and right, <code>DPadLeft</code> and <code>DPadRight</code> by default (an ini's
<code>LB+DPadLeft</code> becomes <code>DPadLeft</code>). They act only while the panel is open and no shoulder button is held. LB + d-pad
is for gears and never the panel's, open or closed. While the panel is open the game sees none of the plain d-pad, so nothing shifts
from inside it (two testers' reports, 27 October).</li></ul>
<h3>Changed in 1.3.2</h3>
<ul><li>The overlay copies a swap chain's method table that another program has put at the start of its memory. Before, it drew
nothing there (no panel and no NEXT GEN menu on two players' PCs). When a table still cannot be copied, <code>TirePressure.log</code>
says in whose code the table and its Present lie, so the program can be named.</li>
<li>LB + d-pad left or right works only while the panel is open. The two chords no longer open the panel a step lower or higher on
their own, so a chord hit while driving changes nothing, and the game keeps those buttons while the panel is closed.</li>
<li>A worn out or broken tire keeps a slight grip, <code>WornGrip</code> percent of its own on the ground, on asphalt and in mud,
10 by default, so the truck still moves; 0 gives the old none (the testers' ask). Hard mode and the insanity mode take 10 too. The tab
has it as Worn tire grip.</li>
<li>The pad bindings rest while the NEXT GEN menu is open, as the panel key does. A binding held as the menu opens or closes is no
press when it closes.</li></ul>
<h3>Not changed, on purpose or not yet measured</h3>
<ul>
<li><b>Above Normal the Increased numbers count even when Increased is off.</b> In hot weather Normal slides toward them
(equation 3), as over-inflated tires do; in the insanity mode the numbers used are its hidden Increased ones.</li>
<li><b>The pull of a flattened wheel</b> depends on how the game turns torque into push (see How a flattened tire rolls): worked out, not measured.</li>
<li>The pressure step and the air tank count 0.05 s per pass, not the measured time; the stand-still test is one pass late; the
two air sounds have no made stand-in if their recordings fail to load.</li></ul>
<h3>Where the README is looser than the code</h3>
<ul><li>A change can take longer than the "up to 6 seconds" of the README: Low to Increased takes 8.7 s at the default.</li>
<li>Hard mode's offsets are 5 and 2.5 cm only on tires with the room; equation (13) shrinks them on others, and the cold carries
them further.</li>
<li>The balance compares with the best vanilla tire of any kind, not of the tire's own kind.</li>
<li>"Grip stops at 10" holds for the multipliers and the ground grip list; the asphalt and mud values written to a wheel are not
capped.</li></ul>
'''


def s_tests():
    items = ['The loader: the proxy is the one in use, forwarded calls return the system\'s answer, a test <code>.asi</code> is loaded; the same through a chain DLL ending in System32; and a module another loader already loaded is left alone.',
             'The ini: a missing ini is written with the defaults; every key reads back as written; help text and unknown keys survive; "nan" and "inf" give the defaults; old <code>Seconds=3</code> and <code>ConfirmSeconds=15</code> move to 6 and 5 once.',
             'Flattening: FlattenLimit within 2.5 mm of the hand-worked values for five radii; the room shared out as equation (13) says.',
             'Rolling: ×1 at stock, ×1.117 at the scout\'s Low, 95.1% of the speed kept; the gear numbers give each gear\'s top ×1.117 exactly.',
             'Cold: a 12 cm Low gets no more than the room, on the scout and on a 0.80 m tire with room to spare.',
             'A soft tire: a contact 3 cm inside the cylinder settles at 3 cm and comes off the room; a firm tire\'s takes nothing, and readings outside the window are not taken.',
             'The scale: each mode\'s pressure gives the mode\'s numbers; half way gives the mean; half a step past each end, then flat.',
             'The temperature: 15 °C changes nothing; Normal holds 0.827 at −20 °C and 1.098 at 35 °C; every effect goes the right way.',
             'The balance: <i>f</i> at strength 1, <i>f</i>² at 2, in between at 1.5; a vanilla tire stays at 1.',
             'Hard and insanity mode: each sets its numbers and options and leaves the key, volume and AmbientC; the insanity mode wins over hard; both turn rolling drag on, with fuel ×1.',
             'Rolling drag: at 10 m/s on paved at Normal the written word\'s torque within 1 % of Crr·N·r, with N by the Havok mass and not the files\'; the same at three times the spin; under the floor the torque falls to nothing; ice none, sand at Low ×1.49, no flag dirt; a broken wheel left alone; airborne, off and another truck get the stock back; a write over 2 % and on the game\'s word; no climb over 10 lost records; none while the probe holds; the probe\'s stock back in the truck update; no hook, off.',
             'Shared state: the block is created, written and read back by name, every field at its offset.',
             'Sounds: WAV and ADPCM decoding, pak reading (zip and zip64), loop and ending cuts, made loops that join without a click and stay within level, the compressor starting on the right filling and stopping once, change times matching the passes.',
             'Game code, with a copy of the Steam image: every place found at its known address, and each of the three ways to the control global alone gives the same one.']
    return '<h2>The offline tests</h2><p><code>test.bat</code> builds and runs them without the game:</p><ul>' + ''.join(f'<li>{i}</li>' for i in items) + '</ul>'


def s_constants():
    rows = [['kModePressure', '0.45, 0.65, 1, 1.25', 'share of Normal', 'Normal 35 psi; the rest the mod\'s choice'],
            ['Factor ranges', 'grip, fuel, steering 0.1 to 10; offset 0 to 0.25 m', '', 'Expeditions\' ranges for its keys'],
            ['Scale overrun', 'half a step (t from −0.5 to 1.5)', '', 'the mod'],
            ['Pass', '50 ms sleep; 0.05 s step', '', '"20 times a second"'],
            ['Seconds', '6 (hard 30, insanity 25)', 's, Normal to Low', 'Expeditions takes 3; 6 since 1.1.0 for the sounds; hard: a truck\'s compressor'],
            ['ConfirmSeconds', '5', 's', '1.1.0'],
            ['Stand still', '0.3', 'm/s', '"about 1 km/h"'],
            ['Still detection', 'under 0.01 mm a pass for 4 passes', '', 'a paused game keeps its velocity'],
            ['Rolling for fuel', 'on above 0.5, stays above 0.25', 'm/s', 'Expeditions applies its modifier above a speed'],
            ['Air tank', 'use 0.17 a mode, start below 0.45, refill 0.04 a second', 'of a tank', 'not stated'],
            ['Gas law', 'atmosphere 0.42 of Normal; reference 15 °C (288.15 K)', '', 'Normal 35 psi over 14.7'],
            ['r_hard', '−0.006 a degree below 15, +0.003 above, 0.7 to 1.08', '', '"a fifth less at 20 below"'],
            ['r_soft', '−0.003 a degree below 15, at least 0.85', '', 'not stated'],
            ['r_speed', '+0.006 a degree below 15, 0.75 to 1.25', '', 'not stated'],
            ['Temperature step', '0.5', '°C', 'no rewrite on drift'],
            ['Damage speeds', 'Low 20 to 30, Reduced 35 to 45 (default)', 'km/h', 'Expeditions\' off-road tires, lowered by driving on 2026-10-04'],
            ['Damage', 'Low 4 to 8 a tick, Reduced 2 to 6', 'points', 'Expeditions\' values doubled'],
            ['Tick', 'Low 7, Reduced 10', 's', 'not stated'],
            ['Reset window', '1', 's', '20 Hz polling against a check every frame'],
            ['Wheel damage', 'about 50', 'points', 'the game'],
            ['FlattenLimit', 'q = min(1.5h, 0.25R) + 0.02; pull 0.8 − 1.1 a/z; z = (min(0.2R, q) + 0.1) 0.8π/R; let through 0.005R', '', 'the game\'s wheel update and wheel shader'],
            ['Cylinder floor', '0.4', '× R', '"whatever the ini says"'],
            ['Sink', 'blend 0.025 a pass (about 2 s); 5 mm steps, moved at 3/4 of a step; window 0.85 to 1.02 of the cylinder', '', 'the mod (1.3.0)'],
            ['Gear numbers', 'a′ = roll·a + 2.5(roll − 1); high gear 2(roll − 1)', '', 'the game\'s 2a + 5, as the Gauges mod reads it'],
            ['Havok half float', '× 1.003906, upper 16 bits', '', 'the game\'s rounding'],
            ['Stock acceptance', 'R 0.05 to 5 m, own offset ≤ 0.3R, asphalt (0, 10.5], mud [0, 10.5]', '', 'what a tire file can hold'],
            ['Wheels, gears', 'at most 64 wheels, 32 gear values', '', 'not stated'],
            ['Search', '120 looks, 500 ms apart; 10 equal looks for a partial result', '', 'Steam unpacks its code at start'],
            ['Game thread', 'update seen within 500 ms; job dropped after 1 s', '', 'not stated'],
            ['Pad rescan', '2', 's', 'not stated'],
            ['Shared block', '64 bytes; stale after 2 s; looked for every 5 s', '', 'the mod'],
            ['Sound level', 'RMS 0.1, peaks 0.6, volume × 0.22', '', 'set by ear at 50'],
            ['Envelope', '16 of 441 vanilla tires (94 grip sets)', '', 'initial.pak, tools/vanilla_tires.ps1']]
    return f'<h2>Constants at a glance</h2>{table(["Constant", "Value", "Unit", "Where it comes from"], rows, "Every constant of the models", "small")}'


def body():
    return ''.join([s_intro(), s_overview(), s_scale(), s_speed(), s_grip(), s_flat(), s_roll(), s_fuel(), s_drag(), s_damage(), s_temp(),
                    s_presets(), s_balance(), s_loader(), s_find(), s_memory(), s_thread(), s_shared(), s_sounds(), s_limits(),
                    s_tests(), s_constants()])
