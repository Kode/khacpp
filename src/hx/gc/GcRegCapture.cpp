#include "GcRegCapture.h"

#ifdef HXCPP_CAPTURE_SETJMP // {

// Nothing

#elif defined(HXCPP_CAPTURE_ARM64)

#pragma optimize( "", off )

namespace hx {

void CaptureARM64(RegisterCaptureBuffer &outBuffer)
{

   void *regX0;
   void *regX1;
   void *regX2;
   void *regX3;
   void *regX4;
   void *regX5;
   void *regX6;
   void *regX7;
   void *regX8;
   void *regX9;
   void *regX10;
   void *regX11;
   void *regX12;
   void *regX13;
   void *regX14;
   void *regX15;
   void *regX16;
   void *regX17;
   void *regX18;
   void *regX19;
   void *regX20;
   void *regX21;
   void *regX22;
   void *regX23;
   void *regX24;
   void *regX25;
   void *regX26;
   void *regX27;
   void *regX28;
   void *regX29;
asm ("mov %0, x0\n\t" : "=r" (regX0) );
asm ("mov %0, x1\n\t" : "=r" (regX1) );
asm ("mov %0, x2\n\t" : "=r" (regX2) );
asm ("mov %0, x3\n\t" : "=r" (regX3) );
asm ("mov %0, x4\n\t" : "=r" (regX4) );
asm ("mov %0, x5\n\t" : "=r" (regX5) );
asm ("mov %0, x6\n\t" : "=r" (regX6) );
asm ("mov %0, x7\n\t" : "=r" (regX7) );
asm ("mov %0, x8\n\t" : "=r" (regX8) );
asm ("mov %0, x9\n\t" : "=r" (regX9) );
asm ("mov %0, x10\n\t" : "=r" (regX10) );
asm ("mov %0, x11\n\t" : "=r" (regX11) );
asm ("mov %0, x12\n\t" : "=r" (regX12) );
asm ("mov %0, x13\n\t" : "=r" (regX13) );
asm ("mov %0, x14\n\t" : "=r" (regX14) );
asm ("mov %0, x15\n\t" : "=r" (regX15) );
asm ("mov %0, x16\n\t" : "=r" (regX16) );
asm ("mov %0, x17\n\t" : "=r" (regX17) );
asm ("mov %0, x18\n\t" : "=r" (regX18) );
asm ("mov %0, x19\n\t" : "=r" (regX19) );
asm ("mov %0, x20\n\t" : "=r" (regX20) );
asm ("mov %0, x21\n\t" : "=r" (regX21) );
asm ("mov %0, x22\n\t" : "=r" (regX22) );
asm ("mov %0, x23\n\t" : "=r" (regX23) );
asm ("mov %0, x24\n\t" : "=r" (regX24) );
asm ("mov %0, x25\n\t" : "=r" (regX25) );
asm ("mov %0, x26\n\t" : "=r" (regX26) );
asm ("mov %0, x27\n\t" : "=r" (regX27) );
asm ("mov %0, x28\n\t" : "=r" (regX28) );
asm ("mov %0, x29\n\t" : "=r" (regX29) );
  outBuffer.x0 = regX0;
  outBuffer.x1 = regX1;
  outBuffer.x2 = regX2;
  outBuffer.x3 = regX3;
  outBuffer.x4 = regX4;
  outBuffer.x5 = regX5;
  outBuffer.x6 = regX6;
  outBuffer.x7 = regX7;
  outBuffer.x8 = regX8;
  outBuffer.x9 = regX9;
  outBuffer.x10 = regX10;
  outBuffer.x11 = regX11;
  outBuffer.x12 = regX12;
  outBuffer.x13 = regX13;
  outBuffer.x14 = regX14;
  outBuffer.x15 = regX15;
  outBuffer.x16 = regX16;
  outBuffer.x17 = regX17;
  outBuffer.x18 = regX18;
  outBuffer.x19 = regX19;
  outBuffer.x20 = regX20;
  outBuffer.x21 = regX21;
  outBuffer.x22 = regX22;
  outBuffer.x23 = regX23;
  outBuffer.x24 = regX24;
  outBuffer.x25 = regX25;
  outBuffer.x26 = regX26;
  outBuffer.x27 = regX27;
  outBuffer.x28 = regX28;
  outBuffer.x29 = regX29;
}

} // end namespace hx


#elif defined(HXCPP_CAPTURE_ARMV7)

#pragma optimize( "", off )

namespace hx {

void CaptureARMV7(RegisterCaptureBuffer &outBuffer)
{

   void *regR0;
   void *regR1;
   void *regR2;
   void *regR3;
   void *regR4;
   void *regR5;
   void *regR6;
   void *regR7;
   void *regR8;
   void *regR9;
   void *regR10;
   void *regR11;
   void *regR12;
   void *regR13;
   void *regR14;
asm ("mov %0, r0\n\t" : "=r" (regR0) );
asm ("mov %0, r1\n\t" : "=r" (regR1) );
asm ("mov %0, r2\n\t" : "=r" (regR2) );
asm ("mov %0, r3\n\t" : "=r" (regR3) );
asm ("mov %0, r4\n\t" : "=r" (regR4) );
asm ("mov %0, r5\n\t" : "=r" (regR5) );
asm ("mov %0, r6\n\t" : "=r" (regR6) );
asm ("mov %0, r7\n\t" : "=r" (regR7) );
asm ("mov %0, r8\n\t" : "=r" (regR8) );
asm ("mov %0, r9\n\t" : "=r" (regR9) );
asm ("mov %0, r10\n\t" : "=r" (regR10) );
asm ("mov %0, r11\n\t" : "=r" (regR11) );
asm ("mov %0, r12\n\t" : "=r" (regR12) );
asm ("mov %0, r13\n\t" : "=r" (regR13) );
asm ("mov %0, r14\n\t" : "=r" (regR14) );
  outBuffer.r0 = regR0;
  outBuffer.r1 = regR1;
  outBuffer.r2 = regR2;
  outBuffer.r3 = regR3;
  outBuffer.r4 = regR4;
  outBuffer.r5 = regR5;
  outBuffer.r6 = regR6;
  outBuffer.r7 = regR7;
  outBuffer.r8 = regR8;
  outBuffer.r9 = regR9;
  outBuffer.r10 = regR10;
  outBuffer.r11 = regR11;
  outBuffer.r12 = regR12;
  outBuffer.r13 = regR13;
  outBuffer.r14 = regR14;
}

} // end namespace hx


#elif defined(HXCPP_CAPTURE_x86) // }  {

#pragma optimize( "", off )


namespace hx {

void CaptureX86(RegisterCaptureBuffer &outBuffer)
{
   void *regEsi;
   void *regEdi;
   void *regEbx;
   void *regEax;
   void *regEcx;
   #ifdef __GNUC__
   asm ("mov %%esi, %0\n\t" : "=r" (regEsi) );
   asm ("mov %%edi, %0\n\t" : "=r" (regEdi) );
   asm ("mov %%ebx, %0\n\t" : "=r" (regEbx) );
   asm ("mov %%eax, %0\n\t" : "=r" (regEax) );
   asm ("mov %%ecx, %0\n\t" : "=r" (regEcx) );
   #else
   __asm {
      mov regEsi, esi
      mov regEdi, edi
      mov regEbx, ebx
      mov regEax, eax
      mov regEcx, ecx
   }
   #endif
   outBuffer.esi = regEsi;
   outBuffer.edi = regEdi;
   outBuffer.ebx = regEbx;
   outBuffer.eax = regEax;
   outBuffer.ecx = regEcx;
}

} // end namespace hx

#elif defined(HXCPP_CAPTURE_x64) // } {

#if !defined(__GNUC__)
#include <windows.h>
#endif

namespace hx {

void CaptureX64(RegisterCaptureBuffer &outBuffer)
{
   #if !defined(__GNUC__)
      CONTEXT context;

      context.ContextFlags = CONTEXT_INTEGER;
      RtlCaptureContext(&context);

      outBuffer.rbx = (void *)context.Rbx;
      outBuffer.rbp = (void *)context.Rbp;
      outBuffer.rdi = (void *)context.Rdi;
      outBuffer.r12 = (void *)context.R12;
      outBuffer.r13 = (void *)context.R13;
      outBuffer.r14 = (void *)context.R14;
      outBuffer.r15 = (void *)context.R15;
      memcpy(outBuffer.xmm, &context.Xmm0, sizeof(outBuffer.xmm));
   #else
      void *regBx;
      void *regBp;
      void *regDi;
      void *reg12;
      void *reg13;
      void *reg14;
      void *reg15;
      asm ("movq %%rbx, %0\n\t" : "=r" (regBx) );
      asm ("movq %%rbp, %0\n\t" : "=r" (regBp) );
      asm ("movq %%rdi, %0\n\t" : "=r" (regDi) );
      asm ("movq %%r12, %0\n\t" : "=r" (reg12) );
      asm ("movq %%r13, %0\n\t" : "=r" (reg13) );
      asm ("movq %%r14, %0\n\t" : "=r" (reg14) );
      asm ("movq %%r15, %0\n\t" : "=r" (reg15) );
      outBuffer.rbx = regBx;
      outBuffer.rbp = regBp;
      outBuffer.r12 = reg12;
      outBuffer.r13 = reg13;
      outBuffer.r14 = reg14;
      outBuffer.r15 = reg15;
   #endif
}

} // end namespace hx


#else // }  {

#include <string.h>

namespace hx {

// Put this function here so we can be reasonablly sure that "this" register and
// the 4 registers that may be used to pass args are on the stack.
int RegisterCapture::Capture(int *inTopOfStack,int **inBuf,int &outSize,int inMaxSize, int *inBottom)
{
	int size = ( (char *)inTopOfStack - (char *)inBottom )/sizeof(void *);
	if (size>inMaxSize)
		size = inMaxSize;
	outSize = size;
	if (size>0)
	   memcpy(inBuf,inBottom,size*sizeof(void*));
	return 1;
}


RegisterCapture *gRegisterCaptureInstance = 0;
RegisterCapture *RegisterCapture::Instance()
{
	if (!gRegisterCaptureInstance)
		gRegisterCaptureInstance = new RegisterCapture();
	return gRegisterCaptureInstance;
}

} // end namespace hx

#endif // }

