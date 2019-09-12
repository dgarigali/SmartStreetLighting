all:
	javac -classpath ./RXTXcomm.jar main/*.java
	jar cfm exec.jar META-INF/MANIFEST.MF main/*.class
	rm main/*.class
	java -Djava.library.path=. -jar exec.jar