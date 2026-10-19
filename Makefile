# Windows 使用 MinGW-w64 的 mingw32-make，或其他 GNU make。
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS = -Iinclude
SOURCES = $(wildcard src/*.c)
HEADERS = $(wildcard include/*.h)
TARGET = smart-lab.exe
PYTHON = python

.PHONY: all test test-p0 test-p1 test-p2 test-p3
all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) -o $@

test: test-p0 test-p1 test-p2 test-p3

test-p0: $(TARGET)
	$(PYTHON) tests/test_p0.py --executable $(TARGET) --cc "$(CC)"

test-p1: $(TARGET)
	$(PYTHON) tests/test_p1.py --executable $(TARGET) --cc "$(CC)"

test-p2: $(TARGET)
	$(PYTHON) tests/test_p2.py --executable $(TARGET) --cc "$(CC)"

test-p3: $(TARGET)
	$(PYTHON) tests/test_p3.py --executable $(TARGET) --cc "$(CC)"

# 不提供批量删除的 clean 目标；测试输出与运行数据由使用者按需保留。
