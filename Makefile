# @echo $(CURDIR)
CMAKE_CXX_FLAGS = -std=c++11
TARGET = server client
OBJS = timer.o UDPServer.o
LIBS = -lpthread -lrt
# echo:
# 	@echo $(CXX)
ALL:${TARGET}
server:$(OBJS) UDPPackage.h
	$(CXX) -o $@ $(OBJS) $(CMAKE_CXX_FLAGS) $(LIBS)
#timer.o:timer.cpp timer.h queue.h assertions.h

#UDPServer.o:UDPServer.cpp timer.h queue.h assertions.h
%.o:%.cpp timer.h queue.h assertions.h UDPPackage.h
	$(CXX) -o $@ -c $< $(CMAKE_CXX_FLAGS)

server-simple:UDP-server-test1.o timer.o
	$(CXX) -o $@ $^ $(CMAKE_CXX_FLAGS) $(LIBS)
client:UDPClient.o 
	$(CXX) -o $@ $^ $(CMAKE_CXX_FLAGS)
clean:
	rm -rf $(OBJS) UDPClient.o server client

.PHONY:ALL