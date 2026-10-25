#pragma once
namespace DlssModes {
// Keep persisted quality IDs stable. The removed native-AA mode used ID 1.
enum Mode {Off=0,Quality=2,Balanced=3,Performance=4,UltraPerformance=5};
inline constexpr Mode Modes[]={Off,Quality,Balanced,Performance,UltraPerformance};
inline constexpr unsigned Count=5;
inline Mode Valid(int value){switch(value){case 1:case Quality:return Quality;case Balanced:return Balanced;case Performance:return Performance;case UltraPerformance:return UltraPerformance;default:return Off;}}
inline Mode Configured(bool enabled,int value){auto mode=Valid(value);return enabled?(mode==Off?Quality:mode):Off;}
inline Mode Next(Mode mode){switch(Valid(mode)){case Off:return Quality;case Quality:return Balanced;case Balanced:return Performance;case Performance:return UltraPerformance;default:return Off;}}
inline const char* Name(Mode mode){switch(Valid(mode)){case Quality:return "DLSS Quality";case Balanced:return "DLSS Balanced";case Performance:return "DLSS Performance";case UltraPerformance:return "DLSS Ultra Performance";default:return "Off";}}
}
