/*
** d_protocol.cpp
** Basic network packet creation routines and simple IFF parsing
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/


#include "d_protocol.h"
#include "network/net.h"
#include "doomstat.h"
#include "cmdlib.h"
#include "serializer.h"


char *ReadString (uint8_t **stream)
{
	char *string = *((char **)stream);

	*stream += strlen (string) + 1;
	return copystring (string);
}

const char *ReadStringConst(uint8_t **stream)
{
	const char *string = *((const char **)stream);
	*stream += strlen (string) + 1;
	return string;
}

int ReadByte (uint8_t **stream)
{
	uint8_t v = **stream;
	*stream += 1;
	return v;
}

int ReadWord (uint8_t **stream)
{
	short v = (((*stream)[0]) << 8) | (((*stream)[1]));
	*stream += 2;
	return v;
}

int ReadLong (uint8_t **stream)
{
	int v = (((*stream)[0]) << 24) | (((*stream)[1]) << 16) | (((*stream)[2]) << 8) | (((*stream)[3]));
	*stream += 4;
	return v;
}

float ReadFloat (uint8_t **stream)
{
	union
	{
		int i;
		float f;
	} fakeint;
	fakeint.i = ReadLong (stream);
	return fakeint.f;
}

void WriteString (const char *string, uint8_t **stream)
{
	char *p = *((char **)stream);

	while (*string) {
		*p++ = *string++;
	}

	*p++ = 0;
	*stream = (uint8_t *)p;
}


void WriteByte (uint8_t v, uint8_t **stream)
{
	**stream = v;
	*stream += 1;
}

void WriteWord (short v, uint8_t **stream)
{
	(*stream)[0] = v >> 8;
	(*stream)[1] = v & 255;
	*stream += 2;
}

void WriteLong (int v, uint8_t **stream)
{
	(*stream)[0] = v >> 24;
	(*stream)[1] = (v >> 16) & 255;
	(*stream)[2] = (v >> 8) & 255;
	(*stream)[3] = v & 255;
	*stream += 4;
}

void WriteFloat (float v, uint8_t **stream)
{
	union
	{
		int i;
		float f;
	} fakeint;
	fakeint.f = v;
	WriteLong (fakeint.i, stream);
}

// Returns the number of bytes read
int UnpackUserCmd (usercmd_t *ucmd, const usercmd_t *basis, uint8_t **stream)
{
	uint8_t *start = *stream;
	uint8_t flags;

	if (basis != NULL)
	{
		if (basis != ucmd)
		{
			memcpy (ucmd, basis, sizeof(usercmd_t));
		}
	}
	else
	{
		memset (ucmd, 0, sizeof(usercmd_t));
	}

	flags = ReadByte (stream);

	if (flags)
	{
		// We can support up to 29 buttons, using from 0 to 4 bytes to store them.
		if (flags & UCMDF_BUTTONS)
		{
			uint32_t buttons = ucmd->buttons;
			uint8_t in = ReadByte(stream);

			buttons = (buttons & ~0x7F) | (in & 0x7F);
			if (in & 0x80)
			{
				in = ReadByte(stream);
				buttons = (buttons & ~(0x7F << 7)) | ((in & 0x7F) << 7);
				if (in & 0x80)
				{
					in = ReadByte(stream);
					buttons = (buttons & ~(0x7F << 14)) | ((in & 0x7F) << 14);
					if (in & 0x80)
					{
						in = ReadByte(stream);
						buttons = (buttons & ~(0xFF << 21)) | (in << 21);
					}
				}
			}
			ucmd->buttons = buttons;
		}
		if (flags & UCMDF_PITCH)
			ucmd->pitch = ReadWord (stream);
		if (flags & UCMDF_YAW)
			ucmd->yaw = ReadWord (stream);
		if (flags & UCMDF_FORWARDMOVE)
			ucmd->forwardmove = ReadWord (stream);
		if (flags & UCMDF_SIDEMOVE)
			ucmd->sidemove = ReadWord (stream);
		if (flags & UCMDF_UPMOVE)
			ucmd->upmove = ReadWord (stream);
		if (flags & UCMDF_ROLL)
			ucmd->roll = ReadWord (stream);
	}

	return int(*stream - start);
}

// Returns the number of bytes written
int PackUserCmd (const usercmd_t *ucmd, const usercmd_t *basis, uint8_t **stream)
{
	uint8_t flags = 0;
	uint8_t *temp = *stream;
	uint8_t *start = *stream;
	usercmd_t blank;
	uint32_t buttons_changed;

	if (basis == NULL)
	{
		memset (&blank, 0, sizeof(blank));
		basis = &blank;
	}

	WriteByte (0, stream);			// Make room for the packing bits

	buttons_changed = ucmd->buttons ^ basis->buttons;
	if (buttons_changed != 0)
	{
		uint8_t bytes[4] = {  uint8_t(ucmd->buttons        & 0x7F),
						  uint8_t((ucmd->buttons >> 7)  & 0x7F),
						  uint8_t((ucmd->buttons >> 14) & 0x7F),
						  uint8_t((ucmd->buttons >> 21) & 0xFF) };

		flags |= UCMDF_BUTTONS;

		if (buttons_changed & 0xFFFFFF80)
		{
			bytes[0] |= 0x80;
			if (buttons_changed & 0xFFFFC000)
			{
				bytes[1] |= 0x80;
				if (buttons_changed & 0xFFE00000)
				{
					bytes[2] |= 0x80;
				}
			}
		}
		WriteByte (bytes[0], stream);
		if (bytes[0] & 0x80)
		{
			WriteByte (bytes[1], stream);
			if (bytes[1] & 0x80)
			{
				WriteByte (bytes[2], stream);
				if (bytes[2] & 0x80)
				{
					WriteByte (bytes[3], stream);
				}
			}
		}
	}
	if (ucmd->pitch != basis->pitch)
	{
		flags |= UCMDF_PITCH;
		WriteWord (ucmd->pitch, stream);
	}
	if (ucmd->yaw != basis->yaw)
	{
		flags |= UCMDF_YAW;
		WriteWord (ucmd->yaw, stream);
	}
	if (ucmd->forwardmove != basis->forwardmove)
	{
		flags |= UCMDF_FORWARDMOVE;
		WriteWord (ucmd->forwardmove, stream);
	}
	if (ucmd->sidemove != basis->sidemove)
	{
		flags |= UCMDF_SIDEMOVE;
		WriteWord (ucmd->sidemove, stream);
	}
	if (ucmd->upmove != basis->upmove)
	{
		flags |= UCMDF_UPMOVE;
		WriteWord (ucmd->upmove, stream);
	}
	if (ucmd->roll != basis->roll)
	{
		flags |= UCMDF_ROLL;
		WriteWord (ucmd->roll, stream);
	}

	// Write the packing bits
	WriteByte (flags, &temp);

	return int(*stream - start);
}

FSerializer &Serialize(FSerializer &arc, const char *key, ticcmd_t &cmd, ticcmd_t *def)
{
	if (arc.BeginObject(key))
	{
		arc("consistency", cmd.consistency)
			("ucmd", cmd.ucmd)
			.EndObject();
	}
	return arc;
}

FSerializer &Serialize(FSerializer &arc, const char *key, usercmd_t &cmd, usercmd_t *def)
{
	// This used packed data with the old serializer but that's totally counterproductive when
	// having a text format that is human-readable. So this compression has been undone here.
	// The few bytes of file size it saves are not worth the obfuscation.

	if (arc.BeginObject(key))
	{
		arc("buttons", cmd.buttons)
			("pitch", cmd.pitch)
			("yaw", cmd.yaw)
			("roll", cmd.roll)
			("forwardmove", cmd.forwardmove)
			("sidemove", cmd.sidemove)
			("upmove", cmd.upmove)
			.EndObject();
	}
	return arc;
}

int WriteUserCmdMessage (usercmd_t *ucmd, const usercmd_t *basis, uint8_t **stream)
{
	if (basis == NULL)
	{
		if (ucmd->buttons != 0 ||
			ucmd->pitch != 0 ||
			ucmd->yaw != 0 ||
			ucmd->forwardmove != 0 ||
			ucmd->sidemove != 0 ||
			ucmd->upmove != 0 ||
			ucmd->roll != 0)
		{
			WriteByte (DEM_USERCMD, stream);
			return PackUserCmd (ucmd, basis, stream) + 1;
		}
	}
	else
	if (ucmd->buttons != basis->buttons ||
		ucmd->pitch != basis->pitch ||
		ucmd->yaw != basis->yaw ||
		ucmd->forwardmove != basis->forwardmove ||
		ucmd->sidemove != basis->sidemove ||
		ucmd->upmove != basis->upmove ||
		ucmd->roll != basis->roll)
	{
		WriteByte (DEM_USERCMD, stream);
		return PackUserCmd (ucmd, basis, stream) + 1;
	}

	WriteByte (DEM_EMPTYUSERCMD, stream);
	return 1;
}

#if 0
int SkipTicCmd (uint8_t **stream, int count)
{
	int i, skip;
	uint8_t *flow = *stream;

	for (i = count; i > 0; i--)
	{
		bool moreticdata = true;

		flow += 2;		// Skip consistency marker
		while (moreticdata)
		{
			uint8_t type = *flow++;

			if (type == DEM_USERCMD)
			{
				moreticdata = false;
				skip = 1;
				if (*flow & UCMDF_PITCH)		skip += 2;
				if (*flow & UCMDF_YAW)			skip += 2;
				if (*flow & UCMDF_FORWARDMOVE)	skip += 2;
				if (*flow & UCMDF_SIDEMOVE)		skip += 2;
				if (*flow & UCMDF_UPMOVE)		skip += 2;
				if (*flow & UCMDF_ROLL)			skip += 2;
				if (*flow & UCMDF_BUTTONS)
				{
					if (*++flow & 0x80)
					{
						if (*++flow & 0x80)
						{
							if (*++flow & 0x80)
							{
								++flow;
							}
						}
					}
				}
				flow += skip;
			}
			else if (type == DEM_EMPTYUSERCMD)
			{
				moreticdata = false;
			}
			else
			{
				Net_SkipCommand (type, &flow);
			}
		}
	}

	skip = int(flow - *stream);
	*stream = flow;

	return skip;
}
#endif

uint8_t *lenspot;

// Write the header of an IFF chunk and leave space
// for the length field.
void StartChunk (int id, uint8_t **stream)
{
	WriteLong (id, stream);
	lenspot = *stream;
	*stream += 4;
}

// Write the length field for the chunk and insert
// pad byte if the chunk is odd-sized.
void FinishChunk (uint8_t **stream)
{
	int len;
	
	if (!lenspot)
		return;

	len = int(*stream - lenspot - 4);
	WriteLong (len, &lenspot);
	if (len & 1)
		WriteByte (0, stream);

	lenspot = NULL;
}

// Skip past an unknown chunk. *stream should be
// pointing to the chunk's length field.
void SkipChunk (uint8_t **stream)
{
	int len;

	len = ReadLong (stream);
	*stream += len + (len & 1);
}
