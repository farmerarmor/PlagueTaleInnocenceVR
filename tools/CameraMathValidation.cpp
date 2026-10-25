#include "CameraMath.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace CameraMath;
static int checks{};
static void require(bool value,const char* name){++checks;if(!value){fprintf(stderr,"FAIL: %s\n",name);exit(1);}}
static bool closeTo(float a,float b,float epsilon=0.0001f){return std::abs(a-b)<epsilon;}
static void identity(const Matrix& m,const char* name){for(int i=0;i<16;++i)require(closeTo(m.m[i],(i%5==0)?1.f:0.f,0.001f),name);}
static float ndc(const Matrix& p,float x,float y,float z,int axis){return (x*p.m[axis]+y*p.m[4+axis]+z*p.m[8+axis]+p.m[12+axis])/z;}
int main(){
 // Asymmetric runtime frustum and the game's observed reverse-Z depth terms.
 Matrix depth{};depth.m[10]=-0.0000147502187f;depth.m[11]=1;depth.m[14]=0.0590008758f;
 Transport::Eye eye{{{0,0,0,1},{0,0,0}},std::atan(-.964925885f),std::atan(.715263844f),std::atan(.889498115f),std::atan(-1.11092532f)};
 auto p=Frustum(depth,eye);const float distance=2.f;
 require(closeTo(ndc(p,std::tan(eye.left)*distance,0,distance,0),-1),"left frustum boundary");
 require(closeTo(ndc(p,std::tan(eye.right)*distance,0,distance,0),1),"right frustum boundary");
 require(closeTo(ndc(p,0,std::tan(eye.up)*distance,distance,1),1),"upper frustum boundary");
 require(closeTo(ndc(p,0,std::tan(eye.down)*distance,distance,1),-1),"lower frustum boundary");
 require(closeTo(ndc(p,0,0,.059f,2),1,.0001f),"native reversed near depth");
 require(closeTo(ndc(p,0,0,4000.f,2),0,.000001f),"native reversed far depth");
 Pose reference{{0,0,0,1},{0,0,0}};EyeMatrices left{},right{};auto game=Identity();
 game.m[12]=-403.9024f;game.m[13]=-19.98073f;game.m[14]=191.67589f;
 eye.pose.position.x=-.03037f;require(BuildEye(game,depth,reference,eye,1,left),"left eye accepted");
 eye.pose.position.x=.03037f;require(BuildEye(game,depth,reference,eye,1,right),"right eye accepted");
 require(closeTo(right.origin.x-left.origin.x,.06074f,.00005f),"physical IPD in game units");
 require(closeTo(left.origin.y,19.98073f),"base camera height");
 identity(Multiply(left.projection,left.inverseProjection),"projection inverse round trip");
 identity(Multiply(left.view,InverseRigid(left.view)),"world view round trip");
 identity(Multiply(left.translatedVP,left.inverseTranslatedVP),"originless VP round trip");
 // A reflected native world basis must preserve length and eye order.
 game.m[0]=-1;game.m[12]=403.9024f;eye.pose.position.x=-.03037f;
 require(BuildEye(game,depth,reference,eye,1,left),"reflected game basis accepted");
 eye.pose.position.x=.03037f;require(BuildEye(game,depth,reference,eye,1,right),"reflected right eye accepted");
 require(closeTo(left.origin.x-right.origin.x,.06074f,.00005f),"reflected basis eye order");
 Pose leaned{{0,0,0,1},{.2f,.1f,-.3f}};auto relative=Relative(reference,leaned,1);
 require(closeTo(relative.m[12],.2f)&&closeTo(relative.m[13],.1f)&&closeTo(relative.m[14],.3f),"XR right/up/back to native right/up/forward");
 const float angle=.4f;Pose yawed{{0,std::sin(angle/2),0,std::cos(angle/2)},{0,0,0}};
 identity(Relative(LevelReference(yawed),yawed,1),"recenter removes yaw");
 // The native stereo representation must render the same per-eye clip
 // coordinates while shared shadow/light transforms retain head-view space.
 auto headWorld=Multiply(Relative(reference,yawed,1),InverseRigid(game));
 auto headView=InverseRigid(headWorld);
 eye.pose=yawed;eye.pose.position=Rotate(yawed.orientation,{-.03037f,0,0});
 require(BuildEye(game,depth,reference,eye,1,left),"rotated explicit eye accepted");
 auto centered=CenteredEyeProjection(depth,yawed,eye,1);
 auto a=Multiply(headView,centered),b=Multiply(left.view,left.projection);
 for(int i=0;i<16;++i)require(closeTo(a.m[i],b.m[i],.0001f),"centred native projection equals explicit eye camera");
 Matrix inverseCentered;require(Inverse(centered,inverseCentered),"centred asymmetric eye inverse accepted");
 identity(Multiply(centered,inverseCentered),"centred eye projection inverse round trip");
 // UI must project into each eye as one physical 16:9 panel, independently
 // of the texture aspect. Unproject all corners through asymmetric lenses.
 for(int e=0;e<2;++e){
  Transport::Eye lens=eye;lens.pose=yawed;lens.pose.position=Rotate(yawed.orientation,{e?.03037f:-.03037f,0,0});
  if(e){lens.left=std::atan(-.715263844f);lens.right=std::atan(.964925885f);}
  const auto panel=ScreenPanelProjection(yawed,lens,2.4f,2.f);
  Matrix panelDepth{};panelDepth.m[14]=.059f;
  const auto eyeP=CenteredEyeProjection(panelDepth,yawed,lens,1.f);Matrix eyeInv{};
  require(Inverse(eyeP,eyeInv),"UI eye projection inverse");
  for(float x:{-1.f,0.f,1.f})for(float y:{-1.f,0.f,1.f}){
   float clip[4]{},actual[4]{};float input[4]={x,y,0,1};
   for(int j=0;j<4;++j)for(int k=0;k<4;++k)clip[j]+=input[k]*panel.m[k*4+j];
   for(int j=0;j<4;++j)for(int k=0;k<4;++k)actual[j]+=clip[k]*eyeInv.m[k*4+j];
   require(closeTo(actual[0]/actual[3],x*1.2f) && closeTo(actual[1]/actual[3],y*.675f) && closeTo(actual[2]/actual[3],2.f),"UI binocular panel corner agreement");
  }
 }
 auto invalid=game;invalid.m[0]=std::numeric_limits<float>::quiet_NaN();require(!BuildEye(invalid,depth,reference,eye,1,left),"nonfinite native view rejected");
 invalid=depth;invalid.m[11]=-1;require(!BuildEye(game,invalid,reference,eye,1,left),"unsupported depth convention rejected");
 Transport::Header header{};Transport::TrackingReader reader{};Transport::Tracking sample{};
 header.tracking.id=1;header.tracking.tick=1000;header.tracking.valid=1;
 require(reader.Read(&header,sample,1001),"fresh tracking accepted");header.trackingLock=1;
 require(reader.Read(&header,sample,1020)&&sample.id==1,"contended mailbox preserves fresh pose");
 require(!reader.Read(&header,sample,1250),"stale contended pose rejected");header.trackingLock=0;header.tracking.valid=0;
 require(!reader.Read(&header,sample,1001),"tracking loss rejected");
 printf("PASS: %d camera, asymmetric projection, eye order and tracking checks. Physical scale and visuals still require headset validation.\n",checks);return 0;
}
