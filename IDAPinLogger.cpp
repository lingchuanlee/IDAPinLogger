/*
IDAPinLogger:
Author: @_wirepair / isaac.dawson{}gmail.com.
Keeps a hit count for every instruction executed in a specified module.
If no module is specified, we use the main executable image.
After the program finishes running, we write the buffer of hits out to a file which
then needs to be imported into IDA Pro using dereko's (?) loadlog.py IDAPython sript.

I feel this method is a bit more simpiler then his (and I was having problems with
certain executables). But who knows maybe I'm doing something wrong :).

How to build:
Copy this project into your pin source directory:
%pin%\source\tools\IDAPinLogger
Open Visual Studio (2008) and build or run the makefile.

How to run:
Log all hits in nc.exe module:
c:\pin\pin.exe -t C:\pin\IDAPinLogger.dll -m nc.exe -- nc.exe -l -v -p 999
or (main exe assumed)
c:\pin\pin.exe -t C:\pin\IDAPinLogger.dll -- nc.exe -l -v -p 999

Log all hits in supporting module (note case sensitive!):
c:\pin\pin.exe -t C:\pin\IDAPinLogger.dll -m KERNEL32.DLL -- nc.exe -l -v -p 999

Only start logging hits after instruction at base+0x991c is hit, stop logging after base+0x4242 is hit:
c:\pin\pin.exe -t C:\pin\IDAPinLogger.dll -s 0x991c -e 0x4242 -- nc.exe -l -v -p 999


*/
#include "pin.H"
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>

namespace WINDOWS {
#include "Windows.h"
}
/* ================================================================== */
// Global variables 
/* ================================================================== */

std::ostream * out = &cerr;
// Module Info
ADDRINT moduleStart = 0;
ADDRINT moduleEnd = 0;
unsigned int moduleSize = 0;
// Hit Count Variables.
FILE *IDAInsLogFile;
WINDOWS::BYTE *logBuffer;
//Log start/stop variables
ADDRINT gLogStart = -1;
ADDRINT gLogStop = -1;
bool gLogging = true;
/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,  "pintool",
	"o", "", "specify file name for IDAPinLogger output");
KNOB<string> KnobModuleToLog(KNOB_MODE_WRITEONCE, "pintool",
	"m", "", "specify the module to record instruction visits.");
KNOB<string> KnobLogStart(KNOB_MODE_WRITEONCE, "pintool",
    "s", "", "specify instruction offset (will be auto added to base) that will start logging.");
KNOB<string> KnobLogStop(KNOB_MODE_WRITEONCE, "pintool",
    "e", "", "specify instruction offset (will be auto added to base) that will stop logging.");


INT32 Usage()
{
	cerr << "This tool writes the number of times an instruction is called to a map file " << endl <<
			"that can then be fed into IDA Pro to highlight which instructions were executed." << endl << endl;
	cerr << KNOB_BASE::StringKnobSummary() << endl;
	return -1;
}

/* ===================================================================== */
// Analysis routines
/* ===================================================================== */
VOID StartLogging(UINT32 eip)
{
	gLogging = true;
	std::cerr << "Logging Started due to log start instruction hit." << endl;
}

VOID StopLogging(UINT32 eip)
{
	gLogging = false;
	std::cerr << "Logging Stopped due to log stop instruction hit." << endl;
}

VOID IncrementCount(UINT32 eip)
{
	unsigned int idx = eip - moduleStart;
	if (gLogging == false)
	{ 
		return; 
	}
	// if we get called more than 255 times, well, stop.
	if (logBuffer[idx] >= 255)
	{
		return;
	}
	logBuffer[idx] += 1;
}

/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */
VOID ImageLoad(IMG img, VOID *v)
{
	std::cerr << "Loading " << IMG_Name(img).c_str() << " Start " << hex << IMG_LowAddress(img) << " End " << IMG_HighAddress(img) << endl;
	// if no module passed, just use the main executable
	string module = KnobModuleToLog.Value();
	if (module.empty() && IMG_IsMainExecutable( img ) )
	{
		module = IMG_Name(img);
	}
	// keep in mind this is case sensitive...
	if (module.empty() || IMG_Name( img ).rfind( module.c_str() ) == string::npos) 
	{
		return;
	}
	moduleStart = IMG_LowAddress(img);
	moduleEnd = IMG_HighAddress(img);
	moduleSize = moduleEnd - moduleStart;
	if (gLogStart != -1)
	{
		gLogStart += moduleStart;
	}
	
	if (gLogStop != -1)
	{
		gLogStop += moduleStart;
	}

	std::cerr << "Module size is: " << moduleSize << endl;
	logBuffer = (WINDOWS::BYTE *)calloc(moduleSize,sizeof(WINDOWS::BYTE));
	if (logBuffer == NULL)
	{
		std::cerr << "Unable to allocate enough heapspace, how friggen big is this module?" << endl;
		return;
	}
	std::cerr << "Creating hit count for instructions in module: " << IMG_Name(img).c_str() << endl;

}

VOID Instruction(INS ins, VOID *v)
{
    ADDRINT loc = INS_Address(ins);
	if (gLogStart != -1 && loc == gLogStart)
	{
		INS_InsertCall(ins,
			IPOINT_BEFORE,
			(AFUNPTR)StartLogging,
			IARG_INST_PTR,
			IARG_END);
	}

	if (gLogStop != -1 && loc == gLogStop)
	{
		INS_InsertCall(ins,
			IPOINT_BEFORE,
			(AFUNPTR)StopLogging,
			IARG_INST_PTR,
			IARG_END);
	}

	if (loc >= moduleStart && loc <= moduleEnd)
	{
		INS_InsertCall(ins,
			IPOINT_BEFORE,
			(AFUNPTR)IncrementCount, 
			IARG_INST_PTR,
			IARG_END);
	}
}


VOID Fini(INT32 code, VOID *v)
{
	std::cerr << "Writing instruction hits to disk." << endl;
	fwrite(logBuffer, sizeof(WINDOWS::BYTE), moduleSize, IDAInsLogFile);
	
	fflush(IDAInsLogFile);
	fclose(IDAInsLogFile);
	free(logBuffer);
}

int main(int argc, char *argv[])
{
	if( PIN_Init(argc,argv) )
	{
		return Usage();
	}

	string fileName = KnobOutputFile.Value();
	if (!fileName.empty()) 
	{ 
		IDAInsLogFile = fopen(fileName.c_str(), "wb+"); 
	}
	else
	{
		std::cerr << "Need an outfile file dingus." << endl;
		return -1;
	}
	
	if (!KnobLogStart.Value().empty())
	{
		stringstream converter(KnobLogStart.Value());
		converter >> hex >> gLogStart;
		gLogging = false;
	}

	if (!KnobLogStop.Value().empty())
	{
		stringstream converter(KnobLogStop.Value());
		converter >> hex >> gLogStop;
	}

	IMG_AddInstrumentFunction(ImageLoad, 0);
	INS_AddInstrumentFunction(Instruction, 0);
	PIN_AddFiniFunction(Fini, 0);
	// Start the program, never returns
	PIN_StartProgram();

	return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
