"""Offline check of the cat rig's poses: a numpy replica of UShadeCatRig::Solve (spine chain + leg IK) that reports how far
each leg is from reaching its paw target ("over" > 0 means the paw would hang in the air), for the sit / sleep poses and
the staged transitions between them (UShadeCatRig::BlendShaped). Joint positions are the ones make_cat.py builds.
Rig space: X forward, Y right, Z up, cm at unit scale (Blender metres * 100, X = -y).

    python Scripts/cat_rig_check.py

Keep the numbers here in step with ComputeSit / ComputeSleep / BlendShaped when those change."""
import numpy as np

def bpt(x, y, z):  # blender metres -> rig cm
    return np.array([-y * 100.0, x * 100.0, z * 100.0])

R = {}
R['Pelvis'] = bpt(0, 0.15, 0.212)
R['Spine1'] = bpt(0, 0.07, 0.215)
R['Spine2'] = bpt(0, -0.01, 0.218)
R['Spine3'] = bpt(0, -0.085, 0.222)
R['Neck'] = bpt(0, -0.125, 0.226)
R['Head'] = bpt(0, -0.19, 0.279)
for s, sx in (('L', -1), ('R', 1)):   # side sign irrelevant for reach
    R['Scap' + s] = bpt(sx * 0.026, -0.078, 0.252)
    R['UArm' + s] = bpt(sx * 0.036, -0.123, 0.188)
    R['FArm' + s] = bpt(sx * 0.039, -0.098, 0.113)
    R['Hand' + s] = bpt(sx * 0.038, -0.106, 0.034)
    R['Fing' + s] = bpt(sx * 0.0375, -0.118, 0.0125)
    R['Thigh' + s] = bpt(sx * 0.037, 0.148, 0.196)
    R['Calf' + s] = bpt(sx * 0.042, 0.098, 0.122)
    R['Foot' + s] = bpt(sx * 0.04, 0.167, 0.066)
    R['Toe' + s] = bpt(sx * 0.039, 0.152, 0.0135)
R['Tail1'] = bpt(0, 0.172, 0.222)

def L(a, b): return float(np.linalg.norm(R[b] - R[a]))
LEGS = [('ScapL', 'UArmL', 'FArmL', 'HandL', 'FingL'), ('ScapR', 'UArmR', 'FArmR', 'HandR', 'FingR'),
        (None, 'ThighL', 'CalfL', 'FootL', 'ToeL'), (None, 'ThighR', 'CalfR', 'FootR', 'ToeR')]
HOME = [R[c[4]] for c in LEGS]

def rot(pitch, yaw=0.0, roll=0.0):
    # FRotator(P, Y, R) -> matrix (UE: positive pitch = nose up)
    p, y, r = np.radians([pitch, yaw, roll])
    SP, CP, SY, CY, SR, CR = np.sin(p), np.cos(p), np.sin(y), np.cos(y), np.sin(r), np.cos(r)
    # FRotationMatrix rows are the axes; columns transform: v' = v * M -> use transpose
    M = np.array([
        [CP * CY, CP * SY, SP],
        [SR * SP * CY - CR * SY, SR * SP * SY + CR * CY, -SR * CP],
        [-(CR * SP * CY + SR * SY), CY * SR - CR * SP * SY, CR * CP]])
    return M.T  # world = M.T @ local

class Pose:
    def __init__(self):
        self.pel = np.zeros(3); self.pitch = 0.0; self.roll = 0.0
        self.sp = [0.0] * 4; self.sy = [0.0] * 4
        self.ball = [h.copy() for h in HOME]; self.meta = [0.0] * 4; self.curl = [0.0] * 4; self.scap = [0.0, 0.0]

def lerp(a, b, t): return a + (b - a) * t

def blend(A, B, t):
    P = Pose()
    P.pel = lerp(A.pel, B.pel, t); P.pitch = lerp(A.pitch, B.pitch, t); P.roll = lerp(A.roll, B.roll, t)
    P.sp = [lerp(a, b, t) for a, b in zip(A.sp, B.sp)]; P.sy = [lerp(a, b, t) for a, b in zip(A.sy, B.sy)]
    P.ball = [lerp(a, b, t) for a, b in zip(A.ball, B.ball)]
    P.meta = [lerp(a, b, t) for a, b in zip(A.meta, B.meta)]; P.curl = [lerp(a, b, t) for a, b in zip(A.curl, B.curl)]
    P.scap = [lerp(a, b, t) for a, b in zip(A.scap, B.scap)]
    return P

def solve(P, verbose=False):
    Body = rot(P.pitch, 0, P.roll)
    PPel = R['Pelvis'] + P.pel
    QPel = Body @ rot(P.sp[0], P.sy[0])
    PS1 = PPel + QPel @ (R['Spine1'] - R['Pelvis']); Q1 = QPel @ rot(P.sp[1], P.sy[1])
    PS2 = PS1 + Q1 @ (R['Spine2'] - R['Spine1']); Q2 = Q1 @ rot(P.sp[2], P.sy[2])
    PS3 = PS2 + Q2 @ (R['Spine3'] - R['Spine2']); Q3 = Q2 @ rot(P.sp[3], P.sy[3])
    out = {'PS1': PS1, 'PS3': PS3, 'PPel': PPel, 'over': [], 'shoulder': [], 'paw_err': []}
    for l, C in enumerate(LEGS):
        A = np.radians(P.meta[l])
        MDir = np.array([np.sin(A), 0, -np.cos(A)])
        Lmeta, L1, L2 = L(C[3], C[4]), L(C[1], C[2]), L(C[2], C[3])
        Ball = P.ball[l]
        if C[0]:
            ScapTop = PS3 + Q3 @ (R[C[0]] - R['Spine3'])
            QS = Q3 @ rot(P.scap[l % 2])
            Root = ScapTop + QS @ (R[C[1]] - R[C[0]])
        else:
            Root = PPel + QPel @ (R[C[1]] - R['Pelvis'])
        T = Ball - MDir * Lmeta
        d = np.linalg.norm(T - Root)
        over = d - 0.985 * (L1 + L2)
        reach = min(d, 0.999 * (L1 + L2))
        End = Root + (T - Root) / max(d, 1e-6) * reach
        BallOut = End + MDir * Lmeta
        out['over'].append(over); out['shoulder'].append(Root); out['paw_err'].append(np.linalg.norm(BallOut - Ball))
    return out

def report(name, P):
    o = solve(P)
    s = ' '.join(f"{o['over'][l]:+5.1f}" for l in range(4))
    e = ' '.join(f"{o['paw_err'][l]:4.1f}" for l in range(4))
    sh = o['shoulder'][0]; hp = o['shoulder'][2]
    print(f"{name:28s} over[FL FR HL HR] {s} | paw miss {e} | shoulder ({sh[0]:5.1f},{sh[2]:5.1f}) hip ({hp[0]:5.1f},{hp[2]:5.1f}) PS3 z {o['PS3'][2]:5.1f}")
    return o

def stand():
    P = Pose()
    for l in range(4):
        P.meta[l] = 10.0 if l < 2 else 9.0
    return P

def sit(pel=(2, 0, -11), pitch=26, sp=(0, 6, 6, 3), fx=-0.5, meta=12, scap=4, hx=11.5, hmeta=86):
    P = Pose()
    P.pel = np.array(pel, float); P.pitch = pitch; P.sp = list(sp)
    for l in range(4):
        H = HOME[l]
        if l < 2:
            P.ball[l] = np.array([H[0] + fx, H[1] * 0.9, H[2]]); P.meta[l] = meta; P.scap[l] = scap
        else:
            P.ball[l] = np.array([H[0] + hx, H[1] * 1.35, H[2]]); P.meta[l] = hmeta
    return P

def sleep():
    P = Pose()
    P.pel = np.array([1, 0, -10.8]); P.pitch = -1; P.sp = [0, -2, -2, -3]; P.sy = [0, 3, 5, 5]
    for l in range(4):
        H = HOME[l]
        if l < 2:
            P.ball[l] = np.array([H[0] - 3.8, H[1] * 0.85, 1.0]); P.meta[l] = -85; P.curl[l] = 10; P.scap[l] = -6
        else:
            P.ball[l] = np.array([H[0] + 8.7, H[1] * 1.45, 1.1]); P.meta[l] = 88
    return P

# --- current poses and transition schedules -------------------------------------------------------------------------
import copy
def S(x): x=min(max(x,0),1); return x*x*(3-2*x)
def W(s,a,b): return S((s-a)/(b-a))

def newsit():  # = UShadeCatRig::ComputeSit
    return sit(pel=(2,0,-11),pitch=20,sp=(0,2,6,3),scap=-6,fx=1.0,meta=10)

def loco_from(Pseat=None):
    P=stand()
    if Pseat is not None:   # gait paws synced to the seated pose
        for l in range(4): P.ball[l]=Pseat.ball[l].copy()
    return P

def lerp_body(O,A,B,w):
    O.pel=lerp(A.pel,B.pel,w); O.pitch=lerp(A.pitch,B.pitch,w); O.roll=lerp(A.roll,B.roll,w)
    O.sp=[lerp(a,b,w) for a,b in zip(A.sp,B.sp)]; O.sy=[lerp(a,b,w) for a,b in zip(A.sy,B.sy)]
def lerp_paw(O,A,B,l,w,arc=0.0,meta=None):
    O.ball[l]=lerp(A.ball[l],B.ball[l],w); 
    d=np.linalg.norm(B.ball[l]-A.ball[l]); O.ball[l][2]+=min(arc,0.35*d)*np.sin(np.pi*w)
    O.meta[l]=lerp(A.meta[l],B.meta[l],w) if meta is None else meta
    O.curl[l]=lerp(A.curl[l],B.curl[l],w)
    if l<2: O.scap[l]=lerp(A.scap[l],B.scap[l],w)

def rise_from_loaf(Psl,Psit,u):
    O=copy.deepcopy(Psit)
    body=0.15*W(u,0,0.25)+0.85*W(u,0.48,0.95)
    lerp_body(O,Psl,Psit,body)
    up=W(u,0.48,0.95)
    for l,(a,b) in ((0,(0.1,0.36)),(1,(0.28,0.54))):
        t=W(u,a,b)
        m=lerp(lerp(Psl.meta[l],75,t),Psit.meta[l],up)
        lerp_paw(O,Psl,Psit,l,t,arc=1.6,meta=m)
        O.scap[l]=lerp(Psl.scap[l],Psit.scap[l],body)
    for l in (2,3): lerp_paw(O,Psl,Psit,l,W(u,0.55,0.92),arc=1.0)
    return O

def sit_down(Pl,Psit,u):
    O=copy.deepcopy(Psit)
    lerp_body(O,Pl,Psit,W(u,0,0.8))
    for l in (2,3): lerp_paw(O,Pl,Psit,l,W(u,0.05,0.6),arc=1.2)
    for l in (0,1): lerp_paw(O,Pl,Psit,l,W(u,0.45,0.85),arc=1.4)
    return O

def get_up(Psit,Pl,r):
    O=copy.deepcopy(Pl)
    lerp_body(O,Psit,Pl,W(r,0,0.85))
    for l in (2,3): lerp_paw(O,Psit,Pl,l,W(r,0,0.85))
    for l in (0,1): lerp_paw(O,Psit,Pl,l,W(r,0.1,0.9))
    return O

def scan(name,f,n=21):
    worst=-99; rows=[]
    for i in range(n):
        s=i/(n-1); o=solve(f(s)); ov=max(o['over'][0],o['over'][1]); worst=max(worst,ov)
        rows.append(f"{s:.2f}:{ov:+.1f}/{max(o['over'][2],o['over'][3]):+.1f}")
    print(f"{name:16s} worst front over {worst:+.2f}  ", ' '.join(rows[::2]))

TAIL_PTS = [(0, 0.172, 0.222), (0, 0.215, 0.226), (0, 0.26, 0.226), (0, 0.305, 0.222), (0, 0.35, 0.216), (0, 0.393, 0.208),
            (0, 0.433, 0.199), (0, 0.468, 0.19), (0, 0.488, 0.185)]
SIT_TP = [-24, -34, -40, -26, -24, -24, -24, -22]   # = UShadeCatRig::ComputeSit TP / TY
SIT_TY = [21, 43, 80, 155, 168, 178, 191, 209]


def sit_tail(P, TP=SIT_TP, TY=SIT_TY, ground=1.1):
    """Tail joint positions for a pose (pelvis frame = body pitch, as UpdateTail's TailFrame), clamped above the ground."""
    pts = [bpt(*q) for q in TAIL_PTS]
    Ls = [float(np.linalg.norm(pts[i + 1] - pts[i])) for i in range(8)]
    Ls[7] = 2.45
    QPel = rot(P.pitch, 0, P.roll) @ rot(P.sp[0], P.sy[0])
    x = R['Pelvis'] + P.pel + QPel @ (R['Tail1'] - R['Pelvis'])
    out = [x.copy()]
    for i in range(8):
        t, f = np.radians(TP[i]), np.radians(TY[i])
        d = QPel @ np.array([-np.cos(t) * np.cos(f), np.cos(t) * np.sin(f), np.sin(t)])
        n = x + d * Ls[i]
        if n[2] < ground:
            n[2] = ground
            n = x + (n - x) / np.linalg.norm(n - x) * Ls[i]
        x = n
        out.append(x.copy())
    return out


if __name__ == '__main__':
    report('stand', stand())
    report('sit', newsit())
    report('sleep', sleep())
    scan('loaf->sit', lambda u: rise_from_loaf(sleep(),newsit(),u))
    scan('stand->sit', lambda u: sit_down(stand(),newsit(),u))
    scan('sit->stand', lambda r: get_up(newsit(),loco_from(newsit()),r))
    print('sit tail (x, y, z):', ' '.join('(%.0f,%.0f,%.1f)' % tuple(p) for p in sit_tail(newsit())))
