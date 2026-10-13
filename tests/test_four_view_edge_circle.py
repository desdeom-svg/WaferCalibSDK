"""Exercise the compiled DLL, including its C ABI and real-image replay."""
import ctypes as C
import hashlib
import json
import os
import pathlib
import sys
import unittest
import cv2
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
DLL = ROOT / 'build/Release/WaferCalibSDK.dll'
SEARCH = os.add_dll_directory(r'D:\DevSDKs\opencv4100\opencv\build\x64\vc16\bin')
class Result(C.Structure):
    _fields_ = [(k, C.c_double) for k in ['center_x_mm', 'center_y_mm', 'radius_mm', 'rms_residual_um', 'max_residual_um']]
lib = C.CDLL(str(DLL))
call = lib.Wafer_FindFourViewEdgeCircle
call.argtypes = [C.POINTER(C.POINTER(C.c_ubyte)), C.POINTER(C.c_double), C.c_int, C.c_int, C.c_double, C.POINTER(Result)]
call.restype = C.c_int

def invoke(images, stages, scale=.9):
    arrays = [np.ascontiguousarray(a, dtype=np.uint8) for a in images]
    pointers = (C.POINTER(C.c_ubyte)*4)(*[a.ctypes.data_as(C.POINTER(C.c_ubyte)) for a in arrays])
    coords = (C.c_double*8)(*np.asarray(stages).ravel())
    r = Result()
    code = call(pointers, coords, arrays[0].shape[1], arrays[0].shape[0], scale, C.byref(r))
    return code, {k:getattr(r,k) for k,_ in Result._fields_}

def synthetic(gains=(1,1,1,1), offsets=(0,0,0,0), w=1024):
    scale=.0009; center=np.array([-1.025,83.61]); radius=150.
    normals=np.array([[1,1],[1,-1],[-1,1],[-1,-1]])/np.sqrt(2)
    stages=center+normals*radius
    v,u=np.mgrid[:w,:w]; images=[]
    for s,g,b in zip(stages,gains,offsets):
        x=s[0]+(u-w/2)*scale; y=s[1]-(v-w/2)*scale
        # Gaussian edge spread; normalized 0.15 crossing lies outside geometric edge.
        from scipy.special import ndtr
        a=3+70*ndtr((radius-np.hypot(x-center[0],y-center[1]))/.0015)
        images.append(np.rint(np.clip(g*a+b,0,255)).astype(np.uint8))
    return images,stages,center,radius

class ContractTests(unittest.TestCase):
    def test_minimum_width_and_noise(self):
        images,s,c,_=synthetic(w=512)
        code,out=invoke(images,s)
        self.assertEqual(code,0,out)
        self.assertLess(np.linalg.norm([out['center_x_mm']-c[0],out['center_y_mm']-c[1]]),.002)
        rng=np.random.default_rng(42)
        noisy=[np.clip(a.astype(float)+rng.normal(0,1,a.shape),0,255).astype(np.uint8) for a in images]
        code,out=invoke(noisy,s)
        self.assertEqual(code,0,out)
        self.assertLess(np.linalg.norm([out['center_x_mm']-c[0],out['center_y_mm']-c[1]]),.002)
        for width in [256,260,511]:
            code,out=invoke([a[:,:width].copy() for a in images],s)
            self.assertEqual(code,1)
            self.assertTrue(all(np.isnan(v) for v in out.values()))

    def test_known_center_and_photometric_changes(self):
        base=None
        for gains,offsets in [((1,1,1,1),(0,0,0,0)),((.5,1.5,.8,2),(3,8,0,5))]:
            images,s,c,r=synthetic(gains,offsets)
            before=[hashlib.sha256(a.tobytes()).hexdigest() for a in images]
            code,out=invoke(images,s)
            self.assertEqual(code,0,out)
            self.assertLess(np.linalg.norm([out['center_x_mm']-c[0],out['center_y_mm']-c[1]]),.002)
            self.assertLess(abs(out['radius_mm']-r),.006)
            self.assertLess(out['rms_residual_um'],3)
            self.assertGreaterEqual(out['max_residual_um'],out['rms_residual_um'])
            self.assertEqual(before,[hashlib.sha256(a.tobytes()).hexdigest() for a in images])
            if base:self.assertLess(np.linalg.norm([out['center_x_mm']-base['center_x_mm'],out['center_y_mm']-base['center_y_mm']]),.001)
            base=out

    def test_invalid_and_blank(self):
        images,s,_,_=synthetic()
        for scale in [0,-1,float('nan'),float('inf')]:
            code,out=invoke(images,s,scale)
            self.assertEqual(code,1)
            self.assertTrue(all(np.isnan(v) for v in out.values()))
        code,out=invoke([np.zeros((1024,1024),np.uint8)]*4,s)
        self.assertEqual(code,4)
        self.assertTrue(all(np.isnan(v) for v in out.values()))
        r=Result()
        self.assertEqual(call(None,None,1024,1024,.9,C.byref(r)),1)
        self.assertTrue(np.isnan(r.center_x_mm))
        self.assertEqual(call(None,None,1024,1024,.9,None),1)
        pointers=(C.POINTER(C.c_ubyte)*4)()
        coords=(C.c_double*8)(*s.ravel())
        self.assertEqual(call(pointers,coords,1024,1024,.9,C.byref(r)),1)
        bad=s.copy();bad[0,0]=float('nan')
        self.assertEqual(invoke(images,bad)[0],1)
        self.assertNotEqual(invoke(images,np.tile(s[0],(4,1)))[0],0)

def replay():
    records={'dll':str(DLL),'sha256':hashlib.sha256(DLL.read_bytes()).hexdigest(),'groups':{}}
    for name in ['EdgeImage_5X','EdgeImage']:
        folder=pathlib.Path(r'D:\images\谷神星\标准化')/name
        fs=sorted(folder.glob('*'))
        if len(fs)!=4:raise RuntimeError(f'Expected four images in {folder}')
        arrays=[cv2.imdecode(np.fromfile(f,np.uint8),0) for f in fs]
        s=np.array([[float(v) for v in f.stem.split('_')[1:]] for f in fs])
        code,out=invoke(arrays,s)
        if code:raise RuntimeError((name,code,out))
        records['groups'][name]={'status':code,'result':out,'files':[{'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in fs]}
    out=ROOT/'output/FourViewEdgeCircle'
    out.mkdir(exist_ok=True,parents=True)
    (out/'dll_replay.json').write_text(json.dumps(records,indent=2,ensure_ascii=False),encoding='utf-8')
    print(json.dumps(records,indent=2,ensure_ascii=False))

if __name__=='__main__':
    if '--replay' in sys.argv:replay()
    else:unittest.main()
