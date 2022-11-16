/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2010  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"
#include "Common.h"
#include "Vif.h"
#include "Vif_Dma.h"
#include "newVif.h"
#include "GS.h"
#include "Gif.h"
#include "MTVU.h"
#include "Gif_Unit.h"
#include "DebugTools/Debug.h"

alignas(16) vifStruct vif0, vif1;

VUTracer::VUTracer() {
	last_regs = new VURegs;
}

void VUTracer::onTraceMenuItemClicked() {
	if(status == VUTRACESTATUS_DISABLED) {
		status = VUTRACESTATUS_WAITING;
	}
}

void VUTracer::onVsync() {
	if(status == VUTRACESTATUS_WAITING) {
		status = VUTRACESTATUS_TRACING;
		beginTraceSession();
	} else if(status == VUTRACESTATUS_TRACING) {
		status = VUTRACESTATUS_DISABLED;
		endTraceSession();
	}
}

void VUTracer::onVif1DmaSendChain(u32 tadr) {
	
}

void VUTracer::onVifDmaTag(u32 madr, u64 dma_tag) {
	
}

void VUTracer::onVu1ExecMicro(u32 pc) {
	if(status == VUTRACESTATUS_TRACING) {
		endTrace();
		beginTrace();
	}
}

void VUTracer::onInstructionExecuted(VURegs* regs) {
	if(status == VUTRACESTATUS_TRACING) {
		pushLastPacket();
		
		// Only write the microcode out once per file.
		if(!has_output_instructions) {
			fputc(VUTRACE_SETINSTRUCTIONS, trace_file);
			fwrite(regs->Micro, VU1_PROGSIZE, 1, trace_file);
			has_output_instructions = true;
		}
		
		// Only write out the registers that have changed.
		if(!last_regs_populated) {
			fputc(VUTRACE_SETREGISTERS, trace_file);
			fwrite(&regs->VF, sizeof(regs->VF), 1, trace_file);
			fwrite(&regs->VI, sizeof(regs->VI), 1, trace_file);
			fwrite(&regs->ACC, sizeof(regs->ACC), 1, trace_file);
			fwrite(&regs->q, sizeof(regs->q), 1, trace_file);
			fwrite(&regs->p, sizeof(regs->p), 1, trace_file);
			memcpy(last_regs, regs, sizeof(VURegs));
			last_regs_populated = true;
		} else {
			// Floating point registers.
			for(u8 i = 0; i < 32; i++) {
				if(memcmp(&last_regs->VF[i], &regs->VF[i], 16) != 0) {
					fputc(VUTRACE_PATCHREGISTER, trace_file);
					u8 register_index = i;
					fwrite(&register_index, 1, 1, trace_file);
					fwrite(&regs->VF[i], 16, 1, trace_file);
					memcpy(&last_regs->VF[i], &regs->VF[i], 16);
				}
			}
			
			// Integer registers.
			for(u8 i = 0; i < 32; i++) {
				if(memcmp(&last_regs->VI[i], &regs->VI[i], 16) != 0) {
					fputc(VUTRACE_PATCHREGISTER, trace_file);
					u8 register_index = 32 + i;
					fwrite(&register_index, 1, 1, trace_file);
					fwrite(&regs->VI[i], 16, 1, trace_file);
					memcpy(&last_regs->VI[i], &regs->VI[i], 16);
				}
			}
			
			// Other registers.
			if(memcmp(&last_regs->ACC, &regs->ACC, 16) != 0) {
				fputc(VUTRACE_PATCHREGISTER, trace_file);
				u8 register_index = 64;
				fwrite(&register_index, 1, 1, trace_file);
				fwrite(&regs->ACC, 16, 1, trace_file);
				memcpy(&last_regs->ACC, &regs->ACC, 16);
			}
			if(memcmp(&last_regs->q, &regs->q, 16) != 0) {
				fputc(VUTRACE_PATCHREGISTER, trace_file);
				u8 register_index = 65;
				fwrite(&register_index, 1, 1, trace_file);
				fwrite(&regs->q, 16, 1, trace_file);
				memcpy(&last_regs->q, &regs->q, 16);
			}
			if(memcmp(&last_regs->p, &regs->p, 16) != 0) {
				fputc(VUTRACE_PATCHREGISTER, trace_file);
				u8 register_index = 66;
				fwrite(&register_index, 1, 1, trace_file);
				fwrite(&regs->p, 16, 1, trace_file);
				memcpy(&last_regs->p, &regs->p, 16);
			}
		}
		
		// Only write out the values from memory that have changed.
		if(!last_memory_populated) {
			fputc(VUTRACE_SETMEMORY, trace_file);
			fwrite(regs->Mem, VU1_MEMSIZE, 1, trace_file);
			memcpy(last_memory, regs->Mem, VU1_MEMSIZE);
			last_memory_populated = true;
		} else {
			for(u32 i = 0; i < VU1_MEMSIZE; i += 4) {
				if(memcmp(&last_memory[i], &regs->Mem[i], 4) != 0) {
					fputc(VUTRACE_PATCHMEMORY, trace_file);
					fwrite(&i, 2, 1, trace_file);
					fwrite(&regs->Mem[i], 4, 1, trace_file);
					memcpy(&last_memory[i], &regs->Mem[i], 4);
				}
			}
		}
		
		// Keep track of which instructions are loads and stores.
		if(read_size > 0) {
			fputc(VUTRACE_LOADOP, trace_file);
			fwrite(&read_addr, sizeof(u32), 1, trace_file);
			fwrite(&read_size, sizeof(u32), 1, trace_file);
			read_size = 0;
		}
		
		if(write_size > 0) {
			fputc(VUTRACE_STOREOP, trace_file);
			fwrite(&write_addr, sizeof(u32), 1, trace_file);
			fwrite(&write_size, sizeof(u32), 1, trace_file);
			write_size = 0;
		}
	}
}

void VUTracer::onMemoryRead(u32 addr, u32 size) {
	read_addr = addr;
	read_size = size;
}

void VUTracer::onMemoryWrite(u32 addr, u32 size) {
	write_addr = addr;
	write_size = size;
}

void vutrace_log(const char* prefix, const char* fmt, ...) {
	FILE* log_file = VUTracer::get().log_file;
	if(log_file == nullptr) {
		return;
	}
	
	va_list args;
	va_start(args, fmt);
	fputs(prefix, log_file);
	vfprintf(log_file, fmt, args);
	fputc('\n', log_file);
	va_end(args);
}

VUTracer& VUTracer::get() {
	static VUTracer tracer;
	return tracer;
}

void VUTracer::beginTraceSession() {
	log_file = fopen("vutrace_output/LOG.txt", "wb");
	if(log_file == nullptr) {
		printf("[VUTrace] Fatal error: Cannot open log file for writing!\n");
	}
	
	trace_index = -1;
	beginTrace();
}

void VUTracer::endTraceSession() {
	endTrace();
	trace_index = -1;
	fclose(log_file);
	log_file = nullptr;
	printf("[VUTrace] Trace session finished.\n");
}

void VUTracer::beginTrace() {
	int local_trace_index = trace_index++;
	
	char file_name[128];
	snprintf(file_name, 128, "vutrace_output/trace%06d.bin", local_trace_index);
	printf("[VUTrace] Tracing to %s\n", file_name);
	fprintf(log_file, "[VUTrace] ******************************** Tracing to %s ********************************\n", file_name);
	trace_file = fopen(file_name, "wb");
	if(trace_file == nullptr) {
		printf("[VUTrace] Fatal error: Cannot open trace file!\n");
	}
	
	// Write header.
	fputc('V', trace_file);
	fputc('U', trace_file);
	fputc('T', trace_file);
	fputc('R', trace_file);
	int format_version = 3;
	fwrite(&format_version, 4, 1, trace_file);
}

void VUTracer::endTrace() {
	pushLastPacket();
	fclose(trace_file);
	has_output_instructions = false;
	last_regs_populated = false;
	last_memory_populated = false;
}

void VUTracer::pushLastPacket() {
	if(ftell(trace_file) > 8) {
		fputc(VUTRACE_PUSHSNAPSHOT, trace_file);
	}
}


void vif0Reset()
{
	/* Reset the whole VIF, meaning the internal pcsx2 vars and all the registers */
	memzero(vif0);
	memzero(vif0Regs);

	resetNewVif(0);
}

void vif1Reset()
{
	/* Reset the whole VIF, meaning the internal pcsx2 vars, and all the registers */
	memzero(vif1);
	memzero(vif1Regs);

	resetNewVif(1);
}

void SaveStateBase::vif0Freeze()
{
	FreezeTag("VIF0dma");

	Freeze(g_vif0Cycles);

	Freeze(vif0);

	Freeze(nVif[0].bSize);
	FreezeMem(nVif[0].buffer, nVif[0].bSize);
}

void SaveStateBase::vif1Freeze()
{
	FreezeTag("VIF1dma");

	Freeze(g_vif1Cycles);

	Freeze(vif1);

	Freeze(nVif[1].bSize);
	FreezeMem(nVif[1].buffer, nVif[1].bSize);
}

//------------------------------------------------------------------
// Vif0/Vif1 Write32
//------------------------------------------------------------------

__fi void vif0FBRST(u32 value)
{
	VIF_LOG("VIF0_FBRST write32 0x%8.8x", value);
	/* Fixme: Forcebreaks are pretty unknown for operation, presumption is it just stops it what its doing
			  usually accompanied by a reset, but if we find a broken game which falls here, we need to see it! (Refraction) */
	if (value & 0x2) // Forcebreak Vif,
	{
		/* I guess we should stop the VIF dma here, but not 100% sure (linuz) */
		cpuRegs.interrupt &= ~1; //Stop all vif0 DMA's
		vif0Regs.stat.VFS = true;
		vif0Regs.stat.VPS = VPS_IDLE;
		Console.WriteLn("vif0 force break");
	}

	if (value & 0x4) // Stop Vif.
	{
		// Not completely sure about this, can't remember what game, used this, but 'draining' the VIF helped it, instead of
		//  just stoppin the VIF (linuz).
		vif0Regs.stat.VSS = true;
		vif0Regs.stat.VPS = VPS_IDLE;
		vif0.vifstalled.enabled = VifStallEnable(vif0ch);
		vif0.vifstalled.value = VIF_IRQ_STALL;
	}

	if (value & 0x8) // Cancel Vif Stall.
	{
		bool cancel = false;

		/* Cancel stall, first check if there is a stall to cancel, and then clear VIF0_STAT VSS|VFS|VIS|INT|ER0|ER1 bits */
		if (vif0Regs.stat.test(VIF0_STAT_VSS | VIF0_STAT_VIS | VIF0_STAT_VFS))
			cancel = true;

		vif0Regs.stat.clear_flags(VIF0_STAT_VSS | VIF0_STAT_VFS | VIF0_STAT_VIS |
								  VIF0_STAT_INT | VIF0_STAT_ER0 | VIF0_STAT_ER1);
		if (cancel)
		{
			g_vif0Cycles = 0;
			// loop necessary for spiderman
			if (vif0ch.chcr.STR)
				CPU_INT(DMAC_VIF0, 0); // Gets the timing right - Flatout
		}
	}

	if (value & 0x1) // Reset Vif.
	{
		//Console.WriteLn("Vif0 Reset %x", vif0Regs.stat._u32);
		u128 SaveCol;
		u128 SaveRow;

		//	if(vif0ch.chcr.STR) DevCon.Warning("FBRST While Vif0 active");
		//Must Preserve Row/Col registers! (Downhill Domination for testing)
		SaveCol._u64[0] = vif0.MaskCol._u64[0];
		SaveCol._u64[1] = vif0.MaskCol._u64[1];
		SaveRow._u64[0] = vif0.MaskRow._u64[0];
		SaveRow._u64[1] = vif0.MaskRow._u64[1];
		memzero(vif0);
		vif0.MaskCol._u64[0] = SaveCol._u64[0];
		vif0.MaskCol._u64[1] = SaveCol._u64[1];
		vif0.MaskRow._u64[0] = SaveRow._u64[0];
		vif0.MaskRow._u64[1] = SaveRow._u64[1];
		vif0ch.qwc = 0; //?
		cpuRegs.interrupt &= ~1; //Stop all vif0 DMA's
		psHu64(VIF0_FIFO) = 0;
		psHu64(VIF0_FIFO + 8) = 0;
		vif0.vifstalled.enabled = false;
		vif0.irqoffset.enabled = false;
		vif0.inprogress = 0;
		vif0.cmd = 0;
		vif0.done = true;
		vif0ch.chcr.STR = false;
		vif0Regs.err.reset();
		vif0Regs.stat.clear_flags(VIF0_STAT_FQC | VIF0_STAT_INT | VIF0_STAT_VSS | VIF0_STAT_VIS | VIF0_STAT_VFS | VIF0_STAT_VPS); // FQC=0
	}
}

__fi void vif1FBRST(u32 value)
{
	VIF_LOG("VIF1_FBRST write32 0x%8.8x", value);

	/* Fixme: Forcebreaks are pretty unknown for operation, presumption is it just stops it what its doing
			  usually accompanied by a reset, but if we find a broken game which falls here, we need to see it! (Refraction) */

	if (FBRST(value).FBK) // Forcebreak Vif.
	{
		/* I guess we should stop the VIF dma here, but not 100% sure (linuz) */
		vif1Regs.stat.VFS = true;
		vif1Regs.stat.VPS = VPS_IDLE;
		cpuRegs.interrupt &= ~((1 << 1) | (1 << 10)); //Stop all vif1 DMA's
		vif1.vifstalled.enabled = VifStallEnable(vif1ch);
		vif1.vifstalled.value = VIF_IRQ_STALL;
		Console.WriteLn("vif1 force break");
	}

	if (FBRST(value).STP) // Stop Vif.
	{
		// Not completely sure about this, can't remember what game used this, but 'draining' the VIF helped it, instead of
		// just stoppin the VIF (linuz).
		vif1Regs.stat.VSS = true;
		vif1Regs.stat.VPS = VPS_IDLE;
		vif1.vifstalled.enabled = VifStallEnable(vif1ch);
		vif1.vifstalled.value = VIF_IRQ_STALL;
	}

	if (FBRST(value).STC) // Cancel Vif Stall.
	{
		bool cancel = false;
		//DevCon.Warning("Cancel stall. Stat = %x", vif1Regs.stat._u32);
		// Cancel stall, first check if there is a stall to cancel, and then clear VIF1_STAT VSS|VFS|VIS|INT|ER0|ER1 bits
		if (vif1Regs.stat.test(VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS))
		{
			cancel = true;
		}

		vif1Regs.stat.clear_flags(VIF1_STAT_VSS | VIF1_STAT_VFS | VIF1_STAT_VIS |
								  VIF1_STAT_INT | VIF1_STAT_ER0 | VIF1_STAT_ER1);

		if (cancel)
		{
			g_vif1Cycles = 0;
			// loop necessary for spiderman
			switch (dmacRegs.ctrl.MFD)
			{
			case MFD_VIF1:
				//Console.WriteLn("MFIFO Stall");
				//MFIFO active and not empty
				if (vif1ch.chcr.STR && !vif1Regs.stat.test(VIF1_STAT_FDR))
					CPU_INT(DMAC_MFIFO_VIF, 0);
				break;

			case NO_MFD:
			case MFD_RESERVED:
			case MFD_GIF: // Wonder if this should be with VIF?
				// Gets the timing right - Flatout
				if (vif1ch.chcr.STR && !vif1Regs.stat.test(VIF1_STAT_FDR))
					CPU_INT(DMAC_VIF1, 0);
				break;
			}

			//vif1ch.chcr.STR = true;
		}
	}

	if (FBRST(value).RST) // Reset Vif.
	{
		u128 SaveCol;
		u128 SaveRow;
		//if(vif1ch.chcr.STR) DevCon.Warning("FBRST While Vif1 active");
		//Must Preserve Row/Col registers! (Downhill Domination for testing) - Really shouldnt be part of the vifstruct.
		SaveCol._u64[0] = vif1.MaskCol._u64[0];
		SaveCol._u64[1] = vif1.MaskCol._u64[1];
		SaveRow._u64[0] = vif1.MaskRow._u64[0];
		SaveRow._u64[1] = vif1.MaskRow._u64[1];
		u8 mfifo_empty = vif1.inprogress & 0x10;
		memzero(vif1);
		vif1.MaskCol._u64[0] = SaveCol._u64[0];
		vif1.MaskCol._u64[1] = SaveCol._u64[1];
		vif1.MaskRow._u64[0] = SaveRow._u64[0];
		vif1.MaskRow._u64[1] = SaveRow._u64[1];


		GUNIT_WARN(Color_Red, "VIF FBRST Reset MSK = %x", vif1Regs.mskpath3);
		vif1Regs.mskpath3 = false;
		gifRegs.stat.M3P = 0;
		vif1Regs.err.reset();
		vif1.inprogress = mfifo_empty;
		vif1.cmd = 0;
		vif1.vifstalled.enabled = false;
		vif1Regs.stat._u32 = 0;
	}
}

__fi void vif1STAT(u32 value)
{
	VIF_LOG("VIF1_STAT write32 0x%8.8x", value);

	/* Only FDR bit is writable, so mask the rest */
	if ((vif1Regs.stat.FDR) ^ ((tVIF_STAT&)value).FDR)
	{
		bool isStalled = false;
		// different so can't be stalled
		if (vif1Regs.stat.test(VIF1_STAT_INT | VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS))
		{
			DbgCon.WriteLn("changing dir when vif1 fifo stalled done = %x qwc = %x stat = %x", vif1.done, vif1ch.qwc, vif1Regs.stat._u32);
			isStalled = true;
		}

		//Hack!! Hotwheels seems to leave 1QW in the fifo and expect the DMA to be ready for a reverse FIFO
		//There's no important data in there so for it to work, we will just end it.
		//Hotwheels had this in the "direction when stalled" area, however Sled Storm seems to keep an eye on the dma
		//position, as we clear it and set it to the end well before the interrupt, the game assumes it's finished,
		//then proceeds to reverse the dma before we have even done it ourselves. So lets just make sure VIF is ready :)
		if (vif1ch.qwc > 0 || isStalled == false)
		{
			if (vif1ch.chcr.STR)
			{
				vif1ch.qwc = 0;
				hwDmacIrq(DMAC_VIF1);
				vif1ch.chcr.STR = false;
			}
			cpuRegs.interrupt &= ~((1 << DMAC_VIF1) | (1 << DMAC_MFIFO_VIF));
		}
		//This is actually more important for our handling, else the DMA for reverse fifo doesnt start properly.
	}

	vif1Regs.stat.FDR = VIF_STAT(value).FDR;

	if (vif1Regs.stat.FDR) // Vif transferring to memory.
	{
		// Hack but it checks this is true before transfer? (fatal frame)
		// Update Refraction: Use of this function has been investigated and understood.
		// Before this ever happens, a DIRECT/HL command takes place sending the transfer info to the GS
		// One of the registers told about this is TRXREG which tells us how much data is going to transfer (th x tw) in words
		// As far as the GS is concerned, the transfer starts as soon as TRXDIR is accessed, which is why fatal frame
		// was expecting data, the GS should already be sending it over (buffering in the FIFO)

		vif1Regs.stat.FQC = std::min((u32)16, vif1.GSLastDownloadSize);
		//Console.Warning("Reversing VIF Transfer for %x QWC", vif1.GSLastDownloadSize);
	}
	else // Memory transferring to Vif.
	{
		//Sometimes the value from the GS is bigger than vif wanted, so it just sets it back and cancels it.
		//Other times it can read it off ;)
		vif1Regs.stat.FQC = 0;
		if (vif1ch.chcr.STR)
			CPU_INT(DMAC_VIF1, 0);
	}
}

#define caseVif(x) (idx ? VIF1_##x : VIF0_##x)

_vifT __fi u32 vifRead32(u32 mem)
{
	vifStruct& vif = MTVU_VifX;
	bool wait = idx && THREAD_VU1;

	switch (mem)
	{
		case caseVif(ROW0):
			if (wait)
				vu1Thread.WaitVU();
			return vif.MaskRow._u32[0];
		case caseVif(ROW1):
			if (wait)
				vu1Thread.WaitVU();
			return vif.MaskRow._u32[1];
		case caseVif(ROW2):
			if (wait)
				vu1Thread.WaitVU();
			return vif.MaskRow._u32[2];
		case caseVif(ROW3):
			if (wait)
				vu1Thread.WaitVU();
			return vif.MaskRow._u32[3];

		case caseVif(COL0):
			if (wait)
				vu1Thread.WaitVU();
			return vif.MaskCol._u32[0];
		case caseVif(COL1):
			if (wait)
				vu1Thread.WaitVU();
			return vif.MaskCol._u32[1];
		case caseVif(COL2):
			if (wait)
				vu1Thread.WaitVU();
			return vif.MaskCol._u32[2];
		case caseVif(COL3):
			if (wait)
				vu1Thread.WaitVU();
			return vif.MaskCol._u32[3];
	}

	return psHu32(mem);
}

// returns FALSE if no writeback is needed (or writeback is handled internally)
// returns TRUE if the caller should writeback the value to the eeHw register map.
_vifT __fi bool vifWrite32(u32 mem, u32 value)
{
	vifStruct& vif = GetVifX;

	switch (mem)
	{
		case caseVif(MARK):
			VIF_LOG("VIF%d_MARK write32 0x%8.8x", idx, value);
			vifXRegs.stat.MRK = false;
			//vifXRegs.mark	   = value;
			break;

		case caseVif(FBRST):
			if (!idx)
				vif0FBRST(value);
			else
				vif1FBRST(value);
			return false;

		case caseVif(STAT):
			if (idx)
			{ // Only Vif1 does this stuff?
				vif1STAT(value);
			}
			return false;

		case caseVif(ERR):
		case caseVif(MODE):
			// standard register writes -- handled by caller.
			break;

		case caseVif(ROW0):
			vif.MaskRow._u32[0] = value;
			vu1Thread.WriteRow(vif);
			return false;
		case caseVif(ROW1):
			vif.MaskRow._u32[1] = value;
			vu1Thread.WriteRow(vif);
			return false;
		case caseVif(ROW2):
			vif.MaskRow._u32[2] = value;
			vu1Thread.WriteRow(vif);
			return false;
		case caseVif(ROW3):
			vif.MaskRow._u32[3] = value;
			vu1Thread.WriteRow(vif);
			return false;

		case caseVif(COL0):
			vif.MaskCol._u32[0] = value;
			vu1Thread.WriteCol(vif);
			return false;
		case caseVif(COL1):
			vif.MaskCol._u32[1] = value;
			vu1Thread.WriteCol(vif);
			return false;
		case caseVif(COL2):
			vif.MaskCol._u32[2] = value;
			vu1Thread.WriteCol(vif);
			return false;
		case caseVif(COL3):
			vif.MaskCol._u32[3] = value;
			vu1Thread.WriteCol(vif);
			return false;
	}

	// fall-through case: issue standard writeback behavior.
	return true;
}

template u32 vifRead32<0>(u32 mem);
template u32 vifRead32<1>(u32 mem);

template bool vifWrite32<0>(u32 mem, u32 value);
template bool vifWrite32<1>(u32 mem, u32 value);
