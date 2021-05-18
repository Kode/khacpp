#ifndef HX_GC_HELPERS_INCLUDED
#define HX_GC_HELPERS_INCLUDED

#if defined(HXCPP_ARM64)
#define HXCPP_CAPTURE_ARM64
#elif defined(ANDROID)
#define HXCPP_CAPTURE_ARMV7
#endif

#ifdef HXCPP_CAPTURE_SETJMP
   #include <setjmp.h>
#else

   #if (defined(HX_WINDOWS) || defined(HX_MACOS)) && !defined(HXCPP_M64)
      #define HXCPP_CAPTURE_x86
   #endif

   #if (defined(HX_MACOS) || (defined(HX_WINDOWS) && !defined(HX_WINRT)) || defined(_XBOX_ONE) || defined(KORE_PS4) || defined(KORE_PS5) || defined(KORE_XBOX_ONE) || defined(KORE_XBOX_SCARLETT)) && defined(HXCPP_M64)
      #define HXCPP_CAPTURE_x64
   #endif

#endif


namespace hx
{

// Capture Registers
//
#ifdef HXCPP_CAPTURE_SETJMP // {

typedef jmp_buf RegisterCaptureBuffer;

#define CAPTURE_REGS \
   setjmp(mRegisterBuf);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(&mRegisterBuf+1)

#elif defined(HXCPP_CAPTURE_ARM64)


struct RegisterCaptureBuffer
{
  void *x0;
   void *x1;
   void *x2;
   void *x3;
   void *x4;
   void *x5;
   void *x6;
   void *x7;
   void *x8;
   void *x9;
   void *x10;
   void *x11;
   void *x12;
   void *x13;
   void *x14;
   void *x15;
   void *x16;
   void *x17;
   void *x18;
   void *x19;
   void *x20;
   void *x21;
   void *x22;
   void *x23;
   void *x24;
   void *x25;
   void *x26;
   void *x27;
   void *x28;
   void *x29;
};

void CaptureARM64(RegisterCaptureBuffer &outBuffer);

#define CAPTURE_REGS \
   hx::CaptureARM64(mRegisterBuf);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(&mRegisterBuf+1)


#elif defined(HXCPP_CAPTURE_ARMV7)


struct RegisterCaptureBuffer
{
  void *r0;
   void *r1;
   void *r2;
   void *r3;
   void *r4;
   void *r5;
   void *r6;
   void *r7;
   void *r8;
   void *r9;
   void *r10;
   void *r11;
   void *r12;
   void *r13;
   void *r14;
};

void CaptureARMV7(RegisterCaptureBuffer &outBuffer);

#define CAPTURE_REGS \
   hx::CaptureARMV7(mRegisterBuf);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(&mRegisterBuf+1)

#elif defined(HXCPP_CAPTURE_x86) // } {

struct RegisterCaptureBuffer
{
   void *eax;
   void *ebx;
   void *ecx;
   void *edi;
   void *esi;
};

void CaptureX86(RegisterCaptureBuffer &outBuffer);

#define CAPTURE_REGS \
   hx::CaptureX86(mRegisterBuf);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(&mRegisterBuf+1)

#elif defined(HXCPP_CAPTURE_x64) // }  {


struct RegisterCaptureBuffer
{
   void *rbx;
   void *rbp;
   void *rdi;
   void *r12;
   void *r13;
   void *r14;
   void *r15;

   void *xmm[16*2];
};

void CaptureX64(RegisterCaptureBuffer &outBuffer);

#define CAPTURE_REGS \
   hx::CaptureX64(mRegisterBuf);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(&mRegisterBuf+1)

#else 

class RegisterCapture
{
public:
	virtual int Capture(int *inTopOfStack,int **inBuf,int &outSize,int inMaxSize,int *inDummy);
   static RegisterCapture *Instance();
};

typedef int *RegisterCaptureBuffer[20];

#define CAPTURE_REGS \
   hx::RegisterCapture::Instance()->Capture(mTopOfStack, \
                mRegisterBuf,mRegisterBufSize,20,mBottomOfStack); \

#define CAPTURE_REG_START (int *)mRegisterBuf
#define CAPTURE_REG_END (int *)(mRegisterBuf+mRegisterBufSize)

#endif // }



}


#endif
