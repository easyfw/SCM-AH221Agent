#include <SysUtils.hpp>
#include <SvcMgr.hpp>
#pragma hdrstop
#define Application Svcmgr::Application
USEFORM("SvcController.cpp", SCM_AH221Agent); /* TService: File Type */
//---------------------------------------------------------------------------
WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	try
	{
		Application->Initialize();
		Application->CreateForm(__classid(TSCM_AH221Agent), &SCM_AH221Agent);
		Application->Run();
	}
	catch (Exception &exception)
	{
		Sysutils::ShowException(&exception, System::ExceptAddr());
	}
        catch(...)
        {
		try
		{
	        	throw Exception("");
		}
		catch(Exception &exception)
		{
			Sysutils::ShowException(&exception, System::ExceptAddr());
		}
        }
	return 0;
}
