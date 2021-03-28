ctrl:
	mkdir -p bin
	gcc cred1ctrl.c cred1struct.c -o bin/cred1ctrl -I/opt/EDTpdv /opt/EDTpdv/libpdv.a -lm -lpthread -ldl

acqu:
	mkdir -p bin
	gcc cred1acqu.c cred1struct.c -o bin/cred1acqu -I/opt/EDTpdv -I/home/scexao/src/cacao/src/ImageStreamIO -I/home/scexao/src/cacao/src /home/scexao/src/cacao/src/ImageStreamIO/ImageStreamIO.c /opt/EDTpdv/libpdv.a -lm -lpthread -ldl 

clean:
	rm -rf *~
	rm -f cred1ctrl
	rm -f *.o
	rm -f cred1acqu
