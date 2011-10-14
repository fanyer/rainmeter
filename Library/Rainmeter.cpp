/*
  Copyright (C) 2001 Kimmo Pekkola

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "StdAfx.h"
#include "Rainmeter.h"
#include "TrayWindow.h"
#include "System.h"
#include "Error.h"
#include "DialogAbout.h"
#include "DialogManage.h"
#include "MeasureNet.h"
#include "MeterString.h"
#include "resource.h"
#include "UpdateCheck.h"
#include "../Version.h"

#include "DisableThreadLibraryCalls.h"	// contains DllMain entry point

using namespace Gdiplus;

CRainmeter* Rainmeter; // The module

/*
** ParseString
**
** Splits the given string into substrings
**
*/
std::vector<std::wstring> CRainmeter::ParseString(LPCTSTR str)
{
	std::vector<std::wstring> result;
	if (str)
	{
		std::wstring arg = str;

		// Split the argument between first space.
		// Or if string is in quotes, the after the second quote.

		size_t pos;
		std::wstring newStr;
		while ((pos = arg.find_first_not_of(L' ')) != std::wstring::npos)
		{
			if (arg[pos] == L'"')
			{
				if (arg.size() > (pos + 2) &&
					arg[pos + 1] == L'"' && arg[pos + 2] == L'"')
				{
					// Eat found quotes and finding ending """
					arg.erase(0, pos + 3);

					size_t extra = 4;
					if ((pos = arg.find(L"\"\"\" ")) == std::wstring::npos)
					{
						extra = 3;
						pos = arg.find(L"\"\"\"");
					}

					if (pos != std::wstring::npos)
					{
						newStr.assign(arg, 0, pos);
						arg.erase(0, pos + extra);

						result.push_back(newStr);
					}

					// Skip stripping quotes
					continue;
				}
				else
				{
					// Eat found quote and find ending quote 
					arg.erase(0, pos + 1);
					pos = arg.find_first_of(L'"');
				}
			}
			else
			{
				if (pos > 0)
				{
					// Eat everything until non-space (and non-quote) char
					arg.erase(0, pos);
				}

				// Find the second quote
				pos = arg.find_first_of(L' ');
			}

			if (pos != std::wstring::npos)
			{
				newStr.assign(arg, 0, pos);
				arg.erase(0, pos + 1);

				// Strip quotes
				while ((pos = newStr.find(L'"')) != std::wstring::npos)
				{
					newStr.erase(pos, 1);
				}

				result.push_back(newStr);
			}
			else  // quote or space not found
			{
				break;
			}
		}

		if (arg.size() > 0)
		{
			// Strip quotes
			while ((pos = arg.find(L'"')) != std::wstring::npos)
			{
				arg.erase(pos, 1);
			}

			result.push_back(arg);
		}
	}

	return result;
}

/*
** Initialize
**
** Initializes Rainmeter
**
*/
int Initialize(HWND hWnd, HINSTANCE hInstance, LPCWSTR lpCmdLine)
{
	int result = 1;

	try
	{
		Rainmeter = new CRainmeter;

		if (Rainmeter)
		{
			result = Rainmeter->Initialize(hWnd, hInstance, lpCmdLine);
		}
	}
	catch (CError& error)
	{
		MessageBox(hWnd, error.GetString().c_str(), APPNAME, MB_OK | MB_TOPMOST | MB_ICONERROR);
	}

	return result;
}

/*
** Quit
**
** Quits Rainmeter.
**
*/
void Quit()
{
	if (Rainmeter)
	{
		delete Rainmeter;
		Rainmeter = NULL;
	}
}

/*
** ExecuteBang
**
** Runs a bang command. This is called from the main application
** when a command is given as a command line argument.
**
*/
void ExecuteBang(LPCTSTR szBang)
{
	if (szBang)
	{
		// ExecuteBang needs to be delayed since it crashes if done during processing.
		// The receiver must free a given string buffer (lParam) by using free().
		WCHAR* bang = _wcsdup(szBang);
		PostMessage(Rainmeter->GetTrayWindow()->GetWindow(), WM_TRAY_DELAYED_EXECUTE, (WPARAM)NULL, (LPARAM)bang);
	}
}

/*
** ReadConfigString
**
** Reads a config string. Used by the plugins.
**
*/
LPCTSTR ReadConfigString(LPCTSTR section, LPCTSTR key, LPCTSTR defValue)
{
	if (Rainmeter)
	{
		CConfigParser* parser = Rainmeter->GetCurrentParser();
		if (parser)
		{
			return parser->ReadString(section, key, defValue, false).c_str();
		}
	}
	return NULL;
}

/*
** PluginBridge
**
** Receives a command and data from a plugin and returns a result.  Used by plugins.
**
*/
LPCTSTR PluginBridge(LPCTSTR _sCommand, LPCTSTR _sData)
{
	if (Rainmeter)
	{
		static std::wstring result;

		if (_sCommand == NULL || *_sCommand == L'\0')
		{
			return L"noop";
		}

		if (_sData == NULL) _sData = L"";

		std::wstring sCommand = _sCommand;
		std::transform(sCommand.begin(), sCommand.end(), sCommand.begin(), ::towlower);

		// Command       GetConfig
		// Data          unquoted full path and filename given to the plugin on initialize
		//               (note: this is CaSe-SeNsItIvE!)
		// Execution     none
		// Result        the config name if found or a blank string if not
		if (sCommand == L"getconfig")
		{
			// returns the config name, lookup by INI file

			CMeterWindow *meterWindow = Rainmeter->GetMeterWindowByINI(_sData);
			if (meterWindow)
			{
				result = L"\"";
				result += meterWindow->GetSkinName();
				result += L"\"";
				return result.c_str();
			}

			return L"";
		}

		// Command       GetWindow
		// Data          [the config name]
		// Execution     none
		// Result        the HWND to the specified config window if found, 'error' otherwise
		if (sCommand == L"getwindow")
		{
			std::vector<std::wstring> subStrings = CRainmeter::ParseString(_sData);

			if (subStrings.size() >= 1)
			{
				const std::wstring& config = subStrings[0];

				CMeterWindow *meterWindow = Rainmeter->GetMeterWindow(config);
				if (meterWindow)
				{
					WCHAR buf1[64];
					_snwprintf_s(buf1, _TRUNCATE, L"%lu", PtrToUlong(meterWindow->GetWindow()));
					result = buf1;
					return result.c_str();
				}
			}
			return L"error";
		}

		// Command       GetVariable
		// Data          [the config name]
		// Execution     none
		// Result        the value of the variable
		if (sCommand == L"getvariable")
		{
			std::vector<std::wstring> subStrings = CRainmeter::ParseString(_sData);

			if (subStrings.size() >= 2)
			{
				const std::wstring& config = subStrings[0];

				CMeterWindow *meterWindow = Rainmeter->GetMeterWindow(config);
				if (meterWindow)
				{
					const std::wstring& variable = subStrings[1];
					std::wstring result_from_parser;

					if (meterWindow->GetParser().GetVariable(variable, result_from_parser))
					{
						result = result_from_parser;
						return result.c_str();
					}
				}
			}

			return L"";
		}

		// Command       SetVariable
		// Data          [the config name] [variable data]
		// Execution     the indicated variable is updated
		// Result        'success' if the config was found, 'error' otherwise
		if (sCommand == L"setvariable")
		{
			std::vector<std::wstring> subStrings = CRainmeter::ParseString(_sData);

			if (subStrings.size() >= 2)
			{
				const std::wstring& config = subStrings[0];
				std::wstring arguments;

				for (size_t i = 1, isize = subStrings.size(); i < isize; ++i)
				{
					if (i != 1) arguments += L" ";
					arguments += subStrings[i];
				}

				CMeterWindow *meterWindow = Rainmeter->GetMeterWindow(config);
				if (meterWindow)
				{
					meterWindow->RunBang(BANG_SETVARIABLE, arguments.c_str());
					return L"success";
				}
			}

			/*
			result = L"er1/";
			result += subStrings[0];
			result += L"/";
			TCHAR x[100];
			_snwprintf_s(x, _TRUNCATE, L"%d", subStrings.size());
			result += x;
			return result.c_str();
			*/
			return L"error";
		}

		return L"noop";
	}

	return L"error:no rainmeter!";
}

/*
** BangWithArgs
**
** Parses Bang args
**
*/
void BangWithArgs(BANGCOMMAND bang, const WCHAR* arg, size_t numOfArgs)
{
	if (Rainmeter)
	{
		std::vector<std::wstring> subStrings = CRainmeter::ParseString(arg);
		size_t subStringsSize = subStrings.size();
		std::wstring config;
		std::wstring argument;

		// Don't include the config name from the arg if there is one
		for (size_t i = 0; i < numOfArgs; ++i)
		{
			if (i != 0) argument += L" ";
			if (i < subStringsSize)
			{
				argument += subStrings[i];
			}
		}

		if (subStringsSize >= numOfArgs)
		{
			if (subStringsSize > numOfArgs)
			{
				config = subStrings[numOfArgs];
			}

			if ((!config.empty()) && (config != L"*"))
			{
				// Config defined, so bang only that
				CMeterWindow* meterWindow = Rainmeter->GetMeterWindow(config);

				if (meterWindow)
				{
					meterWindow->RunBang(bang, argument.c_str());
				}
				else
				{
					LogWithArgs(LOG_ERROR,  L"Bang: Config \"%s\" not found", config.c_str());
				}
			}
			else
			{
				// No config defined -> apply to all.
				const std::map<std::wstring, CMeterWindow*>& windows = Rainmeter->GetAllMeterWindows();
				std::map<std::wstring, CMeterWindow*>::const_iterator iter = windows.begin();

				for (; iter != windows.end(); ++iter)
				{
					((*iter).second)->RunBang(bang, argument.c_str());
				}
			}
		}
		else
		{
			Log(LOG_ERROR, L"Bang: Incorrect number of arugments");
		}
	}
}

/*
** BangGroupWithArgs
**
** Parses Bang args for Group
**
*/
void BangGroupWithArgs(BANGCOMMAND bang, const WCHAR* arg, size_t numOfArgs)
{
	if (Rainmeter)
	{
		std::vector<std::wstring> subStrings = CRainmeter::ParseString(arg);

		if (subStrings.size() > numOfArgs)
		{
			std::multimap<int, CMeterWindow*> windows;
			Rainmeter->GetMeterWindowsByLoadOrder(windows, subStrings[numOfArgs]);

			std::multimap<int, CMeterWindow*>::const_iterator iter = windows.begin();
			for (; iter != windows.end(); ++iter)
			{
				std::wstring argument = L"\"";
				for (size_t i = 0; i < numOfArgs; ++i)
				{
					argument += subStrings[i];
					argument += L"\" \"";
				}
				argument += (*iter).second->GetSkinName();
				argument += L"\"";
				BangWithArgs(bang, argument.c_str(), numOfArgs);
			}
		}
		else
		{
			Log(LOG_ERROR, L"BangGroup: Incorrect number of arguments");
		}
	}
}

/*
** RainmeterActivateConfig
**
** Callback for the !RainmeterActivateConfig bang
**
*/
void RainmeterActivateConfig(const WCHAR* arg)
{
	if (Rainmeter)
	{
		std::vector<std::wstring> subStrings = CRainmeter::ParseString(arg);

		if (subStrings.size() > 1)
		{
			std::pair<int, int> indexes = Rainmeter->GetMeterWindowIndex(subStrings[0], subStrings[1]);
			if (indexes.first != -1 && indexes.second != -1)
			{
				Rainmeter->ActivateConfig(indexes.first, indexes.second);
				return;
			}
			LogWithArgs(LOG_ERROR, L"!ActivateConfig: \"%s\\%s\" not found", subStrings[0].c_str(), subStrings[1].c_str());
		}
		else
		{
			// If we got this far, something went wrong
			Log(LOG_ERROR, L"!ActivateConfig: Invalid parameters");
		}
	}
}

/*
** RainmeterDeactivateConfig
**
** Callback for the !RainmeterDeactivateConfig bang
**
*/
void RainmeterDeactivateConfig(const WCHAR* arg)
{
	if (Rainmeter)
	{
		std::vector<std::wstring> subStrings = CRainmeter::ParseString(arg);

		if (!subStrings.empty())
		{
			CMeterWindow* mw = Rainmeter->GetMeterWindow(subStrings[0]);
			if (mw)
			{
				Rainmeter->DeactivateConfig(mw, -1);
				return;
			}
			LogWithArgs(LOG_WARNING, L"!DeactivateConfig: \"%s\" not active", subStrings[0].c_str());
		}
		else
		{
			Log(LOG_ERROR, L"!DeactivateConfig: Invalid parameters");
		}
	}
}

/*
** RainmeterToggleConfig
**
** Callback for the !RainmeterToggleConfig bang
**
*/
void RainmeterToggleConfig(const WCHAR* arg)
{
	if (Rainmeter)
	{
		std::vector<std::wstring> subStrings = CRainmeter::ParseString(arg);

		if (subStrings.size() >= 2)
		{
			CMeterWindow* mw = Rainmeter->GetMeterWindow(subStrings[0]);
			if (mw)
			{
				Rainmeter->DeactivateConfig(mw, -1);
				return;
			}

			// If the config wasn't active, activate it
			RainmeterActivateConfig(arg);
		}
		else
		{
			Log(LOG_ERROR, L"!ToggleConfig: Invalid parameters");
		}
	}
}

/*
** RainmeterDeactivateConfigGroup
**
** Callback for the !RainmeterDeactivateConfigGroup bang
**
*/
void RainmeterDeactivateConfigGroup(const WCHAR* arg)
{
	if (Rainmeter)
	{
		std::vector<std::wstring> subStrings = CRainmeter::ParseString(arg);

		if (!subStrings.empty())
		{
			std::multimap<int, CMeterWindow*> windows;
			Rainmeter->GetMeterWindowsByLoadOrder(windows, subStrings[0]);

			std::multimap<int, CMeterWindow*>::const_iterator iter = windows.begin();
			for (; iter != windows.end(); ++iter)
			{
				Rainmeter->DeactivateConfig((*iter).second, -1);
			}
		}
		else
		{
			Log(LOG_ERROR, L"!DeactivateConfigGroup: Invalid parameters");
		}
	}
}

/*
** RainmeterRefreshApp
**
** Callback for the !RainmeterRefreshApp bang
**
*/
void RainmeterRefreshApp()
{
	if (Rainmeter)
	{
		// Refresh needs to be delayed since it crashes if done during Update()
		PostMessage(Rainmeter->GetTrayWindow()->GetWindow(), WM_TRAY_DELAYED_REFRESH_ALL, (WPARAM)NULL, (LPARAM)NULL);
	}
}

/*
** RainmeterAbout
**
** Callback for the !RainmeterAbout bang
**
*/
void RainmeterAbout(const WCHAR* arg)
{
	if (Rainmeter)
	{
		int tab = 0;
		if (arg)
		{
			if (_wcsnicmp(arg, L"Measures", 8) == 0)
			{
				tab = 1;
			}
			else if (_wcsnicmp(arg, L"Plugins", 7) == 0)
			{
				tab = 2;
			}
			else if (_wcsnicmp(arg, L"Version", 7) == 0)
			{
				tab = 3;
			}
		}

		CDialogAbout::Open(tab);
	}
}

/*
** RainmeterManage
**
** Callback for the !RainmeterManage bang
**
*/
void RainmeterManage(const WCHAR* arg)
{
	if (Rainmeter)
	{
		int tab = 0;
		if (arg)
		{
			if (_wcsnicmp(arg, L"Themes", 6) == 0)
			{
				tab = 1;
			}
			else if (_wcsnicmp(arg, L"Settings", 8) == 0)
			{
				tab = 2;
			}
		}

		CDialogManage::Open(tab);
	}
}

/*
** RainmeterSkinMenu
**
** Callback for the !RainmeterSkinMenu bang
**
*/
void RainmeterSkinMenu(const WCHAR* arg)
{
	if (Rainmeter)
	{
		std::vector<std::wstring> subStrings = CRainmeter::ParseString(arg);

		if (!subStrings.empty())
		{
			CMeterWindow* mw = Rainmeter->GetMeterWindow(subStrings[0]);
			if (mw)
			{
				POINT pos;
				GetCursorPos(&pos);
				Rainmeter->ShowContextMenu(pos, mw);
				return;
			}
			LogWithArgs(LOG_WARNING, L"!SkinMenu: \"%s\" not active", subStrings[0].c_str());
		}
		else
		{
			Log(LOG_ERROR, L"!SkinMenu: Invalid parameter");
		}
	}
}

/*
** RainmeterTrayMenu
**
** Callback for the !RainmeterTrayMenu bang
**
*/
void RainmeterTrayMenu()
{
	if (Rainmeter)
	{
		POINT pos;
		GetCursorPos(&pos);
		Rainmeter->ShowContextMenu(pos, NULL);
	}
}

/*
** RainmeterResetStats
**
** Callback for the !RainmeterResetStats bang
**
*/
void RainmeterResetStats()
{
	if (Rainmeter)
	{
		Rainmeter->ResetStats();
	}
}

/*
** RainmeterWriteKeyValue
**
** Callback for the !RainmeterWriteKeyValue bang
**
*/
void RainmeterWriteKeyValue(const WCHAR* arg)
{
	if (Rainmeter)
	{
		std::vector<std::wstring> subStrings = CRainmeter::ParseString(arg);

		if (subStrings.size() > 3)
		{
			const std::wstring& iniFile = subStrings[3];

			if (iniFile.find(L"..\\") != std::string::npos || iniFile.find(L"../") != std::string::npos)
			{
				LogWithArgs(LOG_ERROR, L"!WriteKeyValue: Illegal path: %s", iniFile.c_str());
				return;
			}

			const std::wstring& skinPath = Rainmeter->GetSkinPath();
			const std::wstring settingsPath = Rainmeter->GetSettingsPath();

			if (_wcsnicmp(iniFile.c_str(), skinPath.c_str(), skinPath.size()) != 0 &&
				_wcsnicmp(iniFile.c_str(), settingsPath.c_str(), settingsPath.size()) != 0)
			{
				LogWithArgs(LOG_ERROR, L"!WriteKeyValue: Illegal path: %s", iniFile.c_str());
				return;
			}

			// Verify whether the file exists
			if (_waccess(iniFile.c_str(), 0) == -1)
			{
				LogWithArgs(LOG_ERROR, L"!WriteKeyValue: File not found: %s", iniFile.c_str());
				return;
			}

			// Verify whether the file is read-only
			DWORD attr = GetFileAttributes(iniFile.c_str());
			if (attr == -1 || (attr & FILE_ATTRIBUTE_READONLY))
			{
				LogWithArgs(LOG_WARNING, L"!WriteKeyValue: File is read-only: %s", iniFile.c_str());
				return;
			}

			// Avoid "IniFileMapping"
			std::vector<std::wstring> iniFileMappings;
			CSystem::GetIniFileMappingList(iniFileMappings);
			std::wstring iniWrite = CSystem::GetTemporaryFile(iniFileMappings, iniFile);
			if (iniWrite == L"<>")  // error occurred
			{
				return;
			}

			bool temporary = !iniWrite.empty();

			if (temporary)
			{
				if (Rainmeter->GetDebug()) LogWithArgs(LOG_DEBUG, L"!WriteKeyValue: Writing to: %s (Temp: %s)", iniFile.c_str(), iniWrite.c_str());
			}
			else
			{
				if (Rainmeter->GetDebug()) LogWithArgs(LOG_DEBUG, L"!WriteKeyValue: Writing to: %s", iniFile.c_str());
				iniWrite = iniFile;
			}

			const std::wstring& strSection = subStrings[0];
			const std::wstring& strKey = subStrings[1];
			const std::wstring& strValue = subStrings[2];

			bool formula = false;
			BOOL write = 0;

			if (subStrings.size() > 4)
			{
				CMeterWindow* mw = Rainmeter->GetMeterWindow(subStrings[4]);
				if (mw)
				{
					double value;
					formula = mw->GetParser().ReadFormula(strValue, &value);

					// Formula read fine
					if (formula)
					{
						WCHAR buffer[256];
						int len = _snwprintf_s(buffer, _TRUNCATE, L"%.5f", value);
						CMeasure::RemoveTrailingZero(buffer, len);

						const std::wstring& resultString = buffer;

						write = WritePrivateProfileString(strSection.c_str(), strKey.c_str(), resultString.c_str(), iniWrite.c_str());
					}
				}
			}

			if (!formula)
			{
				write = WritePrivateProfileString(strSection.c_str(), strKey.c_str(), strValue.c_str(), iniWrite.c_str());
			}

			if (temporary)
			{
				if (write != 0)
				{
					WritePrivateProfileString(NULL, NULL, NULL, iniWrite.c_str());  // FLUSH

					// Copy the file back
					if (!CSystem::CopyFiles(iniWrite, iniFile))
					{
						LogWithArgs(LOG_ERROR, L"!WriteKeyValue: Failed to copy temporary file to original filepath: %s (Temp: %s)", iniFile.c_str(), iniWrite.c_str());
					}
				}
				else  // failed
				{
					LogWithArgs(LOG_ERROR, L"!WriteKeyValue: Failed to write to: %s (Temp: %s)", iniFile.c_str(), iniWrite.c_str());
				}

				// Remove a temporary file
				CSystem::RemoveFile(iniWrite);
			}
			else
			{
				if (write == 0)  // failed
				{
					LogWithArgs(LOG_ERROR, L"!WriteKeyValue: Failed to write to: %s", iniFile.c_str());
				}
			}
		}
		else
		{
			Log(LOG_ERROR, L"!WriteKeyValue: Invalid parameters");
		}
	}
}

/*
** RainmeterQuit
**
** Callback for the !RainmeterQuit bang
**
*/
void RainmeterQuit()
{
	if (Rainmeter)
	{
		// Quit needs to be delayed since it crashes if done during Update()
		PostMessage(Rainmeter->GetTrayWindow()->GetWindow(), WM_COMMAND, MAKEWPARAM(ID_CONTEXT_QUIT, 0), (LPARAM)NULL);
	}
}


// -----------------------------------------------------------------------------------------------
//
//                                The class starts here
//
// -----------------------------------------------------------------------------------------------

/*
** CRainmeter
**
** Constructor
**
*/
CRainmeter::CRainmeter() :
	m_TrayWindow(),
	m_Debug(false),
	m_DisableVersionCheck(false),
	m_NewVersion(false),
	m_DesktopWorkAreaChanged(false),
	m_DesktopWorkAreaType(false),
	m_MenuActive(false),
	m_DisableRDP(false),
	m_DisableDragging(false),
	m_Logging(false),
	m_CurrentParser(),
	m_Instance(),
	m_ResourceInstance(),
	m_GDIplusToken(),
	m_GlobalConfig()
{
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	InitCommonControls();

	// Initialize GDI+.
	GdiplusStartupInput gdiplusStartupInput;
	GdiplusStartup(&m_GDIplusToken, &gdiplusStartupInput, NULL);
}

/*
** ~CRainmeter
**
** Destructor
**
*/
CRainmeter::~CRainmeter()
{
	DeleteMeterWindow(NULL, false);	// This removes the window from the vector

	if (m_TrayWindow) delete m_TrayWindow;

	CSystem::Finalize();

	CMeasureNet::UpdateIFTable();
	CMeasureNet::UpdateStats();
	WriteStats(true);

	CMeasureNet::FinalizeNewApi();

	CMeterString::FreeFontCache();

	// Change the work area back
	if (m_DesktopWorkAreaChanged)
	{
		UpdateDesktopWorkArea(true);
	}

	FinalizeLitestep();

	if (m_ResourceInstance) FreeLibrary(m_ResourceInstance);

	CoUninitialize();

	GdiplusShutdown(m_GDIplusToken);
}

/*
** Initialize
**
** The main initialization function for the module.
** May throw CErrors !!!!
**
*/
int CRainmeter::Initialize(HWND hParent, HINSTANCE hInstance, LPCWSTR szPath)
{
	int result = 0;

	m_Instance = hInstance;

	WCHAR* tmpSzPath = new WCHAR[MAX_LINE_LENGTH];
	GetModuleFileName(m_Instance, tmpSzPath, MAX_LINE_LENGTH);

	// Remove the module's name from the path
	WCHAR* pos = wcsrchr(tmpSzPath, L'\\');
	if (pos)
	{
		*(pos + 1) = L'\0';
	}
	else
	{
		tmpSzPath[0] = L'\0';
	}

	m_Path = tmpSzPath;

	InitalizeLitestep();

	bool bDefaultIniLocation = false;

	if (*szPath)
	{
		// The command line defines the location of Rainmeter.ini (or whatever it calls it).
		std::wstring iniFile = szPath;
		if (iniFile[0] == L'\"')
		{
			if (iniFile.length() == 1)
			{
				iniFile.clear();
			}
			else if (iniFile[iniFile.length() - 1] == L'\"')
			{
				iniFile.assign(iniFile, 1, iniFile.length() - 2);
			}
		}

		ExpandEnvironmentVariables(iniFile);

		if (iniFile.empty() || iniFile[iniFile.length() - 1] == L'\\')
		{
			iniFile += L"Rainmeter.ini";
		}
		else if (iniFile.length() <= 4 || _wcsicmp(iniFile.substr(iniFile.length() - 4).c_str(), L".ini") != 0)
		{
			iniFile += L"\\Rainmeter.ini";
		}

		if (iniFile[0] != L'\\' && iniFile[0] != L'/' && iniFile.find_first_of(L':') == std::wstring::npos)
		{
			// Make absolute path
			iniFile.insert(0, m_Path);
		}

		m_IniFile = iniFile;

		// If the ini file doesn't exist, create a default Rainmeter.ini file.
		if (_waccess(m_IniFile.c_str(), 0) == -1)
		{
			CreateDefaultConfigFile(m_IniFile);
		}
		bDefaultIniLocation = true;
	}
	else
	{
		m_IniFile = m_Path;
		m_IniFile += L"Rainmeter.ini";

		// If the ini file doesn't exist in the program folder store it to the %APPDATA% instead so that things work better in Vista/Win7
		if (_waccess(m_IniFile.c_str(), 0) == -1)
		{
			m_IniFile = L"%APPDATA%\\Rainmeter\\Rainmeter.ini";
			ExpandEnvironmentVariables(m_IniFile);
			bDefaultIniLocation = true;

			// If the ini file doesn't exist in the %APPDATA% either, create a default Rainmeter.ini file.
			if (_waccess(m_IniFile.c_str(), 0) == -1)
			{
				CreateDefaultConfigFile(m_IniFile);
			}
		}
	}

	// Set the log file and stats file location
	m_LogFile = m_StatsFile = m_IniFile;
	size_t logFileLen = m_LogFile.length();
	if (logFileLen > 4 && _wcsicmp(m_LogFile.substr(logFileLen - 4).c_str(), L".ini") == 0)
	{
		m_LogFile.replace(logFileLen - 4, 4, L".log");
		m_StatsFile.replace(logFileLen - 4, 4, L".stats");
	}
	else
	{
		m_LogFile += L".log";	// Append the extension so that we don't accidentally overwrite the ini file
		m_StatsFile += L".stats";
	}

	// Determine the language resource to load
	std::wstring resource = m_Path + L"Languages\\";
	if (GetPrivateProfileString(L"Rainmeter", L"Language", L"", tmpSzPath, MAX_LINE_LENGTH, m_IniFile.c_str()) == 0)
	{
		// Use whatever the user selected for the installer
		DWORD size = MAX_LINE_LENGTH;
		HKEY hKey;
		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"Software\\Rainmeter", 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS)
		{
			DWORD type = 0;
			if (RegQueryValueEx(hKey, L"Language", NULL, &type, (LPBYTE)tmpSzPath, (LPDWORD)&size) != ERROR_SUCCESS ||
				type != REG_SZ)
			{
				tmpSzPath = L'\0';
			}
			RegCloseKey(hKey);
		}
	}
	m_ResourceLCID = wcstoul(tmpSzPath, NULL, 10);
	resource += tmpSzPath;
	resource += L".dll";

	m_ResourceInstance = LoadLibraryEx(resource.c_str(), NULL, DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_DATAFILE);
	if (!m_ResourceInstance)
	{
		resource.insert(0, L"Unable to load language: ");
		Log(LOG_ERROR, resource.c_str());

		// Try English
		resource = m_Path + L"Languages\\1033.dll";
		m_ResourceInstance = LoadLibraryEx(resource.c_str(), NULL, DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_DATAFILE);
		m_ResourceLCID = 1033;
		if (!m_ResourceInstance)
		{
			throw CError(L"Unable to load language library");
		}
	}

	// Read Logging settings beforehand
	m_Logging = 0!=GetPrivateProfileInt(L"Rainmeter", L"Logging", 0, m_IniFile.c_str());
	m_Debug = 0!=GetPrivateProfileInt(L"Rainmeter", L"Debug", 0, m_IniFile.c_str());

	if (m_Logging)
	{
		StartLogging();
	}

	m_PluginPath = m_AddonPath = m_SkinPath = m_Path;
	m_PluginPath += L"Plugins\\";
	m_AddonPath += L"Addons\\";
	m_SkinPath += L"Skins\\";

	// Read the skin folder from the ini file
	if (GetPrivateProfileString(L"Rainmeter", L"SkinPath", L"", tmpSzPath, MAX_LINE_LENGTH, m_IniFile.c_str()) > 0)
	{
		m_SkinPath = tmpSzPath;
		ExpandEnvironmentVariables(m_SkinPath);

		if (!m_SkinPath.empty())
		{
			WCHAR ch = m_SkinPath[m_SkinPath.size() - 1];
			if (ch != L'\\' && ch != L'/')
			{
				m_SkinPath += L"\\";
			}
		}
	}
	else if (bDefaultIniLocation)
	{
		// If the skin path is not defined in the Rainmeter.ini file use My Documents/Rainmeter/Skins
		tmpSzPath[0] = L'\0';
		HRESULT hr = SHGetFolderPath(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, tmpSzPath);
		if (SUCCEEDED(hr))
		{
			// Make the folders if they don't exist yet
			m_SkinPath = tmpSzPath;
			m_SkinPath += L"\\Rainmeter";
			CreateDirectory(m_SkinPath.c_str(), NULL);
			m_SkinPath += L"\\Skins\\";
			DWORD result = CreateDirectory(m_SkinPath.c_str(), NULL);
			if (result != 0)
			{
				// The folder was created successfully which means that it wasn't available yet.
				// Copy the default skin to the Skins folder
				std::wstring strFrom(m_Path + L"Skins\\*.*");
				std::wstring strTo(m_SkinPath);
				CSystem::CopyFiles(strFrom, strTo);

				// This shouldn't be copied
				std::wstring strNote = strTo + L"Read me before copying skins here.txt";
				CSystem::RemoveFile(strNote);

				// Copy also the themes to the %APPDATA%
				strFrom = std::wstring(m_Path + L"Themes\\*.*");
				strTo = std::wstring(GetSettingsPath() + L"Themes\\");
				CreateDirectory(strTo.c_str(), NULL);
				CSystem::CopyFiles(strFrom, strTo);
			}
		}
		else
		{
			Log(LOG_WARNING, L"Documents folder not found");
		}

		WritePrivateProfileString(L"Rainmeter", L"SkinPath", m_SkinPath.c_str(), m_IniFile.c_str());
	}

	delete [] tmpSzPath;
	tmpSzPath = NULL;

	LogWithArgs(LOG_NOTICE, L"Path: %s", m_Path.c_str());
	LogWithArgs(LOG_NOTICE, L"IniFile: %s", m_IniFile.c_str());
	LogWithArgs(LOG_NOTICE, L"SkinPath: %s", m_SkinPath.c_str());

	// Extract volume path from program path
	// E.g.:
	//  "C:\path\" to "C:"
	//  "\\server\share\" to "\\server\share"
	//  "\\server\C:\path\" to "\\server\C:"
	std::wstring::size_type loc;
	if ((loc = m_Path.find_first_of(L':')) != std::wstring::npos)
	{
		m_Drive.assign(m_Path, 0, loc + 1);
	}
	else if (m_Path.length() >= 2 && (m_Path[0] == L'\\' || m_Path[0] == L'/') && (m_Path[1] == L'\\' || m_Path[1] == L'/'))
	{
		if ((loc = m_Path.find_first_of(L"\\/", 2)) != std::wstring::npos)
		{
			std::wstring::size_type loc2;
			if ((loc2 = m_Path.find_first_of(L"\\/", loc + 1)) != std::wstring::npos || loc != (m_Path.length() - 1))
			{
				loc = loc2;
			}
		}
		m_Drive.assign(m_Path, 0, loc);
	}

	// Test that the Rainmeter.ini file is writable
	TestSettingsFile(bDefaultIniLocation);

	CSystem::Initialize(hInstance);
	CMeasureNet::InitializeNewApi();

	if (m_Debug)
	{
		Log(LOG_DEBUG, L"Enumerating installed font families...");
		CMeterString::EnumerateInstalledFontFamilies();
	}

	// Tray must exist before configs are read
	m_TrayWindow = new CTrayWindow(m_Instance);

	ScanForConfigs(m_SkinPath);
	ScanForThemes(GetSettingsPath() + L"Themes");

	if (m_ConfigStrings.empty())
	{
		std::wstring error = GetFormattedString(ID_STR_NOAVAILABLESKINS, m_SkinPath.c_str());
		MessageBox(NULL, error.c_str(), APPNAME, MB_OK | MB_TOPMOST | MB_ICONERROR);
	}

	ReadGeneralSettings(m_IniFile);

	WritePrivateProfileString(L"Rainmeter", L"CheckUpdate", NULL , m_IniFile.c_str());

	if (!m_DisableVersionCheck)
	{
		CheckUpdate();
	}

	ResetStats();
	ReadStats();

	// Change the work area if necessary
	if (m_DesktopWorkAreaChanged)
	{
		UpdateDesktopWorkArea(false);
	}

	// Create meter windows for active configs
	ActivateActiveConfigs();

	return result;	// Alles OK
}

/*
** CreateDefaultConfigFile
**
** Creates the default Rainmeter.ini file with illustro\System enabled.
**
*/
void CRainmeter::CreateDefaultConfigFile(const std::wstring& strFile)
{
	size_t pos = strFile.find_last_of(L'\\');
	if (pos != std::wstring::npos)
	{
		std::wstring strPath(strFile, 0, pos);
		CreateDirectory(strPath.c_str(), NULL);
	}

	std::wstring defaultIni = GetPath() + L"Default.ini";
	if (_waccess(defaultIni.c_str(), 0) == -1)
	{
		WritePrivateProfileString(L"Rainmeter", L"\r\n[illustro\\System]\r\nActive", L"1", strFile.c_str());
	}
	else
	{
		CSystem::CopyFiles(defaultIni, GetIniFile());
	}
}

void CRainmeter::ReloadSettings()
{
	ScanForConfigs(m_SkinPath);
	ScanForThemes(GetSettingsPath() + L"Themes");
	ReadGeneralSettings(m_IniFile);
}

void CRainmeter::ActivateActiveConfigs()
{
	std::multimap<int, int>::const_iterator iter = m_ConfigOrders.begin();
	for ( ; iter != m_ConfigOrders.end(); ++iter)
	{
		const CONFIG& config = m_ConfigStrings[(*iter).second];
		if (config.active > 0 && config.active <= (int)config.iniFiles.size())
		{
			ActivateConfig((*iter).second, config.active - 1);
		}
	}
}

void CRainmeter::ActivateConfig(int configIndex, int iniIndex)
{
	if (configIndex >= 0 && configIndex < (int)m_ConfigStrings.size())
	{
		const std::wstring skinIniFile = m_ConfigStrings[configIndex].iniFiles[iniIndex];
		const std::wstring skinConfig = m_ConfigStrings[configIndex].config;
		const std::wstring& skinPath = m_SkinPath;

		// Verify that the config is not already active
		std::map<std::wstring, CMeterWindow*>::const_iterator iter = m_Meters.find(skinConfig);
		if (iter != m_Meters.end())
		{
			if (((*iter).second)->GetSkinIniFile() == skinIniFile)
			{
				LogWithArgs(LOG_WARNING, L"!ActivateConfig: \"%s\" already active", skinConfig.c_str());
				return;
			}
			else
			{
				// Deactivate the existing config
				DeactivateConfig((*iter).second, configIndex);
			}
		}

		// Verify whether the ini-file exists
		std::wstring skinIniPath = skinPath + skinConfig;
		skinIniPath += L"\\";
		skinIniPath += skinIniFile;

		if (_waccess(skinIniPath.c_str(), 0) == -1)
		{
			std::wstring message = GetFormattedString(ID_STR_UNABLETOACTIVATESKIN, skinConfig.c_str(), skinIniFile.c_str());
			MessageBox(NULL, message.c_str(), APPNAME, MB_OK | MB_TOPMOST | MB_ICONEXCLAMATION);
			return;
		}

		m_ConfigStrings[configIndex].active = iniIndex + 1;
		WriteActive(skinConfig, iniIndex);

		CreateMeterWindow(skinPath, skinConfig, skinIniFile);
	}
}

bool CRainmeter::DeactivateConfig(CMeterWindow* meterWindow, int configIndex, bool save)
{
	if (configIndex >= 0 && configIndex < (int)m_ConfigStrings.size())
	{
		m_ConfigStrings[configIndex].active = 0;	// Deactivate the config
	}
	else if (configIndex == -1 && meterWindow)
	{
		// Deactivate the config by using the meter window's config name
		const std::wstring skinConfig = meterWindow->GetSkinName();
		for (size_t i = 0, isize = m_ConfigStrings.size(); i < isize; ++i)
		{
			if (_wcsicmp(skinConfig.c_str(), m_ConfigStrings[i].config.c_str()) == 0)
			{
				m_ConfigStrings[i].active = 0;
				break;
			}
		}
	}

	if (meterWindow)
	{
		if (save)
		{
			// Disable the config in the ini-file
			WriteActive(meterWindow->GetSkinName(), -1);
		}

		return DeleteMeterWindow(meterWindow, true);
	}
	return false;
}

void CRainmeter::WriteActive(const std::wstring& config, int iniIndex)
{
	WCHAR buffer[32];
	_snwprintf_s(buffer, _TRUNCATE, L"%i", iniIndex + 1);
	WritePrivateProfileString(config.c_str(), L"Active", buffer, m_IniFile.c_str());
}

void CRainmeter::CreateMeterWindow(const std::wstring& path, const std::wstring& config, const std::wstring& iniFile)
{
	CMeterWindow* mw = new CMeterWindow(path, config, iniFile);

	if (mw)
	{
		m_Meters[config] = mw;

		try
		{
			mw->Initialize(*this);

			CDialogAbout::UpdateSkins();
			CDialogManage::UpdateSkins(mw);
		}
		catch (CError& error)
		{
			DeactivateConfig(mw, -1);
			LogError(error);
		}
	}
}

void CRainmeter::ClearDeleteLaterList()
{
	if (!m_DelayDeleteList.empty())
	{
		do
		{
			CMeterWindow* meterWindow = m_DelayDeleteList.front();

			// Remove from the delete later list
			m_DelayDeleteList.remove(meterWindow);

			// Remove from the meter window list if it is still there
			std::map<std::wstring, CMeterWindow*>::iterator iter = m_Meters.begin();
			for (; iter != m_Meters.end(); ++iter)
			{
				if ((*iter).second == meterWindow)
				{
					m_Meters.erase(iter);
					break;
				}
			}

			CDialogManage::UpdateSkins(meterWindow, true);
			delete meterWindow;
		}
		while (!m_DelayDeleteList.empty());

		CDialogAbout::UpdateSkins();
	}
}

bool CRainmeter::DeleteMeterWindow(CMeterWindow* meterWindow, bool bLater)
{
	if (bLater)
	{
		if (meterWindow)
		{
			m_DelayDeleteList.push_back(meterWindow);
			meterWindow->RunBang(BANG_HIDEFADE, NULL);	// Fade out the window
		}
	}
	else
	{
		if (meterWindow)
		{
			m_DelayDeleteList.remove(meterWindow);	// Remove the window from the delete later list if it is there
		}

		std::map<std::wstring, CMeterWindow*>::iterator iter = m_Meters.begin();
		for (; iter != m_Meters.end(); ++iter)
		{
			if (meterWindow == NULL)
			{
				// Delete all meter windows
				delete (*iter).second;
			}
			else if ((*iter).second == meterWindow)
			{
				m_Meters.erase(iter);
				delete meterWindow;

				return true;
			}
		}

		if (meterWindow == NULL)
		{
			m_Meters.clear();
		}
	}

	return false;
}

CMeterWindow* CRainmeter::GetMeterWindow(const std::wstring& config)
{
	std::map<std::wstring, CMeterWindow*>::const_iterator iter = m_Meters.begin();
	for (; iter != m_Meters.end(); ++iter)
	{
		if (_wcsicmp((*iter).first.c_str(), config.c_str()) == 0)
		{
			return (*iter).second;
		}
	}

	return NULL;
}

// Added by Peter Souza IV / psouza4 / 2010.12.13
//
// Returns a CMeterWindow object given a config's INI path and filename.  Since plugins
// get the full path and filename of an INI file on Initialize(), but not the name of
// the config, this is used to convert the INI filename to a config name.
CMeterWindow* CRainmeter::GetMeterWindowByINI(const std::wstring& ini_searching)
{
	if (_wcsnicmp(m_SkinPath.c_str(), ini_searching.c_str(), m_SkinPath.length()) == 0)
	{
		const std::wstring config_searching = ini_searching.substr(m_SkinPath.length());

		std::map<std::wstring, CMeterWindow*>::const_iterator iter = m_Meters.begin();
		for (; iter != m_Meters.end(); ++iter)
		{
			std::wstring config_current = (*iter).second->GetSkinName() + L"\\";
			config_current += (*iter).second->GetSkinIniFile();

			if (_wcsicmp(config_current.c_str(), config_searching.c_str()) == 0)
			{
				return (*iter).second;
			}
		}
	}

	return NULL;
}

std::pair<int, int> CRainmeter::GetMeterWindowIndex(const std::wstring& config, const std::wstring& iniFile)
{
	std::pair<int, int> indexes;

	for (int i = 0, isize = (int)m_ConfigStrings.size(); i < isize; ++i)
	{
		if (_wcsicmp(m_ConfigStrings[i].config.c_str(), config.c_str()) == 0)
		{
			for (int j = 0, jsize = (int)m_ConfigStrings[i].iniFiles.size(); j < jsize; ++j)
			{
				if (_wcsicmp(m_ConfigStrings[i].iniFiles[j].c_str(), iniFile.c_str()) == 0)
				{
					indexes = std::make_pair(i, j);
					return indexes;
				}
			}
		}
	}

	indexes = std::make_pair(-1, -1);  // error
	return indexes;
}

CMeterWindow* CRainmeter::GetMeterWindow(HWND hwnd)
{
	std::map<std::wstring, CMeterWindow*>::const_iterator iter = m_Meters.begin();
	for (; iter != m_Meters.end(); ++iter)
	{
		if ((*iter).second->GetWindow() == hwnd)
		{
			return (*iter).second;
		}
	}

	return NULL;
}

void CRainmeter::GetMeterWindowsByLoadOrder(std::multimap<int, CMeterWindow*>& windows, const std::wstring& group)
{
	std::map<std::wstring, CMeterWindow*>::const_iterator iter = m_Meters.begin();
	for (; iter != m_Meters.end(); ++iter)
	{
		CMeterWindow* mw = (*iter).second;
		if (mw && (group.empty() || mw->BelongsToGroup(group)))
		{
			windows.insert(std::pair<int, CMeterWindow*>(GetLoadOrder((*iter).first), mw));
		}
	}
}

void CRainmeter::SetLoadOrder(int configIndex, int order)
{
	std::multimap<int, int>::iterator iter = m_ConfigOrders.begin();
	for ( ; iter != m_ConfigOrders.end(); ++iter)
	{
		if ((*iter).second == configIndex)  // already exists
		{
			if ((*iter).first != order)
			{
				m_ConfigOrders.erase(iter);
				break;
			}
			else
			{
				return;
			}
		}
	}

	m_ConfigOrders.insert(std::pair<int, int>(order, configIndex));
}

int CRainmeter::GetLoadOrder(const std::wstring& config)
{
	std::multimap<int, int>::const_iterator iter = m_ConfigOrders.begin();
	for ( ; iter != m_ConfigOrders.end(); ++iter)
	{
		if (m_ConfigStrings[(*iter).second].config == config)
		{
			return (*iter).first;
		}
	}

	// LoadOrder not specified
	return 0;
}

/*
** ScanForConfigs
**
** Scans all the subfolders and locates the ini-files.
*/
void CRainmeter::ScanForConfigs(const std::wstring& path)
{
	m_ConfigStrings.clear();
	m_ConfigMenu.clear();
	m_ConfigOrders.clear();

	ScanForConfigsRecursive(path, L"", 0, m_ConfigMenu, false);
}

int CRainmeter::ScanForConfigsRecursive(const std::wstring& path, std::wstring base, int index, std::vector<CONFIGMENU>& menu, bool DontRecurse)
{
	WIN32_FIND_DATA fileData;      // Data structure describes the file found
	HANDLE hSearch;                // Search handle returned by FindFirstFile
	std::list<std::wstring> folders;
	const bool first = base.empty();

	// Scan all .ini files and folders from the subfolder
	std::wstring filter = path + base;
	filter += L"\\*";

	hSearch = FindFirstFileEx(
		filter.c_str(),
		(CSystem::GetOSPlatform() >= OSPLATFORM_7) ? FindExInfoBasic : FindExInfoStandard,
		&fileData,
		FindExSearchNameMatch,
		NULL,
		0);

	if (hSearch != INVALID_HANDLE_VALUE)
	{
		CONFIG config;
		config.config = base;
		config.active = 0;

		do
		{
			if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (!(wcscmp(L"Backup", fileData.cFileName) == 0 && first) &&		// Skip the backup folder
					wcscmp(L".", fileData.cFileName) != 0 &&
					wcscmp(L"..", fileData.cFileName) != 0)
				{
					folders.push_back(fileData.cFileName);
				}
			}
			else if (!first)
			{
				// Check whether the extension is ".ini"
				size_t filenameLen = wcslen(fileData.cFileName);
				if (filenameLen >= 4 && _wcsicmp(fileData.cFileName + (filenameLen - 4), L".ini") == 0)
				{
					CONFIGMENU menuItem;
					menuItem.name = fileData.cFileName;
					menuItem.index = m_ConfigStrings.size();
					menu.push_back(menuItem);

					config.iniFiles.push_back(fileData.cFileName);
					config.commands.push_back(ID_CONFIG_FIRST + index++);
				}
			}
		} while (FindNextFile(hSearch, &fileData));

		FindClose(hSearch);

		if (!config.iniFiles.empty())
		{
			m_ConfigStrings.push_back(config);
		}
	}

	if (!first)
	{
		base += L"\\";
	}

	std::list<std::wstring>::const_iterator iter = folders.begin();
	for ( ; iter != folders.end(); ++iter)
	{
		CONFIGMENU menuItem;
		menuItem.name = (*iter);
		menuItem.index = -1;
		menu.push_back(menuItem);

		if (!DontRecurse)
		{
			std::vector<CONFIGMENU>::iterator iter2 = menu.end() - 1;
			index = ScanForConfigsRecursive(path, base + (*iter), index, (*iter2).children, false);

			// Remove menu item if it has no child
			if ((*iter2).children.empty())
			{
				menu.erase(iter2);
			}
		}
	}

	return index;
}

/*
** ScanForThemes
**
** Scans the given folder for themes
*/
void CRainmeter::ScanForThemes(const std::wstring& path)
{
	m_Themes.clear();

	WIN32_FIND_DATA fileData;      // Data structure describes the file found
	HANDLE hSearch;                // Search handle returned by FindFirstFile

	// Scan for folders
	std::wstring folders = path + L"\\*";

	hSearch = FindFirstFileEx(
		folders.c_str(),
		(CSystem::GetOSPlatform() >= OSPLATFORM_7) ? FindExInfoBasic : FindExInfoStandard,
		&fileData,
		FindExSearchNameMatch,
		NULL,
		0);

	if (hSearch != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
				wcscmp(L".", fileData.cFileName) != 0 &&
				wcscmp(L"..", fileData.cFileName) != 0)
			{
				m_Themes.push_back(fileData.cFileName);
			}
		} while (FindNextFile(hSearch, &fileData));

		FindClose(hSearch);
	}
}

BOOL CRainmeter::ExecuteBang(const std::wstring& bang, const std::wstring& arg, CMeterWindow* meterWindow)
{
	// Skip "!Rainmeter" or "!"
	LPCWSTR name = bang.c_str();
	name += (_wcsnicmp(name, L"!Rainmeter", 10) == 0) ? 10 : 1;

	if (_wcsicmp(name, L"Refresh") == 0)
	{
		BangWithArgs(BANG_REFRESH, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"RefreshApp") == 0)
	{
		RainmeterRefreshApp();
	}
	else if (_wcsicmp(name, L"Redraw") == 0)
	{
		BangWithArgs(BANG_REDRAW, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"Update") == 0)
	{
		BangWithArgs(BANG_UPDATE, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"Hide") == 0)
	{
		BangWithArgs(BANG_HIDE, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"Show") == 0)
	{
		BangWithArgs(BANG_SHOW, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"Toggle") == 0)
	{
		BangWithArgs(BANG_TOGGLE, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"HideFade") == 0)
	{
		BangWithArgs(BANG_HIDEFADE, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"ShowFade") == 0)
	{
		BangWithArgs(BANG_SHOWFADE, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"ToggleFade") == 0)
	{
		BangWithArgs(BANG_TOGGLEFADE, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"HideMeter") == 0)
	{
		BangWithArgs(BANG_HIDEMETER, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"ShowMeter") == 0)
	{
		BangWithArgs(BANG_SHOWMETER, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"ToggleMeter") == 0)
	{
		BangWithArgs(BANG_TOGGLEMETER, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"MoveMeter") == 0)
	{
		BangWithArgs(BANG_MOVEMETER, arg.c_str(), 3);
	}
	else if (_wcsicmp(name, L"UpdateMeter") == 0)
	{
		BangWithArgs(BANG_UPDATEMETER, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"DisableMeasure") == 0)
	{
		BangWithArgs(BANG_DISABLEMEASURE, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"EnableMeasure") == 0)
	{
		BangWithArgs(BANG_ENABLEMEASURE, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"ToggleMeasure") == 0)
	{
		BangWithArgs(BANG_TOGGLEMEASURE, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"UpdateMeasure") == 0)
	{
		BangWithArgs(BANG_UPDATEMEASURE, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"CommandMeasure") == 0)
	{
		BangWithArgs(BANG_COMMANDMEASURE, arg.c_str(), 2);
	}
	else if (_wcsicmp(name, L"ShowBlur") == 0)
	{
		BangWithArgs(BANG_SHOWBLUR, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"HideBlur") == 0)
	{
		BangWithArgs(BANG_HIDEBLUR, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"ToggleBlur") == 0)
	{
		BangWithArgs(BANG_TOGGLEBLUR, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"AddBlur") == 0)
	{
		BangWithArgs(BANG_ADDBLUR, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"RemoveBlur") == 0)
	{
		BangWithArgs(BANG_REMOVEBLUR, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"ActivateConfig") == 0)
	{
		RainmeterActivateConfig(arg.c_str());
	}
	else if (_wcsicmp(name, L"DeactivateConfig") == 0)
	{
		RainmeterDeactivateConfig(arg.c_str());
	}
	else if (_wcsicmp(name, L"ToggleConfig") == 0)
	{
		RainmeterToggleConfig(arg.c_str());
	}
	else if (_wcsicmp(name, L"Move") == 0)
	{
		BangWithArgs(BANG_MOVE, arg.c_str(), 2);
	}
	else if (_wcsicmp(name, L"ZPos") == 0 || _wcsicmp(name, L"ChangeZPos") == 0)	// For backwards compatibility
	{
		BangWithArgs(BANG_ZPOS, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"ClickThrough") == 0)
	{
		BangWithArgs(BANG_CLICKTHROUGH, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"Draggable") == 0)
	{
		BangWithArgs(BANG_DRAGGABLE, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"SnapEdges") == 0)
	{
		BangWithArgs(BANG_SNAPEDGES, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"KeepOnScreen") == 0)
	{
		BangWithArgs(BANG_KEEPONSCREEN, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"SetTransparency") == 0)
	{
		BangWithArgs(BANG_SETTRANSPARENCY, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"SetVariable") == 0)
	{
		BangWithArgs(BANG_SETVARIABLE, arg.c_str(), 2);
	}
	else if (_wcsicmp(name, L"SetOption") == 0)
	{
		BangWithArgs(BANG_SETOPTION, arg.c_str(), 3);
	}
	else if (_wcsicmp(name, L"RefreshGroup") == 0)
	{
		BangGroupWithArgs(BANG_REFRESH, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"UpdateGroup") == 0)
	{
		BangGroupWithArgs(BANG_UPDATE, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"RedrawGroup") == 0)
	{
		BangGroupWithArgs(BANG_REDRAW, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"HideGroup") == 0)
	{
		BangGroupWithArgs(BANG_HIDE, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"ShowGroup") == 0)
	{
		BangGroupWithArgs(BANG_SHOW, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"ToggleGroup") == 0)
	{
		BangGroupWithArgs(BANG_TOGGLE, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"HideFadeGroup") == 0)
	{
		BangGroupWithArgs(BANG_HIDEFADE, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"ShowFadeGroup") == 0)
	{
		BangGroupWithArgs(BANG_SHOWFADE, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"ToggleFadeGroup") == 0)
	{
		BangGroupWithArgs(BANG_TOGGLEFADE, arg.c_str(), 0);
	}
	else if (_wcsicmp(name, L"HideMeterGroup") == 0)
	{
		BangWithArgs(BANG_HIDEMETERGROUP, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"ShowMeterGroup") == 0)
	{
		BangWithArgs(BANG_SHOWMETERGROUP, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"ToggleMeterGroup") == 0)
	{
		BangWithArgs(BANG_TOGGLEMETERGROUP, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"UpdateMeterGroup") == 0)
	{
		BangWithArgs(BANG_UPDATEMETERGROUP, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"DisableMeasureGroup") == 0)
	{
		BangWithArgs(BANG_DISABLEMEASUREGROUP, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"EnableMeasureGroup") == 0)
	{
		BangWithArgs(BANG_ENABLEMEASUREGROUP, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"ToggleMeasureGroup") == 0)
	{
		BangWithArgs(BANG_TOGGLEMEASUREGROUP, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"UpdateMeasureGroup") == 0)
	{
		BangWithArgs(BANG_UPDATEMEASUREGROUP, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"DeactivateConfigGroup") == 0)
	{
		RainmeterDeactivateConfigGroup(arg.c_str());
	}
	else if (_wcsicmp(name, L"ZPosGroup") == 0)
	{
		BangGroupWithArgs(BANG_ZPOS, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"ClickThroughGroup") == 0)
	{
		BangGroupWithArgs(BANG_CLICKTHROUGH, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"DraggableGroup") == 0)
	{
		BangGroupWithArgs(BANG_DRAGGABLE, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"SnapEdgesGroup") == 0)
	{
		BangGroupWithArgs(BANG_SNAPEDGES, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"KeepOnScreenGroup") == 0)
	{
		BangGroupWithArgs(BANG_KEEPONSCREEN, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"SetTransparencyGroup") == 0)
	{
		BangGroupWithArgs(BANG_SETTRANSPARENCY, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"SetVariableGroup") == 0)
	{
		BangGroupWithArgs(BANG_SETVARIABLE, arg.c_str(), 2);
	}
	else if (_wcsicmp(name, L"SetOptionGroup") == 0)
	{
		BangWithArgs(BANG_SETOPTIONGROUP, arg.c_str(), 3);
	}
	else if (_wcsicmp(name, L"About") == 0)
	{
		RainmeterAbout(arg.c_str());
	}
	else if (_wcsicmp(name, L"Manage") == 0)
	{
		RainmeterManage(arg.c_str());
	}
	else if (_wcsicmp(name, L"SkinMenu") == 0)
	{
		RainmeterSkinMenu(arg.c_str());
	}
	else if (_wcsicmp(name, L"TrayMenu") == 0)
	{
		RainmeterTrayMenu();
	}
	else if (_wcsicmp(name, L"ResetStats") == 0)
	{
		RainmeterResetStats();
	}
	else if (_wcsicmp(name, L"WriteKeyValue") == 0)
	{
		RainmeterWriteKeyValue(arg.c_str());
	}
	else if (_wcsicmp(name, L"PluginBang") == 0)
	{
		BangWithArgs(BANG_PLUGIN, arg.c_str(), 1);
	}
	else if (_wcsicmp(name, L"LsBoxHook") == 0)
	{
		// Nothing to do here (this works only with Litestep)
	}
	else if (_wcsicmp(name, L"Quit") == 0)
	{
		RainmeterQuit();
	}
	else if (_wcsicmp(bang.c_str(), L"!Execute") == 0)
	{
		// Special case for multibang execution
		std::wstring::size_type start = std::wstring::npos;
		std::wstring::size_type end = std::wstring::npos;
		int count = 0;
		for (size_t i = 0, isize = arg.size(); i < isize; ++i)
		{
			if (arg[i] == L'[')
			{
				if (count == 0)
				{
					start = i;
				}
				++count;
			}
			else if (arg[i] == L']')
			{
				--count;

				if (count == 0 && start != std::wstring::npos)
				{
					end = i;

					std::wstring command = arg.substr(start + 1, end - (start + 1));
					// trim leading whitespace
					std::wstring::size_type notwhite = command.find_first_not_of(L" \t\r\n");
					command.erase(0, notwhite);
					ExecuteCommand(command.c_str(), meterWindow);
				}
			}
			else if (isize > (i + 2) &&
				arg[i] == L'"' && arg[i + 1] == L'"' && arg[i + 2] == L'"')
			{
				i += 3;

				std::wstring::size_type pos = arg.find(L"\"\"\"", i);
				if (pos != std::wstring::npos)
				{
					i = pos + 2;	// Skip "", loop will skip last "
				}
			}
		}
	}
	else
	{
		std::wstring error = L"Unknown bang: " + bang;
		Log(LOG_ERROR, error.c_str());
		return FALSE;
	}

	return TRUE;
}

/*
** ParseCommand
**
** Replaces the measure names with the actual text values.
**
*/
std::wstring CRainmeter::ParseCommand(const WCHAR* command, CMeterWindow* meterWindow)
{
	std::wstring strCommand = command;

	if (_wcsnicmp(L"!execute", command, 8) == 0)
	{
		return strCommand;
	}

	// Find the [measures]
	size_t start = 0, end = 0;
	while (start != std::wstring::npos && end != std::wstring::npos)
	{
		start = strCommand.find(L'[', start);
		if (start != std::wstring::npos)
		{
			end = strCommand.find(L']', start + 1);
			if (end != std::wstring::npos)
			{
				std::wstring measureName = strCommand.substr(start + 1, end - (start + 1));
				if (!measureName.empty() && measureName[0] != L'!')	// Ignore bangs
				{
					if (meterWindow)
					{
						if (strCommand[start + 1] == L'*' && strCommand[end - 1] == L'*')
						{
							strCommand.erase(start + 1, 1);
							strCommand.erase(end - 2, 1);
							start = end - 1;
						}
						else
						{
							const std::list<CMeasure*>& measures = meterWindow->GetMeasures();
							std::list<CMeasure*>::const_iterator iter = measures.begin();
							for ( ; iter != measures.end(); ++iter)
							{
								if (_wcsicmp((*iter)->GetName(), measureName.c_str()) == 0)
								{
									std::wstring value = (*iter)->GetStringValue(AUTOSCALE_OFF, 1, -1, false);
									strCommand.replace(start, (end - start) + 1, value);
									start += value.length();
									break;
								}
							}
							if (iter == measures.end())
							{
								//LogWithArgs(LOG_WARNING, L"No such measure [%s] for execute string: %s", measureName.c_str(), command);
								start = end + 1;
							}
						}
					}
				}
				else
				{
					start = end + 1;
				}
			}
		}
	}

	return strCommand;
}

/*
** ExecuteCommand
**
** Runs the given command or bang
**
*/
void CRainmeter::ExecuteCommand(const WCHAR* command, CMeterWindow* meterWindow)
{
	if (command == NULL) return;

	std::wstring strCommand = ParseCommand(command, meterWindow);

	if (!strCommand.empty())
	{
		command = strCommand.c_str();

		if (command[0] == L'!') // Bang
		{
			if (meterWindow)
			{
				// Fake WM_COPYDATA to deliver bangs
				COPYDATASTRUCT cds;
				cds.cbData = 1; // Size doesn't matter as long as not empty
				cds.dwData = 1;
				cds.lpData = (void*)command;
				meterWindow->OnCopyData(WM_COPYDATA, NULL, (LPARAM)&cds);
			}
			else
			{
				std::wstring bang, arg;
				size_t pos = strCommand.find(L' ');
				if (pos != std::wstring::npos)
				{
					bang.assign(strCommand, 0, pos);
					strCommand.erase(0, pos + 1);
					arg = strCommand;
				}
				else
				{
					bang = strCommand;
				}
				ExecuteBang(bang, arg, meterWindow);
			}
		}
		else
		{
			// Check for built-ins
			if (_wcsnicmp(L"PLAY", command, 4) == 0)
			{
				if (command[4] == L' ' ||                      // PLAY
					_wcsnicmp(L"LOOP ", &command[4], 5) == 0)  // PLAYLOOP
				{
					command += 4;	// Skip PLAY

					DWORD flags = SND_FILENAME | SND_ASYNC;

					if (command[0] != L' ')
					{
						flags |= SND_LOOP | SND_NODEFAULT;
						command += 4;	// Skip LOOP
					}

					++command;	// Skip the space
					if (command[0] != L'\0')
					{
						strCommand = command;

						// Strip the quotes
						std::wstring::size_type len = strCommand.length();
						if (len >= 2 && strCommand[0] == L'\"' && strCommand[len - 1] == L'\"')
						{
							len -= 2;
							strCommand.assign(strCommand, 1, len);
						}

						if (meterWindow)
						{
							meterWindow->MakePathAbsolute(strCommand);
						}

						PlaySound(strCommand.c_str(), NULL, flags);
					}
					return;
				}
				else if (_wcsnicmp(L"STOP", &command[4], 4) == 0)  // PLAYSTOP
				{
					PlaySound(NULL, NULL, SND_PURGE);
					return;
				}
			}

			// Run command
			RunCommand(NULL, command, SW_SHOWNORMAL);
		}
	}
}

/*
** ReadGeneralSettings
**
** Reads the general settings from the Rainmeter.ini file
**
*/
void CRainmeter::ReadGeneralSettings(const std::wstring& iniFile)
{
	WCHAR buffer[MAX_PATH];

	// Clear old settings
	m_DesktopWorkAreas.clear();

	CConfigParser parser;
	parser.Initialize(iniFile.c_str(), this);

	// Read Logging settings
	m_Logging = 0!=parser.ReadInt(L"Rainmeter", L"Logging", 0);
	m_Debug = 0!=parser.ReadInt(L"Rainmeter", L"Debug", 0);

	if (m_Logging)
	{
		StartLogging();
	}

	if (m_TrayWindow)
	{
		m_TrayWindow->ReadConfig(parser);
	}

	m_GlobalConfig.netInSpeed = parser.ReadFloat(L"Rainmeter", L"NetInSpeed", 0.0);
	m_GlobalConfig.netOutSpeed = parser.ReadFloat(L"Rainmeter", L"NetOutSpeed", 0.0);

	m_DisableDragging = 0!=parser.ReadInt(L"Rainmeter", L"DisableDragging", 0);
	m_DisableRDP = 0!=parser.ReadInt(L"Rainmeter", L"DisableRDP", 0);

	m_ConfigEditor = parser.ReadString(L"Rainmeter", L"ConfigEditor", L"");
	if (m_ConfigEditor.empty())
	{
		// Get the program path associated with .ini files
		DWORD cchOut = MAX_PATH;
		buffer[0] = L'\0';

		HRESULT hr = AssocQueryString(ASSOCF_NOTRUNCATE, ASSOCSTR_EXECUTABLE, L".ini", L"open", buffer, &cchOut);
		if (SUCCEEDED(hr) && cchOut > 0)
		{
			m_ConfigEditor = buffer;
		}
		else
		{
			m_ConfigEditor = L"Notepad";
		}
	}
	if (!m_ConfigEditor.empty() && m_ConfigEditor[0] != L'\"')
	{
		m_ConfigEditor.insert(0, L"\"");
		m_ConfigEditor.append(L"\"");
	}

	m_LogViewer = parser.ReadString(L"Rainmeter", L"LogViewer", L"");
	if (m_LogViewer.empty())
	{
		// Get the program path associated with .log files
		DWORD cchOut = MAX_PATH;
		buffer[0] = L'\0';

		HRESULT hr = AssocQueryString(ASSOCF_NOTRUNCATE, ASSOCSTR_EXECUTABLE, L".log", L"open", buffer, &cchOut);
		if (SUCCEEDED(hr) && cchOut > 0)
		{
			m_LogViewer = buffer;
		}
		else
		{
			m_LogViewer = L"Notepad";
		}
	}
	if (!m_LogViewer.empty() && m_LogViewer[0] != L'\"')
	{
		m_LogViewer.insert(0, L"\"");
		m_LogViewer.append(L"\"");
	}

	if (m_Debug)
	{
		LogWithArgs(LOG_NOTICE, L"ConfigEditor: %s", m_ConfigEditor.c_str());
		LogWithArgs(LOG_NOTICE, L"LogViewer: %s", m_LogViewer.c_str());
	}

	m_TrayExecuteL = parser.ReadString(L"Rainmeter", L"TrayExecuteL", L"", false);
	m_TrayExecuteR = parser.ReadString(L"Rainmeter", L"TrayExecuteR", L"", false);
	m_TrayExecuteM = parser.ReadString(L"Rainmeter", L"TrayExecuteM", L"", false);
	m_TrayExecuteDL = parser.ReadString(L"Rainmeter", L"TrayExecuteDL", L"", false);
	m_TrayExecuteDR = parser.ReadString(L"Rainmeter", L"TrayExecuteDR", L"", false);
	m_TrayExecuteDM = parser.ReadString(L"Rainmeter", L"TrayExecuteDM", L"", false);

	m_DisableVersionCheck = 0!=parser.ReadInt(L"Rainmeter", L"DisableVersionCheck", 0);

	std::wstring area = parser.ReadString(L"Rainmeter", L"DesktopWorkArea", L"");
	if (!area.empty())
	{
		m_DesktopWorkAreas[0] = parser.ParseRECT(area.c_str());
		m_DesktopWorkAreaChanged = true;
	}

	for (UINT i = 1; i <= CSystem::GetMonitorCount(); ++i)
	{
		_snwprintf_s(buffer, _TRUNCATE, L"DesktopWorkArea@%i", i);
		area = parser.ReadString(L"Rainmeter", buffer, L"");
		if (!area.empty())
		{
			m_DesktopWorkAreas[i] = parser.ParseRECT(area.c_str());
			m_DesktopWorkAreaChanged = true;
		}
	}

	m_DesktopWorkAreaType = 0!=parser.ReadInt(L"Rainmeter", L"DesktopWorkAreaType", 0);

	for (int i = 0, isize = (int)m_ConfigStrings.size(); i < isize; ++i)
	{
		int active  = parser.ReadInt(m_ConfigStrings[i].config.c_str(), L"Active", 0);

		// Make sure there is a ini file available
		if (active > 0 && active <= (int)m_ConfigStrings[i].iniFiles.size())
		{
			m_ConfigStrings[i].active = active;
		}

		int order = parser.ReadInt(m_ConfigStrings[i].config.c_str(), L"LoadOrder", 0);
		SetLoadOrder(i, order);
	}
}

/*
** RefreshAll
**
** Refreshes all active meter windows.
** Note: This function calls CMeterWindow::Refresh() directly for synchronization. Be careful about crash.
**
*/
void CRainmeter::RefreshAll()
{
	// Read skins and settings
	ReloadSettings();

	// Change the work area if necessary
	if (m_DesktopWorkAreaChanged)
	{
		UpdateDesktopWorkArea(false);
	}

	// Make the sending order by using LoadOrder
	std::multimap<int, CMeterWindow*> windows;
	GetMeterWindowsByLoadOrder(windows);

	// Prepare the helper window
	CSystem::PrepareHelperWindow();

	// Refresh all
	std::multimap<int, CMeterWindow*>::const_iterator iter = windows.begin();
	for ( ; iter != windows.end(); ++iter)
	{
		CMeterWindow* mw = (*iter).second;
		if (mw)
		{
			// Verify whether the cached information is valid
			int found = 0;
			const std::wstring& skinConfig = mw->GetSkinName();
			for (int i = 0, isize = (int)m_ConfigStrings.size(); i < isize; ++i)
			{
				if (_wcsicmp(skinConfig.c_str(), m_ConfigStrings[i].config.c_str()) == 0)
				{
					found = 1;
					const std::wstring& skinIniFile = mw->GetSkinIniFile();
					for (int j = 0, jsize = (int)m_ConfigStrings[i].iniFiles.size(); j < jsize; ++j)
					{
						if (_wcsicmp(skinIniFile.c_str(), m_ConfigStrings[i].iniFiles[j].c_str()) == 0)
						{
							found = 2;
							if (m_ConfigStrings[i].active != j + 1)
							{
								// Switch to new ini-file order
								m_ConfigStrings[i].active = j + 1;
								WriteActive(skinConfig, j);
							}
							break;
						}
					}

					if (found == 1)  // Not found in ini-files
					{
						DeactivateConfig(mw, i);

						std::wstring error = GetFormattedString(ID_STR_UNABLETOREFRESHSKIN, skinConfig.c_str(), skinIniFile.c_str());
						MessageBox(NULL, error.c_str(), APPNAME, MB_OK | MB_TOPMOST | MB_ICONEXCLAMATION);
					}
					break;
				}
			}

			if (found != 2)
			{
				if (found == 0)  // Not found in configs
				{
					DeactivateConfig(mw, -2);  // -2 = Deactivate the config forcibly

					std::wstring error = GetFormattedString(ID_STR_UNABLETOREFRESHSKIN, skinConfig.c_str(), L"");
					MessageBox(NULL, error.c_str(), APPNAME, MB_OK | MB_TOPMOST | MB_ICONEXCLAMATION);
				}
				continue;
			}

			try
			{
				mw->Refresh(false, true);
			}
			catch (CError& error)
			{
				LogError(error);
			}
		}
	}

	CDialogAbout::UpdateSkins();
	CDialogManage::UpdateSkins(NULL);
}

void CRainmeter::LoadTheme(const std::wstring& name)
{
	// Delete all meter windows
	DeleteMeterWindow(NULL, false);

	std::wstring backup = GetSettingsPath() + L"Themes\\Backup";
	CreateDirectory(backup.c_str(), NULL);
	backup += L"\\Rainmeter.thm";

	if (_wcsicmp(name.c_str(), L"Backup") == 0)
	{
		// Just load the backup
		CSystem::CopyFiles(backup, m_IniFile);
	}
	else
	{
		// Make a copy of current Rainmeter.ini
		CSystem::CopyFiles(m_IniFile, backup);

		// Replace Rainmeter.ini with theme
		std::wstring theme = Rainmeter->GetSettingsPath() + L"Themes\\";
		theme += name;
		std::wstring wallpaper = theme + L"\\RainThemes.bmp";
		theme += L"\\Rainmeter.thm";
		CSystem::CopyFiles(theme, Rainmeter->GetIniFile());

		PreserveSetting(backup, L"SkinPath");
		PreserveSetting(backup, L"ConfigEditor");
		PreserveSetting(backup, L"LogViewer");
		PreserveSetting(backup, L"Logging");
		PreserveSetting(backup, L"DisableVersionCheck");
		PreserveSetting(backup, L"Language");
		PreserveSetting(backup, L"TrayExecuteL", false);
		PreserveSetting(backup, L"TrayExecuteM", false);
		PreserveSetting(backup, L"TrayExecuteR", false);
		PreserveSetting(backup, L"TrayExecuteDM", false);
		PreserveSetting(backup, L"TrayExecuteDR", false);

		// Set wallpaper if it exists
		if (_waccess(wallpaper.c_str(), 0) != -1)
		{
			SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, (void*)wallpaper.c_str(), SPIF_UPDATEINIFILE);
		}
	}

	ReloadSettings();

	// Create meter windows for active configs
	ActivateActiveConfigs();
}

void CRainmeter::PreserveSetting(const std::wstring& from, LPCTSTR key, bool replace)
{
	WCHAR* buffer = new WCHAR[MAX_LINE_LENGTH];

	if ((replace || GetPrivateProfileString(L"Rainmeter", key, L"", buffer, 4, m_IniFile.c_str()) == 0) &&
		GetPrivateProfileString(L"Rainmeter", key, L"", buffer, MAX_LINE_LENGTH, from.c_str()) > 0)
	{
		WritePrivateProfileString(L"Rainmeter", key, buffer, m_IniFile.c_str());
	}

	delete [] buffer;
}

/*
** UpdateDesktopWorkArea
**
** Applies given DesktopWorkArea and DesktopWorkArea@n.
**
*/
void CRainmeter::UpdateDesktopWorkArea(bool reset)
{
	bool changed = false;

	if (reset)
	{
		if (!m_OldDesktopWorkAreas.empty())
		{
			for (size_t i = 0, isize = m_OldDesktopWorkAreas.size(); i < isize; ++i)
			{
				RECT r = m_OldDesktopWorkAreas[i];

				BOOL result = SystemParametersInfo(SPI_SETWORKAREA, 0, &r, 0);

				if (m_Debug)
				{
					std::wstring format = L"Resetting WorkArea@%i: L=%i, T=%i, R=%i, B=%i (W=%i, H=%i)";
					if (!result)
					{
						format += L" => FAIL";
					}
					LogWithArgs(LOG_DEBUG, format.c_str(), (int)i + 1, r.left, r.top, r.right, r.bottom, r.right - r.left, r.bottom - r.top);
				}
			}
			changed = true;
		}
	}
	else
	{
		const MULTIMONITOR_INFO& multimonInfo = CSystem::GetMultiMonitorInfo();
		const std::vector<MONITOR_INFO>& monitors = multimonInfo.monitors;

		if (m_OldDesktopWorkAreas.empty())
		{
			// Store old work areas for changing them back
			for (size_t i = 0; i < CSystem::GetMonitorCount(); ++i)
			{
				m_OldDesktopWorkAreas.push_back(monitors[i].work);
			}
		}

		if (m_Debug)
		{
			LogWithArgs(LOG_DEBUG, L"DesktopWorkAreaType: %s", m_DesktopWorkAreaType ? L"Margin" : L"Default");
		}

		for (UINT i = 0; i <= CSystem::GetMonitorCount(); ++i)
		{
			std::map<UINT, RECT>::const_iterator it = m_DesktopWorkAreas.find(i);
			if (it != m_DesktopWorkAreas.end())
			{
				RECT r = it->second;

				// Move rect to correct offset
				if (m_DesktopWorkAreaType)
				{
					RECT margin = r;
					r = (i == 0) ? monitors[multimonInfo.primary - 1].screen : monitors[i - 1].screen;
					r.left += margin.left;
					r.top += margin.top;
					r.right -= margin.right;
					r.bottom -= margin.bottom;
				}
				else
				{
					if (i != 0)
					{
						const RECT screenRect = monitors[i - 1].screen;
						r.left += screenRect.left;
						r.top += screenRect.top;
						r.right += screenRect.left;
						r.bottom += screenRect.top;
					}
				}

				BOOL result = SystemParametersInfo(SPI_SETWORKAREA, 0, &r, 0);
				if (result)
				{
					changed = true;
				}

				if (m_Debug)
				{
					std::wstring format = L"Applying DesktopWorkArea";
					if (i != 0)
					{
						WCHAR buffer[64];
						_snwprintf_s(buffer, _TRUNCATE, L"@%i", i);
						format += buffer;
					}
					format += L": L=%i, T=%i, R=%i, B=%i (W=%i, H=%i)";
					if (!result)
					{
						format += L" => FAIL";
					}
					LogWithArgs(LOG_DEBUG, format.c_str(), r.left, r.top, r.right, r.bottom, r.right - r.left, r.bottom - r.top);
				}
			}
		}
	}

	if (changed && CSystem::GetWindow())
	{
		// Update CSystem::MULTIMONITOR_INFO for for work area variables
		SendMessageTimeout(CSystem::GetWindow(), WM_SETTINGCHANGE, SPI_SETWORKAREA, 0, SMTO_ABORTIFHUNG, 1000, NULL);
	}
}

/*
** ReadStats
**
** Reads the statistics from the ini-file
**
*/
void CRainmeter::ReadStats()
{
	// If m_StatsFile doesn't exist, create it and copy the stats section from m_IniFile
	if (_waccess(m_StatsFile.c_str(), 0) == -1)
	{
		WCHAR* tmpSz = new WCHAR[SHRT_MAX];	// Max size returned by GetPrivateProfileSection()

		if (GetPrivateProfileSection(L"Statistics", tmpSz, SHRT_MAX, m_IniFile.c_str()) > 0)
		{
			WritePrivateProfileSection(L"Statistics", tmpSz, m_StatsFile.c_str());
			WritePrivateProfileString(L"Statistics", NULL, NULL, m_IniFile.c_str());
		}

		delete [] tmpSz;
	}

	// Only Net measure has stats at the moment
	CMeasureNet::ReadStats(m_StatsFile.c_str(), m_StatsDate);
}

/*
** WriteStats
**
** Writes the statistics to the ini-file. If bForce is false the stats are written only once per minute.
**
*/
void CRainmeter::WriteStats(bool bForce)
{
	static ULONGLONG lastWrite = 0;

	ULONGLONG ticks = CSystem::GetTickCount64();

	if (bForce || (lastWrite + 1000 * 60 < ticks))
	{
		lastWrite = ticks;

		// Only Net measure has stats at the moment
		CMeasureNet::WriteStats(m_StatsFile.c_str(), m_StatsDate.c_str());

		WritePrivateProfileString(NULL, NULL, NULL, m_StatsFile.c_str());
	}
}

/*
** ResetStats
**
** Clears the statistics
**
*/
void CRainmeter::ResetStats()
{
	// Set the stats-date string
	struct tm *newtime;
	time_t long_time;
	time(&long_time);
	newtime = localtime(&long_time);
	m_StatsDate = _wasctime(newtime);
	m_StatsDate.erase(m_StatsDate.size() - 1);

	// Only Net measure has stats at the moment
	CMeasureNet::ResetStats();
}

/*
** ShowContextMenu
**
** Opens the context menu in given coordinates.
**
*/
void CRainmeter::ShowContextMenu(POINT pos, CMeterWindow* meterWindow)
{
	if (!m_MenuActive)
	{
		m_MenuActive = true;

		// Show context menu, if no actions were executed
		HMENU menu = LoadMenu(m_ResourceInstance, MAKEINTRESOURCE(IDR_CONTEXT_MENU));

		if (menu)
		{
			HMENU subMenu = GetSubMenu(menu, 0);
			if (subMenu)
			{
				SetMenuDefaultItem(subMenu, ID_CONTEXT_MANAGE, MF_BYCOMMAND);

				if (_waccess(m_LogFile.c_str(), 0) == -1)
				{
					EnableMenuItem(subMenu, ID_CONTEXT_SHOWLOGFILE, MF_BYCOMMAND | MF_GRAYED);
					EnableMenuItem(subMenu, ID_CONTEXT_DELETELOGFILE, MF_BYCOMMAND | MF_GRAYED);
					EnableMenuItem(subMenu, ID_CONTEXT_STOPLOG, MF_BYCOMMAND | MF_GRAYED);
				}
				else
				{
					EnableMenuItem(subMenu, (m_Logging) ? ID_CONTEXT_STARTLOG : ID_CONTEXT_STOPLOG, MF_BYCOMMAND | MF_GRAYED);
				}

				if (m_Debug)
				{
					CheckMenuItem(subMenu, ID_CONTEXT_DEBUGLOG, MF_BYCOMMAND | MF_CHECKED);
				}

				HMENU configMenu = GetSubMenu(subMenu, 4);
				if (configMenu)
				{
					if (!m_ConfigMenu.empty())
					{
						DeleteMenu(configMenu, 0, MF_BYPOSITION);  // "No skins available" menuitem
						CreateConfigMenu(configMenu, m_ConfigMenu);
					}

					if (m_DisableDragging)
					{
						CheckMenuItem(configMenu, ID_CONTEXT_DISABLEDRAG, MF_BYCOMMAND | MF_CHECKED);
					}
				}

				HMENU themeMenu = GetSubMenu(subMenu, 5);
				if (themeMenu)
				{
					if (!m_Themes.empty())
					{
						DeleteMenu(themeMenu, 0, MF_BYPOSITION);  // "No themes available" menuitem
						CreateThemeMenu(themeMenu);
					}
				}

				if (meterWindow)
				{
					HMENU rainmeterMenu = subMenu;
					subMenu = CreateSkinMenu(meterWindow, 0, configMenu);

					WCHAR buffer[256];
					GetMenuString(menu, 0, buffer, 256, MF_BYPOSITION);
					InsertMenu(subMenu, 11, MF_BYPOSITION | MF_POPUP, (UINT_PTR)rainmeterMenu, buffer);
					InsertMenu(subMenu, 12, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
				}
				else
				{
					InsertMenu(subMenu, 12, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

					// Create a menu for all active configs
					std::map<std::wstring, CMeterWindow*>::const_iterator iter = Rainmeter->GetAllMeterWindows().begin();

					int index = 0;
					for (; iter != Rainmeter->GetAllMeterWindows().end(); ++iter)
					{
						CMeterWindow* mw = ((*iter).second);
						HMENU skinMenu = CreateSkinMenu(mw, index, configMenu);
						InsertMenu(subMenu, 12, MF_BYPOSITION | MF_POPUP, (UINT_PTR)skinMenu, mw->GetSkinName().c_str());
						++index;
					}

					// Put Update notifications in the Tray menu
					if (m_NewVersion)
					{
						InsertMenu(subMenu, 0, MF_BYPOSITION, ID_CONTEXT_NEW_VERSION, GetString(ID_STR_UPDATEAVAILABLE));
						HiliteMenuItem(Rainmeter->GetTrayWindow()->GetWindow(), subMenu, 0, MF_BYPOSITION | MF_HILITE);
						InsertMenu(subMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
					}
				}

				HWND hWnd = WindowFromPoint(pos);
				if (hWnd != NULL)
				{
					CMeterWindow* mw = GetMeterWindow(hWnd);
					if (mw)
					{
						// Cancel the mouse event beforehand
						mw->SetMouseLeaveEvent(true);
					}
				}

				// Set the window to foreground
				hWnd = meterWindow ? meterWindow->GetWindow() : m_TrayWindow->GetWindow();
				HWND hWndForeground = GetForegroundWindow();
				if (hWndForeground != hWnd)
				{
					DWORD foregroundThreadID = GetWindowThreadProcessId(hWndForeground, NULL);
					DWORD currentThreadID = GetCurrentThreadId();
					AttachThreadInput(currentThreadID, foregroundThreadID, TRUE);
					SetForegroundWindow(hWnd);
					AttachThreadInput(currentThreadID, foregroundThreadID, FALSE);
				}

				// Show context menu
				TrackPopupMenu(
				  subMenu,
				  TPM_RIGHTBUTTON | TPM_LEFTALIGN,
				  pos.x,
				  pos.y,
				  0,
				  hWnd,
				  NULL
				);

				if (meterWindow)
				{
					DestroyMenu(subMenu);
				}
			}

			DestroyMenu(menu);
		}

		m_MenuActive = false;
	}
}

HMENU CRainmeter::CreateConfigMenu(HMENU configMenu, std::vector<CONFIGMENU>& configMenuData)
{
	if (!configMenuData.empty())
	{
		if (!configMenu)
		{
			configMenu = CreatePopupMenu();
		}

		bool separator = false;
		for (int i = 0, j = 0, isize = (int)configMenuData.size(); i < isize; ++i)
		{
			if (configMenuData[i].index == -1)
			{
				HMENU submenu = CreateConfigMenu(NULL, configMenuData[i].children);
				if (submenu)
				{
					if (separator)
					{
						// Insert a separator
						InsertMenu(configMenu, i, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
						++j;
						separator = false;
					}
					InsertMenu(configMenu, i + j, MF_BYPOSITION | MF_POPUP, (UINT_PTR)submenu, configMenuData[i].name.c_str());
				}
			}
			else
			{
				CONFIG& config = m_ConfigStrings[configMenuData[i].index];
				InsertMenu(configMenu, i, MF_BYPOSITION | ((config.active == i + 1) ? MF_CHECKED : MF_UNCHECKED), config.commands[i], configMenuData[i].name.c_str());
				separator = true;
			}
		}

		return configMenu;
	}

	return NULL;
}

void CRainmeter::CreateThemeMenu(HMENU themeMenu)
{
	for (size_t i = 0, isize = m_Themes.size(); i < isize; ++i)
	{
		InsertMenu(themeMenu, i, MF_BYPOSITION, ID_THEME_FIRST + i, m_Themes[i].c_str());
	}
}

HMENU CRainmeter::CreateSkinMenu(CMeterWindow* meterWindow, int index, HMENU configMenu)
{
	HMENU skinMenu = LoadMenu(m_ResourceInstance, MAKEINTRESOURCE(IDR_SKIN_MENU));

	if (skinMenu)
	{
		HMENU subSkinMenu = GetSubMenu(skinMenu, 0);
		RemoveMenu(skinMenu, 0, MF_BYPOSITION);
		DestroyMenu(skinMenu);
		skinMenu = subSkinMenu;
	}

	if (skinMenu)
	{
		// Tick the position
		HMENU settingsMenu = GetSubMenu(skinMenu, 4);
		if (settingsMenu)
		{
			HMENU posMenu = GetSubMenu(settingsMenu, 0);
			if (posMenu)
			{
				switch (meterWindow->GetWindowZPosition())
				{
				case ZPOSITION_ONDESKTOP:
					CheckMenuItem(posMenu, ID_CONTEXT_SKINMENU_ONDESKTOP, MF_BYCOMMAND | MF_CHECKED);
					break;

				case ZPOSITION_ONBOTTOM:
					CheckMenuItem(posMenu, ID_CONTEXT_SKINMENU_BOTTOM, MF_BYCOMMAND | MF_CHECKED);
					break;

				case ZPOSITION_ONTOP:
					CheckMenuItem(posMenu, ID_CONTEXT_SKINMENU_TOPMOST, MF_BYCOMMAND | MF_CHECKED);
					break;

				case ZPOSITION_ONTOPMOST:
					CheckMenuItem(posMenu, ID_CONTEXT_SKINMENU_VERYTOPMOST, MF_BYCOMMAND | MF_CHECKED);
					break;

				default:
					CheckMenuItem(posMenu, ID_CONTEXT_SKINMENU_NORMAL, MF_BYCOMMAND | MF_CHECKED);
				}

				if (meterWindow->GetXFromRight()) CheckMenuItem(posMenu, ID_CONTEXT_SKINMENU_FROMRIGHT, MF_BYCOMMAND | MF_CHECKED);
				if (meterWindow->GetYFromBottom()) CheckMenuItem(posMenu, ID_CONTEXT_SKINMENU_FROMBOTTOM, MF_BYCOMMAND | MF_CHECKED);
				if (meterWindow->GetXPercentage()) CheckMenuItem(posMenu, ID_CONTEXT_SKINMENU_XPERCENTAGE, MF_BYCOMMAND | MF_CHECKED);
				if (meterWindow->GetYPercentage()) CheckMenuItem(posMenu, ID_CONTEXT_SKINMENU_YPERCENTAGE, MF_BYCOMMAND | MF_CHECKED);

				HMENU monitorMenu = GetSubMenu(posMenu, 0);
				if (monitorMenu)
				{
					CreateMonitorMenu(monitorMenu, meterWindow);
				}
			}

			// Tick the transparency
			if (!meterWindow->GetNativeTransparency())
			{
				EnableMenuItem(settingsMenu, 1, MF_BYPOSITION | MF_GRAYED);  // "Transparency" menu
				EnableMenuItem(settingsMenu, ID_CONTEXT_SKINMENU_CLICKTHROUGH, MF_BYCOMMAND | MF_GRAYED);
			}
			else
			{
				HMENU alphaMenu = GetSubMenu(settingsMenu, 1);
				if (alphaMenu)
				{
					int value = (int)(10 - meterWindow->GetAlphaValue() / 25.5);
					value = min(9, value);
					value = max(0, value);
					CheckMenuItem(alphaMenu, value, MF_BYPOSITION | MF_CHECKED);

					switch (meterWindow->GetWindowHide())
					{
					case HIDEMODE_FADEIN:
						CheckMenuItem(alphaMenu, ID_CONTEXT_SKINMENU_TRANSPARENCY_FADEIN, MF_BYCOMMAND | MF_CHECKED);
						EnableMenuItem(alphaMenu, ID_CONTEXT_SKINMENU_TRANSPARENCY_FADEOUT, MF_BYCOMMAND | MF_GRAYED);
						break;

					case HIDEMODE_FADEOUT:
						CheckMenuItem(alphaMenu, ID_CONTEXT_SKINMENU_TRANSPARENCY_FADEOUT, MF_BYCOMMAND | MF_CHECKED);
						EnableMenuItem(alphaMenu, ID_CONTEXT_SKINMENU_TRANSPARENCY_FADEIN, MF_BYCOMMAND | MF_GRAYED);
						break;

					case HIDEMODE_HIDE:
						EnableMenuItem(alphaMenu, ID_CONTEXT_SKINMENU_TRANSPARENCY_FADEIN, MF_BYCOMMAND | MF_GRAYED);
						EnableMenuItem(alphaMenu, ID_CONTEXT_SKINMENU_TRANSPARENCY_FADEOUT, MF_BYCOMMAND | MF_GRAYED);
						break;
					}
				}
			}

			// Tick the configs
			switch (meterWindow->GetWindowHide())
			{
			case HIDEMODE_HIDE:
				CheckMenuItem(settingsMenu, ID_CONTEXT_SKINMENU_HIDEONMOUSE, MF_BYCOMMAND | MF_CHECKED);
				break;

			case HIDEMODE_FADEIN:
			case HIDEMODE_FADEOUT:
				EnableMenuItem(settingsMenu, ID_CONTEXT_SKINMENU_HIDEONMOUSE, MF_BYCOMMAND | MF_GRAYED);
				break;
			}

			if (meterWindow->GetSnapEdges())
			{
				CheckMenuItem(settingsMenu, ID_CONTEXT_SKINMENU_SNAPTOEDGES, MF_BYCOMMAND | MF_CHECKED);
			}

			if (meterWindow->GetSavePosition())
			{
				CheckMenuItem(settingsMenu, ID_CONTEXT_SKINMENU_REMEMBERPOSITION, MF_BYCOMMAND | MF_CHECKED);
			}

			if (m_DisableDragging)
			{
				EnableMenuItem(settingsMenu, ID_CONTEXT_SKINMENU_DRAGGABLE, MF_BYCOMMAND | MF_GRAYED);
			}
			else if (meterWindow->GetWindowDraggable())
			{
				CheckMenuItem(settingsMenu, ID_CONTEXT_SKINMENU_DRAGGABLE, MF_BYCOMMAND | MF_CHECKED);
			}

			if (meterWindow->GetClickThrough())
			{
				CheckMenuItem(settingsMenu, ID_CONTEXT_SKINMENU_CLICKTHROUGH, MF_BYCOMMAND | MF_CHECKED);
			}

			if (meterWindow->GetKeepOnScreen())
			{
				CheckMenuItem(settingsMenu, ID_CONTEXT_SKINMENU_KEEPONSCREEN, MF_BYCOMMAND | MF_CHECKED);
			}
		}

		// Add the name of the Skin to the menu
		const std::wstring& skinName = meterWindow->GetSkinName();
		ModifyMenu(skinMenu, ID_CONTEXT_SKINMENU_OPENSKINSFOLDER, MF_BYCOMMAND, ID_CONTEXT_SKINMENU_OPENSKINSFOLDER, skinName.c_str());
		SetMenuDefaultItem(skinMenu, ID_CONTEXT_SKINMENU_OPENSKINSFOLDER, FALSE);

		// Remove dummy menuitem from the variants menu
		HMENU variantsMenu = GetSubMenu(skinMenu, 2);
		if (variantsMenu)
		{
			DeleteMenu(variantsMenu, 0, MF_BYPOSITION);
		}

		// Give the menuitem the unique id that depends on the skin
		ChangeSkinIndex(skinMenu, index);

		// Add the variants menu
		if (variantsMenu)
		{
			for (int i = 0, isize = (int)m_ConfigStrings.size(); i < isize; ++i)
			{
				const CONFIG& config = m_ConfigStrings[i];
				if (_wcsicmp(config.config.c_str(), skinName.c_str()) == 0)
				{
					for (int j = 0, jsize = (int)config.iniFiles.size(); j < jsize; ++j)
					{
						InsertMenu(variantsMenu, j, MF_BYPOSITION | ((config.active == j + 1) ? MF_CHECKED : MF_UNCHECKED), config.commands[j], config.iniFiles[j].c_str());
					}
					break;
				}
			}
		}

		// Add config's root menu
		int itemCount = GetMenuItemCount(configMenu);
		if (itemCount > 0)
		{
			std::wstring root = meterWindow->GetSkinName();
			std::wstring::size_type pos = root.find_first_of(L'\\');
			if (pos != std::wstring::npos)
			{
				root.erase(pos);
			}

			for (int i = 0; i < itemCount; ++i)
			{
				UINT state = GetMenuState(configMenu, i, MF_BYPOSITION);
				if (state == 0xFFFFFFFF || (state & MF_POPUP) == 0) break;

				WCHAR buffer[MAX_PATH];
				if (GetMenuString(configMenu, i, buffer, MAX_PATH, MF_BYPOSITION))
				{
					if (_wcsicmp(root.c_str(), buffer) == 0)
					{
						HMENU configRootMenu = GetSubMenu(configMenu, i);
						if (configRootMenu)
						{
							InsertMenu(skinMenu, 3, MF_BYPOSITION | MF_POPUP, (UINT_PTR)configRootMenu, root.c_str());
						}
						break;
					}
				}
			}
		}
	}

	return skinMenu;
}

void CRainmeter::CreateMonitorMenu(HMENU monitorMenu, CMeterWindow* meterWindow)
{
	bool screenDefined = meterWindow->GetXScreenDefined();
	int screenIndex = meterWindow->GetXScreen();

	// for the "Specified monitor" (@n)
	if (CSystem::GetMonitorCount() > 0)
	{
		const MULTIMONITOR_INFO& multimonInfo = CSystem::GetMultiMonitorInfo();
		const std::vector<MONITOR_INFO>& monitors = multimonInfo.monitors;

		for (int i = 0, isize = (int)monitors.size(); i < isize; ++i)
		{
			WCHAR buffer[64];
			_snwprintf_s(buffer, _TRUNCATE, L"@%i: ", i + 1);
			std::wstring item = buffer;

			size_t len = wcslen(monitors[i].monitorName);
			if (len > 32)
			{
				item += std::wstring(monitors[i].monitorName, 32);
				item += L"...";
			}
			else
			{
				item += monitors[i].monitorName;
			}

			InsertMenu(monitorMenu,
				i + 3,
				MF_BYPOSITION | ((screenDefined && screenIndex == i + 1) ? MF_CHECKED : MF_UNCHECKED) | ((!monitors[i].active) ? MF_GRAYED : MF_ENABLED),
				ID_MONITOR_FIRST + i + 1,
				item.c_str());
		}
	}

	// Tick the configs
	if (!screenDefined)
	{
		CheckMenuItem(monitorMenu, ID_CONTEXT_SKINMENU_MONITOR_PRIMARY, MF_BYCOMMAND | MF_CHECKED);
	}

	if (screenDefined && screenIndex == 0)
	{
		CheckMenuItem(monitorMenu, ID_MONITOR_FIRST, MF_BYCOMMAND | MF_CHECKED);
	}

	if (meterWindow->GetAutoSelectScreen())
	{
		CheckMenuItem(monitorMenu, ID_CONTEXT_SKINMENU_MONITOR_AUTOSELECT, MF_BYCOMMAND | MF_CHECKED);
	}
}

void CRainmeter::ChangeSkinIndex(HMENU menu, int index)
{
	int count = GetMenuItemCount(menu);

	for (int i = 0; i < count; ++i)
	{
		HMENU subMenu = GetSubMenu(menu, i);
		if (subMenu)
		{
			ChangeSkinIndex(subMenu, index);
		}
		else
		{
			WCHAR buffer[256];
			GetMenuString(menu, i, buffer, 256, MF_BYPOSITION);
			UINT id = GetMenuItemID(menu, i);
			UINT flags = GetMenuState(menu, i, MF_BYPOSITION);
			ModifyMenu(menu, i, MF_BYPOSITION | flags, id | (index << 16), buffer);
		}
	}
}

void CRainmeter::StartLogging()
{
	// Check if the file exists
	if (_waccess(m_LogFile.c_str(), 0) == -1)
	{
		// Create log file
		HANDLE file = CreateFile(m_LogFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			CloseHandle(file);
			ResetLoggingFlag();	// Re-enable logging
			SetLogging(true);

			std::wstring text = GetFormattedString(ID_STR_LOGFILECREATED, m_LogFile.c_str());
			MessageBox(NULL, text.c_str(), APPNAME, MB_OK | MB_TOPMOST | MB_ICONINFORMATION);
		}
		else
		{
			// Disable logging
			SetLogging(false);
			ResetLoggingFlag();
	
			std::wstring text = GetFormattedString(ID_STR_LOGFILECREATEFAIL, m_LogFile.c_str());
			MessageBox(NULL, text.c_str(), APPNAME, MB_OK | MB_TOPMOST | MB_ICONERROR);
		}
	}
	else
	{
		SetLogging(true);
	}
}

void CRainmeter::StopLogging()
{
	SetLogging(false);
}

void CRainmeter::DeleteLogFile()
{
	// Check if the file exists
	if (_waccess(m_LogFile.c_str(), 0) != -1)
	{
		std::wstring text = GetFormattedString(ID_STR_LOGFILEDELETE, m_LogFile.c_str());
		int res = MessageBox(NULL, text.c_str(), APPNAME, MB_YESNO | MB_TOPMOST | MB_ICONQUESTION);
		if (res == IDYES)
		{
			// Disable logging
			SetLogging(false);
			ResetLoggingFlag();

			CSystem::RemoveFile(m_LogFile);
		}
	}
}

void CRainmeter::AddAboutLogInfo(int level, LPCWSTR time, LPCWSTR message)
{
	// Store 20 last items
	LOG_INFO logInfo = {level, time, message};
	m_LogData.push_back(logInfo);
	if (m_LogData.size() > 20)
	{
		m_LogData.pop_front();
	}

	CDialogAbout::AddLogItem(level, time, message);
}

void CRainmeter::SetLogging(bool logging)
{
	m_Logging = logging;
	WritePrivateProfileString(L"Rainmeter", L"Logging", logging ? L"1" : L"0", m_IniFile.c_str());
}

void CRainmeter::SetDebug(bool debug)
{
	m_Debug = debug;
	WritePrivateProfileString(L"Rainmeter", L"Debug", debug ? L"1" : L"0", m_IniFile.c_str());
}

void CRainmeter::SetDisableDragging(bool dragging)
{
	m_DisableDragging = dragging;
	WritePrivateProfileString(L"Rainmeter", L"DisableDragging", dragging ? L"1" : L"0", m_IniFile.c_str());
}

void CRainmeter::SetDisableVersionCheck(bool check)
{
	m_DisableVersionCheck = check;
	WritePrivateProfileString(L"Rainmeter", L"DisableVersionCheck", check ? L"1" : L"0" , m_IniFile.c_str());
}

void CRainmeter::TestSettingsFile(bool bDefaultIniLocation)
{
	WritePrivateProfileString(L"Rainmeter", L"WriteTest", L"TRUE", m_IniFile.c_str());
	WritePrivateProfileString(NULL, NULL, NULL, m_IniFile.c_str());	// FLUSH

	WCHAR tmpSz[5];
	bool bSuccess = (GetPrivateProfileString(L"Rainmeter", L"WriteTest", L"", tmpSz, 5, m_IniFile.c_str()) > 0);
	if (bSuccess)
	{
		bSuccess = (wcscmp(L"TRUE", tmpSz) == 0);
		WritePrivateProfileString(L"Rainmeter", L"WriteTest", NULL, m_IniFile.c_str());
	}
	if (!bSuccess)
	{
		std::wstring error = GetString(ID_STR_SETTINGSNOTWRITABLE);

		if (!bDefaultIniLocation)
		{
			std::wstring strTarget = L"%APPDATA%\\Rainmeter\\";
			ExpandEnvironmentVariables(strTarget);

			error += GetFormattedString(ID_STR_SETTINGSMOVEFILE, m_IniFile.c_str(), strTarget.c_str());
		}
		else
		{
			error += GetFormattedString(ID_STR_SETTINGSREADONLY, m_IniFile.c_str());
		}

		MessageBox(NULL, error.c_str(), APPNAME, MB_OK | MB_ICONERROR);
	}
}

std::wstring CRainmeter::ExtractPath(const std::wstring& strFilePath)
{
	std::wstring::size_type pos = strFilePath.find_last_of(L"\\/");
	if (pos != std::wstring::npos)
	{
		return strFilePath.substr(0, pos + 1);
	}
	return L".\\";
}

void CRainmeter::ExpandEnvironmentVariables(std::wstring& strPath)
{
	if (strPath.find(L'%') != std::wstring::npos)
	{
		DWORD bufSize = 4096;
		WCHAR* buffer = new WCHAR[bufSize];	// lets hope the buffer is large enough...

		// %APPDATA% is a special case
		std::wstring::size_type pos = strPath.find(L"%APPDATA%");
		if (pos != std::wstring::npos)
		{
			HRESULT hr = SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, buffer);
			if (SUCCEEDED(hr))
			{
				std::wstring path = buffer;
				do
				{
					strPath.replace(pos, 9, path);
				}
				while ((pos = strPath.find(L"%APPDATA%", pos + path.length())) != std::wstring::npos);
			}
		}

		if (strPath.find(L'%') != std::wstring::npos)
		{
			// Expand the environment variables
			DWORD ret = ExpandEnvironmentStrings(strPath.c_str(), buffer, bufSize);
			if (ret != 0 && ret < bufSize)
			{
				strPath = buffer;
			}
			else
			{
				LogWithArgs(LOG_WARNING, L"Unable to expand environment strings in: %s", strPath.c_str());
			}
		}

		delete [] buffer;
	}
}
