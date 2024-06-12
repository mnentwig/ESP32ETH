# use under MINGW
all: windowsTestapp.exe escAlg.exe
windowsTestapp.exe: windowsTestapp.c
	g++ -Wall -pedantic -o windowsTestapp.exe windowsTestapp.c -lWs2_32 -O -static

escAlg.exe: escAlg.c
	g++ -Wall -pedantic -o escAlg.exe escAlg.c -O -static

alg:	escAlg.exe
	./escAlg.exe slow.txt > slowFiltered.txt
	./escAlg.exe midLow.txt > midLowFiltered.txt
	./escAlg.exe midHigh.txt > midHighFiltered.txt
	./escAlg.exe full.txt > fullFiltered.txt

clean:
	rm windowsTestapp.exe escAlg.exe
.PHONY: clean alg
