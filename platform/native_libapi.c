/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/psx/LIBAPI.C
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#include <macros.h>

#include <psx/libapi.h>

#include "ctrds.h"

// CTR's retail timer reads root counter 1 once per VSync and converts units
// through divisor 0x147e before MainFrame_GameLogic scales to elapsedTimeMS.
// Native owns VBlank emission, so advance RCNT1 from emitted VBlanks instead
// of SDL wall time. Host wait jitter otherwise leaks into vehicle physics.
#define CTR_NATIVE_RCNT1_TICKS_PER_VBLANK 263u

global_variable u64 s_rootCounterValue = 0;
global_variable u64 s_rootCounterBase = 0;
global_variable unsigned s_rootCounterRemainder = 0;

void NativeRCnt_EmitVBlank(void)
{
	// NOTE(ctrds): the game's whole clock is this counter, so ticks-per-VBlank
	// has to shrink by exactly the factor the VBlank rate grew by. Otherwise a
	// doubled VBlank rate doubles the game's sense of time and everything
	// delta-timed -- physics, timers, cameras -- runs at double speed.
	// 263/2 is not an integer, so carry the remainder rather than truncating:
	// truncation alone would lose 0.5 ticks per VBlank and drift the clock slow.
	const unsigned mult = (unsigned)Ctrds_VBlankMultiplier();

	if (mult <= 1)
	{
		s_rootCounterValue += CTR_NATIVE_RCNT1_TICKS_PER_VBLANK;
		return;
	}

	s_rootCounterValue += CTR_NATIVE_RCNT1_TICKS_PER_VBLANK / mult;
	s_rootCounterRemainder += CTR_NATIVE_RCNT1_TICKS_PER_VBLANK % mult;
	if (s_rootCounterRemainder >= mult)
	{
		s_rootCounterValue++;
		s_rootCounterRemainder -= mult;
	}
}

int SetRCnt(int spec, unsigned short target, int mode)
{
	spec &= 0xffff;
	if (spec > 2)
	{
		return 0;
	}

	(void)target;
	(void)mode;
	return 1;
}

int GetRCnt(int spec)
{
	u64 counts;

	(void)spec;

	counts = s_rootCounterValue - s_rootCounterBase;
	if (counts > 0x7fffffff)
	{
		return 0x7fffffff;
	}

	return (int)counts;
}

int StartRCnt(int spec)
{
	spec &= 0xffff;
	if (spec > 2)
	{
		return 0;
	}

	return 1;
}

int StopRCnt(int spec)
{
	(void)spec;
	return 0;
}

int ResetRCnt(int spec)
{
	(void)spec;

	s_rootCounterBase = s_rootCounterValue;
	return 0;
}

int OpenEvent(unsigned int event, int spec, int mode, int32_t (*func)())
{
	(void)event;
	(void)spec;
	(void)mode;
	(void)func;
	return 0;
}

int CloseEvent(unsigned int event)
{
	(void)event;
	return 0;
}

int EnableEvent(unsigned int event)
{
	(void)event;
	return 0;
}

int TestEvent(unsigned int event)
{
	(void)event;
	return 0;
}

void InitCARD(int val)
{
	(void)val;
}

int StartCARD(void)
{
	return 0;
}

int StopCARD(void)
{
	return 0;
}

void _bu_init(void)
{
}
