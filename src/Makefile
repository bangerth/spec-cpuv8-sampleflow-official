SRC = $(shell echo mcmc-laplace.cc ; \
              find dealii/source -name '*.cc' ; \
              find dealii/bundled/boost-1.70.0/libs -name '*.cpp')
OBJ = $(subst .cpp,.o,$(subst .cc,.o,$(SRC)))

INCLUDEDIRS = -Idealii/include \
              -Idealii/bundled/boost-1.70.0/include \
              -ISampleFlow/include

%.o : %.cc
	c++ -std=c++17 -O3 -ggdb -c $< -o $@ $(INCLUDEDIRS)

%.o : %.cpp
	c++ -std=c++17 -O3 -ggdb -c $< -o $@ $(INCLUDEDIRS)

sample-flow: $(OBJ)
	c++ -o $@ $(OBJ) -lpthread -fuse-ld=mold

clean:
	-rm -f $(OBJ) sample-flow
