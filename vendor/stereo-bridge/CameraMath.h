#pragma once
#include "SharedPair.h"
#include <cmath>
#include <cstring>
#include <algorithm>
namespace CameraMath {
using Transport::Quaternion;using Transport::Vector;using Transport::Pose;
struct alignas(16) Matrix {float m[16]{};};
inline Matrix Load(const void* data){Matrix result;memcpy(result.m,data,64);return result;}
inline void Store(void* data,const Matrix& m){memcpy(data,m.m,64);}
inline Matrix Identity(){Matrix m;for(int i=0;i<4;i++)m.m[i*5]=1;return m;}
inline Matrix Multiply(const Matrix& a,const Matrix& b){Matrix r;for(int i=0;i<4;i++)for(int j=0;j<4;j++){double v=0;for(int k=0;k<4;k++)v+=double(a.m[i*4+k])*b.m[k*4+j];r.m[i*4+j]=float(v);}return r;}
inline Quaternion Inverse(Quaternion q){return {-q.x,-q.y,-q.z,q.w};}
inline Quaternion Multiply(Quaternion a,Quaternion b){return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
inline Vector Rotate(Quaternion q,Vector v){auto r=Multiply(Multiply(q,{v.x,v.y,v.z,0}),Inverse(q));return {r.x,r.y,r.z};}
inline Pose LevelReference(Pose pose){
    const auto forward=Rotate(pose.orientation,{0,0,-1});
    float yaw;
    if(forward.x*forward.x+forward.z*forward.z>1e-6f)yaw=std::atan2(-forward.x,-forward.z);
    else {const auto right=Rotate(pose.orientation,{1,0,0});yaw=std::atan2(-right.z,right.x);}
    pose.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};return pose;
}
inline Matrix Rotation(Quaternion q){Matrix m=Identity();Vector axes[]={{1,0,0},{0,1,0},{0,0,1}};for(int i=0;i<3;i++){auto v=Rotate(q,axes[i]);m.m[i*4]=v.x;m.m[i*4+1]=v.y;m.m[i*4+2]=v.z;}return m;}
inline Matrix InverseRigid(const Matrix& m){Matrix r=Identity();for(int i=0;i<3;i++)for(int j=0;j<3;j++)r.m[i*4+j]=m.m[j*4+i];for(int j=0;j<3;j++)for(int k=0;k<3;k++)r.m[12+j]-=m.m[12+k]*r.m[k*4+j];return r;}
inline bool Inverse(const Matrix& m,Matrix& result){
    double a[4][8]{};for(int i=0;i<4;i++)for(int j=0;j<4;j++){a[i][j]=m.m[i*4+j];a[i][j+4]=i==j;}
    for(int i=0;i<4;i++){
        int pivot=i;for(int j=i+1;j<4;j++)if(std::abs(a[j][i])>std::abs(a[pivot][i]))pivot=j;
        if(!std::isfinite(a[pivot][i])||std::abs(a[pivot][i])<1e-12)return false;
        for(int k=0;k<8;k++)std::swap(a[i][k],a[pivot][k]);
        double d=a[i][i];for(int k=0;k<8;k++)a[i][k]/=d;
        for(int j=0;j<4;j++)if(j!=i){double f=a[j][i];for(int k=0;k<8;k++)a[j][k]-=f*a[i][k];}
    }
    for(int i=0;i<4;i++)for(int j=0;j<4;j++){result.m[i*4+j]=float(a[i][j+4]);if(!std::isfinite(result.m[i*4+j]))return false;}return true;
}
inline bool Rigid(const Matrix& m){
    for(float f:m.m)if(!std::isfinite(f))return false;
    if(std::abs(m.m[3])+std::abs(m.m[7])+std::abs(m.m[11])+std::abs(m.m[15]-1)>.01f)return false;
    for(int i=0;i<3;i++)for(int j=i;j<3;j++){float dot=0;for(int k=0;k<3;k++)dot+=m.m[i*4+k]*m.m[j*4+k];if(std::abs(dot-(i==j?1.f:0.f))>.01f)return false;}return true;
}
// Thief's scene view maps world coordinates to right/up/forward camera space.
// Reflect OpenXR's right/up/back coordinates through Z before composition.
inline Matrix Relative(const Pose& reference,const Pose& pose,float units){
    auto q=Multiply(Inverse(reference.orientation),pose.orientation);
    auto p=Rotate(Inverse(reference.orientation),{pose.position.x-reference.position.x,pose.position.y-reference.position.y,pose.position.z-reference.position.z});
    auto m=Rotation({-q.x,-q.y,q.z,q.w});m.m[12]=p.x*units;m.m[13]=p.y*units;m.m[14]=-p.z*units;return m;
}
inline Matrix Frustum(const Matrix& depth,const Transport::Eye& eye){
    float l=std::tan(eye.left),r=std::tan(eye.right),u=std::tan(eye.up),d=std::tan(eye.down);
    Matrix p;p.m[0]=2/(r-l);p.m[5]=2/(u-d);p.m[8]=-(r+l)/(r-l);p.m[9]=-(u+d)/(u-d);p.m[10]=depth.m[10];p.m[11]=1;p.m[14]=depth.m[14];return p;
}
// Preserve a shared head-centre view space for native lighting/culling data;
// include each runtime eye's full relative pose in its rendering projection.
inline Matrix CenteredEyeProjection(const Matrix& depth,const Pose& head,const Transport::Eye& eye,float units){
    return Multiply(InverseRigid(Relative(head,eye.pose,units)),Frustum(depth,eye));
}
// Normalized logical screen coordinates -> a head-locked 16:9 panel -> eye clip.
// Eye origins and asymmetric/canted lenses are retained, even for flat text.
inline Matrix ScreenPanelProjection(const Pose& head,const Transport::Eye& eye,float width,float distance){
    Matrix panel=Identity();panel.m[0]=width*.5f;panel.m[5]=width*.5f*9.f/16.f;
    panel.m[14]=distance;
    Matrix depth{};depth.m[10]=0;depth.m[14]=.059f;
    return Multiply(panel,CenteredEyeProjection(depth,head,eye,1.f));
}
struct EyeMatrices {Matrix projection,inverseProjection,view,translatedView,translatedVP,inverseTranslatedVP,viewProjection,inverseVP;Vector origin;};
inline bool BuildEye(const Matrix& gameView,const Matrix& depth,const Pose& reference,const Transport::Eye& eye,float units,EyeMatrices& result){
    if(!Rigid(gameView)||std::abs(depth.m[11]-1)>.001f||std::abs(depth.m[15])>.001f)return false;
    auto world=Multiply(Relative(reference,eye.pose,units),InverseRigid(gameView));
    result.origin={world.m[12],world.m[13],world.m[14]};
    result.view=InverseRigid(world);result.projection=Frustum(depth,eye);
    result.translatedView=result.view;result.translatedView.m[12]=result.translatedView.m[13]=result.translatedView.m[14]=0;
    result.translatedVP=Multiply(result.translatedView,result.projection);
    result.viewProjection=Multiply(result.view,result.projection);
    if(!Inverse(result.projection,result.inverseProjection)||!Inverse(result.translatedVP,result.inverseTranslatedVP))return false;
    // Invert the factors rather than the ill-conditioned world VP at distant coordinates.
    result.inverseVP=Multiply(result.inverseProjection,world);return true;
}
}
