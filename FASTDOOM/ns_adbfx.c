#include <stdlib.h>
#include <dos.h>
#include <conio.h>
#include "ns_dpmi.h"
#include "ns_task.h"
#include "ns_cards.h"
#include "ns_user.h"
#include "ns_adbfx.h"
#include "ns_sb.h"
#include "ns_sbdef.h"

#include "ns_sbmus.h"

#include "m_misc.h"

#include "doomstat.h"

static int ADBFX_Installed = 0;

static char *ADBFX_BufferStart;
static char *ADBFX_BufferEnd;
static char *ADBFX_CurrentBuffer;
static int ADBFX_BufferNum = 0;
static int ADBFX_NumBuffers = 0;
static int ADBFX_TotalBufferSize = 0;
static int ADBFX_TransferLength = 0;
static int ADBFX_CurrentLength = 0;

static char *ADBFX_SoundPtr;
volatile int ADBFX_SoundPlaying;

static task *ADBFX_Timer;

void (*ADBFX_CallBack)(void);

/*---------------------------------------------------------------------
   Function: ADBFX_ServiceInterrupt

   Handles interrupt generated by sound card at the end of a voice
   transfer.  Calls the user supplied callback function.
---------------------------------------------------------------------*/

static void ADBFX_ServiceInterrupt(task *Task)
{
    unsigned short valueComp = 255 - ((unsigned short)*ADBFX_SoundPtr + 128);
    unsigned char value = (unsigned char) valueComp >> 2;


    AL_SendOutputToPort(ADLIB_PORT, 0x40, value);

    ADBFX_SoundPtr++;

    ADBFX_CurrentLength--;
    if (ADBFX_CurrentLength == 0)
    {
        // Keep track of current buffer
        ADBFX_CurrentBuffer += ADBFX_TransferLength;
        ADBFX_BufferNum++;
        if (ADBFX_BufferNum >= ADBFX_NumBuffers)
        {
            ADBFX_BufferNum = 0;
            ADBFX_CurrentBuffer = ADBFX_BufferStart;
        }

        ADBFX_CurrentLength = ADBFX_TransferLength;
        ADBFX_SoundPtr = ADBFX_CurrentBuffer;

        // Call the caller's callback function
        if (ADBFX_CallBack != NULL)
        {
            ADBFX_CallBack();
        }
    }
}

/*---------------------------------------------------------------------
   Function: ADBFX_StopPlayback

   Ends the transfer of digitized sound to the Sound Source.
---------------------------------------------------------------------*/

void ADBFX_StopPlayback(void)
{
    if (ADBFX_SoundPlaying)
    {
        TS_Terminate(ADBFX_Timer);
        ADBFX_SoundPlaying = 0;
        ADBFX_BufferStart = NULL;

        AL_Reset();
    }
}

/*---------------------------------------------------------------------
   Function: ADBFX_BeginBufferedPlayback

   Begins multibuffered playback of digitized sound on the Sound Source.
---------------------------------------------------------------------*/

int ADBFX_BeginBufferedPlayback(
    char *BufferStart,
    int BufferSize,
    int NumDivisions,
    void (*CallBackFunc)(void))
{
    if (ADBFX_SoundPlaying)
    {
        ADBFX_StopPlayback();
    }

    ADBFX_SetCallBack(CallBackFunc);

    ADBFX_BufferStart = BufferStart;
    ADBFX_CurrentBuffer = BufferStart;
    ADBFX_SoundPtr = BufferStart;
    ADBFX_TotalBufferSize = BufferSize;
    ADBFX_BufferEnd = BufferStart + BufferSize;
    // VITI95: OPTIMIZE
    ADBFX_TransferLength = BufferSize / NumDivisions;
    ADBFX_CurrentLength = ADBFX_TransferLength;
    ADBFX_BufferNum = 0;
    ADBFX_NumBuffers = NumDivisions;

    ADBFX_SoundPlaying = 1;

    ADBFX_Timer = TS_ScheduleTask(ADBFX_ServiceInterrupt, ADBFX_SampleRate, 1, NULL);
    TS_Dispatch();

    return (ADBFX_Ok);
}

/*---------------------------------------------------------------------
   Function: ADBFX_SetCallBack

   Specifies the user function to call at the end of a sound transfer.
---------------------------------------------------------------------*/

void ADBFX_SetCallBack(void (*func)(void))
{
    ADBFX_CallBack = func;
}

/*---------------------------------------------------------------------
   Function: ADBFX_Init

   Initializes the Sound Source prepares the module to play digitized
   sounds.
---------------------------------------------------------------------*/

int ADBFX_Init(int soundcard)
{
    int status, i;

    if (ADBFX_Installed)
    {
        ADBFX_Shutdown();
    }

    AL_Reset();

    AL_SendOutputToPort(ADLIB_PORT, 0x20, 0x21);
    AL_SendOutputToPort(ADLIB_PORT, 0x60, 0xF0);
    AL_SendOutputToPort(ADLIB_PORT, 0x80, 0xF0);	
    AL_SendOutputToPort(ADLIB_PORT, 0xC0, 0x01);
    AL_SendOutputToPort(ADLIB_PORT, 0xE0, 0x00);
    AL_SendOutputToPort(ADLIB_PORT, 0x43, 0x3F);
    AL_SendOutputToPort(ADLIB_PORT, 0xB0, 0x01);
    AL_SendOutputToPort(ADLIB_PORT, 0xA0, 0x8F);
    AL_SendOutputToPort(ADLIB_PORT, 0xB0, 0x2E);

   /* Wait */
   for (i = 0; i < 256; i++)
   {
      inp(ADLIB_PORT);
   }

    AL_SendOutputToPort(ADLIB_PORT, 0xB0, 0x20);
    AL_SendOutputToPort(ADLIB_PORT, 0xA0, 0x00);

    status = ADBFX_Ok;

    ADBFX_SoundPlaying = 0;

    ADBFX_SetCallBack(NULL);

    ADBFX_BufferStart = NULL;

    ADBFX_Installed = 1;

    return (status);
}

int ADBFX_SetMixMode(int mode)
{
    mode = MONO_8BIT;
    return (mode);
}

/*---------------------------------------------------------------------
   Function: ADBFX_Shutdown

   Ends transfer of sound data to the Sound Source.
---------------------------------------------------------------------*/

void ADBFX_Shutdown(void)
{
    ADBFX_StopPlayback();

    ADBFX_SoundPlaying = 0;

    ADBFX_BufferStart = NULL;

    ADBFX_SetCallBack(NULL);

    ADBFX_Installed = 0;
}
