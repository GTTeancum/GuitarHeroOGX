import unittest
import numpy as np
from transfer_finger_weights import closest_surface,fit_lobe,matrix,serialized,points,partition,anatomical_surface

class FingerTransferTests(unittest.TestCase):
    def test_source_row_matrix_roundtrip(self):
        x=[0,1,0,-1,0,0,0,0,1,3,4,5]
        self.assertEqual(serialized(matrix(x)),x)
        np.testing.assert_equal(points(matrix(x),[[1,0,0]]),[[3,5,5]])

    def test_triangle_interior_edge_and_vertex(self):
        triangles=np.array([[[0.,0,0],[1,0,0],[0,1,0]]])
        for p,b,d in (([.2,.3,2],[.5,.2,.3],2),([2,0,0],[0,1,0],1),([.5,-1,0],[.5,.5,0],1)):
            i,bary,dist=closest_surface(p,triangles)
            self.assertEqual(i,0);np.testing.assert_allclose(bary,b);self.assertAlmostEqual(dist,d)

    def test_digit_affine_fits_rest_curl_without_moving_input(self):
        source=np.array([[x,y,z] for x in (1.,2.,3.) for y in (-.2,.2) for z in (-.3,.3)])
        target=source.copy();target[:,0]=source[:,0]*.7+1;target[:,1]=source[:,1]*.5+source[:,0]*.3;target[:,2]=source[:,2]*.8-1
        original=source.copy();tr=fit_lobe(source,target)
        np.testing.assert_allclose(points(tr,source),target,atol=1e-10)
        np.testing.assert_equal(source,original)

    def test_palette_pruning_preserves_negative_nonhand_weights(self):
        hand='bone_L-hand.mesh';forearm='bone_L-foreArm.mesh'
        w=[{hand:.41,forearm:-.01,'bone_L-index01.mesh':.6},{hand:.41,forearm:-.01,'bone_L-index02.mesh':.6},{hand:.41,forearm:-.01,'bone_L-middlefinger01.mesh':.6}]
        change={'source':{'name':'test.mesh','faces':[[0,1,2]]},'side':'L','weights':w}
        chunks,_=partition(change)
        self.assertLessEqual(len(chunks[0]['palette']),4)
        for vertex in w:
            self.assertEqual(vertex[forearm],-.01);self.assertAlmostEqual(sum(vertex.values()),1)

    def test_palette_pruning_welds_duplicate_uv_seam_vertices(self):
        hand='bone_L-hand.mesh';arm='bone_L-foreArm.mesh'
        w=[{hand:.41,arm:-.01,'bone_L-index01.mesh':.6},
           {hand:.41,arm:-.01,'bone_L-index02.mesh':.6},
           {hand:.41,arm:-.01,'bone_L-middlefinger01.mesh':.6}]
        w.extend([dict(w[0]),{hand:1.},{hand:1.}])
        c={'source':{'name':'uv.mesh','faces':[[0,1,2],[3,4,5]]},'side':'L','weights':w,'weld_keys':[0,1,2,0,4,5]}
        partition(c)
        self.assertEqual(w[0],w[3])

    def test_surface_transfer_cannot_jump_to_adjacent_digit(self):
        p=np.array([[.2,.2,0.],[.3,.2,0.],[.2,.3,0.]])
        class Target:
            def hand_meshes(self,*args):yield {'faces':[[0,1,2]]},p,np.ones(3)
        names=['bone_L-hand.mesh','bone_L-index01.mesh','bone_L-ringfinger01.mesh']
        tri=np.array([[[0,0,0],[1,0,0],[0,1,0]],[[0,0,.1],[1,0,.1],[0,1,.1]]],float)
        ws=np.zeros((2,3,3));ws[0,:,1]=1;ws[1,:,2]=1
        cache,_,_,_=anatomical_surface(Target(),'L',names,tri,ws,{'ringfinger':p[:2],'index':np.empty((0,3))})
        self.assertEqual(cache[tuple(p[0])][1],0)
        self.assertGreater(cache[tuple(p[0])][2],.5)
        # A soft proximal transition is intentional, not a hard palm boundary.
        self.assertGreater(cache[tuple(p[2])][0],0)
        self.assertGreater(cache[tuple(p[2])][2],0)

if __name__=='__main__':unittest.main()
