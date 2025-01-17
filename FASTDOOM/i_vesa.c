#include "i_vesa.h"
#include <stdio.h>
#include <malloc.h>
#include <conio.h>
#include <string.h>
#include "options.h"
#include "r_defs.h"
#include "i_ibm.h"
#include "m_menu.h"
#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"

/*-----------------05-14-97 05:19pm-----------------
 *
 *     SUBMiSSiVES VESA VBE 2.0 Application Core
 *     -----------------------------------------
 *
 * tested and works well under Dos4G, PmodeW and Win95
 *
 * This code also works for buggy VBE's and finds
 * the mode even if the modelist is placed in dos-memory.
 *
 * * Bugfixes:     Now also supports a fix for S3 chips.
 * * Changes:	    Removed Matrox Mystique Support since noone used it..
 * * Bugfixes:	    Now a more clever BPP detection (since some bioses sucks)
 *                 Enhanced FindMode function (now terminates on 0)
 *
 * --------------------------------------------------*/

/*
 * Structure to do the Realmode Interrupt Calls.
 */

#if defined(MODE_VBE2) || defined(MODE_VBE2_DIRECT)

#pragma pack(1)

#define VBE2SIGNATURE "VBE2"
#define VBE3SIGNATURE "VBE3"

/*-----------------07-26-97 11:04am-----------------
 * Realmode Callback Structure!
 * --------------------------------------------------*/

static struct rminfo
{
  unsigned long EDI;
  unsigned long ESI;
  unsigned long EBP;
  unsigned long reserved_by_system;
  unsigned long EBX;
  unsigned long EDX;
  unsigned long ECX;
  unsigned long EAX;
  unsigned short flags;
  unsigned short ES, DS, FS, GS, IP, CS, SP, SS;
} RMI;

/*
 * Structures that hold the preallocated DOS-Memory Aeras
 * and their translated near-pointers!
 */

static struct DPMI_PTR VbeInfoPool = {0, 0};
static struct DPMI_PTR VbeModePool = {0, 0};
static struct VBE_VbeInfoBlock *VBE_Controller_Info_Pointer;
static struct VBE_ModeInfoBlock *VBE_ModeInfo_Pointer;
static void *LastPhysicalMapping = NULL;
/*
 * Structures, that hold the Informations needed to invoke
 * PM-Interrupts
 */

static union REGS regs;
static struct SREGS sregs;

/*
 * Some informations 'bout the last mode which was set
 * These informations are required to compensate some differencies
 * between the normal and direct PM calling methods
 */

static signed short vbelastbank = -1;
static unsigned long BytesPerScanline;
static unsigned char BitsPerPixel;

/*
 *  This function pointers will be initialized after you called
 *  VBE_Init. It'll be set to the realmode or protected mode call
 *  code
 */

static void PrepareRegisters(void)
{
  memset(&RMI, 0, sizeof(RMI));
  memset(&sregs, 0, sizeof(sregs));
  memset(&regs, 0, sizeof(regs));
}

static void RMIRQ10()
{
  memset(&regs, 0, sizeof(regs));
  regs.w.ax = 0x0300; // Simulate Real-Mode interrupt
  regs.h.bl = 0x10;
  sregs.es = FP_SEG(&RMI);
  regs.x.edi = FP_OFF(&RMI);
  int386x(0x31, &regs, &regs, &sregs);
}

void DPMI_AllocDOSMem(short int paras, struct DPMI_PTR *p)
{
  /* DPMI call 100h allocates DOS memory */
  PrepareRegisters();
  regs.w.ax = 0x0100;
  regs.w.bx = paras;
  int386x(0x31, &regs, &regs, &sregs);
  p->segment = regs.w.ax;
  p->selector = regs.w.dx;
}

void DPMI_FreeDOSMem(struct DPMI_PTR *p)
{
  /* DPMI call 101h free DOS memory */
  memset(&sregs, 0, sizeof(sregs));
  regs.w.ax = 0x0101;
  regs.w.dx = p->selector;
  int386x(0x31, &regs, &regs, &sregs);
}

void DPMI_UNMAP_PHYSICAL(void *p)
{
  /* DPMI call 800h map physical memory*/
  PrepareRegisters();
  regs.w.ax = 0x0801;
  regs.w.bx = (unsigned short)(((unsigned long)p) >> 16);
  regs.w.cx = (unsigned short)(((unsigned long)p) & 0xffff);
  int386x(0x31, &regs, &regs, &sregs);
}

void *DPMI_MAP_PHYSICAL(void *p, unsigned long size)
{
  /* DPMI call 800h map physical memory*/
  PrepareRegisters();
  regs.w.ax = 0x0800;
  regs.w.bx = (unsigned short)(((unsigned long)p) >> 16);
  regs.w.cx = (unsigned short)(((unsigned long)p) & 0xffff);
  regs.w.si = (unsigned short)(((unsigned long)size) >> 16);
  regs.w.di = (unsigned short)(((unsigned long)size) & 0xffff);
  int386x(0x31, &regs, &regs, &sregs);
  return (void *)((regs.w.bx << 16) + regs.w.cx);
}

void VBE_Controller_Information(struct VBE_VbeInfoBlock *a)
{
  memcpy(a, VBE_Controller_Info_Pointer, sizeof(struct VBE_VbeInfoBlock));
}

void VBE_Mode_Information(short Mode, struct VBE_ModeInfoBlock *a)
{
  PrepareRegisters();
  RMI.EAX = 0x00004f01; // Get SVGA-Mode Information
  RMI.ECX = Mode;
  RMI.ES = VbeModePool.segment; // Segment of realmode data
  RMI.EDI = 0;                  // offset of realmode data
  RMIRQ10();
  memcpy(a, VBE_ModeInfo_Pointer, sizeof(struct VBE_ModeInfoBlock));
}

int VBE_IsModeLinear(short Mode)
{
  struct VBE_ModeInfoBlock a;
  if (Mode == 0x13)
  {
    return 1;
  }
#ifdef DISABLE_LFB
  return 0;
#else
  VBE_Mode_Information(Mode, &a);
  return ((a.ModeAttributes & 128) == 128);
#endif
}

void VBE_SetDisplayStart(short x, short y)
{
  PrepareRegisters();
  RMI.EAX = 0x00004f07;
  RMI.EBX = 0;
  RMI.ECX = x;
  RMI.EDX = y;
  RMIRQ10();
}

void VBE_SetDisplayStart_Y(short y)
{
  PrepareRegisters();
  RMI.EAX = 0x00004f07;
  RMI.EBX = 0;
  RMI.ECX = 0;
  RMI.EDX = y;
  RMIRQ10();
}

void setbiosmode(unsigned short c);
#pragma aux setbiosmode = "int 0x10" parm[ax] modify[eax ebx ecx edx esi edi];

void VBE_SetMode(short Mode, int linear, int clear)
{
  struct VBE_ModeInfoBlock a;
  int rawmode;
  /* Is it a normal textmode? if so set it directly! */
  if (Mode == 3)
  {
    setbiosmode(Mode);
    return;
  }
  if (Mode == 0x13)
  {
    setbiosmode(Mode);
    return;
  }
  rawmode = Mode & 0x0fff;
  if (linear)
    rawmode |= 1 << 14;
  if (!clear)
    rawmode |= 1 << 15;

  /* Ordinary ModeSwitch call without S3Fix */
  PrepareRegisters();
  RMI.EAX = 0x00004f02;
  RMI.EBX = rawmode;
  RMIRQ10();

  // get the current mode-info block and set some parameters...
  VBE_Mode_Information(Mode, &a);
  BytesPerScanline = a.BytesPerScanline;
  BitsPerPixel = a.BitsPerPixel;
}

char *VBE_GetVideoPtr(short mode)
{
  void *phys;
  struct VBE_ModeInfoBlock ModeInfo;
  if (mode == 13)
    return (char *)0xa0000;
  VBE_Mode_Information(mode, &ModeInfo);
  /* Unmap the last physical mapping (if there is one...) */
  if (LastPhysicalMapping)
  {
    DPMI_UNMAP_PHYSICAL(LastPhysicalMapping);
    LastPhysicalMapping = NULL;
  }
  LastPhysicalMapping = DPMI_MAP_PHYSICAL((void *)ModeInfo.PhysBasePtr,
                                          (long)(VBE_Controller_Info_Pointer->TotalMemory) * 64 * 1024);
  return (char *)LastPhysicalMapping;
}

void VBE_SetDACWidth(char bits)
{
  PrepareRegisters();
  RMI.EAX = 0x00004f08;
  RMI.EBX = bits << 8;
  RMIRQ10();
}

void VBE_Init(void)
{
  FILE *f;

  /* Allocate the Dos Memory for Mode and Controller Information Blocks */
  /* and translate their pointers into flat memory space                */
  DPMI_AllocDOSMem(512 / 16, &VbeInfoPool);
  DPMI_AllocDOSMem(256 / 16, &VbeModePool);
  VBE_ModeInfo_Pointer = (struct VBE_ModeInfoBlock *)(VbeModePool.segment * 16);
  VBE_Controller_Info_Pointer = (struct VBE_VbeInfoBlock *)(VbeInfoPool.segment * 16);

  /* Get Controller Information Block only once and copy this block on  */
  /* all requests                                                       */
  memset(VBE_Controller_Info_Pointer, 0, sizeof(struct VBE_VbeInfoBlock));
  memcpy(VBE_Controller_Info_Pointer->vbeSignature, VBE2SIGNATURE, 4);
  PrepareRegisters();
  RMI.EAX = 0x00004f00;         // Get SVGA-Information
  RMI.ES = VbeInfoPool.segment; // Segment of realmode data
  RMI.EDI = 0;                  // offset of realmode data
  RMIRQ10();
  // Translate the Realmode Pointers into flat-memory address space
  VBE_Controller_Info_Pointer->OemStringPtr = (char *)((((unsigned long)VBE_Controller_Info_Pointer->OemStringPtr >> 16) << 4) + (unsigned short)VBE_Controller_Info_Pointer->OemStringPtr);
  VBE_Controller_Info_Pointer->VideoModePtr = (unsigned short *)((((unsigned long)VBE_Controller_Info_Pointer->VideoModePtr >> 16) << 4) + (unsigned short)VBE_Controller_Info_Pointer->VideoModePtr);
  VBE_Controller_Info_Pointer->OemVendorNamePtr = (char *)((((unsigned long)VBE_Controller_Info_Pointer->OemVendorNamePtr >> 16) << 4) + (unsigned short)VBE_Controller_Info_Pointer->OemVendorNamePtr);
  VBE_Controller_Info_Pointer->OemProductNamePtr = (char *)((((unsigned long)VBE_Controller_Info_Pointer->OemProductNamePtr >> 16) << 4) + (unsigned short)VBE_Controller_Info_Pointer->OemProductNamePtr);
  VBE_Controller_Info_Pointer->OemProductRevPtr = (char *)((((unsigned long)VBE_Controller_Info_Pointer->OemProductRevPtr >> 16) << 4) + (unsigned short)VBE_Controller_Info_Pointer->OemProductRevPtr);
}

void VBE_Done(void)
{
  if (LastPhysicalMapping)
  {
    DPMI_UNMAP_PHYSICAL(LastPhysicalMapping);
  }
  DPMI_FreeDOSMem(&VbeModePool);
  DPMI_FreeDOSMem(&VbeInfoPool);
}

static struct VBE_VbeInfoBlock vbeinfo;
static struct VBE_ModeInfoBlock vbemode;
unsigned short vesavideomode = 0xFFFF;
int vesalinear = -1;
char *vesavideoptr;

void VBE2_InitGraphics(void)
{
  int mode;

  VBE_Init();

  // Get VBE info
  VBE_Controller_Information(&vbeinfo);

  // Get VBE modes
  for (mode = 0; vbeinfo.VideoModePtr[mode] != 0xffff; mode++)
  {
    VBE_Mode_Information(vbeinfo.VideoModePtr[mode], &vbemode);
    if (vbemode.XResolution == 320 && vbemode.YResolution == 200 && vbemode.BitsPerPixel == 8)
    {
      vesavideomode = vbeinfo.VideoModePtr[mode];
      vesalinear = VBE_IsModeLinear(vesavideomode);
      break;
    }
  }

  // If a VESA compatible 320x200 8bpp mode is found, use it!
  if (vesavideomode != 0xFFFF)
  {
    VBE_SetMode(vesavideomode, vesalinear, 1);

    if (vesalinear == 1)
    {
      pcscreen = destscreen = VBE_GetVideoPtr(vesavideomode);
    }
    else
    {
      pcscreen = destscreen = (char *)0xA0000;
    }

    // Force 6 bits resolution per color
    VBE_SetDACWidth(6);
  }
  else
  {
    I_Error("Compatible VESA 2.0 video mode not found! (320x200 8bpp required)");
  }
}

#if defined(MODE_VBE2)
#define SBARHEIGHT 32

void I_FinishUpdate(void)
{
  if (updatestate & I_FULLSCRN)
  {
    CopyDWords(backbuffer, pcscreen, SCREENHEIGHT * SCREENWIDTH / 4);
    updatestate = I_NOUPDATE; // clear out all draw types
  }
  if (updatestate & I_FULLVIEW)
  {
    if (updatestate & I_MESSAGES && screenblocks > 7)
    {
      int i;
      for (i = 0; i < endscreen; i += SCREENWIDTH)
      {
        CopyDWords(backbuffer + i, pcscreen + i, SCREENWIDTH / 4);
      }
      updatestate &= ~(I_FULLVIEW | I_MESSAGES);
    }
    else
    {
      int i;
      for (i = startscreen; i < endscreen; i += SCREENWIDTH)
      {
        CopyDWords(backbuffer + i, pcscreen + i, SCREENWIDTH / 4);
      }
      updatestate &= ~I_FULLVIEW;
    }
  }
  if (updatestate & I_STATBAR)
  {
    CopyDWords(backbuffer + SCREENWIDTH * (SCREENHEIGHT - SBARHEIGHT), pcscreen + SCREENWIDTH * (SCREENHEIGHT - SBARHEIGHT), SCREENWIDTH * SBARHEIGHT / 4);
    updatestate &= ~I_STATBAR;
  }
  if (updatestate & I_MESSAGES)
  {
    CopyDWords(backbuffer, pcscreen, (SCREENWIDTH * 28) / 4);
    updatestate &= ~I_MESSAGES;
  }
}
#endif

#if defined(MODE_VBE2_DIRECT)
short page = 0;

void I_FinishUpdate(void)
{
  VBE_SetDisplayStart_Y(page);

  if (page == 400)
  {
    page = 0;
    destscreen -= 2 * 320 * 200;
  }
  else
  {
    page += 200;
    destscreen += 320 * 200;
  }
}
#endif

#endif
