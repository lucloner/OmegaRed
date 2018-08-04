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

//#undef noexcept
//#undef thread_local

#define _PC_	// disables MIPS opcode macros.

#include "IopCommon.h"
#include "Patch.h"
#include "GameDatabase.h"

#include <memory>
#include <wx/textfile.h>
#include <wx/dir.h>
#include <wx/txtstrm.h>
#include <wx/zipstrm.h>

// This is a declaration for PatchMemory.cpp::_ApplyPatch where we're (patch.cpp)
// the only consumer, so it's not made public via Patch.h
// Applies a single patch line to emulation memory regardless of its "place" value.
extern void _ApplyPatch(IniPatch *p);


IniPatch Patch[ MAX_PATCH ];

int patchnumber = 0;

wxString strgametitle;

struct PatchTextTable
{
	int				code;
	const wxChar*	text;
	PATCHTABLEFUNC*	func;
};

static const PatchTextTable commands_patch[] =
{
	{ 1, L"comment",	PatchFunc::comment },
	{ 2, L"patch",		PatchFunc::patch },
	{ 0, wxEmptyString, NULL } // Array Terminator
};

static const PatchTextTable dataType[] =
{
	{ 1, L"byte", NULL },
	{ 2, L"short", NULL },
	{ 3, L"word", NULL },
	{ 4, L"double", NULL },
	{ 5, L"extended", NULL },
	{ 0, wxEmptyString, NULL }
};

static const PatchTextTable cpuCore[] =
{
	{ 1, L"EE", NULL },
	{ 2, L"IOP", NULL },
	{ 0, wxEmptyString,  NULL }
};

// IniFile Functions.

static void inifile_trim( wxString& buffer )
{
	buffer.Trim(false);			// trims left side.

	if( buffer.Length() <= 1 )	// this I'm not sure about... - air
	{
		buffer.Empty();
		return;
	}

	if( buffer.Left( 2 ) == L"//" )
	{
		buffer.Empty();
		return;
	}

	buffer.Trim(true);			// trims right side.
}

static int PatchTableExecute( const ParsedAssignmentString& set, const PatchTextTable * Table )
{
	int i = 0;

	while (Table[i].text[0])
	{
		if (!set.lvalue.Cmp(Table[i].text))
		{
			if (Table[i].func) Table[i].func(set.lvalue, set.rvalue);
			break;
		}
		i++;
	}

	return Table[i].code;
}

// This routine is for executing the commands of the ini file.
static void inifile_command(const wxString& cmd)
{
	ParsedAssignmentString set( cmd );

	// Is this really what we want to be doing here? Seems like just leaving it empty/blank
	// would make more sense... --air
    if (set.rvalue.IsEmpty()) set.rvalue = set.lvalue;

	/*int code = */PatchTableExecute(set, commands_patch);
}


void inifile_commandProxy(const wxString& cmd)
{
	inifile_command(cmd);
}
//	bool patchFound = false;
//	wxString patch;
//
//	if (game.IsOk())
//	{
//		if (game.sectionExists(L"patches", crc)) {
//			patch = game.getSection(L"patches", crc);
//			patchFound = true;
//		}
//		else if (game.keyExists(L"[patches]")) {
//			patch = game.getString(L"[patches]");
//			patchFound = true;
//		}
//	}
//
//	if (patchFound) TrimPatches(patch);
//
//	return patchnumber;
//}

void inifile_processString(const wxString& inStr)
{
  wxString str(inStr);
  inifile_trim(str);
  if (!str.IsEmpty()) inifile_command(str);
}

void ForgetLoadedPatches()
{
  patchnumber = 0;
}

static u32 StrToU32(const wxString& str, int base = 10)
{
    unsigned long l;
    str.ToULong(&l, base);
    return l;
}

static u64 StrToU64(const wxString& str, int base = 10)
{
    wxULongLong_t l;
    str.ToULongLong(&l, base);
    return l;
}

static void SplitString(wxArrayString &dest, const wxString &src, const wxChar& delims, wxStringTokenizerMode mode = wxTOKEN_RET_EMPTY_ALL)
{
	wxStringTokenizer parts(src, delims);
	while (parts.HasMoreTokens())
		dest.Add(parts.GetNextToken());
}

// PatchFunc Functions.
namespace PatchFunc
{
    void comment( const wxString& text1, const wxString& text2 )
    {
		//PatchesCon->WriteLn(L"comment: " + text2);
    }

    struct PatchPieces
    {
		wxArrayString m_pieces;

		PatchPieces( const wxString& param )
		{
			SplitString( m_pieces, param, L',' );
			if( m_pieces.GetCount() < 5 )
				throw wxsFormat(L"Expected 5 data parameters; only found %d", m_pieces.GetCount());
		}

		const wxString& PlaceToPatch() const	{ return m_pieces[0]; }
		const wxString& CpuType() const			{ return m_pieces[1]; }
		const wxString& MemAddr() const			{ return m_pieces[2]; }
		const wxString& OperandSize() const		{ return m_pieces[3]; }
		const wxString& WriteValue() const		{ return m_pieces[4]; }
    };

	void patchHelper(const wxString& cmd, const wxString& param) {
		// Error Handling Note:  I just throw simple wxStrings here, and then catch them below and
		// format them into more detailed cmd+data+error printouts.  If we want to add user-friendly
		// (translated) messages for display in a popup window then we'll have to upgrade the
		// exception a little bit.

		// print the actual patch lines only in verbose mode (even in devel)
		if (DevConWriterEnabled)
			DevCon.WriteLn(cmd + L" " + param);

		try
		{
			if(patchnumber >= MAX_PATCH)
				throw wxString( L"Maximum number of patches reached" );

			IniPatch& iPatch = Patch[patchnumber];
			PatchPieces pieces(param);

			iPatch.enabled = 0;

			iPatch.placetopatch	= StrToU32(pieces.PlaceToPatch(), 10);
			if (iPatch.placetopatch >= _PPT_END_MARKER)
				throw wxsFormat(L"Invalid 'place' value '%s' (0 - once on startup, 1: continuously)", WX_STR(pieces.PlaceToPatch()));
			iPatch.cpu			= (patch_cpu_type)PatchTableExecute(pieces.CpuType(), cpuCore);
			iPatch.addr			= StrToU32(pieces.MemAddr(), 16);
			iPatch.type			= (patch_data_type)PatchTableExecute(pieces.OperandSize(), dataType);
			iPatch.data			= StrToU64(pieces.WriteValue(), 16);

			if (iPatch.cpu  == 0)
				throw wxsFormat(L"Unrecognized CPU Target: '%s'", WX_STR(pieces.CpuType()));

			if (iPatch.type == 0)
				throw wxsFormat(L"Unrecognized Operand Size: '%s'", WX_STR(pieces.OperandSize()));

			iPatch.enabled = 1; // omg success!!

			patchnumber++;
		}
		catch( wxString& exmsg )
		{
			Console.Error(L"(Patch) Error Parsing: %s=%s", WX_STR(cmd), WX_STR(param));
			Console.Indent().Error( exmsg );
		}
	}
	void patch(const wxString& cmd, const wxString& param) { patchHelper(cmd, param); }
}

// This is for applying patches directly to memory
void ApplyLoadedPatches(patch_place_type place)
{
	for (int i = 0; i < patchnumber; i++)
	{
	    if (Patch[i].placetopatch == place)
            _ApplyPatch(&Patch[i]);
	}
}
