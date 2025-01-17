//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2006 Ben Ryves 2006
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// mus2mid.c - Ben Ryves 2006 - http://benryves.com - benryves@benryves.com
// Use to convert a MUS file into a single track, type 0 MIDI file.

#include <stdio.h>

#include "fastmath.h"
#include "doomtype.h"

#include "mus2mid.h"

#include "options.h"

#define NUM_CHANNELS 16

#define MIDI_PERCUSSION_CHAN 9
#define MUS_PERCUSSION_CHAN 15

// MUS event codes
typedef enum
{
    mus_releasekey = 0x00,
    mus_presskey = 0x10,
    mus_pitchwheel = 0x20,
    mus_systemevent = 0x30,
    mus_changecontroller = 0x40,
    mus_scoreend = 0x60
} musevent;

// MIDI event codes
typedef enum
{
    midi_releasekey = 0x80,
    midi_presskey = 0x90,
    midi_aftertouchkey = 0xA0,
    midi_changecontroller = 0xB0,
    midi_changepatch = 0xC0,
    midi_aftertouchchannel = 0xD0,
    midi_pitchwheel = 0xE0
} midievent;

// Structure to hold MUS file header
typedef struct
{
    byte id[4];
    unsigned short scorelength;
    unsigned short scorestart;
    unsigned short primarychannels;
    unsigned short secondarychannels;
    unsigned short instrumentcount;
} musheader;

// Standard MIDI type 0 header + track header
static const byte midiheader[] =
    {
        'M', 'T', 'h', 'd',     // Main header
        0x00, 0x00, 0x00, 0x06, // Header size
        0x00, 0x00,             // MIDI type (0)
        0x00, 0x01,             // Number of tracks
        0x00, 0x46,             // Resolution
        'M', 'T', 'r', 'k',     // Start of track
        0x00, 0x00, 0x00, 0x00  // Placeholder for track length
};

// Cached channel velocities
static byte channelvelocities[] =
    {
        127, 127, 127, 127, 127, 127, 127, 127,
        127, 127, 127, 127, 127, 127, 127, 127};

// Timestamps between sequences of MUS events

static unsigned int queuedtime = 0;

// Counter for the length of the track

static unsigned int tracksize;

static const byte controller_map[] =
    {
        0x00, 0x20, 0x01, 0x07, 0x0A, 0x0B, 0x5B, 0x5D,
        0x40, 0x43, 0x78, 0x7B, 0x7E, 0x7F, 0x79};

static int channel_map[NUM_CHANNELS];

// Write timestamp to a MIDI file.

static byte WriteTime(unsigned int time, FILE *midioutput)
{
    unsigned int buffer = time & 0x7F;
    byte writeval;

    while ((time >>= 7) != 0)
    {
        buffer <<= 8;
        buffer |= ((time & 0x7F) | 0x80);
    }

    for (;;)
    {
        writeval = (byte)(buffer & 0xFF);

        if (fwrite(&writeval, 1, 1, midioutput) != 1)
        {
            return 1;
        }

        ++tracksize;

        if ((buffer & 0x80) != 0)
        {
            buffer >>= 8;
        }
        else
        {
            queuedtime = 0;
            return 0;
        }
    }
}

// Write the end of track marker
static byte WriteEndTrack(FILE *midioutput)
{
    byte endtrack[] = {0xFF, 0x2F, 0x00};

    if (WriteTime(queuedtime, midioutput))
    {
        return 1;
    }

    if (fwrite(endtrack, 1, 3, midioutput) != 3)
    {
        return 1;
    }

    tracksize += 3;
    return 0;
}

// Write a key press event
static byte WritePressKey(byte channel, byte key, byte velocity, FILE *midioutput)
{
    byte working = midi_presskey | channel;

    if (WriteTime(queuedtime, midioutput))
    {
        return 1;
    }

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    working = key & 0x7F;

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    working = velocity & 0x7F;

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    tracksize += 3;

    return 0;
}

// Write a key release event
static byte WriteReleaseKey(byte channel, byte key,
                               FILE *midioutput)
{
    byte working = midi_releasekey | channel;

    if (WriteTime(queuedtime, midioutput))
    {
        return 1;
    }

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    working = key & 0x7F;

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    working = 0;

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    tracksize += 3;

    return 0;
}

// Write a pitch wheel/bend event
static byte WritePitchWheel(byte channel, short wheel,
                               FILE *midioutput)
{
    byte working = midi_pitchwheel | channel;

    if (WriteTime(queuedtime, midioutput))
    {
        return 1;
    }

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    working = wheel & 0x7F;

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    working = (wheel >> 7) & 0x7F;

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    tracksize += 3;
    return 0;
}

// Write a patch change event
static byte WriteChangePatch(byte channel, byte patch,
                                FILE *midioutput)
{
    byte working = midi_changepatch | channel;

    if (WriteTime(queuedtime, midioutput))
    {
        return 1;
    }

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    working = patch & 0x7F;

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    tracksize += 2;

    return 0;
}

// Write a valued controller change event

static byte WriteChangeController_Valued(byte channel,
                                            byte control,
                                            byte value,
                                            FILE *midioutput)
{
    byte working = midi_changecontroller | channel;

    if (WriteTime(queuedtime, midioutput))
    {
        return 1;
    }

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    working = control & 0x7F;

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    // Quirk in vanilla DOOM? MUS controller values should be
    // 7-bit, not 8-bit.

    working = value; // & 0x7F;

    // Fix on said quirk to stop MIDI players from complaining that
    // the value is out of range:

    if (working & 0x80)
    {
        working = 0x7F;
    }

    if (fwrite(&working, 1, 1, midioutput) != 1)
    {
        return 1;
    }

    tracksize += 3;

    return 0;
}

// Write a valueless controller change event
static byte WriteChangeController_Valueless(byte channel,
                                               byte control,
                                               FILE *midioutput)
{
    return WriteChangeController_Valued(channel, control, 0,
                                        midioutput);
}

// Allocate a free MIDI channel.

static int AllocateMIDIChannel(void)
{
    int result;
    int max;
    int i;

    // Find the current highest-allocated channel.

    max = -1;

    for (i = 0; i < NUM_CHANNELS; ++i)
    {
        if (channel_map[i] > max)
        {
            max = channel_map[i];
        }
    }

    // max is now equal to the highest-allocated MIDI channel.  We can
    // now allocate the next available channel.  This also works if
    // no channels are currently allocated (max=-1)

    result = max + 1;

    // Don't allocate the MIDI percussion channel!

    if (result == MIDI_PERCUSSION_CHAN)
    {
        ++result;
    }

    return result;
}

// Given a MUS channel number, get the MIDI channel number to use
// in the outputted file.

static int GetMIDIChannel(int mus_channel, FILE *midioutput)
{
    // Find the MIDI channel to use for this MUS channel.
    // MUS channel 15 is the percusssion channel.

    if (mus_channel == MUS_PERCUSSION_CHAN)
    {
        return MIDI_PERCUSSION_CHAN;
    }
    else
    {
        // If a MIDI channel hasn't been allocated for this MUS channel
        // yet, allocate the next free MIDI channel.

        if (channel_map[mus_channel] == -1)
        {
            channel_map[mus_channel] = AllocateMIDIChannel();

            // First time using the channel, send an "all notes off"
            // event. This fixes "The D_DDTBLU disease" described here:
            // http://www.doomworld.com/vb/source-ports/66802-the
            WriteChangeController_Valueless(channel_map[mus_channel], 0x7b,
                                            midioutput);
        }

        return channel_map[mus_channel];
    }
}

static int ReadMusHeader(FILE *file, musheader *header)
{
    int result;

    result = fread(&header->id, sizeof(byte), 4, file) == 4 && fread(&header->scorelength, sizeof(short), 1, file) == 1 && fread(&header->scorestart, sizeof(short), 1, file) == 1 && fread(&header->primarychannels, sizeof(short), 1, file) == 1 && fread(&header->secondarychannels, sizeof(short), 1, file) == 1 && fread(&header->instrumentcount, sizeof(short), 1, file) == 1;

    return result;
}

// Read a MUS file from a stream (musinput) and output a MIDI file to
// a stream (midioutput).
//
// Returns 0 on success or 1 on failure.

byte mus2mid(FILE *musinput, FILE *midioutput)
{
    // Header for the MUS file
    musheader musfileheader;

    // Descriptor for the current MUS event
    byte eventdescriptor;
    int channel; // Channel number
    musevent event;

    // Bunch of vars read from MUS lump
    byte key;
    byte controllernumber;
    byte controllervalue;

    // Buffer used for MIDI track size record
    byte tracksizebuffer[4];

    // Flag for when the score end marker is hit.
    int hitscoreend = 0;

    // Temp working byte
    byte working;
    // Used in building up time delays
    unsigned int timedelay;

    // Initialise channel map to mark all channels as unused.
    SetDWords(channel_map, -1, NUM_CHANNELS);

    // Grab the header

    if (!ReadMusHeader(musinput, &musfileheader))
    {
        return 1;
    }

#ifdef CHECK_MUS_HEADER
    // Check MUS header
    if (musfileheader.id[0] != 'M' || musfileheader.id[1] != 'U' || musfileheader.id[2] != 'S' || musfileheader.id[3] != 0x1A)
    {
        return 1;
    }
#endif

    // Seek to where the data is held
    if (fseek(musinput, (long)musfileheader.scorestart,
              SEEK_SET) != 0)
    {
        return 1;
    }

    // So, we can assume the MUS file is faintly legit. Let's start
    // writing MIDI data...

    fwrite(midiheader, 1, sizeof(midiheader), midioutput);
    tracksize = 0;

    // Now, process the MUS file:
    while (!hitscoreend)
    {
        // Handle a block of events:

        while (!hitscoreend)
        {
            // Fetch channel number and event code:

            if (fread(&eventdescriptor, 1, 1, musinput) != 1)
            {
                return 1;
            }

            channel = GetMIDIChannel(eventdescriptor & 0x0F, midioutput);
            event = eventdescriptor & 0x70;

            switch (event)
            {
            case mus_releasekey:
                if (fread(&key, 1, 1, musinput) != 1)
                {
                    return 1;
                }

                if (WriteReleaseKey(channel, key, midioutput))
                {
                    return 1;
                }

                break;

            case mus_presskey:
                if (fread(&key, 1, 1, musinput) != 1)
                {
                    return 1;
                }

                if (key & 0x80)
                {
                    if (fread(&channelvelocities[channel], 1, 1, musinput) != 1)
                    {
                        return 1;
                    }

                    channelvelocities[channel] &= 0x7F;
                }

                if (WritePressKey(channel, key,
                                  channelvelocities[channel], midioutput))
                {
                    return 1;
                }

                break;

            case mus_pitchwheel:
                if (fread(&key, 1, 1, musinput) != 1)
                {
                    break;
                }
                if (WritePitchWheel(channel, (short)(key * 64), midioutput))
                {
                    return 1;
                }

                break;

            case mus_systemevent:
                if (fread(&controllernumber, 1, 1, musinput) != 1)
                {
                    return 1;
                }
                if (controllernumber < 10 || controllernumber > 14)
                {
                    return 1;
                }

                if (WriteChangeController_Valueless(channel,
                                                    controller_map[controllernumber],
                                                    midioutput))
                {
                    return 1;
                }

                break;

            case mus_changecontroller:
                if (fread(&controllernumber, 1, 1, musinput) != 1)
                {
                    return 1;
                }

                if (fread(&controllervalue, 1, 1, musinput) != 1)
                {
                    return 1;
                }

                if (controllernumber == 0)
                {
                    if (WriteChangePatch(channel, controllervalue,
                                         midioutput))
                    {
                        return 1;
                    }
                }
                else
                {
                    if (controllernumber < 1 || controllernumber > 9)
                    {
                        return 1;
                    }

                    if (WriteChangeController_Valued(channel,
                                                     controller_map[controllernumber],
                                                     controllervalue,
                                                     midioutput))
                    {
                        return 1;
                    }
                }

                break;

            case mus_scoreend:
                hitscoreend = 1;
                break;

            default:
                return 1;
                break;
            }

            if (eventdescriptor & 0x80)
            {
                break;
            }
        }
        // Now we need to read the time code:
        if (!hitscoreend)
        {
            timedelay = 0;
            for (;;)
            {
                if (fread(&working, 1, 1, musinput) != 1)
                {
                    return 1;
                }

                timedelay = timedelay * 128 + (working & 0x7F);
                if ((working & 0x80) == 0)
                {
                    break;
                }
            }
            queuedtime += timedelay;
        }
    }

    // End of track
    if (WriteEndTrack(midioutput))
    {
        return 1;
    }

    // Write the track size into the stream
    if (fseek(midioutput, 18, SEEK_SET))
    {
        return 1;
    }

    tracksizebuffer[0] = (tracksize >> 24) & 0xff;
    tracksizebuffer[1] = (tracksize >> 16) & 0xff;
    tracksizebuffer[2] = (tracksize >> 8) & 0xff;
    tracksizebuffer[3] = tracksize & 0xff;

    if (fwrite(tracksizebuffer, 1, 4, midioutput) != 4)
    {
        return 1;
    }

    return 0;
}
