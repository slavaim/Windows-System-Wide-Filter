build -cgwF

%DDK_7600_ROOT%\bin\SelfSign\signtool.exe sign /v /ac MSCV-GlobalSign.cer /s my /n "  Inc" /t http://timestamp.verisign.com/scripts/timestamp.dll .\o_hooker\o_hooker\objchk_wnet_amd64\amd64\ochooker.sys
%DDK_7600_ROOT%\bin\SelfSign\signtool.exe sign /v /ac MSCV-GlobalSign.cer /s my /n "  Inc" /t http://timestamp.verisign.com/scripts/timestamp.dll .\o_core\o_core\objchk_wnet_amd64\amd64\ocore.sys 
%DDK_7600_ROOT%\bin\SelfSign\signtool.exe sign /v /ac MSCV-GlobalSign.cer /s my /n "  Inc" /t http://timestamp.verisign.com/scripts/timestamp.dll .\o_filter\o_filter\objchk_wnet_amd64\amd64\opnpdevfilter.sys
%DDK_7600_ROOT%\bin\SelfSign\signtool.exe sign /v /ac MSCV-GlobalSign.cer /s my /n "  Inc" /t http://timestamp.verisign.com/scripts/timestamp.dll .\PnPEmulator\objchk_wnet_amd64\amd64\DLDriverPnPMan.sys

copy  .\o_hooker\o_hooker\objchk_wnet_amd64\amd64\ochooker.sys E:\Exchange\Debug\o\
copy  .\o_core\o_core\objchk_wnet_amd64\amd64\ocore.sys E:\Exchange\Debug\o\
copy  .\o_filter\o_filter\objchk_wnet_amd64\amd64\opnpdevfilter.sys E:\Exchange\Debug\o\
copy  .\PnPEmulator\objchk_wnet_amd64\amd64\DLDriverPnPMan.sys E:\Exchange\Debug\o\
copy  .\o_core\o_core\ocore.inf E:\Exchange\Debug\o\