all: windowsTestappCamera.exe
windowsTestappCamera.exe: windowsTestappCamera.cpp jpgd.cpp jpgd.h threads.h
	#fltk-config --compile windowsTestappCamera.cpp
	g++ -O3 -owindowsTestappCamera.exe windowsTestappCamera.cpp -lws2_32 -lfltk 
clean:
	rm windowsTestappCamera.exe
.PHONY: clean
