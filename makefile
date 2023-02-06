TARGET = ./app/test
SRC = ./signal/signal.cpp ./app/config_c.cpp ./app/main.cpp ./proc/daemon.cpp ./logs/logs.cpp ./proc/process.cpp ./net/httpconn.cpp   
CXXFLAGS = -I./include/

$(TARGET):$(SRC)
	$(CXX) -pthread $(CXXFLAGS) $^ -o $@ 

clean:
	$(RM) $(SRC)