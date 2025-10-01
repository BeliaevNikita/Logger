LLVM_CONFIG := llvm-config
CXX := clang++
CXXFLAGS := -fPIC -std=c++17 `$(LLVM_CONFIG) --cxxflags`
LDFLAGS := -shared `$(LLVM_CONFIG) --ldflags --libs core` -Wl,-z,relro


all: libinstrument.so logger_runtime.o llvm pass run def_visual


libinstrument.so: llvm_logging_instrumentation.cpp
	$(CXX) $(CXXFLAGS) -o $@ -shared llvm_logging_instrumentation.cpp $(LDFLAGS)


logger_runtime.o: logger_runtime.cpp
	$(CXX) -std=c++17 -c logger_runtime.cpp -o logger_runtime.o

llvm:
	clang -O0 -Xclang -disable-O0-optnone -S -emit-llvm test.c -o test.ll

pass:
	opt-14 -load-pass-plugin ./libinstrument.so -passes="instrument-pass" -S test.ll -o test_instrumented.ll

run:	
	clang++ test_instrumented.ll logger_runtime.o -o test_run -pthread

def_visual:
	dot -Tpng defuse.dot -o defuse.png

cfg_visual:
	dot -Tpng cfg.dot -o cfg.png

graph:
	dot -Tpng cfg_with_vals.dot -o cfg_with_vals.png

full_graph:
	dot -Tpng cfg_with_vals.dot -o cfg_with_vals.png
	
clean:
	rm -f libinstrument.so logger_runtime.o *.dot *.ll *.png log.txt