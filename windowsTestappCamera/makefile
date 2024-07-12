all: windowsTestappCamera.exe
windowsTestappCamera.exe: windowsTestappCamera.cpp jpgd.cpp jpgd.h threads.h
#fltk-config --compile windowsTestappCamera.cpp
#g++ -O3 -owindowsTestappCamera.exe windowsTestappCamera.cpp -lws2_32 -lfltk
#expanding --ldstaticflags gives
	g++ -O3 -static -owindowsTestappCamera.exe windowsTestappCamera.cpp -pipe -Wl,-subsystem,windows -mwindows /mingw64/lib/libfltk.a -lole32 -luuid -lcomctl32 -lws2_32

clean:
	rm windowsTestappCamera.exe
.PHONY: clean
