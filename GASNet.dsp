# Microsoft Developer Studio Project File - Name="GASNet" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=GASNet - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "GASNet.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "GASNet.mak" CFG="GASNet - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "GASNet - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "GASNet - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""$/Ti/GASNet", RACAAAAA"
# PROP Scc_LocalPath "."
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "GASNet - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386

!ELSEIF  "$(CFG)" == "GASNet - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept

!ENDIF 

# Begin Target

# Name "GASNet - Win32 Release"
# Name "GASNet - Win32 Debug"
# Begin Group "top-level"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\gasnet.h
# End Source File
# Begin Source File

SOURCE=.\gasnet_basic.h
# End Source File
# Begin Source File

SOURCE=.\gasnet_handler.h
# End Source File
# Begin Source File

SOURCE=.\gasnet_help.h
# End Source File
# Begin Source File

SOURCE=.\gasnet_internal.c
# End Source File
# Begin Source File

SOURCE=.\gasnet_internal.h
# End Source File
# Begin Source File

SOURCE=.\README
# End Source File
# End Group
# Begin Group "mpi-conduit"

# PROP Default_Filter ""
# Begin Source File

SOURCE=".\mpi-conduit\gasnet_core.c"
# End Source File
# Begin Source File

SOURCE=".\mpi-conduit\gasnet_core.h"
# End Source File
# Begin Source File

SOURCE=".\mpi-conduit\gasnet_core_fwd.h"
# End Source File
# Begin Source File

SOURCE=".\mpi-conduit\gasnet_core_help.h"
# End Source File
# Begin Source File

SOURCE=".\mpi-conduit\gasnet_core_internal.h"
# End Source File
# Begin Source File

SOURCE=".\mpi-conduit\Makefile.am"
# End Source File
# End Group
# Begin Group "template-conduit"

# PROP Default_Filter ""
# Begin Source File

SOURCE=".\template-conduit\gasnet_core.c"
# End Source File
# Begin Source File

SOURCE=".\template-conduit\gasnet_core.h"
# End Source File
# Begin Source File

SOURCE=".\template-conduit\gasnet_core_fwd.h"
# End Source File
# Begin Source File

SOURCE=".\template-conduit\gasnet_core_help.h"
# End Source File
# Begin Source File

SOURCE=".\template-conduit\gasnet_core_internal.h"
# End Source File
# Begin Source File

SOURCE=".\template-conduit\Makefile.am"
# End Source File
# Begin Source File

SOURCE=".\template-conduit\README"
# End Source File
# End Group
# Begin Group "extended-ref"

# PROP Default_Filter ""
# Begin Source File

SOURCE=".\extended-ref\gasnet_extended.c"
# End Source File
# Begin Source File

SOURCE=".\extended-ref\gasnet_extended.h"
# End Source File
# Begin Source File

SOURCE=".\extended-ref\gasnet_extended_fwd.h"
# End Source File
# Begin Source File

SOURCE=".\extended-ref\gasnet_extended_help.h"
# End Source File
# Begin Source File

SOURCE=".\extended-ref\gasnet_extended_internal.h"
# End Source File
# End Group
# Begin Group "tests"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\tests\Makefile
# End Source File
# Begin Source File

SOURCE=.\tests\test.h
# End Source File
# Begin Source File

SOURCE=.\tests\testbarrier.c
# End Source File
# Begin Source File

SOURCE=.\tests\testgasnet.c
# End Source File
# Begin Source File

SOURCE=.\tests\testhsl.c
# End Source File
# Begin Source File

SOURCE=.\tests\testlarge.c
# End Source File
# Begin Source File

SOURCE=.\tests\testsmall.c
# End Source File
# End Group
# Begin Group "Configuration"

# PROP Default_Filter ""
# Begin Group "config-aux"

# PROP Default_Filter ""
# Begin Source File

SOURCE=".\config-aux\config.guess"
# End Source File
# Begin Source File

SOURCE=".\config-aux\config.sub"
# End Source File
# Begin Source File

SOURCE=".\config-aux\install-sh"
# End Source File
# Begin Source File

SOURCE=".\config-aux\Makefile.am"
# End Source File
# Begin Source File

SOURCE=".\config-aux\missing"
# End Source File
# Begin Source File

SOURCE=".\config-aux\mkinstalldirs"
# End Source File
# End Group
# Begin Group "other"

# PROP Default_Filter ""
# Begin Group "AMMPI"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\other\ammpi\Makefile.am
# End Source File
# End Group
# Begin Source File

SOURCE=.\other\Makefile.am
# End Source File
# End Group
# Begin Source File

SOURCE=.\acconfig.h
# End Source File
# Begin Source File

SOURCE=.\acinclude.m4
# End Source File
# Begin Source File

SOURCE=.\Bootstrap
# End Source File
# Begin Source File

SOURCE=.\configure.in
# End Source File
# Begin Source File

SOURCE=.\Makefile.am
# End Source File
# Begin Source File

SOURCE=.\unBootstrap
# End Source File
# End Group
# End Target
# End Project
