default: cleanall tftps

cleanall:
	rm -rf tftps

tftps: tftpServer.c tftpServer.h
	gcc tftpServer.c -o tftps
