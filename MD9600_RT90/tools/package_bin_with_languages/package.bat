Pushd ..\..\MD9600_firmware\application\include\user_interface\languages\src
gcc -Wall -O2 -I../ -o languages_builder languages_builder.c
languages_builder.exe
popd

tar.exe -a -c -f ..\..\MD9600_firmware\MD9600_HW_V1\MD9600_firmware_HW_V1.zip -C ..\..\MD9600_firmware\MD9600_HW_V1 MD9600_firmware_V1HW.bin -C ..\..\MD9600_firmware\application\include\user_interface\languages\src\ *.gla

tar.exe -a -c -f ..\..\MD9600_firmware\MD9600_HW_V2\MD9600_firmware_HW_V2.zip -C ..\..\MD9600_firmware\MD9600_HW_V2 MD9600_firmware_V2HW.bin -C ..\..\MD9600_firmware\application\include\user_interface\languages\src\ *.gla

tar.exe -a -c -f ..\..\MD9600_firmware\MD9600_HW_V4\MD9600_firmware_HW_V4.zip -C ..\..\MD9600_firmware\MD9600_HW_V4 MD9600_firmware_V4HW.bin -C ..\..\MD9600_firmware\application\include\user_interface\languages\src\ *.gla

tar.exe -a -c -f ..\..\MD9600_firmware\MD9600_HW_V5\MD9600_firmware_HW_V5.zip -C ..\..\MD9600_firmware\MD9600_HW_V5 MD9600_firmware_V5HW.bin -C ..\..\MD9600_firmware\application\include\user_interface\languages\src\ *.gla


tar.exe -a -c -f ..\..\MD9600_firmware\JA_MD9600_HW_V1\Japanese_MD9600_firmware_HW_V1.zip -C ..\..\MD9600_firmware\JA_MD9600_HW_V1 MD9600_firmware_V1HW_Japanese.bin -C ..\..\MD9600_firmware\application\include\user_interface\languages\src\ *.gla

tar.exe -a -c -f ..\..\MD9600_firmware\JA_MD9600_HW_V2\Japanese_MD9600_firmware_HW_V2.zip -C ..\..\MD9600_firmware\JA_MD9600_HW_V2 MD9600_firmware_V2HW_Japanese.bin -C ..\..\MD9600_firmware\application\include\user_interface\languages\src\ *.gla

tar.exe -a -c -f ..\..\MD9600_firmware\JA_MD9600_HW_V4\Japanese_MD9600_firmware_HW_V4.zip -C ..\..\MD9600_firmware\JA_MD9600_HW_V4 MD9600_firmware_V4HW_Japanese.bin -C ..\..\MD9600_firmware\application\include\user_interface\languages\src\ *.gla

tar.exe -a -c -f ..\..\MD9600_firmware\JA_MD9600_HW_V5\Japanese_MD9600_firmware_HW_V5.zip -C ..\..\MD9600_firmware\JA_MD9600_HW_V5 MD9600_firmware_V5HW_Japanese.bin -C ..\..\MD9600_firmware\application\include\user_interface\languages\src\ *.gla

pause