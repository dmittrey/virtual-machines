# Compiler and flags
CXX      := g++
CXXFLAGS := -O2 -std=c++17 -Wall -Wextra -Werror
DBGFLAGS := -O0 -g3 -fsanitize=address -fno-omit-frame-pointer -std=c++17 -Wall -Wextra -Werror

# Targets
TARGET   := l1-info
SRC      := l1-cache-info.cc

.PHONY: all clean debug

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

debug: $(SRC)
	$(CXX) $(DBGFLAGS) -o $(TARGET)-debug $<

clean:
	rm -f $(TARGET) $(TARGET)-debug
