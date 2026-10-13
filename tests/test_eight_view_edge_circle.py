"""Native eight-view contract, known geometry, and real-image replay."""
import ctypes as C
import hashlib,json,pathlib,re,sys,unittest
import cv2,numpy as np
from scipy.special import ndtr
from test_four_view_edge_circle import ROOT,lib,DLL

class Result(C.Structure):
    _fields_=[(k,C.c_double) for k in ['center_x_mm','center_y_mm','radius_mm','rms_residual_um','max_residual_um','notch_angle_deg']]

def invoke(images,stages,scale):
    call=lib.Wafer_FindEightViewEdgeCircle
    call.argtypes=[C.POINTER(C.POINTER(C.c_ubyte)),C.POINTER(C.c_double),C.c_int,C.c_int,C.c_double,C.POINTER(Result)]
    call.restype=C.c_int
    arrays=[np.ascontiguousarray(a,np.uint8) for a in images]
    ptrs=(C.POINTER(C.c_ubyte)*8)(*[a.ctypes.data_as(C.POINTER(C.c_ubyte)) for a in arrays])
    xy=(C.c_double*16)(*np.asarray(stages).ravel());r=Result()
    code=call(ptrs,xy,arrays[0].shape[1],arrays[0].shape[0],scale,C.byref(r))
    return code,{k:getattr(r,k) for k,_ in Result._fields_}

def synthetic(angle=-.41,notch=True):
    center=np.array([-1.03,83.60]);radius=150.;scale=.01;n=1024
    normals=np.array([[1,1],[1,-1],[-1,1],[-1,-1],[1,0],[-1,0],[0,1],[0,-1]],float)
    normals/=np.linalg.norm(normals,axis=1)[:,None];stages=center+normals*radius
    v,u=np.mgrid[:n,:n];images=[]
    for stage in stages:
        x=stage[0]+(u-n/2)*scale-center[0];y=stage[1]-(v-n/2)*scale-center[1]
        theta=np.degrees(np.arctan2(x,-y));d=(theta-angle+180)%360-180
        dent=1.1*np.exp(-(d/.5)**2) if notch else 0
        a=3+70*ndtr((radius-dent-np.hypot(x,y))/.003)
        images.append(np.rint(a).astype(np.uint8))
    return images,stages,center,radius

class Tests(unittest.TestCase):
    def test_export(self):
        self.assertTrue(hasattr(lib,'Wafer_FindEightViewEdgeCircle'),'missing eight-view DLL export')
        self.assertEqual(C.sizeof(Result),48)

    def test_known_notch_and_readonly(self):
        for angle in [-.41,.41]:
            images,stages,c,r=synthetic(angle)
            before=[hashlib.sha256(a.tobytes()).digest() for a in images]
            code,out=invoke(images,stages,10.)
            self.assertEqual(code,0,out)
            self.assertLess(np.linalg.norm(np.array([out['center_x_mm'],out['center_y_mm']])-c),.004)
            self.assertLess(abs(out['radius_mm']-r),.04)
            self.assertLess(abs(out['notch_angle_deg']-angle),.01)
            self.assertEqual(before,[hashlib.sha256(a.tobytes()).digest() for a in images])

    def test_no_notch_and_invalid(self):
        images,stages,c,r=synthetic(notch=False)
        code,out=invoke(images,stages,10.)
        self.assertEqual(code,0,out);self.assertTrue(np.isnan(out['notch_angle_deg']))
        for scale in [0,float('nan')]:
            code,out=invoke(images,stages,scale)
            self.assertEqual(code,1);self.assertTrue(all(np.isnan(v) for v in out.values()))
        bad=stages.copy();bad[7,1]=float('nan')
        code,out=invoke(images,bad,10.);self.assertEqual(code,1)
        bad=stages.copy();bad[[4,5]]=bad[[5,4]]
        self.assertEqual(invoke(images,bad,10.)[0],1)
        blank=[np.zeros_like(images[0]) for _ in range(8)]
        code,out=invoke(blank,stages,10.);self.assertEqual(code,4)
        self.assertTrue(all(np.isnan(v) for v in out.values()))

    def test_eighth_view_contributes(self):
        images,stages,c,r=synthetic(notch=False)
        code,base=invoke(images,stages,10.);self.assertEqual(code,0)
        changed=stages.copy();changed[7,1]+=.05
        code,out=invoke(images,changed,10.)
        self.assertEqual(code,0,out)
        self.assertGreater(abs(out['center_y_mm']-base['center_y_mm']),.005)

def replay():
    records=[]
    for name,scale in [('EdgeImage_1.5X_thick_8',3.),('EdgeImage_2.5X_thick_8',1.8),('EdgeImage_2.5X_8',1.8)]:
        folder=pathlib.Path(r'D:\images\谷神星\标准化')/name;files=sorted(folder.glob('*.jpg'));assert len(files)==8
        images=[cv2.imdecode(np.fromfile(f,np.uint8),0) for f in files]
        stages=[[float(x) for x in re.sub(r' \(\d+\)$','',f.stem).split('_')[1:]] for f in files]
        code,out=invoke(images,stages,scale)
        assert code==0 and np.isfinite(out['notch_angle_deg']),(name,code,out)
        records.append({'folder':str(folder),'pixel_size_um':scale,'return_code':code,'result':out,'files':[{'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in files]})
    output=ROOT/'output/EightViewEdgeCircle';output.mkdir(exist_ok=True)
    (output/'dll_replay.json').write_text(json.dumps({'dll':str(DLL),'dll_sha256':hashlib.sha256(DLL.read_bytes()).hexdigest(),'records':records},ensure_ascii=False,indent=2),encoding='utf-8')
    for r in records:print(r['folder'],r['result'])

if __name__=='__main__':
    if '--replay' in sys.argv:replay()
    else:unittest.main()
